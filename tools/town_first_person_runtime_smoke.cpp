// Native first-person integration fixture, linked against the production engine.
// CPU town picking uses a hidden dummy window; physical SDL services are simulated.
// No GPU, physical mouse capture, saves, or live user profile.
#define SDL_MAIN_HANDLED
#ifdef USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#ifdef USE_SDL1
#error This isolated runner supports SDL2/SDL3 only.
#endif
#ifdef UNPACKED_MPQS
#error This runner intentionally selects one original MPQ; adapt unpacked builds separately.
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "town_first_person_path_checks.hpp"
#include "town_first_person_click_checks.hpp"
#include "control/control_chat.hpp"
#include "controls/town_first_person_input.hpp"
#include "controls/remap_keyboard.h"
#include "ingame_settings.h"
#include "cursor.h"
#include "cursor_defs.hpp"
#include "engine/assets.hpp"
#include "engine/load_cel.hpp"
#include "engine/load_file.hpp"
#include "engine/random.hpp"
#include "engine/render/scrollrt.h"
#include "engine/sound.h"
#include "game_mode.hpp"
#include "gamemenu.h"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "init.hpp"
#include "levels/town.h"
#include "lighting.h"
#include "options.h"
#include "panels/quest_log.hpp"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "stores.h"
#include "tables/itemdat.h"
#include "tables/playerdat.hpp"
#include "tables/questdat.hpp"
#include "tables/spelldat.h"
#include "utils/display.h"
#include "utils/paths.h"
#include "utils/ui_fwd.h"

// Existing production function is not currently declared in diablo.h.
namespace devilution { void InitPadmapActions(); }

namespace {
using namespace devilution;
using namespace first_person_root_checks;
constexpr float Pi = 3.14159265358979323846F;
std::ofstream FixtureLog;
size_t Checks = 0;
bool FixtureSdlInitialized = false;
bool FixtureProviderInitialized = false;
EventHandler PreviousHandler = nullptr;
bool FixtureHandlerInstalled = false;
bool FixtureServicesInstalled = false;

void Record(const std::string &message)
{
    std::cout << message << '\n';
    if (FixtureLog) { FixtureLog << message << '\n'; FixtureLog.flush(); }
}
void Check(bool value, const std::string &message)
{
    ++Checks;
    Record(std::string(value ? "PASS " : "FAIL ") + message);
    if (!value) throw std::runtime_error(message);
}

// ADAPT boundary: only physical SDL services are simulated. These callbacks
// never change eligibility, policy, camera pose, command/path, or player state.
// SDL_PushEvent alone cannot change SDL_GetKeyboardState or OS window focus.
struct PhysicalServices {
    static inline bool Relative = false;
    static inline bool Focus = true;
    static inline std::vector<std::pair<SDL_Keycode, uint8_t>> HeldPhysicalKeys;
    static inline unsigned Flushes = 0;
    static bool SetRelative(bool enabled) { Relative = enabled; return true; }
    static bool GetRelative() { return Relative; }
    static bool HasFocus() { return Focus; }
    static uint8_t HeldArrows()
    {
        uint8_t held = 0;
        for (const auto &[key, bit] : HeldPhysicalKeys)
            if (Bit(key) == bit) held |= bit; // Old physical holds cannot inherit a remap.
        return held;
    }
    static void FlushRelative() { ++Flushes; }
    static uint8_t Bit(SDL_Keycode key)
    {
        remap_keyboard_key(&key);
        uint32_t normalized = static_cast<uint32_t>(key);
        if (normalized >= SDLK_A && normalized <= SDLK_Z) normalized -= 'a' - 'A';
        const auto *action = GetOptions().Keymapper.findAction(normalized, KeymapperContext::TownMovement);
        return action != nullptr ? action->movementBit : 0;
    }

    static void Observe(const SDL_Event &event)
    {
        if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) return;
#ifdef USE_SDL3
        const auto key = event.key.key;
#else
        const auto key = event.key.keysym.sym;
#endif
        const auto held = std::find_if(HeldPhysicalKeys.begin(), HeldPhysicalKeys.end(),
            [key](const auto &entry) { return entry.first == key; });
        if (event.type == SDL_EVENT_KEY_DOWN) {
            if (held == HeldPhysicalKeys.end()) HeldPhysicalKeys.emplace_back(key, Bit(key));
            else if (!event.key.repeat) held->second = Bit(key);
        } else if (held != HeldPhysicalKeys.end()) {
            HeldPhysicalKeys.erase(held);
        }
    }
};

