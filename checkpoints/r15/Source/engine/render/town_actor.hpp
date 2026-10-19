#pragma once

#include <array>

#include "engine/render/town_volume.hpp"

namespace devilution {

/** Reconstruct a physical volume from eight native directions of one pose/frame.
 *
 * Input order is Direction: South, SouthWest, West, NorthWest, North, NorthEast,
 * East, SouthEast. forwardView is the direction currently drawn by the game.
 * Returned +X is image-right in that reference, +height is vertical, and +Z is
 * horizontal toward the native camera. Use the fixed world basis
 * X=(1,0,-1)/sqrt(2), Z=(1,0,1)/sqrt(2), WITHOUT adding Z to world height.
 *
 * Before grounding, view j has theta=(j-forwardView)*pi/4 and projects as:
 * px=width/2+32*sqrt(2)*(cos(theta)*x-sin(theta)*z)
 * py=height-1+16*sqrt(2)*(sin(theta)*x+cos(theta)*z)-32*worldHeight.
 * Each triangle has UVs for views[triangle.textureView]. SpriteFront means a
 * patch of that selected native view, including actual back/side directions.
 * Its paletteIndex is an opaque color fallback, not an alpha/color-zero test.
 * The complete reconstructed body is translated by height=-minimumHeight and
 * z=-sqrt(2)*minimumHeight, placing its lowest surface at physical height zero.
 * This rigid translation preserves the native forward image; original UVs are
 * unchanged. Returned width/height/depth are extent spans after calibration.
 *
 * A cell must stay inside the forward silhouette, meet minimumViews votes,
 * and agree with at least one image in every pair of opposing directions.
 * The native lower exterior shadow is excluded using TownActorBodyOpacity;
 * the caller can render that removed opacity separately on the ground.
 * Missing/malformed views return an empty mesh instead of inventing directions.
 * No caller pixels, animation, camera, or simulation state is modified.
 */
TownVolumeMesh BuildTownActorVisualHull(const std::array<TownVolumeSprite, 8> &views,
	int forwardView, int pixelStep = 2, int minimumViews = 6);

} // namespace devilution
