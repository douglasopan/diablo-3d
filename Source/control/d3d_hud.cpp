#include "control/d3d_hud.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <string_view>

#include "control/control.hpp"
#include "control/control_flasks.hpp"
#include "control/control_panel.hpp"
#include "control/d3d_hud_layout.hpp"
#include "controls/control_mode.hpp"
#include "controls/game_controls.h"
#include "engine/palette.h"
#include "engine/render/d3d_ui_layout.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "options.h"
#include "panels/spell_icons.hpp"
#include "panels/spell_list.hpp"
#include "player.h"
#include "utils/display.h"

namespace devilution {

extern Rectangle LevelButtonRect;

namespace {

Rectangle LayoutRect(size_t index)
{
	const int screenHeight = std::max<int>(1, GetScreenHeight());
	const int width = GetScreenWidth() * 480 / screenHeight;
	constexpr int height = 480;
	const int safeWidth = D3dHudSafeWidth(width, height);
	const auto &entry = D3dHudLayoutEntries[index];
	return D3dHudScaleRect(D3dHudPlaceRect(GetD3dUiRect(entry.name, safeWidth, height,
	                         D3dHudDefaultRect(entry, safeWidth, height)),
	    entry, width, height), screenHeight);
}

void BlitScaled(const Surface &out, const Surface &source, Rectangle target, bool skipZero = false)
{
	for (int y = std::max(0, target.position.y); y < std::min(out.h(), target.position.y + target.size.height); ++y) {
		const int sy = (y - target.position.y) * source.h() / target.size.height;
		int runStart = -1;
		for (int x = std::max(0, target.position.x); x < std::min(out.w(), target.position.x + target.size.width); ++x) {
			const uint8_t color = *source.at((x - target.position.x) * source.w() / target.size.width, sy);
			if (!skipZero || color != 0) {
				*out.at(x, y) = color;
				if (runStart < 0) runStart = x;
			} else if (runStart >= 0) {
				MarkUiOverlayRect(out, runStart, y, x - runStart, 1);
				runStart = -1;
			}
		}
		if (runStart >= 0)
			MarkUiOverlayRect(out, runStart, y, std::min(out.w(), target.position.x + target.size.width) - runStart, 1);
	}
}

// Small engraved utility glyphs, rendered in palette colors without new assets.
constexpr std::array<std::array<uint16_t, 12>, 8> UtilityGlyphs {{
	{{ 0x0F0, 0x3FC, 0x7FE, 0x7FE, 0xF6F, 0xF6F, 0xF6F, 0x76E, 0x36C, 0x1F8, 0x0F0, 0x060 }}, // helm
	{{ 0x7FC, 0x404, 0x5F4, 0x404, 0x5F4, 0x404, 0x5F4, 0x404, 0x5F4, 0x404, 0x7FC, 0x000 }}, // quest scroll
	{{ 0x222, 0x777, 0xDDD, 0x999, 0x999, 0x999, 0x999, 0x999, 0xDDD, 0x777, 0x222, 0x000 }}, // folded map
	{{ 0x090, 0x696, 0x3FC, 0x7FE, 0xE07, 0xC03, 0xC03, 0xE07, 0x7FE, 0x3FC, 0x696, 0x090 }}, // gear
	{{ 0x1F8, 0x108, 0x3FC, 0x7FE, 0x6F6, 0x606, 0x6F6, 0x606, 0x606, 0x606, 0x7FE, 0x3FC }}, // pack
	{{ 0x000, 0x7BE, 0xC63, 0xD6B, 0xD6B, 0xD6B, 0xC63, 0xD6B, 0xD6B, 0xC63, 0x7BE, 0x000 }}, // open book
	{{ 0x000, 0x7FE, 0xC03, 0xC03, 0xD6B, 0xC03, 0xC03, 0x7FE, 0x180, 0x100, 0x000, 0x000 }}, // speech
	{{ 0x060, 0x0F0, 0x0F0, 0x060, 0x3FC, 0x7FE, 0x060, 0x060, 0x0F0, 0x198, 0x30C, 0x000 }}, // friendly
}};

std::string_view ButtonKey(int button)
{
	constexpr std::array<std::string_view, 8> Actions { "Character", "QuestLog", "ToggleAutomap", "", "Inventory", "SpellBook", "", "" };
	constexpr std::array<std::string_view, 8> Fallback { "C", "Q", "Tab", "Esc", "I", "B", "Enter", "" };
	if (button == 3 && ControlMode == ControlTypes::Gamepad) {
		const auto key = GetOptions().Padmapper.InputNameForAction("ToggleGameMenu1", true);
		return key.empty() ? GetOptions().Padmapper.InputNameForAction("ToggleGameMenu2", true) : key;
	}
	if (Actions[button].empty()) return Fallback[button];
	const auto key = ControlMode == ControlTypes::Gamepad
	    ? GetOptions().Padmapper.InputNameForAction(Actions[button], true)
	    : GetOptions().Keymapper.KeyNameForAction(Actions[button]);
	return key;
}

bool Intersects(Rectangle a, Rectangle b)
{
	return a.position.x < b.position.x + b.size.width && b.position.x < a.position.x + a.size.width
	    && a.position.y < b.position.y + b.size.height && b.position.y < a.position.y + a.size.height;
}

// Auxiliary native controls have no authored INI entries. Reserve a nearby free
// rectangle so the default HUD never hides them or consumes their mouse input.
Rectangle ReserveAuxiliaryRect(Rectangle preferred, Rectangle extra = {})
{
	const int width = GetScreenWidth(), height = GetScreenHeight();
	preferred.size.width = std::min(preferred.size.width, width);
	preferred.size.height = std::min(preferred.size.height, height);
	std::array<Rectangle, D3dHudLayoutEntries.size() + 3> occupied;
	size_t count = 0;
	for (size_t i = 0; i < D3dHudLayoutEntries.size(); ++i) {
		if (i >= 11 && !IsChatAvailable()) continue;
		occupied[count++] = LayoutRect(i);
	}
	if (IsLeftPanelOpen()) occupied[count++] = GetLeftPanel();
	if (IsRightPanelOpen()) occupied[count++] = GetRightPanel();
	if (extra.size.width > 0 && extra.size.height > 0) occupied[count++] = extra;
	const int gap = std::max(1, 8 * height / 480);
	Rectangle result = preferred;
	int bestDistance = std::numeric_limits<int>::max();
	const auto consider = [&](Rectangle candidate) {
		candidate.position.x = std::clamp(candidate.position.x, 0, width - candidate.size.width);
		candidate.position.y = std::clamp(candidate.position.y, 0, height - candidate.size.height);
		for (size_t i = 0; i < count; ++i)
			if (Intersects(candidate, occupied[i])) return;
		const int distance = std::abs(candidate.position.x - preferred.position.x) + std::abs(candidate.position.y - preferred.position.y);
		if (distance < bestDistance) {
			bestDistance = distance;
			result = candidate;
		}
	};
	consider(preferred);
	for (size_t i = 0; i < count; ++i) {
		Rectangle candidate = preferred;
		candidate.position.x = occupied[i].position.x - gap - candidate.size.width;
		consider(candidate);
		candidate.position.x = occupied[i].position.x + occupied[i].size.width + gap;
		consider(candidate);
		candidate = preferred;
		candidate.position.y = occupied[i].position.y - gap - candidate.size.height;
		consider(candidate);
	}
	// Arbitrarily overlapping authored controls can leave no free area; clamp
	// that fallback as well. Default compact/wide layouts have a reserved result.
	result.position.x = std::clamp(result.position.x, 0, width - result.size.width);
	result.position.y = std::clamp(result.position.y, 0, height - result.size.height);
	return result;
}

Rectangle LevelGroupRect()
{
	const Rectangle orb = GetD3dHudOrbRect(false);
	const int height = GetScreenHeight();
	const Size size { std::max(1, 120 * height / 480), std::max(1, 45 * height / 480) };
	return ReserveAuxiliaryRect({ { orb.Center().x - size.width / 2, orb.position.y - std::max(1, 8 * height / 480) - size.height }, size });
}

} // namespace

bool IsD3dHudEnabled()
{
	// Compact side panels occupy the same rows as the utility strip. Keep their
	// complete legacy surface until the next, explicitly separate panel pass.
	// Modifier menus also need the native spell surface for their button hints.
	return !ChatFlag && ControlMode != ControlTypes::VirtualGamepad
	    && !PadMenuNavigatorActive && !PadHotspellMenuActive
	    && !(CanPanelsCoverView() && (IsLeftPanelOpen() || IsRightPanelOpen()));
}

Rectangle GetD3dHudPanelButtonRect(int button)
{
	if (!IsD3dHudEnabled()) {
		Rectangle rect = MainPanelButtonRect[button];
		SetPanelObjectPosition(UiPanels::Main, rect);
		return rect;
	}
	constexpr std::array<size_t, 8> Indices { 5, 8, 9, 10, 6, 7, 11, 12 };
	return LayoutRect(Indices[button]);
}

Rectangle GetD3dHudBeltRect()
{
	if (IsD3dHudEnabled()) return LayoutRect(2);
	Rectangle rect = BeltRect;
	SetPanelObjectPosition(UiPanels::Main, rect);
	return rect;
}

Rectangle GetD3dHudBeltSlotRect(int slot)
{
	Rectangle rect = D3dHudBeltSlotRect(GetD3dHudBeltRect(), slot);
	if (!IsD3dHudEnabled()) rect.size.height = 29;
	return rect;
}

Rectangle GetD3dHudSpellRect()
{
	if (IsD3dHudEnabled()) return LayoutRect(3);
	Rectangle rect = SpellButtonRect;
	SetPanelObjectPosition(UiPanels::Main, rect);
	return rect;
}

Rectangle GetD3dHudInfoRect()
{
	if (IsD3dHudEnabled()) return LayoutRect(4);
	Rectangle rect = InfoBoxRect;
	SetPanelObjectPosition(UiPanels::Main, rect);
	return rect;
}
Rectangle GetD3dHudOrbRect(bool mana) { return LayoutRect(mana ? 1 : 0); }

Rectangle GetD3dHudLevelButtonRect()
{
	if (!IsD3dHudEnabled()) {
		Rectangle rect = LevelButtonRect;
		SetPanelObjectPosition(UiPanels::Main, rect);
		return rect;
	}
	const Rectangle group = LevelGroupRect();
	const int height = GetScreenHeight();
	const Size size { std::max(1, 41 * height / 480), std::max(1, 22 * height / 480) };
	return { group.position + Displacement { (group.size.width - size.width) / 2, group.size.height - size.height }, size };
}

Rectangle GetD3dHudLevelLabelRect()
{
	if (!IsD3dHudEnabled())
		return { GetMainPanel().position + Displacement { 0, LevelButtonRect.position.y - 23 }, { 120, 23 } };
	Rectangle group = LevelGroupRect();
	group.size.height -= GetD3dHudLevelButtonRect().size.height;
	return group;
}

Rectangle GetD3dHudDurabilityRect()
{
	const int height = GetScreenHeight();
	const Size size { std::max(1, 152 * height / 480), std::max(1, 32 * height / 480) };
	int right = 0, top = height;
	for (int i = 0; i < (IsChatAvailable() ? TotalMpMainPanelButtons : TotalSpMainPanelButtons); ++i) {
		const Rectangle button = GetD3dHudPanelButtonRect(i);
		right = std::max(right, button.position.x + button.size.width);
		top = std::min(top, button.position.y);
	}
	return ReserveAuxiliaryRect({ { right - size.width, top - std::max(1, 8 * height / 480) - size.height }, size }, LevelGroupRect());
}

Rectangle GetD3dHudValueRect(bool mana)
{
	const Rectangle orb = GetD3dHudOrbRect(mana);
	const int height = std::max(1, 14 * GetScreenHeight() / 480);
	Rectangle value { orb.position + Displacement { 0, orb.size.height }, { orb.size.width, height } };
	const Rectangle xp { { GetScreenWidth() / 2 - 155, GetScreenHeight() - 11 }, { 313, 9 } };
	if (*GetOptions().Gameplay.experienceBar
	    && value.position.x < xp.position.x + xp.size.width && xp.position.x < value.position.x + value.size.width
	    && value.position.y < xp.position.y + xp.size.height && xp.position.y < value.position.y + value.size.height)
		value.position.y -= height;
	return value;
}

bool IsPointOnD3dHud(Point point)
{
	if (!IsD3dHudEnabled()) return GetMainPanel().contains(point);
	if (GetD3dHudBeltRect().contains(point) || GetD3dHudSpellRect().contains(point)
	    || GetD3dHudOrbRect(false).contains(point) || GetD3dHudOrbRect(true).contains(point)) return true;
	for (int i = 0; i < (IsChatAvailable() ? TotalMpMainPanelButtons : TotalSpMainPanelButtons); ++i)
		if (GetD3dHudPanelButtonRect(i).contains(point)) return true;
	for (bool mana : { false, true }) {
		if (mana ? !*GetOptions().Gameplay.showManaValues : !*GetOptions().Gameplay.showHealthValues) continue;
		if (GetD3dHudValueRect(mana).contains(point)) return true;
	}
	if (*GetOptions().Gameplay.experienceBar
	    && Rectangle { { GetScreenWidth() / 2 - 155, GetScreenHeight() - 11 }, { 313, 9 } }.contains(point)) return true;
	return false;
}

void DrawD3dHudPlate(const Surface &out, Rectangle rect, bool pressed)
{
	FillRect(out, rect.position.x, rect.position.y, rect.size.width, rect.size.height, PAL16_GRAY + (pressed ? 15 : 14));
	const uint8_t light = PAL16_YELLOW + (pressed ? 11 : 8);
	const uint8_t dark = PAL16_YELLOW + 14;
	DrawHorizontalLine(out, rect.position, rect.size.width, light);
	DrawVerticalLine(out, rect.position, rect.size.height, light);
	DrawHorizontalLine(out, rect.position + Displacement { 0, rect.size.height - 1 }, rect.size.width, dark);
	DrawVerticalLine(out, rect.position + Displacement { rect.size.width - 1, 0 }, rect.size.height, dark);
	if (rect.size.width > 6 && rect.size.height > 6) {
		DrawHorizontalLine(out, rect.position + Displacement { 2, 2 }, rect.size.width - 4, PAL16_GRAY + 10);
		for (int x : { 2, rect.size.width - 3 })
			for (int y : { 2, rect.size.height - 3 }) out.SetPixel(rect.position + Displacement { x, y }, PAL16_YELLOW + 4);
	}
}

void DrawD3dHudPanelButton(const Surface &out, int button, bool pressed)
{
	const Rectangle target = GetD3dHudPanelButtonRect(button);
	OwnedSurface buttonImage(30, 32);
	const Surface &canvas = buttonImage;
	const Rectangle rect { { 0, 0 }, { 30, 32 } };
	DrawD3dHudPlate(canvas, rect, pressed);
	const int glyphSize = std::max(1, std::min(rect.size.width - 6, rect.size.height - 14));
	const Point origin = rect.position + Displacement { (rect.size.width - glyphSize) / 2, 3 + (pressed ? 1 : 0) };
	const uint8_t glyphColor = button == 7 && !MyPlayer->friendlyMode
	    ? PAL16_RED + 3 : PAL16_BEIGE + (pressed ? 8 : 3);
	for (int y = 0; y < glyphSize; ++y)
		for (int x = 0; x < glyphSize; ++x)
			if ((UtilityGlyphs[button][y * 12 / glyphSize] & (0x800 >> (x * 12 / glyphSize))) != 0)
				canvas.SetPixel(origin + Displacement { x, y }, glyphColor);
	DrawString(canvas, ButtonKey(button), { rect.position + Displacement { 1, rect.size.height - 12 }, { rect.size.width - 2, 12 } },
	    { .flags = UiFlags::ColorGold | UiFlags::AlignCenter | UiFlags::KerningFitSpacing, .spacing = 0 });
	BlitScaled(out, canvas, target);
}

void DrawD3dHudBeltBackground(const Surface &out)
{
	for (int i = 0; i < 8; ++i) DrawD3dHudPlate(out, GetD3dHudBeltSlotRect(i));
}

void DrawD3dHudBeltItem(const Surface &out, const Surface &nativeItem, Rectangle slot)
{
	const Rectangle inside = slot.size.width > 4 && slot.size.height > 4 ? slot.inset({ 2, 2 }) : slot;
	BlitScaled(out, nativeItem, inside, true);
}

void DrawD3dHudSpellIcon(const Surface &out, Rectangle rect, SpellID spell, SpellType type)
{
	OwnedSurface icon(SPLICONLENGTH, SPLICONLENGTH);
	FillRect(icon, 0, 0, icon.w(), icon.h(), 0);
	SetSpellTrans(type);
	DrawLargeSpellIcon(icon, { 0, SPLICONLENGTH - 1 }, spell);
	BlitScaled(out, icon, rect);
	DrawHorizontalLine(out, rect.position, rect.size.width, PAL16_YELLOW + 9);
	DrawVerticalLine(out, rect.position, rect.size.height, PAL16_YELLOW + 9);
}

void DrawD3dHudFlask(const Surface &out, bool mana)
{
	const Surface &empty = mana ? *pManaBuff : *pLifeBuff;
	const int offset = mana ? 464 : 96;
	const int fill = std::clamp(mana ? MyPlayer->_pManaPer : MyPlayer->_pHPPer, 0, 81);
	const Rectangle rect = GetD3dHudOrbRect(mana);
	for (int y = 0; y < rect.size.height; ++y) {
		const int sy = y * 88 / rect.size.height;
		int first = -1, last = -1;
		for (int x = 0; x < rect.size.width; ++x) {
			const int sx = x * 88 / rect.size.width;
			const bool globe = (sx - 43) * (sx - 43) + (sy - 43) * (sy - 43) < 37 * 37;
			const uint8_t base = *empty.at(sx, sy);
			if (base == 0 && !globe) continue;
			const Point target = rect.position + Displacement { x, y };
			if (!out.InBounds(target)) continue;
			const bool filled = sy >= 3 && sy < 16
			    ? sy >= 3 + std::clamp(81 - fill, 0, 13)
			    : sy >= 16 && sy < 85 && sy >= 85 - std::min(fill, 69);
			*out.at(target.x, target.y) = filled ? *BottomBuffer->at(offset + sx, sy) : base;
			if (first < 0) first = x;
			last = x;
		}
		if (first >= 0) MarkUiOverlayRect(out, rect.position.x + first, rect.position.y + y, last - first + 1, 1);
	}
}

void DrawD3dHudValues(const Surface &out)
{
	for (bool mana : { false, true }) {
		if (mana ? !*GetOptions().Gameplay.showManaValues : !*GetOptions().Gameplay.showHealthValues)
			continue;
		OwnedSurface values(88, 14);
		DrawD3dHudPlate(values, { { 0, 0 }, { 88, 14 } });
		const bool noMana = HasAnyOf(InspectPlayer->_pIFlags, ItemSpecialEffect::NoMana);
		DrawFlaskValues(values, { 44, 1 },
		    mana ? (noMana || MyPlayer->hasNoMana() ? 0 : MyPlayer->_pMana >> 6) : MyPlayer->_pHitPoints >> 6,
		    mana ? (noMana ? 0 : MyPlayer->_pMaxMana >> 6) : MyPlayer->_pMaxHP >> 6);
		BlitScaled(out, values, GetD3dHudValueRect(mana));
	}
}

void DrawD3dHudKeyLabel(const Surface &out, Rectangle rect, std::string_view text)
{
	const int scaleHeight = std::max<int>(1, GetScreenHeight());
	OwnedSurface label(std::max(1, rect.size.width * 480 / scaleHeight), 13);
	FillRect(label, 0, 0, label.w(), label.h(), 0);
	DrawString(label, text, { { 0, 0 }, { label.w(), label.h() } },
	    { .flags = UiFlags::ColorWhite | UiFlags::AlignRight | UiFlags::Outlined | UiFlags::KerningFitSpacing, .spacing = 0 });
	const int height = std::min(rect.size.height, std::max(1, 13 * scaleHeight / 480));
	BlitScaled(out, label, { rect.position + Displacement { 0, rect.size.height - height }, { rect.size.width, height } }, true);
}

void DrawD3dHudSurface(const Surface &out, const Surface &source, Rectangle rect, bool transparent)
{
	BlitScaled(out, source, rect, transparent);
}

} // namespace devilution
