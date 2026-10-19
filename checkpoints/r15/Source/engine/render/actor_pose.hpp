#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "engine/render/actor_model.hpp"

namespace devilution {

/** Pure visual output in the asset's world space, without actor placement.
 * The caller supplies native footpoint/facing and any explicit root-motion
 * correction later. Evaluating a pose never changes the model or simulation.
 */
struct ActorPose {
	std::vector<ActorMatrix> worldNodes;
	std::vector<ActorMatrix> jointPalette;
	std::vector<ActorVec3> positions;
	std::vector<ActorVec3> normals;
};

/** Evaluate a validated ActorModel at an explicitly selected clip time.
 * Time is clamped to the clip's authored range. STEP and LINEAR are supported;
 * rotations use normalized shortest-arc slerp. This function does not loop,
 * advance time, cancel root motion or emit any gameplay/audio event.
 * Failure leaves output unchanged and supplies a diagnostic in error.
 */
bool EvaluateActorPose(const ActorModel &model, size_t clip, float seconds,
    ActorPose &output, std::string &error);

/** Evaluate rest TRS with the same worldJoint * inverseBind skinning path.
 * Normals use each palette matrix's inverse-transpose before weighted blending
 * and normalization. Failure is atomic, as for EvaluateActorPose.
 */
bool EvaluateActorBindPose(const ActorModel &model, ActorPose &output, std::string &error);

} // namespace devilution
