#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "engine/render/actor_pose.hpp"
#include "engine/render/actor_visual_snapshot.hpp"

namespace devilution {

// Private CPU transport pilot. No native adapter, renderer or installation.
inline constexpr std::string_view OgdenIdlePilotAssetId = "tristram.actor.ogden";
inline constexpr std::string_view OgdenIdlePilotVariant = "default";
inline constexpr std::string_view OgdenIdlePilotRevision = "ogden-idle-transport-r1-20261009";
inline constexpr std::string_view OgdenIdlePilotSourceSha256 = "ff6bed48aa22ea8cbd415e1ba5931a397189cb09b5b81acc61e826314f0e306f";
inline constexpr std::string_view OgdenIdlePilotClipName = "Armature|Idle|baselayer";
inline constexpr float OgdenIdlePilotClipStart = 0.03333333507180214F;
inline constexpr float OgdenIdlePilotClipEnd = 4.0333333015441895F;
// Reference only: this 111-entry native order is NOT the source clip period.
inline constexpr double OgdenNativeIdleSequenceSeconds = 111.0 * 3.0 / 20.0;

struct OgdenIdlePilotClock {
	// Elapsed native ticks from an origin chosen and retained by the caller.
	// This helper never obtains time, increments ticks or chooses that origin.
	std::uint64_t elapsedNativeTicks = 0;
	double tickHz = 20;
	// Required for paused/frozen/terminalHold: caller confirms that BOTH ticks
	// and snapshot.progressToNextTick128 have been retained at the hold phase.
	// A stateless helper cannot verify retention against a previous call.
	bool holdConfirmed = false;
};

enum class OgdenIdlePilotResult : std::uint8_t {
	Sampled,
	UnsupportedIdentity,
	UnsupportedState,
	InvalidSnapshot,
	InvalidClock,
	UnsupportedClip,
	PoseFailure,
};

struct OgdenIdlePilotSample {
	// Exact input DTO copy, including native direction, sequence and footpoint.
	ActorVisualSnapshot native;
	// Source asset-space skin only: worldJoint * IBM, no actor/world placement.
	ActorPose pose;
	float sourceSampleSeconds = 0;
	double sourceLoopSeconds = 0;
	double elapsedNativeSeconds = 0;
	bool clockHeld = false;
};

// Preconditions: immutable model from LoadActorModel; caller verifies the
// candidate file bytes/hash before loading. Identity strings alone do not prove
// those bytes. The DTO is revalidated defensively, including its derived yaw.
// Only visible Ogden Towner Idle/default with the frozen revision/hash and native
// 16-frame, 3-tick, 111-entry contract is supported. Reversed or localPlayer DTOs
// fall back; held Idle phases are supported only under the clock contract above.
// Sample = clip.start + fmod((ticks + progress128/128)/tickHz, end-start).
// End-start is about 4.0 s, not 4.033333 s and never 16.65 s.
// Any non-Sampled result requires caller fallback and leaves output unchanged.
// No globals, RNG, gameplay events, root-motion removal, fit or renderer calls.
OgdenIdlePilotResult SampleOgdenIdlePilot(const ActorModel &model,
    const ActorVisualSnapshot &native, const OgdenIdlePilotClock &clock,
    OgdenIdlePilotSample &output, std::string &error);

} // namespace devilution
