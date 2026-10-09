/**
 * @file diablo.cpp
 *
 * Implementation of the main game initialization functions.
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#else
#include <SDL.h>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

#include <config.h>

#include "DiabloUI/selstart.h"
#include "appfat.h"
#include "automap.h"
#include "capture.h"
#include "control/control.hpp"
#include "control/d3d_hud.hpp"
#include "cursor.h"
#include "dead.h"
#ifdef _DEBUG
#include "debug.h"
#endif
#include "DiabloUI/diabloui.h"
#include "controls/control_mode.hpp"
#include "controls/keymapper.hpp"
#include "controls/plrctrls.h"
#include "controls/remap_keyboard.h"
#include "controls/town_first_person_input.hpp"
#include "diablo.h"
#include "diablo_msg.hpp"
#include "discord/discord.h"
#include "doom.h"
#include "encrypt.h"
#include "engine/backbuffer_state.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/demomode.h"
#include "engine/dx.h"
#include "engine/events.hpp"
#include "engine/load_cel.hpp"
#include "engine/load_file.hpp"
#include "engine/random.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/town_view.hpp"
#include "engine/sound.h"
#include "game_mode.hpp"
#include "gamemenu.h"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "help.h"
#include "hwcursor.hpp"
#include "init.hpp"
#include "ingame_settings.h"
#include "inv.h"
#include "levels/drlg_l1.h"
#include "levels/drlg_l2.h"
#include "levels/drlg_l3.h"
#include "levels/drlg_l4.h"
#include "levels/gendung.h"
#include "levels/setmaps.h"
#include "levels/themes.h"
#include "levels/town.h"
#include "levels/trigs.h"
#include "lighting.h"
#include "loadsave.h"
#include "lua/lua_event.hpp"
#include "lua/lua_global.hpp"
#include "menu.h"
#include "minitext.h"
#include "missiles.h"
#include "mods/mod_identity.h"
#include "movie.h"
#include "multi.h"
#include "nthread.h"
#include "objects.h"
#include "options.h"
#include "panels/console.hpp"
#include "panels/info_box.hpp"
#include "panels/partypanel.hpp"
#include "panels/quest_log.hpp"
#include "panels/spell_book.hpp"
#include "panels/spell_list.hpp"
#include "pfile.h"
#include "plrmsg.h"
#include "qol/chatlog.h"
#include "qol/floatingnumbers.h"
#include "qol/itemlabels.h"
#include "qol/monhealthbar.h"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "qol/xpbar.h"
#include "quick_messages.hpp"
#include "restrict.h"
#include "stores.h"
#include "storm/storm_net.hpp"
#include "storm/storm_svid.h"
#include "tables/monstdat.h"
#include "tables/playerdat.hpp"
#include "towners.h"
#include "track.h"
#include "utils/console.h"
#include "utils/display.h"
#include "utils/format.hpp"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/parse_int.hpp"
#include "utils/paths.h"
#include "utils/screen_reader.hpp"
#include "utils/sdl_compat.h"
#include "utils/sdl_thread.h"
#include "utils/status_macros.hpp"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

#ifndef USE_SDL1
#include "controls/touch/gamepad.h"
#include "controls/touch/renderers.h"
#endif

#ifdef __vita__
#include "platform/vita/touch.h"
#endif

#ifdef GPERF_HEAP_FIRST_GAME_ITERATION
#include <gperftools/heap-profiler.h>
#endif

namespace devilution {

Point MousePosition;
bool gbRunGameResult;
bool ReturnToMainMenu;
/** Enable updating of player character, set to false once Diablo dies */
bool gbProcessPlayers;
bool gbLoadGame;
bool cineflag;
int PauseMode;
clicktype sgbMouseDown;
uint16_t gnTickDelay = 50;
char gszProductName[64] = "DevilutionX vUnknown";

#ifdef _DEBUG
bool DebugDisableNetworkTimeout = false;
std::vector<std::string> DebugCmdsFromCommandLine;
#endif
GameLogicStep gGameLogicStep = GameLogicStep::None;

/** This and the following mouse variables are for handling in-game click-and-hold actions */
PlayerActionType LastPlayerAction = PlayerActionType::None;

// Controller support: Actions to run after updating the cursor state.
// Defined in SourceX/controls/plctrls.cpp.
extern void plrctrls_after_check_curs_move();
extern void plrctrls_every_frame();
extern void plrctrls_after_game_logic();

namespace {

char gszVersionNumber[64] = "internal version unknown";

bool gbGameLoopStartup;
bool forceSpawn;
bool forceDiablo;
int sgnTimeoutCurs;
bool gbShowIntro = true;
/** To know if these things have been done when we get to the diablo_deinit() function */
bool was_archives_init = false;
/** To know if surfaces have been initialized or not */
bool was_window_init = false;
bool was_ui_init = false;

TownFirstPersonInputState FirstPersonInput;
TownFirstPersonInputServicesForDiagnostics FirstPersonServices;
bool FirstPersonHasDiagnosticServices = false;
// Keep physical ownership until the matching UP even across a remap/UI gate.
struct TownMovementOwnedKey {
	uint32_t identity;
	int scancode;
	const KeymapperOptions::Action *action;
	uint64_t epoch;
};
std::vector<TownMovementOwnedKey> TownMovementOwnedKeys;
uint64_t TownMovementEpoch = 0;
TownCameraMode TownMovementLastMode = TownCameraMode::Isometric;
TownFirstPersonInputState ThirdPersonMovementInput;
bool ThirdPersonMovementInitialized = false;
uint32_t FirstPersonConsumedButtons = 0;
Point FirstPersonSavedPointer;
bool FirstPersonHasSavedPointer = false;
uint16_t FirstPersonMovementModifiers = 0;
uint32_t TownCameraLastFrameTicks = 0;
bool TownCameraHasFrameClock = false;
bool TownFollowWheelNeedsDraw = false;

struct FirstPersonDeferredClick {
	Uint8 button;
	uint16_t modifiers;
	bool down;
	float lookX = 0;
	float lookY = 0;
	float wheelSteps = 0;
	bool absoluteClick = false;
	Point pointer {};
};
std::array<FirstPersonDeferredClick, 256> FirstPersonDeferredClicks;
size_t FirstPersonDeferredClickCount = 0;

void GameEventHandler(const SDL_Event &event, uint16_t modState);
bool HasFirstPersonPointerPick();
bool CanUseTownMovementKeys();
TownFirstPersonInputResult SyncThirdPersonMovement(uint8_t pressed = 0, uint8_t released = 0);
void FlushTownFirstPersonClicks();
TownFirstPersonInputResult SyncTownFirstPersonInput(uint8_t pressed = 0, uint8_t released = 0,
    float mouseX = 0, float mouseY = 0, bool escapePressed = false, bool resumeClick = false);

void StartGame(interface_mode uMsg)
{
	CalcViewportGeometry();
	cineflag = false;
	InitCursor();
#ifdef _DEBUG
	LoadDebugGFX();
#endif
	assert(HeadlessMode || ghMainWnd);
	music_stop();
	InitMonsterHealthBar();
	InitXPBar();
	ShowProgress(uMsg);
	gmenu_init_menu();
	InitLevelCursor();
	sgnTimeoutCurs = CURSOR_NONE;
	sgbMouseDown = CLICK_NONE;
	LastPlayerAction = PlayerActionType::None;
}

void FreeGame()
{
	FreeMonsterHealthBar();
	FreeXPBar();
	FreeControlPan();
	FreeInvGFX();
	FreeGMenu();
	FreeQuestText();
	FreeInfoBoxGfx();
	FreeStoreMem();

	for (Player &player : Players)
		ResetPlayerGFX(player);

	FreeCursor();
#ifdef _DEBUG
	FreeDebugGFX();
#endif
	FreeGameMem();
	stream_stop();
	music_stop();
}

bool ProcessInput()
{
	SyncTownFirstPersonInput();
	if (PauseMode == 2) {
		return false;
	}

	plrctrls_every_frame();

	if (!gbIsMultiplayer && gmenu_is_active()) {
		RedrawViewport();
		return false;
	}

	if (!gmenu_is_active() && sgnTimeoutCurs == CURSOR_NONE) {
#ifdef __vita__
		FinishSimulatedMouseClicks(MousePosition);
#endif
		CheckCursMove();
		if (IsTownFirstPersonInputCaptured() && !HasFirstPersonPointerPick())
			LastPlayerAction = PlayerActionType::None;
		plrctrls_after_check_curs_move();
		RepeatPlayerAction();
	}

	return true;
}

void LeftMouseCmd(bool bShift)
{
	bool bNear;

	assert(!IsPointOnD3dHud(MousePosition));

	if (leveltype == DTYPE_TOWN) {
		CloseGoldWithdraw();
		CloseStash();
		CloseVisualStore();
		if (pcursitem != -1 && pcurs == CURSOR_HAND)
			NetSendCmdLocParam1(true, invflag ? CMD_GOTOGETITEM : CMD_GOTOAGETITEM, cursPosition, pcursitem);
		if (pcursmonst != -1)
			NetSendCmdLocParam1(true, CMD_TALKXY, cursPosition, pcursmonst);
		if (pcursitem == -1 && pcursmonst == -1 && PlayerUnderCursor == nullptr) {
			LastPlayerAction = PlayerActionType::Walk;
			NetSendCmdLoc(MyPlayerId, true, CMD_WALKXY, cursPosition);
		}
		return;
	}

	const Player &myPlayer = *MyPlayer;
	bNear = myPlayer.position.tile.WalkingDistance(cursPosition) < 2;
	if (pcursitem != -1 && pcurs == CURSOR_HAND && !bShift) {
		NetSendCmdLocParam1(true, invflag ? CMD_GOTOGETITEM : CMD_GOTOAGETITEM, cursPosition, pcursitem);
	} else if (ObjectUnderCursor != nullptr && !ObjectUnderCursor->IsDisabled() && (!bShift || (bNear && ObjectUnderCursor->_oBreak == 1))) {
		LastPlayerAction = PlayerActionType::OperateObject;
		NetSendCmdLoc(MyPlayerId, true, pcurs == CURSOR_DISARM ? CMD_DISARMXY : CMD_OPOBJXY, cursPosition);
	} else if (myPlayer.UsesRangedWeapon()) {
		if (bShift) {
			LastPlayerAction = PlayerActionType::Attack;
			NetSendCmdLoc(MyPlayerId, true, CMD_RATTACKXY, cursPosition);
		} else if (pcursmonst != -1) {
			if (CanTalkToMonst(Monsters[pcursmonst])) {
				NetSendCmdParam1(true, CMD_ATTACKID, pcursmonst);
			} else {
				LastPlayerAction = PlayerActionType::AttackMonsterTarget;
				NetSendCmdParam1(true, CMD_RATTACKID, pcursmonst);
			}
		} else if (PlayerUnderCursor != nullptr && !PlayerUnderCursor->hasNoLife() && !myPlayer.friendlyMode) {
			LastPlayerAction = PlayerActionType::AttackPlayerTarget;
			NetSendCmdParam1(true, CMD_RATTACKPID, PlayerUnderCursor->getId());
		}
	} else {
		if (bShift) {
			if (pcursmonst != -1) {
				if (CanTalkToMonst(Monsters[pcursmonst])) {
					NetSendCmdParam1(true, CMD_ATTACKID, pcursmonst);
				} else {
					LastPlayerAction = PlayerActionType::Attack;
					NetSendCmdLoc(MyPlayerId, true, CMD_SATTACKXY, cursPosition);
				}
			} else {
				LastPlayerAction = PlayerActionType::Attack;
				NetSendCmdLoc(MyPlayerId, true, CMD_SATTACKXY, cursPosition);
			}
		} else if (pcursmonst != -1) {
			LastPlayerAction = PlayerActionType::AttackMonsterTarget;
			NetSendCmdParam1(true, CMD_ATTACKID, pcursmonst);
		} else if (PlayerUnderCursor != nullptr && !PlayerUnderCursor->hasNoLife() && !myPlayer.friendlyMode) {
			LastPlayerAction = PlayerActionType::AttackPlayerTarget;
			NetSendCmdParam1(true, CMD_ATTACKPID, PlayerUnderCursor->getId());
		}
	}
	if (!bShift && pcursitem == -1 && ObjectUnderCursor == nullptr && pcursmonst == -1 && PlayerUnderCursor == nullptr) {
		LastPlayerAction = PlayerActionType::Walk;
		NetSendCmdLoc(MyPlayerId, true, CMD_WALKXY, cursPosition);
	}
}

bool TryOpenDungeonWithMouse()
{
	if (leveltype != DTYPE_TOWN)
		return false;

	const Item &holdItem = MyPlayer->HoldItem;
	if (holdItem.IDidx == IDI_RUNEBOMB && OpensHive(cursPosition))
		OpenHive();
	else if (holdItem.IDidx == IDI_MAPOFDOOM && OpensGrave(cursPosition))
		OpenGrave();
	else
		return false;

	NewCursor(CURSOR_HAND);
	return true;
}

void LeftMouseDown(uint16_t modState)
{
	CancelTownFirstPersonWalk();
	if (IsTownFirstPersonInputCaptured()) {
		MousePosition = GetTownFirstPersonPointer(MousePosition);
		ResetItemlabelHighlighted();
		CheckCursMove();
		if (!HasFirstPersonPointerPick()) {
			LastPlayerAction = PlayerActionType::None;
			return;
		}
	}
	LastPlayerAction = PlayerActionType::None;

	if (gmenu_left_mouse(true))
		return;

	if (CheckMuteButton())
		return;

	if (sgnTimeoutCurs != CURSOR_NONE)
		return;

	if (MyPlayerIsDead) {
		CheckMainPanelButtonDead();
		return;
	}

	if (PauseMode == 2) {
		return;
	}
	if (DoomFlag) {
		doom_close();
		return;
	}

	if (SpellSelectFlag) {
		SetSpell();
		return;
	}

	if (IsPlayerInStore()) {
		CheckStoreBtn();
		return;
	}

	const bool isShiftHeld = (modState & SDL_KMOD_SHIFT) != 0;
	const bool isCtrlHeld = (modState & SDL_KMOD_CTRL) != 0;

	if (!IsPointOnD3dHud(MousePosition)) {
		if (!gmenu_is_active() && !TryIconCurs()) {
			if (QuestLogIsOpen && GetLeftPanel().contains(MousePosition)) {
				QuestlogESC();
			} else if (qtextflag) {
				qtextflag = false;
				stream_stop();
			} else if (CharFlag && GetLeftPanel().contains(MousePosition)) {
				CheckChrBtns();
			} else if (invflag && GetRightPanel().contains(MousePosition)) {
				if (!DropGoldFlag)
					CheckInvItem(isShiftHeld, isCtrlHeld);
			} else if (IsStashOpen && GetLeftPanel().contains(MousePosition)) {
				if (!IsWithdrawGoldOpen)
					CheckStashItem(MousePosition, isShiftHeld, isCtrlHeld);
				CheckStashButtonPress(MousePosition);
			} else if (IsVisualStoreOpen && GetLeftPanel().contains(MousePosition)) {
				if (!MyPlayer->HoldItem.isEmpty()) {
					CheckVisualStorePaste(MousePosition);
				} else {
					CheckVisualStoreItem(MousePosition, isCtrlHeld, isShiftHeld);
				}
				CheckVisualStoreButtonPress(MousePosition);
			} else if (SpellbookFlag && GetRightPanel().contains(MousePosition)) {
				CheckSBook();
			} else if (!MyPlayer->HoldItem.isEmpty()) {
				if (!TryOpenDungeonWithMouse()) {
					const Point currentPosition = MyPlayer->position.tile;
					std::optional<Point> itemTile = FindAdjacentPositionForItem(currentPosition, GetDirection(currentPosition, cursPosition));
					if (itemTile) {
						NetSendCmdPItem(true, CMD_PUTITEM, *itemTile, MyPlayer->HoldItem);
						NewCursor(CURSOR_HAND);
					}
				}
			} else {
				CheckLevelButton();
				if (!LevelButtonDown)
					LeftMouseCmd(isShiftHeld);
			}
		}
	} else {
		if (!ChatFlag && !DropGoldFlag && !IsWithdrawGoldOpen && !gmenu_is_active())
			CheckInvScrn(isShiftHeld, isCtrlHeld);
		CheckMainPanelButton();
		CheckStashButtonPress(MousePosition);
		if (pcurs > CURSOR_HAND && pcurs < CURSOR_FIRSTITEM)
			NewCursor(CURSOR_HAND);
	}
}

void LeftMouseUp(uint16_t modState)
{
	gmenu_left_mouse(false);
	CheckMuteButtonUp();
	if (MainPanelButtonDown)
		CheckMainPanelButtonUp();
	CheckStashButtonRelease(MousePosition);
	CheckVisualStoreButtonRelease(MousePosition);
	if (CharPanelButtonActive) {
		const bool isShiftHeld = (modState & SDL_KMOD_SHIFT) != 0;
		ReleaseChrBtns(isShiftHeld);
	}
	if (LevelButtonDown)
		CheckLevelButtonUp();
	if (IsPlayerInStore())
		ReleaseStoreBtn();
}

void RightMouseDown(bool isShiftHeld)
{
	CancelTownFirstPersonWalk();
	if (IsTownFirstPersonInputCaptured()) {
		MousePosition = GetTownFirstPersonPointer(MousePosition);
		ResetItemlabelHighlighted();
		CheckCursMove();
		if (!HasFirstPersonPointerPick()) {
			LastPlayerAction = PlayerActionType::None;
			return;
		}
	}
	LastPlayerAction = PlayerActionType::None;

	if (gmenu_is_active() || sgnTimeoutCurs != CURSOR_NONE || PauseMode == 2 || MyPlayer->_pInvincible) {
		return;
	}

	if (qtextflag) {
		qtextflag = false;
		stream_stop();
		return;
	}

	if (DoomFlag) {
		doom_close();
		return;
	}
	if (IsPlayerInStore())
		return;
	if (SpellSelectFlag) {
		SetSpell();
		return;
	}
	if (SpellbookFlag && GetRightPanel().contains(MousePosition))
		return;
	if (TryIconCurs())
		return;
	if (pcursinvitem != -1 && UseInvItem(pcursinvitem))
		return;
	if (pcursstashitem != StashStruct::EmptyCell && UseStashItem(pcursstashitem))
		return;
	if (DidRightClickPartyPortrait())
		return;
	if (pcurs == CURSOR_HAND) {
		if (IsPointOnD3dHud(MousePosition))
			return;
		CheckPlrSpell(isShiftHeld);
	} else if (pcurs > CURSOR_HAND && pcurs < CURSOR_FIRSTITEM) {
		NewCursor(CURSOR_HAND);
	}
}

void ReleaseKey(SDL_Keycode vkey)
{
	remap_keyboard_key(&vkey);
	if (sgnTimeoutCurs != CURSOR_NONE)
		return;
	KeymapperRelease(vkey);
}

void ClosePanels()
{
	if (CanPanelsCoverView()) {
		if (!IsLeftPanelOpen() && IsRightPanelOpen() && MousePosition.x < 480 && MousePosition.y < GetMainPanel().position.y) {
			SetCursorPos(MousePosition + Displacement { 160, 0 });
		} else if (!IsRightPanelOpen() && IsLeftPanelOpen() && MousePosition.x > 160 && MousePosition.y < GetMainPanel().position.y) {
			SetCursorPos(MousePosition - Displacement { 160, 0 });
		}
	}
	CloseInventory();
	CloseCharPanel();
	SpellbookFlag = false;
	QuestLogIsOpen = false;
}

void PressKey(SDL_Keycode vkey, uint16_t modState)
{
	Options &options = GetOptions();
	remap_keyboard_key(&vkey);
	if ((vkey == SDLK_RETURN || vkey == SDLK_KP_ENTER) && (modState & SDL_KMOD_ALT) != 0)
		SuspendTownFirstPersonInput();

	if (vkey == SDLK_UNKNOWN)
		return;

	if (gmenu_presskeys(vkey) || CheckKeypress(vkey)) {
		return;
	}

	if (MyPlayerIsDead) {
		if (vkey == SDLK_ESCAPE) {
			if (!gbIsMultiplayer) {
				if (gbValidSaveFile)
					gamemenu_load_game(false);
				else
					gamemenu_exit_game(false);
			} else {
				NetSendCmd(true, CMD_RETOWN);
			}
			return;
		}
		if (sgnTimeoutCurs != CURSOR_NONE) {
			return;
		}
		KeymapperPress(vkey);
		if (vkey == SDLK_RETURN || vkey == SDLK_KP_ENTER) {
			if ((modState & SDL_KMOD_ALT) != 0) {
				options.Graphics.fullscreen.SetValue(!IsFullScreen());
				if (!demo::IsRunning()) SaveOptions();
			} else {
				TypeChatMessage();
			}
		}
		if (vkey != SDLK_ESCAPE) {
			return;
		}
	}
	// Disallow player from accessing escape menu during the frames before the death message appears
	if (vkey == SDLK_ESCAPE && MyPlayer->_pHitPoints > 0) {
		if (!PressEscKey()) {
			LastPlayerAction = PlayerActionType::None;
			gamemenu_on();
		}
		return;
	}

	if (DropGoldFlag) {
		control_drop_gold(vkey);
		return;
	}
	if (IsWithdrawGoldOpen) {
		WithdrawGoldKeyPress(vkey);
		return;
	}

	if (sgnTimeoutCurs != CURSOR_NONE) {
		return;
	}

	KeymapperPress(vkey);

	if (PauseMode == 2) {
		if ((vkey == SDLK_RETURN || vkey == SDLK_KP_ENTER) && (modState & SDL_KMOD_ALT) != 0) {
			options.Graphics.fullscreen.SetValue(!IsFullScreen());
			if (!demo::IsRunning()) SaveOptions();
		}
		return;
	}

	if (DoomFlag) {
		doom_close();
		return;
	}

	switch (vkey) {
	case SDLK_PLUS:
	case SDLK_KP_PLUS:
	case SDLK_EQUALS:
	case SDLK_KP_EQUALS:
		if (AutomapActive) {
			AutomapZoomIn();
		}
		return;
	case SDLK_MINUS:
	case SDLK_KP_MINUS:
	case SDLK_UNDERSCORE:
		if (AutomapActive) {
			AutomapZoomOut();
		}
		return;
#ifdef _DEBUG
	case SDLK_V:
		if ((modState & SDL_KMOD_SHIFT) != 0)
			NextDebugMonster();
		else
			GetDebugMonster();
		return;
#endif
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		if ((modState & SDL_KMOD_ALT) != 0) {
			options.Graphics.fullscreen.SetValue(!IsFullScreen());
			if (!demo::IsRunning()) SaveOptions();
		} else if (IsPlayerInStore()) {
			StoreEnter();
		} else if (QuestLogIsOpen) {
			QuestlogEnter();
		} else {
			TypeChatMessage();
		}
		return;
	case SDLK_UP:
		if (IsPlayerInStore()) {
			StoreUp();
		} else if (QuestLogIsOpen) {
			QuestlogUp();
		} else if (HelpFlag) {
			HelpScrollUp();
		} else if (ChatLogFlag) {
			ChatLogScrollUp();
		} else if (AutomapActive) {
			AutomapUp();
		} else if (IsStashOpen) {
			Stash.PreviousPage();
		}
		return;
	case SDLK_DOWN:
		if (IsPlayerInStore()) {
			StoreDown();
		} else if (QuestLogIsOpen) {
			QuestlogDown();
		} else if (HelpFlag) {
			HelpScrollDown();
		} else if (ChatLogFlag) {
			ChatLogScrollDown();
		} else if (AutomapActive) {
			AutomapDown();
		} else if (IsStashOpen) {
			Stash.NextPage();
		}
		return;
	case SDLK_PAGEUP:
		if (IsPlayerInStore()) {
			StorePrior();
		} else if (ChatLogFlag) {
			ChatLogScrollTop();
		}
		return;
	case SDLK_PAGEDOWN:
		if (IsPlayerInStore()) {
			StoreNext();
		} else if (ChatLogFlag) {
			ChatLogScrollBottom();
		}
		return;
	case SDLK_LEFT:
		if (AutomapActive && !ChatFlag)
			AutomapLeft();
		return;
	case SDLK_RIGHT:
		if (AutomapActive && !ChatFlag)
			AutomapRight();
		return;
	default:
		break;
	}
}

bool CanUseTownCamera()
{
	return leveltype == DTYPE_TOWN && MyPlayer != nullptr && !MyPlayerIsDead && MyPlayer->_pmode != PM_DEATH
	    && PauseMode != 2 && !InGameMenu() && !IsPlayerInStore() && !IsChatActive()
	    && !QuestLogIsOpen && !HelpFlag && !ChatLogFlag && !qtextflag && !DoomFlag
	    && !SpellSelectFlag && !DropGoldFlag && !IsWithdrawGoldOpen;
}

bool CanControlTownCamera(bool requireWorldPointer = true)
{
	if (!IsTownViewActive() || !CanUseTownCamera())
		return false;
	if (!requireWorldPointer)
		return true;
	if (MousePosition.x < 0 || MousePosition.x >= gnScreenWidth || MousePosition.y < 0 || MousePosition.y >= gnViewportHeight
	    || IsPointOnD3dHud(MousePosition)
	    || (IsLeftPanelOpen() && GetLeftPanel().contains(MousePosition))
	    || (IsRightPanelOpen() && GetRightPanel().contains(MousePosition)))
		return false;
	return true;
}

bool HasFirstPersonServices()
{
	return FirstPersonHasDiagnosticServices && FirstPersonServices.setRelativeMouse != nullptr
	    && FirstPersonServices.relativeMouse != nullptr && FirstPersonServices.heldArrows != nullptr
	    && FirstPersonServices.hasFocus != nullptr && FirstPersonServices.flushRelativeMouse != nullptr;
}

bool FirstPersonRelativeMouse()
{
	if (HasFirstPersonServices())
		return FirstPersonServices.relativeMouse();
#ifdef USE_SDL1
	return false;
#elif defined(USE_SDL3)
	return !HeadlessMode && ghMainWnd != nullptr && SDL_GetWindowRelativeMouseMode(ghMainWnd);
#else
	return !HeadlessMode && ghMainWnd != nullptr && SDL_GetRelativeMouseMode() == SDL_TRUE;
#endif
}

bool SetFirstPersonRelativeMouse(bool enabled)
{
	if (HasFirstPersonServices())
		return FirstPersonServices.setRelativeMouse(enabled);
#ifdef USE_SDL1
	return false;
#elif defined(USE_SDL3)
	return !HeadlessMode && ghMainWnd != nullptr && SDL_SetWindowRelativeMouseMode(ghMainWnd, enabled);
#else
	return !HeadlessMode && ghMainWnd != nullptr && SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) == 0;
#endif
}

