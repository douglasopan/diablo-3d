#pragma once

#include <cstdint>
#include <vector>

#include "engine/point.hpp"

namespace devilution {

enum class TownVegetationFamily : uint8_t { BareTree, AutumnTree, BroadTree, RiversideTree, SmallBareTree, SmallForkedTree };

/** One complete native tree, including MIN fragments without a dSpecial sprite.
 * Cells are verified against the native piece stencil, not a rectangular area.
 * The renderer replaces only scenery above the ground; native ground, river,
 * collision and all simulation arrays remain unchanged. */
struct TownVegetationGroup {
	TownVegetationFamily family;
	Point referenceFootpoint; // Native trunk cell (130,156,212,358,167 or179).
	Point minTile;
	Point maxTile; // Inclusive bounds of actual sourceTiles, not a replacement mask.
	std::vector<Point> sourceTiles;
	std::vector<Point> specialTiles; // Empty for complete trees painted only by MIN.
	bool foliage;
};

/** Cached for the current town. Call ResetTownVegetation after reloading town
 * pieces/specials, including when a new game reuses the same asset pointers. */
const std::vector<TownVegetationGroup> &GetTownVegetationGroups();

/** Exact source-cell membership; never suppress the native ground layer. */
bool TownVegetationReplacesTile(Point tile);
void ResetTownVegetation();

} // namespace devilution
