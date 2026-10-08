#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "engine/render/town_editor_map.hpp"
#include "engine/render/town_model_import.hpp"
#include "engine/render/town_scene.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "mods/mod_identity.h"

namespace devilution {
namespace town_editor_snapshot_detail {

inline void WriteString(std::ostream &out, std::string_view value)
{
	constexpr char Hex[] = "0123456789abcdef";
	out.put('"');
	for (const unsigned char character : value) {
		switch (character) {
		case '"': out << "\\\""; break;
		case '\\': out << "\\\\"; break;
		case '\b': out << "\\b"; break;
		case '\f': out << "\\f"; break;
		case '\n': out << "\\n"; break;
		case '\r': out << "\\r"; break;
		case '\t': out << "\\t"; break;
		default:
			if (character < 0x20) {
				out << "\\u00" << Hex[character >> 4] << Hex[character & 0x0F];
			} else {
				out.put(static_cast<char>(character));
			}
			break;
		}
	}
	out.put('"');
}

inline void WritePoint(std::ostream &out, Point point)
{
	out << '[' << point.x << ',' << point.y << ']';
}

inline void WriteVector(std::ostream &out, TownLightVector value)
{
	out << '[' << value.x << ',' << value.height << ',' << value.z << ']';
}

inline bool SafeInstanceId(std::string_view value)
{
	return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
		return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-';
	});
}

inline bool ValidTriangles(const std::vector<TownSceneTriangle> &triangles)
{
	for (const TownSceneTriangle &triangle : triangles) {
		for (const TownSceneVertex &vertex : triangle.vertices) {
			if (!std::isfinite(vertex.x) || !std::isfinite(vertex.height) || !std::isfinite(vertex.z)
			    || !std::isfinite(vertex.u) || !std::isfinite(vertex.v))
				return false;
		}
	}
	return true;
}

inline bool ValidModel(const TownSceneModel &model)
{
	if (!ValidTriangles(TownSceneExteriorTriangles(model)))
		return false;
	if (model.externalModel && !model.importedTexture)
		return false;
	if (model.importedTexture) {
		const TownImportedTexture &texture = *model.importedTexture;
		if (texture.width == 0 || texture.height == 0 || texture.width > 2048 || texture.height > 2048
		    || texture.rgb.size() != static_cast<std::size_t>(texture.width) * texture.height * 3)
			return false;
	}
	if (!model.cabinInterior)
		return true;
	if (!ValidTriangles(model.cabinInterior->interiorTriangles))
		return false;
	for (const TownCabinFireSource &fire : model.cabinInterior->fireSources) {
		const TownPointLight &light = fire.light;
		if (!ValidTriangles(fire.emissiveTriangles) || !std::isfinite(light.position.x)
		    || !std::isfinite(light.position.height) || !std::isfinite(light.position.z)
		    || !std::isfinite(light.color.red) || !std::isfinite(light.color.green) || !std::isfinite(light.color.blue)
		    || !std::isfinite(light.radius) || !std::isfinite(light.intensity))
			return false;
	}
	return true;
}

inline void WriteTriangles(std::ostream &out, const std::vector<TownSceneTriangle> &triangles, bool &first)
{
	for (const TownSceneTriangle &triangle : triangles) {
		out << (first ? "[" : ",[");
		first = false;
		for (std::size_t i = 0; i < triangle.vertices.size(); ++i) {
			const TownSceneVertex &vertex = triangle.vertices[i];
			out << (i == 0 ? "" : ",") << vertex.x << ',' << vertex.height << ',' << vertex.z << ',' << vertex.u << ',' << vertex.v;
		}
		out << ',' << static_cast<unsigned>(triangle.material) << ',' << static_cast<unsigned>(triangle.surfaceRole)
		    << ',' << static_cast<unsigned>(triangle.surfaceDetail) << ']';
	}
}

inline bool WriteTexture(const std::filesystem::path &path, const TownImportedTexture &texture)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out.imbue(std::locale::classic());
	out << "P6\n" << texture.width << ' ' << texture.height << "\n255\n";
	out.write(reinterpret_cast<const char *>(texture.rgb.data()), static_cast<std::streamsize>(texture.rgb.size()));
	out.close();
	return static_cast<bool>(out);
}

