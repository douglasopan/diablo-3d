#include "control/d3d_hud_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <fstream>
#include <sstream>
#include <string>

#include "automap.h"
#include "control/d3d_hud_layout.hpp"
#include "engine/render/d3d_ui_layout.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {

SDL_Renderer *BoundRenderer = nullptr;
bool HasRendererBinding = false;
std::optional<D3dHudGeometry> CachedGeometry;

struct SavedHudRect { int x, y, width, height; };
std::array<std::optional<SavedHudRect>, D3dHudEditableElementCount> HudOverrides;
std::string HudProfile;
bool HudProfileLoaded = false;
uint64_t HudCustomizationGeneration = 0;
struct HudGesture { size_t element; Rectangle original; Point start; uint8_t edges; D3dHudGeometryContext viewport; std::optional<SavedHudRect> previous; };
std::optional<HudGesture> HudDrag;
constexpr int HudNormalizedUnits = 10000;
constexpr std::array<const char *, D3dHudEditableElementCount> HudElementNames {
	"Health", "Mana", "Belt", "Spell", "Info", "Character", "Inventory", "Spells",
	"Quests", "Map", "Menu", "Chat", "Friendly", "Experience", "Level", "Durability", "Clock", "Minimap"
};

void LoadHudOverrides()
{
	const std::string profile = paths::PrefPath();
	if (HudProfileLoaded && profile == HudProfile) return;
	HudProfileLoaded = true; HudProfile = profile; HudOverrides = {}; HudDrag.reset(); ++HudCustomizationGeneration;
	uintmax_t fileSize = 0;
	if (!GetFileSize((profile + "hud-elements.layout").c_str(), &fileSize) || fileSize > 8192) return;
	std::ifstream input(profile + "hud-elements.layout", std::ios::binary);
	if (!input) return;
	std::string header;
	if (!std::getline(input, header)) return;
	if (!header.empty() && header.back() == '\r') header.pop_back();
	if (header != "D3DHUDLAYOUT1") return;
	std::array<std::optional<SavedHudRect>, D3dHudEditableElementCount> candidate;
	std::string line; size_t bytes = header.size(), rows = 0;
	while (std::getline(input, line)) {
		bytes += line.size() + 1;
		if (bytes > 8192 || ++rows > D3dHudEditableElementCount) return;
		std::istringstream row(line); std::string name, tail; SavedHudRect rect;
		if (!(row >> name >> rect.x >> rect.y >> rect.width >> rect.height) || row >> tail) return;
		const auto found = std::find(HudElementNames.begin(), HudElementNames.end(), name);
		if (found == HudElementNames.end()) return;
		const size_t slot = static_cast<size_t>(found - HudElementNames.begin());
		if (candidate[slot] || rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0
		    || rect.x > HudNormalizedUnits || rect.y > HudNormalizedUnits
		    || rect.width > HudNormalizedUnits || rect.height > HudNormalizedUnits
		    || rect.x + rect.width > HudNormalizedUnits || rect.y + rect.height > HudNormalizedUnits) return;
		candidate[slot] = rect;
	}
	if (input.bad()) return;
	HudOverrides = candidate; // Whole-file acceptance; malformed files stay untouched.
}

void SaveHudOverrides()
{
	if (!HudProfileLoaded) return;
	std::ostringstream serialized;
	serialized << "D3DHUDLAYOUT1\n";
	for (size_t i = 0; i < HudOverrides.size(); ++i) if (HudOverrides[i]) {
		const auto &rect = *HudOverrides[i];
		serialized << HudElementNames[i] << ' ' << rect.x << ' ' << rect.y << ' ' << rect.width << ' ' << rect.height << '\n';
	}
	const std::string path = HudProfile + "hud-elements.layout", temporary = path + ".tmp";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	output << serialized.str(); output.close();
	if (!output) return;
	if (FileExists(path.c_str())) CopyFileOverwrite(path.c_str(), (path + ".bak").c_str());
	CopyFileOverwrite(temporary.c_str(), path.c_str());
	RemoveFile(temporary.c_str());
}

