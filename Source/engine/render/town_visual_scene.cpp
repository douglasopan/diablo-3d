#include "engine/render/town_visual_scene.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#include "engine/assets.hpp"
#include "engine/render/town_editor_map.hpp"
#include "engine/render/town_model_import.hpp"
#include "engine/render/town_scene_logic.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/trigs.h"
#include "mods/mod_identity.h"
#include "player.h"
#include "quests.h"
#include "utils/ini.hpp"
#include "utils/log.hpp"

namespace devilution {
namespace {

constexpr size_t MaxManifestBytes = 256 * 1024;
constexpr size_t MaxModelBytes = 48 * 1024 * 1024;
constexpr size_t MaxSceneBytes = 256 * 1024 * 1024;
constexpr uint32_t MaxTriangles = 200000;
constexpr uint32_t MaxPatches = 256;
constexpr uint32_t MaxObjects = 256;
constexpr uint32_t MaxSceneTriangles = 1000000;
constexpr std::array<std::string_view, 7> ClassNames { "house", "smithy", "tavern", "cathedral", "well", "cabin", "crypt" };
constexpr std::string_view ModelPrefix = "d3d-scenes/objects/";
constexpr std::string_view StaticPrefix = "d3d-scenes/static/";
TownVisualSceneAudit SceneAudit;
std::vector<TownSceneModel> SourceModels;

bool Fail(std::string &failure, std::string_view value)
{
	failure = value;
	return false;
}

bool Reject(std::string_view value)
{
	SceneAudit.status = "rejected";
	SceneAudit.failure = value;
	LogWarn("Persistent town visual scene rejected: {}", value);
	return false;
}

std::string_view Trim(std::string_view value)
{
	const auto first = value.find_first_not_of(" \t\r\n");
	return first == std::string_view::npos ? std::string_view {} : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

bool ValidText(std::string_view text, size_t limit, bool emptyAllowed = true)
{
	if (text.size() > limit || (!emptyAllowed && text.empty()))
		return false;
	// Reject controls and malformed/noncanonical UTF-8 instead of passing them
	// into INI fields, file names or editor labels.
	for (size_t i = 0; i < text.size();) {
		const auto first = static_cast<unsigned char>(text[i++]);
		if (first < 32 || first == 127)
			return false;
		if (first < 128)
			continue;
		unsigned count;
		uint32_t code, minimum;
		if (first >= 0xC2 && first <= 0xDF) { count = 1; code = first & 0x1F; minimum = 0x80; }
		else if (first >= 0xE0 && first <= 0xEF) { count = 2; code = first & 0x0F; minimum = 0x800; }
		else if (first >= 0xF0 && first <= 0xF4) { count = 3; code = first & 7; minimum = 0x10000; }
		else return false;
		if (count > text.size() - i)
			return false;
		for (unsigned j = 0; j < count; ++j) {
			const auto next = static_cast<unsigned char>(text[i++]);
			if ((next & 0xC0) != 0x80)
				return false;
			code = (code << 6) | (next & 0x3F);
		}
		if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
			return false;
	}
	return true;
}

bool ValidId(std::string_view id)
{
	if (id.empty() || id.size() > 96 || id == "Scene")
		return false;
	return std::all_of(id.begin(), id.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
	});
}

bool ValidAssetPath(std::string_view path, std::string_view prefix)
{
	if (!path.starts_with(prefix) || !path.ends_with(".d3dvis") || path.size() > 256
	    || path.size() <= prefix.size() + 7 || path.find("..") != std::string_view::npos || path.find("//") != std::string_view::npos)
		return false;
	return std::all_of(path.begin(), path.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.';
	});
}

bool ValidPoint(Point point)
{
	return point.x >= 0 && point.x < MAXDUNX && point.y >= 0 && point.y < MAXDUNY;
}

bool ValidRectangle(Point minimum, Point maximum)
{
	return ValidPoint(minimum) && ValidPoint(maximum) && minimum.x <= maximum.x && minimum.y <= maximum.y;
}

bool Bounded(float value, float minimum, float maximum)
{
	return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool ValidVertex(const TownSceneVertex &vertex)
{
	return Bounded(vertex.x, -128, 240) && Bounded(vertex.z, -128, 240)
	    && Bounded(vertex.height, -128, 128) && Bounded(vertex.u, -8192, 8192) && Bounded(vertex.v, -8192, 8192);
}

bool ValidTriangle(const TownSceneTriangle &triangle)
{
	if (static_cast<unsigned>(triangle.material) >= 5 || static_cast<unsigned>(triangle.surfaceRole) >= 3
	    || static_cast<unsigned>(triangle.surfaceDetail) >= static_cast<unsigned>(TownSceneSurfaceDetail::Count)
	    || !ValidPoint(triangle.sourceTile) || !ValidPoint(triangle.pickTile)
	    || !std::all_of(triangle.vertices.begin(), triangle.vertices.end(), ValidVertex)
	    || !Bounded(triangle.normal.x, -2, 2) || !Bounded(triangle.normal.height, -2, 2) || !Bounded(triangle.normal.z, -2, 2))
		return false;
	const auto &a = triangle.vertices[0], &b = triangle.vertices[1], &c = triangle.vertices[2];
	const double nx = static_cast<double>(b.height - a.height) * (c.z - a.z) - static_cast<double>(b.z - a.z) * (c.height - a.height);
	const double ny = static_cast<double>(b.z - a.z) * (c.x - a.x) - static_cast<double>(b.x - a.x) * (c.z - a.z);
	const double nz = static_cast<double>(b.x - a.x) * (c.height - a.height) - static_cast<double>(b.height - a.height) * (c.x - a.x);
	const double normalLength = static_cast<double>(triangle.normal.x) * triangle.normal.x
	    + static_cast<double>(triangle.normal.height) * triangle.normal.height + static_cast<double>(triangle.normal.z) * triangle.normal.z;
	return nx * nx + ny * ny + nz * nz > 1e-20 && normalLength > 1e-12 && normalLength <= 4.01;
}

class Reader {
public:
	explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) { }
	bool u32(uint32_t &value)
	{
		if (remaining() < 4) return false;
		value = 0;
		for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(std::to_integer<uint8_t>(bytes_[offset_++])) << (8 * i);
		return true;
	}
	bool i32(int &value)
	{
		uint32_t bits;
		if (!u32(bits)) return false;
		value = std::bit_cast<int32_t>(bits);
		return true;
	}
	bool f32(float &value)
	{
		uint32_t bits;
		if (!u32(bits)) return false;
		value = std::bit_cast<float>(bits);
		return std::isfinite(value);
	}
	bool boolean(bool &value)
	{
		uint32_t bits;
		if (!u32(bits) || bits > 1) return false;
		value = bits != 0;
		return true;
	}
	bool point(Point &point) { return i32(point.x) && i32(point.y); }
	bool vertex(TownSceneVertex &v) { return f32(v.x) && f32(v.height) && f32(v.z) && f32(v.u) && f32(v.v); }
	bool text(std::string &value, size_t limit)
	{
		uint32_t size;
		if (!u32(size) || size > limit || size > remaining()) return false;
		value.assign(reinterpret_cast<const char *>(bytes_.data() + offset_), size);
		offset_ += size;
		return ValidText(value, limit);
	}
	bool skip(size_t size) { if (size > remaining()) return false; offset_ += size; return true; }
	size_t remaining() const { return bytes_.size() - offset_; }
	std::span<const std::byte> rest() const { return bytes_.subspan(offset_); }
private:
	std::span<const std::byte> bytes_;
	size_t offset_ = 0;
};

class Writer {
public:
	explicit Writer(std::vector<std::byte> &bytes) : bytes_(bytes) { }
	void u32(uint32_t value) { for (unsigned i = 0; i < 4; ++i) bytes_.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF)); }
	void i32(int value) { u32(std::bit_cast<uint32_t>(static_cast<int32_t>(value))); }
	void f32(float value) { u32(std::bit_cast<uint32_t>(value)); }
	void boolean(bool value) { u32(value ? 1 : 0); }
	void point(Point value) { i32(value.x); i32(value.y); }
	void vertex(const TownSceneVertex &v) { f32(v.x); f32(v.height); f32(v.z); f32(v.u); f32(v.v); }
	void text(std::string_view value) { u32(static_cast<uint32_t>(value.size())); for (unsigned char c : value) bytes_.push_back(static_cast<std::byte>(c)); }
