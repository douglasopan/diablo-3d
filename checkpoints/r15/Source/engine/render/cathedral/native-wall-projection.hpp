#pragma once

// Private source-only reconstruction helper. This file has not been compiled
// or run by the specialist. A declared plane is a reconstruction choice;
// canonical projection equality does not prove native depth or 3D fidelity.

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace devilution::cathedral::native_wall_projection {

enum class Axis { AlongX, AlongZ };

struct Point {
	float x, height, z;
};

struct LayerSpan {
	unsigned firstRow = 0;
	unsigned rows = 0;
};

struct Plane {
	int sourceX = 0, sourceZ = 0;
	Axis axis = Axis::AlongX;
	unsigned column = 0;
	float declaredNormalOffset = std::numeric_limits<float>::quiet_NaN();
	LayerSpan layers;
	// One representation choice: flip both the stored atlas and its U basis.
	// Keep false when the prepared atlas preserves native screen-X order.
	bool storedAtlasHorizontallyFlipped = false;
};

inline void Validate(const Plane &plane)
{
	if ((plane.axis != Axis::AlongX && plane.axis != Axis::AlongZ)
	    || plane.sourceX < 0 || plane.sourceX >= 112 || plane.sourceZ < 0 || plane.sourceZ >= 112
	    || plane.column > 1 || plane.layers.rows == 0 || plane.layers.rows > 8
	    || plane.layers.firstRow > 7 || plane.layers.firstRow + plane.layers.rows > 8
	    || !std::isfinite(plane.declaredNormalOffset) || std::abs(plane.declaredNormalOffset) > 16)
		throw std::invalid_argument("Native column projection requires a finite declared plane and valid layer span");
	// Bound this private native-grid study to nearby planes. Finite but enormous
	// offsets erase texel phases in float arithmetic; accepting them would not
	// establish the affine inverse. This range grants no artistic admission.
}

inline int AtlasHeight(const Plane &plane)
{
	Validate(plane);
	return static_cast<int>(32 * plane.layers.rows);
}

inline int AtlasTopScreenEdge(const Plane &plane)
{
	Validate(plane);
	return 1 - static_cast<int>(32 * (plane.layers.firstRow + plane.layers.rows));
}

inline std::array<float, 2> ProjectCanonical(Point world, int sourceX, int sourceZ)
{
	if (!std::isfinite(world.x) || !std::isfinite(world.height) || !std::isfinite(world.z))
		throw std::invalid_argument("Native projection world coordinates must be finite");
	const float dx = world.x - static_cast<float>(sourceX);
	const float dz = world.z - static_cast<float>(sourceZ);
	const std::array<float, 2> result { 32 * (dx - dz), 16 * (dx + dz) - 32 * world.height };
	if (!std::isfinite(result[0]) || !std::isfinite(result[1]))
		throw std::invalid_argument("Native projection overflow");
	return result;
}

/** Bitmap edge coordinates u/v, including i+0.5 texel sampling after division
 * by width/height. The result lies on one explicitly declared vertical plane.
 * No physical bounds, collision, pick owner, camera or palette is changed.
 */
inline Point PixelVertex(const Plane &plane, float u, float v)
{
	Validate(plane);
	if (!std::isfinite(u) || !std::isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1)
		throw std::invalid_argument("Native column vertex requires finite bitmap coordinates in [0,1]");
	const float rawU = plane.storedAtlasHorizontallyFlipped ? 1 - u : u;
	const float offset = plane.declaredNormalOffset;
	const float screenY = static_cast<float>(AtlasTopScreenEdge(plane))
	    + static_cast<float>(AtlasHeight(plane)) * v;
	float dx, dz, height;
	if (plane.axis == Axis::AlongX) {
		dz = offset;
		dx = offset + static_cast<float>(plane.column) - 1 + rawU;
		height = offset + (static_cast<float>(plane.column) - 1 + rawU) / 2 - screenY / 32;
	} else {
		dx = offset;
		dz = offset + 1 - static_cast<float>(plane.column) - rawU;
		height = offset + (1 - static_cast<float>(plane.column) - rawU) / 2 - screenY / 32;
	}
	const Point result { static_cast<float>(plane.sourceX) + dx, height,
		static_cast<float>(plane.sourceZ) + dz };
	if (!std::isfinite(result.x) || !std::isfinite(result.height) || !std::isfinite(result.z))
		throw std::invalid_argument("Native column vertex overflow");
	return result;
}

/** Affine UV for native screen-X-ordered atlas on the declared plane. It does
 * not wrap or clamp: the caller must explicitly clip/source-split fragments
 * outside the source column domain. Per-vertex modulo is not permitted.
 */
inline std::array<float, 2> PlanarUv(const Plane &plane, Point world)
{
	Validate(plane);
	if (!std::isfinite(world.x) || !std::isfinite(world.height) || !std::isfinite(world.z))
		throw std::invalid_argument("Native column UV coordinates must be finite");
	const float normalDelta = plane.axis == Axis::AlongX
	    ? world.z - static_cast<float>(plane.sourceZ) : world.x - static_cast<float>(plane.sourceX);
	// Float world positions near this native map have finite precision. The
	// tolerance admits their roundoff, never a different architectural plane.
	if (std::abs(normalDelta - plane.declaredNormalOffset) > 0.0001F)
		throw std::invalid_argument("Native column UV position differs from its declared plane");
	const auto projected = ProjectCanonical(world, plane.sourceX, plane.sourceZ);
	const float rawU = (projected[0] - 32 * (static_cast<float>(plane.column) - 1)) / 32;
	const std::array<float, 2> result {
		plane.storedAtlasHorizontallyFlipped ? 1 - rawU : rawU,
		(projected[1] - static_cast<float>(AtlasTopScreenEdge(plane))) / static_cast<float>(AtlasHeight(plane))
	};
	if (!std::isfinite(result[0]) || !std::isfinite(result[1]))
		throw std::invalid_argument("Native column UV overflow");
	return result;
}

/** A candidate quad only; constructing these values is not a Frame/GPU draw.
 * Winding deliberately remains an explicit caller decision because normal
 * signs and a source-facing heuristic do not establish native artistic facing.
 */
inline std::array<Point, 4> QuadCorners(const Plane &plane)
{
	return { PixelVertex(plane, 0, 0), PixelVertex(plane, 1, 0),
		PixelVertex(plane, 1, 1), PixelVertex(plane, 0, 1) };
}

} // namespace devilution::cathedral::native_wall_projection
