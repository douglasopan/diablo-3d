#include "controls/town_first_person_input.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace devilution {
namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr float PitchLimit = 1.4F;
constexpr float DefaultSensitivity = 0.006F;
constexpr float DefaultHysteresis = 0.05235987756F;

bool IsDirection(Direction direction)
{
	return static_cast<unsigned>(direction) < 8;
}

float Bounded(float value, float fallback, float minimum, float maximum)
{
	return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

uint8_t CanonicalMovementKeys(uint8_t physicalKeys)
{
	return static_cast<uint8_t>(
	    ((physicalKeys & (TownFirstPersonArrowUp | TownFirstPersonKeyW)) != 0 ? TownFirstPersonArrowUp : 0)
	    | ((physicalKeys & (TownFirstPersonArrowDown | TownFirstPersonKeyS)) != 0 ? TownFirstPersonArrowDown : 0)
	    | ((physicalKeys & (TownFirstPersonArrowLeft | TownFirstPersonKeyA)) != 0 ? TownFirstPersonArrowLeft : 0)
	    | ((physicalKeys & (TownFirstPersonArrowRight | TownFirstPersonKeyD)) != 0 ? TownFirstPersonArrowRight : 0));
}

void ClearHeld(TownFirstPersonInputResult &result)
{
	result.stopOwnWalk = result.stopOwnWalk || IsDirection(result.nextState.lastDirection);
	result.nextState.heldKeys = 0;
	result.nextState.blockedKeys = 0;
	result.nextState.lastDirectionKeys = 0;
	result.nextState.lastDirection = Direction::NoDirection;
	result.direction = Direction::NoDirection;
	result.clearKeys = true;
	result.discardMouseDelta = true;
}

struct NativeHeading {
	Direction direction;
	double x;
	double z;
};

// Native tile deltas from engine/displacement.hpp, normalized only for angular
// comparison. The runtime still performs exactly one native movement intent.
constexpr double Diagonal = 0.70710678118654752440;
constexpr std::array<NativeHeading, 8> Headings { {
	{ Direction::South, Diagonal, Diagonal },
	{ Direction::SouthWest, 0, 1 },
	{ Direction::West, -Diagonal, Diagonal },
	{ Direction::NorthWest, -1, 0 },
	{ Direction::North, -Diagonal, -Diagonal },
	{ Direction::NorthEast, 0, -1 },
	{ Direction::East, Diagonal, -Diagonal },
	{ Direction::SouthEast, 1, 0 },
} };

Direction Quantize(double x, double z, const TownFirstPersonInputState &previous,
    uint8_t heldKeys, double hysteresis)
{
	if (heldKeys == previous.lastDirectionKeys && IsDirection(previous.lastDirection)) {
		const NativeHeading &old = Headings[static_cast<size_t>(previous.lastDirection)];
		if (x * old.x + z * old.z >= std::cos(Pi / 8 + hysteresis))
			return old.direction;
	}
	double bestDot = -2;
	Direction best = Direction::NoDirection;
	for (const NativeHeading &heading : Headings) {
		const double dot = x * heading.x + z * heading.z;
		if (dot > bestDot) {
			bestDot = dot;
			best = heading.direction;
		}
	}
	return best;
}

} // namespace

TownFirstPersonInputResult StepTownFirstPersonInput(const TownFirstPersonInputState &state,
    const TownFirstPersonInputSnapshot &input, const TownFirstPersonInputPreferences &preferences)
{
	TownFirstPersonInputResult result;
	result.nextState = state;
	result.nextState.heldKeys &= TownFirstPersonMovementMask;
	result.nextState.blockedKeys &= TownFirstPersonMovementMask;
	result.nextState.lastDirectionKeys &= TownFirstPersonArrowMask;
	const uint8_t physical = input.physicalHeld & TownFirstPersonMovementMask;
	const bool validPose = std::isfinite(input.currentYaw) && std::isfinite(input.currentPitch)
	    && input.currentPitch >= -PitchLimit && input.currentPitch <= PitchLimit;
	const bool eligible = input.firstPersonActive && input.inputAllowed && validPose;
	using Phase = TownFirstPersonCapturePhase;
	if (!eligible || input.escapePressed) {
		const bool ownedCapture = state.phase != Phase::Released;
		if (ownedCapture || state.heldKeys != 0 || state.blockedKeys != 0 || IsDirection(state.lastDirection))
			ClearHeld(result);
		// A new mode entry may acquire automatically; focus/UI/Escape recovery
		// within an existing first-person session always needs an explicit click.
		result.nextState.resumeRequired = input.firstPersonActive;
		if (ownedCapture) {
			if (state.phase != Phase::Releasing) {
				result.nextState.phase = Phase::Releasing;
				result.releaseCapture = true;
			} else if (!input.relativeCaptured) {
				result.nextState.phase = Phase::Released;
			} else if (input.escapePressed) {
				// Explicit retries are allowed; failures never create a per-frame loop.
				result.releaseCapture = true;
			}
		}
		return result;
	}

	switch (state.phase) {
	case Phase::Released:
		if (state.resumeRequired && !input.resumeClick)
			return result;
		ClearHeld(result);
		result.nextState.phase = Phase::Enabling;
		result.nextState.resumeRequired = true;
		result.nextState.blockedKeys = physical;
		result.requestCapture = true;
		result.consumeResumeClick = input.resumeClick;
		result.consumeArrowKeys = true;
		return result;
	case Phase::Enabling:
		result.discardMouseDelta = true;
		result.consumeArrowKeys = true;
		result.consumeResumeClick = input.resumeClick;
		if (input.captureFailed) {
			ClearHeld(result);
			result.nextState.phase = input.relativeCaptured ? Phase::Releasing : Phase::Released;
			result.nextState.resumeRequired = true;
			result.releaseCapture = input.relativeCaptured;
			result.consumeArrowKeys = false;
			return result;
		}
		if (input.relativeCaptured) {
			ClearHeld(result);
			result.nextState.phase = Phase::Captured;
			result.nextState.blockedKeys = physical;
			result.active = true;
		}
		return result;
	case Phase::Releasing:
		result.discardMouseDelta = true;
		if (!input.relativeCaptured) {
			result.nextState.phase = Phase::Released;
		} else if (input.resumeClick) {
			result.releaseCapture = true;
			result.consumeResumeClick = true;
		}
		return result;
	case Phase::Captured:
		if (!input.relativeCaptured) {
			ClearHeld(result);
			result.nextState.phase = Phase::Released;
			result.nextState.resumeRequired = true;
			return result;
		}
		break;
	default:
		ClearHeld(result);
		result.nextState.phase = Phase::Released;
		result.nextState.resumeRequired = true;
		return result;
	}

	return StepTownCameraMovement(result.nextState, input, preferences);
}

