#include "levels/surface_world_codec.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <new>
#include <utility>

namespace devilution::surface {
namespace {

constexpr std::array<std::uint8_t, 8> Magic { 'D', '3', 'D', 'S', 'R', 'F', '1', 0 };
constexpr std::uint16_t FormatVersion = 1;
constexpr std::size_t ChecksumOffset = 36;
constexpr std::int32_t RegionLimit = static_cast<std::int32_t>(MaximumRegionCoordinate);
constexpr std::size_t RegionSpan = static_cast<std::size_t>(2 * RegionLimit + 1);
constexpr std::size_t RegionCells = RegionSpan * RegionSpan;
static_assert(RegionLimit == 96);
static_assert(MaximumSurfaceWorldStateBytes == 32808);

bool WithinBounds(RegionId region) noexcept
{
	return region.x >= -RegionLimit && region.x <= RegionLimit
	    && region.z >= -RegionLimit && region.z <= RegionLimit;
}

bool IsNativeTownId(RegionId region) noexcept
{
	return region.x == 0 && region.z == 0;
}

bool SameRegion(RegionId a, RegionId b) noexcept
{
	return a.x == b.x && a.z == b.z;
}

bool RegionLess(RegionId a, RegionId b) noexcept
{
	return a.x < b.x || (a.x == b.x && a.z < b.z);
}

std::uint16_t Read16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
	return static_cast<std::uint16_t>(std::uint16_t { bytes[offset] }
	    | (std::uint16_t { bytes[offset + 1] } << 8));
}

std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
	std::uint32_t value = 0;
	for (unsigned i = 0; i < 4; ++i)
		value |= std::uint32_t { bytes[offset + i] } << (i * 8);
	return value;
}

std::int32_t ReadSigned32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
	return std::bit_cast<std::int32_t>(Read32(bytes, offset));
}

