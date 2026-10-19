#pragma once

#include "engine/point.hpp"
#include "engine/render/surface_region_view_preflight.hpp"

namespace devilution {

// Read-only native UI values, captured by the Host. Saved panel flags must be
// supplied by the prospective context, never inferred from the old carrier.
struct SurfaceRegionNativeViewport {
	int width = 0, screenHeight = 0, viewportHeight = 0;
	int mainPanelHeight = 0, sidePanelWidth = 0;
	bool zoomed = false, panelsCanCover = false;
	bool leftPanelOpen = false, rightPanelOpen = false;
};

struct SurfaceRegionProspectiveViewRequest {
	surface::RegionId id;
	// Supported prospective ISO VIEW tile, not an old live player/rig position.
	TownCameraPoint localAnchor;
	Point playerTile;
	Point nativeViewPosition;
	bool walking = false;
	SurfaceRegionNativeViewport viewport;
};

struct SurfaceRegionProspectiveView {
	SurfaceRegionViewPreflight view;
	// Standing center is exact when the view is the player tile. Otherwise a
	// saved walking camera offset is bounded by one native cell on each axis.
	// Expand requiredWorldBounds ONCE by this amount; do not also add it to
	// maximumCameraOffsetMetres. Actual rendering must still check coverage.
	double walkingWorldPaddingNative = 0;
	bool exactNativeCenter = false;
	int tileOffsetX = 0, tileOffsetY = 0;
	int tileShiftX = 0, tileShiftZ = 0;
	int firstOffsetX = 0, firstOffsetY = 0;
	int firstTileShiftX = 0, firstTileShiftZ = 0;
	int tileRows = 0, tileColumns = 0;
};

// Pure port of the native standing viewport arithmetic. No mutable globals,
// Player construction, CEL access, scene, draw, RNG, resource or World lookup.
// Perspective TP/FP callers keep their actual half-width/height producer.
bool BuildSurfaceRegionProspectiveView(const SurfaceRegionProspectiveViewRequest &request,
    SurfaceRegionProspectiveView &output, std::string &failure) noexcept;

} // namespace devilution
