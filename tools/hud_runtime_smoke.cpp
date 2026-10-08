// Finite native HUD/input fixtures. No game loop, save, world or normal profile.
// Captures use real UI assets over a synthetic background, not game screenshots.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "control/control.hpp"
#include "control/control_chat.hpp"
#include "control/d3d_hud.hpp"
#include "control/d3d_hud_layout.hpp"
#include "controls/control_mode.hpp"
#include "controls/game_controls.h"
#include "controls/modifier_hints.h"
#include "cursor.h"
#include "cursor_defs.hpp"
#include "diablo.h"
#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "engine/render/d3d_ui_layout.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/text_render.hpp"
#include "engine/sound.h"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "options.h"
#include "panels/quest_log.hpp"
#include "panels/spell_list.hpp"
#include "player.h"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "qol/xpbar.h"
#include "storm/storm_net.hpp"
#include "stores.h"
#include "tables/itemdat.h"
#include "tables/playerdat.hpp"
#include "tables/questdat.hpp"
#include "tables/spelldat.h"
#include "utils/paths.h"
#include "utils/ui_fwd.h"

namespace {
using namespace devilution;

size_t Checks = 0;
std::vector<std::string> Captures;

void Check(bool condition, std::string_view message)
{
	++Checks;
	std::cout << (condition ? "PASS " : "FAIL ") << message << '\n';
	if (!condition)
		throw std::runtime_error(std::string(message));
}

void LoadSelectedArchive()
{
	std::filesystem::path selected;
	for (const char *name : { "DIABDAT.MPQ", "diabdat.mpq", "spawn.mpq", "SPAWN.MPQ" }) {
		const auto candidate = std::filesystem::absolute(paths::BasePath()) / name;
		if (std::filesystem::is_regular_file(candidate)) {
			selected = candidate;
			break;
		}
	}
	Check(!selected.empty(), "explicit data directory contains the game archive");
	gbIsSpawn = selected.filename() == "spawn.mpq" || selected.filename() == "SPAWN.MPQ";
	auto archive = MpqArchive::Open(selected.string().c_str());
	Check(archive.has_value(), "open explicitly selected game archive");
	MpqArchives.insert_or_assign(MainMpqPriority, std::move(*archive));
	const auto ref = FindAsset("ctrlpan\\panel8.cel");
	Check(ref.ok() && ref.archive == &MpqArchives.at(MainMpqPriority), "native panel art comes from the selected archive");
}

void ClosePanels()
{
	invflag = false;
	SpellbookFlag = false;
	CharFlag = false;
	QuestLogIsOpen = false;
	SpellSelectFlag = false;
	IsStashOpen = false;
	IsVisualStoreOpen = false;
	ActiveStore = TalkID::None;
	ResetMainPanelButtons();
}

void InitializeFixture()
{
	HeadlessMode = true;
	gbIsHellfire = false;
	gbIsMultiplayer = false;
	gbSoundOn = false;
	gbMusicOn = false;
	leveltype = DTYPE_TOWN;
	currlevel = 0;
	ControlMode = ControlTypes::KeyboardAndMouse;
	LoadCoreArchives();
	LoadSelectedArchive();
	OverridePaths.emplace_back(paths::PrefPath());
	InitKeymapActions();
	// Button release closes the native menu, which calls SaveOptions(). Its
	// backing Ini must exist even though this fixture never starts a game.
	LoadOptions();
	GetOptions().Keymapper.CommitActions();
	GetOptions().Graphics.hardwareCursor.SetValue(false);
	SaveOptions();
	Check(std::filesystem::is_regular_file(std::filesystem::path(paths::ConfigPath()) / "diablo.ini"),
	    "native options load/save is initialized in the private fixture profile");
	LoadPlayerDataFiles();
	LoadItemData();
	LoadSpellData();
	LoadQuestData();
	Players.resize(1);
	MyPlayerId = 0;
	MyPlayer = InspectPlayer = &Players[0];
	CreatePlayer(*MyPlayer, HeroClass::Warrior);
	MyPlayer->setCharacterLevel(25);
	MyPlayer->_pmode = PM_STAND;
	MyPlayerIsDead = false;
	InitCursor();
	Check(SNetInitializeProvider(SELCONN_LOOPBACK, nullptr), "initialize the isolated loopback provider");
	InitStores();
	GetOptions().Gameplay.experienceBar.SetValue(false);
	GetOptions().Gameplay.showHealthValues.SetValue(true);
	GetOptions().Gameplay.showManaValues.SetValue(true);
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	CalculatePanelAreas();
	// UI sprites, palette and fonts are required; no world or game is started.
	HeadlessMode = false;
	LoadPaletteAndInitBlending("levels\\towndata\\town.pal");
	const auto initialized = InitMainPanel();
	Check(initialized.has_value(), initialized ? "initialize native HUD art" : initialized.error());
	ClosePanels();
	for (int i = 0; i < MaxBeltItems; ++i) {
		InitializeItem(MyPlayer->SpdList[i], i % 2 == 0 ? IDI_HEAL : IDI_MANA);
		MyPlayer->SpdList[i].updateRequiredStatsCacheForPlayer(*MyPlayer);
	}
	MyPlayer->_pMaxHP = MyPlayer->_pMaxHPBase = 100 << 6;
	MyPlayer->_pMaxMana = MyPlayer->_pMaxManaBase = 80 << 6;
	MyPlayer->_pMemSpells = GetSpellBitmask(SpellID::Healing) | GetSpellBitmask(SpellID::Firebolt);
	MyPlayer->_pSplLvl[static_cast<unsigned>(SpellID::Healing)] = 1;
	MyPlayer->_pSplLvl[static_cast<unsigned>(SpellID::Firebolt)] = 1;
	MyPlayer->_pSplHotKey[0] = GetPlayerStartingLoadoutForClass(MyPlayer->_pClass).skill;
	MyPlayer->_pSplTHotKey[0] = SpellType::Skill;
	MyPlayer->_pSplHotKey[1] = SpellID::Healing;
	MyPlayer->_pSplTHotKey[1] = SpellType::Spell;
	MyPlayer->_pSplHotKey[2] = SpellID::Firebolt;
	MyPlayer->_pSplTHotKey[2] = SpellType::Spell;
	MyPlayer->_pSplHotKey[3] = SpellID::Invalid;
	MyPlayer->_pSplTHotKey[3] = SpellType::Invalid;
	MyPlayer->_pRSpell = SpellID::Healing;
	MyPlayer->_pRSplType = SpellType::Spell;
	MyPlayer->HoldItem.clear();
	// Match entering a real level: pcursitem otherwise retains its static zero
	// and DrawInfoBox replaces HUD information with the unrelated world item.
	InitLevelCursor();
	Check(pcursitem == -1 && pcursmonst == -1 && ObjectUnderCursor == nullptr && PlayerUnderCursor == nullptr,
	    "native level cursor initialization clears unrelated world hover references");
	ReloadD3dUiLayout();
}

void ClickPanelButton(int button)
{
	MousePosition = GetD3dHudPanelButtonRect(button).Center();
	CheckPanelInfo();
	Check(MainPanelFlag && !InfoString.empty(), "button hover reaches the native information handler");
	CheckMainPanelButton();
	Check(MainPanelButtonDown, "native button press is recognized at its shared rectangle");
	CheckMainPanelButtonUp();
	Check(!MainPanelButtonDown, "native button release clears the pressed state");
}

void CheckNativeInput()
{
	Check(IsD3dHudEnabled(), "responsive HUD is active for keyboard and mouse");
	ChatFlag = true;
	const Rectangle legacyLevel = GetD3dHudLevelButtonRect();
	Check(legacyLevel.position == GetMainPanel().position + Displacement { 40, -39 } && legacyLevel.size == Size { 41, 22 },
	    "chat fallback preserves exact native Level Up drawing and click bounds");
	for (int i = 0; i < MaxBeltItems; ++i) {
		Rectangle native = InvRect[SLOTXY_BELT_FIRST + i];
		native.position += Displacement { GetMainPanel().position.x, GetMainPanel().position.y };
		const Rectangle fallback = GetD3dHudBeltSlotRect(i);
		Check(fallback.position == native.position && fallback.size == native.size,
		    "legacy belt fallback preserves exact native cell bounds");
	}
	ChatFlag = false;
	ClosePanels();
	ClickPanelButton(0);
	Check(CharFlag, "Character button opens the native character panel");
	ClickPanelButton(0);
	Check(!CharFlag, "Character button closes the native character panel");
	ClickPanelButton(4);
	Check(invflag && !SpellbookFlag, "Inventory button opens the native inventory");
	ClickPanelButton(5);
	Check(SpellbookFlag && !invflag, "Spellbook button preserves native mutual exclusion");
	ClickPanelButton(5);
	Check(!SpellbookFlag, "Spellbook button closes its native panel");
	for (int i = 0; i < 6; ++i) {
		MousePosition = GetD3dHudPanelButtonRect(i).Center();
		CheckPanelInfo();
		Check(MainPanelFlag && !InfoString.empty(), "all six single-player buttons preserve native hover text");
	}
	for (int i = 0; i < MaxBeltItems; ++i) {
		MousePosition = GetD3dHudBeltSlotRect(i).Center();
		Check(CheckInvHLight() == INVITEM_BELT_FIRST + i, "shared belt cell reaches the correct native item");
		CheckPanelInfo();
		Check(pcursinvitem == INVITEM_BELT_FIRST + i && !InfoString.empty(), "belt hover preserves native item information");
	}
	const Item original = MyPlayer->SpdList[7];
	MousePosition = GetD3dHudBeltSlotRect(7).Center();
	CheckInvItem(false, false);
	Check(MyPlayer->SpdList[7].isEmpty() && MyPlayer->HoldItem.IDidx == original.IDidx,
	    "native cut lifts the eighth belt item");
	CheckInvItem(false, false);
	Check(MyPlayer->HoldItem.isEmpty() && MyPlayer->SpdList[7].IDidx == original.IDidx,
	    "native paste restores the eighth belt item");
	ToggleSpell(0);
	Check(MyPlayer->_pRSpell == MyPlayer->_pSplHotKey[0] && MyPlayer->_pRSplType == SpellType::Skill,
	    "existing native spell hotkey preserves the selected skill");
	ToggleSpell(1);
	Check(MyPlayer->_pRSpell == SpellID::Healing && MyPlayer->_pRSplType == SpellType::Spell,
	    "existing native spell hotkey preserves the selected spell");
	MousePosition = GetD3dHudSpellRect().Center();
	CheckPanelInfo();
	Check(MainPanelFlag && !InfoString.empty(), "selected spell preserves native hover text");
	const auto originalModifiers = SDL_GetModState();
	SDL_SetModState(SDL_KMOD_SHIFT);
	CheckMainPanelButton();
	SDL_SetModState(originalModifiers);
	Check(MyPlayer->_pRSpell == SpellID::Invalid && MyPlayer->_pRSplType == SpellType::Invalid && !SpellSelectFlag,
	    "Shift-click on the original spell button clears selection without opening the spell list");
	ToggleSpell(1);
	Check(MyPlayer->_pRSpell == SpellID::Healing && MyPlayer->_pSplHotKey[1] == SpellID::Healing,
	    "Shift-click preserves native hotkey assignments and the selected spell can be restored");
	ClosePanels();
}

std::vector<uint8_t> Pixels(const Surface &out, Rectangle rect)
{
	std::vector<uint8_t> pixels(static_cast<size_t>(rect.size.width) * rect.size.height);
	for (int y = 0; y < rect.size.height; ++y)
		std::memcpy(pixels.data() + static_cast<size_t>(y) * rect.size.width,
		    out.at(rect.position.x, rect.position.y + y), rect.size.width);
	return pixels;
}

void RenderHud(const Surface &out)
{
	const uint32_t random = GetLCGEngineState();
	for (int y = 0; y < out.h(); ++y)
		std::memset(out.at(0, y), PAL16_GRAY + 13 + ((y / 24) % 2), out.w());
	DrawString(out, "TECHNICAL HUD FIXTURE - SYNTHETIC BACKGROUND - NOT GAMEPLAY", { 16, 16 }, { .flags = UiFlags::ColorWhite });
	UpdateLifeManaPercent();
	// Match DrawView: these native controls precede the floating HUD surfaces.
	DrawDurIcon(out);
	DrawLevelButton(out);
	DrawMainPanel(out);
	DrawLifeFlaskUpper(out);
	DrawManaFlaskUpper(out);
	DrawLifeFlaskLower(out, true);
	DrawManaFlaskLower(out, true);
	DrawMainPanelButtons(out);
	DrawInvBelt(out);
	DrawSpell(out);
	if (IsD3dHudEnabled()) {
		DrawD3dHudValues(out);
	} else {
		const Point panel = GetMainPanel().position;
		if (*GetOptions().Gameplay.showHealthValues)
			DrawFlaskValues(out, panel + Displacement { 134, 28 }, MyPlayer->_pHitPoints >> 6, MyPlayer->_pMaxHP >> 6);
		if (*GetOptions().Gameplay.showManaValues) {
			const bool noMana = HasAnyOf(InspectPlayer->_pIFlags, ItemSpecialEffect::NoMana);
			DrawFlaskValues(out, panel + Displacement { GetMainPanel().size.width - 138, 28 },
			    noMana || MyPlayer->hasNoMana() ? 0 : MyPlayer->_pMana >> 6,
			    noMana ? 0 : MyPlayer->_pMaxMana >> 6);
		}
	}
	MousePosition = GetD3dHudBeltSlotRect(0).Center();
	CheckPanelInfo();
	DrawInfoBox(out);
	Check(GetLCGEngineState() == random, "native HUD rendering and hover preserve simulation RNG");
}

void Capture(const Surface &out, const std::filesystem::path &directory, const std::string &name)
{
	Check(SDL_SetPaletteColors(out.surface->format->palette, logical_palette.data(), 0, 256) == 0, "apply actual town palette to technical capture");
	Check(SDL_SaveBMP(out.surface, (directory / name).string().c_str()) == 0, "save technical HUD capture");
	Captures.push_back(name);
}

bool Overlaps(Rectangle a, Rectangle b)
{
	return a.position.x < b.position.x + b.size.width && b.position.x < a.position.x + a.size.width
	    && a.position.y < b.position.y + b.size.height && b.position.y < a.position.y + a.size.height;
}

void CheckAuxiliaryRect(Rectangle rect)
{
	Check(rect.position.x >= 0 && rect.position.y >= 0
	        && rect.position.x + rect.size.width <= gnScreenWidth
	        && rect.position.y + rect.size.height <= gnScreenHeight,
	    "native auxiliary control stays inside the screen, including compact layouts");
	bool clear = true;
	for (const Rectangle occupied : { GetD3dHudOrbRect(false), GetD3dHudOrbRect(true), GetD3dHudBeltRect(),
	         GetD3dHudSpellRect(), GetD3dHudInfoRect(), GetD3dHudValueRect(false), GetD3dHudValueRect(true) })
		clear = clear && !Overlaps(rect, occupied);
	for (int i = 0; i < (IsChatAvailable() ? 8 : 6); ++i)
		clear = clear && !Overlaps(rect, GetD3dHudPanelButtonRect(i));
	Check(clear, "native auxiliary control has no overlap with the visible default HUD");
}

void CheckAuxiliaryControls(const Surface &out, const std::filesystem::path &directory)
{
	ClosePanels();
	const int originalPoints = MyPlayer->_pStatPts;
	const bool originalDown = LevelButtonDown;
	const Point originalMouse = MousePosition;
	const std::array<int, 4> slots { INVLOC_HEAD, INVLOC_CHEST, INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT };
	std::array<Item, 4> originalEquipment;
	for (size_t i = 0; i < slots.size(); ++i) {
		originalEquipment[i] = MyPlayer->InvBody[slots[i]];
		MyPlayer->InvBody[slots[i]].clear();
	}
	MyPlayer->_pStatPts = 0;
	LevelButtonDown = false;
	FillRect(out, 0, 0, out.w(), out.h(), PAL16_GRAY + 13);
	const Rectangle button = GetD3dHudLevelButtonRect();
	const Rectangle label = GetD3dHudLevelLabelRect();
	CheckAuxiliaryRect(button);
	CheckAuxiliaryRect(label);
	const auto emptyButton = Pixels(out, button);
	const auto emptyLabel = Pixels(out, label);
	DrawLevelButton(out);
	Check(Pixels(out, button) == emptyButton, "Level Up stays hidden without available stat points");
	MyPlayer->_pStatPts = 1;
	DrawLevelButton(out);
	Check(Pixels(out, button) != emptyButton && Pixels(out, label) != emptyLabel,
	    "Level Up draws the actual native button and translated label at their shared rectangles");
	MousePosition = button.Center();
	Check(!IsPointOnD3dHud(MousePosition), "Level Up center reaches the native world-branch button handler");
	CheckLevelButton();
	Check(LevelButtonDown, "native Level Up press recognizes the drawn button center");
	CheckLevelButtonUp();
	Check(CharFlag && !LevelButtonDown, "native Level Up release opens Character and clears the pressed state");
	ClosePanels();
	SpellSelectFlag = true;
	CheckLevelButton();
	Check(!LevelButtonDown, "native spell selection gate still suppresses Level Up presses");
	SpellSelectFlag = false;
	MyPlayer->_pStatPts = 0;
	RenderHud(out);
	const Rectangle durability = GetD3dHudDurabilityRect();
	CheckAuxiliaryRect(durability);
	Check(!Overlaps(durability, button) && !Overlaps(durability, label),
	    "durability warnings and Level Up reserve separate readable regions");
	std::array<Rectangle, 4> cells;
	std::array<std::vector<uint8_t>, 4> emptyCells;
	for (int i = 0; i < 4; ++i) {
		const int sourceX = 120 - i * 40;
		const int left = sourceX * durability.size.width / 152;
		const int right = (sourceX + 32) * durability.size.width / 152;
		cells[i] = { durability.position + Displacement { left, 0 }, { right - left, durability.size.height } };
		emptyCells[i] = Pixels(out, cells[i]);
	}
	constexpr std::array<ItemType, 4> types { ItemType::Helm, ItemType::LightArmor, ItemType::Sword, ItemType::Shield };
	for (int count = 1; count <= 4; ++count) {
		Item &item = MyPlayer->InvBody[slots[count - 1]];
		item._itype = types[count - 1];
		item._iDurability = 2;
		RenderHud(out);
		for (int i = 0; i < 4; ++i)
			Check((Pixels(out, cells[i]) != emptyCells[i]) == (i < count),
			    "one through four native durability warnings remain visible in native packing order after HUD rendering");
	}
	MyPlayer->_pStatPts = 1;
	RenderHud(out);
	Capture(out, directory, "hud-" + std::to_string(gnScreenWidth) + "x" + std::to_string(gnScreenHeight) + "-native-auxiliary.bmp");
	MyPlayer->_pStatPts = 0;
	const auto red = Pixels(out, durability);
	for (int slot : slots) MyPlayer->InvBody[slot]._iDurability = 5;
	RenderHud(out);
	const auto gold = Pixels(out, durability);
	Check(gold != red, "native durability gold and red thresholds retain distinct sprite pixels");
	for (int slot : slots) MyPlayer->InvBody[slot]._iDurability = 3;
	RenderHud(out);
	Check(Pixels(out, durability) != red && Pixels(out, durability) != gold,
	    "native durability partition between gold and red remains visible");
	for (int slot : slots) MyPlayer->InvBody[slot]._iDurability = 6;
	RenderHud(out);
	for (int i = 0; i < 4; ++i)
		Check(Pixels(out, cells[i]) == emptyCells[i], "native durability warning disappears above the original threshold");
	for (size_t i = 0; i < slots.size(); ++i) MyPlayer->InvBody[slots[i]] = originalEquipment[i];
	MyPlayer->_pStatPts = originalPoints;
	LevelButtonDown = originalDown;
	MousePosition = originalMouse;
}

void CheckFrameComposition(const Surface &out)
{
	const Rectangle frame = GetD3dHudFrameRect();
	const Rectangle belt = GetD3dHudBeltRect();
	const Rectangle info = GetD3dHudInfoRect();
	const Rectangle spell = GetD3dHudSpellRect();
	const Rectangle character = GetD3dHudPanelButtonRect(0);
	const Rectangle inventory = GetD3dHudPanelButtonRect(4);
	const Rectangle book = GetD3dHudPanelButtonRect(5);
	Check(frame.size.width >= 640 * gnScreenHeight / 480 - 1 && frame.size.width <= 640 * gnScreenHeight / 480 + 1
	        && frame.size.height >= 104 * gnScreenHeight / 480 - 1 && frame.size.height <= 104 * gnScreenHeight / 480 + 1,
	    "cohesive native frame retains the 640 by 104 composition on the 480-high canvas");
	Check(belt.position.y + belt.size.height <= info.position.y,
	    "all eight belt cells sit above the information panel");
	Check(spell.position.x >= inventory.position.x && spell.position.x + spell.size.width <= inventory.position.x + inventory.size.width
	        && spell.position.y >= book.position.y + book.size.height,
	    "current spell sits below INV and SPELLS inside the right column");
	Check(character.position.x + character.size.width < GetD3dHudOrbRect(false).position.x
	        && inventory.position.x > GetD3dHudOrbRect(true).position.x + GetD3dHudOrbRect(true).size.width,
	    "native text button columns flank the complete life and mana globes");
	const Point stone { frame.Center().x, frame.position.y + frame.size.height - 2 };
	Check(IsPointOnD3dHud(stone), "native stone frame blocks world input between the individual controls");
	for (bool mana : { false, true }) {
		const Rectangle value = GetD3dHudValueRect(mana);
		Check(value.position.x >= 0 && value.position.y >= 0
		        && value.position.x + value.size.width <= gnScreenWidth
		        && value.position.y + value.size.height <= gnScreenHeight,
		    "native life and mana values remain completely visible below the full sculptures");
	}
	FillRect(out, 0, 0, out.w(), out.h(), PAL16_GRAY + 13);
	const auto empty = Pixels(out, frame);
	DrawMainPanel(out);
	Check(Pixels(out, frame) != empty, "DrawMainPanel actually draws the cohesive native background");
	std::array<std::vector<uint8_t>, 6> captions;
	for (int i = 0; i < 6; ++i) {
		const Rectangle button = GetD3dHudPanelButtonRect(i);
		DrawD3dHudPanelButton(out, i, false);
		captions[i] = Pixels(out, button);
		MousePosition = button.Center();
		CheckMainPanelButton();
		DrawMainPanelButtons(out);
		Check(Pixels(out, button) != captions[i], "actual native button press changes its written button pixels");
		ResetMainPanelButtons();
	}
	for (int i = 0; i < 6; ++i)
		for (int j = i + 1; j < 6; ++j)
			Check(captions[i] != captions[j], "all six native captions retain their distinct rendered content");
}

void CheckMenuAndInputGates(const Surface &out)
{
	ClosePanels();
	const Point originalMouse = MousePosition;
	const Point originalTile = cursPosition;
	const auto originalAction = LastPlayerAction;
	const int originalMonster = pcursmonst;
	const Player *originalPlayer = PlayerUnderCursor;
	const auto originalObject = ObjectUnderCursor;
	const auto originalItem = pcursitem;
	const auto originalInventoryItem = pcursinvitem;
	pcursmonst = -1;
	PlayerUnderCursor = nullptr;
	ObjectUnderCursor = nullptr;
	pcursitem = -1;
	NewCursor(CURSOR_HAND);
	for (const Point point : { GetD3dHudOrbRect(false).Center(), GetD3dHudPanelButtonRect(0).Center() }) {
		MousePosition = point;
		LastPlayerAction = PlayerActionType::None;
		CheckPlrSpell(false, SpellID::Healing, SpellType::Skill);
		Check(LastPlayerAction == PlayerActionType::None, "real native spell dispatch rejects clicks over the globe and utility button");
	}
	bool foundWorld = false;
	const Rectangle native = GetMainPanel();
	for (int y = 0; y < native.size.height && !foundWorld; y += 8) {
		for (int x = 0; x < native.size.width; x += 8) {
			const Point candidate = native.position + Displacement { x, y };
			if (IsPointOnD3dHud(candidate) || GetD3dHudInfoRect().contains(candidate)) continue;
			MousePosition = candidate;
			foundWorld = true;
			break;
		}
	}
	if (foundWorld) {
		Check(native.contains(MousePosition), "world input remains available in any old-panel area outside the new native frame");
	} else {
		Check(IsPointOnD3dHud(native.Center()), "cohesive native frame protects the old main panel when it fully covers that region");
		MousePosition = { gnScreenWidth / 2, GetD3dHudFrameRect().position.y / 2 };
		Check(!IsPointOnD3dHud(MousePosition), "world input remains available above the cohesive native frame");
	}
	cursPosition = MyPlayer->position.tile;
	LastPlayerAction = PlayerActionType::None;
	CheckPlrSpell(false, SpellID::Healing, SpellType::Skill);
	Check(LastPlayerAction == PlayerActionType::Spell,
	    "real native spell dispatch accepts unobstructed world space through the isolated provider without a game loop");
	LastPlayerAction = originalAction;
	cursPosition = originalTile;
	MousePosition = GetD3dHudPanelButtonRect(3).Center();
	pcursinvitem = -1;
	CheckPanelInfo();
	Check(MainPanelFlag && !InfoString.empty(), "native Menu button hover supplies the information used by the menu-overlap regression");
	const Rectangle info = GetD3dHudInfoRect();
	FillRect(out, 0, 0, out.w(), out.h(), PAL16_GRAY + 13);
	const auto emptyInfo = Pixels(out, info);
	DrawInfoBox(out);
	Check(Pixels(out, info) != emptyInfo, "native information is drawn when the game menu is closed");
	TMenuItem menu[] { { GMENU_ENABLED, "Fixture menu", [](bool) {} }, {} };
	gmenu_set_items(menu, nullptr);
	Check(gmenu_is_active(), "native game menu activation is real in the private fixture");
	FillRect(out, 0, 0, out.w(), out.h(), PAL16_GRAY + 13);
	const auto beforeMenu = Pixels(out, info);
	DrawInfoBox(out);
	Check(Pixels(out, info) == beforeMenu, "frozen native information never paints over the active game menu");
	gmenu_set_items(nullptr, nullptr);
	const ControlTypes originalMode = ControlMode;
	ControlMode = ControlTypes::Gamepad;
	for (int modifier = 0; modifier < 2; ++modifier) {
		PadMenuNavigatorActive = modifier == 0;
		PadHotspellMenuActive = modifier == 1;
		Check(!IsD3dHudEnabled(), "native gamepad modifier menus retain their original HUD surface");
		const Rectangle spell = GetD3dHudSpellRect();
		Check(spell.position == GetMainPanel().position + Displacement { 565, 64 } && spell.size == Size { 56, 56 },
		    "gamepad modifier fallback restores the exact native selected-spell position");
		RenderHud(out);
		const Rectangle hints { GetMainPanel().position + Displacement { 497, -159 }, { 127, 127 } };
		const auto beforeHints = Pixels(out, hints);
		DrawControllerModifierHints(out);
		Check(Pixels(out, hints) != beforeHints, "real native menu and hotspell modifier hints remain visible");
	}
	PadMenuNavigatorActive = false;
	PadHotspellMenuActive = false;
	ControlMode = originalMode;
	pcursmonst = originalMonster;
	PlayerUnderCursor = originalPlayer;
	ObjectUnderCursor = originalObject;
	pcursitem = originalItem;
	pcursinvitem = originalInventoryItem;
	MousePosition = originalMouse;
}

void CheckCompactViewportAndValues()
{
	const uint16_t originalWidth = gnScreenWidth;
	const uint16_t originalHeight = gnScreenHeight;
	const ControlTypes originalMode = ControlMode;
	const bool originalZoom = *GetOptions().Graphics.zoom;
	const bool originalExperience = *GetOptions().Gameplay.experienceBar;
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	ControlMode = ControlTypes::KeyboardAndMouse;
	GetOptions().Graphics.zoom.SetValue(false);
	CalculatePanelAreas();
	Check(gnViewportHeight == 480 && RowsCoveredByPanel() == 4,
	    "compact floating HUD preserves the full viewport and four inverse-transform rows");
	GetOptions().Graphics.zoom.SetValue(true);
	Check(RowsCoveredByPanel() == 2, "compact floating HUD preserves two inverse-transform rows with zoom");
	ControlMode = ControlTypes::VirtualGamepad;
	CalculatePanelAreas();
	Check(gnViewportHeight == 352 && RowsCoveredByPanel() == 0,
	    "touch retains the reserved native panel viewport and zero covered rows");
	ControlMode = ControlTypes::KeyboardAndMouse;
	GetOptions().Gameplay.experienceBar.SetValue(true);
	for (const Size resolution : { Size { 640, 480 }, Size { 640, 360 }, Size { 1920, 1080 } }) {
		gnScreenWidth = resolution.width;
		gnScreenHeight = resolution.height;
		CalculatePanelAreas();
		const Rectangle xp { { resolution.width / 2 - 155, resolution.height - 11 }, { 313, 9 } };
		Check(IsPointOnD3dHud(xp.position + Displacement { xp.size.width - 1, xp.size.height - 1 }),
		    "XP bar protects its last bottom-right pixel from world input");
		for (bool mana : { false, true }) {
			const Rectangle value = GetD3dHudValueRect(mana);
			const bool overlaps = value.position.x < xp.position.x + xp.size.width
			    && xp.position.x < value.position.x + value.size.width
			    && value.position.y < xp.position.y + xp.size.height
			    && xp.position.y < value.position.y + value.size.height;
			Check(!overlaps, "native life/mana values stay clear of the XP bar at compact and full resolutions");
			Check(IsPointOnD3dHud(value.Center()), "native value labels protect their center from world input");
		}
	}
	GetOptions().Gameplay.experienceBar.SetValue(originalExperience);
	GetOptions().Graphics.zoom.SetValue(originalZoom);
	ControlMode = originalMode;
	gnScreenWidth = originalWidth;
	gnScreenHeight = originalHeight;
	CalculatePanelAreas();
}

void CheckResolution(const std::filesystem::path &directory, int width, int height)
{
	gnScreenWidth = width;
	gnScreenHeight = height;
	CalculatePanelAreas();
	CheckNativeInput();
	OwnedSurface surface(width, height);
	CheckFrameComposition(surface);
	CheckAuxiliaryControls(surface, directory);
	CheckMenuAndInputGates(surface);
	std::array<std::vector<uint8_t>, 3> life, mana;
	for (int state = 0; state < 3; ++state) {
		MyPlayer->_pHitPoints = MyPlayer->_pHPBase = MyPlayer->_pMaxHP * state / 2;
		MyPlayer->_pMana = MyPlayer->_pManaBase = MyPlayer->_pMaxMana * state / 2;
		RenderHud(surface);
		life[state] = Pixels(surface, GetD3dHudOrbRect(false));
		mana[state] = Pixels(surface, GetD3dHudOrbRect(true));
		Capture(surface, directory, "hud-" + std::to_string(width) + "x" + std::to_string(height) + "-" + std::to_string(state) + ".bmp");
	}
	Check(life[0] != life[1] && life[1] != life[2], "life zero, half and full produce distinct native globe pixels");
	Check(mana[0] != mana[1] && mana[1] != mana[2], "mana zero, half and full produce distinct native globe pixels");
	const auto normalValues = Pixels(surface, { { 0, 0 }, { width, height } });
	MyPlayer->_pIFlags |= ItemSpecialEffect::NoMana;
	RenderHud(surface);
	Check(Pixels(surface, { { 0, 0 }, { width, height } }) != normalValues, "NoMana changes the displayed native values");
	MyPlayer->_pIFlags &= ~ItemSpecialEffect::NoMana;
	GetOptions().Gameplay.showHealthValues.SetValue(false);
	GetOptions().Gameplay.showManaValues.SetValue(false);
	RenderHud(surface);
	Check(Pixels(surface, { { 0, 0 }, { width, height } }) != normalValues, "native value preferences affect the rendered HUD");
	GetOptions().Gameplay.showHealthValues.SetValue(true);
	GetOptions().Gameplay.showManaValues.SetValue(true);
}

void CheckOverride(const std::filesystem::path &profile, const std::filesystem::path &directory)
{
	gnScreenWidth = 1920;
	gnScreenHeight = 1080;
	CalculatePanelAreas();
	const Rectangle belt = GetD3dHudBeltRect();
	const Rectangle spell = GetD3dHudSpellRect();
	std::filesystem::create_directories(profile / "d3d-ui");
	{
		std::ofstream layout(profile / "d3d-ui" / "layout.ini");
		layout << "[Layout]\nformat=d3d.ui-layout\nschemaVersion=1\n\n"
		          "[HudBelt]\nanchor=bottom-center\noffsetX=-116\noffsetY=-140\nwidth=232\nheight=29\n\n"
		          "[HudSpell]\nanchor=bottom-center\noffsetX=60\noffsetY=-200\nwidth=56\nheight=56\n";
		Check(static_cast<bool>(layout), "write only the private fixture layout override");
	}
	ReloadD3dUiLayout();
	Check(GetD3dHudBeltRect().position != belt.position && GetD3dHudSpellRect().position != spell.position,
	    "real INI loader shifts both shared HUD rectangles");
	CheckNativeInput();
	OwnedSurface out(1920, 1080);
	RenderHud(out);
	Capture(out, directory, "hud-1920x1080-private-layout-override.bmp");
	{
		const Rectangle nativeTarget = InvRect[SLOTXY_INV_FIRST + 7];
		const Point target = nativeTarget.Center() + Displacement { GetRightPanel().position.x, GetRightPanel().position.y };
		const int logicalWidth = gnScreenWidth * 480 / gnScreenHeight;
		const int safeOffset = (logicalWidth - D3dHudSafeWidth(logicalWidth, 480)) / 2;
		constexpr int BeltWidth = 96;
		constexpr int BeltHeight = 29;
		// Align the eighth cell's center with an actual native grid cell. The
		// right panel includes widescreen margins and is not flush to the edge.
		const int offsetX = (target.x * 480 + gnScreenHeight / 2) / gnScreenHeight - safeOffset - BeltWidth * 15 / 16;
		const int offsetY = (target.y * 480 + gnScreenHeight / 2) / gnScreenHeight - BeltHeight / 2;
		std::ofstream layout(profile / "d3d-ui" / "layout.ini");
		layout << "[Layout]\nformat=d3d.ui-layout\nschemaVersion=1\n\n"
		          "[HudBelt]\nanchor=top-left\noffsetX=" << offsetX << "\noffsetY=" << offsetY
		       << "\nwidth=" << BeltWidth << "\nheight=" << BeltHeight << '\n';
		Check(static_cast<bool>(layout), "write the private belt/inventory overlap regression layout");
	}
	ReloadD3dUiLayout();
	const Rectangle overlappingSlot = GetD3dHudBeltSlotRect(7);
	bool overlapsInventory = false;
	for (int i = SLOTXY_INV_FIRST; i <= SLOTXY_INV_LAST; ++i) {
		Rectangle native = InvRect[i];
		native.position += Displacement { GetRightPanel().position.x, GetRightPanel().position.y };
		overlapsInventory = overlapsInventory || native.contains(overlappingSlot.Center());
	}
	Check(overlapsInventory, "override deliberately places a visible belt cell over a native inventory cell");
	CheckNativeInput();
	RenderHud(out);
	Capture(out, directory, "hud-1920x1080-belt-inventory-overlap.bmp");
}

void CheckMultiplayer(const std::filesystem::path &directory)
{
	const bool originalFriendly = MyPlayer->friendlyMode;
	const uint32_t random = GetLCGEngineState();
	FreeControlPan();
	gbIsMultiplayer = true;
	const auto initialized = InitMainPanel();
	Check(initialized.has_value(), initialized ? "bootstrap native multiplayer HUD resources" : initialized.error());
	Check(IsChatAvailable() && BottomBuffer->h() == 288,
	    "multiplayer bootstrap loads the real two-page panel and native chat/friendly assets");
	ClosePanels();
	gnScreenWidth = 1920;
	gnScreenHeight = 1080;
	CalculatePanelAreas();
	OwnedSurface out(gnScreenWidth, gnScreenHeight);
	RenderHud(out);
	const Rectangle chat = GetD3dHudPanelButtonRect(6);
	const Rectangle friendly = GetD3dHudPanelButtonRect(7);
	Check(!IsPointOnD3dHud({ (chat.Center().x + friendly.Center().x) / 2, chat.Center().y }),
	    "multiplayer buttons protect their two rectangles without consuming the empty world space between them");
	CheckAuxiliaryRect(GetD3dHudLevelButtonRect());
	CheckAuxiliaryRect(GetD3dHudLevelLabelRect());
	CheckAuxiliaryRect(GetD3dHudDurabilityRect());
	for (int i : { 6, 7 }) {
		MousePosition = GetD3dHudPanelButtonRect(i).Center();
		CheckPanelInfo();
		Check(MainPanelFlag && !InfoString.empty(), "real multiplayer button preserves native hover information");
		DrawD3dHudPanelButton(out, i, false);
		const auto normal = Pixels(out, GetD3dHudPanelButtonRect(i));
		CheckMainPanelButton();
		Check(MainPanelButtonDown, "native multiplayer button recognizes its drawn press target");
		DrawMainPanelButtons(out);
		Check(Pixels(out, GetD3dHudPanelButtonRect(i)) != normal,
		    "real multiplayer button sprite changes to its native pressed frame");
		ResetMainPanelButtons();
	}
	MyPlayer->friendlyMode = false;
	DrawD3dHudPanelButton(out, 7, false);
	const auto hostile = Pixels(out, GetD3dHudPanelButtonRect(7));
	DrawD3dHudPanelButton(out, 7, true);
	const auto hostilePressed = Pixels(out, GetD3dHudPanelButtonRect(7));
	Check(hostilePressed != hostile, "loaded native hostile sprite preserves its separate pressed frame");
	MyPlayer->friendlyMode = true;
	DrawD3dHudPanelButton(out, 7, false);
	const auto friendlyNormal = Pixels(out, GetD3dHudPanelButtonRect(7));
	Check(friendlyNormal != hostile,
	    "loaded native friendly and hostile sprites reflect the real player state without sending PvP commands");
	DrawD3dHudPanelButton(out, 7, true);
	Check(Pixels(out, GetD3dHudPanelButtonRect(7)) != friendlyNormal
	        && Pixels(out, GetD3dHudPanelButtonRect(7)) != hostilePressed,
	    "loaded native friendly pressed sprite stays distinct from both other states");
	DrawD3dHudPanelButton(out, 7, false);
	Capture(out, directory, "hud-1920x1080-native-multiplayer.bmp");
	ClickPanelButton(6);
	Check(ChatFlag && IsChatActive() && !IsD3dHudEnabled(), "native chat button enters the complete legacy chat surface");
	RenderHud(out);
	LoadSmallSelectionSpinner();
	Check(pSPentSpn2Cels.has_value(), "load the real native chat cursor sprite for the isolated fixture");
	ChatInputState->assign("Native chat fixture");
	const Rectangle chatText { GetMainPanel().position + Displacement { 200, 10 }, { 250, 39 } };
	const auto emptyChat = Pixels(out, chatText);
	DrawChatBox(out);
	Check(Pixels(out, chatText) != emptyChat, "real native chat text and cursor render over the restored chat page");
	Capture(out, directory, "hud-1920x1080-native-chat-fallback.bmp");
	ResetChat();
	Check(!ChatFlag && IsD3dHudEnabled(), "native chat reset returns to the cohesive HUD");
	gnScreenWidth = 853;
	gnScreenHeight = 480;
	CalculatePanelAreas();
	ClosePanels();
	Check(IsD3dHudEnabled(), "multiplayer at 853 by 480 uses the cohesive HUD with side panels closed");
	const Rectangle compactChat = GetD3dHudPanelButtonRect(6);
	const Rectangle compactFriendly = GetD3dHudPanelButtonRect(7);
	Check(Overlaps(compactChat, GetLeftPanel()) && Overlaps(compactFriendly, GetRightPanel()),
	    "853-wide native side panels intersect the authored multiplayer button rectangles");
	ClickPanelButton(0);
	Check(CharFlag && !IsD3dHudEnabled() && Overlaps(compactChat, GetLeftPanel()),
	    "native Character handler selects the complete legacy HUD when its panel covers multiplayer chat");
	Check(GetD3dHudFrameRect().position == GetMainPanel().position && GetD3dHudFrameRect().size == GetMainPanel().size,
	    "Character overlap fallback restores the exact native panel surface");
	ClickPanelButton(0);
	Check(!CharFlag && IsD3dHudEnabled(), "closing Character through its native fallback button restores the cohesive HUD");
	ClickPanelButton(4);
	Check(invflag && !IsD3dHudEnabled() && Overlaps(compactFriendly, GetRightPanel()),
	    "native Inventory handler selects the complete legacy HUD when its panel covers the friendly button");
	Check(GetD3dHudFrameRect().position == GetMainPanel().position && GetD3dHudFrameRect().size == GetMainPanel().size,
	    "Inventory overlap fallback restores the exact native panel surface");
	ClickPanelButton(4);
	Check(!invflag && IsD3dHudEnabled(), "closing Inventory through its native fallback button restores the cohesive HUD");
	gnScreenWidth = 1920;
	gnScreenHeight = 1080;
	CalculatePanelAreas();
	Check(IsD3dHudEnabled() && !IsLeftPanelOpen() && !IsRightPanelOpen(),
	    "multiplayer side-panel regression restores the original 1920 by 1080 context");
	MyPlayer->friendlyMode = originalFriendly;
	FreeControlPan();
	gbIsMultiplayer = false;
	const auto restored = InitMainPanel();
	Check(restored.has_value(), restored ? "restore the native single-player HUD resources" : restored.error());
	ClosePanels();
	Check(GetLCGEngineState() == random, "multiplayer bootstrap and UI handlers preserve simulation RNG");
}

void CheckScaleBoundaryAndInformation(const std::filesystem::path &directory)
{
	Rectangle previousBelt;
	Rectangle previousFrame;
	for (int width : { 853, 854 }) {
		gnScreenWidth = width;
		gnScreenHeight = 480;
		CalculatePanelAreas();
		OwnedSurface out(width, 480);
		CheckFrameComposition(out);
		const Rectangle frame = GetD3dHudFrameRect();
		const Rectangle belt = GetD3dHudBeltRect();
		if (width == 854)
			Check(frame.size == previousFrame.size && belt.size == previousBelt.size
			        && frame.position.y == previousFrame.position.y && belt.position.y == previousBelt.position.y
			        && frame.position.x - previousFrame.position.x >= 0 && frame.position.x - previousFrame.position.x <= 1
			        && belt.position.x - previousBelt.position.x >= 0 && belt.position.x - previousBelt.position.x <= 1,
			    "853 and 854 widths keep the same 480-canvas scale without a compact-layout jump");
		previousBelt = belt;
		previousFrame = frame;
		RenderHud(out);
		MousePosition = GetD3dHudPanelButtonRect(3).Center();
		pcursinvitem = -1;
		CheckPanelInfo();
		constexpr std::string_view FiveLines { "Native information 1\nNative information 2\nNative information 3\nNative information 4\ngjpqy information 5" };
		InfoString = FiveLines;
		InfoColor = UiFlags::ColorWhite;
		const Rectangle info = GetD3dHudInfoRect();
		DrawD3dHudPlate(out, info);
		const auto before = Pixels(out, info);
		DrawInfoBox(out);
		Check(InfoString.str() == FiveLines, "native information preserves all five lines without a stale world hover override");
		const auto after = Pixels(out, info);
		Check(after != before, "five-line native information is rendered in the compact shared panel");
		for (int line = 0; line < 5; ++line) {
			bool changed = false;
			const int top = (2 + line * 12) * info.size.height / 64;
			const int bottom = (14 + line * 12) * info.size.height / 64;
			for (int y = top; y < bottom && !changed; ++y)
				for (int x = 0; x < info.size.width && !changed; ++x) {
					const size_t pixel = static_cast<size_t>(y) * info.size.width + x;
					changed = after[pixel] != before[pixel];
				}
			Check(changed, "each of the five native text rows changes pixels inside the information box");
		}
		Capture(out, directory, "hud-" + std::to_string(width) + "x480-native-frame-five-lines.bmp");
	}
}

} // namespace

