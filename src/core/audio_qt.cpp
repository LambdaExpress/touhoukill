#ifdef AUDIO_SUPPORT

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QBuffer>
#include <QCache>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QScopedPointer>
#include <QSet>
#include <QStringList>
#include <QTime>
#include <QThread>

#include <atomic>

#include <vorbis/vorbisfile.h>

#include "audio.h"
#include "util.h"

// Android audio backend.
//
// The desktop build drives FMOD Ex, whose Android libraries are 32-bit only and
// depend on the GNU STL that the NDK no longer ships. This implementation keeps
// the public Audio interface from audio.h but plays Ogg Vorbis through Qt
// Multimedia, so call sites stay untouched.
//
// The layout of BackgroundMusicPlayList and BackgroundMusicPlayer deliberately
// mirrors audio.cpp: the playlist ordering, the single-track loop and the
// polling timer that advances to the next track are the same behaviour, only the
// sound object underneath differs.

namespace {

size_t readFromQBuffer(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    if (size == 0)
        return 0;
    QIODevice *device = reinterpret_cast<QIODevice *>(datasource);
    const qint64 bytes = device->read(static_cast<char *>(ptr), static_cast<qint64>(size * nmemb));
    return bytes > 0 ? static_cast<size_t>(bytes) / size : 0;
}

int seekQBuffer(void *datasource, ogg_int64_t offset, int whence)
{
    QIODevice *device = reinterpret_cast<QIODevice *>(datasource);
    bool seeked = false;

    if (whence == SEEK_SET)
        seeked = device->seek(offset);
    else if (whence == SEEK_CUR)
        seeked = device->seek(offset + device->pos());
    else if (whence == SEEK_END)
        seeked = device->seek(offset + device->size());

    return seeked ? 0 : -1;
}

long tellQBuffer(void *datasource)
{
    QIODevice *device = reinterpret_cast<QIODevice *>(datasource);
    return static_cast<long>(device->pos());
}

} // namespace

// OggPlayer lives at namespace scope: moc cannot generate meta-object code for a
// class declared inside an anonymous namespace.
class OggPlayer : public QObject
{
    Q_OBJECT

public:
    explicit OggPlayer(const QString &fileName, QObject *parent = nullptr);
    ~OggPlayer() override;

    void play(bool loop = false);
    void stop();
    bool isPlaying() const;
    bool hasFinished() const { return m_finished; }
    void setVolume(float volume);

private slots:
    void handleStateChanged(QAudio::State state);

private:
    Q_DISABLE_COPY(OggPlayer)

    QBuffer m_buffer;
    QAudioOutput *m_output;
    bool m_loop;
    bool m_finished = false;
    float m_volume;
};

OggPlayer::OggPlayer(const QString &fileName, QObject *parent)
    : QObject(parent)
    , m_output(nullptr)
    , m_loop(false)
    , m_volume(1.0f)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return;

    OggVorbis_File vorbisFile;
    ov_callbacks callbacks;
    callbacks.read_func = &readFromQBuffer;
    callbacks.seek_func = &seekQBuffer;
    callbacks.close_func = nullptr;
    callbacks.tell_func = &tellQBuffer;

    if (ov_open_callbacks(&file, &vorbisFile, nullptr, 0, callbacks) != 0) {
        file.close();
        return;
    }

    // The whole stream is decoded up front so QAudioOutput can pull from a plain
    // QBuffer; this matches the reference implementation in the CMake branch.
    m_buffer.open(QIODevice::WriteOnly);
    while (true) {
        char data[20000];
        const long bytesRead = ov_read(&vorbisFile, data, sizeof(data), 0, 2, 1, nullptr);
        if (bytesRead > 0)
            m_buffer.write(data, bytesRead);
        else if (bytesRead == 0)
            break; // end of stream
        else if (bytesRead != OV_HOLE) {
            qWarning("Audio: unable to decode %s (%ld)", qPrintable(fileName), bytesRead);
            ov_clear(&vorbisFile);
            return;
        }
    }

    const vorbis_info *info = ov_info(&vorbisFile, -1);
    if (info == nullptr) {
        ov_clear(&vorbisFile);
        file.close();
        return;
    }

    QAudioFormat format;
    format.setCodec(QStringLiteral("audio/pcm"));
    format.setSampleSize(16);
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);
    format.setChannelCount(info->channels);
    format.setSampleRate(static_cast<int>(info->rate));

    ov_clear(&vorbisFile);
    file.close();

    m_buffer.close();
    m_buffer.open(QIODevice::ReadOnly);

    const QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
    if (device.isNull()) {
        qWarning("Audio: no default audio output device, %s will stay silent", qPrintable(fileName));
        return;
    }

    if (!device.isFormatSupported(format)) {
        // Nearest-format keeps Qt from handing back an unusable device, but if the
        // sample rate or channel count changes the sound would be resampled by the
        // backend. Report it so a broken mixdown is diagnosable on a device.
        const QAudioFormat nearest = device.nearestFormat(format);
        if (nearest.sampleRate() != format.sampleRate() || nearest.channelCount() != format.channelCount()) {
            qWarning("Audio: %s uses %d Hz/%d ch which the output device rejects; falling back to %d Hz/%d ch",
                     qPrintable(fileName), format.sampleRate(), format.channelCount(),
                     nearest.sampleRate(), nearest.channelCount());
        }
        format = nearest;
    }

    m_output = new QAudioOutput(format, this);
    m_output->setVolume(m_volume);
    connect(m_output, &QAudioOutput::stateChanged, this, &OggPlayer::handleStateChanged);
}