TownFirstPersonInputResult StepTownCameraMovement(const TownFirstPersonInputState &state,
    const TownFirstPersonInputSnapshot &input, const TownFirstPersonInputPreferences &preferences)
{
	TownFirstPersonInputResult result;
	result.nextState = state;
	const uint8_t physical = input.physicalHeld & TownFirstPersonMovementMask;
	const uint8_t pressed = input.pressed & TownFirstPersonMovementMask;
	const uint8_t released = input.released & TownFirstPersonMovementMask;
	if (!std::isfinite(input.currentYaw) || !std::isfinite(input.currentPitch)
	    || input.currentPitch < -PitchLimit || input.currentPitch > PitchLimit) {
		ClearHeld(result);
		return result;
	}

	result.active = true;
	result.consumeArrowKeys = true;
	result.nextState.blockedKeys &= physical;
	result.nextState.blockedKeys &= static_cast<uint8_t>(~released);
	result.nextState.heldKeys &= physical;
	result.nextState.heldKeys &= static_cast<uint8_t>(~released);
	result.nextState.heldKeys = static_cast<uint8_t>(result.nextState.heldKeys
	    | (pressed & physical & static_cast<uint8_t>(~result.nextState.blockedKeys)));
	const double sensitivity = Bounded(preferences.radiansPerPixel, DefaultSensitivity, 0.001F, 0.02F);
	const double baseYaw = std::remainder(static_cast<double>(input.currentYaw), 2 * Pi);
	double yaw = baseYaw;
	if (std::isfinite(input.relativeMouseX)) {
		const double mouseYaw = std::remainder(static_cast<double>(input.relativeMouseX) * sensitivity, 2 * Pi);
		yaw = std::remainder(baseYaw + mouseYaw, 2 * Pi);
		result.yawDelta = static_cast<float>(std::remainder(yaw - baseYaw, 2 * Pi));
	}
	if (std::isfinite(input.relativeMouseY)) {
		const double mousePitch = static_cast<double>(input.relativeMouseY) * sensitivity * (preferences.invertY ? -1 : 1);
		const double pitch = std::clamp(input.currentPitch + mousePitch, -static_cast<double>(PitchLimit), static_cast<double>(PitchLimit));
		result.pitchDelta = static_cast<float>(pitch - input.currentPitch);
	}
	const uint8_t held = CanonicalMovementKeys(result.nextState.heldKeys);
	const int forward = ((held & TownFirstPersonArrowUp) != 0 ? 1 : 0)
	    - ((held & TownFirstPersonArrowDown) != 0 ? 1 : 0);
	const int right = ((held & TownFirstPersonArrowRight) != 0 ? 1 : 0)
	    - ((held & TownFirstPersonArrowLeft) != 0 ? 1 : 0);
	if (forward == 0 && right == 0) {
		result.stopOwnWalk = IsDirection(state.lastDirection);
		result.nextState.lastDirection = Direction::NoDirection;
		result.nextState.lastDirectionKeys = held;
		return result;
	}
	const double length = std::sqrt(static_cast<double>(forward * forward + right * right));
	const double x = (-std::cos(yaw) * forward + std::sin(yaw) * right) / length;
	const double z = (-std::sin(yaw) * forward - std::cos(yaw) * right) / length;
	const double hysteresis = Bounded(preferences.hysteresisRadians, DefaultHysteresis, 0, static_cast<float>(Pi / 16));
	result.direction = Quantize(x, z, state, held, hysteresis);
	result.nextState.lastDirection = result.direction;
	result.nextState.lastDirectionKeys = held;
	return result;
}

} // namespace devilution
