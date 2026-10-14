#pragma once

#include <array>
#include <cmath>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "engine/render/actor_player_binding.hpp"
#include "engine/render/player_actor_capture.hpp"

namespace devilution {

// Values mirror PlayerWeaponGraphic without pulling Player or its globals into
// this pure boundary. A production adapter must assert that native mapping.
enum class WarriorWeapon : std::uint8_t {
	Unarmed, UnarmedShield, Sword, SwordShield, Bow, Axe, Mace, MaceShield, Staff,
	Unknown = 255,
};
enum class WarriorArmor : std::uint8_t { Light, Medium, Heavy, Unknown = 255 };
enum class WarriorEnvironment : std::uint8_t { Town, Dungeon, Unknown = 255 };
enum class WarriorReview : std::uint8_t { Unreviewed, DiagnosticValidated, PilotApproved, GameplayApproved };
enum class WarriorAdmissionScope : std::uint8_t { Gameplay, DiagnosticAction, ClosedSwordShieldPilot };
enum class WarriorPresentationSource : std::uint8_t {
	Unspecified, AuthoritativeEquipment, TownCastRemap, DeathRetainedEquipment,
};
enum class WarriorAttachmentKind : std::uint8_t { Sword, Shield, Bow, Axe, Mace, Staff };
enum class WarriorSocketHand : std::uint8_t { Unknown, Right, Left };
enum class WarriorWorldKind : std::uint8_t { Unknown, Town, Cathedral, Other };
enum class WarriorEquipmentDisposition : std::uint8_t { Unknown, CurrentEquipped, DeathRetained, DeathDropped };

struct WarriorWorldIdentity {
	WarriorWorldKind kind = WarriorWorldKind::Unknown;
	std::uint32_t levelNumber = 0;
	bool setLevel = false;
	bool expansion = false;
	bool shareware = false;
	bool operator==(const WarriorWorldIdentity &) const = default;
};

struct WarriorItemIdentity {
	// Mirrors _item_indexes only through the trusted native adapter. For the
	// closed starter pilot IDI_WARRIOR=1 (Short Sword), IDI_WARRSHLD=2 (Buckler).
	std::int32_t nativeItemId = -1;
	std::string nativeItemSymbol;
	std::string assetId;
	std::string variant;
	std::string revision;
	std::string sourceSha256;
	std::string packageSha256;
	bool operator==(const WarriorItemIdentity &) const = default;
};

struct WarriorPilotAuthorization {
	std::string domainId;
	std::string manifestSha256;
	std::string authorizationSha256;
	bool operator==(const WarriorPilotAuthorization &) const = default;
};

// Native owner supplies this observation; an empty current inventory or nibble
// zero is not evidence that these items were present or dropped in this death.
struct WarriorDeathEquipmentWitness {
	std::uint64_t levelEpoch = 0;
	std::uint64_t playerActivation = 0;
	std::uint64_t nativeDeathTransition = 0;
	std::uint64_t equipmentRevisionBefore = 0;
	std::uint64_t equipmentRevisionAfter = 0;
	WarriorWeapon originWeapon = WarriorWeapon::Unknown;
	WarriorArmor originArmor = WarriorArmor::Unknown;
	std::int32_t originSwordItemId = -1;
	std::int32_t originShieldItemId = -1;
	// IDs of the native items actually observed leaving equipment, not IDs
	// inferred from the approved visual attachments or a selected model.
	std::int32_t removedSwordItemId = -1;
	std::int32_t removedShieldItemId = -1;
};

struct WarriorEvidence {
	WarriorReview review = WarriorReview::Unreviewed;
	// External receipt/review identity; a clip name is never evidence.
	std::string receipt;
};

struct WarriorCoverageKey {
	WarriorEnvironment environment = WarriorEnvironment::Unknown;
	// Qualification family. In death this is the witnessed origin; actual
	// current equipment/disposition is carried separately by the pilot context.
	WarriorWeapon equippedWeapon = WarriorWeapon::Unknown;
	WarriorArmor armor = WarriorArmor::Unknown;
	// Native phase family and approved visible family are distinct in death.
	WarriorWeapon nativePhaseWeapon = WarriorWeapon::Unknown;
	WarriorWeapon presentationWeapon = WarriorWeapon::Unknown;
	ActorVisualAction action = ActorVisualAction::Other;
	ActorPlayerSpellGraphic spellGraphic = ActorPlayerSpellGraphic::None;
	bool operator==(const WarriorCoverageKey &) const = default;
};

struct WarriorAttachmentCoverage {
	std::string bindingId;
	WarriorAttachmentKind kind = WarriorAttachmentKind::Sword;
	std::string resourceId;
	std::string resourceSha256;
	std::string boneName;
	ActorTransform local;
	WarriorEvidence calibration;
	WarriorSocketHand hand = WarriorSocketHand::Unknown;
	WarriorItemIdentity item;
};

struct WarriorActionCoverage {
	WarriorCoverageKey key;
	std::string clipName;
	WarriorEvidence pose;
	WarriorEvidence transitions;
	WarriorEvidence terminalHold;
	WarriorEvidence interruptions;
	WarriorEvidence nativeContinuation;
	// Closed pilot requires both death outcomes, with attachment retention and
	// removal independently qualified. Shared terminalHold also covers both.
	WarriorEvidence deathRetained;
	WarriorEvidence deathDropped;
	std::vector<std::string> deathDroppedAttachmentBindings;
	// All required attachments are named explicitly for this presentation.
	std::vector<std::string> attachmentBindings;
	bool loop = false;
	float authoredImpactPhase = -1;
	float authoredEmissionPhase = -1;
};

struct WarriorCoverageManifest {
	std::string assetId;
	std::string variant;
	std::string revision;
	std::string sourceSha256;
	std::string packageSha256;
	WarriorEvidence package;
	// Full gameplay admission needs separate review of these boundaries.
	WarriorEvidence rig;
	WarriorEvidence fit;
	WarriorEvidence materials;
	WarriorEvidence directions;
	std::vector<WarriorAttachmentCoverage> attachments;
	std::vector<WarriorActionCoverage> actions;
	WarriorPilotAuthorization closedPilot;
	WarriorEvidence closedPilotDomain;
};

struct WarriorVerifiedAttachment {
	std::string resourceId;
	std::string resourceSha256;
	WarriorItemIdentity item;
};

struct WarriorPresentationContext {
	WarriorCoverageKey key;
	WarriorPresentationSource source = WarriorPresentationSource::Unspecified;
	std::uint64_t levelEpoch = 0;
	std::uint32_t nativeIndex = 0;
	// Caller-owned activation/equipment generations: never a frame revision.
	std::uint64_t playerActivation = 0;
	std::uint64_t equipmentRevision = 0;
	// These hashes must come from actual verified loaded resources, not merely
	// from the requested registry selection. This helper performs no file I/O.
	std::string loadedPackageSha256;
	std::span<const WarriorVerifiedAttachment> loadedAttachments;
	WarriorWorldIdentity world;
	// Hashes authenticated by the trusted registry/approval loader. Comparing
	// them here does not itself authenticate a user-provided receipt.
	WarriorPilotAuthorization authenticatedPilot;
	WarriorEquipmentDisposition equipmentDisposition = WarriorEquipmentDisposition::Unknown;
	WarriorWeapon currentNativeEquipmentWeapon = WarriorWeapon::Unknown;
	WarriorArmor currentNativeEquipmentArmor = WarriorArmor::Unknown;
	std::int32_t currentNativeSwordItemId = -1;
	std::int32_t currentNativeShieldItemId = -1;
	WarriorDeathEquipmentWitness deathEquipment;
};

struct WarriorActorAdmission {
	std::size_t clipIndex = 0;
	float sampleSeconds = 0;
	ActorPlayerPhaseBinding phaseBinding;
	// Include the loaded package identity in any pose/attachment cache key.
	std::string packageSha256;
	WarriorAdmissionScope scope = WarriorAdmissionScope::DiagnosticAction;
	WarriorWorldIdentity world;
	WarriorPilotAuthorization pilotAuthorization;
	WarriorEquipmentDisposition equipmentDisposition = WarriorEquipmentDisposition::Unknown;
	WarriorWeapon effectivePresentationWeapon = WarriorWeapon::Unknown;
	std::uint64_t nativeDeathTransition = 0;
	WarriorCoverageKey key;
	std::uint64_t levelEpoch = 0;
	std::uint32_t nativeIndex = 0;
	std::uint64_t playerActivation = 0;
	std::uint64_t equipmentRevision = 0;
	// DiagnosticAction can resolve a candidate but can never suppress sprites.
	bool mayReplaceNativePresentation = false;
	std::vector<std::string> attachmentBindings;
};

namespace warrior_admission_detail {

inline bool ValidHash(const std::string &hash)
{
	if (hash.size() != 64) return false;
	for (const char value : hash)
		if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;
	return true;
}

inline bool Reviewed(const WarriorEvidence &evidence, WarriorAdmissionScope scope)
{
	const auto required = scope == WarriorAdmissionScope::Gameplay
	    ? WarriorReview::GameplayApproved : scope == WarriorAdmissionScope::ClosedSwordShieldPilot
	    ? WarriorReview::PilotApproved : WarriorReview::DiagnosticValidated;
	return !evidence.receipt.empty() && evidence.review >= required
	    && evidence.review <= WarriorReview::GameplayApproved;
}

inline bool Shield(WarriorWeapon weapon)
{
	return weapon == WarriorWeapon::UnarmedShield || weapon == WarriorWeapon::SwordShield
	    || weapon == WarriorWeapon::MaceShield;
}

inline WarriorWeapon NativePhaseWeapon(WarriorEnvironment environment, WarriorWeapon equipped, ActorVisualAction action)
{
	if (action == ActorVisualAction::Death) return WarriorWeapon::Unarmed;
	if (environment == WarriorEnvironment::Town && action == ActorVisualAction::Spell) {
		if (equipped == WarriorWeapon::Sword || equipped == WarriorWeapon::Mace) return WarriorWeapon::Unarmed;
		if (equipped == WarriorWeapon::SwordShield || equipped == WarriorWeapon::MaceShield) return WarriorWeapon::UnarmedShield;
	}
	return equipped;
}

inline bool SupportedKey(const WarriorCoverageKey &key)
{
	if (key.environment > WarriorEnvironment::Dungeon || key.equippedWeapon > WarriorWeapon::Staff
	    || key.armor > WarriorArmor::Heavy) return false;
	switch (key.action) {
	case ActorVisualAction::Idle:
	case ActorVisualAction::Walk:
	case ActorVisualAction::Hit:
	case ActorVisualAction::Death: break;
	case ActorVisualAction::Attack: if (key.equippedWeapon == WarriorWeapon::Bow) return false; break;
	case ActorVisualAction::RangedAttack: if (key.equippedWeapon != WarriorWeapon::Bow) return false; break;
	case ActorVisualAction::Block: if (!Shield(key.equippedWeapon)) return false; break;
	case ActorVisualAction::Spell:
		if (key.spellGraphic < ActorPlayerSpellGraphic::Fire || key.spellGraphic > ActorPlayerSpellGraphic::Magic) return false;
		break;
	default: return false;
	}
	if (key.action != ActorVisualAction::Spell && key.spellGraphic != ActorPlayerSpellGraphic::None) return false;
	const auto phaseWeapon = NativePhaseWeapon(key.environment, key.equippedWeapon, key.action);
	const auto visibleWeapon = key.action == ActorVisualAction::Death ? key.equippedWeapon : phaseWeapon;
	return key.nativePhaseWeapon == phaseWeapon && key.presentationWeapon == visibleWeapon;
}

inline unsigned AttachmentMask(WarriorWeapon weapon)
{
	const auto bit = [](WarriorAttachmentKind kind) { return 1U << static_cast<unsigned>(kind); };
	unsigned result = Shield(weapon) ? bit(WarriorAttachmentKind::Shield) : 0;
	switch (weapon) {
	case WarriorWeapon::Sword:
	case WarriorWeapon::SwordShield: return result | bit(WarriorAttachmentKind::Sword);
	case WarriorWeapon::Bow: return result | bit(WarriorAttachmentKind::Bow);
	case WarriorWeapon::Axe: return result | bit(WarriorAttachmentKind::Axe);
	case WarriorWeapon::Mace:
	case WarriorWeapon::MaceShield: return result | bit(WarriorAttachmentKind::Mace);
	case WarriorWeapon::Staff: return result | bit(WarriorAttachmentKind::Staff);
	default: return result;
	}
}

inline bool ValidAttachment(const ActorModel &model, const WarriorAttachmentCoverage &attachment)
{
	if (attachment.bindingId.empty() || attachment.resourceId.empty() || !ValidHash(attachment.resourceSha256)
	    || attachment.kind > WarriorAttachmentKind::Staff || attachment.boneName.empty()) return false;
	std::size_t node = model.nodes.size();
	for (std::size_t i = 0; i < model.nodes.size(); ++i) {
		if (model.nodes[i].name != attachment.boneName) continue;
		if (node != model.nodes.size()) return false;
		node = i;
	}
	if (node == model.nodes.size()) return false;
	unsigned joints = 0;
	for (const auto &joint : model.joints) if (joint.node == node) ++joints;
	if (joints != 1) return false;
	const auto &local = attachment.local;
	const auto &q = local.rotation;
	const float scale = local.scale.x;
	return std::isfinite(scale) && scale > 0 && local.scale.y == scale && local.scale.z == scale
	    && std::isfinite(local.translation.x) && std::isfinite(local.translation.y) && std::isfinite(local.translation.z)
	    && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w)
	    && std::abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1) <= .001F;
}

