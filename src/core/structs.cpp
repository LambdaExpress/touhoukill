#include "structs.h"
#include "exppattern.h"
#include "json.h"
#include "protocol.h"
#include "room.h"
#include "skill.h"
#include <functional>

bool CardsMoveStruct::tryParse(const QVariant &arg)
{
    JsonArray args = arg.value<JsonArray>();
    if (args.size() != 8)
        return false;

    if ((!JsonUtils::isNumber(args[0]) && !args[0].canConvert<JsonArray>()) || !JsonUtils::isNumberArray(args, 1, 2) || !JsonUtils::isStringArray(args, 3, 6))
        return false;

    /*if (JsonUtils::isNumber(args[0])) {
        int size = args[0].toInt();
        for (int i = 0; i < size; i++)
            card_ids.append(Card::S_UNKNOWN_CARD_ID);
    } else */
    if (!JsonUtils::tryParse(args[0], card_ids)) {
        return false;
    }

    from_place = (Player::Place)args[1].toInt();
    to_place = (Player::Place)args[2].toInt();
    from_player_name = args[3].toString();
    to_player_name = args[4].toString();
    from_pile_name = args[5].toString();
    to_pile_name = args[6].toString();
    reason.tryParse(args[7]);
    return true;
}

QVariant CardsMoveStruct::toVariant() const
{
    //notify Client
    JsonArray arg;
    if (open) {
        arg << JsonUtils::toJsonArray(card_ids);
    } else {
        QList<int> notify_ids;
        //keep original order?
        foreach (int id, card_ids) {
            if (shown_ids.contains(id))
                notify_ids << id;
            else
                notify_ids.append(Card::S_UNKNOWN_CARD_ID);
        }
        /*
        int num = card_ids.length() - shown_ids.length();
        notify_ids << shown_ids;
        if (num > 0) {
            for (int i = 0; i < num; i++)
                notify_ids.append(Card::S_UNKNOWN_CARD_ID);
        }*/
        arg << JsonUtils::toJsonArray(notify_ids);
    }

    arg << (int)from_place;
    arg << (int)to_place;
    arg << from_player_name;
    arg << to_player_name;
    arg << from_pile_name;
    arg << to_pile_name;
    arg << reason.toVariant();
    return arg;
}

bool CardMoveReason::tryParse(const QVariant &arg)
{
    JsonArray args = arg.value<JsonArray>();
    if (args.size() != 5 || !args[0].canConvert<int>() || !JsonUtils::isStringArray(args, 1, 4))
        return false;

    m_reason = args[0].toInt();
    m_playerId = args[1].toString();
    m_skillName = args[2].toString();
    m_eventName = args[3].toString();
    m_targetId = args[4].toString();

    return true;
}

QVariant CardMoveReason::toVariant() const
{
    JsonArray result;
    result << m_reason;
    result << m_playerId;
    result << m_skillName;
    result << m_eventName;
    result << m_targetId;
    return result;
}

DamageStruct::DamageStruct()
    : from(nullptr)
    , to(nullptr)
    , card(nullptr)
    , damage(1)
    , nature(Normal)
    , chain(false)
    , transfer(false)
    , by_user(true)
    , trigger_chain(false)
{
}

DamageStruct::DamageStruct(const Card *card, ServerPlayer *from, ServerPlayer *to, int damage, DamageStruct::Nature nature)
    : chain(false)
    , transfer(false)
    , by_user(true)
    , trigger_chain(false)
{
    this->card = card;
    this->from = from;
    this->to = to;
    this->damage = damage;
    this->nature = nature;
}

DamageStruct::DamageStruct(const QString &reason, ServerPlayer *from, ServerPlayer *to, int damage, DamageStruct::Nature nature)
    : card(nullptr)
    , chain(false)
    , transfer(false)
    , by_user(true)
    , trigger_chain(false)
{
    this->from = from;
    this->to = to;
    this->damage = damage;
    this->nature = nature;
    this->reason = reason;
}

QString DamageStruct::getReason() const
{
    if (reason != QString())
        return reason;
    else if (card != nullptr)
        return card->objectName();
    return {};
}

CardEffectStruct::CardEffectStruct()
    : card(nullptr)
    , from(nullptr)
    , to(nullptr)
    , multiple(false)
    , nullified(false)
    , canceled(false)
    , effectValue {0, 0}
{
}

