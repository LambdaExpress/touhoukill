#include "spectateviewmodel.h"

#include "spectateroomstate.h"

SpectateViewModel::SpectateViewModel(SpectateRoomState *state, QObject *parent)
    : QObject(parent)
    , m_state(state)
{
}

int SpectateViewModel::roomId() const
{
    return m_state != nullptr ? m_state->roomId() : 0;
}

QString SpectateViewModel::targetName() const
{
    return m_state != nullptr ? m_state->currentTargetName() : QString();
}
