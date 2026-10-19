#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include <function_ref.hpp>

#include "engine/clx_sprite.hpp"
#include "engine/surface.hpp"
#include "utils/sdl_compat.h"

namespace devilution {

using HeroPortraitPalette = std::array<SDL_Color, 256>;

enum class HeroPortraitPaletteBasis : uint8_t {
	Asset,
	// The legacy selector inherited the selhero background palette. Its relation
	// to the atlas' embedded palette still requires an asset/pixel comparison.
	LegacySelHero,
};

struct HeroPortrait {
	int width = 0;
	int height = 0;
	HeroPortraitPalette sourcePalette {};
	HeroPortraitPalette palette {};
	HeroPortraitPaletteBasis paletteBasis = HeroPortraitPaletteBasis::Asset;
	bool hasSourcePalette = false;
	// Top-down indices, followed by an independent opacity plane. Index 0 is
	// an opaque color whenever the CLX command says so.
	std::unique_ptr<uint8_t[]> raster;
	// Borrowed from ArtHero/override. The dialog frees this object first.
	std::optional<ClxSprite> sourceSprite;

	[[nodiscard]] const uint8_t *indices() const { return raster.get(); }
	[[nodiscard]] const uint8_t *opacity() const { return raster.get() + static_cast<size_t>(width) * height; }
};

// Reads only the palette trailer/sidecar, without extracting or reloading pixels.
[[nodiscard]] std::optional<HeroPortraitPalette> ReadHeroPortraitPalette(const char *assetStem);
// Opaque pixels only; coordinates are top-down. False means malformed/oversize.
[[nodiscard]] bool VisitHeroPortraitClxPixels(ClxSprite sprite, tl::function_ref<void(int, int, uint8_t)> visitor);
[[nodiscard]] std::optional<HeroPortrait> DecodeHeroPortrait(ClxSprite sprite, const HeroPortraitPalette &palette,
    HeroPortraitPaletteBasis basis, const std::optional<HeroPortraitPalette> &sourcePalette);
[[nodiscard]] SDL_Rect FitHeroPortrait(int sourceWidth, int sourceHeight, const SDL_Rect &box);
// Prepares the entire palette mapping before writing; never mutates the global
// palette. Colors may be quantized when the RGB compositor is unavailable.
[[nodiscard]] bool DrawHeroPortraitIndexed(const Surface &out, const HeroPortrait &portrait, const SDL_Rect &rect);

} // namespace devilution
