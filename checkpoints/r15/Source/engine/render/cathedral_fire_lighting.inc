// Included once by town_view.cpp after the structural draw traversal. It uses
// that exact traversal before clipping, never SOL boxes or old collision art.
void PrepareCathedralFireLighting(const Surface &out)
{
	ClearFrameCathedralFire();
	CathedralFireState.enabled=CathedralFireLightingEnabledForDiagnostics && !SmoothWorldPixels.empty();
	if(!CathedralFireState.enabled || !CathedralFrameReady || MyPlayer==nullptr) {
		ResetCathedralFireDynamicShadowCache();
		return;
	}
	const auto start=std::chrono::steady_clock::now();
    struct PrepareTimer {
        std::chrono::steady_clock::time_point start;
        ~PrepareTimer(){CathedralFireState.prepareMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
    } timer{start};
	const auto finish=[&]() { CathedralFireState.prepareMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); };
	struct CollectionScope {
		bool oldCapture=CaptureGpu, oldDrawing=DrawingCathedral;
		const uint8_t *oldTranslation=CathedralTranslation;
		size_t floor=CathedralArt.floorDraws,wall=CathedralArt.wallDraws,thin=CathedralArt.thinWallDraws;
		size_t models=CathedralArt.modelDraws,objectFloor=CathedralArt.objectFloorCells,returns=CathedralArt.sharedReturnsSkipped;
		std::array<size_t,9> parts=CathedralArt.partDraws;
		size_t leaves=DoorModels.leafDraws,arches=DoorModels.archDraws;
        size_t chest=Chest.draws,sarc=Sarcophagus.draws,brazier=Brazier.meshDraws,brazierFlames=Brazier.flameDraws;
        size_t skull=SkullFire.meshDraws,skullFlames=SkullFire.flameDraws,corpses=CorpseGroundDraws;
        decltype(WarriorPilotState) warrior=WarriorPilotState;
        std::array<size_t,5> barrel{Barrel.meshDraws,Barrel.nativeBreakDraws,Barrel.nativeExplosionDraws,Barrel.shardDraws,Barrel.terminalShardDraws};
		~CollectionScope() {
			CaptureGpu=oldCapture;DrawingCathedral=oldDrawing;CathedralTranslation=oldTranslation;
			DrawingCathedralStructure=false;CollectingCathedralFireCasters=false;CollectingCathedralDynamicCasters=false;DrawingCathedralFireEmission=false;
			CathedralArt.floorDraws=floor;CathedralArt.wallDraws=wall;CathedralArt.thinWallDraws=thin;
			CathedralArt.modelDraws=models;CathedralArt.objectFloorCells=objectFloor;CathedralArt.sharedReturnsSkipped=returns;CathedralArt.partDraws=parts;
			DoorModels.leafDraws=leaves;DoorModels.archDraws=arches;
			Chest.draws=chest;Sarcophagus.draws=sarc;Brazier.meshDraws=brazier;Brazier.flameDraws=brazierFlames;
            SkullFire.meshDraws=skull;SkullFire.flameDraws=skullFlames;CorpseGroundDraws=corpses;
            WarriorPilotState=std::move(warrior);
            Barrel.meshDraws=barrel[0];Barrel.nativeBreakDraws=barrel[1];Barrel.nativeExplosionDraws=barrel[2];Barrel.shardDraws=barrel[3];Barrel.terminalShardDraws=barrel[4];
            ReleaseCathedralFireCasterSnapshot();
		}
	};
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
#endif
	CollectionScope scope;
		const auto *snapshot=cathedral::LiveSnapshot();
		if(snapshot==nullptr || snapshot->gameRevision!=cathedral::LiveEpoch()) {
			ResetCathedralFireDynamicShadowCache();finish();return;
		}
		CathedralFireState.epoch=snapshot->gameRevision;
		CathedralFireState.geometryRevision=CathedralFrame.geometryRevision;
		struct Source { TownPointLight light; uint32_t identity; float distance; int slot; };
		std::vector<Source> sources;
		const Point focus=MyPlayer->position.tile;
		for(const auto &metadata:CaptureCathedralNativeLightObjects(*snapshot)) {
			const Point tile{metadata.originX,metadata.originZ};
			const auto &object=Objects[metadata.slot];
			if(!CathedralNativeLightBodyAdmitted(metadata.slot) || !CathedralTileInFrame(tile)
			    || (snapshot->At(tile.x,tile.y).flags&2)==0 || dLight[tile.x][tile.y]>=LightsMax
			    || !object._oAnimData || object._oAnimFrame==0 || object._oAnimFrame>object._oAnimData->numSprites()) continue;
			const float dx=float(tile.x-focus.x),dz=float(tile.y-focus.y),distance=dx*dx+dz*dz;
			if(distance>64)continue;
			const float height=Brazier.loaded?BrazierFlameContact:CathedralNativeLightBody?CathedralNativeLightBody->mesh.height:0;
			if(height<=0)continue;
			TownPointLight light;light.position={float(tile.x),height,float(tile.y)};
			light.radius=float(metadata.emitterRuleRadius);light.color={1.0F,.44F,.08F};light.intensity=3.0F;
			sources.push_back({light,0xB2A10000U^(uint32_t(tile.x)<<8)^uint32_t(tile.y),distance,metadata.slot});
		}
		// The second already-rendered native fire family uses the same radius5
		// rule in AddObjectLight. Match its visual flame anchor byte for byte;
		// no arbitrary emitter is inferred from a bright palette or SOL cell.
		if(SkullFire.loaded)for(int i=0;i<ActiveObjectCount;++i) {
			const int slot=ActiveObjects[i];const auto &object=Objects[slot];const Point tile=object.position;
			if(object._otype!=OBJ_SKFIRE || object._oDelFlag || !CathedralTileInFrame(tile)
			    || tile.x<snapshot->minX || tile.x>=snapshot->maxX || tile.y<snapshot->minZ || tile.y>=snapshot->maxZ)continue;
			const auto &cell=snapshot->At(tile.x,tile.y);
			if((cell.flags&2)==0 || dLight[tile.x][tile.y]>=LightsMax || std::abs(int(cell.object))!=slot+1
			    || dObject[tile.x][tile.y]!=cell.object || dPiece[tile.x][tile.y]!=cell.piece
			    || !object._oAnimData || object._oAnimFrame==0 || object._oAnimFrame>object._oAnimData->numSprites())continue;
			const float dx=float(tile.x-focus.x),dz=float(tile.y-focus.y),distance=dx*dx+dz*dz;
			if(distance>64)continue;
			TownPointLight light;light.position={float(tile.x)-.004782099F,SkullFireFlameHeight,float(tile.y)-.005263244F};
			light.radius=5;light.color={1.0F,.44F,.08F};light.intensity=3;
			sources.push_back({light,0x5CF10000U^(uint32_t(tile.x)<<8)^uint32_t(tile.y),distance,slot});
		}
		std::sort(sources.begin(),sources.end(),[](const Source &a,const Source &b) { return std::tie(a.distance,a.slot)<std::tie(b.distance,b.slot); });
		if(sources.size()>TownMaxFireShadowLights)sources.resize(TownMaxFireShadowLights);
		std::sort(sources.begin(),sources.end(),[](const Source &a,const Source &b){return a.slot<b.slot;});
		FrameCathedralFireLightCount=sources.size();CathedralFireState.lights=sources.size();
		if(sources.empty()) {
			ResetCathedralFireDynamicShadowCache();finish();return;
		}
		// Exact values, not a lossy hash. Excludes time, dLight and camera pose.
		// Visibility matters because the native frame only admits visible owners.
		std::vector<uint64_t> key{snapshot->gameRevision,CathedralFrame.geometryRevision,
			uint64_t(CathedralFrame.regionX),uint64_t(CathedralFrame.regionZ),
			uint64_t(CathedralArt.loaded),uint64_t(DoorModels.loaded),sources.size()};
		const auto bits=[](float value){uint32_t result;std::memcpy(&result,&value,sizeof result);return result;};
		for(size_t i=0;i<sources.size();++i) {
			const auto &source=sources[i];FrameCathedralFireLights[i]=source.light;
			key.insert(key.end(),{uint64_t(source.slot),source.identity,bits(source.light.position.x),bits(source.light.position.height),bits(source.light.position.z),bits(source.light.radius)});
		}
		// Authored placements may extend beyond the selected three regions; a
		// nearby caster outside that band must invalidate when visibility changes.
		for(int z=snapshot->minZ;z<snapshot->maxZ;++z)
		for(int x=snapshot->minX;x<snapshot->maxX;++x) {
			const auto &cell=snapshot->At(x,z);key.push_back(uint64_t(cell.flags&2)|(uint64_t(cell.transparency)<<8));
		}
		for(bool active:snapshot->activeTransparency)key.push_back(active);
		// Door progress was frozen by PrepareTownViewLiveFrame. Never advance its
		// presentation clock in a lighting prepass or read a second native state.
		for(const auto &door:snapshot->doors)if(CathedralTileInFrame({door.x,door.z})) {
			key.insert(key.end(),{uint64_t(door.slot),uint64_t(door.x),uint64_t(door.z),uint64_t(door.orientation),uint64_t(door.state),bits(DoorModels.states[door.slot].progress)});
		}

        const auto refuse=[&](bool limited) {
            CathedralFireMaps={};ResetCathedralFireDynamicShadowCache();FrameCathedralFireValid=false;
            CathedralFireState.failed=true;CathedralFireState.budgetLimited=limited;
        };
        if(key==CathedralFireCacheKey && CathedralFireStaticMaps.stats.ready && CathedralFireStaticMaps.lightCount==sources.size()) {
            CathedralFireState.staticCacheHit=true;
        } else {
            CaptureGpu=false;CollectingCathedralFireCasters=true;CollectingCathedralDynamicCasters=false;
            CathedralFireDomain=CathedralFireCasterDomain::Structure;CathedralFireCollectionFailed=false;
            DrawCathedralWorld(out);
            CollectingCathedralFireCasters=false;DrawingCathedralStructure=false;CaptureGpu=scope.oldCapture;DrawingCathedral=scope.oldDrawing;
            if(CathedralFireCollectionFailed) {refuse(true);finish();return;}
            if(!BuildTownPointShadowMaps(CathedralFireStaticMaps,CathedralFireCasterTriangles,CathedralFireCasterMasks,
                {FrameCathedralFireLights.data(),sources.size()},0xC125B002ULL,++CathedralFireMapRevision)) {
                CathedralFireCacheKey.clear();refuse(CathedralFireStaticMaps.stats.budgetLimited);finish();return;
            }
            CathedralFireState.ownedMaskBytes=CathedralFireOwnedMaskBytes;
            CathedralFireCacheKey=key;
        }
        ReleaseCathedralFireCasterSnapshot();
        CathedralFireState.staticTriangles=CathedralFireStaticMaps.stats.acceptedTriangles;
        if(AutomaticFireCastersEnabledForDiagnostics) {
            FreezeCathedralFirePropPoses();
            CaptureGpu=false;CollectingCathedralFireCasters=CollectingCathedralDynamicCasters=true;
            CathedralFireCollectionFailed=false;
            DrawCathedralWorld(out);
            CollectingCathedralFireCasters=CollectingCathedralDynamicCasters=false;
            DrawingCathedralStructure=false;CaptureGpu=scope.oldCapture;DrawingCathedral=scope.oldDrawing;
            CathedralFireState.ownedMaskBytes=std::max(CathedralFireState.ownedMaskBytes,CathedralFireOwnedMaskBytes);
            if(ConsumeAutomaticFireShadowFailure(TownViewAutomaticShadowFailurePoint::DynamicCollection)
                || CathedralFireCollectionFailed) {refuse(true);finish();return;}
			const bool dynamicCacheHit=CathedralFireDynamicMaps.stats.ready
			    && CathedralFireDynamicKeyMatches(CathedralFireDynamicCacheKey,
			        std::span<const TownPointShadowTriangle>(CathedralFireCasterTriangles),
			        std::span<const TownPointShadowMask>(CathedralFireCasterMasks),key);
			if(dynamicCacheHit) {
				CathedralFireState.dynamicKeyBytes=CathedralFireDynamicCacheKey.size();
                CathedralFireState.dynamicCacheHit=true;
            } else {
				std::vector<uint8_t> dynamicKey;
				if(!CaptureCathedralFireDynamicKey(dynamicKey,
				        std::span<const TownPointShadowTriangle>(CathedralFireCasterTriangles),
				        std::span<const TownPointShadowMask>(CathedralFireCasterMasks),key)) {refuse(true);finish();return;}
				CathedralFireState.dynamicKeyBytes=dynamicKey.size();
				const bool dynamicContextStable=CathedralFireDynamicContextMatches(CathedralFireDynamicCacheKey,key);
				town_point_shadow_detail::Bounds dynamicChangedBounds;
				const auto frameLights=std::span<const TownPointLight>(FrameCathedralFireLights.data(),sources.size());
				const auto dynamicTriangles=std::span<const TownPointShadowTriangle>(CathedralFireCasterTriangles);
				const auto dynamicMasks=std::span<const TownPointShadowMask>(CathedralFireCasterMasks);
				const bool exactDelta=dynamicContextStable
				    && town_point_shadow_detail::FindChangedGeometryBounds(CathedralFireDynamicMaps,dynamicTriangles,dynamicMasks,
				        frameLights,0xC125B003ULL,dynamicChangedBounds);
				const auto *changedBounds=exactDelta?&dynamicChangedBounds:nullptr;
                if(ConsumeAutomaticFireShadowFailure(TownViewAutomaticShadowFailurePoint::DynamicBuild)
                    || !BuildTownPointShadowMapsIncremental(CathedralFireDynamicMaps,dynamicTriangles,dynamicMasks,
                        frameLights,0xC125B003ULL,++CathedralFireMapRevision,CathedralFireDynamicMaps,changedBounds)) {
                    refuse(true);finish();return;
                }
                CathedralFireDynamicCacheKey=std::move(dynamicKey);
            }
            CathedralFireState.dynamicTriangles=CathedralFireDynamicMaps.stats.acceptedTriangles;
            if(ConsumeAutomaticFireShadowFailure(TownViewAutomaticShadowFailurePoint::Merge)) {refuse(false);finish();return;}
            if(CathedralFireMergedStaticRevision!=CathedralFireStaticMaps.revision
                || CathedralFireMergedDynamicRevision!=CathedralFireDynamicMaps.revision
                || !CathedralFireMaps.stats.ready) {
                if(!MergeTownPointShadowLayers(CathedralFireMaps,CathedralFireStaticMaps,CathedralFireDynamicMaps,
                    0xC125B101ULL,++CathedralFireMapRevision)) {refuse(true);finish();return;}
                CathedralFireMergedStaticRevision=CathedralFireStaticMaps.revision;
                CathedralFireMergedDynamicRevision=CathedralFireDynamicMaps.revision;
            }
            CathedralFireState.automaticCasters=true;
        } else {
            CathedralFireMaps=CathedralFireStaticMaps;
            CathedralFireMergedStaticRevision=CathedralFireMergedDynamicRevision=0;
        }
        CathedralFireState.cacheHit=CathedralFireState.staticCacheHit
            && (!AutomaticFireCastersEnabledForDiagnostics || CathedralFireState.dynamicCacheHit);
        CathedralFireState.staticMapRevision=CathedralFireStaticMaps.revision;
        CathedralFireState.dynamicMapRevision=AutomaticFireCastersEnabledForDiagnostics?CathedralFireDynamicMaps.revision:0;
		for(size_t i=0;i<sources.size();++i)
			FrameCathedralFireLights[i]=TownFireLightAtTime(sources[i].light,sources[i].identity,FrameFireTime,FireIntensityVariation);
		const auto &stats=CathedralFireMaps.stats;
		CathedralFireState.ready=FrameCathedralFireValid=stats.ready;
		CathedralFireState.triangles=stats.acceptedTriangles;CathedralFireState.masks=stats.masks;
		CathedralFireState.mapBytes=stats.depthBytes;
		CathedralFireState.retainedMapBytes=(CathedralFireMaps.depth.size()+CathedralFireStaticMaps.depth.size()+CathedralFireDynamicMaps.depth.size())*sizeof(float);CathedralFireState.scratchBytes=stats.scratchBytes;
		CathedralFireState.rays=stats.rays;CathedralFireState.boxTests=stats.boxTests;CathedralFireState.triangleTests=stats.triangleTests;
		CathedralFireState.mapRevision=CathedralFireMaps.revision;
		CathedralFireState.buildMilliseconds=CathedralFireState.cacheHit?0:stats.buildMilliseconds;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	} catch(const std::bad_alloc &) {
		CathedralFireMaps={};ResetCathedralFireDynamicShadowCache();FrameCathedralFireValid=false;CathedralFireState.failed=true;CathedralFireState.budgetLimited=true;
	} catch(const std::exception &) {
		CathedralFireMaps={};ResetCathedralFireDynamicShadowCache();FrameCathedralFireValid=false;CathedralFireState.failed=true;
	}
#endif
	finish();
}
