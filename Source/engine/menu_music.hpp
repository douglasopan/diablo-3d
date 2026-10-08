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

} // namespace devilution
