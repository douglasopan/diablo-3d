#pragma once

#include "engine/render/town_volume.hpp"

namespace devilution {

/** Infer a rounded humanoid body from a single native sprite: distinct head,
 * torso, arms, separated legs or a skirt, and detached accessories. Coordinates
 * have horizontal depth (+z faces the native camera), without height shear;
 * feet touch height zero by rigid translation along the native viewing ray.
 * The authored front uses physical native
 * isometric UVs. Hidden surfaces have opaque colors sampled from that body part.
 * A single view cannot determine true anatomy. Broad/non-humanoid or degenerate
 * silhouettes retain the closed native silhouette relief as an explicit fallback.
 * Pass humanoid=false for known non-human entities (in particular cows); a tall
 * front/rear animal silhouette cannot be classified reliably from opacity alone.
 * Caller caches by sprite/frame identity; no simulation state is changed. */
TownVolumeMesh BuildTownActorSingleViewBody(const TownVolumeSprite &sprite, bool humanoid = true);

} // namespace devilution
