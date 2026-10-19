#pragma once

// Pure deterministic R2 sampling only. No camera/projection, renderer state,
// allocation, native simulation, RNG or backend is owned by this header.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace devilution {

inline constexpr uint32_t TownFireVolumeMaximumSamplingExtent = 16384;
inline constexpr uint32_t TownFireVolumeMaximumSamplingStride = 65535;
inline constexpr uint64_t TownFireVolumeMaximumSamplingBudget = 65536;

struct TownFireVolumeSamplingGrid {
    uint32_t stride = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t rays = 0;

    explicit constexpr operator bool() const
    {
        return stride > 0 && stride <= TownFireVolumeMaximumSamplingStride
            && width > 0 && width <= TownFireVolumeMaximumSamplingExtent
            && height > 0 && height <= TownFireVolumeMaximumSamplingExtent
            && rays > 0 && rays <= TownFireVolumeMaximumSamplingBudget
            && rays == static_cast<uint64_t>(width) * height;
    }
};

namespace town_fire_volume_sampling_detail {
inline constexpr uint32_t CeilDiv(uint32_t value, uint32_t divisor)
{
    // Callers prove divisor>0. Widen before addition; never rely on uint32
    // wraparound even when this helper is used with a large public argument.
    return static_cast<uint32_t>((static_cast<uint64_t>(value) + divisor - 1) / divisor);
}
inline constexpr bool ValidAxis(uint32_t extent, uint32_t stride)
{
    return extent > 0 && extent <= TownFireVolumeMaximumSamplingExtent
        && stride > 0 && stride <= TownFireVolumeMaximumSamplingStride;
}
} // namespace town_fire_volume_sampling_detail

/** Minimum integer stride whose ceil-divided grid fits this one source's
 * supplied remaining budget. Budget0 and budget>65536 are refused, not clamped.
 * The host owns sharing the global budget among up to four actual sources.
 * Changing the sampling grid does not shrink the source's composite ROI. */
inline constexpr TownFireVolumeSamplingGrid ChooseTownFireVolumeSamplingGrid(
    uint32_t roiWidth, uint32_t roiHeight, uint64_t budget)
{
    using namespace town_fire_volume_sampling_detail;
    if (roiWidth == 0 || roiHeight == 0
        || roiWidth > TownFireVolumeMaximumSamplingExtent || roiHeight > TownFireVolumeMaximumSamplingExtent
        || budget == 0 || budget > TownFireVolumeMaximumSamplingBudget)
        return {};
    uint32_t lower = 1;
    uint32_t upper = std::max(roiWidth, roiHeight);
    // ceil(W/s)*ceil(H/s) is monotone nonincreasing. At max(W,H) there is
    // exactly one ray, so a valid nonzero budget always has a solution.
    while (lower < upper) {
        const uint32_t middle = lower + (upper - lower) / 2;
        const uint64_t rays = static_cast<uint64_t>(CeilDiv(roiWidth, middle)) * CeilDiv(roiHeight, middle);
        if (rays <= budget)
            upper = middle;
        else
            lower = middle + 1;
    }
    const uint32_t width = CeilDiv(roiWidth, lower);
    const uint32_t height = CeilDiv(roiHeight, lower);
    const uint64_t rays = static_cast<uint64_t>(width) * height;
    if (lower > TownFireVolumeMaximumSamplingStride || rays > budget)
        return {};
    return { lower, width, height, rays };
}

/** ROI-local raster SAMPLE CENTER, already including the half-pixel position.
 * center=offset+0.5*min(stride,extent-offset). A stride1 cell at index0 has
 * center0.5. Add the ROI minimum, then divide by sampleFactor for the logical
 * camera. Do NOT add another0.5. The shortened last cell uses its own center.
 * Invalid input returnsfalse and resets the output; there is no NaN sentinel. */
inline bool TownFireVolumeSamplingCellCenter(uint32_t index, uint32_t extent,
    uint32_t stride, float &center)
{
    using namespace town_fire_volume_sampling_detail;
    center = 0;
    if (!ValidAxis(extent, stride) || index >= CeilDiv(extent, stride))
        return false;
    const uint64_t offset = static_cast<uint64_t>(index) * stride;
    if (offset >= extent)
        return false;
    const uint32_t cellWidth = std::min(stride, extent - static_cast<uint32_t>(offset));
    center = static_cast<float>(offset) + .5F * cellWidth;
    // Integer and half-integer centers <=16384 are exactly representable in
    // float32, including the final partial cell.
    return std::isfinite(center) && center > 0 && center < extent;
}

struct TownFireVolumeSamplingAxisBlend {
    uint32_t lower = 0;
    uint32_t upper = 0;
    float upperWeight = 0;
};

/** Bracket a ROI-local fine raster center by the ACTUAL coarse sample centers.
 * The final shortened cell makes index/stride interpolation incorrect. Finite
 * coordinates outside the outer centers clamp to the border sample; nonfinite
 * coordinates or invalid axes are refused with a cleared result. */
inline bool TownFireVolumeSamplingAxis(float center, uint32_t extent,
    uint32_t stride, TownFireVolumeSamplingAxisBlend &blend)
{
    using namespace town_fire_volume_sampling_detail;
    blend = {};
    if (!std::isfinite(center) || !ValidAxis(extent, stride))
        return false;
    const uint32_t count = CeilDiv(extent, stride);
    float first = 0;
    float last = 0;
    if (!TownFireVolumeSamplingCellCenter(0, extent, stride, first)
        || !TownFireVolumeSamplingCellCenter(count - 1, extent, stride, last))
        return false;
    if (count == 1 || center <= first)
        return true; // lower=upper=0, weight0
    if (center >= last) {
        blend.lower = blend.upper = count - 1;
        return true;
    }
    uint32_t lower = 0;
    uint32_t upper = count - 1;
    while (lower + 1 < upper) {
        const uint32_t middle = lower + (upper - lower) / 2;
        float position = 0;
        if (!TownFireVolumeSamplingCellCenter(middle, extent, stride, position))
            return false;
        if (center < position)
            upper = middle;
        else
            lower = middle;
    }
    float a = 0;
    float b = 0;
    if (!TownFireVolumeSamplingCellCenter(lower, extent, stride, a)
        || !TownFireVolumeSamplingCellCenter(upper, extent, stride, b) || !(b > a))
        return false;
    const float weight = (center - a) / (b - a);
    if (!std::isfinite(weight) || weight < 0 || weight > 1)
        return false;
    blend = { lower, upper, weight };
    return true;
}

} // namespace devilution