void FlushFirstPersonMouse()
{
	if (HasFirstPersonServices()) {
		FirstPersonServices.flushRelativeMouse();
		return;
	}
	if (HeadlessMode)
		return;
#if defined(USE_SDL3)
	float x, y;
	SDL_GetRelativeMouseState(&x, &y);
	SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
#elif !defined(USE_SDL1)
	int x, y;
	SDL_GetRelativeMouseState(&x, &y);
	SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
#endif
}

bool HasReservedFirstPersonModifiers(uint16_t modifiers)
{
#ifdef USE_SDL1
	return (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0; // FPP capture is disabled on SDL1.
#else
	return (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0;
#endif
}

uint32_t NormalizedTownMovementKey(SDL_Keycode code)
{
	remap_keyboard_key(&code);
	uint32_t key = static_cast<uint32_t>(code);
	if (key >= SDLK_A && key <= SDLK_Z)
		key -= 'a' - 'A';
	return key;
}

const KeymapperOptions::Action *TownMovementAction(SDL_Keycode code)
{
	return GetOptions().Keymapper.findAction(NormalizedTownMovementKey(code), KeymapperContext::TownMovement);
}

uint32_t TownMovementPhysicalIdentity(const SDL_Event &event)
{
#ifdef USE_SDL1
	return static_cast<uint32_t>(SDLC_EventKey(event));
#else
	const auto scan = SDLC_EventScancode(event);
	return scan == SDL_SCANCODE_UNKNOWN ? (1U << 31) | static_cast<uint32_t>(SDLC_EventKey(event)) : static_cast<uint32_t>(scan);
#endif
}

bool TownMovementAllowsModifiers(const KeymapperOptions::Action &action)
{
	const uint32_t key = GetOptions().Keymapper.KeyForAction(action.key);
	return key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT;
}

uint8_t FirstPersonHeldArrows()
{
	uint8_t held = 0;
	const uint16_t modifiers = HasFirstPersonServices() ? FirstPersonMovementModifiers : SDL_GetModState();
	const bool modified = HasReservedFirstPersonModifiers(modifiers);
	if (HasFirstPersonServices() && FirstPersonServices.scancodeHeld == nullptr) {
		held = FirstPersonServices.heldArrows(); // Diagnostic bits are binding slots, not keycodes.
		if (modified) {
			uint8_t allowed = 0;
			for (const auto *entry : GetOptions().Keymapper.GetEntries()) {
				const auto &action = *static_cast<const KeymapperOptions::Action *>(entry);
				if (action.context == KeymapperContext::TownMovement && TownMovementAllowsModifiers(action))
					allowed |= action.movementBit;
			}
			held &= allowed;
		}
		return held;
	}
#ifndef USE_SDL1
	const bool syntheticScancodes = HasFirstPersonServices() && FirstPersonServices.scancodeHeld != nullptr;
	if (syntheticScancodes || !HeadlessMode) {
		int keyCount = 0;
		const auto *keys = syntheticScancodes ? nullptr : SDL_GetKeyboardState(&keyCount);
		// The event records its actual scancode before any platform/ASCII remap.
		// Poll that same identity; never pass persisted uppercase letters to SDL.
		for (const auto &owned : TownMovementOwnedKeys) {
			if (owned.epoch != TownMovementEpoch || owned.scancode <= 0)
				continue;
			const bool physicallyHeld = syntheticScancodes ? FirstPersonServices.scancodeHeld(owned.scancode)
			                                                  : owned.scancode < keyCount && keys[owned.scancode];
			if (physicallyHeld && (!modified || TownMovementAllowsModifiers(*owned.action)))
				held |= owned.action->movementBit;
		}
	}
#endif
	return held;
}

bool IsFirstPersonModeActive()
{
	return gbRunGame && leveltype == DTYPE_TOWN && IsTownViewActive()
	    && GetTownViewCameraMode() == TownCameraMode::FirstPerson;
}

bool CanUseFollowCameraInput()
{
#ifdef USE_SDL1
	return false;
#else
	if (HeadlessMode && !HasFirstPersonServices())
		return false;
	const bool focused = HasFirstPersonServices() ? FirstPersonServices.hasFocus()
	                                            : ghMainWnd != nullptr && diablo_is_focused();
	if (!focused || !gbActive || CurrentEventHandler != GameEventHandler || !gbRunGame || !IsTownViewActive()
	    || !CanUseTownCamera() || MyPlayer->hasNoLife() || MyPlayer->_pLvlChanging || !MyPlayer->isOnActiveLevel()
	    || ControlMode != ControlTypes::KeyboardAndMouse || ControlDevice != ControlTypes::KeyboardAndMouse
	    || pcurs != CURSOR_HAND || !MyPlayer->HoldItem.isEmpty() || sgnTimeoutCurs != CURSOR_NONE
	    || demo::IsRunning() || demo::IsRecording() || movie_playing || IsTownViewCameraDragging()
	    || AutomapActive || IsLeftPanelOpen() || IsRightPanelOpen() || IsDiabloMsgAvailable() || IsInGameSettingsOpen()
	    || gnScreenWidth <= 0 || gnViewportHeight <= 0)
		return false;
#ifdef _DEBUG
	if (IsConsoleOpen())
		return false;
#endif
	// A mode change must not capture the middle of a native held mouse action.
	return FirstPersonInput.phase == TownFirstPersonCapturePhase::Captured || sgbMouseDown == CLICK_NONE;
#endif
}

bool CanCaptureFirstPersonInput()
{
	return IsFirstPersonModeActive() && CanUseFollowCameraInput() && GetTownViewFollowCameraState().valid;
}

bool CanUseTownMovementKeys()
{
	return IsTownFirstPersonInputCaptured()
	    || (GetTownViewCameraMode() == TownCameraMode::ThirdPerson && CanUseFollowCameraInput()
	        && GetTownViewFollowCameraState().valid);
}

TownFirstPersonInputResult SyncThirdPersonMovement(uint8_t pressed, uint8_t released)
{
	TownFirstPersonInputResult result;
	if (GetTownViewCameraMode() != TownCameraMode::ThirdPerson || !CanUseTownMovementKeys()) {
		if (ThirdPersonMovementInitialized)
			StopTownFirstPersonWalk();
		ThirdPersonMovementInitialized = false;
		ThirdPersonMovementInput = {};
		return result;
	}
	TownFirstPersonInputSnapshot input;
	input.physicalHeld = FirstPersonHeldArrows();
	if (!ThirdPersonMovementInitialized) {
		ThirdPersonMovementInput = {};
		ThirdPersonMovementInput.blockedKeys = input.physicalHeld;
		ThirdPersonMovementInitialized = true;
	}
	// A fresh nonrepeat action is proof of a new physical episode after a gate.
	ThirdPersonMovementInput.blockedKeys &= static_cast<uint8_t>(~pressed);
	input.pressed = pressed;
	input.released = released;
	const auto camera = GetTownViewCameraState();
	input.currentYaw = camera.yaw;
	input.currentPitch = camera.pitch;
	result = StepTownCameraMovement(ThirdPersonMovementInput, input, {});
	ThirdPersonMovementInput = result.nextState;
	if (result.stopOwnWalk)
		StopTownFirstPersonWalk();
	return result;
}

void TownMovementActionPressed(uint8_t bit)
{
	if (GetTownViewCameraMode() == TownCameraMode::ThirdPerson)
		SyncThirdPersonMovement(bit);
	else
		SyncTownFirstPersonInput(bit);
}

void TownMovementActionReleased(uint8_t bit)
{
	// A platform remap can yield multiple physical keys for one action. An UP
	// cannot release that slot while another owned physical key still holds it.
	const uint8_t released = (FirstPersonHeldArrows() & bit) != 0 ? 0 : bit;
	if (GetTownViewCameraMode() == TownCameraMode::ThirdPerson)
		SyncThirdPersonMovement(0, released);
	else
		SyncTownFirstPersonInput(0, released);
}

void TownMovementBindingsChanged()
{
	SuspendTownFirstPersonInput(); // Preserves old physical UP latches, resets intent.
}

void AdvanceTownCameraForDraw()
{
	const uint32_t now = SDL_GetTicks();
	const bool eligible = CanUseFollowCameraInput();
	const float seconds = eligible && TownCameraHasFrameClock && FirstPersonDeferredClickCount == 0
	    ? static_cast<float>(now - TownCameraLastFrameTicks) / 1000.0F : 0;
	TownCameraLastFrameTicks = now;
	TownCameraHasFrameClock = eligible;
	if (AdvanceTownViewCamera(seconds))
		RedrawViewport();
}

Point FirstPersonPointerCenter()
{
	return { gnScreenWidth / 2, gnViewportHeight / 2 };
}

bool HasFirstPersonPointerPick()
{
	Point tile;
	int towner, item, player;
	// Both stale epochs after look and sky have no authoritative world target.
	// Never turn CheckCursMove's native hero-tile fallback into a captured click.
	return PickTownView(FirstPersonPointerCenter(), tile, towner, item, player);
}

void RestoreFirstPersonPointer()
{
	if (!FirstPersonHasSavedPointer)
		return;
	FirstPersonHasSavedPointer = false;
	MousePosition = { std::clamp(FirstPersonSavedPointer.x, 0, std::max(0, gnScreenWidth - 1)),
		std::clamp(FirstPersonSavedPointer.y, 0, std::max(0, gnScreenHeight - 1)) };
	// Fixtures replace platform services only; they never warp or touch a hardware cursor.
	if (!HeadlessMode && !FirstPersonHasDiagnosticServices && ghMainWnd != nullptr && diablo_is_focused()) {
		SetCursorPos(MousePosition);
		ResetCursor();
	}
}

TownFirstPersonInputResult SyncTownFirstPersonInput(uint8_t pressed, uint8_t released,
    float mouseX, float mouseY, bool escapePressed, bool resumeClick)
{
	const auto mode = GetTownViewCameraMode();
	if (mode != TownMovementLastMode) {
		++TownMovementEpoch;
		TownMovementLastMode = mode;
		ThirdPersonMovementInitialized = false;
		ThirdPersonMovementInput = {};
		StopTownFirstPersonWalk();
	}
	TownFirstPersonInputSnapshot input;
	input.firstPersonActive = IsFirstPersonModeActive();
	input.inputAllowed = CanCaptureFirstPersonInput();
	input.relativeCaptured = FirstPersonRelativeMouse();
	input.escapePressed = escapePressed;
	input.resumeClick = resumeClick;
	input.physicalHeld = FirstPersonHeldArrows();
	input.pressed = pressed;
	input.released = released;
	input.relativeMouseX = mouseX;
	input.relativeMouseY = mouseY;
	const TownViewCameraState camera = GetTownViewCameraState();
	input.currentYaw = camera.yaw;
	input.currentPitch = camera.pitch;
	TownFirstPersonInputPreferences preferences;
	preferences.radiansPerPixel = 0.006F * static_cast<float>(std::clamp(*GetOptions().Graphics.townViewCameraSensitivity, 25, 200)) / 100.0F;
	TownFirstPersonInputResult result;
	bool consumeResumeClick = false;
	// The platform operation is acknowledged separately. Bound the loop so a
	// failed OS release cannot spin or repeatedly reacquire in this input sample.
	for (unsigned acknowledgement = 0; acknowledgement < 3; ++acknowledgement) {
		const TownFirstPersonCapturePhase previousPhase = FirstPersonInput.phase;
		result = StepTownFirstPersonInput(FirstPersonInput, input, preferences);
		FirstPersonInput = result.nextState;
		consumeResumeClick |= result.consumeResumeClick;
		if (result.stopOwnWalk || result.releaseCapture)
			StopTownFirstPersonWalk();
		if (previousPhase == TownFirstPersonCapturePhase::Captured && FirstPersonInput.phase != TownFirstPersonCapturePhase::Captured) {
			FirstPersonDeferredClickCount = 0;
			// Includes observed external capture loss, which needs no release request.
			sgbMouseDown = CLICK_NONE;
			LastPlayerAction = PlayerActionType::None;
		}
		if (result.discardMouseDelta)
			FlushFirstPersonMouse();
		if (result.yawDelta != 0 || result.pitchDelta != 0) {
			OrbitTownView(result.yawDelta, result.pitchDelta);
			RedrawViewport();
		}
		if (!result.requestCapture && !result.releaseCapture)
			break;
		if (result.requestCapture) {
			FirstPersonSavedPointer = MousePosition;
			FirstPersonHasSavedPointer = true;
			ResetItemlabelHighlighted();
			FlushFirstPersonMouse();
			const bool acquired = SetFirstPersonRelativeMouse(true);
			input.relativeCaptured = FirstPersonRelativeMouse();
			input.captureFailed = !acquired || !input.relativeCaptured;
		} else {
			// These latches were created by clicks during our captured session.
			// Releasing capture cannot leave native click-and-hold repeating.
			sgbMouseDown = CLICK_NONE;
			LastPlayerAction = PlayerActionType::None;
			SetFirstPersonRelativeMouse(false);
			FlushFirstPersonMouse();
			input.relativeCaptured = FirstPersonRelativeMouse();
			if (!input.relativeCaptured)
				RestoreFirstPersonPointer();
		}
		input.pressed = 0;
		input.released = 0;
		input.relativeMouseX = 0;
		input.relativeMouseY = 0;
		input.escapePressed = false;
		input.resumeClick = false;
	}
	result.consumeResumeClick = consumeResumeClick;
	if (FirstPersonInput.phase == TownFirstPersonCapturePhase::Released && !input.relativeCaptured)
		RestoreFirstPersonPointer();
	if (result.active)
		MousePosition = FirstPersonPointerCenter();
	SyncThirdPersonMovement();
	return result;
}

bool ConsumeFirstPersonMouseDown(Uint8 button)
{
	const uint32_t mask = button > 0 && button <= 31 ? 1U << (button - 1) : 0;
	if (IsTownFirstPersonInputCaptured()) {
		if (button != SDL_BUTTON_MIDDLE)
			return false;
		FirstPersonConsumedButtons |= mask;
		return true;
	}
	if ((button != SDL_BUTTON_LEFT && button != SDL_BUTTON_RIGHT) || !CanControlTownCamera()
	    || !CanCaptureFirstPersonInput())
		return false;
	const TownFirstPersonInputResult result = SyncTownFirstPersonInput(0, 0, 0, 0, false, true);
	if (!result.consumeResumeClick)
		return false;
	FirstPersonConsumedButtons |= mask;
	return true;
}

void ReleaseTownCameraDrag()
{
	EndTownViewCameraDrag();
#if SDL_VERSION_ATLEAST(2, 0, 4) && !SDL_VERSION_ATLEAST(3, 0, 0)
	SDL_CaptureMouse(SDL_FALSE);
#elif SDL_VERSION_ATLEAST(3, 0, 0)
	SDL_CaptureMouse(false);
#endif
}

void HandleMouseButtonDown(Uint8 button, uint16_t modState)
{
	// Its previous up may have occurred outside this window after focus loss.
	// A fresh down begins a new pair and cannot inherit an old consumed-up latch.
	const uint32_t firstPersonMask = button > 0 && button <= 31 ? 1U << (button - 1) : 0;
	FirstPersonConsumedButtons &= ~firstPersonMask;
	if (ConsumeFirstPersonMouseDown(button))
		return;
	if (IsTownViewCameraDragging())
		return;
	if (button == SDL_BUTTON_MIDDLE && sgbMouseDown == CLICK_NONE && CanControlTownCamera()
	    && BeginTownViewCameraDrag(MousePosition, (modState & SDL_KMOD_SHIFT) != 0)) {
#if SDL_VERSION_ATLEAST(2, 0, 4) && !SDL_VERSION_ATLEAST(3, 0, 0)
		SDL_CaptureMouse(SDL_TRUE);
#elif SDL_VERSION_ATLEAST(3, 0, 0)
		SDL_CaptureMouse(true);
#endif
		return;
	}
	if (IsPlayerInStore() && (button == SDL_BUTTON_X1
#if !SDL_VERSION_ATLEAST(2, 0, 0)
	        || button == 8
#endif
	        )) {
		StoreESC();
		return;
	}

	switch (button) {
	case SDL_BUTTON_LEFT:
		if (sgbMouseDown == CLICK_NONE) {
			sgbMouseDown = CLICK_LEFT;
			LeftMouseDown(modState);
		}
		break;
	case SDL_BUTTON_RIGHT:
		if (sgbMouseDown == CLICK_NONE) {
			sgbMouseDown = CLICK_RIGHT;
			RightMouseDown((modState & SDL_KMOD_SHIFT) != 0);
		}
		break;
	default:
		KeymapperPress(static_cast<SDL_Keycode>(button | KeymapperMouseButtonMask));
		break;
	}
}

void HandleMouseButtonUp(Uint8 button, uint16_t modState)
{
	const uint32_t firstPersonMask = button > 0 && button <= 31 ? 1U << (button - 1) : 0;
	if ((FirstPersonConsumedButtons & firstPersonMask) != 0) {
		FirstPersonConsumedButtons &= ~firstPersonMask;
		return;
	}
	if (button == SDL_BUTTON_MIDDLE && IsTownViewCameraDragging()) {
		ReleaseTownCameraDrag();
		return;
	}
	if (sgbMouseDown == CLICK_LEFT && button == SDL_BUTTON_LEFT) {
		LastPlayerAction = PlayerActionType::None;
		sgbMouseDown = CLICK_NONE;
		LeftMouseUp(modState);
	} else if (sgbMouseDown == CLICK_RIGHT && button == SDL_BUTTON_RIGHT) {
		LastPlayerAction = PlayerActionType::None;
		sgbMouseDown = CLICK_NONE;
	} else {
		KeymapperRelease(static_cast<SDL_Keycode>(button | KeymapperMouseButtonMask));
	}
}

bool HasDeferredFirstPersonDown(Uint8 button)
{
	for (size_t i = 0; i < FirstPersonDeferredClickCount; ++i)
		if (FirstPersonDeferredClicks[i].button == button && FirstPersonDeferredClicks[i].down)
			return true;
	return false;
}

void QueueTownFirstPersonClick(Uint8 button, uint16_t modifiers, bool down, bool absolute = false)
{
	const uint32_t mask = 1U << (button - 1);
	if (down) {
		FirstPersonConsumedButtons |= mask;
		StopTownFirstPersonWalk();
	}
	if (FirstPersonDeferredClickCount == FirstPersonDeferredClicks.size()) {
		SuspendTownFirstPersonInput();
		return;
	}
	FirstPersonDeferredClicks[FirstPersonDeferredClickCount++] = { button, modifiers, down, 0, 0, 0, absolute, MousePosition };
	RedrawViewport();
}

void QueueTownFirstPersonLook(float x, float y)
{
	if (FirstPersonDeferredClickCount != 0 && FirstPersonDeferredClicks[FirstPersonDeferredClickCount - 1].button == 0
	    && FirstPersonDeferredClicks[FirstPersonDeferredClickCount - 1].wheelSteps == 0
	    && ((FirstPersonDeferredClicks[FirstPersonDeferredClickCount - 1].lookY >= 0 && y >= 0)
	        || (FirstPersonDeferredClicks[FirstPersonDeferredClickCount - 1].lookY <= 0 && y <= 0))) {
		auto &look = FirstPersonDeferredClicks[FirstPersonDeferredClickCount - 1];
		look.lookX += x;
		look.lookY += y;
		return;
	}
	if (FirstPersonDeferredClickCount == FirstPersonDeferredClicks.size()) {
		SuspendTownFirstPersonInput();
		return;
	}
	FirstPersonDeferredClicks[FirstPersonDeferredClickCount++] = { 0, 0, false, x, y };
}

void FlushTownFirstPersonClicks()
{
	TownFollowWheelNeedsDraw = false; // Caller has drawn the current camera/picking epoch.
	if (FirstPersonDeferredClickCount == 0)
		return;
	const auto pending = FirstPersonDeferredClicks;
	const size_t count = FirstPersonDeferredClickCount;
	FirstPersonDeferredClickCount = 0;
	SyncTownFirstPersonInput();
	// DrawAndBlit has refreshed picking. Sky/failed draws never become a walk
	// to the hero's fallback tile; there is no synchronous render per click.
	if (IsTownFirstPersonInputCaptured())
		MousePosition = FirstPersonPointerCenter();
	bool lookedSinceDraw = false;
	for (size_t i = 0; i < count; ++i) {
		const auto &click = pending[i];
		if (click.wheelSteps != 0) {
			if (!CanUseFollowCameraInput())
				continue; // Drop only this ineligible wheel; matching mouse ups must still run.
			ZoomTownView(click.wheelSteps);
			TownFollowWheelNeedsDraw = true;
			SyncTownFirstPersonInput();
			lookedSinceDraw = true;
			RedrawViewport();
			continue;
		}
		if (click.absoluteClick && click.down) {
			if (lookedSinceDraw) {
				for (size_t rest = i; rest < count; ++rest)
					FirstPersonDeferredClicks[FirstPersonDeferredClickCount++] = pending[rest];
				RedrawViewport();
				return;
			}
			if (!IsTownFirstPersonInputCaptured() && CanUseFollowCameraInput()) {
				MousePosition = click.pointer;
				Point tile;
				int towner, item, player;
				if (PickTownView(MousePosition, tile, towner, item, player)) {
					CheckCursMove();
					HandleMouseButtonDown(click.button, click.modifiers);
				}
			}
			continue;
		}
		if (!IsTownFirstPersonInputCaptured()) {
			if (click.button != 0 && !click.down)
				HandleMouseButtonUp(click.button, click.modifiers);
			continue;
		}
		if (click.button == 0) {
			SyncTownFirstPersonInput(0, 0, click.lookX, click.lookY);
			lookedSinceDraw = true;
			continue;
		}
		if (click.down && lookedSinceDraw) {
			// Preserve input order: a later click gets the next rendered pose,
			// never the target from before its intervening mouse motion.
			for (size_t rest = i; rest < count; ++rest)
				FirstPersonDeferredClicks[FirstPersonDeferredClickCount++] = pending[rest];
			RedrawViewport();
			return;
		}
		if (click.down && !HasFirstPersonPointerPick())
			continue; // Fresh sky/no-target: keep its up consumed, apply later look.
		if (click.down)
			HandleMouseButtonDown(click.button, click.modifiers);
		else
			HandleMouseButtonUp(click.button, click.modifiers);
		SyncTownFirstPersonInput();
		// Subsequent wheel events may cross back into FPP. Other events still
		// consult capture/UI eligibility; matching ups only clear owned latches.
	}
}

[[maybe_unused]] void LogUnhandledEvent(const char *name, int value)
{
	LogVerbose("Unhandled SDL event: {} {}", name, value);
}

void PrepareForFadeIn()
{
	if (HeadlessMode) return;
	BlackPalette();

	// Render the game to the buffer(s) with a fully black palette.
	// Palette fade-in will gradually make it visible.
	RedrawEverything();
	while (IsRedrawEverything()) {
		DrawAndBlit();
	}
}

void GameEventHandler(const SDL_Event &event, uint16_t modState)
{
	FirstPersonMovementModifiers = modState;
	// Every early UI/controller return still observes any changed input gate.
	struct SyncAfterEvent {
		~SyncAfterEvent() { SyncTownFirstPersonInput(); }
	} syncAfterEvent;
#if SDL_VERSION_ATLEAST(3, 0, 0)
	if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
		SuspendTownFirstPersonInput();
	if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
		SuspendTownFirstPersonInput();
		ReleaseTownCameraDrag();
	}
#elif SDL_VERSION_ATLEAST(2, 0, 0)
	if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
		SuspendTownFirstPersonInput();
	if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
		SuspendTownFirstPersonInput();
		ReleaseTownCameraDrag();
	}
#endif
	if (event.type == SDL_EVENT_KEY_DOWN && SDLC_EventKey(event) == SDLK_ESCAPE) {
		SuspendTownFirstPersonInput();
		ReleaseTownCameraDrag();
	}
	const TownFirstPersonInputResult synchronizedInput = SyncTownFirstPersonInput();
	// Deliver ups before text/menu/controller handlers can consume them. The
	// latch survives suspension so its matching up cannot invoke a native binding.
	if (event.type == SDL_EVENT_KEY_UP) {
		const uint32_t identity = TownMovementPhysicalIdentity(event);
		const auto owned = std::find_if(TownMovementOwnedKeys.begin(), TownMovementOwnedKeys.end(),
		    [identity](const auto &key) { return key.identity == identity; });
		if (owned != TownMovementOwnedKeys.end()) {
			const auto episode = *owned;
			TownMovementOwnedKeys.erase(owned);
			if (episode.epoch == TownMovementEpoch && episode.action->actionReleased)
				episode.action->actionReleased(); // Original action, even after remap/UI.
			return;
		}
	}
	if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
		const Uint8 button = event.button.button;
		if ((button == SDL_BUTTON_LEFT || button == SDL_BUTTON_RIGHT) && HasDeferredFirstPersonDown(button)) {
			QueueTownFirstPersonClick(button, modState, false);
			return;
		}
		const uint32_t mask = button > 0 && button <= 31 ? 1U << (button - 1) : 0;
		if ((FirstPersonConsumedButtons & mask) != 0) {
			HandleMouseButtonUp(button, modState);
			return;
		}
	}
	if (event.type == SDL_EVENT_KEY_DOWN) {
		bool repeat = false;
#if SDL_VERSION_ATLEAST(2, 0, 0)
		repeat = event.key.repeat;
#endif
		const uint32_t identity = TownMovementPhysicalIdentity(event);
		const auto owned = std::find_if(TownMovementOwnedKeys.begin(), TownMovementOwnedKeys.end(),
		    [identity](const auto &key) { return key.identity == identity; });
		if (repeat && owned != TownMovementOwnedKeys.end())
			return; // Owned repeat stays consumed through modifiers/UI/remapping.
		if (!repeat && owned != TownMovementOwnedKeys.end())
			TownMovementOwnedKeys.erase(owned); // Fresh DOWN replaces a missed external UP.

		// Unowned repeats still belong to the native DOWN, including modified keys.
	}
	if (event.type == SDL_EVENT_MOUSE_MOTION && IsTownFirstPersonInputCaptured()) {
		if (!synchronizedInput.discardMouseDelta) {
			if (FirstPersonDeferredClickCount != 0)
				QueueTownFirstPersonLook(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
			else
				SyncTownFirstPersonInput(0, 0, static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
		}
		return;
	}
	[[maybe_unused]] const Options &options = GetOptions();
	const bool wasNonKeyboardDevice = ControlMode != ControlTypes::KeyboardAndMouse || ControlDevice != ControlTypes::KeyboardAndMouse;
	StaticVector<ControllerButtonEvent, 4> ctrlEvents = ToControllerButtonEvents(event);
	for (const ControllerButtonEvent ctrlEvent : ctrlEvents) {
		GameAction action;
		if (HandleControllerButtonEvent(event, ctrlEvent, action) && action.type == GameActionType_SEND_KEY) {
			if ((action.send_key.vk_code & KeymapperMouseButtonMask) != 0) {
				const unsigned button = action.send_key.vk_code & ~KeymapperMouseButtonMask;
				if (!action.send_key.up)
					HandleMouseButtonDown(static_cast<Uint8>(button), modState);
				else
					HandleMouseButtonUp(static_cast<Uint8>(button), modState);
			} else {
				if (!action.send_key.up)
					PressKey(static_cast<SDL_Keycode>(action.send_key.vk_code), modState);
				else
					ReleaseKey(static_cast<SDL_Keycode>(action.send_key.vk_code));
			}
		}
	}
	if (ctrlEvents.size() > 0 && ctrlEvents[0].button != ControllerButton_NONE) {
		return;
	}

#ifdef _DEBUG
	if (ConsoleHandleEvent(event)) {
		return;
	}
#endif

	if (IsChatActive() && HandleTalkTextInputEvent(event)) {
		return;
	}
	if (DropGoldFlag && HandleGoldDropTextInputEvent(event)) {
		return;
	}
	if (IsWithdrawGoldOpen && HandleGoldWithdrawTextInputEvent(event)) {
		return;
	}

	// Native controller translation/DetectInputMethod above must classify this
	// event before movement eligibility. KBCTRL and gamepad retain their order.
	if (event.type == SDL_EVENT_KEY_DOWN) {
		bool repeat = false;
#if SDL_VERSION_ATLEAST(2, 0, 0)
		repeat = event.key.repeat;
#endif
		const auto *movement = TownMovementAction(SDLC_EventKey(event));
		const bool modified = HasReservedFirstPersonModifiers(modState);
		const bool keyboardTakeover = wasNonKeyboardDevice
		    && ControlMode == ControlTypes::KeyboardAndMouse && ControlDevice == ControlTypes::KeyboardAndMouse;
		const bool resumeFromDevice = !repeat && keyboardTakeover && movement != nullptr
		    && (!modified || TownMovementAllowsModifiers(*movement)) && CanCaptureFirstPersonInput();
		SyncTownFirstPersonInput(0, 0, 0, 0, false, resumeFromDevice);
		if (resumeFromDevice && IsTownFirstPersonInputCaptured())
			FirstPersonInput.blockedKeys &= static_cast<uint8_t>(~movement->movementBit);
		const uint32_t identity = TownMovementPhysicalIdentity(event);
		const auto *action = TownMovementAction(SDLC_EventKey(event));
		if (!repeat && action != nullptr && (action->isEnabled() || resumeFromDevice) && (!modified || TownMovementAllowsModifiers(*action))) {
#ifdef USE_SDL1
			const int scan = 0;
#else
			const int scan = static_cast<int>(SDLC_EventScancode(event));
#endif
			TownMovementOwnedKeys.push_back({ identity, scan, action, TownMovementEpoch });
			if (!action->isEnabled())
				return; // Failed explicit takeover capture still owns its DOWN/UP; no native S leak.
			// Use the native action's enable/pressed callback; never a parallel key switch.
			const SDL_Keycode normalized = static_cast<SDL_Keycode>(NormalizedTownMovementKey(SDLC_EventKey(event)));
			if (KeymapperPress(normalized, KeymapperContext::TownMovement))
				return;
			TownMovementOwnedKeys.pop_back();
		}
	}

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
#if SDL_VERSION_ATLEAST(2, 0, 0)
		if (event.key.repeat) {
			SDL_Keycode code = SDLC_EventKey(event);
			remap_keyboard_key(&code);
			uint32_t key = static_cast<uint32_t>(code);
			// Match the letter normalization in KeymapperPress.
			if (key >= SDLK_A && key <= SDLK_Z)
				key -= 'a' - 'A';
			const auto *action = options.Keymapper.findAction(key);
			if (action != nullptr && (action->key == "ToggleTown3D" || action->key == "Town3DCameraMode"))
				return;
		}
#endif
		PressKey(SDLC_EventKey(event), modState);
		return;
	case SDL_EVENT_KEY_UP:
		ReleaseKey(SDLC_EventKey(event));
		return;
	case SDL_EVENT_MOUSE_MOTION:
		if (ControlMode == ControlTypes::KeyboardAndMouse && invflag)
			InvalidateInventorySlot();
		MousePosition = GetTownFirstPersonPointer({ SDLC_EventMotionIntX(event), SDLC_EventMotionIntY(event) });
		if (IsTownViewCameraDragging()) {
			if (!CanControlTownCamera(false))
				ReleaseTownCameraDrag();
			else if (UpdateTownViewCameraDrag(MousePosition))
				RedrawViewport();
			return;
		}
		gmenu_on_mouse_move();
		return;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		MousePosition = GetTownFirstPersonPointer({ SDLC_EventButtonIntX(event), SDLC_EventButtonIntY(event) });
		if (GetTownViewFollowCameraState().active && !GetTownViewFollowCameraState().valid
		    && CanControlTownCamera() && (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
			FirstPersonConsumedButtons |= 1U << (event.button.button - 1);
			return; // Closed follow frame has no valid native world target; UI stays native.
		}
		if (!IsTownFirstPersonInputCaptured() && CanControlTownCamera() && CanUseFollowCameraInput()
		    && GetTownViewCameraMode() == TownCameraMode::ThirdPerson
		    && (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
			Point tile;
			int towner, item, player;
			if (TownFollowWheelNeedsDraw || FirstPersonDeferredClickCount != 0 || !PickTownView(MousePosition, tile, towner, item, player)) {
				QueueTownFirstPersonClick(event.button.button, modState, true, true);
				return;
			}
			CheckCursMove(); // An absolute follow click never reuses a stale hover target.
		}
		if (IsTownFirstPersonInputCaptured()
		    && (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)
		    && (FirstPersonDeferredClickCount != 0 || (sgbMouseDown == CLICK_NONE && !HasFirstPersonPointerPick()))) {
			QueueTownFirstPersonClick(event.button.button, modState, true);
			return;
		}
		HandleMouseButtonDown(event.button.button, modState);
		return;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		MousePosition = GetTownFirstPersonPointer({ SDLC_EventButtonIntX(event), SDLC_EventButtonIntY(event) });
		HandleMouseButtonUp(event.button.button, modState);
		return;
#if SDL_VERSION_ATLEAST(2, 0, 0)
	case SDL_EVENT_MOUSE_WHEEL:
		if (CanControlTownCamera() && (modState & SDL_KMOD_CTRL) == 0
		    && ((GetTownViewCameraMode() != TownCameraMode::ThirdPerson && GetTownViewCameraMode() != TownCameraMode::FirstPerson)
		        || CanUseFollowCameraInput())) {
			float steps = static_cast<float>(SDLC_EventWheelIntY(event));
#if SDL_VERSION_ATLEAST(3, 0, 0)
			steps = event.wheel.y;
#elif SDL_VERSION_ATLEAST(2, 0, 18)
			if (event.wheel.preciseY != 0)
				steps = event.wheel.preciseY;
#endif
			if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
				steps = -steps;
			if (std::isfinite(steps) && steps != 0) {
				TownFollowWheelNeedsDraw = GetTownViewCameraMode() == TownCameraMode::ThirdPerson
				    || GetTownViewCameraMode() == TownCameraMode::FirstPerson;
				if (FirstPersonDeferredClickCount != 0) {
					if (FirstPersonDeferredClickCount == FirstPersonDeferredClicks.size()) {
						SuspendTownFirstPersonInput();
						return;
					}
					FirstPersonDeferredClicks[FirstPersonDeferredClickCount++] = { 0, 0, false, 0, 0, steps };
				} else {
					ZoomTownView(steps);
					SyncTownFirstPersonInput();
				}
				RedrawViewport();
				return;
			}
		}
		if (SDLC_EventWheelIntY(event) > 0) { // Up
			if (IsPlayerInStore()) {
				StoreUp();
			} else if (QuestLogIsOpen) {
				QuestlogUp();
			} else if (HelpFlag) {
				HelpScrollUp();
			} else if (ChatLogFlag) {
				ChatLogScrollUp();
			} else if (IsStashOpen) {
				Stash.PreviousPage();
			} else if (SDL_GetModState() & SDL_KMOD_CTRL) {
				if (AutomapActive) {
					AutomapZoomIn();
				}
			} else {
				KeymapperPress(MouseScrollUpButton);
			}
		} else if (SDLC_EventWheelIntY(event) < 0) { // down
			if (IsPlayerInStore()) {
				StoreDown();
			} else if (QuestLogIsOpen) {
				QuestlogDown();
			} else if (HelpFlag) {
				HelpScrollDown();
			} else if (ChatLogFlag) {
				ChatLogScrollDown();
			} else if (IsStashOpen) {
				Stash.NextPage();
			} else if (SDL_GetModState() & SDL_KMOD_CTRL) {
				if (AutomapActive) {
					AutomapZoomOut();
				}
			} else {
				KeymapperPress(MouseScrollDownButton);
			}
		} else if (SDLC_EventWheelIntX(event) > 0) { // left
			KeymapperPress(MouseScrollLeftButton);
		} else if (SDLC_EventWheelIntX(event) < 0) { // right
			KeymapperPress(MouseScrollRightButton);
		}
		break;
#endif
	default:
		if (IsCustomEvent(event.type)) {
			if (gbIsMultiplayer)
				pfile_write_hero();
			nthread_ignore_mutex(true);
			PaletteFadeOut(8);
			sound_stop();
			ShowProgress(GetCustomEvent(event));

			PrepareForFadeIn();
			LoadPWaterPalette();
			if (gbRunGame)
				PaletteFadeIn(8);
			nthread_ignore_mutex(false);
			gbGameLoopStartup = true;
			return;
		}
		MainWndProc(event);
		break;
	}
}

void RunGameLoop(interface_mode uMsg)
{
	demo::NotifyGameLoopStart();
	music_set_game_context(true);

	nthread_ignore_mutex(true);
	StartGame(uMsg);
	assert(HeadlessMode || ghMainWnd);
	EventHandler previousHandler = SetEventHandler(GameEventHandler);
	run_delta_info();
	gbRunGame = true;
	gbProcessPlayers = IsDiabloAlive(true);
	gbRunGameResult = true;

	PrepareForFadeIn();
	LoadPWaterPalette();
	PaletteFadeIn(8);
	InitBackbufferState();
	RedrawEverything();
	gbGameLoopStartup = true;
	nthread_ignore_mutex(false);

	discord_manager::StartGame();
	lua::GameStart();
#ifdef GPERF_HEAP_FIRST_GAME_ITERATION
	unsigned run_game_iteration = 0;
#endif

	while (gbRunGame) {
		SyncTownFirstPersonInput();

#ifdef _DEBUG
		if (!gbGameLoopStartup && !DebugCmdsFromCommandLine.empty()) {
			InitConsole();
			for (const std::string &cmd : DebugCmdsFromCommandLine) {
				RunInConsole(cmd);
			}
			DebugCmdsFromCommandLine.clear();
		}
#else
		if (gbIsMultiplayer && IsAssetIntegrityViolated) {
			app_fatal(_("Cannot play Multiplayer with overridden *.lua, *.tsv, or *.sol assets."));
		}
#endif

		SDL_Event event;
		uint16_t modState;
		while (FetchMessage(&event, &modState)) {
			if (event.type == SDL_EVENT_QUIT) {
				gbRunGameResult = false;
				gbRunGame = false;
				break;
			}
			HandleMessage(event, modState);
			SyncTownFirstPersonInput();
		}
		SyncTownFirstPersonInput();
		if (!gbRunGame)
			break;
		music_update();

		bool drawGame = true;
		bool processInput = true;
		const bool runGameLoop = demo::IsRunning() ? demo::GetRunGameLoop(drawGame, processInput) : nthread_has_500ms_passed(&drawGame);
		if (demo::IsRecording())
			demo::RecordGameLoopResult(runGameLoop);

		discord_manager::UpdateGame();

		if (!runGameLoop) {
			if (processInput)
				ProcessInput();
			DvlNet_ProcessNetworkPackets();
			if (!drawGame)
				continue;
			RedrawViewport();
			AdvanceTownCameraForDraw();
			DrawAndBlit();
			FlushTownFirstPersonClicks();
			continue;
		}

		ProcessGameMessagePackets();
		this_sdl_thread::yield();
		if (game_loop(gbGameLoopStartup))
			diablo_color_cyc_logic();
		gbGameLoopStartup = false;
		if (drawGame) {
			AdvanceTownCameraForDraw();
			DrawAndBlit();
			FlushTownFirstPersonClicks();
		}
#ifdef GPERF_HEAP_FIRST_GAME_ITERATION
		if (run_game_iteration++ == 0)
			HeapProfilerDump("first_game_iteration");
#endif
	}

	demo::NotifyGameLoopEnd();
	SuspendTownFirstPersonInput();

	if (gbIsMultiplayer) {
		pfile_write_hero(/*writeGameData=*/false);
		sfile_write_stash();
	}

	PaletteFadeOut(8);
	NewCursor(CURSOR_NONE);
	ClearScreenBuffer();
	RedrawEverything();
	scrollrt_draw_game_screen();
	CloseInGameSettings();
	previousHandler = SetEventHandler(previousHandler);
	assert(HeadlessMode || previousHandler == GameEventHandler);
	FreeGame();

	if (cineflag) {
		cineflag = false;
		DoEnding();
	}
}

void PrintWithRightPadding(std::string_view str, size_t width)
{
	printInConsole(str);
	if (str.size() >= width)
		return;
	printInConsole(std::string(width - str.size(), ' '));
}

void PrintHelpOption(std::string_view flags, std::string_view description)
{
	printInConsole("    ");
	PrintWithRightPadding(flags, 20);
	printInConsole(" ");
	PrintWithRightPadding(description, 30);
	printNewlineInConsole();
}

#if SDL_VERSION_ATLEAST(2, 0, 0)
FILE *SdlLogFile = nullptr;

extern "C" void SdlLogToFile(void *userdata, int /*category*/, SDL_LogPriority priority, const char *message)
{
	FILE *file = reinterpret_cast<FILE *>(userdata);
	static const char *const LogPriorityPrefixes[SDL_LOG_PRIORITY_COUNT] = {
		"",
		"VERBOSE",
		"DEBUG",
		"INFO",
		"WARN",
		"ERROR",
		"CRITICAL"
	};
	std::fprintf(file, "%s: %s\n", LogPriorityPrefixes[priority], message);
	std::fflush(file);
}
#endif

[[noreturn]] void PrintHelpAndExit()
{
	printInConsole((/* TRANSLATORS: Commandline Option */ "Options:"));
	printNewlineInConsole();
	PrintHelpOption("-h, --help", _(/* TRANSLATORS: Commandline Option */ "Print this message and exit"));
	PrintHelpOption("--version", _(/* TRANSLATORS: Commandline Option */ "Print the version and exit"));
	PrintHelpOption("--data-dir", _(/* TRANSLATORS: Commandline Option */ "Specify the folder of diabdat.mpq"));
	PrintHelpOption("--save-dir", _(/* TRANSLATORS: Commandline Option */ "Specify the folder of save files"));
	PrintHelpOption("--config-dir", _(/* TRANSLATORS: Commandline Option */ "Specify the location of diablo.ini"));
	PrintHelpOption("--lang", _(/* TRANSLATORS: Commandline Option */ "Specify the language code (e.g. en or pt_BR)"));
	PrintHelpOption("-n", _(/* TRANSLATORS: Commandline Option */ "Skip startup videos"));
	PrintHelpOption("-f", _(/* TRANSLATORS: Commandline Option */ "Display frames per second"));
	PrintHelpOption("--verbose", _(/* TRANSLATORS: Commandline Option */ "Enable verbose logging"));
#if SDL_VERSION_ATLEAST(2, 0, 0)
	PrintHelpOption("--log-to-file <path>", _(/* TRANSLATORS: Commandline Option */ "Log to a file instead of stderr"));
#endif
#ifndef DISABLE_DEMOMODE
	PrintHelpOption("--record <#>", _(/* TRANSLATORS: Commandline Option */ "Record a demo file"));
	PrintHelpOption("--demo <#>", _(/* TRANSLATORS: Commandline Option */ "Play a demo file"));
	PrintHelpOption("--timedemo", _(/* TRANSLATORS: Commandline Option */ "Disable all frame limiting during demo playback"));
#endif
	printNewlineInConsole();
	printInConsole(_(/* TRANSLATORS: Commandline Option */ "Game selection:"));
	printNewlineInConsole();
	PrintHelpOption("--spawn", _(/* TRANSLATORS: Commandline Option */ "Force Shareware mode"));
	PrintHelpOption("--diablo", _(/* TRANSLATORS: Commandline Option */ "Force Diablo mode"));
	PrintHelpOption("--hellfire", _(/* TRANSLATORS: Commandline Option */ "Force Hellfire mode"));
	printInConsole(_(/* TRANSLATORS: Commandline Option */ "Hellfire options:"));
	printNewlineInConsole();
#ifdef _DEBUG
	printNewlineInConsole();
	printInConsole("Debug options:");
	printNewlineInConsole();
	PrintHelpOption("-i", "Ignore network timeout");
	PrintHelpOption("+<internal command>", "Pass commands to the engine");
#endif
	printNewlineInConsole();
	printInConsole(_("Report bugs at https://github.com/diasurgical/devilutionX/"));
	printNewlineInConsole();
	diablo_quit(0);
}

void PrintFlagMessage(std::string_view flag, std::string_view message)
{
	printInConsole(flag);
	printInConsole(message);
	printNewlineInConsole();
}

void PrintFlagRequiresArgument(std::string_view flag)
{
	PrintFlagMessage(flag, " requires an argument");
}

void DiabloParseFlags(int argc, char **argv)
{
#ifdef _DEBUG
	int argumentIndexOfLastCommandPart = -1;
	std::string currentCommand;
#endif
#ifndef DISABLE_DEMOMODE
	bool timedemo = false;
	int demoNumber = -1;
	int recordNumber = -1;
	bool createDemoReference = false;
#endif
	for (int i = 1; i < argc; i++) {
		const std::string_view arg = argv[i];
		if (arg == "-h" || arg == "--help") {
			PrintHelpAndExit();
		} else if (arg == "--version") {
			printInConsole(PROJECT_NAME);
			printInConsole(" v");
			printInConsole(PROJECT_VERSION);
			printNewlineInConsole();
			diablo_quit(0);
		} else if (arg == "--data-dir") {
			if (i + 1 == argc) {
				PrintFlagRequiresArgument("--data-dir");
				diablo_quit(64);
			}
			paths::SetBasePath(argv[++i]);
		} else if (arg == "--save-dir") {
			if (i + 1 == argc) {
				PrintFlagRequiresArgument("--save-dir");
				diablo_quit(64);
			}
			paths::SetPrefPath(argv[++i]);
		} else if (arg == "--config-dir") {
			if (i + 1 == argc) {
				PrintFlagRequiresArgument("--config-dir");
				diablo_quit(64);
			}
			paths::SetConfigPath(argv[++i]);
		} else if (arg == "--lang") {
			if (i + 1 == argc) {
				PrintFlagRequiresArgument("--lang");
				diablo_quit(64);
			}
			forceLocale = argv[++i];
#ifndef DISABLE_DEMOMODE
		} else if (arg == "--demo") {
			if (i + 1 == argc) {
				PrintFlagRequiresArgument("--demo");
				diablo_quit(64);
			}
			ParseIntResult<int> parsedParam = ParseInt<int>(argv[++i]);
			if (!parsedParam.has_value()) {
				PrintFlagMessage("--demo", " must be a number");
				diablo_quit(64);
			}
			demoNumber = parsedParam.value();
			gbShowIntro = false;
		} else if (arg == "--timedemo") {
			timedemo = true;
		} else if (arg == "--record") {
			if (i + 1 == argc) {
				PrintFlagRequiresArgument("--record");
				diablo_quit(64);
			}
			ParseIntResult<int> parsedParam = ParseInt<int>(argv[++i]);
			if (!parsedParam.has_value()) {
				PrintFlagMessage("--record", " must be a number");
				diablo_quit(64);
			}
			recordNumber = parsedParam.value();
		} else if (arg == "--create-reference") {
			createDemoReference = true;
#else
		} else if (arg == "--demo" || arg == "--timedemo" || arg == "--record" || arg == "--create-reference") {
			printInConsole("Binary compiled without demo mode support.");
			printNewlineInConsole();
			diablo_quit(1);
#endif
		} else if (arg == "-n") {
			gbShowIntro = false;
		} else if (arg == "-f") {
			EnableFrameCount();
		} else if (arg == "--spawn") {
			forceSpawn = true;
		} else if (arg == "--diablo") {
			forceDiablo = true;
		} else if (arg == "--hellfire") {
			forceHellfire = true;
		} else if (arg == "--vanilla") {
			gbVanilla = true;
		} else if (arg == "--verbose") {
			SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
#if SDL_VERSION_ATLEAST(2, 0, 0)
		} else if (arg == "--log-to-file") {
			if (i + 1 == argc) {
				PrintFlagRequiresArgument("--log-to-file");
				diablo_quit(64);
			}
			SdlLogFile = OpenFile(argv[++i], "wb");
			if (SdlLogFile == nullptr) {
				printInConsole("Failed to open log file for writing");
				diablo_quit(64);
			}
			SDL_SetLogOutputFunction(&SdlLogToFile, /*userdata=*/SdlLogFile);
#endif
#ifdef _DEBUG
		} else if (arg == "-i") {
			DebugDisableNetworkTimeout = true;
		} else if (arg[0] == '+') {
			if (!currentCommand.empty())
				DebugCmdsFromCommandLine.push_back(currentCommand);
			argumentIndexOfLastCommandPart = i;
			currentCommand = arg.substr(1);
		} else if (arg[0] != '-' && (argumentIndexOfLastCommandPart + 1) == i) {
			currentCommand.append(" ");
			currentCommand.append(arg);
			argumentIndexOfLastCommandPart = i;
#endif
		} else {
			printInConsole("unrecognized option '");
			printInConsole(argv[i]);
			printInConsole("'");
			printNewlineInConsole();
			PrintHelpAndExit();
		}
	}

#ifdef _DEBUG
	if (!currentCommand.empty())
		DebugCmdsFromCommandLine.push_back(currentCommand);
#endif

#ifndef DISABLE_DEMOMODE
	if (demoNumber != -1)
		demo::InitPlayBack(demoNumber, timedemo);
	if (recordNumber != -1)
		demo::InitRecording(recordNumber, createDemoReference);
#endif
}

void DiabloInitScreen()
{
	MousePosition = { gnScreenWidth / 2, gnScreenHeight / 2 };
	if (ControlMode == ControlTypes::KeyboardAndMouse)
		SetCursorPos(MousePosition);

	ClrDiabloMsg();
}

void SetApplicationVersions()
{
	*BufCopy(gszProductName, PROJECT_NAME, " v", PROJECT_VERSION) = '\0';
	*BufCopy(gszVersionNumber, "version ", PROJECT_VERSION) = '\0';
}

void CheckArchivesUpToDate()
{
	const bool devilutionxMpqOutOfDate = IsDevilutionXMpqOutOfDate();
	const bool fontsMpqOutOfDate = AreExtraFontsOutOfDate();

	if (devilutionxMpqOutOfDate && fontsMpqOutOfDate) {
		app_fatal(_("Please update devilutionx.mpq and fonts.mpq to the latest version"));
	} else if (devilutionxMpqOutOfDate) {
		app_fatal(_("Failed to load UI resources.\n"
		            "\n"
		            "Make sure devilutionx.mpq is in the game folder and that it is up to date."));
	} else if (fontsMpqOutOfDate) {
		app_fatal(_("Please update fonts.mpq to the latest version"));
	}
}

void ApplicationInit()
{
	if (*GetOptions().Graphics.showFPS)
		EnableFrameCount();

	init_create_window();
	was_window_init = true;

	InitializeScreenReader();
	LanguageInitialize();

	SetApplicationVersions();

	ReadOnlyTest();
}

void DiabloInit()
{
	if (forceSpawn || *GetOptions().GameMode.shareware)
		gbIsSpawn = true;

	bool wasHellfireDiscovered = false;
	if (!forceDiablo && !forceHellfire)
		wasHellfireDiscovered = (HaveHellfire() && *GetOptions().GameMode.gameMode == StartUpGameMode::Ask);
	bool enableHellfire = forceHellfire || wasHellfireDiscovered;
	if (!forceDiablo && *GetOptions().GameMode.gameMode == StartUpGameMode::Hellfire) { // Migrate legacy options
		GetOptions().GameMode.gameMode.SetValue(StartUpGameMode::Diablo);
		enableHellfire = true;
	}
	if (forceDiablo || enableHellfire) {
		GetOptions().Mods.SetHellfireEnabled(enableHellfire);
	}

	gbIsHellfireSaveGame = gbIsHellfire;

	for (size_t i = 0; i < QuickMessages.size(); i++) {
		auto &messages = GetOptions().Chat.szHotKeyMsgs[i];
		if (messages.empty()) {
			messages.emplace_back(_(QuickMessages[i].message));
		}
	}

#ifndef USE_SDL1
	InitializeVirtualGamepad();
#endif

	UiInitialize();
	was_ui_init = true;

	if (wasHellfireDiscovered) {
		UiSelStartUpGameOption();
		if (!gbIsHellfire) {
			// Reinitialize the UI Elements because we changed the game
			UnloadUiGFX();
			UiInitialize();
			if (IsHardwareCursor())
				SetHardwareCursor(CursorInfo::UnknownCursor());
		}
	}

	DiabloInitScreen();

	snd_init();

	ui_sound_init();

	// Item graphics are loaded early, they already get touched during hero selection.
	InitItemGFX();

	// Always available.
	LoadSmallSelectionSpinner();
}

void DiabloSplash()
{
	if (!gbShowIntro)
		return;

	if (*GetOptions().StartUp.splash == StartUpSplash::LogoAndTitleDialog)
		play_movie("gendata\\logo.smk", true);

	auto &intro = gbIsHellfire ? GetOptions().StartUp.hellfireIntro : GetOptions().StartUp.diabloIntro;

	if (*intro != StartUpIntro::Off) {
		if (gbIsHellfire)
			play_movie("gendata\\Hellfire.smk", true);
		else
			play_movie("gendata\\diablo1.smk", true);
		if (*intro == StartUpIntro::Once) {
			intro.SetValue(StartUpIntro::Off);
			if (!demo::IsRunning()) SaveOptions();
		}
	}

	if (IsAnyOf(*GetOptions().StartUp.splash, StartUpSplash::TitleDialog, StartUpSplash::LogoAndTitleDialog))
		UiTitleDialog();
}

void DiabloDeinit()
{
	FreeItemGFX();

	LuaShutdown();
	ShutDownScreenReader();

	if (gbSndInited)
		effects_cleanup_sfx();
	snd_deinit();
	if (was_ui_init)
		UiDestroy();
	if (was_archives_init)
		init_cleanup();
	if (was_window_init)
		dx_cleanup(); // Cleanup SDL surfaces stuff, so we have to do it before SDL_Quit().
	UnloadFonts();
	if (SDL_WasInit((~0U) & ~SDL_INIT_HAPTIC) != 0)
		SDL_Quit();
}

std::expected<void, std::string> LoadLvlGFX()
{
	assert(pDungeonCels == nullptr);
	constexpr int SpecialCelWidth = 64;

	const auto loadAll = [](const char *cel, const char *til, const char *special) -> std::expected<void, std::string> {
		ASSIGN_OR_RETURN(pDungeonCels, LoadFileInMemWithStatus(cel));
		ASSIGN_OR_RETURN(pMegaTiles, LoadFileInMemWithStatus<MegaTile>(til));
		ASSIGN_OR_RETURN(pSpecialCels, LoadCelWithStatus(special, SpecialCelWidth));
		return {};
	};

	switch (leveltype) {
	case DTYPE_TOWN: {
		auto cel = LoadFileInMemWithStatus("nlevels\\towndata\\town.cel");
		if (!cel.has_value()) {
			ASSIGN_OR_RETURN(pDungeonCels, LoadFileInMemWithStatus("levels\\towndata\\town.cel"));
		} else {
			pDungeonCels = std::move(*cel);
		}
		auto til = LoadFileInMemWithStatus<MegaTile>("nlevels\\towndata\\town.til");
		if (!til.has_value()) {
			ASSIGN_OR_RETURN(pMegaTiles, LoadFileInMemWithStatus<MegaTile>("levels\\towndata\\town.til"));
		} else {
			pMegaTiles = std::move(*til);
		}
		ASSIGN_OR_RETURN(pSpecialCels, LoadCelWithStatus("levels\\towndata\\towns", SpecialCelWidth));
		return {};
	}
	case DTYPE_CATHEDRAL:
		return loadAll(
		    "levels\\l1data\\l1.cel",
		    "levels\\l1data\\l1.til",
		    "levels\\l1data\\l1s");
	case DTYPE_CATACOMBS:
		return loadAll(
		    "levels\\l2data\\l2.cel",
		    "levels\\l2data\\l2.til",
		    "levels\\l2data\\l2s");
	case DTYPE_CAVES:
		return loadAll(
		    "levels\\l3data\\l3.cel",
		    "levels\\l3data\\l3.til",
		    "levels\\l1data\\l1s");
	case DTYPE_HELL:
		return loadAll(
		    "levels\\l4data\\l4.cel",
		    "levels\\l4data\\l4.til",
		    "levels\\l2data\\l2s");
	case DTYPE_NEST:
		return loadAll(
		    "nlevels\\l6data\\l6.cel",
		    "nlevels\\l6data\\l6.til",
		    "levels\\l1data\\l1s");
	case DTYPE_CRYPT:
		return loadAll(
		    "nlevels\\l5data\\l5.cel",
		    "nlevels\\l5data\\l5.til",
		    "nlevels\\l5data\\l5s");
	default:
		return std::unexpected("LoadLvlGFX");
	}
}

std::expected<void, std::string> LoadAllGFX()
{
	IncProgress();
#if !defined(USE_SDL1) && !defined(__vita__)
	InitVirtualGamepadGFX();
#endif
	IncProgress();
	RETURN_IF_ERROR(InitObjectGFX());
	IncProgress();
	RETURN_IF_ERROR(InitMissileGFX());
	IncProgress();
	return {};
}

/**
 * @param entry Where is the player entering from
 */
void CreateLevel(lvl_entry entry)
{
	CreateDungeon(DungeonSeeds[currlevel], entry);

	switch (leveltype) {
	case DTYPE_TOWN:
		InitTownTriggers();
		break;
	case DTYPE_CATHEDRAL:
		InitL1Triggers();
		break;
	case DTYPE_CATACOMBS:
		InitL2Triggers();
		break;
	case DTYPE_CAVES:
		InitL3Triggers();
		break;
	case DTYPE_HELL:
		InitL4Triggers();
		break;
	case DTYPE_NEST:
		InitHiveTriggers();
		break;
	case DTYPE_CRYPT:
		InitCryptTriggers();
		break;
	default:
		app_fatal("CreateLevel");
	}

	if (leveltype != DTYPE_TOWN) {
		Freeupstairs();
	}
	LoadRndLvlPal(leveltype);
}

void UnstuckChargers()
{
	if (gbIsMultiplayer) {
		for (Player &player : Players) {
			if (!player.plractive)
				continue;
			if (player._pLvlChanging)
				continue;
			if (!player.isOnActiveLevel())
				continue;
			if (&player == MyPlayer)
				continue;
			return;
		}
	}
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.mode == MonsterMode::Charge)
			monster.mode = MonsterMode::Stand;
	}
}

