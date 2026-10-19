#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/render/town_visual_scene.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/lighting_defs.hpp"
#include "engine/palette.h"
#include "levels/dun_tile.hpp"
#include "levels/surface_town_visual.hpp"

namespace devilution {

struct SurfaceTownPreviewBudget {
	size_t maximumModels = 0;
	size_t maximumStaticObjects = 0;
	size_t maximumStoredTriangles = 0;
	size_t maximumDecodedPayloadBytes = 0;
	constexpr bool operator==(const SurfaceTownPreviewBudget &) const = default;
};

struct SurfaceTownPreviewStats {
	size_t models = 0, staticObjects = 0, storedTriangles = 0;
	// Publication payload accounting, including both selected/source model
	// vectors and repeated shared textures. Checked AFTER decoding: this is
	// neither a cold decoder peak-memory limit nor a resident-memory/FPS gate.
	size_t decodedPayloadBytes = 0;
};

// Native Town visual resources are decoded into this independent owner before
// cleanup. Never borrow C1/current-carrier CEL/MIN/SOL/specials or palette.
struct SurfaceTownNativeArtworkSource {
 uint64_t resourceGeneration = 0;
 std::unique_ptr<std::byte[]> cels;
 size_t celsBytes = 0, retainedBytes = 0, pieceCount = 0;
 std::array<MICROS,1379> micros {};
 std::array<TileProperties,1379> properties {};
 OptionalOwnedClxSpriteList specials;
 std::array<SDL_Color,256> palette {};
 std::array<std::array<uint8_t,LightTableSize>,NumLightingLevels> identityLightTables {};
 std::array<uint8_t,LightTableSize> darkLightTable {};
 std::string identity;
};

struct SurfaceTownPreviewSource {
	uint64_t resourceGeneration = 0;
	uint64_t revision = 0;
	std::shared_ptr<const SurfaceTownNativeArtworkSource> native;
	// Initial capture provenance only. Content-equivalent later Town captures
	// may reuse this source; current frame publication/World epoch is external.
	surface::TownVisualMap map;
	TownVisualPreviewSource visual;
	SurfaceTownPreviewStats stats;
	// Real source indexes requiring renderer preparation against the frozen
	// map. Never replace these with live carrier dPiece/dSpecial or old grass.
	std::vector<uint32_t> nativeArtworkModels;
	std::vector<uint32_t> changedNativeSourceObjects;
};

struct SurfaceTownPreviewCache {
	uint64_t resourceGeneration = 0;
	std::optional<surface::TownVisualMap> attemptedMap;
	SurfaceTownPreviewBudget attemptedBudget;
	std::shared_ptr<const SurfaceTownPreviewSource> published;
	std::string failure;
	uint64_t coldLoadAttempts = 0;
};

void ResetSurfaceTownPreview(SurfaceTownPreviewCache &cache) noexcept;

// Content comparison deliberately excludes capturedTownEpoch and all current
// Region/World epochs. The host owns storage/CRC validation and publishes an
// immutable companion; this resource factory validates identity and budgets.
bool SameSurfaceTownPreviewContent(const surface::TownVisualMap &a,
    const surface::TownVisualMap &b) noexcept;

// Real cold source/cache stage, render-thread only and outside draw/pixel loops.
// Input is the actual host's validated companion; sourceSceneSha256 selects the
// real asset manifest, which in turn pins every model and static archive.
// Cache key: map/scene/worldPatch hashes + captured visualSelection kind/all
// three hashes + events + resource generation, never
// RegionId/World epoch. Header/content mismatch or failed load clears output;
// an owned old frame may retain its source until its own freshness rejects it.
// This returns SOURCE data, not DrawReady: native artwork/bridge/event deltas
// and ground9 materials must be prepared using this frozen map and real Town
// resources in the existing compositor before a visual context is published.
bool PrepareSurfaceTownPreview(SurfaceTownPreviewCache &cache, uint64_t resourceGeneration,
    const surface::TownVisualMap &map, const SurfaceTownPreviewBudget &budget,
    std::shared_ptr<const SurfaceTownPreviewSource> &output, std::string &failure);

} // namespace devilution
