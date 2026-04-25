#include "skill.h"
#include "client.h"
#include "engine.h"
#include "player.h"
#include "room.h"
#include "settings.h"
#include "standard.h"
#include "util.h"

#include <QFile>

namespace
{
QList<int> submittedSubcardIds(const Card *card)
{
    if (card == nullptr)
        return QList<int>();
    if (card->isVirtualCard())
        return card->getSubcards();
    return QList<int>() << card->getEffectiveId();
}

bool sameValidationCard(const Card *expected, const Card *actual)
{
    return expected != nullptr && actual != nullptr && expected->getClassName() == actual->getClassName() && expected->getSkillName(false) == actual->getSkillName(false)
        && expected->canRecast() == actual->canRecast() && expected->toString() == actual->toString();
}

void disposeValidationCard(const Card *card)
{
    if (card != nullptr && card->parent() == nullptr)
        delete card;
}

bool skillNameMatchesGrant(const QString &skillName, const QStringList &allowedSkillNames)
{
    if (allowedSkillNames.isEmpty() || skillName.isEmpty())
        return true;
    if (allowedSkillNames.contains(skillName))
        return true;
    if (skillName.startsWith("_") && allowedSkillNames.contains(skillName.mid(1)))
        return true;
    return false;
}

void appendCardId(QList<int> *ids, int id)
{
    if (ids != nullptr && id >= 0 && !ids->contains(id))
        *ids << id;
}

void appendCardIds(QList<int> *ids, const QList<int> &cardIds)
{
    foreach (int id, cardIds)
        appendCardId(ids, id);
}

void appendCardIdsFromProperty(QList<int> *ids, const Player *player, const QString &propertyName)
{
    if (player == nullptr || propertyName.isEmpty())
        return;
    appendCardIds(ids, StringList2IntList(player->property(propertyName.toUtf8().constData()).toString().split(QStringLiteral("+"))));
}

void appendExplicitExpandPileIds(QList<int> *ids, const Player *player, const ClientActionContext &ctx, const QString &expandPile, const QString &skillName)
{
    if (ids == nullptr || player == nullptr || expandPile.isEmpty())
        return;

    if (expandPile == QStringLiteral("#judging_area")) {
        appendCardIds(ids, player->getJudgingAreaID());
    } else if (expandPile.startsWith(QStringLiteral("*"))) {
        appendCardIdsFromProperty(ids, player, expandPile.mid(1));
    } else if (expandPile.startsWith(QStringLiteral("+"))) {
        if (ctx.player == nullptr)
            return;
        Room *room = ctx.player->getRoom();
        ServerPlayer *target = room != nullptr ? room->findPlayerByObjectName(player->property(skillName.toUtf8().constData()).toString()) : nullptr;
        if (target != nullptr)
            appendCardIds(ids, target->getPile(expandPile.mid(1)));
    } else if (expandPile.startsWith(QStringLiteral("%"))) {
        foreach (const Player *other, player->getAliveSiblings()) {
            if (expandPile == QStringLiteral("%shown_card"))
                appendCardIds(ids, other->getShownHandcards());
            else
                appendCardIds(ids, other->getPile(expandPile.mid(1)));
        }
    } else {
        appendCardIds(ids, player->getPile(expandPile));
    }
}

bool usesClientSideViewAsState(const QString &skillName)
{
    static const QStringList unsafeSkills = QStringList()
        << "fsu0413gainian"
        << "toushi"
        << "modian"
        << "zhanzhen"
        << "qiji"
        << "lingbai"
        << "yidan"
        << "xianshi"
        << "xiezou"
        << "chuangshi"
        << "nianli"
        << "yucan"
        << "xihua"
        << "liuneng";
    return unsafeSkills.contains(skillName);
}

bool matchesGeneratedCard(const Card *expected, const Card *actual)
{
    const bool matched = sameValidationCard(expected, actual);
    disposeValidationCard(expected);
    return matched;
}

const Card *attachSkillNameToDummyCard(const Card *card, const QString &skillName)
{
    if (card != nullptr && card->isKindOf("DummyCard") && card->getSkillName(false).isEmpty() && !skillName.isEmpty())
        const_cast<Card *>(card)->setSkillName(skillName);
    return card;
}

bool declaredCardNameMatches(const JsonObject &extra, const Card *card)
{
    if (extra.isEmpty())
        return true;
    if (card == nullptr || extra.keys() != QStringList(QStringLiteral("declaredCardName")))
        return false;
    const QString declaredCardName = extra.value(QStringLiteral("declaredCardName")).toString();
    return declaredCardName.isEmpty() || card->objectName() == declaredCardName;
}

QString declaredCardNameFromExtra(const JsonObject &extra)
{
    if (extra.keys() != QStringList(QStringLiteral("declaredCardName")))
        return QString();
    return extra.value(QStringLiteral("declaredCardName")).toString();
}

void appendPropertyCardNames(QStringList *names, const Player *player, const QString &propertyName)
{
    if (names == nullptr || player == nullptr || propertyName.isEmpty())
        return;

    const QString value = player->property(propertyName.toUtf8().constData()).toString();
    foreach (const QString &name, value.split(QStringLiteral("+"), QString::SkipEmptyParts)) {
        if (!names->contains(name))
            *names << name;
    }
}

}

