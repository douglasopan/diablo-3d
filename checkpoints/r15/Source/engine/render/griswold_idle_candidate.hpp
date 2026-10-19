#pragma once
#include <cmath>
#include <new>
#include <utility>
#include "engine/render/ogden_idle_pilot.hpp"

namespace devilution {
// Candidate transport/sampling only: no artistic or native rig/runtime approval.
using GriswoldIdlePilotClock = OgdenIdlePilotClock;
using GriswoldIdlePilotResult = OgdenIdlePilotResult;
using GriswoldIdlePilotSample = OgdenIdlePilotSample;
inline constexpr std::string_view GriswoldIdlePilotAssetId = "tristram.actor.griswold";
inline constexpr std::string_view GriswoldIdlePilotVariant = "default";
inline constexpr std::string_view GriswoldIdlePilotRevision = "griswold-idle-specific-r1";
inline constexpr std::string_view GriswoldIdlePilotSourceSha256 = "b00a265b3e6c8ab1e52e234468102b97206910657e256ea594f60e47b55ad266";
inline constexpr std::string_view GriswoldIdlePilotClipName = "Armature|Idle|baselayer";
inline constexpr float GriswoldIdlePilotClipStart = 0.033333335071802139F;
inline constexpr float GriswoldIdlePilotClipEnd = 4.0333333015441895F;

namespace griswold_candidate_detail {
inline bool SupportedIdentity(const ActorModel &model, const ActorVisualSnapshot &native)
{
	return model.assetId == GriswoldIdlePilotAssetId && native.assetId == GriswoldIdlePilotAssetId
	    && model.variant == GriswoldIdlePilotVariant && native.variant == GriswoldIdlePilotVariant
	    && model.revision == GriswoldIdlePilotRevision && native.revision == GriswoldIdlePilotRevision
	    && model.sourceSha256 == GriswoldIdlePilotSourceSha256 && native.sourceSha256 == GriswoldIdlePilotSourceSha256;
}

inline GriswoldIdlePilotResult Reject(GriswoldIdlePilotResult result, std::string &error, const char *message)
{
	error = message;
	return result;
}
}

inline GriswoldIdlePilotResult SampleGriswoldIdlePilot(const ActorModel &model,
    const ActorVisualSnapshot &native, const GriswoldIdlePilotClock &clock,
    GriswoldIdlePilotSample &output, std::string &error)
{
	try {
		if (!griswold_candidate_detail::SupportedIdentity(model, native))
			return griswold_candidate_detail::Reject(GriswoldIdlePilotResult::UnsupportedIdentity, error, "Specific Griswold idle candidate identity/revision/source hash is unsupported; caller must fall back");
		ActorVisualSnapshot validated;
		if (!MakeActorVisualSnapshot(static_cast<const ActorVisualInput &>(native), validated, error))
			return GriswoldIdlePilotResult::InvalidSnapshot;
		if (!std::isfinite(native.nativeYawRadians)
		    || std::abs(native.nativeYawRadians - validated.nativeYawRadians) > 0.000001F)
			return griswold_candidate_detail::Reject(GriswoldIdlePilotResult::InvalidSnapshot, error, "DTO derived yaw does not match its native direction");
		if (native.kind != ActorVisualKind::Towner || native.action != ActorVisualAction::Idle
		    || native.hidden || native.localPlayer || native.reversed
		    || native.frameCount != 16 || native.ticksPerFrame != 3 || native.sequenceLength != 130)
			return griswold_candidate_detail::Reject(GriswoldIdlePilotResult::UnsupportedState, error, "Only the visible native Griswold Idle state is supported; caller must fall back");
		const bool held = native.paused || native.frozen || native.terminalHold;
		if (held && !clock.holdConfirmed)
			return griswold_candidate_detail::Reject(GriswoldIdlePilotResult::InvalidClock, error, "Held DTO requires caller-retained native ticks AND sub-tick fraction; no internal hold clock exists");
		// Leave seven mantissa bits for the DTO's 1/128-tick fraction.
		constexpr std::uint64_t MaxExactTicks = (std::uint64_t { 1 } << 46) - 1;
		if (!std::isfinite(clock.tickHz) || clock.tickHz <= 0 || clock.elapsedNativeTicks > MaxExactTicks)
			return griswold_candidate_detail::Reject(GriswoldIdlePilotResult::InvalidClock, error, "Native clock rate must be finite/positive and ticks plus 1/128 fraction must be exactly representable");
		const double seconds = (static_cast<double>(clock.elapsedNativeTicks)
		    + static_cast<double>(native.progressToNextTick128) / 128.0) / clock.tickHz;
		if (!std::isfinite(seconds))
			return griswold_candidate_detail::Reject(GriswoldIdlePilotResult::InvalidClock, error, "Native clock seconds overflowed");
		if (model.clips.size() != 1 || model.clips[0].name != GriswoldIdlePilotClipName)
			return griswold_candidate_detail::Reject(GriswoldIdlePilotResult::UnsupportedClip, error, "The frozen source Idle clip must be selected explicitly");
		const ActorClip &clip = model.clips[0];
		if (clip.startTime != GriswoldIdlePilotClipStart || clip.endTime != GriswoldIdlePilotClipEnd
		    || clip.tracks.empty())
			return griswold_candidate_detail::Reject(GriswoldIdlePilotResult::UnsupportedClip, error, "Frozen Idle authored range/tracks differ; rig clip0 is not moving Idle");
		const double span = static_cast<double>(clip.endTime) - clip.startTime;
		const double phase = std::fmod(seconds, span);
		float sourceTime = static_cast<float>(static_cast<double>(clip.startTime) + phase);
		// Rounding to float can land on the end immediately before a wrap. Keep
		// loop sampling in the authored half-open interval without changing keys.
		if (sourceTime >= clip.endTime)
			sourceTime = std::nextafter(clip.endTime, clip.startTime);
		GriswoldIdlePilotSample candidate;
		if (!EvaluateActorPose(model, 0, sourceTime, candidate.pose, error))
			return GriswoldIdlePilotResult::PoseFailure;
		candidate.native = native;
		candidate.sourceSampleSeconds = sourceTime;
		candidate.sourceLoopSeconds = span;
		candidate.elapsedNativeSeconds = seconds;
		candidate.clockHeld = held;
		output = std::move(candidate);
		error.clear();
		return GriswoldIdlePilotResult::Sampled;
	} catch (const std::bad_alloc &) {
		return griswold_candidate_detail::Reject(GriswoldIdlePilotResult::PoseFailure, error, "Private Griswold sample allocation failed");
	}
}
} // namespace devilution
