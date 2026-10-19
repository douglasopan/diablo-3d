#include "engine/render/town_ground_material.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

#include "engine/assets.hpp"
#include "mods/mod_identity.h"

namespace devilution {
namespace {
constexpr size_t ResourceBytes = 196624;
constexpr std::string_view ResourcePath = "d3d-materials/leafy-grass-lod256.d3dgrs";
constexpr std::string_view ResourceSha = "f51b4a994cc6f80df572aae7c47904bf6e394f1c1a94f21e91c0534012febfaf";
constexpr std::array<std::string_view, 4> SourcePixelsSha {
	"7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc",
	"d4e98dc797cd517554b02d7a039947c508fa83b222bfab86c4b7bfecff602555",
	"23ba7f26d932937ce1590dfc4a51c486616dd200631dbc8d0d14fdde36476ee2",
	"7e5794009ee7403eadad5f036a2611d91d90e5067be099d736230c60119b5c82"
};
constexpr std::string_view SourceOpacitySha = "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4";
// BEGIN_SCHEMA5_FALLBACK_BITMAP: filled offline from the exact approved patch.
constexpr std::array<uint64_t, 196> Schema5GrassFallback {
0x0000000000000000ULL,
0x0000000000000000ULL,
0x0000000000000000ULL,
0x0000000000000000ULL,
0x3000000C00030000ULL,
0x0000000000003FFFULL,
0x3FFF0000000C0000ULL,
0x000000000000000CULL,
0x000C3FFF00000000ULL,
0x0000000000000000ULL,
0x0000C0003FFF0000ULL,
0x0000000000000000ULL,
0x00000000C0003FFFULL,
0x3FFF000000000000ULL,
0x0D00000000000000ULL,
0x00003FFF00000000ULL,
0x00000F0000000000ULL,
0x000000003FFF0000ULL,
0x0000000000000040ULL,
0x0000000000003FFFULL,
0x3FFF000000000000ULL,
0x0000000000000000ULL,
0x00003FFF00000000ULL,
0x0000000000000000ULL,
0x000000003FFF0000ULL,
0x0000000000000000ULL,
0x0000000000003FFFULL,
0x3FFF000000000000ULL,
0x0000000000000000ULL,
0x00003FFF40000000ULL,
0x0000000000000000ULL,
0x000000003FFF0000ULL,
0x0000000000000000ULL,
0x0000000000003FFFULL,
0x3FFF000000000000ULL,
0x0000000000000000ULL,
0x00003FFF10000000ULL,
0x0000000000000000ULL,
0x000000003FFF0000ULL,
0x0000000000000000ULL,
0x0000000000003FFFULL,
0x3FFF000000000000ULL,
0x0000340000000000ULL,
0x00003FFF00300000ULL,
0x000000003C000000ULL,
0x000000343FFF0030ULL,
0x0001010000000000ULL,
0x00000000003C3FFFULL,
0x3FFF000000000000ULL,
0x000000000000001CULL,
0x000C3FFF30000000ULL,
0x0000000000000000ULL,
0x000000043FFF3000ULL,
0x000000000C000000ULL,
0x0000000000003FFFULL,
0x3FFF000000000C00ULL,
0x0000001000000000ULL,
0x00003FFF01000000ULL,
0x0000000000000000ULL,
0x0000400C3FFF0000ULL,
0x0000030000000000ULL,
0x00000000000C3FFFULL,
0x3FFF000000000000ULL,
0x0D0000000000003CULL,
0x003C3FFF000D0000ULL,
0x00000F0000000000ULL,
0x000000DC3FFF000FULL,
0x0000030000000000ULL,
0x00000000000C3FFFULL,
0x3FFF000003000000ULL,
0x00000000DC0001FCULL,
0x00FC3FFF00000000ULL,
0x000000000000F000ULL,
0x000003F43FFF0000ULL,
0x0000000000000000ULL,
0x0000000003FC3FFFULL,
0x3FFF000000000000ULL,
0x0000000000000FD0ULL,
0x0FF03FFF00000000ULL,
0x0000000000000000ULL,
0x000003DC3FFF0000ULL,
0x0000000000000000ULL,
0x0000000003FC3FFFULL,
0x3FFF000000000000ULL,
0xC000000000000D5CULL,
0x0F0C3FFF000D0000ULL,
0x0000C00000000000ULL,
0x000000F43FFF000FULL,
0x0000000000000000ULL,
0x0000000000FC3FFFULL,
0x3FFF000000000000ULL,
0x00100000000000FCULL,
0x00F03FFF0000C000ULL,
0x0000000000000000ULL,
0x000000FC3FFF0000ULL,
0x0000000000000340ULL,
0x03C0000000CC3FFFULL,
0x3FFF000000000000ULL,
0x0000000000000374ULL,
0x033C3FFF00000000ULL,
0x0000000000000000ULL,
0x000007CC3FFF0000ULL,
0x0000000000000000ULL,
0x0000000003CC3FFFULL,
0x3FFF000000000000ULL,
0x00000000000C03FCULL,
0x03FC3FFF00000000ULL,
0x0000000000000000ULL,
0x000D355C3FFF0000ULL,
0x0000000000000000ULL,
0x0000000F3C0C3FFFULL,
0x3FFF000000000000ULL,
0x0000000000347DF4ULL,
0x3F303FFF00000000ULL,
0x000000000000003CULL,
0x00001FDC3FFF0000ULL,
0x0000000000000000ULL,
0x000000003CFC3FFFULL,
0x3FFF000000000000ULL,
0x00000000DC003F74ULL,
0x033C3FFF00000000ULL,
0x000000000000F000ULL,
0xF0C01CFC3FFF0000ULL,
0x0000000000C00001ULL,
0x0000C0C03CFC3FFFULL,
0x3FFF0000000000C0ULL,
0x00000C00C00033F4ULL,
0x30C03FFF00400000ULL,
0x000000000C00C000ULL,
0x43FDFF743FFF0000ULL,
0x000000040000000FULL,
0x000C00FFFF3C3FFFULL,
0x3FFF000000000000ULL,
0x0000000000D777FCULL,
0x33FC3FFF00000000ULL,
0x00000000000000F3ULL,
0x0FF40D743FFF0000ULL,
0x0000000000000003ULL,
0x00030FFC0F3C3FFFULL,
0x3FFF000000000000ULL,
0x000000000F7C0FDCULL,
0x0FCC3FFF00000000ULL,
0x0000000000000FF0ULL,
0x337403DC3FFF0000ULL,
0x0301000000000000ULL,
0x0000333C03F03FFFULL,
0x3FFF000000000000ULL,
0xC0707FF43FDF4FCCULL,
0xCCCC3FFF0000F00CULL,
0xF000C0303FF03FFFULL,
0xF5FDFD3C3FFF0000ULL,
0x0000FC0FFFFFFFF4ULL,
0xFF3CFCCFFF3C3FFFULL,
0x3FFF0000FC0FFFFFULL,
0x3F7F7FFC1D77F7F4ULL,
0xC3F03FFF00D0F7FFULL,
0xF33F3FFFFFCC0F33ULL,
0x3CDDCD3C3FFF00F0ULL,
0x0070FF7773FFF5D0ULL,
0xFCF03CFCCF0C3FFFULL,
0x3FFF0030CFF3C3F3ULL,
0x7DDFDC743FCD3CF4ULL,
0x3CFC3FFF0F41F5F7ULL,
0x3CFF3FCFF03C3CCFULL,
0x3477F5FC3FFF0FC0ULL,
0x4C43777CD7DFDF7CULL,
0xFFF03C3FFC3C3FFFULL,
0x3FFF0003C3F0F3FFULL,
0xFFFFFFFFFFFFFFFCULL,
0xFFFC3FFFFFFFFFFFULL,
0xFFFFFFFFFFFFFFFFULL,
0xFFFFFFFC3FFFFFFFULL,
0xFFFFFFFFFFFFFFFFULL,
0xFFFFFFFFFFFC3FFFULL,
0x3FFFFFFFFFFFFFFFULL,
0xFFFFFFFFFFFFFFFCULL,
0xFFFC3FFFFFFFFFFFULL,
0xFFFFFFFFFFFFFFFFULL,
0xFFFFFFFC3FFFFFFFULL,
0xFFFFFFFFFFFFFFFFULL,
0xFFFFFFFFFFFC3FFFULL,
0x3FFFFFFFFFFFFFFFULL,
0xFFFFFFFFFFFFFFFCULL,
0xFFFC3FFFFFFFFFFFULL,
0xFFFFFFFFFFFFFFFFULL,
0xFFFFFFFC3FFFFFFFULL,
0xFFFFFFFFFFFFFFFFULL,
0xFFFFFFFFFFFC3FFFULL,
0x3FFFFFFFFFFFFFFFULL,
0xFFFFFFFFFFFFFFFCULL,
0xFFFC3FFFFFFFFFFFULL,
0xFFFFFFFFFFFFFFFFULL,
0x000000003FFFFFFFULL,
0x0000000000000000ULL,
0x0000000000000000ULL,
0x0000000000000000ULL
};
// END_SCHEMA5_FALLBACK_BITMAP

bool SameLight(const TownLightingConfig &a, const TownLightingConfig &b)
{
	return a.ambient.red == b.ambient.red && a.ambient.green == b.ambient.green && a.ambient.blue == b.ambient.blue
	    && a.directional.red == b.directional.red && a.directional.green == b.directional.green && a.directional.blue == b.directional.blue
	    && a.toLight.x == b.toLight.x && a.toLight.height == b.toLight.height && a.toLight.z == b.toLight.z
	    && a.directionalIntensity == b.directionalIntensity;
}

std::string Hash(std::span<const uint8_t> bytes)
{
	return ModHashToHex(ComputeBytesSha256(std::as_bytes(bytes)));
}
} // namespace

bool PrepareTownGroundMaterial(TownGroundMaterialCache &cache,
    std::span<const uint32_t> palette, const TownLightingConfig &light)
{
	if (palette.size() != 256) return false;
	if (cache.preparationAttempted && SameLight(cache.light, light) && std::equal(palette.begin(), palette.end(), cache.palette.begin()))
		return cache.material != nullptr;
	cache.preparationAttempted = true;
	std::copy(palette.begin(), palette.end(), cache.palette.begin());
	cache.light = light;
	try {
		if (!cache.resourceAttempted) {
			cache.resourceAttempted = true;
			AssetRef ref = FindAsset(ResourcePath);
			if (!ref.ok() || ref.size() != ResourceBytes) { cache.failure = "grass-resource-missing-or-size"; return false; }
			std::vector<uint8_t> bytes(ResourceBytes);
			AssetHandle handle = OpenAsset(std::move(ref));
			if (!handle.ok() || !handle.read(bytes.data(), bytes.size()) || Hash(bytes) != ResourceSha) {
				cache.failure = "grass-resource-read-or-hash"; return false;
			}
			constexpr std::array<uint8_t, 16> Header { 'D','3','D','G','R','S','1',0, 0,1,0,0, 0,1,0,0 };
			if (!std::equal(Header.begin(), Header.end(), bytes.begin())) { cache.failure = "grass-resource-header"; return false; }
			cache.rgb.assign(bytes.begin() + Header.size(), bytes.end());
		}
		if (cache.rgb.empty()) return false;
		auto prepared = std::make_shared<PlayerActorMaterial>();
		if (!PreparePlayerActorMaterial({ TownGrassLodSide, TownGrassLodSide, cache.rgb, palette, {},
		        PlayerActorMaterialWorld::TownDirectional, light }, *prepared, cache.failure)) {
			cache.material.reset(); return false;
		}
		const size_t lutBytes = prepared->indexedLightLut.size() + prepared->rgbaLightLut.size() * sizeof(uint32_t);
		if (prepared->sourceColors.size() != 41182 || lutBytes > 16U * 1024U * 1024U) {
			cache.material.reset(); cache.failure = "grass-shared-LUT-budget"; return false;
		}
		std::copy(palette.begin(), palette.end(), cache.palette.begin());
		cache.light = light;
		cache.material = std::move(prepared);
		cache.failure.clear();
		return true;
	} catch (const std::bad_alloc &) {
		cache.material.reset(); cache.failure = "grass-allocation"; return false;
	}
}

bool TownGroundMaterialAllowsCell(Point tile, uint16_t piece, bool fallback)
{
	if (tile.x < 2 || tile.y < 2 || tile.x > 109 || tile.y > 109 || piece > 3) return false;
	if (!fallback) return true;
	const unsigned bit = static_cast<unsigned>(tile.y * 112 + tile.x);
	return piece == 0 && (Schema5GrassFallback[bit / 64] & (uint64_t { 1 } << (bit % 64))) != 0;
}

bool TownGroundMaterialSourceMatches(uint16_t piece, int width, int height,
    bool repeat, std::span<const uint8_t> pixels, std::span<const uint8_t> opacity)
{
	return piece < SourcePixelsSha.size() && width == 64 && height == 32 && !repeat
	    && pixels.size() == 2048 && opacity.size() == 2048
	    && Hash(pixels) == SourcePixelsSha[piece] && Hash(opacity) == SourceOpacitySha;
}

bool SampleTownGroundMaterial(const PlayerActorMaterial &material,
    float worldX, float worldZ, uint32_t &code)
{
	if (!std::isfinite(worldX) || !std::isfinite(worldZ) || (material.width != TownGrassLodSide && material.width != 64)
	    || material.height != material.width || material.texelCodes.size() != size_t(material.width) * material.height)
		return false;
	float u = worldX * TownGrassWorldUvScale, v = worldZ * TownGrassWorldUvScale;
	u -= std::floor(u); v -= std::floor(v);
	const auto x = std::min(static_cast<unsigned>(u * material.width), material.width - 1);
	const auto z = std::min(static_cast<unsigned>(v * material.height), material.height - 1);
	code = material.texelCodes[z * material.width + x];
	return true;
}

namespace {
uint32_t RoadHash(int x, int z, uint32_t seed)
{
	uint32_t h = uint32_t(x) * 0x9E3779B1U ^ uint32_t(z) * 0x85EBCA77U ^ seed;
	h ^= h >> 16; h *= 0x7FEB352DU; h ^= h >> 15; h *= 0x846CA68BU; return h ^ (h >> 16);
}
float RoadVisualNoise(float x, float z, uint32_t seed)
{
	x *= 0.75F; z *= 0.75F;
	const int ix = int(std::floor(x)), iz = int(std::floor(z));
	float u = x - ix, v = z - iz;
	u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
	const auto at = [&](int a, int b) { return float(RoadHash(a, b, seed) & 65535) / 32767.5F - 1; };
	const float a = at(ix, iz), b = at(ix + 1, iz), c = at(ix, iz + 1), d = at(ix + 1, iz + 1);
	return (a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v;
}
} // namespace

float SampleTownRoadBand(const TownRoadBand &band, float worldX, float worldZ)
{
	if (band.count < 2 || band.count > band.points.size() || !std::isfinite(worldX) || !std::isfinite(worldZ)
	    || std::abs(worldX) > 1000000 || std::abs(worldZ) > 1000000
	    || !std::isfinite(band.halfWidth) || band.halfWidth < 0.25F || band.halfWidth > 4
	    || !std::isfinite(band.feather) || band.feather < 0 || band.feather > 1
	    || !std::isfinite(band.irregularity) || band.irregularity < 0 || band.irregularity > 0.25F)
		return 0;
	for (const auto &plane : band.clipPlanes) if (plane.enabled) {
		if (!std::isfinite(plane.normalX) || !std::isfinite(plane.normalZ) || !std::isfinite(plane.offset)
		    || std::abs(plane.normalX) > 1 || std::abs(plane.normalZ) > 1 || std::abs(plane.offset) > 512
		    || plane.normalX * plane.normalX + plane.normalZ * plane.normalZ < 0.000001F
		    || worldX * plane.normalX + worldZ * plane.normalZ < plane.offset) return 0;
	}
	float nearest = 10000000;
	for (unsigned i = 1; i < band.count; ++i) {
		const auto a = band.points[i - 1], b = band.points[i];
		if (!std::isfinite(a.x) || !std::isfinite(a.z) || !std::isfinite(b.x) || !std::isfinite(b.z)
		    || std::abs(a.x) > 512 || std::abs(a.z) > 512 || std::abs(b.x) > 512 || std::abs(b.z) > 512) return 0;
		const float dx = b.x - a.x, dz = b.z - a.z, squared = dx * dx + dz * dz;
		const float t = squared > 0 ? std::clamp(((worldX - a.x) * dx + (worldZ - a.z) * dz) / squared, 0.0F, 1.0F) : 0;
		const float x = worldX - a.x - t * dx, z = worldZ - a.z - t * dz;
		nearest = std::min(nearest, std::sqrt(x * x + z * z));
	}
	if (band.feather == 0) return nearest <= band.halfWidth ? 1 : 0;
	float t = std::clamp((band.halfWidth + band.feather - nearest) / band.feather, 0.0F, 1.0F);
	// Irregularity changes only the feather profile. The measured fully painted
	// core and maximum halfWidth+feather envelope never expand or contract.
	t = std::clamp(t + (band.irregularity / band.feather) * RoadVisualNoise(worldX, worldZ, band.visualSeed) * t * (1 - t), 0.0F, 1.0F);
	return t * t * (3 - 2 * t);
}

TownRoadCoverageStatus PrepareTownRoadCellCoverage(const TownRoadBand &band, Point tile, bool routeCellAdmitted,
    std::span<const uint8_t> nativeOpacity, std::vector<uint8_t> &mask)
{
	mask.clear();
	if (!routeCellAdmitted || tile.x < 0 || tile.y < 0 || tile.x > 111 || tile.y > 111
	    || nativeOpacity.size() != 64 * 32 || band.count < 2 || band.count > band.points.size()) return TownRoadCoverageStatus::Invalid;
	// SampleTownRoadBand returns zero for both invalid bands and clipped pixels.
	// Validate its immutable parameters here once so only valid geometry can be Empty.
	if (!std::isfinite(band.halfWidth) || band.halfWidth < .25F || band.halfWidth > 4
	    || !std::isfinite(band.feather) || band.feather < 0 || band.feather > 1
	    || !std::isfinite(band.irregularity) || band.irregularity < 0 || band.irregularity > .25F)
		return TownRoadCoverageStatus::Invalid;
	for (unsigned i = 0; i < band.count; ++i)
		if (!std::isfinite(band.points[i].x) || !std::isfinite(band.points[i].z)
		    || std::abs(band.points[i].x) > 512 || std::abs(band.points[i].z) > 512)
			return TownRoadCoverageStatus::Invalid;
	for (const auto &plane : band.clipPlanes) if (plane.enabled)
		if (!std::isfinite(plane.normalX) || !std::isfinite(plane.normalZ) || !std::isfinite(plane.offset)
		    || std::abs(plane.normalX) > 1 || std::abs(plane.normalZ) > 1 || std::abs(plane.offset) > 512
		    || plane.normalX * plane.normalX + plane.normalZ * plane.normalZ < .000001F)
			return TownRoadCoverageStatus::Invalid;
	try {
		std::vector<uint8_t> prepared(64 * 32, 0);
		bool any = false;
		for (int y = 0; y < 32; ++y) for (int x = 0; x < 64; ++x) {
			const size_t i = size_t(y * 64 + x);
			if (nativeOpacity[i] == 0) continue;
			const float u = (x + 0.5F) / 64, v = (y + 0.5F) / 32;
			const float wx = tile.x - 1.46875F + u + v, wz = tile.y - 0.46875F - u + v;
			const float amount = SampleTownRoadBand(band, wx, wz);
			const uint32_t value = RoadHash(int(std::floor(wx * 32)), int(std::floor(wz * 32)), band.visualSeed ^ 0x45444745U);
			if (amount > float(value & 65535) / 65536.0F) { prepared[i] = 1; any = true; }
		}
		if (!any) return TownRoadCoverageStatus::Empty;
		mask = std::move(prepared); return TownRoadCoverageStatus::Ready;
	} catch (const std::bad_alloc &) { return TownRoadCoverageStatus::Invalid; }
}

bool BuildTownRoadCellCoverage(const TownRoadBand &band, Point tile, bool routeCellAdmitted,
    std::span<const uint8_t> nativeOpacity, std::vector<uint8_t> &mask)
{
 return PrepareTownRoadCellCoverage(band, tile, routeCellAdmitted, nativeOpacity, mask) == TownRoadCoverageStatus::Ready;
}

bool PrepareTownRoadMaterial(std::span<const uint32_t> palette,
    const TownLightingConfig &light, PlayerActorMaterial &output, std::string &failure)
{
	// A periodic small color library: at most64 earthy colors and4096 texels.
	// Quantization is explicit authored palette design, not copied native art.
	std::array<uint8_t, 64 * 64 * 3> rgb;
	for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) {
		const int coarse = int(RoadHash(x >> 3, z >> 3, 0x534F494CU) & 15) - 8;
		const int grain = int(RoadHash(x, z, 0x47524954U) & 31) - 16;
		const int value = std::clamp(32 + coarse + grain / 2, 0, 63);
		const size_t i = size_t(z * 64 + x) * 3;
		rgb[i] = uint8_t(68 + value); rgb[i + 1] = uint8_t(53 + value * 3 / 4); rgb[i + 2] = uint8_t(42 + value / 2);
	}
	PlayerActorMaterial prepared;
	if (!PreparePlayerActorMaterial({ 64, 64, rgb, palette, {}, PlayerActorMaterialWorld::TownDirectional, light }, prepared, failure)) return false;
	if (prepared.sourceColors.size() > 64) { failure = "road-authored-palette-bound"; return false; }
	output = std::move(prepared); return true;
}

bool PrepareTownRoadMaterial(TownGroundMaterialCache &cache,
    std::span<const uint32_t> palette, const TownLightingConfig &light)
{
	if (palette.size() != 256) return false;
	if (cache.preparationAttempted && SameLight(cache.light, light) && std::equal(palette.begin(), palette.end(), cache.palette.begin()))
		return cache.material != nullptr;
	cache.preparationAttempted = true; cache.light = light;
	std::copy(palette.begin(), palette.end(), cache.palette.begin());
	try {
		auto prepared = std::make_shared<PlayerActorMaterial>();
		if (!PrepareTownRoadMaterial(palette, light, *prepared, cache.failure)) { cache.material.reset(); return false; }
		cache.material = std::move(prepared); return true;
	} catch (const std::bad_alloc &) { cache.material.reset(); cache.failure = "road-material-allocation"; return false; }
}

// BEGIN_MEASURED_ENTRANCE_ROAD_DATA: sealed JSON SHA256 2d0771d19623cf7b47eb3dee3b8e7858c1a334f2ae69d97e83cdf8aee282f158
namespace {
struct EntranceRoadSource { unsigned cell; uint16_t piece, special; bool visualGuard; std::string_view pixelsSha, opacitySha; };
constexpr std::array<EntranceRoadSource, 152> EntranceRoadSources {{
{ 8124, 1123, 0, false, "28083cbee7fc65ff5dc1075ecf8ee5f5d7ebdee0ab778021478cc132360628cd", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8125, 1124, 0, false, "0a509a68a930b16f76d9269ce9c58dd2993228892b8126211d639622799b2843", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8126, 1127, 0, false, "952f3fad72eb5e846569ffd2ef0eafe1dcc112ce9793601b446b682f45787518", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8236, 1125, 0, false, "c88f8730fbfaa53f9e73aa8d510c9159ed6b27c746ea7845ca49c84275178f76", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8237, 1126, 0, false, "35ce26144999a7f6a9ac635e8dc6421599e6c32b3926d3dd28204c5d88cb1b5a", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8238, 1129, 0, false, "b85e3ac7724f030ed180243f16b663760d25f62cdb8e79999575a92204d788f8", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8348, 44, 0, false, "07d174f2d8324d049aed87891bb33af0f25f5a111a8105cad1f14372327f825b", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8349, 45, 0, false, "7860949c3943ece8b4d5d9700819344fc93fc2e1e8ea0284a5f70a864be014c1", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8350, 68, 0, false, "3724614fa4effaa815ce5eef2abfc1fb3e5361d45455e58762b5cb4b880af457", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8459, 47, 0, false, "e647fca8e731f96f830af483cadaa03afff7921e39d57617e7bb17e46d0f5900", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8460, 46, 0, false, "db1c875beaf53d315ebd76ccbc25d95103254f536e68589d48c0c2d7936218c5", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8461, 47, 0, false, "e647fca8e731f96f830af483cadaa03afff7921e39d57617e7bb17e46d0f5900", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8462, 70, 0, false, "af32f31e39a8dc20bfb422a35218eccdc84c2b1901902558bbf3b180bca1f415", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8571, 25, 0, false, "aac376ce1395ecdecbbe5030d93bbeed19b1fe41c852228bcb5a8979b2de001b", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8572, 110, 0, false, "ac6631befa968c0a56baed536aa643a79643166feb7a09052b46f22bc525ce2c", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8573, 111, 0, false, "156e3a736998758abbe69cefbde7ffc4111ffdf62d76f32fcb5efc55260f424e", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8683, 27, 0, false, "d43dcbb85b960bd530d6107427a848c864a9e35529d0e5e6124c8c203d20c889", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8684, 22, 0, false, "32d4369c2b15da3636331e666eb277e769aff0cd7d14afb942a9bb3ff58ca1ca", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8685, 112, 0, false, "5b1f5468e364bef4410b379ac9302f207fd33c3a072c14d1c783ff4cbac1f084", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8795, 25, 0, false, "aac376ce1395ecdecbbe5030d93bbeed19b1fe41c852228bcb5a8979b2de001b", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8796, 20, 0, false, "20e4db69460f2f3581f5450eb156dd6af120c2c5ebcd101f69564d22d3fe1ee7", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8797, 21, 0, false, "80724c9b56217f09d98cfc23cb4997129e1b76f3ecd734277013fe5b01db84c9", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8906, 26, 0, false, "4b0032c327686a17b2d67b9a9ba322321af8004f7fa0f012469a8d7574a674ff", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8907, 27, 0, false, "d43dcbb85b960bd530d6107427a848c864a9e35529d0e5e6124c8c203d20c889", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8908, 22, 0, false, "32d4369c2b15da3636331e666eb277e769aff0cd7d14afb942a9bb3ff58ca1ca", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 8909, 23, 0, false, "717bc8bf679003e311533b28e94d2d9bb591ef2baa3c9fe08885561351044725", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9016, 500, 0, false, "b65b5f73f9fb7eb1eb730d5a05e1d14d88a9508107257cfface6aa123a58b684", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9017, 501, 0, false, "3870fe08fc725cb256bb09675d798d7be7c9b6357588d08f6e16872363d5392a", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9018, 492, 0, false, "fffcc3c3491faae018e654037d24ccf6f45c720732b8847ae95bc751ca04f7ce", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9019, 493, 0, false, "e8a2925392964dd87b9ddbbff5d041ef622790c4c8e15307729df2ecca96591f", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9020, 16, 0, false, "3d3557cb45249fbc4161e2fa34cd9967c3725a7cea2794e42134fede24a00640", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9021, 17, 0, false, "b96d310a2e99d566bf067bb76ff1fde3e17b6454679d4d067763ffea19f544f1", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9126, 510, 0, false, "81153baec248c7bcfc85935661fe5f0dd578d2e696e20716ab3d9095df3913ad", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9127, 511, 0, false, "cf1f725e16c41438586da73f42c5021c9a765cbafc99a12b3e98d0cca4636982", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9128, 502, 0, false, "26b3d34609e0c5babc794dd43d4b03f1444c943a3c64aa4fb9def1629a55808a", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9129, 503, 0, false, "c7e4f4cf14678cff3d7cc19ea47742e2a87736b3dd3dee1bde39bec032e738aa", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9130, 494, 0, false, "be55afc62f7afc9dccae5d578abc06d135ec0765cb89f2cc567577458dd6b35e", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9131, 495, 0, false, "4e72fb54c0f5bfbfdc15d6ec8bb1cebb2165699f2e20ef037e2bbb0c06dedecf", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9132, 18, 0, false, "04702f174749999458aa07c05af6266be04c52b83e4b3a62db52cdb50532c656", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9238, 104, 0, false, "a88cb85d41dac2bb44f63126f694d6cbc88e5a652ef2cac165af1c793a016383", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9239, 105, 0, false, "d4977ecc84b07ae208917b980a5af6f5824c66d68ebeaf999ecb52c56b554861", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9240, 496, 0, false, "ddcb44577f2ee72581bb63f14f497ec2d21293a2ffb91ae5b9952ff5acd741fe", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9241, 497, 0, false, "ee0e58bdbea07622ced5536337ab84d81f0bc9619fb2b9ac7902f1947fbee04d", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9242, 488, 0, false, "eb52986ec168e1b9939ec4938080780c8e8ff1729fbbf142d61eeb56e1bf4e4b", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9243, 489, 0, false, "711b2de6ca17d9391478b418299aa8ddcbfe810d8fc22cb2dbc8ce8b45fd8010", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9350, 106, 0, false, "140a1b3670fa6cfc6f02de1e101fa771eb8c4ca2838582a3d3dbee730dc5e1d9", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9351, 107, 0, false, "96f11c2497ac034c9e4dff0dbd1f504c875c9683e145b6eb58339e9fef8dd9e1", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9352, 498, 0, false, "c607a33e036534547a95e51e5c44e0b166bd2a0ce9ee6523063b5ba064893360", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9353, 499, 0, false, "5a55067cfa37101a277d7b25d3e239fe3a50743c0dc954f25ee6ae664b831326", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9461, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9462, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9463, 21, 0, false, "80724c9b56217f09d98cfc23cb4997129e1b76f3ecd734277013fe5b01db84c9", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9573, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9574, 22, 0, false, "32d4369c2b15da3636331e666eb277e769aff0cd7d14afb942a9bb3ff58ca1ca", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9575, 23, 0, false, "717bc8bf679003e311533b28e94d2d9bb591ef2baa3c9fe08885561351044725", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9685, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9686, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9687, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9688, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9797, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9798, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9799, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9800, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9909, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9910, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9911, 25, 0, false, "aac376ce1395ecdecbbe5030d93bbeed19b1fe41c852228bcb5a8979b2de001b", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 9912, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10022, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10023, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10024, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10134, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10135, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10136, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10246, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10247, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10248, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10358, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10359, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10360, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10470, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10471, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10472, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10582, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10583, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10584, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10694, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10695, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10696, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10806, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10807, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10808, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10809, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10918, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10919, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10920, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 10921, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11030, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11031, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11032, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11033, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11142, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11143, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11144, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11145, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11254, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11255, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11256, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11257, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11366, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11367, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11368, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11369, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11478, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11479, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11480, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11481, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11590, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11591, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11592, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11593, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11702, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11703, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11704, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11705, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11814, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11815, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11816, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11817, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11926, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11927, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11928, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 11929, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12038, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12039, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12040, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12041, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12150, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12151, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12152, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12153, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12262, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12263, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12264, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12265, 0, 0, false, "7f5d561a79e96908365d384fb07f2481197081ddb9b010b4c7ec5fd6d8c09ccc", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12374, 426, 0, true, "8cf337a05c4fb25cf41e183e5ed3c8db73f4c5c45d52d7ec18577f5dfbc21bd4", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12375, 426, 0, true, "8cf337a05c4fb25cf41e183e5ed3c8db73f4c5c45d52d7ec18577f5dfbc21bd4", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12376, 426, 0, true, "8cf337a05c4fb25cf41e183e5ed3c8db73f4c5c45d52d7ec18577f5dfbc21bd4", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12377, 426, 0, true, "8cf337a05c4fb25cf41e183e5ed3c8db73f4c5c45d52d7ec18577f5dfbc21bd4", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12486, 426, 0, true, "8cf337a05c4fb25cf41e183e5ed3c8db73f4c5c45d52d7ec18577f5dfbc21bd4", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12487, 426, 0, true, "8cf337a05c4fb25cf41e183e5ed3c8db73f4c5c45d52d7ec18577f5dfbc21bd4", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12488, 426, 0, true, "8cf337a05c4fb25cf41e183e5ed3c8db73f4c5c45d52d7ec18577f5dfbc21bd4", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
{ 12489, 426, 0, true, "8cf337a05c4fb25cf41e183e5ed3c8db73f4c5c45d52d7ec18577f5dfbc21bd4", "e34415a29200b246fcdf9d0b338aa15fe115df883c26aeb6ef6071ed46b45ea4" },
}};
const EntranceRoadSource *EntranceRoadSourceAt(Point tile)
{
	if (tile.x < 0 || tile.y < 0 || tile.x > 111 || tile.y > 111) return nullptr;
	const unsigned key = unsigned(tile.y * 112 + tile.x);
	const auto it = std::lower_bound(EntranceRoadSources.begin(), EntranceRoadSources.end(), key, [](const EntranceRoadSource &a, unsigned b) { return a.cell < b; });
	return it != EntranceRoadSources.end() && it->cell == key ? &*it : nullptr;
}
} // namespace
const TownRoadBand &GetTownEntranceRoadBand()
{
	static const TownRoadBand Band = [] {
		TownRoadBand b;
		b.count = 23; b.halfWidth = 1.0F; b.feather = 0.12F; b.irregularity = 0.08F;
		b.points[0] = { 60.5F, 71.81325023F };
		b.points[1] = { 60.5F, 72.10993767F };
		b.points[2] = { 60.5F, 72.70331256F };
		b.points[3] = { 60.25F, 73.75F };
		b.points[4] = { 59.75F, 75.25F };
		b.points[5] = { 59.5F, 76.5F };
		b.points[6] = { 59.5F, 77.5F };
		b.points[7] = { 59.25F, 78.375F };
		b.points[8] = { 58.75F, 79.125F };
		b.points[9] = { 58.0F, 79.825F };
		b.points[10] = { 57.0F, 80.475F };
		b.points[11] = { 55.875F, 81.1F };
		b.points[12] = { 54.625F, 81.7F };
		b.points[13] = { 54.0F, 82.0F };
		b.points[14] = { 54.0F, 83.125F };
		b.points[15] = { 54.0F, 85.375F };
		b.points[16] = { 54.05F, 87.125F };
		b.points[17] = { 54.15F, 88.375F };
		b.points[18] = { 54.4F, 90.0F };
		b.points[19] = { 54.8F, 92.0F };
		b.points[20] = { 55.0F, 97.5F };
		b.points[21] = { 55.0F, 106.5F };
		b.points[22] = { 55.0F, 111.0F };
		b.clipPlanes[0] = { true, 0, 1, 71.81325023F };
		b.clipPlanes[1] = { true, 0, -1, -111.0F };
		return b;
	}();
	return Band;
}
bool TownEntranceRoadCellSource(Point tile, uint16_t nativePiece, uint16_t special)
{
	const auto *cell = EntranceRoadSourceAt(tile);
	return cell != nullptr && cell->piece == nativePiece && cell->special == special;
}
bool TownEntranceRoadUsesVisualDonor(Point tile)
{
	const auto *cell = EntranceRoadSourceAt(tile); return cell != nullptr && cell->visualGuard;
}
bool TownEntranceRoadTextureMatches(Point tile, int width, int height, bool repeat, std::span<const uint8_t> pixels, std::span<const uint8_t> opacity)
{
	const auto *cell = EntranceRoadSourceAt(tile);
	return cell != nullptr && width == 64 && height == 32 && !repeat && pixels.size() == 2048 && opacity.size() == 2048
	    && Hash(pixels) == cell->pixelsSha && Hash(opacity) == cell->opacitySha;
}
// END_MEASURED_ENTRANCE_ROAD_DATA

} // namespace devilution
