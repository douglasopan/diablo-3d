#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "levels/surface_region.hpp"

namespace devilution::surface {

// Gameplay region labels and visual mesh chunks are different identity spaces.
inline constexpr uint32_t GeometryRevision = 1;
inline constexpr double GroundTexturePeriodMetres = 2.0;
inline constexpr double VisualChunkMetres = 256.0;
inline constexpr double NormalSampleStepMetres = 0.25;
inline constexpr int GeometrySideVertices = StorageSize + 1;
inline constexpr std::size_t GeometryVertexCount = GeometrySideVertices * GeometrySideVertices;
inline constexpr std::size_t GeometryTriangleCount = StorageSize * StorageSize * 2;
inline constexpr double MaximumGeometryHeightMetres = 4096.0;
// Visual sampling envelope, deliberately separate from admitted Region IDs.
inline constexpr double MaximumSurfaceWorldNative = 20000.0;

struct WorldCellLabel {
	int32_t x = 0;
	int32_t z = 0;
	constexpr bool operator==(const WorldCellLabel &) const = default;
};

struct VisualChunkId {
	int32_t x = 0;
	int32_t z = 0;
	constexpr bool operator==(const VisualChunkId &) const = default;
};

struct SurfaceNativePoint {
	double x = RegionalOrigin;
	double z = RegionalOrigin;
};

struct SurfaceIsoPoint {
	double x = 0;
	double y = 0;
};

struct SurfaceRegionNormal {
	double x = 0;
	double height = 1;
	double z = 0;
};

struct SurfaceRegionVertex {
	double worldNativeX = 0;
	double heightMetres = 0;
	double worldNativeZ = 0;
	SurfaceRegionNormal normal;
	// Global regional metres; never restarted at a gameplay/visual chunk edge.
	double regionalMetres56U = 0;
	double regionalMetres56V = 0;
	// Texture phase matches native Town: worldTextureMetres0 / 2, never u56/v56.
	double worldTextureMetres0U = 0;
	double worldTextureMetres0V = 0;
};

// Sole public world vertex sampler for RegionGeometry and a visual-only halo.
// Original H1 plus its existing 0.25m normal derivative and UV phase; no assets,
// native state or new field. Sampling never admits gameplay outside Region IDs.
std::optional<SurfaceRegionVertex> SampleSurfaceWorldVertex(double worldNativeX, double worldNativeZ) noexcept;

struct SurfaceRegionTriangle {
	std::array<uint32_t, 3> vertices;
};

struct RegionGeometry {
	RegionId regionId;
	Tile worldOffset;
	uint32_t generatorRevision = GeneratorRevision;
	uint32_t geometryRevision = GeometryRevision;
	uint32_t seed = VisualSeed;
	// One shared world-space list for both views, not a second 2D generator.
	std::vector<SurfaceRegionVertex> vertices;
	// Z-major cells, X-major within each row, two upward faces per cell.
	std::vector<SurfaceRegionTriangle> triangles;
	std::vector<CellKind> cellKinds;
	// Visual Grass/Road at every world cell, including blocked traversal guards.
	std::vector<CellKind> materialKinds;
};

// Complete bounded structural/semantic admission, with no allocation or field
// resampling. Checks every vertex/face, exact world coordinates/UV phase and
// canonical traversal/material labels. Build remains the HeightMetres authority.
// Cache this result once per immutable snapshot + geometry pointer + epoch;
// metadata matching the host's snapshot/Region remains the host's responsibility.
bool ValidateRegionGeometry(const RegionGeometry &geometry) noexcept;

// Local cell labels are [0,111], including guards. Town cannot be generated or
// passed as a generated region. Cell (x,z) occupies [worldX-1,worldX] etc.
std::optional<WorldCellLabel> GlobalCellLabel(RegionId id, Tile local) noexcept;

// Mathematical canonical owner: floorDiv(worldLabel-2,108), including negatives.
// (0,0) may be returned as the preserved Town's identity, never regenerated.
// Ghosts beyond the admitted +/-96 region range have no admitted owner.
std::optional<RegionId> CanonicalCellOwner(WorldCellLabel label) noexcept;

// floor(((worldNative-56)*CellMetres)/256), not a RegionId or save key.
// Coordinates are bounded to the full admitted geometry including ghost cells.
std::optional<VisualChunkId> VisualChunkAt(double worldNativeX, double worldNativeZ) noexcept;

// Pure surface of all112x112 cells, including blocked guard cells. Traversal
// flags are owned by Region/host; a blocked guard is no wall or height cut.
// HeightMetres is the sole field authority; no RNG, asset, native state or
// renderer is read. Invalid/Town/nonfinite/refused allocation => no partial mesh.
// Canonical host API: consumes one already-generated Region, never regenerates it.
std::optional<RegionGeometry> BuildRegionGeometry(const Region &region);
// Convenience for offline/tests only; host publishes BuildRegionGeometry output.
std::optional<RegionGeometry> GenerateRegionGeometry(RegionId id);

struct RegionSupport {
	double heightMetres = 0;
	SurfaceRegionNormal normal; // Geometric face normal, not analytic vertex normal.
	Tile nativeLocalCell;
	CellKind kind = CellKind::Blocked;
	CellKind materialKind = CellKind::Grass;
	uint32_t geometryRevision = GeometryRevision;
};
// Input is the actual interpolated local native coordinate, without half-cell
// correction. Inclusive bbox[-1,111]; deterministic face0 at the shared diagonal.
// O(1) checks cover only the selected cell/corners; a renderer must first admit
// the whole immutable geometry with ValidateRegionGeometry, once per snapshot.
std::optional<RegionSupport> SampleRegionSupport(const RegionGeometry &geometry,
    double localNativeX, double localNativeZ) noexcept;

// Pure64x32 projection of those very same world vertices; metres are explicitly
// converted to native height. No raster/occlusion/collision admission is implied.
std::optional<SurfaceIsoPoint> IsoProjectedPoint(const SurfaceRegionVertex &vertex,
    SurfaceIsoPoint pixelOrigin = {}, SurfaceNativePoint referenceNative = {}) noexcept;

} // namespace devilution::surface
