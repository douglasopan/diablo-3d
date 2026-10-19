#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

#include "engine/point.hpp"
#include "engine/render/town_camera.hpp"
#include "engine/render/town_lighting.hpp"

namespace devilution {

struct Surface;
struct TownVolumeMesh;
struct TownSceneModel;
namespace cathedral {
struct NativeTextureBinding;
struct PilotFrame;
}

/** Candidate native floor color overlay; call Prepare once before DrawFloor.
 * No GPU/3D submission. Exact native64x32 mask and route coordinates are shared
 * with the3D overlay. Missing/pin mismatch leaves the original floor intact. */
void PrepareTownEntrancePathNativeFrame();
bool DrawTownEntrancePathNativeFloor(const Surface &out, Point tile, Point nativeFloorBottomLeft);

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
TownViewOgdenPilotState GetTownViewGriswoldPilotState();
void SetTownViewGriswoldGpuSubmissionFailureForDiagnostics(bool enabled);
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
/** Level-only reset: retain admitted immutable chest models, reset all slot state. */
void ResetTownViewResourcesForLevel();

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
/** Same current color/depth/pick epoch as ordinary selection. Captured FP may
 * swing into empty space using the camera's horizontal direction, without
 * inventing an entity target or reusing a stale/unavailable world frame. */
bool PickTownViewStandingAttack(Point screen, TownViewPickResult &result);
/** Memory-only preparation after native missile interpolation, before DrawGame. */
bool IsTownViewEnabled();
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
	size_t ceilingMaterials = 0;
	size_t lightBodyTriangles = 0, lightBodyBytes = 0, lightGroundPanels = 0;
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
	// OriginalUpperColumn is raw artwork for an explicitly bounded visual panel,
	// not proof of full-cell composition, native3D depth or visibility.
	bool originalUpperColumn = false;
	bool originalFullColumn = false;
	bool lightingBaked = false, perPixelLighting = false;
	bool nativeOperationsPartitioned = false;
	uint8_t operationPass = 0; // Raw getter always unpartitioned; lit getter identifies its draw pass.
	uint32_t operationRevision = 0;
	uint64_t rawSignature = 0, lightSignature = 0;
	uint64_t authoredPixelHash = 0;
	uint8_t authoredMaterial = 0; // 0: native; 1: own floor r1; 2: own opaque wall r1.
	uint8_t activeMicroTileLength = 0;
	uint32_t packingRevision = 0;
	std::array<uint16_t, 16> upperWords {};
	std::array<uint16_t, 16> sourceWords {};
};
TownViewCathedralNativeTextureReference GetTownViewCathedralNativeTextureReference(
	const cathedral::NativeTextureBinding &binding);
/** Approximate native-stone ceiling, read only; no floor material reuse. */
TownViewCathedralNativeTextureReference GetTownViewCathedralCeilingTextureReference();
/** Copy of the current epoch's native light-stand art, for bounded visual QA. */
TownViewCathedralNativeTextureReference GetTownViewCathedralLightBodyTextureReference();

/** Read-only prepared presentation, for comparison with native lighting. */
TownViewCathedralNativeTextureReference GetTownViewCathedralLitTextureReference(
	const cathedral::NativeTextureBinding &binding);
/** Read-only copy of the actual current prepared runtime frame. False leaves
 * out unchanged; no map/Frame construction, decode, render or export occurs. */
bool CopyTownViewCathedralFrameForDiagnostics(cathedral::PilotFrame &out);

/** Read-only native cell/material evidence; does not prepare/decode a texture.
 * Black texels/light are distinct from absent binding. A named macro family
 * and SOL flags are metadata, not authority to suppress geometry.
 */
struct TownViewCathedralCellMaterialDiagnostic {
	bool current = false, nativeIsFloor = false, nativeWallTransparency = false;
	bool hasBaseWords = false, hasUpperWords = false, nativeObjectActive = false;
	bool nativeLightPieceAndObjectRule = false, objectAppliesLighting = false;
	uint64_t epoch = 0;
	int sourceX = 0, sourceZ = 0, objectSlot = -1, objectType = -1, nativeLightId = -1;
	uint16_t piece = 0;
	uint8_t sol = 0, light = 0, transparencyGroup = 0, rawMega = 0, namedMegaFamily = 0;
	int8_t signedObjectOccupancy = 0;
	uint32_t objectAnimationFrame = 0;
	std::array<uint16_t, 16> sourceWords {};
	size_t technicalTriangles = 0, boundTriangles = 0, preparedReferences = 0;
	size_t suppressedTechnicalTriangles = 0, nativePanelTriangles = 0;
	size_t coveredTexels = 0, coveredIndex0 = 0, coveredIndex255 = 0;
};
TownViewCathedralCellMaterialDiagnostic GetTownViewCathedralCellMaterialDiagnostic(int sourceX, int sourceZ);
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
/** Logical UI anchors from the last completed 3D world frame. Labels retain
 * native through-wall highlighting; they do not depend on a winning pick pixel.
 * The span is borrowed until the next world draw/resource reset. */
