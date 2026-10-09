#include "engine/render/ogden_idle_native_clock.hpp"

#include <limits>

namespace devilution {
namespace {

std::uint64_t NativeVisualTicks = 0;
std::uint64_t NativeClockGeneration = 0;

constexpr std::uint64_t SaturatingIncrement(std::uint64_t value)
{
	return value == std::numeric_limits<std::uint64_t>::max() ? value : value + 1;
}

// No test-only setter or exported mutation hook. Compiler checks both overflow
// boundaries of the same primitive used by normal native callback advancement.
static_assert(SaturatingIncrement(0) == 1);
static_assert(SaturatingIncrement(std::numeric_limits<std::uint64_t>::max() - 1)
    == std::numeric_limits<std::uint64_t>::max());
static_assert(SaturatingIncrement(std::numeric_limits<std::uint64_t>::max())
    == std::numeric_limits<std::uint64_t>::max());

} // namespace

void ResetOgdenIdleNativeClock()
{
	NativeVisualTicks = 0;
	NativeClockGeneration = SaturatingIncrement(NativeClockGeneration);
}

void AdvanceOgdenIdleNativeClock()
{
	NativeVisualTicks = SaturatingIncrement(NativeVisualTicks);
}

std::uint64_t GetOgdenIdleNativeTicks()
{
	return NativeVisualTicks;
}

std::uint64_t GetOgdenIdleNativeClockGeneration()
{
	return NativeClockGeneration;
}

} // namespace devilution
