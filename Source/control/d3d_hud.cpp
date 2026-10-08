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
#include "utils/sdl_geometry.h"

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
	std::array<Rectangle, D3dHudLayoutEntries.size() + 4> occupied;
	size_t count = 0;
	for (size_t i = 0; i < D3dHudLayoutEntries.size(); ++i) {
		if (i >= 11 && !IsChatAvailable()) continue;
		occupied[count++] = LayoutRect(i);
	}
	occupied[count++] = GetD3dHudFrameRect();
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
	if (IsChatAvailable()) {
		for (size_t index : { size_t { 11 }, size_t { 12 } }) {
			const Rectangle button = LayoutRect(index);
			if ((IsLeftPanelOpen() && Intersects(button, GetLeftPanel()))
			    || (IsRightPanelOpen() && Intersects(button, GetRightPanel())))
				return false;
		}
	}
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

Rectangle GetD3dHudFrameRect()
{
	if (!IsD3dHudEnabled()) return GetMainPanel();
	// The stone body follows the editable core. Globes protrude above its top
	// edge; conditional multiplayer controls do not fill the world between them.
	Rectangle frame = LayoutRect(2);
	for (size_t index : { size_t { 3 }, size_t { 4 }, size_t { 5 }, size_t { 6 }, size_t { 7 }, size_t { 8 }, size_t { 9 }, size_t { 10 } }) {
		const Rectangle rect = LayoutRect(index);
		const int right = std::max(frame.position.x + frame.size.width, rect.position.x + rect.size.width);
		const int bottom = std::max(frame.position.y + frame.size.height, rect.position.y + rect.size.height);
		frame.position.x = std::min(frame.position.x, rect.position.x);
		frame.position.y = std::min(frame.position.y, rect.position.y);
		frame.size = { right - frame.position.x, bottom - frame.position.y };
	}
	const int horizontal = std::max(1, 10 * GetScreenHeight() / 480);
	const int top = std::max(1, 6 * GetScreenHeight() / 480);
	const int right = std::min<int>(GetScreenWidth(), frame.position.x + frame.size.width + horizontal);
	frame.position.x = std::max(0, frame.position.x - horizontal);
	frame.size.width = right - frame.position.x;
	frame.position.y = std::max(0, frame.position.y - top);
	frame.size.height += top;
	frame.size.height = std::min<int>(frame.size.height, GetScreenHeight() - frame.position.y);
	return frame;
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
	// Values belong inside the native bulb, above its complete sculpted support.
	return { orb.position + Displacement { (mana ? 16 : 12) * orb.size.width / 112, 48 * orb.size.height / 144 },
		{ std::max(1, 88 * orb.size.width / 112), std::max(1, 14 * orb.size.height / 144) } };
}

