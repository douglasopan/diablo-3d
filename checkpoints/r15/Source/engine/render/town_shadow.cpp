#include "engine/render/town_shadow.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace devilution {
namespace {

struct Vector {
	float x;
	float height;
	float z;
};

float Dot(Vector a, Vector b)
{
	return a.x * b.x + a.height * b.height + a.z * b.z;
}

Vector Cross(Vector a, Vector b)
{
	return { a.height * b.z - a.z * b.height, a.z * b.x - a.x * b.z, a.x * b.height - a.height * b.x };
}

Vector Normalize(Vector vector)
{
	const float inverse = 1.0F / std::sqrt(Dot(vector, vector));
	return { vector.x * inverse, vector.height * inverse, vector.z * inverse };
}

bool Finite(Vector vector)
{
	return std::isfinite(vector.x) && std::isfinite(vector.height) && std::isfinite(vector.z)
	    && std::abs(vector.x) <= 1000000 && std::abs(vector.height) <= 1000000 && std::abs(vector.z) <= 1000000;
}

struct Projected {
	float u;
	float v;
	float depth;
};

struct ShadowMap {
	std::vector<float> depth;
	TownShadowConfig config;
	Vector light { 0, 1, 0 };
	Vector right { 1, 0, 0 };
	Vector up { 0, 0, 1 };
	float minU = 0;
	float minV = 0;
	float texelU = 1;
	float texelV = 1;
	TownShadowReceiver ground;
	TownShadowStats stats;
	bool cached = false;
	uint64_t geometryPublication = 0;
};

ShadowMap Map;

struct GeometryKey {
	uint64_t epoch = 0, identity = 0, revision = 0, hash = 0;
	std::size_t triangles = 0, masks = 0, maskBytes = 0;
	bool operator==(const GeometryKey &) const = default;
};

struct ProjectionBounds {
	float minU = std::numeric_limits<float>::infinity(), minV = minU;
	float maxU = -minU, maxV = -minV;
	bool empty = true;
};

struct GeometryCache {
	ShadowMap staticMap;
	GeometryKey staticKey, dynamicKey;
	ProjectionBounds bounds;
	std::size_t staticBuildCount = 0;
	bool valid = false;
	bool published = false;
};

GeometryCache Geometry;
TownShadowGeometryStats GeometryStats;
// Deliberately survives Clear. A bounds expansion followed by a return to the
// same geometry may change the projection, so content hash alone cannot be a
// GPU upload revision. Only a full cache hit retains this publication token.
uint64_t GeometryPublicationSequence = 0;

void HashWord(uint64_t &hash, uint32_t value)
{
	hash ^= value;
	hash *= 1099511628211ULL;
}

void HashFloat(uint64_t &hash, float value)
{
	HashWord(hash, std::bit_cast<uint32_t>(value));
}

Projected Project(Vector vertex)
{
	return { Dot(vertex, Map.right), Dot(vertex, Map.up), Dot(vertex, Map.light) };
}

float Edge(const Projected &a, const Projected &b, float u, float v)
{
	return (u - a.u) * (b.v - a.v) - (v - a.v) * (b.u - a.u);
}

void Rasterize(std::array<Projected, 3> vertices)
{
	for (Projected &vertex : vertices) {
		vertex.u = (vertex.u - Map.minU) / Map.texelU;
		vertex.v = (vertex.v - Map.minV) / Map.texelV;
	}
	const float area = Edge(vertices[0], vertices[1], vertices[2].u, vertices[2].v);
	if (std::abs(area) <= 0.000001F)
		return;
	const int resolution = Map.config.resolution;
	const int minU = std::max(0, static_cast<int>(std::floor(std::min({ vertices[0].u, vertices[1].u, vertices[2].u }))));
	const int maxU = std::min(resolution - 1, static_cast<int>(std::ceil(std::max({ vertices[0].u, vertices[1].u, vertices[2].u }))));
	const int minV = std::max(0, static_cast<int>(std::floor(std::min({ vertices[0].v, vertices[1].v, vertices[2].v }))));
	const int maxV = std::min(resolution - 1, static_cast<int>(std::ceil(std::max({ vertices[0].v, vertices[1].v, vertices[2].v }))));
	if (minU > maxU || minV > maxV)
		return;
	++Map.stats.rasterizedTriangles;
	const float inverse = 1.0F / area;
	for (int v = minV; v <= maxV; ++v) {
		for (int u = minU; u <= maxU; ++u) {
			const float centerU = static_cast<float>(u) + 0.5F;
			const float centerV = static_cast<float>(v) + 0.5F;
			const float a = Edge(vertices[1], vertices[2], centerU, centerV) * inverse;
			const float b = Edge(vertices[2], vertices[0], centerU, centerV) * inverse;
			const float c = 1 - a - b;
			if (a < -0.000001F || b < -0.000001F || c < -0.000001F)
				continue;
			const float depth = a * vertices[0].depth + b * vertices[1].depth + c * vertices[2].depth;
			float &stored = Map.depth[static_cast<std::size_t>(v) * resolution + u];
			// Positive depth is closer to the light. Every triangle is opaque;
			// writing both orientations also handles closed undersides and eaves.
			if (depth > stored) {
				stored = depth;
				++Map.stats.depthWrites;
			}
		}
	}
}

bool ValidConfig(const TownShadowConfig &config)
{
	const Vector direction { config.toLight.x, config.toLight.height, config.toLight.z };
	return Finite(direction) && Dot(direction, direction) > 0.000001F
	    && config.toLight.height > 0.0001F
	    && config.resolution >= 128 && config.resolution <= 2048
	    && std::isfinite(config.depthBias) && config.depthBias >= 0 && config.depthBias <= 1
	    && std::isfinite(config.slopeBias) && config.slopeBias >= 0 && config.slopeBias <= 1
	    && config.pcfRadius >= 0 && config.pcfRadius <= 2;
}

void HashWide(uint64_t &hash, uint64_t value)
{
	HashWord(hash, static_cast<uint32_t>(value));
	HashWord(hash, static_cast<uint32_t>(value >> 32));
}

void HashConfig(uint64_t &hash, const TownShadowConfig &config)
{
	HashFloat(hash, config.toLight.x);
	HashFloat(hash, config.toLight.height);
	HashFloat(hash, config.toLight.z);
	HashWord(hash, static_cast<uint32_t>(config.resolution));
	HashFloat(hash, config.depthBias);
	HashFloat(hash, config.slopeBias);
	HashWord(hash, static_cast<uint32_t>(config.pcfRadius));
}

bool SameConfig(const TownShadowConfig &a, const TownShadowConfig &b)
{
	return a.toLight.x == b.toLight.x && a.toLight.height == b.toLight.height && a.toLight.z == b.toLight.z
	    && a.resolution == b.resolution && a.depthBias == b.depthBias && a.slopeBias == b.slopeBias && a.pcfRadius == b.pcfRadius;
}

bool LayerPresent(const TownShadowGeometryLayer &layer)
{
	return !layer.triangles.empty() || !layer.masks.empty();
}

bool ValidateGeometryLayer(const TownShadowGeometryLayer &layer, GeometryKey &key,
    TownShadowGeometryStats &stats)
{
	if (LayerPresent(layer) && (layer.epoch == 0 || layer.identity == 0 || layer.revision == 0))
		return false;
	key = { layer.epoch, layer.identity, layer.revision, 14695981039346656037ULL, layer.triangles.size(), layer.masks.size(), 0 };
	HashWide(key.hash, key.epoch);
	HashWide(key.hash, key.identity);
	HashWide(key.hash, key.revision);
	HashWide(key.hash, key.triangles);
	HashWide(key.hash, key.masks);
	// First validate the whole payload bound, before scanning its bytes.
	for (const auto &mask : layer.masks) {
		if (!town_point_shadow_detail::ValidMask(mask))
			return false;
		if (mask.opacity.size() > TownMaxShadowGeometryMaskBytes - stats.maskBytes) {
			stats.budgetLimited = true;
			return false;
		}
		stats.maskBytes += mask.opacity.size();
		key.maskBytes += mask.opacity.size();
		if (mask.codes.size() > TownMaxShadowGeometryMaskBytes - stats.maskBytes) {
			stats.budgetLimited = true;
			return false;
		}
		stats.maskBytes += mask.codes.size();
		key.maskBytes += mask.codes.size();
	}
	for (const auto &mask : layer.masks) {
		HashWord(key.hash, static_cast<uint32_t>(mask.width));
		HashWord(key.hash, static_cast<uint32_t>(mask.height));
		HashWord(key.hash, mask.repeat);
		HashWord(key.hash, mask.transparentZero);
		HashWide(key.hash, mask.opacity.size());
		HashWide(key.hash, mask.codes.size());
		for (uint8_t value : mask.opacity)
			HashWord(key.hash, value);
		for (uint8_t value : mask.codes)
			HashWord(key.hash, value);
	}
	for (const auto &triangle : layer.triangles) {
		if ((!layer.masks.empty() && triangle.mask >= layer.masks.size()) || (layer.masks.empty() && triangle.mask != 0))
			return false;
		HashWord(key.hash, triangle.mask);
		const bool masked = !layer.masks.empty() && (!layer.masks[triangle.mask].opacity.empty() || layer.masks[triangle.mask].transparentZero);
		if (masked)
			++stats.maskedTriangles;
		else
			++stats.opaqueTriangles;
		for (std::size_t n = 0; n < 3; ++n) {
			const auto &vertex = triangle.vertices[n];
			if (!Finite({ vertex.x, vertex.height, vertex.z }) || !std::isfinite(triangle.uv[n][0]) || !std::isfinite(triangle.uv[n][1])
			    || std::abs(triangle.uv[n][0]) > 1000000 || std::abs(triangle.uv[n][1]) > 1000000)
				return false;
			HashFloat(key.hash, vertex.x);
			HashFloat(key.hash, vertex.height);
			HashFloat(key.hash, vertex.z);
			HashFloat(key.hash, triangle.uv[n][0]);
			HashFloat(key.hash, triangle.uv[n][1]);
		}
	}
	return true;
}

Projected Project(const ShadowMap &map, TownLightVector vertex)
{
	const Vector value { vertex.x, vertex.height, vertex.z };
	return { Dot(value, map.right), Dot(value, map.up), Dot(value, map.light) };
}

void IncludeGeometryBounds(ProjectionBounds &bounds, const ShadowMap &map, const TownShadowGeometryLayer &layer)
{
	for (const auto &triangle : layer.triangles) {
		for (const auto &vertex : triangle.vertices) {
			const auto projected = Project(map, vertex);
			bounds.minU = std::min(bounds.minU, projected.u);
			bounds.minV = std::min(bounds.minV, projected.v);
			bounds.maxU = std::max(bounds.maxU, projected.u);
			bounds.maxV = std::max(bounds.maxV, projected.v);
			bounds.empty = false;
		}
	}
}

bool Contains(const ProjectionBounds &outer, const ProjectionBounds &inner)
{
	return inner.empty || (!outer.empty && inner.minU >= outer.minU && inner.minV >= outer.minV && inner.maxU <= outer.maxU && inner.maxV <= outer.maxV);
}

void ConfigureGeometryProjection(ShadowMap &map, const TownShadowConfig &config)
{
	map.config = config;
	map.light = Normalize({ config.toLight.x, config.toLight.height, config.toLight.z });
	const Vector reference = std::abs(map.light.height) < 0.95F ? Vector { 0, 1, 0 } : Vector { 0, 0, 1 };
	map.right = Normalize(Cross(reference, map.light));
	map.up = Normalize(Cross(map.light, map.right));
}

bool RasterizeGeometry(ShadowMap &map, const TownShadowGeometryLayer &layer, bool staticLayer, TownShadowGeometryStats &stats)
{
	for (const auto &triangle : layer.triangles) {
		std::array<Projected, 3> vertices;
		for (std::size_t i = 0; i < 3; ++i) {
			vertices[i] = Project(map, triangle.vertices[i]);
			vertices[i].u = (vertices[i].u - map.minU) / map.texelU;
			vertices[i].v = (vertices[i].v - map.minV) / map.texelV;
		}
		const float area = Edge(vertices[0], vertices[1], vertices[2].u, vertices[2].v);
		if (std::abs(area) <= 0.000001F)
			continue;
		const int resolution = map.config.resolution;
		const int minU = std::max(0, static_cast<int>(std::floor(std::min({ vertices[0].u, vertices[1].u, vertices[2].u }))));
		const int maxU = std::min(resolution - 1, static_cast<int>(std::ceil(std::max({ vertices[0].u, vertices[1].u, vertices[2].u }))));
		const int minV = std::max(0, static_cast<int>(std::floor(std::min({ vertices[0].v, vertices[1].v, vertices[2].v }))));
		const int maxV = std::min(resolution - 1, static_cast<int>(std::ceil(std::max({ vertices[0].v, vertices[1].v, vertices[2].v }))));
		if (minU > maxU || minV > maxV)
			continue;
		const auto candidates = static_cast<std::size_t>(maxU - minU + 1) * (maxV - minV + 1);
		if (candidates > TownMaxShadowGeometryCandidateTests - stats.candidateTests)
			return false;
		if (staticLayer)
			++stats.staticRasterizedTriangles;
		else
			++stats.dynamicRasterizedTriangles;
		++map.stats.rasterizedTriangles;
		const float inverse = 1 / area;
		for (int v = minV; v <= maxV; ++v) {
			for (int u = minU; u <= maxU; ++u) {
				++stats.candidateTests;
				const float centerU = static_cast<float>(u) + 0.5F;
				const float centerV = static_cast<float>(v) + 0.5F;
				const float a = Edge(vertices[1], vertices[2], centerU, centerV) * inverse;
				const float b = Edge(vertices[2], vertices[0], centerU, centerV) * inverse;
				const float c = 1 - a - b;
				if (a < -0.000001F || b < -0.000001F || c < -0.000001F)
					continue;
				const float textureU = a * triangle.uv[0][0] + b * triangle.uv[1][0] + c * triangle.uv[2][0];
				const float textureV = a * triangle.uv[0][1] + b * triangle.uv[1][1] + c * triangle.uv[2][1];
				if (!layer.masks.empty() && !town_point_shadow_detail::Covered(layer.masks[triangle.mask], textureU, textureV)) {
					++stats.maskedSamples;
					continue;
				}
				++stats.opaqueSamples;
				const float depth = a * vertices[0].depth + b * vertices[1].depth + c * vertices[2].depth;
				float &stored = map.depth[static_cast<std::size_t>(v) * resolution + u];
				if (depth > stored) {
					stored = depth;
					++stats.depthWrites;
					++map.stats.depthWrites;
				}
			}
		}
	}
	return true;
}

} // namespace

