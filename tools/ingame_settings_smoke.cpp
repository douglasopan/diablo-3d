// Asset-free checks of the real in-game settings browser and input handlers.
// Every configuration path points at a newly created temporary directory.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "control/control.hpp"
#include "controls/controller.h"
#include "controls/controller_buttons.h"
#ifndef USE_SDL1
#include "controls/devices/game_controller.h"
#endif
#include "diablo.h"
#include "engine/assets.hpp"
#include "engine/events.hpp"
#include "engine/music_catalog.hpp"
#include "engine/random.hpp"
#include "engine/sound.h"
#include "engine/sound_defs.hpp"
#include "game_mode.hpp"
#include "gamemenu.h"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "ingame_settings.h"
#include "options.h"
#include "player.h"
#include "utils/enum_traits.h"
#include "utils/ini.hpp"
#include "utils/language.h"
#include "utils/paths.h"
#include "utils/sdl_compat.h"
#include "ingame_settings_geometry_checks.hpp"
#include "utils/ui_fwd.h"

namespace devilution {
// The production initializer exists in diablo.cpp; it is not public in diablo.h.
void InitPadmapActions();
} // namespace devilution

namespace {
using namespace devilution;

size_t Checks = 0;
size_t DelegatedEvents = 0;

void Check(bool condition, const std::string &message)
{
	++Checks;
	std::cout << (condition ? "PASS " : "FAIL ") << message << '\n';
	if (!condition)
		throw std::runtime_error(message);
}

void SentinelEventHandler(const SDL_Event &event, uint16_t)
{
	// Exercise the same gmenu key dispatch as the production game handler.
	// Any remaining input would reach gameplay and must not leak from capture.
	if (event.type == SDL_EVENT_KEY_DOWN && gmenu_presskeys(SDLC_EventKey(event)))
		return;
	if (gmenu_is_active() && event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
		sgbMouseDown = CLICK_NONE;
		gmenu_left_mouse(false);
		return;
	}
	++DelegatedEvents;
}

struct ConfigFixture {
	std::filesystem::path directory;
	std::filesystem::path iniPath;

	ConfigFixture()
	{
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		directory = std::filesystem::temp_directory_path() / ("d3d-ingame-settings-" + std::to_string(nonce));
		if (!std::filesystem::create_directory(directory))
			throw std::runtime_error("Cannot create a fresh isolated settings fixture");
		iniPath = directory / "diablo.ini";
		paths::SetBasePath(directory.string());
		paths::SetPrefPath(directory.string());
		paths::SetConfigPath(directory.string());
		paths::SetAssetsPath(directory.string());
		std::ofstream out(iniPath, std::ios::binary);
		out << "[Language]\nCode=en\n";
		if (!out)
			throw std::runtime_error("Cannot write the isolated configuration fixture");
	}

