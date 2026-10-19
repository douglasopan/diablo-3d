// Inside the Windows backend namespace. Own PS:b4/t12; no world/mesh/CS ABI changes.
void BindTownPointGeometryResources()
{
	ID3D11Buffer *buffer = FrameTownPointGeometryLighting ? TownPointGeometryConstantBuffer.get() : nullptr;
	ID3D11ShaderResourceView *resource = FrameTownPointGeometryLighting ? TownPointGeometryShadowResource.get() : nullptr;
	Context->PSSetConstantBuffers(4, 1, &buffer);
	Context->PSSetShaderResources(12, 1, &resource);
}

bool SetTownPointGeometryLighting(const TownGeometricPointShadowView &maps,
    std::span<const TownPointLight> lights, bool shadowsEnabled)
{
	if (!FrameActive || FrameFailed || !GeometryCommands.empty())
		return Fail("Town point geometry field must precede geometry in an active frame");
	ClearTownPointGeometryFrameBinding();
	if (maps.sources.empty() && maps.depth.empty() && lights.empty()) return true;
	const size_t count = maps.sources.size();
	if (count == 0 || count > TownGeometricPointMaximumSources || lights.size() != count
	    || maps.resolution != TownFireShadowResolution || maps.epoch == 0 || maps.identity == 0 || maps.revision == 0
	    || maps.depth.size() != count * TownGeometricPointCubeTexels)
		return Fail("Invalid Town point geometry field identity/count/dimensions");
	TownPointGeometryConstants constants {};
	constants.counts = { static_cast<uint32_t>(count), shadowsEnabled ? 1U : 0U,
		static_cast<uint32_t>(maps.resolution), 0 };
	for (size_t i = 0; i < count; ++i) {
		const auto &source = maps.sources[i];
		const auto &light = lights[i];
		if (!town_geometric_point_detail::ValidSource(source)
		    || !Finite(light.position) || !Finite(light.radius)
		    || !Finite(light.color.red) || !Finite(light.color.green) || !Finite(light.color.blue)
		    || light.color.red < 0 || light.color.red > 16 || light.color.green < 0 || light.color.green > 16
		    || light.color.blue < 0 || light.color.blue > 16
		    || !Finite(light.intensity) || light.intensity < 0 || light.intensity > TownMaxPointLightIntensity
		    || light.position.x != source.position.x || light.position.height != source.position.height
		    || light.position.z != source.position.z || light.radius != source.radius)
			return Fail("Town point energy descriptor differs from its admitted geometry source");
		constants.positionRadius[i] = { light.position.x, light.position.height, light.position.z, light.radius };
		constants.colorIntensity[i] = { light.color.red, light.color.green, light.color.blue, light.intensity };
	}
	if (!std::all_of(maps.depth.begin(), maps.depth.end(), [](float depth) {
		return depth > 0 && (std::isfinite(depth) || depth == std::numeric_limits<float>::infinity());
	})) return Fail("Town point geometry depth must be positive or positive infinity");
	const uint64_t fingerprint = PayloadFingerprint(maps.depth);
	const bool hit = TownPointGeometryShadowResource && TownPointGeometryKey == maps.identity
	    && TownPointGeometryRevision == maps.revision && TownPointGeometryEpoch == maps.epoch
	    && TownPointGeometrySourceCount == count;
	if (hit) {
		if (TownPointGeometryFingerprint != fingerprint
		    || std::memcmp(&TownPointGeometrySourcePositions, &constants.positionRadius, sizeof(constants.positionRadius)) != 0)
			return Fail("Immutable Town point geometry field changed without a new publication");
	} else {
		const size_t bytes = maps.depth.size_bytes();
		if (Status.townPointShadowUploadedBytes > std::numeric_limits<size_t>::max() - bytes)
			return Fail("Town point geometry upload counter exceeded", S_OK, TownGpuFailureKind::Capacity);
		ComOwner<ID3D11ShaderResourceView> resource;
		if (!UploadTexture(maps.resolution, static_cast<int>(count * 6 * maps.resolution), DXGI_FORMAT_R32_FLOAT,
		        maps.depth.data(), maps.resolution * sizeof(float), resource)) return false;
		TownPointGeometryShadowResource = std::move(resource);
		TownPointGeometryKey = maps.identity;
		TownPointGeometryRevision = maps.revision;
		TownPointGeometryEpoch = maps.epoch;
		TownPointGeometryFingerprint = fingerprint;
		TownPointGeometrySourceCount = count;
		TownPointGeometrySourcePositions = constants.positionRadius;
		Status.townPointShadowUploadedBytes += bytes;
	}
	if (!TownPointGeometryConstantBuffer) {
		D3D11_BUFFER_DESC description {};
		description.ByteWidth = sizeof(TownPointGeometryConstants);
		description.Usage = D3D11_USAGE_DYNAMIC;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		const HRESULT result = Device->CreateBuffer(&description, nullptr, TownPointGeometryConstantBuffer.put());
		if (FAILED(result)) return Fail("Create Town point geometry constants", result);
	}
	D3D11_MAPPED_SUBRESOURCE mapped {};
	const HRESULT result = Context->Map(TownPointGeometryConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(result)) return Fail("Map Town point geometry constants", result);
	std::memcpy(mapped.pData, &constants, sizeof(constants));
	Context->Unmap(TownPointGeometryConstantBuffer.get(), 0);
	FrameTownPointGeometryLighting = true;
	Status.townPointLightCount = count;
	Status.townPointShadowBytes = maps.depth.size_bytes();
	return true;
}
