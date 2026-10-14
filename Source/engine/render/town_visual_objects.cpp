#include "engine/render/town_visual_objects.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <set>
#include <utility>

#include "engine/assets.hpp"
#include "mods/mod_identity.h"

namespace devilution {
namespace {
constexpr size_t MaxBytes = 256 * 1024 * 1024;
constexpr uint32_t GridSize = 112;
constexpr uint32_t MaxObjects = 30000, MaxTextures = 4096;
constexpr uint32_t MaxTextureSide = 4096;
constexpr size_t MaxTexturePixels = 64 * 1024 * 1024;
constexpr size_t MaxTriangles = 4000000;
constexpr uint32_t MaxObjectTriangles = 100000, MaxObjectCells = 1024;
TownVisualObjectSet Current;
TownVisualObjectAudit Audit;
bool Ready = false;

bool Fail(std::string &failure, std::string_view reason)
{
	failure = reason;
	return false;
}

bool ValidId(std::string_view id)
{
	return !id.empty() && id.size() <= 96 && std::all_of(id.begin(), id.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
	});
}

bool ValidPoint(Point point)
{
	return point.x >= 0 && point.y >= 0 && point.x < static_cast<int>(GridSize) && point.y < static_cast<int>(GridSize);
}

bool ValidFloat(float value, float bound = 512)
{
	return std::isfinite(value) && std::abs(value) <= bound;
}

bool ValidTransform(const TownVisualObjectTransform &transform)
{
	return ValidFloat(transform.x) && ValidFloat(transform.height) && ValidFloat(transform.z)
	    && ValidFloat(transform.yaw, 100) && std::isfinite(transform.scale) && transform.scale >= 0.01F && transform.scale <= 20;
}

bool ValidEventCell(const TownVisualSourceCell &cell, TownVisualObjectEvent binding)
{
	const auto inside = [&](int x0, int z0, int x1, int z1) { return cell.tile.x >= x0 && cell.tile.x <= x1 && cell.tile.y >= z0 && cell.tile.y <= z1; };
	switch (binding) {
	case TownVisualObjectEvent::None: return true;
	case TownVisualObjectEvent::WellWater: return inside(60, 70, 61, 71);
	case TownVisualObjectEvent::CatacombsAccess: return inside(48, 20, 49, 21);
	case TownVisualObjectEvent::CavesAccess: return inside(16, 68, 17, 71);
	case TownVisualObjectEvent::HellAccess: return inside(36, 78, 55, 79);
	}
	return false;
}

struct Reader {
	std::span<const std::byte> bytes;
	size_t offset = 0;
	bool Copy(void *out, size_t size)
	{
		if (size > bytes.size() - offset) return false;
		if (size != 0) std::memcpy(out, bytes.data() + offset, size);
		offset += size;
		return true;
	}
	bool U8(uint8_t &out)
	{
		if (offset == bytes.size()) return false;
		out = std::to_integer<uint8_t>(bytes[offset++]);
		return true;
	}
	bool U16(uint16_t &out)
	{
		uint8_t a, b;
		if (!U8(a) || !U8(b)) return false;
		out = a | (uint16_t(b) << 8);
		return true;
	}
	bool U32(uint32_t &out)
	{
		uint16_t a, b;
		if (!U16(a) || !U16(b)) return false;
		out = a | (uint32_t(b) << 16);
		return true;
	}
	bool Float(float &out)
	{
		uint32_t value;
		if (!U32(value)) return false;
		out = std::bit_cast<float>(value);
		return true;
	}
};

struct Writer {
	std::vector<std::byte> bytes;
	void U8(uint8_t value) { bytes.push_back(std::byte(value)); }
	void U16(uint16_t value) { U8(value & 255); U8(value >> 8); }
	void U32(uint32_t value) { U16(value & 65535); U16(value >> 16); }
	void Float(float value) { U32(std::bit_cast<uint32_t>(value)); }
	void Copy(const void *data, size_t size)
	{
		const auto *begin = reinterpret_cast<const std::byte *>(data);
		bytes.insert(bytes.end(), begin, begin + size);
	}
};

bool Validate(const TownVisualObjectSet &set, std::string &failure)
{
	if (set.objects.empty() || set.objects.size() > MaxObjects || set.textures.empty() || set.textures.size() > MaxTextures)
		return Fail(failure, "count");
	size_t pixels = 0, triangles = 0, encodedBytes = 28;
	for (const auto &texture : set.textures) {
		if (texture.width == 0 || texture.height == 0 || texture.width > MaxTextureSide || texture.height > MaxTextureSide)
			return Fail(failure, "texture-dimensions");
		const size_t count = size_t(texture.width) * texture.height;
		if (texture.pixels.size() != count || (!texture.opacity.empty() && texture.opacity.size() != count)
		    || std::any_of(texture.opacity.begin(), texture.opacity.end(), [](uint8_t a) { return a > 1; })
		    || count > MaxTexturePixels - pixels)
			return Fail(failure, "texture-payload");
		pixels += count;
		const size_t textureBytes = 12 + count + texture.opacity.size();
		if (textureBytes > MaxBytes - encodedBytes) return Fail(failure, "archive-size");
		encodedBytes += textureBytes;
	}
	std::set<std::string_view> ids;
	for (const auto &object : set.objects) {
		if (!ValidId(object.id) || !ids.insert(object.id).second || !ValidPoint(object.anchor)
		    || static_cast<uint8_t>(object.kind) > static_cast<uint8_t>(TownVisualObjectKind::Vegetation)
		    || object.phase > 2 || !ValidTransform(object.transform) || object.texture >= set.textures.size()
		    || static_cast<uint8_t>(object.eventBinding) > static_cast<uint8_t>(TownVisualObjectEvent::HellAccess)
		    || (object.eventBinding == TownVisualObjectEvent::None && object.eventState)
		    || (object.groundFallback && object.kind != TownVisualObjectKind::Ground)
		    || (object.eventBinding != TownVisualObjectEvent::None && object.kind != TownVisualObjectKind::Ground && object.kind != TownVisualObjectKind::Scenery)
		    || object.sourceCells.empty() || object.sourceCells.size() > MaxObjectCells)
			return Fail(failure, "object-metadata");
		if ((object.kind == TownVisualObjectKind::Ground && (object.phase == 2 || !object.mesh.triangles.empty() || object.physicalFrame))
		    || (object.kind == TownVisualObjectKind::Scenery && object.phase != 1)
		    || ((object.kind == TownVisualObjectKind::Prop || object.kind == TownVisualObjectKind::Vegetation) && object.phase != 2))
			return Fail(failure, "object-phase");
		std::set<int> cells;
		for (const auto &cell : object.sourceCells)
			if (!ValidPoint(cell.tile) || cell.piece >= 2048 || cell.special > 255 || !ValidEventCell(cell, object.eventBinding) || !cells.insert(cell.tile.x + cell.tile.y * GridSize).second)
				return Fail(failure, "source-cell");
		if (object.mesh.triangles.size() > MaxObjectTriangles || object.mesh.triangles.size() > MaxTriangles - triangles
		    || !ValidFloat(object.mesh.width) || !ValidFloat(object.mesh.height) || !ValidFloat(object.mesh.depth))
			return Fail(failure, "mesh-limit");
		triangles += object.mesh.triangles.size();
		const size_t objectBytes = 60 + object.id.size() + object.sourceCells.size() * 8 + object.mesh.triangles.size() * 64;
		if (objectBytes > MaxBytes - encodedBytes) return Fail(failure, "archive-size");
		encodedBytes += objectBytes;
		for (const auto &triangle : object.mesh.triangles) {
			if (static_cast<uint8_t>(triangle.material) > static_cast<uint8_t>(TownVolumeMaterial::Foliage) || triangle.textureView != 0)
				return Fail(failure, "mesh-material");
			for (const auto &v : triangle.vertices)
				if (!ValidFloat(v.x) || !ValidFloat(v.height) || !ValidFloat(v.z) || !ValidFloat(v.u, 2) || !ValidFloat(v.v, 2))
					return Fail(failure, "mesh-float");
			const auto &a = triangle.vertices[0], &b = triangle.vertices[1], &c = triangle.vertices[2];
			const double x = double(b.height - a.height) * (c.z - a.z) - double(b.z - a.z) * (c.height - a.height);
			const double h = double(b.z - a.z) * (c.x - a.x) - double(b.x - a.x) * (c.z - a.z);
			const double z = double(b.x - a.x) * (c.height - a.height) - double(b.height - a.height) * (c.x - a.x);
			if (x * x + h * h + z * z <= 1.0e-20) return Fail(failure, "degenerate-triangle");
		}
	}
	failure.clear();
	return true;
}
} // namespace