inline void WriteEditorMapAudit(std::ostream &out, const TownEditorMapAudit &audit)
{
	out << "{\"assetPath\":";
	WriteString(out, audit.assetPath);
	out << ",\"sourceKind\":";
	WriteString(out, audit.sourceKind);
	out << ",\"sha256\":";
	WriteString(out, audit.sha256);
	out << ",\"status\":";
	WriteString(out, audit.status);
	out << ",\"failure\":";
	WriteString(out, audit.failure);
	out << ",\"instances\":[";
	bool first = true;
	for (const TownEditorInstanceAudit &instance : audit.instances) {
		out << (first ? "" : ",") << "{\"instanceId\":";
		first = false;
		WriteString(out, instance.instanceId);
		out << ",\"assetPath\":";
		WriteString(out, instance.assetPath);
		out << ",\"expectedSha256\":";
		WriteString(out, instance.expectedSha256);
		out << ",\"sha256\":";
		WriteString(out, instance.sha256);
		out << ",\"revision\":";
		WriteString(out, instance.revision);
		out << ",\"status\":";
		WriteString(out, instance.status);
		out << ",\"failure\":";
		WriteString(out, instance.failure);
		out << '}';
	}
	out << "]}";
}

inline bool WriteModel(std::ostream &out, const std::filesystem::path &directory,
	const TownSceneModel &model, const TownEditorModelBinding &binding)
{
	std::ostringstream geometry;
	geometry.imbue(std::locale::classic());
	geometry << std::setprecision(std::numeric_limits<float>::max_digits10) << '[';
	bool firstTriangle = true;
	const auto &exterior = TownSceneExteriorTriangles(model);
	WriteTriangles(geometry, exterior, firstTriangle);
	std::size_t interiorCount = 0;
	std::size_t fireCount = 0;
	if (model.cabinInterior) {
		interiorCount = model.cabinInterior->interiorTriangles.size();
		WriteTriangles(geometry, model.cabinInterior->interiorTriangles, firstTriangle);
		for (const TownCabinFireSource &fire : model.cabinInterior->fireSources) {
			fireCount += fire.emissiveTriangles.size();
			WriteTriangles(geometry, fire.emissiveTriangles, firstTriangle);
		}
	}
	geometry << ']';
	if (!geometry)
		return false;
	const std::string triangles = geometry.str();
	const auto geometryHash = ComputeBytesSha256(std::as_bytes(std::span<const char>(triangles.data(), triangles.size())));
	const bool hasFire = model.cabinInterior && !model.cabinInterior->fireSources.empty();
	std::string texturePath;
	if (model.importedTexture) {
		texturePath = "textures/" + std::string(binding.instanceId) + ".ppm";
		if (!WriteTexture(directory / texturePath, *model.importedTexture))
			return false;
	}
	out << "{\"instanceId\":";
	WriteString(out, binding.instanceId);
	out << ",\"assetId\":";
	WriteString(out, binding.assetId);
	out << ",\"variantId\":";
	WriteString(out, binding.variantId);
	out << ",\"kind\":" << static_cast<unsigned>(model.kind) << ",\"nativeMin\":";
	WritePoint(out, binding.nativeMin);
	out << ",\"nativeMax\":";
	WritePoint(out, binding.nativeMax);
	out << ",\"sourceArtReference\":";
	WritePoint(out, model.nativeArtwork.referenceTile);
	out << ",\"sourceArtMin\":";
	WritePoint(out, model.nativeArtwork.minTile);
	out << ",\"sourceArtMax\":";
	WritePoint(out, model.nativeArtwork.maxTile);
	out << ",\"sha256\":";
	WriteString(out, model.runtimeAudit.sha256);
	out << ",\"revision\":";
	WriteString(out, model.runtimeAudit.revision);
	out << ",\"geometrySha256\":";
	WriteString(out, ModHashToHex(geometryHash));
	out << ",\"geometryHashEncoding\":\"utf8-triangles-json-float32-max-digits10\",\"external\":" << (model.externalModel ? "true" : "false")
	    << ",\"texture\":";
	WriteString(out, texturePath);
	out << ",\"textureFormat\":";
	WriteString(out, texturePath.empty() ? "" : "ppm-p6-rgb8-top-row-first");
	out << ",\"textureSurfaceRole\":0,\"appearance\":";
	WriteString(out, texturePath.empty() ? "prototype-material-colors" : "imported-base-color-with-prototype-interior");
	out << ",\"hasFireSources\":" << (hasFire ? "true" : "false") << ",\"readOnly\":" << (hasFire ? "true" : "false")
	    << ",\"readOnlyReason\":";
	WriteString(out, hasFire ? "runtime-fire-adjunct" : "");
	out << ",\"sourceAudit\":{\"assetPath\":";
	WriteString(out, model.runtimeAudit.assetPath);
	out << ",\"sourceKind\":";
	WriteString(out, model.runtimeAudit.sourceKind);
	out << ",\"expectedSha256\":";
	WriteString(out, model.runtimeAudit.expectedSha256);
	out << ",\"status\":";
	WriteString(out, model.runtimeAudit.status);
	out << ",\"failure\":";
	WriteString(out, model.runtimeAudit.failure);
	out << "},\"exteriorTriangleCount\":" << exterior.size() << ",\"interiorTriangleCount\":" << interiorCount
	    << ",\"fireTriangleCount\":" << fireCount << ",\"fireSources\":[";
	std::size_t fireTriangleOffset = exterior.size() + interiorCount;
	if (hasFire) {
		bool firstFire = true;
		for (const TownCabinFireSource &fire : model.cabinInterior->fireSources) {
			out << (firstFire ? "" : ",") << "{\"kind\":" << static_cast<unsigned>(fire.kind) << ",\"position\":";
			firstFire = false;
			WriteVector(out, fire.light.position);
			out << ",\"colorLinear\":[" << fire.light.color.red << ',' << fire.light.color.green << ',' << fire.light.color.blue
			    << "],\"radius\":" << fire.light.radius << ",\"intensity\":" << fire.light.intensity
			    << ",\"flickerSeed\":" << fire.flickerSeed << ",\"firstTriangle\":" << fireTriangleOffset
			    << ",\"triangleCount\":" << fire.emissiveTriangles.size() << '}';
			fireTriangleOffset += fire.emissiveTriangles.size();
		}
	}
	out << "],\"triangles\":" << triangles << '}';
	return static_cast<bool>(out);
}

} // namespace town_editor_snapshot_detail

