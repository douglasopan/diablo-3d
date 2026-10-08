// Window-free checks for the RGBA menu compositor and the Godot layout contract.
// Usage: menu_presentation_smoke <menu-background.png>
#if defined(USE_SDL1) || defined(USE_SDL3)
#include <iostream>
int main()
{
	std::cout << "SKIP: the software-renderer fixture requires SDL2\n";
	return 77;
}
#else
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "DiabloUI/diabloui.h"
#include "DiabloUI/settings_layout.hpp"
#include "engine/palette.h"
#include "engine/render/d3d_menu_presentation.hpp"
#include "engine/render/d3d_ui_layout.hpp"
#include "headless_mode.hpp"
#include "utils/paths.h"
#include "utils/png.h"
#include "utils/sdl_ptrs.h"

namespace {
using namespace devilution;
size_t Checks = 0;
size_t Clicks = 0;
constexpr SDL_Color Clear { 13, 29, 47, 255 };
constexpr SDL_Color Gold { 231, 179, 53, 255 };
constexpr Rectangle Fallback { { 11, 17 }, { 101, 43 } };
constexpr std::string_view Header = "[Layout]\nformat=d3d.ui-layout\nschemaVersion=1\n";

void Check(bool condition, const std::string &message)
{
	++Checks;
	std::cout << (condition ? "PASS " : "FAIL ") << message << '\n';
	if (!condition)
		throw std::runtime_error(message);
}

void RequireSdl(bool condition, const char *operation)
{
	if (!condition)
		throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

bool SameRect(Rectangle a, Rectangle b)
{
	return a.position == b.position && a.size == b.size;
}

bool SameRgb(SDL_Color a, SDL_Color b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b;
}

struct Files {
	std::filesystem::path directory;
	std::filesystem::path assets;
	std::filesystem::path background;
	std::filesystem::path layout;
	std::filesystem::path palette;

	explicit Files(const std::filesystem::path &input)
	{
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		directory = std::filesystem::temp_directory_path() / ("d3d-menu-presentation-" + std::to_string(nonce));
		if (!std::filesystem::create_directory(directory))
			throw std::runtime_error("Cannot create a fresh menu fixture directory");
		assets = directory / "d3d-ui";
		std::filesystem::create_directory(assets);
		background = assets / "menu-background.png";
		layout = assets / "layout.ini";
		std::filesystem::copy_file(input, background);
		std::filesystem::create_directory(directory / "ui_art");
		palette = directory / "ui_art" / "diablo.pal";
		std::filesystem::copy_file(input.parent_path().parent_path() / "ui_art" / "diablo.pal", palette);
		paths::SetBasePath(directory.string());
		paths::SetAssetsPath(directory.string());
		paths::SetPrefPath(directory.string());
		paths::SetConfigPath(directory.string());
	}

	~Files()
	{
		// Delete only files and empty directories created by this fixture.
		std::error_code ignored;
		std::filesystem::remove(layout, ignored);
		std::filesystem::remove(background, ignored);
		std::filesystem::remove(palette, ignored);
		std::filesystem::remove(directory / "ui_art", ignored);
		std::filesystem::remove(assets, ignored);
		std::filesystem::remove(directory, ignored);
	}

	void WriteLayout(std::string_view text) const
	{
		std::ofstream out(layout, std::ios::binary | std::ios::trunc);
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!out)
			throw std::runtime_error("Cannot write the isolated layout fixture");
	}
};

std::string Element(std::string_view name, std::string_view anchor, int x, int y, int width, int height)
{
	return "[" + std::string(name) + "]\nanchor=" + std::string(anchor)
	    + "\noffsetX=" + std::to_string(x) + "\noffsetY=" + std::to_string(y)
	    + "\nwidth=" + std::to_string(width) + "\nheight=" + std::to_string(height) + "\n";
}

void CountClick()
{
	++Clicks;
}

void CheckLayouts(const Files &files)
{
	ReloadD3dUiLayout();
	Check(SameRect(GetD3dUiRect("MenuLogo", 640, 480, Fallback), Fallback), "missing layout preserves the caller's fallback");
	const std::string valid = std::string(Header) + Element("MenuLogo", "center", -100, -60, 200, 80)
	    + Element("Bottom", "bottom-center", -100, -80, 200, 80)
	    + Element("Corner", "bottom-right", -120, -70, 100, 60)
	    + Element("Top", "top-right", -120, 10, 100, 60)
	    + Element("Origin", "top-left", 20, 30, 100, 60)
	    + Element("Clamp", "bottom-right", 4096, -4096, 4096, 4096);
	files.WriteLayout(valid);
	ReloadD3dUiLayout();
	for (const Size size : { Size { 640, 480 }, Size { 1280, 720 }, Size { 2560, 1080 } }) {
		const int w = size.width, h = size.height;
		const std::string resolution = std::to_string(w) + "x" + std::to_string(h);
		Check(SameRect(GetD3dUiRect("MenuLogo", w, h, Fallback), { { w / 2 - 100, h / 2 - 60 }, { 200, 80 } }), "center anchor at " + resolution);
		Check(SameRect(GetD3dUiRect("Bottom", w, h, Fallback), { { w / 2 - 100, h - 80 }, { 200, 80 } }), "bottom-center anchor at " + resolution);
		Check(SameRect(GetD3dUiRect("Corner", w, h, Fallback), { { w - 120, h - 70 }, { 100, 60 } }), "bottom-right anchor at " + resolution);
		Check(SameRect(GetD3dUiRect("Top", w, h, Fallback), { { w - 120, 10 }, { 100, 60 } }), "top-right anchor at " + resolution);
		Check(SameRect(GetD3dUiRect("Origin", w, h, Fallback), { { 20, 30 }, { 100, 60 } }), "top-left anchor at " + resolution);
		Check(SameRect(GetD3dUiRect("Clamp", w, h, Fallback), { { 0, 0 }, { w, h } }), "oversized authored elements clamp to the viewport at " + resolution);
	}
	Check(SameRect(GetD3dUiRect("Unknown", 640, 480, Fallback), Fallback), "unknown element uses its own fallback");
	Check(SameRect(GetD3dUiRect("MenuLogo", 0, 480, Fallback), Fallback), "invalid screen dimensions preserve the fallback");

	const std::string goodElement = Element("MenuLogo", "top-left", 70, 90, 200, 60);
	const std::string badElement = Element("Broken", "top-left", 0, 0, 10, 10);
	std::vector<std::pair<std::string, std::string>> invalid {
		{ "unknown header key", "[Layout]\nformat=d3d.ui-layout\nschemaVersion=1\nunknown=1\n" + goodElement },
		{ "duplicate format", "[Layout]\nformat=d3d.ui-layout\nformat=d3d.ui-layout\nschemaVersion=1\n" + goodElement },
		{ "duplicate schema version", "[Layout]\nformat=d3d.ui-layout\nschemaVersion=1\nschemaVersion=1\n" + goodElement },
		{ "unsupported schema", "[Layout]\nformat=d3d.ui-layout\nschemaVersion=2\n" + goodElement },
		{ "duplicate section", std::string(Header) + goodElement + goodElement },
		{ "duplicate element key", std::string(Header) + goodElement + "width=200\n" },
		{ "unknown element key", std::string(Header) + goodElement + "rowHeight=43\n" },
		{ "unknown anchor", std::string(Header) + goodElement + Element("Broken", "free", 0, 0, 10, 10) },
		{ "zero width", std::string(Header) + goodElement + Element("Broken", "center", 0, 0, 0, 10) },
		{ "negative height", std::string(Header) + goodElement + Element("Broken", "center", 0, 0, 10, -1) },
		{ "excessive size", std::string(Header) + goodElement + Element("Broken", "center", 0, 0, 4097, 10) },
		{ "excessive offset", std::string(Header) + goodElement + Element("Broken", "center", -4097, 0, 10, 10) },
		{ "numeric suffix", std::string(Header) + goodElement + badElement + "offsetX=12px\n" },
		{ "integer overflow", std::string(Header) + goodElement + badElement + "offsetX=9999999999999999999999\n" },
		{ "missing numeric value", std::string(Header) + goodElement + "[Broken]\nanchor=center\nwidth=10\nheight=10\noffsetX=0\n" },
		{ "empty numeric value", std::string(Header) + goodElement + badElement + "width=\n" },
		{ "empty section", std::string(Header) + goodElement + "[]\n" },
		{ "key without section", "orphan=1\n" + std::string(Header) + goodElement },
		{ "unclosed section", std::string(Header) + goodElement + "[Broken\n" },
		{ "empty file", "" },
		{ "oversized file", std::string(32769, 'x') },
	};
	std::string embeddedNul = std::string(Header) + goodElement;
	embeddedNul.push_back('\0');
	invalid.emplace_back("embedded NUL", std::move(embeddedNul));
	for (const auto &[reason, text] : invalid) {
		files.WriteLayout(valid);
		ReloadD3dUiLayout();
		files.WriteLayout(text);
		ReloadD3dUiLayout();
		Check(SameRect(GetD3dUiRect("MenuLogo", 640, 480, Fallback), Fallback)
		        && SameRect(GetD3dUiRect("Broken", 640, 480, Fallback), Fallback),
		    reason + " rejects the entire layout and clears the previous revision");
	}

	// Use the same authored rectangle for pixels and the real UI input dispatcher.
	files.WriteLayout(std::string(Header) + goodElement);
	ReloadD3dUiLayout();
	const Rectangle rect = GetD3dUiRect("MenuLogo", 640, 480, Fallback);
	SDLSurfaceUniquePtr canvas { SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 8, SDL_PIXELFORMAT_INDEX8) };
	RequireSdl(canvas != nullptr, "create input fixture surface");
	SDL_Rect drawn { rect.position.x, rect.position.y, rect.size.width, rect.size.height };
	RequireSdl(SDL_FillRect(canvas.get(), &drawn, 1) == 0, "paint authored input rectangle");
	UiArtTextButton item("Test", CountClick, drawn);
	const std::vector<UiItemBase *> items { &item };
	SDL_Event event {};
	event.type = SDL_MOUSEBUTTONUP;
	event.button.button = SDL_BUTTON_LEFT;
	event.button.x = rect.Center().x;
	event.button.y = rect.Center().y;
	const auto *row = static_cast<const uint8_t *>(canvas->pixels) + event.button.y * canvas->pitch;
	Check(row[event.button.x] == 1 && UiItemMouseEvents(&event, items) && Clicks == 1,
	    "moved authored pixels and real UI click use the same rectangle");
	event.button.x = Fallback.Center().x;
	event.button.y = Fallback.Center().y;
	Check(!UiItemMouseEvents(&event, items) && Clicks == 1, "old fallback position is no longer clickable");
	event.button.x = rect.position.x + rect.size.width;
	event.button.y = rect.Center().y;
	Check(!UiItemMouseEvents(&event, items), "right edge stays outside the authored hitbox");
}