inline const WarriorActionCoverage *FindCoverage(const WarriorCoverageManifest &manifest, const WarriorCoverageKey &key)
{
	for (const auto &entry : manifest.actions) if (entry.key == key) return &entry;
	return nullptr;
}

inline bool QualifiedEntry(const ActorModel &model, const WarriorCoverageManifest &manifest,
    const WarriorActionCoverage &entry, WarriorAdmissionScope scope)
{
	if (!SupportedKey(entry.key) || entry.clipName.empty() || !Reviewed(entry.pose, scope)) return false;
	if (scope != WarriorAdmissionScope::DiagnosticAction && !Reviewed(entry.transitions, scope)) return false;
	if (scope == WarriorAdmissionScope::ClosedSwordShieldPilot
	    && (!Reviewed(entry.interruptions, scope)
	        || ((entry.key.action == ActorVisualAction::Walk || entry.key.action == ActorVisualAction::Attack)
	            && !Reviewed(entry.nativeContinuation, scope)))) return false;
	if (entry.key.action == ActorVisualAction::Death && !Reviewed(entry.terminalHold, scope)) return false;
	const bool attack = entry.key.action == ActorVisualAction::Attack || entry.key.action == ActorVisualAction::RangedAttack;
	const bool spell = entry.key.action == ActorVisualAction::Spell;
	const bool loop = entry.key.action == ActorVisualAction::Idle || entry.key.action == ActorVisualAction::Walk;
	if (entry.loop != loop || (attack ? !(entry.authoredImpactPhase > 0 && entry.authoredImpactPhase < 1) : entry.authoredImpactPhase != -1)
	    || (spell ? !(entry.authoredEmissionPhase > 0 && entry.authoredEmissionPhase < 1) : entry.authoredEmissionPhase != -1)) return false;
	unsigned clipCount = 0;
	for (const auto &clip : model.clips) if (clip.name == entry.clipName) ++clipCount;
	if (clipCount != 1) return false;
	unsigned mask = 0;
	for (const auto &id : entry.attachmentBindings) {
		const WarriorAttachmentCoverage *found = nullptr;
		for (const auto &attachment : manifest.attachments) if (attachment.bindingId == id) found = &attachment;
		if (found == nullptr || !ValidAttachment(model, *found) || !Reviewed(found->calibration, scope)) return false;
		const unsigned bit = 1U << static_cast<unsigned>(found->kind);
		if ((mask & bit) != 0) return false;
		mask |= bit;
	}
	return mask == AttachmentMask(entry.key.presentationWeapon);
}

