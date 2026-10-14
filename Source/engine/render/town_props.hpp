#pragma once

#include <vector>

#include "engine/point.hpp"

namespace devilution {

/** One complete rock painted by the native MIN layout. The exact source cells
 * can include walkable art fragments. Only scenery above the ground is replaced;
 * native ground, collision and simulation arrays remain unchanged. */
struct TownPropGroup {
	Point referenceFootpoint;
	Point minTile;
	Point maxTile; // Inclusive bounds; sourceTiles is the actual replacement mask.
	std::vector<Point> sourceTiles;
};

/** Cached for the current town. Reset after loading or changing town pieces,
 * even if a new game reuses the same dungeon asset allocation. */
const std::vector<TownPropGroup> &GetTownPropGroups();

/** Exact verified MIN-cell membership; never suppress the native ground. */
bool TownPropReplacesTile(Point tile);

/** Native filler wholly inside a closed architectural body. This predicate
 * affects drawing only: the group and its diagnostic mesh remain available. */
bool TownPropHiddenByArchitecture(const TownPropGroup &group);
void ResetTownProps();

} // namespace devilution
