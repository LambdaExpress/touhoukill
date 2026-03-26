#ifndef _SPECTATE_VIEW_MODEL_H
#define _SPECTATE_VIEW_MODEL_H

#include <QObject>

class SpectateRoomState;

class SpectateViewModel : public QObject
{
    Q_OBJECT

public:
    explicit SpectateViewModel(SpectateRoomState *state, QObject *parent = nullptr);

    SpectateRoomState *state() const { return m_state; }
    int roomId() const;
    QString targetName() const;

private:
    SpectateRoomState *m_state;
};

#endif
