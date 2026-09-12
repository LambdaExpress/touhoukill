#include "mainwindow.h"
#include "AboutUs.h"
#include "audio.h"
#include "cardoverview.h"
#include "client.h"
#include "configdialog.h"
#include "connectiondialog.h"
#include "dialogsupport.h"
#include "generaloverview.h"
#include "lua.hpp"
#include "pixmapanimation.h"
#include "record-analysis.h"
#include "recorder.h"
#include "roomscene.h"
#include "server.h"
#include "sgswindow.h"
#include "startscene.h"
#include "ui_mainwindow.h"
#include "updatedialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCommandLinkButton>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGroupBox>
#include <QGridLayout>
#include <QKeyEvent>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressBar>
#include <QSettings>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QtMath>

#ifdef Q_OS_WIN
#include <QWinTaskbarButton>
#include <QWinTaskbarProgress>
#endif

class FitView : public QGraphicsView
{
public:
    explicit FitView(QGraphicsScene *scene)
        : QGraphicsView(scene)
    {
        setSceneRect(Config.Rect);
        setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
#ifdef Q_OS_ANDROID
        // Touch input arrives as left-button mouse events, while the game relies on
        // the right button to cancel a selected card and to open the scene's context
        // menus. A press that is held without moving becomes a right click.
        m_longPressTimer.setSingleShot(true);
        m_longPressTimer.setInterval(LONG_PRESS_INTERVAL);
        connect(&m_longPressTimer, &QTimer::timeout, this, &FitView::onLongPress);
        viewport()->installEventFilter(this);
#endif
    }

#ifdef Q_OS_ANDROID
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Back || event->key() == Qt::Key_Escape) {
            if (MainWindow *window = qobject_cast<MainWindow *>(parentWidget()))
                window->showMobileMenu();
            event->accept();
            return;
        }
        QGraphicsView::keyPressEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != viewport())
            return QGraphicsView::eventFilter(watched, event);

        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            const QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() != Qt::LeftButton)
                break;
            m_pressPosition = mouseEvent->pos();
            m_skillPageOpened = false;
            m_longPressTimer.start();
            break;
        }
        case QEvent::MouseMove: {
            const QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (m_longPressTimer.isActive() && (mouseEvent->pos() - m_pressPosition).manhattanLength() > DRAG_TOLERANCE)
                m_longPressTimer.stop();
            break;
        }
        case QEvent::MouseButtonRelease:
            m_longPressTimer.stop();
            if (m_skillPageOpened) {
                m_skillPageOpened = false;
                m_rightClickEmitted = false;
                return true;
            }
            if (m_rightClickEmitted) {
                // The long press already delivered its own right click, so the real
                // release must not additionally act as a left click.
                m_rightClickEmitted = false;
                return true;
            }
            break;
        default:
            break;
        }

        return QGraphicsView::eventFilter(watched, event);
    }
#endif

    void resizeEvent(QResizeEvent *event) override
    {
        QGraphicsView::resizeEvent(event);
        MainWindow *main_window = qobject_cast<MainWindow *>(parentWidget());
        if (scene()->inherits("RoomScene")) {
            RoomScene *room_scene = qobject_cast<RoomScene *>(scene());
            QRectF newSceneRect(0, 0, event->size().width(), event->size().height());
            room_scene->setSceneRect(newSceneRect);
            room_scene->adjustItems();
            setSceneRect(room_scene->sceneRect());
            if (newSceneRect != room_scene->sceneRect())
                fitInView(room_scene->sceneRect(), Qt::KeepAspectRatio);
            else
                resetTransform();
            main_window->setBackgroundBrush(false);
            return;
        } else if (scene()->inherits("StartScene")) {
            StartScene *start_scene = qobject_cast<StartScene *>(scene());
            QRectF newSceneRect(-event->size().width() / 2, -event->size().height() / 2, event->size().width(), event->size().height());
            start_scene->setSceneRect(newSceneRect);
            setSceneRect(start_scene->sceneRect());
            // The start scene is laid out in viewport coordinates, so it is shown one to
            // one. The transform has to be cleared explicitly because a room scene may
            // have left its own fitInView transform behind.
            resetTransform();
            start_scene->adjustItems();
        }
        if (main_window != nullptr)
            main_window->setBackgroundBrush(true);
    }

