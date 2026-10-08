#pragma once

#include <algorithm>
#include <array>
#include <string_view>

#include "engine/rectangle.hpp"

namespace devilution {

struct D3dHudLayoutEntry {
	std::string_view name;
	bool rightAnchor;
	int x, y, width, height;
	bool utility = false;
};

// Logical pixels, identical to the editable Control nodes in ui/hud.tscn.
inline constexpr std::array<D3dHudLayoutEntry, 13> D3dHudLayoutEntries {{
	{ "HudHealthOrb", false, -226, -122, 88, 113 },
	{ "HudManaOrb", false, 138, -122, 88, 113 },
	{ "HudBelt", false, -116, -103, 232, 29 },
	{ "HudSpell", false, 252, -58, 44, 44 },
	{ "HudInfo", false, -132, -72, 264, 64 },
	{ "HudCharacter", false, -310, -106, 71, 20, true },
	{ "HudInventory", false, 239, -106, 71, 20, true },
	{ "HudSpellbook", false, 239, -82, 71, 20, true },
	{ "HudQuests", false, -310, -82, 71, 20, true },
	{ "HudMap", false, -310, -54, 71, 20, true },
	{ "HudMenu", false, -310, -30, 71, 20, true },
	{ "HudChat", false, -199, -150, 33, 24, true },
	{ "HudFriendly", false, 166, -150, 33, 24, true },
}};

inline int D3dHudSafeWidth(int width, int height)
{
	return std::min(width, height * 16 / 9);
}

inline Rectangle D3dHudDefaultRect(const D3dHudLayoutEntry &entry, int safeWidth, int height)
{
	return { { (entry.rightAnchor ? safeWidth : safeWidth / 2) + entry.x, height + entry.y }, { entry.width, entry.height } };
}

inline Rectangle D3dHudPlaceRect(Rectangle rect, const D3dHudLayoutEntry &, int width, int height)
{
	const int safeWidth = D3dHudSafeWidth(width, height);
	rect.size.width = std::clamp(rect.size.width, 1, std::max(1, safeWidth));
	rect.size.height = std::clamp(rect.size.height, 1, std::max(1, height));
	rect.position.x = std::clamp(rect.position.x, 0, safeWidth - rect.size.width) + (width - safeWidth) / 2;
	rect.position.y = std::clamp(rect.position.y, 0, height - rect.size.height);
	return rect;
}

inline Rectangle D3dHudScaleRect(Rectangle rect, int height)
{
	// Scale edges together so adjacent cells share their boundary exactly.
	const int left = rect.position.x * height / 480;
	const int top = rect.position.y * height / 480;
	const int right = (rect.position.x + rect.size.width) * height / 480;
	const int bottom = (rect.position.y + rect.size.height) * height / 480;
	return { { left, top }, { std::max(1, right - left), std::max(1, bottom - top) } };
}

inline Rectangle D3dHudBeltSlotRect(Rectangle belt, int slot)
{
	const int left = belt.size.width * slot / 8;
	const int right = belt.size.width * (slot + 1) / 8;
	return { belt.position + Displacement { left, 0 }, { right - left, belt.size.height } };
}

} // namespace devilution
