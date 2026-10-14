#include "engine/render/town_presentation.hpp"

#ifndef USE_SDL1
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "engine/render/ui_overlay_regions.hpp"
#include "engine/surface.hpp"
#include "utils/sdl_ptrs.h"

namespace devilution {
namespace {

SDL_Renderer *CachedRenderer = nullptr;
SDLSurfaceUniquePtr WorldColors;
SDLSurfaceUniquePtr UiColors;
SDLTextureUniquePtr WorldTexture;
SDLTextureUniquePtr UiTexture;
bool CachedLinearFilter = true;

SDLSurfaceUniquePtr CreateColors(int width, int height)
{
#ifdef USE_SDL3
	return SDLSurfaceUniquePtr { SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888) };
#else
	return SDLSurfaceUniquePtr { SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888) };
#endif
}

bool Prepare(SDL_Renderer *renderer, int worldWidth, int worldHeight, int width, int height, bool linearFilter)
{
	if (CachedRenderer == renderer && WorldTexture && UiTexture && WorldColors && UiColors
	    && WorldColors->w == worldWidth && WorldColors->h == worldHeight
	    && UiColors->w == width && UiColors->h == height && CachedLinearFilter == linearFilter)
		return true;
	ResetTownPresentationResources();
	WorldColors = CreateColors(worldWidth, worldHeight);
	UiColors = CreateColors(width, height);
	WorldTexture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, worldWidth, worldHeight));
#if !defined(USE_SDL3) && !SDL_VERSION_ATLEAST(2, 0, 12)
	// Older SDL2 chooses the filter at creation rather than per texture.
	const char *qualityHint = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
	const std::string oldQuality = qualityHint != nullptr ? qualityHint : "";
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif
	UiTexture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height));
#if !defined(USE_SDL3) && !SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, oldQuality.c_str());
#endif
	if (!WorldColors || !UiColors || !WorldTexture || !UiTexture) {
		ResetTownPresentationResources();
		return false;
	}
#ifdef USE_SDL3
	if (!SDL_SetTextureBlendMode(WorldTexture.get(), SDL_BLENDMODE_NONE)
	    || !SDL_SetTextureBlendMode(UiTexture.get(), SDL_BLENDMODE_BLEND)
	    || !SDL_SetTextureScaleMode(WorldTexture.get(), linearFilter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST)
	    || !SDL_SetTextureScaleMode(UiTexture.get(), SDL_SCALEMODE_NEAREST)) {
#else
	if (SDL_SetTextureBlendMode(WorldTexture.get(), SDL_BLENDMODE_NONE) < 0
	    || SDL_SetTextureBlendMode(UiTexture.get(), SDL_BLENDMODE_BLEND) < 0
#if SDL_VERSION_ATLEAST(2, 0, 12)
	    || SDL_SetTextureScaleMode(WorldTexture.get(), linearFilter ? SDL_ScaleModeLinear : SDL_ScaleModeNearest) < 0
	    || SDL_SetTextureScaleMode(UiTexture.get(), SDL_ScaleModeNearest) < 0
#endif
	) {
#endif
		ResetTownPresentationResources();
		return false;
	}
	CachedRenderer = renderer;
	CachedLinearFilter = linearFilter;
	return true;
}

} // namespace

void ResetTownPresentationResources()
{
	WorldTexture.reset();
	UiTexture.reset();
	WorldColors.reset();
	UiColors.reset();
	CachedRenderer = nullptr;
}

