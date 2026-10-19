#include "engine/render/town_horizon.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace devilution {
namespace {

constexpr size_t MinimumSegments = 16;
constexpr size_t MaximumSegments = 256;
constexpr float MinimumExtent = 1.0F / 16;
constexpr float MaximumCoordinate = 1048576; // 2^20 native tile units.
constexpr size_t TrianglesPerSegment = 10;

uint32_t VisualHash(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	return value ^ (value >> 16);
}

float HashUnit(uint32_t key, size_t index)
{
	// A bounded, exactly representable mantissa avoids implementation RNGs.
	return static_cast<float>(VisualHash(key ^ static_cast<uint32_t>(index)) >> 8) / 16777215;
}

float SmoothStep(float value)
{
	return value * value * (3 - 2 * value);
}

float PeriodicNoise(size_t sample, size_t segments, size_t knots, uint32_t key)
{
	// Integer phase makes shared quality samples identical and makes sample N
	// exactly sample 0. The same knots exist at every quality level.
	const size_t phase = (sample % segments) * knots;
	const size_t knot = phase / segments;
	const float fraction = static_cast<float>(phase % segments) / static_cast<float>(segments);
	const float first = HashUnit(key, knot);
	const float second = HashUnit(key, (knot + 1) % knots);
	return first + (second - first) * SmoothStep(fraction);
}

float TerrainNoise(size_t sample, size_t segments, uint32_t key)
{
	return 0.72F * PeriodicNoise(sample, segments, 12, key)
	    + 0.28F * PeriodicNoise(sample, segments, 28, key ^ 0x9E3779B9U);
}

TownCameraPoint RingPoint(const TownHorizonBounds &bounds, size_t sample,
    size_t segments, float distance, float height)
{
	const size_t sideSegments = segments / 4;
	const size_t wrapped = sample % segments;
	const size_t side = wrapped / sideSegments;
	const float fraction = static_cast<float>(wrapped % sideSegments) / static_cast<float>(sideSegments);
	const float left = bounds.minX - distance;
	const float right = bounds.maxX + distance;
	const float front = bounds.minZ - distance;
	const float back = bounds.maxZ + distance;
	// Each side includes its first corner, and the next side includes the last.
	// Shared rings/corners are therefore bit-identical, with no angular seam.
	switch (side) {
	case 0:
		return { left + (right - left) * fraction, height, front };
	case 1:
		return { right, height, front + (back - front) * fraction };
	case 2:
		return { right - (right - left) * fraction, height, back };
	default:
		return { left, height, back - (back - front) * fraction };
	}
}

std::array<TownCameraPoint, 6> SampleRings(const TownHorizonConfig &config,
    size_t sample, size_t segments)
{
	const float apronNoise = TerrainNoise(sample, segments, config.visualSeed);
	const float ridgeNoise = TerrainNoise(sample, segments, config.visualSeed ^ 0xA511E9B3U);
	const float farNoise = TerrainNoise(sample, segments, config.visualSeed ^ 0x63D83595U);
	const float apronHeight = config.apronHeight * (0.2F + 0.8F * apronNoise);
	const float valleyHeight = config.apronHeight * (0.15F + 0.35F * apronNoise);
	const float ridgeHeight = config.ridgeHeight * (0.3F + 0.7F * ridgeNoise);
	const float farHeight = config.farRidgeHeight * (0.3F + 0.7F * farNoise);
	const float ridgeCrest = config.apronDistance + (config.ridgeDistance - config.apronDistance) * 0.48F;
	const float farCrest = config.ridgeDistance + (config.farDistance - config.ridgeDistance) * 0.55F;
	return { RingPoint(config.bounds, sample, segments, 0, 0),
		RingPoint(config.bounds, sample, segments, config.apronDistance, apronHeight),
		RingPoint(config.bounds, sample, segments, ridgeCrest, ridgeHeight),
		RingPoint(config.bounds, sample, segments, config.ridgeDistance, valleyHeight),
		RingPoint(config.bounds, sample, segments, farCrest, farHeight),
		RingPoint(config.bounds, sample, segments, config.farDistance, 0) };
}

bool HasRepresentableArea(const TownHorizonConfig &config, size_t segments)
{
	const std::array<float, 6> distances { 0, config.apronDistance,
		config.apronDistance + (config.ridgeDistance - config.apronDistance) * 0.48F,
		config.ridgeDistance,
		config.ridgeDistance + (config.farDistance - config.ridgeDistance) * 0.55F,
		config.farDistance };
	const auto upwardArea = [](TownCameraPoint first, TownCameraPoint second, TownCameraPoint third) {
		const double firstX = static_cast<double>(second.x) - first.x;
		const double firstZ = static_cast<double>(second.z) - first.z;
		const double secondX = static_cast<double>(third.x) - first.x;
		const double secondZ = static_cast<double>(third.z) - first.z;
		return firstZ * secondX - firstX * secondZ;
	};
	for (size_t sample = 0; sample < segments; ++sample) {
		for (size_t band = 0; band + 1 < distances.size(); ++band) {
			const TownCameraPoint first = RingPoint(config.bounds, sample, segments, distances[band], 0);
			const TownCameraPoint second = RingPoint(config.bounds, sample + 1, segments, distances[band], 0);
			const TownCameraPoint third = RingPoint(config.bounds, sample + 1, segments, distances[band + 1], 0);
			const TownCameraPoint fourth = RingPoint(config.bounds, sample, segments, distances[band + 1], 0);
			if (upwardArea(first, second, third) <= 0 || upwardArea(first, third, fourth) <= 0)
				return false;
		}
	}
	return true;
}

TownHorizonColor BandColor(size_t band, size_t sample, size_t segments, uint32_t key)
{
	constexpr std::array<TownHorizonColor, 5> Colors { {
		{ 0.065F, 0.082F, 0.044F },
		{ 0.045F, 0.061F, 0.046F },
		{ 0.042F, 0.056F, 0.043F },
		{ 0.035F, 0.050F, 0.056F },
		{ 0.031F, 0.044F, 0.050F },
	} };
	const float variation = 0.85F + 0.3F * TerrainNoise(sample, segments, key ^ 0xB5297A4DU);
	const TownHorizonColor base = Colors[band];
	return { base.red * variation, base.green * variation, base.blue * variation };
}

bool IsFiniteColor(TownHorizonColor color)
{
	return std::isfinite(color.red) && std::isfinite(color.green) && std::isfinite(color.blue);
}

bool IsUnitColor(TownHorizonColor color)
{
	return IsFiniteColor(color)
	    && color.red >= 0 && color.red <= 1
	    && color.green >= 0 && color.green <= 1
	    && color.blue >= 0 && color.blue <= 1;
}

float CleanChannel(float channel)
{
	return std::isfinite(channel) ? std::clamp(channel, 0.0F, 1.0F) : 0;
}

} // namespace

