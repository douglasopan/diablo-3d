#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>

#include "control/control.hpp"
#include "engine/palette.h"
#include "engine/render/scrollrt.h"
#include "engine/surface.hpp"
#include "utils/ui_fwd.h"
#include "town_camera_runtime_checks.hpp"
#include "town_cathedral_artwork_checks.hpp"

namespace devilution {

/** Entry for the existing town_view_smoke target's --camera branch. The caller
 * supplies its own InitializeTownDiagnostic, Check, NativeSceneState,
 * ViewportPixels and SavePng functions. Call in a fresh diagnostic process,
 * after main has selected BasePath/AssetsPath and private PrefPath/ConfigPath.
 * This is native-asset offscreen validation, not UI input or game-FPS evidence.
 * No old executable or separately linked historical engine objects belong here.
 */
template <typename InitializeFn, typename CheckFn, typename NativeSnapshotFn,
    typename ViewportPixelsFn, typename CaptureFn>
void RunTownCameraRuntimeEntry(const std::filesystem::path &output,
    InitializeFn initialize, CheckFn check, NativeSnapshotFn nativeSnapshot,
    ViewportPixelsFn viewportPixels, CaptureFn capture, std::ostream &report)
{
	initialize();
	const auto nativeBefore = nativeSnapshot();
	const auto randomBefore = GetLCGEngineState();
	RunTownCathedralArtworkChecks(check);
	check(nativeSnapshot() == nativeBefore && GetLCGEngineState() == randomBefore,
	    "cathedral artwork fixture restores native state and RNG");
	GraphicsOptions &graphics = GetOptions().Graphics;
	struct RestoreViewport {
		GraphicsOptions &graphics;
		uint16_t width, height, viewport;
		bool zoom, gpu, smoothing, active;
		~RestoreViewport()
		{
			graphics.zoom.SetValue(zoom);
			graphics.townViewGpuRendering.SetValue(gpu);
			graphics.townViewAntialiasing.SetValue(smoothing);
			gnScreenWidth = width;
			gnScreenHeight = height;
			CalculatePanelAreas();
			CalcViewportGeometry();
			gnViewportHeight = viewport;
			if (IsTownViewActive() != active)
				ToggleTownView();
		}
	} restore { graphics, gnScreenWidth, gnScreenHeight, gnViewportHeight,
		*graphics.zoom, *graphics.townViewGpuRendering, *graphics.townViewAntialiasing, IsTownViewActive() };
	graphics.zoom.SetValue(false);
	graphics.townViewAntialiasing.SetValue(false);
	check(!IsLeftPanelOpen() && !IsRightPanelOpen(), "camera entry starts without side panels");
	if (!IsTownViewActive())
		ToggleTownView();
	std::filesystem::create_directories(output);
	std::ofstream manifest(output / "camera-captures.csv");
	check(manifest.good(), "open camera capture manifest");
	manifest << "file,width,height,mode,gpu,horizon,sampling,yaw,pitch,distance,fov\n";
	constexpr std::array<const char *, 10> FrameNames {
		"cpu-isometric", "cpu-free-orbit", "cpu-third-person", "cpu-first-person",
		"cpu-first-person-no-horizon", "gpu-isometric", "gpu-free-orbit",
		"gpu-third-person", "gpu-first-person", "gpu-first-person-no-horizon"
	};
	for (const Point dimensions : { Point { 960, 540 }, Point { 1920, 1080 } }) {
		gnScreenWidth = static_cast<uint16_t>(dimensions.x);
		gnScreenHeight = static_cast<uint16_t>(dimensions.y);
		CalculatePanelAreas();
		CalcViewportGeometry();
		check(gnViewportHeight == dimensions.y, "camera entry retains the entire wide world viewport");
		const std::string size = std::to_string(dimensions.x) + "x" + std::to_string(dimensions.y);
		const auto directory = output / size;
		std::filesystem::create_directories(directory);
		OwnedSurface allocation(dimensions.x, dimensions.y + 2);
		SDL_SetPaletteColors(allocation.surface->format->palette, logical_palette.data(), 0, 256);
		SDL_FillRect(allocation.surface, nullptr, 255);
		const Surface out = allocation.subregionY(0, dimensions.y);
		const auto guardsIntact = [&] {
			for (int y = dimensions.y; y < allocation.h(); ++y)
				for (int x = 0; x < allocation.w(); ++x)
					if (allocation[{ x, y }] != 255)
						return false;
			return true;
		};
		graphics.townViewGpuRendering.SetValue(false);
		ResetTownViewCamera();
		for (int y = 0; y < out.h(); ++y)
			std::memset(out.at(0, y), 0, out.w());
		check(DrawNativeTownViewReference(out, ViewPosition), size + " draws the actual original backend");
		const auto nativePixels = viewportPixels(out);
		capture(out, directory / "native-reference.png");
		check(DrawTownView(out, false) && IsTownViewNativePose(), size + " Home takes the original backend");
		check(viewportPixels(out) == nativePixels, size + " Home is pixel-identical to the original backend");
		capture(out, directory / "home-original.png");
		check(guardsIntact(), size + " original backend preserves target guard rows");
		size_t frame = 0;
		const auto capturePixels = [&](const Surface &surface) {
			check(frame < FrameNames.size(), size + " camera capture sequence remains bounded");
			const std::string name = std::string(FrameNames[frame++]) + ".png";
			capture(surface.subregionY(0, gnViewportHeight), directory / name);
			const TownViewCameraState state = GetTownViewCameraState();
			const TownViewRendererState renderer = GetTownViewRendererState();
			const TownViewSamplingState sampling = GetTownViewSamplingState();
			manifest << size << '/' << name << ',' << dimensions.x << ',' << dimensions.y << ','
			         << static_cast<int>(state.mode) << ',' << renderer.usedGpu << ','
			         << *graphics.townViewHorizon << ',' << sampling.factor << ','
			         << state.yaw << ',' << state.pitch << ',' << state.distance << ',' << state.verticalFovDegrees << '\n';
			check(guardsIntact(), size + " camera rasterizers preserve target guard rows");
			return viewportPixels(surface);
		};
		report << "CAMERA NATIVE-ASSET FIXTURE " << size << " (hardware GPU required, smoothing disabled)\n";
		RunTownCameraRuntimeChecks(out, check, nativeSnapshot, capturePixels, report, true);
		check(frame == FrameNames.size(), size + " records all ten CPU/GPU native-asset camera frames");
		check(nativeSnapshot() == nativeBefore && GetLCGEngineState() == randomBefore,
		    size + " camera entry preserves native state and RNG after captures and controls");
	}
	check(manifest.good(), "write camera capture manifest");
	report << "LIMIT: 24 native-asset PNGs, fixed town/player fixture, 960x540 and 1920x1080. "
	          "Home comparison validates the shared original backend; no visual acceptance, "
	          "2x sampling, UI event dispatch, moving-actor or game-FPS claim.\n";
}

} // namespace devilution
