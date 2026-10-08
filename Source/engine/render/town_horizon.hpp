#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/render/town_camera.hpp"

namespace devilution {

/** Pure visual geometry; no direct renderer, picking or simulation dependency.
 * Bounds must enclose the actual native ground geometry, including its skirts;
 * they must not be inferred from playable tile indices alone. */
struct TownHorizonBounds {
	float minX = 0;
	float minZ = 0;
	float maxX = 112;
	float maxZ = 112;
};

/** Unlit provisional albedo in linear RGB, never sRGB or palette indices. */
struct TownHorizonColor {
	float red = 0;
	float green = 0;
	float blue = 0;
};

enum class TownHorizonLayer : uint8_t { Apron, Ridge, FarRidge };

struct TownHorizonTriangle {
	std::array<TownCameraPoint, 3> vertices;
	TownHorizonColor color;
	TownHorizonLayer layer = TownHorizonLayer::Apron;
};

/** Synthetic fixture defaults, not calibrated town settings. All distances and
 * heights are native tile units. Segments are clamped to [16, 256], then rounded
 * down to a multiple of four so every rectangular corner is represented.
 * visualSeed is a local hash key; construction never calls the game's RNG. */
struct TownHorizonConfig {
	TownHorizonBounds bounds;
	size_t segments = 128;
	float apronDistance = 18;
	float ridgeDistance = 60;
	float farDistance = 120;
	float apronHeight = 0.7F;
	float ridgeHeight = 8;
	float farRidgeHeight = 14;
	uint32_t visualSeed = 0x54726973U;
};

enum class TownHorizonConfigError : uint8_t {
	None,
	NonFinite,
	InvalidBounds,
	InvalidDistances,
	InvalidHeights,
	ExtentOutOfRange,
};

struct TownHorizonStatistics {
	size_t segments = 0;
	size_t triangles = 0;
	size_t triangleDataBytes = 0;
	size_t reservedTriangleBytes = 0;
};

/** One owned allocation for triangles; statistics exclude allocator overhead
 * and this small container. Invalid configuration returns no allocated geometry. */
struct TownHorizonMesh {
	std::vector<TownHorizonTriangle> triangles;
	TownHorizonStatistics statistics;
	TownHorizonConfigError error = TownHorizonConfigError::None;
	bool valid() const { return error == TownHorizonConfigError::None && !triangles.empty(); }
};

/** Validates every floating value before allocating. To keep float geometry
 * meaningful, expanded X/Z must remain within +/-2^20 tile units, width/depth
 * must be >=1/16, and each radial band must be >=1/16 tile wide. Heights must
 * be nonnegative and no higher than their respective band is wide. Geometry
 * that loses positive area at float precision is also rejected before allocation. */
TownHorizonConfigError ValidateTownHorizonConfig(const TownHorizonConfig &config);

/** Five closed, upward-facing terrain bands: apron, front/back of the near
 * ridge, front/back of the far ridge. There are exactly 10*segments triangles
 * (1280 by default, 2560 maximum), all outside or on the ground AABB boundary.
 * The inner apron edge is height zero; both ridges have real rear slopes.
 * Geometry and colors are fixed in world space, independent of the camera.
 * It contains no gameplay tile, entity, collision or selection identifier.
 * A future renderer must depth-test it and publish an invalid picking record
 * for a visible horizon fragment, rather than retaining hidden native ground. */
TownHorizonMesh BuildTownHorizon(const TownHorizonConfig &config);

/** Finite positive view depth only. This is a geometry shader helper, not a
 * translucent sky overlay: call after normal depth/opacity visibility tests.
 * Invalid parameters disable fog. Zero density selects plain smoothstep. */
struct TownHorizonFogConfig {
	float startDepth = 45;
	float endDepth = 220;
	float density = 2.5F;
	TownHorizonColor color { 0.16F, 0.20F, 0.24F };
};

bool IsValidTownHorizonFogConfig(const TownHorizonFogConfig &config);
float TownHorizonFogAmount(float viewDepth, const TownHorizonFogConfig &config);
/** Blends linear surface RGB using only that surface's own view depth. Finite
 * RGB channels are clamped to [0,1]; nonfinite channels become zero. */
TownHorizonColor ApplyTownHorizonFog(TownHorizonColor surface, float viewDepth,
    const TownHorizonFogConfig &config);

constexpr size_t TownHorizonFogLevels = 64;
using TownHorizonPalette = std::array<TownHorizonColor, 256>;
using TownHorizonFogPalette = std::array<std::array<uint8_t, 256>, TownHorizonFogLevels>;

/** Build once per palette/fog-color change, never per pixel. The caller supplies
 * already decoded linear RGB in [0,1]. Rows address fog amount level/63; columns
 * address the source palette index. Row zero is always index identity, including
 * duplicated colors. Row 63 selects the nearest fog color for every source.
 * Other rows blend RGB before searching the palette by squared RGB distance,
 * choosing the first palette index on an exact tie. No palette-index blending.
 * Any nonfinite/out-of-range palette or fog channel disables this LUT by
 * returning identity in every row. Fixed storage is 16 KiB; no heap allocation.
 * The lookup transforms only color, never depth or selection identifiers. */
TownHorizonFogPalette BuildTownHorizonFogPalette(const TownHorizonPalette &paletteLinear,
    TownHorizonColor fogColor);

} // namespace devilution
