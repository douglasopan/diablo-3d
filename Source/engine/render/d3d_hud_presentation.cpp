#include "engine/render/d3d_hud_presentation.hpp"

#include "control/d3d_hud.hpp"

#ifndef USE_SDL1
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "control/control.hpp"
#include "cursor.h"
#include "diablo.h"
#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/render/town_presentation.hpp"
#include "engine/render/town_view.hpp"
#include "engine/surface.hpp"
#include "options.h"
#include "player.h"
#include "qol/xpbar.h"
#include "utils/display.h"
#include "utils/png.h"
#include "utils/sdl_ptrs.h"
#include "utils/sdl_geometry.h"

namespace devilution {
namespace {

constexpr uint8_t TransparentColor = 1;
constexpr size_t MaxFramePixels = 16 * 1024 * 1024;

struct FloatRect {
	float x, y, w, h;
};

struct Slice {
	const char *path;
	SDL_Rect source;
	std::array<int, 4> sourceMargins; // left, top, right, bottom
	std::array<int, 4> logicalMargins;
};

// Derived from d3d-ui/hud/r1/skin.json. Keep these source measurements and
// Godot's metadata in agreement; all destination geometry still uses the
// existing thirteen layout entries, never PNG padding or a new input region.
constexpr Slice Chassis { "d3d-ui/hud/r1/chassis.png", { 22, 190, 2128, 331 }, { 80, 64, 80, 64 }, { 6, 5, 6, 5 } };
constexpr Slice Button { "d3d-ui/hud/r1/button.png", { 62, 143, 1968, 458 }, { 110, 84, 110, 84 }, { 3, 3, 3, 3 } };
constexpr Slice Inset { "d3d-ui/hud/r1/inset.png", { 12, 192, 1960, 409 }, { 70, 62, 70, 62 }, { 2, 2, 2, 2 } };
constexpr SDL_Rect OrbFrameSource { 15, 66, 1061, 1283 };
constexpr float OrbCircleX = 653.28F;
constexpr float OrbCircleY = 606.48F;
constexpr float OrbCircleRadius = 392.30F;
constexpr std::array<SDL_Rect, 2> LiquidSources { SDL_Rect { 58, 54, 1137, 1136 }, SDL_Rect { 55, 52, 1144, 1143 } };

std::optional<OwnedSurface> Base;
std::optional<OwnedSurface> Foreground;
std::optional<OwnedSurface> Cursor;
std::vector<uint8_t> CursorMask;
std::vector<uint8_t> ForegroundMask;
SDL_Rect ForegroundBounds {};
SDL_Surface *FrameSource = nullptr;
SDL_Rect CapturedRegion {};
Point CursorPosition { 0, 0 };
bool FrameValid = false;
bool CoverageReady = false;
bool UiBlocked = true;
bool CursorUpdated = false;
bool NativeUiCaptureActive = false;
std::array<bool, 8> ButtonPressed {};
std::array<bool, 8> ButtonRecorded {};
std::array<uint8_t, 8> BeltTint {};
std::array<bool, 8> BeltTintRecorded {};
std::vector<SDL_Rect> Footprints;

SDL_Renderer *Owner = nullptr;
bool AssetsAttempted = false;
bool AssetsReady = false;
int CachedBrightness = -1;
std::array<uint8_t, 256> BrightnessLut {};
SDLTextureUniquePtr ChassisTexture;
SDLTextureUniquePtr ButtonTexture;
SDLTextureUniquePtr InsetTexture;
SDLTextureUniquePtr OrbFrameTexture;
std::array<SDLTextureUniquePtr, 2> LiquidTextures;
SDLTextureUniquePtr EmptyOrbTexture;
SDLTextureUniquePtr BeltTintTexture;
std::array<int, 2> LiquidSizes {};
int EmptyOrbSize = 0;
SDLTextureUniquePtr BaseTexture;
SDLTextureUniquePtr ForegroundTexture;
SDLTextureUniquePtr CursorTexture;
SDL_Rect UploadedForegroundBounds {};
int BaseTextureWidth = 0, BaseTextureHeight = 0;
int ForegroundTextureWidth = 0, ForegroundTextureHeight = 0;
int CursorWidth = 0, CursorHeight = 0;
std::vector<uint32_t> BaseColors;
std::vector<uint32_t> ForegroundColors;
std::vector<uint32_t> CursorColors;

bool IsIndexed(const Surface &source)
{
	return source.surface != nullptr && SDLC_SURFACE_BITSPERPIXEL(source.surface) == 8
	    && source.w() > 0 && source.h() > 0 && source.region.x >= 0 && source.region.y >= 0
	    && source.region.x + source.w() <= source.surface->w
	    && source.region.y + source.h() <= source.surface->h;
}

SDLSurfaceUniquePtr CreateIndexed(int width, int height)
{
#ifdef USE_SDL3
	return SDLSurfaceUniquePtr { SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8) };
#else
	return SDLSurfaceUniquePtr { SDL_CreateRGBSurfaceWithFormat(0, width, height, 8, SDL_PIXELFORMAT_INDEX8) };
#endif
}

bool EnsureSurface(std::optional<OwnedSurface> &surface, int width, int height)
{
	if (surface && surface->w() == width && surface->h() == height)
		return true;
	auto allocated = CreateIndexed(width, height);
	if (!allocated)
		return false;
	surface.emplace(std::move(allocated));
	return true;
}

bool Clip(Rectangle target, int width, int height, SDL_Rect &clipped)
{
	const int left = std::max(0, target.position.x);
	const int top = std::max(0, target.position.y);
	const int right = std::min(width, target.position.x + target.size.width);
	const int bottom = std::min(height, target.position.y + target.size.height);
	if (right <= left || bottom <= top)
		return false;
	clipped = { left, top, right - left, bottom - top };
	return true;
}

bool Intersects(const SDL_Rect &a, const SDL_Rect &b)
{
	return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0
	    && a.x < b.x + b.w && b.x < a.x + a.w
	    && a.y < b.y + b.h && b.y < a.y + a.h;
}

void IncludeBounds(SDL_Rect &bounds, const SDL_Rect &region)
{
	if (region.w <= 0 || region.h <= 0)
		return;
	if (bounds.w <= 0 || bounds.h <= 0) {
		bounds = region;
		return;
	}
	const int left = std::min(bounds.x, region.x);
	const int top = std::min(bounds.y, region.y);
	const int right = std::max(bounds.x + bounds.w, region.x + region.w);
	const int bottom = std::max(bounds.y + bounds.h, region.y + region.h);
	bounds = { left, top, right - left, bottom - top };
}

std::vector<SDL_Rect> CurrentFootprints()
{
	std::vector<SDL_Rect> result;
	result.reserve(5);
	result.push_back(MakeSdlRect(GetD3dHudFrameRect()));
	result.push_back(MakeSdlRect(GetD3dHudOrbRect(false)));
	result.push_back(MakeSdlRect(GetD3dHudOrbRect(true)));
	if (IsChatAvailable()) {
		result.push_back(MakeSdlRect(GetD3dHudPanelButtonRect(6)));
		result.push_back(MakeSdlRect(GetD3dHudPanelButtonRect(7)));
	}
	return result;
}

bool SameFootprints()
{
	const auto current = CurrentFootprints();
	if (current.size() != Footprints.size())
		return false;
	for (size_t i = 0; i < current.size(); ++i) {
		const auto &a = current[i];
		const auto &b = Footprints[i];
		if (a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h)
			return false;
	}
	return true;
}

void ResetTextures()
{
	ChassisTexture.reset();
	ButtonTexture.reset();
	InsetTexture.reset();
	OrbFrameTexture.reset();
	for (auto &texture : LiquidTextures)
		texture.reset();
	EmptyOrbTexture.reset();
	BeltTintTexture.reset();
	BaseTexture.reset();
	ForegroundTexture.reset();
	CursorTexture.reset();
	UploadedForegroundBounds = {};
	Owner = nullptr;
	AssetsAttempted = false;
	AssetsReady = false;
	CachedBrightness = -1;
	BaseTextureWidth = BaseTextureHeight = ForegroundTextureWidth = ForegroundTextureHeight = 0;
	CursorWidth = CursorHeight = 0;
	LiquidSizes = {};
	EmptyOrbSize = 0;
	BaseColors.clear();
	ForegroundColors.clear();
	CursorColors.clear();
}

bool ConfigureTexture(SDL_Texture *texture, bool linear, bool opaque = false)
{
#ifdef USE_SDL3
	return SDL_SetTextureBlendMode(texture, opaque ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND)
	    && SDL_SetTextureScaleMode(texture, linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
#else
	return SDL_SetTextureBlendMode(texture, opaque ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND) >= 0
#if SDL_VERSION_ATLEAST(2, 0, 12)
	    && SDL_SetTextureScaleMode(texture, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest) >= 0
#endif
	    ;
#endif
}

SDLTextureUniquePtr MakeTexture(SDL_Renderer *renderer, SDL_Surface *image, bool linear, bool opaque = false)
{
#ifdef USE_SDL3
	SDLSurfaceUniquePtr colors { SDL_ConvertSurface(image, SDL_PIXELFORMAT_ARGB8888) };
#else
	SDLSurfaceUniquePtr colors { SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_ARGB8888, 0) };
#endif
	if (!colors)
		return {};
	// Author textures use the same native tone curve, exactly once. Streaming
	// base/foreground/cursor textures already contain the active palette colors.
	for (int y = 0; y < colors->h; ++y) {
		auto *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(colors->pixels) + y * colors->pitch);
		for (int x = 0; x < colors->w; ++x) {
			const uint32_t color = row[x];
			row[x] = (color & 0xFF000000U)
			    | (static_cast<uint32_t>(BrightnessLut[(color >> 16) & 255U]) << 16)
			    | (static_cast<uint32_t>(BrightnessLut[(color >> 8) & 255U]) << 8)
			    | BrightnessLut[color & 255U];
		}
	}
#if !defined(USE_SDL3) && !SDL_VERSION_ATLEAST(2, 0, 12)
	const char *hint = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
	const std::string previousHint = hint != nullptr ? hint : "";
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear ? "1" : "0");
#endif
	SDLTextureUniquePtr texture { SDL_CreateTextureFromSurface(renderer, colors.get()) };
