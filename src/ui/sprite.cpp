#include "sprite.h"

#include <QAnimationGroup>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QtMath>

namespace {
// Fraction of the source size that EmphasizeEffect::boundingRectFor() adds as padding on
// each side. draw() has to undo exactly this to recover the source box.
const qreal S_EMPHASIZE_PADDING = 0.1;
} // namespace

EffectAnimation::EffectAnimation(QObject *parent)
    : QObject(parent)
{
    effects.clear();
    registered.clear();
}

void EffectAnimation::fade(QGraphicsItem *map)
{
    QAnimatedEffect *effect = qobject_cast<QAnimatedEffect *>(map->graphicsEffect());
    if (effect != nullptr) {
        effectOut(map);
        effect = registered.value(map);
        if (effect != nullptr)
            effect->deleteLater();
        registered.insert(map, new FadeEffect(true));
        return;
    }

    map->show();
    FadeEffect *fade = new FadeEffect(true);
    map->setGraphicsEffect(fade);
    effects.insert(map, fade);
}

void EffectAnimation::emphasize(QGraphicsItem *map, bool stay)
{
    QAnimatedEffect *effect = qobject_cast<QAnimatedEffect *>(map->graphicsEffect());
    if (effect != nullptr) {
        effectOut(map);
        effect = registered.value(map);
        if (effect != nullptr)
            effect->deleteLater();
        registered.insert(map, new EmphasizeEffect(stay));
        return;
    }

    EmphasizeEffect *emphasize = new EmphasizeEffect(stay);
    map->setGraphicsEffect(emphasize);
    effects.insert(map, emphasize);
}

void EffectAnimation::sendBack(QGraphicsItem *map)
{
    QAnimatedEffect *effect = qobject_cast<QAnimatedEffect *>(map->graphicsEffect());
    if (effect != nullptr) {
        effectOut(map);
        effect = registered.value(map);
        if (effect != nullptr)
            effect->deleteLater();
        registered.insert(map, new SentbackEffect(true));
        return;
    }

    SentbackEffect *sendBack = new SentbackEffect(true);
    map->setGraphicsEffect(sendBack);
    effects.insert(map, sendBack);
}

void EffectAnimation::effectOut(QGraphicsItem *map)
{
    QAnimatedEffect *effect = qobject_cast<QAnimatedEffect *>(map->graphicsEffect());
    if (effect != nullptr) {
        effect->setStay(false);
        connect(effect, SIGNAL(loop_finished()), this, SLOT(deleteEffect()));
    }

    effect = registered.value(map);
    if (effect != nullptr)
        effect->deleteLater();
    registered.insert(map, nullptr);
}

void EffectAnimation::deleteEffect()
{
    QAnimatedEffect *effect = qobject_cast<QAnimatedEffect *>(sender());
    deleteEffect(effect);
}

void EffectAnimation::deleteEffect(QAnimatedEffect *effect)
{
    if (effect == nullptr)
        return;
    effect->deleteLater();
    QGraphicsItem *pix = effects.key(effect);
    if (pix != nullptr) {
        QAnimatedEffect *effect = registered.value(pix);
        if (effect != nullptr)
            effect->reset();
        pix->setGraphicsEffect(registered.value(pix));
        effects.insert(pix, registered.value(pix));
        registered.insert(pix, nullptr);
    }
}