void Dispatch(const SDL_Event &event, uint16_t mods)
{
    Check(CurrentEventHandler == GetGameEventHandlerForDiagnostics(),
        "dispatch retains the real live-session production handler");
    PhysicalServices::Observe(event);
    HandleMessage(event, mods); // Exact normal dispatcher; no private policy call.
}

SDL_Event MouseButton(bool down)
{
    SDL_Event e {};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(ghMainWnd);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = gnScreenWidth / 2;
    e.button.y = gnViewportHeight / 2;
    e.button.clicks = 1;
#ifdef USE_SDL3
    e.button.down = down;
#else
    e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
#endif
    return e;
}

ProductionAccess MakeProductionAccess()
{
    ProductionAccess api;
    // Frozen owner names. If root renames a seam, adapt only this block.
    api.dispatch = Dispatch;
    api.sync = SyncFirstPersonInputForDiagnostics;
    api.captured = IsTownFirstPersonInputCaptured;
    api.release = SuspendTownFirstPersonInput;
    api.deliberateResume = [] {
        MousePosition = { gnScreenWidth / 2, gnViewportHeight / 2 };
        Dispatch(MouseButton(true), 0);
        SyncFirstPersonInputForDiagnostics();
        Dispatch(MouseButton(false), 0);
        SyncFirstPersonInputForDiagnostics();
    };
    return api;
}

std::filesystem::path FreshOutput(const std::filesystem::path &parent)
{
    Check(std::filesystem::is_directory(parent), "explicit diagnostics parent exists");
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output = std::filesystem::canonical(parent) / ("run-" + std::to_string(nonce));
    Check(std::filesystem::create_directory(output), "create a fresh private diagnostics directory");
    const auto profile = output / "private-profile";
    Check(std::filesystem::create_directory(profile), "create a fresh empty fixture profile");
    std::ofstream ini(profile / "diablo.ini", std::ios::binary);
    ini << "[Language]\nCode=en\n";
    Check(static_cast<bool>(ini), "initialize only the private options file");
    return output;
}

void SetDummyEnvironment()
{
#ifdef _WIN32
    Check(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0, "force dummy SDL video driver");
    Check(_putenv_s("SDL_AUDIODRIVER", "dummy") == 0, "force dummy SDL audio driver");
#else
    Check(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0, "force dummy SDL video driver");
    Check(setenv("SDL_AUDIODRIVER", "dummy", 1) == 0, "force dummy SDL audio driver");
#endif
    SDL_SetMainReady();
#ifdef USE_SDL3
    Check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS), std::string("SDL dummy initialization: ") + SDL_GetError());
#else
    Check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) == 0,
        std::string("SDL dummy initialization: ") + SDL_GetError());
#endif
    FixtureSdlInitialized = true;
    const char *driver = SDL_GetCurrentVideoDriver();
    Check(driver != nullptr && std::string_view(driver) == "dummy", "reject any non-dummy SDL video driver");
    Check(ghMainWnd == nullptr && renderer == nullptr, "fixture starts without an engine window or GPU renderer");
#ifdef USE_SDL3
    ghMainWnd = SDL_CreateWindow("Diablo 3D private input fixture", 640, 480, SDL_WINDOW_HIDDEN);
#else
    ghMainWnd = SDL_CreateWindow("Diablo 3D private input fixture", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_HIDDEN);
#endif
    Check(ghMainWnd != nullptr, std::string("create dummy hidden window: ") + SDL_GetError());
    Check((SDL_GetWindowFlags(ghMainWnd) & SDL_WINDOW_HIDDEN) != 0, "fixture window remains hidden");
    // Never ShowWindow, RaiseWindow, set OS focus, or set actual relative mode.
}

void SelectOriginalArchive()
{
    std::filesystem::path selected;
    for (const char *name : { "DIABDAT.MPQ", "diabdat.mpq", "spawn.mpq", "SPAWN.MPQ" }) {
        const auto candidate = std::filesystem::path(paths::BasePath()) / name;
        if (std::filesystem::is_regular_file(candidate)) { selected = candidate; break; }
    }
    Check(!selected.empty(), "explicit data directory contains an original game archive");
    gbIsSpawn = selected.filename() == "spawn.mpq" || selected.filename() == "SPAWN.MPQ";
    auto archive = MpqArchive::Open(selected.string().c_str());
    Check(archive.has_value(), "open the selected original archive read-only");
    MpqArchives.insert_or_assign(MainMpqPriority, std::move(*archive));
    auto ref = FindAsset("levels\\towndata\\town.cel");
    Check(ref.ok() && ref.archive == &MpqArchives.at(MainMpqPriority), "town geometry comes from the explicit original archive");
}