bool ParseTownVisualObjects(std::span<const std::byte> bytes, TownVisualObjectSet &out, std::string &failure)
{
	try {
		if (bytes.size() < 28 || bytes.size() > MaxBytes) return Fail(failure, "archive-size");
		Reader reader { bytes };
		char magic[8]; uint32_t version, gridX, gridZ, textures, objects;
		if (!reader.Copy(magic, 8) || std::memcmp(magic, "D3DSTAT1", 8) != 0 || !reader.U32(version) || version != 1
		    || !reader.U32(gridX) || !reader.U32(gridZ) || gridX != GridSize || gridZ != GridSize
		    || !reader.U32(textures) || !reader.U32(objects) || textures == 0 || textures > MaxTextures || objects == 0 || objects > MaxObjects)
			return Fail(failure, "archive-header");
		TownVisualObjectSet candidate;
		candidate.textures.reserve(textures); candidate.objects.reserve(objects);
		size_t pixels = 0, totalTriangles = 0;
		for (uint32_t i = 0; i < textures; ++i) {
			TownVisualObjectTexture texture; uint32_t flags;
			if (!reader.U32(texture.width) || !reader.U32(texture.height) || !reader.U32(flags) || flags > 3
			    || texture.width == 0 || texture.height == 0 || texture.width > MaxTextureSide || texture.height > MaxTextureSide)
				return Fail(failure, "texture-header");
			const size_t count = size_t(texture.width) * texture.height;
			if (count > MaxTexturePixels - pixels || count * ((flags & 2) ? 2 : 1) > bytes.size() - reader.offset)
				return Fail(failure, "texture-size");
			pixels += count; texture.repeat = (flags & 1) != 0;
			texture.pixels.resize(count);
			if (!reader.Copy(texture.pixels.data(), count)) return Fail(failure, "texture-read");
			if (flags & 2) {
				texture.opacity.resize(count);
				if (!reader.Copy(texture.opacity.data(), count)) return Fail(failure, "texture-opacity-read");
			}
			candidate.textures.push_back(std::move(texture));
		}
		for (uint32_t i = 0; i < objects; ++i) {
			TownVisualObject object; uint32_t idSize, anchorX, anchorZ, cells, triangles;
		uint8_t kind, flags, reserved, eventBinding;
			if (!reader.U32(idSize) || idSize == 0 || idSize > 96 || idSize > bytes.size() - reader.offset)
				return Fail(failure, "object-id-size");
			object.id.resize(idSize);
			if (!reader.Copy(object.id.data(), idSize) || !reader.U8(kind) || !reader.U8(flags) || flags > 31
			    || !reader.U8(object.phase) || !reader.U8(eventBinding) || eventBinding > 4
			    || !reader.U32(anchorX) || !reader.U32(anchorZ) || anchorX >= GridSize || anchorZ >= GridSize
			    || !reader.Float(object.transform.x) || !reader.Float(object.transform.height) || !reader.Float(object.transform.z)
			    || !reader.Float(object.transform.yaw) || !reader.Float(object.transform.scale)
			    || !reader.U32(object.texture) || !reader.U32(cells) || !reader.U32(triangles)
			    || !reader.Float(object.mesh.width) || !reader.Float(object.mesh.height) || !reader.Float(object.mesh.depth)
			    || cells == 0 || cells > MaxObjectCells || triangles > MaxObjectTriangles || triangles > MaxTriangles - totalTriangles
			    || size_t(cells) * 8 + size_t(triangles) * 64 > bytes.size() - reader.offset)
				return Fail(failure, "object-header");
			totalTriangles += triangles;
			object.anchor = { static_cast<int>(anchorX), static_cast<int>(anchorZ) };
			object.kind = static_cast<TownVisualObjectKind>(kind);
			object.physicalFrame = (flags & 1) != 0; object.visible = (flags & 2) != 0; object.nativeSuppressed = (flags & 4) != 0;
			object.eventBinding = static_cast<TownVisualObjectEvent>(eventBinding); object.eventState = (flags & 8) != 0;
			object.groundFallback = (flags & 16) != 0;
			object.sourceCells.reserve(cells); object.mesh.triangles.resize(triangles);
			for (uint32_t c = 0; c < cells; ++c) {
				TownVisualSourceCell cell; uint16_t x, z;
				if (!reader.U16(x) || !reader.U16(z) || !reader.U16(cell.piece) || !reader.U16(cell.special)) return Fail(failure, "cell-read");
				cell.tile = { x, z }; object.sourceCells.push_back(cell);
			}
			for (auto &triangle : object.mesh.triangles) {
				for (auto &vertex : triangle.vertices)
					if (!reader.Float(vertex.x) || !reader.Float(vertex.height) || !reader.Float(vertex.z) || !reader.Float(vertex.u) || !reader.Float(vertex.v))
						return Fail(failure, "triangle-read");
				uint8_t material;
				if (!reader.U8(material) || !reader.U8(triangle.paletteIndex) || !reader.U8(triangle.textureView) || !reader.U8(reserved) || reserved != 0)
					return Fail(failure, "triangle-metadata");
				triangle.material = static_cast<TownVolumeMaterial>(material);
			}
			candidate.objects.push_back(std::move(object));
		}
		if (reader.offset != bytes.size()) return Fail(failure, "trailing-bytes");
		if (!Validate(candidate, failure)) return false;
		out = std::move(candidate);
		return true;
	} catch (const std::bad_alloc &) { return Fail(failure, "allocation"); }
}

