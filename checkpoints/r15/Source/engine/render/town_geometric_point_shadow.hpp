#pragma once

// Separate Town32 geometry field. C1's four-light/radius16 contract is unchanged.
#include <atomic>
#include <bit>
#include "engine/render/town_shadow.hpp"

namespace devilution {

inline constexpr size_t TownGeometricPointMaximumSources = 32;
inline constexpr float TownGeometricPointMaximumRadius = 256;
inline constexpr size_t TownGeometricPointMaximumTriangles = 524288;
inline constexpr size_t TownGeometricPointMaximumMasks = 512;
inline constexpr size_t TownGeometricPointMaximumMaskBytes = 16 * 1024 * 1024;
inline constexpr size_t TownGeometricPointMaximumScratchBytes = 32 * 1024 * 1024;
inline constexpr size_t TownGeometricPointMaximumKeyBytes = 64 * 1024 * 1024;
inline constexpr size_t TownGeometricPointMaximumKeyPeakBytes = 128 * 1024 * 1024;
inline constexpr size_t TownGeometricPointMaximumKeyStagingBytes = 64 * 1024 * 1024;
inline constexpr size_t TownGeometricPointCubeTexels = 6 * TownFireShadowResolution * TownFireShadowResolution;
inline constexpr size_t TownGeometricPointMaximumPublishedDepthBytes = TownGeometricPointMaximumSources * TownGeometricPointCubeTexels * sizeof(float);
static_assert(TownGeometricPointMaximumPublishedDepthBytes == 3 * 1024 * 1024);

struct TownGeometricPointSource {
    TownLightVector position;
    float radius = 0;
};

struct TownGeometricPointShadowView {
    std::span<const float> depth;
    std::span<const TownGeometricPointSource> sources;
    int resolution = TownFireShadowResolution;
    uint64_t epoch = 0, identity = 0, revision = 0;
};

struct TownGeometricPointShadowStats {
    bool ready = false, budgetLimited = false;
    TownPointShadowFailure failure = TownPointShadowFailure::None;
    size_t admittedSources = 0, rejectedSources = 0;
    size_t staticTriangles = 0, dynamicTriangles = 0, masks = 0, maskBytes = 0;
    size_t staticBvhBuilds = 0, dynamicBvhBuilds = 0;
    size_t staticMapBuilds = 0, dynamicMapBuilds = 0, staticMapCacheHits = 0, dynamicMapCacheHits = 0;
    size_t immutableLayerCacheHits = 0, exactComparedTriangles = 0, roiTests = 0;
    size_t scratchBytes = 0, scratchPeakBytes = 0, keyBytes = 0, keyPeakBytes = 0, keyStagingPeakBytes = 0;
    size_t publishedDepthBytes = 0, cachedDepthBytes = 0, retainedBytes = 0;
    std::array<uint64_t, TownGeometricPointMaximumSources> staticRelevantRevisions {}, dynamicRelevantRevisions {};
    TownPointShadowStats trace;
    double validationMilliseconds = 0, staticBvhBuildMilliseconds = 0, dynamicBvhBuildMilliseconds = 0;
    double staticBuildMilliseconds = 0, dynamicBuildMilliseconds = 0, buildMilliseconds = 0;
};

namespace town_geometric_point_detail {
using namespace town_point_shadow_detail;
inline std::atomic<uint64_t> NextIdentity { 1 };
inline uint64_t AllocateIdentity()
{
    auto value = NextIdentity.load(std::memory_order_relaxed);
    while (value != std::numeric_limits<uint64_t>::max())
        if (NextIdentity.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
    return 0;
}
inline bool SameFloat(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }
inline bool SameSource(const TownGeometricPointSource &a, const TownGeometricPointSource &b)
{
    return SameFloat(a.position.x,b.position.x) && SameFloat(a.position.height,b.position.height)
        && SameFloat(a.position.z,b.position.z) && SameFloat(a.radius,b.radius);
}
inline bool ValidSource(const TownGeometricPointSource &source)
{
    return Finite(source.position) && std::isfinite(source.radius) && source.radius > 0 && source.radius <= TownGeometricPointMaximumRadius;
}
inline bool SameTriangle(const TownPointShadowTriangle &a, const TownPointShadowTriangle &b)
{
    if (a.mask != b.mask) return false;
    for (size_t i=0;i<3;++i)
        if (!SameFloat(a.vertices[i].x,b.vertices[i].x) || !SameFloat(a.vertices[i].height,b.vertices[i].height)
            || !SameFloat(a.vertices[i].z,b.vertices[i].z) || !SameFloat(a.uv[i][0],b.uv[i][0]) || !SameFloat(a.uv[i][1],b.uv[i][1])) return false;
    return true;
}
inline Bounds TriangleBounds(const TownPointShadowTriangle &triangle)
{
    Bounds bounds; for (const auto vertex:triangle.vertices) Include(bounds,vertex); return bounds;
}
inline bool SphereTouches(const Bounds &bounds,const TownGeometricPointSource &source)
{
    const auto gap=[](float p,float low,float high){return std::max({low-p,0.0F,p-high});};
    const float x=gap(source.position.x,bounds.minimum.x,bounds.maximum.x);
    const float y=gap(source.position.height,bounds.minimum.height,bounds.maximum.height);
    const float z=gap(source.position.z,bounds.minimum.z,bounds.maximum.z);
    return x*x+y*y+z*z <= source.radius*source.radius;
}
inline constexpr size_t NodeBound(size_t triangles)
{
    return triangles == 0 ? 0 : 2 * std::bit_ceil((triangles + 7) / 8) - 1;
}
// Both layers together contain <= MaximumTriangles; this deliberately looser
// two-tree bound still fits, and catches a changed inherited BVH ABI.
static_assert(TownGeometricPointMaximumTriangles*sizeof(Primitive)
    + 2*NodeBound(TownGeometricPointMaximumTriangles)*sizeof(Node) <= TownGeometricPointMaximumScratchBytes);
struct OwnedMask {
    int width=0,height=0; bool repeat=false,transparentZero=false;
    std::vector<uint8_t> opacity,codes;
    bool Same(const TownPointShadowMask &mask) const
    {
        return width==mask.width && height==mask.height && repeat==mask.repeat && transparentZero==mask.transparentZero
            && opacity.size()==mask.opacity.size() && codes.size()==mask.codes.size()
            && std::equal(opacity.begin(),opacity.end(),mask.opacity.begin()) && std::equal(codes.begin(),codes.end(),mask.codes.begin());
    }
    size_t Bytes() const { return opacity.capacity()+codes.capacity(); }
};
struct LayerCache {
    uint64_t epoch=0,identity=0,revision=0;
    std::vector<TownPointShadowTriangle> triangles;
    std::vector<OwnedMask> masks;
    std::vector<Primitive> primitives;
    std::vector<Node> nodes;
    size_t KeyBytes() const
    {
        size_t bytes=triangles.capacity()*sizeof(TownPointShadowTriangle)+masks.capacity()*sizeof(OwnedMask);
        for (const auto &mask:masks) bytes+=mask.Bytes(); return bytes;
    }
    size_t ScratchBytes() const {return primitives.capacity()*sizeof(Primitive)+nodes.capacity()*sizeof(Node);}
};
struct LightCache {
    TownGeometricPointSource source;
    bool valid=false;
    std::vector<float> fixed,moving;
    uint64_t fixedRevision=0,movingRevision=0;
};
} // namespace town_geometric_point_detail

/** Single-render-thread owner. Input geometry/masks are borrowed only during
 * Build. Cache keys own one exact copy per layer, never one copy per light.
 * An unchanged (epoch,identity,revision) is the producer's immutable-layer
 * promise; do not mutate its bytes without a new revision. Such hits avoid
 * serializing static geometry. Changed revisions are compared byte-exactly.
 * Returned views are invalidated at the NEXT Build/Clear, including failure.
 * Clear preserves instance identity and its monotonic publication sequence. */
class TownGeometricPointShadowCache {
public:
    TownGeometricPointShadowCache() : Identity(town_geometric_point_detail::AllocateIdentity()) {}
    TownGeometricPointShadowCache(const TownGeometricPointShadowCache &)=delete;
    TownGeometricPointShadowCache &operator=(const TownGeometricPointShadowCache &)=delete;
    TownGeometricPointShadowCache(TownGeometricPointShadowCache &&)=delete;
    TownGeometricPointShadowCache &operator=(TownGeometricPointShadowCache &&)=delete;

