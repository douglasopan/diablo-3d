#include "engine/render/town_camera_collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace devilution {
namespace {

struct Vector {
	double x, y, z;
	Vector operator+(Vector b) const { return { x + b.x, y + b.y, z + b.z }; }
	Vector operator-(Vector b) const { return { x - b.x, y - b.y, z - b.z }; }
	Vector operator*(double scale) const { return { x * scale, y * scale, z * scale }; }
};
Vector V(TownCameraPoint p) { return { p.x, p.height, p.z }; }
TownCameraPoint P(Vector p) { return { static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z) }; }
double Dot(Vector a, Vector b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vector Cross(Vector a, Vector b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
bool Finite(TownCameraPoint p) { return std::isfinite(p.x) && std::isfinite(p.height) && std::isfinite(p.z); }
float Axis(TownCameraPoint p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.height : p.z; }
TownCameraPoint Minimum(TownCameraPoint a, TownCameraPoint b) { return { std::min(a.x, b.x), std::min(a.height, b.height), std::min(a.z, b.z) }; }
TownCameraPoint Maximum(TownCameraPoint a, TownCameraPoint b) { return { std::max(a.x, b.x), std::max(a.height, b.height), std::max(a.z, b.z) }; }

Vector ClosestOnTriangle(Vector point, const TownCameraCollisionTriangle &triangle)
{
	const Vector a = V(triangle.vertices[0]), b = V(triangle.vertices[1]), c = V(triangle.vertices[2]);
	const Vector ab = b - a, ac = c - a, ap = point - a;
	const double d1 = Dot(ab, ap), d2 = Dot(ac, ap);
	if (d1 <= 0 && d2 <= 0) return a;
	const Vector bp = point - b;
	const double d3 = Dot(ab, bp), d4 = Dot(ac, bp);
	if (d3 >= 0 && d4 <= d3) return b;
	const double vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
	const Vector cp = point - c;
	const double d5 = Dot(ab, cp), d6 = Dot(ac, cp);
	if (d6 >= 0 && d5 <= d6) return c;
	const double vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
	const double va = d3 * d6 - d5 * d4;
	if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
		return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
	const double inverse = 1 / (va + vb + vc);
	return a + ab * (vb * inverse) + ac * (vc * inverse);
}

bool InsideTriangle(Vector point, const TownCameraCollisionTriangle &triangle, Vector normal)
{
	for (size_t i = 0; i < 3; ++i) {
		const Vector a = V(triangle.vertices[i]), b = V(triangle.vertices[(i + 1) % 3]);
		if (Dot(Cross(b - a, point - a), normal) < -1e-9)
			return false;
	}
	return true;
}

template <typename Accept>
void QuadraticContacts(double a, double b, double c, double maximum, Accept accept)
{
	if (a <= 1e-24) return;
	const double discriminant = b * b - 4 * a * c;
	if (discriminant < 0) return;
	const double root = std::sqrt(discriminant);
	const double q = -0.5 * (b + std::copysign(root, b));
	const double first = q == 0 ? -b / (2 * a) : q / a;
	const double second = q == 0 ? first : c / q;
	for (const double t : { std::min(first, second), std::max(first, second) })
		if (t >= 0 && t <= maximum && 2 * a * t + b < -1e-14) accept(t);
}

double SweepTriangle(Vector start, Vector delta, double radius, const TownCameraCollisionTriangle &triangle,
    double maximum, bool &initialOverlap)
{
	const Vector nearest = ClosestOnTriangle(start, triangle), difference = start - nearest;
	if (Dot(difference, difference) < radius * radius) {
		initialOverlap = true;
		return 0;
	}
	const Vector a = V(triangle.vertices[0]), b = V(triangle.vertices[1]), c = V(triangle.vertices[2]);
	Vector normal = Cross(b - a, c - a);
	normal = normal * (1 / std::sqrt(Dot(normal, normal)));
	const double signedStart = Dot(start - a, normal), speed = Dot(delta, normal);
	double result = maximum;
	if (std::abs(speed) > 1e-14) {
		for (const double sign : { -1.0, 1.0 }) {
			const double t = (sign * radius - signedStart) / speed;
			if (speed * sign < 0 && t >= 0 && t <= result && InsideTriangle(start + delta * t - normal * (sign * radius), triangle, normal))
				result = t;
		}
	}
	for (size_t i = 0; i < 3; ++i) {
		const Vector vertex = V(triangle.vertices[i]), edge = V(triangle.vertices[(i + 1) % 3]) - vertex;
		const Vector relative = start - vertex;
		QuadraticContacts(Dot(delta, delta), 2 * Dot(relative, delta), Dot(relative, relative) - radius * radius,
		    result, [&](double t) { result = std::min(result, t); });
		const double lengthSquared = Dot(edge, edge);
		if (lengthSquared <= 1e-20) continue;
		const double alongStart = Dot(relative, edge) / lengthSquared, alongDelta = Dot(delta, edge) / lengthSquared;
		const Vector perpendicular = relative - edge * alongStart, direction = delta - edge * alongDelta;
		QuadraticContacts(Dot(direction, direction), 2 * Dot(perpendicular, direction),
		    Dot(perpendicular, perpendicular) - radius * radius, result, [&](double t) {
			    const double along = alongStart + alongDelta * t;
			    if (along >= 0 && along <= 1) result = std::min(result, t);
		    });
	}
	return result;
}

bool SegmentBounds(TownCameraPoint start, TownCameraPoint end, TownCameraPoint minimum,
    TownCameraPoint maximum, float radius, double limit)
{
	double low = 0, high = limit;
	for (int axis = 0; axis < 3; ++axis) {
		const double origin = Axis(start, axis), delta = static_cast<double>(Axis(end, axis)) - origin;
		const double min = static_cast<double>(Axis(minimum, axis)) - radius;
		const double max = static_cast<double>(Axis(maximum, axis)) + radius;
		if (std::abs(delta) < 1e-20) {
			if (origin < min || origin > max) return false;
		} else {
			double a = (min - origin) / delta, b = (max - origin) / delta;
			if (a > b) std::swap(a, b);
			low = std::max(low, a); high = std::min(high, b);
			if (low > high) return false;
		}
	}
	return true;
}

} // namespace

void TownCameraCollisionIndex::Clear()
{
	triangles_.clear(); order_.clear(); nodes_.clear(); skipped_ = 0;
}

void TownCameraCollisionIndex::Build(std::vector<TownCameraCollisionTriangle> triangles)
{
	Clear();
	triangles_ = std::move(triangles);
	const size_t before = triangles_.size();
	std::erase_if(triangles_, [](const TownCameraCollisionTriangle &triangle) {
		if (!std::all_of(triangle.vertices.begin(), triangle.vertices.end(), Finite)) return true;
		const Vector ab = V(triangle.vertices[1]) - V(triangle.vertices[0]);
		const Vector ac = V(triangle.vertices[2]) - V(triangle.vertices[0]);
		const Vector normal = Cross(ab, ac);
		return Dot(normal, normal) <= 1e-20;
	});
	skipped_ = before - triangles_.size();
	if (triangles_.empty()) return;
	order_.resize(triangles_.size());
	std::iota(order_.begin(), order_.end(), 0U);
	nodes_.reserve(triangles_.size());
	BuildNode(0, static_cast<uint32_t>(triangles_.size()));
}

uint32_t TownCameraCollisionIndex::BuildNode(uint32_t begin, uint32_t count)
{
	const float infinity = std::numeric_limits<float>::infinity();
	Bounds bounds { { infinity, infinity, infinity }, { -infinity, -infinity, -infinity } };
	for (uint32_t i = begin; i < begin + count; ++i)
		for (TownCameraPoint point : triangles_[order_[i]].vertices) {
			bounds.minimum = Minimum(bounds.minimum, point);
			bounds.maximum = Maximum(bounds.maximum, point);
		}
	const uint32_t index = static_cast<uint32_t>(nodes_.size());
	nodes_.push_back({ bounds, begin, count, 0, 0 });
	if (count <= 8) return index;
	const TownCameraPoint extent = bounds.maximum - bounds.minimum;
	const int axis = extent.x >= extent.height && extent.x >= extent.z ? 0 : extent.height >= extent.z ? 1 : 2;
	const uint32_t middle = begin + count / 2;
	std::nth_element(order_.begin() + begin, order_.begin() + middle, order_.begin() + begin + count,
	    [&](uint32_t a, uint32_t b) {
		    const auto centroid = [&](uint32_t triangle) {
			    const auto &points = triangles_[triangle].vertices;
			    return static_cast<double>(Axis(points[0], axis)) + Axis(points[1], axis) + Axis(points[2], axis);
		    };
		    const double ca = centroid(a), cb = centroid(b);
		    return ca == cb ? a < b : ca < cb;
	    });
	const uint32_t left = BuildNode(begin, middle - begin), right = BuildNode(middle, begin + count - middle);
	nodes_[index].count = 0; nodes_[index].left = left; nodes_[index].right = right;
	return index;
}

TownCameraCollisionHit TownCameraCollisionIndex::Sweep(TownCameraPoint start, TownCameraPoint end, float radius) const
{
	TownCameraCollisionHit hit;
	if (!Finite(start) || !Finite(end) || !std::isfinite(radius) || radius <= 0 || radius > 8) {
		hit.valid = false; hit.fraction = 0; return hit;
	}
	if (nodes_.empty()) return hit;
	const Vector origin = V(start), delta = V(end) - origin;
	double fraction = 1;
	std::array<uint32_t, 64> stack;
	size_t pending = 1;
	stack[0] = 0;
	while (pending != 0) {
		const Node &node = nodes_[stack[--pending]];
		++hit.nodesVisited;
		if (!SegmentBounds(start, end, node.bounds.minimum, node.bounds.maximum, radius, fraction)) continue;
		if (node.count != 0) {
			for (uint32_t i = node.begin; i < node.begin + node.count; ++i) {
				++hit.trianglesTested;
				fraction = std::min(fraction, SweepTriangle(origin, delta, radius, triangles_[order_[i]], fraction, hit.initialOverlap));
			}
		} else {
			if (pending + 2 > stack.size()) { hit.valid = false; fraction = 0; break; }
			stack[pending++] = node.right; stack[pending++] = node.left;
		}
	}
	hit.fraction = static_cast<float>(std::clamp(fraction, 0.0, 1.0));
	return hit;
}

TownCameraCollisionPosition TownCameraCollisionIndex::Separate(TownCameraPoint point, TownCameraPoint preferredSide, float radius) const
{
	TownCameraCollisionPosition result { point, false, 0 };
	if (!Finite(point) || !Finite(preferredSide) || !std::isfinite(radius) || radius <= 0 || radius > 8) return result;
	Vector position = V(point);
	for (int iteration = 0; iteration < 12; ++iteration) {
		// Collision publication uses float world coordinates. Test the actual
		// representable position, not a double point that could round into a wall.
		position = V(P(position));
		double deepest = 0;
		Vector correction {};
		std::array<uint32_t, 64> stack;
		size_t pending = nodes_.empty() ? 0 : 1;
		stack[0] = 0;
		while (pending != 0) {
			const Node &node = nodes_[stack[--pending]];
			if (!SegmentBounds(P(position), P(position), node.bounds.minimum, node.bounds.maximum, radius, 1)) continue;
			if (node.count != 0) {
				for (uint32_t i = node.begin; i < node.begin + node.count; ++i) {
					++result.trianglesTested;
					const auto &triangle = triangles_[order_[i]];
					const Vector closest = ClosestOnTriangle(position, triangle), difference = position - closest;
					const double distance = std::sqrt(Dot(difference, difference)), penetration = radius - distance;
					if (penetration <= deepest) continue;
					Vector direction;
					if (distance > 1e-10) direction = difference * (1 / distance);
					else {
						direction = Cross(V(triangle.vertices[1]) - V(triangle.vertices[0]), V(triangle.vertices[2]) - V(triangle.vertices[0]));
						direction = direction * (1 / std::sqrt(Dot(direction, direction)));
						if (Dot(direction, V(preferredSide) - closest) < 0) direction = direction * -1;
					}
					deepest = penetration; correction = direction * (penetration + 0.001);
				}
			} else {
				if (pending + 2 > stack.size()) return result;
				stack[pending++] = node.right; stack[pending++] = node.left;
			}
		}
		if (deepest <= 0) {
			const TownCameraPoint published = P(position);
			const auto validation = Sweep(published, published, radius);
			result.trianglesTested += validation.trianglesTested;
			if (!validation.valid || validation.initialOverlap) return result;
			result.point = published; result.resolved = true; return result;
		}
		position = position + correction;
		if (Dot(position - V(point), position - V(point)) > 4) return result;
	}
	return result;
}

size_t TownCameraCollisionIndex::bytes() const
{
	return triangles_.capacity() * sizeof(TownCameraCollisionTriangle) + order_.capacity() * sizeof(uint32_t) + nodes_.capacity() * sizeof(Node);
}

float TownCameraCollisionRadius(const TownCameraFrame &frame)
{
	if (!frame.valid || !frame.perspective || frame.width <= 0 || frame.height <= 0 || frame.focalPixels <= 0) return 0;
	const double horizontal = std::max(frame.centerX, frame.width - frame.centerX) * frame.nearClip / frame.focalPixels;
	const double vertical = std::max(frame.centerY, frame.height - frame.centerY) * frame.nearClip / frame.focalPixels;
	return static_cast<float>(std::sqrt(horizontal * horizontal + vertical * vertical + frame.nearClip * frame.nearClip) + 0.03);
}

} // namespace devilution