void ClosePanels()
{
    invflag = SpellbookFlag = CharFlag = QuestLogIsOpen = SpellSelectFlag = ChatFlag = false;
    ChatInputState.reset();
    IsStashOpen = IsVisualStoreOpen = false;
    ActiveStore = TalkID::None;
    ResetMainPanelButtons();
    gmenu_init_menu(); // Real headless initializer; does not load/present menu art.
}

void ResetAnimation()
{
    MyPlayer->_pdir = MyPlayer->tempDirection = Direction::South;
    MyPlayer->previewCelSprite = std::nullopt;
    SetPlrAnims(*MyPlayer);
    NewPlrAnim(*MyPlayer, player_graphic::Stand, Direction::South);
}

void InitializeFixture(const std::filesystem::path &data, const std::filesystem::path &assets,
    const std::filesystem::path &output)
{
    paths::SetBasePath(std::filesystem::canonical(data).string());
    paths::SetAssetsPath(std::filesystem::canonical(assets).string());
    paths::SetPrefPath((output / "private-profile").string());
    paths::SetConfigPath((output / "private-profile").string());
    HeadlessMode = true;
    gbIsHellfire = gbIsMultiplayer = false;
    gbSoundOn = gbMusicOn = gbSndInited = false;
    leveltype = DTYPE_TOWN;
    currlevel = 0;
    setlevel = false;
    gbBufferMsgs = 0;
    ControlDevice = ControlMode = ControlTypes::KeyboardAndMouse;
    LoadCoreArchives();
    SelectOriginalArchive();
    OverridePaths.emplace_back(paths::PrefPath());
    InitKeymapActions();
    InitPadmapActions();
    LoadOptions();
    GetOptions().Graphics.hardwareCursor.SetValue(false);
    GetOptions().Graphics.townViewStartIn3D.SetValue(true);
    GetOptions().Graphics.townViewCameraMode.SetValue(3);
    GetOptions().Graphics.townViewCameraSensitivity.SetValue(100);
    GetOptions().Audio.walkingSound.SetValue(false);
    GetOptions().Gameplay.autoPickupInTown.SetValue(false);
    // No SaveOptions, InitSingle, NetInit, LoadGame or SaveGame.
    LoadPlayerDataFiles();
    LoadItemData();
    LoadSpellData();
    LoadQuestData();
    for (auto &quest : Quests) quest._qactive = QUEST_NOTAVAIL;
    Players.resize(1);
    MyPlayerId = 0;
    MyPlayer = InspectPlayer = &Players[0];
    CreatePlayer(*MyPlayer, HeroClass::Warrior);
    MyPlayer->plractive = true;
    MyPlayer->plrlevel = 0;
    MyPlayer->plrIsOnSetLevel = false;
    MyPlayer->_pLvlChanging = MyPlayer->_pInvincible = false;
    MyPlayer->lightId = NO_LIGHT;
    MyPlayerIsDead = false;
    Check(MyPlayer->_pHitPoints > 0 && MyPlayer->_pMaxHP > 0, "create a healthy real native Warrior");
    const auto sol = LoadLevelSOLData();
    Check(sol.has_value(), "load the original town SOL collision data");
    pDungeonCels = LoadFileInMem("levels\\towndata\\town.cel");
    pMegaTiles = LoadFileInMem<MegaTile>("levels\\towndata\\town.til");
    pSpecialCels = LoadCel("levels\\towndata\\towns", 64);
    SetDungeonMicros(pDungeonCels, MicroTileLen);
    CreateTown(ENTRY_MAIN);
    FixturePlacePlayer(FindClearPatch(Check));
    ResetAnimation();
    InitLighting();
    ActivateVision(MyPlayer->position.tile, MyPlayer->_pLightRad, MyPlayerId);
    InitCursor();
    InitLevelCursor();
    InitStores();
    ClosePanels();
    gnScreenWidth = 640;
    gnScreenHeight = 480;
    gnViewportHeight = 352;
    CalculatePanelAreas();
    CalcViewportGeometry();
    sgGameInitInfo = {};
    sgGameInitInfo.size = sizeof(GameData);
    sgGameInitInfo.isSpawn = gbIsSpawn;
    sgGameInitInfo.programid = GetGameId();
    sgGameInitInfo.nTickRate = 20;
    sgGameInitInfo.bRunInTown = 0;
    Check(SNetInitializeProvider(SELCONN_LOOPBACK, nullptr), "initialize only the isolated loopback provider");
    FixtureProviderInitialized = true;
    GameData wireGame = sgGameInitInfo;
    SwapGameDataLE(wireGame);
    int playerId = -1;
    Check(SNetCreateGame("private-first-person-input-fixture", nullptr, reinterpret_cast<char *>(&wireGame),
        sizeof(wireGame), &playerId), "create a private local loopback game without loading a save");
    Check(playerId == 0 && IsLoopback, "fixture loopback player identity is zero");
    std::fill(std::begin(sgwPackPlrOffsetTbl), std::end(sgwPackPlrOffsetTbl), 0);
    gbActive = gbRunGame = gbProcessPlayers = true;
    PauseMode = 0;
    InitializeTownViewForGame();
    SetTownViewCameraMode(TownCameraMode::FirstPerson);
    const TownFirstPersonInputServicesForDiagnostics services {
        PhysicalServices::SetRelative, PhysicalServices::GetRelative, PhysicalServices::HeldArrows,
        PhysicalServices::HasFocus, PhysicalServices::FlushRelative
    };
    PreviousHandler = SetEventHandler(GetGameEventHandlerForDiagnostics());
    FixtureHandlerInstalled = true;
    SetTownFirstPersonInputServicesForDiagnostics(&services);
    FixtureServicesInstalled = true;
    Check(renderer == nullptr && HeadlessMode, "initialization creates no renderer and retains headless player animation");
    SetRndSeed(0x13579BDF); // AFTER CreatePlayer and all initialization, never during a tested motion.
}

