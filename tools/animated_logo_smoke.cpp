// Headless validation of the custom logo asset contract, timing and palette draws.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/d3d_logo.hpp"
#include "headless_mode.hpp"
#include "utils/endian_write.hpp"
#include "utils/paths.h"
#include "utils/pcx.hpp"

namespace {
using namespace devilution;
constexpr uint8_t Background = 253;
constexpr std::array<D3dLogoKind, 3> Kinds { D3dLogoKind::Menu, D3dLogoKind::Title, D3dLogoKind::Pause };

void Require(bool condition, const std::string &message)
{
	if (!condition)
		throw std::runtime_error(message);
}

uint8_t FixturePixel(uint32_t frame, int x, int y)
{
	if (x == 2 && y == 1)
		return 0; // Painted black is opaque; only index 250 is transparent.
	if (x >= 3 && x < 43 && y >= 4 && y < 30)
		return static_cast<uint8_t>(frame + 1);
	return D3dLogoTransparentIndex;
}

void AppendPcxRun(std::vector<uint8_t> &out, uint8_t color, unsigned length)
{
	while (length != 0) {
		const unsigned run = std::min(length, 63U);
		if (run != 1 || color >= 0xC0)
			out.push_back(static_cast<uint8_t>(0xC0 | run));
		out.push_back(color);
		length -= run;
	}
}

std::vector<uint8_t> Fixture(D3dLogoKind kind)
{
	const auto spec = GetD3dLogoSpec(kind);
	std::vector<uint8_t> data(PcxHeaderSize, 0);
	data[0] = 0x0A;
	data[1] = 5;
	data[2] = 1;
	data[3] = 8;
	WriteLE16(&data[8], spec.width - 1);
	WriteLE16(&data[10], static_cast<uint16_t>(spec.frameHeight * D3dLogoFrameCount - 1));
	data[65] = 1;
	WriteLE16(&data[66], spec.width);
	WriteLE16(&data[68], 1);
	for (uint32_t frame = 0; frame < D3dLogoFrameCount; ++frame) {
		for (int y = 0; y < spec.frameHeight; ++y) {
			int x = 0;
			while (x < spec.width) {
				const int begin = x;
				const uint8_t color = FixturePixel(frame, x++, y);
				while (x < spec.width && FixturePixel(frame, x, y) == color)
					++x;
				AppendPcxRun(data, color, static_cast<unsigned>(x - begin));
			}
		}
	}
	data.push_back(0x0C);
	for (unsigned color = 0; color < 256; ++color) {
		for (unsigned channel = 0; channel < 3; ++channel)
			data.push_back(static_cast<uint8_t>(color));
	}
	return data;
}

void VerifyFrames(const D3dLogo &logo, D3dLogoKind kind, bool fixture)
{
	const auto spec = GetD3dLogoSpec(kind);
	const auto list = logo.sprites();
	Require(list.numSprites() == D3dLogoFrameCount, "Exactly 240 frames");
	OwnedSurface decoded(spec.width, spec.frameHeight);
	size_t opaque = 0;
	size_t transparent = 0;
	size_t opaqueBlack = 0;
	size_t animatedChanges = 0;
	std::vector<uint8_t> previous(static_cast<size_t>(spec.width) * spec.frameHeight);
	for (uint32_t frame = 0; frame < D3dLogoFrameCount; ++frame) {
		const ClxSprite sprite = list[frame];
		Require(sprite.width() == spec.width && sprite.height() == spec.frameHeight, "Frame dimensions");
		std::fill_n(decoded.begin(), static_cast<size_t>(decoded.surface->pitch) * decoded.h(), D3dLogoTransparentIndex);
		ClxDraw(decoded, { 0, spec.frameHeight - 1 }, sprite);
		for (int y = 0; y < spec.frameHeight; ++y) {
			for (int x = 0; x < spec.width; ++x) {
				const uint8_t pixel = decoded[{ x, y }];
				if (fixture && pixel != FixturePixel(frame, x, y))
					Require(false, "PCX/CLX roundtrip at frame " + std::to_string(frame));
				const size_t index = static_cast<size_t>(y) * spec.width + x;
				if (frame != 0 && previous[index] != pixel)
					++animatedChanges;
				previous[index] = pixel;
				transparent += pixel == D3dLogoTransparentIndex;
				opaque += pixel != D3dLogoTransparentIndex;
				const auto color = logo.palette()[pixel];
				opaqueBlack += pixel != D3dLogoTransparentIndex && color.r == 0 && color.g == 0 && color.b == 0;
			}
		}
	}
	Require(opaque != 0 && transparent != 0 && animatedChanges != 0, "Logo has painted pixels, transparency and animation");
	if (fixture)
		Require(opaqueBlack == D3dLogoFrameCount, "Opaque black preserved in all 240 frames");
	std::cout << spec.path << ": 240 frames " << spec.width << 'x' << spec.frameHeight
	          << ", painted=" << opaque << ", transparent=" << transparent << ", black=" << opaqueBlack << '\n';
}

void VerifyTiming()
{
	Require(D3dLogoFrameAt(0) == 0 && D3dLogoFrameAt(7999) == 239 && D3dLogoFrameAt(8000) == 0, "Cycle endpoints 0/7999/8000");
	Require(D3dLogoFrameAt(33) == 0 && D3dLogoFrameAt(34) == 1, "Rational 30fps boundaries");
	for (uint32_t frame = 0; frame < D3dLogoFrameCount; ++frame) {
		const uint32_t begin = (frame * D3dLogoCycleDurationMs + D3dLogoFrameCount - 1) / D3dLogoFrameCount;
		const uint32_t end = ((frame + 1) * D3dLogoCycleDurationMs + D3dLogoFrameCount - 1) / D3dLogoFrameCount - 1;
		Require(D3dLogoFrameAt(begin) == frame && D3dLogoFrameAt(end) == frame, "Every frame has its correct display interval");
		Require(D3dLogoFrameAt(begin + 8000) == frame, "Second loop matches first");
	}
	constexpr uint32_t StartBeforeWrap = 0xFFFFFFF0U;
	const uint32_t afterWrap = StartBeforeWrap + 34U;
	Require(D3dLogoFrameAt(afterWrap - StartBeforeWrap) == 1, "Unsigned elapsed time handles tick wrap");
}

void VerifyPalette(const D3dLogo &logo)
{
	logical_palette = logo.palette();
	OwnedSurface out(640, 480);
	const auto drawAndCheck = [&](uint8_t expectedBlack, uint8_t expectedColor) {
		std::fill_n(out.begin(), static_cast<size_t>(out.surface->pitch) * out.h(), Background);
		DrawD3dLogo(out, { 0, 153 }, logo, 0);
		Require(out[{ 0, 0 }] == Background, "Transparent border does not overwrite background");
		Require(out[{ 2, 1 }] == expectedBlack, "Painted black remains an opaque pixel");
		Require(out[{ 3, 4 }] == expectedColor, "Source RGB mapped into target palette");
	};
	drawAndCheck(0, 1);
	std::swap(logical_palette[0], logical_palette[7]);
	std::swap(logical_palette[1], logical_palette[9]);
	drawAndCheck(7, 9);
	// The same instance must restore its original mapping after returning to a menu.
	logical_palette = logo.palette();
	drawAndCheck(0, 1);
	logical_palette[D3dLogoTransparentIndex] = logical_palette[1];
	logical_palette[1] = logical_palette[2];
	Require(logo.translation(logical_palette)[1] != D3dLogoTransparentIndex, "Reserved transparent index is never selected for painted pixels");
}

void VerifyMalformed(const std::vector<uint8_t> &valid)
{
	const auto rejected = [](const std::vector<uint8_t> &data) {
		Require(!DecodeD3dLogoPcx(data, D3dLogoKind::Menu), "Malformed optional logo safely rejected");
	};
	std::vector<uint8_t> bad;
	rejected(bad);
	bad = valid;
	bad[3] = 24;
	rejected(bad);
	bad = valid;
	bad[65] = 3;
	rejected(bad);
	bad = valid;
	WriteLE16(&bad[10], 153); // One frame rather than 240.
	rejected(bad);
	bad = valid;
	WriteLE16(&bad[66], 582);
	rejected(bad);
	bad = valid;
	bad[PcxHeaderSize] = 0xC0; // Zero-length PCX run.
	rejected(bad);
	bad = valid;
	bad.insert(bad.begin() + PcxHeaderSize + 2, { 0xFF, 250 }); // Row overflow.
	rejected(bad);
	bad = valid;
	bad.erase(bad.begin() + PcxHeaderSize, bad.begin() + PcxHeaderSize + 2);
	rejected(bad);
	bad = valid;
	bad.resize(bad.size() - 1);
	rejected(bad);
	bad = valid;
	bad[bad.size() - 769] = 0;
	rejected(bad);
}

// Only known fixture files are removed, never an existing directory tree.
class FixtureDirectory {
public:
	FixtureDirectory()
	{
		for (unsigned attempt = 0; attempt < 100; ++attempt) {
			path = std::filesystem::temp_directory_path() / ("d3d-logo-smoke-" + std::to_string(SDL_GetTicks()) + "-" + std::to_string(attempt));
			if (std::filesystem::create_directory(path)) {
				std::filesystem::create_directory(path / "ui_art");
				return;
			}
		}
		throw std::runtime_error("Unable to create a private fixture directory");
	}
	~FixtureDirectory()
	{
		std::error_code error;
		for (const auto name : { "d3d-menu.pcx", "d3d-title.pcx", "d3d-pause.pcx" })
			std::filesystem::remove(path / "ui_art" / name, error);
		std::filesystem::remove(path / "ui_art", error);
		std::filesystem::remove(path, error);
	}
	std::filesystem::path path;
};

void WriteFile(const std::filesystem::path &path, std::span<const uint8_t> data)
{
	std::ofstream file(path, std::ios::binary);
	file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
	Require(file.good(), "Write private test fixture");
}

void VerifyLoader(const std::vector<uint8_t> &data)
{
	FixtureDirectory directory;
	OverridePaths = { directory.path.string() + "/" };
	paths::SetAssetsPath(directory.path.string() + "/");
	Require(!LoadD3dLogo(D3dLogoKind::Menu), "Missing custom asset returns native fallback signal");
	const auto file = directory.path / "ui_art" / "d3d-menu.pcx";
	WriteFile(file, data);
	{
		auto logo = LoadD3dLogo(D3dLogoKind::Menu);
		Require(logo.has_value(), "Load through ordinary asset override path");
		VerifyFrames(*logo, D3dLogoKind::Menu, true);
	}
	// Lifetime/reload plus malformed fallback after the valid logo was released.
	Require(LoadD3dLogo(D3dLogoKind::Menu).has_value(), "Logo reload after unload");
	WriteFile(file, std::span<const uint8_t> { data.data(), 128 });
	Require(!LoadD3dLogo(D3dLogoKind::Menu), "Invalid override returns native fallback signal");
	OverridePaths.clear();
}

void VerifyLayout()
{
	const auto menu = GetD3dLogoSpec(D3dLogoKind::Menu);
	const auto title = GetD3dLogoSpec(D3dLogoKind::Title);
	const auto pause = GetD3dLogoSpec(D3dLogoKind::Pause);
	Require(menu.width <= 640 && menu.frameHeight < 192, "Main logo fits above first menu row");
	Require(title.width <= 640 && 182 + title.frameHeight <= 410, "Title logo fits above copyright row");
	Require(pause.width <= 640 && 102 - pause.frameHeight + 1 >= 0 && 102 < 110, "ESC logo fits above first menu row");
}

} // namespace

