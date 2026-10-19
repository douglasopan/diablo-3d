#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/render/town_scene.hpp"
#include "engine/render/town_point_shadow.hpp"

namespace devilution {

/** Direction from the receiver towards a distant light, in world x/height/z. */
struct TownShadowDirection {
	float x = 0.23F;
	float height = 1.0F;
	float z = 0.23F;
};

struct TownShadowConfig {
	TownShadowDirection toLight;
	int resolution = 512; // 128..2048; 512 or 1024 suit the current town.
	float depthBias = 0.025F; // World units along the light direction.
	float slopeBias = 0.045F;
	int pcfRadius = 1; // 0..2; one produces a 3x3 depth comparison filter.
};

/** Prepared once per receiver triangle, rather than once per screen pixel. */
struct TownShadowReceiver {
	float depthBias = 0.025F;
	float depthSlopeU = 0;
	float depthSlopeV = 0;
};

struct TownShadowStats {
	bool ready = false;
	int resolution = 0;
	std::size_t buildCount = 0;
	std::size_t inputTriangles = 0;
	std::size_t rasterizedTriangles = 0;
	std::size_t depthWrites = 0;
	std::size_t coveredTexels = 0;
	std::size_t depthBytes = 0;
	double buildMilliseconds = 0;
	uint64_t sceneHash = 0;
};

/** Borrowed, immutable light-space map; invalidated by build/reset. */
struct TownShadowMapView {
	std::span<const float> depth;
	TownShadowConfig config;
	TownShadowDirection light, right, up;
	float minU = 0, minV = 0, texelU = 1, texelV = 1;
	uint64_t revision = 0;
};
TownShadowMapView GetTownShadowMapView();

/**
 * Cache an orthographic light depth map of actual static architecture triangles.
 * Repeating the same geometry/configuration reuses it. Invalid input returns
 * false and clears the old map; an empty scene succeeds and samples as lit.
 * This never reads or changes the game map, camera, simulation or pick buffer.
 */
bool BuildTownShadowMap(const std::vector<TownSceneModel> &scene, const TownShadowConfig &config = {});

// Approved architecture plus ENV already approaches the former 262144 cap.
// This is a capacity bound, not a measured performance qualification.
inline constexpr std::size_t TownMaxShadowGeometryTriangles = 524288;
inline constexpr std::size_t TownMaxShadowGeometryMasks = 512;
inline constexpr std::size_t TownMaxShadowGeometryMaskBytes = 16 * 1024 * 1024;
inline constexpr std::size_t TownMaxShadowGeometryCandidateTests = 64 * 1024 * 1024;

/** The host supplies the actual world draw geometry before camera clipping,
 * in local native world units x/height/z (one unit per native tile). Global
 * coordinates/world offsets/metres are converted by the producer before this
 * boundary; material UV coordinates remain independent of world positions.
 * Spans are borrowed only until Build returns. A nonempty layer needs nonzero
 * epoch/identity/revision; nonempty layers must have the same epoch. The host
 * changes revision whenever its exact geometry/mask content or pose changes. */
struct TownShadowGeometryLayer {
	std::span<const TownPointShadowTriangle> triangles;
	std::span<const TownPointShadowMask> masks;
	uint64_t epoch = 0, identity = 0, revision = 0;
};

enum class TownShadowGeometryFailure { None, InvalidInput, Capacity, Allocation };

/** Per-call observations. ready means a complete valid field, including an
 * empty or wholly transparent field (which has no occlusion). candidateTests
 * counts actual bounding-box raster iterations, not a screen-space estimate.
 * Cache hits still validate and hash every borrowed input byte. */
struct TownShadowGeometryStats {
	bool ready = false, budgetLimited = false;
	bool cacheHit = false, staticCacheHit = false, projectionRebuilt = false;
	TownShadowGeometryFailure failure = TownShadowGeometryFailure::None;
	std::size_t buildCount = 0, staticBuildCount = 0;
	std::size_t staticInputTriangles = 0, dynamicInputTriangles = 0, masks = 0, maskBytes = 0;
	std::size_t opaqueTriangles = 0, maskedTriangles = 0;
	std::size_t staticRasterizedTriangles = 0, dynamicRasterizedTriangles = 0;
	std::size_t candidateTests = 0, maskedSamples = 0, opaqueSamples = 0, depthWrites = 0;
	std::size_t depthBytes = 0, cacheDepthBytes = 0;
	uint64_t staticEpoch = 0, staticIdentity = 0, staticRevision = 0;
	uint64_t dynamicEpoch = 0, dynamicIdentity = 0, dynamicRevision = 0;
	uint64_t staticHash = 0, dynamicHash = 0, sceneHash = 0;
	double validationMilliseconds = 0, staticBuildMilliseconds = 0;
	double dynamicBuildMilliseconds = 0, buildMilliseconds = 0;
};

/** Build the existing directional atlas from shared masked geometry. The
 * projection contains the union of both complete layers, independent of the
 * camera/viewport. Pose changes within its bounds reuse the static depth and
 * overlay dynamic depth. Changed static content/configuration, or geometry
 * outside cached bounds, rebuilds the static projection. Publication is
 * transactional; invalid input, allocation or work-limit refusal clears both
 * the published field and split caches. ClearTownShadowMap does the same.
 * Geometry MapView.revision is a monotonic publication token, surviving Clear;
 * sceneHash stays the content hash in Stats. Full cache hits keep the token.
 * The legacy builder retains its existing content-hash revision contract. */
bool BuildTownShadowGeometryMap(const TownShadowGeometryLayer &staticGeometry,
    const TownShadowGeometryLayer &dynamicGeometry, const TownShadowConfig &config = {});
const TownShadowGeometryStats &GetTownShadowGeometryStats();

TownShadowReceiver PrepareTownShadowReceiver(float normalX, float normalHeight, float normalZ);

/** Return fractional occlusion: 0 is lit, 1 is fully in the geometry's shadow. */
float SampleTownShadow(float x, float height, float z);
float SampleTownShadow(float x, float height, float z, const TownShadowReceiver &receiver);
float SampleTownShadow(float x, float height, float z, float normalX, float normalHeight, float normalZ);

void ClearTownShadowMap();
const TownShadowStats &GetTownShadowStats();
TownShadowDirection GetTownShadowLightDirection();

} // namespace devilution
