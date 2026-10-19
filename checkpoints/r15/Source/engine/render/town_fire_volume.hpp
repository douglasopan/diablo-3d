#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace devilution {

inline constexpr unsigned TownFireVolumeMaxSteps = 32;
inline constexpr double TownFireVolumeMaxSeconds = 0x1p32;
inline constexpr float TownFireVolumeMaxRayDistance = 4096.0F;

struct TownFireVolumeVec3 {
	float x = 0;
	float height = 0;
	float z = 0;
};

struct TownFireVolumeRgb {
	float red = 0;
	float green = 0;
	float blue = 0;
};

struct TownFireVolumeConfig {
	// Local axes are native world tiles, with height up and the bowl at zero.
	float radius = 0.18F;
	float height = 0.75F;
	float smokeHeight = 0.20F;
	float flameAbsorption = 3.0F;
	float flameEmission = 5.0F;
	float smokeAbsorption = 0.90F;
	float smokeDensity = 0.08F;
	float softIntersection = 0.025F;
	// Pass actual TownFireLightAtTime intensity, normally 3 +/- 6 percent.
	float referenceEnergy = 3.0F;
};

struct TownFireVolumeBounds {
	TownFireVolumeVec3 minimum;
	TownFireVolumeVec3 maximum;
};

struct TownFireVolumeFieldSample {
	// j: emitted linear radiance per tile; sigma: extinction per tile.
	TownFireVolumeRgb emission;
	float absorption = 0;
	float flameDensity = 0;
	float smokeDensity = 0;
	float temperatureKelvin = 0;
	bool valid = false;
};

struct TownFireVolumeRayResult {
	// Premultiplied emitted linear RGB over black; Over = rgb + (1-alpha)*bg.
	TownFireVolumeRgb premultiplied;
	float alpha = 0;
	unsigned steps = 0;
	bool valid = false;
};

