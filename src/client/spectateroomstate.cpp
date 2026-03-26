#include "spectateroomstate.h"

#include "engine.h"
#include "json.h"

#include <algorithm>

using namespace QSanProtocol;

namespace {
static void spectatePopulateInnateSkills(ClientPlayer *player)
{
    if (player == nullptr)
        return;

    const General *general = player->getGeneral();
    if (general != nullptr) {
        QList<const Skill *> headSkills = isHegemonyGameMode(ServerInfo.GameMode)
            ? general->getSkillList(true, true)
            : general->getSkillList();
        foreach (const Skill *skill, headSkills) {
            if (skill == nullptr)
                continue;
            if (skill->isLordSkill() && !player->isLord())
                continue;
            player->addSkill(skill->objectName(), true);
        }

        if (!isHegemonyGameMode(ServerInfo.GameMode))
            player->setGender(general->getGender());
    }

    const General *general2 = player->getGeneral2();
    if (general2 != nullptr) {
        QList<const Skill *> deputySkills = isHegemonyGameMode(ServerInfo.GameMode)
            ? general2->getSkillList(true, false)
            : general2->getSkillList();
        foreach (const Skill *skill, deputySkills) {
            if (skill == nullptr)
                continue;
            if (skill->isLordSkill() && !player->isLord())
                continue;
            player->addSkill(skill->objectName(), false);
        }
    }
}
}

SpectateRoomState::SpectateRoomState(QObject *parent)
    : Client(parent, QString(), false, false)
    , m_active(false)
    , m_roomId(0)
    , m_lastEventSeq(0)
    , m_snapshotVersion(0)
{
    players.clear();
    alive_count = 0;
    if (m_self != nullptr) {
        delete m_self;
        m_self = nullptr;
    }
}

void SpectateRoomState::clearVirtualState()
{
    if (Self != nullptr && Self->parent() == this)
        Self = nullptr;

    players.clear();
    qDeleteAll(m_virtualPlayers);
    m_virtualPlayers.clear();
    alive_count = 0;
    m_lastPerspectiveSyncSerial = 0;
    m_perspectiveTargetName.clear();
    m_savedPileOpenState.clear();
    _m_roomState.reset();
}

