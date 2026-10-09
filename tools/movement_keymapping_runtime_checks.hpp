// Prepared for the existing MPQ-backed CPU runner. Root alone builds/runs.
// Exercises production dispatch/native actions/commands; physical SDL is simulated.
namespace {
bool MovementFixtureNativeTDown = false;

KeymapperOptions::Action &RuntimeMovementSetting(std::string_view id)
{
	for (auto *entry : GetOptions().Keymapper.GetEntries())
		if (entry->key == id)
			return static_cast<KeymapperOptions::Action &>(*entry);
	throw std::runtime_error("Missing native movement action");
}

void MovementKeymappingRuntimeChecks(const ProductionAccess &api)
{
	using namespace first_person_root_supplement;
	auto &mapper = GetOptions().Keymapper;
	auto &forward = RuntimeMovementSetting("TownMoveForward");
	auto &alternate = RuntimeMovementSetting("TownMoveForwardAlternate");
	const Point origin = FindClearPatch(Check);
	OwnedSurface frame(640, 480);
	mapper.AddAction("FixtureNativeT", "Fixture native T", "Private fixture only", 'T',
	    [] { MovementFixtureNativeTDown = true; }, [] { MovementFixtureNativeTDown = false; }).SetValue('T');
	const auto release = [&] {
		const auto held = PhysicalServices::HeldPhysicalKeys;
		for (const auto &[key, bit] : held) api.dispatch(FollowKey(key, false), 0);
		api.release();
		DrainEmittedWalks(Check);
		ClosePanels();
		SpellSelectFlag = AutomapActive = ChatFlag = false;
	};
	const auto enter = [&](TownCameraMode mode, float yaw) {
		release();
		PhysicalServices::Focus = true;
		if (mode == TownCameraMode::FirstPerson) {
			ResumeAt(api, Check, origin, yaw);
		} else {
			FixturePlacePlayer(origin);
			SetTownViewCameraMode(mode);
			SetTownViewCameraPoseForDiagnostics({ yaw, 0, 4, {} });
		}
		DrawCpu(frame, Check);
		api.sync();
		Check(IsTownCameraMovementInputActive(), "requested camera enables contextual movement after native draw");
		Check(api.captured() == (mode == TownCameraMode::FirstPerson),
		    "third-person movement never acquires relative mouse");
	};
	Check(forward.SetValue('T') && alternate.SetValue(SDLK_F24), "configure a non-WASD movement pair through native setters");
	constexpr std::array<Point, 8> expected {{{ -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
	    { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }}};
	for (const auto mode : { TownCameraMode::FirstPerson, TownCameraMode::ThirdPerson }) {
		for (unsigned sector = 0; sector < expected.size(); ++sector) {
			enter(mode, static_cast<float>(sector) * Pi / 4);
			const auto before = Sample();
			const auto rng = GetLCGEngineState();
			api.dispatch(FollowKey(SDLK_T, true), 0);
			const auto direction = GetTownFirstPersonMoveDirection();
			api.dispatch(FollowKey(SDLK_F24, true), 0);
			Check(GetTownFirstPersonMoveDirection() == direction && direction != Direction::NoDirection,
			    "remapped main and alternate keys retain one native movement intent");
			api.dispatch(FollowKey(SDLK_T, false), 0);
			Check(GetTownFirstPersonMoveDirection() == direction && !MovementFixtureNativeTDown,
			    "releasing the main slot preserves alternate movement and consumes its own native pair");
			plrctrls_after_game_logic();
			const auto commands = DrainEmittedWalks(Check);
			const Point target = origin + Displacement { expected[sector].x, expected[sector].y };
			Check(commands.size() == 1 && Point { commands[0].x, commands[0].y } == target,
			    "both cameras remap through the same single adjacent native WalkInDir command");
			const auto after = Sample(); // ParseCmd legitimately updates path/action.
			Check(after.tileX == before.tileX && after.tileY == before.tileY
			        && after.futureX == before.futureX && after.futureY == before.futureY
			        && after.tempX == before.tempX && after.tempY == before.tempY
			        && after.mode == before.mode && after.direction == static_cast<int>(direction)
			        && after.frame == before.frame && after.frameTick == before.frameTick
			        && after.frames == before.frames && after.ticksPerFrame == before.ticksPerFrame
			        && GetLCGEngineState() == rng,
			    "remapped input never writes actor coordinates, animation or simulation RNG");
			api.dispatch(FollowKey(SDLK_F24, false), 0);
			plrctrls_after_game_logic();
			const auto stops = DrainEmittedWalks(Check);
			Check(stops.size() == 1 && Point { stops[0].x, stops[0].y } == MyPlayer->position.future,
			    "releasing the last remapped slot stops only its native queued walk");
		}
		enter(mode, 0);
		api.dispatch(FollowKey(SDLK_T, true), SDL_KMOD_CTRL);
		Check(MovementFixtureNativeTDown && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
		    "modified remapped key keeps the native pressed callback");
		api.dispatch(FollowKey(SDLK_T, true, true), 0);
		api.dispatch(FollowKey(SDLK_T, false), 0);
		Check(!MovementFixtureNativeTDown && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
		    "unowned repeat cannot steal a native release or start remapped movement");
		api.dispatch(FollowKey(SDLK_T, true), 0);
		Check(GetTownFirstPersonMoveDirection() != Direction::NoDirection, "fresh remapped key starts movement");
		Check(forward.SetValue('G'), "remap a currently owned physical key");
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection,
		    "a remap clears held intent and pending owned movement immediately");
		if (mode == TownCameraMode::FirstPerson) api.deliberateResume();
		api.sync();
		api.dispatch(FollowKey(SDLK_G, true), 0);
		const auto newDirection = GetTownFirstPersonMoveDirection();
		api.dispatch(FollowKey(SDLK_T, true, true), 0);
		api.dispatch(FollowKey(SDLK_T, false), 0);
		Check(!MovementFixtureNativeTDown && GetTownFirstPersonMoveDirection() == newDirection
		        && newDirection != Direction::NoDirection,
		    "old owned repeat/UP is consumed by physical identity without releasing the new mapping");
		api.dispatch(FollowKey(SDLK_G, false), 0);
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection, "new remapped UP ends only its new episode");
		Check(forward.SetValue('T'), "restore the fixture custom mapping");
		enter(mode, 0);
		api.dispatch(FollowKey(SDLK_T, true), 0);
		OpenInGameSettings();
		api.sync();
		auto settingsRelease = FollowKey(SDLK_T, false);
		PhysicalServices::Observe(settingsRelease);
		HandleMessage(settingsRelease, 0); // Dispatch through the active settings wrapper.
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection && !IsTownCameraMovementInputActive(),
		    "real settings handler suppresses camera movement and clears the held action");
		CloseInGameSettings();
		gamemenu_off();
		api.sync();
		if (mode == TownCameraMode::FirstPerson) api.deliberateResume();
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection, "return from binding UI cannot revive released movement");
		enter(mode, 0);
		api.dispatch(FollowKey(SDLK_T, true), 0);
		PhysicalServices::Focus = false;
		api.sync();
		api.dispatch(FollowKey(SDLK_T, false), 0);
		PhysicalServices::Focus = true;
		api.sync();
		if (mode == TownCameraMode::FirstPerson) api.deliberateResume();
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection,
		    "simulated focus change clears remapped keys in both cameras without auto-walk");
	}
	release();
	SetTownViewCameraMode(TownCameraMode::Isometric);
	api.sync();
	api.dispatch(FollowKey(SDLK_T, true), 0);
	Check(MovementFixtureNativeTDown && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
	    "the same key retains its native action outside follow-camera movement");
	api.dispatch(FollowKey(SDLK_T, false), 0);
	Check(!MovementFixtureNativeTDown, "native release remains paired outside both camera modes");
	mapper.RestoreMovementDefaults();
	Check(mapper.KeyForAction("DisplaySpells") == 'S' && mapper.KeyForAction("Town3DCameraMode") == 'K'
	        && mapper.KeyForAction("ToggleTown3D") == SDLK_F4,
	    "movement reset never evicts spell, mode-cycle or renderer shortcuts");
	release();
}
} // namespace
