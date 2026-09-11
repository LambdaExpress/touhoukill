<#
.SYNOPSIS
    Cross-compiles the third-party native libraries used by the Android build.

.DESCRIPTION
    Produces AArch64 (arm64-v8a) static libraries for FreeType 2.5.5, libogg 1.3.5
    and libvorbis 1.3.7, then installs them into lib/android/arm64/lib together with
    the public headers in include/ogg and include/vorbis.

    FreeType must be 2.5.5 because include/freetype ships the 2.5.5 headers.
    Sources are downloaded (FreeType) or extracted from this repository's
    upstream/refactor branch (libogg, libvorbis) when not already present.

    Every produced archive is verified to contain only ELF64 AArch64 objects.

.EXAMPLE
    pwsh tools/android/build-3rdparty.ps1
    pwsh tools/android/build-3rdparty.ps1 -Clean -ApiLevel 21
#>
[CmdletBinding()]
param(
    # Android NDK r19c root (the directory that contains toolchains/ and source.properties).
    [string]$NdkRoot = 'D:\tools\android-qt512\ndk\android-ndk-r19c',

    # Directory holding the extracted third-party sources.
    [string]$SourceRoot = 'D:\tools\android-qt512\src',

    # Directory for object files and the intermediate archives.
    [string]$WorkRoot = 'D:\tools\android-qt512\build-3rdparty',

    # Android API level to compile against.
    [int]$ApiLevel = 21,

    # Parallel compile jobs.
    [int]$Jobs = 16,

    # Delete the work directory before building.
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$installLibDir = Join-Path $repoRoot 'lib\android\arm64\lib'
$installIncludeDir = Join-Path $repoRoot 'include'
$objRoot = Join-Path $WorkRoot 'obj'
$arRoot = Join-Path $WorkRoot 'lib'

function Write-Step {
    param([string]$Text)
    Write-Host ''
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Invoke-Native {
    param(
        [string]$Exe,
        [string[]]$Arguments,
        [string]$FailMessage
    )
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FailMessage (exit code $LASTEXITCODE): $Exe $($Arguments -join ' ')"
    }
}

# Returns one entry per ELF object found inside a static archive.
function Get-ArchiveElfInfo {
    param([string]$ArchivePath)

    $bytes = [System.IO.File]::ReadAllBytes($ArchivePath)
    $results = New-Object System.Collections.Generic.List[object]
    $cursor = 0

    while ($cursor -lt $bytes.Length - 4) {
        $idx = [Array]::IndexOf($bytes, [byte]0x7F, $cursor)
        if ($idx -lt 0) { break }

        # Require the full ELF identification block, not just the magic, so that
        # archive metadata that happens to contain 0x7F 'E' 'L' 'F' is not
        # mistaken for a member.
        $isElf = ($idx + 19 -lt $bytes.Length) -and
                 ($bytes[$idx + 1] -eq 0x45) -and
                 ($bytes[$idx + 2] -eq 0x4C) -and
                 ($bytes[$idx + 3] -eq 0x46) -and
                 ($bytes[$idx + 5] -eq 1) -and
                 ($bytes[$idx + 6] -eq 1) -and
                 ($bytes[$idx + 7] -eq 0) -and
                 ($bytes[$idx + 8] -eq 0)

        if ($isElf) {
            $results.Add([pscustomobject]@{
                Offset  = $idx
                Class   = $bytes[$idx + 4]
                Machine = [System.BitConverter]::ToUInt16($bytes, $idx + 18)
            })
            $cursor = $idx + 64
        } else {
            $cursor = $idx + 1
        }
    }

    return $results
}

function Test-ArchiveIsAArch64 {
    param(
        [string]$ArchivePath,
        [int]$MinimumMembers = 1
    )

    $entries = Get-ArchiveElfInfo -ArchivePath $ArchivePath
    $name = Split-Path -Leaf $ArchivePath

    if ($entries.Count -lt $MinimumMembers) {
        throw "$name contains $($entries.Count) ELF objects, expected at least $MinimumMembers"
    }

    $bad = @($entries | Where-Object { $_.Class -ne 2 -or $_.Machine -ne 183 })
    if ($bad.Count -gt 0) {
        throw "$name contains $($bad.Count) non-AArch64 objects (class/machine: $(($bad | ForEach-Object { "$($_.Class)/$($_.Machine)" }) -join ', '))"
    }

    "{0,-20} {1,5} objects  ELF64 AArch64  OK" -f $name, $entries.Count
}

function New-ToolchainPaths {
    param([string]$Ndk, [int]$Level)

    $prebuilt = Join-Path $Ndk 'toolchains\llvm\prebuilt\windows-x86_64'
    $bin = Join-Path $prebuilt 'bin'

    return [pscustomobject]@{
        Bin      = $bin
        Sysroot  = Join-Path $prebuilt 'sysroot'
        Cc       = Join-Path $bin "aarch64-linux-android$Level-clang.cmd"
        Ar       = Join-Path $bin 'llvm-ar.exe'
        ReadElf  = Join-Path $bin 'aarch64-linux-android-readelf.exe'
    }
}

function Get-ThirdPartySources {
    param([string]$Root, [string]$RepoRoot)

    $freetype = Join-Path $Root 'freetype-VER-2-5-5'
    $ogg = Join-Path $Root 'src\3rdparty\libogg-1.3.5'
    $vorbis = Join-Path $Root 'src\3rdparty\libvorbis-1.3.7'

    New-Item -ItemType Directory -Force -Path $Root | Out-Null

    if (-not (Test-Path $freetype)) {
        Write-Step 'Downloading FreeType 2.5.5'
        $tarball = Join-Path $Root 'freetype-2.5.5.tar.gz'
        curl.exe -sL -o $tarball 'https://github.com/freetype/freetype/archive/refs/tags/VER-2-5-5.tar.gz'
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tarball)) {
            throw 'Failed to download FreeType 2.5.5'
        }
        tar -xzf $tarball -C $Root
        if ($LASTEXITCODE -ne 0) { throw 'Failed to extract FreeType 2.5.5' }
    }

    if (-not (Test-Path $ogg) -or -not (Test-Path $vorbis)) {
        Write-Step 'Extracting libogg/libvorbis from upstream/refactor'
        Push-Location $RepoRoot
        try {
            & git archive upstream/refactor src/3rdparty/libogg-1.3.5 | tar -x -C $Root
            if ($LASTEXITCODE -ne 0) { throw 'Failed to extract libogg' }
            & git archive upstream/refactor src/3rdparty/libvorbis-1.3.7 | tar -x -C $Root
            if ($LASTEXITCODE -ne 0) { throw 'Failed to extract libvorbis' }
        } finally {
            Pop-Location
        }
    }

    foreach ($path in $freetype, $ogg, $vorbis) {
        if (-not (Test-Path $path)) { throw "Missing third-party source: $path" }
    }

    return [pscustomobject]@{
        FreeType = $freetype
        Ogg      = $ogg
        Vorbis   = $vorbis
    }
}