void SpectateRoomState::applyStartedPayload(const QVariant &arg)
{
    QVariantMap payload = arg.toMap();
    QVariantMap snapshot = payload.value("snapshot").toMap();
    QVariantList snapshotPlayers = snapshot.value("players").toList();
    if (snapshotPlayers.isEmpty())
        return;

    clearVirtualState();

    Sanguosha->registerRoom(this);
    _m_roomState.reset();

    QString targetName = payload.value("targetName").toString();
    if (targetName.isEmpty())
        targetName = snapshot.value("targetName").toString();

    ClientPlayer *targetPlayer = nullptr;
    int targetPublicHandcardNum = 0;
    QList<ClientPlayer *> seatOrdered;

    foreach (const QVariant &playerVar, snapshotPlayers) {
        QVariantMap pObj = playerVar.toMap();
        QString objectName = pObj.value("objectName").toString();
        if (objectName.isEmpty())
            continue;

        ClientPlayer *vp = new ClientPlayer(this);
        vp->setObjectName(objectName);
        vp->setScreenName(pObj.value("screenName").toString());
        vp->setGeneralName(pObj.value("generalName").toString());
        vp->setGeneral2Name(pObj.value("general2Name").toString());
        vp->setKingdom(pObj.value("kingdom").toString());
        vp->setRole(pObj.value("role").toString());
        vp->setHp(pObj.value("hp").toInt());
        vp->setMaxHp(pObj.value("maxHp").toInt());
        vp->setAlive(pObj.value("alive", true).toBool());
        vp->setSeat(pObj.value("seat").toInt());
        vp->setInitialSeat(pObj.value("seat").toInt());
        vp->setPhase(static_cast<Player::Phase>(pObj.value("phase").toInt()));
        vp->setFaceUp(pObj.value("faceUp", true).toBool());
        vp->setChained(pObj.value("chained", false).toBool());
        vp->setHandcardNum(pObj.value("handcardNum").toInt());
        vp->setRemoved(pObj.value("removed", false).toBool());
        vp->setShownRole(pObj.value("roleShown", false).toBool());
        vp->setGeneralShowed(pObj.value("generalShowed", false).toBool());
        vp->setGeneral2Showed(pObj.value("general2Showed", false).toBool());
        spectatePopulateInnateSkills(vp);
        if (pObj.contains("renHp"))
            vp->setRenHp(pObj.value("renHp").toInt());
        if (pObj.contains("lingHp"))
            vp->setLingHp(pObj.value("lingHp").toInt());
        if (pObj.contains("chaoren"))
            vp->setChaoren(pObj.value("chaoren").toInt());

        QVariantList equips = pObj.value("equips").toList();
        foreach (const QVariant &equipVar, equips) {
            WrappedCard *equip = Sanguosha->getWrappedCard(equipVar.toInt());
            if (equip != nullptr)
                vp->setEquip(equip);
        }

        QVariantList judgeArea = pObj.value("judgeArea").toList();
        foreach (const QVariant &judgeVar, judgeArea) {
            WrappedCard *card = Sanguosha->getWrappedCard(judgeVar.toInt());
            if (card != nullptr)
                vp->addDelayedTrick(card);
        }

        QList<int> shownHandcards;
        JsonUtils::tryParse(pObj.value("shownHandcards"), shownHandcards);
        vp->setShownHandcards(shownHandcards);

        QList<int> brokenEquips;
        JsonUtils::tryParse(pObj.value("brokenEquips"), brokenEquips);
        vp->setBrokenEquips(brokenEquips);

        QVariantMap playerPiles = pObj.value("piles").toMap();
        for (auto it = playerPiles.constBegin(); it != playerPiles.constEnd(); ++it) {
            QList<int> pileIds;
            JsonUtils::tryParse(it.value(), pileIds);
            vp->setPile(it.key(), pileIds);
        }

        QVariantMap pileOpenObj = pObj.value("pileOpen").toMap();
        for (auto it = pileOpenObj.constBegin(); it != pileOpenObj.constEnd(); ++it) {
            QStringList openPlayers;
            JsonUtils::tryParse(it.value(), openPlayers);
            foreach (const QString &viewerName, openPlayers)
                vp->setPileOpen(it.key(), viewerName);
        }

        QVariantMap marks = pObj.value("marks").toMap();
        for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
            vp->setMark(it.key(), it.value().toInt());

        QStringList acquiredHeadSkills;
        if (JsonUtils::tryParse(pObj.value("acquiredHeadSkills"), acquiredHeadSkills)) {
            foreach (const QString &skillName, acquiredHeadSkills)
                vp->acquireSkill(skillName, true);
        }

        QStringList acquiredDeputySkills;
        if (JsonUtils::tryParse(pObj.value("acquiredDeputySkills"), acquiredDeputySkills)) {
            foreach (const QString &skillName, acquiredDeputySkills)
                vp->acquireSkill(skillName, false);
        }

        QStringList hiddenGenerals;
        if (JsonUtils::tryParse(pObj.value("hiddenGenerals"), hiddenGenerals))
            vp->setHiddenGenerals(hiddenGenerals);
        QString shownHiddenGeneral = pObj.value("shownHiddenGeneral").toString();
        if (!shownHiddenGeneral.isEmpty())
            vp->setShownHiddenGeneral(shownHiddenGeneral);

        QStringList skillInvalidList;
        if (JsonUtils::tryParse(pObj.value("skillInvalid"), skillInvalidList)) {
            foreach (const QString &invalidSkill, skillInvalidList)
                vp->setSkillInvalidity(invalidSkill, true);
        }

        QStringList disableShowList;
        if (JsonUtils::tryParse(pObj.value("disableShow"), disableShowList)) {
            foreach (const QString &entry, disableShowList) {
                int sep = entry.indexOf(',');
                if (sep > 0)
                    vp->setDisableShow(entry.left(sep), entry.mid(sep + 1));
            }
        }

        QStringList flagList;
        if (JsonUtils::tryParse(pObj.value("flags"), flagList)) {
            foreach (const QString &flag, flagList)
                vp->setFlags(flag);
        }

        m_virtualPlayers << vp;
        seatOrdered << vp;
        if (objectName == targetName) {
            targetPlayer = vp;
            targetPublicHandcardNum = pObj.value("handcardNum").toInt();
        }
    }

    if (m_virtualPlayers.isEmpty())
        return;

    std::sort(seatOrdered.begin(), seatOrdered.end(), [](const ClientPlayer *a, const ClientPlayer *b) {
        return a->getSeat() < b->getSeat();
    });
    for (int i = 0; i < seatOrdered.length(); ++i) {
        ClientPlayer *current = seatOrdered.at(i);
        ClientPlayer *next = seatOrdered.at((i + 1) % seatOrdered.length());
        if (current != nullptr && next != nullptr)
            current->setNext(next->objectName());
    }

    QVariantList modCards = snapshot.value("modifiedCards").toList();
    for (int cardIndex = 0; cardIndex < modCards.size(); ++cardIndex)
        updateCard(modCards.at(cardIndex));

    if (targetPlayer == nullptr)
        targetPlayer = m_virtualPlayers.first();

    QList<int> targetHandCards;
    JsonUtils::tryParse(snapshot.value("handCards"), targetHandCards);
    while (targetHandCards.length() < targetPublicHandcardNum)
        targetHandCards << Card::S_UNKNOWN_CARD_ID;
    targetPlayer->setCards(targetHandCards);
    targetPlayer->setHandcardNum(targetHandCards.length());

    players.clear();
    foreach (ClientPlayer *vp, m_virtualPlayers)
        players << vp;

    m_self = targetPlayer;
    Self = targetPlayer;
    alive_count = 0;
    foreach (const ClientPlayer *player, players) {
        if (player != nullptr && player->isAlive())
            alive_count++;
    }

    m_sessionId = payload.value("sessionId").toString();
    m_roomId = payload.value("roomId", payload.value("targetRoomId")).toInt();
    m_targetName = targetName;
    m_lastEventSeq = payload.value("lastEventSeq").toInt();
    m_snapshotVersion = payload.value("snapshotVersion").toInt();
    m_active = true;
}