TownHorizonConfigError ValidateTownHorizonConfig(const TownHorizonConfig &config)
{
	const TownHorizonBounds &bounds = config.bounds;
	const std::array<float, 10> values { bounds.minX, bounds.minZ, bounds.maxX, bounds.maxZ,
		config.apronDistance, config.ridgeDistance, config.farDistance,
		config.apronHeight, config.ridgeHeight, config.farRidgeHeight };
	if (!std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); }))
		return TownHorizonConfigError::NonFinite;
	// Use double for validation so even hostile finite float values cannot
	// overflow while checking bounds or differences, before any allocation.
	if (static_cast<double>(bounds.maxX) - bounds.minX < MinimumExtent
	    || static_cast<double>(bounds.maxZ) - bounds.minZ < MinimumExtent)
		return TownHorizonConfigError::InvalidBounds;
	const double apronWidth = config.apronDistance;
	const double ridgeWidth = static_cast<double>(config.ridgeDistance) - config.apronDistance;
	const double farWidth = static_cast<double>(config.farDistance) - config.ridgeDistance;
	if (apronWidth < MinimumExtent || ridgeWidth < MinimumExtent || farWidth < MinimumExtent)
		return TownHorizonConfigError::InvalidDistances;
	if (config.apronHeight < 0 || config.ridgeHeight < 0 || config.farRidgeHeight < 0
	    || config.apronHeight > apronWidth || config.ridgeHeight > ridgeWidth || config.farRidgeHeight > farWidth)
		return TownHorizonConfigError::InvalidHeights;
	const double farDistance = config.farDistance;
	if (static_cast<double>(bounds.minX) - farDistance < -MaximumCoordinate
	    || static_cast<double>(bounds.minZ) - farDistance < -MaximumCoordinate
	    || static_cast<double>(bounds.maxX) + farDistance > MaximumCoordinate
	    || static_cast<double>(bounds.maxZ) + farDistance > MaximumCoordinate)
		return TownHorizonConfigError::ExtentOutOfRange;
	const size_t segments = std::clamp(config.segments, MinimumSegments, MaximumSegments) / 4 * 4;
	if (!HasRepresentableArea(config, segments))
		return TownHorizonConfigError::ExtentOutOfRange;
	return TownHorizonConfigError::None;
}