private:
	std::vector<std::byte> &bytes_;
};

bool ReadTriangle(Reader &reader, TownSceneTriangle &triangle)
{
	uint32_t material, role, detail;
	for (auto &vertex : triangle.vertices) if (!reader.vertex(vertex)) return false;
	if (!reader.u32(material) || !reader.point(triangle.sourceTile) || !reader.point(triangle.pickTile)
	    || !reader.boolean(triangle.nativeProjection) || !reader.f32(triangle.normal.x) || !reader.f32(triangle.normal.height)
	    || !reader.f32(triangle.normal.z) || !reader.u32(role) || !reader.u32(detail) || material >= 5 || role >= 3
	    || detail >= static_cast<uint32_t>(TownSceneSurfaceDetail::Count)) return false;
	triangle.material = static_cast<TownSceneMaterial>(material);
	triangle.surfaceRole = static_cast<TownSceneSurfaceRole>(role);
	triangle.surfaceDetail = static_cast<TownSceneSurfaceDetail>(detail);
	return ValidTriangle(triangle);
}

void WriteTriangle(Writer &writer, const TownSceneTriangle &triangle)
{
	for (const auto &vertex : triangle.vertices) writer.vertex(vertex);
	writer.u32(static_cast<uint32_t>(triangle.material)); writer.point(triangle.sourceTile); writer.point(triangle.pickTile);
	writer.boolean(triangle.nativeProjection); writer.f32(triangle.normal.x); writer.f32(triangle.normal.height); writer.f32(triangle.normal.z);
	writer.u32(static_cast<uint32_t>(triangle.surfaceRole)); writer.u32(static_cast<uint32_t>(triangle.surfaceDetail));
}

bool ValidPatch(const TownSceneMaterialPatch &patch)
{
	if (static_cast<unsigned>(patch.material) >= 5 || static_cast<unsigned>(patch.surfaceDetail) >= static_cast<unsigned>(TownSceneSurfaceDetail::Count)
	    || !std::all_of(patch.referencePlane.begin(), patch.referencePlane.end(), ValidVertex)) return false;
	for (size_t i = 0; i < 2; ++i)
		if (!Bounded(patch.uvMin[i], -8192, 8192) || !Bounded(patch.uvMax[i], -8192, 8192) || patch.uvMin[i] >= patch.uvMax[i]
		    || !Bounded(patch.repeatWorldSize[i], 0.000001F, 8192)) return false;
	const auto &a = patch.referencePlane[0], &b = patch.referencePlane[1], &c = patch.referencePlane[2];
	if (std::abs(static_cast<double>(b.u - a.u) * (c.v - a.v) - static_cast<double>(b.v - a.v) * (c.u - a.u)) < 1e-12)
		return false;
	return patch.sourceMin.x >= -16384 && patch.sourceMin.y >= -16384 && patch.sourceMax.x <= 16384 && patch.sourceMax.y <= 16384
	    && patch.sourceMin.x < patch.sourceMax.x && patch.sourceMin.y < patch.sourceMax.y;
}

bool ValidLightVector(const TownLightVector &v)
{
	return Bounded(v.x, -128, 240) && Bounded(v.height, -128, 128) && Bounded(v.z, -128, 240);
}

bool ReadLightVector(Reader &reader, TownLightVector &v)
{
	return reader.f32(v.x) && reader.f32(v.height) && reader.f32(v.z) && ValidLightVector(v);
}

void WriteLightVector(Writer &writer, const TownLightVector &v)
{
	writer.f32(v.x); writer.f32(v.height); writer.f32(v.z);
}

bool ReadColor(Reader &reader, TownLightColor &color)
{
	return reader.f32(color.red) && reader.f32(color.green) && reader.f32(color.blue)
	    && Bounded(color.red, 0, 64) && Bounded(color.green, 0, 64) && Bounded(color.blue, 0, 64);
}

void WriteColor(Writer &writer, const TownLightColor &color)
{
	writer.f32(color.red); writer.f32(color.green); writer.f32(color.blue);
}

bool ReadAperture(Reader &reader, TownLightAperture &a)
{
	uint32_t plane, sides;
	if (!reader.u32(plane) || plane > 2 || !reader.f32(a.coordinate) || !reader.f32(a.minU) || !reader.f32(a.maxU)
	    || !reader.f32(a.minV) || !reader.f32(a.maxV) || !reader.u32(sides)
	    || !Bounded(a.coordinate, -128, 240) || !Bounded(a.minU, -128, 240) || !Bounded(a.maxU, -128, 240)
	    || !Bounded(a.minV, -128, 240) || !Bounded(a.maxV, -128, 240) || a.minU >= a.maxU || a.minV >= a.maxV
	    || (sides != 0 && (sides < 3 || sides > 32))) return false;
	a.plane = static_cast<TownLightPlane>(plane); a.polygonSides = sides;
	return true;
}

void WriteAperture(Writer &writer, const TownLightAperture &a)
{
	writer.u32(static_cast<uint32_t>(a.plane)); writer.f32(a.coordinate); writer.f32(a.minU); writer.f32(a.maxU);
	writer.f32(a.minV); writer.f32(a.maxV); writer.u32(a.polygonSides);
}

