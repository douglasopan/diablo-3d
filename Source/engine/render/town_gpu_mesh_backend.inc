// Included once by town_gpu.cpp after its common material/resource helpers.
// Keeps the new indexed path on the same device, command order and readback.
namespace {
TownGpuMeshStats MeshStats;

#ifdef _WIN32
constexpr size_t MaxMeshBytes = 256 * 1024 * 1024;

struct MeshConstants {
	std::array<Float4, 3> worldToView;
	std::array<Float4, 3> localToWorld;
	std::array<Float4, 3> normalToWorld;
	Float4 camera;
	Float4 eye;
	Float4 towardViewer;
	Float4 lightAndBias;
	Float4 normalDiffuse;
	Float4 receiver;
	Float4 fallback;
	Float4 volumeParameters;
	UInt4 control;
};
static_assert(sizeof(MeshConstants) == 18 * 16);
static_assert(sizeof(TownGpuMeshVertex) == 36);

struct CachedMesh {
	ComOwner<ID3D11Buffer> vertices;
	ComOwner<ID3D11Buffer> indices;
	size_t vertexCount;
	size_t indexCount;
	size_t bytes;
	uint64_t lastSeenFrame;
	uint64_t uploadFrame;
	bool flatNormals;
	bool flatVolumeFaces;
};

struct MeshBatch {
	CachedMesh *mesh;
	CachedTexture *texture;
	Constants constants;
	MeshConstants meshConstants;
	UINT first;
	UINT count;
	bool preservePicking;
};

struct GeometryCommand {
	bool mesh;
	size_t batch;
};

std::unordered_map<TextureKey, CachedMesh, TextureKeyHash> MeshCache;
std::vector<MeshBatch> MeshBatches;
std::vector<GeometryCommand> GeometryCommands;
ComOwner<ID3D11VertexShader> MeshVertexShader;
ComOwner<ID3D11InputLayout> MeshInputLayout;
ComOwner<ID3D11Buffer> MeshConstantBuffer;
TownGpuMeshCamera MeshCamera;
bool MeshCameraReady = false;
size_t MeshBytes = 0;

constexpr char MeshShaderSource[] = R"hlsl(
cbuffer Frame : register(b0) {
 float4 target;
 float4 projection;
 uint4 flags;
 float4 shading;
 float4 normalAndRed;
 float4 roomMinimum;
 float4 roomMaximum;
 float4 parameters;
 float4 shadowRight;
 float4 shadowUp;
 float4 shadowLight;
 float4 shadowScale;
};
cbuffer Mesh : register(b1) {
 float4 worldToView[3];
 float4 localToWorld[3];
 float4 normalToWorld[3];
 float4 camera;
 float4 eye;
 float4 towardViewer;
 float4 lightAndBias;
 float4 materialNormalDiffuse;
 float4 materialReceiver;
 float4 fallback;
 float4 volumeParameters;
 uint4 control;
};
struct Input {
 float3 position : POSITION;
 float2 uv : TEXCOORD1;
 float3 normal : NORMAL;
 uint volumeFace : COLOR1;
};
struct Interpolated {
 float4 position : SV_Position;
 float depth : TEXCOORD0;
 float2 uv : TEXCOORD1;
 float3 world : TEXCOORD2;
 nointerpolation uint pick : COLOR0;
 nointerpolation float4 normalDiffuse : TEXCOORD3;
 nointerpolation float4 shadowParameters : TEXCOORD4;
 nointerpolation float2 fallback : TEXCOORD5;
 nointerpolation float2 volume : TEXCOORD7;
 float3 clipDistance : SV_ClipDistance0;
};
Interpolated VS(Input input) {
 Interpolated output;
 float4 local = float4(input.position, 1);
 float3 world = float3(dot(localToWorld[0], local), dot(localToWorld[1], local), dot(localToWorld[2], local));
 float4 world4 = float4(world, 1);
 float3 view = float3(dot(worldToView[0], world4), dot(worldToView[1], world4), dot(worldToView[2], world4));
 if (volumeParameters.y != 0) {
  // Keep the native camera's subtraction/height scaling/dot ordering.
  // precise forbids contractions and reassociation through these outputs.
  precise float3 relative = world - eye.xyz;
  precise float3 nativeView;
  relative.y = relative.y * camera.w;
  nativeView.x = (relative.x * worldToView[0].x + relative.y * worldToView[0].y) + relative.z * worldToView[0].z;
  nativeView.y = (relative.x * worldToView[1].x + relative.y * worldToView[1].y) + relative.z * worldToView[1].z;
  nativeView.z = (relative.x * worldToView[2].x + relative.y * worldToView[2].y) + relative.z * worldToView[2].z;
  view = nativeView;
 }
 float clipW = projection.z != 0 ? view.z : 1;
 // Do not divide or saturate: hardware clips vertices behind/through the eye.
 float clipZ = projection.z != 0 ? projection.y / (projection.y - projection.x) * (view.z - projection.x) : view.z / 4096;
 output.position = float4(view.x * (2 * camera.x / target.x) + (2 * camera.y / target.x - 1) * clipW,
  view.y * (2 * camera.x / target.y) + (1 - 2 * camera.z / target.y) * clipW, clipZ, clipW);
 if (volumeParameters.y != 0 && (projection.z == 0 || view.z != 0)) {
  // Match the existing CPU Project and projected VS operation order. The
  // algebraically collapsed form above can reverse almost coplanar winners.
  // Keep depth unclamped outside near/far so hardware clipping stays linear.
  precise float reciprocalW = projection.z != 0 ? 1 / view.z : 1;
  precise float screenX = camera.y + camera.x * view.x * reciprocalW;
  precise float screenY = camera.z - camera.x * view.y * reciprocalW;
  precise float normalizedDepth = projection.z != 0
   ? projection.y * (view.z - projection.x) / ((projection.y - projection.x) * view.z)
   : view.z / 4096.0;
  output.position = float4((screenX * 2 / target.x - 1) * clipW,
   (1 - screenY * 2 / target.y) * clipW, normalizedDepth * clipW, clipW);
 }
 output.clipDistance = float3(view.z - projection.x, projection.y - view.z, 1);
 output.depth = view.z;
 output.uv = input.uv;
 output.world = world;
 output.pick = control.x;
 output.normalDiffuse = materialNormalDiffuse;
 output.shadowParameters = materialReceiver;
 output.fallback = fallback.xy;
 output.volume = float2(-1, 1);
 if (control.y != 0) {
  float3 rawNormal = float3(dot(normalToWorld[0].xyz, input.normal), dot(normalToWorld[1].xyz, input.normal), dot(normalToWorld[2].xyz, input.normal));
  // Inverse-transpose values can be finite while their squared length overflows.
  // Rescale first so highly unequal (but valid) instance axes remain lit.
  float normalScale = max(max(abs(rawNormal.x), abs(rawNormal.y)), abs(rawNormal.z));
  float3 n = normalize(rawNormal / max(normalScale, 1.0e-30));
  float3 toward = projection.z != 0 ? eye.xyz - world : towardViewer.xyz;
  if (control.z != 0 && dot(n, toward) < 0) n = -n;
  float facingLight = dot(n, lightAndBias.xyz);
  output.normalDiffuse = float4(n, saturate(facingLight));
  output.shadowParameters.x = lightAndBias.w + fallback.z * (1 - abs(facingLight));
  output.shadowParameters.yz = abs(facingLight) > 0.03
   ? -float2(dot(n, shadowRight.xyz), dot(n, shadowUp.xyz)) / facingLight : float2(0, 0);
  bool receives = flags.x == 2 ? facingLight > 0 : facingLight > 0.02;
  if (!receives) output.shadowParameters.w = (uint)materialReceiver.w & 3;
  if (control.w != 0) {
   // The CPU legacy volume path culls against the real world eye even for
   // orthographic views. Flat geometric normals keep the sign face-constant.
   float facingEye = dot(n, eye.xyz - world);
   output.clipDistance.z = facingEye;
   output.volume.y = facingEye;
   float legacyLight = (n.x + n.y + n.z) / 1.7320508;
   uint legacyShade = (uint)clamp(volumeParameters.x + (legacyLight < -0.1 ? 2 : (legacyLight < 0.4 ? 1 : 0)), 0, 3);
   bool spriteFront = (input.volumeFace & 256) != 0;
   uint palette = input.volumeFace & 255;
   uint shade = spriteFront ? (uint)volumeParameters.x : legacyShade;
   output.shadowParameters.w = ((uint)output.shadowParameters.w & 4) | shade;
   output.fallback = float2(palette, legacyShade);
   output.volume.x = spriteFront ? -1 : (float)palette;
  }
 }
 return output;
}
)hlsl";

