/**
 * @file gamemenu.cpp
 *
 * Implementation of the in-game menu functions.
 */
#include "gamemenu.h"

#include <array>
#include <cstdint>
#include <limits>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#endif

#include "cursor.h"
#include "diablo_msg.hpp"
#include "engine/backbuffer_state.hpp"
#include "engine/demomode.h"
#include "engine/events.hpp"
#include "engine/render/d3d_local_diagnostics.hpp"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "ingame_settings.h"
#include "levels/dun_tile_data.hpp"
#include "loadsave.h"
#include "menu.h"
#include "multi.h"
#include "options.h"
#include "pfile.h"
#include "qol/floatingnumbers.h"
#include "utils/language.h"

#ifndef USE_SDL1
#include "controls/touch/renderers.h"
#endif

namespace devilution {

bool isGameMenuOpen = false;

namespace {

void GamemenuNewGame(bool bActivate);
void GamemenuOptions(bool) { OpenInGameSettings(); }

TMenuItem sgSingleMenu[] = {
	{ GMENU_ENABLED, N_("Settings"), &GamemenuOptions },
	{ GMENU_ENABLED, N_("Save Game"), &gamemenu_save_game },
	{ GMENU_ENABLED, N_("Load Game"), &gamemenu_load_game },
	{ GMENU_ENABLED, N_("Exit to Main Menu"), &GamemenuNewGame },
	{ GMENU_ENABLED, N_("Quit Game"), &gamemenu_quit_game },
	{ GMENU_ENABLED, nullptr, nullptr },
};
TMenuItem sgMultiMenu[] = {
	{ GMENU_ENABLED, N_("Settings"), &GamemenuOptions },
	{ GMENU_ENABLED, N_("Exit to Main Menu"), &GamemenuNewGame },
	{ GMENU_ENABLED, N_("Quit Game"), &gamemenu_quit_game },
	{ GMENU_ENABLED, nullptr, nullptr },
};

void GamemenuUpdateSingle()
{
	sgSingleMenu[2].setEnabled(gbValidSaveFile);
	sgSingleMenu[0].setEnabled(MyPlayer->_pmode != PM_DEATH && !MyPlayerIsDead);
}

void GamemenuNewGame(bool /*bActivate*/)
{
	for (Player &player : Players) {
		player._pmode = PM_QUIT;
		player._pInvincible = true;
	}
	MyPlayerIsDead = false;
	if (!HeadlessMode) {
		RedrawEverything();
		scrollrt_draw_game_screen();
	}
	CornerStone.activated = false;
	gbRunGame = false;
	gamemenu_off();
}
} // namespace

void gamemenu_exit_game(bool bActivate)
{
	GamemenuNewGame(bActivate);
}

void gamemenu_quit_game(bool bActivate)
{
	GamemenuNewGame(bActivate);
#ifndef NOEXIT
	gbRunGameResult = false;
#else
	ReturnToMainMenu = true;
#endif
}

void gamemenu_load_game(bool /*bActivate*/)
{
	EventHandler saveProc = SetEventHandler(DisableInputEventHandler);
	gamemenu_off();
	ClearFloatingNumbers();
	NewCursor(CURSOR_NONE);
	InitDiabloMsg(EMSG_LOADING);
	RedrawEverything();
	DrawAndBlit();

	const std::array<SDL_Color, 256> prevPalette = logical_palette;
#ifndef USE_SDL1
	DeactivateVirtualGamepad();
	FreeVirtualGamepadTextures();
#endif
	if (std::expected<void, std::string> result = LoadGame(false); !result.has_value()) {
		app_fatal(result.error());
	}
#if !defined(USE_SDL1) && !defined(__vita__)
	if (renderer != nullptr) {
		InitVirtualGamepadTextures(*renderer);
	}
#endif
	ClrDiabloMsg();
	PaletteFadeOut(8, prevPalette);

	LoadPWaterPalette();
	NewCursor(CURSOR_HAND);
	CornerStone.activated = false;
	MyPlayerIsDead = false;
	RedrawEverything();
	DrawAndBlit();
	PaletteFadeIn(8);
	NewCursor(CURSOR_HAND);
	interface_msg_pump();
	SetEventHandler(saveProc);
}

void gamemenu_save_game(bool /*bActivate*/)
{
	using d3d_local_diagnostics::SaveActionStage;
	static uint64_t saveAttemptSequence = 0;
	const bool observeSave = d3d_local_diagnostics::SessionDiagnosticsEnabled()
	    && saveAttemptSequence != std::numeric_limits<uint64_t>::max();
	d3d_local_diagnostics::SaveActionContext saveContext;
	const auto recordSaveAction = [&](SaveActionStage stage) {
		if (!observeSave) return;
		saveContext.stage = stage;
		(void)d3d_local_diagnostics::RecordSessionSaveActionContext(saveContext);
	};
	if (observeSave) {
		saveContext.attempt = ++saveAttemptSequence;
		saveContext.saveNumberKnown = true;
		saveContext.saveNumber = gSaveNumber;
		saveContext.level = currlevel;
		saveContext.levelType = static_cast<int32_t>(leveltype);
		saveContext.setId = static_cast<int32_t>(setlvlnum);
		saveContext.setLevel = setlevel;
		saveContext.cursor = static_cast<int32_t>(pcurs);
		saveContext.multiplayer = gbIsMultiplayer;
		saveContext.validBefore = gbValidSaveFile;
		recordSaveAction(SaveActionStage::Entry);
	}
	if (pcurs != CURSOR_HAND) {
		recordSaveAction(SaveActionStage::RefusedCursor);
		return;
	}

	if (observeSave) {
		saveContext.hasPlayerState = true;
		saveContext.playerMode = static_cast<int32_t>(MyPlayer->_pmode);
		saveContext.dead = MyPlayerIsDead;
	}
	if (MyPlayer->_pmode == PM_DEATH || MyPlayerIsDead) {
		recordSaveAction(SaveActionStage::RefusedDead);
		gamemenu_off();
		return;
	}

	EventHandler saveProc = SetEventHandler(DisableInputEventHandler);
	NewCursor(CURSOR_NONE);
	gamemenu_off();
	InitDiabloMsg(EMSG_SAVING);
	RedrawEverything();
	DrawAndBlit();
	const uint32_t currentTime = SDL_GetTicks();
	recordSaveAction(SaveActionStage::BeforeFull);
	SaveGame();
	const bool fullSaveReportedSuccess = gbValidSaveFile;
	if (observeSave) {
		saveContext.hasValidAfter = true;
		saveContext.validAfter = fullSaveReportedSuccess;
	}
	recordSaveAction(SaveActionStage::AfterFull);
	ClrDiabloMsg();
	if (fullSaveReportedSuccess) {
		InitDiabloMsg(EMSG_GAME_SAVED, currentTime + 1000 - SDL_GetTicks());
		recordSaveAction(SaveActionStage::FeedbackSaved);
	} else {
		InitDiabloMsg(_("Unable to save game."));
		recordSaveAction(SaveActionStage::FeedbackFailed);
	}
	RedrawEverything();
	NewCursor(CURSOR_HAND);
	if (CornerStone.activated) {
		CornerstoneSave();
		if (!demo::IsRunning()) SaveOptions();
	}
	interface_msg_pump();
	SetEventHandler(saveProc);
}

void gamemenu_on()
{
	CloseInGameSettings();
	isGameMenuOpen = true;
	if (!gbIsMultiplayer) {
		gmenu_set_items(sgSingleMenu, GamemenuUpdateSingle);
	} else {
		gmenu_set_items(sgMultiMenu, nullptr);
	}
	gmenu_set_settings_presentation({ nullptr, nullptr, &gamemenu_off, nullptr, 0, true });
	PressEscKey();
}

void gamemenu_off()
{
	CloseInGameSettings();
	isGameMenuOpen = false;
	gmenu_set_items(nullptr, nullptr);
}

void gamemenu_handle_previous()
{
	if (IsInGameSettingsOpen()) {
		gmenu_presskeys(SDLK_ESCAPE);
		return;
	}
	if (gmenu_is_active())
		gamemenu_off();
	else
		gamemenu_on();
}

} // namespace devilution
