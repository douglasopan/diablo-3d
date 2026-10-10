#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/point.hpp"
#include "engine/render/town_camera.hpp"
#include "engine/render/town_lighting.hpp"

namespace devilution {

struct Surface;
struct TownVolumeMesh;
struct TownSceneModel;
namespace cathedral { struct NativeTextureBinding; }

/** Rotatable view of the live Tristram map. */
bool IsTownViewActive();
/** Apply the saved startup preference once per new/loaded game session. */
void InitializeTownViewForGame();
/** Exact original framing: native drawing and selection remain authoritative. */
bool IsTownViewNativePose();
void ToggleTownView();
void RotateTownView(float radians);
void AdjustTownViewDistance(float delta);
void OrbitTownView(float yawDelta, float pitchDelta);
void ZoomTownView(float wheelSteps);
/** Once per eligible world frame; zero pauses visual interpolation. */
bool AdvanceTownViewCamera(float seconds);
/** Retained exact native comparison for diagnostics. */
void ResetTownViewCamera();
/** Gameplay Home restores third-person follow without persisting a new mode. */
void ResetTownViewGameplayCamera();
TownCameraMode GetTownViewCameraMode();
/** Session mode only; Home does not overwrite the saved startup preference. */
void SetTownViewCameraMode(TownCameraMode mode);
/** Cycles and updates the saved preference through the normal option setter. */
void CycleTownViewCameraMode();
void ApplyTownViewCameraPreferences();
/** Private fixture control; does not write options or the native player. */
void SetTownViewCameraPoseForDiagnostics(TownCameraPose pose);
struct TownViewCameraState {
	float yaw;
	float pitch;
	float distance;
	float offsetX;
	float offsetZ;
	TownCameraMode mode = TownCameraMode::Isometric;
	float verticalFovDegrees = 60;
	float eyeHeight = 1.1F;
};
TownViewCameraState GetTownViewCameraState();
/** Visual-only collision evidence, separate from the requested camera pose. */
struct TownViewFollowCameraState {
	bool active = false, transition = false, blocked = false, initialOverlap = false;
	bool valid = true, localPlayerHidden = false;
	float desiredDistance = 0, visualDistance = 0, resolvedDistance = 0;
	float eyeHeight = 0, radius = 0;
	TownCameraPoint desiredEye {}, resolvedEye {};
	uint64_t sceneRevision = 0;
	size_t triangles = 0, bytes = 0, nodesVisited = 0, trianglesTested = 0, cacheBuilds = 0;
};
TownViewFollowCameraState GetTownViewFollowCameraState();
/** Local Tristram architecture mask, frozen for color/depth/picking/replay.
 * Open imports are contact-only; props/trees/Cathedral are outside this v1. */
struct TownViewCameraVisibilityState {
	bool active = false, valid = false, budgetExceeded = false;
	uint64_t sceneRevision = 0, revision = 0;
	size_t owners = 0, hiddenOwners = 0, certifiedComponents = 0;
	size_t uncertifiedAtEye = 0, ambiguousAtEye = 0;
	size_t nodesVisited = 0, trianglesTested = 0, bytes = 0, cacheBuilds = 0;
};
TownViewCameraVisibilityState GetTownViewCameraVisibilityState();
bool IsTownViewArchitectureHiddenForDiagnostics(size_t owner);
/** Explicit opt-in for the new policy after a legacy pose fixture disabled it. */
void SetTownViewGameplayCameraPolicyForDiagnostics(bool enabled);
/** Effective world sampling; camera, pointer coordinates and UI remain logical. */
struct TownViewSamplingState {
	bool requested = false;
	int factor = 1;
	int width = 0;
	int height = 0;
	bool limited = false;
};
TownViewSamplingState GetTownViewSamplingState();
/** Last world draw, including explicit fallback and CPU work for validation. */
struct TownViewRendererState {
	bool requestedGpu = false;
	bool usedGpu = false;
	size_t cpuRasterizedTriangles = 0;
	size_t gpuSubmittedTriangles = 0;
	std::string failure;
	size_t horizonTriangles = 0;
	size_t horizonBytes = 0;
	size_t cpuPixelVisits = 0;
	size_t cpuCoveredFragments = 0;
	size_t cpuDepthRejected = 0;
	size_t cpuShadedFragments = 0;
	/** World draw/recording only; exclude HUD, presentation and simulation. */
	double worldMilliseconds = 0;
	double sceneRecordMilliseconds = 0;
};
TownViewRendererState GetTownViewRendererState();
/** Read-only evidence for the opt-in Ogden transport pilot; not art acceptance. */
struct TownViewOgdenPilotState {
	bool attempted = false;
	bool loaded = false;
	bool drawn = false;
	int nativeIndex = -1;
	size_t trianglesVisited = 0;
	uint64_t nativeTicks = 0;
	uint64_t clockGeneration = 0;
	float sampleSeconds = 0;
	double loopSeconds = 0;
};
TownViewOgdenPilotState GetTownViewOgdenPilotState();
/** Simulate one allocation failure after GPU submission; defaults off on reset. */
void SetTownViewOgdenGpuSubmissionFailureForDiagnostics(bool enabled);
/** Real assembled world geometry, including interior and every flame pose.
 * Invalid/empty bounds are retained conservatively by the visibility test. */
struct TownArchitectureBounds {
	TownCameraPoint minimum;
	TownCameraPoint maximum;
	bool valid = false;
};
TownArchitectureBounds BuildTownArchitectureBounds(const TownSceneModel &model);
/** Conservative six-plane test before perspective division. Touching, near/eye
 * crossings and boxes surrounding the camera remain eligible for clipping. */
bool IsTownArchitectureBoundsVisible(const TownCameraFrame &frame, const TownArchitectureBounds &bounds);
/** Last architecture pass; boundsComputed counts cold work in this frame,
 * including a possible GPU attempt before CPU fallback. No mesh LOD is implied. */
struct TownViewArchitectureCullingState {
	bool requested = false;
	size_t modelsConsidered = 0;
	size_t modelsSubmitted = 0;
	size_t modelsCulled = 0;
	size_t trianglesVisited = 0;
	size_t boundsComputed = 0;
	size_t cachedBounds = 0;
};
TownViewArchitectureCullingState GetTownViewArchitectureCullingState();
/** Borrowed world-only 2x image, or nullptr when its draw epoch is stale. */
const Surface *GetTownViewHighResolutionFrame();
bool BeginTownViewCameraDrag(Point screen, bool pan = false);
bool UpdateTownViewCameraDrag(Point screen);
void EndTownViewCameraDrag();
bool IsTownViewCameraDragging();
/** Call when level graphics are released/reloaded, including same-address reloads. */
void ResetTownViewResources();

/** Calibration uses one world-space light for imported albedo and shadow depth. */
struct TownViewLightingState {
	TownLightingConfig configuration;
	bool profileLoaded = false;
	size_t importedTextures = 0;
	size_t albedoColors = 0;
	size_t albedoTableBytes = 0;
	size_t lightLevels = 0;
	size_t cabinInteriors = 0;
	bool cabinFireEnabled = true;
	bool directionalShadowsEnabled = true;
};
TownViewLightingState GetTownViewLightingState();

/** Headless evidence for real interior illumination; production defaults on. */
void SetTownViewCabinFireEnabledForDiagnostics(bool enabled);
/** Same-camera shadow A/B; only directional geometry shadows are disabled.
 * Terrain colors, diffuse illumination, depth and selection remain unchanged.
 * Production and resource reloads default to enabled. */
void SetTownViewDirectionalShadowsEnabledForDiagnostics(bool enabled);
/** A nonnegative time freezes visual flames for reproducible captures; negative
 * restores the runtime timer. Simulation time and random state are untouched. */
void SetTownViewFireTimeForDiagnostics(double seconds);
/** Private A/B of the resident path; no preferences, assets or simulation writes. */
void SetTownViewResidentMeshesEnabledForDiagnostics(bool enabled);
/** Subpixel reference sensitivity fixture; production defaults to zero. */
void SetTownViewRasterJitterForDiagnostics(float x, float y);

/** Draw only the world viewport. forceGeometry is for reconstruction diagnostics. */
bool DrawTownView(const Surface &fullOut, bool forceGeometry = false);
/** Select the frontmost rendered pixel. Entity indices are -1 when there is no entity. */
bool PickTownView(Point screen, Point &tile, int &townerIndex, int &itemIndex, int &playerIndex);
/** Native dungeon bindings share the rendered pixel's color/depth ownership. */
struct TownViewPickResult {
	Point tile { 0, 0 };
	int townerIndex = -1, itemIndex = -1, playerIndex = -1;
	int objectIndex = -1, monsterIndex = -1;
};
bool PickTownViewDetailed(Point screen, TownViewPickResult &result);
/** Memory-only preparation after native missile interpolation, before DrawGame. */
void PrepareTownViewLiveFrame();
void InvalidateTownViewFrameForNativeFallback();
struct TownViewCathedralState {
	bool eligible = false, ready = false, nativeFallback = false;
	uint64_t epoch = 0, revision = 0;
	size_t regions = 0, triangles = 0;
};
TownViewCathedralState GetTownViewCathedralState();
/** Private native-material diagnostics. Counts are cache work, not FPS. */
struct TownViewCathedralNativeTextureState {
	uint64_t epoch = 0;
	size_t entries = 0, bytes = 0, decodes = 0, hits = 0, misses = 0;
	size_t floorMaterials = 0, masonryMaterials = 0, doorMaterials = 0, donorMaterials = 0;
};
TownViewCathedralNativeTextureState GetTownViewCathedralNativeTextureState();
/** Read-only copy of an already prepared private runtime texture; no decode.
 * Empty dimensions mean absent/stale/unsupported. No file export is performed. */
struct TownViewCathedralNativeTextureReference {
	int width = 0, height = 0;
	std::vector<uint8_t> pixels, opacity;
	uint64_t gpuIdentity = 0, epoch = 0;
	int sourceX = 0, sourceZ = 0, nativeSlot = -1, cropX = 0, cropY = 0;
	uint16_t requestedPiece = 0, sourcePiece = 0, sourceBlock = 0, secondBlock = 0;
	uint8_t sourceMicro = 0, sourceColumn = 0;
	bool repeat = false, approximate = false, horizontalFlip = false;
};
TownViewCathedralNativeTextureReference GetTownViewCathedralNativeTextureReference(
	const cathedral::NativeTextureBinding &binding);
/** Private Cathedral allocation fault injection. One-shot, GPU-only, default off.
 * BeforeReadback fires in EndFrame after GPU commands, before output allocation.
 * Requires exception-unwinding support in town_view.cpp and town_gpu.cpp. */
enum class TownViewCathedralAllocationFailurePoint : uint8_t {
	None,
	BeforeOpaque,
	AfterOpaque,
	BeforeReadback,
};
struct TownViewCathedralAllocationState {
	TownViewCathedralAllocationFailurePoint armed = TownViewCathedralAllocationFailurePoint::None;
	TownViewCathedralAllocationFailurePoint injected = TownViewCathedralAllocationFailurePoint::None;
	/** Authoritative allocation-failure marker even when error text cannot allocate. */
	bool failed = false;
	size_t gpuSubmittedBeforeFailure = 0, gpuDrawCallsBeforeFailure = 0;
	size_t cpuRasterizedBeforeFailure = 0;
};
void SetTownViewCathedralAllocationFailureForDiagnostics(TownViewCathedralAllocationFailurePoint point) noexcept;
TownViewCathedralAllocationState GetTownViewCathedralAllocationState() noexcept;
/** Private offscreen gate through the actual DrawGame world branch, without UI. */
bool DrawWorldViewForDiagnostics(const Surface &out, Point viewPosition);
/** Diagnostic ownership of a visible architectural surface; -1 for other pixels. */
int TownViewArchitectureAt(Point screen);
/** Depth of the nearest visible geometric surface, infinity when unavailable. */
float TownViewDepthAt(Point screen);
/** Screen position of the tile's ground center, for labels and other world overlays. */
Point TownViewScreenPosition(Point tile);
/** Diagnostic: cached textures beside native tile decoding, without projection. */
bool DrawTownViewTileDiagnostic(const Surface &out, Point tile);
/** Local authoring reference: the same cleaned/fallback floor pixels used by
 * the 3D renderer, in top-row-first RGBA. No camera or simulation is changed. */
struct TownGroundReferenceTexture {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
};
TownGroundReferenceTexture GetTownGroundReferenceTexture(uint16_t piece, bool fallback);
/** Whole-tree source at left and its explicit opacity mask at right. */
bool DrawTownViewVegetationDiagnostic(const Surface &out, size_t groupIndex, Point *sourceSize = nullptr);
/** Cached complete tree mesh for coverage diagnostics; invalidated on reset. */
const TownVolumeMesh *TownViewVegetationVolume(size_t groupIndex);
/** Cached whole-object prop mesh; invalidated on reset. */
const TownVolumeMesh *TownViewPropVolume(size_t groupIndex);

} // namespace devilution