#if !defined(USE_SDL3) && !SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, previousHint.c_str());
#endif
	if (!texture || !ConfigureTexture(texture.get(), linear, opaque))
		return {};
	return texture;
}

SDLTextureUniquePtr MakeStreaming(SDL_Renderer *renderer, int width, int height, bool opaque = false)
{
#if !defined(USE_SDL3) && !SDL_VERSION_ATLEAST(2, 0, 12)
	const char *hint = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
	const std::string previousHint = hint != nullptr ? hint : "";
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif
	SDLTextureUniquePtr texture { SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height) };
#if !defined(USE_SDL3) && !SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, previousHint.c_str());
#endif
	if (!texture || !ConfigureTexture(texture.get(), false, opaque))
		return {};
	return texture;
}

bool SourceFits(SDL_Surface *image, const SDL_Rect &source)
{
	return image != nullptr && image->w > 0 && image->h > 0 && image->w <= 4096 && image->h <= 4096
	    && source.x >= 0 && source.y >= 0 && source.w > 0 && source.h > 0
	    && source.x + source.w <= image->w && source.y + source.h <= image->h;
}

SDLTextureUniquePtr LoadTexture(SDL_Renderer *renderer, const char *path, SDL_Rect source)
{
	if (!FindAsset(path).ok())
		return {};
	SDLSurfaceUniquePtr image { LoadPNG(path) };
	if (!SourceFits(image.get(), source))
		return {};
	return MakeTexture(renderer, image.get(), true);
}

