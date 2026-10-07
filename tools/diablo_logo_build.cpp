// Rebuilds local UI branding from the selected original archive; never writes the MPQ.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/load_pcx.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "utils/endian_swap.hpp"
#include "utils/paths.h"
#include "utils/pcx.hpp"
#include "utils/png.h"
#include "utils/surface_to_pcx.hpp"
#include "utils/surface_to_png.hpp"

namespace {
using namespace devilution;
constexpr int FrameCount = 15;
constexpr uint8_t Transparent = 250;
using Palette = std::array<SDL_Color, 256>;

void Require(bool condition, const std::string &message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void SavePng(const Surface &surface, const std::filesystem::path &path)
{
	SDL_IOStream *stream = SDL_IOFromFile(path.string().c_str(), "wb");
	Require(stream != nullptr, "Open output PNG: " + path.string());
	const auto result = WriteSurfaceToFilePng(surface, stream);
	Require(result.has_value(), "Write output PNG: " + (result ? path.string() : result.error()));
}

void SavePcx(const Surface &surface, const std::filesystem::path &path)
{
	SDL_IOStream *stream = SDL_IOFromFile(path.string().c_str(), "wb");
	Require(stream != nullptr, "Open output PCX: " + path.string());
	const auto result = WriteSurfaceToFilePcx(surface, stream);
	if (!result)
		SDL_CloseIO(stream);
	Require(result.has_value(), "Write output PCX: " + (result ? path.string() : result.error()));
}

// The PCX is indexed, but PNG previews need explicit alpha: SDL_image's indexed
// writer does not preserve this engine's colour-key transparency.
void SaveTransparentPng(const Surface &indexed, const Palette &palette, const std::filesystem::path &path)
{
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> rgba(
	    SDL_CreateRGBSurfaceWithFormat(0, indexed.w(), indexed.h(), 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
	Require(rgba != nullptr, "Allocate transparent preview");
	for (int y = 0; y < indexed.h(); ++y) {
		auto *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch);
		for (int x = 0; x < indexed.w(); ++x) {
			const uint8_t index = indexed[{ x, y }];
			const auto &color = palette[index];
			row[x] = index == Transparent ? SDL_MapRGBA(rgba->format, 0, 0, 0, 0)
			                            : SDL_MapRGBA(rgba->format, color.r, color.g, color.b, 255);
		}
	}
	SavePng(Surface(rgba.get()), path);
}

std::filesystem::path SelectArchive(const std::filesystem::path &data)
{
	std::filesystem::path selected;
	for (const char *name : { "DIABDAT.MPQ", "diabdat.mpq", "spawn.mpq", "SPAWN.MPQ" }) {
		const auto candidate = data / name;
		if (std::filesystem::is_regular_file(candidate)) {
			selected = candidate;
			break;
		}
	}
	Require(!selected.empty(), "Selected data directory contains no DIABDAT.MPQ or spawn.mpq");
	gbIsSpawn = selected.filename().string() == "spawn.mpq" || selected.filename().string() == "SPAWN.MPQ";
	gbIsHellfire = false;
	OverridePaths.clear();
	MpqArchives.clear();
	auto archive = MpqArchive::Open(selected.string().c_str());
	Require(archive.has_value(), archive ? "" : archive.error());
	MpqArchives.insert_or_assign(MainMpqPriority, std::move(*archive));
	std::cout << "Selected " << (gbIsSpawn ? "shareware" : "retail") << " archive: " << selected.string() << '\n';
	return selected;
}

struct Bounds {
	int left;
	int top;
	int right;
	int bottom;
};

struct Logo {
	std::string name;
	int width;
	int height;
	Palette palette;
	OwnedSurface strip;
};

struct Rgba {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

struct Numeral {
	int width;
	int height;
	std::vector<Rgba> pixels;
};

Bounds InkBounds(const Surface &frame)
{
	Bounds result { frame.w(), frame.h(), -1, -1 };
	for (int y = 0; y < frame.h(); ++y) {
		for (int x = 0; x < frame.w(); ++x) {
			if (frame[{ x, y }] == Transparent)
				continue;
			result.left = std::min(result.left, x);
			result.top = std::min(result.top, y);
			result.right = std::max(result.right, x);
			result.bottom = std::max(result.bottom, y);
		}
	}
	return result;
}

Logo ExportOriginal(const char *name, const char *background, const std::filesystem::path &output, std::ofstream &metadata, bool first)
{
	const std::string asset = std::string("ui_art\\") + name;
	const std::string pcx = asset + ".pcx";
	const auto ref = FindAsset(pcx);
	Require(ref.ok() && ref.archive == &MpqArchives.at(MainMpqPriority), "Logo must come from the explicitly selected MPQ: " + pcx);
	const auto raw = LoadAsset(pcx);
	Require(raw.has_value(), raw ? "" : raw.error());
	Require(raw->size >= PcxHeaderSize, "PCX header is missing");
	PCXHeader header;
	std::memcpy(&header, raw->data.get(), sizeof(header));
	const int width = Swap16LE(header.Xmax) - Swap16LE(header.Xmin) + 1;
	const int sheetHeight = Swap16LE(header.Ymax) - Swap16LE(header.Ymin) + 1;
	Require(header.BitsPerPixel == 8 && header.NPlanes == 1 && sheetHeight % FrameCount == 0, "Expected an indexed, 15-frame PCX sheet");
	Palette embedded;
	auto sprites = LoadPcxSpriteList(asset.c_str(), FrameCount, Transparent, embedded.data());
	Require(sprites.has_value(), "Decode original logo: " + asset);
	Palette palette;
	const std::string backgroundAsset = std::string("ui_art\\") + background;
	auto backgroundSprite = LoadPcx(backgroundAsset.c_str(), std::nullopt, palette.data());
	Require(backgroundSprite.has_value(), "Load native UI background palette: " + backgroundAsset);
	const int frameHeight = sheetHeight / FrameCount;
	OwnedSurface strip(width, sheetHeight);
	Require(strip.surface != nullptr, "Allocate original logo strip");
	SDL_SetPaletteColors(strip.surface->format->palette, palette.data(), 0, 256);
	SDL_FillRect(strip.surface, nullptr, Transparent);
	for (int frame = 0; frame < FrameCount; ++frame)
		ClxDraw(strip, { 0, (frame + 1) * frameHeight - 1 }, (*sprites)[frame]);
	SDL_SetColorKey(strip.surface, SDL_TRUE, Transparent);
	SaveTransparentPng(strip, palette, output / (std::string(name) + "-original-strip.png"));
	SavePcx(strip, output / (std::string(name) + "-original.pcx"));
	OwnedSurface frameSurface(width, frameHeight);
	SDL_SetPaletteColors(frameSurface.surface->format->palette, palette.data(), 0, 256);
	SDL_SetColorKey(frameSurface.surface, SDL_TRUE, Transparent);
	std::filesystem::create_directories(output / (std::string(name) + "-frames"));
	metadata << (first ? "" : ",\n") << "{\"asset\":" << std::quoted(pcx) << ",\"width\":" << width
		<< ",\"frameHeight\":" << frameHeight << ",\"sheetHeight\":" << sheetHeight
		<< ",\"frames\":15,\"frameIntervalMs\":60,\"transparentIndex\":250,\"bytesPerLine\":" << Swap16LE(header.BytesPerLine)
		<< ",\"paletteFrom\":" << std::quoted(backgroundAsset + ".pcx") << ",\"inkBoundsInclusive\":[";
	for (int frame = 0; frame < FrameCount; ++frame) {
		for (int y = 0; y < frameHeight; ++y)
			std::memcpy(frameSurface.at(0, y), strip.at(0, frame * frameHeight + y), width);
		const auto bounds = InkBounds(frameSurface);
		metadata << (frame == 0 ? "" : ",") << '[' << bounds.left << ',' << bounds.top << ',' << bounds.right << ',' << bounds.bottom << ']';
		SaveTransparentPng(frameSurface, palette, output / (std::string(name) + "-frames") / ("frame-" + std::to_string(frame) + ".png"));
		if (frame == 0)
			SaveTransparentPng(frameSurface, palette, output / (std::string(name) + "-original-first.png"));
	}
	metadata << "],\"paletteRGB\":[";
	for (size_t i = 0; i < palette.size(); ++i)
		metadata << (i == 0 ? "" : ",") << '[' << static_cast<int>(palette[i].r) << ',' << static_cast<int>(palette[i].g) << ',' << static_cast<int>(palette[i].b) << ']';
	metadata << "]}";
	std::cout << pcx << ": " << width << 'x' << frameHeight << " per frame, " << FrameCount << " frames, " << sheetHeight << " sheet rows\n";
	return { name, width, frameHeight, palette, std::move(strip) };
}

Numeral LoadNumeral(const std::filesystem::path &path)
{
	SDL_RWops *stream = SDL_RWFromFile(path.string().c_str(), "rb");
	Require(stream != nullptr, "Open numeral PNG: " + path.string());
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> loaded(IMG_LoadPNG_RW(stream), SDL_FreeSurface);
	SDL_RWclose(stream);
	Require(loaded != nullptr, "Load numeral PNG: " + path.string() + ": " + SDL_GetError());
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> rgba(
	    SDL_ConvertSurfaceFormat(loaded.get(), SDL_PIXELFORMAT_RGBA32, 0), SDL_FreeSurface);
	Require(rgba != nullptr, "Convert numeral to RGBA");
	std::vector<Rgba> pixels(static_cast<size_t>(rgba->w) * rgba->h);
	Bounds bounds { rgba->w, rgba->h, -1, -1 };
	bool hasTransparency = false;
	for (int y = 0; y < rgba->h; ++y) {
		const auto *row = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch);
		for (int x = 0; x < rgba->w; ++x) {
			auto &pixel = pixels[static_cast<size_t>(y) * rgba->w + x];
			SDL_GetRGBA(row[x], rgba->format, &pixel.r, &pixel.g, &pixel.b, &pixel.a);
			hasTransparency |= pixel.a == 0;
			if (pixel.a < 64)
				continue;
			bounds.left = std::min(bounds.left, x);
			bounds.top = std::min(bounds.top, y);
			bounds.right = std::max(bounds.right, x);
			bounds.bottom = std::max(bounds.bottom, y);
		}
	}
	Require(hasTransparency && bounds.right >= bounds.left, "Numeral PNG must have a visible glyph and transparent background");
	Numeral result { bounds.right - bounds.left + 1, bounds.bottom - bounds.top + 1, {} };
	result.pixels.resize(static_cast<size_t>(result.width) * result.height);
	for (int y = 0; y < result.height; ++y)
		std::copy_n(pixels.begin() + static_cast<size_t>(y + bounds.top) * rgba->w + bounds.left,
		    result.width, result.pixels.begin() + static_cast<size_t>(y) * result.width);
	std::cout << "Numeral RGBA crop: " << result.width << 'x' << result.height << '\n';
	return result;
}

uint8_t NearestPalette(const Palette &palette, int red, int green, int blue)
{
	int bestDistance = 1000000;
	uint8_t best = 0;
	for (int i = 0; i < 256; ++i) {
		if (i == Transparent)
			continue;
		const int dr = static_cast<int>(palette[i].r) - red;
		const int dg = static_cast<int>(palette[i].g) - green;
		const int db = static_cast<int>(palette[i].b) - blue;
		const int distance = dr * dr + dg * dg + db * db;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = static_cast<uint8_t>(i);
		}
	}
	return best;
}

// Area sampling keeps the narrow metallic edge legible when reducing the large
// generated numeral to the original pixel-art letter height.
std::vector<uint8_t> QuantizeNumeral(const Numeral &numeral, int width, int height, const Palette &palette)
{
	std::vector<uint8_t> result(static_cast<size_t>(width) * height, Transparent);
	for (int y = 0; y < height; ++y) {
		const double top = static_cast<double>(y) * numeral.height / height;
		const double bottom = static_cast<double>(y + 1) * numeral.height / height;
		for (int x = 0; x < width; ++x) {
			const double left = static_cast<double>(x) * numeral.width / width;
			const double right = static_cast<double>(x + 1) * numeral.width / width;
			double coverage = 0;
			double red = 0;
			double green = 0;
			double blue = 0;
			for (int sy = static_cast<int>(top); sy < std::ceil(bottom) && sy < numeral.height; ++sy) {
				const double vertical = std::max(0.0, std::min(bottom, static_cast<double>(sy + 1)) - std::max(top, static_cast<double>(sy)));
				for (int sx = static_cast<int>(left); sx < std::ceil(right) && sx < numeral.width; ++sx) {
					const auto &pixel = numeral.pixels[static_cast<size_t>(sy) * numeral.width + sx];
					const double horizontal = std::max(0.0, std::min(right, static_cast<double>(sx + 1)) - std::max(left, static_cast<double>(sx)));
					const double weight = vertical * horizontal * pixel.a / 255.0;
					coverage += weight;
					red += weight * pixel.r;
					green += weight * pixel.g;
					blue += weight * pixel.b;
				}
			}
			if (coverage < 0.5 * (bottom - top) * (right - left))
				continue;
			result[static_cast<size_t>(y) * width + x] = NearestPalette(palette,
			    static_cast<int>(std::lround(red / coverage)), static_cast<int>(std::lround(green / coverage)), static_cast<int>(std::lround(blue / coverage)));
		}
	}
	return result;
}

bool IsWarm(const SDL_Color &color)
{
	return color.r > 30 && color.r > color.g + 4 && color.r > color.b + 8;
}

std::vector<bool> DynamicFlameMask(const Logo &logo, int donorWidth)
{
	std::vector<bool> result(static_cast<size_t>(donorWidth) * logo.height, false);
	for (int y = 0; y < logo.height; ++y) {
		for (int x = 0; x < donorWidth; ++x) {
			const uint8_t first = logo.strip[{ x, y }];
			bool varies = false;
			bool hasFlame = false;
			for (int frame = 0; frame < FrameCount; ++frame) {
				const uint8_t index = logo.strip[{ x, frame * logo.height + y }];
				varies |= index != first;
				hasFlame |= index != Transparent && IsWarm(logo.palette[index]);
			}
			result[static_cast<size_t>(y) * donorWidth + x] = varies && hasFlame;
		}
	}
	return result;
}

void VerifyOverride(const Logo &original, const Surface &expected, const std::filesystem::path &output, bool unchangedPrefix)
{
	const std::string asset = "ui_art\\" + original.name;
	OverridePaths.emplace_back(output.string() + "/");
	const auto ref = FindAsset(asset + ".pcx");
	Require(ref.ok() && ref.isOverridden && ref.directHandle != nullptr && ref.archive == nullptr,
	    "Generated logo must resolve through the game's loose-file override: " + asset);
	auto sprites = LoadPcxSpriteList(asset.c_str(), FrameCount, Transparent);
	Require(sprites.has_value(), "Reload generated 15-frame PCX: " + asset);
	OwnedSurface decoded(expected.w(), expected.h());
	SDL_FillRect(decoded.surface, nullptr, Transparent);
	for (int frame = 0; frame < FrameCount; ++frame)
		ClxDraw(decoded, { 0, (frame + 1) * original.height - 1 }, (*sprites)[frame]);
	for (int y = 0; y < expected.h(); ++y) {
		Require(std::memcmp(expected.at(0, y), decoded.at(0, y), expected.w()) == 0,
		    "PCX/native decoder round-trip differs at row " + std::to_string(y));
		if (unchangedPrefix)
			Require(std::memcmp(expected.at(0, y), original.strip.at(0, y), original.width) == 0,
			    "Original DIABLO pixels changed at row " + std::to_string(y));
	}
	OverridePaths.clear();
}

void ComposeLogo(const Logo &original, const Numeral &numeral, const std::filesystem::path &output, std::ofstream &metadata, bool first)
{
	const bool menu = original.name == "smlogo";
	Require(original.width == (menu ? 390 : 550) && original.height == (menu ? 154 : 216),
	    "This local compositor expects the verified original Diablo logo dimensions");
	const int donorWidth = menu ? 95 : 133;
	const int glyphHeight = menu ? 74 : 104;
	const int glyphWidth = menu ? 41 : 58;
	const int baseline = menu ? 151 : 213;
	const int wordGap = menu ? 10 : 15;
	const int numeralField = menu ? 85 : 120;
	const int numeralX = original.width + wordGap;
	const int finalDX = numeralX + numeralField;
	const int compositeWidth = finalDX + donorWidth;
	const int targetWidth = menu ? compositeWidth : 620;
	Require(targetWidth <= 640 && targetWidth % 2 == 0, "PCX width must fit the UI and be even");
	OwnedSurface composite(compositeWidth, original.height * FrameCount);
	SDL_SetPaletteColors(composite.surface->format->palette, original.palette.data(), 0, 256);
	SDL_FillRect(composite.surface, nullptr, Transparent);
	const auto glyph = QuantizeNumeral(numeral, glyphWidth, glyphHeight, original.palette);
	const auto flameMask = DynamicFlameMask(original, donorWidth);
	const int glyphLeft = numeralX + (numeralField - glyphWidth) / 2;
	const int glyphTop = baseline - glyphHeight + 1;
	int varyingFlamePixels = 0;
	for (int frame = 0; frame < FrameCount; ++frame) {
		for (int y = 0; y < original.height; ++y) {
			const int sheetY = frame * original.height + y;
			std::memcpy(composite.at(0, sheetY), original.strip.at(0, sheetY), original.width);
			std::memcpy(composite.at(finalDX, sheetY), original.strip.at(0, sheetY), donorWidth);
			for (int x = 0; x < numeralField; ++x) {
				const int donorX = x * donorWidth / numeralField;
				const uint8_t index = original.strip[{ donorX, sheetY }];
				if (!flameMask[static_cast<size_t>(y) * donorWidth + donorX] || index == Transparent || !IsWarm(original.palette[index]))
					continue;
				composite[{ numeralX + x, sheetY }] = index;
			}
		}
		for (int y = 0; y < glyphHeight; ++y) {
			for (int x = 0; x < glyphWidth; ++x) {
				const uint8_t index = glyph[static_cast<size_t>(y) * glyphWidth + x];
				if (index != Transparent)
					composite[{ glyphLeft + x, frame * original.height + glyphTop + y }] = index;
			}
		}
		if (frame != 0) {
			for (int y = 0; y < glyphTop; ++y) {
				for (int x = 0; x < numeralField; ++x)
					varyingFlamePixels += composite[{ numeralX + x, frame * original.height + y }] != composite[{ numeralX + x, y }];
			}
		}
		for (int y = 0; y < original.height; ++y)
			Require(std::memcmp(composite.at(finalDX, frame * original.height + y), original.strip.at(0, frame * original.height + y), donorWidth) == 0,
			    "Copied D differs from its original animation frame");
	}
	Require(varyingFlamePixels > 100, "The new numeral must retain animated native flames");
	OwnedSurface final(targetWidth, original.height * FrameCount);
	SDL_SetPaletteColors(final.surface->format->palette, original.palette.data(), 0, 256);
	SDL_FillRect(final.surface, nullptr, Transparent);
	const double scale = static_cast<double>(targetWidth) / compositeWidth;
	const int topPadding = baseline - static_cast<int>(std::lround(baseline * scale));
	if (menu) {
		for (int y = 0; y < final.h(); ++y)
			std::memcpy(final.at(0, y), composite.at(0, y), targetWidth);
	} else {
		const int scaledHeight = static_cast<int>(std::lround(original.height * scale));
		for (int frame = 0; frame < FrameCount; ++frame) {
			for (int y = 0; y < scaledHeight && topPadding + y < original.height; ++y) {
				const int sourceY = std::min(original.height - 1, static_cast<int>(y / scale));
				for (int x = 0; x < targetWidth; ++x) {
					const int sourceX = std::min(compositeWidth - 1, static_cast<int>(x / scale));
					final[{ x, frame * original.height + topPadding + y }] = composite[{ sourceX, frame * original.height + sourceY }];
				}
			}
		}
	}
	std::filesystem::create_directories(output / "ui_art");
	std::filesystem::create_directories(output / (original.name + "-3d-frames"));
	SavePcx(final, output / "ui_art" / (original.name + ".pcx"));
	VerifyOverride(original, final, output, menu);
	SaveTransparentPng(final, original.palette, output / (original.name + "-3d-strip.png"));
	OwnedSurface frameSurface(targetWidth, original.height);
	SDL_SetPaletteColors(frameSurface.surface->format->palette, original.palette.data(), 0, 256);
	for (int frame = 0; frame < FrameCount; ++frame) {
		for (int y = 0; y < original.height; ++y)
			std::memcpy(frameSurface.at(0, y), final.at(0, frame * original.height + y), targetWidth);
		SaveTransparentPng(frameSurface, original.palette, output / (original.name + "-3d-frames") / ("frame-" + std::to_string(frame) + ".png"));
		if (frame == 0)
			SaveTransparentPng(frameSurface, original.palette, output / (original.name + "-3d-first.png"));
	}
	metadata << (first ? "" : ",\n") << "{\"asset\":" << std::quoted("ui_art\\" + original.name + ".pcx")
		<< ",\"width\":" << targetWidth << ",\"frameHeight\":" << original.height << ",\"frames\":15,\"frameIntervalMs\":60"
		<< ",\"transparentIndex\":250,\"originalWordExact\":" << (menu ? "true" : "false")
		<< ",\"compositeWidth\":" << compositeWidth << ",\"scale\":" << scale << ",\"topPadding\":" << topPadding
		<< ",\"donorDInclusive\":[0,0," << donorWidth - 1 << ',' << original.height - 1 << "]"
		<< ",\"numeralGlyph\":[" << glyphLeft << ',' << glyphTop << ',' << glyphWidth << ',' << glyphHeight << "]"
		<< ",\"copiedDFrameExactBeforeScale\":true,\"numeralFlameDifferences\":" << varyingFlamePixels
		<< ",\"nativePcxRoundTripExact\":true}";
	std::cout << "Verified " << original.name << "3D: " << targetWidth << 'x' << original.height
	          << " / 15 frames, " << varyingFlamePixels << " new-numeral flame differences\n";
}
} // namespace

int main(int argc, char **argv)
{
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;
	const bool inspect = argc == 6 && std::string(argv[1]) == "--inspect";
	const int offset = inspect ? 2 : 1;
	if ((!inspect && argc != 5) || (inspect && argc != 6)) {
		std::cerr << "Usage: diablo_logo_build [--inspect] <game-data-dir> <built-assets-dir> <numeral-PNG> <output-dir>\n";
		return 2;
	}
	SDL_SetMainReady();
	if (SDL_Init(0) != 0) {
		std::cerr << SDL_GetError() << '\n';
		return 1;
	}
	int status = 0;
	try {
		HeadlessMode = true;
		paths::SetBasePath(argv[offset]);
		paths::SetAssetsPath(argv[offset + 1]);
		const std::filesystem::path output = std::filesystem::absolute(argv[offset + 3]);
		std::filesystem::create_directories(output);
		const auto archive = SelectArchive(std::filesystem::absolute(argv[offset]));
		std::ofstream metadata(output / "original-logo-metadata.json");
		Require(metadata.good(), "Open logo metadata output");
		metadata << "{\"archive\":" << std::quoted(archive.string()) << ",\"mode\":" << std::quoted(gbIsSpawn ? "shareware" : "retail") << ",\"logos\":[\n";
		auto menuLogo = ExportOriginal("smlogo", gbIsSpawn ? "swmmenu" : "mainmenu", output, metadata, true);
		auto titleLogo = ExportOriginal("logo", "title", output, metadata, false);
		metadata << "\n]}\n";
		Require(metadata.good(), "Write logo metadata");
		if (!inspect) {
			const auto numeral = LoadNumeral(std::filesystem::absolute(argv[offset + 2]));
			std::ofstream composition(output / "branding-metadata.json");
			Require(composition.good(), "Open branding metadata");
			composition << "{\"animationPeriodMs\":900,\"transparentIndex\":250,\"logos\":[\n";
			ComposeLogo(menuLogo, numeral, output, composition, true);
			ComposeLogo(titleLogo, numeral, output, composition, false);
			composition << "\n]}\n";
			Require(composition.good(), "Write branding metadata");
			std::cout << "Editable local overrides verified: " << (output / "ui_art").string() << '\n';
		}
		std::cout << "Original animation and palette diagnostics ready: " << output.string() << '\n';
	} catch (const std::exception &error) {
		std::cerr << "Logo builder stopped: " << error.what() << '\n';
		status = 1;
	}
	MpqArchives.clear();
	OverridePaths.clear();
	SDL_Quit();
	return status;
}
