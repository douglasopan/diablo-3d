#pragma once

// CPU-only before/after eye-height fixture. Include immediately before main()
// in town_view_smoke.cpp, after its existing helpers.
// No new production seam, SDL window, physical mouse, simulation ticks or GPU.
#include <iterator>

namespace {
namespace eye_height_fixture {

uint64_t HashBytes(const void *data, size_t size, uint64_t hash = 1469598103934665603ULL)
{
	const auto *bytes = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

std::string ReadBytes(const std::filesystem::path &path)
{
	std::ifstream input(path, std::ios::binary);
	Check(input.good(), "read baseline fixture file " + path.filename().string());
	return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void WriteBytes(const std::filesystem::path &path, const std::string &bytes)
{
	std::ofstream output(path, std::ios::binary);
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Check(output.good(), "write private fixture " + path.filename().string());
}

std::string ActorFrames()
{
	std::ostringstream state;
	const auto appendSprite = [&](ClxSprite sprite) {
		state << sprite.width() << ',' << sprite.height() << ','
		      << HashBytes(sprite.pixelData(), sprite.pixelDataSize()) << ';';
	};
	for (const Player &player : Players) {
		state << player.AnimInfo.getFrameToUseForRendering() << ';';
		appendSprite(player.currentSprite());
	}
	for (const Towner &towner : Towners) {
		state << static_cast<int>(towner._ttype) << ',' << static_cast<int>(towner._tAnimFrame) << ','
		      << towner._tAnimCnt << ',' << static_cast<int>(towner._tAnimFrameCnt) << ','
		      << static_cast<int>(towner._tAnimLen) << ',' << towner._tAnimDelay << ','
		      << static_cast<int>(towner.gossip) << ';';
		if (towner.anim)
			appendSprite(towner.currentSprite());
	}
	return state.str();
}

// Field-wise hashing avoids struct padding and avoids materializing the entire
// selected high-detail scene into a second enormous string. Not a security hash.
std::string SceneIdentity()
{
	std::ostringstream result;
	for (const TownSceneModel &model : GetTownScene()) {
		uint64_t geometry = 1469598103934665603ULL;
		const auto add = [&](const auto &value) { geometry = HashBytes(&value, sizeof(value), geometry); };
		const auto triangles = [&](const auto &list) {
			for (const auto &triangle : list) {
				add(triangle.material);
				add(triangle.surfaceRole);
				add(triangle.surfaceDetail);
				add(triangle.sourceTile.x); add(triangle.sourceTile.y);
				add(triangle.pickTile.x); add(triangle.pickTile.y);
				add(triangle.nativeProjection);
				add(triangle.normal.x); add(triangle.normal.height); add(triangle.normal.z);
				for (const auto &vertex : triangle.vertices) {
					add(vertex.x); add(vertex.height); add(vertex.z); add(vertex.u); add(vertex.v);
				}
			}
		};
		triangles(model.triangles);
		if (model.cabinInterior) {
			triangles(model.cabinInterior->exteriorTriangles);
			triangles(model.cabinInterior->interiorTriangles);
			for (const auto &fire : model.cabinInterior->fireSources)
				triangles(fire.emissiveTriangles);
		}
		result << static_cast<int>(model.kind) << ':' << model.minTile.x << ',' << model.minTile.y
		       << ':' << model.triangles.size() << ':' << geometry << ':' << model.externalModel
		       << ':' << model.runtimeAudit.assetPath << ':' << model.runtimeAudit.revision
		       << ':' << model.runtimeAudit.sha256 << ':' << model.runtimeAudit.status;
		if (model.importedTexture)
			result << ":texture=" << model.importedTexture->width << 'x' << model.importedTexture->height
			       << ':' << HashBytes(model.importedTexture->rgb.data(), model.importedTexture->rgb.size());
		result << '\n';
	}
	return result.str();
}

std::string IndexedViewport(const Surface &out)
{
	const auto pixels = ViewportPixels(out);
	std::string result(reinterpret_cast<const char *>(pixels.data()), pixels.size());
	// Palette bytes included explicitly: identical indices alone cannot prove
	// identical color. Dimensions are fixed by this fixture and recorded below.
	for (const SDL_Color color : logical_palette) {
		result.push_back(static_cast<char>(color.r));
		result.push_back(static_cast<char>(color.g));
		result.push_back(static_cast<char>(color.b));
		result.push_back(static_cast<char>(color.a));
	}
	return result;
}

struct PickBounds {
	int left = std::numeric_limits<int>::max(), top = std::numeric_limits<int>::max();
	int right = -1, bottom = -1;
	size_t pixels = 0;
};

PickBounds NpcBounds(int index, int width, int height)
{
	PickBounds bounds;
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			Point tile;
			int towner, item, player;
			if (!PickTownView({ x, y }, tile, towner, item, player) || towner != index)
				continue;
			bounds.left = std::min(bounds.left, x); bounds.right = std::max(bounds.right, x);
			bounds.top = std::min(bounds.top, y); bounds.bottom = std::max(bounds.bottom, y);
			++bounds.pixels;
		}
	return bounds;
}

} // namespace eye_height_fixture

void RunFirstPersonEyeHeightCpu(const std::filesystem::path &output, float expectedFirstPersonEyeHeight,
    const std::filesystem::path &baselineDirectory = {})
{
	using namespace eye_height_fixture;
	Check(expectedFirstPersonEyeHeight == 1.1F || expectedFirstPersonEyeHeight == 1.7F,
	    "bounded comparison expects baseline 1.1 or candidate 1.7 world units");
	InitializeTownDiagnostic(); // Original archive/palette/Warrior/NPCs; no save/music.
	const std::string originalNative = NativeSceneState(), originalFrames = ActorFrames();
	const auto originalRandom = GetLCGEngineState();
	{
		struct Restore {
			ActorPosition position;
			Point view;
			TownViewCameraState camera;
			bool active, gpu, smoothing, zoom;
			int width, height, viewport;
			int8_t occupancy[MAXDUNX][MAXDUNY];
			DungeonFlag flags[MAXDUNX][MAXDUNY];
			~Restore()
			{
				MyPlayer->position = position; ViewPosition = view;
				std::memcpy(dPlayer, occupancy, sizeof(occupancy));
				std::memcpy(dFlags, flags, sizeof(flags));
				GetOptions().Graphics.townViewGpuRendering.SetValue(gpu);
				GetOptions().Graphics.townViewAntialiasing.SetValue(smoothing);
				GetOptions().Graphics.zoom.SetValue(zoom);
				gnScreenWidth = width; gnScreenHeight = height; gnViewportHeight = viewport;
				CalculatePanelAreas(); CalcViewportGeometry();
				SetTownViewCameraMode(camera.mode);
				SetTownViewCameraPoseForDiagnostics({ camera.yaw, camera.pitch, camera.distance, { camera.offsetX, 0, camera.offsetZ } });
				if (IsTownViewActive() != active) ToggleTownView();
			}
		};
		Restore restore { MyPlayer->position, ViewPosition, GetTownViewCameraState(), IsTownViewActive(),
			*GetOptions().Graphics.townViewGpuRendering, *GetOptions().Graphics.townViewAntialiasing,
			*GetOptions().Graphics.zoom, gnScreenWidth, gnScreenHeight, gnViewportHeight, {}, {} };
		std::memcpy(restore.occupancy, dPlayer, sizeof(dPlayer));
		std::memcpy(restore.flags, dFlags, sizeof(dFlags));
		GetOptions().Graphics.townViewGpuRendering.SetValue(false);
		GetOptions().Graphics.townViewAntialiasing.SetValue(false);
		GetOptions().Graphics.zoom.SetValue(false);
		if (!IsTownViewActive()) ToggleTownView();
		// Match the existing CPU initializer (640x480). No resolution, FOV,
		// culling, horizon, texture, lighting or asset-quality A/B is mixed in.
		OwnedSurface allocation(gnScreenWidth, gnScreenHeight + 2);
		SDL_SetPaletteColors(allocation.surface->format->palette, logical_palette.data(), 0, 256);
		const Surface out = allocation.subregionY(0, gnScreenHeight);
		const std::string sceneBefore = SceneIdentity();
		WriteBytes(output / "scene-identity.txt", sceneBefore);
		if (!baselineDirectory.empty())
			Check(ReadBytes(baselineDirectory / "scene-identity.txt") == sceneBefore,
			    "baseline and candidate use identical assembled geometry and RGB texture contents");
		std::ofstream measurements(output / "eye-height-measurements.txt");
		measurements << "scope=CPU world-only, original native NPC sprite bodies; no physical input, HUD, GPU or gameplay/FPS proof\n"
		             << "world viewport=" << gnScreenWidth << 'x' << gnViewportHeight << "; fireTime=0\n"
		             << "clx anchor=bottom pixel; actor body grounded by current BuildTownActorSingleViewBody\n";
		// One bounded viewpoint: Griswold and the selected nearby smithy.
		for (const auto type : { TOWN_SMITH }) {
			Towner *npc = GetTowner(type);
			Check(npc != nullptr && npc->anim.has_value(), "actual selected town has the requested native humanoid NPC");
			const int index = static_cast<int>(npc - Towners.data());
			const std::string stem = "npc-" + std::to_string(static_cast<int>(type));
			const auto sprite = DecodeVolumeSprite(npc->currentSprite());
			ExportDecodedActor(sprite, output / (stem + "-source.png"));
			ExportActorBodyMask(sprite, output / (stem + "-body-mask.png"));
			const auto bodyOpacity = TownActorBodyOpacity(sprite.view());
			Check(bodyOpacity.size() == sprite.opacity.size(), "actual NPC body opacity retains sprite dimensions");
			int sourceTop = sprite.height, sourceBottom = -1, bodyTop = sprite.height, bodyBottom = -1;
			for (int y = 0; y < sprite.height; ++y)
				for (int x = 0; x < sprite.width; ++x) {
					const size_t pixel = static_cast<size_t>(y) * sprite.width + x;
					if (sprite.opacity[pixel]) { sourceTop = std::min(sourceTop, y); sourceBottom = std::max(sourceBottom, y); }
					if (bodyOpacity[pixel]) { bodyTop = std::min(bodyTop, y); bodyBottom = std::max(bodyBottom, y); }
				}
			const auto mesh = BuildTownActorSingleViewBody(sprite.view(), true); // Exact production NPC builder.
			float minHeight = std::numeric_limits<float>::infinity(), maxHeight = -minHeight;
			for (const auto &triangle : mesh.triangles)
				for (const auto &vertex : triangle.vertices) {
					minHeight = std::min(minHeight, vertex.height); maxHeight = std::max(maxHeight, vertex.height);
				}
			Check(!mesh.triangles.empty() && std::abs(minHeight) < 0.0001F && maxHeight > 0,
			    "actual current NPC body has a grounded pivot and measurable world height");
			const std::string actorIdentity = VolumeGeometryState(mesh) + ActorFrames();
			WriteBytes(output / (stem + "-actor-identity.bin"), actorIdentity);
			if (!baselineDirectory.empty())
				Check(ReadBytes(baselineDirectory / (stem + "-actor-identity.bin")) == actorIdentity,
				    "same actual NPC frame, sprite content, body dimensions and player pose across builds");
			PlaceFixturePlayerNear(npc->position + Displacement { 4, 4 });
			const Point playerTile = MyPlayer->position.tile;
			const std::string frozen = NativeSceneState(), frames = ActorFrames();
			WriteBytes(output / (stem + "-native-state.bin"), frozen + frames);
			if (!baselineDirectory.empty())
				Check(ReadBytes(baselineDirectory / (stem + "-native-state.bin")) == frozen + frames,
				    "same map, collision, NPC/player positions and pose across baseline and candidate");
			const float yaw = std::atan2(static_cast<float>(playerTile.y - npc->position.y),
			    static_cast<float>(playerTile.x - npc->position.x)) + 0.25F;
			// Small yaw offset lets the NPC remain visible beside the hero in third
			// person. Both modes use the SAME yaw/pitch/pan; follow distance differs
			// by mode (8 versus the required first-person zero).
			const TownCameraPose pose { yaw, 0.005F, 8.0F, {} };
			measurements << stem << " native-frame=" << static_cast<int>(npc->_tAnimFrame)
			             << " source=" << sprite.width << 'x' << sprite.height << " clx-bottom-row=" << sprite.height - 1
			             << " opaque-rows=[" << sourceTop << ',' << sourceBottom << "] body-mask-rows=[" << bodyTop << ',' << bodyBottom << ']'
			             << " native-render-offset-x=" << npc->getRenderingOffset().deltaX
			             << " body-world-y=[" << minHeight << ',' << maxHeight << "] triangles=" << mesh.triangles.size()
			             << " NPC-tile=" << npc->position.x << ',' << npc->position.y
			             << " player-tile=" << playerTile.x << ',' << playerTile.y << " yaw=" << yaw << " pitch=" << pose.pitch << '\n';
			for (const auto mode : { TownCameraMode::ThirdPerson, TownCameraMode::FirstPerson }) {
				const bool first = mode == TownCameraMode::FirstPerson;
				const std::string name = stem + (first ? "-first-person" : "-third-person");
				SetTownViewCameraMode(mode); SetTownViewCameraPoseForDiagnostics(pose);
				const auto camera = GetTownViewCameraState();
				Check(std::abs(camera.eyeHeight - (first ? expectedFirstPersonEyeHeight : 1.1F)) < 0.00001F,
				    "effective camera eye height is correct for " + name);
				SDL_FillRect(allocation.surface, nullptr, 255);
				Check(DrawTownView(out, true), "draw actual native CPU scene " + name);
				const auto renderer = GetTownViewRendererState();
				Check(!renderer.requestedGpu && !renderer.usedGpu && renderer.cpuRasterizedTriangles > 0,
				    "GPU remains explicitly disabled in " + name);
				const auto bounds = NpcBounds(index, gnScreenWidth, gnViewportHeight);
				Check(bounds.pixels > 0, "requested native NPC has visible picking coverage in " + name);
				SavePng(out.subregionY(0, gnViewportHeight), output / (name + ".png"));
				const std::string pixels = IndexedViewport(out);
				WriteBytes(output / (name + ".indexed-palette.bin"), pixels);
				if (!baselineDirectory.empty()) {
					const auto baseline = ReadBytes(baselineDirectory / (name + ".indexed-palette.bin"));
					Check(baseline.size() == pixels.size(), "baseline viewport and palette byte count agrees");
					size_t changed = 0;
					for (size_t pixel = 0; pixel < pixels.size(); ++pixel)
						changed += pixels[pixel] != baseline[pixel] ? 1 : 0;
					Check(first ? changed > 0 : changed == 0,
					    first ? "first-person image changes with its independent eye height" : "third-person image and palette remain byte-for-byte identical");
					measurements << name << " changed-index/palette-bytes=" << changed << '\n';
				}
				bool guards = true;
				for (int y = gnViewportHeight; y < allocation.h(); ++y)
					for (int x = 0; x < allocation.w(); ++x)
						guards = guards && *allocation.at(x, y) == 255;
				Check(guards && NativeSceneState() == frozen && ActorFrames() == frames
				        && GetLCGEngineState() == originalRandom && SceneIdentity() == sceneBefore,
				    "CPU capture preserves viewport guards, native simulation/collision/pose, RNG and selected geometry/textures");
				measurements << name << " eye=" << camera.eyeHeight << " distance=" << camera.distance
				             << " NPC-pick-bounds=" << bounds.left << ',' << bounds.top << ',' << bounds.right << ',' << bounds.bottom
				             << " visible-NPC-pixels=" << bounds.pixels << '\n';
			}
		}
		Check(measurements.good(), "write actual sprite/pivot/camera/visible-picking measurements");
	}
	Check(NativeSceneState() == originalNative && ActorFrames() == originalFrames && GetLCGEngineState() == originalRandom,
	    "fixture restores initial native player position/map flags and preserves NPC/player frames and native RNG");
	ResetTownViewResources();
}

} // namespace
