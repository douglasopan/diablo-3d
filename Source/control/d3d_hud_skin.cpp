#include "control/d3d_hud_skin.hpp"

#include "control/d3d_hud.hpp"
#include "control/d3d_hud_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/surface.hpp"
#include "utils/png.h"
#include "utils/sdl_ptrs.h"

namespace devilution {
namespace {

constexpr D3dHudSkinSlice Chassis { { 22, 190, 2128, 331 }, { 80, 64, 80, 64 }, { 6, 5, 6, 5 } };
constexpr D3dHudSkinSlice Button { { 62, 143, 1968, 458 }, { 110, 84, 110, 84 }, { 3, 3, 3, 3 } };
constexpr D3dHudSkinSlice Inset { { 12, 192, 1960, 409 }, { 70, 62, 70, 62 }, { 2, 2, 2, 2 } };
constexpr SDL_Rect OrbFrameSource { 15, 66, 1061, 1283 };
constexpr float OrbCircleX = 653.28F;
constexpr float OrbCircleY = 606.48F;
constexpr float OrbCircleRadius = 392.30F;
constexpr std::array<SDL_Rect, 2> LiquidSources { SDL_Rect { 58, 54, 1137, 1136 }, SDL_Rect { 55, 52, 1144, 1143 } };
constexpr std::array<const char *, 6> Paths {
	"d3d-ui/hud/r1/chassis.png", "d3d-ui/hud/r1/button.png", "d3d-ui/hud/r1/inset.png",
	"d3d-ui/hud/r1/orb-frame.png", "d3d-ui/hud/r1/orb-red.png", "d3d-ui/hud/r1/orb-blue.png"
};
constexpr size_t PartCount = static_cast<size_t>(D3dHudSkinPart::Count);
std::array<D3dHudSkinImage, PartCount> Images;
bool Loaded = false;
D3dHudSkinCounters Counters;
std::array<SDL_Color, 256> QuantizedPalette;
std::array<uint8_t, 32 * 32 * 32> QuantizedColors;
bool QuantizerReady = false;

bool Fits(const D3dHudSkinImage &image, const SDL_Rect &source)
{
	return image.width > 0 && image.height > 0 && image.width <= 4096 && image.height <= 4096
	    && source.x >= 0 && source.y >= 0 && source.w > 0 && source.h > 0
	    && source.x + source.w <= image.width && source.y + source.h <= image.height;
}

std::expected<D3dHudSkinImage, std::string> ReadImage(const char *path)
{
	if (!FindAsset(path).ok())
		return std::unexpected(std::string("Required R1 HUD asset is missing: ") + path);
	++Counters.pngDecodes;
	SDLSurfaceUniquePtr source { LoadPNG(path) };
	if (!source || source->w <= 0 || source->h <= 0 || source->w > 4096 || source->h > 4096)
		return std::unexpected(std::string("Required R1 HUD asset cannot be decoded: ") + path);
#ifdef USE_SDL3
	SDLSurfaceUniquePtr colors { SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_ARGB8888) };
#elif defined(USE_SDL1)
	SDLSurfaceUniquePtr format { SDL_CreateRGBSurface(0, 1, 1, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000) };
	SDLSurfaceUniquePtr colors { format ? SDL_ConvertSurface(source.get(), format->format, 0) : nullptr };
#else
	SDLSurfaceUniquePtr colors { SDL_ConvertSurfaceFormat(source.get(), SDL_PIXELFORMAT_ARGB8888, 0) };
#endif
	if (!colors)
		return std::unexpected(std::string("Required R1 HUD asset cannot be converted: ") + path);
	D3dHudSkinImage image { colors->w, colors->h, {} };
	image.pixels.resize(static_cast<size_t>(image.width) * image.height);
	for (int y = 0; y < image.height; ++y)
		std::memcpy(image.pixels.data() + static_cast<size_t>(y) * image.width,
		    static_cast<const uint8_t *>(colors->pixels) + y * colors->pitch, static_cast<size_t>(image.width) * 4);
	return image;
}

void DeriveLiquid(size_t index)
{
	auto &image = Images[static_cast<size_t>(D3dHudSkinPart::LifeLiquid) + index];
	const SDL_Rect source = LiquidSources[index];
	const int side = std::min(source.w, source.h);
	const int left = source.x + (source.w - side) / 2;
	const int top = source.y + (source.h - side) / 2;
	const float radius = (side - 1) / 2.0F;
	D3dHudSkinImage liquid { side, side, {} };
	liquid.pixels.resize(static_cast<size_t>(side) * side);
	auto &empty = Images[static_cast<size_t>(D3dHudSkinPart::EmptyOrb)];
	if (index == 0) {
		empty = { side, side, {} };
		empty.pixels.resize(liquid.pixels.size());
	}
	for (int y = 0; y < side; ++y) {
		for (int x = 0; x < side; ++x) {
			const float dx = x - radius, dy = y - radius;
			const float edge = std::clamp(radius + 0.5F - std::sqrt(dx * dx + dy * dy), 0.0F, 1.0F);
			const uint32_t color = image.pixels[static_cast<size_t>(top + y) * image.width + left + x];
			const uint32_t alpha = static_cast<uint32_t>((color >> 24) * edge);
			const size_t destination = static_cast<size_t>(y) * side + x;
			liquid.pixels[destination] = (color & 0x00FFFFFFU) | (alpha << 24);
			if (index == 0) {
				const uint32_t minimum = std::min({ (color >> 16) & 255U, (color >> 8) & 255U, color & 255U });
				const uint32_t shade = static_cast<uint32_t>(minimum * 0.30F);
				empty.pixels[destination] = (static_cast<uint32_t>(255 * edge) << 24)
				    | ((4 + shade) << 16) | ((5 + shade) << 8) | (7 + shade);
			}
		}
	}
	image = std::move(liquid);
}

void EnsureQuantizer()
{
	bool same = QuantizerReady;
	for (size_t i = 0; same && i < logical_palette.size(); ++i) {
		const auto &a = QuantizedPalette[i];
		const auto &b = logical_palette[i];
		same = a.r == b.r && a.g == b.g && a.b == b.b;
	}
	if (same)
		return;
	QuantizedPalette = logical_palette;
	for (int r = 0; r < 32; ++r) {
		for (int g = 0; g < 32; ++g) {
			for (int b = 0; b < 32; ++b) {
				int bestDistance = std::numeric_limits<int>::max();
				uint8_t best = 0;
				for (int i = 0; i < 256; ++i) {
					const auto &color = QuantizedPalette[i];
					const int dr = r * 8 + 4 - color.r, dg = g * 8 + 4 - color.g, db = b * 8 + 4 - color.b;
					const int distance = dr * dr + dg * dg + db * db;
					if (distance < bestDistance) {
						bestDistance = distance;
						best = static_cast<uint8_t>(i);
					}
				}
				QuantizedColors[static_cast<size_t>((r * 32 + g) * 32 + b)] = best;
			}
		}
	}
	QuantizerReady = true;
}

uint8_t Quantize(unsigned r, unsigned g, unsigned b)
{
	return QuantizedColors[((r >> 3) * 32 + (g >> 3)) * 32 + (b >> 3)];
}

void PaintPixel(const Surface &out, int x, int y, uint32_t color, uint8_t shade)
{
	const unsigned alpha = color >> 24;
	if (alpha == 0)
		return;
	const auto &background = logical_palette[*out.at(x, y)];
	const unsigned r = (((color >> 16) & 255U) * shade / 255 * alpha + background.r * (255 - alpha) + 127) / 255;
	const unsigned g = (((color >> 8) & 255U) * shade / 255 * alpha + background.g * (255 - alpha) + 127) / 255;
	const unsigned b = ((color & 255U) * shade / 255 * alpha + background.b * (255 - alpha) + 127) / 255;
	// The common observer is called BEFORE pixels change. It can materialize a
	// deferred HUD beneath native translucent UI without replaying that UI.
	MarkUiOverlayRect(out, x, y, 1, 1);
	*out.at(x, y) = Quantize(r, g, b);
	++Counters.indexedPixels;
}

void PaintImage(const Surface &out, const D3dHudSkinImage &image, SDL_Rect source,
    D3dHudSkinFloatRect destination, uint8_t shade = 255, bool flip = false)
{
	if (!Loaded || destination.w <= 0 || destination.h <= 0 || !Fits(image, source))
		return;
	const int left = std::max(0, static_cast<int>(std::lround(destination.x)));
	const int top = std::max(0, static_cast<int>(std::lround(destination.y)));
	const int right = std::min(out.w(), static_cast<int>(std::lround(destination.x + destination.w)));
	const int bottom = std::min(out.h(), static_cast<int>(std::lround(destination.y + destination.h)));
	for (int y = top; y < bottom; ++y) {
		const int sy = source.y + std::clamp(static_cast<int>((y + 0.5F - destination.y) * source.h / destination.h), 0, source.h - 1);
		for (int x = left; x < right; ++x) {
			int sx = std::clamp(static_cast<int>((x + 0.5F - destination.x) * source.w / destination.w), 0, source.w - 1);
			if (flip)
				sx = source.w - 1 - sx;
			PaintPixel(out, x, y, image.pixels[static_cast<size_t>(sy) * image.width + source.x + sx], shade);
		}
	}
}

} // namespace