SlashEffectStruct::SlashEffectStruct()
    : jink_num(1)
    , slash(nullptr)
    , jink(nullptr)
    , from(nullptr)
    , to(nullptr)
    , drank(0)
    , magic_drank(0)
    , nature(DamageStruct::Normal)
    , multiple(false)
    , nullified(false)
    , canceled(false)
    , effectValue {0, 0}
{
}

DyingStruct::DyingStruct()
    : who(nullptr)
    , damage(nullptr)
    , nowAskingForPeaches(nullptr)
{
}

DeathStruct::DeathStruct()
    : who(nullptr)
    , damage(nullptr)
    , viewAsKiller(nullptr)
    , useViewAsKiller(false)
{
}

RecoverStruct::RecoverStruct()
    : recover(1)
    , who(nullptr)
    , card(nullptr)
{
}

PindianStruct::PindianStruct()
    : from(nullptr)
    , to(nullptr)
    , askedPlayer(nullptr)
    , from_card(nullptr)
    , to_card(nullptr)
    , success(false)
{
}

bool PindianStruct::isSuccess() const
{
    return success;
}

JudgeStruct::JudgeStruct()
    : who(nullptr)
    , card(nullptr)
    , pattern(".")
    , good(true)
    , time_consuming(false)
    , negative(false)
    , play_animation(true)
    , retrial_by_response(nullptr)
    , relative_player(nullptr)
    , ignore_judge(false)
    , _m_result(TRIAL_RESULT_UNKNOWN)
{
}

bool JudgeStruct::isEffected() const
{
    return negative ? isBad() : isGood();
}

void JudgeStruct::updateResult()
{
    bool effected = (good == ExpPattern(pattern).match(who, card));
    if (effected)
        _m_result = TRIAL_RESULT_GOOD;
    else
        _m_result = TRIAL_RESULT_BAD;
}

bool JudgeStruct::isGood() const
{
    Q_ASSERT(_m_result != TRIAL_RESULT_UNKNOWN);
    return _m_result == TRIAL_RESULT_GOOD;
}

bool JudgeStruct::isBad() const
{
    return !isGood();
}

bool JudgeStruct::isGood(const Card *card) const
{
    Q_ASSERT(card);
    return (good == ExpPattern(pattern).match(who, card));
}

PhaseChangeStruct::PhaseChangeStruct()
    : from(Player::NotActive)
    , to(Player::NotActive)
    , player(nullptr)
{
}

CardUseStruct::CardUseStruct()
    : card(nullptr)
    , from(nullptr)
    , m_isOwnerUse(true)
    , m_addHistory(true)
    , m_effectValue {0, 0}
{
}

CardUseStruct::CardUseStruct(const Card *card, ServerPlayer *from, const QList<ServerPlayer *> &to, bool isOwnerUse)
{
    this->card = card;
    this->from = from;
    this->to = to;
    this->m_isOwnerUse = isOwnerUse;
    this->m_addHistory = true;
    this->m_isHandcard = false;
    this->m_isLastHandcard = false;
    this->m_effectValue = {0, 0};
}

CardUseStruct::CardUseStruct(const Card *card, ServerPlayer *from, ServerPlayer *target, bool isOwnerUse)
{
    this->card = card;
    this->from = from;
    if (target != nullptr)
        to << target;
    this->m_isOwnerUse = isOwnerUse;
    this->m_addHistory = true;
    this->m_isHandcard = false;
    this->m_isLastHandcard = false;
    this->m_effectValue = {0, 0};
}

bool CardUseStruct::isStructurallyValid(const QString &pattern) const
{
    Q_UNUSED(pattern)
    return card != nullptr;
}

