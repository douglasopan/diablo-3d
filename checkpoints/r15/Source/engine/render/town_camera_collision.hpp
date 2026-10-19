#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/render/town_camera.hpp"

namespace devilution {

struct TownCameraCollisionTriangle {
	std::array<TownCameraPoint, 3> vertices;
};

struct TownCameraCollisionHit {
	float fraction = 1;
	bool valid = true;
	bool initialOverlap = false;
	size_t nodesVisited = 0;
	size_t trianglesTested = 0;
};

struct TownCameraCollisionPosition {
	TownCameraPoint point;
	bool resolved = false;
	size_t trianglesTested = 0;
};

/** Visual-only, two-sided architecture surfaces. Owns a BVH built once per
 * assembled-scene revision. Queries allocate nothing and never read simulation.
 * Bounds are broad phase only; windows and other actual openings remain empty. */
class TownCameraCollisionIndex {
public:
	void Build(std::vector<TownCameraCollisionTriangle> triangles);
	void Clear();
	TownCameraCollisionHit Sweep(TownCameraPoint start, TownCameraPoint end, float radius) const;
	/** Bounded local separation for near-plane spheres initially touching a wall.
	 * preferredSide breaks an exactly-on-surface side ambiguity. Temporal crossing
	 * must still be prevented with Sweep from the previous safe eye. Failure is explicit;
	 * callers must retain a safe position rather than accept penetration. */
	TownCameraCollisionPosition Separate(TownCameraPoint point, TownCameraPoint preferredSide, float radius) const;
	size_t triangleCount() const { return triangles_.size(); }
	size_t skippedTriangles() const { return skipped_; }
	size_t bytes() const;

private:
	struct Bounds { TownCameraPoint minimum, maximum; };
	struct Node {
		Bounds bounds;
		uint32_t begin = 0, count = 0, left = 0, right = 0;
	};
	uint32_t BuildNode(uint32_t begin, uint32_t count);
	std::vector<TownCameraCollisionTriangle> triangles_;
	std::vector<uint32_t> order_;
	std::vector<Node> nodes_;
	size_t skipped_ = 0;
};

/** Sphere enclosing all corners of the near plane, including its eye offset.
 * This protects wide-FOV/aspect corners, rather than testing the eye point only. */
float TownCameraCollisionRadius(const TownCameraFrame &frame);

} // namespace devilution
