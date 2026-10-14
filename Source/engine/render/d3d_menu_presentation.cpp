#include "engine/render/d3d_menu_presentation.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>

#include "DiabloUI/hero/hero_portrait.hpp"
#include "engine/assets.hpp"
#include "engine/hd_sprite_pack.hpp"
#include <vector>
#include "engine/palette.h"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/png.h"
#include "utils/sdl_geometry.h"
#include "utils/sdl_ptrs.h"

namespace devilution {
namespace {
struct RgbaSpriteFrame { SDL_Surface *surface=nullptr; void *sourcePixels=nullptr; int sourceWidth=0,sourceHeight=0; Point at; int width=0,height=0,scale=1; std::vector<uint32_t> colors; };
std::vector<RgbaSpriteFrame> RgbaSprites;
bool Active = false;
int Fade = 0;
constexpr size_t MaxPortraitPixels = 216 * 100;
struct PortraitFrame {
	const HeroPortrait *portrait = nullptr;
	SDL_Surface *surface = nullptr;
	void *pixels = nullptr;
	int width = 0, height = 0, pitch = 0;
	SDL_Rect rect {};
	bool rejected = false;
	std::array<uint8_t, MaxPortraitPixels> coverage {};
	std::array<uint8_t, MaxPortraitPixels> nativeCoverage {};
	std::array<uint8_t, MaxPortraitPixels> indexedFallback {};
} Portrait;

bool MatchesPortraitSurface(SDL_Surface *surface)
{
	return Portrait.portrait != nullptr && surface != nullptr
	    && surface == Portrait.surface && surface->pixels == Portrait.pixels
	    && surface->w == Portrait.width && surface->h == Portrait.height
	    && surface->pitch == Portrait.pitch && SDLC_SURFACE_BITSPERPIXEL(surface) == 8;
}

bool OverlapsPortrait(int64_t x, int64_t y, int64_t width, int64_t height)
{
	return width > 0 && height > 0 && x < static_cast<int64_t>(Portrait.rect.x) + Portrait.rect.w
	    && y < static_cast<int64_t>(Portrait.rect.y) + Portrait.rect.h
	    && x + width > Portrait.rect.x && y + height > Portrait.rect.y;
}

uint32_t RgbaColor(const SDL_Color &color)
{
	uint32_t rgba;
	const std::array<uint8_t, 4> bytes { color.r, color.g, color.b, 255 };
	std::memcpy(&rgba, bytes.data(), sizeof(rgba));
	return rgba;
}

#ifndef USE_SDL1
SDL_Renderer *Owner = nullptr;
SDLTextureUniquePtr Background;
SDLTextureUniquePtr Overlay;
int ImageWidth = 0, ImageHeight = 0, OverlayWidth = 0, OverlayHeight = 0;
std::unique_ptr<uint32_t[]> Pixels;

bool EnsureBackground(SDL_Renderer *target)
{
	if (Owner != target) {
		ResetD3dMainMenuResources();
		Owner = target;
	}
	if (Background) return true;
	constexpr const char *Path = "d3d-ui/menu-background.png";
	if (!FindAsset(Path).ok()) return false;
	SDLSurfaceUniquePtr image { LoadPNG(Path) };
	if (!image || image->w > 4096 || image->h > 4096 || image->w <= 0 || image->h <= 0) return false;
	Background.reset(SDL_CreateTextureFromSurface(target, image.get()));
	if (!Background) return false;
	ImageWidth = image->w;
	ImageHeight = image->h;
	SDL_SetTextureBlendMode(Background.get(), SDL_BLENDMODE_BLEND);
	Log("Diablo 3D menu background loaded: {}x{}", ImageWidth, ImageHeight);
	return true;
}
#endif
} // namespace

void BeginD3dMainMenuUiFrame()
{
	RgbaSprites.clear();
	Portrait.portrait = nullptr;
	Portrait.surface = nullptr;
	Portrait.pixels = nullptr;
	Portrait.rect = {};
	Portrait.rejected = false;
}

void RecordD3dMainMenuSpriteRgba(const Surface &out, ClxSprite sprite, Point bottomLeft)
{
	if(sprite.hdFrameId()==0 || RgbaSprites.size()>=4) return;
	auto rgba=LoadHdSpriteRgba(sprite); if(!rgba || rgba->empty()) return;
	RgbaSpriteFrame record; record.surface=out.surface; record.sourcePixels=out.surface->pixels;
	record.sourceWidth=out.surface->w;record.sourceHeight=out.surface->h;
	record.width=sprite.width();record.height=sprite.height();record.scale=sprite.hdScale();
	record.at=bottomLeft+Displacement{out.region.x,out.region.y-record.height+1};
	record.colors=std::move(*rgba);RgbaSprites.push_back(std::move(record));
}
bool CompositeD3dMainMenuSpriteRgba(SDL_Surface *surface, std::span<uint32_t> rgba, int fade)
{
	if(!surface || surface->w<=0 || surface->h<=0 || rgba.size()!=static_cast<size_t>(surface->w)*surface->h) return false;
	for(const auto &sprite:RgbaSprites) {
		if(sprite.surface!=surface || sprite.sourcePixels!=surface->pixels || sprite.sourceWidth!=surface->w || sprite.sourceHeight!=surface->h) continue;
		for(int y=0;y<sprite.height;++y) for(int x=0;x<sprite.width;++x) {
			const int dx=sprite.at.x+x,dy=sprite.at.y+y;
			if(dx<0||dy<0||dx>=surface->w||dy>=surface->h) continue;
			const uint32_t src=sprite.colors[static_cast<size_t>(y*sprite.scale+sprite.scale/2)*sprite.width*sprite.scale+x*sprite.scale+sprite.scale/2];
			const int alpha=src>>24; if(alpha==0) continue;
			auto *dst=reinterpret_cast<uint8_t *>(&rgba[static_cast<size_t>(dy)*surface->w+dx]);
			const int outAlpha=alpha+dst[3]*(255-alpha)/255;
			for(int channel=0;channel<3;++channel) {
				const int color=((src>>(16-channel*8))&255)*std::clamp(fade,0,256)/256;
				dst[channel]=static_cast<uint8_t>((color*alpha+dst[channel]*dst[3]*(255-alpha)/255)/std::max(1,outAlpha));
			}
			dst[3]=static_cast<uint8_t>(outAlpha);
		}
	}
	return !RgbaSprites.empty();
}

void RecordD3dMainMenuPortrait(const Surface &out, const HeroPortrait &portrait, const SDL_Rect &rect)
{
	if (Portrait.rejected) return;
	// A second portrait needs a different ordering contract. Retain both native
	// fallbacks and decline RGB instead of silently replacing an earlier layer.
	if (Portrait.portrait != nullptr) {
		Portrait.rejected = true;
		return;
	}
	SDL_Surface *surface = out.surface;
	const int64_t x = static_cast<int64_t>(out.region.x) + rect.x;
	const int64_t y = static_cast<int64_t>(out.region.y) + rect.y;
	if (surface == nullptr || surface->pixels == nullptr || SDLC_SURFACE_BITSPERPIXEL(surface) != 8
	    || surface->w <= 0 || surface->h <= 0 || surface->pitch < surface->w
	    || !portrait.raster || portrait.width <= 0 || portrait.height <= 0
	    || portrait.width > 512 || portrait.height > 512
	    || rect.w <= 0 || rect.h <= 0 || rect.w > 216 || rect.h > 100
	    || rect.x < 0 || rect.y < 0 || static_cast<int64_t>(rect.x) + rect.w > out.w()
	    || static_cast<int64_t>(rect.y) + rect.h > out.h()
	    || x < 0 || y < 0 || x + rect.w > surface->w || y + rect.h > surface->h) return;
	Portrait.portrait = &portrait;
	Portrait.surface = surface;
	Portrait.pixels = surface->pixels;
	Portrait.width = surface->w;
	Portrait.height = surface->h;
	Portrait.pitch = surface->pitch;
	Portrait.rect = MakeSdlRect(static_cast<int>(x), static_cast<int>(y), rect.w, rect.h);
	for (int py = 0; py < rect.h; ++py) {
		const auto *row = static_cast<const uint8_t *>(surface->pixels) + static_cast<size_t>(y + py) * surface->pitch + x;
		for (int px = 0; px < rect.w; ++px) {
			const size_t destination = static_cast<size_t>(py) * rect.w + px;
			const size_t source = static_cast<size_t>(py * portrait.height / rect.h) * portrait.width + px * portrait.width / rect.w;
			Portrait.coverage[destination] = portrait.opacity()[source];
			Portrait.nativeCoverage[destination] = 0;
			Portrait.indexedFallback[destination] = row[px];
		}
	}
}

void MarkD3dMainMenuNativeSprite(const Surface &out, ClxSprite sprite, Point topLeft)
{
	if (!MatchesPortraitSurface(out.surface)) return;
	const int64_t x = static_cast<int64_t>(out.region.x) + topLeft.x;
	const int64_t y = static_cast<int64_t>(out.region.y) + topLeft.y;
	if (!OverlapsPortrait(x, y, sprite.width(), sprite.height())) return;
	if (!VisitHeroPortraitClxPixels(sprite, [&](int sx, int sy, uint8_t) {
		    // Match the native draw's subregion clipping before marking coverage.
		    const int64_t localX = static_cast<int64_t>(topLeft.x) + sx;
		    const int64_t localY = static_cast<int64_t>(topLeft.y) + sy;
		    if (localX < 0 || localY < 0 || localX >= out.w() || localY >= out.h()) return;
		    const int64_t px = x + sx - Portrait.rect.x, py = y + sy - Portrait.rect.y;
		    if (px < 0 || py < 0 || px >= Portrait.rect.w || py >= Portrait.rect.h) return;
		    const size_t index = static_cast<size_t>(py) * Portrait.rect.w + static_cast<size_t>(px);
		    Portrait.coverage[index] = 0;
		    Portrait.nativeCoverage[index] = 1;
	    })) Portrait.rejected = true;
}

void RejectD3dMainMenuPortraitOverlap(const Surface &out, const SDL_Rect &rect)
{
	if (MatchesPortraitSurface(out.surface) && OverlapsPortrait(static_cast<int64_t>(out.region.x) + rect.x,
	        static_cast<int64_t>(out.region.y) + rect.y, rect.w, rect.h)) Portrait.rejected = true;
}

bool CompositeD3dMainMenuPortrait(SDL_Surface *ui, std::span<uint32_t> rgba, int fade)
{
	if (!MatchesPortraitSurface(ui) || ui->w <= 0 || ui->h <= 0 || ui->w > 8192 || ui->h > 8192 || ui->pitch < ui->w
	    || rgba.size() != static_cast<size_t>(ui->w) * ui->h) return false;
	const HeroPortrait &portrait = *Portrait.portrait;
	HeroPortraitPalette colors {};
	ApplyGlobalBrightness(colors.data(), portrait.palette.data());
	ApplyFadeLevel(static_cast<unsigned>(std::clamp(fade, 0, 256)), colors.data(), colors.data());
	for (int y = 0; y < Portrait.rect.h; ++y) {
		const int destinationY = Portrait.rect.y + y;
		const auto *row = static_cast<const uint8_t *>(ui->pixels) + static_cast<size_t>(destinationY) * ui->pitch;
		for (int x = 0; x < Portrait.rect.w; ++x) {
			const int destinationX = Portrait.rect.x + x;
			const size_t maskIndex = static_cast<size_t>(y) * Portrait.rect.w + x;
			const size_t outputIndex = static_cast<size_t>(destinationY) * ui->w + destinationX;
			if (Portrait.nativeCoverage[maskIndex] != 0 && row[destinationX] == 0) {
				// Opaque native index 0 is coverage, not a transparent texel.
				rgba[outputIndex] = RgbaColor(system_palette[0]);
			} else if (!Portrait.rejected && Portrait.coverage[maskIndex] != 0
			    && row[destinationX] == Portrait.indexedFallback[maskIndex]) {
				const size_t source = static_cast<size_t>(y * portrait.height / Portrait.rect.h) * portrait.width + x * portrait.width / Portrait.rect.w;
				rgba[outputIndex] = RgbaColor(colors[portrait.indices()[source]]);
			}
		}
	}
	return !Portrait.rejected;
}

D3dMainMenuPortraitFrameInfo GetD3dMainMenuPortraitFrameInfo()
{
	return { Portrait.rect, Portrait.portrait != nullptr, Portrait.rejected };
}

bool SetD3dMainMenuActive(bool active)
{
	Active = false;
	Fade = 0;
	BeginD3dMainMenuUiFrame();
#ifndef USE_SDL1
	Active = active && renderer != nullptr && EnsureBackground(renderer);
#endif
	return Active;
}

void SetD3dMainMenuFade(int value) { Fade = std::clamp(value, 0, 256); }
bool IsD3dMainMenuActive() { return Active; }

void ResetD3dMainMenuResources()
{
	BeginD3dMainMenuUiFrame();
#ifndef USE_SDL1
	Overlay.reset();
	Background.reset();
	Owner = nullptr;
	OverlayWidth = OverlayHeight = 0;
	Pixels.reset();
#endif
}

bool RenderD3dMainMenu(SDL_Renderer *target, SDL_Surface *ui)
{
#ifdef USE_SDL1
	return false;
#else
	if ((!Active && RgbaSprites.empty()) || target == nullptr || ui == nullptr || (Active && !EnsureBackground(target)) || SDLC_SURFACE_BITSPERPIXEL(ui) != 8
	    || ui->pixels == nullptr || ui->w <= 0 || ui->h <= 0 || ui->w > 8192 || ui->h > 8192 || ui->pitch < ui->w) return false;
	const size_t pixelCount = static_cast<size_t>(ui->w) * ui->h;
	if (!Overlay || OverlayWidth != ui->w || OverlayHeight != ui->h || !Pixels) {
		// Allocate everything before committing. Failure keeps the complete
		// indexed frame available to the caller's existing native fallback.
		std::unique_ptr<uint32_t[]> pixels(new (std::nothrow) uint32_t[pixelCount]);
		if (!pixels) return false;
		SDLTextureUniquePtr overlay(SDL_CreateTexture(target, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, ui->w, ui->h));
		if (!overlay) return false;
		SDL_SetTextureBlendMode(overlay.get(), SDL_BLENDMODE_BLEND);
		Pixels = std::move(pixels);
		Overlay = std::move(overlay);
		OverlayWidth = ui->w; OverlayHeight = ui->h;
	}
	std::array<uint32_t, 256> colors {};
	for (size_t i = Active ? 1 : 0; i < colors.size(); ++i)
		colors[i] = RgbaColor(system_palette[i]);
	for (int y = 0; y < ui->h; ++y) {
		const auto *row = static_cast<const uint8_t *>(ui->pixels) + static_cast<size_t>(y) * ui->pitch;
		for (int x = 0; x < ui->w; ++x)
			Pixels[static_cast<size_t>(y) * ui->w + x] = colors[row[x]];
	}
	CompositeD3dMainMenuPortrait(ui, { Pixels.get(), pixelCount }, Fade);
	CompositeD3dMainMenuSpriteRgba(ui, { Pixels.get(), pixelCount }, Fade);
	const float scale = std::max(static_cast<float>(gnScreenWidth) / ImageWidth, static_cast<float>(gnScreenHeight) / ImageHeight);
	const float width = ImageWidth * scale, height = ImageHeight * scale;
	if(Active) SDL_SetTextureAlphaMod(Background.get(), static_cast<Uint8>(Fade * 255 / 256));
#ifdef USE_SDL3
	const SDL_FRect rect { (gnScreenWidth - width) / 2, (gnScreenHeight - height) / 2, width, height };
	if (!SDL_UpdateTexture(Overlay.get(), nullptr, Pixels.get(), ui->w * 4)
	    || (Active && !SDL_RenderTexture(target, Background.get(), nullptr, &rect))
	    || !SDL_RenderTexture(target, Overlay.get(), nullptr, nullptr)) return false;
#else
	const SDL_Rect rect { static_cast<int>((gnScreenWidth - width) / 2), static_cast<int>((gnScreenHeight - height) / 2), static_cast<int>(width), static_cast<int>(height) };
	if (SDL_UpdateTexture(Overlay.get(), nullptr, Pixels.get(), ui->w * 4) < 0
	    || (Active && SDL_RenderCopy(target, Background.get(), nullptr, &rect) < 0)
	    || SDL_RenderCopy(target, Overlay.get(), nullptr, nullptr) < 0) return false;
#endif
	return true;
#endif
}

} // namespace devilution
