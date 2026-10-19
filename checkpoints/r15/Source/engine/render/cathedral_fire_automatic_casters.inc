// Included once by town_view.cpp after all actual geometry consumers.
// DrawTriangle is the common collection boundary, before clipping/depth/pick.
void ReleaseCathedralFireCasterSnapshot()
{
    std::vector<TownPointShadowTriangle>().swap(CathedralFireCasterTriangles);
    std::vector<TownPointShadowMask>().swap(CathedralFireCasterMasks);
    std::vector<OwnedCathedralFireMask>().swap(CathedralFireOwnedMasks);
    CathedralFireMaskIds.clear();CathedralFireOwnedMaskBytes=0;
}

bool CathedralFireDynamicKeyMatches(std::span<const uint8_t> cached,
    std::span<const TownPointShadowTriangle> triangles,
    std::span<const TownPointShadowMask> masks,
    std::span<const uint64_t> staticKey)
{
	size_t cursor = 0;
	const auto equal = [&](const void *data, size_t bytes) {
		if (cursor > cached.size() || bytes > cached.size() - cursor
		    || cursor > MaxCathedralFireDynamicKeyBytes || bytes > MaxCathedralFireDynamicKeyBytes - cursor)
			return false;
		if (bytes != 0 && std::memcmp(cached.data() + cursor, data, bytes) != 0)
			return false;
		cursor += bytes;
		return true;
	};
	const auto word = [&](uint64_t value) { return equal(&value, sizeof value); };
	if (!word(staticKey.size()) || !equal(staticKey.data(), staticKey.size_bytes())
	    || !word(triangles.size()) || !word(masks.size()))
		return false;
	for (const auto &triangle : triangles) {
		if (!word(triangle.mask)) return false;
		for (size_t i = 0; i < 3; ++i) {
			const std::array<float, 5> vertex { triangle.vertices[i].x, triangle.vertices[i].height,
				triangle.vertices[i].z, triangle.uv[i][0], triangle.uv[i][1] };
			if (!equal(vertex.data(), sizeof vertex)) return false;
		}
	}
	for (const auto &mask : masks) {
		if (!word(mask.width) || !word(mask.height) || !word(mask.repeat) || !word(mask.transparentZero)
		    || !word(mask.opacity.size()) || !word(mask.codes.size())
		    || (!mask.opacity.empty() && !equal(mask.opacity.data(), mask.opacity.size()))
		    || (!mask.codes.empty() && !equal(mask.codes.data(), mask.codes.size())))
			return false;
	}
	return cursor == cached.size();
}

bool CathedralFireDynamicContextMatches(std::span<const uint8_t> cached, std::span<const uint64_t> staticKey)
{
	if (cached.size() < sizeof(uint64_t) || cached.size() > MaxCathedralFireDynamicKeyBytes)
		return false;
	uint64_t count = 0;
	std::memcpy(&count, cached.data(), sizeof(count));
	if (count != staticKey.size() || count > MaxCathedralFireDynamicKeyBytes / sizeof(uint64_t))
		return false;
	const size_t bytes = static_cast<size_t>(count) * sizeof(uint64_t);
	return bytes <= cached.size() - sizeof(uint64_t)
	    && (bytes == 0 || std::memcmp(cached.data() + sizeof(uint64_t), staticKey.data(), bytes) == 0);
}

bool CaptureCathedralFireDynamicKey(std::vector<uint8_t> &key,
    std::span<const TownPointShadowTriangle> triangles,
    std::span<const TownPointShadowMask> masks,
    std::span<const uint64_t> staticKey)
{
    // Exact field serialization; no pointer, padding or hash-only identity.
    const auto append=[&](const void *data,size_t bytes) {
        if(bytes>MaxCathedralFireDynamicKeyBytes-key.size())return false;
        const auto *begin=static_cast<const uint8_t *>(data);
        key.insert(key.end(),begin,begin+bytes);return true;
    };
    const auto word=[&](uint64_t value) {return append(&value,sizeof value);};
	if(!word(staticKey.size()) || !append(staticKey.data(),staticKey.size_bytes())
	    || !word(triangles.size()) || !word(masks.size()))return false;
	for(const auto &triangle:triangles) {
        if(!word(triangle.mask))return false;
        for(size_t i=0;i<3;++i) {
            const std::array<float,5> vertex{triangle.vertices[i].x,triangle.vertices[i].height,triangle.vertices[i].z,triangle.uv[i][0],triangle.uv[i][1]};
            if(!append(vertex.data(),sizeof vertex))return false;
        }
    }
	for(const auto &mask:masks) {
        if(!word(mask.width) || !word(mask.height) || !word(mask.repeat) || !word(mask.transparentZero)
            || !word(mask.opacity.size()) || !word(mask.codes.size())
            || (!mask.opacity.empty() && !append(mask.opacity.data(),mask.opacity.size()))
            || (!mask.codes.empty() && !append(mask.codes.data(),mask.codes.size())))return false;
    }
    return true;
}