int main(int argc, char **argv)
{
	if (argc > 2) {
		std::cerr << "Usage: animated_logo_smoke [custom-assets-root]\n";
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
		VerifyTiming();
		VerifyLayout();
		auto menu = Fixture(D3dLogoKind::Menu);
		VerifyMalformed(menu);
		for (const auto kind : Kinds) {
			const auto data = kind == D3dLogoKind::Menu ? menu : Fixture(kind);
			auto logo = DecodeD3dLogoPcx(data, kind);
			Require(logo.has_value(), "Valid tall PCX strip decoded");
			VerifyFrames(*logo, kind, true);
			if (kind == D3dLogoKind::Menu)
				VerifyPalette(*logo);
		}
		VerifyLoader(menu);
		if (argc == 2) {
			const auto root = std::filesystem::absolute(argv[1]);
			OverridePaths = { root.string() + "/" };
			paths::SetAssetsPath(root.string() + "/");
			for (const auto kind : Kinds) {
				auto logo = LoadD3dLogo(kind);
				Require(logo.has_value(), "Load real custom logo " + std::string(GetD3dLogoSpec(kind).path));
				VerifyFrames(*logo, kind, false);
			}
		}
		std::cout << "PASS: exact 8s/240-frame timing, all frames, palette invalidation, opaque black, layout, optional fallback and reload.\n";
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		status = 1;
	}
	OverridePaths.clear();
	SDL_Quit();
	return status;
}