inline bool FullGameplayCoverage(const ActorModel &model, const WarriorCoverageManifest &manifest)
{
	// This is deliberately calculated from explicit reviewed entries. A field
	// called complete or the presence of ten familiar clip names cannot pass it.
	constexpr std::array actions { ActorVisualAction::Idle, ActorVisualAction::Walk,
		ActorVisualAction::Attack, ActorVisualAction::RangedAttack, ActorVisualAction::Spell,
		ActorVisualAction::Hit, ActorVisualAction::Block, ActorVisualAction::Death };
	for (unsigned environment = 0; environment < 2; ++environment) {
		for (unsigned armor = 0; armor < 3; ++armor) {
			for (unsigned weapon = 0; weapon < 9; ++weapon) {
				for (const auto action : actions) {
					const unsigned firstSpell = action == ActorVisualAction::Spell ? 1 : 0;
					const unsigned lastSpell = action == ActorVisualAction::Spell ? 3 : 0;
					for (unsigned spell = firstSpell; spell <= lastSpell; ++spell) {
						WarriorCoverageKey key;
						key.environment = static_cast<WarriorEnvironment>(environment);
						key.armor = static_cast<WarriorArmor>(armor);
						key.equippedWeapon = static_cast<WarriorWeapon>(weapon);
						key.action = action;
						key.spellGraphic = static_cast<ActorPlayerSpellGraphic>(spell);
						key.nativePhaseWeapon = NativePhaseWeapon(key.environment, key.equippedWeapon, action);
						key.presentationWeapon = action == ActorVisualAction::Death ? key.equippedWeapon : key.nativePhaseWeapon;
						if (!SupportedKey(key)) continue;
						const auto *entry = FindCoverage(manifest, key);
						if (entry == nullptr || !QualifiedEntry(model, manifest, *entry, WarriorAdmissionScope::Gameplay)) return false;
					}
				}
			}
		}
	}
	return true;
}

