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
	Check(absent.Press(0xF0).direction == Direction::NoDirection, "non-arrow bits are ignored");

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

} // namespace

int main()
{
	CaptureLifecycle();
	GateAndRelease();
	DirectionsAndHysteresis();
	LookAndInvalidValues();
	FunctionalContract();
	std::cout << "First-person input checks: " << Checks << ", failures: " << Failures << '\n';
	return Failures == 0 ? 0 : 1;
}
