#include "engine/render/actor_player_frame.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace devilution {
namespace {
constexpr std::size_t MaxIndices = 300000;
constexpr std::size_t MaxVertices = 100000;
bool Finite(ActorVec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool Bounded(ActorVec3 v) { return Finite(v)&&std::abs(v.x)<=4096&&std::abs(v.y)<=4096&&std::abs(v.z)<=4096; }
bool Sha(std::string_view value) {
    return value.size()==64&&std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
ActorMatrix Multiply(const ActorMatrix &a,const ActorMatrix &b) {
    ActorMatrix result {};
    for(std::size_t column=0;column<4;++column)for(std::size_t row=0;row<4;++row){
        double value=0;for(std::size_t k=0;k<4;++k)value+=static_cast<double>(a[k*4+row])*b[column*4+k];
        result[column*4+row]=static_cast<float>(value);
    }
    return result;
}
ActorMatrix WorldFit(const ActorVisualSnapshot &snapshot,const ActorPlayerFit &fit) {
    const double angle=static_cast<double>(snapshot.nativeYawRadians)+fit.authoredYawOffset;
    const double c=std::cos(angle)*fit.uniformScale,s=std::sin(angle)*fit.uniformScale;
    const auto &p=snapshot.authoritativeFootpoint;const auto &f=fit.authoredFootpoint;
    return {static_cast<float>(c),0,static_cast<float>(-s),0,
        0,fit.uniformScale,0,0,static_cast<float>(s),0,static_cast<float>(c),0,
        static_cast<float>(p.x-c*f.x-s*f.z),p.y-fit.uniformScale*f.y,
        static_cast<float>(p.z+s*f.x-c*f.z),1};
}
bool NormalMatrix(const ActorMatrix &m,std::array<double,9> &n) {
    const double a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!std::isfinite(determinant)||determinant<=1e-12)return false;
    // Row-major inverse transpose. Reject reflections rather than silently
    // reversing authored winding; positive nonuniform parent scale is handled.
    n={(e*i-f*h)/determinant,(f*g-d*i)/determinant,(d*h-e*g)/determinant,
       (c*h-b*i)/determinant,(a*i-c*g)/determinant,(b*g-a*h)/determinant,
       (b*f-c*e)/determinant,(c*d-a*f)/determinant,(a*e-b*d)/determinant};
    return std::all_of(n.begin(),n.end(),[](double value){return std::isfinite(value);});
}
bool TransformVertex(const ActorRigidVertex &v,const ActorMatrix &m,const std::array<double,9> &n,ActorPlayerGeometryVertex &out) {
    if(!Finite(v.position)||!Finite(v.normal)||!std::isfinite(v.uv.x)||!std::isfinite(v.uv.y))return false;
    for(float color: {v.color.x,v.color.y,v.color.z,v.color.w})
        if(!std::isfinite(color)||color<0||color>1)return false;
    const auto &p=v.position;
    out.worldPosition={m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    const auto &normal=v.normal;
    const double x=n[0]*normal.x+n[1]*normal.y+n[2]*normal.z;
    const double y=n[3]*normal.x+n[4]*normal.y+n[5]*normal.z;
    const double z=n[6]*normal.x+n[7]*normal.y+n[8]*normal.z;
    const double length=std::sqrt(x*x+y*y+z*z);
    if(!Bounded(out.worldPosition)||!std::isfinite(length)||length<=1e-12)return false;
    out.worldNormal={static_cast<float>(x/length),static_cast<float>(y/length),static_cast<float>(z/length)};
    out.uv=v.uv;out.authoredColor=v.color;return true;
}
}

bool PrepareActorPlayerFrame(const ActorModel &model,const PlayerActorCapture &capture,
    std::uint64_t liveLevelEpoch,std::uint32_t expectedNativeIndex,std::size_t clip,
    const ActorPlayerPhaseBinding &binding,const ActorPlayerFit &fit,
    std::span<const ActorPlayerAttachmentInput> attachments,ActorPlayerFrame &output,std::string &error)
{
    const auto fail=[&error](const char *message){error=message;return false;};
    if(attachments.size()>8)return fail("Actor frame exceeds attachment count");
    std::size_t indexCount=model.indices.size();
    std::size_t vertexCount=model.vertices.size();
    if(vertexCount>MaxVertices)return fail("Actor body exceeds frame vertex budget");
    for(std::size_t item=0;item<attachments.size();++item){
        const auto &attachment=attachments[item];
        if(attachment.assetId.empty()||attachment.assetId.size()>256||attachment.revision.empty()||attachment.revision.size()>256
            ||!Sha(attachment.sourceSha256)||attachment.nativeItemId<0||attachment.bone.empty()||attachment.bone.size()>256
            ||attachment.parts.empty()||attachment.parts.size()>32)return fail("Actor attachment lacks bounded source/item/part identity");
        for(std::size_t previous=0;previous<item;++previous)
            if(attachments[previous].bone==attachment.bone)return fail("Actor attachment socket is duplicated");
        for(const auto &part:attachment.parts){
            if(part.vertices.empty()||part.vertices.size()>MaxVertices||vertexCount>MaxVertices-part.vertices.size()
                ||part.indices.empty()||part.indices.size()%3!=0
                ||part.indices.size()>MaxIndices||indexCount>MaxIndices-part.indices.size())return fail("Actor frame exceeds rigid geometry budget");
            vertexCount+=part.vertices.size();
            indexCount+=part.indices.size();
            for(auto index:part.indices)if(index>=part.vertices.size())return fail("Actor rigid index outside vertices");
        }
    }
    try {
        ActorPlayerFrame prepared;
        if(!PrepareActorPlayerGeometry(model,capture,liveLevelEpoch,expectedNativeIndex,clip,binding,fit,prepared.body,error))return false;
        ActorPose socketPose;socketPose.worldNodes=prepared.body.authoredWorldNodes;
        const ActorMatrix worldFit=WorldFit(capture.snapshot,fit);
        prepared.attachments.reserve(attachments.size());
        for(const auto &source:attachments){
            ActorMatrix authoredSocket {};
            if(!ActorPlayerAttachment(model,socketPose,source.bone,source.local,authoredSocket,error))return false;
            ActorPlayerAttachmentGeometry instance;
            instance.assetId=source.assetId;instance.revision=source.revision;instance.sourceSha256=source.sourceSha256;
            instance.bone=source.bone;instance.nativeItemId=source.nativeItemId;
            instance.worldMatrix=Multiply(worldFit,authoredSocket);
            std::array<double,9> normalMatrix {};
            if(!NormalMatrix(instance.worldMatrix,normalMatrix))return fail("Actor attachment has singular or mirrored basis");
            instance.parts.reserve(source.parts.size());
            for(const auto &part:source.parts){
                ActorPlayerAttachmentPart geometry;geometry.material=part.material;
                geometry.indices.assign(part.indices.begin(),part.indices.end());geometry.vertices.resize(part.vertices.size());
                for(std::size_t i=0;i<part.vertices.size();++i)
                    if(!TransformVertex(part.vertices[i],instance.worldMatrix,normalMatrix,geometry.vertices[i]))return fail("Actor rigid position/normal/UV/color is invalid");
                instance.parts.push_back(std::move(geometry));
            }
            prepared.attachments.push_back(std::move(instance));
        }
        output=std::move(prepared);error.clear();return true;
    } catch(const std::bad_alloc &){return fail("Actor frame allocation failed");}
}
} // namespace devilution
