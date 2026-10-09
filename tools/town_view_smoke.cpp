// A finite offscreen check using original town assets; it never starts a game or writes a save.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "engine/assets.hpp"
#include "engine/light_tables.hpp"
#include "engine/load_cel.hpp"
#include "engine/load_cl2.hpp"
#include "engine/load_file.hpp"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/town_ground_shadow.hpp"
#include "engine/render/town_gpu.hpp"
#include "engine/render/town_actor.hpp"
#include "engine/render/town_actor_mask.hpp"
#include "engine/render/town_body.hpp"
#include "engine/render/town_model_import.hpp"
#include "engine/render/town_presentation.hpp"
#include "engine/render/town_props.hpp"
#include "engine/render/town_scene.hpp"
#include "engine/render/town_shadow.hpp"
#include "engine/render/town_vegetation.hpp"
#include "engine/render/town_view.hpp"
#include "engine/render/town_view_resolve.hpp"
#include "engine/render/town_volume.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "gamemenu.h"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/tile_properties.hpp"
#include "levels/town.h"
#include "nthread.h"
#include "options.h"
#include "control/control.hpp"
#include "player.h"
#include "quests.h"
#include "towners.h"
#include "utils/paths.h"
#include "utils/surface_to_png.hpp"
#include "utils/ui_fwd.h"
#include "town_editor_snapshot.hpp"
#include "town_editor_map_checks.hpp"
#include "town_cabin_light_gpu_checks.hpp"
#include "town_camera_runtime_entry.hpp"
#include "town_camera_checks.hpp"
#include "town_camera_capture_checks.hpp"
#include "town_architecture_culling_checks.hpp"
#include "town_resident_mesh_checks.hpp"
#include "town_gpu_recovery_checks.hpp"
#include "ingame_menu_visual_checks.hpp"

namespace {
using namespace devilution;

std::vector<std::string> Results;

void Record(const std::string &message)
{
	Results.push_back(message);
	std::cout << message << '\n';
}

void Check(bool condition, const std::string &message)
{
	Results.push_back(std::string(condition ? "PASS " : "FAIL ") + message);
	std::cout << Results.back() << '\n';
	if (!condition)
		throw std::runtime_error(message);
}

void LoadSelectedGameArchive()
{
	const std::filesystem::path data = std::filesystem::absolute(paths::BasePath());
	std::filesystem::path selected;
	for (const char *name : { "DIABDAT.MPQ", "diabdat.mpq", "spawn.mpq", "SPAWN.MPQ" }) {
		const auto candidate = data / name;
		if (std::filesystem::is_regular_file(candidate)) {
			selected = candidate;
			break;
		}
	}
	Check(!selected.empty(), "selected data directory contains DIABDAT.MPQ or spawn.mpq");
	gbIsSpawn = selected.filename().string() == "spawn.mpq" || selected.filename().string() == "SPAWN.MPQ";
	Record("INFO archive mode=" + std::string(gbIsSpawn ? "shareware" : "retail") + " path=" + selected.string()
		+ " bytes=" + std::to_string(std::filesystem::file_size(selected)));
	auto archive = MpqArchive::Open(selected.string().c_str());
	Check(archive.has_value(), archive ? "open explicitly selected game archive" : "open selected archive: " + archive.error());
	MpqArchives.insert_or_assign(MainMpqPriority, std::move(*archive));
	const auto ref = FindAsset("levels\\towndata\\town.cel");
	Check(ref.ok() && ref.archive == &MpqArchives.at(MainMpqPriority), "town CEL comes from the explicitly selected archive");
}

std::vector<uint8_t> ViewportPixels(const Surface &out)
{
	std::vector<uint8_t> pixels(static_cast<size_t>(out.w()) * gnViewportHeight);
	for (int y = 0; y < gnViewportHeight; ++y)
		std::memcpy(pixels.data() + static_cast<size_t>(y) * out.w(), out.at(0, y), out.w());
	return pixels;
}

void Capture(const Surface &out, const std::filesystem::path &path)
{
	OwnedSurface copy(out.w(), gnViewportHeight);
	SDL_SetPaletteColors(copy.surface->format->palette, logical_palette.data(), 0, 256);
	for (int y = 0; y < copy.h(); ++y)
		std::memcpy(copy.at(0, y), out.at(0, y), out.w());
	Check(SDL_SaveBMP(copy.surface, path.string().c_str()) == 0, "capture " + path.filename().string());
}

// Primitive reconstruction for the whole-town atlas only. Fidelity comparisons
// below use DrawNativeTownViewReference and the actual original world backend.
// Indexed atlas only; fidelity captures use the actual DrawGame backend below.
void DrawTownAtlas(const Surface &fullOut, bool wholeTown = false)
{
	const Surface out = wholeTown ? fullOut : fullOut.subregionY(0, gnViewportHeight);
	const Point center = wholeTown ? Point { 46, 46 } : ViewPosition;
	for (int y = 0; y < out.h(); ++y)
		std::memset(out.at(0, y), 0, out.w());
	const std::vector<uint8_t> lighting(static_cast<size_t>(out.pitch()) * out.h(), 0);
	const Lightmap lightmap(out.begin(), lighting, out.pitch(), LightTables, FullyLitLightTable, FullyDarkLightTable);
	const auto position = [&](Point tile) {
		return Point { out.w() / 2 - 32 + 32 * (tile.x - center.x - tile.y + center.y),
			out.h() / 2 + 16 * (tile.x - center.x + tile.y - center.y) };
	};
	std::vector<Point> tiles;
	const int first = 0;
	const int last = 92;
	for (int y = first; y < last; ++y) {
		for (int x = first; x < last; ++x) {
			const Point screen = position({ x, y });
			if (screen.x >= -128 && screen.x < out.w() + 64 && screen.y >= -32 && screen.y < out.h() + 256)
				tiles.push_back({ x, y });
		}
	}
	std::stable_sort(tiles.begin(), tiles.end(), [](Point a, Point b) { return a.x + a.y < b.x + b.y; });
	for (const Point tile : tiles) {
		const auto piece = dPiece[tile.x][tile.y];
		if (piece >= MAXTILES || HasAnyOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile))
			continue;
		const Point base = position(tile);
		for (int i = 0; i < 2; ++i) {
			const LevelCelBlock block = DPieceMicros[piece].mt[i];
			if (block.hasValue())
				RenderTileFrame(out, lightmap, { base.x + 32 * i, base.y }, i == 0 ? TileType::LeftTriangle : TileType::RightTriangle,
					GetDunFrame(pDungeonCels.get(), block.frame()), DunFrameTriangleHeight, MaskType::Solid, FullyLitLightTable);
		}
	}
	for (const Point tile : tiles) {
		const auto piece = dPiece[tile.x][tile.y];
		if (piece >= MAXTILES)
			continue;
		const Point base = position(tile);
		const bool floor = HasNoneOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile);
		for (int i = 0; i < MicroTileLen; ++i) {
			const LevelCelBlock block = DPieceMicros[piece].mt[i];
			if (!block.hasValue())
				continue;
			const Point anchor { base.x + (i & 1) * 32, base.y - (i / 2) * 32 };
			if (floor && i < 2) {
				if (block.type() == TileType::TransparentSquare)
					RenderTileFoliage(out, lightmap, anchor, pDungeonCels.get(), block, FullyLitLightTable);
			} else {
				RenderTile(out, lightmap, anchor, pDungeonCels.get(), block, MaskType::Solid, FullyLitLightTable);
			}
		}
		for (const auto &towner : Towners) {
			if (towner.position == tile && towner.anim)
				ClxDraw(out, base + towner.getRenderingOffset(), towner.currentSprite());
		}
		if (!wholeTown && MyPlayer->position.tile == tile)
			ClxDraw(out, base + MyPlayer->getRenderingOffset(MyPlayer->currentSprite()), MyPlayer->currentSprite());
		const int tree = dSpecial[tile.x][tile.y] - 1;
		if (tree >= 0 && pSpecialCels && static_cast<unsigned>(tree) < pSpecialCels->numSprites())
			ClxDraw(out, base, (*pSpecialCels)[tree]);
	}
}

void SavePng(const Surface &out, const std::filesystem::path &path)
{
	SDL_RWops *file = SDL_RWFromFile(path.string().c_str(), "wb");
	Check(file != nullptr, "open PNG " + path.filename().string());
	// The PNG writer takes the underlying SDL surface, not the Surface view.
	// Materialize viewport slices so diagnostics exclude the untouched UI rows.
	std::unique_ptr<OwnedSurface> cropped;
	if (out.w() != out.surface->w || out.h() != out.surface->h || out.begin() != out.surface->pixels) {
		cropped = std::make_unique<OwnedSurface>(out.w(), out.h());
		SDL_SetPaletteColors(cropped->surface->format->palette, logical_palette.data(), 0, 256);
		for (int y = 0; y < out.h(); ++y)
			std::memcpy(cropped->at(0, y), out.at(0, y), out.w());
	}
	const auto result = WriteSurfaceToFilePng(cropped ? static_cast<const Surface &>(*cropped) : out, file);
	Check(result.has_value(), result ? "capture " + path.filename().string() : result.error());
}

// Archive pixels for auditing painted ground shadows. These are diagnostic
// outputs only: no cleaned ground, inferred transparency, or redrawn artwork.
void ExportNativeGroundPieces(const std::filesystem::path &output)
{
	const auto directory = output / "ground-shadow-sources";
	std::filesystem::create_directories(directory);
	std::set<uint16_t> pieces;
	for (const auto range : { std::pair { 0, 31 }, std::pair { 44, 55 }, std::pair { 253, 260 }, std::pair { 849, 884 } })
		for (int piece = range.first; piece <= range.second; ++piece)
			pieces.insert(static_cast<uint16_t>(piece));
	std::array<uint8_t, 256> identity;
	std::array<SDL_Color, 256> grayscale;
	for (size_t i = 0; i < identity.size(); ++i) {
		identity[i] = static_cast<uint8_t>(i);
		grayscale[i] = { static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i), 255 };
	}
	std::ofstream metadata(directory / "ground-pieces.json");
	metadata << "{\"schema\":1,\"archiveMode\":\"" << (gbIsSpawn ? "shareware" : "retail")
		<< "\",\"source\":\"original MIN/CEL native rendering before ground cleanup\","
		<< "\"opacityMethod\":\"equal output from initial palette indices 0 and 255; opaque black is retained\","
		<< "\"floorSize\":[64,32],\"facadeSize\":[64,256],\"palette\":[";
	for (size_t i = 0; i < logical_palette.size(); ++i) {
		const auto color = logical_palette[i];
		metadata << (i == 0 ? "" : ",") << '[' << static_cast<int>(color.r) << ',' << static_cast<int>(color.g) << ',' << static_cast<int>(color.b) << ']';
	}
	metadata << "],\"pieces\":[\n";
	const auto saveRgba = [&](const Surface &paint, const Surface &coverage, const std::filesystem::path &path) {
		std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> rgba(
			SDL_CreateRGBSurfaceWithFormat(0, paint.w(), paint.h(), 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
		Check(rgba != nullptr, "allocate native ground RGBA export");
		for (int y = 0; y < paint.h(); ++y) {
			auto *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch);
			for (int x = 0; x < paint.w(); ++x) {
				const SDL_Color color = logical_palette[paint[{ x, y }]];
				row[x] = SDL_MapRGBA(rgba->format, color.r, color.g, color.b, paint[{ x, y }] == coverage[{ x, y }] ? 255 : 0);
			}
		}
		SavePng(Surface { rgba.get() }, path);
	};
	bool firstPiece = true;
	for (const uint16_t piece : pieces) {
		const bool solid = HasAnyOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile);
		OwnedSurface floor(64, 32), floorCoverage(64, 32), facade(64, 256), facadeCoverage(64, 256), mask(64, 32);
		SDL_SetPaletteColors(floor.surface->format->palette, logical_palette.data(), 0, 256);
		SDL_SetPaletteColors(mask.surface->format->palette, grayscale.data(), 0, 256);
		SDL_FillRect(floor.surface, nullptr, 0);
		SDL_FillRect(floorCoverage.surface, nullptr, 255);
		SDL_FillRect(facade.surface, nullptr, 0);
		SDL_FillRect(facadeCoverage.surface, nullptr, 255);
		const auto render = [&](const Surface &destination, bool floorOnly) {
			const std::vector<uint8_t> lighting(static_cast<size_t>(destination.pitch()) * destination.h(), 0);
			const Lightmap light(destination.begin(), lighting, destination.pitch(), LightTables, identity.data(), FullyDarkLightTable);
			for (int i = 0; i < (floorOnly ? 2 : std::min<int>(MicroTileLen, 16)); ++i) {
				const LevelCelBlock block = DPieceMicros[piece].mt[i];
				if (!block.hasValue())
					continue;
				const Point anchor { (i & 1) * 32, destination.h() - 1 - (i / 2) * 32 };
				if (floorOnly && !solid) {
					RenderTileFrame(destination, light, anchor, i == 0 ? TileType::LeftTriangle : TileType::RightTriangle,
						GetDunFrame(pDungeonCels.get(), block.frame()), DunFrameTriangleHeight, MaskType::Solid, identity.data());
				} else if (!floorOnly && !solid && i < 2) {
					if (block.type() == TileType::TransparentSquare)
						RenderTileFoliage(destination, light, anchor, pDungeonCels.get(), block, identity.data());
				} else {
					RenderTile(destination, light, anchor, pDungeonCels.get(), block, MaskType::Solid, identity.data());
				}
			}
		};
		render(floor, true);
		render(floorCoverage, true);
		render(facade, false);
		render(facadeCoverage, false);
		int opaquePixels = 0, opaqueBlackPixels = 0;
		for (int y = 0; y < floor.h(); ++y) {
			for (int x = 0; x < floor.w(); ++x) {
				const bool opaque = floor[{ x, y }] == floorCoverage[{ x, y }];
				mask[{ x, y }] = opaque ? 255 : 0;
				opaquePixels += opaque ? 1 : 0;
				opaqueBlackPixels += opaque && floor[{ x, y }] == 0 ? 1 : 0;
			}
		}
		const std::string prefix = "piece-" + std::to_string(piece);
		SavePng(floor, directory / (prefix + "-floor-indexed.png"));
		saveRgba(floor, floorCoverage, directory / (prefix + "-floor.png"));
		SavePng(mask, directory / (prefix + "-floor-mask.png"));
		saveRgba(facade, facadeCoverage, directory / (prefix + "-facade.png"));
		std::vector<Point> sources;
		for (int y = 0; y < MAXDUNY; ++y)
			for (int x = 0; x < MAXDUNX; ++x)
				if (dPiece[x][y] == piece)
					sources.push_back({ x, y });
		int cachedChangedPixels = -1;
		if (!sources.empty()) {
			OwnedSurface diagnostic(256, 256);
			SDL_SetPaletteColors(diagnostic.surface->format->palette, logical_palette.data(), 0, 256);
			Check(DrawTownViewTileDiagnostic(diagnostic, sources.front()), "draw source/cache ground comparison " + prefix);
			SavePng(diagnostic.subregion(160, 0, 64, 32), directory / (prefix + "-cached-floor-indexed.png"));
			cachedChangedPixels = 0;
			int changedOutsideShadow = 0;
			const TownGroundShadowMask *shadowMask = GetTownGroundShadowMask(piece);
			for (int y = 0; y < 32; ++y) {
				for (int x = 0; x < 64; ++x) {
					const bool changed = floor[{ x, y }] != diagnostic[{ x + 160, y }];
					cachedChangedPixels += mask[{ x, y }] != 0 && changed ? 1 : 0;
					const bool selected = shadowMask != nullptr && (shadowMask->rows[y] & (uint64_t { 1 } << x)) != 0;
					changedOutsideShadow += changed && (!selected || mask[{ x, y }] == 0) ? 1 : 0;
				}
			}
			Check(changedOutsideShadow == 0, "ground cleanup preserves every unselected pixel " + prefix);
		}
		// Read the effective texture's own opacity, separately from palette
		// zero. Earlier indexed exports could not detect accidental alpha holes.
		auto effective = GetTownGroundReferenceTexture(piece, false);
		Check(effective.width == 64 && effective.height == 32 && effective.rgba.size() == 2048 * 4,
		    "effective ground RGBA readback has native dimensions " + prefix);
		std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> effectiveSurface(
		    SDL_CreateRGBSurfaceWithFormatFrom(effective.rgba.data(), 64, 32, 32, 64 * 4, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
		Check(effectiveSurface != nullptr, "wrap effective ground RGBA readback " + prefix);
		SavePng(Surface { effectiveSurface.get() }, directory / (prefix + "-cached-floor-rgba.png"));
		bool opacityMatches = true;
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				opacityMatches = opacityMatches && effective.rgba[(static_cast<size_t>(y) * 64 + x) * 4 + 3] == mask[{ x, y }];
		Check(opacityMatches, "effective ground texture preserves original opacity including opaque black " + prefix);
		metadata << (firstPiece ? "" : ",\n") << "{\"piece\":" << piece << ",\"sol\":" << static_cast<unsigned>(SOLData[piece])
			<< ",\"blockedFloor\":" << (solid ? "true" : "false") << ",\"opaquePixels\":" << opaquePixels
			<< ",\"opaqueBlackPixels\":" << opaqueBlackPixels << ",\"cachedChangedOriginalOpaquePixels\":" << cachedChangedPixels
			<< ",\"microframes\":[";
		firstPiece = false;
		for (int i = 0; i < std::min<int>(MicroTileLen, 16); ++i) {
			const auto block = DPieceMicros[piece].mt[i];
			metadata << (i == 0 ? "" : ",") << "{\"slot\":" << i << ",\"frame\":" << block.frame()
				<< ",\"type\":" << static_cast<unsigned>(block.type()) << ",\"present\":" << (block.hasValue() ? "true" : "false") << '}';
		}
		metadata << "],\"sourceTiles\":[";
		for (size_t i = 0; i < sources.size(); ++i)
			metadata << (i == 0 ? "" : ",") << '[' << sources[i].x << ',' << sources[i].y << ']';
		metadata << "],\"indices\":[";
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				metadata << (x == 0 && y == 0 ? "" : ",") << static_cast<unsigned>(floor[{ x, y }]);
		metadata << "],\"opacity\":[";
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				metadata << (x == 0 && y == 0 ? "" : ",") << (mask[{ x, y }] == 0 ? 0 : 1);
		metadata << "]}";
	}
	metadata << "\n]}\n";
	Check(metadata.good(), "export original ground pixels, true opacity, microframes, and current cache changes");
}

void SaveTownShadowStats(const std::filesystem::path &path)
{
	const TownShadowStats &stats = GetTownShadowStats();
	std::ofstream file(path);
	file << "{\"ready\":" << (stats.ready ? "true" : "false") << ",\"resolution\":" << stats.resolution
		<< ",\"buildCount\":" << stats.buildCount << ",\"inputTriangles\":" << stats.inputTriangles
		<< ",\"rasterizedTriangles\":" << stats.rasterizedTriangles << ",\"depthWrites\":" << stats.depthWrites
		<< ",\"coveredTexels\":" << stats.coveredTexels << ",\"depthBytes\":" << stats.depthBytes
		<< ",\"buildMilliseconds\":" << stats.buildMilliseconds << ",\"sceneHash\":\"" << stats.sceneHash << "\"}\n";
	Check(file.good(), "write structural shadow geometry and cache statistics");
}

void SaveTownLightingStats(const std::filesystem::path &path)
{
	const TownViewLightingState state = GetTownViewLightingState();
	const TownShadowDirection shadowLight = GetTownShadowLightDirection();
	const auto &configuration = state.configuration;
	const float length = std::sqrt(configuration.toLight.x * configuration.toLight.x
		+ configuration.toLight.height * configuration.toLight.height + configuration.toLight.z * configuration.toLight.z);
	Check(length > 0 && std::abs(shadowLight.x - configuration.toLight.x / length) < 0.00001F
			&& std::abs(shadowLight.height - configuration.toLight.height / length) < 0.00001F
			&& std::abs(shadowLight.z - configuration.toLight.z / length) < 0.00001F,
		"imported albedo lighting and geometry shadow map use the same world-space sun direction");
	std::ofstream file(path);
	file << "{\"profileLoaded\":" << (state.profileLoaded ? "true" : "false")
		<< ",\"ambientLinearRGB\":[" << configuration.ambient.red << ',' << configuration.ambient.green << ',' << configuration.ambient.blue << ']'
		<< ",\"directionalLinearRGB\":[" << configuration.directional.red << ',' << configuration.directional.green << ',' << configuration.directional.blue << ']'
		<< ",\"toLight\":[" << configuration.toLight.x << ',' << configuration.toLight.height << ',' << configuration.toLight.z << ']'
		<< ",\"directionalIntensity\":" << configuration.directionalIntensity
		<< ",\"importedTextures\":" << state.importedTextures << ",\"albedoColors\":" << state.albedoColors
		<< ",\"albedoTableBytes\":" << state.albedoTableBytes << ",\"lightLevels\":" << state.lightLevels
		<< ",\"cabinInteriors\":" << state.cabinInteriors << ",\"cabinFireEnabled\":" << (state.cabinFireEnabled ? "true" : "false")
		<< ",\"method\":\"source RGB6 albedo -> linear ambient + directional*(NdotL)*(1-shadow) -> sRGB -> game palette\""
		<< ",\"scope\":\"shared imported-base-color lighting; native painted textures retain compatibility shading\"}\n";
	Check(file.good(), "record actual lighting configuration, bounded albedo LUT and source-color scope");
}

void CheckStructuralShadows(const std::filesystem::path &output)
{
	TownSceneModel caster {};
	caster.kind = TownSceneKind::House;
	const std::array<TownSceneVertex, 4> corners { TownSceneVertex { -1, 2, -1, 0, 0 },
		TownSceneVertex { 1, 2, -1, 1, 0 }, TownSceneVertex { 1, 2, 1, 1, 1 }, TownSceneVertex { -1, 2, 1, 0, 1 } };
	caster.triangles.push_back({ { corners[0], corners[1], corners[2] }, TownSceneMaterial::Roof, {}, {} });
	caster.triangles.push_back({ { corners[0], corners[2], corners[3] }, TownSceneMaterial::Roof, {}, {} });
	std::vector<TownSceneModel> scene { caster };
	TownShadowConfig config;
	config.resolution = 128;
	config.toLight = { 1, 1, 0 };
	Check(BuildTownShadowMap(scene, config), "build real light-depth shadow from an elevated square");
	// The ray towards (1,1,0) from ground x=-2 reaches the square at height 2.
	// A ray from x=2 misses it. These expectations are independent of the map.
	Check(SampleTownShadow(-2, 0, 0) > 0.9F && SampleTownShadow(2, 0, 0) < 0.05F,
		"elevated geometry casts ground shadow away from the light");
	Check(SampleTownShadow(0, 3, 0) < 0.05F && SampleTownShadow(0, 2, 0, 0, 1, 0) < 0.05F,
		"receivers above the caster and its own top remain lit");
	const auto firstStats = GetTownShadowStats();
	Check(firstStats.ready && firstStats.inputTriangles == 2 && firstStats.coveredTexels > 0
		&& firstStats.depthBytes == static_cast<size_t>(128 * 128) * sizeof(float), "structural shadow map contains actual triangle depth coverage");
	Check(BuildTownShadowMap(scene, config) && GetTownShadowStats().buildCount == firstStats.buildCount,
		"identical geometry and light reuse the structural shadow cache");
	const auto saveGround = [&](const char *name) {
		OwnedSurface map(256, 256);
		std::array<SDL_Color, 256> palette;
		for (size_t i = 0; i < palette.size(); ++i)
			palette[i] = { static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i), 255 };
		SDL_SetPaletteColors(map.surface->format->palette, palette.data(), 0, 256);
		for (int y = 0; y < map.h(); ++y)
			for (int x = 0; x < map.w(); ++x)
				map[{ x, y }] = static_cast<uint8_t>(255 * (1 - SampleTownShadow(-5 + (x + 0.5F) * 10 / map.w(), 0, -5 + (y + 0.5F) * 10 / map.h())));
		SavePng(map, output / name);
	};
	saveGround("shadow-synthetic-light-positive-x.png");
	config.toLight.x = -1;
	Check(BuildTownShadowMap(scene, config) && SampleTownShadow(2, 0, 0) > 0.9F && SampleTownShadow(-2, 0, 0) < 0.05F,
		"changing light direction moves the ground shadow to the opposite side");
	saveGround("shadow-synthetic-light-negative-x.png");
	config.toLight.x = 1;
	for (auto &triangle : scene[0].triangles)
		for (auto &vertex : triangle.vertices)
			vertex.x += 4;
	Check(BuildTownShadowMap(scene, config) && SampleTownShadow(2, 0, 0) > 0.9F && SampleTownShadow(-2, 0, 0) < 0.05F,
		"moving actual caster geometry moves its shadow without stale coverage");
	scene = { caster };
	for (auto &triangle : scene[0].triangles)
		for (auto &vertex : triangle.vertices)
			vertex.height = 2 + 0.5F * vertex.x;
	Check(BuildTownShadowMap(scene, config) && SampleTownShadow(0, 2, 0, -0.5F, 1, 0) < 0.05F,
		"sloped roof receiver plane prevents filtered self-shadow acne");
	config.resolution = 127;
	Check(!BuildTownShadowMap(scene, config) && !GetTownShadowStats().ready && SampleTownShadow(2, 0, 0) == 0,
		"invalid shadow configuration clears stale geometry shadows");
	config.resolution = 128;
	scene[0].triangles[0].vertices[0].height = std::numeric_limits<float>::quiet_NaN();
	Check(!BuildTownShadowMap(scene, config) && SampleTownShadow(0, 0, 0) == 0, "nonfinite shadow geometry is rejected and remains lit");
	Check(BuildTownShadowMap({}, config) && SampleTownShadow(0, 0, 0) == 0, "empty architecture has no invented structural shadow");
	ClearTownShadowMap();
	Check(BuildTownShadowMap(GetTownScene()), "restore actual Tristram structural shadow map after synthetic checks");
	SaveTownShadowStats(output / "shadow-map.json");
}

void ExportNativeHouseArtwork(Point minTile, Point maxTile, const std::filesystem::path &path)
{
	constexpr int CanvasSize = 1024;
	constexpr Point CanvasOrigin { 512, 768 };
	OwnedSurface paint(CanvasSize, CanvasSize);
	OwnedSurface coverage(CanvasSize, CanvasSize);
	SDL_FillRect(paint.surface, nullptr, 0);
	SDL_FillRect(coverage.surface, nullptr, 255);
	const std::vector<uint8_t> lighting(static_cast<size_t>(paint.pitch()) * paint.h(), 0);
	const Lightmap paintLight(paint.begin(), lighting, paint.pitch(), LightTables, FullyLitLightTable, FullyDarkLightTable);
	const Lightmap coverageLight(coverage.begin(), lighting, coverage.pitch(), LightTables, FullyLitLightTable, FullyDarkLightTable);
	std::vector<Point> tiles;
	for (int y = minTile.y; y <= maxTile.y; ++y)
		for (int x = minTile.x; x <= maxTile.x; ++x)
			tiles.push_back({ x, y });
	std::stable_sort(tiles.begin(), tiles.end(), [](Point a, Point b) { return a.x + a.y < b.x + b.y; });
	for (Point tile : tiles) {
		const auto piece = dPiece[tile.x][tile.y];
		if (piece >= MAXTILES)
			continue;
		const bool floor = HasNoneOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile);
		const Point base { CanvasOrigin.x - 32 + 32 * (tile.x - minTile.x - tile.y + minTile.y),
			CanvasOrigin.y + 16 * (tile.x - minTile.x + tile.y - minTile.y) };
		for (int i = 0; i < MicroTileLen; ++i) {
			const auto block = DPieceMicros[piece].mt[i];
			if (!block.hasValue())
				continue;
			const Point anchor { base.x + (i & 1) * 32, base.y - (i / 2) * 32 };
			// Only the painted building layer: no ground triangles, dSpecial trees,
			// actors, inferred geometry, or redrawn pixels are part of this export.
			if (floor && i < 2) {
				if (block.type() == TileType::TransparentSquare) {
					RenderTileFoliage(paint, paintLight, anchor, pDungeonCels.get(), block, FullyLitLightTable);
					RenderTileFoliage(coverage, coverageLight, anchor, pDungeonCels.get(), block, FullyLitLightTable);
				}
			} else {
				RenderTile(paint, paintLight, anchor, pDungeonCels.get(), block, MaskType::Solid, FullyLitLightTable);
				RenderTile(coverage, coverageLight, anchor, pDungeonCels.get(), block, MaskType::Solid, FullyLitLightTable);
			}
		}
	}
	int left = CanvasSize, top = CanvasSize, right = -1, bottom = -1;
	for (int y = 0; y < CanvasSize; ++y) {
		for (int x = 0; x < CanvasSize; ++x) {
			if (paint[{ x, y }] != coverage[{ x, y }])
				continue;
			left = std::min(left, x);
			top = std::min(top, y);
			right = std::max(right, x);
			bottom = std::max(bottom, y);
		}
	}
	Check(left > 0 && top > 0 && right < CanvasSize - 1 && bottom < CanvasSize - 1 && right >= left,
		"full native house composition fits without clipping " + path.stem().string());
	constexpr int Margin = 8;
	left -= Margin;
	top -= Margin;
	right += Margin;
	bottom += Margin;
	const int width = right - left + 1;
	const int height = bottom - top + 1;
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> rgba(
		SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
	Check(rgba != nullptr, "allocate native house RGBA export");
	for (int y = 0; y < height; ++y) {
		auto *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch);
		for (int x = 0; x < width; ++x) {
			const Point source { left + x, top + y };
			const SDL_Color color = logical_palette[paint[source]];
			const uint8_t alpha = paint[source] == coverage[source] ? 255 : 0;
			row[x] = SDL_MapRGBA(rgba->format, color.r, color.g, color.b, alpha);
		}
	}
	SavePng(Surface { rgba.get() }, path);
	std::ofstream metadata(path.parent_path() / (path.stem().string() + ".json"));
	metadata << "{\"minTile\":[" << minTile.x << ',' << minTile.y << "],\"maxTile\":[" << maxTile.x << ',' << maxTile.y
		<< "],\"referenceTile\":[" << minTile.x << ',' << minTile.y << "],\"pixelOrigin\":[" << left - CanvasOrigin.x << ',' << top - CanvasOrigin.y
		<< "],\"pixelSize\":[" << width << ',' << height << "],\"projection\":\"32*(x-z),16*(x+z)-32*height\",\"nativePixelsOnly\":true}\n";
	Check(metadata.good(), "write native house composition coordinates");
}

void DrawMapLabel(const Surface &out, Point origin, std::string_view value)
{
	static constexpr std::array<std::string_view, 10> Glyphs {
		"111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
		"111100111001111", "111100111101111", "111001010010010", "111101111101111", "111101111001111"
	};
	for (const char character : value) {
		const std::string_view glyph = character >= '0' && character <= '9' ? Glyphs[character - '0']
			: character == 'X' ? "101101010101101" : character == 'Y' ? "101101010010010" : "000000000000000";
		for (int row = 0; row < 5; ++row) {
			for (int column = 0; column < 3; ++column) {
				if (glyph[row * 3 + column] == '1') {
					for (int py = 0; py < 2; ++py)
						for (int px = 0; px < 2; ++px)
							out.SetPixel({ origin.x + column * 2 + px, origin.y + row * 2 + py }, 7);
				}
			}
		}
		origin.x += 8;
	}
}

void ExportTownMapping(const std::filesystem::path &output)
{
	std::ofstream json(output / "town-map.json");
	std::ofstream csv(output / "town-tiles.csv");
	Check(json.good() && csv.good(), "open native town mapping exports");
	json << "{\n\"schema\":1,\"mode\":" << std::quoted(gbIsSpawn ? "shareware" : "retail")
		<< ",\"bounds\":{\"min\":[0,0],\"maxExclusive\":[92,92]},\"playableBounds\":{\"min\":[10,10],\"maxExclusive\":[84,84]},"
		<< "\n\"isometricAtlas\":{\"file\":\"town-atlas-isometric.png\",\"width\":6000,\"height\":3600,\"worldCenter\":[46,46],\"tileLeftX\":\"2968+32*(x-y)\",\"tileBottomY\":\"1800+16*(x+y-92)\"},"
		<< "\n\"topDownAtlas\":{\"file\":\"town-atlas-topdown.png\",\"origin\":[64,64],\"cellSize\":16,\"legend\":{\"2\":\"walkable\",\"3\":\"solid\",\"4\":\"blocksLight\",\"5\":\"tree\",\"8\":\"NPC marker, label is towner index\"}},\n\"tiles\":[\n";
	csv << "x,y,piece,sol,special,monster,player\n";
	for (int y = 0; y < 92; ++y) {
		for (int x = 0; x < 92; ++x) {
			const auto piece = dPiece[x][y];
			const unsigned sol = piece < MAXTILES ? static_cast<unsigned>(SOLData[piece]) : 0;
			json << (x == 0 && y == 0 ? "" : ",\n") << "{\"x\":" << x << ",\"y\":" << y << ",\"piece\":" << piece
				<< ",\"sol\":" << sol << ",\"special\":" << static_cast<int>(dSpecial[x][y]) << '}';
			csv << x << ',' << y << ',' << piece << ',' << sol << ',' << static_cast<int>(dSpecial[x][y])
				<< ',' << dMonster[x][y] << ',' << static_cast<int>(dPlayer[x][y]) << '\n';
		}
	}
	json << "\n],\"towners\":[\n";
	for (size_t i = 0; i < Towners.size(); ++i) {
		const Towner &towner = Towners[i];
		const auto shortName = TownerShortNames.find(towner._ttype);
		json << (i == 0 ? "" : ",\n") << "{\"index\":" << i << ",\"type\":" << static_cast<unsigned>(towner._ttype)
			<< ",\"id\":" << std::quoted(shortName == TownerShortNames.end() ? "" : shortName->second)
			<< ",\"name\":" << std::quoted(std::string(towner.name)) << ",\"x\":" << towner.position.x << ",\"y\":" << towner.position.y << '}';
	}
	json << "\n]}\n";
	json.close();
	csv.close();
	Check(!json.fail() && !csv.fail(), "export town-map.json and town-tiles.csv (8464 original cells)");

	OwnedSurface atlas(6000, 3600);
	SDL_SetPaletteColors(atlas.surface->format->palette, logical_palette.data(), 0, 256);
	DrawTownAtlas(atlas, true);
	SavePng(atlas, output / "town-atlas-isometric.png");

	OwnedSurface topDown(1600, 1600);
	const std::array<SDL_Color, 9> colors { SDL_Color { 0, 0, 0, 255 }, SDL_Color { 12, 16, 22, 255 },
		SDL_Color { 48, 57, 62, 255 }, SDL_Color { 145, 111, 73, 255 }, SDL_Color { 211, 126, 59, 255 },
		SDL_Color { 56, 127, 77, 255 }, SDL_Color { 20, 24, 29, 255 }, SDL_Color { 232, 236, 242, 255 },
		SDL_Color { 214, 73, 89, 255 } };
	SDL_SetPaletteColors(topDown.surface->format->palette, colors.data(), 0, static_cast<int>(colors.size()));
	SDL_FillRect(topDown.surface, nullptr, 1);
	for (int y = 0; y < 92; ++y) {
		for (int x = 0; x < 92; ++x) {
			const auto sol = SOLData[dPiece[x][y]];
			const uint8_t color = dSpecial[x][y] != 0 ? 5 : HasAnyOf(sol, TileProperties::BlockLight) ? 4 : HasAnyOf(sol, TileProperties::Solid) ? 3 : 2;
			SDL_Rect cell { 64 + x * 16, 64 + y * 16, 15, 15 };
			SDL_FillRect(topDown.surface, &cell, color);
		}
	}
	for (int tick = 0; tick < 92; tick += 5) {
		DrawMapLabel(topDown, { 64 + tick * 16, 40 }, std::to_string(tick));
		DrawMapLabel(topDown, { 35, 64 + tick * 16 }, std::to_string(tick));
	}
	DrawMapLabel(topDown, { 1548, 40 }, "X");
	DrawMapLabel(topDown, { 35, 1548 }, "Y");
	for (size_t i = 0; i < Towners.size(); ++i) {
		const Point tile = Towners[i].position;
		SDL_Rect marker { 64 + tile.x * 16 + 3, 64 + tile.y * 16 + 3, 10, 10 };
		SDL_FillRect(topDown.surface, &marker, 8);
		DrawMapLabel(topDown, { marker.x + 12, marker.y - 5 }, std::to_string(i));
	}
	SavePng(topDown, output / "town-atlas-topdown.png");
}

