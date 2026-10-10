// Prepared only. Root builds/runs. The sensor is synthetic; ownership/polling is production.
namespace {
struct MovementR2Sensor {
	static inline std::array<bool, 1024> Held {};
	static inline std::array<unsigned, 1024> Queries {};
	static inline bool RejectAcquire = false;
	static inline unsigned AcquireCalls = 0;
	static bool SetRelative(bool enabled)
	{
		if (enabled) {
			++AcquireCalls;
			if (RejectAcquire) return false;
		}
		return PhysicalServices::SetRelative(enabled);
	}
	static bool ScancodeHeld(int scan)
	{
		if (scan <= 0 || static_cast<size_t>(scan) >= Held.size()) return false;
		++Queries[scan];
		return Held[scan];
	}
};
unsigned MovementR2NativeReleaseCalls = 0;

void MovementKeymappingR2RuntimeChecks(const ProductionAccess &api)
{
	using namespace first_person_root_supplement;
	const TownFirstPersonInputServicesForDiagnostics scanServices {
		MovementR2Sensor::SetRelative, PhysicalServices::GetRelative, PhysicalServices::HeldArrows,
		PhysicalServices::HasFocus, PhysicalServices::FlushRelative, MovementR2Sensor::ScancodeHeld
	};
	const TownFirstPersonInputServicesForDiagnostics legacyServices {
		PhysicalServices::SetRelative, PhysicalServices::GetRelative, PhysicalServices::HeldArrows,
		PhysicalServices::HasFocus, PhysicalServices::FlushRelative
	};
	auto &mapper = GetOptions().Keymapper;
	auto &forward = RuntimeMovementSetting("TownMoveForward");
	mapper.AddAction("FixtureR2NativeRelease", "Fixture R2 release", "Private fixture only", SDLK_F22,
		[] {}, [] { ++MovementR2NativeReleaseCalls; }).SetValue(SDLK_F22);
	const Point origin = FindClearPatch(Check);
	OwnedSurface frame(640, 480);
	const auto dispatch = [&](SDL_Keycode key, int scan, bool down, bool repeat = false, uint16_t mods = 0) {
		auto event = FollowKey(key, down, repeat);
#ifdef USE_SDL3
		event.key.scancode = static_cast<SDL_Scancode>(scan);
#else
		event.key.keysym.scancode = static_cast<SDL_Scancode>(scan);
#endif
		if (scan > 0 && static_cast<size_t>(scan) < MovementR2Sensor::Held.size())
			MovementR2Sensor::Held[scan] = down;
		api.dispatch(event, mods);
	};
	const auto release = [&] {
		for (size_t scan = 1; scan < MovementR2Sensor::Held.size(); ++scan)
			if (MovementR2Sensor::Held[scan]) dispatch(SDLK_UNKNOWN, static_cast<int>(scan), false);
		api.release();
		DrainEmittedWalks(Check);
		PhysicalServices::HeldPhysicalKeys.clear();
		ClosePanels();
		SpellSelectFlag = AutomapActive = ChatFlag = false;
		PauseMode = 0;
	};
	const auto enter = [&](TownCameraMode mode) {
		release();
		MovementR2Sensor::RejectAcquire = false;
		MovementR2Sensor::Held.fill(false);
		MovementR2Sensor::Queries.fill(0);
		PhysicalServices::Focus = true;
		ControlMode = ControlDevice = ControlTypes::KeyboardAndMouse; // Fixture setup, before tested dispatch.
		if (!IsTownViewActive()) ToggleTownView();
		SetTownFirstPersonInputServicesForDiagnostics(&scanServices);
		if (mode == TownCameraMode::FirstPerson) ResumeAt(api, Check, origin, 0);
		else {
			FixturePlacePlayer(origin);
			SetTownViewCameraMode(mode);
			SetTownViewCameraPoseForDiagnostics({ 0, 0, 4, {} });
		}
		DrawCpu(frame, Check);
		api.sync();
		Check(IsTownCameraMovementInputActive(), "R2 fixture enters the requested eligible camera");
	};
	const auto gamepadTakeover = [&] {
		SDL_Event pad {};
		pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
		DetectInputMethod(pad, { ControllerButton_NONE, false });
		api.sync();
		Check(ControlMode == ControlTypes::Gamepad && ControlDevice == ControlTypes::Gamepad && !api.captured(),
			"native device classifier suspends keyboard capture before the tested takeover DOWN");
	};
	const auto expectWalk = [&](Point delta) {
		plrctrls_after_game_logic();
		const auto commands = DrainEmittedWalks(Check);
		const Point target = origin + Displacement { delta.x, delta.y };
		Check(commands.size() == 1 && Point { commands.front().x, commands.front().y } == target,
			"independent cardinal target observes exactly one native walk from the first DOWN");
	};
	// Native gamepad classifier -> one real keyboard DOWN; no click or second press.
	for (const auto mode : { TownCameraMode::ThirdPerson, TownCameraMode::FirstPerson }) {
		for (const auto key : { SDLK_S, SDLK_W, SDLK_T }) {
			mapper.RestoreMovementDefaults();
			if (key == SDLK_T) Check(forward.SetValue('T'), "configure takeover remap through native option");
			enter(mode);
			gamepadTakeover();
			MovementFixtureNativeTDown = FollowNativeWPressed = false;
			const auto rng = GetLCGEngineState();
			dispatch(key, SDL_SCANCODE_A, true); // Deliberately different from logical key: no reverse lookup.
			Check(ControlMode == ControlTypes::KeyboardAndMouse && ControlDevice == ControlTypes::KeyboardAndMouse,
				"the first native movement DOWN classifies keyboard before deciding ownership");
			Check(!SpellSelectFlag && !MovementFixtureNativeTDown && !FollowNativeWPressed,
				"first S/W/remapped DOWN never leaks to native spell or custom callbacks");
			Check(GetTownFirstPersonMoveDirection() != Direction::NoDirection
				&& api.captured() == (mode == TownCameraMode::FirstPerson),
				"first takeover DOWN moves in either camera, with relative capture only in FPP");
			expectWalk(key == SDLK_S ? Point { 1, 0 } : Point { -1, 0 });
			Check(GetLCGEngineState() == rng, "device takeover never consumes simulation RNG");
			dispatch(key, SDL_SCANCODE_A, false);
			DrainEmittedWalks(Check);
		}
	}
	// Failed acquisition consumes the authorized pair, without native S or retry loop.
	mapper.RestoreMovementDefaults();
	enter(TownCameraMode::FirstPerson);
	gamepadTakeover();
	MovementR2Sensor::RejectAcquire = true;
	const auto attempts = MovementR2Sensor::AcquireCalls;
	dispatch(SDLK_S, SDL_SCANCODE_A, true);
	Check(!SpellSelectFlag && !api.captured() && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
		"failed takeover capture keeps S out of the native spell menu and cannot walk");
	api.sync(); api.sync();
	Check(MovementR2Sensor::AcquireCalls == attempts + 1, "failed takeover capture has no implicit retry loop");
	dispatch(SDLK_S, SDL_SCANCODE_A, false);
	MovementR2Sensor::RejectAcquire = false;

	for (const auto mode : { TownCameraMode::ThirdPerson, TownCameraMode::FirstPerson }) {
		mapper.RestoreMovementDefaults();
		enter(mode);
		dispatch(SDLK_W, SDL_SCANCODE_A, true);
		PhysicalServices::HeldPhysicalKeys.clear(); // Sabotage the legacy keycode-slot mock.
		Check(GetTownFirstPersonMoveDirection() != Direction::NoDirection && MovementR2Sensor::Queries[SDL_SCANCODE_A] > 0,
			"production polling follows original owned scancode, independently of legacy heldArrows");
		const auto releases = MovementR2NativeReleaseCalls;
		dispatch(SDLK_F22, SDL_SCANCODE_A, false);
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection && MovementR2NativeReleaseCalls == releases,
			"different UP keycode with same scancode releases original action and never the new native keycode");

		enter(mode);
		dispatch(SDLK_W, SDL_SCANCODE_A, true);
		Check(forward.SetValue('G'), "remap while original physical scancode remains held");
		MovementR2Sensor::Queries.fill(0);
		api.sync();
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection && MovementR2Sensor::Queries[SDL_SCANCODE_A] == 0,
			"epoch filter rejects old held scancodes before consulting the physical sensor");
		if (mode == TownCameraMode::FirstPerson) api.deliberateResume(); // Binding change still requires deliberate resume.
		dispatch(SDLK_G, SDL_SCANCODE_B, true);
		const auto newDirection = GetTownFirstPersonMoveDirection();
		dispatch(SDLK_W, SDL_SCANCODE_A, true, true);
		dispatch(SDLK_F22, SDL_SCANCODE_A, false);
		Check(newDirection != Direction::NoDirection && GetTownFirstPersonMoveDirection() == newDirection
			&& MovementR2NativeReleaseCalls == releases,
			"old-epoch repeat/changed-keycode UP cannot release a new mapping episode");
		dispatch(SDLK_G, SDL_SCANCODE_B, false);
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection, "new scancode UP stops the new episode");

		enter(mode);
		dispatch(SDLK_G, SDL_SCANCODE_A, true);
		dispatch(SDLK_G, SDL_SCANCODE_B, true);
		const auto paired = GetTownFirstPersonMoveDirection();
		dispatch(SDLK_G, SDL_SCANCODE_A, false);
		Check(paired != Direction::NoDirection && GetTownFirstPersonMoveDirection() == paired,
			"two original scancodes mapped to one slot retain movement after the first UP");
		dispatch(SDLK_F22, SDL_SCANCODE_B, false);
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection && MovementR2NativeReleaseCalls == releases,
			"last changed-keycode scancode UP ends its original slot only");
		for (int invalid : { 0, 2048 }) {
			enter(mode);
			dispatch(SDLK_G, invalid, true);
			Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection,
				"unknown/out-of-range synthetic scancode cannot become physical movement");
			dispatch(SDLK_G, invalid, false);
		}
		mapper.RestoreMovementDefaults();
		enter(mode);
		dispatch(SDLK_W, SDL_SCANCODE_A, true);
		PauseMode = 2; api.sync();
		PauseMode = 0; api.sync();
		if (mode == TownCameraMode::FirstPerson) api.deliberateResume();
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection,
			"pause and resume cannot revive an old physical held key without a fresh DOWN");
		dispatch(SDLK_F22, SDL_SCANCODE_A, false);

		// Actual shortcuts release held keys; Home now restores gameplay TPP.
		for (const auto gateKey : { SDLK_HOME, SDLK_F4 }) {
			enter(mode);
			const int savedCamera = *GetOptions().Graphics.townViewCameraMode;
			dispatch(SDLK_W, SDL_SCANCODE_A, true);
			api.dispatch(FollowKey(gateKey, true), 0);
			api.dispatch(FollowKey(gateKey, false), 0);
			Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection,
				"Home/F4 release old physical movement ownership");
			Check(gateKey == SDLK_HOME
			        ? IsTownViewActive() && GetTownViewCameraMode() == TownCameraMode::ThirdPerson
			            && *GetOptions().Graphics.townViewCameraMode == savedCamera
			        : !IsTownViewActive() && !IsTownCameraMovementInputActive(),
				"Home restores TPP without changing saved preference; F4 disables 3D movement");
			dispatch(SDLK_F22, SDL_SCANCODE_A, false);
			dispatch(SDLK_S, SDL_SCANCODE_B, true);
			Check(gateKey == SDLK_HOME
			        ? !SpellSelectFlag && GetTownFirstPersonMoveDirection() != Direction::NoDirection
			        : SpellSelectFlag && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
				"fresh S uses follow movement after Home and native spell action after F4");
			dispatch(SDLK_S, SDL_SCANCODE_B, false);
			SpellSelectFlag = false;
		}
		enter(mode);
		const auto oldLevelType = leveltype;
		leveltype = DTYPE_CATHEDRAL; // Gate-only setup; no dungeon generation/map/seed changes.
		api.sync();
		dispatch(SDLK_S, SDL_SCANCODE_A, true);
		Check(!IsTownCameraMovementInputActive() && SpellSelectFlag
			&& GetTownFirstPersonMoveDirection() == Direction::NoDirection,
			"outside Town the same key uses native controls and never the town movement context");
		dispatch(SDLK_S, SDL_SCANCODE_A, false);
		leveltype = oldLevelType;
		SpellSelectFlag = false;
	}
	release();
	mapper.RestoreMovementDefaults();
	SetTownFirstPersonInputServicesForDiagnostics(&legacyServices);
	SetTownViewCameraMode(TownCameraMode::Isometric);
	api.sync();
	MovementFixtureNativeTDown = FollowNativeWPressed = false;
}
} // namespace
