#pragma once

#include <cstdint>
#include <string>

#include "engine/render/actor_model.hpp"

namespace devilution {

enum class ActorVisualKind : std::uint8_t { Player, Towner, Monster, ChargeMissile };

enum class ActorVisualAction : std::uint8_t {
	Idle, Walk, Attack, RangedAttack, Spell, Hit, Block, Death,
	Special, Fade, Heal, Charge, Other,
};

// Read-only values supplied by a future native adapter. This module does not
// inspect globals, mutate a Player/Monster/Towner, emit events or advance RNG.
struct ActorVisualInput {
	ActorVisualKind kind = ActorVisualKind::Player;
	std::uint32_t nativeIndex = 0;
	ActorVisualAction action = ActorVisualAction::Idle;
	std::string assetId;
	std::string variant;
	std::string revision;
	std::string sourceSha256;
	ActorVec3 authoritativeFootpoint;
	std::uint8_t nativeDirection = 0;
	std::int32_t logicalFrame = 0;
	std::int32_t displayedFrame = 0;
	std::uint32_t frameCount = 1;
	std::uint32_t ticksPerFrame = 1;
	std::uint32_t tickCounter = 0;
	std::uint16_t progressToNextTick128 = 0;
	std::int32_t nativeImpactFrame = -1;
	std::int32_t nativeEmissionFrame = -1;
	// NPCs may traverse a repeated/reversed order; its phase is not frame/N.
	std::uint32_t sequenceIndex = 0;
	std::uint32_t sequenceLength = 0;
	bool paused = false;
	bool frozen = false;
	bool reversed = false;
	bool terminalHold = false;
	bool hidden = false;
	bool localPlayer = false;
};

struct ActorVisualSnapshot : ActorVisualInput {
	float nativeYawRadians = 0;
};

// Atomic validation/copy only. Source authority and native frame conventions
// remain with the caller; a clock or GLB never advances this snapshot.
bool MakeActorVisualSnapshot(const ActorVisualInput &input, ActorVisualSnapshot &output, std::string &error);

// +Z is asset front, +X/+Z are map axes. Native enum order is South,
// SouthWest, West, NorthWest, North, NorthEast, East, SouthEast.
float ActorNativeDirectionYaw(std::uint8_t direction);

// Draw-only visibility decision: other players and native picking are untouched.
bool ActorVisibleInCamera(const ActorVisualSnapshot &snapshot, bool firstPerson, bool hideLocalBody);

} // namespace devilution
