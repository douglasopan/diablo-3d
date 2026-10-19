#include "engine/render/player_actor_presentation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include "player.h"

namespace devilution {
namespace {
bool Same(ActorVec3 a, ActorVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool SameFit(const ActorPlayerFit &a, const ActorPlayerFit &b) {
	return Same(a.authoredFootpoint, b.authoredFootpoint) && a.uniformScale == b.uniformScale && a.authoredYawOffset == b.authoredYawOffset;
}
bool SameVisual(const PlayerActorVisualContext &a, const PlayerActorVisualContext &b) {
	return a.world == b.world && a.nativeIndex == b.nativeIndex && Same(a.authoritativeFootpoint, b.authoritativeFootpoint)
	    && a.retainedInterpolation128 == b.retainedInterpolation128 && a.paused == b.paused && a.localPlayer == b.localPlayer
	    && a.hidden == b.hidden && a.nativeVisible == b.nativeVisible && a.frameSerial == b.frameSerial
	    && a.resourceGeneration == b.resourceGeneration && a.cameraRevision == b.cameraRevision && a.paletteRevision == b.paletteRevision
	    && a.translationIdentity == b.translationIdentity && a.nativeLight == b.nativeLight && a.selectable == b.selectable;
}
bool SameCapture(const PlayerActorCapture &a, const PlayerActorCapture &b) {
	const auto &x = a.snapshot; const auto &y = b.snapshot;
	return a.levelEpoch == b.levelEpoch && a.nativeWeaponGraphic == b.nativeWeaponGraphic && a.nativeArmorGraphic == b.nativeArmorGraphic
	    && a.nativeSpellGraphic == b.nativeSpellGraphic && a.renderPhase.frame == b.renderPhase.frame
	    && a.renderPhase.subframe == b.renderPhase.subframe && a.renderPhase.previousRecovery == b.renderPhase.previousRecovery
	    && a.renderPhase.valid == b.renderPhase.valid && x.kind == y.kind && x.nativeIndex == y.nativeIndex && x.action == y.action
	    && x.assetId == y.assetId && x.variant == y.variant && x.revision == y.revision && x.sourceSha256 == y.sourceSha256
	    && Same(x.authoritativeFootpoint, y.authoritativeFootpoint) && x.nativeDirection == y.nativeDirection && x.nativeYawRadians == y.nativeYawRadians
	    && x.logicalFrame == y.logicalFrame && x.displayedFrame == y.displayedFrame && x.frameCount == y.frameCount
	    && x.ticksPerFrame == y.ticksPerFrame && x.tickCounter == y.tickCounter && x.progressToNextTick128 == y.progressToNextTick128
	    && x.nativeImpactFrame == y.nativeImpactFrame && x.nativeEmissionFrame == y.nativeEmissionFrame
	    && x.sequenceIndex == y.sequenceIndex && x.sequenceLength == y.sequenceLength && x.paused == y.paused
	    && x.frozen == y.frozen && x.reversed == y.reversed && x.terminalHold == y.terminalHold
	    && x.hidden == y.hidden && x.localPlayer == y.localPlayer;
}
PlayerActorCaptureContext CaptureContext(const PlayerActorVisualContext &visual, std::uint64_t epoch) {
	return { visual.nativeIndex, epoch, visual.authoritativeFootpoint, visual.retainedInterpolation128,
		visual.paused, visual.localPlayer, visual.hidden };
}
WarriorEnvironment Environment(const WarriorWorldIdentity &world) {
	return world.kind == WarriorWorldKind::Town ? WarriorEnvironment::Town
	    : world.kind == WarriorWorldKind::Cathedral ? WarriorEnvironment::Dungeon : WarriorEnvironment::Unknown;
}
bool ValidVisual(const PlayerActorVisualContext &visual, std::string &error) {
	if (!visual.nativeVisible || visual.hidden || visual.frameSerial == 0 || visual.resourceGeneration == 0
	    || visual.cameraRevision == 0 || visual.paletteRevision == 0 || visual.nativeLight > 15
	    || visual.retainedInterpolation128 > 128 || Environment(visual.world) == WarriorEnvironment::Unknown) {
		error = "Actor presentation has no visible complete renderer context"; return false;
	}
	return true;
}
const PlayerActorMaterialBinding *Material(const PlayerActorPresentationResources &resources,
    std::string_view id, std::string_view hash, std::uint32_t material, WarriorEnvironment environment) {
	const PlayerActorMaterialBinding *result = nullptr;
	for (const auto &candidate : resources.materials) {
		if (candidate.resourceId != id || candidate.resourceSha256 != hash || candidate.material != material || candidate.environment != environment) continue;
		if (result != nullptr) return nullptr;
		result = &candidate;
	}
	return result;
}
bool ValidResources(const PlayerActorPresentationResources &resources, WarriorEnvironment environment, std::string &error) {
	const auto fail = [&error](const char *message) { error = message; return false; };
	if (resources.model == nullptr || resources.manifest == nullptr || resources.bodyResourceId.empty()
	    || resources.loaderGeneration == 0 || !warrior_admission_detail::ValidHash(resources.loadedPackageSha256)
	    || resources.attachments.size() > 8 || resources.materials.empty() || resources.materials.size() > 257)
		return fail("Actor presentation lacks verified immutable loader resources");
	const auto &model = *resources.model;
	if (model.textureWidth == 0 || model.textureWidth > 2048 || model.textureHeight == 0 || model.textureHeight > 2048
	    || model.textureRgb.size() != static_cast<std::size_t>(model.textureWidth) * model.textureHeight * 3)
		return fail("Actor body texture is not a complete decoded diffuse payload");
	for (std::size_t i = 0; i < resources.materials.size(); ++i) {
		const auto &material = resources.materials[i];
		if (material.resourceId.empty() || !warrior_admission_detail::ValidHash(material.resourceSha256)
		    || material.identity == 0 || material.revision == 0 || material.environment > WarriorEnvironment::Dungeon)
			return fail("Actor material binding has no current resource identity");
		for (std::size_t j = 0; j < i; ++j) {
			const auto &other = resources.materials[j];
			if (other.resourceId == material.resourceId && other.material == material.material && other.environment == material.environment)
				return fail("Actor material reference is ambiguous");
		}
		bool found = material.resourceId == resources.bodyResourceId && material.resourceSha256 == resources.loadedPackageSha256 && material.material == 0;
		for (const auto &resource : resources.attachments) {
			if (material.resourceId != resource.verified.resourceId || material.resourceSha256 != resource.verified.resourceSha256) continue;
			for (const auto &part : resource.parts) if (part.material == material.material) found = true;
		}
		if (!found) return fail("Actor material binding refers to an absent loaded resource/part");
	}
	if (Material(resources, resources.bodyResourceId, resources.loadedPackageSha256, 0, environment) == nullptr)
		return fail("Actor body material is missing for the current native palette world");
	for (std::size_t i = 0; i < resources.attachments.size(); ++i) {
		const auto &resource = resources.attachments[i]; const auto &verified = resource.verified;
		if (verified.resourceId.empty() || verified.resourceId == resources.bodyResourceId || !warrior_admission_detail::ValidHash(verified.resourceSha256)
		    || resource.parts.empty() || resource.parts.size() > 32)
			return fail("Actor rigid resource is not uniquely loaded with complete parts");
		for (std::size_t j = 0; j < i; ++j) if (resources.attachments[j].verified.resourceId == verified.resourceId)
			return fail("Actor rigid resource identity is ambiguous");
		for (const auto &part : resource.parts) {
			if (part.vertices.empty() || part.vertices.size() > 100000 || part.indices.empty() || part.indices.size() % 3 != 0 || part.indices.size() > 300000)
				return fail("Actor rigid resource has an incomplete bounded triangle payload");
			for (auto index : part.indices) if (index >= part.vertices.size()) return fail("Actor rigid resource index outside part");
			for (const auto &vertex : part.vertices) {
				const auto &p = vertex.position; const auto &n = vertex.normal; const auto &color = vertex.color;
				if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(n.x) || !std::isfinite(n.y)
				    || !std::isfinite(n.z) || !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y) || n.x*n.x+n.y*n.y+n.z*n.z <= 0)
					return fail("Actor rigid resource has invalid position/normal/UV");
				if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) || !std::isfinite(color.w)
				    || color.x < 0 || color.x > 1 || color.y < 0 || color.y > 1 || color.z < 0 || color.z > 1 || color.w < 0 || color.w > 1)
					return fail("Actor rigid resource has invalid authored vertex color");
			}
			if (Material(resources, verified.resourceId, verified.resourceSha256, part.material, environment) == nullptr)
				return fail("Actor rigid part material is missing for the current native palette world");
		}
	}
	for (const auto &binding : resources.manifest->attachments) {
		unsigned matches = 0;
		for (const auto &resource : resources.attachments)
			if (resource.verified.resourceId == binding.resourceId && resource.verified.resourceSha256 == binding.resourceSha256
			    && resource.verified.item == binding.item) ++matches;
		if (matches != 1) return fail("Actor manifest binding has no uniquely loaded exact item resource");
	}
	return true;
}
} // namespace

