#include "engine/render/town_shadow.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>

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
};

ShadowMap Map;

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

} // namespace

bool BuildTownShadowMap(const std::vector<TownSceneModel> &scene, const TownShadowConfig &config)
{
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
		Map.minU, Map.minV, Map.texelU, Map.texelV, Map.stats.sceneHash };
}

} // namespace devilution
