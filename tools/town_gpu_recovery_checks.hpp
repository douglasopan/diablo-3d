#pragma once

#include <limits>
#include <string>

#include "engine/render/town_gpu_recovery.hpp"

namespace devilution {

/** Pure state-machine checks; fake clock, no device/profile/simulation writes. */
template <typename CheckFn>
void RunTownGpuRecoveryPolicyChecks(CheckFn check)
{
	TownGpuRecoveryPolicy policy;
	TownGpuRecoveryKey original;
	original.rasterWidth = 1920;
	original.rasterHeight = 1080;
	original.eye = { 32, 10, 32 };
	check(policy.ShouldAttempt(true, original, 0), "GPU recovery permits a healthy requested device");
	check(!policy.ShouldAttempt(false, original, 0), "GPU recovery respects explicit CPU selection");
	const auto changedWorkload = [&](TownGpuRecoveryKey changed, const std::string &label) {
		policy.RecordFailure(TownGpuFailureKind::Capacity, original, 100);
		check(!policy.ShouldAttempt(true, original, 100000), label + " unchanged failed workload is not polled repeatedly");
		check(!policy.ShouldAttempt(true, changed, 1099), label + " changed workload respects cooldown");
		check(policy.ShouldAttempt(true, changed, 1100), label + " changed workload retries after cooldown");
		policy.RecordFailure(TownGpuFailureKind::Capacity, changed, 1100);
		check(!policy.ShouldAttempt(true, original, 1101), label + " another failed attempt renews cooldown");
		check(!policy.ShouldAttempt(true, changed, 999999), label + " failed retry consumes its workload key");
		policy.RecordSuccess();
		check(policy.failureKind() == TownGpuFailureKind::None && policy.ShouldAttempt(true, changed, 1101), label + " success restores normal drawing");
	};
	TownGpuRecoveryKey changed = original;
	++changed.cameraRevision;
	changedWorkload(changed, "camera orbit/zoom");
	changed = original;
	changed.eye[0] += 0.25F;
	changedWorkload(changed, "walking without a rig revision");
	changed = original;
	changed.center[0] += 160;
	changedWorkload(changed, "panel changes the projection center");
	changed = original;
	++changed.sceneRevision;
	changedWorkload(changed, "scene revision");
	changed = original;
	changed.rasterWidth = 640;
	changed.rasterHeight = 480;
	changedWorkload(changed, "raster resize");
	changed = original;
	changed.sampling = 2;
	changedWorkload(changed, "sampling");
	changed = original;
	changed.horizon = !changed.horizon;
	changedWorkload(changed, "horizon visibility");
	changed = original;
	changed.culling = !changed.culling;
	changedWorkload(changed, "frustum visibility");
	changed = original;
	changed.nativeZoom = !changed.nativeZoom;
	changedWorkload(changed, "native zoom");
	for (const auto kind : { TownGpuFailureKind::InvalidInput, TownGpuFailureKind::Device, TownGpuFailureKind::Unsupported }) {
		policy.RecordFailure(kind, original, 100);
		check(!policy.ShouldAttempt(true, changed, 100000), "non-capacity errors do not retry on camera changes");
		const bool expectedReset = kind != TownGpuFailureKind::InvalidInput;
		check(policy.DisableOrReset() == expectedReset, "manual OFF/ON requests device recreation only when appropriate");
		check(!policy.DisableOrReset() && policy.ShouldAttempt(true, original, 101), "manual reset clears state exactly once");
	}
	policy.RecordFailure(TownGpuFailureKind::None, original, 0);
	check(policy.failureKind() == TownGpuFailureKind::InvalidInput && !policy.ShouldAttempt(true, changed, 100000), "missing backend cause treats output validation as invalid input");
	policy.RecordFailure(TownGpuFailureKind::Capacity, original, std::numeric_limits<uint32_t>::max() - 499);
	check(!policy.ShouldAttempt(true, changed, 499), "retry cooldown survives tick wrap before deadline");
	check(policy.ShouldAttempt(true, changed, 500), "retry cooldown survives tick wrap at deadline");
	check(!policy.DisableOrReset(), "capacity recovery keeps a healthy device and its cache");
}

} // namespace devilution