namespace town_fire_volume_detail {

inline bool Bounded(float value, float low, float high)
{
	return std::isfinite(value) && value >= low && value <= high;
}

inline bool Finite(TownFireVolumeVec3 value)
{
	return std::isfinite(value.x) && std::isfinite(value.height) && std::isfinite(value.z);
}

inline bool ValidTimeEnergy(double seconds, float energy)
{
	return std::isfinite(seconds) && seconds >= 0 && seconds <= TownFireVolumeMaxSeconds
	    && Bounded(energy, 0, 16);
}

inline double Smooth(double low, double high, double value)
{
	const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

constexpr uint32_t Hash(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	return value ^ (value >> 16);
}

inline double Lattice(int x, int y, int z, uint32_t identity)
{
	// Conversion to unsigned and all multiplication/overflow are defined.
	// The 64-cell period also closes the advected 64-second time loop.
	const uint32_t value = identity ^ ((static_cast<uint32_t>(x) & 63U) * 0x9E3779B9U)
	    ^ ((static_cast<uint32_t>(y) & 63U) * 0x85EBCA6BU)
	    ^ ((static_cast<uint32_t>(z) & 63U) * 0xC2B2AE35U);
	return static_cast<double>(Hash(value) >> 8) * 0x1p-24;
}

inline double Noise(double x, double y, double z, uint32_t identity)
{
	// Private callers prove coordinates within [-1024,1024] before this cast:
	// bounded local coordinates/frequencies plus time wrapped to [0,64).
	const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
	const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
	const auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
	const double u = fade(x - fx), v = fade(y - fy), w = fade(z - fz);
	const auto mix = [](double a, double b, double t) { return a + (b - a) * t; };
	const double low = mix(mix(Lattice(ix, iy, iz, identity), Lattice(ix + 1, iy, iz, identity), u),
	    mix(Lattice(ix, iy + 1, iz, identity), Lattice(ix + 1, iy + 1, iz, identity), u), v);
	const double high = mix(mix(Lattice(ix, iy, iz + 1, identity), Lattice(ix + 1, iy, iz + 1, identity), u),
	    mix(Lattice(ix, iy + 1, iz + 1, identity), Lattice(ix + 1, iy + 1, iz + 1, identity), u), v);
	return mix(low, high, w);
}

inline bool UnitDirection(TownFireVolumeVec3 direction)
{
	if (!Finite(direction)) return false;
	const double length = static_cast<double>(direction.x) * direction.x
	    + static_cast<double>(direction.height) * direction.height + static_cast<double>(direction.z) * direction.z;
	return length >= 0.999 && length <= 1.001;
}

struct Context {
	double time;
	double phase;
	double offset;
	double energyRatio;
	double flameHeight;
	double densityEnergy;
	double smokeSwayX;
	double smokeSwayZ;
};

inline Context MakeContext(const TownFireVolumeConfig &config, double seconds,
    uint32_t identity, float energy)
{
	constexpr double Tau = 6.28318530717958647692;
	const double time = std::fmod(seconds, 64.0), phase = Tau * time / 64.0;
	const double offset = Tau * static_cast<double>(Hash(identity) & 65535U) / 65536.0;
	const double ratio = static_cast<double>(energy) / config.referenceEnergy;
	return { time, phase, offset, ratio, 0.93 + 0.07 * std::clamp(ratio, 0.0, 1.0),
		std::sqrt(std::clamp(ratio, 0.0, 1.0)),
		0.15 * std::sin(phase * 11 + offset), 0.15 * std::cos(phase * 13 + offset) };
}

inline TownFireVolumeFieldSample Sample(const TownFireVolumeConfig &config,
    TownFireVolumeVec3 local, const Context &context, uint32_t identity)
{
	TownFireVolumeFieldSample result;
	result.valid = true;
	if (context.energyRatio == 0 || local.height < 0 || local.height > config.height + config.smokeHeight
	    || std::abs(local.x) > config.radius * 1.8F || std::abs(local.z) > config.radius * 1.8F)
		return result;
	const double time = context.time, phase = context.phase, offset = context.offset;
	const double y = static_cast<double>(local.height) / config.height;
	const double fy = y / context.flameHeight;
	const double crossAmplitude = 0.20 * std::min(y, 1.4);
	const double swayAx = crossAmplitude * std::sin(phase * 17 + offset + y * 3.1);
	const double swayAz = crossAmplitude * std::sin(phase * 23 + offset * 1.7 - y * 2.3);
	const double swayBx = crossAmplitude * std::sin(phase * 19 + offset * 1.3 - y * 2.7 + 1.5707963267948966);
	const double swayBz = crossAmplitude * std::sin(phase * 13 + offset * 0.7 + y * 2.1 + 1.5707963267948966);
	const double xA = static_cast<double>(local.x) / config.radius - swayAx;
	const double zA = static_cast<double>(local.z) / config.radius - swayAz;
	const double xB = static_cast<double>(local.x) / config.radius - swayBx;
	const double zB = static_cast<double>(local.z) / config.radius - swayBz;
	const double x = (xA + xB) * 0.5;
	const double z = (zA + zB) * 0.5;
	// Upward advection and domain warping; all three scales are continuous3D,
	// independent of camera/frames. Integer velocities close the noise period.
	const double warp = Noise(x * 1.5, y * 2.5 - time, z * 1.5, identity ^ 0xA511E9B3U) - 0.5;
	const double noise = 0.60 * Noise(x * 2 + warp * 0.65, y * 4 - time * 2, z * 2 - warp * 0.65, identity)
	    + 0.28 * Noise(x * 4.2, y * 8 - time * 4, z * 4.2, identity ^ 0x68E31DA4U)
	    + 0.12 * Noise(x * 8.4, y * 16 - time * 6, z * 8.4, identity ^ 0xB5297A4DU);
	double flame = 0, heat = 0;
	if (fy < 1) {
		const double taper = 0.04 + 0.96 * std::pow(std::max(0.0, 1 - fy), 0.65);
		const double radialA = (xA * xA + zA * zA) / (taper * taper);
		const double radialB = (xB * xB + zB * zB) / (taper * taper);
		const double vertical = Smooth(0, 0.055, fy) * (1 - Smooth(0.78, 1.0, fy)) * (1 - fy * 0.40) * context.densityEnergy;
		const double flameA = Smooth(-0.15, 0.70, 1 - radialA + 0.50 * (noise - 0.5)) * vertical;
		const double flameB = Smooth(-0.15, 0.70, 1 - radialB + 0.50 * (noise - 0.5)) * vertical;
		const double radial = flameB * 0.82 > flameA ? radialB : radialA;
		flame = std::max(flameA, flameB * 0.82);
		heat = std::clamp((1 - std::clamp(radial, 0.0, 1.0)) * 0.65
		        + (1 - fy) * 0.35 + (noise - 0.5) * 0.12, 0.0, 1.0);
	}
	double smoke = 0;
	if (config.smokeHeight > 0 && y > 0.55) {
		const double age = (local.height - config.height * 0.55)
		    / (config.height * 0.45 + config.smokeHeight);
		const double radius = 0.42 + age * 0.70;
		const double smokeX = x - age * context.smokeSwayX;
		const double smokeZ = z - age * context.smokeSwayZ;
		const double radial = (smokeX * smokeX + smokeZ * smokeZ) / (radius * radius);
		smoke = config.smokeDensity * Smooth(0, 0.20, age) * (1 - Smooth(0.60, 1, age))
		    * Smooth(-0.10, 0.80, 1 - radial + (noise - 0.5) * 0.55) * (1 - flame * 0.70) * context.densityEnergy;
	}
	const double sigmaFlame = config.flameAbsorption * flame;
	const double sigmaSmoke = config.smokeAbsorption * smoke;
	const double emission = config.flameEmission * flame * context.energyRatio;
	// Orange body transitions into yellow tips in linear emission space.
	const double yellowTip = Smooth(0.60, 0.92, fy);
	const double green = (0.26 + 0.18 * heat) * (1 - yellowTip) + 0.86 * yellowTip;
	const double blue = (0.008 + 0.018 * heat) * (1 - yellowTip) + 0.10 * yellowTip;
	result.emission = { static_cast<float>(emission + sigmaSmoke * 0.012),
		static_cast<float>(emission * green + sigmaSmoke * 0.010),
		static_cast<float>(emission * blue + sigmaSmoke * 0.008) };
	result.absorption = static_cast<float>(sigmaFlame + sigmaSmoke);
	result.flameDensity = static_cast<float>(flame);
	result.smokeDensity = static_cast<float>(smoke);
	result.temperatureKelvin = flame > 0 ? static_cast<float>(1000 + 1400 * heat) : 0;
	return result;
}

} // namespace town_fire_volume_detail

inline bool IsValidTownFireVolumeConfig(const TownFireVolumeConfig &config)
{
	using town_fire_volume_detail::Bounded;
	return Bounded(config.radius, 0.02F, 1) && Bounded(config.height, 0.10F, 4)
	    && Bounded(config.smokeHeight, 0, 0.5F) && Bounded(config.flameAbsorption, 0, 16)
	    && Bounded(config.flameEmission, 0, 16) && Bounded(config.smokeAbsorption, 0, 4)
	    && Bounded(config.smokeDensity, 0, 0.25F) && Bounded(config.softIntersection, 0, 0.25F)
	    && Bounded(config.referenceEnergy, 0.25F, 16);
}

inline TownFireVolumeBounds GetTownFireVolumeBounds(const TownFireVolumeConfig &config)
{
	if (!IsValidTownFireVolumeConfig(config)) return {};
	const float extent = config.radius * 1.8F;
	return { { -extent, 0, -extent }, { extent, config.height + config.smokeHeight, extent } };
}

/** Pure bounded local field. Invalid config/time/energy/nonfinite coordinates
 * return valid=false; outside the support and energy0 are valid transparent.
 * Identity must be the stable visual emitter identity; the host owns epoch/slot.
 * Seconds are absolute visual time [0,2^32], not a simulation tick or delta. */
inline TownFireVolumeFieldSample SampleTownFireVolumeField(const TownFireVolumeConfig &config,
    TownFireVolumeVec3 local, double seconds, uint32_t identity, float energy)
{
	if (!IsValidTownFireVolumeConfig(config) || !town_fire_volume_detail::Finite(local)
	    || !town_fire_volume_detail::ValidTimeEnergy(seconds, energy))
		return {};
	return town_fire_volume_detail::Sample(config, local,
	    town_fire_volume_detail::MakeContext(config, seconds, identity, energy), identity);
}

/** Ray/AABB slabs; ray direction is unit-length (squared tolerance .001).
 * Inputs/outputs t are tile distances, not native camera axial-depth values.
 * False means no admitted intersection or invalid input; outputs stay intact.
 * maxT accepts positive infinity for unobstructed sky and bounds it to4096. */
inline bool IntersectTownFireVolumeBounds(const TownFireVolumeConfig &config,
    TownFireVolumeVec3 origin, TownFireVolumeVec3 direction, float minT, float maxT,
    float &entry, float &exit)
{
	using namespace town_fire_volume_detail;
	if (!IsValidTownFireVolumeConfig(config) || !Finite(origin) || !UnitDirection(direction)
	    || std::abs(origin.x) > TownFireVolumeMaxRayDistance || std::abs(origin.height) > TownFireVolumeMaxRayDistance
	    || std::abs(origin.z) > TownFireVolumeMaxRayDistance || !Bounded(minT, 0, TownFireVolumeMaxRayDistance)
	    || std::isnan(maxT) || maxT < minT)
		return false;
	double near = minT, far = std::min(maxT, TownFireVolumeMaxRayDistance);
	const auto bounds = GetTownFireVolumeBounds(config);
	const std::array<float, 3> o { origin.x, origin.height, origin.z }, d { direction.x, direction.height, direction.z };
	const std::array<float, 3> lo { bounds.minimum.x, bounds.minimum.height, bounds.minimum.z }, hi { bounds.maximum.x, bounds.maximum.height, bounds.maximum.z };
	for (size_t axis = 0; axis < 3; ++axis) {
		if (std::abs(d[axis]) < 1.0e-8F) {
			if (o[axis] < lo[axis] || o[axis] > hi[axis]) return false;
			continue;
		}
		double a = (static_cast<double>(lo[axis]) - o[axis]) / d[axis];
		double b = (static_cast<double>(hi[axis]) - o[axis]) / d[axis];
		if (a > b) std::swap(a, b);
		near = std::max(near, a); far = std::min(far, b);
		if (near >= far) return false;
	}
	const float nextEntry = static_cast<float>(near), nextExit = static_cast<float>(far);
	if (!(nextEntry < nextExit)) return false;
	entry = nextEntry; exit = nextExit;
	return true;
}

/** Front-to-back Beer-Lambert emission/extinction, at most32 midpoint samples.
 * maxT MUST be distance to the actual opaque world receiver (or +infinity),
 * before AABB clipping. This endpoint also supplies the soft intersection.
 * The host converts axial native depth to ray distance; a billboard/old flame
 * depth is not an opaque world receiver. Directions must already be normalized.
 * No background, depth/pick/index/alpha, native state, SDL or GPU is mutated.
 * Result.valid distinguishes malformed input from valid empty/occluded rays.
 * Steps>32 clamp to32; zero is invalid. No allocation, throws or retained state.
 */
inline TownFireVolumeRayResult IntegrateTownFireVolumeRay(const TownFireVolumeConfig &config,
    TownFireVolumeVec3 origin, TownFireVolumeVec3 direction, float minT, float maxT,
    double seconds, uint32_t identity, float energy, unsigned maxSteps = 24)
{
	using namespace town_fire_volume_detail;
	TownFireVolumeRayResult result;
	if (!IsValidTownFireVolumeConfig(config) || !Finite(origin) || !UnitDirection(direction)
	    || std::abs(origin.x) > TownFireVolumeMaxRayDistance || std::abs(origin.height) > TownFireVolumeMaxRayDistance
	    || std::abs(origin.z) > TownFireVolumeMaxRayDistance || !Bounded(minT, 0, TownFireVolumeMaxRayDistance)
	    || std::isnan(maxT) || maxT < minT || !ValidTimeEnergy(seconds, energy) || maxSteps == 0)
		return result;
	result.valid = true;
	if (energy == 0 || maxT == minT) return result;
	float entry, exit;
	if (!IntersectTownFireVolumeBounds(config, origin, direction, minT, maxT, entry, exit)) return result;
	const unsigned count = std::min(maxSteps, TownFireVolumeMaxSteps);
	const double step = (static_cast<double>(exit) - entry) / count;
	const auto context = MakeContext(config, seconds, identity, energy);
	double transmittance = 1, red = 0, green = 0, blue = 0;
	for (unsigned i = 0; i < count; ++i) {
		const double distance = entry + (static_cast<double>(i) + 0.5) * step;
		const TownFireVolumeVec3 local { static_cast<float>(origin.x + direction.x * distance),
			static_cast<float>(origin.height + direction.height * distance), static_cast<float>(origin.z + direction.z * distance) };
		const auto sample = Sample(config, local, context, identity);
		const double intersection = config.softIntersection > 0
		    ? Smooth(0, config.softIntersection, static_cast<double>(maxT) - distance) : 1;
		const double sigma = sample.absorption * intersection;
		const double opacity = -std::expm1(-sigma * step);
		// Analytic homogeneous segment integral, stable as sigma approaches zero.
		const double factor = transmittance * intersection * (sigma > 1.0e-12 ? opacity / sigma : step);
		red += sample.emission.red * factor;
		green += sample.emission.green * factor;
		blue += sample.emission.blue * factor;
		transmittance *= 1 - opacity;
		++result.steps;
	}
	result.premultiplied = { static_cast<float>(red), static_cast<float>(green), static_cast<float>(blue) };
	result.alpha = static_cast<float>(std::clamp(1 - transmittance, 0.0, 1.0));
	return result;
}

} // namespace devilution
