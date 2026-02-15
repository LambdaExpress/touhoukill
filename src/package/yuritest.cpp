#include "yuritest.h"

#include "general.h"
#include "room.h"
#include "skill.h"

namespace {
const char *const XinkongMark = "@xinkong";
const char *const XinkongControllerTag = "xinkong_controller";

// Remove the mind-control mark and tag from a target player.
static void clearXinkongState(Room *room, ServerPlayer *target)
{
    if (room == nullptr || target == nullptr)
        return;

    if (target->getMark(XinkongMark) > 0)
        room->setPlayerMark(target, XinkongMark, 0);
    target->tag.remove(XinkongControllerTag);
}

// Clean up all mind-control marks issued by a specific controller,
// and release the control relation if active.
static void clearXinkongByController(Room *room, ServerPlayer *controller)
{
    if (room == nullptr || controller == nullptr)
        return;

    const QString controllerName = controller->objectName();
    foreach (ServerPlayer *p, room->getAllPlayers(true)) {
        if (p->tag.value(XinkongControllerTag).toString() == controllerName)
            clearXinkongState(room, p);
    }

    room->clearControlRelation(controller);
}
} // anonymous namespace

// -- XinkongCard --

XinkongCard::XinkongCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool XinkongCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    // Select exactly one other alive player who is not already mind-controlled.
    return targets.isEmpty() && to_select != Self && to_select->isAlive() && to_select->getMark(XinkongMark) == 0;
}

void XinkongCard::onEffect(const CardEffectStruct &effect) const
{
    if (effect.from == nullptr || effect.to == nullptr)
        return;

    Room *room = effect.from->getRoom();
    room->setPlayerMark(effect.to, XinkongMark, 1);
    effect.to->tag[XinkongControllerTag] = effect.from->objectName();
}

// -- Xinkong (proactive ViewAsSkill) --

class Xinkong : public ZeroCardViewAsSkill
{
public:
    Xinkong()
        : ZeroCardViewAsSkill("xinkong")
    {
    }

    bool isEnabledAtPlay(const Player *player) const override
    {
        return !player->hasUsed("XinkongCard");
    }

    const Card *viewAs() const override
    {
        return new XinkongCard;
    }
};

// -- XinkongRecord (global TriggerSkill for control lifecycle) --

class XinkongRecord : public TriggerSkill
{
public:
    XinkongRecord()
        : TriggerSkill("#xinkong-record")
    {
        events = { TurnStart, EventPhaseStart, EventLoseSkill, Death };
        global = true;
    }

    void record(TriggerEvent triggerEvent, Room *room, QVariant &data) const override
    {
        if (triggerEvent == TurnStart) {
            ServerPlayer *current = data.value<ServerPlayer *>();
            if (current == nullptr || current->getMark(XinkongMark) == 0)
                return;

            QString controllerName = current->tag.value(XinkongControllerTag).toString();
            if (controllerName.isEmpty()) {
                clearXinkongState(room, current);
                return;
            }

            ServerPlayer *controller = room->findPlayerByObjectName(controllerName, true);
            if (controller == nullptr || !controller->isAlive() || !controller->hasSkill("xinkong")) {
                clearXinkongState(room, current);
                return;
            }

            // Establish control: the controller takes over the target's operations.
            room->setControlRelation(controller, current);
            return;
        }

        if (triggerEvent == EventPhaseStart) {
            ServerPlayer *current = data.value<ServerPlayer *>();
            if (current == nullptr || current->getPhase() != Player::NotActive)
                return;

            if (current->getMark(XinkongMark) == 0 && !current->tag.contains(XinkongControllerTag))
                return;

            // Turn is ending: release control and remove the mark.
            QString controllerName = current->tag.value(XinkongControllerTag).toString();
            if (!controllerName.isEmpty()) {
                ServerPlayer *controller = room->findPlayerByObjectName(controllerName, true);
                if (controller != nullptr)
                    room->clearControlRelation(controller);
            }

            clearXinkongState(room, current);
            return;
        }

        if (triggerEvent == EventLoseSkill) {
            SkillAcquireDetachStruct detach = data.value<SkillAcquireDetachStruct>();
            if (detach.player != nullptr && detach.skill != nullptr && !detach.isAcquire && detach.skill->objectName() == "xinkong")
                clearXinkongByController(room, detach.player);
            return;
        }

        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who == nullptr)
                return;

            // If the dead player was a controller, clean up all their marks.
            clearXinkongByController(room, death.who);

            // If the dead player was a target, remove their own mark.
            clearXinkongState(room, death.who);
        }
    }

    // This skill only uses record(); no triggerable/effect needed.
    QList<SkillInvokeDetail> triggerable(TriggerEvent, const Room *, const QVariant &) const override
    {
        return {};
    }
};

// -- Package registration --

YuriTestPackage::YuriTestPackage()
    : Package("yuritest")
{
    General *yuriTest = new General(this, "yuri_test", "touhougod", 10, true);
    yuriTest->addSkill(new Xinkong);

    addMetaObject<XinkongCard>();
    skills << new XinkongRecord;
    related_skills.insertMulti("xinkong", "#xinkong-record");
}

ADD_PACKAGE(YuriTest)
