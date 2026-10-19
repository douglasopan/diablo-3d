#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/render/town_surroundings_visual.hpp"
#include "levels/surface_region.hpp"
#include "engine/render/surface_region_prop_bindings.hpp"

namespace devilution {

struct SurfaceRegionPropRange {
	std::string_view featureId;
	double supportAnchorWorldX = 0, supportAnchorWorldZ = 0;
	double sourceSupportHeightMetres = 0, triangleSupportHeightMetres = 0;
	double placementDeltaYNative = 0;
	// Stable provenance within the exact authenticated D3DENV1 asset. Neither
	// value is a native actor/object index, collision mask or RegionId.
	uint32_t sourceCellIndex = 0;
	uint32_t sourceFirstTriangle = 0;
	uint32_t firstTriangle = 0;
	uint32_t triangleCount = 0;
	// Original visual painter/sort label, not a measured physical support point.
	int32_t anchorWorldX = 0;
	int32_t anchorWorldZ = 0;
	// Conservative full visual XZ bounds (including crowns/roof overhang).
	// They authorize culling only; never derive SOL or actor support from them.
	float minWorldX = 0, minWorldZ = 0;
	float maxWorldX = 0, maxWorldZ = 0;
};

struct SurfaceRegionPropSet {
	std::string identity; // Exact immutable source asset SHA.
	uint32_t placementRevision = SurfacePropPlacementRevision;
	// Owned copies of ONLY kind1 ranges. Global X/Z, local shape and linear
	// RGB stay exact; Y receives the explicit uniform authored support delta
	// from SurfacePropBindings. No H evaluation or XZ relocation during draw.
	std::vector<TownSurroundingsTriangle> triangles;
	std::vector<SurfaceRegionPropRange> ranges;
};

struct SurfaceRegionPropCache {
	uint64_t resourceEpoch = 0;
	std::shared_ptr<const SurfaceRegionPropSet> published;
};

struct SurfaceRegionPropFrame {
	std::shared_ptr<const SurfaceRegionPropSet> source;
	surface::RegionId regionId;
	surface::Tile worldOffset;
	// Whole source ranges intersecting offset+[-1,111] in XZ. Selection may
	// retain overhangs; it never clips, resamples or splits an adjusted prop.
	std::vector<uint32_t> rangeIndices;
};

void ResetSurfaceRegionProps(SurfaceRegionPropCache &cache) noexcept;

// Render-thread only. Reuses the real29 authenticated loader, not another
// parser or asset path. Failure clears output/publication; an older frame may
// retain its owned immutable set until its own epoch/freshness checks reject it.
// RegionId(0,0) is preserved Town and is refused. Geometry/simulation/props'
// collision readiness are deliberately outside this visual-only interface.
bool PrepareSurfaceRegionProps(SurfaceRegionPropCache &cache,
    uint64_t resourceEpoch, surface::RegionId region,
    SurfaceRegionPropFrame &output, std::string &failure);

// Explicit authenticated owner input; no asset lookup or renderer-cache access.
// Source bytes must have passed DecodeTownSurroundingsVisualSceneOwned. The
// resulting prop set owns copies and survives release of this source owner.
bool PrepareSurfaceRegionPropsFromOwned(SurfaceRegionPropCache &cache,
    uint64_t resourceEpoch, surface::RegionId region,
    std::shared_ptr<const TownSurroundingsVisualScene> source,
    SurfaceRegionPropFrame &output, std::string &failure);

} // namespace devilution