struct StepSample {
    int tileX, tileY, futureX, futureY, tempX, tempY;
    int mode, direction, frame, frameTick, frames, ticksPerFrame, path0, path1, action;
    bool operator==(const StepSample &) const = default;
};
StepSample Sample()
{
    const Player &p = *MyPlayer;
    return { p.position.tile.x, p.position.tile.y, p.position.future.x, p.position.future.y,
        p.position.temp.x, p.position.temp.y, static_cast<int>(p._pmode), static_cast<int>(p._pdir),
        p.AnimInfo.currentFrame, p.AnimInfo.tickCounterOfCurrentFrame, p.AnimInfo.numberOfFrames,
        p.AnimInfo.ticksPerFrame, p.walkpath[0], p.walkpath[1], static_cast<int>(p.destAction) };
}

std::vector<StepSample> ExecuteOneNativeStep(const ProductionAccess &api, Point origin, float yaw,
    Point target, bool firstPerson, SDL_Keycode movementKey = SDLK_UP)
{
    api.release();
    DrainEmittedWalks(Check);
    for (auto key : { SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT }) Arrow(api, key, false);
    if (firstPerson) ResumeAt(api, Check, origin, yaw);
    else FixturePlacePlayer(origin);
    ResetAnimation();
    ClearLastSentPlayerCmd();
    const uint32_t rngBefore = GetLCGEngineState();
    if (firstPerson) {
        Arrow(api, movementKey, true);
        plrctrls_after_game_logic();
    } else {
        NetSendCmdLoc(MyPlayerId, true, CMD_WALKXY, target); // Native reference, same parser/cadence.
    }
    const auto commands = DrainEmittedWalks(Check);
    Check(commands.size() == 1 && Point {commands[0].x, commands[0].y} == target,
        firstPerson ? "candidate queues the same adjacent target as native reference" : "native reference queues adjacent target");
    std::vector<StepSample> samples;
    ProcessPlayers(); // Actual native tick starts walk; no animation/frame assignment.
    samples.push_back(Sample());
    Check(MyPlayer->isWalking() && MyPlayer->position.future == target, "native ProcessPlayers begins the queued step");
    ClearLastSentPlayerCmd();
    if (firstPerson) {
        Arrow(api, movementKey, false);
        api.sync();
        plrctrls_after_game_logic();
    } else {
        NetSendCmdLoc(MyPlayerId, true, CMD_WALKXY, MyPlayer->position.future);
    }
    const auto stops = DrainEmittedWalks(Check);
    Check(stops.size() == 1 && Point {stops[0].x, stops[0].y} == target, "release stops only the path after the in-progress native step");
    for (unsigned tick = 1; tick < 128; ++tick) {
        ClearLastSentPlayerCmd();
        ProcessPlayers();
        samples.push_back(Sample());
        if (firstPerson) {
            plrctrls_after_game_logic();
            Check(DrainEmittedWalks(Check).empty(), "released candidate sends no additional step at later native ticks");
        }
        if (MyPlayer->position.tile == target && MyPlayer->_pmode == PM_STAND) break;
    }
    Check(samples.size() < 128 && MyPlayer->position.tile == target && MyPlayer->_pmode == PM_STAND,
        "actual native walk completes within the finite logical-tick limit");
    Check(MyPlayer->walkpath[0] == WALK_NONE && MyPlayer->destAction == ACTION_NONE,
        "completed step has no queued movement/action");
    Check(GetLCGEngineState() == rngBefore, "headless town movement/adapter does not consume the simulation RNG");
    api.release();
    DrainEmittedWalks(Check);
    return samples;
}