    bool Build(const TownShadowGeometryLayer &fixed,const TownShadowGeometryLayer &moving,
        std::span<const TownGeometricPointSource> sources,uint64_t currentEpoch)
    {
        using namespace town_geometric_point_detail;
        const auto begin=Clock::now();
        TownGeometricPointShadowStats stats;
        stats.staticTriangles=fixed.triangles.size();stats.dynamicTriangles=moving.triangles.size();
        stats.masks=fixed.masks.size()+moving.masks.size();
        const bool wasReady=Ready;
        Ready=false; // No partially updated field is observable during Build.
        const auto refuse=[&](TownPointShadowFailure failure,bool budget=false) {
            stats.failure=failure;stats.budgetLimited=budget;stats.ready=false;stats.rejectedSources=sources.size();stats.admittedSources=0;
            ClearStorage();
            stats.scratchBytes=stats.keyBytes=stats.publishedDepthBytes=stats.cachedDepthBytes=stats.retainedBytes=0;
            stats.trace.ready=false;stats.trace.failure=failure;stats.trace.budgetLimited=budget;
            stats.buildMilliseconds=Milliseconds(begin);Stats=stats;return false;
        };
        if (Identity==0) return refuse(TownPointShadowFailure::Capacity,true);
        if (currentEpoch==0) return refuse(TownPointShadowFailure::InvalidInput);
        if (sources.size()>TownGeometricPointMaximumSources
            || fixed.triangles.size()>TownGeometricPointMaximumTriangles
            || moving.triangles.size()>TownGeometricPointMaximumTriangles-fixed.triangles.size()
            || fixed.masks.size()>TownGeometricPointMaximumMasks || moving.masks.size()>TownGeometricPointMaximumMasks-fixed.masks.size())
            return refuse(TownPointShadowFailure::Capacity,true);
        if (!std::all_of(sources.begin(),sources.end(),ValidSource)) return refuse(TownPointShadowFailure::InvalidInput);
        for (const auto *layer:{&fixed,&moving}) {
            const bool present=!layer->triangles.empty() || !layer->masks.empty();
            if ((present && (layer->epoch!=currentEpoch || layer->identity==0 || layer->revision==0))
                || (!present && layer->epoch!=0 && layer->epoch!=currentEpoch)) return refuse(TownPointShadowFailure::InvalidInput);
            for (const auto &mask:layer->masks) {
                if (!ValidMask(mask)) return refuse(TownPointShadowFailure::InvalidInput);
                if (mask.opacity.size()>TownGeometricPointMaximumMaskBytes-stats.maskBytes) return refuse(TownPointShadowFailure::Capacity,true);
                stats.maskBytes+=mask.opacity.size();
                if (mask.codes.size()>TownGeometricPointMaximumMaskBytes-stats.maskBytes) return refuse(TownPointShadowFailure::Capacity,true);
                stats.maskBytes+=mask.codes.size();
            }
        }
        stats.validationMilliseconds=Milliseconds(begin);
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        try {
#endif
            std::array<bool,TownGeometricPointMaximumSources> dirtyFixed {},dirtyMoving {};
            bool changed=!wasReady || Epoch!=currentEpoch || Count!=sources.size();
            for (size_t i=0;i<sources.size();++i)
                dirtyFixed[i]=dirtyMoving[i]=!Lights[i].valid || Epoch!=currentEpoch || !SameSource(Lights[i].source,sources[i]);
            stats.scratchPeakBytes=ScratchBytes();stats.keyPeakBytes=KeyBytes();
            if (!UpdateLayer(Fixed,fixed,sources,dirtyFixed,true,stats) || !UpdateLayer(Moving,moving,sources,dirtyMoving,false,stats))
                return refuse(LastFailure,LastFailure==TownPointShadowFailure::Capacity);
            stats.trace.inputTriangles=stats.staticTriangles+stats.dynamicTriangles;
            stats.trace.acceptedTriangles=Fixed.primitives.size()+Moving.primitives.size();
            stats.trace.bvhNodes=Fixed.nodes.size()+Moving.nodes.size();stats.trace.masks=stats.masks;
            for (size_t i=0;i<sources.size();++i) {
                auto &cache=Lights[i];
                if (dirtyFixed[i]) {
                    const auto start=Clock::now();
                    if (cache.fixedRevision==std::numeric_limits<uint64_t>::max() || !BuildCube(cache.fixed,Fixed,fixed,sources[i],stats)) return refuse(TownPointShadowFailure::Capacity,true);
                    ++cache.fixedRevision;++stats.staticMapBuilds;stats.staticBuildMilliseconds+=Milliseconds(start);changed=true;
                } else ++stats.staticMapCacheHits;
                if (dirtyMoving[i]) {
                    const auto start=Clock::now();
                    if (cache.movingRevision==std::numeric_limits<uint64_t>::max() || !BuildCube(cache.moving,Moving,moving,sources[i],stats)) return refuse(TownPointShadowFailure::Capacity,true);
                    ++cache.movingRevision;++stats.dynamicMapBuilds;stats.dynamicBuildMilliseconds+=Milliseconds(start);changed=true;
                } else ++stats.dynamicMapCacheHits;
                cache.source=sources[i];cache.valid=true;
                stats.staticRelevantRevisions[i]=cache.fixedRevision;stats.dynamicRelevantRevisions[i]=cache.movingRevision;
            }
            for (size_t i=sources.size();i<Lights.size();++i) Lights[i]=LightCache{};
            if (changed) {
                if (Sequence==std::numeric_limits<uint64_t>::max()) return refuse(TownPointShadowFailure::Capacity,true);
                const size_t texels=sources.size()*TownGeometricPointCubeTexels;
                // The previous view was invalidated before any update. Free
                // before growth: vector's geometric growth must neither make
                // 17->32 exceed the cap nor retain old+new published buffers.
                if (Merged.capacity()<texels) std::vector<float>().swap(Merged);
                Merged.resize(texels);
                if (Merged.capacity()*sizeof(float)>TownGeometricPointMaximumPublishedDepthBytes) return refuse(TownPointShadowFailure::Capacity,true);
                for (size_t i=0;i<sources.size();++i) for (size_t p=0;p<TownGeometricPointCubeTexels;++p)
                    Merged[i*TownGeometricPointCubeTexels+p]=std::min(Lights[i].fixed[p],Lights[i].moving[p]);
                ++Sequence;
            }
            Count=sources.size();std::copy(sources.begin(),sources.end(),Sources.begin());Epoch=currentEpoch;
            Ready=true;stats.ready=true;stats.admittedSources=Count;stats.scratchBytes=ScratchBytes();stats.keyBytes=KeyBytes();
            stats.publishedDepthBytes=Merged.capacity()*sizeof(float);
            for (const auto &cache:Lights) stats.cachedDepthBytes+=(cache.fixed.capacity()+cache.moving.capacity())*sizeof(float);
            stats.retainedBytes=stats.scratchBytes+stats.keyBytes+stats.publishedDepthBytes+stats.cachedDepthBytes;
            stats.trace.ready=true;stats.trace.scratchBytes=stats.scratchBytes;stats.trace.depthBytes=stats.publishedDepthBytes;
            stats.buildMilliseconds=Milliseconds(begin);Stats=stats;return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        } catch (const std::bad_alloc &) {return refuse(TownPointShadowFailure::Allocation,true);}
#endif
    }