std::string NativeSceneState()
{
	std::string state;
	const auto append = [&](const auto &buffer) {
		state.append(reinterpret_cast<const char *>(&buffer), sizeof(buffer));
	};
	append(dPiece);
	append(SOLData);
	append(dSpecial);
	append(dMonster);
	append(dPlayer);
	append(dFlags);
	std::ostringstream actors;
	actors << ViewPosition.x << ',' << ViewPosition.y << ';';
	for (const Player &player : Players) {
		for (const Point point : { Point { player.position.tile }, Point { player.position.future }, Point { player.position.old },
			Point { player.position.last }, Point { player.position.temp } })
			actors << point.x << ',' << point.y << ';';
		actors << player._pHitPoints << ',' << static_cast<int>(player._pmode) << ',' << static_cast<int>(player._pdir) << ';';
	}
	for (const Towner &towner : Towners)
		actors << towner.position.x << ',' << towner.position.y << ';';
	state += actors.str();
	return state;
}

std::string SceneGeometryState(const std::vector<TownSceneModel> &scene)
{
	std::ostringstream state;
	state << std::setprecision(std::numeric_limits<float>::max_digits10);
	for (const TownSceneModel &model : scene) {
		state << static_cast<int>(model.kind) << ':' << model.minTile.x << ',' << model.minTile.y
			<< ',' << model.maxTile.x << ',' << model.maxTile.y << ';'
			<< model.physicalBounds.minX << ',' << model.physicalBounds.minZ << ',' << model.physicalBounds.maxX << ','
			<< model.physicalBounds.maxZ << ',' << model.physicalBounds.wallHeight << ',' << model.physicalBounds.closed << ';'
			<< model.nativeArtwork.enabled << ',' << model.nativeArtwork.referenceTile.x << ',' << model.nativeArtwork.referenceTile.y << ','
			<< model.nativeArtwork.pixelOrigin.x << ',' << model.nativeArtwork.pixelOrigin.y << ','
			<< model.nativeArtwork.pixelSize.x << ',' << model.nativeArtwork.pixelSize.y << ','
			<< model.nativeArtwork.minTile.x << ',' << model.nativeArtwork.minTile.y << ','
			<< model.nativeArtwork.maxTile.x << ',' << model.nativeArtwork.maxTile.y << ','
			<< model.nativeArtwork.fringeMinPiece << ',' << model.nativeArtwork.fringeMaxPiece << ';';
		for (const TownSceneTriangle &triangle : model.triangles) {
			state << static_cast<int>(triangle.material) << ':' << triangle.sourceTile.x << ',' << triangle.sourceTile.y
				<< ',' << triangle.pickTile.x << ',' << triangle.pickTile.y << ':' << triangle.nativeProjection << ':'
				<< triangle.normal.x << ',' << triangle.normal.height << ',' << triangle.normal.z << ','
				<< static_cast<int>(triangle.surfaceRole) << ':';
			for (const TownSceneVertex &vertex : triangle.vertices)
				state << vertex.x << ',' << vertex.height << ',' << vertex.z << ',' << vertex.u << ',' << vertex.v << ';';
		}
		state << '\n';
	}
	return state.str();
}

void CheckTownModelImporter()
{
	const std::string native = NativeSceneState();
	const auto &scene = GetTownScene();
	const auto cabin = std::find_if(scene.begin(), scene.end(), [](const TownSceneModel &model) {
		return model.kind == TownSceneKind::Cabin && model.minTile == Point { 70, 66 };
	});
	Check(cabin != scene.end(), "model importer regression uses the real east cabin metadata");
	TownSceneModel model = *cabin; // Never replace a model in the actual scene.
	const auto metadata = [](const TownSceneModel &source) {
		TownSceneModel withoutTriangles = source;
		withoutTriangles.triangles.clear();
		std::string state = SceneGeometryState({ withoutTriangles });
		if (!source.materialPatches.empty())
			state.append(reinterpret_cast<const char *>(source.materialPatches.data()),
				source.materialPatches.size() * sizeof(TownSceneMaterialPatch));
		return state;
	};
	const std::string originalMetadata = metadata(model);
	std::vector<std::byte> data;
	for (char c : std::string("D3DMESH1"))
		data.push_back(static_cast<std::byte>(c));
	const auto appendWord = [&](uint32_t value) {
		for (int shift = 0; shift < 32; shift += 8)
			data.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
	};
	appendWord(1); // One triangle, two RGB pixels, no implicit struct padding.
	appendWord(2);
	appendWord(1);
	for (float value : { 0.0F, 0.0F, 0.0F, 0.0F, 0.0F,
		1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F })
		appendWord(std::bit_cast<uint32_t>(value));
	for (uint8_t color : std::array<uint8_t, 6> { 10, 20, 30, 40, 50, 60 })
		data.push_back(static_cast<std::byte>(color));
	model.cabinInterior = std::make_shared<TownCabinInterior>();
	Check(data.size() == 20 + 15 * sizeof(float) + 6 && ParseTownModelOverride(model, data),
		"model importer accepts exact D3DMESH1 geometry and RGB file bytes");
	Check(!model.cabinInterior, "successful model replacement releases its previous room adjunct");
	const auto &triangle = model.triangles.front();
	Check(model.externalModel && model.triangles.size() == 1 && model.importedTexture
			&& model.importedTexture->width == 2 && model.importedTexture->height == 1
			&& model.importedTexture->rgb == std::vector<uint8_t> { 10, 20, 30, 40, 50, 60 }
			&& triangle.vertices[0].x == 70 && triangle.vertices[0].z == 66
			&& triangle.vertices[1].x == 71 && triangle.vertices[1].u == 1
			&& triangle.vertices[2].height == 1 && triangle.vertices[2].v == 1
			&& triangle.normal.z == 1 && !triangle.nativeProjection
			&& triangle.surfaceDetail == TownSceneSurfaceDetail::None,
		"imported RGB, authored UVs, world anchor and computed normal are preserved");
	Check(metadata(model) == originalMetadata,
		"imported visual geometry retains native physical bounds, artwork and material metadata");
	const std::string originalGeometry = SceneGeometryState({ model });
	const auto originalTexture = model.importedTexture;
	model.cabinInterior = std::make_shared<TownCabinInterior>();
	const auto originalInterior = model.cabinInterior;
	const auto unchanged = [&] {
		return model.externalModel && model.importedTexture == originalTexture && model.cabinInterior == originalInterior
		    && SceneGeometryState({ model }) == originalGeometry && metadata(model) == originalMetadata
		    && model.importedTexture->rgb == std::vector<uint8_t> { 10, 20, 30, 40, 50, 60 }
		    && model.triangles.front().surfaceDetail == TownSceneSurfaceDetail::None;
	};
	std::vector<std::vector<std::byte>> invalid;
	invalid.push_back(data);
	invalid.back()[0] = std::byte { 'X' };
	invalid.push_back(data);
	invalid.back().pop_back();
	invalid.push_back(data);
	invalid.back().push_back(std::byte { 0 });
	const auto replaceFloat = [](std::vector<std::byte> &bytes, size_t offset, float value) {
		const uint32_t word = std::bit_cast<uint32_t>(value);
		for (int i = 0; i < 4; ++i)
			bytes[offset + i] = static_cast<std::byte>((word >> (i * 8)) & 0xFF);
	};
	invalid.push_back(data);
	replaceFloat(invalid.back(), 20, std::numeric_limits<float>::quiet_NaN());
	invalid.push_back(data);
	replaceFloat(invalid.back(), 32, 1.25F); // First vertex's U is unsupported wrapping.
	bool atomicRejection = true;
	for (const auto &bytes : invalid)
		atomicRejection = !ParseTownModelOverride(model, bytes) && unchanged() && atomicRejection;
	Check(atomicRejection,
		"malformed, truncated, trailing, NaN and invalid-UV models are rejected atomically");
	Check(!LoadTownModelOverride(model, "d3d-models/__smoke_missing_model_5e839da.d3d") && unchanged(),
		"a missing optional model asset leaves the current model and RGB texture intact");
	Check(NativeSceneState() == native,
		"model decoding and optional asset lookup preserve native map, collision and actor state");
}

void CheckTownSceneMeshes()
{
	const std::string native = NativeSceneState();
	const auto &scene = GetTownScene();
	Check(!scene.empty(), "native town has architectural meshes");
	bool validBounds = true;
	bool finiteVertices = true;
	bool nondegenerate = true;
	bool validSources = true;
	bool validArtwork = true;
	bool validPhysicalBounds = true;
	bool closedFootprints = true;
	bool closedWalkPaths = true;
	size_t triangleCount = 0;
	const auto insideTown = [](Point point) { return point.x >= 0 && point.y >= 0 && point.x < 92 && point.y < 92; };
	for (const TownSceneModel &model : scene) {
		validBounds = validBounds && insideTown(model.minTile) && insideTown(model.maxTile)
			&& model.minTile.x <= model.maxTile.x && model.minTile.y <= model.maxTile.y && !model.triangles.empty();
		validArtwork = validArtwork && model.nativeArtwork.enabled && insideTown(model.nativeArtwork.minTile)
			&& insideTown(model.nativeArtwork.maxTile) && model.nativeArtwork.pixelSize.x > 0 && model.nativeArtwork.pixelSize.y > 0;
		const TownScenePhysicalBounds &body = model.physicalBounds;
		validPhysicalBounds = validPhysicalBounds && std::isfinite(body.minX) && std::isfinite(body.maxX)
			&& std::isfinite(body.minZ) && std::isfinite(body.maxZ) && std::isfinite(body.wallHeight)
			&& body.minX < body.maxX && body.minZ < body.maxZ && body.wallHeight > 0;
		int bodyBlocked = 0;
		int bodyWalkable = 0;
		int bodyActorWalkable = 0;
		int crossingSteps = 0;
		std::string walkableCoordinates;
		std::string actorCoordinates;
		const auto inside = [&](float x, float z) {
			return x > body.minX + 0.001F && x < body.maxX - 0.001F && z > body.minZ + 0.001F && z < body.maxZ - 0.001F;
		};
		for (int y = 0; y < 92; ++y) {
			for (int x = 0; x < 92; ++x) {
				const float groundX = static_cast<float>(x) - 0.5F;
				const float groundZ = static_cast<float>(y) - 0.5F;
				const Point point { x, y };
				const bool walkable = IsTileNotSolid(point);
				if (inside(groundX, groundZ)) {
					if (walkable) {
						++bodyWalkable;
						walkableCoordinates += " (" + std::to_string(x) + "," + std::to_string(y) + ")";
					} else {
						++bodyBlocked;
					}
				}
				if (walkable && inside(static_cast<float>(x), static_cast<float>(y))) {
					++bodyActorWalkable;
					actorCoordinates += " (" + std::to_string(x) + "," + std::to_string(y) + ")";
				}
				if (!body.closed || !walkable || x < 10 || y < 10 || x >= 84 || y >= 84
					|| x < body.minX - 2 || x > body.maxX + 2 || y < body.minZ - 2 || y > body.maxZ + 2)
					continue;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						const Point end { x + dx, y + dy };
						if ((dx == 0 && dy == 0) || end.x < 10 || end.y < 10 || end.x >= 84 || end.y >= 84
							|| !IsTileNotSolid(end) || !CanStep(point, end))
							continue;
						for (const float progress : { 0.25F, 0.5F, 0.75F }) {
							if (inside(x + dx * progress, y + dy * progress)) {
								++crossingSteps;
								break;
							}
						}
					}
				}
			}
		}
		Record("INFO physical footprint kind=" + std::to_string(static_cast<int>(model.kind)) + " source="
			+ std::to_string(model.minTile.x) + "," + std::to_string(model.minTile.y) + " blocked cells=" + std::to_string(bodyBlocked)
			+ " walkable centers=" + std::to_string(bodyWalkable) + walkableCoordinates
			+ " walkable actor feet=" + std::to_string(bodyActorWalkable) + actorCoordinates
			+ " crossing native steps=" + std::to_string(crossingSteps));
		if (body.closed) {
			closedFootprints = closedFootprints && bodyWalkable == 0 && bodyActorWalkable == 0 && bodyBlocked > 0;
			closedWalkPaths = closedWalkPaths && crossingSteps == 0;
		}
		for (const TownSceneTriangle &triangle : model.triangles) {
			++triangleCount;
			const bool sourceValid = insideTown(triangle.sourceTile) && insideTown(triangle.pickTile)
				&& dPiece[triangle.sourceTile.x][triangle.sourceTile.y] < MAXTILES;
			validSources = validSources && sourceValid;
			if (!sourceValid)
				Record("INFO invalid mesh source kind=" + std::to_string(static_cast<int>(model.kind)) + " tile="
					+ std::to_string(triangle.sourceTile.x) + "," + std::to_string(triangle.sourceTile.y));
			for (const TownSceneVertex &vertex : triangle.vertices) {
				finiteVertices = finiteVertices && std::isfinite(vertex.x) && std::isfinite(vertex.height)
					&& std::isfinite(vertex.z) && std::isfinite(vertex.u) && std::isfinite(vertex.v);
				// Eaves and buttresses may extend into the adjoining cell, but an object
				// must stay within its declared painted footprint and that small margin.
				const bool vertexBounds = vertex.x >= model.minTile.x - 1 && vertex.x <= model.maxTile.x + 1
					&& vertex.z >= model.minTile.y - 1 && vertex.z <= model.maxTile.y + 1 && vertex.height >= 0;
				validBounds = validBounds && vertexBounds;
				if (!vertexBounds)
					Record("INFO invalid mesh vertex kind=" + std::to_string(static_cast<int>(model.kind)) + " origin="
						+ std::to_string(model.minTile.x) + "," + std::to_string(model.minTile.y) + " xyz="
						+ std::to_string(vertex.x) + "," + std::to_string(vertex.height) + "," + std::to_string(vertex.z));
			}
			const auto &a = triangle.vertices[0];
			const auto &b = triangle.vertices[1];
			const auto &c = triangle.vertices[2];
			const double ux = static_cast<double>(b.x) - a.x;
			const double uy = static_cast<double>(b.height) - a.height;
			const double uz = static_cast<double>(b.z) - a.z;
			const double vx = static_cast<double>(c.x) - a.x;
			const double vy = static_cast<double>(c.height) - a.height;
			const double vz = static_cast<double>(c.z) - a.z;
			const double nx = uy * vz - uz * vy;
			const double ny = uz * vx - ux * vz;
			const double nz = ux * vy - uy * vx;
			nondegenerate = nondegenerate && nx * nx + ny * ny + nz * nz > 1.0e-12;
		}
	}
	Check(finiteVertices && nondegenerate, "architectural triangles have finite coordinates and nonzero area");
	Check(validBounds && validSources, "mesh geometry stays in its town footprint and references real native tiles");
	Check(validArtwork, "every architectural model declares its original native CEL composition");
	Check(validPhysicalBounds, "every architectural wall body has finite positive physical bounds");
	Check(closedFootprints, "every closed wall body avoids both native walkable actor feet and ground cell centers");
	Check(closedWalkPaths, "native allowed walking segments around closed houses do not cross their wall bodies");
	Check(!TownSceneReplacesTile({ 25, 29 }) && !TownSceneReplacesTile({ 25, 30 }), "cathedral entrance trigger tiles remain unsuppressed");
	const std::string geometry = SceneGeometryState(scene);
	ResetTownScene();
	Check(SceneGeometryState(GetTownScene()) == geometry, "scene cache reset reproduces identical mesh geometry");
	Check(!TownSceneReplacesTile({ 25, 29 }) && !TownSceneReplacesTile({ 25, 30 }), "scene reset preserves native cathedral entrance triggers");
	Check(NativeSceneState() == native, "scene construction, suppression queries, and reset preserve native map and actor state");
	Record("INFO architectural models=" + std::to_string(GetTownScene().size()) + " triangles=" + std::to_string(triangleCount));
}

std::string VegetationGeometryState()
{
	std::ostringstream state;
	for (const auto &group : GetTownVegetationGroups()) {
		state << static_cast<int>(group.family) << ':' << group.referenceFootpoint.x << ',' << group.referenceFootpoint.y << ':'
			<< group.minTile.x << ',' << group.minTile.y << ',' << group.maxTile.x << ',' << group.maxTile.y << ':' << group.foliage << ';';
		for (const auto &tile : group.sourceTiles)
			state << tile.x << ',' << tile.y << ';';
		state << '|';
		for (const auto &tile : group.specialTiles)
			state << tile.x << ',' << tile.y << ';';
		state << '\n';
	}
	return state.str();
}

void CheckNativeVegetationGroups()
{
	const std::string original = NativeSceneState();
	const auto &groups = GetTownVegetationGroups();
	Check(!groups.empty(), "town vegetation groups assemble original MIN and special fragments");
	std::array<int, MAXDUNX * MAXDUNY> specialMembership {};
	std::array<bool, MAXDUNX * MAXDUNY> sourceMembership {};
	bool validSources = true;
	bool validSpecials = true;
	size_t sourceCount = 0;
	size_t specialCount = 0;
	const auto inside = [](Point tile) { return tile.x >= 0 && tile.y >= 0 && tile.x < MAXDUNX && tile.y < MAXDUNY; };
	for (const auto &group : groups) {
		validSources = validSources && !group.sourceTiles.empty() && inside(group.referenceFootpoint)
			&& inside(group.minTile) && inside(group.maxTile) && group.minTile.x <= group.maxTile.x && group.minTile.y <= group.maxTile.y;
		std::set<std::pair<int, int>> uniqueSources;
		for (Point tile : group.sourceTiles) {
			++sourceCount;
			if (!inside(tile)) {
				validSources = false;
				continue;
			}
			validSources = validSources && uniqueSources.emplace(tile.x, tile.y).second && dPiece[tile.x][tile.y] < MAXTILES
				&& tile.x >= group.minTile.x && tile.x <= group.maxTile.x && tile.y >= group.minTile.y && tile.y <= group.maxTile.y
				&& TownVegetationReplacesTile(tile);
			sourceMembership[static_cast<size_t>(tile.y) * MAXDUNX + tile.x] = true;
		}
		for (Point tile : group.specialTiles) {
			++specialCount;
			if (!inside(tile)) {
				validSpecials = false;
				continue;
			}
			validSpecials = validSpecials && dSpecial[tile.x][tile.y] != 0 && uniqueSources.contains({ tile.x, tile.y });
			++specialMembership[static_cast<size_t>(tile.y) * MAXDUNX + tile.x];
		}
	}
	bool uniqueSpecialObjects = true;
	bool exactSuppression = true;
	for (int y = 0; y < MAXDUNY; ++y) {
		for (int x = 0; x < MAXDUNX; ++x) {
			const size_t index = static_cast<size_t>(y) * MAXDUNX + x;
			uniqueSpecialObjects = uniqueSpecialObjects && specialMembership[index] == (dSpecial[x][y] != 0 ? 1 : 0);
			exactSuppression = exactSuppression && TownVegetationReplacesTile({ x, y }) == sourceMembership[index];
		}
	}
	Check(validSources && validSpecials, "vegetation source cells are unique within each object, bounded, and backed by actual town data");
	Check(uniqueSpecialObjects, "each original dSpecial belongs to exactly one complete vegetation object");
	Check(exactSuppression, "vegetation scenery suppression includes exactly its native source cells");
	if (dPiece[79][69] == 130)
		Check(TownVegetationReplacesTile({ 79, 69 }), "original solid tree trunk130 beside east cabin belongs to its complete tree");
	const std::string geometry = VegetationGeometryState();
	ResetTownVegetation();
	Check(VegetationGeometryState() == geometry, "vegetation reload reproduces identical native object grouping");
	Check(NativeSceneState() == original, "vegetation grouping, membership queries and reset preserve map and actor state");
	Record("INFO vegetation objects=" + std::to_string(GetTownVegetationGroups().size()) + " MIN source cells="
		+ std::to_string(sourceCount) + " special fragments=" + std::to_string(specialCount));
}

std::string PropGroupingState()
{
	std::ostringstream state;
	for (const auto &group : GetTownPropGroups()) {
		state << group.referenceFootpoint.x << ',' << group.referenceFootpoint.y << ':'
			<< group.minTile.x << ',' << group.minTile.y << ',' << group.maxTile.x << ',' << group.maxTile.y
			<< ':' << TownPropHiddenByArchitecture(group) << ';';
		for (Point tile : group.sourceTiles)
			state << tile.x << ',' << tile.y << ';';
		state << '\n';
	}
	return state.str();
}

void CheckNativePropGroups()
{
	const std::string original = NativeSceneState();
	const auto &groups = GetTownPropGroups();
	Check(!groups.empty(), "native rock fragments form complete original objects");
	std::array<int, MAXDUNX * MAXDUNY> membership;
	membership.fill(-1);
	const auto inside = [](Point tile) { return tile.x >= 0 && tile.y >= 0 && tile.x < MAXDUNX && tile.y < MAXDUNY; };
	bool valid = true;
	size_t sourceCount = 0;
	for (size_t index = 0; index < groups.size(); ++index) {
		const auto &group = groups[index];
		valid = valid && !group.sourceTiles.empty() && inside(group.referenceFootpoint) && inside(group.minTile) && inside(group.maxTile);
		for (Point tile : group.sourceTiles) {
			++sourceCount;
			if (!inside(tile)) {
				valid = false;
				continue;
			}
			const size_t cell = static_cast<size_t>(tile.y) * MAXDUNX + tile.x;
			valid = valid && membership[cell] == -1 && tile.x >= group.minTile.x && tile.x <= group.maxTile.x
				&& tile.y >= group.minTile.y && tile.y <= group.maxTile.y && dPiece[tile.x][tile.y] >= 219
				&& dPiece[tile.x][tile.y] <= 233 && dSpecial[tile.x][tile.y] == 0 && TownPropReplacesTile(tile);
			membership[cell] = static_cast<int>(index);
		}
	}
	Check(valid, "rock groups contain unique bounded native MIN fragments without replacing special sprites");
	bool exactSuppression = true;
	bool largeRocksGrouped = true;
	size_t largeCount = 0;
	for (int y = 0; y < MAXDUNY; ++y) {
		for (int x = 0; x < MAXDUNX; ++x) {
			const int owner = membership[static_cast<size_t>(y) * MAXDUNX + x];
			exactSuppression = exactSuppression && TownPropReplacesTile({ x, y }) == (owner >= 0);
			// This is the observed four-column defect: the real contiguous native
			// 230..233 stencil must become one boulder, including its painted edges.
			if (x + 1 >= MAXDUNX || y + 1 >= MAXDUNY || dPiece[x][y] != 230 || dPiece[x + 1][y] != 231
				|| dPiece[x][y + 1] != 232 || dPiece[x + 1][y + 1] != 233
				|| dSpecial[x][y] != 0 || dSpecial[x + 1][y] != 0 || dSpecial[x][y + 1] != 0 || dSpecial[x + 1][y + 1] != 0)
				continue;
			++largeCount;
			largeRocksGrouped = largeRocksGrouped && owner >= 0 && membership[static_cast<size_t>(y) * MAXDUNX + x + 1] == owner
				&& membership[static_cast<size_t>(y + 1) * MAXDUNX + x] == owner
				&& membership[static_cast<size_t>(y + 1) * MAXDUNX + x + 1] == owner;
		}
	}
	Check(exactSuppression, "rock scenery suppression includes exactly the original object fragments");
	Check(largeCount > 0 && largeRocksGrouped, "each complete native230..233 stencil belongs to one rock instead of four columns");
	for (Point foot : { Point { 27, 49 }, Point { 29, 49 }, Point { 27, 51 },
		Point { 71, 67 }, Point { 71, 69 }, Point { 71, 71 } }) {
		const auto found = std::find_if(groups.begin(), groups.end(), [&](const TownPropGroup &group) { return group.referenceFootpoint == foot; });
		Check(found != groups.end() && TownPropHiddenByArchitecture(*found),
			"native cabin rock filler remains grouped but hidden inside architecture at " + std::to_string(foot.x) + "," + std::to_string(foot.y));
	}
	const auto external = std::find_if(groups.begin(), groups.end(), [](const TownPropGroup &group) { return group.referenceFootpoint == Point { 55, 71 }; });
	Check(external != groups.end() && !TownPropHiddenByArchitecture(*external), "native external boulder55,71 remains visible outside architectural bodies");
	const std::string grouping = PropGroupingState();
	ResetTownProps();
	Check(PropGroupingState() == grouping, "rock grouping reset reproduces the same original stencil composition");
	Check(NativeSceneState() == original, "rock grouping and suppression preserve original SOL, MIN, actors, and native collision");
	Record("INFO native rock objects=" + std::to_string(GetTownPropGroups().size()) + " fragments=" + std::to_string(sourceCount)
		+ " large2x2=" + std::to_string(largeCount));
}

bool Pick(Point screen, Point &tile, int &towner, int &item, int &player)
{
	return PickTownView(screen, tile, towner, item, player);
}

void CheckGroundPicking()
{
	int correct = 0;
	for (int y = std::max(10, ViewPosition.y - 12); y < std::min(84, ViewPosition.y + 12); ++y) {
		for (int x = std::max(10, ViewPosition.x - 12); x < std::min(84, ViewPosition.x + 12); ++x) {
			const Point expected { x, y };
			if (!IsTileNotSolid(expected))
				continue;
			const Point screen = TownViewScreenPosition(expected);
			if (screen.x < 8 || screen.x >= gnScreenWidth - 8 || screen.y < 8 || screen.y >= gnViewportHeight - 8)
				continue;
			Point selected;
			int towner;
			int item;
			int player;
			// Tall scenery and sprites may correctly occlude a ground center.
			if (Pick(screen, selected, towner, item, player) && selected == expected && towner < 0 && item < 0 && player < 0)
				++correct;
		}
	}
	Check(correct >= 10, "projected visible walkable ground roundtrips (" + std::to_string(correct) + " tiles)");
}

bool CheckEntityPicking(bool requireVisible = true)
{
	int playerPixels = 0;
	int townerPixels = 0;
	std::vector<int> perTowner(Towners.size(), 0);
	bool idsValid = true;
	for (int y = 0; y < gnViewportHeight; ++y) {
		for (int x = 0; x < gnScreenWidth; ++x) {
			Point tile;
			int towner;
			int item;
			int player;
			if (!Pick({ x, y }, tile, towner, item, player))
				continue;
			if (player >= 0) {
				idsValid = idsValid && player < static_cast<int>(Players.size()) && tile == Players[player].position.tile;
				++playerPixels;
			}
			if (towner >= 0) {
				idsValid = idsValid && towner < static_cast<int>(Towners.size()) && tile == Towners[towner].position;
				++townerPixels;
				if (towner < static_cast<int>(perTowner.size()))
					++perTowner[towner];
			}
		}
	}
	Record("INFO entity selection pixels yaw=" + std::to_string(GetTownViewCameraState().yaw)
		+ " player=" + std::to_string(playerPixels) + " NPC=" + std::to_string(townerPixels));
	for (size_t i = 0; i < perTowner.size(); ++i)
		if (perTowner[i] != 0)
			Record("INFO NPC selection index=" + std::to_string(i) + " name=" + std::string(Towners[i].name)
				+ " pixels=" + std::to_string(perTowner[i]));
	Check(idsValid, "all player and NPC selection pixels refer to their live tiles");
	const bool visible = playerPixels > 0 && townerPixels > 0;
	if (requireVisible)
		Check(visible, "visible player and NPC are selectable");
	return visible;
}

void LoadTown()
{
	const auto sol = LoadLevelSOLData();
	Check(sol.has_value(), "load original town SOL collision data");
	pDungeonCels = LoadFileInMem("levels\\towndata\\town.cel");
	pMegaTiles = LoadFileInMem<MegaTile>("levels\\towndata\\town.til");
	pSpecialCels = LoadCel("levels\\towndata\\towns", 64);
	SetDungeonMicros(pDungeonCels, MicroTileLen);
	CreateTown(ENTRY_MAIN);
	MakeLightTable();
	std::array<Color, 256> colors;
	LoadFileInMem("levels\\towndata\\town.pal", colors);
	for (size_t i = 0; i < colors.size(); ++i)
		logical_palette[i] = system_palette[i] = colors[i].toSDL();
}

void InitializePlayer()
{
	Players.clear();
	Players.resize(1);
	MyPlayerId = 0;
	MyPlayer = &Players[0];
	Player &player = *MyPlayer;
	player.plractive = true;
	player.plrlevel = 0;
	player._pClass = HeroClass::Warrior;
	player._pHitPoints = player._pMaxHP = 64 * 70;
	player._pmode = PM_STAND;
	player._pdir = Direction::South;
	player.position.tile = player.position.future = player.position.last = player.position.old = player.position.temp = ViewPosition;
	player.AnimationData[0].sprites = LoadCl2Sheet("plrgfx\\warrior\\wln\\wlnst", 96);
	const ClxSpriteList sprites = player.AnimationData[0].spritesForDirection(Direction::South);
	player.AnimInfo.setNewAnimation(OptionalClxSpriteList { ClxSpriteList { sprites } }, static_cast<int8_t>(sprites.numSprites()), 1);
	dPlayer[ViewPosition.x][ViewPosition.y] = 1;
	dFlags[ViewPosition.x][ViewPosition.y] |= DungeonFlag::Lit | DungeonFlag::Visible;
}

void PlaceFixturePlayerNear(Point desired)
{
	for (int radius = 0; radius <= 10; ++radius) {
		for (int dy = -radius; dy <= radius; ++dy) {
			for (int dx = -radius; dx <= radius; ++dx) {
				if (std::abs(dx) + std::abs(dy) != radius)
					continue;
				const Point point { desired.x + dx, desired.y + dy };
				if (point.x < 10 || point.y < 10 || point.x >= 84 || point.y >= 84
				    || !IsTileNotSolid(point)
				    || dMonster[point.x][point.y] != 0 || dSpecial[point.x][point.y] != 0)
					continue;
				dPlayer[MyPlayer->position.tile.x][MyPlayer->position.tile.y] = 0;
				MyPlayer->position.tile = MyPlayer->position.future = MyPlayer->position.last = MyPlayer->position.old = MyPlayer->position.temp = point;
				dPlayer[point.x][point.y] = 1;
				dFlags[point.x][point.y] |= DungeonFlag::Lit | DungeonFlag::Visible;
				ViewPosition = point;
				return;
			}
		}
	}
	Check(false, "locate walkable fixture position near requested capture");
}

void CheckWalkingContinuity(const Surface &out, const std::filesystem::path &output)
{
	Player &player = *MyPlayer;
	const ActorPosition savedPosition = player.position;
	const AnimationInfo savedAnimation = player.AnimInfo;
	const PLR_MODE savedMode = player._pmode;
	const Direction savedDirection = player._pdir;
	const Point savedView = ViewPosition;
	const uint8_t savedTickProgress = ProgressToNextGameTick;
	const Point start = player.position.tile;
	const Point destination = start + Direction::South;
	Check(CanStep(start, destination) && dMonster[destination.x][destination.y] == 0,
		"central fixture has a native walkable south step");
	player.AnimationData[static_cast<size_t>(player_graphic::Walk)].sprites = LoadCl2Sheet("plrgfx\\warrior\\wln\\wlnwl", 96);
	const ClxSpriteList walk = player.AnimationData[static_cast<size_t>(player_graphic::Walk)].spritesForDirection(Direction::South);
	player.AnimInfo.setNewAnimation(OptionalClxSpriteList { ClxSpriteList { walk } }, static_cast<int8_t>(walk.numSprites()), 1);
	player._pmode = PM_WALK_SOUTHWARDS;
	player._pdir = Direction::South;
	player.position.future = player.position.temp = destination;
	ProgressToNextGameTick = 0;
	Check(DrawTownView(out, true), "draw original warrior walk animation at step start");
	const Point first = TownViewScreenPosition(destination);
	Capture(out, output / "tristram-walk-start.bmp");
	for (int frame = 1; frame < static_cast<int>(walk.numSprites()); ++frame) {
		player.AnimInfo.processAnimation();
		ProgressToNextGameTick = frame == walk.numSprites() - 1 ? 127 : 0;
		Check(DrawTownView(out, true), "draw walking frame " + std::to_string(frame));
	}
	const Point beforeCompletion = TownViewScreenPosition(destination);
	Capture(out, output / "tristram-walk-before-completion.bmp");
	Check(player.position.tile == start && player.position.future == destination,
		"rendering walk interpolation preserves live step positions");
	// Native DoWalk finishes by moving tile to temp and selecting the stand animation.
	// The same fixed ground point should barely move when that discrete update happens.
	dPlayer[start.x][start.y] = 0;
	dPlayer[destination.x][destination.y] = 1;
	player.position.tile = player.position.future = player.position.last = player.position.old = player.position.temp = destination;
	player._pmode = PM_STAND;
	player.AnimInfo = savedAnimation;
	ProgressToNextGameTick = 0;
	ViewPosition = destination;
	Check(DrawTownView(out, true), "draw completed native walking step");
	const Point afterCompletion = TownViewScreenPosition(destination);
	const int completionShift = std::abs(beforeCompletion.x - afterCompletion.x) + std::abs(beforeCompletion.y - afterCompletion.y);
	const int travel = std::abs(first.x - beforeCompletion.x) + std::abs(first.y - beforeCompletion.y);
	Check(travel >= 4 && completionShift <= 3,
		"walk camera interpolates toward its destination without tile-boundary jump (travel="
			+ std::to_string(travel) + "px, completion=" + std::to_string(completionShift) + "px)");
	Capture(out, output / "tristram-walk-completed.bmp");
	CheckGroundPicking();
	dPlayer[destination.x][destination.y] = 0;
	dPlayer[start.x][start.y] = 1;
	player.position = savedPosition;
	player.AnimInfo = savedAnimation;
	player._pmode = savedMode;
	player._pdir = savedDirection;
	ViewPosition = savedView;
	ProgressToNextGameTick = savedTickProgress;
	Check(DrawTownView(out, true), "restore stationary fixture after walking check");
}

