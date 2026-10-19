// Private D3D11 implementation, included inside town_gpu.cpp's anonymous
// namespace. No host/native state and no borrowed descriptor storage.
#include "engine/render/town_gpu_fire_volume_hlsl.inc"

constexpr size_t MaxFireVolumeBytecodeBytes = 2 * 1024 * 1024;
constexpr unsigned AdaptiveFireVolumeSteps = 12;
constexpr size_t FireVolumePrefixRecordsPerRay = AdaptiveFireVolumeSteps + 2;
constexpr size_t FireVolumePrefixBufferBytes = TownGpuMaxFireVolumeRays * FireVolumePrefixRecordsPerRay * sizeof(Float4);
static_assert(FireVolumePrefixBufferBytes == 14 * 1024 * 1024);
struct FireVolumeConstants {
	Float4 eyeHeightScale, rightBasis, upBasis, forwardBasis;
	Float4 cameraData, clipTime, sourceEnergy, config0, config1, context0, context1;
	UInt4 roi, control;
	std::array<UInt4, 64> palette, tone;
};
static_assert(sizeof(FireVolumeConstants) == 2256);
static_assert(offsetof(FireVolumeConstants, roi) == 176);
static_assert(offsetof(FireVolumeConstants, palette) == 208);
static_assert(offsetof(FireVolumeConstants, tone) == 1232);

std::array<ComOwner<ID3D11Buffer>, TownGpuMaxFireVolumeSources> FireVolumeBuffers;
std::array<ComOwner<ID3D11ComputeShader>, 3> FireVolumeShaders;
std::array<ComOwner<ID3D11ShaderResourceView>, 3> FireVolumeViews;
ComOwner<ID3D11UnorderedAccessView> FireVolumeColorView, FireVolumeFailureView;
ComOwner<ID3D11Buffer> FireVolumeFailureBuffer, FireVolumeFailureStaging;
ComOwner<ID3D11Buffer> FireVolumePrefixBuffer;
ComOwner<ID3D11UnorderedAccessView> FireVolumePrefixUav;
ComOwner<ID3D11ShaderResourceView> FireVolumePrefixSrv;
ComOwner<ID3D11Query> FireVolumeTimingDisjoint, FireVolumeTimingBegin, FireVolumeTimingEnd;
bool FireVolumeTimingAttempted = false;
size_t FrameFireVolumeCount = 0;
// Device-independent CPU bytecode, separate from the two unchanged25A slots.
std::array<ComOwner<ID3DBlob>, 3> FireVolumeBytecodes;
std::string_view FireVolumeBytecodeSource;
UINT FireVolumeBytecodeFlags = 0;
std::string FireVolumeColdFailure;

void ClearFireVolumeFrame()
{
	FrameFireVolumeCount = 0;
	FireVolumeStats.ready = FireVolumeStats.dispatched = FireVolumeStats.applied = false;
	FireVolumeStats.sourceCount = FireVolumeStats.plannedRays = 0;
	FireVolumeStats.roiPixels = FireVolumeStats.adaptiveSources = FireVolumeStats.compositePixels = 0;
	FireVolumeStats.maximumSamplingStride = 1;
	FireVolumeStats.prefixBufferBytes = FireVolumePrefixBuffer ? FireVolumePrefixBufferBytes : 0;
	FireVolumeStats.commandMilliseconds = 0;
	FireVolumeStats.gpuTimingKnown = false; FireVolumeStats.gpuMilliseconds = -1;
	FireVolumeStats.failure.clear();
}

void ResetFireVolumeTargetViews()
{
	for (auto &view : FireVolumeViews) view.reset();
	FireVolumeColorView.reset();
}

void ResetFireVolumeResources()
{
	ClearFireVolumeFrame(); ResetFireVolumeTargetViews();
	for (auto &buffer : FireVolumeBuffers) buffer.reset();
	for (auto &shader : FireVolumeShaders) shader.reset();
	FireVolumeFailureView.reset();
	FireVolumePrefixSrv.reset(); FireVolumePrefixUav.reset(); FireVolumePrefixBuffer.reset();
	FireVolumeStats.prefixBufferBytes = 0;
	FireVolumeFailureBuffer.reset(); FireVolumeFailureStaging.reset();
	FireVolumeTimingDisjoint.reset(); FireVolumeTimingBegin.reset(); FireVolumeTimingEnd.reset();
	FireVolumeTimingAttempted = false;
	FireVolumeColdFailure.clear();
	// The owned CPU blob and session counters deliberately survive device reset.
}