	~ConfigFixture()
	{
		// Remove only exact fixture-owned files, then the empty directory.
		std::error_code ignored;
		std::filesystem::remove(iniPath, ignored);
		std::filesystem::remove(directory, ignored);
	}
};

size_t MenuCount()
{
	if (sgpCurrentMenu == nullptr)
		throw std::runtime_error("Expected an active in-game menu");
	for (size_t i = 0; i <= 8; ++i) {
		if (sgpCurrentMenu[i].fnMenu == nullptr) {
			Check(i <= 8, "menu has at most eight rows");
			return i;
		}
		if (sgpCurrentMenu[i].pszStr == nullptr)
			throw std::runtime_error("A menu row lost its owned label");
	}
	throw std::runtime_error("Menu has no terminator within its eight-row bound");
}

std::string_view Unmarked(std::string_view text)
{
	if (text.starts_with("* "))
		text.remove_prefix(2);
	return text;
}

bool Matches(std::string_view actual, std::string_view wanted)
{
	actual = Unmarked(actual);
	return actual == wanted || (actual.starts_with(wanted) && actual.size() > wanted.size()
	    && (actual[wanted.size()] == ':' || actual[wanted.size()] == ' '));
}

std::optional<size_t> FindHere(std::string_view label)
{
	const size_t count = MenuCount();
	for (size_t row = 0; row < count; ++row) {
		if (Matches(sgpCurrentMenu[row].pszStr, label))
			return row;
	}
	return std::nullopt;
}

void Activate(size_t row)
{
	if (row >= MenuCount())
		throw std::runtime_error("Activation would cross the current menu");
	gmenu_select_index(row);
	Check(gmenu_selected_index() == row, "row selection uses the production gmenu index");
	Check(gmenu_presskeys(SDLK_RETURN), "Enter dispatches through the production menu handler");
}

size_t FindPaged(std::string_view requestedLabel)
{
	// Numbered key/pad actions return a view into a mutable name cache.
	// Changing page asks GetName() again and may replace that string's
	// allocation. Own the requested text before dispatching any row handler.
	const std::string label(requestedLabel);
	// A finite cap makes a navigation regression fail instead of hanging.
	for (size_t page = 0; page < 64; ++page) {
		if (const auto found = FindHere(label))
			return *found;
		const auto next = FindHere(_("Next Page"));
		if (!next)
			break;
		Activate(*next);
	}
	throw std::runtime_error("Expected reachable menu row: " + std::string(label));
}

void ActivateNamed(std::string_view label)
{
	Activate(FindPaged(label));
}

void Back()
{
	ActivateNamed(_("Previous Menu"));
}

void OpenRoot()
{
	CloseInGameSettings();
	gamemenu_on();
	Check(MenuCount() == (gbIsMultiplayer ? 3 : 5), "native game menu retains save/load and exit scope");
	Activate(0);
	Check(IsInGameSettingsOpen(), "Esc menu opens the shared settings browser");
	Check(CurrentEventHandler != SentinelEventHandler, "settings installs its own input wrapper");
	Check(Matches(sgpCurrentMenu[0].pszStr, GetOptions().Music.GetName()), "Soundtrack is the first settings category");
}

void OpenCategory(OptionCategoryBase &category)
{
	OpenRoot();
	ActivateNamed(category.GetName());
}

void SendKey(SDL_Keycode key, bool up = false)
{
	SDL_Event event {};
	event.type = up ? SDL_EVENT_KEY_UP : SDL_EVENT_KEY_DOWN;
#ifdef USE_SDL3
	event.key.key = key;
#else
	event.key.keysym.sym = key;
#endif
	HandleMessage(event, 0);
}

void SendMouseButton(uint8_t button, bool up = false)
{
	SDL_Event event {};
	event.type = up ? SDL_EVENT_MOUSE_BUTTON_UP : SDL_EVENT_MOUSE_BUTTON_DOWN;
	event.button.button = button;
	HandleMessage(event, 0);
}

void SendWheel(int x, int y)
{
#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_Event event {};
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.x = x;
	event.wheel.y = y;
	HandleMessage(event, 0);
#else
	(void)x;
	(void)y;
#endif
}

bool Visible(const OptionEntryBase &entry)
{
	const auto flags = entry.GetFlags();
	if (HasAnyOf(flags, OptionEntryFlags::NeedDiabloMpq) && !HaveIntro())
		return false;
	return HasNoneOf(flags, OptionEntryFlags::Invisible
	    | (gbIsHellfire ? OptionEntryFlags::OnlyDiablo : OptionEntryFlags::OnlyHellfire));
}

bool Blocked(const OptionEntryBase &entry)
{
	const auto flags = entry.GetFlags();
	return HasAnyOf(flags, OptionEntryFlags::CantChangeInGame | OptionEntryFlags::RecreateUI)
	    || (gbIsMultiplayer && HasAnyOf(flags, OptionEntryFlags::CantChangeInMultiPlayer));
}

std::string OptionsSnapshot()
{
	std::string snapshot;
	for (OptionCategoryBase *category : GetOptions().GetCategories()) {
		for (OptionEntryBase *entry : category->GetEntries()) {
			snapshot.append(category->GetKey()).push_back('/');
			snapshot.append(entry->key).push_back('=');
			if (entry->GetType() == OptionEntryType::List && static_cast<OptionEntryListBase *>(entry)->GetListSize() == 0)
				snapshot.append("<empty list>");
			else
				snapshot.append(entry->GetValueDescription());
			snapshot.push_back('\n');
		}
	}
	return snapshot;
}

void CheckCoverageAndRestrictions()
{
	for (const bool hellfire : { false, true }) {
		gbIsHellfire = hellfire;
		for (const bool multiplayer : { false, true }) {
			gbIsMultiplayer = multiplayer;
			size_t visibleCount = 0;
			size_t blockedCount = 0;
			for (OptionCategoryBase *category : GetOptions().GetCategories()) {
				for (OptionEntryBase *entry : category->GetEntries()) {
					if (!Visible(*entry))
						continue;
					++visibleCount;
					OpenCategory(*category);
					const size_t row = FindPaged(entry->GetName());
					Check(sgpCurrentMenu[row].enabled(), "visible preference is focusable: " + std::string(entry->key));
					if (!Blocked(*entry))
						continue;
					++blockedCount;
					const std::string before = OptionsSnapshot();
					const auto tickRate = sgGameInitInfo.nTickRate;
					const auto runInTown = sgGameInitInfo.bRunInTown;
					const auto friendlyFire = sgGameInitInfo.bFriendlyFire;
					const auto quests = sgGameInitInfo.fullQuests;
					Activate(row);
					Check(MenuCount() == 1 && FindHere(_("Previous Menu")).has_value(),
					    "restricted preference opens details with a way back: " + std::string(entry->key));
					SendKey(SDLK_RIGHT);
					SendKey(SDLK_RETURN);
					Check(OptionsSnapshot() == before && sgGameInitInfo.nTickRate == tickRate
					        && sgGameInitInfo.bRunInTown == runInTown && sgGameInitInfo.bFriendlyFire == friendlyFire
					        && sgGameInitInfo.fullQuests == quests,
					    "restricted preference cannot mutate options or session rules: " + std::string(entry->key));
				}
			}
			std::cout << "COVERAGE " << (hellfire ? "Hellfire" : "Diablo") << ' '
			          << (multiplayer ? "multiplayer" : "single-player") << ": " << visibleCount
			          << " visible model entries, " << blockedCount << " read-only\n";
		}
	}
	gbIsHellfire = false;
	gbIsMultiplayer = false;
}

void CheckDynamicNamePaging()
{
	for (const std::string_view key : { "QuickSpell10", "QuickMessage10" }) {
		OptionEntryBase *action = nullptr;
		for (OptionEntryBase *entry : GetOptions().Keymapper.GetEntries()) {
			if (entry->key == key) {
				action = entry;
				break;
			}
		}
		Check(action != nullptr, "numbered action exists for mutable-name paging regression: " + std::string(key));
		OpenCategory(GetOptions().Keymapper);
		const std::string expected(action->GetName());
		// Deliberately pass the model's borrowed view. The paged search must
		// retain its text while real menu handlers regenerate dynamic names.
		const size_t row = FindPaged(action->GetName());
		Check(std::string_view(sgpCurrentMenu[row].pszStr) == expected,
		    "paged search preserves the exact numbered action name: " + std::string(key));
	}
}

void CheckNavigation()
{
	OpenRoot();
	const std::string first(sgpCurrentMenu[0].pszStr);
	Check(FindHere(_("Next Page")).has_value(), "root settings categories have a next page");
	SendKey(SDLK_PAGEDOWN);
	Check(std::string_view(sgpCurrentMenu[0].pszStr) != first && FindHere(_("Previous Page")).has_value(),
	    "Page Down moves to the next category page");
	SendKey(SDLK_PAGEUP);
	Check(std::string_view(sgpCurrentMenu[0].pszStr) == first, "Page Up returns to the original category page");
	ActivateNamed(GetOptions().Music.GetName());
	Check(FindHere(_("Next Page")).has_value(), "all nine soundtrack preferences can be paged");
	ActivateNamed(_("Next Page"));
	SendKey(SDLK_ESCAPE);
	Check(IsInGameSettingsOpen() && Matches(sgpCurrentMenu[0].pszStr, GetOptions().Music.GetName()),
	    "Esc from a category returns to settings categories");
	SendKey(SDLK_ESCAPE);
	Check(!IsInGameSettingsOpen() && MenuCount() == 5 && CurrentEventHandler == SentinelEventHandler,
	    "Esc from settings restores the native game menu and event handler");
	OpenRoot();
	gamemenu_off();
	Check(!IsInGameSettingsOpen() && !gmenu_is_active() && CurrentEventHandler == SentinelEventHandler,
	    "closing the game menu also releases the settings input wrapper");
}

void CheckChanges()
{
	Options &options = GetOptions();
	OpenCategory(options.Audio);
	const bool beforeWalking = *options.Audio.walkingSound;
	ActivateNamed(options.Audio.walkingSound.GetName());
	Check(*options.Audio.walkingSound != beforeWalking, "boolean preference uses its shared setter immediately");
	OpenCategory(options.Gameplay);
	ActivateNamed(options.Gameplay.storeUi.GetName());
	Check(MenuCount() == 4, "three store layouts use a value list and Previous Menu");
	ActivateNamed(options.Gameplay.storeUi.GetListDescription(1));
	Check(*options.Gameplay.storeUi == StoreUi::ListWithItemGraphics, "list selection uses the shared stable enum value");
	OpenCategory(options.Graphics);
	const bool startBefore = *options.Graphics.townViewStartIn3D;
	ActivateNamed(options.Graphics.townViewStartIn3D.GetName());
	Check(*options.Graphics.townViewStartIn3D != startBefore, "next-session startup preference can be saved during play");
	OpenCategory(options.Gameplay);
	const bool runBefore = *options.Gameplay.runInTown;
	const auto sessionRun = sgGameInitInfo.bRunInTown;
	ActivateNamed(options.Gameplay.runInTown.GetName());
	Check(*options.Gameplay.runInTown != runBefore && sgGameInitInfo.bRunInTown == sessionRun,
	    "Run in Town saves the next-session preference without rewriting current rules");
	OpenCategory(options.Music);
	ActivateNamed(options.Music.theme.GetName());
	ActivateNamed(options.Music.theme.GetListDescription(0));
	Check(*options.Music.theme == MusicTheme::Vanilla, "soundtrack mode uses the shared theme selector");
	OpenCategory(options.Music);
	ActivateNamed(options.Music.town.GetName());
	Check(MenuCount() == 6, "Tristram exposes all five stable music choices and Previous Menu");
	ActivateNamed(options.Music.town.GetListDescription(4));
	Check(*options.Music.town == MusicVariant::Third && *options.Music.theme == MusicTheme::Custom,
	    "Tristram 3 preserves ID 4 and activates Custom through the engine callback");
	OpenCategory(options.Music);
	ActivateNamed(options.Music.town.GetName());
	ActivateNamed(options.Music.town.GetListDescription(3));
	Check(*options.Music.town == MusicVariant::Random && static_cast<int>(*options.Music.town) == 3,
	    "Random preserves its previously persisted ID 3");
}

void SetSlider(OptionCategoryBase &category, std::string_view label, int min, int max, int value)
{
	OpenCategory(category);
	ActivateNamed(label);
	Check(MenuCount() == 2 && sgpCurrentMenu[0].isSlider(), "legacy control uses a dedicated slider view: " + std::string(label));
	gmenu_slider_set(&sgpCurrentMenu[0], min, max, value);
	sgpCurrentMenu[0].fnMenu(false);
}

void CheckSliders()
{
	Options &options = GetOptions();
	// Initialize the real pipeline on the dummy device: the Sound handler
	// needs its mutex even when there are no loaded sound samples.
#ifndef NOSOUND
	options.Audio.musicVolume.SetValue(VOLUME_MIN);
	options.Audio.soundVolume.SetValue(VOLUME_MIN);
	snd_init();
	Check(gbSndInited && !gbMusicOn && !gbSoundOn, "isolated dummy audio initializes with music and sound muted");
	SetSlider(options.Audio, _("Music"), VOLUME_MIN, VOLUME_MAX, VOLUME_MIN);
	Check(*options.Audio.musicVolume == VOLUME_MIN && !gbMusicOn, "Music slider respects native mute");
	SetSlider(options.Audio, _("Sound"), VOLUME_MIN, VOLUME_MAX, VOLUME_MIN);
	Check(*options.Audio.soundVolume == VOLUME_MIN && !gbSoundOn, "Sound slider respects native mute");
	SetSlider(options.Audio, _("Audio Cues Volume"), VOLUME_MIN, VOLUME_MAX, VOLUME_MIN);
	Check(*options.Audio.audioCuesVolume == VOLUME_MIN, "navigation cues have an independent volume control");
	snd_deinit();
#else
	std::cout << "SKIP audio sliders in a NOSOUND build\n";
#endif
	SetSlider(options.Graphics, _("Gamma"), 0, 100, 100);
	Check(*options.Graphics.brightness == 100, "Gamma preserves the native brightness handler");
	SetSlider(options.Gameplay, _("Speed"), 20, 50, 35);
	Check(*options.Gameplay.tickRate == 35 && sgGameInitInfo.nTickRate == 35, "single-player Speed preserves the native tick-rate handler");
	gbIsMultiplayer = true;
	OpenCategory(options.Gameplay);
	const auto beforeSpeed = sgGameInitInfo.nTickRate;
	ActivateNamed(_("Speed"));
	if (MenuCount() == 2 && sgpCurrentMenu[0].isSlider()) {
		gmenu_slider_set(&sgpCurrentMenu[0], 20, 50, 50);
		sgpCurrentMenu[0].fnMenu(false);
	}
	Check(sgGameInitInfo.nTickRate == beforeSpeed && *options.Gameplay.tickRate == 35,
	    "multiplayer Speed cannot change the session tick rate");
	gbIsMultiplayer = false;
	gbSndInited = false;
}

KeymapperOptions::Action &InventoryKey()
{
	for (OptionEntryBase *entry : GetOptions().Keymapper.GetEntries()) {
		if (entry->key == "Inventory")
			return static_cast<KeymapperOptions::Action &>(*entry);
	}
	throw std::runtime_error("The production Inventory key action was not initialized");
}

void CheckKeyCapture()
{
	auto &key = InventoryKey();
	OpenCategory(GetOptions().Keymapper);
	ActivateNamed(key.GetName());
	Check(MenuCount() == 3, "key binding exposes bind, unbind and Previous Menu");
	ActivateNamed(_("Bind key"));
	SendKey(SDLK_F24);
	SendKey(SDLK_F24, true);
	Check(key.GetValueDescription() == "F24", "real keyboard events rebind the selected action");
	ActivateNamed(_("Bind key"));
	SendKey(SDLK_SPACE);
	SendKey(SDLK_SPACE); // Repeat while held would otherwise reactivate Bind.
	Check(MenuCount() == 3 && FindHere(_("Bind key")).has_value(), "the completing key cannot repeat into the binding menu");
	SendKey(SDLK_SPACE, true);
	ActivateNamed(_("Bind key"));
	SendMouseButton(SDL_BUTTON_X1);
	SendMouseButton(SDL_BUTTON_X1, true);
	Check(key.GetValueDescription() == "X1MOUSE", "real mouse events use the existing mouse-button mask");
#if SDL_VERSION_ATLEAST(2, 0, 0)
	ActivateNamed(_("Bind key"));
	SendWheel(0, 1);
	Check(key.GetValueDescription() == "SCROLLUPMOUSE", "real wheel events bind the native scroll action");
	ActivateNamed(_("Bind key"));
	SendWheel(-1, 0);
	Check(key.GetValueDescription() == "SCROLLLEFTMOUSE", "horizontal wheel binding remains available");
#endif
	ActivateNamed(_("Bind key"));
	const std::string beforeCancel(key.GetValueDescription());
	SendKey(SDLK_ESCAPE);
	SendKey(SDLK_ESCAPE, true);
	Check(key.GetValueDescription() == beforeCancel && IsInGameSettingsOpen(), "Esc cancels key capture without rebinding or leaving settings");
	ActivateNamed(_("Unbind key"));
	Check(key.GetValueDescription().empty(), "Unbind key uses the production mapping setter");
	ActivateNamed(_("Bind key"));
	SendKey(SDLK_F24);
	SendKey(SDLK_F24, true);
	Check(key.GetValueDescription() == "F24", "key input remains usable after cancellation and unbinding");
	// LeftMouseDown sets CLICK_LEFT before the row handler enters capture.
	// Reproduce that real ordering and ensure consuming the UP cannot leave
	// the native mouse state latched, which would block subsequent clicks.
	const auto bindingLayout = gmenu_settings_geometry({ gnScreenWidth, gnScreenHeight }, GetMainPanel().position.y, MenuCount());
	const Rectangle bindRow = bindingLayout.row(FindPaged(_("Bind key")));
	MousePosition = bindRow.position + Displacement { bindRow.size.width / 2, bindRow.size.height / 2 };
	sgbMouseDown = CLICK_LEFT;
	Check(gmenu_left_mouse(true), "mouse activates the real Bind key row");
	Check(sgbMouseDown == CLICK_NONE, "starting capture releases the preceding native left-click state");
	SendMouseButton(SDL_BUTTON_LEFT, true);
	SendKey(SDLK_F24);
	SendKey(SDLK_F24, true);
	Check(sgbMouseDown == CLICK_NONE && key.GetValueDescription() == "F24", "capture completion does not leave the mouse held");
	const auto backLayout = gmenu_settings_geometry({ gnScreenWidth, gnScreenHeight }, GetMainPanel().position.y, MenuCount());
	const Rectangle backRow = backLayout.row(FindPaged(_("Previous Menu")));
	MousePosition = backRow.position + Displacement { backRow.size.width / 2, backRow.size.height / 2 };
	sgbMouseDown = CLICK_LEFT;
	Check(gmenu_left_mouse(true), "a subsequent mouse click can activate Previous Menu");
	SendMouseButton(SDL_BUTTON_LEFT, true);
	Check(sgbMouseDown == CLICK_NONE && FindHere(key.GetName()).has_value(), "mouse return restores the original keymapping page");
	Check(DelegatedEvents == 0, "captured keyboard and mouse input never reaches the previous gameplay handler");
}

#if !defined(USE_SDL1) && !defined(USE_SDL3) && SDL_VERSION_ATLEAST(2, 0, 14)
struct VirtualPad {
	int deviceIndex = -1;
	SDL_Joystick *joystick = nullptr;
	SDL_JoystickID instance = -1;

