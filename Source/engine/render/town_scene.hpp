#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "engine/point.hpp"

namespace devilution {

/** Geometry materials are resolved from the original town artwork by town_view. */
enum class TownSceneMaterial : uint8_t { Wall, Roof, Stone, Timber, Water };
enum class TownSceneKind : uint8_t { House, Smithy, Tavern, Cathedral, Well, Cabin, Crypt };
enum class TownSceneSurfaceRole : uint8_t { Exterior, Interior, Underside };

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
};

/** One coherent architectural object, replacing its painted scenery footprint. */
struct TownSceneModel {
	TownSceneKind kind;
	Point minTile;
	Point maxTile; // Inclusive scenery bounds; ground and simulation remain native.
	std::vector<TownSceneTriangle> triangles;
	TownScenePhysicalBounds physicalBounds;
	TownSceneNativeArtwork nativeArtwork;
};

const std::vector<TownSceneModel> &GetTownScene();
bool TownSceneReplacesTile(Point tile);
void ResetTownScene();

} // namespace devilution