bool RefuseFireVolume(const std::string &reason, HRESULT result = S_OK)
{
	FireVolumeStats.failure = reason;
	if (FAILED(result)) FireVolumeStats.failure += " (HRESULT " + std::to_string(static_cast<uint32_t>(result)) + ")";
	return false; // Optional preflight never calls Fail or invalidates the frame.
}

bool InjectFireVolume(TownGpuFireVolumeFault phase)
{
	if (FireVolumeFault != phase) return false;
	FireVolumeFault = TownGpuFireVolumeFault::None;
	return true;
}

bool PrepareFireVolumeShader(size_t slot)
{
	const char *entries[] { "CS", "CSField", "CSComposite" };
	const char *details[] {
		"Source/engine/render/town_gpu_fire_volume_hlsl.inc :: CS / cs_5_0",
		"Source/engine/render/town_gpu_fire_volume_hlsl.inc :: CSField / cs_5_0",
		"Source/engine/render/town_gpu_fire_volume_hlsl.inc :: CSComposite / cs_5_0"
	};
	if (slot >= FireVolumeShaders.size()) return RefuseFireVolume("Invalid fire shader slot");
	const std::string_view source { FireVolumeShaderSource, sizeof(FireVolumeShaderSource) - 1 };
	if (FireVolumeBytecodeSource != source || FireVolumeBytecodeFlags != ShaderCompileFlags) {
		for (auto &code : FireVolumeBytecodes) code.reset();
		for (auto &shader : FireVolumeShaders) shader.reset();
		FireVolumeStats.retainedBytecodeBytes = 0;
		FireVolumeStats.retainedBytecodeBytesByEntry = {};
		FireVolumeBytecodeSource = source; FireVolumeBytecodeFlags = ShaderCompileFlags;
	}
	if (FireVolumeShaders[slot]) return true;
	if (!FireVolumeColdFailure.empty()) return RefuseFireVolume(FireVolumeColdFailure);
	ComOwner<ID3DBlob> code;
	if (FireVolumeBytecodes[slot]) {
		FireVolumeBytecodes[slot]->AddRef(); *code.put() = FireVolumeBytecodes[slot].get();
		if (FireVolumeStats.shaderCacheHits != std::numeric_limits<uint64_t>::max()) ++FireVolumeStats.shaderCacheHits;
		loading_ui::Report("3D rendering", "Reusing fire shader", details[slot]);
	} else {
		ComOwner<ID3DBlob> errors;
		diag::StageScope diagnosticCompile(diag::SessionLogger(), diag::Stage::ShaderCompile, diag::SessionFrame(), diag::SampleSessionFrame());
		loading_ui::Report("3D rendering", "Compiling fire shader", details[slot]);
		loading_ui::SetProgress(0, 0);
		const auto compileStart = Clock::now();
		const HRESULT compiled = loading_ui::RunResponsive([&] {
			return D3DCompile(FireVolumeShaderSource, sizeof(FireVolumeShaderSource) - 1, "town_gpu_fire_volume", nullptr, nullptr,
			    entries[slot], "cs_5_0", ShaderCompileFlags, 0, code.put(), errors.put());
		});
		FireVolumeStats.lastShaderCompileMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - compileStart).count();
		if (FireVolumeStats.shaderCompilations != std::numeric_limits<uint64_t>::max()) ++FireVolumeStats.shaderCompilations;
		if (FAILED(compiled)) {
			std::string reason = std::string("Compile fire volume ") + entries[slot];
			if (errors) reason.append(": ").append(static_cast<const char *>(errors->GetBufferPointer()), std::min<size_t>(errors->GetBufferSize(),4096));
			FireVolumeColdFailure = reason; return RefuseFireVolume(reason, compiled);
		}
		size_t retained = 0;
		for (const auto &entry : FireVolumeBytecodes) if (entry) retained += entry->GetBufferSize();
		if (!code || code->GetBufferSize() == 0 || retained > MaxFireVolumeBytecodeBytes
		    || code->GetBufferSize() > MaxFireVolumeBytecodeBytes - retained) {
			FireVolumeColdFailure = "Fire volume combined CPU bytecode exceeds separate 2 MiB budget";
			return RefuseFireVolume(FireVolumeColdFailure);
		}
		code->AddRef(); *FireVolumeBytecodes[slot].put() = code.get();
		FireVolumeStats.retainedBytecodeBytes = retained + code->GetBufferSize();
		FireVolumeStats.retainedBytecodeBytesByEntry[slot] = code->GetBufferSize();
	}
	if (!Device || !Context) return RefuseFireVolume("Fire volume device changed during shader preparation");
	const HRESULT result = Device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, FireVolumeShaders[slot].put());
	if (FAILED(result)) return RefuseFireVolume("Create fire volume compute shader", result);
	return true;
}

