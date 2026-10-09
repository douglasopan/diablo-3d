#pragma once
// PRIVATE PREPARATION ONLY: include at the bottom of town_view_smoke.cpp,
// after its native helpers/GpuPickSnapshot. Existing main owns SDL/archives.
// Finite DrawTownView/offscreen; no dialogue, full game, FPS or art acceptance.
#include <iterator>
#include <span>
#include <string_view>
#include "diablo.h"
#include "engine/render/ogden_idle_native_clock.hpp"
#include "engine/render/ogden_idle_pilot.hpp"
#include "engine/render/town_editor_map.hpp"
#include "mods/mod_identity.h"

namespace ogden_idle_render_gate {
using namespace devilution;
inline constexpr std::string_view PackagePath = "d3d-actors/ogden-idle-r1.actor";
inline constexpr std::string_view SelectionPath = "d3d-actors/ogden-idle-r1.selection";
inline constexpr std::string_view PackageSha = "9b7793d3e2187228a206ff139ba9e44e575de2f7ff40a783f4eee3d5b970759d";
inline constexpr std::string_view MapSha = "6618ea370be49482b6c88524ca504ea993dd0a85b0cf79002165528afa9ebc2c";
struct ExpectedArchitecture { const char *id, *path, *sha, *revision; };
inline constexpr std::array<ExpectedArchitecture, 13> Architecture {{
	{ "house-gillian", "d3d-models/editor/house-gillian-42df3d0e2eece276.d3d", "42df3d0e2eece276c8825ef887ada7691c8f77130102123f230245dcb4defe05", "common-house-short-meshy-r1-static-albedo-r1" },
	{ "house-pepin", "d3d-models/editor/house-pepin-4e19f753639c84fc.d3d", "4e19f753639c84fc2f676943c9af90362144e2132db60162c4a10ccf8ace098d", "pepin-meshy-r1-static-albedo-r1" },
	{ "house-adria", "d3d-models/editor/house-adria-ba4b553f3627bc46.d3d", "ba4b553f3627bc46f4b85b06eb072963f95f79e513f635807176765bf954b845", "adria-meshy-r1-static-albedo-r1" },
	{ "house-north", "d3d-models/editor/house-north-d8e8c2ddd000089e.d3d", "d8e8c2ddd000089e4c308cb9a52e8346c8b67968d9dc86e109b0aac704e0134e", "common-house-long-meshy-r1-static-albedo-r1" },
	{ "house-southeast", "d3d-models/editor/house-southeast-05acffbb80d4652b.d3d", "05acffbb80d4652be80d484856fc77d2f15ba4e2d60cb0604add2298766757ce", "common-house-short-meshy-r1-static-albedo-r1" },
	{ "cabin-west", "d3d-models/editor/cabin-west-e58e48ab1f22a0e5.d3d", "e58e48ab1f22a0e59e299b6dbb75a15b22b6ccbc6506043f5ca8edc181830ae2", "cabin-west-meshy-r1-static-albedo-r1" },
	{ "cathedral", "d3d-models/editor/cathedral-uniform-de7ed5872d58ee5b.d3d", "de7ed5872d58ee5bede3879875ae776aaed7b7729ebc7a872bfefb238d9c195b", "cathedral-meshy-r2-uniform-door-anchor-r1" },
	{ "tavern-main", "d3d-models/editor/tavern-main-11cf73e7ea0b2779.d3d", "11cf73e7ea0b2779e8a6c8b989ea5ad0c062c518ece1e2674bc776d2093d88a0", "tavern-meshy-r1-main-disjoint-albedo-r1" },
	{ "tavern-wing", "d3d-models/editor/tavern-wing-a0adba330232fcd7.d3d", "a0adba330232fcd785e0640dcc76a11067ace1347cf326eac6870def0c7efc62", "tavern-meshy-r1-peer-disjoint-albedo-r1" },
	{ "smithy-house", "d3d-models/editor/smithy-house-2b86d6cd4c2ad826.d3d", "2b86d6cd4c2ad826f4e43ed3e77c071ec3c9f98922664ca2a84f63902ffe8739", "smithy-meshy-r1-main-disjoint-albedo-r1" },
	{ "smithy-forge", "d3d-models/editor/smithy-forge-de3fd42311cb4ff2.d3d", "de3fd42311cb4ff2139a87a38976517a9a320035d2d0a5a1470b04b702cecc9a", "smithy-meshy-r1-peer-disjoint-albedo-r1" },
	{ "well", "d3d-models/editor/well-f8f25dbfb49dd7a8.d3d", "f8f25dbfb49dd7a82ce4d7795bd71919b4ead773ce22165de37bc8bb59e83563", "well-local-lod-r1-static-albedo-inspection-r1" },
	{ "cabin-east", "d3d-models/cabin-east.d3d", "d12cc57e798151fb4abf3173149a4d7cf1da2cc8bdbc6ac39439aee3fe9069f9", "meshy-v2-multiview" },
}};

inline std::vector<uint8_t> Read(const std::filesystem::path &path)
{
	Check(std::filesystem::is_regular_file(path) && !std::filesystem::is_symlink(path), "regular private input: " + path.filename().string());
	std::ifstream input(path, std::ios::binary);
	Check(input.good(), "read private input");
	std::vector<uint8_t> bytes { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	Check(!input.bad(), "complete private input read");
	return bytes;
}
inline std::string Sha(std::span<const uint8_t> bytes)
{
	return ModHashToHex(ComputeBytesSha256(std::as_bytes(bytes)));
}
inline std::string FileSha(const std::filesystem::path &path) { return Sha(Read(path)); }
inline void WriteNew(const std::filesystem::path &path, std::span<const uint8_t> bytes)
{
	Check(!std::filesystem::exists(path), "disposable output must be new");
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	output.close();
	Check(output.good() && FileSha(path) == Sha(bytes), "write exact disposable bytes");
}
inline bool Below(const std::filesystem::path &path, const std::filesystem::path &root)
{
	const auto child = std::filesystem::weakly_canonical(path);
	const auto parent = std::filesystem::weakly_canonical(root);
	auto c = child.begin();
	for (auto p = parent.begin(); p != parent.end(); ++p, ++c)
		if (c == child.end() || *c != *p) return false;
	return c != child.end();
}
inline void ResetVisual()
{
	ResetTownViewResources();
	// Refreeze scene light on EVERY reset; wall time cannot affect comparisons.
	SetTownViewFireTimeForDiagnostics(0);
	Check(!GetTownViewOgdenPilotState().attempted, "reset starts with unattempted pilot");
}
inline std::string AnimationState()
{
	// NativeSceneState excludes animation counters. Snapshot public fields too.
	std::ostringstream state;
	for (const auto &npc : Towners) {
		state << static_cast<int>(npc._ttype) << ':' << npc._tAnimWidth << ',' << npc._tAnimDelay << ',' << npc._tAnimCnt
		      << ',' << static_cast<int>(npc._tAnimLen) << ',' << static_cast<int>(npc._tAnimFrame)
		      << ',' << static_cast<int>(npc._tAnimFrameCnt) << ';';
		for (const auto frame : npc.animOrder) state << static_cast<int>(frame) << ',';
		state << ';';
	}
	for (const auto &player : Players) {
		const auto &a = player.AnimInfo;
		state << static_cast<int>(a.ticksPerFrame) << ',' << static_cast<int>(a.tickCounterOfCurrentFrame)
		      << ',' << static_cast<int>(a.numberOfFrames) << ',' << static_cast<int>(a.currentFrame)
		      << ',' << a.isPetrified << ',' << static_cast<int>(a.getFrameToUseForRendering()) << ';';
	}
	return state.str();
}
struct Frame {
	std::vector<uint8_t> pixels;
	std::vector<GpuPickSnapshot> picks;
	TownViewOgdenPilotState pilot;
	TownViewRendererState renderer;
	TownGpuStatus gpu;
};
inline bool SamePicks(const std::vector<GpuPickSnapshot> &a, const std::vector<GpuPickSnapshot> &b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (!a[i].SameIdentity(b[i]) || a[i].depth != b[i].depth) return false;
	return true;
}
inline Frame Draw(const Surface &out, bool gpu)
{
	GetOptions().Graphics.townViewGpuRendering.SetValue(gpu);
	const auto native = NativeSceneState(), animation = AnimationState();
	const auto rng = GetLCGEngineState();
	const auto ticks = GetOgdenIdleNativeTicks(), generation = GetOgdenIdleNativeClockGeneration();
	const auto progress = ProgressToNextGameTick;
	Check(DrawTownView(out, true), "real world draw completes");
	Frame frame { ViewportPixels(out), ReadGpuWorldPicks(out), GetTownViewOgdenPilotState(), GetTownViewRendererState(), GetTownGpuStatus() };
	Check(NativeSceneState() == native && AnimationState() == animation && GetLCGEngineState() == rng
	          && GetOgdenIdleNativeTicks() == ticks && GetOgdenIdleNativeClockGeneration() == generation
	          && ProgressToNextGameTick == progress,
	    "draw preserves native map/actors/animation, RNG, clock and interpolation");
	if (gpu)
		Check(frame.renderer.requestedGpu && frame.renderer.usedGpu && frame.renderer.cpuRasterizedTriangles == 0
		          && frame.renderer.gpuSubmittedTriangles > 0 && frame.gpu.available && frame.gpu.frameSucceeded && !frame.gpu.warp,
		    "hardware GPU; no WARP/CPU raster: " + frame.renderer.failure);
	else
		Check(!frame.renderer.requestedGpu && !frame.renderer.usedGpu && frame.renderer.cpuRasterizedTriangles > 0,
		    "CPU reference identifies its raster path");
	return frame;
}
inline size_t CheckOgdenPicks(const Frame &frame, int ogden)
{
	size_t owned = 0, alias = 0;
	for (const auto &pick : frame.picks) {
		if (!pick.picked || pick.tile != Towners[ogden].position || pick.npc < 0) continue;
		if (pick.npc == ogden && pick.item == -1 && pick.player == -1) ++owned;
		else ++alias;
	}
	Check(owned > 0 && alias == 0, "actual native Ogden index owns visible pixels at its tile without alias");
	return owned;
}
inline void CheckPilot(const Frame &frame, int ogden)
{
	Check(frame.pilot.attempted && frame.pilot.loaded && frame.pilot.drawn && frame.pilot.nativeIndex == ogden
	          && frame.pilot.trianglesVisited > 0 && frame.pilot.nativeTicks == GetOgdenIdleNativeTicks()
	          && frame.pilot.clockGeneration == GetOgdenIdleNativeClockGeneration()
	          && std::abs(frame.pilot.loopSeconds - 4.0) < 0.000001,
	    "selected/loaded/skinned/drawn authored four-second pilot has actual NPC/clock identity");
	CheckOgdenPicks(frame, ogden);
}
inline void CheckArchitecture()
{
	const auto &scene = GetTownScene();
	const auto &map = GetTownEditorMapAudit();
	Check(map.status == "matched" && map.sourceKind == "override" && map.sha256 == MapSha && map.instances.size() == 12,
	    "copied habitual map actually decoded and all twelve bindings audited");
	for (const auto &expected : Architecture) {
		const auto found = std::find_if(scene.begin(), scene.end(), [&](const TownSceneModel &model) {
			const auto *binding = FindTownEditorModelBinding(model);
			return binding && binding->instanceId == expected.id;
		});
		Check(found != scene.end() && found->externalModel && found->importedTexture && !found->triangles.empty()
		          && found->runtimeAudit.status == "matched" && found->runtimeAudit.sourceKind == "override"
		          && found->runtimeAudit.assetPath == expected.path && found->runtimeAudit.sha256 == expected.sha
		          && found->runtimeAudit.expectedSha256 == expected.sha && found->runtimeAudit.revision == expected.revision,
		    std::string("actual selected architectural bytes decoded: ") + expected.id);
	}
	for (const auto &entry : map.instances)
		Check(entry.status == "matched" && entry.sha256 == entry.expectedSha256, "each map binding has matched runtime identity");
	// Loaded scene is thirteen imports. Do not claim all thirteen visible in pixels.
}
inline void RecordFrame(std::ofstream &evidence, bool &first, const Surface &out,
    const std::filesystem::path &output, std::string_view name, const Frame &frame, int ogden)
{
	const auto png = output / (std::string(name) + ".png");
	Check(!std::filesystem::exists(png), "capture destination new");
	SavePng(out.subregionY(0, gnViewportHeight), png);
	const auto camera = GetTownViewCameraState();
	evidence << (first ? "" : ",\n") << "{\"name\":";
	town_editor_snapshot_detail::WriteString(evidence, name);
	evidence << ",\"png\":";
	town_editor_snapshot_detail::WriteString(evidence, png.filename().string());
	evidence << ",\"sha256\":\"" << FileSha(png) << "\",\"width\":" << out.w() << ",\"height\":" << gnViewportHeight
	         << ",\"gpu\":" << (frame.renderer.usedGpu ? "true" : "false") << ",\"requestedGpu\":" << (frame.renderer.requestedGpu ? "true" : "false")
	         << ",\"cpuTriangles\":" << frame.renderer.cpuRasterizedTriangles << ",\"gpuTriangles\":" << frame.renderer.gpuSubmittedTriangles
	         << ",\"warp\":" << (frame.gpu.warp ? "true" : "false") << ",\"adapter\":";
	town_editor_snapshot_detail::WriteString(evidence, frame.renderer.usedGpu ? frame.gpu.adapter : "CPU/reference or discarded");
	evidence << ",\"pilotLoaded\":" << (frame.pilot.loaded ? "true" : "false") << ",\"pilotDrawn\":" << (frame.pilot.drawn ? "true" : "false")
	         << ",\"pilotIndex\":" << frame.pilot.nativeIndex << ",\"actualOgdenIndex\":" << ogden
	         << ",\"clockTicks\":" << GetOgdenIdleNativeTicks() << ",\"clockGeneration\":" << GetOgdenIdleNativeClockGeneration()
	         << ",\"sampleSeconds\":" << frame.pilot.sampleSeconds << ",\"loopSeconds\":" << frame.pilot.loopSeconds
	         << ",\"progress128\":" << static_cast<int>(ProgressToNextGameTick) << ",\"pauseMode\":" << PauseMode
	         << ",\"mode\":" << static_cast<int>(camera.mode) << ",\"yaw\":" << camera.yaw << ",\"pitch\":" << camera.pitch
	         << ",\"distance\":" << camera.distance << '}';
	evidence.flush();
	Check(evidence.good(), "persist actual frame evidence");
	first = false;
}
inline bool Interior(const std::vector<GpuPickSnapshot> &picks, int width, int x, int y)
{
	const auto &center = picks[static_cast<size_t>(y) * width + x];
	if (!center.picked || !std::isfinite(center.depth)) return false;
	for (int dy = -2; dy <= 2; ++dy)
		for (int dx = -2; dx <= 2; ++dx)
			if (!center.SameIdentity(picks[static_cast<size_t>(y + dy) * width + x + dx])) return false;
	return true;
}
inline void CompareStableOgden(const Frame &cpu, const Frame &gpu, int width, int height, int ogden)
{
	size_t stable = 0, identityMismatch = 0, depthMismatch = 0, colorDifference = 0;
	for (int y = 2; y + 2 < height; ++y) {
		for (int x = 2; x + 2 < width; ++x) {
			const size_t i = static_cast<size_t>(y) * width + x;
			if (cpu.picks[i].npc != ogden || !Interior(cpu.picks, width, x, y) || !Interior(gpu.picks, width, x, y)) continue;
			++stable;
			identityMismatch += cpu.picks[i].SameIdentity(gpu.picks[i]) ? 0 : 1;
			depthMismatch += std::abs(cpu.picks[i].depth - gpu.picks[i].depth) > 0.001F ? 1 : 0;
			colorDifference += cpu.pixels[i] == gpu.pixels[i] ? 0 : 1;
		}
	}
	Record("INFO Ogden stable5x5=" + std::to_string(stable) + " identityMismatch=" + std::to_string(identityMismatch)
	       + " depthMismatch=" + std::to_string(depthMismatch) + " indexedColorDifference=" + std::to_string(colorDifference));
	Check(stable > 0 && identityMismatch == 0 && depthMismatch == 0,
	    "CPU/hardware picks/depth agree on stable Ogden interiors; palette differences recorded, edges excluded");
}

inline void Run(const std::filesystem::path &output)
{
	const auto privateRoot = std::filesystem::path("D:/Diablo 1 3D/diagnostics/actor-procedural-integration-20261009");
	Check(Below(output, privateRoot) && std::filesystem::weakly_canonical(paths::PrefPath()) == std::filesystem::weakly_canonical(output)
	          && std::filesystem::weakly_canonical(paths::ConfigPath()) == std::filesystem::weakly_canonical(output),
	    "Pref/Config/output same private directory outside habitual profile");
	for (auto part = output; part != part.root_path(); part = part.parent_path())
		Check(!std::filesystem::is_symlink(part), "no output symlinks; root additionally rejects NTFS reparse points before launch");
	const auto originalOverrides = OverridePaths;
	const auto savedPause = PauseMode;
	const auto savedProgress = ProgressToNextGameTick;
	const auto savedTickRate = sgGameInitInfo.nTickRate;
	struct Restore {
		std::vector<std::string> paths;
		int pause; uint8_t progress; decltype(sgGameInitInfo.nTickRate) tickRate;
		~Restore() { OverridePaths = paths; PauseMode = pause; ProgressToNextGameTick = progress; sgGameInitInfo.nTickRate = tickRate; ResetTownViewResources(); SetTownViewFireTimeForDiagnostics(0); }
	} restore { originalOverrides, savedPause, savedProgress, savedTickRate };
	const auto package = Read(output / PackagePath), selection = Read(output / SelectionPath);
	Check(package.size() == 13190983 && Sha(package) == PackageSha, "exact frozen actor package in private profile");
	const auto selectionSha = Sha(selection);
	sgGameInitInfo.nTickRate = 20; PauseMode = 0; ProgressToNextGameTick = 0;
	InitializeTownDiagnostic();
	Check(!gbIsHellfire && !gbIsMultiplayer && leveltype == DTYPE_TOWN && currlevel == 0, "retail single-player town fixture");
	Check(std::count_if(Towners.begin(), Towners.end(), [](const Towner &n) { return n._ttype == TOWN_TAVERN; }) == 1, "exactly one native Ogden");
	const int ogden = static_cast<int>(std::distance(Towners.begin(), std::find_if(Towners.begin(), Towners.end(), [](const Towner &n) { return n._ttype == TOWN_TAVERN; })));
	Check(Towners[ogden].anim && Towners[ogden]._tAnimLen == 16 && Towners[ogden]._tAnimDelay == 3 && Towners[ogden].animOrder.size() == 111,
	    "actual native Ogden supported 16/3/111 contract");
	Check(GetOgdenIdleNativeTicks() == 0, "InitTowners reset native pilot clock");
	PlaceFixturePlayerNear(Towners[ogden].position + Displacement { 2, 2 });
	ViewPosition = Towners[ogden].position;
	gnScreenWidth = 960; gnScreenHeight = 540;
	GetOptions().Graphics.zoom.SetValue(false);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	GetOptions().Graphics.townViewHorizon.SetValue(false);
	GetOptions().Graphics.townViewFrustumCulling.SetValue(true);
	CalculatePanelAreas(); CalcViewportGeometry();
	Check(gnViewportHeight == 540 && !IsLeftPanelOpen() && !IsRightPanelOpen(), "wide unobstructed viewport");
	if (!IsTownViewActive()) ToggleTownView();
	OwnedSurface out(960, 540);
	SDL_SetPaletteColors(out.surface->format->palette, logical_palette.data(), 0, 256);
	ResetVisual(); ResetTownViewCamera();
	CheckArchitecture();
	const auto architectureState = SceneGeometryState(GetTownScene());
	const auto snapshotDir = output / "architecture-initial";
	Check(!std::filesystem::exists(snapshotDir), "snapshot directory new");
	std::filesystem::create_directories(snapshotDir);
	const auto exportNative = NativeSceneState(), exportAnim = AnimationState();
	const auto exportRng = GetLCGEngineState();
	Check(ExportTownEditorSnapshot(snapshotDir) && NativeSceneState() == exportNative && AnimationState() == exportAnim && GetLCGEngineState() == exportRng,
	    "export actual thirteen-import snapshot without native mutation");
	Check(!GetTownViewOgdenPilotState().attempted, "architecture export did not warm actor cache");
	Check(!std::filesystem::exists(output / "ogden-idle-evidence.json"), "new runtime evidence");
	std::ofstream evidence(output / "ogden-idle-evidence.json");
	Check(evidence.good(), "open evidence manifest");
	evidence << std::setprecision(10) << "{\"schema\":1,\"scope\":\"native DrawTownView offscreen; no dialogue/gameplay/FPS/art approval\",\"hardwareRequired\":true,\"architectureSnapshot\":\"architecture-initial/town-snapshot.json\",\"frames\":[\n";
	bool first = true;
	const auto capture = [&](std::string_view name, const Frame &frame) { RecordFrame(evidence, first, out, output, name, frame, ogden); };

	const auto cold = Draw(out, false); CheckPilot(cold, ogden); capture("ogden-cpu-cold", cold);
	Check(cold.pilot.nativeTicks == 0 && std::abs(cold.pilot.sampleSeconds - OgdenIdlePilotClipStart) < 0.00001F, "cold idle authored clip start");
	const auto warm = Draw(out, false);
	Check(cold.pixels == warm.pixels && SamePicks(cold.picks, warm.picks), "warm frozen CPU pixels/picks/depth exact");
	ProgressToNextGameTick = 32;
	const auto beforePause = Draw(out, false);
	PauseMode = 2;
	for (const uint8_t progress : { uint8_t { 0 }, uint8_t { 127 }, uint8_t { 64 } }) {
		ProgressToNextGameTick = progress;
		const auto held = Draw(out, false); CheckPilot(held, ogden);
		Check(held.pilot.nativeTicks == beforePause.pilot.nativeTicks && held.pilot.sampleSeconds == beforePause.pilot.sampleSeconds
		          && held.pixels == beforePause.pixels && SamePicks(held.picks, beforePause.picks),
		    "pause retains ticks/last unpaused interpolation: same sample/pixels/picks/depth");
	}
	capture("ogden-cpu-paused", Draw(out, false));
	// Actual game loop omits ProcessTowners during pause. This tests renderer
	// hold only; it does not execute the outer loop's pause gate.
	PauseMode = 0; ProgressToNextGameTick = 0;
	const auto beforeTicksNative = NativeSceneState(); const auto beforeTicksRng = GetLCGEngineState();
	for (unsigned tick = 1; tick <= 333; ++tick) {
		ProcessTowners();
		Check(GetOgdenIdleNativeTicks() == tick, "ProcessTowners advances one native visual tick");
		Check(NativeSceneState() == beforeTicksNative && GetLCGEngineState() == beforeTicksRng, "native ticking preserves placement/map/RNG in quest-disabled fixture");
		if (tick != 1 && tick != 80 && tick != 333) continue;
		const auto frame = Draw(out, false); CheckPilot(frame, ogden);
		const double span = static_cast<double>(OgdenIdlePilotClipEnd) - OgdenIdlePilotClipStart;
		const double expected = OgdenIdlePilotClipStart + std::fmod(static_cast<double>(tick) / 20.0, span);
		Check(std::abs(frame.pilot.sampleSeconds - expected) < 0.00001, "sample uses authored loop/native tick rate, not native frame order");
		if (tick == 80) Check(std::abs(frame.pilot.sampleSeconds - OgdenIdlePilotClipStart) < 0.00001F, "80 native ticks wrap authored four-second clip");
		if (tick == 333) Check(frame.pilot.sampleSeconds > 0.6F && frame.pilot.sampleSeconds < 0.8F
		                          && std::abs(OgdenNativeIdleSequenceSeconds - 16.65) < 0.000001, "native 16.65-second order not used as source clip duration");
		capture("ogden-cpu-tick-" + std::to_string(tick), frame);
	}
	struct PoseCase { const char *name; float yaw, pitch, zoom; };
	for (const auto test : std::array<PoseCase, 3> {{ { "view0", 0, 0, 0 }, { "view1", 0.30F, 0.03F, 1 }, { "view2", -0.25F, -0.02F, 2 } }}) {
		ResetTownViewCamera(); OrbitTownView(test.yaw, test.pitch); ZoomTownView(test.zoom);
		const auto cpu = Draw(out, false); CheckPilot(cpu, ogden); capture(std::string("ogden-cpu-") + test.name, cpu);
		const auto gpu = Draw(out, true); CheckPilot(gpu, ogden); capture(std::string("ogden-gpu-") + test.name, gpu);
		CompareStableOgden(cpu, gpu, out.w(), gnViewportHeight, ogden);
		const auto repeated = Draw(out, true);
		Check(repeated.pixels == gpu.pixels && SamePicks(repeated.picks, gpu.picks) && repeated.pilot.sampleSeconds == gpu.pilot.sampleSeconds, "frozen hardware pixels/IDs/depth/pose repeat exactly");
	}
	// One independent negative architecture root (13 D3Ds + map + lighting).
	// Per-case actor roots shadow only that root, NEVER the valid profile. No
	// input is renamed/deleted/overwritten; all negative bytes are new copies.
	const auto inputs = output / "ogden-gate-inputs";
	Check(!std::filesystem::exists(inputs), "new negative directories");
	std::filesystem::create_directories(inputs);
	const auto architectureRoot = inputs / "architecture";
	for (const auto &asset : Architecture) {
		const auto bytes = Read(output / asset.path);
		Check(Sha(bytes) == asset.sha, "original architecture input unchanged");
		WriteNew(architectureRoot / asset.path, bytes);
	}
	for (const char *relative : { "d3d-maps/tristram.ini", "d3d-lighting.ini" }) WriteNew(architectureRoot / relative, Read(output / relative));
	const auto validOverrides = OverridePaths;
	struct RestoreOverrides { std::vector<std::string> original; ~RestoreOverrides() { OverridePaths = original; ResetTownViewResources(); SetTownViewFireTimeForDiagnostics(0); } } restoreOverrides { validOverrides };
	for (const std::string name : { "missing-selection", "invalid-selection", "missing-package", "invalid-package" }) {
		const auto caseRoot = inputs / name;
		std::filesystem::create_directories(caseRoot);
		if (name != "missing-selection") {
			auto bytes = selection;
			if (name == "invalid-selection") { Check(!bytes.empty(), "nonempty sidecar negative"); bytes[0] ^= 1; }
			WriteNew(caseRoot / SelectionPath, bytes);
		}
		if (name == "missing-selection" || name == "invalid-selection") WriteNew(caseRoot / PackagePath, package);
		if (name == "invalid-package") { auto bytes = package; bytes.back() ^= 1; WriteNew(caseRoot / PackagePath, bytes); }
		// FindAsset concatenates its root and relative path; retain its separator.
		OverridePaths = { caseRoot.generic_string() + "/", architectureRoot.generic_string() + "/" };
		ResetVisual(); ResetTownViewCamera();
		CheckArchitecture();
		Check(SceneGeometryState(GetTownScene()) == architectureState, "actor negatives preserve thirteen architectural geometries");
		const auto frame = Draw(out, true);
		Check(frame.pilot.attempted && !frame.pilot.loaded && !frame.pilot.drawn, "missing/invalid actor input declines pilot: " + name);
		CheckOgdenPicks(frame, ogden); capture("ogden-gpu-" + name, frame);
		Check(SamePicks(frame.picks, Draw(out, true).picks), "native volume fallback repeats hardware picking");
	}
	OverridePaths = validOverrides; ResetVisual(); ResetTownViewCamera();
	const auto savedDelay = Towners[ogden]._tAnimDelay;
	struct RestoreDelay { int index; int16_t delay; ~RestoreDelay() { Towners[index]._tAnimDelay = delay; } } restoreDelay { ogden, savedDelay };
	Towners[ogden]._tAnimDelay = 4;
	const auto unsupported = Draw(out, true);
	Check(unsupported.pilot.loaded && !unsupported.pilot.drawn, "loaded pilot declines unsupported native animation");
	CheckOgdenPicks(unsupported, ogden); capture("ogden-gpu-unsupported-state", unsupported);
	Towners[ogden]._tAnimDelay = savedDelay;
	const auto retry = Draw(out, true); CheckPilot(retry, ogden); capture("ogden-gpu-supported-retry", retry);

	// Prime retained 2x output, then fail AFTER shadow submission; no Town2x
	// performance or GPU skinning claim. Whole-frame failure is the assertion.
	GetOptions().Graphics.townViewAntialiasing.SetValue(true);
	const auto hd = Draw(out, true); CheckPilot(hd, ogden);
	Check(GetTownViewSamplingState().factor == 2 && GetTownViewHighResolutionFrame() != nullptr, "healthy hardware retained high-resolution image");
	SetTownViewOgdenGpuSubmissionFailureForDiagnostics(true);
	const auto failNative = NativeSceneState(), failAnim = AnimationState();
	const auto failRng = GetLCGEngineState();
	const auto failTicks = GetOgdenIdleNativeTicks();
	Check(DrawTownView(out, true), "post-shadow failure returns discarded frame");
	const Frame failed { ViewportPixels(out), ReadGpuWorldPicks(out), GetTownViewOgdenPilotState(), GetTownViewRendererState(), GetTownGpuStatus() };
	Check(failed.renderer.requestedGpu && !failed.renderer.usedGpu && failed.renderer.cpuRasterizedTriangles == 0
	          && !failed.pilot.loaded && !failed.pilot.drawn
	          && failed.renderer.failure == "Ogden GPU submission allocation failed; frame discarded"
	          && *GetOptions().Graphics.townViewGpuRendering && GetTownViewHighResolutionFrame() == nullptr,
	    "partial GPU allocation discards whole frame, keeps GPU preference, no CPU raster/stale HD");
	Check(std::all_of(failed.pixels.begin(), failed.pixels.end(), [](uint8_t value) { return value == 0; })
	          && std::all_of(failed.picks.begin(), failed.picks.end(), [](const GpuPickSnapshot &p) { return !p.picked && p.architecture == -1 && !std::isfinite(p.depth); }),
	    "discarded pixels cleared; every pick/depth/architecture invalid");
	Check(NativeSceneState() == failNative && AnimationState() == failAnim && GetLCGEngineState() == failRng && GetOgdenIdleNativeTicks() == failTicks,
	    "post-shadow failure preserves native state/RNG/animation/clock");
	capture("ogden-gpu-discarded", failed);
	const auto nativeRetry = Draw(out, true);
	Check(!nativeRetry.pilot.loaded && !nativeRetry.pilot.drawn, "next hardware frame uses native actor, not poisoned pilot cache");
	CheckOgdenPicks(nativeRetry, ogden); capture("ogden-gpu-native-after-discard", nativeRetry);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	ResetVisual(); ResetTownViewCamera();
	const auto reload = Draw(out, true); CheckPilot(reload, ogden); capture("ogden-gpu-resource-reloaded", reload);

	ResetTownViewCamera();
	DrawActualNativeReference(out); const auto homePixels = ViewportPixels(out);
	Check(DrawTownView(out) && ViewportPixels(out) == homePixels && !GetTownViewRendererState().usedGpu, "Home selected is byte-exact to original backend");
	const auto absent = inputs / "missing-selection";
	OverridePaths = { absent.generic_string() + "/", architectureRoot.generic_string() + "/" };
	ResetVisual(); ResetTownViewCamera();
	Check(DrawTownView(out) && ViewportPixels(out) == homePixels && !GetTownViewRendererState().usedGpu, "Home without selection has same native pixels");
	OverridePaths = validOverrides; ResetVisual(); ResetTownViewCamera();
	Check(Read(output / PackagePath) == package && Read(output / SelectionPath) == selection && FileSha(output / SelectionPath) == selectionSha,
	    "valid private actor package/selection unchanged; negatives separate");
	CheckArchitecture();
	evidence << "\n],\"completed\":true,\"nativeBackendExact\":true,\"architectureLoaded\":13,\"negativeInputsAreSeparateCopies\":true,"
	            "\"pauseScope\":\"renderer hold; outer game loop not exercised\",\"simulationTicksExercised\":333,\"gpuSkinningClaim\":false,"
	            "\"fullGameplayClaim\":false,\"dialogueClaim\":false,\"gameFpsClaim\":false,\"artApprovalClaim\":false}\n";
	evidence.close(); Check(evidence.good(), "complete evidence manifest");
}
} // namespace ogden_idle_render_gate
