#ifndef _CROSS_ROOM_SPECTATE_MANAGER_H
#define _CROSS_ROOM_SPECTATE_MANAGER_H

#include "protocol.h"

#include <QHash>
#include <QMultiHash>
#include <QObject>
#include <QPointer>
#include <QReadWriteLock>

class Room;
class Server;
class ServerPlayer;

// Manages cross-room spectate sessions: creation, lookup, teardown, and event forwarding.
// Lives in the Server's thread (main thread). Receives tap signals from target Rooms
// via Qt::QueuedConnection (thread-safe, value-type parameters only).
class CrossRoomSpectateManager : public QObject
{
    Q_OBJECT

public:
    struct Session
    {
        QString sessionId; // UUID
        QPointer<ServerPlayer> viewer;
        int sourceRoomId; // waiting room where the viewer sits
        int targetRoomId; // game room being spectated
        QString viewerObjectName;
        QString targetObjectName;
        int serial; // incremental sync counter for ordering
    };

    explicit CrossRoomSpectateManager(Server *server);

    // Entry point called by Room callback methods to dispatch cross-room commands.
    // Runs in the RoomThread context — must not access Room internals beyond the args.
    void handleCrossRoomCommand(ServerPlayer *player, QSanProtocol::CommandType command, const QVariant &arg);

    // Stop all sessions whose viewer is in the given source room (e.g. when that room starts a game).
    void stopSessionsBySourceRoom(int sourceRoomId, const QString &reason);

public slots:
    // Tap receivers — connected to Room signals via Qt::QueuedConnection.
    // Parameters are all value types; no Room* or ServerPlayer* pointers cross thread boundaries.
    void onTargetNotify(int roomId, const QString &recipientName, int command, const QVariant &arg);
    void onBroadcastNotify(int roomId, int command, const QVariant &arg);
    void onRoomTeardown(int roomId);

private:
    void handleListRequest(ServerPlayer *player);
    void handleStartRequest(ServerPlayer *player, const QVariant &arg);
    void handleStopRequest(ServerPlayer *player);
    void handleSwitchTargetRequest(ServerPlayer *player, const QVariant &arg);

    QVariantList buildRoomListPayload() const;
    void sendToViewer(ServerPlayer *viewer, QSanProtocol::CommandType command, const QVariant &arg);
    void removeSession(const QString &viewerObjectName, const QString &reason);

    Server *m_server;

    // Session storage — protected by m_lock (read-heavy: event forwarding is read, session create/destroy is write)
    mutable QReadWriteLock m_lock;
    QHash<QString, Session> m_sessionsByViewer; // viewerObjectName -> Session
    QMultiHash<int, QString> m_viewersByTargetRoom; // targetRoomId -> viewerObjectNames
};

#endif
