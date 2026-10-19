#include "control/d3d_floating_panels.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <fstream>
#include <optional>
#include <cmath>
#include <cstdint>
#include <sstream>
#include "utils/paths.h"
#include "qol/visual_store.h"
#include "qol/stash.h"

#include "control/control.hpp"
#include "control/d3d_hud.hpp"
#include "control/d3d_hud_skin.hpp"
#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "diablo.h"
#include "automap.h"
#include "engine/palette.h"
#include "engine/render/d3d_panel_presentation.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "inv.h"
#include "panels/quest_log.hpp"
#include "player.h"
#include "utils/display.h"
#include "utils/language.h"

namespace devilution {
namespace {

constexpr std::array<UiPanels, 5> FloatingPanels {
	UiPanels::Character, UiPanels::Quest, UiPanels::Inventory, UiPanels::Spell, UiPanels::Stash
};

std::array<UiPanels, FloatingPanels.size()> WindowDrawOrder = FloatingPanels;

std::array<std::optional<Point>,7> WindowPositions;
std::optional<UiPanels> DraggedWindow;
Displacement DragOffset;
Size DraggedWindowSize;
bool ResizingWindow=false;
std::array<float,7> WindowScales {};
Point DragStart;
Point DraggedWindowPosition;
float DraggedWindowScale = 1;
std::optional<Point> PreviousWindowPosition;
float PreviousWindowScale = 0;
D3dHudGeometryContext DragViewport;
int PointerTransformDepth=0;
std::optional<UiPanels> PointerWindow;
std::string LayoutProfile;
bool WindowLayoutLoaded = false;
void LoadWindowLayout()
{
	const std::string profile=paths::PrefPath();
	if(WindowLayoutLoaded && LayoutProfile==profile) return;
	WindowLayoutLoaded = true;
	CancelD3dFloatingPanelDrag();
	LayoutProfile=profile; WindowPositions={}; WindowScales={};
	WindowDrawOrder = FloatingPanels;
	std::ifstream input(profile+"hud-windows.layout", std::ios::binary | std::ios::ate);
	if (!input) return;
	const std::streamoff bytes = input.tellg();
	if (bytes < 0 || bytes > 8192) return;
	input.seekg(0);
	std::string line;
	while(std::getline(input,line)) {std::istringstream row(line);int slot,x,y;float scale=0;
		if(row>>slot>>x>>y && slot>0&&slot<7&&x>=0&&x<32768&&y>=0&&y<32768) {
			WindowPositions[slot]=Point{x,y};if(row>>scale && std::isfinite(scale) && scale > 0) WindowScales[slot]=std::clamp(scale,1.0F,2.0F);
		}
	}
}
void SaveWindowLayout()
{
	if(!WindowLayoutLoaded) return;
	std::ofstream output(LayoutProfile+"hud-windows.layout",std::ios::trunc);
	for(size_t slot=1;slot<WindowPositions.size();++slot) if(WindowPositions[slot])
		output<<slot<<' '<<WindowPositions[slot]->x<<' '<<WindowPositions[slot]->y<<' '<<WindowScales[slot]<<'\n';
}

bool IsFloatingPanel(UiPanels panel)
{
	return std::find(FloatingPanels.begin(), FloatingPanels.end(), panel) != FloatingPanels.end();
}

bool IsPanelVisible(UiPanels panel)
{
	switch (panel) {
	case UiPanels::Character: return CharFlag;
	case UiPanels::Quest: return QuestLogIsOpen && (IsD3dHudSelected() || !CharFlag);
	case UiPanels::Inventory: return invflag;
	case UiPanels::Stash: return IsStashOpen || IsVisualStoreOpen;
	case UiPanels::Map: return AutomapActive && GetAutomapType()==AutomapType::Minimap;
	case UiPanels::Spell: return SpellbookFlag && (IsD3dHudSelected() || !invflag);
	default: return false;
	}
}

Rectangle HeaderBounds(UiPanels panel)
{
	Rectangle bounds = GetD3dFloatingPanelNativeBounds(panel);
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
	case UiPanels::Stash: return IsVisualStoreOpen ? _("Store") : _("Stash");
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
	case UiPanels::Stash:
		if(IsVisualStoreOpen) CloseVisualStore(); else CloseStash();
		break;
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

Point WindowPointerToNative(UiPanels panel, Point point)
{
	const Rectangle visual = GetD3dFloatingPanelBounds(panel);
	const Rectangle native = GetD3dFloatingPanelNativeBounds(panel);
	return native.position + Displacement {
		(point.x - visual.position.x) * native.size.width / visual.size.width,
		(point.y - visual.position.y) * native.size.height / visual.size.height
	};
}

bool WindowGestureAllowed()
{
	if (!DraggedWindow || !IsD3dHudSelected() || !IsPanelVisible(*DraggedWindow) || InGameMenu()
	    || (MyPlayer && !MyPlayer->HoldItem.isEmpty())) return false;
	// Layout edits do not alter an in-flight gesture's physical output transform.
	const auto viewport = GetD3dHudGeometryContext();
	auto originalViewport = DragViewport;
	originalViewport.layoutGeneration = viewport.layoutGeneration;
	originalViewport.customizationGeneration = viewport.customizationGeneration;
	return originalViewport == viewport;
}

} // namespace

float GetD3dWindowScale(UiPanels panel)
{
	if(!IsFloatingPanel(panel) || !IsD3dHudEnabled()) return 1;
	LoadWindowLayout();
	const float requested=WindowScales[static_cast<size_t>(panel)]>0 ? WindowScales[static_cast<size_t>(panel)] : gnScreenHeight>=700?1.25F:1.0F;
	const float fit=std::min(float(gnScreenWidth)/SidePanelSize.width,float(gnScreenHeight)/(SidePanelSize.height+D3dFloatingPanelHeaderHeight));
	return std::clamp(requested,1.0F,std::max(1.0F,std::min(2.0F,fit)));
}
Point MapD3dWindowPointer(Point point)
{
	if(IsTownFirstPersonInputCaptured() || PointerTransformDepth != 0) return point;
	if (const auto panel = D3dFloatingPanelAt(point))
		return WindowPointerToNative(*panel, point);
	return point;
}

bool IsD3dWindowPointerTransformed()
{
	return PointerTransformDepth != 0 && PointerWindow.has_value();
}

Point D3dPanelPointerToVisual(UiPanels panel, Point nativePoint)
{
	if (!IsD3dHudSelected() || !IsFloatingPanel(panel))
		return nativePoint;
	const Rectangle native = GetD3dFloatingPanelNativeBounds(panel);
	const Rectangle visual = GetD3dFloatingPanelBounds(panel);
	if (!native.contains(nativePoint))
		return nativePoint;
	// Ceil selects the first physical pixel in the native pixel's inverse-map
	// interval. Truncation here would move a focused slot one pixel backward.
	const auto map = [](int offset, int destination, int source) {
		return static_cast<int>((static_cast<int64_t>(offset) * destination + source - 1) / source);
	};
	return visual.position + Displacement {
		map(nativePoint.x - native.position.x, visual.size.width, native.size.width),
		map(nativePoint.y - native.position.y, visual.size.height, native.size.height)
	};
}
ScopedD3dWindowPointer::ScopedD3dWindowPointer()
{
	owner = PointerTransformDepth == 0;
	saved = MousePosition;
	if (owner) {
		PointerWindow = IsTownFirstPersonInputCaptured() ? std::nullopt : D3dFloatingPanelAt(saved);
		MousePosition = MapD3dWindowPointer(saved);
	}
	++PointerTransformDepth;
}
ScopedD3dWindowPointer::~ScopedD3dWindowPointer()
{
	--PointerTransformDepth;
	if (owner) {
		MousePosition = saved;
		PointerWindow.reset();
	}
}

Rectangle ResolveD3dFloatingPanelArea(UiPanels panel, Rectangle nativeArea)
{
	if(!IsD3dHudSelected()) return nativeArea;
	LoadWindowLayout();
	const auto &saved=WindowPositions[static_cast<size_t>(panel)];
	if(saved) nativeArea.position=*saved;
	const float scale = GetD3dWindowScale(panel);
	const int header = panel == UiPanels::Map ? 0 : D3dFloatingPanelHeaderHeight;
	const int top = int(std::round(header * scale));
	// Clamp the same rounded outer rectangle that is painted and hit-tested.
	// Separately ceiling the body/header can exceed a fitted viewport by 1px.
	nativeArea.position.x = std::clamp(nativeArea.position.x, 0,
	    std::max(0, gnScreenWidth - int(std::round(nativeArea.size.width * scale))));
	nativeArea.position.y = std::clamp(nativeArea.position.y, top,
	    std::max(top, gnScreenHeight - int(std::round((nativeArea.size.height + header) * scale)) + top));
	return nativeArea;
}
void BeginD3dWindowDrag(UiPanels panel, Rectangle area, Point pointer)
{
	LoadWindowLayout();
	if (MyPlayer && !MyPlayer->HoldItem.isEmpty()) return;
	const size_t slot = static_cast<size_t>(panel);
	if (slot >= WindowPositions.size()) return;
	CancelD3dFloatingPanelDrag();
	FocusD3dFloatingPanel(panel);
	PreviousWindowPosition = WindowPositions[slot];
	PreviousWindowScale = WindowScales[slot];
	DragStart = pointer;
	DraggedWindowPosition = area.position;
	DraggedWindowScale = GetD3dWindowScale(panel);
	DragViewport = GetD3dHudGeometryContext();
	ResizingWindow=false; DraggedWindow=panel; DragOffset=pointer-area.position; DraggedWindowSize=area.size;
}
bool UpdateD3dFloatingPanelDrag(Point pointer)
{
	if(!DraggedWindow) return false;
	LoadWindowLayout();
	if (!DraggedWindow) return false;
	if (!WindowGestureAllowed()) {CancelD3dFloatingPanelDrag();return false;}
	const int header = *DraggedWindow == UiPanels::Map ? 0 : D3dFloatingPanelHeaderHeight;
	const int top = int(std::round(header * GetD3dWindowScale(*DraggedWindow)));
	if(ResizingWindow) {
		const Displacement delta = pointer - DragStart;
		const float width = SidePanelSize.width, height = SidePanelSize.height;
		// Project pointer motion onto the original aspect-ratio diagonal. This
		// is continuous even when horizontal and vertical movement disagree.
		const float change = (delta.deltaX * width + delta.deltaY * height)
		    / (width * width + height * height);
		const float fit = std::min(float(gnScreenWidth) / SidePanelSize.width,
		    float(gnScreenHeight) / (SidePanelSize.height + D3dFloatingPanelHeaderHeight));
		const size_t slot = static_cast<size_t>(*DraggedWindow);
		WindowScales[slot] = std::clamp(DraggedWindowScale + change, 1.0F, std::max(1.0F, std::min(2.0F, fit)));
		// Legacy rows store position and scale together, including a resize-only edit.
		WindowPositions[slot] = DraggedWindowPosition;
		return true;
	}
	Point at=pointer-DragOffset;
	at.x=std::clamp(at.x,0,std::max(0,gnScreenWidth-int(std::round(DraggedWindowSize.width*GetD3dWindowScale(*DraggedWindow)))));
	at.y=std::clamp(at.y,top,std::max(top,gnScreenHeight-int(std::round((DraggedWindowSize.height+header)*GetD3dWindowScale(*DraggedWindow)))+top));
	WindowPositions[static_cast<size_t>(*DraggedWindow)]=at;
	return true;
}
void EndD3dFloatingPanelDrag()
{
	LoadWindowLayout();
	if (DraggedWindow && !WindowGestureAllowed()) { CancelD3dFloatingPanelDrag(); return; }
	if(DraggedWindow) SaveWindowLayout();
	// A successful release commits the preview rather than restoring it.
	DraggedWindow.reset();
	ResizingWindow = false;
	PreviousWindowPosition.reset();
}
void CancelD3dFloatingPanelDrag()
{
	if (DraggedWindow) {
		const size_t slot = static_cast<size_t>(*DraggedWindow);
		WindowPositions[slot] = PreviousWindowPosition;
		WindowScales[slot] = PreviousWindowScale;
	}
	DraggedWindow.reset();
	ResizingWindow = false;
	PreviousWindowPosition.reset();
}

void ResetD3dFloatingPanelLayout()
{
	LoadWindowLayout();
	CancelD3dFloatingPanelDrag();
	WindowPositions = {};
	WindowScales = {};
	WindowDrawOrder = FloatingPanels;
	SaveWindowLayout();
}

std::span<const UiPanels> GetD3dFloatingPanelDrawOrder()
{
	LoadWindowLayout();
	return WindowDrawOrder;
}

void AppendD3dFloatingPanelOcclusionRects(std::vector<Rectangle> &areas)
{
    if(!IsD3dHudSelected())return;
    LoadWindowLayout();
    for(auto panel:WindowDrawOrder)if(IsPanelVisible(panel))areas.push_back(GetD3dFloatingPanelBounds(panel));
}

void FocusD3dFloatingPanel(UiPanels panel)
{
	if (!IsD3dHudSelected() || !IsFloatingPanel(panel) || !IsPanelVisible(panel))
		return;
	LoadWindowLayout();
	const auto found = std::find(WindowDrawOrder.begin(), WindowDrawOrder.end(), panel);
	if (found != WindowDrawOrder.end())
		std::rotate(found, found + 1, WindowDrawOrder.end());
}

std::optional<UiPanels> D3dFloatingPanelAt(Point point, bool bodyOnly)
{
	if (!IsD3dHudSelected())
		return std::nullopt;
	// Nested native handlers see the point after inverse scaling. Keep the
	// physical hit's identity instead of selecting a different overlapping UI.
	if (PointerTransformDepth != 0 && point == MousePosition) {
		if (!PointerWindow || !IsPanelVisible(*PointerWindow))
			return std::nullopt;
		const Rectangle bounds = bodyOnly ? GetPanelArea(*PointerWindow) : GetD3dFloatingPanelNativeBounds(*PointerWindow);
		return bounds.contains(point) ? PointerWindow : std::nullopt;
	}
	// Clock is painted by the late Lua hook. Test it only in physical space;
	// native coordinates inside a nested handler must retain their panel owner.
	if (PointerTransformDepth == 0 && IsD3dHudClockPoint(point)) return std::nullopt;
	LoadWindowLayout();
	for (auto it = WindowDrawOrder.rbegin(); it != WindowDrawOrder.rend(); ++it) {
		if (!IsPanelVisible(*it))
			continue;
		const Rectangle bounds = GetD3dFloatingPanelBounds(*it);
		if (!bounds.contains(point))
			continue;
		if (bodyOnly) {
			// Use the same integer inverse map as native handlers. Separately
			// rounding the scaled header can expose a one-pixel input seam.
			if (!GetPanelArea(*it).contains(WindowPointerToNative(*it, point)))
				return std::nullopt;
		}
		return *it;
	}
	return std::nullopt;
}

bool IsD3dPanelBodyPoint(UiPanels panel, Point point)
{
	return IsD3dHudSelected() ? D3dFloatingPanelAt(point, true) == panel : GetPanelArea(panel).contains(point);
}

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

Rectangle GetD3dFloatingPanelNativeBounds(UiPanels panel)
{
	if (!IsD3dHudSelected() || !IsFloatingPanel(panel))
		return { { 0, 0 }, { 0, 0 } };
	return { GetPanelPosition(panel) + Displacement { 0, -D3dFloatingPanelHeaderHeight },
		{ SidePanelSize.width, SidePanelSize.height + D3dFloatingPanelHeaderHeight } };
}

Rectangle GetD3dFloatingPanelBounds(UiPanels panel)
{
	const auto native=GetD3dFloatingPanelNativeBounds(panel);const float scale=GetD3dWindowScale(panel);
	return {native.position+Displacement{0,D3dFloatingPanelHeaderHeight-int(std::round(D3dFloatingPanelHeaderHeight*scale))},
	    {int(std::round(native.size.width*scale)),int(std::round(native.size.height*scale))}};
}
bool IsD3dFloatingPanelBodyPoint(Point point)
{
	return D3dFloatingPanelAt(point, true).has_value();
}

bool IsD3dFloatingPanelPoint(Point point)
{
	return D3dFloatingPanelAt(point).has_value();
}

bool HandleD3dFloatingPanelClose(Point point)
{
	if (!IsD3dHudSelected() || InGameMenu())
		return false;
	if (const auto hit = D3dFloatingPanelAt(point)) {
		const UiPanels panel = *hit;
		FocusD3dFloatingPanel(panel);
		if(IsPanelVisible(panel)) { const auto bounds=GetD3dFloatingPanelBounds(panel);
			if(Rectangle{bounds.position+Displacement{bounds.size.width-18,bounds.size.height-18},{18,18}}.contains(point)) {
				if (MyPlayer && !MyPlayer->HoldItem.isEmpty()) return true;
				BeginD3dWindowDrag(panel,{GetPanelPosition(panel),SidePanelSize},point);ResizingWindow=true;return true;
			}
		}
		if (!HeaderBounds(panel).contains(MapD3dWindowPointer(point))) {
			FocusD3dFloatingPanel(panel);
			return false;
		}
		if (CloseBounds(panel).contains(MapD3dWindowPointer(point)))
			ClosePanel(panel);
		else if (!MyPlayer || MyPlayer->HoldItem.isEmpty()) { BeginD3dWindowDrag(panel,{GetPanelPosition(panel),SidePanelSize},point); }
		return true;
	}
	return false;
}

void DrawD3dFloatingPanelFrame(const Surface &out, Rectangle rect, bool selected)
{
    if(rect.size.width<=0 || rect.size.height<=0)return;
    DrawD3dHudSkinIndexedFrame(out,rect,selected);
}


void DrawD3dFloatingPanelBody(const Surface &out, UiPanels panel)
{
	if (IsD3dHudSelected() && IsFloatingPanel(panel))
		DrawD3dPanelBackplate(out, GetD3dFloatingPanelNativeBounds(panel),GetD3dFloatingPanelBounds(panel));
}

void DrawD3dFloatingPanelHeader(const Surface &out, UiPanels panel)
{
	if (!IsD3dHudSelected() || !IsFloatingPanel(panel))
		return;
	const Rectangle header = HeaderBounds(panel);
	const Rectangle close = CloseBounds(panel);
	const auto native=GetD3dFloatingPanelNativeBounds(panel);
	DrawString(out,"+",{native.position+Displacement{native.size.width-18,native.size.height-18},{18,18}}, {.flags=UiFlags::ColorGold|UiFlags::AlignCenter});
	DrawD3dFloatingPanelFrame(out, header);
	DrawD3dFloatingPanelFrame(out, close,
		D3dFloatingPanelAt(MousePosition) == panel && close.contains(MapD3dWindowPointer(MousePosition)));
	DrawString(out, PanelTitle(panel), { header.position + Displacement { 8, 0 }, { header.size.width - 38, header.size.height } },
		{ .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::KerningFitSpacing | UiFlags::ColorWhitegold });
	DrawString(out, "X", close,
		{ .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite });
}

} // namespace devilution