Rectangle ClampHudElement(size_t id, Rectangle rect, const D3dHudGeometryContext &context)
{
	constexpr std::array<Size, D3dHudEditableElementCount> minimum {{
		{64,82},{64,82},{192,24},{32,32},{180,64},{50,20},{50,20},{50,20},
		{50,20},{50,20},{50,20},{32,24},{32,24},{160,9},{100,40},{100,24},{64,12},{160,72}
	}};
	auto size = minimum[std::min(id, minimum.size() - 1)];
	if (id == 4) {
		const auto metrics = ResolveD3dHudGeometry(context).metrics;
		size.height = std::max(size.height, static_cast<int>(std::ceil(5 * metrics.infoPhysicalLinePitch + 2 * metrics.infoPhysicalInset)));
	}
	const int logicalMinWidth = id == D3dHudMinimapElement ? 160 : 1;
	const int logicalMinHeight = id == D3dHudMinimapElement ? 72 : 1;
	const int minWidth = std::min(context.logicalWidth, std::max(logicalMinWidth, static_cast<int>(std::ceil(size.width / std::max(0.001F, context.outputScaleX)))));
	const int minHeight = std::min(context.logicalHeight, std::max(logicalMinHeight, static_cast<int>(std::ceil(size.height / std::max(0.001F, context.outputScaleY)))));
	const Size limits { std::max(1, context.logicalWidth), std::max(1, context.logicalHeight) };
	rect.size.width = std::clamp(rect.size.width, std::max(1,minWidth), limits.width);
	rect.size.height = std::clamp(rect.size.height, std::max(1,minHeight), limits.height);
	rect.position.x = std::clamp(rect.position.x, 0, limits.width - rect.size.width);
	rect.position.y = std::clamp(rect.position.y, 0, limits.height - rect.size.height);
	return rect;
}

Rectangle ApplyHudOverride(size_t id, Rectangle standard, const D3dHudGeometryContext &context)
{
	if (id >= HudOverrides.size() || !HudOverrides[id]) return standard;
	const auto &saved = *HudOverrides[id];
	if (id == D3dHudClockElement || id == D3dHudMinimapElement) {
		const Point position { saved.x * context.logicalWidth / HudNormalizedUnits, saved.y * context.logicalHeight / HudNormalizedUnits };
		return ClampHudElement(id, { position,
			{ (saved.x + saved.width) * context.logicalWidth / HudNormalizedUnits - position.x,
			  (saved.y + saved.height) * context.logicalHeight / HudNormalizedUnits - position.y } }, context);
	}
	return ClampHudElement(id, { { saved.x * context.logicalWidth / HudNormalizedUnits, saved.y * context.logicalHeight / HudNormalizedUnits },
		{ saved.width * context.logicalWidth / HudNormalizedUnits, saved.height * context.logicalHeight / HudNormalizedUnits } }, context);
}

void StoreHudOverride(size_t id, Rectangle rect, const D3dHudGeometryContext &context)
{
	rect = ClampHudElement(id, rect, context);
	if (id == D3dHudClockElement || id == D3dHudMinimapElement) {
		// New auxiliary records store normalized edge differences. Ceil maps each
		// logical pixel back exactly and keeps the opposite resize edge fixed.
		const auto edge = [](int pixel, int extent) {
			return std::clamp(static_cast<int>((int64_t(pixel) * HudNormalizedUnits + std::max(1, extent) - 1) / std::max(1, extent)), 0, HudNormalizedUnits);
		};
		const int left = edge(rect.position.x, context.logicalWidth), top = edge(rect.position.y, context.logicalHeight);
		HudOverrides[id] = SavedHudRect { left, top,
			std::max(1, edge(rect.position.x + rect.size.width, context.logicalWidth) - left),
			std::max(1, edge(rect.position.y + rect.size.height, context.logicalHeight) - top) };
		++HudCustomizationGeneration;
		return;
	}
	const auto units = [](int pixel, int extent) { return std::clamp(pixel * HudNormalizedUnits / std::max(1, extent), 0, HudNormalizedUnits); };
	HudOverrides[id] = SavedHudRect { units(rect.position.x, context.logicalWidth), units(rect.position.y, context.logicalHeight),
		std::max(1, units(rect.size.width, context.logicalWidth)), std::max(1, units(rect.size.height, context.logicalHeight)) };
	++HudCustomizationGeneration;
}