void CheckCameraControls(const Surface &out, const std::filesystem::path &output)
{
	constexpr float Pi = 3.14159265358979323846F;
	const Point originalPlayer = MyPlayer->position.tile;
	const Point originalView = ViewPosition;
	std::array<uint16_t, MAXDUNX * MAXDUNY> originalMap;
	std::memcpy(originalMap.data(), dPiece, sizeof(dPiece));
	const auto finite = [](TownViewCameraState state) {
		return std::isfinite(state.yaw) && std::isfinite(state.pitch) && std::isfinite(state.distance)
			&& std::isfinite(state.offsetX) && std::isfinite(state.offsetZ);
	};
	const auto draw = [&](const std::string &description) {
		Check(finite(GetTownViewCameraState()) && DrawTownView(out, true), description);
		CheckGroundPicking();
	};
	ResetTownViewCamera();
	draw("draw reset camera before control checks");
	const TownViewCameraState initial = GetTownViewCameraState();
	const Point probe = originalView + Direction::South;
	const Point initialScreen = TownViewScreenPosition(probe);
	for (int octant = 1; octant <= 8; ++octant) {
		OrbitTownView(Pi / 4, 0);
		Point tile;
		int npc, item, player;
		Check(!Pick(initialScreen, tile, npc, item, player), "orbit invalidates cursor result before redraw");
		draw("draw full orbit at " + std::to_string(octant * 45) + " degrees");
		if (octant % 2 == 0)
			Capture(out, output / ("tristram-orbit-" + std::to_string(octant * 45) + ".bmp"));
	}
	const Point orbitScreen = TownViewScreenPosition(probe);
	Check(std::abs(orbitScreen.x - initialScreen.x) + std::abs(orbitScreen.y - initialScreen.y) <= 1,
		"full 360 degree orbit returns the ground projection to its starting point");
	ResetTownViewCamera();
	Check(!BeginTownViewCameraDrag({ 320, gnViewportHeight }) && !BeginTownViewCameraDrag({ -1, 80 }),
		"camera drag starts only inside the world viewport");
	Check(BeginTownViewCameraDrag({ 320, 176 }) && IsTownViewCameraDragging(), "begin native camera orbit drag");
	Check(UpdateTownViewCameraDrag({ 410, 226 }), "mouse drag updates yaw and pitch");
	EndTownViewCameraDrag();
	const auto dragged = GetTownViewCameraState();
	Check(!IsTownViewCameraDragging() && std::abs(std::remainder(dragged.yaw - initial.yaw, 2 * Pi)) > 0.1F
			&& dragged.pitch > initial.pitch,
		"camera orbit drag changes both axes and ends cleanly");
	draw("draw mouse-orbited camera");
	Capture(out, output / "tristram-camera-drag.bmp");
	ResetTownViewCamera();
	ZoomTownView(5);
	Check(GetTownViewCameraState().distance < initial.distance, "wheel forward moves camera closer");
	draw("draw wheel zoom closer");
	ZoomTownView(-5);
	Check(std::abs(GetTownViewCameraState().distance - initial.distance) < 0.01F, "opposite wheel input restores distance");
	ZoomTownView(1000);
	const float nearDistance = GetTownViewCameraState().distance;
	ZoomTownView(1000);
	Check(nearDistance > 0 && nearDistance < initial.distance && GetTownViewCameraState().distance == nearDistance,
		"extreme wheel zoom settles at a positive near limit");
	draw("draw camera at near zoom limit");
	Capture(out, output / "tristram-camera-near.bmp");
	ZoomTownView(-1000);
	const float farDistance = GetTownViewCameraState().distance;
	ZoomTownView(-1000);
	Check(farDistance > initial.distance && GetTownViewCameraState().distance == farDistance,
		"extreme wheel zoom settles at a finite far limit");
	draw("draw camera at far zoom limit");
	Capture(out, output / "tristram-camera-far.bmp");
	ResetTownViewCamera();
	OrbitTownView(0, -1000);
	const float lowPitch = GetTownViewCameraState().pitch;
	OrbitTownView(0, -1000);
	Check(lowPitch > 0 && lowPitch < initial.pitch && GetTownViewCameraState().pitch == lowPitch,
		"downward orbit remains above the horizon and stops at its limit");
	draw("draw camera at lowest inclination");
	Capture(out, output / "tristram-camera-low.bmp");
	OrbitTownView(0, 1000);
	const float highPitch = GetTownViewCameraState().pitch;
	OrbitTownView(0, 1000);
	Check(highPitch > initial.pitch && highPitch < Pi / 2 && GetTownViewCameraState().pitch == highPitch,
		"upward orbit remains below vertical and stops at its limit");
	draw("draw camera at highest inclination");
	Capture(out, output / "tristram-camera-high.bmp");
	Record("INFO camera limits distance=" + std::to_string(nearDistance) + ".." + std::to_string(farDistance)
		+ " pitch=" + std::to_string(lowPitch) + ".." + std::to_string(highPitch));
	ResetTownViewCamera();
	draw("draw camera before pan drag");
	Check(BeginTownViewCameraDrag({ 320, 176 }, true) && UpdateTownViewCameraDrag({ 400, 200 }), "native drag pans the camera");
	EndTownViewCameraDrag();
	const auto panned = GetTownViewCameraState();
	Check(finite(panned) && std::abs(panned.offsetX) + std::abs(panned.offsetZ) > 0.1F, "pan changes the camera target offset");
	draw("draw panned camera");
	Check(TownViewScreenPosition(probe) != initialScreen, "pan changes the world projection");
	Capture(out, output / "tristram-camera-pan.bmp");
	Check(BeginTownViewCameraDrag({ 320, 176 }), "begin drag before changing display mode");
	ToggleTownView();
	Check(!IsTownViewCameraDragging() && !UpdateTownViewCameraDrag({ 330, 180 }), "mode switch cancels active camera drag");
	ToggleTownView();
	ResetTownViewCamera();
	draw("draw reset camera after control checks");
	const auto reset = GetTownViewCameraState();
	Check(reset.yaw == initial.yaw && reset.pitch == initial.pitch && reset.distance == initial.distance
			&& reset.offsetX == 0 && reset.offsetZ == 0 && TownViewScreenPosition(probe) == initialScreen,
		"camera reset restores orbit, inclination, zoom, pan, and ground projection");
	Check(MyPlayer->position.tile == originalPlayer && ViewPosition == originalView
			&& std::memcmp(originalMap.data(), dPiece, sizeof(dPiece)) == 0,
		"orbit, zoom, inclination, and pan preserve native player and town data");
}

void ClearWorld(const Surface &out)
{
	for (int y = 0; y < std::min<int>(gnViewportHeight, out.h()); ++y)
		std::memset(out.at(0, y), 0, out.w());
}

void DrawActualNativeReference(const Surface &out)
{
	Check(out.w() >= gnScreenWidth && out.h() >= gnViewportHeight, "native target covers the real engine viewport");
	ClearWorld(out);
	Check(DrawNativeTownViewReference(out, ViewPosition), "native backend accepts a complete world target");
}

void CheckZeroPieceGround(const Surface &out)
{
	constexpr Point tile { 72, 74 };
	Check(dPiece[tile.x][tile.y] == 0 && IsTileNotSolid(tile), "zero MIN piece is real walkable ground beside the east cabin");
	ResetTownViewCamera();
	DrawActualNativeReference(out);
	const Point center = GetScreenPosition(tile) + Displacement { 32, -16 };
	const bool visible = center.x >= 2 && center.x < out.w() - 2 && center.y >= 2 && center.y < gnViewportHeight - 2;
	Check(visible, "zero-piece ground regression fixture is visible");
	if (!visible)
		return;
	std::array<uint8_t, 25> native;
	for (int dy = -2; dy <= 2; ++dy)
		for (int dx = -2; dx <= 2; ++dx)
			native[(dy + 2) * 5 + dx + 2] = out[center + Displacement { dx, dy }];
	Check(DrawTownView(out, true), "draw real zero-piece ground with raw geometry");
	bool equal = true;
	for (int dy = -2; dy <= 2; ++dy)
		for (int dx = -2; dx <= 2; ++dx)
			equal = equal && out[center + Displacement { dx, dy }] == native[(dy + 2) * 5 + dx + 2];
	Check(equal, "zero-piece ground reproduces the original pixels instead of a black hole");
	Point selected;
	int npc, item, player;
	Check(Pick(center, selected, npc, item, player) && selected == tile && npc < 0 && item < 0 && player < 0,
		"zero-piece ground remains selectable through raw geometry");
}

void CheckNativeCameraCalibration(const Surface &out)
{
	ResetTownViewCamera();
	CalcViewportGeometry();
	const Point nativeAnchor = GetScreenPosition(ViewPosition) + Displacement { 32, 0 };
	Check(DrawTownView(out, true), "draw raw geometry at the actual native camera anchor");
	bool calibrated = true;
	for (int dy = -5; dy <= 5; ++dy) {
		for (int dx = -5; dx <= 5; ++dx) {
			const Point screen = TownViewScreenPosition(ViewPosition + Displacement { dx, dy });
			const Point expected = nativeAnchor + Displacement { 32 * (dx - dy), 16 * (dx + dy) - 16 };
			calibrated = calibrated && std::abs(screen.x - expected.x) <= 1 && std::abs(screen.y - expected.y) <= 1;
		}
	}
	Check(calibrated, "raw geometry matches the actual native tile-center projection across 121 points");
}

void CheckNativePoseRoute(const Surface &out)
{
	ResetTownViewCamera();
	Check(IsTownViewNativePose(), "reset selects the native rendering and cursor route");
	OwnedSurface undersized(gnScreenWidth, gnViewportHeight - 1);
	for (int y = 0; y < undersized.h(); ++y)
		std::memset(undersized.at(0, y), 37, undersized.w());
	Check(!DrawNativeTownViewReference(undersized, ViewPosition), "native backend rejects a target shorter than its viewport");
	bool untouched = true;
	for (int y = 0; y < undersized.h(); ++y) {
		for (int x = 0; x < undersized.w(); ++x)
			untouched = untouched && *undersized.at(x, y) == 37;
	}
	Check(untouched, "rejected native target remains untouched");
	const std::string state = NativeSceneState();
	DrawActualNativeReference(out);
	const auto native = ViewportPixels(out);
	Check(DrawTownView(out), "default pose calls the actual native world backend");
	Check(native == ViewportPixels(out), "native pose is pixel-identical to original backend (shared rendering path, not a mesh fidelity test)");
	Point tile;
	int npc, item, player;
	Check(!Pick({ out.w() / 2, gnViewportHeight / 2 }, tile, npc, item, player),
		"native pose invalidates geometric IDs so the native cursor route is used");
	Check(BeginTownViewCameraDrag({ out.w() / 2, gnViewportHeight / 2 }), "native pose still accepts camera drag");
	Check(UpdateTownViewCameraDrag({ out.w() / 2 + 25, gnViewportHeight / 2 + 10 }), "drag leaves the native pose");
	EndTownViewCameraDrag();
	Check(!IsTownViewNativePose() && DrawTownView(out), "changed camera switches to geometric rendering and picking");
	CheckGroundPicking();
	ResetTownViewCamera();
	Check(IsTownViewNativePose() && !IsTownViewCameraDragging() && DrawTownView(out), "reset restores native route and ends drag");
	Check(native == ViewportPixels(out), "return from orbit restores original native pixels");
	Check(NativeSceneState() == state, "native backend, orbit and reset preserve fixture map and actor state");
}

void CaptureNativeProjectionPairs(const Surface &out, const std::filesystem::path &output)
{
	const ActorPosition savedPosition = MyPlayer->position;
	const Point savedView = ViewPosition;
	struct Fixture {
		const char *name;
		Point position;
		bool cabin;
	};
	const std::array<Fixture, 12> fixtures { Fixture { "tavern", { 51, 64 }, false },
		Fixture { "smithy", { 62, 65 }, false }, Fixture { "gillian", { 43, 66 }, false },
		Fixture { "pepin", { 55, 79 }, false }, Fixture { "adria", { 80, 20 }, false },
		Fixture { "northern-house", { 55, 44 }, false }, Fixture { "farnham-house", { 73, 83 }, false },
		Fixture { "cabin-west", { 32, 51 }, true }, Fixture { "cabin-east", { 76, 69 }, true },
		Fixture { "well", { 62, 72 }, false }, Fixture { "cathedral", { 25, 31 }, false },
		Fixture { "crypt", { 49, 22 }, false } };
	constexpr int ComparisonSize = 1024;
	// Wide native views also render beside the 128px panel. CalculatePanelAreas
	// therefore keeps the entire screen as its viewport; allocate all of it and
	// crop only when exporting the requested comparison frame.
	OwnedSurface fullArchitecture(ComparisonSize, ComparisonSize + GetMainPanel().size.height);
	SDL_SetPaletteColors(fullArchitecture.surface->format->palette, logical_palette.data(), 0, 256);
	for (const Fixture &fixture : fixtures) {
		PlaceFixturePlayerNear(fixture.position);
		CheckNativeCameraCalibration(out);
		Check(IsTileNotSolid(MyPlayer->position.tile) && dPlayer[MyPlayer->position.tile.x][MyPlayer->position.tile.y] == 1,
			std::string("comparison fixture seeds the real native player cell ") + fixture.name);
		const std::string state = NativeSceneState();
		DrawActualNativeReference(out);
		const auto native = ViewportPixels(out);
		SavePng(out.subregionY(0, gnViewportHeight), output / (std::string("native-") + fixture.name + ".png"));
		Check(DrawTownView(out), std::string("draw native fidelity route ") + fixture.name);
		Check(native == ViewportPixels(out), std::string("shared native backend produces identical pixels ") + fixture.name);
		SavePng(out.subregionY(0, gnViewportHeight), output / (std::string("nativefidelity-") + fixture.name + ".png"));
		Check(DrawTownView(out, true), std::string("draw forced raw geometry diagnostic ") + fixture.name);
		Check(NativeSceneState() == state, "actual native and raw geometry rendering preserve map, collision and actor state");
		SavePng(out.subregionY(0, gnViewportHeight), output / (std::string("calibrated-") + fixture.name + ".png"));
		int heroPixels = 0;
		for (int y = 0; y < gnViewportHeight; ++y) {
			for (int x = 0; x < out.w(); ++x) {
				Point tile;
				int npc, item, player;
				if (Pick({ x, y }, tile, npc, item, player) && player == MyPlayerId)
					++heroPixels;
			}
		}
		Record(std::string("INFO raw geometry diagnostic ") + fixture.name + " fixture=" + std::to_string(ViewPosition.x) + ","
			+ std::to_string(ViewPosition.y) + " geometric hero selection pixels=" + std::to_string(heroPixels));
		if (fixture.cabin)
			Check(heroPixels > 0, std::string("raw geometry keeps hero selectable on cabin approach ") + fixture.name);
		const int savedWidth = gnScreenWidth;
		const int savedHeight = gnScreenHeight;
		const int savedViewport = gnViewportHeight;
		gnScreenWidth = ComparisonSize;
		gnScreenHeight = fullArchitecture.h();
		CalculatePanelAreas();
		Check(gnViewportHeight == gnScreenHeight && fullArchitecture.h() >= gnViewportHeight,
			"wide native viewport includes panel-side rows within its allocation");
		CheckNativeCameraCalibration(fullArchitecture);
		DrawActualNativeReference(fullArchitecture);
		const auto fullNative = ViewportPixels(fullArchitecture);
		SavePng(fullArchitecture.subregionY(0, ComparisonSize), output / (std::string("native-full-") + fixture.name + ".png"));
		Check(DrawTownView(fullArchitecture), std::string("draw large native fidelity frame ") + fixture.name);
		Check(fullNative == ViewportPixels(fullArchitecture), std::string("large native route exactly matches original backend ") + fixture.name);
		SavePng(fullArchitecture.subregionY(0, ComparisonSize), output / (std::string("nativefidelity-full-") + fixture.name + ".png"));
		Check(DrawTownView(fullArchitecture, true), std::string("draw large raw geometry frame at native scale ") + fixture.name);
		SavePng(fullArchitecture.subregionY(0, ComparisonSize), output / (std::string("calibrated-full-") + fixture.name + ".png"));
		gnScreenWidth = savedWidth;
		gnScreenHeight = savedHeight;
		gnViewportHeight = savedViewport;
		CalculatePanelAreas();
		CalcViewportGeometry();
	}
	std::ofstream metadata(output / "comparison-capture-method.json");
	metadata << "{\"native\":\"actual original DrawGame world backend via DrawNativeTownViewReference\","
		<< "\"nativefidelity\":\"same native backend selected by default pose; equality validates dispatch, not mesh reconstruction\","
		<< "\"calibrated\":\"forced raw 3D geometry diagnostic at real native anchor; artistic differences remain measurable\","
		<< "\"zoom\":false,\"fullViewport\":[1024," << fullArchitecture.h()
		<< "],\"comparisonCrop\":[1024,1024],\"fullScreenHeight\":" << fullArchitecture.h() << "}\n";
	Check(metadata.good(), "capture metadata distinguishes shared native path from raw geometric fidelity");
	dPlayer[MyPlayer->position.tile.x][MyPlayer->position.tile.y] = 0;
	MyPlayer->position = savedPosition;
	dPlayer[savedPosition.tile.x][savedPosition.tile.y] = 1;
	ViewPosition = savedView;
	ResetTownViewCamera();
	Check(DrawTownView(out, true), "restore original geometric fixture after comparison captures");
}

// These checks use geometry and visible ownership, rather than treating black
// palette entries as holes. The original art contains intentional black pixels.
struct RayVector {
	double x, y, z;
	RayVector operator+(RayVector b) const { return { x + b.x, y + b.y, z + b.z }; }
	RayVector operator-(RayVector b) const { return { x - b.x, y - b.y, z - b.z }; }
	RayVector operator*(double s) const { return { x * s, y * s, z * s }; }
};