bool CreateFireVolumeObjects(bool adaptive)
{
	if (!Device || !Context) return RefuseFireVolume("Fire volume needs a current D3D11 device");
	UINT support = 0;
	const HRESULT supported = Device->CheckFormatSupport(DXGI_FORMAT_R32_UINT, &support);
	if (FAILED(supported) || (support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) == 0)
		return RefuseFireVolume("R32_UINT fire volume UAV unavailable", supported);
	for (size_t index : { 0U, 2U, 4U, 3U }) {
		if (!Targets[index].texture) return RefuseFireVolume("Fire volume target unavailable");
		D3D11_TEXTURE2D_DESC description {}; Targets[index].texture->GetDesc(&description);
		const UINT required = index == 3 ? D3D11_BIND_UNORDERED_ACCESS : D3D11_BIND_SHADER_RESOURCE;
		if ((description.BindFlags & required) == 0)
			return RefuseFireVolume("Original RTV-only targets retained; optional fire compute declined");
	}
	if (!PrepareFireVolumeShader(0)) return false;
	if (adaptive && (!PrepareFireVolumeShader(1) || !PrepareFireVolumeShader(2))) return false;
	if (adaptive && (!FireVolumePrefixBuffer || !FireVolumePrefixUav || !FireVolumePrefixSrv)) {
		FireVolumePrefixSrv.reset(); FireVolumePrefixUav.reset(); FireVolumePrefixBuffer.reset();
		FireVolumeStats.prefixBufferBytes = 0;
		ComOwner<ID3D11Buffer> prefixBuffer;
		ComOwner<ID3D11UnorderedAccessView> prefixUav;
		ComOwner<ID3D11ShaderResourceView> prefixSrv;
		D3D11_BUFFER_DESC description {};
		description.ByteWidth = static_cast<UINT>(FireVolumePrefixBufferBytes);
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		description.StructureByteStride = sizeof(Float4);
		HRESULT result = Device->CreateBuffer(&description, nullptr, prefixBuffer.put());
		if (FAILED(result)) return RefuseFireVolume("Create bounded fire prefix buffer", result);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav {};
		uav.Format = DXGI_FORMAT_UNKNOWN; uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.NumElements = TownGpuMaxFireVolumeRays * FireVolumePrefixRecordsPerRay;
		result = Device->CreateUnorderedAccessView(prefixBuffer.get(), &uav, prefixUav.put());
		if (FAILED(result)) return RefuseFireVolume("Create fire prefix UAV", result);
		D3D11_SHADER_RESOURCE_VIEW_DESC srv {};
		srv.Format = DXGI_FORMAT_UNKNOWN; srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv.Buffer.NumElements = uav.Buffer.NumElements;
		result = Device->CreateShaderResourceView(prefixBuffer.get(), &srv, prefixSrv.put());
		if (FAILED(result)) return RefuseFireVolume("Create fire prefix SRV", result);
		FireVolumePrefixBuffer = std::move(prefixBuffer);
		FireVolumePrefixUav = std::move(prefixUav);
		FireVolumePrefixSrv = std::move(prefixSrv);
		FireVolumeStats.prefixBufferBytes = FireVolumePrefixBufferBytes;
	}
	const size_t indices[] { 0, 2, 4 };
	for (size_t i = 0; i < FireVolumeViews.size(); ++i) {
		if (FireVolumeViews[i]) continue;
		const HRESULT result = Device->CreateShaderResourceView(Targets[indices[i]].texture.get(), nullptr, FireVolumeViews[i].put());
		if (FAILED(result)) return RefuseFireVolume("Create fire volume world SRV", result);
	}
	if (!FireVolumeColorView) {
		const HRESULT result = Device->CreateUnorderedAccessView(Targets[3].texture.get(), nullptr, FireVolumeColorView.put());
		if (FAILED(result)) return RefuseFireVolume("Create fire volume ARGB UAV", result);
	}
	if (!FireVolumeFailureBuffer || !FireVolumeFailureStaging || !FireVolumeFailureView) {
		FireVolumeFailureView.reset(); FireVolumeFailureBuffer.reset(); FireVolumeFailureStaging.reset();
		D3D11_BUFFER_DESC description {};
		description.ByteWidth = sizeof(uint32_t); description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; description.StructureByteStride = sizeof(uint32_t);
		HRESULT result = Device->CreateBuffer(&description, nullptr, FireVolumeFailureBuffer.put());
		if (FAILED(result)) return RefuseFireVolume("Create fire volume failure buffer", result);
		D3D11_UNORDERED_ACCESS_VIEW_DESC view {};
		view.Format = DXGI_FORMAT_UNKNOWN; view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; view.Buffer.NumElements = 1;
		result = Device->CreateUnorderedAccessView(FireVolumeFailureBuffer.get(), &view, FireVolumeFailureView.put());
		if (FAILED(result)) return RefuseFireVolume("Create fire volume failure UAV", result);
		description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		description.MiscFlags = 0; description.StructureByteStride = 0;
		result = Device->CreateBuffer(&description, nullptr, FireVolumeFailureStaging.put());
		if (FAILED(result)) return RefuseFireVolume("Create fire volume failure staging", result);
	}
	for (auto &buffer : FireVolumeBuffers) {
		if (buffer) continue;
		D3D11_BUFFER_DESC description {};
		description.ByteWidth = sizeof(FireVolumeConstants); description.Usage = D3D11_USAGE_DYNAMIC;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER; description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		const HRESULT result = Device->CreateBuffer(&description, nullptr, buffer.put());
		if (FAILED(result)) return RefuseFireVolume("Create fire volume constant buffer", result);
	}
	if (!FireVolumeTimingAttempted) {
		FireVolumeTimingAttempted = true;
		D3D11_QUERY_DESC query {}; query.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		HRESULT result = Device->CreateQuery(&query, FireVolumeTimingDisjoint.put());
		query.Query = D3D11_QUERY_TIMESTAMP;
		if (SUCCEEDED(result)) result = Device->CreateQuery(&query, FireVolumeTimingBegin.put());
		if (SUCCEEDED(result)) result = Device->CreateQuery(&query, FireVolumeTimingEnd.put());
		if (FAILED(result)) {
			FireVolumeTimingDisjoint.reset(); FireVolumeTimingBegin.reset(); FireVolumeTimingEnd.reset();
		}
	}
	return true;
}

