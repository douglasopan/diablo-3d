#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "engine/render/player_actor_resources.hpp"

namespace devilution {

// Fixed inputs for the explicitly requested private human pilot. These are
// content/authoring identities, not fabricated reviews or coverage. The host
// still owns native timing/combat, resource verification and sprite fallback.
inline constexpr std::string_view PlayerActorHumanPilotProfileId = "warrior-human-pilot-v1";

struct PlayerActorRuntimeResourceProfile {
	std::string_view path;
	std::string_view resourceId;
	std::string_view assetId;
	std::string_view variant;
	std::string_view revision;
	std::string_view sourceSha256;
	std::string_view packageSha256;
	std::size_t packageBytes;

	PlayerActorExpectedResource Expected() const
	{
		return { std::string(resourceId), std::string(assetId), std::string(variant),
			std::string(revision), std::string(sourceSha256), std::string(packageSha256) };
	}
};

// Stable order matches LoadPlayerActorResourcesDiagnostic: BODY, sword, buckler.
inline constexpr std::array<PlayerActorRuntimeResourceProfile, 3> PlayerActorHumanPilotResources {{
	{ "d3d-actors/warrior-body.d3dactor", "warrior-human-pilot-v1/body",
		"tristram.hero.warrior", "warrior-light-start-swordshield-r1", "warrior-composed-body-r1",
		"6b2c8a25f2a12b21f66d7cfb546e5cfa09736dd2b1d4dda15b2218a88920a6a0",
		"48106d5b7d95200f70977a95f34a16d96c8ee0b404bfb354b4c672a3b3d9863d", 13771051 },
	{ "d3d-actors/short-sword.d3d2", "warrior-human-pilot-v1/short-sword",
		"tristram.item.short-sword", "starter-private", "original-short-sword-r1",
		"0204d107bdf5ca131bfe5affb7449d57f292d19cddcd16c9a27ac41ba4a078b8",
		"6a64e13b835e337d7d39c2a9cd2c5462c76c5bb0bfeb667d7a872ec301c35971", 504563 },
	{ "d3d-actors/buckler.d3d2", "warrior-human-pilot-v1/buckler",
		"tristram.item.buckler", "starter-private", "original-buckler-r2",
		"eb43ee37cd35725f587bc51e958c5c7a8eb14ac6d64a0624c1035a82c093e65a",
		"45c84d2aeac7f160e6a4b362ffc91e1b6985276fb4c778cb11f5382be9bc738d", 1189251 },
}};

inline constexpr std::string_view PlayerActorHumanPilotComposedClipMapSha256 =
	"9df113072e5192d646fa417ebbee3cd1b44a2530ba5db3954fec1bcd501852b1";
inline constexpr std::string_view PlayerActorHumanPilotClipLedgerSha256 =
	"de64343b037b760032fe0aa2c6b38842091c8dad94546f6f67ea9abfc026f401";

struct PlayerActorRuntimeClipProfile {
	std::string_view role;
	std::size_t bundleIndex;
	float startTime;
	float endTime;
	float authoredImpactPhase = -1;
	float authoredEmissionPhase = -1;
};

// NEW eleven-clip bundle indices; every separate composed GLB had animation 0.
// Ranges are the actual D3DACTR1 float32 values, not native action durations.
// Only casts declare a visual marker in the pinned runtime GLB's ownSourceAnimation
// metadata. No impact phase was declared there. The native marker remains an
// input from Player capture; these values emit no spell, hit or damage.
inline constexpr std::array<PlayerActorRuntimeClipProfile, 11> PlayerActorHumanPilotClips {{
	{ "death", 0, 0.0F, 2.0F },
	{ "block", 1, 0.0F, 0.20000000298023224F },
	{ "cast-fire", 2, 0.0F, 1.0F, -1.0F, 13.0F / 19.0F },
	{ "cast-lightning", 3, 0.0F, 1.0F, -1.0F, 13.0F / 19.0F },
	{ "cast-magic", 4, 0.0F, 1.0F, -1.0F, 13.0F / 19.0F },
	{ "idle", 5, 0.0F, 2.5F },
	{ "walk", 6, 0.0F, 2.3333332538604736F },
	{ "attack", 7, 0.0F, 2.9583332538604736F },
	{ "hit", 8, 0.0F, 0.75F },
	{ "walk-entry", 9, 0.0F, 2.3333332538604736F },
	{ "walk-exit", 10, 0.0F, 0.5833333134651184F },
}};

inline bool MatchesPlayerActorHumanPilotBodyLayout(const ActorModel &model)
{
	const auto &body = PlayerActorHumanPilotResources[0];
	if (model.assetId != body.assetId || model.variant != body.variant || model.revision != body.revision
	    || model.sourceSha256 != body.sourceSha256 || model.nodes.size() != 68 || model.joints.size() != 52
	    || model.vertices.size() != 13627 || model.indices.size() != 33117 || model.clips.size() != PlayerActorHumanPilotClips.size()
	    || model.textureWidth != 2048 || model.textureHeight != 2048 || model.textureRgb.size() != 2048U * 2048U * 3U)
		return false;
	for (const auto &clip : PlayerActorHumanPilotClips) {
		const auto &loaded = model.clips[clip.bundleIndex];
		if (loaded.name != clip.role || loaded.startTime != clip.startTime || loaded.endTime != clip.endTime || loaded.tracks.size() != 53)
			return false;
	}
	return true; // Metadata only; package bytes must also pass the owned loader.
}

struct PlayerActorRuntimeSocketProfile {
	std::string_view bindingId;
	std::size_t resourceIndex;
	WarriorAttachmentKind kind;
	WarriorSocketHand hand;
	std::string_view bone;
	std::int32_t nativeItemId;
	std::string_view nativeItemSymbol;
	std::string_view socketSourceSha256;
	ActorTransform local;

