#include "engine/render/town_lighting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace devilution {
namespace {

constexpr float BoundaryEpsilon = 0.0001F;
constexpr float SegmentEndpointEpsilon = 0.00001F;

bool Finite(TownLightVector vector)
{
	return std::isfinite(vector.x) && std::isfinite(vector.height) && std::isfinite(vector.z)
	    && std::abs(vector.x) <= 1000000 && std::abs(vector.height) <= 1000000 && std::abs(vector.z) <= 1000000;
}

float Dot(TownLightVector a, TownLightVector b)
{
	return a.x * b.x + a.height * b.height + a.z * b.z;
}

TownLightVector Difference(TownLightVector a, TownLightVector b)
{
	return { a.x - b.x, a.height - b.height, a.z - b.z };
}

bool Normalize(TownLightVector &vector)
{
	if (!Finite(vector))
		return false;
	const float lengthSquared = Dot(vector, vector);
	if (lengthSquared <= 0.00000001F)
		return false;
	const float scale = 1 / std::sqrt(lengthSquared);
	vector.x *= scale;
	vector.height *= scale;
	vector.z *= scale;
	return true;
}

float Positive(float value, float maximum = 16)
{
	return std::isfinite(value) ? std::clamp(value, 0.0F, maximum) : 0;
}

TownLightColor Positive(TownLightColor color)
{
	return { Positive(color.red), Positive(color.green), Positive(color.blue) };
}

TownLightColor Scale(TownLightColor color, float value)
{
	return { color.red * value, color.green * value, color.blue * value };
}

TownLightColor Add(TownLightColor a, TownLightColor b)
{
	return { a.red + b.red, a.green + b.green, a.blue + b.blue };
}

bool ValidAperture(const TownLightAperture &aperture)
{
	return (aperture.plane == TownLightPlane::X || aperture.plane == TownLightPlane::Height || aperture.plane == TownLightPlane::Z)
	    && std::isfinite(aperture.coordinate) && std::isfinite(aperture.minU) && std::isfinite(aperture.maxU)
	    && std::isfinite(aperture.minV) && std::isfinite(aperture.maxV)
	    && aperture.minU < aperture.maxU && aperture.minV < aperture.maxV
	    && (aperture.polygonSides == 0 || (aperture.polygonSides >= 3 && aperture.polygonSides <= 32));
}

bool InsideAperturePolygon(float u, float v, const TownLightAperture &aperture)
{
	if (aperture.polygonSides == 0)
		return true;
	// Double intermediates avoid overflow when finite float bounds straddle
	// zero. Every valid regular aperture has a fixed, at-most-32-edge budget.
	const double radiusU = (static_cast<double>(aperture.maxU) - aperture.minU) * 0.5;
	const double radiusV = (static_cast<double>(aperture.maxV) - aperture.minV) * 0.5;
	const double centerU = (static_cast<double>(aperture.maxU) + aperture.minU) * 0.5;
	const double centerV = (static_cast<double>(aperture.maxV) + aperture.minV) * 0.5;
	const float normalizedU = static_cast<float>((u - centerU) / radiusU);
	const float normalizedV = static_cast<float>((v - centerV) / radiusV);
	constexpr float Pi = 3.14159265358979323846F;
	const float sideDistance = std::cos(Pi / static_cast<float>(aperture.polygonSides));
	for (unsigned side = 0; side < aperture.polygonSides; ++side) {
		const float angle = 2 * Pi * (static_cast<float>(side) + 0.5F) / static_cast<float>(aperture.polygonSides);
		if (normalizedU * std::cos(angle) + normalizedV * std::sin(angle) >= sideDistance - BoundaryEpsilon)
			return false;
	}
	return true;
}

bool OpeningAt(TownLightVector hit, unsigned axis, float boundary, const TownLightOccluder &occluder)
{
	const TownLightPlane plane = axis == 0 ? TownLightPlane::X : (axis == 1 ? TownLightPlane::Height : TownLightPlane::Z);
	const float u = axis == 0 ? hit.z : hit.x;
	const float v = axis == 1 ? hit.z : hit.height;
	for (const TownLightAperture &aperture : occluder.apertures) {
		if (aperture.plane != plane || std::abs(aperture.coordinate - boundary) > BoundaryEpsilon)
			continue;
		if (u > aperture.minU + BoundaryEpsilon && u < aperture.maxU - BoundaryEpsilon
		    && v > aperture.minV + BoundaryEpsilon && v < aperture.maxV - BoundaryEpsilon
		    && InsideAperturePolygon(u, v, aperture))
			return true;
	}
	return false;
}

bool CrossingOpen(TownLightVector origin, TownLightVector delta, float parameter, const TownLightOccluder &occluder)
{
	if (parameter <= SegmentEndpointEpsilon || parameter >= 1 - SegmentEndpointEpsilon)
		return true; // Contact with the receiving surface is not an extra wall.
	const TownLightVector hit { origin.x + delta.x * parameter,
		origin.height + delta.height * parameter, origin.z + delta.z * parameter };
	const std::array<float, 3> values { hit.x, hit.height, hit.z };
	const std::array<float, 3> minimum { occluder.minimum.x, occluder.minimum.height, occluder.minimum.z };
	const std::array<float, 3> maximum { occluder.maximum.x, occluder.maximum.height, occluder.maximum.z };
	const std::array<float, 3> direction { delta.x, delta.height, delta.z };
	for (unsigned axis = 0; axis < 3; ++axis) {
		if (std::abs(direction[axis]) <= 0.000001F)
			continue;
		if (std::abs(values[axis] - minimum[axis]) <= BoundaryEpsilon
		    && !OpeningAt(hit, axis, minimum[axis], occluder))
			return false;
		if (std::abs(values[axis] - maximum[axis]) <= BoundaryEpsilon
		    && !OpeningAt(hit, axis, maximum[axis], occluder))
			return false;
	}
	return true;
}

bool VisibleThrough(const TownLightOccluder &occluder, TownLightVector origin, TownLightVector delta)
{
	if (!Finite(occluder.minimum) || !Finite(occluder.maximum)
	    || occluder.minimum.x >= occluder.maximum.x || occluder.minimum.height >= occluder.maximum.height
	    || occluder.minimum.z >= occluder.maximum.z || occluder.apertures.size() > TownMaxLightApertures)
		return false;
	for (const TownLightAperture &aperture : occluder.apertures)
		if (!ValidAperture(aperture))
			return false;
	const std::array<float, 3> start { origin.x, origin.height, origin.z };
	const std::array<float, 3> direction { delta.x, delta.height, delta.z };
	const std::array<float, 3> minimum { occluder.minimum.x, occluder.minimum.height, occluder.minimum.z };
	const std::array<float, 3> maximum { occluder.maximum.x, occluder.maximum.height, occluder.maximum.z };
	float entry = 0;
	float exit = 1;
	for (unsigned axis = 0; axis < 3; ++axis) {
		if (std::abs(direction[axis]) <= 0.000001F) {
			if (start[axis] < minimum[axis] || start[axis] > maximum[axis])
				return true; // Parallel ray misses this room entirely.
			continue;
		}
		float first = (minimum[axis] - start[axis]) / direction[axis];
		float second = (maximum[axis] - start[axis]) / direction[axis];
		if (first > second)
			std::swap(first, second);
		entry = std::max(entry, first);
		exit = std::min(exit, second);
		if (entry > exit)
			return true;
	}
	return CrossingOpen(origin, delta, entry, occluder) && CrossingOpen(origin, delta, exit, occluder);
}

} // namespace

