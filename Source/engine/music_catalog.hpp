#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "engine/sound.h"
#include "utils/language.h"

namespace devilution {

enum class MusicTheme : uint8_t { Vanilla = 0, Rock = 1, Custom = 2 };
enum class MusicVariant : uint8_t { Original = 0, Rock = 1, Alternative = 2 };

struct MusicTrackDefinition {
	std::string_view key;
	const char *name;
	const char *description;
	const char *originalPath;
	const char *sharewarePath;
	const char *rockPath;
	const char *alternativePath;
};

inline constexpr std::array<MusicTrackDefinition, NUM_MUSIC> MusicTrackCatalog { {
	{ "Town", N_("Tristram"), N_("Town and outdoor services. Rock uses the original until a replacement is installed."), "music\\dtowne.wav", "music\\stowne.wav", "music\\d3d\\town-rock.mp3", "" },
	{ "Cathedral", N_("Cathedral"), N_("Cathedral floors 1-4 and maps using that environment. Rock uses the original until a replacement is installed."), "music\\dlvla.wav", "music\\slvla.wav", "music\\d3d\\cathedral-rock.mp3", "" },
	{ "Catacombs", N_("Catacombs"), N_("Catacombs floors 5-8 and maps using that environment. Rock uses the original until a replacement is installed."), "music\\dlvlb.wav", "music\\slvla.wav", "music\\d3d\\catacombs-rock.mp3", "" },
	{ "Caves", N_("Caves"), N_("Caves floors 9-12 and maps using that environment. Rock uses the original until a replacement is installed."), "music\\dlvlc.wav", "music\\slvla.wav", "music\\d3d\\caves-rock.mp3", "" },
	{ "Hell", N_("Hell"), N_("Hell floors 13-16 and maps using that environment. Rock uses the original until a replacement is installed."), "music\\dlvld.wav", "music\\slvla.wav", "music\\d3d\\hell-rock.mp3", "" },
	{ "Nest", N_("Nest (Hellfire)"), N_("Hellfire Nest floors 17-20. Rock uses the original until a replacement is installed."), "music\\dlvlf.wav", "music\\dlvlf.wav", "music\\d3d\\nest-rock.mp3", "" },
	{ "Crypt", N_("Crypt (Hellfire)"), N_("Hellfire Crypt floors 21-24. Rock uses the original until a replacement is installed."), "music\\dlvle.wav", "music\\dlvle.wav", "music\\d3d\\crypt-rock.mp3", "" },
	{ "Menu", N_("Main Menu"), N_("Main menu, hero selection, settings, credits and support. Rock2 is the latest version; Main Menu is the earlier alternative."), "music\\dintro.wav", "music\\sintro.wav", "music\\d3d\\menu-rock2.mp3", "music\\d3d\\menu-alternative.mp3" },
} };

struct MusicSelection {
	const char *path;
	MusicVariant requested;
	MusicVariant actual;
	bool fallback;
};

// Pure selection shared by playback and its diagnostic fixtures.
template <typename Exists>
MusicSelection ResolveMusicSelection(_music_id track, MusicTheme theme, MusicVariant choice, bool fullMusic, Exists exists,
    bool inMenu = false, MusicVariant menuChoice = MusicVariant::Rock)
{
	const MusicTrackDefinition &definition = MusicTrackCatalog[track];
	const char *original = fullMusic ? definition.originalPath : definition.sharewarePath;
	// The inherited menu rotates level track IDs. Its choice must stay independent
	// of the variants selected for those same environments during a game.
	const MusicTrackDefinition &variants = inMenu ? MusicTrackCatalog[TMUSIC_INTRO] : definition;
	if (inMenu)
		choice = menuChoice;
	const MusicVariant requested = theme == MusicTheme::Rock ? MusicVariant::Rock
	    : theme == MusicTheme::Custom ? choice : MusicVariant::Original;
	const char *candidate = requested == MusicVariant::Rock ? variants.rockPath
	    : requested == MusicVariant::Alternative ? variants.alternativePath : "";
	if (candidate[0] != '\0' && exists(candidate))
		return { candidate, requested, requested, false };
	return { original, requested, MusicVariant::Original, requested != MusicVariant::Original };
}

} // namespace devilution
