#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <utility>
#include <vector>

#include "engine/render/town_horizon.hpp"

namespace devilution {

/** Private diagnostic support; no executable/main, game state or build changes. */
inline bool RunTownHorizonChecks(std::ostream &output, size_t &checks)
{
	const size_t initialChecks = checks;
	size_t failures = 0;
	const auto check = [&](bool passed, const char *label) {
		++checks;
		if (!passed) {
			++failures;
			output << "FAIL horizon: " << label << '\n';
		}
	};
	const auto samePoint = [](TownCameraPoint first, TownCameraPoint second) {
		return first.x == second.x && first.height == second.height && first.z == second.z;
	};
	const auto sameColor = [](TownHorizonColor first, TownHorizonColor second) {
		return first.red == second.red && first.green == second.green && first.blue == second.blue;
	};
	const auto finiteColor = [](TownHorizonColor color) {
		return std::isfinite(color.red) && std::isfinite(color.green) && std::isfinite(color.blue)
		    && color.red >= 0 && color.red <= 1 && color.green >= 0 && color.green <= 1
		    && color.blue >= 0 && color.blue <= 1;
	};
	const auto insideFootprint = [](TownCameraPoint point, TownHorizonBounds bounds) {
		return point.x > bounds.minX && point.x < bounds.maxX
		    && point.z > bounds.minZ && point.z < bounds.maxZ;
	};
	const auto normal = [](const TownHorizonTriangle &triangle) {
		const TownCameraPoint &a = triangle.vertices[0];
		const TownCameraPoint &b = triangle.vertices[1];
		const TownCameraPoint &c = triangle.vertices[2];
		const double ax = static_cast<double>(b.x) - a.x;
		const double ay = static_cast<double>(b.height) - a.height;
		const double az = static_cast<double>(b.z) - a.z;
		const double bx = static_cast<double>(c.x) - a.x;
		const double by = static_cast<double>(c.height) - a.height;
		const double bz = static_cast<double>(c.z) - a.z;
		return std::array<double, 3> { ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx };
	};
	const auto inspectGeometry = [&](const TownHorizonMesh &mesh, TownHorizonBounds bounds) {
		check(mesh.valid(), "mesh is valid");
		if (!mesh.valid())
			return;
		check(mesh.statistics.triangles == mesh.triangles.size(), "reported triangle count");
		check(mesh.statistics.triangleDataBytes == mesh.triangles.size() * sizeof(TownHorizonTriangle), "reported used bytes");
		check(mesh.statistics.reservedTriangleBytes == mesh.triangles.capacity() * sizeof(TownHorizonTriangle), "reported reserved bytes");
		check(mesh.statistics.reservedTriangleBytes >= mesh.statistics.triangleDataBytes, "reserved bytes cover geometry");
		check(mesh.triangles.size() <= 5000 && mesh.statistics.reservedTriangleBytes <= 5000 * sizeof(TownHorizonTriangle), "bounded geometry budget");
		std::array<size_t, 3> layers {};
		for (const TownHorizonTriangle &triangle : mesh.triangles) {
			check(finiteColor(triangle.color), "finite bounded linear albedo");
			const size_t layer = static_cast<size_t>(triangle.layer);
			check(layer < layers.size(), "known visual layer");
			if (layer < layers.size())
				++layers[layer];
			const auto n = normal(triangle);
			check(std::isfinite(n[0]) && std::isfinite(n[1]) && std::isfinite(n[2])
			        && n[0] * n[0] + n[1] * n[1] + n[2] * n[2] > 0,
			    "finite nondegenerate 3D area");
			check(n[1] > 0, "consistent upward winding");
			TownCameraPoint centroid;
			for (size_t vertex = 0; vertex < 3; ++vertex) {
				const TownCameraPoint point = triangle.vertices[vertex];
				const TownCameraPoint next = triangle.vertices[(vertex + 1) % 3];
				check(std::isfinite(point.x) && std::isfinite(point.height) && std::isfinite(point.z) && point.height >= 0, "finite nonnegative geometry");
				check(!insideFootprint(point, bounds), "vertex outside native ground interior");
				check(!insideFootprint({ (point.x + next.x) / 2, 0, (point.z + next.z) / 2 }, bounds), "edge midpoint outside native ground interior");
				centroid.x += point.x / 3;
				centroid.z += point.z / 3;
			}
			check(!insideFootprint(centroid, bounds), "triangle centroid outside native ground interior");
		}
		check(layers[0] == mesh.statistics.segments * 2, "apron budget");
		check(layers[1] == mesh.statistics.segments * 4, "near ridge front and back budget");
		check(layers[2] == mesh.statistics.segments * 4, "far ridge front and back budget");
	};

	TownHorizonConfig config;
	const TownHorizonMesh baseline = BuildTownHorizon(config);
	check(ValidateTownHorizonConfig(config) == TownHorizonConfigError::None, "fixture config accepted");
	check(baseline.statistics.segments == 128 && baseline.triangles.size() == 1280, "default bounded quality");
	inspectGeometry(baseline, config.bounds);
	if (!baseline.valid())
		return false;

	// Compare authored values, never object padding or an implementation hash.
	const TownHorizonMesh repeated = BuildTownHorizon(config);
	check(repeated.triangles.size() == baseline.triangles.size(), "deterministic geometry size");
	for (size_t triangle = 0; triangle < std::min(baseline.triangles.size(), repeated.triangles.size()); ++triangle) {
		const auto &first = baseline.triangles[triangle];
		const auto &second = repeated.triangles[triangle];
		for (size_t vertex = 0; vertex < 3; ++vertex)
			check(samePoint(first.vertices[vertex], second.vertices[vertex]), "deterministic world vertex");
		check(sameColor(first.color, second.color) && first.layer == second.layer, "deterministic albedo and layer");
	}

	// A complete annular surface has exactly two unpaired boundary loops.
	// Every other edge must be shared twice with opposite directed winding.
	using PointKey = std::array<float, 3>;
	using EdgeKey = std::pair<PointKey, PointKey>;
	struct EdgeUse {
		size_t count = 0;
		int directedBalance = 0;
	};
	std::map<EdgeKey, EdgeUse> edges;
	double projectedArea = 0;
	for (const TownHorizonTriangle &triangle : baseline.triangles) {
		projectedArea += normal(triangle)[1] / 2;
		for (size_t vertex = 0; vertex < 3; ++vertex) {
			const auto &a = triangle.vertices[vertex];
			const auto &b = triangle.vertices[(vertex + 1) % 3];
			PointKey first { a.x, a.height, a.z };
			PointKey second { b.x, b.height, b.z };
			const int direction = first < second ? 1 : -1;
			if (direction < 0)
				std::swap(first, second);
			EdgeUse &use = edges[{ first, second }];
			++use.count;
			use.directedBalance += direction;
		}
	}
	const double innerWidth = config.bounds.maxX - config.bounds.minX;
	const double innerDepth = config.bounds.maxZ - config.bounds.minZ;
	const double expectedArea = (innerWidth + 2 * config.farDistance) * (innerDepth + 2 * config.farDistance) - innerWidth * innerDepth;
	check(std::abs(projectedArea - expectedArea) < 0.5, "projected annulus area covers exactly the exterior band");
	std::map<PointKey, std::vector<PointKey>> boundary;
	size_t boundaryEdges = 0;
	for (const auto &entry : edges) {
		const EdgeKey &edge = entry.first;
		const EdgeUse &use = entry.second;
		check(use.count == 1 || use.count == 2, "manifold edge multiplicity");
		if (use.count == 2) {
			check(use.directedBalance == 0, "shared edges have opposite winding");
			continue;
		}
		if (use.count == 1) {
			++boundaryEdges;
			boundary[edge.first].push_back(edge.second);
			boundary[edge.second].push_back(edge.first);
			check(edge.first[1] == 0 && edge.second[1] == 0, "only ground-height inner and outer rims remain open");
		}
	}
	check(boundaryEdges == baseline.statistics.segments * 2, "exactly two rim edge budgets, no seam edges");
	std::set<PointKey> visited;
	size_t loops = 0;
	for (const auto &entry : boundary) {
		check(entry.second.size() == 2, "each rim vertex has degree two");
		if (visited.count(entry.first) != 0)
			continue;
		++loops;
		std::vector<PointKey> pending { entry.first };
		while (!pending.empty()) {
			const PointKey point = pending.back();
			pending.pop_back();
			if (!visited.insert(point).second)
				continue;
			for (const PointKey &neighbor : boundary.at(point))
				pending.push_back(neighbor);
		}
	}
	check(loops == 2, "watertight 360-degree joins with two continuous rims");

	TownHorizonConfig quality = config;
	quality.segments = 0;
	const TownHorizonMesh minimum = BuildTownHorizon(quality);
	check(minimum.statistics.segments == 16 && minimum.triangles.size() == 160, "minimum quality clamped");
	inspectGeometry(minimum, quality.bounds);
	quality.segments = std::numeric_limits<size_t>::max();
	const TownHorizonMesh maximum = BuildTownHorizon(quality);
	check(maximum.statistics.segments == 256 && maximum.triangles.size() == 2560, "hostile integer quality stays bounded");
	inspectGeometry(maximum, quality.bounds);
	quality.segments = 127;
	const TownHorizonMesh corners = BuildTownHorizon(quality);
	check(corners.statistics.segments == 124 && corners.triangles.size() == 1240, "all quality settings preserve four corners");
	quality.segments = 64;
	const TownHorizonMesh lower = BuildTownHorizon(quality);
	if (lower.valid()) {
		for (size_t sample = 0; sample < lower.statistics.segments; ++sample) {
			for (size_t band = 0; band < 5; ++band)
				check(samePoint(lower.triangles[sample * 10 + band * 2].vertices[0], baseline.triangles[sample * 20 + band * 2].vertices[0]), "quality changes retain shared world samples");
		}
	} else {
		check(false, "lower quality valid");
	}

	TownHorizonConfig translated = config;
	translated.bounds = { -1.46875F, -7.125F, 110.53125F, 85.375F };
	inspectGeometry(BuildTownHorizon(translated), translated.bounds);
	TownHorizonConfig flat = config;
	flat.apronHeight = flat.ridgeHeight = flat.farRidgeHeight = 0;
	inspectGeometry(BuildTownHorizon(flat), flat.bounds);

	TownHorizonConfig otherSeed = config;
	otherSeed.visualSeed ^= 0xDEADBEEFU;
	const TownHorizonMesh changed = BuildTownHorizon(otherSeed);
	bool sawChange = false;
	check(changed.triangles.size() == baseline.triangles.size(), "seed does not change topology budget");
	for (size_t triangle = 0; triangle < std::min(changed.triangles.size(), baseline.triangles.size()); ++triangle) {
		const auto &first = baseline.triangles[triangle];
		const auto &second = changed.triangles[triangle];
		check(first.layer == second.layer, "seed does not change layer assignment");
		sawChange = sawChange || !sameColor(first.color, second.color);
		for (size_t vertex = 0; vertex < 3; ++vertex) {
			const TownCameraPoint a = first.vertices[vertex];
			const TownCameraPoint b = second.vertices[vertex];
			check(a.x == b.x && a.z == b.z, "seed changes only exterior visual elevations and albedo");
			check(!insideFootprint(b, config.bounds), "new seed stays outside native ground");
			if (a.height != b.height) {
				sawChange = true;
				check(a.x < config.bounds.minX || a.x > config.bounds.maxX || a.z < config.bounds.minZ || a.z > config.bounds.maxZ, "changed elevation strictly outside native ground");
			}
		}
	}
	check(sawChange, "visual seed changes the background");

	const auto rejected = [&](const TownHorizonConfig &invalid, TownHorizonConfigError error) {
		const TownHorizonMesh result = BuildTownHorizon(invalid);
		check(ValidateTownHorizonConfig(invalid) == error && result.error == error && !result.valid(), "invalid config rejected consistently");
		check(result.triangles.empty() && result.triangles.capacity() == 0, "invalid config allocates no triangle buffer");
		check(result.statistics.segments == 0 && result.statistics.triangles == 0
		        && result.statistics.triangleDataBytes == 0 && result.statistics.reservedTriangleBytes == 0,
		    "invalid config publishes no geometry statistics");
	};
	TownHorizonConfig invalid = config;
	const std::array<float *, 10> fields { &invalid.bounds.minX, &invalid.bounds.minZ, &invalid.bounds.maxX, &invalid.bounds.maxZ,
		&invalid.apronDistance, &invalid.ridgeDistance, &invalid.farDistance,
		&invalid.apronHeight, &invalid.ridgeHeight, &invalid.farRidgeHeight };
	for (float *field : fields) {
		const float original = *field;
		for (const float value : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
			*field = value;
			rejected(invalid, TownHorizonConfigError::NonFinite);
		}
		*field = original;
	}
	invalid = config;
	invalid.bounds.maxX = invalid.bounds.minX;
	rejected(invalid, TownHorizonConfigError::InvalidBounds);
	invalid = config;
	invalid.bounds.maxZ = invalid.bounds.minZ - 1;
	rejected(invalid, TownHorizonConfigError::InvalidBounds);
	invalid = config;
	invalid.apronDistance = 0;
	rejected(invalid, TownHorizonConfigError::InvalidDistances);
	invalid = config;
	invalid.ridgeDistance = invalid.apronDistance;
	rejected(invalid, TownHorizonConfigError::InvalidDistances);
	invalid = config;
	invalid.farDistance = invalid.ridgeDistance;
	rejected(invalid, TownHorizonConfigError::InvalidDistances);
	invalid = config;
	invalid.ridgeHeight = -1;
	rejected(invalid, TownHorizonConfigError::InvalidHeights);
	invalid = config;
	invalid.farRidgeHeight = invalid.farDistance + 1;
	rejected(invalid, TownHorizonConfigError::InvalidHeights);
	invalid = config;
	invalid.bounds.maxX = std::numeric_limits<float>::max();
	rejected(invalid, TownHorizonConfigError::ExtentOutOfRange);
	invalid = config;
	invalid.bounds = { 100000, 100000, 100000.0625F, 100000.0625F };
	rejected(invalid, TownHorizonConfigError::ExtentOutOfRange);

	TownHorizonFogConfig fog;
	const TownHorizonColor nearby { 0.12F, 0.32F, 0.08F };
	check(IsValidTownHorizonFogConfig(fog), "default fog valid");
	check(TownHorizonFogAmount(0, fog) == 0 && TownHorizonFogAmount(fog.startDepth, fog) == 0, "fog preserves near surfaces");
	check(sameColor(nearby, ApplyTownHorizonFog(nearby, fog.startDepth / 2, fog)), "sky color cannot wash a near surface");
	check(TownHorizonFogAmount(fog.endDepth, fog) == 1
	        && TownHorizonFogAmount(std::numeric_limits<float>::max(), fog) == 1,
	    "fog reaches finite far endpoint");
	check(sameColor(fog.color, ApplyTownHorizonFog(nearby, fog.endDepth, fog)), "far geometry reaches its fog color");
	for (const float density : { 0.0F, std::numeric_limits<float>::denorm_min(), 0.000001F, 2.5F, 20.0F }) {
		fog.density = density;
		float previous = 0;
		for (size_t sample = 0; sample <= 512; ++sample) {
			const float depth = fog.endDepth * 1.2F * static_cast<float>(sample) / 512;
			const float amount = TownHorizonFogAmount(depth, fog);
			check(std::isfinite(amount) && amount >= previous && amount >= 0 && amount <= 1, "finite monotonic bounded depth fog");
			check(finiteColor(ApplyTownHorizonFog(nearby, depth, fog)), "finite linear fog blend");
			previous = amount;
		}
		const float midpoint = TownHorizonFogAmount((fog.startDepth + fog.endDepth) / 2, fog);
		check(midpoint > 0 && midpoint < 1, "very small densities retain a smooth finite transition");
	}
	fog = TownHorizonFogConfig {};
	for (const float invalidDepth : { -1.0F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		check(TownHorizonFogAmount(invalidDepth, fog) == 0, "invalid view depth disables fog");
		check(sameColor(nearby, ApplyTownHorizonFog(nearby, invalidDepth, fog)), "invalid view depth preserves finite surface");
	}
	const auto rejectedFog = [&](TownHorizonFogConfig bad) {
		check(!IsValidTownHorizonFogConfig(bad) && TownHorizonFogAmount(100, bad) == 0, "invalid fog configuration disabled");
		check(sameColor(nearby, ApplyTownHorizonFog(nearby, 100, bad)), "invalid fog color cannot contaminate valid surface");
	};
	fog.startDepth = -1;
	rejectedFog(fog);
	fog = TownHorizonFogConfig {};
	fog.endDepth = fog.startDepth;
	rejectedFog(fog);
	fog = TownHorizonFogConfig {};
	fog.density = std::numeric_limits<float>::quiet_NaN();
	rejectedFog(fog);
	fog = TownHorizonFogConfig {};
	fog.density = 21;
	rejectedFog(fog);
	fog = TownHorizonFogConfig {};
	fog.color.green = std::numeric_limits<float>::quiet_NaN();
	rejectedFog(fog);
	fog = TownHorizonFogConfig {};
	fog.color.blue = std::numeric_limits<float>::infinity();
	rejectedFog(fog);
	fog = TownHorizonFogConfig {};
	check(finiteColor(ApplyTownHorizonFog({ std::numeric_limits<float>::quiet_NaN(), -2, std::numeric_limits<float>::infinity() }, 100, fog)), "invalid surface RGB becomes finite before blending");

	check(TownHorizonFogLevels == 64 && sizeof(TownHorizonFogPalette) == 64 * 256 * sizeof(uint8_t), "fog LUT fixed 16 KiB storage");
	TownHorizonPalette grayscale;
	for (size_t index = 0; index < grayscale.size(); ++index) {
		const float value = static_cast<float>(index) / 255;
		grayscale[index] = { value, value, value };
	}
	const TownHorizonColor grayFog { 0.75F, 0.75F, 0.75F };
	const TownHorizonFogPalette grayscaleTable = BuildTownHorizonFogPalette(grayscale, grayFog);
	const TownHorizonFogPalette grayscaleRepeated = BuildTownHorizonFogPalette(grayscale, grayFog);
	check(grayscaleTable == grayscaleRepeated, "fog LUT is deterministic by value");
	for (size_t source = 0; source < grayscale.size(); ++source) {
		check(grayscaleTable.front()[source] == source, "zero fog LUT preserves exact source index");
		check(grayscaleTable.back()[source] == 191, "full fog LUT selects nearest linear fog color");
	}
	// With amount 21/63=1/3 and fog .75, black maps to .25 (index64)
	// and white maps to .91666... (index234). The palette is already linear.
	check(grayscaleTable[21][0] == 64 && grayscaleTable[21][255] == 234, "intermediate grayscale fog follows RGB distance");
	uint8_t previousDark = grayscaleTable.front()[0];
	uint8_t previousLight = grayscaleTable.front()[255];
	for (size_t level = 1; level < TownHorizonFogLevels; ++level) {
		check(grayscaleTable[level][0] >= previousDark, "grayscale LUT black converges monotonically");
		check(grayscaleTable[level][255] <= previousLight, "grayscale LUT white converges monotonically");
		previousDark = grayscaleTable[level][0];
		previousLight = grayscaleTable[level][255];
	}

	TownHorizonPalette nonordered;
	nonordered.fill({ 0, 0, 1 });
	nonordered[5] = { 1, 0, 0 };
	nonordered[171] = { 0.5F, 0, 0.5F };
	nonordered[241] = { 0, 1, 0 };
	const TownHorizonFogPalette nonorderedTable = BuildTownHorizonFogPalette(nonordered, { 0, 0, 1 });
	check(nonorderedTable[31][5] == 171, "nonordered palette chooses purple RGB, never blended indices");
	check(nonorderedTable.front()[5] == 5 && nonorderedTable.back()[5] == 0, "nonordered palette endpoint contract");
	for (const uint8_t source : nonorderedTable.back())
		check(source == 0, "duplicate far fog colors choose first palette index");

	TownHorizonPalette duplicates;
	duplicates.fill({ 0, 0, 0 });
	const TownHorizonFogPalette duplicateTable = BuildTownHorizonFogPalette(duplicates, { 0, 0, 0 });
	for (size_t source = 0; source < duplicates.size(); ++source) {
		check(duplicateTable.front()[source] == source, "row zero preserves duplicated palette indices");
		check(duplicateTable[1][source] == 0 && duplicateTable.back()[source] == 0, "positive fog uses deterministic first duplicate");
	}
	TownHorizonPalette tiePalette;
	tiePalette.fill({ 1, 1, 1 });
	tiePalette[47] = { 0, 0, 0 };
	const TownHorizonFogPalette tieTable = BuildTownHorizonFogPalette(tiePalette, { 0.5F, 0.5F, 0.5F });
	check(tieTable.back()[47] == 0 && tieTable.back()[255] == 0, "equal RGB distances choose the first index");

	const auto identityFallback = [&](const TownHorizonPalette &palette, TownHorizonColor color) {
		const TownHorizonFogPalette table = BuildTownHorizonFogPalette(palette, color);
		for (const auto &row : table) {
			bool identity = true;
			for (size_t source = 0; source < row.size(); ++source)
				identity = identity && row[source] == source;
			check(identity, "invalid LUT channels fall back to complete index identity");
		}
	};
	TownHorizonPalette badPalette = grayscale;
	badPalette[200].red = std::numeric_limits<float>::quiet_NaN();
	identityFallback(badPalette, grayFog);
	badPalette = grayscale;
	badPalette[0].green = -0.01F;
	identityFallback(badPalette, grayFog);
	badPalette = grayscale;
	badPalette[255].blue = 1.01F;
	identityFallback(badPalette, grayFog);
	identityFallback(grayscale, { std::numeric_limits<float>::infinity(), 0, 0 });
	identityFallback(grayscale, { 0, std::numeric_limits<float>::quiet_NaN(), 0 });
	identityFallback(grayscale, { 0, 0, -1 });
	output << "Horizon checks: " << checks - initialChecks << ", failures: " << failures << '\n';
	return failures == 0;
}

} // namespace devilution