TownHorizonMesh BuildTownHorizon(const TownHorizonConfig &config)
{
	TownHorizonMesh mesh;
	mesh.error = ValidateTownHorizonConfig(config);
	if (mesh.error != TownHorizonConfigError::None)
		return mesh;
	const size_t segments = std::clamp(config.segments, MinimumSegments, MaximumSegments) / 4 * 4;
	mesh.triangles.reserve(segments * TrianglesPerSegment);
	for (size_t sample = 0; sample < segments; ++sample) {
		const auto current = SampleRings(config, sample, segments);
		const auto next = SampleRings(config, sample + 1, segments);
		for (size_t band = 0; band < 5; ++band) {
			const TownHorizonLayer layer = band == 0 ? TownHorizonLayer::Apron
			                                       : band < 3 ? TownHorizonLayer::Ridge : TownHorizonLayer::FarRidge;
			const TownHorizonColor color = BandColor(band, sample, segments, config.visualSeed);
			mesh.triangles.push_back({ { current[band], next[band], next[band + 1] }, color, layer });
			mesh.triangles.push_back({ { current[band], next[band + 1], current[band + 1] }, color, layer });
		}
	}
	mesh.statistics = { segments, mesh.triangles.size(),
		mesh.triangles.size() * sizeof(TownHorizonTriangle),
		mesh.triangles.capacity() * sizeof(TownHorizonTriangle) };
	return mesh;
}

bool IsValidTownHorizonFogConfig(const TownHorizonFogConfig &config)
{
	return std::isfinite(config.startDepth) && std::isfinite(config.endDepth)
	    && std::isfinite(config.density) && IsFiniteColor(config.color)
	    && config.startDepth >= 0 && config.endDepth > config.startDepth
	    && config.density >= 0 && config.density <= 20
	    && config.color.red >= 0 && config.color.red <= 1
	    && config.color.green >= 0 && config.color.green <= 1
	    && config.color.blue >= 0 && config.color.blue <= 1;
}

float TownHorizonFogAmount(float viewDepth, const TownHorizonFogConfig &config)
{
	if (!std::isfinite(viewDepth) || viewDepth <= 0 || !IsValidTownHorizonFogConfig(config)
	    || viewDepth <= config.startDepth)
		return 0;
	if (viewDepth >= config.endDepth)
		return 1;
	const float position = (viewDepth - config.startDepth) / (config.endDepth - config.startDepth);
	const float smooth = SmoothStep(position);
	// expm1 preserves precision as density approaches zero. The normalization
	// reaches exactly one at endDepth without an unbounded distance exponential.
	if (config.density == 0)
		return smooth;
	const double density = config.density;
	return std::clamp(static_cast<float>(std::expm1(-density * smooth) / std::expm1(-density)), 0.0F, 1.0F);
}

TownHorizonColor ApplyTownHorizonFog(TownHorizonColor surface, float viewDepth,
    const TownHorizonFogConfig &config)
{
	const TownHorizonColor clean { CleanChannel(surface.red), CleanChannel(surface.green), CleanChannel(surface.blue) };
	const float amount = TownHorizonFogAmount(viewDepth, config);
	if (amount == 0)
		return clean;
	if (amount == 1)
		return config.color;
	return { clean.red + (config.color.red - clean.red) * amount,
		clean.green + (config.color.green - clean.green) * amount,
		clean.blue + (config.color.blue - clean.blue) * amount };
}

TownHorizonFogPalette BuildTownHorizonFogPalette(const TownHorizonPalette &paletteLinear,
    TownHorizonColor fogColor)
{
	TownHorizonFogPalette table;
	for (auto &row : table) {
		for (size_t source = 0; source < row.size(); ++source)
			row[source] = static_cast<uint8_t>(source);
	}
	if (!IsUnitColor(fogColor)
	    || !std::all_of(paletteLinear.begin(), paletteLinear.end(), IsUnitColor))
		return table;
	const auto nearestIndex = [&paletteLinear](TownHorizonColor target) {
		// Linear unit RGB has squared distance <=3. Double keeps accumulated
		// distance stable without overflow, and strict '<' preserves first ties.
		double nearestDistance = 4;
		uint8_t nearest = 0;
		for (size_t index = 0; index < paletteLinear.size(); ++index) {
			const TownHorizonColor candidate = paletteLinear[index];
			const double red = static_cast<double>(candidate.red) - target.red;
			const double green = static_cast<double>(candidate.green) - target.green;
			const double blue = static_cast<double>(candidate.blue) - target.blue;
			const double distance = red * red + green * green + blue * blue;
			if (distance < nearestDistance) {
				nearestDistance = distance;
				nearest = static_cast<uint8_t>(index);
			}
		}
		return nearest;
	};
	// The endpoint is computed directly from fog RGB, independent of source
	// rounding, and avoids 256 identical palette searches for this row.
	table.back().fill(nearestIndex(fogColor));
	for (size_t level = 1; level + 1 < table.size(); ++level) {
		const float amount = static_cast<float>(level) / static_cast<float>(TownHorizonFogLevels - 1);
		for (size_t source = 0; source < paletteLinear.size(); ++source) {
			const TownHorizonColor color = paletteLinear[source];
			const TownHorizonColor target { color.red + (fogColor.red - color.red) * amount,
				color.green + (fogColor.green - color.green) * amount,
				color.blue + (fogColor.blue - color.blue) * amount };
			table[level][source] = nearestIndex(target);
		}
	}
	return table;
}

} // namespace devilution
