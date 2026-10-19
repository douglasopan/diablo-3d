// Host-owned immutable authored resources; per-frame placements observe native
// scene owners. Native SOL, object states, RNG and camera collision stay native.
#include "engine/render/cathedral_authored_assets.inc"
struct CathedralArtResource { TownSceneModel model; ResidentMeshArtwork resident; Vec3 low{},high{}; size_t sourceTriangles=0; bool visualTopLimited=false; };
struct CathedralArtContributor { Point tile; uint64_t owner; };
struct CathedralArtPlacement {
    size_t part; Vec3 origin; uint8_t turns; Point tile; uint64_t owner;
    // Primary tile/owner keep their R2 pick/light identity. Shared presentation
    // visibility uses every current native contributor, never first-writer only.
    std::vector<CathedralArtContributor> contributors;
};
struct CathedralThinWall { Vec3 center; float oldHalf; bool alongZ; Vec3 a{},b{}; float oldLong=.5F; Point pick{}; bool visible=false,paired=false,capOwner=true; };
struct CathedralDownStair { Vec3 center; Point tile; uint64_t owner; uint8_t light; bool visible; };
struct CathedralArtState {
    std::vector<CathedralDownStair> downStairs;
    std::set<std::pair<int,int>> replacedSpecialCells;
    std::map<uint64_t,CathedralThinWall> thinWalls;
    std::map<std::pair<int,int>,uint8_t> wallPaving;
    size_t mergedWalls=0;
    size_t thinWallDraws=0,fullPierDraws=0,specialOverlaysSkipped=0;
    std::array<CathedralArtResource,9> resources;
    std::vector<CathedralArtPlacement> placements;
    std::set<uint64_t> replaced;
    std::vector<Vec3> nativeObjectFootprints;
    std::map<std::array<int,9>,unsigned> wallEdges;
    size_t sharedReturnsSkipped=0;
    uint64_t epoch=0,geometry=0;
    bool attempted=false,loaded=false;
    size_t ceilingDraws=0;
    size_t floorDraws=0,wallDraws=0,modelDraws=0,objectFloorCells=0,recessedWalls=0;
    std::array<size_t,9> partDraws{};
};
CathedralArtState CathedralArt;
using CathedralArtContributorMap=std::map<uint64_t,Point>;
bool CathedralArtContributorCurrent(const CathedralArtContributor &contributor,const CathedralArtContributorMap &current)
{
    const auto found=current.find(contributor.owner);
    return contributor.owner!=0 && found!=current.end() && found->second==contributor.tile;
}
bool CathedralArtPlacementCurrent(const CathedralArtPlacement &placement,const CathedralArtContributorMap &current)
{
    if(placement.contributors.empty())return false;
    bool primary=false;
    for(size_t n=0;n<placement.contributors.size();++n){
        const auto &c=placement.contributors[n];
        if(!CathedralArtContributorCurrent(c,current))return false;
        if(c.owner==placement.owner && c.tile==placement.tile)primary=true;
        for(size_t prior=0;prior<n;++prior){const auto &other=placement.contributors[prior];if(other.owner==c.owner && other.tile==c.tile)return false;}
    }
    return primary;
}
bool CathedralArtPlacementsCurrent(const CathedralArtContributorMap &current)
{
    return std::all_of(CathedralArt.placements.begin(),CathedralArt.placements.end(),[&](const auto &p){return CathedralArtPlacementCurrent(p,current);});
}
bool AssociateCathedralArtContributors(const std::vector<size_t> &placements,const std::vector<CathedralArtContributor> &incoming,const CathedralArtContributorMap &current)
{
    // Preflight only the touched mesh/supports (at most3) and incoming pairs.
    // A bad second support/source cannot partially bind the first mesh.
    // The global invariant is checked once at the end of Prepare, not per arc.
    if(placements.empty() || placements.size()>3 || incoming.empty() || incoming.size()>2)return false;
    for(const auto placement:placements)
        if(placement>=CathedralArt.placements.size() || !CathedralArtPlacementCurrent(CathedralArt.placements[placement],current))return false;
    for(const auto &c:incoming)if(!CathedralArtContributorCurrent(c,current))return false;
    for(const auto placement:placements){
        auto &p=CathedralArt.placements[placement];
        for(const auto &c:incoming)if(std::none_of(p.contributors.begin(),p.contributors.end(),[&](const auto &old){return old.owner==c.owner && old.tile==c.tile;}))p.contributors.push_back(c);
    }
    return true;
}
bool AssociateCathedralArtContributor(size_t placement,Point tile,uint64_t owner,const CathedralArtContributorMap &current)
{
    return AssociateCathedralArtContributors({placement},{{tile,owner}},current);
}
constexpr size_t CathedralArtTextureKey=std::numeric_limits<size_t>::max()-4096;
bool LimitCathedralPierVisualToSelectedCourse(TownSceneModel &model)
{
    // The selected R19 mesh has a complete top cap at this existing course.
    // Keep its original lower triangles and UVs, including that cap; remove
    // the following course's bottom cap and all upper courses. No asset is
    // rewritten, no cap is duplicated and no model is scaled along one axis.
    constexpr float epsilon = 0.00001F;
    const float top = cathedral::VisualWallTopMetres;
    auto retained = model.triangles;
    retained.clear();
    size_t capTriangles = 0;
    for (const auto &triangle : model.triangles) {
        float low = triangle.vertices[0].height;
        float high = low;
        for (const auto &vertex : triangle.vertices) {
            low = std::min(low, vertex.height);
            high = std::max(high, vertex.height);
        }
        if (high > top + epsilon) {
            if (low < top - epsilon) return false; // Unknown crossing topology.
            continue;
        }
        if (low >= top - epsilon) {
            if (triangle.normal.height <= 0.5F) continue; // Upper bottom cap.
            ++capTriangles;
        }
        retained.push_back(triangle);
    }
    if (model.triangles.size() != 1292 || retained.size() != 1148 || capTriangles != 2)
        return false;
    model.triangles = std::move(retained);
    return true;
}