/** Export the current native town's actual architecture and SOL collision.
 * Call after town assets/state have been initialized. This diagnostic writes
 * only generated local files; it never advances simulation or changes a save.
 * Imported textures are PPM for an explicit later PNG conversion. Prototype
 * colors do not reproduce town_view's native-material/light/palette rendering.
 */
inline bool ExportTownEditorSnapshot(const std::filesystem::path &directory)
{
	using namespace town_editor_snapshot_detail;
	if (leveltype != DTYPE_TOWN || !pDungeonCels || MicroTileLen == 0)
		return false;
	const auto &models = GetTownScene();
	if (models.empty() || models.size() != GetTownEditorModelBindings().size())
		return false;
	std::vector<std::string_view> instanceIds;
	for (const TownSceneModel &model : models) {
		const TownEditorModelBinding *binding = FindTownEditorModelBinding(model);
		if (binding == nullptr || !SafeInstanceId(binding->instanceId) || !ValidModel(model)
		    || std::find(instanceIds.begin(), instanceIds.end(), binding->instanceId) != instanceIds.end())
			return false;
		instanceIds.push_back(binding->instanceId);
	}
	for (int z = 0; z < MAXDUNY; ++z) {
		for (int x = 0; x < MAXDUNX; ++x) {
			if (dPiece[x][z] >= MAXTILES)
				return false;
		}
	}
	std::error_code error;
	std::filesystem::create_directories(directory / "textures", error);
	if (error)
		return false;
	std::ostringstream snapshot;
	snapshot.imbue(std::locale::classic());
	snapshot << std::setprecision(std::numeric_limits<float>::max_digits10)
	         << "{\"format\":\"d3d.town-snapshot\",\"schemaVersion\":1,\"edition\":";
	WriteString(snapshot, gbIsHellfire ? "hellfire" : (gbIsSpawn ? "shareware" : "retail"));
	snapshot << ",\"editorMapAudit\":";
	WriteEditorMapAudit(snapshot, GetTownEditorMapAudit());
	snapshot << ",\"gridSize\":[" << MAXDUNX << ',' << MAXDUNY
	         << "],\"coordinates\":\"absolute-x-height-z; one-unit-per-native-cell\","
	            "\"collisionSource\":\"SOLData[dPiece[x][z]] & Solid; excludes transient actors/objects\","
	            "\"prototypeMaterials\":true,\"prototypeMaterialColors\":[[128,112,91],[102,79,47],[119,117,108],[106,76,43],[47,80,92]],"
	            "\"prototypeMaterialOrder\":[\"Wall\",\"Roof\",\"Stone\",\"Timber\",\"Water\"],\"collision\":[";
	bool firstCollision = true;
	for (int z = 0; z < MAXDUNY; ++z) {
		for (int x = 0; x < MAXDUNX; ++x) {
			if (!HasAnyOf(SOLData[dPiece[x][z]], TileProperties::Solid))
				continue;
			snapshot << (firstCollision ? "" : ",") << '[' << x << ',' << z << ']';
			firstCollision = false;
		}
	}
	snapshot << "],\"models\":[";
	bool firstModel = true;
	for (const TownSceneModel &model : models) {
		snapshot << (firstModel ? "" : ",");
		firstModel = false;
		if (!WriteModel(snapshot, directory, model, *FindTownEditorModelBinding(model)))
			return false;
	}
	snapshot << "]}\n";
	if (!snapshot)
		return false;
	std::ofstream out(directory / "town-snapshot.json", std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	const std::string json = snapshot.str();
	out.write(json.data(), static_cast<std::streamsize>(json.size()));
	out.close();
	return static_cast<bool>(out);
}

} // namespace devilution
