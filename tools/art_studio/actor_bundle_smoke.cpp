#include "engine/render/actor_model.hpp"
#include "engine/render/actor_pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

int main(int argc, char **argv)
{
    using namespace devilution;
    if(argc!=2)return 2;
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    ActorModel model;ActorPose pose;std::string error;
    if(!LoadActorModel(bytes,model,error)){std::fprintf(stderr,"%s\n",error.c_str());return 3;}
    constexpr const char *names[]={"idle","walk","attack","hit","cast-fire","cast-lightning","cast-magic","block-posture","death","walk-entry"};
    if(model.clips.size()<8||model.clips.size()>std::size(names)||model.joints.size()!=52)return 4;
    std::printf("{\"clips\":[");
    for(std::size_t clip=0;clip<model.clips.size();++clip){
        const auto &animation=model.clips[clip];
        if(animation.name!=names[clip])return 5;
        float ground=1e9F,motion=0,seam=0,holdDelta=0;std::vector<ActorVec3> first,held;
        for(unsigned sample=0;sample<=1024;++sample){
            const float seconds=animation.startTime+(animation.endTime-animation.startTime)*sample/1024;
            if(!EvaluateActorPose(model,clip,seconds,pose,error))return 6;
            if(sample==0)first=pose.positions;
            if(clip==8&&sample>=881&&held.empty())held=pose.positions;
            if(pose.positions.size()!=first.size())return 7;
            for(std::size_t vertex=0;vertex<first.size();++vertex){
                const auto &p=pose.positions[vertex];const auto &a=first[vertex];
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return 8;
                ground=std::min(ground,p.y);
                const float delta=std::sqrt((p.x-a.x)*(p.x-a.x)+(p.y-a.y)*(p.y-a.y)+(p.z-a.z)*(p.z-a.z));
                motion=std::max(motion,delta);if(sample==1024)seam=std::max(seam,delta);
                if(!held.empty()){
                    const auto &h=held[vertex];
                    holdDelta=std::max(holdDelta,std::sqrt((p.x-h.x)*(p.x-h.x)+(p.y-h.y)*(p.y-h.y)+(p.z-h.z)*(p.z-h.z)));
                }
            }
        }
        if(ground<-.002F||motion<.001F)return 9;
        if(clip==8&&(held.empty()||holdDelta>1e-6F))return 10;
        std::printf("%s{\"name\":\"%s\",\"samples\":1025,\"ground\":%.9f,\"motion\":%.9f,\"endpointDelta\":%.9f,\"terminalHoldDelta\":%.9f}",clip?",":"",names[clip],ground,motion,seam,holdDelta);
    }
    std::printf("],\"allClipGroundAndMotionPass\":true,\"gameplayCoverageComplete\":false,\"worldRendererConnected\":false}\n");
    return 0;
}
