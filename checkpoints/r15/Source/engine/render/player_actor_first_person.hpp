#pragma once

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "engine/clx_sprite.hpp"
#include "engine/render/actor_player_frame.hpp"

namespace devilution {

enum class PlayerActorHand : uint8_t { Left, Right };

/** Caller captures these from the actual equipped InvBody slot, never from
 * HoldItem, a dropped item or a weapon category. Empty/unusable slots attach
 * nothing. inventoryCursor is the exact native item._iCurs, without the
 * CURSOR_FIRSTITEM offset. No equipment/stat calculation happens here. */
struct PlayerActorEquippedHand {
	PlayerActorHand hand = PlayerActorHand::Right;
	int nativeSlot = -1;
	int nativeItemId = -1;
	int inventoryCursor = -1;
	bool occupied = false;
	bool usable = true;
};

/** Same model's calibrated hand socket. local is in authored rig units; the
 * caller provides the explicit uniform body fit already used by posedFrame. */
struct PlayerActorFirstPersonSocket {
	PlayerActorHand hand = PlayerActorHand::Right;
	std::string bone;
	ActorTransform local;
};

struct PlayerActorFirstPersonConfig {
	std::string leftForeArm = "mixamorig:LeftForeArm";
	std::string rightForeArm = "mixamorig:RightForeArm";
	ActorPlayerFit fit;
	float nativeYawRadians = 0;
	ActorVec3 authoritativeFootpoint;
};

/** Borrowed exact inventory sprite. The renderer resolves
 * GetInvItemSprite(item._iCurs + CURSOR_FIRSTITEM), and supplies an explicit
 * grip pivot measured in pixels from its top-left and pixel-to-rig scale.
 * Transparent padding is retained; no alpha crop or category substitution. */
struct PlayerActorInventorySpriteSource {
	std::optional<ClxSprite> sprite;
	ActorVec2 pivotPixels;
	float rigUnitsPerPixel = 0;
};
using PlayerActorInventorySpriteResolver = std::function<std::optional<PlayerActorInventorySpriteSource>(const PlayerActorEquippedHand &)>;

struct PlayerActorFirstPersonRigidAttachment {
	PlayerActorEquippedHand equipment;
	/** Index into the same posedFrame.attachments; geometry/socket remain exact. */
	size_t attachmentIndex = 0;
};

struct PlayerActorInventorySpriteAttachment {
	PlayerActorEquippedHand equipment;
	std::optional<ClxSprite> sprite;
	ActorVec2 pivotPixels;
	float rigUnitsPerPixel = 0;
	ActorMatrix worldMatrix;
	/** Top-left, top-right, bottom-right, bottom-left; full sprite UVs. */
	std::array<ActorPlayerGeometryVertex, 4> quad;
};

/** Winding agrees with the socket quad's authored +Z normal. */
inline constexpr std::array<uint32_t, 6> PlayerActorInventorySpriteQuadIndices { 0, 2, 1, 0, 3, 2 };

/** Indices borrow posedFrame.body.vertices; no re-skinning or second rig.
 * Rigid indices borrow the same posedFrame attachments. Native sprite bytes
 * remain owned by the caller's inventory resources. Keep all three alive
 * through draw/replay. No world depth, picking or native state is changed. */
struct PlayerActorFirstPersonFrame {
	std::vector<uint32_t> leftArmIndices;
	std::vector<uint32_t> rightArmIndices;
	std::vector<PlayerActorFirstPersonRigidAttachment> rigidAttachments;
	std::vector<PlayerActorInventorySpriteAttachment> spriteAttachments;
};

/** Shared TP/FP bridge. Reuses the hand node matrices of the already sampled
 * frame, then applies the exact fit/native placement. Does not sample a pose.
 * Failure is atomic; quad normal uses the socket inverse-transpose. */
bool BuildPlayerActorInventorySpriteQuad(const ActorModel &model, const ActorPlayerFrame &posedFrame,
	const PlayerActorEquippedHand &equipment, const PlayerActorFirstPersonSocket &socket,
	const PlayerActorInventorySpriteSource &source, const PlayerActorFirstPersonConfig &config,
	PlayerActorInventorySpriteAttachment &output, std::string &error);

/** Pure bounded presentation selection. Descendant joints of each ForeArm
 * classify a vertex when their total weight >= .5. A triangle is admitted only
 * when all three source vertices belong to that same side; indices reference
 * the unchanged posed body. Exact nativeItemId + hand bone selects an existing
 * posed rigid attachment, otherwise the exact inventory sprite resolver is
 * required. Empty slots produce no attachment. No clock/events/gameplay writes.
 * A renderer may apply one additional camera-relative visual transform to the
 * selected arms AND all their attachments together after this helper returns. */
bool PreparePlayerActorFirstPerson(const ActorModel &model, const ActorPlayerFrame &posedFrame,
	std::span<const PlayerActorEquippedHand> equipment,
	std::span<const PlayerActorFirstPersonSocket> sockets,
	const PlayerActorInventorySpriteResolver &spriteResolver, const PlayerActorFirstPersonConfig &config,
	PlayerActorFirstPersonFrame &output, std::string &error);

} // namespace devilution
