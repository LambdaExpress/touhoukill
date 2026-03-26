#include "spectatescene.h"

#include "button.h"
#include "engine.h"
#include "spectateroomstate.h"
#include "spectateviewmodel.h"

#include <QKeyEvent>
#include <algorithm>

SpectateScene::SpectateScene(QMainWindow *mainWindow, SpectateViewModel *viewModel)
    : RoomScene(mainWindow, viewModel != nullptr ? viewModel->state() : nullptr, true)
    , m_viewModel(viewModel)
    , m_stopButton(nullptr)
    , m_needsInitialLayout(true)
{
    initializeFromState();

    m_stopButton = new Button(tr("Stop Spectating"));
    m_stopButton->setZValue(10.0);
    addItem(m_stopButton);
    m_stopButton->setPos(sceneRect().right() - m_stopButton->boundingRect().width() - 40, 30);
    connect(m_stopButton, &Button::clicked, this, &SpectateScene::stopSpectating);
}

SpectateScene::~SpectateScene() = default;

void SpectateScene::initializeFromState()
{
    SpectateRoomState *state = (m_viewModel != nullptr) ? m_viewModel->state() : nullptr;
    if (state == nullptr)
        return;

    clearPendingMoveStash();
    m_tablePile->clear(false);

    QList<ClientPlayer *> seatPlayers;
    foreach (const ClientPlayer *player, state->getPlayers()) {
        if (player != Self)
            seatPlayers << const_cast<ClientPlayer *>(player);
    }

    std::sort(seatPlayers.begin(), seatPlayers.end(), [](const ClientPlayer *left, const ClientPlayer *right) {
        return left->getSeat() < right->getSeat();
    });

    if (Self != nullptr) {
        int selfSeat = Self->getSeat();
        int rotateIndex = 0;
        while (rotateIndex < seatPlayers.length() && seatPlayers.at(rotateIndex)->getSeat() <= selfSeat)
            ++rotateIndex;
        if (rotateIndex > 0 && rotateIndex < seatPlayers.length()) {
            QList<ClientPlayer *> rotated = seatPlayers.mid(rotateIndex);
            rotated.append(seatPlayers.mid(0, rotateIndex));
            seatPlayers = rotated;
        }
    }

    name2photo.clear();
    for (int index = 0; index < photos.length(); ++index) {
        Photo *photo = photos.at(index);
        if (index >= seatPlayers.length()) {
            photo->setPlayer(nullptr);
            photo->hide();
            continue;
        }

        ClientPlayer *player = seatPlayers.at(index);
        photo->show();
        photo->setEnabled(true);
        photo->setPlayer(player);
        name2photo[player->objectName()] = photo;
    }

    dashboard->setPlayer(Self);

    QList<const ClientPlayer *> seats;
    foreach (ClientPlayer *player, seatPlayers)
        seats << player;
    if (!seats.isEmpty())
        arrangeSeats(seats);

    for (int index = 0; index < seatPlayers.length() && index < photos.length(); ++index) {
        Photo *photo = photos.at(index);
        ClientPlayer *player = seatPlayers.at(index);
        photo->syncCardAreasFromPlayer();
        if (player->isAlive())
            photo->revivePlayer();
        else
            photo->killPlayer();
        photo->syncRemovedVisualState();
        if (isHegemonyGameMode(ServerInfo.GameMode))
            photo->getHegemonyRoleComboBox()->fix(player->getRole() == "careerist" ? "careerist" : player->getRole());
        else
            photo->getRoleComboBox()->fix(player->getRole());
    }

    onGameStart();
    dashboard->setSpectating(true);
    applyPerspectiveInputLock(true);
    refreshItem2PlayerMap();
    updateTable();

    log_box->append(QString("<font color='%1'>-- %2 --</font>")
                        .arg(Config.TextEditColor.name())
                        .arg(tr("Spectating room %1, target %2").arg(m_viewModel->roomId()).arg(m_viewModel->targetName())));
    log_box->append(QString("<font color='%1'>%2</font>")
                        .arg(Config.TextEditColor.name())
                        .arg(tr("Press Escape to stop spectating.")));
}

void SpectateScene::adjustItems()
{
    RoomScene::adjustItems();

    if (m_needsInitialLayout) {
        dashboard->syncContainerFromPlayer();
        if (Self != nullptr && Self->isAlive())
            dashboard->revivePlayer();
        else
            dashboard->killPlayer();
        dashboard->syncRemovedVisualState();
        if (Self != nullptr) {
            if (isHegemonyGameMode(ServerInfo.GameMode))
                dashboard->getHegemonyRoleComboBox()->fix(Self->getRole() == "careerist" ? "careerist" : Self->getRole());
            else
                dashboard->getRoleComboBox()->fix(Self->getRole());
        }
        updateTable();
        m_needsInitialLayout = false;
    }

    if (m_stopButton != nullptr) {
        qreal x = sceneRect().right() - m_stopButton->boundingRect().width() - 30;
        qreal y = sceneRect().top() + 30;
        m_stopButton->setPos(x, y);
    }
}

void SpectateScene::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        stopSpectating();
        return;
    }
    RoomScene::keyReleaseEvent(event);
}

void SpectateScene::stopSpectating()
{
    if (ClientInstance != nullptr)
        ClientInstance->requestStopCrossRoomSpectate();
}
