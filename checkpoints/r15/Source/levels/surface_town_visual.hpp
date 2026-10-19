#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/render/town_border_visual_admission.hpp"
#include "engine/render/town_visual_selection_snapshot.hpp"

namespace devilution::surface {

// Visual-only owned Town data. Native simulation and regional collision never
// consume these cells. The host captures real Town before the carrier changes;
// a cold regional load restores the validated companion before freeing Town.
inline constexpr uint32_t TownVisualSchemaRevision = 2;
inline constexpr size_t TownVisualSide = 112;
inline constexpr size_t TownVisualCellCount = TownVisualSide * TownVisualSide;

struct TownVisualEvents {
    bool wellWater = false;
    bool catacombsAccess = false;
    bool cavesAccess = false;
    bool hellAccess = false;
};

struct TownVisualMap {
    uint32_t schema = TownVisualSchemaRevision;
    // Only retail Diablo single-player is currently admitted; value zero.
    uint32_t gameVariant = 0;
    // Source capture identity; distinct from the current World publication epoch.
    uint64_t capturedTownEpoch = 0;
    std::array<uint8_t, 32> mapSha256 {};
    std::array<uint8_t, 32> sourceTownSceneSha256 {};
    std::array<uint8_t, 32> worldPatchSha256 {};
    // Literal row-major index z * 112 + x, no transpose. Piece indices are
    // validated against actual Town assets before publication.
    std::array<uint16_t, TownVisualCellCount> pieces {};
    std::array<uint8_t, TownVisualCellCount> specials {};
    // Town is fullbright; do not sample dLight from the regional carrier.
    uint8_t fullbrightLight = 0;
    TownVisualEvents events {};
    // Same native-thread capture as the cells. These are actual admission
    // decisions, never reconstructed from asset presence or cold live counters.
    TownBorderVisualAdmission natureAdmission {};
    TownVisualSelectionSnapshot visualSelection {};
};

} // namespace devilution::surface
