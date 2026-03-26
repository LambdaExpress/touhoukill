#ifndef _SPECTATE_HUB_H
#define _SPECTATE_HUB_H

#include "protocol.h"

#include <QHash>
#include <QMultiHash>
#include <QObject>
#include <QPointer>

class Room;
class Server;
class ServerPlayer;

class SpectateHub : public QObject
{
    Q_OBJECT

public:
    struct Session
    {
        QString sessionId;
        QPointer<ServerPlayer> viewer;
        QString viewerObjectName;
        int sourceRoomId;
        int targetRoomId;
        QString targetObjectName;
        int lastSentSeq;
    };

    explicit SpectateHub(Server *server);

    void handleCommand(ServerPlayer *player, QSanProtocol::CommandType command, const QVariant &arg);
    void stopSessionsBySourceRoom(int sourceRoomId, const QString &reason);

public slots:
    void onProjectionAdvanced(int roomId, int eventSeq);
    void onRoomTeardown(int roomId);

private:
    void handleListRequest(ServerPlayer *player);
    void handleStartRequest(ServerPlayer *player, const QVariant &arg);
    void handleStopRequest(ServerPlayer *player, const QString &reason = QStringLiteral("VIEWER_REQUESTED"));
    void handleSwitchTargetRequest(ServerPlayer *player, const QVariant &arg);
    void handleResyncRequest(ServerPlayer *player, const QVariant &arg);

    void sendPacket(ServerPlayer *viewer, QSanProtocol::CommandType command, const QVariant &payload);
    void sendStarted(Session &session, Room *targetRoom);
    void sendEvents(Session &session, Room *targetRoom);
    void sendTargetSwitched(Session &session, Room *targetRoom);
    void removeSession(const QString &viewerName, const QString &reason);
    QVariantList buildRoomListPayload() const;

    Server *m_server;
    QHash<QString, Session> m_sessionsByViewer;
    QMultiHash<int, QString> m_viewersByTargetRoom;
};

#endif
