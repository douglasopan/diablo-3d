#pragma once

#include <cstdint>

namespace devilution {

// Private visual tick authority adapter. Planned native lifecycle owner calls
// Reset at InitTowners/FreeTownerGFX and Advance once per ProcessTowners entry.
// Renderer calls only getters. No SDL time, NPC access, RNG or simulation change.
// These functions share native-loop state and require the owner's game thread;
// they are not a cross-thread clock or a replacement for the simulation clock.
// Both counters saturate at UINT64_MAX. Reset clears ticks and increments the
// generation; at generation saturation, a caller must retire any cached epoch.
void ResetOgdenIdleNativeClock();
void AdvanceOgdenIdleNativeClock();
std::uint64_t GetOgdenIdleNativeTicks();
std::uint64_t GetOgdenIdleNativeClockGeneration();

} // namespace devilution
