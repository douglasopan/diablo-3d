#pragma once

#include <cstdint>
#include <string_view>

#include "engine/rectangle.hpp"

namespace devilution {

// Authored in Godot, consumed by rendering and input in the same logical space.
void ReloadD3dUiLayout();
Rectangle GetD3dUiRect(std::string_view name, int screenWidth, int screenHeight, Rectangle fallback);
uint64_t GetD3dUiLayoutGeneration();
bool HasD3dUiLayoutOverride(std::string_view name);
// Only the existing 13 HUD IDs count; menu/foreign sections keep auto layout.
bool HasD3dHudUiLayoutOverride();

} // namespace devilution