Skill::Skill(const QString &name, const QString &showType)
    : frequent(false)
    , compulsory(false)
    , limited(false)
    , wake(false)
    , eternal(false)
    , attached_lord_skill(false)
    , show_type(showType)
    , equip_skill(false)
{
    static QChar lord_symbol('$');

    if (name.endsWith(lord_symbol)) {
        QString copy = name;
        copy.remove(lord_symbol);
        setObjectName(copy);
        lord_skill = true;
    } else {
        setObjectName(name);
        lord_skill = false;
    }
}

bool Skill::isLordSkill() const
{
    return lord_skill;
}

bool Skill::isAttachedLordSkill() const
{
    return attached_lord_skill;
}

bool Skill::isFrequent() const
{
    return frequent;
}

bool Skill::isCompulsory() const
{
    return compulsory;
}

bool Skill::isLimited() const
{
    return limited;
}

bool Skill::isWake() const
{
    return wake;
}

bool Skill::isEternal() const
{
    return eternal;
}

bool Skill::playerRevivable(const Player * /*player*/, const Room * /*room*/) const
{
    return false;
}

bool Skill::shouldBeVisible(const Player *Self) const
{
    return Self != nullptr;
}

QString Skill::getDescription(bool yellow, bool addHegemony) const
{
    bool normal_game = ServerInfo.DuringGame && isNormalGameMode(ServerInfo.GameMode);
    QString name = QString("%1%2").arg(objectName(), (normal_game ? "_p" : ""));
    //bool addHegemony = isHegemony && !objectName().endsWith("_hegemony");
    QString des_src = Sanguosha->translate(":" + name, addHegemony);
    if (normal_game && des_src.startsWith(":"))
        des_src = Sanguosha->translate(":" + objectName());
    if (des_src.startsWith(":"))
        return {};
    QString desc = QString("<font color=%1>%2</font>").arg((yellow ? "#FFFF33" : "#FF0080"), des_src);
    //if (isHegemonyGameMode(ServerInfo.GameMode) && !canPreshow())
    if (addHegemony && !canPreshow())
        desc.prepend(QString("<font color=gray>(%1)</font><br/>").arg(tr("this skill cannot preshow")));
    return desc;
}

QString Skill::getNotice(int index) const
{
    if (index == -1)
        return Sanguosha->translate("~" + objectName());

    return Sanguosha->translate(QString("~%1%2").arg(objectName()).arg(index));
}

bool Skill::isVisible() const
{
    return !objectName().startsWith("#");
}

int Skill::getEffectIndex(const ServerPlayer * /*unused*/, const Card * /*unused*/) const
{
    return -1;
}

void Skill::initMediaSource()
{
    sources.clear();
    for (int i = 1;; i++) {
        QString effect_file = QString("audio/skill/%1%2.ogg").arg(objectName(), QString::number(i));
        if (QFile::exists(effect_file))
            sources << effect_file;
        else
            break;
    }

    if (sources.isEmpty()) {
        QString effect_file = QString("audio/skill/%1.ogg").arg(objectName());
        if (QFile::exists(effect_file))
            sources << effect_file;
    }
}

void Skill::playAudioEffect(int index) const
{
    if (!sources.isEmpty()) {
        if (index == -1)
            index = qrand() % sources.length();
        else
            index--;

        // check length
        QString filename;
        if (index >= 0 && index < sources.length())
            filename = sources.at(index);
        else if (index >= sources.length()) {
            while (index >= sources.length())
                index -= sources.length();
            filename = sources.at(index);
        } else
            filename = sources.first();

        Sanguosha->playAudioEffect(filename);
        if (ClientInstance != nullptr)
            ClientInstance->setLines(filename);
    }
}

QString Skill::getShowType() const
{
    return show_type;
}

void Skill::setFrequent(bool frequent)
{
    this->frequent = frequent;
}

void Skill::setCompulsory(bool compulsory)
{
    this->compulsory = compulsory;
}

void Skill::setLimited(bool limited)
{
    this->limited = limited;
}

void Skill::setWake(bool wake)
{
    this->wake = wake;
}

void Skill::setEternal(bool eternal)
{
    this->eternal = eternal;
}

QString Skill::getLimitMark() const
{
    return limit_mark;
}

QString Skill::getRelatedMark() const
{
    return related_mark;
}

QString Skill::getRelatedPileName() const
{
    return related_pile;
}

QStringList Skill::getSources() const
{
    return sources;
}

QDialog *Skill::getDialog() const
{
    return nullptr;
}

bool Skill::canPreshow() const
{
    if (inherits("TriggerSkill")) {
        const TriggerSkill *triskill = qobject_cast<const TriggerSkill *>(this);
        return triskill->getViewAsSkill() == nullptr;
    }

    return false;
}

bool Skill::relateToPlace(bool head) const
{
    if (head)
        return relate_to_place == "head";
    else
        return relate_to_place == "deputy";
    return false;
}

