#pragma once

#include <cstdint>

#include "engine/direction.hpp"

namespace devilution {

constexpr uint8_t TownFirstPersonArrowUp = 1U << 0;
constexpr uint8_t TownFirstPersonArrowDown = 1U << 1;
constexpr uint8_t TownFirstPersonArrowLeft = 1U << 2;
constexpr uint8_t TownFirstPersonArrowRight = 1U << 3;
constexpr uint8_t TownFirstPersonArrowMask = 0x0FU;

enum class TownFirstPersonCapturePhase : uint8_t { Released, Enabling, Captured, Releasing };

/** Pure state, copied by Step; no SDL capture, player or simulation ownership.
 * lastDirection records intent. The runtime adapter separately owns actual
 * native walk commands and filters stopOwnWalk using that ownership. */
struct TownFirstPersonInputState {
	TownFirstPersonCapturePhase phase = TownFirstPersonCapturePhase::Released;
	uint8_t heldKeys = 0;
	uint8_t blockedKeys = 0;
	uint8_t lastDirectionKeys = 0;
	Direction lastDirection = Direction::NoDirection;
	bool resumeRequired = false;
};

struct TownFirstPersonInputSnapshot {
	bool firstPersonActive = false;
	/** Adapter gate: supported relative mode, focus, live town session/player,
	 * no text entry/menu/dialog/panel/drag that needs native pointer input. */
	bool inputAllowed = false;
	bool relativeCaptured = false; // Observed platform state, never requested state.
	bool captureFailed = false; // Failure acknowledgement for the pending change.
	bool escapePressed = false; // Nonrepeat pulse; native Escape stays available.
	bool resumeClick = false; // Fresh click inside eligible world, decided by adapter.
	uint8_t physicalHeld = 0;
	uint8_t pressed = 0; // Fresh NONREPEAT arrow downs since the preceding Step.
	uint8_t released = 0; // Arrow ups; deliver even when native UI consumes them.
	float relativeMouseX = 0; // Pixels since preceding Step; never multiply by dt.
	float relativeMouseY = 0;
	float currentYaw = 0.7853981634F; // Actual camera pose, from anchor toward eye.
	float currentPitch = 0; // Positive looks down, valid interval [-1.4,+1.4].
};

struct TownFirstPersonInputPreferences {
	float radiansPerPixel = 0.006F; // Same units/range as TownCameraPreferences.
	float hysteresisRadians = 0.05235987756F; // 3 degrees; zero disables the margin.
	bool invertY = false;
};

struct TownFirstPersonInputResult {
	TownFirstPersonInputState nextState;
	Direction direction = Direction::NoDirection;
	float yawDelta = 0;
	float pitchDelta = 0;
	bool requestCapture = false;
	bool releaseCapture = false;
	bool clearKeys = false; // Clear only FPP keys, never global/native key bindings.
	bool discardMouseDelta = false; // Adapter also flushes stale platform deltas.
	bool consumeResumeClick = false;
	bool stopOwnWalk = false; // One-shot loss of an FPP intent; adapter checks ownership.
	bool active = false; // Confirmed capture and eligible; includes acknowledgement step.
	bool consumeArrowKeys = false; // Eligible Enabling/Captured only, never modal text/UI.
};

/** Functional transition, no clock/heap/SDL/player/MPQ. Initial eligible mode
 * entry requests capture once. Lost focus/UI/Escape/capture failure requires a
 * fresh world resumeClick; returning focus or holding a key never reacquires.
 * Acquisition acknowledges separately, drops that sample's mouse/key presses,
 * and blocks all physically held arrows until observed release.
 * While Captured, arrows are latched only from nonrepeat presses. Movement uses
 * horizontal camera forward/right, normalizes combinations, and emits one native
 * 8-way intent with no speed/magnitude. Same-held-mask angular hysteresis only.
 * The adapter consumes direction at the native movement tick, never per event
 * or rendered frame, and must stop only a walk command it actually issued.
 * Snapshot/Result deltas can be applied through the existing camera rig.
 * No attack, interaction, target, path, collision or ControlMode is changed. */
TownFirstPersonInputResult StepTownFirstPersonInput(const TownFirstPersonInputState &state,
    const TownFirstPersonInputSnapshot &input, const TownFirstPersonInputPreferences &preferences);

} // namespace devilution