void UpdateMonsterLights()
{
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];

		if ((monster.flags & MFLAG_BERSERK) != 0) {
			const int lightRadius = leveltype == DTYPE_NEST ? 9 : 3;
			monster.lightId = AddLight(monster.position.tile, lightRadius);
		}

		if (monster.lightId != NO_LIGHT) {
			if (monster.lightId == MyPlayer->lightId) { // Fix old saves where some monsters had 0 instead of NO_LIGHT
				monster.lightId = NO_LIGHT;
				continue;
			}

			const Light &light = Lights[monster.lightId];
			if (monster.position.tile != light.position.tile) {
				ChangeLightXY(monster.lightId, monster.position.tile);
			}
		}
	}
}

void GameLogic()
{
	if (!ProcessInput()) {
		return;
	}
	if (gbProcessPlayers) {
		gGameLogicStep = GameLogicStep::ProcessPlayers;
		ProcessPlayers();
	}
	if (leveltype != DTYPE_TOWN) {
		gGameLogicStep = GameLogicStep::ProcessMonsters;
#ifdef _DEBUG
		if (!DebugInvisible)
#endif
			ProcessMonsters();
		gGameLogicStep = GameLogicStep::ProcessObjects;
		ProcessObjects();
		gGameLogicStep = GameLogicStep::ProcessMissiles;
		ProcessMissiles();
		gGameLogicStep = GameLogicStep::ProcessItems;
		ProcessItems();
		ProcessLightList();
		ProcessVisionList();
	} else {
		gGameLogicStep = GameLogicStep::ProcessTowners;
		ProcessTowners();
		gGameLogicStep = GameLogicStep::ProcessItemsTown;
		ProcessItems();
		gGameLogicStep = GameLogicStep::ProcessMissilesTown;
		ProcessMissiles();
	}
	gGameLogicStep = GameLogicStep::None;

#ifdef _DEBUG
	if (DebugScrollViewEnabled && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
		ScrollView();
	}
#endif

	sound_update();
	CheckTriggers();
	CheckQuests();
	RedrawViewport();
	pfile_update(false);

	plrctrls_after_game_logic();
}

