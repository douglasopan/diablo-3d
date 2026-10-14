#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include "engine/render/actor_pose.hpp"
#include "engine/render/actor_visual_snapshot.hpp"

namespace devilution {

// Pure sampling/attachment helpers. Caller owns native state, pose identity,
// level epoch and resource lifetime. No clock, damage event or RNG is advanced.
struct ActorPlayerPhaseBinding {
    bool loop = false;
    // A visual marker only. Native combat continues to own whether a hit occurs.
    float authoredImpactPhase = -1;
    // Visual cast gesture anchor; the native spell remains the sole emitter.
    float authoredEmissionPhase = -1;
    // Optional fraction from the native rendered-frame adapter. Distribution
    // can display recovery from a previous attack while logicalFrame restarts.
    // Never invent that fraction from a wall clock or ordinary tick delay.
    float nativeRenderedSubframe = -1;
};

inline bool ActorPlayerClipTime(const ActorModel &model, const ActorVisualSnapshot &snapshot,
    std::size_t clipIndex, const ActorPlayerPhaseBinding &binding, float &output, std::string &error)
{
    const auto fail = [&error](const char *message) { error = message; return false; };
    if (snapshot.assetId != model.assetId || snapshot.variant != model.variant
        || snapshot.revision != model.revision || snapshot.sourceSha256 != model.sourceSha256)
        return fail("Actor phase belongs to another asset revision");
    if (snapshot.kind != ActorVisualKind::Player || snapshot.sequenceLength != 0 || snapshot.reversed)
        return fail("Player binding cannot sample an NPC/reversed sequence");
    if (clipIndex >= model.clips.size() || snapshot.frameCount == 0
        || snapshot.ticksPerFrame == 0 || snapshot.tickCounter >= snapshot.ticksPerFrame
        || snapshot.progressToNextTick128 > 128 || snapshot.logicalFrame < -8
        || snapshot.logicalFrame >= static_cast<int>(snapshot.frameCount)
        || snapshot.displayedFrame < 0 || snapshot.displayedFrame >= static_cast<int>(snapshot.frameCount))
        return fail("Invalid native actor phase");
    const auto &clip = model.clips[clipIndex];
    if (!std::isfinite(clip.startTime) || !std::isfinite(clip.endTime) || clip.endTime < clip.startTime)
        return fail("Invalid authored clip range");
    double fraction = snapshot.displayedFrame == snapshot.logicalFrame
        ? (snapshot.tickCounter + snapshot.progressToNextTick128 / 128.0) / snapshot.ticksPerFrame : 0;
    if (binding.nativeRenderedSubframe != -1) {
        if (!std::isfinite(binding.nativeRenderedSubframe) || binding.nativeRenderedSubframe < 0 || binding.nativeRenderedSubframe > 1)
            return fail("Invalid native rendered-frame fraction");
        fraction = binding.nativeRenderedSubframe;
    }
    const double frame = snapshot.displayedFrame + fraction;
    const double denominator = binding.loop ? snapshot.frameCount : std::max(snapshot.frameCount - 1U, 1U);
    double phase = std::clamp(frame / denominator, 0.0, 1.0);
    if (!binding.loop && snapshot.terminalHold) phase = 1;
    if (binding.authoredImpactPhase != -1 && binding.authoredEmissionPhase != -1)
        return fail("Cannot anchor attack impact and spell emission together");
    const bool emission = binding.authoredEmissionPhase != -1;
    const float authoredMarker = emission ? binding.authoredEmissionPhase : binding.authoredImpactPhase;
    const int markerFrame = emission ? snapshot.nativeEmissionFrame : snapshot.nativeImpactFrame;
    if (authoredMarker != -1) {
        const bool matchingAction = emission ? snapshot.action == ActorVisualAction::Spell
            : snapshot.action == ActorVisualAction::Attack || snapshot.action == ActorVisualAction::RangedAttack;
        if (!matchingAction || binding.loop || !std::isfinite(authoredMarker)
            || authoredMarker <= 0 || authoredMarker >= 1
            || markerFrame <= 0
            || markerFrame >= static_cast<int>(snapshot.frameCount - 1U))
            return fail("Invalid visual impact mapping");
        const double nativeMarker = markerFrame / denominator;
        phase = phase <= nativeMarker
            ? phase / nativeMarker * authoredMarker
            : authoredMarker + (phase - nativeMarker) / (1 - nativeMarker) * (1 - authoredMarker);
    }
    const float time = static_cast<float>(clip.startTime + phase * (clip.endTime - clip.startTime));
    if (!std::isfinite(time)) return fail("Nonfinite visual sample time");
    output = time;
    error.clear();
    return true;
}

inline bool ActorPlayerAttachment(const ActorModel &model, const ActorPose &pose,
    std::string_view boneName, const ActorTransform &local, ActorMatrix &output, std::string &error)
{
    const auto fail = [&error](const char *message) { error = message; return false; };
    if (pose.worldNodes.size() != model.nodes.size()) return fail("Attachment pose node count mismatch");
    std::size_t node = model.nodes.size();
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].name != boneName) continue;
        if (node != model.nodes.size()) return fail("Ambiguous attachment bone");
        node = i;
    }
    if (node == model.nodes.size() || std::none_of(model.joints.begin(), model.joints.end(),
            [node](const ActorJoint &joint) { return joint.node == node; }))
        return fail("Attachment bone is not in the skin rig");
    const auto &q = local.rotation;
    const float s = local.scale.x;
    if (!std::isfinite(s) || s <= 0 || local.scale.y != s || local.scale.z != s
        || !std::isfinite(local.translation.x) || !std::isfinite(local.translation.y) || !std::isfinite(local.translation.z)
        || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)
        || std::abs(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w-1) > .001F)
        return fail("Attachment requires finite TRS, uniform positive scale and unit quaternion");
    const ActorMatrix offset {
        s*(1-2*(q.y*q.y+q.z*q.z)), s*2*(q.x*q.y+q.z*q.w), s*2*(q.x*q.z-q.y*q.w), 0,
        s*2*(q.x*q.y-q.z*q.w), s*(1-2*(q.x*q.x+q.z*q.z)), s*2*(q.y*q.z+q.x*q.w), 0,
        s*2*(q.x*q.z+q.y*q.w), s*2*(q.y*q.z-q.x*q.w), s*(1-2*(q.x*q.x+q.y*q.y)), 0,
        local.translation.x, local.translation.y, local.translation.z, 1,
    };
    ActorMatrix result {};
    const auto &parent = pose.worldNodes[node];
    if (std::abs(parent[3]) > .00001F || std::abs(parent[7]) > .00001F
        || std::abs(parent[11]) > .00001F || std::abs(parent[15]-1) > .00001F)
        return fail("Attachment parent must be affine");
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            double value = 0;
            for (std::size_t k = 0; k < 4; ++k) value += static_cast<double>(parent[k*4+row]) * offset[column*4+k];
            result[column*4+row] = static_cast<float>(value);
            if (!std::isfinite(value) || !std::isfinite(result[column*4+row])) return fail("Nonfinite attachment matrix");
        }
    }
    output = result;
    error.clear();
    return true;
}

} // namespace devilution