bool Skill::isEquipSkill() const
{
    return equip_skill;
}

ViewAsSkill::ViewAsSkill(const QString &name)
    : Skill(name, "viewas")
    , response_or_use(false)
{
}

bool ViewAsSkill::isAvailable(const Player *invoker, CardUseStruct::CardUseReason reason, const QString &pattern) const
{
    if (invoker == nullptr)
        return false;

    if (!invoker->hasSkill(objectName()) && !invoker->hasLordSkill(objectName())
        && invoker->getMark("ViewAsSkill_" + objectName() + "Effect") == 0 // For skills like Shuangxiong(ViewAsSkill effect remains even if the player has lost the skill)
        && !invoker->hasFlag("RoomScene_" + objectName() + "TempUse")) // for RoomScene Temp Use
        return false;
    switch (reason) {
    case CardUseStruct::CARD_USE_REASON_PLAY:
        return isEnabledAtPlay(invoker);
    case CardUseStruct::CARD_USE_REASON_RESPONSE:
    case CardUseStruct::CARD_USE_REASON_RESPONSE_USE:
        if (pattern == "nullification") {
            const ServerPlayer *serverPlayer = qobject_cast<const ServerPlayer *>(invoker);
            if (serverPlayer != nullptr && isEnabledAtNullification(serverPlayer))
                return true;
        }
        return isEnabledAtResponse(invoker, pattern);
    case CardUseStruct::CARD_USE_REASON_UNKNOWN:
        if (pattern.startsWith(QStringLiteral("@@")))
            return isEnabledAtResponse(invoker, pattern);
        break;
    default:
        return false;
    }
    return false;
}

QStringList ViewAsSkill::producedCardClasses() const
{
    return QStringList();
}

CardUseGrant ViewAsSkill::makeGrant(const Player *player, const ClientActionContext &ctx) const
{
    CardUseGrant grant;
    if (player == nullptr || ctx.player == nullptr || player != ctx.player)
        return grant;

    const bool isMethodNoneRequest = ctx.method == Card::MethodNone && ctx.pattern.startsWith("@@") && isEnabledAtResponse(player, ctx.pattern);
    const bool isRequestScopedResponse = ctx.requestSkillNames.contains(objectName()) && ctx.pattern.startsWith(QStringLiteral("@@")) && isEnabledAtResponse(player, ctx.pattern);
    if (!isMethodNoneRequest && !isRequestScopedResponse && !isAvailable(player, ctx.reason, ctx.pattern))
        return grant;

    grant.valid = true;
    grant.player = ctx.player;
    grant.sourceSkill = objectName();
    grant.allowedSkillNames << objectName();
    grant.allowedCardClasses = producedCardClasses();
    grant.reason = ctx.reason;
    grant.method = ctx.method;
    grant.pattern = ctx.pattern;
    grant.requestSerial = -1;
    grant.allowNoSubcards = inherits("ZeroCardViewAsSkill");
    grant.allowOtherPlayersCards = false;
    grant.allowVirtualCard = true;
    grant.allowRealCard = true;
    grant.requireViewAsValidation = true;
    grant.allowedPlaces << Player::PlaceHand << Player::PlaceEquip;
    appendCardIds(&grant.allowedSpecialCardIds, player->getHandPile());
    appendExplicitExpandPileIds(&grant.allowedSpecialCardIds, player, ctx, expand_pile, objectName());

    if (player->getAcquiredSkills().contains(objectName()))
        grant.sourceKind = CardUseGrant::AcquiredSkill;
    else if (player->hasEquipSkill(objectName()))
        grant.sourceKind = CardUseGrant::EquipSkill;
    else if (player->ownSkill(objectName()))
        grant.sourceKind = CardUseGrant::OwnedSkill;
    else
        grant.sourceKind = CardUseGrant::HiddenGeneralSkill;

    return grant;
}

bool ViewAsSkill::isCardUseValid(const CardUseStruct &cardUse, const CardUseGrant &grant, const ClientActionContext &ctx) const
{
    if (cardUse.card == nullptr)
        return false;

    if (!grant.allowedCardClasses.isEmpty() && !grant.allowedCardClasses.contains(cardUse.card->getClassName()))
        return false;

    const QString skillName = cardUse.card->getSkillName();
    if (!skillNameMatchesGrant(skillName, grant.allowedSkillNames))
        return false;

    QList<const Card *> subcards;
    QList<int> seenIds;
    const QList<int> ids = submittedSubcardIds(cardUse.card);
    if (ids.isEmpty())
        return (grant.allowNoSubcards || ctx.serverBuiltCard) && isGeneratedCardValid(subcards, cardUse.card, grant, ctx);

    foreach (int id, ids) {
        if (seenIds.contains(id))
            return false;
        const Card *card = Sanguosha->getCard(id);
        if (card == nullptr)
            return false;
        if (!isSubcardSelectionValid(subcards, card, grant, ctx))
            return false;
        seenIds << id;
        subcards << card;
    }
    return isGeneratedCardValid(subcards, cardUse.card, grant, ctx);
}

