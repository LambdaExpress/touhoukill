#include "spectatehub.h"

#include "json.h"
#include "room.h"
#include "server.h"
#include "serverplayer.h"

#include <QUuid>

using namespace QSanProtocol;

SpectateHub::SpectateHub(Server *server)
    : QObject(server)
    , m_server(server)
{
}

void SpectateHub::handleCommand(ServerPlayer *player, CommandType command, const QVariant &arg)
{
    if (player == nullptr)
        return;

    switch (command) {
    case S_COMMAND_SPECTATE_LIST_REQUEST:
        handleListRequest(player);
        break;
    case S_COMMAND_SPECTATE_START:
        handleStartRequest(player, arg);
        break;
    case S_COMMAND_SPECTATE_STOP:
        handleStopRequest(player);
        break;
    case S_COMMAND_SPECTATE_SWITCH_TARGET:
        handleSwitchTargetRequest(player, arg);
        break;
    case S_COMMAND_SPECTATE_RESYNC_REQUEST:
        handleResyncRequest(player, arg);
        break;
    default:
        break;
    }
}

void SpectateHub::stopSessionsBySourceRoom(int sourceRoomId, const QString &reason)
{
    QList<QString> viewersToRemove;
    for (auto it = m_sessionsByViewer.constBegin(); it != m_sessionsByViewer.constEnd(); ++it) {
        if (it.value().sourceRoomId == sourceRoomId)
            viewersToRemove << it.key();
    }

    foreach (const QString &viewerName, viewersToRemove)
        removeSession(viewerName, reason);
}

void SpectateHub::onProjectionAdvanced(int roomId, int eventSeq)
{
    Q_UNUSED(eventSeq);

    Room *targetRoom = m_server->findRoomById(roomId);
    if (targetRoom == nullptr)
        return;

    QList<QString> viewers = m_viewersByTargetRoom.values(roomId);
    foreach (const QString &viewerName, viewers) {
        auto it = m_sessionsByViewer.find(viewerName);
        if (it == m_sessionsByViewer.end())
            continue;

        Session &session = it.value();
        if (!targetRoom->isSpectateAlive(session.targetObjectName)) {
            QString newTarget = targetRoom->nextSpectateAliveTarget(session.targetObjectName);
            if (newTarget.isEmpty()) {
                removeSession(viewerName, QStringLiteral("ALL_TARGETS_DEAD"));
                continue;
            }
            session.targetObjectName = newTarget;
            sendTargetSwitched(session, targetRoom);
            continue;
        }

        sendEvents(session, targetRoom);
    }
}

void SpectateHub::onRoomTeardown(int roomId)
{
    QList<QString> viewers = m_viewersByTargetRoom.values(roomId);
    foreach (const QString &viewerName, viewers)
        removeSession(viewerName, QStringLiteral("TARGET_ROOM_OVER"));
}

void SpectateHub::handleListRequest(ServerPlayer *player)
{
    Room *viewerRoom = player->getRoom();
    if (viewerRoom == nullptr || viewerRoom->hasStarted())
        return;

    sendPacket(player, S_COMMAND_SPECTATE_LIST, buildRoomListPayload());
}

void SpectateHub::handleStartRequest(ServerPlayer *player, const QVariant &arg)
{
    JsonArray body = arg.value<JsonArray>();
    if (body.size() < 2)
        return;

    int targetRoomId = body.at(0).toInt();
    QString targetObjectName = body.at(1).toString();
    if (targetObjectName.isEmpty())
        return;

    Room *viewerRoom = player->getRoom();
    if (viewerRoom == nullptr || viewerRoom->hasStarted())
        return;
    if (m_sessionsByViewer.contains(player->objectName()))
        return;

    Room *targetRoom = m_server->findRoomById(targetRoomId);
    if (targetRoom == nullptr || !targetRoom->hasStarted() || targetRoom->isFinished())
        return;
    if (!targetRoom->isSpectateAlive(targetObjectName))
        return;

    Session session;
    session.sessionId = QUuid::createUuid().toString();
    session.viewer = player;
    session.viewerObjectName = player->objectName();
    session.sourceRoomId = viewerRoom->getId();
    session.targetRoomId = targetRoomId;
    session.targetObjectName = targetObjectName;
    session.lastSentSeq = targetRoom->lastSpectateEventSeq();

    m_sessionsByViewer.insert(session.viewerObjectName, session);
    m_viewersByTargetRoom.insert(session.targetRoomId, session.viewerObjectName);
    sendStarted(m_sessionsByViewer[session.viewerObjectName], targetRoom);
}

void SpectateHub::handleStopRequest(ServerPlayer *player, const QString &reason)
{
    if (player == nullptr)
        return;
    removeSession(player->objectName(), reason);
}

