#pragma once

#include "../cathedral_snapshot.hpp"

namespace devilution::cathedral {

struct CaptureMetadata {
	uint32_t seed;
	uint64_t gameRevision;
	uint8_t entry;
};

// Call at the native simulation/render boundary on its owning thread. No locks,
// generator, resource loading, RNG advancement or simulation mutation occurs.
// Metadata scopes native object slots to this loaded level, not to a save.
Snapshot CaptureSnapshot(CaptureMetadata metadata);

} // namespace devilution::cathedral
