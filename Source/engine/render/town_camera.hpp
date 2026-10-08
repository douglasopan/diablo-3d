#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace devilution {

/** Visual coordinates only: one unit is one native tile, height is up. */
struct TownCameraPoint {
	float x = 0;
	float height = 0;
	float z = 0;
	TownCameraPoint operator+(TownCameraPoint other) const;
	TownCameraPoint operator-(TownCameraPoint other) const;
	TownCameraPoint operator*(float scale) const;
};

enum class TownCameraMode : uint8_t { Isometric, FreeOrbit, ThirdPerson, FirstPerson };

/** Yaw is the direction FROM the anchor TO the eye. First person looks back
 * along that direction too. Modes restore their own saved yaw; matching yaws
 * have matching view directions. Positive pitch looks down. This state never
 * changes the native actor's facing/input. */
struct TownCameraPose {
	float yaw = 0.7853981634F;
	float pitch = 0.5235987756F;
	float distance = 22;
	TownCameraPoint pan;
};

/** Projection defaults; the runtime adapter persists FOV and look sensitivity. */
struct TownCameraPreferences {
	float verticalFovDegrees = 60;
	float nearClip = 0.08F;
	float farClip = 320;
	float eyeHeight = 1.1F; // Provisional art scale; calibrate against native hero.
	float orbitRadiansPerPixel = 0.006F;
	float zoomExponentPerStep = 0.12783337F;
};

/** Stores a separate pose for each mode. F4 suspension is owned by the caller;
 * Suspend preserves poses; RestoreIsometric implements the Home contract. */
class TownCameraRig {
public:
	TownCameraRig();
	TownCameraMode mode() const { return mode_; }
	const TownCameraPose &pose() const;
	const TownCameraPreferences &preferences() const { return preferences_; }
	uint64_t revision() const { return revision_; }
	bool suspended() const { return suspended_; }
	bool SetMode(TownCameraMode mode);
	void SetPose(TownCameraPose pose);
	void SetPreferences(TownCameraPreferences preferences);
	void Orbit(float yawDelta, float pitchDelta);
	void Zoom(float steps);
	void Pan(float x, float z);
	void Suspend(bool suspended);
	void RestoreIsometric();
	bool HideLocalPlayer() const;

private:
	std::array<TownCameraPose, 4> poses_;
	TownCameraPreferences preferences_;
	TownCameraMode mode_ = TownCameraMode::Isometric;
	bool suspended_ = false;
	uint64_t revision_ = 0;
};

/** eye is authored world space. Basis vectors operate after height scaling;
 * Isometric alone retains sqrt(2/3) and the legacy fixed eye offset/zoom. */
struct TownCameraFrame {
	TownCameraPoint eye;
	TownCameraPoint right;
	TownCameraPoint up;
	TownCameraPoint forward;
	float heightScale = 1;
	float focalPixels = 1;
	float centerX = 0;
	float centerY = 0;
	float nearClip = 0.08F;
	float farClip = 320;
	float fogDepthOffset = 0; // Legacy eye=256 must not fog the nearby town.
	int width = 0;
	int height = 0;
	bool perspective = false;
	bool valid = false;
};

/** Native centers and zoom are supplied by the existing viewport adapter;
 * perspective uses the same logical viewport/center, without UI rescaling.
 * FOV is nominal for a centered view; an off-center viewport is an asymmetric
 * frustum with the same focal length. No camera/scene collision is performed. */
TownCameraFrame BuildTownCameraFrame(const TownCameraRig &rig, TownCameraPoint anchor,
	int width, int height, float centerX, float centerY, float nativeZoom = 1);
TownCameraPoint TownCameraToView(const TownCameraFrame &frame, TownCameraPoint world);
TownCameraPoint TownCameraToWorld(const TownCameraFrame &frame, TownCameraPoint view);

struct TownCameraVertex {
	TownCameraPoint world;
	float u = 0;
	float v = 0;
};

struct TownCameraProjectedVertex {
	float x = 0;
	float y = 0;
	float depth = 0; // Positive view distance; lower is nearer.
	float reciprocalW = 1; // 1/depth in perspective, 1 in orthographic.
	TownCameraVertex source;
};

/** Six-plane view clipping before division. A clipped triangle has <=9 vertices
 * and <=7 triangles; fixed capacity avoids frame allocations. World/UV values
 * are clipped linearly, then corrected during screen-space interpolation. */
struct TownCameraClippedTriangles {
	std::array<std::array<TownCameraProjectedVertex, 3>, 7> triangles;
	size_t count = 0;
};
TownCameraClippedTriangles ClipTownCameraTriangle(const TownCameraFrame &frame,
	const std::array<TownCameraVertex, 3> &triangle);
bool ProjectTownCameraPoint(const TownCameraFrame &frame, TownCameraPoint world,
	TownCameraProjectedVertex &output);
/** D3D11 [0,1] depth. Perspective clipW=depth; orthographic clipW=1.
 * Returns NaN for invalid/out-of-range input. Does not replace camera depth. */
float NormalizeTownCameraDepth(const TownCameraFrame &frame, float depth);
/** Color-only atmospheric depth. Orthographic depth removes the artificial
 * 256-unit eye offset, replacing it with the selected legacy view distance.
 * Raster depth and selection must continue using the original depth value. */
float TownCameraFogDepth(const TownCameraFrame &frame, float depth);

struct TownCameraSample {
	TownCameraPoint world;
	float depth = 0;
	float u = 0;
	float v = 0;
};
/** Barycentrics are screen-space weights; output depth, world and UV come from
 * the same corrected weights. Accepted tiny negative edge errors clamp to zero
 * before normalization, never extrapolating beyond the triangle. The caller
 * publishes color/depth/pick together. */
bool InterpolateTownCameraSample(const std::array<TownCameraProjectedVertex, 3> &triangle,
	const std::array<float, 3> &barycentric, TownCameraSample &output);

struct TownCameraRay {
	TownCameraPoint origin;
	TownCameraPoint direction;
	bool valid = false;
};
/** Diagnostic ray only. Pass pixel centers (x+.5,y+.5) to match raster samples.
 * Never turn a horizon/missed ray into native movement. */
TownCameraRay TownCameraScreenRay(const TownCameraFrame &frame, float x, float y);

} // namespace devilution
