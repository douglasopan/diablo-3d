#pragma once

#include "../world_grid_metric.hpp"

namespace devilution::cathedral {
// First existing masonry course above the selected 3.808378935 m arch.
// This is the model's exact float32 boundary, rounded to 3.92 m for display;
// the human supplied a visual redline, not a numerical height.
inline constexpr float VisualWallTopMetres = 3.919999837875366F;
inline constexpr float LegacyWallTopMetres = 5.6F;

inline constexpr bool UsesAuthoredVisualWallTop(int level, bool setLevel)
{
    return level == 1 && !setLevel;
}

inline constexpr float VisualWallTopMetresForLevel(int level, bool setLevel)
{
    return UsesAuthoredVisualWallTop(level, setLevel) ? VisualWallTopMetres : LegacyWallTopMetres;
}

inline constexpr float VisualWallTopNativeGrid(int level, bool setLevel)
{
    // Match the CPU and resident-mesh transform of the retained authored cap.
    // H / metric and H * (1 / metric) otherwise differ by one float32 ULP.
    return UsesAuthoredVisualWallTop(level, setLevel)
        ? VisualWallTopMetres * PhysicalToNativeGrid(1.0F)
        : PhysicalToNativeGrid(LegacyWallTopMetres);
}

inline constexpr float ArchitecturalHeightToNativeGrid(float metres, int level, bool setLevel)
{
    if (UsesAuthoredVisualWallTop(level, setLevel) && metres == VisualWallTopMetres)
        return VisualWallTopNativeGrid(level, setLevel);
    return PhysicalToNativeGrid(metres);
}
} // namespace devilution::cathedral
