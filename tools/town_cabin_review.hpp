#pragma once

// Included inside town_view_smoke's anonymous namespace. These fixtures use
// the actual selected scene, borrowed textures and independent triangle rays.
// Captures contain private game art and stay outside the public repository.

using CabinReviewVisibility = std::tuple<int, float, bool, int, int, int, int, int>;

std::vector<CabinReviewVisibility> CaptureCabinReviewVisibility(const Surface &out)
{
	std::vector<CabinReviewVisibility> result;
	result.reserve(static_cast<size_t>(out.w()) * gnViewportHeight);
	for (int y = 0; y < gnViewportHeight; ++y) {
		for (int x = 0; x < out.w(); ++x) {
			Point tile { -1, -1 };
			int npc = -1, item = -1, player = -1;
			const bool picked = Pick({ x, y }, tile, npc, item, player);
			result.emplace_back(TownViewArchitectureAt({ x, y }), TownViewDepthAt({ x, y }), picked,
			    tile.x, tile.y, npc, item, player);
		}
	}
	return result;
}

void CheckCabinWindowLightBlockers(const TownSceneModel &model)
{
	const auto &interior = *model.cabinInterior;
	Check(interior.lightOccluders.size() == 5 && interior.lightOccluders.front().apertures.size() == 3,
	    "cabin candle visibility uses its room and the four actual round-window timber bars");
	const TownLightVector blocked { 71.80229F, 2.26367034F, 71.59660148F };
	const TownLightVector clear { 71.79857893F, 2.28710413F, 71.53200023F };
	const auto source = interior.fireSources.front().light.position;
	const std::span<const TownLightOccluder> room(&interior.lightOccluders.front(), 1);
	Check(TownPointLightVisibility(source, blocked, room) == 1
	        && TownPointLightVisibility(source, blocked, interior.lightOccluders) == 0
	        && TownPointLightVisibility(source, clear, interior.lightOccluders) == 1,
	    "reproduced tunnel receiver is shadowed by its timber bar while a real adjacent clear receiver remains lit");
	const auto blocks = [&](TownLightVector receiver) {
		const RayVector origin { source.x, source.height, source.z };
		const RayVector direction { receiver.x - source.x, receiver.height - source.height, receiver.z - source.z };
		for (const auto &triangle : interior.interiorTriangles) {
			double distance, u, v;
			if (RayTriangle(origin, direction, triangle, distance, u, v) && distance > 0.00001 && distance < 0.999)
				return true;
		}
		return false;
	};
	Check(blocks(blocked) && !blocks(clear),
	    "independent actual interior triangle rays confirm the blocked and clear regression receivers");
	TownLightingConfig config;
	config.directionalIntensity = 0;
	const TownLightVector normal { -0.98769148F, -0.15641466F, 0 };
	const std::span<const TownPointLight> light(&interior.fireSources.front().light, 1);
	Check(SampleTownLighting(normal, blocked, 0, config, light, room).point.red > 0
	        && SampleTownLighting(normal, blocked, 0, config, light, interior.lightOccluders).point.red == 0
	        && SampleTownLighting(normal, clear, 0, config, light, interior.lightOccluders).point.red > 0,
	    "the actual CPU diffuse sampler removes leaked candle light and retains the clear receiver contribution");
}