bool LoadLiquid(SDL_Renderer *renderer, size_t index)
{
	const char *path = index == 0 ? "d3d-ui/hud/r1/orb-red.png" : "d3d-ui/hud/r1/orb-blue.png";
	if (!FindAsset(path).ok())
		return false;
	SDLSurfaceUniquePtr image { LoadPNG(path) };
	const auto source = LiquidSources[index];
	if (!SourceFits(image.get(), source))
		return false;
#ifdef USE_SDL3
	SDLSurfaceUniquePtr colors { SDL_ConvertSurface(image.get(), SDL_PIXELFORMAT_ARGB8888) };
#else
	SDLSurfaceUniquePtr colors { SDL_ConvertSurfaceFormat(image.get(), SDL_PIXELFORMAT_ARGB8888, 0) };
#endif
	if (!colors)
		return false;
	// The measured crops differ by a single pixel. A central square and uniform
	// scaling preserve a round opening instead of deforming either liquid.
	const int side = std::min(source.w, source.h);
	const int left = source.x + (source.w - side) / 2;
	const int top = source.y + (source.h - side) / 2;
	const float radius = (side - 1) / 2.0F;
	std::vector<uint32_t> pixels(static_cast<size_t>(side) * side);
	std::vector<uint32_t> empty;
	if (index == 0)
		empty.resize(pixels.size());
	for (int y = 0; y < side; ++y) {
		const auto *row = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(colors->pixels) + (top + y) * colors->pitch);
		for (int x = 0; x < side; ++x) {
			const float dx = x - radius, dy = y - radius;
			const float edge = std::clamp(radius + 0.5F - std::sqrt(dx * dx + dy * dy), 0.0F, 1.0F);
			const uint32_t color = row[left + x];
			const uint32_t alpha = static_cast<uint32_t>((color >> 24) * edge);
			pixels[static_cast<size_t>(y) * side + x] = (color & 0x00FFFFFFU) | (alpha << 24);
			if (index == 0) {
				// Retain only the shared neutral reflection: no red identity or
				// resource remains visible when empty, yet the glass has depth.
				const uint32_t minimum = std::min({ (color >> 16) & 255U, (color >> 8) & 255U, color & 255U });
				const uint32_t shade = static_cast<uint32_t>(minimum * 0.30F);
				empty[static_cast<size_t>(y) * side + x] = (static_cast<uint32_t>(255 * edge) << 24)
				    | ((4 + shade) << 16) | ((5 + shade) << 8) | (7 + shade);
			}
		}
	}
#ifdef USE_SDL3
	SDLSurfaceUniquePtr crop { SDL_CreateSurfaceFrom(side, side, SDL_PIXELFORMAT_ARGB8888, pixels.data(), side * 4) };
#else
	SDLSurfaceUniquePtr crop { SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), side, side, 32, side * 4, SDL_PIXELFORMAT_ARGB8888) };
#endif
	if (!crop)
		return false;
	LiquidTextures[index] = MakeTexture(renderer, crop.get(), true);
	if (!LiquidTextures[index])
		return false;
	LiquidSizes[index] = side;
	if (index == 0) {
#ifdef USE_SDL3
		SDLSurfaceUniquePtr emptyImage { SDL_CreateSurfaceFrom(side, side, SDL_PIXELFORMAT_ARGB8888, empty.data(), side * 4) };
#else
		SDLSurfaceUniquePtr emptyImage { SDL_CreateRGBSurfaceWithFormatFrom(empty.data(), side, side, 32, side * 4, SDL_PIXELFORMAT_ARGB8888) };
#endif
		if (!emptyImage)
			return false;
		EmptyOrbTexture = MakeTexture(renderer, emptyImage.get(), true);
		EmptyOrbSize = side;
		if (!EmptyOrbTexture)
			return false;
	}
	return true;
}

bool EnsureAssets(SDL_Renderer *renderer)
{
	const int brightness = *GetOptions().Graphics.brightness;
	if (Owner != renderer || CachedBrightness != brightness) {
		ResetTextures();
		Owner = renderer;
		CachedBrightness = brightness;
		std::array<SDL_Color, 256> gray {};
		std::array<SDL_Color, 256> mapped {};
		for (size_t i = 0; i < gray.size(); ++i) {
			const auto value = static_cast<uint8_t>(i);
			gray[i] = { value, value, value, SDL_ALPHA_OPAQUE };
		}
		ApplyGlobalBrightness(mapped.data(), gray.data());
		for (size_t i = 0; i < BrightnessLut.size(); ++i)
			BrightnessLut[i] = mapped[i].r;
	}
	if (AssetsAttempted)
		return AssetsReady;
	AssetsAttempted = true;
	if (!ChassisTexture)
		ChassisTexture = LoadTexture(renderer, Chassis.path, Chassis.source);
	if (!ButtonTexture)
		ButtonTexture = LoadTexture(renderer, Button.path, Button.source);
	if (!InsetTexture)
		InsetTexture = LoadTexture(renderer, Inset.path, Inset.source);
	if (!OrbFrameTexture)
		OrbFrameTexture = LoadTexture(renderer, "d3d-ui/hud/r1/orb-frame.png", OrbFrameSource);
	for (size_t i = 0; i < LiquidTextures.size(); ++i) {
		if (!LiquidTextures[i] && !LoadLiquid(renderer, i))
			return false;
	}
	uint32_t tintPixel = 0x20FFFFFFU;
#ifdef USE_SDL3
	SDLSurfaceUniquePtr tintImage { SDL_CreateSurfaceFrom(1, 1, SDL_PIXELFORMAT_ARGB8888, &tintPixel, 4) };
#else
	SDLSurfaceUniquePtr tintImage { SDL_CreateRGBSurfaceWithFormatFrom(&tintPixel, 1, 1, 32, 4, SDL_PIXELFORMAT_ARGB8888) };
#endif
	if (tintImage)
		BeltTintTexture = MakeTexture(renderer, tintImage.get(), false);
	AssetsReady = ChassisTexture && ButtonTexture && InsetTexture && OrbFrameTexture && EmptyOrbTexture && BeltTintTexture;
	return AssetsReady;
}

