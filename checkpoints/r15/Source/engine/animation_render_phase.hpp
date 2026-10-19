#pragma once
#include <algorithm>
#include <cstdint>

namespace devilution {

struct AnimationRenderPhaseInput {
    int frameCount = 0;
    int currentFrame = 0;
    int ticksPerFrame = 0;
    int tickCounter = 0;
    int distributedFrames = 0;
    int previousSkippedFrames = 0;
    int elapsedTicks128 = 0;
    unsigned tickModifier = 0;
    unsigned interpolation128 = 0;
    bool petrified = false;
};

struct AnimationRenderPhase {
    int frame = 0;
    float subframe = 0;
    bool previousRecovery = false;
    bool valid = false;
};

// Read-only equivalent of the native displayed-frame arithmetic, plus its
// subframe. Explicit interpolation is retained by the caller during pause.
inline AnimationRenderPhase SampleAnimationRenderPhase(const AnimationRenderPhaseInput &input)
{
    AnimationRenderPhase result;
    if (input.frameCount <= 0 || input.frameCount > 127 || input.ticksPerFrame <= 0
        || input.tickCounter < 0 || input.tickCounter >= input.ticksPerFrame
        || input.currentFrame < -8 || input.currentFrame >= input.frameCount
        || input.interpolation128 > 128) return result;
    const unsigned interpolation = input.petrified ? 0 : input.interpolation128;
    if (input.distributedFrames <= 0 || input.currentFrame >= input.distributedFrames) {
        result.frame = std::max(input.currentFrame, 0);
        result.subframe = input.currentFrame < 0 ? 0
            : static_cast<float>((input.tickCounter + interpolation / 128.0) / input.ticksPerFrame);
        result.valid = true;
        return result;
    }
    if (input.distributedFrames > input.frameCount || input.tickModifier == 0
        || input.previousSkippedFrames < 0 || input.previousSkippedFrames >= input.frameCount) return result;
    const std::int64_t total = std::max(input.elapsedTicks128, 0) + interpolation;
    const std::int64_t raw = total * input.tickModifier;
    constexpr std::int64_t Unit = 128 * 128;
    const std::int64_t absolute = raw / Unit;
    if (absolute > 127) return result; // refuse overflow/wrapped malformed native state
    int frame = static_cast<int>(absolute);
    float fraction = static_cast<float>(raw % Unit) / Unit;
    if (input.previousSkippedFrames > 0) {
        frame -= input.previousSkippedFrames;
        if (frame < 0) { frame += input.frameCount; result.previousRecovery = true; }
    } else if (frame >= input.distributedFrames) {
        frame = input.distributedFrames - 1;
        fraction = 0; // native cap: do not predict past the authorized frame
    }
    if (frame < 0 || frame >= input.frameCount) return result;
    result.frame = frame;
    result.subframe = fraction;
    result.valid = true;
    return result;
}

} // namespace devilution
