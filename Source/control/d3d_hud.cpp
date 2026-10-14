#include "control/d3d_hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>

#include "control/control.hpp"
#include "automap.h"
#include "control/control_flasks.hpp"
#include "control/control_panel.hpp"
#include "control/d3d_floating_panels.hpp"
#include "control/d3d_hud_layout.hpp"
#include "control/d3d_hud_skin.hpp"
#include "controls/control_mode.hpp"
#include "controls/game_controls.h"
#include "controls/plrctrls.h"
#include "engine/palette.h"
#include "engine/hd_sprite_pack.hpp"
#include "engine/render/d3d_hud_presentation.hpp"
#include "engine/render/d3d_ui_layout.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "options.h"
#include "panels/mainpanel.hpp"
#include "panels/spell_icons.hpp"
#include "panels/spell_list.hpp"
#include "player.h"
#include "utils/display.h"
#include "utils/sdl_geometry.h"
#include "utils/str_cat.hpp"

namespace devilution {

extern Rectangle LevelButtonRect;

namespace {

bool ClockDrawnInLastFrame = false;

Rectangle LayoutRect(size_t index)
{
	return GetD3dHudGeometry().elements[index];
}

bool IsCompactLayout()
{
	return IsD3dHudLayoutActive() && GetD3dHudGeometry().mode == D3dHudGeometryMode::Compact;
}

int DesignPixelsX(int pixels)
{
	return IsCompactLayout() ? GetD3dHudDesignPixelsX(pixels) : std::max(1, pixels * GetScreenHeight() / 480);
}

int DesignPixelsY(int pixels)
{
	return IsCompactLayout() ? GetD3dHudDesignPixelsY(pixels) : std::max(1, pixels * GetScreenHeight() / 480);
}

Rectangle ContainContent(Rectangle target, int width, int height)
{
	const auto &context = GetD3dHudGeometry().context;
	const float scale = std::min(target.size.width * context.outputScaleX / width, target.size.height * context.outputScaleY / height);
	const Size size { std::clamp(static_cast<int>(std::lround(width * scale / context.outputScaleX)), 1, target.size.width),
		std::clamp(static_cast<int>(std::lround(height * scale / context.outputScaleY)), 1, target.size.height) };
	return { target.position + Displacement { (target.size.width - size.width) / 2, (target.size.height - size.height) / 2 }, size };
}

Rectangle CenterGlyphBuffer(Rectangle target, const Surface &source, float scaleX, float scaleY)
{
	const Size size { std::clamp(static_cast<int>(std::lround(source.w() * scaleX)), 1, target.size.width),
		std::clamp(static_cast<int>(std::lround(source.h() * scaleY)), 1, target.size.height) };
	return { target.position + Displacement { (target.size.width - size.width) / 2, (target.size.height - size.height) / 2 }, size };
}

void BlitScaled(const Surface &out, const Surface &source, Rectangle target, bool skipZero = false)
{
	if (target.size.width <= 0 || target.size.height <= 0) return;
	const int right = std::min(out.w(), target.position.x + target.size.width);
	for (int y = std::max(0, target.position.y); y < std::min(out.h(), target.position.y + target.size.height); ++y) {
		const int sy = (y - target.position.y) * source.h() / target.size.height;
		int x = std::max(0, target.position.x);
		while (x < right) {
			while (x < right && skipZero && *source.at((x - target.position.x) * source.w() / target.size.width, sy) == 0) ++x;
			const int start = x;
			while (x < right && (!skipZero || *source.at((x - target.position.x) * source.w() / target.size.width, sy) != 0)) ++x;
			if (start == x) continue;
			MarkUiOverlayRect(out, start, y, x - start, 1);
			for (int dx = start; dx < x; ++dx)
				*out.at(dx, y) = *source.at((dx - target.position.x) * source.w() / target.size.width, sy);
		}
	}
}

Rectangle LevelGroupRect()
{
	// Auxiliary defaults belong to the base layout. Opening/moving a panel or
	// another module must never repack this module's independent saved position.
	const auto standard = ResolveD3dHudGeometry(GetD3dHudGeometryContext());
	const Rectangle orb = standard.elements[0];
	const Size size { DesignPixelsX(120), DesignPixelsY(45) };
	return ResolveD3dHudCustomizedRect(14, { { orb.Center().x - size.width / 2, orb.position.y - DesignPixelsY(8) - size.height }, size });
}

} // namespace

bool IsD3dHudSelected()
{
	// This build has one selected skin. Home/F4 select the world backend only;
	// chat, controllers and renderer failure change presentation, never the skin.
	return true;
}

bool IsD3dHudLayoutActive()
{
	// RGB may decline for native controller overlays; their CPU path keeps
	// the same geometry. Chat retains its modal native input/recipient layout.
	return IsD3dHudSelected() && !ChatFlag;
}

uint8_t GetD3dHudBackgroundOpacity()
{
	const int transparency = std::clamp(*GetOptions().Graphics.hudTransparency, 0, 100);
	return static_cast<uint8_t>((255 * (100 - transparency) + 50) / 100);
}

bool IsD3dHudEnabled()
{
    // Floating panels retain their own presentation and hit priority. Their
    // visibility never repacks or replaces independently customized modules.
    return !ChatFlag && ControlMode != ControlTypes::VirtualGamepad
        && !PadMenuNavigatorActive && !PadHotspellMenuActive;
}

Rectangle GetD3dHudPanelButtonRect(int button)
{
	if (!IsD3dHudLayoutActive()) {
		Rectangle rect = MainPanelButtonRect[button];
		SetPanelObjectPosition(UiPanels::Main, rect);
		return rect;
	}
	constexpr std::array<size_t, 8> Indices { 5, 8, 9, 10, 6, 7, 11, 12 };
	return LayoutRect(Indices[button]);
}

Rectangle GetD3dHudBeltRect()
{
	if (IsD3dHudLayoutActive()) return LayoutRect(2);
	Rectangle rect = BeltRect;
	SetPanelObjectPosition(UiPanels::Main, rect);
	return rect;
}

Rectangle GetD3dHudBeltSlotRect(int slot)
{
	if (IsD3dHudLayoutActive()) return GetD3dHudGeometry().beltSlots[std::clamp(slot, 0, 7)];
	Rectangle rect = D3dHudBeltSlotRect(GetD3dHudBeltRect(), slot);
	rect.size.height = 29;
	return rect;
}

Rectangle GetD3dHudBeltInteriorRect(Rectangle slot)
{
	const bool compact = IsCompactLayout();
	const int insetX = compact ? DesignPixelsX(2) : 2;
	const int insetY = compact ? DesignPixelsY(2) : 2;
	return slot.size.width > 2 * insetX && slot.size.height > 2 * insetY ? slot.inset({ insetX, insetY }) : slot;
}

Rectangle GetD3dHudSpellRect()
{
	if (IsD3dHudLayoutActive()) return LayoutRect(3);
	Rectangle rect = SpellButtonRect;
	SetPanelObjectPosition(UiPanels::Main, rect);
	return rect;
}

Rectangle GetD3dHudInfoRect()
{
	if (IsD3dHudLayoutActive()) return LayoutRect(4);
	Rectangle rect = InfoBoxRect;
	SetPanelObjectPosition(UiPanels::Main, rect);
	return rect;
}

Rectangle GetD3dHudFrameRect()
{
	if (!IsD3dHudLayoutActive()) return GetMainPanel();
	return GetD3dHudGeometry().frame;
}

Rectangle GetD3dHudOrbRect(bool mana)
{
	if (IsD3dHudLayoutActive()) return LayoutRect(mana ? 1 : 0);
	return { GetMainPanel().position + Displacement { mana ? 448 : 84, -16 }, { 112, 144 } };
}

Rectangle GetD3dHudLevelButtonRect()
{
	if (!IsD3dHudLayoutActive()) {
		Rectangle rect = LevelButtonRect;
		SetPanelObjectPosition(UiPanels::Main, rect);
		return rect;
	}
	const Rectangle group = LevelGroupRect();
	const Size size { std::max(1, group.size.width * 41 / 120), std::max(1, group.size.height * 22 / 45) };
	return { group.position + Displacement { (group.size.width - size.width) / 2, group.size.height - size.height }, size };
}

Rectangle GetD3dHudLevelLabelRect()
{
	if (!IsD3dHudLayoutActive())
		return { GetMainPanel().position + Displacement { 0, LevelButtonRect.position.y - 23 }, { 120, 23 } };
	Rectangle group = LevelGroupRect();
	group.size.height -= GetD3dHudLevelButtonRect().size.height;
	return group;
}

Rectangle GetD3dHudDurabilityRect()
{
	const Size size { DesignPixelsX(152), DesignPixelsY(32) };
	int right = 0, top = GetScreenHeight();
	const auto standard = ResolveD3dHudGeometry(GetD3dHudGeometryContext());
	for (int i = 0; i < (IsChatAvailable() ? TotalMpMainPanelButtons : TotalSpMainPanelButtons); ++i) {
		constexpr std::array<size_t, 8> indices { 5, 8, 9, 10, 6, 7, 11, 12 };
		const Rectangle button = standard.elements[indices[i]];
		right = std::max(right, button.position.x + button.size.width);
		top = std::min(top, button.position.y);
	}
	return ResolveD3dHudCustomizedRect(15, { { std::max(0,right - size.width), std::max(0,top - DesignPixelsY(8) - size.height) }, size });
}

Rectangle GetD3dHudExperienceRect()
{
	const auto &panel = GetMainPanel();
	return ResolveD3dHudCustomizedRect(13, { { panel.position.x + panel.size.width / 2 - 155, panel.position.y + panel.size.height - 11 }, {313,9} });
}

Rectangle GetD3dHudClockRect()
{
	return ResolveD3dHudCustomizedRect(D3dHudClockElement,
		{ { std::max<int>(0, gnScreenWidth - 76), 6 }, { 76, 16 } });
}

bool IsD3dHudClockPoint(Point point)
{
	return ClockDrawnInLastFrame && !IsD3dWindowPointerTransformed()
		&& IsD3dHudLayoutActive() && GetD3dHudClockRect().contains(point);
}

void BeginD3dHudAuxiliaryFrame() { ClockDrawnInLastFrame = false; }

void DrawD3dHudClock(const Surface &out, std::string_view text)
{
	if (text.empty()) return;
	// Preserve the existing late Lua hook and native UI capture/recovery order.
	// Time comes from Lua, never from a new gameplay clock or timer.
	OwnedSurface glyphs(76, 16);
	SDL_FillRect(glyphs.surface, nullptr, 1);
	DrawString(glyphs, text.substr(0, 64), { 0, 0 });
	DrawD3dHudContent(out, glyphs, GetD3dHudClockRect());
	ClockDrawnInLastFrame = true;
}

std::optional<size_t> GetD3dHudElementAt(Point point)
{
	if (!IsD3dHudLayoutActive()) return std::nullopt;
	// The existing Lua callback paints last, above panels and Map.
	if (IsD3dHudClockPoint(point)) return D3dHudClockElement;
	if (D3dFloatingPanelAt(point) || IsD3dMinimapPoint(point)) return std::nullopt;
	// Top-to-bottom native foreground order. Values belong to their orb;
	// every other decorative module is one complete interactive container.
	for (bool mana : {true,false}) {
		if ((mana ? *GetOptions().Gameplay.showManaValues : *GetOptions().Gameplay.showHealthValues)
		    && GetD3dHudValueRect(mana).contains(point)) return mana ? 1 : 0;
	}
	constexpr std::array<size_t,16> order {13,12,11,7,6,10,9,8,5,2,3,1,0,4,14,15};
	for (size_t id : order) {
		if (id >= 11 && id <= 12 && !IsChatAvailable()) continue;
		if (id == 13 && !*GetOptions().Gameplay.experienceBar) continue;
		if (id == 14 && (!MyPlayer || MyPlayer->_pStatPts == 0 || CharFlag || SpellSelectFlag)) continue;
		if (id == 15) {
			bool visible = false;
			if (MyPlayer) for (int slot : {INVLOC_HEAD,INVLOC_CHEST,INVLOC_HAND_LEFT,INVLOC_HAND_RIGHT}) {
				const Item &item = MyPlayer->InvBody[slot];
				visible |= !item.isEmpty() && item._iDurability <= 5;
			}
			if (!visible) continue;
		}
		const Rectangle area = id < 13 ? LayoutRect(id) : id == 13 ? GetD3dHudExperienceRect()
		    : id == 14 ? LevelGroupRect() : GetD3dHudDurabilityRect();
		if (area.contains(point)) return id;
	}
	return std::nullopt;
}

bool HandleD3dHudCustomizationDown(Point point, uint8_t button, uint16_t modifiers)
{
	if (!IsD3dHudLayoutActive() || InGameMenu() || ControlDevice != ControlTypes::KeyboardAndMouse
	    || (modifiers & SDL_KMOD_CTRL) == 0 || (MyPlayer && !MyPlayer->HoldItem.isEmpty())) return false;
	const auto id = GetD3dHudElementAt(point);
	if (!id) return false;
	const Rectangle area = *id < 13 ? LayoutRect(*id) : *id == 13 ? GetD3dHudExperienceRect()
	    : *id == 14 ? LevelGroupRect() : *id == D3dHudClockElement ? GetD3dHudClockRect() : GetD3dHudDurabilityRect();
	if (button == SDL_BUTTON_RIGHT) {
		if ((modifiers & SDL_KMOD_SHIFT) != 0) { ResetAllD3dHudCustomizations(); ResetD3dFloatingPanelLayout(); }
		else ResetD3dHudCustomization(*id);
		return true;
	}
	if (button != SDL_BUTTON_LEFT) return false;
	BeginD3dHudCustomization(*id, area, point, (modifiers & SDL_KMOD_SHIFT) != 0);
	return true;
}

Rectangle GetD3dHudValueRect(bool mana)
{
	const Rectangle orb = GetD3dHudOrbRect(mana);
	if (IsCompactLayout()) {
		const auto placement = PlaceD3dHudSkinOrb(orb, mana);
		const int width = std::max(1, static_cast<int>(placement.radius * 2));
		const int height = std::clamp(static_cast<int>(std::lround(14 * GetD3dHudMetrics().valueGlyphScaleY)), 1, width);
		return { { static_cast<int>(std::lround(placement.centerX - width / 2.0F)),
			         static_cast<int>(std::lround(placement.centerY - height / 2.0F)) }, { width, height } };
	}
	// Values belong inside the native bulb, above its complete sculpted support.
	return { orb.position + Displacement { (mana ? 16 : 12) * orb.size.width / 112, 48 * orb.size.height / 144 },
		{ std::max(1, 88 * orb.size.width / 112), std::max(1, 14 * orb.size.height / 144) } };
}

bool IsPointOnD3dHud(Point point)
{
	if (!IsD3dHudLayoutActive()) return GetMainPanel().contains(point);
	return GetD3dHudElementAt(point).has_value();
}

void DrawD3dHudPlate(const Surface &out, Rectangle rect, bool pressed)
{
	if (!IsD3dHudPresentationCapturing(out))
		DrawD3dHudSkinIndexedPlate(out, rect, pressed);
}

void DrawD3dHudBackground(const Surface &out)
{
	if (IsD3dHudPresentationCapturing(out)) return;
	// Modular controls leave the spaces between them transparent and interactive.
	DrawD3dHudPlate(out, GetD3dHudInfoRect());
	DrawD3dHudPlate(out, GetD3dHudSpellRect());
}

void DrawD3dHudPanelButton(const Surface &out, int button, bool pressed)
{
	RecordD3dHudButtonState(button, pressed);
	const Rectangle target = GetD3dHudPanelButtonRect(button);
	if (!IsD3dHudPresentationCapturing(out))
		DrawD3dHudSkinIndexedPlateShade(out, target, D3dHudSkinPart::Button,
		    pressed ? 184 : target.contains(MousePosition) ? 255 : 232);
	const bool compact = IsCompactLayout();
	const auto &metrics = GetD3dHudMetrics();
	const bool label = button < TotalSpMainPanelButtons;
	OwnedSurface foreground(compact && label ? std::max(1, static_cast<int>(target.size.width / metrics.labelGlyphScaleX)) : label ? 71 : 33,
	    compact && label ? std::max(1, static_cast<int>(target.size.height / metrics.labelGlyphScaleY)) : label ? 21 : 32);
	FillRect(foreground, 0, 0, foreground.w(), foreground.h(), 1);
	if (button < TotalSpMainPanelButtons)
		DrawMainPanelButtonLabel(foreground, button, pressed);
	else
		DrawNativePanelButton(foreground, button, pressed);
	const Rectangle content = !compact ? target : label ? CenterGlyphBuffer(target, foreground, metrics.labelGlyphScaleX, metrics.labelGlyphScaleY) : ContainContent(target, foreground.w(), foreground.h());
	DrawD3dHudContent(out, foreground, content);
}

void DrawD3dHudBeltBackground(const Surface &out)
{
	if (IsD3dHudPresentationCapturing(out)) return;
	for (int i = 0; i < 8; ++i)
		DrawD3dHudSkinIndexedPlate(out, GetD3dHudBeltSlotRect(i));
}

void DrawD3dHudBeltItem(const Surface &out, const Surface &nativeItem, Rectangle slot)
{
	const bool compact = IsCompactLayout();
	const Rectangle inside = GetD3dHudBeltInteriorRect(slot);
	DrawD3dHudContent(out, nativeItem, compact ? ContainContent(inside, nativeItem.w(), nativeItem.h()) : inside);
}

bool DrawD3dHudBeltHdItem(const Surface &out, ClxSprite sprite, Rectangle slot)
{
	auto pixels = LoadHdSpritePixels(sprite);
	if (!pixels || pixels->pixels.empty()) return false;
	OwnedSurface image(pixels->width, pixels->height);
	for (int y = 0; y < pixels->height; ++y)
		for (int x = 0; x < pixels->width; ++x) {
			const size_t i = static_cast<size_t>(y) * pixels->width + x;
			*image.at(x, y) = pixels->opacity[i] ? pixels->pixels[i] : 1;
		}
	const Rectangle inside = GetD3dHudBeltInteriorRect(slot);
	const Rectangle target = ContainContent(inside, image.w(), image.h());
	DrawD3dHudContent(out, image, target);
	auto rgba = LoadHdSpriteRgba(sprite);
	if (rgba && !rgba->empty()) RecordD3dHudRgbaForeground(image.w(), image.h(), *rgba, target);
	return true;
}

void DrawD3dHudSpellIcon(const Surface &out, Rectangle rect, SpellID spell, SpellType type)
{
	OwnedSurface icon(SPLICONLENGTH, SPLICONLENGTH);
	FillRect(icon, 0, 0, icon.w(), icon.h(), 1);
	SetSpellTrans(type);
	DrawLargeSpellIcon(icon, { 0, SPLICONLENGTH - 1 }, spell);
	if (IsCompactLayout()) {
		const int insetX = DesignPixelsX(2), insetY = DesignPixelsY(2);
		if (rect.size.width > 2 * insetX && rect.size.height > 2 * insetY) rect = rect.inset({ insetX, insetY });
		rect = ContainContent(rect, icon.w(), icon.h());
	}
	auto hd = LoadLargeSpellIconHdPixels(spell);
	if (hd && !hd->pixels.empty()) {
		OwnedSurface image(hd->width, hd->height);
		for (int y = 0; y < hd->height; ++y)
			for (int x = 0; x < hd->width; ++x) {
				const size_t i = static_cast<size_t>(y) * hd->width + x;
				*image.at(x, y) = hd->opacity[i] ? hd->pixels[i] : 1;
			}
		DrawD3dHudContent(out, image, rect);
		auto rgba = LoadLargeSpellIconRgba(spell);
		if (rgba && !rgba->empty()) RecordD3dHudRgbaForeground(image.w(), image.h(), *rgba, rect);
	} else DrawD3dHudContent(out, icon, rect);
}

void DrawD3dHudFlask(const Surface &out, bool mana)
{
	if (IsD3dHudPresentationCapturing(out)) return;
	const int fill = std::clamp(mana ? MyPlayer->_pManaPer : MyPlayer->_pHPPer, 0, 81);
	DrawD3dHudSkinIndexedOrb(out, GetD3dHudOrbRect(mana), mana, fill);
}

void DrawD3dHudValues(const Surface &out)
{
	for (bool mana : { false, true }) {
		if (mana ? !*GetOptions().Gameplay.showManaValues : !*GetOptions().Gameplay.showHealthValues)
			continue;
		Rectangle target = GetD3dHudPresentationValueRect(mana);
		const bool compact = IsCompactLayout();
		int width = compact ? std::max(1, static_cast<int>(target.size.width / GetD3dHudMetrics().valueGlyphScaleX)) : 88;
		const bool noMana = HasAnyOf(InspectPlayer->_pIFlags, ItemSpecialEffect::NoMana);
		const int current = mana ? (noMana || MyPlayer->hasNoMana() ? 0 : MyPlayer->_pMana >> 6) : MyPlayer->_pHitPoints >> 6;
		const int maximum = mana ? (noMana ? 0 : MyPlayer->_pMaxMana >> 6) : MyPlayer->_pMaxHP >> 6;
		if (compact) {
			// The native slash is centred, with both values placed independently.
			// Preserve every digit when either side exceeds the intended font size;
			// fit that complete source uniformly inside the measured bulb rectangle.
			const int half = std::max(GetLineWidth(StrCat(current), GameFont12), GetLineWidth(StrCat(maximum), GameFont12));
			width = std::max(width, 2 * (half + GetLineWidth("/", GameFont12) + 2));
			const int height = std::min(target.size.height, std::max(1, static_cast<int>(std::lround(14.0F * target.size.width / width))));
			target.position.y += (target.size.height - height) / 2;
			target.size.height = height;
		}
		OwnedSurface values(width, 14);
		FillRect(values, 0, 0, values.w(), values.h(), 1);
		DrawFlaskValues(values, { values.w() / 2, 1 }, current, maximum);
		DrawD3dHudContent(out, values, target);
	}
}

void DrawD3dHudKeyLabel(const Surface &out, Rectangle rect, std::string_view text)
{
	const int scaleHeight = IsD3dHudLayoutActive() ? std::max<int>(1, GetScreenHeight()) : 480;
	const bool compact = IsCompactLayout();
	const auto &metrics = GetD3dHudMetrics();
	OwnedSurface label(compact ? std::max(1, static_cast<int>(rect.size.width / metrics.keyGlyphScaleX)) : std::max(1, rect.size.width * 480 / scaleHeight), 13);
	FillRect(label, 0, 0, label.w(), label.h(), 1);
	DrawString(label, text, { { 0, 0 }, { label.w(), label.h() } },
	    { .flags = UiFlags::ColorWhite | UiFlags::AlignRight | UiFlags::Outlined | UiFlags::KerningFitSpacing, .spacing = 0 });
	const int height = std::min(rect.size.height, compact ? std::max(1, static_cast<int>(std::lround(13 * metrics.keyGlyphScaleY))) : std::max(1, 13 * scaleHeight / 480));
	const Rectangle target { rect.position + Displacement { 0, rect.size.height - height }, { rect.size.width, height } };
	DrawD3dHudContent(out, label, target);
}

void DrawD3dHudSurface(const Surface &out, const Surface &source, Rectangle rect, bool transparent)
{
	BlitScaled(out, source, rect, transparent);
}

void DrawD3dHudContent(const Surface &out, const Surface &source, Rectangle rect, uint8_t transparentIndex)
{
	RecordD3dHudForeground(source, rect, transparentIndex);
	if (rect.size.width <= 0 || rect.size.height <= 0) return;
	const int right = std::min(out.w(), rect.position.x + rect.size.width);
	for (int y = std::max(0, rect.position.y); y < std::min(out.h(), rect.position.y + rect.size.height); ++y) {
		const int sy = (y - rect.position.y) * source.h() / rect.size.height;
		int x = std::max(0, rect.position.x);
		while (x < right) {
			while (x < right && *source.at((x - rect.position.x) * source.w() / rect.size.width, sy) == transparentIndex) ++x;
			const int start = x;
			while (x < right && *source.at((x - rect.position.x) * source.w() / rect.size.width, sy) != transparentIndex) ++x;
			if (start == x) continue;
			// Coverage is announced before the write, including opaque palette 0.
			MarkUiOverlayRect(out, start, y, x - start, 1);
			for (int dx = start; dx < x; ++dx)
				*out.at(dx, y) = *source.at((dx - rect.position.x) * source.w() / rect.size.width, sy);
		}
	}
}

} // namespace devilution