bool EnsureRegionTexture(SDL_Renderer *renderer, SDLTextureUniquePtr &texture, const SDL_Rect &bounds,
    int &width, int &height, bool opaque = false, bool *recreated = nullptr)
{
	if (recreated != nullptr)
		*recreated = false;
	if (bounds.w <= 0 || bounds.h <= 0)
		return true;
	if (texture && width == bounds.w && height == bounds.h)
		return true;
	texture = MakeStreaming(renderer, bounds.w, bounds.h, opaque);
	if (!texture)
		return false;
	width = bounds.w;
	height = bounds.h;
	if (recreated != nullptr)
		*recreated = true;
	return true;
}

bool InitializeTransparentStreaming(SDL_Texture *texture, int width, int height)
{
	void *pixels;
	int pitch;
#ifdef USE_SDL3
	if (!SDL_LockTexture(texture, nullptr, &pixels, &pitch))
#else
	if (SDL_LockTexture(texture, nullptr, &pixels, &pitch) < 0)
#endif
		return false;
	for (int y = 0; y < height; ++y)
		std::memset(static_cast<uint8_t *>(pixels) + y * pitch, 0, static_cast<size_t>(width) * 4);
	SDL_UnlockTexture(texture);
	return true;
}

void ConvertIndexed(const Surface &surface, const SDL_Rect &region, const std::array<uint32_t, 256> &colors, bool transparent,
    std::vector<uint32_t> &pixels, const std::vector<uint8_t> *mask = nullptr)
{
	pixels.resize(static_cast<size_t>(region.w) * region.h);
	for (int y = 0; y < region.h; ++y) {
		const auto *source = surface.at(region.x, region.y + y);
		const size_t sourceRow = static_cast<size_t>(region.y + y) * surface.w() + region.x;
		const size_t destinationRow = static_cast<size_t>(y) * region.w;
		for (int x = 0; x < region.w; ++x) {
			const bool empty = mask != nullptr ? (*mask)[sourceRow + x] == 0 : transparent && source[x] == TransparentColor;
			pixels[destinationRow + x] = empty ? 0 : colors[source[x]];
		}
	}
}

bool Upload(SDL_Texture *texture, const std::vector<uint32_t> &pixels, int width, const SDL_Rect *region = nullptr)
{
#ifdef USE_SDL3
	return SDL_UpdateTexture(texture, region, pixels.data(), width * 4);
#else
	return SDL_UpdateTexture(texture, region, pixels.data(), width * 4) >= 0;
#endif
}

bool DrawTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *source, FloatRect destination, bool flip = false)
{
	if (destination.w <= 0 || destination.h <= 0)
		return true;
#ifdef USE_SDL3
	const SDL_FRect target { destination.x, destination.y, destination.w, destination.h };
	SDL_FRect sourceFloat {};
	if (source != nullptr)
		sourceFloat = { static_cast<float>(source->x), static_cast<float>(source->y), static_cast<float>(source->w), static_cast<float>(source->h) };
	return SDL_RenderTextureRotated(renderer, texture, source != nullptr ? &sourceFloat : nullptr, &target, 0, nullptr,
	    flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
#elif SDL_VERSION_ATLEAST(2, 0, 10)
	const SDL_FRect target { destination.x, destination.y, destination.w, destination.h };
	return SDL_RenderCopyExF(renderer, texture, source, &target, 0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) >= 0;
#else
	const SDL_Rect target { static_cast<int>(std::lround(destination.x)), static_cast<int>(std::lround(destination.y)),
		static_cast<int>(std::lround(destination.x + destination.w) - std::lround(destination.x)),
		static_cast<int>(std::lround(destination.y + destination.h) - std::lround(destination.y)) };
	return target.w <= 0 || target.h <= 0 || SDL_RenderCopyEx(renderer, texture, source, &target, 0, nullptr,
	    flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) >= 0;
#endif
}

FloatRect AsFloat(Rectangle rect)
{
	return { static_cast<float>(rect.position.x), static_cast<float>(rect.position.y), static_cast<float>(rect.size.width), static_cast<float>(rect.size.height) };
}

bool DrawNineSlice(SDL_Renderer *renderer, SDL_Texture *texture, const Slice &slice, Rectangle destination)
{
	const float scale = static_cast<float>(Base->h()) / 480;
	const float left = std::min(slice.logicalMargins[0] * scale, destination.size.width / 2.0F);
	const float top = std::min(slice.logicalMargins[1] * scale, destination.size.height / 2.0F);
	const float right = std::min(slice.logicalMargins[2] * scale, destination.size.width - left);
	const float bottom = std::min(slice.logicalMargins[3] * scale, destination.size.height - top);
	const std::array<int, 4> sx { slice.source.x, slice.source.x + slice.sourceMargins[0],
		slice.source.x + slice.source.w - slice.sourceMargins[2], slice.source.x + slice.source.w };
	const std::array<int, 4> sy { slice.source.y, slice.source.y + slice.sourceMargins[1],
		slice.source.y + slice.source.h - slice.sourceMargins[3], slice.source.y + slice.source.h };
	const std::array<float, 4> dx { static_cast<float>(destination.position.x), destination.position.x + left,
		destination.position.x + destination.size.width - right, static_cast<float>(destination.position.x + destination.size.width) };
	const std::array<float, 4> dy { static_cast<float>(destination.position.y), destination.position.y + top,
		destination.position.y + destination.size.height - bottom, static_cast<float>(destination.position.y + destination.size.height) };
	for (size_t y = 0; y < 3; ++y) {
		for (size_t x = 0; x < 3; ++x) {
			const SDL_Rect source { sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y] };
			if (!DrawTexture(renderer, texture, &source, { dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y] }))
				return false;
		}
	}
	return true;
}

