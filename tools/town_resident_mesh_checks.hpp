#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

#include "engine/random.hpp"
#include "engine/render/town_gpu.hpp"
#include "engine/render/town_gpu_mesh.hpp"
#include "engine/render/town_shadow.hpp"
#include "engine/render/town_view.hpp"
#include "engine/surface.hpp"
#include "options.h"
#include "utils/ui_fwd.h"

namespace devilution {

/** Asset-backed same-GPU projected/resident comparison after the caller's
 * InitializeTownDiagnostic. No profile/save/native asset writes, window or RNG.
 * Central exact mismatch counts are retained. For projected/resident A/B only,
 * the old projected path is independently sampled with <=1/256 raster-pixel
 * offsets per axis on a 5 x 5 grid (excluding the center). A differing pixel is classified only
 * when one guard frame matches color, semantic identity AND depth (0.002 unit)
 * at the same output pixel. Semantic neighborhoods alone grant no tolerance.
 * Timings cover DrawTownView only, including readback/resolve, excluding
 * comparisons, simulation, UI/SDL and sustained presented gameplay FPS.
 * The isolated caller supplies its previous diagnostic flag (normally true). */
template <typename CheckFn, typename SnapshotFn, typename PixelsFn>
void RunTownResidentMeshCaptureChecks(const Surface &out, CheckFn check,
    SnapshotFn nativeSnapshot, PixelsFn viewportPixels, std::ostream &report,
    int onlyMode = -1, bool testAntialiasing = true, bool restoreResidentEnabled = true)
{
	struct Pixel {
		Point tile { -1, -1 };
		int npc = -1, item = -1, player = -1, architecture = -1;
		float depth = std::numeric_limits<float>::infinity();
		bool picked = false;
		bool SameIdentity(const Pixel &other) const
		{
			return tile == other.tile && npc == other.npc && item == other.item && player == other.player
			    && architecture == other.architecture && picked == other.picked;
		}
	};
	struct Frame {
		std::vector<uint8_t> color;
		std::vector<Pixel> pixels;
		TownGpuStatus gpu;
		TownGpuMeshStats meshes;
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
	const bool originalShadows = GetTownViewLightingState().directionalShadowsEnabled;
	std::array<TownCameraPose, 4> originalPoses;
	for (size_t i = 0; i < originalPoses.size(); ++i) {
		SetTownViewCameraMode(static_cast<TownCameraMode>(i));
		const auto state = GetTownViewCameraState();
		originalPoses[i] = { state.yaw, state.pitch, state.distance, { state.offsetX, 0, state.offsetZ } };
	}
	if (!IsTownViewActive())
		ToggleTownView();
	graphics.townViewGpuRendering.SetValue(true);
	graphics.townViewFrustumCulling.SetValue(true);
	SetTownViewFireTimeForDiagnostics(1.25);
	SetTownViewCabinFireEnabledForDiagnostics(true);
	SetTownViewDirectionalShadowsEnabledForDiagnostics(true);
	SetTownViewRasterJitterForDiagnostics(0, 0);
	const auto capture = [&]() {
		Frame frame;
		frame.color = viewportPixels(out);
		frame.gpu = GetTownGpuStatus();
		frame.meshes = GetTownGpuMeshStats();
		frame.pixels.reserve(static_cast<size_t>(out.w()) * gnViewportHeight);
		for (int y = 0; y < gnViewportHeight; ++y) {
			for (int x = 0; x < out.w(); ++x) {
				Pixel pixel;
				pixel.picked = PickTownView({ x, y }, pixel.tile, pixel.npc, pixel.item, pixel.player);
				pixel.architecture = TownViewArchitectureAt({ x, y });
				pixel.depth = TownViewDepthAt({ x, y });
				frame.pixels.push_back(pixel);
			}
		}
		return frame;
	};
	const auto draw = [&](const std::string &label, bool resident) {
		SetTownViewResidentMeshesEnabledForDiagnostics(resident);
		const auto begin = std::chrono::steady_clock::now();
		const bool drawn = DrawTownView(out, true);
		const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
		const auto renderer = GetTownViewRendererState();
		const auto &gpu = GetTownGpuStatus();
		check(drawn && renderer.requestedGpu && renderer.usedGpu && renderer.cpuRasterizedTriangles == 0
		        && renderer.failure.empty() && gpu.frameSucceeded && !gpu.warp,
		    label + " hardware GPU without CPU/WARP substitution");
		if (!renderer.usedGpu)
			report << "RESIDENT_BACKEND_FAILURE label=" << label << " renderer=" << renderer.failure << " gpu=" << gpu.failure << '\n';
		return elapsed;
	};
	const auto compare = [&](const Frame &reference, const Frame &actual, const std::string &label, bool exactDepth, bool classifySubpixelGuard = false) {
		const size_t expected = static_cast<size_t>(out.w()) * gnViewportHeight;
		check(reference.color.size() == expected && actual.color.size() == expected
		        && reference.pixels.size() == expected && actual.pixels.size() == expected,
		    label + " coherent color/semantic-pick/depth sizes");
		if (reference.color.size() != expected || actual.color.size() != expected
		    || reference.pixels.size() != expected || actual.pixels.size() != expected)
			return;
		size_t colors = 0, identities = 0, depths = 0, exactDepthDifferences = 0;
		size_t edgeCandidates = 0, stableDifferences = 0, printed = 0;
		float maximumDepthDelta = 0;
		std::vector<size_t> differing;
		const auto nearSemanticBoundary = [&](const Frame &frame, int x, int y) {
			if (x == 0 || y == 0 || x + 1 == out.w() || y + 1 == gnViewportHeight)
				return true;
			const auto &center = frame.pixels[static_cast<size_t>(y) * out.w() + x];
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					if (!center.SameIdentity(frame.pixels[static_cast<size_t>(y + dy) * out.w() + x + dx]))
						return true;
			return false;
		};
		for (int y = 0; y < gnViewportHeight; ++y) {
			for (int x = 0; x < out.w(); ++x) {
				const size_t i = static_cast<size_t>(y) * out.w() + x;
				const auto &a = reference.pixels[i];
				const auto &b = actual.pixels[i];
				const bool color = reference.color[i] != actual.color[i];
				const bool identity = !a.SameIdentity(b);
				const bool bothFinite = std::isfinite(a.depth) && std::isfinite(b.depth);
				const bool bothInfinite = std::isinf(a.depth) && std::isinf(b.depth) && a.depth == b.depth;
				const float delta = bothFinite ? std::abs(a.depth - b.depth) : 0;
				const bool depth = !(bothFinite || bothInfinite) || (bothFinite && (exactDepth ? delta != 0 : delta > 0.002F));
				maximumDepthDelta = std::max(maximumDepthDelta, delta);
				colors += color;
				identities += identity;
				depths += depth;
				exactDepthDifferences += a.depth != b.depth;
				if (!(color || identity || depth))
					continue;
				differing.push_back(i);
				const bool edge = nearSemanticBoundary(reference, x, y) || nearSemanticBoundary(actual, x, y);
				edgeCandidates += edge;
				stableDifferences += !edge;
				if (printed++ < 16)
					report << "RESIDENT_DIFFERENCE label=" << label << " x=" << x << " y=" << y
					       << " color=" << static_cast<unsigned>(reference.color[i]) << "->" << static_cast<unsigned>(actual.color[i])
					       << " pickDiff=" << identity << " depth=" << a.depth << "->" << b.depth
					       << " edgeCandidate=" << edge << '\n';
			}
		}
		report << "RESIDENT_COMPARE label=" << label << " pixels=" << expected << " colors=" << colors
		       << " identities=" << identities << " depthsBeyondEpsilon=" << depths << " exactDepthDifferences=" << exactDepthDifferences
		       << " maximumFiniteDepthDelta=" << maximumDepthDelta << " semanticEdgeCandidates=" << edgeCandidates
		       << " stableDifferences=" << stableDifferences << " edgeCandidatesForgiven=0"
		       << " strictCentralParity=" << (colors == 0 && identities == 0 && depths == 0) << '\n';
		if (!classifySubpixelGuard) {
			check(stableDifferences == 0, label + " stable image interiors preserve color, depth and semantic picking");
			check(colors == 0 && identities == 0 && depths == 0,
			    label + " complete image parity; edge candidates require independent geometric review");
			return;
		}
		std::vector<bool> classified(differing.size(), false);
		const int sampling = std::max(1, GetTownViewSamplingState().factor);
		const float logicalGuard = 1.0F / (512 * sampling);
		for (const int dy : { -2, -1, 0, 1, 2 }) {
			for (const int dx : { -2, -1, 0, 1, 2 }) {
				if ((dx == 0 && dy == 0) || differing.empty() || std::all_of(classified.begin(), classified.end(), [](bool value) { return value; }))
					continue;
				SetTownViewRasterJitterForDiagnostics(dx * logicalGuard, dy * logicalGuard);
				draw(label + " projected subpixel guard", false);
				const Frame guard = capture();
				check(guard.color.size() == expected && guard.pixels.size() == expected,
				    label + " projected guard preserves dimensions and comparison bounds");
				size_t newlyClassified = 0;
				if (guard.color.size() == expected && guard.pixels.size() == expected) {
					for (size_t mismatch = 0; mismatch < differing.size(); ++mismatch) {
						if (classified[mismatch])
							continue;
						const size_t i = differing[mismatch];
						const auto &a = actual.pixels[i];
						const auto &g = guard.pixels[i];
						const bool depthMatches = (std::isfinite(a.depth) && std::isfinite(g.depth)
						        && std::abs(a.depth - g.depth) <= 0.002F)
						    || (std::isinf(a.depth) && a.depth == g.depth);
						if (actual.color[i] == guard.color[i] && a.SameIdentity(g) && depthMatches) {
							classified[mismatch] = true;
							++newlyClassified;
						}
					}
				}
				report << "RESIDENT_GUARD_SAMPLE label=" << label << " rasterDx=" << static_cast<float>(dx) / 512
				       << " rasterDy=" << static_cast<float>(dy) / 512 << " logicalDx=" << dx * logicalGuard
				       << " logicalDy=" << dy * logicalGuard << " newlyClassifiedCombinedSamples=" << newlyClassified << '\n';
			}
		}
		SetTownViewRasterJitterForDiagnostics(0, 0);
		if (!differing.empty())
			draw(label + " restore zero-jitter resident", true);
		size_t unmatched = 0;
		for (size_t mismatch = 0; mismatch < differing.size(); ++mismatch) {
			if (classified[mismatch])
				continue;
			const size_t i = differing[mismatch];
			if (unmatched++ < 16) {
				const auto &a = reference.pixels[i];
				const auto &b = actual.pixels[i];
				report << "RESIDENT_GUARD_UNMATCHED label=" << label << " x=" << i % out.w() << " y=" << i / out.w()
				       << " color=" << static_cast<unsigned>(reference.color[i]) << "->" << static_cast<unsigned>(actual.color[i])
				       << " tile=" << a.tile.x << ',' << a.tile.y << "->" << b.tile.x << ',' << b.tile.y
				       << " npc=" << a.npc << "->" << b.npc << " item=" << a.item << "->" << b.item
				       << " player=" << a.player << "->" << b.player << " architecture=" << a.architecture << "->" << b.architecture
				       << " picked=" << a.picked << "->" << b.picked << " depth=" << a.depth << "->" << b.depth << '\n';
			}
		}
		report << "RESIDENT_GUARD_RESULT label=" << label << " originalDifferingPixels=" << differing.size()
		       << " combinedSamplesClassified=" << differing.size() - unmatched << " unmatched=" << unmatched
		       << " maximumRasterOffsetPerAxis=0.00390625 semanticNeighborhoodForgiveness=0\n";
		check(unmatched == 0, label + " every central difference has same-pixel color/identity/depth evidence from projected subpixel guards");
	};
	const std::array<TownCameraPose, 4> poses { {
		{ 1.0053981634F, 0.5235987756F, 22, { 8, 0, -8 } },
		{ 1.4F, 0.12F, 12, { 3, 0, -3 } },
		{ -0.4F, 0.16F, 6, {} }, { -0.4F, 0.06F, 0, {} }
	} };
	bool exercisedResident = false;
	for (size_t mode = 0; mode < poses.size(); ++mode) {
		if (onlyMode >= 0 && static_cast<int>(mode) != onlyMode)
			continue;
		SetTownViewCameraMode(static_cast<TownCameraMode>(mode));
		SetTownViewCameraPoseForDiagnostics(poses[mode]);
		for (const bool aa : { false, true }) {
			if (aa && !testAntialiasing)
				continue;
			graphics.townViewAntialiasing.SetValue(aa);
			const std::string label = "resident mode=" + std::to_string(mode) + " aa=" + std::to_string(aa);
			ResetTownGpuResources();
			draw(label + " projected", false);
			const Frame baseline = capture();
			const auto shadow = GetTownShadowStats();
			const auto shadowView = GetTownShadowMapView();
			const std::vector<float> shadowDepth(shadowView.depth.begin(), shadowView.depth.end());
			draw(label + " cold", true);
			const Frame cold = capture();
			exercisedResident |= cold.meshes.instances > 0;
			if (mode == 0) {
				check(cold.meshes.instances == 0 && cold.meshes.uploads == 0,
				    label + " orthographic camera retains the projected path pending clipping parity");
			} else {
				check(cold.meshes.instances > 0 && cold.meshes.uploads > 0 && cold.meshes.cachedMeshes > 0
				        && cold.meshes.uploadedVertexBytes > 0 && cold.meshes.uploadedIndexBytes > 0,
				    label + " cold path actually uploads and draws resident geometry");
			}
			compare(baseline, cold, label + " projected/cold", false, true);
			draw(label + " warm", true);
			const Frame warm = capture();
			check(warm.meshes.uploads == 0 && warm.meshes.uploadedVertexBytes == 0 && warm.meshes.uploadedIndexBytes == 0
			        && (mode == 0 || warm.meshes.instances > 0) && warm.meshes.instances == warm.meshes.reusedInstances && warm.meshes.evictions == 0,
			    label + " warm frame has zero static vertex/index upload and no mesh eviction");
			check(warm.gpu.uploadedTexelBytes == 0 && warm.gpu.uploadedLutBytes == 0,
			    label + " warm frame reuses texture and light LUT payloads");
			compare(cold, warm, label + " cold/warm", true);
			const auto afterShadow = GetTownShadowMapView();
			check(GetTownShadowStats().sceneHash == shadow.sceneHash && GetTownShadowStats().inputTriangles == shadow.inputTriangles
			        && afterShadow.depth.size() == shadowDepth.size()
			        && std::equal(shadowDepth.begin(), shadowDepth.end(), afterShadow.depth.begin()),
			    label + " static shadowcaster geometry and full shadow map are unchanged");
			report << "RESIDENT_CACHE mode=" << mode << " aa=" << aa << " coldUploads=" << cold.meshes.uploads
			       << " coldVertexBytes=" << cold.meshes.uploadedVertexBytes << " coldIndexBytes=" << cold.meshes.uploadedIndexBytes
			       << " residentBytes=" << warm.meshes.residentBytes << " instances=" << warm.meshes.instances
			       << " projectedUploadBytes=" << baseline.meshes.projectedUploadBytes << "->" << warm.meshes.projectedUploadBytes
			       << " drawCalls=" << baseline.gpu.drawCalls << "->" << warm.gpu.drawCalls
			       << " warmGeometryUpload=0 adapter=" << warm.gpu.adapter << '\n';
			std::array<double, 4> projectedMs {}, residentMs {}, projectedReadback {}, residentReadback {};
			for (size_t pair = 0; pair < projectedMs.size(); ++pair) {
				for (size_t order = 0; order < 2; ++order) {
					const bool resident = ((pair + order) % 2) != 0;
					const double elapsed = draw(label + " timed", resident);
					(resident ? residentMs : projectedMs)[pair] = elapsed;
					(resident ? residentReadback : projectedReadback)[pair] = GetTownGpuStatus().readbackMilliseconds;
					check(!resident || GetTownGpuMeshStats().uploads == 0, label + " measured resident frame stays warm");
				}
			}
			const auto median = [](std::array<double, 4> samples) {
				std::sort(samples.begin(), samples.end());
				return (samples[1] + samples[2]) / 2;
			};
			report << "RESIDENT_PERF mode=" << mode << " width=" << out.w() << " height=" << gnViewportHeight
			       << " requestedAA=" << aa << " sampling=" << GetTownViewSamplingState().factor
			       << " projectedMedianMs=" << median(projectedMs) << " residentMedianMs=" << median(residentMs)
			       << " projectedReadbackMedianMs=" << median(projectedReadback) << " residentReadbackMedianMs=" << median(residentReadback)
			       << " warmPairs=4 order=AB/BA drawOnly=1 sustainedGameplayFps=0\n";
			draw(label + " restored resident", true);
			compare(warm, capture(), label + " after AB/BA", true);
			check(nativeSnapshot() == native && GetLCGEngineState() == random, label + " native state/collision/actors/RNG preserved");
		}
	}
	check(exercisedResident, "resident fixture exercised real resident meshes");
	graphics.townViewGpuRendering.SetValue(originalGpu);
	graphics.townViewAntialiasing.SetValue(originalAA);
	graphics.townViewFrustumCulling.SetValue(originalCulling);
	SetTownViewResidentMeshesEnabledForDiagnostics(restoreResidentEnabled);
	SetTownViewRasterJitterForDiagnostics(0, 0);
	SetTownViewCabinFireEnabledForDiagnostics(originalFire);
	SetTownViewDirectionalShadowsEnabledForDiagnostics(originalShadows);
	SetTownViewFireTimeForDiagnostics(-1);
	for (size_t i = 0; i < originalPoses.size(); ++i) {
		SetTownViewCameraMode(static_cast<TownCameraMode>(i));
		SetTownViewCameraPoseForDiagnostics(originalPoses[i]);
	}
	SetTownViewCameraMode(originalMode);
	if (IsTownViewActive() != active)
		ToggleTownView();
	check(nativeSnapshot() == native && GetLCGEngineState() == random, "resident fixture restores native state and RNG");
}

} // namespace devilution
