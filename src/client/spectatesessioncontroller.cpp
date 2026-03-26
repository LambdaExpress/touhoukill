#include "spectatesessioncontroller.h"

#include "json.h"
#include "spectateroomstate.h"

using namespace QSanProtocol;

SpectateSessionController::SpectateSessionController(Client *transport, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_state(new SpectateRoomState(this))
{
}

bool SpectateSessionController::isActive() const
{
    return m_state != nullptr && m_state->isActive();
}

void SpectateSessionController::handleStarted(const QVariant &arg)
{
    if (m_state == nullptr)
        return;
    m_state->applyStartedPayload(arg);
    if (m_state->hasIncompleteTargetHand()) {
        JsonObject resyncPayload;
        resyncPayload["sessionId"] = m_state->sessionId();
        resyncPayload["lastAppliedSeq"] = m_state->lastEventSeq();
        m_transport->notifyServer(S_COMMAND_SPECTATE_RESYNC_REQUEST, QVariant(resyncPayload));
    }
    emit started(arg.toMap());
}

void SpectateSessionController::handleEvents(const QVariant &arg)
{
    if (m_state == nullptr || !m_state->isActive())
        return;

    if (!m_state->applyEventBatch(arg) || m_state->hasIncompleteTargetHand()) {
        JsonObject resyncPayload;
        resyncPayload["sessionId"] = m_state->sessionId();
        resyncPayload["lastAppliedSeq"] = m_state->lastEventSeq();
        m_transport->notifyServer(S_COMMAND_SPECTATE_RESYNC_REQUEST, QVariant(resyncPayload));
    }
}

void SpectateSessionController::handleEnded(const QVariant &arg)
{
    QVariantMap payload = arg.toMap();
    QString reason = payload.value("reason").toString();
    if (m_state != nullptr)
        m_state->applyEndedPayload(arg);
    emit ended(reason);
}

void SpectateSessionController::handleTargetSwitched(const QVariant &arg)
{
    if (m_state == nullptr || !m_state->isActive())
        return;
    m_state->applyTargetSwitchedPayload(arg);
    if (m_state->hasIncompleteTargetHand()) {
        JsonObject resyncPayload;
        resyncPayload["sessionId"] = m_state->sessionId();
        resyncPayload["lastAppliedSeq"] = m_state->lastEventSeq();
        m_transport->notifyServer(S_COMMAND_SPECTATE_RESYNC_REQUEST, QVariant(resyncPayload));
    }
}

void SpectateSessionController::requestStop()
{
    if (!isActive())
        return;

    m_transport->notifyServer(S_COMMAND_SPECTATE_STOP, QVariant());

    JsonObject payload;
    payload["sessionId"] = m_state->sessionId();
    payload["reason"] = QStringLiteral("VIEWER_REQUESTED");
    handleEnded(QVariant(payload));
}