// Physical FullHD pixels, bottom-centred in the renderer's content viewport.
// The order is the existing 13-node v1 contract, never a new control/preset.
struct PhysicalRect {
	float x, y, width, height;
};
constexpr std::array<PhysicalRect, 13> CompactEntries {{
	{ -324, -186, 108, 139 }, { 216, -186, 108, 139 },
	{ -192, -196, 384, 48 }, { 152, -108, 48, 48 },
	{ -200, -140, 344, 112 },
	{ -440, -196, 100, 48 }, { 340, -196, 100, 48 },
	{ 340, -140, 100, 48 }, { -440, -140, 100, 48 },
	{ -440, -84, 100, 48 }, { 340, -84, 100, 48 },
	{ -294, -260, 48, 48 }, { 246, -260, 48, 48 },
}};

Rectangle Unite(Rectangle a, Rectangle b)
{
	const int right = std::max(a.position.x + a.size.width, b.position.x + b.size.width);
	const int bottom = std::max(a.position.y + a.size.height, b.position.y + b.size.height);
	a.position.x = std::min(a.position.x, b.position.x);
	a.position.y = std::min(a.position.y, b.position.y);
	a.size = { right - a.position.x, bottom - a.position.y };
	return a;
}

Rectangle AuthoredFrame(const D3dHudGeometry &geometry)
{
	Rectangle frame = geometry.elements[2];
	for (size_t index : { size_t { 3 }, size_t { 4 }, size_t { 5 }, size_t { 6 }, size_t { 7 }, size_t { 8 }, size_t { 9 }, size_t { 10 } })
		frame = Unite(frame, geometry.elements[index]);
	const auto &context = geometry.context;
	const int horizontal = std::max(1, 10 * context.logicalHeight / 480);
	const int top = std::max(1, 6 * context.logicalHeight / 480);
	const int right = std::min(context.logicalWidth, frame.position.x + frame.size.width + horizontal);
	frame.position.x = std::max(0, frame.position.x - horizontal);
	frame.size.width = right - frame.position.x;
	frame.position.y = std::max(0, frame.position.y - top);
	frame.size.height += top;
	frame.size.height = std::min(frame.size.height, context.logicalHeight - frame.position.y);
	return frame;
}

int LogicalEdge(float physical, float origin, float scale, int limit)
{
	return std::clamp(static_cast<int>(std::lround((physical - origin) / scale)), 0, limit);
}

Rectangle InverseRect(PhysicalRect rect, const D3dHudGeometryContext &context)
{
	const float center = context.outputOriginX + context.viewportWidth / 2;
	const float bottom = context.outputOriginY + context.viewportHeight;
	const int left = LogicalEdge(center + rect.x, context.outputOriginX, context.outputScaleX, context.logicalWidth);
	const int right = LogicalEdge(center + rect.x + rect.width, context.outputOriginX, context.outputScaleX, context.logicalWidth);
	const int top = LogicalEdge(bottom + rect.y, context.outputOriginY, context.outputScaleY, context.logicalHeight);
	const int end = LogicalEdge(bottom + rect.y + rect.height, context.outputOriginY, context.outputScaleY, context.logicalHeight);
	return { { left, top }, { std::max(1, right - left), std::max(1, end - top) } };
}

bool PositiveFinite(float value)
{
	return std::isfinite(value) && value > 0;
}

} // namespace

ScopedD3dHudGeometryRenderer::ScopedD3dHudGeometryRenderer(SDL_Renderer *target)
    : previousRenderer_(BoundRenderer)
    , previousBinding_(HasRendererBinding)
{
	BoundRenderer = target;
	HasRendererBinding = true;
}

