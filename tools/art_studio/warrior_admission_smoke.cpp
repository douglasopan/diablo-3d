#include "engine/render/warrior_actor_admission.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

namespace {
using namespace devilution;

WarriorCoverageKey Key(ActorVisualAction action, ActorPlayerSpellGraphic spell = ActorPlayerSpellGraphic::None)
{
	WarriorCoverageKey key;
	key.environment = WarriorEnvironment::Dungeon;
	key.equippedWeapon = key.nativePhaseWeapon = key.presentationWeapon = WarriorWeapon::Sword;
	key.armor = WarriorArmor::Light;
	key.action = action;
	key.spellGraphic = spell;
	if (action == ActorVisualAction::Death) key.nativePhaseWeapon = WarriorWeapon::Unarmed;
	return key;
}

PlayerActorCapture Capture(const ActorModel &model, const WarriorCoverageKey &key)
{
	PlayerActorCapture capture;
	auto &snapshot = capture.snapshot;
	snapshot.assetId = model.assetId;
	snapshot.variant = model.variant;
	snapshot.revision = model.revision;
	snapshot.sourceSha256 = model.sourceSha256;
	snapshot.action = key.action;
	snapshot.nativeIndex = 1;
	snapshot.frameCount = 20;
	snapshot.ticksPerFrame = 2;
	snapshot.logicalFrame = snapshot.displayedFrame = 6;
	snapshot.tickCounter = 1;
	snapshot.progressToNextTick128 = 64;
	snapshot.nativeDirection = 2;
	snapshot.nativeYawRadians = ActorNativeDirectionYaw(2);
	snapshot.authoritativeFootpoint = { 20, 0, 30 };
	if (key.action == ActorVisualAction::Attack || key.action == ActorVisualAction::RangedAttack) snapshot.nativeImpactFrame = 8;
	if (key.action == ActorVisualAction::Spell) snapshot.nativeEmissionFrame = 13;
	capture.nativeSpellGraphic = key.spellGraphic;
	capture.nativeArmorGraphic = static_cast<std::uint8_t>(key.armor);
	capture.nativeWeaponGraphic = key.action == ActorVisualAction::Death ? 0 : static_cast<std::uint8_t>(key.equippedWeapon);
	capture.levelEpoch = 71;
	capture.renderPhase = { 6, .75F, false, true };
	return capture;
}

WarriorPresentationContext Context(const WarriorCoverageKey &key, const std::string &packageSha,
    std::span<const WarriorVerifiedAttachment> attachments)
{
	WarriorPresentationContext context;
	context.key = key;
	context.source = key.action == ActorVisualAction::Death ? WarriorPresentationSource::DeathRetainedEquipment
	    : key.environment == WarriorEnvironment::Town && key.action == ActorVisualAction::Spell
	    ? WarriorPresentationSource::TownCastRemap : WarriorPresentationSource::AuthoritativeEquipment;
	context.levelEpoch = 71;
	context.nativeIndex = 1;
	context.playerActivation = 4;
	context.equipmentRevision = 12;
	context.loadedPackageSha256 = packageSha;
	context.loadedAttachments = attachments;
	return context;
}

bool SameAdmission(const WarriorActorAdmission &a, const WarriorActorAdmission &b)
{
	return a.clipIndex == b.clipIndex && a.sampleSeconds == b.sampleSeconds && a.key == b.key && a.packageSha256 == b.packageSha256
	    && a.scope == b.scope && a.world == b.world && a.pilotAuthorization == b.pilotAuthorization
	    && a.equipmentDisposition == b.equipmentDisposition && a.effectivePresentationWeapon == b.effectivePresentationWeapon
	    && a.nativeDeathTransition == b.nativeDeathTransition
	    && a.phaseBinding.loop == b.phaseBinding.loop && a.phaseBinding.authoredImpactPhase == b.phaseBinding.authoredImpactPhase
	    && a.phaseBinding.authoredEmissionPhase == b.phaseBinding.authoredEmissionPhase
	    && a.phaseBinding.nativeRenderedSubframe == b.phaseBinding.nativeRenderedSubframe
	    && a.levelEpoch == b.levelEpoch && a.nativeIndex == b.nativeIndex && a.playerActivation == b.playerActivation
	    && a.equipmentRevision == b.equipmentRevision && a.mayReplaceNativePresentation == b.mayReplaceNativePresentation
	    && a.attachmentBindings == b.attachmentBindings;
}
} // namespace

