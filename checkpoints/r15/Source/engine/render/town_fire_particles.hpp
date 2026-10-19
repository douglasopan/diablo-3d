#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "engine/render/town_lighting.hpp"

namespace devilution {

inline constexpr size_t TownMaxFireParticlesPerEmitter = 8;
// The caller shares this budget across all emitters in one accepted frame.
inline constexpr size_t TownMaxFireParticlesPerFrame = 128;
// Keeping the quotient below 2^36 makes every float-to-integer cycle conversion
// defined, including long-running sessions. Invalid times fail closed.
inline constexpr double TownMaxFireParticleRenderSeconds = 0x1p32;
inline constexpr float TownMinFireParticleLifetimeSeconds = 0.125F;
inline constexpr float TownMaxFireParticleLifetimeSeconds = 8.0F;

struct TownFireParticleConfig {
	unsigned particleCount = 6;
	float lifetimeSeconds = 1.6F;
	float spawnRadius = 0.06F;
	float riseHeight = 0.9F;
	float driftRadius = 0.14F;
	float minSize = 0.009F;
	float maxSize = 0.022F;
	float intensity = 0.7F;
};

struct TownFireParticle {
	TownLightVector position;
	// World-space diameter; the compositor decides projected coverage.
	float size = 0;
	// Linear RGB self-emission, multiplied by intensity at composition time.
	TownLightColor color;
	float intensity = 0;
	float normalizedAge = 0;
};

namespace town_fire_particles_detail {

constexpr uint32_t Hash(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	return value ^ (value >> 16);
}

constexpr float Unit(uint32_t value)
{
	return static_cast<float>(Hash(value) >> 8) * 0x1p-24F;
}

constexpr double SignedUnit(uint32_t value)
{
	return 2.0 * Unit(value) - 1.0;
}

inline bool Finite(TownLightVector value)
{
	return std::isfinite(value.x) && std::isfinite(value.height) && std::isfinite(value.z);
}

inline bool ValidConfig(const TownFireParticleConfig &config)
{
	return std::isfinite(config.lifetimeSeconds)
	    && config.lifetimeSeconds >= TownMinFireParticleLifetimeSeconds
	    && config.lifetimeSeconds <= TownMaxFireParticleLifetimeSeconds
	    && std::isfinite(config.spawnRadius) && config.spawnRadius >= 0 && config.spawnRadius <= 1
	    && std::isfinite(config.riseHeight) && config.riseHeight >= 0 && config.riseHeight <= 4
	    && std::isfinite(config.driftRadius) && config.driftRadius >= 0 && config.driftRadius <= 2
	    && std::isfinite(config.minSize) && config.minSize >= 0x1p-12F
	    && std::isfinite(config.maxSize) && config.maxSize >= config.minSize && config.maxSize <= 0.25F
	    && std::isfinite(config.intensity) && config.intensity >= 0 && config.intensity <= 4;
}

constexpr double SmoothStep(double value)
{
	value = std::clamp(value, 0.0, 1.0);
	return value * value * (3.0 - 2.0 * value);
}

} // namespace town_fire_particles_detail

/** Sample visual embers from absolute render time, without state or allocation.
 * Identity, slot and lifetime cycle seed an integer hash; no simulation RNG,
 * native animation clock, camera, SDL or GPU context is read or modified.
 * A fixed timestamp repeats exactly on the same platform, regardless of call
 * order or preceding frame cadence. Trajectories are continuous within a life;
 * intensity fades to zero at birth/death before that slot changes its seed.
 *
 * Source coordinates and all config floats must be finite. Lifetime is
 * [0.125,8] seconds, spawnRadius [0,1], riseHeight [0,4], driftRadius [0,2],
 * sizes [1/4096,0.25] with minSize <= maxSize, and intensity [0,4]. Count is
 * clamped to 8 and output capacity. Time is [0,TownMaxFireParticleRenderSeconds].
 * Invalid input returns zero without writing output; unused slots are untouched.
 * A valid slot is returned even when its faded intensity is zero, preserving
 * stable slot ordering and prefix truncation under the caller's global budget.
 *
 * Lifecycle: sample anew for the current frame/epoch. This helper retains no
 * cache, resources or borrowed references and needs no reset/dispose call.
 * The caller must discard its sampled frame on refusal, level/resource reset
 * or disposal and compose only with that frame's matching color/depth/pick.
 */
inline size_t SampleTownFireParticles(TownLightVector source, uint32_t emitterIdentity,
    double renderSeconds, const TownFireParticleConfig &config, std::span<TownFireParticle> output)
{
	using namespace town_fire_particles_detail;
	if (!Finite(source) || !ValidConfig(config) || !std::isfinite(renderSeconds)
	    || renderSeconds < 0 || renderSeconds > TownMaxFireParticleRenderSeconds)
		return 0;

	const size_t count = std::min({ static_cast<size_t>(config.particleCount), TownMaxFireParticlesPerEmitter, output.size() });
	if (count == 0)
		return 0;
	std::array<TownFireParticle, TownMaxFireParticlesPerEmitter> sampled;
	for (size_t slot = 0; slot < count; ++slot) {
		const uint32_t slotSeed = Hash(emitterIdentity ^ (0x9E3779B9U * (static_cast<uint32_t>(slot) + 1)));
		const double lifetime = config.lifetimeSeconds;
		const double phaseTime = renderSeconds / lifetime + Unit(slotSeed ^ 0xA511E9B3U);
		const double cycleValue = std::floor(phaseTime);
		const uint64_t cycle = static_cast<uint64_t>(cycleValue);
		const double age = phaseTime - cycleValue;
		// Both halves participate so this does not repeat at a 32-bit cycle wrap.
		const uint32_t seed = Hash(slotSeed ^ Hash(static_cast<uint32_t>(cycle))
		    ^ Hash(static_cast<uint32_t>(cycle >> 32) ^ 0x63D83595U));
		constexpr double InverseSqrtTwo = 0.70710678118654752440;
		const double sway = 4.0 * age * (1.0 - age);
		const auto horizontalOffset = [&](uint32_t salt) {
			const double start = config.spawnRadius * SignedUnit(seed ^ salt);
			const double drift = config.driftRadius * (0.65 * age * SignedUnit(seed ^ (salt + 1))
			    + 0.35 * sway * SignedUnit(seed ^ (salt + 2)));
			return InverseSqrtTwo * (start + drift);
		};
		const double x = static_cast<double>(source.x) + horizontalOffset(0xB5297A4DU);
		const double height = static_cast<double>(source.height) + config.riseHeight * age;
		const double z = static_cast<double>(source.z) + horizontalOffset(0x1B56C4E9U);
		constexpr double MaxCoordinate = std::numeric_limits<float>::max();
		if (std::abs(x) > MaxCoordinate || std::abs(height) > MaxCoordinate || std::abs(z) > MaxCoordinate)
			return 0;

		TownFireParticle &particle = sampled[slot];
		particle.position = { static_cast<float>(x), static_cast<float>(height), static_cast<float>(z) };
		particle.size = config.minSize + (config.maxSize - config.minSize) * Unit(seed ^ 0x68E31DA4U);
		// Rounding a double immediately before death must not publish age == 1.
		particle.normalizedAge = std::min(static_cast<float>(age), 0x1.fffffep-1F);
		const float cooling = static_cast<float>(1.0 - age);
		particle.color = { 0.8F + 0.2F * cooling,
			(0.20F + 0.25F * Unit(seed ^ 0xC2B2AE35U)) * (0.5F + 0.5F * cooling),
			(0.006F + 0.014F * Unit(seed ^ 0x27D4EB2FU)) * cooling };
		const double fade = SmoothStep(age / 0.10) * SmoothStep((1.0 - age) / 0.30);
		particle.intensity = static_cast<float>(config.intensity * fade * (1.0 - 0.25 * age));
	}
	std::copy_n(sampled.begin(), count, output.begin());
	return count;
}

} // namespace devilution
