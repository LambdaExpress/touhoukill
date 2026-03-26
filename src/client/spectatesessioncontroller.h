#ifndef _SPECTATE_SESSION_CONTROLLER_H
#define _SPECTATE_SESSION_CONTROLLER_H

#include <QObject>
#include <QVariant>

class Client;
class SpectateRoomState;

class SpectateSessionController : public QObject
{
    Q_OBJECT

public:
    explicit SpectateSessionController(Client *transport, QObject *parent = nullptr);

    SpectateRoomState *state() const { return m_state; }
    bool isActive() const;

    void handleStarted(const QVariant &arg);
    void handleEvents(const QVariant &arg);
    void handleEnded(const QVariant &arg);
    void handleTargetSwitched(const QVariant &arg);
    void requestStop();

signals:
    void started(const QVariantMap &payload);
    void ended(const QString &reason);

private:
    Client *m_transport;
    SpectateRoomState *m_state;
};

#endif
