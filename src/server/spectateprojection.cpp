#include "spectateprojection.h"

#include "card.h"
#include "json.h"
#include "room.h"
#include "serverplayer.h"

#include <algorithm>

using namespace QSanProtocol;

namespace {
static int spectateMoveId(const QVariant &payload, bool *ok)
{
    QVariantList payloadList = payload.toList();
    if (payloadList.isEmpty()) {
        if (ok != nullptr)
            *ok = false;
        return -1;
    }
    bool localOk = false;
    int moveId = payloadList.first().toInt(&localOk);
    if (ok != nullptr)
        *ok = localOk;
    return moveId;
}

static bool spectatePayloadHasVisibleCards(const QVariant &payload)
{
    QVariantList payloadList = payload.toList();
    for (int index = 1; index < payloadList.length(); ++index) {
        QVariantList moveArgs = payloadList.at(index).toList();
        if (moveArgs.isEmpty())
            continue;

        QList<int> ids;
        if (!JsonUtils::tryParse(moveArgs.first(), ids))
            continue;

        foreach (int id, ids) {
            if (id != Card::S_UNKNOWN_CARD_ID)
                return true;
        }
    }
    return false;
}

static int spectateMoveScore(const SpectateProjection::EventRecord &record, const QString &targetName)
{
    if (!targetName.isEmpty() && record.recipientName == targetName)
        return 3;
    if (spectatePayloadHasVisibleCards(record.payload))
        return 2;
    if (record.recipientName.isEmpty())
        return 1;
    return 0;
}

static int spectateKnownCardCount(const QVariant &cardsVariant)
{
    QList<int> ids;
    if (!JsonUtils::tryParse(cardsVariant, ids))
        return 0;

    int count = 0;
    foreach (int id, ids) {
        if (id != Card::S_UNKNOWN_CARD_ID)
            ++count;
    }
    return count;
}

static QVariantList spectateMergeMovePayload(const QVariantList &basePayload, const QVariantList &candidatePayload,
                                             const SpectateProjection::EventRecord &baseRecord,
                                             const SpectateProjection::EventRecord &candidateRecord,
                                             const QString &targetName)
{
    QVariantList merged = basePayload;
    int maxCount = qMin(basePayload.length(), candidatePayload.length());
    bool preferCandidateOnTie = (!targetName.isEmpty() && candidateRecord.recipientName == targetName
                                 && baseRecord.recipientName != targetName);

    for (int index = 1; index < maxCount; ++index) {
        QVariantList baseMove = merged.at(index).toList();
        QVariantList candidateMove = candidatePayload.at(index).toList();
        if (baseMove.isEmpty() || candidateMove.isEmpty())
            continue;

        QList<int> baseIds;
        QList<int> candidateIds;
        if (!JsonUtils::tryParse(baseMove.first(), baseIds) || !JsonUtils::tryParse(candidateMove.first(), candidateIds))
            continue;

        int mergedCount = qMin(baseIds.length(), candidateIds.length());
        for (int cardIndex = 0; cardIndex < mergedCount; ++cardIndex) {
            if (baseIds[cardIndex] == Card::S_UNKNOWN_CARD_ID && candidateIds[cardIndex] != Card::S_UNKNOWN_CARD_ID)
                baseIds[cardIndex] = candidateIds[cardIndex];
        }

        int baseKnownCount = spectateKnownCardCount(baseMove.first());
        int candidateKnownCount = spectateKnownCardCount(candidateMove.first());
        if (candidateKnownCount > baseKnownCount || (candidateKnownCount == baseKnownCount && preferCandidateOnTie)) {
            QVariantList newMove = candidateMove;
            newMove[0] = JsonUtils::toJsonArray(baseIds);
            merged[index] = QVariant(newMove);
        } else {
            baseMove[0] = JsonUtils::toJsonArray(baseIds);
            merged[index] = QVariant(baseMove);
        }
    }

    for (int index = merged.length(); index < candidatePayload.length(); ++index)
        merged << candidatePayload.at(index);

    return merged;
}
}

