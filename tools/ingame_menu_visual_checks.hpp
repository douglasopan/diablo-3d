#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "control/control.hpp"
#include "diablo.h"
#include "engine/events.hpp"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/text_render.hpp"
#include "engine/sound.h"
#include "engine/surface.hpp"
#include "gamemenu.h"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "ingame_settings.h"
#include "options.h"
#include "utils/language.h"
#include "utils/paths.h"
#include "utils/ui_fwd.h"

namespace devilution {

// The initializer is defined by diablo.cpp, as in ingame_settings_smoke.cpp.
void InitPadmapActions();

/** Native, archive-backed menu captures. The entry must bind PrefPath and
 * ConfigPath to output before calling this, initialize SDL with the dummy video
 * driver, and supply the same callbacks used by town_view_smoke. No save/load,
 * option-value or binding callback is activated. Music rows reflect the actual
 * media visible through the archive/override paths; absent choices stay absent.
 *
 * Suggested entry (owned by the integrator):
 * RunInGameMenuVisualChecks(output, InitializeTownDiagnostic, Check,
 *     NativeSceneState, SavePng, std::cout);
 */
template <typename Initialize, typename Check, typename NativeState, typename SavePng>
void RunInGameMenuVisualChecks(const std::filesystem::path &output, Initialize initialize,
    Check check, NativeState nativeState, SavePng savePng, std::ostream &log)
{
	const auto canonicalOutput = std::filesystem::weakly_canonical(output);
	check(std::filesystem::weakly_canonical(paths::PrefPath()) == canonicalOutput
	        && std::filesystem::weakly_canonical(paths::ConfigPath()) == canonicalOutput,
	    "native menu capture uses only its private preference/configuration directory");
	initialize();
	check(!gmenu_is_active() && !IsInGameSettingsOpen() && !gbSndInited,
	    "native menu capture starts in an isolated diagnostic session");
	const std::string originalScene = nativeState();
	const uint32_t originalRng = GetLCGEngineState();
	InitKeymapActions();
	InitPadmapActions();
	LoadOptions();
	LanguageInitialize();

	struct Restore {
		bool headless = HeadlessMode;
		bool sound = gbSoundOn;
		bool music = gbMusicOn;
		bool audioStarted = false;
		uint16_t width = gnScreenWidth;
		uint16_t height = gnScreenHeight;
		uint16_t viewport = gnViewportHeight;
		Point mouse = MousePosition;
		EventHandler eventHandler = CurrentEventHandler;
		int soundVolume = *GetOptions().Audio.soundVolume;
		int musicVolume = *GetOptions().Audio.musicVolume;
		~Restore()
		{
			gamemenu_off();
			FreeGMenu();
			UnloadFonts();
			if (audioStarted)
				snd_deinit();
			GetOptions().Audio.soundVolume.SetValue(soundVolume);
			GetOptions().Audio.musicVolume.SetValue(musicVolume);
			gbSoundOn = sound;
			gbMusicOn = music;
			HeadlessMode = headless;
			MousePosition = mouse;
			SetEventHandler(eventHandler);
			gnScreenWidth = width;
			gnScreenHeight = height;
			CalculatePanelAreas();
			CalcViewportGeometry();
			gnViewportHeight = viewport;
		}
	} restore;

	// Initialize the real audio backend without playing audio. This makes the
	// native Music category available without inventing an initialized flag.
#ifndef NOSOUND
#ifdef USE_SDL3
	SDL_SetHint("SDL_AUDIODRIVER", "dummy");
#else
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
#endif
	snd_init();
	restore.audioStarted = gbSndInited;
	check(gbSndInited, "native menu capture initializes the real dummy audio backend");
#endif
	gbSoundOn = gbMusicOn = false;
	HeadlessMode = false;
	gmenu_init_menu(); // Loads the real logo, pentagram and slider CELs.
	std::filesystem::create_directories(output);
	std::ofstream report(output / "ingame-menu-visual.txt");
	check(report.good(), "open native menu geometry/capture receipt");
	report << "Native gmenu drawing; original town backdrop; HUD is not drawn.\n"
	          "Fonts/logo/sliders are loaded from the real local assets/archives.\n"
	          "Only navigation/focus callbacks run; no option values or saves are activated.\n"
	       << "language=" << GetLanguageCode() << " audioAvailable=" << gbSndInited << '\n';

	const auto countRows = [&]() {
		check(sgpCurrentMenu != nullptr, "native menu has a live item array");
		for (size_t row = 0; row <= GMenuSettingsMaxContentRows + 3; ++row) {
			if (sgpCurrentMenu[row].fnMenu == nullptr)
				return row;
			check(sgpCurrentMenu[row].pszStr != nullptr, "native menu owns every displayed label");
		}
		throw std::runtime_error("native menu has no terminator within its public row bound");
	};
	const auto findHere = [&](std::string_view label) -> std::optional<size_t> {
		const size_t rows = countRows();
		for (size_t row = 0; row < rows; ++row)
			if (std::string_view(sgpCurrentMenu[row].pszStr) == label)
				return row;
		return std::nullopt;
	};
	const auto activate = [&](size_t row, bool mouse) {
		check(row < countRows() && sgpCurrentMenu[row].enabled(), "navigation targets an enabled native row");
		if (mouse) {
			const Rectangle box = gmenu_get_settings_geometry().row(row);
			MousePosition = { box.position.x + box.size.width / 2, box.position.y + box.size.height / 2 };
			check(gmenu_left_mouse(true), "native mouse click dispatches through the displayed row hitbox");
			gmenu_left_mouse(false);
		} else {
			gmenu_select_index(row);
			check(gmenu_selected_index() == row && gmenu_presskeys(SDLK_RETURN), "native Enter activates the focused navigation row");
		}
	};
	const auto inside = [](Rectangle outer, Rectangle inner) {
		return inner.size.width > 0 && inner.size.height > 0
		    && inner.position.x >= outer.position.x && inner.position.y >= outer.position.y
		    && inner.position.x + inner.size.width <= outer.position.x + outer.size.width
		    && inner.position.y + inner.size.height <= outer.position.y + outer.size.height;
	};
	const auto overlap = [](Rectangle a, Rectangle b) {
		return a.position.x < b.position.x + b.size.width && b.position.x < a.position.x + a.size.width
		    && a.position.y < b.position.y + b.size.height && b.position.y < a.position.y + a.size.height;
	};

	for (const Size dimensions : { Size { 960, 540 }, Size { 1920, 1080 } }) {
		gnScreenWidth = dimensions.width;
		gnScreenHeight = dimensions.height;
		CalculatePanelAreas();
		CalcViewportGeometry();
		const std::string prefix = std::to_string(dimensions.width) + "x" + std::to_string(dimensions.height);
		OwnedSurface allocation(dimensions.width, dimensions.height + 2);
		SDL_SetPaletteColors(allocation.surface->format->palette, logical_palette.data(), 0, 256);
		const Surface out = allocation.subregionY(0, dimensions.height);
		std::memset(allocation.at(0, dimensions.height), 0xA7, allocation.pitch() * 2);
		const auto capture = [&](const std::string &name) {
			const size_t rows = countRows();
			const auto layout = gmenu_get_settings_geometry();
			check(layout.rows == rows && rows > 0, "draw/input geometry covers the real native item array");
			check(inside({ { 0, 0 }, { dimensions.width, GetMainPanel().position.y } }, layout.panel), "native menu panel fits above the HUD");
			check(std::abs(layout.panel.position.x * 2 + layout.panel.size.width - dimensions.width) <= 1,
			    "native menu panel is horizontally centered");
			check(std::abs(layout.panel.position.y * 2 + layout.panel.size.height - GetMainPanel().position.y) <= 1,
			    "native menu panel is vertically centered above the HUD");
			check(IsInGameSettingsOpen() ? layout.fontSize == (dimensions.height >= 900 ? 30 : 24) : layout.fontSize >= 30,
			    "native menu uses its expected readable font tier");
			report << prefix << ' ' << name << " rows=" << rows << " content=" << layout.contentRows
			       << " navigation=" << layout.navigationItems << " font=" << layout.fontSize << '\n';
			for (size_t row = 0; row < rows; ++row) {
				const Rectangle box = layout.row(row);
				check(inside(layout.panel, box), "every native row hitbox fits inside the menu panel");
				for (size_t previous = 0; previous < row; ++previous)
					check(!overlap(box, layout.row(previous)), "native content/footer hitboxes do not overlap");
				report << "  row=" << row << " enabled=" << sgpCurrentMenu[row].enabled() << " box="
				       << box.position.x << ',' << box.position.y << ',' << box.size.width << ',' << box.size.height
				       << " label=" << sgpCurrentMenu[row].pszStr << '\n';
				if (!sgpCurrentMenu[row].enabled())
					continue;
				MousePosition = { box.position.x + box.size.width / 2, box.position.y + box.size.height / 2 };
				check(gmenu_on_mouse_move() && gmenu_selected_index() == row, "native hover focus matches every displayed row hitbox");
			}
			if (layout.navigationItems > 1) {
				gmenu_select_index(layout.contentRows);
				check(gmenu_presskeys(SDLK_RIGHT) && gmenu_selected_index() == layout.contentRows + 1,
				    "native Right selects the next horizontal footer action");
				check(gmenu_presskeys(SDLK_LEFT) && gmenu_selected_index() == layout.contentRows,
				    "native Left restores the previous horizontal footer action");
			}
			for (const bool last : { false, true }) {
				gmenu_select_index(last ? rows - 1 : 0);
				for (int y = 0; y < out.h(); ++y)
					std::memset(out.at(0, y), 0, out.w());
				check(DrawNativeTownViewReference(out, ViewPosition), "draw the original archive-backed town backdrop");
				std::vector<uint8_t> backdrop(static_cast<size_t>(out.w()) * out.h());
				for (int y = 0; y < out.h(); ++y)
					std::memcpy(backdrop.data() + static_cast<size_t>(y) * out.w(), out.at(0, y), out.w());
				gmenu_draw(out);
				size_t changed = 0;
				size_t outsidePanel = 0;
				for (int y = 0; y < out.h(); ++y)
					for (int x = 0; x < out.w(); ++x)
						if (out[{ x, y }] != backdrop[static_cast<size_t>(y) * out.w() + x]) {
							++changed;
							outsidePanel += !layout.panel.contains(Point { x, y });
						}
				bool guards = true;
				for (int y = dimensions.height; y < dimensions.height + 2; ++y)
					for (int x = 0; x < allocation.pitch(); ++x)
						guards &= allocation.at(0, y)[x] == 0xA7;
				const std::string filename = prefix + '-' + name + (last ? "-focus-last.png" : "-focus-first.png");
				savePng(out, output / filename);
				report << "  capture=" << filename << " changedPixels=" << changed << " outsidePanel=" << outsidePanel
				       << " guardRows=" << guards << " selected=" << gmenu_selected_index() << std::endl;
				check(changed > 0, "native gmenu produces visible pixels with HeadlessMode disabled");
				check(outsidePanel == 0, "native title, labels, focus and description stay within the menu panel");
				check(guards, "native menu capture preserves the output guard rows");
			}
			check(nativeState() == originalScene && GetLCGEngineState() == originalRng, "native menu drawing and focus preserve the scene and simulation RNG");
		};
		const auto capturePages = [&](const std::string &name) {
			const std::string initialLabel(sgpCurrentMenu[0].pszStr);
			for (size_t page = 0; page < 64; ++page) {
				capture(name + "-page-" + std::to_string(page + 1));
				const auto next = findHere(_("Next Page"));
				if (!next)
					return;
				// The final page returns to the first. The title and label array are
				// owned by production; compare label content rather than pointers.
				const std::string firstLabel(sgpCurrentMenu[0].pszStr);
				activate(*next, page % 2 == 0);
				if (firstLabel == sgpCurrentMenu[0].pszStr)
					throw std::runtime_error("native next-page callback did not advance");
				// Page labels are unique within a category; reopening below resets
				// each category. Stop after the wrap to the initial first label.
				if (sgpCurrentMenu[0].pszStr == initialLabel)
					return;
			}
			throw std::runtime_error("native menu page navigation exceeded its finite limit");
		};
		gamemenu_on();
		capture("escape");
		activate(0, true);
		check(IsInGameSettingsOpen(), "native Esc Settings click opens the real category browser");
		capturePages("categories");
		for (auto *category : { static_cast<OptionCategoryBase *>(&GetOptions().Graphics), static_cast<OptionCategoryBase *>(&GetOptions().Music) }) {
			if (category == &GetOptions().Music && !gbSndInited) {
				report << prefix << " music unavailable in NOSOUND build\n";
				continue;
			}
			OpenInGameSettings();
			const std::string label(category->GetName());
			std::optional<size_t> found;
			for (size_t page = 0; page < 64 && !found; ++page) {
				found = findHere(label);
				if (!found) {
					const auto next = findHere(_("Next Page"));
					if (!next)
						break;
					activate(*next, false);
				}
			}
			check(found.has_value(), "real settings category is reachable: " + label);
			activate(*found, true);
			capturePages(category == &GetOptions().Graphics ? "graphics" : "music");
			check(gmenu_presskeys(SDLK_ESCAPE), "native Escape returns from entries to categories");
		}
		gamemenu_off();
	}
	check(nativeState() == originalScene && GetLCGEngineState() == originalRng,
	    "all native menu captures preserve the original map, actors and simulation RNG");
	check(CurrentEventHandler == restore.eventHandler && !gmenu_is_active() && !IsInGameSettingsOpen(),
	    "native menu capture releases its input wrapper and active menu");
	log << "Native Esc/settings screenshots and geometry receipt: " << output.string() << '\n';
}

} // namespace devilution
