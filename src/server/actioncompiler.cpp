#include "actioncompiler.h"

#include "engine.h"
#include "exppattern.h"
#include "room.h"
#include "skill.h"

namespace
{
void disposeCompiledCard(const Card *card)
{
    if (card != nullptr && card->parent() == nullptr)
        delete card;
}

bool shouldPassDeclaredCardName(const ActionProposal &proposal)
{
    if (proposal.declaredCardName.isEmpty())
        return false;
    if (proposal.declaredCardName != proposal.cardName)
        return true;
    return proposal.type == ActionProposal::ViewAsSkillCard && !proposal.cardClass.endsWith(QStringLiteral("Card")) && proposal.cardClass != QStringLiteral("DummyCard");
}

bool shouldCompileWithViewAsSkill(const ActionRequestContext &ctx, const ActionProposal &proposal)
{
    return proposal.type == ActionProposal::ViewAsSkillCard || proposal.type == ActionProposal::SkillCard
        || (proposal.type == ActionProposal::RealCard && !proposal.skillName.isEmpty() && ctx.pattern.startsWith(QStringLiteral("@@")));
}
}

ActionCompileResult::ActionCompileResult()
    : success(false)
    , cancelled(false)
{
}

ActionCompiler::ActionCompiler(Room *room)
    : m_room(room)
{
}

ActionCompileResult ActionCompiler::compile(const ActionRequestContext &ctx, const ActionProposal &proposal) const
{
    if (m_room == nullptr)
        return fail(ctx, proposal, QStringLiteral("missing room"));
    if (ctx.player == nullptr)
        return fail(ctx, proposal, QStringLiteral("missing player"));
    if (!proposal.isValid())
        return fail(ctx, proposal, QStringLiteral("invalid proposal"));
    if (proposal.isCancel()) {
        ActionCompileResult result;
        result.cancelled = true;
        return result;
    }
    if (ctx.requireSingleCardSelection && proposal.type != ActionProposal::RealCard && proposal.type != ActionProposal::SelectedCards)
        return fail(ctx, proposal, QStringLiteral("single-card selection requires a real card"));

    QList<ServerPlayer *> targets;
    QString error;
    if (!resolveTargets(proposal, &targets, &error))
        return fail(ctx, proposal, error);

    const Card *card = nullptr;
    const bool useViewAsCompiler = shouldCompileWithViewAsSkill(ctx, proposal);
    if (proposal.type == ActionProposal::RealCard && !useViewAsCompiler)
        card = compileRealCard(proposal, &error);
    else if (proposal.type == ActionProposal::SelectedCards)
        card = compileSelectedCards(ctx, proposal, &error);
    else if (proposal.type == ActionProposal::SkillCard && proposal.skillName.isEmpty())
        card = compileStandaloneSkillCard(proposal, &error);
    else if (useViewAsCompiler)
        card = compileViewAsCard(ctx, proposal, &error);
    else
        error = QStringLiteral("unsupported proposal type");

    if (card == nullptr)
        return fail(ctx, proposal, error.isEmpty() ? QStringLiteral("card compilation failed") : error);

    CardUseStruct cardUse(card, ctx.player, targets);
    cardUse.m_reason = ctx.reason;

    if (proposal.type == ActionProposal::SelectedCards) {
        if (!targets.isEmpty())
            return fail(ctx, proposal, QStringLiteral("selected-card proposal cannot carry targets"));
        ActionCompileResult result;
        result.success = true;
        result.cardUse = cardUse;
        return result;
    }
    if (isStandaloneSpecialSkillCard(proposal)) {
        if (!targets.isEmpty())
            return fail(ctx, proposal, QStringLiteral("standalone skill card cannot carry targets"));
        ActionCompileResult result;
        result.success = true;
        result.cardUse = cardUse;
        return result;
    }

    ActionRequestContext validationCtx = ctx;
    validationCtx.fromClient = true;
    validationCtx.serverBuiltCard = useViewAsCompiler;
    if (useViewAsCompiler)
        validationCtx.expectedSkillName = proposal.skillName;
    if (!m_room->validateClientCardUse(cardUse, validationCtx))
        return fail(ctx, proposal, QStringLiteral("compiled card use failed server validation"));

    ActionCompileResult result;
    result.success = true;
    result.cardUse = cardUse;
    return result;
}

