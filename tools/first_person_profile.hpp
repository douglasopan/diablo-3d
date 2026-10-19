#pragma once

#include <array>
#include <chrono>
#include <cstddef>

// Diagnostic-only wall-clock scopes. The private runner injects these into
// frozen source copies; this header is not linked into the production game.
namespace devilution::first_person_profile {
enum Stage : size_t {
	World, Buffers, Lighting, HorizonPrepare, ShadowValidate, SceneRecord,
	HorizonRecord, TerrainRecord, SceneryRecord, ArchitectureRecord,
	VegetationRecord, PropsRecord, ActorsRecord, GpuBegin, GpuShadow,
	GpuEnd, GpuVertexUpload, GpuDrawSubmission, GpuCopySubmission,
	ReadbackAllocation, ReadbackMapWait, ReadbackCpuCopy, GpuPublish,
	Atmosphere, SamplingResolve, Count
};
inline constexpr std::array<const char *, Count> Names {
	"world", "buffers", "lighting", "horizon_prepare", "shadow_validate",
	"scene_record", "horizon_record", "terrain_record", "scenery_record",
	"architecture_record", "vegetation_record", "props_record", "actors_record",
	"gpu_begin", "gpu_shadow", "gpu_end", "gpu_vertex_upload",
	"gpu_draw_submission", "gpu_copy_submission", "readback_allocation",
	"readback_map_wait", "readback_cpu_copy", "gpu_publish", "atmosphere", "sampling_resolve"
};
struct Result {
	std::array<double, Count> milliseconds {};
	std::array<size_t, Count> calls {};
	size_t textureUploads = 0;
	size_t uploadedCodeBytes = 0;
	size_t uploadedLutBytes = 0;
	size_t cacheBytesPeak = 0;
	size_t cachedTexturesPeak = 0;
	size_t failedUploadBytes = 0;
	size_t failedLutBytes = 0;
	bool gpuTimestampValid = false;
	double gpuDrawTimestampMs = 0;
	double gpuCopyTimestampMs = 0;
};
inline thread_local Result Current;
inline void Reset() { Current = {}; }
class Scope {
public:
	explicit Scope(Stage stage) : stage_(stage), start_(std::chrono::steady_clock::now()) { }
	~Scope() { Stop(); }
	void Stop()
	{
		if (!active_) return;
		Current.milliseconds[stage_] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
		++Current.calls[stage_];
		active_ = false;
	}
	Scope(const Scope &) = delete;
	Scope &operator=(const Scope &) = delete;
private:
	Stage stage_;
	std::chrono::steady_clock::time_point start_;
	bool active_ = true;
};
} // namespace devilution::first_person_profile
