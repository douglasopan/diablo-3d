#include "engine/render/player_actor_lifecycle.hpp"

#include <limits>
#include "player.h"

namespace devilution {
namespace {
static_assert(MAX_PLRS == 4 && NUM_INVLOC == 7);
static_assert(static_cast<unsigned>(PlayerWeaponGraphic::SwordShield) == static_cast<unsigned>(WarriorWeapon::SwordShield));
static_assert(IDI_WARRIOR == 1 && IDI_WARRSHLD == 2);

bool Fail(PlayerActorLifecycleError *error, PlayerActorLifecycleError value) noexcept
{
	if (error != nullptr) *error = value;
	return false;
}
bool Success(PlayerActorLifecycleError *error) noexcept
{
	if (error != nullptr) *error = PlayerActorLifecycleError::None;
	return true;
}
bool Advance(std::uint64_t &counter) noexcept
{
	if (counter == std::numeric_limits<std::uint64_t>::max()) return false;
	++counter;
	return true;
}
bool ValidWorld(const WarriorWorldIdentity &world) noexcept
{
	return (world.kind == WarriorWorldKind::Town && world.levelNumber == 0 && !world.setLevel)
	    || (world.kind == WarriorWorldKind::Cathedral && world.levelNumber > 0 && world.levelNumber <= 255)
	    || (world.kind == WarriorWorldKind::Other && world.levelNumber <= 255);
}
PlayerActorEquipmentIdentity ReadEquipment(const Player &player) noexcept
{
	PlayerActorEquipmentIdentity result;
	result.nativeGraphic = player._pgfxnum;
	result.nativeClass = static_cast<std::uint8_t>(player._pClass);
	result.strength = player._pStrength; result.magic = player._pMagic; result.dexterity = player._pDexterity;
	for (std::size_t i = 0; i < result.slots.size(); ++i) {
		const Item &item = player.InvBody[i];
		if (item.isEmpty()) continue;
		auto &value = result.slots[i];
		value.occupied = true; value.identified = item._iIdentified; value.statFlag = item._iStatFlag;
		value.itemId = item.IDidx; value.uniqueId = item._iUid; value.itemType = static_cast<std::int32_t>(item._itype);
		value.itemClass = item._iClass; value.location = item._iLoc; value.quality = item._iMagical;
		value.seed = item._iSeed; value.buffer = item.dwBuff; value.createInfo = item._iCreateInfo;
		value.cursor = item._iCurs; value.minMagic = item._iMinMag;
		value.minStrength = item._iMinStr; value.minDexterity = item._iMinDex;
	}
	return result;
}
bool SameEquippedState(PlayerActorEquipmentIdentity a, PlayerActorEquipmentIdentity b) noexcept
{
	a.nativeGraphic = b.nativeGraphic = 0;
	return a == b;
}
WarriorWeapon Weapon(std::uint8_t graphic) noexcept
{
	const unsigned value = graphic & 15;
	return value <= static_cast<unsigned>(WarriorWeapon::Staff) ? static_cast<WarriorWeapon>(value) : WarriorWeapon::Unknown;
}
WarriorArmor Armor(std::uint8_t graphic) noexcept
{
	const unsigned value = graphic >> 4;
	return value <= static_cast<unsigned>(WarriorArmor::Heavy) ? static_cast<WarriorArmor>(value) : WarriorArmor::Unknown;
}
std::int32_t ItemId(const PlayerActorEquipmentIdentity &equipment, ItemType type) noexcept
{
	std::int32_t result = -1;
	for (const auto slot : { INVLOC_HAND_LEFT, INVLOC_HAND_RIGHT }) {
		const auto &item = equipment.slots[slot];
		if (!item.occupied || item.itemType != static_cast<std::int32_t>(type) || !item.statFlag) continue;
		if (result != -1) return -1; // Ambiguous dual equipment is outside this pilot.
		result = item.itemId;
	}
	return result;
}
std::uint64_t Fingerprint(const PlayerActorEquipmentIdentity &equipment) noexcept
{
	std::uint64_t hash = 1469598103934665603ULL;
	const auto add = [&hash](std::uint64_t value) {
		for (unsigned i = 0; i < 8; ++i) { hash ^= static_cast<std::uint8_t>(value); hash *= 1099511628211ULL; value >>= 8; }
	};
	add(equipment.nativeGraphic); add(equipment.nativeClass);
	add(static_cast<std::uint32_t>(equipment.strength)); add(static_cast<std::uint32_t>(equipment.magic)); add(static_cast<std::uint32_t>(equipment.dexterity));
	for (const auto &item : equipment.slots) {
		add(item.occupied); add(item.identified); add(item.statFlag); add(static_cast<std::uint32_t>(item.itemId));
		add(static_cast<std::uint32_t>(item.uniqueId)); add(static_cast<std::uint32_t>(item.itemType)); add(static_cast<std::uint32_t>(item.itemClass));
		add(static_cast<std::uint32_t>(item.location)); add(static_cast<std::uint32_t>(item.quality)); add(item.seed); add(item.buffer); add(item.createInfo);
		add(item.cursor); add(item.minMagic); add(static_cast<std::uint8_t>(item.minStrength)); add(static_cast<std::uint8_t>(item.minDexterity));
	}
	return hash;
}
bool EmptyEquipment(const PlayerActorEquipmentIdentity &equipment) noexcept
{
	for (const auto &item : equipment.slots) if (item.occupied) return false;
	return true;
}
bool SameDeath(const WarriorDeathEquipmentWitness &a, const WarriorDeathEquipmentWitness &b) noexcept
{
	return a.levelEpoch == b.levelEpoch && a.playerActivation == b.playerActivation
	    && a.nativeDeathTransition == b.nativeDeathTransition && a.equipmentRevisionBefore == b.equipmentRevisionBefore
	    && a.equipmentRevisionAfter == b.equipmentRevisionAfter && a.originWeapon == b.originWeapon && a.originArmor == b.originArmor
	    && a.originSwordItemId == b.originSwordItemId && a.originShieldItemId == b.originShieldItemId
	    && a.removedSwordItemId == b.removedSwordItemId && a.removedShieldItemId == b.removedShieldItemId;
}
} // namespace

const char *PlayerActorLifecycleErrorText(PlayerActorLifecycleError error) noexcept
{
	switch (error) {
	case PlayerActorLifecycleError::None: return "current";
	case PlayerActorLifecycleError::InvalidWorld: return "native world identity mismatch";
	case PlayerActorLifecycleError::LevelUnavailable: return "native level is unavailable or not complete";
	case PlayerActorLifecycleError::InvalidSlot: return "native player slot outside bounds";
	case PlayerActorLifecycleError::InactiveSlot: return "native player slot was not activated for this level";
	case PlayerActorLifecycleError::PlayerMismatch: return "native player belongs to another slot activation";
	case PlayerActorLifecycleError::PlayerUnavailable: return "native player is inactive, changing level or on another level";
	case PlayerActorLifecycleError::EquipmentUpdateOpen: return "native equipment update is incomplete";
	case PlayerActorLifecycleError::DeathUpdateOpen: return "native death update is incomplete";
	case PlayerActorLifecycleError::EquipmentUpdateUnbalanced: return "native equipment event nesting is unbalanced";
	case PlayerActorLifecycleError::EquipmentObservationMissed: return "native equipment changed without a completed observation";
	case PlayerActorLifecycleError::DeathObservationMissed: return "native death has no current before/after witness";
	case PlayerActorLifecycleError::DeathOutcomeInvalid: return "native death did not prove equipment retention or removal";
	case PlayerActorLifecycleError::ResurrectionUpdateOpen: return "native resurrection update is incomplete";
	case PlayerActorLifecycleError::ResurrectionSourceInvalid: return "native resurrection requires a current dead source";
	case PlayerActorLifecycleError::ResurrectionObservationMissed: return "native resurrection has no matching before observation";
	case PlayerActorLifecycleError::ResurrectionOutcomeInvalid: return "native resurrection did not complete as a live standing player";
	case PlayerActorLifecycleError::CounterExhausted: return "native lifecycle counter exhausted";
	}
	return "invalid lifecycle error";
}

bool PlayerActorLifecycle::BeginLevel(PlayerActorLifecycleError *error) noexcept
{
	InvalidateLevel();
	if (!Advance(epochCounter_)) return Fail(error, PlayerActorLifecycleError::CounterExhausted);
	epoch_ = epochCounter_; loading_ = true;
	return Success(error);
}
bool PlayerActorLifecycle::CompleteLevel(const WarriorWorldIdentity &world, PlayerActorLifecycleError *error) noexcept
{
	if (!loading_ || epoch_ == 0) return Fail(error, PlayerActorLifecycleError::LevelUnavailable);
	if (!ValidWorld(world)) return Fail(error, PlayerActorLifecycleError::InvalidWorld);
	world_ = world; loading_ = false; ready_ = true;
	return Success(error);
}
void PlayerActorLifecycle::InvalidateLevel() noexcept
{
	ready_ = loading_ = false; epoch_ = 0; world_ = {};
	for (auto &slot : slots_) slot = {};
}
bool PlayerActorLifecycle::Activate(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error) noexcept
{
	if (slot >= slots_.size()) return Fail(error, PlayerActorLifecycleError::InvalidSlot);
	if (epoch_ == 0 || (!ready_ && !loading_)) return Fail(error, PlayerActorLifecycleError::LevelUnavailable);
	if (!player.plractive) return Fail(error, PlayerActorLifecycleError::PlayerUnavailable);
	if (activationCounter_ == std::numeric_limits<std::uint64_t>::max() || equipmentCounter_ == std::numeric_limits<std::uint64_t>::max()) {
		Deactivate(slot); return Fail(error, PlayerActorLifecycleError::CounterExhausted);
	}
	SlotState state;
	state.player = &player; state.active = state.observed = true;
	state.epoch = epoch_; state.activation = ++activationCounter_; state.equipmentRevision = ++equipmentCounter_;
	state.equipment = ReadEquipment(player);
	state.deadBaseline = player._pmode == PM_DEATH;
	slots_[slot] = state;
	return Success(error);
}
void PlayerActorLifecycle::Deactivate(std::uint32_t slot) noexcept
{
	if (slot < slots_.size()) slots_[slot] = {};
}
bool PlayerActorLifecycle::ValidateSlot(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error) const noexcept
{
	if (slot >= slots_.size()) return Fail(error, PlayerActorLifecycleError::InvalidSlot);
	const auto &state = slots_[slot];
	if (epoch_ == 0 || state.epoch != epoch_ || !state.active) return Fail(error, PlayerActorLifecycleError::InactiveSlot);
	if (state.player != &player) return Fail(error, PlayerActorLifecycleError::PlayerMismatch);
	if (!player.plractive) return Fail(error, PlayerActorLifecycleError::PlayerUnavailable);
	return true;
}
bool PlayerActorLifecycle::BeginEquipmentUpdate(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error) noexcept
{
	if (!ValidateSlot(player, slot, error)) return false;
	auto &state = slots_[slot];
	if (state.equipmentDepth == std::numeric_limits<std::uint32_t>::max()) {
		state.observed = false; return Fail(error, PlayerActorLifecycleError::CounterExhausted);
	}
	++state.equipmentDepth;
	return Success(error);
}
bool PlayerActorLifecycle::CommitEquipment(const Player &player, SlotState &state, PlayerActorLifecycleError *error) noexcept
{
	const auto equipment = ReadEquipment(player);
	const bool changed = !state.observed || !SameEquippedState(state.equipment, equipment)
	    || (state.equipment.nativeGraphic != equipment.nativeGraphic && !state.deathOpen && player._pmode != PM_DEATH);
	if (changed && !Advance(equipmentCounter_)) {
		state.observed = false; return Fail(error, PlayerActorLifecycleError::CounterExhausted);
	}
	if (changed) { state.equipmentRevision = equipmentCounter_; state.death = {}; state.disposition = WarriorEquipmentDisposition::CurrentEquipped; }
	state.equipment = equipment; state.observed = true;
	return Success(error);
}
bool PlayerActorLifecycle::EndEquipmentUpdate(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error) noexcept
{
	if (!ValidateSlot(player, slot, error)) return false;
	auto &state = slots_[slot];
	if (state.equipmentDepth == 0) { state.observed = false; return Fail(error, PlayerActorLifecycleError::EquipmentUpdateUnbalanced); }
	if (--state.equipmentDepth != 0) return Success(error);
	return CommitEquipment(player, state, error);
}
bool PlayerActorLifecycle::BeginDeath(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error) noexcept
{
	if (!ready_) return Fail(error, PlayerActorLifecycleError::LevelUnavailable);
	if (!ValidateSlot(player, slot, error)) return false;
	if (player._pLvlChanging || player.plrlevel != world_.levelNumber || player.plrIsOnSetLevel != world_.setLevel)
		return Fail(error, PlayerActorLifecycleError::PlayerUnavailable);
	auto &state = slots_[slot];
	if (state.equipmentDepth != 0) return Fail(error, PlayerActorLifecycleError::EquipmentUpdateOpen);
	if (state.deathOpen) return Fail(error, PlayerActorLifecycleError::DeathUpdateOpen);
	if (state.resurrectionOpen) return Fail(error, PlayerActorLifecycleError::ResurrectionUpdateOpen);
	if (player._pmode == PM_DEATH) return Fail(error, PlayerActorLifecycleError::DeathObservationMissed);
	if (!state.observed || state.equipment != ReadEquipment(player)) return Fail(error, PlayerActorLifecycleError::EquipmentObservationMissed);
	if (!Advance(deathCounter_)) return Fail(error, PlayerActorLifecycleError::CounterExhausted);
	state.deathOrigin = state.equipment; state.deathEquipmentRevision = state.equipmentRevision;
	state.pendingDeathTransition = deathCounter_; state.deathOpen = true; state.death = {};
	return Success(error);
}
bool PlayerActorLifecycle::EndDeath(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error) noexcept
{
	if (!ValidateSlot(player, slot, error)) return false;
	auto &state = slots_[slot];
	if (!state.deathOpen) return Fail(error, PlayerActorLifecycleError::DeathObservationMissed);
	if (!ready_ || player._pLvlChanging || player.plrlevel != world_.levelNumber || player.plrIsOnSetLevel != world_.setLevel) {
		state.deathOpen = false; state.observed = false; state.death = {};
		return Fail(error, PlayerActorLifecycleError::PlayerUnavailable);
	}
	if (state.equipmentDepth != 0) return Fail(error, PlayerActorLifecycleError::EquipmentUpdateOpen);
	const auto current = ReadEquipment(player);
	const auto &origin = state.deathOrigin;
	const bool retained = SameEquippedState(origin, current) && current.nativeGraphic == (origin.nativeGraphic & 0xF0);
	const bool dropped = EmptyEquipment(current) && current.nativeGraphic == 0 && !EmptyEquipment(origin);
	if (player._pmode != PM_DEATH || player._pHitPoints > 0 || current.nativeClass != origin.nativeClass
	    || (!retained && !dropped) || (retained && state.equipmentRevision != state.deathEquipmentRevision)) {
		state.deathOpen = false; state.observed = false; state.death = {};
		return Fail(error, PlayerActorLifecycleError::DeathOutcomeInvalid);
	}
	if (!CommitEquipment(player, state, error)) { state.deathOpen = false; return false; }
	WarriorDeathEquipmentWitness witness;
	witness.levelEpoch = epoch_; witness.playerActivation = state.activation;
	witness.nativeDeathTransition = state.pendingDeathTransition;
	witness.equipmentRevisionBefore = state.deathEquipmentRevision; witness.equipmentRevisionAfter = state.equipmentRevision;
	witness.originWeapon = Weapon(origin.nativeGraphic); witness.originArmor = Armor(origin.nativeGraphic);
	witness.originSwordItemId = ItemId(origin, ItemType::Sword); witness.originShieldItemId = ItemId(origin, ItemType::Shield);
	if (dropped) { witness.removedSwordItemId = witness.originSwordItemId; witness.removedShieldItemId = witness.originShieldItemId; }
	state.death = witness; state.deathOpen = false; state.deadBaseline = true;
	state.disposition = dropped ? WarriorEquipmentDisposition::DeathDropped : WarriorEquipmentDisposition::DeathRetained;
	return Success(error);
}
bool PlayerActorLifecycle::BeginResurrection(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error) noexcept
{
	if (!ready_) return Fail(error, PlayerActorLifecycleError::LevelUnavailable);
	if (!ValidateSlot(player, slot, error)) return false;
	if (player._pLvlChanging || player.plrlevel != world_.levelNumber || player.plrIsOnSetLevel != world_.setLevel)
		return Fail(error, PlayerActorLifecycleError::PlayerUnavailable);
	auto &state = slots_[slot];
	if (state.equipmentDepth != 0) return Fail(error, PlayerActorLifecycleError::EquipmentUpdateOpen);
	if (state.deathOpen) return Fail(error, PlayerActorLifecycleError::DeathUpdateOpen);
	if (state.resurrectionOpen) return Fail(error, PlayerActorLifecycleError::ResurrectionUpdateOpen);
	// Native life is fixed point: hasNoLife tests (_pHitPoints >> 6) <= 0.
	if (!state.deadBaseline || player._pmode != PM_DEATH || player._pHitPoints >= 64)
		return Fail(error, PlayerActorLifecycleError::ResurrectionSourceInvalid);
	if (!state.observed || state.equipment != ReadEquipment(player)) return Fail(error, PlayerActorLifecycleError::EquipmentObservationMissed);
	state.resurrectionEpoch = epoch_; state.resurrectionActivation = state.activation;
	state.resurrectionNativeClass = state.equipment.nativeClass; state.resurrectionOpen = true;
	return Success(error);
}
bool PlayerActorLifecycle::EndResurrection(const Player &player, std::uint32_t slot, PlayerActorLifecycleError *error) noexcept
{
	if (!ValidateSlot(player, slot, error)) return false;
	auto &state = slots_[slot];
	if (!state.resurrectionOpen || state.resurrectionEpoch != epoch_ || state.resurrectionActivation != state.activation)
		return Fail(error, PlayerActorLifecycleError::ResurrectionObservationMissed);
	if (state.equipmentDepth != 0) return Fail(error, PlayerActorLifecycleError::EquipmentUpdateOpen);
	if (!ready_ || player._pLvlChanging || player.plrlevel != world_.levelNumber || player.plrIsOnSetLevel != world_.setLevel) {
		state.resurrectionOpen = false; state.observed = false;
		return Fail(error, PlayerActorLifecycleError::PlayerUnavailable);
	}
	if (player._pmode != PM_STAND || player._pHitPoints < 64
	    || static_cast<std::uint8_t>(player._pClass) != state.resurrectionNativeClass) {
		state.resurrectionOpen = false; state.observed = false;
		return Fail(error, PlayerActorLifecycleError::ResurrectionOutcomeInvalid);
	}
	if (activationCounter_ == std::numeric_limits<std::uint64_t>::max()) {
		state.resurrectionOpen = false; state.observed = false;
		return Fail(error, PlayerActorLifecycleError::CounterExhausted);
	}
	if (!CommitEquipment(player, state, error)) { state.resurrectionOpen = false; return false; }
	// Native reactivation, even when a dropped corpse has unchanged empty
	// equipment. Render/resource resets never produce this generation.
	state.activation = ++activationCounter_;
	state.death = {}; state.deathOrigin = {}; state.pendingDeathTransition = state.deathEquipmentRevision = 0;
	state.disposition = WarriorEquipmentDisposition::CurrentEquipped;
	state.deadBaseline = false;
	state.resurrectionOpen = false; state.resurrectionEpoch = state.resurrectionActivation = 0;
	return Success(error);
}
bool PlayerActorLifecycle::ReadSnapshot(const Player &player, std::uint32_t slot, const WarriorWorldIdentity &liveWorld,
    PlayerActorLifecycleSnapshot &output, PlayerActorLifecycleError *error) const noexcept
{
	if (!ready_) return Fail(error, PlayerActorLifecycleError::LevelUnavailable);
	if (!(world_ == liveWorld)) return Fail(error, PlayerActorLifecycleError::InvalidWorld);
	if (!ValidateSlot(player, slot, error)) return false;
	if (player._pLvlChanging || player.plrlevel != world_.levelNumber || player.plrIsOnSetLevel != world_.setLevel)
		return Fail(error, PlayerActorLifecycleError::PlayerUnavailable);
	const auto &state = slots_[slot];
	if (state.equipmentDepth != 0) return Fail(error, PlayerActorLifecycleError::EquipmentUpdateOpen);
	if (state.deathOpen) return Fail(error, PlayerActorLifecycleError::DeathUpdateOpen);
	if (state.resurrectionOpen) return Fail(error, PlayerActorLifecycleError::ResurrectionUpdateOpen);
	if (!state.observed || state.equipment != ReadEquipment(player)) return Fail(error, PlayerActorLifecycleError::EquipmentObservationMissed);
	if (state.deadBaseline && player._pmode != PM_DEATH)
		return Fail(error, PlayerActorLifecycleError::ResurrectionObservationMissed);
	if ((player._pmode == PM_DEATH) != (state.death.nativeDeathTransition != 0))
		return Fail(error, PlayerActorLifecycleError::DeathObservationMissed);
	PlayerActorLifecycleSnapshot snapshot;
	snapshot.world = world_; snapshot.levelEpoch = epoch_; snapshot.playerActivation = state.activation;
	snapshot.equipmentRevision = state.equipmentRevision; snapshot.nativeIndex = slot;
	snapshot.equipment = state.equipment; snapshot.equipmentFingerprint = Fingerprint(state.equipment);
	snapshot.equipmentDisposition = state.disposition;
	snapshot.currentNativeEquipmentWeapon = state.disposition == WarriorEquipmentDisposition::DeathRetained
	    ? state.death.originWeapon : Weapon(state.equipment.nativeGraphic);
	snapshot.currentNativeEquipmentArmor = Armor(state.equipment.nativeGraphic);
	snapshot.currentNativeSwordItemId = ItemId(state.equipment, ItemType::Sword);
	snapshot.currentNativeShieldItemId = ItemId(state.equipment, ItemType::Shield);
	snapshot.deathEquipment = state.death;
	output = snapshot;
	return Success(error);
}
bool PlayerActorLifecycle::IsCurrent(const Player &player, const PlayerActorLifecycleSnapshot &snapshot) const noexcept
{
	PlayerActorLifecycleSnapshot current;
	return ReadSnapshot(player, snapshot.nativeIndex, snapshot.world, current)
	    && current.levelEpoch == snapshot.levelEpoch && current.playerActivation == snapshot.playerActivation
	    && current.equipmentRevision == snapshot.equipmentRevision && current.equipment == snapshot.equipment
	    && current.equipmentFingerprint == snapshot.equipmentFingerprint
	    && current.equipmentDisposition == snapshot.equipmentDisposition
	    && current.currentNativeEquipmentWeapon == snapshot.currentNativeEquipmentWeapon
	    && current.currentNativeEquipmentArmor == snapshot.currentNativeEquipmentArmor
	    && current.currentNativeSwordItemId == snapshot.currentNativeSwordItemId
	    && current.currentNativeShieldItemId == snapshot.currentNativeShieldItemId
	    && SameDeath(current.deathEquipment, snapshot.deathEquipment);
}
PlayerActorLifecycle &NativePlayerActorLifecycle() noexcept
{
	static PlayerActorLifecycle owner;
	return owner;
}
} // namespace devilution
