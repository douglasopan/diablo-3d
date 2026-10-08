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
	{ "HudHealthOrb", false, -224, -104, 88, 88 },
	{ "HudManaOrb", false, 136, -104, 88, 88 },
	{ "HudBelt", false, -116, -43, 232, 29 },
	{ "HudSpell", false, 60, -103, 56, 56 },
	{ "HudInfo", false, -200, -210, 400, 64 },
	{ "HudCharacter", true, -198, -44, 30, 32, true },
	{ "HudInventory", true, -166, -44, 30, 32, true },
	{ "HudSpellbook", true, -134, -44, 30, 32, true },
	{ "HudQuests", true, -102, -44, 30, 32, true },
	{ "HudMap", true, -70, -44, 30, 32, true },
	{ "HudMenu", true, -38, -44, 30, 32, true },
	{ "HudChat", true, -70, -80, 30, 32, true },
	{ "HudFriendly", true, -38, -80, 30, 32, true },
}};

inline int D3dHudSafeWidth(int width, int height)
{
	return std::min(width, height * 16 / 9);
}

inline Rectangle D3dHudDefaultRect(const D3dHudLayoutEntry &entry, int safeWidth, int height)
{
	return { { (entry.rightAnchor ? safeWidth : safeWidth / 2) + entry.x, height + entry.y }, { entry.width, entry.height } };
}

inline Rectangle D3dHudPlaceRect(Rectangle rect, const D3dHudLayoutEntry &entry, int width, int height)
{
	const int safeWidth = D3dHudSafeWidth(width, height);
	if (entry.utility && safeWidth < 844)
		rect.position.y -= 100;
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
