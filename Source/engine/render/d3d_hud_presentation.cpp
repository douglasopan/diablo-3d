#include "engine/render/d3d_hud_presentation.hpp"
#include "engine/render/d3d_panel_presentation.hpp"

#include "control/d3d_hud.hpp"
#include "control/d3d_hud_geometry.hpp"
#include "control/d3d_hud_skin.hpp"

#include <algorithm>
#include <cmath>

#include "utils/display.h"

#ifndef USE_SDL1
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <new>
#include <string>
#include <vector>

#include "control/control.hpp"
#include "control/control_panel.hpp"
#include "cursor.h"
#include "diablo.h"
#include "engine/palette.h"
#include "engine/render/town_presentation.hpp"
#include "engine/render/town_view.hpp"
#include "engine/surface.hpp"
#include "inv.h"
#include "options.h"
#include "player.h"
#include "qol/xpbar.h"
#include "utils/display.h"
#include "utils/sdl_ptrs.h"
#include "utils/sdl_geometry.h"

namespace devilution {
namespace {

constexpr uint8_t TransparentColor = 1;
constexpr size_t MaxFramePixels = 16 * 1024 * 1024;

using FloatRect = D3dHudSkinFloatRect;
using Slice = D3dHudSkinSlice;

// Derived from d3d-ui/hud/r1/skin.json. Keep these source measurements and
// Godot's metadata in agreement; all destination geometry still uses the
// existing thirteen layout entries, never PNG padding or a new input region.
const Slice &Chassis = GetD3dHudSkinSlice(D3dHudSkinPart::Chassis);
const Slice &Button = GetD3dHudSkinSlice(D3dHudSkinPart::Button);
const Slice &Inset = GetD3dHudSkinSlice(D3dHudSkinPart::Inset);
const SDL_Rect OrbFrameSource = GetD3dHudSkinOrbFrameSource();

struct FrameRecipe {
	Rectangle chassis;
	Rectangle info;
	Rectangle spell;
	std::array<Rectangle, 8> belt;
	std::array<Rectangle, 8> beltInteriors;
	std::array<Rectangle, 8> buttons;
	std::array<Rectangle, 2> orbs;
	std::array<int, 2> fill {};
	std::array<bool, 8> pressed {};
	std::array<uint8_t, 8> shade {};
	std::array<uint8_t, 8> tint {};
	std::array<bool, 8> tinted {};
	int buttonCount = 6;
	float designScaleX = 1;
	float designScaleY = 1;
	float valueGlyphScaleY = 1;
};
FrameRecipe Recipe;
bool RecipeReady = false;
bool FrameMaterialized = false;
bool Materializing = false;
bool ForegroundSealed = false;
bool FrameFinalized = false;
bool Prepared = false;
bool PreparedWithTown = false;
bool SubmissionStarted = false;
SDL_Rect PreparedBaseBounds {};
std::array<SDL_Color, 256> PreparedPalette {};
void (*BeforeRecoveryCursor)(const Surface &) = nullptr;
void (*AfterRecoveryCursor)(const Surface &) = nullptr;
D3dHudPresentationCounters Counters;
D3dHudPresentationFault Fault = D3dHudPresentationFault::None;
uint64_t FaultOrdinal = 1;
uint64_t FaultUpload = 0;
uint64_t FaultAllocation = 0;
uint64_t FaultDraw = 0;
uint64_t FaultCursorSurface = 0;
uint64_t FaultCursorMask = 0;

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
std::array<bool, 8> ButtonRecorded {};
std::array<bool, 8> BeltTintRecorded {};
std::vector<SDL_Rect> Footprints;

SDL_Renderer *Owner = nullptr;
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
	result.reserve(6);
	result.push_back(MakeSdlRect(GetD3dHudFrameRect()));
	result.push_back(MakeSdlRect(GetD3dHudOrbRect(false)));
	result.push_back(MakeSdlRect(GetD3dHudOrbRect(true)));
	if (IsChatAvailable()) {
		result.push_back(MakeSdlRect(GetD3dHudPanelButtonRect(6)));
		result.push_back(MakeSdlRect(GetD3dHudPanelButtonRect(7)));
	}
	if (*GetOptions().Gameplay.experienceBar && !ChatFlag) {
		const Rectangle &panel = GetMainPanel();
		result.push_back({ panel.position.x + panel.size.width / 2 - 155,
		    panel.position.y + panel.size.height - 11, 313, 9 });
	}
	return result;
}

void FreezeRecipe()
{
	const auto &metrics = GetD3dHudMetrics();
	const bool enabled = IsD3dHudEnabled();
	Recipe.designScaleX = enabled ? metrics.designScaleX : 1.0F;
	Recipe.designScaleY = enabled ? metrics.designScaleY : 1.0F;
	Recipe.valueGlyphScaleY = enabled ? metrics.valueGlyphScaleY : 1.0F;
	Recipe.chassis = GetD3dHudFrameRect();
	Recipe.info = GetD3dHudInfoRect();
	Recipe.spell = GetD3dHudSpellRect();
	Recipe.buttonCount = IsChatAvailable() ? 8 : 6;
	Recipe.orbs = { GetD3dHudOrbRect(false), GetD3dHudOrbRect(true) };
	Recipe.fill = { std::clamp(MyPlayer->_pHPPer, 0, 81), std::clamp(MyPlayer->_pManaPer, 0, 81) };
	for (int i = 0; i < 8; ++i) {
		Recipe.belt[i] = GetD3dHudBeltSlotRect(i);
		Recipe.beltInteriors[i] = GetD3dHudBeltInteriorRect(Recipe.belt[i]);
		Recipe.buttons[i] = GetD3dHudPanelButtonRect(i);
		Recipe.pressed[i] = IsMainPanelButtonPressed(i);
		Recipe.shade[i] = Recipe.pressed[i] ? 184 : Recipe.buttons[i].contains(MousePosition) ? 255 : 232;
		Recipe.tinted[i] = GetD3dHudBeltTint(i, Recipe.tint[i]);
	}
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

bool AllowTextureAllocation()
{
	++Counters.textureAllocationAttempts;
	++FaultAllocation;
	if (Fault == D3dHudPresentationFault::BeforeTextureAllocation && FaultAllocation == FaultOrdinal) {
		Fault = D3dHudPresentationFault::None;
		++Counters.injectedFailures;
		return false;
	}
	return true;
}

SDLTextureUniquePtr MakeTexture(SDL_Renderer *renderer, SDL_Surface *image, bool linear, bool opaque = false)
{
	if (!AllowTextureAllocation())
		return {};
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
	++Counters.textureCreations;
	SDLTextureUniquePtr texture { SDL_CreateTextureFromSurface(renderer, colors.get()) };
	if (texture)
		++Counters.textureAllocationSuccesses;
#if !defined(USE_SDL3) && !SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, previousHint.c_str());
#endif
	if (!texture || !ConfigureTexture(texture.get(), linear, opaque))
		return {};
	return texture;
}

SDLTextureUniquePtr MakeStreaming(SDL_Renderer *renderer, int width, int height, bool opaque = false)
{
	if (!AllowTextureAllocation())
		return {};
#if !defined(USE_SDL3) && !SDL_VERSION_ATLEAST(2, 0, 12)
	const char *hint = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
	const std::string previousHint = hint != nullptr ? hint : "";
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif
	++Counters.textureCreations;
	SDLTextureUniquePtr texture { SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height) };
	if (texture)
		++Counters.textureAllocationSuccesses;
#if !defined(USE_SDL3) && !SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, previousHint.c_str());
#endif
	if (!texture || !ConfigureTexture(texture.get(), false, opaque))
		return {};
	return texture;
}

SDLTextureUniquePtr LoadTexture(SDL_Renderer *renderer, D3dHudSkinPart part, bool linear = true)
{
	const auto &image = GetD3dHudSkinImage(part);
	if (image.width <= 0 || image.height <= 0 || image.pixels.empty())
		return {};
#ifdef USE_SDL3
	SDLSurfaceUniquePtr view { SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_ARGB8888,
	    const_cast<uint32_t *>(image.pixels.data()), image.width * 4) };
#else
	SDLSurfaceUniquePtr view { SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint32_t *>(image.pixels.data()),
	    image.width, image.height, 32, image.width * 4, SDL_PIXELFORMAT_ARGB8888) };
