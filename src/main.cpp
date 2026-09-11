#include "audio.h"
#include "dialogsupport.h"
#include "mainwindow.h"
#include "server.h"
#include "settings.h"

#ifdef Q_OS_ANDROID
#include "androidassets.h"

#include <QtAndroidExtras/QAndroidJniObject>
#include <QtAndroidExtras/QtAndroid>
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTranslator>

#ifdef Q_OS_ANDROID
namespace {

// Turns the Qt activity into a game window: no action bar, no status bar.
//
// Everything here must run on the Android UI thread. main() runs on Qt's
// "qtMainLoopThread", and touching the view hierarchy from there makes Android
// throw CalledFromWrongThreadException inside ViewRootImpl::requestLayout, which
// then aborts the process the next time a JNI reference is created.
void configureAndroidWindow()
{
    QtAndroid::runOnAndroidThread([]() {
        QAndroidJniObject activity = QtAndroid::androidActivity();
        if (!activity.isValid())
            return;

        // QtActivityLoader requests Window.FEATURE_ACTION_BAR unconditionally, so a
        // bar can exist even when the activity theme asks for none; hiding it is
        // the only reliable way to get rid of it. getActionBar() returns null when
        // the theme already suppressed the bar, in which case there is nothing left
        // to do.
        QAndroidJniObject actionBar = activity.callObjectMethod("getActionBar", "()Landroid/app/ActionBar;");
        if (actionBar.isValid())
            actionBar.callMethod<void>("hide", "()V");

        QAndroidJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (window.isValid()) {
            // WindowManager.LayoutParams.FLAG_FULLSCREEN
            window.callMethod<void>("addFlags", "(I)V", static_cast<jint>(0x00000400));
        }
    });
}

} // namespace
#endif

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "-server") == 0) {
        new QCoreApplication(argc, argv);
    } else {
        new QApplication(argc, argv);
        QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/plugins");

        // Keeps dialogs usable on screens smaller than the desktop layout assumes.
        DialogSupport::install(qApp);

#ifdef Q_OS_ANDROID
        configureAndroidWindow();
#endif

#ifdef Q_OS_OSX
        if (QStyleFactory::keys().contains("Fusion", Qt::CaseInsensitive))
            qApp->setStyle(QStyleFactory::create("Fusion"));
#endif
    }

#ifdef Q_OS_ANDROID
    // The application directory is read-only on Android and the bundled resources
    // live inside the APK. Materialise them into a writable directory and make it
    // the working directory, because the engine resolves everything relatively.
    {
        const QString androidDataDir = AndroidAssets::provisionDataDirectory();
        if (androidDataDir.isEmpty() || !QDir::setCurrent(androidDataDir)) {
            const char *message = "Failed to prepare the application data directory.";
            if (qobject_cast<QApplication *>(qApp) != nullptr)
                QMessageBox::critical(nullptr, "TouhouKill", message);
            else
                qCritical("%s", message);
            return 1;
        }
    }
#elif defined(QT_NO_DEBUG)
    QDir::setCurrent(qApp->applicationDirPath());
#endif

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    QDir dir(QString("lua"));
    if (dir.exists() && (dir.exists(QString("config.lua")))) {
        // things look good and use current dir
    } else {
        QDir::setCurrent(qApp->applicationFilePath().replace("games", "share"));
    }
#endif

    // initialize random seed for later use
    qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));

    QTranslator qt_translator;
    QTranslator translator;
    qt_translator.load("qt_zh_CN.qm");
    translator.load("sanguosha.qm");

    qApp->installTranslator(&qt_translator);
    qApp->installTranslator(&translator);

    Sanguosha->init();
    Config.init();

    if (qApp->arguments().contains("-server")) {
        Server *server = new Server(qApp);
        printf("Server is starting on port %u\n", Config.ServerPort);

        if (server->listen())
            printf("Starting successfully\n");
        else
            printf("Starting failed!\n");

        return qApp->exec();
    }

    QFile file("sanguosha.qss");
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        qApp->setStyleSheet(stream.readAll());
    }

    qApp->setFont(Config.AppFont);

#ifdef AUDIO_SUPPORT
    Audio::init();
#endif

    MainWindow *main_window = new MainWindow;

    Sanguosha->setParent(main_window);
    main_window->show();

    foreach (QString arg, qApp->arguments()) {
        if (arg.startsWith("-connect:")) {
            arg.remove("-connect:");
            Config.HostAddress = arg;
            Config.setValue("HostUrl", arg);

            main_window->startConnection();
            break;
        }
    }

    int execResult = qApp->exec();
    delete qApp;
    return execResult;
}
