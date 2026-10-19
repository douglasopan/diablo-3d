#include "levels/surface_region_obstacles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace devilution::surface {
namespace {

#include "levels/surface_region_obstacle_catalog.inc"

constexpr double Epsilon = 1e-12;
constexpr double RadiusNative = TraversalBodyRadiusMetres / CellMetres;
constexpr double RadiusSquared = RadiusNative * RadiusNative;

bool ValidPosition(double x, double z) noexcept
{
	constexpr double minimum = -MaximumRegionCoordinate * InteriorSize - 1;
	constexpr double maximum = MaximumRegionCoordinate * InteriorSize + StorageSize - 1;
	return std::isfinite(x) && std::isfinite(z) && x >= minimum && z >= minimum && x <= maximum && z <= maximum;
}

double Cross(ObstaclePoint a, ObstaclePoint b, ObstaclePoint c) noexcept
{
	return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

double PointSegmentSquared(ObstaclePoint p, ObstaclePoint a, ObstaclePoint b) noexcept
{
	const double dx = b.x - a.x, dz = b.z - a.z;
	const double length = dx * dx + dz * dz;
	const double t = length > 0 ? std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / length, 0.0, 1.0) : 0;
	const double x = p.x - a.x - t * dx, z = p.z - a.z - t * dz;
	return x * x + z * z;
}

bool SegmentsIntersect(ObstaclePoint a, ObstaclePoint b, ObstaclePoint c, ObstaclePoint d) noexcept
{
	if (std::max(a.x, b.x) + Epsilon < std::min(c.x, d.x) || std::max(c.x, d.x) + Epsilon < std::min(a.x, b.x)
	    || std::max(a.z, b.z) + Epsilon < std::min(c.z, d.z) || std::max(c.z, d.z) + Epsilon < std::min(a.z, b.z))
		return false;
	const double abC = Cross(a, b, c), abD = Cross(a, b, d), cdA = Cross(c, d, a), cdB = Cross(c, d, b);
	return ((abC >= -Epsilon && abD <= Epsilon) || (abD >= -Epsilon && abC <= Epsilon))
	    && ((cdA >= -Epsilon && cdB <= Epsilon) || (cdB >= -Epsilon && cdA <= Epsilon));
}

bool Inside(ObstaclePoint p, const ObstaclePolygon &polygon) noexcept
{
	if (polygon.pointCount < 3)
		return false;
	for (uint32_t i = 0; i < polygon.pointCount; ++i) {
		if (Cross(CatalogPoints[polygon.firstPoint + i], CatalogPoints[polygon.firstPoint + (i + 1) % polygon.pointCount], p) < -Epsilon)
			return false;
	}
	return true;
}

double SegmentPolygonSquared(ObstaclePoint a, ObstaclePoint b, const ObstaclePolygon &polygon) noexcept
{
	if (Inside(a, polygon) || Inside(b, polygon))
		return 0;
	double minimum = std::numeric_limits<double>::infinity();
	for (uint32_t i = 0; i < polygon.pointCount; ++i) {
		const auto c = CatalogPoints[polygon.firstPoint + i];
		const auto d = CatalogPoints[polygon.firstPoint + (i + 1) % polygon.pointCount];
		if (SegmentsIntersect(a, b, c, d))
			return 0;
		minimum = std::min({ minimum, PointSegmentSquared(a, c, d), PointSegmentSquared(b, c, d),
		    PointSegmentSquared(c, a, b), PointSegmentSquared(d, a, b) });
	}
	return minimum;
}

uint16_t BlockingObstacle(ObstaclePoint a, ObstaclePoint b) noexcept
{
	const double minX = std::min(a.x, b.x) - RadiusNative, maxX = std::max(a.x, b.x) + RadiusNative;
	const double minZ = std::min(a.z, b.z) - RadiusNative, maxZ = std::max(a.z, b.z) + RadiusNative;
	for (const auto &obstacle : CatalogObstacles) {
		if (obstacle.maxX < minX || obstacle.minX > maxX || obstacle.maxZ < minZ || obstacle.minZ > maxZ)
			continue; // Culling only; contact always tests the real authored polygons.
		for (uint32_t i = obstacle.firstPolygon; i < obstacle.firstPolygon + obstacle.polygonCount; ++i) {
			if (SegmentPolygonSquared(a, b, CatalogPolygons[i]) <= RadiusSquared + Epsilon)
				return obstacle.id;
		}
	}
	return 0;
}

} // namespace

std::span<const SurfaceObstacle> AuthoredSurfaceObstacles() noexcept { return CatalogObstacles; }
std::span<const ObstaclePolygon> AuthoredSurfaceObstaclePolygons() noexcept { return CatalogPolygons; }
std::span<const ObstaclePoint> AuthoredSurfaceObstaclePoints() noexcept { return CatalogPoints; }
std::string_view SurfaceObstacleSourceAssetSha256() noexcept { return CatalogSourceAssetSha256; }

std::optional<WorldCellSemantics> GetWorldCellSemantics(int32_t x, int32_t z) noexcept
{
	constexpr int32_t minimumLabel = -MaximumRegionCoordinate * InteriorSize;
	if (!ValidPosition(x, z) || x < minimumLabel || z < minimumLabel)
		return std::nullopt;
	const CellKind material = IsRoadWorldCell(x, z) ? CellKind::Road : CellKind::Grass;
	const uint16_t obstacle = BlockingObstacle({ double(x), double(z) }, { double(x), double(z) });
	return WorldCellSemantics { obstacle != 0 ? CellKind::Blocked : material, material, obstacle };
}

bool CanTraverseWorldSegment(double fromX, double fromZ, double toX, double toZ) noexcept
{
	return ValidPosition(fromX, fromZ) && ValidPosition(toX, toZ)
	    && BlockingObstacle({ fromX, fromZ }, { toX, toZ }) == 0;
}

std::optional<double> WorldObstacleClearanceMetres(double x, double z) noexcept
{
	if (!ValidPosition(x, z))
		return std::nullopt;
	double minimum = std::numeric_limits<double>::infinity();
	const ObstaclePoint point { x, z };
	for (const auto &polygon : CatalogPolygons)
		minimum = std::min(minimum, SegmentPolygonSquared(point, point, polygon));
	return std::sqrt(minimum) * CellMetres;
}

} // namespace devilution::surface
