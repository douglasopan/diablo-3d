// Included in town_view.cpp: a bounded, frame-local RGB presentation pilot.
// Native simulation and the25B light/shadow field remain separate owners.

bool FireVolumeScreenBounds(const Surface &out, FrameFireVolume &source)
{
	if (!IsValidTownFireVolumeConfig(source.config) || out.w() <= 0 || out.h() <= 0) return false;
	const auto bounds = GetTownFireVolumeBounds(source.config);
	const auto &camera = ViewCamera.projection;
	const float near = camera.perspective ? std::max(camera.nearClip, 0.0001F) : camera.nearClip;
	if (!std::isfinite(near) || !std::isfinite(camera.farClip) || near >= camera.farClip) return false;
	float lowX = float(out.w()), lowY = float(out.h()), highX = -1, highY = -1;
	std::array<TownCameraPoint, 8> corners;
	float minDepth = std::numeric_limits<float>::infinity(), maxDepth = -minDepth;
	for (unsigned corner = 0; corner < 8; ++corner) {
		const TownCameraPoint point {
			source.position.x + ((corner & 1) ? bounds.maximum.x : bounds.minimum.x),
			source.position.height + ((corner & 2) ? bounds.maximum.height : bounds.minimum.height),
			source.position.z + ((corner & 4) ? bounds.maximum.z : bounds.minimum.z)
		};
		const auto view = TownCameraToView(camera, point);
		if (!std::isfinite(view.x) || !std::isfinite(view.height) || !std::isfinite(view.z)) return false;
		corners[corner] = view;
		minDepth = std::min(minDepth, view.z); maxDepth = std::max(maxDepth, view.z);
	}
	if (maxDepth < near || minDepth > camera.farClip) return false;
	const bool cameraInside = camera.eye.x >= source.position.x + bounds.minimum.x
	    && camera.eye.x <= source.position.x + bounds.maximum.x
	    && camera.eye.height >= source.position.height + bounds.minimum.height
	    && camera.eye.height <= source.position.height + bounds.maximum.height
	    && camera.eye.z >= source.position.z + bounds.minimum.z
	    && camera.eye.z <= source.position.z + bounds.maximum.z;
	if (camera.perspective && cameraInside) {
		source.minX = source.minY = 0; source.maxX = out.w() - 1; source.maxY = out.h() - 1;
		return true; // Conservative viewport; adaptive sampling retains the ray cap.
	}
	bool projected = false;
	const auto project = [&](TownCameraPoint view) {
		if (view.z < near || view.z > camera.farClip) return true;
		const float scale = camera.focalPixels * (camera.perspective ? 1 / view.z : 1);
		const float px = (camera.centerX + view.x * scale) * RasterSampleFactor;
		const float py = (camera.centerY - view.height * scale) * RasterSampleFactor;
		if (!std::isfinite(px) || !std::isfinite(py)) return false;
		lowX = std::min(lowX, px); lowY = std::min(lowY, py);
		highX = std::max(highX, px); highY = std::max(highY, py);
		projected = true; return true;
	};
	for (const auto &corner : corners) if (!project(corner)) return false;
	// A clipped convex box projects to extrema at its surviving corners and
	// edge/plane intersections. All twelve edges are visited exactly once.
	for (unsigned a = 0; a < corners.size(); ++a) for (unsigned bit : { 1U, 2U, 4U }) {
		if ((a & bit) != 0) continue;
		const auto p = corners[a], q = corners[a | bit];
		for (float plane : { near, camera.farClip }) {
			if ((p.z < plane) == (q.z < plane) || p.z == q.z) continue;
			const float t = (plane - p.z) / (q.z - p.z);
			if (!std::isfinite(t) || t < 0 || t > 1
			    || !project({ p.x + t * (q.x - p.x), p.height + t * (q.height - p.height), plane })) return false;
		}
	}
	if (!projected || highX < 0 || highY < 0 || lowX >= out.w() || lowY >= out.h()) return false;
	source.minX = int(std::floor(std::clamp(lowX, 0.0F, float(out.w() - 1))));
	source.minY = int(std::floor(std::clamp(lowY, 0.0F, float(out.h() - 1))));
	source.maxX = int(std::ceil(std::clamp(highX, 0.0F, float(out.w() - 1))));
	source.maxY = int(std::ceil(std::clamp(highY, 0.0F, float(out.h() - 1))));
	return source.minX <= source.maxX && source.minY <= source.maxY;
}

