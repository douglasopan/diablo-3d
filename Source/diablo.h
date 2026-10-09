/**
 * @file diablo.h
 *
 * Interface of the main game initialization functions.
 */
#pragma once

#include <cstdint>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#else
#include <SDL.h>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

#ifdef _DEBUG
#include "tables/monstdat.h"
#endif
#include "engine/direction.hpp"
#include "engine/events.hpp"
#include "levels/gendung.h"
#include "utils/attributes.h"
#include "utils/endian_read.hpp"

namespace devilution {

// Base game branding ids.
constexpr uint32_t GameIdDiabloFull = LoadBE32("DRTL");  // Diablo Retail (full game)
constexpr uint32_t GameIdDiabloSpawn = LoadBE32("DSHR"); // Diablo Shareware (spawn)
/** Generic ID for mods that do not set there own. */
constexpr uint32_t GameIdGenericMod = LoadBE32("DXMD"); // DevilutionX + mod

/**
 * @brief The multiplayer game mode branding id.
 *
 * This is a cosmetic branding identifier (shown in the game browser / chat), NOT a compatibility check.
 */
[[nodiscard]] uint32_t GetGameId();

enum clicktype : int8_t {
	CLICK_NONE,
	CLICK_LEFT,
	CLICK_RIGHT,
};

/**
 * @brief Specifies what game logic step is currently executed
 */
enum class GameLogicStep : uint8_t {
	None,
	ProcessPlayers,
	ProcessMonsters,
	ProcessObjects,
	ProcessMissiles,
	ProcessItems,
	ProcessTowners,
	ProcessItemsTown,
	ProcessMissilesTown,
};

enum class PlayerActionType : uint8_t {
	None,
	Walk,
	Spell,
	SpellMonsterTarget,
	SpellPlayerTarget,
	Attack,
	AttackMonsterTarget,
	AttackPlayerTarget,
	OperateObject,
};

extern DVL_API_FOR_TEST Point MousePosition;

extern bool gbRunGameResult;
extern bool ReturnToMainMenu;
extern bool gbProcessPlayers;
extern DVL_API_FOR_TEST bool gbLoadGame;
extern bool cineflag;
/* These are defined in fonts.h */
extern void FontsCleanup();
extern DVL_API_FOR_TEST int PauseMode;
extern clicktype sgbMouseDown;
extern uint16_t gnTickDelay;
extern char gszProductName[64];

extern PlayerActionType LastPlayerAction;

void InitKeymapActions();
void SetCursorPos(Point position);
/** Captured first-person input uses the logical world viewport center. */
bool IsTownFirstPersonInputCaptured();
/** Eligible keyboard movement in FPP or Third; Third does not capture mouse. */
bool IsTownCameraMovementInputActive();
Point GetTownFirstPersonPointer(Point absolute);
Direction GetTownFirstPersonMoveDirection();
/** Release capture, owned arrows and only a walk issued by this adapter. */
void SuspendTownFirstPersonInput();
void StopTownFirstPersonWalk();
/** Native interaction takes ownership before its command is dispatched. */
void CancelTownFirstPersonWalk();

/** Diagnostic seam replaces only SDL capture/focus/keyboard services.
 * Supplying services permits a headless fixture; it does not bypass game/UI
 * gates or prove real operating-system capture. nullptr restores SDL services.
 * The adapter copies this struct and never retains the caller's pointer. */
struct TownFirstPersonInputServicesForDiagnostics {
	bool (*setRelativeMouse)(bool) = nullptr;
	bool (*relativeMouse)() = nullptr;
	uint8_t (*heldArrows)() = nullptr;
	bool (*hasFocus)() = nullptr;
	void (*flushRelativeMouse)() = nullptr;
	/** Optional synthetic scancode state. When provided, exercises the same
	 * owned-scancode/epoch polling loop as SDL, instead of heldArrows slots. */
	bool (*scancodeHeld)(int) = nullptr;
};
void SetTownFirstPersonInputServicesForDiagnostics(const TownFirstPersonInputServicesForDiagnostics *services);
/** Calls the exact live-session handler and synchronization used by gameplay. */
void DispatchGameEventForDiagnostics(const SDL_Event &event, uint16_t modState);
void SyncFirstPersonInputForDiagnostics();
/** Same post-draw queue flush used by the live game loop. */
void FlushFirstPersonClicksForDiagnostics();
EventHandler GetGameEventHandlerForDiagnostics();
void FreeGameMem();
bool StartGame(bool bNewGame, bool bSinglePlayer);
[[noreturn]] void diablo_quit(int exitStatus);
int DiabloMain(int argc, char **argv);
bool TryIconCurs();
void diablo_pause_game();
bool diablo_is_focused();
void diablo_focus_pause();
void diablo_focus_unpause();
bool PressEscKey();
void DisableInputEventHandler(const SDL_Event &event, uint16_t modState);
std::expected<void, std::string> LoadGameLevel(bool firstflag, lvl_entry lvldir);
bool IsDiabloAlive(bool playSFX);
void PrintScreen(SDL_Keycode vkey);

/**
 * @param bStartup Process additional ticks before returning
 */
bool game_loop(bool bStartup);
void diablo_color_cyc_logic();

/* rdata */

#ifdef _DEBUG
extern bool DebugDisableNetworkTimeout;
#endif

/**
 * @brief Specifies what game logic step is currently executed
 */
extern GameLogicStep gGameLogicStep;

} // namespace devilution
