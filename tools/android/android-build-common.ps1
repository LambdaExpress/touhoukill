# Shared implementation of the Android (arm64-v8a) APK builds.
#
# Dot-sourced by build-android-debug.ps1 and build-android-release.ps1; this file is
# not an entry point and does nothing on its own. The two entry scripts only pick the
# configuration and the signing material, so the steps below stay in one place and
# cannot drift apart between the two variants.
#
# Toolchain: the Qt 5.12.12 Android arm64 kit, NDK r19c, SDK platform android-28 and
# JDK 8 -- the combination Qt 5.12 was released against, and the same NDK that
# tools/android/build-3rdparty.ps1 cross-compiles the native libraries with.

Set-StrictMode -Version Latest

function Write-Step {
    param([string]$Text)
    Write-Host ''
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Assert-Path {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path $Path)) { throw "$Description not found: $Path" }
}

# The generated Makefile embeds these paths in command lines that are executed by a
# POSIX shell, where a backslash is an escape character. Forward slashes are accepted
# by every tool involved, so the values are normalised here.
function ConvertTo-PosixPath {
    param([string]$Path)
    return $Path.Replace('\', '/')
}

# Reads the keystore.properties file that holds the release signing material. Kept
# outside the repository so that the keystore and its passwords never enter git.
function Read-AndroidKeystoreProperties {
    param([string]$Path)

    $values = @{}
    foreach ($line in Get-Content $Path) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) { continue }
        $separator = $trimmed.IndexOf('=')
        if ($separator -lt 1) { throw "Malformed line in ${Path}: $line" }
        $values[$trimmed.Substring(0, $separator).Trim()] = $trimmed.Substring($separator + 1).Trim()
    }
    return $values
}

# Locates apksigner for verifying the signed APK. The newest installed revision is
# used, which is the one androiddeployqt signs with: qmake records the highest
# available build-tools in the deployment settings. The wrapper in build-tools 28.0.3
# is unusable here in any case, because it looks for the removed
# tools/lib/find_java.bat and exits with status 0 without doing anything.
function Find-ApkSigner {
    param([string]$SdkRoot)

    $buildTools = Join-Path $SdkRoot 'build-tools'
    if (-not (Test-Path $buildTools)) { return $null }

    $candidates = Get-ChildItem $buildTools -Directory |
        Where-Object { $_.Name -match '^\d+(\.\d+)*$' } |
        Sort-Object { [version]$_.Name } -Descending

    foreach ($candidate in $candidates) {
        $executable = Join-Path $candidate.FullName 'apksigner.bat'
        if (Test-Path $executable) { return $executable }
    }
    return $null
}

