#include "engine/render/d3d_menu_presentation.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "utils/display.h"
#include "utils/log.hpp"
#include "utils/png.h"
#include "utils/sdl_ptrs.h"

namespace devilution {
namespace {
bool Active = false;
int Fade = 0;
#ifndef USE_SDL1
SDL_Renderer *Owner = nullptr;
SDLTextureUniquePtr Background;
SDLTextureUniquePtr Overlay;
int ImageWidth = 0, ImageHeight = 0, OverlayWidth = 0, OverlayHeight = 0;
std::vector<uint32_t> Pixels;

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

bool SetD3dMainMenuActive(bool active)
{
	Active = false;
	Fade = 0;
#ifndef USE_SDL1
	Active = active && renderer != nullptr && EnsureBackground(renderer);
#endif
	return Active;
}

void SetD3dMainMenuFade(int value) { Fade = std::clamp(value, 0, 256); }
bool IsD3dMainMenuActive() { return Active; }

void ResetD3dMainMenuResources()
{
#ifndef USE_SDL1
	Overlay.reset();
	Background.reset();
	Owner = nullptr;
	OverlayWidth = OverlayHeight = 0;
	Pixels.clear();
#endif
}

bool RenderD3dMainMenu(SDL_Renderer *target, SDL_Surface *ui)
{
#ifdef USE_SDL1
	return false;
#else
	if (!Active || target == nullptr || ui == nullptr || !EnsureBackground(target) || SDLC_SURFACE_BITSPERPIXEL(ui) != 8) return false;
	if (!Overlay || OverlayWidth != ui->w || OverlayHeight != ui->h) {
		Overlay.reset(SDL_CreateTexture(target, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, ui->w, ui->h));
		if (!Overlay) return false;
		OverlayWidth = ui->w; OverlayHeight = ui->h;
		Pixels.resize(static_cast<size_t>(ui->w) * ui->h);
		SDL_SetTextureBlendMode(Overlay.get(), SDL_BLENDMODE_BLEND);
	}
	std::array<uint32_t, 256> colors {};
	for (size_t i = 1; i < colors.size(); ++i) {
		const auto &c = system_palette[i];
		const std::array<uint8_t, 4> bytes { c.r, c.g, c.b, 255 };
		std::memcpy(&colors[i], bytes.data(), 4);
	}
	for (int y = 0; y < ui->h; ++y) {
		const auto *row = static_cast<const uint8_t *>(ui->pixels) + static_cast<size_t>(y) * ui->pitch;
		for (int x = 0; x < ui->w; ++x)
			Pixels[static_cast<size_t>(y) * ui->w + x] = colors[row[x]];
	}
	const float scale = std::max(static_cast<float>(gnScreenWidth) / ImageWidth, static_cast<float>(gnScreenHeight) / ImageHeight);
	const float width = ImageWidth * scale, height = ImageHeight * scale;
	SDL_SetTextureAlphaMod(Background.get(), static_cast<Uint8>(Fade * 255 / 256));
#ifdef USE_SDL3
	const SDL_FRect rect { (gnScreenWidth - width) / 2, (gnScreenHeight - height) / 2, width, height };
	if (!SDL_UpdateTexture(Overlay.get(), nullptr, Pixels.data(), ui->w * 4)
	    || !SDL_RenderTexture(target, Background.get(), nullptr, &rect)
	    || !SDL_RenderTexture(target, Overlay.get(), nullptr, nullptr)) return false;
#else
	const SDL_Rect rect { static_cast<int>((gnScreenWidth - width) / 2), static_cast<int>((gnScreenHeight - height) / 2), static_cast<int>(width), static_cast<int>(height) };
	if (SDL_UpdateTexture(Overlay.get(), nullptr, Pixels.data(), ui->w * 4) < 0
	    || SDL_RenderCopy(target, Background.get(), nullptr, &rect) < 0
	    || SDL_RenderCopy(target, Overlay.get(), nullptr, nullptr) < 0) return false;
#endif
	return true;
#endif
}

} // namespace devilution
