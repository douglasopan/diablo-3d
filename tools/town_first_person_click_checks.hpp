#pragma once
// CPU ground-picking and pending native-command integration checks.
// Include after town_first_person_path_checks.hpp; wired by the native runner.
// Physical input is isolated from the active player profile.
// Physical capture/focus remains the runner's explicitly simulated SDL service.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "control/control_chat.hpp"
#include "cursor.h"
#include "engine/light_tables.hpp"
#include "engine/load_file.hpp"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "engine/render/scrollrt.h"
#include "engine/surface.hpp"
#include "headless_mode.hpp"
#include "options.h"
#include "utils/display.h"
#include "utils/palette_blending.hpp"

namespace first_person_root_supplement {
using namespace devilution;
using namespace first_person_root_checks;
constexpr float Pi = 3.14159265358979323846F;
constexpr float Sensitivity = 0.006F; // Runner sets the real option to 100%.

inline SDL_Event Motion(int x, int y)
{
    SDL_Event event {};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = SDL_GetWindowID(ghMainWnd);
    event.motion.x = 17; // Deliberately unrelated to the captured world center.
    event.motion.y = 23;
    event.motion.xrel = x;
    event.motion.yrel = y;
    return event;
}
inline void Click(const ProductionAccess &api, bool down)
{
    SDL_Event event {};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = SDL_GetWindowID(ghMainWnd);
    event.button.button = SDL_BUTTON_LEFT;
    event.button.clicks = 1;
    event.button.x = 17;
    event.button.y = 23;
#ifdef USE_SDL3
    event.button.down = down;
#else
    event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
#endif
    api.dispatch(event, 0);
}
inline Point Center() { return { gnScreenWidth / 2, gnViewportHeight / 2 }; }

struct PickResult {
    bool valid = false;
    Point tile {};
    int towner = -1, item = -1, player = -1;
    bool IsWalkableGround() const
    {
        return valid && towner == -1 && item == -1 && player == -1
            && PosOkPlayer(*MyPlayer, tile);
    }
    bool operator==(const PickResult &) const = default;
};
inline PickResult Pick()
{
    PickResult result;
    result.valid = PickTownView(Center(), result.tile, result.towner, result.item, result.player);
    return result;
}
inline bool Near(float a, float b) { return std::abs(a - b) < 0.00002F; }
inline bool NearAngle(float a, float b) { return std::abs(std::remainder(a - b, 2 * Pi)) < 0.00002F; }
inline bool SamePose(TownViewCameraState a, TownViewCameraState b)
{
    return NearAngle(a.yaw, b.yaw) && Near(a.pitch, b.pitch) && Near(a.distance, b.distance)
        && Near(a.offsetX, b.offsetX) && Near(a.offsetZ, b.offsetZ) && a.mode == b.mode;
}
inline void Pose(float yaw, float pitch)
{
    SetTownViewCameraPoseForDiagnostics({ yaw, pitch, 0, {} });
}
inline void DrawCpu(const Surface &out, const CheckFn &check)
{
    check(!*GetOptions().Graphics.townViewGpuRendering, "supplement explicitly disables the GPU before every world draw");
    check(DrawTownView(out, true), "actual CPU DrawTownView completes without SDL presentation");
    const auto state = GetTownViewRendererState();
    check(!state.requestedGpu && !state.usedGpu, "actual draw confirms GPU was neither requested nor used");
}

// Copy exact emitted bodies without parsing, to reproduce a request still in
// flight when UI/focus changes. Real ParseCmd is deferred until after the gate.
inline std::vector<TCmdLoc> TakeWalksWithoutParse(const CheckFn &check)
{
    std::vector<TCmdLoc> walks;
    uint8_t sender = 255;
    void *raw = nullptr;
    size_t size = 0;
    unsigned packets = 0;
    while (SNetReceiveMessage(&sender, &raw, &size)) {
        check(++packets <= 64 && sender == MyPlayerId && size >= sizeof(TPktHdr),
            "bounded unparsed actual loopback walk packet has native header/local sender");
        TPktHdr header {};
        std::memcpy(&header, raw, sizeof(header));
        check(Swap16LE(header.wLen) == size, "unparsed native packet retains exact serialized length");
        const auto *body = reinterpret_cast<const std::byte *>(raw) + sizeof(header);
        size_t remaining = size - sizeof(header);
        while (remaining != 0) {
            check(remaining >= sizeof(TCmdLoc), "unparsed walk body is complete");
            TCmdLoc command {};
            std::memcpy(&command, body, sizeof(command));
            check(command.bCmd == CMD_WALKXY, "supplement emits only actual native walk/stop commands");
            walks.push_back(command);
            body += sizeof(command);
            remaining -= sizeof(command);
        }
    }
    return walks;
}
inline void ParseObservedWalk(const TCmdLoc &walk, const CheckFn &check)
{
    check(ParseCmd(MyPlayerId, reinterpret_cast<const TCmd *>(&walk), sizeof(walk)) == sizeof(walk),
        "production ParseCmd consumes the exact previously observed walk body");
}

struct PosePair {
    float yaw = 0, pitch = 0.55F;
    PickResult first, later;
};
inline PosePair FindTwoGroundTargets(const Surface &out, Point origin, const CheckFn &check)
{
    // Bounded scene-dependent discovery: never synthesize IDs/ground or replace
    // the original SOL/map. Two different independent render targets make an
    // event-order regression observable instead of passing on one repeated tile.
    for (const float pitch : { 0.55F, 0.70F }) {
        for (unsigned sector = 0; sector < 8; ++sector) {
            const float yaw = static_cast<float>(sector) * Pi / 4;
            Pose(yaw, pitch);
            DrawCpu(out, check);
            const PickResult a = Pick();
            Pose(yaw + 150 * Sensitivity, pitch);
            DrawCpu(out, check);
            const PickResult b = Pick();
            if (a.IsWalkableGround() && b.IsWalkableGround() && a.tile != b.tile
                && a.tile != origin && b.tile != origin)
                return { std::remainder(yaw, 2 * Pi), pitch, a, b };
        }
    }
    check(false, "original clear patch exposes two distinct walkable ground targets in bounded camera search");
    return {};
}

inline void RunCpuDeferredClickChecks(const ProductionAccess &api, const CheckFn &check)
{
    check(HeadlessMode && renderer == nullptr, "CPU click fixture retains headless engine and dummy window without a renderer");
    check(IsLoopback && !gbIsMultiplayer, "CPU picking case uses only the initialized single-player loopback");
    // Keep OFF through the last draw. Restore only after api.release and the last
    // check. No presentation, screenshot, resource export or model selection.
    const bool savedGpu = *GetOptions().Graphics.townViewGpuRendering;
    const bool savedAa = *GetOptions().Graphics.townViewAntialiasing;
    const bool savedHorizon = *GetOptions().Graphics.townViewHorizon;
    const auto savedLogical = logical_palette;
    const auto savedSystem = system_palette;
    GetOptions().Graphics.townViewGpuRendering.SetValue(false);
    GetOptions().Graphics.townViewAntialiasing.SetValue(false);
    GetOptions().Graphics.townViewHorizon.SetValue(false);
    GetOptions().Graphics.townViewCameraSensitivity.SetValue(100);
    // LoadPalette/LoadPaletteAndInitBlending return immediately in HeadlessMode.
    // Initialize their real data directly, like town_view_smoke's LoadTown.
    std::array<Color, 256> original {};
    LoadFileInMem("levels\\towndata\\town.pal", original);
    for (size_t i = 0; i < original.size(); ++i)
        logical_palette[i] = system_palette[i] = original[i].toSDL();
    GenerateBlendedLookupTable(logical_palette.data());
    MakeLightTable();
    OwnedSurface frame(640, 480);
    check(frame.surface != nullptr && frame.w() == 640 && frame.h() == 480, "allocate only a private CPU indexed 640x480 surface");
    gnScreenWidth = 640;
    gnScreenHeight = 480;
    gnViewportHeight = 352;
    CalculatePanelAreas();
    CalcViewportGeometry();
    const Point origin = FindClearPatch(check); // Integrated root version requires dPiece != 0.
    ResumeAt(api, check, origin, 0);
    const auto pair = FindTwoGroundTargets(frame, origin, check);
    const uint32_t rngBefore = GetLCGEngineState();

    // motion -> down -> motion -> up. First motion applies now, later motion
    // remains in the same queue after down, and cannot change this frame's pick.
    Pose(pair.yaw - 10 * Sensitivity, pair.pitch);
    api.dispatch(Motion(10, 0), 0);
    const auto firstDownPose = GetTownViewCameraState();
    check(NearAngle(firstDownPose.yaw, pair.yaw) && Near(firstDownPose.pitch, pair.pitch), "first mouse motion establishes the exact first-down camera pose");
    check(!Pick().valid, "look invalidates the previous render's picking epoch");
    Click(api, true);
    api.dispatch(Motion(150, 0), 0);
    Click(api, false);
    check(SamePose(GetTownViewCameraState(), firstDownPose), "post-down look waits in event order until after the first click");
    check(TakeWalksWithoutParse(check).empty(), "queued down/up generates no CMD before a fresh CPU draw");
    check(MyPlayer->position.tile == origin && MyPlayer->position.future == origin, "deferred input never edits native player position");
    DrawCpu(frame, check);
    const PickResult firstPick = Pick();
    check(firstPick == pair.first, "fresh first-down render independently reproduces the expected ground ID/tile");
    FlushFirstPersonClicksForDiagnostics(); // Exact post-draw production queue flush.
    const auto clicked = TakeWalksWithoutParse(check);
    check(clicked.size() == 1 && Point {clicked[0].x, clicked[0].y} == firstPick.tile,
        "flush sends one native click command using the first-down pose, before its later look");
    for (const auto &walk : clicked) ParseObservedWalk(walk, check);
    check(NearAngle(GetTownViewCameraState().yaw, pair.yaw + 150 * Sensitivity)
        && Near(GetTownViewCameraState().pitch, pair.pitch), "post-click look applies after the first native action");
    check(!Pick().valid, "later look invalidates the pick epoch immediately after the flush");
    check(api.captured() && sgbMouseDown == CLICK_NONE && LastPlayerAction == PlayerActionType::None,
        "queued up releases native held-click state while capture remains active");
    DrawCpu(frame, check);
    const PickResult laterPick = Pick();
    check(laterPick == pair.later && laterPick.tile != firstPick.tile, "next draw refreshes center pick to the later camera target");
    ClearLastSentPlayerCmd();
    Click(api, true);
    Click(api, false);
    const auto next = TakeWalksWithoutParse(check);
    check(next.size() == 1 && Point {next[0].x, next[0].y} == laterPick.tile,
        "next click consumes the fresh later render's independent native target");
    for (const auto &walk : next) ParseObservedWalk(walk, check);
    check(api.captured() && sgbMouseDown == CLICK_NONE, "fresh click/up preserves capture without a stuck native mouse action");

    // Opposite pitch deltas must not coalesce: clamp(1.3+.6)-.6 == .8,
    // whereas summing them would incorrectly leave 1.3. Down is stale -> queued.
    api.release();
    DrainEmittedWalks(check);
    ResumeAt(api, check, origin, pair.yaw);
    Pose(pair.yaw, 1.30F);
    Click(api, true);
    api.dispatch(Motion(0, 100), 0);
    api.dispatch(Motion(0, -100), 0);
    Click(api, false);
    check(Near(GetTownViewCameraState().pitch, 1.30F) && TakeWalksWithoutParse(check).empty(),
        "opposite queued pitch samples remain deferred before draw");
    DrawCpu(frame, check);
    const PickResult clampPick = Pick();
    check(clampPick.valid && clampPick.towner == -1 && clampPick.item == -1 && clampPick.player == -1,
        "steep first-down pose has an actual original-world ground pick");
    FlushFirstPersonClicksForDiagnostics();
    const auto clampClick = TakeWalksWithoutParse(check);
    check(clampClick.size() == 1 && Point {clampClick[0].x, clampClick[0].y} == clampPick.tile,
        "pitch-clamp queue executes its click against the pre-look pick");
    for (const auto &walk : clampClick) ParseObservedWalk(walk, check);
    check(Near(GetTownViewCameraState().pitch, 0.80F), "positive/negative queued deltas preserve sequential clamp: 1.3 -> 1.4 -> 0.8");
    check(api.captured() && !Pick().valid && sgbMouseDown == CLICK_NONE, "clamp sequence keeps capture, invalidates old IDs and releases click");

    // Fresh rendered sky is different from stale picking: both have no target,
    // but only the former proves that no fallback hero-tile command is emitted.
    api.release();
    DrainEmittedWalks(check);
    ResumeAt(api, check, origin, pair.yaw);
    Pose(pair.yaw, -1.30F);
    DrawCpu(frame, check);
    check(!Pick().valid, "actual upward CPU frame has no world target at the captured center");
    Click(api, true);
    Click(api, false);
    check(TakeWalksWithoutParse(check).empty(), "sky down/up emits no immediate walk to the native fallback tile");
    DrawCpu(frame, check);
    FlushFirstPersonClicksForDiagnostics();
    check(TakeWalksWithoutParse(check).empty() && api.captured() && sgbMouseDown == CLICK_NONE,
        "fresh sky flush consumes click/up without gameplay command or capture loss");

    // Native inventory gate cancels both pending click and its later look.
    ResumeAt(api, check, origin, pair.yaw);
    Pose(pair.yaw, pair.pitch);
    Click(api, true);
    api.dispatch(Motion(20, 0), 0);
    Click(api, false);
    const auto beforeUi = GetTownViewCameraState();
    invflag = true;
    api.sync();
    check(!api.captured(), "opening the actual inventory suspends captured input and cancels deferred queue");
    check(TakeWalksWithoutParse(check).empty(), "canceled pending click emits no gameplay command while inventory is open");
    invflag = false;
    api.sync();
    check(!api.captured(), "closing inventory does not auto-resume a queued click");
    api.deliberateResume();
    DrawCpu(frame, check);
    FlushFirstPersonClicksForDiagnostics();
    check(TakeWalksWithoutParse(check).empty() && SamePose(GetTownViewCameraState(), beforeUi),
        "deliberate resume cannot replay a canceled click or its deferred look");
    check(api.captured() && sgbMouseDown == CLICK_NONE, "UI-canceled queue leaves no stuck click after deliberate resume");
    check(GetLCGEngineState() == rngBefore, "actual CPU draw/selection/ordered queue tests preserve the simulation RNG");
    api.release();
    DrainEmittedWalks(check);
    GetOptions().Graphics.townViewGpuRendering.SetValue(savedGpu);
    GetOptions().Graphics.townViewAntialiasing.SetValue(savedAa);
    GetOptions().Graphics.townViewHorizon.SetValue(savedHorizon);
    logical_palette = savedLogical;
    system_palette = savedSystem;
    GenerateBlendedLookupTable(logical_palette.data());
    // No further world draw after restoring preferences. The runner's process
    // discards these fixture resources; exception also terminates that process.
}

inline void RunPendingNativeWalkGateChecks(const ProductionAccess &api, const CheckFn &check,
    const std::function<void(bool)> &setMockFocus)
{
    check(static_cast<bool>(setMockFocus), "root supplies only the existing simulated physical focus setter");
    const Point origin = FindClearPatch(check);
    const Point target = origin + Direction::NorthWest;
    for (unsigned gate = 0; gate < 4; ++gate) {
        ResumeAt(api, check, origin, 0);
        Arrow(api, SDLK_UP, true);
        plrctrls_after_game_logic();
        const auto pending = TakeWalksWithoutParse(check);
        check(pending.size() == 1 && Point {pending[0].x, pending[0].y} == target,
            "adapter emitted a real walk that has not reached native OnWalk yet");
        check(MyPlayer->walkpath[0] == WALK_NONE && MyPlayer->position.tile == origin,
            "precondition: actual emitted walk is still unparsed, with no native path");
        if (gate == 0) PauseMode = 2;
        if (gate == 1) {
            // IsChatAvailable requires this flag. The provider remains local
            // loopback; this tests native chat/UI eligibility, not multiplayer.
            gbIsMultiplayer = true;
            TypeChatMessage();
            check(ChatFlag && ChatInputState.has_value() && IsChatActive(), "native TypeChatMessage actually opens text entry for the gate case");
        }
        if (gate == 2) setMockFocus(false);
        if (gate == 3) SetTownViewCameraMode(TownCameraMode::ThirdPerson);
        api.sync();
        check(!api.captured() && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
            "pause/chat/focus/other-camera gate suspends movement before ParseCmd, case=" + std::to_string(gate));
        const auto stops = TakeWalksWithoutParse(check);
        check(stops.size() == 1 && Point {stops[0].x, stops[0].y} == origin,
            "suspension serializes an owned native stop even while earlier walkpath is empty");
        for (const auto &walk : pending) ParseObservedWalk(walk, check);
        check(MyPlayer->walkpath[0] != WALK_NONE, "production parser receives the real earlier walk first");
        for (const auto &stop : stops) ParseObservedWalk(stop, check);
        check(MyPlayer->walkpath[0] == WALK_NONE && MyPlayer->position.tile == origin
            && MyPlayer->position.future == origin, "following serialized stop cancels the in-flight request through native OnWalk");
        Arrow(api, SDLK_UP, false); // Delivered while native pause/chat/UI disables capture.
        check(TakeWalksWithoutParse(check).empty(), "suspended KEYUP produces no native extra movement/binding");
        if (gate == 0) PauseMode = 0;
        if (gate == 1) { ResetChat(); gbIsMultiplayer = false; }
        if (gate == 2) setMockFocus(true);
        if (gate == 3) SetTownViewCameraMode(TownCameraMode::FirstPerson);
        api.sync();
        if (gate != 3)
            check(!api.captured(), "restoring pause/chat/focus alone does not silently reacquire capture");
        // Frozen policy allows automatic acquisition on a NEW mode entry.
        // Do not turn that into a click if it already captured successfully.
        if (!api.captured()) api.deliberateResume();
        api.sync();
        check(api.captured(), "resume or new-mode acquisition restores capture after pending-command suspension");
        plrctrls_after_game_logic();
        check(TakeWalksWithoutParse(check).empty(), "resume cannot resurrect the previous arrow or native command");
        api.release();
        DrainEmittedWalks(check);
    }
    // Adapter-specific API stays neutral/absolute outside first person. This
    // does not claim renderer or all original gameplay controls are covered.
    for (const auto mode : { TownCameraMode::Isometric, TownCameraMode::FreeOrbit, TownCameraMode::ThirdPerson }) {
        SetTownViewCameraMode(mode);
        api.sync();
        const auto before = GetTownViewCameraState();
        api.dispatch(Motion(12, 7), 0);
        api.sync();
        check(!api.captured() && GetTownFirstPersonMoveDirection() == Direction::NoDirection,
            "first-person adapter remains released/neutral in every other camera mode");
        check(GetTownFirstPersonPointer({17,23}) == Point {17,23} && SamePose(before, GetTownViewCameraState()),
            "uncaptured mouse motion preserves other-mode pose and its absolute native pointer");
    }
    SetTownViewCameraMode(TownCameraMode::FirstPerson);
    api.release();
    DrainEmittedWalks(check);
}
} // namespace first_person_root_supplement