bool EncodeTownVisualObjects(const TownVisualObjectSet &set, std::vector<std::byte> &out, std::string &failure)
{
	try {
		if (!Validate(set, failure)) return false;
		Writer writer;
		writer.Copy("D3DSTAT1", 8); writer.U32(1); writer.U32(GridSize); writer.U32(GridSize);
		writer.U32(static_cast<uint32_t>(set.textures.size())); writer.U32(static_cast<uint32_t>(set.objects.size()));
		for (const auto &texture : set.textures) {
			writer.U32(texture.width); writer.U32(texture.height); writer.U32((texture.repeat ? 1 : 0) | (texture.opacity.empty() ? 0 : 2));
			writer.Copy(texture.pixels.data(), texture.pixels.size());
			if (!texture.opacity.empty()) writer.Copy(texture.opacity.data(), texture.opacity.size());
		}
		for (const auto &object : set.objects) {
			writer.U32(static_cast<uint32_t>(object.id.size())); writer.Copy(object.id.data(), object.id.size());
			writer.U8(static_cast<uint8_t>(object.kind)); writer.U8((object.physicalFrame ? 1 : 0) | (object.visible ? 2 : 0) | (object.nativeSuppressed ? 4 : 0) | (object.eventState ? 8 : 0) | (object.groundFallback ? 16 : 0));
			writer.U8(object.phase); writer.U8(static_cast<uint8_t>(object.eventBinding)); writer.U32(object.anchor.x); writer.U32(object.anchor.y);
			writer.Float(object.transform.x); writer.Float(object.transform.height); writer.Float(object.transform.z); writer.Float(object.transform.yaw); writer.Float(object.transform.scale);
			writer.U32(object.texture); writer.U32(static_cast<uint32_t>(object.sourceCells.size())); writer.U32(static_cast<uint32_t>(object.mesh.triangles.size()));
			writer.Float(object.mesh.width); writer.Float(object.mesh.height); writer.Float(object.mesh.depth);
			for (const auto &cell : object.sourceCells) { writer.U16(cell.tile.x); writer.U16(cell.tile.y); writer.U16(cell.piece); writer.U16(cell.special); }
			for (const auto &triangle : object.mesh.triangles) {
				for (const auto &v : triangle.vertices) { writer.Float(v.x); writer.Float(v.height); writer.Float(v.z); writer.Float(v.u); writer.Float(v.v); }
				writer.U8(static_cast<uint8_t>(triangle.material)); writer.U8(triangle.paletteIndex); writer.U8(triangle.textureView); writer.U8(0);
			}
			if (writer.bytes.size() > MaxBytes) return Fail(failure, "archive-size");
		}
		out = std::move(writer.bytes);
		return true;
	} catch (const std::bad_alloc &) { return Fail(failure, "allocation"); }
}

