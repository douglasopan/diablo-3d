#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "engine/point.hpp"
#include "engine/render/player_actor_material.hpp"

namespace devilution {

inline constexpr float TownGrassWorldUvScale = 1.458F / 2.0F;
inline constexpr unsigned TownGrassLodSide = 256;

/** One immutable color atlas/LUT for all admitted floor pieces, never per cell.
 * Reset belongs to the host's level/CEL resource epoch. Palette/light changes
 * publish a new owned material; borrowed input spans never survive this call. */
struct TownGroundMaterialCache {
	bool resourceAttempted = false;
	bool preparationAttempted = false;
	std::vector<uint8_t> rgb;
	std::shared_ptr<const PlayerActorMaterial> material;
	std::array<uint32_t, 256> palette {};
	TownLightingConfig light;
	std::string failure;
};

bool PrepareTownGroundMaterial(TownGroundMaterialCache &cache,
    std::span<const uint32_t> palette, const TownLightingConfig &light);

/** Semantic admission is explicit pure grass0..3, never SOL or RGB thresholds.
 * Inherited fallback is allowed only in the4122 schema5 donor40,40/after0 cells.
 * The caller additionally checks native/source identity, presentation flags,
 * special/event state and identity transform. Guards stay native. */
bool TownGroundMaterialAllowsCell(Point tile, uint16_t piece, bool fallback);
bool TownGroundMaterialSourceMatches(uint16_t piece, int width, int height,
    bool repeat, std::span<const uint8_t> pixels, std::span<const uint8_t> opacity);

/** World coordinates are native cells, matching the geometry/depth contract.
 * Coverage is sampled separately in the original64x32 UV domain by the host. */
bool SampleTownGroundMaterial(const PlayerActorMaterial &material,
    float worldX, float worldZ, uint32_t &code);


/** Shared future2D/3D route datum, in the same native world coordinates.
 * A zero-count band is disabled. The caller must additionally admit each cell
 * against an explicit route allowlist; this field never classifies SOL. */
struct TownRoadBandPoint { float x = 0, z = 0; };
struct TownRoadClipPlane { bool enabled = false; float normalX = 0, normalZ = 1, offset = 0; };
struct TownRoadBand {
	std::array<TownRoadBandPoint, 64> points {};
	unsigned count = 0;
	float halfWidth = 1;
	float feather = 0.25F;
	float irregularity = 0.10F;
	uint32_t visualSeed = 0x54524F41U;
	std::array<TownRoadClipPlane, 2> clipPlanes {};
};
float SampleTownRoadBand(const TownRoadBand &band, float worldX, float worldZ);
/** Optional coarse color overlay coverage, not ground ownership. Input is the
 * exact native64x32 opacity. Caller explicitly admits the route cell; unknown
 * water/bridge/architecture/event cells stay unadmitted. Both2D/3D consume these
 * same mask bytes at native UV. Stateless dither softens the band feather. */
// Preparation distinguishes an authenticated no-fragment edge cell from invalid input/OOM.
enum class TownRoadCoverageStatus : uint8_t { Invalid, Empty, Ready };
TownRoadCoverageStatus PrepareTownRoadCellCoverage(const TownRoadBand &band, Point tile, bool routeCellAdmitted,
    std::span<const uint8_t> nativeOpacity, std::vector<uint8_t> &mask);
// Legacy bool remains true only for nonempty coverage.
bool BuildTownRoadCellCoverage(const TownRoadBand &band, Point tile, bool routeCellAdmitted,
    std::span<const uint8_t> nativeOpacity, std::vector<uint8_t> &mask);
/** Original small earthy albedo,64x64/2m, generated from integer visual hashes.
 * No external image/native pixel, simulation RNG, normal/roughness or geometry.
 * Returns an ordinary shared indexed/RGBA diffuse material; not selected here. */
bool PrepareTownRoadMaterial(std::span<const uint32_t> palette,
    const TownLightingConfig &light, PlayerActorMaterial &output, std::string &failure);

bool PrepareTownRoadMaterial(TownGroundMaterialCache &cache,
    std::span<const uint32_t> palette, const TownLightingConfig &light);
const TownRoadBand &GetTownEntranceRoadBand();
bool TownEntranceRoadCellSource(Point tile, uint16_t nativePiece, uint16_t special);
bool TownEntranceRoadUsesVisualDonor(Point tile);
bool TownEntranceRoadTextureMatches(Point tile, int width, int height, bool repeat,
    std::span<const uint8_t> pixels, std::span<const uint8_t> opacity);

} // namespace devilution