double RayDot(RayVector a, RayVector b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
RayVector RayCross(RayVector a, RayVector b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
RayVector RayPosition(const TownSceneVertex &v) { return { v.x, v.height, v.z }; }

bool RayTriangle(RayVector origin, RayVector direction, const TownSceneTriangle &triangle, double &distance, double &u, double &v)
{
	const RayVector a = RayPosition(triangle.vertices[0]);
	const RayVector e1 = RayPosition(triangle.vertices[1]) - a;
	const RayVector e2 = RayPosition(triangle.vertices[2]) - a;
	const RayVector h = RayCross(direction, e2);
	const double determinant = RayDot(e1, h);
	if (std::abs(determinant) < 1.0e-10)
		return false;
	const RayVector s = origin - a;
	u = RayDot(s, h) / determinant;
	if (u < 0 || u > 1)
		return false;
	const RayVector q = RayCross(s, e1);
	v = RayDot(direction, q) / determinant;
	if (v < 0 || u + v > 1)
		return false;
	distance = RayDot(e2, q) / determinant;
	return distance > 1.0e-6;
}

void CheckClosedArchitecture()
{
	size_t closedModels = 0;
	bool normalsValid = true;
	for (const TownSceneModel &model : GetTownScene()) {
		for (const auto &triangle : model.triangles) {
			const auto &n = triangle.normal;
			const double lengthSquared = n.x * n.x + n.height * n.height + n.z * n.z;
			normalsValid = normalsValid && std::isfinite(lengthSquared) && std::abs(lengthSquared - 1) < 0.001;
			if (triangle.nativeProjection)
				normalsValid = normalsValid && triangle.surfaceRole == TownSceneSurfaceRole::Exterior && n.x + n.height + n.z > 0;
		}
		if (!model.physicalBounds.closed)
			continue;
		++closedModels;
		const auto &body = model.physicalBounds;
		const RayVector origin { (body.minX + body.maxX) * 0.5, body.wallHeight * 0.5, (body.minZ + body.maxZ) * 0.5 };
		int enclosedDirections = 0;
		for (const RayVector direction : { RayVector { 1, 0, 0 }, RayVector { -1, 0, 0 }, RayVector { 0, 1, 0 },
			RayVector { 0, -1, 0 }, RayVector { 0, 0, 1 }, RayVector { 0, 0, -1 } }) {
			bool hit = false;
			for (const auto &triangle : model.triangles) {
				double t, u, v;
				hit = hit || RayTriangle(origin, direction, triangle, t, u, v);
			}
			enclosedDirections += hit ? 1 : 0;
		}
		Check(enclosedDirections == 6, "closed architectural body has surfaces in all six directions at "
			+ std::to_string(model.minTile.x) + "," + std::to_string(model.minTile.y));
	}
	Check(closedModels > 0 && normalsValid, "architectural surface normals are finite unit vectors and native overlay faces point toward the original view");
}

struct DecodedVolumeSprite {
	int width, height;
	std::vector<uint8_t> pixels, opacity;
	TownVolumeSprite view() const { return { width, height, pixels, opacity }; }
};

DecodedVolumeSprite DecodeVolumeSprite(ClxSprite sprite)
{
	OwnedSurface paint(sprite.width(), sprite.height());
	OwnedSurface coverage(sprite.width(), sprite.height());
	SDL_FillRect(paint.surface, nullptr, 0);
	SDL_FillRect(coverage.surface, nullptr, 255);
	ClxDraw(paint, { 0, paint.h() - 1 }, sprite);
	ClxDraw(coverage, { 0, coverage.h() - 1 }, sprite);
	DecodedVolumeSprite decoded { paint.w(), paint.h(), {}, {} };
	for (int y = 0; y < paint.h(); ++y) {
		for (int x = 0; x < paint.w(); ++x) {
			decoded.pixels.push_back(*paint.at(x, y));
			decoded.opacity.push_back(*paint.at(x, y) == *coverage.at(x, y) ? 1 : 0);
		}
	}
	return decoded;
}

std::string VolumeGeometryState(const TownVolumeMesh &mesh)
{
	std::ostringstream state;
	state << std::setprecision(std::numeric_limits<float>::max_digits10) << mesh.width << ',' << mesh.height << ',' << mesh.depth << ';';
	for (const auto &triangle : mesh.triangles) {
		state << static_cast<int>(triangle.material) << ',' << static_cast<int>(triangle.paletteIndex) << ',' << static_cast<int>(triangle.textureView) << ';';
		for (const auto &v : triangle.vertices)
			state << v.x << ',' << v.height << ',' << v.z << ',' << v.u << ',' << v.v << ';';
	}
	return state.str();
}

void CheckVolumeMesh(const TownVolumeMesh &mesh, const std::string &name)
{
	using VertexKey = std::tuple<int64_t, int64_t, int64_t>;
	using EdgeKey = std::pair<VertexKey, VertexKey>;
	std::map<EdgeKey, size_t> edges;
	std::map<EdgeKey, int> edgeBalance;
	std::map<EdgeKey, std::vector<size_t>> edgeOwners;
	bool finite = true;
	bool nondegenerate = true;
	double minimumZ = std::numeric_limits<double>::max();
	double maximumZ = std::numeric_limits<double>::lowest();
	const auto key = [](const TownVolumeVertex &v) {
		return VertexKey { std::llround(v.x * 100000.0), std::llround(v.height * 100000.0), std::llround(v.z * 100000.0) };
	};
	for (size_t triangleIndex = 0; triangleIndex < mesh.triangles.size(); ++triangleIndex) {
		const auto &triangle = mesh.triangles[triangleIndex];
		for (const auto &v : triangle.vertices) {
			finite = finite && std::isfinite(v.x) && std::isfinite(v.height) && std::isfinite(v.z) && std::isfinite(v.u) && std::isfinite(v.v);
			minimumZ = std::min(minimumZ, static_cast<double>(v.z));
			maximumZ = std::max(maximumZ, static_cast<double>(v.z));
		}
		if (!finite)
			break;
		const auto &a = triangle.vertices[0];
		const auto &b = triangle.vertices[1];
		const auto &c = triangle.vertices[2];
		const RayVector cross = RayCross({ b.x - a.x, b.height - a.height, b.z - a.z }, { c.x - a.x, c.height - a.height, c.z - a.z });
		nondegenerate = nondegenerate && RayDot(cross, cross) > 1.0e-14;
		for (size_t i = 0; i < 3; ++i) {
			VertexKey first = key(triangle.vertices[i]);
			VertexKey second = key(triangle.vertices[(i + 1) % 3]);
			const int direction = first < second ? 1 : -1;
			if (second < first)
				std::swap(first, second);
			++edges[{ first, second }];
			edgeBalance[{ first, second }] += direction;
			edgeOwners[{ first, second }].push_back(triangleIndex);
		}
	}
	const auto openEdges = std::count_if(edges.begin(), edges.end(), [](const auto &edge) { return edge.second != 2; });
	const auto inconsistentEdges = std::count_if(edgeBalance.begin(), edgeBalance.end(), [](const auto &edge) { return edge.second != 0; });
	size_t reportedEdges = 0;
	for (const auto &[edge, count] : edges) {
		if (count == 2 && edgeBalance[edge] == 0)
			continue;
		if (reportedEdges++ >= 8)
			break;
		const auto coordinates = [](const VertexKey &vertex) {
			std::ostringstream point;
			point << std::setprecision(9) << std::get<0>(vertex) / 100000.0 << ',' << std::get<1>(vertex) / 100000.0 << ',' << std::get<2>(vertex) / 100000.0;
			return point.str();
		};
		std::string triangleIndices;
		for (size_t index : edgeOwners[edge])
			triangleIndices += " " + std::to_string(index);
		Record("INFO invalid volume edge " + name + " from=" + coordinates(edge.first) + " to=" + coordinates(edge.second)
			+ " occurrences=" + std::to_string(count) + " direction balance=" + std::to_string(edgeBalance[edge]) + " triangles=" + triangleIndices);
	}
	Record("INFO volume " + name + " triangles=" + std::to_string(mesh.triangles.size()) + " unmatched edges=" + std::to_string(openEdges)
		+ " actual depth=" + std::to_string(maximumZ - minimumZ));
	Check(finite && nondegenerate && mesh.triangles.size() >= 24 && maximumZ - minimumZ > 0.05,
		name + " uses finite nondegenerate triangles with physical depth, rather than a flat image card");
	Check(openEdges == 0 && inconsistentEdges == 0, name + " has closed oriented components (two oppositely directed triangles per edge)");
}

void ExportDecodedActor(const DecodedVolumeSprite &sprite, const std::filesystem::path &path)
{
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> rgba(
		SDL_CreateRGBSurfaceWithFormat(0, sprite.width, sprite.height, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
	Check(rgba != nullptr, "allocate exact native actor RGBA export");
	for (int y = 0; y < sprite.height; ++y) {
		auto *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(rgba->pixels) + y * rgba->pitch);
		for (int x = 0; x < sprite.width; ++x) {
			const size_t index = static_cast<size_t>(y) * sprite.width + x;
			const SDL_Color color = logical_palette[sprite.pixels[index]];
			row[x] = SDL_MapRGBA(rgba->format, color.r, color.g, color.b, sprite.opacity[index] ? 255 : 0);
		}
	}
	SavePng(Surface { rgba.get() }, path);
	std::ofstream pixels(path.parent_path() / (path.stem().string() + ".pixels.bin"), std::ios::binary);
	pixels.write(reinterpret_cast<const char *>(sprite.pixels.data()), static_cast<std::streamsize>(sprite.pixels.size()));
	std::ofstream opacity(path.parent_path() / (path.stem().string() + ".opacity.bin"), std::ios::binary);
	opacity.write(reinterpret_cast<const char *>(sprite.opacity.data()), static_cast<std::streamsize>(sprite.opacity.size()));
	std::ofstream metadata(path.parent_path() / (path.stem().string() + ".json"));
	metadata << "{\"width\":" << sprite.width << ",\"height\":" << sprite.height << ",\"nativePixelsOnly\":true,\"opacityExplicit\":true}\n";
	Check(pixels.good() && opacity.good() && metadata.good(), "export original actor pixels and opacity for independent volume diagnostics");
}

void ExportActorBodyMask(const DecodedVolumeSprite &source, const std::filesystem::path &path)
{
	DecodedVolumeSprite body = source;
	body.opacity = TownActorBodyOpacity(source.view());
	Check(body.opacity.size() == source.opacity.size(), "real actor body mask has original sprite dimensions");
	bool paintPreserved = true;
	size_t shadowPixels = 0;
	for (size_t index = 0; index < source.opacity.size(); ++index) {
		paintPreserved = paintPreserved && (body.opacity[index] == 0 || source.opacity[index] != 0)
			&& (source.opacity[index] == 0 || source.pixels[index] == 0 || body.opacity[index] != 0);
		shadowPixels += source.opacity[index] != 0 && body.opacity[index] == 0 ? 1 : 0;
	}
	Check(paintPreserved, "separating actor ground shadow retains every colored native body pixel and adds no paint");
	ExportDecodedActor(body, path);
	Record("INFO actor ground shadow " + path.stem().string() + " separated pixels=" + std::to_string(shadowPixels));
}

void CheckDirectionalActorBody(const TownVolumeMesh &body, const std::array<TownVolumeSprite, 8> &views, int facing, const std::string &name)
{
	CheckVolumeMesh(body, name);
	float minimumHeight = std::numeric_limits<float>::infinity();
	double maximumForwardProjectionError = 0;
	size_t forwardVertices = 0;
	bool textureReferences = true;
	for (const auto &triangle : body.triangles) {
		textureReferences = textureReferences && triangle.textureView < views.size();
		for (const auto &vertex : triangle.vertices) {
			minimumHeight = std::min(minimumHeight, vertex.height);
			textureReferences = textureReferences && vertex.u >= -0.00001F && vertex.u <= 1.00001F
				&& vertex.v >= -0.00001F && vertex.v <= 1.00001F;
			if (triangle.textureView == facing) {
				const double originalX = views[facing].width * 0.5 + 45.25483399593904 * vertex.x;
				const double originalY = views[facing].height - 1 + 22.62741699796952 * vertex.z - 32.0 * vertex.height;
				maximumForwardProjectionError = std::max({ maximumForwardProjectionError,
					std::abs(originalX - vertex.u * views[facing].width),
					std::abs(originalY - vertex.v * views[facing].height) });
				++forwardVertices;
			}
		}
	}
	Record("INFO " + name + " minimum height=" + std::to_string(minimumHeight)
		+ " native projection UV error pixels=" + std::to_string(maximumForwardProjectionError));
	Check(minimumHeight >= -0.00001F && minimumHeight < 0.0001F, name + " touches the physical ground without floating");
	Check(forwardVertices > 0 && maximumForwardProjectionError < 0.0001, name + " retains original forward texture projection after grounding");
	Check(textureReferences, name + " references valid native directional textures and UVs");
}

TownVolumeMesh CheckRealActorBodies(const std::filesystem::path &output)
{
	const std::string original = NativeSceneState();
	const auto directory = output / "actor-sources";
	std::filesystem::create_directories(directory);
	const auto &data = MyPlayer->AnimationData[static_cast<size_t>(MyPlayer->getGraphic())];
	const int frame = MyPlayer->AnimInfo.getFrameToUseForRendering();
	Check(data.sprites && frame >= 0, "real warrior current animation and frame are available");
	const ClxSpriteSheet sheet = *data.sprites;
	Check(sheet.numLists() == 8, "real current warrior animation provides all eight native directions");
	std::array<DecodedVolumeSprite, 8> decoded {};
	std::array<TownVolumeSprite, 8> views {};
	TownVolumeMesh currentBody;
	for (size_t direction = 0; direction < views.size(); ++direction) {
		const ClxSpriteList list = data.spritesForDirection(static_cast<Direction>(direction));
		Check(static_cast<uint32_t>(frame) < list.numSprites(), "same real animation frame exists in warrior direction=" + std::to_string(direction));
		decoded[direction] = DecodeVolumeSprite(list[frame]);
		views[direction] = decoded[direction].view();
		ExportDecodedActor(decoded[direction], directory / ("warrior-direction-" + std::to_string(direction) + ".png"));
		ExportActorBodyMask(decoded[direction], directory / ("warrior-direction-" + std::to_string(direction) + "-body.png"));
	}
	for (int facing = 0; facing < 8; ++facing) {
		const auto body = BuildTownActorVisualHull(views, facing);
		CheckDirectionalActorBody(body, views, facing, "real eight-view warrior body facing=" + std::to_string(facing));
		Check(VolumeGeometryState(body) == VolumeGeometryState(BuildTownActorVisualHull(views, facing)),
			"real eight-view body rebuilds deterministically facing=" + std::to_string(facing));
		if (facing == static_cast<int>(MyPlayer->_pdir))
			currentBody = body;
	}
	std::ofstream metadata(directory / "warrior-frame.json");
	metadata << "{\"graphic\":" << static_cast<int>(MyPlayer->getGraphic()) << ",\"frame\":" << frame
		<< ",\"directions\":8,\"body\":\"physical visual hull; no billboard shear\"}\n";
	for (size_t index = 0; index < Towners.size(); ++index) {
		const auto &towner = Towners[index];
		if (!towner.anim)
			continue;
		const auto source = DecodeVolumeSprite(towner.currentSprite());
		ExportDecodedActor(source, directory / ("npc-" + std::to_string(index) + "-type-" + std::to_string(static_cast<int>(towner._ttype)) + ".png"));
		ExportActorBodyMask(source, directory / ("npc-" + std::to_string(index) + "-body.png"));
		const bool humanoid = towner._ttype != TOWN_COW;
		const auto body = BuildTownActorSingleViewBody(source.view(), humanoid);
		CheckVolumeMesh(body, "original NPC body index=" + std::to_string(index));
		bool aboveGround = true;
		float minimumHeight = std::numeric_limits<float>::infinity();
		for (const auto &triangle : body.triangles)
			for (const auto &vertex : triangle.vertices) {
				aboveGround = aboveGround && vertex.height >= -0.00001F;
				minimumHeight = std::min(minimumHeight, vertex.height);
			}
		Record("INFO original NPC index=" + std::to_string(index) + " minimum height=" + std::to_string(minimumHeight));
		Check(minimumHeight >= -0.00001F && minimumHeight < 0.0001F,
			"real NPC body touches the physical ground without floating index=" + std::to_string(index));
		Check(aboveGround && VolumeGeometryState(body) == VolumeGeometryState(BuildTownActorSingleViewBody(source.view(), humanoid)),
			"real NPC body has stable geometry with feet above ground index=" + std::to_string(index));
		if (towner._ttype != TOWN_COW)
			continue;
		const auto cowSheet = GetTownCowSpriteSheet();
		Check(cowSheet && cowSheet->numLists() == 8, "real cow current animation provides eight native directions index=" + std::to_string(index));
		std::array<DecodedVolumeSprite, 8> cowDecoded {};
		std::array<TownVolumeSprite, 8> cowViews {};
		int currentDirection = -1;
		for (int direction = 0; direction < 8; ++direction) {
			const ClxSpriteList list = (*cowSheet)[direction];
			Check(towner._tAnimFrame < list.numSprites(), "same real cow frame is available direction=" + std::to_string(direction));
			const ClxSprite sprite = list[towner._tAnimFrame];
			if (sprite.pixelData() == towner.currentSprite().pixelData())
				currentDirection = direction;
			cowDecoded[direction] = DecodeVolumeSprite(sprite);
			cowViews[direction] = cowDecoded[direction].view();
			const std::string stem = "cow-" + std::to_string(index) + "-direction-" + std::to_string(direction);
			ExportDecodedActor(cowDecoded[direction], directory / (stem + ".png"));
			ExportActorBodyMask(cowDecoded[direction], directory / (stem + "-body.png"));
		}
		Check(currentDirection >= 0, "cow current facing matches an actual native animation direction index=" + std::to_string(index));
		const auto cowBody = BuildTownActorVisualHull(cowViews, currentDirection);
		CheckDirectionalActorBody(cowBody, cowViews, currentDirection, "real eight-view cow body index=" + std::to_string(index));
		Check(VolumeGeometryState(cowBody) == VolumeGeometryState(BuildTownActorVisualHull(cowViews, currentDirection)),
			"real eight-view cow rebuilds deterministically index=" + std::to_string(index));
		Record("INFO cow source index=" + std::to_string(index) + " frame=" + std::to_string(towner._tAnimFrame)
			+ " native direction=" + std::to_string(currentDirection));
	}
	Check(NativeSceneState() == original, "real eight-direction and NPC body diagnostics preserve map, directions, and animation state");
	Check(!currentBody.triangles.empty(), "current native warrior facing supplies the independently audited body for occlusion diagnostics");
	return currentBody;
}

void ExportVegetationVolume(const Surface &diagnostic, Point sourceSize, const TownVolumeMesh &mesh,
	const TownVegetationGroup &group, size_t index, bool failed, const std::filesystem::path &output)
{
	const std::string stem = "group-native-" + std::to_string(group.referenceFootpoint.x) + "-" + std::to_string(group.referenceFootpoint.y);
	SavePng(diagnostic, output / (stem + ".png"));
	std::ofstream pixels(output / (stem + ".pixels.bin"), std::ios::binary);
	std::ofstream opacity(output / (stem + ".opacity.bin"), std::ios::binary);
	for (int y = 0; y < sourceSize.y; ++y) {
		pixels.write(reinterpret_cast<const char *>(diagnostic.at(0, y)), sourceSize.x);
		for (int x = 0; x < sourceSize.x; ++x) {
			const uint8_t covered = *diagnostic.at(sourceSize.x + 16 + x, y) != 0 ? 1 : 0;
			opacity.write(reinterpret_cast<const char *>(&covered), 1);
		}
	}
	std::ofstream metadata(output / (stem + ".json"));
	metadata << std::setprecision(std::numeric_limits<float>::max_digits10)
		<< "{\"groupIndex\":" << index << ",\"family\":" << static_cast<int>(group.family)
		<< ",\"foot\":[" << group.referenceFootpoint.x << ',' << group.referenceFootpoint.y
		<< "],\"seed\":" << group.referenceFootpoint.x + group.referenceFootpoint.y * MAXDUNX + 1
		<< ",\"foliage\":" << (group.foliage ? "true" : "false")
		<< ",\"width\":" << sourceSize.x << ",\"height\":" << sourceSize.y
		<< ",\"meshWidth\":" << mesh.width << ",\"meshHeight\":" << mesh.height << ",\"meshDepth\":" << mesh.depth
		<< ",\"triangles\":" << mesh.triangles.size() << ",\"failedVolumeAudit\":" << (failed ? "true" : "false")
		<< ",\"nativePixelsOnly\":true,\"opacityExplicit\":true}\n";
	std::ofstream geometry(output / (stem + ".obj"));
	geometry << std::setprecision(std::numeric_limits<float>::max_digits10)
		<< "# Actual renderer mesh; vertex axes x, height, z. Each triangle has separate vertices.\n";
	for (size_t triangleIndex = 0; triangleIndex < mesh.triangles.size(); ++triangleIndex) {
		const auto &triangle = mesh.triangles[triangleIndex];
		geometry << "# triangle " << triangleIndex << " material " << static_cast<int>(triangle.material)
			<< " palette " << static_cast<int>(triangle.paletteIndex) << " textureView " << static_cast<int>(triangle.textureView) << '\n';
		for (const auto &v : triangle.vertices)
			geometry << "v " << v.x << ' ' << v.height << ' ' << v.z << '\n';
		for (const auto &v : triangle.vertices)
			geometry << "vt " << v.u << ' ' << v.v << '\n';
		const size_t first = triangleIndex * 3 + 1;
		geometry << "f " << first << '/' << first << ' ' << first + 1 << '/' << first + 1 << ' ' << first + 2 << '/' << first + 2 << '\n';
	}
	Check(pixels.good() && opacity.good() && metadata.good() && geometry.good(), "export actual tree source, opacity, metadata, and raw volume geometry index=" + std::to_string(index));
	Record("INFO tree composition index=" + std::to_string(index) + " family=" + std::to_string(static_cast<int>(group.family))
		+ " foot=" + std::to_string(group.referenceFootpoint.x) + "," + std::to_string(group.referenceFootpoint.y)
		+ " source=" + std::to_string(sourceSize.x) + "x" + std::to_string(sourceSize.y)
		+ " mesh dimensions=" + std::to_string(mesh.width) + "," + std::to_string(mesh.height) + "," + std::to_string(mesh.depth));
}

bool CheckRenderedVegetationVolumes(const std::filesystem::path &output)
{
	const std::string original = NativeSceneState();
	const auto &groups = GetTownVegetationGroups();
	std::set<TownVegetationFamily> exported;
	size_t nearestBare = groups.size();
	int nearestDistance = std::numeric_limits<int>::max();
	Point cabinCenter { 72, 69 };
	for (const auto &model : GetTownScene()) {
		if (model.kind == TownSceneKind::Cabin && model.minTile.x > 60)
			cabinCenter = { static_cast<int>(std::lround((model.physicalBounds.minX + model.physicalBounds.maxX) * 0.5F)),
				static_cast<int>(std::lround((model.physicalBounds.minZ + model.physicalBounds.maxZ) * 0.5F)) };
	}
	for (size_t index = 0; index < groups.size(); ++index) {
		if (groups[index].family != TownVegetationFamily::BareTree)
			continue;
		const Point tile = groups[index].referenceFootpoint;
		const int distance = std::abs(tile.x - cabinCenter.x) + std::abs(tile.y - cabinCenter.y);
		if (distance < nearestDistance) {
			nearestDistance = distance;
			nearestBare = index;
		}
	}
	OwnedSurface diagnostic(2048, 1024);
	SDL_SetPaletteColors(diagnostic.surface->format->palette, logical_palette.data(), 0, 256);
	size_t failedCount = 0;
	for (size_t index = 0; index < groups.size(); ++index) {
		const auto *mesh = TownViewVegetationVolume(index);
		Check(mesh != nullptr, "renderer provides its actual complete vegetation volume index=" + std::to_string(index));
		bool failed = false;
		try {
			CheckVolumeMesh(*mesh, "rendered whole tree index=" + std::to_string(index));
		} catch (const std::runtime_error &) {
			failed = true;
			++failedCount;
		}
		const auto &group = groups[index];
		if (exported.insert(group.family).second || index == nearestBare || failed) {
			Point sourceSize;
			Check(DrawTownViewVegetationDiagnostic(diagnostic, index, &sourceSize), "export renderer original tree composition and explicit opacity");
			ExportVegetationVolume(diagnostic, sourceSize, *mesh, group, index, failed, output);
		}
	}
	Record("INFO actual vegetation volume audit groups=" + std::to_string(groups.size()) + " failures=" + std::to_string(failedCount));
	Check(NativeSceneState() == original, "actual renderer vegetation mesh diagnostics preserve native state");
	return failedCount == 0;
}

bool CheckRenderedPropVolumes(const std::filesystem::path &output)
{
	const std::string original = NativeSceneState();
	const auto &groups = GetTownPropGroups();
	std::ofstream metadata(output / "rock-volume-audit.json");
	metadata << "{\"geometry\":\"actual renderer cache, not a diagnostic reconstruction\",\"objects\":[";
	size_t failedCount = 0;
	for (size_t index = 0; index < groups.size(); ++index) {
		const auto *mesh = TownViewPropVolume(index);
		Check(mesh != nullptr, "renderer provides its actual complete rock volume index=" + std::to_string(index));
		bool failed = false;
		try {
			CheckVolumeMesh(*mesh, "rendered native rock index=" + std::to_string(index));
		} catch (const std::runtime_error &) {
			failed = true;
			++failedCount;
		}
		const auto &group = groups[index];
		Record("INFO native rock index=" + std::to_string(index) + " foot=" + std::to_string(group.referenceFootpoint.x)
			+ "," + std::to_string(group.referenceFootpoint.y) + " source cells=" + std::to_string(group.sourceTiles.size())
			+ " hidden by architecture=" + std::to_string(TownPropHiddenByArchitecture(group)));
		if (index != 0)
			metadata << ',';
		metadata << "{\"index\":" << index << ",\"foot\":[" << group.referenceFootpoint.x << ',' << group.referenceFootpoint.y
			<< "],\"triangles\":" << mesh->triangles.size() << ",\"depth\":" << mesh->depth
			<< ",\"hiddenByArchitecture\":" << (TownPropHiddenByArchitecture(group) ? "true" : "false")
			<< ",\"closedVolumeAudit\":" << (failed ? "false" : "true") << '}';
	}
	metadata << "]}\n";
	Check(metadata.good(), "record actual original rock mesh audit independently of screenshot colors");
	Check(NativeSceneState() == original, "rendering and auditing complete rock meshes preserve native map and collision state");
	return failedCount == 0;
}

Point FindTurntableTree()
{
	Point result { -1, -1 };
	int best = std::numeric_limits<int>::max();
	for (int y = 10; y < 84; ++y) {
		for (int x = 10; x < 84; ++x) {
			const int sprite = dSpecial[x][y] - 1;
			if (sprite < 0 || !pSpecialCels || static_cast<unsigned>(sprite) >= pSpecialCels->numSprites() || (*pSpecialCels)[sprite].height() < 80)
				continue;
			const int distance = std::abs(x - 55) + std::abs(y - 70);
			if (distance < best) {
				result = { x, y };
				best = distance;
			}
		}
	}
	Check(result.x >= 0, "locate an actual full-size native tree for volume and turntable diagnostics");
	return result;
}

void CheckNativeSpriteVolumes(Point treeTile)
{
	const std::string state = NativeSceneState();
	const auto actor = DecodeVolumeSprite(MyPlayer->currentSprite());
	const auto tree = DecodeVolumeSprite((*pSpecialCels)[dSpecial[treeTile.x][treeTile.y] - 1]);
	const uint32_t seed = static_cast<uint32_t>(treeTile.x * 65537 + treeTile.y);
	const auto actorMesh = BuildTownActorVolume(actor.view());
	const auto treeMesh = BuildTownTreeVolume(tree.view(), seed, false);
	const auto foliageMesh = BuildTownTreeVolume(tree.view(), seed, true);
	CheckVolumeMesh(actorMesh, "original warrior sprite volume");
	CheckVolumeMesh(treeMesh, "original tree branch volume");
	CheckVolumeMesh(foliageMesh, "original tree foliage volume");
	Check(VolumeGeometryState(actorMesh) == VolumeGeometryState(BuildTownActorVolume(actor.view()))
			&& VolumeGeometryState(treeMesh) == VolumeGeometryState(BuildTownTreeVolume(tree.view(), seed, false))
			&& VolumeGeometryState(foliageMesh) == VolumeGeometryState(BuildTownTreeVolume(tree.view(), seed, true)),
		"real native sprite volumes rebuild deterministically without depending on camera direction");
	Check(NativeSceneState() == state, "volume construction preserves original town and actor state");
}

struct ArchitectureCoverage {
	int tested = 0;
	int covered = 0;
	int entityOccluded = 0;
	int holes = 0;
};

ArchitectureCoverage CheckArchitectureCoverage(int target, const Surface &out)
{
	ArchitectureCoverage result;
	if (target < 0)
		return result;
	const auto &scene = GetTownScene();
	const auto camera = GetTownViewCameraState();
	constexpr double HeightScale = 0.816496580927726;
	const double focal = 45.25483399593904 * 22 / camera.distance;
	const double cy = std::cos(camera.yaw), sy = std::sin(camera.yaw);
	const double cp = std::cos(camera.pitch), sp = std::sin(camera.pitch);
	const RayVector right { sy, 0, -cy };
	const RayVector up { -cy * sp, cp, -sy * sp };
	const RayVector eyeDirection { cy * cp, sp, sy * cp };
	const RayVector targetPoint { ViewPosition.x + camera.offsetX, 0, ViewPosition.y + camera.offsetZ };
	const RayVector eye = targetPoint + eyeDirection * 256;
	const Point anchor = GetScreenPosition(ViewPosition) + Displacement { 32, 0 };
	std::set<std::pair<int, int>> samples;
	for (const auto &triangle : TownSceneExteriorTriangles(scene[target])) {
		for (const std::array<double, 3> weights : { std::array<double, 3> { 0.333333, 0.333333, 0.333334 },
			std::array<double, 3> { 0.2, 0.3, 0.5 }, std::array<double, 3> { 0.5, 0.2, 0.3 } }) {
			RayVector world { 0, 0, 0 };
			for (size_t i = 0; i < 3; ++i)
				world = world + RayPosition(triangle.vertices[i]) * weights[i];
			world.y *= HeightScale;
			const RayVector relative = world - targetPoint;
			const int x = static_cast<int>(std::floor(anchor.x + focal * RayDot(relative, right)));
			const int y = static_cast<int>(std::floor(anchor.y - focal * RayDot(relative, up)));
			if (x >= 0 && x < out.w() && y >= 0 && y < gnViewportHeight)
				samples.emplace(x, y);
		}
	}
	for (const auto &[x, y] : samples) {
		RayVector origin = eye + right * ((x + 0.5 - anchor.x) / focal) + up * ((anchor.y - y - 0.5) / focal);
		origin.y /= HeightScale;
		const RayVector direction { -eyeDirection.x, -eyeDirection.y / HeightScale, -eyeDirection.z };
		double nearest = std::numeric_limits<double>::max();
		double hitU = 0, hitV = 0;
		int expected = -1;
		for (size_t model = 0; model < scene.size(); ++model) {
			for (const auto &triangle : scene[model].triangles) {
				double distance, u, v;
				if (RayTriangle(origin, direction, triangle, distance, u, v) && distance < nearest) {
					nearest = distance;
					expected = static_cast<int>(model);
					hitU = u;
					hitV = v;
				}
			}
		}
		// Exclude the projected silhouette and triangle seams from rasterization
		// tolerance; the remaining samples are strictly inside real surfaces.
		if (expected != target || std::min({ hitU, hitV, 1 - hitU - hitV }) < 0.04)
			continue;
		++result.tested;
		if (TownViewArchitectureAt({ x, y }) == expected) {
			++result.covered;
			continue;
		}
		Point tile;
		int npc, item, player;
		const bool picked = Pick({ x, y }, tile, npc, item, player);
		const double renderedDepth = TownViewDepthAt({ x, y });
		// Every live object, tree, and native prop can legitimately obscure the
		// tested building. Its actual depth must be strictly closer than the ray
		// intersection: ground behind a missing face never excuses that hole.
		if (picked && std::isfinite(renderedDepth) && renderedDepth > 0 && renderedDepth < nearest - 0.001) {
			++result.entityOccluded;
			continue;
		}
		++result.holes;
		if (result.holes <= 3)
			Record("INFO missing architectural coverage model=" + std::to_string(target) + " screen=" + std::to_string(x) + "," + std::to_string(y)
				+ " owner=" + std::to_string(TownViewArchitectureAt({ x, y })) + " rendered depth=" + std::to_string(renderedDepth)
				+ " expected depth=" + std::to_string(nearest) + " picked=" + std::to_string(picked)
				+ (picked ? " tile=" + std::to_string(tile.x) + "," + std::to_string(tile.y) : ""));
	}
	return result;
}

void RecordCabinScenerySources(const Surface &out, const std::filesystem::path &directory)
{
	// Fixed screen regions from the native-scale east-cabin diagnostic. The
	// selected source cells identify actual visible props, not guessed colors.
	struct Region { const char *name; int left, top, right, bottom; };
	const std::array<Region, 2> regions { Region { "left-prism", 140, 135, 235, 330 },
		Region { "right-prism", 400, 285, 505, 490 } };
	std::ofstream metadata(directory / "cabin-scenery-sources.json");
	metadata << "{\"view\":\"raw native-scale orbit0\",\"regions\":[";
	bool firstRegion = true;
	for (const auto &region : regions) {
		std::map<std::pair<int, int>, int> sourceCounts;
		for (int y = region.top; y < std::min<int>(region.bottom, gnViewportHeight); y += 2) {
			for (int x = region.left; x < std::min<int>(region.right, out.w()); x += 2) {
				Point tile;
				int npc, item, player;
				if (TownViewArchitectureAt({ x, y }) >= 0 || !Pick({ x, y }, tile, npc, item, player)
					|| npc >= 0 || item >= 0 || player >= 0)
					continue;
				// Exclude ground: samples need to sit visibly above the selected
				// tile's ground center to be useful for diagnosing tall scenery.
				if (y < TownViewScreenPosition(tile).y - 24)
					++sourceCounts[{ tile.x, tile.y }];
			}
		}
		if (!firstRegion)
			metadata << ',';
		firstRegion = false;
		metadata << "{\"name\":\"" << region.name << "\",\"sources\":[";
		bool firstSource = true;
		for (const auto &[position, count] : sourceCounts) {
			const Point tile { position.first, position.second };
			const auto piece = dPiece[tile.x][tile.y];
			const bool vegetation = TownVegetationReplacesTile(tile);
			Record("INFO cabin scenery " + std::string(region.name) + " selected=" + std::to_string(tile.x) + "," + std::to_string(tile.y)
				+ " piece=" + std::to_string(piece) + " SOL=" + std::to_string(static_cast<int>(SOLData[piece]))
				+ " special=" + std::to_string(static_cast<int>(dSpecial[tile.x][tile.y])) + " vegetation=" + std::to_string(vegetation)
				+ " sampled pixels=" + std::to_string(count));
			if (!firstSource)
				metadata << ',';
			firstSource = false;
			metadata << "{\"tile\":[" << tile.x << ',' << tile.y << "],\"piece\":" << piece
				<< ",\"sol\":" << static_cast<int>(SOLData[piece]) << ",\"special\":" << static_cast<int>(dSpecial[tile.x][tile.y])
				<< ",\"vegetation\":" << (vegetation ? "true" : "false") << ",\"sampledPixels\":" << count << '}';
		}
		metadata << "]}";
	}
	metadata << "],\"windowRockProbes\":[";
	const auto &props = GetTownPropGroups();
	bool firstProbe = true;
	for (Point screen : { Point { 224, 335 }, Point { 220, 331 }, Point { 224, 331 }, Point { 228, 331 },
		Point { 220, 335 }, Point { 228, 335 }, Point { 220, 339 }, Point { 224, 339 }, Point { 228, 339 } }) {
		Point tile { -1, -1 };
		int npc, item, player;
		const bool picked = Pick(screen, tile, npc, item, player);
		const int architecture = TownViewArchitectureAt(screen);
		const int piece = picked ? dPiece[tile.x][tile.y] : -1;
		const bool sceneReplaced = picked && TownSceneReplacesTile(tile);
		int propGroup = -1;
		for (size_t index = 0; picked && index < props.size(); ++index) {
			if (props[index].referenceFootpoint == tile
				|| std::find(props[index].sourceTiles.begin(), props[index].sourceTiles.end(), tile) != props[index].sourceTiles.end()) {
				propGroup = static_cast<int>(index);
				break;
			}
		}
		Record("INFO cabin window rock probe screen=" + std::to_string(screen.x) + "," + std::to_string(screen.y)
			+ " picked=" + std::to_string(picked) + " tile=" + std::to_string(tile.x) + "," + std::to_string(tile.y)
			+ " piece=" + std::to_string(piece) + " prop group=" + std::to_string(propGroup)
			+ " scene replaced=" + std::to_string(sceneReplaced) + " architecture=" + std::to_string(architecture)
			+ " depth=" + std::to_string(TownViewDepthAt(screen)));
		if (!firstProbe)
			metadata << ',';
		firstProbe = false;
		metadata << "{\"screen\":[" << screen.x << ',' << screen.y << "],\"picked\":" << (picked ? "true" : "false")
			<< ",\"tile\":[" << tile.x << ',' << tile.y << "],\"piece\":" << piece << ",\"propGroup\":" << propGroup
			<< ",\"sceneReplaced\":" << (sceneReplaced ? "true" : "false") << ",\"architecture\":" << architecture
			<< ",\"npc\":" << npc << ",\"item\":" << item << ",\"player\":" << player;
		if (propGroup >= 0) {
			const auto &group = props[propGroup];
			metadata << ",\"propFoot\":[" << group.referenceFootpoint.x << ',' << group.referenceFootpoint.y << "],\"sourceTiles\":[";
			for (size_t source = 0; source < group.sourceTiles.size(); ++source) {
				const Point sourceTile = group.sourceTiles[source];
				if (source != 0)
					metadata << ',';
				metadata << "{\"tile\":[" << sourceTile.x << ',' << sourceTile.y << "],\"piece\":" << dPiece[sourceTile.x][sourceTile.y]
					<< ",\"sceneReplaced\":" << (TownSceneReplacesTile(sourceTile) ? "true" : "false") << '}';
			}
			metadata << ']';
		}
		metadata << '}';
	}
	metadata << "]}\n";
	Check(metadata.good(), "record real source-cell picks and window rock probes without changing fixture or geometry");
}

void RecordPlayerArchitectureOcclusion(const Surface &out, const TownVolumeMesh &body, int degrees)
{
	constexpr double HeightScale = 0.816496580927726;
	constexpr double InverseSqrt2 = 0.7071067811865475;
	const auto camera = GetTownViewCameraState();
	const double focal = 45.25483399593904 * 22 / camera.distance;
	const double cy = std::cos(camera.yaw), sy = std::sin(camera.yaw);
	const double cp = std::cos(camera.pitch), sp = std::sin(camera.pitch);
	const RayVector right { sy, 0, -cy }, up { -cy * sp, cp, -sy * sp };
	const RayVector eyeDirection { cy * cp, sp, sy * cp };
	const RayVector target { ViewPosition.x + camera.offsetX, 0, ViewPosition.y + camera.offsetZ };
	const RayVector eye = target + eyeDirection * 256;
	const Point anchor = GetScreenPosition(ViewPosition) + Displacement { 32, 0 };
	std::vector<TownSceneTriangle> actorTriangles;
	actorTriangles.reserve(body.triangles.size());
	std::set<std::pair<int, int>> samples;
	for (size_t index = 0; index < body.triangles.size(); ++index) {
		const auto &source = body.triangles[index];
		TownSceneTriangle triangle {};
		RayVector center { 0, 0, 0 };
		for (size_t vertex = 0; vertex < 3; ++vertex) {
			const auto &v = source.vertices[vertex];
			triangle.vertices[vertex] = { MyPlayer->position.tile.x + static_cast<float>((v.x + v.z) * InverseSqrt2),
				v.height, MyPlayer->position.tile.y + static_cast<float>((-v.x + v.z) * InverseSqrt2), v.u, v.v };
			center = center + RayPosition(triangle.vertices[vertex]) * (1.0 / 3);
		}
		actorTriangles.push_back(triangle);
		if (index % std::max<size_t>(1, body.triangles.size() / 128) != 0)
			continue;
		center.y *= HeightScale;
		const RayVector relative = center - target;
		const int x = static_cast<int>(std::floor(anchor.x + focal * RayDot(relative, right)));
		const int y = static_cast<int>(std::floor(anchor.y - focal * RayDot(relative, up)));
		if (x >= 0 && x < out.w() && y >= 0 && y < gnViewportHeight)
			samples.emplace(x, y);
	}
	int visible = 0, architectureOccluded = 0, otherOccluded = 0, unexplained = 0;
	std::set<int> occludingModels;
	const auto &scene = GetTownScene();
	for (const auto &[x, y] : samples) {
		RayVector origin = eye + right * ((x + 0.5 - anchor.x) / focal) + up * ((anchor.y - y - 0.5) / focal);
		origin.y /= HeightScale;
		const RayVector direction { -eyeDirection.x, -eyeDirection.y / HeightScale, -eyeDirection.z };
		double actorDepth = std::numeric_limits<double>::infinity(), actorU = 0, actorV = 0;
		for (const auto &triangle : actorTriangles) {
			double distance, u, v;
			if (RayTriangle(origin, direction, triangle, distance, u, v) && distance < actorDepth) {
				actorDepth = distance;
				actorU = u;
				actorV = v;
			}
		}
		if (!std::isfinite(actorDepth) || std::min({ actorU, actorV, 1 - actorU - actorV }) < 0.04)
			continue;
		Point tile;
		int npc, item, player;
		const bool picked = Pick({ x, y }, tile, npc, item, player);
		if (picked && player == MyPlayerId) {
			++visible;
			continue;
		}
		double architectureDepth = std::numeric_limits<double>::infinity();
		int occluder = -1;
		for (size_t model = 0; model < scene.size(); ++model) {
			for (const auto &triangle : TownSceneExteriorTriangles(scene[model])) {
				double distance, u, v;
				if (RayTriangle(origin, direction, triangle, distance, u, v) && distance < architectureDepth) {
					architectureDepth = distance;
					occluder = static_cast<int>(model);
				}
			}
		}
		const double renderedDepth = TownViewDepthAt({ x, y });
		if (occluder >= 0 && architectureDepth < actorDepth - 0.001 && TownViewArchitectureAt({ x, y }) == occluder
			&& std::abs(renderedDepth - architectureDepth) < 0.001) {
			++architectureOccluded;
			occludingModels.insert(occluder);
		} else if (picked && renderedDepth < actorDepth - 0.001) {
			++otherOccluded;
		} else {
			++unexplained;
		}
	}
	Record("INFO cabin actor independent ray evidence orbit=" + std::to_string(degrees) + " visible=" + std::to_string(visible)
		+ " architecture occluded=" + std::to_string(architectureOccluded) + " other closer surfaces=" + std::to_string(otherOccluded)
		+ " unexplained=" + std::to_string(unexplained));
	for (int model : occludingModels)
		Record("INFO cabin actor verified occluder model=" + std::to_string(model) + " kind=" + std::to_string(static_cast<int>(scene[model].kind)));
}

void CheckCabinDoorApertureGeometry(const TownSceneModel &model)
{
	const auto &interior = *model.cabinInterior;
	const auto door = std::find_if(interior.openings.begin(), interior.openings.end(), [](const TownCabinOpening &opening) {
		return opening.kind == TownCabinOpeningKind::Window && opening.aperture.plane == TownLightPlane::X;
	});
	Check(door != interior.openings.end() && door->aperture.polygonSides == 0 && !door->woodenMuntins
	        && std::abs(door->aperture.coordinate - interior.roomMaximum.x) < 0.00001F,
	    "door window declares a real rectangular inner-wall portal without replacing its imported divisions");
	const auto blocked = [](const std::vector<TownSceneTriangle> &triangles, float z, float height) {
		const RayVector origin { 74, height, z }, direction { -1.10, 0, 0 };
		for (const auto &triangle : triangles) {
			double distance, u, v;
			if (RayTriangle(origin, direction, triangle, distance, u, v) && distance < 1)
				return true;
		}
		return false;
	};
	// Actual imported holes are irregular; sample the measured clear regions,
	// rather than assuming a symmetric four-pane window or testing the bars.
	for (const auto [z, height] : std::array<std::pair<float, float>, 3> {
		     std::pair { 67.35F, 1.32F }, { 67.55F, 1.27F }, { 67.42F, 1.15F } }) {
		Check(!blocked(model.triangles, z, height) && !blocked(interior.exteriorTriangles, z, height)
		        && !blocked(interior.interiorTriangles, z, height),
		    "measured door-window ray traverses the original opening, tunnel and inner shell at z=" + std::to_string(z)
		        + " height=" + std::to_string(height));
	}
	for (const auto [z, height] : std::array<std::pair<float, float>, 4> {
		     std::pair { 67.48F, 1.28F }, { 67.605F, 1.29F }, { 67.30F, 1.22F }, { 67.45F, 0.70F } }) {
		Check(blocked(model.triangles, z, height) && blocked(interior.exteriorTriangles, z, height),
		    "original door divisions, frame and closed lower door retain real opaque geometry at z=" + std::to_string(z)
		        + " height=" + std::to_string(height));
	}
	const auto sameTriangle = [](const TownSceneTriangle &a, const TownSceneTriangle &b) {
		for (size_t i = 0; i < 3; ++i) {
			const auto &v = a.vertices[i], &w = b.vertices[i];
			if (v.x != w.x || v.height != w.height || v.z != w.z || v.u != w.u || v.v != w.v)
				return false;
		}
		return a.normal.x == b.normal.x && a.normal.height == b.normal.height && a.normal.z == b.normal.z
		    && a.material == b.material && a.surfaceDetail == b.surfaceDetail && a.surfaceRole == b.surfaceRole
		    && a.sourceTile == b.sourceTile && a.pickTile == b.pickTile && a.nativeProjection == b.nativeProjection;
	};
	size_t unchangedDoorTriangles = 0;
	for (const auto &triangle : model.triangles) {
		bool intersects = true;
		for (int axis = 0; axis < 3; ++axis) {
			float low = std::numeric_limits<float>::infinity(), high = -low;
			for (const auto &v : triangle.vertices) {
				const float coordinate = axis == 0 ? v.x : (axis == 1 ? v.height : v.z);
				low = std::min(low, coordinate);
				high = std::max(high, coordinate);
			}
			const std::array<float, 3> minimum { 72.94F, 1.0F, 67.30F }, maximum { 73.20F, 1.45F, 67.65F };
			intersects = intersects && high >= minimum[axis] && low <= maximum[axis];
		}
		if (!intersects)
			continue;
		Check(std::any_of(interior.exteriorTriangles.begin(), interior.exteriorTriangles.end(), [&](const TownSceneTriangle &copy) {
			return sameTriangle(triangle, copy);
		}), "each imported triangle around the door window retains its exact geometry, UVs and material metadata");
		++unchangedDoorTriangles;
	}
	Check(model.triangles.size() == 5783 && unchangedDoorTriangles > 0,
	    "the selected 5783-triangle source remains intact and the door-window exterior is copied exactly");
	Record("INFO door window exact source triangles preserved=" + std::to_string(unchangedDoorTriangles));
}

void CheckCabinInteriorLight(const Surface &out, int modelIndex, const std::filesystem::path &output, int orbit)
{
	const auto &model = GetTownScene()[modelIndex];
	if (!model.cabinInterior)
		return;
	const std::string state = NativeSceneState();
	const auto directory = output / "cabin-interior" / ("orbit-" + std::to_string(orbit));
	std::filesystem::create_directories(directory);
	OwnedSurface lit(out.w(), gnViewportHeight);
	SDL_SetPaletteColors(lit.surface->format->palette, logical_palette.data(), 0, 256);
	for (int y = 0; y < gnViewportHeight; ++y)
		std::memcpy(lit.at(0, y), out.at(0, y), out.w());
	SavePng(lit, directory / "fire-on.png");
	SetTownViewCabinFireEnabledForDiagnostics(false);
	Check(DrawTownView(out, true), "draw physical cabin interior with candle sources and flame emission disabled");
	SavePng(out.subregionY(0, gnViewportHeight), directory / "fire-off.png");
	int changed = 0, warm = 0, outsideOwner = 0;
	for (int y = 0; y < gnViewportHeight; ++y) {
		for (int x = 0; x < out.w(); ++x) {
			if (lit[{ x, y }] == out[{ x, y }])
				continue;
			++changed;
			outsideOwner += TownViewArchitectureAt({ x, y }) != modelIndex ? 1 : 0;
			const auto color = logical_palette[lit[{ x, y }]];
			warm += color.r > 30 && color.r >= color.g && color.g > color.b * 1.5 ? 1 : 0;
		}
	}
	const auto &interior = *model.cabinInterior;
	const TownLightOccluder room { interior.roomMinimum, interior.roomMaximum, interior.apertures };
	Check(interior.openings.size() == 3 && interior.fireSources.size() == 2,
		"review cabin declares front, rear and door windows plus two physical candle sources");
	CheckCabinDoorApertureGeometry(model);
	const auto openingPoint = [](const TownLightAperture &aperture, float coordinate, float u, float v) -> TownLightVector {
		if (aperture.plane == TownLightPlane::X)
			return { coordinate, v, u };
		if (aperture.plane == TownLightPlane::Height)
			return { u, coordinate, v };
		return { u, v, coordinate };
	};
	for (const auto &opening : interior.openings) {
		const auto &aperture = opening.aperture;
		const float centerU = (aperture.minU + aperture.maxU) * 0.5F;
		const float centerV = (aperture.minV + aperture.maxV) * 0.5F;
		const TownLightVector center = openingPoint(aperture, aperture.coordinate, centerU, centerV);
		const auto &source = interior.fireSources.front().light.position;
		const auto extendFromSource = [&](TownLightVector point) -> TownLightVector {
			return { source.x + (point.x - source.x) * 1.25F,
				source.height + (point.height - source.height) * 1.25F, source.z + (point.z - source.z) * 1.25F };
		};
		Check(TownPointLightVisibility(source, extendFromSource(center), std::span<const TownLightOccluder>(&room, 1)) == 1,
			"fire visibility passes through each declared window, including the rear face");
		const TownLightVector corner = aperture.polygonSides != 0
		    ? openingPoint(aperture, aperture.coordinate, centerU + (aperture.maxU - centerU) * 0.95F, centerV + (aperture.maxV - centerV) * 0.95F)
		    : openingPoint(aperture, aperture.coordinate, aperture.maxU + (aperture.maxU - aperture.minU) * 0.1F, centerV);
		Check(TownPointLightVisibility(source, extendFromSource(corner), std::span<const TownLightOccluder>(&room, 1)) == 0,
			"inner wall outside the declared polygon or rectangle blocks candle light");
		if (aperture.plane == TownLightPlane::X)
			continue; // Its imported divisions and irregular clear panes were ray-tested above.
		// Independent triangle rays traverse four panes and the full wall depth.
		const float directionSign = aperture.coordinate > opening.outerCoordinate ? 1.0F : -1.0F;
		bool continuous = true;
		for (float du : { -(aperture.maxU - centerU) * 0.35F, (aperture.maxU - centerU) * 0.35F }) {
			for (float dv : { -(aperture.maxV - centerV) * 0.35F, (aperture.maxV - centerV) * 0.35F }) {
				const auto originPoint = openingPoint(aperture, opening.outerCoordinate - directionSign * 0.2F, centerU + du, centerV + dv);
				const auto destinationPoint = openingPoint(aperture, aperture.coordinate + directionSign * 0.05F, centerU + du, centerV + dv);
				const RayVector origin { originPoint.x, originPoint.height, originPoint.z };
				const RayVector direction { destinationPoint.x - originPoint.x, destinationPoint.height - originPoint.height, destinationPoint.z - originPoint.z };
				const auto blocks = [&](const auto &triangles) {
					for (const auto &triangle : triangles) {
						double distance, u, v;
						if (RayTriangle(origin, direction, triangle, distance, u, v) && distance < 1)
							return true;
					}
					return false;
				};
				continuous = continuous && !blocks(interior.exteriorTriangles) && !blocks(interior.interiorTriangles);
			}
		}
		Check(continuous, "independent pane rays prove each cut continues through exterior and inner wall thickness");
	}
	Check(changed > 16 && warm > 8 && outsideOwner == 0,
		"actual point light changes warm interior pixels without lighting unrelated geometry");
	std::ofstream report(directory / "interior-light.json");
	report << "{\"method\":\"actual same-camera runtime point-source plus emission on/off; source albedo and exterior unchanged\","
		<< "\"changedPixels\":" << changed << ",\"warmChangedPixels\":" << warm << ",\"changesOutsideCabinOwner\":" << outsideOwner
		<< ",\"interiorTriangles\":" << interior.interiorTriangles.size() << ",\"openings\":" << interior.openings.size()
		<< ",\"clippedSourceTriangles\":" << interior.clippedSourceTriangles << ",\"fireLinearRGB\":[" << interior.fireColor.red << ',' << interior.fireColor.green << ',' << interior.fireColor.blue
		<< "],\"flickerVariation\":0.06,\"frozenVisualTimeSeconds\":0,\"sources\":[";
	for (size_t i = 0; i < interior.fireSources.size(); ++i) {
		const auto &source = interior.fireSources[i];
		const auto &light = source.light;
		report << (i == 0 ? "" : ",") << "{\"kind\":\"candle\",\"position\":[" << light.position.x << ',' << light.position.height << ',' << light.position.z
			<< "],\"radius\":" << light.radius << ",\"intensity\":" << light.intensity << ",\"flickerSeed\":" << source.flickerSeed
			<< ",\"emissiveTriangles\":" << source.emissiveTriangles.size() << '}';
	}
	report << "]}\n";
	Check(report.good(), "record actual candle illumination, apertures and geometry evidence");
	SetTownViewCabinFireEnabledForDiagnostics(true);
	Check(DrawTownView(out, true), "restore production candle lights and emission after the on/off fixture");
	int repeatDifferences = 0;
	for (int y = 0; y < gnViewportHeight; ++y)
		for (int x = 0; x < out.w(); ++x)
			repeatDifferences += lit[{ x, y }] != out[{ x, y }] ? 1 : 0;
	Check(repeatDifferences == 0, "frozen candle time produces identical actual runtime frames");
	if (orbit == 0) {
		int animatedChanges = 0, unrelatedChanges = 0;
		for (const double time : { 3.0, 17.0 }) {
			SetTownViewFireTimeForDiagnostics(time);
			Check(DrawTownView(out, true), "advance candle render time without changing simulation time");
			SavePng(out.subregionY(0, gnViewportHeight), directory / ("fire-time-" + std::to_string(static_cast<int>(time)) + ".png"));
			for (int y = 0; y < gnViewportHeight; ++y) {
				for (int x = 0; x < out.w(); ++x) {
					if (lit[{ x, y }] == out[{ x, y }])
						continue;
					++animatedChanges;
					unrelatedChanges += TownViewArchitectureAt({ x, y }) != modelIndex ? 1 : 0;
				}
			}
		}
		Check(animatedChanges > 0 && unrelatedChanges == 0,
			"actual palette-mapped candle flicker changes visible pixels only inside its building");
		std::ofstream flicker(directory / "fire-animation.json");
		flicker << "{\"frozenTimesSeconds\":[0,3,17],\"sumChangedPixelsAgainstTimeZero\":" << animatedChanges
			<< ",\"changesOutsideCabinOwner\":" << unrelatedChanges << "}\n";
		Check(flicker.good(), "record visible candle animation after final palette mapping");
		SetTownViewFireTimeForDiagnostics(0);
		Check(DrawTownView(out, true), "restore the frozen production candle fixture after animation evidence");
	}
	Check(NativeSceneState() == state, "interior illumination fixture preserves the native game and collision state");
}

void CaptureObjectTurntables(const Surface &originalOut, const std::filesystem::path &output)
{
	const auto directory = output / "turntables";
	std::filesystem::create_directories(directory);
	const std::string initialState = NativeSceneState();
	const int savedScreenWidth = gnScreenWidth;
	const int savedScreenHeight = gnScreenHeight;
	const int savedViewportHeight = gnViewportHeight;
	// Keep native pixel scale, but provide enough vertical room for entire roofs
	// and trees. The backing allocation includes the real engine panel rows.
	constexpr int FrameSize = 640;
	OwnedSurface out(FrameSize, FrameSize + GetMainPanel().size.height);
	SDL_SetPaletteColors(out.surface->format->palette, logical_palette.data(), 0, 256);
	gnScreenWidth = out.w();
	gnScreenHeight = out.h();
	CalculatePanelAreas();
	CalcViewportGeometry();
	Check(gnViewportHeight == FrameSize && out.h() >= gnViewportHeight, "turntable native target includes panel rows and a full 640px world frame");
	const ActorPosition savedPosition = MyPlayer->position;
	const Point savedView = ViewPosition;
	std::vector<uint8_t> savedFlags(sizeof(dFlags)), savedPlayers(sizeof(dPlayer));
	std::memcpy(savedFlags.data(), dFlags, sizeof(dFlags));
	std::memcpy(savedPlayers.data(), dPlayer, sizeof(dPlayer));
	const Point tree = FindTurntableTree();
	CheckNativeSpriteVolumes(tree);
	const TownVolumeMesh diagnosticActorBody = CheckRealActorBodies(output);
	const bool vegetationValid = CheckRenderedVegetationVolumes(output);
	const bool propsValid = CheckRenderedPropVolumes(output);
	CheckClosedArchitecture();
	int cabin = -1, westCabin = -1, well = -1;
	const auto &scene = GetTownScene();
	for (size_t i = 0; i < scene.size(); ++i) {
		if (scene[i].kind == TownSceneKind::Cabin && scene[i].minTile.x > 60)
			cabin = static_cast<int>(i);
		if (scene[i].kind == TownSceneKind::Cabin && scene[i].minTile.x < 60)
			westCabin = static_cast<int>(i);
		if (scene[i].kind == TownSceneKind::Well)
			well = static_cast<int>(i);
	}
	Check(cabin >= 0 && westCabin >= 0 && well >= 0, "turntable targets are both real cabins and the town well models");
	const auto center = [&](int model) {
		const auto &body = scene[model].physicalBounds;
		return Point { static_cast<int>(std::lround((body.minX + body.maxX) * 0.5F)), static_cast<int>(std::lround((body.minZ + body.maxZ) * 0.5F)) };
	};
	struct Fixture { const char *name; Point focus; Point hero; int model; };
	const std::array<Fixture, 5> fixtures { Fixture { "cabin-east", center(cabin), { 76, 69 }, cabin },
		Fixture { "cabin-west", center(westCabin), { 32, 50 }, westCabin },
		Fixture { "well", center(well), { 63, 72 }, well }, Fixture { "tree", tree, tree + Displacement { 3, 3 }, -1 },
		// This native clearing was selected from SOL/special data, independently
		// of renderer visibility. The cabin fixture retains actual roof occlusion.
		Fixture { "player", { 53, 29 }, { 53, 29 }, -1 } };
	std::ofstream manifest(directory / "turntables.json");
	manifest << "{\"archiveMode\":\"" << (gbIsSpawn ? "shareware" : "retail")
		<< "\",\"native\":\"actual original world backend\",\"raw\":\"forced geometry; never native fallback\","
		<< "\"coverage\":\"independent ray intersections and visible architectural IDs, not pixel color\","
		<< "\"playerFixture\":\"native walkable clearing at 53,29; scenery retained and cabin actor occlusion audited separately\","
		<< "\"width\":" << out.w() << ",\"height\":" << gnViewportHeight << ",\"frames\":[\n";
	bool firstFrame = true;
	bool coverageValid = true;
	bool playerVisible = true;
	constexpr float Pi = 3.14159265358979323846F;
	for (const Fixture &fixture : fixtures) {
		PlaceFixturePlayerNear(fixture.hero);
		if (std::string(fixture.name) == "player")
			Check(IsTileNotSolid(MyPlayer->position.tile) && dMonster[MyPlayer->position.tile.x][MyPlayer->position.tile.y] == 0
				&& dSpecial[MyPlayer->position.tile.x][MyPlayer->position.tile.y] == 0, "360 player fixture is an actual unoccupied native walkable clearing");
		ViewPosition = fixture.model < 0 && std::string(fixture.name) == "player" ? Point { MyPlayer->position.tile } : fixture.focus;
		ResetTownViewCamera();
		const std::string frozen = NativeSceneState();
		DrawActualNativeReference(out);
		SavePng(out.subregionY(0, gnViewportHeight), directory / (std::string(fixture.name) + "-native.png"));
		for (int degrees : { -5, 0, 5, 45, 90, 135, 180, 225, 270, 315 }) {
			ResetTownViewCamera();
			OrbitTownView(degrees * Pi / 180, 0);
			Check(DrawTownView(out, true), std::string("draw raw turntable ") + fixture.name + " orbit=" + std::to_string(degrees));
			if (degrees == 0 && std::string(fixture.name) == "cabin-east")
				RecordCabinScenerySources(out, directory);
			if (std::string(fixture.name) == "cabin-east")
				RecordPlayerArchitectureOcclusion(out, diagnosticActorBody, degrees);
			const auto coverage = CheckArchitectureCoverage(fixture.model, out);
			if (fixture.model >= 0) {
				coverageValid = coverageValid && coverage.covered >= 3 && coverage.holes == 0;
				Record(std::string("INFO architectural interior ray coverage ")
					+ fixture.name + " orbit=" + std::to_string(degrees) + " covered=" + std::to_string(coverage.covered)
					+ " occluded=" + std::to_string(coverage.entityOccluded) + " holes=" + std::to_string(coverage.holes));
			}
			int playerPixels = 0;
			int treePixels = 0;
			for (int y = 0; y < gnViewportHeight; ++y) {
				for (int x = 0; x < out.w(); ++x) {
					Point tile;
					int npc, item, player;
					if (Pick({ x, y }, tile, npc, item, player)) {
						playerPixels += player == MyPlayerId ? 1 : 0;
						treePixels += tile == tree && npc < 0 && item < 0 && player < 0 ? 1 : 0;
					}
				}
			}
			if (std::string(fixture.name) == "player")
				playerVisible = playerVisible && playerPixels > 0;
			Check(NativeSceneState() == frozen, "raw turntable rendering preserves native map and actor state");
			const std::string filename = std::string(fixture.name) + "-orbit-" + std::to_string(degrees) + ".png";
			SavePng(out.subregionY(0, gnViewportHeight), directory / filename);
			if ((degrees == 0 || degrees == 180) && std::string(fixture.name) == "cabin-east")
				CheckCabinInteriorLight(out, fixture.model, output, degrees);
			if (!firstFrame)
				manifest << ",\n";
			firstFrame = false;
			manifest << "{\"object\":\"" << fixture.name << "\",\"orbitDegrees\":" << degrees << ",\"file\":\"" << filename
				<< "\",\"focus\":[" << ViewPosition.x << ',' << ViewPosition.y << "],\"heroTile\":["
				<< static_cast<int>(MyPlayer->position.tile.x) << ',' << static_cast<int>(MyPlayer->position.tile.y)
				<< "],\"cameraYaw\":" << GetTownViewCameraState().yaw
				<< ",\"cameraPitch\":" << GetTownViewCameraState().pitch << ",\"cameraDistance\":" << GetTownViewCameraState().distance
				<< ",\"cameraPan\":[" << GetTownViewCameraState().offsetX << ',' << GetTownViewCameraState().offsetZ << ']'
				<< ",\"architectureSamples\":" << coverage.tested << ",\"architectureCovered\":" << coverage.covered
				<< ",\"architectureOccluded\":" << coverage.entityOccluded << ",\"architectureHoles\":" << coverage.holes
				<< ",\"playerPixels\":" << playerPixels << ",\"treeTileSelectionPixels\":" << treePixels << '}';
		}
	}
	manifest << "\n]}\n";
	Check(manifest.good(), "turntable manifest records raw angles, visible IDs, and independent coverage evidence");
	std::memcpy(dFlags, savedFlags.data(), sizeof(dFlags));
	std::memcpy(dPlayer, savedPlayers.data(), sizeof(dPlayer));
	MyPlayer->position = savedPosition;
	ViewPosition = savedView;
	gnScreenWidth = savedScreenWidth;
	gnScreenHeight = savedScreenHeight;
	gnViewportHeight = savedViewportHeight;
	CalculatePanelAreas();
	CalcViewportGeometry();
	ResetTownViewCamera();
	Check(DrawTownView(originalOut, true), "restore original scene and viewport after object turntables");
	Check(NativeSceneState() == initialState, "all turntable fixtures restore original map flags, actor cells, and positions");
	Check(vegetationValid, "all actual renderer tree meshes have closed oriented components; failure sources exported and turntables preserved");
	Check(propsValid, "all actual renderer native rock objects have closed volumes and nonzero depth; turntables preserved");
	Check(playerVisible, "original warrior volume remains selectable at every turntable angle");
	Check(coverageValid, "opaque east/west cabin and well surfaces cover all independent interior ray samples across ten angles");
}

void InitializeTownDiagnostic()
{
	SetTownViewFireTimeForDiagnostics(0);
	HeadlessMode = true;
	gbIsHellfire = false;
	gbIsMultiplayer = false;
	leveltype = DTYPE_TOWN;
	currlevel = 0;
	setlevel = false;
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	gnViewportHeight = 352;
	GetOptions().Graphics.zoom.SetValue(false);
	CalculatePanelAreas();
	CalcViewportGeometry();
	SetRndSeed(0);
	for (auto &quest : Quests)
		quest._qactive = QUEST_NOTAVAIL;
	std::cout << "Loading original archives\n";
	LoadCoreArchives();
	LoadSelectedGameArchive();
	OverridePaths.emplace_back(paths::PrefPath());
	Check(FindAsset("levels\\towndata\\town.cel").ok(), "game archive provides original town CEL");
	// Retail town generation queries this player's unlocked dungeon entrances.
	ViewPosition = { 75, 68 };
	InitializePlayer();
	LoadTown();
	InitTowners();
	dPlayer[MyPlayer->position.tile.x][MyPlayer->position.tile.y] = 1;
	dFlags[MyPlayer->position.tile.x][MyPlayer->position.tile.y] |= DungeonFlag::Lit | DungeonFlag::Visible;
	for (size_t i = 0; i < Towners.size(); ++i) {
		const Point tile = Towners[i].position;
		dMonster[tile.x][tile.y] = static_cast<int16_t>(i + 1);
		dFlags[tile.x][tile.y] |= DungeonFlag::Lit | DungeonFlag::Visible;
	}
	Check(!Towners.empty(), "original Tristram NPC sprites loaded");
}

// A resolution experiment, deliberately separate from the broad smoke suite.
// It measures warm CPU world rendering, not a running game's frame rate.
void RunPresentation(const std::filesystem::path &output)
{
	InitializeTownDiagnostic();
	Check(!IsTownViewActive(), "presentation experiment starts with the optional view disabled");
	ToggleTownView();
	ResetTownViewCamera();
	const auto initialCamera = GetTownViewCameraState();
	const std::string originalState = NativeSceneState();
	const auto cameraUnchanged = [&] {
		const auto camera = GetTownViewCameraState();
		return camera.yaw == initialCamera.yaw && camera.pitch == initialCamera.pitch
		    && camera.distance == initialCamera.distance && camera.offsetX == initialCamera.offsetX
		    && camera.offsetZ == initialCamera.offsetZ;
	};
	const size_t importedModels = std::count_if(GetTownScene().begin(), GetTownScene().end(),
	    [](const TownSceneModel &model) { return model.externalModel; });
	std::ofstream manifest(output / "presentation.json");
	manifest << std::setprecision(9)
		<< "{\"experiment\":\"warm offscreen CPU world rendering; not game FPS\","
		<< "\"archiveMode\":\"" << (gbIsSpawn ? "shareware" : "retail") << "\","
		<< "\"uiDrawn\":false,\"gpuPresentationMeasured\":false,\"windowDpiOrScalingValidated\":false,"
		<< "\"warmupDraws\":3,\"measuredDraws\":5,\"fireTimeSeconds\":0,"
		<< "\"cameraPolicy\":\"original yaw/pitch/distance; no resolution-dependent zoom\","
		<< "\"fieldOfViewEquivalent\":false,\"largerCanvasShowsMoreWorld\":true,"
		<< "\"sourceArtAddsDetail\":false,\"importedModels\":" << importedModels
		<< ",\"focusTile\":[" << ViewPosition.x << ',' << ViewPosition.y << ']'
		<< ",\"playerTile\":[" << static_cast<int>(MyPlayer->position.tile.x) << ',' << static_cast<int>(MyPlayer->position.tile.y) << ']'
		<< ",\"cases\":[\n";
	bool firstCase = true;
	Point previousNativeStep, previousGeometryStep;
	bool havePreviousStep = false;
	for (const Point dimensions : { Point { 960, 540 }, Point { 1280, 720 }, Point { 1920, 1080 } }) {
		gnScreenWidth = dimensions.x;
		gnScreenHeight = dimensions.y;
		CalculatePanelAreas();
		CalcViewportGeometry();
		Check(gnViewportHeight == dimensions.y, "wide presentation canvas retains the real engine world viewport");
		constexpr int GuardRows = 2;
		OwnedSurface allocation(dimensions.x, dimensions.y + GuardRows);
		SDL_SetPaletteColors(allocation.surface->format->palette, logical_palette.data(), 0, 256);
		SDL_FillRect(allocation.surface, nullptr, 255);
		const Surface out = allocation.subregionY(0, dimensions.y);
		const auto guardsIntact = [&] {
			for (int y = out.h(); y < allocation.h(); ++y)
				for (int x = 0; x < allocation.w(); ++x)
					if (allocation[{ x, y }] != 255)
						return false;
			return true;
		};
		for (const bool geometry : { false, true }) {
			const std::string renderer = geometry ? "forced-geometry" : "actual-native";
			const auto draw = [&] {
				ClearWorld(out);
				return geometry ? DrawTownView(out, true) : DrawNativeTownViewReference(out, ViewPosition);
			};
			std::array<double, 3> warmup;
			std::array<double, 5> samples;
			const auto timed = [&] {
				const auto begin = std::chrono::steady_clock::now();
				const bool drawn = draw();
				const auto end = std::chrono::steady_clock::now();
				if (!drawn)
					throw std::runtime_error("presentation world draw failed: " + renderer);
				return std::chrono::duration<double, std::milli>(end - begin).count();
			};
			for (double &milliseconds : warmup)
				milliseconds = timed();
			const auto expectedPixels = ViewportPixels(out);
			bool deterministic = true;
			for (double &milliseconds : samples) {
				milliseconds = timed();
				// Pixel copying and comparisons are outside the measured interval.
				deterministic = deterministic && ViewportPixels(out) == expectedPixels;
			}
			Check(deterministic, renderer + " repeated frames are identical with animation/fire frozen");
			Check(guardsIntact(), renderer + " preserves padding rows beyond the real viewport");
			Check(NativeSceneState() == originalState && cameraUnchanged(), renderer + " resolution draws preserve native state and camera");
			auto sorted = samples;
			std::sort(sorted.begin(), sorted.end());
			double mean = 0;
			for (double milliseconds : samples)
				mean += milliseconds / samples.size();
			const std::string filename = renderer + "-" + std::to_string(out.w()) + "x" + std::to_string(out.h()) + ".png";
			SavePng(out, output / filename);
			const Point nativeCenter = GetScreenPosition(ViewPosition) + Displacement { 32, -16 };
			const Point nativeNext = GetScreenPosition(ViewPosition + Displacement { 1, 0 }) + Displacement { 32, -16 };
			const Point nativeStep { nativeNext.x - nativeCenter.x, nativeNext.y - nativeCenter.y };
			Point geometryCenter, geometryStep;
			size_t pickedPixels = 0, groundPixels = 0, architecturePixels = 0, playerPixels = 0, npcPixels = 0;
			bool validIds = true;
			int visibleGroundRoundtrips = 0;
			if (geometry) {
				geometryCenter = TownViewScreenPosition(ViewPosition);
				const Point next = TownViewScreenPosition(ViewPosition + Displacement { 1, 0 });
				geometryStep = { next.x - geometryCenter.x, next.y - geometryCenter.y };
				for (int y = 0; y < out.h(); ++y) {
					for (int x = 0; x < out.w(); ++x) {
						Point tile;
						int npc, item, player;
						if (!Pick({ x, y }, tile, npc, item, player))
							continue;
						++pickedPixels;
						const int architecture = TownViewArchitectureAt({ x, y });
						architecturePixels += architecture >= 0 ? 1 : 0;
						playerPixels += player >= 0 ? 1 : 0;
						npcPixels += npc >= 0 ? 1 : 0;
						groundPixels += architecture < 0 && player < 0 && npc < 0 && item < 0 ? 1 : 0;
						validIds = validIds && tile.x >= 0 && tile.y >= 0 && tile.x < MAXDUNX && tile.y < MAXDUNY
						    && std::isfinite(TownViewDepthAt({ x, y })) && architecture < static_cast<int>(GetTownScene().size())
						    && (player < 0 || (player < static_cast<int>(Players.size()) && tile == Players[player].position.tile))
						    && (npc < 0 || (npc < static_cast<int>(Towners.size()) && tile == Towners[npc].position));
					}
				}
				for (int y = ViewPosition.y - 10; y <= ViewPosition.y + 10; ++y) {
					for (int x = ViewPosition.x - 10; x <= ViewPosition.x + 10; ++x) {
						const Point expected { x, y };
						if (!InDungeonBounds(expected) || !IsTileNotSolid(expected))
							continue;
						const Point screen = TownViewScreenPosition(expected);
						Point tile;
						int npc, item, player;
						if (out.InBounds(screen) && Pick(screen, tile, npc, item, player) && tile == expected
						    && npc < 0 && item < 0 && player < 0 && TownViewArchitectureAt(screen) < 0)
							++visibleGroundRoundtrips;
					}
				}
				Check(validIds && pickedPixels > 0 && visibleGroundRoundtrips > 0, "presentation geometry retains valid live picking and visible walkable tile roundtrips");
				for (Point screen : { Point { -1, 0 }, Point { out.w(), 0 }, Point { 0, out.h() }, Point { 0, -1 } }) {
					Point tile;
					int npc, item, player;
					Check(!Pick(screen, tile, npc, item, player), "presentation picking rejects points outside the world viewport");
				}
				if (havePreviousStep)
					Check(nativeStep == previousNativeStep && geometryStep == previousGeometryStep,
					    "larger presentation canvases preserve measured native and geometric tile scale");
				previousNativeStep = nativeStep;
				previousGeometryStep = geometryStep;
				havePreviousStep = true;
			}
			if (!firstCase)
				manifest << ",\n";
			firstCase = false;
			manifest << "{\"renderer\":\"" << renderer << "\",\"file\":\"" << filename << "\",\"width\":" << out.w()
				<< ",\"height\":" << out.h() << ",\"viewportHeight\":" << gnViewportHeight
				<< ",\"uiPanelHeight\":" << GetMainPanel().size.height << ",\"uiPanelPosition\":["
				<< GetMainPanel().position.x << ',' << GetMainPanel().position.y << ']'
				<< ",\"worldExtendsBehindPanel\":true,\"paddingGuardRows\":" << GuardRows
				<< ",\"camera\":{\"yaw\":" << initialCamera.yaw << ",\"pitch\":" << initialCamera.pitch
				<< ",\"distance\":" << initialCamera.distance << ",\"pan\":[" << initialCamera.offsetX << ',' << initialCamera.offsetZ << "]}"
				<< ",\"nativeGroundCenter\":[" << nativeCenter.x << ',' << nativeCenter.y << ']'
				<< ",\"nativeTileXStepPixels\":[" << nativeStep.x << ',' << nativeStep.y << ']';
			if (geometry)
				manifest << ",\"geometryGroundCenter\":[" << geometryCenter.x << ',' << geometryCenter.y << ']'
					<< ",\"geometryTileXStepPixels\":[" << geometryStep.x << ',' << geometryStep.y << ']'
					<< ",\"picking\":{\"pickedPixels\":" << pickedPixels << ",\"nonEntityNonArchitecturePixels\":" << groundPixels
					<< ",\"architecturePixels\":" << architecturePixels << ",\"playerPixels\":" << playerPixels
					<< ",\"npcPixels\":" << npcPixels << ",\"visibleWalkableRoundtrips\":" << visibleGroundRoundtrips << ",\"valid\":true}";
			else
				manifest << ",\"picking\":{\"tested\":false,\"reason\":\"native cursor backend requires an interactive game; geometric IDs are not used as native proof\"}";
			manifest << ",\"warmupMilliseconds\":[";
			for (size_t i = 0; i < warmup.size(); ++i)
				manifest << (i == 0 ? "" : ",") << warmup[i];
			manifest << "],\"renderMilliseconds\":[";
			for (size_t i = 0; i < samples.size(); ++i)
				manifest << (i == 0 ? "" : ",") << samples[i];
			manifest << "],\"meanMilliseconds\":" << mean << ",\"medianMilliseconds\":" << sorted[sorted.size() / 2]
				<< ",\"minimumMilliseconds\":" << sorted.front() << ",\"maximumMilliseconds\":" << sorted.back()
				<< ",\"frameDeterministic\":true,\"paddingPreserved\":true,\"statePreserved\":true}";
			Record("INFO presentation " + renderer + " " + std::to_string(out.w()) + "x" + std::to_string(out.h())
			    + " median CPU world draw=" + std::to_string(sorted[sorted.size() / 2]) + " ms");
		}
	}
	manifest << "\n]}\n";
	Check(manifest.good(), "record presentation resolution costs, fixed tile scale and measurement limits");
	SaveTownShadowStats(output / "shadow-map.json");
	SaveTownLightingStats(output / "lighting-state.json");
	Check(NativeSceneState() == originalState && cameraUnchanged(), "complete presentation experiment preserves live data and camera");
	FreeTownerGFX();
}

// This experiment exercises optional world sampling separately from the normal
// smoke suite. The logical canvas, camera, cursor coordinates and UI stay fixed.
void CheckQualityPaletteResolve()
{
	TownViewColorResolve resolve;
	Check(resolve.Resolve({ 7, 8, 9, 10 }) == 7, "unprepared quality color helper has a safe first-sample fallback");
	std::array<SDL_Color, 256> palette;
	palette.fill({ 255, 0, 255, 255 });
	palette[0] = { 0, 0, 0, 255 };
	palette[17] = { 255, 255, 255, 255 };
	palette[89] = { 66, 66, 66, 255 };
	palette[203] = palette[204] = { 132, 132, 132, 255 };
	resolve.Prepare(palette);
	Check(resolve.Resolve({ 0, 0, 0, 0 }) == 0 && resolve.Resolve({ 204, 204, 204, 204 }) == 204,
	    "quality resolve preserves opaque black and exact uniform palette indices");
	Check(resolve.Resolve({ 0, 17, 0, 17 }) == 203 && resolve.Resolve({ 0, 0, 0, 17 }) == 89,
	    "quality resolve averages actual RGB rather than unordered palette indices, with deterministic nearest-color ties");
	palette[203].a = 0;
	resolve.Prepare(palette);
	Check(resolve.Resolve({ 0, 17, 0, 17 }) == 203, "quality color lookup treats palette alpha independently of opaque indexed samples");
	palette[203] = { 255, 0, 0, 255 };
	resolve.Prepare(palette);
	Check(resolve.Resolve({ 0, 17, 0, 17 }) == 204, "quality color lookup refreshes after an RGB palette change");
}

uint64_t QualityHashBytes(const std::vector<uint8_t> &bytes)
{
	uint64_t hash = 14695981039346656037ULL;
	for (uint8_t value : bytes)
		hash = (hash ^ value) * 1099511628211ULL;
	return hash;
}

struct QualityPicking {
	uint64_t hash = 14695981039346656037ULL;
	size_t picked = 0, architecture = 0, player = 0, npc = 0;
	Point probe { -1, -1 };
	bool valid = true;
};

QualityPicking InspectQualityPicking(const Surface &out)
{
	QualityPicking result;
	const auto append = [&](uint32_t value) {
		for (int shift = 0; shift < 32; shift += 8)
			result.hash = (result.hash ^ static_cast<uint8_t>(value >> shift)) * 1099511628211ULL;
	};
	for (int y = 0; y < gnViewportHeight; ++y) {
		for (int x = 0; x < out.w(); ++x) {
			Point tile { -1, -1 };
			int npc, item, player;
			const bool picked = Pick({ x, y }, tile, npc, item, player);
			const int architecture = TownViewArchitectureAt({ x, y });
			const float depth = TownViewDepthAt({ x, y });
			for (int value : { static_cast<int>(picked), tile.x, tile.y, npc, item, player, architecture })
				append(static_cast<uint32_t>(value));
			append(std::bit_cast<uint32_t>(depth));
			if (!picked)
				continue;
			if (result.probe.x < 0)
				result.probe = { x, y };
			++result.picked;
			result.architecture += architecture >= 0 ? 1 : 0;
			result.player += player >= 0 ? 1 : 0;
			result.npc += npc >= 0 ? 1 : 0;
			result.valid = result.valid && InDungeonBounds(tile) && std::isfinite(depth) && depth > 0
			    && architecture >= -1 && architecture < static_cast<int>(GetTownScene().size())
			    && item >= -1 && player >= -1 && npc >= -1
			    && (player < 0 || (player < static_cast<int>(Players.size()) && tile == Players[player].position.tile))
			    && (npc < 0 || (npc < static_cast<int>(Towners.size()) && tile == Towners[npc].position));
		}
	}
	return result;
}

struct QualityRayAudit {
	int tested = 0, matched = 0, occluded = 0, failed = 0;
};

// Independent world-space rays use the actual logical subpixel centers. This
// compares resolved depth against the nearest of four rays, not the central ray
// with a widened tolerance. Live trees/actors may legitimately be closer.
QualityRayAudit AuditQualityArchitecture(const Surface &out, int factor)
{
	QualityRayAudit result;
	const auto camera = GetTownViewCameraState();
	constexpr double HeightScale = 0.816496580927726;
	const int zoom = *GetOptions().Graphics.zoom ? 2 : 1;
	const double focal = 45.25483399593904 * 22 / camera.distance * zoom;
	Point anchor = GetScreenPosition(ViewPosition) + Displacement { 32, 0 };
	anchor = { anchor.x * zoom, anchor.y * zoom };
	if (zoom == 2 && CanPanelsCoverView() && IsLeftPanelOpen())
		anchor.x += SidePanelSize.width;
	const double cy = std::cos(camera.yaw), sy = std::sin(camera.yaw);
	const double cp = std::cos(camera.pitch), sp = std::sin(camera.pitch);
	const RayVector right { sy, 0, -cy }, up { -cy * sp, cp, -sy * sp };
	const RayVector eyeDirection { cy * cp, sp, sy * cp };
	const RayVector target { ViewPosition.x + camera.offsetX, 0, ViewPosition.y + camera.offsetZ };
	const RayVector eye = target + eyeDirection * 256;
	const RayVector direction { -eyeDirection.x, -eyeDirection.y / HeightScale, -eyeDirection.z };
	const auto &scene = GetTownScene();
	std::vector<std::pair<const TownSceneTriangle *, int>> architecture;
	for (size_t model = 0; model < scene.size(); ++model) {
		const auto append = [&](const std::vector<TownSceneTriangle> &triangles) {
			for (const auto &triangle : triangles)
				architecture.emplace_back(&triangle, static_cast<int>(model));
		};
		append(TownSceneExteriorTriangles(scene[model]));
		if (scene[model].cabinInterior) {
			append(scene[model].cabinInterior->interiorTriangles);
			for (const auto &fire : scene[model].cabinInterior->fireSources)
				append(fire.emissiveTriangles);
		}
	}
	std::set<std::pair<int, int>> samples;
	// Distributed triangle centroids provide architectural samples without
	// relying on screenshot colors or assuming ground is never black.
	for (const auto &model : scene) {
		const auto &triangles = TownSceneExteriorTriangles(model);
		const size_t step = std::max<size_t>(1, triangles.size() / 12);
		for (size_t i = 0; i < triangles.size(); i += step) {
			RayVector center { 0, 0, 0 };
			for (const auto &vertex : triangles[i].vertices)
				center = center + RayPosition(vertex) * (1.0 / 3);
			center.y *= HeightScale;
			const RayVector relative = center - target;
			const int x = static_cast<int>(std::floor(anchor.x + focal * RayDot(relative, right)));
			const int y = static_cast<int>(std::floor(anchor.y - focal * RayDot(relative, up)));
			if (x >= 0 && x < out.w() && y >= 0 && y < gnViewportHeight)
				samples.emplace(x, y);
		}
	}
	for (const auto &[x, y] : samples) {
		double nearest = std::numeric_limits<double>::infinity();
		int expected = -1;
		double interior = 0;
		for (int dy = 0; dy < factor; ++dy) {
			for (int dx = 0; dx < factor; ++dx) {
				const double px = x + (dx + 0.5) / factor;
				const double py = y + (dy + 0.5) / factor;
				RayVector origin = eye + right * ((px - anchor.x) / focal) + up * ((anchor.y - py) / focal);
				origin.y /= HeightScale;
				for (const auto &[triangle, model] : architecture) {
					double distance, u, v;
					if (RayTriangle(origin, direction, *triangle, distance, u, v) && distance < nearest) {
						nearest = distance;
						expected = model;
						interior = std::min({ u, v, 1 - u - v });
					}
				}
			}
		}
		if (expected < 0 || interior < 0.04)
			continue; // Silhouette/triangle seams are outside this interior oracle.
		++result.tested;
		Point tile;
		int npc, item, player;
		const bool picked = Pick({ x, y }, tile, npc, item, player);
		const float rendered = TownViewDepthAt({ x, y });
		if (picked && TownViewArchitectureAt({ x, y }) == expected && std::abs(rendered - nearest) < 0.001) {
			++result.matched;
		} else if (picked && TownViewArchitectureAt({ x, y }) < 0
		    && std::isfinite(rendered) && rendered > 0 && rendered < nearest - 0.001) {
			++result.occluded;
		} else {
			++result.failed;
			if (result.failed <= 3)
				Record("INFO quality ray mismatch screen=" + std::to_string(x) + "," + std::to_string(y)
				    + " expected architecture=" + std::to_string(expected) + " actual=" + std::to_string(TownViewArchitectureAt({ x, y }))
				    + " expected depth=" + std::to_string(nearest) + " actual=" + std::to_string(rendered));
		}
	}
	return result;
}

void RunQuality(const std::filesystem::path &output)
{
	InitializeTownDiagnostic();
	Check(!*GetOptions().Graphics.townViewAntialiasing, "optional world antialiasing defaults off");
	CheckQualityPaletteResolve();
	ToggleTownView();
	const auto initialState = NativeSceneState();
	const bool originalCharFlag = CharFlag;
	std::ofstream manifest(output / "quality.json");
	manifest << std::setprecision(9)
	    << "{\"experiment\":\"optional 2x world sampling at fixed logical framing\",\"archiveMode\":\""
	    << (gbIsSpawn ? "shareware" : "retail") << "\",\"uiDrawn\":false,\"gpuPresentationMeasured\":false,"
	    << "\"sourceArtAddsDetail\":false,\"fieldOfViewEquivalent\":true,\"fireTimeSeconds\":0,\"warmupDraws\":3,\"measuredDraws\":5,"
	    << "\"costScope\":\"CPU world draw only, not game FPS; optional 2x requests four samples per logical pixel\","
	    << "\"pickPolicy\":\"nearest finite depth sample and that same sample's entity/architecture record\",\"cases\":[\n";
	struct Case { const char *name; int width, height; bool panelPanZoom, benchmark; };
	const std::array<Case, 4> cases { Case { "native-scale", 640, 480, false, false },
		Case { "odd-canvas", 645, 481, false, false }, Case { "wide-canvas", 960, 540, false, true },
		Case { "left-panel-pan-zoom", 640, 480, true, false } };
	bool firstCase = true;
	bool retainedWorldExported = false;
	for (const auto &test : cases) {
		gnScreenWidth = test.width;
		gnScreenHeight = test.height;
		CharFlag = test.panelPanZoom;
		GetOptions().Graphics.zoom.SetValue(test.panelPanZoom);
		CalculatePanelAreas();
		CalcViewportGeometry();
		ResetTownViewCamera();
		if (test.panelPanZoom) {
			Check(BeginTownViewCameraDrag({ 400, 150 }, true) && UpdateTownViewCameraDrag({ 423, 161 }),
			    "quality fixture accepts logical pan with the left panel open");
			EndTownViewCameraDrag();
			OrbitTownView(0.08F, 0.04F);
			ZoomTownView(1);
		}
		const auto camera = GetTownViewCameraState();
		const auto cameraPreserved = [&] {
			const auto current = GetTownViewCameraState();
			return current.yaw == camera.yaw && current.pitch == camera.pitch && current.distance == camera.distance
			    && current.offsetX == camera.offsetX && current.offsetZ == camera.offsetZ;
		};
		OwnedSurface allocation(test.width, test.height + 2);
		SDL_SetPaletteColors(allocation.surface->format->palette, logical_palette.data(), 0, 256);
		SDL_FillRect(allocation.surface, nullptr, 255);
		const Surface out = allocation.subregionY(0, test.height);
		const auto guardsPreserved = [&] {
			for (int y = gnViewportHeight; y < allocation.h(); ++y)
				for (int x = 0; x < allocation.w(); ++x)
					if (allocation[{ x, y }] != 255)
						return false;
			return true;
		};
		std::vector<uint8_t> baseline;
		QualityPicking baselinePicking;
		std::array<Point, 3> logicalProbes;
		for (int mode = 0; mode < 3; ++mode) {
			const bool quality = mode == 1;
			if (mode != 0) {
				GetOptions().Graphics.townViewAntialiasing.SetValue(quality);
				Point tile;
				int npc, item, player;
				Check(!Pick(baselinePicking.probe, tile, npc, item, player)
				        && TownViewArchitectureAt(baselinePicking.probe) == -1
				        && !std::isfinite(TownViewDepthAt(baselinePicking.probe)) && GetTownViewHighResolutionFrame() == nullptr,
				    "quality option change invalidates stale selection, ownership and depth before redraw");
			} else {
				GetOptions().Graphics.townViewAntialiasing.SetValue(false);
			}
			Check(DrawTownView(out, true), std::string("draw quality fixture ") + test.name + (quality ? " 2x" : " 1x"));
			const auto sampling = GetTownViewSamplingState();
			const int factor = quality ? 2 : 1;
			Check(sampling.requested == quality && sampling.factor == factor && !sampling.limited
			        && sampling.width == out.w() * factor && sampling.height == gnViewportHeight * factor,
			    "effective raster density matches quality request while logical dimensions remain fixed");
			const Surface *retainedWorld = GetTownViewHighResolutionFrame();
			Check(quality ? retainedWorld != nullptr && retainedWorld->w() == out.w() * 2 && retainedWorld->h() == gnViewportHeight * 2
			              : retainedWorld == nullptr,
			    "only a current 2x draw exposes its borrowed world-only image for SDL presentation");
			if (quality && !retainedWorldExported) {
				OwnedSurface evidence(retainedWorld->w(), retainedWorld->h());
				SDL_SetPaletteColors(evidence.surface->format->palette, logical_palette.data(), 0, 256);
				for (int y = 0; y < evidence.h(); ++y)
					std::memcpy(evidence.at(0, y), retainedWorld->at(0, y), evidence.w());
				SavePng(evidence, output / (std::string(test.name) + "-world-retained-2x.png"));
				retainedWorldExported = true;
				Record("INFO retained 2x capture is world-only offscreen evidence, not a game-window screenshot");
			}
			const auto pixels = ViewportPixels(out);
			const auto picking = InspectQualityPicking(out);
			Check(picking.valid && picking.picked > 0, "resolved picks retain valid live IDs, architecture ownership and finite depth");
			const std::array<Point, 3> probes { TownViewScreenPosition(ViewPosition),
				TownViewScreenPosition(ViewPosition + Displacement { 1, 0 }), TownViewScreenPosition(ViewPosition + Displacement { 0, 1 }) };
			if (mode == 0) {
				baseline = pixels;
				baselinePicking = picking;
				logicalProbes = probes;
			} else {
				Check(probes == logicalProbes, "quality density preserves logical world projection exactly");
				if (mode == 2)
					Check(pixels == baseline && picking.hash == baselinePicking.hash,
					    "1x to 2x to 1x restores exact indexed pixels, picks, ownership and depth");
			}
			Check(guardsPreserved(), "quality draw preserves UI rows and both allocation guard rows");
			Check(cameraPreserved() && NativeSceneState() == initialState, "quality draw preserves camera, original map, collision and actor state");
			for (Point screen : { Point { -1, 0 }, Point { out.w(), 0 }, Point { 0, gnViewportHeight }, Point { 0, -1 } }) {
				Point tile;
				int npc, item, player;
				Check(!Pick(screen, tile, npc, item, player) && TownViewArchitectureAt(screen) == -1
				        && !std::isfinite(TownViewDepthAt(screen)), "quality queries reject coordinates outside the logical world viewport");
			}
			if (mode == 2)
				continue;
			const auto rays = AuditQualityArchitecture(out, factor);
			Check(rays.tested > 0 && rays.matched > 0 && rays.failed == 0,
			    "resolved architecture depth matches independent logical subpixel rays or a demonstrably nearer live occluder");
			const std::string filename = std::string(test.name) + (quality ? "-2x.png" : "-1x.png");
			SavePng(out.subregionY(0, gnViewportHeight), output / filename);
			std::array<double, 3> warmup {};
			std::array<double, 5> measured {};
			if (test.benchmark) {
				const auto timed = [&] {
					const auto begin = std::chrono::steady_clock::now();
					const bool drawn = DrawTownView(out, true);
					const auto end = std::chrono::steady_clock::now();
					if (!drawn)
						throw std::runtime_error("quality benchmark draw failed");
					return std::chrono::duration<double, std::milli>(end - begin).count();
				};
				for (double &value : warmup)
					value = timed();
				bool deterministic = ViewportPixels(out) == pixels;
				for (double &value : measured) {
					value = timed();
					deterministic = deterministic && ViewportPixels(out) == pixels;
				}
				Check(deterministic && InspectQualityPicking(out).hash == picking.hash && guardsPreserved()
				        && cameraPreserved() && NativeSceneState() == initialState,
				    "warm quality measurements preserve frozen pixels, picking, bounds and live state");
			}
			if (!firstCase)
				manifest << ",\n";
			firstCase = false;
			manifest << "{\"name\":\"" << test.name << "\",\"quality\":" << (quality ? "true" : "false")
			    << ",\"file\":\"" << filename << "\",\"logicalWidth\":" << out.w() << ",\"logicalHeight\":" << gnViewportHeight
			    << ",\"screenHeight\":" << test.height << ",\"rasterWidth\":" << sampling.width << ",\"rasterHeight\":" << sampling.height
			    << ",\"camera\":{\"yaw\":" << camera.yaw << ",\"pitch\":" << camera.pitch << ",\"distance\":" << camera.distance
			    << ",\"pan\":[" << camera.offsetX << ',' << camera.offsetZ << "]},\"nativeZoom\":" << (test.panelPanZoom ? "true" : "false")
			    << ",\"leftPanelOpen\":" << (test.panelPanZoom ? "true" : "false") << ",\"uiPanelHeight\":" << GetMainPanel().size.height
			    << ",\"factor\":" << sampling.factor << ",\"pixelHash\":\"" << QualityHashBytes(pixels) << "\",\"pickDepthHash\":\"" << picking.hash
			    << "\",\"pickedPixels\":" << picking.picked << ",\"architecturePixels\":" << picking.architecture
			    << ",\"playerPixels\":" << picking.player << ",\"npcPixels\":" << picking.npc
			    << ",\"rayTested\":" << rays.tested << ",\"rayMatched\":" << rays.matched << ",\"rayOccluded\":" << rays.occluded
			    << ",\"rayFailed\":" << rays.failed << ",\"uiRowsPreserved\":" << test.height - gnViewportHeight
			    << ",\"allocationGuardRows\":2,\"benchmarked\":" << (test.benchmark ? "true" : "false");
			if (test.benchmark) {
				auto sorted = measured;
				std::sort(sorted.begin(), sorted.end());
				manifest << ",\"warmupMilliseconds\":[";
				for (size_t i = 0; i < warmup.size(); ++i)
					manifest << (i == 0 ? "" : ",") << warmup[i];
				manifest << "],\"renderMilliseconds\":[";
				for (size_t i = 0; i < measured.size(); ++i)
					manifest << (i == 0 ? "" : ",") << measured[i];
				manifest << "],\"medianMilliseconds\":" << sorted[2] << ",\"minimumMilliseconds\":" << sorted.front()
				    << ",\"maximumMilliseconds\":" << sorted.back();
				Record(std::string("INFO optional quality ") + (quality ? "2x" : "1x") + " 960x540 CPU world median=" + std::to_string(sorted[2]) + " ms");
			}
			manifest << '}';
		}
		const auto staleRejected = [&] {
			Point tile;
			int npc, item, player;
			return !Pick(baselinePicking.probe, tile, npc, item, player)
			    && TownViewArchitectureAt(baselinePicking.probe) == -1
			    && !std::isfinite(TownViewDepthAt(baselinePicking.probe)) && GetTownViewHighResolutionFrame() == nullptr;
		};
		++gnScreenWidth;
		Check(staleRejected(), "logical resize invalidates previous quality selection before redraw");
		--gnScreenWidth;
		Check(DrawTownView(out, true), "redraw after restoring logical size");
		GetOptions().Graphics.zoom.SetValue(!test.panelPanZoom);
		Check(staleRejected(), "native zoom change invalidates previous quality selection before redraw");
		GetOptions().Graphics.zoom.SetValue(test.panelPanZoom);
		Check(DrawTownView(out, true), "redraw after restoring native zoom");
		CharFlag = !test.panelPanZoom;
		Check(staleRejected(), "side-panel change invalidates previous quality selection before redraw");
		CharFlag = test.panelPanZoom;
		Check(DrawTownView(out, true) && ViewportPixels(out) == baseline && InspectQualityPicking(out).hash == baselinePicking.hash,
		    "restoring size, zoom and panel state recreates the same 1x world and picking");
		if (!test.panelPanZoom) {
			ResetTownViewCamera();
			DrawActualNativeReference(out);
			const auto native = ViewportPixels(out);
			for (const bool quality : { false, true }) {
				GetOptions().Graphics.townViewAntialiasing.SetValue(quality);
				Check(DrawTownView(out) && ViewportPixels(out) == native && GetTownViewSamplingState().factor == 1
				        && GetTownViewHighResolutionFrame() == nullptr,
				    "native pose remains pixel-identical to the real original backend at both quality settings");
				Point tile;
				int npc, item, player;
				Check(!Pick({ out.w() / 2, gnViewportHeight / 2 }, tile, npc, item, player) && guardsPreserved(),
				    "native pose keeps original selection authoritative and preserves guards");
			}
		}
	}
	CharFlag = originalCharFlag;
	GetOptions().Graphics.zoom.SetValue(false);
	ResetTownViewCamera();
	gnScreenWidth = 1920;
	gnScreenHeight = 1080;
	CalculatePanelAreas();
	CalcViewportGeometry();
	OwnedSurface limited(1920, 1082);
	SDL_FillRect(limited.surface, nullptr, 255);
	GetOptions().Graphics.townViewAntialiasing.SetValue(true);
	Check(DrawTownView(limited.subregionY(0, 1080), true), "oversized optional quality request retains a working world renderer");
	const auto fallback = GetTownViewSamplingState();
	Check(fallback.requested && fallback.limited && fallback.factor == 1 && fallback.width == 1920 && fallback.height == 1080
	        && GetTownViewHighResolutionFrame() == nullptr,
	    "sample budget bounds optional quality memory and reports its explicit 1x fallback");
	bool guard = true;
	for (int y = 1080; y < limited.h(); ++y)
		for (int x = 0; x < limited.w(); ++x)
			guard = guard && limited[{ x, y }] == 255;
	Check(guard && NativeSceneState() == initialState, "limited quality fallback preserves guard rows and original simulation state");
	manifest << "\n],\"limitedRequest\":{\"logicalWidth\":1920,\"logicalHeight\":1080,\"requested\":true,\"factor\":1,\"limited\":true},"
	    << "\"roundtripPixelsAndPickingExact\":true,\"nativeBackendExact\":true,\"statePreserved\":true}\n";
	Check(manifest.good(), "record optional sampling evidence and CPU measurement limits");
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	FreeTownerGFX();
}

void Run(const std::filesystem::path &output)
{
	InitializeTownDiagnostic();
	CheckTownSceneMeshes();
	CheckTownModelImporter();
	CheckNativeVegetationGroups();
	CheckNativePropGroups();
	const std::string beforeShadowCheck = NativeSceneState();
	CheckStructuralShadows(output);
	Check(NativeSceneState() == beforeShadowCheck, "structural shadow construction preserves original map, camera and actor state");
	ExportTownMapping(output);
	ExportNativeHouseArtwork({ 26, 48 }, { 30, 52 }, output / "cabin-west-original.png");
	ExportNativeHouseArtwork({ 70, 66 }, { 74, 72 }, output / "cabin-east-original.png");
	const std::string beforeGroundExport = NativeSceneState();
	ExportNativeGroundPieces(output);
	Check(NativeSceneState() == beforeGroundExport, "native ground shadow exports preserve the original map and actor state");
	OwnedSurface out(640, 480);
	SDL_SetPaletteColors(out.surface->format->palette, logical_palette.data(), 0, 256);
	SDL_FillRect(out.surface, nullptr, 255);
	Check(!IsTownViewActive() && !DrawTownView(out, true), "perspective mode defaults off");
	DrawActualNativeReference(out);
	Capture(out, output / "isometric-reference-spawn.bmp");
	for (const Point sample : { Point { 60, 63 }, Point { 62, 65 }, Point { 63, 65 }, Point { 65, 64 }, Point { 69, 67 },
		Point { 60, 60 }, Point { 61, 58 }, Point { 64, 60 }, Point { 65, 59 },
		Point { 38, 64 }, Point { 52, 78 }, Point { 25, 23 }, Point { 24, 20 }, Point { 60, 70 },
		Point { 78, 18 }, Point { 53, 57 }, Point { 64, 58 }, Point { 22, 18 }, Point { 24, 25 },
		Point { 26, 26 }, Point { 24, 19 }, Point { 23, 17 } }) {
		const auto piece = dPiece[sample.x][sample.y];
		std::cout << "Source tile " << sample.x << ',' << sample.y << " piece=" << piece << " SOL=" << static_cast<unsigned>(SOLData[piece]) << '\n';
		Check(DrawTownViewTileDiagnostic(out.subregionY(0, gnViewportHeight), sample), "decode isolated source tile");
		Capture(out, output / ("tile-" + std::to_string(sample.x) + "-" + std::to_string(sample.y) + ".bmp"));
	}
	const auto initialPlayer = MyPlayer->position.tile;
	const auto initialView = ViewPosition;
	std::array<uint16_t, MAXDUNX * MAXDUNY> initialMap;
	std::memcpy(initialMap.data(), dPiece, sizeof(dPiece));
	ToggleTownView();
	Check(IsTownViewActive(), "toggle activates the view in Tristram");
	CheckZeroPieceGround(out);
	leveltype = DTYPE_CATHEDRAL;
	Check(!IsTownViewActive() && !DrawTownView(out, true), "cathedral keeps original rendering");
	leveltype = DTYPE_TOWN;
	for (int i = 0; i < 12; ++i) {
		Check(DrawTownView(out, true), "draw live town after toggle " + std::to_string(i + 1));
		ToggleTownView();
		Check(!IsTownViewActive() && !DrawTownView(out, true), "original mode skips perspective renderer");
		ToggleTownView();
	}
	Check(std::memcmp(initialMap.data(), dPiece, sizeof(dPiece)) == 0 && MyPlayer->position.tile == initialPlayer && ViewPosition == initialView,
		"24 mode switches preserve map, player, and camera target");
	Check(DrawTownView(out, true), "redraw current view before cursor checks");
	Capture(out, output / "tristram-spawn.bmp");
	CheckGroundPicking();
	for (const Point screen : { Point { -1, 0 }, Point { 640, 20 }, Point { 5, 352 }, Point { 5, 479 } }) {
		Point tile;
		int npc, item, player;
		Check(!Pick(screen, tile, npc, item, player), "reject cursor outside world viewport");
	}
	bool interfacePreserved = true;
	for (int y = gnViewportHeight; y < out.h(); ++y)
		interfacePreserved = interfacePreserved && std::all_of(out.at(0, y), out.at(out.w(), y), [](uint8_t value) { return value == 255; });
	Check(interfacePreserved, "world rendering preserves all interface rows");
	PlaceFixturePlayerNear({ 62, 65 });
	DrawActualNativeReference(out);
	Capture(out, output / "isometric-reference-center.bmp");
	Check(DrawTownView(out, true), "draw central Tristram");
	std::cout << "Central fixture=" << ViewPosition.x << ',' << ViewPosition.y << '\n';
	for (const Point sample : { Point { 62, 65 }, Point { 63, 65 }, Point { 65, 64 }, Point { 69, 67 } }) {
		Point selected;
		int npc, item, player;
		const Point projected = TownViewScreenPosition(sample);
		if (Pick(projected, selected, npc, item, player))
			std::cout << "Ground projected=" << sample.x << ',' << sample.y << " actual=" << selected.x << ',' << selected.y
				<< " palette=" << static_cast<unsigned>(out[projected]) << " npc=" << npc << " player=" << player << '\n';
	}
	CheckGroundPicking();
	Capture(out, output / "tristram-center.bmp");
	CheckEntityPicking();
	const auto baseline = ViewportPixels(out);
	ResetTownViewResources();
	Check(DrawTownView(out, true) && baseline == ViewportPixels(out), "resource cache reset recreates identical image");
	CheckWalkingContinuity(out, output);
	CheckCameraControls(out, output);
	CheckNativePoseRoute(out);
	CaptureNativeProjectionPairs(out, output);
	CaptureObjectTurntables(out, output);
	SaveTownShadowStats(output / "shadow-map.json");
	SaveTownLightingStats(output / "lighting-state.json");
	RotateTownView(0.75F);
	Point tile;
	int npc, item, player;
	Check(!Pick({ 320, 176 }, tile, npc, item, player), "camera change invalidates stale picking");
	Check(DrawTownView(out, true), "draw rotated camera");
	CheckGroundPicking();
	Check(baseline != ViewportPixels(out), "rotation changes projection");
	Capture(out, output / "tristram-rotated.bmp");
	RotateTownView(-1.5F);
	AdjustTownViewDistance(-4.0F);
	PlaceFixturePlayerNear({ 25, 31 });
	Check(DrawTownView(out, true), "draw cathedral approach");
	CheckGroundPicking();
	Capture(out, output / "tristram-cathedral-approach.bmp");
	ResetTownViewResources();
	pDungeonCels.reset();
	Check(!DrawTownView(out, true), "missing level graphics gracefully skips view");
	LoadTown();
	Check(DrawTownView(out, true), "original town resources reload after release");
	CheckGroundPicking();
	FreeTownerGFX();
}

#ifndef USE_SDL1
std::vector<uint8_t> OverlayCoverage(std::span<const SDL_Rect> regions, int width, int height)
{
	std::vector<uint8_t> result(static_cast<size_t>(width) * height);
	for (const SDL_Rect &rect : regions) {
		const int firstX = static_cast<int>(std::max<int64_t>(0, rect.x));
		const int firstY = static_cast<int>(std::max<int64_t>(0, rect.y));
		const int lastX = static_cast<int>(std::min<int64_t>(width, static_cast<int64_t>(rect.x) + rect.w));
		const int lastY = static_cast<int>(std::min<int64_t>(height, static_cast<int64_t>(rect.y) + rect.h));
		for (int y = firstY; y < lastY; ++y)
			for (int x = firstX; x < lastX; ++x)
				result[static_cast<size_t>(y) * width + x] = 1;
	}
	return result;
}

void CheckUiOverlayRegionTracking()
{
	OwnedSurface storage(12, 11);
	const Surface view = storage.subregion(2, 3, 8, 8);
	OwnedSurface foreign(8, 8);
	BeginUiOverlayRegions(view);
	MarkUiOverlayRect(view.subregion(1, 2, 4, 3), -1, -1, 3, 3);
	MarkUiOverlayRect(view, -2, 5, 5, 2);
	MarkUiOverlayRect(foreign, 0, 0, 8, 8);
	MarkUiOverlayRect(view, std::numeric_limits<int>::max(), 0, std::numeric_limits<int>::max(), 1);
	const std::array<SDL_Rect, 2> expected { SDL_Rect { 1, 2, 2, 2 }, SDL_Rect { 0, 5, 3, 2 } };
	auto frame = GetUiOverlayFrame();
	Check(frame.source == storage.surface && frame.sourceRegion.x == 2 && frame.sourceRegion.y == 3
	        && frame.sourceRegion.w == 8 && frame.sourceRegion.h == 8
	        && OverlayCoverage(frame.regions, 8, 8) == OverlayCoverage(expected, 8, 8),
	    "UI regions clip local subviews, retain source origin and ignore foreign surfaces/overflowing coordinates");
	BeginUiOverlayCursor();
	MarkUiOverlayRect(view, 6, 1, 1, 1);
	EndUiOverlayCursor();
	MarkUiOverlayRect(view, 7, 7, 1, 1);
	frame = GetUiOverlayFrame();
	const auto retainedUi = OverlayCoverage(frame.regions, 8, 8);
	Check(retainedUi[63] == 1 && OverlayCoverage(frame.cursorRegions, 8, 8)[14] == 1,
	    "cursor recording uses its own channel and End resumes ordinary UI recording");
	BeginUiOverlayCursor();
	MarkUiOverlayRect(view, 1, 0, 1, 1);
	EndUiOverlayCursor();
	frame = GetUiOverlayFrame();
	const auto cursor = OverlayCoverage(frame.cursorRegions, 8, 8);
	Check(OverlayCoverage(frame.regions, 8, 8) == retainedUi && cursor[1] == 1 && cursor[14] == 0,
	    "replacing cursor regions preserves UI and discards the previous cursor position");
	ClearUiOverlayRegions();
	MarkUiOverlayRect(view, 0, 0, 8, 8);
	frame = GetUiOverlayFrame();
	Check(frame.source == nullptr && frame.regions.empty() && frame.cursorRegions.empty(),
	    "clearing UI regions disables recording and removes both previous frame channels");
	BeginUiOverlayRegions(Surface { storage.surface, SDL_Rect { -1, 2, 8, 4 } });
	frame = GetUiOverlayFrame();
	Check(frame.sourceRegion.x == 0 && frame.sourceRegion.y == 2 && frame.sourceRegion.w == 7 && frame.sourceRegion.h == 4,
	    "partially outside source regions are clipped before logical overlay coordinates are assigned");
	ClearUiOverlayRegions();
	OwnedSurface many(130, 130);
	BeginUiOverlayRegions(many);
	for (int i = 0; i < 4097; ++i)
		MarkUiOverlayRect(many, 2 * (i % 65), 2 * (i / 65), 1, 1);
	frame = GetUiOverlayFrame();
	Check(frame.regions.size() == 1 && frame.regions.front().x == 0 && frame.regions.front().y == 0
	        && frame.regions.front().w == many.w() && frame.regions.front().h == many.h(),
	    "fragmented UI coverage has a bounded conservative full-frame fallback");
	ClearUiOverlayRegions();
}

struct SyntheticPresentationTarget {
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> output { nullptr, SDL_FreeSurface };
	std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer { nullptr, SDL_DestroyRenderer };

	SyntheticPresentationTarget(int logicalWidth, int logicalHeight, int physicalWidth = 0, int physicalHeight = 0)
	{
		output.reset(SDL_CreateRGBSurfaceWithFormat(0, physicalWidth > 0 ? physicalWidth : logicalWidth * 2,
		    physicalHeight > 0 ? physicalHeight : logicalHeight * 2, 32, SDL_PIXELFORMAT_ARGB8888));
		Check(output != nullptr, "allocate synthetic physical RGB output");
		renderer.reset(SDL_CreateSoftwareRenderer(output.get()));
		Check(renderer != nullptr && SDL_RenderSetLogicalSize(renderer.get(), logicalWidth, logicalHeight) == 0,
		    "create offscreen software renderer with externally configured logical size");
	}

	~SyntheticPresentationTarget()
	{
		// SDL destroys textures with their renderer; the cache must release them first.
		ResetTownPresentationResources();
	}
};

uint32_t SyntheticArgb(SDL_Color color)
{
	return 0xFF000000U | static_cast<uint32_t>(color.r) << 16 | static_cast<uint32_t>(color.g) << 8 | color.b;
}

void DrawSyntheticLegacy(SDL_Renderer *renderer, SDL_Surface *logical)
{
	std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> legacy(SDL_CreateTextureFromSurface(renderer, logical), SDL_DestroyTexture);
	Check(legacy != nullptr && SDL_SetTextureBlendMode(legacy.get(), SDL_BLENDMODE_NONE) == 0,
	    "prepare opaque synthetic legacy fallback");
#if SDL_VERSION_ATLEAST(2, 0, 12)
	Check(SDL_SetTextureScaleMode(legacy.get(), SDL_ScaleModeNearest) == 0, "use exact nearest sampling for synthetic legacy reference");
#endif
	Check(SDL_RenderCopy(renderer, legacy.get(), nullptr, nullptr) == 0, "draw complete synthetic legacy frame before optional layers");
}

std::vector<uint32_t> ReadSyntheticPresentation(SyntheticPresentationTarget &target)
{
	SDL_RenderPresent(target.renderer.get());
	std::vector<uint32_t> result(static_cast<size_t>(target.output->w) * target.output->h);
	Check(SDL_RenderReadPixels(target.renderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888,
	          result.data(), target.output->w * static_cast<int>(sizeof(uint32_t))) == 0,
	    "read back synthetic software-renderer pixels");
	return result;
}

void RunPresentationLayers(const std::filesystem::path &output)
{
	Record("INFO synthetic SDL software-renderer fixtures only; no game assets, gameplay or real window screenshots");
	CheckUiOverlayRegionTracking();
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	constexpr int Width = 8;
	constexpr int Height = 8;
	constexpr int ViewportHeight = 6;
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> logical(
	    SDL_CreateRGBSurfaceWithFormat(0, Width, Height, 24, SDL_PIXELFORMAT_RGB24), SDL_FreeSurface);
	Check(logical != nullptr, "allocate final logical RGB24 pixels independently of the indexed overlay source");
	const SDL_Color legacyColor { 17, 37, 91, 255 };
	SDL_FillRect(logical.get(), nullptr, SDL_MapRGB(logical->format, legacyColor.r, legacyColor.g, legacyColor.b));
	SDL_Rect black { 2, 1, 2, 2 };
	SDL_FillRect(logical.get(), &black, SDL_MapRGB(logical->format, 0, 0, 0));
	SDL_Rect colored { 5, 0, 1, 3 };
	const SDL_Color uiColor { 213, 71, 43, 255 };
	SDL_FillRect(logical.get(), &colored, SDL_MapRGB(logical->format, uiColor.r, uiColor.g, uiColor.b));
	OwnedSurface sourceStorage(12, 11);
	const Surface indexedSource = sourceStorage.subregion(2, 3, Width, Height);
	SDL_FillRect(sourceStorage.surface, nullptr, 3); // Deliberately not the RGB UI pixels.
	OwnedSurface worldStorage(20, 16);
	SDL_FillRect(worldStorage.surface, nullptr, 3);
	const Surface world = worldStorage.subregion(2, 1, Width * 2, ViewportHeight * 2);
	for (int y = 0; y < world.h(); ++y)
		for (int x = 0; x < world.w(); ++x)
			world[{ x, y }] = static_cast<uint8_t>(1 + (x + y) % 2);
	std::array<SDL_Color, 256> colors {};
	for (auto &color : colors)
		color.a = 255;
	colors[0] = { 0, 0, 0, 0 }; // Black is opaque UI by region, independent of palette alpha.
	colors[1] = { 231, 31, 19, 255 };
	colors[2] = { 29, 223, 47, 255 };
	colors[3] = { 201, 7, 211, 255 };
	Check(SDL_SetPaletteColors(worldStorage.surface->format->palette, colors.data(), 0, 256) == 0,
	    "prepare explicit synthetic world palette");
	const std::array<SDL_Rect, 5> regions { black, colored, SDL_Rect { -1, 4, 3, 2 },
		SDL_Rect { 7, 5, 3, 3 }, SDL_Rect { 3, 2, 2, 1 } };
	UiOverlayFrame frame { indexedSource.surface, indexedSource.region, regions, {} };
	const auto logicalColor = [&](int x, int y) {
		if (x >= black.x && x < black.x + black.w && y >= black.y && y < black.y + black.h)
			return SyntheticArgb(SDL_Color { 0, 0, 0, 255 });
		if (x >= colored.x && x < colored.x + colored.w && y >= colored.y && y < colored.y + colored.h)
			return SyntheticArgb(uiColor);
		return SyntheticArgb(legacyColor);
	};
	const auto expected = [&] {
		auto mask = OverlayCoverage(frame.regions, Width, Height);
		const auto cursorMask = OverlayCoverage(frame.cursorRegions, Width, Height);
		for (size_t i = 0; i < mask.size(); ++i)
			mask[i] |= cursorMask[i];
		std::vector<uint32_t> pixels(Width * 2 * Height * 2);
		for (int y = 0; y < Height * 2; ++y)
			for (int x = 0; x < Width * 2; ++x)
				pixels[static_cast<size_t>(y) * Width * 2 + x] = y >= ViewportHeight * 2 || mask[static_cast<size_t>(y / 2) * Width + x / 2]
				    ? logicalColor(x / 2, y / 2) : SyntheticArgb(colors[world[{ x, y }]]);
		return pixels;
	};
	SyntheticPresentationTarget target(Width, Height);
	DrawSyntheticLegacy(target.renderer.get(), logical.get());
	Check(RenderTownPresentationLayers(target.renderer.get(), logical.get(), world, frame, worldStorage.surface->format->palette, false),
	    "compose retained 2x indexed world with RGB24 logical UI");
	Check(ReadSyntheticPresentation(target) == expected(),
	    "physical output retains every 1px high-resolution detail and clips opaque black/color UI to its logical regions");
	SavePng(Surface { target.output.get() }, output / "synthetic-2x-density-black-ui.png");
	colors[1] = { 23, 53, 239, 255 };
	colors[2] = { 241, 227, 13, 255 };
	Check(SDL_SetPaletteColors(worldStorage.surface->format->palette, colors.data(), 0, 256) == 0, "change world palette between synthetic frames");
	DrawSyntheticLegacy(target.renderer.get(), logical.get());
	Check(RenderTownPresentationLayers(target.renderer.get(), logical.get(), world, frame, worldStorage.surface->format->palette, false)
	        && ReadSyntheticPresentation(target) == expected(),
	    "palette changes refresh retained-world RGB while logical UI colors stay independent");
	SavePng(Surface { target.output.get() }, output / "synthetic-palette-changed.png");
	const std::array<SDL_Rect, 1> oldCursor { SDL_Rect { 0, 0, 1, 1 } };
	const std::array<SDL_Rect, 1> movedCursor { SDL_Rect { 6, 3, 1, 1 } };
	frame.cursorRegions = oldCursor;
	DrawSyntheticLegacy(target.renderer.get(), logical.get());
	Check(RenderTownPresentationLayers(target.renderer.get(), logical.get(), world, frame, worldStorage.surface->format->palette, false)
	        && ReadSyntheticPresentation(target) == expected(),
	    "compose transient cursor independently over persistent opaque UI");
	frame.cursorRegions = movedCursor;
	DrawSyntheticLegacy(target.renderer.get(), logical.get());
	Check(RenderTownPresentationLayers(target.renderer.get(), logical.get(), world, frame, worldStorage.surface->format->palette, false)
	        && ReadSyntheticPresentation(target) == expected(),
	    "moving cursor restores the retained world at its previous position without removing persistent UI");
	SavePng(Surface { target.output.get() }, output / "synthetic-cursor-moved.png");
	frame.cursorRegions = {};
	const auto beforeInvalid = ReadSyntheticPresentation(target);
	UiOverlayFrame invalid = frame;
	++invalid.sourceRegion.w;
	Check(!RenderTownPresentationLayers(target.renderer.get(), logical.get(), world, invalid, worldStorage.surface->format->palette, false)
	        && ReadSyntheticPresentation(target) == beforeInvalid,
	    "unsupported overlay dimensions fail without changing the existing renderer output");
	Check(!RenderTownPresentationLayers(nullptr, logical.get(), world, frame, worldStorage.surface->format->palette, false)
	        && !RenderTownPresentationLayers(target.renderer.get(), logical.get(), Surface {}, frame, worldStorage.surface->format->palette, false)
	        && !RenderTownPresentationLayers(target.renderer.get(), logical.get(), world, frame, nullptr, false),
	    "missing renderer, world or palette gracefully reject layered presentation");
	SyntheticPresentationTarget second(Width, Height);
	DrawSyntheticLegacy(second.renderer.get(), logical.get());
	Check(RenderTownPresentationLayers(second.renderer.get(), logical.get(), world, frame, worldStorage.surface->format->palette, false)
	        && ReadSyntheticPresentation(second) == expected(),
	    "switching between live renderer instances recreates renderer-owned textures");
	ResetTownPresentationResources();
	ResetTownPresentationResources();
	DrawSyntheticLegacy(target.renderer.get(), logical.get());
	Check(RenderTownPresentationLayers(target.renderer.get(), logical.get(), world, frame, worldStorage.surface->format->palette, false)
	        && ReadSyntheticPresentation(target) == expected(),
	    "repeated reset releases caches safely and the original renderer can rebuild them");
	std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> resizedLogical(
	    SDL_CreateRGBSurfaceWithFormat(0, 9, 7, 32, SDL_PIXELFORMAT_ARGB8888), SDL_FreeSurface);
	Check(resizedLogical != nullptr, "allocate resized RGB32 logical UI fixture");
	SDL_FillRect(resizedLogical.get(), nullptr, SDL_MapRGB(resizedLogical->format, 255, 255, 255));
	OwnedSurface resizedWorld(18, 10);
	SDL_FillRect(resizedWorld.surface, nullptr, 1);
	OwnedSurface resizedSource(9, 7);
	const std::array<SDL_Rect, 1> resizedRegions { SDL_Rect { 1, 1, 3, 3 } };
	const UiOverlayFrame resizedFrame { resizedSource.surface, resizedSource.region, resizedRegions, {} };
	Check(SDL_RenderSetLogicalSize(target.renderer.get(), 9, 7) == 0
	        && RenderTownPresentationLayers(target.renderer.get(), resizedLogical.get(), resizedWorld, resizedFrame,
	            worldStorage.surface->format->palette, false),
	    "same renderer accepts odd logical dimensions and reallocates world/UI textures");
	Check(SDL_RenderSetLogicalSize(target.renderer.get(), Width, Height) == 0, "restore original logical size after texture resize");
	DrawSyntheticLegacy(target.renderer.get(), logical.get());
	Check(RenderTownPresentationLayers(target.renderer.get(), logical.get(), world, frame, worldStorage.surface->format->palette, false)
	        && ReadSyntheticPresentation(target) == expected(),
	    "resizing back preserves exact retained world and opaque UI composition");
	std::array<SDL_Color, 256> whitePalette = colors;
	whitePalette[1] = { 255, 255, 255, 255 };
	OwnedSurface whiteWorld(16, 12);
	SDL_FillRect(whiteWorld.surface, nullptr, 1);
	Check(SDL_SetPaletteColors(whiteWorld.surface->format->palette, whitePalette.data(), 0, 256) == 0,
	    "prepare uniform white world for fractional-scale UI seam check");
	SDL_FillRect(logical.get(), nullptr, SDL_MapRGB(logical->format, 255, 255, 255));
	SyntheticPresentationTarget fractional(Width, Height, 13, 13);
	DrawSyntheticLegacy(fractional.renderer.get(), logical.get());
	Check(RenderTownPresentationLayers(fractional.renderer.get(), logical.get(), whiteWorld, frame, whiteWorld.surface->format->palette, true),
	    "compose linear world with logical UI at fractional 13x13 physical output");
	const auto whitePixels = ReadSyntheticPresentation(fractional);
	Check(std::all_of(whitePixels.begin(), whitePixels.end(), [](uint32_t pixel) { return pixel == 0xFFFFFFFFU; }),
	    "opaque white UI over white world has no dark straight-alpha edge halo at fractional scaling");
	SavePng(Surface { fractional.output.get() }, output / "synthetic-fractional-white-ui.png");
	std::ofstream manifest(output / "synthetic-presentation.json");
	manifest << "{\"scope\":\"synthetic-software-renderer-only\",\"actualGameCapture\":false,"
	         << "\"logicalWidth\":8,\"logicalHeight\":8,\"worldWidth\":16,\"worldHeight\":12,"
	         << "\"outputWidth\":16,\"outputHeight\":16,\"exactPhysicalPixels\":true,\"blackUiOpaque\":true,"
	         << "\"sourceRegionOrigin\":[2,3],\"paletteRefresh\":true,\"rendererLifecycle\":true,"
	         << "\"oddLogicalResize\":true,\"cursorNoTrail\":true,\"fractionalWhiteUiNoHalo\":true}\n";
	Check(manifest.good(), "record clearly labeled synthetic layered-presentation evidence");
}
#endif
struct GpuFixtureTexture {
	std::vector<uint32_t> codes;
	std::vector<uint8_t> opacity;
	std::vector<uint8_t> lut;
	TownGpuTexture view;

	GpuFixtureTexture(uint64_t key, int width, int height, std::vector<uint32_t> colors,
	    std::vector<uint8_t> mask = {})
	    : codes(std::move(colors))
	    , opacity(std::move(mask))
	{
		view.stableKey = key;
		view.revision = 1;
		view.width = width;
		view.height = height;
		Refresh();
	}

	void Refresh()
	{
		view.texelCodes = codes;
		view.opacity = opacity;
		view.lightLut = lut;
	}
};

void GpuFixtureBegin(int width, int height)
{
	Check(TownGpuBeginFrame(width, height, true), "start explicit offscreen GPU diagnostic frame: " + GetTownGpuStatus().failure);
	Check(TownGpuSetShadow({}), "empty shadow view disables the previous frame's shadow binding");
}

void GpuFixtureQuad(float x, float y, float width, float height, float depth,
	const TownGpuTexture &texture, const TownGpuMaterial &material, uint32_t pickId,
	float uMaximum = 1, float vMaximum = 1, bool reverse = false)
{
	const auto vertex = [&](float px, float py, float u, float v) {
		return TownGpuVertex { px, py, depth, u, v, { px * 0.01F, 0, py * 0.01F } };
	};
	const std::array<TownGpuVertex, 4> vertices { vertex(x, y, 0, 0), vertex(x + width, y, uMaximum, 0),
		vertex(x + width, y + height, uMaximum, vMaximum), vertex(x, y + height, 0, vMaximum) };
	std::array<TownGpuVertex, 3> first { vertices[0], vertices[1], vertices[2] };
	std::array<TownGpuVertex, 3> second { vertices[0], vertices[2], vertices[3] };
	if (reverse) {
		std::swap(first[1], first[2]);
		std::swap(second[1], second[2]);
	}
	Check(TownGpuSubmitProjectedTriangle(first, texture, material, pickId)
	        && TownGpuSubmitProjectedTriangle(second, texture, material, pickId),
	    "submit both windings of a synthetic affine quad without CPU rasterization");
}

TownGpuFrame GpuFixtureEnd(int width, int height, size_t triangles)
{
	TownGpuFrame frame;
	Check(TownGpuEndFrame(frame), "complete GPU color, depth and pick readback: " + GetTownGpuStatus().failure);
	const auto &status = GetTownGpuStatus();
	const size_t pixels = static_cast<size_t>(width) * height;
	Check(status.available && status.frameSucceeded && status.submittedTriangles == triangles
	        && (triangles == 0 || status.drawCalls > 0) && frame.width == width && frame.height == height
	        && frame.indexed.size() == pixels && frame.pickIds.size() == pixels && frame.depth.size() == pixels,
	    "GPU success explicitly publishes three equally sized outputs, never a silent CPU fallback");
	return frame;
}

void CaptureGpuFixture(const TownGpuFrame &frame, const std::filesystem::path &path)
{
	OwnedSurface evidence(frame.width, frame.height);
	std::array<SDL_Color, 256> palette {};
	for (size_t i = 0; i < palette.size(); ++i)
		palette[i] = { static_cast<uint8_t>(i), static_cast<uint8_t>((i * 37) % 256), static_cast<uint8_t>((i * 71) % 256), 255 };
	SDL_SetPaletteColors(evidence.surface->format->palette, palette.data(), 0, 256);
	for (int y = 0; y < frame.height; ++y)
		std::memcpy(evidence.at(0, y), frame.indexed.data() + static_cast<size_t>(y) * frame.width, frame.width);
	SavePng(evidence, path);
}

void RunGpuFixtures(const std::filesystem::path &output)
{
	Record("INFO synthetic D3D11 fixtures, not a game-window capture; WARP is permitted explicitly for diagnostics");
	ResetTownGpuResources();
	TownGpuMaterial plain;
	GpuFixtureTexture base(0x10001, 1, 1, { 11 });
	GpuFixtureTexture black(0x10002, 1, 1, { 0 });
	GpuFixtureTexture decal(0x10003, 1, 1, { 99 });
	GpuFixtureTexture nearer(0x10004, 1, 1, { 77 });
	constexpr int Width = 19;
	constexpr int Height = 13;
	const auto index = [](int x, int y) { return static_cast<size_t>(y) * Width + x; };

	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, base.view, plain, 11);
	auto frame = GpuFixtureEnd(Width, Height, 2);
	bool completeQuad = true;
	for (size_t i = 0; i < frame.indexed.size(); ++i)
		completeQuad = completeQuad && frame.indexed[i] == 11 && frame.pickIds[i] == 11 && std::abs(frame.depth[i] - 100) < 0.001F;
	Check(completeQuad, "odd-width readback and split diagonal retain every color, depth and ID sample");
	const bool warp = GetTownGpuStatus().warp;
	const std::string adapter = GetTownGpuStatus().adapter;
	Record("INFO GPU adapter=" + adapter + " WARP=" + (warp ? "true" : "false"));
	const auto first = frame;

	// These odd target dimensions avoid the 3x3 texel boundaries. Native GPU
	// interpolation may fall on either side of an exact rational boundary.
	GpuFixtureTexture masked(0x10005, 3, 3, { 0, 33, 44, 55, 66, 77, 88, 101, 111 }, { 1, 0, 1, 1, 1, 0, 0, 1, 1 });
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, base.view, plain, 11);
	GpuFixtureQuad(0, 0, Width, Height, 50, masked.view, plain, 22, 1, 1, true);
	frame = GpuFixtureEnd(Width, Height, 4);
	bool maskCorrect = true;
	size_t maskMismatches = 0;
	for (int y = 0; y < Height; ++y) {
		for (int x = 0; x < Width; ++x) {
			const int tx = (6 * x + 3) / (2 * Width);
			const int ty = (6 * y + 3) / (2 * Height);
			const size_t texel = static_cast<size_t>(ty) * 3 + tx;
			const bool visible = masked.opacity[texel] != 0;
			const size_t pixel = index(x, y);
			const bool correct = frame.indexed[pixel] == (visible ? masked.codes[texel] : 11)
			        && frame.pickIds[pixel] == (visible ? 22 : 11)
			        && std::abs(frame.depth[pixel] - (visible ? 50 : 100)) < 0.001F;
			maskCorrect = maskCorrect && correct;
			if (!correct && ++maskMismatches <= 12)
				Record("INFO GPU mask mismatch x=" + std::to_string(x) + " y=" + std::to_string(y)
				    + " actual=" + std::to_string(frame.indexed[pixel]) + "," + std::to_string(frame.pickIds[pixel]) + "," + std::to_string(frame.depth[pixel])
				    + " expected=" + std::to_string(visible ? masked.codes[texel] : 11) + "," + std::to_string(visible ? 22 : 11)
				    + "," + std::to_string(visible ? 50 : 100));
		}
	}
	CaptureGpuFixture(frame, output / "synthetic-opacity-index-zero.png");
	Check(maskCorrect, "explicit opacity preserves painted index zero and discards masked pixels before depth or pick writes");

	TownGpuMaterial transparent = plain;
	transparent.transparentZero = true;
	TownGpuMaterial preserve = plain;
	preserve.preservePicking = true;
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, base.view, plain, 11);
	GpuFixtureQuad(0, 0, Width, Height, 80, decal.view, preserve, 99);
	GpuFixtureQuad(0, 0, Width, Height, 90, nearer.view, plain, 77);
	GpuFixtureQuad(0, 0, Width, Height, 70, black.view, transparent, 66);
	frame = GpuFixtureEnd(Width, Height, 8);
	Check(std::all_of(frame.indexed.begin(), frame.indexed.end(), [](uint8_t color) { return color == 99; })
	        && std::all_of(frame.pickIds.begin(), frame.pickIds.end(), [](uint32_t id) { return id == 11; })
	        && std::all_of(frame.depth.begin(), frame.depth.end(), [](float depth) { return std::abs(depth - 80) < 0.001F; }),
	    "shadow-like decals preserve underlying picks while updating depth; farther geometry and legacy transparent zero cannot overwrite them");

	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, base.view, plain, 11);
	GpuFixtureQuad(0, 0, Width, Height, 100, nearer.view, plain, 77);
	frame = GpuFixtureEnd(Width, Height, 4);
	Check(std::all_of(frame.indexed.begin(), frame.indexed.end(), [](uint8_t color) { return color == 77; })
	        && std::all_of(frame.pickIds.begin(), frame.pickIds.end(), [](uint32_t id) { return id == 77; }),
	    "exact coplanar overlays drawn later win color and selection together");

	GpuFixtureTexture stripes(0x10006, 3, 1, { 41, 83, 121 });
	TownGpuMaterial repeat = plain;
	repeat.repeat = true;
	GpuFixtureBegin(Width, Height);
	// 1.75 periods crosses a wrap while keeping every sample off a texel edge.
	GpuFixtureQuad(0, 0, Width, Height, 100, stripes.view, repeat, 41, 1.75F, 1);
	frame = GpuFixtureEnd(Width, Height, 2);
	bool repeated = true;
	for (int y = 0; y < Height; ++y)
		for (int x = 0; x < Width; ++x) {
			const int texel = (21 * (2 * x + 1) / (8 * Width)) % 3;
			repeated = repeated && frame.indexed[index(x, y)] == stripes.codes[texel];
		}
	Check(repeated, "affine UVs repeat in material units with nearest texel sampling");
	GpuFixtureTexture directional(0x10009, 1, 1, { 1 });
	directional.lut = { 0, 0, 0, 0, 41, 52, 63, 74 };
	directional.view.lightLevels = 4;
	directional.Refresh();
	TownGpuMaterial lit = plain;
	lit.lighting = TownGpuLighting::Directional;
	lit.diffuse = 0.5F;
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, directional.view, lit, 52);
	frame = GpuFixtureEnd(Width, Height, 2);
	Check(std::all_of(frame.indexed.begin(), frame.indexed.end(), [](uint8_t color) { return color == 63; }),
	    "directional materials index the lighting LUT by texel code and rounded irradiance level");
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, base.view, plain, 11);
	const std::array<TownGpuVertex, 3> sloped { TownGpuVertex { 0, 0, 20, 0, 0, {} },
		TownGpuVertex { Width, 0, 40, 1, 0, {} }, TownGpuVertex { 0, Height, 60, 0, 1, {} } };
	Check(TownGpuSubmitProjectedTriangle(sloped, nearer.view, plain, 77), "submit a triangle with distinct affine vertex depths");
	frame = GpuFixtureEnd(Width, Height, 3);
	const float expectedDepth = 20 + 20 * 2.5F / Width + 40 * 2.5F / Height;
	Check(frame.pickIds[index(2, 2)] == 77 && std::abs(frame.depth[index(2, 2)] - expectedDepth) < 0.001F
	        && frame.pickIds[index(Width - 2, Height - 2)] == 11,
	    "readback depth is unnormalized affine camera depth and excludes points outside the triangle");
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, 9, Height, 100, base.view, plain, 11);
	GpuFixtureQuad(9, 0, Width - 9, Height, 100, nearer.view, plain, 77);
	GpuFixtureQuad(5, 4, 9, 5, 50, decal.view, plain, 99);
	frame = GpuFixtureEnd(Width, Height, 6);
	bool geometricPicks = true;
	for (int y = 0; y < Height; ++y)
		for (int x = 0; x < Width; ++x) {
			// Parallel rays through pixel centers meet these exact rectangles;
			// every boundary is between sample centers, independent of raster rules.
			const bool foreground = x >= 5 && x < 14 && y >= 4 && y < 9;
			const uint32_t expected = foreground ? 99 : (x < 9 ? 11 : 77);
			geometricPicks = geometricPicks && frame.pickIds[index(x, y)] == expected
			    && frame.indexed[index(x, y)] == expected && std::abs(frame.depth[index(x, y)] - (foreground ? 50 : 100)) < 0.001F;
		}
	Check(geometricPicks, "independent parallel-ray rectangle oracle verifies adjacent ground IDs and frontmost object depth at every pixel");
	GpuFixtureTexture volumeBase(0x10010, 1, 1, { 87 });
	GpuFixtureTexture volumeFront(0x10011, 3, 3, masked.codes, masked.opacity);
	for (GpuFixtureTexture *texture : { &volumeBase, &volumeFront }) {
		texture->lut.resize(256 * 16);
		for (size_t code = 0; code < 256; ++code)
			for (size_t shade = 0; shade < 4; ++shade)
				for (size_t shadow = 0; shadow < 4; ++shadow)
					texture->lut[code * 16 + shade * 4 + shadow] = static_cast<uint8_t>((code + shade * 17 + shadow * 3) % 256);
		texture->view.lightLevels = 16;
		texture->Refresh();
	}
	TownGpuMaterial baseShade = plain;
	baseShade.lighting = TownGpuLighting::Shadow;
	baseShade.shade = 2;
	TownGpuMaterial frontShade = baseShade;
	frontShade.shade = 0;
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, volumeBase.view, baseShade, 11, 1.3F, 1.2F);
	GpuFixtureQuad(0, 0, Width, Height, 100, volumeFront.view, frontShade, 11, 1.3F, 1.2F);
	const auto twoPassVolume = GpuFixtureEnd(Width, Height, 4);
	TownGpuMaterial fused = frontShade;
	fused.fallbackPaletteIndex = 87;
	fused.fallbackShade = 2;
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, volumeFront.view, fused, 11, 1.3F, 1.2F);
	frame = GpuFixtureEnd(Width, Height, 2);
	Check(frame.indexed == twoPassVolume.indexed && frame.pickIds == twoPassVolume.pickIds && frame.depth == twoPassVolume.depth,
	    "one-pass volume fallback exactly matches opaque base plus masked front, including index zero, shaded base and UVs outside the front texture");
	CaptureGpuFixture(frame, output / "synthetic-fused-volume.png");

	GpuFixtureTexture transient(0x10007, 1, 1, { 42 });
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, transient.view, plain, 42);
	transient.codes[0] = 125;
	frame = GpuFixtureEnd(Width, Height, 2);
	Check(std::all_of(frame.indexed.begin(), frame.indexed.end(), [](uint8_t color) { return color == 42; }),
	    "submitted texture bytes are owned before borrowed sprite storage can change");
	++transient.view.revision;
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, transient.view, plain, 125);
	frame = GpuFixtureEnd(Width, Height, 2);
	Check(std::all_of(frame.indexed.begin(), frame.indexed.end(), [](uint8_t color) { return color == 125; }),
	    "texture revision invalidates an otherwise stable upload key");

	GpuFixtureBegin(7, 5);
	frame = GpuFixtureEnd(7, 5, 0);
	Check(std::all_of(frame.pickIds.begin(), frame.pickIds.end(), [](uint32_t id) { return id == 0; })
	        && std::all_of(frame.depth.begin(), frame.depth.end(), [](float depth) { return !std::isfinite(depth); }),
	    "resizing and starting an empty frame clears stale picking and depth");
	GpuFixtureBegin(Width, Height);
	GpuFixtureTexture malformed(0x10008, 2, 2, { 1, 2, 3 });
	const std::array<TownGpuVertex, 3> triangle { TownGpuVertex { 0, 0, 100, 0, 0, {} },
		TownGpuVertex { 10, 0, 100, 1, 0, {} }, TownGpuVertex { 0, 10, 100, 0, 1, {} } };
	Check(!TownGpuSubmitProjectedTriangle(triangle, malformed.view, plain, 3), "reject a texture whose storage does not match its declared dimensions");
	Check(!TownGpuEndFrame(frame) && !GetTownGpuStatus().frameSucceeded && frame.width == 0 && frame.height == 0
	        && frame.indexed.empty() && frame.pickIds.empty() && frame.depth.empty(),
	    "a failed GPU frame never publishes a partial image or stale depth and picking");
	Check(!TownGpuBeginFrame(0, Height, true), "invalid frame dimensions fail before GPU allocation");
	ResetTownGpuResources();
	ResetTownGpuResources();
	GpuFixtureBegin(Width, Height);
	GpuFixtureQuad(0, 0, Width, Height, 100, base.view, plain, 11);
	frame = GpuFixtureEnd(Width, Height, 2);
	Check(frame.indexed == first.indexed && frame.pickIds == first.pickIds && frame.depth == first.depth,
	    "device reset and recreation restore the same synthetic frame");
	std::ofstream metadata(output / "synthetic-gpu.json");
	metadata << "{\"synthetic\":true,\"gameWindowCapture\":false,\"backend\":\"D3D11\",\"warp\":" << (warp ? "true" : "false")
	    << ",\"adapter\":" << std::quoted(adapter) << ",\"width\":" << Width << ",\"height\":" << Height
	    << ",\"cpuRasterFallbackAllowed\":false,\"depthTiePolicy\":\"exact LEQUAL; CPU subepsilon tolerance is not claimed equivalent\"}\n";
	Check(metadata.good(), "record the actual GPU adapter and synthetic test scope");
	ResetTownGpuResources();
}

