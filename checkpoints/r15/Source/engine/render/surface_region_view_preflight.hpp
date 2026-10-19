#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "engine/render/town_camera.hpp"
#include "levels/surface_region_halo.hpp"

namespace devilution {
struct SurfaceWorldSnapshot;
struct TownSurroundingsVisualScene;
struct TownBorderFrozenVisualOwner;

// Explicit immutable sources prepared by the host before native cleanup.
// Nature is ADD-only; the base ENV is shared with the pure prop extraction.
struct SurfaceRegionVisualSourceOwners {
 std::shared_ptr<const TownSurroundingsVisualScene> environment;
 std::shared_ptr<const TownBorderFrozenVisualOwner> nature;
};

// The host supplies the actual prospective native viewport/layout and arrival
// on its already-built geometry. This function never observes a map carrier,
// moves an actor, changes camera preferences, generates terrain or draws.
struct SurfaceRegionViewPreflight {
	surface::RegionId id;
	TownCameraPoint localAnchor;
	int width = 0, height = 0;
	float nativeCenterX = 0, nativeCenterY = 0, nativeZoom = 1;
};

bool PrepareTownViewSurfaceHaloRequest(const surface::RegionGeometry &geometry,
    const SurfaceRegionViewPreflight &view, surface::SurfaceHaloRequest &output, std::string &failure);

// All eight six-plane frustum corners, including asymmetric viewport centers
// and orthographic near/far. X/Z are absolute native units, never Far-as-radius.
bool GetTownViewSurfaceFrustumWorldBounds(const TownCameraFrame &frame, surface::Tile worldOffset,
    surface::SurfaceHaloWorldBounds &output) noexcept;

enum class SurfaceRegionViewContextStatus { Inactive, Preparing, Ready, RepreflightRequired, Refused };
SurfaceRegionViewContextStatus GetTownViewSurfaceContextStatus() noexcept;

// Host main/load boundary polls before the next draw. It may prepare a new
// immutable visual snapshot/epoch without resetting campaign/visited/storage.
// The renderer only requests; it never invokes Build or publishes a World.
// A request stays pending until an admitted publication covers the real view.
bool GetTownViewSurfaceHaloRepreflightRequest(surface::SurfaceHaloRequest &output);

struct SurfaceRegionVisualResourceStats {
	size_t sourceBytes = 0, derivedBytes = 0, haloBytes = 0;
	size_t retainedBytes = 0, preflightPeakBytes = 0;
	// Tracked owned payload only; source codec temporaries/allocator/RSS are
	// not measured here and require the real cold-load gate.
	bool sourceDecodePeakMeasured = false;
	size_t haloSourceTriangles = 0, haloDrawTriangles = 0, maximumWorldTriangles = 0;
	uint64_t epoch = 0, resourceGeneration = 0;
};
// Render-thread load/resource boundary, before publication. Draft identity is
// validated directly; it need not be current/published. Commit is all-or-none;
// the last published owner remains available if a later draft is cancelled.
// The two-argument overload only reuses an already prepared exact context.
// A fresh epoch requires these explicit owned sources; no live asset lookup.
bool PrepareTownViewSurfaceVisualResources(const SurfaceWorldSnapshot &draft, std::string &failure);
bool PrepareTownViewSurfaceVisualResources(const SurfaceWorldSnapshot &draft,
    const SurfaceRegionVisualSourceOwners &sources, std::string &failure);
SurfaceRegionVisualResourceStats GetTownViewSurfaceVisualResourceStats() noexcept;

} // namespace devilution