bool ReadAdjunct(Reader &reader, std::shared_ptr<const TownCabinInterior> &output, std::string &failure)
{
	uint32_t exterior, interior, openings, apertures, occluders, fires;
	auto candidate = std::make_shared<TownCabinInterior>();
	if (!reader.u32(exterior) || !reader.u32(interior) || !reader.u32(openings) || !reader.u32(apertures) || !reader.u32(occluders) || !reader.u32(fires)
	    || !reader.u32(candidate->clippedSourceTriangles) || exterior == 0 || exterior > MaxTriangles || interior > MaxTriangles
	    || openings > 256 || apertures > 256 || occluders == 0 || occluders > TownMaxLightOccluders || fires > TownMaxPointLights || candidate->clippedSourceTriangles > MaxTriangles
	    || static_cast<uint64_t>(exterior + interior) * 104 + static_cast<uint64_t>(openings) * 52 + static_cast<uint64_t>(apertures) * 28
	            + static_cast<uint64_t>(occluders) * 32 + static_cast<uint64_t>(fires) * 44 + 72 > reader.remaining())
		return Fail(failure, "adjunct-counts");
	if (!ReadLightVector(reader, candidate->roomMinimum) || !ReadLightVector(reader, candidate->roomMaximum)
	    || candidate->roomMinimum.x >= candidate->roomMaximum.x || candidate->roomMinimum.height >= candidate->roomMaximum.height || candidate->roomMinimum.z >= candidate->roomMaximum.z
	    || !ReadColor(reader, candidate->fireColor) || !ReadColor(reader, candidate->fireEmissionLinear) || !ReadColor(reader, candidate->fireCoreEmissionLinear) || !ReadColor(reader, candidate->fireTipEmissionLinear)
	    || candidate->fireColor.red < 0.000001F)
		return Fail(failure, "adjunct-room-color");
	candidate->exteriorTriangles.resize(exterior); candidate->interiorTriangles.resize(interior);
	for (auto *triangles : { &candidate->exteriorTriangles, &candidate->interiorTriangles })
		for (auto &triangle : *triangles) if (!ReadTriangle(reader, triangle)) return Fail(failure, "adjunct-triangle");
	candidate->openings.resize(openings);
	for (auto &opening : candidate->openings) {
		uint32_t kind, material;
		if (!reader.u32(kind) || kind > 2 || !ReadAperture(reader, opening.aperture) || !reader.f32(opening.outerCoordinate)
		    || !reader.f32(opening.clipMinimum) || !reader.f32(opening.clipMaximum) || !reader.boolean(opening.woodenMuntins)
		    || !reader.u32(material) || material >= 5 || !Bounded(opening.outerCoordinate, -128, 240)
		    || !Bounded(opening.clipMinimum, -128, 240) || !Bounded(opening.clipMaximum, -128, 240) || opening.clipMinimum >= opening.clipMaximum)
			return Fail(failure, "adjunct-opening");
		opening.kind = static_cast<TownCabinOpeningKind>(kind); opening.liningMaterial = static_cast<TownSceneMaterial>(material);
	}
	candidate->apertures.resize(apertures);
	for (auto &aperture : candidate->apertures) if (!ReadAperture(reader, aperture)) return Fail(failure, "adjunct-aperture");
	candidate->lightOccluders.resize(occluders);
	for (auto &occluder : candidate->lightOccluders) {
		uint32_t first, count;
		if (!ReadLightVector(reader, occluder.minimum) || !ReadLightVector(reader, occluder.maximum) || !reader.u32(first) || !reader.u32(count)
		    || first > apertures || count > apertures - first || count > TownMaxLightApertures || occluder.minimum.x >= occluder.maximum.x
		    || occluder.minimum.height >= occluder.maximum.height || occluder.minimum.z >= occluder.maximum.z)
			return Fail(failure, "adjunct-occluder-span");
		occluder.apertures = std::span<const TownLightAperture>(candidate->apertures).subspan(first, count);
	}
	const auto &shell = candidate->lightOccluders.front();
	if (shell.minimum.x != candidate->roomMinimum.x || shell.minimum.height != candidate->roomMinimum.height || shell.minimum.z != candidate->roomMinimum.z
	    || shell.maximum.x != candidate->roomMaximum.x || shell.maximum.height != candidate->roomMaximum.height || shell.maximum.z != candidate->roomMaximum.z)
		return Fail(failure, "adjunct-first-occluder-room");
	candidate->fireSources.resize(fires);
	size_t adjunctTriangles = static_cast<size_t>(exterior) + interior;
	for (auto &fire : candidate->fireSources) {
		uint32_t kind, count;
		if (!reader.u32(kind) || kind > static_cast<uint32_t>(TownCabinFireKind::EditorPoint) || !ReadLightVector(reader, fire.light.position) || !ReadColor(reader, fire.light.color)
		    || !reader.f32(fire.light.radius) || !reader.f32(fire.light.intensity) || !reader.u32(fire.flickerSeed) || !reader.u32(count)
		    || count > MaxTriangles || (kind == static_cast<uint32_t>(TownCabinFireKind::EditorPoint) && count != 0)
		    || static_cast<uint64_t>(count) * 104 > reader.remaining() || !Bounded(fire.light.radius, 0.000001F, 128)
		    || !Bounded(fire.light.intensity, 0, TownMaxPointLightIntensity)
		    || !Bounded(fire.light.color.red, 0, 16) || !Bounded(fire.light.color.green, 0, 16) || !Bounded(fire.light.color.blue, 0, 16))
			return Fail(failure, "adjunct-fire");
		adjunctTriangles += count;
		if (adjunctTriangles > MaxTriangles) return Fail(failure, "adjunct-triangle-budget");
		fire.kind = static_cast<TownCabinFireKind>(kind); fire.emissiveTriangles.resize(count);
		for (auto &triangle : fire.emissiveTriangles) if (!ReadTriangle(reader, triangle)) return Fail(failure, "adjunct-fire-triangle");
	}
	if (reader.remaining() != 0) return Fail(failure, "adjunct-trailing-bytes");
	output = std::move(candidate);
	return true;
}

size_t ModelTriangleCount(const TownSceneModel &model)
{
	size_t count = model.triangles.size();
	if (model.cabinInterior) {
		count += model.cabinInterior->exteriorTriangles.size() + model.cabinInterior->interiorTriangles.size();
		for (const auto &fire : model.cabinInterior->fireSources) count += fire.emissiveTriangles.size();
	}
	return count;
}

bool WriteAdjunct(Writer &writer, const TownCabinInterior &adjunct, std::string &failure)
{
	if (adjunct.exteriorTriangles.size() > MaxTriangles || adjunct.interiorTriangles.size() > MaxTriangles || adjunct.openings.size() > 256
	    || adjunct.apertures.size() > 256 || adjunct.lightOccluders.size() > TownMaxLightOccluders || adjunct.fireSources.size() > TownMaxPointLights)
		return Fail(failure, "adjunct-counts");
	writer.u32(static_cast<uint32_t>(adjunct.exteriorTriangles.size())); writer.u32(static_cast<uint32_t>(adjunct.interiorTriangles.size()));
	writer.u32(static_cast<uint32_t>(adjunct.openings.size())); writer.u32(static_cast<uint32_t>(adjunct.apertures.size()));
	writer.u32(static_cast<uint32_t>(adjunct.lightOccluders.size())); writer.u32(static_cast<uint32_t>(adjunct.fireSources.size())); writer.u32(adjunct.clippedSourceTriangles);
	WriteLightVector(writer, adjunct.roomMinimum); WriteLightVector(writer, adjunct.roomMaximum);
	WriteColor(writer, adjunct.fireColor); WriteColor(writer, adjunct.fireEmissionLinear); WriteColor(writer, adjunct.fireCoreEmissionLinear); WriteColor(writer, adjunct.fireTipEmissionLinear);
	for (const auto *triangles : { &adjunct.exteriorTriangles, &adjunct.interiorTriangles }) for (const auto &triangle : *triangles) WriteTriangle(writer, triangle);
	for (const auto &opening : adjunct.openings) {
		writer.u32(static_cast<uint32_t>(opening.kind)); WriteAperture(writer, opening.aperture);
		writer.f32(opening.outerCoordinate); writer.f32(opening.clipMinimum); writer.f32(opening.clipMaximum); writer.boolean(opening.woodenMuntins); writer.u32(static_cast<uint32_t>(opening.liningMaterial));
	}
	for (const auto &aperture : adjunct.apertures) WriteAperture(writer, aperture);
	for (const auto &occluder : adjunct.lightOccluders) {
		uint32_t first = 0;
		bool found = occluder.apertures.empty();
		for (size_t i = 0; i < adjunct.apertures.size() && !found; ++i)
			if (occluder.apertures.data() == &adjunct.apertures[i] && occluder.apertures.size() <= adjunct.apertures.size() - i) { first = static_cast<uint32_t>(i); found = true; }
		if (!found) return Fail(failure, "adjunct-foreign-aperture-span");
		WriteLightVector(writer, occluder.minimum); WriteLightVector(writer, occluder.maximum); writer.u32(first); writer.u32(static_cast<uint32_t>(occluder.apertures.size()));
	}
	for (const auto &fire : adjunct.fireSources) {
		if (fire.emissiveTriangles.size() > MaxTriangles
		    || static_cast<uint32_t>(fire.kind) > static_cast<uint32_t>(TownCabinFireKind::EditorPoint)
		    || (fire.kind == TownCabinFireKind::EditorPoint && !fire.emissiveTriangles.empty())
		    || !Bounded(fire.light.intensity, 0, TownMaxPointLightIntensity)
		    || !Bounded(fire.light.color.red, 0, 16) || !Bounded(fire.light.color.green, 0, 16) || !Bounded(fire.light.color.blue, 0, 16))
			return Fail(failure, "adjunct-fire-count");
		writer.u32(static_cast<uint32_t>(fire.kind)); WriteLightVector(writer, fire.light.position); WriteColor(writer, fire.light.color);
		writer.f32(fire.light.radius); writer.f32(fire.light.intensity); writer.u32(fire.flickerSeed); writer.u32(static_cast<uint32_t>(fire.emissiveTriangles.size()));
		for (const auto &triangle : fire.emissiveTriangles) WriteTriangle(writer, triangle);
	}
	return true;
}

