#ifndef _CLIENT_LOG_BOX_H
#define _CLIENT_LOG_BOX_H

class ClientPlayer;
class Client;

#include <QTextEdit>

class ClientLogBox : public QTextEdit
{
    Q_OBJECT

public:
    explicit ClientLogBox(QWidget *parent = nullptr);
    void setClient(Client *client);
    void appendLog(const QString &type, const QString &from_general, const QStringList &to, const QString &card_str = QString(), QString arg = QString(), QString arg2 = QString());

private:
    QString bold(const QString &str, const QColor &color) const;
    Client *m_client;

public slots:
    void appendLog(const QStringList &log_str);
    QString append(const QString &text);
};

#endif