TownLightColor TownLightingSample::total() const
{
	return Add(Add(ambient, directional), point);
}

TownLightVector OrientTownLightingNormal(TownLightVector normal, TownLightVector towardViewer, bool doubleSided)
{
	if (!Finite(normal))
		return {};
	if (!doubleSided || !Finite(towardViewer) || Dot(normal, towardViewer) >= 0)
		return normal;
	return { -normal.x, -normal.height, -normal.z };
}

TownPointLight TownFireLightAtTime(const TownPointLight &source, uint32_t emitterIdentity,
	double elapsedSeconds, float intensityVariation)
{
	TownPointLight result = source;
	result.intensity = Positive(source.intensity);
	if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0)
		return result;
	const float variation = Positive(intensityVariation, 0.15F);
	if (variation == 0 || result.intensity == 0)
		return result;
	const auto mix = [](uint32_t value) {
		value ^= value >> 16;
		value *= 0x7FEB352DU;
		value ^= value >> 15;
		value *= 0x846CA68BU;
		return value ^ (value >> 16);
	};
	const uint32_t first = mix(emitterIdentity + 0x9E3779B9U);
	const uint32_t second = mix(first + 0x85EBCA6BU);
	const uint32_t third = mix(second + 0xC2B2AE35U);
	constexpr double Tau = 6.28318530717958647692;
	// Integer cycle counts make wrapping continuous. Three differently phased
	// frequencies avoid synchronous pulsing between neighboring fire sources.
	const double time = std::fmod(elapsedSeconds, 64.0) / 64.0;
	const auto wave = [&](uint32_t seed, unsigned minimumCycles, unsigned cycleMask) {
		const double phase = Tau * static_cast<double>(seed & 65535U) / 65536.0;
		const double cycles = minimumCycles + ((seed >> 16) & cycleMask);
		return std::sin(Tau * cycles * time + phase);
	};
	const double oscillation = 0.55 * wave(first, 39, 7)
	    + 0.30 * wave(second, 145, 31) + 0.15 * wave(third, 383, 63);
	result.intensity = Positive(result.intensity * (1 + variation * static_cast<float>(std::clamp(oscillation, -1.0, 1.0))));
	return result;
}

