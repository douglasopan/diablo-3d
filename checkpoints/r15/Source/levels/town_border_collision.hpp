#pragma once

#include "engine/point.hpp"
#include "levels/dun_tile.hpp"

namespace devilution {

/** Explicit authored Town-body footprints, never inferred from art/AABBs/SOL.
 * Adds only Solid/BlockMissile to the existing native query after whole-map
 * admission. Does not change dPiece, SOLData, RNG or a saved dungeon index. */
bool TownBorderTileHasAny(Point position, TileProperties properties) noexcept;

} // namespace devilution
