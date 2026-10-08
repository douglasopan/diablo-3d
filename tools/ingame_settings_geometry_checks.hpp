#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <string>

#include "gmenu.h"

namespace devilution {

/**
 * Geometry checks for the actual menu layout helper. The caller supplies
 * check(bool, message); these checks do not initialize SDL, render fonts,
 * dispatch input, load assets or access options/saves. Passing these checks
 * establishes rectangle coverage only, not visual approval or navigation.
 */
template <typename Check>
void CheckInGameSettingsGeometry(Check check)
{
	const auto inside = [](const Rectangle &outer, const Rectangle &inner) {
		return inner.size.width >= 0 && inner.size.height >= 0
		    && inner.position.x >= outer.position.x && inner.position.y >= outer.position.y
		    && inner.position.x + inner.size.width <= outer.position.x + outer.size.width
		    && inner.position.y + inner.size.height <= outer.position.y + outer.size.height;
	};
	const auto overlaps = [](const Rectangle &left, const Rectangle &right) {
		return left.size.width > 0 && left.size.height > 0 && right.size.width > 0 && right.size.height > 0
		    && left.position.x < right.position.x + right.size.width && right.position.x < left.position.x + left.size.width
		    && left.position.y < right.position.y + right.size.height && right.position.y < left.position.y + left.size.height;
	};
	const auto empty = [](const Rectangle &rect) {
		return rect.size.width == 0 && rect.size.height == 0;
	};
	const std::array<Size, 8> screens { Size { 640, 480 }, Size { 853, 480 }, Size { 960, 540 }, Size { 1280, 720 },
		Size { 1920, 1080 }, Size { 2560, 1080 }, Size { 2560, 1440 }, Size { 3440, 1440 } };
	for (const Size screen : screens) {
		// CalculatePanelAreas keeps GetMainPanel's native 128px rectangle.
		// The custom HUD has separate scaled rectangles; these checks use the
		// same GetMainPanel boundary as gmenu, not individual HUD asset bounds.
		const int hudTop = screen.height - 128;
		const Rectangle available { { 0, 0 }, { screen.width, hudTop } };
		const std::string resolution = std::to_string(screen.width) + "x" + std::to_string(screen.height);
		const size_t capacity = gmenu_settings_page_size(screen, hudTop);
		check(capacity > 5 && capacity <= GMenuSettingsMaxContentRows,
		    resolution + ": responsive pages expose more than five bounded content rows");
		for (const bool sliders : { false, true }) {
			const size_t maximumRows = sliders ? 2 : GMenuSettingsMaxContentRows + 3;
			for (const size_t requestedNavigation : { size_t { 0 }, size_t { 1 }, size_t { 3 } }) {
				if (sliders && requestedNavigation > 1)
					continue;
				for (size_t count = 0; count <= maximumRows; ++count) {
					const size_t navigation = std::min(count, requestedNavigation);
					const auto layout = gmenu_settings_geometry(screen, hudTop, count, sliders, navigation);
					const std::string name = resolution + (sliders ? " slider " : " list ") + std::to_string(count);
					check(layout.rows == layout.contentRows + layout.navigationItems && layout.rows <= count,
					    name + ": visible rows partition into content and footer");
					check(layout.navigationItems == navigation, name + ": all navigation actions remain visible");
					check(layout.contentRows <= GMenuSettingsMaxContentRows && layout.contentRows <= count - navigation,
					    name + ": content count remains bounded before integer conversion");
					if (!sliders && count - navigation <= capacity)
						check(layout.rows == count, name + ": every row within responsive capacity remains visible");
					const int fontSize = screen.height >= 1440 ? 42 : screen.height >= 900 ? 30 : screen.width >= 960 ? 24 : 12;
					const int rowHeight = fontSize == 42 ? 48 : fontSize == 30 ? 42 : fontSize == 24 ? 32 : 24;
					check(layout.fontSize == fontSize && layout.rowHeight == (sliders ? std::max(46, rowHeight) : rowHeight),
					    name + ": native font and row heights follow screen size");
					check(inside(available, layout.panel), name + ": panel stays inside screen above HUD");
					check(inside(layout.panel, layout.title), name + ": title stays inside panel");
					check(inside(layout.panel, layout.items), name + ": items stay inside panel");
					check(inside(layout.panel, layout.description), name + ": description stays inside panel");
					check(layout.description.size.height >= (screen.height >= 900 ? 52 : 32),
					    name + ": at least two description lines are reserved at the native font size");
					check(layout.items.size.height == static_cast<int>(layout.contentRows) * layout.rowHeight,
					    name + ": content extent matches its vertical rows");
					check(!overlaps(layout.title, layout.items) && !overlaps(layout.items, layout.description)
					        && !overlaps(layout.title, layout.description), name + ": title, items and description do not overlap");
					if (navigation == 0) {
						check(empty(layout.navigation), name + ": absent footer has no clickable rectangle");
					} else {
						check(inside(layout.panel, layout.navigation) && inside(available, layout.navigation), name + ": footer fits above HUD");
						check(!overlaps(layout.title, layout.navigation) && !overlaps(layout.items, layout.navigation)
						        && !overlaps(layout.description, layout.navigation), name + ": footer does not overlap other regions");
						check(layout.navigation.size.height == layout.rowHeight, name + ": navigation shares one horizontal row");
					}
					for (size_t row = 0; row < layout.rows; ++row) {
						const Rectangle rect = layout.row(row);
						check(inside(row < layout.contentRows ? layout.items : layout.navigation, rect),
						    name + ": row " + std::to_string(row) + " stays inside its content or footer region");
						check(inside(available, rect), name + ": row " + std::to_string(row) + " stays above HUD");
						check(rect.contains(rect.position) && !rect.contains(rect.position.x + rect.size.width, rect.position.y)
						        && !rect.contains(rect.position.x, rect.position.y + rect.size.height), name + ": row uses half-open click bounds");
						for (size_t other = row + 1; other < layout.rows; ++other)
							check(!overlaps(rect, layout.row(other)), name + ": rows " + std::to_string(row) + "/" + std::to_string(other) + " are exclusive");
					}
					check(empty(layout.row(layout.rows)), name + ": first out-of-range row has no clickable rectangle");
					check(empty(layout.row(std::numeric_limits<size_t>::max())), name + ": oversized row index has no rectangle");
				}
			}
		}
	}

	const size_t minimumCapacity = gmenu_settings_page_size({ 640, 480 }, 352);
	const size_t fullHdCapacity = gmenu_settings_page_size({ 1920, 1080 }, 952);
	check(fullHdCapacity > minimumCapacity, "1080p exposes more content rows than 480p");
	check(fullHdCapacity == GMenuSettingsMaxContentRows, "1080p uses the full bounded eighteen-row capacity");
	const auto maximum = gmenu_settings_geometry({ 1920, 1080 }, 952, std::numeric_limits<size_t>::max(), false, 3);
	check(maximum.contentRows == GMenuSettingsMaxContentRows && maximum.rows == GMenuSettingsMaxContentRows + 3,
	    "oversized row counts are bounded before integer conversion and retain their footer");
	const auto oversizedFooter = gmenu_settings_geometry({ 640, 480 }, 352, 4, false, std::numeric_limits<size_t>::max());
	check(oversizedFooter.navigationItems == 3 && oversizedFooter.contentRows == 1,
	    "oversized navigation counts clamp to three actions without unsigned subtraction");
	const auto sliders = gmenu_settings_geometry({ 640, 480 }, 352, 2, true, 1);
	check(sliders.rows == 2 && sliders.contentRows == 1 && sliders.navigationItems == 1 && sliders.items.size.width >= 490,
	    "640x480: the native slider and horizontal return action fit");

	// The public helper accepts explicit HUD boundaries so callers need not
	// assume that a future panel, diagnostic crop or tiny screen uses 128px.
	for (const int hudTop : { 128, 240, 352, 480, 600 }) {
		const auto layout = gmenu_settings_geometry({ 640, 480 }, hudTop, GMenuSettingsMaxContentRows + 3, false, 3);
		const Rectangle available { { 0, 0 }, { 640, std::min(hudTop, 480) } };
		check(inside(available, layout.panel), "explicit HUD boundary: panel stays in available space");
		check(layout.contentRows <= GMenuSettingsMaxContentRows, "explicit HUD boundary: content capacity remains bounded");
		for (size_t row = 0; row < layout.rows; ++row)
			check(inside(available, layout.row(row)), "explicit HUD boundary: content and footer actions stay above boundary");
	}
	for (const Size screen : { Size { 0, 0 }, Size { 0, 480 }, Size { 640, 0 }, Size { 16, 480 }, Size { 39, 480 }, Size { -1, -1 } }) {
		const auto layout = gmenu_settings_geometry(screen, std::max(0, screen.height - 128), 8, false, 3);
		check(layout.rows == 0 && empty(layout.panel) && empty(layout.title) && empty(layout.items)
		        && empty(layout.navigation) && empty(layout.description),
		    "zero, negative or too-narrow screens return empty layout");
		check(empty(layout.row(0)), "empty layouts have no clickable row");
	}
	for (const Size screen : { Size { 56, 480 }, Size { 128, 480 }, Size { 320, 480 } }) {
		const Rectangle available { { 0, 0 }, { screen.width, 352 } };
		const auto layout = gmenu_settings_geometry(screen, 352, 8, false, 3);
		check(layout.rows == 8 && inside(available, layout.panel) && inside(layout.panel, layout.title)
		        && inside(layout.panel, layout.items) && inside(layout.panel, layout.navigation) && inside(layout.panel, layout.description),
		    "narrow normal lists keep content and shared footer inside screen");
		const auto nativeSlider = gmenu_settings_geometry(screen, 352, 2, true, 1);
		check(nativeSlider.rows == 0 && empty(nativeSlider.panel), "narrow screens do not draw a native slider outside its panel");
	}
	for (const int hudTop : { -1, 0, 8, 64 }) {
		const auto layout = gmenu_settings_geometry({ 640, 480 }, hudTop, 8, false, 3);
		check(layout.rows == 0 && empty(layout.panel), "absent or too-short space above HUD returns empty layout");
	}
}

} // namespace devilution
