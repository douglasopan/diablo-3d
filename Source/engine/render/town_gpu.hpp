#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/render/town_lighting.hpp"

namespace devilution {

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
};

/** Palette uses exact shade 0..15 with a 16-level caller-supplied native LUT.
 * Legacy requires 256 codes; an explicit RGB pair may have a larger source
 * code domain while indexed output still uses its paired uint8 LUT. It does
 * not calculate directional shadows. Optional sprite point RGB is a separate
 * frame field; it leaves the indexed shade/LUT result untouched. */
enum class TownGpuLighting : uint32_t { Unlit, Shadow, Directional, Interior, Palette };

struct TownGpuMaterial {
	TownGpuLighting lighting = TownGpuLighting::Unlit;
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
};

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

struct TownGpuStatus {
	bool available = false;
	bool frameSucceeded = false;
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
/** Invalidate device, frame, cached uploads and borrowed outputs before a scene reset. */
void ResetTownGpuResources();
const TownGpuStatus &GetTownGpuStatus();

} // namespace devilution
