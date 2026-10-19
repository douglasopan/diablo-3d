#pragma once

#include <cstdint>
#include <type_traits>

#include "engine/point.hpp"

namespace devilution {

// Read-only prospective native values. No Player/global/render dependencies and
// no new serialization: cold load fills these from the native save probe.
// Animation scalars are decoded/validated BEFORE narrowing, including native
// ticksPerFrame=encoded+1 and currentFrame=encoded-1. walking must agree with
// mode. A valid walking state admits an X/Z envelope, not an exact timed pose.
struct SurfaceWorldProspectiveContext {
    Point playerTile { 0, 0 };
    Point nativeViewPosition { 0, 0 };
    bool inventoryOpen = false;
    bool characterOpen = false;
    bool walking = false;
    int32_t mode = 0;
    int32_t direction = 0;
    int32_t ticksPerFrame = 1;
    int32_t tickCounter = 0;
    int32_t numberOfFrames = 1;
    int32_t currentFrame = 0;
};
static_assert(std::is_trivially_copyable_v<SurfaceWorldProspectiveContext>);

} // namespace devilution
