#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "engine/random.hpp"
#include "engine/render/town_gpu.hpp"
#include "engine/render/town_model_import.hpp"
#include "engine/render/town_scene.hpp"
#include "engine/render/town_shadow.hpp"
#include "engine/render/town_view.hpp"
#include "engine/surface.hpp"
#include "options.h"
#include "utils/ui_fwd.h"

namespace devilution {

/** Analytic bounds/frustum fixtures; no scene loading, options, SDL or RNG. */
inline bool RunTownArchitectureCullingChecks(std::ostream &out, size_t &checks)
{
	size_t failures = 0;
	const size_t first = checks;
	const auto check = [&](bool condition, const std::string &message) {
		++checks;
		if (!condition) {
			++failures;
			out << "FAIL architecture culling: " << message << '\n';
		}
	};
	TownCameraFrame frame;
	frame.right = { 1, 0, 0 };
	frame.up = { 0, 1, 0 };
	frame.forward = { 0, 0, 1 };
	frame.focalPixels = 100;
	frame.centerX = frame.centerY = 100;
	frame.width = frame.height = 200;
	frame.nearClip = 1;
	frame.farClip = 10;
	frame.valid = true;
	const auto box = [](TownCameraPoint minimum, TownCameraPoint maximum) {
		return TownArchitectureBounds { minimum, maximum, true };
	};
	for (const bool perspective : { false, true }) {
		frame.perspective = perspective;
		const float side = perspective ? 2.0F : 1.0F;
		const std::string prefix = perspective ? "perspective " : "orthographic ";
		check(IsTownArchitectureBoundsVisible(frame, box({ -0.1F, -0.1F, 2 }, { 0.1F, 0.1F, 3 })), prefix + "inside box retained");
		const std::array<TownArchitectureBounds, 6> outside { {
			box({ -0.1F, -0.1F, 0.2F }, { 0.1F, 0.1F, 0.5F }),
			box({ -0.1F, -0.1F, 11 }, { 0.1F, 0.1F, 12 }),
			box({ -3 * side, -0.1F, 2 }, { -2 * side, 0.1F, 2.1F }),
			box({ 2 * side, -0.1F, 2 }, { 3 * side, 0.1F, 2.1F }),
			box({ -0.1F, 2 * side, 2 }, { 0.1F, 3 * side, 2.1F }),
			box({ -0.1F, -3 * side, 2 }, { 0.1F, -2 * side, 2.1F })
		} };
		for (size_t plane = 0; plane < outside.size(); ++plane)
			check(!IsTownArchitectureBoundsVisible(frame, outside[plane]), prefix + "box outside plane " + std::to_string(plane));
		check(IsTownArchitectureBoundsVisible(frame, box({ -0.2F, -0.2F, 0.5F }, { 0.2F, 0.2F, 2 })), prefix + "near crossing retained");
		check(IsTownArchitectureBoundsVisible(frame, box({ -20, -20, -2 }, { 20, 20, 12 })), prefix + "box contains eye and frustum although no corner projects");
		check(IsTownArchitectureBoundsVisible(frame, box({ -2 * side, -2 * side, 2 }, { 2 * side, 2 * side, 2.1F })), prefix + "wall crosses viewport with every corner outside");
		check(IsTownArchitectureBoundsVisible(frame, box({ side, -0.1F, 2 }, { side + 0.005F, 0.1F, 2 })), prefix + "side tangent and numerical margin retained");
		check(IsTownArchitectureBoundsVisible(frame, box({ -0.1F, -0.1F, 0.995F }, { 0.1F, 0.1F, 1 })), prefix + "near tangent retained");
		check(IsTownArchitectureBoundsVisible(frame, box({ -0.1F, -0.1F, 10 }, { 0.1F, 0.1F, 10.005F })), prefix + "far tangent retained");
		check(!IsTownArchitectureBoundsVisible(frame, box({ -0.2F, -0.2F, -3 }, { 0.2F, 0.2F, -1 })), prefix + "box wholly behind eye rejected");
		TownCameraFrame asymmetric = frame;
		asymmetric.centerX = 30;
		asymmetric.centerY = 170;
		check(IsTownArchitectureBoundsVisible(asymmetric, box({ 0.3F * side, -0.1F, 2 }, { 0.4F * side, 0.1F, 2.1F })), prefix + "asymmetric center retained");
		TownCameraFrame sampled = asymmetric;
		sampled.width *= 2;
		sampled.height *= 2;
		sampled.centerX *= 2;
		sampled.centerY *= 2;
		sampled.focalPixels *= 2;
		for (const auto &fixture : outside)
			check(IsTownArchitectureBoundsVisible(asymmetric, fixture) == IsTownArchitectureBoundsVisible(sampled, fixture), prefix + "1x/2x agrees away from guard band");
	}
	const float nan = std::numeric_limits<float>::quiet_NaN();
	check(IsTownArchitectureBoundsVisible(frame, {}), "empty bounds retain conservatively");
	check(IsTownArchitectureBoundsVisible(frame, box({ nan, 0, 0 }, { 1, 1, 1 })), "nonfinite bounds retain conservatively");
	check(IsTownArchitectureBoundsVisible(frame, box({ 5, 5, 5 }, { 1, 1, 1 })), "reversed bounds retain conservatively");
	check(IsTownArchitectureBoundsVisible(TownCameraFrame {}, box({ 100, 100, 100 }, { 101, 101, 101 })), "invalid frame retains conservatively");
	TownCameraFrame invalid = frame;
	invalid.centerX = nan;
	check(IsTownArchitectureBoundsVisible(invalid, box({ 100, 100, 100 }, { 101, 101, 101 })), "nonfinite camera retains conservatively");

	const auto triangle = [](TownCameraPoint p) {
		TownSceneTriangle result {};
		result.vertices = { TownSceneVertex { p.x, p.height, p.z, 0, 0 },
			TownSceneVertex { p.x + 0.1F, p.height, p.z, 1, 0 }, TownSceneVertex { p.x, p.height + 0.1F, p.z, 0, 1 } };
		return result;
	};
	TownSceneModel model {};
	check(!BuildTownArchitectureBounds(model).valid, "empty model produces invalid conservative bounds");
	model.triangles.push_back(triangle({ -20, -5, 30 }));
	const auto sourceBounds = BuildTownArchitectureBounds(model);
	check(sourceBounds.valid && sourceBounds.minimum.x == -20 && sourceBounds.minimum.height == -5
	        && sourceBounds.maximum.z == 30, "actual geometry extrema supersede default physical bounds");
	auto interior = std::make_shared<TownCabinInterior>();
	interior->exteriorTriangles.push_back(triangle({ 10, 0, 2 }));
	interior->interiorTriangles.push_back(triangle({ 0, 0, 2 }));
	interior->fireSources.resize(10); // Include sources beyond the renderer's light budget.
	interior->fireSources.back().emissiveTriangles.push_back(triangle({ -12, 7, 2 }));
	model.cabinInterior = interior;
	model.externalModel = true;
	model.importedTexture = std::make_shared<TownImportedTexture>();
	const auto assembled = BuildTownArchitectureBounds(model);
	check(assembled.valid && assembled.minimum.x == -12 && assembled.maximum.x > 10
	        && assembled.maximum.height > 7 && assembled.maximum.z == 2,
	    "bounds include effective exterior, interior and every fire source while excluding unused source shell");
	check(IsTownArchitectureBoundsVisible(frame, assembled), "interior/fire can retain object when exterior alone is offscreen");
	interior->interiorTriangles.front() = triangle({ 12, 0, 2 });
	interior->fireSources.back().emissiveTriangles.front() = triangle({ 0, 0, 2 });
	auto shell = std::make_shared<TownCabinInterior>(*interior);
	shell->fireSources.clear();
	TownSceneModel shellModel = model;
	shellModel.cabinInterior = shell;
	check(!IsTownArchitectureBoundsVisible(frame, BuildTownArchitectureBounds(shellModel))
	        && IsTownArchitectureBoundsVisible(frame, BuildTownArchitectureBounds(model)),
	    "only a visible flame retains an otherwise offscreen exterior and interior");
	interior->fireSources.back().emissiveTriangles.back().vertices[0].x = nan;
	check(!BuildTownArchitectureBounds(model).valid, "malformed adjunct makes bounds conservative rather than partially small");
	out << "Architecture culling analytic checks=" << checks - first << " failures=" << failures << '\n';
	return failures == 0;
}

/** Asset-backed A/B supplement after InitializeTownDiagnostic. Uses semantic
 * picking, exact depth/color, full shadow map and cold/warm cache evidence.
 * Caller chooses one backend per run; hardware GPU may not silently fall back.
 * No profile writes, player relocation, asset mutation or gameplay RNG. */
template <typename CheckFn, typename SnapshotFn, typename PixelsFn>
void RunTownArchitectureCullingCaptureChecks(const Surface &out, CheckFn check,
    SnapshotFn nativeSnapshot, PixelsFn viewportPixels, std::ostream &report, bool gpu = true, int onlyMode = -1)
{
	struct Pixel {
		Point tile;
		int npc, item, player, architecture;
		float depth;
		bool picked;
	};
	GraphicsOptions &graphics = GetOptions().Graphics;
	const auto native = nativeSnapshot();
	const auto random = GetLCGEngineState();
	const bool active = IsTownViewActive();
	const auto originalMode = GetTownViewCameraMode();
	const bool originalGpu = *graphics.townViewGpuRendering;
	const bool originalAA = *graphics.townViewAntialiasing;
	const bool originalCulling = *graphics.townViewFrustumCulling;
	const bool originalFire = GetTownViewLightingState().cabinFireEnabled;
	std::array<TownCameraPose, 4> originalPoses;
	for (size_t i = 0; i < originalPoses.size(); ++i) {
		SetTownViewCameraMode(static_cast<TownCameraMode>(i));
		const auto state = GetTownViewCameraState();
		originalPoses[i] = { state.yaw, state.pitch, state.distance, { state.offsetX, 0, state.offsetZ } };
	}
	if (!IsTownViewActive())
		ToggleTownView();
	graphics.townViewGpuRendering.SetValue(gpu);
	SetTownViewFireTimeForDiagnostics(1.25);
	SetTownViewCabinFireEnabledForDiagnostics(true);
	const auto pixels = [&]() {
		std::vector<Pixel> values;
		values.reserve(static_cast<size_t>(out.w()) * gnViewportHeight);
		for (int y = 0; y < gnViewportHeight; ++y) {
			for (int x = 0; x < out.w(); ++x) {
				Pixel value {};
				value.tile = { -1, -1 };
				value.picked = PickTownView({ x, y }, value.tile, value.npc, value.item, value.player);
				value.architecture = TownViewArchitectureAt({ x, y });
				value.depth = TownViewDepthAt({ x, y });
				values.push_back(value);
			}
		}
		return values;
	};
	const auto equalPixels = [&](const std::vector<Pixel> &before, const std::vector<Pixel> &after) {
		if (before.size() != after.size()) return false;
		for (size_t i = 0; i < before.size(); ++i) {
			const auto &a = before[i];
			const auto &b = after[i];
			if (a.tile != b.tile || a.npc != b.npc || a.item != b.item || a.player != b.player
			    || a.architecture != b.architecture || a.picked != b.picked || a.depth != b.depth)
				return false;
		}
		return true;
	};
	bool discardedAny = false, savedAnyTriangleVisits = false;
	const std::array<TownCameraPose, 4> poses { {
		{ 1.0053981634F, 0.5235987756F, 22, { 8, 0, -8 } },
		{ 1.4F, 0.12F, 12, { 3, 0, -3 } },
		{ -0.4F, 0.16F, 6, {} }, { -0.4F, 0.06F, 0, {} }
	} };
	for (size_t mode = 0; mode < poses.size(); ++mode) {
		if (onlyMode >= 0 && static_cast<int>(mode) != onlyMode)
			continue;
		SetTownViewCameraMode(static_cast<TownCameraMode>(mode));
		SetTownViewCameraPoseForDiagnostics(poses[mode]);
		for (const bool aa : { false, true }) {
			const std::string label = "architecture " + std::to_string(mode) + (aa ? " 2x " : " 1x ") + (gpu ? "GPU" : "CPU");
			graphics.townViewAntialiasing.SetValue(aa);
			graphics.townViewFrustumCulling.SetValue(false);
			const uint64_t revision = GetTownSceneRevision();
			ResetTownScene();
			check(GetTownSceneRevision() == revision + 1, label + " isolated scene reset has a new cache identity");
			check(DrawTownView(out, true), label + " uncullled baseline draws");
			const auto baselineColor = viewportPixels(out);
			const auto baselinePixels = pixels();
			const auto baseline = GetTownViewArchitectureCullingState();
			const auto shadow = GetTownShadowStats();
			const auto shadowView = GetTownShadowMapView();
			const std::vector<float> shadowDepth(shadowView.depth.begin(), shadowView.depth.end());
			graphics.townViewFrustumCulling.SetValue(true);
			check(DrawTownView(out, true), label + " cold culling frame draws");
			const auto cold = GetTownViewArchitectureCullingState();
			const auto renderer = GetTownViewRendererState();
			if (gpu && !renderer.usedGpu)
				report << "BACKEND_FAILURE renderer=" << renderer.failure << " gpu=" << GetTownGpuStatus().failure << '\n';
			check(gpu ? renderer.requestedGpu && renderer.usedGpu : !renderer.requestedGpu && !renderer.usedGpu,
			    label + " uses requested backend without silent substitution");
			check(viewportPixels(out) == baselineColor && equalPixels(baselinePixels, pixels()), label + " exact color/depth/semantic picking preserved");
			check(cold.requested && cold.modelsConsidered == GetTownScene().size()
			        && cold.modelsCulled + cold.modelsSubmitted == cold.modelsConsidered
			        && cold.boundsComputed == cold.modelsConsidered && cold.cachedBounds == cold.modelsConsidered,
			    label + " one bounds computation per model after scene reset");
			check(cold.trianglesVisited <= baseline.trianglesVisited, label + " culling only removes architecture traversal");
			const auto afterShadow = GetTownShadowMapView();
			check(GetTownShadowStats().sceneHash == shadow.sceneHash && GetTownShadowStats().inputTriangles == shadow.inputTriangles
			        && afterShadow.depth.size() == shadowDepth.size()
			        && std::equal(shadowDepth.begin(), shadowDepth.end(), afterShadow.depth.begin()),
			    label + " all offscreen shadowcasters and shadow depth remain exact");
			discardedAny |= cold.modelsCulled != 0;
			savedAnyTriangleVisits |= cold.trianglesVisited < baseline.trianglesVisited;
			check(DrawTownView(out, true), label + " warm culling frame draws");
			if (gpu) {
				const auto &gpuWarm = GetTownGpuStatus();
				check(gpuWarm.frameSucceeded && gpuWarm.uploadedTexelBytes == 0 && gpuWarm.uploadedLutBytes == 0,
				    label + " warm frame reuses texels and lighting LUT without upload");
				report << "CACHE mode=" << mode << " gpu=1 cachedBytes=" << gpuWarm.cachedTextureBytes
				       << " uploadedTexelBytes=" << gpuWarm.uploadedTexelBytes << " uploadedLutBytes=" << gpuWarm.uploadedLutBytes << '\n';
			}
			const auto warm = GetTownViewArchitectureCullingState();
			const auto warmRenderer = GetTownViewRendererState();
			check(gpu ? warmRenderer.requestedGpu && warmRenderer.usedGpu : !warmRenderer.requestedGpu && !warmRenderer.usedGpu,
			    label + " warm frame keeps requested backend without silent substitution");
			check(warm.boundsComputed == 0 && warm.cachedBounds == cold.cachedBounds
			        && warm.modelsCulled == cold.modelsCulled && warm.modelsSubmitted == cold.modelsSubmitted,
			    label + " warm cache scans no mesh vertices");
			check(viewportPixels(out) == baselineColor && equalPixels(baselinePixels, pixels()), label + " warm frame exact image/depth/picking preserved");
			check(nativeSnapshot() == native && GetLCGEngineState() == random, label + " map/actors/collision/RNG unchanged");
			// Four warm pairs alternate AB/BA to reduce fixed ordering bias.
			// Timings include only DrawTownView, not validation or SDL presentation.
			std::array<double, 4> unculledMs {}, culledMs {}, unculledReadback {}, culledReadback {};
			const bool restoreCulling = *graphics.townViewFrustumCulling;
			for (size_t pair = 0; pair < unculledMs.size(); ++pair) {
				for (size_t order = 0; order < 2; ++order) {
					const bool culling = ((pair + order) % 2) != 0;
					graphics.townViewFrustumCulling.SetValue(culling);
					const auto begin = std::chrono::steady_clock::now();
					const bool drawn = DrawTownView(out, true);
					const auto end = std::chrono::steady_clock::now();
					check(drawn, label + " measured warm frame draws");
					const auto measuredRenderer = GetTownViewRendererState();
					check(gpu ? measuredRenderer.requestedGpu && measuredRenderer.usedGpu
					          : !measuredRenderer.requestedGpu && !measuredRenderer.usedGpu,
					    label + " measured warm frame uses requested backend");
					const double milliseconds = std::chrono::duration<double, std::milli>(end - begin).count();
					(culling ? culledMs : unculledMs)[pair] = milliseconds;
					(culling ? culledReadback : unculledReadback)[pair] = gpu ? GetTownGpuStatus().readbackMilliseconds : 0;
				}
			}
			const auto median = [](std::array<double, 4> values) {
				std::sort(values.begin(), values.end());
				return (values[1] + values[2]) / 2;
			};
			graphics.townViewFrustumCulling.SetValue(restoreCulling);
			check(DrawTownView(out, true), label + " restores the premeasurement culling frame");
			report << "PERF mode=" << mode << " gpu=" << gpu << " width=" << out.w() << " height=" << gnViewportHeight
			       << " requestedAA=" << aa << " sampling=" << GetTownViewSamplingState().factor << " limited=" << GetTownViewSamplingState().limited
			       << " medianUnculledMs=" << median(unculledMs) << " medianCulledMs=" << median(culledMs)
			       << " medianReadbackUnculledMs=" << median(unculledReadback)
			       << " medianReadbackCulledMs=" << median(culledReadback) << " warmPairs=4 order=AB/BA drawOnly=1\n";
			report << "CULLING mode=" << mode << " sampling=" << (aa ? 2 : 1) << " gpu=" << renderer.usedGpu
			       << " submitted=" << cold.modelsSubmitted << " discarded=" << cold.modelsCulled
			       << " triangleVisits=" << baseline.trianglesVisited << "->" << cold.trianglesVisited
			       << " coldBounds=" << cold.boundsComputed << " warmBounds=" << warm.boundsComputed << '\n';
		}
	}
	check(discardedAny && savedAnyTriangleVisits, "camera fixtures actually discard architecture and skip triangle traversal");
	const auto beforeResetColor = viewportPixels(out);
	const auto beforeResetPixels = pixels();
	ResetTownViewResources();
	SetTownViewFireTimeForDiagnostics(1.25);
	check(DrawTownView(out, true), "architecture resource reset frame draws");
	const auto reset = GetTownViewArchitectureCullingState();
	check(reset.boundsComputed == GetTownScene().size() && reset.cachedBounds == GetTownScene().size(),
	    "resource reset invalidates every cached architecture bound");
	check(viewportPixels(out) == beforeResetColor && equalPixels(beforeResetPixels, pixels()),
	    "resource reset restores exact image/depth/picking");
	check(DrawTownView(out, true) && GetTownViewArchitectureCullingState().boundsComputed == 0,
	    "resource reset cache is warm on the following frame");
	graphics.townViewGpuRendering.SetValue(originalGpu);
	graphics.townViewAntialiasing.SetValue(originalAA);
	graphics.townViewFrustumCulling.SetValue(originalCulling);
	SetTownViewCabinFireEnabledForDiagnostics(originalFire);
	SetTownViewFireTimeForDiagnostics(-1);
	for (size_t i = 0; i < originalPoses.size(); ++i) {
		SetTownViewCameraMode(static_cast<TownCameraMode>(i));
		SetTownViewCameraPoseForDiagnostics(originalPoses[i]);
	}
	SetTownViewCameraMode(originalMode);
	if (IsTownViewActive() != active)
		ToggleTownView();
	check(nativeSnapshot() == native && GetLCGEngineState() == random, "architecture fixture restores native state and RNG");
}

} // namespace devilution
