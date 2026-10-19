#pragma once

#include <cstddef>
#include <string_view>

#include "engine/render/town_lighting.hpp"

namespace devilution {

inline constexpr std::size_t TownLightingProfileMaxBytes = 4096;

/** Explicit Tristram calibration, separate from the generic lighting math. */
TownLightingConfig TristramLightingConfig() noexcept;

/**
 * Parse a bounded optional d3d-lighting.ini profile without asset dependencies.
 * The single [Tristram] section must contain AmbientRGB, DirectionalRGB,
 * SunDirection (x,height,z), and DirectionalIntensity exactly once. RGB values
 * and intensity are 0..4; direction components are -4..4 with height > 0.0001.
 * Blank lines, #/; comments, surrounding whitespace, and an initial UTF-8 BOM
 * are allowed. Unknown, duplicate, missing or malformed data is rejected.
 * On failure config is unchanged; callers may retain TristramLightingConfig().
 */
bool ParseTownLightingProfile(std::string_view text, TownLightingConfig &config) noexcept;

} // namespace devilution
