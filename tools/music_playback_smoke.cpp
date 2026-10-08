// Real SDL2 playback checks using short, generated audio in a fresh fixture.
// Usage: music_playback_smoke <synthetic-mp3-under-two-seconds>
// No game archive, model, player configuration or save is read or modified.
#if defined(NOSOUND) || defined(USE_SDL3)
#include <iostream>
int main()
{
	std::cout << "SKIP: this playback fixture requires SDL2 audio\n";
	return 77;
}
#else
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "engine/menu_music.hpp"
#include "engine/music_catalog.hpp"
#include "engine/random.hpp"
#include "headless_mode.hpp"
#include "options.h"
#include "utils/paths.h"

namespace {
using namespace devilution;

size_t Checks = 0;
std::mutex PlaybackLogMutex;
std::vector<std::string> PlaybackLogs;

void Check(bool condition, std::string_view message)
{
	++Checks;
	std::cout << (condition ? "PASS " : "FAIL ") << message << '\n';
	if (!condition)
		throw std::runtime_error(std::string(message));
}

void CaptureLog(void *, int, SDL_LogPriority, const char *message)
{
	if (std::string_view(message).find("Diablo 3D music playing:") == std::string_view::npos)
		return;
	const std::lock_guard<std::mutex> lock(PlaybackLogMutex);
	PlaybackLogs.emplace_back(message);
}

size_t PlaybackCount()
{
	const std::lock_guard<std::mutex> lock(PlaybackLogMutex);
	return PlaybackLogs.size();
}

bool LastPlaybackHas(std::string_view text)
{
	const std::lock_guard<std::mutex> lock(PlaybackLogMutex);
	return !PlaybackLogs.empty() && PlaybackLogs.back().find(text) != std::string::npos;
}

bool WaitForPlaybacks(size_t count, uint32_t timeoutMs)
{
	const uint32_t start = SDL_GetTicks();
	do {
		music_update();
		if (PlaybackCount() >= count)
			return true;
		SDL_Delay(5);
	} while (SDL_GetTicks() - start < timeoutMs);
	return false;
}

void UpdateFor(uint32_t milliseconds)
{
	const uint32_t start = SDL_GetTicks();
	do {
		music_update();
		SDL_Delay(5);
	} while (SDL_GetTicks() - start < milliseconds);
}

// Generate a short original tone as PCM; replacement fixtures come from an
// independently generated MP3 supplied to this executable.
void WriteTone(const std::filesystem::path &path)
{
	std::ofstream out(path, std::ios::binary);
	const auto littleEndian = [&out](uint32_t value, unsigned bytes) {
		for (unsigned i = 0; i < bytes; ++i)
			out.put(static_cast<char>((value >> (8 * i)) & 0xFF));
	};
	constexpr uint32_t Rate = 22050;
	constexpr uint32_t Samples = Rate / 10;
	out.write("RIFF", 4);
	littleEndian(36 + Samples * 2, 4);
	out.write("WAVEfmt ", 8);
	littleEndian(16, 4);
	littleEndian(1, 2);
	littleEndian(1, 2);
	littleEndian(Rate, 4);
	littleEndian(Rate * 2, 4);
	littleEndian(2, 2);
	littleEndian(16, 2);
	out.write("data", 4);
	littleEndian(Samples * 2, 4);
	for (uint32_t i = 0; i < Samples; ++i) {
		const auto sample = static_cast<int16_t>(1000 * std::sin(6.283185307179586 * 440 * i / Rate));
		littleEndian(static_cast<uint16_t>(sample), 2);
	}
	if (!out)
		throw std::runtime_error("Cannot write the generated WAV fixture");
}

struct PlaybackFixture {
	std::filesystem::path directory;
	std::filesystem::path musicDirectory;
	std::filesystem::path replacementDirectory;
	std::filesystem::path source;
	std::filesystem::path first;
	std::filesystem::path second;
	std::filesystem::path third;
	std::filesystem::path menu;
	std::filesystem::path menuAlternative;
	std::filesystem::path townOriginal;
	std::filesystem::path menuOriginal;
	std::filesystem::path catacombsOriginal;
	std::filesystem::path cavesOriginal;