struct TownViewItemLabelAnchor {
	int itemIndex;
	Point position;
	Point tile;
};
std::span<const TownViewItemLabelAnchor> GetTownViewItemLabelAnchors();
/** Zero when the published world frame is stale, native, or unavailable. */
uint64_t TownViewItemLabelFrameId();
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

struct TownViewSarcophagusInfo { bool loaded; size_t bodyTriangles,lidTriangles,draws; };
TownViewSarcophagusInfo GetTownViewSarcophagusInfo();

struct TownViewBarrelInfo {bool loaded;size_t triangles,meshDraws,nativeBreakDraws,nativeExplosionDraws,shardDraws,terminalShardDraws;};
TownViewBarrelInfo GetTownViewBarrelInfo();

size_t GetTownViewCorpseGroundDraws();

} // namespace devilution

namespace devilution {
struct TownViewChestInfo { bool loaded; size_t triangles,draws; };
/** Freeze presentation prop time only; negative restores SDL clock. */
void SetTownViewPropAnimationTimeForDiagnostics(int64_t milliseconds);
TownViewChestInfo GetTownViewChestInfo();
TownViewChestInfo GetTownViewChestVariantInfo(int variant);
float GetTownViewChestOpenProgress(int slot);
}

namespace devilution {
struct TownViewDoorModelInfo { bool loaded; size_t leafTriangles,archTriangles,leafDraws,archDraws,residentDraws; };
TownViewDoorModelInfo GetTownViewDoorModelInfo();
float GetTownViewDoorOpenProgress(int slot);
}

namespace devilution {
struct TownViewBrazierInfo {bool loaded;size_t triangles,meshDraws,flameDraws;float flameContact;};
TownViewBrazierInfo GetTownViewBrazierInfo();
}

namespace devilution {
struct TownViewSkullFireInfo {bool loaded;size_t triangles,meshDraws,flameDraws;float minHeight,maxHeight,flameContact;size_t flameCoveredTexels;};
TownViewSkullFireInfo GetTownViewSkullFireInfo();
}