void TimeoutCursor(bool bTimeout)
{
	if (bTimeout) {
		if (sgnTimeoutCurs == CURSOR_NONE && sgbMouseDown == CLICK_NONE) {
			sgnTimeoutCurs = pcurs;
			multi_net_ping();
			InfoString = StringOrView {};
			AddInfoBoxString(_("-- Network timeout --"));
			AddInfoBoxString(_("-- Waiting for players --"));
			for (uint8_t i = 0; i < Players.size(); i++) {
				bool isConnected = (player_state[i] & PS_CONNECTED) != 0;
				bool isActive = (player_state[i] & PS_ACTIVE) != 0;
				if (!isConnected || isActive) continue;

				DvlNetLatencies latencies = DvlNet_GetLatencies(i);

				std::string ping = FormatRuntime(
				    _(/* TRANSLATORS: {:s} means: Character Name */ "Player {:s} is timing out!"),
				    Players[i].name());

				StrAppend(ping, "\n  ", FormatRuntime(_(/* TRANSLATORS: Network connectivity statistics */ "Echo latency: {:d} ms"), latencies.echoLatency));

				if (latencies.providerLatency) {
					if (latencies.isRelayed && *latencies.isRelayed) {
						StrAppend(ping, "\n  ", FormatRuntime(_(/* TRANSLATORS: Network connectivity statistics */ "Provider latency: {:d} ms (Relayed)"), *latencies.providerLatency));
					} else {
						StrAppend(ping, "\n  ", FormatRuntime(_(/* TRANSLATORS: Network connectivity statistics */ "Provider latency: {:d} ms"), *latencies.providerLatency));
					}
				}
				EventPlrMsg(ping);
			}
			NewCursor(CURSOR_HOURGLASS);
			RedrawEverything();
		}
		scrollrt_draw_game_screen();
	} else if (sgnTimeoutCurs != CURSOR_NONE) {
		// Timeout is gone, we should restore the previous cursor.
		// But the timeout cursor could already be changed by the now processed messages (for example item cursor from CMD_GETITEM).
		// Changing the item cursor back to the previous (hand) cursor could result in deleted items, because this resets Player.HoldItem (see NewCursor).
		if (pcurs == CURSOR_HOURGLASS)
			NewCursor(sgnTimeoutCurs);
		sgnTimeoutCurs = CURSOR_NONE;
		InfoString = StringOrView {};
		RedrawEverything();
	}
}

