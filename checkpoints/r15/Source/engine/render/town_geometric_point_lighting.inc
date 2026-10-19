// Included once by town_view.cpp beside frame-local point-light descriptors.
// The same owned static/dynamic geometry feeds Sun and this independent field.
TownGeometricPointShadowCache TownPointGeometryCache;
TownGeometricPointShadowView FrameTownPointGeometryView;
bool TownPointGeometryEnabledForDiagnostics=true;
bool FailTownPointGeometryBuildForDiagnostics=false;
bool FrameTownPointGeometryReady=false,FrameTownPointSourcesValid=false;
bool DrawingTownPointBillboard=false;
std::array<TownPointLight,TownMaxPointLights> FrameTownPointLights;
std::array<TownGeometricPointSource,TownMaxPointLights> FrameTownPointSources;
size_t FrameTownPointLightCount=0,FrameTownPointRoomCount=0;
struct TownPointRoomBinding {const TownCabinInterior *room=nullptr;size_t offset=0,count=0;};
std::array<TownPointRoomBinding,TownMaxPointLights> FrameTownPointRooms;
TownViewTownPointGeometryState TownPointGeometryState;

uint32_t TintTownPointGeometryDiffuseArgb(uint32_t baseArgb,uint32_t albedoArgb,TownLightColor irradiance)
{
    using namespace town_point_shadow_detail;
    // Town32 has its own bound; C1's four-source tint remains unchanged.
    const auto bounded=[](float value){return std::isfinite(value) && value>=0 && value<=TownMaxPointLights*16*TownMaxPointLightIntensity;};
    if(!bounded(irradiance.red) || !bounded(irradiance.green) || !bounded(irradiance.blue)
        || (irradiance.red==0 && irradiance.green==0 && irradiance.blue==0))return baseArgb;
    const auto channel=[&](unsigned shift,float light) {
        const float base=SrgbToLinear(static_cast<float>((baseArgb>>shift)&255)/255);
        const float albedo=SrgbToLinear(static_cast<float>((albedoArgb>>shift)&255)/255);
        return static_cast<uint32_t>(std::clamp(LinearToSrgb(base+albedo*light)*255+.5F,0.0F,255.0F))<<shift;
    };
    return (baseArgb&0xFF000000U)|channel(16,irradiance.red)|channel(8,irradiance.green)|channel(0,irradiance.blue);
}

struct TownPointBillboardScope {
    bool previous=DrawingTownPointBillboard;
    TownPointBillboardScope(){DrawingTownPointBillboard=true;}
    ~TownPointBillboardScope(){DrawingTownPointBillboard=previous;}
};

void ClearFrameTownPointMetadata()
{
    FrameTownPointGeometryReady=false;FrameTownPointSourcesValid=false;FrameTownPointGeometryView={};
    FrameTownPointLightCount=FrameTownPointRoomCount=0;FrameTownPointRooms={};
    TownPointGeometryState={};TownPointGeometryState.enabled=TownPointGeometryEnabledForDiagnostics;
}

void ResetTownPointGeometryLighting()
{
    TownPointGeometryCache.Clear();ClearFrameTownPointMetadata();
    DrawingTownPointBillboard=false;FailTownPointGeometryBuildForDiagnostics=false;
}

void AppendFrameTownPointRoom(const TownCabinInterior &room)
{
    if(!TownPointGeometryEnabledForDiagnostics || room.fireSources.empty())return;
    auto &state=TownPointGeometryState;
    const size_t before=state.eligibleSources;
    state.eligibleSources+=room.fireSources.size();++state.rooms;
    if(room.fireSources.size()>TownMaxPointLights || before>TownMaxPointLights
        || room.fireSources.size()>TownMaxPointLights-before || FrameTownPointRoomCount==FrameTownPointRooms.size()) {
        state.budgetLimited=true;state.failed=true;return;
    }
    const size_t offset=FrameTownPointLightCount;
    FrameTownPointRooms[FrameTownPointRoomCount++]={&room,offset,room.fireSources.size()};
    for(const auto &source:room.fireSources) {
        // This frozen energy is also reused by interior and Church consumers.
        const TownPointLight light=source.kind==TownCabinFireKind::EditorPoint?source.light
            :TownFireLightAtTime(source.light,source.flickerSeed,FrameFireTime,FireIntensityVariation);
        const TownGeometricPointSource geometry{light.position,light.radius};
        if(!town_geometric_point_detail::ValidSource(geometry)
            || !ValidateTownSpritePointLighting({&light,1},{})) {++state.invalidSources;state.failed=true;}
        FrameTownPointLights[FrameTownPointLightCount]=light;
        FrameTownPointSources[FrameTownPointLightCount++]=geometry;
    }
}

void FinishFrameTownPointMetadata()
{
    auto &state=TownPointGeometryState;
    if(!TownPointGeometryEnabledForDiagnostics)return;
    FrameTownPointSourcesValid=!state.failed && FrameTownPointLightCount==state.eligibleSources;
    if(!FrameTownPointSourcesValid) {
        state.failed=state.fallback=true;state.rejectedSources=state.eligibleSources;
        TownPointGeometryCache.Clear();
    }
}