function Invoke-AndroidBuild {
    [CmdletBinding()]
    param(
        # Selects the native configuration and the Gradle packaging variant.
        [Parameter(Mandatory)]
        [ValidateSet('debug', 'release')]
        [string]$Configuration,

        # Shadow build and packaging directory for this configuration.
        [Parameter(Mandatory)]
        [string]$BuildRoot,

        # Qt installation that provides the Android arm64 kit.
        [string]$QtDir = 'D:\tools\Qt\5.12.12\android_arm64_v8a',

        # Host Qt installation that provides the build tools (mingw32-make).
        [string]$HostQtDir = 'D:\tools\Qt',

        # NDK r19c root.
        [string]$NdkRoot = 'D:\tools\android-qt512\ndk\android-ndk-r19c',

        # Android SDK root; defaults to the per-user SDK location.
        [string]$SdkRoot = (Join-Path $env:LOCALAPPDATA 'Android\Sdk'),

        # JDK 8 root; Gradle 4.6 rejects newer JDKs.
        [string]$JdkRoot = 'D:\tools\android-qt512\jdk8',

        # SDK platform to compile against. androiddeployqt writes it into the generated
        # gradle.properties as androidCompileSdkVersion.
        [string]$AndroidPlatform = 'android-28',

        # Parallel compile jobs.
        [int]$Jobs = 16,

        # Delete the shadow build directory before configuring.
        [switch]$Clean,

        # Run tools/android/stage-assets.ps1 before building.
        [switch]$StageAssets,

        # Extra arguments forwarded to qmake, e.g. "QMAKE_CXXFLAGS+=-fstack-protector-all".
        [string[]]$ExtraQmakeArgs = @(),

        # Release signing material; required when the configuration is 'release'.
        [string]$Keystore,
        [string]$KeyAlias,
        [string]$StorePass,
        [string]$KeyPass,
        [string]$StoreType = 'JKS'
    )

    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    $projectFile = Join-Path $repoRoot 'QSanguosha.pro'
    $sourceBuildDir = Join-Path $BuildRoot 'build'
    $apkOutputDir = Join-Path $BuildRoot 'apk'

    Write-Step 'Checking inputs'
    Assert-Path $QtDir 'Qt Android arm64 kit'
    Assert-Path $NdkRoot 'Android NDK'
    Assert-Path $JdkRoot 'JDK 8'
    Assert-Path (Join-Path $SdkRoot 'platforms') 'Android SDK platforms'
    Assert-Path $projectFile 'Project file'

    $qmake = Join-Path $QtDir 'bin\qmake.exe'
    $androiddeployqt = Join-Path $QtDir 'bin\androiddeployqt.exe'
    Assert-Path $qmake 'qmake (Android kit)'
    Assert-Path $androiddeployqt 'androiddeployqt'

    $make = Join-Path $HostQtDir 'Tools\mingw730_32\bin\mingw32-make.exe'
    Assert-Path $make 'mingw32-make'

    $platformDir = Join-Path $SdkRoot "platforms\$AndroidPlatform"
    Assert-Path $platformDir "Android platform $AndroidPlatform"

    $assetsDir = Join-Path $repoRoot 'android\assets'
    if (-not $StageAssets -and -not (Test-Path (Join-Path $assetsDir 'lua\config.lua'))) {
        throw "android/assets is not staged; run tools/android/stage-assets.ps1 first (or pass -StageAssets)"
    }

    if ($Configuration -eq 'release') {
        Assert-Path $Keystore 'Release keystore'
        if ([string]::IsNullOrWhiteSpace($KeyAlias)) { throw 'KeyAlias is required for a release build' }
        if ([string]::IsNullOrWhiteSpace($StorePass)) { throw 'StorePass is required for a release build' }
        if ([string]::IsNullOrWhiteSpace($KeyPass)) { $KeyPass = $StorePass }
    }

    if ($Clean -and (Test-Path $BuildRoot)) {
        Write-Step "Cleaning $BuildRoot"
        Remove-Item $BuildRoot -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $sourceBuildDir, $apkOutputDir | Out-Null

    Write-Step 'Preparing environment'
    $env:ANDROID_NDK_ROOT = ConvertTo-PosixPath $NdkRoot
    $env:ANDROID_NDK_HOST = 'windows-x86_64'
    $env:ANDROID_SDK_ROOT = ConvertTo-PosixPath $SdkRoot
    $env:JAVA_HOME = ConvertTo-PosixPath $JdkRoot
    $env:PATH = "$(Join-Path $QtDir 'bin');$(Join-Path $JdkRoot 'bin');$env:PATH"

    "Configuration    : $Configuration"
    "Qt kit           : $QtDir"
    "NDK              : $NdkRoot"
    "SDK              : $SdkRoot"
    "Platform         : $AndroidPlatform"
    "JDK              : $JdkRoot"
    "Shadow build dir : $sourceBuildDir"

    Write-Step 'Running qmake'
    Push-Location $sourceBuildDir
    try {
        & $qmake -spec android-clang "CONFIG+=$Configuration" CONFIG+=ANDROID_TARGET_ARCH=arm64-v8a @ExtraQmakeArgs $projectFile
        if ($LASTEXITCODE -ne 0) { throw "qmake failed (exit code $LASTEXITCODE)" }

        if ($StageAssets) {
            Write-Step 'Staging assets'
            & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'stage-assets.ps1')
            if ($LASTEXITCODE -ne 0) { throw 'Asset staging failed' }
        }

        Write-Step 'Compiling'
        & $make "-j$Jobs"
        if ($LASTEXITCODE -ne 0) { throw "make failed (exit code $LASTEXITCODE)" }

        # qmake names it after the application binary, e.g.
        # android-libQSanguosha.so-deployment-settings.json.
        $settingsFile = Get-ChildItem $sourceBuildDir -Filter '*-deployment-settings.json' |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($null -eq $settingsFile) { throw "No Android deployment settings produced in $sourceBuildDir" }

        # qmake writes the "qt" key from QT_INSTALL_PREFIX, which on Windows uses
        # backslashes. androiddeployqt copies that value into gradle.properties, where
        # Java's properties parser treats a backslash as an escape character, so
        # "D:\tools\Qt\..." turns into "D:<tab>oolsQt..." and Gradle fails with
        # "Illegal character in opaque part". Normalise every absolute Windows path in
        # the settings to forward slashes before androiddeployqt reads the file.
        $settings = Get-Content $settingsFile.FullName -Raw | ConvertFrom-Json
        foreach ($property in @($settings.PSObject.Properties)) {
            if ($property.Value -isnot [string]) { continue }
            if ($property.Value -match '^[A-Za-z]:\\') {
                $property.Value = $property.Value.Replace('\', '/')
            }
        }
        $settings | ConvertTo-Json -Depth 4 | Set-Content $settingsFile.FullName -Encoding UTF8
        "Normalised paths in $($settingsFile.Name)"

        # --deployment bundled expects the application binary to be staged under
        # libs/<abi>/ in the output directory, which is what the install target does.
        Write-Step 'Installing to staging directory'
        & $make install "INSTALL_ROOT=$apkOutputDir"
        if ($LASTEXITCODE -ne 0) { throw "make install failed (exit code $LASTEXITCODE)" }

        # The platform and JDK are passed explicitly: the machine also has android-35/36
        # and Android Studio's JDK 21 installed, and taking either of those breaks the
        # Gradle 4.6 / build-tools 28.0.3 combination that Qt 5.12 expects.
        #
        # Without --release Gradle's debug variant is built and signed with the debug
        # keystore; with it the unsigned release APK is zipaligned and signed here with
        # apksigner (v1 and v2 signatures) using the keystore below. --storepass and
        # --keypass are the only way to hand the passwords to androiddeployqt
        # non-interactively.
        $deployArgs = @(
            '--input', $settingsFile.FullName
            '--output', $apkOutputDir
            '--deployment', 'bundled'
            '--gradle'
            '--android-platform', $AndroidPlatform
            '--jdk', $JdkRoot
        )
        if ($Configuration -eq 'release') {
            $deployArgs += @(
                '--release'
                '--sign', (ConvertTo-PosixPath $Keystore), $KeyAlias
                '--storetype', $StoreType
                '--storepass', $StorePass
                '--keypass', $KeyPass
            )
        }

        Write-Step 'Packaging with androiddeployqt'
        & $androiddeployqt @deployArgs
        if ($LASTEXITCODE -ne 0) { throw "androiddeployqt failed (exit code $LASTEXITCODE)" }
    } finally {
        Pop-Location
    }

    $apks = @(Get-ChildItem $apkOutputDir -Recurse -Filter '*.apk' | Sort-Object LastWriteTime -Descending)
    if ($apks.Count -eq 0) { throw "No APK produced under $apkOutputDir" }

    $apk = $apks[0]
    if ($Configuration -eq 'release') {
        # Gradle assembles apk-release-unsigned.apk and androiddeployqt turns it into
        # apk-release-signed.apk, so the signed package is the one that must exist.
        $signed = $apks | Where-Object { $_.Name -like '*-signed.apk' } | Select-Object -First 1
        if ($null -eq $signed) { throw "Release packaging produced no signed APK under $apkOutputDir" }
        $apk = $signed

        Write-Step 'Verifying the signature'
        $apksigner = Find-ApkSigner -SdkRoot $SdkRoot
        if ($null -eq $apksigner) { throw "apksigner not found under $SdkRoot\build-tools" }
        $verification = & $apksigner verify --verbose --print-certs $apk.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "apksigner verify failed (exit code $LASTEXITCODE):`n$($verification -join "`n")"
        }
        # A wrapper that cannot find java exits 0 without checking anything, so an
        # empty result is a failed verification rather than a silent pass.
        if (-not $verification) { throw "$apksigner reported nothing; the signature was not verified" }
        $verification | Where-Object { $_ -match 'Verified using v\d|Number of signers|certificate DN' } | Write-Host
    }

    Write-Step 'Done'
    Write-Host ('APK      : {0}' -f $apk.FullName)
    Write-Host ('Size     : {0:N1} MB' -f ($apk.Length / 1MB))
    Write-Host ('Install  : adb install -r "{0}"' -f $apk.FullName)
    if ($Configuration -eq 'debug') {
        # androiddeployqt strips the copy it stages into the package, so the library in
        # the shadow build directory is the one that still carries the line tables a
        # tombstone backtrace is resolved against.
        Write-Host ('Symbols  : {0}' -f (Join-Path $sourceBuildDir 'libQSanguosha.so'))
    }
}
