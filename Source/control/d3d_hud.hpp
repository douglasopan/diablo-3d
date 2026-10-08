#pragma once

#include <string_view>

#include "engine/rectangle.hpp"
#include "engine/surface.hpp"
#include "tables/spelldat.h"

namespace devilution {

bool IsD3dHudEnabled();
// Includes the legacy panel only when the legacy presentation is active.
bool IsPointOnD3dHud(Point point);
Rectangle GetD3dHudPanelButtonRect(int button);
Rectangle GetD3dHudBeltRect();
Rectangle GetD3dHudBeltSlotRect(int slot);
Rectangle GetD3dHudSpellRect();
Rectangle GetD3dHudInfoRect();
Rectangle GetD3dHudOrbRect(bool mana);
Rectangle GetD3dHudValueRect(bool mana);
Rectangle GetD3dHudLevelButtonRect();
Rectangle GetD3dHudLevelLabelRect();
Rectangle GetD3dHudDurabilityRect();
void DrawD3dHudPlate(const Surface &out, Rectangle rect, bool pressed = false);
void DrawD3dHudPanelButton(const Surface &out, int button, bool pressed);
void DrawD3dHudBeltBackground(const Surface &out);
void DrawD3dHudBeltItem(const Surface &out, const Surface &nativeItem, Rectangle slot);
void DrawD3dHudSpellIcon(const Surface &out, Rectangle rect, SpellID spell, SpellType type);
void DrawD3dHudFlask(const Surface &out, bool mana);
void DrawD3dHudValues(const Surface &out);
void DrawD3dHudKeyLabel(const Surface &out, Rectangle rect, std::string_view text);
void DrawD3dHudSurface(const Surface &out, const Surface &source, Rectangle rect, bool transparent = false);

} // namespace devilution