	VirtualPad()
	{
		deviceIndex = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
		if (deviceIndex < 0)
			return;
		joystick = SDL_JoystickOpen(deviceIndex);
		if (joystick == nullptr)
			return;
		instance = SDL_JoystickInstanceID(joystick);
		GameController::Add(deviceIndex);
	}

	~VirtualPad()
	{
		if (instance >= 0) {
			SDL_GameController *controller = SDL_GameControllerFromInstanceID(instance);
			GameController::Remove(instance);
			if (controller != nullptr)
				SDL_GameControllerClose(controller);
		}
		if (joystick != nullptr)
			SDL_JoystickClose(joystick);
		if (deviceIndex >= 0)
			SDL_JoystickDetachVirtual(deviceIndex);
	}

	bool Ready() const
	{
		return joystick != nullptr && GameController::Get(instance) != nullptr;
	}

	void Send(SDL_GameControllerButton button, bool up)
	{
		Check(SDL_JoystickSetVirtualButton(joystick, static_cast<int>(button), up ? 0 : 1) == 0,
		    "isolated virtual gamepad updates its button state");
		SDL_JoystickUpdate();
		SDL_Event event {};
		event.type = up ? SDL_CONTROLLERBUTTONUP : SDL_CONTROLLERBUTTONDOWN;
		event.cbutton.which = instance;
		event.cbutton.button = static_cast<uint8_t>(button);
		event.cbutton.state = up ? SDL_RELEASED : SDL_PRESSED;
		HandleMessage(event, 0);
	}
};
#endif

PadmapperOptions::Action &FirstPadAction()
{
	const auto entries = GetOptions().Padmapper.GetEntries();
	if (entries.empty())
		throw std::runtime_error("The production pad actions were not initialized");
	return static_cast<PadmapperOptions::Action &>(*entries.front());
}

void CheckPadCapture()
{
	auto &pad = FirstPadAction();
	OpenCategory(GetOptions().Padmapper);
	ActivateNamed(pad.GetName());
	Check(MenuCount() == 3, "gamepad binding exposes bind, unbind and Previous Menu");
	ActivateNamed(_("Unbind button combo"));
	Check(pad.boundInput.button == ControllerButton_NONE, "Unbind combo uses the production pad setter");
#if !defined(USE_SDL1) && !defined(USE_SDL3) && SDL_VERSION_ATLEAST(2, 0, 14)
	VirtualPad virtualPad;
	if (!virtualPad.Ready()) {
		std::cout << "SKIP virtual gamepad capture: SDL did not expose a virtual controller\n";
		return;
	}
	ActivateNamed(_("Bind button combo"));
	virtualPad.Send(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false);
	virtualPad.Send(SDL_CONTROLLER_BUTTON_X, false);
	virtualPad.Send(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true);
	virtualPad.Send(SDL_CONTROLLER_BUTTON_X, true);
	Check(pad.boundInput.modifier == ControllerButton_BUTTON_LEFTSHOULDER
	        && pad.boundInput.button == ControllerButton_BUTTON_X,
	    "real virtual-controller events capture a modifier and button combo");
	Check(MenuCount() == 3 && FindHere(_("Bind button combo")).has_value(), "releasing all combo buttons finishes capture");
	ActivateNamed(_("Bind button combo"));
	const ControllerButtonCombo before = pad.boundInput;
	SendKey(SDLK_ESCAPE);
	SendKey(SDLK_ESCAPE, true);
	Check(pad.boundInput.modifier == before.modifier && pad.boundInput.button == before.button,
	    "Esc cancels gamepad capture without changing the saved combo");
	Check(DelegatedEvents == 0, "captured gamepad events never reach gameplay");
#else
	std::cout << "SKIP virtual gamepad capture requires SDL2 >= 2.0.14\n";
#endif
}

void CheckPersistence(const ConfigFixture &fixture)
{
	Options &options = GetOptions();
	const bool walking = *options.Audio.walkingSound;
	const bool start = *options.Graphics.townViewStartIn3D;
	const bool run = *options.Gameplay.runInTown;
	const ControllerButtonCombo pad = FirstPadAction().boundInput;
	CloseInGameSettings();
	Check(CurrentEventHandler == SentinelEventHandler, "explicit settings close restores its previous input handler");
	std::ifstream input(fixture.iniPath, std::ios::binary);
	const std::string saved { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	const auto parsed = Ini::parse(saved);
	Check(parsed.has_value(), "the browser saves a valid isolated INI before closing");
	Check(parsed->getBool("Audio", "Walking Sound", !walking) == walking, "immediate audio preference persists through browser close");
	Check(parsed->getBool("Graphics", "Start in 3D", !start) == start, "next-session graphics preference persists through browser close");
	Check(parsed->getBool("Game", "Run in Town", !run) == run, "next-session gameplay preference persists independently of active rules");
	Check(parsed->getInt("Game", "Store UI", -1) == static_cast<int>(StoreUi::ListWithItemGraphics), "shared list values retain their native INI representation");
	Check(parsed->getInt("Music", "Theme", -1) == static_cast<int>(MusicTheme::Custom)
	        && parsed->getInt("Music", "Town", -1) == 3,
	    "soundtrack preference and stable Random ID persist independently");
	Check(parsed->getString("Keymapping", "Inventory") == "F24", "captured keyboard binding persists under its native action key");
	options.Audio.walkingSound.SetValue(!walking);
	options.Graphics.townViewStartIn3D.SetValue(!start);
	options.Gameplay.runInTown.SetValue(!run);
	InventoryKey().SetValue(SDLK_UNKNOWN);
	FirstPadAction().SetValue(ControllerButton_NONE);
	LoadOptions();
	Check(*options.Audio.walkingSound == walking && *options.Graphics.townViewStartIn3D == start
	        && *options.Gameplay.runInTown == run && *options.Gameplay.storeUi == StoreUi::ListWithItemGraphics,
	    "native option loader restores choices saved by the in-game browser");
	Check(InventoryKey().GetValueDescription() == "F24" && FirstPadAction().boundInput.modifier == pad.modifier
	        && FirstPadAction().boundInput.button == pad.button,
	    "native loaders restore keyboard and gamepad bindings saved during play");
}

} // namespace

int main()
{
	SDL_SetMainReady();
#ifdef USE_SDL3
	SDL_SetHint("SDL_VIDEODRIVER", "dummy");
	SDL_SetHint("SDL_AUDIODRIVER", "dummy");
#else
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
#ifdef USE_SDL3
	const bool initialized = SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);
#elif defined(USE_SDL1)
	const bool initialized = SDL_Init(0) == 0;
#else
	const bool initialized = SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) == 0;
