#include "engine/render/d3d_panel_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <vector>

#include "control/d3d_hud.hpp"
#include "control/d3d_hud_skin.hpp"
#include "cursor.h"
#include "engine/palette.h"
#include "engine/hd_sprite_pack.hpp"
#include "engine/render/town_presentation.hpp"
#include "engine/render/town_view.hpp"
#include "engine/render/ui_overlay_regions.hpp"
#include "engine/surface.hpp"
#include "utils/sdl_ptrs.h"

namespace devilution {
namespace {

constexpr uint8_t ContentKey = 1;
constexpr size_t MaxPanels = 8;
constexpr size_t MaxPixels = 16 * 1024 * 1024;

struct PanelRecipe {
	SDL_Rect bounds;
	SDL_Rect destination {};
	std::vector<uint8_t> backdrop;
	std::vector<uint8_t> contents;
	std::vector<uint8_t> opacity;
	bool sealed = false;
	std::vector<uint32_t> authoredItemColors;
};

SDL_Surface *Source = nullptr;
SDL_Rect SourceRegion {};
std::vector<uint8_t> World;
std::vector<PanelRecipe> Panels;
std::vector<SDL_Rect> EarlierUi;
std::optional<size_t> OpenPanel;
std::array<SDL_Color, 256> FramePalette {};
std::array<SDL_Color, 256> FrameSystemPalette {};
std::array<uint8_t, 256> ArtBrightness {};
std::array<uint8_t, 32768> Quantized {};
std::array<SDL_Color, 256> QuantizedPalette {};
bool QuantizedReady = false;
bool Valid = false;
bool Capture = false;
bool Sealed = false;
bool Painting = false;
bool Materialized = false;
bool Recovering = false;
Rectangle CursorBounds;
std::vector<uint8_t> CursorPixels;
std::vector<uint8_t> CursorOpacity;
bool CursorDrawn = false;
D3dPanelPresentationCounters Counters;
D3dPanelPresentationFault Fault = D3dPanelPresentationFault::None;
uint64_t FaultOrdinal = 1, FaultAttempt = 0;
void (*BeforeCursor)(const Surface &) = nullptr;
void (*AfterCursor)(const Surface &) = nullptr;
#ifndef USE_SDL1
SDL_Renderer *Owner = nullptr;
SDLTextureUniquePtr CompositeTexture;
SDLTextureUniquePtr WorldTexture;
std::vector<uint32_t> Composite;
std::vector<uint32_t> WorldColors;
bool Prepared = false;
bool PreparedWithTown = false;
#endif

bool Intersects(SDL_Rect a, SDL_Rect b)
{
	return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0
	    && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool Matches(const Surface &out)
{
	return Valid && out.surface == Source && out.region.x >= SourceRegion.x && out.region.y >= SourceRegion.y
	    && out.region.x + out.w() <= SourceRegion.x + SourceRegion.w && out.region.y + out.h() <= SourceRegion.y + SourceRegion.h;
}

bool RecorderMatches()
{
	const UiOverlayFrame ui = GetUiOverlayFrame();
	return detail::UiOverlayRegionsActive && ui.source == Source && ui.sourceRegion.x == SourceRegion.x
	    && ui.sourceRegion.y == SourceRegion.y && ui.sourceRegion.w == SourceRegion.w && ui.sourceRegion.h == SourceRegion.h;
}

bool Inject(D3dPanelPresentationFault phase)
{
	if (Fault != phase || ++FaultAttempt != FaultOrdinal) return false;
	Fault = D3dPanelPresentationFault::None; ++Counters.injectedFailures; return true;
}

Surface FrameSurface()
{
	return Surface { Source, SourceRegion };
}

uint32_t PaletteColor(uint8_t index, const std::array<SDL_Color, 256> &palette)
{
	const auto &color = palette[index];
	return 0xFF000000U | (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
}

// Exact straight-alpha source-over; used only for decorative pixels.
uint32_t Over(uint32_t front, uint32_t back)
{
	const unsigned fa = front >> 24, ba = back >> 24;
	const unsigned alphaNumerator = fa * 255 + ba * (255 - fa);
	if (alphaNumerator == 0) return 0;
	uint32_t result = ((alphaNumerator + 127) / 255) << 24;
	for (unsigned shift : { 0U, 8U, 16U }) {
		const unsigned color = (((front >> shift) & 255U) * fa * 255
		    + ((back >> shift) & 255U) * ba * (255 - fa) + alphaNumerator / 2) / alphaNumerator;
		result |= color << shift;
	}
	return result;
}

int SliceCoordinate(int position, int length, int sourceLength, int first, int last, int firstTarget, int lastTarget)
{
	firstTarget = std::min(firstTarget, length / 2);
	lastTarget = std::min(lastTarget, length - firstTarget);
	if (position < firstTarget) return position * first / std::max(1, firstTarget);
	if (position >= length - lastTarget)
		return sourceLength - last + (position - length + lastTarget) * last / std::max(1, lastTarget);
	return first + (position - firstTarget) * std::max(0, sourceLength - first - last)
	    / std::max(1, length - firstTarget - lastTarget);
}

uint32_t ArtPixel(const PanelRecipe &panel, int x, int y, bool bright)
{
	const auto &slice = GetD3dHudSkinSlice(D3dHudSkinPart::Inset);
	const auto &image = GetD3dHudSkinImage(D3dHudSkinPart::Inset);
	const int sx = slice.source.x + SliceCoordinate(x, panel.bounds.w, slice.source.w,
	    slice.sourceMargins[0], slice.sourceMargins[2], slice.logicalMargins[0], slice.logicalMargins[2]);
	const int sy = slice.source.y + SliceCoordinate(y, panel.bounds.h, slice.source.h,
	    slice.sourceMargins[1], slice.sourceMargins[3], slice.logicalMargins[1], slice.logicalMargins[3]);
	const uint32_t color = image.pixels[static_cast<size_t>(sy) * image.width + sx];
	uint32_t result = (((color >> 24) * GetD3dHudBackgroundOpacity() + 127) / 255) << 24;
	for (unsigned shift : { 0U, 8U, 16U }) {
		const uint8_t channel = static_cast<uint8_t>(color >> shift);
		result |= static_cast<uint32_t>(bright ? ArtBrightness[channel] : channel) << shift;
	}
	return result;
}

bool SamePalette(const SDL_Color *palette, const std::array<SDL_Color, 256> &reference)
{
	for (size_t i = 0; i < reference.size(); ++i)
		if (palette[i].r != reference[i].r || palette[i].g != reference[i].g || palette[i].b != reference[i].b) return false;
	return true;
}

void BuildQuantizer(const std::array<SDL_Color, 256> &palette)
{
	if (QuantizedReady && SamePalette(palette.data(), QuantizedPalette)) return;
	for (unsigned rgb = 0; rgb < Quantized.size(); ++rgb) {
		const int r = static_cast<int>((rgb >> 10) * 8 + 4);
		const int g = static_cast<int>(((rgb >> 5) & 31) * 8 + 4);
		const int b = static_cast<int>((rgb & 31) * 8 + 4);
		int bestDistance = std::numeric_limits<int>::max();
		uint8_t best = 0;
		for (size_t i = 0; i < palette.size(); ++i) {
			const int dr = r - palette[i].r, dg = g - palette[i].g, db = b - palette[i].b;
			const int distance = dr * dr + dg * dg + db * db;
			if (distance < bestDistance) { bestDistance = distance; best = static_cast<uint8_t>(i); }
		}
		Quantized[rgb] = best;
	}
	QuantizedPalette = palette; QuantizedReady = true;
}

void PaintCpu(const Surface &out, const PanelRecipe &panel, const std::array<SDL_Color, 256> &palette)
{
	MarkUiOverlayRect(out, panel.destination.x, panel.destination.y, panel.destination.w, panel.destination.h);
	for (int y = 0; y < panel.destination.h; ++y) {
		for (int x = 0; x < panel.destination.w; ++x) {
			const int sx=x*panel.bounds.w/panel.destination.w,sy=y*panel.bounds.h/panel.destination.h;
			const size_t local = static_cast<size_t>(sy) * panel.bounds.w + sx;
			const Point point { panel.destination.x + x, panel.destination.y + y };
			const uint32_t color = Over(ArtPixel(panel, sx, sy, false), PaletteColor(out[point], palette));
			out[point] = Quantized[((((color >> 16) & 255U) >> 3) * 32 + (((color >> 8) & 255U) >> 3)) * 32 + ((color & 255U) >> 3)];
			if (panel.sealed && panel.opacity[local] != 0) out[point] = panel.contents[local];
		}
	}
}

void PaintImmediately(const Surface &out, const PanelRecipe &panel)
{
	const bool matchingFrame = Matches(out);
	if (matchingFrame) {
		RecoverD3dPanelPresentationFrame(out);
		// Even with no previous recipe, later panels must not erase this one.
		Materialized = true;
	}
	const auto &palette = matchingFrame ? FramePalette : logical_palette;
	BuildQuantizer(palette);
	Painting = true; PaintCpu(out, panel, palette); Painting = false;
}

} // namespace

void InvalidateD3dPanelPresentationFrame()
{
	Source = nullptr; SourceRegion = {}; World.clear(); Panels.clear(); EarlierUi.clear(); OpenPanel.reset();
	Valid = Capture = Sealed = Painting = Materialized = Recovering = false;
	CursorPixels.clear(); CursorOpacity.clear(); CursorBounds = {}; CursorDrawn = false;
#ifndef USE_SDL1
	Prepared = false;
#endif
}

void BeginD3dPanelPresentationFrame(const Surface &world)
{
	++Counters.frames;
	InvalidateD3dPanelPresentationFrame();
	if (world.surface == nullptr || SDLC_SURFACE_BITSPERPIXEL(world.surface) != 8 || world.w() <= 0 || world.h() <= 0
	    || static_cast<size_t>(world.w()) * world.h() > MaxPixels || world.region.x < 0 || world.region.y < 0
	    || world.region.x + world.w() > world.surface->w || world.region.y + world.h() > world.surface->h) return;
	try {
		World.resize(static_cast<size_t>(world.w()) * world.h());
		for (int y = 0; y < world.h(); ++y) std::memcpy(World.data() + static_cast<size_t>(y) * world.w(), world.at(0, y), world.w());
		Panels.reserve(MaxPanels);
	} catch (const std::bad_alloc &) { InvalidateD3dPanelPresentationFrame(); return; }
	Source = world.surface; SourceRegion = world.region; FramePalette = logical_palette; FrameSystemPalette = system_palette;
	std::array<SDL_Color, 256> gray {}, mapped {};
	for (size_t i = 0; i < gray.size(); ++i) gray[i] = SDL_Color { static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i), 255 };
	ApplyGlobalBrightness(mapped.data(), gray.data());
	for (size_t i = 0; i < gray.size(); ++i) ArtBrightness[i] = mapped[i].r;
	Valid = true;
}

void SealD3dPanelBackplate(const Surface &out)
{
	if (!Matches(out) || !OpenPanel) return;
	const Surface frame = FrameSurface();
	auto &panel = Panels[*OpenPanel];
	for (int y = 0; y < panel.bounds.h; ++y) {
		for (int x = 0; x < panel.bounds.w; ++x) {
			const size_t i = static_cast<size_t>(y) * panel.bounds.w + x;
			uint8_t &pixel = frame[{ panel.bounds.x + x, panel.bounds.y + y }];
			panel.contents[i] = pixel;
			panel.opacity[i] = pixel != ContentKey;
			if (Materialized || pixel == ContentKey) pixel = panel.backdrop[i];
		}
	}
	panel.sealed = true; OpenPanel.reset();
	if (Materialized) {
		// A previous window may have forced indexed recovery. Capture this
		// complete native tail too, restore its scratch, then scale it once.
		BuildQuantizer(FramePalette);
		Painting = true; PaintCpu(frame, panel, FramePalette); Painting = false;
	}
}

void RecordD3dPanelItemRgba(const Surface &out, Point position, ClxSprite sprite)
{
	if (!Valid || !OpenPanel || Materialized || Recovering || Painting || !Matches(out) || sprite.hdFrameId() == 0) return;
	auto rgba = LoadHdSpriteRgba(sprite);
	if (!rgba || rgba->empty()) return;
	auto &panel = Panels[*OpenPanel];
	if (panel.authoredItemColors.empty()) {
		try { panel.authoredItemColors.resize(static_cast<size_t>(panel.bounds.w) * panel.bounds.h); }
		catch (const std::bad_alloc &) { return; }
	}
	++Counters.rgbItemRecords;
	const int scale = sprite.hdScale(), width = sprite.width(), height = sprite.height();
	for (int y = 0; y < height; ++y) {
		const int py = position.y - height + 1 + y - panel.bounds.y;
		if (py < 0 || py >= panel.bounds.h) continue;
		for (int x = 0; x < width; ++x) {
			const int px = position.x + x - panel.bounds.x;
			if (px < 0 || px >= panel.bounds.w) continue;
			const uint32_t color = (*rgba)[static_cast<size_t>(y * scale + scale / 2) * width * scale + x * scale + scale / 2];
			if ((color >> 24) != 0) {
				panel.authoredItemColors[static_cast<size_t>(py) * panel.bounds.w + px] = color;
				++Counters.rgbItemPixels;
			}
		}
	}
}

void DrawD3dPanelBackplate(const Surface &out, Rectangle bounds, Rectangle destination)
{
	SealD3dPanelBackplate(out);
	const int left = std::max(0, bounds.position.x), top = std::max(0, bounds.position.y);
	const int right = static_cast<int>(std::min<int64_t>(out.w(), static_cast<int64_t>(bounds.position.x) + bounds.size.width));
	const int bottom = static_cast<int>(std::min<int64_t>(out.h(), static_cast<int64_t>(bounds.position.y) + bounds.size.height));
	if (right <= left || bottom <= top || !IsD3dHudSkinLoaded()) return;
	PanelRecipe panel { { left, top, right - left, bottom - top }, {}, {}, {}, {}, false };
	panel.destination=destination.size.width>0 ? SDL_Rect{destination.position.x,destination.position.y,destination.size.width,destination.size.height}:panel.bounds;
	if(panel.destination.x<0 || panel.destination.y<0 || panel.destination.w<=0 || panel.destination.h<=0 || int64_t(panel.destination.x)+panel.destination.w>out.w() || int64_t(panel.destination.y)+panel.destination.h>out.h()) panel.destination=panel.bounds;
	// Chat, controller navigation and virtual controls keep their existing
	// native HUD geometry. Paint the same R1 art now, before that native tail,
	// instead of restoring a world rectangle over its later controls.
	if (!IsD3dHudEnabled()) ++Counters.compatibilityDeclines;
	if (!RecorderMatches()) ++Counters.recorderDeclines;
	if (!IsD3dHudEnabled() || !Matches(out) || !RecorderMatches() || Sealed || Panels.size() >= MaxPanels) {
		PaintImmediately(out, panel); return;
	}
	const SDL_Rect frameBounds { left + out.region.x - SourceRegion.x, top + out.region.y - SourceRegion.y, right - left, bottom - top };
	// Unknown earlier UI is never erased by a world restoration. Use CPU R1
	// for this frame before that UI becomes the new panel's backdrop.
	if (!Materialized && panel.destination.w==panel.bounds.w && std::any_of(EarlierUi.begin(), EarlierUi.end(), [&](SDL_Rect region) { return Intersects(region, frameBounds); })) {
		PaintImmediately(out, panel); return;
	}
	try {
		const size_t area = static_cast<size_t>(panel.bounds.w) * panel.bounds.h;
		panel.backdrop.resize(area); panel.contents.resize(area); panel.opacity.resize(area);
	} catch (const std::bad_alloc &) {
		PaintImmediately(out, panel); return;
	}
	// Notify the existing HUD before clearing the scratch: an overlap must
	// materialize R1 first. The panel recorder ignores our decorative mark.
	Painting = true;
	MarkUiOverlayRect(out, left, top, right - left, bottom - top);
	Painting = false;
	for (int y = 0; y < panel.bounds.h; ++y) {
		std::memcpy(panel.backdrop.data() + static_cast<size_t>(y) * panel.bounds.w, out.at(left, top + y), panel.bounds.w);
		std::memset(out.at(left, top + y), ContentKey, panel.bounds.w);
	}
	panel.bounds.x += out.region.x - SourceRegion.x;
	panel.bounds.y += out.region.y - SourceRegion.y;
	panel.destination.x+=out.region.x-SourceRegion.x;panel.destination.y+=out.region.y-SourceRegion.y;
	Panels.push_back(std::move(panel)); OpenPanel = Panels.size() - 1; ++Counters.recipes;
}

void SealD3dPanelPresentationFrame(const Surface &out)
{
	SealD3dPanelBackplate(out); Sealed = true;
}

void FinalizeD3dPanelPresentationFrame(const Surface &out)
{
	SealD3dPanelPresentationFrame(out); CursorDrawn = Matches(out) && !CursorPixels.empty();
}

void ClearD3dPanelPresentationCursor()
{
	CursorPixels.clear(); CursorOpacity.clear(); CursorBounds = {}; CursorDrawn = false;
}

void CaptureD3dPanelPresentationCursorPixels(Rectangle bounds, std::span<const uint8_t> pixels, std::span<const uint8_t> opacity)
{
	if (!Valid || !HasD3dPanelPresentationDeferredFrame()) return;
	const size_t area = static_cast<size_t>(std::max(0, bounds.size.width)) * std::max(0, bounds.size.height);
	if (bounds.position.x < 0 || bounds.position.y < 0 || bounds.position.x + bounds.size.width > SourceRegion.w
	    || bounds.position.y + bounds.size.height > SourceRegion.h || area == 0 || pixels.size() != area || opacity.size() != area) {
		RecoverD3dPanelPresentationFrame(FrameSurface()); return;
	}
	try { CursorPixels.assign(pixels.begin(), pixels.end()); CursorOpacity.assign(opacity.begin(), opacity.end()); }
	catch (const std::bad_alloc &) { ClearD3dPanelPresentationCursor(); RecoverD3dPanelPresentationFrame(FrameSurface()); return; }
	CursorBounds = bounds;
}

void CaptureD3dPanelPresentationCursor(Rectangle bounds, Point spriteBottomLeft, int cursorId)
{
	if (!Valid || !HasD3dPanelPresentationDeferredFrame()) return;
	if (bounds.size.width <= 0 || bounds.size.height <= 0 || static_cast<size_t>(bounds.size.width) * bounds.size.height > MaxPixels) {
		RecoverD3dPanelPresentationFrame(FrameSurface()); return;
	}
	try {
#ifdef USE_SDL3
		SDLSurfaceUniquePtr allocated { SDL_CreateSurface(bounds.size.width, bounds.size.height, SDL_PIXELFORMAT_INDEX8) };
#elif defined(USE_SDL1)
		SDLSurfaceUniquePtr allocated { SDL_CreateRGBSurface(0, bounds.size.width, bounds.size.height, 8, 0, 0, 0, 0) };
#else
		SDLSurfaceUniquePtr allocated { SDL_CreateRGBSurfaceWithFormat(0, bounds.size.width, bounds.size.height, 8, SDL_PIXELFORMAT_INDEX8) };
#endif
		if (!allocated) { RecoverD3dPanelPresentationFrame(FrameSurface()); return; }
		const Surface scratch { allocated.get() };
		for (int y = 0; y < scratch.h(); ++y) std::memset(scratch.at(0, y), ContentKey, scratch.w());
		DrawSoftwareCursor(scratch, spriteBottomLeft - Displacement { bounds.position.x, bounds.position.y }, cursorId);
		std::vector<uint8_t> pixels(static_cast<size_t>(scratch.w()) * scratch.h()), opacity(pixels.size());
		for (int y = 0; y < scratch.h(); ++y) {
			for (int x = 0; x < scratch.w(); ++x) {
				const size_t i = static_cast<size_t>(y) * scratch.w() + x;
				pixels[i] = *scratch.at(x, y); opacity[i] = pixels[i] != ContentKey;
			}
		}
		CaptureD3dPanelPresentationCursorPixels(bounds, pixels, opacity);
	} catch (const std::bad_alloc &) { RecoverD3dPanelPresentationFrame(FrameSurface()); }
}

bool HasD3dPanelPresentationDeferredFrame()
{
	return Valid && !Materialized && !Panels.empty();
}

void SetD3dPanelPresentationNativeUiCapture(bool active) { Capture = active && Valid; }
void SetD3dPanelRecoveryCursorCallbacks(void (*before)(const Surface &), void (*after)(const Surface &)) { BeforeCursor = before; AfterCursor = after; }
bool IsD3dPanelPresentationRecoveryPixel(Point point)
{
	if (!Recovering) return false;
	for (const auto &panel : Panels) if (MakeRectangle(panel.bounds).contains(point) || MakeRectangle(panel.destination).contains(point)) return true;
	return false;
}
bool IsD3dPanelPresentationRecovering() { return Recovering; }

namespace detail {
void RecordD3dPanelNativeUiRect(const SDL_Rect &region)
{
	if (!Valid || !Capture || Painting || Recovering || Materialized) return;
	if (OpenPanel) {
		const SDL_Rect bounds = Panels[*OpenPanel].bounds;
		if (region.x < bounds.x || region.y < bounds.y || region.x + region.w > bounds.x + bounds.w || region.y + region.h > bounds.y + bounds.h) {
			++Counters.outsideContentDeclines;
			RecoverD3dPanelPresentationFrame(FrameSurface());
		}
		return; // Opacity comes from the key scratch, never this bbox.
	}
	for (const auto &panel : Panels) {
		if (Intersects(region, panel.bounds) || Intersects(region, panel.destination)) { RecoverD3dPanelPresentationFrame(FrameSurface()); return; }
	}
	if (EarlierUi.size() >= 4096) { RecoverD3dPanelPresentationFrame(FrameSurface()); Materialized = true; return; }
	try { EarlierUi.push_back(region); } catch (const std::bad_alloc &) { RecoverD3dPanelPresentationFrame(FrameSurface()); Materialized = true; }
}
} // namespace detail

bool RecoverD3dPanelPresentationFrame(const Surface &out)
{
	if (!Matches(out) || !HasD3dPanelPresentationDeferredFrame()) return false;
	SealD3dPanelBackplate(out); Materialized = true; Recovering = Painting = true;
	++Counters.recoveries;
	const Surface frame = FrameSurface();
	BuildQuantizer(FramePalette);
	if (CursorDrawn && BeforeCursor != nullptr) BeforeCursor(frame);
	for (const auto &panel : Panels)
		for (int y = 0; y < panel.bounds.h; ++y)
			std::memcpy(frame.at(panel.bounds.x, panel.bounds.y + y), World.data() + static_cast<size_t>(panel.bounds.y + y) * frame.w() + panel.bounds.x, panel.bounds.w);
	for(const auto &panel:Panels) for(int y=0;y<panel.destination.h;++y) std::memcpy(frame.at(panel.destination.x,panel.destination.y+y),World.data()+static_cast<size_t>(panel.destination.y+y)*frame.w()+panel.destination.x,panel.destination.w);
	for (const auto &panel : Panels) PaintCpu(frame, panel, FramePalette);
	if (CursorDrawn && AfterCursor != nullptr) AfterCursor(frame);
	if (CursorDrawn)
		for (int y = 0; y < CursorBounds.size.height; ++y)
			for (int x = 0; x < CursorBounds.size.width; ++x) {
				const size_t i = static_cast<size_t>(y) * CursorBounds.size.width + x;
				const Point point = CursorBounds.position + Displacement { x, y };
				if (CursorOpacity[i] != 0 && IsD3dPanelPresentationRecoveryPixel(point)) frame[point] = CursorPixels[i];
			}
	Recovering = Painting = false;
#ifndef USE_SDL1
	Prepared = false;
#endif
	return true;
}

#ifndef USE_SDL1
bool PrepareD3dPanelPresentation(SDL_Renderer *renderer, SDL_Surface *source, SDL_Palette *palette, bool allow, bool townLayersAvailable)
{
	++Counters.preparations;
	Prepared = false;
	if (!allow || renderer == nullptr || !IsD3dHudEnabled() || !IsD3dHudSkinLoaded() || !HasD3dPanelPresentationDeferredFrame() || source != Source || palette == nullptr || palette->colors == nullptr || palette->ncolors < 256
	    || SDLC_SURFACE_BITSPERPIXEL(source) != 8 || SourceRegion.x + SourceRegion.w > source->w || SourceRegion.y + SourceRegion.h > source->h
	    || !Sealed || OpenPanel || Capture || !RecorderMatches() || (GetTownViewHighResolutionFrame() != nullptr && !townLayersAvailable)) return false;
	if (!SamePalette(palette->colors, FrameSystemPalette)) return false;
	if (std::all_of(palette->colors, palette->colors + 256, [](SDL_Color color) { return color.r == 0 && color.g == 0 && color.b == 0; })) return false;
	try {
		Composite.assign(World.size(), 0); WorldColors.resize(World.size());
		for (size_t i = 0; i < World.size(); ++i) WorldColors[i] = PaletteColor(World[i], FrameSystemPalette);
		for (const auto &panel : Panels) {
			for (int y = 0; y < panel.destination.h; ++y) {
				for (int x = 0; x < panel.destination.w; ++x) {
					const int sx=x*panel.bounds.w/panel.destination.w,sy=y*panel.bounds.h/panel.destination.h;
					const size_t local = static_cast<size_t>(sy) * panel.bounds.w + sx;
					const size_t index = static_cast<size_t>(panel.destination.y + y) * SourceRegion.w + panel.destination.x + x;
					Composite[index] = Over(ArtPixel(panel, sx, sy, true), Composite[index]);
					if (panel.opacity[local]) Composite[index] = PaletteColor(panel.contents[local], FrameSystemPalette);
					if (!panel.authoredItemColors.empty() && (panel.authoredItemColors[local] >> 24) != 0)
						Composite[index] = Over(panel.authoredItemColors[local], Composite[index]);
				}
			}
		}
		for (int y = 0; y < CursorBounds.size.height; ++y)
			for (int x = 0; x < CursorBounds.size.width; ++x) {
				const size_t i = static_cast<size_t>(y) * CursorBounds.size.width + x;
				if (CursorOpacity[i] != 0)
					Composite[static_cast<size_t>(CursorBounds.position.y + y) * SourceRegion.w + CursorBounds.position.x + x] = PaletteColor(CursorPixels[i], FrameSystemPalette);
			}
	} catch (const std::bad_alloc &) { return false; }
	if (Owner != renderer) { CompositeTexture.reset(); WorldTexture.reset(); Owner = renderer; }
	int width = 0, height = 0;
	if (CompositeTexture) {
#ifdef USE_SDL3
		float fw = 0, fh = 0; SDL_GetTextureSize(CompositeTexture.get(), &fw, &fh); width = static_cast<int>(fw); height = static_cast<int>(fh);
#else
		SDL_QueryTexture(CompositeTexture.get(), nullptr, nullptr, &width, &height);
#endif
	}
	if (!CompositeTexture || !WorldTexture || width != SourceRegion.w || height != SourceRegion.h) {
		++Counters.allocationAttempts;
		if (Inject(D3dPanelPresentationFault::BeforeTextureAllocation)) return false;
		CompositeTexture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SourceRegion.w, SourceRegion.h));
		WorldTexture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SourceRegion.w, SourceRegion.h));
	}
	if (!CompositeTexture || !WorldTexture) return false;
	++Counters.uploadAttempts;
	if (Inject(D3dPanelPresentationFault::BeforeUpload)) return false;
