#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/render/town_lighting.hpp"
#include "engine/render/town_point_shadow.hpp"
#include "engine/render/town_camera.hpp"
#include "engine/render/town_fire_volume.hpp"

namespace devilution {

struct TownGeometricPointShadowView;

/** Raster coordinates after projection, with unnormalized view depth/world/UV.
 * clipW is view depth for perspective, exactly one for orthographic frames.
 * Appended default preserves existing orthographic aggregate initializers. */
struct TownGpuVertex {
	float x;
	float y;
	float depth;
	float u;
	float v;
	std::array<float, 3> world;
	float clipW = 1;
	/** Optional linear COLOR0. Clip this with UV/world before projection. White
	 * retains the legacy stream; nonwhite uses a bounded parallel GPU SRV. Alpha
	 * is published in RGBA only; opacity/index/depth/picking coverage is unchanged. */
	std::array<float, 4> authoredColor { 1, 1, 1, 1 };
};

/** Frame-wide projection; vertices must already be clipped to near/far.
 * Valid range is 0 < nearClip < farClip <= 4096. Orthographic hardware depth
 * retains depth/4096 for compatibility; published depth is always view depth. */
struct TownGpuProjection {
	bool perspective = false;
	float nearClip = 0.4F;
	float farClip = 4096;
};

/** Borrowed inputs are uploaded during Submit, never retained by the backend.
 * Payload spans may be omitted for resident identities; cache misses require
 * their payload. Always supply dimensions/lightLevels. Revisions are immutable
 * content contracts. Legacy warm hits stay O(1). Optional RGB-paired supplied
 * bytes are checked against content fingerprints; omit spans on warm hits to
 * avoid rescanning. No span is retained after Submit. */
struct TownGpuTexture {
	/** Identity/revision of texelCodes and opacity only. */
	uint64_t stableKey = 0;
	uint64_t revision = 0;
	int width = 0;
	int height = 0;
	std::span<const uint32_t> texelCodes;
	/** Optional original palette codes for RGB lighting of native prelit masonry. */
	std::span<const uint8_t> unlitCodes;
	std::span<const uint8_t> opacity;
	/** Palette output at [texelCode * lightLevels + level]. */
	std::span<const uint8_t> lightLut;
	unsigned lightLevels = 1;
	/** Nonzero shares the immutable LUT independently of texel/opacity data.
	 * Zero retains a private LUT identified by stableKey/revision. A change to
	 * LUT values/layout requires a new lightLutRevision (or private revision).
	 * Global LUT growth must not change the revision of unchanged texels. */
	uint64_t lightLutKey = 0;
	uint64_t lightLutRevision = 0;
	/** Optional ARGB8888 sRGB pair, exactly one entry per indexed lightLut.
	 * Shares its immutable identity/revision/layout. Set enabled on warm hits
	 * when omitting both LUT spans; a nonempty span also enables it. */
	std::span<const uint32_t> rgbaLightLut;
	bool rgbaLightLutEnabled = false;
	/** Optional immutable coverage resource, independent from shared albedo.
	 * Nonzero key requires its own dimensions; opacity spans may be omitted on
	 * warm hits. Zero preserves the legacy texel-sized private coverage path. */
	uint64_t opacityKey = 0;
	uint64_t opacityRevision = 0;
	int opacityWidth = 0;
	int opacityHeight = 0;
};

/** Palette uses exact shade 0..15 with a 16-level caller-supplied native LUT.
 * Legacy requires 256 codes; an explicit RGB pair may have a larger source
 * code domain while indexed output still uses its paired uint8 LUT. It does
 * not calculate directional shadows. Optional sprite point RGB is a separate
 * frame field; it leaves the indexed shade/LUT result untouched. */
enum class TownGpuLighting : uint32_t { Unlit, Shadow, Directional, Interior, Palette };

struct TownGpuMaterial {
	TownGpuLighting lighting = TownGpuLighting::Unlit;
	// Projected Unlit only: COLOR0 supplies final display sRGB.
	bool absoluteDisplayRgb = false;
	bool repeat = false;
	bool transparentZero = false;
	bool preservePicking = false;
	bool receivesShadow = false;
	int shade = 0;
	/** Native Shadow/Unlit material: fill cutout texels with this opaque palette color. */
	int fallbackPaletteIndex = -1;
	int fallbackShade = 0;
	float diffuse = 1;
	float shadowBias = 0.025F;
	float shadowSlopeU = 0;
	float shadowSlopeV = 0;
	std::array<float, 3> normal { 0, 1, 0 };
	/** Interior point red irradiance is divided by these two positive values. */
	float interiorRedNormalization = 1;
	float interiorPointRange = 1;
	std::span<const TownPointLight> lights;
	const TownLightOccluder *room = nullptr;
	/** Additional opaque room shells for interior point-light visibility. They
	 * have no apertures and are copied during Submit. At most
	 * TownMaxLightOccluders - 1 are allowed, reserving one for room. */
	std::span<const TownLightOccluder> blockers;
	/** Projected color-only native palette overlay, default off. Submit all
	 * opaque geometry/actors first, then overlays back-to-front. Each triangle
	 * snapshots prior indexed color and tests opaque hardware depth without
	 * writing hardware depth, published view depth or picking. Requires a
	 * frame palette blend table; resident mesh overlays are unsupported. */
	bool paletteBlend = false;
	/** Palette RGB samples the native light grid by default. False retains the
	 * exact supplied shade/TRN row, without disabling frame smooth lighting. */
	bool nativeLighting = true;
	/** Optional frame sprite point field, applied to published RGB only after
	 * native shade and COLOR0. Indexed color, coverage, alpha, depth and picking
	 * remain unchanged. Default off; requires smooth RGB or a paired RGB LUT. */
	bool receivesSpritePointLight = false;
	/** Interior diffuse lighting may opt into the validated frame field's full
	 * shell/aperture visibility. Source lights, normals, indexed LUT and diffuse
	 * math still come from this material; no RGB output is required. False keeps
	 * the legacy single room plus opaque blockers. Caller must match the field
	 * to this room before opting in. */
	bool useFramePointLightVisibility = false;
	/** Projected-only final first-person visual pass. It preserves published
	 * world depth/picking, resets hardware depth once for viewmodel self-occlusion,
	 * and publishes an explicit coverage mask without changing RGBA alpha.
	 * Submit after world/native palette overlays; later world draws are refused.
	 * Cannot be combined with paletteBlend or used by resident meshes. */
	bool viewModel = false;
	/** Opt-in per-channel diffuse RGB from the paired interior LUT. The indexed
	 * shade remains a legacy palette witness. Requires Interior plus a paired
	 * RGB LUT; false preserves the original scalar fire-color path exactly. */
	bool interiorRgbLighting = false;
	/** Optional same-frame geometric fire diffuse RGB. Requires a validated fire
	 * field and smooth RGB or a paired RGB LUT; native indexed color, alpha,
	 * coverage, depth and picking remain unchanged. World geometry, cutout actors
	 * and the projected viewmodel share this field; palette blends are excluded.
	 * A viewmodel only receives light: its authoritative world body casts shadows. */
	bool receivesFirePointLight = false;
	/** Interior opt-in marker; RGB samples the global field, index remains local. */
	uint32_t pointGeometryOffset = 0xFFFFFFFFU;
	bool receivesTownPointLight = false;
	bool townPointDiffuse = true;
	/** Ground albedo uses continuous world X/Z; coverage always uses the original
	 * projected UV. Offset rebases a region without changing local geometry. */
	bool groundWorldUv = false;
	float groundWorldUvScale = 1.458F / 2.0F;
	std::array<float, 2> groundWorldUvOffset { 0, 0 };
};

/** Immutable native blend table at [destination * 256 + source]. Borrowed
 * bytes are uploaded by SetPaletteBlend, never retained. A warm matching
 * identity may omit bytes; changed content requires a new revision. */
struct TownGpuPaletteBlend {
	uint64_t stableKey = 0;
	uint64_t revision = 0;
	std::span<const uint8_t> lookup;
};

/** Same light-space depth convention as town_shadow: larger is nearer the light. */
struct TownGpuShadow {
	uint64_t stableKey = 0;
	uint64_t revision = 0;
	int resolution = 0;
	std::span<const float> depth;
	std::array<float, 3> right { 1, 0, 0 };
	std::array<float, 3> up { 0, 0, 1 };
	std::array<float, 3> light { 0, 1, 0 };
	float minU = 0;
	float minV = 0;
	float texelU = 1;
	float texelV = 1;
	int pcfRadius = 1;
};

struct TownGpuFrame {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> indexed;
	/** ARGB8888 continuous visual color. Viewmodels may overlay this color while
	 * depth/picks remain those of the world behind them. */
	std::vector<uint32_t> rgba;
	/** Zero denotes no pick record; the caller owns the frame's ID-to-record table. */
	std::vector<uint32_t> pickIds;
	/** Unnormalized camera depth, infinity for untouched pixels. */
	std::vector<float> depth;
	/** 0/1 coverage of emitted viewmodel fragments, width*height bytes. Zero in
	 * ordinary frames. Atmosphere must skip 1 before AA color/ownership resolve;
	 * this mask is independent of authored/palette alpha and gameplay ownership. */
	std::vector<uint8_t> viewModelMask;
	/** Published only with the complete successful readback. The host must skip
	 * CPU volume integration when this count matches its admitted descriptors. */
	bool fireVolumesApplied = false;
	uint32_t fireVolumeSources = 0;
	bool townPointLightingApplied = false;
	uint32_t townPointLightCount = 0;
	uint64_t townPointFieldIdentity = 0, townPointFieldRevision = 0;
	uint64_t townPointFrameSerial = 0;
};

inline constexpr size_t TownGpuMaxFireVolumeSources = 4;
inline constexpr size_t TownGpuMaxFireVolumeRays = 65536;
inline constexpr unsigned TownGpuFireVolumeSteps = 24;

struct TownGpuFireVolumeSource {
	TownFireVolumeVec3 position;
	uint32_t identity = 0;
	float energy = 0;
	TownFireVolumeConfig config;
	/** Inclusive raster-space ROI, already multiplied by sampleFactor. */
	int minX = 0, minY = 0, maxX = -1, maxY = -1;
	/** R2: full ROI remains unchanged; only transport integrations use this grid. */
	unsigned samplingStride = 1;
};

struct TownGpuFireVolumeStats {
	bool ready = false, dispatched = false, applied = false;
	size_t sourceCount = 0, plannedRays = 0;
	size_t roiPixels = 0, adaptiveSources = 0, compositePixels = 0, prefixBufferBytes = 0;
	unsigned maximumSamplingStride = 1;
	unsigned steps = TownGpuFireVolumeSteps;
	/** CPU command submission only; not GPU execution time or whole-game FPS. */
	double commandMilliseconds = 0;
	/** Timestamp queries, sampled once after the existing readback. Unknown is
	 * false/-1 (query absent, not ready, disjoint or invalid), never fake zero. */
	bool gpuTimingKnown = false;
	double gpuMilliseconds = -1;
	double lastShaderCompileMilliseconds = -1;
	uint64_t shaderCompilations = 0, shaderCacheHits = 0;
	size_t retainedBytecodeBytes = 0;
	/** Exact retained CPU blob sizes by CS / CSField / CSComposite; GPU driver
	 * shader allocations and D3DCompile temporary memory are separate unknowns. */
	std::array<size_t, 3> retainedBytecodeBytesByEntry {};
	std::string failure;
};

/** Optional preflight after Begin, BEFORE geometry/billboard omission. Copies
 * all sources/camera/palette/tone inputs and prepares GPU objects/constants.
 * Refusal leaves the normal GPU frame valid; callers retain original flames.
 * Empty sources disable this pass. No borrowed span survives. After accepted
 * omission, any dispatch/status/device failure invalidates the WHOLE frame.
 * The pilot is C1 only: no post-readback Town horizon/fog may follow this pass. */
bool TownGpuSetFireVolumes(const TownCameraFrame &camera, unsigned sampleFactor,
	double seconds, std::span<const TownGpuFireVolumeSource> sources,
	std::span<const uint32_t> palette256Argb, std::span<const uint32_t> toneRamp256Argb);
const TownGpuFireVolumeStats &GetTownGpuFireVolumeStats();
enum class TownGpuFireVolumeFault { None, Preflight, BeforeDispatch, AfterDispatch, BeforeStatusReadback };
/** One-shot, diagnostic only. Install after Begin; Begin/reset clears it. */
void SetTownGpuFireVolumeFaultForDiagnostics(TownGpuFireVolumeFault fault);

/** Capacity is a bounded workload/resource limit, distinct from invalid input
 * or device failure. A failed recording retains its first cause until Begin
 * or Reset; callers may retry capacity pressure after the workload changes. */
enum class TownGpuFailureKind {
	None,
	Capacity,
	InvalidInput,
	Device,
	Unsupported,
};

/** Same-frame GPU timeline from geometry draws through fire compute and MRT
 * copies. Excludes CPU preparation/uploads and SDL Present; this is not FPS.
 * Optional queries are read once after successful readback, without flushing.
 * Unknown/disjoint/unavailable means known=false and -1, never a fabricated 0.
 * frameSerial is process-monotonic across resource resets; 0 has no frame. */
struct TownGpuWorldFrameTiming {
	bool known = false;
	uint64_t frameSerial = 0;
	double gpuMilliseconds = -1;
};
const TownGpuWorldFrameTiming &GetTownGpuWorldFrameTiming();

struct TownGpuStatus {
	bool available = false;
	bool frameSucceeded = false;
	uint64_t townPointFrameSerial = 0;
	bool warp = false;
	std::string adapter;
	uint32_t featureLevel = 0;
	std::string failure;
	TownGpuFailureKind failureKind = TownGpuFailureKind::None;
	/** Aggregate draws, including reused resident indices. Not a stream budget. */
	size_t submittedTriangles = 0;
	size_t drawCalls = 0;
	size_t cachedTextures = 0;
	size_t cachedLightLuts = 0;
	/** Actual resident texel, opacity and padded LUT payload, budget 256 MiB. */
	size_t cachedTextureBytes = 0;
	/** Per-frame immutable upload payload; warm shared LUTs/texels count zero. */
	size_t uploadedTexelBytes = 0;
	size_t uploadedLutBytes = 0;
	size_t textureEvictions = 0;
	/** Submitted color-only overlay triangles, limited to 2048 per frame. */
	size_t paletteBlendTriangles = 0;
	/** Actual issued R8 prior-color snapshot region payload, not memory-bus
	 * read/write traffic or merely recorded/refused submission payload. */
	size_t paletteBlendCopyBytes = 0;
	double frameMilliseconds = 0;
	double readbackMilliseconds = 0;
	/** Conservative R8 snapshot payload committed by accepted overlay triangles.
	 * Sum of their stored clamped copy boxes, bounded to 512 MiB before recording.
	 * Can exceed actual issued bytes when EndFrame refuses or aborts the frame. */
	size_t paletteBlendAdmittedBytes = 0;
	/** Parallel linear COLOR0 stream, only when nonwhite projected vertices are
	 * recorded; bounded to 48 MiB. The legacy projected stream remains 80 B. */
	size_t uploadedAuthoredColorBytes = 0;
	/** Appended source ABI: full recompile required. Actual DXGI numeric ID and
	 * first numeric HRESULT of this recording; unknown stays false. Session
	 * first-error registry survives backend resource resets. */
	uint32_t vendorId = 0, deviceId = 0;
	int32_t failureCode = 0;
	bool failureCodeKnown = false;
	/** Appended source ABI: rebuild consumers. Process-session CPU shader cache
	 * counters survive GPU resource resets. Compilations count completed real
	 * D3DCompile calls, including failures; hits reuse exact embedded inputs.
	 * Bytes count retained bytecode only (two fixed variants, at most 8 MiB),
	 * not GPU resources or compiler scratch memory. Non-Windows stays zero. */
	uint64_t shaderCompilations = 0, shaderCacheHits = 0;
	size_t shaderBytecodeBytes = 0;
	/** Appended source ABI: rebuild consumers. Bounded point-shadow atlas payload
	 * only, excluding device overhead/constant buffers. Upload bytes are per
	 * frame; resident bytes survive Begin but are released on resource reset. */
	size_t fireShadowUploadedBytes = 0, fireShadowBytes = 0;
	size_t fireLightCount = 0;
	size_t townPointLightCount = 0, townPointShadowBytes = 0, townPointShadowUploadedBytes = 0;
};

/** Hardware only by default. WARP fallback requires this explicit diagnostic flag. */
bool TownGpuBeginFrame(int width, int height, bool allowWarpForDiagnostics = false,
    const TownGpuProjection &projection = {});
/** Nonzero only while a successful frame is recording. Every successful Begin
 * uses a new token, including after resource reset; reset/failure/end return 0.
 * This is not the resettable cache TTL serial. */
uint64_t GetTownGpuRecordingFrame();
/** Abandon recording without readback/publication; retain immutable caches/token. */
void TownGpuDiscardFrame();
/** Validate/upload the exact immutable texture payload and pin its resources
 * for the active frame without submitting geometry or changing material/pass
 * state. Only after success may the caller omit resident payload spans while
 * preserving identities, revisions, layout and rgbaLightLutEnabled. Prepare
 * again after a new recording token, owner/revision or lighting mode. Changed
 * supplied contents/layout still pass through the complete GetTexture guards.
 * Failure invalidates the frame; borrowed spans are not retained. */
bool TownGpuPrepareTexture(const TownGpuTexture &texture, TownGpuLighting lighting);
/** Warm lookup pins resident immutable resources without hashing supplied
 * payloads again. A miss leaves the recording untouched. Layout errors retain
 * the ordinary invalid-frame policy. */
bool TownGpuPrepareTextureIfResident(const TownGpuTexture &texture, TownGpuLighting lighting);
/** Optional material upload. Cache-budget refusal keeps the recording valid
 * so the host may draw its original material; malformed/device failures remain
 * failures. No recorded/current-frame resource is evicted. */
bool TownGpuTryPrepareTexture(const TownGpuTexture &texture, TownGpuLighting lighting);
/** Call after Begin; an empty view disables directional shadows for this frame. */
/** Optional RGB palette and native visual light field after Begin, before geometry. */
bool TownGpuSetSmoothLighting(std::span<const uint32_t> palette, std::span<const float> nativeLight, int fieldWidth, int fieldHeight);
/** Optional world-space RGB irradiance for opted-in sprites/entities. Call
 * after Begin and before any geometry. Copies <=TownMaxPointLights lights and <=32 shells with
 * <=8 apertures each; borrowed spans are never retained. Empty inputs disable
 * the field. Begin/reset disable it; invalid input invalidates the whole frame.
 * This field does not replace the separate diffuse interior-lighting material. */
bool TownGpuSetSpritePointLighting(std::span<const TownPointLight> lights,
    std::span<const TownLightOccluder> occluders = {});
/** After Begin and before geometry. Copies current flickered lights and uploads
 * <=4*6*64*64 axial depths by immutable identity/revision. Source positions and
 * radii must match the maps exactly. Empty maps/lights disable the frame field;
 * malformed inputs invalidate the complete frame. No borrowed span is retained. */
/** Own PS:b4/t12, all admitted Town32 sources. Must precede geometry. */
bool TownGpuSetTownPointGeometryLighting(const TownGeometricPointShadowView &field,
    std::span<const TownPointLight> currentLights, bool shadowsEnabled = true);

bool TownGpuSetFirePointLighting(const TownPointShadowMaps &maps,
    std::span<const TownPointLight> currentFlickeredLights, bool shadowsEnabled = true);
bool TownGpuSetShadow(const TownGpuShadow &shadow);
/** After Begin and before any geometry. Exact 65536-byte table on cache miss;
 * nonzero stableKey required. One immutable table is bound for the frame.
 * Capacity/input/device failures invalidate the complete frame. */
bool TownGpuSetPaletteBlend(const TownGpuPaletteBlend &blend);
bool TownGpuSubmitProjectedTriangle(const std::array<TownGpuVertex, 3> &vertices,
    const TownGpuTexture &texture, const TownGpuMaterial &material, uint32_t pickId);
/** Synchronous readback publishes complete color/ownership/depth/mask together,
 * or clears output on failure. */
bool TownGpuEndFrame(TownGpuFrame &output);
/** One-shot diagnostic callback before readback allocation, default null.
 * Begin/reset clears it. Exception tests require backend C++ unwinding. */
void SetTownGpuBeforeReadbackForDiagnostics(void (*callback)()) noexcept;
/** Invalidate device, frame, cached uploads and borrowed outputs before a scene
 * reset. The bounded, device-independent CPU bytecode cache survives until
 * process exit; its exact source/options key prevents stale shader reuse. */
void ResetTownGpuResources();
const TownGpuStatus &GetTownGpuStatus();

} // namespace devilution
