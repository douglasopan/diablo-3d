#include "engine/render/player_actor_capture.hpp"
#include <utility>
#include "player.h"
#include "tables/spelldat.h"

namespace devilution {
bool CaptureWarriorActor(const Player &player, const ActorModel &model,
    const PlayerActorCaptureContext &context, PlayerActorCapture &output, std::string &error)
{
    const auto fail=[&error](const char *message){error=message;return false;};
    if (player._pClass != HeroClass::Warrior || model.assetId != "tristram.hero.warrior")
        return fail("Warrior capture requires the matching native class and asset");
    if (context.nativeIndex >= MAX_PLRS || context.levelEpoch == 0 || context.retainedInterpolation128 > 128)
        return fail("Invalid player capture index, epoch or retained interpolation");
    ActorVisualAction action;
    switch(player._pmode) {
    case PM_STAND: action=ActorVisualAction::Idle;break;
    case PM_WALK_NORTHWARDS:
    case PM_WALK_SOUTHWARDS:
    case PM_WALK_SIDEWAYS: action=ActorVisualAction::Walk;break;
    case PM_ATTACK: action=ActorVisualAction::Attack;break;
    case PM_RATTACK: action=ActorVisualAction::RangedAttack;break;
    case PM_BLOCK: action=ActorVisualAction::Block;break;
    case PM_GOTHIT: action=ActorVisualAction::Hit;break;
    case PM_DEATH: action=ActorVisualAction::Death;break;
    case PM_SPELL: action=ActorVisualAction::Spell;break;
    default:return fail("Player transition/unsupported mode must retain native presentation");
    }
    const auto &animation=player.AnimInfo;
    const auto phase=animation.sampleRenderPhase(context.retainedInterpolation128);
    if (!phase.valid) return fail("Native animation has no valid visual phase");
    ActorVisualInput input;
    input.kind=ActorVisualKind::Player;input.nativeIndex=context.nativeIndex;input.action=action;
    input.assetId=model.assetId;input.variant=model.variant;input.revision=model.revision;input.sourceSha256=model.sourceSha256;
    input.authoritativeFootpoint=context.authoritativeFootpoint;
    input.nativeDirection=static_cast<std::uint8_t>(player._pdir);
    input.logicalFrame=animation.currentFrame;input.displayedFrame=phase.frame;
    input.frameCount=animation.numberOfFrames;input.ticksPerFrame=animation.ticksPerFrame;
    input.tickCounter=animation.tickCounterOfCurrentFrame;
    input.progressToNextTick128=context.retainedInterpolation128;
    input.paused=context.paused;input.frozen=animation.isPetrified;
    input.terminalHold=action==ActorVisualAction::Death&&animation.isLastFrame();
    input.localPlayer=context.localPlayer;input.hidden=context.hidden;
    if (action==ActorVisualAction::Attack||action==ActorVisualAction::RangedAttack) input.nativeImpactFrame=player._pAFNum-1;
    // DoSpell compares its zero-based currentFrame directly with _pSFNum;
    // unlike DoAttack, it does not subtract one before emitting CastSpell.
    if (action==ActorVisualAction::Spell) input.nativeEmissionFrame=player._pSFNum;
    PlayerActorCapture result;
    if (action == ActorVisualAction::Spell) {
        const auto spellIndex = static_cast<int>(player.executedSpell.spellId);
        if (spellIndex <= static_cast<int>(SpellID::Null)
            || spellIndex > static_cast<int>(SpellID::LAST)
            || static_cast<std::size_t>(spellIndex) >= SpellsData.size())
            return fail("Executing spell has no valid native spell metadata");
        // Player::getGraphic() is the native read-only selector. In PM_SPELL it
        // reads executedSpell, including flags loaded from the native table.
        switch (player.getGraphic()) {
        case player_graphic::Fire: result.nativeSpellGraphic = ActorPlayerSpellGraphic::Fire; break;
        case player_graphic::Lightning: result.nativeSpellGraphic = ActorPlayerSpellGraphic::Lightning; break;
        case player_graphic::Magic: result.nativeSpellGraphic = ActorPlayerSpellGraphic::Magic; break;
        default: return fail("Executing spell has no supported native graphic");
        }
    }
    if(!MakeActorVisualSnapshot(input,result.snapshot,error))return false;
    result.renderPhase=phase;result.levelEpoch=context.levelEpoch;
    result.nativeWeaponGraphic=player._pgfxnum&0xF;
    result.nativeArmorGraphic=player._pgfxnum>>4;
    if(result.nativeWeaponGraphic>static_cast<std::uint8_t>(PlayerWeaponGraphic::Staff)||result.nativeArmorGraphic>2)
        return fail("Native equipment graphic outside validated Warrior range");
    output=std::move(result);error.clear();return true;
}
} // namespace devilution
