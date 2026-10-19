#include "engine/render/town_scene_projection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace devilution {
namespace {

constexpr double EdgeTolerance = 0.000001;
constexpr float MinimumDepthTolerance = 0.00001F;
constexpr size_t MaximumTexels = 8'000'000;

struct Projected {
	double x;
	double y;
	double depth;
};

bool PaintEligible(const TownSceneTriangle &triangle)
{
	return triangle.nativeProjection && triangle.surfaceRole == TownSceneSurfaceRole::Exterior;
}

Projected Project(const TownSceneVertex &vertex, Point referenceTile, Point origin)
{
	const double x = static_cast<double>(vertex.x) - referenceTile.x;
	const double z = static_cast<double>(vertex.z) - referenceTile.y;
	return { 32 * (x - z) - origin.x, 16 * (x + z) - 32 * vertex.height - origin.y,
		x + z + vertex.height };
}

double Edge(const Projected &a, const Projected &b, double x, double y)
{
	return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}

void Rasterize(const TownSceneTriangle &triangle, int32_t owner, Point referenceTile,
	Point origin, int width, int height, std::vector<float> &depth,
	std::vector<int32_t> &owners)
{
	std::array<Projected, 3> points;
	for (size_t i = 0; i < points.size(); ++i) {
		points[i] = Project(triangle.vertices[i], referenceTile, origin);
		if (!std::isfinite(points[i].x) || !std::isfinite(points[i].y) || !std::isfinite(points[i].depth))
			return;
	}
	const Projected &a = points[0], &b = points[1], &c = points[2];
	const double area = Edge(a, b, c.x, c.y);
	if (std::abs(area) < 0.000001)
		return;
	const double minX = std::min({ a.x, b.x, c.x }), maxX = std::max({ a.x, b.x, c.x });
	const double minY = std::min({ a.y, b.y, c.y }), maxY = std::max({ a.y, b.y, c.y });
	if (maxX < 0 || maxY < 0 || minX >= width || minY >= height)
		return;
	const int left = static_cast<int>(std::floor(std::max(0.0, minX)));
	const int top = static_cast<int>(std::floor(std::max(0.0, minY)));
	const int right = static_cast<int>(std::ceil(std::min(static_cast<double>(width - 1), maxX)));
	const int bottom = static_cast<int>(std::ceil(std::min(static_cast<double>(height - 1), maxY)));
	const double inverseArea = 1 / area;
	for (int y = top; y <= bottom; ++y) {
		for (int x = left; x <= right; ++x) {
			const double px = x + 0.5, py = y + 0.5;
			const double wa = Edge(b, c, px, py) * inverseArea;
			const double wb = Edge(c, a, px, py) * inverseArea;
			const double wc = 1 - wa - wb;
			if (wa < -EdgeTolerance || wb < -EdgeTolerance || wc < -EdgeTolerance)
				continue;
			const float candidate = static_cast<float>(wa * a.depth + wb * b.depth + wc * c.depth);
			const size_t pixel = static_cast<size_t>(y) * width + x;
			if (std::isfinite(depth[pixel])) {
				const float tolerance = std::max(MinimumDepthTolerance,
					4 * std::numeric_limits<float>::epsilon() * std::max(std::abs(candidate), std::abs(depth[pixel])));
				if (candidate <= depth[pixel] + tolerance)
					continue; // Deterministic first triangle wins coplanar ties.
			}
			depth[pixel] = candidate;
			owners[pixel] = owner;
		}
	}
}

} // namespace

std::vector<int32_t> BuildTownSceneProjectionOwners(const TownSceneModel &model,
	Point referenceTile, Point pixelOrigin, int width, int height,
	std::span<const TownSceneModel> occluders)
{
	if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
		return {};
	const size_t size = static_cast<size_t>(width) * height;
	if (size > MaximumTexels)
		return {};
	std::vector<int32_t> owners(size, -1);
	std::vector<float> depth(size, -std::numeric_limits<float>::infinity());
	for (size_t i = 0; i < model.triangles.size(); ++i) {
		const TownSceneTriangle &triangle = model.triangles[i];
		const int32_t owner = PaintEligible(triangle) && i <= static_cast<size_t>(std::numeric_limits<int32_t>::max())
		    ? static_cast<int32_t>(i) : -1;
		Rasterize(triangle, owner, referenceTile, pixelOrigin, width, height, depth, owners);
	}
	for (const TownSceneModel &other : occluders) {
		if (&other == &model)
			continue;
		for (const TownSceneTriangle &triangle : other.triangles)
			Rasterize(triangle, -1, referenceTile, pixelOrigin, width, height, depth, owners);
	}
	return owners;
}

bool TownSceneProjectionOwnerMatches(const TownSceneModel &model,
	int32_t owner, size_t triangleIndex)
{
	if (owner < 0 || static_cast<size_t>(owner) >= model.triangles.size() || triangleIndex >= model.triangles.size())
		return false;
	const TownSceneTriangle &visible = model.triangles[static_cast<size_t>(owner)];
	const TownSceneTriangle &target = model.triangles[triangleIndex];
	if (!PaintEligible(visible) || !PaintEligible(target))
		return false;
	if (static_cast<size_t>(owner) == triangleIndex)
		return true;
	if (visible.material != target.material || visible.surfaceRole != target.surfaceRole)
		return false;
	const auto &normal = visible.normal;
	const auto &otherNormal = target.normal;
	const float alignment = normal.x * otherNormal.x + normal.height * otherNormal.height + normal.z * otherNormal.z;
	if (!std::isfinite(alignment) || alignment < 0.99999F)
		return false;
	const TownSceneVertex &origin = visible.vertices[0];
	for (const TownSceneVertex &vertex : target.vertices) {
		const double distance = normal.x * (static_cast<double>(vertex.x) - origin.x)
		    + normal.height * (static_cast<double>(vertex.height) - origin.height)
		    + normal.z * (static_cast<double>(vertex.z) - origin.z);
		if (!std::isfinite(distance) || std::abs(distance) > 0.0001)
			return false;
	}
	return true;
}

} // namespace devilution
