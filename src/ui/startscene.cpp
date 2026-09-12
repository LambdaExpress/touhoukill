#include "startscene.h"
#include "audio.h"
#include "dialogsupport.h"
#include "engine.h"

#include <QGraphicsDropShadowEffect>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsProxyWidget>
#include <QGridLayout>
#include <QPushButton>
#include <QNetworkInterface>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

StartScene::StartScene()
{
    // game logo
    logo = new QSanSelectableItem("image/logo/logo.png", true);
    addItem(logo);

    //the website URL
    QFont website_font(Config.SmallFont);
    website_font.setStyle(QFont::StyleItalic);
    website_text = addSimpleText(tr("TouhouSatsu QQ Qun: 384318315"), website_font);
    website_text->setBrush(Qt::white);
    server_log = nullptr;
#ifdef Q_OS_ANDROID
    QWidget *panel = new QWidget;
    panel->setObjectName("mobilePanel");
    DialogSupport::applyMobileStyle(panel);
    mobile_grid = new QGridLayout(panel);
    mobile_grid->setContentsMargins(16, 16, 16, 16);
    mobile_grid->setSpacing(12);
    mobile_panel = addWidget(panel);
    QFont captionFont(QStringLiteral("sans-serif"));
    captionFont.setPixelSize(14);
    website_text->setFont(captionFont);
#endif
}

void StartScene::addButton(QAction *action)
{
#ifdef Q_OS_ANDROID
    QString text = action->text();
    if (action->objectName() == "actionStart_Game")
        text = tr("Online play");
    else if (action->objectName() == "actionStart_Server")
        text = tr("Create room");
    else if (action->objectName() == "actionPC_Console_Start")
        text = tr("Solo practice");
    QPushButton *button = new QPushButton(text);
    button->setProperty("sgsPrimaryAction", action->objectName() == "actionStart_Game" || action->objectName() == "actionPC_Console_Start");
    button->setEnabled(action->isEnabled());
    connect(action, &QAction::changed, button, [button, action]() { button->setEnabled(action->isEnabled()); });
    connect(button, &QPushButton::clicked, action, &QAction::trigger);
    const int index = mobile_grid->count();
    mobile_grid->addWidget(button, index / 2, index % 2);
    adjustItems();
#else
    Button *button = new Button(action->text());
    button->setMute(false);

    connect(button, SIGNAL(clicked()), action, SLOT(trigger()));
    addItem(button);

    buttons << button;
    adjustItems();
#endif
}