bool BuildTownShadowMap(const std::vector<TownSceneModel> &scene, const TownShadowConfig &config)
{
	// The legacy architecture API and split geometry API share one publication.
	// A call through either API cannot accidentally reuse the other's cache.
	if (Geometry.published)
		Map.cached = false;
	Map.geometryPublication = 0;
	Geometry = GeometryCache {};
	GeometryStats = {};
	if (!ValidConfig(config)) {
		ClearTownShadowMap();
		return false;
	}
	uint64_t hash = 14695981039346656037ULL;
	HashFloat(hash, config.toLight.x);
	HashFloat(hash, config.toLight.height);
	HashFloat(hash, config.toLight.z);
	HashWord(hash, static_cast<uint32_t>(config.resolution));
	HashFloat(hash, config.depthBias);
	HashFloat(hash, config.slopeBias);
	HashWord(hash, static_cast<uint32_t>(config.pcfRadius));
	std::size_t inputTriangles = 0;
	for (const TownSceneModel &model : scene) {
		for (const TownSceneTriangle &triangle : TownSceneExteriorTriangles(model)) {
			++inputTriangles;
			for (const TownSceneVertex &vertex : triangle.vertices) {
				if (!Finite({ vertex.x, vertex.height, vertex.z })) {
					ClearTownShadowMap();
					return false;
				}
				HashFloat(hash, vertex.x);
				HashFloat(hash, vertex.height);
				HashFloat(hash, vertex.z);
			}
		}
	}
	if (Map.cached && Map.stats.sceneHash == hash && Map.stats.inputTriangles == inputTriangles)
		return true;
	const auto start = std::chrono::steady_clock::now();
	const std::size_t builds = Map.stats.buildCount + 1;
	Map.stats = {};
	Map.stats.buildCount = builds;
	Map.stats.sceneHash = hash;
	Map.stats.inputTriangles = inputTriangles;
	Map.stats.resolution = config.resolution;
	Map.config = config;
	Map.light = Normalize({ config.toLight.x, config.toLight.height, config.toLight.z });
	const Vector reference = std::abs(Map.light.height) < 0.95F ? Vector { 0, 1, 0 } : Vector { 0, 0, 1 };
	Map.right = Normalize(Cross(reference, Map.light));
	Map.up = Normalize(Cross(Map.light, Map.right));
	Map.ground = PrepareTownShadowReceiver(0, 1, 0);
	Map.depth.clear();
	Map.cached = true;
	if (inputTriangles == 0)
		return true;
	float minU = std::numeric_limits<float>::infinity(), minV = minU;
	float maxU = -minU, maxV = -minV;
	for (const TownSceneModel &model : scene) {
		for (const TownSceneTriangle &triangle : TownSceneExteriorTriangles(model)) {
			for (const TownSceneVertex &vertex : triangle.vertices) {
				const Projected projected = Project({ vertex.x, vertex.height, vertex.z });
				minU = std::min(minU, projected.u);
				minV = std::min(minV, projected.v);
				maxU = std::max(maxU, projected.u);
				maxV = std::max(maxV, projected.v);
			}
		}
	}
	// A border keeps PCF taps valid at a roof or wall's outer projected edge.
	const float padding = std::max(0.5F, std::max(maxU - minU, maxV - minV) * 4 / config.resolution);
	Map.minU = minU - padding;
	Map.minV = minV - padding;
	Map.texelU = std::max(0.001F, (maxU - minU + 2 * padding) / config.resolution);
	Map.texelV = std::max(0.001F, (maxV - minV + 2 * padding) / config.resolution);
	Map.depth.assign(static_cast<std::size_t>(config.resolution) * config.resolution, -std::numeric_limits<float>::infinity());
	Map.stats.depthBytes = Map.depth.size() * sizeof(float);
	for (const TownSceneModel &model : scene) {
		for (const TownSceneTriangle &triangle : TownSceneExteriorTriangles(model)) {
			std::array<Projected, 3> vertices;
			for (std::size_t i = 0; i < vertices.size(); ++i)
				vertices[i] = Project({ triangle.vertices[i].x, triangle.vertices[i].height, triangle.vertices[i].z });
			Rasterize(vertices);
		}
	}
	Map.stats.coveredTexels = static_cast<std::size_t>(std::count_if(Map.depth.begin(), Map.depth.end(), [](float depth) { return std::isfinite(depth); }));
	Map.stats.ready = Map.stats.coveredTexels != 0;
	Map.stats.buildMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return true;
}

