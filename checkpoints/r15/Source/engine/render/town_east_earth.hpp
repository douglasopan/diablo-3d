#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "engine/point.hpp"
namespace devilution {
bool TownRoadPresentationCellSource(Point tile, uint16_t nativePiece, uint16_t special);
bool TownRoadPresentationUsesVisualDonor(Point tile);
bool TownRoadPresentationTextureMatches(Point tile, int width, int height, bool repeat,
    std::span<const uint8_t> pixels, std::span<const uint8_t> opacity);
enum class TownRoadCoverageStatus : uint8_t;
TownRoadCoverageStatus PrepareTownRoadPresentationCoverage(Point tile, bool admitted,
    std::span<const uint8_t> nativeOpacity, std::vector<uint8_t> &mask);
bool BuildTownRoadPresentationCoverage(Point tile, bool admitted,
    std::span<const uint8_t> nativeOpacity, std::vector<uint8_t> &mask);
} // namespace devilution
