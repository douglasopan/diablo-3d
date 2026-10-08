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
void ResetTownViewCamera();
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
};
TownViewRendererState GetTownViewRendererState();
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

/** Draw only the world viewport. forceGeometry is for reconstruction diagnostics. */
bool DrawTownView(const Surface &fullOut, bool forceGeometry = false);
/** Select the frontmost rendered pixel. Entity indices are -1 when there is no entity. */
bool PickTownView(Point screen, Point &tile, int &townerIndex, int &itemIndex, int &playerIndex);
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
