#include "engine/render/actor_player_frame.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

namespace {
template<class T> bool Read(std::istream &file,T &value){return static_cast<bool>(file.read(reinterpret_cast<char *>(&value),sizeof(value)));}
bool Text(std::istream &file,std::string &value){std::uint16_t size=0;if(!Read(file,size)||size==0||size>256)return false;value.resize(size);return static_cast<bool>(file.read(value.data(),size));}
struct RigidSource {devilution::ActorPlayerAttachmentInput input;std::vector<devilution::ActorRigidPartInput> parts;std::vector<std::vector<devilution::ActorRigidVertex>> vertices;std::vector<std::vector<std::uint32_t>> indices;};
bool SameVertices(const std::vector<devilution::ActorPlayerGeometryVertex> &a,const std::vector<devilution::ActorPlayerGeometryVertex> &b){
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(a[i].worldPosition.x!=b[i].worldPosition.x||a[i].worldPosition.y!=b[i].worldPosition.y||a[i].worldPosition.z!=b[i].worldPosition.z
        ||a[i].worldNormal.x!=b[i].worldNormal.x||a[i].worldNormal.y!=b[i].worldNormal.y||a[i].worldNormal.z!=b[i].worldNormal.z||a[i].uv.x!=b[i].uv.x||a[i].uv.y!=b[i].uv.y)return false;
    return true;
}
bool SameFrame(const devilution::ActorPlayerFrame &a,const devilution::ActorPlayerFrame &b){
    const auto &x=a.body;const auto &y=b.body;
    if(x.assetId!=y.assetId||x.variant!=y.variant||x.revision!=y.revision||x.sourceSha256!=y.sourceSha256||x.levelEpoch!=y.levelEpoch
        ||x.nativeIndex!=y.nativeIndex||x.clip!=y.clip||x.sampleSeconds!=y.sampleSeconds||x.indices!=y.indices||x.authoredWorldNodes!=y.authoredWorldNodes
        ||!SameVertices(x.vertices,y.vertices)||a.attachments.size()!=b.attachments.size())return false;
    for(std::size_t i=0;i<a.attachments.size();++i){
        const auto &p=a.attachments[i];const auto &q=b.attachments[i];
        if(p.assetId!=q.assetId||p.revision!=q.revision||p.sourceSha256!=q.sourceSha256||p.bone!=q.bone||p.nativeItemId!=q.nativeItemId||p.worldMatrix!=q.worldMatrix||p.parts.size()!=q.parts.size())return false;
        for(std::size_t j=0;j<p.parts.size();++j)if(p.parts[j].material!=q.parts[j].material||p.parts[j].indices!=q.parts[j].indices||!SameVertices(p.parts[j].vertices,q.parts[j].vertices))return false;
    }
    return true;
}
}
int main(int argc,char **argv)
{
    using namespace devilution;
    if(argc!=4)return 2;
    std::ifstream actor(argv[1],std::ios::binary),rigid(argv[2],std::ios::binary),reference(argv[3]);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(actor)),{});ActorModel model;std::string error;
    if(!LoadActorModel(bytes,model,error)||model.clips.size()!=10)return 3;
    char magic[8];std::uint32_t count=0;
    if(!rigid.read(magic,8)||std::string_view(magic,8)!="D3DRIGD1"||!Read(rigid,count)||count!=2)return 4;
    std::vector<RigidSource> sources(count);std::vector<ActorPlayerAttachmentInput> inputs;
    for(auto &source:sources){
        auto &in=source.input;std::uint32_t parts=0;
        if(!Read(rigid,in.nativeItemId)||!Text(rigid,in.assetId)||!Text(rigid,in.revision)||!Text(rigid,in.sourceSha256)||!Text(rigid,in.bone)
            ||!Read(rigid,in.local.translation)||!Read(rigid,in.local.rotation)||!Read(rigid,in.local.scale)||!Read(rigid,parts)||parts==0||parts>32)return 5;
        source.parts.resize(parts);source.vertices.resize(parts);source.indices.resize(parts);
        for(std::size_t p=0;p<parts;++p){
            auto &part=source.parts[p];std::uint32_t verts=0,indices=0;
            if(!Read(rigid,part.material)||!Read(rigid,verts)||!Read(rigid,indices)||verts>100000||indices>300000)return 6;
            source.vertices[p].resize(verts);source.indices[p].resize(indices);
            for(auto &v:source.vertices[p])if(!Read(rigid,v.position)||!Read(rigid,v.normal)||!Read(rigid,v.uv))return 7;
            for(auto &i:source.indices[p])if(!Read(rigid,i))return 8;
            part.vertices=source.vertices[p];part.indices=source.indices[p];
        }
        in.parts=source.parts;inputs.push_back(in);
    }
    ActorPlayerFit fit;fit.uniformScale=2;fit.authoredFootpoint={.11F,-.04F,.02F};fit.authoredYawOffset=.15F;
    ActorPlayerPhaseBinding binding;PlayerActorCapture capture;capture.levelEpoch=12;
    ActorVisualInput input;input.assetId=model.assetId;input.variant=model.variant;input.revision=model.revision;input.sourceSha256=model.sourceSha256;
    input.action=ActorVisualAction::Idle;input.frameCount=5;input.ticksPerFrame=2;input.authoritativeFootpoint={5,.3F,7};
    ActorPlayerFrame frame;std::size_t clip=0;unsigned direction=0,samples=0,checks=0;double phase=0,matrixDelta=0,vertexDelta=0,normalDelta=0;
    while(reference>>clip>>phase>>direction){
        std::array<ActorMatrix,2> expected;
        for(auto &m:expected)for(float &v:m)if(!(reference>>v))return 9;
        input.nativeDirection=static_cast<std::uint8_t>(direction);input.logicalFrame=input.displayedFrame=static_cast<std::uint32_t>(phase*4);
        if(!MakeActorVisualSnapshot(input,capture.snapshot,error))return 10;
        capture.renderPhase={input.displayedFrame,0,false,true};
        if(!PrepareActorPlayerFrame(model,capture,12,0,clip,binding,fit,inputs,frame,error)){std::fprintf(stderr,"%s\n",error.c_str());return 11;}
        if(frame.attachments.size()!=inputs.size())return 27;
        for(std::size_t item=0;item<inputs.size();++item){
            const auto &actual=frame.attachments[item];const auto &m=expected[item];
            if(actual.parts.size()!=inputs[item].parts.size()||actual.assetId!=inputs[item].assetId||actual.revision!=inputs[item].revision
                ||actual.sourceSha256!=inputs[item].sourceSha256||actual.bone!=inputs[item].bone||actual.nativeItemId!=inputs[item].nativeItemId)return 28;
            for(std::size_t i=0;i<16;++i)matrixDelta=std::max(matrixDelta,std::abs(static_cast<double>(actual.worldMatrix[i])-m[i]));
            for(std::size_t part=0;part<actual.parts.size();++part){
                const auto &g=actual.parts[part];const auto &src=sources[item].parts[part];
                if(g.material!=src.material||g.indices!=sources[item].indices[part]||g.vertices.size()!=src.vertices.size())return 12;
                for(std::size_t i=0;i<g.vertices.size();++i){
                    const auto &v=src.vertices[i];const auto &a=g.vertices[i];const auto &p=v.position;
                    const ActorVec3 ref {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
                    vertexDelta=std::max(vertexDelta,static_cast<double>(std::max({std::abs(ref.x-a.worldPosition.x),std::abs(ref.y-a.worldPosition.y),std::abs(ref.z-a.worldPosition.z)})));
                    const double length=std::sqrt(a.worldNormal.x*a.worldNormal.x+a.worldNormal.y*a.worldNormal.y+a.worldNormal.z*a.worldNormal.z);
                    normalDelta=std::max(normalDelta,std::abs(length-1));
                    if(a.uv.x!=v.uv.x||a.uv.y!=v.uv.y)return 13;
                }
            }
        }
        ++samples;
    }
    if(samples!=400||matrixDelta>2e-5||vertexDelta>2e-5||normalDelta>2e-6)return 14;
    // Deliberately synthetic positive nonuniform bone scale verifies inverse
    // transpose, not anatomy or an approved source-rig edit.
    ActorModel stretched=model;
    for(auto &node:stretched.nodes)if(node.name==inputs[0].bone)node.rest.scale={1.4F,.7F,1.1F};
    ActorPlayerFrame nonuniform;
    if(!PrepareActorPlayerFrame(stretched,capture,12,0,0,binding,fit,inputs,nonuniform,error))return 24;
    double tangentDot=0;
    for(std::size_t part=0;part<nonuniform.attachments[0].parts.size();++part){
        const auto &m=nonuniform.attachments[0].worldMatrix;const auto &vertices=nonuniform.attachments[0].parts[part].vertices;
        for(std::size_t i=0;i<vertices.size();++i){
            const auto &n=sources[0].parts[part].vertices[i].normal;
            ActorVec3 tangent{n.y,-n.x,0};
            if(std::abs(n.x)+std::abs(n.y)<.01F)tangent={0,n.z,-n.y};
            const double x=m[0]*tangent.x+m[4]*tangent.y+m[8]*tangent.z;
            const double y=m[1]*tangent.x+m[5]*tangent.y+m[9]*tangent.z;
            const double z=m[2]*tangent.x+m[6]*tangent.y+m[10]*tangent.z;
            const auto &a=vertices[i].worldNormal;
            tangentDot=std::max(tangentDot,std::abs(x*a.x+y*a.y+z*a.z));
        }
    }
    if(tangentDot>2e-6)return 25;
    ++checks;
    const ActorPlayerFrame checkpoint=frame;
    const auto unchanged=[&](){return SameFrame(frame,checkpoint);};
    const auto reject=[&](){const bool ok=!PrepareActorPlayerFrame(model,capture,12,0,0,binding,fit,inputs,frame,error)&&unchanged();if(ok)++checks;return ok;};
    auto old=inputs[1].bone;inputs[1].bone=inputs[0].bone;if(!reject())return 15;inputs[1].bone="missing";if(!reject())return 16;inputs[1].bone=old;
    inputs[1].local.scale.y*=2;if(!reject())return 17;inputs[1].local.scale.y=inputs[1].local.scale.x;
    inputs[1].sourceSha256="foreign";if(!reject())return 18;inputs[1].sourceSha256=sources[1].input.sourceSha256;
    auto oldIndex=sources[1].indices[0][0];sources[1].indices[0][0]=static_cast<std::uint32_t>(sources[1].vertices[0].size());if(!reject())return 19;sources[1].indices[0][0]=oldIndex;
    auto oldNormal=sources[1].vertices[0][0].normal;sources[1].vertices[0][0].normal={};if(!reject())return 20;sources[1].vertices[0][0].normal=oldNormal;
    auto oldUv=sources[1].vertices[0][0].uv;sources[1].vertices[0][0].uv.x=std::numeric_limits<float>::quiet_NaN();if(!reject())return 21;sources[1].vertices[0][0].uv=oldUv;
    auto oldItem=inputs[1].nativeItemId;inputs[1].nativeItemId=-1;if(!reject())return 22;inputs[1].nativeItemId=oldItem;
    std::vector<ActorRigidVertex> oversized(100000,sources[1].vertices[0][0]);
    const auto priorSpan=sources[1].parts[0].vertices;sources[1].parts[0].vertices=oversized;if(!reject())return 26;sources[1].parts[0].vertices=priorSpan;
    if(!PrepareActorPlayerFrame(model,capture,12,0,0,binding,fit,{},frame,error)||!frame.attachments.empty()||frame.body.vertices.empty())return 23;
    ++checks;
    std::printf("{\"samples\":%u,\"checks\":%u,\"matrixMaxDelta\":%.9g,\"vertexMaxDelta\":%.9g,\"normalUnitMaxDelta\":%.9g,\"syntheticNonuniformNormalTangentMaxDot\":%.9g,\"allMaterialPartsUvsIndicesPreserved\":true,\"bodyOnlyDropPreparation\":true,\"admissionOrRendererConnected\":false}\n",samples,checks,matrixDelta,vertexDelta,normalDelta,tangentDot);
    return 0;
}
