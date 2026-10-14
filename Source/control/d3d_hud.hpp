#pragma once

#include <cstdint>
#include <string_view>

#include "control/d3d_hud_geometry.hpp"
#include "engine/rectangle.hpp"
#include "engine/surface.hpp"
#include "tables/spelldat.h"

namespace devilution {

// Skin policy is independent of the current layout and renderer availability.
bool IsD3dHudSelected();
// Shared layout policy, independent of the RGB presentation admission gate.
bool IsD3dHudLayoutActive();
bool IsD3dHudEnabled();
// Shared hit areas retain selected geometry on controller CPU presentation;
// the native chat modal retains its input/recipient layout.
bool IsPointOnD3dHud(Point point);
Rectangle GetD3dHudPanelButtonRect(int button);
Rectangle GetD3dHudBeltRect();
Rectangle GetD3dHudBeltSlotRect(int slot);
// Shared content area; Compact uses its independent design scale.
Rectangle GetD3dHudBeltInteriorRect(Rectangle slot);
Rectangle GetD3dHudSpellRect();
Rectangle GetD3dHudInfoRect();
Rectangle GetD3dHudFrameRect();
Rectangle GetD3dHudOrbRect(bool mana);
Rectangle GetD3dHudValueRect(bool mana);
Rectangle GetD3dHudLevelButtonRect();
Rectangle GetD3dHudLevelLabelRect();
Rectangle GetD3dHudDurabilityRect();
void DrawD3dHudPlate(const Surface &out, Rectangle rect, bool pressed = false);
void DrawD3dHudBackground(const Surface &out);
void DrawD3dHudPanelButton(const Surface &out, int button, bool pressed);
void DrawD3dHudBeltBackground(const Surface &out);
void DrawD3dHudBeltItem(const Surface &out, const Surface &nativeItem, Rectangle slot);
void DrawD3dHudSpellIcon(const Surface &out, Rectangle rect, SpellID spell, SpellType type);
void DrawD3dHudFlask(const Surface &out, bool mana);
void DrawD3dHudValues(const Surface &out);
void DrawD3dHudKeyLabel(const Surface &out, Rectangle rect, std::string_view text);
void DrawD3dHudSurface(const Surface &out, const Surface &source, Rectangle rect, bool transparent = false);
void DrawD3dHudContent(const Surface &out, const Surface &source, Rectangle rect, uint8_t transparentIndex = 1);

} // namespace devilution