bool SetShade(SDL_Texture *texture, uint8_t shade)
{
#ifdef USE_SDL3
	return SDL_SetTextureColorMod(texture, shade, shade, shade);
#else
	return SDL_SetTextureColorMod(texture, shade, shade, shade) >= 0;
#endif
}

struct OrbPlacement {
	FloatRect frame;
	float centerX, centerY, radius;
};

OrbPlacement PlaceOrb(bool mana)
{
	const Rectangle target = GetD3dHudOrbRect(mana);
	const float scale = std::min(static_cast<float>(target.size.width) / OrbFrameSource.w,
	    static_cast<float>(target.size.height) / OrbFrameSource.h);
	const float width = OrbFrameSource.w * scale, height = OrbFrameSource.h * scale;
	const FloatRect frame { target.position.x + (target.size.width - width) / 2,
		target.position.y + target.size.height - height, width, height };
	const float localCenter = (OrbCircleX - OrbFrameSource.x) * scale;
	const float centerX = frame.x + (mana ? width - localCenter : localCenter);
	const float centerY = frame.y + (OrbCircleY - OrbFrameSource.y) * scale;
	const float radius = (OrbCircleRadius + 2) * scale;
	return { frame, centerX, centerY, radius };
}

bool DrawOrb(SDL_Renderer *renderer, bool mana)
{
	const auto [frame, centerX, centerY, radius] = PlaceOrb(mana);
	const FloatRect globe { centerX - radius, centerY - radius, radius * 2, radius * 2 };
	const SDL_Rect emptySource { 0, 0, EmptyOrbSize, EmptyOrbSize };
	if (!DrawTexture(renderer, EmptyOrbTexture.get(), &emptySource, globe))
		return false;
	const size_t index = mana ? 1 : 0;
	const int fill = std::clamp(mana ? MyPlayer->_pManaPer : MyPlayer->_pHPPer, 0, 81);
	if (fill > 0) {
		const int side = LiquidSizes[index];
		const int firstRow = side * (81 - fill) / 81;
		const SDL_Rect source { 0, firstRow, side, side - firstRow };
		const float offset = globe.h * firstRow / side;
		if (!DrawTexture(renderer, LiquidTextures[index].get(), &source,
		        { globe.x, globe.y + offset, globe.w, globe.h - offset }))
			return false;
	}
	return DrawTexture(renderer, OrbFrameTexture.get(), &OrbFrameSource, frame, mana);
}

bool DrawSkin(SDL_Renderer *renderer, SDL_Palette *palette)
{
	if (!DrawNineSlice(renderer, ChassisTexture.get(), Chassis, GetD3dHudFrameRect())
	    || !DrawNineSlice(renderer, InsetTexture.get(), Inset, GetD3dHudInfoRect())
	    || !DrawNineSlice(renderer, InsetTexture.get(), Inset, GetD3dHudSpellRect()))
		return false;
	for (int i = 0; i < 8; ++i) {
		if (!DrawNineSlice(renderer, InsetTexture.get(), Inset, GetD3dHudBeltSlotRect(i)))
			return false;
		if (BeltTintRecorded[i]) {
			const auto color = palette->colors[BeltTint[i]];
#ifdef USE_SDL3
			if (!SDL_SetTextureColorMod(BeltTintTexture.get(), color.r, color.g, color.b))
#else
			if (SDL_SetTextureColorMod(BeltTintTexture.get(), color.r, color.g, color.b) < 0)
#endif
				return false;
			const Rectangle slot = GetD3dHudBeltSlotRect(i);
			const Rectangle inside = slot.size.width > 4 && slot.size.height > 4 ? slot.inset({ 2, 2 }) : slot;
			if (!DrawTexture(renderer, BeltTintTexture.get(), nullptr, AsFloat(inside)))
				return false;
		}
	}
	const int count = IsChatAvailable() ? 8 : 6;
	for (int i = 0; i < count; ++i) {
		const Rectangle rect = GetD3dHudPanelButtonRect(i);
		if (!SetShade(ButtonTexture.get(), ButtonPressed[i] ? 184 : rect.contains(MousePosition) ? 255 : 232)
		    || !DrawNineSlice(renderer, ButtonTexture.get(), Button, rect))
			return false;
	}
	return DrawOrb(renderer, false) && DrawOrb(renderer, true);
}

} // namespace

Rectangle GetD3dHudPresentationValueRect(bool mana)
{
	const auto placement = PlaceOrb(mana);
	const int width = std::max(1, static_cast<int>(placement.radius * 2));
	const int height = std::clamp(14 * GetScreenHeight() / 480, 1, width);
	return { { static_cast<int>(std::lround(placement.centerX - width / 2.0F)),
		         static_cast<int>(std::lround(placement.centerY - height / 2.0F)) }, { width, height } };
}