float TownPointLightVisibility(TownLightVector light, TownLightVector receiver,
	std::span<const TownLightOccluder> occluders)
{
	if (!Finite(light) || !Finite(receiver) || occluders.size() > TownMaxLightOccluders)
		return 0;
	const TownLightVector delta = Difference(receiver, light);
	for (const TownLightOccluder &occluder : occluders)
		if (!VisibleThrough(occluder, light, delta))
			return 0;
	return 1;
}

TownLightingSample SampleTownLighting(TownLightVector normal, TownLightVector position,
	float directionalShadow, const TownLightingConfig &config,
	std::span<const TownPointLight> lights, std::span<const TownLightOccluder> occluders)
{
	TownLightingSample sample;
	sample.ambient = Positive(config.ambient);
	if (!Finite(position) || !Normalize(normal))
		return sample;
	TownLightVector lightDirection = config.toLight;
	if (Normalize(lightDirection)) {
		const float diffuse = std::max(0.0F, Dot(normal, lightDirection));
		const float shadow = std::isfinite(directionalShadow) ? std::clamp(directionalShadow, 0.0F, 1.0F) : 1;
		sample.directional = Scale(Positive(config.directional), diffuse * Positive(config.directionalIntensity) * (1 - shadow));
	}
	const std::size_t count = std::min<std::size_t>(lights.size(), TownMaxPointLights);
	for (std::size_t index = 0; index < count; ++index) {
		const TownPointLight &light = lights[index];
		if (!Finite(light.position) || !std::isfinite(light.radius) || light.radius <= 0 || light.radius > 256)
			continue;
		TownLightVector towardsLight = Difference(light.position, position);
		const float distanceSquared = Dot(towardsLight, towardsLight);
		const float radiusSquared = light.radius * light.radius;
		if (distanceSquared >= radiusSquared || distanceSquared <= 0.00000001F || !Normalize(towardsLight))
			continue;
		const float diffuse = std::max(0.0F, Dot(normal, towardsLight));
		if (diffuse <= 0 || TownPointLightVisibility(light.position, position, occluders) == 0)
			continue;
		// Inverse-square falloff is regularized at the emitter; the squared
		// range window reaches zero smoothly at the finite world-space radius.
		const float range = 1 - distanceSquared / radiusSquared;
		const float attenuation = range * range / (1 + distanceSquared);
		sample.point = Add(sample.point, Scale(Positive(light.color), diffuse * attenuation * Positive(light.intensity)));
	}
	return sample;
}

TownLightColor ComposeTownLitColor(TownLightColor baseLinear, const TownLightingSample &lighting,
	TownLightColor emissionLinear)
{
	const TownLightColor base = Positive(baseLinear);
	const TownLightColor total = lighting.total();
	const TownLightColor emission = Positive(emissionLinear);
	return { Positive(base.red * total.red + emission.red, 1024),
		Positive(base.green * total.green + emission.green, 1024),
		Positive(base.blue * total.blue + emission.blue, 1024) };
}

float TownSrgbToLinear(float normalizedSrgb)
{
	const float component = Positive(normalizedSrgb, 1);
	return component <= 0.04045F ? component / 12.92F : std::pow((component + 0.055F) / 1.055F, 2.4F);
}

float TownLinearToSrgb(float linear)
{
	const float component = Positive(linear, 1);
	return component <= 0.0031308F ? component * 12.92F : 1.055F * std::pow(component, 1 / 2.4F) - 0.055F;
}

TownLightColor TownSrgbToLinear(TownLightColor normalizedSrgb)
{
	return { TownSrgbToLinear(normalizedSrgb.red), TownSrgbToLinear(normalizedSrgb.green), TownSrgbToLinear(normalizedSrgb.blue) };
}

TownLightColor TownLinearToSrgb(TownLightColor linear)
{
	return { TownLinearToSrgb(linear.red), TownLinearToSrgb(linear.green), TownLinearToSrgb(linear.blue) };
}

} // namespace devilution
