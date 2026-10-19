#pragma once

#include <array>
#include <span>
#include <string>

#include "engine/render/player_actor_first_person.hpp"
#include "engine/render/player_actor_lifecycle.hpp"
#include "engine/render/player_actor_resources.hpp"

namespace devilution {
struct Player;

// Explicit human-requested native pilot. This does not manufacture historical
// review receipts or promote the diagnostic loader to full gameplay admission.
enum class PlayerActorNativePolicy { NativeHumanWarriorLightTownCathedral1 };
struct PlayerActorNativeContext {
    WarriorWorldIdentity world;
    std::uint32_t nativeIndex = 0;
    ActorVec3 footpoint;
    std::uint8_t retainedInterpolation128 = 0;
    bool paused = false, localPlayer = false;
    bool worldBodyHidden = false, firstPerson = false;
    // Render-only geometry request. Presentation still obeys worldBodyHidden.
    bool worldShadowBodyRequested = false;
};
struct PlayerActorNativePilotFrame {
    PlayerActorNativePolicy policy = PlayerActorNativePolicy::NativeHumanWarriorLightTownCathedral1;
    PlayerActorLifecycleSnapshot lifecycle;
    PlayerActorCapture capture;
    ActorPlayerFit fit;
    ActorPlayerFrame geometry;
    // Keeps the exact immutable model alive across an external inventory
    // sprite resolver, resource replacement and guarded renderer replay.
    PlayerActorResources resourceOwner;
    struct InventoryPixels {
        int nativeItemId = -1, inventoryCursor = -1, width = 0, height = 0;
        std::vector<uint8_t> pixels, opacity;
    };
    // Renderer copies decoded inventory coverage/colors before committing a
    // frame. Its saved sprite attachments no longer borrow native CLX memory.
    std::vector<InventoryPixels> inventoryPixels;
    std::array<PlayerActorEquippedHand, 2> equipment;
    PlayerActorFirstPersonFrame hands;
    bool firstPerson = false, worldBodyHidden = false;
    std::uint64_t resourceGeneration = 0;
    int nativeTileX = 0, nativeTileY = 0, nativeHitPoints = 0;
};

// No I/O or renderer, and no native simulation writes. Load snapshots/owns the
// three pinned packets atomically. Prepare borrows the live native lifecycle and
// samples its rendered phase exactly once for body, hands and all attachments.
class PlayerActorNativeRuntime {
public:
    bool Load(const std::array<std::span<const std::byte>, 3> &packets, std::string &error);
    bool Prepare(const Player &player, const PlayerActorLifecycle &lifecycle,
        const PlayerActorNativeContext &context, const PlayerActorInventorySpriteResolver &sprites,
        PlayerActorNativePilotFrame &output, std::string &error) const;
    bool IsCurrent(const Player &player, const PlayerActorLifecycle &lifecycle,
        const PlayerActorNativePilotFrame &frame) const;
    bool loaded() const noexcept { return !resources_.empty(); }
    const PlayerActorResources &Resources() const noexcept { return resources_; }
    void Reset() noexcept { resources_ = {}; }
private:
    PlayerActorResources resources_;
};

struct TownViewWarriorPilotState {
    bool attempted = false, loaded = false;
    std::size_t preparedPlayers = 0, worldBodyDraws = 0, firstPersonDraws = 0;
    std::size_t bodyTriangles = 0, handTriangles = 0, rigidTriangles = 0, spriteQuads = 0;
    std::string refusal;
};
TownViewWarriorPilotState GetTownViewWarriorPilotState();
void SetTownViewWarriorViewModelEnabledForDiagnostics(bool enabled);
// Borrowed until the next world draw/reset; no pose resampling happens here.
const PlayerActorNativePilotFrame *GetTownViewWarriorPilotFrameForDiagnostics(std::size_t slot);
} // namespace devilution