int main(int argc, char **argv)
{
	using namespace devilution;
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;
	if (argc != 4) {
		std::cerr << "Usage: hud_runtime_smoke <game-data-directory> <built-assets-directory> <capture-parent-directory>\n";
		return 2;
	}
	const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto directory = std::filesystem::absolute(argv[3]) / ("hud-runtime-" + std::to_string(nonce));
	const auto profile = directory / "private-profile";
	std::filesystem::create_directories(profile);
	paths::SetBasePath(argv[1]);
	paths::SetAssetsPath(argv[2]);
	paths::SetPrefPath(profile.string());
	paths::SetConfigPath(profile.string());
	SDL_SetMainReady();
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		std::cerr << SDL_GetError() << '\n';
		return 2;
	}
	int status = 0;
	try {
		InitializeFixture();
		CheckCompactViewportAndValues();
		for (const Size resolution : { Size { 1920, 1080 }, Size { 1280, 720 }, Size { 2560, 1080 }, Size { 640, 480 } })
			CheckResolution(directory, resolution.width, resolution.height);
		CheckScaleBoundaryAndInformation(directory);
		CheckMultiplayer(directory);
		CheckOverride(profile, directory);
		std::ofstream receipt(directory / "receipt.json");
		receipt << "{\"kind\":\"native-hud-technical-fixture\",\"gameplayScreenshot\":false,\"visualApproval\":false,\"worldRendered\":false,\"gameLoopStarted\":false,\"savesWritten\":false,\"configurationWrites\":\"private-fixture-profile-only\",\"checks\":" << Checks << ",\"captures\":[";
		for (size_t i = 0; i < Captures.size(); ++i)
			receipt << (i ? "," : "") << '"' << Captures[i] << '"';
		receipt << "]}\n";
		std::cout << "PASS " << Checks << " checks; technical captures: " << directory.string() << '\n';
	} catch (const std::exception &error) {
		std::cerr << "HUD fixture stopped: " << error.what() << '\n';
		status = 1;
	}
	FreeControlPan();
	FreeCursor();
	UnloadFonts();
	SNetDestroy();
	SDL_Quit();
	return status;
}