#endif
	return view ? MakeTexture(renderer, view.get(), linear) : SDLTextureUniquePtr {};
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
	if (AssetsReady)
		return true;
	// The required CPU cache owns decoding. Renderer recreation and brightness
	// changes reuse those same immutable images instead of loading another skin.
	if (!IsD3dHudSkinLoaded() && !LoadD3dHudSkin())
		return false;
	if (!ChassisTexture)
		ChassisTexture = LoadTexture(renderer, D3dHudSkinPart::Chassis);
	if (!ButtonTexture)
		ButtonTexture = LoadTexture(renderer, D3dHudSkinPart::Button);
	if (!InsetTexture)
		InsetTexture = LoadTexture(renderer, D3dHudSkinPart::Inset);
	if (!OrbFrameTexture)
		OrbFrameTexture = LoadTexture(renderer, D3dHudSkinPart::OrbFrame);
	if (!LiquidTextures[0])
		LiquidTextures[0] = LoadTexture(renderer, D3dHudSkinPart::LifeLiquid);
	if (!LiquidTextures[1])
		LiquidTextures[1] = LoadTexture(renderer, D3dHudSkinPart::ManaLiquid);
	if (!EmptyOrbTexture)
		EmptyOrbTexture = LoadTexture(renderer, D3dHudSkinPart::EmptyOrb);
	if (!BeltTintTexture)
		BeltTintTexture = LoadTexture(renderer, D3dHudSkinPart::BeltTint, false);
	LiquidSizes[0] = GetD3dHudSkinImage(D3dHudSkinPart::LifeLiquid).width;
	LiquidSizes[1] = GetD3dHudSkinImage(D3dHudSkinPart::ManaLiquid).width;
	EmptyOrbSize = GetD3dHudSkinImage(D3dHudSkinPart::EmptyOrb).width;
	AssetsReady = ChassisTexture && ButtonTexture && InsetTexture && OrbFrameTexture
	    && LiquidTextures[0] && LiquidTextures[1] && EmptyOrbTexture && BeltTintTexture;
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

bool BeforeUpload()
{
	++Counters.uploadAttempts;
	++FaultUpload;
	if (Fault == D3dHudPresentationFault::BeforeUpload && FaultUpload == FaultOrdinal) {
		Fault = D3dHudPresentationFault::None;
		++Counters.injectedFailures;
		return false;
	}
	return true;
}

