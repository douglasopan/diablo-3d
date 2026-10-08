// Asset-free checks for soundtrack selection, session startup and isolated INI persistence.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#if !defined(NOSOUND) && !defined(USE_SDL3)
#include <Aulib/Stream.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include "engine/assets.hpp"
#include "engine/music_catalog.hpp"
#include "engine/render/town_view.hpp"
#include "engine/sound_defs.hpp"
#include "game_mode.hpp"
#include "gamemenu.h"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "options.h"
#include "player.h"
#include "utils/ini.hpp"
#include "utils/paths.h"

namespace {
using namespace devilution;

size_t Checks = 0;

void Check(bool condition, const std::string &message)
{
	++Checks;
	std::cout << (condition ? "PASS " : "FAIL ") << message << '\n';
	if (!condition)
		throw std::runtime_error(message);
}

// All configuration reads/writes are confined to this newly created fixture.
// It deliberately has no MPQ, models, music or save files.
struct ConfigFixture {
	std::filesystem::path directory;
	std::filesystem::path iniPath;
	std::filesystem::path corruptAudioPath;

	ConfigFixture()
	{
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		directory = std::filesystem::temp_directory_path() / ("d3d-music-selection-" + std::to_string(nonce));
		if (!std::filesystem::create_directory(directory))
			throw std::runtime_error("Cannot create a fresh isolated configuration directory");
		iniPath = directory / "diablo.ini";
		corruptAudioPath = directory / "invalid.mp3";
		paths::SetBasePath(directory.string());
		paths::SetPrefPath(directory.string());
		paths::SetConfigPath(directory.string());
		paths::SetAssetsPath(directory.string());
	}

