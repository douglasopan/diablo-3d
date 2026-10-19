#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "engine/render/town_ground_material.hpp"
#include "levels/surface_region_geometry.hpp"

namespace devilution {

// Bounded local coordinates for the immutable visual halo. Gameplay ownership
// stays in the active112x112 region; this value only admits texture samples.
// |world - activeOffset| <= 20000 + 96*108 = 30368 native units.
// Mesh preflight still owns coverage, memory and finite world admission.
inline constexpr float SurfaceRegionMaterialMaxLocalCoordinate = static_cast<float>(
	surface::MaximumSurfaceWorldNative + surface::MaximumRegionCoordinate * surface::InteriorSize);

// Owned references survive a later palette/resource publication. The frame
// captures this set once; it never reads a changing cache during rasterization.
struct SurfaceRegionGroundMaterials {
	std::shared_ptr<const PlayerActorMaterial> grass;
	std::shared_ptr<const PlayerActorMaterial> soil;
	uint64_t revision = 0;
};

// Render-thread owner, reset at the host resource/SurfaceWorld epoch. The
// existing factories own decoded RGB/LUTs and enforce their source hashes.
struct SurfaceRegionGroundMaterialCache {
	uint64_t resourceEpoch = 0;
	TownGroundMaterialCache grass;
	TownGroundMaterialCache soil;
	SurfaceRegionGroundMaterials published;
};

void ResetSurfaceRegionGroundMaterials(SurfaceRegionGroundMaterialCache &cache);

// Requires Tristram's already admitted ground-material module. Regional cells
// use their explicit CellKind, never Town's native-piece/color classifier or
// measured64x32 road coverage. Triangle coverage/depth belongs to the renderer.
// On any failure output is empty: no partial or stale palette publication.
bool PrepareSurfaceRegionGroundMaterials(SurfaceRegionGroundMaterialCache &cache,
    uint64_t resourceEpoch, std::span<const uint32_t> palette,
    const TownLightingConfig &light, SurfaceRegionGroundMaterials &output,
    std::string &failure);

// Same per-fragment phase as the existing GPU descriptor: local*scale+offset
// in cycles. No divide/multiply round trip through a large native coordinate.
// The ordinary Town sampler remains unchanged for its zero-offset path.
bool SampleSurfaceRegionGroundMaterial(const PlayerActorMaterial &material,
    float localNativeX, float localNativeZ, float offsetCyclesU,
    float offsetCyclesV, uint32_t &code);

} // namespace devilution