void SpectateHub::handleSwitchTargetRequest(ServerPlayer *player, const QVariant &arg)
{
    if (player == nullptr)
        return;

    auto it = m_sessionsByViewer.find(player->objectName());
    if (it == m_sessionsByViewer.end())
        return;

    QString targetName = arg.toString().trimmed();
    if (targetName.isEmpty())
        return;

    Room *targetRoom = m_server->findRoomById(it->targetRoomId);
    if (targetRoom == nullptr || !targetRoom->isSpectateAlive(targetName))
        return;

    it->targetObjectName = targetName;
    sendTargetSwitched(it.value(), targetRoom);
}

void SpectateHub::handleResyncRequest(ServerPlayer *player, const QVariant &arg)
{
    Q_UNUSED(arg);
    if (player == nullptr)
        return;

    auto it = m_sessionsByViewer.find(player->objectName());
    if (it == m_sessionsByViewer.end())
        return;

    Room *targetRoom = m_server->findRoomById(it->targetRoomId);
    if (targetRoom == nullptr)
        return;

    sendStarted(it.value(), targetRoom);
}

void SpectateHub::sendPacket(ServerPlayer *viewer, CommandType command, const QVariant &payload)
{
    if (viewer == nullptr)
        return;

    Packet packet(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT, command);
    packet.setMessageBody(payload);
    viewer->invoke(&packet);
}

void SpectateHub::sendStarted(Session &session, Room *targetRoom)
{
    if (targetRoom == nullptr)
        return;

    JsonObject payload;
    payload["sessionId"] = session.sessionId;
    payload["roomId"] = session.targetRoomId;
    payload["targetName"] = session.targetObjectName;
    payload["snapshotVersion"] = targetRoom->currentSpectateSnapshotVersion();
    payload["lastEventSeq"] = targetRoom->lastSpectateEventSeq();
    payload["snapshot"] = targetRoom->buildSpectateSnapshot(session.targetObjectName);
    session.lastSentSeq = payload["lastEventSeq"].toInt();
    sendPacket(session.viewer.data(), S_COMMAND_SPECTATE_STARTED, QVariant(payload));
}

void SpectateHub::sendEvents(Session &session, Room *targetRoom)
{
    if (targetRoom == nullptr)
        return;

    bool overflow = false;
    QVariantList events = targetRoom->spectateEventsAfter(session.lastSentSeq, session.targetObjectName, &overflow);
    if (overflow) {
        sendStarted(session, targetRoom);
        return;
    }
    if (events.isEmpty())
        return;

    QVariantMap firstEvent = events.first().toMap();
    QVariantMap lastEvent = events.last().toMap();

    JsonObject payload;
    payload["sessionId"] = session.sessionId;
    payload["fromSeq"] = firstEvent.value("seq").toInt();
    payload["toSeq"] = lastEvent.value("seq").toInt();
    payload["events"] = events;
    session.lastSentSeq = payload["toSeq"].toInt();
    sendPacket(session.viewer.data(), S_COMMAND_SPECTATE_EVENTS, QVariant(payload));
}

void SpectateHub::sendTargetSwitched(Session &session, Room *targetRoom)
{
    if (targetRoom == nullptr)
        return;

    JsonObject payload;
    payload["sessionId"] = session.sessionId;
    payload["targetName"] = session.targetObjectName;
    payload["snapshotVersion"] = targetRoom->currentSpectateSnapshotVersion();
    payload["lastEventSeq"] = targetRoom->lastSpectateEventSeq();
    payload["privateState"] = targetRoom->buildSpectatePrivateState(session.targetObjectName);
    session.lastSentSeq = payload["lastEventSeq"].toInt();
    sendPacket(session.viewer.data(), S_COMMAND_SPECTATE_TARGET_SWITCHED, QVariant(payload));
}

void SpectateHub::removeSession(const QString &viewerName, const QString &reason)
{
    auto it = m_sessionsByViewer.find(viewerName);
    if (it == m_sessionsByViewer.end())
        return;

    Session session = it.value();
    m_sessionsByViewer.erase(it);
    m_viewersByTargetRoom.remove(session.targetRoomId, viewerName);

    ServerPlayer *viewer = session.viewer.data();
    if (viewer != nullptr) {
        JsonObject payload;
        payload["sessionId"] = session.sessionId;
        payload["reason"] = reason;
        payload["lastKnownSeq"] = session.lastSentSeq;
        sendPacket(viewer, S_COMMAND_SPECTATE_ENDED, QVariant(payload));
    }
}

QVariantList SpectateHub::buildRoomListPayload() const
{
    QVariantList result;
    foreach (Room *room, m_server->getRooms()) {
        if (room == nullptr || !room->hasStarted() || room->isFinished())
            continue;
        result << QVariant(room->buildSpectateRoomEntry());
    }
    return result;
}
