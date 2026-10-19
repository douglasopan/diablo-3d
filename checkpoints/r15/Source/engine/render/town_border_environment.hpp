#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include "engine/render/town_border_visual_admission.hpp"

#include "engine/render/town_surroundings_visual.hpp"

namespace devilution {

/** Called after InitTownPieces, before actors/path queries. Stages geometry,
 * exact support pins and the authored footprint together; no partial publish. */
bool PrepareTownBorderEnvironmentForMap() noexcept;
/** Native load/join phase AFTER actors/items and SyncInitPlrPos have finished.
 * Before this call no new obstacle influences RNG, spawn or movement. Any
 * occupied footprint or support drift refuses the entire addition unchanged. */
bool CommitTownBorderEnvironmentAfterActors() noexcept;
bool TownBorderEnvironmentStaged() noexcept;
/** Map lifecycle only, before destroying/recreating the native map. */
void ClearTownBorderEnvironmentForMap() noexcept;
/** Visual-cache reset only; F4/camera/resource resets retain native semantics. */
void ResetTownBorderEnvironmentVisualResources() noexcept;
/** Same immutable composite geometry for both existing2D/3D adapters. Missing
 * or refused companion returns the original ENV29 and adds no obstruction. */
const TownSurroundingsVisualScene *GetTownEnvironmentVisualScene(bool includeSurroundings = true) noexcept;
bool TownBorderEnvironmentActive() noexcept;
/** Owned by the native map lifecycle, independent of optional asset admission.
 * World must Clear before replacing carrier-Town cells with a region. */
bool TownNativeMapPresentationActive() noexcept;
/** Native map epoch only; visual resets leave it unchanged. World carrier-Town
 * maps must Clear before replacing their map without InitGlobals. */
uint64_t GetTownBorderEnvironmentEpoch() noexcept;
/** Literal tree/rock surfaces for the existing camera BVH. Flexible grass is
 * deliberately excluded; no AABB proxy is substituted for the actual mesh. */
std::span<const TownSurroundingsTriangle> GetTownBorderCameraSurfaces() noexcept;

/** Read-only native admission capture; F4/resource reset does not alter it. */
TownBorderVisualAdmission CaptureTownBorderVisualAdmission() noexcept;
inline constexpr size_t TownBorderFrozenSide = 112;
inline constexpr size_t TownBorderFrozenCells = TownBorderFrozenSide * TownBorderFrozenSide;
/** Leaf view over a separately owned immutable TownVisualMap. The returned
 * owner never retains these spans or any base-ENV pointer. All epochs differ. */
struct TownBorderFrozenMapView {
    std::span<const uint16_t> pieces;
    std::span<const uint8_t> specials;
    std::array<uint8_t, 32> mapSha256 {};
    std::array<uint8_t, 32> sourceTownSceneSha256 {};
    uint64_t capturedTownEpoch = 0;
};
struct TownBorderFrozenVisualOwner {
    TownBorderVisualAdmission admission;
    // NotPrepared is explicitly incomplete; callers must not label it a complete preview.
    bool decisionComplete = false;
    uint64_t worldSnapshotEpoch = 0;
    uint64_t capturedTownEpoch = 0;
    std::array<uint8_t, 32> frozenMapSha256 {};
    std::string identity;
    // ONLY Nature ADD; no ENV/Halo duplication and no collision/map sidecar.
    TownSurroundingsVisualScene additions;
    std::vector<TownSurroundingsTriangle> cameraSurfaces;
};
/** Pure preflight: consumes only explicit frozen data, authenticated base and
 * exact asset bytes. Does not load files, borrow input data or consult/mutate
 * native map, occupancy, RNG, global admission, camera or renderer caches.
 * Refused/NotPrepared -> known empty owner. Admitted+bad/missing bytes ->
 * nullptr+error, never a silent visual downgrade. Allocation refusal -> nullptr.
 * World owner/codec embedding is a SEPARATE integration; this TU needs no World. */
std::shared_ptr<const TownBorderFrozenVisualOwner> PrepareTownBorderForFrozenMap(
    const TownBorderFrozenMapView &map, const TownBorderVisualAdmission &admission,
    const TownSurroundingsVisualScene &authenticatedBase,
    std::span<const std::byte> companionBytes, uint64_t worldSnapshotEpoch,
    std::string &error) noexcept;

} // namespace devilution