void BeginD3dHudPresentationFrame(const Surface &base)
{
	FrameValid = false;
	FrameSource = nullptr;
	CapturedRegion = {};
	CoverageReady = false;
	UiBlocked = false;
	CursorUpdated = false;
	NativeUiCaptureActive = false;
	ButtonRecorded.fill(false);
	BeltTintRecorded.fill(false);
	Footprints.clear();
	ForegroundBounds = {};
	if (!IsD3dHudEnabled() || !IsIndexed(base) || base.w() > 8192 || base.h() > 8192
	    || static_cast<size_t>(base.w()) * base.h() > MaxFramePixels
	    || !EnsureSurface(Base, base.w(), base.h()) || !EnsureSurface(Foreground, base.w(), base.h()))
		return;
	for (int y = 0; y < base.h(); ++y) {
		std::memcpy(Base->at(0, y), base.at(0, y), base.w());
		std::memset(Foreground->at(0, y), TransparentColor, base.w());
	}
	ForegroundMask.assign(static_cast<size_t>(base.w()) * base.h(), 0);
	Footprints = CurrentFootprints();
	FrameSource = base.surface;
	CapturedRegion = base.region;
	FrameValid = true;
}

void RecordD3dHudForeground(const Surface &source, Rectangle target, uint8_t key)
{
	if (!FrameValid)
		return;
	if (!IsIndexed(source) || target.size.width <= 0 || target.size.height <= 0) {
		FrameValid = false;
		return;
	}
	SDL_Rect clipped;
	if (!Clip(target, Foreground->w(), Foreground->h(), clipped))
		return;
	for (int y = clipped.y; y < clipped.y + clipped.h; ++y) {
		const int sy = (y - target.position.y) * source.h() / target.size.height;
		auto *destination = Foreground->at(clipped.x, y);
		int left = clipped.x + clipped.w;
		int right = clipped.x;
		for (int x = clipped.x; x < clipped.x + clipped.w; ++x) {
			const uint8_t color = *source.at((x - target.position.x) * source.w() / target.size.width, sy);
			if (color != key) {
				destination[x - clipped.x] = color;
				ForegroundMask[static_cast<size_t>(y) * Foreground->w() + x] = 1;
				left = std::min(left, x);
				right = x + 1;
			}
		}
		IncludeBounds(ForegroundBounds, { left, y, right - left, 1 });
	}
}

void ClearD3dHudForeground(Rectangle target)
{
	SDL_Rect clipped;
	if (!FrameValid || !Clip(target, Foreground->w(), Foreground->h(), clipped))
		return;
	for (int y = clipped.y; y < clipped.y + clipped.h; ++y) {
		std::memset(Foreground->at(clipped.x, y), TransparentColor, clipped.w);
		std::memset(ForegroundMask.data() + static_cast<size_t>(y) * Foreground->w() + clipped.x, 0, clipped.w);
	}
}

void RecordD3dHudButtonState(int button, bool pressed)
{
	if (!FrameValid || button < 0 || button >= static_cast<int>(ButtonPressed.size()))
		return;
	ButtonPressed[button] = pressed;
	ButtonRecorded[button] = true;
}

void RecordD3dHudBeltTint(int slot, uint8_t paletteIndex)
{
	if (!FrameValid || slot < 0 || slot >= static_cast<int>(BeltTint.size()))
		return;
	BeltTint[slot] = paletteIndex;
	BeltTintRecorded[slot] = true;
}

void RecordD3dHudExperienceBar()
{
	if (!FrameValid)
		return;
	DrawXPBar(*Foreground);
	const Rectangle &panel = GetMainPanel();
	const Rectangle bar { { panel.position.x + panel.size.width / 2 - 155,
		panel.position.y + panel.size.height - 11 }, { 313, 9 } };
	SDL_Rect clipped;
	if (!Clip(bar, Foreground->w(), Foreground->h(), clipped))
		return;
	for (int y = clipped.y; y < clipped.y + clipped.h; ++y) {
		int left = clipped.x + clipped.w;
		int right = clipped.x;
		for (int x = clipped.x; x < clipped.x + clipped.w; ++x) {
			if (*Foreground->at(x, y) != TransparentColor) {
				ForegroundMask[static_cast<size_t>(y) * Foreground->w() + x] = 1;
				left = std::min(left, x);
				right = x + 1;
			}
		}
		IncludeBounds(ForegroundBounds, { left, y, right - left, 1 });
	}
}

void SetD3dHudPresentationUiCoverage(std::span<const SDL_Rect> nonHudRegions)
{
	CoverageReady = FrameValid;
	for (const auto &region : nonHudRegions) {
		for (const auto &footprint : Footprints) {
			if (Intersects(region, footprint)) {
				UiBlocked = true;
				return;
			}
		}
	}
}

void SetD3dHudPresentationNativeUiCapture(bool active)
{
	NativeUiCaptureActive = active && FrameValid;
}

namespace detail {
void RecordD3dHudPresentationNativeUiRect(const SDL_Rect &region)
{
	if (!FrameValid || !NativeUiCaptureActive)
		return;
	for (const auto &footprint : Footprints) {
		if (Intersects(region, footprint)) {
			UiBlocked = true;
			return;
		}
	}
}
} // namespace detail

void ClearD3dHudPresentationCursor()
{
	Cursor.reset();
	CursorMask.clear();
	CursorUpdated = true;
}

void CaptureD3dHudPresentationCursor(Rectangle clippedBounds, Point spriteBottomLeft, int cursorId)
{
	Cursor.reset();
	CursorMask.clear();
	CursorUpdated = true;
	if (!FrameValid || clippedBounds.size.width <= 0 || clippedBounds.size.height <= 0)
		return;
	if (clippedBounds.size.width > 1024 || clippedBounds.size.height > 1024
	    || !EnsureSurface(Cursor, clippedBounds.size.width, clippedBounds.size.height)) {
		CursorUpdated = false;
		return;
	}
	for (int y = 0; y < Cursor->h(); ++y)
		std::memset(Cursor->at(0, y), TransparentColor, Cursor->w());
	// Same palette contract as SetHardwareCursorFromSprite: UI sprites never
	// use indices 1..127, so black (0) remains real, opaque cursor coverage.
	DrawSoftwareCursor(*Cursor, spriteBottomLeft - Displacement { clippedBounds.position.x, clippedBounds.position.y }, cursorId);
	CursorPosition = clippedBounds.position;
}