void PrepareCathedralArt()
{
    if(CathedralArt.attempted)return;
    CathedralArt.attempted=true;bool all=true;
    for(size_t i=0;i<CathedralArt.resources.size();++i) {
        auto &r=CathedralArt.resources[i];
        all=LoadTownModelOverride(r.model,std::string("d3d-props/cathedral-r14/")+CathedralArtNames[i]+".d3d",CathedralArtHashes[i],"cathedral-authored-r14")&&all;
        if(!r.model.importedTexture)continue;
        r.sourceTriangles=r.model.triangles.size();
        if(i==6){r.visualTopLimited=LimitCathedralPierVisualToSelectedCourse(r.model);all=r.visualTopLimited && all;}
        const float inf=std::numeric_limits<float>::infinity();r.low={inf,inf,inf};r.high={-inf,-inf,-inf};
        for(const auto &t:r.model.triangles)for(const auto &v:t.vertices) {
            r.low.x=std::min(r.low.x,v.x);r.low.y=std::min(r.low.y,v.height);r.low.z=std::min(r.low.z,v.z);
            r.high.x=std::max(r.high.x,v.x);r.high.y=std::max(r.high.y,v.height);r.high.z=std::max(r.high.z,v.z);
            r.resident.indices.push_back(static_cast<uint32_t>(r.resident.vertices.size()));
            r.resident.vertices.push_back({{v.x,v.height,v.z},{v.u,v.v},{t.normal.x,t.normal.height,t.normal.z}});
        }
        r.resident.ready=true;
    }
    CathedralArt.loaded=all;
}
void PrepareCathedralArtTextures()
{
    if(!CathedralArt.loaded)return;
    for(size_t i=0;i<9;++i)RigidPropNativeTexture(CathedralArtTextureKey-i,*CathedralArt.resources[i].model.importedTexture,currlevel==1 && i<4);
    for(size_t i=0;i<5;++i)ImportedTextureCache.at(CathedralArtTextureKey-i).repeat=true;
}
bool CathedralArtFrameReady()
{
    return CathedralArt.loaded && currlevel==1 && !setlevel && CathedralArt.epoch==cathedral::LiveEpoch()
        && CathedralArt.geometry==CathedralFrame.geometryRevision;
}
struct CathedralBoundaryContactPlan {float offset=0;int pavingCells=0;};
CathedralBoundaryContactPlan CathedralBoundaryContact(int width,bool nearDoor) noexcept
{
    if(nearDoor || width<=0)return {};
    return {width<=2?width*.5F:.5F,width<=2?width:1};
}
template<class IsSolid>
void CathedralPaveRemappedCorner(std::map<std::pair<int,int>,uint8_t> &paving,Vec3 original,Vec3 mapped,uint8_t light,const IsSolid &solid)
{
    if(std::abs(original.x-mapped.x)<.00001F || std::abs(original.z-mapped.z)<.00001F)return;
    const int x0=int(std::floor(std::min(original.x,mapped.x)+.5F)),x1=int(std::ceil(std::max(original.x,mapped.x)+.5F))-1;
    const int z0=int(std::floor(std::min(original.z,mapped.z)+.5F)),z1=int(std::ceil(std::max(original.z,mapped.z)+.5F))-1;
    if(x1-x0>1 || z1-z0>1)return;
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)if(solid(x,z))paving[{x,z}]=light;
}
Vec3 CathedralUpLocal(const cathedral::Instance &up,Vec3 p)
{
    const auto v=p-Vec3{up.translation.x,up.translation.y,up.translation.z};
    switch(up.quarterTurns%4){case 1:return {v.z,v.y,-v.x};case 2:return {-v.x,v.y,-v.z};case 3:return {-v.z,v.y,v.x};default:return v;}
}
Vec3 CathedralUpWorld(const cathedral::Instance &up,Vec3 v)
{
    switch(up.quarterTurns%4){case 1:v={-v.z,v.y,v.x};break;case 2:v={-v.x,v.y,-v.z};break;case 3:v={v.z,v.y,-v.x};break;}
    return Vec3{up.translation.x,up.translation.y,up.translation.z}+v;
}
bool CathedralUnusedUpHalf(const cathedral::Snapshot &snapshot,const cathedral::Instance &up)
{
    if(up.module!=cathedral::ModuleKind::StairsUp || up.nativeMega!=64 || up.pick.kind!=cathedral::PickKind::Trigger
        || up.pick.nativeSlot<0 || static_cast<size_t>(up.pick.nativeSlot)>=snapshot.triggers.size())return false;
    const auto cell=[&](float x,float z){const auto p=CathedralUpWorld(up,{x,0,z});return Point{int(std::lround(p.x)),int(std::lround(p.z))};};
    const auto active=[](Point p){return p.x>=16&&p.x<96&&p.y>=16&&p.y<96;};
    const Point trigger=cell(-1,-.5F),flight=cell(0,-.5F),room=cell(-1,.5F),side=cell(-2,-.5F),back=cell(-1,-1.5F);
    if(!active(trigger)||!active(flight)||!active(room)||!active(side)||!active(back))return false;
    const auto &t=snapshot.triggers[static_cast<size_t>(up.pick.nativeSlot)];
    if(t.x!=trigger.x || t.z!=trigger.y || t.message!=static_cast<int>(WM_DIABPREVLVL) || t.targetLevel!=0)return false;
    if((snapshot.At(trigger.x,trigger.y).properties&cathedral::Solid)!=0 || (snapshot.At(flight.x,flight.y).properties&cathedral::Solid)!=0
        || (snapshot.At(room.x,room.y).properties&cathedral::Solid)!=0
        || (snapshot.At(side.x,side.y).properties&cathedral::Solid)==0 || (snapshot.At(back.x,back.y).properties&cathedral::Solid)==0)return false;
    if(snapshot.At(trigger.x,trigger.y).object!=0 || snapshot.At(flight.x,flight.y).object!=0)return false;
    const auto front=CathedralUpWorld(up,{-1.25F,0,0});
    for(const auto &d:snapshot.doors)if(std::abs(front.x-d.x)<1.8F && std::abs(front.z-d.z)<1.8F)return false;
    return true;
}
Vec3 CathedralWallPoint(const CathedralThinWall &w,Vec3 v)
{
    const float along=w.alongZ?v.z-w.center.z:v.x-w.center.x;
    const float across=w.alongZ?v.x-w.center.x:v.z-w.center.z;
    const Vec3 direction=w.b-w.a;const float length=std::sqrt(direction.x*direction.x+direction.z*direction.z);
    if(length<.00001F)return (w.a+w.b)*.5F+Vec3{0,v.y,0};
    Vec3 normal=w.alongZ?Vec3{direction.z/length,0,-direction.x/length}:Vec3{-direction.z/length,0,direction.x/length};
    Vec3 result=(w.a+w.b)*.5F+direction*(along/(w.oldLong*2))+normal*(across*PhysicalToNativeGrid(.12F)*.5F/w.oldHalf);result.y=v.y;return result;
}
void PrepareCathedralArtPlacements(const cathedral::Snapshot &snapshot,const cathedral::Scene &scene)
{
    CathedralArt.replacedSpecialCells.clear();CathedralArt.specialOverlaysSkipped=0;
    CathedralArt.downStairs.clear();CathedralArt.placements.clear();CathedralArt.replaced.clear();CathedralArt.thinWalls.clear();CathedralArt.wallPaving.clear();CathedralArt.mergedWalls=0;CathedralArt.nativeObjectFootprints.clear();CathedralArt.epoch=0;CathedralArt.geometry=0;
    if(!CathedralArt.loaded || currlevel!=1 || setlevel)return;
    const auto visible=[&](const cathedral::Instance &i){return (snapshot.At(i.pick.x,i.pick.z).flags&2)!=0;};
    CathedralArtContributorMap sourceIdentities,currentContributors;
    const auto recordContributor=[&](const cathedral::Instance &i){
        const Point tile{i.pick.x,i.pick.z};
        if(i.id==0 || tile.x<16 || tile.x>=96 || tile.y<16 || tile.y>=96)throw std::runtime_error("Authored Cathedral contributor outside current native context");
        const auto [found,inserted]=sourceIdentities.emplace(i.id,tile);
        if(!inserted && found->second!=tile)throw std::runtime_error("Authored Cathedral current owner has ambiguous native tile");
        if(visible(i))currentContributors.emplace(i.id,tile);
    };
    for(const auto &region:scene.regions){
        for(const auto &i:region->instances)recordContributor(i);
        // FitSockets may retain the original wall only in upperWallSources.
        // An identical repeated id/tile is idempotent; conflicts are refused.
        for(const auto &i:region->upperWallSources)recordContributor(i);
    }
    for(const auto &region:scene.regions)for(const auto &i:region->instances)if(i.module==cathedral::ModuleKind::StairsDown){
        const auto &cell=snapshot.At(i.pick.x,i.pick.z);
        CathedralArt.downStairs.push_back({{i.translation.x,i.translation.y,i.translation.z},{i.pick.x,i.pick.z},i.id,cell.light,(cell.flags&2)!=0});
    }
    // The six native miniset megas own the old technical solid contour. It is
    // presentation-only replacement by a low enclosure; native SOL is retained.
    const auto nearDownStair=[&](Vec3 p){return std::any_of(CathedralArt.downStairs.begin(),CathedralArt.downStairs.end(),[&](const auto &st){return p.x>=16+std::floor((st.center.x-16)/2)*2+.5F-3.15F && p.x<=16+std::floor((st.center.x-16)/2)*2+.5F+3.15F && p.z>=st.center.z-2.65F && p.z<=st.center.z+1.65F;});};
    using Key=std::tuple<int,int,int>;std::map<Key,const cathedral::Instance*> walls;
    std::map<std::pair<int,int>,size_t> piers;
    std::vector<Vec3> nativeLights;
    for(int n=0;n<ActiveObjectCount;++n) {
        const int slot=ActiveObjects[n];const auto &o=Objects[slot];
        if(o._otype==OBJ_L1LIGHT && !o._oDelFlag && snapshot.At(o.position.x,o.position.y).piece==269)
            nativeLights.push_back({float(o.position.x),0,float(o.position.y)});
    }
    const auto nearLight=[&](Vec3 at){return std::any_of(nativeLights.begin(),nativeLights.end(),[&](Vec3 p){return std::abs(at.x-p.x)<=1.5F && std::abs(at.z-p.z)<=1.5F;});};
    std::vector<Vec3> upStairCenters;
    for(const auto &region:scene.regions)for(const auto &i:region->instances)if(i.module==cathedral::ModuleKind::StairsUp)
        upStairCenters.push_back({i.translation.x,i.translation.y,i.translation.z});
    std::vector<const cathedral::Instance*> unusedUpHalves;
    for(const auto &region:scene.regions)for(const auto &i:region->instances)if(CathedralUnusedUpHalf(snapshot,i))unusedUpHalves.push_back(&i);
    const auto add=[&](size_t part,Vec3 at,uint8_t turns,Point tile,uint64_t owner)->std::optional<size_t>{
        if(nearDownStair(at) || !CathedralArtContributorCurrent({tile,owner},currentContributors))return std::nullopt;
        // Meet the actual one-cell opening with the selected mesh's inner face.
        // Arcade endpoints stay on native anchors and remain inside the pier.
        // This translates only the two entry supports; no artwork is rescaled.
        if(part==6){
            const auto &r=CathedralArt.resources[part];
            const unsigned q=turns%4;
            const float localMin=PhysicalToNativeGrid(q==0?r.low.x:q==1?-r.high.z:q==2?-r.high.x:r.low.z);
            const float localMax=PhysicalToNativeGrid(q==0?r.high.x:q==1?-r.low.z:q==2?-r.low.x:r.high.z);
            for(const auto center:upStairCenters){
                if(std::abs(at.z-(center.z-.5F))>.0001F)continue;
                if(std::abs(at.x-(center.x-1.F))<.0001F)at.x=center.x-.5F-localMax;
                else if(std::abs(at.x-(center.x+1.F))<.0001F)at.x=center.x+.5F-localMin;
            }
        }
        if(CathedralArt.placements.size()>=512)throw std::runtime_error("Authored Cathedral placement budget");
        const size_t placement=CathedralArt.placements.size();
        CathedralArt.placements.push_back({part,at,turns,tile,owner,{{tile,owner}}});
        return placement;
    };
    const auto pier=[&](size_t part,Vec3 at,uint8_t turns,Point tile,uint64_t owner)->std::optional<size_t>{
        // Deduplicate on the same nominal anchors as R2. add() alone applies
        // the measured entry-support offset; every contributor shares that mesh.
        const auto key=std::pair{int(std::lround(at.x*10000)),int(std::lround(at.z*10000))};
        if(const auto found=piers.find(key);found!=piers.end()){
            if(found->second>=CathedralArt.placements.size())return std::nullopt;
            const auto actual=CathedralArt.placements[found->second].part;
            if(actual!=6 && actual!=8)return std::nullopt;
            return AssociateCathedralArtContributor(found->second,tile,owner,currentContributors)?std::optional<size_t>{found->second}:std::nullopt;
        }
        const auto placement=add(part,at,turns,tile,owner);
        if(placement)piers.emplace(key,*placement);
        return placement;
    };
    const auto nearDoor=[&](Vec3 at){return std::any_of(snapshot.doors.begin(),snapshot.doors.end(),[&](const auto &d){return std::abs(at.x-d.x)<1.8F && std::abs(at.z-d.z)<1.8F;});};
    struct Arcade { size_t placement; std::array<std::optional<size_t>,2> supports; };
    std::map<std::tuple<int,int,int>,Arcade> placedArches;
    const auto associateArcade=[&](const Arcade &arcade,const cathedral::Instance &source,const cathedral::Instance *secondary){
        if(arcade.placement>=CathedralArt.placements.size() || CathedralArt.placements[arcade.placement].part!=5)return false;
        std::vector<size_t> pieces{arcade.placement};
        for(const auto support:arcade.supports)if(support){
            if(*support>=CathedralArt.placements.size())return false;
            const auto part=CathedralArt.placements[*support].part;
            if(part!=6 && part!=8)return false;
            pieces.push_back(*support);
        }
        std::vector<CathedralArtContributor> incoming{{{source.pick.x,source.pick.z},source.id}};
        if(secondary!=nullptr)incoming.push_back({{secondary->pick.x,secondary->pick.z},secondary->id});
        return AssociateCathedralArtContributors(pieces,incoming,currentContributors);
    };
    const auto arch=[&](const cathedral::Instance &i,Vec3 at,const cathedral::Instance *secondary=nullptr){
        const auto profile=CathedralArt.thinWalls.find(i.id);
        if(profile!=CathedralArt.thinWalls.end()){const auto &w=profile->second;if(w.alongZ)at.x=(w.a.x+w.b.x)*.5F;else at.z=(w.a.z+w.b.z)*.5F;}
        if(nearDoor(at) || nearLight(at) || nearDownStair(at))return false;
        if(i.module==cathedral::ModuleKind::Wall)for(const auto *up:unusedUpHalves){
            const auto local=CathedralUpLocal(*up,at);
            if(std::abs(local.x+2.F)<.0001F && std::abs(local.z+.5F)<.0001F)return false;
        }
        const Point tile{i.pick.x,i.pick.z};
        if(!CathedralArtContributorCurrent({tile,i.id},currentContributors))return false;
        if(secondary!=nullptr && !CathedralArtContributorCurrent({{secondary->pick.x,secondary->pick.z},secondary->id},currentContributors))return false;
        const auto archKey=std::tuple{int(std::lround(at.x*10000)),int(std::lround(at.z*10000)),int(i.quarterTurns%2)};
        if(const auto found=placedArches.find(archKey);found!=placedArches.end())
            return associateArcade(found->second,i,secondary);
        std::array<Vec3,2> supportPoints;
        std::array<bool,2> required{};
        std::array<std::optional<size_t>,2> existing{};
        // Intentional light/descent omissions differ from a malformed existing
        // support. Validate every existing member BEFORE creating/binding any.
        for(int side:{-1,1}){
            const size_t n=side<0?0:1;
            auto &p=supportPoints[n];p=at;if(i.quarterTurns%2==0)p.x+=side;else p.z+=side;
            if(nearLight(p) || nearDownStair(p))continue;
            required[n]=true;
            const auto key=std::pair{int(std::lround(p.x*10000)),int(std::lround(p.z*10000))};
            if(const auto found=piers.find(key);found!=piers.end()){
                if(found->second>=CathedralArt.placements.size())return false;
                const auto &piece=CathedralArt.placements[found->second];
                if((piece.part!=6 && piece.part!=8) || !CathedralArtPlacementCurrent(piece,currentContributors))return false;
                existing[n]=found->second;
            }
        }
        const auto placement=add(5,at,i.quarterTurns,tile,i.id);
        if(!placement)return false;
        Arcade arcade{*placement,{}};
        for(size_t n=0;n<2;++n)if(required[n]){
            arcade.supports[n]=existing[n]?existing[n]:pier(6,supportPoints[n],i.quarterTurns,tile,i.id);
            if(!arcade.supports[n])return false;
        }
        if(!associateArcade(arcade,i,secondary))return false;
        placedArches.emplace(archKey,arcade);
        return true;
    };
    // Native SOL walls were two boundary shells. Merge bounded opposing
    // shells onto one visual centerline; collision and native coordinates stay.
    struct Node { float x=0,z=0;unsigned nx=0,nz=0;bool visible=false;uint8_t light=15; };
    std::map<std::pair<int,int>,Node> nodes;
    std::vector<const cathedral::Instance*> ends;
    const auto nodeKey=[](Vec3 p){return std::pair{int(std::lround(p.x*2)),int(std::lround(p.z*2))};};
    const auto solid=[&](int x,int z){return x>=16 && x<96 && z>=16 && z<96 && (snapshot.At(x,z).properties&cathedral::wall_composition::SolSolid)!=0;};
    for(const auto &region:scene.regions) {
        if(std::abs(region->x-CathedralFrame.regionX)>cathedral::RegionSize || std::abs(region->z-CathedralFrame.regionZ)>cathedral::RegionSize)continue;
        std::vector<const cathedral::Instance*> structural;
        for(const auto &i:region->instances)structural.push_back(&i);
        // Keep the original node when a socket split its lower wall into pieces.
        for(const auto &i:region->upperWallSources)if(i.module==cathedral::ModuleKind::Wall && std::none_of(region->instances.begin(),region->instances.end(),[&](const auto &v){return v.id==i.id;}))structural.push_back(&i);
        for(const auto *source:structural) {const auto &i=*source;
            if(i.module>=cathedral::ModuleKind::Corner && i.module<=cathedral::ModuleKind::Junction4){ends.push_back(&i);continue;}
            if(i.module!=cathedral::ModuleKind::Wall && i.module!=cathedral::ModuleKind::WallFragment && i.module!=cathedral::ModuleKind::JoinFragment)continue;
            const float dx=i.bounds.max.x-i.bounds.min.x,dz=i.bounds.max.z-i.bounds.min.z;const bool alongZ=dz>dx;const float half=(alongZ?dx:dz)*.5F;if(half<.0001F)continue;
            const Vec3 center{(i.bounds.min.x+i.bounds.max.x)*.5F,0,(i.bounds.min.z+i.bounds.max.z)*.5F};
            CathedralThinWall w{center,half,alongZ};w.oldLong=(alongZ?dz:dx)*.5F;w.pick={i.pick.x,i.pick.z};w.visible=visible(i);
            Vec3 target=center;const int sign=(alongZ?center.x-i.pick.x:center.z-i.pick.z)>0?1:-1;
            int width=0;while(width<5 && solid(i.pick.x+(alongZ?(width+1)*sign:0),i.pick.z+(alongZ?0:(width+1)*sign)))++width;
            // Supported paving follows every displaced wall, including wide exterior backing; width0 never shifts into VOID.
            const auto contact=CathedralBoundaryContact(width,nearDoor(center));
            if(alongZ)target.x+=sign*contact.offset;else target.z+=sign*contact.offset;
            if(w.visible)for(int n=1;n<=contact.pavingCells;++n)
                CathedralArt.wallPaving[{i.pick.x+(alongZ?n*sign:0),i.pick.z+(alongZ?0:n*sign)}]=snapshot.At(i.pick.x,i.pick.z).light;
            const Vec3 tangent=alongZ?Vec3{0,0,w.oldLong}:Vec3{w.oldLong,0,0};w.a=target-tangent;w.b=target+tangent;
            CathedralArt.thinWalls[i.id]=w;
            if(i.module==cathedral::ModuleKind::Wall)for(const auto point:{center-tangent,center+tangent}) {
                auto &node=nodes[nodeKey(point)];if(alongZ){node.x+=target.x;++node.nx;}else{node.z+=target.z;++node.nz;}node.visible=node.visible||w.visible;node.light=std::min(node.light,snapshot.At(i.pick.x,i.pick.z).light);
            }
        }
    }
    const auto remapNode=[&](Vec3 point){const auto found=nodes.find(nodeKey(point));if(found!=nodes.end()){const auto &node=found->second;if(node.nx)point.x=node.x/node.nx;if(node.nz)point.z=node.z/node.nz;}return point;};
    for(const auto &[key,node]:nodes)if(node.visible && node.nx && node.nz){
        const Vec3 original{key.first*.5F,0,key.second*.5F};
        if(!nearDoor(original) && !nearDownStair(original))CathedralPaveRemappedCorner(CathedralArt.wallPaving,original,remapNode(original),node.light,solid);
    }
    std::map<std::array<int,4>,std::vector<uint64_t>> groups;
    for(auto &[id,w]:CathedralArt.thinWalls) {
        const Vec3 tangent=w.alongZ?Vec3{0,0,w.oldLong}:Vec3{w.oldLong,0,0};
        w.a=remapNode(w.center-tangent);w.b=remapNode(w.center+tangent);
        for(const auto *up:unusedUpHalves){
            if(w.pick.x!=up->pick.x || w.pick.y!=up->pick.z)continue;
            const auto local=CathedralUpLocal(*up,w.center);
            if(std::abs(local.x+1.F)<.0001F && std::abs(local.z+1.F)<.0001F){
                w.a=CathedralUpWorld(*up,{-2,0,0});w.b=CathedralUpWorld(*up,{-.5F,0,0});
            }else if(std::abs(local.x+1.5F)<.0001F && std::abs(local.z+.5F)<.0001F){
                CathedralArt.replaced.insert(id);w.a=w.b=CathedralUpWorld(*up,{-2,0,0});
            }
        }
        const Vec3 delta=w.b-w.a;if(Dot(delta,delta)<.00001F){CathedralArt.replaced.insert(id);continue;}
        {std::array<int,2>a{{int(std::lround(w.a.x*10000)),int(std::lround(w.a.z*10000))}},b{{int(std::lround(w.b.x*10000)),int(std::lround(w.b.z*10000))}};if(b<a)std::swap(a,b);groups[{{a[0],a[1],b[0],b[1]}}].push_back(id);}
    }
    for(const auto &[key,ids]:groups)if(ids.size()>1) {
        const auto first=std::find_if(ids.begin(),ids.end(),[&](uint64_t id){return CathedralArt.thinWalls.at(id).visible;});
        const auto count=std::count_if(ids.begin(),ids.end(),[&](uint64_t id){return CathedralArt.thinWalls.at(id).visible;});
        for(const auto id:ids){auto &w=CathedralArt.thinWalls.at(id);w.paired=count>1;w.capOwner=first!=ids.end() && id==*first;++CathedralArt.mergedWalls;}
    }
    // SOL graph joins are presentation seams, never semantic isolated columns.
    // Their square technical blocks are replaced by the straight wall support.
    // Real raw15 megas are reserved first so a wall/arch cannot duplicate them.
    for(const auto &region:scene.regions)for(const auto &i:region->instances) {
        if(i.module!=cathedral::ModuleKind::Pillar || !visible(i))continue;
        const Vec3 at{i.translation.x,i.translation.y,i.translation.z};
        if(!nearLight(at))if(pier(8,at,0,{i.pick.x,i.pick.z},i.id))CathedralArt.replaced.insert(i.id);
    }
    for(const auto *up:unusedUpHalves)if(visible(*up))
        pier(6,CathedralUpWorld(*up,{-1,0,-.5F}),up->quarterTurns,{up->pick.x,up->pick.z},up->id);
    for(const auto *i:ends)if(visible(*i)) {
        bool idle=false;
        for(const auto *up:unusedUpHalves){const auto local=CathedralUpLocal(*up,{i->translation.x,i->translation.y,i->translation.z});
            if(i->nativeMega==64 && std::abs(local.x+1.5F)<.0001F && local.z>=-1.0001F && local.z<=.0001F){idle=true;break;}}
        if(idle)continue;
        const Vec3 at=remapNode({i->translation.x,i->translation.y,i->translation.z});
        // The SOL contour surrounding a column or a brazier does not create
        // additional authored piers. Actual masonry corners and junctions use
        // the wall-mounted straight head, never the isolated flared crown.
        const bool objectContour=std::any_of(scene.regions.begin(),scene.regions.end(),[&](const auto &r){
            return std::any_of(r->instances.begin(),r->instances.end(),[&](const auto &v){return (v.module==cathedral::ModuleKind::Pillar || v.module==cathedral::ModuleKind::Fence) && std::abs(at.x-v.translation.x)<=1.5F && std::abs(at.z-v.translation.z)<=1.5F;});
        });
        if(!objectContour && !nearLight(at))
            if(pier(6,at,i->quarterTurns,{i->pick.x,i->pick.z},i->id))CathedralArt.replaced.insert(i->id);
    }
    for(const auto &region:scene.regions) {
        if(std::abs(region->x-CathedralFrame.regionX)>cathedral::RegionSize || std::abs(region->z-CathedralFrame.regionZ)>cathedral::RegionSize)continue;
        for(const auto &i:region->instances) {
            const Vec3 position{i.translation.x,i.translation.y,i.translation.z};
            if(i.module==cathedral::ModuleKind::Wall && CathedralArt.replaced.count(i.id)==0)walls[{int(std::lround(i.translation.x*2)),int(std::lround(i.translation.z*2)),i.quarterTurns%2}]=&i;
            if(!visible(i))continue;
            if(i.module==cathedral::ModuleKind::Pillar || i.module==cathedral::ModuleKind::Fence)CathedralArt.nativeObjectFootprints.push_back(position);
            if(i.module==cathedral::ModuleKind::Pillar){/* reserved once above at the exact mega center */}
            else if(i.module==cathedral::ModuleKind::Fence){if(add(7,position,i.quarterTurns,{i.pick.x,i.pick.z},i.id))CathedralArt.replaced.insert(i.id);arch(i,position);}
            else if(i.module==cathedral::ModuleKind::ArchFrame){if(arch(i,position)){CathedralArt.replaced.insert(i.id);for(int z=i.pick.z;z<i.pick.z+2;++z)for(int x=i.pick.x;x<i.pick.x+2;++x)CathedralArt.replacedSpecialCells.insert({x,z});}}
        }
    }
    for(const auto &[key,i]:walls) {
        const bool alongZ=i->quarterTurns%2!=0;const int along=std::get<0>(key)*(alongZ?0:1)+std::get<1>(key)*(alongZ?1:0);
        if(along%4!=0)continue;auto next=key;if(alongZ)std::get<1>(next)+=2;else std::get<0>(next)+=2;
        const auto neighbor=walls.find(next);if(neighbor==walls.end() || (!visible(*i)&&!visible(*neighbor->second)))continue;
        Vec3 at{i->translation.x,i->translation.y,i->translation.z};// Snap the two-cell arcade to integer native support anchors.
        if(alongZ)at.z+=1.0F;else at.x+=1.0F;
        if(std::any_of(CathedralArt.nativeObjectFootprints.begin(),CathedralArt.nativeObjectFootprints.end(),[&](Vec3 p){return std::abs(at.x-p.x)<=1.4F && std::abs(at.z-p.z)<=1.4F;}))continue;
        const auto &primary=visible(*i)?*i:*neighbor->second;
        const auto *secondary=visible(*i)?neighbor->second:i;
        // One synthesized arcade spans both native wall segments. Associate the
        // second current visible source without recomputing its centerline.
        arch(primary,at,visible(*secondary)?secondary:nullptr);
    }
    for(const auto &st:CathedralArt.downStairs)if(st.visible){
        const int bx=16+int(std::floor((st.center.x-16)/2))*2,bz=int(st.center.z);
        for(int z=bz-2;z<=bz+1;++z)for(int x=bx-2;x<=bx+3;++x){
            if(x==int(st.center.x) && z>=bz && z<=bz+1){CathedralArt.wallPaving.erase({x,z});continue;}
            if((snapshot.At(x,z).flags&2)!=0)CathedralArt.wallPaving[{x,z}]=snapshot.At(x,z).light;
        }
    }
    if(!CathedralArtPlacementsCurrent(currentContributors))
        throw std::runtime_error("Authored Cathedral placement has stale, duplicate or noncanonical contributors");
    CathedralArt.epoch=cathedral::LiveEpoch();CathedralArt.geometry=CathedralFrame.geometryRevision;
}
bool CathedralArtHidden(uint64_t owner)
{
    if(CollectingCathedralFireCasters)return false;
    if(!FollowThroughTownArchitecture() || CameraRig.mode()!=TownCameraMode::ThirdPerson || CathedralVisibilityGeometry!=CathedralFrame.geometryRevision)return false;
    const auto it=CathedralVisibilityOwners.find(owner);return it!=CathedralVisibilityOwners.end() && it->second<CathedralHiddenOwners.size() && CathedralHiddenOwners[it->second]!=0;
}
template<class HiddenPredicate>
bool CathedralArtAllContributorsHidden(const CathedralArtPlacement &placement,const HiddenPredicate &hidden)
{
    if(placement.contributors.empty())return true;
    return std::all_of(placement.contributors.begin(),placement.contributors.end(),[&](const auto &c){return hidden(c.owner);});
}
bool DrawCathedralArtSurface(const Surface &out,const cathedral::FrameTriangle &surface);
void DrawCathedralArtModels(const Surface &out)
{
    CathedralArt.modelDraws=0;CathedralArt.objectFloorCells=0;CathedralArt.partDraws={};
    if(!CathedralArtFrameReady())return;
    // Solid native object megas have no walkable Floor instances. They still
    // need visible paving under their smaller authored base. Fill presentation
    // only, per currently visible native subcell; SOL and picks remain native.
    for(const auto &[cell,light]:CathedralArt.wallPaving) {
        const int x=cell.first,z=cell.second;
        if(std::any_of(CathedralArt.nativeObjectFootprints.begin(),CathedralArt.nativeObjectFootprints.end(),[&](Vec3 p){return x>=int(std::lround(p.x-.5F)) && x<=int(std::lround(p.x+.5F)) && z>=int(std::lround(p.z-.5F)) && z<=int(std::lround(p.z+.5F));}))continue;
        const std::array<cathedral::Vec3,4> corners{{{x-.5F,0,z-.5F},{x+.5F,0,z-.5F},{x+.5F,0,z+.5F},{x-.5F,0,z+.5F}}};
        for(const auto indices:{std::array<size_t,3>{{0,1,2}},std::array<size_t,3>{{0,2,3}}}){cathedral::FrameTriangle triangle{};triangle.module=cathedral::ModuleKind::Floor;triangle.pick={cathedral::PickKind::Ground,x,z,-1};triangle.light=light;for(size_t k=0;k<3;++k)triangle.vertices[k]={corners[indices[k]],0,0};DrawCathedralArtSurface(out,triangle);}
    }
    if(const auto *snapshot=cathedral::LiveSnapshot())for(const auto p:CathedralArt.nativeObjectFootprints) {
        const int bx=int(std::lround(p.x-.5F)),bz=int(std::lround(p.z-.5F));
        uint8_t floorLight=15;
        for(int oz=-1;oz<=2;++oz)for(int ox=-1;ox<=2;++ox) {
            const int x=bx+ox,z=bz+oz;if(x<16 || x>=96 || z<16 || z>=96)continue;
            if((snapshot->At(x,z).flags&2)!=0)floorLight=std::min(floorLight,snapshot->At(x,z).light);
        }
        for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx) {
            const int x=bx+dx,z=bz+dz;if((snapshot->At(x,z).flags&2)==0)continue;
            ++CathedralArt.objectFloorCells;
            const std::array<cathedral::Vec3,4> corners{{{x-.5F,0,z-.5F},{x+.5F,0,z-.5F},{x+.5F,0,z+.5F},{x-.5F,0,z+.5F}}};
            for(const auto indices:{std::array<size_t,3>{{0,1,2}},std::array<size_t,3>{{0,2,3}}}) {
                cathedral::FrameTriangle triangle{};triangle.module=cathedral::ModuleKind::Floor;triangle.pick={cathedral::PickKind::Ground,x,z,-1};triangle.light=floorLight;
                for(size_t k=0;k<3;++k)triangle.vertices[k]={corners[indices[k]],0,0};
                DrawCathedralArtSurface(out,triangle);
            }
        }
    }
    // Reuse only the selected R19 cylindrical marble faces, uniformly at70%.
    // Keep their exact atlas UVs; bases/caps belong to the closed stair module.
    for(const auto &st:CathedralArt.downStairs)if(st.visible && CathedralTileInFrame(st.tile)){
        const auto &resource=CathedralArt.resources[6];
        for(const auto &triangle:resource.model.triangles){
            bool shaft=true;for(const auto &v:triangle.vertices){const float radius=std::sqrt(v.x*v.x+v.z*v.z);shaft=shaft && radius>.305F && radius<.315F && v.height>=.17F && v.height<=1.94F;}
            if(!shaft)continue;
            for(float side:{-1.F,1.F})for(float end:{-1.3F,1.3F}){std::array<Vertex,3> vertices;
                for(size_t k=0;k<3;++k){const auto &v=triangle.vertices[k];const float scale=PhysicalToNativeGrid(.7F);vertices[k]={{st.center.x+side*1.3F+v.x*scale,v.height*scale,st.center.z+end+v.z*scale},v.u,v.v};}
                DrawTriangle(out,vertices,ImportedTextureCache.at(CathedralArtTextureKey-6),PickAt(st.tile),st.light);
            }
        }
    }
    const float scale=PhysicalToNativeGrid(1);
    for(const auto &p:CathedralArt.placements) {
        if(CathedralArtAllContributorsHidden(p,[](uint64_t owner){return CathedralArtHidden(owner);}))continue;
        auto &r=CathedralArt.resources[p.part];const float angle=p.turns*1.570796327F,cs=std::cos(angle),sn=std::sin(angle);
        const auto world=[&](Vec3 v){return p.origin+Vec3{(cs*v.x-sn*v.z)*scale,v.y*scale,(sn*v.x+cs*v.z)*scale};};
        const float inf=std::numeric_limits<float>::infinity();TownArchitectureBounds bounds{{inf,inf,inf},{-inf,-inf,-inf},true};
        for(float x:{r.low.x,r.high.x})for(float y:{r.low.y,r.high.y})for(float z:{r.low.z,r.high.z}) {
            const auto v=world({x,y,z});bounds.minimum.x=std::min(bounds.minimum.x,v.x);bounds.minimum.height=std::min(bounds.minimum.height,v.y);bounds.minimum.z=std::min(bounds.minimum.z,v.z);
            bounds.maximum.x=std::max(bounds.maximum.x,v.x);bounds.maximum.height=std::max(bounds.maximum.height,v.y);bounds.maximum.z=std::max(bounds.maximum.z,v.z);
        }
        if(!CollectingCathedralFireCasters && ViewCamera.projection.perspective && !IsTownArchitectureBoundsVisible(ViewCamera.projection,bounds))continue;
        const auto &texture=ImportedTextureCache.at(CathedralArtTextureKey-p.part);const auto pick=PickAt(p.tile);const int shade=dLight[p.tile.x][p.tile.y];
        if(!CollectingCathedralFireCasters && CaptureGpu && ResidentMeshesEnabledForDiagnostics && ViewCamera.projection.perspective) {
            if(!PrepareResidentMesh(r.resident))return;TownGpuMeshInstance instance;instance.identity=r.resident.identity();instance.pickId=GpuPickId(pick);
            instance.localToWorld={{{cs*scale,0,-sn*scale,p.origin.x},{0,scale,0,p.origin.y},{sn*scale,0,cs*scale,p.origin.z}}};TownGpuMaterial material;material.lighting=TownGpuLighting::Palette;material.shade=shade;
            instance.vertexNormals=ReceivesCathedralFire(texture);material.receivesFirePointLight=ReceivesCathedralFire(texture);
            if(!TownGpuSubmitMeshInstance(instance,PrepareGpuTexture(texture,nullptr),material)){CaptureGpuFailed=true;return;}
        } else for(const auto &t:r.model.triangles) {
            std::array<Vertex,3> vertices;for(size_t k=0;k<3;++k){const auto &v=t.vertices[k];vertices[k]={world({v.x,v.height,v.z}),v.u,v.v};}
            DrawTriangle(out,vertices,texture,pick,shade);
        }
        ++CathedralArt.modelDraws;++CathedralArt.partDraws[p.part];
    }
}
bool DrawCathedralArtSurface(const Surface &out,const cathedral::FrameTriangle &surface)
{
    if(!CathedralArtFrameReady())return false;
    if(CathedralArt.replaced.count(surface.instanceId)!=0)return true;
    const auto module=surface.module;
    if(module>=cathedral::ModuleKind::Wall && module<=cathedral::ModuleKind::Junction4 || module==cathedral::ModuleKind::WallFragment || module==cathedral::ModuleKind::JoinFragment){
        for(const auto &st:CathedralArt.downStairs){bool inside=true;for(const auto &v:surface.vertices)inside=inside && v.position.x>=16+std::floor((st.center.x-16)/2)*2+.5F-3.15F && v.position.x<=16+std::floor((st.center.x-16)/2)*2+.5F+3.15F && v.position.z>=st.center.z-2.65F && v.position.z<=st.center.z+1.65F;if(inside)return true;}
    }
    if(module==cathedral::ModuleKind::StairsUp || module==cathedral::ModuleKind::StairsDown) {
        std::array<Vertex,3> vertices;
        for(size_t k=0;k<3;++k){const auto p=surface.vertices[k].position;vertices[k]={{p.x,p.y,p.z},0,0};}
        const auto n=Cross(vertices[1].position-vertices[0].position,vertices[2].position-vertices[0].position);
        const bool cap=std::abs(n.y)>std::max(std::abs(n.x),std::abs(n.z));
        for(auto &v:vertices){const auto p=v.position;v.u=(cap?p.x:(std::abs(n.x)>std::abs(n.z)?p.z:p.x))*WorldMetresPerTile/.92F;v.v=(cap?p.z:p.y)*WorldMetresPerTile/.28F;}
        DrawTriangle(out,vertices,ImportedTextureCache.at(CathedralArtTextureKey-4),PickAt({surface.pick.x,surface.pick.z}),surface.light);
        return true;
    }
    if((module>=cathedral::ModuleKind::Wall && module<=cathedral::ModuleKind::Junction4) || module==cathedral::ModuleKind::WallFragment || module==cathedral::ModuleKind::JoinFragment) {
        // The native SOL boundary graph also boxes in solid pillar/grille
        // megas. Replace those presentation boxes, never their native SOL or
        // conservative camera collision, when the authored object owns them.
        for(const auto p:CathedralArt.nativeObjectFootprints) {
            bool inside=true;
            for(const auto &v:surface.vertices)inside=inside && std::abs(v.position.x-p.x)<=1.2F && std::abs(v.position.z-p.z)<=1.2F;
            if(inside)return true;
        }
    }
    if(module==cathedral::ModuleKind::Floor) {
        DrawCathedralFloorCeiling(out, surface);
        for(const auto &st:CathedralArt.downStairs){bool inside=true;for(const auto &v:surface.vertices)inside=inside && v.position.x>=st.center.x-.50001F && v.position.x<=st.center.x+.50001F && v.position.z>=st.center.z-.50001F && v.position.z<=st.center.z+1.50001F;if(inside)return true;}

        // Split along authored texture tile edges; neighboring native cells use
        // the same stable material and UV phase. No native coordinates change.
        constexpr float period=2.4F;const float step=period/WorldMetresPerTile;
        const auto &input=surface.vertices;float minX=input[0].position.x,maxX=minX,minZ=input[0].position.z,maxZ=minZ;
        for(const auto &v:input){minX=std::min(minX,v.position.x);maxX=std::max(maxX,v.position.x);minZ=std::min(minZ,v.position.z);maxZ=std::max(maxZ,v.position.z);}
        for(int z=int(std::floor(minZ/step));z<=int(std::floor(maxZ/step));++z)for(int x=int(std::floor(minX/step));x<=int(std::floor(maxX/step));++x) {
            std::vector<Vec3> polygon;for(const auto &v:input)polygon.push_back({v.position.x,v.position.y,v.position.z});
            for(int plane=0;plane<4 && !polygon.empty();++plane) {
                const bool axis=plane>=2;const float boundary=((plane%2==0)?(axis?z:x):(axis?z+1:x+1))*step;
                const auto distance=[&](Vec3 v){const float q=(axis?v.z:v.x)-boundary;return plane%2==0?q:-q;};
                std::vector<Vec3> clipped;auto a=polygon.back();float da=distance(a);
                for(const auto b:polygon){const float db=distance(b);if((da>=0)!=(db>=0)){const float t=da/(da-db);clipped.push_back(a+(b-a)*t);}if(db>=0)clipped.push_back(b);a=b;da=db;}
                polygon=std::move(clipped);
            }
            if(polygon.size()<3)continue;const uint32_t hash=uint32_t(x)*73856093U^uint32_t(z)*19349663U;const auto &texture=ImportedTextureCache.at(CathedralArtTextureKey-(hash%4));
            for(size_t k=1;k+1<polygon.size();++k){std::array<Vertex,3> vertices;const std::array<size_t,3> indices{{0,k,k+1}};
                for(size_t j=0;j<3;++j){auto v=polygon[indices[j]];vertices[j]={v,v.x/step,v.z/step};}DrawTriangle(out,vertices,texture,PickAt({surface.pick.x,surface.pick.z}),surface.light);++CathedralArt.floorDraws;}
        }
        return true;
    }
    if((module>=cathedral::ModuleKind::Wall && module<=cathedral::ModuleKind::Junction4) || module==cathedral::ModuleKind::WallFragment || module==cathedral::ModuleKind::JoinFragment) {
        const auto profile=CathedralArt.thinWalls.find(surface.instanceId);
        if(surface.nativeTexture.kind!=cathedral::NativeTextureKind::Masonry && profile==CathedralArt.thinWalls.end())return false;
        if(profile!=CathedralArt.thinWalls.end() && profile->second.paired) {
            const auto &w=profile->second;std::array<Vec3,3> p;for(size_t k=0;k<3;++k)p[k]={surface.vertices[k].position.x,surface.vertices[k].position.y,surface.vertices[k].position.z};
            const Vec3 normal=Cross(p[1]-p[0],p[2]-p[0]);const float facing=Dot(normal,Vec3{float(w.pick.x)-w.center.x,0,float(w.pick.y)-w.center.z});
            if(facing<-.000001F || (std::abs(facing)<.000001F && !w.capOwner))return true;
        }
        std::array<Vertex,3> vertices;
        for(size_t k=0;k<3;++k) {
            const auto &source=surface.vertices[k];Vec3 v{source.position.x,source.position.y,source.position.z};
            if(profile!=CathedralArt.thinWalls.end()) {
                v=CathedralWallPoint(profile->second,v);
            }
            vertices[k]={v,0,0};
        }
        const Vec3 face=Cross(vertices[1].position-vertices[0].position,vertices[2].position-vertices[0].position);
        const bool horizontal=std::abs(face.y)>std::max(std::abs(face.x),std::abs(face.z));const bool alongZ=std::abs(face.x)>std::abs(face.z);
        for(auto &vertex:vertices){const auto v=vertex.position;vertex.u=(horizontal?v.x:(alongZ?v.z:v.x))*WorldMetresPerTile/.92F;vertex.v=(horizontal?v.z:v.y)*WorldMetresPerTile/.28F;}
        DrawTriangle(out,vertices,ImportedTextureCache.at(CathedralArtTextureKey-4),PickAt({surface.pick.x,surface.pick.z}),surface.light);
        if(profile!=CathedralArt.thinWalls.end())++CathedralArt.thinWallDraws;
        ++CathedralArt.wallDraws;return true;
    }
    return false;
}