#ifdef USE_SDL3
	if (!SDL_SetTextureBlendMode(CompositeTexture.get(), SDL_BLENDMODE_BLEND) || !SDL_SetTextureBlendMode(WorldTexture.get(), SDL_BLENDMODE_NONE)
	    || !SDL_SetTextureScaleMode(CompositeTexture.get(), SDL_SCALEMODE_NEAREST) || !SDL_SetTextureScaleMode(WorldTexture.get(), SDL_SCALEMODE_NEAREST)
	    || !SDL_UpdateTexture(CompositeTexture.get(), nullptr, Composite.data(), SourceRegion.w * 4)
	    || !SDL_UpdateTexture(WorldTexture.get(), nullptr, WorldColors.data(), SourceRegion.w * 4)) return false;
#else
	if (SDL_SetTextureBlendMode(CompositeTexture.get(), SDL_BLENDMODE_BLEND) < 0 || SDL_SetTextureBlendMode(WorldTexture.get(), SDL_BLENDMODE_NONE) < 0
#if SDL_VERSION_ATLEAST(2, 0, 12)
	    || SDL_SetTextureScaleMode(CompositeTexture.get(), SDL_ScaleModeNearest) < 0 || SDL_SetTextureScaleMode(WorldTexture.get(), SDL_ScaleModeNearest) < 0
#endif
	    || SDL_UpdateTexture(CompositeTexture.get(), nullptr, Composite.data(), SourceRegion.w * 4) < 0
	    || SDL_UpdateTexture(WorldTexture.get(), nullptr, WorldColors.data(), SourceRegion.w * 4) < 0) return false;