ScopedD3dHudGeometryRenderer::~ScopedD3dHudGeometryRenderer()
{
	BoundRenderer = previousRenderer_;
	HasRendererBinding = previousBinding_;
}

D3dHudGeometryContext CaptureD3dHudGeometryContext(SDL_Renderer *target)
{
	D3dHudGeometryContext context;
	context.renderer = target;
	context.logicalWidth = std::max<int>(1, GetScreenWidth());
	context.logicalHeight = std::max<int>(1, GetScreenHeight());
	context.outputWidth = context.rawViewportWidth = context.logicalWidth;
	context.outputHeight = context.rawViewportHeight = context.logicalHeight;
	context.viewportWidth = static_cast<float>(context.logicalWidth);
	context.viewportHeight = static_cast<float>(context.logicalHeight);
	context.layoutGeneration = GetD3dUiLayoutGeneration();
	context.hasHudOverride = HasD3dHudUiLayoutOverride();
	// CPU/no-renderer layouts have a valid 1:1 logical content viewport.
	context.valid = true;
	if (target == nullptr) return context;
#ifdef USE_SDL3
	SDL_Rect viewport;
	float scaleX = 1, scaleY = 1;
	if (!SDL_GetRenderOutputSize(target, &context.outputWidth, &context.outputHeight)
	    || !SDL_GetRenderScale(target, &scaleX, &scaleY)
	    || !SDL_GetRenderViewport(target, &viewport)) return context;
	SDL_RendererLogicalPresentation presentation;
	if (!SDL_GetRenderLogicalPresentation(target, &context.rendererLogicalWidth, &context.rendererLogicalHeight, &presentation)) return context;
	context.rawViewportX = viewport.x;
	context.rawViewportY = viewport.y;
	context.rawViewportWidth = viewport.w;
	context.rawViewportHeight = viewport.h;
	context.integerScale = presentation == SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
	SDL_FRect logicalViewport;
	if (context.rendererLogicalWidth == context.logicalWidth && context.rendererLogicalHeight == context.logicalHeight
	    && SDL_GetRenderLogicalPresentationRect(target, &logicalViewport)
	    && PositiveFinite(logicalViewport.w) && PositiveFinite(logicalViewport.h)) {
		context.outputScaleX = logicalViewport.w / context.logicalWidth * scaleX;
		context.outputScaleY = logicalViewport.h / context.logicalHeight * scaleY;
		context.outputOriginX = logicalViewport.x + viewport.x * context.outputScaleX;
		context.outputOriginY = logicalViewport.y + viewport.y * context.outputScaleY;
		context.viewportWidth = std::min(logicalViewport.w, viewport.w * context.outputScaleX);
		context.viewportHeight = std::min(logicalViewport.h, viewport.h * context.outputScaleY);
	} else {
		context.outputScaleX = scaleX;
		context.outputScaleY = scaleY;
		context.outputOriginX = viewport.x * scaleX;
		context.outputOriginY = viewport.y * scaleY;
		context.viewportWidth = viewport.w * scaleX;
		context.viewportHeight = viewport.h * scaleY;
	}
#elif !defined(USE_SDL1)
	if (SDL_GetRendererOutputSize(target, &context.outputWidth, &context.outputHeight) != 0) return context;
	SDL_RenderGetScale(target, &context.outputScaleX, &context.outputScaleY);
	SDL_Rect viewport;
	SDL_RenderGetViewport(target, &viewport);
	SDL_RenderGetLogicalSize(target, &context.rendererLogicalWidth, &context.rendererLogicalHeight);
	context.rawViewportX = viewport.x;
	context.rawViewportY = viewport.y;
	context.rawViewportWidth = viewport.w;
	context.rawViewportHeight = viewport.h;
#if SDL_VERSION_ATLEAST(2, 0, 5)
	context.integerScale = SDL_RenderGetIntegerScale(target) == SDL_TRUE;
#endif
	if (!PositiveFinite(context.outputScaleX) || !PositiveFinite(context.outputScaleY)) {
		context.outputScaleX = context.outputScaleY = 1;
		return context;
	}
	// SDL2's public viewport getter floors physical/scale. Multiplying it back
	// can turn a real 720-high viewport into 718.67. Recover standard logical
	// presentation from its logical size, preserving custom viewport changes.
	const bool standardLogical = context.rendererLogicalWidth == context.logicalWidth
	    && context.rendererLogicalHeight == context.logicalHeight
	    && std::abs(viewport.w - context.logicalWidth) <= 1
	    && std::abs(viewport.h - context.logicalHeight) <= 1;
	context.viewportWidth = standardLogical ? std::floor(context.logicalWidth * context.outputScaleX + 0.001F) : viewport.w * context.outputScaleX;
	context.viewportHeight = standardLogical ? std::floor(context.logicalHeight * context.outputScaleY + 0.001F) : viewport.h * context.outputScaleY;
	context.outputOriginX = standardLogical ? static_cast<float>((context.outputWidth - static_cast<int>(context.viewportWidth)) / 2) : viewport.x * context.outputScaleX;
	context.outputOriginY = standardLogical ? static_cast<float>((context.outputHeight - static_cast<int>(context.viewportHeight)) / 2) : viewport.y * context.outputScaleY;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	float logicalZeroX = 0, logicalZeroY = 0;
	SDL_RenderWindowToLogical(target, 0, 0, &logicalZeroX, &logicalZeroY);
	// Surface renderers may have no initialized window/DPI transform. A NaN
	// leaves the standard-fit or raw-viewport fallback above intact.
	if (std::isfinite(logicalZeroX) && std::isfinite(logicalZeroY)) {
		context.outputOriginX = -logicalZeroX * context.outputScaleX;
		context.outputOriginY = -logicalZeroY * context.outputScaleY;
	}
#endif
#else
	return context;
#endif
	if (!PositiveFinite(context.outputScaleX) || !PositiveFinite(context.outputScaleY)
	    || !std::isfinite(context.outputOriginX) || !std::isfinite(context.outputOriginY)
	    || context.outputWidth <= 0 || context.outputHeight <= 0) {
		context.outputScaleX = context.outputScaleY = 1;
		context.outputOriginX = context.outputOriginY = 0;
		return context;
	}
	context.viewportWidth = std::min(context.viewportWidth, context.outputWidth - context.outputOriginX);
	context.viewportHeight = std::min(context.viewportHeight, context.outputHeight - context.outputOriginY);
	context.valid = PositiveFinite(context.viewportWidth) && PositiveFinite(context.viewportHeight);
	return context;
}

