#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace devilution {

// Existing ENV29 artistic field, independent of generator/collision revisions.
// Restores the dry soil/litter/grass/ash colors lost by the new binary selector.
inline constexpr uint32_t SurfaceRegionGroundAppearanceRevision = 2;
inline constexpr std::string_view SurfaceRegionGroundAppearanceSourceSha256 =
    "4125468a79b2aa64c2ef426b34c9c4c7e8f6283bb685cee549992bb00573c260";

// Absolute world native X/Z. Same authored linear RGB for active and Halo,
// both views and every RegionId. No H, native state, RNG, camera or menu input.
// Prepare/cache the result with immutable geometry; do not evaluate per pixel.
std::optional<std::array<float, 3>> SampleSurfaceRegionGroundAppearance(
    double worldNativeX, double worldNativeZ) noexcept;

} // namespace devilution
