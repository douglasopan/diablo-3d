#include "engine/render/ogden_idle_pilot.hpp"

#include <cmath>
#include <new>
#include <utility>

namespace devilution {
namespace {

bool SupportedIdentity(const ActorModel &model, const ActorVisualSnapshot &native)
{
	return model.assetId == OgdenIdlePilotAssetId && native.assetId == OgdenIdlePilotAssetId
	    && model.variant == OgdenIdlePilotVariant && native.variant == OgdenIdlePilotVariant
	    && model.revision == OgdenIdlePilotRevision && native.revision == OgdenIdlePilotRevision
	    && model.sourceSha256 == OgdenIdlePilotSourceSha256 && native.sourceSha256 == OgdenIdlePilotSourceSha256;
}

OgdenIdlePilotResult Reject(OgdenIdlePilotResult result, std::string &error, const char *message)
{
	error = message;
	return result;
}

} // namespace

OgdenIdlePilotResult SampleOgdenIdlePilot(const ActorModel &model,
    const ActorVisualSnapshot &native, const OgdenIdlePilotClock &clock,
    OgdenIdlePilotSample &output, std::string &error)
{
	try {
		if (!SupportedIdentity(model, native))
			return Reject(OgdenIdlePilotResult::UnsupportedIdentity, error, "Private Ogden idle identity/revision/source hash is unsupported; caller must fall back");
		ActorVisualSnapshot validated;
		if (!MakeActorVisualSnapshot(static_cast<const ActorVisualInput &>(native), validated, error))
			return OgdenIdlePilotResult::InvalidSnapshot;
		if (!std::isfinite(native.nativeYawRadians)
		    || std::abs(native.nativeYawRadians - validated.nativeYawRadians) > 0.000001F)
			return Reject(OgdenIdlePilotResult::InvalidSnapshot, error, "DTO derived yaw does not match its native direction");
		if (native.kind != ActorVisualKind::Towner || native.action != ActorVisualAction::Idle
		    || native.hidden || native.localPlayer || native.reversed
		    || native.frameCount != 16 || native.ticksPerFrame != 3 || native.sequenceLength != 111)
			return Reject(OgdenIdlePilotResult::UnsupportedState, error, "Only the visible native Ogden Idle state is supported; caller must fall back");
		const bool held = native.paused || native.frozen || native.terminalHold;
		if (held && !clock.holdConfirmed)
			return Reject(OgdenIdlePilotResult::InvalidClock, error, "Held DTO requires caller-retained native ticks AND sub-tick fraction; no internal hold clock exists");
		// Leave seven mantissa bits for the DTO's 1/128-tick fraction.
		constexpr std::uint64_t MaxExactTicks = (std::uint64_t { 1 } << 46) - 1;
		if (!std::isfinite(clock.tickHz) || clock.tickHz <= 0 || clock.elapsedNativeTicks > MaxExactTicks)
			return Reject(OgdenIdlePilotResult::InvalidClock, error, "Native clock rate must be finite/positive and ticks plus 1/128 fraction must be exactly representable");
		const double seconds = (static_cast<double>(clock.elapsedNativeTicks)
		    + static_cast<double>(native.progressToNextTick128) / 128.0) / clock.tickHz;
		if (!std::isfinite(seconds))
			return Reject(OgdenIdlePilotResult::InvalidClock, error, "Native clock seconds overflowed");
		if (model.clips.size() != 1 || model.clips[0].name != OgdenIdlePilotClipName)
			return Reject(OgdenIdlePilotResult::UnsupportedClip, error, "The frozen source Idle clip must be selected explicitly");
		const ActorClip &clip = model.clips[0];
		if (clip.startTime != OgdenIdlePilotClipStart || clip.endTime != OgdenIdlePilotClipEnd
		    || clip.tracks.empty())
			return Reject(OgdenIdlePilotResult::UnsupportedClip, error, "Frozen Idle authored range/tracks differ; rig clip0 is not moving Idle");
		const double span = static_cast<double>(clip.endTime) - clip.startTime;
		const double phase = std::fmod(seconds, span);
		float sourceTime = static_cast<float>(static_cast<double>(clip.startTime) + phase);
		// Rounding to float can land on the end immediately before a wrap. Keep
		// loop sampling in the authored half-open interval without changing keys.
		if (sourceTime >= clip.endTime)
			sourceTime = std::nextafter(clip.endTime, clip.startTime);
		OgdenIdlePilotSample candidate;
		if (!EvaluateActorPose(model, 0, sourceTime, candidate.pose, error))
			return OgdenIdlePilotResult::PoseFailure;
		candidate.native = native;
		candidate.sourceSampleSeconds = sourceTime;
		candidate.sourceLoopSeconds = span;
		candidate.elapsedNativeSeconds = seconds;
		candidate.clockHeld = held;
		output = std::move(candidate);
		error.clear();
		return OgdenIdlePilotResult::Sampled;
	} catch (const std::bad_alloc &) {
		return Reject(OgdenIdlePilotResult::PoseFailure, error, "Private Ogden sample allocation failed");
	}
}

} // namespace devilution