D3dHudGeometryContext GetD3dHudGeometryContext()
{
	LoadHudOverrides();
	auto context = CaptureD3dHudGeometryContext(HasRendererBinding ? BoundRenderer : renderer);
	context.customizationGeneration = HudCustomizationGeneration;
	return context;
}

D3dHudGeometry ResolveD3dHudGeometry(const D3dHudGeometryContext &context)
{
	D3dHudGeometry result;
	result.context = context;
	const bool compact = !context.hasHudOverride && context.valid;
	if (!compact) {
		result.mode = context.hasHudOverride ? D3dHudGeometryMode::ExplicitAuthoredV1 : D3dHudGeometryMode::SmallAuthored;
		const int width = context.logicalWidth * 480 / context.logicalHeight;
		const int safeWidth = D3dHudSafeWidth(width, 480);
		for (size_t i = 0; i < D3dHudLayoutEntries.size(); ++i) {
			const auto &entry = D3dHudLayoutEntries[i];
			result.elements[i] = D3dHudScaleRect(D3dHudPlaceRect(GetD3dUiRect(entry.name, safeWidth, 480,
			                                       D3dHudDefaultRect(entry, safeWidth, 480)),
			    entry, width, 480), context.logicalHeight);
		}
		for (int i = 0; i < 8; ++i) result.beltSlots[i] = D3dHudBeltSlotRect(result.elements[2], i);
		result.frame = AuthoredFrame(result);
		auto &metrics = result.metrics;
		metrics.uiScale = metrics.designScaleX = metrics.designScaleY = context.logicalHeight / 480.0F;
		metrics.infoGlyphScaleX = metrics.labelGlyphScaleX = metrics.keyGlyphScaleX = metrics.valueGlyphScaleX = metrics.designScaleX;
		metrics.infoGlyphScaleY = metrics.labelGlyphScaleY = metrics.keyGlyphScaleY = metrics.valueGlyphScaleY = metrics.designScaleY;
		metrics.infoPhysicalGlyphHeight = 12 * metrics.infoGlyphScaleY * context.outputScaleY;
		metrics.infoPhysicalLinePitch = metrics.infoPhysicalGlyphHeight;
		return result;
	}
	result.mode = D3dHudGeometryMode::Compact;
	auto &metrics = result.metrics;
	const float u = std::min({ 1.0F, context.viewportHeight / 1080.0F, context.viewportWidth / 912.0F });
	const bool smallOutput = context.viewportHeight < 720;
	metrics.uiScale = u;
	metrics.designScaleX = u / context.outputScaleX;
	metrics.designScaleY = u / context.outputScaleY;
	metrics.infoPhysicalGlyphHeight = std::max(12.0F, 16 * u);
	metrics.infoPhysicalLinePitch = std::max(smallOutput ? 12.0F : 14.0F, 20 * u);
	metrics.infoPhysicalInset = std::max(smallOutput ? 2.0F : 4.0F, 6 * u);
	const float infoFont = metrics.infoPhysicalGlyphHeight / 12;
	const float labelFont = std::max(12.0F, 18 * u) / 12;
	metrics.infoGlyphScaleX = infoFont / context.outputScaleX;
	metrics.infoGlyphScaleY = infoFont / context.outputScaleY;
	metrics.labelGlyphScaleX = metrics.keyGlyphScaleX = metrics.valueGlyphScaleX = labelFont / context.outputScaleX;
	metrics.labelGlyphScaleY = metrics.keyGlyphScaleY = metrics.valueGlyphScaleY = labelFont / context.outputScaleY;
	metrics.infoLineHeight = static_cast<int>(std::ceil(metrics.infoPhysicalLinePitch / infoFont - 0.001F));
	// Source raster line boxes have integer pitch. Reserve their effective
	// physical pitch, including the 720..810 interval where ceil adds a pixel.
	metrics.infoPhysicalLinePitch = metrics.infoLineHeight * infoFont;
	const int sourceInset = static_cast<int>(std::floor(metrics.infoPhysicalInset / infoFont));
	std::array<PhysicalRect, 13> physical = CompactEntries;
	for (auto &rect : physical) { rect.x *= u; rect.y *= u; rect.width *= u; rect.height *= u; }
	// Five actual line boxes, rather than five nominal glyph heights, must fit.
	float infoHeight = std::max(physical[4].height, 5 * metrics.infoPhysicalLinePitch + 2 * metrics.infoPhysicalInset);
	const float infoBottom = physical[4].y + physical[4].height;
	physical[4].y = infoBottom - infoHeight;
	physical[4].height = infoHeight;
	// Inverting fractional SDL edges can lose one logical pixel. Keep the
	// existing bottom edge and grow upward only when that source buffer would
	// no longer contain all five line boxes plus both actual source insets.
	const int sourceMinimum = 5 * metrics.infoLineHeight + 2 * sourceInset;
	const int logicalMinimum = static_cast<int>(std::ceil(sourceMinimum * metrics.infoGlyphScaleY - 0.001F));
	const int inverseHeight = InverseRect(physical[4], context).size.height;
	if (inverseHeight < logicalMinimum) {
		infoHeight += (logicalMinimum - inverseHeight) * context.outputScaleY;
		physical[4].y = infoBottom - infoHeight;
		physical[4].height = infoHeight;
	}
	const float lift = infoHeight - CompactEntries[4].height * u;
	physical[2].y -= lift;
	// Keep the existing multiplayer controls in the same protected bottom band
	// as the belt when native 352px bodies are open at 640x480.
	if (smallOutput || context.logicalHeight <= 480) {
		physical[11].y = physical[12].y = physical[2].y;
	}
	physical[3].y = physical[4].y + (infoHeight - physical[3].height) / 2;
	for (size_t i = 0; i < physical.size(); ++i) result.elements[i] = InverseRect(physical[i], context);
	for (int i = 0; i < 8; ++i) {
		PhysicalRect slot = physical[2];
		slot.x += slot.width * i / 8;
		slot.width /= 8;
		result.beltSlots[i] = InverseRect(slot, context);
	}
	float left = physical[2].x, top = physical[2].y;
	float right = left + physical[2].width, bottom = top + physical[2].height;
	for (size_t i : { size_t { 3 }, size_t { 4 }, size_t { 5 }, size_t { 6 }, size_t { 7 }, size_t { 8 }, size_t { 9 }, size_t { 10 } }) {
		left = std::min(left, physical[i].x); top = std::min(top, physical[i].y);
		right = std::max(right, physical[i].x + physical[i].width); bottom = std::max(bottom, physical[i].y + physical[i].height);
	}
	result.frame = InverseRect({ left - 16 * u, top - 8 * u, right - left + 32 * u, bottom - top + 12 * u }, context);
	const int sourceHeight = std::max(1, static_cast<int>(result.elements[4].size.height / metrics.infoGlyphScaleY));
	metrics.infoInsetX = sourceInset;
	metrics.infoInsetY = std::min(sourceInset, std::max(0, (sourceHeight - 5 * metrics.infoLineHeight) / 2));
	metrics.infoPhysicalInset = metrics.infoInsetY * infoFont;
	return result;
}