struct GpuPickSnapshot {
	Point tile { -1, -1 };
	int npc = -1, item = -1, player = -1, architecture = -1;
	float depth = std::numeric_limits<float>::infinity();
	bool picked = false;

	bool SameIdentity(const GpuPickSnapshot &other) const
	{
		return picked == other.picked && tile == other.tile && npc == other.npc && item == other.item
		    && player == other.player && architecture == other.architecture;
	}
};

struct GpuRayFrame {
	RayVector eye, right, up, direction;
	Point anchor;
	double focal;
	TownViewCameraState camera;
	int zoom;

	GpuRayFrame()
	{
		camera = GetTownViewCameraState();
		constexpr double HeightScale = 0.816496580927726;
		zoom = *GetOptions().Graphics.zoom ? 2 : 1;
		focal = 45.25483399593904 * 22 / camera.distance * zoom;
		anchor = GetScreenPosition(ViewPosition) + Displacement { 32, 0 };
		anchor = { anchor.x * zoom, anchor.y * zoom };
		if (zoom == 2 && CanPanelsCoverView() && IsLeftPanelOpen())
			anchor.x += SidePanelSize.width;
		const double cy = std::cos(camera.yaw), sy = std::sin(camera.yaw);
		const double cp = std::cos(camera.pitch), sp = std::sin(camera.pitch);
		right = { sy, 0, -cy };
		up = { -cy * sp, cp, -sy * sp };
		const RayVector eyeDirection { cy * cp, sp, sy * cp };
		const RayVector target { ViewPosition.x + camera.offsetX, 0, ViewPosition.y + camera.offsetZ };
		eye = target + eyeDirection * 256;
		direction = { -eyeDirection.x, -eyeDirection.y / HeightScale, -eyeDirection.z };
	}

