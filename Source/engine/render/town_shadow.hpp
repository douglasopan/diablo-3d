#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/render/town_scene.hpp"

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

TownShadowReceiver PrepareTownShadowReceiver(float normalX, float normalHeight, float normalZ);

/** Return fractional occlusion: 0 is lit, 1 is fully in the geometry's shadow. */
float SampleTownShadow(float x, float height, float z);
float SampleTownShadow(float x, float height, float z, const TownShadowReceiver &receiver);
float SampleTownShadow(float x, float height, float z, float normalX, float normalHeight, float normalZ);

void ClearTownShadowMap();
const TownShadowStats &GetTownShadowStats();
TownShadowDirection GetTownShadowLightDirection();

} // namespace devilution
