#pragma once

#include "engine/render/actor_player_frame.hpp"
#include "engine/render/town_model_v2.hpp"

namespace devilution {

struct ActorRigidResourcePart {
    std::uint32_t material = 0;
    std::vector<ActorRigidVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<ActorVec4> authoredTangents;
    // Exact correspondence to the parsed source, including color/UV seams.
    std::vector<std::uint32_t> sourceVertices;
};

// D3DMESH2 remains the static transport. This adapter owns the bounded parts
// needed by the animated frame, without changing authored coordinates/anchor,
// reducing geometry, flattening materials or applying scenery placement.
struct ActorRigidResource {
    std::string assetId, variant, revision, sourceSha256, packageSha256;
    std::vector<ActorRigidResourcePart> parts;
    std::vector<TownModelV2Material> materials;
    std::vector<TownModelV2Texture> textures;
    std::uint32_t tangentMode = 0;

    // Borrowed only until PrepareActorPlayerFrame finishes. A resource must
    // remain immutable and alive while the loader exposes these views.
    std::vector<ActorRigidPartInput> PartViews() const;
};

// Parses exact expected bytes first. Native item identity/socket/review and
// renderer material support are separate loader/admission responsibilities.
// Alpha modes/PBR metadata are preserved, not implicitly declared renderable.
// Failure leaves output unchanged; one resource has <=32 parts and the same
// 100000-vertex/300000-index bounds as the aggregate frame (host rechecks sum).
bool LoadActorRigidResource(std::span<const std::byte> bytes, std::string_view expectedSha256,
    ActorRigidResource &output, std::string &error);

} // namespace devilution