#endif
	PreparedWithTown = townLayersAvailable; Prepared = true; return true;
}

D3dHudPresentationResult RenderD3dPanelPresentation(SDL_Renderer *renderer, SDL_Surface *source, SDL_Palette *palette, bool allow, bool townLayersPresented)
{
	if (!Prepared && !PrepareD3dPanelPresentation(renderer, source, palette, allow, townLayersPresented)) return D3dHudPresentationResult::Inactive;
	if (!allow || !IsD3dHudEnabled() || !HasD3dPanelPresentationDeferredFrame() || Owner != renderer || source != Source || PreparedWithTown != townLayersPresented
	    || palette == nullptr || palette->colors == nullptr || palette->ncolors < 256 || !SamePalette(palette->colors, FrameSystemPalette) || !IsD3dHudSkinLoaded()) {
		Prepared = false; return D3dHudPresentationResult::Inactive;
	}
	Prepared = false;
	++Counters.drawAttempts;
	if (Inject(D3dPanelPresentationFault::BeforeDraw)) return D3dHudPresentationResult::Inactive;
	std::array<SDL_Rect, MaxPanels*2> regions {};
	for (size_t i = 0; i < Panels.size(); ++i) {regions[i*2]=Panels[i].bounds;regions[i*2+1]=Panels[i].destination;}
	if (townLayersPresented) {
		if (!RestoreTownPresentationWorldRegions(renderer, std::span<const SDL_Rect> { regions.data(), Panels.size()*2 })) return D3dHudPresentationResult::Failed;
	} else {
		for (const auto &panel : Panels) {
#ifdef USE_SDL3
			const SDL_FRect bounds { static_cast<float>(panel.destination.x), static_cast<float>(panel.destination.y), static_cast<float>(panel.destination.w), static_cast<float>(panel.destination.h) };
			if (!SDL_RenderTexture(renderer, WorldTexture.get(), &bounds, &bounds)) return D3dHudPresentationResult::Failed;
#else
			if (SDL_RenderCopy(renderer, WorldTexture.get(), &panel.destination, &panel.destination) < 0) return D3dHudPresentationResult::Failed;
#endif
		}
	}
	if (Inject(D3dPanelPresentationFault::AfterWorldRestore)) return D3dHudPresentationResult::Failed;
#ifdef USE_SDL3
	if (!SDL_RenderTexture(renderer, CompositeTexture.get(), nullptr, nullptr)) return D3dHudPresentationResult::Failed;
#else
	if (SDL_RenderCopy(renderer, CompositeTexture.get(), nullptr, nullptr) < 0) return D3dHudPresentationResult::Failed;
#endif
	return D3dHudPresentationResult::Presented;
}

#else
bool PrepareD3dPanelPresentation(SDL_Renderer *, SDL_Surface *, SDL_Palette *, bool, bool) { return false; }
D3dHudPresentationResult RenderD3dPanelPresentation(SDL_Renderer *, SDL_Surface *, SDL_Palette *, bool, bool) { return D3dHudPresentationResult::Inactive; }
#endif

void SetD3dPanelPresentationFault(D3dPanelPresentationFault fault, uint64_t ordinal) { Fault = fault; FaultOrdinal = std::max<uint64_t>(1, ordinal); FaultAttempt = 0; }
D3dPanelPresentationCounters GetD3dPanelPresentationCounters() { return Counters; }
void ResetD3dPanelPresentationCounters() { Counters = {}; }

void ResetD3dPanelPresentationResources()
{
	InvalidateD3dPanelPresentationFrame();
#ifndef USE_SDL1
	CompositeTexture.reset(); WorldTexture.reset(); Owner = nullptr; Composite.clear(); WorldColors.clear();
#endif
}

} // namespace devilution