const D3dHudGeometry &GetD3dHudGeometry()
{
	const auto context = GetD3dHudGeometryContext();
	if (!CachedGeometry || !(CachedGeometry->context == context)) {
		CachedGeometry = ResolveD3dHudGeometry(context);
		for (size_t i = 0; i < CachedGeometry->elements.size(); ++i)
			CachedGeometry->elements[i] = ApplyHudOverride(i, CachedGeometry->elements[i], context);
		for (int i = 0; i < 8; ++i) CachedGeometry->beltSlots[i] = D3dHudBeltSlotRect(CachedGeometry->elements[2], i);
	}
	return *CachedGeometry;
}

const D3dHudMetrics &GetD3dHudMetrics()
{
	return GetD3dHudGeometry().metrics;
}

int GetD3dHudDesignPixelsX(int pixels)
{
	return std::max(1, static_cast<int>(std::lround(pixels * GetD3dHudMetrics().designScaleX)));
}

int GetD3dHudDesignPixelsY(int pixels)
{
	return std::max(1, static_cast<int>(std::lround(pixels * GetD3dHudMetrics().designScaleY)));
}

Rectangle ResolveD3dHudCustomizedRect(size_t element, Rectangle standard)
{
	const auto context = GetD3dHudGeometryContext();
	return ApplyHudOverride(element, standard, context);
}

