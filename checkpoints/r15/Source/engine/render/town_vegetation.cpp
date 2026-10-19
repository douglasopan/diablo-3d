#include "engine/render/town_vegetation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <set>
#include <span>
#include <tuple>
#include <utility>

#include "levels/dun_tile_data.hpp"
#include "levels/gendung_defs.hpp"

namespace devilution {
namespace {

struct NativeCell {
	int8_t x;
	int8_t y;
	uint16_t piece;
	uint16_t alternate = 0;
	bool Matches(uint16_t actual) const { return actual == piece || (alternate != 0 && actual == alternate); }
};

// These are complete native MIN layouts, anchored at 127/155/211/357/167/179. Special
// sprites provide only selected columns: MIN cells bridge the separated canopy
// pieces and also contain the solid trunk. A family-wide flood fill would merge
// distinct neighboring trees (for example the broad trees at 64,42 and 68,48).
constexpr NativeCell BareTreeCells[] = {
	{ -2, 0, 123 }, { -1, 0, 124 }, { 0, 0, 127 }, { 1, 0, 128 },
	{ -2, 1, 125 }, { -1, 1, 126 }, { 0, 1, 129 }, { 1, 1, 130 },
	{ -2, 2, 115 }, { -1, 2, 116 }, { 0, 2, 119 }, { 1, 2, 120 },
	{ -2, 3, 117 }, { -1, 3, 118 }, { 0, 3, 121 }, { 1, 3, 122 },
};

constexpr NativeCell AutumnTreeCells[] = {
	{ -2, -2, 139 }, { -1, -2, 140 }, { 0, -2, 135 }, { 1, -2, 136 }, { 2, -2, 159 }, { 3, -2, 160 },
	{ -2, -1, 141 }, { -1, -1, 142 }, { 0, -1, 137 }, { 1, -1, 138 }, { 2, -1, 161 }, { 3, -1, 162 },
	{ -2, 0, 131 }, { -1, 0, 132 }, { 0, 0, 155 }, { 1, 0, 156 }, { 2, 0, 143 }, { 3, 0, 144 },
	{ -2, 1, 133 }, { -1, 1, 134 }, { 0, 1, 157 }, { 1, 1, 158 }, { 2, 1, 145 }, { 3, 1, 146 },
	{ -2, 2, 147 }, { -1, 2, 148 }, { 0, 2, 151 }, { 1, 2, 152 },
	{ -2, 3, 149 }, { -1, 3, 150 }, { 0, 3, 153 }, { 1, 3, 154 },
};

constexpr NativeCell BroadTreeCells[] = {
	{ -2, -2, 203 }, { -1, -2, 204 }, { 0, -2, 207 }, { 1, -2, 208 }, { 2, -2, 215 },
	{ -2, -1, 205 }, { -1, -1, 206 }, { 0, -1, 209 }, { 1, -1, 210 }, { 2, -1, 216 },
	{ -2, 0, 199 }, { -1, 0, 200 }, { 0, 0, 211 }, { 1, 0, 212 }, { 2, 0, 195 }, { 3, 0, 196 },
	{ -2, 1, 201 }, { -1, 1, 202 }, { 0, 1, 213 }, { 1, 1, 214 }, { 2, 1, 197 }, { 3, 1, 198 },
	{ -2, 2, 187 }, { -1, 2, 188 }, { 0, 2, 191 }, { 1, 2, 192 },
	{ -2, 3, 189 }, { -1, 3, 190 }, { 0, 3, 193 }, { 1, 3, 194 },
};

constexpr NativeCell RiversideTreeCells[] = {
	{ 0, -4, 349 }, { 1, -4, 350 }, { 2, -4, 353 }, { 3, -4, 354 },
	{ 0, -3, 351 }, { 1, -3, 352 }, { 2, -3, 355 }, { 3, -3, 356 },
	{ -2, -2, 341 }, { -1, -2, 342 }, { 0, -2, 345 }, { 1, -2, 346 }, { 2, -2, 361 }, { 3, -2, 362 },
	{ -2, -1, 343 }, { -1, -1, 344 }, { 0, -1, 347 }, { 1, -1, 348 }, { 2, -1, 363 }, { 3, -1, 364 },
	{ -4, 0, 313 }, { -3, 0, 314 }, { -2, 0, 321 }, { -1, 0, 322 }, { 0, 0, 357 }, { 1, 0, 358 }, { 2, 0, 337 }, { 3, 0, 338 },
	{ -4, 1, 315 }, { -3, 1, 316 }, { -2, 1, 323 }, { -1, 1, 324 }, { 0, 1, 359 }, { 1, 1, 360 }, { 2, 1, 339 }, { 3, 1, 340 },
	{ -4, 2, 317 }, { -3, 2, 318 }, { -2, 2, 325 }, { -1, 2, 326 }, { 0, 2, 329 }, { 1, 2, 330 }, { 2, 2, 333 }, { 3, 2, 334 },
	{ -4, 3, 319 }, { -3, 3, 320 }, { -2, 3, 327 }, { -1, 3, 328 }, { 0, 3, 331 }, { 1, 3, 332 }, { 2, 3, 335 }, { 3, 3, 336 },
};

// These smaller bare trees are entirely MIN artwork; InitTownPieces does not
// assign any special CLX columns. Their solid anchor must not become a cell box.
constexpr NativeCell SmallBareTreeCells[] = {
	{ -2, 0, 171 }, { -1, 0, 172 }, { 0, 0, 167 }, { 1, 0, 168 },
	{ -2, 1, 173 }, { -1, 1, 174 }, { 0, 1, 169 }, { 1, 1, 170 },
	{ -2, 2, 163, 175 }, { -1, 2, 164, 176 },
	{ -2, 3, 165, 177 }, { -1, 3, 166, 178 },
};

constexpr NativeCell SmallForkedTreeCells[] = {
	{ -2, 0, 183, 171 }, { -1, 0, 184, 172 }, { 0, 0, 179 }, { 1, 0, 180 },
	{ -2, 1, 185, 173 }, { -1, 1, 186, 174 }, { 0, 1, 181 }, { 1, 1, 182 },
	{ -2, 2, 175 }, { -1, 2, 176 },
	{ -2, 3, 177 }, { -1, 3, 178 },
};

struct NativeTree {
	TownVegetationFamily family;
	std::span<const NativeCell> cells;
	Point trunkOffset;
	bool foliage;
	uint16_t minOnlyAnchor = 0;
};

const std::array<NativeTree, 6> NativeTrees = { {
	{ TownVegetationFamily::BareTree, BareTreeCells, { 1, 1 }, false },
	{ TownVegetationFamily::AutumnTree, AutumnTreeCells, { 1, 0 }, true },
	{ TownVegetationFamily::BroadTree, BroadTreeCells, { 1, 0 }, true },
	{ TownVegetationFamily::RiversideTree, RiversideTreeCells, { 1, 0 }, true },
	{ TownVegetationFamily::SmallBareTree, SmallBareTreeCells, { 0, 0 }, false, 167 },
	{ TownVegetationFamily::SmallForkedTree, SmallForkedTreeCells, { 0, 0 }, false, 179 },
} };

std::vector<TownVegetationGroup> Groups;
std::array<std::array<bool, MAXDUNY>, MAXDUNX> Replaced {};
bool Ready = false;

int NativeSpecial(uint16_t piece)
{
	// Keep the exact pairing from InitTownPieces. A coincidental reused MIN
	// piece without its native dSpecial must not create an extra tree.
	switch (piece) {
	case 359: return 1;
	case 357: return 2;
	case 128: return 6;
	case 129: return 7;
	case 127: return 8;
	case 116: return 9;
	case 156: return 10;
	case 157: return 11;
	case 155: return 12;
	case 161: return 13;
	case 159: return 14;
	case 213: return 15;
	case 211: return 16;
	case 216: return 17;
	case 215: return 18;
	default: return 0;
	}
}

void BuildGroups()
{
	// Derive the same anchor from every surviving special column. This also
	// handles tree footprints partially overwritten by buildings or map edges.
	std::set<std::tuple<size_t, int, int>> anchors;
	for (int y = 0; y < MAXDUNY; ++y) {
		for (int x = 0; x < MAXDUNX; ++x) {
			const uint16_t piece = dPiece[x][y];
			for (size_t family = 0; family < NativeTrees.size(); ++family) {
				if (NativeTrees[family].minOnlyAnchor != 0 && NativeTrees[family].minOnlyAnchor == piece)
					anchors.emplace(family, x, y);
			}
			const int special = NativeSpecial(piece);
			if (special == 0 || dSpecial[x][y] != special)
				continue;
			for (size_t family = 0; family < NativeTrees.size(); ++family) {
				for (const NativeCell &cell : NativeTrees[family].cells) {
					if (cell.piece == piece)
						anchors.emplace(family, x - cell.x, y - cell.y);
				}
			}
		}
	}

	for (const auto &[family, x, y] : anchors) {
		const NativeTree &tree = NativeTrees[family];
		TownVegetationGroup group { tree.family, { x + tree.trunkOffset.x, y + tree.trunkOffset.y }, { MAXDUNX, MAXDUNY }, { -1, -1 }, {}, {}, tree.foliage };
		for (const NativeCell &cell : tree.cells) {
			const Point tile { x + cell.x, y + cell.y };
			if (!InDungeonBounds(tile) || !cell.Matches(dPiece[tile.x][tile.y]))
				continue;
			group.sourceTiles.push_back(tile);
			group.minTile.x = std::min(group.minTile.x, tile.x);
			group.minTile.y = std::min(group.minTile.y, tile.y);
			group.maxTile.x = std::max(group.maxTile.x, tile.x);
			group.maxTile.y = std::max(group.maxTile.y, tile.y);
			const int special = NativeSpecial(cell.piece);
			if (special != 0 && dSpecial[tile.x][tile.y] == special)
				group.specialTiles.push_back(tile);
			Replaced[tile.x][tile.y] = true;
		}
		if (!group.specialTiles.empty() || tree.minOnlyAnchor != 0)
			Groups.push_back(std::move(group));
	}
	Ready = true;
}

} // namespace

const std::vector<TownVegetationGroup> &GetTownVegetationGroups()
{
	if (leveltype != DTYPE_TOWN) {
		static const std::vector<TownVegetationGroup> Empty;
		return Empty;
	}
	if (!Ready)
		BuildGroups();
	return Groups;
}

bool TownVegetationReplacesTile(Point tile)
{
	if (leveltype != DTYPE_TOWN || !InDungeonBounds(tile))
		return false;
	if (!Ready)
		BuildGroups();
	return Replaced[tile.x][tile.y];
}

void ResetTownVegetation()
{
	Groups.clear();
	for (auto &column : Replaced)
		column.fill(false);
	Ready = false;
}

} // namespace devilution