bool IsPointOnD3dHud(Point point)
{
	if (!IsD3dHudEnabled()) return GetMainPanel().contains(point);
	if (GetD3dHudFrameRect().contains(point) || GetD3dHudBeltRect().contains(point) || GetD3dHudSpellRect().contains(point)
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

void DrawD3dHudPlate(const Surface &out, Rectangle rect, bool)
{
	// InfoBoxRect is the black text interior. Add the native panel's fine
	// border explicitly; copying that interior alone leaves an unframed hole.
	OwnedSurface plate(288, 64);
	plate.BlitFrom(*BottomBuffer, MakeSdlRect(177, 62, 288, 64), { 0, 0 });
	BlitScaled(plate, BottomBuffer->subregion(205, 16, 232, 4), { { 0, 0 }, { 288, 2 } });
	BlitScaled(plate, BottomBuffer->subregion(0, 140, 640, 4), { { 0, 62 }, { 288, 2 } });
	BlitScaled(plate, BottomBuffer->subregion(0, 16, 4, 128), { { 0, 0 }, { 2, 64 } });
	BlitScaled(plate, BottomBuffer->subregion(636, 16, 4, 128), { { 286, 0 }, { 2, 64 } });
	BlitScaled(out, plate, rect);
}

void DrawD3dHudBackground(const Surface &out)
{
	const Rectangle frame = GetD3dHudFrameRect();
	const int screenHeight = std::max<int>(1, GetScreenHeight());
	OwnedSurface body(std::max(1, frame.size.width * 480 / screenHeight),
	    std::max(1, frame.size.height * 480 / screenHeight));
	// This narrow native stone strip contains no buttons, text or filled bulb.
	// Repeat at its original pixel scale instead of stretching a complete panel
	// with stale controls underneath the editable elements.
	for (int y = 0; y < body.h(); ++y)
		for (int x = 0; x < body.w(); ++x)
			*body.at(x, y) = *BottomBuffer->at(185 + x % 16, 18 + y % 40);
	const int border = std::min({ 4, body.w() / 2, body.h() / 2 });
	if (border > 0) {
		BlitScaled(body, BottomBuffer->subregion(205, 16, 232, 4), { { 0, 0 }, { body.w(), border } });
		BlitScaled(body, BottomBuffer->subregion(0, 140, 640, 4), { { 0, body.h() - border }, { body.w(), border } });
		BlitScaled(body, BottomBuffer->subregion(0, 16, 4, 128), { { 0, 0 }, { border, body.h() } });
		BlitScaled(body, BottomBuffer->subregion(636, 16, 4, 128), { { body.w() - border, 0 }, { border, body.h() } });
	}
	BlitScaled(out, body, frame);
	// Keep the existing contextual area visible even while it contains no text.
	DrawD3dHudPlate(out, GetD3dHudInfoRect());
}

void DrawD3dHudPanelButton(const Surface &out, int button, bool pressed)
{
	const Rectangle target = GetD3dHudPanelButtonRect(button);
	OwnedSurface buttonImage(button < TotalSpMainPanelButtons ? 71 : 33,
	    button < TotalSpMainPanelButtons ? 21 : 32);
	FillRect(buttonImage, 0, 0, buttonImage.w(), buttonImage.h(), 0);
	DrawNativePanelButton(buttonImage, button, pressed);
	BlitScaled(out, buttonImage, target);
}

void DrawD3dHudBeltBackground(const Surface &out)
{
	for (int i = 0; i < 8; ++i)
		BlitScaled(out, BottomBuffer->subregion(205 + i * 29, 21, 29, 29), GetD3dHudBeltSlotRect(i));
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
}

void DrawD3dHudFlask(const Surface &out, bool mana)
{
	const Surface &empty = mana ? *pManaBuff : *pLifeBuff;
	const int offset = mana ? 464 : 96;
	const int columnOffset = mana ? 448 : 84;
	const int bulbOffset = offset - columnOffset;
	const int fill = std::clamp(mana ? MyPlayer->_pManaPer : MyPlayer->_pHPPer, 0, 81);
	const Rectangle rect = GetD3dHudOrbRect(mana);
	// Include the complete native support below the bulb. Replace the full
	// bulb opaquely before applying its fill, so 0% cannot reveal stale liquid.
	OwnedSurface column(112, 144);
	column.BlitFrom(*BottomBuffer, MakeSdlRect(columnOffset, 0, 112, 144), { 0, 0 });
	column.BlitFrom(empty, MakeSdlRect(0, 0, 88, 88), { bulbOffset, 0 });
	for (int y = 3; y < 85; ++y) {
		const bool filled = y < 16 ? y >= 3 + std::clamp(81 - fill, 0, 13)
		                          : y >= 85 - std::min(fill, 69);
		if (filled)
			column.BlitFrom(*BottomBuffer, MakeSdlRect(offset, y, 88, 1), { bulbOffset, y });
	}
	for (int y = 0; y < rect.size.height; ++y) {
		const int sy = y * 144 / rect.size.height;
		int runStart = -1;
		for (int x = 0; x < rect.size.width; ++x) {
			const int sx = x * 112 / rect.size.width;
			const bool globe = (sx - bulbOffset - 43) * (sx - bulbOffset - 43) + (sy - 43) * (sy - 43) < 37 * 37;
			const uint8_t color = *column.at(sx, sy);
			const Point target = rect.position + Displacement { x, y };
			if ((color != 0 || globe) && out.InBounds(target)) {
				*out.at(target.x, target.y) = color;
				if (runStart < 0) runStart = x;
			} else if (runStart >= 0) {
				MarkUiOverlayRect(out, rect.position.x + runStart, rect.position.y + y, x - runStart, 1);
				runStart = -1;
			}
		}
		if (runStart >= 0)
			MarkUiOverlayRect(out, rect.position.x + runStart, rect.position.y + y, rect.size.width - runStart, 1);
	}
}

void DrawD3dHudValues(const Surface &out)
{
	for (bool mana : { false, true }) {
		if (mana ? !*GetOptions().Gameplay.showManaValues : !*GetOptions().Gameplay.showHealthValues)
			continue;
		OwnedSurface values(88, 14);
		FillRect(values, 0, 0, values.w(), values.h(), 0);
		const bool noMana = HasAnyOf(InspectPlayer->_pIFlags, ItemSpecialEffect::NoMana);
		DrawFlaskValues(values, { 44, 1 },
		    mana ? (noMana || MyPlayer->hasNoMana() ? 0 : MyPlayer->_pMana >> 6) : MyPlayer->_pHitPoints >> 6,
		    mana ? (noMana ? 0 : MyPlayer->_pMaxMana >> 6) : MyPlayer->_pMaxHP >> 6);
		BlitScaled(out, values, GetD3dHudValueRect(mana), true);
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
