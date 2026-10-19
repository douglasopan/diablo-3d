#include "engine/render/surface_region_props.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

#include "engine/render/town_surroundings_asset_identity.hpp"

namespace devilution {
namespace {

constexpr size_t ExpectedPropRanges = 65;
constexpr size_t ExpectedPropTriangles = 32768;
static_assert(SurfacePropBindings.size() == ExpectedPropRanges);
static_assert(SurfacePropSourceSha256 == TownSurroundingsExpectedSha256);
static_assert(TownSurroundingsExpectedSha256
    == "2109d74823d65f50e3dba20412e8f06e78507d6213c484dd5bda52880dd9b878");

bool TriangleFinite(const TownSurroundingsTriangle &triangle)
{
	for (const auto &v : triangle.vertices) {
		if (!std::isfinite(v.x) || !std::isfinite(v.height) || !std::isfinite(v.z))
			return false;
	}
	const auto &c = triangle.color;
	return std::isfinite(c.red) && std::isfinite(c.green) && std::isfinite(c.blue)
	    && c.red >= 0 && c.red <= 1 && c.green >= 0 && c.green <= 1
	    && c.blue >= 0 && c.blue <= 1;
}

std::shared_ptr<const SurfaceRegionPropSet> CopyPropRanges(
    const TownSurroundingsVisualScene &scene)
{
	auto result = std::make_shared<SurfaceRegionPropSet>();
	result->identity = scene.identity;
	result->ranges.reserve(ExpectedPropRanges);
	result->triangles.reserve(ExpectedPropTriangles);
	for (size_t cellIndex = 0; cellIndex < scene.cells.size(); ++cellIndex) {
		const auto &cell = scene.cells[cellIndex];
		if (cell.kind != 1)
			continue;
		if (cellIndex > std::numeric_limits<uint32_t>::max()
		    || cell.triangleCount == 0 || cell.firstTriangle > scene.triangles.size()
		    || cell.triangleCount > scene.triangles.size() - cell.firstTriangle
		    || result->ranges.size() >= ExpectedPropRanges
		    || cell.triangleCount > ExpectedPropTriangles - result->triangles.size())
			return {};
		const auto &binding = SurfacePropBindings[result->ranges.size()];
		if (binding.sourceFirstTriangle != cell.firstTriangle || binding.triangleCount != cell.triangleCount
		    || binding.featureId.empty() || !std::isfinite(binding.deltaYNative)
		    || std::abs(binding.deltaYNative) > 1.0 / surface::CellMetres)
			return {};
		SurfaceRegionPropRange range;
		range.featureId = binding.featureId;
		range.supportAnchorWorldX = binding.anchorWorldX;
		range.supportAnchorWorldZ = binding.anchorWorldZ;
		range.sourceSupportHeightMetres = binding.sourceSupportHeightMetres;
		range.triangleSupportHeightMetres = binding.triangleSupportHeightMetres;
		range.placementDeltaYNative = binding.deltaYNative;
		range.sourceCellIndex = static_cast<uint32_t>(cellIndex);
		range.sourceFirstTriangle = cell.firstTriangle;
		range.firstTriangle = static_cast<uint32_t>(result->triangles.size());
		range.triangleCount = cell.triangleCount;
		range.anchorWorldX = cell.x;
		range.anchorWorldZ = cell.z;
		range.minWorldX = range.minWorldZ = std::numeric_limits<float>::infinity();
		range.maxWorldX = range.maxWorldZ = -std::numeric_limits<float>::infinity();
		for (size_t i = cell.firstTriangle, end = i + cell.triangleCount; i < end; ++i) {
			const auto &triangle = scene.triangles[i];
			if (!TriangleFinite(triangle))
				return {};
			for (const auto &v : triangle.vertices) {
				range.minWorldX = std::min(range.minWorldX, v.x);
				range.minWorldZ = std::min(range.minWorldZ, v.z);
				range.maxWorldX = std::max(range.maxWorldX, v.x);
				range.maxWorldZ = std::max(range.maxWorldZ, v.z);
			}
			auto adjusted = triangle;
			for (auto &vertex : adjusted.vertices)
				vertex.height = static_cast<float>(static_cast<double>(vertex.height) + binding.deltaYNative);
			if (!TriangleFinite(adjusted)) return {};
			result->triangles.push_back(adjusted);
		}
		result->ranges.push_back(range);
	}
	if (result->ranges.size() != ExpectedPropRanges
	    || result->triangles.size() != ExpectedPropTriangles)
		return {};
	return result;
}

} // namespace

void ResetSurfaceRegionProps(SurfaceRegionPropCache &cache) noexcept
{
	cache = {};
}

static bool PrepareSurfaceRegionPropsImpl(SurfaceRegionPropCache &cache,
    uint64_t resourceEpoch, surface::RegionId region,
    const TownSurroundingsVisualScene *scene,
    SurfaceRegionPropFrame &output, std::string &failure)
{
	output = {};
	failure.clear();
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
	if (resourceEpoch == 0 || !surface::ValidRegionId(region) || region.IsTown()) {
		ResetSurfaceRegionProps(cache);
		failure = "surface-props-invalid-context";
		return false;
	}
	if (cache.resourceEpoch != resourceEpoch) {
		ResetSurfaceRegionProps(cache);
		cache.resourceEpoch = resourceEpoch;
	}
	if (scene == nullptr || !scene->valid || scene->identity != TownSurroundingsExpectedSha256) {
		cache.published.reset();
		failure = "surface-props-asset-refused";
		return false;
	}
	if (!cache.published || cache.published->identity != scene->identity
	    || cache.published->placementRevision != SurfacePropPlacementRevision) {
		cache.published.reset();
		auto candidate = CopyPropRanges(*scene);
		if (!candidate) {
			failure = "surface-props-ranges-refused";
			return false;
		}
		cache.published = std::move(candidate);
	}
	SurfaceRegionPropFrame frame;
	frame.source = cache.published;
	frame.regionId = region;
	frame.worldOffset = surface::WorldOffset(region);
	const double minimumX = frame.worldOffset.x - 1.0;
	const double minimumZ = frame.worldOffset.z - 1.0;
	const double maximumX = frame.worldOffset.x + surface::StorageSize - 1.0;
	const double maximumZ = frame.worldOffset.z + surface::StorageSize - 1.0;
	frame.rangeIndices.reserve(frame.source->ranges.size());
	for (size_t i = 0; i < frame.source->ranges.size(); ++i) {
		const auto &range = frame.source->ranges[i];
		// Inclusive conservative footprint intersection: whole triangles stay
		// identical through overlapping region ghosts. Town uses source29 Y;
		// the recorded support-origin delta is verified separately at its border.
		if (range.maxWorldX < minimumX || range.minWorldX > maximumX
		    || range.maxWorldZ < minimumZ || range.minWorldZ > maximumZ)
			continue;
		frame.rangeIndices.push_back(static_cast<uint32_t>(i));
	}
	output = std::move(frame);
	return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch (const std::bad_alloc &) {
		output = {};
		cache.published.reset();
		failure = "props-oom";
		return false;
	}
#endif
}

bool PrepareSurfaceRegionProps(SurfaceRegionPropCache &cache,
    uint64_t resourceEpoch, surface::RegionId region,
    SurfaceRegionPropFrame &output, std::string &failure)
{
	// Preserve the legacy loader path and avoid asset lookup for invalid contexts.
	const auto *scene = resourceEpoch != 0 && surface::ValidRegionId(region) && !region.IsTown()
	    ? GetTownSurroundingsVisualScene() : nullptr;
	return PrepareSurfaceRegionPropsImpl(cache, resourceEpoch, region, scene, output, failure);
}

bool PrepareSurfaceRegionPropsFromOwned(SurfaceRegionPropCache &cache,
    uint64_t resourceEpoch, surface::RegionId region,
    std::shared_ptr<const TownSurroundingsVisualScene> source,
    SurfaceRegionPropFrame &output, std::string &failure)
{
	return PrepareSurfaceRegionPropsImpl(cache, resourceEpoch, region, source.get(), output, failure);
}

} // namespace devilution
