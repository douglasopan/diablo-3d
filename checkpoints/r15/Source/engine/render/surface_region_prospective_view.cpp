#include "engine/render/surface_region_prospective_view.hpp"

#include <cmath>

#include "engine/direction.hpp"
#include "engine/displacement.hpp"

namespace devilution {

bool BuildSurfaceRegionProspectiveView(const SurfaceRegionProspectiveViewRequest &request,
    SurfaceRegionProspectiveView &output, std::string &failure) noexcept
{
	output = {};
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
		failure.clear();
		const auto &v = request.viewport;
		const auto tileValid = [](Point p) { return p.x >= 0 && p.x < surface::StorageSize && p.y >= 0 && p.y < surface::StorageSize; };
		if (!surface::ValidRegionId(request.id) || request.id.IsTown()
		    || v.width < 2 || v.width > 16384 || v.screenHeight < 2 || v.screenHeight > 16384
		    || v.viewportHeight < 1 || v.viewportHeight > v.screenHeight
		    || v.mainPanelHeight < 0 || v.mainPanelHeight > v.screenHeight
		    || v.sidePanelWidth < 0 || v.sidePanelWidth > v.width
		    || !tileValid(request.playerTile) || !tileValid(request.nativeViewPosition)
		    || !std::isfinite(request.localAnchor.x) || !std::isfinite(request.localAnchor.height)
		    || !std::isfinite(request.localAnchor.z)
		    || request.localAnchor.x != request.nativeViewPosition.x
		    || request.localAnchor.z != request.nativeViewPosition.y) {
			failure = "surface-prospective-view-invalid-input";
			return false;
		}
		// Same integer arithmetic/order as native CalcViewportGeometry.
		constexpr int TileWidth = 64, TileHeight = 32;
		const int zoomFactor = v.zoomed ? 2 : 1;
		const int screenWidth = v.width / zoomFactor;
		const int screenHeight = v.screenHeight / zoomFactor;
		const int panelHeight = v.mainPanelHeight / zoomFactor;
		Point playerPosition { screenWidth / 2, (screenHeight - panelHeight) / 2 };
		if (v.zoomed)
			playerPosition.y += TileHeight / 4;
		const int tilesToTop = (playerPosition.y + TileHeight - 1) / TileHeight;
		const int tilesToLeft = (playerPosition.x + TileWidth - 1) / TileWidth;
		Point startPosition = playerPosition - Displacement { tilesToLeft * TileWidth, tilesToTop * TileHeight };
		Displacement shift { 0, 0 };
		shift += Displacement(Direction::North) * tilesToTop;
		shift += Displacement(Direction::West) * tilesToLeft;
		if (tilesToLeft * TileWidth >= playerPosition.x) {
			startPosition += Displacement { TileWidth / 2, -TileHeight / 2 };
			shift += Displacement(Direction::NorthEast);
		} else if (tilesToTop * TileHeight < playerPosition.y) {
			startPosition += Displacement { 0, -TileHeight };
			shift += Displacement(Direction::North);
		}
		Displacement offset { startPosition.x - TileWidth / 2, startPosition.y + TileHeight / 2 - 1 };
		SurfaceRegionProspectiveView candidate;
		candidate.tileOffsetX = offset.deltaX;
		candidate.tileOffsetY = offset.deltaY;
		candidate.tileShiftX = shift.deltaX;
		candidate.tileShiftZ = shift.deltaY;
		const Point renderStart = startPosition - Displacement { TileWidth / 2, TileHeight / 2 };
		candidate.tileRows = (v.viewportHeight / zoomFactor - renderStart.y + TileHeight / 2 - 1) / (TileHeight / 2);
		candidate.tileColumns = (screenWidth - renderStart.x + TileWidth - 1) / TileWidth;
		// CalcFirstTilePosition standing path, with explicit saved panel flags.
		if (v.panelsCanCover && (v.leftPanelOpen || v.rightPanelOpen)) {
			const int multiplier = v.zoomed ? 1 : 2;
			shift += Displacement(Direction::East) * multiplier;
			offset.deltaX += -TileWidth * multiplier / 2 / 2;
			if (v.leftPanelOpen && !v.zoomed)
				offset.deltaX += v.sidePanelWidth;
		}
		candidate.firstOffsetX = offset.deltaX;
		candidate.firstOffsetY = offset.deltaY;
		candidate.firstTileShiftX = shift.deltaX;
		candidate.firstTileShiftZ = shift.deltaY;
		// GetScreenPosition(ViewPosition): the common absolute tile cancels.
		Point anchor {};
		anchor += shift.worldToScreen();
		anchor += offset;
		anchor += Displacement { 32, 0 };
		candidate.view.id = request.id;
		candidate.view.localAnchor = request.localAnchor;
		candidate.view.width = v.width;
		candidate.view.height = v.viewportHeight;
		candidate.view.nativeCenterX = static_cast<float>(anchor.x * zoomFactor);
		candidate.view.nativeCenterY = static_cast<float>(anchor.y * zoomFactor);
		candidate.view.nativeZoom = static_cast<float>(zoomFactor);
		if (v.zoomed && v.panelsCanCover && v.leftPanelOpen)
			candidate.view.nativeCenterX += v.sidePanelWidth;
		candidate.exactNativeCenter = !request.walking || request.nativeViewPosition == request.playerTile;
		// Even with exact center, the interpolated camera target can move by a
		// cell. Native MovingOffset[] spans at most one cell on each X/Z axis.
		candidate.walkingWorldPaddingNative = request.walking ? 1 : 0;
		output = candidate;
		return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (...) {
		failure.clear();
		return false;
	}
#endif
}

} // namespace devilution
