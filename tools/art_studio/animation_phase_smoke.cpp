#include "engine/animation_render_phase.hpp"
#include <cmath>
#include <cstdio>

int main()
{
    using namespace devilution;
    unsigned checks=0;
    const auto check=[&checks](bool condition){if(condition)++checks;return condition;};
    AnimationRenderPhaseInput input;
    input.frameCount=16;input.currentFrame=5;input.ticksPerFrame=2;input.tickCounter=1;input.interpolation128=64;
    auto phase=SampleAnimationRenderPhase(input);
    if(!check(phase.valid&&phase.frame==5&&std::abs(phase.subframe-.75F)<1e-6F))return 1;
    input.petrified=true;phase=SampleAnimationRenderPhase(input);
    if(!check(phase.valid&&phase.subframe==.5F))return 2;
    input.petrified=false;input.currentFrame=-1;phase=SampleAnimationRenderPhase(input);
    if(!check(phase.valid&&phase.frame==0&&phase.subframe==0))return 3;
    input.currentFrame=0;input.tickCounter=0;input.distributedFrames=9;input.previousSkippedFrames=6;input.tickModifier=256;input.elapsedTicks128=0;
    phase=SampleAnimationRenderPhase(input);
    if(!check(phase.valid&&phase.previousRecovery&&phase.frame==11&&phase.subframe==0))return 4;
    input.elapsedTicks128=256;input.interpolation128=96;phase=SampleAnimationRenderPhase(input);
    if(!check(phase.valid&&phase.previousRecovery&&phase.frame==15&&phase.subframe==.5F))return 5;
    input.elapsedTicks128=384;input.interpolation128=0;phase=SampleAnimationRenderPhase(input);
    if(!check(phase.valid&&!phase.previousRecovery&&phase.frame==0&&phase.subframe==0))return 6;
    input.previousSkippedFrames=0;input.elapsedTicks128=640;phase=SampleAnimationRenderPhase(input);
    if(!check(phase.valid&&phase.frame==8&&phase.subframe==0))return 7;
    input.currentFrame=9;input.tickCounter=1;input.previousSkippedFrames=99;phase=SampleAnimationRenderPhase(input);
    if(!check(phase.valid&&phase.frame==9&&phase.subframe==.5F))return 8;
    input.currentFrame=0;input.previousSkippedFrames=6;input.elapsedTicks128=128;input.interpolation128=32;
    const auto retained=SampleAnimationRenderPhase(input);const auto again=SampleAnimationRenderPhase(input);
    if(!check(retained.valid&&retained.frame==again.frame&&retained.subframe==again.subframe))return 9;
    input.interpolation128=129;if(!check(!SampleAnimationRenderPhase(input).valid))return 10;
    input.interpolation128=0;input.tickModifier=65535;input.elapsedTicks128=32767;
    if(!check(!SampleAnimationRenderPhase(input).valid))return 11;
    std::printf("{\"checks\":%u,\"distributedRecoveryAndFraction\":true,\"nativeStateMutated\":false}\n",checks);
    return 0;
}