bool DecodeModel(TownSceneModel &model, std::span<const std::byte> data, std::string &failure)
{
	constexpr std::string_view Magic("D3DVIS1\0", 8);
	if (data.size() < 168 || data.size() > MaxModelBytes || std::string_view(reinterpret_cast<const char *>(data.data()), 8) != Magic)
		return Fail(failure, "model-size-or-magic");
	Reader reader(data); reader.skip(8);
	uint32_t version, kind, flags, triangleCount, patchCount, width, height, adjunct, fringeMin, fringeMax;
	TownSceneModel candidate {};
	if (!reader.u32(version) || version != 1 || !reader.u32(kind) || kind >= ClassNames.size() || !reader.u32(flags) || flags > 3)
		return Fail(failure, "model-version-kind-flags");
	candidate.kind = static_cast<TownSceneKind>(kind);
	candidate.externalModel = (flags & 1) != 0;
	auto &bounds = candidate.physicalBounds;
	auto &art = candidate.nativeArtwork;
	if (!reader.point(candidate.minTile) || !reader.point(candidate.maxTile)
	    || !reader.f32(bounds.minX) || !reader.f32(bounds.minZ) || !reader.f32(bounds.maxX) || !reader.f32(bounds.maxZ)
	    || !reader.f32(bounds.wallHeight) || !reader.boolean(bounds.closed)
	    || !reader.point(art.referenceTile) || !reader.point(art.pixelOrigin) || !reader.point(art.pixelSize) || !reader.boolean(art.enabled)
	    || !reader.point(art.minTile) || !reader.point(art.maxTile) || !reader.u32(fringeMin) || !reader.u32(fringeMax)
	    || !reader.u32(triangleCount) || !reader.u32(patchCount) || !reader.u32(width) || !reader.u32(height) || !reader.u32(adjunct))
		return Fail(failure, "model-header");
	if (!ValidRectangle(candidate.minTile, candidate.maxTile) || !ValidPoint(art.referenceTile)
	    || !ValidRectangle(art.minTile, art.maxTile) || !Bounded(bounds.minX, -128, 240) || !Bounded(bounds.maxX, -128, 240)
	    || !Bounded(bounds.minZ, -128, 240) || !Bounded(bounds.maxZ, -128, 240) || bounds.minX > bounds.maxX || bounds.minZ > bounds.maxZ
	    || !Bounded(bounds.wallHeight, 0, 128) || std::abs(static_cast<int64_t>(art.pixelOrigin.x)) > 16384 || std::abs(static_cast<int64_t>(art.pixelOrigin.y)) > 16384
	    || art.pixelSize.x < 0 || art.pixelSize.x > 16384 || art.pixelSize.y < 0 || art.pixelSize.y > 16384
	    || (art.enabled && (art.pixelSize.x == 0 || art.pixelSize.y == 0)) || fringeMin > 65535 || fringeMax > 65535 || fringeMin > fringeMax)
		return Fail(failure, "model-bounds");
	art.fringeMinPiece = static_cast<uint16_t>(fringeMin); art.fringeMaxPiece = static_cast<uint16_t>(fringeMax);
	// ComposeNativeArtwork allocates a native bitmap from this envelope and
	// BuildTownSceneProjectionOwners accepts at most these same dimensions.
	// Reject incompatible source envelopes before any renderer allocation.
	const Point artworkMinimum = art.enabled ? art.minTile : candidate.minTile;
	const Point artworkMaximum = art.enabled ? art.maxTile : candidate.maxTile;
	const uint64_t span = static_cast<uint64_t>(artworkMaximum.x - artworkMinimum.x + artworkMaximum.y - artworkMinimum.y);
	const uint64_t composedWidth = 32 * (span + 2) + 16, composedHeight = 16 * span + 272;
	if (composedWidth > 4096 || composedHeight > 4096 || composedWidth * composedHeight > 8'000'000)
		return Fail(failure, "model-artwork-budget");
	if (adjunct > 1) return Fail(failure, "model-adjunct-version");
	if (triangleCount == 0 || triangleCount > MaxTriangles || patchCount > MaxPatches
	    || (((flags & 2) == 0) ? (width != 0 || height != 0) : (width == 0 || width > 2048 || height == 0 || height > 2048)))
		return Fail(failure, "model-counts");
	auto &audit = candidate.runtimeAudit;
	if (!reader.text(audit.assetPath, 1024) || !reader.text(audit.sourceKind, 128) || !reader.text(audit.sha256, 64)
	    || !reader.text(audit.expectedSha256, 64) || !reader.text(audit.revision, 512) || !reader.text(audit.status, 128) || !reader.text(audit.failure, 1024))
		return Fail(failure, "model-provenance");
	std::array<uint8_t, 32> hash;
	if ((!audit.sha256.empty() && !HexToModHash(audit.sha256, hash)) || (!audit.expectedSha256.empty() && !HexToModHash(audit.expectedSha256, hash)))
		return Fail(failure, "model-provenance-hash");
	const uint64_t expectedSize = static_cast<uint64_t>(triangleCount) * 104 + static_cast<uint64_t>(patchCount) * 112 + static_cast<uint64_t>(width) * height * 3;
	if (adjunct == 0 ? reader.remaining() != expectedSize : reader.remaining() < expectedSize + 100) return Fail(failure, "model-byte-count");
	candidate.triangles.resize(triangleCount);
	for (auto &triangle : candidate.triangles)
		if (!ReadTriangle(reader, triangle)) return Fail(failure, "model-triangle");
	candidate.materialPatches.resize(patchCount);
	for (auto &patch : candidate.materialPatches) {
		uint32_t detail, material;
		if (!reader.u32(detail) || !reader.u32(material) || detail >= static_cast<uint32_t>(TownSceneSurfaceDetail::Count) || material >= 5)
			return Fail(failure, "model-patch-enum");
		patch.surfaceDetail = static_cast<TownSceneSurfaceDetail>(detail); patch.material = static_cast<TownSceneMaterial>(material);
		for (auto &vertex : patch.referencePlane) if (!reader.vertex(vertex)) return Fail(failure, "model-patch-plane");
		for (auto *values : { &patch.uvMin, &patch.uvMax, &patch.repeatWorldSize })
			for (float &value : *values) if (!reader.f32(value)) return Fail(failure, "model-patch-uv");
		if (!reader.boolean(patch.repeat) || !reader.point(patch.sourceMin) || !reader.point(patch.sourceMax) || !ValidPatch(patch))
			return Fail(failure, "model-patch");
	}
	if (flags & 2) {
		auto texture = std::make_shared<TownImportedTexture>(); texture->width = width; texture->height = height;
		texture->rgb.resize(static_cast<size_t>(width) * height * 3);
		const size_t rgbBytes = texture->rgb.size();
		std::transform(reader.rest().begin(), reader.rest().begin() + rgbBytes, texture->rgb.begin(), [](std::byte value) { return std::to_integer<uint8_t>(value); });
		reader.skip(rgbBytes);
		candidate.importedTexture = std::move(texture);
	}
	if (adjunct == 1 && !ReadAdjunct(reader, candidate.cabinInterior, failure)) return false;
	if (ModelTriangleCount(candidate) > MaxTriangles) return Fail(failure, "model-total-triangle-budget");
	model = std::move(candidate);
	failure.clear();
	return true;
}

bool ParseFloat(std::string_view value, float &result)
{
	value = Trim(value);
	const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
	return !value.empty() && parsed.ec == std::errc() && parsed.ptr == value.data() + value.size() && std::isfinite(result);
}

bool ParseVector(std::string_view value, TownLightVector &result)
{
	const auto a = value.find(','), b = a == std::string_view::npos ? a : value.find(',', a + 1);
	return a != std::string_view::npos && b != std::string_view::npos && value.find(',', b + 1) == std::string_view::npos
	    && ParseFloat(value.substr(0, a), result.x) && ParseFloat(value.substr(a + 1, b - a - 1), result.height) && ParseFloat(value.substr(b + 1), result.z);
}