void CheckSettingsLayouts()
{
	for (const Size size : { Size { 640, 480 }, Size { 853, 480 }, Size { 1280, 720 }, Size { 1920, 1080 }, Size { 2560, 1080 } }) {
		for (const size_t count : { 3, 8, 13, 60 }) {
			const auto bounds = GetSettingsUiRectangle(size.width, size.height, count);
			const std::string label = std::to_string(size.width) + "x" + std::to_string(size.height) + "/" + std::to_string(count);
			Check(std::abs(bounds.Center().x - size.width / 2) <= 1 && std::abs(bounds.Center().y - size.height / 2) <= 1,
			    "Settings occupied block is centered: " + label);
			Check(bounds.position.y >= 0 && bounds.position.y + bounds.size.height <= size.height,
			    "Settings block stays inside the screen: " + label);
			const int rows = (bounds.size.height - 284) / 26;
			Check(rows >= 1 && rows <= static_cast<int>(count) && (count <= 8 || bounds.size.height >= 466),
			    "Settings keeps selectable rows and scrolls long lists: " + label);
			const Rectangle description { { bounds.position.x + 25, bounds.position.y + 204 + rows * 26 + 16 }, { bounds.size.width - 50, 64 } };
			Check(description.Center().x == bounds.Center().x && description.position.y + description.size.height <= size.height,
			    "Settings description shares the title/list center and fits: " + label);
		}
	}
}