bool BuildTownShadowGeometryMap(const TownShadowGeometryLayer &staticGeometry,
    const TownShadowGeometryLayer &dynamicGeometry, const TownShadowConfig &config)
{
	const auto start = std::chrono::steady_clock::now();
	auto phaseStart = start;
	enum class BuildPhase { Validation, Static, Dynamic, Publication };
	BuildPhase phase = BuildPhase::Validation;
	const auto milliseconds = [](auto since) {
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
	};
	TownShadowGeometryStats stats;
	stats.staticInputTriangles = staticGeometry.triangles.size();
	stats.dynamicInputTriangles = dynamicGeometry.triangles.size();
	stats.staticEpoch = staticGeometry.epoch;
	stats.staticIdentity = staticGeometry.identity;
	stats.staticRevision = staticGeometry.revision;
	stats.dynamicEpoch = dynamicGeometry.epoch;
	stats.dynamicIdentity = dynamicGeometry.identity;
	stats.dynamicRevision = dynamicGeometry.revision;
	const auto refuse = [&](TownShadowGeometryFailure failure, bool budget = false) {
		if (phase == BuildPhase::Validation)
			stats.validationMilliseconds = milliseconds(phaseStart);
		else if (phase == BuildPhase::Static)
			stats.staticBuildMilliseconds = milliseconds(phaseStart);
		else if (phase == BuildPhase::Dynamic)
			stats.dynamicBuildMilliseconds = milliseconds(phaseStart);
		stats.ready = false;
		stats.failure = failure;
		stats.budgetLimited = budget;
		stats.buildMilliseconds = milliseconds(start);
		ClearTownShadowMap();
		GeometryStats = stats;
		return false;
	};
	if (!ValidConfig(config))
		return refuse(TownShadowGeometryFailure::InvalidInput);
	if (staticGeometry.triangles.size() > TownMaxShadowGeometryTriangles
	    || dynamicGeometry.triangles.size() > TownMaxShadowGeometryTriangles - staticGeometry.triangles.size()
	    || staticGeometry.masks.size() > TownMaxShadowGeometryMasks
	    || dynamicGeometry.masks.size() > TownMaxShadowGeometryMasks - staticGeometry.masks.size())
		return refuse(TownShadowGeometryFailure::Capacity, true);
	stats.masks = staticGeometry.masks.size() + dynamicGeometry.masks.size();
	if (LayerPresent(staticGeometry) && LayerPresent(dynamicGeometry) && staticGeometry.epoch != dynamicGeometry.epoch)
		return refuse(TownShadowGeometryFailure::InvalidInput);
	GeometryKey staticKey, dynamicKey;
	if (!ValidateGeometryLayer(staticGeometry, staticKey, stats) || !ValidateGeometryLayer(dynamicGeometry, dynamicKey, stats))
		return refuse(stats.budgetLimited ? TownShadowGeometryFailure::Capacity : TownShadowGeometryFailure::InvalidInput, stats.budgetLimited);
	stats.staticHash = staticKey.hash;
	stats.dynamicHash = dynamicKey.hash;
	uint64_t sceneHash = 14695981039346656037ULL;
	HashConfig(sceneHash, config);
	HashWide(sceneHash, staticKey.hash);
	HashWide(sceneHash, dynamicKey.hash);
	stats.sceneHash = sceneHash;
	ShadowMap projection;
	ConfigureGeometryProjection(projection, config);
	ProjectionBounds bounds;
	IncludeGeometryBounds(bounds, projection, staticGeometry);
	IncludeGeometryBounds(bounds, projection, dynamicGeometry);
	stats.validationMilliseconds = milliseconds(start);
	const bool staticHit = Geometry.valid && staticKey == Geometry.staticKey && SameConfig(config, Geometry.staticMap.config)
	    && Contains(Geometry.bounds, bounds);
	stats.staticCacheHit = staticHit;
	stats.projectionRebuilt = !staticHit;
	if (staticHit && Geometry.published && dynamicKey == Geometry.dynamicKey) {
		stats.ready = true;
		stats.cacheHit = true;
		stats.buildCount = Map.stats.buildCount;
		stats.staticBuildCount = Geometry.staticBuildCount;
		stats.depthBytes = Map.depth.size() * sizeof(float);
		stats.cacheDepthBytes = Geometry.staticMap.depth.size() * sizeof(float);
		stats.buildMilliseconds = milliseconds(start);
		Map.stats.buildMilliseconds = stats.buildMilliseconds;
		GeometryStats = stats;
		return true;
	}
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
		ShadowMap nextStatic;
		if (!staticHit) {
			const auto staticStart = std::chrono::steady_clock::now();
			phaseStart = staticStart;
			phase = BuildPhase::Static;
			nextStatic = std::move(projection);
			if (!bounds.empty) {
				const float padding = std::max(0.5F, std::max(bounds.maxU - bounds.minU, bounds.maxV - bounds.minV) * 4 / config.resolution);
				nextStatic.minU = bounds.minU - padding;
				nextStatic.minV = bounds.minV - padding;
				nextStatic.texelU = std::max(0.001F, (bounds.maxU - bounds.minU + 2 * padding) / config.resolution);
				nextStatic.texelV = std::max(0.001F, (bounds.maxV - bounds.minV + 2 * padding) / config.resolution);
				nextStatic.depth.assign(static_cast<std::size_t>(config.resolution) * config.resolution, -std::numeric_limits<float>::infinity());
			}
			if (!RasterizeGeometry(nextStatic, staticGeometry, true, stats)) {
				stats.staticBuildMilliseconds = milliseconds(staticStart);
				return refuse(TownShadowGeometryFailure::Capacity, true);
			}
			stats.staticBuildMilliseconds = milliseconds(staticStart);
		}
		const auto dynamicStart = std::chrono::steady_clock::now();
		phaseStart = dynamicStart;
		phase = BuildPhase::Dynamic;
		// This owns its depth. Failure at any point cannot mutate the previous
		// map or expose a partial static/dynamic combination to CPU or GPU.
		ShadowMap next = staticHit ? Geometry.staticMap : nextStatic;
		if (!RasterizeGeometry(next, dynamicGeometry, false, stats)) {
			stats.dynamicBuildMilliseconds = milliseconds(dynamicStart);
			return refuse(TownShadowGeometryFailure::Capacity, true);
		}
		stats.dynamicBuildMilliseconds = milliseconds(dynamicStart);
		phase = BuildPhase::Publication;
		next.stats.buildCount = Map.stats.buildCount + 1;
		next.stats.inputTriangles = staticGeometry.triangles.size() + dynamicGeometry.triangles.size();
		next.stats.resolution = config.resolution;
		next.stats.sceneHash = sceneHash;
		next.stats.depthBytes = next.depth.size() * sizeof(float);
		next.stats.coveredTexels = static_cast<std::size_t>(std::count_if(next.depth.begin(), next.depth.end(), [](float depth) { return std::isfinite(depth); }));
		next.stats.ready = next.stats.coveredTexels != 0;
		next.stats.buildMilliseconds = milliseconds(start);
		next.cached = false; // Legacy cache keys have a distinct provenance.
		if (!staticHit) {
			Geometry.staticMap = std::move(nextStatic);
			Geometry.staticKey = staticKey;
			Geometry.bounds = bounds;
			++Geometry.staticBuildCount;
			Geometry.valid = true;
		}
		Geometry.dynamicKey = dynamicKey;
		Geometry.published = true;
		if (++GeometryPublicationSequence == 0)
			++GeometryPublicationSequence;
		next.geometryPublication = GeometryPublicationSequence;
		Map = std::move(next);
		Map.ground = PrepareTownShadowReceiver(0, 1, 0);
		stats.ready = true;
		stats.buildCount = Map.stats.buildCount;
		stats.staticBuildCount = Geometry.staticBuildCount;
		stats.depthBytes = Map.depth.size() * sizeof(float);
		stats.cacheDepthBytes = Geometry.staticMap.depth.size() * sizeof(float);
		stats.buildMilliseconds = milliseconds(start);
		GeometryStats = stats;
		return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (const std::bad_alloc &) {
		return refuse(TownShadowGeometryFailure::Allocation, true);
	}
#endif
}

