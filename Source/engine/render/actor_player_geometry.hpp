#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "engine/render/actor_player_binding.hpp"
#include "engine/render/player_actor_capture.hpp"

namespace devilution {

struct ActorPlayerFit {
    ActorVec3 authoredFootpoint;
    float uniformScale = 1;
    float authoredYawOffset = 0;
};

struct ActorPlayerGeometryVertex {
    ActorVec3 worldPosition;
    ActorVec3 worldNormal;
    ActorVec2 uv;
    ActorVec4 authoredColor { 1, 1, 1, 1 };
};

// Owned frame payload for projected triangle submission. It never uses the
// immutable static-mesh cache. A later renderer must still admit equipment,
// visibility, palette/material, selection and picking before drawing it.
struct ActorPlayerGeometry {
    std::string assetId, variant, revision, sourceSha256;
    std::uint64_t levelEpoch = 0;
    std::uint32_t nativeIndex = 0;
    std::size_t clip = 0;
    float sampleSeconds = 0;
    std::vector<ActorPlayerGeometryVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Same evaluated pose, still in authored space, for rigid attachments.
    std::vector<ActorMatrix> authoredWorldNodes;
};

// Pure preparation: sample using the captured native fraction, then apply an
// explicit uniform fit, asset footpoint and native facing/placement. No floor
// lift, movement, camera-facing rotation, texture mapping or simulation events.
// Failure leaves the owned output unchanged. Caller owns fresh capture/player
// activation identity, level lifecycle and complete presentation admission.
bool PrepareActorPlayerGeometry(const ActorModel &model, const PlayerActorCapture &capture,
    std::uint64_t liveLevelEpoch, std::uint32_t expectedNativeIndex, std::size_t clip,
    const ActorPlayerPhaseBinding &binding, const ActorPlayerFit &fit,
    ActorPlayerGeometry &output, std::string &error);

} // namespace devilution
