#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace devilution {

// Read-only observations, not a second fallback policy or a backend diagnosis.
enum class CathedralFallbackReason : uint8_t {
	None, ContextIneligible, PlayerUnavailable, RefreshLiveFrameRefused,
	LiveBindingsUnavailable, PilotFrameFallback, PreparationAllocationFailure,
	NativeMaterialBudgetRefused, NativeMaterialValidationRefused,
	PreparationLengthError, PreparationException, RenderDisabled,
	NativeResourcesUnavailable, PilotNotReady, MaterialBindingsStale,
	ViewportUnavailable, NativePoseReference, CameraProjectionInvalid,
	GpuRequestDeferred, GpuBeginRefused, GpuSubmissionRefused,
	GpuEndFrameRefused, GpuOutputRejected, DrawAllocationFailure, Published3D,
};

enum class CathedralFallbackStage : uint8_t {
	None, Context, RefreshLiveFrame, LiveBindings, BuildPilotFrame,
	NativeMaterials, Preflight, Camera, SamplingBuffers, GpuBegin, GpuShadow, GpuPaletteBlend,
	Submission, GpuEnd, OutputValidation, Resolve, SessionTrace, Published,
};

/** The latest terminal observation. POD capture remains usable under heap
 * pressure. First focus is latched on a classification transition; latest focus
 * can move without creating a log line. GPU status may describe an earlier
 * backend attempt when gpuAttempted is false: it is context, never causation.
 * RefreshLiveFrameRefused is intentionally opaque inside cathedral_live. */
struct TownViewCathedralFallbackDiagnostic {
	CathedralFallbackReason reason = CathedralFallbackReason::None;
	CathedralFallbackStage stage = CathedralFallbackStage::None;
	bool nativeFallback = false, enabled = false, eligible = false, playerPresent = false;
	bool requestedGpu = false, usedGpu = false, gpuAttempted = false, forceGeometry = false;
	bool setLevel = false, frameReady = false, frameRequiresFallback = false, pickingValid = false;
	int level = -1, levelType = -1, setLevelId = -1;
	int focusX = -1, focusZ = -1, firstFocusX = -1, firstFocusZ = -1;
	uint32_t tick = 0, firstTick = 0, frameFallbackFlags = 0;
	uint32_t seed = 0;
	bool seedValid = false, materialBindingValid = false;
	int regionX = 0, regionZ = 0;
	size_t regions = 0, frameTriangles = 0, nativeSpecialOverlays = 0;
	int materialKind = -1, materialAxis = -1, materialPiece = -1, materialColumn = -1;
	int materialNativeSlot = -1, materialX = -1, materialZ = -1;
	uint64_t liveEpoch = 0, frameGeometryRevision = 0, frameEpoch = 0, hostEpoch = 0;
	size_t hostEntries = 0, hostBytes = 0, hostEntryLimit = 0, hostByteLimit = 0;
	size_t hostDecodes = 0, hostHits = 0, hostMisses = 0;
	bool gpuAvailable = false, gpuFrameSucceeded = false;
	int gpuFailureKind = 0, recoveryFailureKind = 0;
	std::array<char, 192> gpuFailureText {};
	bool gpuFailureTextTruncated = false;
	size_t gpuTextures = 0, gpuTextureBytes = 0, gpuTriangles = 0, gpuDrawCalls = 0;
	size_t gpuOverlayTriangles = 0, gpuOverlayCopyBytes = 0;
	uint64_t observations = 0, transitions = 0, logLines = 0, logFailures = 0;
};

static_assert(std::is_trivially_copyable_v<TownViewCathedralFallbackDiagnostic>);
static_assert(std::is_nothrow_copy_assignable_v<TownViewCathedralFallbackDiagnostic>);

/** Owning native/SDL thread only. Copies numbers, never strings or allocations.
 * This API does not change resources, camera, eligibility, options or picking. */
TownViewCathedralFallbackDiagnostic GetTownViewCathedralFallbackDiagnostic() noexcept;