uint8_t GetD3dHudResizeEdges(Rectangle area, Point pointer, int grip)
{
	if (!area.contains(pointer) || grip <= 0) return D3dHudResizeNone;
	grip = std::min({ grip, std::max(1, area.size.width / 2), std::max(1, area.size.height / 2) });
	uint8_t edges = D3dHudResizeNone;
	if (pointer.x < area.position.x + grip) edges |= D3dHudResizeLeft;
	else if (pointer.x >= area.position.x + area.size.width - grip) edges |= D3dHudResizeRight;
	if (pointer.y < area.position.y + grip) edges |= D3dHudResizeTop;
	else if (pointer.y >= area.position.y + area.size.height - grip) edges |= D3dHudResizeBottom;
	return edges;
}

void BeginD3dHudEdgeCustomization(size_t element, Rectangle area, Point pointer, uint8_t edges)
{
	LoadHudOverrides();
	// A repeated DOWN cancels rather than adopting the first preview as a
	// new origin. Its caller already owns the UI pair, including the UP.
	if (HudDrag) { CancelD3dHudCustomization(); return; }
	const auto context = GetD3dHudGeometryContext();
	if (element < HudOverrides.size()) HudDrag = HudGesture { element, ClampHudElement(element, area, context), pointer,
		static_cast<uint8_t>(edges & 15), context, HudOverrides[element] };
}