PlayerActorPresentation::ResourceStamp PlayerActorPresentation::StampResources(const PlayerActorPresentationResources &resources) {
	ResourceStamp stamp { reinterpret_cast<std::uintptr_t>(resources.model), reinterpret_cast<std::uintptr_t>(resources.manifest),
		resources.bodyResourceId, resources.loadedPackageSha256, resources.authenticatedPilot, resources.loaderGeneration, {}, {} };
	stamp.materials.assign(resources.materials.begin(), resources.materials.end());
	for (const auto &resource : resources.attachments) {
		RigidStamp rigid { resource.verified.resourceId, resource.verified.resourceSha256, resource.verified.item, {} };
		for (const auto &part : resource.parts) rigid.parts.push_back({ part.material, reinterpret_cast<std::uintptr_t>(part.vertices.data()),
			reinterpret_cast<std::uintptr_t>(part.indices.data()), part.vertices.size(), part.indices.size() });
		stamp.rigid.push_back(std::move(rigid));
	}
	return stamp;
}
PlayerActorPresentation::NativeStamp PlayerActorPresentation::StampNative(const Player &player) {
	NativeStamp stamp { player.position.tile.x, player.position.tile.y, player._pHitPoints, 0, 0, 0, 0, player._pInvincible };
	if (player._pmode == PM_SPELL) { const auto &spell = player.executedSpell;
		stamp.spellId = static_cast<int>(spell.spellId); stamp.spellType = static_cast<int>(spell.spellType);
		stamp.spellFrom = spell.spellFrom; stamp.spellLevel = spell.spellLevel;
	}
	return stamp;
}
PlayerActorPresentationResult PlayerActorPresentation::Prepare(const Player &player, const PlayerActorLifecycle &lifecycle,
    const PlayerActorPresentationResources &resources, const PlayerActorVisualContext &visual,
    const ActorPlayerFit &fit, WarriorAdmissionScope scope, std::string &error)
{
	Invalidate(); // An atomic helper's preserved old output is never published as a new frame.
	try {
		if (!ValidVisual(visual, error) || !ValidResources(resources, Environment(visual.world), error)) return PlayerActorPresentationResult::Refused;
		const auto resourceStamp = StampResources(resources);
		const auto nativeStamp = StampNative(player);
		PlayerActorPreparedPresentation next;
		PlayerActorLifecycleError nativeError;
		if (!lifecycle.ReadSnapshot(player, visual.nativeIndex, visual.world, next.lifecycle, &nativeError)) {
			error = PlayerActorLifecycleErrorText(nativeError); return PlayerActorPresentationResult::Refused;
		}
		if (!CaptureWarriorActor(player, *resources.model, CaptureContext(visual, next.lifecycle.levelEpoch), next.capture, error)) return PlayerActorPresentationResult::Refused;
		std::vector<WarriorVerifiedAttachment> verified;
		for (const auto &resource : resources.attachments) verified.push_back(resource.verified);
		WarriorPresentationContext context;
		const auto &native = next.lifecycle; const auto action = next.capture.snapshot.action;
		context.key.environment = Environment(native.world); context.key.action = action; context.key.spellGraphic = next.capture.nativeSpellGraphic;
		context.key.equippedWeapon = action == ActorVisualAction::Death ? native.deathEquipment.originWeapon : native.currentNativeEquipmentWeapon;
		context.key.armor = action == ActorVisualAction::Death ? native.deathEquipment.originArmor : native.currentNativeEquipmentArmor;
		context.key.nativePhaseWeapon = warrior_admission_detail::NativePhaseWeapon(context.key.environment, context.key.equippedWeapon, action);
		context.key.presentationWeapon = action == ActorVisualAction::Death ? context.key.equippedWeapon : context.key.nativePhaseWeapon;
		context.source = action == ActorVisualAction::Death ? WarriorPresentationSource::DeathRetainedEquipment
		    : action == ActorVisualAction::Spell && context.key.environment == WarriorEnvironment::Town
		    ? WarriorPresentationSource::TownCastRemap : WarriorPresentationSource::AuthoritativeEquipment;
		context.levelEpoch = native.levelEpoch; context.nativeIndex = native.nativeIndex; context.playerActivation = native.playerActivation;
		context.equipmentRevision = native.equipmentRevision; context.world = native.world; context.equipmentDisposition = native.equipmentDisposition;
		context.currentNativeEquipmentWeapon = native.currentNativeEquipmentWeapon; context.currentNativeEquipmentArmor = native.currentNativeEquipmentArmor;
		context.currentNativeSwordItemId = native.currentNativeSwordItemId; context.currentNativeShieldItemId = native.currentNativeShieldItemId;
		context.deathEquipment = native.deathEquipment; context.loadedPackageSha256 = resources.loadedPackageSha256;
		context.loadedAttachments = verified; context.authenticatedPilot = resources.authenticatedPilot;
		if (!ResolveWarriorActorClip(*resources.model, next.capture, *resources.manifest, context, scope, next.admission, error)) return PlayerActorPresentationResult::Refused;
		std::vector<ActorPlayerAttachmentInput> inputs;
		next.materials.push_back(*Material(resources, resources.bodyResourceId, resources.loadedPackageSha256, 0, context.key.environment));
		for (const auto &binding : next.admission.attachmentBindings) {
			const WarriorAttachmentCoverage *coverage = nullptr;
			for (const auto &entry : resources.manifest->attachments) if (entry.bindingId == binding) coverage = &entry;
			if (coverage == nullptr) { error = "Admitted attachment binding has no declaration"; return PlayerActorPresentationResult::Refused; }
			const PlayerActorRigidResource *loaded = nullptr;
			for (const auto &resource : resources.attachments) if (resource.verified.resourceId == coverage->resourceId
			    && resource.verified.resourceSha256 == coverage->resourceSha256 && resource.verified.item == coverage->item) loaded = &resource;
			if (loaded == nullptr) { error = "Admitted attachment has no exact loaded item resource"; return PlayerActorPresentationResult::Refused; }
			inputs.push_back({ loaded->verified.item.assetId, loaded->verified.item.revision, loaded->verified.item.sourceSha256,
				coverage->boneName, loaded->verified.item.nativeItemId, coverage->local, loaded->parts });
			std::vector<std::size_t> materials;
			for (const auto &part : loaded->parts) {
				materials.push_back(next.materials.size());
				next.materials.push_back(*Material(resources, loaded->verified.resourceId, loaded->verified.resourceSha256, part.material, context.key.environment));
			}
			next.attachmentMaterials.push_back(std::move(materials));
		}
		if (!PrepareActorPlayerFrame(*resources.model, next.capture, native.levelEpoch, native.nativeIndex, next.admission.clipIndex,
		    next.admission.phaseBinding, fit, inputs, next.geometry, error)) return PlayerActorPresentationResult::Refused;
		PlayerActorCapture finalCapture;
		if (!lifecycle.IsCurrent(player, native) || !(StampNative(player) == nativeStamp)
		    || !(StampResources(resources) == resourceStamp)
		    || !CaptureWarriorActor(player, *resources.model, CaptureContext(visual, native.levelEpoch), finalCapture, error)
		    || !SameCapture(next.capture, finalCapture)) {
			error = "Native actor or loader resources changed during frame preparation"; return PlayerActorPresentationResult::Refused;
		}
		next.source = context.source; next.visual = visual; next.fit = fit;
		prepared_.emplace(std::move(next)); resourceStamp_ = resourceStamp; nativeStamp_ = nativeStamp;
		if (preparationCount_ != std::numeric_limits<std::uint64_t>::max()) ++preparationCount_;
		error.clear(); return PlayerActorPresentationResult::Prepared;
	} catch (const std::bad_alloc &) { Invalidate(); error = "Actor presentation allocation failed before drawing"; return PlayerActorPresentationResult::Refused; }
}
const PlayerActorPreparedPresentation *PlayerActorPresentation::Current(const Player &player, const PlayerActorLifecycle &lifecycle,
    const PlayerActorPresentationResources &resources, const PlayerActorVisualContext &visual,
    const ActorPlayerFit &fit, WarriorAdmissionScope scope, std::string &error) const
{
	try {
		if (!prepared_ || !SameVisual(visual, prepared_->visual) || !SameFit(fit, prepared_->fit)
		    || scope != prepared_->admission.scope || resources.model == nullptr || !(StampResources(resources) == resourceStamp_)
		    || !(StampNative(player) == nativeStamp_) || !lifecycle.IsCurrent(player, prepared_->lifecycle)) {
			error = "Actor prepared frame is absent or belongs to stale native/resource/visual context"; return nullptr;
		}
		PlayerActorCapture current;
		if (!CaptureWarriorActor(player, *resources.model, CaptureContext(visual, prepared_->lifecycle.levelEpoch), current, error)
		    || !SameCapture(current, prepared_->capture)) { error = "Actor native pose changed after frame preparation"; return nullptr; }
		error.clear(); return &*prepared_;
	} catch (const std::bad_alloc &) { error = "Actor freshness check allocation failed"; return nullptr; }
}
} // namespace devilution