constexpr const char *CathedralFallbackReasonName(CathedralFallbackReason reason) noexcept
{
	switch (reason) {
#define D3D_CATHEDRAL_REASON_NAME(name) case CathedralFallbackReason::name: return #name
	D3D_CATHEDRAL_REASON_NAME(None);
	D3D_CATHEDRAL_REASON_NAME(ContextIneligible);
	D3D_CATHEDRAL_REASON_NAME(PlayerUnavailable);
	D3D_CATHEDRAL_REASON_NAME(RefreshLiveFrameRefused);
	D3D_CATHEDRAL_REASON_NAME(LiveBindingsUnavailable);
	D3D_CATHEDRAL_REASON_NAME(PilotFrameFallback);
	D3D_CATHEDRAL_REASON_NAME(PreparationAllocationFailure);
	D3D_CATHEDRAL_REASON_NAME(NativeMaterialBudgetRefused);
	D3D_CATHEDRAL_REASON_NAME(NativeMaterialValidationRefused);
	D3D_CATHEDRAL_REASON_NAME(PreparationLengthError);
	D3D_CATHEDRAL_REASON_NAME(PreparationException);
	D3D_CATHEDRAL_REASON_NAME(RenderDisabled);
	D3D_CATHEDRAL_REASON_NAME(NativeResourcesUnavailable);
	D3D_CATHEDRAL_REASON_NAME(PilotNotReady);
	D3D_CATHEDRAL_REASON_NAME(MaterialBindingsStale);
	D3D_CATHEDRAL_REASON_NAME(ViewportUnavailable);
	D3D_CATHEDRAL_REASON_NAME(NativePoseReference);
	D3D_CATHEDRAL_REASON_NAME(CameraProjectionInvalid);
	D3D_CATHEDRAL_REASON_NAME(GpuRequestDeferred);
	D3D_CATHEDRAL_REASON_NAME(GpuBeginRefused);
	D3D_CATHEDRAL_REASON_NAME(GpuSubmissionRefused);
	D3D_CATHEDRAL_REASON_NAME(GpuEndFrameRefused);
	D3D_CATHEDRAL_REASON_NAME(GpuOutputRejected);
	D3D_CATHEDRAL_REASON_NAME(DrawAllocationFailure);
	D3D_CATHEDRAL_REASON_NAME(Published3D);
#undef D3D_CATHEDRAL_REASON_NAME
	}
	return "Unknown";
}

constexpr const char *CathedralFallbackStageName(CathedralFallbackStage stage) noexcept
{
	switch (stage) {
#define D3D_CATHEDRAL_STAGE_NAME(name) case CathedralFallbackStage::name: return #name
	D3D_CATHEDRAL_STAGE_NAME(None);
	D3D_CATHEDRAL_STAGE_NAME(Context);
	D3D_CATHEDRAL_STAGE_NAME(RefreshLiveFrame);
	D3D_CATHEDRAL_STAGE_NAME(LiveBindings);
	D3D_CATHEDRAL_STAGE_NAME(BuildPilotFrame);
	D3D_CATHEDRAL_STAGE_NAME(NativeMaterials);
	D3D_CATHEDRAL_STAGE_NAME(Preflight);
	D3D_CATHEDRAL_STAGE_NAME(Camera);
	D3D_CATHEDRAL_STAGE_NAME(SamplingBuffers);
	D3D_CATHEDRAL_STAGE_NAME(GpuBegin);
	D3D_CATHEDRAL_STAGE_NAME(GpuShadow);
	D3D_CATHEDRAL_STAGE_NAME(GpuPaletteBlend);
	D3D_CATHEDRAL_STAGE_NAME(Submission);
	D3D_CATHEDRAL_STAGE_NAME(GpuEnd);
	D3D_CATHEDRAL_STAGE_NAME(OutputValidation);
	D3D_CATHEDRAL_STAGE_NAME(Resolve);
	D3D_CATHEDRAL_STAGE_NAME(SessionTrace);
	D3D_CATHEDRAL_STAGE_NAME(Published);
#undef D3D_CATHEDRAL_STAGE_NAME
	}
	return "Unknown";
}

} // namespace devilution