bool CaptureCathedralFireDynamicKey(std::vector<uint8_t> &key,
    std::span<const uint64_t> staticKey)
{
	return CaptureCathedralFireDynamicKey(key,
	    std::span<const TownPointShadowTriangle>(CathedralFireCasterTriangles),
	    std::span<const TownPointShadowMask>(CathedralFireCasterMasks),staticKey);
}

void FreezeCathedralFirePropPoses()
{
    FrameCathedralChestPosesFrozen=false;
    for(int i=0;i<ActiveObjectCount;++i) {
        const int slot=ActiveObjects[i];
        if(slot<0 || slot>=MAXOBJECTS)continue;
        const auto &object=Objects[slot];
        if(!CathedralTileInFrame(object.position))continue;
        if(object._otype==OBJ_CHEST1 || object._otype==OBJ_CHEST2 || object._otype==OBJ_CHEST3
            || object._otype==OBJ_TCHEST1 || object._otype==OBJ_TCHEST2 || object._otype==OBJ_TCHEST3)
            FrameCathedralChestProgress[slot]=ChestVisualProgress(object,slot);
    }
    FrameCathedralChestPosesFrozen=true;
}

struct OwnedTownShadowGeometry {
    std::vector<TownPointShadowTriangle> triangles;
    std::vector<TownPointShadowMask> masks;
    std::vector<OwnedCathedralFireMask> owned;
    size_t maskBytes=0;
};
OwnedTownShadowGeometry TownShadowStaticCapture;
std::vector<uint64_t> TownShadowStaticAdmissionKey;

OwnedTownShadowGeometry TakeTownShadowGeometrySnapshot()
{
    OwnedTownShadowGeometry capture;
    capture.triangles.swap(CathedralFireCasterTriangles);
    capture.masks.swap(CathedralFireCasterMasks);
    capture.owned.swap(CathedralFireOwnedMasks);
    capture.maskBytes=CathedralFireOwnedMaskBytes;
    CathedralFireOwnedMaskBytes=0;CathedralFireMaskIds.clear();
    return capture;
}