void StartScene::adjustItems()
{
    QRectF rect = sceneRect();
    if (rect.isEmpty())
        return;

#ifdef Q_OS_ANDROID
    if (server_log == nullptr) {
        const qreal panelWidth = rect.width() * 0.54;
        const qreal panelHeight = qMin<qreal>(rect.height() - 24, 292);
        mobile_panel->setGeometry(QRectF(rect.right() - panelWidth - 12, rect.center().y() - panelHeight / 2, panelWidth, panelHeight));
        const qreal logoScale = qMin((rect.width() * 0.40) / 418.0, (rect.height() * 0.40) / 167.0);
        logo->setTransform(QTransform::fromScale(logoScale, logoScale));
        const qreal centerX = rect.left() + rect.width() * 0.22;
        logo->setPos(centerX - 209 * logoScale, rect.center().y() - 100 * logoScale);
        website_text->setPos(centerX - website_text->boundingRect().width() / 2, rect.center().y() + 94 * logoScale);
        return;
    }
#endif
    if (server_log != nullptr) {
        // The server console keeps to the right of the shrunken logo and inside the
        // scene, whatever the scene rect is.
        const qreal left = rect.left() + (rect.width() * 0.34);
        const qreal top = rect.top() + (rect.height() * 0.06);
        server_log->resize(qMax(80, static_cast<int>(rect.right() - left - 8)), qMax(60, static_cast<int>(rect.bottom() - top - 8)));
        server_log->move(static_cast<int>(left), static_cast<int>(top));
        return;
    }

    const qreal buttonW = 189;
    const qreal buttonH = 46;
    const qreal gap = 10;
    const qreal pitch = buttonH * 1.2;
    const int rows = (buttons.length() + 1) / 2;
    const qreal blockH = (rows > 0) ? (((rows - 1) * pitch) + buttonH) : 0;

    // The website line is placed first, because the band the logo and buttons go in ends
    // where that line begins.
    //
    // The original layout pinned the line to the 1024x640 design rect, which is centred
    // on the origin: right edge at x=512, bottom at y=304.8. Keeping those coordinates
    // while the scene is at least that large preserves the desktop appearance exactly;
    // clamping is what brings the line back on screen when the viewport is smaller than
    // the design, which is where it used to be cut off.
    qreal lineTop = rect.bottom();
    if (website_text != nullptr) {
        const qreal designRight = 512;
        const qreal designBottom = 304.8;
        const QSizeF textSize = website_text->boundingRect().size();
        const qreal x = qMin(designRight, rect.right() - 6) - textSize.width();
        const qreal y = qMin(designBottom, rect.bottom() - 6) - textSize.height();
        website_text->setPos(qMax(rect.left() + 6, x), qMax(rect.top() + 6, y));
        lineTop = website_text->pos().y();
    }

    const qreal bandTop = rect.top() + 6;
    const qreal bandBottom = lineTop - 6;
    const qreal bandH = qMax<qreal>(1, bandBottom - bandTop);

    // logo.png is 418x167. It shrinks as far as it must to leave room for the button
    // block, so the two never overlap however short the window is.
    const qreal logoW = 418;
    const qreal logoH = 167;
    const qreal maxLogoH = qMin(bandH * 0.34, bandH - gap - blockH);
    const qreal logoScale = qMin(1.0, qMin((rect.width() * 0.45) / logoW, qMax<qreal>(0, maxLogoH) / logoH));
    const qreal logoDisplayH = logoH * logoScale;

    const qreal contentH = logoDisplayH + gap + blockH;
    const qreal startY = bandTop + ((bandH - contentH) / 2);

    // The transform is rebuilt rather than accumulated with scaleSmoothly(), because
    // this runs on every resize and any accumulating call would compound.
    logo->resetTransform();
    QTransform logoTransform = QTransform::fromTranslate(-logoW / 2, -logoH / 2);
    logoTransform.scale(logoScale, logoScale);
    logo->setTransform(logoTransform);
    // The item was created with center_as_origin, so its position addresses the centre.
    logo->setPos(rect.center().x(), startY + (logoDisplayH / 2));

    const qreal blockTop = startY + logoDisplayH + gap;
    for (int n = 0; n < buttons.length(); ++n) {
        Button *button = buttons.at(n);
        const int column = n / 4;
        const int row = n % 4;
        // Button::boundingRect() starts at the origin, so the position is its top left.
        button->setPos((column == 0) ? rect.center().x() - buttonW - (gap / 2) : rect.center().x() + (gap / 2), blockTop + (row * pitch));
    }
}

void StartScene::setServerLogBackground()
{
    if (server_log != nullptr) {
        // make its background the same as background, looks transparent
        QPalette palette;
        palette.setBrush(QPalette::Base, backgroundBrush());
        server_log->setPalette(palette);
    }
}

