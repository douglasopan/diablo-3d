#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "levels/surface_region.hpp"

namespace devilution::surface {

inline constexpr uint32_t ObstacleCatalogRevision = 1;
// Authored navigation policy from selected Warrior BODY at actual runtime fit.
// See ACTOR-CLEARANCE-POLICY.json: measured core max .2595531117m, top1.98882858m;
// rounded up to centimetres. Not an equipment/limb or native-physics collider.
inline constexpr double TraversalBodyRadiusMetres = 0.26;
inline constexpr double TraversalBodyTopMetres = 1.99;
inline constexpr double TraversalStepOverMetres = 0.0;

struct ObstaclePoint { double x, z; };
struct ObstaclePolygon {
	uint32_t firstPoint, pointCount;
	uint16_t obstacleId, componentId;
	uint32_t sourceFirstTriangle, sourceTriangleCount;
	std::string_view role;
};
struct SurfaceObstacle {
	uint16_t id;
	std::string_view featureId, assetAlias;
	uint32_t sourceFirstTriangle, sourceTriangleCount;
	double anchorWorldX, sourceSupportYMetres, anchorWorldZ;
	uint32_t firstPolygon, polygonCount;
	// Broadphase only: never an obstacle or SOL footprint by itself.
	double minX, minZ, maxX, maxZ;
};
struct WorldCellSemantics {
	CellKind kind = CellKind::Grass; // Canonical physical traversal, before guards.
	CellKind materialKind = CellKind::Grass; // Original world road phase.
	uint16_t obstacleId = 0; // 0 means no authored obstruction at this label.
};

std::span<const SurfaceObstacle> AuthoredSurfaceObstacles() noexcept;
std::span<const ObstaclePolygon> AuthoredSurfaceObstaclePolygons() noexcept;
std::span<const ObstaclePoint> AuthoredSurfaceObstaclePoints() noexcept;
std::string_view SurfaceObstacleSourceAssetSha256() noexcept;

// Global actor labels, used literally as native point coordinates: no -0.5.
// Sole R2 authority for interior traversal/material semantics. Local ghost
// guards are an additional host/Region rule and do not change the material.
// Refuses labels outside admitted storage bounds. Town generation remains refused.
std::optional<WorldCellSemantics> GetWorldCellSemantics(int32_t worldLabelX, int32_t worldLabelZ) noexcept;

// Continuous swept navigation core capsule against the same authored solids.
// Call from each current path node or actual interpolated actor origin, never
// one fixed hero origin for every A* node. Does not authorize crossing guards,
// a transition, native occupancy or a non-adjacent native movement command.
bool CanTraverseWorldSegment(double fromX, double fromZ, double toX, double toZ) noexcept;

// Diagnostic geometric distance from a point centre to the nearest authored
// solid projection, in metres, before subtracting the policy body radius.
std::optional<double> WorldObstacleClearanceMetres(double worldX, double worldZ) noexcept;

} // namespace devilution::surface
