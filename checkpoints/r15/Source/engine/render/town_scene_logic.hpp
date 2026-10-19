#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/point.hpp"

namespace devilution {

inline constexpr std::string_view TownSceneLogicPatchPath = "d3d-scenes/world-patches/tristram.ini";

struct TownSceneLogicCellAudit {
	std::string objectId;
	Point tile { 0, 0 };
	uint16_t beforePiece = 0, afterPiece = 0;
	uint16_t beforeSpecial = 0, afterSpecial = 0;
	Point referenceTile { 0, 0 };
};

struct TownSceneLogicRouteAudit {
	std::string name;
	std::vector<Point> cells;
};

struct TownSceneLogicAudit {
	std::string status = "not-loaded";
	std::string path;
	std::string sha256, expectedSha256;
	std::string sourceManifestSha256, baselineStaticSha256, targetStaticSha256, intentSha256;
	std::string failure;
	size_t objects = 0;
	std::vector<TownSceneLogicCellAudit> cells;
	std::vector<TownSceneLogicRouteAudit> routes;
};

struct TownSceneLogicPatchContext {
	std::string patchPath = std::string(TownSceneLogicPatchPath);
	std::string patchSha256;
	std::string targetStaticData;
	std::string targetStaticSha256;
};

using TownSceneLogicAssetResolver = std::function<bool(std::string_view, std::vector<std::byte> &, std::string &)>;

/** INI [WorldPatch] schema1, Diablo retail single player only. Each listed rock section has
 * repeated cell=x,z,beforePiece,beforeSpecial,refX,refZ,refPiece. Source rock
 * cells must exactly match the hash-bound baseline D3DSTAT1 record; the native
 * before values, safe grass donor, protected anchors and complete expected
 * final D3DSTAT1 bytes and full architecture assets are validated before any
 * map write. [Routes] has four singleton keys north/south/east/west, each a
 * semicolon-separated orthogonally adjacent x,z list, 2..94 inclusive, starting
 * inside 10..84 and ending outside that window in the named direction. Route
 * cells must all be walkable after staging; only rock cells are mutated.
 * Repeated application
 * on an already changed map is refused; CreateTown reconstructs the native
 * before map on NEW, return and reload, then calls this hook once.
 *
 * Success writes ONLY approved dPiece and dSpecial cells. SOLData and MINs
 * remain unchanged; native walkability/rendering derives from the grass piece.
 * The caller invokes this after native quest/warp changes and before
 * InitTownPieces. Direct tests can call it without that routine: afterSpecial
 * is already zero. Failure leaves the entire native map untouched. */
bool ParseApplyTownSceneLogicPatch(std::span<const std::byte> data,
    const TownSceneLogicPatchContext &context, const TownSceneLogicAssetResolver &resolver);
/** Optional paired Scene.worldPatch/worldPatchSha256. Reads through FindAsset.
 * Missing or rejected data retains the normal native town, recorded in audit. */
bool LoadApplyTownSceneLogicPatch();
const TownSceneLogicAudit &GetTownSceneLogicAudit();
void ResetTownSceneLogicAudit();

} // namespace devilution
