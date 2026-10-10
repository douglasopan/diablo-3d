#pragma once

// PRIVATE SOURCE ONLY. The principal adds the --item-labels dispatch and runs it.
// Requires the proposed GetTownViewItemLabelAnchors() API. No production hooks,
// simulated input, gameplay loop, Lua script or asset generation are introduced.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "control/control.hpp"
#include "control/d3d_hud.hpp"
#include "controls/control_mode.hpp"
#include "cursor.h"
#include "diablo.h"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/text_render.hpp"
#include "engine/render/town_gpu.hpp"
#include "engine/render/town_view.hpp"
#include "engine/render/ui_overlay_regions.hpp"
#include "engine/surface.hpp"
#include "headless_mode.hpp"
#include "gmenu.h"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "options.h"
#include "qol/itemlabels.h"
#include "stores.h"
#include "tables/itemdat.h"
#include "utils/format.hpp"
#include "utils/format_int.hpp"
#include "utils/language.h"

namespace devilution {
// Exported by scrollrt.cpp; the existing production header does not declare it.
bool DrawWorldViewForDiagnostics(const Surface &out, Point viewPosition);
}

namespace item_label_runtime_checks {
using namespace devilution;

struct Bounds {
	int left = std::numeric_limits<int>::max(), top = std::numeric_limits<int>::max();
	int right = -1, bottom = -1;
	bool valid() const { return right >= left && bottom >= top; }
	int width() const { return right - left + 1; }
	int height() const { return bottom - top + 1; }
	void include(int x, int y)
	{
		left = std::min(left, x); right = std::max(right, x);
		top = std::min(top, y); bottom = std::max(bottom, y);
	}
	bool overlaps(const Bounds &other) const
	{
		return valid() && other.valid() && left <= other.right && other.left <= right
		    && top <= other.bottom && other.top <= bottom;
	}
};

inline std::vector<uint8_t> Pixels(const Surface &out)
{
	std::vector<uint8_t> value(static_cast<size_t>(out.w()) * gnViewportHeight);
	for (int y = 0; y < gnViewportHeight; ++y)
		std::memcpy(value.data() + static_cast<size_t>(y) * out.w(), out.at(0, y), static_cast<size_t>(out.w()));
	return value;
}

inline void Restore(const Surface &out, const std::vector<uint8_t> &value)
{
	for (int y = 0; y < gnViewportHeight; ++y)
		std::memcpy(out.at(0, y), value.data() + static_cast<size_t>(y) * out.w(), static_cast<size_t>(out.w()));
}

inline Bounds DifferenceBounds(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, int width)
{
	Bounds result;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i] != b[i]) result.include(static_cast<int>(i % static_cast<size_t>(width)), static_cast<int>(i / static_cast<size_t>(width)));
	return result;
}

inline std::string ItemWitness()
{
	std::ostringstream result;
	result << GetLCGEngineState() << ':' << static_cast<unsigned>(ActiveItemCount);
	for (int y = 0; y < MAXDUNY; ++y)
		for (int x = 0; x < MAXDUNX; ++x) result << ',' << static_cast<int>(dItem[x][y]);
	for (uint8_t i = 0; i < ActiveItemCount; ++i) {
		const int slot = ActiveItems[i]; const Item &item = Items[slot];
		result << '|' << slot << ':' << static_cast<int>(item._itype) << ':' << item._iCurs << ':' << item._ivalue
		       << ':' << static_cast<int>(item._iMagical) << ':' << item._iIdentified << ':' << item._iName << ':' << item._iIName
		       << ':' << static_cast<unsigned>(item.position.x) << ':' << static_cast<unsigned>(item.position.y)
		       << ':' << static_cast<int>(item.AnimInfo.currentFrame) << ':' << static_cast<int>(item.AnimInfo.numberOfFrames)
		       << ':' << static_cast<int>(item.selectionRegion) << ':' << item._iAnimFlag;
	}
	return result.str();
}