void BeginD3dHudCustomization(size_t element, Rectangle area, Point pointer, bool resize)
{
	BeginD3dHudEdgeCustomization(element, area, pointer,
		resize ? D3dHudResizeRight | D3dHudResizeBottom : D3dHudResizeNone);
}

bool UpdateD3dHudCustomization(Point pointer)
{
	LoadHudOverrides();
	if (!HudDrag) return false;
	if (HudDrag->element == D3dHudMinimapElement
	    && (!AutomapActive || GetAutomapType() != AutomapType::Minimap)) {
		CancelD3dHudCustomization(); return false;
	}
	Rectangle rect = HudDrag->original;
	const Displacement delta = pointer - HudDrag->start;
	const auto context = GetD3dHudGeometryContext();
	auto originalViewport = HudDrag->viewport;
	originalViewport.layoutGeneration = context.layoutGeneration;
	originalViewport.customizationGeneration = context.customizationGeneration;
	if (!(originalViewport == context)) {
		CancelD3dHudCustomization();
		return false; // A new viewport/DPI cannot reuse an old physical gesture.
	}
	if (HudDrag->edges != D3dHudResizeNone) {
		const Size minimum = ClampHudElement(HudDrag->element, { { 0, 0 }, { 1, 1 } }, context).size;
		int left = rect.position.x, top = rect.position.y;
		int right = left + rect.size.width, bottom = top + rect.size.height;
		if ((HudDrag->edges & D3dHudResizeLeft) != 0)
			left = std::clamp(left + delta.deltaX, 0, right - minimum.width);
		else if ((HudDrag->edges & D3dHudResizeRight) != 0)
			right = std::clamp(right + delta.deltaX, left + minimum.width, context.logicalWidth);
		if ((HudDrag->edges & D3dHudResizeTop) != 0)
			top = std::clamp(top + delta.deltaY, 0, bottom - minimum.height);
		else if ((HudDrag->edges & D3dHudResizeBottom) != 0)
			bottom = std::clamp(bottom + delta.deltaY, top + minimum.height, context.logicalHeight);
		rect = { { left, top }, { right - left, bottom - top } };
	} else rect.position += delta;
	StoreHudOverride(HudDrag->element, rect, context);
	return true;
}

bool EndD3dHudCustomization()
{
	if (!HudDrag) return false;
	SaveHudOverrides(); HudDrag.reset(); return true;
}

void CancelD3dHudCustomization()
{
	if (HudDrag) { HudOverrides[HudDrag->element] = HudDrag->previous; ++HudCustomizationGeneration; HudDrag.reset(); }
}
void ResetD3dHudCustomization(size_t element)
{
	LoadHudOverrides();
	if (element >= HudOverrides.size()) return;
	CancelD3dHudCustomization();
	HudOverrides[element].reset(); ++HudCustomizationGeneration; SaveHudOverrides();
}
void ResetAllD3dHudCustomizations()
{
	LoadHudOverrides(); HudOverrides = {}; HudDrag.reset(); ++HudCustomizationGeneration; SaveHudOverrides();
}
bool HasD3dHudCustomization()
{
	LoadHudOverrides();
	return std::any_of(HudOverrides.begin(), HudOverrides.end(), [](const auto &rect) { return rect.has_value(); });
}

} // namespace devilution