void HelpKeyPressed()
{
	if (HelpFlag) {
		HelpFlag = false;
	} else if (IsPlayerInStore()) {
		InfoString = StringOrView {};
		AddInfoBoxString(_("No help available")); /// BUGFIX: message isn't displayed
		AddInfoBoxString(_("while in stores"));
		LastPlayerAction = PlayerActionType::None;
	} else {
		CloseInventory();
		CloseCharPanel();
		SpellbookFlag = false;
		SpellSelectFlag = false;
		if (qtextflag && leveltype == DTYPE_TOWN) {
			qtextflag = false;
			stream_stop();
		}
		QuestLogIsOpen = false;
		CancelCurrentDiabloMsg();
		gamemenu_off();
		DisplayHelp();
		doom_close();
	}
}

void InventoryKeyPressed()
{
	if (IsPlayerInStore())
		return;
	invflag = !invflag;
	if (!IsLeftPanelOpen() && CanPanelsCoverView()) {
		if (!invflag) { // We closed the inventory
			if (MousePosition.x < 480 && MousePosition.y < GetMainPanel().position.y) {
				SetCursorPos(MousePosition + Displacement { 160, 0 });
			}
		} else if (!SpellbookFlag) { // We opened the inventory
			if (MousePosition.x > 160 && MousePosition.y < GetMainPanel().position.y) {
				SetCursorPos(MousePosition - Displacement { 160, 0 });
			}
		}
	}
	SpellbookFlag = false;
	CloseGoldWithdraw();
	CloseStash();
	CloseVisualStore();
}

void CharacterSheetKeyPressed()
{
	if (IsPlayerInStore())
		return;
	if (!IsRightPanelOpen() && CanPanelsCoverView()) {
		if (CharFlag) { // We are closing the character sheet
			if (MousePosition.x > 160 && MousePosition.y < GetMainPanel().position.y) {
				SetCursorPos(MousePosition - Displacement { 160, 0 });
			}
		} else if (!QuestLogIsOpen) { // We opened the character sheet
			if (MousePosition.x < 480 && MousePosition.y < GetMainPanel().position.y) {
				SetCursorPos(MousePosition + Displacement { 160, 0 });
			}
		}
	}
	ToggleCharPanel();
}

void PartyPanelSideToggleKeyPressed()
{
	PartySidePanelOpen = !PartySidePanelOpen;
}

void QuestLogKeyPressed()
{
	if (IsPlayerInStore())
		return;
	if (!QuestLogIsOpen) {
		StartQuestlog();
	} else {
		QuestLogIsOpen = false;
	}
	if (!IsRightPanelOpen() && CanPanelsCoverView()) {
		if (!QuestLogIsOpen) { // We closed the quest log
			if (MousePosition.x > 160 && MousePosition.y < GetMainPanel().position.y) {
				SetCursorPos(MousePosition - Displacement { 160, 0 });
			}
		} else if (!CharFlag) { // We opened the character quest log
			if (MousePosition.x < 480 && MousePosition.y < GetMainPanel().position.y) {
				SetCursorPos(MousePosition + Displacement { 160, 0 });
			}
		}
	}
	CloseCharPanel();
	CloseGoldWithdraw();
	CloseStash();
	CloseVisualStore();
}

void DisplaySpellsKeyPressed()
{
	if (IsPlayerInStore())
		return;
	CloseCharPanel();
	QuestLogIsOpen = false;
	CloseInventory();
	SpellbookFlag = false;
	if (!SpellSelectFlag) {
		DoSpeedBook();
	} else {
		SpellSelectFlag = false;
	}
	LastPlayerAction = PlayerActionType::None;
}

void SpellBookKeyPressed()
{
	if (IsPlayerInStore())
		return;
	SpellbookFlag = !SpellbookFlag;
	if (!IsLeftPanelOpen() && CanPanelsCoverView()) {
		if (!SpellbookFlag) { // We closed the inventory
			if (MousePosition.x < 480 && MousePosition.y < GetMainPanel().position.y) {
				SetCursorPos(MousePosition + Displacement { 160, 0 });
			}
		} else if (!invflag) { // We opened the inventory
			if (MousePosition.x > 160 && MousePosition.y < GetMainPanel().position.y) {
				SetCursorPos(MousePosition - Displacement { 160, 0 });
			}
		}
	}
	CloseInventory();
	CloseVisualStore();
}

void CycleSpellHotkeys(bool next)
{
	StaticVector<size_t, NumHotkeys> validHotKeyIndexes;
	std::optional<size_t> currentIndex;
	for (size_t slot = 0; slot < NumHotkeys; slot++) {
		if (!IsValidSpeedSpell(slot))
			continue;
		if (MyPlayer->_pRSpell == MyPlayer->_pSplHotKey[slot] && MyPlayer->_pRSplType == MyPlayer->_pSplTHotKey[slot]) {
			// found current
			currentIndex = validHotKeyIndexes.size();
		}
		validHotKeyIndexes.emplace_back(slot);
	}
	if (validHotKeyIndexes.size() == 0)
		return;

	size_t newIndex;
	if (!currentIndex) {
		newIndex = next ? 0 : (validHotKeyIndexes.size() - 1);
	} else if (next) {
		newIndex = (*currentIndex == validHotKeyIndexes.size() - 1) ? 0 : (*currentIndex + 1);
	} else {
		newIndex = *currentIndex == 0 ? (validHotKeyIndexes.size() - 1) : (*currentIndex - 1);
	}
	ToggleSpell(validHotKeyIndexes[newIndex]);
}

bool IsPlayerDead()
{
	return MyPlayer->_pmode == PM_DEATH || MyPlayerIsDead;
}

bool IsGameRunning()
{
	return PauseMode != 2;
}

bool CanPlayerTakeAction()
{
	return !IsPlayerDead() && IsGameRunning();
}

bool CanAutomapBeToggledOff()
{
	// check if every window is closed - if yes, automap can be toggled off
	return !QuestLogIsOpen && !IsWithdrawGoldOpen && !IsStashOpen && !IsVisualStoreOpen && !CharFlag
	    && !SpellbookFlag && !invflag && !isGameMenuOpen && !qtextflag && !SpellSelectFlag
	    && !ChatLogFlag && !HelpFlag;
}

void OptionLanguageCodeChanged()
{
	UnloadFonts();
	LanguageInitialize();
	LoadLanguageArchive();
	effects_cleanup_sfx(false);
	if (gbRunGame)
		sound_init();
	else
		ui_sound_init();
}

const auto OptionChangeHandlerLanguage = (GetOptions().Language.code.SetValueChangedCallback(OptionLanguageCodeChanged), true);

void OptionTownViewCameraModeChanged()
{
	SuspendTownFirstPersonInput();
	ReleaseTownCameraDrag();
	SetTownViewCameraMode(static_cast<TownCameraMode>(std::clamp(*GetOptions().Graphics.townViewCameraMode, 0, 3)));
	ResetItemlabelHighlighted();
	RedrawEverything();
}

void OptionTownViewCameraPreferencesChanged()
{
	// FOV/sensitivity changes must not reapply the saved mode after Home.
	ReleaseTownCameraDrag();
	ApplyTownViewCameraPreferences();
	RedrawViewport();
}

void OptionTownViewHorizonChanged()
{
	RedrawViewport();
}

const auto OptionChangeHandlerTownViewCameraMode = (GetOptions().Graphics.townViewCameraMode.SetValueChangedCallback(OptionTownViewCameraModeChanged), true);
const auto OptionChangeHandlerTownViewCameraFov = (GetOptions().Graphics.townViewCameraFov.SetValueChangedCallback(OptionTownViewCameraPreferencesChanged), true);
const auto OptionChangeHandlerTownViewCameraSensitivity = (GetOptions().Graphics.townViewCameraSensitivity.SetValueChangedCallback(OptionTownViewCameraPreferencesChanged), true);
const auto OptionChangeHandlerTownViewHorizon = (GetOptions().Graphics.townViewHorizon.SetValueChangedCallback(OptionTownViewHorizonChanged), true);

} // namespace

bool IsTownFirstPersonInputCaptured()
{
	return FirstPersonInput.phase == TownFirstPersonCapturePhase::Captured
	    && CanCaptureFirstPersonInput() && FirstPersonRelativeMouse();
}

Point GetTownFirstPersonPointer(Point absolute)
{
	return IsTownFirstPersonInputCaptured() ? FirstPersonPointerCenter() : absolute;
}

Direction GetTownFirstPersonMoveDirection()
{
	// Consumed only by native Movement(), once per GameLogic tick.
	const auto result = SyncTownFirstPersonInput();
	const Direction direction = GetTownViewCameraMode() == TownCameraMode::ThirdPerson
	    ? SyncThirdPersonMovement().direction : result.direction;
	return FirstPersonDeferredClickCount == 0 ? direction : Direction::NoDirection;
}

bool IsTownCameraMovementInputActive()
{
	return CanUseTownMovementKeys();
}