#ifdef Q_OS_ANDROID
private:
    void onLongPress()
    {
        RoomScene *room_scene = qobject_cast<RoomScene *>(scene());
        if ((room_scene != nullptr) && room_scene->showSkillOverviewAt(mapToScene(m_pressPosition))) {
            m_skillPageOpened = true;
            return;
        }

        emitSyntheticRightClick();
    }

    // A held press stands in for the right button, which a touch screen does not have,
    // but only where the right button does something: a card, a player photo or the
    // table background. A control that declares the left button alone -- the menu
    // buttons, the skill dock -- is clicked with a single button, so substituting a
    // right click there brings nothing and costs everything: the release is swallowed
    // and the control never fires. A deliberate press easily outlasts the long-press
    // interval, which is what made every button in the interface look dead.
    bool rightClickIsMeaningful() const
    {
        const QGraphicsItem *item = itemAt(m_pressPosition);
        if (item == nullptr)
            return true;

        const Qt::MouseButtons accepted = item->acceptedMouseButtons();
        return (accepted & Qt::LeftButton) == 0 || (accepted & Qt::RightButton) != 0;
    }

    void emitSyntheticRightClick()
    {
        if (!rightClickIsMeaningful())
            return;

        m_rightClickEmitted = true;

        const QPoint viewportPos = m_pressPosition;
        const QPoint globalPos = viewport()->mapToGlobal(viewportPos);

        QMouseEvent press(QEvent::MouseButtonPress, viewportPos, globalPos, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, viewportPos, globalPos, Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport(), &release);

        // A real right click is followed by a context menu event, which is the only
        // way to reach the room's miscellaneous menu. It is delivered to the
        // viewport just like the real one: the scroll area's filter routes it to
        // QGraphicsView::contextMenuEvent, which converts the viewport position to
        // scene coordinates itself. Calling QWidget::mapFrom here instead would be
        // wrong -- it requires the argument to be an ancestor of the widget, while
        // the viewport is a child of the view, and it dereferences null in release
        // builds once the walk runs off the top of the parent chain.
        QContextMenuEvent contextMenuEvent(QContextMenuEvent::Mouse, viewportPos, globalPos);
        QCoreApplication::sendEvent(viewport(), &contextMenuEvent);
    }

    QTimer m_longPressTimer;
    QPoint m_pressPosition;
    bool m_rightClickEmitted = false;
    bool m_skillPageOpened = false;

    // Deliberately longer than the platform's 500 ms long press. The gesture is a
    // secondary action here, so mistaking an ordinary press for it is worse than
    // making the gesture itself slower; on a loaded device the press and the release
    // arrive with enough of a gap between them to reach 500 ms on their own.
    static const int LONG_PRESS_INTERVAL = 700;
    static const int DRAG_TOLERANCE = 10;
#endif
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    scene = nullptr;

    setWindowTitle(tr("TouhouSatsu") + "    " + Sanguosha->getVersionName() + "    " + Sanguosha->getVersionNumber());

    connection_dialog = new ConnectionDialog(this);
    connect(ui->actionStart_Game, SIGNAL(triggered()), connection_dialog, SLOT(exec()));
    connect(connection_dialog, SIGNAL(accepted()), this, SLOT(startConnection()));

    config_dialog = new ConfigDialog(this);
    connect(ui->actionConfigure, SIGNAL(triggered()), config_dialog, SLOT(show()));
    connect(config_dialog, SIGNAL(bg_changed()), this, SLOT(changeBackground()));
    connect(config_dialog, SIGNAL(tableBg_changed()), this, SLOT(changeTableBg()));

    connect(ui->actionAbout_Qt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(ui->actionAcknowledgement_2, SIGNAL(triggered()), this, SLOT(on_actionAcknowledgement_triggered()));

    update_dialog = new UpdateDialog(this);

    StartScene *start_scene = new StartScene;
    //play title BGM
#ifdef AUDIO_SUPPORT
    if (Config.EnableBgMusic) {
        QString bgm = "audio/title/main.ogg";
        Audio::stopBGM();
        Audio::playBGM(bgm, true, true);
        Audio::setBGMVolume(Config.BGMVolume);
    }
#endif
    QList<QAction *> actions;
    actions << ui->actionStart_Game << ui->actionStart_Server << ui->actionPC_Console_Start << ui->actionReplay << ui->actionGeneral_Overview << ui->actionCard_Overview
            << ui->actionConfigure << ui->actionAbout_Us;

    foreach (QAction *action, actions)
        start_scene->addButton(action);
    view = new FitView(scene);

    setCentralWidget(view);
    restoreFromConfig();
#ifdef Q_OS_ANDROID
    menuBar()->hide();
    statusBar()->hide();
    view->setFrameShape(QFrame::NoFrame);
    mobile_menu_button = new QPushButton(tr("Menu"), view->viewport());
    DialogSupport::applyMobileStyle(mobile_menu_button);
    mobile_menu_button->setFixedSize(84, 48);
    mobile_menu_button->move(8, 6);
    connect(mobile_menu_button, &QPushButton::clicked, this, &MainWindow::showMobileMenu);
#endif

    BackLoader::preload();
    gotoScene(start_scene);

    addAction(ui->actionShow_Hide_Menu);
    addAction(ui->actionFullscreen);

    systray = nullptr;

    if (Config.EnableAutoUpdate)
        update_dialog->checkForUpdate();
}

void MainWindow::restoreFromConfig()
{
    int width = Config.value("WindowWidth", 1366).toInt();
    int height = Config.value("WindowHeight", 706).toInt();
    int x = Config.value("WindowX", -8).toInt();
    int y = Config.value("WindowY", -8).toInt();
    bool maximized = Config.value("WindowMaximized", false).toBool();

    if (maximized)
        setWindowState(Qt::WindowMaximized);
    else {
        resize(QSize(width, height));
        move(x, y);
    }

    QFont font;
    if (Config.UIFont != font)
        QApplication::setFont(Config.UIFont, "QTextEdit");

    ui->actionEnable_Hotkey->setChecked(Config.EnableHotKey);
    ui->actionNever_nullify_my_trick->setChecked(Config.NeverNullifyMyTrick);
    ui->actionNever_nullify_my_trick->setEnabled(false);
}

