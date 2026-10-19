#pragma once

#include <cstdint>

#include "cathedral_adapter.hpp"

namespace devilution::cathedral {

// Owning native/SDL thread only. Begin-load and free invalidate borrowed frames.
// A successful native load supplies its entry; map/seed/resources remain native.
void InvalidateLiveLevel() noexcept;
void MarkLiveLevelLoaded(uint8_t entry) noexcept;
bool LiveLevelEligible() noexcept;

// Memory-only capture and adapter refresh before drawing. Failure clears the
// frame so callers fall back to native drawing; no simulation state is changed.
bool RefreshLiveFrame(int focusX, int focusZ) noexcept;
const Snapshot *LiveSnapshot() noexcept;
const Scene *LiveScene() noexcept;
uint64_t LiveEpoch() noexcept;
uint64_t LiveSceneRevision() noexcept;

// Conservative comparison of live map, presentation and native bindings against
// the published frame. No allocation, IO, RNG advance or native write occurs.
bool LiveFrameCurrent() noexcept;

} // namespace devilution::cathedral