void StartScene::switchToServer(Server *server)
{
#ifdef AUDIO_SUPPORT
    Audio::quit();
#endif
    // performs leaving animation; the logo parks in the upper left of whatever the
    // scene rect actually is, rather than at an offset from the fixed design rect.
    const QRectF rect = sceneRect();
    QPropertyAnimation *logo_shift = new QPropertyAnimation(logo, "pos");
    logo_shift->setEndValue(QPointF(rect.left() + (rect.width() * 0.20), rect.top() + (rect.height() * 0.18)));

    QPropertyAnimation *logo_shrink = new QPropertyAnimation(logo, "scale");
    logo_shrink->setEndValue(0.5);

    QParallelAnimationGroup *group = new QParallelAnimationGroup(this);
    group->addAnimation(logo_shift);
    group->addAnimation(logo_shrink);
    group->start(QAbstractAnimation::DeleteWhenStopped);

    foreach (Button *button, buttons)
        delete button;
    buttons.clear();

    server_log = new QTextEdit();
    server_log->setReadOnly(true);
    server_log->setFrameShape(QFrame::NoFrame);
#ifdef Q_OS_LINUX
    server_log->setFont(QFont("DroidSansFallback", 12));
#else
    server_log->setFont(QFont("Verdana", 12));
#endif
    server_log->setTextColor(Config.TextEditColor);
    setServerLogBackground();
    addWidget(server_log);
    // Sized and placed by adjustItems(), which knows the scene rect; this keeps the
    // log on screen on a viewport much smaller than the 700x420 it was written for.
    adjustItems();

    printServerInfo();
    connect(server, SIGNAL(server_message(QString)), server_log, SLOT(append(QString)));
    update();
}

void StartScene::printServerInfo()
{
    QStringList items;
    QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    foreach (QHostAddress address, addresses) {
        quint32 ipv4 = address.toIPv4Address();
        if (ipv4 != 0U)
            items << address.toString();
    }

    items.sort();

    foreach (QString item, items) {
        if (item.startsWith("192.168.") || item.startsWith("10."))
            server_log->append(tr("Your LAN address: %1, this address is available only for hosts that in the same LAN").arg(item));
        else if (item == "127.0.0.1")
            server_log->append(tr("Your loopback address %1, this address is available only for your host").arg(item));
        else if (item.startsWith("5."))
            server_log->append(tr("Your Hamachi address: %1, the address is available for users that joined the same Hamachi network").arg(item));
        else if (!item.startsWith("169.254."))
            server_log->append(tr("Your other address: %1, if this is a public IP, that will be available for all cases").arg(item));
    }

    server_log->append(tr("Binding port number is %1").arg(Config.ServerPort));
    server_log->append(tr("Game mode is %1").arg(Sanguosha->getModeName(Config.GameMode)));
    server_log->append(tr("Player count is %1").arg(Sanguosha->getPlayerCount(Config.GameMode)));
    server_log->append(Config.OperationNoLimit ? tr("There is no time limit") : tr("Operation timeout is %1 seconds").arg(Config.OperationTimeout));
    server_log->append(Config.EnableCheat ? tr("Cheat is enabled") : tr("Cheat is disabled"));
    if (Config.EnableCheat)
        server_log->append(Config.FreeChoose ? tr("Free choose is enabled") : tr("Free choose is disabled"));

    if (Config.Enable2ndGeneral) {
        QString scheme_str;
        switch (Config.MaxHpScheme) {
        case 0:
            scheme_str = QString(tr("Sum - %1")).arg(Config.Scheme0Subtraction);
            break;
        case 1:
            scheme_str = tr("Minimum");
            break;
        case 2:
            scheme_str = tr("Maximum");
            break;
        case 3:
            scheme_str = tr("Average");
            break;
        }
        if (!isHegemonyGameMode(Config.GameMode))
            server_log->append(tr("Secondary general is enabled, max hp scheme is %1").arg(scheme_str));
    } else
        server_log->append(tr("Seconardary general is disabled"));

    server_log->append(Config.EnableSame ? tr("Same Mode is enabled") : tr("Same Mode is disabled"));

    if (Config.EnableAI) {
        server_log->append(tr("This server is AI enabled, AI delay is %1 milliseconds").arg(Config.AIDelay));
    } else
        server_log->append(tr("This server is AI disabled"));
}
