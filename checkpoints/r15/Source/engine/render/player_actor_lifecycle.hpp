#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/render/warrior_actor_admission.hpp"

namespace devilution {

struct Player;

enum class PlayerActorLifecycleError : std::uint8_t {
	None, InvalidWorld, LevelUnavailable, InvalidSlot, InactiveSlot,
	PlayerMismatch, PlayerUnavailable, EquipmentUpdateOpen, DeathUpdateOpen,
	EquipmentUpdateUnbalanced, EquipmentObservationMissed, DeathObservationMissed,
	DeathOutcomeInvalid, ResurrectionUpdateOpen, ResurrectionSourceInvalid,
	ResurrectionObservationMissed, ResurrectionOutcomeInvalid, CounterExhausted,
};
const char *PlayerActorLifecycleErrorText(PlayerActorLifecycleError error) noexcept;

// Exact semantic item identity; empty slots deliberately ignore the stale data
// left behind by Item::clear(). Equality, not the diagnostic hash, gates reads.
struct PlayerActorEquippedItemIdentity {
	bool occupied = false, identified = false, statFlag = false;
	std::int32_t itemId = -1, uniqueId = 0, itemType = 0, itemClass = 0, location = 0, quality = 0;
	std::uint32_t seed = 0, buffer = 0;
	std::uint16_t createInfo = 0;
	std::uint8_t cursor = 0, minMagic = 0;
	std::int8_t minStrength = 0, minDexterity = 0;
	bool operator==(const PlayerActorEquippedItemIdentity &) const = default;
};

struct PlayerActorEquipmentIdentity {
	std::array<PlayerActorEquippedItemIdentity, 7> slots {};
	std::uint8_t nativeGraphic = 0, nativeClass = 0;
	std::int32_t strength = 0, magic = 0, dexterity = 0;
	bool operator==(const PlayerActorEquipmentIdentity &) const = default;
};

struct PlayerActorLifecycleSnapshot {
	WarriorWorldIdentity world;
	std::uint64_t levelEpoch = 0, playerActivation = 0, equipmentRevision = 0;
	std::uint32_t nativeIndex = 0;
	std::uint64_t equipmentFingerprint = 0; // Evidence only; never authenticates a loaded asset.
	PlayerActorEquipmentIdentity equipment;
	WarriorEquipmentDisposition equipmentDisposition = WarriorEquipmentDisposition::Unknown;
	WarriorWeapon currentNativeEquipmentWeapon = WarriorWeapon::Unknown;
	WarriorArmor currentNativeEquipmentArmor = WarriorArmor::Unknown;
	std::int32_t currentNativeSwordItemId = -1, currentNativeShieldItemId = -1;
	WarriorDeathEquipmentWitness deathEquipment;
};

// Native owning thread only. No renderer resource generation, frame counter,
// wall clock, art approval, Player writes, simulation events or save fields.
// Native owners must bracket equipment/death updates and announce slot reuse.
class PlayerActorLifecycle {
public:
	bool BeginLevel(PlayerActorLifecycleError *error = nullptr) noexcept;
	bool CompleteLevel(const WarriorWorldIdentity &world, PlayerActorLifecycleError *error = nullptr) noexcept;
	void InvalidateLevel() noexcept;
	bool Activate(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error = nullptr) noexcept;
	void Deactivate(std::uint32_t slot) noexcept;
	bool BeginEquipmentUpdate(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error = nullptr) noexcept;
	bool EndEquipmentUpdate(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error = nullptr) noexcept;
	bool BeginDeath(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error = nullptr) noexcept;
	bool EndDeath(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error = nullptr) noexcept;
	// Explicit native ApplyResurrect transaction. A loaded corpse needs no
	// invented death origin; successful native revival publishes a fresh alive
	// activation only after HP/mode/level and final equipment have been observed.
	bool BeginResurrection(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error = nullptr) noexcept;
	bool EndResurrection(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error = nullptr) noexcept;
	// Refusal never changes output; an old preserved payload is not current.
	bool ReadSnapshot(const Player &player, std::uint32_t slot, const WarriorWorldIdentity &liveWorld,
	    PlayerActorLifecycleSnapshot &output, PlayerActorLifecycleError *error = nullptr) const noexcept;
	bool IsCurrent(const Player &player, const PlayerActorLifecycleSnapshot &snapshot) const noexcept;
	std::uint64_t epoch() const noexcept { return ready_ ? epoch_ : 0; }

private:
	struct SlotState {
		const Player *player = nullptr;
		bool active = false, observed = false, deadBaseline = false, deathOpen = false, resurrectionOpen = false;
		std::uint32_t equipmentDepth = 0;
		std::uint64_t epoch = 0, activation = 0, equipmentRevision = 0;
		PlayerActorEquipmentIdentity equipment, deathOrigin;
		std::uint64_t deathEquipmentRevision = 0, pendingDeathTransition = 0;
		std::uint64_t resurrectionEpoch = 0, resurrectionActivation = 0;
		std::uint8_t resurrectionNativeClass = 0;
		WarriorDeathEquipmentWitness death;
		WarriorEquipmentDisposition disposition = WarriorEquipmentDisposition::CurrentEquipped;
	};
	bool ValidateSlot(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error) const noexcept;
	bool CommitEquipment(const Player &player, SlotState &state, PlayerActorLifecycleError *error) noexcept;
	std::array<SlotState, 4> slots_ {};
	std::uint64_t epochCounter_ = 0, activationCounter_ = 0, equipmentCounter_ = 0, deathCounter_ = 0;
	std::uint64_t epoch_ = 0;
	bool loading_ = false, ready_ = false;
	WarriorWorldIdentity world_;
};

// This singleton survives visual resource resets. InvalidateLevel belongs to
// native LoadGameLevel/FreeGameMem; presentation reset must not call it.
PlayerActorLifecycle &NativePlayerActorLifecycle() noexcept;

} // namespace devilution