bool CreateMeshPipeline()
{
	if (MeshVertexShader && MeshInputLayout && MeshConstantBuffer)
		return true;
	ComOwner<ID3DBlob> code;
	ComOwner<ID3DBlob> errors;
	HRESULT result = D3DCompile(MeshShaderSource, sizeof(MeshShaderSource) - 1, "town_gpu_mesh", nullptr, nullptr,
	    "VS", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put());
	if (FAILED(result)) {
		std::string message = "D3D11 mesh shader compilation failed";
		if (errors)
			message.append(": ").append(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
		return Fail(message, result);
	}
	result = Device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, MeshVertexShader.put());
	if (FAILED(result))
		return Fail("Create mesh vertex shader", result);
	const D3D11_INPUT_ELEMENT_DESC layout[] {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(TownGpuMeshVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(TownGpuMeshVertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(TownGpuMeshVertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 1, DXGI_FORMAT_R32_UINT, 0, offsetof(TownGpuMeshVertex, volumeFace), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	result = Device->CreateInputLayout(layout, 4, code->GetBufferPointer(), code->GetBufferSize(), MeshInputLayout.put());
	if (FAILED(result))
		return Fail("Create mesh input layout", result);
	D3D11_BUFFER_DESC description {};
	description.ByteWidth = sizeof(MeshConstants);
	description.Usage = D3D11_USAGE_DYNAMIC;
	description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	result = Device->CreateBuffer(&description, nullptr, MeshConstantBuffer.put());
	return SUCCEEDED(result) || Fail("Create mesh constant buffer", result);
}

template <typename Range>
bool BoundedMeshValues(const Range &values)
{
	return std::all_of(values.begin(), values.end(), [](float value) { return Finite(value) && std::abs(value) <= 1000000; });
}

bool ValidMeshAffine(const TownGpuMeshAffine &matrix)
{
	return std::all_of(matrix.begin(), matrix.end(), [](const auto &row) { return BoundedMeshValues(row); });
}

// Compute the inverse transpose once per submission, independent of mesh size.
bool MeshNormalMatrix(const TownGpuMeshAffine &matrix, std::array<Float4, 3> &output)
{
	const auto &a = matrix[0];
	const auto &b = matrix[1];
	const auto &c = matrix[2];
	const double determinant = static_cast<double>(a[0]) * (static_cast<double>(b[1]) * c[2] - static_cast<double>(b[2]) * c[1])
	    - static_cast<double>(a[1]) * (static_cast<double>(b[0]) * c[2] - static_cast<double>(b[2]) * c[0])
	    + static_cast<double>(a[2]) * (static_cast<double>(b[0]) * c[1] - static_cast<double>(b[1]) * c[0]);
	if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12)
		return Fail("Singular GPU mesh instance transform");
	const auto value = [determinant](float x, float y, float z, float w) {
		return static_cast<float>((static_cast<double>(x) * y - static_cast<double>(z) * w) / determinant);
	};
	output = { Float4 { value(b[1], c[2], b[2], c[1]), value(b[2], c[0], b[0], c[2]), value(b[0], c[1], b[1], c[0]), 0 },
		Float4 { value(c[1], a[2], c[2], a[1]), value(c[2], a[0], c[0], a[2]), value(c[0], a[1], c[1], a[0]), 0 },
		Float4 { value(a[1], b[2], a[2], b[1]), value(a[2], b[0], a[0], b[2]), value(a[0], b[1], a[1], b[0]), 0 } };
	for (const Float4 &row : output) {
		if (!Finite(row.x) || !Finite(row.y) || !Finite(row.z))
			return Fail("Nonfinite GPU mesh normal transform");
	}
	return true;
}

void UpdateMeshResidentStats()
{
	MeshStats.cachedMeshes = MeshCache.size();
	MeshStats.residentBytes = MeshBytes;
}

void BeginMeshFrame()
{
	GeometryCommands.clear();
	MeshBatches.clear();
	MeshCamera = {};
	MeshCameraReady = false;
	MeshStats = {};
	UpdateMeshResidentStats();
}

void ResetMeshResources()
{
	BeginMeshFrame();
	MeshCache.clear();
	MeshBytes = 0;
	MeshConstantBuffer.reset();
	MeshInputLayout.reset();
	MeshVertexShader.reset();
	MeshStats = {};
}

bool MakeMeshRoom(size_t bytes)
{
	if (bytes > MaxMeshBytes)
		return Fail("GPU mesh payload exceeds the mesh-cache budget", S_OK, TownGpuFailureKind::Capacity);
	while (MeshBytes > MaxMeshBytes - bytes) {
		auto oldest = MeshCache.end();
		for (auto entry = MeshCache.begin(); entry != MeshCache.end(); ++entry) {
			if (entry->second.lastSeenFrame < FrameSerial
			    && (oldest == MeshCache.end() || entry->second.lastSeenFrame < oldest->second.lastSeenFrame))
				oldest = entry;
		}
		if (oldest == MeshCache.end())
			return Fail("GPU mesh-cache budget exceeded by current-frame resources", S_OK, TownGpuFailureKind::Capacity);
		MeshBytes -= oldest->second.bytes;
		MeshCache.erase(oldest);
		++MeshStats.evictions;
		UpdateMeshResidentStats();
	}
	UpdateMeshResidentStats();
	return true;
}

bool UploadMeshBuffer(const void *data, size_t bytes, UINT bind, ComOwner<ID3D11Buffer> &buffer)
{
	D3D11_BUFFER_DESC description {};
	description.ByteWidth = static_cast<UINT>(bytes);
	description.Usage = D3D11_USAGE_IMMUTABLE;
	description.BindFlags = bind;
	const D3D11_SUBRESOURCE_DATA input { data, 0, 0 };
	const HRESULT result = Device->CreateBuffer(&description, &input, buffer.put());
	return SUCCEEDED(result) || Fail("Upload immutable mesh buffer", result);
}

bool BindGeometryConstants(const Constants &constants)
{
	D3D11_MAPPED_SUBRESOURCE mapped {};
	const HRESULT result = Context->Map(ConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(result))
		return Fail("Map batch constants", result);
	std::memcpy(mapped.pData, &constants, sizeof(Constants));
	Context->Unmap(ConstantBuffer.get(), 0);
	return true;
}

void BindGeometryMaterial(const CachedTexture &texture, bool preservePicking, bool paletteBlend = false)
{
	ID3D11ShaderResourceView *resources[] { texture.texels->codes.get(), texture.texels->opacity.get(), texture.lut != nullptr ? texture.lut->view.get() : nullptr, ShadowResource.get(),
		paletteBlend ? PriorIndexedColorView.get() : nullptr, paletteBlend ? PaletteBlendResource.get() : nullptr };
	Context->PSSetShaderResources(0, 6, resources);
	Context->OMSetBlendState(paletteBlend ? PaletteBlendWriteState.get() : BlendStates[preservePicking ? 1 : 0].get(), nullptr, 0xFFFFFFFFU);
	Context->OMSetDepthStencilState(paletteBlend ? PaletteBlendDepthState.get() : DepthState.get(), 0);
}

bool DrawMeshBatch(const MeshBatch &batch)
{
	if (!BindGeometryConstants(batch.constants))
		return false;
	D3D11_MAPPED_SUBRESOURCE mapped {};
	const HRESULT result = Context->Map(MeshConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(result))
		return Fail("Map mesh instance constants", result);
	std::memcpy(mapped.pData, &batch.meshConstants, sizeof(MeshConstants));
	Context->Unmap(MeshConstantBuffer.get(), 0);
	Context->VSSetShader(MeshVertexShader.get(), nullptr, 0);
	Context->IASetInputLayout(MeshInputLayout.get());
	ID3D11Buffer *buffer = batch.mesh->vertices.get();
	const UINT stride = sizeof(TownGpuMeshVertex);
	const UINT offset = 0;
	Context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
	Context->IASetIndexBuffer(batch.mesh->indices.get(), DXGI_FORMAT_R32_UINT, 0);
	ID3D11Buffer *constants = MeshConstantBuffer.get();
	Context->VSSetConstantBuffers(1, 1, &constants);
	BindGeometryMaterial(*batch.texture, batch.preservePicking);
	Context->DrawIndexed(batch.count, batch.first, 0);
	++Status.drawCalls;
	return true;
}
#endif
} // namespace

bool TownGpuSetMeshCamera(const TownGpuMeshCamera &camera)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed || !GeometryCommands.empty())
		return Fail("Mesh camera must precede GPU geometry submission");
	if (!ValidMeshAffine(camera.worldToView) || !Finite(camera.focalPixels) || camera.focalPixels <= 0 || camera.focalPixels > 1000000
	    || !Finite(camera.centerX) || !Finite(camera.centerY) || std::abs(camera.centerX) > 1000000 || std::abs(camera.centerY) > 1000000
	    || !BoundedMeshValues(camera.eye) || !BoundedMeshValues(camera.towardViewer)
	    || (camera.nativeArithmetic && (!Finite(camera.heightScale) || camera.heightScale <= 0 || camera.heightScale > 1000000
	        || camera.worldToView[0][3] != 0 || camera.worldToView[1][3] != 0 || camera.worldToView[2][3] != 0)))
		return Fail("Invalid GPU mesh camera");
	MeshCamera = camera;
	MeshCameraReady = true;
	return true;
#else
	(void)camera;
	return false;
#endif
}

