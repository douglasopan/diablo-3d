#pragma once

#include "engine/render/town_camera_collision.hpp"

// Include after the native town diagnostic/eye-height helpers, before main.
namespace {
void RunFollowCameraCpu(const std::filesystem::path &output)
{
	size_t pureChecks = 0;
	Check(RunTownCameraChecks(std::cout, pureChecks), "pure follow-camera contract passes");
	InitializeTownDiagnostic();
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	GetOptions().Graphics.townViewHorizon.SetValue(false);
	GetOptions().Graphics.zoom.SetValue(false);
	if (!IsTownViewActive()) ToggleTownView();
	gnScreenWidth = 640; gnScreenHeight = gnViewportHeight = 480;
	CalculatePanelAreas(); CalcViewportGeometry();
	SetTownViewFireTimeForDiagnostics(0);
	const auto rng = GetLCGEngineState();
	const auto actors = eye_height_fixture::ActorFrames();
	const auto geometry = eye_height_fixture::SceneIdentity();
	eye_height_fixture::WriteBytes(output / "scene-identity.txt", geometry);
	TownCameraCollisionIndex independent;
	std::vector<TownCameraCollisionTriangle> triangles;
	const auto append = [&](const auto &surfaces) {
		for (const auto &surface : surfaces) {
			if (surface.surfaceDetail == TownSceneSurfaceDetail::FireCore || surface.surfaceDetail == TownSceneSurfaceDetail::FireTip) continue;
			TownCameraCollisionTriangle triangle;
			for (size_t i = 0; i < 3; ++i) triangle.vertices[i] = { surface.vertices[i].x, surface.vertices[i].height, surface.vertices[i].z };
			triangles.push_back(triangle);
		}
	};
	std::vector<TownArchitectureBounds> bounds;
	for (const auto &model : GetTownScene()) {
		append(TownSceneExteriorTriangles(model));
		if (model.cabinInterior) append(model.cabinInterior->interiorTriangles);
		if (model.kind == TownSceneKind::Cabin || model.kind == TownSceneKind::Cathedral)
			bounds.push_back(BuildTownArchitectureBounds(model));
	}
	independent.Build(std::move(triangles));
	Check(independent.triangleCount() > 0 && bounds.size() >= 2, "real selected town has architecture blockers and bounded review locations");
	OwnedSurface out(640, 480);
	SDL_SetPaletteColors(out.surface->format->palette, logical_palette.data(), 0, 256);
	size_t blocked = 0, valid = 0, closed = 0, cacheBuilds = 0;
	std::ofstream evidence(output / "follow-camera-measurements.txt");
	evidence << "scope=CPU world only; real selected architecture; no physical capture or FPS evidence\n";
	const auto draw = [&] {
		const auto native = NativeSceneState();
		Check(DrawTownView(out, true), "production CPU follow draw completes, including explicit closed-frame failure");
		const auto state = GetTownViewFollowCameraState();
		Check(state.active && state.triangles == independent.triangleCount(), "cached production collision uses the selected real triangle count");
		Check(!GetTownViewRendererState().requestedGpu && !GetTownViewRendererState().usedGpu, "follow fixture never requests a GPU backend");
		Check(NativeSceneState() == native && GetLCGEngineState() == rng && eye_height_fixture::ActorFrames() == actors,
		    "camera resolution preserves native map/collision/actor frames and RNG");
		if (cacheBuilds != 0) Check(state.cacheBuilds == cacheBuilds, "ordinary camera redraw reuses the scene BVH");
		cacheBuilds = state.cacheBuilds;
		if (state.valid) {
			++valid;
			const auto hit = independent.Sweep(state.resolvedEye, state.resolvedEye, state.radius);
			Check(hit.valid && !hit.initialOverlap && state.resolvedEye.height >= state.radius,
			    "published real eye and near-plane sphere are outside architecture and above ground");
			Check(state.trianglesTested < state.triangles, "warm collision queries avoid scanning every selected triangle");
		} else {
			++closed;
			Point tile; int npc, item, player;
			Check(!PickTownView({320,240}, tile, npc, item, player), "unresolvable near-plane sphere publishes no stale native target");
		}
		blocked += state.blocked;
		evidence << "mode=" << static_cast<int>(GetTownViewCameraMode()) << " valid=" << state.valid << " blocked=" << state.blocked
		         << " desired=" << state.desiredDistance << " visual=" << state.visualDistance << " resolved=" << state.resolvedDistance
		         << " eye=" << state.resolvedEye.x << ',' << state.resolvedEye.height << ',' << state.resolvedEye.z
		         << " radius=" << state.radius << " triangles=" << state.triangles << " tests=" << state.trianglesTested
		         << " cacheBuilds=" << state.cacheBuilds << '\n';
		return state;
	};
	for (size_t location = 0; location < std::min<size_t>(bounds.size(), 3); ++location) {
		const auto &box = bounds[location];
		PlaceFixturePlayerNear({ static_cast<int>(box.minimum.x) - 1, static_cast<int>((box.minimum.z + box.maximum.z) / 2) });
		SetTownViewCameraMode(TownCameraMode::ThirdPerson);
		SetTownViewCameraPoseForDiagnostics({ 0, 0.2F, 8, {} });
		for (unsigned turn = 0; turn < 8; ++turn) {
			OrbitTownView(3.14159265358979323846F / 4, 0);
			AdvanceTownViewCamera(0.1F);
			draw();
		}
		SetTownViewCameraPoseForDiagnostics({ 0, -1, 8, {} });
		AdvanceTownViewCamera(0.1F);
		draw(); // Upward pitch must shorten the boom above the floor.
	}
	Check(valid > 0 && blocked > 0, "real architecture/floor constrain at least one valid follow pose");
	// Bounded before/middle/end pictures at one unchanged native position.
	SetTownViewCameraMode(TownCameraMode::ThirdPerson);
	SetTownViewCameraPoseForDiagnostics({ 0.7F, 0.15F, 2, {} });
	draw(); SavePng(out, output / "01-third.png");
	const auto beforeEntry = eye_height_fixture::IndexedViewport(out);
	ZoomTownView(50);
	draw(); SavePng(out, output / "02-entry-same-eye.png");
	Check(eye_height_fixture::IndexedViewport(out) == beforeEntry, "wheel entry preserves the displayed image before visual time advances");
	AdvanceTownViewCamera(0.05F);
	draw(); SavePng(out, output / "03-entry-middle.png");
	for (unsigned step = 0; step < 16; ++step) AdvanceTownViewCamera(0.1F);
	draw(); SavePng(out, output / "04-first.png");
	ZoomTownView(-8);
	AdvanceTownViewCamera(0.05F);
	const auto once = draw(); SavePng(out, output / "05-exit-middle.png");
	const auto twice = draw();
	Check(std::abs(once.resolvedEye.x - twice.resolvedEye.x) < 0.000001F
	        && std::abs(once.resolvedEye.height - twice.resolvedEye.height) < 0.000001F
	        && std::abs(once.resolvedEye.z - twice.resolvedEye.z) < 0.000001F,
	    "a second draw without Advance cannot reuse camera recovery time");
	for (unsigned step = 0; step < 16; ++step) AdvanceTownViewCamera(0.1F);
	draw(); SavePng(out, output / "06-third-out.png");
	Check(eye_height_fixture::SceneIdentity() == geometry, "all assembled geometry/material bytes remain unchanged by the camera");
	evidence << "valid=" << valid << " blocked=" << blocked << " closed=" << closed << " pureChecks=" << pureChecks << '\n';
	Check(evidence.good(), "write bounded native follow-camera measurements");
}
} // namespace