bool ParsePoint(std::string_view value, Point &result)
{
	const auto comma = value.find(',');
	if (comma == std::string_view::npos || value.find(',', comma + 1) != std::string_view::npos) return false;
	const auto x = Trim(value.substr(0, comma)), y = Trim(value.substr(comma + 1));
	const auto a = std::from_chars(x.data(), x.data() + x.size(), result.x), b = std::from_chars(y.data(), y.data() + y.size(), result.y);
	return !x.empty() && !y.empty() && a.ec == std::errc() && b.ec == std::errc() && a.ptr == x.data() + x.size() && b.ptr == y.data() + y.size() && ValidPoint(result);
}

bool KnownKeys(const Ini &ini, std::string_view section, std::span<const std::string_view> keys, std::string_view repeated = {})
{
	for (const auto &key : ini.getKeys(section))
		if (std::find(keys.begin(), keys.end(), key) == keys.end() || (key != repeated && ini.get(section, key).size() != 1)) return false;
	return true;
}

bool ValidTransform(const TownVisualSceneTransform &t)
{
	return Bounded(t.pivot.x, -128, 240) && Bounded(t.pivot.height, -128, 128) && Bounded(t.pivot.z, -128, 240)
	    && Bounded(t.translation.x, -112, 112) && Bounded(t.translation.height, -64, 64) && Bounded(t.translation.z, -112, 112)
	    && Bounded(t.yawDegrees, -36000, 36000) && Bounded(t.uniformScale, 0.01F, 100);
}

bool ApplyTransform(TownSceneModel &model, const TownVisualSceneTransform &t)
{
	// Identity is bit-exact; exported baseline vertices/normals are not rebaked.
	if (t.translation.x == 0 && t.translation.height == 0 && t.translation.z == 0 && t.yawDegrees == 0 && t.uniformScale == 1) return true;
	// Occluder shells/apertures are axis aligned by the existing lighting API.
	// Do not turn a rotated opening into a bounding-box light leak.
	if (model.cabinInterior && t.yawDegrees != 0) return false;
	constexpr float Pi = 3.14159265358979323846F;
	const float yaw = t.yawDegrees * Pi / 180, c = std::cos(yaw), s = std::sin(yaw);
	auto position = [&](float &x, float &height, float &z) {
		const float dx = (x - t.pivot.x) * t.uniformScale, dz = (z - t.pivot.z) * t.uniformScale;
		x = t.pivot.x + t.translation.x + c * dx + s * dz;
		z = t.pivot.z + t.translation.z - s * dx + c * dz;
		height = t.pivot.height + t.translation.height + (height - t.pivot.height) * t.uniformScale;
	};
	for (auto &triangle : model.triangles) {
		for (auto &vertex : triangle.vertices) position(vertex.x, vertex.height, vertex.z);
		const float nx = triangle.normal.x, nz = triangle.normal.z;
		triangle.normal.x = c * nx + s * nz; triangle.normal.z = -s * nx + c * nz;
		if (!ValidTriangle(triangle)) return false;
	}
	// Material reference planes rectify the original source artwork. They
	// stay in source space, just like sourceTile/nativeArtwork, while UVs and
	// the visible triangles move together.
	if (model.cabinInterior) {
		auto adjunct = std::make_shared<TownCabinInterior>(*model.cabinInterior);
		for (auto *triangles : { &adjunct->exteriorTriangles, &adjunct->interiorTriangles })
			for (auto &triangle : *triangles) {
				for (auto &vertex : triangle.vertices) position(vertex.x, vertex.height, vertex.z);
				if (!ValidTriangle(triangle)) return false;
			}
		auto vector = [&](TownLightVector &v) { position(v.x, v.height, v.z); return ValidLightVector(v); };
		auto coordinate = [&](float value, unsigned axis) {
			const float pivot = axis == 0 ? t.pivot.x : axis == 1 ? t.pivot.height : t.pivot.z;
			const float translation = axis == 0 ? t.translation.x : axis == 1 ? t.translation.height : t.translation.z;
			return pivot + translation + (value - pivot) * t.uniformScale;
		};
		auto aperture = [&](TownLightAperture &a) {
			const unsigned axis = static_cast<unsigned>(a.plane);
			const unsigned uAxis = axis == 0 ? 2 : 0, vAxis = axis == 1 ? 2 : 1;
			a.coordinate = coordinate(a.coordinate, axis); a.minU = coordinate(a.minU, uAxis); a.maxU = coordinate(a.maxU, uAxis);
			a.minV = coordinate(a.minV, vAxis); a.maxV = coordinate(a.maxV, vAxis);
			return Bounded(a.coordinate, -128, 240) && Bounded(a.minU, -128, 240) && Bounded(a.maxU, -128, 240) && Bounded(a.minV, -128, 240) && Bounded(a.maxV, -128, 240);
		};
		if (!vector(adjunct->roomMinimum) || !vector(adjunct->roomMaximum)) return false;
		for (auto &a : adjunct->apertures) if (!aperture(a)) return false;
		for (auto &opening : adjunct->openings) {
			const unsigned axis = static_cast<unsigned>(opening.aperture.plane);
			opening.outerCoordinate = coordinate(opening.outerCoordinate, axis); opening.clipMinimum = coordinate(opening.clipMinimum, axis); opening.clipMaximum = coordinate(opening.clipMaximum, axis);
			if (!aperture(opening.aperture)) return false;
		}
		for (size_t i = 0; i < adjunct->lightOccluders.size(); ++i) {
			auto &occluder = adjunct->lightOccluders[i];
			const auto old = model.cabinInterior->lightOccluders[i].apertures;
			if (!vector(occluder.minimum) || !vector(occluder.maximum)) return false;
			if (old.empty()) occluder.apertures = {};
			else {
				const size_t first = static_cast<size_t>(old.data() - model.cabinInterior->apertures.data());
				occluder.apertures = std::span<const TownLightAperture>(adjunct->apertures).subspan(first, old.size());
			}
		}
		for (auto &fire : adjunct->fireSources) {
			if (!vector(fire.light.position)) return false;
			fire.light.radius *= t.uniformScale;
			if (!Bounded(fire.light.radius, 0.000001F, 128)) return false;
			for (auto &triangle : fire.emissiveTriangles) {
				for (auto &vertex : triangle.vertices) position(vertex.x, vertex.height, vertex.z);
				if (!ValidTriangle(triangle)) return false;
			}
		}
		model.cabinInterior = std::move(adjunct);
	}
	auto &b = model.physicalBounds;
	float minX = std::numeric_limits<float>::max(), minZ = minX, maxX = -minX, maxZ = -minX;
	for (const float x : { b.minX, b.maxX }) for (const float z : { b.minZ, b.maxZ }) {
		float nx = x, nz = z, h = 0; position(nx, h, nz);
		minX = std::min(minX, nx); maxX = std::max(maxX, nx); minZ = std::min(minZ, nz); maxZ = std::max(maxZ, nz);
	}
	b.minX = minX; b.maxX = maxX; b.minZ = minZ; b.maxZ = maxZ;
	b.wallHeight = t.pivot.height + t.translation.height + (b.wallHeight - t.pivot.height) * t.uniformScale;
	return Bounded(minX, -128, 240) && Bounded(maxX, -128, 240) && Bounded(minZ, -128, 240) && Bounded(maxZ, -128, 240) && Bounded(b.wallHeight, 0, 128);
}

bool ResolveRuntimeAsset(std::string_view path, std::vector<std::byte> &data, std::string &sourceKind, std::string &failure)
{
	AssetRef ref = FindAsset(path);
	if (!ref.ok()) return Fail(failure, "asset-missing");
#ifdef UNPACKED_MPQS
	sourceKind = "unpacked-file";
#else
	sourceKind = ref.archive != nullptr ? "archive" : (ref.isOverridden ? "override" : "loose-file");
#endif
	const size_t size = ref.size();
	if (size == 0 || size > MaxModelBytes) return Fail(failure, "asset-size");
	AssetHandle handle = OpenAsset(std::move(ref));
	if (!handle.ok()) return Fail(failure, "asset-open");
	data.resize(size);
	return handle.read(data.data(), data.size()) || Fail(failure, "asset-read");
}

