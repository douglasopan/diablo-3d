#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include "levels/surface_region_geometry.hpp"

namespace devilution::surface {
inline constexpr uint32_t SurfaceHaloRevision = 2;
inline constexpr uint64_t MaximumSurfaceHaloBytes = 48ULL*1024*1024;
inline constexpr uint64_t MaximumSurfaceHaloTriangles = 1000000;
inline constexpr uint64_t MaximumSurfaceHaloVertices = 524288;
inline constexpr int SurfaceHaloClusterCells = 8;

// Absolute world NATIVE X/Z, from all actual frustum/viewport corners. Far is
// view.z depth, NOT a radius. Caller must include its admitted pose/pan range.
struct SurfaceHaloWorldBounds {
	double minX, minZ, maxX, maxZ;
	constexpr bool operator==(const SurfaceHaloWorldBounds &) const = default;
};
struct SurfaceHaloRequest {
	RegionId activeRegion;
	double farMetres = 1024; // Identity only; never substituted for corner bounds.
	double maximumCameraOffsetMetres = 90;
	// Required. Expanded by maximumCameraOffsetMetres once, then union active.
	// Set offset0 if the supplied envelope already includes camera displacement.
	std::optional<SurfaceHaloWorldBounds> requiredWorldBounds;
	constexpr bool operator==(const SurfaceHaloRequest &) const = default;
};
enum class SurfaceHaloBuildFailure : uint8_t {
	None, InvalidRequest, MissingWorldBounds, TriangleBudget, VertexBudget,
	MemoryBudget, VisualSamplingBounds, SamplingFailed, AllocationFailed, InvalidGeneratedGeometry,
};
struct SurfaceHaloExtent {
	int32_t minWorldCellX=0,minWorldCellZ=0,maxWorldCellX=0,maxWorldCellZ=0;
	uint32_t widthCells=0,heightCells=0;
	uint64_t vertexCount=0,triangleCount=0,clusterCapacity=0,plannedPayloadBytes=0,plannedPeakPayloadBytes=0;
	bool townHoleInExtent=false;
	constexpr bool operator==(const SurfaceHaloExtent &) const = default;
};
// X/Z world native; heights METRES. Renderer converts height exactly once.
struct SurfaceHaloBounds { double minX,minHeightMetres,minZ,maxX,maxHeightMetres,maxZ; };
struct SurfaceHaloCluster {
	// Stable global floor(vertexCoord/(8*gridStep)), qualified by gridStep.
	int32_t worldClusterX,worldClusterZ;
	uint32_t gridStep,firstTriangle,triangleCount;
	SurfaceHaloBounds bounds;
};
struct SurfaceHaloGeometry {
	SurfaceHaloRequest request;
	SurfaceHaloExtent extent;
	Tile activeWorldOffset;
	uint32_t seed=VisualSeed,generatorRevision=GeneratorRevision,heightRevision=HeightRevision;
	uint32_t geometryRevision=GeometryRevision,haloRevision=SurfaceHaloRevision;
	uint64_t retainedPayloadBytes=0;
	// Sparse, unique integer world vertices shared by both2D/3D. Same H1/normal/UV.
	std::vector<SurfaceRegionVertex> vertices;
	std::vector<SurfaceRegionTriangle> triangles;
	std::vector<SurfaceHaloCluster> clusters;
};
struct SurfaceHaloCompanions {
	bool activeSurfaceReady=false;
	bool townSurfaceReady=false;
};

// Bounded planning may allocate; samples no H. Exact topology counts include
// stitched1/2/4/8/16/32 transitions and BOTH active112²/Town[-1,111] holes.
std::optional<SurfaceHaloExtent> PreflightSurfaceHalo(const SurfaceHaloRequest &request,SurfaceHaloBuildFailure *failure=nullptr);
std::optional<SurfaceHaloGeometry> BuildSurfaceHalo(const SurfaceHaloRequest &request,SurfaceHaloBuildFailure *failure=nullptr);
bool ValidateSurfaceHalo(const SurfaceHaloGeometry &halo) noexcept;

// O(1) complete composition preflight after cached validation/publication.
// Missing active companion, or visible Town hole without Town preview, refuses.
bool SurfaceHaloCoversWorldBounds(const SurfaceHaloGeometry &halo,double minX,double minZ,double maxX,double maxZ,
    SurfaceHaloCompanions companions={}) noexcept;
bool SurfaceHaloCoversCamera(const SurfaceHaloGeometry &halo,const SurfaceHaloWorldBounds &actualFrustumBounds,
    SurfaceHaloCompanions companions={}) noexcept;
// Explicitly prohibit the old false Far-as-sphere proof at compile time.
bool SurfaceHaloCoversCamera(const SurfaceHaloGeometry &,double,double,double)=delete;
} // namespace devilution::surface