void PrepareFrameFireVolumes(const Surface &out, bool allowGpu)
{
	const auto &camera = ViewCamera.projection;
	FireVolumeState.enabled = FireVolumesEnabledForDiagnostics && Brazier.loaded
	    && !SmoothWorldPixels.empty() && SmoothWorldPixels.size() == DepthBuffer.size()
	    && DepthBuffer.size() == size_t(out.w()) * out.h()
	    && camera.valid && std::isfinite(camera.heightScale) && camera.heightScale > 0
	    && std::isfinite(camera.focalPixels) && camera.focalPixels > 0
	    && std::isfinite(camera.nearClip) && camera.nearClip >= 0
	    && std::isfinite(camera.farClip) && camera.farClip > camera.nearClip
	    && RasterSampleFactor > 0 && std::isfinite(FrameFireTime)
	    && FrameFireTime >= 0 && FrameFireTime <= TownFireVolumeMaxSeconds;
	const auto *snapshot = cathedral::LiveSnapshot();
	if (!FireVolumeState.enabled || MyPlayer == nullptr || snapshot == nullptr
	    || snapshot->gameRevision != cathedral::LiveEpoch()) return;
	FireVolumeState.epoch = snapshot->gameRevision;
	FireVolumeState.scratchBytes = sizeof(FireVolumeRgbScratch);
	const bool requestGpu = CaptureGpu && !CaptureGpuFailed && allowGpu && !ForceCpuFireVolumesForDiagnostics && !FailFireVolumeIntegrationForDiagnostics;
	FireVolumeState.gpuRequested = requestGpu;
	const size_t pixelBudget = requestGpu || ForceCpuFireVolumesForDiagnostics || FailFireVolumeIntegrationForDiagnostics
	    ? MaxFrameFireVolumePixels : MaxCpuFireVolumePixels;
	const auto earlier = [](const FrameFireVolume &a, const FrameFireVolume &b) {
		return std::tie(a.distance, a.slot) < std::tie(b.distance, b.slot);
	};
	// Keep the nearest four in fixed storage, then admit entire rectangles.
	// The selection is independent of availability of25B's shadow maps.
	for (const auto &metadata : CaptureCathedralNativeLightObjects(*snapshot)) {
		if (metadata.slot < 0 || metadata.slot >= MAXOBJECTS) continue;
		const Object &object = Objects[metadata.slot];
		const Point tile { metadata.originX, metadata.originZ };
		if (!CathedralNativeLightBodyAdmitted(metadata.slot) || !CathedralTileInFrame(tile)
		    || !IsTileVisible(tile) || object._oDelFlag || object.canInteractWith()
		    || dLight[tile.x][tile.y] >= LightsMax || !object._oAnimData
		    || object._oAnimFrame == 0 || object._oAnimFrame > object._oAnimData->numSprites()) continue;
		++FireVolumeState.eligible;
		FrameFireVolume source;
		source.config = MakeCathedralFireVolumeConfig();
		source.position = { float(tile.x), BrazierFlameContact, float(tile.y) };
		source.identity = 0xB2A10000U ^ (uint32_t(tile.x) << 8) ^ uint32_t(tile.y);
		source.slot = metadata.slot;
		const float dx = float(tile.x - MyPlayer->position.tile.x), dz = float(tile.y - MyPlayer->position.tile.y);
		source.distance = dx * dx + dz * dz;
		if (source.distance > 64 || !IsValidTownFireVolumeConfig(source.config)) continue;
		TownPointLight light;
		light.position = source.position; light.intensity = 3;
		source.energy = TownFireLightAtTime(light, source.identity, FrameFireTime, FireIntensityVariation).intensity;
		if (!std::isfinite(source.energy) || source.energy < 0 || source.energy > 16
		    || !FireVolumeScreenBounds(out, source)) continue;
		if (FrameFireVolumeCount < FrameFireVolumes.size()) {
			FrameFireVolumes[FrameFireVolumeCount++] = source;
		} else if (earlier(source, FrameFireVolumes[FrameFireVolumeCount - 1])) {
			FrameFireVolumes[FrameFireVolumeCount - 1] = source;
		} else {
			FireVolumeState.budgetLimited = true;
			continue;
		}
		std::sort(FrameFireVolumes.begin(), FrameFireVolumes.begin() + FrameFireVolumeCount, earlier);
	}
	size_t admitted = 0;
	for (size_t i = 0; i < FrameFireVolumeCount; ++i) {
		auto source = FrameFireVolumes[i];
		const size_t pixels = size_t(source.maxX - source.minX + 1) * (source.maxY - source.minY + 1);
		// The ray cap alone does not bound the adaptive composite pass: a camera
		// inside a fire AABB conservatively projects to the whole viewport, and
		// CSComposite still visits every ROI pixel to clip/interpolate transport.
		// Bound that second pass by the same frame budget. A rejected source keeps
		// its native animated flame; its independent point light/shadow is intact.
		if (pixels > pixelBudget - FireVolumeState.plannedPixels) {
			FireVolumeState.budgetLimited = true;
			continue;
		}
		if (requestGpu) {
			// Equal remaining shares prevent one close ROI from starving the
			// remaining selected sources; unused shares flow to the next source.
			const size_t remaining = pixelBudget - FireVolumeState.plannedSampleRays;
			const size_t share = remaining / (FrameFireVolumeCount - i);
			// The GPU still reconstructs the complete ROI at each pixel's receiver
			// depth; bound integration work near one ninth of the same ROI. The
			// existing frame/ray budgets remain the hard ceilings for every source.
			const size_t ninthResolutionBudget = (pixels + 8) / 9;
			const auto grid = ChooseTownFireVolumeSamplingGrid(source.maxX - source.minX + 1,
			    source.maxY - source.minY + 1, std::min(share, ninthResolutionBudget));
			if (!grid) { FireVolumeState.budgetLimited = true; continue; }
			source.samplingStride = grid.stride;
			FireVolumeState.plannedSampleRays += grid.rays;
		} else FireVolumeState.plannedSampleRays += pixels;
		FireVolumeState.plannedPixels += pixels;
		FireVolumeState.roiPixels += pixels;
		FireVolumeState.adaptiveSources += source.samplingStride > 1;
		FireVolumeState.maximumSamplingStride = std::max(FireVolumeState.maximumSamplingStride, source.samplingStride);
		FrameFireVolumes[admitted++] = source;
	}
	FrameFireVolumeCount = admitted;
	FireVolumeState.admitted = admitted;
	if (requestGpu && admitted != 0) {
		// Begin resets backend-only faults. Arm here so a fixture can exercise
		// the real host path rather than only an isolated compute call.
		switch (FireVolumeGpuFailureForDiagnostics) {
		case TownViewFireVolumeGpuFailurePoint::Preflight: SetTownGpuFireVolumeFaultForDiagnostics(TownGpuFireVolumeFault::Preflight); break;
		case TownViewFireVolumeGpuFailurePoint::BeforeDispatch: SetTownGpuFireVolumeFaultForDiagnostics(TownGpuFireVolumeFault::BeforeDispatch); break;
		case TownViewFireVolumeGpuFailurePoint::AfterDispatch: SetTownGpuFireVolumeFaultForDiagnostics(TownGpuFireVolumeFault::AfterDispatch); break;
		case TownViewFireVolumeGpuFailurePoint::BeforeStatusReadback: SetTownGpuFireVolumeFaultForDiagnostics(TownGpuFireVolumeFault::BeforeStatusReadback); break;
		case TownViewFireVolumeGpuFailurePoint::None: break;
		}
		std::array<TownGpuFireVolumeSource, MaxFrameFireVolumes> sources;
		std::array<size_t, MaxFrameFireVolumes> order {};
		for (size_t i = 0; i < admitted; ++i) order[i] = i;
		std::sort(order.begin(), order.begin() + admitted,
		    [](size_t a, size_t b) { return FrameFireVolumes[a].slot < FrameFireVolumes[b].slot; });
		for (size_t i = 0; i < admitted; ++i) {
			const auto &source = FrameFireVolumes[order[i]];
			sources[i] = { { source.position.x, source.position.height, source.position.z },
				source.identity, source.energy, source.config, source.minX, source.minY,
				source.maxX, source.maxY, source.samplingStride };
		}
		std::array<uint32_t, 256> tone;
		for (size_t i = 0; i < tone.size(); ++i) {
			const auto color = SmoothToneRamp[i];
			tone[i] = 0xFF000000U | (uint32_t(color.r) << 16) | (uint32_t(color.g) << 8) | color.b;
		}
		FrameFireVolumesGpuSubmitted = TownGpuSetFireVolumes(camera, RasterSampleFactor, FrameFireTime,
		    { sources.data(), admitted }, SmoothWorldPalette, tone);
		if (!FrameFireVolumesGpuSubmitted) {
			// Preflight declined before omission. Recompute complete sources at
			// the conservative CPU budget; all others retain their old flame.
			ClearFrameFireVolumes();
			PrepareFrameFireVolumes(out, false);
			FireVolumeState.gpuRequested = FireVolumeState.gpuDeclined = true;
		}
	}
}