std::expected<void, std::string> LoadD3dHudSkin()
{
	if (Loaded)
		return {};
	std::array<D3dHudSkinImage, PartCount> decoded;
	const std::array<SDL_Rect, 6> sources { Chassis.source, Button.source, Inset.source, OrbFrameSource, LiquidSources[0], LiquidSources[1] };
	for (size_t i = 0; i < Paths.size(); ++i) {
		auto image = ReadImage(Paths[i]);
		if (!image)
			return std::unexpected(image.error());
		if (!Fits(*image, sources[i]))
			return std::unexpected(std::string("Required R1 HUD asset has invalid dimensions: ") + Paths[i]);
		decoded[i] = std::move(*image);
	}
	Images = std::move(decoded);
	DeriveLiquid(0);
	DeriveLiquid(1);
	Images[static_cast<size_t>(D3dHudSkinPart::BeltTint)] = { 1, 1, { 0x20FFFFFFU } };
	Loaded = true;
	return {};
}

void FreeD3dHudSkin()
{
	Images = {};
	Loaded = false;
	QuantizerReady = false;
}

bool IsD3dHudSkinLoaded() { return Loaded; }

const D3dHudSkinImage &GetD3dHudSkinImage(D3dHudSkinPart part)
{
	return Images[static_cast<size_t>(part)];
}

