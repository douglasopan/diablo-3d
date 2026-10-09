#include "engine/render/town_props.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>

#include "engine/render/town_scene.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/gendung_defs.hpp"

namespace devilution {
namespace {

struct NativeCell {
	int8_t x;
	int8_t y;
	uint16_t piece;
};

// Verified native rock layouts: every anchor in the original town atlas has
// these exact neighbors. In particular, 230..233 form one irregular boulder,
// not four columns. Some edge fragments (220/227/229) are walkable but still
// belong to the same painted object. No flood fill joins neighboring rocks.
constexpr NativeCell PairedRock[] = { { 0, 0, 219 }, { 1, 0, 220 } };
constexpr NativeCell SmallRockA[] = { { 0, 0, 221 } };
constexpr NativeCell SmallRockB[] = { { 0, 0, 222 } };
constexpr NativeCell AngledRock[] = { { 0, 0, 223 }, { 0, 1, 224 }, { 1, 1, 225 } };
constexpr NativeCell WideRock[] = { { 0, 0, 226 }, { 1, 0, 227 }, { 0, 1, 228 }, { 1, 1, 229 } };
constexpr NativeCell LargeRock[] = { { 0, 0, 230 }, { 1, 0, 231 }, { 0, 1, 232 }, { 1, 1, 233 } };

struct NativeProp {
	std::span<const NativeCell> cells;
	Point footOffset;
};

const std::array<NativeProp, 6> NativeProps = { {
	{ PairedRock, { 1, 0 } },
	{ SmallRockA, { 0, 0 } },
	{ SmallRockB, { 0, 0 } },
	{ AngledRock, { 1, 1 } },
	{ WideRock, { 1, 1 } },
	{ LargeRock, { 1, 1 } },
} };

std::vector<TownPropGroup> Groups;
std::array<std::array<bool, MAXDUNY>, MAXDUNX> Replaced {};
bool Ready = false;

void BuildGroups()
{
	for (int y = 0; y < MAXDUNY; ++y) {
		for (int x = 0; x < MAXDUNX; ++x) {
			for (const NativeProp &prop : NativeProps) {
				if (dPiece[x][y] != prop.cells.front().piece)
					continue;
				// All cells must agree before any proxy is suppressed. Partial,
				// modified or edge-clipped layouts keep their ordinary fallback.
				bool matches = true;
				for (const NativeCell &cell : prop.cells) {
					const Point tile { x + cell.x, y + cell.y };
					if (!InDungeonBounds(tile) || dPiece[tile.x][tile.y] != cell.piece
					    || dSpecial[tile.x][tile.y] != 0 || Replaced[tile.x][tile.y]) {
						matches = false;
						break;
					}
				}
				if (!matches)
					continue;
				TownPropGroup group { { x + prop.footOffset.x, y + prop.footOffset.y },
					{ MAXDUNX, MAXDUNY }, { -1, -1 }, {} };
				group.sourceTiles.reserve(prop.cells.size());
				for (const NativeCell &cell : prop.cells) {
					const Point tile { x + cell.x, y + cell.y };
					group.sourceTiles.push_back(tile);
					group.minTile.x = std::min(group.minTile.x, tile.x);
					group.minTile.y = std::min(group.minTile.y, tile.y);
					group.maxTile.x = std::max(group.maxTile.x, tile.x);
					group.maxTile.y = std::max(group.maxTile.y, tile.y);
					Replaced[tile.x][tile.y] = true;
				}
				Groups.push_back(std::move(group));
			}
		}
	}
	Ready = true;
}

} // namespace

const std::vector<TownPropGroup> &GetTownPropGroups()
{
	if (leveltype != DTYPE_TOWN) {
		static const std::vector<TownPropGroup> Empty;
		return Empty;
	}
	if (!Ready)
		BuildGroups();
	return Groups;
}

bool TownPropReplacesTile(Point tile)
{
	if (leveltype != DTYPE_TOWN || !InDungeonBounds(tile))
		return false;
	if (!Ready)
		BuildGroups();
	return Replaced[tile.x][tile.y];
}

bool TownPropHiddenByArchitecture(const TownPropGroup &group)
{
	if (leveltype != DTYPE_TOWN || group.sourceTiles.empty())
		return false;
	for (Point tile : group.sourceTiles) {
		if (!InDungeonBounds(tile) || !TownSceneReplacesTile(tile))
			return false;
	}
	// This four-cell native filler is behind the tavern frontage in the
	// original art. The imported main+wing supplies that masonry; the old
	// conservative wall bounds end at z61.4 and must not keep this relief.
	// Exact native layout and external binding avoid hiding legitimate rocks.
	if (group.referenceFootpoint == Point { 51, 63 } && group.sourceTiles.size() == 4) {
		constexpr std::array<NativeCell, 4> TavernFiller = { {
			{ 50, 62, 230 }, { 51, 62, 231 }, { 50, 63, 232 }, { 51, 63, 233 },
		} };
		const bool exactNativeLayout = std::all_of(TavernFiller.begin(), TavernFiller.end(), [&](const NativeCell &cell) {
			const Point tile { cell.x, cell.y };
			return dPiece[tile.x][tile.y] == cell.piece
			    && std::find(group.sourceTiles.begin(), group.sourceTiles.end(), tile) != group.sourceTiles.end();
		});
		if (exactNativeLayout) {
			for (const TownSceneModel &model : GetTownScene()) {
				if (model.externalModel && model.kind == TownSceneKind::Tavern
				    && model.minTile == Point { 46, 54 } && model.maxTile == Point { 53, 63 })
					return true;
			}
		}
	}
	constexpr float Margin = 0.01F;
	for (const TownSceneModel &model : GetTownScene()) {
		const TownScenePhysicalBounds &bounds = model.physicalBounds;
		if (bounds.closed
		    && group.referenceFootpoint.x > bounds.minX + Margin
		    && group.referenceFootpoint.x < bounds.maxX - Margin
		    && group.referenceFootpoint.y > bounds.minZ + Margin
		    && group.referenceFootpoint.y < bounds.maxZ - Margin)
			return true;
	}
	return false;
}

void ResetTownProps()
{
	Groups.clear();
	for (auto &column : Replaced)
		column.fill(false);
	Ready = false;
}

} // namespace devilution
