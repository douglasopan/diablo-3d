#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <utility>
#include <vector>

#include "engine/render/town_lighting.hpp"

namespace devilution {

inline constexpr size_t TownMaxFireShadowLights = 4;
inline constexpr int TownFireShadowResolution = 64;
inline constexpr size_t MaxFireShadowTriangles = 131072;
inline constexpr size_t MaxFireShadowMasks = 512;
inline constexpr size_t TownMaxFireShadowScratchBytes = 32 * 1024 * 1024;
inline constexpr size_t TownMaxFireShadowHistoryBytes = 16 * 1024 * 1024;
// A malformed/pathological mesh must not turn a cold cache miss into unbounded
// work. Reaching either limit refuses the field transaction instead of leaking
// light through a partial map. These count exact operations, not milliseconds.
inline constexpr size_t TownMaxFireShadowBoxTests = 64 * 1024 * 1024;
inline constexpr size_t TownMaxFireShadowTriangleTests = 32 * 1024 * 1024;
inline constexpr float TownFireShadowNear = 0.02F;
inline constexpr float TownFireShadowDepthBias = 0.02F;
inline constexpr float TownFireShadowSlopeBias = 0.025F;

struct TownPointShadowTriangle {
	std::array<TownLightVector, 3> vertices {};
	std::array<std::array<float, 2>, 3> uv {};
	uint16_t mask = 0;
};

/** Borrowed only during Build. Empty opacity/codes means full coverage. An
 * opacity byte !=0 is opaque; codes are needed only for transparentZero. A
 * nonempty payload is exactly width*height bytes. UV sampling matches the
 * host texture: repeat wraps first. Explicit opacity rejects coordinates
 * outside [0,1); code-only transparent-zero textures clamp to the edge.
 * With no masks, triangle mask zero denotes implicit full coverage. */
struct TownPointShadowMask {
	int width = 0, height = 0;
	std::span<const uint8_t> opacity;
	std::span<const uint8_t> codes;
	bool repeat = false, transparentZero = false;
};

enum class TownPointShadowFailure { None, InvalidInput, Capacity, Allocation };

struct TownPointShadowStats {
	bool ready = false, incremental = false, hasAffectedBounds = false;
	bool budgetLimited = false;
	TownPointShadowFailure failure = TownPointShadowFailure::None;
	size_t inputTriangles = 0, acceptedTriangles = 0, masks = 0;
	size_t bvhNodes = 0, scratchBytes = 0, depthBytes = 0;
	size_t rays = 0, boxTests = 0, triangleTests = 0;
	size_t raysReused = 0, raysRecomputed = 0, geometrySnapshotBytes = 0;
	TownLightVector affectedBoundsMinimum {}, affectedBoundsMaximum {};
	size_t maskedIntersections = 0, opaqueIntersections = 0;
	double buildMilliseconds = 0;
};

/** Bounded, owned copy of the exact geometry/masks used to create a dynamic
 * map. It exists only to prove which previous cube rays remain unaffected. */
struct TownPointShadowOwnedMask {
	int width = 0, height = 0;
	std::vector<uint8_t> opacity, codes;
	bool repeat = false, transparentZero = false;
};
struct TownPointShadowGeometrySnapshot {
	bool ready = false;
	std::vector<TownPointShadowTriangle> triangles;
	std::vector<TownPointShadowOwnedMask> masks;
	size_t bytes = 0;
};

/** Immutable completed cube field. Its optional bounded history owns copies
 * of dynamic source geometry only; it retains no native graphics, borrowed
 * masks or GPU objects. The host owns cache identity/invalidation.
 * Layout: depth[((light*6+face)*resolution+y)*resolution+x]. Infinity denotes
 * no opaque hit. Depth is the positive major-axis coordinate, not distance.
 * Face order/direction (u,v in [-1,1]) shared verbatim with HLSL:
 * +X(1,-v,-u), -X(-1,-v,u), +Y(u,1,v), -Y(u,-1,-v),
 * +Z(u,-v,1), -Z(-u,-v,-1).
 * Rays use u=2*(x+.5)/64-1 and v=2*(y+.5)/64-1. Receivers choose
 * the largest absolute delta component; ties prefer X, then Y, then Z.
 * Lookup center=floor((uv+1)*.5*64). Each of the nine center+offset coordinates
 * clamps to [0,63] on the selected face; no interpolation of depth occurs.
 * A comparison is lit when axialDepth <= storedDepth + .02 + .025*(1-NdotL).
 * This bounded PCF has a one-texel face-edge seam; it is not a soft penumbra.
 */
struct TownPointShadowMaps {
	std::vector<float> depth;
	std::array<TownPointLight, TownMaxFireShadowLights> lights {};
	size_t lightCount = 0;
	int resolution = TownFireShadowResolution;
	uint64_t identity = 0, revision = 0;
	TownPointShadowStats stats;
	TownPointShadowGeometrySnapshot geometrySnapshot;
};

namespace town_point_shadow_detail {

inline float Component(TownLightVector value, unsigned axis)
{
	return axis == 0 ? value.x : axis == 1 ? value.height : value.z;
}
inline TownLightVector Add(TownLightVector a, TownLightVector b) { return { a.x + b.x, a.height + b.height, a.z + b.z }; }
inline TownLightVector Subtract(TownLightVector a, TownLightVector b) { return { a.x - b.x, a.height - b.height, a.z - b.z }; }
inline TownLightVector Scale(TownLightVector a, float factor) { return { a.x * factor, a.height * factor, a.z * factor }; }
inline float Dot(TownLightVector a, TownLightVector b) { return a.x * b.x + a.height * b.height + a.z * b.z; }
inline TownLightVector Cross(TownLightVector a, TownLightVector b)
{
	return { a.height * b.z - a.z * b.height, a.z * b.x - a.x * b.z, a.x * b.height - a.height * b.x };
}
inline bool Finite(TownLightVector value)
{
	return std::isfinite(value.x) && std::isfinite(value.height) && std::isfinite(value.z)
	    && std::abs(value.x) <= 4096 && std::abs(value.height) <= 4096 && std::abs(value.z) <= 4096;
}
inline bool Normalize(TownLightVector &value)
{
	const float lengthSquared = Dot(value, value);
	if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
		return false;
	value = Scale(value, 1 / std::sqrt(lengthSquared));
	return true;
}
inline bool ValidLight(const TownPointLight &light)
{
	const auto color = [](float value) { return std::isfinite(value) && value >= 0 && value <= 16; };
	return Finite(light.position) && std::isfinite(light.radius) && light.radius > TownFireShadowNear && light.radius <= 16
	    && color(light.color.red) && color(light.color.green) && color(light.color.blue)
	    && std::isfinite(light.intensity) && light.intensity >= 0 && light.intensity <= TownMaxPointLightIntensity;
}
inline bool ValidMask(const TownPointShadowMask &mask)
{
	if (mask.width < 0 || mask.height < 0 || mask.width > 4096 || mask.height > 4096)
		return false;
	if (mask.opacity.empty() && mask.codes.empty())
		return !mask.transparentZero;
	if (mask.width == 0 || mask.height == 0)
		return false;
	const size_t size = static_cast<size_t>(mask.width) * mask.height;
	return (mask.opacity.empty() || mask.opacity.size() == size)
	    && (mask.codes.empty() || mask.codes.size() == size)
	    && (!mask.transparentZero || mask.codes.size() == size);
}
inline bool Covered(const TownPointShadowMask &mask, float u, float v)
{
	if (mask.opacity.empty() && !mask.transparentZero)
		return true;
	if (mask.repeat) {
		u -= std::floor(u);
		v -= std::floor(v);
	} else if (!mask.opacity.empty() && (u < 0 || v < 0 || u >= 1 || v >= 1)) {
		return false;
	}
	const int x = static_cast<int>(std::clamp(u * mask.width, 0.0F, static_cast<float>(mask.width - 1)));
	const int y = static_cast<int>(std::clamp(v * mask.height, 0.0F, static_cast<float>(mask.height - 1)));
	const size_t index = static_cast<size_t>(y) * mask.width + x;
	return (mask.opacity.empty() || mask.opacity[index] != 0) && (!mask.transparentZero || mask.codes[index] != 0);
}

struct Bounds {
	TownLightVector minimum { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };
	TownLightVector maximum { -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
};
inline void Include(Bounds &bounds, TownLightVector point)
{
	bounds.minimum = { std::min(bounds.minimum.x, point.x), std::min(bounds.minimum.height, point.height), std::min(bounds.minimum.z, point.z) };
	bounds.maximum = { std::max(bounds.maximum.x, point.x), std::max(bounds.maximum.height, point.height), std::max(bounds.maximum.z, point.z) };
}
inline void Include(Bounds &bounds, const Bounds &other)
{
	Include(bounds, other.minimum);
	Include(bounds, other.maximum);
}
struct Primitive {
	Bounds bounds;
	TownLightVector center;
	uint32_t triangle;
};
struct Node {
	Bounds bounds;
	uint32_t begin = 0, count = 0, left = 0, right = 0;
};

inline uint32_t BuildNode(std::vector<Node> &nodes, std::vector<Primitive> &primitives, uint32_t begin, uint32_t end)
{
	Node node;
	node.begin = begin;
	node.count = end - begin;
	Bounds centers;
	for (uint32_t i = begin; i < end; ++i) {
		Include(node.bounds, primitives[i].bounds);
		Include(centers, primitives[i].center);
	}
	const auto index = static_cast<uint32_t>(nodes.size());
	nodes.push_back(node);
	if (node.count <= 8)
		return index;
	const TownLightVector extent = Subtract(centers.maximum, centers.minimum);
	unsigned axis = extent.height > extent.x ? 1 : 0;
	if (extent.z > Component(extent, axis))
		axis = 2;
	const uint32_t middle = begin + node.count / 2;
	std::nth_element(primitives.begin() + begin, primitives.begin() + middle, primitives.begin() + end,
	    [axis](const Primitive &a, const Primitive &b) {
		    const float first = Component(a.center, axis), second = Component(b.center, axis);
		    return first < second || (first == second && a.triangle < b.triangle);
	    });
	const uint32_t left = BuildNode(nodes, primitives, begin, middle);
	const uint32_t right = BuildNode(nodes, primitives, middle, end);
	nodes[index].left = left;
	nodes[index].right = right;
	nodes[index].count = 0;
	return index;
}

inline bool BoxHit(const Bounds &bounds, TownLightVector origin, TownLightVector direction, float maximum, float &nearHit)
{
	float minimum = TownFireShadowNear;
	for (unsigned axis = 0; axis < 3; ++axis) {
		const float o = Component(origin, axis), d = Component(direction, axis);
		// Tiny expansion prevents a flat triangle's float bounds from missing
		// its own mathematical plane; it does not thicken the triangle itself.
		const float low = Component(bounds.minimum, axis) - 1.0e-5F;
		const float high = Component(bounds.maximum, axis) + 1.0e-5F;
		if (std::abs(d) < 1.0e-12F) {
			if (o < low || o > high)
				return false;
			continue;
		}
		float entry, exit;
		if (d == 1.0F) {
			entry = low - o;
			exit = high - o;
		} else if (d == -1.0F) {
			entry = -(low - o);
			exit = -(high - o);
		} else {
			entry = (low - o) / d;
			exit = (high - o) / d;
		}
		if (entry > exit)
			std::swap(entry, exit);
		minimum = std::max(minimum, entry);
		maximum = std::min(maximum, exit);
		if (minimum > maximum)
			return false;
	}
	nearHit = minimum;
	return true;
}

inline bool TriangleHit(const TownPointShadowTriangle &triangle, TownLightVector origin, TownLightVector direction,
    float maximum, float &distance, float &u, float &v)
{
	// Double intermediates stabilize the ray/edge decision for tiny authored
	// faces. The output map and all receiver comparisons remain float.
	const auto a = triangle.vertices[0], b = triangle.vertices[1], c = triangle.vertices[2];
	const std::array<double, 3> edge1 { double(b.x) - a.x, double(b.height) - a.height, double(b.z) - a.z };
	const std::array<double, 3> edge2 { double(c.x) - a.x, double(c.height) - a.height, double(c.z) - a.z };
	const std::array<double, 3> p { direction.height * edge2[2] - direction.z * edge2[1], direction.z * edge2[0] - direction.x * edge2[2], direction.x * edge2[1] - direction.height * edge2[0] };
	const double determinant = edge1[0] * p[0] + edge1[1] * p[1] + edge1[2] * p[2];
	if (std::abs(determinant) < 1.0e-14)
		return false;
	const double inverse = 1 / determinant;
	const std::array<double, 3> delta { double(origin.x) - a.x, double(origin.height) - a.height, double(origin.z) - a.z };
	const double baryB = (delta[0] * p[0] + delta[1] * p[1] + delta[2] * p[2]) * inverse;
	if (baryB < -1.0e-8 || baryB > 1 + 1.0e-8)
		return false;
	const std::array<double, 3> q { delta[1] * edge1[2] - delta[2] * edge1[1], delta[2] * edge1[0] - delta[0] * edge1[2], delta[0] * edge1[1] - delta[1] * edge1[0] };
	const double baryC = (direction.x * q[0] + direction.height * q[1] + direction.z * q[2]) * inverse;
	if (baryC < -1.0e-8 || baryB + baryC > 1 + 1.0e-8)
		return false;
	const double t = (edge2[0] * q[0] + edge2[1] * q[1] + edge2[2] * q[2]) * inverse;
	if (t < TownFireShadowNear || t > maximum)
		return false;
	distance = static_cast<float>(t);
	u = static_cast<float>((1 - baryB - baryC) * triangle.uv[0][0] + baryB * triangle.uv[1][0] + baryC * triangle.uv[2][0]);
	v = static_cast<float>((1 - baryB - baryC) * triangle.uv[0][1] + baryB * triangle.uv[1][1] + baryC * triangle.uv[2][1]);
	return true;
}

inline TownLightVector FaceDirection(unsigned face, float u, float v)
{
	switch (face) {
	case 0: return { 1, -v, -u };
	case 1: return { -1, -v, u };
	case 2: return { u, 1, v };
	case 3: return { u, -1, -v };
	case 4: return { u, -v, 1 };
	default: return { -u, -v, -1 };
	}
}
struct FaceSample { unsigned face; float u, v, axial; };
inline FaceSample SelectFace(TownLightVector delta)
{
	const float x = std::abs(delta.x), y = std::abs(delta.height), z = std::abs(delta.z);
	if (x >= y && x >= z)
		return delta.x >= 0 ? FaceSample { 0, -delta.z / x, -delta.height / x, x } : FaceSample { 1, delta.z / x, -delta.height / x, x };
	if (y >= z)
		return delta.height >= 0 ? FaceSample { 2, delta.x / y, delta.z / y, y } : FaceSample { 3, delta.x / y, -delta.z / y, y };
	return delta.z >= 0 ? FaceSample { 4, delta.x / z, -delta.height / z, z } : FaceSample { 5, -delta.x / z, -delta.height / z, z };
}

inline bool Trace(const std::vector<Node> &nodes, const std::vector<Primitive> &primitives,
    std::span<const TownPointShadowTriangle> triangles, std::span<const TownPointShadowMask> masks,
    TownLightVector origin, TownLightVector direction, float radius, TownPointShadowStats &stats, float &depth)
{
	depth = std::numeric_limits<float>::infinity();
	if (nodes.empty())
		return true;
	const float maximum = radius / std::sqrt(Dot(direction, direction));
	float best = maximum;
	struct Pending { uint32_t node; float nearHit; };
	std::array<Pending, 64> stack;
	size_t count = 0;
	float nearHit = 0;
	if (++stats.boxTests > TownMaxFireShadowBoxTests)
		return false;
	if (!BoxHit(nodes[0].bounds, origin, direction, best, nearHit))
		return true;
	stack[count++] = { 0, nearHit };
	while (count != 0) {
		const auto pending = stack[--count];
		if (pending.nearHit > best)
			continue;
		const Node &node = nodes[pending.node];
		if (node.count != 0) {
			for (uint32_t n = node.begin; n < node.begin + node.count; ++n) {
				if (++stats.triangleTests > TownMaxFireShadowTriangleTests)
					return false;
				const auto &triangle = triangles[primitives[n].triangle];
				float distance, u, v;
				if (!TriangleHit(triangle, origin, direction, best, distance, u, v))
					continue;
				if (!masks.empty() && !Covered(masks[triangle.mask], u, v)) {
					++stats.maskedIntersections;
					continue;
				}
				++stats.opaqueIntersections;
				best = distance;
				depth = distance;
			}
			continue;
		}
		float nearLeft = 0, nearRight = 0;
		stats.boxTests += 2;
		if (stats.boxTests > TownMaxFireShadowBoxTests)
			return false;
		const bool left = BoxHit(nodes[node.left].bounds, origin, direction, best, nearLeft);
		const bool right = BoxHit(nodes[node.right].bounds, origin, direction, best, nearRight);
		if (count + unsigned(left) + unsigned(right) > stack.size())
			return false;
		if (left && right) {
			if (nearLeft <= nearRight) {
				stack[count++] = { node.right, nearRight };
				stack[count++] = { node.left, nearLeft };
			} else {
				stack[count++] = { node.left, nearLeft };
				stack[count++] = { node.right, nearRight };
			}
		} else if (left) {
			stack[count++] = { node.left, nearLeft };
		} else if (right) {
			stack[count++] = { node.right, nearRight };
		}
	}
	return true;
}

inline bool ValidMaps(const TownPointShadowMaps &maps)
{
	return maps.stats.ready && maps.identity != 0 && maps.revision != 0 && maps.lightCount <= TownMaxFireShadowLights
	    && maps.resolution == TownFireShadowResolution
	    && maps.depth.size() == maps.lightCount * 6 * TownFireShadowResolution * TownFireShadowResolution;
}

inline bool SameBits(float a, float b)
{
	return std::memcmp(&a, &b, sizeof(float)) == 0;
}
inline bool SameSnapshotTriangle(const TownPointShadowTriangle &a, const TownPointShadowTriangle &b)
{
	if (a.mask != b.mask)
		return false;
	for (size_t i = 0; i < 3; ++i) {
		if (!SameBits(a.vertices[i].x, b.vertices[i].x) || !SameBits(a.vertices[i].height, b.vertices[i].height)
		    || !SameBits(a.vertices[i].z, b.vertices[i].z) || !SameBits(a.uv[i][0], b.uv[i][0]) || !SameBits(a.uv[i][1], b.uv[i][1]))
			return false;
	}
	return true;
}
inline bool SameLight(const TownPointLight &a, const TownPointLight &b)
{
	return SameBits(a.position.x, b.position.x) && SameBits(a.position.height, b.position.height) && SameBits(a.position.z, b.position.z)
	    && SameBits(a.color.red, b.color.red) && SameBits(a.color.green, b.color.green) && SameBits(a.color.blue, b.color.blue)
	    && SameBits(a.radius, b.radius) && SameBits(a.intensity, b.intensity);
}
inline bool SameMask(const TownPointShadowOwnedMask &a, const TownPointShadowMask &b)
{
	return a.width == b.width && a.height == b.height && a.repeat == b.repeat && a.transparentZero == b.transparentZero
	    && a.opacity.size() == b.opacity.size() && a.codes.size() == b.codes.size()
	    && (a.opacity.empty() || std::memcmp(a.opacity.data(), b.opacity.data(), a.opacity.size()) == 0)
	    && (a.codes.empty() || std::memcmp(a.codes.data(), b.codes.data(), a.codes.size()) == 0);
}
inline bool Empty(const Bounds &bounds)
{
	return bounds.minimum.x > bounds.maximum.x || bounds.minimum.height > bounds.maximum.height || bounds.minimum.z > bounds.maximum.z;
}
inline bool ValidBounds(const Bounds &bounds)
{
	return Empty(bounds) || (Finite(bounds.minimum) && Finite(bounds.maximum)
	    && bounds.minimum.x <= bounds.maximum.x && bounds.minimum.height <= bounds.maximum.height && bounds.minimum.z <= bounds.maximum.z);
}
inline bool ContainsBounds(const Bounds &outer, const Bounds &inner)
{
	if (!ValidBounds(outer) || !ValidBounds(inner))
		return false;
	if (Empty(inner))
		return true;
	return !Empty(outer) && outer.minimum.x <= inner.minimum.x && outer.minimum.height <= inner.minimum.height
	    && outer.minimum.z <= inner.minimum.z && outer.maximum.x >= inner.maximum.x
	    && outer.maximum.height >= inner.maximum.height && outer.maximum.z >= inner.maximum.z;
}
inline void Include(Bounds &bounds, const TownPointShadowTriangle &triangle)
{
	for (const TownLightVector vertex : triangle.vertices)
		Include(bounds, vertex);
}

// Validate actual retained capacity, not a caller-provided accounting field.
inline bool ValidGeometrySnapshotStorage(const TownPointShadowGeometrySnapshot &snapshot)
{
 size_t retained = sizeof(TownPointShadowGeometrySnapshot);
 const auto add = [&](size_t count, size_t element) {
  if (count > (TownMaxFireShadowHistoryBytes - retained) / element) return false;
  retained += count * element; return true;
 };
 if (!add(snapshot.triangles.capacity(), sizeof(TownPointShadowTriangle))
     || !add(snapshot.masks.capacity(), sizeof(TownPointShadowOwnedMask))) return false;
 for (const auto &mask : snapshot.masks) {
  if (!add(mask.opacity.capacity(), sizeof(uint8_t)) || !add(mask.codes.capacity(), sizeof(uint8_t))) return false;
 }
 return retained == snapshot.bytes;
}

inline bool ReusableShadowHistory(const TownPointShadowMaps &previous,
	    std::span<const TownPointShadowTriangle> triangles, std::span<const TownPointShadowMask> masks,
	    std::span<const TownPointLight> lights, uint64_t identity)
{
	if (!ValidMaps(previous) || previous.identity != identity || previous.lightCount != lights.size()
	    || !previous.geometrySnapshot.ready || previous.geometrySnapshot.bytes > TownMaxFireShadowHistoryBytes
	    || !ValidGeometrySnapshotStorage(previous.geometrySnapshot)
	    || previous.geometrySnapshot.triangles.size() != triangles.size()
	    || previous.geometrySnapshot.masks.size() != masks.size() || triangles.size() > MaxFireShadowTriangles
	    || masks.size() > MaxFireShadowMasks)
		return false;
	if (!std::all_of(previous.depth.begin(), previous.depth.end(), [](float depth) { return depth > 0 && !std::isnan(depth); }))
		return false;
	for (size_t i = 0; i < lights.size(); ++i) {
		if (!ValidLight(lights[i]) || !SameLight(previous.lights[i], lights[i]))
			return false;
	}
	for (size_t i = 0; i < masks.size(); ++i) {
		if (!ValidMask(masks[i]) || !SameMask(previous.geometrySnapshot.masks[i], masks[i]))
			return false;
	}
	return true;
}

/** Reuse is permitted only when source identity, lights, ordered triangle
 * count, mask count and mask contents are exact. Any count/mask delta falls
 * back to the original full build. Changed ordered triangles contribute both
 * old and new bounds, so rays outside their union cannot hit changed geometry. */
inline bool FindChangedGeometryBounds(const TownPointShadowMaps &previous,
	    std::span<const TownPointShadowTriangle> triangles, std::span<const TownPointShadowMask> masks,
	    std::span<const TownPointLight> lights, uint64_t identity, Bounds &changed)
{
	if (!ReusableShadowHistory(previous, triangles, masks, lights, identity))
		return false;
	for (size_t i = 0; i < triangles.size(); ++i) {
		const TownPointShadowTriangle &current = triangles[i];
		const TownPointShadowTriangle &old = previous.geometrySnapshot.triangles[i];
		if ((!masks.empty() && old.mask >= masks.size()) || (masks.empty() && old.mask != 0)) return false;
        for (size_t vertex = 0; vertex < 3; ++vertex) {
            if (!Finite(old.vertices[vertex]) || !std::isfinite(old.uv[vertex][0]) || !std::isfinite(old.uv[vertex][1])
                || std::abs(old.uv[vertex][0]) > 1000000 || std::abs(old.uv[vertex][1]) > 1000000) return false;
        }
		if ((!masks.empty() && current.mask >= masks.size()) || (masks.empty() && current.mask != 0))
			return false;
		for (size_t vertex = 0; vertex < 3; ++vertex) {
			if (!Finite(current.vertices[vertex]) || !std::isfinite(current.uv[vertex][0]) || !std::isfinite(current.uv[vertex][1])
			    || std::abs(current.uv[vertex][0]) > 1000000 || std::abs(current.uv[vertex][1]) > 1000000)
				return false;
		}
		if (SameSnapshotTriangle(old, current))
			continue;
		Include(changed, old);
		Include(changed, current);
	}
	return true;
}

inline bool CaptureGeometrySnapshot(TownPointShadowGeometrySnapshot &snapshot,
	    std::span<const TownPointShadowTriangle> triangles, std::span<const TownPointShadowMask> masks)
{
	snapshot = {};
	if (triangles.size() > MaxFireShadowTriangles || masks.size() > MaxFireShadowMasks
	    || triangles.size() > TownMaxFireShadowHistoryBytes / sizeof(TownPointShadowTriangle))
		return false;
	size_t bytes = triangles.size() * sizeof(TownPointShadowTriangle);
	for (const TownPointShadowMask &mask : masks) {
		if (!ValidMask(mask) || mask.opacity.size() > TownMaxFireShadowHistoryBytes - bytes)
			return false;
		bytes += mask.opacity.size();
		if (mask.codes.size() > TownMaxFireShadowHistoryBytes - bytes)
			return false;
		bytes += mask.codes.size();
	}
	if (bytes > TownMaxFireShadowHistoryBytes)
		return false;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
		snapshot.triangles.assign(triangles.begin(), triangles.end());
		snapshot.masks.reserve(masks.size());
		for (const TownPointShadowMask &mask : masks) {
			TownPointShadowOwnedMask owned;
			owned.width = mask.width;
			owned.height = mask.height;
			owned.repeat = mask.repeat;
			owned.transparentZero = mask.transparentZero;
			owned.opacity.assign(mask.opacity.begin(), mask.opacity.end());
			owned.codes.assign(mask.codes.begin(), mask.codes.end());
			snapshot.masks.push_back(std::move(owned));
		}
		size_t retained = sizeof(TownPointShadowGeometrySnapshot) + snapshot.triangles.capacity() * sizeof(TownPointShadowTriangle)
		    + snapshot.masks.capacity() * sizeof(TownPointShadowOwnedMask);
		if (retained > TownMaxFireShadowHistoryBytes) {
			snapshot = {};
			return false;
		}
		bool overBudget = false;
		for (const TownPointShadowOwnedMask &mask : snapshot.masks) {
			if (mask.opacity.capacity() > TownMaxFireShadowHistoryBytes - retained) {
				overBudget = true;
				break;
			}
			retained += mask.opacity.capacity();
			if (mask.codes.capacity() > TownMaxFireShadowHistoryBytes - retained) {
				overBudget = true;
				break;
			}
			retained += mask.codes.capacity();
		}
		if (overBudget || retained > TownMaxFireShadowHistoryBytes) {
			snapshot = {};
			return false;
		}
		snapshot.bytes = retained;
		snapshot.ready = true;
		return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (const std::bad_alloc &) {
		snapshot = {};
		return false;
	}
#endif
}

inline bool RayIntersectsChangedBounds(const Bounds &bounds, TownLightVector origin, TownLightVector direction, float radius)
{
	if (Empty(bounds))
		return false;
	float nearHit = 0;
	return BoxHit(bounds, origin, direction, radius / std::sqrt(Dot(direction, direction)), nearHit);
}
inline float SrgbToLinear(float value)
{
	return value <= .04045F ? value / 12.92F : std::pow((value + .055F) / 1.055F, 2.4F);
}
inline float LinearToSrgb(float value)
{
	value = std::clamp(value, 0.0F, 1.0F);
	return value <= .0031308F ? value * 12.92F : 1.055F * std::pow(value, 1 / 2.4F) - .055F;
}

} // namespace town_point_shadow_detail

/** Transactional bounded CPU build; no RNG/game/camera/GPU access. All masks
 * and triangles are borrowed until this call returns. BVH scratch is released
 * on return and never exceeds 32 MiB; the full builder retains only float maps.
 * The host supplies nonzero content identity/revision and handles cache hits.
 * Any bad input, resource/work budget or allocation failure clears old output.
 * Empty lights succeeds with an empty field; empty geometry samples as lit. */
/** Full transactional build; the original trace path remains the reference. */
inline bool BuildTownPointShadowMaps(TownPointShadowMaps &output,
    std::span<const TownPointShadowTriangle> triangles, std::span<const TownPointShadowMask> masks,
    std::span<const TownPointLight> lights, uint64_t identity, uint64_t revision)
{
	using namespace town_point_shadow_detail;
	const auto start = std::chrono::steady_clock::now();
	TownPointShadowStats stats;
	stats.inputTriangles = triangles.size();
	stats.masks = masks.size();
	const auto milliseconds = [&]() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
	const auto refuse = [&](bool budget = false, TownPointShadowFailure failure = TownPointShadowFailure::InvalidInput) {
		stats.budgetLimited = budget;
		stats.failure = budget && failure == TownPointShadowFailure::InvalidInput ? TownPointShadowFailure::Capacity : failure;
		stats.ready = false;
		stats.buildMilliseconds = milliseconds();
		output = {};
		output.stats = stats;
		return false;
	};
	if (identity == 0 || revision == 0 || triangles.size() > MaxFireShadowTriangles || masks.size() > MaxFireShadowMasks
	    || lights.size() > TownMaxFireShadowLights)
		return refuse(triangles.size() > MaxFireShadowTriangles || masks.size() > MaxFireShadowMasks || lights.size() > TownMaxFireShadowLights);
	if (!std::all_of(lights.begin(), lights.end(), ValidLight) || !std::all_of(masks.begin(), masks.end(), ValidMask))
		return refuse();
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
		std::vector<Primitive> primitives;
		std::vector<Node> nodes;
		// Reserve the worst-case binary tree up front. The actual balanced tree
		// stops at eight primitives per leaf, so it is substantially smaller.
		const size_t maximumNodes = triangles.empty() ? 0 : triangles.size() * 2 - 1;
		const size_t scratchLimit = triangles.size() * sizeof(Primitive) + maximumNodes * sizeof(Node);
		if (scratchLimit > TownMaxFireShadowScratchBytes)
			return refuse(true);
		primitives.reserve(triangles.size());
		nodes.reserve(maximumNodes);
		stats.scratchBytes = primitives.capacity() * sizeof(Primitive) + nodes.capacity() * sizeof(Node);
		if (stats.scratchBytes > TownMaxFireShadowScratchBytes)
			return refuse(true);
		for (size_t i = 0; i < triangles.size(); ++i) {
			const auto &triangle = triangles[i];
			if ((!masks.empty() && triangle.mask >= masks.size()) || (masks.empty() && triangle.mask != 0))
				return refuse();
			Primitive primitive;
			primitive.triangle = static_cast<uint32_t>(i);
			for (size_t n = 0; n < 3; ++n) {
				if (!Finite(triangle.vertices[n]) || !std::isfinite(triangle.uv[n][0]) || !std::isfinite(triangle.uv[n][1])
				    || std::abs(triangle.uv[n][0]) > 1000000 || std::abs(triangle.uv[n][1]) > 1000000)
					return refuse();
				Include(primitive.bounds, triangle.vertices[n]);
			}
			const auto cross = Cross(Subtract(triangle.vertices[1], triangle.vertices[0]), Subtract(triangle.vertices[2], triangle.vertices[0]));
			if (Dot(cross, cross) <= 1.0e-18F)
				continue; // A finite zero-area triangle casts no shadow.
			primitive.center = Scale(Add(primitive.bounds.minimum, primitive.bounds.maximum), .5F);
			primitives.push_back(primitive);
		}
		stats.acceptedTriangles = primitives.size();
		if (!primitives.empty())
			BuildNode(nodes, primitives, 0, static_cast<uint32_t>(primitives.size()));
		stats.bvhNodes = nodes.size();
		TownPointShadowMaps next;
		next.identity = identity;
		next.revision = revision;
		next.lightCount = lights.size();
		std::copy(lights.begin(), lights.end(), next.lights.begin());
		next.depth.assign(lights.size() * 6 * TownFireShadowResolution * TownFireShadowResolution, std::numeric_limits<float>::infinity());
		stats.depthBytes = next.depth.size() * sizeof(float);
		for (size_t light = 0; light < lights.size(); ++light)
			for (unsigned face = 0; face < 6; ++face)
				for (int y = 0; y < TownFireShadowResolution; ++y)
					for (int x = 0; x < TownFireShadowResolution; ++x) {
						const auto direction = FaceDirection(face, 2 * (x + .5F) / TownFireShadowResolution - 1, 2 * (y + .5F) / TownFireShadowResolution - 1);
						const size_t index = ((light * 6 + face) * TownFireShadowResolution + y) * TownFireShadowResolution + x;
						++stats.rays;
						++stats.raysRecomputed;
						if (!Trace(nodes, primitives, triangles, masks, lights[light].position, direction, lights[light].radius, stats, next.depth[index]))
							return refuse(true);
					}
		stats.ready = true;
		stats.buildMilliseconds = milliseconds();
		next.stats = stats;
		output = std::move(next);
		return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (const std::bad_alloc &) {
		return refuse(true, TownPointShadowFailure::Allocation);
	}
#endif
}

inline bool BuildTownPointShadowMapsIncremental(TownPointShadowMaps &output,
    std::span<const TownPointShadowTriangle> triangles, std::span<const TownPointShadowMask> masks,
    std::span<const TownPointLight> lights, uint64_t identity, uint64_t revision,
    const TownPointShadowMaps &previous, const town_point_shadow_detail::Bounds *changedBounds)
{
	using namespace town_point_shadow_detail;
	const auto buildFullWithHistory = [&]() {
		if (!BuildTownPointShadowMaps(output, triangles, masks, lights, identity, revision))
			return false;
		const auto historyStart = std::chrono::steady_clock::now();
		CaptureGeometrySnapshot(output.geometrySnapshot, triangles, masks);
		output.stats.geometrySnapshotBytes = output.geometrySnapshot.bytes;
		output.stats.buildMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - historyStart).count();
		return true;
	};
	Bounds verifiedBounds;
	if (changedBounds == nullptr || !ValidBounds(*changedBounds)
	    || !FindChangedGeometryBounds(previous, triangles, masks, lights, identity, verifiedBounds)
	    || !ContainsBounds(*changedBounds, verifiedBounds))
		return buildFullWithHistory();
	Bounds effectiveBounds = verifiedBounds;
	if (!Empty(*changedBounds))
		Include(effectiveBounds, *changedBounds);
	const auto start = std::chrono::steady_clock::now();
	TownPointShadowStats stats;
	stats.inputTriangles = triangles.size();
	stats.masks = masks.size();
	const auto milliseconds = [&]() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); };
	const auto refuse = [&](bool budget = false, TownPointShadowFailure failure = TownPointShadowFailure::InvalidInput) {
		stats.budgetLimited = budget;
		stats.failure = budget && failure == TownPointShadowFailure::InvalidInput ? TownPointShadowFailure::Capacity : failure;
		stats.ready = false;
		stats.buildMilliseconds = milliseconds();
		output = {};
		output.stats = stats;
		return false;
	};
	if (identity == 0 || revision == 0 || triangles.size() > MaxFireShadowTriangles || masks.size() > MaxFireShadowMasks
	    || lights.size() > TownMaxFireShadowLights)
		return refuse(triangles.size() > MaxFireShadowTriangles || masks.size() > MaxFireShadowMasks || lights.size() > TownMaxFireShadowLights);
	if (!std::all_of(lights.begin(), lights.end(), ValidLight) || !std::all_of(masks.begin(), masks.end(), ValidMask))
		return refuse();
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
		std::vector<Primitive> primitives;
		std::vector<Node> nodes;
		// Reserve the worst-case binary tree up front. The actual balanced tree
		// stops at eight primitives per leaf, so it is substantially smaller.
		const size_t maximumNodes = triangles.empty() ? 0 : triangles.size() * 2 - 1;
		const size_t scratchLimit = triangles.size() * sizeof(Primitive) + maximumNodes * sizeof(Node);
		if (scratchLimit > TownMaxFireShadowScratchBytes)
			return refuse(true);
		primitives.reserve(triangles.size());
		nodes.reserve(maximumNodes);
		stats.scratchBytes = primitives.capacity() * sizeof(Primitive) + nodes.capacity() * sizeof(Node);
		if (stats.scratchBytes > TownMaxFireShadowScratchBytes)
			return refuse(true);
		for (size_t i = 0; i < triangles.size(); ++i) {
			const auto &triangle = triangles[i];
			if ((!masks.empty() && triangle.mask >= masks.size()) || (masks.empty() && triangle.mask != 0))
				return refuse();
			Primitive primitive;
			primitive.triangle = static_cast<uint32_t>(i);
			for (size_t n = 0; n < 3; ++n) {
				if (!Finite(triangle.vertices[n]) || !std::isfinite(triangle.uv[n][0]) || !std::isfinite(triangle.uv[n][1])
				    || std::abs(triangle.uv[n][0]) > 1000000 || std::abs(triangle.uv[n][1]) > 1000000)
					return refuse();
				Include(primitive.bounds, triangle.vertices[n]);
			}
			const auto cross = Cross(Subtract(triangle.vertices[1], triangle.vertices[0]), Subtract(triangle.vertices[2], triangle.vertices[0]));
			if (Dot(cross, cross) <= 1.0e-18F)
				continue; // A finite zero-area triangle casts no shadow.
			primitive.center = Scale(Add(primitive.bounds.minimum, primitive.bounds.maximum), .5F);
			primitives.push_back(primitive);
		}
		stats.acceptedTriangles = primitives.size();
		if (!primitives.empty())
			BuildNode(nodes, primitives, 0, static_cast<uint32_t>(primitives.size()));
		stats.bvhNodes = nodes.size();
		TownPointShadowMaps next;
		next.identity = identity;
		next.revision = revision;
		next.lightCount = lights.size();
		std::copy(lights.begin(), lights.end(), next.lights.begin());
		next.depth.assign(lights.size() * 6 * TownFireShadowResolution * TownFireShadowResolution, std::numeric_limits<float>::infinity());
		stats.depthBytes = next.depth.size() * sizeof(float);
		for (size_t light = 0; light < lights.size(); ++light)
			for (unsigned face = 0; face < 6; ++face)
				for (int y = 0; y < TownFireShadowResolution; ++y)
					for (int x = 0; x < TownFireShadowResolution; ++x) {
						const auto direction = FaceDirection(face, 2 * (x + .5F) / TownFireShadowResolution - 1, 2 * (y + .5F) / TownFireShadowResolution - 1);
						const size_t index = ((light * 6 + face) * TownFireShadowResolution + y) * TownFireShadowResolution + x;
						++stats.rays;
						if (!RayIntersectsChangedBounds(effectiveBounds, lights[light].position, direction, lights[light].radius)) {
							next.depth[index] = previous.depth[index];
							++stats.raysReused;
							continue;
						}
						++stats.raysRecomputed;
						if (!Trace(nodes, primitives, triangles, masks, lights[light].position, direction, lights[light].radius, stats, next.depth[index]))
							return refuse(true);
					}
		stats.ready = true;
		stats.incremental = true;
		stats.hasAffectedBounds = !Empty(effectiveBounds);
		if (stats.hasAffectedBounds) {
			stats.affectedBoundsMinimum = effectiveBounds.minimum;
			stats.affectedBoundsMaximum = effectiveBounds.maximum;
		}
		CaptureGeometrySnapshot(next.geometrySnapshot, triangles, masks);
		stats.geometrySnapshotBytes = next.geometrySnapshot.bytes;
		stats.buildMilliseconds = milliseconds();
		next.stats = stats;
		output = std::move(next);
		return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (const std::bad_alloc &) {
		return refuse(true, TownPointShadowFailure::Allocation);
	}
