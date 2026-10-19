#pragma once

#include "levels/surface_world.hpp"

namespace devilution {

// Prepare stages a CPU-owned visual context without publishing it. The Host
// must call this on every abort after successful preparation. Exact matching
// staged drafts alone are released; published/current/rollback owners survive.
// Call before discarding the draft identities, AFTER releasing World StateMutex.
// This boundary reads the published World owner; never call during cleanup
// while PreparedSurfaceWorldRestore retains that lock. No allocation/native I/O.
void CancelTownViewSurfaceVisualResources(const SurfaceWorldSnapshot &draft) noexcept;

} // namespace devilution
