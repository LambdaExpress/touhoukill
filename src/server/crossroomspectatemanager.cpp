#include "crossroomspectatemanager.h"
#include "json.h"
#include "room.h"
#include "server.h"
#include "serverplayer.h"

#include <QUuid>

using namespace QSanProtocol;

CrossRoomSpectateManager::CrossRoomSpectateManager(Server *server)
    : QObject(server)
    , m_server(server)
{
}

void CrossRoomSpectateManager::handleCrossRoomCommand(ServerPlayer *player, CommandType command, const QVariant &arg)
{
    if (player == nullptr)
        return;

    switch (command) {
    case S_COMMAND_CROSS_ROOM_LIST_REQUEST:
        handleListRequest(player);
        break;
    case S_COMMAND_CROSS_ROOM_SPECTATE_START:
        handleStartRequest(player, arg);
        break;
    case S_COMMAND_CROSS_ROOM_SPECTATE_STOP:
        handleStopRequest(player);
        break;
    case S_COMMAND_CROSS_ROOM_SWITCH_TARGET:
        handleSwitchTargetRequest(player, arg);
        break;
    default:
        break;
    }
}

void CrossRoomSpectateManager::handleListRequest(ServerPlayer *player)
{
    // Admission: only players in a waiting room (game not started) may request the room list
    Room *viewerRoom = player->getRoom();
    if (viewerRoom == nullptr || viewerRoom->hasStarted())
        return;

    QVariantList payload = buildRoomListPayload();
    sendToViewer(player, S_COMMAND_CROSS_ROOM_LIST, QVariant(payload));
}

void CrossRoomSpectateManager::handleStartRequest(ServerPlayer *player, const QVariant &arg)
{
    // Parse request: [roomId, targetObjectName]
    JsonArray body = arg.value<JsonArray>();
    if (body.size() < 2)
        return;

    int targetRoomId = body.at(0).toInt();
    QString targetObjectName = body.at(1).toString();
    if (targetObjectName.isEmpty())
        return;

    Room *viewerRoom = player->getRoom();
    if (viewerRoom == nullptr)
        return;

    // Admission: viewer must be in a waiting room (game not started)
    if (viewerRoom->hasStarted())
        return;

    // Admission: viewer must not already have an active session
    {
        QReadLocker locker(&m_lock);
        if (m_sessionsByViewer.contains(player->objectName()))
            return;
    }

    // Find the target room
    Room *targetRoom = m_server->findRoomById(targetRoomId);
    if (targetRoom == nullptr)
        return;

    // Admission: target room must be in-game and not finished
    if (!targetRoom->hasStarted() || targetRoom->isFinished())
        return;

    // Admission: target player must exist and be alive
    ServerPlayer *targetPlayer = targetRoom->findPlayerByObjectName(targetObjectName);
    if (targetPlayer == nullptr || !targetPlayer->isAlive())
        return;

    // Build the snapshot BEFORE registering the observer to avoid double-counting:
    // if we register first, state changes between registration and snapshot read would
    // be both captured in the snapshot (absolute values) and forwarded as delta events.
    // NOTE: This is a cross-thread read — the target room's game thread may be modifying
    // state concurrently. The tiny window between snapshot and registration may lose events,
    // but this is preferable to double-counting which causes persistent state drift.
    QVariantMap snapshot = targetRoom->buildCrossRoomSnapshot(targetObjectName);
    targetRoom->addCrossRoomObserver(targetObjectName);

    // Create session
    QString sessionId = QUuid::createUuid().toString();
    Session session;
    session.sessionId = sessionId;
    session.viewer = player;
    session.sourceRoomId = viewerRoom->getId();
    session.targetRoomId = targetRoomId;
    session.viewerObjectName = player->objectName();
    session.targetObjectName = targetObjectName;
    session.serial = 0;

    {
        QWriteLocker locker(&m_lock);
        m_sessionsByViewer[player->objectName()] = session;
        m_viewersByTargetRoom.insert(targetRoomId, player->objectName());
    }

    // Send initial snapshot to the viewer
    JsonObject snapshotPayload;
    snapshotPayload["sessionId"] = sessionId;
    snapshotPayload["targetRoomId"] = targetRoomId;
    snapshotPayload["targetName"] = targetObjectName;
    snapshotPayload["snapshot"] = snapshot;
    sendToViewer(player, S_COMMAND_CROSS_ROOM_SPECTATE_SNAPSHOT, QVariant(snapshotPayload));
}

