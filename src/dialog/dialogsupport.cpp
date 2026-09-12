#include "dialogsupport.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSlider>
#include <QTableView>
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
// Converts a drag distance in viewport pixels into the units this scroll bar counts
// in, which differs by widget. A scroll area scrolls per pixel, so its page step is
// the viewport extent and the ratio is one. An item view such as a table scrolls per
// row by default, and its page step is the number of rows that fit, so the ratio
// turns out to be one row per row height. Feeding pixels straight to the value of a
// table therefore scrolls a row per pixel, which throws the list hundreds of entries
// down the screen on the slightest drag.
int pixelsToScrollUnits(const QScrollBar *bar, int pixels, int viewportExtent)
{
    if (bar->pageStep() <= 0 || viewportExtent <= 0)
        return pixels;
    return qRound(pixels * static_cast<qreal>(bar->pageStep()) / viewportExtent);
}

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
    explicit DragScrollFilter(QAbstractScrollArea *area)
        : QObject(area)
        , m_area(area)
        , m_pressed(false)
        , m_dragging(false)
    {
        area->viewport()->installEventFilter(this);
        foreach (QWidget *child, area->viewport()->findChildren<QWidget *>())
            child->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (m_releasing)
            return false;

        QWidget *target = qobject_cast<QWidget *>(watched);
        if (target == nullptr || qobject_cast<QLineEdit *>(target) != nullptr || qobject_cast<QSlider *>(target) != nullptr)
            return false;
        QWidget *ancestor = target;
        while (ancestor != nullptr && ancestor != m_area->viewport()) {
            if (qobject_cast<QAbstractScrollArea *>(ancestor) != nullptr)
                return false;
            ancestor = ancestor->parentWidget();
        }
        if (ancestor == nullptr)
            return false;

        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_pressed = true;
                m_dragging = false;
                m_target = target;
                m_lastPosition = mouseEvent->globalPos();
            }
            // Not consumed: the widget underneath must still see the press so that
            // a plain tap keeps working.
            return false;
        }
        case QEvent::MouseMove: {
            if (!m_pressed)
                return false;

            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (!m_dragging && (mouseEvent->globalPos() - m_lastPosition).manhattanLength() > QApplication::startDragDistance()) {
                m_dragging = true;
                // Cancel a button's pending click without activating it after a pan.
                if (m_target != nullptr) {
                    m_releasing = true;
                    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(-100, -100), mouseEvent->screenPos(), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(m_target, &release);
                    m_releasing = false;
                }
            }

            if (!m_dragging)
                return false;

            const QPoint step = mouseEvent->globalPos() - m_lastPosition;
            m_lastPosition = mouseEvent->globalPos();

            QScrollBar *vertical = m_area->verticalScrollBar();
            vertical->setValue(vertical->value() - pixelsToScrollUnits(vertical, step.y(), m_area->viewport()->height()));
            if (m_area->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff) {
                QScrollBar *horizontal = m_area->horizontalScrollBar();
                horizontal->setValue(horizontal->value() - pixelsToScrollUnits(horizontal, step.x(), m_area->viewport()->width()));
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
    QAbstractScrollArea *m_area;
    QPoint m_lastPosition;
    bool m_pressed;
    bool m_dragging;
    bool m_releasing = false;
    QPointer<QWidget> m_target;
};

// A scrollable widget that Qt drives like a mouse-driven one neither reacts to a
// finger nor lets the gesture through, so a drag that starts on it is simply lost.
// The overview dialogs show this plainly: dragging beside their table scrolls the
// page, dragging the table itself does nothing. Every scroll area inside a dialog
// therefore gets the filter, the wrapper included. A drag is consumed by the
// innermost area under the finger, so nested areas do not scroll together.
void attachDragScroll(QAbstractScrollArea *area)
{
    // A header has scroll bars it never uses; taking its drags would only interfere
    // with resizing and reordering sections by hand.
    if (qobject_cast<QHeaderView *>(area) != nullptr)
        return;

    // Dialogs are shown repeatedly, and one filter per viewport is enough: two of
    // them would apply every drag twice.
    QWidget *viewport = area->viewport();
    if (viewport->property("sgsDragScroll").toBool())
        return;

    viewport->setProperty("sgsDragScroll", true);
    if (QAbstractItemView *view = qobject_cast<QAbstractItemView *>(area)) {
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
    new DragScrollFilter(area);
}
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
#ifdef Q_OS_ANDROID
        case QEvent::Polish:
            if (!m_busy && !dialog->property("sgsMobileStyled").toBool()) {
                m_busy = true;
                DialogSupport::applyMobileStyle(dialog);
                dialog->setProperty("sgsMobileStyled", true);
                m_busy = false;
            }
            break;
#endif
        case QEvent::Show:
#ifdef Q_OS_ANDROID
            // Configure synchronously, before the first paint can expose native styling.
            adapt(dialog);
            // The mobile layouts rebuild the dialog's layout while it is being shown. Qt
            // leaves the rectangle of an opaque child out of its parent's paint, expecting
            // the child to draw itself, so a child whose first paint is missed keeps
            // whatever the previous frame left there -- the game scene the dialog was
            // opened over -- and nothing repaints it until an input event arrives. Mark the
            // whole dialog dirty, children included, once the event loop has settled.
            QTimer::singleShot(0, dialog, [dialog]() {
                if (!dialog->isVisible())
                    return;
                dialog->update();
                foreach (QWidget *child, dialog->findChildren<QWidget *>())
                    child->update();
            });
#else
            // Deferred: a dialog's own showEvent often calls setFixedSize(), which
            // would immediately undo anything applied here. Running after the event
            // has been fully delivered means those calls already happened.
            QTimer::singleShot(0, dialog, [this, dialog]() { adapt(dialog); });
#endif
            break;
        case QEvent::Resize:
            // A dialog can re-pin its size later (the avatar list toggles between two
            // fixed sizes), so the limit is re-asserted whenever it grows again.
            if (m_wrapped.contains(dialog) || dialog->property("sgsMobileLayout").toBool())
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

#ifdef Q_OS_ANDROID
        if (!dialog->property("sgsMobileStyled").toBool()) {
            DialogSupport::applyMobileStyle(dialog);
            dialog->setProperty("sgsMobileStyled", true);
        }
        foreach (QTableView *table, dialog->findChildren<QTableView *>()) {
            table->verticalHeader()->setMinimumSectionSize(48);
            table->verticalHeader()->setDefaultSectionSize(qMax(48, table->property("sgsRowHeight").toInt()));
        }
        installDragScroll(dialog);

        if (dialog->property("sgsMobileLayout").toBool()) {
            m_busy = true;
            if (dialog->layout() != nullptr)
                dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
            dialog->setMinimumSize(0, 0);
            dialog->setMaximumSize(available);
            dialog->setGeometry(QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft(), available));
            m_busy = false;
            return;
        }

        if (qobject_cast<QMessageBox *>(dialog) == nullptr && !m_wrapped.contains(dialog) && dialog->layout() != nullptr) {
            m_busy = true;
            QLayout *content = dialog->layout();
            QLayoutItem *footer = nullptr;
            if (QVBoxLayout *column = qobject_cast<QVBoxLayout *>(content)) {
                QLayoutItem *last = column->itemAt(column->count() - 1);
                bool actions = last != nullptr && qobject_cast<QDialogButtonBox *>(last->widget()) != nullptr;
                if (last != nullptr && qobject_cast<QHBoxLayout *>(last->layout()) != nullptr) {
                    actions = true;
                    for (int i = 0; i < last->layout()->count(); ++i) {
                        QLayoutItem *item = last->layout()->itemAt(i);
                        if (item->spacerItem() == nullptr && qobject_cast<QAbstractButton *>(item->widget()) == nullptr)
                            actions = false;
                    }
                }
                if (actions)
                    footer = column->takeAt(column->count() - 1);
            }

            QWidget *page = new QWidget;
            page->setLayout(content);
            foreach (QFormLayout *form, page->findChildren<QFormLayout *>())
                form->setRowWrapPolicy(QFormLayout::WrapLongRows);

            QVBoxLayout *outer = new QVBoxLayout(dialog);
            outer->setContentsMargins(12, 8, 12, 8);
            QHBoxLayout *header = new QHBoxLayout;
            QPushButton *back = new QPushButton(QApplication::translate("MobileUI", "Back"));
            connect(back, &QPushButton::clicked, dialog, &QDialog::reject);
            header->addWidget(back);
            QLabel *title = new QLabel(dialog->windowTitle());
            title->setProperty("sgsHeading", true);
            header->addWidget(title, 1);
            outer->addLayout(header);
            outer->addWidget(DialogSupport::createScrollArea(page), 1);
            if (footer != nullptr)
                outer->addItem(footer);

            dialog->setProperty("sgsMobileLayout", true);
            m_busy = false;
            adapt(dialog);
            return;
        }
#endif

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
            attachDragScroll(area);
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

#ifdef Q_OS_ANDROID
    void installDragScroll(QDialog *dialog)
    {
        foreach (QAbstractScrollArea *area, dialog->findChildren<QAbstractScrollArea *>())
            attachDragScroll(area);
    }
#endif

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

QScrollArea *DialogSupport::createScrollArea(QWidget *content)
{
    QScrollArea *area = new QScrollArea;
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setMinimumSize(0, 0);
    content->setMinimumWidth(0);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    area->setWidget(content);
    return area;
}

void DialogSupport::applyMobileStyle(QWidget *widget)
{
#ifdef Q_OS_ANDROID
    QFont font(QStringLiteral("sans-serif"));
    font.setPixelSize(16);
    widget->setFont(font);
    widget->setStyleSheet(QStringLiteral(
        "QWidget { color: #e8edf2; }"
        "QDialog, QWidget#mobilePanel { background: #111c28; }"
        "QLabel { background: transparent; }"
        "QLabel[sgsHeading=true] { color: #edce8c; font-size: 20px; font-weight: bold; }"
        "QPushButton, QToolButton { min-height: 46px; padding: 0 14px; border: 1px solid #4b6173; border-radius: 7px; background: #223448; }"
        "QPushButton:pressed, QToolButton:pressed { background: #476079; }"
        "QPushButton:checked, QPushButton[sgsPrimaryAction=true] { background: #d2b478; color: #152331; border-color: #ecd49d; font-weight: bold; }"
        "QPushButton:disabled, QToolButton:disabled { color: #77828c; background: #1c2835; border-color: #30404e; }"
        "QLineEdit, QComboBox, QAbstractSpinBox { min-height: 42px; padding: 2px 9px; border: 1px solid #536475; border-radius: 6px; background: #1b2b3b; selection-background-color: #49677c; }"
        "QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border-color: #e6c88b; }"
        "QLineEdit:read-only { border: none; background: #192837; }"
        "QComboBox::drop-down { width: 38px; border-left: 1px solid #536475; }"
        "QComboBox QAbstractItemView { background: #1b2b3b; selection-background-color: #49677c; }"
        "QComboBox QAbstractItemView::item { min-height: 46px; }"
        "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 32px; border: 1px solid #536475; background: #30475c; }"
        "QCheckBox, QRadioButton { min-height: 46px; spacing: 10px; }"
        "QCheckBox::indicator, QRadioButton::indicator { width: 23px; height: 23px; border: 1px solid #81909d; background: #192b3c; }"
        "QRadioButton::indicator { border-radius: 12px; }"
        "QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: #d2b478; border: 3px solid #f0d59f; }"
        "QGroupBox { border: 1px solid #3c5062; border-radius: 8px; margin-top: 12px; padding: 14px 8px 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 14px; color: #edce8c; }"
        "QTabWidget::pane { border: 1px solid #3c5062; background: #142231; }"
        "QTabBar::tab { min-height: 46px; padding: 0 18px; background: #223448; border-bottom: 3px solid transparent; }"
        "QTabBar::tab:selected { color: #edce8c; background: #30465b; border-bottom-color: #edce8c; }"
        "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }"
        "QAbstractItemView { background: #18293a; alternate-background-color: #203246; border: 1px solid #3c5062; selection-background-color: #4a657c; selection-color: #fff1d0; }"
        "QListView::item { min-height: 48px; padding: 4px; }"
        "QHeaderView::section { background: #263c50; padding: 8px; border: none; }"
        "QTextEdit { border: 1px solid #45596b; border-image: none; border-radius: 6px; background: #172838; color: #e8edf2; padding: 6px; }"
        "QTextEdit[description=true] { background: #f5f1e5; color: #202934; border-image: none; padding: 10px; }"
        "QScrollBar:vertical { width: 8px; background: #152433; margin: 0; }"
        "QScrollBar::handle:vertical { background: #718797; min-height: 36px; border-radius: 4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QSlider:horizontal { min-height: 48px; }"
        "QSlider::groove:horizontal { height: 8px; background: #354b60; border-radius: 4px; }"
        "QSlider::handle:horizontal { background: #edce8c; width: 28px; margin: -10px 0; border-radius: 14px; }"
        "QMenu { background: #18293a; border: 1px solid #607689; }"
        "QMenu::item { padding: 14px 24px; }"
        "QMenu::item:selected { background: #4a657c; }"));
    if (QAbstractScrollArea *area = qobject_cast<QAbstractScrollArea *>(widget))
        attachDragScroll(area);
    foreach (QAbstractScrollArea *area, widget->findChildren<QAbstractScrollArea *>())
        attachDragScroll(area);
#else
    Q_UNUSED(widget);
#endif
}