	RayVector Origin(double pixelX, double pixelY) const
	{
		RayVector origin = eye + right * ((pixelX - anchor.x) / focal) + up * ((anchor.y - pixelY) / focal);
		origin.y /= 0.816496580927726;
		return origin;
	}

	std::array<double, 3> RasterVertex(const TownSceneVertex &vertex, int factor) const
	{
		// Match the submitted float projection, then separately quantize coverage.
		const float cy = std::cos(camera.yaw), sy = std::sin(camera.yaw);
		const float cp = std::cos(camera.pitch), sp = std::sin(camera.pitch);
		const float eyeX = (static_cast<float>(ViewPosition.x) + camera.offsetX) + cy * cp * 256.0F;
		const float eyeY = sp * 256.0F;
		const float eyeZ = (static_cast<float>(ViewPosition.y) + camera.offsetZ) + sy * cp * 256.0F;
		const float x = vertex.x - eyeX, y = vertex.height * 0.816496580927726F - eyeY, z = vertex.z - eyeZ;
		const float cameraX = x * sy + y * 0 + z * -cy;
		const float cameraY = x * (-cy * sp) + y * cp + z * (-sy * sp);
		const float cameraDepth = x * (-cy * cp) + y * -sp + z * (-sy * cp);
		const float scale = 45.25483399593904F * 22.0F / camera.distance * zoom;
		const float screenX = (static_cast<float>(anchor.x) + cameraX * scale) * factor;
		const float screenY = (static_cast<float>(anchor.y) - cameraY * scale) * factor;
		return { screenX, screenY, cameraDepth };
	}

