#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "engine/render/town_gpu.hpp"

namespace devilution {

/** Identifies immutable decoded geometry, independently of camera, lights,
 * texture/palette revisions and frame-local picking. Zero stableKey is invalid.
 * Change revision whenever any position, UV, normal or index changes. */
struct TownGpuMeshIdentity {
	uint64_t stableKey = 0;
	uint64_t revision = 0;
};

/** Local x/height/z, authored UV and normal. Keep separate vertices at UV and
 * hard-normal seams. This increment retains flat lighting: each triangle must
 * have the same authored normal at its three vertices when vertexNormals is
 * enabled. Smooth-normal shading is a separate material/renderer change. */
struct TownGpuMeshVertex {
	std::array<float, 3> position;
	std::array<float, 2> uv;
	std::array<float, 3> normal { 0, 1, 0 };
	/** Optional legacy volume face: palette in bits 0..7, SpriteFront in bit 8.
	 * Equal at all three corners. Ignored unless instance.volumeMaterial is true. */
	uint32_t volumeFace = 0;
};

/** Inputs are copied into immutable GPU buffers during Upload, never retained.
 * Indices are uint32 triangle lists and may share vertices. */
struct TownGpuMeshUpload {
	TownGpuMeshIdentity identity;
	std::span<const TownGpuMeshVertex> vertices;
	std::span<const uint32_t> indices;
};

using TownGpuMeshAffine = std::array<std::array<float, 4>, 3>;

/** Row-major affine world -> view (right, up, positive depth). In the default
 * affine mode include heightScale in these rows and their translation terms.
 * The optional nativeArithmetic mode below instead consumes unscaled bases.
 * focalPixels/center are in raster-sample coordinates, matching BeginFrame.
 * eye and towardViewer are unscaled world space, for two-sided normal lighting.
 * perspective and near/far come from the existing BeginFrame projection. */
struct TownGpuMeshCamera {
	TownGpuMeshAffine worldToView { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
	float focalPixels = 1;
	float centerX = 0;
	float centerY = 0;
	std::array<float, 3> eye { 0, 0, 0 };
	std::array<float, 3> towardViewer { 0, 0, -1 };
	/** Optional parity mode for TownCameraToView. worldToView.xyz contains
	 * unscaled right/up/forward basis rows and w must be zero. GPU subtracts
	 * eye first, scales relative height, then performs each left-associated dot
	 * with precise arithmetic, matching the native CPU camera operation order.
	 * False retains the original affine API and ignores heightScale. */
	bool nativeArithmetic = false;
	float heightScale = 1;
};

struct TownGpuMeshInstance {
	TownGpuMeshIdentity identity;
	TownGpuMeshAffine localToWorld { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
	uint32_t firstIndex = 0;
	/** Zero draws the remaining indices. Range boundaries must be triangles. */
	uint32_t indexCount = 0;
	/** Frame-local ID; split cached ranges wherever native pickTile changes.
	 * The backend never bakes this ID into immutable vertex/index buffers. */
	uint32_t pickId = 0;
	/** False uses the supplied material's exact existing normal/diffuse/receiver.
	 * True transforms authored vertex normals using the inverse transpose and
	 * derives flat diffuse and shadow receiver data in the GPU vertex shader. */
	bool vertexNormals = false;
	/** For parity with existing two-sided lighting, flat authored normals must
	 * also align with the geometric face plane (not an arbitrary smooth normal). */
	bool doubleSidedNormals = false;
	/** Must match the shadow map's normalized light direction when it is active. */
	std::array<float, 3> toLight { 0, 1, 0 };
	float shadowDepthBias = 0.025F;
	float shadowSlopeBias = 0.045F;
	/** Legacy native volume: GPU backface rejection, per-face palette/fallback
	 * and shade, sharing one front texture binding with opaque side faces.
	 * Requires geometric flat vertex normals, positive-orientation transform,
	 * vertexNormals=true and doubleSidedNormals=false. Material must be Shadow
	 * with the full native 256-color/16-level LUT. Triangle order is preserved. */
	bool volumeMaterial = false;
	int volumeLighting = 0;
};

/** Set once after BeginFrame, before recording any geometry. */
bool TownGpuSetMeshCamera(const TownGpuMeshCamera &camera);
/** Pure cache query; missing/evicted/reset identities return false. Does not
 * mark the frame failed or retain the mesh. Upload again on a cache miss. */
bool TownGpuHasMesh(TownGpuMeshIdentity identity);
/** Requires a valid recording frame. Repeated identity/layout is O(1), without
 * scanning payloads; identical identity means identical immutable contents.
 * Capacity is the 256 MiB immutable payload budget, not projected vertices. */
bool TownGpuUploadMesh(const TownGpuMeshUpload &mesh);
/** Preserves the exact submission order with SubmitProjectedTriangle. GPU
 * performs homogeneous clipping and projection; spans are not needed on hits.
 * Material, texture, light/room/blocker spans are consumed before returning.
 * Repeated draws do not consume the separate CPU projected-vertex capacity.
 * Any failure invalidates the entire frame, using the existing CPU fallback. */
bool TownGpuSubmitMeshInstance(const TownGpuMeshInstance &instance,
    const TownGpuTexture &texture, const TownGpuMaterial &material);

struct TownGpuMeshStats {
	size_t cachedMeshes = 0;
	size_t residentBytes = 0;
	size_t uploads = 0;
	size_t uploadedVertexBytes = 0;
	size_t uploadedIndexBytes = 0;
	size_t instances = 0;
	size_t reusedInstances = 0;
	size_t evictions = 0;
	size_t projectedUploadBytes = 0;
	double uploadMilliseconds = 0;
	/** CPU time issuing/uploading all commands; excludes synchronous readback.
	 * It is not an elapsed GPU timestamp or final presentation/FPS measurement. */
	double commandMilliseconds = 0;
};

/** Per-frame counters; resident totals include unused retained meshes. Cache
 * budget is 256 MiB, with oldest unused entries evicted only under pressure.
 * Current-frame resources cannot be evicted; Reset releases every resource. */
const TownGpuMeshStats &GetTownGpuMeshStats();

} // namespace devilution
