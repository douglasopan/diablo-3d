#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <memory>
#include <span>
#include <string>

#include "levels/surface_world_codec.hpp"
#include "levels/surface_town_visual_codec.hpp"
#include "pfile.h"

namespace devilution::surface {

inline constexpr char SurfaceWorldArchiveName[] = "worldregions-v1";
inline constexpr char TownVisualArchiveName[] = "townvisual-v1";
inline constexpr std::size_t NativeArchiveWriteReserve = 8;

/** nullopt means ABSENT legacy metadata only. Present malformed/empty data is an error. */
[[nodiscard]] std::expected<std::optional<PersistentState>, std::string> ReadSurfaceWorldState(SaveReader &archive);
[[nodiscard]] bool WriteSurfaceWorldState(SaveWriter &archive, const PersistentState &state);
// nullptr means ABSENT only. Present malformed/short/unsupported data refuses.
[[nodiscard]] std::expected<std::shared_ptr<const TownVisualMap>, std::string> ReadSurfaceTownVisualMap(SaveReader &archive);
[[nodiscard]] bool WriteSurfaceTownVisualMap(SaveWriter &archive, const TownVisualMap &map);

/** srt1/srp1 plus two three-digit biased coordinates. Town is never a regional entry. */
[[nodiscard]] std::optional<std::string> SurfaceRegionArchiveName(RegionId region, bool temporary);
[[nodiscard]] bool HasSurfaceRegionLevel(SaveReader &archive, RegionId region, bool temporary);
[[nodiscard]] bool HasSurfaceRegionLevel(SaveWriter &archive, RegionId region, bool temporary);

/** Budget EVERY planned WriteFile, including replacing an existing entry.
 * This checks one region temp + one metadata + nativeWriteReserve writes.
 * It does not guarantee memory/disk availability; callers must check all writes. */
[[nodiscard]] std::expected<void, std::string> PreflightSurfaceRegionSave(SaveWriter &archive, RegionId region, std::size_t nativeWriteReserve = NativeArchiveWriteReserve);
[[nodiscard]] std::expected<void, std::string> PreflightSurfaceWorldWrites(SaveWriter &archive, std::size_t plannedWrites);

/** Same open native SaveWriter as the FULL save. Rename/remove use no append slots.
 * A false result must abort the caller's save/transition, never free the live level. */
[[nodiscard]] bool PromoteSurfaceRegionTempFiles(SaveWriter &archive, std::span<const RegionId> visited);
/** Enumerates the ENTIRE reserved temp namespace, not just last saved visited IDs. */
[[nodiscard]] bool RemoveSurfaceRegionTempFiles(SaveWriter &archive);
/** Detect any raw owned namespace entry, independent of live Fresh/load policy. */
[[nodiscard]] bool HasSurfaceRegionStorage(SaveWriter &archive);
/** Deferred campaign FULL-save cleanup: removes ONLY srp1 entries. Keeps
 * current-session srt1 temps and old metadata until the new full save writes it. */
[[nodiscard]] bool RemoveSurfaceRegionPermanentFiles(SaveWriter &archive);
/** Explicit maintenance helper. includePermanent also removes owned metadata.
 * NOT used by NEW: a prior full game must retain its metadata/permanent levels.
 * Never names native temp/perml/s entries or Town0. */
[[nodiscard]] bool RemoveSurfaceRegionNamespace(SaveWriter &archive, bool includePermanent = false);

} // namespace devilution::surface