bool ActionCompiler::resolveTargets(const ActionProposal &proposal, QList<ServerPlayer *> *targets, QString *error) const
{
    if (targets == nullptr)
        return false;

    QList<ServerPlayer *> seenTargets;
    foreach (const QString &targetName, proposal.targetNames) {
        if (targetName.isEmpty()) {
            if (error != nullptr)
                *error = QStringLiteral("empty target");
            return false;
        }

        ServerPlayer *target = m_room->findPlayerByObjectName(targetName);
        if (target == nullptr) {
            if (error != nullptr)
                *error = QStringLiteral("unknown or dead target: %1").arg(targetName);
            return false;
        }
        if (seenTargets.contains(target)) {
            if (error != nullptr)
                *error = QStringLiteral("duplicate target: %1").arg(targetName);
            return false;
        }

        seenTargets << target;
        *targets << target;
    }

    return true;
}

const Card *ActionCompiler::compileRealCard(const ActionProposal &proposal, QString *error) const
{
    if (proposal.subcardIds.length() != 1) {
        if (error != nullptr)
            *error = QStringLiteral("real-card proposal must contain exactly one card id");
        return nullptr;
    }

    const int cardId = proposal.subcardIds.first();
    const Card *card = Sanguosha->getCard(cardId);
    if (card == nullptr) {
        if (error != nullptr)
            *error = QStringLiteral("unknown card id: %1").arg(cardId);
        return nullptr;
    }

    return card->getRealCard();
}

const Card *ActionCompiler::compileSelectedCards(const ActionRequestContext &ctx, const ActionProposal &proposal, QString *error) const
{
    if (proposal.subcardIds.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("selected-card proposal has no subcards");
        return nullptr;
    }
    if (ctx.requireSingleCardSelection && proposal.subcardIds.length() != 1) {
        if (error != nullptr)
            *error = QStringLiteral("single-card selection must contain exactly one card id");
        return nullptr;
    }
    if (ctx.method == Card::MethodUse && ctx.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
        if (error != nullptr)
            *error = QStringLiteral("selected-card proposal is not allowed for play use");
        return nullptr;
    }

    QList<int> ids;
    foreach (int cardId, proposal.subcardIds) {
        if (ids.contains(cardId)) {
            if (error != nullptr)
                *error = QStringLiteral("duplicate selected card id: %1").arg(cardId);
            return nullptr;
        }

        const Card *card = Sanguosha->getCard(cardId);
        if (card == nullptr) {
            if (error != nullptr)
                *error = QStringLiteral("unknown selected card id: %1").arg(cardId);
            return nullptr;
        }
        if (m_room->getCardOwner(cardId) != ctx.player) {
            if (error != nullptr)
                *error = QStringLiteral("selected card is not owned by acting player: %1").arg(cardId);
            return nullptr;
        }

        const Player::Place place = m_room->getCardPlace(cardId);
        const bool isHandPileCard = place == Player::PlaceSpecial && ctx.player != nullptr && ctx.player->getHandPile().contains(cardId);
        const bool placeAllowed = place == Player::PlaceHand || place == Player::PlaceEquip || isHandPileCard;
        if (!placeAllowed) {
            if (error != nullptr)
                *error = QStringLiteral("selected card is in an illegal place: %1").arg(cardId);
            return nullptr;
        }
        const QString normalizedPattern = normalizeCardUsePattern(ctx.pattern);
        const CardPattern *cardPattern = normalizedPattern.isEmpty() ? nullptr : Sanguosha->getPattern(normalizedPattern);
        if (cardPattern == nullptr && !normalizedPattern.isEmpty()) {
            if (error != nullptr)
                *error = QStringLiteral("unknown selected-card pattern: %1").arg(normalizedPattern);
            return nullptr;
        }
        if (cardPattern != nullptr && !cardPattern->match(ctx.player, card)) {
            if (error != nullptr)
                *error = QStringLiteral("selected card does not match pattern: %1").arg(cardId);
            return nullptr;
        }
        if (ctx.player != nullptr && ctx.player->isCardLimited(card, ctx.method)) {
            if (error != nullptr)
                *error = QStringLiteral("selected card is limited: %1").arg(cardId);
            return nullptr;
        }

        ids << cardId;
    }

    DummyCard *dummy = new DummyCard(ids);
    dummy->deleteLater();
    return dummy;
}

