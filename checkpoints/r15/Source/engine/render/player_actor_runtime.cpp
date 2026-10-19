#include "engine/render/player_actor_runtime.hpp"
#include "engine/render/world_grid_metric.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

#include "engine/render/player_actor_runtime_profile.hpp"
#include "player.h"

namespace devilution {
namespace {
bool Fail(std::string &error, const char *message) { error = message; return false; }
bool PilotWorld(const WarriorWorldIdentity &world)
{
    return !world.setLevel && !world.expansion && !world.shareware
        && ((world.kind == WarriorWorldKind::Town && world.levelNumber == 0)
            || (world.kind == WarriorWorldKind::Cathedral && world.levelNumber == 1));
}
std::size_t Clip(const PlayerActorCapture &capture)
{
    switch (capture.snapshot.action) {
    case ActorVisualAction::Death: return 0;
    case ActorVisualAction::Block: return 1;
    case ActorVisualAction::Spell:
        switch (capture.nativeSpellGraphic) {
        case ActorPlayerSpellGraphic::Fire: return 2;
        case ActorPlayerSpellGraphic::Lightning: return 3;
        case ActorPlayerSpellGraphic::Magic: return 4;
        default: return 11;
        }
    case ActorVisualAction::Idle: return 5;
    case ActorVisualAction::Walk: return 6;
    case ActorVisualAction::Attack: return 7;
    case ActorVisualAction::Hit: return 8;
    default: return 11; // No ranged/transition clip is invented.
    }
}
bool Equipment(const PlayerActorLifecycleSnapshot &snapshot,
    std::array<PlayerActorEquippedHand, 2> &output, std::string &error)
{
    output[0].hand = PlayerActorHand::Right;
    output[1].hand = PlayerActorHand::Left;
    for (const auto slot : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
        const auto &item = snapshot.equipment.slots[slot];
        if (!item.occupied || !item.statFlag) continue;
        const bool shield = item.itemType == static_cast<int>(ItemType::Shield);
        auto &hand = output[shield ? 1 : 0];
        if (hand.occupied) return Fail(error, "Human Warrior pilot cannot represent ambiguous dual-hand equipment");
        hand.nativeSlot = slot; hand.nativeItemId = item.itemId;
        hand.inventoryCursor = item.cursor; hand.occupied = true; hand.usable = true;
    }
    return true;
}
} // namespace

bool PlayerActorNativeRuntime::Load(const std::array<std::span<const std::byte>, 3> &packets, std::string &error)
{
    try {
        const auto bindings = PlayerActorHumanPilotBindings();
        std::array<PlayerActorResourceClipRole, 11> ledger;
        for (std::size_t i = 0; i < ledger.size(); ++i)
            ledger[i] = { std::string(PlayerActorHumanPilotClips[i].role), i };
        PlayerActorResourceLoadInput input;
        input.body = { packets[0], PlayerActorHumanPilotResources[0].Expected() };
        input.sword = { packets[1], PlayerActorHumanPilotResources[1].Expected() };
        input.buckler = { packets[2], PlayerActorHumanPilotResources[2].Expected() };
        input.ledger = ledger; input.bindings = bindings;
        input.loaderGeneration = resources_.generation() + 1;
        if (input.loaderGeneration == 0) return Fail(error, "Human Warrior resource generation exhausted");
        // Owned bounded transport only: no review, coverage or authorization
        // claims are injected into its diagnostic manifest.
        PlayerActorResources next;
        if (!LoadPlayerActorResourcesDiagnostic(input, next, error)
            || !MatchesPlayerActorHumanPilotBodyLayout(*next.Body())) return false;
        resources_ = std::move(next);
        error.clear(); return true;
    } catch (const std::bad_alloc &) { return Fail(error, "Human Warrior resource allocation failed"); }
}

bool PlayerActorNativeRuntime::Prepare(const Player &player, const PlayerActorLifecycle &lifecycle,
    const PlayerActorNativeContext &context, const PlayerActorInventorySpriteResolver &sprites,
    PlayerActorNativePilotFrame &output, std::string &error) const
{
    const PlayerActorResources owned = resources_;
    if (owned.empty() || !PilotWorld(context.world) || player._pClass != HeroClass::Warrior
        || !player.plractive || !player.isOnActiveLevel() || context.nativeIndex >= MAX_PLRS
        || (context.worldBodyHidden && !context.firstPerson && !context.worldShadowBodyRequested)
        || (context.firstPerson && (!context.localPlayer || player.hasNoLife())))
        return Fail(error, "Human Warrior pilot resources, visibility or limited world domain unavailable");
    try {
        PlayerActorNativePilotFrame next;
        next.resourceOwner = owned;
        next.resourceGeneration = owned.generation();
        next.nativeTileX = player.position.tile.x; next.nativeTileY = player.position.tile.y;
        next.nativeHitPoints = player._pHitPoints;
        PlayerActorLifecycleError lifecycleError;
        if (!lifecycle.ReadSnapshot(player, context.nativeIndex, context.world, next.lifecycle, &lifecycleError)) {
            error = PlayerActorLifecycleErrorText(lifecycleError); return false;
        }
        if (next.lifecycle.currentNativeEquipmentArmor != WarriorArmor::Light)
            return Fail(error, "Human Warrior pilot preserves native fallback for other armor");
        if (!CaptureWarriorActor(player, *owned.Body(),
                { context.nativeIndex, next.lifecycle.levelEpoch, context.footpoint, context.retainedInterpolation128,
                    context.paused, context.localPlayer, context.worldBodyHidden }, next.capture, error)) return false;
        const auto clip = Clip(next.capture);
        if (clip >= PlayerActorHumanPilotClips.size()) return Fail(error, "Human Warrior pilot action has no authored clip");
        if (!Equipment(next.lifecycle, next.equipment, error)) return false;
        // The authored floor and rig units are retained. Any camera-only FP
        // presentation transform is applied by the renderer to the complete
        // selected body/eye landmarks and attachments; native placement is unchanged.
        // Real idle pose height is 0.96368939 authored units. Uniform 2 makes
        // it 1.92738 town units and the head 1.7059; floor/root stay authored.
        next.fit = { { 0, 0, 0 }, PhysicalToNativeGrid(2), 0 };
        const auto &authored = PlayerActorHumanPilotClips[clip];
        ActorPlayerPhaseBinding phase;
        phase.loop = next.capture.snapshot.action == ActorVisualAction::Idle || next.capture.snapshot.action == ActorVisualAction::Walk;
        phase.authoredImpactPhase = authored.authoredImpactPhase;
        phase.authoredEmissionPhase = authored.authoredEmissionPhase;
        phase.nativeRenderedSubframe = next.capture.renderPhase.subframe;
        if (phase.authoredEmissionPhase != -1
            && (next.capture.snapshot.nativeEmissionFrame <= 0
                || next.capture.snapshot.nativeEmissionFrame >= static_cast<int>(next.capture.snapshot.frameCount - 1)))
            phase.authoredEmissionPhase = -1; // Native emission still solely owns gameplay.
        std::array<ActorPlayerAttachmentInput, 2> selected;
        std::size_t selectedCount = 0;
        const auto rigid = owned.DiagnosticAttachments();
        for (std::size_t i = 0; i < next.equipment.size(); ++i) {
            const auto &hand = next.equipment[i];
            if (hand.occupied && hand.nativeItemId == (i == 0 ? 1 : 2)) selected[selectedCount++] = rigid[i];
        }
        if (!PrepareActorPlayerFrame(*owned.Body(), next.capture, next.lifecycle.levelEpoch,
                context.nativeIndex, clip, phase, next.fit, { selected.data(), selectedCount }, next.geometry, error)) return false;
        std::array<PlayerActorFirstPersonSocket, 2> sockets;
        for (std::size_t i = 0; i < sockets.size(); ++i) {
            const auto &socket = PlayerActorHumanPilotSockets[i];
            sockets[i] = { i == 0 ? PlayerActorHand::Right : PlayerActorHand::Left, std::string(socket.bone), socket.local };
        }
        // Same skinned BODY, through the shoulders to the authored hand/socket.
        // Mixed torso seams are excluded by the existing exclusive weight mask.
        const PlayerActorFirstPersonConfig config { "mixamorig:LeftShoulder", "mixamorig:RightShoulder",
            next.fit, next.capture.snapshot.nativeYawRadians, context.footpoint };
        if (!PreparePlayerActorFirstPerson(*owned.Body(), next.geometry, next.equipment, sockets,
                sprites, config, next.hands, error)) return false;
        next.firstPerson = context.firstPerson; next.worldBodyHidden = context.worldBodyHidden;
        if (!IsCurrent(player, lifecycle, next)) return Fail(error, "Human Warrior native pose/lifecycle changed during preparation");
        output = std::move(next); error.clear(); return true;
    } catch (const std::bad_alloc &) { return Fail(error, "Human Warrior frame allocation failed"); }
}

bool PlayerActorNativeRuntime::IsCurrent(const Player &player, const PlayerActorLifecycle &lifecycle,
    const PlayerActorNativePilotFrame &frame) const
{
    if (!loaded() || frame.resourceGeneration != resources_.generation()
        || resources_.Body() != frame.resourceOwner.Body()
        || !lifecycle.IsCurrent(player, frame.lifecycle)
        || player.position.tile.x != frame.nativeTileX || player.position.tile.y != frame.nativeTileY
        || player._pHitPoints != frame.nativeHitPoints) return false;
    PlayerActorCapture current;
    std::string error;
    const auto &old = frame.capture;
    const auto &snapshot = old.snapshot;
    if (snapshot.progressToNextTick128 > 128) return false;
    if (!CaptureWarriorActor(player, *resources_.Body(),
            { snapshot.nativeIndex, old.levelEpoch, snapshot.authoritativeFootpoint,
                static_cast<std::uint8_t>(snapshot.progressToNextTick128), snapshot.paused, snapshot.localPlayer, snapshot.hidden }, current, error)) return false;
    const auto &now = current.snapshot;
    return now.action == snapshot.action && now.nativeDirection == snapshot.nativeDirection
        && now.logicalFrame == snapshot.logicalFrame && now.displayedFrame == snapshot.displayedFrame
        && now.frameCount == snapshot.frameCount && now.ticksPerFrame == snapshot.ticksPerFrame
        && now.tickCounter == snapshot.tickCounter && now.frozen == snapshot.frozen
        && now.terminalHold == snapshot.terminalHold && now.nativeImpactFrame == snapshot.nativeImpactFrame
        && now.nativeEmissionFrame == snapshot.nativeEmissionFrame
        && current.nativeWeaponGraphic == old.nativeWeaponGraphic && current.nativeArmorGraphic == old.nativeArmorGraphic
        && current.nativeSpellGraphic == old.nativeSpellGraphic
        && current.renderPhase.frame == old.renderPhase.frame && current.renderPhase.subframe == old.renderPhase.subframe;
}
} // namespace devilution
