#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/render/town_lighting.hpp"

namespace devilution {

/** Projected raster coordinates and affine world coordinates; no perspective divide. */
struct TownGpuVertex {
	float x;
	float y;
	float depth;
	float u;
	float v;
	std::array<float, 3> world;
};

/** Borrowed inputs are uploaded during Submit, never retained by the backend. */
struct TownGpuTexture {
	uint64_t stableKey = 0;
	uint64_t revision = 0;
	int width = 0;
	int height = 0;
	std::span<const uint32_t> texelCodes;
	std::span<const uint8_t> opacity;
	/** Palette output at [texelCode * lightLevels + level]. */
	std::span<const uint8_t> lightLut;
	unsigned lightLevels = 1;
};

enum class TownGpuLighting : uint32_t { Unlit, Shadow, Directional, Interior };

struct TownGpuMaterial {
	TownGpuLighting lighting = TownGpuLighting::Unlit;
	bool repeat = false;
	bool transparentZero = false;
	bool preservePicking = false;
	bool receivesShadow = false;
	int shade = 0;
	/** Native Shadow/Unlit material: fill cutout texels with this opaque palette color. */
	int fallbackPaletteIndex = -1;
	int fallbackShade = 0;
	float diffuse = 1;
	float shadowBias = 0.025F;
	float shadowSlopeU = 0;
	float shadowSlopeV = 0;
	std::array<float, 3> normal { 0, 1, 0 };
	/** Interior point red irradiance is divided by these two positive values. */
	float interiorRedNormalization = 1;
	float interiorPointRange = 1;
	std::span<const TownPointLight> lights;
	const TownLightOccluder *room = nullptr;
};

/** Same light-space depth convention as town_shadow: larger is nearer the light. */
struct TownGpuShadow {
	uint64_t stableKey = 0;
	uint64_t revision = 0;
	int resolution = 0;
	std::span<const float> depth;
	std::array<float, 3> right { 1, 0, 0 };
	std::array<float, 3> up { 0, 0, 1 };
	std::array<float, 3> light { 0, 1, 0 };
	float minU = 0;
	float minV = 0;
	float texelU = 1;
	float texelV = 1;
	int pcfRadius = 1;
};

struct TownGpuFrame {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> indexed;
	/** Zero denotes no pick record; the caller owns the frame's ID-to-record table. */
	std::vector<uint32_t> pickIds;
	/** Unnormalized camera depth, infinity for untouched pixels. */
	std::vector<float> depth;
};

struct TownGpuStatus {
	bool available = false;
	bool frameSucceeded = false;
	bool warp = false;
	std::string adapter;
	uint32_t featureLevel = 0;
	std::string failure;
	size_t submittedTriangles = 0;
	size_t drawCalls = 0;
	size_t cachedTextures = 0;
	double frameMilliseconds = 0;
	double readbackMilliseconds = 0;
};

/** Hardware only by default. WARP fallback requires this explicit diagnostic flag. */
bool TownGpuBeginFrame(int width, int height, bool allowWarpForDiagnostics = false);
/** Call after Begin; an empty view disables directional shadows for this frame. */
bool TownGpuSetShadow(const TownGpuShadow &shadow);
bool TownGpuSubmitProjectedTriangle(const std::array<TownGpuVertex, 3> &vertices,
    const TownGpuTexture &texture, const TownGpuMaterial &material, uint32_t pickId);
/** Synchronous readback publishes all three outputs together, or clears output on failure. */
bool TownGpuEndFrame(TownGpuFrame &output);
/** Invalidate device, frame, cached uploads and borrowed outputs before a scene reset. */
void ResetTownGpuResources();
const TownGpuStatus &GetTownGpuStatus();

} // namespace devilution