void CrossRoomSpectateManager::handleStopRequest(ServerPlayer *player)
{
    if (player == nullptr)
        return;

    QString viewerName = player->objectName();

    QReadLocker locker(&m_lock);
    if (!m_sessionsByViewer.contains(viewerName))
        return;
    locker.unlock();

    removeSession(viewerName, "VIEWER_REQUESTED");
}

void CrossRoomSpectateManager::handleSwitchTargetRequest(ServerPlayer *player, const QVariant &arg)
{
    // Parse the new target name from the argument (sent as a plain string)
    QString newTargetName = arg.toString().trimmed();
    if (newTargetName.isEmpty())
        return;

    QString viewerName = player->objectName();

    // Take a snapshot of the session under read lock to validate outside the lock
    Session sessionSnapshot;
    {
        QReadLocker locker(&m_lock);
        auto it = m_sessionsByViewer.constFind(viewerName);
        if (it == m_sessionsByViewer.constEnd())
            return;
        sessionSnapshot = it.value();
    }

    // Validate the target room is still active
    Room *targetRoom = m_server->findRoomById(sessionSnapshot.targetRoomId);
    if (targetRoom == nullptr || !targetRoom->hasStarted() || targetRoom->isFinished())
        return;

    // Validate the new target player exists and is alive
    ServerPlayer *newTarget = targetRoom->findPlayerByObjectName(newTargetName);
    if (newTarget == nullptr || !newTarget->isAlive())
        return;

    // Build sync data while outside the write lock (cross-thread best-effort read)
    QVariant syncDataVar = targetRoom->buildPerspectiveSyncData(newTargetName);
    JsonArray syncData = syncDataVar.value<JsonArray>();
    if (syncData.size() < 4)
        return;

    // Apply the switch under write lock
    bool shouldRemoveSession = false;
    {
        QWriteLocker locker(&m_lock);
        auto it = m_sessionsByViewer.find(viewerName);
        if (it == m_sessionsByViewer.end())
            return;

        Session &session = it.value();
        // Guard against concurrent session changes
        if (session.targetRoomId != sessionSnapshot.targetRoomId)
            return;

        // Update observer ref counts if the target actually changed
        QString oldTargetName = session.targetObjectName;
        if (oldTargetName != newTargetName) {
            targetRoom->addCrossRoomObserver(newTargetName);
            targetRoom->removeCrossRoomObserver(oldTargetName);
        }

        session.targetObjectName = newTargetName;
        session.serial++;

        ServerPlayer *viewer = session.viewer.data();
        if (viewer == nullptr) {
            shouldRemoveSession = true;
        } else {
            // Response: [sessionId, serial, targetName, handCards, pilesObj, modifiedCards]
            JsonArray payload;
            payload << session.sessionId;
            payload << session.serial;
            payload << syncData.at(0);
            payload << syncData.at(1);
            payload << syncData.at(2);
            payload << syncData.at(3);
            sendToViewer(viewer, S_COMMAND_CROSS_ROOM_SWITCH_TARGET, QVariant(payload));
        }
    }

    if (shouldRemoveSession)
        removeSession(viewerName, "VIEWER_DISCONNECTED");
}

void CrossRoomSpectateManager::onTargetNotify(int roomId, const QString &recipientName, int command, const QVariant &arg)
{
    // Write lock required: session.serial is mutated below
    QWriteLocker locker(&m_lock);
    QList<QString> viewers = m_viewersByTargetRoom.values(roomId);
    if (viewers.isEmpty())
        return;

    QList<QString> staleViewers;

    // Forward the event to each viewer whose target matches the recipient
    foreach (const QString &viewerName, viewers) {
        auto it = m_sessionsByViewer.find(viewerName);
        if (it == m_sessionsByViewer.end())
            continue;

        Session &session = it.value();
        if (session.targetObjectName != recipientName)
            continue;

        ServerPlayer *viewer = session.viewer.data();
        if (viewer == nullptr) {
            staleViewers << viewerName;
            continue;
        }

        session.serial++;

        JsonArray eventPayload;
        eventPayload << session.sessionId;
        eventPayload << session.serial;
        eventPayload << command;
        eventPayload << arg;
        sendToViewer(viewer, S_COMMAND_CROSS_ROOM_SPECTATE_EVENT, QVariant(eventPayload));
    }
    locker.unlock();

    // Clean up stale sessions (viewer disconnected / destroyed)
    foreach (const QString &viewerName, staleViewers)
        removeSession(viewerName, "VIEWER_DISCONNECTED");
}

