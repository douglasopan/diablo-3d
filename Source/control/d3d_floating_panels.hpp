#pragma once

#include "engine/rectangle.hpp"
#include "engine/surface.hpp"
#include "panels/ui_panels.hpp"

namespace devilution {

inline constexpr int D3dFloatingPanelHeaderHeight = 22;

/** Changes only the shared body rectangles; content and input stay at native scale. */
void UpdateD3dFloatingPanelAreas(Rectangle &left, Rectangle &right);
/** Body plus header; unsupported panels return an empty rectangle. */
Rectangle GetD3dFloatingPanelBounds(UiPanels panel);
/** Only the four visible, open native panels participate. */
bool IsD3dFloatingPanelPoint(Point point);
/** Consumes a visible header; only its X dispatches the native close action. */
bool HandleD3dFloatingPanelClose(Point point);
/** Starts one R1 backplate recipe including the header. Seal after native content. */
void DrawD3dFloatingPanelBody(const Surface &out, UiPanels panel);
void DrawD3dFloatingPanelHeader(const Surface &out, UiPanels panel);
/** Opaque functional field/slot/tab outline. No background or asset loading. */
void DrawD3dFloatingPanelFrame(const Surface &out, Rectangle rect, bool selected = false);

} // namespace devilution