template<class DrawWorld>
bool PrepareAutomaticTownShadowGeometry(const DrawWorld &drawWorld,const TownShadowConfig &config)
{
    AutomaticTownShadowState={};AutomaticTownShadowState.enabled=AutomaticFireCastersEnabledForDiagnostics;
    struct PrepareTimer {
        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
        ~PrepareTimer(){AutomaticTownShadowState.prepareMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();TownPointGeometryState.prepareMilliseconds=TownPointGeometryState.sourcePrepareMilliseconds+AutomaticTownShadowState.prepareMilliseconds;}
    } timer;
    if(!AutomaticFireCastersEnabledForDiagnostics && !TownPointGeometryEnabledForDiagnostics)return BuildTownShadowMap(GetTownScene(),config);
    struct CollectionScope {
        bool capture=CaptureGpu,oldDrawing=DrawingCathedral;
        decltype(ArchitectureCullingState) architecture=ArchitectureCullingState;
        decltype(WarriorPilotState) warrior=WarriorPilotState;
        decltype(OgdenPilotState) ogden=OgdenPilotState;
        decltype(GriswoldPilotState) griswold=GriswoldPilotState;
        decltype(FixedTownObjectState) fixed=FixedTownObjectState;
        double record=RendererState.sceneRecordMilliseconds;
        ~CollectionScope() {
            CaptureGpu=capture;DrawingCathedral=oldDrawing;
            CollectingTownDirectionalCasters=CollectingCathedralFireCasters=false;
            TownShadowDynamicConsumer=TownShadowCollectDynamicOnly=false;CollectingCathedralDynamicCasters=false;
            TownShadowReservedTriangles=TownShadowReservedMasks=TownShadowReservedMaskBytes=0;
            DrawingCathedralFireEmission=false;
            ArchitectureCullingState=architecture;WarriorPilotState=std::move(warrior);OgdenPilotState=ogden;GriswoldPilotState=griswold;FixedTownObjectState=std::move(fixed);
            RendererState.sceneRecordMilliseconds=record;ReleaseCathedralFireCasterSnapshot();
        }
    };
    const auto refuse=[&](bool budget) {
        ClearTownShadowMap();RefuseTownPointGeometryLighting(budget);AutomaticTownShadowState.failed=true;
        AutomaticTownShadowState.budgetLimited=budget;return false;
    };
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
#endif
        CollectionScope scope;
        const uint64_t epoch=NativePlayerActorLifecycle().epoch();
        if(AutomaticFireCastersEnabledForDiagnostics)AutomaticTownShadowState.epoch=epoch;
        if(epoch==0)return refuse(false);
        std::vector<uint64_t> admission{epoch,GetTownSceneRevision(),uint64_t(*GetOptions().Graphics.townViewHorizon),HorizonTriangleCount()};
        admission.push_back(HorizonSurroundingsIdentity.size());
        for(const unsigned char value:HorizonSurroundingsIdentity)admission.push_back(value);
        // The native terrain/event owners, not RGB brightness, own this cache.
        for(int z=0;z<MAXDUNY;++z)for(int x=0;x<MAXDUNX;++x)admission.push_back(dPiece[x][z]);
        const auto &scene=GetTownScene();
        for(size_t i=0;i<scene.size();++i)if(scene[i].kind==TownSceneKind::Well)
            admission.push_back(TownVisualSceneWaterPoisoned(i));
        CaptureGpu=false;CollectingTownDirectionalCasters=CollectingCathedralFireCasters=true;CollectingCathedralDynamicCasters=false;
        TownShadowReservedTriangles=TownShadowReservedMasks=TownShadowReservedMaskBytes=0;
        ReleaseCathedralFireCasterSnapshot();
        if(admission!=TownShadowStaticAdmissionKey || TownShadowStaticGeometryRevision==0) {
            TownShadowStaticCapture={};TownShadowStaticAdmissionKey.clear();
            TownShadowCollectDynamicOnly=false;TownShadowDynamicConsumer=false;CathedralFireCollectionFailed=false;
            drawWorld();
            if(CathedralFireCollectionFailed)return refuse(true);
            TownShadowStaticCapture=TakeTownShadowGeometrySnapshot();
            TownShadowStaticAdmissionKey=admission;++TownShadowStaticGeometryRevision;
        } else AutomaticTownShadowState.staticGatherCacheHit=true;
        TownShadowReservedTriangles=TownShadowStaticCapture.triangles.size();TownShadowReservedMasks=TownShadowStaticCapture.masks.size();
        TownShadowReservedMaskBytes=TownShadowStaticCapture.maskBytes;
        TownShadowCollectDynamicOnly=true;TownShadowDynamicConsumer=true;CathedralFireCollectionFailed=false;
        drawWorld();
        if(CathedralFireCollectionFailed)return refuse(true);
        std::vector<uint8_t> dynamicKey;
        if(!CaptureCathedralFireDynamicKey(dynamicKey,admission))return refuse(true);
        if(dynamicKey!=TownShadowDynamicGeometryKey || TownShadowDynamicGeometryRevision==0) {
            TownShadowDynamicGeometryKey=std::move(dynamicKey);++TownShadowDynamicGeometryRevision;
        }
        auto dynamic=TakeTownShadowGeometrySnapshot();
        AutomaticTownShadowState.staticTriangles=TownShadowStaticCapture.triangles.size();
        AutomaticTownShadowState.dynamicTriangles=dynamic.triangles.size();
        AutomaticTownShadowState.masks=TownShadowStaticCapture.masks.size()+dynamic.masks.size();
        AutomaticTownShadowState.ownedMaskBytes=TownShadowStaticCapture.maskBytes+dynamic.maskBytes;
        AutomaticTownShadowState.staticGeometryRevision=TownShadowStaticGeometryRevision;
        AutomaticTownShadowState.dynamicGeometryRevision=TownShadowDynamicGeometryRevision;
        const TownShadowGeometryLayer fixedLayer{TownShadowStaticCapture.triangles,TownShadowStaticCapture.masks,epoch,0x701A571CULL,TownShadowStaticGeometryRevision};
        const TownShadowGeometryLayer movingLayer{dynamic.triangles,dynamic.masks,epoch,0x701AD1A0ULL,TownShadowDynamicGeometryRevision};
        // Direct synchronous consumer: BOTH owned snapshots are still alive.
        PrepareTownPointGeometryLighting(fixedLayer,movingLayer,epoch);
        TownPointGeometryState.staticGatherCacheHit=AutomaticTownShadowState.staticGatherCacheHit;
        const bool sunReady=AutomaticFireCastersEnabledForDiagnostics?BuildTownShadowGeometryMap(fixedLayer,movingLayer,config):BuildTownShadowMap(GetTownScene(),config);
        if(!sunReady) {ClearTownShadowMap();AutomaticTownShadowState.failed=true;AutomaticTownShadowState.budgetLimited=GetTownShadowGeometryStats().budgetLimited;return false;}
        AutomaticTownShadowState.ready=AutomaticFireCastersEnabledForDiagnostics;return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    } catch(const std::bad_alloc &) {return refuse(true);}
      catch(const std::exception &) {return refuse(false);}
#endif
}

void ResetAutomaticTownShadowGeometry()
{
    ResetTownPointGeometryLighting();
    TownShadowStaticCapture={};std::vector<uint64_t>().swap(TownShadowStaticAdmissionKey);
    std::vector<uint8_t>().swap(TownShadowDynamicGeometryKey);
}
