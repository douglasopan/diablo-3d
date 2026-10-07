#include "engine/render/town_model_import.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include "mods/mod_identity.h"
#include "engine/assets.hpp"
#include "levels/dun_tile_data.hpp"

namespace devilution {
namespace {

constexpr std::size_t HeaderBytes = 20;
constexpr uint32_t MaxTriangles = 20000;
constexpr uint32_t MaxTextureSide = 2048;
constexpr std::size_t BytesPerTriangle = 15 * sizeof(uint32_t);
constexpr std::size_t MaxAssetBytes = HeaderBytes + MaxTriangles * BytesPerTriangle + MaxTextureSide * MaxTextureSide * 3;
constexpr std::array<char, 8> Magic { 'D', '3', 'D', 'M', 'E', 'S', 'H', '1' };

uint32_t ReadWord(std::span<const std::byte> data, std::size_t offset)
{
	return std::to_integer<uint32_t>(data[offset])
	    | (std::to_integer<uint32_t>(data[offset + 1]) << 8)
	    | (std::to_integer<uint32_t>(data[offset + 2]) << 16)
	    | (std::to_integer<uint32_t>(data[offset + 3]) << 24);
}

float ReadFloat(std::span<const std::byte> data, std::size_t &offset)
{
	const float value = std::bit_cast<float>(ReadWord(data, offset));
	offset += sizeof(uint32_t);
	return value;
}

std::vector<Point> BlockedSourceTiles(const TownSceneModel &model)
{
	std::vector<Point> result;
	const int minX = std::max(0, model.minTile.x);
	const int minY = std::max(0, model.minTile.y);
	const int maxX = std::min(MAXDUNX - 1, model.maxTile.x);
	const int maxY = std::min(MAXDUNY - 1, model.maxTile.y);
	for (int y = minY; y <= maxY; ++y) {
		for (int x = minX; x <= maxX; ++x) {
			const uint16_t piece = dPiece[x][y];
			if (piece < MAXTILES && HasAnyOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile))
				result.push_back({ x, y });
		}
	}
	return result;
}

Point NearestPickTile(const TownSceneTriangle &triangle, const std::vector<Point> &blocked, Point fallback)
{
	const float x = (triangle.vertices[0].x + triangle.vertices[1].x + triangle.vertices[2].x) / 3;
	const float z = (triangle.vertices[0].z + triangle.vertices[1].z + triangle.vertices[2].z) / 3;
	float nearest = std::numeric_limits<float>::infinity();
	Point result = fallback;
	for (Point tile : blocked) {
		const float dx = static_cast<float>(tile.x) - x;
		const float dz = static_cast<float>(tile.y) - z;
		const float distance = dx * dx + dz * dz;
		if (distance < nearest) {
			nearest = distance;
			result = tile;
		}
	}
	return result;
}

bool ReadVertex(std::span<const std::byte> data, std::size_t &offset, Point origin, TownSceneVertex &vertex)
{
	const float x = ReadFloat(data, offset);
	const float height = ReadFloat(data, offset);
	const float z = ReadFloat(data, offset);
	const float u = ReadFloat(data, offset);
	const float v = ReadFloat(data, offset);
	if (!std::isfinite(x) || !std::isfinite(height) || !std::isfinite(z) || !std::isfinite(u) || !std::isfinite(v)
	    || x < -64 || x > 128 || z < -64 || z > 128 || height < 0 || height > 64
	    || u < 0 || u > 1 || v < 0 || v > 1)
		return false;
	vertex = { x + static_cast<float>(origin.x), height, z + static_cast<float>(origin.y), u, v };
	return true;
}

bool ComputeNormal(TownSceneTriangle &triangle)
{
	const auto &a = triangle.vertices[0];
	const auto &b = triangle.vertices[1];
	const auto &c = triangle.vertices[2];
	const double ax = static_cast<double>(b.x) - a.x;
	const double ah = static_cast<double>(b.height) - a.height;
	const double az = static_cast<double>(b.z) - a.z;
	const double bx = static_cast<double>(c.x) - a.x;
	const double bh = static_cast<double>(c.height) - a.height;
	const double bz = static_cast<double>(c.z) - a.z;
	const double x = ah * bz - az * bh;
	const double height = az * bx - ax * bz;
	const double z = ax * bh - ah * bx;
	const double length = std::sqrt(x * x + height * height + z * z);
	if (!std::isfinite(length) || length <= 0.000001)
		return false;
	triangle.normal = { static_cast<float>(x / length), static_cast<float>(height / length), static_cast<float>(z / length) };
	return true;
}

} // namespace

