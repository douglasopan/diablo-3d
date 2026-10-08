#pragma once

// Production dispatcher/commands with the runner's simulated physical services.
// No GPU, live profile, physical focus/capture or sustained-performance claims.
namespace {
bool FollowNativeWPressed = false;

SDL_Event FollowKey(SDL_Keycode key, bool down, bool repeat = false)
{
	SDL_Event event {};
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
#ifdef USE_SDL3
	event.key.key = key;
	event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
	event.key.down = down;
#else
	event.key.keysym.sym = key;
	event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
	event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
#endif
	event.key.repeat = repeat;
	return event;
}

SDL_Event FollowWheel(float steps, bool flipped = false)
{
	SDL_Event event {};
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.y = static_cast<decltype(event.wheel.y)>(steps);
#if SDL_VERSION_ATLEAST(2, 0, 18) && !defined(USE_SDL3)
	event.wheel.preciseY = steps;
#elif defined(USE_SDL3)
	event.wheel.y = steps;
#endif
	event.wheel.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
	return event;
}

void FollowInputChecks(const ProductionAccess &api)
{
	const Point origin = FindClearPatch(Check);
	const auto releaseAll = [&] {
		api.release();
		for (const auto key : { SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_W, SDLK_A, SDLK_S, SDLK_D })
			api.dispatch(FollowKey(key, false), 0);
		DrainEmittedWalks(Check);
		ClosePanels();
	};
	const auto resume = [&] {
		releaseAll();
		ResumeAt(api, Check, origin, 0);
	};
	const std::array<std::array<SDL_Keycode, 2>, 4> pairs {{{ SDLK_UP, SDLK_W }, { SDLK_DOWN, SDLK_S }, { SDLK_LEFT, SDLK_A }, { SDLK_RIGHT, SDLK_D }}};
	for (const auto pair : pairs) {
		for (const bool arrowFirst : { false, true }) {
			resume();
			api.dispatch(FollowKey(pair[0], true), 0);
			const Direction direction = GetTownFirstPersonMoveDirection();
			api.dispatch(FollowKey(pair[1], true), 0);
			Check(GetTownFirstPersonMoveDirection() == direction && direction != Direction::NoDirection,
			    "arrow and WASD aliases retain one camera-relative direction");
			api.dispatch(FollowKey(pair[arrowFirst ? 0 : 1], false), 0);
			Check(GetTownFirstPersonMoveDirection() == direction,
			    "releasing either alias retains the other physical key");
			api.dispatch(FollowKey(pair[arrowFirst ? 1 : 0], false), 0);
			Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection,
			    "releasing both aliases removes movement");
		}
	}
	for (const uint16_t modifiers : { uint16_t(SDL_KMOD_CTRL), uint16_t(SDL_KMOD_ALT), uint16_t(SDL_KMOD_GUI) }) {
		resume();
		api.dispatch(FollowKey(SDLK_S, true), 0);
		Check(GetTownFirstPersonMoveDirection() != Direction::NoDirection && !SpellSelectFlag,
		    "captured S owns backward movement without opening the native speedbook");
		api.dispatch(FollowKey(SDLK_S, true, true), modifiers);
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection && !SpellSelectFlag,
		    "modifier snapshot stops WASD and owned S repeats cannot open a native binding");
		api.dispatch(FollowKey(SDLK_S, true, true), 0);
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection && !SpellSelectFlag,
		    "modifier removal and repeat do not resurrect the held alias");
		api.dispatch(FollowKey(SDLK_S, false), modifiers);
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection && !SpellSelectFlag,
		    "owned KEYUP remains consumed through modifier changes");
		api.dispatch(FollowKey(SDLK_W, true), modifiers);
		Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection,
		    "fresh modified WASD down stays outside movement ownership");
		api.dispatch(FollowKey(SDLK_W, false), 0);
		api.dispatch(FollowKey(SDLK_W, true), 0);
		Check(GetTownFirstPersonMoveDirection() != Direction::NoDirection,
		    "a fresh unmodified press restores WASD intent");
	}
	resume();
	api.dispatch(FollowKey(SDLK_W, true), SDL_KMOD_SHIFT);
	Check(GetTownFirstPersonMoveDirection() != Direction::NoDirection, "Shift remains compatible with WASD movement");
	invflag = true;
	api.sync();
	api.dispatch(FollowKey(SDLK_W, false), 0);
	Check(!api.captured() && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
	    "owned WASD KEYUP behind inventory clears intent and cannot reacquire capture");
	invflag = false;
	api.deliberateResume();
	Check(api.captured() && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
	    "inventory resume retains no stale WASD movement");
	api.dispatch(FollowKey(SDLK_W, true), 0);
	PhysicalServices::Focus = false;
	api.sync();
	api.dispatch(FollowKey(SDLK_W, false), 0);
	PhysicalServices::Focus = true;
	api.sync();
	Check(!api.captured(), "simulated focus return after WASD release requires deliberate resume");
	api.deliberateResume();
	Check(api.captured() && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
	    "simulated focus cycle does not stick WASD");
	GetOptions().Keymapper.AddAction("FixtureHeldW", "Fixture held W", "Private fixture only", 'W',
	    [] { FollowNativeWPressed = true; }, [] { FollowNativeWPressed = false; });
	for (auto *entry : GetOptions().Keymapper.GetEntries())
		if (entry->key == "FixtureHeldW")
			Check(static_cast<KeymapperOptions::Action *>(entry)->SetValue('W'), "assign only a private in-memory fixture binding");
	api.dispatch(FollowKey(SDLK_W, true), SDL_KMOD_CTRL);
	Check(FollowNativeWPressed, "modified W reaches a real private native binding");
	api.dispatch(FollowKey(SDLK_W, true, true), 0);
	Check(GetTownFirstPersonMoveDirection() == Direction::NoDirection,
	    "unowned native W repeat cannot acquire movement");
	api.dispatch(FollowKey(SDLK_W, false), 0);
	Check(!FollowNativeWPressed, "unowned W release reaches its native binding after modifiers change");
	releaseAll();
	FixturePlacePlayer(origin);
	SetTownViewCameraMode(TownCameraMode::ThirdPerson);
	SetTownViewCameraPoseForDiagnostics({ 0.7F, -0.3F, 0.61F, {} });
	MousePosition = { gnScreenWidth / 2, gnViewportHeight / 2 };
	const auto nativeBefore = Sample();
	const uint32_t rngBefore = GetLCGEngineState();
	api.dispatch(FollowWheel(0.05F), 0);
	Check(GetTownViewCameraMode() == TownCameraMode::ThirdPerson, "precise fractional wheel stays above the FPP threshold");
	api.dispatch(FollowWheel(0.1F), 0);
	Check(GetTownViewCameraMode() == TownCameraMode::FirstPerson && api.captured(),
	    "precise fractional wheel crossing enters FPP and requests capture through the real adapter");
	const auto entered = GetTownViewCameraState();
	Check(std::abs(entered.yaw - 0.7F) < 0.00001F && std::abs(entered.pitch + 0.3F) < 0.00001F,
	    "wheel mode entry preserves yaw and upward pitch");
	api.dispatch(FollowWheel(-0.25F), 0);
	Check(GetTownViewCameraMode() == TownCameraMode::ThirdPerson && !api.captured() && !PhysicalServices::Relative,
	    "scroll-out releases capture and moves into third person");
	const float thirdDistance = GetTownViewCameraState().distance;
	api.dispatch(FollowWheel(-0.25F), 0);
	Check(GetTownViewCameraState().distance > thirdDistance, "continued scroll-out increases third-person requested distance");
	api.dispatch(FollowWheel(-50, true), 0);
	Check(GetTownViewCameraMode() == TownCameraMode::FirstPerson && api.captured(), "flipped wheel sign is applied exactly once");
	for (unsigned i = 0; i < 12; ++i) {
		api.dispatch(FollowWheel(-0.1F), 0);
		api.dispatch(FollowWheel(0.1F), 0);
	}
	Check(GetTownViewCameraMode() == TownCameraMode::ThirdPerson && !api.captured(),
	    "rapid fractional opposing wheel events do not ping-pong capture at the hysteresis band");
	const auto beforeUi = GetTownViewCameraState();
	for (unsigned gate = 0; gate < 4; ++gate) {
		invflag = gate == 0;
		CharFlag = gate == 1;
		AutomapActive = gate == 2;
		ChatFlag = gate == 3;
		api.dispatch(FollowWheel(2), 0);
		Check(first_person_root_supplement::SamePose(beforeUi, GetTownViewCameraState()),
		    "wheel behind inventory/character/automap/chat cannot alter the follow camera");
		ClosePanels();
		AutomapActive = false;
	}
	api.dispatch(FollowWheel(2), SDL_KMOD_CTRL);
	Check(first_person_root_supplement::SamePose(beforeUi, GetTownViewCameraState()), "Ctrl-wheel keeps native behavior without follow zoom");
	Check(Sample() == nativeBefore && GetLCGEngineState() == rngBefore, "wheel/capture/UI routing preserves actor state and RNG");
	releaseAll();
}