std::array<UInt4, TownGpuMaxFireVolumeSources> FrameFireVolumeRois {};
std::array<UInt4, TownGpuMaxFireVolumeSources> FrameFireVolumeGrids {};

bool PrepareFireVolumes(const TownCameraFrame &camera, unsigned sampleFactor, double seconds,
    std::span<const TownGpuFireVolumeSource> sources, std::span<const uint32_t> palette, std::span<const uint32_t> tone)
{
	ClearFireVolumeFrame();
	if (sources.empty()) return true;
	if (InjectFireVolume(TownGpuFireVolumeFault::Preflight)) return RefuseFireVolume("Injected fire volume preflight refusal");
	const auto finitePoint = [](TownCameraPoint p) { return Finite(p.x) && Finite(p.height) && Finite(p.z); };
	const auto dot = [](TownCameraPoint a, TownCameraPoint b) { return a.x*b.x + a.height*b.height + a.z*b.z; };
	if (sources.size() > TownGpuMaxFireVolumeSources || palette.size() != 256 || tone.size() != 256
	    || sampleFactor == 0 || sampleFactor > 4 || !std::isfinite(seconds) || seconds < 0 || seconds > TownFireVolumeMaxSeconds
	    || !camera.valid || camera.width <= 0 || camera.height <= 0
	    || uint64_t(camera.width) * sampleFactor != uint64_t(Width) || uint64_t(camera.height) * sampleFactor != uint64_t(Height)
	    || !finitePoint(camera.eye) || !finitePoint(camera.right) || !finitePoint(camera.up) || !finitePoint(camera.forward)
	    || !Finite(camera.heightScale) || camera.heightScale < 0.01F || camera.heightScale > 1000
	    || !Finite(camera.focalPixels) || camera.focalPixels <= 0 || camera.focalPixels > 1.0e6F
	    || !Finite(camera.centerX) || !Finite(camera.centerY) || std::abs(camera.centerX) > 1.0e6F || std::abs(camera.centerY) > 1.0e6F
	    || camera.perspective != FrameProjection.perspective || camera.nearClip != FrameProjection.nearClip || camera.farClip != FrameProjection.farClip
	    || std::abs(dot(camera.right,camera.right)-1) > 0.001F || std::abs(dot(camera.up,camera.up)-1) > 0.001F
	    || std::abs(dot(camera.forward,camera.forward)-1) > 0.001F || std::abs(dot(camera.right,camera.up)) > 0.001F
	    || std::abs(dot(camera.right,camera.forward)) > 0.001F || std::abs(dot(camera.up,camera.forward)) > 0.001F)
		return RefuseFireVolume("Invalid fire volume descriptors/camera/palette/sample factor");
	size_t planned = 0;
	size_t roiPixels = 0, adaptiveSources = 0;
	unsigned maximumStride = 1;
	std::array<size_t, TownGpuMaxFireVolumeSources> order {};
	std::array<float, TownGpuMaxFireVolumeSources> depths {};
	for (size_t i = 0; i < sources.size(); ++i) {
		const auto &s = sources[i];
		if (!IsValidTownFireVolumeConfig(s.config) || !Finite(s.position.x) || !Finite(s.position.height) || !Finite(s.position.z)
		    || !Finite(s.energy) || s.energy < 0 || s.energy > 16 || s.minX < 0 || s.minY < 0
		    || s.maxX < s.minX || s.maxY < s.minY || s.maxX >= Width || s.maxY >= Height
		    || s.samplingStride == 0 || s.samplingStride > 65535)
			return RefuseFireVolume("Invalid fire volume source/config/ROI");
		const uint64_t width = s.maxX - s.minX + 1, height = s.maxY - s.minY + 1;
		const uint64_t rays = ((width + s.samplingStride - 1) / s.samplingStride)
		    * ((height + s.samplingStride - 1) / s.samplingStride);
		if (rays > TownGpuMaxFireVolumeRays - planned) return RefuseFireVolume("Fire volume 65536-ray frame budget exceeded");
		planned += rays;
		roiPixels += width * height;
		adaptiveSources += s.samplingStride > 1;
		maximumStride = std::max(maximumStride, s.samplingStride);
		for (size_t j = 0; j < i; ++j)
			if (s.identity == sources[j].identity) return RefuseFireVolume("Duplicate fire volume source identity");
		// Preflight the ray ABI at all ROI extremes before the caller may omit
		// any original flame. Per-pixel shader guards remain transactional too.
		for (unsigned corner = 0; corner < 4; ++corner) {
			const float x = float((corner&1) ? s.maxX : s.minX), y = float((corner&2) ? s.maxY : s.minY);
			const auto ray = TownCameraScreenRay(camera, (x+0.5F)/sampleFactor, (y+0.5F)/sampleFactor);
			if (!ray.valid || std::abs(ray.origin.x-s.position.x) > 4096 || std::abs(ray.origin.height-s.position.height) > 4096
			    || std::abs(ray.origin.z-s.position.z) > 4096)
				return RefuseFireVolume("Fire volume ray origin outside core range");
			const float axial = ray.direction.x*camera.forward.x + ray.direction.height*camera.heightScale*camera.forward.height + ray.direction.z*camera.forward.z;
			if (!Finite(axial) || axial <= 0) return RefuseFireVolume("Fire volume ray has nonpositive axial direction");
		}
		const TownCameraPoint relative { s.position.x-camera.eye.x,
			(s.position.height+s.config.height*0.5F-camera.eye.height)*camera.heightScale, s.position.z-camera.eye.z };
		depths[i] = dot(relative,camera.forward); order[i] = i;
	}
	std::sort(order.begin(), order.begin()+sources.size(), [&](size_t a,size_t b) {
		return depths[a] != depths[b] ? depths[a] > depths[b] : a < b;
	});
	if (!CreateFireVolumeObjects(adaptiveSources != 0)) return false;
	if (!FrameActive || FrameFailed || !Device || !Context) return RefuseFireVolume("Fire volume recording changed during preflight");
	for (size_t index = 0; index < sources.size(); ++index) {
		const auto &s = sources[order[index]];
		const auto c = town_fire_volume_detail::MakeContext(s.config, seconds, s.identity, s.energy);
		FireVolumeConstants constants {};
		constants.eyeHeightScale = { camera.eye.x,camera.eye.height,camera.eye.z,camera.heightScale };
		constants.rightBasis = { camera.right.x,camera.right.height,camera.right.z,0 };
		constants.upBasis = { camera.up.x,camera.up.height,camera.up.z,0 };
		constants.forwardBasis = { camera.forward.x,camera.forward.height,camera.forward.z,0 };
		constants.cameraData = { camera.focalPixels,camera.centerX,camera.centerY,float(sampleFactor) };
		constants.clipTime = { camera.nearClip,camera.farClip,float(c.time),camera.perspective?1.0F:0.0F };
		constants.sourceEnergy = { s.position.x,s.position.height,s.position.z,s.energy };
		constants.config0 = { s.config.radius,s.config.height,s.config.smokeHeight,s.config.flameAbsorption };
		constants.config1 = { s.config.flameEmission,s.config.smokeAbsorption,s.config.smokeDensity,s.config.softIntersection };
		constants.context0 = { s.config.referenceEnergy,float(c.energyRatio),float(c.flameHeight),float(c.densityEnergy) };
		constants.context1 = { float(c.phase),float(c.offset),float(c.smokeSwayX),float(c.smokeSwayZ) };
		constants.roi = { uint32_t(s.minX),uint32_t(s.minY),uint32_t(s.maxX-s.minX+1),uint32_t(s.maxY-s.minY+1) };
		constants.control = { s.identity,uint32_t(Width),uint32_t(Height),
			(s.samplingStride > 1 ? AdaptiveFireVolumeSteps : TownGpuFireVolumeSteps)
			    | (s.samplingStride > 1 ? s.samplingStride << 8 : 0) };
		for (size_t i = 0; i < 64; ++i) {
			constants.palette[i] = { palette[i*4],palette[i*4+1],palette[i*4+2],palette[i*4+3] };
			constants.tone[i] = { tone[i*4],tone[i*4+1],tone[i*4+2],tone[i*4+3] };
		}
		D3D11_MAPPED_SUBRESOURCE mapped {};
		const HRESULT result = Context->Map(FireVolumeBuffers[index].get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(result)) return RefuseFireVolume("Map preflight fire volume constants", result);
		std::memcpy(mapped.pData, &constants, sizeof(constants)); Context->Unmap(FireVolumeBuffers[index].get(), 0);
		FrameFireVolumeRois[index] = constants.roi;
		FrameFireVolumeGrids[index] = { (constants.roi.z + s.samplingStride - 1) / s.samplingStride,
			(constants.roi.w + s.samplingStride - 1) / s.samplingStride, s.samplingStride, 0 };
	}
	FrameFireVolumeCount = sources.size();
	FireVolumeStats.sourceCount = sources.size(); FireVolumeStats.plannedRays = planned;
	FireVolumeStats.roiPixels = roiPixels; FireVolumeStats.adaptiveSources = adaptiveSources;
	FireVolumeStats.maximumSamplingStride = maximumStride;
	FireVolumeStats.ready = true;
	return true;
}