const TownShadowGeometryStats &GetTownShadowGeometryStats()
{
	return GeometryStats;
}

TownShadowReceiver PrepareTownShadowReceiver(float normalX, float normalHeight, float normalZ)
{
	Vector normal { normalX, normalHeight, normalZ };
	if (!Finite(normal) || Dot(normal, normal) <= 0.000001F)
		normal = { 0, 1, 0 };
	normal = Normalize(normal);
	const float towardsLight = Dot(normal, Map.light);
	TownShadowReceiver result;
	result.depthBias = Map.config.depthBias + Map.config.slopeBias * (1 - std::abs(towardsLight));
	if (std::abs(towardsLight) > 0.03F) {
		// Compare each tap against the receiver plane at that tap's position.
		// A sloped roof therefore does not mistake its own neighbouring samples
		// for an occluder, while a separate roof above it still casts a shadow.
		result.depthSlopeU = -Dot(normal, Map.right) / towardsLight;
		result.depthSlopeV = -Dot(normal, Map.up) / towardsLight;
	}
	return result;
}

float SampleTownShadow(float x, float height, float z)
{
	return SampleTownShadow(x, height, z, Map.ground);
}

float SampleTownShadow(float x, float height, float z, float normalX, float normalHeight, float normalZ)
{
	return SampleTownShadow(x, height, z, PrepareTownShadowReceiver(normalX, normalHeight, normalZ));
}