void FollowClickChecks(const ProductionAccess &api)
{
	using namespace first_person_root_supplement;
	const bool gpu = *GetOptions().Graphics.townViewGpuRendering;
	const bool aa = *GetOptions().Graphics.townViewAntialiasing;
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	OwnedSurface frame(640, 480);
	const Point origin = FindClearPatch(Check);
	ResumeAt(api, Check, origin, 0);
	const auto pair = FindTwoGroundTargets(frame, origin, Check);
	Pose(pair.yaw, pair.pitch);
	DrawCpu(frame, Check);
	const auto a = Pick();
	Point pixelB { -1, -1 }, targetB;
	for (int y = 16; y < gnViewportHeight && pixelB.x < 0; y += 24) {
		for (int x = 16; x < gnScreenWidth; x += 24) {
			Point tile; int npc, item, player;
			if (PickTownView({x,y}, tile, npc, item, player) && npc == -1 && item == -1 && player == -1
			    && tile != a.tile && tile != origin && PosOkPlayer(*MyPlayer, tile)) {
				pixelB = { x, y }; targetB = tile; break;
			}
		}
	}
	Check(pixelB.x >= 0, "bounded production render exposes an absolute ground target distinct from the old reticle");
	const auto clickB = [&](bool down) {
		auto event = MouseButton(down);
		event.button.x = pixelB.x; event.button.y = pixelB.y;
		api.dispatch(event, 0);
	};
	for (unsigned sequence = 0; sequence < 4; ++sequence) {
		ResumeAt(api, Check, origin, pair.yaw);
		Pose(pair.yaw, pair.pitch);
		DrawCpu(frame, Check);
		CheckCursMove(); // Deliberately establish old target A through production picking.
		api.dispatch(FollowWheel(-1), 0);
		Check(!api.captured() && GetTownViewCameraMode() == TownCameraMode::ThirdPerson, "wheel-out releases capture before its next click batch");
		clickB(true);
		if (sequence == 3) api.dispatch(FollowWheel(50), 0);
		clickB(false);
		Check(TakeWalksWithoutParse(Check).empty(), "absolute click after wheel cannot emit an old-target command before drawing");
		if (sequence == 1) api.dispatch(FollowWheel(50), 0);
		if (sequence == 2) { invflag = true; api.sync(); }
		DrawCpu(frame, Check);
		FlushFirstPersonClicksForDiagnostics();
		const auto commands = TakeWalksWithoutParse(Check);
		if (sequence != 2) {
			Check(commands.size() == 1 && Point {commands[0].x, commands[0].y} == targetB,
			    "post-draw native click uses absolute target B, never previous reticle target A");
			for (const auto &command : commands) ParseObservedWalk(command, Check);
			if (sequence == 1) Check(api.captured() && GetTownViewCameraMode() == TownCameraMode::FirstPerson,
			    "FIFO applies the inverse wheel only after the preceding absolute click and up");
			if (sequence == 3) Check(!api.captured() && GetTownViewCameraMode() == TownCameraMode::ThirdPerson,
			    "wheel rejected during a native held down preserves its matching up and third-person mode");
		} else {
			Check(commands.empty(), "inventory opened before the flush cancels the queued world action");
			invflag = false;
		}
		Check(sgbMouseDown == CLICK_NONE && LastPlayerAction == PlayerActionType::None, "wheel-click sequence leaves no native held-mouse action");
		api.release(); DrainEmittedWalks(Check);
	}
	GetOptions().Graphics.townViewGpuRendering.SetValue(gpu);
	GetOptions().Graphics.townViewAntialiasing.SetValue(aa);
}

} // namespace