bool DispatchFireVolumes()
{
	if (FrameFireVolumeCount == 0) return true;
	struct Bindings {
		bool active = true;
		void Clear() {
			if (!active || !Context) return;
			active = false;
			ID3D11UnorderedAccessView *emptyUavs[3] {};
			ID3D11ShaderResourceView *emptyViews[12] {};
			ID3D11Buffer *emptyBuffer = nullptr;
			Context->OMSetRenderTargets(0, nullptr, nullptr);
			Context->PSSetShaderResources(0, 12, emptyViews);
			Context->VSSetShaderResources(10, 1, emptyViews);
			Context->CSSetUnorderedAccessViews(0, 3, emptyUavs, nullptr);
			Context->CSSetShaderResources(0, 4, emptyViews);
			Context->CSSetConstantBuffers(0, 1, &emptyBuffer);
			Context->CSSetShader(nullptr, nullptr, 0);
		}
		~Bindings() { Clear(); }
	} bindings;
	const auto start = Clock::now();
	if (!FireVolumeStats.ready || !FireVolumeShaders[0] || !FireVolumeColorView || !FireVolumeFailureView
	    || (FireVolumeStats.adaptiveSources != 0 && (!FireVolumeShaders[1] || !FireVolumeShaders[2]
	        || !FireVolumePrefixUav || !FireVolumePrefixSrv)))
		return Fail("Fire volume accepted without a complete compute pipeline");
	if (InjectFireVolume(TownGpuFireVolumeFault::BeforeDispatch)) {
		FireVolumeStats.failure = "Injected fire volume before-dispatch failure";
		return Fail(FireVolumeStats.failure);
	}
	// One thread owns one ARGB pixel; sequential source dispatches preserve the
	// host's far-to-near composition. D3D11 completes UAV access at each
	// Dispatch boundary (functional spec5.3.9), so overlapping ROIs cannot race
	// between these sequential dispatches. No DX12 barrier model is involved.
	// No UAV/RTV or UAV/SRV alias is bound.
	Context->OMSetRenderTargets(0, nullptr, nullptr);
	ID3D11ShaderResourceView *emptyPs[12] {};
	Context->PSSetShaderResources(0, 12, emptyPs);
	const UINT zero[4] {};
	Context->ClearUnorderedAccessViewUint(FireVolumeFailureView.get(), zero);
	ID3D11ShaderResourceView *views[] { FireVolumeViews[0].get(), FireVolumeViews[1].get(), FireVolumeViews[2].get() };
	Context->CSSetShaderResources(0, 3, views);
	if (FireVolumeTimingDisjoint && FireVolumeTimingBegin && FireVolumeTimingEnd) {
		Context->Begin(FireVolumeTimingDisjoint.get());
		Context->End(FireVolumeTimingBegin.get());
	}
	for (size_t i = 0; i < FrameFireVolumeCount; ++i) {
		ID3D11Buffer *buffer = FireVolumeBuffers[i].get();
		Context->CSSetConstantBuffers(0, 1, &buffer);
		ID3D11ShaderResourceView *emptyPrefix = nullptr;
		ID3D11UnorderedAccessView *emptyUavs[3] {};
		// The previous emitter's SRV is removed before this buffer becomes UAV.
		Context->CSSetShaderResources(3, 1, &emptyPrefix);
		Context->CSSetUnorderedAccessViews(0, 3, emptyUavs, nullptr);
		if (FrameFireVolumeGrids[i].z > 1) {
			ID3D11UnorderedAccessView *field[] { nullptr, FireVolumeFailureView.get(), FireVolumePrefixUav.get() };
			Context->CSSetUnorderedAccessViews(0, 3, field, nullptr);
			Context->CSSetShader(FireVolumeShaders[1].get(), nullptr, 0);
			Context->Dispatch((FrameFireVolumeGrids[i].x + 7) / 8, (FrameFireVolumeGrids[i].y + 7) / 8, 1);
			// Dispatch boundary orders field writes; never bind the scratch as SRV
			// while any UAV view of it is present. No CPU wait/readback is added.
			Context->CSSetUnorderedAccessViews(0, 3, emptyUavs, nullptr);
			ID3D11ShaderResourceView *prefix = FireVolumePrefixSrv.get();
			Context->CSSetShaderResources(3, 1, &prefix);
			Context->CSSetShader(FireVolumeShaders[2].get(), nullptr, 0);
		} else {
			Context->CSSetShader(FireVolumeShaders[0].get(), nullptr, 0);
		}
		ID3D11UnorderedAccessView *composite[] { FireVolumeColorView.get(), FireVolumeFailureView.get(), nullptr };
		Context->CSSetUnorderedAccessViews(0, 3, composite, nullptr);
		Context->Dispatch((FrameFireVolumeRois[i].z + 7) / 8, (FrameFireVolumeRois[i].w + 7) / 8, 1);
		FireVolumeStats.compositePixels += size_t(FrameFireVolumeRois[i].z) * FrameFireVolumeRois[i].w;
	}
	if (FireVolumeTimingDisjoint && FireVolumeTimingBegin && FireVolumeTimingEnd) {
		Context->End(FireVolumeTimingEnd.get());
		Context->End(FireVolumeTimingDisjoint.get());
	}
	bindings.Clear();
	FireVolumeStats.dispatched = true;
	FireVolumeStats.commandMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	if (InjectFireVolume(TownGpuFireVolumeFault::AfterDispatch)) {
		FireVolumeStats.failure = "Injected fire volume after-dispatch failure";
		return Fail(FireVolumeStats.failure);
	}
	Context->CopyResource(FireVolumeFailureStaging.get(), FireVolumeFailureBuffer.get());
	return true;
}