float SampleTownShadow(float x, float height, float z, const TownShadowReceiver &receiver)
{
	if (!Map.stats.ready || !Finite({ x, height, z }))
		return 0;
	const Projected projected = Project({ x, height, z });
	const float texelU = (projected.u - Map.minU) / Map.texelU;
	const float texelV = (projected.v - Map.minV) / Map.texelV;
	const int resolution = Map.config.resolution;
	if (texelU < 0 || texelV < 0 || texelU >= resolution || texelV >= resolution)
		return 0;
	const int centerU = static_cast<int>(texelU);
	const int centerV = static_cast<int>(texelV);
	const int radius = Map.config.pcfRadius;
	int occluded = 0;
	int taps = 0;
	for (int dv = -radius; dv <= radius; ++dv) {
		for (int du = -radius; du <= radius; ++du) {
			++taps;
			const int u = centerU + du;
			const int v = centerV + dv;
			if (u < 0 || v < 0 || u >= resolution || v >= resolution)
				continue;
			const float planeDepth = projected.depth
			    + (static_cast<float>(u) + 0.5F - texelU) * Map.texelU * receiver.depthSlopeU
			    + (static_cast<float>(v) + 0.5F - texelV) * Map.texelV * receiver.depthSlopeV;
			const float stored = Map.depth[static_cast<std::size_t>(v) * resolution + u];
			occluded += stored > planeDepth + receiver.depthBias;
		}
	}
	return static_cast<float>(occluded) / static_cast<float>(taps);
}

void ClearTownShadowMap()
{
	Map = ShadowMap {};
	Geometry = GeometryCache {};
	GeometryStats = {};
}

const TownShadowStats &GetTownShadowStats()
{
	return Map.stats;
}

TownShadowDirection GetTownShadowLightDirection()
{
	return { Map.light.x, Map.light.height, Map.light.z };
}

TownShadowMapView GetTownShadowMapView()
{
	if (!Map.stats.ready)
		return {};
	return { Map.depth, Map.config,
		{ Map.light.x, Map.light.height, Map.light.z },
		{ Map.right.x, Map.right.height, Map.right.z },
		{ Map.up.x, Map.up.height, Map.up.z },
		Map.minU, Map.minV, Map.texelU, Map.texelV,
		Map.geometryPublication != 0 ? Map.geometryPublication : Map.stats.sceneHash };
}

} // namespace devilution
