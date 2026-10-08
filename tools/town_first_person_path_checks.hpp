#pragma once
// Native movement/path integration checks; pure policy is covered separately.
// ADAPT only ProductionAccess to the incoming camera-owner's production entry points.
// Preconditions and limitations are documented in README.md beside this file.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "control/control.hpp"
#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "diablo.h"
#include "inv.h"
#include "engine/path.h"
#include "engine/render/town_view.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/tile_properties.hpp"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "storm/storm_net.hpp"
#include "utils/endian_swap.hpp"
#include "utils/sdl_compat.h"

namespace first_person_root_checks {
using namespace devilution;
using CheckFn = std::function<void(bool, const std::string &)>;

struct ProductionAccess {
    // ADAPT: these must call the production shared functions/wrappers. No mocks.
    std::function<void(const SDL_Event &, uint16_t)> dispatch;
    std::function<void()> sync;
    std::function<bool()> captured;
    // ADAPT: send the deliberately chosen real resume/capture gesture via dispatch.
    std::function<void()> deliberateResume;
    // ADAPT: production reset/release path; no editing policy private fields.
    std::function<void()> release;
};

inline void Arrow(const ProductionAccess &api, SDL_Keycode key, bool down)
{
    SDL_Event event {};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
#ifdef USE_SDL3
    event.key.key = key;
    event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    event.key.down = down;
#else
    event.key.keysym.sym = key;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
#endif
    event.key.repeat = false;
    api.dispatch(event, 0);
}

inline std::vector<TCmdLoc> DrainEmittedWalks(const CheckFn &check)
{
    // Observe actual NetSendCmdLoc -> NetSendHiPri -> SendPacket bytes.
    // Parse those same bytes through the production command parser. This avoids
    // fabricating a CMD_WALKXY and mistakenly claiming input integration passed.
    std::vector<TCmdLoc> result;
    uint8_t sender = 255;
    void *raw = nullptr;
    size_t bytes = 0;
    unsigned packets = 0;
    while (SNetReceiveMessage(&sender, &raw, &bytes)) {
        check(++packets <= 64, "bounded isolated loopback message drain");
        check(sender == MyPlayerId, "walk command came from the local input player");
        check(bytes >= sizeof(TPktHdr), "real walk packet contains its native header");
        if (bytes < sizeof(TPktHdr)) break;
        TPktHdr hdr {};
        std::memcpy(&hdr, raw, sizeof(hdr));
        check(Swap16LE(hdr.wLen) == bytes, "native packet records exact serialized size");
        const auto *body = reinterpret_cast<const std::byte *>(raw) + sizeof(TPktHdr);
        size_t remaining = bytes - sizeof(TPktHdr);
        while (remaining != 0) {
            const auto *cmd = reinterpret_cast<const TCmd *>(body);
            check(cmd->bCmd == CMD_WALKXY, "input case emits only the expected walk/stop command");
            if (cmd->bCmd != CMD_WALKXY || remaining < sizeof(TCmdLoc)) {
                check(false, "unexpected command or truncated walk payload; not parsed");
                return result;
            }
            TCmdLoc walk {};
            std::memcpy(&walk, body, sizeof(walk));
            result.push_back(walk);
            const size_t parsed = ParseCmd(sender, cmd, remaining);
            check(parsed == sizeof(TCmdLoc), "production ParseCmd consumed the actual emitted CMD_WALKXY");
            if (parsed == 0 || parsed > remaining) return result;
            body += parsed;
            remaining -= parsed;
        }
    }
    return result;
}

inline void FixturePlacePlayer(Point origin)
{
    // Test setup only, before input; all tested movement subsequently uses CMD_WALKXY.
    std::memset(dPlayer, 0, sizeof(dPlayer));
    Player &p = *MyPlayer;
    p.position.tile = p.position.future = p.position.last = p.position.old = p.position.temp = origin;
    p._pmode = PM_STAND;
    p.destAction = ACTION_NONE;
    ClrPlrPath(p);
    dPlayer[origin.x][origin.y] = 1;
    ViewPosition = origin;
    LastPlayerAction = PlayerActionType::None;
    sgbMouseDown = CLICK_NONE;
    ClearLastSentPlayerCmd();
}

inline Point FindClearPatch(const CheckFn &check)
{
    // A real SOL area, not a synthetic collision system.
    for (int y = 12; y < MAXDUNY - 12; ++y) {
        for (int x = 12; x < MAXDUNX - 12; ++x) {
            bool clear = true;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    clear &= dPiece[x + dx][y + dy] != 0 && PosOkPlayer(*MyPlayer, { x + dx, y + dy });
            if (clear) return { x, y };
        }
    }
    check(false, "original town has a clear five-by-five patch for deterministic walk checks");
    return MyPlayer->position.tile;
}

inline void ResumeAt(const ProductionAccess &api, const CheckFn &check, Point origin, float yaw)
{
    api.release();
    // Flush a release-owned stop before relocating the private fixture player.
    DrainEmittedWalks(check);
    for (auto key : { SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT }) Arrow(api, key, false);
    FixturePlacePlayer(origin);
    SetTownViewCameraMode(TownCameraMode::FirstPerson);
    SetTownViewCameraPoseForDiagnostics({ yaw, 0, 0, {} });
    api.sync();
    if (!api.captured()) api.deliberateResume();
    api.sync();
    check(api.captured(), "real production capture accepted the deliberate resume gesture");
    check(DrainEmittedWalks(check).empty(), "capture gesture causes no walk or unrelated action");
}

inline void RunProductionWalkChecks(const ProductionAccess &api, const CheckFn &check)
{
    check(IsLoopback && !gbIsMultiplayer, "fixture uses only the local loopback provider");
    check(IsTownViewActive(), "fixture started the real optional town view");
    check(ControlMode == ControlTypes::KeyboardAndMouse, "first-person arrows retain keyboard/mouse control mode");
    const Point origin = FindClearPatch(check);
    constexpr float Pi = 3.14159265358979323846F;
    const std::array<Point, 8> expectedDeltas {{ {-1,0}, {-1,-1}, {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1} }};
    for (size_t sector = 0; sector < expectedDeltas.size(); ++sector) {
        ResumeAt(api, check, origin, static_cast<float>(sector) * Pi / 4);
        Arrow(api, SDLK_UP, true);
        plrctrls_after_game_logic(); // SAME cadence and entry point used by GameLogic.
        const auto sent = DrainEmittedWalks(check);
        const Point target = origin + Displacement { expectedDeltas[sector].x, expectedDeltas[sector].y };
        check(sent.size() == 1 && Point { sent[0].x, sent[0].y } == target,
            "camera-relative forward produces one real native target, sector " + std::to_string(sector));
        check(MyPlayer->position.tile == origin && MyPlayer->position.future == origin,
            "input only queues native movement; it never teleports or edits actor coordinates");
        check(MyPlayer->walkpath[0] == GetPathDirection(origin, target) && MyPlayer->walkpath[1] == WALK_NONE,
            "native OnWalk/FindPath produces exactly the expected adjacent step");
        ClearLastSentPlayerCmd();
        Arrow(api, SDLK_UP, false);
        api.sync();
        plrctrls_after_game_logic();
        const auto stopped = DrainEmittedWalks(check);
        for (const auto &cmd : stopped)
            check(Point { cmd.x, cmd.y } == MyPlayer->position.future, "release emits only a native stop at future tile");
        check(MyPlayer->walkpath[0] == WALK_NONE, "arrow release cancels its queued walk through native stop");
    }

    // Cardinal strafe/back at the familiar reference yaw, still through production input.
    const std::array<std::pair<SDL_Keycode, Point>, 3> local {{
        { SDLK_DOWN, {1,1} }, { SDLK_LEFT, {-1,1} }, { SDLK_RIGHT, {1,-1} }
    }};
    for (const auto &[key, delta] : local) {
        ResumeAt(api, check, origin, Pi / 4);
        Arrow(api, key, true);
        plrctrls_after_game_logic();
        const auto sent = DrainEmittedWalks(check);
        check(sent.size() == 1 && Point { sent[0].x, sent[0].y } == origin + Displacement { delta.x, delta.y },
            "back/strafe emits the correct camera-relative native target");
        Arrow(api, key, false);
    }

    // Occupancy: reuse the native town towner grid contract, no art/GPU work.
    ResumeAt(api, check, origin, 0);
    const Point occupied = origin + Direction::NorthWest;
    const auto savedMonster = dMonster[occupied.x][occupied.y];
    dMonster[occupied.x][occupied.y] = 1;
    check(!PosOkPlayer(*MyPlayer, occupied), "real native collision rejects the occupied towner tile");
    Arrow(api, SDLK_UP, true);
    plrctrls_after_game_logic();
    const auto blocked = DrainEmittedWalks(check);
    for (const auto &cmd : blocked)
        check(Point {cmd.x, cmd.y} == occupied || Point {cmd.x, cmd.y} == origin, "blocked movement does not invent a detour target");
    // Native FindPath may retain a blocked destination as its final step.
    // Movement collision is authoritative when that step is attempted.
    ProcessPlayers();
    check(MyPlayer->position.tile == origin && MyPlayer->position.future == origin && !MyPlayer->isWalking(),
        "native ProcessPlayers refuses the occupied towner tile without moving");
    dMonster[occupied.x][occupied.y] = savedMonster;
    Arrow(api, SDLK_UP, false);
    api.release();
    DrainEmittedWalks(check);
}

inline void RunInventorySuspensionCheck(const ProductionAccess &api, const CheckFn &check)
{
    const Point origin = FindClearPatch(check);
    ResumeAt(api, check, origin, 0);
    Arrow(api, SDLK_UP, true);
    plrctrls_after_game_logic();
    DrainEmittedWalks(check);
    check(MyPlayer->walkpath[0] != WALK_NONE, "precondition: native walk is pending before inventory opens");
    invflag = true; // Real native panel state, not a mocked eligibility boolean.
    api.sync();
    check(!api.captured(), "opening native inventory releases first-person capture immediately");
    DrainEmittedWalks(check);
    check(MyPlayer->walkpath[0] == WALK_NONE, "inventory suspension cancels its owned pending native walk");
    Arrow(api, SDLK_UP, false); // Must reach release handling even while panel disables capture.
    plrctrls_after_game_logic();
    check(DrainEmittedWalks(check).empty(), "inventory does not issue first-person movement");
    invflag = false;
    api.sync();
    check(!api.captured(), "closing inventory alone does not silently reacquire capture");
    api.deliberateResume();
    api.sync();
    check(api.captured(), "deliberate gesture restores capture after closing inventory");
    plrctrls_after_game_logic();
    check(DrainEmittedWalks(check).empty(), "resumed capture does not resurrect the previously held arrow");
    api.release();
    DrainEmittedWalks(check);
}
} // namespace first_person_root_checks
