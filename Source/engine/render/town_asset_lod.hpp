#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace devilution {

inline constexpr size_t TownAssetNoLod = std::numeric_limits<size_t>::max();

/** Borrowed metadata ordered from finest to coarsest. Source-only masters may
 * precede drawable levels, but are never selected. The first drawable must have
 * verified zero error and an infinite diameter limit. A geometric master without
 * usable UVs/materials must be marked drawable=false by the format adapter. */
struct TownAssetLodLevel {
	uint64_t stableKey = 0;
	/** Conservative distance bound to the drawable reference, in object units. */
	double geometricError = 0;
	/** Coarser levels require a finite positive limit; equality is eligible. */
	double maxProjectedDiameterPixels = std::numeric_limits<double>::infinity();
	bool drawable = false;
	/** Unknown/unverified error allows inspection, never automatic drawing. */
	bool errorVerified = false;
	bool available = false;
	bool resident = false;
};

/** The caller owns projection/frustum calculation. pixelsPerWorldUnit must be
 * a conservative maximum over the bounds, not merely a perspective scale at its
 * center. The radius encloses every LOD about the same anchor in object space.
 * Nonuniform transforms need a conservative scale bound in the adapter. */
struct TownAssetLodProjection {
	double pixelsPerWorldUnit = 0;
	double boundingRadius = 0;
	double worldUnitsPerObjectUnit = 1;
	bool valid = false;
	/** Set when the enclosing bounds cross/touch the camera near plane. */
	bool intersectsNearPlane = false;
};

struct TownAssetLodPolicy {
	double pixelTolerance = 1;
	/** Retain a current coarse level until either limit exceeds (1 + margin). */
	double refineMargin = 0.15;
	/** Enter a coarser level only below both limits multiplied by (1 - margin). */
	double coarsenMargin = 0.15;
};

/** Caller-supplied identity; change revision when any descriptor/bounds change.
 * Keys identify the asset and instance, never the native picking entity itself. */
struct TownAssetLodIdentity {
	uint64_t assetKey = 0;
	uint64_t revision = 0;
	uint64_t instanceKey = 0;
	bool operator==(const TownAssetLodIdentity &) const = default;
};

/** One state per render instance. It remembers the desired quality, not a
 * temporary residency fallback. Reset/invalidate together with scene identity. */
struct TownAssetLodState {
	TownAssetLodIdentity identity;
	size_t desiredLevel = TownAssetNoLod;
	uint64_t desiredLevelKey = 0;
	bool initialized = false;
};

enum class TownAssetLodReason {
	InitialSelection,
	Retained,
	Refined,
	Coarsened,
	IdentityReset,
	InvalidProjection,
	IntersectsNearPlane,
	InvalidPolicy,
	InvalidLevels,
	NoDrawableReference,
};

enum class TownAssetLodFallback {
	None,
	FinerResident,
	CoarserResidentAdequate,
	/** A proposal only: the caller must explicitly accept or defer this loss. */
	CoarserResidentInadequate,
	NoResidentLevel,
};

struct TownAssetLodSelection {
	size_t desiredLevel = TownAssetNoLod;
	/** Resident proposal. An inadequate fallback is never silently sanctioned. */
	size_t drawableLevel = TownAssetNoLod;
	TownAssetLodReason reason = TownAssetLodReason::NoDrawableReference;
	TownAssetLodFallback fallback = TownAssetLodFallback::NoResidentLevel;
	double projectedDiameterPixels = 0;
	double desiredProjectedErrorPixels = 0;
	double drawableProjectedErrorPixels = std::numeric_limits<double>::infinity();
	/** Nominal error/diameter limits, before hysteresis. Finest always meets them. */
	bool drawableMeetsQuality = false;
	bool hysteresisApplied = false;
	bool stateReset = false;
};

/** No engine state, RNG, loading, triangle budget or rendering side effects.
 * Malformed metadata/projection/policy and near-plane intersections demand the
 * finest verified drawable reference. Invalid metadata disables coarse fallbacks.
 * No allocations; spans are consumed during this call and never retained. */
TownAssetLodSelection SelectTownAssetLod(std::span<const TownAssetLodLevel> levels,
    const TownAssetLodIdentity &identity, const TownAssetLodProjection &projection,
    const TownAssetLodPolicy &policy, TownAssetLodState &state);

} // namespace devilution
