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
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProxyStyle>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSlider>
#include <QStyleFactory>
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
// Draws push buttons as lacquered boards banded in metal, which is what the game's
// own artwork looks like; a plain rounded rectangle reads as a generic application.
// A proxy style is what lets every dialog share the look: a stylesheet can draw a
// frame and a gradient, but not the inner band and the corner brackets, and it
// cannot tell a primary action from the rest, because the panel's stylesheet is set
// long before the buttons exist.
//
// The stylesheet must not set a background, border, padding or min-height on buttons
// for any of this to be reached: a rule carrying one of those makes QStyleSheetStyle
// draw the button itself and never ask this style.
class MobileButtonStyle : public QProxyStyle
{
public:
    explicit MobileButtonStyle(QStyle *base)
        : QProxyStyle(base)
    {
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override
    {
        // Spin and combo boxes ask for these primitives on behalf of their sub-controls,
        // which have their own look; only actual buttons are repainted.
        const bool button = qobject_cast<const QAbstractButton *>(widget) != nullptr;
        if (!button || (element != PE_PanelButtonCommand && element != PE_PanelButtonTool)) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        const bool primary = (option->state & State_On) != 0
            || (widget != nullptr && widget->property("sgsPrimaryAction").toBool());
        const bool down = (option->state & State_Sunken) != 0;
        const bool live = (option->state & State_Enabled) != 0;

        const QRectF panel = QRectF(option->rect).adjusted(1.5, 1.5, -1.5, -1.5);
        const qreal radius = 9;

        QColor ring = primary ? QColor(0xf7, 0xdf, 0xa8) : QColor(0xb3, 0x85, 0x3c);
        QColor grain = primary ? QColor(0xff, 0xf0, 0xcd, 150) : QColor(0xb3, 0x85, 0x3c, 130);
        if (!live) {
            ring = QColor(0x6a, 0x5a, 0x4a);
            grain = QColor(0x6a, 0x5a, 0x4a, 110);
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        QLinearGradient field(panel.topLeft(), panel.bottomLeft());
        if (primary) {
            field.setColorAt(0.0, down ? QColor("#b2405f") : QColor("#ea7596"));
            field.setColorAt(0.55, down ? QColor("#8e2b48") : QColor("#c94c6d"));
            field.setColorAt(1.0, down ? QColor("#6f1f36") : QColor("#a42e50"));
        } else {
            field.setColorAt(0.0, down ? QColor("#4d2233") : QColor("#351c28"));
            field.setColorAt(1.0, down ? QColor("#2b1120") : QColor("#1d0f17"));
        }
        painter->setBrush(field);
        painter->setPen(QPen(ring, 2));
        painter->drawRoundedRect(panel, radius, radius);

        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(grain, 1));
        painter->drawRoundedRect(panel.adjusted(4.5, 4.5, -4.5, -4.5), radius - 3.5, radius - 3.5);

        const QColor bracket = live ? (primary ? QColor("#ffe9b8") : QColor("#d3a24a")) : QColor(0x6a, 0x5a, 0x4a);
        painter->setPen(QPen(bracket, 2.4, Qt::SolidLine, Qt::RoundCap));
        const qreal inset = 6;
        const qreal arm = qMin<qreal>(13, qMax<qreal>(7, panel.height() * 0.3));
        for (int corner = 0; corner < 4; ++corner) {
            const qreal x = (corner % 2 == 0) ? panel.left() : panel.right();
            const qreal y = (corner < 2) ? panel.top() : panel.bottom();
            const qreal dx = (corner % 2 == 0) ? 1 : -1;
            const qreal dy = (corner < 2) ? 1 : -1;
            painter->drawLine(QPointF(x + dx * inset, y + dy * (inset + arm)), QPointF(x + dx * inset, y + dy * inset));
            painter->drawLine(QPointF(x + dx * inset, y + dy * inset), QPointF(x + dx * (inset + arm), y + dy * inset));
        }

        // Lacquer gloss: the upper half brightens towards the top edge.
        QPainterPath face;
        face.addRoundedRect(panel, radius, radius);
        painter->setClipPath(face);
        QLinearGradient gloss(panel.topLeft(), QPointF(panel.left(), panel.center().y()));
        gloss.setColorAt(0.0, QColor(255, 255, 255, primary ? 66 : 28));
        gloss.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter->fillRect(QRectF(panel.left(), panel.top(), panel.width(), panel.height() / 2), gloss);

        painter->restore();
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const override
    {
        QSize contents = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_PushButton) {
            contents.setHeight(qMax(contents.height() + 14, 52));
            contents.setWidth(contents.width() + 30);
        }
        return contents;
    }
};

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

#ifdef Q_OS_ANDROID
QStyle *g_button_style = nullptr;
#endif

} // namespace

