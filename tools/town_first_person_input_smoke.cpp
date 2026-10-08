#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

#include "controls/town_first_person_input.hpp"

namespace {

using namespace devilution;
using Phase = TownFirstPersonCapturePhase;
constexpr float Pi = 3.14159265358979323846F;

size_t Checks = 0;
size_t Failures = 0;

void Check(bool passed, const char *label)
{
	++Checks;
	if (!passed) {
		++Failures;
		std::cerr << "FAIL first-person input: " << label << '\n';
	}
}

bool Near(float first, float second, float tolerance = 0.00001F)
{
	return std::isfinite(first) && std::isfinite(second) && std::abs(first - second) <= tolerance;
}

struct Probe {
	TownFirstPersonInputState state;
	TownFirstPersonInputSnapshot input;
	TownFirstPersonInputPreferences preferences;

	TownFirstPersonInputResult Step()
	{
		const TownFirstPersonInputResult result = StepTownFirstPersonInput(state, input, preferences);
		state = result.nextState;
		input.pressed = input.released = 0;
		input.relativeMouseX = input.relativeMouseY = 0;
		input.escapePressed = input.resumeClick = input.captureFailed = false;
		Check(std::isfinite(result.yawDelta) && std::isfinite(result.pitchDelta), "every output delta remains finite");
		Check(!(result.requestCapture && result.releaseCapture), "capture request and release are mutually exclusive");
		return result;
	}

	void Acquire(uint8_t held = 0)
	{
		input.firstPersonActive = input.inputAllowed = true;
		input.physicalHeld = held;
		const auto requested = Step();
		Check(requested.requestCapture && state.phase == Phase::Enabling && !requested.active, "eligible mode entry requests capture");
		input.relativeCaptured = true;
		const auto acknowledged = Step();
		Check(acknowledged.active && state.phase == Phase::Captured && acknowledged.clearKeys, "capture acknowledgement establishes ownership");
		Check(acknowledged.direction == Direction::NoDirection && acknowledged.discardMouseDelta, "acknowledgement sample has no movement or stale mouse");
	}

