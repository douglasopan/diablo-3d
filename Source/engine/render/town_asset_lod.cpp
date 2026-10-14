#include "engine/render/town_asset_lod.hpp"

#include <cmath>

namespace devilution {
namespace {

bool IsFinitePositive(double value)
{
	return std::isfinite(value) && value > 0;
}

bool HasValidMetadata(const TownAssetLodLevel &level, bool reference)
{
	if (level.stableKey == 0 || !std::isfinite(level.geometricError) || level.geometricError < 0)
		return false;
	if (reference)
		return level.geometricError == 0 && level.maxProjectedDiameterPixels == std::numeric_limits<double>::infinity();
	return IsFinitePositive(level.maxProjectedDiameterPixels);
}

bool HasValidLevels(std::span<const TownAssetLodLevel> levels, size_t finest)
{
	double lastError = 0;
	double lastDiameter = std::numeric_limits<double>::infinity();
	for (size_t i = finest; i < levels.size(); ++i) {
		const auto &level = levels[i];
		if (!level.drawable)
			continue;
		if (!HasValidMetadata(level, i == finest)
		    || level.geometricError < lastError || level.maxProjectedDiameterPixels > lastDiameter)
			return false;
		for (size_t previous = finest; previous < i; ++previous) {
			if (levels[previous].drawable && levels[previous].stableKey == level.stableKey)
				return false;
		}
		lastError = level.geometricError;
		lastDiameter = level.maxProjectedDiameterPixels;
	}
	return true;
}

bool HasValidPolicy(const TownAssetLodPolicy &policy)
{
	return IsFinitePositive(policy.pixelTolerance)
	    && std::isfinite(policy.refineMargin) && policy.refineMargin >= 0 && policy.refineMargin < 1
	    && std::isfinite(policy.coarsenMargin) && policy.coarsenMargin >= 0 && policy.coarsenMargin < 1;
}

bool IsAutomatic(const TownAssetLodLevel &level)
{
	return level.drawable && level.errorVerified;
}

bool IsResident(const TownAssetLodLevel &level)
{
	return IsAutomatic(level) && level.available && level.resident;
}

bool MeetsLimits(const TownAssetLodLevel &level, double pixelsPerObjectUnit,
    double diameter, const TownAssetLodPolicy &policy, double factor)
{
	const double error = level.geometricError * pixelsPerObjectUnit;
	// Divide measured values instead of multiplying limits: finite huge limits
	// must not overflow into infinity and admit an overflowing measured error.
	return std::isfinite(error) && error / factor <= policy.pixelTolerance
	    && diameter / factor <= level.maxProjectedDiameterPixels;
}

size_t CoarsestEligible(std::span<const TownAssetLodLevel> levels, size_t finest,
    size_t end, double pixelsPerObjectUnit, double diameter,
    const TownAssetLodPolicy &policy, double factor)
{
	size_t result = finest;
	for (size_t i = finest + 1; i < end; ++i) {
		if (IsAutomatic(levels[i]) && MeetsLimits(levels[i], pixelsPerObjectUnit, diameter, policy, factor))
			result = i;
	}
	return result;
}

} // namespace

TownAssetLodSelection SelectTownAssetLod(std::span<const TownAssetLodLevel> levels,
    const TownAssetLodIdentity &identity, const TownAssetLodProjection &projection,
    const TownAssetLodPolicy &policy, TownAssetLodState &state)
{
	TownAssetLodSelection result;
	size_t finest = TownAssetNoLod;
	for (size_t i = 0; i < levels.size(); ++i) {
		if (levels[i].drawable) {
			finest = i;
			break;
		}
	}
	if (finest == TownAssetNoLod || !IsAutomatic(levels[finest]) || !HasValidMetadata(levels[finest], true)) {
		result.stateReset = state.initialized;
		state = {};
		return result;
	}

	const bool validLevels = HasValidLevels(levels, finest);
	const bool validPolicy = HasValidPolicy(policy);
	const double pixelsPerObjectUnit = projection.pixelsPerWorldUnit * projection.worldUnitsPerObjectUnit;
	const double diameter = 2 * projection.boundingRadius * pixelsPerObjectUnit;
	const bool validProjection = projection.valid && IsFinitePositive(projection.pixelsPerWorldUnit)
	    && IsFinitePositive(projection.worldUnitsPerObjectUnit) && IsFinitePositive(projection.boundingRadius)
	    && IsFinitePositive(pixelsPerObjectUnit) && IsFinitePositive(diameter);
	if (validProjection)
		result.projectedDiameterPixels = diameter;

	const bool wasInitialized = state.initialized;
	const bool sameIdentity = state.identity == identity;
	const bool validState = wasInitialized && sameIdentity && state.desiredLevel >= finest
	    && state.desiredLevel < levels.size() && IsAutomatic(levels[state.desiredLevel])
	    && state.desiredLevelKey == levels[state.desiredLevel].stableKey;
	result.stateReset = !validState;
	const size_t previous = validState ? state.desiredLevel : finest;
	result.desiredLevel = finest;

	if (!validLevels) {
		result.reason = TownAssetLodReason::InvalidLevels;
	} else if (!validPolicy) {
		result.reason = TownAssetLodReason::InvalidPolicy;
	} else if (!validProjection) {
		result.reason = TownAssetLodReason::InvalidProjection;
	} else if (projection.intersectsNearPlane) {
		result.reason = TownAssetLodReason::IntersectsNearPlane;
	} else {
		const size_t nominal = CoarsestEligible(levels, finest, levels.size(), pixelsPerObjectUnit, diameter, policy, 1);
		if (!validState) {
			result.desiredLevel = nominal;
			result.reason = wasInitialized ? TownAssetLodReason::IdentityReset : TownAssetLodReason::InitialSelection;
		} else if (previous != finest && !MeetsLimits(levels[previous], pixelsPerObjectUnit, diameter, policy, 1 + policy.refineMargin)) {
			// Refinement crosses the relaxed threshold, then restores nominal quality.
			result.desiredLevel = CoarsestEligible(levels, finest, previous, pixelsPerObjectUnit, diameter, policy, 1);
			result.reason = TownAssetLodReason::Refined;
		} else {
			result.desiredLevel = previous;
			for (size_t i = previous + 1; i < levels.size(); ++i) {
				if (IsAutomatic(levels[i]) && MeetsLimits(levels[i], pixelsPerObjectUnit, diameter, policy, 1 - policy.coarsenMargin))
					result.desiredLevel = i;
			}
			result.reason = result.desiredLevel == previous ? TownAssetLodReason::Retained : TownAssetLodReason::Coarsened;
		}
		result.hysteresisApplied = result.desiredLevel != nominal;
	}

	state.identity = identity;
	state.desiredLevel = result.desiredLevel;
	state.desiredLevelKey = levels[result.desiredLevel].stableKey;
	state.initialized = true;
	if (validProjection)
		result.desiredProjectedErrorPixels = levels[result.desiredLevel].geometricError * pixelsPerObjectUnit;

	if (IsResident(levels[result.desiredLevel])) {
		result.drawableLevel = result.desiredLevel;
		result.fallback = TownAssetLodFallback::None;
	} else {
		// Prefer the closest finer resident. Availability never changes the state.
		for (size_t i = result.desiredLevel; i > finest; --i) {
			if (IsResident(levels[i - 1])) {
				result.drawableLevel = i - 1;
				result.fallback = TownAssetLodFallback::FinerResident;
				break;
			}
		}
		if (result.drawableLevel == TownAssetNoLod && validLevels) {
			size_t firstCoarser = TownAssetNoLod;
			for (size_t i = result.desiredLevel + 1; i < levels.size(); ++i) {
				if (!IsResident(levels[i]))
					continue;
				if (firstCoarser == TownAssetNoLod)
					firstCoarser = i;
				if (validProjection && validPolicy && !projection.intersectsNearPlane
				    && MeetsLimits(levels[i], pixelsPerObjectUnit, diameter, policy, 1)) {
					result.drawableLevel = i;
					result.fallback = TownAssetLodFallback::CoarserResidentAdequate;
					break;
				}
			}
			if (result.drawableLevel == TownAssetNoLod && firstCoarser != TownAssetNoLod) {
				result.drawableLevel = firstCoarser;
				result.fallback = TownAssetLodFallback::CoarserResidentInadequate;
			}
		}
	}
	if (result.drawableLevel != TownAssetNoLod) {
		result.drawableMeetsQuality = result.drawableLevel == finest
		    || (validProjection && validPolicy && !projection.intersectsNearPlane
		        && MeetsLimits(levels[result.drawableLevel], pixelsPerObjectUnit, diameter, policy, 1));
		if (result.drawableLevel == finest)
			result.drawableProjectedErrorPixels = 0;
		else if (validProjection)
			result.drawableProjectedErrorPixels = levels[result.drawableLevel].geometricError * pixelsPerObjectUnit;
	}
	return result;
}

} // namespace devilution
