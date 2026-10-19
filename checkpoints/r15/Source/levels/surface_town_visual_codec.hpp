#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "levels/surface_town_visual.hpp"

namespace devilution::surface {

inline constexpr std::size_t TownVisualHeaderBytes = 320;
inline constexpr std::size_t TownVisualCanonicalMapBytes = TownVisualCellCount * 3;
inline constexpr std::size_t TownVisualRecordBytes = TownVisualHeaderBytes + TownVisualCanonicalMapBytes;
// Structural upper bound only. The host also checks against the real Town MIN.
inline constexpr uint16_t TownVisualMaximumPieceCount = 1379;

enum class TownVisualCodecError : uint8_t {
	None, Malformed, UnsupportedFormat, SizeMismatch, ChecksumMismatch,
	UnsupportedSchema, UnsupportedVariant, InvalidCaptureEpoch, InvalidIdentity,
	InvalidPiece, InvalidLight, InvalidEvent, InvalidReservedBytes, MapHashMismatch,
	AllocationFailure, IncompleteNatureAdmission, InvalidNatureAdmission,
	IncompleteVisualSelection, InvalidVisualSelection,
};

struct TownVisualDecodeResult {
	std::optional<TownVisualMap> map;
	TownVisualCodecError error = TownVisualCodecError::None;
};

// Canonical SHA input: ALL uint16 pieces little-endian, row-major z*112+x,
// followed by ALL uint8 specials. No native struct bytes, padding or header.
[[nodiscard]] std::array<uint8_t, 32> ComputeTownVisualMapSha256(const TownVisualMap &map);
[[nodiscard]] TownVisualCodecError ValidateTownVisualMap(const TownVisualMap &map);
[[nodiscard]] std::optional<std::vector<uint8_t>> EncodeTownVisualMap(const TownVisualMap &map);
// Exact fixed record; CRC32 covers schema/variant/identities/events and cells.
[[nodiscard]] TownVisualDecodeResult DecodeTownVisualMap(std::span<const uint8_t> bytes);

} // namespace devilution::surface