	bool MatchesGroundDepth(Point pixel, int factor, float depth) const
	{
		for (int dy = 0; dy < factor; ++dy) {
			for (int dx = 0; dx < factor; ++dx) {
				const auto origin = Origin(pixel.x + (dx + 0.5) / factor, pixel.y + (dy + 0.5) / factor);
				const double distance = -origin.y / direction.y;
				if (std::isfinite(distance) && std::abs(distance - depth) <= 0.0001)
					return true;
			}
		}
		return false;
	}
};

bool AuditGpuVegetationDepth(Point tile, Point pixel, int factor, float cpuDepth, float gpuDepth)
{
	const auto &groups = GetTownVegetationGroups();
	const auto group = std::find_if(groups.begin(), groups.end(), [&](const TownVegetationGroup &entry) { return entry.referenceFootpoint == tile; });
	if (group == groups.end())
		return false;
	const size_t groupIndex = static_cast<size_t>(group - groups.begin());
	const TownVolumeMesh *volume = TownViewVegetationVolume(groupIndex);
	if (volume == nullptr)
		return false;
	const GpuRayFrame rays;
	std::vector<TownSceneTriangle> geometry;
	geometry.reserve(volume->triangles.size());
	constexpr float InverseSqrt2 = 0.7071067811865475F;
	for (const auto &source : volume->triangles) {
		TownSceneTriangle triangle;
		for (size_t i = 0; i < 3; ++i) {
			const auto &v = source.vertices[i];
			triangle.vertices[i] = { tile.x + (v.x + v.z) * InverseSqrt2,
				v.height + v.z * InverseSqrt2 - 1 / 32.0F, tile.y + (-v.x + v.z) * InverseSqrt2, v.u, v.v };
		}
		geometry.push_back(triangle);
	}
	double nearestStrict = std::numeric_limits<double>::infinity();
	double nearestExpanded = nearestStrict;
	double nearestQuantized = nearestStrict;
	double winningEdgeDistance = nearestStrict;
	bool winningQuantizedCoverage = true;
	const auto screenEdge = [](const std::array<double, 3> &a, const std::array<double, 3> &b, double x, double y) {
		return (x - a[0]) * (b[1] - a[1]) - (y - a[1]) * (b[0] - a[0]);
	};
	for (int dy = 0; dy < factor; ++dy) {
		for (int dx = 0; dx < factor; ++dx) {
			const auto origin = rays.Origin(pixel.x + (dx + 0.5) / factor, pixel.y + (dy + 0.5) / factor);
			double strict = std::numeric_limits<double>::infinity(), expanded = strict;
			double quantized = strict, strictEdgeDistance = strict;
			double strictBarycentric = 0, expandedBarycentric = 0;
			size_t strictTriangle = geometry.size(), expandedTriangle = geometry.size();
			bool strictQuantizedCoverage = true;
			for (size_t i = 0; i < geometry.size(); ++i) {
				const auto &triangle = geometry[i];
				const auto a = RayPosition(triangle.vertices[0]);
				const auto e1 = RayPosition(triangle.vertices[1]) - a;
				const auto e2 = RayPosition(triangle.vertices[2]) - a;
				const auto h = RayCross(rays.direction, e2);
				const double determinant = RayDot(e1, h);
				const RayVector physicalEye { rays.eye.x, rays.eye.y / 0.816496580927726, rays.eye.z };
				if (std::abs(determinant) < 1e-10 || RayDot(RayCross(e1, e2), physicalEye - a) <= 0)
					continue;
				const auto s = origin - a;
				const double u = RayDot(s, h) / determinant;
				const auto q = RayCross(s, e1);
				const double v = RayDot(rays.direction, q) / determinant;
				const double distance = RayDot(e2, q) / determinant;
				const double margin = std::min({ u, v, 1 - u - v });
				if (distance <= 0)
					continue;
				std::array<std::array<double, 3>, 3> projected, snapped;
				for (size_t corner = 0; corner < 3; ++corner) {
					projected[corner] = rays.RasterVertex(triangle.vertices[corner], factor);
					snapped[corner] = projected[corner];
					for (size_t axis = 0; axis < 2; ++axis)
						snapped[corner][axis] = std::round(projected[corner][axis] * 256) / 256;
				}
				const double sampleX = pixel.x * factor + dx + 0.5, sampleY = pixel.y * factor + dy + 0.5;
				const double snappedArea = screenEdge(snapped[0], snapped[1], snapped[2][0], snapped[2][1]);
				bool quantizedCoverage = std::abs(snappedArea) > 1e-12;
				if (quantizedCoverage)
					for (size_t edge = 0; edge < 3; ++edge)
						quantizedCoverage = quantizedCoverage && screenEdge(snapped[edge], snapped[(edge + 1) % 3], sampleX, sampleY) / snappedArea >= 0;
				if (quantizedCoverage)
					quantized = std::min(quantized, distance);
				if (margin < -0.0001)
					continue;
				if (distance < expanded) {
					expanded = distance;
					expandedBarycentric = margin;
					expandedTriangle = i;
				}
				if (margin >= 0 && distance < strict) {
					strict = distance;
					strictBarycentric = margin;
					strictTriangle = i;
					strictQuantizedCoverage = quantizedCoverage;
					strictEdgeDistance = std::numeric_limits<double>::infinity();
					for (size_t edge = 0; edge < 3; ++edge) {
						const auto &pa = projected[edge], &pb = projected[(edge + 1) % 3];
						const double length = std::hypot(pb[0] - pa[0], pb[1] - pa[1]);
						if (length > 0)
							strictEdgeDistance = std::min(strictEdgeDistance, std::abs(screenEdge(pa, pb, sampleX, sampleY)) / length);
					}
				}
			}
			if (strict < nearestStrict) {
				nearestStrict = strict;
				winningEdgeDistance = strictEdgeDistance;
				winningQuantizedCoverage = strictQuantizedCoverage;
			}
			nearestExpanded = std::min(nearestExpanded, expanded);
			nearestQuantized = std::min(nearestQuantized, quantized);
			std::ostringstream details;
			details << std::setprecision(12) << "INFO GPU tree ray foot=" << tile.x << ',' << tile.y
			    << " pixel=" << pixel.x << ',' << pixel.y << " sample=" << dx << ',' << dy << " group=" << groupIndex
			    << " strictDepth=" << strict << " strictTriangle=" << strictTriangle << " strictMargin=" << strictBarycentric
			    << " strictScreenEdgeDistance=" << strictEdgeDistance << " strictQuantizedCoverage=" << strictQuantizedCoverage
			    << " quantizedDepth=" << quantized
			    << " expandedDepth=" << expanded << " expandedTriangle=" << expandedTriangle << " expandedMargin=" << expandedBarycentric;
			Record(details.str());
		}
	}
	const bool classified = std::abs(nearestStrict - cpuDepth) <= 0.001 && std::abs(nearestQuantized - gpuDepth) <= 0.001
	    && nearestStrict + 0.0001 < nearestQuantized && winningEdgeDistance <= 1.0 / 256 && !winningQuantizedCoverage;
	std::ostringstream summary;
	summary << std::setprecision(12) << "INFO GPU tree ray minima CPU=" << cpuDepth << " GPU=" << gpuDepth
	    << " independentStrict=" << nearestStrict << " independentBarycentricExpanded=" << nearestExpanded
	    << " independentQuantized=" << nearestQuantized << " winningScreenEdgeDistance=" << winningEdgeDistance
	    << " classifiedFixedPointCoverage=" << classified;
	Record(summary.str());
	return classified;
}

std::vector<GpuPickSnapshot> ReadGpuWorldPicks(const Surface &out)
{
	std::vector<GpuPickSnapshot> samples(static_cast<size_t>(out.w()) * gnViewportHeight);
	for (int y = 0; y < gnViewportHeight; ++y) {
		for (int x = 0; x < out.w(); ++x) {
			auto &sample = samples[static_cast<size_t>(y) * out.w() + x];
			sample.picked = Pick({ x, y }, sample.tile, sample.npc, sample.item, sample.player);
			sample.architecture = TownViewArchitectureAt({ x, y });
			sample.depth = TownViewDepthAt({ x, y });
		}
	}
	return samples;
}

void CheckGpuVideoOptions()
{
	// Main binds ConfigPath to the diagnostic output, never a player's profile.
	LoadOptions();
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	SaveOptions();
	const auto state = NativeSceneState();
	const auto find = [](std::string label) {
		for (size_t page = 0; page < 64; ++page) {
			size_t next = GMenuSettingsMaxContentRows + 4;
			for (size_t row = 0; row <= GMenuSettingsMaxContentRows + 3; ++row) {
				if (sgpCurrentMenu[row].fnMenu == nullptr)
					break;
				if (sgpCurrentMenu[row].enabled() && sgpCurrentMenu[row].pszStr == label)
					return row;
				if (sgpCurrentMenu[row].enabled() && std::string_view(sgpCurrentMenu[row].pszStr) == _("Next Page"))
					next = row;
			}
			if (next > GMenuSettingsMaxContentRows + 3)
				break;
			sgpCurrentMenu[next].fnMenu(true);
		}
		throw std::runtime_error("GPU menu option is not reachable: " + label);
	};
	gamemenu_on();
	Check(sgpCurrentMenu != nullptr && sgpCurrentMenu[0].fnMenu != nullptr, "open the real headless game Settings menu");
	sgpCurrentMenu[0].fnMenu(true);
	sgpCurrentMenu[find(std::string(GetOptions().Graphics.GetName()))].fnMenu(true);
	sgpCurrentMenu[find(std::string(GetOptions().Graphics.townViewGpuRendering.GetName()))].fnMenu(true);
	sgpCurrentMenu[find(std::string(GetOptions().Graphics.townViewAntialiasing.GetName()))].fnMenu(true);
	Check(*GetOptions().Graphics.townViewGpuRendering && *GetOptions().Graphics.townViewAntialiasing,
	    "real paginated Graphics callbacks enable GPU and smoothing independently");
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	LoadOptions();
	Check(*GetOptions().Graphics.townViewGpuRendering && *GetOptions().Graphics.townViewAntialiasing,
	    "Video callbacks persist both switches immediately to the isolated diagnostic INI");
	sgpCurrentMenu[find(std::string(GetOptions().Graphics.townViewGpuRendering.GetName()))].fnMenu(false);
	Check(*GetOptions().Graphics.townViewGpuRendering, "a non-activation menu callback cannot toggle GPU rendering");
	sgpCurrentMenu[find(std::string(GetOptions().Graphics.townViewGpuRendering.GetName()))].fnMenu(true);
	Check(!*GetOptions().Graphics.townViewGpuRendering && *GetOptions().Graphics.townViewAntialiasing,
	    "disabling GPU preserves the independent smoothing preference");
	Check(gmenu_presskeys(SDLK_ESCAPE) && IsInGameSettingsOpen(), "Graphics Escape returns to the shared settings categories");
	gamemenu_off();
	LoadOptions();
	Check(!*GetOptions().Graphics.townViewGpuRendering && *GetOptions().Graphics.townViewAntialiasing
	        && NativeSceneState() == state, "menu close and reload preserve independent settings without changing the native scene");
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
}

std::string CheckGpuWorldBudgetFallback()
{
	const std::string original = NativeSceneState();
	const auto randomState = GetLCGEngineState();
	CharFlag = false;
	GetOptions().Graphics.zoom.SetValue(false);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	gnScreenWidth = 2304;
	gnScreenHeight = 2048;
	CalculatePanelAreas();
	CalcViewportGeometry();
	ResetTownViewCamera();
	OrbitTownView(0.12F, 0);
	Check(static_cast<uint64_t>(gnScreenWidth) * gnViewportHeight > 4ULL * 1024 * 1024,
	    "real GPU fallback fixture exceeds the GPU frame budget with quality disabled");
	const int budgetViewportHeight = gnViewportHeight;
	std::string failure;
	double fallbackMilliseconds = 0;
	{
		OwnedSurface allocation(gnScreenWidth, gnScreenHeight + 2);
		SDL_SetPaletteColors(allocation.surface->format->palette, logical_palette.data(), 0, 256);
		SDL_FillRect(allocation.surface, nullptr, 255);
		const Surface out = allocation.subregionY(0, gnScreenHeight);
		Check(DrawTownView(out, true) && !GetTownViewRendererState().requestedGpu
		        && GetTownViewRendererState().cpuRasterizedTriangles > 0,
		    "oversized fallback has a real CPU reference without forcing a GPU allocation");
		const auto referencePixels = ViewportPixels(out);
		const auto referencePicking = InspectQualityPicking(out);
		Check(referencePicking.valid && referencePicking.picked > 0, "oversized CPU reference publishes valid live selection and depth");
		GetOptions().Graphics.townViewGpuRendering.SetValue(true);
		Point tile;
		int npc, item, player;
		Check(!Pick(referencePicking.probe, tile, npc, item, player) && !std::isfinite(TownViewDepthAt(referencePicking.probe)),
		    "requesting the rejected GPU frame invalidates the previous CPU selection before redraw");
		const auto begin = std::chrono::steady_clock::now();
		Check(DrawTownView(out, true), "a real GPU pixel-budget rejection completes through the CPU backend");
		fallbackMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
		const auto fallback = GetTownViewRendererState();
		const auto rejected = GetTownGpuStatus();
		failure = fallback.failure;
		Check(fallback.requestedGpu && !fallback.usedGpu && fallback.cpuRasterizedTriangles > 0
		        && fallback.gpuSubmittedTriangles == 0 && !rejected.frameSucceeded && rejected.drawCalls == 0
		        && rejected.failureKind == TownGpuFailureKind::Capacity && failure.find("pixel budget") != std::string::npos,
		    "real GPU budget failure is explicit and occurs before any triangle submission: " + failure);
		Check(GetTownViewSamplingState().factor == 1 && GetTownViewHighResolutionFrame() == nullptr
		        && ViewportPixels(out) == referencePixels && InspectQualityPicking(out).hash == referencePicking.hash,
		    "the rejected GPU request restores exact CPU colors, selection and depth without a partial GPU frame");
		Check(DrawTownView(out, true) && GetTownViewRendererState().requestedGpu && !GetTownViewRendererState().usedGpu
		        && GetTownViewRendererState().cpuRasterizedTriangles > 0 && GetTownViewRendererState().gpuSubmittedTriangles == 0
		        && GetTownViewRendererState().failure == failure
		        && ViewportPixels(out) == referencePixels && InspectQualityPicking(out).hash == referencePicking.hash,
		    "a repeated blocked GPU request remains an exact CPU frame with the original failure reason");
		bool guards = true;
		for (int y = gnViewportHeight; y < allocation.h(); ++y)
			for (int x = 0; x < allocation.w(); ++x)
				guards = guards && allocation[{ x, y }] == 255;
		Check(guards, "the rejected GPU frame and its CPU fallback preserve every UI and allocation guard row");
	}
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	CalculatePanelAreas();
	CalcViewportGeometry();
	ResetTownViewCamera();
	OrbitTownView(0.12F, 0);
	OwnedSurface recovery(640, 482);
	SDL_FillRect(recovery.surface, nullptr, 255);
	const Surface out = recovery.subregionY(0, 480);
	// The request stays ON. Capacity recovery must notice a supported workload
	// without requiring the player to toggle the renderer manually.
	SDL_Delay(TownGpuRecoveryPolicy::RetryDelayMilliseconds);
	Check(DrawTownView(out, true) && GetTownViewRendererState().requestedGpu && GetTownViewRendererState().usedGpu
	        && GetTownViewRendererState().cpuRasterizedTriangles == 0 && GetTownViewRendererState().failure.empty()
	        && GetTownGpuStatus().frameSucceeded && !GetTownGpuStatus().warp
	        && GetTownGpuStatus().failureKind == TownGpuFailureKind::None && InspectQualityPicking(out).valid,
	    "capacity pressure recovers hardware automatically after viewport change without OFF/ON");
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	Check(DrawTownView(out, true) && !GetTownViewRendererState().usedGpu && GetTownViewRendererState().failure.empty(),
	    "switching GPU off clears the blocked backend after returning to a supported viewport");
	GetOptions().Graphics.townViewGpuRendering.SetValue(true);
	Check(DrawTownView(out, true) && GetTownViewRendererState().requestedGpu && GetTownViewRendererState().usedGpu
	        && GetTownViewRendererState().cpuRasterizedTriangles == 0 && GetTownViewRendererState().gpuSubmittedTriangles > 0
	        && GetTownViewRendererState().failure.empty() && GetTownGpuStatus().frameSucceeded && !GetTownGpuStatus().warp
	        && InspectQualityPicking(out).valid,
	    "switching GPU back on recovers real hardware rendering after a bounded budget failure");
	bool recoveryGuards = true;
	for (int y = gnViewportHeight; y < recovery.h(); ++y)
		for (int x = 0; x < recovery.w(); ++x)
			recoveryGuards = recoveryGuards && recovery[{ x, y }] == 255;
	Check(recoveryGuards && NativeSceneState() == original && GetLCGEngineState() == randomState,
	    "real GPU failure and OFF/ON recovery preserve simulation, RNG and UI guard rows");
	std::ostringstream result;
	result << "{\"width\":2304,\"viewportHeight\":" << budgetViewportHeight
	       << ",\"qualityRequested\":false,\"requestedGpu\":true,\"usedGpu\":false,\"failure\":" << std::quoted(failure)
	       << ",\"exactCpuFallback\":true,\"repeatedBlockedFrameExact\":true,\"hardwareRecoveredAutomaticallyAfterResize\":true,\"hardwareRecoveredAfterOffOn\":true,\"fallbackFullDrawMilliseconds\":"
	       << fallbackMilliseconds << '}';
	Record("INFO GPU real budget fallback " + result.str());
	return result.str();
}