bool LoadTownVisualObjects(std::string_view path, std::string_view expectedSha256, std::string &failure)
{
	try {
		constexpr std::string_view Prefix = "d3d-scenes/static/";
		if (!path.starts_with(Prefix) || !path.ends_with(".d3dvis") || path.size() > 256 || path.find("..") != std::string_view::npos
		    || path.find("//") != std::string_view::npos || !std::all_of(path.begin(), path.end(), [](char c) {
			    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.';
		    })) return Fail(failure, "static-path");
		std::array<uint8_t, 32> expected;
		if (!HexToModHash(expectedSha256, expected)) return Fail(failure, "static-hash-format");
		AssetRef ref = FindAsset(path);
		if (!ref.ok()) return Fail(failure, "static-missing");
		const size_t size = ref.size();
		if (size < 28 || size > MaxBytes) return Fail(failure, "static-size");
		AssetHandle handle = OpenAsset(std::move(ref));
		if (!handle.ok()) return Fail(failure, "static-open");
		std::vector<std::byte> bytes(size);
		if (!handle.read(bytes.data(), bytes.size())) return Fail(failure, "static-read");
		const auto hash = ComputeBytesSha256(bytes);
		if (hash != expected) return Fail(failure, "static-hash-mismatch");
		TownVisualObjectSet candidate;
		if (!ParseTownVisualObjects(bytes, candidate, failure)) return false;
		TownVisualObjectAudit audit;
		audit.path = path; audit.expectedSha256 = expectedSha256; audit.sha256 = ModHashToHex(hash); audit.status = "matched";
		audit.objects = candidate.objects.size(); audit.textures = candidate.textures.size();
		for (const auto &object : candidate.objects) audit.triangles += object.mesh.triangles.size();
		Current = std::move(candidate); Audit = std::move(audit); Ready = true;
		failure.clear(); return true;
	} catch (const std::bad_alloc &) { return Fail(failure, "allocation"); }
}

bool WriteTownVisualObjects(const TownVisualObjectSet &objects, const std::filesystem::path &path, std::string &failure)
{
	std::vector<std::byte> bytes;
	if (!EncodeTownVisualObjects(objects, bytes, failure)) return false;
	std::error_code error;
	if (std::filesystem::exists(path, error)) return Fail(failure, "static-export-exists");
	std::filesystem::create_directories(path.parent_path(), error);
	if (error) return Fail(failure, "static-export-directory");
	std::ofstream file(path, std::ios::binary);
	if (!file) return Fail(failure, "static-export-open");
	file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())); file.close();
	if (!file) return Fail(failure, "static-export-write");
	failure.clear(); return true;
}

bool HasTownVisualObjects() { return Ready; }
const TownVisualObjectSet &GetTownVisualObjects() { return Current; }
const TownVisualObjectAudit &GetTownVisualObjectAudit() { return Audit; }
void ClearTownVisualObjects() { Current = {}; Audit = {}; Ready = false; }

} // namespace devilution
