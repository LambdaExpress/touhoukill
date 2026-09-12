#include "button.h"
#include "audio.h"

#include <QGraphicsDropShadowEffect>
#include <QGraphicsRotation>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

static QRectF ButtonRect(0, 0, 189, 46);

Button::Button(const QString &label, qreal scale)
    : label(label)
    , size(ButtonRect.size() * scale)
    , mute(true)
    , font(Config.SmallFont)
{
    init();
}

Button::Button(const QString &label, const QSizeF &size)
    : label(label)
    , size(size)
    , mute(true)
    , font(Config.SmallFont)
{
    init();
}

void Button::init()
{
    setFlags(ItemIsFocusable);

    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);

#ifdef Q_OS_ANDROID
    size.setHeight(qMax<qreal>(56, size.height()));
    size.setWidth(qMax<qreal>(96, size.width()));
    font.setFamily(QStringLiteral("sans-serif"));
    font.setPixelSize(20);
    title = nullptr;
    outimg = nullptr;
    title_item = nullptr;
    glow = 0;
    timer_id = 0;
    return;
#endif
    title = new QPixmap(size.toSize());
    title->fill(QColor(0, 0, 0, 0));
    QPainter pt(title);
    pt.setFont(font);
    pt.setPen(Config.TextEditColor);
    pt.setRenderHint(QPainter::TextAntialiasing);
    pt.drawText(boundingRect(), Qt::AlignCenter, label);

    title_item = new QGraphicsPixmapItem(this);
    title_item->setPixmap(*title);
    title_item->show();

    QGraphicsDropShadowEffect *de = new QGraphicsDropShadowEffect;
    de->setOffset(0);
    de->setBlurRadius(12);
    de->setColor(QColor(255, 165, 0));

    title_item->setGraphicsEffect(de);

    QImage bgimg("image/system/button/button.png");
    outimg = new QImage(size.toSize(), QImage::Format_ARGB32);

    qreal pad = 10;

    int w = bgimg.width();
    int h = bgimg.height();

    int tw = outimg->width();
    int th = outimg->height();

    qreal xc = (w - 2 * pad) / (tw - 2 * pad);
    qreal yc = (h - 2 * pad) / (th - 2 * pad);

    for (int i = 0; i < tw; i++) {
        for (int j = 0; j < th; j++) {
            int x = i;
            int y = j;

            if (x >= pad && x <= (tw - pad))
                x = pad + ((x - pad) * xc);
            else if (x >= (tw - pad))
                x = w - (tw - x);

            if (y >= pad && y <= (th - pad))
                y = pad + ((y - pad) * yc);
            else if (y >= (th - pad))
                y = h - (th - y);

            QRgb rgb = bgimg.pixel(x, y);
            outimg->setPixel(i, j, rgb);
        }
    }

    QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect;
    effect->setBlurRadius(5);
    effect->setOffset(boundingRect().height() / 7.0);
    effect->setColor(QColor(0, 0, 0, 200));
    setGraphicsEffect(effect);

    glow = 0;
    timer_id = 0;
}

void Button::setMute(bool mute)
{
    this->mute = mute;
}

void Button::setFont(const QFont &font)
{
    this->font = font;
#ifdef Q_OS_ANDROID
    this->font.setPixelSize(qMax(18, font.pixelSize()));
    update();
    return;
#endif
    title->fill(QColor(0, 0, 0, 0));
    QPainter pt(title);
    pt.setFont(font);
    pt.setPen(Config.TextEditColor);
    pt.setRenderHint(QPainter::TextAntialiasing);
    pt.drawText(boundingRect(), Qt::AlignCenter, label);

    title_item->setPixmap(*title);
}

#include "engine.h"

void Button::hoverEnterEvent(QGraphicsSceneHoverEvent * /*event*/)
{
    setFocus(Qt::MouseFocusReason);
    if (!mute)
        Sanguosha->playSystemAudioEffect("button-hover");
    if (timer_id == 0)
        timer_id = QObject::startTimer(40);
}

void Button::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    event->accept();
}

void Button::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
#ifdef Q_OS_ANDROID
    if (!boundingRect().contains(event->pos()))
        return;
#else
    Q_UNUSED(event);
#endif
    if (!mute)
        Sanguosha->playSystemAudioEffect("button-down");
    emit clicked();
}

QRectF Button::boundingRect() const
{
    return QRectF(QPointF(), size);
}

void Button::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    QRectF rect = boundingRect();

#ifdef Q_OS_ANDROID
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QColor(isEnabled() ? "#d2b478" : "#546072"));
    painter->setBrush(QColor(isEnabled() ? "#263a4f" : "#202c3a"));
    painter->drawRoundedRect(rect.adjusted(1, 1, -1, -1), 8, 8);
    painter->setPen(QColor(isEnabled() ? "#f1e1bc" : "#8793a3"));
    painter->setFont(font);
    painter->drawText(rect.adjusted(6, 2, -6, -2), Qt::AlignCenter | Qt::TextWordWrap, label);
#else
    painter->drawImage(rect, *outimg);
    painter->fillRect(rect, QColor(255, 255, 255, glow * 10));
#endif
}

void Button::timerEvent(QTimerEvent * /*event*/)
{
    update();
    if (hasFocus()) {
        if (glow < 5)
            glow++;
    } else {
        if (glow > 0)
            glow--;
        else if (timer_id != 0) {
            QObject::killTimer(timer_id);
            timer_id = 0;
        }
    }
}
