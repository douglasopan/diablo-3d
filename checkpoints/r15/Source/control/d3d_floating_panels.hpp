#pragma once

#include <optional>
#include <span>
#include <vector>

#include "engine/rectangle.hpp"
#include "engine/surface.hpp"
#include "panels/ui_panels.hpp"

namespace devilution {

inline constexpr int D3dFloatingPanelHeaderHeight = 22;

/** Uniform visual scale. Native content is captured once and input is inverse-mapped. */
float GetD3dWindowScale(UiPanels panel);
Rectangle GetD3dFloatingPanelNativeBounds(UiPanels panel);
bool IsD3dFloatingPanelBodyPoint(Point point);
/** Visible topmost window at the physical pointer. bodyOnly never passes
 * through a foreground header to a window behind it. Inside the scoped native
 * pointer transform, the captured window identity remains authoritative. */
std::optional<UiPanels> D3dFloatingPanelAt(Point point, bool bodyOnly = false);
/** Per-panel functional hit test. Movable HUD windows require that panel to
 * own the topmost body hit; legacy callers retain their native rectangle. */
bool IsD3dPanelBodyPoint(UiPanels panel, Point point);
/** Bottom-to-top order. Drawing and pointer ownership must use the same order. */
std::span<const UiPanels> GetD3dFloatingPanelDrawOrder();
void AppendD3dFloatingPanelOcclusionRects(std::vector<Rectangle> &areas);
void FocusD3dFloatingPanel(UiPanels panel);
Point MapD3dWindowPointer(Point point);
/** True only while the shared mouse point belongs to a captured native panel.
 * Late physical overlays such as Clock must not test that native point. */
bool IsD3dWindowPointerTransformed();
/** Map a native point inside this window to a physical pixel that inverse
 * mapping resolves to that same native pixel. Unsupported/outside points and
 * the legacy HUD are left unchanged. */
Point D3dPanelPointerToVisual(UiPanels panel, Point nativePoint);
class ScopedD3dWindowPointer {
public: ScopedD3dWindowPointer(); ~ScopedD3dWindowPointer();
	ScopedD3dWindowPointer(const ScopedD3dWindowPointer &) = delete;
	ScopedD3dWindowPointer &operator=(const ScopedD3dWindowPointer &) = delete;
private: Point saved; bool owner=false;
};
Rectangle ResolveD3dFloatingPanelArea(UiPanels panel, Rectangle nativeArea);
void BeginD3dWindowDrag(UiPanels panel, Rectangle area, Point pointer);
bool UpdateD3dFloatingPanelDrag(Point pointer);
void EndD3dFloatingPanelDrag();
/** Restores the pre-gesture position/scale on focus loss, modal ownership or
 * viewport/DPI change. No profile write occurs until an accepted release. */
void CancelD3dFloatingPanelDrag();
/** Explicit current-profile reset, including the separately drawn minimap. */
void ResetD3dFloatingPanelLayout();
void UpdateD3dFloatingPanelAreas(Rectangle &left, Rectangle &right);
/** Body plus header; unsupported panels return an empty rectangle. */
Rectangle GetD3dFloatingPanelBounds(UiPanels panel);
/** Only visible, open native floating panels participate. */
bool IsD3dFloatingPanelPoint(Point point);
/** Consumes a visible header; only its X dispatches the native close action. */
bool HandleD3dFloatingPanelClose(Point point);
/** Starts one R1 backplate recipe including the header. Seal after native content. */
void DrawD3dFloatingPanelBody(const Surface &out, UiPanels panel);
void DrawD3dFloatingPanelHeader(const Surface &out, UiPanels panel);
/** Opaque functional field/slot/tab outline. No background or asset loading. */
void DrawD3dFloatingPanelFrame(const Surface &out, Rectangle rect, bool selected = false);

} // namespace devilution
