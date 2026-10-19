#include "levels/surface_town_visual_codec.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

#include "mods/mod_identity.h"

namespace devilution::surface {
namespace {
constexpr std::array<uint8_t, 8> Magic { 'D', '3', 'D', 'T', 'W', 'N', '2', 0 };
constexpr std::size_t ChecksumOffset = 316;
static_assert(TownVisualRecordBytes == 37952);

uint16_t Read16(std::span<const uint8_t> bytes, std::size_t at)
{
	return static_cast<uint16_t>(uint16_t(bytes[at]) | (uint16_t(bytes[at + 1]) << 8));
}
uint32_t Read32(std::span<const uint8_t> bytes, std::size_t at)
{
	uint32_t result = 0;
	for (unsigned i = 0; i < 4; ++i) result |= uint32_t(bytes[at + i]) << (i * 8);
	return result;
}
uint64_t Read64(std::span<const uint8_t> bytes, std::size_t at)
{
	return uint64_t(Read32(bytes, at)) | (uint64_t(Read32(bytes, at + 4)) << 32);
}
void Write(std::span<uint8_t> bytes, std::size_t at, uint64_t value, unsigned width)
{
	for (unsigned i = 0; i < width; ++i) bytes[at + i] = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t Checksum(std::span<const uint8_t> bytes)
{
	uint32_t crc = 0xFFFFFFFFU;
	for (std::size_t i = 0; i < bytes.size(); ++i) {
		crc ^= i >= ChecksumOffset && i < ChecksumOffset + 4 ? 0U : bytes[i];
		for (unsigned bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ ((crc & 1U) != 0 ? 0xEDB88320U : 0U);
	}
	return ~crc;
}
bool Nonzero(const std::array<uint8_t, 32> &hash)
{
	return std::any_of(hash.begin(), hash.end(), [](uint8_t value) { return value != 0; });
}
TownVisualDecodeResult Failure(TownVisualCodecError error) { return { std::nullopt, error }; }

std::optional<std::vector<uint8_t>> Encode(const TownVisualMap &map)
{
	if (ValidateTownVisualMap(map) != TownVisualCodecError::None) return std::nullopt;
	std::vector<uint8_t> bytes(TownVisualRecordBytes, 0);
	std::copy(Magic.begin(), Magic.end(), bytes.begin());
	Write(bytes, 8, 2, 2); Write(bytes, 10, TownVisualHeaderBytes, 2); Write(bytes, 12, bytes.size(), 4);
	Write(bytes, 16, map.schema, 4); Write(bytes, 20, map.gameVariant, 4); Write(bytes, 24, map.capturedTownEpoch, 8);
	Write(bytes, 32, TownVisualSide, 2); Write(bytes, 34, TownVisualSide, 2);
	bytes[36] = map.fullbrightLight;
	bytes[37] = map.events.wellWater ? 1 : 0; bytes[38] = map.events.catacombsAccess ? 1 : 0;
	bytes[39] = map.events.cavesAccess ? 1 : 0; bytes[40] = map.events.hellAccess ? 1 : 0;
	std::copy(map.mapSha256.begin(), map.mapSha256.end(), bytes.begin() + 44);
	std::copy(map.sourceTownSceneSha256.begin(), map.sourceTownSceneSha256.end(), bytes.begin() + 76);
	std::copy(map.worldPatchSha256.begin(), map.worldPatchSha256.end(), bytes.begin() + 108);
    bytes[140] = static_cast<uint8_t>(map.natureAdmission.status);
    Write(bytes, 141, map.natureAdmission.nativeMapEpoch, 8);
    std::copy(map.natureAdmission.companionSha256.begin(), map.natureAdmission.companionSha256.end(), bytes.begin() + 149);
    std::copy(map.natureAdmission.baseEnvironmentSha256.begin(), map.natureAdmission.baseEnvironmentSha256.end(), bytes.begin() + 181);
    bytes[213] = static_cast<uint8_t>(map.visualSelection.kind);
    std::copy(map.visualSelection.canonicalSha256.begin(), map.visualSelection.canonicalSha256.end(), bytes.begin() + 214);
    std::copy(map.visualSelection.selectedSha256.begin(), map.visualSelection.selectedSha256.end(), bytes.begin() + 246);
    std::copy(map.visualSelection.staticSha256.begin(), map.visualSelection.staticSha256.end(), bytes.begin() + 278);
	for (std::size_t i = 0; i < TownVisualCellCount; ++i) Write(bytes, TownVisualHeaderBytes + i * 2, map.pieces[i], 2);
	std::copy(map.specials.begin(), map.specials.end(), bytes.begin() + TownVisualHeaderBytes + TownVisualCellCount * 2);
	Write(bytes, ChecksumOffset, Checksum(bytes), 4);
	return bytes;
}
TownVisualDecodeResult Decode(std::span<const uint8_t> bytes)
{
	if (bytes.size() != TownVisualRecordBytes) return Failure(TownVisualCodecError::SizeMismatch);
	if (!std::equal(Magic.begin(), Magic.end(), bytes.begin())) return Failure(TownVisualCodecError::Malformed);
	if (Read16(bytes, 8) != 2) return Failure(TownVisualCodecError::UnsupportedFormat);
	if (Read16(bytes, 10) != TownVisualHeaderBytes || Read32(bytes, 12) != TownVisualRecordBytes
	    || Read16(bytes, 32) != TownVisualSide || Read16(bytes, 34) != TownVisualSide)
		return Failure(TownVisualCodecError::SizeMismatch);
	if (Read32(bytes, ChecksumOffset) != Checksum(bytes)) return Failure(TownVisualCodecError::ChecksumMismatch);
	for (std::size_t i = 37; i <= 40; ++i) if (bytes[i] > 1) return Failure(TownVisualCodecError::InvalidEvent);
	for (std::size_t i = 41; i < 44; ++i) if (bytes[i] != 0) return Failure(TownVisualCodecError::InvalidReservedBytes);
	for (std::size_t i = 310; i < 316; ++i) if (bytes[i] != 0) return Failure(TownVisualCodecError::InvalidReservedBytes);
	TownVisualMap map;
	map.schema = Read32(bytes, 16); map.gameVariant = Read32(bytes, 20); map.capturedTownEpoch = Read64(bytes, 24);
	map.fullbrightLight = bytes[36];
	map.events = { bytes[37] != 0, bytes[38] != 0, bytes[39] != 0, bytes[40] != 0 };
	std::copy_n(bytes.begin() + 44, 32, map.mapSha256.begin());
	std::copy_n(bytes.begin() + 76, 32, map.sourceTownSceneSha256.begin());
	std::copy_n(bytes.begin() + 108, 32, map.worldPatchSha256.begin());
    map.natureAdmission.status = static_cast<TownBorderVisualStatus>(bytes[140]);
    map.natureAdmission.nativeMapEpoch = Read64(bytes, 141);
    std::copy_n(bytes.begin() + 149, 32, map.natureAdmission.companionSha256.begin());
    std::copy_n(bytes.begin() + 181, 32, map.natureAdmission.baseEnvironmentSha256.begin());
    map.visualSelection.kind = static_cast<TownVisualSelectionKind>(bytes[213]);
    std::copy_n(bytes.begin() + 214, 32, map.visualSelection.canonicalSha256.begin());
    std::copy_n(bytes.begin() + 246, 32, map.visualSelection.selectedSha256.begin());
    std::copy_n(bytes.begin() + 278, 32, map.visualSelection.staticSha256.begin());
	for (std::size_t i = 0; i < TownVisualCellCount; ++i) map.pieces[i] = Read16(bytes, TownVisualHeaderBytes + i * 2);
	std::copy_n(bytes.begin() + TownVisualHeaderBytes + TownVisualCellCount * 2, TownVisualCellCount, map.specials.begin());
	if (const auto error = ValidateTownVisualMap(map); error != TownVisualCodecError::None) return Failure(error);
	return { std::move(map), TownVisualCodecError::None };
}
} // namespace

std::array<uint8_t, 32> ComputeTownVisualMapSha256(const TownVisualMap &map)
{
	std::array<uint8_t, TownVisualCanonicalMapBytes> canonical {};
	for (std::size_t i = 0; i < TownVisualCellCount; ++i) Write(canonical, i * 2, map.pieces[i], 2);
	std::copy(map.specials.begin(), map.specials.end(), canonical.begin() + TownVisualCellCount * 2);
	return ComputeBytesSha256(std::as_bytes(std::span(canonical)));
}
TownVisualCodecError ValidateTownVisualMap(const TownVisualMap &map)
{
	if (map.schema != TownVisualSchemaRevision) return TownVisualCodecError::UnsupportedSchema;
	if (map.gameVariant != 0) return TownVisualCodecError::UnsupportedVariant;
	if (map.capturedTownEpoch == 0 || map.capturedTownEpoch == std::numeric_limits<uint64_t>::max()) return TownVisualCodecError::InvalidCaptureEpoch;
	if (!Nonzero(map.sourceTownSceneSha256) || !Nonzero(map.worldPatchSha256)) return TownVisualCodecError::InvalidIdentity;
    if (map.natureAdmission.status == TownBorderVisualStatus::NotPrepared)
        return TownVisualCodecError::IncompleteNatureAdmission;
    if ((map.natureAdmission.status != TownBorderVisualStatus::Refused && map.natureAdmission.status != TownBorderVisualStatus::Admitted)
        || map.natureAdmission.nativeMapEpoch == 0 || !Nonzero(map.natureAdmission.companionSha256)
        || !Nonzero(map.natureAdmission.baseEnvironmentSha256)) return TownVisualCodecError::InvalidNatureAdmission;
    if (map.visualSelection.kind == TownVisualSelectionKind::NotAdmitted)
        return TownVisualCodecError::IncompleteVisualSelection;
    if (!Nonzero(map.visualSelection.staticSha256) || map.visualSelection.canonicalSha256 != map.sourceTownSceneSha256)
        return TownVisualCodecError::InvalidVisualSelection;
    if (map.visualSelection.kind == TownVisualSelectionKind::Canonical) {
        if (map.visualSelection.selectedSha256 != map.sourceTownSceneSha256) return TownVisualCodecError::InvalidVisualSelection;
    } else if (map.visualSelection.kind == TownVisualSelectionKind::SpecificOptional) {
        std::array<uint8_t, 32> canonical, selected;
        if (!HexToModHash(TownVisualSpecificCanonicalSha256, canonical) || !HexToModHash(TownVisualSpecificSelectedSha256, selected)
            || map.visualSelection.canonicalSha256 != canonical || map.visualSelection.selectedSha256 != selected)
            return TownVisualCodecError::InvalidVisualSelection;
    } else return TownVisualCodecError::InvalidVisualSelection;
	if (map.fullbrightLight != 0) return TownVisualCodecError::InvalidLight;
	for (const auto piece : map.pieces) if (piece >= TownVisualMaximumPieceCount) return TownVisualCodecError::InvalidPiece;
	if (map.mapSha256 != ComputeTownVisualMapSha256(map)) return TownVisualCodecError::MapHashMismatch;
	return TownVisualCodecError::None;
}
std::optional<std::vector<uint8_t>> EncodeTownVisualMap(const TownVisualMap &map)
{
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try { return Encode(map); } catch (const std::bad_alloc &) { return std::nullopt; }
#else
	return Encode(map);
#endif
}
TownVisualDecodeResult DecodeTownVisualMap(std::span<const uint8_t> bytes)
{
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try { return Decode(bytes); } catch (const std::bad_alloc &) { return Failure(TownVisualCodecError::AllocationFailure); }
#else
	return Decode(bytes);
#endif
}
} // namespace devilution::surface
