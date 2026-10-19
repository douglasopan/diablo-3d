#include "engine/render/player_actor_resources.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include <picosha2.h>

namespace devilution {

struct PlayerActorResourceStorage {
	ActorModel body;
	std::array<ActorRigidResource, 2> rigid;
	std::array<std::vector<ActorRigidPartInput>, 2> partViews;
	std::array<PlayerActorRigidResource, 2> rigidViews;
	std::array<WarriorAttachmentCoverage, 2> bindings;
	std::array<ActorPlayerAttachmentInput, 2> frameInputs;
	std::vector<PlayerActorResourceClipRole> ledger;
	std::optional<WarriorCoverageManifest> manifest;
	std::vector<PlayerActorMaterialBinding> materials;
	PlayerActorPresentationResources presentation;
};

namespace {
constexpr std::size_t MaxPacketBytes = 128U * 1024U * 1024U;
constexpr std::size_t MaxVertices = 100000, MaxIndices = 300000;
constexpr std::array<std::string_view, 11> Roles { "death", "block", "cast-fire", "cast-lightning", "cast-magic",
	"idle", "walk", "attack", "hit", "walk-entry", "walk-exit" };

bool Fail(std::string &error, const char *message)
{
	error = message;
	return false;
}

bool Text(std::string_view value, std::size_t max = 127)
{
	return !value.empty() && value.size() <= max
	    && std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

bool HashText(const std::string &value)
{
	return warrior_admission_detail::ValidHash(value);
}

bool Same(float a, float b)
{
	return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool Same(ActorVec3 a, ActorVec3 b)
{
	return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z);
}

bool Same(ActorVec4 a, ActorVec4 b)
{
	return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z) && Same(a.w, b.w);
}

bool Same(const ActorTransform &a, const ActorTransform &b)
{
	return Same(a.translation, b.translation) && Same(a.rotation, b.rotation) && Same(a.scale, b.scale);
}

bool Same(const WarriorEvidence &a, const WarriorEvidence &b)
{
	return a.review == b.review && a.receipt == b.receipt;
}

bool Same(const WarriorAttachmentCoverage &a, const WarriorAttachmentCoverage &b)
{
	return a.bindingId == b.bindingId && a.kind == b.kind && a.resourceId == b.resourceId
	    && a.resourceSha256 == b.resourceSha256 && a.boneName == b.boneName && Same(a.local, b.local)
	    && Same(a.calibration, b.calibration) && a.hand == b.hand && a.item == b.item;
}

std::string PackageHash(std::span<const std::byte> bytes)
{
	picosha2::hash256_one_by_one hash;
	const auto *first = reinterpret_cast<const std::uint8_t *>(bytes.data());
	hash.process(first, first + bytes.size());
	hash.finish();
	return picosha2::get_hash_hex_string(hash);
}

bool Expected(const PlayerActorResourcePacket &packet)
{
	const auto &id = packet.expected;
	return !packet.bytes.empty() && packet.bytes.size() <= MaxPacketBytes && Text(id.resourceId)
	    && Text(id.assetId) && Text(id.variant) && Text(id.revision) && HashText(id.sourceSha256)
	    && HashText(id.packageSha256) && id.sourceSha256 != id.packageSha256;
}

template <class Resource>
bool Identity(const Resource &resource, const PlayerActorExpectedResource &expected)
{
	return resource.assetId == expected.assetId && resource.variant == expected.variant
	    && resource.revision == expected.revision && resource.sourceSha256 == expected.sourceSha256;
}

bool Diagnostic(const WarriorEvidence &evidence)
{
	return evidence.review <= WarriorReview::DiagnosticValidated
	    && (evidence.receipt.empty() ? evidence.review == WarriorReview::Unreviewed : Text(evidence.receipt, 1024));
}

ActorTransform Socket(bool sword)
{
	// float32 conversion of the real frozen right/left recipes. These constants
	// describe only this diagnostic candidate, never calibration/art approval.
	if (sword) return { { -0.004626527338436933F, 0.07166664838453221F, 0.015139352580342625F },
		{ 0.11092371420877929F, -0.13528301899135078F, -0.6343335528273173F, 0.7530042351515613F },
		{ 0.5504108214918582F, 0.5504108214918582F, 0.5504108214918582F } };
	return { { -0.026768344615913472F, 0.07093311584053634F, 0.04824623237044686F },
		{ 0.01410490115818619F, -0.28424469601123303F, -0.024063744247310736F, 0.9583459400266688F },
		{ 0.5504109427301928F, 0.5504109427301928F, 0.5504109427301928F } };
}

bool Binding(const ActorModel &body, const ActorRigidResource &resource,
    const PlayerActorExpectedResource &expected, const WarriorAttachmentCoverage &binding, bool sword)
{
	const auto &item = binding.item;
	return Text(binding.bindingId) && binding.kind == (sword ? WarriorAttachmentKind::Sword : WarriorAttachmentKind::Shield)
	    && binding.hand == (sword ? WarriorSocketHand::Right : WarriorSocketHand::Left)
	    && binding.boneName == (sword ? "mixamorig:RightHand" : "mixamorig:LeftHand")
	    && binding.resourceId == expected.resourceId && binding.resourceSha256 == expected.packageSha256
	    && item.nativeItemId == (sword ? 1 : 2) && item.nativeItemSymbol == (sword ? "IDI_WARRIOR" : "IDI_WARRSHLD")
	    && item.assetId == resource.assetId && item.variant == resource.variant && item.revision == resource.revision
	    && item.sourceSha256 == resource.sourceSha256 && item.packageSha256 == resource.packageSha256
	    && Same(binding.local, Socket(sword)) && Diagnostic(binding.calibration)
	    && warrior_admission_detail::ValidAttachment(body, binding);
}

std::string_view ActionRole(const WarriorActionCoverage &action)
{
	switch (action.key.action) {
	case ActorVisualAction::Idle: return "idle";
	case ActorVisualAction::Walk: return "walk";
	case ActorVisualAction::Attack: return "attack";
	case ActorVisualAction::Hit: return "hit";
	case ActorVisualAction::Block: return "block";
	case ActorVisualAction::Death: return "death";
	case ActorVisualAction::Spell:
		switch (action.key.spellGraphic) {
		case ActorPlayerSpellGraphic::Fire: return "cast-fire";
		case ActorPlayerSpellGraphic::Lightning: return "cast-lightning";
		case ActorPlayerSpellGraphic::Magic: return "cast-magic";
		default: return {};
		}
	default: return {};
	}
}

bool Manifest(const WarriorCoverageManifest &manifest, const PlayerActorResourceStorage &storage,
    const PlayerActorExpectedResource &body)
{
	if (manifest.assetId != storage.body.assetId || manifest.variant != storage.body.variant
	    || manifest.revision != storage.body.revision || manifest.sourceSha256 != storage.body.sourceSha256
	    || manifest.packageSha256 != body.packageSha256 || manifest.attachments.size() != 2 || manifest.actions.size() > 18
	    || !manifest.closedPilot.domainId.empty() || !manifest.closedPilot.manifestSha256.empty() || !manifest.closedPilot.authorizationSha256.empty()
	    || !Diagnostic(manifest.package) || !Diagnostic(manifest.rig) || !Diagnostic(manifest.fit)
	    || !Diagnostic(manifest.materials) || !Diagnostic(manifest.directions) || !Diagnostic(manifest.closedPilotDomain)) return false;
	for (std::size_t i = 0; i < 2; ++i) if (!Same(manifest.attachments[i], storage.bindings[i])) return false;
	for (std::size_t i = 0; i < manifest.actions.size(); ++i) {
		const auto &entry = manifest.actions[i];
		if (!warrior_admission_detail::SupportedKey(entry.key) || entry.key.environment > WarriorEnvironment::Dungeon || entry.key.armor != WarriorArmor::Light
		    || entry.key.equippedWeapon != WarriorWeapon::SwordShield || ActionRole(entry).empty()
		    || entry.key != warrior_admission_detail::ClosedPilotKey(entry.key.environment, entry.key.action, entry.key.spellGraphic)
		    || entry.clipName != ActionRole(entry) || !Diagnostic(entry.pose) || !Diagnostic(entry.transitions)
		    || !Diagnostic(entry.terminalHold) || !Diagnostic(entry.interruptions) || !Diagnostic(entry.nativeContinuation)
		    || !Diagnostic(entry.deathRetained) || !Diagnostic(entry.deathDropped)
		    || entry.attachmentBindings.size() > 2 || !entry.deathDroppedAttachmentBindings.empty()) return false;
		const auto phase = [](float value) { return value == -1 || (std::isfinite(value) && value > 0 && value < 1); };
		if (!phase(entry.authoredImpactPhase) || !phase(entry.authoredEmissionPhase)
		    || entry.loop != (entry.key.action == ActorVisualAction::Idle || entry.key.action == ActorVisualAction::Walk)
		    || (entry.key.action != ActorVisualAction::Attack && entry.authoredImpactPhase != -1)
		    || (entry.key.action != ActorVisualAction::Spell && entry.authoredEmissionPhase != -1)) return false;
		for (const auto *ids : { &entry.attachmentBindings, &entry.deathDroppedAttachmentBindings }) {
			for (std::size_t j = 0; j < ids->size(); ++j) {
				const auto &id = (*ids)[j];
				if (id != storage.bindings[0].bindingId && id != storage.bindings[1].bindingId) return false;
				for (std::size_t k = 0; k < j; ++k) if ((*ids)[k] == id) return false;
			}
		}
		for (std::size_t j = 0; j < i; ++j) if (manifest.actions[j].key == entry.key) return false;
	}
	return true;
}

bool Materials(std::span<const PlayerActorMaterialBinding> materials, const PlayerActorResourceStorage &storage,
    const PlayerActorExpectedResource &body)
{
	if (materials.size() > 257) return false;
	for (std::size_t i = 0; i < materials.size(); ++i) {
		const auto &m = materials[i];
		if (!Text(m.resourceId) || !HashText(m.resourceSha256) || m.identity == 0 || m.revision == 0
		    || m.environment > WarriorEnvironment::Dungeon) return false;
		bool found = m.resourceId == body.resourceId && m.resourceSha256 == body.packageSha256 && m.material == 0;
		for (std::size_t r = 0; r < 2; ++r) {
			if (m.resourceId != storage.bindings[r].resourceId || m.resourceSha256 != storage.rigid[r].packageSha256) continue;
			for (const auto &part : storage.rigid[r].parts) if (part.material == m.material) found = true;
		}
		if (!found) return false;
		for (std::size_t j = 0; j < i; ++j) if (materials[j].resourceId == m.resourceId && materials[j].material == m.material
		    && materials[j].environment == m.environment) return false;
	}
	return true;
}
} // namespace

std::uint64_t PlayerActorResources::generation() const noexcept { return storage_ ? storage_->presentation.loaderGeneration : 0; }
const ActorModel *PlayerActorResources::Body() const noexcept { return storage_ ? &storage_->body : nullptr; }
const ActorRigidResource *PlayerActorResources::Sword() const noexcept { return storage_ ? &storage_->rigid[0] : nullptr; }
const ActorRigidResource *PlayerActorResources::Buckler() const noexcept { return storage_ ? &storage_->rigid[1] : nullptr; }
std::span<const PlayerActorResourceClipRole> PlayerActorResources::ClipLedger() const noexcept { return storage_ ? std::span<const PlayerActorResourceClipRole>(storage_->ledger) : std::span<const PlayerActorResourceClipRole>(); }
std::span<const WarriorAttachmentCoverage> PlayerActorResources::AttachmentBindings() const noexcept { return storage_ ? std::span<const WarriorAttachmentCoverage>(storage_->bindings) : std::span<const WarriorAttachmentCoverage>(); }
std::span<const ActorPlayerAttachmentInput> PlayerActorResources::DiagnosticAttachments() const noexcept { return storage_ ? std::span<const ActorPlayerAttachmentInput>(storage_->frameInputs) : std::span<const ActorPlayerAttachmentInput>(); }
const PlayerActorPresentationResources *PlayerActorResources::PresentationView() const & noexcept { return storage_ ? &storage_->presentation : nullptr; }

bool LoadPlayerActorResourcesDiagnostic(const PlayerActorResourceLoadInput &input,
    PlayerActorResources &output, std::string &error)
{
	if (input.loaderGeneration == 0 || (!output.empty() && input.loaderGeneration <= output.generation()))
		return Fail(error, "Actor loader generation is absent, reused or older than the current output");
	if (!Expected(input.body) || !Expected(input.sword) || !Expected(input.buckler)
	    || input.body.expected.resourceId == input.sword.expected.resourceId || input.body.expected.resourceId == input.buckler.expected.resourceId
	    || input.sword.expected.resourceId == input.buckler.expected.resourceId || input.ledger.size() != Roles.size() || input.bindings.size() != 2)
		return Fail(error, "Actor loader needs bounded exact identities, eleven-role ledger and two distinct bindings");
	for (std::size_t i = 0; i < Roles.size(); ++i) if (input.ledger[i].bundleIndex != i || input.ledger[i].role != Roles[i])
		return Fail(error, "Actor loader ledger does not match the new composed bundle indices");
	try {
		const std::vector<std::byte> bodyBytes(input.body.bytes.begin(), input.body.bytes.end());
		const std::vector<std::byte> swordBytes(input.sword.bytes.begin(), input.sword.bytes.end());
		const std::vector<std::byte> bucklerBytes(input.buckler.bytes.begin(), input.buckler.bytes.end());
		if (PackageHash(bodyBytes) != input.body.expected.packageSha256 || PackageHash(swordBytes) != input.sword.expected.packageSha256
		    || PackageHash(bucklerBytes) != input.buckler.expected.packageSha256)
			return Fail(error, "Actor loader package SHA256 does not match the owned byte snapshot");
		auto next = std::make_shared<PlayerActorResourceStorage>();
		if (!LoadActorModel({ reinterpret_cast<const std::uint8_t *>(bodyBytes.data()), bodyBytes.size() }, next->body, error)
		    || !LoadActorRigidResource(swordBytes, input.sword.expected.packageSha256, next->rigid[0], error)
		    || !LoadActorRigidResource(bucklerBytes, input.buckler.expected.packageSha256, next->rigid[1], error)) return false;
		if (!Identity(next->body, input.body.expected) || !Identity(next->rigid[0], input.sword.expected) || !Identity(next->rigid[1], input.buckler.expected)
		    || next->rigid[0].packageSha256 != input.sword.expected.packageSha256 || next->rigid[1].packageSha256 != input.buckler.expected.packageSha256
		    || next->body.assetId != "tristram.hero.warrior" || next->body.variant != "warrior-light-start-swordshield-r1"
		    || next->body.revision != "warrior-composed-body-r1" || next->body.joints.size() != 52 || next->body.clips.size() != Roles.size()
		    || next->rigid[0].assetId != "tristram.item.short-sword" || next->rigid[1].assetId != "tristram.item.buckler"
		    || next->rigid[0].sourceSha256 != "0204d107bdf5ca131bfe5affb7449d57f292d19cddcd16c9a27ac41ba4a078b8"
		    || next->rigid[1].sourceSha256 != "eb43ee37cd35725f587bc51e958c5c7a8eb14ac6d64a0624c1035a82c093e65a")
			return Fail(error, "Actor loader source/model/item identity is inconsistent with the private starter candidate");
		for (std::size_t i = 0; i < Roles.size(); ++i) if (next->body.clips[i].name != Roles[i])
			return Fail(error, "Actor parser roles do not match the explicit new ledger");
		std::size_t vertices = next->body.vertices.size(), indices = next->body.indices.size();
		if (vertices > MaxVertices || indices > MaxIndices)
			return Fail(error, "Actor loader BODY exceeds the aggregate frame budget");
		for (const auto &resource : next->rigid) for (const auto &part : resource.parts) {
			if (part.vertices.size() > MaxVertices - vertices || part.indices.size() > MaxIndices - indices)
				return Fail(error, "Actor loader aggregate BODY/rigid geometry exceeds the frame budget");
			vertices += part.vertices.size(); indices += part.indices.size();
		}
		for (std::size_t i = 0; i < 2; ++i) {
			const auto &expected = i == 0 ? input.sword.expected : input.buckler.expected;
			if (!Binding(next->body, next->rigid[i], expected, input.bindings[i], i == 0))
				return Fail(error, "Actor loader item/bone/socket declaration is not the exact diagnostic starter binding");
			next->bindings[i] = input.bindings[i];
		}
		if (next->bindings[0].bindingId == next->bindings[1].bindingId)
			return Fail(error, "Actor loader binding ids are ambiguous");
		if (input.diagnosticManifest != nullptr) {
			if (!Manifest(*input.diagnosticManifest, *next, input.body.expected))
				return Fail(error, "Actor loader diagnostic manifest claims/identity do not match the loaded candidate");
			next->manifest = *input.diagnosticManifest;
		}
		if (!Materials(input.materials, *next, input.body.expected))
			return Fail(error, "Actor loader material declarations refer to absent/ambiguous resource parts");
		next->materials.assign(input.materials.begin(), input.materials.end());
		next->ledger.assign(input.ledger.begin(), input.ledger.end());
		for (std::size_t i = 0; i < 2; ++i) {
			const auto &binding = next->bindings[i];
			next->partViews[i] = next->rigid[i].PartViews();
			next->rigidViews[i] = { { binding.resourceId, binding.resourceSha256, binding.item }, next->partViews[i] };
			next->frameInputs[i] = { binding.item.assetId, binding.item.revision, binding.item.sourceSha256, binding.boneName,
				binding.item.nativeItemId, binding.local, next->partViews[i] };
		}
		next->presentation.model = &next->body;
		next->presentation.manifest = next->manifest ? &*next->manifest : nullptr;
		next->presentation.bodyResourceId = input.body.expected.resourceId;
		next->presentation.loadedPackageSha256 = input.body.expected.packageSha256;
		next->presentation.loaderGeneration = input.loaderGeneration;
		next->presentation.attachments = next->rigidViews;
		next->presentation.materials = next->materials;
		// authenticatedPilot remains empty. Diagnostic claims cannot authorize a
		// closed pilot or full gameplay, even if this transport/geometry passes.
		output.storage_ = std::move(next);
		error.clear();
		return true;
	} catch (const std::bad_alloc &) {
		return Fail(error, "Actor loader allocation failed");
	} catch (const std::length_error &) {
		return Fail(error, "Actor loader container length failed");
	}
}

} // namespace devilution
