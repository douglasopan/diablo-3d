#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/render/town_camera.hpp"

namespace devilution {

struct TownCameraVisibilityTriangle {
	std::array<TownCameraPoint, 3> vertices;
	uint32_t owner = 0;
};

enum class TownCameraVisibilityBuildStatus : uint8_t {
	Empty,
	Ready,
	InvalidInput,
	LimitExceeded,
	AllocationFailure
};

struct TownCameraVisibilityStats {
	bool valid = false;
	bool budgetExceeded = false;
	size_t hiddenOwners = 0;
	// Components whose geometry bounds contain the eye (not owners).
	size_t containmentCertified = 0;
	size_t containmentUncertified = 0;
	size_t containmentAmbiguous = 0;
	size_t nodesVisited = 0;
	size_t trianglesTested = 0;
	size_t bytes = 0;
	size_t indexTriangles = 0;
	size_t components = 0;
	size_t certifiedComponents = 0;
	size_t skippedDegenerateTriangles = 0;
	size_t certificationPairs = 0;
	size_t certificationBudgetComponents = 0;
};

/** Private prototype, not integrated. The current caller scope is Tristram
 * architecture only. An owner is a placement, never a shared texture/asset ID.
 * Build takes the COMPLETE immutable geometry, including hidden owners. This
 * module never moves the camera or edits scene, shadows, lighting, picking,
 * native collision, visibility, simulation, options or multiplayer state.
 *
 * Exact float seams are welded per owner; +0/-0 are equal. No epsilon welding.
 * Containment is certified per connected component only after orientable closed
 * edge/vertex-manifold, nonzero-volume and bounded self-intersection validation.
 * Uncertified/open components still participate in sphere/segment contact.
 * Several closed components of one owner are a UNION, including nested shells.
 */
class TownCameraVisibilityIndex {
public:
	static constexpr size_t MaxTriangles = 131072;
	static constexpr uint32_t MaxOwners = 4096;
	static constexpr size_t MaxComponents = 8192;
	static constexpr size_t MaxQueryNodes = 524288;
	static constexpr size_t MaxQueryTriangleTests = 262144;
	static constexpr size_t MaxCertificationNodes = 8388608;
	static constexpr size_t MaxCertificationPairs = 1048576;
	static constexpr size_t MaxIndexBytes = 32 * 1024 * 1024;
	static constexpr float MaxCoordinate = 1048576;
	static constexpr float MaxNearRadius = 64;

	/** Publishes the candidate only after complete construction. Any failure
	 * invalidates the previous index. Invalid/over-limit inputs are rejected
	 * before internal allocation. Retained data is bounded by MaxIndexBytes;
	 * caller-owned input capacity is not adopted. Exact zero-area triangles are omitted; their
	 * absence participates in closure validation. Empty geometry is valid.
	 * Catches bad_alloc when exceptions are enabled. MSVC integration requires
	 * /EHsc for this source; a no-exceptions allocator failure cannot be caught.
	 */
	bool Build(uint64_t epoch, uint32_t ownerCount, std::vector<TownCameraVisibilityTriangle> triangles);
	void Clear();
	bool ready() const { return ready_; }
	uint32_t ownerCount() const { return ownerCount_; }
	uint64_t sceneEpoch() const { return epoch_; }
	TownCameraVisibilityBuildStatus buildStatus() const { return buildStatus_; }
	size_t bytes() const;

	/** No allocations or state mutation. Clears the ENTIRE supplied span before
	 * validation and on every failure (including budget/epoch/size mismatch).
	 * Span length must equal ownerCount. Values are exactly zero or one.
	 * thirdPerson=true queries the eye-to-focus capsule with nearRadius. The
	 * caller also sets it during a transition toward first person while a visual
	 * boom remains. false queries ONLY the eye sphere plus containment, never
	 * a forward sight ray (settled first person).
	 * Input coordinates must be finite within MaxCoordinate; radius is [0,64].
	 * The caller may supply a separate hysteresis margin. Contact tolerance is
	 * only 1e-12 squared world units for floating-point boundary roundoff.
	 * Freeze this result for color/depth/pick, including a CPU/GPU replay.
	 */
	TownCameraVisibilityStats Query(TownCameraPoint eye, TownCameraPoint focus, float nearRadius,
	    bool thirdPerson, uint64_t expectedEpoch, std::span<uint8_t> hidden) const;

private:
	struct Bounds { TownCameraPoint minimum, maximum; };
	struct Node {
		Bounds bounds;
		uint32_t begin = 0, count = 0, left = 0, right = 0;
	};
	struct Component {
		Bounds bounds;
		uint32_t owner = 0, root = 0, begin = 0, count = 0;
		bool certified = false;
	};
	bool BuildCandidate(uint64_t epoch, uint32_t ownerCount, std::vector<TownCameraVisibilityTriangle> triangles);
	uint32_t BuildNode(uint32_t begin, uint32_t count);
	std::vector<TownCameraVisibilityTriangle> triangles_;
	// Global BVH followed by component BVHs; each owns a disjoint order range.
	std::vector<uint32_t> order_;
	std::vector<Node> nodes_;
	std::vector<Component> components_;
	uint64_t epoch_ = 0;
	uint32_t ownerCount_ = 0, root_ = 0;
	bool ready_ = false;
	TownCameraVisibilityBuildStatus buildStatus_ = TownCameraVisibilityBuildStatus::Empty;
	size_t skipped_ = 0, certified_ = 0, certificationPairs_ = 0, certificationBudgetComponents_ = 0;
};

} // namespace devilution