void RunCabinReview(const std::filesystem::path &output)
{
	RunTownCabinLightGpuChecks(Check);
	InitializeTownDiagnostic();
	ToggleTownView();
	PlaceFixturePlayerNear({ 76, 69 });
	ViewPosition = { 71, 69 };
	CharFlag = false;
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	gnScreenWidth = 640;
	gnScreenHeight = 640 + GetMainPanel().size.height;
	CalculatePanelAreas();
	// The responsive HUD can expose the entire window below its controls.
	// Keep this world fixture's historical crop and camera anchor explicit,
	// independently of the product's current UI viewport policy.
	gnViewportHeight = 640;
	CalcViewportGeometry();
	OwnedSurface allocation(gnScreenWidth, gnScreenHeight + 2);
	SDL_SetPaletteColors(allocation.surface->format->palette, logical_palette.data(), 0, 256);
	SDL_FillRect(allocation.surface, nullptr, 255);
	const Surface out = allocation.subregionY(0, gnScreenHeight);
	ResetTownViewCamera();
	Check(DrawTownView(out, true), "prepare G1 forced-geometry scene at the recorded original camera");
	const auto &scene = GetTownScene();
	const auto found = std::find_if(scene.begin(), scene.end(), [](const TownSceneModel &model) {
		return model.kind == TownSceneKind::Cabin && model.minTile == Point { 70, 66 };
	});
	Check(found != scene.end() && found->externalModel && found->cabinInterior
	        && found->runtimeAudit.sha256 == TownCabinBaselineSha256 && found->runtimeAudit.status == "matched",
	    "G1 requires the bytes actually decoded to match the explicitly selected cabin baseline");
	const int modelIndex = static_cast<int>(found - scene.begin());
	CheckCabinWindowLightBlockers(*found);
	Check(ExportTownEditorSnapshot(output), "record actual decoded identity and assembled geometry for G1");
	const std::string original = NativeSceneState();
	const std::string geometry = SceneGeometryState(scene);
	const auto random = GetLCGEngineState();
	ExportNativeGroundPieces(output);
	std::ofstream manifest(output / "cabin-review.json");
	manifest << std::setprecision(9)
	         << "{\"sourceSha256\":" << std::quoted(found->runtimeAudit.sha256)
	         << ",\"revision\":" << std::quoted(found->runtimeAudit.revision)
	         << ",\"raw\":\"forced geometry; never native fallback\",\"fireTimeSeconds\":0,"
	            "\"width\":640,\"height\":640,\"nativeZoom\":false,\"worldSamplingFactor\":1,"
	            "\"shadowToggleScope\":\"all directional geometry shadows; native painted shading and candle illumination remain\","
	            "\"artisticApproval\":false,\"frames\":[";
	bool first = true;
	for (bool gpu : { false, true }) {
		GetOptions().Graphics.townViewGpuRendering.SetValue(gpu);
		const auto directory = output / (gpu ? "gpu" : "cpu");
		std::filesystem::create_directories(directory);
		for (int degrees : { -5, 0, 5, 45, 90, 135, 180, 225, 270, 315 }) {
			ResetTownViewCamera();
			OrbitTownView(degrees * 3.14159265358979323846F / 180, 0);
			SetTownViewDirectionalShadowsEnabledForDiagnostics(true);
			Check(DrawTownView(out, true), "draw G1 frozen shadow-on frame");
			const auto renderer = GetTownViewRendererState();
			Check(renderer.usedGpu == gpu && (gpu ? !GetTownGpuStatus().warp && renderer.cpuRasterizedTriangles == 0
			                                         : renderer.cpuRasterizedTriangles > 0),
			    "G1 uses the requested CPU or hardware GPU without silent fallback: " + renderer.failure);
			const auto lit = ViewportPixels(out);
			const auto visibility = CaptureCabinReviewVisibility(out);
			const auto coverage = CheckArchitectureCoverage(modelIndex, out);
			Check(coverage.covered > 0 && coverage.holes == 0, "G1 cabin has no unexplained independent ray coverage holes");
			SavePng(out.subregionY(0, gnViewportHeight), directory / ("orbit-" + std::to_string(degrees) + "-shadows-on.png"));
			SetTownViewDirectionalShadowsEnabledForDiagnostics(false);
			Check(DrawTownView(out, true), "draw G1 same-camera frame with directional geometry shadows disabled");
			Check(!GetTownViewLightingState().directionalShadowsEnabled && CaptureCabinReviewVisibility(out) == visibility,
			    "shadow A/B preserves every visible depth, architectural owner and native interaction ID");
			SavePng(out.subregionY(0, gnViewportHeight), directory / ("orbit-" + std::to_string(degrees) + "-shadows-off.png"));
			int changed = 0, groundChanged = 0, nearCabinGroundChanged = 0, newBlack = 0;
			for (int y = 0; y < gnViewportHeight; ++y) {
				for (int x = 0; x < out.w(); ++x) {
					const size_t index = static_cast<size_t>(y) * out.w() + x;
					if (lit[index] == out[{ x, y }])
						continue;
					++changed;
					const auto &[owner, depth, picked, tileX, tileY, npc, item, player] = visibility[index];
					const bool ground = picked && owner < 0 && npc < 0 && item < 0 && player < 0
					    && tileX >= 0 && tileY >= 0 && !TownVegetationReplacesTile({ tileX, tileY }) && !TownPropReplacesTile({ tileX, tileY });
					groundChanged += ground ? 1 : 0;
					nearCabinGroundChanged += ground && tileX >= 68 && tileX <= 77 && tileY >= 64 && tileY <= 75 ? 1 : 0;
					const auto color = logical_palette[lit[index]];
					newBlack += color.r == 0 && color.g == 0 && color.b == 0 ? 1 : 0;
				}
			}
			Check(changed > 0 && groundChanged > 0, "actual geometry shadows change visible ground at this orbit angle");
			SetTownViewDirectionalShadowsEnabledForDiagnostics(true);
			Check(DrawTownView(out, true) && ViewportPixels(out) == lit && CaptureCabinReviewVisibility(out) == visibility,
			    "restoring physical shadows recovers the exact frozen frame and interaction buffers");
			bool guards = true;
			for (int y = gnViewportHeight; y < allocation.h(); ++y)
				for (int x = 0; x < allocation.w(); ++x)
					guards = guards && allocation[{ x, y }] == 255;
			Check(guards && NativeSceneState() == original && GetLCGEngineState() == random && SceneGeometryState(GetTownScene()) == geometry,
			    "G1 A/B preserves native map, collision, actors, RNG, geometry and UI guard rows");
			const auto camera = GetTownViewCameraState();
			manifest << (first ? "" : ",") << "{\"backend\":\"" << (gpu ? "hardware-gpu" : "cpu")
			         << "\",\"orbitDegrees\":" << degrees << ",\"focus\":[71,69],\"heroTile\":[76,69],\"cameraYaw\":" << camera.yaw
			         << ",\"cameraPitch\":" << camera.pitch << ",\"cameraDistance\":" << camera.distance
			         << ",\"cameraPan\":[" << camera.offsetX << ',' << camera.offsetZ << "],\"architectureCovered\":" << coverage.covered
			         << ",\"architectureHoles\":" << coverage.holes << ",\"changedPixels\":" << changed << ",\"groundChangedPixels\":" << groundChanged
			         << ",\"nearCabinGroundChangedPixels\":" << nearCabinGroundChanged << ",\"shadowedPureBlackPixels\":" << newBlack << '}';
			first = false;
			if (degrees == 0 || degrees == 180)
				CheckCabinInteriorLight(out, modelIndex, directory, degrees);
		}
	}
	manifest << "],\"visibilityUnchanged\":true,\"nativeStatePreserved\":true}\n";
	Check(manifest.good(), "record G1 source identity, exact cameras and shadow A/B evidence without artistic approval");
	ResetTownViewCamera();
	DrawActualNativeReference(out);
	const auto native = ViewportPixels(out);
	SetTownViewDirectionalShadowsEnabledForDiagnostics(false);
	Check(DrawTownView(out) && ViewportPixels(out) == native && !GetTownViewRendererState().usedGpu,
	    "the G1 diagnostic shadow toggle preserves Home's exact original backend");
	ResetTownViewResources();
	Check(GetTownViewLightingState().directionalShadowsEnabled, "resource reset restores production geometry shadows");
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	FreeTownerGFX();
}