namespace devilution {
/** Frame-local RGB effect telemetry. Counts describe the last completed 3D
 * world only when published is true. No particle owns native state or a pick. */
struct TownViewFireParticleState {
	bool enabled = false, published = false, budgetLimited = false;
	size_t emitters = 0, sampledParticles = 0, projectedParticles = 0;
	size_t candidatePixels = 0, occludedPixels = 0, blendedPixels = 0;
	double compositionMilliseconds = 0;
};
TownViewFireParticleState GetTownViewFireParticleState();
/** Comparison only, not a saved preference. The effect follows Smooth 3D
 * Lighting; disabling it retains the indexed world exactly. Reset restores ON. */
void SetTownViewFireParticlesEnabledForDiagnostics(bool enabled);

/** Bounded experimental C1 brazier plume. Presentation only, sampled afresh
 * from the same source identity/time/energy as25B; no persistent effect state.
 * Indexed recovery is drawn separately without taking depth or pick ownership. */
struct TownViewFireVolumeState {
	bool enabled = false, published = false, budgetLimited = false;
	bool gpuRequested = false, gpuApplied = false, gpuDeclined = false;
	size_t eligible = 0, admitted = 0, drawn = 0, plannedPixels = 0;
	/** R2 distinguishes full composited coverage from bounded transport samples. */
	size_t roiPixels = 0, plannedSampleRays = 0, adaptiveSources = 0;
	unsigned maximumSamplingStride = 1;
	size_t rays = 0, steps = 0, blendedPixels = 0, occludedPixels = 0;
	size_t indexedFallbackTriangles = 0, indexedFallbackPixels = 0;
	size_t recoveredEmitters = 0, failedRays = 0, scratchBytes = 0;
	uint64_t epoch = 0;
	double compositionMilliseconds = 0;
};
TownViewFireVolumeState GetTownViewFireVolumeState();
/** Private A/B control; not a new saved preference. Full resource reset restores ON. */
void SetTownViewFireVolumesEnabledForDiagnostics(bool enabled);
/** Forces transactional per-source recovery for the candidate's native gate. */
void SetTownViewFireVolumeFailureForDiagnostics(bool enabled);
/** Forces the full reference CPU budget for bounded A/B fixtures, not gameplay. */
void SetTownViewFireVolumeCpuForDiagnostics(bool enabled);
enum class TownViewFireVolumeGpuFailurePoint { None, Preflight, BeforeDispatch, AfterDispatch, BeforeStatusReadback };
/** Armed by a fixture before the real host frame; installed after GPU Begin. */
void SetTownViewFireVolumeGpuFailureForDiagnostics(TownViewFireVolumeGpuFailurePoint point);

/** Last completed RGB fire field. This owns no native object or preference.
 * Geometry, opacity and doors invalidate its CPU cache; camera/flicker do not.
 * First increment admits at most four native C1 brazier/skull fires and structural meshes.
 * Native indexed color/depth/picking and simulation remain independent. */
struct TownViewCathedralFireState {
	bool enabled=false, ready=false, published=false, cacheHit=false, failed=false, budgetLimited=false;
	bool automaticCasters=false, staticCacheHit=false, dynamicCacheHit=false;
	uint64_t staticMapRevision=0, dynamicMapRevision=0;
	size_t staticTriangles=0, dynamicTriangles=0, propTriangles=0, actorTriangles=0, alphaQuadTriangles=0;
	size_t ownedMaskBytes=0, dynamicKeyBytes=0, retainedMapBytes=0;
    size_t bodyTriangles=0,equipmentTriangles=0,nativeActorQuadTriangles=0,actorFallbacks=0;
	size_t lights=0, triangles=0, masks=0, mapBytes=0, scratchBytes=0;
	size_t rays=0, boxTests=0, triangleTests=0;
	uint64_t epoch=0, geometryRevision=0, mapRevision=0;
	double prepareMilliseconds=0, buildMilliseconds=0;
};
TownViewCathedralFireState GetTownViewCathedralFireState();

/** Current Town32 geometry field; whole-field refusal uses approved legacy
 * room/Church lighting and is never reported as a qualified new shadow. */
struct TownViewTownPointGeometryState {
    bool enabled=false,ready=false,published=false,failed=false,budgetLimited=false,fallback=false;
    bool staticGatherCacheHit=false,gpuRequested=false,gpuApplied=false,gpuDeclined=false;
    uint8_t failure=0;
    uint64_t epoch=0,identity=0,revision=0,gpuFrameSerial=0;
    size_t rooms=0,eligibleSources=0,admittedSources=0,rejectedSources=0,invalidSources=0;
    size_t staticTriangles=0,dynamicTriangles=0,masks=0,maskBytes=0;
    size_t staticMapBuilds=0,dynamicMapBuilds=0,staticMapCacheHits=0,dynamicMapCacheHits=0;
    size_t staticBvhBuilds=0,dynamicBvhBuilds=0,roiTests=0,rays=0,triangleTests=0;
    size_t scratchBytes=0,keyBytes=0,retainedBytes=0,publishedDepthBytes=0,cachedDepthBytes=0;
    double sourcePrepareMilliseconds=0,prepareMilliseconds=0,buildMilliseconds=0;
};
TownViewTownPointGeometryState GetTownViewTownPointGeometryState();
void SetTownViewTownPointGeometryEnabledForDiagnostics(bool enabled);
void SetTownViewTownPointGeometryFailureForDiagnostics(bool fail);

struct TownViewAutomaticShadowState {
    bool enabled=false,ready=false,published=false,failed=false,budgetLimited=false,staticGatherCacheHit=false;
    uint64_t epoch=0,staticGeometryRevision=0,dynamicGeometryRevision=0;
    size_t staticTriangles=0,dynamicTriangles=0,masks=0,ownedMaskBytes=0,actorFallbacks=0;
    double prepareMilliseconds=0;
};
TownViewAutomaticShadowState GetTownViewAutomaticShadowState();
enum class TownViewAutomaticShadowFailurePoint : uint8_t { None, DynamicCollection, DynamicBuild, Merge };
/** RAM-only diagnostic controls; full resource reset restores automatic ON. */
void SetTownViewAutomaticFireCastersEnabledForDiagnostics(bool enabled);
void SetTownViewAutomaticFireShadowFailureForDiagnostics(TownViewAutomaticShadowFailurePoint point);
/** Private A/B controls, reset to ON with full resources; not saved settings. */
void SetTownViewCathedralFireLightingForDiagnostics(bool enabled, bool shadowsEnabled=true);
}