inline WarriorCoverageKey ClosedPilotKey(WarriorEnvironment environment, ActorVisualAction action,
    ActorPlayerSpellGraphic spell = ActorPlayerSpellGraphic::None)
{
	WarriorCoverageKey key;
	key.environment = environment;
	key.equippedWeapon = WarriorWeapon::SwordShield;
	key.armor = WarriorArmor::Light;
	key.action = action;
	key.spellGraphic = spell;
	key.nativePhaseWeapon = NativePhaseWeapon(environment, key.equippedWeapon, action);
	key.presentationWeapon = action == ActorVisualAction::Death ? key.equippedWeapon : key.nativePhaseWeapon;
	return key;
}

inline bool ClosedPilotWorld(const WarriorPresentationContext &context)
{
	const auto &world = context.world;
	if (world.setLevel || world.expansion || world.shareware) return false;
	return context.key.environment == WarriorEnvironment::Town
	    ? world.kind == WarriorWorldKind::Town && world.levelNumber == 0
	    : context.key.environment == WarriorEnvironment::Dungeon
	        && world.kind == WarriorWorldKind::Cathedral && world.levelNumber == 1;
}

inline bool PilotItem(const WarriorAttachmentCoverage &attachment)
{
	const auto &item = attachment.item;
	if (item.assetId.empty() || item.variant.empty() || item.revision.empty()
	    || !ValidHash(item.sourceSha256) || !ValidHash(item.packageSha256)
	    || item.packageSha256 != attachment.resourceSha256) return false;
	if (attachment.kind == WarriorAttachmentKind::Sword)
		return item.nativeItemId == 1 && item.nativeItemSymbol == "IDI_WARRIOR" && attachment.hand == WarriorSocketHand::Right;
	if (attachment.kind == WarriorAttachmentKind::Shield)
		return item.nativeItemId == 2 && item.nativeItemSymbol == "IDI_WARRSHLD" && attachment.hand == WarriorSocketHand::Left;
	return false;
}

