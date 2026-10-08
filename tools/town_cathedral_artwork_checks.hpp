#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "engine/render/town_scene.hpp"
#include "levels/gendung.h"

namespace devilution {

// Run only in the isolated retail town fixture, after scene initialization.
template <typename Check>
void RunTownCathedralArtworkChecks(Check check)
{
	size_t complete = 0;
	size_t tower = 0;
	for (int y = 8; y <= 31; ++y) {
		for (int x = 10; x <= 33; ++x) {
			const auto piece = dPiece[x][y];
			if (piece < 700 || piece > 848)
				continue;
			const bool nativeEntrance = x == 25 && (y == 29 || y == 30);
			check(TownSceneReplacesTile({ x, y }) != nativeEntrance,
			    "cathedral artwork is replaced while native entrance stairs are retained: " + std::to_string(piece));
			++complete;
			if (piece >= 835)
				++tower;
		}
	}
	check(complete == 145 && tower == 14,
	    "complete native cathedral family includes the fourteen previously unmasked tower cells");
	const Point towerTile { 12, 8 };
	const auto previous = dPiece[towerTile.x][towerTile.y];
	for (const auto unrelated : std::array<unsigned short, 10> { 0, 1, 248, 250, 285, 397, 699, 849, 900, 1170 }) {
		dPiece[towerTile.x][towerTile.y] = unrelated;
		const bool masked = TownSceneReplacesTile(towerTile);
		dPiece[towerTile.x][towerTile.y] = previous;
		check(!masked, "cathedral cleanup preserves unrelated art families inside its artwork rectangle");
	}
	for (const Point outside : std::array<Point, 4> { Point { 9, 8 }, Point { 10, 7 }, Point { 34, 8 }, Point { 10, 32 } }) {
		const auto original = dPiece[outside.x][outside.y];
		dPiece[outside.x][outside.y] = 835;
		const bool masked = TownSceneReplacesTile(outside);
		dPiece[outside.x][outside.y] = original;
		check(!masked, "cathedral cleanup does not suppress matching piece IDs outside the measured artwork rectangle");
	}
	check(!TownSceneReplacesTile({ 25, 29 }) && !TownSceneReplacesTile({ 25, 30 }),
	    "original cathedral entrance and lower stair retain their native scenery and gameplay route");
}

} // namespace devilution
