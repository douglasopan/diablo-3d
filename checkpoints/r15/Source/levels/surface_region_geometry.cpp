#include "levels/surface_region_geometry.hpp"
#include "levels/surface_region_obstacles.hpp"

#include <cmath>
#include <algorithm>
#include <utility>

namespace devilution::surface {
namespace {

constexpr double MinimumWorldNative = -MaximumRegionCoordinate * InteriorSize - 1;
constexpr double MaximumWorldNative = MaximumRegionCoordinate * InteriorSize + StorageSize - 1;
constexpr double MaximumPixelOrigin = 16777216.0;

bool WorldPositionValid(double x, double z) noexcept
{
	return std::isfinite(x) && std::isfinite(z)
	    && x >= MinimumWorldNative && x <= MaximumWorldNative
	    && z >= MinimumWorldNative && z <= MaximumWorldNative;
}

bool VisualPositionValid(double x, double z) noexcept
{
	return std::isfinite(x) && std::isfinite(z)
	    && std::abs(x) <= MaximumSurfaceWorldNative && std::abs(z) <= MaximumSurfaceWorldNative;
}

bool GeneratedRegionValid(RegionId id) noexcept
{
	return ValidRegionId(id) && !id.IsTown();
}

bool GeometryShapeValid(const RegionGeometry &geometry) noexcept
{
	// Check the admitted ID before WorldOffset multiplies its components.
	return GeneratedRegionValid(geometry.regionId)
	    && geometry.worldOffset == WorldOffset(geometry.regionId)
	    && geometry.generatorRevision == GeneratorRevision
	    && geometry.geometryRevision == GeometryRevision && geometry.seed == VisualSeed
	    && geometry.vertices.size() == GeometryVertexCount
	    && geometry.triangles.size() == GeometryTriangleCount
	    && geometry.cellKinds.size() == StorageSize * StorageSize
	    && geometry.materialKinds.size() == StorageSize * StorageSize;
}

bool VertexValid(const SurfaceRegionVertex &vertex, int worldX, int worldZ) noexcept
{
	const auto &normal = vertex.normal;
	return vertex.worldNativeX == worldX && vertex.worldNativeZ == worldZ
	    && WorldPositionValid(vertex.worldNativeX, vertex.worldNativeZ)
	    && std::isfinite(vertex.heightMetres) && std::abs(vertex.heightMetres) <= MaximumGeometryHeightMetres
	    && std::isfinite(normal.x) && std::isfinite(normal.height) && std::isfinite(normal.z)
	    && std::abs(normal.x) <= 1 && normal.height > 0 && normal.height <= 1 && std::abs(normal.z) <= 1
	    && std::abs(std::hypot(normal.x, normal.height, normal.z) - 1) <= 1e-10
	    && vertex.regionalMetres56U == (worldX - RegionalOrigin) * CellMetres
	    && vertex.regionalMetres56V == (worldZ - RegionalOrigin) * CellMetres
	    && vertex.worldTextureMetres0U == worldX * CellMetres
	    && vertex.worldTextureMetres0V == worldZ * CellMetres;
}

bool CellValid(const RegionGeometry &geometry, int x, int z) noexcept
{
	const std::size_t cell = static_cast<std::size_t>(z) * StorageSize + x;
	const auto semantics = GetWorldCellSemantics(geometry.worldOffset.x + x, geometry.worldOffset.z + z);
	if (!semantics)
		return false;
	const CellKind material = semantics->materialKind;
	const CellKind traversal = InInterior({ x, z }) ? semantics->kind : CellKind::Blocked;
	const uint32_t a = z * GeometrySideVertices + x;
	// No caller-controlled index is dereferenced. Exact canonical indices also
	// establish winding, two faces per cell, and bounds on all index values.
	return geometry.cellKinds[cell] == traversal && geometry.materialKinds[cell] == material
	    && geometry.triangles[2 * cell].vertices == std::array<uint32_t, 3> { a, a + GeometrySideVertices + 1, a + 1 }
	    && geometry.triangles[2 * cell + 1].vertices == std::array<uint32_t, 3> { a, a + GeometrySideVertices, a + GeometrySideVertices + 1 };
}

int64_t FloorDivide(int64_t numerator, int64_t denominator) noexcept
{
	const int64_t quotient = numerator / denominator;
	return quotient - (numerator % denominator < 0 ? 1 : 0);
}

std::optional<SurfaceRegionVertex> SampleVertex(double x, double z) noexcept
{
	if (!VisualPositionValid(x, z))
		return std::nullopt;
	const double height = HeightMetres(x, z);
	const double stepNative = NormalSampleStepMetres / CellMetres;
	// Derivatives are per WORLD METRE, at a fixed world step, not per local
	// vertex or LOD. Height samples remain defined across blocked guard cells.
	const double slopeX = (HeightMetres(x + stepNative, z) - HeightMetres(x - stepNative, z))
	    / (2 * NormalSampleStepMetres);
	const double slopeZ = (HeightMetres(x, z + stepNative) - HeightMetres(x, z - stepNative))
	    / (2 * NormalSampleStepMetres);
	const double length = std::hypot(slopeX, 1.0, slopeZ);
	if (!std::isfinite(height) || std::abs(height) > MaximumGeometryHeightMetres
	    || !std::isfinite(length) || length < 1.0)
		return std::nullopt;
	SurfaceRegionVertex result;
	result.worldNativeX = x;
	result.heightMetres = height;
	result.worldNativeZ = z;
	result.normal = { -slopeX / length, 1.0 / length, -slopeZ / length };
	result.regionalMetres56U = (x - RegionalOrigin) * CellMetres;
	result.regionalMetres56V = (z - RegionalOrigin) * CellMetres;
	result.worldTextureMetres0U = x * CellMetres;
	result.worldTextureMetres0V = z * CellMetres;
	if (!std::isfinite(result.regionalMetres56U) || !std::isfinite(result.regionalMetres56V)
	    || !std::isfinite(result.worldTextureMetres0U) || !std::isfinite(result.worldTextureMetres0V))
		return std::nullopt;
	return result;
}

} // namespace

std::optional<SurfaceRegionVertex> SampleSurfaceWorldVertex(double x, double z) noexcept
{
	return SampleVertex(x, z);
}

bool ValidateRegionGeometry(const RegionGeometry &geometry) noexcept
{
	if (!GeometryShapeValid(geometry))
		return false;
	for (int z = 0; z < GeometrySideVertices; ++z) {
		for (int x = 0; x < GeometrySideVertices; ++x) {
			if (!VertexValid(geometry.vertices[static_cast<std::size_t>(z) * GeometrySideVertices + x],
			        geometry.worldOffset.x + x - 1, geometry.worldOffset.z + z - 1))
				return false;
		}
	}
	for (int z = 0; z < StorageSize; ++z) {
		for (int x = 0; x < StorageSize; ++x) {
			if (!CellValid(geometry, x, z))
				return false;
		}
	}
	return true;
}

std::optional<WorldCellLabel> GlobalCellLabel(RegionId id, Tile local) noexcept
{
	if (!GeneratedRegionValid(id) || local.x < 0 || local.x >= StorageSize
	    || local.z < 0 || local.z >= StorageSize)
		return std::nullopt;
	const Tile offset = WorldOffset(id);
	return WorldCellLabel { offset.x + local.x, offset.z + local.z };
}

std::optional<RegionId> CanonicalCellOwner(WorldCellLabel label) noexcept
{
	// Widen before subtracting, even for caller-supplied INT32_MIN/MAX labels.
	const int64_t x = FloorDivide(int64_t(label.x) - GuardSize, InteriorSize);
	const int64_t z = FloorDivide(int64_t(label.z) - GuardSize, InteriorSize);
	if (x < -MaximumRegionCoordinate || x > MaximumRegionCoordinate
	    || z < -MaximumRegionCoordinate || z > MaximumRegionCoordinate)
		return std::nullopt;
	return RegionId { static_cast<int32_t>(x), static_cast<int32_t>(z) };
}

std::optional<VisualChunkId> VisualChunkAt(double worldNativeX, double worldNativeZ) noexcept
{
	if (!WorldPositionValid(worldNativeX, worldNativeZ))
		return std::nullopt;
	const double x = std::floor((worldNativeX - RegionalOrigin) * CellMetres / VisualChunkMetres);
	const double z = std::floor((worldNativeZ - RegionalOrigin) * CellMetres / VisualChunkMetres);
	// The validated coordinate envelope makes these conversions bounded.
	return VisualChunkId { static_cast<int32_t>(x), static_cast<int32_t>(z) };
}

std::optional<RegionGeometry> BuildRegionGeometry(const Region &region)
{
	const RegionId id = region.id;
	if (!GeneratedRegionValid(id) || region.worldOffset != WorldOffset(id))
		return std::nullopt;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
		RegionGeometry result;
		result.regionId = id;
		result.worldOffset = WorldOffset(id);
		result.cellKinds.reserve(StorageSize * StorageSize);
		result.materialKinds.reserve(StorageSize * StorageSize);
		for (int z = 0; z < StorageSize; ++z) for (int x = 0; x < StorageSize; ++x) {
			const Cell &cell = region.At(x,z);
			const auto semantics = GetWorldCellSemantics(result.worldOffset.x+x,result.worldOffset.z+z);
			if (!semantics)
				return std::nullopt;
			const CellKind expected = InInterior({x,z}) ? semantics->kind : CellKind::Blocked;
			if (cell.kind != expected || cell.variation > 15 || !std::isfinite(cell.heightMetres)
			    || std::abs(cell.heightMetres) > MaximumGeometryHeightMetres) return std::nullopt;
			result.cellKinds.push_back(cell.kind);
			result.materialKinds.push_back(semantics->materialKind);
		}
		result.vertices.reserve(GeometryVertexCount);
		result.triangles.reserve(GeometryTriangleCount);
		for (int z = 0; z < GeometrySideVertices; ++z) {
			for (int x = 0; x < GeometrySideVertices; ++x) {
				const auto vertex = SampleSurfaceWorldVertex(result.worldOffset.x + x - 1, result.worldOffset.z + z - 1);
				if (!vertex)
					return std::nullopt;
				result.vertices.push_back(*vertex);
			}
		}
		for (int z = 0; z < StorageSize; ++z) {
			for (int x = 0; x < StorageSize; ++x) {
				const uint32_t a = z * GeometrySideVertices + x;
				const uint32_t b = a + 1;
				const uint32_t d = a + GeometrySideVertices;
				const uint32_t c = d + 1;
				result.triangles.push_back({ { a, c, b } });
				result.triangles.push_back({ { a, d, c } });
			}
		}
		return result;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (...) {
		return std::nullopt;
	}
#endif
}

std::optional<RegionGeometry> GenerateRegionGeometry(RegionId id)
{
	const auto region = GenerateRegion(id);
	return region ? BuildRegionGeometry(*region) : std::nullopt;
}

std::optional<RegionSupport> SampleRegionSupport(const RegionGeometry &geometry,
    double localNativeX, double localNativeZ) noexcept
{
	if (!GeometryShapeValid(geometry) || !std::isfinite(localNativeX) || !std::isfinite(localNativeZ)
	    || localNativeX < -1 || localNativeX > StorageSize - 1
	    || localNativeZ < -1 || localNativeZ > StorageSize - 1)
		return std::nullopt;
	const int x = std::min(static_cast<int>(std::floor(localNativeX)) + 1, StorageSize - 1);
	const int z = std::min(static_cast<int>(std::floor(localNativeZ)) + 1, StorageSize - 1);
	const size_t cell = static_cast<size_t>(z) * StorageSize + x;
	const CellKind kind = geometry.cellKinds[cell];
	const CellKind materialKind = geometry.materialKinds[cell];
	if (!CellValid(geometry, x, z))
		return std::nullopt;
	const uint32_t a = z * GeometrySideVertices + x, b = a + 1;
	const uint32_t d = a + GeometrySideVertices, c = d + 1;
	const std::array<uint32_t,4> indices { a,b,c,d };
	const std::array<int,4> cornerX { x-1,x,x,x-1 };
	const std::array<int,4> cornerZ { z-1,z-1,z,z };
	for (size_t i=0; i<4; ++i) {
		const auto &vertex=geometry.vertices[indices[i]];
		if (!VertexValid(vertex, geometry.worldOffset.x+cornerX[i], geometry.worldOffset.z+cornerZ[i]))
			return std::nullopt;
	}
	const double u=localNativeX-(x-1), v=localNativeZ-(z-1);
	const auto &va=geometry.vertices[a], &vb=geometry.vertices[b];
	const auto &vc=geometry.vertices[c], &vd=geometry.vertices[d];
	const bool first=u>=v;
	const double height=first ? (1-u)*va.heightMetres+v*vc.heightMetres+(u-v)*vb.heightMetres
	                         : (1-v)*va.heightMetres+(v-u)*vd.heightMetres+u*vc.heightMetres;
	const auto &face=geometry.triangles[2*cell+(first ? 0 : 1)].vertices;
	const auto &p=geometry.vertices[face[0]], &q=geometry.vertices[face[1]], &r=geometry.vertices[face[2]];
	const double qx=(q.worldNativeX-p.worldNativeX)*CellMetres, qh=q.heightMetres-p.heightMetres, qz=(q.worldNativeZ-p.worldNativeZ)*CellMetres;
	const double rx=(r.worldNativeX-p.worldNativeX)*CellMetres, rh=r.heightMetres-p.heightMetres, rz=(r.worldNativeZ-p.worldNativeZ)*CellMetres;
	const double nx=qh*rz-qz*rh, nh=qz*rx-qx*rz, nz=qx*rh-qh*rx;
	const double length=std::hypot(nx,nh,nz);
	if (!std::isfinite(height) || !std::isfinite(length) || length<=0 || nh<=0)
		return std::nullopt;
	return RegionSupport { height, { nx/length,nh/length,nz/length }, {x,z},kind,materialKind,geometry.geometryRevision };
}

std::optional<SurfaceIsoPoint> IsoProjectedPoint(const SurfaceRegionVertex &vertex,
    SurfaceIsoPoint pixelOrigin, SurfaceNativePoint referenceNative) noexcept
{
	if (!VisualPositionValid(vertex.worldNativeX, vertex.worldNativeZ)
	    || !VisualPositionValid(referenceNative.x, referenceNative.z)
	    || !std::isfinite(vertex.heightMetres) || std::abs(vertex.heightMetres) > MaximumGeometryHeightMetres
	    || !std::isfinite(pixelOrigin.x) || !std::isfinite(pixelOrigin.y)
	    || std::abs(pixelOrigin.x) > MaximumPixelOrigin || std::abs(pixelOrigin.y) > MaximumPixelOrigin)
		return std::nullopt;
	const double dx = vertex.worldNativeX - referenceNative.x;
	const double dz = vertex.worldNativeZ - referenceNative.z;
	SurfaceIsoPoint result { pixelOrigin.x + 32 * (dx - dz),
		pixelOrigin.y + 16 * (dx + dz) - 32 * (vertex.heightMetres / CellMetres) };
	return std::isfinite(result.x) && std::isfinite(result.y)
	    ? std::optional<SurfaceIsoPoint>(result) : std::nullopt;
}

static_assert(InteriorSize == 108 && StorageSize == 112 && GuardSize == 2);
static_assert(GeometryVertexCount == 12769 && GeometryTriangleCount == 25088);

} // namespace devilution::surface