bool ParseManifest(std::vector<TownSceneModel> &models, std::span<const std::byte> data,
    const TownVisualSceneAssetResolver &resolver, const TownVisualSceneStaticCommit &staticCommit, bool requireStatic = false, bool logicPreflight = false)
{
	if (data.empty() || data.size() > MaxManifestBytes) return Reject("manifest-size");
	SceneAudit.sha256 = ModHashToHex(ComputeBytesSha256(data));
	std::string_view text(reinterpret_cast<const char *>(data.data()), data.size());
	if (text.find('\0') != std::string_view::npos) return Reject("manifest-nul");
	if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
	auto parsed = Ini::parse(text);
	if (!parsed) return Reject("manifest-ini");
	const Ini &ini = *parsed;
	constexpr std::array<std::string_view, 10> SceneKeys { "format", "schemaVersion", "edition", "revision", "objectCount", "object", "staticData", "staticSha256", "worldPatch", "worldPatchSha256" };
	if (!KnownKeys(ini, "Scene", SceneKeys, "object") || !ini.getKeys("").empty()) return Reject("manifest-scene-fields");
	for (auto key : { "format", "schemaVersion", "edition", "revision", "objectCount" })
		if (ini.get("Scene", key).size() != 1 || ini.getString("Scene", key).empty()) return Reject("manifest-required-field");
	if (ini.getString("Scene", "format") != "d3d.town-visual-scene" || ini.getString("Scene", "schemaVersion") != "1") return Reject("manifest-schema");
	if (gbIsHellfire || ini.getString("Scene", "edition") != (gbIsSpawn ? "shareware" : "retail")) return Reject("manifest-edition");
	TownVisualSceneAudit candidateAudit;
	candidateAudit.sourceKind = SceneAudit.sourceKind; candidateAudit.sha256 = SceneAudit.sha256;
	candidateAudit.revision = ini.getString("Scene", "revision");
	if (!ValidText(candidateAudit.revision, 128, false)) return Reject("manifest-revision");
	candidateAudit.staticData = ini.getString("Scene", "staticData"); candidateAudit.staticSha256 = ini.getString("Scene", "staticSha256");
	if (requireStatic && candidateAudit.staticData.empty()) return Reject("manifest-complete-static-required");
	std::array<uint8_t, 32> expectedHash;
	if (candidateAudit.staticData.empty() != candidateAudit.staticSha256.empty()
	    || (!candidateAudit.staticData.empty() && (!ValidAssetPath(candidateAudit.staticData, StaticPrefix) || !HexToModHash(candidateAudit.staticSha256, expectedHash))))
		return Reject("manifest-static-reference");
	const auto worldPatch = ini.getString("Scene", "worldPatch"), worldHash = ini.getString("Scene", "worldPatchSha256");
	if (worldPatch.empty() != worldHash.empty() || (!worldPatch.empty() && (gbIsMultiplayer || worldPatch != TownSceneLogicPatchPath || !HexToModHash(worldHash, expectedHash))))
		return Reject("manifest-world-patch-reference");
	if (!logicPreflight && !worldPatch.empty()) {
		const auto &logic = GetTownSceneLogicAudit();
		if (logic.status != "applied" || logic.path != worldPatch || logic.sha256 != worldHash || logic.targetStaticSha256 != candidateAudit.staticSha256)
			return Reject("manifest-world-patch-not-applied");
	}
	if (!logicPreflight && worldPatch.empty() && GetTownSceneLogicAudit().status == "applied")
		return Reject("manifest-world-patch-required-for-native-state");
	const auto requests = ini.get("Scene", "object");
	uint32_t objectCount = 0; const auto countText = ini.getString("Scene", "objectCount");
	const auto count = std::from_chars(countText.data(), countText.data() + countText.size(), objectCount);
	if (count.ec != std::errc() || count.ptr != countText.data() + countText.size() || objectCount == 0 || objectCount > MaxObjects || requests.size() != objectCount)
		return Reject("manifest-object-count");
	std::set<std::string_view> ids, sections;
	for (const auto &request : requests) if (!ValidId(request.value) || !ids.insert(request.value).second) return Reject("manifest-object-id");
	for (size_t begin = 0; begin < text.size();) {
		const auto end = text.find('\n', begin);
		const auto line = Trim(text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin));
		if (line.starts_with('[')) {
			const auto close = line.find(']');
			if (close == std::string_view::npos) return Reject("manifest-section");
			const auto section = Trim(line.substr(1, close - 1));
			if (!sections.insert(section).second || (section != "Scene" && !ids.contains(section))) return Reject("manifest-duplicate-or-unlisted-section");
		}
		if (end == std::string_view::npos) break;
		begin = end + 1;
	}
	if (sections.size() != ids.size() + 1 || !sections.contains("Scene")) return Reject("manifest-missing-section");
	constexpr std::array<std::string_view, 11> ObjectKeys { "name", "class", "model", "sha256", "pivot", "translation", "yawDegrees", "uniformScale", "nativeMin", "nativeMax", "eventBinding" };
	std::vector<TownSceneModel> candidates;
	std::vector<TownSceneModel> candidateSources;
	candidates.reserve(objectCount); candidateSources.reserve(objectCount); candidateAudit.objects.reserve(objectCount);
	size_t sceneBytes = 0, sceneTriangles = 0;
	for (const auto &request : requests) {
		const std::string_view id = request.value;
		if (!KnownKeys(ini, id, ObjectKeys)) return Reject("manifest-object-fields");
		for (auto key : ObjectKeys)
			if (ini.get(id, key).size() != 1 || ini.getString(id, key).empty()) return Reject("manifest-object-required-field");
		TownVisualSceneObjectAudit object;
		object.sceneIndex = candidates.size(); object.stableId = id;
		object.name = ini.getString(id, "name"); object.objectClass = ini.getString(id, "class"); object.assetPath = ini.getString(id, "model");
		object.sha256 = ini.getString(id, "sha256"); object.eventBinding = ini.getString(id, "eventBinding");
		const auto kind = std::find(ClassNames.begin(), ClassNames.end(), object.objectClass);
		if (!ValidText(object.name, 256, false) || kind == ClassNames.end() || !ValidAssetPath(object.assetPath, ModelPrefix) || !HexToModHash(object.sha256, expectedHash)
		    || !ParsePoint(ini.getString(id, "nativeMin"), object.nativeMin) || !ParsePoint(ini.getString(id, "nativeMax"), object.nativeMax)
		    || !ValidRectangle(object.nativeMin, object.nativeMax)) return Reject("manifest-object-identity");
		auto &transform = object.transform;
		if (!ParseVector(ini.getString(id, "pivot"), transform.pivot) || !ParseVector(ini.getString(id, "translation"), transform.translation)
		    || !ParseFloat(ini.getString(id, "yawDegrees"), transform.yawDegrees) || !ParseFloat(ini.getString(id, "uniformScale"), transform.uniformScale) || !ValidTransform(transform))
			return Reject("manifest-transform");
		if (object.eventBinding != "none" && object.eventBinding != "well-water" && object.eventBinding != "catacombs-access") return Reject("manifest-event-binding");
		if ((object.eventBinding == "well-water" && (object.objectClass != "well" || id != "well" || object.nativeMin != Point { 60, 70 } || object.nativeMax != Point { 61, 71 }))
		    || (object.eventBinding == "catacombs-access" && (object.objectClass != "crypt" || id != "catacombs-entrance" || object.nativeMin != Point { 48, 18 } || object.nativeMax != Point { 50, 21 })))
			return Reject("manifest-event-anchor");
		if ((id == "well" && object.eventBinding != "well-water") || (id == "catacombs-entrance" && object.eventBinding != "catacombs-access"))
			return Reject("manifest-required-event-binding");
		std::vector<std::byte> bytes;
		std::string failure;
		if (!resolver || !resolver(object.assetPath, bytes, object.sourceKind, failure)) return Reject(failure.empty() ? "asset-resolver" : failure);
		if (!ValidText(object.sourceKind, 128)) return Reject("asset-source-kind");
		if (bytes.size() > MaxModelBytes || sceneBytes > MaxSceneBytes - bytes.size()) return Reject("scene-byte-budget");
		sceneBytes += bytes.size();
		if (ComputeBytesSha256(bytes) != expectedHash) return Reject("asset-sha256");
		TownSceneModel model {};
		if (!DecodeModel(model, bytes, failure)) return Reject(failure);
		if (model.kind != static_cast<TownSceneKind>(kind - ClassNames.begin()) || model.minTile != object.nativeMin || model.maxTile != object.nativeMax) return Reject("asset-native-binding");
		sceneTriangles += ModelTriangleCount(model);
		if (sceneTriangles > MaxSceneTriangles) return Reject("scene-triangle-budget");
		candidateSources.push_back(model);
		if (model.cabinInterior && transform.yawDegrees != 0) return Reject("adjunct-rotation-unsupported");
		if (!ApplyTransform(model, transform)) return Reject("transformed-geometry-bounds");
		candidates.push_back(std::move(model)); candidateAudit.objects.push_back(std::move(object));
	}
	candidateAudit.status = "matched";
	// Evaluate native presentation before the static callback, so no allocation
	// or potentially failing decoder remains after its successful commit.
	for (auto &object : candidateAudit.objects) {
		if (object.eventBinding == "well-water") object.eventState = Quests[Q_PWATER]._qactive != QUEST_DONE && Quests[Q_PWATER]._qactive != QUEST_NOTAVAIL ? "poisoned-water" : "clean-water";
		else if (object.eventBinding == "catacombs-access") object.eventState = MyPlayer != nullptr && IsWarpOpen(DTYPE_CATACOMBS) ? "open" : "closed";
	}
	if (!candidateAudit.staticData.empty()) {
		std::string failure;
		if (!staticCommit || !staticCommit(candidateAudit.staticData, candidateAudit.staticSha256, failure)) return Reject(failure.empty() ? "static-commit-unavailable" : failure);
	}
	if (!logicPreflight) {
		models = std::move(candidates);
		SourceModels = std::move(candidateSources);
		SceneAudit = std::move(candidateAudit);
	}
	return true;
}