OggPlayer::~OggPlayer()
{
    stop();
}

void OggPlayer::handleStateChanged(QAudio::State state)
{
    Q_UNUSED(state)

    if (m_output->state() != QAudio::IdleState)
        return;

    if (m_loop) {
        m_buffer.seek(0);
        m_output->start(&m_buffer);
    } else {
        m_finished = true;
        // OpenSL's stop() retains the native player. reset() releases its track,
        // while the decoded PCM remains cached for the next playback.
        m_output->reset();
    }
}

void OggPlayer::play(bool loop)
{
    m_loop = loop;
    m_finished = false;

    if (m_output == nullptr)
        return;

    m_output->reset();
    m_buffer.seek(0);
    m_output->start(&m_buffer);
}

void OggPlayer::stop()
{
    m_finished = false;
    if (m_output != nullptr)
        m_output->reset();
}

bool OggPlayer::isPlaying() const
{
    return m_output != nullptr && m_output->state() == QAudio::ActiveState;
}

void OggPlayer::setVolume(float volume)
{
    m_volume = volume;
    if (m_output != nullptr)
        m_output->setVolume(volume);
}

class BackgroundMusicPlayList
{
public:
    enum PlayOrder
    {
        Sequential = 1,
        Shuffle = 2,
    };

    explicit BackgroundMusicPlayList(const QStringList &fileNames, BackgroundMusicPlayList::PlayOrder order = Sequential, const QStringList &openings = QStringList())
        : m_fileNames(fileNames)
        , m_order(order)
        , m_index(-1)
        , m_openings(openings)
    {
    }

    int count() const
    {
        return m_fileNames.size();
    }

    bool operator==(const BackgroundMusicPlayList &other) const
    {
        if (this == &other) {
            return true;
        }

        if (this->m_order != other.m_order) {
            return false;
        }

        switch (other.m_order) {
        case Sequential:
            return this->m_fileNames == other.m_fileNames;

        case Shuffle:
            return this->m_fileNames.toSet() == other.m_fileNames.toSet();

        default:
            return false;
        }
    }
    bool operator!=(const BackgroundMusicPlayList &other) const
    {
        return !(*this == other);
    }

    QString nextFileName()
    {
        if (Shuffle == m_order) {
            if (m_randomQueue.isEmpty()) {
                fillRandomQueue();
            }

            QString fileName = m_randomQueue.takeFirst();
            m_index = m_fileNames.indexOf(fileName);
            return fileName;
        } else {
            if (++m_index >= m_fileNames.size()) {
                m_index = 0;
            }
            return m_fileNames.at(m_index);
        }
    }

private:
    void fillRandomQueue()
    {
        m_randomQueue = m_fileNames;
        m_randomQueue.prepend("");
        qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));

        if (m_openings.isEmpty())
            qShuffle(m_randomQueue);
        else {
            qShuffle(m_openings);
            QString first = m_openings.takeFirst();
            foreach (QString opening, m_openings) {
                m_randomQueue << opening;
            }
            qShuffle(m_randomQueue);
            m_randomQueue.prepend(first);
        }
    }