const Card *ViewAsSkill::buildServerCard(const QList<const Card *> &selected, const ActionRequestContext &ctx, const JsonObject &extra) const
{
    Q_UNUSED(selected)
    Q_UNUSED(ctx)
    Q_UNUSED(extra)
    return nullptr;
}

Card *ViewAsSkill::prepareViewAsCard(Card *card, const QList<const Card *> &subcards, const QString &skillName, const QString &showSkill) const
{
    if (card == nullptr)
        return nullptr;
    card->addSubcards(subcards);
    if (!skillName.isEmpty())
        card->setSkillName(skillName);
    if (!showSkill.isEmpty())
        card->setShowSkill(showSkill);
    return card;
}

Card *ViewAsSkill::cloneViewAsCard(const QString &cardName, const QList<const Card *> &subcards, const QString &skillName, const QString &showSkill, Card::Suit suit,
    int number, bool canRecast) const
{
    Card *card = Sanguosha->cloneCard(cardName, suit, number);
    if (card == nullptr)
        return nullptr;
    prepareViewAsCard(card, subcards, skillName, showSkill);
    if (!canRecast)
        card->setCanRecast(false);
    return card;
}

bool ViewAsSkill::acceptsDeclaredCardName(const JsonObject &extra, const QString &cardName) const
{
    return extra.isEmpty() || (extra.keys() == QStringList(QStringLiteral("declaredCardName")) && extra.value(QStringLiteral("declaredCardName")).toString() == cardName);
}

bool ViewAsSkill::isDeclaredCardNameAccepted(const QString &cardName, const QList<const Card *> &selected, const ActionRequestContext &ctx) const
{
    Q_UNUSED(selected)
    if (ctx.player == nullptr || cardName.isEmpty())
        return false;

    QStringList names;
    appendPropertyCardNames(&names, ctx.player, objectName() + QStringLiteral("_card"));
    appendPropertyCardNames(&names, ctx.player, objectName());
    return names.contains(cardName);
}

bool ViewAsSkill::isDeclaredCardUseValid(const Card *card, const ActionRequestContext &ctx) const
{
    if (card == nullptr || ctx.player == nullptr)
        return false;

    if (ctx.reason == CardUseStruct::CARD_USE_REASON_PLAY)
        return card->isAvailable(ctx.player);

    if (!ctx.pattern.isEmpty() && !ctx.pattern.startsWith(QStringLiteral("@@"))) {
        const QString normalizedPattern = normalizeCardUsePattern(ctx.pattern);
        const CardPattern *pattern = Sanguosha->getPattern(normalizedPattern);
        if (pattern == nullptr || !pattern->match(ctx.player, card))
            return false;
    }

    return !ctx.player->isCardLimited(card, ctx.method);
}

Card *ViewAsSkill::buildDeclaredCardForServer(
    const QList<const Card *> &selected, const ActionRequestContext &ctx, const JsonObject &extra, bool inheritSelectedCardSuitNumber, const QString &showSkill) const
{
    const QString cardName = declaredCardNameFromExtra(extra);
    if (cardName.isEmpty() || !isDeclaredCardNameAccepted(cardName, selected, ctx))
        return nullptr;

    Card::Suit suit = Card::SuitToBeDecided;
    int number = -1;
    if (inheritSelectedCardSuitNumber && selected.length() == 1 && selected.first() != nullptr) {
        suit = selected.first()->getSuit();
        number = selected.first()->getNumber();
    }

    Card *card = cloneViewAsCard(cardName, selected, objectName(), showSkill, suit, number);
    if (!isDeclaredCardUseValid(card, ctx)) {
        disposeValidationCard(card);
        return nullptr;
    }

    return card;
}

bool ViewAsSkill::isGeneratedCardValid(const QList<const Card *> &selected, const Card *to_validate, const CardUseGrant &grant, const ClientActionContext &ctx) const
{
    Q_UNUSED(selected)
    Q_UNUSED(to_validate)
    Q_UNUSED(grant)
    Q_UNUSED(ctx)
    return true;
}

bool ViewAsSkill::isSubcardSelectionValid(const QList<const Card *> &selected, const Card *to_select, const CardUseGrant &grant, const ClientActionContext &ctx) const
{
    Q_UNUSED(selected)
    Q_UNUSED(grant)
    if (to_select == nullptr || to_select->hasFlag("using"))
        return false;
    if (!expand_pile.isEmpty() && !expand_pile.startsWith("*") && !expand_pile.startsWith("#") && !expand_pile.startsWith("+")
        && (ctx.player == nullptr || !ctx.player->getPile(expand_pile).contains(to_select->getEffectiveId())))
        return false;
    return true;
}

bool ViewAsSkill::isEnabledAtPlay(const Player * /*unused*/) const
{
    return response_pattern.isEmpty();
}

bool ViewAsSkill::isEnabledAtResponse(const Player * /*unused*/, const QString &pattern) const
{
    if (!response_pattern.isEmpty())
        return pattern == response_pattern;
    return false;
}

bool ViewAsSkill::isEnabledAtNullification(const ServerPlayer * /*unused*/) const
{
    return false;
}

