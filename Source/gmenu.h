/**
 * @file gmenu.h
 *
 * Interface of the in-game navigation and interaction.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "utils/attributes.h"

#ifdef USE_SDL3
#include <SDL3/SDL_keycode.h>
#else
#include <SDL.h>
#endif

#include "engine/rectangle.hpp"
#include "engine/surface.hpp"

namespace devilution {

#define GMENU_SLIDER 0x40000000
#define GMENU_ENABLED 0x80000000

struct TMenuItem {
	uint32_t dwFlags;
	const char *pszStr;
	void (*fnMenu)(bool);
	/** @brief Optional, already translated value used by the compact settings menu. Owned by the caller. */
	const char *value = nullptr;

	[[nodiscard]] bool enabled() const
	{
		return (dwFlags & GMENU_ENABLED) != 0;
	}

	[[nodiscard]] bool isSlider() const
	{
		return (dwFlags & GMENU_SLIDER) != 0;
	}

	[[nodiscard]] uint16_t sliderStep() const
	{
		return dwFlags & 0xFFF;
	}

	void setSliderStep(uint16_t step)
	{
		dwFlags &= 0xFFFFF000;
		dwFlags |= step;
	}

	[[nodiscard]] uint16_t sliderSteps() const
	{
		return (dwFlags & 0xFFF000) >> 12;
	}

	void setSliderSteps(uint16_t steps)
	{
		dwFlags |= (steps << 12) & 0xFFF000;
	}

	void addFlags(uint32_t flags)
	{
		dwFlags |= flags;
	}

	void removeFlags(uint32_t flags)
	{
		dwFlags &= ~flags;
	}

	void setEnabled(bool enabled)
	{
		if (enabled) {
			addFlags(GMENU_ENABLED);
		} else {
			removeFlags(GMENU_ENABLED);
		}
	}
};

extern DVL_API_FOR_TEST TMenuItem *sgpCurrentMenu;

inline constexpr size_t GMenuSettingsMaxContentRows = 18;

/** @brief Optional presentation for settings pages. All text is already translated and owned by the caller. */
struct GMenuSettingsPresentation {
	const char *title = nullptr;
	std::string_view (*describe)(size_t row) = nullptr;
	void (*back)() = nullptr;
	void (*page)(bool next) = nullptr;
	/** @brief Final entries share one horizontal footer. */
	size_t navigationItems = 0;
	/** @brief Preserve the pause logo, native item translations and large selection spinners. */
	bool pauseMenu = false;
	/** @brief Input capture owns controller navigation until its handler finishes or cancels. */
	bool lockNavigation = false;
};

/** @brief Shared compact settings geometry for rendering, input and finite diagnostics. */
struct GMenuSettingsGeometry {
	Rectangle panel;
	Rectangle title;
	Rectangle items;
	Rectangle navigation;
	Rectangle description;
	int rowHeight;
	size_t rows;
	size_t contentRows;
	size_t navigationItems;
	int fontSize;
	int titleFontSize;
	int descriptionFontSize;

	[[nodiscard]] Rectangle row(size_t index) const
	{
		if (index >= rows)
			return { { 0, 0 }, { 0, 0 } };
		if (index >= contentRows) {
			const int column = static_cast<int>(index - contentRows);
			const int count = static_cast<int>(navigationItems);
			const int left = navigation.position.x + navigation.size.width * column / count;
			const int right = navigation.position.x + navigation.size.width * (column + 1) / count;
			return { { left, navigation.position.y }, { right - left, navigation.size.height } };
		}
		return { { items.position.x, items.position.y + static_cast<int>(index) * rowHeight }, { items.size.width, rowHeight } };
	}
};

/** @brief Native font tiers and a shared footer, bounded by screen/HUD and eighteen content rows. */
[[nodiscard]] GMenuSettingsGeometry gmenu_settings_geometry(Size screenSize, int mainPanelTop, size_t rowCount, bool containsSlider = false, size_t navigationItems = 0);
/** @brief Content capacity with room for both page actions and Back. */
[[nodiscard]] size_t gmenu_settings_page_size(Size screenSize, int mainPanelTop);
/** @brief Geometry currently used by rendering/input, including the pause menu presentation. */
[[nodiscard]] GMenuSettingsGeometry gmenu_get_settings_geometry();

void gmenu_draw_pause(const Surface &out);
void FreeGMenu();
void gmenu_init_menu();
bool gmenu_is_active();
void gmenu_set_items(TMenuItem *pItem, void (*gmFunc)());
/** @brief Enable the compact settings presentation after setting the menu items. gmenu_set_items clears it. */
void gmenu_set_settings_presentation(GMenuSettingsPresentation presentation);
/** @brief Returns zero if there is no selected item. */
[[nodiscard]] size_t gmenu_selected_index();
/** @brief Select an enabled visible item, clamping the index and skipping disabled rows. */
void gmenu_select_index(size_t index);
void gmenu_draw(const Surface &out);
bool gmenu_presskeys(SDL_Keycode vkey);
bool gmenu_on_mouse_move();
bool gmenu_left_mouse(bool isDown);

/**
 * @brief Set the TMenuItem slider position based on the given value
 */
void gmenu_slider_set(TMenuItem *pItem, int min, int max, int value);

/**
 * @brief Get the current value for the slider
 */
int gmenu_slider_get(TMenuItem *pItem, int min, int max);

/**
 * @brief Set the number of steps for the slider
 */
void gmenu_slider_steps(TMenuItem *pItem, int steps);

} // namespace devilution
