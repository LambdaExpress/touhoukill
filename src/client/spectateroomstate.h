#ifndef _SPECTATE_ROOM_STATE_H
#define _SPECTATE_ROOM_STATE_H

#include "client.h"

class SpectateRoomState : public Client
{
    Q_OBJECT

public:
    explicit SpectateRoomState(QObject *parent = nullptr);

    void applyStartedPayload(const QVariant &arg);
    bool applyEventBatch(const QVariant &arg);
    void applyTargetSwitchedPayload(const QVariant &arg);
    void applyEndedPayload(const QVariant &arg);

    bool isActive() const { return m_active; }
    QString sessionId() const { return m_sessionId; }
    int roomId() const { return m_roomId; }
    int lastEventSeq() const { return m_lastEventSeq; }
    QString currentTargetName() const { return m_targetName; }
    bool hasIncompleteTargetHand() const;

private:
    void clearVirtualState();

    bool m_active;
    QString m_sessionId;
    int m_roomId;
    QString m_targetName;
    int m_lastEventSeq;
    int m_snapshotVersion;
    QList<ClientPlayer *> m_virtualPlayers;
};

#endif
