#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/render/town_scene.hpp"

namespace devilution {

/** Native orthographic reference visibility, independent of artwork colors.
 * Texel centers are (x+.5,y+.5); coordinates match the cropped native artwork.
 * The greatest x+z+height is nearest along the original (1,1,1) reference ray.
 * All model surfaces are opaque occluders, including interior/underside faces;
 * only Exterior && nativeProjection surfaces can own projected paint.
 * Result is a local triangle index, or -1 for empty/ineligible/occluded paint.
 * Additional scene models can occlude the target; its own address is skipped.
 * Dimensions must be 1..4096 with at most 8 million texels, otherwise empty.
 */
std::vector<int32_t> BuildTownSceneProjectionOwners(const TownSceneModel &model,
	Point referenceTile, Point pixelOrigin, int width, int height,
	std::span<const TownSceneModel> occluders = {});

/** Accept the actual owner or a same-material coplanar triangle of that face.
 * This removes artificial texture-mask seams across a triangulated quad.
 * Opposite normals, different materials, displaced planes and hidden/interior
 * faces never match. Both triangle indices belong to the target model.
 */
bool TownSceneProjectionOwnerMatches(const TownSceneModel &model,
	int32_t owner, size_t triangleIndex);

} // namespace devilution
