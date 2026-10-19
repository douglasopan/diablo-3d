#include "engine/render/surface_region_town_preview.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <cstring>
#include <new>
#include <utility>

#include "engine/render/town_model_import.hpp"
#include "mods/mod_identity.h"
#include "engine/assets.hpp"
#include "levels/reencode_dun_cels.hpp"
#include "utils/endian_read.hpp"
#include "utils/cel_to_clx.hpp"
#include "utils/clx_decode.hpp"

namespace devilution {
namespace {
bool ReadNativeTownBytes(const char *primary,const char *fallback,size_t maximum,
 std::vector<std::byte> &output,std::string &selected,std::string &failure)
{
 auto ref=FindAsset(primary);selected=primary;
 if(!ref.ok()&&fallback){ref=FindAsset(fallback);selected=fallback;}
 if(!ref.ok()||ref.size()==0||ref.size()>maximum){failure="preview-native-resource-size";return false;}
 output.resize(ref.size());auto handle=OpenAsset(std::move(ref));
 if(!handle.ok()||!handle.read(output.data(),output.size())){failure="preview-native-resource-read";return false;}
 return true;
}

bool NativeCelOffsets(std::span<const std::byte> bytes,std::vector<uint32_t> &offsets)
{
 offsets.clear();
 if(bytes.size()<12)return false;
 const uint32_t count=LoadLE32(bytes.data());
 if(count==0||count>4095||uint64_t(count+2)*4>bytes.size())return false;
 offsets.reserve(count+1);
 uint32_t previous=(count+2)*4;
 for(uint32_t i=1;i<=count+1;++i){const uint32_t at=LoadLE32(bytes.data()+size_t(i)*4);
  if(at<previous||at>bytes.size())return false;offsets.push_back(at);previous=at;}
 return offsets.back()==bytes.size();
}

bool NativeRleRows(std::span<const std::byte> bytes,unsigned width,unsigned rows,bool exactRows)
{
 size_t cursor=0;unsigned completed=0;
 while(cursor<bytes.size()&&(!exactRows||completed<rows)){
  unsigned left=width;
  while(left){if(cursor==bytes.size())return false;
   const int control=static_cast<int8_t>(std::to_integer<uint8_t>(bytes[cursor++]));
   const unsigned length=unsigned(control<0?-control:control);
   if(length==0||length>left)return false;
   if(control>0){if(length>bytes.size()-cursor)return false;cursor+=length;}left-=length;
  }
  if(++completed>4096)return false;
 }
 return exactRows?completed==rows:cursor==bytes.size()&&completed>0;
}

std::shared_ptr<const SurfaceTownNativeArtworkSource> PrepareNativeTownArtwork(uint64_t generation,std::string &failure)
{
 static std::weak_ptr<const SurfaceTownNativeArtworkSource> previous;
 if(auto ready=previous.lock();ready&&ready->resourceGeneration==generation)return ready;
 std::vector<std::byte> cel,min,sol,pal,special;std::string cp,mp,sp,pp,xp;
 if(!ReadNativeTownBytes("nlevels\\towndata\\town.cel","levels\\towndata\\town.cel",64*1024*1024,cel,cp,failure)
  ||!ReadNativeTownBytes("nlevels\\towndata\\town.min","levels\\towndata\\town.min",1379*32,min,mp,failure)
  ||!ReadNativeTownBytes("nlevels\\towndata\\town.sol","levels\\towndata\\town.sol",1379,sol,sp,failure)
  ||!ReadNativeTownBytes("levels\\towndata\\town.pal",nullptr,768,pal,pp,failure))return {};
#ifdef UNPACKED_MPQS
 if(!ReadNativeTownBytes("levels\\towndata\\towns.clx",nullptr,16*1024*1024,special,xp,failure))return {};
#else
 if(!ReadNativeTownBytes("levels\\towndata\\towns.cel",nullptr,16*1024*1024,special,xp,failure))return {};
#endif
 if(min.empty()||min.size()%32!=0||min.size()/32>1379||sol.size()<min.size()/32||pal.size()!=768){failure="preview-native-town-shape";return {};}
 std::vector<uint32_t> offsets;
 if(!NativeCelOffsets(cel,offsets)){failure="preview-native-cel-offsets";return {};}
 auto owner=std::make_shared<SurfaceTownNativeArtworkSource>();owner->resourceGeneration=generation;owner->pieceCount=min.size()/32;
 std::map<uint16_t,DunFrameInfo> frames;
 // EXACT Town branch/order from SetDungeonMicros, with all storage local.
 for(size_t tile=0;tile<owner->pieceCount;++tile){owner->properties[tile]=static_cast<TileProperties>(std::to_integer<uint8_t>(sol[tile]));
  for(uint32_t block=0;block<16;++block){
   const auto raw=LoadLE16(min.data()+(tile*16+14+(block&1)-(block&0xE))*2);
   const LevelCelBlock value{raw};owner->micros[tile].mt[block]=value;
   if(!value.hasValue())continue;
   if(value.frame()==0||value.frame()>=offsets.size()||static_cast<unsigned>(value.type())>5){failure="preview-native-min-frame";return {};}
   const DunFrameInfo reference{static_cast<uint8_t>(block),value.type(),owner->properties[tile]};
   const auto [found,inserted]=frames.try_emplace(value.frame(),reference);
   // All MICROS keep their own type words after reencoding. A single CEL
   // frame must therefore have one encoding; checking the first use alone
   // could encode512 bytes then let a later Square use read1024. Only raw
   // TransparentSquare changes its layout with floor/left-right semantics.
   if(!inserted&&(found->second.type!=reference.type
    ||(reference.type==TileType::TransparentSquare
     &&(found->second.isFloor()!=reference.isFloor()
      ||(reference.isFloor()&&found->second.isFloorLeft()!=reference.isFloorLeft()))))){
    failure="preview-native-min-conflicting-frame-layout";return {};
   }
  }
 }
 if(frames.empty()){failure="preview-native-min-empty";return {};}
 std::vector<std::pair<uint16_t,DunFrameInfo>> frameList(frames.begin(),frames.end());
 for(const auto &[frame,info]:frameList){const size_t begin=offsets[frame-1],end=offsets[frame];const auto bytes=std::span(cel).subspan(begin,end-begin);
  const size_t need=info.type==TileType::Square?1024:(info.type==TileType::LeftTriangle||info.type==TileType::RightTriangle)?544:800;
  if(info.type==TileType::TransparentSquare){if(!NativeRleRows(bytes,32,32,true)){failure="preview-native-floor-rle";return {};}}
  else if(bytes.size()<need){failure="preview-native-frame-size";return {};}
 }
 owner->cels=std::make_unique<std::byte[]>(cel.size());std::memcpy(owner->cels.get(),cel.data(),cel.size());
 ReencodeDungeonCels(owner->cels,frameList);
 const auto adjustments=ComputeCelBlockAdjustments(frameList);
 for(auto &tile:owner->micros)for(auto &value:tile.mt){const auto frame=value.frame();const auto pair=std::make_pair(frame,frame);
  const auto it=std::upper_bound(adjustments.begin(),adjustments.end(),pair,[](auto a,auto b){return a.first<b.first;});
  if(it!=adjustments.end())value.data-=it->second;
 }
 owner->celsBytes=LoadLE32(owner->cels.get()+(frameList.size()+1)*4);
 if(owner->celsBytes>64*1024*1024){failure="preview-native-reencoded-budget";return {};}
 std::vector<uint32_t> specialOffsets;
 if(!NativeCelOffsets(special,specialOffsets)){failure="preview-native-special-offsets";return {};}
#ifdef UNPACKED_MPQS
 for(size_t i=1;i<specialOffsets.size();++i){auto frame=std::span(special).subspan(specialOffsets[i-1],specialOffsets[i]-specialOffsets[i-1]);
  if(frame.size()<6){failure="preview-native-clx-header";return {};}
  const size_t header=LoadLE16(frame.data()),width=LoadLE16(frame.data()+2),height=LoadLE16(frame.data()+4);
  if(header<6||header>frame.size()||width!=64||height==0||height>4096){failure="preview-native-clx-shape";return {};}
  size_t cursor=header,pixels=0;
  while(cursor<frame.size()){const auto control=std::to_integer<uint8_t>(frame[cursor++]);unsigned length;
   if(!IsClxOpaque(control))length=control;
   else if(IsClxOpaqueFill(control)){length=GetClxOpaqueFillWidth(control);if(cursor==frame.size()){failure="preview-native-clx-fill";return {};}++cursor;}
   else{length=GetClxOpaquePixelsWidth(control);if(length>frame.size()-cursor){failure="preview-native-clx-literal";return {};}cursor+=length;}
   if(length==0||pixels+length>width*height){failure="preview-native-clx-pixels";return {};}pixels+=length;
  }if(pixels!=width*height){failure="preview-native-clx-total";return {};}
 }
 auto specialCopy=std::make_unique<uint8_t[]>(special.size());std::memcpy(specialCopy.get(),special.data(),special.size());
 owner->specials=OwnedClxSpriteList(std::move(specialCopy));
#else
 for(size_t i=1;i<specialOffsets.size();++i){auto frame=std::span(special).subspan(specialOffsets[i-1],specialOffsets[i]-specialOffsets[i-1]);
  if(frame.size()<2){failure="preview-native-special-frame";return {};}
  if(LoadLE16(frame.data())==10){if(frame.size()<10){failure="preview-native-special-header";return {};}frame=frame.subspan(10);}
  if(!NativeRleRows(frame,64,0,false)){failure="preview-native-special-rle";return {};}
 }
 owner->specials=CelToClx(reinterpret_cast<const uint8_t*>(special.data()),special.size(),PointerOrValue<uint16_t>{64}).list();
#endif
 for(size_t i=0;i<256;++i){owner->palette[i]={std::to_integer<uint8_t>(pal[i*3]),std::to_integer<uint8_t>(pal[i*3+1]),std::to_integer<uint8_t>(pal[i*3+2]),255};
  for(auto &table:owner->identityLightTables)table[i]=static_cast<uint8_t>(i);}
 owner->identity="native-town-owned-r1:"+cp+":"+ModHashToHex(ComputeBytesSha256(cel))+":"+mp+":"+ModHashToHex(ComputeBytesSha256(min))
  +":"+sp+":"+ModHashToHex(ComputeBytesSha256(sol))+":"+pp+":"+ModHashToHex(ComputeBytesSha256(pal))+":"+xp+":"+ModHashToHex(ComputeBytesSha256(special));
 owner->retainedBytes=sizeof(*owner)+owner->celsBytes+owner->specials->dataSize()+owner->identity.capacity();
 if(owner->retainedBytes>96*1024*1024){failure="preview-native-owned-budget";return {};}
 previous=owner;return owner;
}

bool ZeroHash(const std::array<uint8_t, 32> &hash)
{
	return std::all_of(hash.begin(), hash.end(), [](uint8_t value) { return value == 0; });
}

bool SameEvents(const surface::TownVisualEvents &a, const surface::TownVisualEvents &b) noexcept
{
	return a.wellWater == b.wellWater && a.catacombsAccess == b.catacombsAccess
	    && a.cavesAccess == b.cavesAccess && a.hellAccess == b.hellAccess;
}

bool FrozenEvent(const surface::TownVisualEvents &events, TownVisualObjectEvent event)
{
	switch (event) {
	case TownVisualObjectEvent::None: return false;
	case TownVisualObjectEvent::WellWater: return events.wellWater;
	case TownVisualObjectEvent::CatacombsAccess: return events.catacombsAccess;
	case TownVisualObjectEvent::CavesAccess: return events.cavesAccess;
	case TownVisualObjectEvent::HellAccess: return events.hellAccess;
	}
	return false;
}

bool WithinBudget(const SurfaceTownPreviewStats &stats, const SurfaceTownPreviewBudget &budget)
{
	return budget.maximumModels != 0 && budget.maximumStaticObjects != 0
	    && budget.maximumStoredTriangles != 0 && budget.maximumDecodedPayloadBytes != 0
	    && stats.models <= budget.maximumModels && stats.staticObjects <= budget.maximumStaticObjects
	    && stats.storedTriangles <= budget.maximumStoredTriangles
	    && stats.decodedPayloadBytes <= budget.maximumDecodedPayloadBytes;
}

size_t AuditBytes(const TownModelRuntimeAudit &audit)
{
	return audit.assetPath.capacity() + audit.sourceKind.capacity() + audit.sha256.capacity()
	    + audit.expectedSha256.capacity() + audit.revision.capacity() + audit.status.capacity() + audit.failure.capacity();
}

void CountModel(SurfaceTownPreviewStats &stats, const TownSceneModel &model)
{
	stats.storedTriangles += model.triangles.size();
	stats.decodedPayloadBytes += sizeof(model) + model.triangles.capacity() * sizeof(TownSceneTriangle)
	    + model.materialPatches.capacity() * sizeof(TownSceneMaterialPatch) + AuditBytes(model.runtimeAudit);
	if (model.importedTexture) stats.decodedPayloadBytes += sizeof(TownImportedTexture) + model.importedTexture->rgb.capacity();
	if (model.cabinInterior) {
		const auto &room = *model.cabinInterior;
		stats.storedTriangles += room.exteriorTriangles.size() + room.interiorTriangles.size();
		stats.decodedPayloadBytes += sizeof(room) + (room.exteriorTriangles.capacity() + room.interiorTriangles.capacity()) * sizeof(TownSceneTriangle)
		    + room.openings.capacity() * sizeof(TownCabinOpening) + room.apertures.capacity() * sizeof(TownLightAperture)
		    + room.lightOccluders.capacity() * sizeof(TownLightOccluder) + room.fireSources.capacity() * sizeof(TownCabinFireSource);
		for (const auto &fire : room.fireSources) {
			stats.storedTriangles += fire.emissiveTriangles.size();
			stats.decodedPayloadBytes += fire.emissiveTriangles.capacity() * sizeof(TownSceneTriangle);
		}
	}
}

bool PrepareIndexesAndStats(SurfaceTownPreviewSource &source, std::string &failure)
{
	auto &stats = source.stats;
	stats.models = source.visual.models.size(); stats.staticObjects = source.visual.statics.objects.size();
	stats.decodedPayloadBytes = sizeof(source) + (source.native ? source.native->retainedBytes : 0);
	stats.decodedPayloadBytes += (source.visual.models.capacity() - source.visual.models.size()
	    + source.visual.materialSources.capacity() - source.visual.materialSources.size()) * sizeof(TownSceneModel)
	    + (source.visual.statics.objects.capacity() - source.visual.statics.objects.size()) * sizeof(TownVisualObject)
	    + (source.visual.statics.textures.capacity() - source.visual.statics.textures.size()) * sizeof(TownVisualObjectTexture);
	for (size_t i = 0; i < source.visual.models.size(); ++i) {
		const auto &model = source.visual.models[i];
		CountModel(stats, model);
		if (model.nativeArtwork.enabled || std::any_of(model.triangles.begin(), model.triangles.end(),
		        [](const TownSceneTriangle &triangle) { return triangle.nativeProjection; }))
			source.nativeArtworkModels.push_back(static_cast<uint32_t>(i));
	}
	for (const auto &model : source.visual.materialSources) CountModel(stats, model);
	for (const auto &texture : source.visual.statics.textures)
		stats.decodedPayloadBytes += sizeof(texture) + texture.pixels.capacity() + texture.opacity.capacity();
	for (size_t i = 0; i < source.visual.statics.objects.size(); ++i) {
		const auto &object = source.visual.statics.objects[i];
		stats.storedTriangles += object.mesh.triangles.size();
		stats.decodedPayloadBytes += sizeof(object) + object.mesh.triangles.capacity() * sizeof(TownVolumeTriangle)
		    + object.sourceCells.capacity() * sizeof(TownVisualSourceCell) + object.id.capacity();
		bool changed = object.eventBinding != TownVisualObjectEvent::None
		    && FrozenEvent(source.map.events, object.eventBinding) != object.eventState;
		for (const auto &cell : object.sourceCells) {
			if (cell.tile.x < 0 || cell.tile.y < 0 || size_t(cell.tile.x) >= surface::TownVisualSide || size_t(cell.tile.y) >= surface::TownVisualSide) {
				failure = "preview-source-cell-bounds"; return false;
			}
			const size_t index = size_t(cell.tile.y) * surface::TownVisualSide + cell.tile.x;
			changed |= cell.piece != source.map.pieces[index] || cell.special != source.map.specials[index];
		}
		if (changed) source.changedNativeSourceObjects.push_back(static_cast<uint32_t>(i));
	}
	const auto &audit = source.visual.sceneAudit;
	stats.decodedPayloadBytes += audit.assetPath.capacity() + audit.sourceKind.capacity() + audit.sha256.capacity()
	    + audit.revision.capacity() + audit.status.capacity() + audit.failure.capacity()
	    + audit.staticData.capacity() + audit.staticSha256.capacity()
	    + audit.objects.capacity() * sizeof(TownVisualSceneObjectAudit);
	for (const auto &object : audit.objects)
		stats.decodedPayloadBytes += object.stableId.capacity() + object.name.capacity() + object.objectClass.capacity()
		    + object.assetPath.capacity() + object.sha256.capacity() + object.sourceKind.capacity()
		    + object.eventBinding.capacity() + object.eventState.capacity();
	const auto &statics = source.visual.staticAudit;
	stats.decodedPayloadBytes += statics.path.capacity() + statics.sha256.capacity() + statics.expectedSha256.capacity()
	    + statics.failure.capacity() + statics.status.capacity() + source.visual.worldPatchSha256.capacity()
	    + source.nativeArtworkModels.capacity() * sizeof(uint32_t) + source.changedNativeSourceObjects.capacity() * sizeof(uint32_t);
	return true;
}
} // namespace

void ResetSurfaceTownPreview(SurfaceTownPreviewCache &cache) noexcept
{
	cache = {};
}

bool SameSurfaceTownPreviewContent(const surface::TownVisualMap &a, const surface::TownVisualMap &b) noexcept
{
	return a.schema == b.schema && a.gameVariant == b.gameVariant && a.fullbrightLight == b.fullbrightLight
	    && a.mapSha256 == b.mapSha256 && a.sourceTownSceneSha256 == b.sourceTownSceneSha256
	    && a.worldPatchSha256 == b.worldPatchSha256
	    && a.visualSelection.kind == b.visualSelection.kind
	    && a.visualSelection.canonicalSha256 == b.visualSelection.canonicalSha256
	    && a.visualSelection.selectedSha256 == b.visualSelection.selectedSha256
	    && a.visualSelection.staticSha256 == b.visualSelection.staticSha256
	    && SameEvents(a.events, b.events)
	    && a.pieces == b.pieces && a.specials == b.specials;
}

bool PrepareSurfaceTownPreview(SurfaceTownPreviewCache &cache, uint64_t resourceGeneration,
    const surface::TownVisualMap &map, const SurfaceTownPreviewBudget &budget,
    std::shared_ptr<const SurfaceTownPreviewSource> &output, std::string &failure)
{
	output.reset();
	const auto fail = [&](std::string_view reason) {
		cache.published.reset(); cache.failure = reason; failure = reason; return false;
	};
	if (resourceGeneration == 0 || map.schema != surface::TownVisualSchemaRevision || map.gameVariant != 0
	    || map.capturedTownEpoch == 0 || map.fullbrightLight != 0 || ZeroHash(map.mapSha256) || ZeroHash(map.sourceTownSceneSha256)
	    || map.visualSelection.kind == TownVisualSelectionKind::NotAdmitted
	    || map.visualSelection.canonicalSha256 != map.sourceTownSceneSha256 || ZeroHash(map.visualSelection.staticSha256)) {
		// An invalid request must not poison the memoized result of an older,
		// valid content key when the host returns to it.
		cache.attemptedMap.reset(); cache.resourceGeneration = 0;
		return fail("preview-companion-header");
	}
	if (budget.maximumModels == 0 || budget.maximumStaticObjects == 0
	    || budget.maximumStoredTriangles == 0 || budget.maximumDecodedPayloadBytes == 0) {
		cache.attemptedMap.reset(); cache.resourceGeneration = 0;
		return fail("preview-source-budget-header");
	}
	const bool same = cache.resourceGeneration == resourceGeneration && cache.attemptedMap
	    && SameSurfaceTownPreviewContent(*cache.attemptedMap, map) && cache.attemptedBudget == budget;
	if (same) {
		if (!cache.published) { failure = cache.failure; return false; }
		output = cache.published; failure.clear(); return true;
	}
	cache.published.reset(); cache.resourceGeneration = resourceGeneration;
	cache.attemptedMap = map; cache.attemptedBudget = budget; cache.failure.clear();
	try {
		if (cache.coldLoadAttempts == std::numeric_limits<uint64_t>::max()) return fail("preview-attempt-counter-exhausted");
		++cache.coldLoadAttempts;
		auto candidate = std::make_shared<SurfaceTownPreviewSource>();
		candidate->resourceGeneration = resourceGeneration; candidate->map = map;
		candidate->native=PrepareNativeTownArtwork(resourceGeneration,failure);
		if(!candidate->native)return fail(failure);
		const TownVisualPreviewEvents events { map.events.wellWater, map.events.catacombsAccess,
			map.events.cavesAccess, map.events.hellAccess };
		if (!LoadTownVisualPreviewSource(map.visualSelection, events, candidate->visual, failure)) return fail(failure);
		const auto &worldHash = candidate->visual.worldPatchSha256;
		if ((worldHash.empty() && !ZeroHash(map.worldPatchSha256))
		    || (!worldHash.empty() && worldHash != ModHashToHex(map.worldPatchSha256))) return fail("preview-companion-world-patch-identity");
		if (!PrepareIndexesAndStats(*candidate, failure)) return fail(failure);
		if (!WithinBudget(candidate->stats, budget)) return fail("preview-source-budget");
		static uint64_t nextRevision = 0; // Render-thread resource identity, no RNG.
		if (nextRevision == std::numeric_limits<uint64_t>::max()) return fail("preview-revision-exhausted");
		candidate->revision = ++nextRevision;
		cache.published = std::move(candidate); output = cache.published; failure.clear(); return true;
	} catch (const std::bad_alloc &) { return fail("preview-source-allocation"); }
}
} // namespace devilution