function Compile-Sources {
    param(
        [string]$Name,
        [string[]]$Sources,
        [string[]]$IncludeDirs,
        [string[]]$Defines,
        [string]$Std,
        [string]$ObjectRoot,
        [string]$Compiler,
        [int]$ThrottleLimit
    )

    $outDir = Join-Path $ObjectRoot $Name
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    $includeFlags = @()
    foreach ($dir in $IncludeDirs) { $includeFlags += "-I$dir" }
    $defineFlags = @()
    foreach ($define in $Defines) { $defineFlags += "-D$define" }

    $baseFlags = @('-O2', '-fPIC', '-fno-strict-aliasing', '-w')
    if ($Std) { $baseFlags += "-std=$Std" }
    $baseFlags += $includeFlags
    $baseFlags += $defineFlags

    $units = foreach ($src in $Sources) {
        $leaf = [System.IO.Path]::GetFileNameWithoutExtension($src)
        [pscustomobject]@{
            Source = $src
            Object = Join-Path $outDir "$leaf.o"
            Flags  = $baseFlags
        }
    }

    $failures = @($units | ForEach-Object -Parallel {
        $unit = $_
        $log = (& $using:Compiler @($unit.Flags) -c $unit.Source -o $unit.Object 2>&1) -join "`n"
        if ($LASTEXITCODE -ne 0) { "FAILED: $($unit.Source)`n$log" }
    } -ThrottleLimit $ThrottleLimit)

    if ($failures.Count -gt 0) {
        throw "$Name : $($failures.Count) source file(s) failed to compile`n$($failures -join "`n")"
    }

    $objects = @($units | ForEach-Object { $_.Object })
    foreach ($obj in $objects) {
        if (-not (Test-Path $obj)) { throw "$Name : missing object $obj" }
    }

    Write-Host "$Name : compiled $($objects.Count) objects"
    return $objects
}

function New-StaticArchive {
    param(
        [string]$ArchivePath,
        [string[]]$Objects,
        [string]$Archiver
    )

    if (Test-Path $ArchivePath) { Remove-Item $ArchivePath -Force }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ArchivePath) | Out-Null

    Invoke-Native -Exe $Archiver -Arguments (@('rcs', $ArchivePath) + $Objects) `
        -FailMessage "Failed to archive $(Split-Path -Leaf $ArchivePath)"
}

if ($Clean -and (Test-Path $WorkRoot)) {
    Write-Step "Cleaning $WorkRoot"
    Remove-Item $WorkRoot -Recurse -Force
}

Write-Step 'Checking toolchain'
$tools = New-ToolchainPaths -Ndk $NdkRoot -Level $ApiLevel
foreach ($required in $tools.Cc, $tools.Ar) {
    if (-not (Test-Path $required)) { throw "Missing NDK tool: $required" }
}
$ndkVersion = (Get-Content (Join-Path $NdkRoot 'source.properties') | Select-String 'Pkg.Revision').Line
"NDK              : $NdkRoot"
"NDK revision     : $ndkVersion"
"Compiler         : $($tools.Cc)"
"API level        : $ApiLevel"
"Install lib dir  : $installLibDir"

$sources = Get-ThirdPartySources -Root $SourceRoot -RepoRoot $repoRoot
"FreeType source  : $($sources.FreeType)"
"libogg source    : $($sources.Ogg)"
"libvorbis source : $($sources.Vorbis)"

# ---------------------------------------------------------------- FreeType 2.5.5

Write-Step 'Compiling FreeType 2.5.5'

$freetypeSrc = $sources.FreeType
$freetypeSrcDir = Join-Path $freetypeSrc 'src'

# FreeType's own module list: the aggregator files are the compilation units, and
# each one #includes the sub-sources it owns. Building sub-sources directly is
# wrong -- several of them (e.g. src/gzip/infutil.c) are include-only fragments.
# Aggregators mirror modules.cfg plus the base layer split used by freetype.mk.
$freetypeUnits = @(
    # base layer + the standalone base objects handled by freetype.mk
    'base\ftbase.c', 'base\ftsystem.c', 'base\ftinit.c', 'base\ftdebug.c',
    # base layer extensions, in the order listed by modules.cfg
    'base\ftbbox.c', 'base\ftbdf.c', 'base\ftbitmap.c', 'base\ftcid.c',
    'base\ftfstype.c', 'base\ftgasp.c', 'base\ftglyph.c', 'base\ftgxval.c',
    'base\ftlcdfil.c', 'base\ftmm.c', 'base\ftotval.c', 'base\ftpatent.c',
    'base\fttype1.c', 'base\ftwinfnt.c', 'base\ftstroke.c', 'base\ftsynth.c',
    'base\ftxf86.c',
    # font drivers
    'truetype\truetype.c', 'type1\type1.c', 'cff\cff.c', 'cid\type1cid.c',
    'pfr\pfr.c', 'type42\type42.c', 'winfonts\winfnt.c', 'pcf\pcf.c',
    'bdf\bdf.c', 'sfnt\sfnt.c',
    # hinting, rasterizers and auxiliary modules
    'autofit\autofit.c', 'pshinter\pshinter.c', 'raster\raster.c',
    'smooth\smooth.c', 'cache\ftcache.c', 'gzip\ftgzip.c', 'lzw\ftlzw.c',
    'bzip2\ftbzip2.c', 'psaux\psaux.c', 'psnames\psnames.c'
)

$freetypeSources = @($freetypeUnits | ForEach-Object { Join-Path $freetypeSrcDir $_ })
$missingUnits = @($freetypeSources | Where-Object { -not (Test-Path $_) })
if ($missingUnits.Count -gt 0) {
    throw "FreeType: missing module source(s):`n$($missingUnits -join "`n")"
}

$freetypeObjects = Compile-Sources -Name 'freetype' -Sources $freetypeSources `
    -IncludeDirs @((Join-Path $freetypeSrc 'include')) `
    -Defines @('FT2_BUILD_LIBRARY') `
    -Std 'gnu89' `
    -ObjectRoot $objRoot -Compiler $tools.Cc -ThrottleLimit $Jobs

New-StaticArchive -ArchivePath (Join-Path $arRoot 'libfreetype.a') `
    -Objects $freetypeObjects -Archiver $tools.Ar

# ------------------------------------------------------------ libogg 1.3.5

Write-Step 'Compiling libogg 1.3.5'

$oggSrc = $sources.Ogg
$oggSources = @(
    (Join-Path $oggSrc 'src\bitwise.c'),
    (Join-Path $oggSrc 'src\framing.c')
)

$oggObjects = Compile-Sources -Name 'ogg' -Sources $oggSources `
    -IncludeDirs @((Join-Path $oggSrc 'include')) `
    -Defines @() `
    -Std 'gnu99' `
    -ObjectRoot $objRoot -Compiler $tools.Cc -ThrottleLimit $Jobs

New-StaticArchive -ArchivePath (Join-Path $arRoot 'libogg.a') `
    -Objects $oggObjects -Archiver $tools.Ar

# -------------------------------------------------------- libvorbis 1.3.7

Write-Step 'Compiling libvorbis 1.3.7'

$vorbisSrc = $sources.Vorbis
$vorbisLibDir = Join-Path $vorbisSrc 'lib'

$vorbisCore = 'analysis', 'bitrate', 'block', 'codebook', 'envelope', 'floor0',
              'floor1', 'info', 'lookup', 'lpc', 'lsp', 'mapping0', 'mdct', 'psy',
              'registry', 'res0', 'sharedbook', 'smallft', 'synthesis', 'window'

$vorbisSources = @($vorbisCore | ForEach-Object { Join-Path $vorbisLibDir "$_.c" })
$vorbisInclude = @(
    (Join-Path $vorbisSrc 'include'),
    (Join-Path $oggSrc 'include')
)

$vorbisObjects = Compile-Sources -Name 'vorbis' -Sources $vorbisSources `
    -IncludeDirs $vorbisInclude -Defines @() -Std 'gnu99' `
    -ObjectRoot $objRoot -Compiler $tools.Cc -ThrottleLimit $Jobs

New-StaticArchive -ArchivePath (Join-Path $arRoot 'libvorbis.a') `
    -Objects $vorbisObjects -Archiver $tools.Ar

$vorbisfileObjects = Compile-Sources -Name 'vorbisfile' `
    -Sources @((Join-Path $vorbisLibDir 'vorbisfile.c')) `
    -IncludeDirs $vorbisInclude -Defines @() -Std 'gnu99' `
    -ObjectRoot $objRoot -Compiler $tools.Cc -ThrottleLimit $Jobs

New-StaticArchive -ArchivePath (Join-Path $arRoot 'libvorbisfile.a') `
    -Objects $vorbisfileObjects -Archiver $tools.Ar

# ------------------------------------------------------------------ install

Write-Step 'Installing archives and headers'

New-Item -ItemType Directory -Force -Path $installLibDir | Out-Null
foreach ($name in 'libfreetype.a', 'libogg.a', 'libvorbis.a', 'libvorbisfile.a') {
    Copy-Item (Join-Path $arRoot $name) (Join-Path $installLibDir $name) -Force
}

$oggIncludeOut = Join-Path $installIncludeDir 'ogg'
$vorbisIncludeOut = Join-Path $installIncludeDir 'vorbis'
New-Item -ItemType Directory -Force -Path $oggIncludeOut, $vorbisIncludeOut | Out-Null

Copy-Item (Join-Path $oggSrc 'include\ogg\ogg.h') (Join-Path $oggIncludeOut 'ogg.h') -Force
Copy-Item (Join-Path $oggSrc 'include\ogg\os_types.h') (Join-Path $oggIncludeOut 'os_types.h') -Force
Copy-Item (Join-Path $vorbisSrc 'include\vorbis\codec.h') (Join-Path $vorbisIncludeOut 'codec.h') -Force
Copy-Item (Join-Path $vorbisSrc 'include\vorbis\vorbisfile.h') (Join-Path $vorbisIncludeOut 'vorbisfile.h') -Force

Write-Step 'Verifying AArch64'
foreach ($name in 'libfreetype.a', 'libogg.a', 'libvorbis.a', 'libvorbisfile.a') {
    $archive = Join-Path $installLibDir $name
    $minimum = if ($name -eq 'libfreetype.a') { 35 } else { 1 }
    Test-ArchiveIsAArch64 -ArchivePath $archive -MinimumMembers $minimum | Write-Host
}

Write-Step 'Done'
Write-Host "Libraries : $installLibDir"
Write-Host "Headers   : $oggIncludeOut, $vorbisIncludeOut"
