#include "engine/render/actor_model.hpp"
#include "engine/render/actor_pose.hpp"
#include "engine/render/actor_visual_snapshot.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

using namespace devilution;
namespace {
template<class T> bool Read(std::istream &f,T &v){return static_cast<bool>(f.read(reinterpret_cast<char *>(&v),sizeof(v)));}
bool Text(std::istream &f,std::string &s){std::uint32_t n=0;if(!Read(f,n)||n>1024)return false;s.resize(n);return static_cast<bool>(f.read(s.data(),n));}
bool Same(float a,float b){return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);}
bool Same(ActorVec2 a,ActorVec2 b){return Same(a.x,b.x)&&Same(a.y,b.y);}
bool Same(ActorVec3 a,ActorVec3 b){return Same(a.x,b.x)&&Same(a.y,b.y)&&Same(a.z,b.z);}
bool Same(ActorVec4 a,ActorVec4 b){return Same(a.x,b.x)&&Same(a.y,b.y)&&Same(a.z,b.z)&&Same(a.w,b.w);}
template<std::size_t N> bool Same(const std::array<float,N> &a,const std::array<float,N> &b){
    for(std::size_t i=0;i<N;++i)if(!Same(a[i],b[i]))return false;return true;
}
bool SameMatrices(const std::vector<ActorMatrix> &a,const std::vector<ActorMatrix> &b){
    if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(!Same(a[i],b[i]))return false;return true;
}
bool SamePose(const ActorPose &a,const ActorPose &b){
    if(!SameMatrices(a.worldNodes,b.worldNodes)||!SameMatrices(a.jointPalette,b.jointPalette)||a.positions.size()!=b.positions.size()||a.normals.size()!=b.normals.size())return false;
    for(std::size_t i=0;i<a.positions.size();++i)if(!Same(a.positions[i],b.positions[i]))return false;
    for(std::size_t i=0;i<a.normals.size();++i)if(!Same(a.normals[i],b.normals[i]))return false;
    return true;
}
bool SameModel(const ActorModel &a,const ActorModel &b){
    if(a.assetId!=b.assetId||a.variant!=b.variant||a.revision!=b.revision||a.sourceSha256!=b.sourceSha256
        ||a.nodes.size()!=b.nodes.size()||a.joints.size()!=b.joints.size()||a.vertices.size()!=b.vertices.size()||a.clips.size()!=b.clips.size()
        ||a.indices!=b.indices||a.textureWidth!=b.textureWidth||a.textureHeight!=b.textureHeight||a.textureRgb!=b.textureRgb)return false;
    for(std::size_t i=0;i<a.nodes.size();++i){const auto &x=a.nodes[i];const auto &y=b.nodes[i];
        if(x.parent!=y.parent||x.name!=y.name||!Same(x.rest.translation,y.rest.translation)||!Same(x.rest.rotation,y.rest.rotation)||!Same(x.rest.scale,y.rest.scale))return false;}
    for(std::size_t i=0;i<a.joints.size();++i)if(a.joints[i].node!=b.joints[i].node||!Same(a.joints[i].inverseBind,b.joints[i].inverseBind))return false;
    for(std::size_t i=0;i<a.vertices.size();++i){const auto &x=a.vertices[i];const auto &y=b.vertices[i];
        if(!Same(x.position,y.position)||!Same(x.normal,y.normal)||!Same(x.uv,y.uv)||x.joints!=y.joints||!Same(x.weights,y.weights))return false;}
    for(std::size_t i=0;i<a.clips.size();++i){const auto &x=a.clips[i];const auto &y=b.clips[i];
        if(x.name!=y.name||!Same(x.startTime,y.startTime)||!Same(x.endTime,y.endTime)||x.tracks.size()!=y.tracks.size())return false;
        for(std::size_t j=0;j<x.tracks.size();++j){const auto &u=x.tracks[j];const auto &v=y.tracks[j];
            if(u.node!=v.node||u.path!=v.path||u.interpolation!=v.interpolation||u.times.size()!=v.times.size()||u.values.size()!=v.values.size())return false;
            for(std::size_t k=0;k<u.times.size();++k)if(!Same(u.times[k],v.times[k]))return false;
            for(std::size_t k=0;k<u.values.size();++k)if(!Same(u.values[k],v.values[k]))return false;}}
    return true;
}
double Delta(const std::vector<ActorVec3> &a,const std::vector<ActorVec3> &b){
    if(a.size()!=b.size())return std::numeric_limits<double>::infinity();double result=0;
    for(std::size_t i=0;i<a.size();++i)result=std::max(result,static_cast<double>(std::max({std::abs(a[i].x-b[i].x),std::abs(a[i].y-b[i].y),std::abs(a[i].z-b[i].z)})));
    return result;
}
double MatrixDelta(const std::vector<ActorMatrix> &a,const std::vector<ActorMatrix> &b){
    if(a.size()!=b.size())return std::numeric_limits<double>::infinity();double result=0;
    for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<16;++j)result=std::max(result,static_cast<double>(std::abs(a[i][j]-b[i][j])));
    return result;
}
struct Hash {
    std::uint64_t value=1469598103934665603ULL;
    void Add(std::uint64_t n){for(unsigned i=0;i<8;++i){value^=static_cast<std::uint8_t>(n);value*=1099511628211ULL;n>>=8;}}
    void Add(float n){Add(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(n)));}
    void Add(const std::string &s){Add(static_cast<std::uint64_t>(s.size()));for(unsigned char c:s)Add(static_cast<std::uint64_t>(c));}
    void Add(ActorVec2 v){Add(v.x);Add(v.y);}void Add(ActorVec3 v){Add(v.x);Add(v.y);Add(v.z);}void Add(ActorVec4 v){Add(v.x);Add(v.y);Add(v.z);Add(v.w);}
};
std::uint64_t Fingerprint(const ActorModel &m){
    Hash h;h.Add(m.assetId);h.Add(m.variant);h.Add(m.revision);h.Add(m.sourceSha256);
    for(const auto &n:m.nodes){h.Add(static_cast<std::uint64_t>(n.parent));h.Add(n.name);h.Add(n.rest.translation);h.Add(n.rest.rotation);h.Add(n.rest.scale);}
    for(const auto &j:m.joints){h.Add(static_cast<std::uint64_t>(j.node));for(float v:j.inverseBind)h.Add(v);}
    for(const auto &v:m.vertices){h.Add(v.position);h.Add(v.normal);h.Add(v.uv);for(auto j:v.joints)h.Add(static_cast<std::uint64_t>(j));for(auto w:v.weights)h.Add(w);}
    for(auto i:m.indices)h.Add(static_cast<std::uint64_t>(i));
    for(const auto &c:m.clips){h.Add(c.name);h.Add(c.startTime);h.Add(c.endTime);for(const auto &t:c.tracks){h.Add(static_cast<std::uint64_t>(t.node));h.Add(static_cast<std::uint64_t>(t.path));h.Add(static_cast<std::uint64_t>(t.interpolation));for(auto v:t.times)h.Add(v);for(auto v:t.values)for(float x:v)h.Add(x);}}
    h.Add(static_cast<std::uint64_t>(m.textureWidth));h.Add(static_cast<std::uint64_t>(m.textureHeight));for(auto c:m.textureRgb)h.Add(static_cast<std::uint64_t>(c));return h.value;
}
bool StaticSourceMatches(const ActorModel &m,std::istream &f){
    char magic[8];if(!f.read(magic,8)||std::string_view(magic,8)!="D3DBREF1")return false;
    std::string source;if(!Text(f,source)||source!=m.sourceSha256)return false;
    std::uint32_t nodes=0,joints=0,vertices=0,indices=0,tw=0,th=0;
    if(!Read(f,nodes)||!Read(f,joints)||!Read(f,vertices)||!Read(f,indices)||!Read(f,tw)||!Read(f,th)
        ||nodes!=m.nodes.size()||joints!=m.joints.size()||vertices!=m.vertices.size()||indices!=m.indices.size()||tw!=m.textureWidth||th!=m.textureHeight)return false;
    for(const auto &n:m.nodes){std::int32_t parent=0;std::string name;ActorTransform t;
        if(!Read(f,parent)||!Text(f,name)||!Read(f,t.translation)||!Read(f,t.rotation)||!Read(f,t.scale)||parent!=n.parent||name!=n.name
            ||!Same(t.translation,n.rest.translation)||!Same(t.scale,n.rest.scale)||!Same(t.rotation,n.rest.rotation))return false;}
    for(const auto &j:m.joints){std::uint32_t node=0;ActorMatrix ibm;if(!Read(f,node)||!Read(f,ibm)||node!=j.node||!Same(ibm,j.inverseBind))return false;}
    for(const auto &v:m.vertices){ActorSkinVertex expected;if(!Read(f,expected.position)||!Read(f,expected.normal)||!Read(f,expected.uv)||!Read(f,expected.joints)||!Read(f,expected.weights)
        ||!Same(expected.position,v.position)||!Same(expected.normal,v.normal)||!Same(expected.uv,v.uv)||expected.joints!=v.joints||!Same(expected.weights,v.weights))return false;}
    for(auto index:m.indices){std::uint32_t expected=0;if(!Read(f,expected)||expected!=index)return false;}
    std::vector<std::uint8_t> texture(m.textureRgb.size());if(!f.read(reinterpret_cast<char *>(texture.data()),texture.size())||texture!=m.textureRgb)return false;
    char trailing=0;return !f.read(&trailing,1);
}
bool FinitePose(const ActorPose &p,const ActorModel &model){
    if(p.worldNodes.size()!=model.nodes.size()||p.jointPalette.size()!=model.joints.size()||p.positions.size()!=model.vertices.size()||p.normals.size()!=model.vertices.size())return false;
    for(const auto &m:p.worldNodes)for(float v:m)if(!std::isfinite(v))return false;
    for(const auto &m:p.jointPalette)for(float v:m)if(!std::isfinite(v))return false;
    for(const auto &v:p.positions)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return false;
    for(const auto &v:p.normals)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||v.x*v.x+v.y*v.y+v.z*v.z<=0)return false;
    return true;
}
}

