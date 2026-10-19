#include "engine/render/actor_player_geometry.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

int main(int argc,char **argv)
{
    using namespace devilution;
    if(argc!=2)return 2;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    ActorModel model;std::string error;
    if(!LoadActorModel(bytes,model,error)||model.clips.size()!=10)return 3;
    PlayerActorCapture capture;
    ActorVisualInput input;
    input.assetId=model.assetId;input.variant=model.variant;input.revision=model.revision;input.sourceSha256=model.sourceSha256;
    input.action=ActorVisualAction::Idle;input.frameCount=10;input.ticksPerFrame=2;input.progressToNextTick128=64;
    input.authoritativeFootpoint={5,.3F,7};capture.levelEpoch=12;
    capture.renderPhase={0,.25F,false,true};
    ActorPlayerFit fit;fit.uniformScale=2;fit.authoredFootpoint={.11F,-.04F,.02F};fit.authoredYawOffset=.15F;
    ActorPlayerPhaseBinding binding;binding.loop=true;
    ActorPlayerGeometry output;unsigned checks=0;
    const auto check=[&](bool condition){if(condition)++checks;return condition;};
    float maxPositionDelta=0,maxNormalDelta=0;
    for(unsigned direction=0;direction<8;++direction){
        input.nativeDirection=static_cast<std::uint8_t>(direction);
        if(!MakeActorVisualSnapshot(input,capture.snapshot,error)
            ||!PrepareActorPlayerGeometry(model,capture,12,0,0,binding,fit,output,error))return 4;
        ActorPose reference;
        if(!EvaluateActorPose(model,0,model.clips[0].endTime*.025F,reference,error))return 5;
        const double angle=ActorNativeDirectionYaw(direction)+fit.authoredYawOffset;
        const double c=std::cos(angle),s=std::sin(angle);
        for(std::size_t i=0;i<reference.positions.size();++i){
            const auto &p=reference.positions[i];const auto &n=reference.normals[i];const auto &actual=output.vertices[i];
            const double x=(p.x-fit.authoredFootpoint.x)*2,y=(p.y-fit.authoredFootpoint.y)*2,z=(p.z-fit.authoredFootpoint.z)*2;
            maxPositionDelta=std::max(maxPositionDelta,static_cast<float>(std::max({std::abs(actual.worldPosition.x-(c*x+s*z+5)),std::abs(actual.worldPosition.y-(y+.3)),std::abs(actual.worldPosition.z-(-s*x+c*z+7))})));
            const double nx=c*n.x+s*n.z,nz=-s*n.x+c*n.z;
            maxNormalDelta=std::max(maxNormalDelta,static_cast<float>(std::max({std::abs(actual.worldNormal.x-nx),std::abs(static_cast<double>(actual.worldNormal.y)-n.y),std::abs(actual.worldNormal.z-nz)})));
            if(actual.uv.x!=model.vertices[i].uv.x||actual.uv.y!=model.vertices[i].uv.y)return 6;
        }
        if(!check(output.indices==model.indices&&output.levelEpoch==12&&output.nativeIndex==0&&maxPositionDelta<2e-6F&&maxNormalDelta<2e-6F))return 7;
    }
    const auto originalCount=output.vertices.size();const auto originalFirst=output.vertices[0].worldPosition;const auto originalEpoch=output.levelEpoch;
    const auto unchanged=[&](){return output.levelEpoch==originalEpoch&&output.vertices.size()==originalCount
        &&output.vertices[0].worldPosition.x==originalFirst.x&&output.vertices[0].worldPosition.y==originalFirst.y&&output.vertices[0].worldPosition.z==originalFirst.z;};
    const auto reject=[&](std::uint64_t epoch,std::uint32_t slot,std::size_t clip){return !PrepareActorPlayerGeometry(model,capture,epoch,slot,clip,binding,fit,output,error)&&unchanged();};
    if(!check(reject(0,0,0))||!check(reject(13,0,0))||!check(reject(12,1,0))||!check(reject(12,0,99)))return 8;
    capture.renderPhase.frame=1;if(!check(reject(12,0,0)))return 9;capture.renderPhase.frame=0;
    capture.snapshot.sourceSha256="foreign";if(!check(reject(12,0,0)))return 10;capture.snapshot.sourceSha256=model.sourceSha256;
    fit.uniformScale=0;if(!check(reject(12,0,0)))return 11;fit.uniformScale=2;
    fit.authoredYawOffset=std::numeric_limits<float>::quiet_NaN();if(!check(reject(12,0,0)))return 12;fit.authoredYawOffset=.15F;
    binding.nativeRenderedSubframe=.5F;if(!check(reject(12,0,0)))return 13;binding.nativeRenderedSubframe=-1;
    auto index=model.indices[0];model.indices[0]=static_cast<std::uint32_t>(model.vertices.size());if(!check(reject(12,0,0)))return 14;model.indices[0]=index;
    capture.snapshot.authoritativeFootpoint.x=std::numeric_limits<float>::infinity();if(!check(reject(12,0,0)))return 15;capture.snapshot.authoritativeFootpoint.x=5;
    capture.snapshot.nativeYawRadians+=.1F;if(!check(reject(12,0,0)))return 16;
    std::printf("{\"checks\":%u,\"directions\":8,\"verticesPerDirection\":%zu,\"positionMaxDelta\":%.9g,\"normalMaxDelta\":%.9g,\"uvAndIndicesPreserved\":true,\"atomicRejects\":true,\"worldRendererConnected\":false}\n",checks,model.vertices.size(),maxPositionDelta,maxNormalDelta);
    return 0;
}
