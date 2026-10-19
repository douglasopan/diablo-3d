#include "engine/render/actor_player_binding.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

int main(int argc, char **argv)
{
    using namespace devilution;
    if (argc != 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    ActorModel model; ActorPose pose; std::string error;
    if (!LoadActorModel(bytes, model, error) || !EvaluateActorPose(model, 0, 0, pose, error)) return 3;
    unsigned checks = 0;
    const auto check = [&checks](bool condition) { if (condition) ++checks; return condition; };
    ActorVisualSnapshot input;
    input.assetId=model.assetId; input.variant=model.variant; input.revision=model.revision; input.sourceSha256=model.sourceSha256;
    input.action=ActorVisualAction::Attack; input.frameCount=16; input.nativeImpactFrame=8; input.ticksPerFrame=2;
    ActorPlayerPhaseBinding binding; binding.authoredImpactPhase=.5F;
    float time=-1;
    input.logicalFrame=input.displayedFrame=8;
    if (!check(ActorPlayerClipTime(model,input,0,binding,time,error) && std::abs(time-model.clips[0].endTime*.5F)<1e-6F)) return 4;
    input.logicalFrame=input.displayedFrame=15;
    if (!check(ActorPlayerClipTime(model,input,0,binding,time,error) && time==model.clips[0].endTime)) return 5;
    input.logicalFrame=-1; input.displayedFrame=0;
    if (!check(ActorPlayerClipTime(model,input,0,binding,time,error) && time==model.clips[0].startTime)) return 6;
    input.logicalFrame=input.displayedFrame=6; input.tickCounter=1; input.progressToNextTick128=64;
    input.paused=true;
    float held=0;
    if (!check(ActorPlayerClipTime(model,input,0,binding,held,error)
            && ActorPlayerClipTime(model,input,0,binding,time,error) && time==held)) return 7;
    input.sourceSha256="foreign"; const float sentinel=time;
    if (!check(!ActorPlayerClipTime(model,input,0,binding,time,error) && time==sentinel)) return 8;
    input.sourceSha256=model.sourceSha256;
    input.logicalFrame=0; input.displayedFrame=12;binding.nativeRenderedSubframe=.4F;
    if (!check(ActorPlayerClipTime(model,input,0,binding,time,error)
            && std::abs(time/model.clips[0].endTime-(.5F+4.4F/7*.5F))<1e-6F)) return 15;
    binding.nativeRenderedSubframe=-1;
    input.action=ActorVisualAction::Spell;input.frameCount=20;
    input.nativeEmissionFrame=13;input.logicalFrame=input.displayedFrame=13;input.tickCounter=0;input.progressToNextTick128=0;
    binding.authoredImpactPhase=-1;binding.authoredEmissionPhase=.68F;
    if (!check(ActorPlayerClipTime(model,input,0,binding,time,error)
            && std::abs(time/model.clips[0].endTime-.68F)<1e-6F)) return 16;
    input.logicalFrame=input.displayedFrame=19;
    if (!check(ActorPlayerClipTime(model,input,0,binding,time,error) && time==model.clips[0].endTime)) return 17;
    const float emissionSentinel=time;binding.authoredImpactPhase=.5F;
    if (!check(!ActorPlayerClipTime(model,input,0,binding,time,error)&&time==emissionSentinel)) return 18;
    binding.authoredImpactPhase=-1;input.action=ActorVisualAction::Attack;
    if (!check(!ActorPlayerClipTime(model,input,0,binding,time,error)&&time==emissionSentinel)) return 19;
    input.action=ActorVisualAction::Spell;binding.loop=true;
    if (!check(!ActorPlayerClipTime(model,input,0,binding,time,error)&&time==emissionSentinel)) return 20;
    binding.authoredEmissionPhase=-1;
    input.action=ActorVisualAction::Walk; input.frameCount=8; input.logicalFrame=input.displayedFrame=7; input.tickCounter=1;
    binding.loop=true; binding.authoredImpactPhase=-1;
    if (!check(ActorPlayerClipTime(model,input,0,binding,time,error) && time>=0 && time<model.clips[0].endTime)) return 9;
    ActorTransform local; ActorMatrix attachment{};
    std::size_t hand=model.nodes.size();
    for (std::size_t i=0;i<model.nodes.size();++i) if (model.nodes[i].name=="mixamorig:RightHand") hand=i;
    if (hand==model.nodes.size()) return 10;
    if (!check(ActorPlayerAttachment(model,pose,"mixamorig:RightHand",local,attachment,error)
            && attachment==pose.worldNodes[hand])) return 11;
    pose.worldNodes[hand]={0,1,0,0,-1,0,0,0,0,0,1,0,2,3,4,1};local.translation={1,0,0};
    if (!check(ActorPlayerAttachment(model,pose,"mixamorig:RightHand",local,attachment,error)
            && attachment[12]==2 && attachment[13]==4 && attachment[14]==4)) return 12;
    const auto original=attachment;local.scale={1,2,1};
    if (!check(!ActorPlayerAttachment(model,pose,"mixamorig:RightHand",local,attachment,error) && attachment==original)) return 13;
    local.scale={1,1,1};pose.worldNodes[hand][0]=std::numeric_limits<float>::quiet_NaN();
    if (!check(!ActorPlayerAttachment(model,pose,"mixamorig:RightHand",local,attachment,error) && attachment==original)) return 14;
    std::printf("{\"checks\":%u,\"nativeImpactFrame\":8,\"phaseAndAttachmentHelpers\":true,\"runtimeHostConnected\":false,\"damageEmitted\":false}\n",checks);
    return 0;
}