size_t FrameTownPointRoomOffset(const TownCabinInterior *room)
{
    if(TownPointGeometryEnabledForDiagnostics && !TownPointGeometryState.failed)for(size_t i=0;i<FrameTownPointRoomCount;++i)
        if(FrameTownPointRooms[i].room==room)return FrameTownPointRooms[i].offset;
    return TownMaxPointLights;
}

bool FrozenTownPointRoomLights(const TownCabinInterior &room,InteriorLighting &lighting)
{
    const size_t offset=FrameTownPointRoomOffset(&room);
    if(offset==TownMaxPointLights || room.fireSources.size()>FrameTownPointLightCount-offset)return false;
    lighting.lightCount=room.fireSources.size();lighting.globalPointOffset=offset;
    std::copy_n(FrameTownPointLights.begin()+offset,lighting.lightCount,lighting.lights.begin());return true;
}

void RefuseTownPointGeometryLighting(bool budget=false)
{
    if(!TownPointGeometryEnabledForDiagnostics)return;
    FrameTownPointGeometryReady=false;FrameTownPointGeometryView={};TownPointGeometryCache.Clear();
    auto &state=TownPointGeometryState;state.ready=false;state.failed=state.fallback=true;
    state.budgetLimited|=budget;state.admittedSources=0;state.rejectedSources=state.eligibleSources;
}

bool PrepareTownPointGeometryLighting(const TownShadowGeometryLayer &fixed,const TownShadowGeometryLayer &moving,uint64_t epoch)
{
    if(!TownPointGeometryEnabledForDiagnostics)return false;
    auto &state=TownPointGeometryState;state.epoch=epoch;
    if(!FrameTownPointSourcesValid || FailTownPointGeometryBuildForDiagnostics) {
        FailTownPointGeometryBuildForDiagnostics=false;RefuseTownPointGeometryLighting();return false;
    }
    if(!TownPointGeometryCache.Build(fixed,moving,{FrameTownPointSources.data(),FrameTownPointLightCount},epoch)) {
        const auto failure=TownPointGeometryCache.GetStats();
        RefuseTownPointGeometryLighting(failure.budgetLimited);state.failure=static_cast<uint8_t>(failure.failure);return false;
    }
    FrameTownPointGeometryView=TownPointGeometryCache.GetView();
    const auto &stats=TownPointGeometryCache.GetStats();
    if(!stats.ready || FrameTownPointGeometryView.epoch!=epoch
        || FrameTownPointGeometryView.sources.size()!=FrameTownPointLightCount
        || FrameTownPointGeometryView.depth.size()!=FrameTownPointLightCount*TownGeometricPointCubeTexels
        || !std::equal(FrameTownPointGeometryView.sources.begin(),FrameTownPointGeometryView.sources.end(),
            FrameTownPointSources.begin(),town_geometric_point_detail::SameSource)) {
        RefuseTownPointGeometryLighting();return false;
    }
    FrameTownPointGeometryReady=stats.ready;
    state.ready=stats.ready;state.failed=state.fallback=false;
    state.identity=FrameTownPointGeometryView.identity;state.revision=FrameTownPointGeometryView.revision;
    state.admittedSources=stats.admittedSources;state.rejectedSources=stats.rejectedSources;
    state.staticTriangles=stats.staticTriangles;state.dynamicTriangles=stats.dynamicTriangles;
    state.masks=stats.masks;state.maskBytes=stats.maskBytes;
    state.staticMapBuilds=stats.staticMapBuilds;state.dynamicMapBuilds=stats.dynamicMapBuilds;
    state.staticMapCacheHits=stats.staticMapCacheHits;state.dynamicMapCacheHits=stats.dynamicMapCacheHits;
    state.staticBvhBuilds=stats.staticBvhBuilds;state.dynamicBvhBuilds=stats.dynamicBvhBuilds;
    state.roiTests=stats.roiTests;state.rays=stats.trace.rays;state.triangleTests=stats.trace.triangleTests;
    state.scratchBytes=stats.scratchBytes;state.keyBytes=stats.keyBytes;state.retainedBytes=stats.retainedBytes;
    state.publishedDepthBytes=stats.publishedDepthBytes;state.cachedDepthBytes=stats.cachedDepthBytes;
    state.buildMilliseconds=stats.buildMilliseconds;return state.ready;
}

float SampleFrameTownPointGeometry(const void *,size_t index,TownLightVector position,TownLightVector normal)
{
    return FrameTownPointGeometryReady?SampleTownGeometricPointVisibility(FrameTownPointGeometryView,index,position,normal):0;
}
std::span<const TownPointLight> InteriorTownPointLights(const InteriorLighting &interior)
{
    return FrameTownPointGeometryReady?std::span<const TownPointLight>{FrameTownPointLights.data(),FrameTownPointLightCount}
        :std::span<const TownPointLight>{interior.lights.data(),interior.lightCount};
}
TownPointGeometryVisibility InteriorTownPointGeometry()
{
    // A refused whole field intentionally retains the exact approved shell path.
    return FrameTownPointGeometryReady
        ?TownPointGeometryVisibility{&FrameTownPointGeometryView,SampleFrameTownPointGeometry}:TownPointGeometryVisibility{};
}
