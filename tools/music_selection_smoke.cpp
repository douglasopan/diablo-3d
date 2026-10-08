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
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "engine/assets.hpp"
#include "control/control.hpp"
#include "engine/music_catalog.hpp"
#include "engine/random.hpp"
#include "engine/render/town_view.hpp"
#include "engine/sound.h"
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
// It deliberately has no MPQ, models, player music or save files.
struct ConfigFixture {
	std::filesystem::path directory;
	std::filesystem::path iniPath;
	std::filesystem::path corruptAudioPath;
	std::filesystem::path replacementDirectory;
	std::array<std::filesystem::path, 3> replacements;

	ConfigFixture()
	{
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		directory = std::filesystem::temp_directory_path() / ("d3d-music-selection-" + std::to_string(nonce));
		if (!std::filesystem::create_directory(directory))
			throw std::runtime_error("Cannot create a fresh isolated configuration directory");
		iniPath = directory / "diablo.ini";
		corruptAudioPath = directory / "invalid.mp3";
		replacementDirectory = directory / "music" / "d3d";
		std::filesystem::create_directories(replacementDirectory);
		replacements = { replacementDirectory / "town-rock.mp3", replacementDirectory / "town-alternative.mp3", replacementDirectory / "town-third.mp3" };
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
		for (const auto &path : replacements)
			std::filesystem::remove(path, ignored);
		std::filesystem::remove(replacementDirectory, ignored);
		std::filesystem::remove(directory / "music", ignored);
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
		Check(*options.Music.ForTrack(track) == (track == TMUSIC_TOWN ? MusicVariant::Random : MusicVariant::Rock),
		    "default soundtrack choice for " + std::string(MusicTrackCatalog[i].key));
		const size_t choices = track == TMUSIC_TOWN ? 5 : track == TMUSIC_INTRO ? 3 : 2;
		Check(options.Music.ForTrack(track).GetListSize() == choices,
		    "supported soundtrack choices for " + std::string(MusicTrackCatalog[i].key));
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

void CheckTownRandomSelection()
{
	constexpr std::array<const char *, 3> Paths {
		"music\\d3d\\town-rock.mp3", "music\\d3d\\town-alternative.mp3", "music\\d3d\\town-third.mp3"
	};
	constexpr std::array<MusicVariant, 3> Variants { MusicVariant::Rock, MusicVariant::Alternative, MusicVariant::Third };
	const auto installed = [](const char *) { return true; };
	const uint32_t simulationRng = GetLCGEngineState();
	Check(static_cast<unsigned>(MusicVariant::Random) == 3 && static_cast<unsigned>(MusicVariant::Third) == 4,
	    "adding Tristram3 preserves the persisted Random ID3 and uses the new ID4");
	constexpr std::array<unsigned, 7> Draws { 0, 1, 2, 3, 4, 5, std::numeric_limits<unsigned>::max() };
	// Exhaust every availability subset, including only the third replacement.
	// Expected order is specified here independently of the production catalog.
	for (unsigned mask = 0; mask < 8; ++mask) {
		std::array<unsigned, 3> available {};
		unsigned count = 0;
		for (unsigned candidate = 0; candidate < Paths.size(); ++candidate) {
			if ((mask & (1U << candidate)) != 0)
				available[count++] = candidate;
		}
		const auto exists = [&](const char *path) {
			for (unsigned candidate = 0; candidate < Paths.size(); ++candidate) {
				if (std::string_view(path) == Paths[candidate])
					return (mask & (1U << candidate)) != 0;
			}
			return false;
		};
		const std::string subset = " (availability " + std::to_string(mask) + ")";
		for (const unsigned draw : Draws) {
			const char *expectedPath = count == 0 ? "music\\dtowne.wav" : Paths[available[draw % count]];
			const MusicVariant expectedVariant = count == 0 ? MusicVariant::Original : Variants[available[draw % count]];
			for (const MusicTheme theme : { MusicTheme::Custom, MusicTheme::Rock }) {
				const auto selection = ResolveMusicSelection(TMUSIC_TOWN, theme, MusicVariant::Random, true, exists,
				    false, MusicVariant::Rock, draw);
				Check(std::string_view(selection.path) == expectedPath && selection.requested == MusicVariant::Random
				        && selection.actual == expectedVariant && selection.fallback == (count == 0),
				    "random Town selects the expected available candidate for draw " + std::to_string(draw)
				        + (theme == MusicTheme::Rock ? " in global Rock" : " in Custom") + subset);
			}
		}
		for (unsigned candidate = 0; candidate < Paths.size(); ++candidate) {
			const bool present = (mask & (1U << candidate)) != 0;
			for (const bool fullMusic : { true, false }) {
				const char *original = fullMusic ? "music\\dtowne.wav" : "music\\stowne.wav";
				const auto fixed = ResolveMusicSelection(TMUSIC_TOWN, MusicTheme::Custom, Variants[candidate], fullMusic, exists,
				    false, MusicVariant::Rock, std::numeric_limits<unsigned>::max());
				Check(std::string_view(fixed.path) == (present ? Paths[candidate] : original)
				        && fixed.actual == (present ? Variants[candidate] : MusicVariant::Original)
				        && fixed.requested == Variants[candidate] && fixed.fallback == !present,
				    "fixed Tristram" + std::to_string(candidate + 1) + " ignores random draws and other candidates"
				        + (fullMusic ? " in retail" : " in shareware") + subset);
			}
		}
		const auto shareware = ResolveMusicSelection(TMUSIC_TOWN, MusicTheme::Custom, MusicVariant::Random, false, exists,
		    false, MusicVariant::Rock, 2);
		Check(std::string_view(shareware.path) == (count == 0 ? "music\\stowne.wav" : Paths[available[2 % count]])
		        && shareware.fallback == (count == 0), "random shareware preserves native fallback" + subset);
		const auto vanilla = ResolveMusicSelection(TMUSIC_TOWN, MusicTheme::Vanilla, MusicVariant::Third, true, exists);
		Check(std::string_view(vanilla.path) == "music\\dtowne.wav" && vanilla.requested == MusicVariant::Original && !vanilla.fallback,
		    "Vanilla ignores the selected third version and all replacements" + subset);
	}
	for (unsigned i = 0; i < NUM_MUSIC; ++i) {
		if (i == TMUSIC_TOWN)
			continue;
		for (const MusicVariant unsupported : { MusicVariant::Random, MusicVariant::Third }) {
			const auto choice = ResolveMusicSelection(static_cast<_music_id>(i), MusicTheme::Custom, unsupported, true, installed);
			Check(std::string_view(choice.path) == MusicTrackCatalog[i].originalPath && choice.actual == MusicVariant::Original,
			    "unsupported Town variant " + std::to_string(static_cast<unsigned>(unsupported)) + " is safe for " + std::string(MusicTrackCatalog[i].key));
		}
	}
	for (const MusicVariant unsupported : { MusicVariant::Random, MusicVariant::Third }) {
		const auto invalidMenu = ResolveMusicSelection(TMUSIC_CATACOMBS, MusicTheme::Custom, MusicVariant::Rock, true, installed,
		    true, unsupported, 1);
		Check(std::string_view(invalidMenu.path) == "music\\dlvlb.wav" && invalidMenu.actual == MusicVariant::Original,
		    "invalid Town menu variant preserves the inherited menu track mapping");
	}
	const auto menuRock = ResolveMusicSelection(TMUSIC_TOWN, MusicTheme::Rock, MusicVariant::Random, true, installed,
	    true, MusicVariant::Random, 1);
	Check(std::string_view(menuRock.path) == "music\\d3d\\menu-rock2.mp3" && menuRock.requested == MusicVariant::Rock,
	    "Rock menu rotation cannot activate the Town random policy");
	Check(GetLCGEngineState() == simulationRng, "music selection and random draw indexing preserve the simulation RNG");
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

void CheckTownOptionAvailability(const ConfigFixture &fixture)
{
	const auto &town = GetOptions().Music.town;
	constexpr std::array<MusicVariant, 3> Variants { MusicVariant::Rock, MusicVariant::Alternative, MusicVariant::Third };
	constexpr std::array<size_t, 3> ListIndices { 1, 2, 4 };
	for (unsigned mask = 0; mask < 8; ++mask) {
		for (unsigned candidate = 0; candidate < fixture.replacements.size(); ++candidate) {
			const auto &path = fixture.replacements[candidate];
			std::filesystem::remove(path);
			if ((mask & (1U << candidate)) != 0) {
				// Only the existence probe is under test; these bytes must not be decoded.
				std::ofstream asset(path, std::ios::binary);
				asset << "synthetic optional soundtrack availability fixture";
				if (!asset)
					throw std::runtime_error("Cannot write an isolated soundtrack availability fixture");
			}
		}
		for (unsigned candidate = 0; candidate < Variants.size(); ++candidate) {
			const bool present = (mask & (1U << candidate)) != 0;
			const std::string description(town.GetListDescription(ListIndices[candidate]));
			const std::string expected = "Tristram " + std::to_string(candidate + 1) + (present ? "" : " (pending)");
			Check(town.HasReplacement(Variants[candidate]) == present && description == expected,
			    "Town option describes the actual availability of Tristram" + std::to_string(candidate + 1)
			        + " for subset " + std::to_string(mask));
		}
		Check(town.HasReplacement(MusicVariant::Random) == (mask != 0)
		        && town.GetListDescription(3) == (mask != 0 ? "Random" : "Random (pending)"),
		    "Random option is available exactly when any of the three Town replacements exists: " + std::to_string(mask));
	}
	for (const auto &path : fixture.replacements)
		std::filesystem::remove(path);
}

void CheckPersistence(const ConfigFixture &fixture)
{
	{
		std::ofstream out(fixture.iniPath, std::ios::binary);
		out << "[Music]\nTheme=2\nMenu=2\nTown=3\nCathedral=1\nCatacombs=0\nCaves=1\nHell=0\nNest=1\nCrypt=0\n"
		       "[Graphics]\nStart in 3D=0\n";
		if (!out)
			throw std::runtime_error("Cannot write the isolated configuration fixture");
	}
	LoadOptions();
	Options &options = GetOptions();
	Check(*options.Music.theme == MusicTheme::Custom && *options.Music.menu == MusicVariant::Alternative,
	    "INI loads Custom mode and the earlier menu alternative");
	constexpr std::array<int, NUM_MUSIC> Expected { 3, 1, 0, 1, 0, 1, 0, 2 };
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
	        && *options.Music.town == MusicVariant::Random && *options.Graphics.townViewStartIn3D,
	    "reloading restores soundtrack choices, random Town selection and startup preference");
	options.Music.town.SetValue(MusicVariant::Third);
	SaveOptions();
	{
		std::ifstream thirdInput(fixture.iniPath, std::ios::binary);
		const std::string thirdSaved { std::istreambuf_iterator<char>(thirdInput), std::istreambuf_iterator<char>() };
		const auto thirdIni = Ini::parse(thirdSaved);
		Check(thirdIni.has_value() && thirdIni->getInt("Music", "Town", -1) == 4
		        && thirdIni->getInt("Music", "Theme", -1) == 2,
		    "Tristram3 persists as ID4 and an individual choice activates Custom");
	}
	options.Music.town.SetValue(MusicVariant::Original);
	LoadOptions();
	Check(*options.Music.town == MusicVariant::Third && options.Music.town.GetActiveListIndex() == 4,
	    "reloading ID4 restores the independent third Town version");
	options.Music.town.SetValue(MusicVariant::Random);
	SaveOptions();
	options.Music.town.SetValue(MusicVariant::Third);
	LoadOptions();
	Check(*options.Music.town == MusicVariant::Random && options.Music.town.GetActiveListIndex() == 3,
	    "older persisted ID3 continues to restore Random after adding Tristram3");
}

size_t CurrentMenuCount()
{
	if (sgpCurrentMenu == nullptr)
		throw std::runtime_error("Expected an active in-game settings menu");
	for (size_t i = 0; i <= GMenuSettingsMaxContentRows + 3; ++i) {
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

std::optional<size_t> FindMenuRowIfPresent(std::string_view requestedName)
{
	// Some action names reuse mutable model storage when pages rebuild.
	const std::string name(requestedName);
	std::vector<std::vector<std::string>> seenPages;
	for (size_t page = 0; page < 40; ++page) {
		const size_t count = CurrentMenuCount();
		std::vector<std::string> labels;
		for (size_t row = 0; row < count; ++row) {
			if (name == sgpCurrentMenu[row].pszStr)
				return row;
			labels.emplace_back(sgpCurrentMenu[row].pszStr);
		}
		if (std::find(seenPages.begin(), seenPages.end(), labels) != seenPages.end())
			return std::nullopt;
		seenPages.push_back(labels);
		bool next = false;
		for (size_t row = 0; row < count; ++row) {
			if (std::string_view(sgpCurrentMenu[row].pszStr) == "Next Page") {
				ActivateMenuRow(row);
				next = true;
				break;
			}
		}
		if (!next)
			return std::nullopt;
	}
	throw std::runtime_error("Menu search did not finish its finite page cycle: " + name);
}

size_t FindMenuRow(std::string_view name)
{
	const std::string ownedName(name);
	if (const auto row = FindMenuRowIfPresent(ownedName))
		return *row;
	throw std::runtime_error("Menu option is not reachable: " + ownedName);
}

struct MutedAudioFixture {
	int previousMusicVolume = *GetOptions().Audio.musicVolume;
	int previousSoundVolume = *GetOptions().Audio.soundVolume;
	bool previousMusicOn = gbMusicOn;
	bool previousSoundOn = gbSoundOn;
	bool initialized = false;

	MutedAudioFixture()
	{
#ifndef NOSOUND
		Check(!gbSndInited, "isolated menu fixture starts without an existing audio pipeline");
		GetOptions().Audio.musicVolume.SetValue(VOLUME_MIN);
		GetOptions().Audio.soundVolume.SetValue(VOLUME_MIN);
		snd_init();
		initialized = gbSndInited;
		Check(initialized && !gbMusicOn && !gbSoundOn, "real dummy audio makes soundtrack controls available while playback remains muted");
#endif
	}

	~MutedAudioFixture()
	{
		if (gmenu_is_active())
			gamemenu_off();
		if (initialized)
			snd_deinit();
		GetOptions().Audio.musicVolume.SetValue(previousMusicVolume);
		GetOptions().Audio.soundVolume.SetValue(previousSoundVolume);
		gbMusicOn = previousMusicOn;
		gbSoundOn = previousSoundOn;
	}
};

void CheckInGameMusicChoices()
{
	Options &options = GetOptions();
	Check(std::string_view(sgpCurrentMenu[0].pszStr) == "Soundtrack", "Soundtrack is the first category directly inside Settings");
	ActivateMenuRow(0);
	ActivateMenuRow(FindMenuRow(options.Music.theme.GetName()));
	Check(CurrentMenuCount() == 4, "all three soundtrack modes are explicit choices");
	for (size_t index = 0; index < options.Music.theme.GetListSize(); ++index) {
		ActivateMenuRow(index);
		Check(options.Music.theme.GetActiveListIndex() == index, "mode applies through the shared option setter");
		ActivateMenuRow(FindMenuRow(options.Music.theme.GetName()));
	}
	gmenu_presskeys(SDLK_ESCAPE);
	constexpr std::array<_music_id, NUM_MUSIC> Order {
		TMUSIC_INTRO, TMUSIC_TOWN, TMUSIC_CATHEDRAL, TMUSIC_CATACOMBS,
		TMUSIC_CAVES, TMUSIC_HELL, TMUSIC_NEST, TMUSIC_CRYPT,
	};
	for (const auto track : Order) {
		auto &option = options.Music.ForTrack(track);
		for (size_t index = 0; index < option.GetListSize(); ++index) {
			ActivateMenuRow(FindMenuRow(option.GetName()));
			Check(CurrentMenuCount() == option.GetListSize() + 1 && CurrentMenuCount() <= 6, "all variants fit in one compact page, including five Town choices");
			std::array<MusicVariant, NUM_MUSIC> before;
			for (unsigned i = 0; i < NUM_MUSIC; ++i)
				before[i] = *options.Music.ForTrack(static_cast<_music_id>(i));
			ActivateMenuRow(index);
			Check(option.GetActiveListIndex() == index && *options.Music.theme == MusicTheme::Custom, "location choice applies the existing Custom callback");
			for (unsigned i = 0; i < NUM_MUSIC; ++i) {
				if (i != track)
					Check(*options.Music.ForTrack(static_cast<_music_id>(i)) == before[i], "editing a location preserves every other location");
			}
		}
	}
	gmenu_presskeys(SDLK_ESCAPE);
}

void CheckInGameMenus()
{
	Options &options = GetOptions();
	const uint32_t simulationRng = GetLCGEngineState();
	const auto mainGraphicsEntries = options.Graphics.GetEntries();
	const auto mainCategories = options.GetCategories();
	const MutedAudioFixture audio;
	gbIsMultiplayer = false;
	gbRunGame = true;
	Players.resize(1);
	MyPlayer = &Players.front();
	MyPlayer->_pmode = PM_STAND;
	MyPlayerIsDead = false;
	sgGameInitInfo.nTickRate = 20;
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	CalculatePanelAreas();
	gmenu_init_menu();
	gamemenu_on();
	Check(CurrentMenuCount() == 5 && std::string_view(sgpCurrentMenu[0].pszStr) == "Settings", "single-player menu exposes the shared Settings browser");
	ActivateMenuRow(0);
	Check(!FindMenuRowIfPresent(options.StartUp.GetName()).has_value(), "startup-only category is absent during play");
	if (audio.initialized) {
		CheckInGameMusicChoices();
	} else {
		Check(!FindMenuRowIfPresent(options.Music.GetName()).has_value() && !FindMenuRowIfPresent(options.Audio.GetName()).has_value(),
		    "without initialized audio the runtime omits Soundtrack and Audio categories");
		std::cout << "SKIP in-game soundtrack choices in a NOSOUND build\n";
	}
	ActivateMenuRow(FindMenuRow(options.Graphics.GetName()));
	leveltype = DTYPE_TOWN;
	options.Graphics.townViewStartIn3D.SetValue(true);
	InitializeTownViewForGame();
	const bool gpuBefore = *options.Graphics.townViewGpuRendering;
	ActivateMenuRow(FindMenuRow(options.Graphics.townViewGpuRendering.GetName()));
	Check(*options.Graphics.townViewGpuRendering != gpuBefore, "Graphics exposes the shared GPU option");
	const bool aaBefore = *options.Graphics.townViewAntialiasing;
	ActivateMenuRow(FindMenuRow(options.Graphics.townViewAntialiasing.GetName()));
	Check(*options.Graphics.townViewAntialiasing != aaBefore, "Graphics exposes shared edge smoothing");
	const auto before = GetTownViewCameraState();
	Check(!FindMenuRowIfPresent(options.Graphics.townViewStartIn3D.GetName()).has_value()
	        && *options.Graphics.townViewStartIn3D && IsTownViewActive() && GetTownViewCameraState().yaw == before.yaw,
	    "runtime omits Start in 3D and preserves the saved preference and active camera");
	Check(options.Graphics.GetEntries() == mainGraphicsEntries
	        && std::find(mainGraphicsEntries.begin(), mainGraphicsEntries.end(), &options.Graphics.townViewStartIn3D) != mainGraphicsEntries.end()
	        && options.GetCategories() == mainCategories
	        && std::find(mainCategories.begin(), mainCategories.end(), &options.StartUp) != mainCategories.end(),
	    "runtime filtering keeps Start in 3D and startup categories in the main Settings model");
	gmenu_presskeys(SDLK_ESCAPE);
	ActivateMenuRow(FindMenuRow(options.Gameplay.GetName()));
	ActivateMenuRow(FindMenuRow("Speed"));
	gmenu_slider_set(&sgpCurrentMenu[0], 20, 50, 35);
	sgpCurrentMenu[0].fnMenu(false);
	Check(sgGameInitInfo.nTickRate == 35 && *options.Gameplay.tickRate == 35, "the Speed slider preserves its native single-player behavior");
	gamemenu_off();
	Check(!gmenu_is_active() && !gbMusicOn && !gbSoundOn, "closing settings preserves muted playback");
	Check(GetLCGEngineState() == simulationRng, "runtime soundtrack menus and dummy audio preserve the simulation RNG");
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
#ifdef USE_SDL3
	SDL_SetHint("SDL_VIDEODRIVER", "dummy");
	SDL_SetHint("SDL_AUDIODRIVER", "dummy");
	const bool initialized = SDL_Init(0);
#else
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	const bool initialized = SDL_Init(0) == 0;
#endif
	if (!initialized) {
		std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}
	try {
		HeadlessMode = true;
		const ConfigFixture fixture;
		CheckDefaults();
		CheckSelections();
		CheckTownRandomSelection();
		CheckTownOptionAvailability(fixture);
		CheckStartup();
		CheckPersistence(fixture);
		CheckInGameMenus();
		CheckInvalidAudio(fixture);
		std::cout << "PASS " << Checks << " checks; no game assets loaded or player saves accessed\n";
	} catch (const std::exception &error) {
		if (gmenu_is_active())
			gamemenu_off();
		if (gbSndInited)
			snd_deinit();
		std::cerr << "FAIL after " << Checks << " checks: " << error.what() << '\n';
		SDL_Quit();
		return 1;
	}
	SDL_Quit();
	return 0;
}