std::string ObjectId(const TownSceneModel &model, size_t index)
{
	if (const auto *binding = FindTownEditorModelBinding(model)) return std::string(binding->instanceId);
	// Source-bound deterministic identity even for an unregistered future group.
	return std::string(ClassNames[static_cast<unsigned>(model.kind)]) + "-" + std::to_string(model.minTile.x) + "-" + std::to_string(model.minTile.y) + "-" + std::to_string(index);
}

std::string ObjectName(std::string_view id)
{
	constexpr std::array<std::pair<std::string_view, std::string_view>, 14> Names { {
		{ "tavern-main", "Taverna - corpo principal" }, { "tavern-wing", "Taverna - ala lateral" },
		{ "smithy-house", "Ferraria - casa" }, { "smithy-forge", "Ferraria - oficina aberta" },
		{ "house-gillian", "Casa de Gillian" }, { "house-pepin", "Casa de Pepin" }, { "house-adria", "Cabana de Adria" },
		{ "house-north", "Casa ao norte" }, { "house-southeast", "Casa de Farnham" },
		{ "cabin-west", "Cabana oeste" }, { "cabin-east", "Cabana leste" }, { "well", "Poco de Tristram" },
		{ "cathedral", "Catedral" }, { "catacombs-entrance", "Entrada das Catacumbas" },
	} };
	for (const auto &[key, name] : Names) if (id == key) return std::string(name);
	return std::string(id);
}

bool WriteFile(const std::filesystem::path &path, std::span<const std::byte> bytes, std::string &failure)
{
	std::error_code error;
	if (std::filesystem::exists(path, error) || error) return Fail(failure, "export-target-exists");
	std::ofstream file(path, std::ios::binary);
	if (!file) return Fail(failure, "export-open");
	file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	file.flush();
	return file.good() || Fail(failure, "export-write");
}

} // namespace

bool ParseTownVisualSceneModel(TownSceneModel &model, std::span<const std::byte> data, std::string &failure)
{
	try { return DecodeModel(model, data, failure); }
	catch (const std::bad_alloc &) { return Fail(failure, "allocation"); }
}

bool SerializeTownVisualSceneModel(const TownSceneModel &model, std::vector<std::byte> &data, std::string &failure)
{
	try {
		if (static_cast<unsigned>(model.kind) >= ClassNames.size() || model.triangles.empty() || model.triangles.size() > MaxTriangles || model.materialPatches.size() > MaxPatches)
			return Fail(failure, "model-counts");
		if (ModelTriangleCount(model) > MaxTriangles) return Fail(failure, "model-total-triangle-budget");
		const auto &sourceAudit = model.runtimeAudit;
		if (!ValidText(sourceAudit.assetPath, 1024) || !ValidText(sourceAudit.sourceKind, 128) || !ValidText(sourceAudit.sha256, 64)
		    || !ValidText(sourceAudit.expectedSha256, 64) || !ValidText(sourceAudit.revision, 512) || !ValidText(sourceAudit.status, 128) || !ValidText(sourceAudit.failure, 1024))
			return Fail(failure, "model-provenance");
		uint32_t width = 0, height = 0;
		if (model.importedTexture) {
			width = model.importedTexture->width; height = model.importedTexture->height;
			if (width == 0 || width > 2048 || height == 0 || height > 2048 || model.importedTexture->rgb.size() != static_cast<size_t>(width) * height * 3)
				return Fail(failure, "model-texture");
		}
		std::vector<std::byte> bytes;
		bytes.reserve(168 + model.triangles.size() * 104 + model.materialPatches.size() * 112 + static_cast<size_t>(width) * height * 3);
		for (unsigned char c : std::string_view("D3DVIS1\0", 8)) bytes.push_back(static_cast<std::byte>(c));
		Writer writer(bytes);
		writer.u32(1); writer.u32(static_cast<uint32_t>(model.kind)); writer.u32((model.externalModel ? 1 : 0) | (model.importedTexture ? 2 : 0));
		writer.point(model.minTile); writer.point(model.maxTile);
		const auto &b = model.physicalBounds;
		writer.f32(b.minX); writer.f32(b.minZ); writer.f32(b.maxX); writer.f32(b.maxZ); writer.f32(b.wallHeight); writer.boolean(b.closed);
		const auto &art = model.nativeArtwork;
		writer.point(art.referenceTile); writer.point(art.pixelOrigin); writer.point(art.pixelSize); writer.boolean(art.enabled);
		writer.point(art.minTile); writer.point(art.maxTile); writer.u32(art.fringeMinPiece); writer.u32(art.fringeMaxPiece);
		writer.u32(static_cast<uint32_t>(model.triangles.size())); writer.u32(static_cast<uint32_t>(model.materialPatches.size())); writer.u32(width); writer.u32(height); writer.u32(model.cabinInterior ? 1 : 0);
		const auto &audit = model.runtimeAudit;
		for (const auto *value : { &audit.assetPath, &audit.sourceKind, &audit.sha256, &audit.expectedSha256, &audit.revision, &audit.status, &audit.failure }) writer.text(*value);
		for (const auto &triangle : model.triangles) WriteTriangle(writer, triangle);
		for (const auto &patch : model.materialPatches) {
			writer.u32(static_cast<uint32_t>(patch.surfaceDetail)); writer.u32(static_cast<uint32_t>(patch.material));
			for (const auto &vertex : patch.referencePlane) writer.vertex(vertex);
			for (const auto *values : { &patch.uvMin, &patch.uvMax, &patch.repeatWorldSize }) for (float value : *values) writer.f32(value);
			writer.boolean(patch.repeat); writer.point(patch.sourceMin); writer.point(patch.sourceMax);
		}
		if (model.importedTexture) for (uint8_t value : model.importedTexture->rgb) bytes.push_back(static_cast<std::byte>(value));
		if (model.cabinInterior && !WriteAdjunct(writer, *model.cabinInterior, failure)) return false;
		TownSceneModel check {};
		if (!DecodeModel(check, bytes, failure)) return false;
		data = std::move(bytes); failure.clear(); return true;
	} catch (const std::bad_alloc &) { return Fail(failure, "allocation"); }
}

