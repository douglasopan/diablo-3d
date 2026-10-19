#pragma once

#include <algorithm>
#include <cstddef>

#include "engine/rectangle.hpp"

namespace devilution {

// Center the occupied block, while allowing long lists to use the full height.
inline Rectangle GetSettingsUiRectangle(int screenWidth, int screenHeight, size_t itemCount)
{
	constexpr int RowHeight = 26;
	constexpr int NonListHeight = 284; // 204 above the list, 80 for its description.
	const int width = std::clamp(screenWidth, 640, 720);
	const int availableRows = std::max(1, (screenHeight - NonListHeight) / RowHeight);
	const int rows = static_cast<int>(std::min(itemCount, static_cast<size_t>(availableRows)));
	const int height = std::min(screenHeight, NonListHeight + rows * RowHeight);
	return { { (screenWidth - width) / 2, (screenHeight - height) / 2 }, { width, height } };
}

} // namespace devilution
