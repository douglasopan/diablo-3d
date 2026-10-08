#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/point.hpp"
#include "engine/render/town_lighting.hpp"

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
	CandleWax,
	CandleHolder,
	CandleWick,
	FireCore,
	FireTip,
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

enum class TownCabinOpeningKind : uint8_t { Window, Door, RoofVent };

/** Explicit geometric opening. X planes use u=z,v=height; Z planes u=x,v=height;
 * Height planes use u=x,v=z. The aperture coordinate is the inner room plane.
 * The depth slab bounds the only imported geometry eligible for a local cut.
 * Height supports measured horizontal roof vents; sloped vents require their
 * own measured geometry rather than an invented hole. Doors are not added by
 * declaring this type: native closed-door collision is always preserved.
 */
struct TownCabinOpening {
	TownCabinOpeningKind kind = TownCabinOpeningKind::Window;
	TownLightAperture aperture;
	float outerCoordinate = 0;
	float clipMinimum = 0;
	float clipMaximum = 0;
	bool woodenMuntins = true;
	TownSceneMaterial liningMaterial = TownSceneMaterial::Stone;
};

enum class TownCabinFireKind : uint8_t { Candle, Candelabrum, Hearth, Campfire };

/** Actual fire geometry plus its deterministic base point light. Flicker is a
 * runtime intensity calculation; it never rewrites source geometry or colors.
 */
struct TownCabinFireSource {
	TownCabinFireKind kind = TownCabinFireKind::Candle;
	TownPointLight light;
	std::vector<TownSceneTriangle> emissiveTriangles;
	uint32_t flickerSeed = 0;
};

/** Runtime adjunct for the optional east-cabin model; imported source stays exact.
 * Openings and lighting use the same physical polygons, preserving opaque
 * corners. Interior UVs are world-unit coordinates; exterior copies keep their
 * imported normalized UVs. All fire sources share these linear RGB colors for
 * the renderer's scalar irradiance/emission caches. No electrical lights.
 */
struct TownCabinInterior {
	std::vector<TownSceneTriangle> exteriorTriangles;
	std::vector<TownSceneTriangle> interiorTriangles;
	TownLightVector roomMinimum;
	TownLightVector roomMaximum;
	std::vector<TownCabinOpening> openings;
	std::vector<TownLightAperture> apertures;
	/** The room shell followed by opaque authored window bars. The first
	 * aperture span borrows this same adjunct's stable apertures vector. */
	std::vector<TownLightOccluder> lightOccluders;
	std::vector<TownCabinFireSource> fireSources;
	TownLightColor fireColor { 1.0F, 0.665F, 0.094F };
	TownLightColor fireEmissionLinear;
	TownLightColor fireCoreEmissionLinear;
	TownLightColor fireTipEmissionLinear;
	uint32_t clippedSourceTriangles = 0;
};

/** Identity of the bytes actually decoded, distinct from the launcher's receipt. */
struct TownModelRuntimeAudit {
	std::string assetPath;
	std::string sourceKind;
	std::string sha256;
	std::string expectedSha256;
	std::string revision;
	std::string status = "procedural";
	std::string failure;
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
	std::shared_ptr<const TownCabinInterior> cabinInterior;
	TownModelRuntimeAudit runtimeAudit;
};

/** Original imported triangles are never rewritten by this bounded adjunct. */
bool BuildTownCabinInterior(TownSceneModel &model);
const std::vector<TownSceneTriangle> &TownSceneExteriorTriangles(const TownSceneModel &model);
const std::vector<TownSceneModel> &GetTownScene();
/** Monotonic reset identity for caches borrowing the assembled scene. */
uint64_t GetTownSceneRevision();
bool TownSceneReplacesTile(Point tile);
void ResetTownScene();

} // namespace devilution