bool DeferBrazierFlame(int slot, const Object &object)
{
	for (size_t i = 0; i < FrameFireVolumeCount; ++i) {
		auto &source = FrameFireVolumes[i];
		if (source.slot != slot) continue;
		source.fallback = object.currentSprite();
		source.fallbackLighting = object.applyLighting ? dLight[object.position.x][object.position.y] : 0;
		source.drawn = true;
		return true;
	}
	return false;
}

void ComposeFrameFireVolumes(const Surface &out)
{
	if (!FireVolumeState.enabled || FrameFireVolumeCount == 0) return;
	const auto start = std::chrono::steady_clock::now();
	// Native fallback remains complete even if the final RGB surface/renderer
	// fails. These are indexed compatibility quads, not CPU world geometry.
	struct RestoreDrawFlags {
		bool capture = CaptureGpu, cathedral = DrawingCathedral;
		bool paletteBlend = CathedralPaletteBlend;
		const uint8_t *translation = CathedralTranslation;
		~RestoreDrawFlags() {
			CaptureGpu = capture; DrawingCathedral = cathedral; CathedralPaletteBlend = paletteBlend;
			CathedralTranslation = translation; DrawingIndexedFireFallback = false; DrawingFireRgbFallback = false;
		}
	} restore;
	CaptureGpu = false;
	DrawingCathedral = true;
	CathedralTranslation = nullptr;
	CathedralPaletteBlend = false;
	DrawingIndexedFireFallback = true;
	for (size_t i = 0; i < FrameFireVolumeCount; ++i) {
		const auto &source = FrameFireVolumes[i];
		if (!source.drawn || !source.fallback) continue;
		++FireVolumeState.drawn;
		DrawBillboard(out, { source.position.x, 0, source.position.z },
		    { int(source.position.x), int(source.position.z) }, *source.fallback,
		    PickKind::Ground, -1, source.fallbackLighting, false, 0.45F * CathedralFireVisualScale, source.position.height);
	}
	DrawingIndexedFireFallback = false;
	if (FrameFireVolumesGpuSubmitted && RendererState.usedGpu && GpuFrame.fireVolumesApplied
	    && GpuFrame.fireVolumeSources == FrameFireVolumeCount) {
		FireVolumeState.gpuApplied = true;
		FireVolumeState.compositionMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return; // The GPU already composed the contribution; keep it exactly once.
	}
	const auto &camera = ViewCamera.projection;
	// Camera depth, rather than the player's position, determines alpha order.
	std::array<size_t, MaxFrameFireVolumes> order {};
	for (size_t i = 0; i < FrameFireVolumeCount; ++i) order[i] = i;
	const auto sourceDepth = [&](size_t index) {
		const auto &source = FrameFireVolumes[index];
		return TownCameraToView(camera, { source.position.x, source.position.height + source.config.height * 0.5F, source.position.z }).z;
	};
	std::sort(order.begin(), order.begin() + FrameFireVolumeCount, [&](size_t a, size_t b) {
		const float depthA = sourceDepth(a), depthB = sourceDepth(b);
		return depthA != depthB ? depthA > depthB : FrameFireVolumes[a].slot < FrameFireVolumes[b].slot;
	});
	for (size_t i = 0; i < FrameFireVolumeCount; ++i) {
		const auto &source = FrameFireVolumes[order[i]];
		if (!source.drawn) continue;
		const size_t pixels = size_t(source.maxX - source.minX + 1) * (source.maxY - source.minY + 1);
		bool recover = source.samplingStride != 1 || pixels > FireVolumeRgbScratch.size();
		size_t saved = 0;
		for (int y = source.minY; y <= source.maxY && !recover; ++y) for (int x = source.minX; x <= source.maxX; ++x)
			FireVolumeRgbScratch[saved++] = SmoothWorldPixels[size_t(y) * out.w() + x];
		const size_t previousBlended = FireVolumeState.blendedPixels;
		for (int y = source.minY; y <= source.maxY && !recover; ++y) for (int x = source.minX; x <= source.maxX; ++x) {
			const size_t pixel = size_t(y) * out.w() + x;
			if (WarriorViewModelCoverage.size() == DepthBuffer.size() && WarriorViewModelCoverage[pixel]) {
				++FireVolumeState.occludedPixels; continue;
			}
			const auto ray = TownCameraScreenRay(camera, (x + 0.5F) / RasterSampleFactor, (y + 0.5F) / RasterSampleFactor);
			if (!ray.valid) { recover = true; ++FireVolumeState.failedRays; break; }
			const float originDepth = TownCameraToView(camera, ray.origin).z;
			const float depthPerTile = ray.direction.x * camera.forward.x
			    + ray.direction.height * camera.heightScale * camera.forward.height + ray.direction.z * camera.forward.z;
			if (!std::isfinite(depthPerTile) || depthPerTile <= 0) { recover = true; ++FireVolumeState.failedRays; break; }
			const float minT = std::max(0.0F, (camera.nearClip - originDepth) / depthPerTile);
			const float maxT = (std::min(DepthBuffer[pixel], camera.farClip) - originDepth) / depthPerTile;
			if (!std::isfinite(maxT)) { recover = true; ++FireVolumeState.failedRays; break; }
			if (maxT <= minT) { ++FireVolumeState.occludedPixels; continue; }
			const auto sample = IntegrateTownFireVolumeRay(source.config,
			    { ray.origin.x - source.position.x, ray.origin.height - source.position.height, ray.origin.z - source.position.z },
			    { ray.direction.x, ray.direction.height, ray.direction.z }, minT, maxT,
			    FrameFireTime, source.identity, source.energy, ForceCpuFireVolumesForDiagnostics ? 24 : 12);
			++FireVolumeState.rays; FireVolumeState.steps += sample.steps;
			if (!sample.valid || FailFireVolumeIntegrationForDiagnostics) { recover = true; ++FireVolumeState.failedRays; break; }
			if (sample.alpha == 0 && sample.premultiplied.red == 0
			    && sample.premultiplied.green == 0 && sample.premultiplied.blue == 0) continue;
			const uint32_t base = SmoothWorldPixels[pixel] ? SmoothWorldPixels[pixel] : SmoothWorldPalette[*out.at(x, y)];
			const auto background = TownSrgbToLinear({ float((base >> 16) & 255) / 255, float((base >> 8) & 255) / 255, float(base & 255) / 255 });
			// Apply the existing brightness ramp to the new contribution once.
			// This is local LDR presentation, not a new global HDR/tone pipeline.
			const auto encodedEmission = TownLinearToSrgb({ sample.premultiplied.red, sample.premultiplied.green, sample.premultiplied.blue });
			const uint32_t toned = ToneWorldRgb(encodedEmission);
			const auto emission = TownSrgbToLinear({ float((toned >> 16) & 255) / 255, float((toned >> 8) & 255) / 255, float(toned & 255) / 255 });
			const float transmittance = 1 - sample.alpha;
			const auto output = TownLinearToSrgb({ emission.red + background.red * transmittance,
			    emission.green + background.green * transmittance, emission.blue + background.blue * transmittance });
			const auto byte = [](float value) { return uint32_t(std::clamp(value * 255 + 0.5F, 0.0F, 255.0F)); };
			SmoothWorldPixels[pixel] = (base & 0xFF000000U) | (byte(output.red) << 16) | (byte(output.green) << 8) | byte(output.blue);
			++FireVolumeState.blendedPixels;
		}
		if (recover) {
			// Never leave a partly integrated or missing RGB flame. Restore the
			// complete rectangle and present its existing native flame instead.
			size_t restored = 0;
			for (int y = source.minY; y <= source.maxY && restored < saved; ++y) for (int x = source.minX; x <= source.maxX && restored < saved; ++x)
				SmoothWorldPixels[size_t(y) * out.w() + x] = FireVolumeRgbScratch[restored++];
			FireVolumeState.blendedPixels = previousBlended;
			DrawingIndexedFireFallback = DrawingFireRgbFallback = true;
			DrawBillboard(out, { source.position.x, 0, source.position.z },
			    { int(source.position.x), int(source.position.z) }, *source.fallback,
			    PickKind::Ground, -1, source.fallbackLighting, false, 0.45F * CathedralFireVisualScale, source.position.height);
			DrawingIndexedFireFallback = DrawingFireRgbFallback = false;
			++FireVolumeState.recoveredEmitters;
		}
	}
	FireVolumeState.compositionMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
