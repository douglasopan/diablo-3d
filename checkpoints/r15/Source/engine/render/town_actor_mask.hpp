#pragma once

#include <cstdint>
#include <vector>

#include "engine/render/town_volume.hpp"

namespace devilution {

/** Copy the decoded actor opacity, separating native ground shadow pixels.
 * The native actor outline renderer treats palette index zero as shadow. Here
 * only zero pixels reachable from the exterior through transparent/zero pixels
 * and in the lower half of the colored body's bounds are removed. Isometric
 * ground shadow can reach above the lowest foot row. Enclosed black paint and
 * upper body black remain opaque. Original pixels and opacity are never changed.
 * Renderer can draw originalOpacity && !returnedOpacity as a ground decal.
 * Invalid/truncated sprites or dimensions outside 1..512 return an empty mask.
 * A sprite without any opaque nonzero paint is preserved as an unknown shape.
 */
std::vector<uint8_t> TownActorBodyOpacity(const TownVolumeSprite &sprite);

} // namespace devilution
