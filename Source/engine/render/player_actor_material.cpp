#include "engine/render/player_actor_material.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <unordered_map>
#include <utility>

namespace devilution {
namespace {
constexpr std::size_t MaxPixels = 4096U * 4096U;
constexpr std::size_t MaxMaterialBytes = 128U * 1024U * 1024U;
constexpr std::size_t MaxColors = 524288;
TownLightColor Decode(std::uint32_t argb)
{
    return TownSrgbToLinear({ static_cast<float>((argb >> 16) & 255) / 255,
        static_cast<float>((argb >> 8) & 255) / 255, static_cast<float>(argb & 255) / 255 });
}
std::uint32_t Encode(TownLightColor linear)
{
    const auto color = TownLinearToSrgb(linear);
    const auto byte = [](float value) { return static_cast<std::uint32_t>(std::clamp(value * 255 + 0.5F, 0.0F, 255.0F)); };
    return 0xFF000000U | (byte(color.red) << 16) | (byte(color.green) << 8) | byte(color.blue);
}
float Luminance(TownLightColor linear)
{
    return linear.red * 0.2126F + linear.green * 0.7152F + linear.blue * 0.0722F;
}
class PaletteSearch {
    struct Node { std::array<int, 3> rgb; unsigned index, axis; int left, right; };
    std::array<Node, 256> nodes_ {};
    unsigned count_ = 0;
    int Build(std::array<unsigned, 256> &order, unsigned first, unsigned end, unsigned depth,
        std::span<const std::uint32_t> palette)
    {
        if (first == end) return -1;
        const auto axis = depth % 3, shift = 16 - axis * 8, middle = (first + end) / 2;
        std::nth_element(order.begin() + first, order.begin() + middle, order.begin() + end, [&](unsigned a, unsigned b) {
            const auto x = (palette[a] >> shift) & 255, y = (palette[b] >> shift) & 255;
            return x == y ? a < b : x < y;
        });
        const auto id = count_++; const auto color = palette[order[middle]];
        nodes_[id] = { { int((color >> 16) & 255), int((color >> 8) & 255), int(color & 255) }, order[middle], axis, -1, -1 };
        nodes_[id].left = Build(order, first, middle, depth + 1, palette);
        nodes_[id].right = Build(order, middle + 1, end, depth + 1, palette);
        return static_cast<int>(id);
    }
    void Search(int id, const std::array<int, 3> &rgb, unsigned &distance, unsigned &index) const
    {
        if (id < 0) return;
        const auto &node = nodes_[static_cast<unsigned>(id)];
        const int r = rgb[0] - node.rgb[0], g = rgb[1] - node.rgb[1], b = rgb[2] - node.rgb[2];
        const auto squared = static_cast<unsigned>(r * r + g * g + b * b);
        if (squared < distance || (squared == distance && node.index < index)) { distance = squared; index = node.index; }
        const auto delta = rgb[node.axis] - node.rgb[node.axis];
        Search(delta < 0 ? node.left : node.right, rgb, distance, index);
        if (static_cast<unsigned>(delta * delta) <= distance) Search(delta < 0 ? node.right : node.left, rgb, distance, index);
    }
public:
    explicit PaletteSearch(std::span<const std::uint32_t> palette)
    {
        std::array<unsigned, 256> order; for (unsigned i = 0; i < 256; ++i) order[i] = i;
        Build(order, 0, 256, 0, palette);
    }
    std::uint8_t Closest(std::uint32_t color) const
    {
        unsigned distance = std::numeric_limits<unsigned>::max(), index = 256;
        Search(0, { int((color >> 16) & 255), int((color >> 8) & 255), int(color & 255) }, distance, index);
        return static_cast<std::uint8_t>(index);
    }
};
bool FiniteLight(TownLightColor color)
{
    for (float value : { color.red, color.green, color.blue })
        if (!std::isfinite(value) || value < 0 || value > 8) return false;
    return true;
}
} // namespace

bool PreparePlayerActorMaterial(const PlayerActorMaterialInput &input,
    PlayerActorMaterial &output, std::string &error)
{
    const auto fail = [&error](const char *message) { error = message; return false; };
    if (input.width == 0 || input.height == 0 || input.width > 4096 || input.height > 4096
        || input.palette.size() != 256)
        return fail("Actor material requires bounded RGB dimensions and a full palette");
    const auto pixels = std::size_t(input.width) * input.height;
    if (pixels > MaxPixels || input.rgb.size() != pixels * 3)
        return fail("Actor material RGB size does not match dimensions");
    const bool cathedral = input.world == PlayerActorMaterialWorld::NativeCathedral;
    if ((!cathedral && input.world != PlayerActorMaterialWorld::TownDirectional)
        || (cathedral && input.nativeLightLut.size() != 256 * 16))
        return fail("Actor material world/native lighting layout is invalid");
    if (!cathedral && (!FiniteLight(input.townLight.ambient) || !FiniteLight(input.townLight.directional)
        || !std::isfinite(input.townLight.directionalIntensity) || input.townLight.directionalIntensity < 0
        || input.townLight.directionalIntensity > 8))
        return fail("Actor Town material lighting is invalid");
    const unsigned levels = cathedral ? 16 : 64;
    try {
        PlayerActorMaterial prepared;
        prepared.width = input.width; prepared.height = input.height; prepared.lightLevels = levels;
        prepared.texelCodes.resize(pixels);
        std::unordered_map<std::uint32_t, std::uint32_t> colors;
        colors.reserve(std::min(pixels, MaxColors));
        for (std::size_t i = 0; i < pixels; ++i) {
            const auto *rgb = input.rgb.data() + i * 3;
            const std::uint32_t argb = 0xFF000000U | (std::uint32_t(rgb[0]) << 16) | (std::uint32_t(rgb[1]) << 8) | rgb[2];
            const auto found = colors.find(argb);
            if (found != colors.end()) { prepared.texelCodes[i] = found->second; continue; }
            const std::size_t count = prepared.sourceColors.size() + 1;
            // Account for the exact source table, uint32 texels and paired LUT
            // payload. The temporary dictionary has its own MaxColors bound.
            if (count > MaxColors || pixels * 4 + count * (4 + levels * 5) > MaxMaterialBytes)
                return fail("Actor material exceeds lossless color/LUT budget");
            const auto code = static_cast<std::uint32_t>(prepared.sourceColors.size());
            prepared.texelCodes[i] = code; colors.emplace(argb, code); prepared.sourceColors.push_back(argb);
        }
        prepared.indexedLightLut.resize(prepared.sourceColors.size() * levels);
        prepared.rgbaLightLut.resize(prepared.indexedLightLut.size());
        const PaletteSearch palette(input.palette);
        std::array<float, 256> paletteLuminance;
        for (std::size_t i = 0; i < paletteLuminance.size(); ++i) paletteLuminance[i] = Luminance(Decode(input.palette[i]));
        for (std::size_t code = 0; code < prepared.sourceColors.size(); ++code) {
            const auto source = prepared.sourceColors[code];
            const auto base = Decode(source);
            const auto native = palette.Closest(source);
            for (unsigned level = 0; level < levels; ++level) {
                const auto index = code * levels + level;
                if (cathedral) {
                    const auto mapped = input.nativeLightLut[std::size_t(native) * 16 + level];
                    prepared.indexedLightLut[index] = mapped;
                    const float amount = level == 0 ? 1 : level == 15 ? 0 : paletteLuminance[native] > 0
                        ? std::clamp(paletteLuminance[mapped] / paletteLuminance[native], 0.0F, 1.0F) : 0;
                    prepared.rgbaLightLut[index] = level == 0 ? source : Encode({ base.red * amount, base.green * amount, base.blue * amount });
                } else {
                    const float amount = static_cast<float>(level) / static_cast<float>(levels - 1);
                    TownLightingSample light;
                    light.ambient = input.townLight.ambient;
                    light.directional = { input.townLight.directional.red * input.townLight.directionalIntensity * amount,
                        input.townLight.directional.green * input.townLight.directionalIntensity * amount,
                        input.townLight.directional.blue * input.townLight.directionalIntensity * amount };
                    const auto rgb = Encode(ComposeTownLitColor(base, light));
                    prepared.rgbaLightLut[index] = rgb; prepared.indexedLightLut[index] = palette.Closest(rgb);
                }
            }
        }
        output = std::move(prepared); error.clear(); return true;
    } catch (const std::bad_alloc &) { return fail("Actor material allocation failed"); }
}
} // namespace devilution