bool AfterUpload(bool uploaded)
{
	if (uploaded)
		++Counters.uploads;
	if (uploaded && Fault == D3dHudPresentationFault::AfterUpload && FaultUpload == FaultOrdinal) {
		Fault = D3dHudPresentationFault::None;
		++Counters.injectedFailures;
		return false;
	}
	return uploaded;
}

bool InitializeTransparentStreaming(SDL_Texture *texture, int width, int height)
{
	if (!BeforeUpload())
		return false;
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
	return AfterUpload(true);
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
	if (!BeforeUpload())
		return false;
#ifdef USE_SDL3
	const bool uploaded = SDL_UpdateTexture(texture, region, pixels.data(), width * 4);
#else
	const bool uploaded = SDL_UpdateTexture(texture, region, pixels.data(), width * 4) >= 0;
#endif
	return AfterUpload(uploaded);
}

bool BeforeSubmit()
{
	if (SubmissionStarted)
		return true;
	if (Fault == D3dHudPresentationFault::BeforeFirstSubmit) {
		Fault = D3dHudPresentationFault::None;
		++Counters.injectedFailures;
		return false;
	}
	return true;
}

bool AfterSubmit(bool success)
{
	if (!success)
		return false;
	const bool first = !SubmissionStarted;
	SubmissionStarted = true;
	if (first && Fault == D3dHudPresentationFault::AfterFirstSubmit) {
		Fault = D3dHudPresentationFault::None;
		++Counters.injectedFailures;
		return false;
	}
	return true;
}

