#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

#include "cathedral_adapter.hpp"

namespace devilution::cathedral {

enum class NativeTextureKind : uint8_t { Technical, FloorDiamond, Masonry, DoorWood };
enum class NativeTextureAxis : uint8_t { Horizontal, AlongX, AlongZ };

/** Native source provenance, independent of the pick tile and palette lighting.
 * An approximate binding repeats a surface family on a face without native art.
 * This binding does not grant collision, selection, alpha or gameplay authority.
 */
struct NativeTextureBinding {
	NativeTextureKind kind = NativeTextureKind::Technical;
	NativeTextureAxis axis = NativeTextureAxis::Horizontal;
	int x = 0, z = 0;
	uint16_t piece = 0;
	int nativeSlot = -1;
	uint8_t column = 0; // Preferred MIN source column: AlongX=1, AlongZ=0; not the shear selector.
	bool approximate = false;
};

/** Visual inverse of the complete native 64x32 bitmap rectangle in DrawGround.
 * Its separate coverage clips pixel stairs; this is not a physical floor mesh.
 * u/v are bitmap coordinates. No diamond-to-square resampling is performed.
 */
inline Vec3 NativeFloorPixelVertex(int x, int z, float u, float v)
{
	if (!std::isfinite(u) || !std::isfinite(v))
		throw std::invalid_argument("Native floor bitmap coordinates must be finite");
	const Vec3 result { static_cast<float>(x) - 1.46875F + u + v, 0,
		static_cast<float>(z) - 0.46875F - u + v };
	if (!std::isfinite(result.x) || !std::isfinite(result.z))
		throw std::invalid_argument("Native floor bitmap footprint overflow");
	return result;
}

/** Continuous world basis for a rectified 32x16 masonry band: 1 by 0.5 units.
 * Fragment vertices retain the original source origin, not each cube's UVs.
 * DoorWood deliberately has no world basis here: it uses the original local
 * door UVs transported by the Frame through the native hinge rotation.
 */
inline std::array<float, 2> NativePlanarTextureUv(Vec3 world, NativeTextureBinding binding)
{
	if (binding.kind != NativeTextureKind::Masonry
	    || (binding.axis != NativeTextureAxis::AlongX && binding.axis != NativeTextureAxis::AlongZ))
		throw std::invalid_argument("Native planar UV requires oriented masonry");
	if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
		throw std::invalid_argument("Native planar UV position must be finite");
	const float u = binding.axis == NativeTextureAxis::AlongX
	    ? world.x - static_cast<float>(binding.x) : world.z - static_cast<float>(binding.z);
	const float v = -2.0F * world.y;
	if (!std::isfinite(u) || !std::isfinite(v))
		throw std::invalid_argument("Native planar UV overflow");
	return { u, v };
}

struct NativeTextureBand {
	std::array<uint8_t, 32 * 16> pixels {};
	std::array<uint8_t, 32 * 16> opacity {};
};

/** Decoded source is 32x32, row-major, top row first, with independent coverage.
 * shearSelector 0 removes AlongX's positive (+0.5) image slope; 1 removes
 * AlongZ's negative (-0.5) slope. This selector is independent of the MIN
 * source column (AlongX=1, AlongZ=0). It does not flip the horizontal texels:
 * a caller preserving native AlongZ orientation flips the resulting band.
 * Source must be a selected material band, not an entire isometric facade.
 * No lighting, palette remap, alpha inference or missing-texel fill occurs.
 */
inline NativeTextureBand RectifyNativeMasonryBand(std::span<const uint8_t> pixels,
    std::span<const uint8_t> coverage, unsigned shearSelector)
{
	if (pixels.size() != 32 * 32 || coverage.size() != 32 * 32 || shearSelector > 1)
		throw std::invalid_argument("Native masonry band requires 32x32 pixels/coverage and shear selector 0 or 1");
	NativeTextureBand result;
	for (unsigned y = 0; y < 16; ++y) {
		for (unsigned x = 0; x < 32; ++x) {
			const unsigned sourceY = y + (shearSelector == 0 ? x / 2 : (31 - x) / 2);
			const size_t source = static_cast<size_t>(sourceY) * 32 + x;
			const size_t target = static_cast<size_t>(y) * 32 + x;
			result.pixels[target] = pixels[source];
			result.opacity[target] = coverage[source];
		}
	}
	return result;
}

/** Select an entirely covered 8x8 or 16x16 decoded CLX patch for DoorWood.
 * Closest patch center to image center wins; ties use top-to-bottom, then left.
 * Coverage alone decides existence: palette codes 0/255 remain valid paint.
 * Returns {x, y, width, height}; nullopt means no such patch, never filled art.
 */
inline std::optional<std::array<int, 4>> FindNativeOpaquePatch(std::span<const uint8_t> coverage,
    int width, int height, unsigned size)
{
	if (width <= 0 || height <= 0 || (size != 8 && size != 16))
		throw std::invalid_argument("Native opaque patch requires positive dimensions and size 8 or 16");
	const size_t w = static_cast<size_t>(width), h = static_cast<size_t>(height);
	if (w > std::numeric_limits<size_t>::max() / h || coverage.size() != w * h)
		throw std::invalid_argument("Native opaque patch coverage shape invalid");
	if (size > w || size > h) return std::nullopt;
	std::optional<std::array<int, 4>> result;
	uint64_t bestDistance = std::numeric_limits<uint64_t>::max();
	const int extent = static_cast<int>(size);
	for (int y = 0; y <= height - extent; ++y) {
		for (int x = 0; x <= width - extent; ++x) {
			bool covered = true;
			for (int dy = 0; dy < extent && covered; ++dy) {
				for (int dx = 0; dx < extent; ++dx) {
					if (coverage[static_cast<size_t>(y + dy) * w + static_cast<size_t>(x + dx)] == 0) {
						covered = false;
						break;
					}
				}
			}
			if (!covered) continue;
			// Doubled centers avoid fractional ranking and platform rounding.
			const int64_t centerX = 2 * static_cast<int64_t>(x) + extent - width;
			const int64_t centerY = 2 * static_cast<int64_t>(y) + extent - height;
			const uint64_t distance = static_cast<uint64_t>(centerX * centerX) + static_cast<uint64_t>(centerY * centerY);
			if (!result || distance < bestDistance) {
				bestDistance = distance;
				result = std::array<int, 4> { x, y, extent, extent };
			}
		}
	}
	return result;
}

} // namespace devilution::cathedral
