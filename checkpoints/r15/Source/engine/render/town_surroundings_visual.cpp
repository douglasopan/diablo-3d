#include "engine/render/town_surroundings_visual.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#include "engine/assets.hpp"
#include "engine/render/town_surroundings_asset_identity.hpp"
#include "mods/mod_identity.h"

namespace devilution {
namespace {

constexpr std::string_view AssetPath = "d3d-scenes/environment/tristram-surroundings-r2.d3denv";
constexpr size_t HeaderBytes = 24;
constexpr size_t TriangleBytes = 48;
constexpr size_t CellBytes = 20;
constexpr size_t MaxBytes = 64 * 1024 * 1024;
constexpr uint32_t MaxTriangles = 1000000;
constexpr uint32_t MaxCells = 100000;
constexpr uint32_t SceneSeed = 0x54525331;
constexpr float CoordinateLimit = 20000;
constexpr float HeightLimit = 4096;
constexpr double ProtectedMinimum = -2;
constexpr double ProtectedMaximum = 114;
// Conservative envelope of all native floor quads, not their opaque footprint.
constexpr double FloorMinimum = -1.46875;
constexpr double FloorMaximum = 111.53125;
constexpr int UnderlaySideCells = 116;
constexpr uint32_t UnderlayCellCount = 4240;
constexpr float UnderlayHeight = -0.0625F;

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

TownSurroundingsVisualScene Current;
bool Attempted = false;

struct Reader {
	std::span<const std::byte> bytes;
	size_t offset = 0;

	bool Copy(void *out, size_t count)
	{
		if (offset > bytes.size() || count > bytes.size() - offset)
			return false;
		if (count != 0)
			std::memcpy(out, bytes.data() + offset, count);
		offset += count;
		return true;
	}

	bool U32(uint32_t &out)
	{
		if (offset > bytes.size() || 4 > bytes.size() - offset)
			return false;
		out = std::to_integer<uint32_t>(bytes[offset])
		    | (std::to_integer<uint32_t>(bytes[offset + 1]) << 8)
		    | (std::to_integer<uint32_t>(bytes[offset + 2]) << 16)
		    | (std::to_integer<uint32_t>(bytes[offset + 3]) << 24);
		offset += 4;
		return true;
	}

	bool I32(int32_t &out)
	{
		uint32_t bits;
		if (!U32(bits))
			return false;
		out = std::bit_cast<int32_t>(bits);
		return true;
	}