const D3dHudSkinSlice &GetD3dHudSkinSlice(D3dHudSkinPart part)
{
	if (part == D3dHudSkinPart::Chassis)
		return Chassis;
	if (part == D3dHudSkinPart::Button)
		return Button;
	return Inset;
}

SDL_Rect GetD3dHudSkinOrbFrameSource() { return OrbFrameSource; }

D3dHudSkinOrbPlacement PlaceD3dHudSkinOrb(Rectangle target, bool mana)
{
	const float scale = std::min(static_cast<float>(target.size.width) / OrbFrameSource.w,
	    static_cast<float>(target.size.height) / OrbFrameSource.h);
	const float width = OrbFrameSource.w * scale, height = OrbFrameSource.h * scale;
	const D3dHudSkinFloatRect frame { target.position.x + (target.size.width - width) / 2,
		target.position.y + target.size.height - height, width, height };
	const float localCenter = (OrbCircleX - OrbFrameSource.x) * scale;
	return { frame, frame.x + (mana ? width - localCenter : localCenter),
		frame.y + (OrbCircleY - OrbFrameSource.y) * scale, (OrbCircleRadius + 2) * scale };
}

void DrawD3dHudSkinIndexedPlate(const Surface &out, Rectangle rect, bool pressed, D3dHudSkinPart part, float designScaleX, float designScaleY)
{
	DrawD3dHudSkinIndexedPlateShade(out, rect, part, pressed ? 184 : 255, designScaleX, designScaleY);
}