	TownFirstPersonInputResult Press(uint8_t keys)
	{
		input.physicalHeld = keys;
		input.pressed = keys;
		return Step();
	}
};

void CaptureLifecycle()
{
	Probe inactive;
	inactive.input.pressed = inactive.input.physicalHeld = TownFirstPersonArrowUp;
	inactive.input.relativeMouseX = 100;
	const auto untouched = inactive.Step();
	Check(!untouched.requestCapture && !untouched.releaseCapture && !untouched.consumeArrowKeys && !untouched.consumeResumeClick, "inactive module leaves native input available");
	Check(!untouched.active && untouched.direction == Direction::NoDirection && untouched.yawDelta == 0, "inactive module emits no camera or walk changes");

	Probe blockedEntry;
	blockedEntry.input.firstPersonActive = true;
	blockedEntry.Step();
	blockedEntry.input.inputAllowed = true;
	Check(!blockedEntry.Step().requestCapture, "closing a UI gate does not silently acquire");
	blockedEntry.input.resumeClick = true;
	const auto resumed = blockedEntry.Step();
	Check(resumed.requestCapture && resumed.consumeResumeClick, "eligible fresh world click acquires and is consumed");

	Probe barrier;
	barrier.input.firstPersonActive = barrier.input.inputAllowed = true;
	barrier.input.pressed = barrier.input.physicalHeld = TownFirstPersonArrowUp;
	barrier.input.relativeMouseX = 1000;
	const auto request = barrier.Step();
	Check(request.requestCapture && request.clearKeys && request.yawDelta == 0 && request.direction == Direction::NoDirection, "acquisition drops preexisting presses and mouse");
	barrier.input.relativeMouseY = 1000;
	const auto pending = barrier.Step();
	Check(!pending.active && !pending.requestCapture && pending.discardMouseDelta && pending.pitchDelta == 0, "capture waits for platform without request spam");
	barrier.input.relativeCaptured = true;
	barrier.input.pressed = TownFirstPersonArrowUp;
	barrier.input.relativeMouseX = 1000;
	const auto ack = barrier.Step();
	Check(ack.active && ack.direction == Direction::NoDirection && ack.yawDelta == 0 && barrier.state.blockedKeys == TownFirstPersonArrowUp, "physically held arrows remain barred at acknowledgement");
	for (size_t repeat = 0; repeat < 4; ++repeat)
		Check(barrier.Step().direction == Direction::NoDirection, "OS repeat with no fresh press cannot defeat the held-key barrier");
	barrier.input.pressed = TownFirstPersonArrowUp;
	Check(barrier.Step().direction == Direction::NoDirection, "even an acquisition-era held press stays blocked until release");
	barrier.input.released = TownFirstPersonArrowUp;
	barrier.input.physicalHeld = 0;
	barrier.Step();
	Check(barrier.state.blockedKeys == 0, "release removes held-arrow barrier");
	Check(barrier.Press(TownFirstPersonArrowUp).direction == Direction::North, "fresh press after release moves in the camera direction");
	barrier.input.released = TownFirstPersonArrowUp;
	barrier.input.physicalHeld = 0;
	Check(barrier.Step().stopOwnWalk, "releasing movement requests one owned-walk stop");
	Check(!barrier.Step().stopOwnWalk, "neutral snapshots do not repeat owned-walk stop");

	Probe failure;
	failure.input.firstPersonActive = failure.input.inputAllowed = true;
	failure.Step();
	failure.input.captureFailed = true;
	const auto failed = failure.Step();
	Check(failure.state.phase == Phase::Released && failure.state.resumeRequired && failed.clearKeys && !failed.active, "capture failure is acknowledged and disarmed");
	for (size_t frame = 0; frame < 10; ++frame)
		Check(!failure.Step().requestCapture, "failed acquisition never retries every frame");
	failure.input.resumeClick = true;
	Check(failure.Step().requestCapture, "fresh click permits explicit retry after failure");
	failure.input.captureFailed = failure.input.relativeCaptured = true;
	const auto partial = failure.Step();
	Check(partial.releaseCapture && failure.state.phase == Phase::Releasing && !partial.active, "failed acquisition that nevertheless captured requests cleanup");
	Check(!failure.Step().releaseCapture, "pending release has no automatic retry loop");
	failure.input.relativeCaptured = false;
	failure.Step();
	Check(failure.state.phase == Phase::Released, "effective release completes cleanup");

	Probe cancel;
	cancel.input.firstPersonActive = cancel.input.inputAllowed = true;
	cancel.Step();
	cancel.input.inputAllowed = false;
	cancel.input.relativeCaptured = true;
	const auto cancellation = cancel.Step();
	Check(cancellation.releaseCapture && !cancellation.active && cancel.state.phase == Phase::Releasing, "UI/focus gate cancels a late acquisition acknowledgement");
	Check(!cancel.Step().releaseCapture, "cancelled capture does not loop platform requests");
	cancel.input.relativeCaptured = false;
	cancel.Step();
	cancel.input.inputAllowed = true;
	Check(!cancel.Step().requestCapture, "reopening cancelled gate requires explicit resume");
}

void GateAndRelease()
{
	Probe probe;
	probe.Acquire();
	probe.Press(TownFirstPersonArrowUp);
	probe.input.inputAllowed = false;
	probe.input.relativeMouseX = 150;
	const auto lostGate = probe.Step();
	Check(lostGate.releaseCapture && lostGate.clearKeys && lostGate.stopOwnWalk && !lostGate.active, "focus/modal/death/level gate releases input and stops owned intent");
	Check(!lostGate.consumeArrowKeys && lostGate.direction == Direction::NoDirection && lostGate.yawDelta == 0, "modal gate returns arrows to native UI and ignores look");
	probe.input.relativeCaptured = false;
	probe.Step();
	probe.input.inputAllowed = true;
	Check(!probe.Step().requestCapture, "focus recovery with a held arrow does not reacquire");
	probe.input.resumeClick = true;
	Check(probe.Step().consumeResumeClick, "resume click is consumed rather than attacking");
	probe.input.relativeCaptured = true;
	probe.Step();
	Check(probe.Step().direction == Direction::NoDirection, "arrow held through modal and resume cannot restart movement");
	probe.input.physicalHeld = 0;
	probe.input.released = TownFirstPersonArrowUp;
	probe.Step();
	probe.Press(TownFirstPersonArrowUp);
	probe.input.escapePressed = true;
	const auto escaped = probe.Step();
	Check(escaped.releaseCapture && escaped.stopOwnWalk && escaped.clearKeys, "Escape releases and clears first-person ownership");
	probe.input.relativeCaptured = false;
	probe.Step();
	Check(!probe.Step().requestCapture, "Escape stays released until explicit resume");
	probe.input.firstPersonActive = false;
	probe.Step();
	probe.input.firstPersonActive = true;
	Check(probe.Step().requestCapture, "a new eligible mode entry may auto-acquire again");

	Probe lost;
	lost.Acquire();
	lost.Press(TownFirstPersonArrowUp);
	lost.input.relativeCaptured = false;
	const auto loss = lost.Step();
	Check(loss.stopOwnWalk && loss.clearKeys && lost.state.phase == Phase::Released && lost.state.resumeRequired, "unexpected platform capture loss clears input");
	Check(!lost.Step().requestCapture, "unexpected capture loss does not silently recapture");

	Probe modeExit;
	modeExit.Acquire();
	modeExit.Press(TownFirstPersonArrowLeft);
	modeExit.input.firstPersonActive = false;
	const auto exit = modeExit.Step();
	Check(exit.releaseCapture && exit.stopOwnWalk && !exit.active && !exit.consumeArrowKeys, "Home/F4/mode/session exit relinquishes first-person controls");
}

void DirectionsAndHysteresis()
{
	const std::array<float, 8> yaw { 0, Pi / 4, Pi / 2, 3 * Pi / 4, Pi, -3 * Pi / 4, -Pi / 2, -Pi / 4 };
	const std::array<Direction, 8> forward { Direction::NorthWest, Direction::North, Direction::NorthEast, Direction::East,
		Direction::SouthEast, Direction::South, Direction::SouthWest, Direction::West };
	for (size_t index = 0; index < yaw.size(); ++index) {
		Probe probe;
		probe.Acquire();
		probe.input.currentYaw = yaw[index];
		Check(probe.Press(TownFirstPersonArrowUp).direction == forward[index], "camera forward quantizes to the expected native octant");
	}
	const std::array<Direction, 16> combinations {
		Direction::NoDirection, Direction::North, Direction::South, Direction::NoDirection,
		Direction::West, Direction::NorthWest, Direction::SouthWest, Direction::West,
		Direction::East, Direction::NorthEast, Direction::SouthEast, Direction::East,
		Direction::NoDirection, Direction::North, Direction::South, Direction::NoDirection,
	};
	for (uint8_t mask = 0; mask < combinations.size(); ++mask) {
		Probe probe;
		probe.Acquire();
		const auto result = probe.Press(mask);
		Check(result.direction == combinations[mask], "all arrow combinations normalize/cancel to exactly one native intent");
		Check(!result.stopOwnWalk, "new intent or neutral does not invent a previous walk to stop");
	}
	Probe absent;
	absent.Acquire();
	absent.input.pressed = TownFirstPersonArrowUp;
	Check(absent.Step().direction == Direction::NoDirection, "queued press without physical down cannot stick");
	Check(absent.Press(0xF0).direction == Direction::NoDirection && absent.state.heldKeys == TownFirstPersonWasdMask, "all WASD directions cancel while retaining physical aliases");

	Probe hysteresis;
	hysteresis.Acquire();
	hysteresis.input.currentYaw = 20 * Pi / 180;
	Check(hysteresis.Press(TownFirstPersonArrowUp).direction == Direction::NorthWest, "initial angular sector");
	hysteresis.input.currentYaw = 23 * Pi / 180;
	Check(hysteresis.Step().direction == Direction::NorthWest, "three-degree hysteresis prevents boundary chatter");
	hysteresis.input.currentYaw = 25 * Pi / 180;
	Check(hysteresis.Step().direction == Direction::NorthWest, "direction persists inside extended sector");
	hysteresis.input.currentYaw = 26 * Pi / 180;
	Check(hysteresis.Step().direction == Direction::North, "crossing hysteresis threshold changes direction");
	hysteresis.input.currentYaw = 23 * Pi / 180;
	Check(hysteresis.Step().direction == Direction::North, "reverse boundary chatter stays stable");
	Check(hysteresis.Press(TownFirstPersonArrowUp | TownFirstPersonArrowRight).direction == Direction::NorthEast, "changing held mask bypasses old directional hysteresis immediately");
	Probe noHysteresis;
	noHysteresis.Acquire();
	noHysteresis.preferences.hysteresisRadians = 0;
	noHysteresis.input.currentYaw = 20 * Pi / 180;
	noHysteresis.Press(TownFirstPersonArrowUp);
	noHysteresis.input.currentYaw = 23 * Pi / 180;
	Check(noHysteresis.Step().direction == Direction::North, "zero hysteresis uses the nearest sector without extra margin");
}

void LookAndInvalidValues()
{
	Probe look;
	look.Acquire();
	look.input.currentYaw = 0;
	look.input.relativeMouseX = 10;
	look.input.relativeMouseY = 10;
	const auto delta = look.Step();
	Check(Near(delta.yawDelta, 0.06F) && Near(delta.pitchDelta, 0.06F), "mouse look uses radians per pixel with right/down positive");
	look.preferences.radiansPerPixel = 0.003F;
	look.input.relativeMouseX = 10;
	Check(Near(look.Step().yawDelta, 0.03F), "sensitivity scales pixel displacement directly");
	look.preferences.invertY = true;
	look.input.relativeMouseY = 10;
	Check(Near(look.Step().pitchDelta, -0.03F), "invert Y reverses only pitch");
	look.preferences = TownFirstPersonInputPreferences {};
	look.input.currentPitch = 1.3F;
	look.input.relativeMouseY = 100;
	Check(Near(look.Step().pitchDelta, 1.4F - 1.3F), "pitch clamps to the camera rig upper limit");
	look.input.currentPitch = -1.3F;
	look.input.relativeMouseY = -100;
	Check(Near(look.Step().pitchDelta, -1.4F + 1.3F), "pitch clamps to the camera rig lower limit");
	for (const float pitch : { -1.4F, 1.4F }) {
		Probe steep;
		steep.Acquire();
		steep.input.currentPitch = pitch;
		Check(steep.Press(TownFirstPersonArrowUp).direction == Direction::North, "ground movement is independent of steep pitch");
	}
	Probe sameSample;
	sameSample.Acquire();
	sameSample.input.currentYaw = 0;
	sameSample.input.relativeMouseX = (Pi / 4) / sameSample.preferences.radiansPerPixel;
	const auto turnedMove = sameSample.Press(TownFirstPersonArrowUp);
	Check(Near(turnedMove.yawDelta, Pi / 4) && turnedMove.direction == Direction::North, "movement follows the look direction updated in the same sample");

	for (const float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		Probe invalidMouse;
		invalidMouse.Acquire();
		invalidMouse.input.relativeMouseX = bad;
		invalidMouse.input.relativeMouseY = 10;
		const auto mouse = invalidMouse.Step();
		Check(mouse.yawDelta == 0 && Near(mouse.pitchDelta, 0.06F), "nonfinite mouse axis is ignored independently");
		invalidMouse.input.relativeMouseX = 10;
		invalidMouse.input.relativeMouseY = bad;
		const auto badVertical = invalidMouse.Step();
		Check(Near(badVertical.yawDelta, 0.06F) && badVertical.pitchDelta == 0, "nonfinite vertical mouse leaves horizontal look available");
		invalidMouse.preferences.radiansPerPixel = bad;
		invalidMouse.input.relativeMouseX = 10;
		Check(Near(invalidMouse.Step().yawDelta, 0.06F), "invalid sensitivity uses the documented default");
		Probe invalidPose;
		invalidPose.Acquire();
		invalidPose.Press(TownFirstPersonArrowUp);
		invalidPose.input.currentYaw = bad;
		const auto pose = invalidPose.Step();
		Check(pose.releaseCapture && pose.stopOwnWalk && pose.clearKeys && !pose.active && pose.direction == Direction::NoDirection, "nonfinite yaw cannot emit movement and safely relinquishes capture");
		Probe invalidPitch;
		invalidPitch.Acquire();
		invalidPitch.input.currentPitch = bad;
		Check(invalidPitch.Step().releaseCapture, "nonfinite pitch relinquishes capture");
	}
	Probe huge;
	huge.Acquire();
	huge.input.currentYaw = std::numeric_limits<float>::max();
	huge.input.relativeMouseX = huge.input.relativeMouseY = std::numeric_limits<float>::max();
	const auto enormous = huge.Press(TownFirstPersonArrowUp);
	Check(enormous.active && std::isfinite(enormous.yawDelta) && std::abs(enormous.yawDelta) <= Pi && Near(enormous.pitchDelta, 1.4F), "huge finite values wrap/clamp without overflow");
	Check(static_cast<unsigned>(enormous.direction) < 8, "huge finite yaw still yields a bounded native direction");
	Probe invalidRange;
	invalidRange.Acquire();
	invalidRange.input.currentPitch = 2;
	Check(invalidRange.Step().releaseCapture, "pose outside the rig pitch range is rejected");
}

void FunctionalContract()
{
	TownFirstPersonInputState state;
	state.phase = Phase::Captured;
	TownFirstPersonInputSnapshot input;
	input.firstPersonActive = input.inputAllowed = input.relativeCaptured = true;
	input.physicalHeld = input.pressed = TownFirstPersonArrowRight;
	input.relativeMouseX = 10;
	const TownFirstPersonInputPreferences preferences;
	const auto first = StepTownFirstPersonInput(state, input, preferences);
	const auto second = StepTownFirstPersonInput(state, input, preferences);
	Check(first.direction == second.direction && first.yawDelta == second.yawDelta && first.pitchDelta == second.pitchDelta
	        && first.nextState.phase == second.nextState.phase && first.nextState.heldKeys == second.nextState.heldKeys
	        && first.requestCapture == second.requestCapture && first.stopOwnWalk == second.stopOwnWalk,
	    "identical snapshots produce identical decisions without external state");
	Check(state.heldKeys == 0 && state.lastDirection == Direction::NoDirection
	        && input.pressed == TownFirstPersonArrowRight && input.relativeMouseX == 10,
	    "functional transition does not mutate the caller snapshot or previous state");
}

// Independent physical aliases, canonical directions and capture barriers.
void WasdAliases()
{
	struct AliasPair {
		uint8_t arrow;
		uint8_t key;
		Direction expected;
	};
	const std::array<AliasPair, 4> pairs { {
		{ TownFirstPersonArrowUp, TownFirstPersonKeyW, Direction::North },
		{ TownFirstPersonArrowLeft, TownFirstPersonKeyA, Direction::West },
		{ TownFirstPersonArrowDown, TownFirstPersonKeyS, Direction::South },
		{ TownFirstPersonArrowRight, TownFirstPersonKeyD, Direction::East },
	} };
	for (const AliasPair pair : pairs) {
		for (const bool releaseArrowFirst : { false, true }) {
			Probe independent;
			independent.Acquire();
			const uint8_t both = static_cast<uint8_t>(pair.arrow | pair.key);
			const auto together = independent.Press(both);
			Check(together.direction == pair.expected && independent.state.heldKeys == both
			        && independent.state.lastDirectionKeys == pair.arrow,
			    "a simultaneous arrow and WASD alias produce one canonical intent while retaining both physical keys");
			const uint8_t released = releaseArrowFirst ? pair.arrow : pair.key;
			const uint8_t remaining = releaseArrowFirst ? pair.key : pair.arrow;
			independent.input.released = released;
			independent.input.physicalHeld = remaining;
			const auto partial = independent.Step();
			Check(partial.direction == pair.expected && !partial.stopOwnWalk
			        && independent.state.heldKeys == remaining && independent.state.lastDirectionKeys == pair.arrow,
			    "releasing either alias independently preserves the other without an owned-walk stop");
			independent.input.released = remaining;
			independent.input.physicalHeld = 0;
			const auto finalRelease = independent.Step();
			Check(finalRelease.direction == Direction::NoDirection && finalRelease.stopOwnWalk
			        && independent.state.heldKeys == 0, "only releasing the last allowed alias requests a stop");
			Check(!independent.Step().stopOwnWalk, "the final alias release emits a stop pulse only once");
		}
	}

	// High-nibble ordering is W,A,S,D, deliberately different from Up,Down,Left,Right.
	const std::array<Direction, 16> wasdCombinations {
		Direction::NoDirection, Direction::North, Direction::West, Direction::NorthWest,
		Direction::South, Direction::NoDirection, Direction::SouthWest, Direction::West,
		Direction::East, Direction::NorthEast, Direction::NoDirection, Direction::North,
		Direction::SouthEast, Direction::East, Direction::South, Direction::NoDirection,
	};
	for (size_t combination = 0; combination < wasdCombinations.size(); ++combination) {
		Probe mapped;
		mapped.Acquire();
		const uint8_t raw = static_cast<uint8_t>(combination << 4);
		const auto result = mapped.Press(raw);
		Check(result.direction == wasdCombinations[combination] && mapped.state.heldKeys == raw
		        && !result.stopOwnWalk && (mapped.state.lastDirectionKeys & 0xF0U) == 0,
		    "all WASD combinations map, normalize and cancel without confusing A and S or losing raw keys");
	}
	const std::array<float, 8> yaw { 0, Pi / 4, Pi / 2, 3 * Pi / 4, Pi, -3 * Pi / 4, -Pi / 2, -Pi / 4 };
	const std::array<Direction, 8> forward { Direction::NorthWest, Direction::North, Direction::NorthEast, Direction::East,
		Direction::SouthEast, Direction::South, Direction::SouthWest, Direction::West };
	for (size_t index = 0; index < yaw.size(); ++index) {
		Probe rotated;
		rotated.Acquire();
		rotated.input.currentYaw = yaw[index];
		Check(rotated.Press(TownFirstPersonKeyW).direction == forward[index],
		    "W follows the same eight native camera-relative directions as Up");
	}
	Probe duplicates;
	duplicates.Acquire();
	Check(duplicates.Press(TownFirstPersonKeyW | TownFirstPersonArrowUp | TownFirstPersonArrowRight).direction == Direction::NorthEast,
	    "two forward aliases and one strafe alias retain the normalized diagonal instead of weighting forward twice");
	duplicates.input.physicalHeld = TownFirstPersonKeyW | TownFirstPersonArrowUp | TownFirstPersonKeyS;
	duplicates.input.pressed = TownFirstPersonKeyS;
	duplicates.input.released = TownFirstPersonArrowRight;
	const auto opposite = duplicates.Step();
	Check(opposite.direction == Direction::NoDirection && opposite.stopOwnWalk
	        && duplicates.state.heldKeys == (TownFirstPersonKeyW | TownFirstPersonArrowUp | TownFirstPersonKeyS),
	    "W plus Up cancels S once rather than outvoting one backward alias");
	Check(!duplicates.Step().stopOwnWalk, "a held opposing combination does not repeat the stop pulse");
	duplicates.input.physicalHeld = TownFirstPersonKeyW | TownFirstPersonArrowUp;
	duplicates.input.released = TownFirstPersonKeyS;
	Check(duplicates.Step().direction == Direction::North,
	    "releasing the opposing key restores the still-held allowed forward aliases");

	for (const uint8_t oldKey : { TownFirstPersonArrowUp, TownFirstPersonKeyW }) {
		const uint8_t freshKey = oldKey == TownFirstPersonArrowUp ? TownFirstPersonKeyW : TownFirstPersonArrowUp;
		Probe blocked;
		blocked.Acquire(oldKey);
		Check(blocked.state.blockedKeys == oldKey && blocked.state.heldKeys == 0,
		    "capture blocks each old physical alias independently");
		blocked.input.physicalHeld = static_cast<uint8_t>(oldKey | freshKey);
		blocked.input.pressed = freshKey;
		const auto fresh = blocked.Step();
		Check(fresh.direction == Direction::North && blocked.state.blockedKeys == oldKey
		        && blocked.state.heldKeys == freshKey && blocked.state.lastDirectionKeys == TownFirstPersonArrowUp,
		    "a fresh alias moves even while its old equivalent remains physically held and blocked");
		Probe releaseFreshFirst = blocked;
		releaseFreshFirst.input.physicalHeld = oldKey;
		releaseFreshFirst.input.released = freshKey;
		const auto freshReleased = releaseFreshFirst.Step();
		Check(freshReleased.direction == Direction::NoDirection && freshReleased.stopOwnWalk
		        && releaseFreshFirst.state.blockedKeys == oldKey && releaseFreshFirst.state.heldKeys == 0,
		    "releasing the only allowed alias stops even if the blocked equivalent stays physically held");
		releaseFreshFirst.input.pressed = oldKey;
		Check(releaseFreshFirst.Step().direction == Direction::NoDirection,
		    "a blocked old alias cannot use repeat or another alias release to restart movement");
		releaseFreshFirst.input.physicalHeld = 0;
		releaseFreshFirst.input.released = oldKey;
		releaseFreshFirst.Step();
		Check(releaseFreshFirst.Press(oldKey).direction == Direction::North,
		    "the old alias becomes usable only after its own release and fresh press");
		blocked.input.physicalHeld = freshKey;
		blocked.input.released = oldKey;
		const auto oldReleased = blocked.Step();
		Check(oldReleased.direction == Direction::North && !oldReleased.stopOwnWalk
		        && blocked.state.blockedKeys == 0 && blocked.state.heldKeys == freshKey,
		    "releasing a blocked alias does not cancel its allowed equivalent");
	}
	Probe allBlocked;
	allBlocked.Acquire(TownFirstPersonMovementMask);
	Check(allBlocked.state.blockedKeys == TownFirstPersonMovementMask,
	    "capture acknowledgement blocks all eight held physical keys");
	allBlocked.input.physicalHeld = static_cast<uint8_t>(TownFirstPersonMovementMask & ~TownFirstPersonKeyW);
	allBlocked.input.released = TownFirstPersonKeyW;
	allBlocked.Step();
	allBlocked.input.physicalHeld = TownFirstPersonMovementMask;
	allBlocked.input.pressed = TownFirstPersonKeyW;
	Check(allBlocked.Step().direction == Direction::North && allBlocked.state.heldKeys == TownFirstPersonKeyW
	        && allBlocked.state.blockedKeys == static_cast<uint8_t>(TownFirstPersonMovementMask & ~TownFirstPersonKeyW),
	    "releasing and repressing W unlocks only W while seven other old aliases stay blocked");

	Probe hysteresis;
	hysteresis.Acquire();
	hysteresis.input.currentYaw = 20 * Pi / 180;
	Check(hysteresis.Press(TownFirstPersonArrowUp).direction == Direction::NorthWest, "alias hysteresis starts in the expected sector");
	hysteresis.input.currentYaw = 23 * Pi / 180;
	hysteresis.input.physicalHeld = TownFirstPersonArrowUp | TownFirstPersonKeyW;
	hysteresis.input.pressed = TownFirstPersonKeyW;
	Check(hysteresis.Step().direction == Direction::NorthWest && hysteresis.state.lastDirectionKeys == TownFirstPersonArrowUp,
	    "adding a redundant alias preserves the canonical held mask and angular hysteresis");
	hysteresis.input.physicalHeld = TownFirstPersonKeyW;
	hysteresis.input.released = TownFirstPersonArrowUp;
	Check(hysteresis.Step().direction == Direction::NorthWest && hysteresis.state.heldKeys == TownFirstPersonKeyW,
	    "releasing the first alias while keeping the second preserves the same hysteresis margin");
	hysteresis.input.currentYaw = 26 * Pi / 180;
	Check(hysteresis.Step().direction == Direction::North, "the retained alias crosses the normal hysteresis threshold");
	hysteresis.input.physicalHeld = TownFirstPersonKeyW | TownFirstPersonKeyD;
	hysteresis.input.pressed = TownFirstPersonKeyD;
	Check(hysteresis.Step().direction == Direction::NorthEast,
	    "adding a genuinely different canonical direction bypasses the previous angular margin");
	Probe atomicAlias;
	atomicAlias.Acquire();
	atomicAlias.input.currentYaw = 20 * Pi / 180;
	atomicAlias.Press(TownFirstPersonArrowUp);
	atomicAlias.input.currentYaw = 23 * Pi / 180;
	atomicAlias.input.physicalHeld = TownFirstPersonKeyW;
	atomicAlias.input.released = TownFirstPersonArrowUp;
	atomicAlias.input.pressed = TownFirstPersonKeyW;
	Check(atomicAlias.Step().direction == Direction::NorthWest && atomicAlias.state.heldKeys == TownFirstPersonKeyW,
	    "a same-snapshot alias replacement retains canonical hysteresis without inventing a neutral frame");

	Probe gated;
	gated.Acquire();
	gated.Press(TownFirstPersonKeyW | TownFirstPersonKeyD);
	gated.input.inputAllowed = false;
	gated.input.relativeMouseX = 100;
	const auto suspended = gated.Step();
	Check(suspended.releaseCapture && suspended.clearKeys && suspended.stopOwnWalk && !suspended.active
	        && !suspended.consumeArrowKeys && suspended.yawDelta == 0 && gated.state.heldKeys == 0
	        && gated.state.blockedKeys == 0 && gated.state.lastDirectionKeys == 0,
	    "the UI/focus gate releases capture and clears every WASD alias and canonical intent");
	gated.input.relativeCaptured = false;
	gated.Step();
	gated.input.inputAllowed = true;
	gated.input.pressed = TownFirstPersonKeyW | TownFirstPersonKeyD;
	Check(!gated.Step().requestCapture, "returning focus or fresh movement keys cannot replace an explicit resume click");
	gated.input.resumeClick = true;
	const auto resume = gated.Step();
	Check(resume.requestCapture && resume.consumeResumeClick
	        && gated.state.blockedKeys == (TownFirstPersonKeyW | TownFirstPersonKeyD),
	    "resume consumes the click and independently blocks WASD keys still held through the gate");
	gated.input.relativeCaptured = true;
	gated.input.pressed = TownFirstPersonKeyW | TownFirstPersonKeyD;
	gated.input.relativeMouseX = 100;
	const auto acknowledgement = gated.Step();
	Check(acknowledgement.active && acknowledgement.direction == Direction::NoDirection && acknowledgement.yawDelta == 0
	        && gated.state.heldKeys == 0, "capture acknowledgement discards stale WASD presses and mouse delta");
	gated.input.physicalHeld = TownFirstPersonKeyW | TownFirstPersonKeyD | TownFirstPersonArrowUp;
	gated.input.pressed = TownFirstPersonArrowUp;
	Check(gated.Step().direction == Direction::North && gated.state.heldKeys == TownFirstPersonArrowUp,
	    "a fresh Up works after resume even while old W and D remain blocked");
	gated.input.physicalHeld = TownFirstPersonKeyW | TownFirstPersonKeyD;
	gated.input.released = TownFirstPersonArrowUp;
	Check(gated.Step().stopOwnWalk, "releasing that fresh alias stops without reviving the blocked resume-era keys");

	Probe filtered;
	filtered.Acquire();
	filtered.input.physicalHeld = 0;
	filtered.input.pressed = TownFirstPersonKeyW;
	Check(filtered.Step().direction == Direction::NoDirection && filtered.state.heldKeys == 0,
	    "an adapter-filtered modifier press cannot latch a key absent from its eligible physical snapshot");
	filtered.input.physicalHeld = TownFirstPersonKeyW;
	Check(filtered.Step().direction == Direction::NoDirection,
	    "removing a modifier while W stays physically held does not synthesize a fresh movement press");
	filtered.input.physicalHeld = 0;
	filtered.input.released = TownFirstPersonKeyW;
	filtered.Step();
	Check(filtered.Press(TownFirstPersonKeyW).direction == Direction::North,
	    "a release and new eligible W press moves after modifier filtering");
	filtered.input.physicalHeld = 0;
	Check(filtered.Step().stopOwnWalk && filtered.state.heldKeys == 0,
	    "filtering an already moving key out of the physical snapshot stops its owned intent");
	filtered.input.physicalHeld = TownFirstPersonKeyW;
	Check(filtered.Step().direction == Direction::NoDirection,
	    "a key filtered during motion still requires a fresh press when it becomes eligible again");
	Probe inactive;
	inactive.input.physicalHeld = inactive.input.pressed = TownFirstPersonWasdMask;
	const auto native = inactive.Step();
	Check(!native.consumeArrowKeys && !native.active && native.direction == Direction::NoDirection
	        && !native.requestCapture, "WASD remains available to native input outside eligible FPP");

	TownFirstPersonInputState rawState;
	rawState.phase = Phase::Captured;
	rawState.heldKeys = TownFirstPersonArrowUp | TownFirstPersonKeyW;
	rawState.blockedKeys = TownFirstPersonKeyD;
	rawState.lastDirectionKeys = TownFirstPersonArrowUp;
	rawState.lastDirection = Direction::North;
	TownFirstPersonInputSnapshot rawInput;
	rawInput.firstPersonActive = rawInput.inputAllowed = rawInput.relativeCaptured = true;
	rawInput.physicalHeld = TownFirstPersonKeyW | TownFirstPersonKeyD | TownFirstPersonArrowRight;
	rawInput.pressed = TownFirstPersonArrowRight;
	rawInput.released = TownFirstPersonArrowUp;
	const TownFirstPersonInputPreferences rawPreferences;
	const auto first = StepTownFirstPersonInput(rawState, rawInput, rawPreferences);
	const auto second = StepTownFirstPersonInput(rawState, rawInput, rawPreferences);
	Check(first.direction == Direction::NorthEast && first.direction == second.direction
	        && first.yawDelta == second.yawDelta && first.pitchDelta == second.pitchDelta
	        && first.nextState.heldKeys == (TownFirstPersonKeyW | TownFirstPersonArrowRight)
	        && first.nextState.heldKeys == second.nextState.heldKeys
	        && first.nextState.blockedKeys == TownFirstPersonKeyD
	        && first.nextState.blockedKeys == second.nextState.blockedKeys
	        && first.nextState.lastDirectionKeys == (TownFirstPersonArrowUp | TownFirstPersonArrowRight)
	        && first.nextState.lastDirectionKeys == second.nextState.lastDirectionKeys,
	    "mixed raw aliases produce deterministic independent latches and one canonical diagonal");
	Check(rawState.heldKeys == (TownFirstPersonArrowUp | TownFirstPersonKeyW)
	        && rawState.blockedKeys == TownFirstPersonKeyD && rawState.lastDirectionKeys == TownFirstPersonArrowUp
	        && rawInput.released == TownFirstPersonArrowUp && rawInput.pressed == TownFirstPersonArrowRight,
	    "alias transitions do not mutate the caller's raw state or snapshot");
}

} // namespace

int main()
{
	CaptureLifecycle();
	GateAndRelease();
	DirectionsAndHysteresis();
	LookAndInvalidValues();
	FunctionalContract();
	WasdAliases();
	std::cout << "First-person input checks: " << Checks << ", failures: " << Failures << '\n';
	return Failures == 0 ? 0 : 1;
}
