#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

#include "first_person_profile.hpp"

// Included only by the private copy of town_view_smoke.cpp, after its native
// initialization and invariant helpers. No settings are saved by this fixture.
namespace {
std::string FirstPersonJsonString(const std::string &value)
{
	std::string escaped;
	for (const char ch : value) {
		if (ch == '\\' || ch == '"') escaped += '\\';
		if (ch == '\n') escaped += "\\n";
		else if (ch == '\r') escaped += "\\r";
		else escaped += ch;
	}
	return '"' + escaped + '"';
}
uint64_t FirstPersonFrameHash(const devilution::Surface &out)
{
	uint64_t hash = 1469598103934665603ULL;
	const auto bytes = [&](const void *data, size_t size) {
		const auto *p = static_cast<const uint8_t *>(data);
		for (size_t i = 0; i < size; ++i) { hash ^= p[i]; hash *= 1099511628211ULL; }
	};
	for (int y = 0; y < gnViewportHeight; ++y) {
		bytes(out.at(0, y), out.w());
		for (int x = 0; x < out.w(); ++x) {
			const float depth = TownViewDepthAt({ x, y });
			bytes(&depth, sizeof(depth));
			Point tile { -1, -1 };
			int npc, item, player;
			const bool picked = PickTownView({ x, y }, tile, npc, item, player);
			const int architecture = TownViewArchitectureAt({ x, y });
			bytes(&tile.x, sizeof(tile.x)); bytes(&tile.y, sizeof(tile.y));
			bytes(&npc, sizeof(npc)); bytes(&item, sizeof(item)); bytes(&player, sizeof(player));
			bytes(&picked, sizeof(picked)); bytes(&architecture, sizeof(architecture));
		}
	}
	return hash;
}

void RunFirstPersonStageProfile(const std::filesystem::path &output)
{
	using namespace devilution;
	namespace fp = devilution::first_person_profile;
	InitializeTownDiagnostic();
	const std::string native = NativeSceneState();
	const auto rng = GetLCGEngineState();
	auto &graphics = GetOptions().Graphics;
	graphics.townViewGpuRendering.SetValue(true);
	graphics.townViewFrustumCulling.SetValue(true);
	graphics.zoom.SetValue(true);
	if (!IsTownViewActive()) ToggleTownView();
	SetTownViewFireTimeForDiagnostics(1.25);
	SetTownViewCameraMode(TownCameraMode::FirstPerson);
	SetTownViewCameraPoseForDiagnostics({ -0.4F, 0.06F, 0, {} });
	struct Case { const char *name; int width, height, fov; bool aa, horizon; float yaw; };
	// A baseline is repeated between axes to expose drift. Test preferences only
	// affect this isolated process and are never proposed as a quality reduction.
	const std::array cases {
		Case { "baseline_open", 1920, 1080, 80, true, true, -0.4F },
		Case { "res_960", 960, 540, 80, false, true, -0.4F },
		Case { "res_1280", 1280, 720, 80, false, true, -0.4F },
		Case { "baseline_after_res", 1920, 1080, 80, true, true, -0.4F },
		Case { "fov_60", 1920, 1080, 60, true, true, -0.4F },
		Case { "fov_100", 1920, 1080, 100, true, true, -0.4F },
		Case { "baseline_after_fov", 1920, 1080, 80, true, true, -0.4F },
		Case { "horizon_off", 1920, 1080, 80, true, false, -0.4F },
		Case { "aa_960_2x", 960, 540, 80, true, true, -0.4F },
		Case { "aa_1280_2x", 1280, 720, 80, true, true, -0.4F },
		Case { "baseline_after_aa", 1920, 1080, 80, true, true, -0.4F },
		Case { "yaw_90", 1920, 1080, 80, true, true, 1.170796327F },
		Case { "yaw_180", 1920, 1080, 80, true, true, 2.741592654F },
		Case { "yaw_270", 1920, 1080, 80, true, true, 4.31238898F },
		Case { "baseline_close", 1920, 1080, 80, true, true, -0.4F },
	};
	std::ofstream report(output / "first-person-stages.jsonl");
	report << std::setprecision(12);
	const char *quick = std::getenv("FIRST_PERSON_PROFILE_QUICK");
	for (size_t index = 0; index < cases.size(); ++index) {
		if (quick && index != 0 && index != cases.size() - 1) continue;
		const auto &test = cases[index];
		gnScreenWidth = test.width; gnScreenHeight = test.height;
		CalculatePanelAreas(); CalcViewportGeometry();
		OwnedSurface out(test.width, test.height);
		SDL_SetPaletteColors(out.surface->format->palette, logical_palette.data(), 0, 256);
		graphics.townViewCameraFov.SetValue(test.fov);
		ApplyTownViewCameraPreferences();
		graphics.townViewAntialiasing.SetValue(test.aa);
		graphics.townViewHorizon.SetValue(test.horizon);
		SetTownViewCameraPoseForDiagnostics({ test.yaw, 0.06F, 0, {} });
		uint64_t referenceHash = 0;
		bool allHardware = true;
		for (int sample = -3; sample < 9; ++sample) {
			fp::Reset();
			const auto start = std::chrono::steady_clock::now();
			const bool drawn = DrawTownView(out, true);
			const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			auto stages = fp::Current;
			const auto renderer = GetTownViewRendererState();
			const auto gpu = GetTownGpuStatus();
			const auto sampling = GetTownViewSamplingState();
			const auto camera = GetTownViewCameraState();
			size_t cachedLuts = 0, cacheEvictions = 0;
#ifdef FIRST_PERSON_SPLIT_CACHE
			stages.cacheBytesPeak = std::max(stages.cacheBytesPeak, gpu.cachedTextureBytes);
			cachedLuts = gpu.cachedLightLuts;
			cacheEvictions = gpu.textureEvictions;
#endif
			if (!drawn) throw std::runtime_error("World did not draw: " + renderer.failure);
			if (std::abs(camera.verticalFovDegrees - test.fov) > 0.001F || camera.mode != TownCameraMode::FirstPerson)
				throw std::runtime_error("Effective camera mode/FOV differs from requested fixture");
			allHardware &= renderer.usedGpu && !gpu.warp && gpu.frameSucceeded && !renderer.cpuRasterizedTriangles;
			// Pixel/depth/semantic-picking hash validation is outside timed regions,
			// once before and once after each warm block to avoid cache distortion.
			if (sample == -1) referenceHash = FirstPersonFrameHash(out);
			if (sample == 8 && FirstPersonFrameHash(out) != referenceHash)
				throw std::runtime_error("warm block changed color/depth/semantic picking");
			report << "{\"case\":\"" << test.name << "\",\"sample\":" << sample
			    << ",\"coldOrWarmup\":" << (sample < 0 ? "true" : "false")
			    << ",\"width\":" << out.w() << ",\"height\":" << gnViewportHeight
			    << ",\"windowCanvasHeight\":" << out.h() << ",\"sampleWidth\":" << sampling.width
			    << ",\"sampleHeight\":" << sampling.height << ",\"aaRequested\":" << (sampling.requested ? "true" : "false")
			    << ",\"sampleFactor\":" << sampling.factor << ",\"sampleLimited\":" << (sampling.limited ? "true" : "false")
			    << ",\"fov\":" << test.fov << ",\"effectiveFov\":" << camera.verticalFovDegrees << ",\"yaw\":" << camera.yaw
			    << ",\"horizon\":" << (test.horizon ? "true" : "false")
			    << ",\"requestedGpu\":true,\"usedGpu\":" << (renderer.usedGpu ? "true" : "false")
			    << ",\"warp\":" << (gpu.warp ? "true" : "false") << ",\"adapter\":" << FirstPersonJsonString(gpu.adapter)
			    << ",\"featureLevel\":" << gpu.featureLevel
			    << ",\"failure\":" << FirstPersonJsonString(renderer.failure)
			    << ",\"frameHash\":" << referenceHash << ",\"triangles\":" << renderer.gpuSubmittedTriangles
			    << ",\"cpuTriangles\":" << renderer.cpuRasterizedTriangles << ",\"drawCalls\":" << gpu.drawCalls
			    << ",\"cachedTextures\":" << gpu.cachedTextures
			    << ",\"cachedLuts\":" << cachedLuts << ",\"cacheEvictions\":" << cacheEvictions
			    << ",\"textureUploads\":" << stages.textureUploads << ",\"uploadedCodeBytes\":" << stages.uploadedCodeBytes
			    << ",\"uploadedLutBytes\":" << stages.uploadedLutBytes << ",\"cacheBytesPeak\":" << stages.cacheBytesPeak
			    << ",\"cachedTexturesPeak\":" << stages.cachedTexturesPeak << ",\"failedUploadBytes\":" << stages.failedUploadBytes
			    << ",\"failedLutBytes\":" << stages.failedLutBytes
			    << ",\"gpuTimestampValid\":" << (stages.gpuTimestampValid ? "true" : "false")
			    << ",\"gpuDrawTimestampMs\":" << stages.gpuDrawTimestampMs << ",\"gpuCopyTimestampMs\":" << stages.gpuCopyTimestampMs
			    << ",\"fullWorldMs\":" << elapsed << ",\"backendWallMs\":" << gpu.frameMilliseconds
			    << ",\"legacyReadbackMs\":" << gpu.readbackMilliseconds << ",\"stagesMs\":{";
			for (size_t stage = 0; stage < fp::Count; ++stage)
				report << (stage ? "," : "") << '"' << fp::Names[stage] << "\":" << stages.milliseconds[stage];
			report << "},\"scope\":\"offscreen DrawTownView; excludes game loop/UI/SDL; wall stages may nest; not sustained FPS\"}\n";
			report.flush();
		}
		Check(NativeSceneState() == native && GetLCGEngineState() == rng, std::string(test.name) + " preserves native state/RNG and warm image/depth/picks");
		std::cout << "PROFILE complete " << test.name << '\n';
		if (!allHardware) {
			std::cout << "PROFILE STOP: requested GPU fell back; later GPU curves would be mislabeled.\n";
			break;
		}
	}
	FreeTownerGFX();
}
} // namespace