	WarriorAttachmentCoverage Binding() const
	{
		const auto &resource = PlayerActorHumanPilotResources[resourceIndex];
		WarriorAttachmentCoverage result;
		result.bindingId = bindingId;
		result.kind = kind;
		result.resourceId = resource.resourceId;
		result.resourceSha256 = resource.packageSha256;
		result.boneName = bone;
		result.local = local;
		result.hand = hand;
		result.item.nativeItemId = nativeItemId;
		result.item.nativeItemSymbol = nativeItemSymbol;
		result.item.assetId = resource.assetId;
		result.item.variant = resource.variant;
		result.item.revision = resource.revision;
		result.item.sourceSha256 = resource.sourceSha256;
		result.item.packageSha256 = resource.packageSha256;
		// calibration remains Unreviewed with an empty receipt. This describes
		// the real recipe; it does not manufacture artistic/pilot acceptance.
		return result;
	}
};

// Float32 conversion of the exact frozen right/left socket JSON referenced by
// COMPOSED-CLIP-MAP. Same rig and worldJoint*local transform in every view.
inline constexpr std::array<PlayerActorRuntimeSocketProfile, 2> PlayerActorHumanPilotSockets {{
	{ "warrior-human-pilot-v1/right-sword", 1, WarriorAttachmentKind::Sword, WarriorSocketHand::Right,
		"mixamorig:RightHand", 1, "IDI_WARRIOR",
		"aaa39decfe76dc7de7b42d1e0a4d142e41090aba812a28b52e9fd63cc41a897d",
		{ { -0.004626527338436933F, 0.07166664838453221F, 0.015139352580342625F },
			{ 0.11092371420877929F, -0.13528301899135078F, -0.6343335528273173F, 0.7530042351515613F },
			{ 0.5504108214918582F, 0.5504108214918582F, 0.5504108214918582F } } },
	{ "warrior-human-pilot-v1/left-buckler", 2, WarriorAttachmentKind::Shield, WarriorSocketHand::Left,
		"mixamorig:LeftHand", 2, "IDI_WARRSHLD",
		"70815a34d563267f1ed66938be9c84f04e268ac8952d386a3e8060d6cccc93af",
		{ { -0.026768344615913472F, 0.07093311584053634F, 0.04824623237044686F },
			{ 0.01410490115818619F, -0.28424469601123303F, -0.024063744247310736F, 0.9583459400266688F },
			{ 0.5504109427301928F, 0.5504109427301928F, 0.5504109427301928F } } },
}};

inline std::array<WarriorAttachmentCoverage, 2> PlayerActorHumanPilotBindings()
{
	return { PlayerActorHumanPilotSockets[0].Binding(), PlayerActorHumanPilotSockets[1].Binding() };
}

enum class PlayerActorRuntimeMaterialPayload : std::uint8_t { DiffuseRgb, RigidPartitionsColor0 };
struct PlayerActorRuntimeMaterialProfile {
	std::size_t resourceIndex;
	PlayerActorRuntimeMaterialPayload payload;
	std::uint32_t textureWidth, textureHeight;
	std::uint32_t sourceMaterialCount;
};

// All material data lives inside the pinned packages; no extra PNG path exists.
// BODY is explicit diffuse-only RGB (baseColorFactor already applied), opaque.
// Equipment has real material partitions/COLOR_0 and no encoded image textures.
// Normal/PBR/tangent fidelity is not implied by this transport description.
inline constexpr std::array<PlayerActorRuntimeMaterialProfile, 3> PlayerActorHumanPilotMaterials {{
	{ 0, PlayerActorRuntimeMaterialPayload::DiffuseRgb, 2048, 2048, 1 },
	{ 1, PlayerActorRuntimeMaterialPayload::RigidPartitionsColor0, 0, 0, 6 },
	{ 2, PlayerActorRuntimeMaterialPayload::RigidPartitionsColor0, 0, 0, 4 },
}};

} // namespace devilution