#endif
}

/** No allocation or validation scan in pixel loops. The host only publishes a
 * validated field from Build. Invalid layouts fail closed. Outside the source
 * radius is unshadowed because that light contributes zero irradiance there. */
inline float SampleTownPointShadowVisibility(const TownPointShadowMaps &maps, size_t lightIndex,
    TownLightVector position, TownLightVector normal)
{
	using namespace town_point_shadow_detail;
	if (!ValidMaps(maps) || lightIndex >= maps.lightCount || !Finite(position) || !Finite(normal))
		return 0;
	const auto &light = maps.lights[lightIndex];
	if (!ValidLight(light))
		return 0;
	const auto delta = Subtract(position, light.position);
	const float squared = Dot(delta, delta);
	if (squared >= light.radius * light.radius || squared <= TownFireShadowNear * TownFireShadowNear)
		return 1;
	const auto sample = SelectFace(delta);
	const int centerX = static_cast<int>(std::floor((sample.u + 1) * .5F * TownFireShadowResolution));
	const int centerY = static_cast<int>(std::floor((sample.v + 1) * .5F * TownFireShadowResolution));
	const float diffuse = Normalize(normal) ? std::max(0.0F, Dot(normal, Scale(delta, -1 / std::sqrt(squared)))) : 0;
	const float bias = TownFireShadowDepthBias + TownFireShadowSlopeBias * (1 - diffuse);
	float lit = 0;
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x) {
			const int px = std::clamp(centerX + x, 0, TownFireShadowResolution - 1);
			const int py = std::clamp(centerY + y, 0, TownFireShadowResolution - 1);
			const size_t index = ((lightIndex * 6 + sample.face) * TownFireShadowResolution + py) * TownFireShadowResolution + px;
			const float depth = maps.depth[index];
			if (sample.axial <= depth + bias)
				lit += 1;
		}
	return lit / 9;
}

