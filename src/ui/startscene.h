#ifndef _START_SCENE_H
#define _START_SCENE_H

#include "QSanSelectableItem.h"
#include "button.h"
#include "server.h"

#include <QAction>
#include <QGraphicsScene>
#include <QTextEdit>

class QGraphicsSimpleTextItem;

class StartScene : public QGraphicsScene
{
    Q_OBJECT

public:
    StartScene();
    void addButton(QAction *action);
    void setServerLogBackground();
    void switchToServer(Server *server);
    // Places the logo, the button block and the website line inside the current scene
    // rect. The scene rect is not known when the scene is constructed, so this has to
    // run again whenever the view is resized.
    void adjustItems();

private:
    void printServerInfo();

    QSanSelectableItem *logo;
    QTextEdit *server_log;
    QGraphicsSimpleTextItem *website_text;
    QList<Button *> buttons;
};

#endif