bool CardUseStruct::isValid(const QString &pattern) const
{
    return isStructurallyValid(pattern);
#if 0
    if (card == NULL)
        return false;
    if (!card->getSkillName().isEmpty()) {
        bool validSkill = false;
        QString skillName = card->getSkillName();
        QSet<const Skill *> skills = from->getVisibleSkills();
        for (int i = 0; i < 4; i++) {
            const EquipCard *equip = from->getEquip(i);
            if (equip == NULL)
                continue;
            const Skill *skill = Sanguosha->getSkill(equip);
            if (skill)
                skills.insert(skill);
        }
        foreach (const Skill *skill, skills) {
            if (skill->objectName() != skillName)
                continue;
            const ViewAsSkill *vsSkill = ViewAsSkill::parseViewAsSkill(skill);
            if (vsSkill) {
                if (!vsSkill->isAvailable(from, m_reason, pattern))
                    return false;
                else {
                    validSkill = true;
                    break;
                }
            } else if (skill->isWake()) {
                bool valid = (from->getMark(skill->objectName()) > 0);
                if (!valid)
                    return false;
                else
                    validSkill = true;
            } else
                return false;
        }
        if (!validSkill)
            return false;
    }
    if (card->targetFixed())
        return true;
    else {
        QList<const Player *> targets;
        foreach (const ServerPlayer *player, to)
            targets.push_back(player);
        return card->targetsFeasible(targets, from);
    }
#endif
}

QString normalizeCardUsePattern(const QString &pattern)
{
    QString normalized = pattern;
    if (normalized.endsWith(QChar('!')))
        normalized.chop(1);
    if (normalized == QStringLiteral("."))
        return QStringLiteral("..");
    return normalized;
}

ActionRequestContext::ActionRequestContext()
    : player(nullptr)
    , command(QSanProtocol::S_COMMAND_UNKNOWN)
    , reason(CardUseStruct::CARD_USE_REASON_UNKNOWN)
    , method(Card::MethodNone)
    , requestSerial(-1)
    , fromClient(true)
    , serverBuiltCard(false)
    , requireSingleCardSelection(false)
{
}

CardUseGrant::CardUseGrant()
    : valid(false)
    , player(nullptr)
    , sourceKind(OwnedSkill)
    , reason(CardUseStruct::CARD_USE_REASON_UNKNOWN)
    , method(Card::MethodNone)
    , allowNoSubcards(false)
    , allowOtherPlayersCards(false)
    , allowVirtualCard(true)
    , allowRealCard(true)
    , requireViewAsValidation(false)
    , requestSerial(-1)
    , remainingUses(1)
{
}

bool CardUseGrant::isValid() const
{
    return valid && player != nullptr && remainingUses != 0;
}

bool CardUseGrant::matchesContext(const ClientActionContext &ctx) const
{
    if (!isValid() || ctx.player == nullptr || player != ctx.player)
        return false;
    if (reason != CardUseStruct::CARD_USE_REASON_UNKNOWN && ctx.reason != CardUseStruct::CARD_USE_REASON_UNKNOWN && reason != ctx.reason)
        return false;
    if (method != Card::MethodNone && ctx.method != Card::MethodNone && method != ctx.method)
        return false;
    if (!pattern.isEmpty() && pattern != ctx.pattern)
        return false;
    if (requestSerial >= 0 && requestSerial != ctx.requestSerial)
        return false;
    return true;
}

bool CardUseStruct::tryParse(const QVariant &usage, Room *room)
{
    JsonArray arr = usage.value<JsonArray>();
    if (arr.length() < 2 || !JsonUtils::isString(arr.first()) || !arr.value(1).canConvert<JsonArray>())
        return false;

    card = Card::Parse(arr.first().toString());
    JsonArray targets = arr.value(1).value<JsonArray>();

    for (int i = 0; i < targets.size(); i++) {
        if (!JsonUtils::isString(targets.value(i)))
            return false;
        to << room->findChild<ServerPlayer *>(targets.value(i).toString());
    }
    return true;
}

void CardUseStruct::parse(const QString &str, Room *room)
{
    QStringList words = str.split("->", QString::KeepEmptyParts);
    Q_ASSERT(words.length() == 1 || words.length() == 2);

    const QString &card_str = words.at(0);
    QString target_str = ".";

    if (words.length() == 2 && !words.at(1).isEmpty())
        target_str = words.at(1);

    card = Card::Parse(card_str);

    if (target_str != ".") {
        QStringList target_names = target_str.split("+");
        foreach (const QString &target_name, target_names)
            to << room->findChild<ServerPlayer *>(target_name);
    }
}