	bool Float(float &out)
	{
		uint32_t bits;
		if (!U32(bits))
			return false;
		out = std::bit_cast<float>(bits);
		return true;
	}
};

bool ValidPoint(const TownSurroundingsPoint &point)
{
	return std::isfinite(point.x) && std::abs(point.x) <= CoordinateLimit
	    && std::isfinite(point.height) && std::abs(point.height) <= HeightLimit
	    && std::isfinite(point.z) && std::abs(point.z) <= CoordinateLimit;
}

bool ValidColorComponent(float component)
{
	return std::isfinite(component) && component >= 0 && component <= 1;
}

bool ValidTriangle(const TownSurroundingsTriangle &triangle)
{
	if (!ValidColorComponent(triangle.color.red) || !ValidColorComponent(triangle.color.green)
	    || !ValidColorComponent(triangle.color.blue)
	    || !std::all_of(triangle.vertices.begin(), triangle.vertices.end(), ValidPoint))
		return false;
	const auto &a = triangle.vertices[0];
	const auto &b = triangle.vertices[1];
	const auto &c = triangle.vertices[2];
	const double abX = double(b.x) - a.x;
	const double abHeight = double(b.height) - a.height;
	const double abZ = double(b.z) - a.z;
	const double acX = double(c.x) - a.x;
	const double acHeight = double(c.height) - a.height;
	const double acZ = double(c.z) - a.z;
	const double normalX = abHeight * acZ - abZ * acHeight;
	const double normalHeight = abZ * acX - abX * acZ;
	const double normalZ = abX * acHeight - abHeight * acX;
	return normalX * normalX + normalHeight * normalHeight + normalZ * normalZ > 1.0e-20;
}

bool FitsNativeProjection(const TownSurroundingsTriangle &triangle, const TownSurroundingsCell &cell)
{
	// Kind 3 is drawn as a clipped background, outside the interleaved cell loop.
	if (cell.kind == 3)
		return true;
	constexpr double MaximumVertical = 16.0 * (TownSurroundingsNativeMaxExtraRows - 2);
	constexpr double MaximumHorizontal = 64.0 * (TownSurroundingsNativeMaxExtraColumns - 1);
	for (const auto &vertex : triangle.vertices) {
		// Relative to RenderTile's left X / bottom Y, not the cell center.
		const double x = 32.0 + 32.0 * (double(vertex.x) - cell.x - vertex.z + cell.z);
		const double y = 16.0 * (double(vertex.x) - cell.x + vertex.z - cell.z) - 32.0 * vertex.height;
		if (y < -MaximumVertical || y > MaximumVertical || x < -MaximumHorizontal || x > 64.0 + MaximumHorizontal)
			return false;
	}
	return true;
}

bool CanonicalUnderlayCell(const TownSurroundingsCell &cell,
    std::span<const TownSurroundingsTriangle> triangles)
{
	if (cell.x < -1 || cell.x > 114 || cell.z < -1 || cell.z > 114
	    || (cell.x >= 9 && cell.x <= 104 && cell.z >= 9 && cell.z <= 104)
	    || cell.triangleCount != 2 || triangles.size() != 2)
		return false;
	const float x = static_cast<float>(cell.x);
	const float z = static_cast<float>(cell.z);
	const std::array<TownSurroundingsPoint, 4> corners { {
		{ x - 1, UnderlayHeight, z - 1 }, { x, UnderlayHeight, z - 1 },
		{ x, UnderlayHeight, z }, { x - 1, UnderlayHeight, z },
	} };
	constexpr std::array<std::array<size_t, 3>, 2> indices { { { 0, 2, 1 }, { 0, 3, 2 } } };
	for (size_t face = 0; face < indices.size(); ++face) {
		for (size_t vertex = 0; vertex < indices[face].size(); ++vertex) {
			const auto &actual = triangles[face].vertices[vertex];
			const auto &expected = corners[indices[face][vertex]];
			if (actual.x != expected.x || actual.height != expected.height || actual.z != expected.z)
				return false;
		}
	}
	return true;
}

/**
 * Separating-axis test in XZ. A face whose projection merely touches the exact
 * rectangle boundary is allowed. Testing only its vertices would miss a face
 * crossing the city, and testing area alone would miss a vertical prop face.
 */
bool IntersectsInterior(const TownSurroundingsTriangle &triangle, double minimum, double maximum)
{
	const auto &vertices = triangle.vertices;
	const double minimumX = std::min({ double(vertices[0].x), double(vertices[1].x), double(vertices[2].x) });
	const double maximumX = std::max({ double(vertices[0].x), double(vertices[1].x), double(vertices[2].x) });
	const double minimumZ = std::min({ double(vertices[0].z), double(vertices[1].z), double(vertices[2].z) });
	const double maximumZ = std::max({ double(vertices[0].z), double(vertices[1].z), double(vertices[2].z) });
	if (maximumX <= minimum || minimumX >= maximum || maximumZ <= minimum || minimumZ >= maximum)
		return false;
	for (size_t i = 0; i < vertices.size(); ++i) {
		const auto &a = vertices[i];
		const auto &b = vertices[(i + 1) % vertices.size()];
		const double axisX = double(b.z) - a.z;
		const double axisZ = double(a.x) - b.x;
		if (axisX == 0 && axisZ == 0)
			continue;
		const double projection0 = axisX * vertices[0].x + axisZ * vertices[0].z;
		const double projection1 = axisX * vertices[1].x + axisZ * vertices[1].z;
		const double projection2 = axisX * vertices[2].x + axisZ * vertices[2].z;
		const double triangleMinimum = std::min({ projection0, projection1, projection2 });
		const double triangleMaximum = std::max({ projection0, projection1, projection2 });
		const double rectangleMinimum = axisX * (axisX >= 0 ? minimum : maximum)
		    + axisZ * (axisZ >= 0 ? minimum : maximum);
		const double rectangleMaximum = axisX * (axisX >= 0 ? maximum : minimum)
		    + axisZ * (axisZ >= 0 ? maximum : minimum);
		if (triangleMaximum <= rectangleMinimum || triangleMinimum >= rectangleMaximum)
			return false;
	}
	return true;
}

bool ParseScene(std::span<const std::byte> bytes, TownSurroundingsVisualScene &out)
{
	if (bytes.size() < HeaderBytes || bytes.size() > MaxBytes)
		return false;
	Reader reader { bytes };
	char magic[8];
	uint32_t version;
	uint32_t seed;
	uint32_t triangleCount;
	uint32_t cellCount;
	if (!reader.Copy(magic, sizeof(magic)) || std::memcmp(magic, "D3DENV1\0", sizeof(magic)) != 0
	    || !reader.U32(version) || version != 1 || !reader.U32(seed) || seed != SceneSeed
	    || !reader.U32(triangleCount) || triangleCount == 0 || triangleCount > MaxTriangles
	    || !reader.U32(cellCount) || cellCount == 0 || cellCount > MaxCells)
		return false;
	// Bounded counts make the size arithmetic safe on both 32-bit and 64-bit.
	const size_t encodedBytes = HeaderBytes + size_t(triangleCount) * TriangleBytes + size_t(cellCount) * CellBytes;
	if (encodedBytes != bytes.size())
		return false;
	TownSurroundingsVisualScene candidate;
	candidate.triangles.resize(triangleCount);
	candidate.cells.resize(cellCount);
	for (auto &triangle : candidate.triangles) {
		for (auto &vertex : triangle.vertices) {
			if (!reader.Float(vertex.x) || !reader.Float(vertex.height) || !reader.Float(vertex.z))
				return false;
		}
		if (!reader.Float(triangle.color.red) || !reader.Float(triangle.color.green)
		    || !reader.Float(triangle.color.blue) || !ValidTriangle(triangle))
			return false;
	}
	uint32_t nextTriangle = 0;
	uint32_t underlayCells = 0;
	std::array<bool, UnderlaySideCells * UnderlaySideCells> underlaySeen {};
	for (auto &cell : candidate.cells) {
		if (!reader.I32(cell.x) || !reader.I32(cell.z) || !reader.U32(cell.firstTriangle)
		    || !reader.U32(cell.triangleCount) || !reader.U32(cell.kind)
		    || cell.x < -20000 || cell.x > 20000 || cell.z < -20000 || cell.z > 20000 || cell.kind > 4
		    || cell.firstTriangle != nextTriangle || cell.triangleCount == 0
		    || cell.triangleCount > triangleCount - nextTriangle)
			return false;
		if (cell.kind == 4) {
			if (!CanonicalUnderlayCell(cell, std::span<const TownSurroundingsTriangle>(candidate.triangles).subspan(nextTriangle, cell.triangleCount)))
				return false;
			const size_t index = size_t(cell.z + 1) * UnderlaySideCells + size_t(cell.x + 1);
			if (underlaySeen[index])
				return false;
			underlaySeen[index] = true;
			++underlayCells;
			nextTriangle += cell.triangleCount;
			continue;
		}
		const double minimum = cell.kind == 2 ? FloorMinimum : ProtectedMinimum;
		const double maximum = cell.kind == 2 ? FloorMaximum : ProtectedMaximum;
		for (uint32_t i = 0; i < cell.triangleCount; ++i) {
			const auto &triangle = candidate.triangles[nextTriangle + i];
			if (IntersectsInterior(triangle, minimum, maximum) || !FitsNativeProjection(triangle, cell))
				return false;
		}
		nextTriangle += cell.triangleCount;
	}
	if (nextTriangle != triangleCount || reader.offset != bytes.size()
	    || underlayCells != UnderlayCellCount)
		return false;
	candidate.hasNativePaddingUnderlay = true;
	out = std::move(candidate);
	return true;
}

bool DecodeAuthenticatedScene(std::span<const std::byte> bytes, TownSurroundingsVisualScene &out)
{
	std::array<uint8_t, 32> expected;
	if (TownSurroundingsExpectedBytes < HeaderBytes || TownSurroundingsExpectedBytes > MaxBytes
	    || bytes.size() != TownSurroundingsExpectedBytes
	    || !HexToModHash(TownSurroundingsExpectedSha256, expected)
	    || ComputeBytesSha256(bytes) != expected)
		return false;
	TownSurroundingsVisualScene candidate;
	if (!ParseScene(bytes, candidate))
		return false;
	candidate.identity = TownSurroundingsExpectedSha256;
	candidate.valid = true;
	out = std::move(candidate);
	return true;
}

bool LoadScene(TownSurroundingsVisualScene &out)
{
	std::array<uint8_t, 32> expected;
	if (TownSurroundingsExpectedBytes < HeaderBytes || TownSurroundingsExpectedBytes > MaxBytes
	    || !HexToModHash(TownSurroundingsExpectedSha256, expected))
		return false;
	AssetRef ref = FindAsset(AssetPath);
	if (!ref.ok())
		return false;
	const size_t size = ref.size();
	if (size != TownSurroundingsExpectedBytes)
		return false;
	AssetHandle handle = OpenAsset(std::move(ref));
	if (!handle.ok())
		return false;
	std::vector<std::byte> bytes(size);
	if (!handle.read(bytes.data(), bytes.size()))
		return false;
	return DecodeAuthenticatedScene(bytes, out);
}

} // namespace

std::shared_ptr<const TownSurroundingsVisualScene> DecodeTownSurroundingsVisualSceneOwned(
    std::span<const std::byte> bytes, std::string &failure) noexcept
{
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
		failure.clear();
		auto candidate = std::make_shared<TownSurroundingsVisualScene>();
		if (!DecodeAuthenticatedScene(bytes, *candidate)) {
			failure = "surface-environment-authentication-or-decode-refused";
			return nullptr;
		}
		return candidate;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (...) {
		// Error reporting must not allocate during allocation-failure recovery.
		failure.clear();
		return nullptr;
	}
#endif
}

bool IsTownSurroundingsNativePaddingCell(int x, int z, uint16_t piece) noexcept
{
	// For native quad AABB [x-1.46875,x+.53125] x [z-1.46875,z+.53125],
	// these integer bounds are wholly within [-2,114]^2 and outside (8,104)^2.
	return piece == 426 && x >= 0 && x <= 111 && z >= 0 && z <= 111
	    && (x <= 7 || x >= 106 || z <= 7 || z >= 106);
}

bool AuthenticateTownSurroundingsNativePaddingArtwork(std::span<const uint8_t> pixels,
    std::span<const uint8_t> opacity, int width, int height) noexcept
{
	if (width != 64 || height != 32 || pixels.size() != 2048 || opacity.size() != 2048)
		return false;
	size_t covered = 0;
	for (size_t i = 0; i < opacity.size(); ++i) {
		if (opacity[i] > 1)
			return false;
		if (opacity[i] != 0) {
			if (pixels[i] != 127)
				return false;
			++covered;
		}
	}
	return covered == 1024;
}

const TownSurroundingsVisualScene *GetTownSurroundingsVisualScene() noexcept
{
	if (!Attempted) {
		Attempted = true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		try {
#endif
			TownSurroundingsVisualScene candidate;
			if (LoadScene(candidate))
				Current = std::move(candidate);
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		} catch (...) {
			// Nothing was published: preserve the empty fallback in both modes.
		}
#endif
	}
	return Current.valid ? &Current : nullptr;
}

void ClearTownSurroundingsVisualScene() noexcept
{
	Current = {};
	Attempted = false;
}

} // namespace devilution