	~ConfigFixture()
	{
		std::error_code ignored;
		std::filesystem::remove(iniPath, ignored);
		std::filesystem::remove(corruptAudioPath, ignored);
		std::filesystem::remove(directory, ignored);
	}
};

void CheckDefaults()
{
	const Options &options = GetOptions();
	Check(*options.Music.theme == MusicTheme::Rock, "new profiles prefer the Rock soundtrack");
	Check(*options.Graphics.townViewStartIn3D, "new profiles prefer starting in 3D");
	for (unsigned i = 0; i < NUM_MUSIC; ++i) {
		const auto track = static_cast<_music_id>(i);
		Check(*options.Music.ForTrack(track) == MusicVariant::Rock, "Rock default for " + std::string(MusicTrackCatalog[i].key));
		Check(options.Music.ForTrack(track).GetListSize() == (track == TMUSIC_INTRO ? 3U : 2U),
		    "only the main menu offers the previous alternative: " + std::string(MusicTrackCatalog[i].key));
	}
}

void CheckSelections()
{
	constexpr std::array<const char *, NUM_MUSIC> OriginalPaths {
		"music\\dtowne.wav", "music\\dlvla.wav", "music\\dlvlb.wav", "music\\dlvlc.wav",
		"music\\dlvld.wav", "music\\dlvlf.wav", "music\\dlvle.wav", "music\\dintro.wav",
	};
	constexpr std::array<const char *, NUM_MUSIC> SharewarePaths {
		"music\\stowne.wav", "music\\slvla.wav", "music\\slvla.wav", "music\\slvla.wav",
		"music\\slvla.wav", "music\\dlvlf.wav", "music\\dlvle.wav", "music\\sintro.wav",
	};
	const auto installed = [](const char *) { return true; };
	const auto missing = [](const char *) { return false; };
	const auto menuOnly = [](const char *path) {
		return std::string_view(path) == "music\\d3d\\menu-rock2.mp3"
		    || std::string_view(path) == "music\\d3d\\menu-alternative.mp3";
	};

	for (unsigned i = 0; i < NUM_MUSIC; ++i) {
		const auto track = static_cast<_music_id>(i);
		const std::string key(MusicTrackCatalog[i].key);
		const auto vanilla = ResolveMusicSelection(track, MusicTheme::Vanilla, MusicVariant::Rock, true, installed);
		Check(std::string_view(vanilla.path) == OriginalPaths[i] && vanilla.actual == MusicVariant::Original && !vanilla.fallback,
		    "Vanilla ignores installed replacements and retains the native ID: " + key);
		const auto fallback = ResolveMusicSelection(track, MusicTheme::Rock, MusicVariant::Original, true, missing);
		Check(std::string_view(fallback.path) == OriginalPaths[i] && fallback.actual == MusicVariant::Original && fallback.fallback,
		    "missing Rock returns to the retail original: " + key);
		const auto spawn = ResolveMusicSelection(track, MusicTheme::Rock, MusicVariant::Rock, false, missing);
		Check(std::string_view(spawn.path) == SharewarePaths[i] && spawn.actual == MusicVariant::Original && spawn.fallback,
		    "missing Rock preserves the shareware mapping: " + key);

		const auto menuOriginal = ResolveMusicSelection(track, MusicTheme::Custom, MusicVariant::Rock, true, installed,
		    true, MusicVariant::Original);
		Check(std::string_view(menuOriginal.path) == OriginalPaths[i] && menuOriginal.actual == MusicVariant::Original,
		    "custom original menu rotation ignores the level's Rock choice: " + key);
		const auto menuRock = ResolveMusicSelection(track, MusicTheme::Custom, MusicVariant::Original, true, installed,
		    true, MusicVariant::Rock);
		Check(std::string_view(menuRock.path) == "music\\d3d\\menu-rock2.mp3" && menuRock.actual == MusicVariant::Rock,
		    "custom Rock menu uses Rock2 for every inherited menu ID: " + key);

		const auto invalidTheme = ResolveMusicSelection(track, static_cast<MusicTheme>(255), MusicVariant::Rock, true, installed);
		Check(std::string_view(invalidTheme.path) == OriginalPaths[i] && invalidTheme.actual == MusicVariant::Original,
		    "unknown theme safely uses the original: " + key);
		const auto invalidChoice = ResolveMusicSelection(track, MusicTheme::Custom, static_cast<MusicVariant>(255), true, installed);
		Check(std::string_view(invalidChoice.path) == OriginalPaths[i] && invalidChoice.actual == MusicVariant::Original,
		    "unknown variant safely uses the original: " + key);
	}

	const auto defaultMenu = ResolveMusicSelection(TMUSIC_INTRO, MusicTheme::Rock, MusicVariant::Original, true, menuOnly);
	Check(std::string_view(defaultMenu.path) == "music\\d3d\\menu-rock2.mp3" && !defaultMenu.fallback,
	    "Rock2 is the installed default menu music");
	const auto alternative = ResolveMusicSelection(TMUSIC_CATACOMBS, MusicTheme::Custom, MusicVariant::Rock, true, menuOnly,
	    true, MusicVariant::Alternative);
	Check(std::string_view(alternative.path) == "music\\d3d\\menu-alternative.mp3" && alternative.actual == MusicVariant::Alternative,
	    "earlier Main Menu remains independently selectable during menu rotation");
	const auto missingAlternative = ResolveMusicSelection(TMUSIC_INTRO, MusicTheme::Custom, MusicVariant::Alternative, true, missing);
	Check(std::string_view(missingAlternative.path) == "music\\dintro.wav" && missingAlternative.fallback,
	    "missing earlier menu music falls back without selecting Rock2");
	const auto gameRock = ResolveMusicSelection(TMUSIC_TOWN, MusicTheme::Custom, MusicVariant::Rock, true, installed,
	    false, MusicVariant::Original);
	Check(std::string_view(gameRock.path) == "music\\d3d\\town-rock.mp3", "game location Rock choice ignores the menu's original choice");
	const auto gameOriginal = ResolveMusicSelection(TMUSIC_HELL, MusicTheme::Custom, MusicVariant::Original, true, installed,
	    false, MusicVariant::Alternative);
	Check(std::string_view(gameOriginal.path) == "music\\dlvld.wav", "game location original choice ignores the menu's alternative choice");
}

void CheckStartup()
{
	Options &options = GetOptions();
	leveltype = DTYPE_TOWN;
	options.Graphics.townViewStartIn3D.SetValue(true);
	InitializeTownViewForGame();
	Check(IsTownViewActive() && !IsTownViewNativePose(), "starting in 3D requests actual geometry in town");
	const TownViewCameraState initial = GetTownViewCameraState();
	OrbitTownView(0.6F, 0.2F);
	AdjustTownViewDistance(3);
	InitializeTownViewForGame();
	const TownViewCameraState restarted = GetTownViewCameraState();
	Check(std::abs(initial.yaw - restarted.yaw) < 0.00001F
	        && std::abs(initial.pitch - restarted.pitch) < 0.00001F
	        && std::abs(initial.distance - restarted.distance) < 0.00001F,
	    "each new game session resets the previous camera changes");
	ToggleTownView();
	Check(!IsTownViewActive(), "F4 can switch away from the startup preference");
	ResetTownViewResources();
	Check(!IsTownViewActive(), "resource reset preserves the player's F4 choice during the session");
	InitializeTownViewForGame();
	Check(IsTownViewActive(), "the next game session applies the saved preference again");
	ResetTownViewCamera();
	Check(IsTownViewNativePose(), "Home retains the exact native comparison pose");
	leveltype = DTYPE_CATHEDRAL;
	Check(!IsTownViewActive(), "unsupported dungeon levels keep their original renderer");
	leveltype = DTYPE_TOWN;
	Check(IsTownViewActive(), "returning to town retains the session's 3D choice");
	options.Graphics.townViewStartIn3D.SetValue(false);
	Check(IsTownViewActive(), "changing startup preference does not alter the current session");
	InitializeTownViewForGame();
	Check(!IsTownViewActive(), "disabled startup preference begins the next session in the original view");
	ToggleTownView();
	ResetTownViewResources();
	Check(IsTownViewActive(), "resource reset also preserves a manually enabled 3D view");
}

void CheckPersistence(const ConfigFixture &fixture)
{
	{
		std::ofstream out(fixture.iniPath, std::ios::binary);
		out << "[Music]\nTheme=2\nMenu=2\nTown=0\nCathedral=1\nCatacombs=0\nCaves=1\nHell=0\nNest=1\nCrypt=0\n"
		       "[Graphics]\nStart in 3D=0\n";
		if (!out)
			throw std::runtime_error("Cannot write the isolated configuration fixture");
	}
	LoadOptions();
	Options &options = GetOptions();
	Check(*options.Music.theme == MusicTheme::Custom && *options.Music.menu == MusicVariant::Alternative,
	    "INI loads Custom mode and the earlier menu alternative");
	constexpr std::array<int, NUM_MUSIC> Expected { 0, 1, 0, 1, 0, 1, 0, 2 };
	for (unsigned i = 0; i < NUM_MUSIC; ++i)
		Check(static_cast<int>(*options.Music.ForTrack(static_cast<_music_id>(i))) == Expected[i],
		    "INI independently loads the stable key " + std::string(MusicTrackCatalog[i].key));
	Check(!*options.Graphics.townViewStartIn3D, "INI honors an explicit original-view startup preference");
	options.Music.menu.SetValue(MusicVariant::Rock);
	options.Music.theme.SetValue(MusicTheme::Vanilla);
	options.Graphics.townViewStartIn3D.SetValue(true);
	SaveOptions();
	std::ifstream input(fixture.iniPath, std::ios::binary);
	const std::string saved { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	auto parsed = Ini::parse(saved);
	Check(parsed.has_value(), "saved temporary INI parses successfully");
	Check(parsed->getInt("Music", "Theme", -1) == 0 && parsed->getInt("Music", "Menu", -1) == 1,
	    "saved INI persists Vanilla mode separately from the Rock2 per-menu choice");
	Check(parsed->getBool("Graphics", "Start in 3D", false), "saved INI persists Start in 3D");
	for (unsigned i = 0; i < TMUSIC_INTRO; ++i)
		Check(parsed->getInt("Music", MusicTrackCatalog[i].key, -1) == Expected[i],
		    "saving menu choices preserves the environment key " + std::string(MusicTrackCatalog[i].key));
	options.Music.theme.SetValue(MusicTheme::Rock);
	options.Music.menu.SetValue(MusicVariant::Alternative);
	options.Graphics.townViewStartIn3D.SetValue(false);
	LoadOptions();
	Check(*options.Music.theme == MusicTheme::Vanilla && *options.Music.menu == MusicVariant::Rock
	        && *options.Graphics.townViewStartIn3D,
	    "reloading restores both soundtrack choices and startup preference");
}

size_t CurrentMenuCount()
{
	if (sgpCurrentMenu == nullptr)
		throw std::runtime_error("Expected an active in-game settings menu");
	for (size_t i = 0; i < 8; ++i) {
		if (sgpCurrentMenu[i].fnMenu == nullptr)
			return i;
		if (sgpCurrentMenu[i].pszStr == nullptr)
			throw std::runtime_error("Active menu item has no stable label");
	}
	throw std::runtime_error("In-game settings menu has no bounded terminator");
}

void ActivateMenuRow(size_t index)
{
	if (index >= CurrentMenuCount())
		throw std::runtime_error("Menu activation would cross the active table");
	sgpCurrentMenu[index].fnMenu(true);
}

void CheckInGameMenus()
{
	Options &options = GetOptions();
	gbIsMultiplayer = false;
	gbRunGame = true;
	gbSndInited = false;
	gbMusicOn = gbSoundOn = false;
	Players.resize(1);
	MyPlayer = &Players.front();
	MyPlayer->_pmode = PM_STAND;
	MyPlayerIsDead = false;
	sgGameInitInfo.nTickRate = 20;
	options.Audio.musicVolume.SetValue(-600);
	options.Audio.soundVolume.SetValue(-700);
	gmenu_init_menu();
	gamemenu_on();
	Check(CurrentMenuCount() == 5 && std::string_view(sgpCurrentMenu[0].pszStr) == "Options", "real single-player menu exposes Options without invoking save/load");
	ActivateMenuRow(0);
	Check(CurrentMenuCount() == 4
	        && std::string_view(sgpCurrentMenu[0].pszStr) == "Audio Options"
	        && std::string_view(sgpCurrentMenu[1].pszStr) == "Video Options"
	        && sgpCurrentMenu[2].isSlider(),
	    "Options has Audio, Video and the relocated Speed slider in four rows");
	gmenu_slider_set(&sgpCurrentMenu[2], 20, 50, 35);
	sgpCurrentMenu[2].fnMenu(false);
	Check(sgGameInitInfo.nTickRate == 35 && *options.Gameplay.tickRate == 35
	        && *options.Audio.musicVolume == -600 && *options.Audio.soundVolume == -700,
	    "the Speed slider changes tick rate without touching the audio rows");
	ActivateMenuRow(0);
	Check(CurrentMenuCount() == 4 && std::string_view(sgpCurrentMenu[2].pszStr) == "Soundtrack", "Audio exposes the Soundtrack submenu at row two");
	// Audio devices remain disabled. Exercise the real callbacks at the muted
	// endpoint, which must not initialize a decoder or load a music asset.
	gmenu_slider_steps(&sgpCurrentMenu[0], VOLUME_STEPS);
	gmenu_slider_set(&sgpCurrentMenu[0], VOLUME_MIN, VOLUME_MAX, VOLUME_MIN);
	sgpCurrentMenu[0].fnMenu(false);
	Check(*options.Audio.musicVolume == VOLUME_MIN && *options.Audio.soundVolume == -700 && !gbMusicOn,
	    "the relocated Music slider changes only music volume and preserves mute");
	gmenu_slider_steps(&sgpCurrentMenu[1], VOLUME_STEPS);
	gmenu_slider_set(&sgpCurrentMenu[1], VOLUME_MIN, VOLUME_MAX, VOLUME_MIN);
	sgpCurrentMenu[1].fnMenu(false);
	Check(*options.Audio.soundVolume == VOLUME_MIN && *options.Audio.musicVolume == VOLUME_MIN && !gbSoundOn,
	    "the relocated Sound slider changes only sound volume and preserves mute");
	ActivateMenuRow(2);
	Check(CurrentMenuCount() == 3 && std::string_view(sgpCurrentMenu[1].pszStr) == "Music by Location", "Soundtrack exposes its mode and music by location");
	options.Music.theme.SetValue(MusicTheme::Vanilla);
	ActivateMenuRow(0);
	Check(*options.Music.theme == MusicTheme::Rock, "mode activation changes Vanilla to Rock through the real menu handler");
	ActivateMenuRow(0);
	Check(*options.Music.theme == MusicTheme::Custom, "mode activation changes Rock to Custom");
	ActivateMenuRow(0);
	Check(*options.Music.theme == MusicTheme::Vanilla, "mode activation cycles Custom back to Vanilla");
	ActivateMenuRow(1);
	constexpr std::array<_music_id, NUM_MUSIC> Order {
		TMUSIC_INTRO, TMUSIC_TOWN, TMUSIC_CATHEDRAL, TMUSIC_CATACOMBS,
		TMUSIC_CAVES, TMUSIC_HELL, TMUSIC_NEST, TMUSIC_CRYPT,
	};
	for (size_t page = 0; page < 3; ++page) {
		const size_t first = page * 3;
		const size_t count = page == 2 ? 2 : 3;
		Check(CurrentMenuCount() == count + 2 && CurrentMenuCount() <= 5, "location page " + std::to_string(page + 1) + " fits above the 640x480 control panel");
		for (size_t row = 0; row < count; ++row) {
			const _music_id track = Order[first + row];
			const std::string key(MusicTrackCatalog[track].key);
			Check(std::string_view(sgpCurrentMenu[row].pszStr) == MusicTrackCatalog[track].name, "location page maps the real row to " + key);
			ActivateMenuRow(row);
			const size_t variants = track == TMUSIC_INTRO ? 3 : 2;
			Check(CurrentMenuCount() == variants + 1, "variant menu exposes exactly the supported choices for " + key);
			std::array<MusicVariant, NUM_MUSIC> before;
			for (unsigned i = 0; i < NUM_MUSIC; ++i)
				before[i] = *options.Music.ForTrack(static_cast<_music_id>(i));
			ActivateMenuRow(0);
			bool otherTracksUnchanged = true;
			for (unsigned i = 0; i < NUM_MUSIC; ++i) {
				if (i != track)
					otherTracksUnchanged &= *options.Music.ForTrack(static_cast<_music_id>(i)) == before[i];
			}
			Check(*options.Music.theme == MusicTheme::Custom && *options.Music.ForTrack(track) == MusicVariant::Original && otherTracksUnchanged,
			    "Original selects Custom mode and changes only " + key);
			ActivateMenuRow(1);
			Check(*options.Music.ForTrack(track) == MusicVariant::Rock && std::string_view(sgpCurrentMenu[1].pszStr).starts_with("* "),
			    "Rock selection marks the current choice for " + key);
			if (track == TMUSIC_INTRO) {
				ActivateMenuRow(2);
				Check(*options.Music.menu == MusicVariant::Alternative, "Main Menu alternative is reachable through the real variant handler");
			}
			ActivateMenuRow(variants);
			Check(CurrentMenuCount() == count + 2 && std::string_view(sgpCurrentMenu[0].pszStr) == MusicTrackCatalog[Order[first]].name,
			    "Previous preserves location page after editing " + key);
		}
		ActivateMenuRow(count);
	}
	Check(CurrentMenuCount() == 5 && std::string_view(sgpCurrentMenu[0].pszStr) == "Main Menu", "First Page returns from the final two-location page to the menu location");
	ActivateMenuRow(4);
	Check(CurrentMenuCount() == 3, "location list Previous returns to Soundtrack");
	ActivateMenuRow(2);
	Check(CurrentMenuCount() == 4 && std::string_view(sgpCurrentMenu[2].pszStr) == "Soundtrack", "Soundtrack Previous returns to Audio");
	ActivateMenuRow(3);
	ActivateMenuRow(1);
	Check(CurrentMenuCount() == 5 && sgpCurrentMenu[3].isSlider(), "Video keeps Gamma at row three with five total rows");
	leveltype = DTYPE_TOWN;
	options.Graphics.townViewStartIn3D.SetValue(true);
	InitializeTownViewForGame();
	const bool gpuBefore = *options.Graphics.townViewGpuRendering;
	const bool aaBefore = *options.Graphics.townViewAntialiasing;
	ActivateMenuRow(0);
	Check(*options.Graphics.townViewGpuRendering != gpuBefore && *options.Graphics.townViewAntialiasing == aaBefore
	        && *options.Graphics.townViewStartIn3D,
	    "Video GPU row modifies only the GPU preference");
	ActivateMenuRow(1);
	Check(*options.Graphics.townViewAntialiasing != aaBefore && *options.Graphics.townViewStartIn3D,
	    "Video smoothing row remains separate from startup preference");
	const TownViewCameraState beforeStartToggle = GetTownViewCameraState();
	ActivateMenuRow(2);
	Check(!*options.Graphics.townViewStartIn3D && IsTownViewActive()
	        && GetTownViewCameraState().yaw == beforeStartToggle.yaw,
	    "Video Start in 3D row changes the next-session preference without changing the current view");
	gmenu_slider_set(&sgpCurrentMenu[3], 0, 100, 100);
	sgpCurrentMenu[3].fnMenu(false);
	Check(*options.Graphics.brightness == 100 && !*options.Graphics.townViewStartIn3D
	        && *options.Audio.musicVolume == VOLUME_MIN && *options.Audio.soundVolume == VOLUME_MIN,
	    "Gamma uses its relocated slider without changing startup or audio preferences");
	ActivateMenuRow(4);
	Check(CurrentMenuCount() == 4 && std::string_view(sgpCurrentMenu[0].pszStr) == "Audio Options", "Video Previous returns to Options");
	gamemenu_off();
	Check(!gmenu_is_active() && !gbMusicOn && !gbSoundOn, "closing settings leaves music and sound muted");
	gbRunGame = false;
}

void CheckInvalidAudio(const ConfigFixture &fixture)
{
#if !defined(NOSOUND) && !defined(USE_SDL3)
	{
		std::ofstream out(fixture.corruptAudioPath, std::ios::binary);
		out << "Synthetic invalid MP3 decoder fixture, not an audio file.";
		if (!out)
			throw std::runtime_error("Cannot write the invalid-audio fixture");
	}
	Check(FindAsset("invalid.mp3").ok(), "invalid MP3 exists so decoder rejection is exercised after file opening");
	{
		SoundSample sample;
		Check(sample.SetChunkStream("invalid.mp3", true, false) != 0 && !sample.IsLoaded(),
		    "invalid streamed MP3 returns an error without dereferencing an absent decoder stream");
	}
	{
		auto bytes = MakeArraySharedPtr<uint8_t>(64);
		std::fill_n(bytes.get(), 64, 0x0B);
		SoundSample sample;
		Check(sample.SetChunk(bytes, 64, true) != 0 && !sample.IsLoaded(),
		    "invalid in-memory MP3 returns an error without dereferencing an absent decoder stream");
	}
#else
	std::cout << "SKIP invalid MP3 guard regression requires the SDL2 audio decoder\n";
#endif
}

} // namespace

int main()
{
	SDL_SetMainReady();
	if (SDL_Init(0) != 0) {
		std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}
	try {
		HeadlessMode = true;
		const ConfigFixture fixture;
		CheckDefaults();
		CheckSelections();
		CheckStartup();
		CheckPersistence(fixture);
		CheckInGameMenus();
		CheckInvalidAudio(fixture);
		std::cout << "PASS " << Checks << " checks; no game assets loaded or player saves accessed\n";
	} catch (const std::exception &error) {
		std::cerr << "FAIL after " << Checks << " checks: " << error.what() << '\n';
		SDL_Quit();
		return 1;
	}
	SDL_Quit();
	return 0;
}