void Write16(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value) noexcept
{
	for (unsigned i = 0; i < 2; ++i)
		bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void Write32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) noexcept
{
	for (unsigned i = 0; i < 4; ++i)
		bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void WriteSigned32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::int32_t value) noexcept
{
	Write32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

/** CRC32 IEEE, reflected polynomial. The stored checksum word is treated as
 * zero; the complete exact record, including all header fields, is covered. */
std::uint32_t RecordChecksum(std::span<const std::uint8_t> bytes) noexcept
{
	std::uint32_t crc = 0xFFFFFFFFU;
	for (std::size_t i = 0; i < bytes.size(); ++i) {
		crc ^= i >= ChecksumOffset && i < ChecksumOffset + 4 ? 0U : bytes[i];
		for (unsigned bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ ((crc & 1U) != 0 ? 0xEDB88320U : 0U);
	}
	return ~crc;
}

DecodeResult Failure(StateCodecError error)
{
	return { std::nullopt, error };
}

std::optional<std::vector<std::uint8_t>> EncodeValidated(const PersistentState &state)
{
	auto visited = state.visited;
	std::sort(visited.begin(), visited.end(), RegionLess);
	const std::size_t size = SurfaceWorldStateHeaderBytes + visited.size() * 8;
	std::vector<std::uint8_t> bytes(size, 0);
	std::copy(Magic.begin(), Magic.end(), bytes.begin());
	Write16(bytes, 8, FormatVersion);
	Write16(bytes, 10, static_cast<std::uint16_t>(SurfaceWorldStateHeaderBytes));
	Write32(bytes, 12, static_cast<std::uint32_t>(size));
	Write32(bytes, 16, state.seed);
	Write32(bytes, 20, state.revision);
	WriteSigned32(bytes, 24, state.region.x);
	WriteSigned32(bytes, 28, state.region.z);
	Write32(bytes, 32, static_cast<std::uint32_t>(visited.size()));
	std::size_t offset = SurfaceWorldStateHeaderBytes;
	for (const auto region : visited) {
		WriteSigned32(bytes, offset, region.x);
		WriteSigned32(bytes, offset + 4, region.z);
		offset += 8;
	}
	Write32(bytes, ChecksumOffset, RecordChecksum(bytes));
	return std::optional<std::vector<std::uint8_t>> { std::move(bytes) };
}

DecodeResult DecodeChecked(std::span<const std::uint8_t> bytes)
{
	if (bytes.size() < SurfaceWorldStateHeaderBytes)
		return Failure(StateCodecError::Malformed);
	if (bytes.size() > MaximumSurfaceWorldStateBytes)
		return Failure(StateCodecError::SizeMismatch);
	if (!std::equal(Magic.begin(), Magic.end(), bytes.begin()))
		return Failure(StateCodecError::InvalidMagic);
	if (Read16(bytes, 8) != FormatVersion)
		return Failure(StateCodecError::UnsupportedFormatVersion);
	if (Read16(bytes, 10) != SurfaceWorldStateHeaderBytes)
		return Failure(StateCodecError::InvalidHeaderSize);
	const std::uint32_t count = Read32(bytes, 32);
	if (count > MaximumPersistentVisitedRegions)
		return Failure(StateCodecError::TooManyVisited);
	// Count is bounded before multiplication; no input-controlled overflow or
	// allocation precedes exact total/record-size and CRC validation.
	const std::size_t expected = SurfaceWorldStateHeaderBytes + static_cast<std::size_t>(count) * 8;
	if (Read32(bytes, 12) != expected || bytes.size() != expected)
		return Failure(StateCodecError::SizeMismatch);
	if (Read32(bytes, ChecksumOffset) != RecordChecksum(bytes))
		return Failure(StateCodecError::ChecksumMismatch);
	if (Read32(bytes, 16) != VisualSeed)
		return Failure(StateCodecError::UnsupportedSeed);
	if (Read32(bytes, 20) != GeneratorRevision)
		return Failure(StateCodecError::UnsupportedGeneratorRevision);
	PersistentState state;
	state.seed = Read32(bytes, 16);
	state.revision = Read32(bytes, 20);
	state.region = { ReadSigned32(bytes, 24), ReadSigned32(bytes, 28) };
	state.visited.reserve(count);
	for (std::size_t offset = SurfaceWorldStateHeaderBytes; offset < expected; offset += 8)
		state.visited.push_back({ ReadSigned32(bytes, offset), ReadSigned32(bytes, offset + 4) });
	if (const auto error = ValidatePersistentState(state); error != StateCodecError::None)
		return Failure(error);
	for (std::size_t i = 1; i < state.visited.size(); ++i) {
		if (!RegionLess(state.visited[i - 1], state.visited[i]))
			return Failure(StateCodecError::NonCanonicalVisitedOrder);
	}
	return { std::optional<PersistentState> { std::move(state) }, StateCodecError::None };
}

} // namespace

StateCodecError ValidatePersistentState(const PersistentState &state) noexcept
{
	if (state.seed != VisualSeed)
		return StateCodecError::UnsupportedSeed;
	if (state.revision != GeneratorRevision)
		return StateCodecError::UnsupportedGeneratorRevision;
	if (state.visited.size() > MaximumPersistentVisitedRegions)
		return StateCodecError::TooManyVisited;
	if (!WithinBounds(state.region))
		return StateCodecError::RegionOutOfBounds;
	std::bitset<RegionCells> seen;
	bool currentPresent = IsNativeTownId(state.region);
	for (const auto region : state.visited) {
		if (!WithinBounds(region))
			return StateCodecError::RegionOutOfBounds;
		if (IsNativeTownId(region))
			return StateCodecError::TownInVisited;
		const std::size_t index = static_cast<std::size_t>(region.x + RegionLimit) * RegionSpan
		    + static_cast<std::size_t>(region.z + RegionLimit);
		if (seen[index])
			return StateCodecError::DuplicateVisited;
		seen[index] = true;
		currentPresent = currentPresent || SameRegion(region, state.region);
	}
	return currentPresent ? StateCodecError::None : StateCodecError::CurrentRegionNotVisited;
}

std::optional<std::vector<std::uint8_t>> EncodeSurfaceWorldState(const PersistentState &state)
{
	if (ValidatePersistentState(state) != StateCodecError::None)
		return std::nullopt;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
		return EncodeValidated(state);
	} catch (const std::bad_alloc &) {
		return std::nullopt;
	}
#else
	return EncodeValidated(state);
#endif
}

DecodeResult DecodeSurfaceWorldState(std::span<const std::uint8_t> bytes)
{
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
		return DecodeChecked(bytes);
	} catch (const std::bad_alloc &) {
		return Failure(StateCodecError::AllocationFailure);
	}
#else
	return DecodeChecked(bytes);
#endif
}

} // namespace devilution::surface