	explicit PlaybackFixture(const std::filesystem::path &syntheticMp3)
	{
		source = syntheticMp3;
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		directory = std::filesystem::temp_directory_path() / ("d3d-music-playback-" + std::to_string(nonce));
		if (!std::filesystem::create_directory(directory))
			throw std::runtime_error("Cannot create a fresh playback fixture");
		musicDirectory = directory / "music";
		replacementDirectory = musicDirectory / "d3d";
		std::filesystem::create_directories(replacementDirectory);
		first = replacementDirectory / "town-rock.mp3";
		second = replacementDirectory / "town-alternative.mp3";
		third = replacementDirectory / "town-third.mp3";
		menu = replacementDirectory / "menu-rock2.mp3";
		menuAlternative = replacementDirectory / "menu-alternative.mp3";
		townOriginal = musicDirectory / "dtowne.wav";
		menuOriginal = musicDirectory / "dintro.wav";
		catacombsOriginal = musicDirectory / "dlvlb.wav";
		cavesOriginal = musicDirectory / "dlvlc.wav";
		std::filesystem::copy_file(syntheticMp3, first);
		std::filesystem::copy_file(syntheticMp3, second);
		std::filesystem::copy_file(syntheticMp3, third);
		std::filesystem::copy_file(syntheticMp3, menu);
		std::filesystem::copy_file(syntheticMp3, menuAlternative);
		WriteTone(townOriginal);
		WriteTone(menuOriginal);
		WriteTone(catacombsOriginal);
		WriteTone(cavesOriginal);
		paths::SetBasePath(directory.string());
		paths::SetAssetsPath(directory.string());
		paths::SetPrefPath(directory.string());
		paths::SetConfigPath(directory.string());
	}

