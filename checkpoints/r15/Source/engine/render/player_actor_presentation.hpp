#pragma once

#include <optional>
#include "engine/render/actor_player_frame.hpp"
#include "engine/render/player_actor_lifecycle.hpp"

namespace devilution {
struct Player;

// Immutable resources authenticated by the asset/approval loader. Strings here
// are loaded-resource witnesses, not user-supplied native-state fingerprints.
struct PlayerActorRigidResource {
	WarriorVerifiedAttachment verified;
	std::span<const ActorRigidPartInput> parts;
};
struct PlayerActorMaterialBinding {
	std::string resourceId, resourceSha256;
	std::uint32_t material = 0;
	std::uint64_t identity = 0, revision = 0;
	WarriorEnvironment environment = WarriorEnvironment::Unknown;
	bool operator==(const PlayerActorMaterialBinding &) const = default;
};
struct PlayerActorPresentationResources {
	const ActorModel *model = nullptr;
	const WarriorCoverageManifest *manifest = nullptr;
	std::string bodyResourceId, loadedPackageSha256;
	WarriorPilotAuthorization authenticatedPilot;
	// Loader changes this on any package/manifest/rigid-resource mutation. Those
	// verified inputs stay immutable until replacement; resources are borrowed
	// only during preparation/freshness checks, never by the resulting geometry.
	std::uint64_t loaderGeneration = 0;
	std::span<const PlayerActorRigidResource> attachments;
	std::span<const PlayerActorMaterialBinding> materials;
};
struct PlayerActorVisualContext {
	WarriorWorldIdentity world;
	std::uint32_t nativeIndex = 0;
	ActorVec3 authoritativeFootpoint;
	std::uint8_t retainedInterpolation128 = 0;
	bool paused = false, localPlayer = false, hidden = false, nativeVisible = true;
	// The renderer freezes these native visibility/light/TRN/pick facts for one
	// transaction. They cannot authorize native equipment or art selection.
	std::uint64_t frameSerial = 0, resourceGeneration = 0, cameraRevision = 0, paletteRevision = 0;
	std::uint64_t translationIdentity = 0;
	std::uint8_t nativeLight = 0;
	bool selectable = false;
};
enum class PlayerActorPresentationResult : std::uint8_t { Refused, Prepared };
struct PlayerActorPreparedPresentation {
	PlayerActorLifecycleSnapshot lifecycle;
	PlayerActorCapture capture;
	WarriorActorAdmission admission;
	WarriorPresentationSource source = WarriorPresentationSource::Unspecified;
	PlayerActorVisualContext visual;
	ActorPlayerFit fit;
	ActorPlayerFrame geometry;
	// Resolved material identities are owned; geometry preserves authored part
	// boundaries. The body has one material; attachmentMaterials[i][j] indexes
	// materials for geometry.attachments[i].parts[j]. No material flattening.
	std::vector<PlayerActorMaterialBinding> materials;
	std::size_t bodyMaterial = 0;
	std::vector<std::vector<std::size_t>> attachmentMaterials;
};

class PlayerActorPresentation {
public:
	PlayerActorPresentationResult Prepare(const Player &player, const PlayerActorLifecycle &lifecycle,
	    const PlayerActorPresentationResources &resources, const PlayerActorVisualContext &visual,
	    const ActorPlayerFit &fit, WarriorAdmissionScope scope, std::string &error);
	// Read-only native capture comparison; never EvaluateActorPose/resample skin
	// or advance a native clock. Only this guarded accessor exposes a payload.
	const PlayerActorPreparedPresentation *Current(const Player &player, const PlayerActorLifecycle &lifecycle,
	    const PlayerActorPresentationResources &resources, const PlayerActorVisualContext &visual,
	    const ActorPlayerFit &fit, WarriorAdmissionScope scope, std::string &error) const;
	void Invalidate() noexcept { prepared_.reset(); }
	std::uint64_t preparationCount() const noexcept { return preparationCount_; }
private:
	struct PartStamp {
		std::uint32_t material;
		std::uintptr_t vertices, indices;
		std::size_t vertexCount, indexCount;
		bool operator==(const PartStamp &) const = default;
	};
	struct RigidStamp {
		std::string id, hash;
		WarriorItemIdentity item;
		std::vector<PartStamp> parts;
		bool operator==(const RigidStamp &) const = default;
	};
	struct ResourceStamp {
		std::uintptr_t model, manifest;
		std::string bodyId, package;
		WarriorPilotAuthorization authorization;
		std::uint64_t generation;
		std::vector<RigidStamp> rigid;
		std::vector<PlayerActorMaterialBinding> materials;
		bool operator==(const ResourceStamp &) const = default;
	};
	struct NativeStamp {
		int tileX, tileY, hitPoints, spellId, spellType, spellFrom, spellLevel;
		bool invincible;
		bool operator==(const NativeStamp &) const = default;
	};
	static ResourceStamp StampResources(const PlayerActorPresentationResources &resources);
	static NativeStamp StampNative(const Player &player);
	ResourceStamp resourceStamp_ {};
	NativeStamp nativeStamp_ {};
	std::optional<PlayerActorPreparedPresentation> prepared_;
	std::uint64_t preparationCount_ = 0;
};
} // namespace devilution
