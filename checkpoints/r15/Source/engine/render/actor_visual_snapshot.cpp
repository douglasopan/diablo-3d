#include "engine/render/actor_visual_snapshot.hpp"

#include <array>
#include <cmath>
#include <utility>

namespace devilution {

float ActorNativeDirectionYaw(std::uint8_t direction)
{
	constexpr float Pi = 3.14159265358979323846F;
	// Source/engine/displacement.hpp Direction construction, glTF +Z front.
	constexpr std::array<float, 8> Yaw { Pi / 4, 0, -Pi / 4, -Pi / 2, -3 * Pi / 4, Pi, 3 * Pi / 4, Pi / 2 };
	return direction < Yaw.size() ? Yaw[direction] : 0;
}

bool MakeActorVisualSnapshot(const ActorVisualInput &input, ActorVisualSnapshot &output, std::string &error)
{
	const auto reject = [&error](const char *message) { error = message; return false; };
	if (static_cast<std::uint8_t>(input.kind) > static_cast<std::uint8_t>(ActorVisualKind::ChargeMissile)
	    || static_cast<std::uint8_t>(input.action) > static_cast<std::uint8_t>(ActorVisualAction::Other))
		return reject("Invalid visual identity or action");
	if (input.nativeDirection >= 8 || !std::isfinite(input.authoritativeFootpoint.x)
	    || !std::isfinite(input.authoritativeFootpoint.y) || !std::isfinite(input.authoritativeFootpoint.z))
		return reject("Invalid native direction or authoritative footpoint");
	if (input.assetId.empty() || input.variant.empty() || input.revision.empty()
	    || input.assetId.size() > 127 || input.variant.size() > 127 || input.revision.size() > 127
	    || input.sourceSha256.size() != 64)
		return reject("Explicit asset identity, revision and source hash are required");
	for (const char value : input.sourceSha256) {
		if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
			return reject("Source hash must be lowercase SHA256 hex");
	}
	if (input.frameCount == 0 || input.frameCount > 100000 || input.ticksPerFrame == 0
	    || input.ticksPerFrame > 100000 || input.tickCounter >= input.ticksPerFrame
	    || input.progressToNextTick128 > 128 || input.logicalFrame < -8
	    || input.logicalFrame >= static_cast<std::int32_t>(input.frameCount)
	    || input.displayedFrame < 0 || input.displayedFrame >= static_cast<std::int32_t>(input.frameCount))
		return reject("Invalid native animation phase");
	if (input.nativeImpactFrame < -1 || input.nativeEmissionFrame < -1
	    || input.nativeImpactFrame >= static_cast<std::int32_t>(input.frameCount)
	    || input.nativeEmissionFrame >= static_cast<std::int32_t>(input.frameCount))
		return reject("Visual marker must use a reachable native frame or -1");
	if ((input.sequenceLength == 0 && input.sequenceIndex != 0)
	    || (input.sequenceLength > 0 && input.sequenceIndex >= input.sequenceLength))
		return reject("Invalid NPC sequence phase");
	ActorVisualSnapshot result;
	static_cast<ActorVisualInput &>(result) = input;
	result.nativeYawRadians = ActorNativeDirectionYaw(input.nativeDirection);
	output = std::move(result);
	error.clear();
	return true;
}

bool ActorVisibleInCamera(const ActorVisualSnapshot &snapshot, bool firstPerson, bool hideLocalBody)
{
	return !snapshot.hidden && !(firstPerson && hideLocalBody
	    && snapshot.kind == ActorVisualKind::Player && snapshot.localPlayer);
}

} // namespace devilution
