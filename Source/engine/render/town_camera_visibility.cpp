#include "engine/render/town_camera_visibility.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <utility>

namespace devilution {
namespace {

constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
constexpr double ContactRoundoff = 1e-12;
struct D3 { double x, y, z; };
D3 D(TownCameraPoint p) { return { p.x, p.height, p.z }; }
D3 Add(D3 a, D3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
D3 Sub(D3 a, D3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
D3 Mul(D3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
double Dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 Cross(D3 a, D3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double LengthSquared(D3 a) { return Dot(a, a); }
bool Same(TownCameraPoint a, TownCameraPoint b) { return a.x == b.x && a.height == b.height && a.z == b.z; }
bool ValidPoint(TownCameraPoint p)
{
	return std::isfinite(p.x) && std::isfinite(p.height) && std::isfinite(p.z)
	    && std::abs(p.x) <= TownCameraVisibilityIndex::MaxCoordinate
	    && std::abs(p.height) <= TownCameraVisibilityIndex::MaxCoordinate
	    && std::abs(p.z) <= TownCameraVisibilityIndex::MaxCoordinate;
}
bool Degenerate(const TownCameraVisibilityTriangle &triangle)
{
	return LengthSquared(Cross(Sub(D(triangle.vertices[1]), D(triangle.vertices[0])),
	           Sub(D(triangle.vertices[2]), D(triangle.vertices[0])))) == 0;
}

template <typename Bounds> void Extend(Bounds &bounds, TownCameraPoint p)
{
	bounds.minimum.x = std::min(bounds.minimum.x, p.x);
	bounds.minimum.height = std::min(bounds.minimum.height, p.height);
	bounds.minimum.z = std::min(bounds.minimum.z, p.z);
	bounds.maximum.x = std::max(bounds.maximum.x, p.x);
	bounds.maximum.height = std::max(bounds.maximum.height, p.height);
	bounds.maximum.z = std::max(bounds.maximum.z, p.z);
}
template <typename Bounds> Bounds EmptyBounds()
{
	const float inf = std::numeric_limits<float>::infinity();
	return { { inf, inf, inf }, { -inf, -inf, -inf } };
}
template <typename Bounds> bool Contains(const Bounds &b, TownCameraPoint p)
{
	return p.x >= b.minimum.x && p.x <= b.maximum.x
	    && p.height >= b.minimum.height && p.height <= b.maximum.height
	    && p.z >= b.minimum.z && p.z <= b.maximum.z;
}
template <typename Bounds> bool BoundsOverlap(const Bounds &a, const Bounds &b, double margin = 0)
{
	return a.minimum.x <= b.maximum.x + margin && a.maximum.x + margin >= b.minimum.x
	    && a.minimum.height <= b.maximum.height + margin && a.maximum.height + margin >= b.minimum.height
	    && a.minimum.z <= b.maximum.z + margin && a.maximum.z + margin >= b.minimum.z;
}
template <typename Bounds> bool SegmentBounds(D3 a, D3 b, const Bounds &bounds, double radius)
{
	const D3 delta = Sub(b, a);
	const std::array<double, 3> start { a.x, a.y, a.z }, direction { delta.x, delta.y, delta.z };
	const std::array<double, 3> low { bounds.minimum.x, bounds.minimum.height, bounds.minimum.z };
	const std::array<double, 3> high { bounds.maximum.x, bounds.maximum.height, bounds.maximum.z };
	double enter = 0, leave = 1;
	for (size_t axis = 0; axis < 3; ++axis) {
		if (direction[axis] == 0) {
			if (start[axis] < low[axis] - radius || start[axis] > high[axis] + radius)
				return false;
			continue;
		}
		double first = (low[axis] - radius - start[axis]) / direction[axis];
		double last = (high[axis] + radius - start[axis]) / direction[axis];
		if (first > last)
			std::swap(first, last);
		enter = std::max(enter, first);
		leave = std::min(leave, last);
		if (enter > leave)
			return false;
	}
	return true;
}

double PointTriangleSquared(D3 p, const TownCameraVisibilityTriangle &triangle)
{
	const D3 a = D(triangle.vertices[0]), b = D(triangle.vertices[1]), c = D(triangle.vertices[2]);
	const D3 ab = Sub(b, a), ac = Sub(c, a), ap = Sub(p, a);
	const double d1 = Dot(ab, ap), d2 = Dot(ac, ap);
	if (d1 <= 0 && d2 <= 0)
		return LengthSquared(ap);
	const D3 bp = Sub(p, b);
	const double d3 = Dot(ab, bp), d4 = Dot(ac, bp);
	if (d3 >= 0 && d4 <= d3)
		return LengthSquared(bp);
	const double vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0)
		return LengthSquared(Sub(p, Add(a, Mul(ab, d1 / (d1 - d3)))));
	const D3 cp = Sub(p, c);
	const double d5 = Dot(ab, cp), d6 = Dot(ac, cp);
	if (d6 >= 0 && d5 <= d6)
		return LengthSquared(cp);
	const double vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0)
		return LengthSquared(Sub(p, Add(a, Mul(ac, d2 / (d2 - d6)))));
	const double va = d3 * d6 - d5 * d4;
	if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
		const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return LengthSquared(Sub(p, Add(b, Mul(Sub(c, b), w))));
	}
	const double inverse = 1 / (va + vb + vc);
	return LengthSquared(Sub(p, Add(a, Add(Mul(ab, vb * inverse), Mul(ac, vc * inverse)))));
}

struct SegmentPair { D3 first, second; double squared; };
SegmentPair ClosestSegments(D3 a, D3 b, D3 c, D3 d)
{
	const D3 u = Sub(b, a), v = Sub(d, c), r = Sub(a, c);
	const double uu = Dot(u, u), vv = Dot(v, v), uv = Dot(u, v), ur = Dot(u, r), vr = Dot(v, r);
	double s = 0, t = 0;
	if (uu == 0 && vv == 0)
		return { a, c, LengthSquared(r) };
	if (uu == 0) {
		t = std::clamp(vr / vv, 0.0, 1.0);
	} else if (vv == 0) {
		s = std::clamp(-ur / uu, 0.0, 1.0);
	} else {
		const double denominator = uu * vv - uv * uv;
		if (denominator > 0)
			s = std::clamp((uv * vr - ur * vv) / denominator, 0.0, 1.0);
		t = (uv * s + vr) / vv;
		if (t < 0) {
			t = 0;
			s = std::clamp(-ur / uu, 0.0, 1.0);
		} else if (t > 1) {
			t = 1;
			s = std::clamp((uv - ur) / uu, 0.0, 1.0);
		}
	}
	const D3 first = Add(a, Mul(u, s)), second = Add(c, Mul(v, t));
	return { first, second, LengthSquared(Sub(first, second)) };
}

struct RayHit { bool hit = false, ambiguous = false; D3 point {}; };
RayHit SegmentTriangle(D3 start, D3 end, const TownCameraVisibilityTriangle &triangle, bool parity)
{
	const D3 a = D(triangle.vertices[0]), edge1 = Sub(D(triangle.vertices[1]), a), edge2 = Sub(D(triangle.vertices[2]), a);
	const D3 direction = Sub(end, start), p = Cross(direction, edge2);
	const double determinant = Dot(edge1, p);
	const double determinantScale = std::sqrt(LengthSquared(edge1) * LengthSquared(p));
	// An almost-parallel NONZERO determinant is numerically uncertain even
	// when the origin is off the plane. Retry parity with a different direction;
	// never turn a discarded uncertain intersection into a definitive miss.
	if (parity && determinant != 0 && std::abs(determinant) <= determinantScale * 1e-13)
		return { false, true, {} };
	if (determinant == 0) {
		const D3 normal = Cross(edge1, edge2);
		const double plane = Dot(Sub(start, a), normal);
		const bool coplanar = std::abs(plane) <= std::sqrt(LengthSquared(normal)) * 1e-10;
		return { false, parity && coplanar, {} };
	}
	const double inverse = 1 / determinant;
	const D3 s = Sub(start, a), q = Cross(s, edge1);
	const double u = Dot(s, p) * inverse, v = Dot(direction, q) * inverse, t = Dot(edge2, q) * inverse;
	constexpr double Epsilon = 1e-10;
	if (u < -Epsilon || v < -Epsilon || u + v > 1 + Epsilon || t < -Epsilon || t > 1 + Epsilon)
		return {};
	return { true, parity && (u <= Epsilon || v <= Epsilon || u + v >= 1 - Epsilon || t <= Epsilon), Add(start, Mul(direction, t)) };
}

double SegmentTriangleSquared(D3 a, D3 b, const TownCameraVisibilityTriangle &triangle)
{
	if (SegmentTriangle(a, b, triangle, false).hit)
		return 0;
	double result = std::min(PointTriangleSquared(a, triangle), PointTriangleSquared(b, triangle));
	for (size_t edge = 0; edge < 3; ++edge)
		result = std::min(result, ClosestSegments(a, b, D(triangle.vertices[edge]), D(triangle.vertices[(edge + 1) % 3])).squared);
	return result;
}

// Any intersection outside an EXACT shared vertex/edge disqualifies embedded
// containment. The small tolerance is conservative: uncertainty means open-only.
bool ForbiddenIntersection(const TownCameraVisibilityTriangle &a, const TownCameraVisibilityTriangle &b)
{
	std::array<D3, 3> common {};
	size_t count = 0;
	double scale = 1;
	for (const auto &p : a.vertices) {
		for (const auto &q : b.vertices) {
			scale = std::max(scale, std::sqrt(LengthSquared(Sub(D(p), D(q)))));
			if (Same(p, q))
				common[count++] = D(p);
		}
	}
	if (count == 3)
		return true; // Duplicate faces cannot certify volume.
	const double toleranceSquared = scale * scale * 1e-18;
	const auto allowed = [&](D3 p) {
		for (size_t i = 0; i < count; ++i)
			if (LengthSquared(Sub(p, common[i])) <= toleranceSquared)
				return true;
		return count == 2 && ClosestSegments(p, p, common[0], common[1]).squared <= toleranceSquared;
	};
	for (size_t i = 0; i < 3; ++i) {
		if (PointTriangleSquared(D(a.vertices[i]), b) <= toleranceSquared && !allowed(D(a.vertices[i])))
			return true;
		if (PointTriangleSquared(D(b.vertices[i]), a) <= toleranceSquared && !allowed(D(b.vertices[i])))
			return true;
		const D3 a0 = D(a.vertices[i]), a1 = D(a.vertices[(i + 1) % 3]);
		const D3 b0 = D(b.vertices[i]), b1 = D(b.vertices[(i + 1) % 3]);
		const auto ah = SegmentTriangle(a0, a1, b, false), bh = SegmentTriangle(b0, b1, a, false);
		if ((ah.hit && !allowed(ah.point)) || (bh.hit && !allowed(bh.point)))
			return true;
		for (size_t j = 0; j < 3; ++j) {
			const auto pair = ClosestSegments(a0, a1, D(b.vertices[j]), D(b.vertices[(j + 1) % 3]));
			if (pair.squared <= toleranceSquared && (!allowed(pair.first) || !allowed(pair.second)))
				return true;
		}
	}
	return false;
}

class DisjointSet {
public:
	explicit DisjointSet(size_t count) : parent_(count) { std::iota(parent_.begin(), parent_.end(), 0U); }
	uint32_t Find(uint32_t value)
	{
		uint32_t root = value;
		while (parent_[root] != root)
			root = parent_[root];
		while (parent_[value] != value) {
			const uint32_t next = parent_[value];
			parent_[value] = root;
			value = next;
		}
		return root;
	}
	void Unite(uint32_t a, uint32_t b)
	{
		a = Find(a); b = Find(b);
		if (a != b)
			parent_[std::max(a, b)] = std::min(a, b);
	}
private:
	std::vector<uint32_t> parent_;
};

} // namespace

void TownCameraVisibilityIndex::Clear()
{
	std::vector<TownCameraVisibilityTriangle>().swap(triangles_);
	std::vector<uint32_t>().swap(order_);
	std::vector<Node>().swap(nodes_);
	std::vector<Component>().swap(components_);
	epoch_ = 0; ownerCount_ = 0; root_ = 0; ready_ = false;
	skipped_ = certified_ = certificationPairs_ = certificationBudgetComponents_ = 0;
	buildStatus_ = TownCameraVisibilityBuildStatus::Empty;
}

bool TownCameraVisibilityIndex::Build(uint64_t epoch, uint32_t ownerCount, std::vector<TownCameraVisibilityTriangle> triangles)
{
	const auto fail = [&](TownCameraVisibilityBuildStatus status) {
		Clear(); buildStatus_ = status; return false;
	};
	if (ownerCount > MaxOwners || triangles.size() > MaxTriangles)
		return fail(TownCameraVisibilityBuildStatus::LimitExceeded);
	for (const auto &triangle : triangles) {
		if (triangle.owner >= ownerCount || std::any_of(triangle.vertices.begin(), triangle.vertices.end(), [](TownCameraPoint p) { return !ValidPoint(p); }))
			return fail(TownCameraVisibilityBuildStatus::InvalidInput);
	}
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
		TownCameraVisibilityIndex candidate;
		if (!candidate.BuildCandidate(epoch, ownerCount, std::move(triangles)))
			return fail(candidate.buildStatus_);
		*this = std::move(candidate);
		return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (const std::bad_alloc &) {
		return fail(TownCameraVisibilityBuildStatus::AllocationFailure);
	}
#endif
}

uint32_t TownCameraVisibilityIndex::BuildNode(uint32_t begin, uint32_t count)
{
	const uint32_t index = static_cast<uint32_t>(nodes_.size());
	nodes_.push_back({ EmptyBounds<Bounds>(), begin, count, 0, 0 });
	for (uint32_t i = begin; i < begin + count; ++i)
		for (const auto &vertex : triangles_[order_[i]].vertices)
			Extend(nodes_[index].bounds, vertex);
	if (count <= 8)
		return index;
	const Bounds bounds = nodes_[index].bounds;
	const std::array<double, 3> extent { static_cast<double>(bounds.maximum.x) - bounds.minimum.x,
		static_cast<double>(bounds.maximum.height) - bounds.minimum.height, static_cast<double>(bounds.maximum.z) - bounds.minimum.z };
	const size_t axis = static_cast<size_t>(std::max_element(extent.begin(), extent.end()) - extent.begin());
	const auto coordinate = [&](uint32_t triangle) {
		double result = 0;
		for (const auto &p : triangles_[triangle].vertices)
			result += axis == 0 ? p.x : axis == 1 ? p.height : p.z;
		return result;
	};
	const uint32_t middle = begin + count / 2;
	std::nth_element(order_.begin() + begin, order_.begin() + middle, order_.begin() + begin + count,
	    [&](uint32_t a, uint32_t b) { const double ca = coordinate(a), cb = coordinate(b); return ca != cb ? ca < cb : a < b; });
	const uint32_t left = BuildNode(begin, middle - begin), right = BuildNode(middle, begin + count - middle);
	nodes_[index].left = left; nodes_[index].right = right; nodes_[index].count = 0;
	return index;
}

bool TownCameraVisibilityIndex::BuildCandidate(uint64_t epoch, uint32_t ownerCount, std::vector<TownCameraVisibilityTriangle> triangles)
{
	epoch_ = epoch; ownerCount_ = ownerCount;
	const size_t inputCount = triangles.size();
	triangles_.reserve(inputCount);
	for (const auto &triangle : triangles)
		if (!Degenerate(triangle)) triangles_.push_back(triangle);
	skipped_ = inputCount - triangles_.size();
	std::vector<TownCameraVisibilityTriangle>().swap(triangles);
	const uint32_t count = static_cast<uint32_t>(triangles_.size());
	if (count == 0) {
		ready_ = true; buildStatus_ = TownCameraVisibilityBuildStatus::Ready; return true;
	}
	struct VertexRecord { std::array<uint32_t, 4> key; uint32_t corner; };
	std::vector<VertexRecord> vertices;
	vertices.reserve(static_cast<size_t>(count) * 3);
	for (uint32_t i = 0; i < count; ++i) {
		for (uint32_t corner = 0; corner < 3; ++corner) {
			const auto &p = triangles_[i].vertices[corner];
			const auto bits = [](float v) { return std::bit_cast<uint32_t>(v == 0 ? 0.0F : v); };
			vertices.push_back({ { triangles_[i].owner, bits(p.x), bits(p.height), bits(p.z) }, i * 3 + corner });
		}
	}
	std::sort(vertices.begin(), vertices.end(), [](const VertexRecord &a, const VertexRecord &b) { return a.key != b.key ? a.key < b.key : a.corner < b.corner; });
	std::vector<uint32_t> vertexIds(static_cast<size_t>(count) * 3);
	uint32_t vertexId = 0;
	for (size_t i = 0; i < vertices.size(); ++i) {
		if (i != 0 && vertices[i].key != vertices[i - 1].key)
			++vertexId;
		vertexIds[vertices[i].corner] = vertexId;
	}
	struct Edge { uint32_t low, high, triangle, corner; bool ascending; };
	std::vector<Edge> edges;
	edges.reserve(static_cast<size_t>(count) * 3);
	for (uint32_t i = 0; i < count; ++i) {
		for (uint32_t corner = 0; corner < 3; ++corner) {
			const uint32_t from = vertexIds[i * 3 + corner], to = vertexIds[i * 3 + (corner + 1) % 3];
			edges.push_back({ std::min(from, to), std::max(from, to), i, corner, from < to });
		}
	}
	std::sort(edges.begin(), edges.end(), [](const Edge &a, const Edge &b) {
		if (a.low != b.low) return a.low < b.low;
		if (a.high != b.high) return a.high < b.high;
		return a.triangle != b.triangle ? a.triangle < b.triangle : a.corner < b.corner;
	});
	DisjointSet connected(count), fans(static_cast<size_t>(count) * 3);
	std::vector<uint8_t> bad(count, 0);
	for (size_t begin = 0; begin < edges.size();) {
		size_t end = begin + 1;
		while (end < edges.size() && edges[end].low == edges[begin].low && edges[end].high == edges[begin].high)
			++end;
		for (size_t i = begin + 1; i < end; ++i)
			connected.Unite(edges[begin].triangle, edges[i].triangle);
		if (end - begin != 2 || edges[begin].ascending == edges[begin + 1].ascending) {
			for (size_t i = begin; i < end; ++i)
				bad[edges[i].triangle] = 1;
		}
		if (end - begin == 2) {
			const Edge &a = edges[begin], &b = edges[begin + 1];
			for (uint32_t endpoint = 0; endpoint < 2; ++endpoint) {
				const uint32_t cornerA = (a.corner + endpoint) % 3;
				const uint32_t cornerB = (b.corner + (a.ascending == b.ascending ? endpoint : 1 - endpoint)) % 3;
				fans.Unite(a.triangle * 3 + cornerA, b.triangle * 3 + cornerB);
			}
		}
		begin = end;
	}
	std::vector<uint32_t> rootComponent(count, InvalidIndex), triangleComponent(count);
	for (uint32_t i = 0; i < count; ++i) {
		const uint32_t root = connected.Find(i);
		if (rootComponent[root] == InvalidIndex) {
			if (components_.size() == MaxComponents) { buildStatus_ = TownCameraVisibilityBuildStatus::LimitExceeded; return false; }
			rootComponent[root] = static_cast<uint32_t>(components_.size());
			components_.push_back({ EmptyBounds<Bounds>(), triangles_[i].owner, 0, 0, 0, true });
		}
		const uint32_t component = rootComponent[root];
		triangleComponent[i] = component;
		++components_[component].count;
		components_[component].certified &= bad[i] == 0;
		for (const auto &p : triangles_[i].vertices)
			Extend(components_[component].bounds, p);
	}
	// A two-manifold also needs one connected face fan at each vertex.
	struct Fan { uint32_t vertex, component, root; };
	std::vector<Fan> vertexFans;
	vertexFans.reserve(vertices.size());
	for (uint32_t i = 0; i < count * 3; ++i)
		vertexFans.push_back({ vertexIds[i], triangleComponent[i / 3], fans.Find(i) });
	std::sort(vertexFans.begin(), vertexFans.end(), [](const Fan &a, const Fan &b) {
		if (a.vertex != b.vertex) return a.vertex < b.vertex;
		if (a.component != b.component) return a.component < b.component;
		return a.root < b.root;
	});
	for (size_t i = 1; i < vertexFans.size(); ++i)
		if (vertexFans[i].vertex == vertexFans[i - 1].vertex && vertexFans[i].component == vertexFans[i - 1].component
		    && vertexFans[i].root != vertexFans[i - 1].root)
			components_[vertexFans[i].component].certified = false;
	std::vector<double> volumes(components_.size(), 0);
	for (uint32_t i = 0; i < count; ++i) {
		const uint32_t component = triangleComponent[i];
		const D3 origin = D(components_[component].bounds.minimum);
		volumes[component] += Dot(Sub(D(triangles_[i].vertices[0]), origin),
		    Cross(Sub(D(triangles_[i].vertices[1]), origin), Sub(D(triangles_[i].vertices[2]), origin)));
	}
	order_.resize(static_cast<size_t>(count) * 2);
	std::iota(order_.begin(), order_.begin() + count, 0U);
	uint32_t cursor = count;
	std::vector<uint32_t> next(components_.size());
	for (size_t i = 0; i < components_.size(); ++i) {
		auto &component = components_[i];
		component.begin = cursor; next[i] = cursor; cursor += component.count;
		const D3 extent = Sub(D(component.bounds.maximum), D(component.bounds.minimum));
		const double referenceVolume = extent.x * extent.y * extent.z;
		component.certified &= component.count >= 4 && referenceVolume > 0 && std::abs(volumes[i]) > referenceVolume * 1e-12;
	}
	for (uint32_t i = 0; i < count; ++i)
		order_[next[triangleComponent[i]]++] = i;
	nodes_.reserve(static_cast<size_t>(count) * 2 + components_.size());
	root_ = BuildNode(0, count);
	for (auto &component : components_)
		component.root = BuildNode(component.begin, component.count);
	// Embeddedness is checked once, using component BVHs to reject distant pairs.
	// If the cold-build budget is exhausted, keep contacts but deny containment.
	size_t certificationNodes = 0;
	for (auto &component : components_) {
		if (!component.certified)
			continue;
		bool exceeded = false;
		for (uint32_t offset = 0; offset < component.count && component.certified; ++offset) {
			const uint32_t triangleIndex = order_[component.begin + offset];
			Bounds bounds = EmptyBounds<Bounds>();
			for (const auto &p : triangles_[triangleIndex].vertices) Extend(bounds, p);
			std::array<uint32_t, 64> stack {}; size_t size = 1; stack[0] = component.root;
			while (size != 0 && component.certified) {
				if (++certificationNodes > MaxCertificationNodes) { exceeded = true; component.certified = false; break; }
				const Node &node = nodes_[stack[--size]];
				if (!BoundsOverlap(bounds, node.bounds, 1e-8)) continue;
				if (node.count == 0) {
					stack[size++] = node.right; stack[size++] = node.left; continue;
				}
				for (uint32_t i = node.begin; i < node.begin + node.count; ++i) {
					const uint32_t other = order_[i];
					if (other <= triangleIndex) continue;
					Bounds otherBounds = EmptyBounds<Bounds>();
					for (const auto &p : triangles_[other].vertices) Extend(otherBounds, p);
					if (!BoundsOverlap(bounds, otherBounds, 1e-8)) continue;
					if (++certificationPairs_ > MaxCertificationPairs) { exceeded = true; component.certified = false; break; }
					if (ForbiddenIntersection(triangles_[triangleIndex], triangles_[other])) { component.certified = false; break; }
				}
			}
		}
		if (exceeded) ++certificationBudgetComponents_;
		if (component.certified) ++certified_;
	}
	if (bytes() > MaxIndexBytes) { buildStatus_ = TownCameraVisibilityBuildStatus::LimitExceeded; return false; }
	ready_ = true; buildStatus_ = TownCameraVisibilityBuildStatus::Ready;
	return true;
}

size_t TownCameraVisibilityIndex::bytes() const
{
	return sizeof(*this) + triangles_.capacity() * sizeof(TownCameraVisibilityTriangle)
	    + order_.capacity() * sizeof(uint32_t) + nodes_.capacity() * sizeof(Node) + components_.capacity() * sizeof(Component);
}

TownCameraVisibilityStats TownCameraVisibilityIndex::Query(TownCameraPoint eye, TownCameraPoint focus, float nearRadius,
    bool thirdPerson, uint64_t expectedEpoch, std::span<uint8_t> hidden) const
{
	std::fill(hidden.begin(), hidden.end(), uint8_t { 0 });
	TownCameraVisibilityStats stats;
	stats.bytes = bytes(); stats.indexTriangles = triangles_.size(); stats.components = components_.size();
	stats.certifiedComponents = certified_; stats.skippedDegenerateTriangles = skipped_;
	stats.certificationPairs = certificationPairs_; stats.certificationBudgetComponents = certificationBudgetComponents_;
	if (!ready_ || expectedEpoch != epoch_ || hidden.size() != ownerCount_ || !ValidPoint(eye) || !ValidPoint(focus)
	    || !std::isfinite(nearRadius) || nearRadius < 0 || nearRadius > MaxNearRadius)
		return stats;
	const auto fail = [&]() { std::fill(hidden.begin(), hidden.end(), uint8_t { 0 }); stats.hiddenOwners = 0; stats.valid = false; return stats; };
	const auto nodeBudget = [&]() { if (++stats.nodesVisited <= MaxQueryNodes) return true; stats.budgetExceeded = true; return false; };
	const auto triangleBudget = [&]() { if (++stats.trianglesTested <= MaxQueryTriangleTests) return true; stats.budgetExceeded = true; return false; };
	const D3 start = D(eye), end = thirdPerson ? D(focus) : start;
	if (!triangles_.empty()) {
		std::array<uint32_t, 64> stack {}; size_t size = 1; stack[0] = root_;
		while (size != 0) {
			if (!nodeBudget()) return fail();
			const Node &node = nodes_[stack[--size]];
			if (!SegmentBounds(start, end, node.bounds, static_cast<double>(nearRadius) + 1e-6)) continue;
			if (node.count == 0) { stack[size++] = node.right; stack[size++] = node.left; continue; }
			for (uint32_t i = node.begin; i < node.begin + node.count; ++i) {
				const auto &triangle = triangles_[order_[i]];
				if (hidden[triangle.owner] != 0) continue;
				if (!triangleBudget()) return fail();
				if (SegmentTriangleSquared(start, end, triangle) <= static_cast<double>(nearRadius) * nearRadius + ContactRoundoff)
					hidden[triangle.owner] = 1;
			}
		}
	}
	constexpr std::array<D3, 5> Directions { D3 { 1, 0.371390676, 0.618033989 }, D3 { -0.427050983, 1, 0.173205081 },
		D3 { 0.291502622, -0.531128874, 1 }, D3 { 1, -0.693147181, -0.414213562 }, D3 { -0.707106781, -1, 0.223606798 } };
	for (const Component &component : components_) {
		if (!Contains(component.bounds, eye)) continue;
		if (component.certified) ++stats.containmentCertified;
		else { ++stats.containmentUncertified; continue; }
		if (hidden[component.owner] != 0) continue; // UNION, never XOR across shells.
		bool resolved = false;
		const D3 extent = Sub(D(component.bounds.maximum), D(component.bounds.minimum));
		const double length = std::sqrt(LengthSquared(extent)) * 4 + 1;
		for (const D3 direction : Directions) {
			const D3 outside = Add(start, Mul(direction, length));
			size_t intersections = 0;
			bool ambiguous = false;
			std::array<uint32_t, 64> stack {}; size_t size = 1; stack[0] = component.root;
			while (size != 0 && !ambiguous) {
				if (!nodeBudget()) return fail();
				const Node &node = nodes_[stack[--size]];
				if (!SegmentBounds(start, outside, node.bounds, 1e-10)) continue;
				if (node.count == 0) { stack[size++] = node.right; stack[size++] = node.left; continue; }
				for (uint32_t i = node.begin; i < node.begin + node.count; ++i) {
					if (!triangleBudget()) return fail();
					const RayHit hit = SegmentTriangle(start, outside, triangles_[order_[i]], true);
					if (hit.ambiguous) { ambiguous = true; break; }
					if (hit.hit) ++intersections;
				}
			}
			if (!ambiguous) {
				if ((intersections & 1U) != 0) hidden[component.owner] = 1;
				resolved = true; break;
			}
		}
		if (!resolved) { ++stats.containmentAmbiguous; return fail(); }
	}
	stats.hiddenOwners = static_cast<size_t>(std::count(hidden.begin(), hidden.end(), static_cast<uint8_t>(1)));
	stats.valid = true;
	return stats;
}

} // namespace devilution
