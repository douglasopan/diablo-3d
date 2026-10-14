#include "DiabloUI/hero/hero_portrait.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "utils/sdl_geometry.h"

namespace devilution {
namespace {
constexpr int MaxSourceDimension = 512;
constexpr size_t MaxPaletteAssetBytes = 64 * 1024 * 1024;
} // namespace

std::optional<HeroPortraitPalette> ReadHeroPortraitPalette(const char *assetStem)
{
	if (assetStem == nullptr) return std::nullopt;
	char path[512];
#ifdef UNPACKED_MPQS
	const int length = std::snprintf(path, sizeof(path), "%s.pal", assetStem);
#else
	const int length = std::snprintf(path, sizeof(path), "%s.pcx", assetStem);
#endif
	if (length < 0 || static_cast<size_t>(length) >= sizeof(path)) return std::nullopt;
	size_t size = 0;
	auto asset = OpenAsset(path, size);
	if (!asset.ok() || size > MaxPaletteAssetBytes || size > static_cast<size_t>(std::numeric_limits<long>::max())) return std::nullopt;
	std::array<uint8_t, 768> colors {};
#ifdef UNPACKED_MPQS
	if (size != colors.size() || !asset.read(colors.data(), colors.size())) return std::nullopt;
#else
	std::array<uint8_t, 128> header {};
	if (size < header.size() + 769 || !asset.read(header.data(), header.size())
	    || header[0] != 10 || header[2] != 1 || header[3] != 8 || header[65] != 1) return std::nullopt;
	uint8_t marker = 0;
	if (!asset.seek(static_cast<long>(size - 769)) || !asset.read(&marker, 1)
	    || marker != 12 || !asset.read(colors.data(), colors.size())) return std::nullopt;
#endif
	HeroPortraitPalette palette {};
	for (size_t i = 0; i < palette.size(); ++i) {
		palette[i].r = colors[i * 3];
		palette[i].g = colors[i * 3 + 1];
		palette[i].b = colors[i * 3 + 2];
#ifndef USE_SDL1
		palette[i].a = 255;
#endif
	}
	return palette;
}

bool VisitHeroPortraitClxPixels(ClxSprite sprite, tl::function_ref<void(int, int, uint8_t)> visitor)
{
	const int width = sprite.width(), height = sprite.height();
	if (width <= 0 || height <= 0 || width > MaxSourceDimension || height > MaxSourceDimension) return false;
	const size_t count = static_cast<size_t>(width) * height;
	const auto *data = sprite.pixelData();
	const size_t dataSize = sprite.pixelDataSize();
	// CLX is already loaded/validated by the engine. This bounds our own walk,
	// including malformed command streams supplied by a fixture or override.
	if (dataSize > count * 2) return false;
	size_t position = 0, offset = 0;
	while (offset < dataSize) {
		const uint8_t command = data[offset++];
		const bool opaque = command >= 0x80;
		const bool fill = opaque && command <= 0xBE;
		const size_t length = !opaque ? command : fill ? 0xBF - command : 256 - command;
		if (length > count - position) return false;
		if (!opaque) {
			position += length;
			continue;
		}
		const size_t bytes = fill ? 1 : length;
		if (bytes > dataSize - offset) return false;
		for (size_t i = 0; i < length; ++i) {
			const size_t pixel = position + i;
			visitor(static_cast<int>(pixel % width), height - 1 - static_cast<int>(pixel / width), data[offset + (fill ? 0 : i)]);
		}
		position += length;
		offset += bytes;
	}
	return position == count;
}

std::optional<HeroPortrait> DecodeHeroPortrait(ClxSprite sprite, const HeroPortraitPalette &palette,
    HeroPortraitPaletteBasis basis, const std::optional<HeroPortraitPalette> &sourcePalette)
{
	if (sprite.width() == 0 || sprite.height() == 0 || sprite.width() > MaxSourceDimension || sprite.height() > MaxSourceDimension) return std::nullopt;
	HeroPortrait portrait;
	portrait.width = sprite.width();
	portrait.height = sprite.height();
	portrait.palette = palette;
	portrait.paletteBasis = basis;
	portrait.hasSourcePalette = sourcePalette.has_value();
	portrait.sourcePalette = sourcePalette.value_or(palette);
	portrait.sourceSprite = sprite;
	if (!VisitHeroPortraitClxPixels(sprite, [](int, int, uint8_t) {})) return std::nullopt;
	const size_t count = static_cast<size_t>(portrait.width) * portrait.height;
	portrait.raster.reset(new (std::nothrow) uint8_t[count * 2] {});
	// Allocation failure retains a validated CLX and its palette for the
	// complete indexed fallback; the RGB contribution requires the raster.
	if (!portrait.raster) return portrait;
	if (!VisitHeroPortraitClxPixels(sprite, [&](int x, int y, uint8_t index) {
		    const size_t position = static_cast<size_t>(y) * portrait.width + x;
		    portrait.raster[position] = index;
		    portrait.raster[count + position] = 1;
	    })) return std::nullopt;
	return portrait;
}

SDL_Rect FitHeroPortrait(int sourceWidth, int sourceHeight, const SDL_Rect &box)
{
	if (sourceWidth <= 0 || sourceHeight <= 0 || sourceWidth > MaxSourceDimension || sourceHeight > MaxSourceDimension
	    || box.w <= 0 || box.h <= 0 || box.w > 216 || box.h > 100
	    || static_cast<int64_t>(box.x) + box.w > std::numeric_limits<int>::max()
	    || static_cast<int64_t>(box.y) + box.h > std::numeric_limits<int>::max()) return MakeSdlRect(box.x, box.y, 0, 0);
	int width = box.w, height = box.h;
	if (static_cast<int64_t>(box.w) * sourceHeight <= static_cast<int64_t>(box.h) * sourceWidth)
		height = std::max(1, box.w * sourceHeight / sourceWidth);
	else
		width = std::max(1, box.h * sourceWidth / sourceHeight);
	return MakeSdlRect(box.x + (box.w - width) / 2, box.y + (box.h - height) / 2, width, height);
}

bool DrawHeroPortraitIndexed(const Surface &out, const HeroPortrait &portrait, const SDL_Rect &rect)
{
	if (out.surface == nullptr || out.surface->pixels == nullptr || SDLC_SURFACE_BITSPERPIXEL(out.surface) != 8
	    || out.surface->w <= 0 || out.surface->h <= 0 || out.surface->pitch < out.surface->w
	    || out.region.x < 0 || out.region.y < 0 || out.w() < 0 || out.h() < 0
	    || static_cast<int64_t>(out.region.x) + out.w() > out.surface->w
	    || static_cast<int64_t>(out.region.y) + out.h() > out.surface->h
	    || (!portrait.raster && !portrait.sourceSprite) || portrait.width <= 0 || portrait.height <= 0
	    || portrait.width > MaxSourceDimension || portrait.height > MaxSourceDimension
	    || rect.w <= 0 || rect.h <= 0 || rect.w > 216 || rect.h > 100
	    || static_cast<int64_t>(rect.x) + rect.w > std::numeric_limits<int>::max()
	    || static_cast<int64_t>(rect.y) + rect.h > std::numeric_limits<int>::max()) return false;
	if (!portrait.raster && (portrait.sourceSprite->width() != portrait.width || portrait.sourceSprite->height() != portrait.height
	        || !VisitHeroPortraitClxPixels(*portrait.sourceSprite, [](int, int, uint8_t) {}))) return false;
	std::array<uint8_t, 256> mapping {};
	for (size_t index = 0; index < mapping.size(); ++index) {
		const auto &source = portrait.palette[index];
		int closest = std::numeric_limits<int>::max();
		// 0 means transparent in the RGB menu overlay, independently of CLX.
		// Map opaque black to an opaque palette entry before drawing the fallback.
		for (size_t i = 1; i < logical_palette.size(); ++i) {
			const auto &target = logical_palette[i];
			const int r = static_cast<int>(source.r) - target.r;
			const int g = static_cast<int>(source.g) - target.g;
			const int b = static_cast<int>(source.b) - target.b;
			const int distance = r * r + g * g + b * b;
			if (distance < closest) {
				closest = distance;
				mapping[index] = static_cast<uint8_t>(i);
			}
		}
	}
	const int left = std::max(0, static_cast<int>(rect.x)), top = std::max(0, static_cast<int>(rect.y));
	const int right = static_cast<int>(std::min<int64_t>(out.w(), static_cast<int64_t>(rect.x) + rect.w));
	const int bottom = static_cast<int>(std::min<int64_t>(out.h(), static_cast<int64_t>(rect.y) + rect.h));
	if (!portrait.raster) {
		// The complete stream and LUT were validated before this first write.
		// This is rendering only; no dialog handler or simulation is replayed.
		return VisitHeroPortraitClxPixels(*portrait.sourceSprite, [&](int sx, int sy, uint8_t index) {
			const int x0 = rect.x + (sx * rect.w + portrait.width - 1) / portrait.width;
			const int x1 = rect.x + ((sx + 1) * rect.w + portrait.width - 1) / portrait.width;
			const int y0 = rect.y + (sy * rect.h + portrait.height - 1) / portrait.height;
			const int y1 = rect.y + ((sy + 1) * rect.h + portrait.height - 1) / portrait.height;
			for (int y = std::max(top, y0); y < std::min(bottom, y1); ++y)
				for (int x = std::max(left, x0); x < std::min(right, x1); ++x)
					out.SetPixel({ x, y }, mapping[index]);
		});
	}
	for (int y = top; y < bottom; ++y) {
		const int sy = (y - rect.y) * portrait.height / rect.h;
		for (int x = left; x < right; ++x) {
			const int sx = (x - rect.x) * portrait.width / rect.w;
			const size_t position = static_cast<size_t>(sy) * portrait.width + sx;
			if (portrait.opacity()[position] != 0)
				out.SetPixel({ x, y }, mapping[portrait.indices()[position]]);
		}
	}
	return true;
}

} // namespace devilution
