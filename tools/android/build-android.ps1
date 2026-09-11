<#
.SYNOPSIS
    Builds the Android (arm64-v8a) APK of TouhouKill.

.DESCRIPTION
    Uses the Qt 5.12.12 Android arm64 kit, NDK r19c, SDK platform android-28 and
    JDK 8 -- the combination Qt 5.12 was released against.

    The build happens in a shadow directory outside the repository, because the
    Android Makefile would otherwise overwrite the desktop build's Makefile in the
    repository root.

    Run tools/android/stage-assets.ps1 first (or pass -StageAssets) so that
    android/assets is populated; androiddeployqt packages whatever it finds there.

.EXAMPLE
    pwsh tools/android/build-android.ps1
    pwsh tools/android/build-android.ps1 -Clean -StageAssets
#>
[CmdletBinding()]
param(
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

    # Shadow build and packaging directory.
    [string]$BuildRoot = 'D:\tools\touhoukill-android',

    # SDK platform to compile against. Must match build-toolsVersion in android/build.gradle.
    [string]$AndroidPlatform = 'android-28',

    # Parallel compile jobs.
    [int]$Jobs = 16,

    # Delete the shadow build directory before configuring.
    [switch]$Clean,

    # Run tools/android/stage-assets.ps1 before building.
    [switch]$StageAssets
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$projectFile = Join-Path $repoRoot 'QSanguosha.pro'
$sourceBuildDir = Join-Path $BuildRoot 'build'
$apkOutputDir = Join-Path $BuildRoot 'apk'

function Write-Step {
    param([string]$Text)
    Write-Host ''
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Assert-Path {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path $Path)) { throw "$Description not found: $Path" }
}

Write-Step 'Checking inputs'
Assert-Path $QtDir 'Qt Android arm64 kit'
Assert-Path $NdkRoot 'Android NDK'
Assert-Path $JdkRoot 'JDK 8'
Assert-Path (Join-Path $SdkRoot 'platforms' ) 'Android SDK platforms'
Assert-Path $projectFile 'Project file'

$qmake = Join-Path $QtDir 'bin\qmake.exe'
$androiddeployqt = Join-Path $QtDir 'bin\androiddeployqt.exe'
Assert-Path $qmake 'qmake (Android kit)'
Assert-Path $androiddeployqt 'androiddeployqt'

$make = Join-Path $HostQtDir 'Tools\mingw730_32\bin\mingw32-make.exe'
Assert-Path $make 'mingw32-make'

$platformDir = Join-Path $SdkRoot "platforms\$AndroidPlatform"
Assert-Path $platformDir "Android platform $AndroidPlatform"

if ($StageAssets) {
    Write-Step 'Staging assets'
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'stage-assets.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Asset staging failed' }
}

$assetsDir = Join-Path $repoRoot 'android\assets'
if (-not (Test-Path (Join-Path $assetsDir 'lua\config.lua'))) {
    throw "android/assets is not staged; run tools/android/stage-assets.ps1 first (or pass -StageAssets)"
}

if ($Clean -and (Test-Path $BuildRoot)) {
    Write-Step "Cleaning $BuildRoot"
    Remove-Item $BuildRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $sourceBuildDir, $apkOutputDir | Out-Null

Write-Step 'Preparing environment'
# The generated Makefile embeds these paths in command lines that are executed by
# a POSIX shell, where a backslash is an escape character. Forward slashes are
# accepted by every tool involved, so the values are normalised here.
function ConvertTo-PosixPath {
    param([string]$Path)
    return $Path.Replace('\', '/')
}

$env:ANDROID_NDK_ROOT = ConvertTo-PosixPath $NdkRoot
$env:ANDROID_NDK_HOST = 'windows-x86_64'
$env:ANDROID_SDK_ROOT = ConvertTo-PosixPath $SdkRoot
$env:JAVA_HOME = ConvertTo-PosixPath $JdkRoot
$env:PATH = "$(Join-Path $QtDir 'bin');$(Join-Path $JdkRoot 'bin');$env:PATH"

"Qt kit           : $QtDir"
"NDK              : $NdkRoot"
"SDK              : $SdkRoot"
"Platform         : $AndroidPlatform"
"JDK              : $JdkRoot"
"Shadow build dir : $sourceBuildDir"

Write-Step 'Running qmake'
Push-Location $sourceBuildDir
try {
    & $qmake -spec android-clang CONFIG+=release CONFIG+=ANDROID_TARGET_ARCH=arm64-v8a $projectFile
    if ($LASTEXITCODE -ne 0) { throw "qmake failed (exit code $LASTEXITCODE)" }

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
    Write-Step 'Packaging with androiddeployqt'
    & $androiddeployqt --input $settingsFile.FullName --output $apkOutputDir --deployment bundled --gradle --android-platform $AndroidPlatform --jdk $JdkRoot
    if ($LASTEXITCODE -ne 0) { throw "androiddeployqt failed (exit code $LASTEXITCODE)" }
} finally {
    Pop-Location
}

$apk = Get-ChildItem $apkOutputDir -Recurse -Filter '*.apk' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($null -eq $apk) { throw "No APK produced under $apkOutputDir" }

Write-Step 'Done'
Write-Host ('APK : {0}' -f $apk.FullName)
Write-Host ('Size: {0:N1} MB' -f ($apk.Length / 1MB))