	~PlaybackFixture()
	{
		// Remove only exact files and empty directories that this fixture owns.
		std::error_code ignored;
		for (const auto &path : { first, second, third, menu, menuAlternative, townOriginal, menuOriginal,
		         catacombsOriginal, cavesOriginal, directory / "diablo.ini" })
			std::filesystem::remove(path, ignored);
		std::filesystem::remove(replacementDirectory, ignored);
		std::filesystem::remove(musicDirectory, ignored);
		std::filesystem::remove(directory, ignored);
	}
};

void CheckMenuContinuity(const PlaybackFixture &fixture)
{
	Options &options = GetOptions();
	const uint32_t simulationRng = GetLCGEngineState();
	options.Music.theme.SetValue(MusicTheme::Rock);
	music_stop();
	const size_t initial = PlaybackCount();
	RefreshMenuMusic(TMUSIC_INTRO);
	Check(PlaybackCount() == initial + 1 && sgnMusicTrack == TMUSIC_INTRO
	        && LastPlaybackHas("menu-rock2.mp3") && LastPlaybackHas("context=menu"),
	    "initial menu entry starts the selected real stream");
	SDL_Delay(75);
	// InitMenu reaches this same continuation path after cancelled provider,
	// game-selection or hero dialogs. The inherited NextTrack would advance here.
	RefreshMenuMusic(TMUSIC_CATACOMBS);
	Check(PlaybackCount() == initial + 1 && sgnMusicTrack == TMUSIC_INTRO,
	    "provider cancellation preserves Rock2 without replay or native-track advance");
	SDL_Delay(75);
	RefreshMenuMusic(TMUSIC_CATACOMBS);
	Check(PlaybackCount() == initial + 1 && sgnMusicTrack == TMUSIC_INTRO,
	    "hero-selection cancellation and return preserve the active menu stream");
	for (unsigned i = 0; i < 10; ++i)
		RefreshMenuMusic(TMUSIC_CATACOMBS);
	Check(PlaybackCount() == initial + 1, "repeated multiplayer round trips never restart unchanged menu music");

	options.Music.menu.SetValue(MusicVariant::Alternative);
	Check(PlaybackCount() == initial + 2 && LastPlaybackHas("menu-alternative.mp3"),
	    "explicit menu soundtrack change still replaces the active stream immediately");
	RefreshMenuMusic(TMUSIC_CATACOMBS);
	Check(PlaybackCount() == initial + 2 && sgnMusicTrack == TMUSIC_INTRO,
	    "multiplayer cancellation also preserves the explicitly selected alternative");
	options.Music.theme.SetValue(MusicTheme::Vanilla);
	Check(PlaybackCount() == initial + 3 && LastPlaybackHas("dintro.wav"),
	    "explicit Vanilla selection still applies immediately");
	RefreshMenuMusic(TMUSIC_CATACOMBS);
	Check(PlaybackCount() == initial + 3 && sgnMusicTrack == TMUSIC_INTRO,
	    "Vanilla menu music is not advanced merely by cancelling multiplayer");
	music_start(TMUSIC_CATACOMBS);
	const size_t nativeRotation = PlaybackCount();
	RefreshMenuMusic(TMUSIC_CAVES);
	Check(PlaybackCount() == nativeRotation && sgnMusicTrack == TMUSIC_CATACOMBS
	        && LastPlaybackHas("dlvlb.wav"),
	    "a playing native rotation track also retains its stream and identity");

	music_stop();
	std::filesystem::remove(fixture.menu);
	options.Music.theme.SetValue(MusicTheme::Rock);
	RefreshMenuMusic(TMUSIC_INTRO);
	Check(LastPlaybackHas("dintro.wav") && LastPlaybackHas("fallback=true"),
	    "missing menu replacement still uses the generated original");
	const size_t missing = PlaybackCount();
	RefreshMenuMusic(TMUSIC_CATACOMBS);
	Check(PlaybackCount() == missing && sgnMusicTrack == TMUSIC_INTRO,
	    "multiplayer cancellation preserves the missing-file fallback");
	music_stop();
	{
		std::ofstream invalid(fixture.menu, std::ios::binary);
		invalid << "invalid menu MPEG audio fixture";
	}
	RefreshMenuMusic(TMUSIC_INTRO);
	Check(LastPlaybackHas("dintro.wav") && LastPlaybackHas("fallback=true"),
	    "corrupt menu replacement still falls back through the real decoder");
	const size_t rejected = PlaybackCount();
	RefreshMenuMusic(TMUSIC_CATACOMBS);
	Check(PlaybackCount() == rejected && sgnMusicTrack == TMUSIC_INTRO,
	    "returning from multiplayer does not retry a rejected menu replacement");
	music_stop();
	std::filesystem::copy_file(fixture.source, fixture.menu, std::filesystem::copy_options::overwrite_existing);

	music_set_game_context(true);
	music_start(TMUSIC_TOWN);
	Check(LastPlaybackHas("context=game"), "real game entry still loads its own soundtrack");
	// FreeGame stops the soundtrack before returning from a real session.
	music_stop();
	const size_t afterGame = PlaybackCount();
	RefreshMenuMusic(TMUSIC_INTRO);
	Check(PlaybackCount() == afterGame + 1 && LastPlaybackHas("context=menu")
	        && LastPlaybackHas("menu-rock2.mp3") && sgnMusicTrack == TMUSIC_INTRO,
	    "return from a stopped real game starts the menu soundtrack normally");
	music_stop();
	gbMusicOn = false;
	const size_t disabled = PlaybackCount();
	RefreshMenuMusic(TMUSIC_INTRO);
	Check(PlaybackCount() == disabled && sgnMusicTrack == NUM_MUSIC,
	    "menu navigation never starts audio while music is disabled");
	gbMusicOn = true;
	Check(GetLCGEngineState() == simulationRng, "menu continuation preserves the simulation RNG");
}

void CheckPlayback(const PlaybackFixture &fixture)
{
	Options &options = GetOptions();
	const uint32_t simulationRng = GetLCGEngineState();
	const int originalVolume = *options.Audio.musicVolume;
	auto probe = SoundFileLoadWithStatus("music\\d3d\\town-rock.mp3", true);
	Check(probe.has_value(), "synthetic replacement opens in the real decoder");
	const int duration = probe.value()->DSB.GetLength();
	Check(duration > 0 && duration <= 2000, "replacement fixture is under two seconds");
	probe.value().reset();
	const uint32_t observation = static_cast<uint32_t>(duration * 3 + 150);

	music_set_game_context(true);
	options.Music.town.SetValue(MusicVariant::Random);
	const size_t before = PlaybackCount();
	music_start(TMUSIC_TOWN);
	Check(PlaybackCount() == before + 1 && LastPlaybackHas("random=true"), "random Town starts a finite real playthrough");
	for (unsigned i = 0; i < 10; ++i)
		music_refresh();
	options.Music.cathedral.SetValue(MusicVariant::Original);
	Check(PlaybackCount() == before + 1, "refresh and unrelated music choices do not reroll or restart Town");
	Check(WaitForPlaybacks(before + 3, 10000), "two completed playthroughs draw from the three installed Town versions through music_update");
	Check(LastPlaybackHas("town-rock.mp3") || LastPlaybackHas("town-alternative.mp3") || LastPlaybackHas("town-third.mp3"),
	    "real EOF playback selects one of the three installed replacements");
	Check(sgnMusicTrack == TMUSIC_TOWN && LastPlaybackHas("context=game"), "EOF keeps the game location authoritative");

	music_mute();
	const size_t muted = PlaybackCount();
	Check(WaitForPlaybacks(muted + 1, 10000), "muted finite playback still completes and advances");
	music_unmute();
	Check(*options.Audio.musicVolume == originalVolume, "mute and random restarts preserve the stored volume");

	music_stop();
	const size_t stopped = PlaybackCount();
	UpdateFor(observation);
	Check(PlaybackCount() == stopped && sgnMusicTrack == NUM_MUSIC, "stopped playback never resurrects at EOF");

	music_set_game_context(false);
	options.Music.theme.SetValue(MusicTheme::Rock);
	music_start(TMUSIC_TOWN);
	Check(LastPlaybackHas("menu-rock2.mp3") && LastPlaybackHas("random=false"), "menu rotation using the Town ID still loops menu Rock2");
	const size_t inMenu = PlaybackCount();
	UpdateFor(observation);
	Check(PlaybackCount() == inMenu, "game polling cannot reroll the menu soundtrack");
	music_set_game_context(true);
	music_refresh();
	Check(PlaybackCount() == inMenu + 1 && LastPlaybackHas("random=true") && LastPlaybackHas("context=game"),
	    "switching to the game refreshes the same native track ID to Town");

	options.Music.theme.SetValue(MusicTheme::Vanilla);
	Check(LastPlaybackHas("dtowne.wav") && LastPlaybackHas("random=false"), "Vanilla selects and loops the generated original Town tone");
	const size_t vanilla = PlaybackCount();
	UpdateFor(observation);
	Check(PlaybackCount() == vanilla, "Vanilla never rerolls after its original audio duration");

	music_stop();
	options.Music.town.SetValue(MusicVariant::Third);
	music_start(TMUSIC_TOWN);
	Check(LastPlaybackHas("town-third.mp3") && LastPlaybackHas("random=false"), "fixed Tristram3 plays its own file with every replacement installed");
	const size_t fixedThird = PlaybackCount();
	UpdateFor(observation);
	Check(PlaybackCount() == fixedThird, "fixed Tristram3 loops indefinitely without a new random draw");
	options.Music.theme.SetValue(MusicTheme::Vanilla);
	Check(LastPlaybackHas("dtowne.wav") && LastPlaybackHas("random=false"), "Vanilla ignores a saved Tristram3 choice and all three installed versions");
	music_stop();
	std::filesystem::remove(fixture.first);
	std::filesystem::remove(fixture.second);
	options.Music.town.SetValue(MusicVariant::Random);
	music_start(TMUSIC_TOWN);
	Check(LastPlaybackHas("town-third.mp3") && LastPlaybackHas("random=true"), "Random supports an installation containing only Tristram3");
	const size_t onlyThird = PlaybackCount();
	Check(WaitForPlaybacks(onlyThird + 2, 10000) && LastPlaybackHas("town-third.mp3") && LastPlaybackHas("random=true"),
	    "two real EOF draws replay the sole third candidate without requiring the first two versions");
	const size_t randomThird = PlaybackCount();
	options.Music.town.SetValue(MusicVariant::Third);
	Check(PlaybackCount() == randomThird + 1 && LastPlaybackHas("town-third.mp3") && LastPlaybackHas("random=false"),
	    "Random to fixed Tristram3 changes looping policy even when the selected path stays identical");
	music_stop();
	std::filesystem::remove(fixture.third);
	std::filesystem::copy_file(fixture.source, fixture.first);
	music_start(TMUSIC_TOWN);
	Check(LastPlaybackHas("dtowne.wav") && LastPlaybackHas("fallback=true") && LastPlaybackHas("random=false"),
	    "missing fixed Tristram3 uses the original even when Tristram1 exists");
	const size_t missingThird = PlaybackCount();
	UpdateFor(observation);
	Check(PlaybackCount() == missingThird, "missing fixed third version does not retry or select another replacement at EOF");
	music_stop();
	{
		std::ofstream invalid(fixture.third, std::ios::binary);
		invalid << "invalid third-version MPEG audio fixture";
	}
	music_start(TMUSIC_TOWN);
	Check(LastPlaybackHas("dtowne.wav") && LastPlaybackHas("fallback=true") && LastPlaybackHas("random=false"),
	    "corrupt fixed Tristram3 safely falls back through the real decoder");
	const size_t rejectedThird = PlaybackCount();
	for (unsigned i = 0; i < 10; ++i)
		music_refresh();
	UpdateFor(observation);
	Check(PlaybackCount() == rejectedThird, "refresh and EOF do not repeatedly retry corrupt Tristram3");
	music_stop();
	std::filesystem::remove(fixture.third);
	options.Music.town.SetValue(MusicVariant::Rock);
	music_start(TMUSIC_TOWN);
	Check(LastPlaybackHas("town-rock.mp3") && LastPlaybackHas("random=false"), "fixed Tristram 1 loops its chosen version");
	const size_t fixed = PlaybackCount();
	options.Music.town.SetValue(MusicVariant::Random);
	Check(PlaybackCount() == fixed + 1 && LastPlaybackHas("random=true"), "fixed to Random restarts even when the selected path is identical");
	Check(WaitForPlaybacks(fixed + 2, 10000), "Random with only one available replacement still advances at EOF");
	const size_t random = PlaybackCount();
	options.Music.town.SetValue(MusicVariant::Rock);
	Check(PlaybackCount() == random + 1 && LastPlaybackHas("random=false"), "Random to fixed restores infinite looping on the same path");
	const size_t fixedAgain = PlaybackCount();
	UpdateFor(observation);
	Check(PlaybackCount() == fixedAgain, "fixed replacement stays playing without new starts");

	gbMusicOn = false;
	music_stop();
	options.Music.town.SetValue(MusicVariant::Random);
	UpdateFor(observation);
	Check(sgnMusicTrack == NUM_MUSIC && PlaybackCount() == fixedAgain, "disabled music remains stopped after a policy change");
	gbMusicOn = true;

	std::filesystem::remove(fixture.first);
	music_start(TMUSIC_TOWN);
	Check(LastPlaybackHas("dtowne.wav") && LastPlaybackHas("fallback=true") && LastPlaybackHas("random=false"),
	    "missing random candidates loop the original");
	const size_t missing = PlaybackCount();
	UpdateFor(observation);
	Check(PlaybackCount() == missing, "missing candidates do not cause an EOF retry loop");

	music_stop();
	{
		std::ofstream invalid(fixture.first, std::ios::binary);
		invalid << "not an MPEG audio stream";
	}
	music_start(TMUSIC_TOWN);
	Check(LastPlaybackHas("dtowne.wav") && LastPlaybackHas("fallback=true") && LastPlaybackHas("random=false"),
	    "rejected random replacement safely loops the original");
	const size_t rejected = PlaybackCount();
	for (unsigned i = 0; i < 10; ++i)
		music_refresh();
	UpdateFor(observation);
	Check(PlaybackCount() == rejected, "refresh and EOF do not repeatedly retry a rejected random candidate");
	sound_disable_music(true);
	sound_disable_music(false);
	UpdateFor(observation);
	Check(PlaybackCount() == rejected && sgnMusicTrack == NUM_MUSIC, "video-style stop cannot resurrect the random playlist");
	Check(GetLCGEngineState() == simulationRng, "real audio random draws preserve the simulation RNG");
}

} // namespace

int main(int argc, char **argv)
{
	if (argc != 2 || !std::filesystem::is_regular_file(argv[1])) {
		std::cerr << "Usage: music_playback_smoke <generated-short.mp3>\n";
		return 1;
	}
	SDL_SetMainReady();
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	if (SDL_Init(0) != 0) {
		std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}
	SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
	SDL_LogSetOutputFunction(CaptureLog, nullptr);
	try {
		HeadlessMode = true;
		const PlaybackFixture fixture(argv[1]);
		LoadOptions();
		snd_init();
		Check(gbSndInited, "real SDL audio initializes with the isolated dummy device");
		CheckMenuContinuity(fixture);
		CheckPlayback(fixture);
		music_stop();
		snd_deinit();
		std::cout << "PASS " << Checks << " checks; generated audio only, no game assets or player saves accessed\n";
	} catch (const std::exception &error) {
		music_stop();
		if (gbSndInited)
			snd_deinit();
		std::cerr << "FAIL after " << Checks << " checks: " << error.what() << '\n';
		SDL_Quit();
		return 1;
	}
	SDL_Quit();
	return 0;
}
#endif
