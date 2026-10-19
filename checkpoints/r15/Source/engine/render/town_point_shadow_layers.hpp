#pragma once

#include "engine/render/town_point_shadow.hpp"

namespace devilution {

/** Both completed layers use exactly the same source space and six-face grid.
 * Merge never publishes a partial layer or changes the existing PCF policy. */
inline bool MergeTownPointShadowLayers(TownPointShadowMaps &output,
    const TownPointShadowMaps &fixed, const TownPointShadowMaps &moving,
    uint64_t identity, uint64_t revision)
{
    const auto refuse=[&]() { output={};return false; };
    if(identity==0 || revision==0 || !fixed.stats.ready || !moving.stats.ready || fixed.lightCount==0 || fixed.lightCount>TownMaxFireShadowLights
        || fixed.lightCount!=moving.lightCount || fixed.resolution!=TownFireShadowResolution
        || fixed.resolution!=moving.resolution || fixed.depth.size()!=moving.depth.size()
        || fixed.depth.size()!=fixed.lightCount*6*TownFireShadowResolution*TownFireShadowResolution
        || fixed.stats.acceptedTriangles>MaxFireShadowTriangles
        || moving.stats.acceptedTriangles>MaxFireShadowTriangles-fixed.stats.acceptedTriangles
        || fixed.stats.masks>MaxFireShadowMasks || moving.stats.masks>MaxFireShadowMasks-fixed.stats.masks)
        return refuse();
    for(size_t i=0;i<fixed.lightCount;++i) {
        const auto &a=fixed.lights[i];const auto &b=moving.lights[i];
        if(!town_point_shadow_detail::ValidLight(a) || !town_point_shadow_detail::ValidLight(b))return refuse();
        if(a.position.x!=b.position.x || a.position.height!=b.position.height
            || a.position.z!=b.position.z || a.radius!=b.radius) return refuse();
    }
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
#endif
        TownPointShadowMaps candidate=fixed;
        for(size_t i=0;i<candidate.depth.size();++i) {
            const float a=fixed.depth[i],b=moving.depth[i];
            if(std::isnan(a) || std::isnan(b) || a<=0 || b<=0) return refuse();
            candidate.depth[i]=std::min(a,b);
        }
        candidate.identity=identity;candidate.revision=revision;
        candidate.stats.inputTriangles+=moving.stats.inputTriangles;
        candidate.stats.acceptedTriangles+=moving.stats.acceptedTriangles;
        candidate.stats.masks+=moving.stats.masks;
        candidate.stats.bvhNodes+=moving.stats.bvhNodes;
        candidate.stats.scratchBytes=std::max(candidate.stats.scratchBytes,moving.stats.scratchBytes);
        candidate.stats.rays+=moving.stats.rays;
        candidate.stats.boxTests+=moving.stats.boxTests;
        candidate.stats.triangleTests+=moving.stats.triangleTests;
        candidate.stats.maskedIntersections+=moving.stats.maskedIntersections;
        candidate.stats.opaqueIntersections+=moving.stats.opaqueIntersections;
        candidate.stats.buildMilliseconds+=moving.stats.buildMilliseconds;
        output=std::move(candidate);
        return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    } catch(const std::bad_alloc &) { return refuse(); }
#endif
}

} // namespace devilution