void NativeCadenceChecks(const ProductionAccess &api)
{
    const Point origin = FindClearPatch(Check);
    const std::array<Point, 8> deltas {{{-1,0}, {-1,-1}, {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}}};
    for (const uint8_t run : { uint8_t{0}, uint8_t{1} }) {
        sgGameInitInfo.bRunInTown = run;
        for (size_t sector = 0; sector < deltas.size(); ++sector) {
            const Point target = origin + Displacement {deltas[sector].x, deltas[sector].y};
            const float yaw = static_cast<float>(sector) * Pi / 4;
            const auto native = ExecuteOneNativeStep(api, origin, yaw, target, false);
            const auto candidate = ExecuteOneNativeStep(api, origin, yaw, target, true);
            const auto wasd = ExecuteOneNativeStep(api, origin, yaw, target, true, SDLK_W);
            Check(native == wasd, "WASD W preserves complete native movement/animation/collision cadence");
            Check(native == candidate, "complete tile/future/mode/frame/path cadence equals native CMD_WALKXY, run="
                + std::to_string(run) + ", sector=" + std::to_string(sector)
                + ", ticks=" + std::to_string(native.size()));
        }
    }
    sgGameInitInfo.bRunInTown = 0;
}

void RelativeMouseAndFocusChecks(const ProductionAccess &api)
{
    ResumeAt(api, Check, FindClearPatch(Check), 0);
    const auto before = GetTownViewCameraState();
    const auto nativeBefore = Sample();
    const auto rngBefore = GetLCGEngineState();
    SDL_Event motion {};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = SDL_GetWindowID(ghMainWnd);
    motion.motion.x = 25; // Absolute position deliberately differs from center.
    motion.motion.y = 30;
    motion.motion.xrel = 10;
    motion.motion.yrel = 5;
    // Exercise the SDL queue and real FetchMessage, including coordinate conversion.
    // Drain bootstrap window events without dispatch: they are unrelated fixture setup.
    SDL_Event ignored {};
    while (SDL_PollEvent(&ignored)) {}
#ifdef USE_SDL3
    Check(SDL_PushEvent(&motion), "enqueue relative mouse event in SDL dummy queue");
#else
    Check(SDL_PushEvent(&motion) == 1, "enqueue relative mouse event in SDL dummy queue");
#endif
    SDL_Event fetched {};
    uint16_t mods = 0;
    Check(FetchMessage(&fetched, &mods) && fetched.type == SDL_EVENT_MOUSE_MOTION,
        "real FetchMessage returns the injected relative mouse event");
    Check(fetched.motion.xrel == motion.motion.xrel && fetched.motion.yrel == motion.motion.yrel,
        "production SDL conversion preserves relative deltas without a renderer");
    Dispatch(fetched, mods);
    api.sync();
    const auto after = GetTownViewCameraState();
    Check(after.yaw > before.yaw && after.pitch > before.pitch,
        "real event dispatch looks right/down from positive relative mouse deltas");
    Check(std::abs(after.yaw - before.yaw - 0.06F) < 0.00001F
        && std::abs(after.pitch - before.pitch - 0.03F) < 0.00001F,
        "real dispatch applies current 100-percent radians-per-pixel sensitivity once");
    Check(Sample() == nativeBefore && GetLCGEngineState() == rngBefore,
        "mouse look leaves actor animation/path and RNG untouched");
    Check(DrainEmittedWalks(Check).empty(), "relative mouse look emits no movement/gameplay command");
    Check(GetTownFirstPersonPointer({25,30}) == Point {gnScreenWidth / 2, gnViewportHeight / 2},
        "captured pointer is the logical world center rather than full-window center");
    Arrow(api, SDLK_UP, true);
    plrctrls_after_game_logic();
    DrainEmittedWalks(Check);
    PhysicalServices::Focus = false;
    api.sync(); // Polling path is real; only the platform focus observation is injected.
    Check(!api.captured() && !PhysicalServices::Relative, "loss of focus releases capture during production Sync");
    DrainEmittedWalks(Check);
    Check(MyPlayer->walkpath[0] == WALK_NONE, "focus loss cancels the adapter-owned pending native path");
    Arrow(api, SDLK_UP, false);
    PhysicalServices::Focus = true;
    api.sync();
    Check(!api.captured(), "focus return alone does not reacquire input");
    api.deliberateResume();
    Check(api.captured(), "a fresh deliberate world gesture reacquires capture after focus return");
    plrctrls_after_game_logic();
    Check(DrainEmittedWalks(Check).empty(), "focus-cycle resume cannot resurrect the released arrow");
    Check(PhysicalServices::Flushes > 0, "production adapter requests platform stale-delta flushes");
    api.release();
    DrainEmittedWalks(Check);
}