QString CardUseStruct::toString() const
{
    if (card == nullptr)
        return {};

    QStringList l;
    l << card->toString();

    if (to.isEmpty())
        l << ".";
    else {
        QStringList tos;
        foreach (ServerPlayer *p, to)
            tos << p->objectName();

        l << tos.join("+");
    }
    return l.join("->");
}

ActionProposal::ActionProposal()
    : type(Invalid)
{
}

bool ActionProposal::isValid() const
{
    return type != Invalid;
}

bool ActionProposal::isCancel() const
{
    return type == Cancel;
}

ActionProposal ActionProposal::makeCancel()
{
    ActionProposal proposal;
    proposal.type = Cancel;
    return proposal;
}

QString ActionProposal::typeName() const
{
    switch (type) {
    case Cancel:
        return QStringLiteral("cancel");
    case RealCard:
        return QStringLiteral("real_card");
    case SelectedCards:
        return QStringLiteral("selected_cards");
    case ViewAsSkillCard:
        return QStringLiteral("view_as");
    case SkillCard:
        return QStringLiteral("skill_card");
    case Invalid:
        break;
    }
    return QStringLiteral("invalid");
}

static ActionProposal::ActionType actionProposalTypeFromName(const QString &name)
{
    if (name == QStringLiteral("cancel"))
        return ActionProposal::Cancel;
    if (name == QStringLiteral("real_card"))
        return ActionProposal::RealCard;
    if (name == QStringLiteral("selected_cards"))
        return ActionProposal::SelectedCards;
    if (name == QStringLiteral("view_as"))
        return ActionProposal::ViewAsSkillCard;
    if (name == QStringLiteral("skill_card"))
        return ActionProposal::SkillCard;
    return ActionProposal::Invalid;
}

static QStringList actionProposalTargetNames(const QList<const Player *> &targets)
{
    QStringList names;
    foreach (const Player *target, targets) {
        if (target != nullptr)
            names << target->objectName();
    }
    return names;
}

static QString extractSkillCardUserString(const Card *card)
{
    if (card == nullptr || !card->isVirtualCard())
        return QString();

    const QString cardString = card->toString();
    const int subcardMarker = cardString.indexOf(QStringLiteral("]="));
    const int userStringMarker = subcardMarker >= 0 ? cardString.indexOf(QChar(':'), subcardMarker + 2) : -1;
    if (userStringMarker < 0)
        return QString();

    return cardString.mid(userStringMarker + 1);
}

static bool actionProposalUserStringIsSelectedEffect(const QString &cardClass)
{
    return cardClass == QStringLiteral("XianshiCard");
}

static QString actionProposalDeclaredCardNameFromUserString(const QString &cardClass, const QString &userString)
{
    if (userString.isEmpty())
        return QString();

    static const QStringList declaredCardNameCardClasses = QStringList() << QStringLiteral("NianliCard") << QStringLiteral("HuaxiangCard") << QStringLiteral("XihuaCard")
                                                                        << QStringLiteral("YucanCard") << QStringLiteral("ChuangshiCard");
    if (!declaredCardNameCardClasses.contains(cardClass))
        return QString();

    return userString.split(QStringLiteral("+")).first();
}

QString ActionProposal::skillNameFromPattern(const QString &pattern)
{
    if (!pattern.startsWith(QChar('@')))
        return QString();

    int index = pattern.startsWith(QStringLiteral("@@")) ? 2 : 1;
    QString skillName;
    while (index < pattern.length()) {
        const QChar ch = pattern.at(index);
        if (ch.isLetterOrNumber() || ch == QChar('_')) {
            skillName.append(ch);
            ++index;
        } else {
            break;
        }
    }

    return skillName;
}