private:
    QStringList m_fileNames;
    QStringList m_randomQueue;
    PlayOrder m_order;
    int m_index;
    QStringList m_openings; //need play title/open at first
};

static std::atomic_bool BackgroundPlaying(false);

class BackgroundMusicPlayer : public QObject
{
public:
    explicit BackgroundMusicPlayer(QObject *parent = nullptr)
        : QObject(parent)
        , m_timer(0)
        , m_count(0)
        , m_volume(1.0f)
    {
    }

    void play(const QString &fileNames, bool random, bool playAll = false, bool isGeneralName = false)
    {
        if (m_timer != 0) {
            BackgroundPlaying.store(isPlaying());
            return;
        }

        {
            BackgroundMusicPlayList::PlayOrder playOrder = random ? BackgroundMusicPlayList::Shuffle : BackgroundMusicPlayList::Sequential;

            QStringList all;
            if (fileNames.endsWith(".ogg"))
                all = fileNames.split(";");
            QStringList openings;

            if (isGeneralName) { //fileNames is  generalName
                //just support title only
                QString path = "audio/bgm/";
                QDir *dir = new QDir(path);
                QStringList filter;
                filter << "*.ogg";
                dir->setNameFilters(filter);
                QList<QFileInfo> file_info(dir->entryInfoList(filter));

                foreach (const QFileInfo &file, file_info) {
                    QString fileName = path + file.fileName();
                    if (file.fileName().startsWith(fileNames + "_") && !all.contains(fileName))
                        all << fileName;
                }
            }

            if (all.isEmpty() || playAll) {
                QString path = "audio/title/";
                QDir *dir = new QDir(path);
                QStringList filter;
                filter << "*.ogg";
                dir->setNameFilters(filter);
                QList<QFileInfo> file_info(dir->entryInfoList(filter));

                foreach (const QFileInfo &file, file_info) {
                    QString fileName = path + file.fileName();
                    if ((file.fileName().startsWith("main") || file.fileName().startsWith("opening")))
                        openings << fileName;
                    else if (!all.contains(fileName))
                        all << fileName;
                }
            }

            QScopedPointer<BackgroundMusicPlayList> playList(new BackgroundMusicPlayList(all, playOrder, openings));
            if (!m_playList || (*m_playList != *playList)) {
                m_playList.swap(playList);
                m_count = m_playList->count();
            }
        }

        // Nothing to play: an empty playlist would index out of range below.
        if (m_count < 1) {
            BackgroundPlaying.store(false);
            return;
        }

        playNext();
        m_timer = startTimer(m_interval);
    }

    void stop()
    {
        BackgroundPlaying.store(false);
        if (m_timer != 0) {
            killTimer(m_timer);
            m_timer = 0;
        }

        if (!m_sound.isNull())
            m_sound->stop();
    }

    void shutdown()
    {
        m_sound.reset();
    }

    bool isPlaying() const
    {
        return !m_sound.isNull() && m_sound->isPlaying();
    }

    void setVolume(float volume)
    {
        m_volume = volume;
        if (!m_sound.isNull())
            m_sound->setVolume(volume);
    }

protected:
    void timerEvent(QTimerEvent *) override
    {
        // A failed output is not the end of a track. Advancing on every failure
        // decoded a whole BGM on the UI thread again every 500 ms.
        if (!m_sound.isNull() && m_sound->hasFinished()) {
            playNext();
        }
    }

private:
    void playNext()
    {
        BackgroundPlaying.store(true);
        m_sound.reset();
        m_sound.reset(new OggPlayer(m_playList->nextFileName()));
        m_sound->setVolume(m_volume);
        m_sound->play(1 == m_count);
        BackgroundPlaying.store(m_sound->isPlaying());
    }

    Q_DISABLE_COPY(BackgroundMusicPlayer)

private:
    QScopedPointer<BackgroundMusicPlayList> m_playList;
    QScopedPointer<OggPlayer> m_sound;
    int m_timer;
    int m_count;
    float m_volume;