void CaptureD3dHudPresentationCursorPixels(const Surface &source, Rectangle clippedBounds,
    std::span<const uint8_t> opacity)
{
	Cursor.reset();
	CursorMask.clear();
	CursorUpdated = true;
	if (!FrameValid)
		return;
	const int width = clippedBounds.size.width, height = clippedBounds.size.height;
	if (source.surface == nullptr || source.surface->pixels == nullptr || SDLC_SURFACE_BITSPERPIXEL(source.surface) != 8
	    || source.region.x < 0 || source.region.y < 0 || source.w() < 0 || source.h() < 0
	    || source.region.x > source.surface->w - source.w() || source.region.y > source.surface->h - source.h()
	    || width <= 0 || height <= 0 || width > 1024 || height > 1024
	    || clippedBounds.position.x < 0 || clippedBounds.position.y < 0
	    || clippedBounds.position.x > source.w() - width || clippedBounds.position.y > source.h() - height
	    || opacity.size() != static_cast<size_t>(width) * height
	    || !EnsureSurface(Cursor, width, height)) {
		CursorUpdated = false;
		return;
	}
	CursorMask.assign(opacity.begin(), opacity.end());
	for (int y = 0; y < height; ++y) {
		const auto *pixels = source.at(clippedBounds.position.x, clippedBounds.position.y + y);
		auto *target = Cursor->at(0, y);
		for (int x = 0; x < width; ++x)
			target[x] = opacity[static_cast<size_t>(y) * width + x] != 0 ? pixels[x] : TransparentColor;
	}
	CursorPosition = clippedBounds.position;
}

