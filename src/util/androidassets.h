#ifndef ANDROIDASSETS_H
#define ANDROIDASSETS_H

#include <QString>

#ifdef Q_OS_ANDROID

namespace AndroidAssets {

// Copies the resources bundled in the APK into a writable directory and returns
// that directory, or an empty string when the essential resources could not be
// extracted.
//
// The engine loads its Lua scripts, skins, fonts and configuration through
// relative paths, and the APK's asset storage is read-only and not a normal
// filesystem, so the files have to be materialised before anything else runs.
// The list of files comes from assets/asset-manifest.txt, written by
// tools/android/stage-assets.ps1. The copy is skipped while the marker file
// already records the current version and manifest.
QString provisionDataDirectory();

} // namespace AndroidAssets

#endif // Q_OS_ANDROID

#endif // ANDROIDASSETS_H