bool ReadFireVolumeStatus()
{
	if (FrameFireVolumeCount == 0) return true;
	if (InjectFireVolume(TownGpuFireVolumeFault::BeforeStatusReadback)) {
		FireVolumeStats.failure = "Injected fire volume status-readback failure";
		return Fail(FireVolumeStats.failure);
	}
	D3D11_MAPPED_SUBRESOURCE mapped {};
	const HRESULT result = Context->Map(FireVolumeFailureStaging.get(), 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(result)) return Fail("Read fire volume failure status", result);
	uint32_t failure = 0; std::memcpy(&failure, mapped.pData, sizeof(failure));
	Context->Unmap(FireVolumeFailureStaging.get(), 0);
	if (failure != 0) {
		// Stage bits come from failRay: 1=direct CS, 2=adaptive CSField,
		// 4=adaptive CSComposite. The nonzero failure remains fatal either way.
		FireVolumeStats.failure = "Fire volume shader reported an invalid ray or nonfinite result (failure mask " + std::to_string(failure) + ")";
		return Fail(FireVolumeStats.failure);
	}
	return true;
}

void UpdateFireVolumeTiming()
{
	// The existing Map/readback already waited for GPU work. Query once with
	// DONOTFLUSH: no polling, spinning or extra GPU/CPU synchronization.
	if (!FireVolumeStats.dispatched || !FireVolumeTimingDisjoint || !FireVolumeTimingBegin || !FireVolumeTimingEnd) return;
	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint {};
	UINT64 begin = 0, end = 0;
	if (Context->GetData(FireVolumeTimingDisjoint.get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
	    || Context->GetData(FireVolumeTimingBegin.get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
	    || Context->GetData(FireVolumeTimingEnd.get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
	    || disjoint.Disjoint || disjoint.Frequency == 0 || end < begin) return;
	FireVolumeStats.gpuMilliseconds = static_cast<double>(end-begin) * 1000.0 / static_cast<double>(disjoint.Frequency);
	FireVolumeStats.gpuTimingKnown = std::isfinite(FireVolumeStats.gpuMilliseconds);
	if (!FireVolumeStats.gpuTimingKnown) FireVolumeStats.gpuMilliseconds = -1;
}
