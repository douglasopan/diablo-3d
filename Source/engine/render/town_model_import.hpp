#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "engine/render/town_scene.hpp"

namespace devilution {

/** Authored RGB base color; lighting precedes the runtime's final palette mapping. */
struct TownImportedTexture {
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> rgb;
};

inline constexpr std::string_view TownCabinBaselineSha256 = "d12cc57e798151fb4abf3173149a4d7cf1da2cc8bdbc6ac39439aee3fe9069f9";

/**
 * Optional local model override. Missing or malformed assets leave the existing
 * model untouched. Call after its procedural builder has assigned native metadata.
 * FindAsset/OpenAsset honor normal profile/asset overrides; no archive is changed.
 * A nonempty expected hash is enforced before committing. The optional attempt
 * audit reports rejected bytes separately from an already loaded model identity.
 */
bool LoadTownModelOverride(TownSceneModel &model, std::string_view assetPath,
    std::string_view expectedSha256 = {}, std::string_view revision = {}, TownModelRuntimeAudit *attemptAudit = nullptr);

/**
 * D3DMESH1 binary contract (all numbers little-endian, no struct padding):
 *   8 bytes "D3DMESH1"; uint32 triangleCount, width, height;
 *   triangleCount * 15 float32: three vertices (x,height,z,u,v);
 *   width * height * 3 uint8: row-major RGB, top row first.
 * x/z are relative to model.minTile; height is above the native ground plane.
 * UVs are normalized, use the same top-row-first image convention, and are kept.
 * Limits: 1..20000 triangles, 1..2048 pixels per side, local x/z -64..128,
 * height 0..64, finite nondegenerate triangles, exact byte count and UVs 0..1.
 * This replaces visual geometry only; native bounds/collision/map stay intact.
 * Success records SHA-256 of this exact span; failure leaves all model fields intact.
 */
bool ParseTownModelOverride(TownSceneModel &model, std::span<const std::byte> data);

} // namespace devilution
