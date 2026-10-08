#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <ostream>
#include <string>

#include "engine/random.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/town_gpu.hpp"
#include "engine/render/town_scene.hpp"
#include "engine/render/town_view.hpp"
#include "engine/surface.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/tile_properties.hpp"
#include "options.h"
#include "player.h"
#include "utils/ui_fwd.h"

namespace devilution {

/** Native capture supplement, called after InitializeTownDiagnostic(). Pass
 * Check, NativeSceneState, ViewportPixels and SavePng from town_view_smoke.
 * The output directory must already exist in the private diagnostics tree.
 * No archive/model is copied, no profile/save is written and no game starts.
 *
 * RunTownCameraCaptureChecks(out, Check, NativeSceneState, ViewportPixels,
 *     SavePng, output / "camera-captures", std::cout, true);
 *
 * One requested backend per call: true requires hardware GPU (no silent CPU
 * substitution); false explicitly renders CPU. Captures show four modes at
 * 1x/2x, eight ocular map-edge directions, a measured actual triangle crossing
 * the near plane when available, and Home versus the original backend.
 * Opacity/silhouette quality is a visual review item, never inferred from
 * global image agreement or from a zero self-selection count.
 */
template <typename CheckFn, typename SnapshotFn, typename PixelsFn, typename CaptureFn>
void RunTownCameraCaptureChecks(const Surface &out, CheckFn check, SnapshotFn nativeSnapshot,
    PixelsFn viewportPixels, CaptureFn savePng, const std::filesystem::path &directory,
    std::ostream &report, bool gpu = true)
{
	check(MyPlayer != nullptr && leveltype == DTYPE_TOWN && out.w() == gnScreenWidth
	        && out.h() >= gnViewportHeight && gnViewportHeight > 0,
	    "camera captures use initialized town assets and the complete configured logical viewport");
	if (MyPlayer == nullptr || leveltype != DTYPE_TOWN || out.w() != gnScreenWidth
	    || out.h() < gnViewportHeight || gnViewportHeight == 0)
		return;
	check(std::filesystem::is_directory(directory), "private camera capture directory already exists");
	if (!std::filesystem::is_directory(directory))
		return;
	const auto originalNative = nativeSnapshot();
	const auto originalRandom = GetLCGEngineState();
	{
		struct Restore {
			GraphicsOptions &graphics;
			ActorPosition position;
			Point view;
			bool active;
			TownCameraMode mode;
			int cameraMode, fov, sensitivity;
			bool gpu, smoothing, horizon;
			std::array<TownCameraPose, 4> poses;
			int8_t occupancy[MAXDUNX][MAXDUNY];
			DungeonFlag flags[MAXDUNX][MAXDUNY];
			~Restore()
			{
				MyPlayer->position = position;
				ViewPosition = view;
				std::memcpy(dPlayer, occupancy, sizeof(occupancy));
				std::memcpy(dFlags, flags, sizeof(flags));
				graphics.townViewCameraMode.SetValue(cameraMode);
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
		};
		GraphicsOptions &graphics = GetOptions().Graphics;
		Restore restore { graphics, MyPlayer->position, ViewPosition, IsTownViewActive(), GetTownViewCameraMode(),
			*graphics.townViewCameraMode, *graphics.townViewCameraFov, *graphics.townViewCameraSensitivity,
			*graphics.townViewGpuRendering, *graphics.townViewAntialiasing, *graphics.townViewHorizon, {}, {}, {} };
		std::memcpy(restore.occupancy, dPlayer, sizeof(dPlayer));
		std::memcpy(restore.flags, dFlags, sizeof(dFlags));
		for (size_t i = 0; i < restore.poses.size(); ++i) {
			SetTownViewCameraMode(static_cast<TownCameraMode>(i));
			const auto state = GetTownViewCameraState();
			restore.poses[i] = { state.yaw, state.pitch, state.distance, { state.offsetX, 0, state.offsetZ } };
		}
		if (!IsTownViewActive())
			ToggleTownView();
		graphics.townViewCameraFov.SetValue(60);
		graphics.townViewCameraSensitivity.SetValue(100);
		graphics.townViewGpuRendering.SetValue(gpu);
		graphics.townViewHorizon.SetValue(true);
		ApplyTownViewCameraPreferences();

		// Relocation is fixture setup on existing walkable native tiles. It is
		// explicitly reversible and does not demonstrate travel or an expansion.
		const auto relocate = [&](Point desired) {
			for (int radius = 0; radius <= 32; ++radius) {
				for (int y = -radius; y <= radius; ++y) {
					for (int x = -radius; x <= radius; ++x) {
						if (std::abs(x) + std::abs(y) != radius)
							continue;
						const Point tile = desired + Displacement { x, y };
						if (!InDungeonBounds(tile) || !IsTileWalkable(tile) || dMonster[tile.x][tile.y] != 0
						    || (dPlayer[tile.x][tile.y] != 0 && tile != MyPlayer->position.tile))
							continue;
						dPlayer[MyPlayer->position.tile.x][MyPlayer->position.tile.y] = 0;
						MyPlayer->position.tile = MyPlayer->position.future = MyPlayer->position.last
						    = MyPlayer->position.old = MyPlayer->position.temp = tile;
						dPlayer[tile.x][tile.y] = static_cast<int8_t>(MyPlayerId + 1);
						dFlags[tile.x][tile.y] |= DungeonFlag::Lit | DungeonFlag::Visible;
						ViewPosition = tile;
						return true;
					}
				}
			}
			return false;
		};
		const auto capture = [&](const std::string &name, TownCameraMode mode, TownCameraPose pose, bool smooth) {
			SetTownViewCameraMode(mode);
			SetTownViewCameraPoseForDiagnostics(pose);
			graphics.townViewAntialiasing.SetValue(smooth);
			const auto native = nativeSnapshot();
			const auto random = GetLCGEngineState();
			check(DrawTownView(out, true), name + " draws forced native geometry");
			const auto renderer = GetTownViewRendererState();
			check(gpu ? renderer.requestedGpu && renderer.usedGpu && renderer.cpuRasterizedTriangles == 0
			        && renderer.gpuSubmittedTriangles > 0 && GetTownGpuStatus().frameSucceeded
			             : !renderer.requestedGpu && !renderer.usedGpu && renderer.cpuRasterizedTriangles > 0,
			    name + " uses the explicitly requested backend");
			const auto sampling = GetTownViewSamplingState();
			check(sampling.factor == (smooth ? 2 : 1) && !sampling.limited, name + " uses the requested world sampling factor");
			size_t sky = 0, nativePixels = 0, unowned = 0, self = 0;
			float minimumDepth = std::numeric_limits<float>::infinity(), maximumDepth = 0;
			bool valid = true;
			for (int y = 0; y < gnViewportHeight; ++y) {
				for (int x = 0; x < out.w(); ++x) {
					Point tile { -1, -1 };
					int npc = -1, item = -1, player = -1;
					const bool picked = PickTownView({ x, y }, tile, npc, item, player);
					const float depth = TownViewDepthAt({ x, y });
					if (std::isfinite(depth)) {
						minimumDepth = std::min(minimumDepth, depth);
						maximumDepth = std::max(maximumDepth, depth);
						valid &= depth >= (mode == TownCameraMode::Isometric ? 0.3999F : 0.0799F)
						    && depth <= (mode == TownCameraMode::Isometric ? 4096.001F : 320.001F);
						unowned += !picked ? 1 : 0;
					} else {
						++sky;
						valid &= !picked && TownViewArchitectureAt({ x, y }) == -1;
					}
					if (picked) {
						++nativePixels;
						valid &= InDungeonBounds(tile) && std::isfinite(depth) && npc >= -1 && item >= -1
						    && player >= -1 && player < static_cast<int>(Players.size());
						self += player == MyPlayerId ? 1 : 0;
					}
				}
			}
			check(valid, name + " resolves finite clipped depths and native IDs while leaving sky unselectable");
			if (mode == TownCameraMode::FirstPerson)
				check(self == 0, name + " has no local-hero selection pixels");
			check(nativeSnapshot() == native && GetLCGEngineState() == random, name + " rendering preserves the fixture's native state and RNG");
			savePng(out.subregionY(0, gnViewportHeight), directory / (name + ".png"));
			if (smooth) {
				const Surface *high = GetTownViewHighResolutionFrame();
				check(high != nullptr, name + " retains the high-resolution world frame");
				if (high != nullptr) {
					// SamplingSurface stores indices; its SDL palette is not the
					// presentation palette. Materialize the diagnostic with the
					// current logical palette before asking the PNG writer to encode.
					OwnedSurface encoded(high->w(), high->h());
					SDL_SetPaletteColors(encoded.surface->format->palette, logical_palette.data(), 0, 256);
					for (int y = 0; y < high->h(); ++y)
						std::memcpy(encoded.at(0, y), high->at(0, y), high->w());
					savePng(encoded, directory / (name + "-world-2x.png"));
				}
			}
			report << "CAPTURE " << name << " tile=" << static_cast<int>(MyPlayer->position.tile.x) << ',' << static_cast<int>(MyPlayer->position.tile.y)
			       << " mode=" << static_cast<int>(mode) << " yaw=" << pose.yaw << " pitch=" << pose.pitch
			       << " distance=" << pose.distance << " nativePixels=" << nativePixels << " sky=" << sky
			       << " unownedGeometry=" << unowned << " selfIDs=" << self << " depth=" << minimumDepth << ".." << maximumDepth
			       << " gpu=" << renderer.usedGpu << " cpuTriangles=" << renderer.cpuRasterizedTriangles
			       << " gpuTriangles=" << renderer.gpuSubmittedTriangles << " horizonBytes=" << renderer.horizonBytes << '\n';
		};

		const std::array<TownCameraPose, 4> modePoses { {
			{ 1.0053981634F, 0.5235987756F, 22, {} }, { 1.4F, 0.12F, 12, {} },
			{ -0.4F, 0.16F, 6, {} }, { -0.4F, 0.06F, 0, {} },
		} };
		for (size_t mode = 0; mode < modePoses.size(); ++mode)
			for (const bool smooth : { false, true })
				capture("mode-" + std::to_string(mode) + (smooth ? "-2x" : "-1x"), static_cast<TownCameraMode>(mode), modePoses[mode], smooth);
		// Paired images show the atmospheric/decorative contribution. The
		// runtime checks separately compare raw depth/IDs where visibility is
		// unchanged; image differences alone do not establish that contract.
		graphics.townViewHorizon.SetValue(false);
		capture("ocular-horizon-off-1x", TownCameraMode::FirstPerson, modePoses[3], false);
		graphics.townViewHorizon.SetValue(true);
		capture("ocular-horizon-on-1x", TownCameraMode::FirstPerson, modePoses[3], false);

		// Eight tile-axis edge/corner poses. Use the actual 112x112 native array,
		// rather than treating dmin/dmax (debug camera bounds) as movement limits.
		struct Edge { const char *name; Point desired; float yaw; };
		constexpr float Pi = 3.14159265358979323846F;
		const std::array<Edge, 8> edges { {
			{ "min-x", { 2, 56 }, 0 }, { "max-x", { 109, 56 }, Pi },
			{ "min-z", { 56, 2 }, Pi / 2 }, { "max-z", { 56, 109 }, -Pi / 2 },
			{ "min-x-min-z", { 2, 2 }, Pi / 4 }, { "max-x-min-z", { 109, 2 }, 3 * Pi / 4 },
			{ "max-x-max-z", { 109, 109 }, -3 * Pi / 4 }, { "min-x-max-z", { 2, 109 }, -Pi / 4 },
		} };
		for (const Edge &edge : edges) {
			if (!relocate(edge.desired)) {
				report << "SKIP ocular edge " << edge.name << ": no native walkable fixture tile within thirty-two cells; no terrain was created\n";
				continue;
			}
			capture(std::string("ocular-edge-") + edge.name, TownCameraMode::FirstPerson, { edge.yaw, 0.025F, 0, {} }, false);
		}
		// Restore the stationary hero before inspecting a measured wall plane.
		MyPlayer->position = restore.position;
		ViewPosition = restore.view;
		std::memcpy(dPlayer, restore.occupancy, sizeof(dPlayer));
		std::memcpy(dFlags, restore.flags, sizeof(dFlags));
		bool nearFixture = false;
		for (const auto &model : GetTownScene()) {
			if (nearFixture)
				break;
			for (const auto &triangle : model.triangles) {
				const auto &v = triangle.vertices;
				const TownCameraPoint center { (v[0].x + v[1].x + v[2].x) / 3,
					(v[0].height + v[1].height + v[2].height) / 3, (v[0].z + v[1].z + v[2].z) / 3 };
				if (center.height < 0.4F || center.height > 5)
					continue;
				for (int heading = 0; heading < 8 && !nearFixture; ++heading) {
					const float yaw = heading * Pi / 4, pitch = 0.3F;
					const float distance = center.height / std::sin(pitch);
					TownCameraPose pose { yaw, pitch, distance,
						{ center.x - MyPlayer->position.tile.x - std::cos(yaw) * std::cos(pitch) * distance, 0,
							center.z - MyPlayer->position.tile.y - std::sin(yaw) * std::cos(pitch) * distance } };
					TownCameraRig rig;
					rig.SetMode(TownCameraMode::FreeOrbit);
					rig.SetPose(pose);
					const auto frame = BuildTownCameraFrame(rig,
						{ static_cast<float>(MyPlayer->position.tile.x), 0, static_cast<float>(MyPlayer->position.tile.y) },
						out.w(), gnViewportHeight, out.w() / 2.0F, gnViewportHeight / 2.0F);
					std::array<TownCameraVertex, 3> world;
					float minDepth = std::numeric_limits<float>::infinity(), maxDepth = -minDepth;
					for (size_t i = 0; i < world.size(); ++i) {
						world[i] = { { v[i].x, v[i].height, v[i].z }, v[i].u, v[i].v };
						const float depth = TownCameraToView(frame, world[i].world).z;
						minDepth = std::min(minDepth, depth);
						maxDepth = std::max(maxDepth, depth);
					}
					if (minDepth >= frame.nearClip || maxDepth <= frame.nearClip || ClipTownCameraTriangle(frame, world).count == 0)
						continue;
					nearFixture = true;
					report << "MEASURED near-crossing modelKind=" << static_cast<int>(model.kind)
					       << " vertexDepth=" << minDepth << ".." << maxDepth << " near=" << frame.nearClip
					       << "; clipping is measured on an actual source triangle, its final visibility, opacity and silhouette require image review\n";
					capture("native-near-crossing-1x", TownCameraMode::FreeOrbit, pose, false);
					capture("native-near-crossing-2x", TownCameraMode::FreeOrbit, pose, true);
				}
				if (nearFixture)
					break;
			}
		}
		if (!nearFixture)
			report << "SKIP measured native near-crossing: no suitable source triangle found; analytical clipping tests do not replace this missing capture\n";

		// Exact Home identity is checked against the real original backend. It
		// remains separate from the forced-geometry screenshots above.
		ResetTownViewCamera();
		graphics.townViewAntialiasing.SetValue(true);
		check(DrawNativeTownViewReference(out, ViewPosition), "capture reference draws the original native backend");
		const auto nativePixels = viewportPixels(out);
		savePng(out.subregionY(0, gnViewportHeight), directory / "home-native-reference.png");
		check(DrawTownView(out) && IsTownViewNativePose() && viewportPixels(out) == nativePixels
		        && !GetTownViewRendererState().usedGpu && GetTownViewHighResolutionFrame() == nullptr,
		    "Home matches the original backend exactly with horizon and smoothing requested");
		savePng(out.subregionY(0, gnViewportHeight), directory / "home-runtime.png");
	}
	check(nativeSnapshot() == originalNative && GetLCGEngineState() == originalRandom,
	    "capture fixtures restore occupancy, flags, actor position, camera target and the native fields covered by the supplied snapshot, with unchanged RNG");
	report << "LIMIT: captures contain private native art; inspect low-angle silhouettes, cutout foliage, floors, roofs and near-plane openings visually."
	          " These fixtures do not certify normal travel to an edge, combat, saves, all heroes or runtime FPS.\n";
}

} // namespace devilution
