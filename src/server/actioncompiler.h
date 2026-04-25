#ifndef _ACTIONCOMPILER_H
#define _ACTIONCOMPILER_H

#include "structs.h"

class Room;

struct ActionCompileResult
{
    ActionCompileResult();

    bool success;
    bool cancelled;
    QString error;
    CardUseStruct cardUse;
};

class ActionCompiler
{
public:
    explicit ActionCompiler(Room *room);

    ActionCompileResult compile(const ActionRequestContext &ctx, const ActionProposal &proposal) const;

private:
    bool resolveTargets(const ActionProposal &proposal, QList<ServerPlayer *> *targets, QString *error) const;
    const Card *compileRealCard(const ActionProposal &proposal, QString *error) const;
    const Card *compileSelectedCards(const ActionRequestContext &ctx, const ActionProposal &proposal, QString *error) const;
    const Card *compileStandaloneSkillCard(const ActionProposal &proposal, QString *error) const;
    const Card *compileViewAsCard(const ActionRequestContext &ctx, const ActionProposal &proposal, QString *error) const;
    bool isStandaloneSpecialSkillCard(const ActionProposal &proposal) const;
    ActionCompileResult fail(const ActionRequestContext &ctx, const ActionProposal &proposal, const QString &reason) const;

    Room *m_room;
};

#endif