void SuspendTownFirstPersonInput()
{
	++TownMovementEpoch;
	ThirdPersonMovementInitialized = false;
	ThirdPersonMovementInput = {};
	TownFollowWheelNeedsDraw = false;
	TownCameraHasFrameClock = false;
	AdvanceTownViewCamera(0);
	FirstPersonDeferredClickCount = 0;
	// Stop before pause/UI/device changes can disable GameLogic or ownership
	// guards. This also cancels a queued walk whose path does not exist yet.
	StopTownFirstPersonWalk();
	SyncTownFirstPersonInput(0, 0, 0, 0, true);
}

void SetTownFirstPersonInputServicesForDiagnostics(const TownFirstPersonInputServicesForDiagnostics *services)
{
	SuspendTownFirstPersonInput();
	FirstPersonServices = services != nullptr ? *services : TownFirstPersonInputServicesForDiagnostics {};
	FirstPersonHasDiagnosticServices = services != nullptr;
	FirstPersonInput = {};
	TownMovementOwnedKeys.clear();
	FirstPersonConsumedButtons = 0;
	FirstPersonHasSavedPointer = false;
	FirstPersonMovementModifiers = 0;
}

void DispatchGameEventForDiagnostics(const SDL_Event &event, uint16_t modState)
{
	GameEventHandler(event, modState);
}

void SyncFirstPersonInputForDiagnostics()
{
	SyncTownFirstPersonInput();
}

void FlushFirstPersonClicksForDiagnostics()
{
	FlushTownFirstPersonClicks();
}

EventHandler GetGameEventHandlerForDiagnostics()
{
	return GameEventHandler;
}

uint32_t GetGameId()
{
	uint32_t declared = 0;
	bool hasDeclared = false;
	bool hasUnrecognisedMod = false;
	for (const ModIdentifier &mod : ActiveModIdentifiers) {
		const std::string &programId = mod.manifest.programId;
		const uint32_t candidate = programId.size() == 4 ? LoadBE32(programId.data()) : 0;
		// A mod may not brand itself as DRTL/DSHR
		if (candidate != 0 && candidate != GameIdDiabloFull && candidate != GameIdDiabloSpawn) {
			declared = candidate; // last active mod that declares one wins
			hasDeclared = true;
		} else if (!mod.whitelisted) {
			hasUnrecognisedMod = true;
		}
	}
	if (hasDeclared)
		return declared;
	if (hasUnrecognisedMod)
		return GameIdGenericMod;
	return gbIsSpawn ? GameIdDiabloSpawn : GameIdDiabloFull;
}

void InitKeymapActions()
{
	Options &options = GetOptions();
	// Load this new default first so existing custom bindings keep priority.
	options.Keymapper.AddAction(
	    "Town3DCameraMode",
	    N_("Cycle Tristram camera mode"),
	    N_("Cycle and save the Isometric, Free Orbit, Third Person and First Person camera preference. Movement and combat keep their native controls."),
	    'K',
	    [] {
		    SuspendTownFirstPersonInput();
		    ReleaseTownCameraDrag();
		    CycleTownViewCameraMode();
		    ResetItemlabelHighlighted();
		    RedrawEverything();
	    },
	    nullptr,
	    [] { return CanControlTownCamera(false); });
	for (uint32_t i = 0; i < 8; ++i) {
		options.Keymapper.AddAction(
		    "BeltItem{}",
		    N_("Belt item {}"),
		    N_("Use Belt item."),
		    '1' + i,
		    [i] {
			    const Player &myPlayer = *MyPlayer;
			    if (!myPlayer.SpdList[i].isEmpty() && myPlayer.SpdList[i]._itype != ItemType::Gold) {
				    UseInvItem(INVITEM_BELT_FIRST + i);
			    }
		    },
		    nullptr,
		    CanPlayerTakeAction,
		    i + 1);
	}
	for (uint32_t i = 0; i < NumHotkeys; ++i) {
		options.Keymapper.AddAction(
		    "QuickSpell{}",
		    N_("Quick spell {}"),
		    N_("Hotkey for skill or spell."),
		    i < 4 ? static_cast<uint32_t>(SDLK_F5) + i : static_cast<uint32_t>(SDLK_UNKNOWN),
		    [i]() {
			    if (SpellSelectFlag) {
				    SetSpeedSpell(i);
				    return;
			    }
			    if (!*GetOptions().Gameplay.quickCast)
				    ToggleSpell(i);
			    else
				    QuickCast(i);
		    },
		    nullptr,
		    CanPlayerTakeAction,
		    i + 1);
	}
	options.Keymapper.AddAction(
	    "QuickSpellPrevious",
	    N_("Previous quick spell"),
	    N_("Selects the previous quick spell (cycles)."),
	    MouseScrollUpButton,
	    [] { CycleSpellHotkeys(false); },
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "QuickSpellNext",
	    N_("Next quick spell"),
	    N_("Selects the next quick spell (cycles)."),
	    MouseScrollDownButton,
	    [] { CycleSpellHotkeys(true); },
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "UseHealthPotion",
	    N_("Use health potion"),
	    N_("Use health potions from belt."),
	    SDLK_UNKNOWN,
	    [] { UseBeltItem(BeltItemType::Healing); },
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "UseManaPotion",
	    N_("Use mana potion"),
	    N_("Use mana potions from belt."),
	    SDLK_UNKNOWN,
	    [] { UseBeltItem(BeltItemType::Mana); },
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "DisplaySpells",
	    N_("Speedbook"),
	    N_("Open Speedbook."),
	    'S',
	    DisplaySpellsKeyPressed,
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "QuickSave",
	    N_("Quick save"),
	    N_("Saves the game."),
	    SDLK_F2,
	    [] { gamemenu_save_game(false); },
	    nullptr,
	    [&]() { return !gbIsMultiplayer && CanPlayerTakeAction(); });
	options.Keymapper.AddAction(
	    "ToggleTown3D",
	    N_("Tristram 3D"),
	    N_("Switch between the original and 3D view in Tristram."),
	    SDLK_F4,
	    [] {
		    SuspendTownFirstPersonInput();
		    ReleaseTownCameraDrag();
		    ToggleTownView();
		    ResetItemlabelHighlighted();
		    RedrawEverything();
	    },
	    nullptr,
	    [] { return CanUseTownCamera(); });
	options.Keymapper.AddAction(
	    "Town3DRotateLeft",
	    N_("Rotate Tristram camera left"),
	    N_("Rotate the 3D camera without moving the hero."),
	    SDLK_LEFTBRACKET,
	    [] { RotateTownView(-0.15F); RedrawViewport(); },
	    nullptr,
	    [] { return CanControlTownCamera(false); });
	options.Keymapper.AddAction(
	    "Town3DRotateRight",
	    N_("Rotate Tristram camera right"),
	    N_("Rotate the 3D camera without moving the hero."),
	    SDLK_RIGHTBRACKET,
	    [] { RotateTownView(0.15F); RedrawViewport(); },
	    nullptr,
	    [] { return CanControlTownCamera(false); });
	options.Keymapper.AddAction(
	    "Town3DCameraNear",
	    N_("Move Tristram camera closer"),
	    N_("Decrease the distance of the 3D camera."),
	    SDLK_PAGEUP,
	    [] { AdjustTownViewDistance(-1.0F); RedrawViewport(); },
	    nullptr,
	    [] { return CanControlTownCamera(false); });
	options.Keymapper.AddAction(
	    "Town3DResetCamera",
	    N_("Restore Tristram camera"),
	    N_("Restore the 3D camera and follow the hero."),
	    SDLK_HOME,
	    [] { SuspendTownFirstPersonInput(); ReleaseTownCameraDrag(); ResetTownViewCamera(); RedrawViewport(); },
	    nullptr,
	    [] { return CanControlTownCamera(false); });
	options.Keymapper.AddAction(
	    "Town3DCameraFar",
	    N_("Move Tristram camera farther"),
	    N_("Increase the distance of the 3D camera."),
	    SDLK_PAGEDOWN,
	    [] { AdjustTownViewDistance(1.0F); RedrawViewport(); },
	    nullptr,
	    [] { return CanControlTownCamera(false); });
	options.Keymapper.AddAction(
	    "QuickLoad",
	    N_("Quick load"),
	    N_("Loads the game."),
	    SDLK_F3,
	    [] { gamemenu_load_game(false); },
	    nullptr,
	    [&]() { return !gbIsMultiplayer && gbValidSaveFile && !IsPlayerInStore() && IsGameRunning(); });
#ifndef NOEXIT
	options.Keymapper.AddAction(
	    "QuitGame",
	    N_("Quit game"),
	    N_("Closes the game."),
	    SDLK_UNKNOWN,
	    [] { gamemenu_quit_game(false); });
#endif
	options.Keymapper.AddAction(
	    "StopHero",
	    N_("Stop hero"),
	    N_("Stops walking and cancel pending actions."),
	    SDLK_UNKNOWN,
	    [] { MyPlayer->Stop(); },
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "ItemHighlighting",
	    N_("Item highlighting"),
	    N_("Show/hide items on ground."),
	    SDLK_LALT,
	    [] { HighlightKeyPressed(true); },
	    [] { HighlightKeyPressed(false); });
	options.Keymapper.AddAction(
	    "ToggleItemHighlighting",
	    N_("Toggle item highlighting"),
	    N_("Permanent show/hide items on ground."),
	    SDLK_RCTRL,
	    nullptr,
	    [] { ToggleItemLabelHighlight(); });
	options.Keymapper.AddAction(
	    "ToggleAutomap",
	    N_("Toggle automap"),
	    N_("Toggles if automap is displayed."),
	    SDLK_TAB,
	    DoAutoMap,
	    nullptr,
	    IsGameRunning);
	options.Keymapper.AddAction(
	    "CycleAutomapType",
	    N_("Cycle map type"),
	    N_("Opaque -> Transparent -> Minimap -> None"),
	    SDLK_M,
	    CycleAutomapType,
	    nullptr,
	    IsGameRunning);

	options.Keymapper.AddAction(
	    "Inventory",
	    N_("Inventory"),
	    N_("Open Inventory screen."),
	    'I',
	    InventoryKeyPressed,
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "Character",
	    N_("Character"),
	    N_("Open Character screen."),
	    'C',
	    CharacterSheetKeyPressed,
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "Party",
	    N_("Party"),
	    N_("Open side Party panel."),
	    'Y',
	    PartyPanelSideToggleKeyPressed,
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "QuestLog",
	    N_("Quest log"),
	    N_("Open Quest log."),
	    'Q',
	    QuestLogKeyPressed,
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "SpellBook",
	    N_("Spellbook"),
	    N_("Open Spellbook."),
	    'B',
	    SpellBookKeyPressed,
	    nullptr,
	    CanPlayerTakeAction);
	for (uint32_t i = 0; i < QuickMessages.size(); ++i) {
		options.Keymapper.AddAction(
		    "QuickMessage{}",
		    N_("Quick Message {}"),
		    N_("Use Quick Message in chat."),
		    (i < 4) ? static_cast<uint32_t>(SDLK_F9) + i : static_cast<uint32_t>(SDLK_UNKNOWN),
		    [i]() { DiabloHotkeyMsg(i); },
		    nullptr,
		    nullptr,
		    i + 1);
	}
	options.Keymapper.AddAction(
	    "HideInfoScreens",
	    N_("Hide Info Screens"),
	    N_("Hide all info screens."),
	    SDLK_SPACE,
	    [] {
		    if (CanAutomapBeToggledOff())
			    AutomapActive = false;

		    ClosePanels();
		    HelpFlag = false;
		    ChatLogFlag = false;
		    SpellSelectFlag = false;
		    if (qtextflag && leveltype == DTYPE_TOWN) {
			    qtextflag = false;
			    stream_stop();
		    }

		    CancelCurrentDiabloMsg();
		    gamemenu_off();
		    doom_close();
	    },
	    nullptr,
	    IsGameRunning);
	options.Keymapper.AddAction(
	    "Zoom",
	    N_("Zoom"),
	    N_("Zoom Game Screen."),
	    'Z',
	    [] {
		    GetOptions().Graphics.zoom.SetValue(!*GetOptions().Graphics.zoom);
		    CalcViewportGeometry();
	    },
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "PauseGame",
	    N_("Pause Game"),
	    N_("Pauses the game."),
	    'P',
	    diablo_pause_game);
	options.Keymapper.AddAction(
	    "PauseGameAlternate",
	    N_("Pause Game (Alternate)"),
	    N_("Pauses the game."),
	    SDLK_PAUSE,
	    diablo_pause_game);
	options.Keymapper.AddAction(
	    "DecreaseBrightness",
	    N_("Decrease Brightness"),
	    N_("Reduce screen brightness."),
	    'F',
	    DecreaseBrightness,
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "IncreaseBrightness",
	    N_("Increase Brightness"),
	    N_("Increase screen brightness."),
	    'G',
	    IncreaseBrightness,
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "Help",
	    N_("Help"),
	    N_("Open Help Screen."),
	    SDLK_F1,
	    HelpKeyPressed,
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "Screenshot",
	    N_("Screenshot"),
	    N_("Takes a screenshot."),
	    SDLK_PRINTSCREEN,
	    nullptr,
	    CaptureScreen);
	options.Keymapper.AddAction(
	    "GameInfo",
	    N_("Game info"),
	    N_("Displays game infos."),
	    'V',
	    [] {
		    EventPlrMsg(FormatRuntime(
		                    _(/* TRANSLATORS: {:s} means: Project Name, Game Version. */ "{:s} {:s}"),
		                    PROJECT_NAME,
		                    PROJECT_VERSION),
		        UiFlags::ColorWhite);
	    },
	    nullptr,
	    CanPlayerTakeAction);
	options.Keymapper.AddAction(
	    "ChatLog",
	    N_("Chat Log"),
	    N_("Displays chat log."),
	    'L',
	    [] {
		    ToggleChatLog();
	    });
	options.Keymapper.AddAction(
	    "SortInv",
	    N_("Sort Inventory"),
	    N_("Sorts the inventory."),
	    'R',
	    [] {
		    ReorganizeInventory(*MyPlayer);
	    });
#ifdef _DEBUG
	options.Keymapper.AddAction(
	    "OpenConsole",
	    N_("Console"),
	    N_("Opens Lua console."),
	    SDLK_GRAVE,
	    OpenConsole);
	options.Keymapper.AddAction(
	    "DebugToggle",
	    "Debug toggle",
	    "Programming is like magic.",
	    'X',
	    [] {
		    DebugToggle = !DebugToggle;
	    });
#endif
	// One native Key option per slot keeps both existing binding menus/INI format.
	// This separate context never evicts S=DisplaySpells or custom native keys.
	const auto addMovement = [&](std::string_view id, const char *name, uint32_t key, uint8_t bit) {
		options.Keymapper.AddAction(id, name,
		    N_("Camera-relative movement in first and third person. Main and alternate keys work independently. Menus and chat keep native controls."),
		    key, [bit] { TownMovementActionPressed(bit); }, [bit] { TownMovementActionReleased(bit); },
		    CanUseTownMovementKeys, 0, KeymapperContext::TownMovement, bit).SetValueChangedCallback(TownMovementBindingsChanged);
	};
	addMovement("TownMoveForward", N_("Move forward (main)"), 'W', TownFirstPersonKeyW);
	addMovement("TownMoveForwardAlternate", N_("Move forward (alternate)"), SDLK_UP, TownFirstPersonArrowUp);
	addMovement("TownMoveBackward", N_("Move backward (main)"), 'S', TownFirstPersonKeyS);
	addMovement("TownMoveBackwardAlternate", N_("Move backward (alternate)"), SDLK_DOWN, TownFirstPersonArrowDown);
	addMovement("TownMoveLeft", N_("Strafe left (main)"), 'A', TownFirstPersonKeyA);
	addMovement("TownMoveLeftAlternate", N_("Strafe left (alternate)"), SDLK_LEFT, TownFirstPersonArrowLeft);
	addMovement("TownMoveRight", N_("Strafe right (main)"), 'D', TownFirstPersonKeyD);
	addMovement("TownMoveRightAlternate", N_("Strafe right (alternate)"), SDLK_RIGHT, TownFirstPersonArrowRight);
	options.Keymapper.CommitActions();
}