inline uint64_t PickWitness(const Surface &out)
{
	uint64_t hash = 1469598103934665603ULL;
	const auto append = [&](uint32_t value) { hash ^= value; hash *= 1099511628211ULL; };
	for (int y = 0; y < gnViewportHeight; ++y) for (int x = 0; x < out.w(); ++x) {
		TownViewPickResult pick; const bool picked = PickTownViewDetailed({ x, y }, pick);
		append(picked ? 1U : 0U);
		append(std::bit_cast<uint32_t>(TownViewDepthAt({ x, y })));
		if (picked) {
			append(static_cast<uint32_t>(pick.tile.x)); append(static_cast<uint32_t>(pick.tile.y));
			append(static_cast<uint32_t>(pick.itemIndex)); append(static_cast<uint32_t>(pick.townerIndex));
			append(static_cast<uint32_t>(pick.playerIndex)); append(static_cast<uint32_t>(pick.objectIndex));
			append(static_cast<uint32_t>(pick.monsterIndex));
		}
	}
	return hash;
}

inline std::array<Bounds, 3> VisibleItemBounds(const Surface &out, const std::array<int, 3> &ids)
{
	std::array<Bounds, 3> result;
	for (int y = 0; y < gnViewportHeight; ++y) for (int x = 0; x < out.w(); ++x) {
		TownViewPickResult pick;
		if (!PickTownViewDetailed({ x, y }, pick)) continue;
		for (size_t i = 0; i < ids.size(); ++i) if (pick.itemIndex == ids[i]) result[i].include(x, y);
	}
	return result;
}

inline std::string ExpectedName(int id)
{
	const Item &item = Items[id];
	return item._itype == ItemType::Gold
	    ? FormatRuntime(_("{:s} gold"), FormatInteger(item._ivalue))
	    : std::string(item.getName().str());
}

// Owns only fixture-memory UI state. Item/map setup belongs to the new isolated
// diagnostic process, never to the habitual profile or a running human game.
struct UiRestore {
	bool labels = *GetOptions().Gameplay.showItemLabels, zoom = *GetOptions().Graphics.zoom;
	bool gpu = *GetOptions().Graphics.townViewGpuRendering, aa = *GetOptions().Graphics.townViewAntialiasing;
	bool inv = invflag, book = SpellbookFlag, character = CharFlag, dead = MyPlayerIsDead;
	bool headless = HeadlessMode; int pause = PauseMode;
	int width = gnScreenWidth, height = gnScreenHeight, viewport = gnViewportHeight;
	Point mouse = MousePosition, cursor = cursPosition, view = ViewPosition;
	int item = pcursitem; PlayerActionType action = LastPlayerAction;
	ControlTypes control = ControlMode; TalkID store = ActiveStore;
	~UiRestore()
	{
		GetOptions().Gameplay.showItemLabels.SetValue(labels); GetOptions().Graphics.zoom.SetValue(zoom);
		GetOptions().Graphics.townViewGpuRendering.SetValue(gpu); GetOptions().Graphics.townViewAntialiasing.SetValue(aa);
		invflag = inv; SpellbookFlag = book; CharFlag = character; MyPlayerIsDead = dead; HeadlessMode = headless;
		PauseMode = pause; MousePosition = mouse; cursPosition = cursor; ViewPosition = view; pcursitem = item;
		LastPlayerAction = action; ControlMode = control; ActiveStore = store;
		gnScreenWidth = width; gnScreenHeight = height; gnViewportHeight = viewport;
		CalculatePanelAreas(); CalcViewportGeometry();
		HighlightKeyPressed(false); ResetItemlabelHighlighted(); ClearUiOverlayRegions();
	}
};

