#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

#include "engine/random.hpp"
#include "engine/render/town_gpu.hpp"
#include "engine/render/town_view.hpp"
#include "engine/surface.hpp"
#include "levels/dun_tile_data.hpp"
#include "options.h"
#include "player.h"

namespace devilution {

/** Include normally, then call after InitializeTownDiagnostic() with an
 * offscreen Surface and the smoke harness's Check, NativeSceneState and
 * ViewportPixels callbacks. This function never initializes a game, saves
 * options, touches profiles or advances the simulation. GPU is hardware-only;
 * requireGpu=false records unavailable GPU coverage as SKIP, never as a pass.
 *
 * Example: RunTownCameraRuntimeChecks(out, Check, NativeSceneState,
 *              ViewportPixels, std::cout, true);
 *
 * This checks native assets from one stationary fixture. It does not certify
 * artistic quality, all map boundaries, UI event dispatch, sustained FPS,
 * combat, moving actors, every hero class or sky visibility from every pose.
 */
template <typename CheckFn, typename NativeSnapshotFn, typename ViewportPixelsFn>
void RunTownCameraRuntimeChecks(const Surface &out, CheckFn check,
    NativeSnapshotFn nativeSnapshot, ViewportPixelsFn viewportPixels,
    std::ostream &report, bool requireGpu = true)
{
	const auto close = [](float a, float b) { return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < 0.0001F; };
	const auto samePose = [&](const TownViewCameraState &a, const TownViewCameraState &b) {
		return a.mode == b.mode && close(a.yaw, b.yaw) && close(a.pitch, b.pitch)
		    && close(a.distance, b.distance) && close(a.offsetX, b.offsetX) && close(a.offsetZ, b.offsetZ);
	};
	const auto poseFromState = [](const TownViewCameraState &state) {
		return TownCameraPose { state.yaw, state.pitch, state.distance, { state.offsetX, 0, state.offsetZ } };
	};
	const std::array<TownCameraMode, 4> modes { TownCameraMode::Isometric, TownCameraMode::FreeOrbit,
		TownCameraMode::ThirdPerson, TownCameraMode::FirstPerson };
	const std::array<const char *, 4> names { "isometric", "free-orbit", "third-person", "first-person" };
	check(MyPlayer != nullptr && leveltype == DTYPE_TOWN && out.w() > 0 && out.h() > 0,
	    "camera runtime fixture has an initialized native town and an offscreen surface");
	if (MyPlayer == nullptr || leveltype != DTYPE_TOWN || out.w() <= 0 || out.h() <= 0)
		return;
	const auto nativeBefore = nativeSnapshot();
	const auto randomBefore = GetLCGEngineState();
	const bool wasActive = IsTownViewActive();
	const TownCameraMode originalMode = GetTownViewCameraMode();
	GraphicsOptions &graphics = GetOptions().Graphics;

	// Preserve both user preferences in memory and every session pose. No
	// SaveOptions or InitializeTownViewForGame call belongs in this fixture.
	struct Restore {
		GraphicsOptions &graphics;
		bool active;
		TownCameraMode mode;
		int savedMode, fov, sensitivity;
		bool gpu, smoothing, horizon;
		std::array<TownCameraPose, 4> poses;
		~Restore()
		{
			graphics.townViewCameraMode.SetValue(savedMode);
			graphics.townViewCameraFov.SetValue(fov);
			graphics.townViewCameraSensitivity.SetValue(sensitivity);
			graphics.townViewGpuRendering.SetValue(gpu);
			graphics.townViewAntialiasing.SetValue(smoothing);
			graphics.townViewHorizon.SetValue(horizon);
			ApplyTownViewCameraPreferences();
			for (size_t i = 0; i < poses.size(); ++i) {
				SetTownViewCameraMode(static_cast<TownCameraMode>(i));
				SetTownViewCameraPoseForDiagnostics(poses[i]);
			}
			SetTownViewCameraMode(mode);
			if (IsTownViewActive() != active)
				ToggleTownView();
			EndTownViewCameraDrag();
		}
	} restore { graphics, wasActive, originalMode, *graphics.townViewCameraMode,
		*graphics.townViewCameraFov, *graphics.townViewCameraSensitivity,
		*graphics.townViewGpuRendering, *graphics.townViewAntialiasing, *graphics.townViewHorizon, {} };
	for (size_t i = 0; i < modes.size(); ++i) {
		SetTownViewCameraMode(modes[i]);
		restore.poses[i] = poseFromState(GetTownViewCameraState());
	}
	if (!IsTownViewActive())
		ToggleTownView();
	graphics.townViewAntialiasing.SetValue(false);
	graphics.townViewCameraMode.SetValue(3);
	SetTownViewCameraMode(TownCameraMode::FirstPerson);
	graphics.townViewCameraFov.SetValue(75);
	graphics.townViewCameraSensitivity.SetValue(125);
	ApplyTownViewCameraPreferences();
	check(close(GetTownViewCameraState().verticalFovDegrees, 75), "runtime applies configured FOV");
	ResetTownViewCamera();
	check(IsTownViewNativePose() && GetTownViewCameraMode() == TownCameraMode::Isometric
	        && *graphics.townViewCameraMode == 3, "Home restores the original session view without replacing the saved first-person preference");
	graphics.townViewCameraFov.SetValue(80);
	ApplyTownViewCameraPreferences();
	check(IsTownViewNativePose() && *graphics.townViewCameraMode == 3
	        && close(GetTownViewCameraState().verticalFovDegrees, 80), "FOV preferences do not undo Home");
	CycleTownViewCameraMode();
	check(GetTownViewCameraMode() == TownCameraMode::FreeOrbit && *graphics.townViewCameraMode == 1,
	    "runtime camera cycle updates the in-memory saved preference and session mode");
	graphics.townViewCameraFov.SetValue(75);
	ApplyTownViewCameraPreferences();

	struct PickSample {
		bool picked = false;
		Point tile { -1, -1 };
		int npc = -1, item = -1, player = -1, architecture = -1;
		float depth = std::numeric_limits<float>::infinity();
		bool SameIdentity(const PickSample &other) const
		{
			return picked == other.picked && tile == other.tile && npc == other.npc && item == other.item
			    && player == other.player && architecture == other.architecture;
		}
	};
	const auto readPicks = [&](int height) {
		std::vector<PickSample> result(static_cast<size_t>(out.w()) * height);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < out.w(); ++x) {
				PickSample &sample = result[static_cast<size_t>(y) * out.w() + x];
				sample.picked = PickTownView({ x, y }, sample.tile, sample.npc, sample.item, sample.player);
				sample.architecture = TownViewArchitectureAt({ x, y });
				sample.depth = TownViewDepthAt({ x, y });
			}
		}
		return result;
	};
	const std::array<TownCameraPose, 4> poses { {
		{ 1.0053981634F, 0.5235987756F, 22, {} }, { 1.4F, 0.12F, 12, {} },
		{ -0.4F, 0.16F, 6, {} }, { -0.4F, 0.06F, 0, {} },
	} };
	for (const bool gpu : { false, true }) {
		graphics.townViewGpuRendering.SetValue(gpu);
		for (size_t i = 0; i < modes.size(); ++i) {
			const std::string label = std::string(gpu ? "GPU " : "CPU ") + names[i];
			SetTownViewCameraMode(modes[i]);
			SetTownViewCameraPoseForDiagnostics(poses[i]);
			graphics.townViewHorizon.SetValue(true);
			check(DrawTownView(out, true), label + " renders native assets with forced geometry");
			const TownViewRendererState renderer = GetTownViewRendererState();
			if (gpu && !renderer.usedGpu) {
				report << "SKIP " << label << " hardware coverage: " << renderer.failure << '\n';
				if (requireGpu)
					check(false, label + " requires hardware GPU rendering, rather than CPU fallback");
				continue;
			}
			check(gpu ? renderer.requestedGpu && renderer.usedGpu && renderer.cpuRasterizedTriangles == 0
			        && renderer.gpuSubmittedTriangles > 0 && GetTownGpuStatus().frameSucceeded
			             : !renderer.requestedGpu && !renderer.usedGpu && renderer.cpuRasterizedTriangles > 0,
			    label + " uses its requested rasterizer and publishes a complete frame");
			check(renderer.horizonTriangles > 0 && renderer.horizonBytes > 0, label + " includes bounded horizon geometry");
			const int height = std::min(out.h(), GetTownViewSamplingState().height);
			const auto picks = readPicks(height);
			const auto pixels = viewportPixels(out);
			size_t sky = 0, unownedGeometry = 0, nativePixels = 0, localHeroPixels = 0;
			bool identitiesValid = true, skyUnselectable = true;
			for (const PickSample &sample : picks) {
				if (sample.picked) {
					++nativePixels;
					identitiesValid &= InDungeonBounds(sample.tile) && std::isfinite(sample.depth)
					    && sample.npc >= -1 && sample.item >= -1 && sample.player >= -1
					    && sample.player < static_cast<int>(Players.size());
					localHeroPixels += sample.player >= 0 && sample.player < static_cast<int>(Players.size())
					        && &Players[sample.player] == MyPlayer ? 1 : 0;
				} else if (std::isfinite(sample.depth)) {
					++unownedGeometry;
				} else {
					++sky;
					skyUnselectable &= sample.npc == -1 && sample.item == -1 && sample.player == -1 && sample.architecture == -1;
				}
			}
			check(nativePixels > 0 && identitiesValid && skyUnselectable, label + " publishes visible native identities and leaves sky unselectable");
			if (modes[i] == TownCameraMode::FirstPerson)
				check(localHeroPixels == 0, label + " publishes no pixels identified as the local hero's body");
			report << label << " pixels: native=" << nativePixels << ", sky=" << sky
			       << ", unownedGeometry=" << unownedGeometry << ", localHero=" << localHeroPixels << '\n';
			check(nativeSnapshot() == nativeBefore && GetLCGEngineState() == randomBefore,
			    label + " preserves native map, collision, actors, facing and RNG");

			if (modes[i] == TownCameraMode::FirstPerson) {
				graphics.townViewHorizon.SetValue(false);
				check(DrawTownView(out, true), label + " draws the same pose without horizon/fog");
				check(GetTownViewRendererState().usedGpu == gpu, label + " horizon A/B retains the same rasterizer");
				const auto noHorizonPicks = readPicks(height);
				const auto noHorizonPixels = viewportPixels(out);
				size_t stableNative = 0, changedColor = 0, visibilityChanged = 0;
				bool rawDepthPreserved = true;
				for (size_t pixel = 0; pixel < picks.size(); ++pixel) {
					if (picks[pixel].picked && noHorizonPicks[pixel].picked
					    && picks[pixel].SameIdentity(noHorizonPicks[pixel])) {
						++stableNative;
						rawDepthPreserved &= close(picks[pixel].depth, noHorizonPicks[pixel].depth);
						changedColor += pixels[pixel] != noHorizonPixels[pixel] ? 1 : 0;
					} else if (!picks[pixel].SameIdentity(noHorizonPicks[pixel])) {
						++visibilityChanged;
					}
				}
				check(stableNative > 0 && rawDepthPreserved, label + " fog preserves raw depth on equally visible native identities");
				report << label << " horizon A/B: stableNative=" << stableNative << ", colorChanged=" << changedColor
				       << ", identityOrVisibilityChanged=" << visibilityChanged
				       << "; zero visible fog samples does not prove an atmospheric color change\n";
				graphics.townViewHorizon.SetValue(true);
			}

			const auto beforeSuspend = GetTownViewCameraState();
			ToggleTownView();
			check(!IsTownViewActive(), label + " F4 suspends the 3D view");
			ToggleTownView();
			check(IsTownViewActive() && samePose(beforeSuspend, GetTownViewCameraState()), label + " F4 restores the saved session pose");
			const Point dragStart { out.w() / 2, height / 2 };
			const auto beforeLook = GetTownViewCameraState();
			check(BeginTownViewCameraDrag(dragStart) && UpdateTownViewCameraDrag(dragStart + Displacement { 2, 2 }),
			    label + " middle mouse look updates the camera");
			EndTownViewCameraDrag();
			const auto afterLook = GetTownViewCameraState();
			check(close(afterLook.yaw - beforeLook.yaw, 0.02F) && close(afterLook.pitch - beforeLook.pitch, 0.015F),
			    label + " middle mouse look honors the configured sensitivity");
			const bool canPan = modes[i] == TownCameraMode::Isometric || modes[i] == TownCameraMode::FreeOrbit;
			const bool beganPan = BeginTownViewCameraDrag(dragStart, true);
			check(beganPan == canPan, label + " Shift-middle pan respects the selected mode");
			if (beganPan) {
				check(UpdateTownViewCameraDrag(dragStart + Displacement { 4, 3 }), label + " pan consumes pointer motion");
				const auto afterPan = GetTownViewCameraState();
				check(!close(afterPan.offsetX, afterLook.offsetX) || !close(afterPan.offsetZ, afterLook.offsetZ), label + " pan updates the stored rig offset");
			}
			EndTownViewCameraDrag();
			check(nativeSnapshot() == nativeBefore && GetLCGEngineState() == randomBefore,
			    label + " camera controls preserve the native simulation");
		}
	}
	report << "LIMIT: these are offscreen native-asset fixtures; unowned finite pixels are not asserted to be exclusively horizon,"
	          " no exact CPU/GPU edge identity comparison is made, and no gameplay input or artistic approval is claimed.\n";
}

} // namespace devilution