inline bool PilotGear(const WarriorCoverageManifest &manifest, const WarriorPresentationContext &context)
{
	if (manifest.attachments.size() != 2) return false;
	const WarriorAttachmentCoverage *sword = nullptr, *shield = nullptr;
	for (const auto &attachment : manifest.attachments) {
		if (!PilotItem(attachment) || !Reviewed(attachment.calibration, WarriorAdmissionScope::ClosedSwordShieldPilot)) return false;
		if (attachment.kind == WarriorAttachmentKind::Sword) sword = &attachment;
		if (attachment.kind == WarriorAttachmentKind::Shield) shield = &attachment;
		unsigned matches = 0;
		for (const auto &loaded : context.loadedAttachments)
			if (loaded.resourceId == attachment.resourceId && loaded.resourceSha256 == attachment.resourceSha256
			    && loaded.item == attachment.item) ++matches;
		if (matches != 1) return false;
	}
	return sword != nullptr && shield != nullptr && sword->boneName != shield->boneName;
}

inline bool PilotEquipment(const WarriorPresentationContext &context)
{
	if (context.currentNativeEquipmentArmor != WarriorArmor::Light) return false;
	if (context.key.action != ActorVisualAction::Death) {
		return context.equipmentDisposition == WarriorEquipmentDisposition::CurrentEquipped
		    && context.currentNativeEquipmentWeapon == WarriorWeapon::SwordShield
		    && context.currentNativeSwordItemId == 1 && context.currentNativeShieldItemId == 2
		    && context.deathEquipment.nativeDeathTransition == 0;
	}
	const auto &witness = context.deathEquipment;
	if (witness.levelEpoch != context.levelEpoch || witness.playerActivation != context.playerActivation
	    || witness.nativeDeathTransition == 0 || witness.equipmentRevisionBefore == 0
	    || witness.equipmentRevisionAfter != context.equipmentRevision || witness.originWeapon != WarriorWeapon::SwordShield
	    || witness.originArmor != WarriorArmor::Light || witness.originSwordItemId != 1 || witness.originShieldItemId != 2) return false;
	if (context.equipmentDisposition == WarriorEquipmentDisposition::DeathRetained) {
		return witness.equipmentRevisionAfter == witness.equipmentRevisionBefore
		    && witness.removedSwordItemId == -1 && witness.removedShieldItemId == -1
		    && context.currentNativeEquipmentWeapon == WarriorWeapon::SwordShield
		    && context.currentNativeSwordItemId == 1 && context.currentNativeShieldItemId == 2;
	}
	if (context.equipmentDisposition == WarriorEquipmentDisposition::DeathDropped) {
		return witness.equipmentRevisionAfter > witness.equipmentRevisionBefore
		    && witness.removedSwordItemId == 1 && witness.removedShieldItemId == 2
		    && context.currentNativeEquipmentWeapon == WarriorWeapon::Unarmed
		    && context.currentNativeSwordItemId == -1 && context.currentNativeShieldItemId == -1;
	}
	return false;
}