bool SpectateRoomState::applyEventBatch(const QVariant &arg)
{
    QVariantMap payload = arg.toMap();
    if (!m_active)
        return false;

    QString sessionId = payload.value("sessionId").toString();
    if (!sessionId.isEmpty() && sessionId != m_sessionId)
        return true;

    int fromSeq = payload.value("fromSeq").toInt();
    if (fromSeq <= m_lastEventSeq)
        return false;

    QVariantList events = payload.value("events").toList();
    foreach (const QVariant &eventVar, events) {
        QVariantMap eventMap = eventVar.toMap();
        int seq = eventMap.value("seq").toInt();
        if (seq <= m_lastEventSeq)
            return false;

        CommandType commandType = static_cast<CommandType>(eventMap.value("command").toInt());
        if (hasInteractiveCommand(commandType)) {
            switch (commandType) {
            case S_COMMAND_SHOW_CARD:
            case S_COMMAND_INVOKE_SKILL:
                break;
            default:
                m_lastEventSeq = seq;
                continue;
            }
        }

        switch (commandType) {
        case S_COMMAND_GAME_OVER:
        case S_COMMAND_GAME_START:
        case S_COMMAND_ADD_PLAYER:
        case S_COMMAND_REMOVE_PLAYER:
        case S_COMMAND_ARRANGE_SEATS:
        case S_COMMAND_SETUP:
        case S_COMMAND_CHECK_VERSION:
        case S_COMMAND_WARN:
            m_lastEventSeq = seq;
            continue;
        default:
            break;
        }

        Callback callback = callbackForCommand(commandType);
        if (callback != nullptr) {
            Sanguosha->registerRoom(this);
            (this->*callback)(eventMap.value("payload"));
        }
        m_lastEventSeq = seq;
        m_snapshotVersion = qMax(m_snapshotVersion, eventMap.value("snapshotVersion").toInt());
    }

    return true;
}

void SpectateRoomState::applyTargetSwitchedPayload(const QVariant &arg)
{
    QVariantMap payload = arg.toMap();
    if (!m_active)
        return;

    QString sessionId = payload.value("sessionId").toString();
    if (!sessionId.isEmpty() && sessionId != m_sessionId)
        return;

    QVariantMap privateState = payload.value("privateState").toMap();
    QString targetName = payload.value("targetName").toString();
    ClientPlayer *targetPlayer = getPlayer(targetName);
    QList<int> handCards;
    JsonUtils::tryParse(privateState.value("handCards"), handCards);
    int targetPublicHandcardNum = targetPlayer != nullptr ? targetPlayer->getHandcardNum() : handCards.length();
    while (handCards.length() < targetPublicHandcardNum)
        handCards << Card::S_UNKNOWN_CARD_ID;
    QVariantMap pilesObj = privateState.value("piles").toMap();
    QVariantList modifiedCards = privateState.value("modifiedCards").toList();

    JsonArray perspectivePayload;
    perspectivePayload << (m_lastPerspectiveSyncSerial + 1);
    perspectivePayload << targetName;
    perspectivePayload << JsonUtils::toJsonArray(handCards);
    perspectivePayload << QVariant(pilesObj);
    perspectivePayload << QVariant(modifiedCards);

    m_targetName = targetName;
    m_snapshotVersion = qMax(m_snapshotVersion, payload.value("snapshotVersion").toInt());
    m_lastEventSeq = qMax(m_lastEventSeq, payload.value("lastEventSeq").toInt());

    Sanguosha->registerRoom(this);
    perspectiveSync(QVariant(perspectivePayload));
}

bool SpectateRoomState::hasIncompleteTargetHand() const
{
    if (!m_active || m_self == nullptr)
        return false;

    QList<int> ids = m_self->getKnownHandCardIds();
    if (ids.length() != m_self->getHandcardNum())
        return true;

    return ids.contains(Card::S_UNKNOWN_CARD_ID);
}

void SpectateRoomState::applyEndedPayload(const QVariant &arg)
{
    QVariantMap payload = arg.toMap();
    QString sessionId = payload.value("sessionId").toString();
    if (!sessionId.isEmpty() && !m_sessionId.isEmpty() && sessionId != m_sessionId)
        return;

    clearVirtualState();
    m_active = false;
    m_sessionId.clear();
    m_roomId = 0;
    m_targetName.clear();
    m_lastEventSeq = 0;
    m_snapshotVersion = 0;
}
