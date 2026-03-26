#ifndef _SPECTATE_SCENE_H
#define _SPECTATE_SCENE_H

#include "roomscene.h"

class Button;
class SpectateViewModel;

class SpectateScene : public RoomScene
{
    Q_OBJECT

public:
    explicit SpectateScene(QMainWindow *mainWindow, SpectateViewModel *viewModel);
    ~SpectateScene() override;
    void adjustItems() override;

protected:
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void stopSpectating();

private:
    void initializeFromState();

    SpectateViewModel *m_viewModel;
    Button *m_stopButton;
    bool m_needsInitialLayout;
};

#endif
