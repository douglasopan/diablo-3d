#include "control/d3d_floating_panels.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "control/control.hpp"
#include "control/d3d_hud.hpp"
#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "diablo.h"
#include "engine/palette.h"
#include "engine/render/d3d_panel_presentation.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "inv.h"
#include "panels/quest_log.hpp"
#include "utils/display.h"
#include "utils/language.h"

namespace devilution {
namespace {

constexpr std::array<UiPanels, 4> FloatingPanels {
	UiPanels::Character, UiPanels::Quest, UiPanels::Inventory, UiPanels::Spell
};

bool IsFloatingPanel(UiPanels panel)
{
	return std::find(FloatingPanels.begin(), FloatingPanels.end(), panel) != FloatingPanels.end();
}

bool IsPanelVisible(UiPanels panel)
{
	switch (panel) {
	case UiPanels::Character: return CharFlag;
	case UiPanels::Quest: return QuestLogIsOpen && !CharFlag;
	case UiPanels::Inventory: return invflag;
	case UiPanels::Spell: return SpellbookFlag && !invflag;
	default: return false;
	}
}

Rectangle HeaderBounds(UiPanels panel)
{
	Rectangle bounds = GetD3dFloatingPanelBounds(panel);
	bounds.size.height = D3dFloatingPanelHeaderHeight;
	return bounds;
}

Rectangle CloseBounds(UiPanels panel)
{
	const Rectangle header = HeaderBounds(panel);
	return { header.position + Displacement { header.size.width - 21, 2 }, { 18, 18 } };
}

std::string_view PanelTitle(UiPanels panel)
{
	switch (panel) {
	case UiPanels::Character: return _("Character");
	case UiPanels::Quest: return _("Quests");
	case UiPanels::Inventory: return _("Inventory");
	case UiPanels::Spell: return _("Spells");
	default: return {};
	}
}

void ClosePanel(UiPanels panel)
{
	// Controller closing must retain the native held-item/drop refusal and cursor rules.
	if (ControlDevice != ControlTypes::KeyboardAndMouse) {
		GameActionType action = GameActionType_NONE;
		switch (panel) {
		case UiPanels::Character: action = GameActionType_TOGGLE_CHARACTER_INFO; break;
		case UiPanels::Quest: action = GameActionType_TOGGLE_QUEST_LOG; break;
		case UiPanels::Inventory: action = GameActionType_TOGGLE_INVENTORY; break;
		case UiPanels::Spell: action = GameActionType_TOGGLE_SPELL_BOOK; break;
		default: return;
		}
		ProcessGameAction(GameAction { action });
		if ((panel == UiPanels::Inventory && !invflag) || (panel == UiPanels::Spell && !SpellbookFlag))
			CloseGoldDrop();
		return;
	}
	switch (panel) {
	case UiPanels::Character: CloseCharPanel(); break;
	case UiPanels::Quest: QuestLogIsOpen = false; break;
	case UiPanels::Inventory:
		CloseInventory();
		CloseGoldDrop();
		break;
	case UiPanels::Spell:
		SpellbookFlag = false;
		CloseGoldDrop();
		break;
	default: break;
	}
}

} // namespace

void UpdateD3dFloatingPanelAreas(Rectangle &left, Rectangle &right)
{
	if (!IsD3dHudSelected())
		return;
	left.size = SidePanelSize;
	right.size = SidePanelSize;
	// Existing widescreen/gamepad x positions remain shared by every renderer and hit test.
	// The compact 640x480 HUD must start at or below y=374; its geometry is owned by core.
	left.position.y = std::max(D3dFloatingPanelHeaderHeight, left.position.y);
	right.position.y = left.position.y;
}

Rectangle GetD3dFloatingPanelBounds(UiPanels panel)
{
	if (!IsD3dHudSelected() || !IsFloatingPanel(panel))
		return { { 0, 0 }, { 0, 0 } };
	return { GetPanelPosition(panel) + Displacement { 0, -D3dFloatingPanelHeaderHeight },
		{ SidePanelSize.width, SidePanelSize.height + D3dFloatingPanelHeaderHeight } };
}

bool IsD3dFloatingPanelPoint(Point point)
{
	if (!IsD3dHudSelected())
		return false;
	for (const UiPanels panel : FloatingPanels) {
		if (IsPanelVisible(panel) && GetD3dFloatingPanelBounds(panel).contains(point))
			return true;
	}
	return false;
}

bool HandleD3dFloatingPanelClose(Point point)
{
	if (!IsD3dHudSelected() || InGameMenu())
		return false;
	for (const UiPanels panel : FloatingPanels) {
		if (!IsPanelVisible(panel) || !HeaderBounds(panel).contains(point))
			continue;
		if (CloseBounds(panel).contains(point))
			ClosePanel(panel);
		return true;
	}
	return false;
}

void DrawD3dFloatingPanelFrame(const Surface &out, Rectangle rect, bool selected)
{
	if (rect.size.width <= 0 || rect.size.height <= 0)
		return;
	const uint8_t color = selected ? PAL16_YELLOW + 4 : PAL16_BEIGE + 6;
	DrawHorizontalLine(out, rect.position, rect.size.width, color);
	DrawHorizontalLine(out, rect.position + Displacement { 0, rect.size.height - 1 }, rect.size.width, color);
	DrawVerticalLine(out, rect.position, rect.size.height, color);
	DrawVerticalLine(out, rect.position + Displacement { rect.size.width - 1, 0 }, rect.size.height, color);
}

void DrawD3dFloatingPanelBody(const Surface &out, UiPanels panel)
{
	if (IsD3dHudSelected() && IsFloatingPanel(panel))
		DrawD3dPanelBackplate(out, GetD3dFloatingPanelBounds(panel));
}

void DrawD3dFloatingPanelHeader(const Surface &out, UiPanels panel)
{
	if (!IsD3dHudSelected() || !IsFloatingPanel(panel))
		return;
	const Rectangle header = HeaderBounds(panel);
	const Rectangle close = CloseBounds(panel);
	DrawD3dFloatingPanelFrame(out, header);
	DrawD3dFloatingPanelFrame(out, close, close.contains(MousePosition));
	DrawString(out, PanelTitle(panel), { header.position + Displacement { 8, 0 }, { header.size.width - 38, header.size.height } },
		{ .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::KerningFitSpacing | UiFlags::ColorWhitegold });
	DrawString(out, "X", close,
		{ .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite });
}

} // namespace devilution