const ViewAsSkill *ViewAsSkill::parseViewAsSkill(const Skill *skill)
{
    if (skill == nullptr)
        return nullptr;
    if (skill->inherits("ViewAsSkill")) {
        const ViewAsSkill *view_as_skill = qobject_cast<const ViewAsSkill *>(skill);
        return view_as_skill;
    }
    if (skill->inherits("TriggerSkill")) {
        const TriggerSkill *trigger_skill = qobject_cast<const TriggerSkill *>(skill);
        Q_ASSERT(trigger_skill != nullptr);
        const ViewAsSkill *view_as_skill = trigger_skill->getViewAsSkill();
        if (view_as_skill != nullptr)
            return view_as_skill;
    }
    if (skill->inherits("DistanceSkill")) {
        const DistanceSkill *trigger_skill = qobject_cast<const DistanceSkill *>(skill);
        Q_ASSERT(trigger_skill != nullptr);
        const ViewAsSkill *view_as_skill = trigger_skill->getViewAsSkill();
        if (view_as_skill != nullptr)
            return view_as_skill;
    }
    if (skill->inherits("AttackRangeSkill")) {
        const AttackRangeSkill *trigger_skill = qobject_cast<const AttackRangeSkill *>(skill);
        Q_ASSERT(trigger_skill != nullptr);
        const ViewAsSkill *view_as_skill = trigger_skill->getViewAsSkill();
        if (view_as_skill != nullptr)
            return view_as_skill;
    }
    if (skill->inherits("MaxCardsSkill")) {
        const MaxCardsSkill *trigger_skill = qobject_cast<const MaxCardsSkill *>(skill);
        Q_ASSERT(trigger_skill != nullptr);
        const ViewAsSkill *view_as_skill = trigger_skill->getViewAsSkill();
        if (view_as_skill != nullptr)
            return view_as_skill;
    }
    return nullptr;
}

QString ViewAsSkill::getExpandPile() const
{
    return expand_pile;
}

ZeroCardViewAsSkill::ZeroCardViewAsSkill(const QString &name)
    : ViewAsSkill(name)
{
}

const Card *ZeroCardViewAsSkill::viewAs(const QList<const Card *> &cards) const
{
    if (cards.isEmpty())
        return attachSkillNameToDummyCard(viewAs(), objectName());
    else
        return nullptr;
}

bool ZeroCardViewAsSkill::isCardUseValid(const CardUseStruct &cardUse, const CardUseGrant &grant, const ClientActionContext &ctx) const
{
    if (cardUse.card == nullptr || cardUse.card->subcardsLength() != 0)
        return false;

    return ViewAsSkill::isCardUseValid(cardUse, grant, ctx);
}

const Card *ZeroCardViewAsSkill::buildServerCard(const QList<const Card *> &selected, const ActionRequestContext &ctx, const JsonObject &extra) const
{
    if (!selected.isEmpty())
        return nullptr;
    if (usesClientSideViewAsState(objectName()))
        return buildDeclaredCardForServer(selected, ctx, extra, false);
    const Card *card = attachSkillNameToDummyCard(viewAs(), objectName());
    if (!declaredCardNameMatches(extra, card)) {
        disposeValidationCard(card);
        return nullptr;
    }
    return card;
}

bool ZeroCardViewAsSkill::isGeneratedCardValid(const QList<const Card *> &selected, const Card *to_validate, const CardUseGrant &grant, const ClientActionContext &ctx) const
{
    if (!ViewAsSkill::isGeneratedCardValid(selected, to_validate, grant, ctx))
        return false;
    if (to_validate == nullptr)
        return false;
    if (ctx.serverBuiltCard && usesClientSideViewAsState(objectName()))
        return true;

    return matchesGeneratedCard(viewAs(), to_validate);
}

bool ZeroCardViewAsSkill::viewFilter(const QList<const Card *> & /*selected*/, const Card * /*to_select*/) const
{
    return false;
}

OneCardViewAsSkill::OneCardViewAsSkill(const QString &name)
    : ViewAsSkill(name)
{
}

bool OneCardViewAsSkill::viewFilter(const QList<const Card *> &selected, const Card *to_select) const
{
    return selected.isEmpty() && !to_select->hasFlag("using") && viewFilter(to_select);
}

bool OneCardViewAsSkill::viewFilter(const Card *to_select) const
{
    if (!inherits("FilterSkill") && !filter_pattern.isEmpty()) {
        QString pat = filter_pattern;
        if (pat.endsWith("!")) {
            if (Self->isJilei(to_select))
                return false;
            pat.chop(1);
        } else if (response_or_use && pat.contains("hand")) {
            pat.replace("hand", "hand,wooden_ox");
            //pat.replace("hand", handlist.join(","));
        }
        ExpPattern pattern(pat);
        return pattern.match(Self, to_select);
    }
    return false;
}

const Card *OneCardViewAsSkill::viewAs(const QList<const Card *> &cards) const
{
    if (cards.length() != 1)
        return nullptr;
    else
        return attachSkillNameToDummyCard(viewAs(cards.first()), objectName());
}

