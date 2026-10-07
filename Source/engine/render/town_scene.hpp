#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/point.hpp"

namespace devilution {

struct TownImportedTexture;

/** Geometry materials are resolved from the original town artwork by town_view. */
enum class TownSceneMaterial : uint8_t { Wall, Roof, Stone, Timber, Water };
enum class TownSceneKind : uint8_t { House, Smithy, Tavern, Cathedral, Well, Cabin, Crypt };
enum class TownSceneSurfaceRole : uint8_t { Exterior, Interior, Underside };

/** Distinct finishes within a building; never sample door/glass into masonry. */
enum class TownSceneSurfaceDetail : uint8_t {
	None,
	Masonry,
	Thatch,
	Door,
	WindowGlass,
	TimberTrim,
	BarrelStaves,
	BarrelHoops,
	Foundation,
	Count,
};

/** Outward unit normal in the same x/height/z coordinates as scene vertices. */
struct TownSceneNormal {
	float x = 0;
	float height = 1;
	float z = 0;
};

/** Wall volume, separate from the source artwork and roof overhang. */
struct TownScenePhysicalBounds {
	float minX = 0;
	float minZ = 0;
	float maxX = 0;
	float maxZ = 0;
	float wallHeight = 0;
	bool closed = false;
};

/** Native isometric composition: pixel = (32*(x-z), 16*(x+z)-32*height). */
struct TownSceneNativeArtwork {
	Point referenceTile { 0, 0 };
	Point pixelOrigin { 0, 0 }; // Relative to the reference tile's native footpoint.
	Point pixelSize { 0, 0 };
	bool enabled = false;
	Point minTile { 0, 0 }; // CEL composition bounds, not the wall footprint.
	Point maxTile { 0, 0 };
	uint16_t fringeMinPiece = 0; // Extra cells contain only this building's paint family.
	uint16_t fringeMaxPiece = 0;
};

struct TownSceneVertex {
	float x;
	float height;
	float z;
	float u; // Continuous material coordinate in world units; not native-photo UV.
	float v;
};

struct TownSceneTriangle {
	std::array<TownSceneVertex, 3> vertices;
	TownSceneMaterial material;
	Point sourceTile;
	Point pickTile;
	bool nativeProjection = false;
	TownSceneNormal normal;
	TownSceneSurfaceRole surfaceRole = TownSceneSurfaceRole::Exterior;
	TownSceneSurfaceDetail surfaceDetail = TownSceneSurfaceDetail::None;
};

/**
 * A clean native material reference, rectified through an explicit world plane.
 * referencePlane maps its physical u/v to x/height/z, including outside the
 * triangle. uvMin/uvMax bound the sample rectangle; sourceMin/sourceMax are
 * native pixel audit bounds relative to nativeArtwork.referenceTile. A renderer
 * samples the plane's projected pixels, not a stretched isometric bitmap crop.
 * Repeating finishes use world-unit UVs and repeatWorldSize. Door/glass finishes
 * use their local physical UVs and clamp to this rectangle instead of repeating.
 */
struct TownSceneMaterialPatch {
	TownSceneSurfaceDetail surfaceDetail;
	TownSceneMaterial material;
	std::array<TownSceneVertex, 3> referencePlane;
	std::array<float, 2> uvMin;
	std::array<float, 2> uvMax;
	std::array<float, 2> repeatWorldSize;
	bool repeat = true;
	Point sourceMin;
	Point sourceMax;
};

/** One coherent architectural object, replacing its painted scenery footprint. */
struct TownSceneModel {
	TownSceneKind kind;
	Point minTile;
	Point maxTile; // Inclusive scenery bounds; ground and simulation remain native.
	std::vector<TownSceneTriangle> triangles;
	TownScenePhysicalBounds physicalBounds;
	TownSceneNativeArtwork nativeArtwork;
	std::vector<TownSceneMaterialPatch> materialPatches;
	std::shared_ptr<const TownImportedTexture> importedTexture;
	bool externalModel = false;
};

const std::vector<TownSceneModel> &GetTownScene();
bool TownSceneReplacesTile(Point tile);
void ResetTownScene();

} // namespace devilution