EmphasizeEffect::EmphasizeEffect(bool stay)
{
    setObjectName("emphasizer");
    index = 0;
    this->stay = stay;
    QPropertyAnimation *anim = new QPropertyAnimation(this, "index");
    connect(anim, SIGNAL(valueChanged(QVariant)), this, SLOT(update()));
    anim->setEndValue(40);
    anim->setDuration((40 - index) * 5);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void EmphasizeEffect::draw(QPainter *painter)
{
    QPoint offset;
    QPixmap pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset);
    if (pixmap.isNull())
        return;

    // The target is measured in the logical coordinates the painter draws in, which are
    // the same ones boundingRect() is expressed in.
    const QSizeF source = sourceBoundingRect().size();
    qreal scale = (-qAbs(index - 50) + 50) / 1000.0;
    scale = 0.1 - scale;

    const QRectF target = boundingRect().adjusted((source.width() * scale) - 1, source.height() * scale, -source.width() * scale, -source.height() * scale);
    // An explicit source rectangle is in the pixmap's own pixels, not in the logical
    // units used above: the pixmap holds the source grown by the padding on each side
    // and then by the device pixel ratio. Reading the source size straight out of
    // sourceBoundingRect() therefore cut a rectangle a few times too small out of the
    // top left corner and stretched it over the whole target, so an emphasised card
    // showed nothing but its number and suit. Only a device pixel ratio of 1 makes the
    // two spaces agree, which is why this never showed on the desktop.
    const qreal dpr = pixmap.devicePixelRatio();
    const QRectF sourceRect(source.width() * S_EMPHASIZE_PADDING * dpr, source.height() * S_EMPHASIZE_PADDING * dpr, source.width() * dpr, source.height() * dpr);

    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawPixmap(target, pixmap, sourceRect);
}

QRectF EmphasizeEffect::boundingRectFor(const QRectF &sourceRect) const
{
    QRectF rect(sourceRect);
    rect.adjust(-sourceRect.width() * S_EMPHASIZE_PADDING, -sourceRect.height() * S_EMPHASIZE_PADDING, sourceRect.width() * S_EMPHASIZE_PADDING, sourceRect.height() * S_EMPHASIZE_PADDING);
    return rect;
}

void QAnimatedEffect::setStay(bool stay)
{
    this->stay = stay;
    if (!stay) {
        QPropertyAnimation *anim = new QPropertyAnimation(this, "index");
        anim->setEndValue(0);
        anim->setDuration(index * 5);

        connect(anim, SIGNAL(finished()), this, SLOT(deleteLater()));
        connect(anim, SIGNAL(valueChanged(QVariant)), this, SLOT(update()));
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

SentbackEffect::SentbackEffect(bool stay)
{
    grayed = nullptr;
    setObjectName("backsender");
    index = 0;
    this->stay = stay;

    QPropertyAnimation *anim = new QPropertyAnimation(this, "index");
    connect(anim, SIGNAL(valueChanged(QVariant)), this, SLOT(update()));
    anim->setEndValue(40);
    anim->setDuration((40 - index) * 5);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

QRectF SentbackEffect::boundingRectFor(const QRectF &sourceRect) const
{
    qreal scale = 0.05;
    QRectF rect(sourceRect);
    rect.adjust(-sourceRect.width() * scale, -sourceRect.height() * scale, sourceRect.width() * scale, sourceRect.height() * scale);
    return rect;
}

void SentbackEffect::draw(QPainter *painter)
{
    QPoint offset;
    QPixmap pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset);

    if (grayed == nullptr) {
        grayed = new QImage(pixmap.size(), QImage::Format_ARGB32);

        QImage image = pixmap.toImage();
        int width = image.width();
        int height = image.height();
        int gray = 0;

        QRgb col = 0;

        for (int i = 0; i < width; ++i) {
            for (int j = 0; j < height; ++j) {
                col = image.pixel(i, j);
                gray = qGray(col) >> 1;
                grayed->setPixel(i, j, qRgba(gray, gray, gray, qAlpha(col)));
            }
        }
    }

    painter->drawPixmap(offset, pixmap);
    painter->setOpacity((40 - qAbs(index - 40)) / 80.0);
    painter->drawImage(offset, *grayed);

    return;
}

FadeEffect::FadeEffect(bool stay)
{
    setObjectName("fader");
    index = 0;
    this->stay = stay;

    QPropertyAnimation *anim = new QPropertyAnimation(this, "index");
    connect(anim, SIGNAL(valueChanged(QVariant)), this, SLOT(update()));
    anim->setEndValue(40);
    anim->setDuration((40 - index) * 5);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void FadeEffect::draw(QPainter *painter)
{
    QPoint offset;
    QPixmap pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset);
    painter->setOpacity(index / 40.0);
    painter->drawPixmap(offset, pixmap);
}