int main(int argc, char **argv)
{
	using namespace devilution;
	// The runner independently computes these loaded byte hashes. Contexts below
	// are isolated DTO fixtures; they are not evidence of real Player capture or
	// equipment/armor/Town gameplay approval. The 18-key pilot authorization below
	// is explicitly synthetic and never supplies production asset approval.
	if (argc != 3) return 2;
	std::ifstream file(argv[1], std::ios::binary);
	std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
	ActorModel model;
	std::string error;
	if (!LoadActorModel(bytes, model, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 3; }
	WarriorCoverageManifest manifest;
	manifest.assetId = model.assetId;
	manifest.variant = model.variant;
	manifest.revision = model.revision;
	manifest.sourceSha256 = model.sourceSha256;
	manifest.packageSha256 = argv[2];
	manifest.package = { WarriorReview::DiagnosticValidated, "warrior-clips-bundle-r2/DELIVERY-BUNDLE-R2.json" };
	WarriorAttachmentCoverage sword;
	sword.bindingId = "sword-r4";
	sword.resourceId = "diagnostic-left-whole-sword-candidate";
	sword.resourceSha256 = "9c538ac4e283a6852a012eb70b649ee1d79af805f46cfe59bd0fb585fd82078a";
	sword.boneName = "mixamorig:RightHand";
	sword.local.translation = { -.172469862F, .034413638F, .239612873F };
	sword.local.rotation = { .324448084F, .630767581F, .563525598F, -.423443739F };
	sword.local.scale = { .719648526F, .719648526F, .719648526F };
	sword.calibration = { WarriorReview::DiagnosticValidated, "weapon-socket-calibration-r4/receipt.json; fixture-only resource verification" };
	manifest.attachments.push_back(sword);
	const std::array verifiedAttachments { WarriorVerifiedAttachment { sword.resourceId, sword.resourceSha256, {} } };
	const auto addEntry = [&manifest](const WarriorCoverageKey &key, const char *clip) {
		WarriorActionCoverage entry;
		entry.key = key;
		entry.clipName = clip;
		entry.pose = { WarriorReview::DiagnosticValidated, "isolated resolver DTO fixture; candidate C++ pose only" };
		entry.attachmentBindings = { "sword-r4" };
		entry.loop = key.action == ActorVisualAction::Idle || key.action == ActorVisualAction::Walk;
		if (key.action == ActorVisualAction::Attack || key.action == ActorVisualAction::RangedAttack) entry.authoredImpactPhase = .5F;
		if (key.action == ActorVisualAction::Spell) entry.authoredEmissionPhase = .68F;
		if (key.action == ActorVisualAction::Death)
			entry.terminalHold = { WarriorReview::DiagnosticValidated, "warrior-clips-bundle-r2/DELIVERY-BUNDLE-R2.json" };
		manifest.actions.push_back(entry);
	};
	addEntry(Key(ActorVisualAction::Idle), "idle");
	addEntry(Key(ActorVisualAction::Walk), "walk");
	addEntry(Key(ActorVisualAction::Attack), "attack");
	addEntry(Key(ActorVisualAction::Hit), "hit");
	addEntry(Key(ActorVisualAction::Spell, ActorPlayerSpellGraphic::Fire), "cast-fire");
	addEntry(Key(ActorVisualAction::Spell, ActorPlayerSpellGraphic::Lightning), "cast-lightning");
	addEntry(Key(ActorVisualAction::Spell, ActorPlayerSpellGraphic::Magic), "cast-magic");
	addEntry(Key(ActorVisualAction::Death), "death");
	unsigned checks = 0, positives = 0, refusals = 0;
	std::vector<std::string> cases;
	const auto check = [&](bool condition, const char *name) {
		if (!condition) { std::fprintf(stderr, "FAIL %s: %s\n", name, error.c_str()); return false; }
		++checks;
		cases.emplace_back(name);
		return true;
	};
	const auto refuse = [&](const ActorModel &candidate, const PlayerActorCapture &capture, const WarriorCoverageManifest &coverage,
	                         const WarriorPresentationContext &context, const char *name, WarriorAdmissionScope scope = WarriorAdmissionScope::DiagnosticAction) {
		WarriorActorAdmission output;
		output.clipIndex = 999;
		output.sampleSeconds = -99;
		output.levelEpoch = 123;
		output.nativeIndex = 3;
		output.playerActivation = 222;
		output.equipmentRevision = 333;
		output.packageSha256 = "sentinel-package";
		output.scope = WarriorAdmissionScope::ClosedSwordShieldPilot;
		output.world = { WarriorWorldKind::Other, 44, true, true, true };
		output.pilotAuthorization = { "sentinel-domain", "sentinel-manifest", "sentinel-authorization" };
		output.equipmentDisposition = WarriorEquipmentDisposition::DeathDropped;
		output.effectivePresentationWeapon = WarriorWeapon::Staff;
		output.nativeDeathTransition = 77;
		output.mayReplaceNativePresentation = true;
		output.phaseBinding.authoredEmissionPhase = .42F;
		output.attachmentBindings = { "sentinel" };
		const auto original = output;
		const bool passed = !ResolveWarriorActorClip(candidate, capture, coverage, context, scope, output, error)
		    && !error.empty() && SameAdmission(output, original);
		if (passed) ++refusals;
		return check(passed, name);
	};
	for (const auto &entry : manifest.actions) {
		const auto capture = Capture(model, entry.key);
		const auto context = Context(entry.key, manifest.packageSha256, verifiedAttachments);
		WarriorActorAdmission output;
		ActorPose pose;
		if (!check(ResolveWarriorActorClip(model, capture, manifest, context, WarriorAdmissionScope::DiagnosticAction, output, error)
		        && !output.mayReplaceNativePresentation && model.clips[output.clipIndex].name == entry.clipName
		        && output.levelEpoch == 71 && output.nativeIndex == 1 && output.equipmentRevision == 12 && output.playerActivation == 4
		        && output.packageSha256 == manifest.packageSha256
		        && EvaluateActorPose(model, output.clipIndex, output.sampleSeconds, pose, error), entry.clipName.c_str())) return 4;
		++positives;
	}
	const auto idleKey = Key(ActorVisualAction::Idle);
	const auto idleCapture = Capture(model, idleKey);
	const auto idleContext = Context(idleKey, manifest.packageSha256, verifiedAttachments);
	if (!refuse(model, idleCapture, manifest, idleContext, "partial_bundle_gameplay_refused", WarriorAdmissionScope::Gameplay)) return 5;
	if (!check(!warrior_admission_detail::FullGameplayCoverage(model, manifest), "explicit_matrix_is_incomplete")) return 5;
	{
		auto bodyReviewed = manifest;
		// Negative-only fixture: even body review cannot confer missing actions.
		const WarriorEvidence fixture { WarriorReview::GameplayApproved, "negative-test-only body boundary" };
		bodyReviewed.package = bodyReviewed.rig = bodyReviewed.fit = bodyReviewed.materials = bodyReviewed.directions = fixture;
		if (!refuse(model, idleCapture, bodyReviewed, idleContext, "body_review_cannot_replace_full_coverage", WarriorAdmissionScope::Gameplay)) return 5;
	}
	{
		auto coverage = manifest;
		coverage.actions.pop_back();
		const auto key = Key(ActorVisualAction::Death);
		if (!refuse(model, Capture(model, key), coverage, Context(key, manifest.packageSha256, verifiedAttachments), "missing_death_despite_death_clip")) return 6;
		coverage = manifest;
		coverage.actions.back().terminalHold = {};
		if (!refuse(model, Capture(model, key), coverage, Context(key, manifest.packageSha256, verifiedAttachments), "death_hold_has_no_review")) return 6;
		auto capture = Capture(model, key);
		auto context = Context(key, manifest.packageSha256, verifiedAttachments);
		context.source = WarriorPresentationSource::Unspecified;
		if (!refuse(model, capture, manifest, context, "death_nibble_zero_needs_explicit_origin")) return 6;
		context = Context(key, manifest.packageSha256, verifiedAttachments);
		context.key.equippedWeapon = WarriorWeapon::Unknown;
		if (!refuse(model, capture, manifest, context, "death_unknown_equipment_refused")) return 6;
		context = Context(key, manifest.packageSha256, verifiedAttachments);
		capture.nativeWeaponGraphic = 2;
		if (!refuse(model, capture, manifest, context, "death_nonzero_graphic_refused")) return 6;
		capture = Capture(model, key);
		capture.snapshot.terminalHold = true;
		if (!refuse(model, capture, manifest, context, "death_terminal_hold_before_native_last_frame_refused")) return 6;
		capture.snapshot.logicalFrame = capture.snapshot.displayedFrame = capture.renderPhase.frame = 19;
		capture.renderPhase.subframe = 0;
		WarriorActorAdmission output;
		if (!check(ResolveWarriorActorClip(model, capture, manifest, context, WarriorAdmissionScope::DiagnosticAction, output, error)
		        && output.sampleSeconds == model.clips[output.clipIndex].endTime && output.key.presentationWeapon == WarriorWeapon::Sword
		        && !output.mayReplaceNativePresentation, "death_zero_nibble_retains_explicit_sword_and_terminal_pose")) return 6;
		++positives;
	}
	{
		auto key = idleKey;
		key.equippedWeapon = key.nativePhaseWeapon = key.presentationWeapon = WarriorWeapon::SwordShield;
		if (!refuse(model, Capture(model, key), manifest, Context(key, manifest.packageSha256, verifiedAttachments), "starting_sword_shield_not_sword_coverage")) return 7;
		key.action = ActorVisualAction::Block;
		auto coverage = manifest;
		WarriorActionCoverage block;
		block.key = key;
		block.clipName = "block-posture";
		block.pose = { WarriorReview::DiagnosticValidated, "negative-only block fixture" };
		block.attachmentBindings = { "sword-r4" };
		coverage.actions.push_back(block);
		if (!refuse(model, Capture(model, key), coverage, Context(key, manifest.packageSha256, verifiedAttachments), "block_posture_without_shield_binding_refused")) return 7;
		key = idleKey;
		key.equippedWeapon = key.nativePhaseWeapon = key.presentationWeapon = WarriorWeapon::Bow;
		key.action = ActorVisualAction::RangedAttack;
		if (!refuse(model, Capture(model, key), manifest, Context(key, manifest.packageSha256, verifiedAttachments), "ranged_bow_missing_coverage")) return 7;
		for (const auto armor : { WarriorArmor::Medium, WarriorArmor::Heavy }) {
			key = idleKey;
			key.armor = armor;
			if (!refuse(model, Capture(model, key), manifest, Context(key, manifest.packageSha256, verifiedAttachments),
			        armor == WarriorArmor::Medium ? "medium_armor_missing_coverage" : "heavy_armor_missing_coverage")) return 7;
		}
		key = idleKey;
		key.environment = WarriorEnvironment::Town;
		if (!refuse(model, Capture(model, key), manifest, Context(key, manifest.packageSha256, verifiedAttachments), "town_idle_missing_coverage")) return 7;
	}
	{
		auto key = Key(ActorVisualAction::Spell, ActorPlayerSpellGraphic::Fire);
		key.environment = WarriorEnvironment::Town;
		key.nativePhaseWeapon = key.presentationWeapon = WarriorWeapon::Unarmed;
		auto coverage = manifest;
		auto townCast = coverage.actions[4];
		townCast.key = key;
		townCast.attachmentBindings.clear();
		townCast.pose.receipt = "isolated Town remap DTO policy fixture; no Town art approval";
		coverage.actions.push_back(townCast);
		const auto capture = Capture(model, key);
		auto context = Context(key, manifest.packageSha256, verifiedAttachments);
		WarriorActorAdmission output;
		if (!check(ResolveWarriorActorClip(model, capture, coverage, context, WarriorAdmissionScope::DiagnosticAction, output, error)
		        && output.key.equippedWeapon == WarriorWeapon::Sword && output.key.nativePhaseWeapon == WarriorWeapon::Unarmed
		        && output.attachmentBindings.empty() && !output.mayReplaceNativePresentation, "explicit_town_cast_remap_diagnostic_only")) return 8;
		++positives;
		context.key.nativePhaseWeapon = context.key.presentationWeapon = WarriorWeapon::Sword;
		if (!refuse(model, capture, coverage, context, "town_cast_cannot_infer_phase_from_raw_sword_nibble")) return 8;
		context = Context(key, manifest.packageSha256, verifiedAttachments);
		context.source = WarriorPresentationSource::AuthoritativeEquipment;
		if (!refuse(model, capture, coverage, context, "town_cast_requires_remap_source")) return 8;
		key.equippedWeapon = WarriorWeapon::SwordShield;
		key.nativePhaseWeapon = key.presentationWeapon = WarriorWeapon::UnarmedShield;
		if (!refuse(model, Capture(model, key), coverage, Context(key, manifest.packageSha256, verifiedAttachments), "town_cast_shield_remap_requires_its_own_coverage")) return 8;
	}
	{
		const auto key = Key(ActorVisualAction::Spell, ActorPlayerSpellGraphic::Lightning);
		auto capture = Capture(model, key);
		auto context = Context(key, manifest.packageSha256, verifiedAttachments);
		capture.snapshot.logicalFrame = capture.snapshot.displayedFrame = 13;
		capture.renderPhase.frame = 13;
		capture.renderPhase.subframe = 0;
		WarriorActorAdmission output;
		if (!check(ResolveWarriorActorClip(model, capture, manifest, context, WarriorAdmissionScope::DiagnosticAction, output, error)
		        && model.clips[output.clipIndex].name == "cast-lightning"
		        && std::abs(output.sampleSeconds - (model.clips[output.clipIndex].startTime
		            + .68F * (model.clips[output.clipIndex].endTime - model.clips[output.clipIndex].startTime))) < 1e-6F,
		        "executed_lightning_selects_and_anchors_lightning")) return 9;
		++positives;
		context.key.spellGraphic = ActorPlayerSpellGraphic::Fire;
		if (!refuse(model, capture, manifest, context, "stale_fire_context_cannot_override_executed_lightning")) return 9;
		context = Context(key, manifest.packageSha256, verifiedAttachments);
		capture.snapshot.nativeEmissionFrame = -1;
		if (!refuse(model, capture, manifest, context, "spell_missing_native_emission_marker")) return 9;
		capture.snapshot.nativeEmissionFrame = 19;
		if (!refuse(model, capture, manifest, context, "spell_terminal_emission_marker_invalid")) return 9;
		capture = Capture(model, key);
		capture.snapshot.nativeImpactFrame = 8;
		if (!refuse(model, capture, manifest, context, "spell_with_attack_marker_refused")) return 9;
		const auto attackKey = Key(ActorVisualAction::Attack);
		capture = Capture(model, attackKey);
		capture.snapshot.nativeImpactFrame = -1;
		if (!refuse(model, capture, manifest, Context(attackKey, manifest.packageSha256, verifiedAttachments), "attack_missing_native_impact_marker")) return 9;
	}
	{
		auto context = idleContext;
		++context.levelEpoch;
		if (!refuse(model, idleCapture, manifest, context, "epoch_invalidation")) return 10;
		context = idleContext;
		++context.nativeIndex;
		if (!refuse(model, idleCapture, manifest, context, "slot_invalidation")) return 10;
		context = idleContext;
		context.playerActivation = 0;
		if (!refuse(model, idleCapture, manifest, context, "activation_identity_required")) return 10;
		context = idleContext;
		context.equipmentRevision = 0;
		if (!refuse(model, idleCapture, manifest, context, "equipment_identity_required")) return 10;
		context = idleContext;
		context.loadedPackageSha256[0] = context.loadedPackageSha256[0] == 'a' ? 'b' : 'a';
		if (!refuse(model, idleCapture, manifest, context, "loaded_package_hash_mismatch")) return 10;
		context = idleContext;
		context.loadedAttachments = {};
		if (!refuse(model, idleCapture, manifest, context, "attachment_not_loaded")) return 10;
		auto capture = idleCapture;
		capture.snapshot.revision += "-other";
		if (!refuse(model, capture, manifest, idleContext, "capture_revision_invalidation")) return 10;
		capture = idleCapture;
		capture.renderPhase.frame = 7;
		if (!refuse(model, capture, manifest, idleContext, "rendered_phase_invalidation")) return 10;
		capture = idleCapture;
		capture.renderPhase.subframe = std::numeric_limits<float>::quiet_NaN();
		if (!refuse(model, capture, manifest, idleContext, "nonfinite_render_phase")) return 10;
		capture = idleCapture;
		capture.snapshot.nativeImpactFrame = 8;
		if (!refuse(model, capture, manifest, idleContext, "idle_with_attack_marker_refused")) return 10;
		capture = idleCapture;
		capture.snapshot.frameCount = 128;
		if (!refuse(model, capture, manifest, idleContext, "native_frame_count_outside_capture_range")) return 10;
		capture = idleCapture;
		capture.snapshot.ticksPerFrame = 128;
		if (!refuse(model, capture, manifest, idleContext, "native_tick_delay_outside_capture_range")) return 10;
		auto coverage = manifest;
		coverage.actions.push_back(coverage.actions.front());
		if (!refuse(model, idleCapture, coverage, idleContext, "duplicate_coverage_refused")) return 10;
		coverage = manifest;
		coverage.actions.front().pose = {};
		if (!refuse(model, idleCapture, coverage, idleContext, "familiar_clip_name_without_review_refused")) return 10;
		coverage = manifest;
		coverage.attachments.front().boneName = "missing-hand";
		if (!refuse(model, idleCapture, coverage, idleContext, "missing_attachment_socket")) return 10;
		coverage = manifest;
		coverage.attachments.front().local.scale.y = 2;
		if (!refuse(model, idleCapture, coverage, idleContext, "nonuniform_attachment_scale")) return 10;
		auto duplicateClip = model;
		duplicateClip.clips.push_back(duplicateClip.clips.front());
		if (!refuse(duplicateClip, idleCapture, manifest, idleContext, "duplicate_clip_name")) return 10;
	}
	{
		auto capture = idleCapture;
		capture.snapshot.paused = true;
		WarriorActorAdmission a, b;
		if (!check(ResolveWarriorActorClip(model, capture, manifest, idleContext, WarriorAdmissionScope::DiagnosticAction, a, error)
		        && ResolveWarriorActorClip(model, capture, manifest, idleContext, WarriorAdmissionScope::DiagnosticAction, b, error)
		        && SameAdmission(a, b), "paused_repeat_does_not_advance")) return 11;
		capture.snapshot.logicalFrame = 0;
		capture.snapshot.displayedFrame = capture.renderPhase.frame = 15;
		capture.renderPhase.previousRecovery = true;
		capture.renderPhase.subframe = .4F;
		if (!check(ResolveWarriorActorClip(model, capture, manifest, idleContext, WarriorAdmissionScope::DiagnosticAction, a, error)
		        && a.phaseBinding.nativeRenderedSubframe == .4F, "native_displayed_fraction_preserved")) return 11;
	}
	unsigned syntheticPilotPositives = 0;
	{
		// No artwork is approved here. These fake identity/evidence values prove
		// only the trusted-caller policy boundary and are labeled in the result.
		const auto pilotScope = WarriorAdmissionScope::ClosedSwordShieldPilot;
		const WarriorEvidence syntheticReview { WarriorReview::PilotApproved, "SYNTHETIC TEST ONLY: no production approval" };
		auto pilot = manifest;
		pilot.actions.clear();
		pilot.attachments.clear();
		pilot.package = pilot.rig = pilot.fit = pilot.materials = pilot.directions = pilot.closedPilotDomain = syntheticReview;
		pilot.closedPilot = { "SYNTHETIC:starter-light-sword-shield-town-c1", std::string(64, 'a'), std::string(64, 'b') };
		WarriorAttachmentCoverage shortSword = sword;
		shortSword.bindingId = "SYNTHETIC:short-sword";
		shortSword.resourceId = "SYNTHETIC:short-sword-resource";
		shortSword.resourceSha256 = std::string(64, 'c');
		shortSword.hand = WarriorSocketHand::Right;
		shortSword.calibration = syntheticReview;
		shortSword.item = { 1, "IDI_WARRIOR", "SYNTHETIC:short-sword-asset", "fixture", "fixture-r1", std::string(64, 'd'), shortSword.resourceSha256 };
		WarriorAttachmentCoverage buckler = shortSword;
		buckler.bindingId = "SYNTHETIC:buckler";
		buckler.resourceId = "SYNTHETIC:buckler-resource";
		buckler.resourceSha256 = std::string(64, 'e');
		buckler.kind = WarriorAttachmentKind::Shield;
		buckler.boneName = "mixamorig:LeftHand";
		buckler.hand = WarriorSocketHand::Left;
		buckler.item = { 2, "IDI_WARRSHLD", "SYNTHETIC:buckler-asset", "fixture", "fixture-r1", std::string(64, 'f'), buckler.resourceSha256 };
		pilot.attachments = { shortSword, buckler };
		const std::array pilotLoaded {
			WarriorVerifiedAttachment { shortSword.resourceId, shortSword.resourceSha256, shortSword.item },
			WarriorVerifiedAttachment { buckler.resourceId, buckler.resourceSha256, buckler.item },
		};
		const auto pilotKey = [](WarriorEnvironment environment, ActorVisualAction action, ActorPlayerSpellGraphic spell = ActorPlayerSpellGraphic::None) {
			auto key = Key(action, spell);
			key.environment = environment;
			key.equippedWeapon = key.nativePhaseWeapon = key.presentationWeapon = WarriorWeapon::SwordShield;
			if (action == ActorVisualAction::Death) key.nativePhaseWeapon = WarriorWeapon::Unarmed;
			else if (environment == WarriorEnvironment::Town && action == ActorVisualAction::Spell)
				key.nativePhaseWeapon = key.presentationWeapon = WarriorWeapon::UnarmedShield;
			return key;
		};
		const auto pilotContext = [&](const WarriorCoverageKey &key) {
			auto context = Context(key, pilot.packageSha256, pilotLoaded);
			context.world.kind = key.environment == WarriorEnvironment::Town ? WarriorWorldKind::Town : WarriorWorldKind::Cathedral;
			context.world.levelNumber = key.environment == WarriorEnvironment::Town ? 0 : 1;
			context.authenticatedPilot = pilot.closedPilot;
			context.currentNativeEquipmentWeapon = WarriorWeapon::SwordShield;
			context.currentNativeEquipmentArmor = WarriorArmor::Light;
			context.currentNativeSwordItemId = 1;
			context.currentNativeShieldItemId = 2;
			context.equipmentDisposition = WarriorEquipmentDisposition::CurrentEquipped;
			if (key.action == ActorVisualAction::Death) {
				context.equipmentDisposition = WarriorEquipmentDisposition::DeathRetained;
				context.deathEquipment = { 71, 4, 91, 12, 12, WarriorWeapon::SwordShield, WarriorArmor::Light, 1, 2, -1, -1 };
			}
			return context;
		};
		constexpr std::array pilotActions { ActorVisualAction::Idle, ActorVisualAction::Walk, ActorVisualAction::Attack,
			ActorVisualAction::Hit, ActorVisualAction::Block, ActorVisualAction::Death, ActorVisualAction::Spell };
		for (const auto environment : { WarriorEnvironment::Town, WarriorEnvironment::Dungeon }) {
			for (const auto action : pilotActions) {
				const unsigned first = action == ActorVisualAction::Spell ? 1 : 0;
				const unsigned last = action == ActorVisualAction::Spell ? 3 : 0;
				for (unsigned spell = first; spell <= last; ++spell) {
					WarriorActionCoverage entry;
					entry.key = pilotKey(environment, action, static_cast<ActorPlayerSpellGraphic>(spell));
					switch (action) {
					case ActorVisualAction::Idle: entry.clipName = "idle"; break;
					case ActorVisualAction::Walk: entry.clipName = "walk"; break;
					case ActorVisualAction::Attack: entry.clipName = "attack"; break;
					case ActorVisualAction::Hit: entry.clipName = "hit"; break;
					case ActorVisualAction::Block: entry.clipName = "block-posture"; break;
					case ActorVisualAction::Death: entry.clipName = "death"; break;
					case ActorVisualAction::Spell: entry.clipName = spell == 1 ? "cast-fire" : spell == 2 ? "cast-lightning" : "cast-magic"; break;
					default: return 12;
					}
					entry.pose = entry.transitions = entry.interruptions = syntheticReview;
					entry.attachmentBindings = { shortSword.bindingId, buckler.bindingId };
					if (environment == WarriorEnvironment::Town && action == ActorVisualAction::Spell)
						entry.attachmentBindings = { buckler.bindingId };
					entry.loop = action == ActorVisualAction::Idle || action == ActorVisualAction::Walk;
					if (action == ActorVisualAction::Walk || action == ActorVisualAction::Attack) entry.nativeContinuation = syntheticReview;
					if (action == ActorVisualAction::Attack) entry.authoredImpactPhase = .5F;
					if (action == ActorVisualAction::Spell) entry.authoredEmissionPhase = .68F;
					if (action == ActorVisualAction::Death) entry.terminalHold = entry.deathRetained = entry.deathDropped = syntheticReview;
					pilot.actions.push_back(entry);
				}
			}
		}
		for (const auto &entry : pilot.actions) {
			auto capture = Capture(model, entry.key);
			const auto context = pilotContext(entry.key);
			WarriorActorAdmission output;
			const auto name = std::string("SYNTHETIC_PILOT:") + (entry.key.environment == WarriorEnvironment::Town ? "town:" : "c1:") + entry.clipName;
			if (!check(ResolveWarriorActorClip(model, capture, pilot, context, pilotScope, output, error)
			        && output.mayReplaceNativePresentation && output.scope == pilotScope && output.world == context.world
			        && output.pilotAuthorization == pilot.closedPilot && output.attachmentBindings == entry.attachmentBindings,
			        name.c_str())) return 12;
			++syntheticPilotPositives;
		}
		const auto key = pilotKey(WarriorEnvironment::Dungeon, ActorVisualAction::Idle);
		const auto capture = Capture(model, key);
		const auto context = pilotContext(key);
		if (!refuse(model, capture, manifest, context, "real_candidate_incomplete_refuses_pilot", pilotScope)) return 13;
		if (!refuse(model, capture, pilot, context, "18_key_pilot_does_not_confer_global_450_approval", WarriorAdmissionScope::Gameplay)) return 13;
		WarriorActorAdmission diagnostic;
		if (!check(ResolveWarriorActorClip(model, capture, pilot, context, WarriorAdmissionScope::DiagnosticAction, diagnostic, error)
		        && !diagnostic.mayReplaceNativePresentation, "synthetic_pilot_manifest_diagnostic_still_cannot_suppress_sprites")) return 13;
		for (std::size_t missing = 0; missing < pilot.actions.size(); ++missing) {
			auto incomplete = pilot;
			incomplete.actions.erase(incomplete.actions.begin() + missing);
			const auto name = std::string("whole_pilot_refuses_missing_key_") + std::to_string(missing);
			if (!refuse(model, capture, incomplete, context, name.c_str(), pilotScope)) return 13;
		}
		{
			auto bad = context;
			bad.authenticatedPilot.manifestSha256[0] = 'b';
			if (!refuse(model, capture, pilot, bad, "pilot_manifest_not_authenticated", pilotScope)) return 14;
			bad = context; bad.authenticatedPilot.authorizationSha256[0] = 'a';
			if (!refuse(model, capture, pilot, bad, "pilot_approval_not_authenticated", pilotScope)) return 14;
			bad = context; bad.world.levelNumber = 2;
			if (!refuse(model, capture, pilot, bad, "pilot_c2_outside_world_domain", pilotScope)) return 14;
			bad = context; bad.world.kind = WarriorWorldKind::Other;
			if (!refuse(model, capture, pilot, bad, "pilot_other_dungeon_outside_world_domain", pilotScope)) return 14;
			bad = context; bad.world.setLevel = true;
			if (!refuse(model, capture, pilot, bad, "pilot_set_level_outside_world_domain", pilotScope)) return 14;
			bad = context; bad.world.expansion = true;
			if (!refuse(model, capture, pilot, bad, "pilot_expansion_outside_world_domain", pilotScope)) return 14;
			bad = context; bad.world.shareware = true;
			if (!refuse(model, capture, pilot, bad, "pilot_shareware_outside_world_domain", pilotScope)) return 14;
			bad = context; bad.currentNativeSwordItemId = 99;
			if (!refuse(model, capture, pilot, bad, "pilot_same_graphic_wrong_native_item", pilotScope)) return 14;
			bad = context; bad.equipmentDisposition = WarriorEquipmentDisposition::Unknown;
			if (!refuse(model, capture, pilot, bad, "pilot_actual_equipment_disposition_required", pilotScope)) return 14;
			auto badKey = key; badKey.armor = WarriorArmor::Medium;
			if (!refuse(model, Capture(model, badKey), pilot, pilotContext(badKey), "pilot_medium_armor_outside_domain", pilotScope)) return 14;
			badKey = key; badKey.equippedWeapon = badKey.nativePhaseWeapon = badKey.presentationWeapon = WarriorWeapon::Bow;
			if (!refuse(model, Capture(model, badKey), pilot, pilotContext(badKey), "pilot_bow_outside_domain", pilotScope)) return 14;
			auto gear = pilotLoaded; gear[1].item.revision = "unverified-shield-revision";
			bad = context; bad.loadedAttachments = gear;
			if (!refuse(model, capture, pilot, bad, "pilot_loaded_shield_identity_mismatch", pilotScope)) return 14;
			auto incomplete = pilot; incomplete.attachments[1].boneName = incomplete.attachments[0].boneName;
			if (!refuse(model, capture, incomplete, context, "pilot_shield_cannot_share_sword_socket", pilotScope)) return 14;
			incomplete = pilot; incomplete.attachments[1].hand = WarriorSocketHand::Right;
			if (!refuse(model, capture, incomplete, context, "pilot_shield_requires_left_semantic_socket", pilotScope)) return 14;
			incomplete = pilot; incomplete.actions[1].transitions = {};
			if (!refuse(model, capture, incomplete, context, "pilot_missing_noncurrent_transition_refuses_whole_domain", pilotScope)) return 14;
			incomplete = pilot; incomplete.actions[2].nativeContinuation = {};
			if (!refuse(model, capture, incomplete, context, "pilot_missing_attack_carry_refuses_whole_domain", pilotScope)) return 14;
			incomplete = pilot; incomplete.actions[8].interruptions = {};
			if (!refuse(model, capture, incomplete, context, "pilot_missing_noncurrent_interruption_refuses_whole_domain", pilotScope)) return 14;
		}
		for (const auto environment : { WarriorEnvironment::Town, WarriorEnvironment::Dungeon }) {
			const auto deathKey = pilotKey(environment, ActorVisualAction::Death);
			auto deathCapture = Capture(model, deathKey);
			deathCapture.snapshot.logicalFrame = deathCapture.snapshot.displayedFrame = deathCapture.renderPhase.frame = 19;
			// DoDeath uses 100 ticks/frame on its final frame; the sampled native
			// fraction remains valid but must not pull a held corpse off its end.
			deathCapture.snapshot.ticksPerFrame = 100;
			deathCapture.snapshot.tickCounter = 30;
			deathCapture.snapshot.progressToNextTick128 = 0;
			deathCapture.renderPhase.subframe = .3F;
			deathCapture.snapshot.terminalHold = true;
			auto retained = pilotContext(deathKey);
			WarriorActorAdmission output;
			if (!check(ResolveWarriorActorClip(model, deathCapture, pilot, retained, pilotScope, output, error)
			        && output.effectivePresentationWeapon == WarriorWeapon::SwordShield && output.attachmentBindings.size() == 2
			        && output.nativeDeathTransition == 91 && output.sampleSeconds == model.clips[output.clipIndex].endTime,
			        environment == WarriorEnvironment::Town ? "SYNTHETIC_PILOT:town_retained_death_hold" : "SYNTHETIC_PILOT:c1_retained_death_hold")) return 15;
			++syntheticPilotPositives;
			auto dropped = retained;
			dropped.equipmentDisposition = WarriorEquipmentDisposition::DeathDropped;
			dropped.equipmentRevision = dropped.deathEquipment.equipmentRevisionAfter = 13;
			dropped.deathEquipment.removedSwordItemId = 1;
			dropped.deathEquipment.removedShieldItemId = 2;
			dropped.currentNativeEquipmentWeapon = WarriorWeapon::Unarmed;
			dropped.currentNativeSwordItemId = dropped.currentNativeShieldItemId = -1;
			if (!check(ResolveWarriorActorClip(model, deathCapture, pilot, dropped, pilotScope, output, error)
			        && output.effectivePresentationWeapon == WarriorWeapon::Unarmed && output.attachmentBindings.empty()
			        && output.equipmentDisposition == WarriorEquipmentDisposition::DeathDropped && output.equipmentRevision == 13
			        && output.sampleSeconds == model.clips[output.clipIndex].endTime && deathCapture.nativeWeaponGraphic == 0,
			        environment == WarriorEnvironment::Town ? "SYNTHETIC_PILOT:town_dropped_corpse_no_attachments" : "SYNTHETIC_PILOT:c1_dropped_corpse_no_attachments")) return 15;
			++syntheticPilotPositives;
			auto bad = dropped; bad.deathEquipment = {};
			if (!refuse(model, deathCapture, pilot, bad, "pilot_empty_inventory_is_not_death_origin_proof", pilotScope)) return 15;
			bad = dropped; ++bad.deathEquipment.playerActivation;
			if (!refuse(model, deathCapture, pilot, bad, "pilot_death_origin_activation_mismatch", pilotScope)) return 15;
			bad = dropped; ++bad.deathEquipment.levelEpoch;
			if (!refuse(model, deathCapture, pilot, bad, "pilot_death_origin_epoch_mismatch", pilotScope)) return 15;
			bad = dropped; bad.deathEquipment.removedShieldItemId = -1;
			if (!refuse(model, deathCapture, pilot, bad, "pilot_drop_must_observe_both_native_items", pilotScope)) return 15;
			bad = dropped; bad.currentNativeSwordItemId = 1;
			if (!refuse(model, deathCapture, pilot, bad, "pilot_drop_cannot_pretend_current_equipment", pilotScope)) return 15;
			bad = dropped; bad.deathEquipment.equipmentRevisionAfter = bad.deathEquipment.equipmentRevisionBefore;
			if (!refuse(model, deathCapture, pilot, bad, "pilot_drop_requires_actual_equipment_revision_change", pilotScope)) return 15;
			bad = retained; bad.deathEquipment.removedSwordItemId = 1;
			if (!refuse(model, deathCapture, pilot, bad, "pilot_retention_cannot_use_drop_witness", pilotScope)) return 15;
			auto incomplete = pilot;
			for (auto &entry : incomplete.actions) if (entry.key == deathKey) entry.deathDropped = {};
			if (!refuse(model, capture, incomplete, context, "pilot_missing_drop_disposition_review_refuses_whole_domain", pilotScope)) return 15;
			incomplete = pilot;
			for (auto &entry : incomplete.actions) if (entry.key == deathKey) entry.deathRetained = {};
			if (!refuse(model, capture, incomplete, context, "pilot_missing_retained_disposition_review_refuses_whole_domain", pilotScope)) return 15;
			incomplete = pilot;
			for (auto &entry : incomplete.actions) if (entry.key == deathKey) entry.deathDroppedAttachmentBindings = { shortSword.bindingId };
			if (!refuse(model, capture, incomplete, context, "pilot_dropped_corpse_cannot_keep_attachment", pilotScope)) return 15;
		}
	}
	std::printf("{\"checks\":%u,\"positiveDiagnosticContexts\":%u,\"positiveSyntheticPilotContexts\":%u,\"atomicRefusals\":%u,\"cases\":[", checks, positives, syntheticPilotPositives, refusals);
	for (std::size_t i = 0; i < cases.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", cases[i].c_str());
	std::printf("],\"syntheticPilotAuthorizationFixture\":true,\"requiredPilotActionKeys\":18,\"fullGameplayManifestFabricated\":false,\"realCandidatePilotAdmitted\":false,\"productionPilotApproved\":false,\"gameplayCoverageApproved\":false,\"nativeCaptureExercised\":false,\"nativeDropEventExercised\":false,\"rendererConnected\":false,\"productionSpriteSuppressionAuthorized\":false,\"globalsRngEventsUsed\":false}\n");
	return 0;
}
