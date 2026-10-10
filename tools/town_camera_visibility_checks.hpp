#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

#include "engine/render/town_camera_visibility.hpp"

namespace devilution {
namespace town_camera_visibility_checks {

inline void Cube(std::vector<TownCameraVisibilityTriangle> &triangles, uint32_t owner, TownCameraPoint minimum, TownCameraPoint maximum)
{
	const std::array<TownCameraPoint, 8> p { TownCameraPoint { minimum.x, minimum.height, minimum.z },
		{ maximum.x, minimum.height, minimum.z }, { maximum.x, maximum.height, minimum.z }, { minimum.x, maximum.height, minimum.z },
		{ minimum.x, minimum.height, maximum.z }, { maximum.x, minimum.height, maximum.z },
		{ maximum.x, maximum.height, maximum.z }, { minimum.x, maximum.height, maximum.z } };
	const auto quad = [&](size_t a, size_t b, size_t c, size_t d) {
		triangles.push_back({ { p[a], p[b], p[c] }, owner });
		triangles.push_back({ { p[a], p[c], p[d] }, owner });
	};
	quad(0, 3, 2, 1); quad(4, 5, 6, 7); quad(0, 4, 7, 3);
	quad(1, 2, 6, 5); quad(0, 1, 5, 4); quad(3, 7, 6, 2);
}

inline std::vector<TownCameraVisibilityTriangle> Tetrahedron()
{
	const TownCameraPoint a { 0, 0, 0 }, b { 2, 0, 0 }, c { 0, 2, 0 }, d { 0, 0, 2 };
	return { { { a, c, b }, 0 }, { { a, b, d }, 0 }, { { a, d, c }, 0 }, { { b, c, d }, 0 } };
}

} // namespace town_camera_visibility_checks

/** Private fixture only, not integrated and not executed in this preparation.
 * The caller supplies check(bool condition, const char *label), owns counting
 * and reporting, and invokes RunTownCameraVisibilityChecks(check). No runner,
 * SDL, MPQ, world state, GPU, files or simulation are used by this header.
 */
template <typename Checker> void RunTownCameraVisibilityChecks(Checker &&check)
{
	using town_camera_visibility_checks::Cube;
	using town_camera_visibility_checks::Tetrahedron;
	using Index = TownCameraVisibilityIndex;
	const float nan = std::numeric_limits<float>::quiet_NaN(), infinity = std::numeric_limits<float>::infinity();
	Index index;
	std::array<uint8_t, 2> mask { 7, 9 };
	check(!index.Query({}, {}, 0, false, 7, mask).valid && mask == std::array<uint8_t, 2> { 0, 0 }, "unready index clears stale mask");
	std::vector<TownCameraVisibilityTriangle> cubes;
	Cube(cubes, 0, { -1, -1, -1 }, { 1, 1, 1 });
	Cube(cubes, 1, { 3, -1, -1 }, { 5, 1, 1 });
	check(index.Build(7, 2, cubes) && index.ready() && index.ownerCount() == 2 && index.sceneEpoch() == 7, "build publishes full owner/epoch contract");
	auto stats = index.Query({ -3, 0, 0 }, { 7, 0, 0 }, 0.05F, true, 7, mask);
	check(stats.valid && stats.hiddenOwners == 2 && mask == std::array<uint8_t, 2> { 1, 1 }, "third person returns ALL obstructing owners");
	check(stats.certifiedComponents == 2 && stats.components == 2, "two closed separated owners certify independently");
	check(stats.bytes == index.bytes() && stats.bytes <= Index::MaxIndexBytes, "persistent bytes reported and bounded");
	stats = index.Query({ -3, 0, 0 }, { 7, 0, 0 }, 0.05F, false, 7, mask);
	check(stats.valid && stats.hiddenOwners == 0, "first person ignores long forward/focus ray");
	stats = index.Query({ 0, 0, 0 }, { 7, 0, 0 }, 0.05F, false, 7, mask);
	check(stats.valid && stats.containmentCertified == 1 && mask == std::array<uint8_t, 2> { 1, 0 }, "first person inside deep closed shell hides its owner");
	stats = index.Query({ 4, 0, 0 }, { 4, 0, 0 }, 0, false, 7, mask);
	check(stats.valid && mask == std::array<uint8_t, 2> { 0, 1 }, "zero radius and zero segment remain valid containment");
	stats = index.Query({ -1.02F, 0, 0 }, { -1.02F, 0, 0 }, 0.03F, false, 7, mask);
	check(stats.valid && mask[0] == 1, "near sphere outside wall hides shell without point containment");
	stats = index.Query({ -1.03F, 1.03F, 0 }, {}, 0.05F, false, 7, mask);
	check(stats.valid && mask[0] == 1, "sphere reaches corner even though eye is outside bounds on two axes");
	stats = index.Query({ -1.03F, 1.03F, 0 }, {}, 0.03F, false, 7, mask);
	check(stats.valid && mask[0] == 0, "expanded AABB alone does not hide owner at corner");
	stats = index.Query({ -4, 0, 0 }, { -4, 0, 0 }, 0.01F, false, 7, mask);
	check(stats.valid && mask == std::array<uint8_t, 2> { 0, 0 }, "camera leaving restores both owners from complete geometry");
	const Index &immutable = index;
	std::array<uint8_t, 2> replay {};
	const auto first = immutable.Query({ -3, 0, 0 }, { 7, 0, 0 }, 0.05F, true, 7, mask);
	const auto second = immutable.Query({ -3, 0, 0 }, { 7, 0, 0 }, 0.05F, true, 7, replay);
	check(first.valid && second.valid && mask == replay && first.trianglesTested == second.trianglesTested && first.nodesVisited == second.nodesVisited, "const query is deterministic for CPU/GPU replay");
	Index rebuilt;
	check(rebuilt.Build(7, 2, cubes), "independent rebuild accepts same snapshot");
	const auto rebuiltStats = rebuilt.Query({ -3, 0, 0 }, { 7, 0, 0 }, 0.05F, true, 7, replay);
	check(rebuiltStats.valid && mask == replay && first.trianglesTested == rebuiltStats.trianglesTested && first.nodesVisited == rebuiltStats.nodesVisited, "independent builds choose deterministic BVH order");
	check(!index.Query({}, {}, 0, false, 8, mask).valid && mask == std::array<uint8_t, 2> { 0, 0 }, "epoch mismatch clears stale owners");
	std::array<uint8_t, 3> oversized { 1, 1, 1 };
	std::array<uint8_t, 1> undersized { 1 };
	check(!index.Query({}, {}, 0, false, 7, oversized).valid && oversized == std::array<uint8_t, 3> { 0, 0, 0 }, "oversized output rejected and entirely cleared");
	check(!index.Query({}, {}, 0, false, 7, undersized).valid && undersized[0] == 0, "undersized output rejected and cleared");
	for (const TownCameraPoint invalid : { TownCameraPoint { nan, 0, 0 }, TownCameraPoint { 0, infinity, 0 }, TownCameraPoint { 0, 0, Index::MaxCoordinate * 2 } }) {
		mask.fill(1);
		check(!index.Query(invalid, {}, 0, false, 7, mask).valid && mask == std::array<uint8_t, 2> { 0, 0 }, "nonfinite/over-limit eye clears mask");
		mask.fill(1);
		check(!index.Query({}, invalid, 0, false, 7, mask).valid && mask == std::array<uint8_t, 2> { 0, 0 }, "invalid focus rejected even when first person ignores segment");
	}
	for (const float radius : { nan, infinity, -0.01F, Index::MaxNearRadius + 1 }) {
		mask.fill(1);
		check(!index.Query({}, {}, radius, false, 7, mask).valid && mask == std::array<uint8_t, 2> { 0, 0 }, "invalid radius clears stale mask");
	}
	std::array<uint8_t, 1> one {};
	Index shape;
	check(shape.Build(9, 1, Tetrahedron()), "closed tetrahedron accepted");
	stats = shape.Query({ 1.5F, 1.5F, 1.5F }, {}, 0.01F, false, 9, one);
	check(stats.valid && stats.containmentCertified == 1 && one[0] == 0, "eye inside tetrahedron AABB but outside volume stays visible");
	stats = shape.Query({ 0.3F, 0.3F, 0.3F }, {}, 0.01F, false, 9, one);
	check(stats.valid && one[0] == 1, "tetrahedron true interior hidden by containment");
	std::vector<TownCameraVisibilityTriangle> nested;
	Cube(nested, 0, { -2, -2, -2 }, { 2, 2, 2 });
	Cube(nested, 0, { -1, -1, -1 }, { 1, 1, 1 });
	check(shape.Build(10, 1, nested), "same owner nested closed components accepted");
	stats = shape.Query({}, {}, 0.01F, false, 10, one);
	check(stats.valid && stats.certifiedComponents == 2 && stats.containmentCertified == 2 && one[0] == 1, "nested shell containment is owner UNION rather than XOR");
	std::vector<TownCameraVisibilityTriangle> disconnected;
	Cube(disconnected, 0, { -1, -1, -1 }, { 1, 1, 1 });
	Cube(disconnected, 0, { 4, -1, -1 }, { 6, 1, 1 });
	check(shape.Build(11, 1, disconnected), "same owner separated shells accepted");
	check(shape.Query({ 5, 0, 0 }, {}, 0.01F, false, 11, one).valid && one[0] == 1, "second closed component independently contains eye");
	check(shape.Query({ 2.5F, 0, 0 }, {}, 0.01F, false, 11, one).valid && one[0] == 0, "gap inside owner aggregate bounds is not containment");
	std::vector<TownCameraVisibilityTriangle> open;
	Cube(open, 0, { -1, -1, -1 }, { 1, 1, 1 });
	open.erase(open.begin(), open.begin() + 2); // Actual front aperture, not opacity.
	check(shape.Build(12, 1, open), "open shell still builds contact index");
	stats = shape.Query({}, {}, 0.01F, false, 12, one);
	check(stats.valid && stats.certifiedComponents == 0 && stats.containmentUncertified == 1 && one[0] == 0, "open shell never claims certified containment");
	check(shape.Query({ -1.01F, 0, 0 }, {}, 0.02F, false, 12, one).valid && one[0] == 1, "uncertified shell retains near contact");
	check(shape.Query({ -3, 0, 0 }, { 3, 0, 0 }, 0.01F, true, 12, one).valid && one[0] == 1, "uncertified shell retains eye-to-focus obstruction");
	check(shape.Query({ 0, 0, -3 }, {}, 0.01F, true, 12, one).valid && one[0] == 0, "eye-to-focus passage through real opening does not hide shell");
	check(shape.Query({ 0, 0, -1.01F }, {}, 0.02F, false, 12, one).valid && one[0] == 0, "near sphere through opening does not use AABB as wall");
	std::vector<TownCameraVisibilityTriangle> seams;
	Cube(seams, 0, { 0, 0, 0 }, { 2, 2, 2 });
	for (size_t i = 0; i < seams.size(); ++i) {
		for (auto &p : seams[i].vertices) {
			if (p.x == 0) p.x = (i & 1U) ? -0.0F : 0.0F;
			if (p.height == 0) p.height = (i & 1U) ? -0.0F : 0.0F;
			if (p.z == 0) p.z = (i & 1U) ? -0.0F : 0.0F;
		}
	}
	check(shape.Build(13, 1, seams), "signed zero seam snapshot accepted");
	stats = shape.Query({ 1, 1, 1 }, {}, 0.01F, false, 13, one);
	check(stats.valid && stats.certifiedComponents == 1 && one[0] == 1, "positive/negative zero seams weld exactly");
	seams[0].vertices[0].x = 0.0001F;
	check(shape.Build(14, 1, seams), "inexact seam remains usable geometry");
	stats = shape.Query({ 1, 1, 1 }, {}, 0.01F, false, 14, one);
	check(stats.valid && stats.certifiedComponents == 0 && one[0] == 0, "inexact seams are not epsilon welded into certified shell");
	std::vector<TownCameraVisibilityTriangle> closed;
	Cube(closed, 0, { -1, -1, -1 }, { 1, 1, 1 });
	auto duplicate = closed;
	duplicate.push_back(closed.front());
	check(shape.Build(15, 1, duplicate), "duplicate face remains contact geometry");
	stats = shape.Query({}, {}, 0.01F, false, 15, one);
	check(stats.valid && stats.certifiedComponents == 0 && one[0] == 0, "duplicate/nonmanifold edges cannot certify containment");
	auto inverted = closed;
	std::swap(inverted[0].vertices[1], inverted[0].vertices[2]);
	check(shape.Build(16, 1, inverted), "inconsistent orientation remains contact geometry");
	check(shape.Query({}, {}, 0.01F, false, 16, one).certifiedComponents == 0 && one[0] == 0, "one inverted face denies certification");
	auto collapsed = closed;
	collapsed[0].vertices[2] = collapsed[0].vertices[1];
	check(shape.Build(17, 1, collapsed), "degenerate face omitted without rejecting useful geometry");
	stats = shape.Query({}, {}, 0.01F, false, 17, one);
	check(stats.valid && stats.skippedDegenerateTriangles == 1 && stats.indexTriangles == 11 && stats.certifiedComponents == 0 && one[0] == 0, "omitted degenerate face opens shell rather than preserving certification");
	// Move one cube corner beyond its opposite side, preserving exact topology.
	// Two nonadjacent faces cross: closed edges alone must not certify this mesh.
	auto crossing = closed;
	for (auto &triangle : crossing)
		for (auto &p : triangle.vertices)
			if (p.x == -1 && p.height == -1 && p.z == -1) p = { 2, 0, 0 };
	check(shape.Build(18, 1, crossing), "self-intersecting shell remains contact geometry");
	stats = shape.Query({}, {}, 0, false, 18, one);
	check(stats.valid && stats.certifiedComponents == 0, "bounded embeddedness check rejects self-intersecting closed topology");
	std::vector<TownCameraVisibilityTriangle> far;
	for (uint32_t i = 0; i < 1000; ++i) {
		const float x = 100 + static_cast<float>(i) * 4;
		far.push_back({ { TownCameraPoint { x, 0, 0 }, { x + 1, 0, 0 }, { x, 1, 0 } }, 0 });
	}
	check(shape.Build(19, 1, far), "many separated open components build bounded BVH");
	stats = shape.Query({}, { 1, 0, 0 }, 0.1F, true, 19, one);
	check(stats.valid && stats.trianglesTested == 0 && stats.nodesVisited == 1 && one[0] == 0, "distant geometry prunes at root rather than scanning every triangle");
	stats = shape.Query({ 99, 0.25F, 0 }, { 101, 0.25F, 0 }, 0, true, 19, one);
	check(stats.valid && one[0] == 1 && stats.trianglesTested < far.size() / 16, "local coplanar segment uses BVH and exact geometry");
	check(stats.nodesVisited <= Index::MaxQueryNodes && stats.trianglesTested <= Index::MaxQueryTriangleTests, "successful query stays inside published budgets");
	auto invalidOwner = closed;
	invalidOwner[0].owner = 1;
	check(!shape.Build(20, 1, invalidOwner) && !shape.ready() && shape.ownerCount() == 0 && shape.buildStatus() == TownCameraVisibilityBuildStatus::InvalidInput, "invalid owner invalidates previous index transactionally");
	one[0] = 1;
	check(!shape.Query({}, {}, 0, false, 19, one).valid && one[0] == 0, "failed replacement cannot reuse old valid epoch");
	auto invalidVertex = closed;
	invalidVertex[0].vertices[0].height = nan;
	check(!shape.Build(21, 1, invalidVertex) && !shape.ready(), "nonfinite geometry rejects entire candidate");
	invalidVertex[0].vertices[0].height = Index::MaxCoordinate * 2;
	check(!shape.Build(21, 1, invalidVertex) && !shape.ready(), "over-limit geometry rejects entire candidate");
	check(!shape.Build(22, Index::MaxOwners + 1, {}) && shape.buildStatus() == TownCameraVisibilityBuildStatus::LimitExceeded, "owner limit rejects before internal allocation");
	std::vector<TownCameraVisibilityTriangle> overTriangles(Index::MaxTriangles + 1);
	check(!shape.Build(23, 1, std::move(overTriangles)) && shape.buildStatus() == TownCameraVisibilityBuildStatus::LimitExceeded, "triangle limit rejects before internal allocation");
	std::vector<TownCameraVisibilityTriangle> overComponents;
	for (size_t i = 0; i <= Index::MaxComponents; ++i) {
		const float x = static_cast<float>(i) * 3;
		overComponents.push_back({ { TownCameraPoint { x, 0, 0 }, { x + 1, 0, 0 }, { x, 1, 0 } }, 0 });
	}
	check(!shape.Build(24, 1, std::move(overComponents)) && !shape.ready() && shape.buildStatus() == TownCameraVisibilityBuildStatus::LimitExceeded, "component limit rejects and clears candidate");
	check(shape.Build(25, 1, {}) && shape.ready(), "empty owner geometry is a valid clear mask snapshot");
	one[0] = 1;
	stats = shape.Query({}, {}, Index::MaxNearRadius, true, 25, one);
	check(stats.valid && one[0] == 0 && stats.indexTriangles == 0 && stats.nodesVisited == 0, "empty geometry restores owner without queries");
	check(shape.Build(26, 0, {}), "zero owners and empty geometry accepted");
	check(shape.Query({}, {}, 0, false, 26, std::span<uint8_t> {}).valid, "zero-owner output is valid empty span");
	shape.Clear();
	check(!shape.ready() && shape.ownerCount() == 0 && shape.sceneEpoch() == 0 && shape.buildStatus() == TownCameraVisibilityBuildStatus::Empty, "Clear releases borrowed scene identity and retained geometry");
}

} // namespace devilution