bool ParseTownVisualScene(std::vector<TownSceneModel> &models, std::span<const std::byte> manifest,
    const TownVisualSceneAssetResolver &resolver, const TownVisualSceneStaticCommit &staticCommit)
{
	SceneAudit = {}; SceneAudit.sourceKind = "memory";
	try { return ParseManifest(models, manifest, resolver, staticCommit); }
	catch (const std::bad_alloc &) { return Reject("allocation"); }
}

bool ValidateTownVisualSceneForLogic(std::span<const std::byte> manifest,
    const TownVisualSceneAssetResolver &resolver, std::string &failure)
{
	// Native validation must not change a previously loaded visual source or
	// audit, including when malformed architecture throws during staging.
	auto savedAudit = std::move(SceneAudit);
	SceneAudit = {};
	bool valid = false;
	try {
		std::vector<TownSceneModel> models;
		const TownVisualSceneStaticCommit validatedStatic = [](std::string_view, std::string_view, std::string &) { return true; };
		valid = ParseManifest(models, manifest, resolver, validatedStatic, true, true);
		if (!valid) failure = SceneAudit.failure;
	} catch (const std::bad_alloc &) {
		failure = "patch-architecture-allocation";
	} catch (const std::exception &) {
		failure = "patch-architecture-exception";
	}
	SceneAudit = std::move(savedAudit);
	if (valid) failure.clear();
	return valid;
}

bool LoadTownVisualScene(std::vector<TownSceneModel> &models, const TownVisualSceneStaticCommit &staticCommit)
{
	SceneAudit = {};
	try {
		AssetRef ref = FindAsset(TownVisualSceneManifestPath);
		if (!ref.ok()) { SceneAudit.status = "missing"; return false; }
#ifdef UNPACKED_MPQS
		SceneAudit.sourceKind = "unpacked-file";
#else
		SceneAudit.sourceKind = ref.archive != nullptr ? "archive" : (ref.isOverridden ? "override" : "loose-file");
#endif
		const size_t size = ref.size();
		if (size == 0 || size > MaxManifestBytes) return Reject("manifest-size");
		AssetHandle handle = OpenAsset(std::move(ref));
		if (!handle.ok()) return Reject("manifest-open");
		std::vector<std::byte> data(size);
		if (!handle.read(data.data(), data.size())) return Reject("manifest-read");
		return ParseManifest(models, data, ResolveRuntimeAsset, staticCommit, true);
	} catch (const std::bad_alloc &) { return Reject("allocation"); }
}

bool ExportTownVisualScene(std::span<const TownSceneModel> models, const std::filesystem::path &directory,
    std::string_view revision, std::string &failure, const TownVisualSceneStaticReference &staticReference)
{
	try {
		if (gbIsHellfire || models.empty() || models.size() > MaxObjects || !directory.is_absolute() || !ValidText(revision, 128, false)) return Fail(failure, "export-arguments");
		std::array<uint8_t, 32> hash;
		if (staticReference.assetPath.empty() != staticReference.sha256.empty()
		    || (!staticReference.assetPath.empty() && (!ValidAssetPath(staticReference.assetPath, StaticPrefix) || !HexToModHash(staticReference.sha256, hash))))
			return Fail(failure, "export-static-reference");
		std::set<std::string> ids;
		std::vector<std::pair<std::string, std::vector<std::byte>>> assets;
		assets.reserve(models.size());
		for (size_t i = 0; i < models.size(); ++i) {
			if (static_cast<unsigned>(models[i].kind) >= ClassNames.size()) return Fail(failure, "export-model-kind");
			std::string id = ObjectId(models[i], i);
			if (!ValidId(id) || !ids.insert(id).second) return Fail(failure, "export-duplicate-id");
			std::vector<std::byte> bytes;
			if (!SerializeTownVisualSceneModel(models[i], bytes, failure)) { failure = id + ":" + failure; return false; }
			assets.emplace_back(std::move(id), std::move(bytes));
		}
		std::ostringstream text;
		text.imbue(std::locale::classic()); text << std::setprecision(std::numeric_limits<float>::max_digits10);
		text << "; Explicit one-time migration; never regenerated during startup. Private assets.\n[Scene]\nformat=d3d.town-visual-scene\nschemaVersion=1\nedition=" << (gbIsSpawn ? "shareware" : "retail")
		     << "\nrevision=" << revision << "\nobjectCount=" << models.size() << '\n';
		if (!staticReference.assetPath.empty()) text << "staticData=" << staticReference.assetPath << "\nstaticSha256=" << staticReference.sha256 << '\n';
		for (const auto &[id, bytes] : assets) text << "object=" << id << '\n';
		for (size_t i = 0; i < models.size(); ++i) {
			const auto &model = models[i]; const auto &[id, bytes] = assets[i];
			text << "\n[" << id << "]\nname=" << ObjectName(id) << "\nclass=" << ClassNames[static_cast<unsigned>(model.kind)]
			     << "\nmodel=" << ModelPrefix << id << ".d3dvis\nsha256=" << ModHashToHex(ComputeBytesSha256(bytes))
			     << "\npivot=" << (static_cast<float>(model.minTile.x) + model.maxTile.x) / 2 << ",0," << (static_cast<float>(model.minTile.y) + model.maxTile.y) / 2
			     << "\ntranslation=0,0,0\nyawDegrees=0\nuniformScale=1\nnativeMin=" << model.minTile.x << ',' << model.minTile.y
			     << "\nnativeMax=" << model.maxTile.x << ',' << model.maxTile.y << "\neventBinding=" << (id == "well" ? "well-water" : id == "catacombs-entrance" ? "catacombs-access" : "none") << '\n';
		}
		const auto objectsPath = directory / "d3d-scenes" / "objects", manifestPath = directory / "d3d-scenes" / "tristram.ini";
		std::error_code error;
		if (std::filesystem::exists(manifestPath, error) || error) return Fail(failure, "export-target-exists");
		std::filesystem::create_directories(objectsPath, error);
		if (error) return Fail(failure, "export-directory");
		for (const auto &[id, bytes] : assets) if (!WriteFile(objectsPath / (id + ".d3dvis"), bytes, failure)) return false;
		const std::string manifest = text.str();
		if (!WriteFile(manifestPath, std::as_bytes(std::span(manifest)), failure)) return false;
		failure.clear(); return true;
	} catch (const std::bad_alloc &) { return Fail(failure, "allocation"); }
	catch (const std::filesystem::filesystem_error &) { return Fail(failure, "export-filesystem"); }
}

bool UpdateTownVisualSceneEvents()
{
	if (SceneAudit.status != "matched") return false;
	bool changed = false;
	for (auto &object : SceneAudit.objects) {
		std::string_view state;
		if (object.eventBinding == "well-water") state = Quests[Q_PWATER]._qactive != QUEST_DONE && Quests[Q_PWATER]._qactive != QUEST_NOTAVAIL ? "poisoned-water" : "clean-water";
		else if (object.eventBinding == "catacombs-access") state = MyPlayer != nullptr && IsWarpOpen(DTYPE_CATACOMBS) ? "open" : "closed";
		else continue;
		if (object.eventState != state) { object.eventState = state; changed = true; }
	}
	return changed;
}

bool TownVisualSceneWaterPoisoned(size_t sceneIndex)
{
	if (SceneAudit.status == "matched") {
		if (sceneIndex >= SceneAudit.objects.size() || SceneAudit.objects[sceneIndex].eventBinding != "well-water") return false;
	}
	return Quests[Q_PWATER]._qactive != QUEST_DONE && Quests[Q_PWATER]._qactive != QUEST_NOTAVAIL;
}

const TownVisualSceneAudit &GetTownVisualSceneAudit() { return SceneAudit; }
std::span<const TownSceneModel> GetTownVisualSceneSourceModels()
{
	// A failed attempted reload leaves both active models and their material
	// sources intact. Reset, rather than audit status, owns their lifetime.
	return SourceModels;
}
void ResetTownVisualSceneAudit() { SceneAudit = {}; SourceModels.clear(); }

} // namespace devilution
