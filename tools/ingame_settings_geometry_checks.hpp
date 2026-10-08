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
	const std::array<Size, 6> screens { Size { 640, 480 }, Size { 853, 480 }, Size { 1920, 1080 },
		Size { 2560, 1080 }, Size { 2560, 1440 }, Size { 3440, 1440 } };
	for (const Size screen : screens) {
		// CalculatePanelAreas keeps GetMainPanel's native 128px rectangle.
		// The custom HUD has separate scaled rectangles; these checks use the
		// same GetMainPanel boundary as gmenu, not individual HUD asset bounds.
		const int hudTop = screen.height - 128;
		const Rectangle available { { 0, 0 }, { screen.width, hudTop } };
		const std::string resolution = std::to_string(screen.width) + "x" + std::to_string(screen.height);
		for (const bool sliders : { false, true }) {
			const size_t maximumRows = sliders ? 2 : 8;
			for (size_t count = 0; count <= maximumRows; ++count) {
				const auto layout = gmenu_settings_geometry(screen, hudTop, count, sliders);
				const std::string name = resolution + (sliders ? " slider " : " list ") + std::to_string(count);
				check(layout.rows == count, name + ": all requested rows remain visible");
				check(layout.rowHeight == (sliders ? 46 : 32), name + ": row height respects presentation");
				check(inside(available, layout.panel), name + ": panel stays inside screen above HUD");
				check(inside(layout.panel, layout.title), name + ": title stays inside panel");
				check(inside(layout.panel, layout.items), name + ": items stay inside panel");
				check(inside(layout.panel, layout.description), name + ": description stays inside panel");
				check(layout.description.size.height >= 28, name + ": at least two Latin description lines are reserved");
				check(layout.items.size.height == static_cast<int>(count) * layout.rowHeight, name + ": item extent matches rows");
				check(!overlaps(layout.title, layout.items) && !overlaps(layout.items, layout.description)
				        && !overlaps(layout.title, layout.description), name + ": title, items and description do not overlap");
				for (size_t row = 0; row < count; ++row) {
					const Rectangle rect = layout.row(row);
					check(inside(layout.items, rect), name + ": row " + std::to_string(row) + " stays inside items");
					check(inside(available, rect), name + ": row " + std::to_string(row) + " stays above HUD");
					check(rect.contains(rect.position) && !rect.contains(rect.position.x + rect.size.width, rect.position.y)
					        && !rect.contains(rect.position.x, rect.position.y + rect.size.height), name + ": row uses half-open click bounds");
					for (size_t other = row + 1; other < count; ++other)
						check(!overlaps(rect, layout.row(other)), name + ": rows " + std::to_string(row) + "/" + std::to_string(other) + " are exclusive");
				}
				check(empty(layout.row(count)), name + ": first out-of-range row has no clickable rectangle");
				check(empty(layout.row(std::numeric_limits<size_t>::max())), name + ": oversized row index has no rectangle");
			}
		}
	}

	const auto minimum = gmenu_settings_geometry({ 640, 480 }, 352, 8);
	check(minimum.rows == 8 && minimum.row(7).position.y + minimum.row(7).size.height <= 352,
	    "640x480: all five content rows and three navigation rows remain above HUD");
	check(minimum.description.size.height >= 32, "640x480: two 16px tall-font description lines fit");
	check(gmenu_settings_geometry({ 640, 480 }, 352, 9).rows == 8, "layout caps normal pages at eight rows");
	check(gmenu_settings_geometry({ 640, 480 }, 352, std::numeric_limits<size_t>::max()).rows == 8,
	    "oversized row counts are bounded before integer conversion");
	const auto sliders = gmenu_settings_geometry({ 640, 480 }, 352, 2, true);
	check(sliders.rows == 2 && sliders.items.size.width >= 490, "640x480: slider and return rows fit the native 490px item");

	// The public helper accepts explicit HUD boundaries so callers need not
	// assume that a future panel, diagnostic crop or tiny screen uses 128px.
	for (const int hudTop : { 128, 240, 352, 480, 600 }) {
		const auto layout = gmenu_settings_geometry({ 640, 480 }, hudTop, 8);
		const Rectangle available { { 0, 0 }, { 640, std::min(hudTop, 480) } };
		check(inside(available, layout.panel), "explicit HUD boundary: panel stays in available space");
		check(layout.rows <= 8, "explicit HUD boundary: row capacity remains bounded");
		for (size_t row = 0; row < layout.rows; ++row)
			check(inside(available, layout.row(row)), "explicit HUD boundary: visible rows stay above boundary");
	}
	for (const Size screen : { Size { 0, 0 }, Size { 0, 480 }, Size { 640, 0 }, Size { 16, 480 }, Size { 39, 480 }, Size { -1, -1 } }) {
		const auto layout = gmenu_settings_geometry(screen, std::max(0, screen.height - 128), 8);
		check(layout.rows == 0 && empty(layout.panel) && empty(layout.title) && empty(layout.items) && empty(layout.description),
		    "zero, negative or too-narrow screens return empty layout");
		check(empty(layout.row(0)), "empty layouts have no clickable row");
	}
	for (const Size screen : { Size { 56, 480 }, Size { 128, 480 }, Size { 320, 480 } }) {
		const Rectangle available { { 0, 0 }, { screen.width, 352 } };
		const auto layout = gmenu_settings_geometry(screen, 352, 8);
		check(layout.rows == 8 && inside(available, layout.panel) && inside(layout.panel, layout.title)
		        && inside(layout.panel, layout.items) && inside(layout.panel, layout.description),
		    "narrow normal lists keep all rectangles within screen");
		const auto nativeSlider = gmenu_settings_geometry(screen, 352, 2, true);
		check(nativeSlider.rows == 0 && empty(nativeSlider.panel), "narrow screens do not draw a native slider outside its panel");
	}
	for (const int hudTop : { -1, 0, 8, 64 }) {
		const auto layout = gmenu_settings_geometry({ 640, 480 }, hudTop, 8);
		check(layout.rows == 0 && empty(layout.panel), "absent or too-short space above HUD returns empty layout");
	}
}

} // namespace devilution