SpectateProjection::SpectateProjection(QObject *parent)
    : QObject(parent)
    , m_snapshotVersion(0)
    , m_eventSeq(0)
    , m_maxBufferedEvents(4096)
{
}

void SpectateProjection::captureState(const Room *room)
{
    QWriteLocker locker(&m_lock);
    captureStateLocked(room);
}

void SpectateProjection::captureEvent(const Room *room, CommandType command, const QVariant &payload, const QString &recipientName)
{
    int newSeq = 0;
    {
        QWriteLocker locker(&m_lock);
        captureStateLocked(room);

        EventRecord record;
        record.seq = ++m_eventSeq;
        record.snapshotVersion = m_snapshotVersion;
        record.command = static_cast<int>(command);
        record.payload = payload;
        record.recipientName = recipientName;
        m_events << record;
        while (m_events.length() > m_maxBufferedEvents)
            m_events.removeFirst();
        newSeq = record.seq;
    }

    emit advanced(newSeq);
}

QVariantMap SpectateProjection::buildSnapshot(const QString &targetName) const
{
    QReadLocker locker(&m_lock);
    QVariantMap snapshot = m_baseSnapshot;
    snapshot["snapshotVersion"] = m_snapshotVersion;
    snapshot["targetName"] = targetName;

    QVariantMap privateState = m_privateStateByPlayer.value(targetName);
    snapshot["handCards"] = privateState.value("handCards");
    snapshot["modifiedCards"] = privateState.value("modifiedCards");
    snapshot["privatePiles"] = privateState.value("piles");
    return snapshot;
}

QVariantMap SpectateProjection::buildPrivateState(const QString &targetName) const
{
    QReadLocker locker(&m_lock);
    QVariantMap result = m_privateStateByPlayer.value(targetName);
    result["targetName"] = targetName;
    result["snapshotVersion"] = m_snapshotVersion;
    return result;
}

QVariantMap SpectateProjection::buildRoomEntry() const
{
    QReadLocker locker(&m_lock);
    QVariantMap entry;
    entry["roomId"] = m_baseSnapshot.value("roomId");
    entry["mode"] = m_baseSnapshot.value("mode");

    QVariantList players = m_baseSnapshot.value("players").toList();
    QVariantList alivePlayers;
    foreach (const QVariant &playerVar, players) {
        QVariantMap player = playerVar.toMap();
        if (player.value("alive", true).toBool())
            alivePlayers << playerVar;
    }

    entry["playerCount"] = alivePlayers.length();
    entry["players"] = alivePlayers;
    return entry;
}

QVariantList SpectateProjection::eventsAfter(int lastSeq, const QString &targetName, bool *overflow) const
{
    QReadLocker locker(&m_lock);
    if (overflow != nullptr)
        *overflow = false;

    if (!m_events.isEmpty() && lastSeq < (m_events.first().seq - 1)) {
        if (overflow != nullptr)
            *overflow = true;
        return QVariantList();
    }

    QVariantList result;
    for (int index = 0; index < m_events.length(); ++index) {
        const EventRecord &record = m_events.at(index);
        if (record.seq <= lastSeq)
            continue;

        if ((record.command == S_COMMAND_GET_CARD || record.command == S_COMMAND_LOSE_CARD)) {
            bool ok = false;
            int moveId = spectateMoveId(record.payload, &ok);
            if (ok) {
                EventRecord chosen = record;
                EventRecord mergedTail = record;
                int chosenScore = spectateMoveScore(record, targetName);
                QVariantList mergedPayload = record.payload.toList();
                int nextIndex = index + 1;
                while (nextIndex < m_events.length()) {
                    const EventRecord &candidate = m_events.at(nextIndex);
                    if (candidate.command != record.command)
                        break;
                    bool sameMoveOk = false;
                    int candidateMoveId = spectateMoveId(candidate.payload, &sameMoveOk);
                    if (!sameMoveOk || candidateMoveId != moveId)
                        break;

                    int candidateScore = spectateMoveScore(candidate, targetName);
                    mergedPayload = spectateMergeMovePayload(mergedPayload, candidate.payload.toList(), chosen, candidate, targetName);
                    if (candidateScore > chosenScore) {
                        chosen = candidate;
                        chosenScore = candidateScore;
                    }
                    mergedTail = candidate;
                    ++nextIndex;
                }

                JsonObject eventObj;
                eventObj["seq"] = mergedTail.seq;
                eventObj["snapshotVersion"] = mergedTail.snapshotVersion;
                eventObj["command"] = chosen.command;
                eventObj["payload"] = QVariant(mergedPayload);
                result << QVariant(eventObj);
                index = nextIndex - 1;
                continue;
            }
        }

        if (!record.recipientName.isEmpty() && record.recipientName != targetName)
            continue;

        JsonObject eventObj;
        eventObj["seq"] = record.seq;
        eventObj["snapshotVersion"] = record.snapshotVersion;
        eventObj["command"] = record.command;
        eventObj["payload"] = record.payload;
        result << QVariant(eventObj);
    }

    return result;
}

