#pragma once
#include <array>
#include <cstdint>
namespace devilution {
/** Fixed, serializer-friendly leaf. No World/simulation headers or globals.
 * The World codec must encode fields explicitly, never sizeof this struct. */
enum class TownBorderVisualStatus : uint8_t { NotPrepared = 0, Refused = 1, Admitted = 2 };
struct TownBorderVisualAdmission {
    TownBorderVisualStatus status = TownBorderVisualStatus::NotPrepared;
    uint64_t nativeMapEpoch = 0;
    std::array<uint8_t, 32> companionSha256 {};
    std::array<uint8_t, 32> baseEnvironmentSha256 {};
};
} // namespace devilution
