// Standalone synthetic selector checks. No MPQ, renderer, game globals or window.
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <span>

#include "engine/render/town_asset_lod.hpp"

namespace {
using namespace devilution;

int Checks = 0;
int Failures = 0;

void Check(bool passed, const char *description, int line)
{
	++Checks;
	if (!passed) {
		++Failures;
		std::cerr << "FAIL line " << line << ": " << description << '\n';
	}
}

#define CHECK(condition, description) Check(condition, description, __LINE__)

constexpr TownAssetLodIdentity Identity { 17, 23, 41 };

std::array<TownAssetLodLevel, 4> Levels()
{
	return { {
	    // A preserved dense source with no UV/material is not a draw candidate.
	    { 101, 0, std::numeric_limits<double>::infinity(), false, false, true, true },
	    { 102, 0, std::numeric_limits<double>::infinity(), true, true, true, true },
	    { 103, 1.0 / 64, 256, true, true, true, true },
	    { 104, 1.0 / 16, 64, true, true, true, true },
	} };
}

TownAssetLodProjection Projection(double pixelsPerWorldUnit)
{
	return { pixelsPerWorldUnit, 1, 1, true, false };
}

TownAssetLodSelection Select(std::span<const TownAssetLodLevel> levels,
    double pixelsPerWorldUnit, TownAssetLodState &state, const TownAssetLodPolicy &policy = {})
{
	return SelectTownAssetLod(levels, Identity, Projection(pixelsPerWorldUnit), policy, state);
}

void TestNominalSelection()
{
	const auto levels = Levels();
	TownAssetLodState state;
	auto result = Select(levels, 100, state);
	CHECK(result.desiredLevel == 1 && result.drawableLevel == 1, "near uses drawable reference, never source-only master");
	CHECK(result.reason == TownAssetLodReason::InitialSelection && result.stateReset, "initial state is reported");
	CHECK(result.drawableMeetsQuality && result.fallback == TownAssetLodFallback::None, "resident finest needs no fallback");
	CHECK(result.projectedDiameterPixels == 200 && result.desiredProjectedErrorPixels == 0, "screen metrics use radius and error");

	state = {};
	result = Select(levels, 64, state);
	CHECK(result.desiredLevel == 2 && result.desiredProjectedErrorPixels == 1, "nominal error boundary is inclusive");
	state = {};
	result = Select(levels, 16, state);
	CHECK(result.desiredLevel == 3 && result.desiredProjectedErrorPixels == 1, "coarsest nominal error boundary is inclusive");
	state = {};
	result = Select(levels, 1, state);
	CHECK(result.desiredLevel == 3, "far picks coarsest eligible");
	result = Select(levels, 1000, state);
	CHECK(result.desiredLevel == 1 && result.reason == TownAssetLodReason::Refined, "large camera movement refines across multiple levels");
	result = Select(levels, 1, state);
	CHECK(result.desiredLevel == 3 && result.reason == TownAssetLodReason::Coarsened, "large distance change coarsens across multiple levels");

	auto diameterLimited = levels;
	diameterLimited[2].geometricError = 0;
	diameterLimited[2].maxProjectedDiameterPixels = 64;
	diameterLimited[3].geometricError = 0;
	diameterLimited[3].maxProjectedDiameterPixels = 16;
	state = {};
	result = Select(diameterLimited, 40, state);
	CHECK(result.desiredLevel == 1, "size threshold independently protects near detail despite zero error");
	state = {};
	result = Select(diameterLimited, 32, state);
	CHECK(result.desiredLevel == 2, "nominal diameter boundary is inclusive");
	state = {};
	result = Select(diameterLimited, 8, state);
	CHECK(result.desiredLevel == 3, "coarse size boundary is inclusive");

	auto unverified = levels;
	unverified[3].errorVerified = false;
	state = {};
	result = Select(unverified, 1, state);
	CHECK(result.desiredLevel == 2 && result.drawableLevel == 2, "unverified error never participates in automatic selection");
	unverified[1].errorVerified = false;
	result = Select(unverified, 1, state);
	CHECK(result.desiredLevel == TownAssetNoLod && result.reason == TownAssetLodReason::NoDrawableReference, "unverified reference cannot silently promote an approximate LOD");
	CHECK(!state.initialized, "missing reference invalidates previous state");
}

void TestHysteresis()
{
	auto levels = Levels();
	levels[3].drawable = false;
	const TownAssetLodPolicy policy { 1, 0.25, 0.25 };
	TownAssetLodState state;
	auto result = Select(levels, 80, state, policy);
	CHECK(result.desiredLevel == 1, "error-hysteresis setup is finest");
	result = Select(levels, 60, state, policy);
	CHECK(result.desiredLevel == 1 && result.hysteresisApplied, "coarsen holds finer inside nominal error boundary");
	result = Select(levels, 48, state, policy);
	CHECK(result.desiredLevel == 2 && result.reason == TownAssetLodReason::Coarsened, "coarsen error uses stricter boundary");
	result = Select(levels, 72, state, policy);
	CHECK(result.desiredLevel == 2 && result.hysteresisApplied, "refine holds coarse inside relaxed error boundary");
	CHECK(!result.drawableMeetsQuality, "nominal quality metric exposes allowed hysteresis excess");
	result = Select(levels, 80, state, policy);
	CHECK(result.desiredLevel == 2, "relaxed error boundary is inclusive");
	result = Select(levels, 80.01, state, policy);
	CHECK(result.desiredLevel == 1 && result.reason == TownAssetLodReason::Refined, "refine restores nominal quality after relaxed error boundary");

	levels[2].geometricError = 0;
	levels[2].maxProjectedDiameterPixels = 128;
	state = {};
	result = Select(levels, 80, state, policy);
	CHECK(result.desiredLevel == 1, "size-hysteresis setup is finest");
	result = Select(levels, 60, state, policy);
	CHECK(result.desiredLevel == 1 && result.hysteresisApplied, "coarsen holds finer inside nominal size boundary");
	result = Select(levels, 48, state, policy);
	CHECK(result.desiredLevel == 2, "coarsen size uses stricter boundary");
	result = Select(levels, 80, state, policy);
	CHECK(result.desiredLevel == 2 && result.hysteresisApplied, "refine size uses relaxed inclusive boundary");
	result = Select(levels, 80.01, state, policy);
	CHECK(result.desiredLevel == 1, "diameter alone triggers refinement");

	// Alternating samples straddle the nominal threshold but not the two bands.
	for (int i = 0; i < 200; ++i) {
		result = Select(levels, i % 2 == 0 ? 63 : 65, state, policy);
		CHECK(result.desiredLevel == 1, "jitter does not oscillate from finer state");
	}
	Select(levels, 40, state, policy);
	for (int i = 0; i < 200; ++i) {
		result = Select(levels, i % 2 == 0 ? 63 : 65, state, policy);
		CHECK(result.desiredLevel == 2, "jitter does not oscillate from coarse state");
	}

	const TownAssetLodPolicy asymmetric { 1, 0.5, 0.125 };
	state = {};
	Select(levels, 80, state, asymmetric);
	result = Select(levels, 56, state, asymmetric);
	CHECK(result.desiredLevel == 2, "coarsen margin has its own threshold");
	result = Select(levels, 96, state, asymmetric);
	CHECK(result.desiredLevel == 2, "independent refine margin is applied");
	result = Select(levels, 96.01, state, asymmetric);
	CHECK(result.desiredLevel == 1, "independent refine margin exits at its own threshold");
}

void TestResidency()
{
	auto levels = Levels();
	TownAssetLodState state;
	levels[3].resident = false;
	auto result = Select(levels, 1, state);
	CHECK(result.desiredLevel == 3 && result.drawableLevel == 2, "nonresident desired chooses closest finer resident");
	CHECK(result.fallback == TownAssetLodFallback::FinerResident && result.drawableMeetsQuality, "finer fallback is explicitly adequate");
	CHECK(state.desiredLevel == 3, "residency fallback never changes desired hysteresis state");
	levels[3].resident = true;
	result = Select(levels, 1, state);
	CHECK(result.drawableLevel == 3 && result.fallback == TownAssetLodFallback::None, "new residency immediately restores desired without artificial hysteresis");
	levels[3].available = false;
	result = Select(levels, 1, state);
	CHECK(result.drawableLevel == 2 && result.fallback == TownAssetLodFallback::FinerResident, "resident flag alone cannot override unavailable bytes");
	levels[2].resident = false;
	result = Select(levels, 1, state);
	CHECK(result.drawableLevel == 1, "finer fallback skips nonresident intermediate level");
	levels[1].resident = false;
	result = Select(levels, 1, state);
	CHECK(result.drawableLevel == TownAssetNoLod && result.fallback == TownAssetLodFallback::NoResidentLevel, "no resident drawable is reported without selecting source master");
	CHECK(!result.drawableMeetsQuality && std::isinf(result.drawableProjectedErrorPixels), "no resident result has no misleading quality claim");

	levels = Levels();
	levels[1].available = false;
	state = {};
	result = Select(levels, 100, state);
	CHECK(result.desiredLevel == 1 && result.drawableLevel == 2, "near quality request remains finest even with only coarse resident");
	CHECK(result.fallback == TownAssetLodFallback::CoarserResidentInadequate && !result.drawableMeetsQuality, "near coarse fallback needs explicit caller decision");
	CHECK(state.desiredLevel == 1, "inadequate fallback does not poison ideal state");

	// The previous finer state can be retained by the coarsen band while a
	// resident coarse level already satisfies nominal quality.
	result = Select(levels, 60, state);
	CHECK(result.desiredLevel == 1 && result.drawableLevel == 2, "hysteresis retains unavailable finer desired level");
	CHECK(result.fallback == TownAssetLodFallback::CoarserResidentAdequate && result.drawableMeetsQuality, "coarser resident may satisfy nominal quality inside coarsen band");
	levels[2].errorVerified = false;
	result = Select(levels, 60, state);
	CHECK(result.drawableLevel == 3 && result.fallback == TownAssetLodFallback::CoarserResidentInadequate, "unverified resident cannot be selected as fallback");
}

void TestInvalidInputs()
{
	const auto levels = Levels();
	const double invalidValues[] = { 0, -1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() };
	for (double invalid : invalidValues) {
		for (int field = 0; field < 3; ++field) {
			TownAssetLodState state;
			Select(levels, 1, state);
			auto projection = Projection(1);
			if (field == 0)
				projection.pixelsPerWorldUnit = invalid;
			else if (field == 1)
				projection.boundingRadius = invalid;
			else
				projection.worldUnitsPerObjectUnit = invalid;
			const auto result = SelectTownAssetLod(levels, Identity, projection, {}, state);
			CHECK(result.desiredLevel == 1 && result.reason == TownAssetLodReason::InvalidProjection, "invalid projection component immediately forces finest");
			CHECK(result.drawableMeetsQuality, "resident zero-error reference remains conservative on invalid projection");
		}
	}
	TownAssetLodState state;
	Select(levels, 1, state);
	auto projection = Projection(1);
	projection.valid = false;
	auto result = SelectTownAssetLod(levels, Identity, projection, {}, state);
	CHECK(result.desiredLevel == 1 && result.reason == TownAssetLodReason::InvalidProjection, "invalid flag immediately forces finest");
	projection = Projection(1);
	projection.intersectsNearPlane = true;
	Select(levels, 1, state);
	result = SelectTownAssetLod(levels, Identity, projection, {}, state);
	CHECK(result.desiredLevel == 1 && result.reason == TownAssetLodReason::IntersectsNearPlane, "near-plane bounds force finest regardless of hysteresis or apparent diameter");

	auto coarseOnlyResident = levels;
	coarseOnlyResident[1].resident = false;
	result = SelectTownAssetLod(coarseOnlyResident, Identity, projection, {}, state);
	CHECK(result.fallback == TownAssetLodFallback::CoarserResidentInadequate && !result.drawableMeetsQuality, "near-plane coarse fallback is never called adequate");
	projection = Projection(std::numeric_limits<double>::max());
	result = SelectTownAssetLod(levels, Identity, projection, {}, state);
	CHECK(result.reason == TownAssetLodReason::InvalidProjection && result.desiredLevel == 1, "projection arithmetic overflow is conservative");
	projection = Projection(std::numeric_limits<double>::denorm_min());
	projection.worldUnitsPerObjectUnit = std::numeric_limits<double>::denorm_min();
	result = SelectTownAssetLod(levels, Identity, projection, {}, state);
	CHECK(result.reason == TownAssetLodReason::InvalidProjection, "projection scale underflow is conservative");

	for (int field = 0; field < 3; ++field) {
		for (double invalid : invalidValues) {
			// Zero is permitted for hysteresis margins, but never for tolerance.
			if (field != 0 && invalid == 0)
				continue;
			TownAssetLodPolicy policy;
			if (field == 0)
				policy.pixelTolerance = invalid;
			else if (field == 1)
				policy.refineMargin = invalid;
			else
				policy.coarsenMargin = invalid;
			Select(levels, 1, state);
			result = Select(levels, 1, state, policy);
			CHECK(result.reason == TownAssetLodReason::InvalidPolicy && result.desiredLevel == 1, "invalid policy forces finest");
		}
	}
	result = Select(levels, 1, state, { 1, 1, 0 });
	CHECK(result.reason == TownAssetLodReason::InvalidPolicy, "refine margin must be below one");
	result = Select(levels, 1, state, { 1, 0, 1 });
	CHECK(result.reason == TownAssetLodReason::InvalidPolicy, "coarsen margin must be below one");

	for (int defect = 0; defect < 7; ++defect) {
		auto malformed = levels;
		if (defect == 0)
			malformed[3].geometricError = 1.0 / 128;
		else if (defect == 1)
			malformed[3].maxProjectedDiameterPixels = 512;
		else if (defect == 2)
			malformed[3].stableKey = malformed[2].stableKey;
		else if (defect == 3)
			malformed[3].geometricError = std::numeric_limits<double>::quiet_NaN();
		else if (defect == 4)
			malformed[3].maxProjectedDiameterPixels = std::numeric_limits<double>::infinity();
		else if (defect == 5)
			malformed[3].geometricError = -1;
		else
			malformed[3].stableKey = 0;
		Select(levels, 1, state);
		result = Select(malformed, 1, state);
		CHECK(result.desiredLevel == 1 && result.reason == TownAssetLodReason::InvalidLevels, "malformed coarse metadata forces valid finest reference");
		malformed[1].resident = false;
		result = Select(malformed, 1, state);
		CHECK(result.drawableLevel == TownAssetNoLod, "malformed metadata disables coarse fallback rather than guessing");
	}
	auto invalidReference = levels;
	invalidReference[1].geometricError = 0.01;
	result = Select(invalidReference, 1, state);
	CHECK(result.reason == TownAssetLodReason::NoDrawableReference && result.drawableLevel == TownAssetNoLod, "reference must have verified zero error");
	invalidReference = levels;
	invalidReference[1].maxProjectedDiameterPixels = 10000;
	result = Select(invalidReference, 1, state);
	CHECK(result.reason == TownAssetLodReason::NoDrawableReference, "reference has no size-based degradation limit");
	result = Select(std::span<const TownAssetLodLevel> {}, 1, state);
	CHECK(result.reason == TownAssetLodReason::NoDrawableReference, "empty levels are safe");
	result = Select(std::span<const TownAssetLodLevel>(levels).first(1), 1, state);
	CHECK(result.reason == TownAssetLodReason::NoDrawableReference, "source-only master never substitutes for drawable reference");
}

void TestIdentityAndScale()
{
	const auto levels = Levels();
	TownAssetLodState state;
	Select(levels, 100, state);
	auto result = Select(levels, 60, state);
	CHECK(result.desiredLevel == 1 && result.hysteresisApplied, "identity test starts inside finer hysteresis band");
	for (int field = 0; field < 3; ++field) {
		state = {};
		Select(levels, 100, state);
		auto identity = Identity;
		if (field == 0)
			++identity.assetKey;
		else if (field == 1)
			++identity.revision;
		else
			++identity.instanceKey;
		result = SelectTownAssetLod(levels, identity, Projection(60), {}, state);
		CHECK(result.stateReset && result.reason == TownAssetLodReason::IdentityReset && result.desiredLevel == 2, "asset/revision/instance change discards stale hysteresis");
	}
	state = {};
	Select(levels, 100, state);
	state.desiredLevelKey = 999;
	result = Select(levels, 60, state);
	CHECK(result.stateReset && result.desiredLevel == 2, "changed selected key also resets state conservatively");
	state.desiredLevel = TownAssetNoLod;
	result = Select(levels, 60, state);
	CHECK(result.stateReset && result.desiredLevel == 2, "out-of-range cached index is not dereferenced");

	TownAssetLodState nearState;
	TownAssetLodState farState;
	Select(levels, 100, nearState);
	Select(levels, 1, farState);
	CHECK(nearState.desiredLevel == 1 && farState.desiredLevel == 3, "separate instances keep independent hysteresis states");

	state = {};
	auto scaledProjection = Projection(32);
	scaledProjection.worldUnitsPerObjectUnit = 2;
	result = SelectTownAssetLod(levels, Identity, scaledProjection, {}, state);
	CHECK(result.desiredLevel == 2 && result.projectedDiameterPixels == 128 && result.desiredProjectedErrorPixels == 1, "uniform scale affects both error and bounding diameter");
	state = {};
	result = Select(levels, 32, state);
	CHECK(result.projectedDiameterPixels == 64 && result.desiredProjectedErrorPixels == 0.5, "unscaled projection has comparable object-space units");
}

void TestSweep()
{
	const auto levels = Levels();
	const TownAssetLodPolicy noHysteresis { 1, 0, 0 };
	TownAssetLodState state;
	size_t previous = 3;
	for (int step = 1; step <= 1600; ++step) {
		const double pixels = static_cast<double>(step) / 8;
		const auto result = Select(levels, pixels, state, noHysteresis);
		const size_t expected = pixels <= 16 ? 3 : pixels <= 64 ? 2 : 1;
		CHECK(result.desiredLevel == expected, "sweep follows analytic screen-error thresholds");
		CHECK(result.desiredLevel <= previous, "increasing screen size never coarsens with zero hysteresis");
		CHECK(result.drawableMeetsQuality && !result.hysteresisApplied, "zero-hysteresis sweep always satisfies nominal quality");
		previous = result.desiredLevel;
	}
	for (int step = 1600; step >= 1; --step) {
		const double pixels = static_cast<double>(step) / 8;
		const auto result = Select(levels, pixels, state, noHysteresis);
		const size_t expected = pixels <= 16 ? 3 : pixels <= 64 ? 2 : 1;
		CHECK(result.desiredLevel == expected, "reverse sweep restores analytic thresholds without history bias");
	}

	// A finite error can overflow after projection even while the diameter fits.
	auto hugeError = levels;
	hugeError[2].geometricError = std::numeric_limits<double>::max();
	hugeError[3].geometricError = std::numeric_limits<double>::max();
	state = {};
	const auto result = Select(hugeError, 2, state, { std::numeric_limits<double>::max(), 0.5, 0.5 });
	CHECK(result.desiredLevel == 1, "overflowing projected error never becomes eligible against huge tolerance");
}

} // namespace

int main()
{
	TestNominalSelection();
	TestHysteresis();
	TestResidency();
	TestInvalidInputs();
	TestIdentityAndScale();
	TestSweep();
	std::cout << "town_asset_lod_smoke: checks=" << Checks << " failures=" << Failures << '\n';
	return Failures == 0 ? 0 : 1;
}