bool RenderTownPresentationLayers(SDL_Renderer *renderer, SDL_Surface *logicalOutput,
    const Surface &highResolutionWorld, const UiOverlayFrame &overlay,
    SDL_Palette *palette, bool linearFilter)
{
	if (renderer == nullptr || logicalOutput == nullptr || palette == nullptr || palette->ncolors < 256
	    || logicalOutput->w <= 0 || logicalOutput->h <= 0 || logicalOutput->w > 8192 || logicalOutput->h > 8192
	    || overlay.source == nullptr || overlay.sourceRegion.w != logicalOutput->w || overlay.sourceRegion.h != logicalOutput->h
	    || highResolutionWorld.surface == nullptr || SDLC_SURFACE_BITSPERPIXEL(highResolutionWorld.surface) != 8
	    || highResolutionWorld.region.x < 0 || highResolutionWorld.region.y < 0
	    || highResolutionWorld.w() > highResolutionWorld.surface->w - highResolutionWorld.region.x
	    || highResolutionWorld.h() > highResolutionWorld.surface->h - highResolutionWorld.region.y
	    || highResolutionWorld.w() != logicalOutput->w * 2 || highResolutionWorld.h() <= 0
	    || highResolutionWorld.h() % 2 != 0 || highResolutionWorld.h() > logicalOutput->h * 2
	    || static_cast<uint64_t>(highResolutionWorld.w()) * highResolutionWorld.h() > 4 * 1024 * 1024
	    || SDL_MUSTLOCK(highResolutionWorld.surface) || SDL_MUSTLOCK(logicalOutput))
		return false;
	if (!Prepare(renderer, highResolutionWorld.w(), highResolutionWorld.h(), logicalOutput->w, logicalOutput->h, linearFilter))
		return false;

	// Use the active game palette, including fades/gamma, rather than the scratch
	// allocation's palette. All world pixels are opaque, including index zero.
	std::array<uint32_t, 256> colors;
	for (size_t i = 0; i < colors.size(); ++i) {
		const SDL_Color color = palette->colors[i];
		colors[i] = 0xFF000000U | (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
	}
	for (int y = 0; y < highResolutionWorld.h(); ++y) {
		const uint8_t *source = highResolutionWorld.at(0, y);
		auto *destination = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(WorldColors->pixels) + y * WorldColors->pitch);
		for (int x = 0; x < highResolutionWorld.w(); ++x)
			destination[x] = colors[source[x]];
	}
	std::memset(UiColors->pixels, 0, static_cast<size_t>(UiColors->pitch) * UiColors->h);
#ifdef USE_SDL3
	const auto format = logicalOutput->format;
	const int bytesPerPixel = SDL_BYTESPERPIXEL(format);
#else
	const auto format = logicalOutput->format->format;
	const int bytesPerPixel = logicalOutput->format->BytesPerPixel;
#endif
	if (bytesPerPixel < 2)
		return false;
	const SDL_Rect bounds { 0, 0, logicalOutput->w, logicalOutput->h };
	// UI coverage has sharp alpha edges; nearest filtering avoids dark fringes
	// from interpolating RGB against transparent black with straight alpha.
	for (const auto regions : { overlay.regions, overlay.cursorRegions }) {
		for (const SDL_Rect &region : regions) {
			SDL_Rect clipped;
#ifdef USE_SDL3
			if (!SDL_GetRectIntersection(&bounds, &region, &clipped))
#else
			if (SDL_IntersectRect(&bounds, &region, &clipped) == SDL_FALSE)
#endif
				continue;
			const auto *source = static_cast<const uint8_t *>(logicalOutput->pixels) + clipped.y * logicalOutput->pitch + clipped.x * bytesPerPixel;
			auto *destination = static_cast<uint8_t *>(UiColors->pixels) + clipped.y * UiColors->pitch + clipped.x * 4;
#ifdef USE_SDL3
			if (!SDL_ConvertPixels(clipped.w, clipped.h, format, source, logicalOutput->pitch, SDL_PIXELFORMAT_ARGB8888, destination, UiColors->pitch))
#else
			if (SDL_ConvertPixels(clipped.w, clipped.h, format, source, logicalOutput->pitch, SDL_PIXELFORMAT_ARGB8888, destination, UiColors->pitch) < 0)
#endif
				return false;
			// Coverage comes from drawing regions, never color differences: black
			// letters/panels and pixels equal to the world are still real UI pixels.
			for (int y = 0; y < clipped.h; ++y) {
				auto *row = reinterpret_cast<uint32_t *>(destination + y * UiColors->pitch);
				for (int x = 0; x < clipped.w; ++x)
					row[x] |= 0xFF000000U;
			}
		}
	}
	// Prepare both textures before drawing anything. A late draw failure is
	// also reported so the caller can cover the partial result with legacy.
#ifdef USE_SDL3
	if (!SDL_UpdateTexture(WorldTexture.get(), nullptr, WorldColors->pixels, WorldColors->pitch)
	    || !SDL_UpdateTexture(UiTexture.get(), nullptr, UiColors->pixels, UiColors->pitch))
		return false;
	const SDL_FRect destination { 0, 0, static_cast<float>(logicalOutput->w), static_cast<float>(highResolutionWorld.h() / 2) };
	return SDL_RenderTexture(renderer, WorldTexture.get(), nullptr, &destination)
	    && SDL_RenderTexture(renderer, UiTexture.get(), nullptr, nullptr);
#else
	if (SDL_UpdateTexture(WorldTexture.get(), nullptr, WorldColors->pixels, WorldColors->pitch) < 0
	    || SDL_UpdateTexture(UiTexture.get(), nullptr, UiColors->pixels, UiColors->pitch) < 0)
		return false;
	const SDL_Rect destination { 0, 0, logicalOutput->w, highResolutionWorld.h() / 2 };
	return SDL_RenderCopy(renderer, WorldTexture.get(), nullptr, &destination) >= 0
	    && SDL_RenderCopy(renderer, UiTexture.get(), nullptr, nullptr) >= 0;
#endif
}

