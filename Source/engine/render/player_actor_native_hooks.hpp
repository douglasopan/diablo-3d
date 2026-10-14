#pragma once

#include <optional>
#include "engine/render/player_actor_lifecycle.hpp"
#include "player.h"

namespace devilution {

// Avoid pointer subtraction/getId() on temporary Players used by native tools.
// Only the authoritative global array produces runtime activation identities.
inline std::optional<std::uint32_t> NativePlayerActorSlot(const Player &player) noexcept
{
    for (std::size_t i = 0; i < Players.size(); ++i)
        if (&Players[i] == &player) return static_cast<std::uint32_t>(i);
    return std::nullopt;
}

inline void DeactivateNativePlayerActor(const Player &player) noexcept
{
    if (const auto slot = NativePlayerActorSlot(player))
        NativePlayerActorLifecycle().Deactivate(*slot);
}

// Native construction/load/join owns this scope. It cannot run in the renderer.
// Deactivation precedes mutation; the final complete native value is sampled.
class NativePlayerActorActivationScope {
public:
    explicit NativePlayerActorActivationScope(const Player &player, bool activateOnExit = true) noexcept
        : player_(player), slot_(NativePlayerActorSlot(player)), activateOnExit_(activateOnExit)
    {
        if (slot_) NativePlayerActorLifecycle().Deactivate(*slot_);
    }
    ~NativePlayerActorActivationScope()
    {
        if (slot_) {
            if (activateOnExit_) NativePlayerActorLifecycle().Activate(player_, *slot_);
            else NativePlayerActorLifecycle().Deactivate(*slot_);
        }
    }
    // Fallible native unpackers authorize their final value only on success.
    void Complete() noexcept { activateOnExit_ = true; }
    NativePlayerActorActivationScope(const NativePlayerActorActivationScope &) = delete;
    NativePlayerActorActivationScope &operator=(const NativePlayerActorActivationScope &) = delete;
private:
    const Player &player_;
    std::optional<std::uint32_t> slot_;
    bool activateOnExit_;
};

// CalcPlrInv nests CalcPlrItemVals. The lifecycle owner commits only the outer
// end, after the native graphics/stat flags have reached their final values.
class NativePlayerActorEquipmentScope {
public:
    explicit NativePlayerActorEquipmentScope(const Player &player) noexcept
        : player_(player), slot_(NativePlayerActorSlot(player))
    {
        if (slot_) opened_ = NativePlayerActorLifecycle().BeginEquipmentUpdate(player_, *slot_);
    }
    ~NativePlayerActorEquipmentScope()
    {
        if (opened_) NativePlayerActorLifecycle().EndEquipmentUpdate(player_, *slot_);
    }
    NativePlayerActorEquipmentScope(const NativePlayerActorEquipmentScope &) = delete;
    NativePlayerActorEquipmentScope &operator=(const NativePlayerActorEquipmentScope &) = delete;
private:
    const Player &player_;
    std::optional<std::uint32_t> slot_;
    bool opened_ = false;
};

// Bracket the actual StartPlayerKill body after its already-dead early return.
// Neither the scope nor its owner generates drops, damage, RNG or save data.
class NativePlayerActorDeathScope {
public:
    explicit NativePlayerActorDeathScope(const Player &player) noexcept
        : player_(player), slot_(NativePlayerActorSlot(player))
    {
        if (slot_) opened_ = NativePlayerActorLifecycle().BeginDeath(player_, *slot_);
    }
    ~NativePlayerActorDeathScope()
    {
        if (opened_) NativePlayerActorLifecycle().EndDeath(player_, *slot_);
    }
    NativePlayerActorDeathScope(const NativePlayerActorDeathScope &) = delete;
    NativePlayerActorDeathScope &operator=(const NativePlayerActorDeathScope &) = delete;
private:
    const Player &player_;
    std::optional<std::uint32_t> slot_;
    bool opened_ = false;
};

// ApplyResurrect owns the transition out of PM_DEATH even when the native
// inventory/graphic stay identical (for example after all equipment dropped).
class NativePlayerActorResurrectionScope {
public:
    explicit NativePlayerActorResurrectionScope(const Player &player) noexcept
        : player_(player), slot_(NativePlayerActorSlot(player))
    {
        if (slot_) opened_ = NativePlayerActorLifecycle().BeginResurrection(player_, *slot_);
    }
    ~NativePlayerActorResurrectionScope()
    {
        if (opened_) NativePlayerActorLifecycle().EndResurrection(player_, *slot_);
    }
    NativePlayerActorResurrectionScope(const NativePlayerActorResurrectionScope &) = delete;
    NativePlayerActorResurrectionScope &operator=(const NativePlayerActorResurrectionScope &) = delete;
private:
    const Player &player_;
    std::optional<std::uint32_t> slot_;
    bool opened_ = false;
};

} // namespace devilution