template <class Initialize, class Check, class NativeWitness, class SavePng>
void Run(const std::filesystem::path &output, Initialize initialize, Check check, NativeWitness nativeWitness, SavePng savePng)
{
	initialize(); UiRestore restore;
	size_t checks = 0, worldFrames = 0, hardwareFrames = 0, captures = 0;
	const auto require = [&](bool condition, const char *message) { ++checks; check(condition, message); };
	require(!gmenu_is_active(), "labels isolated fixture starts without an active native menu");
	HeadlessMode = false; ControlMode = ControlTypes::KeyboardAndMouse;
	invflag = SpellbookFlag = CharFlag = MyPlayerIsDead = false; PauseMode = 0;
	ActiveStore = TalkID::None; LastPlayerAction = PlayerActionType::None;
	HighlightKeyPressed(false); GetOptions().Gameplay.showItemLabels.SetValue(true);
	GetOptions().Graphics.zoom.SetValue(false);
	LoadItemData(); InitItemGFX(); InitItems(); // Initialization only, before the measured witness.
	const auto cape = std::find_if(AllItemsList.begin(), AllItemsList.end(), [](const ItemData &item) { return item.iCurs == ICURS_CAPE; });
	require(cape != AllItemsList.end(), "retail item table supplies an original Cape");
	const auto capeId = static_cast<_item_indexes>(cape - AllItemsList.begin());
	Point base; bool found = false;
	for (int dy = -6; dy <= 6 && !found; ++dy) for (int dx = -6; dx <= 6 && !found; ++dx) {
		const Point candidate { MyPlayer->position.tile.x + dx, MyPlayer->position.tile.y + dy - 3 };
		bool clear = true;
		for (int i = 0; i < 3; ++i) {
			const Point tile { candidate.x + i, candidate.y - i };
			clear &= InDungeonBounds(tile) && IsTileNotSolid(tile)
			    && dMonster[tile.x][tile.y] == 0 && dPlayer[tile.x][tile.y] == 0 && dSpecial[tile.x][tile.y] == 0;
		}
		if (clear) { base = candidate; found = true; }
	}
	require(found, "retail town provides three real clear item cells without changing SOL");
	std::array<int, 3> ids;
	for (size_t i = 0; i < ids.size(); ++i) {
		Item item; InitializeItem(item, i == 2 ? IDI_GOLD : capeId);
		if (i == 1) item._iMagical = ITEM_QUALITY_MAGIC;
		if (i == 2) item._ivalue = 1234567;
		const WorldTilePosition tile { static_cast<uint8_t>(base.x + static_cast<int>(i)), static_cast<uint8_t>(base.y - static_cast<int>(i)) };
		ids[i] = PlaceItemInWorld(std::move(item), tile); Items[ids[i]].setNewAnimation(false);
		require(Items[ids[i]].AnimInfo.sprites.has_value() && Items[ids[i]].AnimInfo.isLastFrame(), "staged native item has actual retail CLX and completed drop animation");
	}
	ViewPosition = base;
	if (!IsTownViewActive()) ToggleTownView();
	SetTownViewCameraMode(TownCameraMode::FreeOrbit);
	const auto nativeState = nativeWitness(); const auto itemState = ItemWitness();
	bool overlapProved = false, panelProved = false;
	for (const bool useGpu : { false, true }) for (const bool aa : { false, true }) {
		gnScreenWidth = aa ? 960 : 640; gnScreenHeight = aa ? 540 : 480;
		CalculatePanelAreas(); CalcViewportGeometry();
		GetOptions().Graphics.townViewGpuRendering.SetValue(useGpu); GetOptions().Graphics.townViewAntialiasing.SetValue(aa);
		SetTownViewCameraPoseForDiagnostics({ 0.7853981634F, 0.5235987756F, aa ? 45.0F : 32.0F, {} });
		OwnedSurface storage(gnScreenWidth, gnScreenHeight + 2);
		require(SDL_SetPaletteColors(storage.surface->format->palette, logical_palette.data(), 0, 256) == 0, "apply retail palette to private label capture");
		for (int y = 0; y < storage.h(); ++y) std::memset(storage.at(0, y), 202, static_cast<size_t>(storage.w()));
		const Surface out = storage.subregionY(1, gnScreenHeight);
		require(DrawWorldViewForDiagnostics(out, ViewPosition), "real DrawGame/DrawTownView world path completes before label drawing"); ++worldFrames;
		require(IsTownViewActive() && !IsTownViewNativePose(), "caption case uses nonnative 3D, not retained original framing");
		const auto renderer = GetTownViewRendererState();
		if (useGpu) {
			require(renderer.requestedGpu && renderer.usedGpu && renderer.cpuRasterizedTriangles == 0 && !GetTownGpuStatus().warp,
			    "label hardware control is actual GPU without WARP or CPU3D substitution"); ++hardwareFrames;
		} else require(!renderer.requestedGpu && !renderer.usedGpu, "label CPU control uses explicitly selected CPU renderer");
		const auto world = Pixels(out); const auto picks = PickWitness(out);
		const auto bounds = VisibleItemBounds(out, ids); const auto anchors = GetTownViewItemLabelAnchors();
		std::array<Bounds, 3> captions;
		for (size_t i = 0; i < ids.size(); ++i) {
			require(bounds[i].valid(), "core caption control has real visible item pixels before UI");
			const auto anchor = std::find_if(anchors.begin(), anchors.end(), [&](const auto &value) { return value.itemIndex == ids[i]; });
			require(anchor != anchors.end(), "visible Cape/Cape/gold each supply a current 3D label anchor");
			require(anchor->tile == Items[ids[i]].position && anchor->position.x >= 0 && anchor->position.x < out.w()
			    && anchor->position.y >= 0 && anchor->position.y < gnViewportHeight,
			    "3D anchor binds current native item tile and logical on-screen coordinates");
		}
		const auto drawLabels = [&](int selected, Point mouse) {
			Restore(out, world); MousePosition = mouse; pcursitem = selected; ResetItemlabelHighlighted();
			BeginUiOverlayRegions(out); DrawItemNameLabels(out); return Pixels(out);
		};
		const auto unselected = drawLabels(-1, { -1, -1 });
		require(unselected != world, "real 3D frame displays labels through DrawItemNameLabels");
		for (size_t i = 0; i < ids.size(); ++i) {
			const auto selected = drawLabels(ids[i], { -1, -1 });
			captions[i] = DifferenceBounds(unselected, selected, out.w());
			const auto &rect = captions[i]; const std::string text = ExpectedName(ids[i]);
			const int height = IsSmallFontTall() ? 18 : 13;
			require(rect.valid() && rect.width() == GetLineWidth(text) + 4 && rect.height() == height,
			    "observed selected caption has the native formatter width and actual font-dependent height");
			OwnedSurface expected(rect.width(), rect.height());
			for (int y = 0; y < expected.h(); ++y) std::memset(expected.at(0, y), PAL8_BLUE + 6, static_cast<size_t>(expected.w()));
			DrawString(expected, text, { { 2, IsSmallFontTall() ? 1 : -1 }, { rect.width(), rect.height() } }, { .flags = Items[ids[i]].getTextColor() });
			bool exactText = true;
			for (int y = 0; y < rect.height(); ++y) for (int x = 0; x < rect.width(); ++x)
				exactText &= selected[static_cast<size_t>(rect.top + y) * out.w() + rect.left + x] == expected[{ x, y }];
			require(exactText, "Cape, magic blue Cape and localized formatted gold retain exact native glyph/color pixels");
			const Point hover { rect.left + rect.width() / 2, rect.top + rect.height() / 2 };
			drawLabels(-1, hover);
			require(IsItemLabelHighlighted() && pcursitem == ids[i] && cursPosition == Items[ids[i]].position,
			    "caption hover selects the real world item ID/tile despite shifted overlap layout");
			CheckCursMove(); require(pcursitem == ids[i] && cursPosition == Items[ids[i]].position, "label priority survives the productive CheckCursMove picking route");
			ResetItemlabelHighlighted(); pcursitem = -1; MousePosition = { -1, -1 };
			require(SelectProjectedItemLabelAt(hover) && pcursitem == ids[i] && cursPosition == Items[ids[i]].position,
			    "fresh pointer hit selects final shifted caption independently of last-frame highlight");
			ResetItemlabelHighlighted(); pcursitem = -1;
			require(!SelectProjectedItemLabelAt({ -1, -1 }) && !IsItemLabelHighlighted(), "pointer outside final captions does not reuse an old label hover");
			PauseMode = 1; drawLabels(-1, hover); require(!IsItemLabelHighlighted() && pcursitem == -1, "paused caption refuses mouse selection"); PauseMode = 0;
			LastPlayerAction = PlayerActionType::Walk; drawLabels(-1, hover); require(!IsItemLabelHighlighted() && pcursitem == -1, "active player action refuses caption selection"); LastPlayerAction = PlayerActionType::None;
			MyPlayerIsDead = true; drawLabels(-1, hover); require(!IsItemLabelHighlighted() && pcursitem == -1, "dead player caption refuses selection"); MyPlayerIsDead = false;
			SpellbookFlag = true;
			const auto panel = GetRightPanel();
			const Point panelHover { std::max(rect.left, panel.position.x), std::max(rect.top, panel.position.y) };
			if (panel.contains(panelHover) && panelHover.x <= rect.right && panelHover.y <= rect.bottom) {
				drawLabels(-1, panelHover); require(!IsItemLabelHighlighted() && pcursitem == -1, "Spellbook excludes a mouse point actually inside the caption and right panel"); panelProved = true;
			}
			SpellbookFlag = false;
		}
		for (size_t i = 0; i < ids.size(); ++i) for (size_t j = i + 1; j < ids.size(); ++j) {
			require(!captions[i].overlaps(captions[j]), "actual caption backgrounds are separated after common overlap resolution");
			const auto a = std::find_if(anchors.begin(), anchors.end(), [&](const auto &value) { return value.itemIndex == ids[i]; });
			const auto b = std::find_if(anchors.begin(), anchors.end(), [&](const auto &value) { return value.itemIndex == ids[j]; });
			if (std::abs(a->position.y - b->position.y) < (IsSmallFontTall() ? 20 : 15) && std::abs(a->position.x - b->position.x) < (captions[i].width() + captions[j].width()) / 2 + 4) overlapProved = true;
		}
		GetOptions().Gameplay.showItemLabels.SetValue(false);
		require(drawLabels(-1, { -1, -1 }) == world && !IsItemLabelHighlighted(), "disabled XOR clears captions and previous highlight on the very next UI pass");
		HighlightKeyPressed(true); require(drawLabels(-1, { -1, -1 }) != world, "highlight key displays captions when persisted option is disabled");
		GetOptions().Gameplay.showItemLabels.SetValue(true); require(drawLabels(-1, { -1, -1 }) == world, "highlight key XOR suppresses captions when persisted option is enabled"); HighlightKeyPressed(false);
		require(PickWitness(out) == picks, "label UI/hover/gates never modify published 3D depth or picking ownership");
		drawLabels(-1, { -1, -1 });
		const auto prefix = std::string(useGpu ? "gpu" : "cpu") + (aa ? "-aa" : "-plain");
		savePng(out.subregionY(0, gnViewportHeight), output / (prefix + "-labels.png")); ++captures;
		GetOptions().Graphics.zoom.SetValue(true); gnScreenWidth += 16;
		require(GetTownViewItemLabelAnchors().empty(), "unredrawn logical resize/zoom refuses stale prior 3D anchors");
		require(!SelectProjectedItemLabelAt({ captions[0].left, captions[0].top }), "resize refuses cached prior-frame final caption rectangles");
		GetOptions().Graphics.zoom.SetValue(false); gnScreenWidth -= 16;
		SetTownViewCameraPoseForDiagnostics({ 0.9F, 0.5235987756F, 32.0F, {} });
		require(GetTownViewItemLabelAnchors().empty() && !SelectProjectedItemLabelAt({ captions[0].left, captions[0].top }), "camera revision refuses old projected anchors and final caption rectangles before redraw");
		ToggleTownView(); require(GetTownViewItemLabelAnchors().empty(), "F4 suspension immediately invalidates prior caption anchors");
		require(DrawWorldViewForDiagnostics(out, ViewPosition), "F4 DrawGame renders the actual native world"); ++worldFrames;
		DrawItemNameLabels(out); const auto f4 = Pixels(out);
		OwnedSurface referenceStorage(gnScreenWidth, gnScreenHeight);
		const Surface reference = referenceStorage;
		require(DrawNativeTownViewReference(reference, ViewPosition), "independent retained native reference draws without 3D preparation"); ++worldFrames;
		DrawItemNameLabels(reference);
		require(Pixels(reference) == f4, "F4 caption/world output remains byte-exact with intact independent native comparison");
		ToggleTownView(); require(GetTownViewItemLabelAnchors().empty(), "3D resume cannot reuse a previous native/3D caption epoch");
		require(nativeWitness() == nativeState && ItemWitness() == itemState, "all label cases preserve native map/SOL/RNG/actor/item data after initialization");
		bool guards = true; for (int x = 0; x < storage.w(); ++x) guards &= storage[{ x, 0 }] == 202 && storage[{ x, storage.h() - 1 }] == 202;
		require(guards, "world and caption drawing preserve allocation guard rows");
	}
	// Alt preserves eligible native labels even when a body is fully occluded.
	// Finite camera search only: never clear picks, insert fake geometry, alter SOL
	// or call AddItemToLabelQueue to manufacture this 3D control.
	std::array<bool, 2> occlusionProved {};
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	gnScreenWidth = 640; gnScreenHeight = 480; CalculatePanelAreas(); CalcViewportGeometry();
	for (const bool useGpu : { false, true }) {
		bool observed = false; GetOptions().Graphics.townViewGpuRendering.SetValue(useGpu);
		for (const float yaw : { -0.7853981634F, 2.3561944902F, 0.0F, 1.5707963268F }) {
			SetTownViewCameraPoseForDiagnostics({ yaw, 0.12F, 32.0F, {} });
			OwnedSurface storage(gnScreenWidth, gnScreenHeight); const Surface out = storage;
			require(DrawWorldViewForDiagnostics(out, ViewPosition), "finite occlusion control uses actual 3D world publication"); ++worldFrames;
			if (useGpu) { const auto renderer = GetTownViewRendererState(); require(renderer.requestedGpu && renderer.usedGpu && renderer.cpuRasterizedTriangles == 0 && !GetTownGpuStatus().warp, "occlusion control stays on requested hardware without WARP"); ++hardwareFrames; }
			const auto body = VisibleItemBounds(out, ids); const auto anchors = GetTownViewItemLabelAnchors();
			for (size_t i = 0; i < ids.size(); ++i) {
				const auto anchor = std::find_if(anchors.begin(), anchors.end(), [&](const auto &value) { return value.itemIndex == ids[i]; });
				if (body[i].valid() || anchor == anchors.end() || anchor->position.x < 128 || anchor->position.x >= out.w() - 128
				    || anchor->position.y < 32 || anchor->position.y >= gnViewportHeight - 32) continue;
				const auto world = Pixels(out); Restore(out, world); pcursitem = -1; MousePosition = { -1, -1 };
				DrawItemNameLabels(out); const auto unselected = Pixels(out); Restore(out, world); pcursitem = ids[i]; DrawItemNameLabels(out);
				const auto rect = DifferenceBounds(unselected, Pixels(out), out.w());
				require(rect.valid(), "eligible on-screen item with no published body pixels still has a rendered caption");
				const Point hover { rect.left + rect.width() / 2, rect.top + rect.height() / 2 };
				ResetItemlabelHighlighted(); pcursitem = -1;
				require(SelectProjectedItemLabelAt(hover) && pcursitem == ids[i] && cursPosition == Items[ids[i]].position, "occluded item's final caption selects native item without requiring a body pick or sky depth");
				savePng(out.subregionY(0, gnViewportHeight), output / (std::string(useGpu ? "gpu" : "cpu") + "-occluded-label.png")); ++captures;
				observed = true; break;
			}
			if (observed) break;
		}
		// Absence of a suitable occluded item in these bounded poses is a recorded
		// coverage gap, not manufactured by clearing the authoritative pick buffer.
		occlusionProved[useGpu ? 1 : 0] = observed;
		require(nativeWitness() == nativeState && ItemWitness() == itemState, "occlusion label controls preserve native map/SOL/RNG/actor/item data");
	}
	require(overlapProved, "at least one real published caption pair exercised pre-layout overlap, not only isolated labels");
	require(panelProved, "at least one actual caption/panel intersection exercised Spellbook input exclusion");
	std::ofstream receipt(output / "item-label-checks.json");
	receipt << "{\"format\":\"d3d.private-item-label-runtime-checks-r1\",\"status\":\"PASS\",\"completed\":true,\"checks\":" << checks
	        << ",\"worldFrames\":" << worldFrames << ",\"hardwareFrames\":" << hardwareFrames << ",\"captures\":" << captures
	        << ",\"actualWorldAndLabels\":true,\"nativeComparisonIntact\":true,\"warpAllowed\":false,\"overlapProved\":true,\"spellbookCaptionIntersectionProved\":true"
	        << ",\"occludedItemCpuProved\":" << (occlusionProved[0] ? "true" : "false") << ",\"occludedItemGpuProved\":" << (occlusionProved[1] ? "true" : "false")
	        << ",\"firstPersonCaptureTested\":false,\"gameplayClaim\":false,\"fpsClaim\":false,\"fullHudPresentTested\":false,\"luaScriptExecuted\":false,\"installed\":false}\n";
	require(receipt.good(), "write private label checks only after every actual case returned");
	FreeItemGFX();
}

