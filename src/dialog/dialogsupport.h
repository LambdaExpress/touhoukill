#ifndef DIALOGSUPPORT_H
#define DIALOGSUPPORT_H

class QApplication;
class QScrollArea;
class QWidget;

// Mobile pages own their navigation and action rows. Only their content scrolls.
// Other dialogs receive a scrollable body and a fixed dismissal/action area.
namespace DialogSupport {

// Installs the filter. Must be called after the QApplication exists.
void install(QApplication *application);

QScrollArea *createScrollArea(QWidget *content);
void applyMobileStyle(QWidget *widget);

} // namespace DialogSupport

#endif // DIALOGSUPPORT_H