void MainWindow::closeEvent(QCloseEvent * /*event*/)
{
    Config.setValue("WindowWidth", width());
    Config.setValue("WindowHeight", height());
    Config.setValue("WindowX", x());
    Config.setValue("WindowY", y());
    Config.setValue("WindowMaximized", bool(windowState() & Qt::WindowMaximized));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::gotoScene(QGraphicsScene *scene)
{
    if (this->scene != nullptr)
        this->scene->deleteLater();
    this->scene = scene;
    view->setScene(scene);
    /* @todo: Need a better way to replace the magic number '4' */
    //QResizeEvent e(QSize(view->size().width() - 4, view->size().height() - 4), view->size());
    QResizeEvent e(QSize(view->size().width(), view->size().height()), view->size());
    view->resizeEvent(&e);
#ifdef Q_OS_ANDROID
    mobile_menu_button->setVisible(scene->inherits("RoomScene"));
    mobile_menu_button->raise();
#endif
    //play BGM
#ifdef AUDIO_SUPPORT
    if (Config.EnableBgMusic && !Audio::isBackgroundMusicPlaying()) {
        Audio::stopBGM();
        Audio::playBGM("audio/title/main.ogg", true, true);
        Audio::setBGMVolume(Config.BGMVolume);
    }
#endif
    changeBackground();
}

void MainWindow::on_actionExit_triggered()
{
    QMessageBox::StandardButton result = QMessageBox::question(this, tr("TouhouSatsu"), tr("Are you sure to exit?"), QMessageBox::Ok | QMessageBox::Cancel);
    if (result == QMessageBox::Ok) {
        delete systray;
        systray = nullptr;
        close();
    }
}

void MainWindow::on_actionStart_Server_triggered()
{
#ifdef Q_OS_ANDROID
    startMobileRoom(false);
#else
    ServerDialog *dialog = new ServerDialog(this);
    if (!dialog->config())
        return;

    Server *server = new Server(this);
    if (!server->listen()) {
        QMessageBox::warning(this, tr("Warning"), tr("Can not start server!"));
        return;
    }

    server->daemonize();

    ui->actionStart_Game->disconnect();
    connect(ui->actionStart_Game, SIGNAL(triggered()), this, SLOT(startGameInAnotherInstance()));

    StartScene *start_scene = qobject_cast<StartScene *>(scene);
    if (start_scene != nullptr) {
        start_scene->switchToServer(server);
        if (Config.value("EnableMinimizeDialog", false).toBool())
            on_actionMinimize_to_system_tray_triggered();
    }
#endif
}

void MainWindow::checkVersion(const QString &server_version, const QString &server_mod)
{
    Client *client = qobject_cast<Client *>(sender());

    QString client_mod = Sanguosha->getMODName();
    if (client_mod != server_mod) {
        client->disconnectFromHost();
        QMessageBox::warning(this, tr("Warning"), tr("Client MOD name is not same as the server!"));
        return;
    }
    QString client_version = Sanguosha->getVersionNumber();

    if (server_version == client_version) {
        client->signup();
        connect(client, SIGNAL(server_connected()), SLOT(enterRoom()));
        return;
    }

    client->disconnectFromHost();

    QString text = tr("Server version is %1, client version is %2 <br/>").arg(server_version).arg(client_version);
    if (server_version > client_version)
        text.append(tr("Your client version is older than the server's, please update it <br/>"));
    else
        text.append(tr("The server version is older than your client version, please ask the server to update<br/>"));

    if (!Config.EnableAutoUpdate)
        text.append(tr("Enable auto update from the config dialog, and restart the game to check update."));
    else if (Config.AutoUpdateNeedsRestart) {
        if (Config.AutoUpdateDataRececived)
            text.append(tr("An error occurred when parsing update info. Please restart the game and retry auto updating."));
        else
            text.append(tr("Please restart the game and try auto updating."));
    } else if (!Config.AutoUpdateDataRececived)
        text.append(tr("Please wait a minute for downloading update info."));
    else
        text.append(tr("It seems like your version is the latest version. Either the server is using a test version, or auto updater is not up-to-date."));

    QMessageBox::warning(this, tr("Warning"), text);
}

void MainWindow::startConnection()
{
    Client *client = new Client(this);

    connect(client, SIGNAL(version_checked(QString, QString)), SLOT(checkVersion(QString, QString)));
    connect(client, SIGNAL(error_message(QString)), SLOT(networkError(QString)));
}

void MainWindow::on_actionReplay_triggered()
{
    QString location = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QString last_dir = Config.value("LastReplayDir").toString();
    if (!last_dir.isEmpty())
        location = last_dir;

    QString filename = QFileDialog::getOpenFileName(this, tr("Select a reply file"), location, tr("Pure text replay file (*.txt);; Image replay file (*.png)"));

    if (filename.isEmpty())
        return;

    QFileInfo file_info(filename);
    last_dir = file_info.absoluteDir().path();
    Config.setValue("LastReplayDir", last_dir);

    Client *client = new Client(this, filename);
    connect(client, SIGNAL(server_connected()), SLOT(enterRoom()));
    client->signup();
}

void MainWindow::networkError(const QString &error_msg)
{
    if (isVisible())
        QMessageBox::warning(this, tr("Network error"), error_msg);
}

void BackLoader::preload()
{
    QStringList emotions = G_ROOM_SKIN.getAnimationFileNames();

    foreach (QString emotion, emotions) {
        int n = PixmapAnimation::GetFrameCount(emotion);
        for (int i = 0; i < n; i++) {
            QString filename = QString("image/system/emotion/%1/%2.png").arg(emotion).arg(QString::number(i));
            G_ROOM_SKIN.getPixmapFromFileName(filename);
        }
    }
}

void MainWindow::enterRoom()
{
    if (QUrl(Config.HostAddress).path().length() == 0) {
        // add current ip to history only if the modifiers does not exist.
        // add the last connected address to the first one. DO NOT SORT
        if (Config.HistoryIPs.contains(Config.HostAddress))
            Config.HistoryIPs.removeAll(Config.HostAddress);
        Config.HistoryIPs.prepend(Config.HostAddress);
        Config.setValue("HistoryUrls", Config.HistoryIPs);
    }

    ui->actionStart_Game->setEnabled(false);
    ui->actionStart_Server->setEnabled(false);
#ifdef Q_OS_ANDROID
    ui->actionPC_Console_Start->setEnabled(false);
#endif

    RoomScene *room_scene = new RoomScene(this);
    ui->actionView_Discarded->setEnabled(true);
    ui->actionView_distance->setEnabled(true);
    ui->actionServerInformation->setEnabled(true);
    ui->actionSurrender->setEnabled(true);
    ui->actionNever_nullify_my_trick->setEnabled(true);
    ui->actionSaveRecord->setEnabled(true);

    connect(ClientInstance, SIGNAL(surrender_enabled(bool)), ui->actionSurrender, SLOT(setEnabled(bool)));

    connect(ui->actionView_Discarded, SIGNAL(triggered()), room_scene, SLOT(toggleDiscards()));
    connect(ui->actionView_distance, SIGNAL(triggered()), room_scene, SLOT(viewDistance()));
    connect(ui->actionServerInformation, SIGNAL(triggered()), room_scene, SLOT(showServerInformation()));
    connect(ui->actionSurrender, SIGNAL(triggered()), room_scene, SLOT(surrender()));
    connect(ui->actionSaveRecord, SIGNAL(triggered()), room_scene, SLOT(saveReplayRecord()));

    if (ServerInfo.EnableCheat) {
        ui->menuCheat->setEnabled(true);

        connect(ui->actionDeath_note, SIGNAL(triggered()), room_scene, SLOT(makeKilling()));
        connect(ui->actionDamage_maker, SIGNAL(triggered()), room_scene, SLOT(makeDamage()));
        connect(ui->actionRevive_wand, SIGNAL(triggered()), room_scene, SLOT(makeReviving()));
        connect(ui->actionExecute_script_at_server_side, SIGNAL(triggered()), room_scene, SLOT(doScript()));
    } else {
        ui->menuCheat->setEnabled(false);
        ui->actionDeath_note->disconnect();
        ui->actionDamage_maker->disconnect();
        ui->actionRevive_wand->disconnect();
        ui->actionExecute_script_at_server_side->disconnect();
    }

    connect(room_scene, SIGNAL(restart()), this, SLOT(startConnection()));
    connect(room_scene, SIGNAL(return_to_start()), this, SLOT(gotoStartScene()));

    gotoScene(room_scene);
#ifdef Q_OS_ANDROID
    if (mobile_fill_robots) {
        auto fill = [this](bool owner) {
            if (owner && mobile_fill_robots && ClientInstance != nullptr) {
                mobile_fill_robots = false;
                ClientInstance->fillRobots();
            }
        };
        connect(Self, &ClientPlayer::owner_changed, this, fill);
        fill(Self->isOwner());
    }
#endif
}

void MainWindow::gotoStartScene()
{
    //play BGM
#ifdef AUDIO_SUPPORT
    if (Config.EnableBgMusic && !Audio::isBackgroundMusicPlaying()) {
        Audio::stopBGM();
        Audio::playBGM("audio/title/main.ogg", true, true);
        Audio::setBGMVolume(Config.BGMVolume);
    }
#endif
    ServerInfo.DuringGame = false;
#ifdef Q_OS_ANDROID
    mobile_fill_robots = false;
    ui->actionStart_Game->setEnabled(true);
    ui->actionStart_Server->setEnabled(true);
    ui->actionPC_Console_Start->setEnabled(true);
#endif
    QList<Server *> servers = findChildren<Server *>();
    foreach (Server *server, servers)
        server->shutdown();
    if (!servers.isEmpty())
        servers.first()->deleteLater();

    StartScene *start_scene = new StartScene;

    QList<QAction *> actions;
    actions << ui->actionStart_Game << ui->actionStart_Server << ui->actionPC_Console_Start << ui->actionReplay << ui->actionGeneral_Overview << ui->actionCard_Overview
            << ui->actionConfigure << ui->actionAbout_Us;

    foreach (QAction *action, actions)
        start_scene->addButton(action);

    setCentralWidget(view);

    ui->menuCheat->setEnabled(false);
    ui->actionDeath_note->disconnect();
    ui->actionDamage_maker->disconnect();
    ui->actionRevive_wand->disconnect();
    ui->actionExecute_script_at_server_side->disconnect();
    gotoScene(start_scene);

    addAction(ui->actionShow_Hide_Menu);
    addAction(ui->actionFullscreen);

    delete systray;
    systray = nullptr;
    if (ClientInstance != nullptr) {
        if (Self != nullptr) {
            delete Self;
            Self = nullptr;
        }
        delete ClientInstance;
        ClientInstance = nullptr;
    }
}

void MainWindow::startGameInAnotherInstance()
{
    QProcess::startDetached(QApplication::applicationFilePath(), QStringList());
}

void MainWindow::on_actionGeneral_Overview_triggered()
{
    GeneralOverview *overview = GeneralOverview::getInstance(this);
    overview->fillGenerals(Sanguosha->findChildren<const General *>());
    overview->show();
}

void MainWindow::on_actionCard_Overview_triggered()
{
    CardOverview *overview = CardOverview::getInstance(this);
    overview->loadFromAll();
    overview->show();
}

void MainWindow::on_actionEnable_Hotkey_toggled(bool checked)
{
    if (Config.EnableHotKey != static_cast<int>(checked)) {
        Config.EnableHotKey = checked;
        Config.setValue("EnableHotKey", checked);
    }
}

void MainWindow::on_actionNever_nullify_my_trick_toggled(bool checked)
{
    if (Config.NeverNullifyMyTrick != static_cast<int>(checked)) {
        Config.NeverNullifyMyTrick = checked;
        Config.setValue("NeverNullifyMyTrick", checked);
    }
}

void MainWindow::on_actionAbout_triggered()
{
    // Cao Cao's pixmap
    QString content = "<center><img src='image/system/shencc.png'> <br /> </center>";

    // Cao Cao' poem
    QString poem = tr("Disciples dressed in blue, my heart worries for you. You are the cause, of this song without pause");
    content.append(QString("<p align='right'><i>%1</i></p>").arg(poem));

    // Cao Cao's signature
    QString signature = tr("\"A Short Song\" by Cao Cao");
    content.append(QString("<p align='right'><i>%1</i></p>").arg(signature));

    //QString email = "moligaloo@gmail.com";
    //content.append(tr("This is the open source clone of the popular <b>Sanguosha</b> game,"
    //    "totally written in C++ Qt GUI framework <br />"
    //    "My Email: <a href='mailto:%1' style = \"color:#0072c1; \">%1</a> <br/>"
    //    "My QQ: 365840793 <br/>"
    //    "My Weibo: http://weibo.com/moligaloo <br/>").arg(email));
    content.append(tr("This is the open source clone of the popular <b>Sanguosha</b> game,"
                      "totally written in C++ Qt GUI framework <br />"));
    //"My QQ: 384318315 <br/>"
    QString config;

#ifdef QT_NO_DEBUG
    config = "release";
#else
    config = "debug";
#endif

    content.append(tr("Current version: %1 %2 (%3)<br/>").arg(Sanguosha->getVersion()).arg(config).arg(Sanguosha->getVersionName()));

    const char *date = __DATE__;
    const char *time = __TIME__;
    content.append(tr("Compilation time: %1 %2 <br/>").arg(date).arg(time));

    QString project_url = "https://github.com/lwtmusou/touhoukill";
    content.append(tr("Source code: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg(project_url));

    QString forum_url = "http://qsanguosha.org";
    content.append(tr("Forum: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg(forum_url));

    Window *window = new Window(tr("About QSanguosha"), QSize(420, 465));
    scene->addItem(window);
    window->setZValue(32766);

    window->addContent(content);
    window->addCloseButton(tr("OK"));
    window->shift(scene->inherits("RoomScene") ? scene->width() : 0, scene->inherits("RoomScene") ? scene->height() : 0);

    window->appear();
}

void MainWindow::on_actionAbout_Us_triggered()
{
    AboutUsDialog *dialog = new AboutUsDialog(this);
    dialog->show();
}

void MainWindow::setBackgroundBrush(bool centerAsOrigin)
{
    if (scene != nullptr) {
        QPixmap pixmap(Config.BackgroundImage);
        QBrush brush(pixmap);
        qreal sx = (qreal)width() / qreal(pixmap.width());
        qreal sy = (qreal)height() / qreal(pixmap.height());

        QTransform transform;
        if (centerAsOrigin)
            transform.translate(-(qreal)width() / 2, -(qreal)height() / 2);
        transform.scale(sx, sy);
        brush.setTransform(transform);
        scene->setBackgroundBrush(brush);
    }
}

void MainWindow::changeBackground()
{
    bool centerAsOrigin = scene != nullptr && !scene->inherits("RoomScene");
    setBackgroundBrush(centerAsOrigin);

    if (scene->inherits("StartScene")) {
        StartScene *start_scene = qobject_cast<StartScene *>(scene);
        start_scene->setServerLogBackground();
    }
}

void MainWindow::changeTableBg()
{
    if (!scene->inherits("RoomScene"))
        return;

    RoomSceneInstance->changeTableBg();
}

void MainWindow::on_actionFullscreen_triggered()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::on_actionShow_Hide_Menu_triggered()
{
    QMenuBar *menu_bar = menuBar();
    menu_bar->setVisible(!menu_bar->isVisible());
}

void MainWindow::on_actionMinimize_to_system_tray_triggered()
{
    if (systray == nullptr) {
        QIcon icon("image/system/magatamas/5.png");
        systray = new QSystemTrayIcon(icon, this);

        QAction *appear = new QAction(tr("Show main window"), this);
        connect(appear, SIGNAL(triggered()), this, SLOT(show()));

        QMenu *menu = new QMenu;
        menu->addAction(appear);
        menu->addMenu(ui->menuGame);
        menu->addMenu(ui->menuView);
        menu->addMenu(ui->menuOptions);
        menu->addMenu(ui->menuHelp);

        systray->setContextMenu(menu);
    }

    systray->show();
    systray->showMessage(windowTitle(), tr("Game is minimized"));

    hide();
}

void MainWindow::on_actionRole_assign_table_triggered()
{
    QString content;

    QStringList headers;
    headers << tr("Count") << tr("Lord") << tr("Loyalist") << tr("Rebel") << tr("Renegade");
    foreach (QString header, headers)
        content += QString("<th>%1</th>").arg(header);

    content = QString("<tr>%1</tr>").arg(content);

    QStringList rows;
    rows << "2 1 0 1 0"
         << "3 1 0 1 1"
         << "4 1 0 2 1"
         << "5 1 1 2 1"
         << "6 1 1 3 1"
         << "6d 1 1 2 2"
         << "7 1 2 3 1"
         << "8 1 2 4 1"
         << "8d 1 2 3 2"
         << "8z 1 3 4 0"
         << "9 1 3 4 1"
         << "10 1 3 4 2"
         << "10z 1 4 5 0"
         << "10o 1 3 5 1";

    foreach (QString row, rows) {
        QStringList cells = row.split(" ");
        QString header = cells.takeFirst();
        if (header.endsWith("d")) {
            header.chop(1);
            header += tr(" (double renegade)");
        }
        if (header.endsWith("z")) {
            header.chop(1);
            header += tr(" (no renegade)");
        }
        if (header.endsWith("o")) {
            header.chop(1);
            header += tr(" (single renegade)");
        }

        QString row_content;
        row_content = QString("<td>%1</td>").arg(header);
        foreach (QString cell, cells)
            row_content += QString("<td>%1</td>").arg(cell);

        content += QString("<tr>%1</tr>").arg(row_content);
    }

    content = QString("<table border='1'>%1</table").arg(content);

    Window *window = new Window(tr("Role assign table"), QSize(240, 450));
    scene->addItem(window);

    window->addContent(content);
    window->addCloseButton(tr("OK"));
    window->shift((scene != nullptr) && scene->inherits("RoomScene") ? scene->width() : 0, (scene != nullptr) && scene->inherits("RoomScene") ? scene->height() : 0);
    window->setZValue(32766);

    window->appear();
}

BroadcastBox::BroadcastBox(Server *server, QWidget *parent)
    : QDialog(parent)
    , server(server)
{
    setWindowTitle(tr("Broadcast"));

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(new QLabel(tr("Please input the message to broadcast")));

    text_edit = new QTextEdit;
    layout->addWidget(text_edit);

    QHBoxLayout *hlayout = new QHBoxLayout;
    hlayout->addStretch();
    QPushButton *ok_button = new QPushButton(tr("OK"));
    hlayout->addWidget(ok_button);

    layout->addLayout(hlayout);

    setLayout(layout);

    connect(ok_button, SIGNAL(clicked()), this, SLOT(accept()));
}

void BroadcastBox::accept()
{
    QDialog::accept();
    server->broadcast(text_edit->toPlainText());
}

void MainWindow::on_actionBroadcast_triggered()
{
    Server *server = findChild<Server *>();
    if (server == nullptr) {
        QMessageBox::warning(this, tr("Warning"), tr("Server is not started yet!"));
        return;
    }

    BroadcastBox *dialog = new BroadcastBox(server, this);
    dialog->exec();
}

void MainWindow::on_actionAcknowledgement_triggered()
{
    Window *window = new Window(QString(), QSize(1000, 677), "image/system/acknowledgement.png");
    scene->addItem(window);

    Button *button = window->addCloseButton(tr("OK"));
    button->moveBy(-85, -35);
    window->setZValue(32766);
    window->shift((scene != nullptr) && scene->inherits("RoomScene") ? scene->width() : 0, (scene != nullptr) && scene->inherits("RoomScene") ? scene->height() : 0);

    window->appear();
}

void MainWindow::on_actionPC_Console_Start_triggered()
{
#ifdef Q_OS_ANDROID
    startMobileRoom(true);
#else
    ServerDialog *dialog = new ServerDialog(this);
    if (!dialog->config())
        return;

    Server *server = new Server(this);
    if (!server->listen()) {
        QMessageBox::warning(this, tr("Warning"), tr("Can not start server!"));
        return;
    }

    server->createNewRoom();

    Config.HostAddress = "qths://127.0.0.1";
    startConnection();
#endif
}

#ifdef Q_OS_ANDROID
void MainWindow::startMobileRoom(bool practice)
{
    ServerDialog dialog(this);
    if (!dialog.config())
        return;
    QList<Server *> old_servers = findChildren<Server *>();
    foreach (Server *old_server, old_servers) {
        old_server->shutdown();
        old_server->deleteLater();
    }
    Server *server = new Server(this);
    if (!server->listen()) {
        QMessageBox::warning(this, tr("Warning"), tr("Can not start server!"));
        delete server;
        return;
    }
    server->createNewRoom();
    mobile_fill_robots = practice && Config.EnableAI;
    Config.HostAddress = QString("qths://127.0.0.1:%1").arg(Config.ServerPort);
    startConnection();
}

void MainWindow::showMobileMenu()
{
    QDialog dialog(this);
    dialog.setProperty("sgsMobileLayout", true);
    dialog.setWindowTitle(tr("Menu"));
    QVBoxLayout *root = new QVBoxLayout(&dialog);
    QHBoxLayout *header = new QHBoxLayout;
    QPushButton *back = new QPushButton(tr("Back to game"));
    connect(back, &QPushButton::clicked, &dialog, &QDialog::reject);
    header->addWidget(back);
    QLabel *title = new QLabel(tr("Menu"));
    title->setProperty("sgsHeading", true);
    header->addWidget(title, 1);
    root->addLayout(header);
    QTabWidget *tabs = new QTabWidget;
    const QList<QMenu *> menus = {ui->menuGame, ui->menuOptions, ui->menuHelp, ui->menuCheat};
    foreach (QMenu *menu, menus) {
        if (!menu->isEnabled())
            continue;
        QWidget *page = new QWidget;
        QGridLayout *grid = new QGridLayout(page);
        int count = 0;
        foreach (QAction *action, menu->actions()) {
            if (action->isSeparator() || action == ui->actionEnable_Hotkey)
                continue;
            QPushButton *button = new QPushButton(action->text().remove('&'));
            button->setEnabled(action->isEnabled());
            button->setCheckable(action->isCheckable());
            button->setChecked(action->isChecked());
            grid->addWidget(button, count / 2, count % 2);
            connect(button, &QPushButton::clicked, &dialog, [&dialog, action]() {
                dialog.accept();
                action->trigger();
            });
            ++count;
        }
        RoomScene *room = qobject_cast<RoomScene *>(scene);
        if (menu == ui->menuGame && room != nullptr) {
            QPushButton *automatic = new QPushButton;
            automatic->setCheckable(true);
            automatic->setEnabled(room->game_started && !room->isPerspectiveInputLocked() && ClientInstance->getReplayer() == nullptr);
            auto updateAutomatic = [automatic]() {
                const bool active = Self != nullptr && Self->getState() == "trust";
                automatic->setChecked(active);
                automatic->setText(active ? tr("Resume manual play") : tr("Enable auto play"));
            };
            updateAutomatic();
            connect(Self, &ClientPlayer::state_changed, automatic, updateAutomatic);
            connect(automatic, &QPushButton::clicked, &dialog, [&dialog, room]() {
                dialog.accept();
                room->trust();
            });
            grid->addWidget(automatic, count / 2, count % 2);
            ++count;
        }
        grid->setRowStretch((count + 1) / 2, 1);
        tabs->addTab(DialogSupport::createScrollArea(page), menu->title().remove('&'));
    }
    root->addWidget(tabs, 1);
    if (scene->inherits("RoomScene")) {
        QPushButton *leave = new QPushButton(tr("Leave room"));
        root->addWidget(leave);
        connect(leave, &QPushButton::clicked, &dialog, [this, &dialog]() {
            if (QMessageBox::question(&dialog, tr("Leave room"), tr("Leave the current game?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                dialog.accept();
                gotoStartScene();
            }
        });
    }
    dialog.exec();
}
#endif

void MainWindow::on_actionReplay_file_convert_triggered()
{
    QString filename = QFileDialog::getOpenFileName(this, tr("Please select a replay file"), Config.value("LastReplayDir").toString(),
                                                    tr("Pure text replay file (*.txt);; Image replay file (*.png)"));

    if (filename.isEmpty())
        return;

    QFile file(filename);
    if (file.open(QIODevice::ReadOnly)) {
        QFileInfo info(filename);
        QString tosave = info.absoluteDir().absoluteFilePath(info.baseName());

        if (filename.endsWith(".txt")) {
            tosave.append(".png");

            // txt to png
            Recorder::TXT2PNG(file.readAll()).save(tosave);

        } else if (filename.endsWith(".png")) {
            tosave.append(".txt");

            // png to txt
            QByteArray data = Recorder::PNG2TXT(filename);

            QFile tosave_file(tosave);
            if (tosave_file.open(QIODevice::WriteOnly))
                tosave_file.write(data);
        }
    }
}

void MainWindow::on_actionRecord_analysis_triggered()
{
    QString location = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QString filename = QFileDialog::getOpenFileName(this, tr("Load replay record"), location, tr("Pure text replay file (*.txt);; Image replay file (*.png)"));

    if (filename.isEmpty())
        return;

    QDialog *rec_dialog = new QDialog(this);
    rec_dialog->setWindowTitle(tr("Record Analysis"));
    rec_dialog->resize(800, 500);
    QTableWidget *table = new QTableWidget;

    RecAnalysis *record = new RecAnalysis(filename);
    QMap<QString, PlayerRecordStruct *> record_map = record->getRecordMap();
    table->setColumnCount(11);
    table->setRowCount(record_map.keys().length());
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    static QStringList labels;
    if (labels.isEmpty()) {
        labels << tr("ScreenName") << tr("General") << tr("Role") << tr("Living") << tr("WinOrLose") << tr("TurnCount") << tr("Recover") << tr("Damage") << tr("Damaged")
               << tr("Kill");
    }
    table->setHorizontalHeaderLabels(labels);
    table->setSelectionBehavior(QTableWidget::SelectRows);

    int i = 0;
    foreach (PlayerRecordStruct *rec, record_map.values()) {
        QTableWidgetItem *item = new QTableWidgetItem;
        QString screen_name = Sanguosha->translate(rec->m_screenName);
        if (rec->m_statue == "robot")
            screen_name += "(" + Sanguosha->translate("robot") + ")";

        item->setText(screen_name);
        table->setItem(i, 0, item);

        item = new QTableWidgetItem;
        QString generals = Sanguosha->translate(rec->m_generalName);
        if (!rec->m_general2Name.isEmpty())
            generals += "/" + Sanguosha->translate(rec->m_general2Name);
        item->setText(generals);
        table->setItem(i, 1, item);

        item = new QTableWidgetItem;
        item->setText(Sanguosha->translate(rec->m_role));
        table->setItem(i, 2, item);

        item = new QTableWidgetItem;
        item->setText(rec->m_isAlive ? tr("Alive") : tr("Dead"));
        table->setItem(i, 3, item);

        item = new QTableWidgetItem;
        bool is_win = record->getRecordWinners().contains(rec->m_role) || record->getRecordWinners().contains(record_map.key(rec));
        item->setText(is_win ? tr("Win") : tr("Lose"));
        table->setItem(i, 4, item);

        item = new QTableWidgetItem;
        item->setText(QString::number(rec->m_turnCount));
        table->setItem(i, 5, item);

        item = new QTableWidgetItem;
        item->setText(QString::number(rec->m_recover));
        table->setItem(i, 6, item);

        item = new QTableWidgetItem;
        item->setText(QString::number(rec->m_damage));
        table->setItem(i, 7, item);

        item = new QTableWidgetItem;
        item->setText(QString::number(rec->m_damaged));
        table->setItem(i, 8, item);

        item = new QTableWidgetItem;
        item->setText(QString::number(rec->m_kill));
        table->setItem(i, 9, item);
        i++;
    }

    table->resizeColumnsToContents();

    QLabel *label = new QLabel;
    label->setText(tr("Packages:") + record->getRecordPackages().join(","));

    QLabel *label_game_mode = new QLabel;
    label_game_mode->setText(tr("GameMode:") + Sanguosha->getModeName(record->getRecordGameMode()));

    QLabel *label_options = new QLabel;
    label_options->setText(tr("ServerOptions:") + record->getRecordServerOptions().join(","));

    QTextEdit *chat_info = new QTextEdit;
    chat_info->setReadOnly(chat_info != nullptr);
    chat_info->setText(record->getRecordChat());

    QLabel *table_chat_title = new QLabel;
    table_chat_title->setText(tr("Chat Information:"));

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(label);
    layout->addWidget(label_game_mode);
    layout->addWidget(label_options);
    layout->addWidget(table);
    layout->addSpacing(15);
    layout->addWidget(table_chat_title);
    layout->addWidget(chat_info);
    rec_dialog->setLayout(layout);

    rec_dialog->exec();
}

void MainWindow::on_actionView_ban_list_triggered()
{
    BanlistDialog *dialog = new BanlistDialog(this, true);
    dialog->exec();
}

void MainWindow::on_actionAbout_fmod_triggered()
{
    QString content = tr("FMOD is a proprietary audio library made by Firelight Technologies");
    content.append("<p align='center'> <img src='image/logo/fmod.png' /> </p> <br/>");

    QString address = "http://www.fmod.org";
    content.append(tr("Official site: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg(address));

#ifdef AUDIO_SUPPORT
    content.append(tr("Current version %1 <br/>").arg(Audio::getVersion()));
#endif

    Window *window = new Window(tr("About fmod"), QSize(500, 260));
    scene->addItem(window);

    window->addContent(content);
    window->addCloseButton(tr("OK"));
    window->setZValue(32766);
    window->shift((scene != nullptr) && scene->inherits("RoomScene") ? scene->width() : 0, (scene != nullptr) && scene->inherits("RoomScene") ? scene->height() : 0);

    window->appear();
}

void MainWindow::on_actionAbout_Lua_triggered()
{
    QString content = tr("Lua is a powerful, fast, lightweight, embeddable scripting language.");
    content.append("<p align='center'> <img src='image/logo/lua.png' /> </p> <br/>");

    QString address = "http://www.lua.org";
    content.append(tr("Official site: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg(address));

    content.append(tr("Current version %1 <br/>").arg(LUA_RELEASE));
    content.append(LUA_COPYRIGHT);

    Window *window = new Window(tr("About Lua"), QSize(500, 585));
    scene->addItem(window);

    window->addContent(content);
    window->addCloseButton(tr("OK"));
    window->setZValue(32766);
    window->shift((scene != nullptr) && scene->inherits("RoomScene") ? scene->width() : 0, (scene != nullptr) && scene->inherits("RoomScene") ? scene->height() : 0);

    window->appear();
}

void MainWindow::on_actionAbout_GPLv3_triggered()
{
    QString content = tr(
        "The GNU General Public License is the most widely used free software license, which guarantees end users the freedoms to use, study, share, and modify the software.");
    content.append("<p align='center'> <img src='image/logo/gplv3.png' /> </p> <br/>");

    QString address = "http://gplv3.fsf.org";
    content.append(tr("Official site: <a href='%1' style = \"color:#0072c1; \">%1</a> <br/>").arg(address));

    Window *window = new Window(tr("About GPLv3"), QSize(500, 225));
    scene->addItem(window);

    window->addContent(content);
    window->addCloseButton(tr("OK"));
    window->setZValue(32766);
    window->shift((scene != nullptr) && scene->inherits("RoomScene") ? scene->width() : 0, (scene != nullptr) && scene->inherits("RoomScene") ? scene->height() : 0);

    window->appear();
}

// ATTENTION!!!! this slot is for "Download/update contents" menu item
void MainWindow::on_actionDownload_Hero_Skin_and_BGM_triggered()
{
    if (!Config.EnableAutoUpdate) {
        QMessageBox::warning(this, tr("TouhouSatsu"), tr("Please enable auto update, restart the game and retry."));
        return;
    } else if (!Config.AutoUpdateDataRececived) {
        if (Config.AutoUpdateNeedsRestart) {
            QMessageBox::warning(this, tr("TouhouSatsu"), tr("Please restart the game and retry."));
            return;
        } else {
            QMessageBox::information(this, tr("TouhouSatsu"), tr("Please wait a minute for downloading update info."));
            return;
        }
    } else {
        if (Config.AutoUpdateNeedsRestart) {
            QMessageBox::warning(this, tr("TouhouSatsu"), tr("An error occurred when parsing update info. Please restart the game and retry."));
            return;
        } else {
            update_dialog->exec();
        }
    }
}
