#include "engine/render/town_border_environment.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "engine/assets.hpp"
#include "engine/render/town_border_environment_identity.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/town_border_collision.hpp"
#include "mods/mod_identity.h"
#include "multi.h"
#include "player.h"

#if !defined(__cpp_exceptions) && !defined(_CPPUNWIND)
#error Town border admission requires explicit exception unwinding; include CMAKE-AFTER-NINE.cmake.
#endif

namespace devilution {
namespace {

constexpr size_t HeaderBytes = 96;
constexpr size_t TriangleBytes = 48;
constexpr size_t RangeBytes = 20;
constexpr size_t SupportBytes = 12;
constexpr size_t MaxBytes = 16 * 1024 * 1024;
constexpr uint32_t MaxTriangles = 100000;
constexpr uint32_t MaxSupportPins = 2048;
constexpr size_t TownSide = 112;
constexpr uint32_t AuthoredProperties = 5; // Solid | BlockMissile, no fake shadow.
static_assert(MAXDUNX == TownSide && MAXDUNY == TownSide);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

struct SupportPin {
	uint16_t x = 0, z = 0, piece = 0;
	int16_t special = 0;
	uint32_t properties = 0;
};
struct BorderCandidate {
	TownSurroundingsVisualScene geometry;
	std::vector<SupportPin> support;
	std::array<uint8_t, TownSide * TownSide> properties {};
	std::array<uint16_t, TownSide * TownSide> expectedPieces {};
	std::vector<TownSurroundingsTriangle> cameraSurfaces;
};

BorderCandidate Border;
TownSurroundingsVisualScene Composite;
bool MapAdmitted = false;
bool MapStaged = false;
bool NativeTownMapPrepared = false;
uint64_t NativeMapEpoch = 0;
TownBorderVisualStatus NativeVisualAdmission = TownBorderVisualStatus::NotPrepared;

struct Reader {
	std::span<const std::byte> bytes;
	size_t offset = 0;
	bool Copy(void *out, size_t count)
	{
		if (offset > bytes.size() || count > bytes.size() - offset) return false;
		std::memcpy(out, bytes.data() + offset, count); offset += count; return true;
	}
	bool U16(uint16_t &out)
	{
		if (offset > bytes.size() || 2 > bytes.size() - offset) return false;
		out = std::to_integer<uint16_t>(bytes[offset]) | (std::to_integer<uint16_t>(bytes[offset + 1]) << 8);
		offset += 2; return true;
	}
	bool U32(uint32_t &out)
	{
		if (offset > bytes.size() || 4 > bytes.size() - offset) return false;
		out = std::to_integer<uint32_t>(bytes[offset]) | (std::to_integer<uint32_t>(bytes[offset + 1]) << 8)
		    | (std::to_integer<uint32_t>(bytes[offset + 2]) << 16) | (std::to_integer<uint32_t>(bytes[offset + 3]) << 24);
		offset += 4; return true;
	}
	bool I32(int32_t &out) { uint32_t v; if (!U32(v)) return false; out = std::bit_cast<int32_t>(v); return true; }
	bool Float(float &out) { uint32_t v; if (!U32(v)) return false; out = std::bit_cast<float>(v); return true; }
};

bool ValidTriangle(const TownSurroundingsTriangle &t)
{
	float minX = 108, minZ = 108, maxX = 2, maxZ = 2;
	for (const auto &p : t.vertices) {
		if (!std::isfinite(p.x) || !std::isfinite(p.height) || !std::isfinite(p.z)
		    || p.x < 2 || p.x > 108 || p.z < 2 || p.z > 108 || p.height < 0 || p.height > 16) return false;
		minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
		minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
	}
	// Conservative visual-band admission ONLY. Collision comes from authored
	// triangle/cell ownership records and is never created from these bounds.
	if (!(maxX <= 12 || minX >= 98 || maxZ <= 12 || minZ >= 98)) return false;
	for (float c : { t.color.red, t.color.green, t.color.blue })
		if (!std::isfinite(c) || c < 0 || c > 1) return false;
	const auto &a = t.vertices[0]; const auto &b = t.vertices[1]; const auto &c = t.vertices[2];
	const double ux = double(b.x) - a.x, uy = double(b.height) - a.height, uz = double(b.z) - a.z;
	const double vx = double(c.x) - a.x, vy = double(c.height) - a.height, vz = double(c.z) - a.z;
	const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
	return nx * nx + ny * ny + nz * nz > 1.0e-20;
}

bool Parse(std::span<const std::byte> bytes, BorderCandidate &out)
{
	if (bytes.size() < HeaderBytes || bytes.size() > MaxBytes) return false;
	Reader r { bytes }; char magic[8]; uint32_t version, seed, triangles, ranges, supports, reserved;
	std::array<uint8_t, 32> env, town, expectedEnv, expectedTown;
	if (!r.Copy(magic, 8) || std::memcmp(magic, "D3DBDR1\0", 8) != 0
	    || !r.U32(version) || version != 1 || !r.U32(seed) || seed != 0x54525331
	    || !r.U32(triangles) || triangles == 0 || triangles > MaxTriangles
	    || !r.U32(ranges) || ranges != 27 || !r.U32(supports) || supports == 0 || supports > MaxSupportPins
	    || !r.U32(reserved) || reserved != 0 || !r.Copy(env.data(), env.size()) || !r.Copy(town.data(), town.size())
	    || !HexToModHash(TownBorderBaseEnvSha256, expectedEnv) || env != expectedEnv
	    || !HexToModHash(TownBorderTownManifestSha256, expectedTown) || town != expectedTown) return false;
	if (HeaderBytes + size_t(triangles) * TriangleBytes + size_t(ranges) * RangeBytes + size_t(supports) * SupportBytes != bytes.size()) return false;
	BorderCandidate candidate;
	candidate.expectedPieces.fill(std::numeric_limits<uint16_t>::max());
	candidate.geometry.triangles.resize(triangles); candidate.geometry.cells.resize(ranges); candidate.support.resize(supports);
	for (auto &t : candidate.geometry.triangles) {
		for (auto &p : t.vertices) if (!r.Float(p.x) || !r.Float(p.height) || !r.Float(p.z)) return false;
		if (!r.Float(t.color.red) || !r.Float(t.color.green) || !r.Float(t.color.blue) || !ValidTriangle(t)) return false;
	}
	uint32_t cursor = 0;
	size_t rangeIndex = 0;
	for (auto &cell : candidate.geometry.cells) {
		if (!r.I32(cell.x) || !r.I32(cell.z) || !r.U32(cell.firstTriangle) || !r.U32(cell.triangleCount) || !r.U32(cell.kind)
		    || cell.x < 3 || cell.x > 108 || cell.z < 3 || cell.z > 108 || cell.kind != 1
		    || cell.firstTriangle != cursor || cell.triangleCount == 0 || cell.triangleCount > triangles - cursor) return false;
		for (size_t i = cursor; i < size_t(cursor) + cell.triangleCount; ++i) {
			for (const auto &p : candidate.geometry.triangles[i].vertices) {
				const double x = 32 + 32 * (double(p.x) - cell.x - p.z + cell.z);
				const double y = 16 * (double(p.x) - cell.x + p.z - cell.z) - 32 * p.height;
				if (std::abs(y) > 16 * (TownSurroundingsNativeMaxExtraRows - 2)
				    || x < -64 * (TownSurroundingsNativeMaxExtraColumns - 1)
				    || x > 64 * TownSurroundingsNativeMaxExtraColumns) return false;
			}
		}
		if (TownBorderCameraGroups[rangeIndex])
			candidate.cameraSurfaces.insert(candidate.cameraSurfaces.end(), candidate.geometry.triangles.begin() + cursor,
			    candidate.geometry.triangles.begin() + cursor + cell.triangleCount);
		++rangeIndex; cursor += cell.triangleCount;
	}
	if (cursor != triangles) return false;
	size_t previous = 0; bool first = true;
	for (auto &p : candidate.support) {
		uint16_t special;
		if (!r.U16(p.x) || !r.U16(p.z) || !r.U16(p.piece) || !r.U16(special) || !r.U32(p.properties)
		    || p.x < 3 || p.x > 108 || p.z < 3 || p.z > 108 || p.piece > 3 || special != 0
		    || (p.properties != 0 && p.properties != AuthoredProperties)) return false;
		p.special = 0; const size_t index = size_t(p.z) * TownSide + p.x;
		if (!first && index <= previous) return false;
		first = false; previous = index; candidate.properties[index] = static_cast<uint8_t>(p.properties);
		candidate.expectedPieces[index] = p.piece;
	}
	if (r.offset != bytes.size()) return false;
	candidate.geometry.valid = true; candidate.geometry.identity = TownBorderExpectedSha256;
	out = std::move(candidate); return true;
}

bool LoadBytes(const char *path, size_t expectedBytes, const char *expectedSha, std::vector<std::byte> &out)
{
	std::array<uint8_t, 32> expected;
	if (!HexToModHash(expectedSha, expected) || expectedBytes == 0 || expectedBytes > MaxBytes) return false;
	AssetRef ref = FindAsset(path);
	if (!ref.ok() || ref.size() != expectedBytes) return false;
	AssetHandle handle = OpenAsset(std::move(ref)); if (!handle.ok()) return false;
	std::vector<std::byte> bytes(expectedBytes);
	if (!handle.read(bytes.data(), bytes.size()) || ComputeBytesSha256(bytes) != expected) return false;
	out = std::move(bytes); return true;
}

bool NativeSupportMatches(const BorderCandidate &candidate)
{
	if (leveltype != DTYPE_TOWN || setlevel || gbIsMultiplayer || gbIsHellfire || gbIsSpawn) return false;
	for (const auto &p : candidate.support)
		if (dPiece[p.x][p.z] != p.piece || dSpecial[p.x][p.z] != p.special) return false;
	return true;
}

bool NativeContextAllowed()
{
	return leveltype == DTYPE_TOWN && !setlevel && !gbIsMultiplayer && !gbIsHellfire && !gbIsSpawn;
}

} // namespace

void ClearTownBorderEnvironmentForMap() noexcept
{
	MapAdmitted = false; MapStaged = false; NativeTownMapPrepared = false; Composite = {}; Border = {};
	NativeVisualAdmission = TownBorderVisualStatus::NotPrepared;
	++NativeMapEpoch;
}

void ResetTownBorderEnvironmentVisualResources() noexcept
{
	// Literal scene+footprint belong to the native MAP, not a GPU/view cache.
	// F4/visual reset retains both. Existing adapters reset their derived data.
}

bool PrepareTownBorderEnvironmentForMap() noexcept
{
	ClearTownBorderEnvironmentForMap();
	if (!NativeContextAllowed()) return false;
	NativeTownMapPrepared = true;
	NativeVisualAdmission = TownBorderVisualStatus::Refused;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
		std::vector<std::byte> bytes, manifest;
		if (!LoadBytes(TownBorderAssetPath, TownBorderExpectedBytes, TownBorderExpectedSha256, bytes)
		    || !LoadBytes("d3d-scenes/tristram.ini", TownBorderTownManifestBytes, TownBorderTownManifestSha256, manifest)) return false;
		BorderCandidate candidate;
		if (!Parse(bytes, candidate) || !NativeSupportMatches(candidate)) return false;
		const auto *base = GetTownSurroundingsVisualScene();
		if (base == nullptr || !base->valid || base->identity != TownBorderBaseEnvSha256) return false;
		TownSurroundingsVisualScene composed = *base;
		const size_t offset = composed.triangles.size();
		if (offset + candidate.geometry.triangles.size() > 1000000 || composed.cells.size() + candidate.geometry.cells.size() > 100000) return false;
		composed.triangles.insert(composed.triangles.end(), candidate.geometry.triangles.begin(), candidate.geometry.triangles.end());
		for (auto range : candidate.geometry.cells) { range.firstTriangle += static_cast<uint32_t>(offset); composed.cells.push_back(range); }
		// Companion bytes bind exact base SHA+Town manifest+authored additions.
		composed.identity = TownBorderExpectedSha256; composed.valid = true;
		// Native item/spawn setup must see the original map and RNG behavior.
		// Publish neither geometry nor collision until the post-actor commit.
		Border = std::move(candidate); Composite = std::move(composed); MapStaged = true;
		NativeVisualAdmission = TownBorderVisualStatus::NotPrepared; return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (...) { ClearTownBorderEnvironmentForMap(); NativeVisualAdmission = TownBorderVisualStatus::Refused; return false; }
#endif
}

bool TownBorderEnvironmentStaged() noexcept { return MapStaged; }

bool CommitTownBorderEnvironmentAfterActors() noexcept
{
	if (!MapStaged || !TownNativeMapPresentationActive()) return false;
	bool safe = NativeSupportMatches(Border);
	// ENTRY_LOAD may not yet have repopulated dPlayer. Check the real player
	// state as well, without relocating it or changing any native field.
	if (safe) for (const auto &player : Players) {
		if (!player.plractive || !player.isOnActiveLevel()) continue;
		for (Point tile : { player.position.tile, player.position.future }) {
			if (tile.x < 0 || tile.y < 0 || tile.x >= int(TownSide) || tile.y >= int(TownSide)
			    || Border.properties[size_t(tile.y) * TownSide + tile.x] != 0) { safe = false; break; }
		}
		if (!safe) break;
	}
	if (safe) {
		for (const auto &p : Border.support) {
			if (p.properties == 0) continue;
			if (dPlayer[p.x][p.z] != 0 || dMonster[p.x][p.z] != 0 || dObject[p.x][p.z] != 0
			    || dItem[p.x][p.z] != 0 || dCorpse[p.x][p.z] != 0
			    || HasAnyOf(dFlags[p.x][p.z], DungeonFlag::DeadPlayer)) { safe = false; break; }
		}
	}
	MapStaged = false;
	if (!safe) {
		// Native lifecycle refusal, with no actor teleport or simulation write.
		MapAdmitted = false; Composite = {}; Border = {}; NativeVisualAdmission = TownBorderVisualStatus::Refused; ++NativeMapEpoch; return false;
	}
	MapAdmitted = true; NativeVisualAdmission = TownBorderVisualStatus::Admitted; ++NativeMapEpoch; return true;
}

bool TownBorderTileHasAny(Point position, TileProperties properties) noexcept
{
	if (!MapAdmitted || !NativeContextAllowed() || position.x < 0 || position.y < 0 || position.x >= int(TownSide) || position.y >= int(TownSide)) return false;
	const size_t index = size_t(position.y) * TownSide + position.x;
	const auto extra = static_cast<TileProperties>(Border.properties[index]);
	return HasAnyOf(extra, properties) && dPiece[position.x][position.y] == Border.expectedPieces[index]
	    && dSpecial[position.x][position.y] == 0;
}

bool TownNativeMapPresentationActive() noexcept { return NativeTownMapPrepared && NativeContextAllowed(); }
bool TownBorderEnvironmentActive() noexcept { return MapAdmitted && TownNativeMapPresentationActive(); }

const TownSurroundingsVisualScene *GetTownEnvironmentVisualScene(bool includeSurroundings) noexcept
{
	// This getter is read-only. Render/camera/F4 never changes map admission.
	// Native owners must Clear before every map/World region replacement.
	if (TownBorderEnvironmentActive()) return includeSurroundings ? &Composite : &Border.geometry;
	return includeSurroundings ? GetTownSurroundingsVisualScene() : nullptr;
}

uint64_t GetTownBorderEnvironmentEpoch() noexcept { return NativeMapEpoch; }

std::span<const TownSurroundingsTriangle> GetTownBorderCameraSurfaces() noexcept
{
	if (!MapAdmitted || !NativeContextAllowed()) return {};
	return Border.cameraSurfaces;
}

TownBorderVisualAdmission CaptureTownBorderVisualAdmission() noexcept
{
    return { NativeVisualAdmission, NativeMapEpoch, TownBorderExpectedSha256Raw, TownBorderBaseEnvSha256Raw };
}

std::shared_ptr<const TownBorderFrozenVisualOwner> PrepareTownBorderForFrozenMap(
    const TownBorderFrozenMapView &map, const TownBorderVisualAdmission &admission,
    const TownSurroundingsVisualScene &authenticatedBase,
    std::span<const std::byte> companionBytes, uint64_t worldSnapshotEpoch,
    std::string &error) noexcept
{
    error.clear();
    try {
        const auto fail = [&](const char *reason) -> std::shared_ptr<const TownBorderFrozenVisualOwner> {
            error = reason; return {};
        };
        if (admission.status != TownBorderVisualStatus::NotPrepared
            && admission.status != TownBorderVisualStatus::Refused
            && admission.status != TownBorderVisualStatus::Admitted)
            return fail("Unknown frozen Nature admission status");
        if (admission.nativeMapEpoch == 0 || worldSnapshotEpoch == 0 || map.capturedTownEpoch == 0)
            return fail("Frozen Nature epochs must be nonzero and remain distinct counters");
        if (admission.companionSha256 != TownBorderExpectedSha256Raw
            || admission.baseEnvironmentSha256 != TownBorderBaseEnvSha256Raw
            || map.sourceTownSceneSha256 != TownBorderTownManifestSha256Raw)
            return fail("Frozen Nature source identities do not match this revision");
        if (map.pieces.size() != TownBorderFrozenCells || map.specials.size() != TownBorderFrozenCells)
            return fail("Frozen Town arrays must be exactly112x112");
        std::array<std::byte, TownBorderFrozenCells * 3> canonical {};
        for (size_t i = 0; i < TownBorderFrozenCells; ++i) {
            if (map.pieces[i] >= 1379) return fail("Frozen Town piece outside structural bound");
            canonical[i * 2] = std::byte(map.pieces[i] & 255);
            canonical[i * 2 + 1] = std::byte(map.pieces[i] >> 8);
            canonical[TownBorderFrozenCells * 2 + i] = std::byte(map.specials[i]);
        }
        if (ComputeBytesSha256(canonical) != map.mapSha256)
            return fail("Frozen Town canonical map hash mismatch");
        auto owner = std::make_shared<TownBorderFrozenVisualOwner>();
        owner->admission = admission;
        owner->decisionComplete = admission.status != TownBorderVisualStatus::NotPrepared;
        owner->worldSnapshotEpoch = worldSnapshotEpoch;
        owner->capturedTownEpoch = map.capturedTownEpoch;
        owner->frozenMapSha256 = map.mapSha256;
        // Delimited fixed-width hashes and decimal counters are unambiguous.
        owner->identity = "town-border-frozen-r4:" + std::to_string(static_cast<unsigned>(admission.status))
            + ":" + std::to_string(admission.nativeMapEpoch) + ":" + std::to_string(worldSnapshotEpoch)
            + ":" + std::to_string(map.capturedTownEpoch) + ":" + ModHashToHex(map.mapSha256)
            + ":" + ModHashToHex(map.sourceTownSceneSha256) + ":" + TownBorderExpectedSha256 + ":" + TownBorderBaseEnvSha256;
        owner->additions.valid = true;
        owner->additions.identity = owner->identity;
        if (admission.status != TownBorderVisualStatus::Admitted)
            return owner; // Known refusal/not-prepared; NEVER revive Nature.
        if (!authenticatedBase.valid || authenticatedBase.identity != TownBorderBaseEnvSha256
            || !authenticatedBase.hasNativePaddingUnderlay)
            return fail("Admitted frozen Nature requires its authenticated base ENV");
        if (companionBytes.size() != TownBorderExpectedBytes
            || ComputeBytesSha256(companionBytes) != TownBorderExpectedSha256Raw)
            return fail("Admitted frozen Nature companion missing or hash mismatch");
        BorderCandidate candidate;
        if (!Parse(companionBytes, candidate) || candidate.support.size() != 154
            || candidate.geometry.triangles.size() != 49928 || candidate.geometry.cells.size() != 27)
            return fail("Admitted frozen Nature companion decode refused");
        for (const auto &pin : candidate.support) {
            const size_t at = size_t(pin.z) * TownBorderFrozenSide + pin.x;
            if (map.pieces[at] != pin.piece || map.specials[at] != static_cast<uint8_t>(pin.special))
                return fail("Admitted frozen Nature support piece/special mismatch");
        }
        // Move the ADD and literal camera faces only. No base copies, no spans.
        owner->additions = std::move(candidate.geometry);
        owner->additions.identity = owner->identity;
        owner->cameraSurfaces = std::move(candidate.cameraSurfaces);
        return owner;
    } catch (...) {
        // Even writing an error string may allocate; do not violate noexcept.
        error.clear(); return {};
    }
}

} // namespace devilution
