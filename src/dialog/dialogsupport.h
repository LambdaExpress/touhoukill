#ifndef DIALOGSUPPORT_H
#define DIALOGSUPPORT_H

class QApplication;

// Adapts the desktop-sized dialogs to phone screens, from one place.
//
// The application was laid out for a desktop window of roughly 1360x700 logical
// pixels. A phone in landscape offers far less than that, so dialogs that size
// themselves to their content end up taller than the display; their bottom row,
// which normally holds the confirm button, becomes unreachable. Modal dialogs
// cannot be scrolled or dismissed in that state, so the game cannot be played.
//
// Rather than adjusting each of the thirty dialog classes (and missing future
// ones), a single application event filter watches every QDialog that is shown.
// Dialogs that fit are left completely untouched, so desktop behaviour is
// unchanged. Dialogs that do not fit are given a scroll area around their content
// and clamped to the screen.
namespace DialogSupport {

// Installs the filter. Must be called after the QApplication exists.
void install(QApplication *application);

} // namespace DialogSupport

#endif // DIALOGSUPPORT_H
