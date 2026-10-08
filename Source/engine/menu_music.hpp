#pragma once

#include "engine/assets.hpp"
#include "engine/music_catalog.hpp"
#include "options.h"

namespace devilution {

[[nodiscard]] inline bool HaveMenuMusicOverride()
{
	const auto &options = GetOptions().Music;
	return ResolveMusicSelection(TMUSIC_INTRO, *options.theme, *options.menu, true,
	           [](const char *path) { return FindAsset(path).ok(); })
	           .actual != MusicVariant::Original;
}

/**
 * Continue menu playback when a provider/hero dialog is cancelled. Only a
 * stopped soundtrack (initial entry, intro, or return from a real game) starts
 * the next track; unchanged preferences keep the existing stream and position.
 */
inline void RefreshMenuMusic(_music_id nextTrack)
{
	music_set_game_context(false);
	if (sgnMusicTrack != NUM_MUSIC) {
		music_refresh();
		return;
	}
	music_start(nextTrack);
}

} // namespace devilution
