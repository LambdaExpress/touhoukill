#include "crossroomlistdialog.h"
#include "engine.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

CrossRoomListDialog::CrossRoomListDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Cross-room Spectate"));
    resize(640, 420);

    // Room list table
    m_roomTable = new QTableWidget(this);
    m_roomTable->setColumnCount(3);
    m_roomTable->setHorizontalHeaderLabels(QStringList() << tr("Room ID") << tr("Mode") << tr("Players"));
    m_roomTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_roomTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_roomTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_roomTable->setAlternatingRowColors(true);
    m_roomTable->verticalHeader()->setVisible(false);
    m_roomTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_roomTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_roomTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    // Player list table (populated when a room is selected)
    QLabel *playerLabel = new QLabel(tr("Select a player to spectate:"), this);

    m_playerTable = new QTableWidget(this);
    m_playerTable->setColumnCount(2);
    m_playerTable->setHorizontalHeaderLabels(QStringList() << tr("Screen Name") << tr("General"));
    m_playerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_playerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_playerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_playerTable->setAlternatingRowColors(true);
    m_playerTable->verticalHeader()->setVisible(false);
    m_playerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // Buttons
    m_spectateButton = new QPushButton(tr("Spectate"), this);
    QPushButton *cancelButton = new QPushButton(tr("Cancel"), this);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_spectateButton);
    buttonLayout->addWidget(cancelButton);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_roomTable, 1);
    mainLayout->addWidget(playerLabel);
    mainLayout->addWidget(m_playerTable, 1);
    mainLayout->addLayout(buttonLayout);

    connect(m_roomTable, &QTableWidget::itemSelectionChanged, this, &CrossRoomListDialog::onRoomSelectionChanged);
    connect(m_playerTable, &QTableWidget::itemSelectionChanged, this, &CrossRoomListDialog::onPlayerSelectionChanged);
    connect(m_spectateButton, &QPushButton::clicked, this, &CrossRoomListDialog::onSpectateClicked);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    updateSpectateButtonState();
}

void CrossRoomListDialog::setRoomList(const QVariantList &rooms)
{
    m_roomTable->clearContents();
    m_roomTable->setRowCount(0);

    for (int i = 0; i < rooms.size(); ++i) {
        QVariantMap room = rooms.at(i).toMap();

        m_roomTable->insertRow(i);

        QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(room.value("roomId").toInt()));
        idItem->setData(Qt::UserRole, room.value("roomId"));
        idItem->setData(Qt::UserRole + 1, room.value("players"));

        m_roomTable->setItem(i, 0, idItem);
        m_roomTable->setItem(i, 1, new QTableWidgetItem(Sanguosha->getModeName(room.value("mode").toString())));
        m_roomTable->setItem(i, 2, new QTableWidgetItem(QString::number(room.value("playerCount").toInt())));
    }

    if (m_roomTable->rowCount() > 0) {
        m_roomTable->setCurrentCell(0, 0);
        onRoomSelectionChanged();
    } else {
        updatePlayerTable(QVariantList());
    }

    updateSpectateButtonState();
}

void CrossRoomListDialog::onRoomSelectionChanged()
{
    QTableWidgetItem *idItem = m_roomTable->item(m_roomTable->currentRow(), 0);
    if (idItem == nullptr) {
        updatePlayerTable(QVariantList());
        return;
    }

    updatePlayerTable(idItem->data(Qt::UserRole + 1).toList());
}

void CrossRoomListDialog::onPlayerSelectionChanged()
{
    updateSpectateButtonState();
}

void CrossRoomListDialog::onSpectateClicked()
{
    int roomId = currentRoomId();
    QString target = currentTargetObjectName();
    if (roomId < 0 || target.isEmpty())
        return;

    emit spectateRequested(roomId, target);
    accept();
}

void CrossRoomListDialog::updatePlayerTable(const QVariantList &players)
{
    m_playerTable->clearContents();
    m_playerTable->setRowCount(players.size());

    for (int i = 0; i < players.size(); ++i) {
        QVariantMap p = players.at(i).toMap();
        QTableWidgetItem *screenNameItem = new QTableWidgetItem(p.value("screenName").toString());
        screenNameItem->setData(Qt::UserRole, p.value("objectName").toString());
        m_playerTable->setItem(i, 0, screenNameItem);
        m_playerTable->setItem(i, 1, new QTableWidgetItem(Sanguosha->translate(p.value("generalName").toString())));
    }

    if (m_playerTable->rowCount() > 0)
        m_playerTable->setCurrentCell(0, 0);

    updateSpectateButtonState();
}

void CrossRoomListDialog::updateSpectateButtonState()
{
    m_spectateButton->setEnabled(currentRoomId() >= 0 && !currentTargetObjectName().isEmpty());
}

int CrossRoomListDialog::currentRoomId() const
{
    QTableWidgetItem *idItem = m_roomTable->item(m_roomTable->currentRow(), 0);
    if (idItem == nullptr)
        return -1;

    bool ok = false;
    int roomId = idItem->data(Qt::UserRole).toInt(&ok);
    return ok ? roomId : -1;
}

QString CrossRoomListDialog::currentTargetObjectName() const
{
    QTableWidgetItem *item = m_playerTable->item(m_playerTable->currentRow(), 0);
    return (item != nullptr) ? item->data(Qt::UserRole).toString() : QString();
}
