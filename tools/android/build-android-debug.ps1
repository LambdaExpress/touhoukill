<#
.SYNOPSIS
    Builds the Android (arm64-v8a) debug APK of TouhouKill.

.DESCRIPTION
    The native side is compiled with CONFIG+=debug, so libQSanguosha.so keeps its
    symbols and is not optimised; the Qt libraries it links against are the release
    builds shipped in the Qt Android kit, which is what a Qt 5.12 Android debug build
    uses.

    Packaging runs Gradle's debug variant, which is signed with the debug keystore in
    %USERPROFILE%\.android, so `adb install -r` replaces the previous build in place,
    and bundles gdbserver. androiddeployqt strips the library it puts into the package,
    which leaves <BuildRoot>\build\libQSanguosha.so as the symbolication source for
    tombstones; the build prints that path when it finishes.

    The build happens in a shadow directory outside the repository, because the
    Android Makefile would otherwise overwrite the desktop build's Makefile in the
    repository root. The debug and release builds use separate shadow directories, so
    switching between them neither recompiles the other nor discards its Gradle state.

    Run tools/android/stage-assets.ps1 first (or pass -StageAssets) so that
    android/assets is populated; androiddeployqt packages whatever it finds there.

    Toolchain paths and the shared build steps live in android-build-common.ps1.

.EXAMPLE
    pwsh tools/android/build-android-debug.ps1
    pwsh tools/android/build-android-debug.ps1 -Clean -StageAssets
    pwsh tools/android/build-android-debug.ps1 -Jobs 8
#>
[CmdletBinding()]
param(
    # Shadow build and packaging directory.
    [string]$BuildRoot = 'D:\tools\touhoukill-android\debug',

    # Parallel compile jobs.
    [int]$Jobs = 16,

    # Delete the shadow build directory before configuring.
    [switch]$Clean,

    # Run tools/android/stage-assets.ps1 before building.
    [switch]$StageAssets,

    # Extra arguments forwarded to qmake, e.g. "QMAKE_CXXFLAGS+=-g -fstack-protector-all".
    [string[]]$ExtraQmakeArgs = @()
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'android-build-common.ps1')

Invoke-AndroidBuild -Configuration debug -BuildRoot $BuildRoot -Jobs $Jobs -Clean:$Clean `
    -StageAssets:$StageAssets -ExtraQmakeArgs $ExtraQmakeArgs