    static const int m_interval = 500;
};

class AudioWorker : public QObject
{
public:
    AudioWorker()
        : music(new BackgroundMusicPlayer(this))
    {
    }

    void play(const QString &fileName, bool restart)
    {
        OggPlayer *sound = sounds[fileName];
        if (sound == nullptr) {
            sound = new OggPlayer(fileName);
            sounds.insert(fileName, sound);
        } else if (!restart && sound->isPlaying()) {
            return;
        }
        sound->setVolume(effectVolume);
        sound->play();
    }

    void stopAll()
    {
        foreach (const QString &key, sounds.keys())
            sounds[key]->stop();
        music->stop();
    }

    QCache<QString, OggPlayer> sounds;
    BackgroundMusicPlayer *music;
    float effectVolume = 1.0f;
};

static QMutex AudioMutex;
static QThread *AudioThread = nullptr;
static AudioWorker *Worker = nullptr;

template<typename Function>
static bool dispatchAudio(Function function)
{
    QMutexLocker lock(&AudioMutex);
    AudioWorker *worker = Worker;
    if (worker == nullptr)
        return false;
    return QMetaObject::invokeMethod(worker, [worker, function]() { function(worker); }, Qt::QueuedConnection);
}

QString Audio::m_customBackgroundMusicFileName;

void Audio::init()
{
    QMutexLocker lock(&AudioMutex);
    if (Worker != nullptr)
        return;
    AudioThread = new QThread;
    AudioThread->setObjectName(QStringLiteral("AudioPlayback"));
    Worker = new AudioWorker;
    Worker->moveToThread(AudioThread);
    QObject::connect(AudioThread, &QThread::finished, Worker, &QObject::deleteLater);
    AudioThread->start();
}

void Audio::quit()
{
    QMutexLocker lock(&AudioMutex);
    if (Worker == nullptr)
        return;
    QMetaObject::invokeMethod(Worker, []() {
        Worker->stopAll();
        Worker->sounds.clear();
        Worker->music->shutdown();
    }, Qt::BlockingQueuedConnection);
    AudioThread->quit();
    AudioThread->wait();
    delete AudioThread;
    AudioThread = nullptr;
    Worker = nullptr;
    resetCustomBackgroundMusicFileName();
}

void Audio::play(const QString &fileName, bool continuePlayWhenPlaying /* = false*/)
{
    dispatchAudio([fileName, continuePlayWhenPlaying](AudioWorker *worker) { worker->play(fileName, continuePlayWhenPlaying); });
}

void Audio::setEffectVolume(float volume)
{
    dispatchAudio([volume](AudioWorker *worker) {
        worker->effectVolume = volume;
        foreach (const QString &key, worker->sounds.keys())
            worker->sounds[key]->setVolume(volume);
    });
}

void Audio::setBGMVolume(float volume)
{
    dispatchAudio([volume](AudioWorker *worker) { worker->music->setVolume(volume); });
}

void Audio::playBGM(const QString &fileNames, bool random /* = false*/, bool playAll, bool isGeneralName)
{
    const QString names = m_customBackgroundMusicFileName.isEmpty() ? fileNames : m_customBackgroundMusicFileName;
    BackgroundPlaying.store(true);
    if (!dispatchAudio([names, random, playAll, isGeneralName](AudioWorker *worker) { worker->music->play(names, random, playAll, isGeneralName); }))
        BackgroundPlaying.store(false);
}

void Audio::stopBGM()
{
    BackgroundPlaying.store(false);
    dispatchAudio([](AudioWorker *worker) { worker->music->stop(); });
}

bool Audio::isBackgroundMusicPlaying()
{
    return BackgroundPlaying.load();
}

void Audio::stopAll()
{
    BackgroundPlaying.store(false);
    dispatchAudio([](AudioWorker *worker) { worker->stopAll(); });
    resetCustomBackgroundMusicFileName();
}

QString Audio::getVersion()
{
    return QStringLiteral("Qt Multimedia (Ogg Vorbis)");
}

#include "audio_qt.moc"

#endif // AUDIO_SUPPORT
