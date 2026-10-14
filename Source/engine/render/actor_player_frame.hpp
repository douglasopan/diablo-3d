#pragma once

#include <span>
#include "engine/render/actor_player_geometry.hpp"

namespace devilution {

struct ActorRigidVertex {
    ActorVec3 position, normal;
    ActorVec2 uv;
    // Authored glTF linear vertex color; kept separate from material factors.
    ActorVec4 color { 1, 1, 1, 1 };
};

// Borrowed only while preparing. A part keeps its own material identity; no
// flattening of distinct authored materials into one texture is performed.
struct ActorRigidPartInput {
    std::uint32_t material = 0;
    std::span<const ActorRigidVertex> vertices;
    std::span<const std::uint32_t> indices;
};

struct ActorPlayerAttachmentInput {
    std::string assetId, revision, sourceSha256, bone;
    std::int32_t nativeItemId = -1;
    ActorTransform local;
    std::span<const ActorRigidPartInput> parts;
};

struct ActorPlayerAttachmentPart {
    std::uint32_t material = 0;
    std::vector<ActorPlayerGeometryVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct ActorPlayerAttachmentGeometry {
    std::string assetId, revision, sourceSha256, bone;
    std::int32_t nativeItemId = -1;
    ActorMatrix worldMatrix {};
    std::vector<ActorPlayerAttachmentPart> parts;
};

// Owned, atomic preparation of body and all currently admitted attachments.
// Empty attachments is a real body-only presentation, e.g. a native gear drop;
// this helper never decides whether dropping or retaining equipment is allowed.
struct ActorPlayerFrame {
    ActorPlayerGeometry body;
    std::vector<ActorPlayerAttachmentGeometry> attachments;
};

// Samples the body once; every socket uses those exact authored node matrices.
// Caller must first authenticate equipment/disposition, reviewed coverage,
// material/texture bindings, selection and lifecycle. This is not admission or
// a renderer hook. On any part failure, the complete prior output is unchanged.
bool PrepareActorPlayerFrame(const ActorModel &model, const PlayerActorCapture &capture,
    std::uint64_t liveLevelEpoch, std::uint32_t expectedNativeIndex, std::size_t clip,
    const ActorPlayerPhaseBinding &binding, const ActorPlayerFit &fit,
    std::span<const ActorPlayerAttachmentInput> attachments,
    ActorPlayerFrame &output, std::string &error);

} // namespace devilution
