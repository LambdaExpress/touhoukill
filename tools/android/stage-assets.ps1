<#
.SYNOPSIS
    Stages the game's runtime resources into android/assets for the APK build.

.DESCRIPTION
    androiddeployqt copies android/ into the Android build directory and then
    pregenerates the asset file list that the Qt asset file engine uses for
    directory listings at runtime. That scan runs

        QDirIterator(assetsPath, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                     QDirIterator::Subdirectories)

    and QDirIterator refuses to descend into symbolic links unless FollowSymlinks
    is requested (qdiriterator.cpp, checkAndPushDirectory). Directory junctions on
    Windows are reported as symlinks by Qt, so staging the resources as junctions
    silently drops every file below them from the generated list, and the engine
    cannot enumerate its own resource directories on the device.

    The resources are therefore mirrored with robocopy. The mirror is incremental,
    so only the first staging run pays for the ~310 MB copy and later builds only
    transfer what changed.

.EXAMPLE
    pwsh tools/android/stage-assets.ps1
    pwsh tools/android/stage-assets.ps1 -Prune
#>
[CmdletBinding()]
param(
    # Remove android/assets before staging.
    [switch]$Prune
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$assetsDir = Join-Path $repoRoot 'android\assets'

# Directories and files that the release packaging step ships with the game.
# Kept in sync with the "Create playable package" step of
# .github/workflows/windows-release.yml, minus fmodex.dll, which only the desktop
# build loads. src/util/androidassets.cpp extracts exactly this set from the APK.
$resourceDirs = 'acknowledgement', 'audio', 'backdrop', 'developers', 'etc',
                'font', 'image', 'lang', 'lua', 'skins'
$resourceFiles = 'sanguosha.qm', 'sanguosha.qss', 'LICENSE', 'GPLv3', 'MCFR', 'README.md'

if ($Prune -and (Test-Path $assetsDir)) {
    Write-Host "Removing $assetsDir"
    Remove-Item $assetsDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null

$missing = New-Object System.Collections.Generic.List[string]

function Invoke-Robocopy {
    param(
        [string]$Source,
        [string]$Destination,
        [bool]$Mirror
    )

    $arguments = @($Source, $Destination, '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/R:1', '/W:1')
    if ($Mirror) { $arguments += '/MIR' }

    & robocopy.exe @arguments | Out-Null
    # robocopy exit codes below 8 mean success (1 = files copied, 0 = nothing to do).
    if ($LASTEXITCODE -ge 8) {
        throw "robocopy failed for $Source (exit code $LASTEXITCODE)"
    }
}

Write-Host "Staging into $assetsDir"

foreach ($name in $resourceDirs) {
    $sourcePath = Join-Path $repoRoot $name
    if (-not (Test-Path $sourcePath)) {
        $missing.Add($name)
        continue
    }
    Invoke-Robocopy -Source $sourcePath -Destination (Join-Path $assetsDir $name) -Mirror $true
}

foreach ($name in $resourceFiles) {
    $sourcePath = Join-Path $repoRoot $name
    if (-not (Test-Path $sourcePath)) {
        $missing.Add($name)
        continue
    }
    # robocopy needs a directory destination; single files are copied directly.
    Copy-Item $sourcePath (Join-Path $assetsDir $name) -Force
}

if ($missing.Count -gt 0) {
    throw "Missing resource(s) in the repository: $($missing -join ', ')"
}

# Build the asset manifest that src/util/androidassets.cpp reads at startup.
#
# Qt's asset file engine cannot enumerate directories recursively: QDirIterator
# only ever reports the direct children of an "assets:/" path, so a runtime walk
# finds nothing below the first level (which silently emptied image/ and audio/,
# as neither has files at its top level). The manifest is the flat, authoritative
# list instead, and it is written from the staged tree so it cannot drift.
$manifestPath = Join-Path $assetsDir 'asset-manifest.txt'
$manifestEntries = New-Object System.Collections.Generic.List[string]

foreach ($name in $resourceDirs) {
    $root = Join-Path $assetsDir $name
    Get-ChildItem $root -Recurse -File -Force | ForEach-Object {
        $manifestEntries.Add($_.FullName.Substring($assetsDir.Length + 1).Replace('\', '/'))
    }
}
foreach ($name in $resourceFiles) {
    $manifestEntries.Add($name)
}

$sortedEntries = @($manifestEntries | Sort-Object)
if ($sortedEntries.Count -lt 100) {
    throw "Only $($sortedEntries.Count) asset entries staged; the mirror step must have failed"
}

# UTF-8 without a byte order mark, one path per line, forward slashes, LF endings
# (WriteAllLines would use CRLF, which every reader would then have to trim).
[System.IO.File]::WriteAllText($manifestPath, (($sortedEntries -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))

# Digest of what was staged, read by src/util/androidassets.cpp to decide whether the
# copy on the device is still current. It cannot be derived from the manifest alone:
# the manifest is a list of paths, so editing a file in place leaves it unchanged and
# the device would keep running the copy it extracted the first time. Size and
# timestamp are included so any edit to any staged file changes the digest; reading
# them costs one stat per entry, where hashing the ~310 MB of contents would not be
# worth it on every build.
$digestPath = Join-Path $assetsDir 'asset-digest.txt'
$digestEntries = foreach ($relative in $sortedEntries) {
    $staged = Get-Item (Join-Path $assetsDir $relative.Replace('/', '\')) -Force
    '{0}|{1}|{2}' -f $relative, $staged.Length, $staged.LastWriteTimeUtc.Ticks
}
[System.IO.File]::WriteAllText($digestPath, (($digestEntries -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))

# Sanity checks: the engine fails hard when any of these are absent.
$mustExist = @(
    'lua\config.lua',
    'lua\sanguosha.lua',
    'lua\ai\smart-ai.lua',
    'skins\skinList.json',
    'font\simli.ttf',
    'sanguosha.qm',
    'audio\title\main.ogg',
    'image\logo\logo.png',
    'image\card\Axe.png',
    'backdrop\hall'
)
foreach ($relative in $mustExist) {
    $probe = Join-Path $assetsDir $relative
    if (-not (Test-Path $probe)) {
        throw "Staged assets are incomplete, missing: $relative"
    }
}

# Only the game's own resources are staged here; --Added-by-androiddeployqt-- is
# written later by androiddeployqt and must not exist yet.
$stagedFiles = @(Get-ChildItem $assetsDir -Recurse -File -Force)
$totalBytes = ($stagedFiles | Measure-Object -Property Length -Sum).Sum
if ($null -eq $totalBytes) { $totalBytes = 0 }

Write-Host ''
Write-Host ('Staged {0} files, {1:N1} MB' -f $stagedFiles.Count, ($totalBytes / 1MB))
Write-Host ('Manifest: {0} entries -> {1}' -f $sortedEntries.Count, $manifestPath)
Write-Host ''
Write-Host 'Next: pwsh tools/android/build-android.ps1'
