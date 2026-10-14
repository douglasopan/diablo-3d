#include "engine/render/actor_player_geometry.hpp"
#include <cmath>
#include <new>
#include <utility>

namespace devilution {
namespace {
bool Finite(ActorVec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool Bounded(ActorVec3 v) { return Finite(v)&&std::abs(v.x)<=4096&&std::abs(v.y)<=4096&&std::abs(v.z)<=4096; }
}

bool PrepareActorPlayerGeometry(const ActorModel &model, const PlayerActorCapture &capture,
    std::uint64_t liveLevelEpoch, std::uint32_t expectedNativeIndex, std::size_t clip,
    const ActorPlayerPhaseBinding &binding, const ActorPlayerFit &fit,
    ActorPlayerGeometry &output, std::string &error)
{
    const auto fail=[&error](const char *message){error=message;return false;};
    const auto &snapshot=capture.snapshot;
    if(liveLevelEpoch==0||capture.levelEpoch!=liveLevelEpoch||snapshot.nativeIndex!=expectedNativeIndex)
        return fail("Actor geometry belongs to another level or player slot");
    if(!capture.renderPhase.valid||capture.renderPhase.frame!=snapshot.displayedFrame
        ||!std::isfinite(capture.renderPhase.subframe)||capture.renderPhase.subframe<0||capture.renderPhase.subframe>1)
        return fail("Actor geometry requires a coherent native rendered phase");
    if(!std::isfinite(fit.uniformScale)||fit.uniformScale<=0||fit.uniformScale>64
        ||!Finite(fit.authoredFootpoint)||!std::isfinite(fit.authoredYawOffset)
        ||!Bounded(snapshot.authoritativeFootpoint)||snapshot.nativeDirection>7
        ||!std::isfinite(snapshot.nativeYawRadians)
        ||std::abs(snapshot.nativeYawRadians-ActorNativeDirectionYaw(snapshot.nativeDirection))>1e-5F)
        return fail("Actor geometry requires finite uniform fit and native facing");
    if(model.indices.empty()||model.indices.size()%3!=0||model.indices.size()>300000)
        return fail("Actor geometry has no bounded triangle list");
    for(auto index:model.indices)if(index>=model.vertices.size())return fail("Actor geometry index outside skin vertices");
    ActorPlayerPhaseBinding sampled=binding;
    if(sampled.nativeRenderedSubframe!=-1&&sampled.nativeRenderedSubframe!=capture.renderPhase.subframe)
        return fail("Caller phase disagrees with native capture");
    sampled.nativeRenderedSubframe=capture.renderPhase.subframe;
    float seconds=0;
    if(!ActorPlayerClipTime(model,snapshot,clip,sampled,seconds,error))return false;
    try {
        ActorPose pose;
        if(!EvaluateActorPose(model,clip,seconds,pose,error))return false;
        if(pose.positions.size()!=model.vertices.size()||pose.normals.size()!=pose.positions.size())
            return fail("Actor geometry pose size mismatch");
        ActorPlayerGeometry prepared;
        prepared.assetId=model.assetId;prepared.variant=model.variant;prepared.revision=model.revision;
        prepared.sourceSha256=model.sourceSha256;prepared.levelEpoch=liveLevelEpoch;
        prepared.nativeIndex=expectedNativeIndex;prepared.clip=clip;prepared.sampleSeconds=seconds;
        prepared.indices=model.indices;prepared.vertices.resize(model.vertices.size());
        const double angle=static_cast<double>(snapshot.nativeYawRadians)+fit.authoredYawOffset;
        const double c=std::cos(angle),s=std::sin(angle);
        for(std::size_t i=0;i<pose.positions.size();++i){
            const auto &p=pose.positions[i];const auto &n=pose.normals[i];
            const double x=(p.x-fit.authoredFootpoint.x)*fit.uniformScale;
            const double y=(p.y-fit.authoredFootpoint.y)*fit.uniformScale;
            const double z=(p.z-fit.authoredFootpoint.z)*fit.uniformScale;
            auto &vertex=prepared.vertices[i];
            vertex.worldPosition={static_cast<float>(c*x+s*z+snapshot.authoritativeFootpoint.x),
                static_cast<float>(y+snapshot.authoritativeFootpoint.y),
                static_cast<float>(-s*x+c*z+snapshot.authoritativeFootpoint.z)};
            vertex.worldNormal={static_cast<float>(c*n.x+s*n.z),n.y,static_cast<float>(-s*n.x+c*n.z)};
            const double length=std::sqrt(static_cast<double>(vertex.worldNormal.x)*vertex.worldNormal.x
                +static_cast<double>(vertex.worldNormal.y)*vertex.worldNormal.y
                +static_cast<double>(vertex.worldNormal.z)*vertex.worldNormal.z);
            if(!Bounded(vertex.worldPosition)||!std::isfinite(length)||length<=0)
                return fail("Actor geometry position/normal is nonfinite or outside world bounds");
            vertex.worldNormal.x=static_cast<float>(vertex.worldNormal.x/length);
            vertex.worldNormal.y=static_cast<float>(vertex.worldNormal.y/length);
            vertex.worldNormal.z=static_cast<float>(vertex.worldNormal.z/length);
            vertex.uv=model.vertices[i].uv;
            if(!std::isfinite(vertex.uv.x)||!std::isfinite(vertex.uv.y))return fail("Actor geometry UV is nonfinite");
        }
        prepared.authoredWorldNodes=std::move(pose.worldNodes);
        output=std::move(prepared);error.clear();return true;
    } catch(const std::bad_alloc &){return fail("Actor geometry allocation failed");}
}
} // namespace devilution