/** Per-channel diffuse irradiance from the same frozen frame sources. Only
 * intensity/color may differ from Build (deterministic render-time flicker).
 * Position/radius/count mismatch fails closed so stale geometry cannot light
 * another source. No RNG, allocation, palette, alpha, depth or picking access.
 * Attenuation = max(dot(N,L),0)*(1-distanceSquared/radiusSquared)^2 /
 * (1+distanceSquared), multiplied by linear source RGB/intensity/visibility. */
inline TownLightColor SampleTownFireDiffuse(const TownPointShadowMaps &maps, TownLightVector position,
    TownLightVector normal, std::span<const TownPointLight> currentLights, bool shadowsEnabled = true)
{
	using namespace town_point_shadow_detail;
	if (!ValidMaps(maps) || !Finite(position) || !Finite(normal) || !Normalize(normal) || currentLights.size() != maps.lightCount)
		return {};
	TownLightColor result;
	for (size_t i = 0; i < currentLights.size(); ++i) {
		const auto &light = currentLights[i];
		const auto &built = maps.lights[i];
		if (!ValidLight(light) || light.position.x != built.position.x || light.position.height != built.position.height
		    || light.position.z != built.position.z || light.radius != built.radius)
			return {};
		const auto delta = Subtract(light.position, position);
		const float squared = Dot(delta, delta), radiusSquared = light.radius * light.radius;
		if (squared >= radiusSquared || squared <= 1.0e-8F || light.intensity == 0)
			continue;
		const float diffuse = std::max(0.0F, Dot(normal, Scale(delta, 1 / std::sqrt(squared))));
		if (diffuse <= 0)
			continue;
		const float range = 1 - squared / radiusSquared;
		const float amount = diffuse * range * range / (1 + squared) * light.intensity
		    * (shadowsEnabled ? SampleTownPointShadowVisibility(maps, i, position, normal) : 1);
		result.red += light.color.red * amount;
		result.green += light.color.green * amount;
		result.blue += light.color.blue * amount;
	}
	return result;
}