D3dHudPresentationResult RenderD3dHudPresentation(SDL_Renderer *renderer, SDL_Surface *indexedUi,
    SDL_Palette *palette, bool allowTownLayers, bool townLayersPresented)
{
	if (!allowTownLayers || renderer == nullptr || !FrameValid || !CoverageReady || UiBlocked || !CursorUpdated || NativeUiCaptureActive
	    || !IsD3dHudEnabled() || MyPlayer == nullptr || InspectPlayer == nullptr || indexedUi == nullptr || indexedUi != FrameSource
	    || SDLC_SURFACE_BITSPERPIXEL(indexedUi) != 8 || palette == nullptr || palette->ncolors < 256
	    || CapturedRegion.x < 0 || CapturedRegion.y < 0
	    || CapturedRegion.x + Base->w() > indexedUi->w || CapturedRegion.y + Base->h() > indexedUi->h
	    || Base->w() != GetScreenWidth() || Base->h() != GetScreenHeight() || !SameFootprints())
		return D3dHudPresentationResult::Inactive;
	// PrepareForFadeIn can present with allowTownLayers=true immediately after
	// BlackPalette. Author RGB must not flash over that preparatory black frame.
	if (std::all_of(palette->colors, palette->colors + 256, [](const SDL_Color &color) {
		    return color.r == 0 && color.g == 0 && color.b == 0;
	    }))
		return D3dHudPresentationResult::Inactive;
	const auto ui = GetUiOverlayFrame();
	if (ui.source != FrameSource || ui.sourceRegion.x != CapturedRegion.x || ui.sourceRegion.y != CapturedRegion.y
	    || ui.sourceRegion.w != Base->w() || ui.sourceRegion.h != Base->h())
		return D3dHudPresentationResult::Inactive;
	const int count = IsChatAvailable() ? 8 : 6;
	if (!std::all_of(ButtonRecorded.begin(), ButtonRecorded.begin() + count, [](bool recorded) { return recorded; }))
		return D3dHudPresentationResult::Inactive;
	for (size_t i = 0; i < BeltTintRecorded.size(); ++i) {
		if (!InspectPlayer->SpdList[i].isEmpty() && !BeltTintRecorded[i])
			return D3dHudPresentationResult::Inactive;
	}
	// A retained world must be restored from its uploaded 2x texture, never
	// from this frame's logical snapshot after a failed Town presentation.
	if (GetTownViewHighResolutionFrame() != nullptr && !townLayersPresented)
		return D3dHudPresentationResult::Inactive;
	SDL_Rect baseBounds {};
	for (const auto &footprint : Footprints) {
		SDL_Rect clipped;
		if (Clip(MakeRectangle(footprint), Base->w(), Base->h(), clipped))
			IncludeBounds(baseBounds, clipped);
	}
	// A cropped foreground quad changes SDL software's nearest sampling phase
	// at fractional logical scales. Keep its full-canvas texture/quad as before,
	// while converting and uploading only current/previous foreground coverage.
	const SDL_Rect foregroundCanvas { 0, 0, Foreground->w(), Foreground->h() };
	bool foregroundRecreated = false;
	if (!EnsureAssets(renderer)
	    || !EnsureRegionTexture(renderer, ForegroundTexture, foregroundCanvas, ForegroundTextureWidth, ForegroundTextureHeight, false, &foregroundRecreated))
		return D3dHudPresentationResult::Inactive;
	if (foregroundRecreated) {
		UploadedForegroundBounds = {};
		if (!InitializeTransparentStreaming(ForegroundTexture.get(), ForegroundTextureWidth, ForegroundTextureHeight)) {
			ForegroundTexture.reset();
			return D3dHudPresentationResult::Inactive;
		}
	}
	if (!townLayersPresented && !EnsureRegionTexture(renderer, BaseTexture, baseBounds, BaseTextureWidth, BaseTextureHeight, true))
		return D3dHudPresentationResult::Inactive;
	std::array<uint32_t, 256> colors {};
	for (size_t i = 0; i < colors.size(); ++i) {
		const auto &color = palette->colors[i];
		colors[i] = 0xFF000000U | (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
	}
	SDL_Rect foregroundUpload = ForegroundBounds;
	IncludeBounds(foregroundUpload, UploadedForegroundBounds);
	if (foregroundUpload.w > 0 && foregroundUpload.h > 0) {
		ConvertIndexed(*Foreground, foregroundUpload, colors, true, ForegroundColors, &ForegroundMask);
		// A failed SDL upload may have written part of the region. Retain the
		// union until a successful upload has cleared all previous coverage.
		UploadedForegroundBounds = foregroundUpload;
		if (!Upload(ForegroundTexture.get(), ForegroundColors, foregroundUpload.w, &foregroundUpload))
			return D3dHudPresentationResult::Inactive;
		UploadedForegroundBounds = ForegroundBounds;
	}
	if (!townLayersPresented && baseBounds.w > 0 && baseBounds.h > 0) {
		ConvertIndexed(*Base, baseBounds, colors, false, BaseColors);
		if (!Upload(BaseTexture.get(), BaseColors, baseBounds.w))
			return D3dHudPresentationResult::Inactive;
	}
	if (Cursor) {
		if (!CursorTexture || CursorWidth != Cursor->w() || CursorHeight != Cursor->h()) {
			CursorTexture = MakeStreaming(renderer, Cursor->w(), Cursor->h());
			if (!CursorTexture)
				return D3dHudPresentationResult::Inactive;
			CursorWidth = Cursor->w();
			CursorHeight = Cursor->h();
		}
		ConvertIndexed(*Cursor, { 0, 0, Cursor->w(), Cursor->h() }, colors, true, CursorColors,
		    CursorMask.empty() ? nullptr : &CursorMask);
		if (!Upload(CursorTexture.get(), CursorColors, Cursor->w()))
			return D3dHudPresentationResult::Inactive;
	}
	// Everything above is preparation. Only from here can Failed require the
	// caller to cover a partially composed result with its full native texture.
	if (townLayersPresented) {
		if (!RestoreTownPresentationWorldRegions(renderer, Footprints))
			return D3dHudPresentationResult::Failed;
	} else {
		for (const auto &footprint : Footprints) {
			SDL_Rect clipped;
			if (!Clip(MakeRectangle(footprint), Base->w(), Base->h(), clipped))
				continue;
			const SDL_Rect source { clipped.x - baseBounds.x, clipped.y - baseBounds.y, clipped.w, clipped.h };
			if (!DrawTexture(renderer, BaseTexture.get(), &source, AsFloat(MakeRectangle(clipped))))
				return D3dHudPresentationResult::Failed;
		}
	}
	if (!DrawSkin(renderer, palette) || !DrawTexture(renderer, ForegroundTexture.get(), nullptr,
	        { 0, 0, static_cast<float>(Base->w()), static_cast<float>(Base->h()) }))
		return D3dHudPresentationResult::Failed;
	if (Cursor && !DrawTexture(renderer, CursorTexture.get(), nullptr,
	                  { static_cast<float>(CursorPosition.x), static_cast<float>(CursorPosition.y), static_cast<float>(Cursor->w()), static_cast<float>(Cursor->h()) }))
		return D3dHudPresentationResult::Failed;
	return D3dHudPresentationResult::Presented;
}

void InvalidateD3dHudPresentationFrame()
{
	Cursor.reset();
	CursorMask.clear();
	FrameSource = nullptr;
	CapturedRegion = {};
	FrameValid = false;
	CoverageReady = false;
	UiBlocked = true;
	CursorUpdated = false;
	NativeUiCaptureActive = false;
	ButtonRecorded.fill(false);
	BeltTintRecorded.fill(false);
	Footprints.clear();
	ForegroundBounds = {};
}

void ResetD3dHudPresentationResources()
{
	InvalidateD3dHudPresentationFrame();
	ResetTextures();
	Base.reset();
	Foreground.reset();
	ForegroundMask.clear();
}

} // namespace devilution
#else
namespace devilution {
void BeginD3dHudPresentationFrame(const Surface &) { }
void RecordD3dHudForeground(const Surface &, Rectangle, uint8_t) { }
void ClearD3dHudForeground(Rectangle) { }
void RecordD3dHudButtonState(int, bool) { }
void RecordD3dHudBeltTint(int, uint8_t) { }
void RecordD3dHudExperienceBar() { }
Rectangle GetD3dHudPresentationValueRect(bool mana) { return GetD3dHudValueRect(mana); }
void SetD3dHudPresentationUiCoverage(std::span<const SDL_Rect>) { }
void SetD3dHudPresentationNativeUiCapture(bool) { }
namespace detail {
void RecordD3dHudPresentationNativeUiRect(const SDL_Rect &) { }
} // namespace detail
void ClearD3dHudPresentationCursor() { }
void CaptureD3dHudPresentationCursor(Rectangle, Point, int) { }
void CaptureD3dHudPresentationCursorPixels(const Surface &, Rectangle, std::span<const uint8_t>) { }
D3dHudPresentationResult RenderD3dHudPresentation(SDL_Renderer *, SDL_Surface *, SDL_Palette *, bool, bool)
{
	return D3dHudPresentationResult::Inactive;
}
void ResetD3dHudPresentationResources() { }
void InvalidateD3dHudPresentationFrame() { }
} // namespace devilution
#endif