#endif
	if (!initialized) {
		std::cerr << "SDL fixture initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}
	try {
		HeadlessMode = true;
		const ConfigFixture fixture;
		gbIsHellfire = gbIsMultiplayer = false;
		gbRunGame = true;
		gbSndInited = gbMusicOn = gbSoundOn = false;
		Players.resize(1);
		MyPlayer = &Players.front();
		MyPlayer->_pmode = PM_STAND;
		MyPlayerIsDead = false;
		sgGameInitInfo.nTickRate = 20;
		sgGameInitInfo.bRunInTown = 0;
		InitKeymapActions();
		InitPadmapActions();
		LoadOptions();
		gnScreenWidth = 640;
		gnScreenHeight = 480;
		CalculatePanelAreas();
		gmenu_init_menu();
		SetEventHandler(SentinelEventHandler);
		const uint32_t rngBefore = GetLCGEngineState();
		CheckInGameSettingsGeometry(Check);
		CheckDynamicNamePaging();
		CheckCoverageAndRestrictions();
		CheckNavigation();
		CheckChanges();
		CheckSliders();
		CheckKeyCapture();
		CheckPadCapture();
		CheckPersistence(fixture);
		gamemenu_off();
		Check(GetLCGEngineState() == rngBefore, "settings navigation, audio choices and input capture preserve simulation RNG");
		Check(CurrentEventHandler == SentinelEventHandler && !IsInGameSettingsOpen() && !gmenu_is_active(),
		    "fixture leaves no active browser or input wrapper");
		gbRunGame = false;
		std::cout << "PASS " << Checks << " checks; no window, game archive, player profile or save was used\n";
	} catch (const std::exception &error) {
		CloseInGameSettings();
		if (gbSndInited)
			snd_deinit();
		std::cerr << "FAIL after " << Checks << " checks: " << error.what() << '\n';
		SDL_Quit();
		return 1;
	}
	SDL_Quit();
	return 0;
}
