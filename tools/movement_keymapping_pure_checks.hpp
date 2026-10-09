// Included inside the existing pure runner's anonymous namespace.
// Prepared only; root compiles/runs. No SDL, live profile or new test framework.
void MovementKernelChecks()
{
	for (unsigned yaw = 0; yaw < 8; ++yaw) {
		for (unsigned mask = 0; mask <= 255; ++mask) {
			TownFirstPersonInputState captured;
			captured.phase = Phase::Captured;
			TownFirstPersonInputSnapshot input;
			input.firstPersonActive = input.inputAllowed = input.relativeCaptured = true;
			input.physicalHeld = input.pressed = static_cast<uint8_t>(mask);
			input.currentYaw = static_cast<float>(yaw) * Pi / 4;
			const auto first = StepTownFirstPersonInput(captured, input, {});
			TownFirstPersonInputState third;
			const auto follow = StepTownCameraMovement(third, input, {});
			Check(first.direction == follow.direction && first.nextState.heldKeys == follow.nextState.heldKeys,
			    "shared movement kernel preserves all aliases and native directions in both cameras");
			Check(!follow.requestCapture && !follow.releaseCapture && follow.nextState.phase == Phase::Released,
			    "third-person movement kernel never acquires or releases relative mouse");
		}
	}
	TownFirstPersonInputState state;
	TownFirstPersonInputSnapshot input;
	input.currentYaw = 0;
	input.physicalHeld = input.pressed = TownFirstPersonArrowUp | TownFirstPersonKeyW;
	auto result = StepTownCameraMovement(state, input, {});
	const auto direction = result.direction;
	input.physicalHeld = TownFirstPersonKeyW;
	input.pressed = 0;
	input.released = TownFirstPersonArrowUp;
	result = StepTownCameraMovement(result.nextState, input, {});
	Check(result.direction == direction && !result.stopOwnWalk,
	    "main and alternate slots release independently without capture state");
	input.physicalHeld = 0;
	input.released = TownFirstPersonKeyW;
	result = StepTownCameraMovement(result.nextState, input, {});
	Check(result.direction == Direction::NoDirection && result.stopOwnWalk,
	    "releasing the final third-person slot emits one owned-walk stop");
	input.released = 0;
	result = StepTownCameraMovement(result.nextState, input, {});
	Check(!result.stopOwnWalk, "third-person idle does not repeat native stop requests");
	input.physicalHeld = TownFirstPersonKeyW;
	result = StepTownCameraMovement(result.nextState, input, {});
	Check(result.direction == Direction::NoDirection,
	    "a held or repeated key cannot start movement without a fresh native action press");
	input.pressed = TownFirstPersonKeyW;
	result = StepTownCameraMovement(result.nextState, input, {});
	Check(result.direction == direction, "a fresh bound action starts third-person movement");
	input.currentYaw = std::numeric_limits<float>::quiet_NaN();
	result = StepTownCameraMovement(result.nextState, input, {});
	Check(result.direction == Direction::NoDirection && result.stopOwnWalk && result.nextState.heldKeys == 0,
	    "invalid camera pose clears third-person movement without capture operations");
}
