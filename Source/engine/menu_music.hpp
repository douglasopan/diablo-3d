#pragma once

#include "engine/assets.hpp"

namespace devilution {

// Optional owner-provided soundtrack; original game music remains the fallback.
inline constexpr char MenuMusicOverridePath[] = "music\\d3d-main-menu.mp3";

[[nodiscard]] inline bool HaveMenuMusicOverride()
{
	return FindAsset(MenuMusicOverridePath).ok();
}

} // namespace devilution