void CrossRoomSpectateManager::onBroadcastNotify(int roomId, int command, const QVariant &arg)
{
    // Write lock required: session.serial is mutated below
    QWriteLocker locker(&m_lock);
    QList<QString> viewers = m_viewersByTargetRoom.values(roomId);
    if (viewers.isEmpty())
        return;

    QList<QString> staleViewers;

    // Forward the broadcast event to all viewers of this room
    foreach (const QString &viewerName, viewers) {
        auto it = m_sessionsByViewer.find(viewerName);
        if (it == m_sessionsByViewer.end())
            continue;

        Session &session = it.value();
        ServerPlayer *viewer = session.viewer.data();
        if (viewer == nullptr) {
            staleViewers << viewerName;
            continue;
        }

        session.serial++;

        JsonArray eventPayload;
        eventPayload << session.sessionId;
        eventPayload << session.serial;
        eventPayload << command;
        eventPayload << arg;
        sendToViewer(viewer, S_COMMAND_CROSS_ROOM_SPECTATE_EVENT, QVariant(eventPayload));
    }
    locker.unlock();

    // Clean up stale sessions (viewer disconnected / destroyed)
    foreach (const QString &viewerName, staleViewers)
        removeSession(viewerName, "VIEWER_DISCONNECTED");
}

void CrossRoomSpectateManager::onRoomTeardown(int roomId)
{
    // Collect all viewers watching this room, then remove their sessions
    QList<QString> viewersToRemove;
    {
        QReadLocker locker(&m_lock);
        viewersToRemove = m_viewersByTargetRoom.values(roomId);
    }

    foreach (const QString &viewerName, viewersToRemove)
        removeSession(viewerName, "TARGET_ROOM_OVER");
}

void CrossRoomSpectateManager::stopSessionsBySourceRoom(int sourceRoomId, const QString &reason)
{
    // Collect all viewers whose source room matches
    QList<QString> viewersToRemove;
    {
        QReadLocker locker(&m_lock);
        for (auto it = m_sessionsByViewer.constBegin(); it != m_sessionsByViewer.constEnd(); ++it) {
            if (it.value().sourceRoomId == sourceRoomId)
                viewersToRemove << it.key();
        }
    }

    foreach (const QString &viewerName, viewersToRemove)
        removeSession(viewerName, reason);
}

void CrossRoomSpectateManager::removeSession(const QString &viewerObjectName, const QString &reason)
{
    Session session;
    {
        QWriteLocker locker(&m_lock);
        auto it = m_sessionsByViewer.find(viewerObjectName);
        if (it == m_sessionsByViewer.end())
            return;
        session = it.value();
        m_sessionsByViewer.erase(it);
        m_viewersByTargetRoom.remove(session.targetRoomId, viewerObjectName);
    }

    // Unregister observer ref count on the target room
    Room *targetRoom = m_server->findRoomById(session.targetRoomId);
    if (targetRoom != nullptr)
        targetRoom->removeCrossRoomObserver(session.targetObjectName);

    // Notify the viewer that spectating has ended
    ServerPlayer *viewer = session.viewer.data();
    if (viewer != nullptr) {
        JsonObject endPayload;
        endPayload["sessionId"] = session.sessionId;
        endPayload["reason"] = reason;
        sendToViewer(viewer, S_COMMAND_CROSS_ROOM_SPECTATE_ENDED, QVariant(endPayload));
    }
}

QVariantList CrossRoomSpectateManager::buildRoomListPayload() const
{
    QVariantList result;
    QList<Room *> allRooms = m_server->getRooms();
    foreach (Room *room, allRooms) {
        if (!room->hasStarted() || room->isFinished())
            continue;

        JsonObject entry;
        entry["roomId"] = room->getId();
        entry["mode"] = room->getMode();
        entry["playerCount"] = room->alivePlayerCount();

        JsonArray playerList;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            JsonObject pInfo;
            pInfo["objectName"] = p->objectName();
            pInfo["screenName"] = p->screenName();
            pInfo["generalName"] = p->getGeneralName();
            playerList << QVariant(pInfo);
        }
        entry["players"] = QVariant(playerList);
        result << QVariant(entry);
    }
    return result;
}

void CrossRoomSpectateManager::sendToViewer(ServerPlayer *viewer, CommandType command, const QVariant &arg)
{
    if (viewer == nullptr)
        return;

    Packet packet(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT, command);
    packet.setMessageBody(arg);
    viewer->invoke(&packet);
}