inline bool FullClosedPilotCoverage(const ActorModel &model, const WarriorCoverageManifest &manifest)
{
	if (manifest.actions.size() != 18) return false;
	constexpr std::array actions { ActorVisualAction::Idle, ActorVisualAction::Walk, ActorVisualAction::Attack,
		ActorVisualAction::Hit, ActorVisualAction::Block, ActorVisualAction::Death, ActorVisualAction::Spell };
	for (const auto environment : { WarriorEnvironment::Town, WarriorEnvironment::Dungeon }) {
		for (const auto action : actions) {
			const unsigned first = action == ActorVisualAction::Spell ? 1 : 0;
			const unsigned last = action == ActorVisualAction::Spell ? 3 : 0;
			for (unsigned spell = first; spell <= last; ++spell) {
				const auto key = ClosedPilotKey(environment, action, static_cast<ActorPlayerSpellGraphic>(spell));
				const auto *entry = FindCoverage(manifest, key);
				if (entry == nullptr || !QualifiedEntry(model, manifest, *entry, WarriorAdmissionScope::ClosedSwordShieldPilot)) return false;
				if (action == ActorVisualAction::Death
				    && (!Reviewed(entry->deathRetained, WarriorAdmissionScope::ClosedSwordShieldPilot)
				        || !Reviewed(entry->deathDropped, WarriorAdmissionScope::ClosedSwordShieldPilot)
				        || !entry->deathDroppedAttachmentBindings.empty())) return false;
			}
		}
	}
	return true;
}

inline bool ClosedPilotAuthorized(const ActorModel &model, const WarriorCoverageManifest &manifest,
    const WarriorPresentationContext &context)
{
	const auto scope = WarriorAdmissionScope::ClosedSwordShieldPilot;
	return !manifest.closedPilot.domainId.empty() && ValidHash(manifest.closedPilot.manifestSha256)
	    && ValidHash(manifest.closedPilot.authorizationSha256) && context.authenticatedPilot == manifest.closedPilot
	    && Reviewed(manifest.closedPilotDomain, scope) && Reviewed(manifest.rig, scope) && Reviewed(manifest.fit, scope)
	    && Reviewed(manifest.materials, scope) && Reviewed(manifest.directions, scope)
	    && context.key.equippedWeapon == WarriorWeapon::SwordShield && context.key.armor == WarriorArmor::Light
	    && ClosedPilotWorld(context) && PilotEquipment(context) && PilotGear(manifest, context)
	    && FullClosedPilotCoverage(model, manifest);
}

} // namespace warrior_admission_detail

