#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace devilution {

enum class TownVisualSelectionKind : uint8_t {
    NotAdmitted = 0,
    Canonical = 1,
    SpecificOptional = 2,
};

// Immutable value leaf, captured from an already admitted live scene. This is
// not an asset lookup, parser or authorization to fall back to canonical art.
struct TownVisualSelectionSnapshot {
    TownVisualSelectionKind kind = TownVisualSelectionKind::NotAdmitted;
    std::array<uint8_t, 32> canonicalSha256 {};
    std::array<uint8_t, 32> selectedSha256 {};
    std::array<uint8_t, 32> staticSha256 {};
};
static_assert(std::is_trivially_copyable_v<TownVisualSelectionSnapshot>);
static_assert(sizeof(TownVisualSelectionSnapshot) == 97);

inline constexpr std::string_view TownVisualCanonicalSelectionPath = "d3d-scenes/tristram.ini";
inline constexpr std::string_view TownVisualSpecificSelectionPath = "d3d-scenes/tristram-specific-review-r1.ini";
inline constexpr std::string_view TownVisualSpecificCanonicalSha256 = "1a46e15840729f193452a1441f45d8789ccd67fc497ce4c315d235ae45d7ce88";
inline constexpr std::string_view TownVisualSpecificSelectedSha256 = "1b40bf19c2876d25529e59842d79df91f18b69bd491e1b8db58b756960c21ef7";

// Persist each field explicitly; never memcpy C++ object bytes to a wire codec.
// No allocation, asset read, scene refresh or native/RNG mutation is allowed.
// A false result resets every field to NotAdmitted/zero. The host must refuse
// capture before swap; it must not reinterpret that result as Canonical.
bool CaptureTownVisualSelectionForSnapshot(const std::array<uint8_t, 32> &expectedCanonicalSha,
    TownVisualSelectionSnapshot &out) noexcept;

} // namespace devilution