    TownGeometricPointShadowView GetView() const
    {
        if (!Ready) return {};
        return {std::span<const float>(Merged),std::span<const TownGeometricPointSource>(Sources.data(),Count),TownFireShadowResolution,Epoch,Identity,Sequence};
    }
    const TownGeometricPointShadowStats &GetStats() const {return Stats;}
    void Clear() {ClearStorage();Stats={};}

private:
    using Clock=std::chrono::steady_clock;
    using LayerCache=town_geometric_point_detail::LayerCache;
    using LightCache=town_geometric_point_detail::LightCache;
    static double Milliseconds(Clock::time_point begin) {return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();}
    uint64_t Identity=0,Sequence=0,Epoch=0;
    size_t Count=0;
    bool Ready=false;
    LayerCache Fixed,Moving;
    std::array<LightCache,TownGeometricPointMaximumSources> Lights;
    std::array<TownGeometricPointSource,TownGeometricPointMaximumSources> Sources {};
    std::vector<float> Merged;
    TownGeometricPointShadowStats Stats;
    TownPointShadowFailure LastFailure=TownPointShadowFailure::InvalidInput;
    size_t ScratchBytes() const {return Fixed.ScratchBytes()+Moving.ScratchBytes();}
    size_t KeyBytes() const {return Fixed.KeyBytes()+Moving.KeyBytes();}
    void ClearStorage()
    {
        Ready=false;Epoch=0;Count=0;Fixed=LayerCache{};Moving=LayerCache{};
        for (auto &cache:Lights) cache=LightCache{};
        std::vector<float>().swap(Merged);Sources={};
    }