// Optional companion invoked by the principal's existing live-handler/seam
// adapter, in a separately initialized FPP case BEFORE FreeItemGFX. publish()
// must perform the real DrawWorld path for the same unchanged FPP pose; setCapture
// must drive/sync the existing native services, never substitute a test boolean.
// No PASS is emitted merely because a callback exists: each state is observed.
template <class Publish, class SetCapture, class Check>
void RunCapturedPointerPolicyChecks(const Surface &out, int id, Publish publish, SetCapture setCapture, Check check)
{
	check(GetTownViewCameraMode() == TownCameraMode::FirstPerson, "label capture companion uses actual FPP mode");
	setCapture(false); check(!IsTownFirstPersonInputCaptured(), "native capture adapter proves released FPP before label control");
	publish(); const auto world = Pixels(out); MousePosition = { -1, -1 }; pcursitem = -1;
	DrawItemNameLabels(out); const auto unselected = Pixels(out);
	Restore(out, world); pcursitem = id; DrawItemNameLabels(out);
	const auto rect = DifferenceBounds(unselected, Pixels(out), out.w());
	check(rect.valid(), "FPP companion has a real current caption before testing capture");
	const Point hover { rect.left + rect.width() / 2, rect.top + rect.height() / 2 };
	MousePosition = hover; pcursitem = -1; ResetItemlabelHighlighted();
	check(SelectProjectedItemLabelAt(hover) && pcursitem == id && cursPosition == Items[id].position, "released FPP caption selects the actual item");
	setCapture(true); check(IsTownFirstPersonInputCaptured(), "native input seam actually reports captured FPP");
	publish(); pcursitem = -1; MousePosition = hover; DrawItemNameLabels(out);
	check(!SelectProjectedItemLabelAt(hover) && !IsItemLabelHighlighted() && pcursitem == -1, "captured FPP refuses absolute caption override and keeps crosshair picking authoritative");
	setCapture(false); check(!IsTownFirstPersonInputCaptured(), "native adapter actually releases FPP capture");
	publish(); pcursitem = -1; MousePosition = hover; DrawItemNameLabels(out);
	check(SelectProjectedItemLabelAt(hover) && pcursitem == id && cursPosition == Items[id].position, "fresh released FPP publication restores caption selection");
}
} // namespace item_label_runtime_checks