void DrawD3dHudSkinIndexedPlateShade(const Surface &out, Rectangle rect, D3dHudSkinPart part, uint8_t shade, float designScaleX, float designScaleY)
{
	if (!Loaded || out.surface == nullptr || SDLC_SURFACE_BITSPERPIXEL(out.surface) != 8)
		return;
	++Counters.indexedPlates;
	EnsureQuantizer();
	const auto &slice = GetD3dHudSkinSlice(part);
	const auto &image = GetD3dHudSkinImage(part);
	const auto &metrics = GetD3dHudMetrics();
	const float scaleX = designScaleX > 0 ? designScaleX : IsD3dHudLayoutActive() ? metrics.designScaleX : 1.0F;
	const float scaleY = designScaleY > 0 ? designScaleY : IsD3dHudLayoutActive() ? metrics.designScaleY : 1.0F;
	const float left = std::min(slice.logicalMargins[0] * scaleX, rect.size.width / 2.0F);
	const float top = std::min(slice.logicalMargins[1] * scaleY, rect.size.height / 2.0F);
	const float right = std::min(slice.logicalMargins[2] * scaleX, rect.size.width - left);
	const float bottom = std::min(slice.logicalMargins[3] * scaleY, rect.size.height - top);
	const std::array<int, 4> sx { slice.source.x, slice.source.x + slice.sourceMargins[0],
		slice.source.x + slice.source.w - slice.sourceMargins[2], slice.source.x + slice.source.w };
	const std::array<int, 4> sy { slice.source.y, slice.source.y + slice.sourceMargins[1],
		slice.source.y + slice.source.h - slice.sourceMargins[3], slice.source.y + slice.source.h };
	const std::array<float, 4> dx { static_cast<float>(rect.position.x), rect.position.x + left,
		rect.position.x + rect.size.width - right, static_cast<float>(rect.position.x + rect.size.width) };
	const std::array<float, 4> dy { static_cast<float>(rect.position.y), rect.position.y + top,
		rect.position.y + rect.size.height - bottom, static_cast<float>(rect.position.y + rect.size.height) };
	for (size_t y = 0; y < 3; ++y) {
		for (size_t x = 0; x < 3; ++x)
			PaintImage(out, image, { sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y] },
			    { dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y] }, shade);
	}
}

void DrawD3dHudSkinIndexedOrb(const Surface &out, Rectangle rect, bool mana, int fill)
{
	if (!Loaded || out.surface == nullptr || SDLC_SURFACE_BITSPERPIXEL(out.surface) != 8)
		return;
	++Counters.indexedOrbs;
	EnsureQuantizer();
	const auto [frame, centerX, centerY, radius] = PlaceD3dHudSkinOrb(rect, mana);
	const D3dHudSkinFloatRect globe { centerX - radius, centerY - radius, radius * 2, radius * 2 };
	const auto &empty = GetD3dHudSkinImage(D3dHudSkinPart::EmptyOrb);
	PaintImage(out, empty, { 0, 0, empty.width, empty.height }, globe);
	fill = std::clamp(fill, 0, 81);
	if (fill > 0) {
		const auto &liquid = GetD3dHudSkinImage(mana ? D3dHudSkinPart::ManaLiquid : D3dHudSkinPart::LifeLiquid);
		const int firstRow = liquid.height * (81 - fill) / 81;
		const float offset = globe.h * firstRow / liquid.height;
		PaintImage(out, liquid, { 0, firstRow, liquid.width, liquid.height - firstRow },
		    { globe.x, globe.y + offset, globe.w, globe.h - offset });
	}
	PaintImage(out, GetD3dHudSkinImage(D3dHudSkinPart::OrbFrame), OrbFrameSource, frame, 255, mana);
}

void DrawD3dHudSkinIndexedTint(const Surface &out, Rectangle rect, uint8_t paletteIndex)
{
	if (!Loaded || out.surface == nullptr || SDLC_SURFACE_BITSPERPIXEL(out.surface) != 8)
		return;
	++Counters.indexedTints;
	EnsureQuantizer();
	const auto color = logical_palette[paletteIndex];
	const uint32_t tint = 0x20000000U | (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
	for (int y = std::max(0, rect.position.y); y < std::min(out.h(), rect.position.y + rect.size.height); ++y) {
		for (int x = std::max(0, rect.position.x); x < std::min(out.w(), rect.position.x + rect.size.width); ++x)
			PaintPixel(out, x, y, tint, 255);
	}
}

D3dHudSkinCounters GetD3dHudSkinCounters() { return Counters; }
void ResetD3dHudSkinCounters() { Counters = {}; }

} // namespace devilution