    bool UpdateLayer(LayerCache &cache,const TownShadowGeometryLayer &input,
        std::span<const TownGeometricPointSource> sources,std::array<bool,TownGeometricPointMaximumSources> &dirty,
        bool fixed,TownGeometricPointShadowStats &stats)
    {
        using namespace town_geometric_point_detail;
        const auto validationBegin=Clock::now();
        const auto reject=[&](TownPointShadowFailure failure){LastFailure=failure;return false;};
        if (cache.epoch==input.epoch && cache.identity==input.identity && cache.revision==input.revision) {
            if (cache.triangles.size()!=input.triangles.size() || cache.masks.size()!=input.masks.size()) return reject(TownPointShadowFailure::InvalidInput);
            ++stats.immutableLayerCacheHits;return true;
        }
        std::array<bool,TownGeometricPointMaximumMasks> maskChanged {};
        bool keyChanged=false;
        for (size_t i=0;i<std::max(cache.masks.size(),input.masks.size());++i)
            keyChanged=(maskChanged[i]=i>=cache.masks.size() || i>=input.masks.size() || !cache.masks[i].Same(input.masks[i])) || keyChanged;
        bool geometryChanged=cache.triangles.size()!=input.triangles.size();
        const auto touch=[&](const TownPointShadowTriangle *before,const TownPointShadowTriangle *after) {
            const auto oldBounds=before?TriangleBounds(*before):Bounds{};
            const auto newBounds=after?TriangleBounds(*after):Bounds{};
            for (size_t light=0;light<sources.size();++light) if (!dirty[light]) {
                ++stats.roiTests;
                if ((before && SphereTouches(oldBounds,sources[light])) || (after && SphereTouches(newBounds,sources[light]))) dirty[light]=true;
            }
        };
        for (const auto &triangle:input.triangles) {
            if ((!input.masks.empty() && triangle.mask>=input.masks.size()) || (input.masks.empty() && triangle.mask!=0)) return reject(TownPointShadowFailure::InvalidInput);
            for (size_t v=0;v<3;++v) if (!Finite(triangle.vertices[v]) || !std::isfinite(triangle.uv[v][0]) || !std::isfinite(triangle.uv[v][1])
                || std::abs(triangle.uv[v][0])>1000000 || std::abs(triangle.uv[v][1])>1000000) return reject(TownPointShadowFailure::InvalidInput);
        }
        for (size_t i=0;i<std::max(cache.triangles.size(),input.triangles.size());++i) {
            const auto *before=i<cache.triangles.size()?&cache.triangles[i]:nullptr;
            const auto *after=i<input.triangles.size()?&input.triangles[i]:nullptr;
            ++stats.exactComparedTriangles;
            if (!before || !after || !SameTriangle(*before,*after)
                || (!cache.masks.empty() && maskChanged[before->mask]) || (!input.masks.empty() && maskChanged[after->mask])) {
                geometryChanged=true;touch(before,after);
            }
        }
        stats.validationMilliseconds+=Milliseconds(validationBegin);
        keyChanged=keyChanged || geometryChanged;
        LayerCache next;
        if (keyChanged) {
            next.triangles.assign(input.triangles.begin(),input.triangles.end());next.masks.reserve(input.masks.size());
            for (const auto &mask:input.masks) next.masks.push_back({mask.width,mask.height,mask.repeat,mask.transparentZero,
                {mask.opacity.begin(),mask.opacity.end()},{mask.codes.begin(),mask.codes.end()}});
            const size_t peak=KeyBytes()+next.KeyBytes();stats.keyPeakBytes=std::max(stats.keyPeakBytes,peak);
            stats.keyStagingPeakBytes=std::max(stats.keyStagingPeakBytes,next.KeyBytes());
            if (next.KeyBytes()>TownGeometricPointMaximumKeyStagingBytes || peak>TownGeometricPointMaximumKeyPeakBytes
                || KeyBytes()-cache.KeyBytes()+next.KeyBytes()>TownGeometricPointMaximumKeyBytes)
                return reject(TownPointShadowFailure::Capacity);
        }
        if (geometryChanged) {
            const auto bvhBegin=Clock::now();
            std::vector<Primitive>().swap(cache.primitives);std::vector<Node>().swap(cache.nodes);
            const size_t prospective=ScratchBytes()+input.triangles.size()*sizeof(Primitive)+NodeBound(input.triangles.size())*sizeof(Node);
            if (prospective>TownGeometricPointMaximumScratchBytes) return reject(TownPointShadowFailure::Capacity);
            cache.primitives.reserve(input.triangles.size());cache.nodes.reserve(NodeBound(input.triangles.size()));
            if (ScratchBytes()>TownGeometricPointMaximumScratchBytes) return reject(TownPointShadowFailure::Capacity);
            stats.scratchPeakBytes=std::max(stats.scratchPeakBytes,ScratchBytes());
            for (size_t i=0;i<input.triangles.size();++i) {
                const auto &triangle=input.triangles[i];
                const auto cross=Cross(Subtract(triangle.vertices[1],triangle.vertices[0]),Subtract(triangle.vertices[2],triangle.vertices[0]));
                if (Dot(cross,cross)<=1.0e-18F) continue;
                Primitive primitive;primitive.triangle=static_cast<uint32_t>(i);primitive.bounds=TriangleBounds(triangle);
                primitive.center=Scale(Add(primitive.bounds.minimum,primitive.bounds.maximum),.5F);cache.primitives.push_back(primitive);
            }
            if (!cache.primitives.empty()) BuildNode(cache.nodes,cache.primitives,0,static_cast<uint32_t>(cache.primitives.size()));
            if (cache.nodes.size()>NodeBound(input.triangles.size())) return reject(TownPointShadowFailure::Capacity);
            if (fixed) ++stats.staticBvhBuilds;else ++stats.dynamicBvhBuilds;
            if (fixed) stats.staticBvhBuildMilliseconds+=Milliseconds(bvhBegin);else stats.dynamicBvhBuildMilliseconds+=Milliseconds(bvhBegin);
        }
        if (keyChanged) {cache.triangles=std::move(next.triangles);cache.masks=std::move(next.masks);}
        cache.epoch=input.epoch;cache.identity=input.identity;cache.revision=input.revision;return true;
    }

