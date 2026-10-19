#include "engine/render/actor_model.hpp"
#include "engine/render/player_actor_material.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

namespace {
using namespace devilution;
unsigned Checks = 0, Failures = 0;
void Check(bool pass, const char *name)
{
    ++Checks; if (!pass) { ++Failures; std::fprintf(stderr, "FAIL %s\n", name); }
}
std::uint8_t Brute(std::uint32_t rgb, const std::array<std::uint32_t, 256> &palette)
{
    unsigned bestDistance = std::numeric_limits<unsigned>::max(), best = 0;
    for (unsigned i = 0; i < 256; ++i) {
        const int r = int((rgb >> 16) & 255) - int((palette[i] >> 16) & 255);
        const int g = int((rgb >> 8) & 255) - int((palette[i] >> 8) & 255);
        const int b = int(rgb & 255) - int(palette[i] & 255);
        const auto distance = static_cast<unsigned>(r * r + g * g + b * b);
        if (distance < bestDistance) { bestDistance = distance; best = i; }
    }
    return static_cast<std::uint8_t>(best);
}
} // namespace
int main(int argc, char **argv)
{
    using namespace devilution;
    if (argc != 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<char> bytes { std::istreambuf_iterator<char>(file), {} };
    ActorModel body; std::string error;
    Check(LoadActorModel({ reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size() }, body, error), "actual composed BODY parser");
    if (!error.empty()) return 3;
    std::array<std::uint32_t, 256> palette;
    std::array<std::uint8_t, 4096> light;
    for (unsigned i = 0; i < 256; ++i) {
        palette[i] = 0xFF000000U | (i << 16) | (i << 8) | i;
        for (unsigned level = 0; level < 16; ++level) light[i * 16 + level] = static_cast<std::uint8_t>(i * (15 - level) / 15);
    }
    PlayerActorMaterialInput input;
    input.width = body.textureWidth; input.height = body.textureHeight; input.rgb = body.textureRgb;
    input.palette = palette; input.nativeLightLut = light;
    PlayerActorMaterial material;
    const auto start = std::chrono::steady_clock::now();
    Check(PreparePlayerActorMaterial(input, material, error), "full actual 2048 BODY RGB, synthetic native palette/LUT");
    if (!error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 4; }
    const auto colors = material.sourceColors.size();
    Check(colors == 196356 && material.texelCodes.size() == 2048U * 2048U && material.lightLevels == 16, "actual exact RGB domain and expected full texture dimensions");
    bool texels = true;
    for (std::size_t i = 0; i < material.texelCodes.size(); ++i) {
        const auto *rgb = body.textureRgb.data() + i * 3;
        const auto expected = 0xFF000000U | (std::uint32_t(rgb[0]) << 16) | (std::uint32_t(rgb[1]) << 8) | rgb[2];
        texels = texels && material.texelCodes[i] < colors && material.sourceColors[material.texelCodes[i]] == expected;
    }
    Check(texels, "all 4194304 source RGB texels reconstruct exactly, no channel reduction");
    bool rows = material.indexedLightLut.size() == colors * 16 && material.rgbaLightLut.size() == colors * 16;
    for (std::size_t code = 0; code < colors; ++code) {
        const auto native = Brute(material.sourceColors[code], palette);
        rows = rows && material.rgbaLightLut[code * 16] == material.sourceColors[code]
            && material.rgbaLightLut[code * 16 + 15] == 0xFF000000U;
        for (unsigned level = 0; level < 16; ++level)
            rows = rows && material.indexedLightLut[code * 16 + level] == light[std::size_t(native) * 16 + level];
    }
    Check(rows, "all lossless native rows/index mapping match independent 256-entry brute palette search");
    const auto *oldCodes = material.texelCodes.data();
    auto broken = input; broken.rgb = input.rgb.first(input.rgb.size() - 1);
    Check(!PreparePlayerActorMaterial(broken, material, error) && material.texelCodes.data() == oldCodes, "truncated RGB refuses atomically");
    broken = input; broken.width = 4097;
    Check(!PreparePlayerActorMaterial(broken, material, error) && material.texelCodes.data() == oldCodes, "oversized dimensions refuse before allocation");
    broken = input; broken.nativeLightLut = input.nativeLightLut.first(4095);
    Check(!PreparePlayerActorMaterial(broken, material, error) && material.texelCodes.data() == oldCodes, "wrong native LUT layout refuses atomically");
    // A small palette with duplicate entries exercises exact lowest-index ties.
    std::vector<std::uint8_t> testRgb;
    for (unsigned i = 0; i < 8192; ++i) { testRgb.push_back(static_cast<std::uint8_t>(i * 13)); testRgb.push_back(static_cast<std::uint8_t>(i / 17)); testRgb.push_back(static_cast<std::uint8_t>(i * 29)); }
    for (unsigned i = 0; i < 256; ++i) palette[i] = 0xFF000000U | ((i % 19) * 13 << 16) | ((i % 17) * 15 << 8) | (i % 13) * 19;
    input.width = 128; input.height = 64; input.rgb = testRgb; input.world = PlayerActorMaterialWorld::TownDirectional;
    PlayerActorMaterial town;
    Check(PreparePlayerActorMaterial(input, town, error), "Town color model and adversarial synthetic palette");
    bool exactSearch = true;
    for (std::size_t i = 0; i < town.rgbaLightLut.size(); ++i) exactSearch = exactSearch && town.indexedLightLut[i] == Brute(town.rgbaLightLut[i], palette);
    Check(exactSearch && town.lightLevels == 64, "every Town encoded row agrees with independent brute search including ties");
    broken = input; broken.townLight.ambient.red = std::numeric_limits<float>::quiet_NaN();
    const auto *oldTown = town.texelCodes.data();
    Check(!PreparePlayerActorMaterial(broken, town, error) && town.texelCodes.data() == oldTown, "nonfinite Town light refuses atomically");
    std::vector<std::uint8_t> excessive(512U * 1024U * 3);
    for (std::size_t i = 0; i < excessive.size() / 3; ++i) { excessive[i * 3] = static_cast<std::uint8_t>(i >> 16); excessive[i * 3 + 1] = static_cast<std::uint8_t>(i >> 8); excessive[i * 3 + 2] = static_cast<std::uint8_t>(i); }
    broken = input; broken.width = 512; broken.height = 1024; broken.rgb = excessive;
    Check(!PreparePlayerActorMaterial(broken, town, error) && town.texelCodes.data() == oldTown, "lossless LUT budget refuses without silently reducing source RGB");
    const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("{\"checks\":%u,\"failures\":%u,\"actualBodyTexels\":4194304,\"actualBodyColors\":%zu,\"nativePairedLutBytes\":%zu,\"syntheticPaletteAndLighting\":true,\"hostIntegrated\":false,\"pbrRendered\":false,\"testMilliseconds\":%.3f}\n",
        Checks, Failures, colors, colors * 16 * 5, milliseconds);
    return Failures == 0 ? 0 : 1;
}
