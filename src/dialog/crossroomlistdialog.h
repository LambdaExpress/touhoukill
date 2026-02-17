#ifndef _CROSS_ROOM_LIST_DIALOG_H
#define _CROSS_ROOM_LIST_DIALOG_H

#include <QDialog>
#include <QVariantList>

class QPushButton;
class QTableWidget;

class CrossRoomListDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CrossRoomListDialog(QWidget *parent = nullptr);

    void setRoomList(const QVariantList &rooms);

signals:
    void spectateRequested(int roomId, const QString &targetObjectName);

private slots:
    void onRoomSelectionChanged();
    void onPlayerSelectionChanged();
    void onSpectateClicked();

private:
    void updatePlayerTable(const QVariantList &players);
    void updateSpectateButtonState();
    int currentRoomId() const;
    QString currentTargetObjectName() const;

    QTableWidget *m_roomTable;
    QTableWidget *m_playerTable;
    QPushButton *m_spectateButton;
};

#endif