bool RestoreTownPresentationWorldRegions(SDL_Renderer *renderer,
    std::span<const SDL_Rect> footprints)
{
	if (renderer == nullptr || renderer != CachedRenderer
	    || !WorldTexture || !WorldColors || !UiColors
	    || WorldColors->w != UiColors->w * 2
	    || WorldColors->h <= 0 || WorldColors->h % 2 != 0)
		return false;
	const SDL_Rect bounds { 0, 0, UiColors->w, UiColors->h };
	const int worldHeight = WorldColors->h / 2;
	// Validate every region before issuing the first draw command.
	for (const SDL_Rect &footprint : footprints) {
		SDL_Rect clipped;
#ifdef USE_SDL3
		if (!SDL_GetRectIntersection(&bounds, &footprint, &clipped))
#else
		if (SDL_IntersectRect(&bounds, &footprint, &clipped) == SDL_FALSE)
#endif
			continue;
		if (clipped.y + clipped.h > worldHeight)
			return false;
	}
	for (const SDL_Rect &footprint : footprints) {
		SDL_Rect clipped;
#ifdef USE_SDL3
		if (!SDL_GetRectIntersection(&bounds, &footprint, &clipped))
			continue;
		const SDL_FRect source { static_cast<float>(clipped.x * 2), static_cast<float>(clipped.y * 2),
			static_cast<float>(clipped.w * 2), static_cast<float>(clipped.h * 2) };
		const SDL_FRect destination { static_cast<float>(clipped.x), static_cast<float>(clipped.y),
			static_cast<float>(clipped.w), static_cast<float>(clipped.h) };
		if (!SDL_RenderTexture(renderer, WorldTexture.get(), &source, &destination))
#else
		if (SDL_IntersectRect(&bounds, &footprint, &clipped) == SDL_FALSE)
			continue;
		const SDL_Rect source { clipped.x * 2, clipped.y * 2, clipped.w * 2, clipped.h * 2 };
		if (SDL_RenderCopy(renderer, WorldTexture.get(), &source, &clipped) < 0)
#endif
			return false;
	}
	return true;
}

} // namespace devilution
#endif
