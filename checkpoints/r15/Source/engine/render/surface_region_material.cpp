#include "engine/render/surface_region_material.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace devilution {
namespace {

// Publication identity is rendering metadata only. Its value may depend on
// preparation order; it never controls geography, gameplay RNG or the clock.
// Never reset or wrap it.
std::atomic<uint64_t> NextMaterialPublication { 1 };

bool LightFinite(const TownLightingConfig &light)
{
	return std::isfinite(light.ambient.red) && std::isfinite(light.ambient.green)
	    && std::isfinite(light.ambient.blue) && std::isfinite(light.directional.red)
	    && std::isfinite(light.directional.green) && std::isfinite(light.directional.blue)
	    && std::isfinite(light.toLight.x) && std::isfinite(light.toLight.height)
	    && std::isfinite(light.toLight.z) && std::isfinite(light.directionalIntensity);
}

bool MaterialComplete(const PlayerActorMaterial &material, uint32_t side)
{
	return material.width == side && material.height == side
	    && material.lightLevels == 64 // Both original factories use TownDirectional.
	    && material.texelCodes.size() == static_cast<size_t>(side) * side
	    && !material.sourceColors.empty() && material.sourceColors.size() <= 65536
	    && material.indexedLightLut.size() == material.sourceColors.size() * material.lightLevels
	    && material.rgbaLightLut.size() == material.sourceColors.size() * material.lightLevels;
}

uint64_t NewPublication()
{
	uint64_t current = NextMaterialPublication.load(std::memory_order_relaxed);
	while (current != std::numeric_limits<uint64_t>::max()) {
		if (NextMaterialPublication.compare_exchange_weak(current, current + 1,
		        std::memory_order_relaxed, std::memory_order_relaxed))
			return current;
	}
	return 0;
}

} // namespace

void ResetSurfaceRegionGroundMaterials(SurfaceRegionGroundMaterialCache &cache)
{
	cache = {};
}

bool PrepareSurfaceRegionGroundMaterials(SurfaceRegionGroundMaterialCache &cache,
    uint64_t resourceEpoch, std::span<const uint32_t> palette,
    const TownLightingConfig &light, SurfaceRegionGroundMaterials &output,
    std::string &failure)
{
	output = {};
	failure.clear();
	if (resourceEpoch == 0 || palette.size() != 256 || !LightFinite(light)) {
		cache.published = {};
		failure = "surface-material-invalid-context";
		return false;
	}
	if (cache.resourceEpoch != resourceEpoch) {
		ResetSurfaceRegionGroundMaterials(cache);
		cache.resourceEpoch = resourceEpoch;
	}
	// These are the original grass and authored-soil factories, including the
	// original asset authentication, palette lighting and allocation budgets.
	if (!PrepareTownGroundMaterial(cache.grass, palette, light)) {
		cache.published = {};
		failure = cache.grass.failure.empty() ? "surface-grass-refused" : cache.grass.failure;
		return false;
	}
	if (!PrepareTownRoadMaterial(cache.soil, palette, light)) {
		cache.published = {};
		failure = cache.soil.failure.empty() ? "surface-soil-refused" : cache.soil.failure;
		return false;
	}
	if (!cache.grass.material || !cache.soil.material
	    || !MaterialComplete(*cache.grass.material, TownGrassLodSide)
	    || !MaterialComplete(*cache.soil.material, 64)) {
		cache.published = {};
		failure = "surface-material-incomplete";
		return false;
	}
	if (cache.published.grass != cache.grass.material
	    || cache.published.soil != cache.soil.material
	    || cache.published.revision == 0) {
		const uint64_t revision = NewPublication();
		if (revision == 0) {
			cache.published = {};
			failure = "surface-material-publication-exhausted";
			return false;
		}
		cache.published = { cache.grass.material, cache.soil.material, revision };
	}
	output = cache.published;
	return true;
}

bool SampleSurfaceRegionGroundMaterial(const PlayerActorMaterial &material,
    float localNativeX, float localNativeZ, float offsetCyclesU,
    float offsetCyclesV, uint32_t &code)
{
	if (!std::isfinite(localNativeX) || !std::isfinite(localNativeZ)
	    || !std::isfinite(offsetCyclesU) || !std::isfinite(offsetCyclesV)
	    || std::abs(localNativeX) > SurfaceRegionMaterialMaxLocalCoordinate
	    || std::abs(localNativeZ) > SurfaceRegionMaterialMaxLocalCoordinate
	    || std::abs(offsetCyclesU) > 8192 || std::abs(offsetCyclesV) > 8192
	    || (material.width != TownGrassLodSide && material.width != 64)
	    || material.height != material.width
	    || material.texelCodes.size() != static_cast<size_t>(material.width) * material.height)
		return false;
	float u = localNativeX * TownGrassWorldUvScale + offsetCyclesU;
	float v = localNativeZ * TownGrassWorldUvScale + offsetCyclesV;
	u -= std::floor(u);
	v -= std::floor(v);
	const auto x = std::min(static_cast<unsigned>(u * material.width), material.width - 1);
	const auto z = std::min(static_cast<unsigned>(v * material.height), material.height - 1);
	const uint32_t sample = material.texelCodes[static_cast<size_t>(z) * material.width + x];
	if (sample >= material.sourceColors.size())
		return false;
	code = sample;
	return true;
}

} // namespace devilution