bool OneCardViewAsSkill::isCardUseValid(const CardUseStruct &cardUse, const CardUseGrant &grant, const ClientActionContext &ctx) const
{
    if (cardUse.card == nullptr)
        return false;
    if (!grant.allowedCardClasses.isEmpty() && !grant.allowedCardClasses.contains(cardUse.card->getClassName()))
        return false;

    const QString skillName = cardUse.card->getSkillName();
    const bool skilllessDummy = cardUse.card->isKindOf("DummyCard") && skillName.isEmpty();
    if (!skilllessDummy && !skillNameMatchesGrant(skillName, grant.allowedSkillNames))
        return false;

    QList<const Card *> subcards;
    QList<int> seenIds;
    const QList<int> ids = submittedSubcardIds(cardUse.card);
    if (ids.length() != 1)
        return false;

    foreach (int id, ids) {
        if (seenIds.contains(id))
            return false;
        const Card *card = Sanguosha->getCard(id);
        if (card == nullptr || !isSubcardSelectionValid(subcards, card, grant, ctx))
            return false;
        seenIds << id;
        subcards << card;
    }

    if (skilllessDummy) {
        const Card *expected = viewAs(subcards.first());
        const bool matched = sameValidationCard(expected, cardUse.card);
        disposeValidationCard(expected);
        if (!matched)
            return false;
    }

    return isGeneratedCardValid(subcards, cardUse.card, grant, ctx);
}

const Card *OneCardViewAsSkill::buildServerCard(const QList<const Card *> &selected, const ActionRequestContext &ctx, const JsonObject &extra) const
{
    if (selected.length() != 1)
        return nullptr;
    if (!isSubcardSelectionValid(QList<const Card *>(), selected.first(), CardUseGrant(), ctx))
        return nullptr;
    if (usesClientSideViewAsState(objectName()))
        return buildDeclaredCardForServer(selected, ctx, extra, true);
    const Card *card = attachSkillNameToDummyCard(viewAs(selected.first()), objectName());
    if (!declaredCardNameMatches(extra, card)) {
        disposeValidationCard(card);
        return nullptr;
    }
    return card;
}

bool OneCardViewAsSkill::isGeneratedCardValid(const QList<const Card *> &selected, const Card *to_validate, const CardUseGrant &grant, const ClientActionContext &ctx) const
{
    if (!ViewAsSkill::isGeneratedCardValid(selected, to_validate, grant, ctx))
        return false;
    if (to_validate == nullptr || selected.length() != 1)
        return false;
    if (ctx.serverBuiltCard && usesClientSideViewAsState(objectName()))
        return true;
    return matchesGeneratedCard(viewAs(selected.first()), to_validate);
}

bool OneCardViewAsSkill::isSubcardSelectionValid(const QList<const Card *> &selected, const Card *to_select, const CardUseGrant &grant, const ClientActionContext &ctx) const
{
    Q_UNUSED(grant)
    if (to_select == nullptr || !selected.isEmpty() || to_select->hasFlag("using"))
        return false;
    if (!filter_pattern.isEmpty() && !inherits("FilterSkill")) {
        QString pattern = filter_pattern;
        if (pattern.endsWith("!")) {
            if (ctx.player == nullptr || ctx.player->isJilei(to_select))
                return false;
            pattern.chop(1);
        } else if (response_or_use && pattern.contains("hand"))
            pattern.replace("hand", "hand,wooden_ox");

        ExpPattern exp(pattern);
        if (!exp.match(ctx.player, to_select))
            return false;
    }
    return serverViewFilter(to_select, ctx);
}

bool OneCardViewAsSkill::serverViewFilter(const Card *to_select, const ClientActionContext &ctx) const
{
    if (ctx.player == nullptr || to_select == nullptr)
        return false;

    if (!expand_pile.isEmpty() && !expand_pile.startsWith("*") && !expand_pile.startsWith("#") && !expand_pile.startsWith("+")
        && !ctx.player->getPile(expand_pile).contains(to_select->getEffectiveId()))
        return false;

    return true;
}

FilterSkill::FilterSkill(const QString &name)
    : OneCardViewAsSkill(name)
{
    setCompulsory();
    show_type = "static";
}

TriggerSkill::TriggerSkill(const QString &name)
    : Skill(name)
    , view_as_skill(nullptr)
    , global(false)
{
}

const ViewAsSkill *TriggerSkill::getViewAsSkill() const
{
    return view_as_skill;
}

QList<TriggerEvent> TriggerSkill::getTriggerEvents() const
{
    return events;
}

int TriggerSkill::getPriority() const
{
    return 2;
}

void TriggerSkill::record(TriggerEvent /*unused*/, Room * /*unused*/, QVariant & /*unused*/) const
{
}

QList<SkillInvokeDetail> TriggerSkill::triggerable(TriggerEvent /*unused*/, const Room * /*unused*/, const QVariant & /*unused*/) const
{
    return {};
}