void InitPadmapActions()
{
	Options &options = GetOptions();
	for (int i = 0; i < 8; ++i) {
		options.Padmapper.AddAction(
		    "BeltItem{}",
		    N_("Belt item {}"),
		    N_("Use Belt item."),
		    ControllerButton_NONE,
		    [i] {
			    const Player &myPlayer = *MyPlayer;
			    if (!myPlayer.SpdList[i].isEmpty() && myPlayer.SpdList[i]._itype != ItemType::Gold) {
				    UseInvItem(INVITEM_BELT_FIRST + i);
			    }
		    },
		    nullptr,
		    CanPlayerTakeAction,
		    i + 1);
	}
	for (uint32_t i = 0; i < NumHotkeys; ++i) {
		options.Padmapper.AddAction(
		    "QuickSpell{}",
		    N_("Quick spell {}"),
		    N_("Hotkey for skill or spell."),
		    ControllerButton_NONE,
		    [i]() {
			    if (SpellSelectFlag) {
				    SetSpeedSpell(i);
				    return;
			    }
			    if (!*GetOptions().Gameplay.quickCast)
				    ToggleSpell(i);
			    else
				    QuickCast(i);
		    },
		    nullptr,
		    []() { return CanPlayerTakeAction() && !InGameMenu(); },
		    i + 1);
	}
	options.Padmapper.AddAction(
	    "PrimaryAction",
	    N_("Primary action"),
	    N_("Attack monsters, talk to towners, lift and place inventory items."),
	    ControllerButton_BUTTON_B,
	    [] {
		    ControllerActionHeld = GameActionType_PRIMARY_ACTION;
		    LastPlayerAction = PlayerActionType::None;
		    PerformPrimaryAction();
	    },
	    [] {
		    ControllerActionHeld = GameActionType_NONE;
		    LastPlayerAction = PlayerActionType::None;
	    },
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "SecondaryAction",
	    N_("Secondary action"),
	    N_("Open chests, interact with doors, pick up items."),
	    ControllerButton_BUTTON_Y,
	    [] {
		    ControllerActionHeld = GameActionType_SECONDARY_ACTION;
		    LastPlayerAction = PlayerActionType::None;
		    PerformSecondaryAction();
	    },
	    [] {
		    ControllerActionHeld = GameActionType_NONE;
		    LastPlayerAction = PlayerActionType::None;
	    },
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "SpellAction",
	    N_("Spell action"),
	    N_("Cast the active spell."),
	    ControllerButton_BUTTON_X,
	    [] {
		    ControllerActionHeld = GameActionType_CAST_SPELL;
		    LastPlayerAction = PlayerActionType::None;
		    PerformSpellAction();
	    },
	    [] {
		    ControllerActionHeld = GameActionType_NONE;
		    LastPlayerAction = PlayerActionType::None;
	    },
	    []() { return CanPlayerTakeAction() && !InGameMenu(); });
	options.Padmapper.AddAction(
	    "CancelAction",
	    N_("Cancel action"),
	    N_("Close menus."),
	    ControllerButton_BUTTON_A,
	    [] {
		    if (DoomFlag) {
			    doom_close();
			    return;
		    }

		    GameAction action;
		    if (SpellSelectFlag)
			    action = GameAction(GameActionType_TOGGLE_QUICK_SPELL_MENU);
		    else if (invflag)
			    action = GameAction(GameActionType_TOGGLE_INVENTORY);
		    else if (SpellbookFlag)
			    action = GameAction(GameActionType_TOGGLE_SPELL_BOOK);
		    else if (QuestLogIsOpen)
			    action = GameAction(GameActionType_TOGGLE_QUEST_LOG);
		    else if (CharFlag)
			    action = GameAction(GameActionType_TOGGLE_CHARACTER_INFO);
		    ProcessGameAction(action);
	    },
	    nullptr,
	    [] { return DoomFlag || SpellSelectFlag || invflag || SpellbookFlag || QuestLogIsOpen || CharFlag; });
	options.Padmapper.AddAction(
	    "MoveUp",
	    N_("Move up"),
	    N_("Moves the player character up."),
	    ControllerButton_BUTTON_DPAD_UP,
	    [] {});
	options.Padmapper.AddAction(
	    "MoveDown",
	    N_("Move down"),
	    N_("Moves the player character down."),
	    ControllerButton_BUTTON_DPAD_DOWN,
	    [] {});
	options.Padmapper.AddAction(
	    "MoveLeft",
	    N_("Move left"),
	    N_("Moves the player character left."),
	    ControllerButton_BUTTON_DPAD_LEFT,
	    [] {});
	options.Padmapper.AddAction(
	    "MoveRight",
	    N_("Move right"),
	    N_("Moves the player character right."),
	    ControllerButton_BUTTON_DPAD_RIGHT,
	    [] {});
	options.Padmapper.AddAction(
	    "StandGround",
	    N_("Stand ground"),
	    N_("Hold to prevent the player from moving."),
	    ControllerButton_NONE,
	    [] {});
	options.Padmapper.AddAction(
	    "ToggleStandGround",
	    N_("Toggle stand ground"),
	    N_("Toggle whether the player moves."),
	    ControllerButton_NONE,
	    [] { StandToggle = !StandToggle; },
	    nullptr,
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "UseHealthPotion",
	    N_("Use health potion"),
	    N_("Use health potions from belt."),
	    ControllerButton_BUTTON_LEFTSHOULDER,
	    [] { UseBeltItem(BeltItemType::Healing); },
	    nullptr,
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "UseManaPotion",
	    N_("Use mana potion"),
	    N_("Use mana potions from belt."),
	    ControllerButton_BUTTON_RIGHTSHOULDER,
	    [] { UseBeltItem(BeltItemType::Mana); },
	    nullptr,
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "Character",
	    N_("Character"),
	    N_("Open Character screen."),
	    ControllerButton_AXIS_TRIGGERLEFT,
	    [] {
		    ProcessGameAction(GameAction { GameActionType_TOGGLE_CHARACTER_INFO });
	    },
	    nullptr,
	    []() { return CanPlayerTakeAction() && !InGameMenu(); });
	options.Padmapper.AddAction(
	    "Inventory",
	    N_("Inventory"),
	    N_("Open Inventory screen."),
	    ControllerButton_AXIS_TRIGGERRIGHT,
	    [] {
		    ProcessGameAction(GameAction { GameActionType_TOGGLE_INVENTORY });
	    },
	    nullptr,
	    []() { return CanPlayerTakeAction() && !InGameMenu(); });
	options.Padmapper.AddAction(
	    "QuestLog",
	    N_("Quest log"),
	    N_("Open Quest log."),
	    { ControllerButton_BUTTON_BACK, ControllerButton_AXIS_TRIGGERLEFT },
	    [] {
		    ProcessGameAction(GameAction { GameActionType_TOGGLE_QUEST_LOG });
	    },
	    nullptr,
	    []() { return CanPlayerTakeAction() && !InGameMenu(); });
	options.Padmapper.AddAction(
	    "SpellBook",
	    N_("Spellbook"),
	    N_("Open Spellbook."),
	    { ControllerButton_BUTTON_BACK, ControllerButton_AXIS_TRIGGERRIGHT },
	    [] {
		    ProcessGameAction(GameAction { GameActionType_TOGGLE_SPELL_BOOK });
	    },
	    nullptr,
	    []() { return CanPlayerTakeAction() && !InGameMenu(); });
	options.Padmapper.AddAction(
	    "DisplaySpells",
	    N_("Speedbook"),
	    N_("Open Speedbook."),
	    ControllerButton_BUTTON_A,
	    [] {
		    ProcessGameAction(GameAction { GameActionType_TOGGLE_QUICK_SPELL_MENU });
	    },
	    nullptr,
	    []() { return CanPlayerTakeAction() && !InGameMenu(); });
	options.Padmapper.AddAction(
	    "ToggleAutomap",
	    N_("Toggle automap"),
	    N_("Toggles if automap is displayed."),
	    ControllerButton_BUTTON_LEFTSTICK,
	    DoAutoMap);
	options.Padmapper.AddAction(
	    "AutomapMoveUp",
	    N_("Automap Move Up"),
	    N_("Moves the automap up when active."),
	    ControllerButton_NONE,
	    [] {});
	options.Padmapper.AddAction(
	    "AutomapMoveDown",
	    N_("Automap Move Down"),
	    N_("Moves the automap down when active."),
	    ControllerButton_NONE,
	    [] {});
	options.Padmapper.AddAction(
	    "AutomapMoveLeft",
	    N_("Automap Move Left"),
	    N_("Moves the automap left when active."),
	    ControllerButton_NONE,
	    [] {});
	options.Padmapper.AddAction(
	    "AutomapMoveRight",
	    N_("Automap Move Right"),
	    N_("Moves the automap right when active."),
	    ControllerButton_NONE,
	    [] {});
	options.Padmapper.AddAction(
	    "MouseUp",
	    N_("Move mouse up"),
	    N_("Simulates upward mouse movement."),
	    { ControllerButton_BUTTON_BACK, ControllerButton_BUTTON_DPAD_UP },
	    [] {});
	options.Padmapper.AddAction(
	    "MouseDown",
	    N_("Move mouse down"),
	    N_("Simulates downward mouse movement."),
	    { ControllerButton_BUTTON_BACK, ControllerButton_BUTTON_DPAD_DOWN },
	    [] {});
	options.Padmapper.AddAction(
	    "MouseLeft",
	    N_("Move mouse left"),
	    N_("Simulates leftward mouse movement."),
	    { ControllerButton_BUTTON_BACK, ControllerButton_BUTTON_DPAD_LEFT },
	    [] {});
	options.Padmapper.AddAction(
	    "MouseRight",
	    N_("Move mouse right"),
	    N_("Simulates rightward mouse movement."),
	    { ControllerButton_BUTTON_BACK, ControllerButton_BUTTON_DPAD_RIGHT },
	    [] {});
	auto leftMouseDown = [] {
		const ControllerButtonCombo standGroundCombo = GetOptions().Padmapper.ButtonComboForAction("StandGround");
		const bool standGround = StandToggle || IsControllerButtonComboPressed(standGroundCombo);
		sgbMouseDown = CLICK_LEFT;
		LeftMouseDown(standGround ? SDL_KMOD_SHIFT : SDL_KMOD_NONE);
	};
	auto leftMouseUp = [] {
		const ControllerButtonCombo standGroundCombo = GetOptions().Padmapper.ButtonComboForAction("StandGround");
		const bool standGround = StandToggle || IsControllerButtonComboPressed(standGroundCombo);
		LastPlayerAction = PlayerActionType::None;
		sgbMouseDown = CLICK_NONE;
		LeftMouseUp(standGround ? SDL_KMOD_SHIFT : SDL_KMOD_NONE);
	};
	options.Padmapper.AddAction(
	    "LeftMouseClick1",
	    N_("Left mouse click"),
	    N_("Simulates the left mouse button."),
	    ControllerButton_BUTTON_RIGHTSTICK,
	    leftMouseDown,
	    leftMouseUp);
	options.Padmapper.AddAction(
	    "LeftMouseClick2",
	    N_("Left mouse click"),
	    N_("Simulates the left mouse button."),
	    { ControllerButton_BUTTON_BACK, ControllerButton_BUTTON_LEFTSHOULDER },
	    leftMouseDown,
	    leftMouseUp);
	auto rightMouseDown = [] {
		const ControllerButtonCombo standGroundCombo = GetOptions().Padmapper.ButtonComboForAction("StandGround");
		const bool standGround = StandToggle || IsControllerButtonComboPressed(standGroundCombo);
		LastPlayerAction = PlayerActionType::None;
		sgbMouseDown = CLICK_RIGHT;
		RightMouseDown(standGround);
	};
	auto rightMouseUp = [] {
		LastPlayerAction = PlayerActionType::None;
		sgbMouseDown = CLICK_NONE;
	};
	options.Padmapper.AddAction(
	    "RightMouseClick1",
	    N_("Right mouse click"),
	    N_("Simulates the right mouse button."),
	    { ControllerButton_BUTTON_BACK, ControllerButton_BUTTON_RIGHTSTICK },
	    rightMouseDown,
	    rightMouseUp);
	options.Padmapper.AddAction(
	    "RightMouseClick2",
	    N_("Right mouse click"),
	    N_("Simulates the right mouse button."),
	    { ControllerButton_BUTTON_BACK, ControllerButton_BUTTON_RIGHTSHOULDER },
	    rightMouseDown,
	    rightMouseUp);
	options.Padmapper.AddAction(
	    "PadHotspellMenu",
	    N_("Gamepad hotspell menu"),
	    N_("Hold to set or use spell hotkeys."),
	    ControllerButton_BUTTON_BACK,
	    [] { PadHotspellMenuActive = true; },
	    [] { PadHotspellMenuActive = false; });
	options.Padmapper.AddAction(
	    "PadMenuNavigator",
	    N_("Gamepad menu navigator"),
	    N_("Hold to access gamepad menu navigation."),
	    ControllerButton_BUTTON_START,
	    [] { PadMenuNavigatorActive = true; },
	    [] { PadMenuNavigatorActive = false; });
	auto toggleGameMenu = [] {
		const bool inMenu = gmenu_is_active();
		PressEscKey();
		LastPlayerAction = PlayerActionType::None;
		PadHotspellMenuActive = false;
		PadMenuNavigatorActive = false;
		if (!inMenu)
			gamemenu_on();
	};
	options.Padmapper.AddAction(
	    "ToggleGameMenu1",
	    N_("Toggle game menu"),
	    N_("Opens the game menu."),
	    {
	        ControllerButton_BUTTON_BACK,
	        ControllerButton_BUTTON_START,
	    },
	    toggleGameMenu);
	options.Padmapper.AddAction(
	    "ToggleGameMenu2",
	    N_("Toggle game menu"),
	    N_("Opens the game menu."),
	    {
	        ControllerButton_BUTTON_START,
	        ControllerButton_BUTTON_BACK,
	    },
	    toggleGameMenu);
	options.Padmapper.AddAction(
	    "QuickSave",
	    N_("Quick save"),
	    N_("Saves the game."),
	    ControllerButton_NONE,
	    [] { gamemenu_save_game(false); },
	    nullptr,
	    [&]() { return !gbIsMultiplayer && CanPlayerTakeAction(); });
	options.Padmapper.AddAction(
	    "QuickLoad",
	    N_("Quick load"),
	    N_("Loads the game."),
	    ControllerButton_NONE,
	    [] { gamemenu_load_game(false); },
	    nullptr,
	    [&]() { return !gbIsMultiplayer && gbValidSaveFile && !IsPlayerInStore() && IsGameRunning(); });
	options.Padmapper.AddAction(
	    "ItemHighlighting",
	    N_("Item highlighting"),
	    N_("Show/hide items on ground."),
	    ControllerButton_NONE,
	    [] { HighlightKeyPressed(true); },
	    [] { HighlightKeyPressed(false); });
	options.Padmapper.AddAction(
	    "ToggleItemHighlighting",
	    N_("Toggle item highlighting"),
	    N_("Permanent show/hide items on ground."),
	    ControllerButton_NONE,
	    nullptr,
	    [] { ToggleItemLabelHighlight(); });
	options.Padmapper.AddAction(
	    "HideInfoScreens",
	    N_("Hide Info Screens"),
	    N_("Hide all info screens."),
	    ControllerButton_NONE,
	    [] {
		    if (CanAutomapBeToggledOff())
			    AutomapActive = false;

		    ClosePanels();
		    HelpFlag = false;
		    ChatLogFlag = false;
		    SpellSelectFlag = false;
		    if (qtextflag && leveltype == DTYPE_TOWN) {
			    qtextflag = false;
			    stream_stop();
		    }

		    CancelCurrentDiabloMsg();
		    gamemenu_off();
		    doom_close();
	    },
	    nullptr,
	    IsGameRunning);
	options.Padmapper.AddAction(
	    "Zoom",
	    N_("Zoom"),
	    N_("Zoom Game Screen."),
	    ControllerButton_NONE,
	    [] {
		    GetOptions().Graphics.zoom.SetValue(!*GetOptions().Graphics.zoom);
		    CalcViewportGeometry();
	    },
	    nullptr,
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "PauseGame",
	    N_("Pause Game"),
	    N_("Pauses the game."),
	    ControllerButton_NONE,
	    diablo_pause_game);
	options.Padmapper.AddAction(
	    "DecreaseBrightness",
	    N_("Decrease Brightness"),
	    N_("Reduce screen brightness."),
	    ControllerButton_NONE,
	    DecreaseBrightness,
	    nullptr,
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "IncreaseBrightness",
	    N_("Increase Brightness"),
	    N_("Increase screen brightness."),
	    ControllerButton_NONE,
	    IncreaseBrightness,
	    nullptr,
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "Help",
	    N_("Help"),
	    N_("Open Help Screen."),
	    ControllerButton_NONE,
	    HelpKeyPressed,
	    nullptr,
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "Screenshot",
	    N_("Screenshot"),
	    N_("Takes a screenshot."),
	    ControllerButton_NONE,
	    nullptr,
	    CaptureScreen);
	options.Padmapper.AddAction(
	    "GameInfo",
	    N_("Game info"),
	    N_("Displays game infos."),
	    ControllerButton_NONE,
	    [] {
		    EventPlrMsg(FormatRuntime(
		                    _(/* TRANSLATORS: {:s} means: Project Name, Game Version. */ "{:s} {:s}"),
		                    PROJECT_NAME,
		                    PROJECT_VERSION),
		        UiFlags::ColorWhite);
	    },
	    nullptr,
	    CanPlayerTakeAction);
	options.Padmapper.AddAction(
	    "SortInv",
	    N_("Sort Inventory"),
	    N_("Sorts the inventory."),
	    ControllerButton_NONE,
	    [] {
		    ReorganizeInventory(*MyPlayer);
	    });
	options.Padmapper.AddAction(
	    "ChatLog",
	    N_("Chat Log"),
	    N_("Displays chat log."),
	    ControllerButton_NONE,
	    [] {
		    ToggleChatLog();
	    });
	options.Padmapper.CommitActions();
}

void SetCursorPos(Point position)
{
	if (ControlDevice != ControlTypes::KeyboardAndMouse) {
		MousePosition = position;
		return;
	}

	LogicalToOutput(&position.x, &position.y);
	if (!demo::IsRunning())
		SDL_WarpMouseInWindow(ghMainWnd, position.x, position.y);
}

void FreeGameMem()
{
	SuspendTownFirstPersonInput();
	ReleaseTownCameraDrag();
	ResetTownViewResources();
	pDungeonCels = nullptr;
	pMegaTiles = nullptr;
	pSpecialCels = std::nullopt;

	FreeMonsters();
	FreeMissileGFX();
	FreeObjectGFX();
	FreeTownerGFX();
	FreeStashGFX();
	FreeVisualStoreGFX();
#ifndef USE_SDL1
	DeactivateVirtualGamepad();
	FreeVirtualGamepadGFX();
#endif
}

bool StartGame(bool bNewGame, bool bSinglePlayer)
{
	gbSelectProvider = true;
	ReturnToMainMenu = false;

	do {
		gbLoadGame = false;

		if (!NetInit(bSinglePlayer)) {
			gbRunGameResult = true;
			break;
		}

		// Save 2.8 MiB of RAM by freeing all main menu resources
		// before starting the game.
		UiDestroy();

		gbSelectProvider = false;

		if (bNewGame || !gbValidSaveFile) {
			InitLevels();
			InitQuests();
			InitPortals();
			InitDungMsgs(*MyPlayer);
			DeltaSyncJunk();
		}
		giNumberOfLevels = gbIsHellfire ? 25 : 17;
		interface_mode uMsg = WM_DIABNEWGAME;
		if (gbValidSaveFile && gbLoadGame) {
			uMsg = WM_DIABLOADGAME;
		}
		InitializeTownViewForGame();
		RunGameLoop(uMsg);
		NetClose();
		UnloadFonts();

		// If the player left the game into the main menu,
		// initialize main menu resources.
		if (gbRunGameResult)
			UiInitialize();
		if (ReturnToMainMenu)
			return true;
	} while (gbRunGameResult);

	SNetDestroy();
	return gbRunGameResult;
}

void diablo_quit(int exitStatus)
{
	FreeGameMem();
	music_stop();
	DiabloDeinit();

#if SDL_VERSION_ATLEAST(2, 0, 0)
	if (SdlLogFile != nullptr) std::fclose(SdlLogFile);
#endif

	exit(exitStatus);
}

#ifdef __UWP__
void (*onInitialized)() = NULL;

void setOnInitialized(void (*callback)())
{
	onInitialized = callback;
}
#endif

int DiabloMain(int argc, char **argv)
{
#ifdef _DEBUG
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_DEBUG);
#endif

	DiabloParseFlags(argc, argv);
	InitKeymapActions();
	InitPadmapActions();

	// Need to ensure devilutionx.mpq (and fonts.mpq if available) are loaded before attempting to read translation settings
	LoadCoreArchives();
	was_archives_init = true;

	// Read settings including translation next. This will use the presence of fonts.mpq and look for assets in devilutionx.mpq
	LoadOptions();
	if (demo::IsRunning()) demo::OverrideOptions();

	// Then look for a voice pack file based on the selected translation
	LoadLanguageArchive();

	ApplicationInit();

	// Ensure the core archives are up to date before loading any assets from them,
	// e.g. `lua\inspect.lua` is loaded during `LuaInitialize()` and would otherwise
	// abort with a confusing "Asset not found" error on an out-of-date archive.
	CheckArchivesUpToDate();

	LuaInitialize();
	if (!demo::IsRunning()) SaveOptions();

	// Finally load game data
	LoadGameArchives();

	LoadTextData();

	// Load dynamic data before we go into the menu as we need to initialise player characters in memory pretty early.
	LoadPlayerDataFiles();

	// TODO: We can probably load this much later (when the game is starting).
	LoadSpellData();
	LoadMissileData();
	LoadMonsterData();
	LoadItemData();
	LoadObjectData();
	LoadQuestData();

	DiabloInit();
#ifdef __UWP__
	onInitialized();
#endif
	if (!demo::IsRunning()) SaveOptions();

	DiabloSplash();
	mainmenu_loop();
	DiabloDeinit();

	return 0;
}