struct RendererDeleter {
	void operator()(SDL_Renderer *value) const { SDL_DestroyRenderer(value); }
};

struct Frame {
	SDLSurfaceUniquePtr output;
	SDLSurfaceUniquePtr ui;
	std::unique_ptr<SDL_Renderer, RendererDeleter> target;
	std::vector<uint8_t> readback;
	int width, height;

	Frame(int w, int h)
	    : output(SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32))
	    , ui(SDL_CreateRGBSurfaceWithFormat(0, w, h, 8, SDL_PIXELFORMAT_INDEX8))
	    , width(w)
	    , height(h)
	{
		RequireSdl(output && ui, "create software-renderer surfaces");
		target.reset(SDL_CreateSoftwareRenderer(output.get()));
		RequireSdl(target != nullptr, "create window-free software renderer");
		renderer = target.get();
		PalSurface = ui.get();
		gnScreenWidth = static_cast<uint16_t>(w);
		gnScreenHeight = static_cast<uint16_t>(h);
		readback.resize(static_cast<size_t>(w) * h * 4);
		std::array<SDL_Color, 256> palette {};
		palette[0] = { 255, 0, 255, 255 }; // Index zero is transparent regardless of RGB.
		palette[1] = Gold;
		palette[2] = { 0, 0, 0, 255 }; // Nonzero black must remain opaque.
		system_palette = palette;
		RequireSdl(SDL_SetPaletteColors(ui->format->palette, palette.data(), 0, 256) == 0, "set indexed UI palette");
		RequireSdl(SDL_FillRect(ui.get(), nullptr, 0) == 0, "clear indexed UI");
		SDL_Rect gold { w / 2 - 4, h / 2 - 4, 8, 8 };
		SDL_Rect black { w / 2 + 20, h / 2 - 4, 8, 8 };
		RequireSdl(SDL_FillRect(ui.get(), &gold, 1) == 0 && SDL_FillRect(ui.get(), &black, 2) == 0, "paint gold and opaque black UI");
	}

	~Frame()
	{
		SetD3dMainMenuActive(false);
		ResetD3dMainMenuResources(); // Textures must die before their renderer.
		PalSurface = nullptr;
		renderer = nullptr;
	}

	void ClearOutput()
	{
		RequireSdl(SDL_SetRenderDrawColor(target.get(), Clear.r, Clear.g, Clear.b, Clear.a) == 0
		        && SDL_RenderClear(target.get()) == 0,
		    "clear software output");
	}

	void Read()
	{
		RequireSdl(SDL_RenderReadPixels(target.get(), nullptr, SDL_PIXELFORMAT_RGBA32, readback.data(), width * 4) == 0, "read rendered RGBA pixels");
	}

	SDL_Color Pixel(int x, int y) const
	{
		const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
		return { readback[offset], readback[offset + 1], readback[offset + 2], readback[offset + 3] };
	}
};