/** Add reflected fire to an already shaded RGB using the material's unlit
 * albedo, in linear light. This can illuminate a dark surface while preserving
 * its authored pigment. Alpha comes byte-exact from base; zero/invalid light
 * returns all original bytes without a gamma roundtrip. No tone mapping beyond
 * per-channel [0,1] saturation, matching the GPU shader's final RGB encoding. */
inline uint32_t TintTownFireDiffuseArgb(uint32_t baseArgb, uint32_t albedoArgb, TownLightColor irradiance)
{
	using namespace town_point_shadow_detail;
	const auto bounded = [](float value) { return std::isfinite(value) && value >= 0 && value <= TownMaxFireShadowLights * 16 * TownMaxPointLightIntensity; };
	if (!bounded(irradiance.red) || !bounded(irradiance.green) || !bounded(irradiance.blue)
	    || (irradiance.red == 0 && irradiance.green == 0 && irradiance.blue == 0))
		return baseArgb;
	const auto channel = [&](unsigned shift, float light) {
		const float base = SrgbToLinear(static_cast<float>((baseArgb >> shift) & 255) / 255);
		const float albedo = SrgbToLinear(static_cast<float>((albedoArgb >> shift) & 255) / 255);
		return static_cast<uint32_t>(std::clamp(LinearToSrgb(base + albedo * light) * 255 + .5F, 0.0F, 255.0F)) << shift;
	};
	return (baseArgb & 0xFF000000U) | channel(16, irradiance.red) | channel(8, irradiance.green) | channel(0, irradiance.blue);
}

} // namespace devilution
