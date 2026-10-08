#pragma once

#include <array>
#include <cstdint>

#include "engine/render/town_gpu.hpp"

namespace devilution {

/** Effective workload, including walking without a camera-rig revision. */
struct TownGpuRecoveryKey {
	uint64_t cameraRevision = 0;
	uint64_t sceneRevision = 0;
	std::array<float, 3> eye {};
	std::array<float, 2> center {};
	int rasterWidth = 0;
	int rasterHeight = 0;
	int sampling = 1;
	bool horizon = false;
	bool culling = false;
	bool nativeZoom = false;

	bool operator==(const TownGpuRecoveryKey &) const = default;
};

/** Capacity failures may recover after the workload changes. A cooldown also
 * applies while the player/camera keeps moving, preventing costly per-frame
 * retries. Invalid inputs and device failures require an explicit reset. */
class TownGpuRecoveryPolicy {
public:
	static constexpr uint32_t RetryDelayMilliseconds = 1000;

	bool ShouldAttempt(bool requested, const TownGpuRecoveryKey &key, uint32_t now) const
	{
		return requested && (failure_ == TownGpuFailureKind::None
		    || (failure_ == TownGpuFailureKind::Capacity && !(key == failedKey_)
		        && uint32_t(now - failedAt_) >= RetryDelayMilliseconds));
	}

	void RecordFailure(TownGpuFailureKind kind, const TownGpuRecoveryKey &key, uint32_t now)
	{
		// A caller-side output validation failure may have no backend error.
		failure_ = kind == TownGpuFailureKind::None ? TownGpuFailureKind::InvalidInput : kind;
		failedKey_ = key;
		failedAt_ = now;
	}

	void RecordSuccess() { failure_ = TownGpuFailureKind::None; }
	TownGpuFailureKind failureKind() const { return failure_; }

	/** OFF/ON recreates resources after a removed/unavailable device. Healthy
	 * capacity failures retain resident geometry/textures across recovery. */
	bool DisableOrReset()
	{
		const bool resetDevice = failure_ == TownGpuFailureKind::Device || failure_ == TownGpuFailureKind::Unsupported;
		*this = {};
		return resetDevice;
	}

private:
	TownGpuFailureKind failure_ = TownGpuFailureKind::None;
	TownGpuRecoveryKey failedKey_;
	uint32_t failedAt_ = 0;
};

} // namespace devilution