bool TriggerSkill::cost(TriggerEvent /*unused*/, Room *room, QSharedPointer<SkillInvokeDetail> invoke, QVariant &data) const
{
    if (invoke->isCompulsory) { //for hegemony_mode or reimu_god
        if (invoke->owner == nullptr || invoke->owner != invoke->invoker || isEternal())
            return true;
        if (invoke->invoker != nullptr) {
            if (!invoke->invoker->hasSkill(this, true))
                return true;
            if (!invoke->invoker->hasShownSkill(this))
                return invoke->invoker->askForSkillInvoke(this, data);

            if (invoke->showHidden) {
                room->notifySkillInvoked(invoke->invoker, objectName());
                room->sendLog("#TriggerSkill", invoke->invoker, objectName());
            }
        }
        return true;
    } else {
        if (invoke->invoker != nullptr) {
            //for ai
            invoke->invoker->tag[objectName()] = data;
            QVariant notify_data = data;
            if (invoke->preferredTarget != nullptr)
                notify_data = QVariant::fromValue(invoke->preferredTarget);
            return invoke->invoker->askForSkillInvoke(this, notify_data);
        }
    }

    return false;
}

bool TriggerSkill::effect(TriggerEvent /*unused*/, Room * /*unused*/, QSharedPointer<SkillInvokeDetail> /*unused*/, QVariant & /*unused*/) const
{
    return false;
}

MasochismSkill::MasochismSkill(const QString &name)
    : TriggerSkill(name)
{
    events << Damaged;
}

QList<SkillInvokeDetail> MasochismSkill::triggerable(TriggerEvent /*triggerEvent*/, const Room *room, const QVariant &data) const
{
    DamageStruct damage = data.value<DamageStruct>();
    return triggerable(room, damage);
}

QList<SkillInvokeDetail> MasochismSkill::triggerable(const Room * /*unused*/, const DamageStruct & /*unused*/) const
{
    return {};
}

bool MasochismSkill::effect(TriggerEvent /*triggerEvent*/, Room *room, QSharedPointer<SkillInvokeDetail> invoke, QVariant &data) const
{
    DamageStruct damage = data.value<DamageStruct>();
    onDamaged(room, invoke, damage);

    return false;
}

PhaseChangeSkill::PhaseChangeSkill(const QString &name)
    : TriggerSkill(name)
{
    events << EventPhaseStart;
}

bool PhaseChangeSkill::effect(TriggerEvent /*triggerEvent*/, Room * /*room*/, QSharedPointer<SkillInvokeDetail> /*invoke*/, QVariant &data) const
{
    ServerPlayer *player = data.value<ServerPlayer *>();
    return onPhaseChange(player);
}

int MaxCardsSkill::getExtra(const Player * /*unused*/) const
{
    return 0;
}

int MaxCardsSkill::getFixed(const Player * /*unused*/) const
{
    return -1;
}

ProhibitSkill::ProhibitSkill(const QString &name)
    : Skill(name)
{
    setCompulsory();
}

DistanceSkill::DistanceSkill(const QString &name)
    : Skill(name, "static")
{
    setCompulsory();
    view_as_skill = new ShowDistanceSkill(objectName());
}

const ViewAsSkill *DistanceSkill::getViewAsSkill() const
{
    return view_as_skill;
}

ShowDistanceSkill::ShowDistanceSkill(const QString &name)
    : ZeroCardViewAsSkill(name)
{
}

const Card *ShowDistanceSkill::viewAs() const
{
    SkillCard *card = Sanguosha->cloneSkillCard("ShowFengsu");
    card->setUserString(objectName());
    return card;
}

bool ShowDistanceSkill::isEnabledAtPlay(const Player *player) const
{
    if (!isHegemonyGameMode(ServerInfo.GameMode))
        return false;
    //const DistanceSkill *skill = qobject_cast<const DistanceSkill *>(Sanguosha->getSkill(objectName()));
    const Skill *skill = Sanguosha->getSkill(objectName());
    if (skill != nullptr) {
        if (!player->hasShownSkill(skill->objectName()))
            return true;
    }
    return false;
}

MaxCardsSkill::MaxCardsSkill(const QString &name)
    : Skill(name, "static")
{
    setCompulsory();
    view_as_skill = new ShowDistanceSkill(objectName());
}

const ViewAsSkill *MaxCardsSkill::getViewAsSkill() const
{
    return view_as_skill;
}

TargetModSkill::TargetModSkill(const QString &name)
    : Skill(name)
{
    setCompulsory();
    pattern = "Slash";
}

QString TargetModSkill::getPattern() const
{
    return pattern;
}

int TargetModSkill::getResidueNum(const Player * /*unused*/, const Card * /*unused*/) const
{
    return 0;
}

int TargetModSkill::getDistanceLimit(const Player * /*unused*/, const Card * /*unused*/) const
{
    return 0;
}

int TargetModSkill::getExtraTargetNum(const Player * /*unused*/, const Card * /*unused*/) const
{
    return 0;
}

AttackRangeSkill::AttackRangeSkill(const QString &name)
    : Skill(name, "static")
{
    setCompulsory();
    view_as_skill = new ShowDistanceSkill(objectName()); //alternative method: add ShowDistanceSkill to specific AttackRangeSkills.
}