bool TownGpuHasMesh(TownGpuMeshIdentity identity)
{
#ifdef _WIN32
	return identity.stableKey != 0 && MeshCache.find({ identity.stableKey, identity.revision }) != MeshCache.end();
#else
	(void)identity;
	return false;
#endif
}

bool TownGpuUploadMesh(const TownGpuMeshUpload &input)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed)
		return Fail("No valid GPU frame is recording a mesh upload");
	if (input.identity.stableKey == 0 || input.vertices.empty() || input.indices.empty() || input.indices.size() % 3 != 0)
		return Fail("Invalid GPU mesh identity, vertex/index count or triangle-list layout");
	const TextureKey key { input.identity.stableKey, input.identity.revision };
	const auto found = MeshCache.find(key);
	if (found != MeshCache.end()) {
		if (found->second.vertexCount != input.vertices.size() || found->second.indexCount != input.indices.size())
			return Fail("GPU mesh key/revision reused with a different layout");
		found->second.lastSeenFrame = FrameSerial;
		return true;
	}
	// Check before multiplication. Mesh capacity is payload bytes, independent
	// of how many times its index ranges are drawn in the current frame.
	if (input.vertices.size() > MaxMeshBytes / sizeof(TownGpuMeshVertex) || input.indices.size() > MaxMeshBytes / sizeof(uint32_t))
		return Fail("GPU immutable mesh buffer/cache budget exceeded", S_OK, TownGpuFailureKind::Capacity);
	const size_t vertexBytes = input.vertices.size() * sizeof(TownGpuMeshVertex);
	const size_t indexBytes = input.indices.size() * sizeof(uint32_t);
	if (vertexBytes > std::numeric_limits<UINT>::max() || indexBytes > std::numeric_limits<UINT>::max()
	    || vertexBytes > MaxMeshBytes || indexBytes > MaxMeshBytes - vertexBytes)
		return Fail("GPU immutable mesh buffer/cache budget exceeded", S_OK, TownGpuFailureKind::Capacity);
	for (const TownGpuMeshVertex &vertex : input.vertices) {
		if (!BoundedMeshValues(vertex.position) || !BoundedMeshValues(vertex.uv) || !BoundedMeshValues(vertex.normal))
			return Fail("GPU mesh vertices must have finite bounded position, UV and normal");
	}
	bool flatNormals = true;
	bool flatVolumeFaces = true;
	for (size_t i = 0; i < input.indices.size(); i += 3) {
		for (size_t j = 0; j < 3; ++j) {
			if (input.indices[i + j] >= input.vertices.size())
				return Fail("GPU mesh index exceeds vertex count");
		}
		const auto &normal = input.vertices[input.indices[i]].normal;
		const double squared = static_cast<double>(normal[0]) * normal[0] + static_cast<double>(normal[1]) * normal[1] + static_cast<double>(normal[2]) * normal[2];
		flatNormals = flatNormals && squared > 0.000001 && normal == input.vertices[input.indices[i + 1]].normal && normal == input.vertices[input.indices[i + 2]].normal;
		const uint32_t face = input.vertices[input.indices[i]].volumeFace;
		flatVolumeFaces = flatVolumeFaces && face <= 511
		    && face == input.vertices[input.indices[i + 1]].volumeFace
		    && face == input.vertices[input.indices[i + 2]].volumeFace;
	}
	if (!MakeMeshRoom(vertexBytes + indexBytes))
		return false;
	const auto start = Clock::now();
	CachedMesh mesh {};
	if (!UploadMeshBuffer(input.vertices.data(), vertexBytes, D3D11_BIND_VERTEX_BUFFER, mesh.vertices)
	    || !UploadMeshBuffer(input.indices.data(), indexBytes, D3D11_BIND_INDEX_BUFFER, mesh.indices))
		return false;
	mesh.vertexCount = input.vertices.size();
	mesh.indexCount = input.indices.size();
	mesh.bytes = vertexBytes + indexBytes;
	mesh.lastSeenFrame = mesh.uploadFrame = FrameSerial;
	mesh.flatNormals = flatNormals;
	mesh.flatVolumeFaces = flatVolumeFaces;
	MeshCache.emplace(key, std::move(mesh));
	MeshBytes += vertexBytes + indexBytes;
	++MeshStats.uploads;
	MeshStats.uploadedVertexBytes += vertexBytes;
	MeshStats.uploadedIndexBytes += indexBytes;
	MeshStats.uploadMilliseconds += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	UpdateMeshResidentStats();
	return true;
