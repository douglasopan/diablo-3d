#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace devilution {

/** Caller-owned, decoded native sprite. Opacity is explicit: black is a color. */
struct TownVolumeSprite {
	int width = 0;
	int height = 0;
	std::span<const uint8_t> pixels;
	std::span<const uint8_t> opacity;
};

enum class TownVolumeMaterial : uint8_t {
	SpriteFront, // UVs address only the corresponding patch of the original sprite.
	SpriteSide,  // Opaque paletteIndex; never repeat the complete front image.
	SpriteBack,  // Opaque paletteIndex; caller may shade its actual surface normal.
	Bark,
	Branch,
	Foliage,
};

/** Fixed local frame: +x image-right, +height up, +z toward the native camera.
 * Rotate this frame with the entity, never with the viewing camera. The native
 * world basis is x=(1,0,-1)/sqrt(2), z=(1,0,1)/sqrt(2). */
struct TownVolumeVertex {
	float x;
	float height;
	float z;
	float u;
	float v;
};

struct TownVolumeTriangle {
	std::array<TownVolumeVertex, 3> vertices;
	TownVolumeMaterial material;
	uint8_t paletteIndex = 0;
	uint8_t textureView = 0; // Native direction image for SpriteFront; legacy meshes use view 0.
};

struct TownVolumeMesh {
	std::vector<TownVolumeTriangle> triangles;
	float width = 0;
	float height = 0;
	float depth = 0;
};

/** Closed, rounded silhouette envelope with separated limbs and body-scaled
 * depth. Caller caches by native sprite frame/direction; there is no global
 * cache or simulation state. pixelStep trades contour detail for triangle count. */
TownVolumeMesh BuildTownActorVolume(const TownVolumeSprite &sprite, int pixelStep = 2);

/** Closed relief for native rocks and other nonliving props. Rounded depth is
 * fitted to the painted row width and full silhouette height, without actor
 * anatomy or shadow masking. Uses the same native-ray frame/UVs as ActorVolume. */
TownVolumeMesh BuildTownPropVolume(const TownVolumeSprite &sprite, int pixelStep = 4);

/** Real tapered tubes, branch forks and optional closed foliage clusters fitted
 * to native sprite extents. Each tube is closed, including its end caps; forks
 * intersect their parent volume. Seed only supplies deterministic depth/bend. */
TownVolumeMesh BuildTownTreeVolume(const TownVolumeSprite &sprite, uint32_t seed = 0, bool foliage = false);

} // namespace devilution