void DialogSupport::install(QApplication *application)
{
    if (application == nullptr || g_filter != nullptr)
        return;

    g_filter = new DialogScreenFilter(application);
    application->installEventFilter(g_filter);
}

void DialogSupport::installMobileButtonStyle(QApplication *application)
{
#ifdef Q_OS_ANDROID
    if (application == nullptr || g_button_style != nullptr)
        return;

    // The base style is created rather than taken from the application, because
    // setStyle() disposes of the style it replaces and the proxy would then be
    // drawing with a dangling base. Fusion is also what the platform style is built
    // on, so nothing outside the buttons changes appearance.
    g_button_style = new MobileButtonStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    application->setStyle(g_button_style);
#else
    Q_UNUSED(application);
#endif
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
    // The buttons themselves are drawn by MobileButtonStyle, so no rule below may
    // carry a background, a border or padding for a button: any of those would make
    // QStyleSheetStyle draw the button and the style would never be reached.
    widget->setStyleSheet(QStringLiteral(
        "QWidget { color: #f5e9e7; }"
        "QDialog { background: #1b0f16; }"
        "QWidget#mobilePanel { background: rgba(26, 13, 22, 200); border: 1px solid #a8752f; border-radius: 10px; }"
        "QLabel { background: transparent; }"
        "QLabel[sgsHeading=true] { color: #f0cd8b; font-size: 20px; font-weight: bold; }"
        "QPushButton, QToolButton { color: #f5e9e7; }"
        "QPushButton:checked, QPushButton[sgsPrimaryAction=true] { color: #fff7f8; font-weight: bold; }"
        "QPushButton:disabled, QToolButton:disabled { color: #7d6a72; }"
        "QLineEdit, QComboBox, QAbstractSpinBox { min-height: 42px; padding: 2px 9px; border: 1px solid #7b4658; border-radius: 7px; background: #291b26; selection-background-color: #8e3d5c; }"
        "QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border-color: #f0cd8b; }"
        "QLineEdit:read-only { border: none; background: #241722; }"
        "QComboBox::drop-down { width: 38px; border-left: 1px solid #7b4658; }"
        "QComboBox QAbstractItemView { background: #291b26; selection-background-color: #8e3d5c; }"
        "QComboBox QAbstractItemView::item { min-height: 46px; }"
        "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 32px; border: 1px solid #7b4658; background: #3f2a36; }"
        "QCheckBox, QRadioButton { min-height: 46px; spacing: 10px; }"
        "QCheckBox::indicator, QRadioButton::indicator { width: 23px; height: 23px; border: 1px solid #a5707f; background: #261a23; }"
        "QRadioButton::indicator { border-radius: 12px; }"
        "QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: #e6708f; border: 3px solid #f3d69b; }"
        "QGroupBox { border: 1px solid #5c3546; border-radius: 10px; margin-top: 12px; padding: 14px 8px 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 14px; color: #f0cd8b; }"
        "QTabWidget::pane { border: 1px solid #5c3546; background: #22141d; }"
        "QTabBar::tab { min-height: 46px; padding: 0 18px; background: #2f1c28; border-bottom: 3px solid transparent; }"
        "QTabBar::tab:selected { color: #f0cd8b; background: #452a38; border-bottom-color: #e6708f; }"
        "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }"
        "QAbstractItemView { background: #241722; alternate-background-color: #2c1c28; border: 1px solid #5c3546; selection-background-color: #8e3d5c; selection-color: #fff2e2; }"
        "QListView::item { min-height: 48px; padding: 4px; }"
        "QHeaderView::section { background: #3a2431; padding: 8px; border: none; }"
        "QTextEdit { border: 1px solid #6b4053; border-image: none; border-radius: 7px; background: #241722; color: #f5e9e7; padding: 6px; }"
        "QTextEdit[description=true] { background: #f6efe2; color: #2a1e22; border-image: none; padding: 10px; }"
        "QScrollBar:vertical { width: 8px; background: #1e141c; margin: 0; }"
        "QScrollBar::handle:vertical { background: #8a5668; min-height: 36px; border-radius: 4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QSlider:horizontal { min-height: 48px; }"
        "QSlider::groove:horizontal { height: 8px; background: #432b38; border-radius: 4px; }"
        "QSlider::handle:horizontal { background: #f0cd8b; width: 28px; margin: -10px 0; border-radius: 14px; }"
        "QMenu { background: #241722; border: 1px solid #8d4f66; }"
        "QMenu::item { padding: 14px 24px; }"
        "QMenu::item:selected { background: #8e3d5c; }"));
    if (QAbstractScrollArea *area = qobject_cast<QAbstractScrollArea *>(widget))
        attachDragScroll(area);
    foreach (QAbstractScrollArea *area, widget->findChildren<QAbstractScrollArea *>())
        attachDragScroll(area);
#else
    Q_UNUSED(widget);
#endif
}
