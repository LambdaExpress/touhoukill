#ifndef _yuritest_H
#define _yuritest_H

#include "card.h"
#include "package.h"

class XinkongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XinkongCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override;
    void onEffect(const CardEffectStruct &effect) const override;
};

class YuriTestPackage : public Package
{
    Q_OBJECT

public:
    YuriTestPackage();
};

#endif