#else
	(void)input;
	return false;
#endif
}

bool TownGpuSubmitMeshInstance(const TownGpuMeshInstance &instance, const TownGpuTexture &texture, const TownGpuMaterial &material)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed)
		return Fail("No valid GPU frame/mesh camera is recording");
	if (material.paletteBlend)
		return Fail("Native palette overlays require projected triangles; resident mesh overlays are unsupported", S_OK, TownGpuFailureKind::Unsupported);
	if (!MeshCameraReady)
		return Fail("No valid GPU frame/mesh camera is recording");
	if (FramePaletteOverlayStarted)
		return Fail("Opaque GPU resident geometry must precede native palette overlays");
	const auto found = MeshCache.find({ instance.identity.stableKey, instance.identity.revision });
	if (instance.identity.stableKey == 0 || found == MeshCache.end())
		return Fail("GPU mesh identity is not resident; upload required");
	CachedMesh &mesh = found->second;
	if (instance.firstIndex >= mesh.indexCount || instance.firstIndex % 3 != 0)
		return Fail("Invalid GPU mesh first index");
	const size_t count = instance.indexCount == 0 ? mesh.indexCount - instance.firstIndex : instance.indexCount;
	if (count % 3 != 0 || count > mesh.indexCount - instance.firstIndex)
		return Fail("Invalid GPU mesh index range");
	if (count / 3 > std::numeric_limits<size_t>::max() - Status.submittedTriangles)
		return Fail("GPU submitted-triangle counter overflow", S_OK, TownGpuFailureKind::Capacity);
	if (!ValidMeshAffine(instance.localToWorld) || !BoundedMeshValues(instance.toLight)
	    || !Finite(instance.shadowDepthBias) || !Finite(instance.shadowSlopeBias)
	    || instance.shadowDepthBias < 0 || instance.shadowDepthBias > 1 || instance.shadowSlopeBias < 0 || instance.shadowSlopeBias > 1
	    || (instance.vertexNormals && !mesh.flatNormals))
		return Fail("Invalid GPU mesh instance/flat-normal lighting parameters");
	if (instance.volumeMaterial && (!instance.vertexNormals || instance.doubleSidedNormals || !mesh.flatVolumeFaces
	        || instance.volumeLighting < 0 || instance.volumeLighting > 3 || material.lighting != TownGpuLighting::Shadow))
		return Fail("Invalid GPU legacy volume material, face attributes or lighting");
	if (instance.volumeMaterial) {
		const auto &a = instance.localToWorld[0];
		const auto &b = instance.localToWorld[1];
		const auto &c = instance.localToWorld[2];
		const double determinant = static_cast<double>(a[0]) * (static_cast<double>(b[1]) * c[2] - static_cast<double>(b[2]) * c[1])
		    - static_cast<double>(a[1]) * (static_cast<double>(b[0]) * c[2] - static_cast<double>(b[2]) * c[0])
		    + static_cast<double>(a[2]) * (static_cast<double>(b[0]) * c[1] - static_cast<double>(b[1]) * c[0]);
		if (determinant <= 0)
			return Fail("Legacy volume transform must preserve geometric winding");
	}
	MeshConstants meshConstants {};
	if (!MeshNormalMatrix(instance.localToWorld, meshConstants.normalToWorld))
		return false;
	const double lightLength = std::sqrt(static_cast<double>(instance.toLight[0]) * instance.toLight[0]
	    + static_cast<double>(instance.toLight[1]) * instance.toLight[1] + static_cast<double>(instance.toLight[2]) * instance.toLight[2]);
	if (instance.vertexNormals && lightLength <= 0.000001)
		return Fail("Invalid GPU mesh directional-light vector");
	CachedTexture *uploaded = GetTexture(texture, material.lighting);
	if (uploaded == nullptr)
		return false;
	if (instance.volumeMaterial && (uploaded->lightLevels != 16 || uploaded->codeCount != 256))
		return Fail("Legacy volume requires the full native palette/shadow LUT");
	Constants constants {};
	if (!MakeConstants(material, *uploaded, constants) || !CreateMeshPipeline())
		return false;
	for (size_t i = 0; i < 3; ++i) {
		const auto &view = MeshCamera.worldToView[i];
		const auto &world = instance.localToWorld[i];
		meshConstants.worldToView[i] = { view[0], view[1], view[2], view[3] };
		meshConstants.localToWorld[i] = { world[0], world[1], world[2], world[3] };
	}
	meshConstants.camera = { MeshCamera.focalPixels, MeshCamera.centerX, MeshCamera.centerY, MeshCamera.nativeArithmetic ? MeshCamera.heightScale : 1 };
	meshConstants.eye = { MeshCamera.eye[0], MeshCamera.eye[1], MeshCamera.eye[2], 0 };
	meshConstants.towardViewer = { MeshCamera.towardViewer[0], MeshCamera.towardViewer[1], MeshCamera.towardViewer[2], 0 };
	const float inverseLight = lightLength > 0.000001 ? static_cast<float>(1 / lightLength) : 0;
	meshConstants.lightAndBias = { instance.toLight[0] * inverseLight, instance.toLight[1] * inverseLight, instance.toLight[2] * inverseLight, instance.shadowDepthBias };
	meshConstants.normalDiffuse = { material.normal[0], material.normal[1], material.normal[2], std::clamp(material.diffuse, 0.0F, 1.0F) };
	meshConstants.receiver = { material.shadowBias, material.shadowSlopeU, material.shadowSlopeV,
		static_cast<float>(std::clamp(material.shade, 0, 3) + (material.receivesShadow ? 4 : 0)) };
	meshConstants.fallback = { static_cast<float>(material.fallbackPaletteIndex), static_cast<float>(std::clamp(material.fallbackShade, 0, 3)), instance.shadowSlopeBias, 0 };
	meshConstants.volumeParameters = { static_cast<float>(instance.volumeLighting), MeshCamera.nativeArithmetic ? 1.0F : 0.0F, 0, 0 };
	meshConstants.control = { instance.pickId, instance.vertexNormals ? 1U : 0U, instance.doubleSidedNormals ? 1U : 0U, instance.volumeMaterial ? 1U : 0U };
	GeometryCommands.push_back({ true, MeshBatches.size() });
	MeshBatches.push_back({ &mesh, uploaded, constants, meshConstants, instance.firstIndex, static_cast<UINT>(count), material.preservePicking });
	mesh.lastSeenFrame = FrameSerial;
	++MeshStats.instances;
	if (mesh.uploadFrame != FrameSerial)
		++MeshStats.reusedInstances;
	Status.submittedTriangles += count / 3;
	return true;
#else
	(void)instance;
	(void)texture;
	(void)material;
	return false;
#endif
}

const TownGpuMeshStats &GetTownGpuMeshStats()
{
	return MeshStats;
}
