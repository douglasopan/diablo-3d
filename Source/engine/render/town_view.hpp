#pragma once

#include <cstddef>

#include "engine/point.hpp"
#include "engine/render/town_lighting.hpp"

namespace devilution {

struct Surface;
struct TownVolumeMesh;

/** Experimental rotatable view of the live Tristram map. Native isometric pose by default. */
bool IsTownViewActive();
/** Exact original framing: native drawing and selection remain authoritative. */
bool IsTownViewNativePose();
void ToggleTownView();
void RotateTownView(float radians);
void AdjustTownViewDistance(float delta);
void OrbitTownView(float yawDelta, float pitchDelta);
void ZoomTownView(float wheelSteps);
void ResetTownViewCamera();
struct TownViewCameraState {
	float yaw;
	float pitch;
	float distance;
	float offsetX;
	float offsetZ;
};
TownViewCameraState GetTownViewCameraState();
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
	bool cabinLampEnabled = true;
};
TownViewLightingState GetTownViewLightingState();

/** Headless evidence for real interior illumination; production defaults on. */
void SetTownViewCabinLampEnabledForDiagnostics(bool enabled);

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
/** Whole-tree source at left and its explicit opacity mask at right. */
bool DrawTownViewVegetationDiagnostic(const Surface &out, size_t groupIndex, Point *sourceSize = nullptr);
/** Cached complete tree mesh for coverage diagnostics; invalidated on reset. */
const TownVolumeMesh *TownViewVegetationVolume(size_t groupIndex);
/** Cached whole-object prop mesh; invalidated on reset. */
const TownVolumeMesh *TownViewPropVolume(size_t groupIndex);

} // namespace devilution
