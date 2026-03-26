#ifndef _SPECTATE_PROJECTION_H
#define _SPECTATE_PROJECTION_H

#include "protocol.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QReadWriteLock>
#include <QVariant>

class Room;

class SpectateProjection : public QObject
{
    Q_OBJECT

public:
    struct EventRecord
    {
        int seq;
        int snapshotVersion;
        int command;
        QVariant payload;
        QString recipientName;
    };

    explicit SpectateProjection(QObject *parent = nullptr);

    void captureState(const Room *room);
    void captureEvent(const Room *room, QSanProtocol::CommandType command, const QVariant &payload, const QString &recipientName = QString());

    QVariantMap buildSnapshot(const QString &targetName) const;
    QVariantMap buildPrivateState(const QString &targetName) const;
    QVariantMap buildRoomEntry() const;
    QVariantList eventsAfter(int lastSeq, const QString &targetName, bool *overflow = nullptr) const;
    int lastEventSeq() const;
    int snapshotVersion() const;
    bool isAlive(const QString &targetName) const;
    QString nextAliveTarget(const QString &currentTarget) const;

signals:
    void advanced(int eventSeq);

private:
    void captureStateLocked(const Room *room);

    mutable QReadWriteLock m_lock;
    int m_snapshotVersion;
    int m_eventSeq;
    QVariantMap m_baseSnapshot;
    QHash<QString, QVariantMap> m_privateStateByPlayer;
    QHash<QString, bool> m_aliveByPlayer;
    QStringList m_aliveOrder;
    QList<EventRecord> m_events;
    int m_maxBufferedEvents;
};

#endif
