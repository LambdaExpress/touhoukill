#include "androidassets.h"

#ifdef Q_OS_ANDROID

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

namespace {

// Written by tools/android/stage-assets.ps1 and packaged into the APK next to the
// resources it describes: one relative path per line.
//
// A list is used instead of walking the APK because Qt's asset file engine cannot
// enumerate recursively. QDirIterator only reports the direct children of an
// "assets:/" directory, so a recursive walk silently finds nothing below the first
// level; image/ and audio/ have no files at their top level at all and would come
// out completely empty.
const char *const MANIFEST_ASSET = "assets:/asset-manifest.txt";

// Written by tools/android/stage-assets.ps1 alongside the manifest: one line per staged
// file holding its path, size and timestamp. The manifest lists paths only, so without
// this an edit to a staged file would leave the manifest byte for byte identical and
// the device would keep using the copy it extracted the first time.
const char *const DIGEST_ASSET = "assets:/asset-digest.txt";

const char *const ASSET_PREFIX = "assets:/";
const char *const VERSION_MARKER = ".assets-version";

// The one file the engine cannot start without; a failure here is fatal.
const char *const ESSENTIAL_FILE = "lua/config.lua";

bool copyAssetFile(const QString &assetPath, const QString &targetPath)
{
    QFile source(assetPath);
    if (!source.open(QIODevice::ReadOnly)) {
        qWarning("AndroidAssets: cannot open %s", qPrintable(assetPath));
        return false;
    }

    const QFileInfo targetInfo(targetPath);
    if (!QDir().mkpath(targetInfo.absolutePath())) {
        qWarning("AndroidAssets: cannot create %s", qPrintable(targetInfo.absolutePath()));
        return false;
    }

    QFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("AndroidAssets: cannot write %s", qPrintable(targetPath));
        return false;
    }

    // 64 KiB keeps the peak memory cost low while still streaming the large image
    // and audio files in few iterations.
    const qint64 bufferSize = 64 * 1024;
    QByteArray buffer;
    while (true) {
        buffer = source.read(bufferSize);
        if (buffer.isEmpty())
            break;
        if (target.write(buffer) != buffer.size()) {
            qWarning("AndroidAssets: short write on %s", qPrintable(targetPath));
            return false;
        }
    }

    return true;
}

// Reads the staged digest; empty when it is missing, in which case the caller falls
// back to the manifest hash.
QByteArray readStagedDigest()
{
    // Named first: "QFile digest(QLatin1String(...))" would be parsed as a function
    // declaration, exactly as in readManifest below.
    const QString digestPath = QLatin1String(DIGEST_ASSET);
    QFile digest(digestPath);
    if (!digest.open(QIODevice::ReadOnly))
        return QByteArray();

    return digest.readAll();
}

// Reads the manifest and returns its entries with forward slashes and no prefix.
QStringList readManifest(QByteArray *manifestBytes)
{
    // Named first: "QFile manifest(QLatin1String(...))" would be parsed as a
    // function declaration.
    const QString manifestPath = QLatin1String(MANIFEST_ASSET);
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) {
        qWarning("AndroidAssets: cannot read %s", MANIFEST_ASSET);
        return QStringList();
    }

    *manifestBytes = manifest.readAll();
    manifest.close();

    QStringList entries;
    foreach (const QByteArray &rawLine, manifestBytes->split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        entries << QString::fromUtf8(line);
    }

    return entries;
}

// Reads the recorded extraction state, or an empty string when the directory was
// never populated by this code.
QString readVersionMarker(const QString &dataPath)
{
    QFile marker(QDir(dataPath).filePath(QLatin1String(VERSION_MARKER)));
    if (!marker.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QTextStream stream(&marker);
    return stream.readAll().trimmed();
}

bool writeVersionMarker(const QString &dataPath, const QString &value)
{
    QFile marker(QDir(dataPath).filePath(QLatin1String(VERSION_MARKER)));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QTextStream stream(&marker);
    stream << value;
    return marker.error() == QFile::NoError;
}

} // namespace

QString AndroidAssets::provisionDataDirectory()
{
    const QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataPath.isEmpty()) {
        qWarning("AndroidAssets: no writable application data directory");
        return QString();
    }

    QByteArray manifestBytes;
    const QStringList relativePaths = readManifest(&manifestBytes);
    if (relativePaths.isEmpty()) {
        qWarning("AndroidAssets: the asset manifest is empty");
        return QString();
    }

    // The marker has to change whenever the packaged resources change, otherwise the
    // device keeps using the copy it extracted on a previous run. The manifest covers
    // the set of files; the staged digest covers their contents, which the manifest
    // cannot, because a list of paths is unchanged by editing a file in place. The
    // digest is what makes a plain resource edit take effect on a reinstall; falls back
    // to the manifest alone when an older staging script produced no digest.
    const QByteArray stagedDigest = readStagedDigest();
    const QString markerValue =
        QStringLiteral("%1 %2 %3")
            .arg(QLatin1String(QSGS_VERSIONNUMBER),
                 QString::fromLatin1(QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha1).toHex()),
                 stagedDigest.isEmpty() ? QStringLiteral("-") : QString::fromLatin1(QCryptographicHash::hash(stagedDigest, QCryptographicHash::Sha1).toHex()));

    if (readVersionMarker(dataPath) == markerValue
        && QFile::exists(QDir(dataPath).filePath(QLatin1String(ESSENTIAL_FILE)))) {
        return dataPath;
    }

    QScopedPointer<QProgressDialog> progress;
    if (qobject_cast<QApplication *>(QCoreApplication::instance()) != nullptr) {
        progress.reset(new QProgressDialog(QObject::tr("Preparing game resources..."), QString(), 0, relativePaths.size()));
        progress->setWindowModality(Qt::ApplicationModal);
        progress->setCancelButton(nullptr);
        progress->setMinimumDuration(0);
        progress->show();
        QCoreApplication::processEvents();
    }

    QDir().mkpath(dataPath);

    int copied = 0;
    bool essentialCopied = false;
    foreach (const QString &relative, relativePaths) {
        const QString targetPath = QDir(dataPath).filePath(relative);

        if (!copyAssetFile(QLatin1String(ASSET_PREFIX) + relative, targetPath)) {
            // A single missing image must not stop the game from starting; only the
            // essential file decides whether extraction counts as success.
            continue;
        }

        if (relative == QLatin1String(ESSENTIAL_FILE))
            essentialCopied = true;

        ++copied;
        if (progress && progress->value() != copied) {
            progress->setValue(copied);
            QCoreApplication::processEvents();
        }
    }

    if (progress)
        progress->close();

    if (!essentialCopied) {
        qWarning("AndroidAssets: failed to extract %s", ESSENTIAL_FILE);
        return QString();
    }

    if (copied != relativePaths.size())
        qWarning("AndroidAssets: extracted %d of %d files", copied, relativePaths.size());

    if (!writeVersionMarker(dataPath, markerValue))
        qWarning("AndroidAssets: could not record the extraction state");

    return dataPath;
}

#endif // Q_OS_ANDROID