SDL_Color PatternColor(int x, int y)
{
	const int column = x < 40 ? 0 : x >= 360 ? 2 : 1;
	const int row = y < 22 ? 0 : y >= 202 ? 2 : 1;
	return { static_cast<uint8_t>(60 + column * 65), static_cast<uint8_t>(45 + row * 75), static_cast<uint8_t>(30 + column * 15 + row * 10), 255 };
}

void WritePattern(const Files &files)
{
	SDLSurfaceUniquePtr pattern { SDL_CreateRGBSurfaceWithFormat(0, 400, 224, 32, SDL_PIXELFORMAT_RGBA32) };
	RequireSdl(pattern != nullptr, "create generated crop pattern");
	for (int y = 0; y < pattern->h; ++y) {
		auto *row = static_cast<uint32_t *>(pattern->pixels) + static_cast<size_t>(y) * pattern->pitch / 4;
		for (int x = 0; x < pattern->w; ++x) {
			const auto color = PatternColor(x, y);
			row[x] = SDL_MapRGBA(pattern->format, color.r, color.g, color.b, color.a);
		}
	}
	RequireSdl(IMG_SavePNG(pattern.get(), files.background.string().c_str()) == 0, "write generated crop pattern");
}

void CheckRendering(const Files &files)
{
	{
		Frame frame(640, 480);
		UiLoadMenuBackground("unused-original-menu");
		Check(IsD3dMainMenuActive() && !ArtBackground && !ArtBackgroundWidescreen,
		    "submenu loader shares the custom RGB background without loading its inherited art");
		UiLoadBlackBackground();
		Check(!IsD3dMainMenuActive(), "native black-background screens disable the custom layer");
		UiLoadMenuBackground();
		Check(IsD3dMainMenuActive(), "Settings and nested menus reactivate the shared background");
		UnloadUiGFX();
		Check(!IsD3dMainMenuActive(), "leaving UI for gameplay disables the custom background");
	}
	{
		Frame frame(640, 480);
		Check(DiabloUiSurface() == frame.ui.get(), "menu UI and PalSurface refer to the same indexed surface");
		Check(SetD3dMainMenuActive(true), "provided PNG loads through the real asset and texture path");
		SetD3dMainMenuFade(256);
		frame.ClearOutput();
		Check(RenderD3dMainMenu(frame.target.get(), frame.ui.get()), "provided PNG composes without a window");
		frame.Read();
		bool foundColor = false;
		for (int y = 20; y < 480; y += 40)
			for (int x = 20; x < 640; x += 40) {
				const auto pixel = frame.Pixel(x, y);
				foundColor |= (pixel.r != 0 || pixel.g != 0 || pixel.b != 0) && !SameRgb(pixel, Clear);
			}
		Check(foundColor, "provided background contributes colored pixels beyond the indexed UI");
	}
	WritePattern(files);
	for (const Size size : { Size { 640, 480 }, Size { 1280, 720 }, Size { 2560, 1080 } }) {
		Frame frame(size.width, size.height);
		const std::string resolution = std::to_string(size.width) + "x" + std::to_string(size.height);
		Check(SetD3dMainMenuActive(true), "activate fresh renderer at " + resolution);
		SetD3dMainMenuFade(256);
		frame.ClearOutput();
		Check(RenderD3dMainMenu(frame.target.get(), frame.ui.get()), "compose at " + resolution);
		frame.Read();
		Check(SameRgb(frame.Pixel(size.width / 2, size.height / 2), Gold), "palette gold stays exact at " + resolution);
		Check(SameRgb(frame.Pixel(size.width / 2 + 23, size.height / 2), { 0, 0, 0, 255 }), "nonzero palette black stays opaque at " + resolution);
		const double scale = std::max(size.width / 400.0, size.height / 224.0);
		const double left = (400.0 - size.width / scale) / 2;
		const double top = (224.0 - size.height / scale) / 2;
		for (const Point point : { Point { 10, 10 }, Point { size.width - 11, 10 }, Point { 10, size.height - 11 }, Point { size.width - 11, size.height - 11 } }) {
			const auto expected = PatternColor(static_cast<int>(left + (point.x + 0.5) / scale), static_cast<int>(top + (point.y + 0.5) / scale));
			Check(SameRgb(frame.Pixel(point.x, point.y), expected), "centered cover crop preserves source regions at " + resolution + " / " + std::to_string(point.x) + "," + std::to_string(point.y));
		}
		SetD3dMainMenuFade(-5);
		frame.ClearOutput();
		Check(RenderD3dMainMenu(frame.target.get(), frame.ui.get()), "fade-zero composition succeeds at " + resolution);
		frame.Read();
		Check(SameRgb(frame.Pixel(10, 10), Clear) && SameRgb(frame.Pixel(size.width / 2, size.height / 2), Gold), "fade clamps to zero and index zero remains transparent at " + resolution);
		SetD3dMainMenuFade(999);
		frame.ClearOutput();
		Check(RenderD3dMainMenu(frame.target.get(), frame.ui.get()), "fade clamps to full opacity at " + resolution);
		frame.Read();
		Check(!SameRgb(frame.Pixel(10, 10), Clear), "full fade reveals the colored crop at " + resolution);
		Check(!SetD3dMainMenuActive(false), "deactivation returns inactive at " + resolution);
		frame.ClearOutput();
		Check(!RenderD3dMainMenu(frame.target.get(), frame.ui.get()), "inactive compositor leaves fallback rendering available at " + resolution);
		frame.Read();
		Check(SameRgb(frame.Pixel(size.width / 2, size.height / 2), Clear), "inactive compositor does not draw stale UI at " + resolution);
		Check(SetD3dMainMenuActive(true), "reactivation reuses the owning renderer at " + resolution);
		ResetD3dMainMenuResources();
		Check(SetD3dMainMenuActive(true), "reset resources can be recreated at " + resolution);
		SetD3dMainMenuFade(256);
		frame.ClearOutput();
		Check(RenderD3dMainMenu(frame.target.get(), frame.ui.get()), "recreated textures render without stale ownership at " + resolution);
		Check(!RenderD3dMainMenu(nullptr, frame.ui.get()) && !RenderD3dMainMenu(frame.target.get(), nullptr), "null compositor arguments fail safely at " + resolution);
		Check(!RenderD3dMainMenu(frame.target.get(), frame.output.get()), "RGBA input is rejected instead of being treated as indexed UI at " + resolution);
	}
	{
		Frame frame(640, 480);
		std::filesystem::remove(files.background);
		Check(!SetD3dMainMenuActive(true), "missing background rejects activation and enables the inherited fallback");
		Check(!RenderD3dMainMenu(frame.target.get(), frame.ui.get()), "missing background does not reuse a previous renderer's texture");
	}
}
} // namespace

int main(int argc, char **argv)
{
	if (argc != 2 || !std::filesystem::is_regular_file(argv[1])) {
		std::cerr << "Usage: menu_presentation_smoke <menu-background.png>\n";
		return 1;
	}
	SDL_SetMainReady();
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	if (SDL_Init(0) != 0) {
		std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}
	try {
		HeadlessMode = true;
		RequireSdl((InitPNG() & IMG_INIT_PNG) != 0, "initialize PNG support");
		const Files files(argv[1]);
		CheckLayouts(files);
		CheckSettingsLayouts();
		CheckRendering(files);
		std::cout << "PASS " << Checks << " checks; no window, game archives, player profile or saves accessed\n";
	} catch (const std::exception &error) {
		SetD3dMainMenuActive(false);
		ResetD3dMainMenuResources();
		std::cerr << "FAIL after " << Checks << " checks: " << error.what() << '\n';
		QuitPNG();
		SDL_Quit();
		return 1;
	}
	QuitPNG();
	SDL_Quit();
	return 0;
}
#endif