ActionProposal ActionProposal::fromCard(const Card *card, const QList<const Player *> &targets, const QString &sourceSkillName)
{
    if (card == nullptr)
        return makeCancel();

    QString cardSkillName = card->getSkillName(false);
    if (cardSkillName.startsWith(QChar('_')))
        cardSkillName = cardSkillName.mid(1);
    QString inferredSkillName = cardSkillName;
    if (inferredSkillName.isEmpty() && card->isKindOf("SkillCard") && card->getClassName().endsWith(QStringLiteral("Card"))
        && card->getClassName() != QStringLiteral("DummyCard") && card->getClassName() != QStringLiteral("SurrenderCard")
        && card->getClassName() != QStringLiteral("CheatCard")) {
        inferredSkillName = card->getClassName();
        inferredSkillName.chop(4);
        inferredSkillName = inferredSkillName.toLower();
    }

    QString shownSkillName = card->showSkill();
    if (shownSkillName.startsWith(QChar('_')))
        shownSkillName = shownSkillName.mid(1);

    ActionProposal proposal;
    proposal.skillName = sourceSkillName;
    if (proposal.skillName.isEmpty())
        proposal.skillName = shownSkillName;
    if (proposal.skillName.isEmpty())
        proposal.skillName = inferredSkillName;
    if (proposal.skillName.startsWith(QChar('_')))
        proposal.skillName = proposal.skillName.mid(1);
    const bool delegatedByAnyun = card->isVirtualCard() && (proposal.skillName == QStringLiteral("anyun") || shownSkillName == QStringLiteral("anyun"))
        && !inferredSkillName.isEmpty() && inferredSkillName != QStringLiteral("anyun");
    if (delegatedByAnyun) {
        proposal.skillName = QStringLiteral("anyun");
        proposal.extra.insert(QStringLiteral("delegatedSkillName"), inferredSkillName);
    }
    proposal.cardClass = card->getClassName();
    proposal.cardName = card->objectName();
    proposal.declaredCardName = card->objectName();
    proposal.targetNames = actionProposalTargetNames(targets);

    if (card->isVirtualCard()) {
        if (card->isKindOf("DummyCard") && proposal.skillName.isEmpty())
            proposal.type = SelectedCards;
        else
            proposal.type = proposal.skillName.isEmpty() ? SkillCard : ViewAsSkillCard;
        proposal.subcardIds = card->getSubcards();

        const QString userString = extractSkillCardUserString(card);
        if (!userString.isEmpty()) {
            if (proposal.cardClass == QStringLiteral("CheatCard")) {
                proposal.extra.insert(QStringLiteral("legacyUserString"), userString);
            } else if (actionProposalUserStringIsSelectedEffect(proposal.cardClass)) {
                proposal.extra.insert(QStringLiteral("selectedEffect"), userString);
            } else {
                const QString declaredCardName = actionProposalDeclaredCardNameFromUserString(proposal.cardClass, userString);
                if (!declaredCardName.isEmpty())
                    proposal.declaredCardName = declaredCardName;
            }
        }
    } else {
        proposal.type = RealCard;
        proposal.subcardIds << card->getEffectiveId();
    }

    return proposal;
}

bool ActionProposal::tryParse(const QVariant &value)
{
    *this = ActionProposal();
    if (!value.canConvert<JsonObject>())
        return false;

    JsonObject object = value.value<JsonObject>();
    if (!object.contains(QStringLiteral("type")) || !JsonUtils::isString(object.value(QStringLiteral("type"))))
        return false;

    type = actionProposalTypeFromName(object.value(QStringLiteral("type")).toString());
    if (type == Invalid)
        return false;

    skillName = object.value(QStringLiteral("skillName")).toString();
    cardClass = object.value(QStringLiteral("cardClass")).toString();
    cardName = object.value(QStringLiteral("cardName")).toString();
    declaredCardName = object.value(QStringLiteral("declaredCardName")).toString();

    if (object.contains(QStringLiteral("cardId"))) {
        bool ok = false;
        int cardId = object.value(QStringLiteral("cardId")).toInt(&ok);
        if (!ok)
            return false;
        subcardIds << cardId;
    }

    if (object.contains(QStringLiteral("subcards"))) {
        QList<int> parsedSubcards;
        if (!JsonUtils::tryParse(object.value(QStringLiteral("subcards")), parsedSubcards))
            return false;
        subcardIds = parsedSubcards;
    }

    if (object.contains(QStringLiteral("targets"))) {
        QStringList parsedTargets;
        if (!JsonUtils::tryParse(object.value(QStringLiteral("targets")), parsedTargets))
            return false;
        targetNames = parsedTargets;
    }

    if (object.contains(QStringLiteral("extra"))) {
        const QVariant extraValue = object.value(QStringLiteral("extra"));
        if (!extraValue.canConvert<JsonObject>())
            return false;
        extra = extraValue.value<JsonObject>();
    }

    if (type == RealCard && subcardIds.length() != 1)
        return false;
    if (type == SelectedCards && subcardIds.isEmpty())
        return false;
    return true;
}