void RunGpuWorld(const std::filesystem::path &output)
{
	InitializeTownDiagnostic();
	CheckGpuVideoOptions();
	ToggleTownView();
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	const std::string original = NativeSceneState();
	const auto randomState = GetLCGEngineState();
	std::ofstream manifest(output / "gpu-world.json");
	manifest << std::setprecision(9) << "{\"archiveMode\":\"" << (gbIsSpawn ? "shareware" : "retail")
	    << "\",\"gameWindowCapture\":false,\"hardwareRequired\":true,\"cpuRasterFallbackAllowedInComparisonCases\":false,"
	    << "\"timingScope\":\"first calls and five warm DrawTownView calls per backend including resolve/readback; excludes SDL/UI and is not game FPS\","
	    << "\"comparison\":\"identity and depth on stable planar interiors; raster edge rules and subepsilon ties can differ\",\"cases\":[\n";
	struct Case { const char *name; int width, height; float yaw; bool panel, zoom, quality; };
	const std::array<Case, 6> cases { Case { "native-forced", 640, 480, 0, false, false, false },
		Case { "orbit-90", 640, 480, 1.57079632679F, false, false, false },
		Case { "odd-panel-pan-zoom", 645, 481, 0.08F, true, true, false },
		Case { "retained-world-2x", 640, 480, 0.12F, false, false, true },
		Case { "wide-2x", 960, 540, 0.12F, false, false, true },
		Case { "full-hd-native-zoom", 1920, 1080, 0.12F, false, true, false } };
	bool firstCase = true;
	bool allComparisonsPassed = true;
	for (const auto &test : cases) {
		gnScreenWidth = test.width;
		gnScreenHeight = test.height;
		CharFlag = test.panel;
		GetOptions().Graphics.zoom.SetValue(test.zoom);
		GetOptions().Graphics.townViewAntialiasing.SetValue(test.quality);
		CalculatePanelAreas();
		CalcViewportGeometry();
		ResetTownViewCamera();
		OrbitTownView(test.yaw, test.panel ? 0.04F : 0);
		if (test.panel) {
			Check(BeginTownViewCameraDrag({ 410, 150 }, true) && UpdateTownViewCameraDrag({ 427, 161 }),
			    "GPU fixture uses the live logical panel and pan path");
			EndTownViewCameraDrag();
			ZoomTownView(1);
		}
		const auto camera = GetTownViewCameraState();
		const auto cameraPreserved = [&] {
			const auto now = GetTownViewCameraState();
			return now.yaw == camera.yaw && now.pitch == camera.pitch && now.distance == camera.distance
			    && now.offsetX == camera.offsetX && now.offsetZ == camera.offsetZ;
		};
		OwnedSurface allocation(test.width, test.height + 2);
		SDL_SetPaletteColors(allocation.surface->format->palette, logical_palette.data(), 0, 256);
		SDL_FillRect(allocation.surface, nullptr, 255);
		const Surface out = allocation.subregionY(0, test.height);
		const auto timedDraw = [&] {
			const auto begin = std::chrono::steady_clock::now();
			const bool drawn = DrawTownView(out, true);
			const auto end = std::chrono::steady_clock::now();
			Check(drawn, "complete world draw including GPU readback and optional color resolve");
			return std::chrono::duration<double, std::milli>(end - begin).count();
		};
		const auto guardsPreserved = [&] {
			for (int y = gnViewportHeight; y < allocation.h(); ++y)
				for (int x = 0; x < allocation.w(); ++x)
					if (allocation[{ x, y }] != 255)
						return false;
			return true;
		};
		GetOptions().Graphics.townViewGpuRendering.SetValue(false);
		Record("INFO GPU real-asset case=" + std::string(test.name));
		const double cpuFirstMilliseconds = timedDraw();
		const auto cpuState = GetTownViewRendererState();
		Check(!cpuState.requestedGpu && !cpuState.usedGpu && cpuState.cpuRasterizedTriangles > 0,
		    "CPU reference explicitly identifies its active raster backend");
		const auto cpuPixels = ViewportPixels(out);
		const auto cpuPicks = ReadGpuWorldPicks(out);
		const auto cpuHash = InspectQualityPicking(out).hash;
		std::array<double, 5> cpuWarmMilliseconds;
		for (double &time : cpuWarmMilliseconds)
			time = timedDraw();
		Check(ViewportPixels(out) == cpuPixels && InspectQualityPicking(out).hash == cpuHash,
		    "five warm CPU timing draws preserve the frozen reference pixels, picks and depth");
		const auto projection = TownViewScreenPosition(ViewPosition);
		SavePng(out.subregionY(0, gnViewportHeight), output / (std::string(test.name) + "-cpu.png"));
		GetOptions().Graphics.townViewGpuRendering.SetValue(true);
		Point tile;
		int npc, item, player;
		Check(!Pick(projection, tile, npc, item, player) && TownViewArchitectureAt(projection) == -1
		        && !std::isfinite(TownViewDepthAt(projection)) && GetTownViewHighResolutionFrame() == nullptr,
		    "changing the requested GPU backend invalidates the previous frame before redraw");
		const double gpuFirstMilliseconds = timedDraw();
		const auto gpuState = GetTownViewRendererState();
		const auto status = GetTownGpuStatus();
		Check(gpuState.requestedGpu && gpuState.usedGpu && gpuState.cpuRasterizedTriangles == 0
		        && gpuState.gpuSubmittedTriangles > 0 && status.available && status.frameSucceeded && !status.warp,
		    "hardware GPU renders the entire world without CPU triangle rasterization or WARP: " + gpuState.failure);
		const auto sampling = GetTownViewSamplingState();
		const Surface *retained = GetTownViewHighResolutionFrame();
		Check(sampling.factor == (test.quality ? 2 : 1)
		        && (test.quality ? retained != nullptr && retained->w() == out.w() * 2 && retained->h() == gnViewportHeight * 2 : retained == nullptr)
		        && TownViewScreenPosition(ViewPosition) == projection,
		    "GPU density and retained presentation preserve logical projection and UI framing");
		const auto gpuPixels = ViewportPixels(out);
		const auto gpuPicks = ReadGpuWorldPicks(out);
		const auto picking = InspectQualityPicking(out);
		Check(picking.valid && picking.picked > 0 && picking.architecture > 0,
		    "GPU readback exposes valid live entity IDs and architectural ownership");
		const GpuRayFrame independentRays;
		size_t stable = 0, identityMismatch = 0, depthMismatch = 0, colorMismatch = 0, classifiedGroundTies = 0, classifiedRasterEdges = 0;
		for (int y = 1; y + 1 < gnViewportHeight; ++y) {
			for (int x = 1; x + 1 < out.w(); ++x) {
				const size_t i = static_cast<size_t>(y) * out.w() + x;
				colorMismatch += gpuPixels[i] != cpuPixels[i] ? 1 : 0;
				const auto &reference = cpuPicks[i];
				if (!reference.picked || !std::isfinite(reference.depth))
					continue;
				bool interior = true;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
						interior = interior && reference.SameIdentity(cpuPicks[static_cast<size_t>(y + dy) * out.w() + x + dx]);
				const auto at = [&](int dx, int dy) { return cpuPicks[static_cast<size_t>(y + dy) * out.w() + x + dx].depth; };
				interior = interior && std::abs(at(-1, 0) + at(1, 0) - 2 * reference.depth) < 0.001F
				    && std::abs(at(0, -1) + at(0, 1) - 2 * reference.depth) < 0.001F;
				if (!interior)
					continue;
				++stable;
				const bool identityDiffers = !reference.SameIdentity(gpuPicks[i]);
				const bool depthDiffers = !std::isfinite(gpuPicks[i].depth) || std::abs(gpuPicks[i].depth - reference.depth) > 0.002F;
				identityMismatch += identityDiffers ? 1 : 0;
				depthMismatch += depthDiffers ? 1 : 0;
				const auto groundIdentity = [](const GpuPickSnapshot &sample) {
					return sample.picked && sample.npc < 0 && sample.item < 0 && sample.player < 0 && sample.architecture < 0;
				};
				const bool groundTie = identityDiffers && groundIdentity(reference) && groundIdentity(gpuPicks[i])
				    && std::abs(reference.depth - gpuPicks[i].depth) <= 0.0001F
				    && independentRays.MatchesGroundDepth({ x, y }, sampling.factor, reference.depth)
				    && independentRays.MatchesGroundDepth({ x, y }, sampling.factor, gpuPicks[i].depth);
				classifiedGroundTies += groundTie ? 1 : 0;
				if (depthDiffers && groundIdentity(reference) && reference.SameIdentity(gpuPicks[i]))
					classifiedRasterEdges += AuditGpuVegetationDepth(reference.tile, { x, y }, sampling.factor, reference.depth, gpuPicks[i].depth) ? 1 : 0;
				if ((identityDiffers || depthDiffers) && identityMismatch + depthMismatch <= 16) {
					const auto describe = [](const GpuPickSnapshot &sample) {
						return "picked=" + std::to_string(sample.picked) + " tile=" + std::to_string(sample.tile.x) + "," + std::to_string(sample.tile.y)
						    + " npc=" + std::to_string(sample.npc) + " item=" + std::to_string(sample.item) + " player=" + std::to_string(sample.player)
						    + " architecture=" + std::to_string(sample.architecture) + " depth=" + std::to_string(sample.depth);
					};
					Record("INFO GPU world mismatch " + std::string(test.name) + " x=" + std::to_string(x) + " y=" + std::to_string(y)
					    + " CPU[" + describe(reference) + "] GPU[" + describe(gpuPicks[i]) + "]");
				}
			}
		}
		Record("INFO GPU " + std::string(test.name) + " stable=" + std::to_string(stable)
		    + " identityMismatch=" + std::to_string(identityMismatch) + " depthMismatch=" + std::to_string(depthMismatch)
		    + " classifiedGroundTies=" + std::to_string(classifiedGroundTies) + " classifiedRasterEdges=" + std::to_string(classifiedRasterEdges)
		    + " indexedColorDifferences=" + std::to_string(colorMismatch));
		SavePng(out.subregionY(0, gnViewportHeight), output / (std::string(test.name) + "-gpu.png"));
		allComparisonsPassed = allComparisonsPassed && stable > 0 && identityMismatch == classifiedGroundTies && depthMismatch == classifiedRasterEdges;
		std::array<double, 5> gpuWarmMilliseconds;
		for (double &time : gpuWarmMilliseconds) {
			time = timedDraw();
			Check(GetTownViewRendererState().usedGpu && GetTownViewRendererState().cpuRasterizedTriangles == 0,
			    "each warm GPU timing draw uses hardware without CPU triangle rasterization");
		}
		const auto warmStatus = GetTownGpuStatus();
		Check(GetTownViewRendererState().usedGpu
		        && GetTownViewRendererState().cpuRasterizedTriangles == 0
		        && ViewportPixels(out) == gpuPixels && InspectQualityPicking(out).hash == picking.hash,
		    "a frozen GPU frame repeats deterministically, including selection and depth");
		Check(cameraPreserved() && NativeSceneState() == original && GetLCGEngineState() == randomState && guardsPreserved(),
		    "GPU draws preserve camera, native map, collision, actors, simulation RNG and all UI guard rows");
		++gnScreenWidth;
		Check(!Pick(projection, tile, npc, item, player) && !std::isfinite(TownViewDepthAt(projection))
		        && GetTownViewHighResolutionFrame() == nullptr, "logical resize invalidates GPU readback and retained image before redraw");
		--gnScreenWidth;
		GetOptions().Graphics.townViewGpuRendering.SetValue(false);
		const double cpuRepeatMilliseconds = timedDraw();
		Check(!GetTownViewRendererState().usedGpu && ViewportPixels(out) == cpuPixels
		        && InspectQualityPicking(out).hash == cpuHash,
		    "CPU to GPU to CPU restores the exact original CPU pixels, IDs and depth");
		if (!firstCase)
			manifest << ",\n";
		firstCase = false;
		auto sortedCpu = cpuWarmMilliseconds;
		auto sortedGpu = gpuWarmMilliseconds;
		std::sort(sortedCpu.begin(), sortedCpu.end());
		std::sort(sortedGpu.begin(), sortedGpu.end());
		Record("INFO GPU timing " + std::string(test.name) + " CPUmedian=" + std::to_string(sortedCpu[2])
		    + " ms GPUmedian=" + std::to_string(sortedGpu[2]) + " ms readbackLast=" + std::to_string(warmStatus.readbackMilliseconds) + " ms");
		manifest << "{\"name\":\"" << test.name << "\",\"width\":" << out.w() << ",\"height\":" << gnViewportHeight
		    << ",\"factor\":" << sampling.factor << ",\"nativeZoom\":" << (test.zoom ? "true" : "false")
		    << ",\"leftPanelOpen\":" << (test.panel ? "true" : "false") << ",\"triangles\":" << gpuState.gpuSubmittedTriangles
		    << ",\"drawCalls\":" << status.drawCalls << ",\"frameMilliseconds\":" << status.frameMilliseconds
		    << ",\"readbackMilliseconds\":" << status.readbackMilliseconds << ",\"adapter\":" << std::quoted(status.adapter)
		    << ",\"cpuFirstFullDrawMilliseconds\":" << cpuFirstMilliseconds << ",\"gpuFirstFullDrawMilliseconds\":" << gpuFirstMilliseconds
		    << ",\"cpuRoundtripFullDrawMilliseconds\":" << cpuRepeatMilliseconds
		    << ",\"cpuMedianFullDrawMilliseconds\":" << sortedCpu[2] << ",\"gpuMedianFullDrawMilliseconds\":" << sortedGpu[2]
		    << ",\"gpuWarmBackendMilliseconds\":" << warmStatus.frameMilliseconds << ",\"gpuWarmReadbackMilliseconds\":" << warmStatus.readbackMilliseconds
		    << ",\"stableInteriorPixels\":" << stable << ",\"identityMismatch\":" << identityMismatch
		    << ",\"depthMismatch\":" << depthMismatch << ",\"classifiedGroundTies\":" << classifiedGroundTies
		    << ",\"classifiedFixedPointEdges\":" << classifiedRasterEdges
		    << ",\"unexplainedIdentityMismatch\":" << identityMismatch - classifiedGroundTies
		    << ",\"unexplainedDepthMismatch\":" << depthMismatch - classifiedRasterEdges
		    << ",\"indexedColorDifferences\":" << colorMismatch << ",\"cpuWarmMilliseconds\":[";
		for (size_t i = 0; i < cpuWarmMilliseconds.size(); ++i)
			manifest << (i == 0 ? "" : ",") << cpuWarmMilliseconds[i];
		manifest << "],\"gpuWarmMilliseconds\":[";
		for (size_t i = 0; i < gpuWarmMilliseconds.size(); ++i)
			manifest << (i == 0 ? "" : ",") << gpuWarmMilliseconds[i];
		manifest << "]}";
	}
	const std::string budgetFallback = CheckGpuWorldBudgetFallback();
	CharFlag = false;
	GetOptions().Graphics.zoom.SetValue(false);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	GetOptions().Graphics.townViewGpuRendering.SetValue(true);
	gnScreenWidth = 640;
	gnScreenHeight = 480;
	CalculatePanelAreas();
	CalcViewportGeometry();
	ResetTownViewCamera();
	OwnedSurface native(640, 480);
	DrawActualNativeReference(native);
	const auto originalPixels = ViewportPixels(native);
	Check(DrawTownView(native) && ViewportPixels(native) == originalPixels && !GetTownViewRendererState().usedGpu
	        && GetTownViewHighResolutionFrame() == nullptr, "Home keeps the exact original backend authoritative even when GPU rendering is requested");
	Check(NativeSceneState() == original && GetLCGEngineState() == randomState,
	    "complete GPU fixture preserves native state and simulation RNG");
	manifest << "\n],\"budgetFallback\":" << budgetFallback
	    << ",\"nativeBackendExact\":true,\"cpuRoundtripExact\":true,\"statePreserved\":true,\"allComparisonsPassed\":"
	    << (allComparisonsPassed ? "true" : "false") << "}\n";
	Check(manifest.good(), "record GPU backend, draw submission, readback and comparison limits");
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	ResetTownViewResources();
	FreeTownerGFX();
	Check(allComparisonsPassed, "GPU selection and depth have no unexplained disagreement; classified ground ties and fixed-point edges retain explicit independent geometric evidence");
}

void RunCabinOpenings(const std::filesystem::path &output)
{
	InitializeTownDiagnostic();
	ToggleTownView();
	PlaceFixturePlayerNear({ 76, 69 });
	ViewPosition = { 73, 68 };
	CharFlag = false;
	GetOptions().Graphics.zoom.SetValue(true);
	GetOptions().Graphics.townViewAntialiasing.SetValue(false);
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	gnScreenWidth = 960;
	gnScreenHeight = 640 + GetMainPanel().size.height;
	CalculatePanelAreas();
	CalcViewportGeometry();
	OwnedSurface allocation(gnScreenWidth, gnScreenHeight + 2);
	SDL_SetPaletteColors(allocation.surface->format->palette, logical_palette.data(), 0, 256);
	SDL_FillRect(allocation.surface, nullptr, 255);
	const Surface out = allocation.subregionY(0, gnScreenHeight);
	ResetTownViewCamera();
	ZoomTownView(3);
	Check(DrawTownView(out, true), "prepare an actual selected cabin in an isolated close-up fixture");
	const auto &scene = GetTownScene();
	const auto found = std::find_if(scene.begin(), scene.end(), [](const TownSceneModel &model) {
		return model.kind == TownSceneKind::Cabin && model.minTile == Point { 70, 66 };
	});
	Check(found != scene.end() && found->externalModel && found->cabinInterior != nullptr,
	    "targeted opening review requires the actual selected imported cabin and runtime adjunct");
	const int modelIndex = static_cast<int>(found - scene.begin());
	const std::string original = NativeSceneState();
	const std::string geometry = SceneGeometryState(scene);
	const auto randomState = GetLCGEngineState();
	std::ofstream manifest(output / "cabin-openings.json");
	manifest << "{\"gameWindowCapture\":false,\"sourceTriangles\":5783,\"fireTimeSeconds\":0,"
	            "\"scope\":\"actual CPU/hardware-GPU door-window closeups; source divisions occlude visually, room light portal is rectangular\",\"frames\":[";
	bool first = true;
	for (bool gpu : { false, true }) {
		GetOptions().Graphics.townViewGpuRendering.SetValue(gpu);
		for (int degrees : { 0, -5, 5, -45 }) {
			ResetTownViewCamera();
			ZoomTownView(3);
			OrbitTownView(degrees * 3.14159265358979323846F / 180, 0);
			Check(DrawTownView(out, true), "draw selected door-window closeup with the requested backend");
			const auto renderer = GetTownViewRendererState();
			Check(renderer.requestedGpu == gpu && renderer.usedGpu == gpu
			        && (gpu ? renderer.cpuRasterizedTriangles == 0 && renderer.gpuSubmittedTriangles > 0
			                       && GetTownGpuStatus().frameSucceeded && !GetTownGpuStatus().warp
			                : renderer.cpuRasterizedTriangles > 0),
			    "targeted door opening uses CPU or hardware GPU explicitly, without a silent fallback: " + renderer.failure);
			const auto directory = output / (gpu ? "gpu" : "cpu");
			std::filesystem::create_directories(directory);
			SavePng(out.subregionY(0, gnViewportHeight), directory / ("door-orbit-" + std::to_string(degrees) + ".png"));
			const auto lit = ViewportPixels(out);
			const GpuRayFrame camera;
			int minX = out.w(), maxX = 0, minY = gnViewportHeight, maxY = 0;
			for (float z : { 67.315F, 67.625F }) {
				for (float height : { 1.035F, 1.395F }) {
					const auto projected = camera.RasterVertex({ 73.04F, height, z, 0, 0 }, 1);
					minX = std::min(minX, static_cast<int>(std::floor(projected[0])));
					maxX = std::max(maxX, static_cast<int>(std::ceil(projected[0])));
					minY = std::min(minY, static_cast<int>(std::floor(projected[1])));
					maxY = std::max(maxY, static_cast<int>(std::ceil(projected[1])));
				}
			}
			minX = std::clamp(minX, 0, out.w() - 1);
			maxX = std::clamp<int>(maxX, minX + 1, out.w());
			minY = std::clamp(minY, 0, gnViewportHeight - 1);
			maxY = std::clamp<int>(maxY, minY + 1, gnViewportHeight);
			SetTownViewCabinFireEnabledForDiagnostics(false);
			Check(DrawTownView(out, true), "draw the same door-window closeup without candle lighting and emission");
			SavePng(out.subregionY(0, gnViewportHeight), directory / ("door-fire-off-" + std::to_string(degrees) + ".png"));
			int changed = 0, warm = 0, outsideOwner = 0;
			for (int y = minY; y < maxY; ++y) {
				for (int x = minX; x < maxX; ++x) {
					const uint8_t colorIndex = lit[static_cast<size_t>(y) * out.w() + x];
					if (colorIndex == out[{ x, y }])
						continue;
					++changed;
					outsideOwner += TownViewArchitectureAt({ x, y }) != modelIndex ? 1 : 0;
					const auto color = logical_palette[colorIndex];
					warm += color.r > 30 && color.r >= color.g && color.g > color.b * 1.5 ? 1 : 0;
				}
			}
			Record("INFO door opening " + std::string(gpu ? "GPU" : "CPU") + " orbit=" + std::to_string(degrees)
			    + " candleChanged=" + std::to_string(changed) + " warmChanged=" + std::to_string(warm)
			    + " changesOutsideCabinOwner=" + std::to_string(outsideOwner));
			Check(changed > 0 && warm > 0 && outsideOwner == 0,
			    "actual door-window pixels reveal warm candle-lit interior through the preserved imported divisions");
			SetTownViewCabinFireEnabledForDiagnostics(true);
			Check(DrawTownView(out, true) && ViewportPixels(out) == lit,
			    "frozen candle time restores the exact same door-window frame");
			CheckCabinInteriorLight(out, modelIndex, directory, degrees);
			bool guards = true;
			for (int y = gnViewportHeight; y < allocation.h(); ++y)
				for (int x = 0; x < allocation.w(); ++x)
					guards = guards && allocation[{ x, y }] == 255;
			Check(guards && NativeSceneState() == original && GetLCGEngineState() == randomState && SceneGeometryState(GetTownScene()) == geometry,
			    "door-window capture preserves imported source, native closed-door collision, simulation, RNG and UI guard rows");
			manifest << (first ? "" : ",") << "{\"backend\":\"" << (gpu ? "hardware-gpu" : "cpu")
			         << "\",\"orbitDegrees\":" << degrees << ",\"doorChangedPixels\":" << changed << ",\"doorWarmChangedPixels\":" << warm
			         << ",\"changesOutsideCabinOwner\":" << outsideOwner << ",\"doorBoundsPixels\":[" << minX << ',' << minY << ',' << maxX << ',' << maxY << "]}";
			first = false;
		}
	}
	manifest << "]}\n";
	Check(manifest.good(), "record actual CPU/GPU opening closeups and proportional candle evidence");
	GetOptions().Graphics.townViewGpuRendering.SetValue(false);
	ResetTownViewResources();
	FreeTownerGFX();
}

#include "town_cabin_review.hpp"

} // namespace

namespace {

void ExportTownEditorGround(const std::filesystem::path &output)
{
	const auto directory = output / "ground";
	std::filesystem::create_directories(directory);
	std::map<std::string, std::pair<uint16_t, bool>> textures;
	std::ostringstream tiles;
	bool first = true;
	for (int z = 0; z < MAXDUNY; ++z) {
		for (int x = 0; x < MAXDUNX; ++x) {
			const uint16_t piece = dPiece[x][z];
			if (piece >= MAXTILES)
				Check(false, "editor ground references a valid native MIN piece");
			const bool fallback = TownSceneReplacesTile({ x, z }) || TownPropReplacesTile({ x, z })
			    || HasAnyOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile);
			const std::string key = fallback ? "fallback" : "p" + std::to_string(piece);
			textures.try_emplace(key, piece, fallback);
			tiles << (first ? "" : ",") << '[' << x << ',' << z << ",\"" << key << "\"]";
			first = false;
		}
	}
	std::ofstream manifest(output / "ground-reference.json");
	manifest << "{\"source\":\"same cleaned/fallback floor pixels as town_view; grass reference beneath solid scenery; no terrain editing\","
	            "\"exportLocked\":true,\"textures\":[";
	first = true;
	for (const auto &[key, source] : textures) {
		const auto texture = GetTownGroundReferenceTexture(source.first, source.second);
		Check(texture.width == 64 && texture.height == 32 && texture.rgba.size() == 64 * 32 * 4,
		    "editor ground exports actual 64x32 palette pixels and opacity");
		std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
		    SDL_CreateRGBSurfaceWithFormat(0, texture.width, texture.height, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
		Check(surface != nullptr, "allocate local editor ground texture");
		for (int y = 0; y < texture.height; ++y)
			std::memcpy(static_cast<uint8_t *>(surface->pixels) + y * surface->pitch,
			    texture.rgba.data() + static_cast<size_t>(y) * texture.width * 4, texture.width * 4);
		SavePng(Surface { surface.get() }, directory / (key + ".png"));
		manifest << (first ? "" : ",") << "{\"key\":\"" << key << "\",\"path\":\"ground/" << key
		         << ".png\",\"width\":64,\"height\":32}";
		first = false;
	}
	manifest << "],\"tiles\":[" << tiles.str() << "]}\n";
	Check(manifest.good(), "export native town floor layout and locked texture references for Godot");
}

} // namespace

// Reuses native scene helpers without a window, GPU or gameplay ticks.
#include "town_camera_eye_height_checks.hpp"
#include "town_camera_follow_checks.hpp"
#include "ogden_idle_render_checks.hpp"

int main(int argc, char **argv)
{
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;
	const bool followCamera = argc == 5 && std::string(argv[4]) == "--follow-camera";
	const bool ogdenIdle = argc == 5 && std::string(argv[4]) == "--ogden-idle";
	const bool eyeHeight = (argc == 6 || argc == 7) && std::string(argv[4]) == "--eye-height";
	const bool presentation = argc == 5 && std::string(argv[4]) == "--presentation";
	const bool quality = argc == 5 && std::string(argv[4]) == "--quality";
	const bool gpu = argc == 5 && std::string(argv[4]) == "--gpu";
	const bool gpuRecovery = argc == 5 && std::string(argv[4]) == "--gpu-recovery";
	const bool camera = argc == 5 && std::string(argv[4]) == "--camera";
	const bool cameraExtra = argc == 5 && std::string(argv[4]) == "--camera-extra";
	const bool architectureCulling = argc == 5 && std::string(argv[4]) == "--architecture-culling";
	const bool firstPersonPerformance = argc == 5 && std::string(argv[4]) == "--first-person-performance";
	const bool residentMeshes = argc == 5 && (std::string(argv[4]) == "--resident-meshes" || std::string(argv[4]) == "--resident-fullhd");
	const bool residentFullHd = residentMeshes && std::string(argv[4]) == "--resident-fullhd";
	const bool residentZoomStress = argc == 5 && std::string(argv[4]) == "--resident-zoom-stress";
	const bool ingameMenuVisual = argc == 5 && std::string(argv[4]) == "--ingame-menu-visual";
	const bool cabinOpenings = argc == 5 && std::string(argv[4]) == "--cabin-openings";
	const bool cabinReview = argc == 5 && std::string(argv[4]) == "--cabin-review";
	const bool editorSnapshot = argc == 5 && std::string(argv[4]) == "--editor-snapshot";
	const bool editorChecks = argc == 5 && std::string(argv[4]) == "--editor-map-checks";
	const bool layers = argc == 3 && std::string(argv[1]) == "--presentation-layers";
	const bool gpuFixtures = argc == 3 && std::string(argv[1]) == "--gpu-fixtures";
	const bool synthetic = layers || gpuFixtures;
	if (argc != 4 && !presentation && !quality && !gpu && !gpuRecovery && !camera && !cameraExtra && !architectureCulling && !firstPersonPerformance && !residentMeshes && !residentZoomStress && !ingameMenuVisual && !cabinOpenings && !cabinReview && !editorSnapshot && !editorChecks && !synthetic && !eyeHeight && !followCamera && !ogdenIdle) {
		std::cerr << "Usage: town_view_smoke <game-data-directory> <built-assets-directory> <capture-directory> --eye-height 1.1|1.7 [baseline-directory]\n"
		          << "       town_view_smoke <game-data-directory> <built-assets-directory> <capture-directory> [--ogden-idle|--follow-camera|--presentation|--quality|--gpu|--gpu-recovery|--camera|--camera-extra|--architecture-culling|--first-person-performance|--resident-meshes|--resident-fullhd|--resident-zoom-stress|--ingame-menu-visual|--cabin-openings|--cabin-review|--editor-snapshot|--editor-map-checks]\n"
		          << "       town_view_smoke --presentation-layers <synthetic-capture-directory>\n"
		          << "       town_view_smoke --gpu-fixtures <synthetic-capture-directory>\n";
		return 2;
	}
	const std::filesystem::path output = std::filesystem::absolute(argv[synthetic ? 2 : 3]);
	std::filesystem::create_directories(output);
	std::ofstream log(output / "town-view-runtime.txt");
	SDL_LogSetOutputFunction([](void *userdata, int, SDL_LogPriority, const char *message) {
		auto &stream = *static_cast<std::ofstream *>(userdata);
		stream << message << std::endl;
		std::cerr << message << '\n';
	}, &log);
	if (!synthetic) {
		devilution::paths::SetBasePath(argv[1]);
		devilution::paths::SetAssetsPath(argv[2]);
	}
	devilution::paths::SetPrefPath(output.string());
	devilution::paths::SetConfigPath(output.string());
	SDL_SetMainReady();
	SDL_setenv("SDL_VIDEODRIVER", "dummy", (eyeHeight || followCamera || ogdenIdle) ? 1 : 0);
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		std::cerr << SDL_GetError() << '\n';
		return 2;
	}
	const auto start = std::chrono::steady_clock::now();
	int status = 0;
	try {
		if (layers) {
#ifndef USE_SDL1
			RunPresentationLayers(output);
#else
			Check(false, "layered presentation fixtures require SDL2 or newer");
#endif
		} else if (gpuFixtures)
			RunGpuFixtures(output);
		else if (ogdenIdle) {
			ogden_idle_render_gate::Run(output);
			FreeTownerGFX();
		}
		else if (followCamera) {
			RunFollowCameraCpu(output);
			FreeTownerGFX();
		}
		else if (eyeHeight) {
			size_t cameraChecks = 0;
			Check(RunTownCameraChecks(std::cout, cameraChecks), "pure camera contract passes before native eye-height capture");
			RunFirstPersonEyeHeightCpu(output, std::stof(argv[5]),
				argc == 7 ? std::filesystem::absolute(argv[6]) : std::filesystem::path {});
			FreeTownerGFX();
		}
		else if (presentation)
			RunPresentation(output);
		else if (quality)
			RunQuality(output);
		else if (gpu)
			RunGpuWorld(output);
		else if (gpuRecovery) {
			RunTownGpuRecoveryPolicyChecks(Check);
			InitializeTownDiagnostic();
			if (!IsTownViewActive())
				ToggleTownView();
			CheckGpuWorldBudgetFallback();
			FreeTownerGFX();
		}
		else if (camera) {
			RunTownCameraRuntimeEntry(output, InitializeTownDiagnostic, Check, NativeSceneState, ViewportPixels, SavePng, std::cout);
			FreeTownerGFX();
		}
		else if (cameraExtra) {
			InitializeTownDiagnostic();
			gnScreenWidth = 960;
			gnScreenHeight = 540;
			CalculatePanelAreas();
			CalcViewportGeometry();
			OwnedSurface out(960, 540);
			SDL_SetPaletteColors(out.surface->format->palette, logical_palette.data(), 0, 256);
			for (const bool useGpu : { false, true }) {
				const auto directory = output / (useGpu ? "gpu" : "cpu");
				std::filesystem::create_directories(directory);
				RunTownCameraCaptureChecks(out, Check, NativeSceneState, ViewportPixels, SavePng, directory, std::cout, useGpu);
			}
			FreeTownerGFX();
		}
		else if (residentMeshes || residentZoomStress) {
			InitializeTownDiagnostic();
			gnScreenWidth = residentFullHd || residentZoomStress ? 1920 : 960;
			gnScreenHeight = residentFullHd || residentZoomStress ? 1080 : 540;
			CalculatePanelAreas();
			CalcViewportGeometry();
			OwnedSurface out(gnScreenWidth, gnScreenHeight);
			SDL_SetPaletteColors(out.surface->format->palette, logical_palette.data(), 0, 256);
			if (residentZoomStress) {
				RunTownResidentZoomStressChecks(out, Check, NativeSceneState, ViewportPixels, std::cout);
			} else if (residentFullHd) {
				GetOptions().Graphics.townViewCameraFov.SetValue(80);
				for (const int mode : { 2, 3 })
					RunTownResidentMeshCaptureChecks(out, Check, NativeSceneState, ViewportPixels, std::cout, mode, false);
			} else {
				GetOptions().Graphics.townViewCameraFov.SetValue(80);
				RunTownResidentMeshCaptureChecks(out, Check, NativeSceneState, ViewportPixels, std::cout);
			}
			FreeTownerGFX();
		}
		else if (architectureCulling || firstPersonPerformance) {
			size_t checks = 0;
			Check(RunTownArchitectureCullingChecks(std::cout, checks), "conservative architecture bounds and frustum fixtures pass");
			InitializeTownDiagnostic();
			gnScreenWidth = firstPersonPerformance ? 1920 : 960;
			gnScreenHeight = firstPersonPerformance ? 1080 : 540;
			CalculatePanelAreas();
			CalcViewportGeometry();
			OwnedSurface out(gnScreenWidth, gnScreenHeight);
			SDL_SetPaletteColors(out.surface->format->palette, logical_palette.data(), 0, 256);
			if (firstPersonPerformance) {
				GetOptions().Graphics.townViewCameraFov.SetValue(80);
				RunTownArchitectureCullingCaptureChecks(out, Check, NativeSceneState, ViewportPixels, std::cout, true, 3);
			} else {
				for (const bool useGpu : { false, true })
					RunTownArchitectureCullingCaptureChecks(out, Check, NativeSceneState, ViewportPixels, std::cout, useGpu);
			}
			FreeTownerGFX();
		}
		else if (ingameMenuVisual) {
			RunInGameMenuVisualChecks(output, InitializeTownDiagnostic, Check, NativeSceneState, SavePng, std::cout);
			FreeTownerGFX();
		}
		else if (cabinOpenings)
			RunCabinOpenings(output);
		else if (cabinReview)
			RunCabinReview(output);
		else if (editorSnapshot) {
			InitializeTownDiagnostic();
			const std::string native = NativeSceneState();
			const auto random = GetLCGEngineState();
			Check(ExportTownEditorSnapshot(output), "export the actual native-bound architecture, decoded model identities, openings, fire geometry and collision for Godot");
			ExportTownEditorGround(output);
			Check(NativeSceneState() == native && GetLCGEngineState() == random,
			    "Godot snapshot export preserves native simulation, map, actors and random state");
			FreeTownerGFX();
		}
		else if (editorChecks) {
			InitializeTownDiagnostic();
			CheckTownEditorMapLoader(output, Check);
			FreeTownerGFX();
		}
		else
			Run(output);
	} catch (const std::exception &error) {
		std::cerr << "Smoke check stopped: " << error.what() << '\n';
		status = 1;
	}
	std::ofstream report(output / "town-view-smoke.txt");
	for (const auto &result : Results)
		report << result << '\n';
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	report << "Elapsed: " << elapsed << " ms\n";
	std::cout << "Elapsed: " << elapsed << " ms\n";
	SDL_Quit();
	return status;
}