void Cleanup() noexcept
{
    // Keep test services installed while releasing, so OS capture is never called.
    if (FixtureHandlerInstalled) {
        SuspendTownFirstPersonInput();
        SetEventHandler(PreviousHandler);
        FixtureHandlerInstalled = false;
    }
    if (FixtureServicesInstalled) {
        SetTownFirstPersonInputServicesForDiagnostics(nullptr);
        FixtureServicesInstalled = false;
    }
    gbRunGame = gbProcessPlayers = false;
    if (FixtureProviderInitialized) { SNetDestroy(); FixtureProviderInitialized = false; }
    if (ghMainWnd != nullptr) {
        SDL_Window *fixtureWindow = ghMainWnd;
        ghMainWnd = nullptr;
        SDL_DestroyWindow(fixtureWindow);
    }
    if (FixtureSdlInitialized) { SDL_Quit(); FixtureSdlInitialized = false; }
}
} // namespace

// Added production dispatcher regression cases, sharing this isolated runner.
#include "town_follow_input_checks.hpp"
#include "movement_keymapping_runtime_checks.hpp"
#include "movement_keymapping_r2_runtime_checks.hpp"

int main(int argc, char **argv)
{
    if (argc != 4) {
        std::cerr << "usage: first_person_production_runner <original-game-data-dir> <built-assets-dir> <private-diagnostics-parent>\n";
        return 2;
    }
    try {
        const auto output = FreshOutput(argv[3]);
        FixtureLog.open(output / "checks.log", std::ios::binary);
        Check(static_cast<bool>(FixtureLog), "open private fixture log");
        Record("SCOPE: real production SDL dispatch/Sync/movement/path/ProcessPlayers; physical SDL services simulated.");
        Record("NOT VALIDATED: physical capture/focus/Alt+Tab, reticle composition, NPC/item/combat picking, FPS, multiplayer sync, legacy demos.");
        Record("PRIVATE PROFILE: " + (output / "private-profile").string());
        SetDummyEnvironment();
        InitializeFixture(argv[1], argv[2], output);
        const auto api = MakeProductionAccess();
        RunProductionWalkChecks(api, Check);
        RunInventorySuspensionCheck(api, Check);
        NativeCadenceChecks(api);
        RelativeMouseAndFocusChecks(api);
        first_person_root_supplement::RunCpuDeferredClickChecks(api, Check);
        first_person_root_supplement::RunPendingNativeWalkGateChecks(api, Check, [](bool focused) { PhysicalServices::Focus = focused; });
        FollowInputChecks(api);
        MovementKeymappingRuntimeChecks(api);
        MovementKeymappingR2RuntimeChecks(api);
        FollowClickChecks(api);
        Check(renderer == nullptr, "no GPU renderer was created during the entire fixture");
        Check((SDL_GetWindowFlags(ghMainWnd) & SDL_WINDOW_HIDDEN) != 0, "dummy window remains hidden at completion");
        Cleanup();
        Record("PASS COMPLETE checks=" + std::to_string(Checks));
        return 0;
    } catch (const std::exception &e) {
        Record("FAIL ABORT " + std::string(e.what()));
        Cleanup();
        return 1;
    }
}