    static bool BuildCube(std::vector<float> &depth,const LayerCache &cache,const TownShadowGeometryLayer &input,
        const TownGeometricPointSource &source,TownGeometricPointShadowStats &stats)
    {
        using namespace town_point_shadow_detail;
        depth.assign(TownGeometricPointCubeTexels,std::numeric_limits<float>::infinity());
        if (depth.capacity()>TownGeometricPointCubeTexels) return false;
        if (cache.nodes.empty()) return true;
        for (unsigned face=0;face<6;++face) for (int y=0;y<TownFireShadowResolution;++y) for (int x=0;x<TownFireShadowResolution;++x) {
            const auto direction=FaceDirection(face,2*(x+.5F)/TownFireShadowResolution-1,2*(y+.5F)/TownFireShadowResolution-1);
            const size_t at=(face*TownFireShadowResolution+y)*TownFireShadowResolution+x;
            ++stats.trace.rays;
            if (!Trace(cache.nodes,cache.primitives,input.triangles,input.masks,source.position,direction,source.radius,stats.trace,depth[at])) return false;
        }
        return true;
    }
};

/** Same major-axis/tie/clamp/PCF/bias as C1, with a typed32/radius256 view.
 * This is only visibility. The caller retains its existing energy, LUT,
 * attenuation, normal diffuse and deterministic flicker arithmetic. */
inline float SampleTownGeometricPointVisibility(const TownGeometricPointShadowView &field,size_t lightIndex,
    TownLightVector position,TownLightVector normal)
{
    using namespace town_point_shadow_detail;
    using town_geometric_point_detail::ValidSource;
    if (field.identity==0 || field.revision==0 || field.epoch==0 || field.resolution!=TownFireShadowResolution
        || field.sources.size()>TownGeometricPointMaximumSources || lightIndex>=field.sources.size()
        || field.depth.size()!=field.sources.size()*TownGeometricPointCubeTexels || !Finite(position) || !Finite(normal)) return 0;
    const auto &source=field.sources[lightIndex];if (!ValidSource(source)) return 0;
    const auto delta=Subtract(position,source.position);const float squared=Dot(delta,delta);
    if (squared>=source.radius*source.radius || squared<=TownFireShadowNear*TownFireShadowNear) return 1;
    const auto sample=SelectFace(delta);
    const int centerX=static_cast<int>(std::floor((sample.u+1)*.5F*TownFireShadowResolution));
    const int centerY=static_cast<int>(std::floor((sample.v+1)*.5F*TownFireShadowResolution));
    const float diffuse=Normalize(normal)?std::max(0.0F,Dot(normal,Scale(delta,-1/std::sqrt(squared)))):0;
    const float bias=TownFireShadowDepthBias+TownFireShadowSlopeBias*(1-diffuse);
    float lit=0;
    for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
        const int px=std::clamp(centerX+x,0,TownFireShadowResolution-1),py=std::clamp(centerY+y,0,TownFireShadowResolution-1);
        const float depth=field.depth[((lightIndex*6+sample.face)*TownFireShadowResolution+py)*TownFireShadowResolution+px];
        if (std::isnan(depth) || depth<=0) return 0;
        if (sample.axial<=depth+bias) lit+=1;
    }
    return lit/9;
}

} // namespace devilution
