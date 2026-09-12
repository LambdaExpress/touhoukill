<#
.SYNOPSIS
    Builds and signs the Android (arm64-v8a) release APK of TouhouKill.

.DESCRIPTION
    The native side is compiled with CONFIG+=release. Packaging runs Gradle's release
    variant, and androiddeployqt zipaligns the unsigned package it produced and signs
    it with the keystore below using apksigner, v1, v2 and v3 signatures included,
    naming the result apk-release-signed.apk. The signature is verified before the
    build reports success, so an unreadable package cannot be shipped by accident.

    The keystore and its passwords are deliberately kept outside the repository: they
    must never enter git, and every future update of an installed app has to be signed
    with the same key, because Android refuses to install an APK whose signing
    certificate differs from the installed one. Passwords are read from the
    keystore.properties file that sits next to the keystore, unless -StorePass and
    -KeyPass are given explicitly.

    The build happens in a shadow directory outside the repository, because the
    Android Makefile would otherwise overwrite the desktop build's Makefile in the
    repository root. The debug and release builds use separate shadow directories, so
    switching between them neither recompiles the other nor discards its Gradle state.

    Run tools/android/stage-assets.ps1 first (or pass -StageAssets) so that
    android/assets is populated; androiddeployqt packages whatever it finds there.

    Toolchain paths and the shared build steps live in android-build-common.ps1.

.EXAMPLE
    pwsh tools/android/build-android-release.ps1
    pwsh tools/android/build-android-release.ps1 -Clean -StageAssets
    pwsh tools/android/build-android-release.ps1 -StorePass 'secret' -KeyPass 'secret'
#>
[CmdletBinding()]
param(
    # Shadow build and packaging directory.
    [string]$BuildRoot = 'D:\tools\touhoukill-android\release',

    # Release keystore. Kept outside the repository.
    [string]$Keystore = 'D:\tools\touhoukill-keystore\release.jks',

    # File holding storeFile, storePassword, keyAlias and keyPassword.
    [string]$Credentials = 'D:\tools\touhoukill-keystore\keystore.properties',

    # Key alias inside the keystore; defaults to keyAlias from the credentials file.
    [string]$KeyAlias,

    # Keystore password; defaults to storePassword from the credentials file.
    [string]$StorePass,

    # Private key password; defaults to keyPassword from the credentials file, and to
    # the keystore password when that is absent.
    [string]$KeyPass,

    # Keystore format; apksigner assumes JKS when it is not told otherwise.
    [string]$StoreType = 'JKS',

    # Parallel compile jobs.
    [int]$Jobs = 16,

    # Delete the shadow build directory before configuring.
    [switch]$Clean,

    # Run tools/android/stage-assets.ps1 before building.
    [switch]$StageAssets,

    # Extra arguments forwarded to qmake, e.g. "QMAKE_CXXFLAGS+=-fstack-protector-all".
    [string[]]$ExtraQmakeArgs = @()
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'android-build-common.ps1')

if (-not $StorePass -or -not $KeyAlias) {
    if (-not (Test-Path $Credentials)) {
        throw "No signing credentials: pass -StorePass and -KeyAlias, or provide $Credentials"
    }
    $values = Read-AndroidKeystoreProperties $Credentials
    if (-not $PSBoundParameters.ContainsKey('Keystore') -and $values.ContainsKey('storeFile')) {
        $Keystore = $values['storeFile']
    }
    if (-not $KeyAlias -and $values.ContainsKey('keyAlias')) { $KeyAlias = $values['keyAlias'] }
    if (-not $StorePass -and $values.ContainsKey('storePassword')) { $StorePass = $values['storePassword'] }
    if (-not $KeyPass -and $values.ContainsKey('keyPassword')) { $KeyPass = $values['keyPassword'] }
}

Invoke-AndroidBuild -Configuration release -BuildRoot $BuildRoot -Jobs $Jobs -Clean:$Clean `
    -StageAssets:$StageAssets -ExtraQmakeArgs $ExtraQmakeArgs `
    -Keystore $Keystore -KeyAlias $KeyAlias -StorePass $StorePass -KeyPass $KeyPass -StoreType $StoreType
