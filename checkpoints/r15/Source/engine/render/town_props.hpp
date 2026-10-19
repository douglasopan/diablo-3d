#pragma once

#include <vector>

#include "engine/point.hpp"
#include "engine/render/town_prop_stencils.hpp"

namespace devilution {

/** One identified rock, grave arrangement or selected wall native layout.
 * The exact source cells
 * can include walkable art fragments. Only scenery above the ground is replaced;
 * native ground, collision and simulation arrays remain unchanged. */
struct TownPropGroup {
	Point referenceFootpoint;
	Point minTile;
	Point maxTile; // Inclusive bounds; sourceTiles is the actual replacement mask.
	std::vector<Point> sourceTiles;
	town_prop_stencils::Kind kind = town_prop_stencils::Kind::Rock;
	uint16_t nativeLayout = 0; // Rock anchor MIN piece or verified native TIL ID.
};

/** Cached for the current town. Reset after loading or changing town pieces,
 * even if a new game reuses the same dungeon asset allocation. */
const std::vector<TownPropGroup> &GetTownPropGroups();

/** Exact verified MIN-cell membership; never suppress the native ground. */
bool TownPropReplacesTile(Point tile);

/** Only rock paint is replaced by a clean ground donor. Graves retain their
 * own native ground finish; wall ground cells are never in the scenery mask. */
bool TownPropReplacesGroundPaint(Point tile);

/** Native filler wholly inside a closed architectural body. This predicate
 * affects drawing only: the group and its diagnostic mesh remain available. */
bool TownPropHiddenByArchitecture(const TownPropGroup &group);
void ResetTownProps();

} // namespace devilution
