#pragma once

#include <string_view>

#include "engine/rectangle.hpp"

namespace devilution {

// Authored in Godot, consumed by rendering and input in the same logical space.
void ReloadD3dUiLayout();
Rectangle GetD3dUiRect(std::string_view name, int screenWidth, int screenHeight, Rectangle fallback);

} // namespace devilution
