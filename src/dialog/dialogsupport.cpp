#include "dialogsupport.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QLayout>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QSize availableScreenSize()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr)
        return QSize();

    return screen->availableGeometry().size();
}

#ifdef Q_OS_ANDROID
// Drag-to-scroll for the wrapped pages.
//
// QScroller was tried first and is not usable here: its gesture recogniser
// consumes the press/release pair that a tap needs, so tapping a general in the
// choice dialog stopped selecting anything. This filter leaves short taps
// completely untouched and only takes over once the finger has moved past the
// drag threshold, which is the behaviour a touch screen expects.
class DragScrollFilter : public QObject
{
public:
    explicit DragScrollFilter(QScrollArea *area)
        : QObject(area)
        , m_area(area)
        , m_pressed(false)
        , m_dragging(false)
    {
        area->viewport()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_area->viewport())
            return false;

        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_pressed = true;
                m_dragging = false;
                m_lastPosition = mouseEvent->pos();
            }
            // Not consumed: the widget underneath must still see the press so that
            // a plain tap keeps working.
            return false;
        }
        case QEvent::MouseMove: {
            if (!m_pressed)
                return false;

            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (!m_dragging && (mouseEvent->pos() - m_lastPosition).manhattanLength() > QApplication::startDragDistance())
                m_dragging = true;

            if (!m_dragging)
                return false;

            const QPoint step = mouseEvent->pos() - m_lastPosition;
            m_lastPosition = mouseEvent->pos();

            QScrollBar *vertical = m_area->verticalScrollBar();
            vertical->setValue(vertical->value() - step.y());
            if (m_area->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff) {
                QScrollBar *horizontal = m_area->horizontalScrollBar();
                horizontal->setValue(horizontal->value() - step.x());
            }

            return true; // consumed, otherwise the content would also pan itself
        }
        case QEvent::MouseButtonRelease: {
            const bool wasDragging = m_dragging;
            m_pressed = false;
            m_dragging = false;
            // Swallow only the release that ends a drag; after a drag the widget
            // underneath must not also treat the gesture as a click on it.
            return wasDragging;
        }
        default:
            return false;
        }
    }

private:
    QScrollArea *m_area;
    QPoint m_lastPosition;
    bool m_pressed;
    bool m_dragging;
};
#endif // Q_OS_ANDROID

class DialogScreenFilter : public QObject
{
public:
    explicit DialogScreenFilter(QObject *parent = nullptr)
        : QObject(parent)
        , m_busy(false)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        QDialog *dialog = qobject_cast<QDialog *>(watched);
        if (dialog == nullptr)
            return QObject::eventFilter(watched, event);

        switch (event->type()) {
        case QEvent::Show:
            // Deferred: a dialog's own showEvent often calls setFixedSize(), which
            // would immediately undo anything applied here. Running after the event
            // has been fully delivered means those calls already happened.
            QTimer::singleShot(0, dialog, [this, dialog]() { adapt(dialog); });
            break;
        case QEvent::Resize:
            // A dialog can re-pin its size later (the avatar list toggles between two
            // fixed sizes), so the limit is re-asserted whenever it grows again.
            if (m_wrapped.contains(dialog))
                clamp(dialog);
            break;
        default:
            break;
        }

        return QObject::eventFilter(watched, event);
    }

private:
    // Wraps the dialog content in a scroll area the first time it proves too large,
    // then always keeps the dialog within the screen.
    void adapt(QDialog *dialog)
    {
        if (m_busy)
            return;

        const QSize available = availableScreenSize();
        if (available.isEmpty())
            return;

        if (!m_wrapped.contains(dialog)) {
            const QSize wanted = dialog->sizeHint();
            if (wanted.width() <= available.width() && wanted.height() <= available.height()) {
                // Fits as-is: leave the dialog exactly as the desktop build behaves.
                return;
            }

            QLayout *content = dialog->layout();
            if (content == nullptr)
                return;

            m_busy = true;

            // The existing layout is moved onto a page inside the scroll area; Qt
            // reparents the layout and every widget it manages along with it.
            QWidget *page = new QWidget;
            page->setLayout(content);

            QScrollArea *area = new QScrollArea(dialog);
            area->setWidget(page);
            area->setWidgetResizable(true);
            area->setFrameShape(QFrame::NoFrame);
            // The page is exactly as wide as the dialog wants to be, so a horizontal
            // bar can only ever be an artefact of the vertical bar taking a slice off
            // the viewport. When the content does fit the screen the dialog is grown by
            // that slice instead and the horizontal bar is switched off; content that
            // genuinely is too wide keeps its bar.
            const bool contentFitsHorizontally = wanted.width() <= available.width();
            if (contentFitsHorizontally) {
                area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                const int barWidth = area->verticalScrollBar()->sizeHint().width();
                m_grownWidth.insert(dialog, qMin(available.width(), wanted.width() + barWidth));
            } else {
                area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            }

            // A scroll bar is a few millimetres wide on a phone, so dragging the
            // content itself has to work. Qt synthesises mouse events from touch,
            // which is what the filter consumes.
#ifdef Q_OS_ANDROID
            new DragScrollFilter(area);
#endif

            QVBoxLayout *outer = new QVBoxLayout(dialog);
            outer->setContentsMargins(0, 0, 0, 0);
            outer->addWidget(area);

            m_wrapped.insert(dialog);
            // Dialogs are usually stack allocated and recreated on every use, so a
            // stale entry would later match an unrelated dialog that happens to
            // reuse the address.
            connect(dialog, &QObject::destroyed, this, [this](QObject *object) {
                QDialog *dialog = static_cast<QDialog *>(object);
                m_wrapped.remove(dialog);
                m_grownWidth.remove(dialog);
            });
            m_busy = false;
        }

        clamp(dialog);

        // Applied after the clamp, which clears the size constraints and would
        // otherwise shrink the dialog straight back to its content width.
        const int grownWidth = m_grownWidth.value(dialog, 0);
        if (grownWidth > dialog->width() && dialog->height() > 0)
            dialog->resize(grownWidth, dialog->height());
    }

    void clamp(QDialog *dialog)
    {
        if (m_busy)
            return;

        const QSize available = availableScreenSize();
        if (available.isEmpty())
            return;

        m_busy = true;
        // Minimum size is cleared first: a dialog whose content asks for more room
        // than the screen cannot be shrunk otherwise, and on a phone it has to be.
        dialog->setMinimumSize(0, 0);
        dialog->setMaximumSize(available.width(), available.height());
        dialog->resize(qMin(dialog->size().width(), available.width()),
                       qMin(dialog->size().height(), available.height()));
        m_busy = false;
    }

    QSet<QDialog *> m_wrapped;
    QHash<QDialog *, int> m_grownWidth;
    bool m_busy;
};

DialogScreenFilter *g_filter = nullptr;

} // namespace

void DialogSupport::install(QApplication *application)
{
    if (application == nullptr || g_filter != nullptr)
        return;

    g_filter = new DialogScreenFilter(application);
    application->installEventFilter(g_filter);
}
