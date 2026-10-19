#if !defined(__cpp_exceptions) && !defined(_CPPUNWIND)
#error Town Earth requires explicit source-local exception unwinding.
#endif
#include "engine/render/town_east_earth.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <new>
#include <utility>
#include "engine/render/town_ground_material.hpp"
#include "engine/render/town_border_environment.hpp"
#if !defined(__cpp_exceptions) && !defined(_CPPUNWIND)
#error Town east-earth mask allocation requires explicit exception unwinding.
#endif
namespace devilution {
namespace {
struct EastCell { int x, z; uint16_t piece; std::array<uint64_t, 32> coverage; };
#include "engine/render/town_east_earth_data.hpp"
const EastCell *At(Point tile)
{
    const auto it = std::lower_bound(EastCells.begin(), EastCells.end(), tile,
        [](const EastCell &a, Point b) { return a.z < b.y || (a.z == b.y && a.x < b.x); });
    return it != EastCells.end() && it->x == tile.x && it->z == tile.y ? &*it : nullptr;
}
} // namespace
bool TownRoadPresentationCellSource(Point tile, uint16_t piece, uint16_t special)
{
    if (!TownNativeMapPresentationActive()) return false;
    if (TownEntranceRoadCellSource(tile, piece, special)) return true;
    const auto *cell = At(tile); return cell != nullptr && piece == cell->piece && special == 0;
}
bool TownRoadPresentationUsesVisualDonor(Point tile) { return TownEntranceRoadUsesVisualDonor(tile); }
bool TownRoadPresentationTextureMatches(Point tile, int width, int height, bool repeat,
    std::span<const uint8_t> pixels, std::span<const uint8_t> opacity)
{
    if (!TownNativeMapPresentationActive()) return false;
    const auto *cell = At(tile);
    if (cell != nullptr) return TownGroundMaterialSourceMatches(cell->piece, width, height, repeat, pixels, opacity);
    return TownEntranceRoadTextureMatches(tile, width, height, repeat, pixels, opacity);
}
TownRoadCoverageStatus PrepareTownRoadPresentationCoverage(Point tile, bool admitted,
    std::span<const uint8_t> opacity, std::vector<uint8_t> &mask)
{
    if (!TownNativeMapPresentationActive()) { mask.clear(); return TownRoadCoverageStatus::Invalid; }
    const auto *cell = At(tile);
    if (cell == nullptr) return PrepareTownRoadCellCoverage(GetTownEntranceRoadBand(), tile, admitted, opacity, mask);
    mask.clear();
    if (!admitted || opacity.size() != 2048) return TownRoadCoverageStatus::Invalid;
    try {
        std::vector<uint8_t> prepared(2048, 0); bool any = false;
        for (size_t i = 0; i < prepared.size(); ++i) {
            if (opacity[i] != 0 && ((cell->coverage[i / 64] >> (i % 64)) & 1U) != 0) {
                prepared[i] = 1; any = true;
            }
        }
        if (!any) return TownRoadCoverageStatus::Empty;
        mask = std::move(prepared); return TownRoadCoverageStatus::Ready;
    } catch (const std::bad_alloc &) { return TownRoadCoverageStatus::Invalid; }
}
bool BuildTownRoadPresentationCoverage(Point tile, bool admitted,
    std::span<const uint8_t> opacity, std::vector<uint8_t> &mask)
{
 return PrepareTownRoadPresentationCoverage(tile, admitted, opacity, mask) == TownRoadCoverageStatus::Ready;
}

} // namespace devilution