const ViewAsSkill *AttackRangeSkill::getViewAsSkill() const
{
    return view_as_skill;
}

int AttackRangeSkill::getExtra(const Player * /*unused*/, bool /*unused*/) const
{
    return 0;
}

int AttackRangeSkill::getFixed(const Player * /*unused*/, bool /*unused*/) const
{
    return -1;
}

SlashNoDistanceLimitSkill::SlashNoDistanceLimitSkill(const QString &skill_name)
    : TargetModSkill(QString("#%1-slash-ndl").arg(skill_name))
    , name(skill_name)
{
}

int SlashNoDistanceLimitSkill::getDistanceLimit(const Player *from, const Card *card) const
{
    if (from->hasSkill(name) && card->getSkillName() == name)
        return 1000;
    else
        return 0;
}

bool EquipSkill::equipAvailable(const Player *p, EquipCard::Location location, const QString &equipName, const Player *to /*= NULL*/)
{
    if (p == nullptr)
        return false;

    if (p->getMark("Equips_Nullified_to_Yourself") > 0)
        return false;

    if (to != nullptr && to->getMark("Equips_of_Others_Nullified_to_You") > 0)
        return false;

    switch (location) {
    case EquipCard::WeaponLocation:
        if (!p->hasWeapon(equipName))
            return false;
        break;
    case EquipCard::ArmorLocation:
        if (!p->hasArmorEffect(equipName))
            return false;
        break;
    case EquipCard::TreasureLocation:
        if (!p->hasTreasure(equipName))
            return false;
        break;
    default:
        break; // shenmegui?
    }

    return true;
}

bool EquipSkill::equipAvailable(const Player *p, const EquipCard *card, const Player *to /*= NULL*/)
{
    if (card == nullptr)
        return false;

    return equipAvailable(p, card->location(), card->objectName(), to);
}

ViewHasSkill::ViewHasSkill(const QString &name)
    : Skill(name)
    , global(false)
{
    setCompulsory();
}

BattleArraySkill::BattleArraySkill(const QString &name, const QString &type) //
    : TriggerSkill(name)
    , array_type(type)
{
    if (!inherits("LuaBattleArraySkill")) //extremely dirty hack!!!
        view_as_skill = new ArraySummonSkill(objectName());
}

//bool BattleArraySkill::triggerable(const ServerPlayer *player) const
//QList<SkillInvokeDetail> triggerable(TriggerEvent triggerEvent, const Room *room, const QVariant &data) const
//{
//return  TriggerSkill::triggerable(        //TriggerSkill::triggerable(player) && player->aliveCount() >= 4;
//if (room->getAlivePlayers().length() >= 4 && TriggerSkill::triggerable(triggerEvent, )

//    return QList<SkillInvokeDetail>();
//}

void BattleArraySkill::summonFriends(ServerPlayer *player) const
{
    player->summonFriends(array_type);
}

ArraySummonSkill::ArraySummonSkill(const QString &name)
    : ZeroCardViewAsSkill(name)
{
}

const Card *ArraySummonSkill::viewAs() const
{
    QString name = objectName();
    name[0] = name[0].toUpper();
    name += "Summon";
    Card *card = Sanguosha->cloneSkillCard(name);
    card->setShowSkill(objectName());
    return card;
}

//using namespace HegemonyMode;
bool ArraySummonSkill::isEnabledAtPlay(const Player *player) const
{
    if (player->getAliveSiblings().length() < 3)
        return false;
    if (player->hasFlag("Global_SummonFailed"))
        return false;
    if (!player->canShowGeneral(player->inHeadSkills(objectName()) ? "h" : "d"))
        return false;
    const BattleArraySkill *skill = qobject_cast<const BattleArraySkill *>(Sanguosha->getTriggerSkill(objectName()));
    if (skill != nullptr) {
        QString type = skill->getArrayType();

        if (type == "Siege") {
            //return true;
            if (player->willBeFriendWith(player->getNextAlive()) && player->willBeFriendWith(player->getLastAlive()))
                return false;
            if (!player->willBeFriendWith(player->getNextAlive())) {
                if (!player->getNextAlive(2)->hasShownOneGeneral() && player->getNextAlive()->hasShownOneGeneral())
                    return true;
            }
            if (!player->willBeFriendWith(player->getLastAlive()))
                return !player->getLastAlive(2)->hasShownOneGeneral() && player->getLastAlive()->hasShownOneGeneral();

        } else if (type == "Formation") {
            int n = player->aliveCount(false);
            int asked = n;
            for (int i = 1; i < n; ++i) {
                Player *target = player->getNextAlive(i);
                if (player->isFriendWith(target))
                    continue;
                else if (!target->hasShownOneGeneral())
                    return true;
                else {
                    asked = i;
                    break;
                }
            }
            n -= asked;
            for (int i = 1; i < n; ++i) {
                Player *target = player->getLastAlive(i);
                if (player->isFriendWith(target))
                    continue;
                else
                    return !target->hasShownOneGeneral();
            }
        }
    }
    return false;
}
