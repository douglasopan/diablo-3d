#pragma once
#include <cstdint>
#include <string>
#include "engine/animation_render_phase.hpp"
#include "engine/render/actor_visual_snapshot.hpp"

namespace devilution {
struct Player;

enum class ActorPlayerSpellGraphic : std::uint8_t {
    None,
    Fire,
    Lightning,
    Magic,
};

struct PlayerActorCaptureContext {
    std::uint32_t nativeIndex = 0;
    std::uint64_t levelEpoch = 0;
    ActorVec3 authoritativeFootpoint;
    std::uint8_t retainedInterpolation128 = 0;
    bool paused = false;
    bool localPlayer = false;
    bool hidden = false;
};

struct PlayerActorCapture {
    ActorVisualSnapshot snapshot;
    AnimationRenderPhase renderPhase;
    std::uint64_t levelEpoch = 0;
    std::uint8_t nativeWeaponGraphic = 0;
    std::uint8_t nativeArmorGraphic = 0;
    // Native graphical family of the spell currently being executed. A queued
    // next cast must not replace this discriminator. None for all other actions.
    ActorPlayerSpellGraphic nativeSpellGraphic = ActorPlayerSpellGraphic::None;
};

// Read-only Warrior adapter. Caller supplies placement from the same native
// frame and owns epoch/cache invalidation. Capture does not approve a model,
// resolve missing clips, replace sprites or change any player state.
bool CaptureWarriorActor(const Player &player, const ActorModel &selectedModel,
    const PlayerActorCaptureContext &context, PlayerActorCapture &output, std::string &error);
} // namespace devilution