QVariant ActionProposal::toVariant() const
{
    JsonObject object;
    object.insert(QStringLiteral("type"), typeName());
    if (!skillName.isEmpty())
        object.insert(QStringLiteral("skillName"), skillName);
    if (!cardClass.isEmpty())
        object.insert(QStringLiteral("cardClass"), cardClass);
    if (!cardName.isEmpty())
        object.insert(QStringLiteral("cardName"), cardName);
    if (!declaredCardName.isEmpty())
        object.insert(QStringLiteral("declaredCardName"), declaredCardName);
    if (!subcardIds.isEmpty())
        object.insert(QStringLiteral("subcards"), JsonUtils::toJsonArray(subcardIds));
    if (!targetNames.isEmpty())
        object.insert(QStringLiteral("targets"), JsonUtils::toJsonArray(targetNames));
    if (!extra.isEmpty())
        object.insert(QStringLiteral("extra"), extra);
    return object;
}

QString ActionProposal::diagnosticString() const
{
    QStringList subcards;
    foreach (int id, subcardIds)
        subcards << QString::number(id);
    return QStringLiteral("type=%1 skill=%2 class=%3 card=%4 declared=%5 subcards=%6 targets=%7 extra=%8")
        .arg(typeName(), skillName, cardClass, cardName, declaredCardName, subcards.join(QStringLiteral("+")), targetNames.join(QStringLiteral("+")))
        .arg(extra.keys().join(QStringLiteral("+")));
}

MarkChangeStruct::MarkChangeStruct()
    : num(1)
    , player(nullptr)
{
}

bool SkillInvokeDetail::operator<(const SkillInvokeDetail &arg2) const // the operator < for sorting the invoke order.
{
    //  we sort firstly according to the priority, then the seat of invoker, at last whether it is a skill of an equip.
    if (!isValid() || !arg2.isValid())
        return false;

    if (skill->getPriority() > arg2.skill->getPriority())
        return true;
    else if (skill->getPriority() < arg2.skill->getPriority())
        return false;

    std::function<Room *(ServerPlayer *)> getRoom = [this](ServerPlayer *p) -> Room * {
        if (p != nullptr)
            return p->getRoom();
        else {
            // let's treat it as a gamerule, the gamerule is created inside roomthread
            RoomThread *thread = qobject_cast<RoomThread *>(skill->thread());
            if (thread == nullptr)
                return nullptr;

            return thread->getRoom();
        }

        return nullptr;
    };

    if (invoker != arg2.invoker) {
        Room *room = getRoom(owner);
        if (room == nullptr)
            return false;

        return room->getFront(invoker, arg2.invoker) == invoker;
    }

    return !skill->isEquipSkill() && arg2.skill->isEquipSkill();
}

bool SkillInvokeDetail::sameSkill(const SkillInvokeDetail &arg2) const
{
    // it only judge the skill name, the skill invoker and the skill owner. It don't judge the skill target because it is chosen by the skill invoker
    return skill == arg2.skill && owner == arg2.owner && invoker == arg2.invoker;
}

bool SkillInvokeDetail::sameTimingWith(const SkillInvokeDetail &arg2) const
{
    // used to judge 2 skills has the same timing. only 2 structs with the same priority and the same invoker and the same "whether or not it is a skill of equip"
    if (!isValid() || !arg2.isValid())
        return false;

    return skill->getPriority() == arg2.skill->getPriority() && invoker == arg2.invoker && skill->isEquipSkill() == arg2.skill->isEquipSkill();
}

SkillInvokeDetail::SkillInvokeDetail(const TriggerSkill *skill /*= NULL*/, ServerPlayer *owner /*= NULL*/, ServerPlayer *invoker /*= NULL*/,
                                     const QList<ServerPlayer *> &targets /*= QList<ServerPlayer *>()*/, bool isCompulsory /*= false*/, ServerPlayer *preferredTarget /*= NULL*/,
                                     bool showHidden)
    : skill(skill)
    , owner(owner)
    , invoker(invoker)
    , targets(targets)
    , isCompulsory(isCompulsory)
    , triggered(false)
    , preferredTarget(preferredTarget)
    , showHidden(showHidden)
{
}