const Card *ActionCompiler::compileStandaloneSkillCard(const ActionProposal &proposal, QString *error) const
{
    if (!isStandaloneSpecialSkillCard(proposal)) {
        if (error != nullptr)
            *error = QStringLiteral("standalone skill card is not allowed");
        return nullptr;
    }
    if (!proposal.subcardIds.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("standalone skill card cannot carry subcards");
        return nullptr;
    }

    SkillCard *card = Sanguosha->cloneSkillCard(proposal.cardClass);
    if (card == nullptr) {
        if (error != nullptr)
            *error = QStringLiteral("cannot clone standalone skill card: %1").arg(proposal.cardClass);
        return nullptr;
    }
    if (proposal.cardClass == QStringLiteral("CheatCard"))
        card->setUserString(proposal.extra.value(QStringLiteral("legacyUserString")).toString());
    card->deleteLater();
    return card;
}

const Card *ActionCompiler::compileViewAsCard(const ActionRequestContext &ctx, const ActionProposal &proposal, QString *error) const
{
    if (proposal.skillName.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("missing view-as skill name");
        return nullptr;
    }

    const ViewAsSkill *skill = Sanguosha->getViewAsSkill(proposal.skillName);
    if (skill == nullptr) {
        if (error != nullptr)
            *error = QStringLiteral("unknown view-as skill: %1").arg(proposal.skillName);
        return nullptr;
    }

    ClientActionContext authorizationCtx = ctx;
    authorizationCtx.fromClient = true;
    authorizationCtx.serverBuiltCard = true;
    authorizationCtx.expectedSkillName = proposal.skillName;
    if (!m_room->canCompileClientViewAsSkill(authorizationCtx, proposal.skillName)) {
        if (error != nullptr)
            *error = QStringLiteral("view-as skill is not authorized for this request: %1").arg(proposal.skillName);
        return nullptr;
    }

    QList<const Card *> selected;
    foreach (int cardId, proposal.subcardIds) {
        const Card *card = Sanguosha->getCard(cardId);
        if (card == nullptr) {
            if (error != nullptr)
                *error = QStringLiteral("unknown subcard id: %1").arg(cardId);
            return nullptr;
        }
        selected << card;
    }

    JsonObject builderExtra = proposal.extra;
    if (shouldPassDeclaredCardName(proposal) && !builderExtra.contains(QStringLiteral("declaredCardName")))
        builderExtra.insert(QStringLiteral("declaredCardName"), proposal.declaredCardName);

    const Card *card = skill->buildServerCard(selected, ctx, builderExtra);
    if (card == nullptr) {
        if (error != nullptr)
            *error = QStringLiteral("view-as skill cannot build server card: %1").arg(proposal.skillName);
        return nullptr;
    }

    if (!proposal.cardClass.isEmpty() && card->getClassName() != proposal.cardClass) {
        const QString builtClass = card->getClassName();
        disposeCompiledCard(card);
        if (error != nullptr)
            *error = QStringLiteral("built card class mismatch: expected %1 got %2").arg(proposal.cardClass, builtClass);
        return nullptr;
    }

    if (card->parent() == nullptr && card->isVirtualCard())
        const_cast<Card *>(card)->deleteLater();
    return card;
}

bool ActionCompiler::isStandaloneSpecialSkillCard(const ActionProposal &proposal) const
{
    return proposal.type == ActionProposal::SkillCard
        && (proposal.cardClass == QStringLiteral("SurrenderCard") || proposal.cardClass == QStringLiteral("CheatCard"));
}

ActionCompileResult ActionCompiler::fail(const ActionRequestContext &ctx, const ActionProposal &proposal, const QString &reason) const
{
    const QString playerName = ctx.player != nullptr ? ctx.player->objectName() : QStringLiteral("<none>");
    qWarning("Action proposal rejected: player=%s command=%d serial=%d pattern=%s skill=%s reason=%s proposal={%s}", qPrintable(playerName), int(ctx.command),
        ctx.requestSerial, qPrintable(ctx.pattern), qPrintable(ctx.skillName), qPrintable(reason), qPrintable(proposal.diagnosticString()));

    ActionCompileResult result;
    result.error = reason;
    return result;
}
