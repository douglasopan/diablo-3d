#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "engine/render/actor_attachment_resource.hpp"
#include "engine/render/player_actor_presentation.hpp"

namespace devilution {

// The source identity describes the authored/conversion input. The package
// identity authenticates the actual bytes passed to this loader. They are never
// interchangeable and neither identity is an art/coverage authorization.
struct PlayerActorExpectedResource {
	std::string resourceId;
	std::string assetId, variant, revision;
	std::string sourceSha256, packageSha256;
};

struct PlayerActorResourcePacket {
	std::span<const std::byte> bytes;
	PlayerActorExpectedResource expected;
};

struct PlayerActorResourceClipRole {
	std::string role;
	std::size_t bundleIndex = 0;
};

struct PlayerActorResourceLoadInput {
	PlayerActorResourcePacket body, sword, buckler;
	// Explicit NEW eleven-clip ledger, ordered by native bundle index. No role
	// is inferred from a prior ten-clip bundle or converted into native coverage.
	std::span<const PlayerActorResourceClipRole> ledger;
	// Exactly sword/right then buckler/left, including real item identities and
	// the calibrated float32 socket TRS from COMPOSED-CLIP-MAP. Copied as given.
	std::span<const WarriorAttachmentCoverage> bindings;
	std::uint64_t loaderGeneration = 0;
	// Optional caller declarations, never fabricated. This diagnostic loader
	// rejects review levels above DiagnosticValidated and any pilot credentials.
	const WarriorCoverageManifest *diagnosticManifest = nullptr;
	std::span<const PlayerActorMaterialBinding> materials;
};

struct PlayerActorResourceStorage;

// An owned handle to a frozen heap allocation. Copies retain ownership; moves
// preserve the addresses of every model/part/material/view. All views borrow
// from this storage: keep a handle alive while using them, including across
// replacement of another handle. No mutable resource access is exposed.
class PlayerActorResources {
public:
	bool empty() const noexcept { return !storage_; }
	std::uint64_t generation() const noexcept;
	const ActorModel *Body() const noexcept;
	const ActorRigidResource *Sword() const noexcept;
	const ActorRigidResource *Buckler() const noexcept;
	std::span<const PlayerActorResourceClipRole> ClipLedger() const noexcept;
	std::span<const WarriorAttachmentCoverage> AttachmentBindings() const noexcept;
	// Direct geometry gate inputs. These bytes/sockets are mechanically checked;
	// obtaining these views grants no permission to replace native sprites.
	std::span<const ActorPlayerAttachmentInput> DiagnosticAttachments() const noexcept;
	const PlayerActorPresentationResources *PresentationView() const & noexcept;
	const PlayerActorPresentationResources *PresentationView() const && = delete;
	bool diagnosticOnly() const noexcept { return true; }

private:
	std::shared_ptr<const PlayerActorResourceStorage> storage_;
	friend bool LoadPlayerActorResourcesDiagnostic(const PlayerActorResourceLoadInput &,
	    PlayerActorResources &, std::string &);
};

// Pure, bounded loader for the private Warrior light/starter SwordShield BODY
// and its two real rigid resources. No I/O, globals, RNG, decoder, renderer or
// event. Caller keeps input bytes/declarations stable during this call. Parsers
// consume owned byte snapshots; parsed resources/declarations are fully owned.
// Generation must be nonzero and greater than the output's current generation.
// Failure leaves the complete previous owner/views unchanged. Success never
// creates reviews/coverage/PilotAuthorization or native-world acceptance.
bool LoadPlayerActorResourcesDiagnostic(const PlayerActorResourceLoadInput &input,
    PlayerActorResources &output, std::string &error);

} // namespace devilution