bool ParseTownModelOverride(TownSceneModel &model, std::span<const std::byte> data)
{
	if (data.size() < HeaderBytes || data.size() > MaxAssetBytes
	    || std::memcmp(data.data(), Magic.data(), Magic.size()) != 0
	    || model.minTile.x < 0 || model.minTile.x >= MAXDUNX || model.minTile.y < 0 || model.minTile.y >= MAXDUNY)
		return false;
	const uint32_t triangleCount = ReadWord(data, 8);
	const uint32_t width = ReadWord(data, 12);
	const uint32_t height = ReadWord(data, 16);
	if (triangleCount == 0 || triangleCount > MaxTriangles || width == 0 || width > MaxTextureSide || height == 0 || height > MaxTextureSide)
		return false;
	const std::size_t triangleBytes = static_cast<std::size_t>(triangleCount) * BytesPerTriangle;
	const std::size_t textureBytes = static_cast<std::size_t>(width) * height * 3;
	if (data.size() != HeaderBytes + triangleBytes + textureBytes)
		return false;
	try {
		const std::vector<Point> blocked = BlockedSourceTiles(model);
		const Point sourceTile = model.triangles.empty() ? model.minTile : model.triangles.front().sourceTile;
		const Point fallbackPick = model.triangles.empty() ? model.minTile : model.triangles.front().pickTile;
		std::vector<TownSceneTriangle> triangles;
		triangles.reserve(triangleCount);
		std::size_t offset = HeaderBytes;
		for (uint32_t i = 0; i < triangleCount; ++i) {
			TownSceneTriangle triangle {};
			for (TownSceneVertex &vertex : triangle.vertices)
				if (!ReadVertex(data, offset, model.minTile, vertex))
					return false;
			if (!ComputeNormal(triangle))
				return false;
			triangle.material = TownSceneMaterial::Wall;
			triangle.sourceTile = sourceTile;
			triangle.pickTile = NearestPickTile(triangle, blocked, fallbackPick);
			triangle.nativeProjection = false;
			triangle.surfaceRole = TownSceneSurfaceRole::Exterior;
			triangle.surfaceDetail = TownSceneSurfaceDetail::None;
			triangles.push_back(triangle);
		}
		auto texture = std::make_shared<TownImportedTexture>();
		texture->width = width;
		texture->height = height;
		texture->rgb.resize(textureBytes);
		std::memcpy(texture->rgb.data(), data.data() + offset, textureBytes);
		// Commit only after all decoding succeeds. A corrupt optional override
		// cannot erase the current procedural model or partially change its art.
		model.triangles = std::move(triangles);
		model.importedTexture = std::move(texture);
		model.externalModel = true;
		return true;
	} catch (const std::bad_alloc &) {
		return false;
	}
}

bool LoadTownModelOverride(TownSceneModel &model, std::string_view assetPath)
{
	if (assetPath.empty() || assetPath.find('\0') != std::string_view::npos)
		return false;
	AssetRef ref = FindAsset(assetPath);
	if (!ref.ok())
		return false;
	const std::size_t size = ref.size();
	if (size < HeaderBytes || size > MaxAssetBytes)
		return false;
	AssetHandle handle = OpenAsset(std::move(ref));
	if (!handle.ok())
		return false;
	try {
		std::vector<std::byte> data(size);
		if (!handle.read(data.data(), data.size()))
			return false;
		return ParseTownModelOverride(model, data);
	} catch (const std::bad_alloc &) {
		return false;
	}
}

} // namespace devilution