int main(int argc,char **argv){
    if(argc!=3)return 2;
    std::ifstream file(argv[1],std::ios::binary),reference(argv[2],std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});ActorModel model;std::string error;
    if(!LoadActorModel(bytes,model,error)){std::fprintf(stderr,"%s\n",error.c_str());return 3;}
    constexpr const char *names[]={"death","block","cast-fire","cast-lightning","cast-magic","idle","walk","attack","hit","walk-entry","walk-exit"};
    if(model.clips.size()!=11||model.joints.size()!=52||model.nodes.size()!=68||model.vertices.size()!=13627||model.textureWidth!=2048||model.textureHeight!=2048
        ||model.assetId!="tristram.hero.warrior"||model.variant!="warrior-light-start-swordshield-r1"||model.revision!="warrior-composed-body-r1"||!StaticSourceMatches(model,reference))return 4;
    const auto before=Fingerprint(model);const ActorModel frozen=model;ActorPose pose;std::size_t total=0,directionChecks=0;bool spatial=true;
    std::printf("{\"clips\":[");
    for(std::size_t clip=0;clip<model.clips.size();++clip){
        const auto &animation=model.clips[clip];if(animation.name!=names[clip])return 5;
        std::vector<float> times;for(unsigned i=0;i<=1024;++i)times.push_back(animation.startTime+(animation.endTime-animation.startTime)*i/1024);
        std::size_t sourceKeys=0;for(const auto &t:animation.tracks){sourceKeys+=t.times.size();times.insert(times.end(),t.times.begin(),t.times.end());}
        std::sort(times.begin(),times.end());times.erase(std::unique(times.begin(),times.end()),times.end());
        float floor=std::numeric_limits<float>::max();double motion=0,seam=0;ActorPose first;
        for(float t:times){
            if(!EvaluateActorPose(model,clip,t,pose,error)||!FinitePose(pose,model))return 6;
            if(t==times.front())first=pose;
            motion=std::max(motion,Delta(pose.positions,first.positions));if(t==times.back())seam=Delta(pose.positions,first.positions);
            for(const auto &p:pose.positions)floor=std::min(floor,p.y);
            // Asset-space evaluator has no native facing input. Explicit eight
            // yaw rotations check finite placement invariance, not dispatcher,
            // native capture, geometry-fit or renderer admission.
            for(unsigned direction=0;direction<8;++direction){
                const double yaw=ActorNativeDirectionYaw(static_cast<std::uint8_t>(direction)),co=std::cos(yaw),si=std::sin(yaw);
                for(const auto &p:pose.positions){const double x=p.x*co+p.z*si,z=-p.x*si+p.z*co;if(!std::isfinite(x)||!std::isfinite(z))return 7;++directionChecks;}
            }
        }
        total+=times.size();spatial=spatial&&floor>=-.002F&&motion>.001;
        std::printf("%s{\"index\":%zu,\"name\":\"%s\",\"regularSamples\":1025,\"allUniqueTimes\":%zu,\"sourceKeyRecords\":%zu,\"floor\":%.12g,\"motionMaximumComponent\":%.12g,\"startEndMaximumComponent\":%.12g}",clip?",":"",clip,names[clip],times.size(),sourceKeys,floor,motion,seam);
    }
    std::printf("],\"transitionEndpoints\":[");
    constexpr std::array<std::array<unsigned,4>,4> pairs={{{9,0,5,0},{9,1,6,1},{10,0,6,1},{10,1,5,0}}};bool endpoints=true;
    for(std::size_t i=0;i<pairs.size();++i){const auto p=pairs[i];ActorPose a,b;
        const auto &ac=model.clips[p[0]];const auto &bc=model.clips[p[2]];
        if(!EvaluateActorPose(model,p[0],p[1]?ac.endTime:ac.startTime,a,error)||!EvaluateActorPose(model,p[2],p[3]?bc.endTime:bc.startTime,b,error))return 8;
        const double body=Delta(a.positions,b.positions),world=MatrixDelta(a.worldNodes,b.worldNodes);const bool same=SamePose(a,b);endpoints=endpoints&&same;
        std::printf("%s{\"aClip\":%u,\"aEndpoint\":%u,\"bClip\":%u,\"bEndpoint\":%u,\"bodyDelta\":%.12g,\"worldDelta\":%.12g,\"completePoseBitwisePass\":%s}",i?",":"",p[0],p[1],p[2],p[3],body,world,same?"true":"false");
    }
    ActorPose walkFirst,walkLast;if(!EvaluateActorPose(model,6,model.clips[6].startTime,walkFirst,error)||!EvaluateActorPose(model,6,model.clips[6].endTime,walkLast,error))return 9;
    const float heldTime=1.7200000286102295F;ActorPose held;
    if(!EvaluateActorPose(model,0,heldTime,held,error)||!FinitePose(held,model))return 10;
    std::vector<float> holdTimes={heldTime};for(unsigned i=0;i<=1024;++i){float t=model.clips[0].endTime*i/1024;if(t>=heldTime)holdTimes.push_back(t);}
    for(const auto &t:model.clips[0].tracks)for(float key:t.times)if(key>=heldTime)holdTimes.push_back(key);
    for(unsigned i=0;i<65;++i)holdTimes.push_back(model.clips[0].endTime+2.F*i/64);
    std::sort(holdTimes.begin(),holdTimes.end());holdTimes.erase(std::unique(holdTimes.begin(),holdTimes.end()),holdTimes.end());
    double varyingBody=0,varyingWorld=0,varyingNormals=0,varyingPalette=0;bool varyingExact=true;unsigned varyingMismatches=0;
    float firstTime=0,firstExpected=0,firstActual=0;std::size_t firstNode=0,firstComponent=0;bool firstRecorded=false;
    for(float t:holdTimes){if(!EvaluateActorPose(model,0,t,pose,error)||!FinitePose(pose,model))return 11;
        const bool same=SamePose(pose,held);varyingExact=varyingExact&&same;if(!same)++varyingMismatches;
        if(!same&&!firstRecorded){for(std::size_t i=0;i<pose.worldNodes.size()&&!firstRecorded;++i)for(std::size_t j=0;j<16&&!firstRecorded;++j)
            if(!Same(pose.worldNodes[i][j],held.worldNodes[i][j])){firstRecorded=true;firstTime=t;firstNode=i;firstComponent=j;firstExpected=held.worldNodes[i][j];firstActual=pose.worldNodes[i][j];}}
        varyingBody=std::max(varyingBody,Delta(pose.positions,held.positions));varyingWorld=std::max(varyingWorld,MatrixDelta(pose.worldNodes,held.worldNodes));varyingNormals=std::max(varyingNormals,Delta(pose.normals,held.normals));varyingPalette=std::max(varyingPalette,MatrixDelta(pose.jointPalette,held.jointPalette));}
    ActorPose terminal;if(!EvaluateActorPose(model,0,model.clips[0].endTime,terminal,error))return 12;bool fixed=true;
    for(unsigned i=0;i<65;++i){if(!EvaluateActorPose(model,0,model.clips[0].endTime,pose,error)||!SamePose(pose,terminal))fixed=false;}
    // Meaningful real-loader/evaluator refusal checks preserve every payload.
    unsigned atomicChecks=0;
    if(LoadActorModel(std::span(bytes).first(bytes.size()-1),model,error)||!SameModel(model,frozen)||Fingerprint(model)!=before)return 13;++atomicChecks;
    ActorPose oldPose=pose;
    if(EvaluateActorPose(model,model.clips.size(),0,pose,error)||!SamePose(oldPose,pose))return 14;++atomicChecks;
    if(EvaluateActorPose(model,0,std::numeric_limits<float>::quiet_NaN(),pose,error)||!SamePose(oldPose,pose))return 15;++atomicChecks;
    if(!SameModel(model,frozen)||Fingerprint(model)!=before)return 16;
    std::printf("],\"uniqueDensePoses\":%zu,\"eightYawVertexChecks\":%zu,\"allDenseBodyFloorMotionPass\":%s,\"declaredTransitionEndpointsExact\":%s,\"walkTerminalToStartBodyDelta\":%.12g,\"heldStoredTime\":%.12g,\"differentHeldTimes\":%zu,\"differentHeldBodyDelta\":%.12g,\"differentHeldWorldDelta\":%.12g,\"differentHeldNormalDelta\":%.12g,\"differentHeldPaletteDelta\":%.12g,\"differentHeldBitwisePass\":%s,\"differentHeldMismatchCalls\":%u,",total,directionChecks,spatial?"true":"false",endpoints?"true":"false",Delta(walkFirst.positions,walkLast.positions),heldTime,holdTimes.size(),varyingBody,varyingWorld,varyingNormals,varyingPalette,varyingExact?"true":"false",varyingMismatches);
    if(firstRecorded)std::printf("\"firstDifferentHeldWitness\":{\"time\":%.12g,\"field\":\"worldNodes\",\"element\":%zu,\"nodeName\":\"%s\",\"component\":%zu,\"reference\":%.12g,\"actual\":%.12g,\"referenceFloat32Bits\":%u,\"actualFloat32Bits\":%u},",firstTime,firstNode,model.nodes[firstNode].name.c_str(),firstComponent,firstExpected,firstActual,std::bit_cast<std::uint32_t>(firstExpected),std::bit_cast<std::uint32_t>(firstActual));
    else std::printf("\"firstDifferentHeldWitness\":null,");
    std::printf("\"fixedTerminalRepeatCalls\":65,\"fixedTerminalBitwisePass\":%s,\"atomicRefusalChecks\":%u,\"allDensePoseShapesExact\":true,\"staticSourceAndProfileTextureExact\":true,\"loadedModelReadonlyFullPayloadUnchanged\":true,\"loadedModelReadonlyFingerprintUnchanged\":true,\"fingerprintIsDiagnosticNotAuthentication\":true,\"cppParserEvaluatorReal\":true,\"differentTimeHoldMeasuredSeparatelyFromFixedTerminalGate\":true,\"nativePlayerRendererGameplayOrApproval\":false}\n",fixed?"true":"false",atomicChecks);
    return spatial&&endpoints&&fixed?0:17;
}
