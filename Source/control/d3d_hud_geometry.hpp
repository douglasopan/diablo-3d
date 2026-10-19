#pragma once

#include <array>
#include <cstdint>

#include "engine/rectangle.hpp"
#include "utils/display.h"

namespace devilution {

enum class D3dHudGeometryMode : uint8_t {
	SmallAuthored,
	ExplicitAuthoredV1,
	Compact,
};

// Glyph scales convert pixels of the native GameFont12 buffer into logical
// pixels. Design scales convert this mode's authored/table pixels into logical
// pixels. Source-buffer line height/insets must not be scaled a second time.
struct D3dHudMetrics {
	float designScaleX = 1, designScaleY = 1;
	float infoGlyphScaleX = 1, infoGlyphScaleY = 1;
	float labelGlyphScaleX = 1, labelGlyphScaleY = 1;
	float keyGlyphScaleX = 1, keyGlyphScaleY = 1;
	float valueGlyphScaleX = 1, valueGlyphScaleY = 1;
	int infoLineHeight = 12;
	int infoInsetX = 0, infoInsetY = 0;
	float infoPhysicalGlyphHeight = 12;
	float infoPhysicalLinePitch = 12;
	float infoPhysicalInset = 0;
	float uiScale = 1;
};

// Everything affecting an edge is part of the cache key, including failed
// renderer queries. Output-space edges are inverted once; input stays logical.
struct D3dHudGeometryContext {
	SDL_Renderer *renderer = nullptr;
	int logicalWidth = 1, logicalHeight = 1;
	int outputWidth = 1, outputHeight = 1;
	int rendererLogicalWidth = 0, rendererLogicalHeight = 0;
	int rawViewportX = 0, rawViewportY = 0, rawViewportWidth = 1, rawViewportHeight = 1;
	float outputScaleX = 1, outputScaleY = 1;
	float outputOriginX = 0, outputOriginY = 0;
	float viewportWidth = 1, viewportHeight = 1;
	uint64_t layoutGeneration = 0;
	bool hasHudOverride = false;
	bool integerScale = false;
	bool valid = false;

	bool operator==(const D3dHudGeometryContext &) const = default;
};

struct D3dHudGeometry {
	D3dHudGeometryMode mode = D3dHudGeometryMode::SmallAuthored;
	std::array<Rectangle, 13> elements;
	std::array<Rectangle, 8> beltSlots;
	Rectangle frame;
	D3dHudMetrics metrics;
	D3dHudGeometryContext context;
};

// Borrowed, nestable binding for an offscreen renderer. No global SDL renderer,
// viewport, scale or world-input conversion is changed. The renderer must live
// longer than this guard. Bind before the first draw, hover or layout query.
class ScopedD3dHudGeometryRenderer {
public:
	explicit ScopedD3dHudGeometryRenderer(SDL_Renderer *target);
	~ScopedD3dHudGeometryRenderer();
	ScopedD3dHudGeometryRenderer(const ScopedD3dHudGeometryRenderer &) = delete;
	ScopedD3dHudGeometryRenderer &operator=(const ScopedD3dHudGeometryRenderer &) = delete;

private:
	SDL_Renderer *previousRenderer_;
	bool previousBinding_;
};

D3dHudGeometryContext CaptureD3dHudGeometryContext(SDL_Renderer *target);
D3dHudGeometryContext GetD3dHudGeometryContext();
D3dHudGeometry ResolveD3dHudGeometry(const D3dHudGeometryContext &context);
const D3dHudGeometry &GetD3dHudGeometry();
const D3dHudMetrics &GetD3dHudMetrics();
int GetD3dHudDesignPixelsX(int pixels);
int GetD3dHudDesignPixelsY(int pixels);

} // namespace devilution