bool DrawTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *source, FloatRect destination, bool flip = false)
{
	if (destination.w <= 0 || destination.h <= 0)
		return true;
	if (!BeforeSubmit())
		return false;
	++FaultDraw;
	if (Fault == D3dHudPresentationFault::BeforeDraw && FaultDraw == FaultOrdinal) {
		Fault = D3dHudPresentationFault::None;
		++Counters.injectedFailures;
		return false;
	}
	++Counters.drawAttempts;
	const bool firstSubmission = !SubmissionStarted;
	if (firstSubmission)
		++Counters.firstSubmissionAttempts;
#ifdef USE_SDL3
	const SDL_FRect target { destination.x, destination.y, destination.w, destination.h };
	SDL_FRect sourceFloat {};
	if (source != nullptr)
		sourceFloat = { static_cast<float>(source->x), static_cast<float>(source->y), static_cast<float>(source->w), static_cast<float>(source->h) };
	const bool drawn = SDL_RenderTextureRotated(renderer, texture, source != nullptr ? &sourceFloat : nullptr, &target, 0, nullptr,
	    flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
#elif SDL_VERSION_ATLEAST(2, 0, 10)
	const SDL_FRect target { destination.x, destination.y, destination.w, destination.h };
	const bool drawn = SDL_RenderCopyExF(renderer, texture, source, &target, 0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) >= 0;
#else
	const SDL_Rect target { static_cast<int>(std::lround(destination.x)), static_cast<int>(std::lround(destination.y)),
		static_cast<int>(std::lround(destination.x + destination.w) - std::lround(destination.x)),
		static_cast<int>(std::lround(destination.y + destination.h) - std::lround(destination.y)) };
	const bool drawn = target.w <= 0 || target.h <= 0 || SDL_RenderCopyEx(renderer, texture, source, &target, 0, nullptr,
	    flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) >= 0;
#endif
	if (drawn) {
		++Counters.draws;
		if (firstSubmission)
			++Counters.firstSubmissionSuccesses;
	}
	if (drawn && Fault == D3dHudPresentationFault::AfterDraw && FaultDraw == FaultOrdinal) {
		Fault = D3dHudPresentationFault::None;
		++Counters.injectedFailures;
		SubmissionStarted = true;
		return false;
	}
	return AfterSubmit(drawn);
}

FloatRect AsFloat(Rectangle rect)
{
	return { static_cast<float>(rect.position.x), static_cast<float>(rect.position.y), static_cast<float>(rect.size.width), static_cast<float>(rect.size.height) };
}

bool DrawNineSlice(SDL_Renderer *renderer, SDL_Texture *texture, const Slice &slice, Rectangle destination)
{
	const float left = std::min(slice.logicalMargins[0] * Recipe.designScaleX, destination.size.width / 2.0F);
	const float top = std::min(slice.logicalMargins[1] * Recipe.designScaleY, destination.size.height / 2.0F);
	const float right = std::min(slice.logicalMargins[2] * Recipe.designScaleX, destination.size.width - left);
	const float bottom = std::min(slice.logicalMargins[3] * Recipe.designScaleY, destination.size.height - top);
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

D3dHudSkinOrbPlacement PlaceOrb(bool mana)
{
	return PlaceD3dHudSkinOrb(RecipeReady ? Recipe.orbs[mana ? 1 : 0] : GetD3dHudOrbRect(mana), mana);
}

bool DrawOrb(SDL_Renderer *renderer, bool mana)
{
	const auto [frame, centerX, centerY, radius] = PlaceOrb(mana);
	const FloatRect globe { centerX - radius, centerY - radius, radius * 2, radius * 2 };
	const SDL_Rect emptySource { 0, 0, EmptyOrbSize, EmptyOrbSize };
	if (!DrawTexture(renderer, EmptyOrbTexture.get(), &emptySource, globe))
		return false;
	const size_t index = mana ? 1 : 0;
	const int fill = Recipe.fill[index];
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

bool DrawSkin(SDL_Renderer *renderer)
{
	if (!DrawNineSlice(renderer, ChassisTexture.get(), Chassis, Recipe.chassis)
	    || !DrawNineSlice(renderer, InsetTexture.get(), Inset, Recipe.info)
	    || !DrawNineSlice(renderer, InsetTexture.get(), Inset, Recipe.spell))
		return false;
	for (int i = 0; i < 8; ++i) {
		if (!DrawNineSlice(renderer, InsetTexture.get(), Inset, Recipe.belt[i]))
			return false;
		if (Recipe.tinted[i]) {
			const auto color = PreparedPalette[Recipe.tint[i]];
#ifdef USE_SDL3
			if (!SDL_SetTextureColorMod(BeltTintTexture.get(), color.r, color.g, color.b))
#else
			if (SDL_SetTextureColorMod(BeltTintTexture.get(), color.r, color.g, color.b) < 0)
#endif
				return false;
			const Rectangle inside = Recipe.beltInteriors[i];
			if (!DrawTexture(renderer, BeltTintTexture.get(), nullptr, AsFloat(inside)))
				return false;
		}
	}
	for (int i = 0; i < Recipe.buttonCount; ++i) {
		const Rectangle rect = Recipe.buttons[i];
		if (!SetShade(ButtonTexture.get(), Recipe.shade[i])
		    || !DrawNineSlice(renderer, ButtonTexture.get(), Button, rect))
			return false;
	}
	return DrawOrb(renderer, false) && DrawOrb(renderer, true);
}

bool CursorBoundsFit(Rectangle bounds)
{
	const int width = bounds.size.width, height = bounds.size.height;
	return width > 0 && height > 0 && width <= 1024 && height <= 1024
	    && bounds.position.x >= 0 && bounds.position.y >= 0
	    && bounds.position.x <= CapturedRegion.w - width
	    && bounds.position.y <= CapturedRegion.h - height;
}

bool AllocateCursorSurface(int width, int height)
{
	++Counters.cursorSurfaceAllocationAttempts;
	++FaultCursorSurface;
	if (Fault == D3dHudPresentationFault::BeforeCursorSurfaceAllocation && FaultCursorSurface == FaultOrdinal) {
		Fault = D3dHudPresentationFault::None;
		++Counters.injectedFailures;
		return false;
	}
	return EnsureSurface(Cursor, width, height);
}

bool AssignCursorMask(std::span<const uint8_t> opacity)
{
	// Only label an allocation when capacity really has to grow. ResetResources
	// releases this capacity so cold allocation faults remain reproducible.
	if (opacity.size() > CursorMask.capacity()) {
		++Counters.cursorMaskAllocationAttempts;
		++FaultCursorMask;
		if (Fault == D3dHudPresentationFault::BeforeCursorMaskAllocation && FaultCursorMask == FaultOrdinal) {
			Fault = D3dHudPresentationFault::None;
			++Counters.injectedFailures;
			return false;
		}
	}
	try {
		CursorMask.assign(opacity.begin(), opacity.end());
	} catch (const std::bad_alloc &) {
		return false;
	}
	return true;
}

void RejectCursorCapture()
{
	++Counters.cursorCaptureRejections;
	Cursor.reset();
	CursorMask.clear();
	// The engine has calculated its new cursor rectangle, but has NOT saved
	// behindBuffer or drawn the cursor yet. Never run recovery cursor callbacks
	// against that old behindBuffer, even after a partial optional allocation.
	CursorUpdated = false;
	Prepared = false;
	FrameValid = false;
	if (HasD3dHudPresentationDeferredFrame())
		RecoverD3dHudPresentationFrame(Surface { FrameSource, CapturedRegion });
}

} // namespace

Rectangle GetD3dHudPresentationValueRect(bool mana)
{
	if (!IsD3dHudEnabled())
		return GetD3dHudValueRect(mana);
	const auto placement = PlaceOrb(mana);
	const int width = std::max(1, static_cast<int>(placement.radius * 2));
	const float glyphScale = RecipeReady ? Recipe.valueGlyphScaleY : GetD3dHudMetrics().valueGlyphScaleY;
	const int height = std::clamp(static_cast<int>(std::lround(14 * glyphScale)), 1, width);
	return { { static_cast<int>(std::lround(placement.centerX - width / 2.0F)),
		         static_cast<int>(std::lround(placement.centerY - height / 2.0F)) }, { width, height } };
}

void BeginD3dHudPresentationFrame(const Surface &base)
{
	BeginD3dPanelPresentationFrame(base);
	++Counters.frames;
	RecipeReady = false;
	FrameMaterialized = false;
	Materializing = false;
	ForegroundSealed = false;
	FrameFinalized = false;
	Prepared = false;
	SubmissionStarted = false;
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
	if (!IsD3dHudEnabled() || MyPlayer == nullptr || InspectPlayer == nullptr || !IsIndexed(base) || base.w() > 8192 || base.h() > 8192
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
	FreezeRecipe();
	RecipeReady = true;
	FrameValid = true;
}

bool IsD3dHudPresentationCapturing(const Surface &source)
{
	return RecipeReady && source.surface == FrameSource && source.region.x == CapturedRegion.x
	    && source.region.y == CapturedRegion.y && source.region.w == CapturedRegion.w && source.region.h == CapturedRegion.h;
}

bool HasD3dHudPresentationDeferredFrame()
{
	return RecipeReady && FrameSource != nullptr && !FrameMaterialized;
}

void SealD3dHudPresentationForeground(const Surface &source)
{
	SealD3dPanelPresentationFrame(source);
	if (!IsD3dHudPresentationCapturing(source)) {
		FrameValid = false;
		return;
	}
	ForegroundSealed = true;
}

void FinalizeD3dHudPresentationFrame(const Surface &source)
{
	FinalizeD3dPanelPresentationFrame(source);
	if (!IsD3dHudPresentationCapturing(source) || NativeUiCaptureActive || !SameFootprints()) {
		FrameValid = false;
		return;
	}
	SealD3dHudPresentationForeground(source);
	FrameFinalized = true;
}

void SetD3dHudRecoveryCursorCallbacks(void (*before)(const Surface &), void (*after)(const Surface &))
{
	SetD3dPanelRecoveryCursorCallbacks(before, after);
	BeforeRecoveryCursor = before;
	AfterRecoveryCursor = after;
}

bool IsD3dHudPresentationRecoveryPixel(Point position)
{
	if (IsD3dPanelPresentationRecoveryPixel(position)) return true;
	if (IsD3dPanelPresentationRecovering() && !Materializing) return false;
	for (const auto &footprint : Footprints) {
		if (position.x >= footprint.x && position.y >= footprint.y
		    && position.x < footprint.x + footprint.w && position.y < footprint.y + footprint.h)
			return true;
	}
	// Dynamic targets can extend outside authored decoration. Track their
	// covered pixels exactly; never treat their transparent bounding box as UI.
	if (Foreground && position.x >= 0 && position.y >= 0
	    && position.x < Foreground->w() && position.y < Foreground->h())
		return ForegroundMask[static_cast<size_t>(position.y) * Foreground->w() + position.x] != 0;
	return false;
}

void BlitD3dHudPresentationCapturedCursor(const Surface &source)
{
	if (!Cursor || !IsD3dHudPresentationCapturing(source))
		return;
	for (int y = 0; y < Cursor->h(); ++y) {
		for (int x = 0; x < Cursor->w(); ++x) {
			const Point destination = CursorPosition + Displacement { x, y };
			const uint8_t color = *Cursor->at(x, y);
			const bool opaque = CursorMask.empty() ? color != TransparentColor
			                                      : CursorMask[static_cast<size_t>(y) * Cursor->w() + x] != 0;
			if (opaque && source.InBounds(destination) && IsD3dHudPresentationRecoveryPixel(destination)) {
				MarkUiOverlayRect(source, destination.x, destination.y, 1, 1);
				source[destination] = color;
			}
		}
	}
}

bool RecoverD3dHudPresentationFrame(const Surface &source)
{
	if (!IsD3dHudPresentationCapturing(source) || !IsIndexed(source) || !IsD3dHudSkinLoaded())
		return false;
	if (FrameMaterialized)
		return true;
	// Set this BEFORE calling any painter. Its UI marks pass through the same
	// before-write observer and must never recursively materialize this frame.
	FrameMaterialized = true;
	Materializing = true;
	const bool previousUiCapture = NativeUiCaptureActive;
	NativeUiCaptureActive = false;
	Prepared = false;
	++Counters.recoveries;
	if (CursorUpdated && Cursor && BeforeRecoveryCursor != nullptr)
		BeforeRecoveryCursor(source);
	DrawD3dHudSkinIndexedPlate(source, Recipe.chassis, false, D3dHudSkinPart::Chassis, Recipe.designScaleX, Recipe.designScaleY);
	DrawD3dHudSkinIndexedPlate(source, Recipe.info, false, D3dHudSkinPart::Inset, Recipe.designScaleX, Recipe.designScaleY);
	DrawD3dHudSkinIndexedPlate(source, Recipe.spell, false, D3dHudSkinPart::Inset, Recipe.designScaleX, Recipe.designScaleY);
	for (int i = 0; i < 8; ++i) {
		DrawD3dHudSkinIndexedPlate(source, Recipe.belt[i], false, D3dHudSkinPart::Inset, Recipe.designScaleX, Recipe.designScaleY);
		if (Recipe.tinted[i]) {
			DrawD3dHudSkinIndexedTint(source, Recipe.beltInteriors[i],
			    Recipe.tint[i]);
		}
	}
	for (int i = 0; i < Recipe.buttonCount; ++i)
		DrawD3dHudSkinIndexedPlateShade(source, Recipe.buttons[i], D3dHudSkinPart::Button, Recipe.shade[i], Recipe.designScaleX, Recipe.designScaleY);
	DrawD3dHudSkinIndexedOrb(source, Recipe.orbs[0], false, Recipe.fill[0]);
	DrawD3dHudSkinIndexedOrb(source, Recipe.orbs[1], true, Recipe.fill[1]);
	// Early recovery contains only foreground captured so far. Later HUD
	// functions draw their native dynamic pixels once, in their original order.
	// An already materialized frame never reapplies foreground over native UI.
	for (int y = 0; y < source.h(); ++y) {
		for (int x = 0; x < source.w(); ++x) {
			if (ForegroundMask[static_cast<size_t>(y) * source.w() + x] != 0 && IsD3dHudPresentationRecoveryPixel({ x, y }))
				*source.at(x, y) = *Foreground->at(x, y);
		}
	}
	if (CursorUpdated && Cursor && AfterRecoveryCursor != nullptr)
		AfterRecoveryCursor(source);
	else if (CursorUpdated && Cursor)
		BlitD3dHudPresentationCapturedCursor(source);
	NativeUiCaptureActive = previousUiCapture;
	Materializing = false;
	return true;
}

void RecordD3dHudForeground(const Surface &source, Rectangle target, uint8_t key)
{
	if (!RecipeReady || ForegroundSealed)
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
	if (!RecipeReady || ForegroundSealed || !Clip(target, Foreground->w(), Foreground->h(), clipped))
		return;
	for (int y = clipped.y; y < clipped.y + clipped.h; ++y) {
		std::memset(Foreground->at(clipped.x, y), TransparentColor, clipped.w);
		std::memset(ForegroundMask.data() + static_cast<size_t>(y) * Foreground->w() + clipped.x, 0, clipped.w);
	}
}

void RecordD3dHudButtonState(int button, bool pressed)
{
	if (!RecipeReady || button < 0 || button >= static_cast<int>(Recipe.pressed.size()))
		return;
	if (Recipe.pressed[button] != pressed)
		FrameValid = false;
	ButtonRecorded[button] = true;
}

void RecordD3dHudBeltTint(int slot, uint8_t paletteIndex)
{
	if (!RecipeReady || slot < 0 || slot >= static_cast<int>(Recipe.tint.size()))
		return;
	if (!Recipe.tinted[slot] || Recipe.tint[slot] != paletteIndex)
		FrameValid = false;
	BeltTintRecorded[slot] = true;
}

void RecordD3dHudExperienceBar()
{
	if (!RecipeReady || ForegroundSealed)
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
	CoverageReady = RecipeReady;
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
	SetD3dPanelPresentationNativeUiCapture(active);
	NativeUiCaptureActive = active && RecipeReady;
}

namespace detail {
void RecordD3dHudPresentationNativeUiRect(const SDL_Rect &region)
{
	detail::RecordD3dPanelNativeUiRect(region);
	if (!RecipeReady || !NativeUiCaptureActive || Materializing)
		return;
	for (const auto &footprint : Footprints) {
		if (Intersects(region, footprint)) {
			UiBlocked = true;
			// The common recorder reaches us before the first native write.
			// Translucent UI therefore reads the materialized R1 underneath it.
			if (!FrameMaterialized)
				RecoverD3dHudPresentationFrame(Surface { FrameSource, CapturedRegion });
			return;
		}
	}
	if (Intersects(region, ForegroundBounds)) {
		UiBlocked = true;
		if (!FrameMaterialized)
			RecoverD3dHudPresentationFrame(Surface { FrameSource, CapturedRegion });
	}
}
} // namespace detail

void ClearD3dHudPresentationCursor()
{
	ClearD3dPanelPresentationCursor();
	Cursor.reset();
	CursorMask.clear();
	CursorUpdated = true;
}

void CaptureD3dHudPresentationCursor(Rectangle clippedBounds, Point spriteBottomLeft, int cursorId)
{
	CaptureD3dPanelPresentationCursor(clippedBounds, spriteBottomLeft, cursorId);
	++Counters.cursorCaptureAttempts;
	Cursor.reset();
	CursorMask.clear();
	CursorUpdated = true;
	if (!RecipeReady)
		return;
	if (!CursorBoundsFit(clippedBounds)
	    || !AllocateCursorSurface(clippedBounds.size.width, clippedBounds.size.height)) {
		RejectCursorCapture();
		return;
	}
	for (int y = 0; y < Cursor->h(); ++y)
		std::memset(Cursor->at(0, y), TransparentColor, Cursor->w());
	// Same palette contract as SetHardwareCursorFromSprite: UI sprites never
	// use indices 1..127, so black (0) remains real, opaque cursor coverage.
	DrawSoftwareCursor(*Cursor, spriteBottomLeft - Displacement { clippedBounds.position.x, clippedBounds.position.y }, cursorId);
	CursorPosition = clippedBounds.position;
}

void CaptureD3dHudPresentationCursorPixels(Rectangle clippedBounds,
    std::span<const uint8_t> pixels, std::span<const uint8_t> opacity)
{
	CaptureD3dPanelPresentationCursorPixels(clippedBounds, pixels, opacity);
	++Counters.cursorCaptureAttempts;
	Cursor.reset();
	CursorMask.clear();
	CursorUpdated = true;
	if (!RecipeReady)
		return;
	const int width = clippedBounds.size.width, height = clippedBounds.size.height;
	if (!CursorBoundsFit(clippedBounds)
	    || pixels.size() != static_cast<size_t>(width) * height
	    || opacity.size() != static_cast<size_t>(width) * height
	    || !AllocateCursorSurface(width, height)
	    || !AssignCursorMask(opacity)) {
		RejectCursorCapture();
		return;
	}
	for (int y = 0; y < height; ++y) {
		const size_t offset = static_cast<size_t>(y) * width;
		auto *target = Cursor->at(0, y);
		for (int x = 0; x < width; ++x)
			target[x] = opacity[offset + x] != 0 ? pixels[offset + x] : TransparentColor;
	}
	CursorPosition = clippedBounds.position;
}

bool PrepareD3dHudPresentation(SDL_Renderer *renderer, SDL_Surface *indexedUi,
    SDL_Palette *palette, bool allowTownLayers, bool townLayersAvailable)
{
	++Counters.preparations;
	Prepared = false;
	SubmissionStarted = false;
	if (!allowTownLayers || renderer == nullptr || !FrameValid || !HasD3dHudPresentationDeferredFrame()
	    || !CoverageReady || UiBlocked || !CursorUpdated || NativeUiCaptureActive
	    || indexedUi == nullptr || indexedUi != FrameSource
	    || SDLC_SURFACE_BITSPERPIXEL(indexedUi) != 8 || palette == nullptr || palette->ncolors < 256
	    || CapturedRegion.x < 0 || CapturedRegion.y < 0
	    || CapturedRegion.x + Base->w() > indexedUi->w || CapturedRegion.y + Base->h() > indexedUi->h
	    || Base->w() != GetScreenWidth() || Base->h() != GetScreenHeight())
		return false;
	if (!FrameFinalized)
		FinalizeD3dHudPresentationFrame(Surface { FrameSource, CapturedRegion });
	if (!FrameValid || !FrameFinalized)
		return false;
	// The indexed R1 recovery follows the active palette during fade/black.
	if (std::all_of(palette->colors, palette->colors + 256, [](const SDL_Color &color) {
		    return color.r == 0 && color.g == 0 && color.b == 0;
	    }))
		return false;
	const auto ui = GetUiOverlayFrame();
	if (ui.source != FrameSource || ui.sourceRegion.x != CapturedRegion.x || ui.sourceRegion.y != CapturedRegion.y
	    || ui.sourceRegion.w != Base->w() || ui.sourceRegion.h != Base->h())
		return false;
	if (!std::all_of(ButtonRecorded.begin(), ButtonRecorded.begin() + Recipe.buttonCount, [](bool recorded) { return recorded; }))
		return false;
	for (size_t i = 0; i < Recipe.tinted.size(); ++i) {
		if (Recipe.tinted[i] && !BeltTintRecorded[i])
			return false;
	}
	if (GetTownViewHighResolutionFrame() != nullptr && !townLayersAvailable)
		return false;
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
		return false;
	if (foregroundRecreated) {
		UploadedForegroundBounds = {};
		if (!InitializeTransparentStreaming(ForegroundTexture.get(), ForegroundTextureWidth, ForegroundTextureHeight)) {
			ForegroundTexture.reset();
			return false;
		}
	}
	if (!townLayersAvailable && !EnsureRegionTexture(renderer, BaseTexture, baseBounds, BaseTextureWidth, BaseTextureHeight, true))
		return false;
	std::copy_n(palette->colors, PreparedPalette.size(), PreparedPalette.begin());
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
			return false;
		UploadedForegroundBounds = ForegroundBounds;
	}
	if (!townLayersAvailable && baseBounds.w > 0 && baseBounds.h > 0) {
		ConvertIndexed(*Base, baseBounds, colors, false, BaseColors);
		if (!Upload(BaseTexture.get(), BaseColors, baseBounds.w))
			return false;
	}
	if (Cursor) {
		if (!CursorTexture || CursorWidth != Cursor->w() || CursorHeight != Cursor->h()) {
			CursorTexture = MakeStreaming(renderer, Cursor->w(), Cursor->h());
			if (!CursorTexture)
				return false;
			CursorWidth = Cursor->w();
			CursorHeight = Cursor->h();
		}
		ConvertIndexed(*Cursor, { 0, 0, Cursor->w(), Cursor->h() }, colors, true, CursorColors,
		    CursorMask.empty() ? nullptr : &CursorMask);
		if (!Upload(CursorTexture.get(), CursorColors, Cursor->w()))
			return false;
	}
	PreparedBaseBounds = baseBounds;
	PreparedWithTown = townLayersAvailable;
	Prepared = true;
	return true;
}

D3dHudPresentationResult RenderD3dHudPresentation(SDL_Renderer *renderer, SDL_Surface *indexedUi,
    SDL_Palette *palette, bool allowTownLayers, bool townLayersPresented)
{
	// Direct fixture callers retain the same entry point. The engine explicitly
	// prepares before converting/uploading its ordinary indexed presentation.
	if (!Prepared && !PrepareD3dHudPresentation(renderer, indexedUi, palette, allowTownLayers, townLayersPresented))
		return D3dHudPresentationResult::Inactive;
	if (!HasD3dHudPresentationDeferredFrame() || !Prepared || Owner != renderer || indexedUi != FrameSource
	    || PreparedWithTown != townLayersPresented || !allowTownLayers)
		return D3dHudPresentationResult::Inactive;
	Prepared = false;
	if (townLayersPresented) {
		if (!BeforeSubmit())
			return D3dHudPresentationResult::Failed;
		++Counters.retainedWorldRestores;
		const bool firstSubmission = !SubmissionStarted;
		if (firstSubmission)
			++Counters.firstSubmissionAttempts;
		const bool restored = RestoreTownPresentationWorldRegions(renderer, Footprints);
		if (restored && firstSubmission)
			++Counters.firstSubmissionSuccesses;
		if (!AfterSubmit(restored))
			return D3dHudPresentationResult::Failed;
	} else {
		for (const auto &footprint : Footprints) {
			SDL_Rect clipped;
			if (!Clip(MakeRectangle(footprint), Base->w(), Base->h(), clipped))
				continue;
			const SDL_Rect source { clipped.x - PreparedBaseBounds.x, clipped.y - PreparedBaseBounds.y, clipped.w, clipped.h };
			if (!DrawTexture(renderer, BaseTexture.get(), &source, AsFloat(MakeRectangle(clipped))))
				return D3dHudPresentationResult::Failed;
		}
	}
	if (!DrawSkin(renderer) || !DrawTexture(renderer, ForegroundTexture.get(), nullptr,
	        { 0, 0, static_cast<float>(Base->w()), static_cast<float>(Base->h()) }))
		return D3dHudPresentationResult::Failed;
	if (Cursor && !DrawTexture(renderer, CursorTexture.get(), nullptr,
	                  { static_cast<float>(CursorPosition.x), static_cast<float>(CursorPosition.y), static_cast<float>(Cursor->w()), static_cast<float>(Cursor->h()) }))
		return D3dHudPresentationResult::Failed;
	return D3dHudPresentationResult::Presented;
}

void InvalidateD3dHudPresentationFrame()
{
	InvalidateD3dPanelPresentationFrame();
	RecipeReady = false;
	FrameMaterialized = false;
	Materializing = false;
	ForegroundSealed = false;
	FrameFinalized = false;
	Prepared = false;
	SubmissionStarted = false;
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
	ResetD3dPanelPresentationResources();
	InvalidateD3dHudPresentationFrame();
	ResetTextures();
	Base.reset();
	Foreground.reset();
	ForegroundMask.clear();
	std::vector<uint8_t> {}.swap(CursorMask);
}

void SetD3dHudPresentationFault(D3dHudPresentationFault fault, uint64_t ordinal)
{
	Fault = fault;
	FaultOrdinal = std::max<uint64_t>(1, ordinal);
	FaultUpload = FaultAllocation = FaultDraw = 0;
	FaultCursorSurface = FaultCursorMask = 0;
}

D3dHudPresentationCounters GetD3dHudPresentationCounters() { return Counters; }
void ResetD3dHudPresentationCounters() { Counters = {}; }

} // namespace devilution
#else
namespace devilution {
void BeginD3dHudPresentationFrame(const Surface &source) { BeginD3dPanelPresentationFrame(source); }
bool IsD3dHudPresentationCapturing(const Surface &) { return false; }
bool HasD3dHudPresentationDeferredFrame() { return false; }
void SealD3dHudPresentationForeground(const Surface &source) { SealD3dPanelPresentationFrame(source); }
void FinalizeD3dHudPresentationFrame(const Surface &source) { FinalizeD3dPanelPresentationFrame(source); }
void SetD3dHudRecoveryCursorCallbacks(void (*before)(const Surface &), void (*after)(const Surface &)) { SetD3dPanelRecoveryCursorCallbacks(before, after); }
bool IsD3dHudPresentationRecoveryPixel(Point point) { return IsD3dPanelPresentationRecoveryPixel(point); }
void BlitD3dHudPresentationCapturedCursor(const Surface &) { }
bool RecoverD3dHudPresentationFrame(const Surface &) { return false; }
bool PrepareD3dHudPresentation(SDL_Renderer *, SDL_Surface *, SDL_Palette *, bool, bool) { return false; }
void RecordD3dHudForeground(const Surface &, Rectangle, uint8_t) { }
void ClearD3dHudForeground(Rectangle) { }
void RecordD3dHudButtonState(int, bool) { }
void RecordD3dHudBeltTint(int, uint8_t) { }
void RecordD3dHudExperienceBar() { }
Rectangle GetD3dHudPresentationValueRect(bool mana)
{
	if (!IsD3dHudEnabled())
		return GetD3dHudValueRect(mana);
	const auto placement = PlaceD3dHudSkinOrb(GetD3dHudOrbRect(mana), mana);
	const int width = std::max(1, static_cast<int>(placement.radius * 2));
	const int height = std::clamp(static_cast<int>(std::lround(14 * GetD3dHudMetrics().valueGlyphScaleY)), 1, width);
	return { { static_cast<int>(std::lround(placement.centerX - width / 2.0F)),
		         static_cast<int>(std::lround(placement.centerY - height / 2.0F)) }, { width, height } };
}
void SetD3dHudPresentationUiCoverage(std::span<const SDL_Rect>) { }
void SetD3dHudPresentationNativeUiCapture(bool active) { SetD3dPanelPresentationNativeUiCapture(active); }
namespace detail {
void RecordD3dHudPresentationNativeUiRect(const SDL_Rect &region) { detail::RecordD3dPanelNativeUiRect(region); }
} // namespace detail
void ClearD3dHudPresentationCursor() { ClearD3dPanelPresentationCursor(); }
void CaptureD3dHudPresentationCursor(Rectangle bounds, Point position, int cursorId) { CaptureD3dPanelPresentationCursor(bounds, position, cursorId); }
void CaptureD3dHudPresentationCursorPixels(Rectangle bounds, std::span<const uint8_t> pixels, std::span<const uint8_t> opacity) { CaptureD3dPanelPresentationCursorPixels(bounds, pixels, opacity); }
D3dHudPresentationResult RenderD3dHudPresentation(SDL_Renderer *, SDL_Surface *, SDL_Palette *, bool, bool)
{
	return D3dHudPresentationResult::Inactive;
}
void ResetD3dHudPresentationResources() { ResetD3dPanelPresentationResources(); }
void InvalidateD3dHudPresentationFrame() { InvalidateD3dPanelPresentationFrame(); }
void SetD3dHudPresentationFault(D3dHudPresentationFault, uint64_t) { }
D3dHudPresentationCounters GetD3dHudPresentationCounters() { return {}; }
void ResetD3dHudPresentationCounters() { }
} // namespace devilution
#endif