// Pure and atomic: on refusal output remains unchanged. Caller supplies a
// consistent capture/context and actual package verification; this function
// does not recalculate native equipment, consult globals, emit events or run RNG.
inline bool ResolveWarriorActorClip(const ActorModel &model, const PlayerActorCapture &capture,
    const WarriorCoverageManifest &manifest, const WarriorPresentationContext &context,
    WarriorAdmissionScope scope, WarriorActorAdmission &output, std::string &error)
{
	using namespace warrior_admission_detail;
	const auto fail = [&error](const char *message) { error = message; return false; };
	if (scope != WarriorAdmissionScope::Gameplay && scope != WarriorAdmissionScope::DiagnosticAction
	    && scope != WarriorAdmissionScope::ClosedSwordShieldPilot)
		return fail("Invalid Warrior admission scope");
	if (model.assetId != "tristram.hero.warrior" || model.variant.empty() || model.revision.empty()
	    || !ValidHash(model.sourceSha256) || manifest.assetId != model.assetId || manifest.variant != model.variant
	    || manifest.revision != model.revision || manifest.sourceSha256 != model.sourceSha256
	    || !ValidHash(manifest.packageSha256) || context.loadedPackageSha256 != manifest.packageSha256
	    || !Reviewed(manifest.package, scope)) return fail("Warrior package/manifest identity or review mismatch");
	const auto &snapshot = capture.snapshot;
	if (snapshot.assetId != model.assetId || snapshot.variant != model.variant || snapshot.revision != model.revision
	    || snapshot.sourceSha256 != model.sourceSha256 || snapshot.kind != ActorVisualKind::Player)
		return fail("Warrior capture belongs to another model identity");
	if (context.levelEpoch == 0 || context.levelEpoch != capture.levelEpoch || context.nativeIndex >= 4
	    || context.nativeIndex != snapshot.nativeIndex || context.playerActivation == 0 || context.equipmentRevision == 0)
		return fail("Warrior context has no current epoch/slot/activation/equipment identity");
	if (!SupportedKey(context.key) || context.key.action != snapshot.action
	    || context.key.spellGraphic != capture.nativeSpellGraphic || static_cast<unsigned>(context.key.armor) != capture.nativeArmorGraphic)
		return fail("Warrior action, executed spell or armor context mismatch");
	const auto rawWeapon = snapshot.action == ActorVisualAction::Death ? WarriorWeapon::Unarmed : context.key.equippedWeapon;
	if (static_cast<unsigned>(rawWeapon) != capture.nativeWeaponGraphic)
		return fail("Warrior equipment context disagrees with captured native graphic");
	const auto source = snapshot.action == ActorVisualAction::Death ? WarriorPresentationSource::DeathRetainedEquipment
	    : snapshot.action == ActorVisualAction::Spell && context.key.environment == WarriorEnvironment::Town
	    ? WarriorPresentationSource::TownCastRemap : WarriorPresentationSource::AuthoritativeEquipment;
	if (context.source != source) return fail("Warrior presentation requires an explicit native phase policy");
	if (!capture.renderPhase.valid || capture.renderPhase.frame != snapshot.displayedFrame
	    || !std::isfinite(capture.renderPhase.subframe) || capture.renderPhase.subframe < 0 || capture.renderPhase.subframe > 1
	    || snapshot.frameCount == 0 || snapshot.frameCount > 127 || snapshot.ticksPerFrame > 127
	    || snapshot.sequenceLength != 0 || snapshot.sequenceIndex != 0 || snapshot.reversed
	    || snapshot.nativeDirection >= 8 || !std::isfinite(snapshot.authoritativeFootpoint.x)
	    || !std::isfinite(snapshot.authoritativeFootpoint.y) || !std::isfinite(snapshot.authoritativeFootpoint.z)
	    || !std::isfinite(snapshot.nativeYawRadians) || (snapshot.terminalHold
	        && (snapshot.action != ActorVisualAction::Death || snapshot.logicalFrame != static_cast<int>(snapshot.frameCount - 1)
	            || snapshot.displayedFrame != static_cast<int>(snapshot.frameCount - 1))))
		return fail("Warrior native rendered phase or placement is invalid");
	const bool attack = snapshot.action == ActorVisualAction::Attack || snapshot.action == ActorVisualAction::RangedAttack;
	const bool spell = snapshot.action == ActorVisualAction::Spell;
	if ((!attack && snapshot.nativeImpactFrame != -1) || (!spell && snapshot.nativeEmissionFrame != -1))
		return fail("Warrior capture contains a marker for another action");
	if (model.nodes.empty() || model.joints.empty() || model.clips.empty()) return fail("Warrior has no loaded rig/clips");
	for (std::size_t i = 0; i < model.clips.size(); ++i) {
		const auto &clip = model.clips[i];
		if (clip.name.empty() || clip.tracks.empty() || !std::isfinite(clip.startTime)
		    || !std::isfinite(clip.endTime) || clip.endTime <= clip.startTime) return fail("Warrior clip is invalid");
		for (std::size_t j = 0; j < i; ++j) if (model.clips[j].name == clip.name) return fail("Warrior clips have ambiguous names");
	}
	for (std::size_t i = 0; i < manifest.attachments.size(); ++i) {
		if (!ValidAttachment(model, manifest.attachments[i])) return fail("Warrior attachment/socket declaration is invalid");
		for (std::size_t j = 0; j < i; ++j)
			if (manifest.attachments[j].bindingId == manifest.attachments[i].bindingId) return fail("Warrior attachment binding is ambiguous");
	}
	for (std::size_t i = 0; i < manifest.actions.size(); ++i) {
		if (!SupportedKey(manifest.actions[i].key)) return fail("Warrior coverage declares an inconsistent phase family");
		for (std::size_t j = 0; j < i; ++j)
			if (manifest.actions[j].key == manifest.actions[i].key) return fail("Warrior coverage key is ambiguous");
	}
	if (scope == WarriorAdmissionScope::Gameplay
	    && (!Reviewed(manifest.rig, scope) || !Reviewed(manifest.fit, scope) || !Reviewed(manifest.materials, scope)
	        || !Reviewed(manifest.directions, scope) || !FullGameplayCoverage(model, manifest)))
		return fail("Warrior full equipment/armor/Town/dungeon/action/transition coverage is not approved");
	if (scope == WarriorAdmissionScope::ClosedSwordShieldPilot && !ClosedPilotAuthorized(model, manifest, context))
		return fail("Warrior closed SwordShield/light/Town/Cathedral1 pilot domain, gear, death witness or approval is incomplete");
	const auto *entry = FindCoverage(manifest, context.key);
	if (entry == nullptr || !QualifiedEntry(model, manifest, *entry, scope))
		return fail("Warrior current action has no explicit qualified coverage/attachments");
	for (const auto &id : entry->attachmentBindings) {
		const WarriorAttachmentCoverage *attachment = nullptr;
		for (const auto &candidate : manifest.attachments) if (candidate.bindingId == id) attachment = &candidate;
		unsigned matches = 0;
		for (const auto &loaded : context.loadedAttachments)
			if (loaded.resourceId == attachment->resourceId && loaded.resourceSha256 == attachment->resourceSha256) ++matches;
		if (matches != 1) return fail("Warrior attachment resource is not uniquely verified and loaded");
	}
	WarriorActorAdmission result;
	for (; result.clipIndex < model.clips.size(); ++result.clipIndex) if (model.clips[result.clipIndex].name == entry->clipName) break;
	result.phaseBinding.loop = entry->loop;
	result.phaseBinding.authoredImpactPhase = entry->authoredImpactPhase;
	result.phaseBinding.authoredEmissionPhase = entry->authoredEmissionPhase;
	result.phaseBinding.nativeRenderedSubframe = capture.renderPhase.subframe;
	if (!ActorPlayerClipTime(model, snapshot, result.clipIndex, result.phaseBinding, result.sampleSeconds, error)) return false;
	result.key = context.key;
	result.packageSha256 = context.loadedPackageSha256;
	result.scope = scope;
	result.world = context.world;
	result.pilotAuthorization = context.authenticatedPilot;
	result.equipmentDisposition = context.equipmentDisposition;
	result.effectivePresentationWeapon = context.key.presentationWeapon;
	result.nativeDeathTransition = context.deathEquipment.nativeDeathTransition;
	result.levelEpoch = context.levelEpoch;
	result.nativeIndex = context.nativeIndex;
	result.playerActivation = context.playerActivation;
	result.equipmentRevision = context.equipmentRevision;
	result.mayReplaceNativePresentation = scope != WarriorAdmissionScope::DiagnosticAction;
	result.attachmentBindings = entry->attachmentBindings;
	if (scope == WarriorAdmissionScope::ClosedSwordShieldPilot && context.equipmentDisposition == WarriorEquipmentDisposition::DeathDropped) {
		result.effectivePresentationWeapon = WarriorWeapon::Unarmed;
		result.attachmentBindings = entry->deathDroppedAttachmentBindings;
	}
	output = std::move(result);
	error.clear();
	return true;
}

} // namespace devilution