bool TryIconCurs()
{
	if (pcurs == CURSOR_RESURRECT) {
		if (PlayerUnderCursor != nullptr) {
			NetSendCmdParam1(true, CMD_RESURRECT, PlayerUnderCursor->getId());
			NewCursor(CURSOR_HAND);
			return true;
		}

		return false;
	}

	if (pcurs == CURSOR_HEALOTHER) {
		if (PlayerUnderCursor != nullptr) {
			NetSendCmdParam1(true, CMD_HEALOTHER, PlayerUnderCursor->getId());
			NewCursor(CURSOR_HAND);
			return true;
		}

		return false;
	}

	if (pcurs == CURSOR_TELEKINESIS) {
		DoTelekinesis();
		return true;
	}

	Player &myPlayer = *MyPlayer;

	if (pcurs == CURSOR_IDENTIFY) {
		if (pcursinvitem != -1 && !IsInspectingPlayer())
			CheckIdentify(myPlayer, pcursinvitem);
		else if (pcursstashitem != StashStruct::EmptyCell) {
			Item &item = Stash.stashList[pcursstashitem];
			item._iIdentified = true;
			Stash.dirty = true;
		}
		NewCursor(CURSOR_HAND);
		return true;
	}

	if (pcurs == CURSOR_REPAIR) {
		if (pcursinvitem != -1 && !IsInspectingPlayer()) {
			if (IsVisualStoreOpen)
				VisualStoreRepairItem(pcursinvitem);
			else
				DoRepair(myPlayer, pcursinvitem);
		} else if (pcursstashitem != StashStruct::EmptyCell) {
			Item &item = Stash.stashList[pcursstashitem];
			RepairItem(item, myPlayer.getCharacterLevel());
			Stash.dirty = true;
		}
		NewCursor(CURSOR_HAND);
		return true;
	}

	if (pcurs == CURSOR_RECHARGE) {
		if (pcursinvitem != -1 && !IsInspectingPlayer())
			DoRecharge(myPlayer, pcursinvitem);
		else if (pcursstashitem != StashStruct::EmptyCell) {
			Item &item = Stash.stashList[pcursstashitem];
			RechargeItem(item, myPlayer);
			Stash.dirty = true;
		}
		NewCursor(CURSOR_HAND);
		return true;
	}

	if (pcurs == CURSOR_OIL) {
		bool changeCursor = true;
		if (pcursinvitem != -1 && !IsInspectingPlayer())
			changeCursor = DoOil(myPlayer, pcursinvitem);
		else if (pcursstashitem != StashStruct::EmptyCell) {
			Item &item = Stash.stashList[pcursstashitem];
			changeCursor = ApplyOilToItem(item, myPlayer);
			Stash.dirty = true;
		}
		if (changeCursor)
			NewCursor(CURSOR_HAND);
		return true;
	}

	if (pcurs == CURSOR_TELEPORT) {
		const SpellID spellID = myPlayer.inventorySpell;
		const SpellType spellType = SpellType::Scroll;
		const int spellFrom = myPlayer.spellFrom;
		if (IsWallSpell(spellID)) {
			const Direction sd = GetDirection(myPlayer.position.tile, cursPosition);
			NetSendCmdLocParam4(true, CMD_SPELLXYD, cursPosition, static_cast<int8_t>(spellID), static_cast<uint8_t>(spellType), static_cast<uint16_t>(sd), spellFrom);
		} else if (pcursmonst != -1 && leveltype != DTYPE_TOWN) {
			NetSendCmdParam4(true, CMD_SPELLID, pcursmonst, static_cast<int8_t>(spellID), static_cast<uint8_t>(spellType), spellFrom);
		} else if (PlayerUnderCursor != nullptr && !PlayerUnderCursor->hasNoLife() && !myPlayer.friendlyMode) {
			NetSendCmdParam4(true, CMD_SPELLPID, PlayerUnderCursor->getId(), static_cast<int8_t>(spellID), static_cast<uint8_t>(spellType), spellFrom);
		} else {
			NetSendCmdLocParam3(true, CMD_SPELLXY, cursPosition, static_cast<int8_t>(spellID), static_cast<uint8_t>(spellType), spellFrom);
		}
		NewCursor(CURSOR_HAND);
		return true;
	}

	if (pcurs == CURSOR_DISARM && ObjectUnderCursor == nullptr) {
		NewCursor(CURSOR_HAND);
		return true;
	}

	return false;
}

void diablo_pause_game()
{
	if (!gbIsMultiplayer) {
		SuspendTownFirstPersonInput();
		if (PauseMode != 0) {
			PauseMode = 0;
		} else {
			PauseMode = 2;
			sound_stop();
			qtextflag = false;
			LastPlayerAction = PlayerActionType::None;
		}

		RedrawEverything();
	}
}

bool GameWasAlreadyPaused = false;
bool MinimizePaused = false;

bool diablo_is_focused()
{
#ifndef USE_SDL1
	return SDL_GetKeyboardFocus() == ghMainWnd;
#else
	Uint8 appState = SDL_GetAppState();
	return (appState & SDL_APPINPUTFOCUS) != 0;
#endif
}

void diablo_focus_pause()
{
	SuspendTownFirstPersonInput();
	if (!movie_playing && (gbIsMultiplayer || MinimizePaused)) {
		return;
	}

	GameWasAlreadyPaused = PauseMode != 0;

	if (!GameWasAlreadyPaused) {
		PauseMode = 2;
		sound_stop();
		LastPlayerAction = PlayerActionType::None;
	}

	SVidMute();
	music_mute();

	MinimizePaused = true;
}

void diablo_focus_unpause()
{
	if (!GameWasAlreadyPaused) {
		PauseMode = 0;
	}

	SVidUnmute();
	music_unmute();

	MinimizePaused = false;
}

bool PressEscKey()
{
	SuspendTownFirstPersonInput();
	bool rv = false;

	if (DoomFlag) {
		doom_close();
		rv = true;
	}

	if (HelpFlag) {
		HelpFlag = false;
		rv = true;
	}

	if (ChatLogFlag) {
		ChatLogFlag = false;
		rv = true;
	}

	if (qtextflag) {
		qtextflag = false;
		stream_stop();
		rv = true;
	}

	if (IsPlayerInStore()) {
		StoreESC();
		rv = true;
	}

	if (IsDiabloMsgAvailable()) {
		CancelCurrentDiabloMsg();
		rv = true;
	}

	if (ChatFlag) {
		ResetChat();
		rv = true;
	}

	if (DropGoldFlag) {
		control_drop_gold(SDLK_ESCAPE);
		rv = true;
	}

	if (IsWithdrawGoldOpen) {
		WithdrawGoldKeyPress(SDLK_ESCAPE);
		rv = true;
	}

	if (SpellSelectFlag) {
		SpellSelectFlag = false;
		rv = true;
	}

	if (IsLeftPanelOpen() || IsRightPanelOpen()) {
		ClosePanels();
		rv = true;
	}

	return rv;
}

void DisableInputEventHandler(const SDL_Event &event, uint16_t /*modState*/)
{
	SuspendTownFirstPersonInput();
	switch (event.type) {
	case SDL_EVENT_MOUSE_MOTION:
		MousePosition = { SDLC_EventMotionIntX(event), SDLC_EventMotionIntY(event) };
		return;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (sgbMouseDown != CLICK_NONE)
			return;
		switch (event.button.button) {
		case SDL_BUTTON_LEFT:
			sgbMouseDown = CLICK_LEFT;
			return;
		case SDL_BUTTON_RIGHT:
			sgbMouseDown = CLICK_RIGHT;
			return;
		default:
			return;
		}
	case SDL_EVENT_MOUSE_BUTTON_UP:
		sgbMouseDown = CLICK_NONE;
		return;
	}

	MainWndProc(event);
}

void LoadGameLevelStopMusic(_music_id neededTrack)
{
	if (neededTrack != sgnMusicTrack)
		music_stop();
}

void LoadGameLevelStartMusic(_music_id neededTrack)
{
	if (sgnMusicTrack != neededTrack)
		music_start(neededTrack);
	else
		music_refresh();

	if (MinimizePaused) {
		music_mute();
	}
}

void LoadGameLevelResetCursor()
{
	if (pcurs > CURSOR_HAND && pcurs < CURSOR_FIRSTITEM) {
		NewCursor(CURSOR_HAND);
	}
}

void SetRndSeedForDungeonLevel()
{
	if (setlevel) {
		// Maps are not randomly generated, but the monsters max hitpoints are.
		// So we need to ensure that we have a stable seed when generating quest/set-maps.
		// For this purpose we reuse the normal dungeon seeds.
		SetRndSeed(DungeonSeeds[static_cast<size_t>(setlvlnum)]);
	} else {
		SetRndSeed(DungeonSeeds[currlevel]);
	}
}

void LoadGameLevelFirstFlagEntry()
{
	CloseInventory();
	qtextflag = false;
	if (!HeadlessMode) {
		InitInv();
		ClearUniqueItemFlags();
		InitQuestText();
		InitInfoBoxGfx();
		InitHelp();
	}
	InitStores();
	InitAutomapOnce();
}

void LoadGameLevelStores()
{
	if (leveltype == DTYPE_TOWN) {
		SetupTownStores();
	} else {
		FreeStoreMem();
	}
}

void LoadGameLevelStash()
{
	const bool isHellfireSaveGame = gbIsHellfireSaveGame;

	gbIsHellfireSaveGame = gbIsHellfire;
	LoadStash();
	gbIsHellfireSaveGame = isHellfireSaveGame;
}

std::expected<void, std::string> LoadGameLevelDungeon(bool firstflag, lvl_entry lvldir, const Player &myPlayer)
{
	if (firstflag || lvldir == ENTRY_LOAD || !myPlayer._pLvlVisited[currlevel] || gbIsMultiplayer) {
		HoldThemeRooms();
		[[maybe_unused]] const uint32_t mid1Seed = GetLCGEngineState();
		InitGolems();
		InitObjects();
		[[maybe_unused]] const uint32_t mid2Seed = GetLCGEngineState();

		IncProgress();

		RETURN_IF_ERROR(InitMonsters());
		InitItems();
		CreateThemeRooms();

		IncProgress();

		[[maybe_unused]] const uint32_t mid3Seed = GetLCGEngineState();
		InitMissiles();
		InitCorpses();
#ifdef _DEBUG
		SetDebugLevelSeedInfos(mid1Seed, mid2Seed, mid3Seed, GetLCGEngineState());
#endif
		SavePreLighting();

		IncProgress();

		if (gbIsMultiplayer)
			DeltaLoadLevel();
	} else {
		HoldThemeRooms();
		InitGolems();
		RETURN_IF_ERROR(InitMonsters());
		InitMissiles();
		InitCorpses();

		IncProgress();

		RETURN_IF_ERROR(LoadLevel());

		IncProgress();
	}
	return {};
}

void LoadGameLevelSyncPlayerEntry(lvl_entry lvldir)
{
	for (Player &player : Players) {
		if (player.plractive && player.isOnActiveLevel() && (!player._pLvlChanging || &player == MyPlayer)) {
			if (player._pHitPoints > 0) {
				if (lvldir != ENTRY_LOAD)
					SyncInitPlrPos(player);
			} else {
				dFlags[player.position.tile.x][player.position.tile.y] |= DungeonFlag::DeadPlayer;
			}
		}
	}
}

void LoadGameLevelLightVision()
{
	if (leveltype != DTYPE_TOWN) {
		memcpy(dLight, dPreLight, sizeof(dLight));                                     // resets the light on entering a level to get rid of incorrect light
		ChangeLightXY(Players[MyPlayerId].lightId, Players[MyPlayerId].position.tile); // forces player light refresh
		ProcessLightList();
		ProcessVisionList();
	}
}

void LoadGameLevelReturn()
{
	ViewPosition = GetMapReturnPosition();
	if (Quests[Q_BETRAYER]._qactive == QUEST_DONE)
		Quests[Q_BETRAYER]._qvar2 = 2;
}

void LoadGameLevelInitPlayers(bool firstflag, lvl_entry lvldir)
{
	for (Player &player : Players) {
		if (player.plractive && player.isOnActiveLevel()) {
			InitPlayerGFX(player);
			if (lvldir != ENTRY_LOAD)
				InitPlayer(player, firstflag);
		}
	}
}

void LoadGameLevelSetVisited()
{
	bool visited = false;
	for (const Player &player : Players) {
		if (player.plractive)
			visited = visited || player._pLvlVisited[currlevel];
	}
}

std::expected<void, std::string> LoadGameLevelTown(bool firstflag, lvl_entry lvldir, const Player &myPlayer)
{
	for (int i = 0; i < MAXDUNX; i++) { // NOLINT(modernize-loop-convert)
		for (int j = 0; j < MAXDUNY; j++) {
			dFlags[i][j] |= DungeonFlag::Lit;
		}
	}

	InitTowners();
	InitStash();
	InitVisualStore();
	InitItems();
	InitMissiles();

	IncProgress();

	if (!firstflag && lvldir != ENTRY_LOAD && myPlayer._pLvlVisited[currlevel] && !gbIsMultiplayer)
		RETURN_IF_ERROR(LoadLevel());
	if (gbIsMultiplayer)
		DeltaLoadLevel();

	IncProgress();

	for (int x = 0; x < DMAXX; x++)
		for (int y = 0; y < DMAXY; y++)
			UpdateAutomapExplorer({ x, y }, MAP_EXP_SELF);
	return {};
}

std::expected<void, std::string> LoadGameLevelSetLevel(bool firstflag, lvl_entry lvldir, const Player &myPlayer)
{
	RETURN_IF_ERROR(LoadSetMap());
	IncProgress();
	RETURN_IF_ERROR(GetLevelMTypes());
	IncProgress();
	InitGolems();
	RETURN_IF_ERROR(InitMonsters());
	IncProgress();
	if (!HeadlessMode) {
#if !defined(USE_SDL1) && !defined(__vita__)
		InitVirtualGamepadGFX();
#endif
		RETURN_IF_ERROR(InitMissileGFX());
		IncProgress();
	}
	InitCorpses();
	IncProgress();

	if (lvldir == ENTRY_WARPLVL)
		GetPortalLvlPos();
	IncProgress();

	for (Player &player : Players) {
		if (player.plractive && player.isOnActiveLevel()) {
			InitPlayerGFX(player);
			if (lvldir != ENTRY_LOAD)
				InitPlayer(player, firstflag);
		}
	}
	IncProgress();
	InitMultiView();
	IncProgress();

	if (firstflag || lvldir == ENTRY_LOAD || !myPlayer._pSLvlVisited[setlvlnum] || gbIsMultiplayer) {
		InitItems();
		SavePreLighting();
	} else {
		RETURN_IF_ERROR(LoadLevel());
	}
	if (gbIsMultiplayer) {
		DeltaLoadLevel();
		if (!UseMultiplayerQuests())
			ResyncQuests();
	}

	PlayDungMsgs();
	InitMissiles();
	IncProgress();
	return {};
}

std::expected<void, std::string> LoadGameLevelStandardLevel(bool firstflag, lvl_entry lvldir, const Player &myPlayer)
{
	CreateLevel(lvldir);

	IncProgress();

	SetRndSeedForDungeonLevel();

	if (leveltype != DTYPE_TOWN) {
		RETURN_IF_ERROR(GetLevelMTypes());
		InitThemes();
		if (!HeadlessMode)
			RETURN_IF_ERROR(LoadAllGFX());
	} else if (!HeadlessMode) {
		IncProgress();

#if !defined(USE_SDL1) && !defined(__vita__)
		InitVirtualGamepadGFX();
#endif

		IncProgress();

		RETURN_IF_ERROR(InitMissileGFX());

		IncProgress();
		IncProgress();
	}

	IncProgress();

	if (lvldir == ENTRY_RTNLVL) {
		LoadGameLevelReturn();
	}

	if (lvldir == ENTRY_WARPLVL)
		GetPortalLvlPos();

	IncProgress();

	LoadGameLevelInitPlayers(firstflag, lvldir);
	InitMultiView();

	IncProgress();

	LoadGameLevelSetVisited();

	SetRndSeedForDungeonLevel();

	if (leveltype == DTYPE_TOWN) {
		RETURN_IF_ERROR(LoadGameLevelTown(firstflag, lvldir, myPlayer));
	} else {
		RETURN_IF_ERROR(LoadGameLevelDungeon(firstflag, lvldir, myPlayer));
	}

	PlayDungMsgs();

	if (UseMultiplayerQuests())
		ResyncMPQuests();
	else
		ResyncQuests();
	return {};
}

void LoadGameLevelCrypt()
{
	if (CornerStone.isAvailable()) {
		CornerstoneLoad(CornerStone.position);
	}
	if (Quests[Q_NAKRUL]._qactive == QUEST_DONE && currlevel == 24) {
		SyncNakrulRoom();
	}
}

void LoadGameLevelCalculateCursor()
{
	// Recalculate mouse selection of entities after level change/load
	LastPlayerAction = PlayerActionType::None;
	sgbMouseDown = CLICK_NONE;
	ResetItemlabelHighlighted(); // level changed => item changed
	pcursmonst = -1;             // ensure pcurstemp is set to a valid value
	CheckCursMove();
}

std::expected<void, std::string> LoadGameLevel(bool firstflag, lvl_entry lvldir)
{
	const _music_id neededTrack = GetLevelMusic(leveltype);

	ClearFloatingNumbers();
	LoadGameLevelStopMusic(neededTrack);
	LoadGameLevelResetCursor();
	SetRndSeedForDungeonLevel();
	NaKrulTomeSequence = 0;

	IncProgress();

	RETURN_IF_ERROR(LoadTrns());
	MakeLightTable();
	RETURN_IF_ERROR(LoadLevelSOLData());

	IncProgress();

	RETURN_IF_ERROR(LoadLvlGFX());
	SetDungeonMicros(pDungeonCels, MicroTileLen);
	ResetTownViewResources();
	ClearClxDrawCache();

	IncProgress();

	if (firstflag) {
		LoadGameLevelFirstFlagEntry();
	}

	SetRndSeedForDungeonLevel();

	LoadGameLevelStores();

	if (firstflag || lvldir == ENTRY_LOAD) {
		LoadGameLevelStash();
	}

	IncProgress();

	InitAutomap();

	if (leveltype != DTYPE_TOWN && lvldir != ENTRY_LOAD) {
		InitLighting();
	}

	InitLevelMonsters();

	IncProgress();

	const Player &myPlayer = *MyPlayer;

	if (setlevel) {
		RETURN_IF_ERROR(LoadGameLevelSetLevel(firstflag, lvldir, myPlayer));
	} else {
		RETURN_IF_ERROR(LoadGameLevelStandardLevel(firstflag, lvldir, myPlayer));
	}

	SyncPortals();
	LoadGameLevelSyncPlayerEntry(lvldir);

	IncProgress();
	IncProgress();

	if (firstflag) {
		RETURN_IF_ERROR(InitMainPanel());
	}

	IncProgress();

	UpdateMonsterLights();
	UnstuckChargers();

	LoadGameLevelLightVision();

	if (leveltype == DTYPE_CRYPT) {
		LoadGameLevelCrypt();
	}

#ifndef USE_SDL1
	ActivateVirtualGamepad();
#endif
	LoadGameLevelStartMusic(neededTrack);

	CompleteProgress();

	LoadGameLevelCalculateCursor();
	return {};
}

bool game_loop(bool bStartup)
{
	const uint16_t wait = bStartup ? sgGameInitInfo.nTickRate * 3 : 3;

	for (unsigned i = 0; i < wait; i++) {
		if (!multi_handle_delta()) {
			TimeoutCursor(true);
			return false;
		}
		TimeoutCursor(false);
		GameLogic();
		ClearLastSentPlayerCmd();

		if (!gbRunGame || !gbIsMultiplayer || demo::IsRunning() || demo::IsRecording() || !nthread_has_500ms_passed())
			break;
	}
	return true;
}

void diablo_color_cyc_logic()
{
	if (!*GetOptions().Graphics.colorCycling)
		return;

	if (PauseMode != 0)
		return;

	if (leveltype == DTYPE_CAVES) {
		if (setlevel && setlvlnum == Quests[Q_PWATER]._qslvl) {
			UpdatePWaterPalette();
		} else {
			palette_update_caves();
		}
	} else if (leveltype == DTYPE_HELL) {
		lighting_color_cycling();
	} else if (leveltype == DTYPE_NEST) {
		palette_update_hive();
	} else if (leveltype == DTYPE_CRYPT) {
		palette_update_crypt();
	}
}

bool IsDiabloAlive(bool playSFX)
{
	if (Quests[Q_DIABLO]._qactive == QUEST_DONE && !gbIsMultiplayer) {
		if (playSFX)
			PlaySFX(SfxID::DiabloDeath);
		return false;
	}

	return true;
}

void PrintScreen(SDL_Keycode vkey)
{
	ReleaseKey(vkey);
}

} // namespace devilution
