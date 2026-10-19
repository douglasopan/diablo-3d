#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "levels/surface_region.hpp"

namespace devilution::surface {

inline constexpr std::size_t MaximumPersistentVisitedRegions = 4096;
inline constexpr std::size_t SurfaceWorldStateHeaderBytes = 40;
inline constexpr std::size_t MaximumSurfaceWorldStateBytes = SurfaceWorldStateHeaderBytes + MaximumPersistentVisitedRegions * 8;

/** Native Town is represented by region(0,0), never by a visited entry.
 * This value owns its visited IDs; no level/global/native RNG state is borrowed. */
struct PersistentState {
	RegionId region {};
	std::uint32_t seed = VisualSeed;
	std::uint32_t revision = GeneratorRevision;
	std::vector<RegionId> visited;
};

enum class StateCodecError : std::uint8_t {
	None,
	Malformed,
	InvalidMagic,
	UnsupportedFormatVersion,
	InvalidHeaderSize,
	SizeMismatch,
	TooManyVisited,
	ChecksumMismatch,
	UnsupportedSeed,
	UnsupportedGeneratorRevision,
	RegionOutOfBounds,
	TownInVisited,
	DuplicateVisited,
	NonCanonicalVisitedOrder,
	CurrentRegionNotVisited,
	AllocationFailure,
};

struct DecodeResult {
	std::optional<PersistentState> state;
	StateCodecError error = StateCodecError::None;
};

/** Checks semantic validity, uniqueness and current-region membership without
 * allocating. Input order need not be canonical; Encode sorts a private copy. */
[[nodiscard]] StateCodecError ValidatePersistentState(const PersistentState &state) noexcept;
/** D3DSRF1, little-endian, exact40-byte header plus8 bytes per visited ID.
 * Invalid input/allocation refusal returns nullopt and never mutates the caller. */
[[nodiscard]] std::optional<std::vector<std::uint8_t>> EncodeSurfaceWorldState(const PersistentState &state);
/** A present empty record is Malformed. Only the archive caller may interpret
 * an ABSENT record as a legacy save. Decoding never repairs or sorts input. */
[[nodiscard]] DecodeResult DecodeSurfaceWorldState(std::span<const std::uint8_t> bytes);

} // namespace devilution::surface