int SpectateProjection::lastEventSeq() const
{
    QReadLocker locker(&m_lock);
    return m_eventSeq;
}

int SpectateProjection::snapshotVersion() const
{
    QReadLocker locker(&m_lock);
    return m_snapshotVersion;
}

bool SpectateProjection::isAlive(const QString &targetName) const
{
    QReadLocker locker(&m_lock);
    return m_aliveByPlayer.value(targetName, false);
}

QString SpectateProjection::nextAliveTarget(const QString &currentTarget) const
{
    QReadLocker locker(&m_lock);
    foreach (const QString &targetName, m_aliveOrder) {
        if (targetName != currentTarget)
            return targetName;
    }
    return QString();
}

void SpectateProjection::captureStateLocked(const Room *room)
{
    if (room == nullptr)
        return;

    ++m_snapshotVersion;
    m_baseSnapshot.clear();
    m_privateStateByPlayer.clear();
    m_aliveByPlayer.clear();
    m_aliveOrder.clear();

    JsonObject snapshot;
    snapshot["roomId"] = room->getId();
    snapshot["mode"] = room->getMode();

    QVariantList playerStates;
    QList<ServerPlayer *> roomPlayers = room->getPlayers();
    foreach (ServerPlayer *player, roomPlayers) {
        if (player == nullptr)
            continue;

        JsonObject playerObj;
        playerObj["objectName"] = player->objectName();
        playerObj["screenName"] = player->screenName();
        playerObj["generalName"] = player->getGeneralName();
        playerObj["general2Name"] = player->getGeneral2Name();
        playerObj["kingdom"] = player->getKingdom();
        playerObj["role"] = player->getRole();
        playerObj["roleShown"] = player->hasShownRole();
        playerObj["hp"] = player->getHp();
        playerObj["maxHp"] = player->getMaxHp();
        playerObj["renHp"] = player->getRenHp();
        playerObj["lingHp"] = player->getLingHp();
        playerObj["chaoren"] = player->getChaoren();
        playerObj["alive"] = player->isAlive();
        playerObj["removed"] = player->isRemoved();
        playerObj["seat"] = player->getSeat();
        playerObj["phase"] = static_cast<int>(player->getPhase());
        playerObj["faceUp"] = player->faceUp();
        playerObj["chained"] = player->isChained();
        playerObj["handcardNum"] = player->getHandcardNum();
        playerObj["generalShowed"] = player->hasShownGeneral();
        playerObj["general2Showed"] = player->hasShownGeneral2();

        JsonArray equips;
        if (player->getWeapon() != nullptr) equips << player->getWeapon()->getId();
        if (player->getArmor() != nullptr) equips << player->getArmor()->getId();
        if (player->getDefensiveHorse() != nullptr) equips << player->getDefensiveHorse()->getId();
        if (player->getOffensiveHorse() != nullptr) equips << player->getOffensiveHorse()->getId();
        if (player->getTreasure() != nullptr) equips << player->getTreasure()->getId();
        playerObj["equips"] = QVariant(equips);
        playerObj["judgeArea"] = JsonUtils::toJsonArray(player->getJudgingAreaID());
        playerObj["shownHandcards"] = JsonUtils::toJsonArray(player->getShownHandcards());
        playerObj["brokenEquips"] = JsonUtils::toJsonArray(player->getBrokenEquips());

        JsonObject playerPilesObj;
        foreach (const QString &pileName, player->getPileNames())
            playerPilesObj[pileName] = JsonUtils::toJsonArray(player->getPile(pileName));
        playerObj["piles"] = QVariant(playerPilesObj);

        JsonObject pileOpenObj;
        foreach (const QString &pileName, player->getPileNames()) {
            QStringList openPlayers;
            foreach (ServerPlayer *viewer, roomPlayers) {
                if (player->pileOpen(pileName, viewer->objectName()))
                    openPlayers << viewer->objectName();
            }
            if (!openPlayers.isEmpty())
                pileOpenObj[pileName] = JsonUtils::toJsonArray(openPlayers);
        }
        playerObj["pileOpen"] = QVariant(pileOpenObj);

        JsonObject marks;
        QMap<QString, int> markMap = player->getMarkMap();
        for (auto it = markMap.constBegin(); it != markMap.constEnd(); ++it) {
            if (it.value() != 0)
                marks[it.key()] = it.value();
        }
        playerObj["marks"] = QVariant(marks);
        playerObj["acquiredHeadSkills"] = JsonUtils::toJsonArray(player->getAcquiredHeadSkills().toList());
        playerObj["acquiredDeputySkills"] = JsonUtils::toJsonArray(player->getAcquiredDeputySkills().toList());
        playerObj["hiddenGenerals"] = JsonUtils::toJsonArray(player->getHiddenGenerals());
        playerObj["shownHiddenGeneral"] = player->getShownHiddenGeneral();
        playerObj["skillInvalid"] = JsonUtils::toJsonArray(player->getSkillInvalidityList());
        playerObj["disableShow"] = JsonUtils::toJsonArray(player->getDisableShowList());
        playerObj["flags"] = JsonUtils::toJsonArray(player->getFlagList());

        playerStates << QVariant(playerObj);
        m_aliveByPlayer[player->objectName()] = player->isAlive();
        if (player->isAlive())
            m_aliveOrder << player->objectName();

        QVariantMap privateState;
        privateState["handCards"] = JsonUtils::toJsonArray(player->handCards());
        privateState["piles"] = QVariant(playerPilesObj);

        JsonArray modifiedCards;
        QSet<int> seenCardIds;
        auto appendModified = [&](int cardId) {
            if (cardId < 0 || seenCardIds.contains(cardId))
                return;
            seenCardIds.insert(cardId);
            Card *card = room->getCard(cardId);
            if (card != nullptr && card->isModified()) {
                JsonArray cardInfo;
                cardInfo << cardId << card->getSuit() << card->getNumber()
                         << card->getClassName() << card->getSkillName()
                         << card->objectName() << JsonUtils::toJsonArray(card->getFlags());
                modifiedCards << QVariant(cardInfo);
            }
        };

        foreach (int cardId, player->handCards())
            appendModified(cardId);
        foreach (const QString &pileName, player->getPileNames()) {
            foreach (int cardId, player->getPile(pileName))
                appendModified(cardId);
        }
        foreach (ServerPlayer *otherPlayer, roomPlayers) {
            foreach (const Card *equip, otherPlayer->getEquips()) {
                if (equip != nullptr)
                    appendModified(equip->getEffectiveId());
            }
            foreach (int cardId, otherPlayer->getJudgingAreaID())
                appendModified(cardId);
        }
        privateState["modifiedCards"] = QVariant(modifiedCards);
        m_privateStateByPlayer[player->objectName()] = privateState;
    }

    snapshot["players"] = QVariant(playerStates);
    m_baseSnapshot = snapshot;

    std::sort(m_aliveOrder.begin(), m_aliveOrder.end(), [&](const QString &left, const QString &right) {
        ServerPlayer *leftPlayer = room->findPlayerByObjectName(left, true);
        ServerPlayer *rightPlayer = room->findPlayerByObjectName(right, true);
        if (leftPlayer == nullptr || rightPlayer == nullptr)
            return left < right;
        return leftPlayer->getSeat() < rightPlayer->getSeat();
    });
}
