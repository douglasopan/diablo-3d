#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "engine/render/town_lighting.hpp"

namespace devilution {

enum class PlayerActorMaterialWorld { TownDirectional, NativeCathedral };
struct PlayerActorMaterialInput {
    std::uint32_t width = 0, height = 0;
    std::span<const std::uint8_t> rgb;
    // ARGB8888 palette and code-major 256*16 native light table. The caller
    // freezes both with the world/palette revision of this material.
    std::span<const std::uint32_t> palette;
    std::span<const std::uint8_t> nativeLightLut;
    PlayerActorMaterialWorld world = PlayerActorMaterialWorld::NativeCathedral;
    TownLightingConfig townLight;
};
struct PlayerActorMaterial {
    std::uint32_t width = 0, height = 0;
    unsigned lightLevels = 0;
    // Exact 8-bit RGB source colors; no six-bit palette reduction. Texels index
    // the paired LUTs. Material factor/COLOR_0 remain separate linear values.
    std::vector<std::uint32_t> sourceColors, texelCodes;
    std::vector<std::uint8_t> indexedLightLut;
    std::vector<std::uint32_t> rgbaLightLut;
};

// Pure bounded material preparation, not a loader/admission or PBR shader.
// Cathedral RGB retains authored hue and follows the luminance attenuation of
// its closest native palette entry (approximation); shade 0 is exact authored
// RGB and 15 black. Native indexed output follows the native table exactly.
// Town uses the existing ambient/directional color model. Failure is atomic.
bool PreparePlayerActorMaterial(const PlayerActorMaterialInput &input,
    PlayerActorMaterial &output, std::string &error);

} // namespace devilution