SkillInvokeDetail::SkillInvokeDetail(const TriggerSkill *skill, ServerPlayer *owner, ServerPlayer *invoker, ServerPlayer *target, bool isCompulsory /*= false*/,
                                     ServerPlayer *preferredTarget /*= NULL*/, bool showHidden)
    : skill(skill)
    , owner(owner)
    , invoker(invoker)
    , isCompulsory(isCompulsory)
    , triggered(false)
    , preferredTarget(preferredTarget)
    , showHidden(showHidden)
{
    if (target != nullptr)
        targets << target;
}

bool SkillInvokeDetail::isValid() const // validity check
{
    return skill != nullptr;
}

bool SkillInvokeDetail::preferredTargetLess(const SkillInvokeDetail &arg2) const
{
    if (skill == arg2.skill && owner == arg2.owner && invoker == arg2.invoker) {
        // we compare preferred target to ensure the target selected is in the order of seat only in the case that 2 skills are the same
        if (preferredTarget != nullptr && arg2.preferredTarget != nullptr)
            return ServerPlayer::CompareByActionOrder(preferredTarget, arg2.preferredTarget);
    }

    return false;
}

QVariant SkillInvokeDetail::toVariant() const
{
    if (!isValid())
        return {};

    JsonObject ob;
    if (skill != nullptr)
        ob["skill"] = skill->objectName();
    if (owner != nullptr)
        ob["owner"] = owner->objectName();
    if (invoker != nullptr)
        ob["invoker"] = invoker->objectName();
    if (preferredTarget != nullptr) {
        ob["preferredtarget"] = preferredTarget->objectName();
        Room *room = preferredTarget->getRoom();
        ServerPlayer *current = room->getCurrent();
        if (current == nullptr)
            current = room->getLord();
        if (current == nullptr)
            current = preferredTarget;

        // send the seat info to the client so that we can compare the trigger order of tieqi-like skill in the client side
        int seat = preferredTarget->getSeat() - current->getSeat();
        if (seat < 0)
            seat += room->getPlayers().length();

        ob["preferredtargetseat"] = seat;
    }
    return ob;
}

QStringList SkillInvokeDetail::toList() const
{
    QStringList l;
    if (!isValid())
        l << QString() << QString() << QString() << QString();
    else {
        std::function<void(const QObject *)> insert = [&l](const QObject *item) {
            if (item != nullptr)
                l << item->objectName();
            else
                l << QString();
        };

        insert(skill);
        insert(owner);
        insert(invoker);
        insert(preferredTarget);
    }

    return l;
}

SkillAcquireDetachStruct::SkillAcquireDetachStruct()
    : skill(nullptr)
    , player(nullptr)
    , isAcquire(false)
{
}

ChoiceMadeStruct::ChoiceMadeStruct()
    : player(nullptr)
{
}

CardAskedStruct::CardAskedStruct()
    : player(nullptr)
{
}

HpLostStruct::HpLostStruct()
    : player(nullptr)
    , num(0)
{
}

JinkEffectStruct::JinkEffectStruct()
    : jink(nullptr)
{
}

PhaseSkippingStruct::PhaseSkippingStruct()
    : phase(Player::NotActive)
    , player(nullptr)
    , isCost(false)
{
}

DrawNCardsStruct::DrawNCardsStruct()
    : player(nullptr)
    , n(0)
    , isInitial(false)
{
}

SkillInvalidStruct::SkillInvalidStruct()
    : player(nullptr)
    , skill(nullptr)
    , invalid(false)
{
}

ExtraTurnStruct::ExtraTurnStruct()
    : player(nullptr)
    , extraTarget(nullptr)
{
}

BrokenEquipChangedStruct::BrokenEquipChangedStruct()
    : player(nullptr)
    , broken(false)
    , moveFromEquip(false)
{
}

ShownCardChangedStruct::ShownCardChangedStruct()
    : player(nullptr)
    , shown(false)
    , moveFromHand(false)
{
}

ShowGeneralStruct::ShowGeneralStruct()
    : player(nullptr)
    , isHead(true)
    , isShow(true)
{
}
