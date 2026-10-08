#include "engine/render/town_gpu.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <cstdio>
#include <unordered_map>
#endif

namespace devilution {
namespace {

TownGpuStatus Status;

#ifdef _WIN32
using Clock = std::chrono::steady_clock;
constexpr size_t MaxPixels = 4 * 1024 * 1024;
constexpr size_t MaxTextureBytes = 256 * 1024 * 1024;
constexpr size_t MaxTriangles = 1024 * 1024;

template <typename T>
class ComOwner {
	T *value = nullptr;

public:
	ComOwner() = default;
	~ComOwner() { reset(); }
	ComOwner(const ComOwner &) = delete;
	ComOwner &operator=(const ComOwner &) = delete;
	ComOwner(ComOwner &&other) noexcept
	    : value(std::exchange(other.value, nullptr))
	{
	}
	ComOwner &operator=(ComOwner &&other) noexcept
	{
		if (this != &other) {
			reset();
			value = std::exchange(other.value, nullptr);
		}
		return *this;
	}
	void reset()
	{
		if (value != nullptr)
			value->Release();
		value = nullptr;
	}
	T **put()
	{
		reset();
		return &value;
	}
	T *get() const { return value; }
	T *operator->() const { return value; }
	explicit operator bool() const { return value != nullptr; }
};

struct Float4 { float x, y, z, w; };
struct UInt4 { uint32_t x, y, z, w; };

// All members occupy complete HLSL constant-buffer registers. Value-initialize
// the structure before filling it so bytewise batch comparison has no padding.
struct Constants {
	Float4 target;
	Float4 projection;
	UInt4 flags;
	Float4 shading;
	Float4 normal;
	Float4 roomMinimum;
	Float4 roomMaximum;
	Float4 parameters;
	Float4 shadowRight;
	Float4 shadowUp;
	Float4 shadowLight;
	Float4 shadowScale;
	std::array<Float4, TownMaxPointLights> lightPositionRadius;
	std::array<Float4, TownMaxPointLights> lightRedIntensity;
	std::array<Float4, TownMaxLightApertures> apertureBounds;
	std::array<Float4, TownMaxLightApertures> aperturePlanes;
	std::array<Float4, TownMaxLightOccluders - 1> blockerMinimum;
	std::array<Float4, TownMaxLightOccluders - 1> blockerMaximum;
};
static_assert(sizeof(Constants) % 16 == 0);

struct GpuVertex {
	TownGpuVertex vertex;
	uint32_t pickId;
	Float4 normalDiffuse;
	Float4 shadowParameters;
	std::array<float, 2> fallback;
};
static_assert(sizeof(TownGpuVertex) == 36);
static_assert(sizeof(GpuVertex) == 80);

struct CachedTexture {
	ComOwner<ID3D11ShaderResourceView> codes;
	ComOwner<ID3D11ShaderResourceView> opacity;
	ComOwner<ID3D11ShaderResourceView> lut;
	int width;
	int height;
	unsigned lightLevels;
	unsigned codeCount;
	unsigned lutWidth;
	uint32_t maximumCode;
	bool hasOpacity;
	size_t bytes;
	uint64_t lastSeenFrame;
};

struct TextureKey {
	uint64_t key;
	uint64_t revision;
	bool operator==(const TextureKey &other) const { return key == other.key && revision == other.revision; }
};
struct TextureKeyHash {
	size_t operator()(TextureKey value) const
	{
		return static_cast<size_t>(value.key ^ (value.revision + 0x9E3779B97F4A7C15ULL + (value.key << 6) + (value.key >> 2)));
	}
};

struct Batch {
	CachedTexture *texture;
	Constants constants;
	UINT first;
	UINT count;
	bool preservePicking;
};

struct Target {
	ComOwner<ID3D11Texture2D> texture;
	ComOwner<ID3D11RenderTargetView> view;
	ComOwner<ID3D11Texture2D> staging;
};

ComOwner<ID3D11Device> Device;
ComOwner<ID3D11DeviceContext> Context;
ComOwner<ID3D11VertexShader> VertexShader;
ComOwner<ID3D11PixelShader> PixelShader;
ComOwner<ID3D11InputLayout> InputLayout;
ComOwner<ID3D11Buffer> VertexBuffer;
ComOwner<ID3D11Buffer> ConstantBuffer;
ComOwner<ID3D11RasterizerState> Rasterizer;
ComOwner<ID3D11DepthStencilState> DepthState;
std::array<ComOwner<ID3D11BlendState>, 2> BlendStates;
std::array<Target, 3> Targets;
ComOwner<ID3D11Texture2D> HardwareDepth;
ComOwner<ID3D11DepthStencilView> HardwareDepthView;
ComOwner<ID3D11ShaderResourceView> ShadowResource;
std::unordered_map<TextureKey, CachedTexture, TextureKeyHash> TextureCache;
size_t TextureBytes = 0;
size_t VertexCapacity = 0;
uint64_t FrameSerial = 0;
std::vector<GpuVertex> Vertices;
std::vector<Batch> Batches;
TownGpuShadow FrameShadow;
TownGpuProjection FrameProjection;
uint64_t ShadowKey = 0;
uint64_t ShadowRevision = 0;
int ShadowResolution = 0;
int Width = 0;
int Height = 0;
bool FrameActive = false;
bool FrameFailed = false;
Clock::time_point FrameStart;

constexpr char ShaderSource[] = R"hlsl(
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
 float4 lightPositionRadius[8];
 float4 lightRedIntensity[8];
 float4 apertureBounds[8];
 float4 aperturePlanes[8];
 float4 blockerMinimum[31];
 float4 blockerMaximum[31];
};
Texture2D<uint> codes : register(t0);
Texture2D<uint> opacity : register(t1);
Texture2D<uint> lightLut : register(t2);
Texture2D<float> shadowDepth : register(t3);
struct Input {
 float2 position : POSITION;
 float depth : TEXCOORD0;
 float2 uv : TEXCOORD1;
 float3 world : TEXCOORD2;
 float clipW : TEXCOORD6;
 uint pick : COLOR0;
 float4 normalDiffuse : TEXCOORD3;
 float4 shadowParameters : TEXCOORD4;
 float2 fallback : TEXCOORD5;
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
};
Interpolated VS(Input input) {
 Interpolated output;
 float normalizedDepth = projection.z != 0
  ? saturate(projection.y * (input.depth - projection.x) / ((projection.y - projection.x) * input.depth))
  : input.depth / 4096.0;
 output.position = float4((input.position.x * 2 / target.x - 1) * input.clipW,
  (1 - input.position.y * 2 / target.y) * input.clipW, normalizedDepth * input.clipW, input.clipW);
 output.depth = input.depth;
 output.uv = input.uv;
 output.world = input.world;
 output.pick = input.pick;
 output.normalDiffuse = input.normalDiffuse;
 output.shadowParameters = input.shadowParameters;
 output.fallback = input.fallback;
 return output;
}
float directionalShadow(float3 world, float4 receiver) {
 if ((flags.w & 8) == 0 || parameters.w < 1 || (((uint)receiver.w & 4) == 0)) return 0;
 float2 texel = float2((dot(world, shadowRight.xyz) - shadowRight.w) / shadowLight.w,
  (dot(world, shadowUp.xyz) - shadowUp.w) / shadowScale.x);
 int resolution = (int)parameters.w;
 if (any(texel < 0) || any(texel >= resolution)) return 0;
 int2 center = (int2)texel;
 int radius = (int)shadowScale.y;
 float occluded = 0;
 float taps = 0;
 float receiverDepth = dot(world, shadowLight.xyz);
 [loop] for (int dv = -radius; dv <= radius; ++dv) {
  [loop] for (int du = -radius; du <= radius; ++du) {
   taps += 1;
   int2 samplePoint = center + int2(du, dv);
   if (any(samplePoint < 0) || any(samplePoint >= resolution)) continue;
   float plane = receiverDepth + (samplePoint.x + 0.5 - texel.x) * shadowLight.w * receiver.y
    + (samplePoint.y + 0.5 - texel.y) * shadowScale.x * receiver.z;
   occluded += shadowDepth.Load(int3(samplePoint, 0)) > plane + receiver.x ? 1 : 0;
  }
 }
 return occluded / max(taps, 1);
}
bool openingAt(float3 hit, int axis, float boundary) {
 float2 uv = float2(axis == 0 ? hit.z : hit.x, axis == 1 ? hit.z : hit.y);
 [loop] for (int i = 0; i < (int)parameters.z; ++i) {
  float4 plane = aperturePlanes[i];
  float4 bounds = apertureBounds[i];
  if ((int)plane.x != axis || abs(plane.y - boundary) > 0.0001) continue;
  if (uv.x <= bounds.x + 0.0001 || uv.x >= bounds.y - 0.0001
   || uv.y <= bounds.z + 0.0001 || uv.y >= bounds.w - 0.0001) continue;
  int sides = (int)plane.z;
  bool inside = true;
  if (sides != 0) {
   float2 normalized = (uv - float2(bounds.x + bounds.y, bounds.z + bounds.w) * 0.5)
    / (float2(bounds.y - bounds.x, bounds.w - bounds.z) * 0.5);
   float sideDistance = cos(3.14159265358979323846 / sides);
   [loop] for (int side = 0; side < sides; ++side) {
    float angle = 6.28318530717958647692 * (side + 0.5) / sides;
    if (dot(normalized, float2(cos(angle), sin(angle))) >= sideDistance - 0.0001) inside = false;
   }
  }
  if (inside) return true;
 }
 return false;
}
bool crossingOpen(float3 origin, float3 delta, float t) {
 if (t <= 0.00001 || t >= 0.99999) return true;
 float3 hit = origin + delta * t;
 [unroll] for (int axis = 0; axis < 3; ++axis) {
  if (abs(delta[axis]) <= 0.000001) continue;
  if (abs(hit[axis] - roomMinimum[axis]) <= 0.0001 && !openingAt(hit, axis, roomMinimum[axis])) return false;
  if (abs(hit[axis] - roomMaximum[axis]) <= 0.0001 && !openingAt(hit, axis, roomMaximum[axis])) return false;
 }
 return true;
}
bool visibleThroughRoom(float3 origin, float3 receiver) {
 if (roomMinimum.w == 0) return true;
 float3 delta = receiver - origin;
 float entry = 0;
 float exitPoint = 1;
 [unroll] for (int axis = 0; axis < 3; ++axis) {
  if (abs(delta[axis]) <= 0.000001) {
   if (origin[axis] < roomMinimum[axis] || origin[axis] > roomMaximum[axis]) return true;
  } else {
   float first = (roomMinimum[axis] - origin[axis]) / delta[axis];
   float second = (roomMaximum[axis] - origin[axis]) / delta[axis];
   entry = max(entry, min(first, second));
   exitPoint = min(exitPoint, max(first, second));
   if (entry > exitPoint) return true;
  }
 }
 return crossingOpen(origin, delta, entry) && crossingOpen(origin, delta, exitPoint);
}
bool opaqueCrossingOpen(float3 origin, float3 delta, float t, float3 minimum, float3 maximum) {
 if (t <= 0.00001 || t >= 0.99999) return true;
 float3 hit = origin + delta * t;
 [unroll] for (int axis = 0; axis < 3; ++axis) {
  if (abs(delta[axis]) <= 0.000001) continue;
  if (abs(hit[axis] - minimum[axis]) <= 0.0001 || abs(hit[axis] - maximum[axis]) <= 0.0001) return false;
 }
 return true;
}
bool visibleThroughOpaqueBlocker(float3 origin, float3 receiver, float3 minimum, float3 maximum) {
 float3 delta = receiver - origin;
 float entry = 0;
 float exitPoint = 1;
 [unroll] for (int axis = 0; axis < 3; ++axis) {
  if (abs(delta[axis]) <= 0.000001) {
   if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return true;
  } else {
   float first = (minimum[axis] - origin[axis]) / delta[axis];
   float second = (maximum[axis] - origin[axis]) / delta[axis];
   entry = max(entry, min(first, second));
   exitPoint = min(exitPoint, max(first, second));
   if (entry > exitPoint) return true;
  }
 }
 return opaqueCrossingOpen(origin, delta, entry, minimum, maximum)
  && opaqueCrossingOpen(origin, delta, exitPoint, minimum, maximum);
}
bool visibleThrough(float3 origin, float3 receiver) {
 if (!visibleThroughRoom(origin, receiver)) return false;
 [loop] for (int i = 0; i < (int)shadowScale.w; ++i) {
  if (!visibleThroughOpaqueBlocker(origin, receiver, blockerMinimum[i].xyz, blockerMaximum[i].xyz)) return false;
 }
 return true;
}
float pointAmount(float3 world, float3 authoredNormal) {
 float red = 0;
 float normalLength = length(authoredNormal);
 if (normalLength <= 0) return 0;
 float3 normal = authoredNormal / normalLength;
 [loop] for (int i = 0; i < (int)parameters.y; ++i) {
  float4 light = lightPositionRadius[i];
  float3 direction = light.xyz - world;
  float distanceSquared = dot(direction, direction);
  float radiusSquared = light.w * light.w;
  if (distanceSquared >= radiusSquared || distanceSquared <= 0.00000001) continue;
  float diffuse = max(0, dot(normal, direction * rsqrt(distanceSquared)));
  if (diffuse <= 0 || !visibleThrough(light.xyz, world)) continue;
  float range = 1 - distanceSquared / radiusSquared;
  red += lightRedIntensity[i].x * lightRedIntensity[i].y * diffuse * range * range / (1 + distanceSquared);
 }
 return saturate(red / normalAndRed.w / parameters.x);
}
struct Pixel {
 uint color : SV_Target0;
 uint pick : SV_Target1;
 float depth : SV_Target2;
};
Pixel PS(Interpolated input) {
 uint width, height;
 codes.GetDimensions(width, height);
 float2 uv = input.uv;
 if ((flags.w & 1) != 0) uv = frac(uv);
 bool absent = (flags.w & 4) != 0 && (any(uv < 0) || any(uv >= 1));
 int2 texel = (int2)clamp(uv * float2(width, height), float2(0, 0), float2(width - 1, height - 1));
 absent = absent || ((flags.w & 4) != 0 && opacity.Load(int3(texel, 0)) == 0);
 uint code = codes.Load(int3(texel, 0));
 absent = absent || ((flags.w & 2) != 0 && code == 0);
 if (absent) {
  if (input.fallback.x < 0) discard;
  code = (uint)input.fallback.x;
 }
 uint color = code;
 if (flags.x != 0) {
  uint level = 0;
  if (flags.x == 1) level = ((uint)(absent ? input.fallback.y : input.shadowParameters.w) & 3) * 4
   + (uint)(directionalShadow(input.world, input.shadowParameters) * 3 + 0.5);
  else if (flags.x == 2) level = (uint)(saturate(input.normalDiffuse.w * (1 - directionalShadow(input.world, input.shadowParameters))) * (flags.y - 1) + 0.5);
  else level = (uint)(pointAmount(input.world, input.normalDiffuse.xyz) * (flags.y - 1) + 0.5);
  uint lutIndex = min(code, flags.z - 1) * flags.y + min(level, flags.y - 1);
  uint lutWidth = max(1, (uint)shadowScale.z);
  color = lightLut.Load(int3(lutIndex % lutWidth, lutIndex / lutWidth, 0));
 }
 Pixel output;
 output.color = color;
 output.pick = input.pick;
 output.depth = input.depth;
 return output;
}
)hlsl";

bool Fail(const std::string &message, HRESULT result = S_OK)
{
	Status.frameSucceeded = false;
	Status.failure = message;
	if (FAILED(result)) {
		char code[24];
		std::snprintf(code, sizeof(code), " (HRESULT %08lX)", static_cast<unsigned long>(result));
		Status.failure += code;
	}
	FrameFailed = true;
	return false;
}

bool Compile(const char *entry, const char *profile, ComOwner<ID3DBlob> &blob)
{
	ComOwner<ID3DBlob> errors;
	const HRESULT result = D3DCompile(ShaderSource, sizeof(ShaderSource) - 1, "town_gpu", nullptr, nullptr,
	    entry, profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.put(), errors.put());
	if (FAILED(result)) {
		std::string message = "D3D11 shader compilation failed";
		if (errors)
			message.append(": ").append(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
		return Fail(message, result);
	}
	return true;
}

bool CreateDevice(bool allowWarp)
{
	D3D_FEATURE_LEVEL feature {};
	const D3D_FEATURE_LEVEL levels[] { D3D_FEATURE_LEVEL_11_0 };
	HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
	    levels, 1, D3D11_SDK_VERSION, Device.put(), &feature, Context.put());
	Status.warp = false;
	if (FAILED(result) && allowWarp) {
		result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
		    levels, 1, D3D11_SDK_VERSION, Device.put(), &feature, Context.put());
		Status.warp = SUCCEEDED(result);
	}
	if (FAILED(result))
		return Fail("D3D11 hardware device unavailable; no production software fallback", result);
	Status.featureLevel = static_cast<uint32_t>(feature);
	ComOwner<IDXGIDevice> dxgiDevice;
	ComOwner<IDXGIAdapter> adapter;
	DXGI_ADAPTER_DESC description {};
	if (SUCCEEDED(Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(dxgiDevice.put())))
	    && SUCCEEDED(dxgiDevice->GetAdapter(adapter.put())) && SUCCEEDED(adapter->GetDesc(&description))) {
		char name[512] {};
		WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name), nullptr, nullptr);
		Status.adapter = name;
	}
	ComOwner<ID3DBlob> vertexCode;
	ComOwner<ID3DBlob> pixelCode;
	if (!Compile("VS", "vs_5_0", vertexCode) || !Compile("PS", "ps_5_0", pixelCode))
		return false;
	result = Device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, VertexShader.put());
	if (FAILED(result))
		return Fail("CreateVertexShader", result);
	result = Device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, PixelShader.put());
	if (FAILED(result))
		return Fail("CreatePixelShader", result);
	const D3D11_INPUT_ELEMENT_DESC layout[] {
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 6, DXGI_FORMAT_R32_FLOAT, 0, offsetof(TownGpuVertex, clipW), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(GpuVertex, pickId), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertex, normalDiffuse), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(GpuVertex, shadowParameters), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 5, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(GpuVertex, fallback), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	result = Device->CreateInputLayout(layout, 9, vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), InputLayout.put());
	if (FAILED(result))
		return Fail("CreateInputLayout", result);
	D3D11_BUFFER_DESC constantDescription {};
	constantDescription.ByteWidth = sizeof(Constants);
	constantDescription.Usage = D3D11_USAGE_DYNAMIC;
	constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	result = Device->CreateBuffer(&constantDescription, nullptr, ConstantBuffer.put());
	if (FAILED(result))
		return Fail("Create constant buffer", result);
	D3D11_RASTERIZER_DESC raster {};
	raster.FillMode = D3D11_FILL_SOLID;
	raster.CullMode = D3D11_CULL_NONE;
	raster.DepthClipEnable = TRUE;
	result = Device->CreateRasterizerState(&raster, Rasterizer.put());
	if (FAILED(result))
		return Fail("Create rasterizer state", result);
	D3D11_DEPTH_STENCIL_DESC depth {};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	result = Device->CreateDepthStencilState(&depth, DepthState.put());
	if (FAILED(result))
		return Fail("Create depth state", result);
	for (size_t i = 0; i < BlendStates.size(); ++i) {
		D3D11_BLEND_DESC blend {};
		blend.IndependentBlendEnable = TRUE;
		for (auto &target : blend.RenderTarget)
			target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (i != 0)
			blend.RenderTarget[1].RenderTargetWriteMask = 0;
		result = Device->CreateBlendState(&blend, BlendStates[i].put());
		if (FAILED(result))
			return Fail("Create pick-preservation blend state", result);
	}
	Status.available = true;
	return true;
}

bool CreateTargets(int width, int height)
{
	if (Width == width && Height == height && HardwareDepthView)
		return true;
	Width = Height = 0;
	HardwareDepthView.reset();
	const DXGI_FORMAT formats[] { DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_FLOAT };
	for (size_t i = 0; i < Targets.size(); ++i) {
		D3D11_TEXTURE2D_DESC description {};
		description.Width = width;
		description.Height = height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = formats[i];
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_RENDER_TARGET;
		HRESULT result = Device->CreateTexture2D(&description, nullptr, Targets[i].texture.put());
		if (FAILED(result))
			return Fail("Create offscreen render target", result);
		result = Device->CreateRenderTargetView(Targets[i].texture.get(), nullptr, Targets[i].view.put());
		if (FAILED(result))
			return Fail("Create offscreen render-target view", result);
		description.Usage = D3D11_USAGE_STAGING;
		description.BindFlags = 0;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		result = Device->CreateTexture2D(&description, nullptr, Targets[i].staging.put());
		if (FAILED(result))
			return Fail("Create readback texture", result);
	}
	D3D11_TEXTURE2D_DESC description {};
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_D32_FLOAT;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	HRESULT result = Device->CreateTexture2D(&description, nullptr, HardwareDepth.put());
	if (FAILED(result))
		return Fail("Create GPU depth texture", result);
	result = Device->CreateDepthStencilView(HardwareDepth.get(), nullptr, HardwareDepthView.put());
	if (FAILED(result))
		return Fail("Create GPU depth view", result);
	Width = width;
	Height = height;
	return true;
}

bool UploadTexture(int width, int height, DXGI_FORMAT format, const void *data, UINT pitch,
    ComOwner<ID3D11ShaderResourceView> &resource)
{
	D3D11_TEXTURE2D_DESC description {};
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = format;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_IMMUTABLE;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	const D3D11_SUBRESOURCE_DATA input { data, pitch, 0 };
	ComOwner<ID3D11Texture2D> texture;
	HRESULT result = Device->CreateTexture2D(&description, &input, texture.put());
	if (FAILED(result))
		return Fail("Upload immutable texture", result);
	result = Device->CreateShaderResourceView(texture.get(), nullptr, resource.put());
	return SUCCEEDED(result) || Fail("Create uploaded texture view", result);
}

CachedTexture *GetTexture(const TownGpuTexture &input, TownGpuLighting lighting)
{
	if (input.width <= 0 || input.height <= 0 || input.width > 16384 || input.height > 16384
	    || input.lightLevels == 0 || input.lightLevels > 16384) {
		Fail("Invalid GPU texture dimensions or light-level count");
		return nullptr;
	}
	const TextureKey key { input.stableKey, input.revision };
	const auto found = TextureCache.find(key);
	if (found != TextureCache.end()) {
		if (found->second.width != input.width || found->second.height != input.height || found->second.lightLevels != input.lightLevels
		    || (lighting != TownGpuLighting::Unlit && !found->second.lut)
		    || (lighting == TownGpuLighting::Unlit && found->second.maximumCode > 255)) {
			Fail("GPU texture key/revision reused with a different layout");
			return nullptr;
		}
		found->second.lastSeenFrame = FrameSerial;
		return &found->second;
	}
	const size_t count = static_cast<size_t>(input.width) * input.height;
	if (count > MaxPixels || input.texelCodes.size() != count || (!input.opacity.empty() && input.opacity.size() != count)
	    || input.lightLut.size() % input.lightLevels != 0 || input.lightLut.size() / input.lightLevels > 262144
	    || (lighting != TownGpuLighting::Unlit && input.lightLut.empty())) {
		Fail("Invalid GPU texture payload");
		return nullptr;
	}
	const unsigned codeCount = input.lightLut.empty() ? 256 : static_cast<unsigned>(input.lightLut.size() / input.lightLevels);
	for (uint32_t code : input.texelCodes) {
		if (code >= codeCount || (lighting == TownGpuLighting::Unlit && code > 255)) {
			Fail("GPU texel code exceeds its palette/LUT");
			return nullptr;
		}
	}
	const size_t bytes = count * sizeof(uint32_t) + input.opacity.size() + input.lightLut.size();
	if (bytes > MaxTextureBytes || TextureBytes > MaxTextureBytes - bytes) {
		Fail("GPU texture-cache budget exceeded; reset required");
		return nullptr;
	}
	CachedTexture texture {};
	texture.width = input.width;
	texture.height = input.height;
	texture.lightLevels = input.lightLevels;
	texture.codeCount = codeCount;
	texture.lutWidth = static_cast<unsigned>(std::min<size_t>(16384, input.lightLut.size()));
	texture.maximumCode = *std::max_element(input.texelCodes.begin(), input.texelCodes.end());
	texture.hasOpacity = !input.opacity.empty();
	texture.bytes = bytes;
	texture.lastSeenFrame = FrameSerial;
	if (!UploadTexture(input.width, input.height, DXGI_FORMAT_R32_UINT, input.texelCodes.data(), input.width * sizeof(uint32_t), texture.codes)
	    || (!input.opacity.empty() && !UploadTexture(input.width, input.height, DXGI_FORMAT_R8_UINT, input.opacity.data(), input.width, texture.opacity)))
		return nullptr;
	if (!input.lightLut.empty()) {
		const size_t lutHeight = (input.lightLut.size() + texture.lutWidth - 1) / texture.lutWidth;
		std::vector<uint8_t> padded;
		const uint8_t *lutData = input.lightLut.data();
		if (lutHeight * texture.lutWidth != input.lightLut.size()) {
			padded.assign(input.lightLut.begin(), input.lightLut.end());
			padded.resize(lutHeight * texture.lutWidth);
			lutData = padded.data();
		}
		if (!UploadTexture(texture.lutWidth, static_cast<int>(lutHeight), DXGI_FORMAT_R8_UINT, lutData, texture.lutWidth, texture.lut))
			return nullptr;
	}
	TextureBytes += bytes;
	return &TextureCache.emplace(key, std::move(texture)).first->second;
}

bool Finite(float value) { return std::isfinite(value); }
bool Finite(TownLightVector value) { return Finite(value.x) && Finite(value.height) && Finite(value.z); }

bool ValidOpaqueBlocker(const TownLightOccluder &blocker)
{
	const auto bounded = [](TownLightVector value) {
		return Finite(value) && std::abs(value.x) <= 1000000 && std::abs(value.height) <= 1000000 && std::abs(value.z) <= 1000000;
	};
	return bounded(blocker.minimum) && bounded(blocker.maximum)
	    && blocker.minimum.x < blocker.maximum.x && blocker.minimum.height < blocker.maximum.height
	    && blocker.minimum.z < blocker.maximum.z && blocker.apertures.empty();
}

bool MakeConstants(const TownGpuMaterial &material, const CachedTexture &texture, Constants &output)
{
	if (static_cast<uint32_t>(material.lighting) > static_cast<uint32_t>(TownGpuLighting::Interior)
	    || !Finite(material.diffuse) || !Finite(material.shadowBias) || !Finite(material.shadowSlopeU) || !Finite(material.shadowSlopeV)
	    || !std::all_of(material.normal.begin(), material.normal.end(), [](float value) { return Finite(value); })
	    || material.fallbackPaletteIndex < -1 || material.fallbackPaletteIndex > 255
	    || (material.fallbackPaletteIndex >= 0 && material.lighting != TownGpuLighting::Shadow && material.lighting != TownGpuLighting::Unlit)
	    || (material.lighting == TownGpuLighting::Shadow && material.fallbackPaletteIndex >= static_cast<int>(texture.codeCount))
	    || (material.lighting == TownGpuLighting::Shadow && texture.lightLevels != 16)
	    || (material.lighting == TownGpuLighting::Interior
	        && (!Finite(material.interiorRedNormalization) || material.interiorRedNormalization <= 0
	            || !Finite(material.interiorPointRange) || material.interiorPointRange <= 0)))
		return Fail("Invalid GPU material parameters");
	if (material.blockers.size() > TownMaxLightOccluders - 1
	    || !std::all_of(material.blockers.begin(), material.blockers.end(), ValidOpaqueBlocker))
		return Fail("Invalid GPU interior opaque blockers");
	output = {};
	output.target = { static_cast<float>(Width), static_cast<float>(Height), 0, 0 };
	output.projection = { FrameProjection.nearClip, FrameProjection.farClip, FrameProjection.perspective ? 1.0F : 0.0F, 0 };
	output.flags = { static_cast<uint32_t>(material.lighting), texture.lightLevels, texture.codeCount,
		static_cast<uint32_t>((material.repeat ? 1 : 0) | (material.transparentZero && !texture.hasOpacity ? 2 : 0)
		    | (texture.hasOpacity ? 4 : 0) | (FrameShadow.resolution > 0 ? 8 : 0)) };
	output.normal = { 0, 0, 0, material.interiorRedNormalization };
	output.parameters = { material.interiorPointRange, 0, 0, static_cast<float>(FrameShadow.resolution) };
	output.shadowRight = { FrameShadow.right[0], FrameShadow.right[1], FrameShadow.right[2], FrameShadow.minU };
	output.shadowUp = { FrameShadow.up[0], FrameShadow.up[1], FrameShadow.up[2], FrameShadow.minV };
	output.shadowLight = { FrameShadow.light[0], FrameShadow.light[1], FrameShadow.light[2], FrameShadow.texelU };
	output.shadowScale = { FrameShadow.texelV, static_cast<float>(FrameShadow.pcfRadius), static_cast<float>(texture.lutWidth), 0 };
	if (material.lighting != TownGpuLighting::Interior)
		return true;
	for (const TownPointLight &light : material.lights.first(std::min<size_t>(material.lights.size(), TownMaxPointLights))) {
		if (!Finite(light.position) || !Finite(light.radius) || light.radius <= 0 || light.radius > 256)
			continue;
		const size_t index = static_cast<size_t>(output.parameters.y++);
		output.lightPositionRadius[index] = { light.position.x, light.position.height, light.position.z, light.radius };
		output.lightRedIntensity[index] = { Finite(light.color.red) ? std::clamp(light.color.red, 0.0F, 16.0F) : 0,
			Finite(light.intensity) ? std::clamp(light.intensity, 0.0F, 16.0F) : 0, 0, 0 };
	}
	for (const TownLightOccluder &blocker : material.blockers) {
		const size_t index = static_cast<size_t>(output.shadowScale.w++);
		output.blockerMinimum[index] = { blocker.minimum.x, blocker.minimum.height, blocker.minimum.z, 0 };
		output.blockerMaximum[index] = { blocker.maximum.x, blocker.maximum.height, blocker.maximum.z, 0 };
	}
	if (material.room == nullptr)
		return true;
	const TownLightOccluder &room = *material.room;
	if (!Finite(room.minimum) || !Finite(room.maximum) || room.minimum.x >= room.maximum.x
	    || room.minimum.height >= room.maximum.height || room.minimum.z >= room.maximum.z || room.apertures.size() > TownMaxLightApertures)
		return Fail("Invalid GPU interior room");
	output.roomMinimum = { room.minimum.x, room.minimum.height, room.minimum.z, 1 };
	output.roomMaximum = { room.maximum.x, room.maximum.height, room.maximum.z, 0 };
	for (const TownLightAperture &aperture : room.apertures) {
		if (static_cast<unsigned>(aperture.plane) > 2 || !Finite(aperture.coordinate) || !Finite(aperture.minU)
		    || !Finite(aperture.maxU) || !Finite(aperture.minV) || !Finite(aperture.maxV)
		    || aperture.minU >= aperture.maxU || aperture.minV >= aperture.maxV
		    || (aperture.polygonSides != 0 && (aperture.polygonSides < 3 || aperture.polygonSides > 32)))
			return Fail("Invalid GPU interior aperture");
		const size_t index = static_cast<size_t>(output.parameters.z++);
		output.apertureBounds[index] = { aperture.minU, aperture.maxU, aperture.minV, aperture.maxV };
		output.aperturePlanes[index] = { static_cast<float>(aperture.plane), aperture.coordinate, static_cast<float>(aperture.polygonSides), 0 };
	}
	return true;
}

template <typename T>
bool Readback(Target &target, std::vector<T> &output)
{
	D3D11_MAPPED_SUBRESOURCE mapped {};
	const HRESULT result = Context->Map(target.staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(result))
		return Fail("Map GPU readback", result);
	for (int y = 0; y < Height; ++y)
		std::memcpy(output.data() + static_cast<size_t>(y) * Width,
		    static_cast<const uint8_t *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, static_cast<size_t>(Width) * sizeof(T));
	Context->Unmap(target.staging.get(), 0);
	return true;
}
#endif

} // namespace

bool TownGpuBeginFrame(int width, int height, bool allowWarpForDiagnostics, const TownGpuProjection &projection)
{
#ifdef _WIN32
	FrameActive = false;
	FrameFailed = false;
	Vertices.clear();
	Batches.clear();
	++FrameSerial;
	for (auto entry = TextureCache.begin(); entry != TextureCache.end();) {
		if (FrameSerial - entry->second.lastSeenFrame > 2) {
			TextureBytes -= entry->second.bytes;
			entry = TextureCache.erase(entry);
		} else {
			++entry;
		}
	}
	FrameShadow = {};
	FrameProjection = {};
	Status.frameSucceeded = false;
	Status.failure.clear();
	Status.submittedTriangles = 0;
	Status.drawCalls = 0;
	Status.frameMilliseconds = 0;
	Status.readbackMilliseconds = 0;
	FrameStart = Clock::now();
	if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || static_cast<uint64_t>(width) * height > MaxPixels)
		return Fail("Invalid GPU frame dimensions or pixel budget");
	if (!Finite(projection.nearClip) || !Finite(projection.farClip) || projection.nearClip <= 0
	    || projection.farClip <= projection.nearClip || projection.farClip > 4096)
		return Fail("Invalid GPU projection range (0 < near < far <= 4096 required)");
	if (Device && Status.warp && !allowWarpForDiagnostics)
		ResetTownGpuResources();
	if (!Device && !CreateDevice(allowWarpForDiagnostics)) {
		const TownGpuStatus failed = Status;
		ResetTownGpuResources();
		Status = failed;
		Status.available = false;
		return false;
	}
	if (!CreateTargets(width, height))
		return false;
	FrameProjection = projection;
	FrameActive = true;
	return true;
#else
	(void)width;
	(void)height;
	(void)allowWarpForDiagnostics;
	(void)projection;
	Status = {};
	Status.failure = "Direct3D11 town rendering is available only on Windows";
	return false;
#endif
}

bool TownGpuSetShadow(const TownGpuShadow &shadow)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed || !Batches.empty())
		return Fail("Shadow upload must precede GPU triangle submission");
	if (shadow.resolution == 0 && shadow.depth.empty()) {
		FrameShadow = {};
		return true;
	}
	if (shadow.resolution <= 0 || shadow.resolution > 2048
	    || shadow.depth.size() != static_cast<size_t>(shadow.resolution) * shadow.resolution
	    || !Finite(shadow.texelU) || shadow.texelU <= 0 || !Finite(shadow.texelV) || shadow.texelV <= 0
	    || !Finite(shadow.minU) || !Finite(shadow.minV) || shadow.pcfRadius < 0 || shadow.pcfRadius > 2
	    || !std::all_of(shadow.right.begin(), shadow.right.end(), [](float value) { return Finite(value); })
	    || !std::all_of(shadow.up.begin(), shadow.up.end(), [](float value) { return Finite(value); })
	    || !std::all_of(shadow.light.begin(), shadow.light.end(), [](float value) { return Finite(value); }))
		return Fail("Invalid GPU shadow map");
	if (!ShadowResource || ShadowKey != shadow.stableKey || ShadowRevision != shadow.revision || ShadowResolution != shadow.resolution) {
		if (!UploadTexture(shadow.resolution, shadow.resolution, DXGI_FORMAT_R32_FLOAT,
		        shadow.depth.data(), shadow.resolution * sizeof(float), ShadowResource))
			return false;
		ShadowKey = shadow.stableKey;
		ShadowRevision = shadow.revision;
		ShadowResolution = shadow.resolution;
	}
	FrameShadow = shadow;
	FrameShadow.depth = {};
	return true;
#else
	(void)shadow;
	return false;
#endif
}

bool TownGpuSubmitProjectedTriangle(const std::array<TownGpuVertex, 3> &vertices,
    const TownGpuTexture &texture, const TownGpuMaterial &material, uint32_t pickId)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed)
		return Fail("No valid GPU frame is recording");
	if (Status.submittedTriangles >= MaxTriangles)
		return Fail("GPU triangle budget exceeded");
	for (const TownGpuVertex &vertex : vertices) {
		if (!Finite(vertex.x) || !Finite(vertex.y) || !Finite(vertex.depth)
		    || vertex.depth < FrameProjection.nearClip || vertex.depth > FrameProjection.farClip
		    || !Finite(vertex.clipW) || vertex.clipW != (FrameProjection.perspective ? vertex.depth : 1)
		    || !Finite((vertex.x * 2 / Width - 1) * vertex.clipW) || !Finite((1 - vertex.y * 2 / Height) * vertex.clipW)
		    || !Finite(vertex.u) || !Finite(vertex.v)
		    || !std::all_of(vertex.world.begin(), vertex.world.end(), [](float value) { return Finite(value); }))
			return Fail("GPU projected vertices must be finite, depth-clipped and use clipW=depth (perspective) or 1 (orthographic)");
	}
	CachedTexture *uploaded = GetTexture(texture, material.lighting);
	if (uploaded == nullptr)
		return false;
	Constants constants {};
	if (!MakeConstants(material, *uploaded, constants))
		return false;
	const UINT first = static_cast<UINT>(Vertices.size());
	for (const TownGpuVertex &vertex : vertices)
		Vertices.push_back({ vertex, pickId,
		    { material.normal[0], material.normal[1], material.normal[2], std::clamp(material.diffuse, 0.0F, 1.0F) },
		    { material.shadowBias, material.shadowSlopeU, material.shadowSlopeV,
		        static_cast<float>(std::clamp(material.shade, 0, 3) + (material.receivesShadow ? 4 : 0)) },
		    { static_cast<float>(material.fallbackPaletteIndex), static_cast<float>(std::clamp(material.fallbackShade, 0, 3)) } });
	if (!Batches.empty() && Batches.back().texture == uploaded && Batches.back().preservePicking == material.preservePicking
	    && std::memcmp(&Batches.back().constants, &constants, sizeof(constants)) == 0)
		Batches.back().count += 3;
	else
		Batches.push_back({ uploaded, constants, first, 3, material.preservePicking });
	++Status.submittedTriangles;
	return true;
#else
	(void)vertices;
	(void)texture;
	(void)material;
	(void)pickId;
	return false;
#endif
}

bool TownGpuEndFrame(TownGpuFrame &output)
{
	output = {};
#ifdef _WIN32
	if (FrameFailed) {
		FrameActive = false;
		Status.frameSucceeded = false;
		return false;
	}
	if (!FrameActive) {
		FrameActive = false;
		return Fail("Cannot publish an incomplete GPU frame");
	}
	FrameActive = false;
	const float zero[] { 0, 0, 0, 0 };
	const float infinity[] { std::numeric_limits<float>::infinity(), 0, 0, 0 };
	Context->ClearRenderTargetView(Targets[0].view.get(), zero);
	Context->ClearRenderTargetView(Targets[1].view.get(), zero);
	Context->ClearRenderTargetView(Targets[2].view.get(), infinity);
	Context->ClearDepthStencilView(HardwareDepthView.get(), D3D11_CLEAR_DEPTH, 1, 0);
	ID3D11RenderTargetView *targets[] { Targets[0].view.get(), Targets[1].view.get(), Targets[2].view.get() };
	Context->OMSetRenderTargets(3, targets, HardwareDepthView.get());
	Context->OMSetDepthStencilState(DepthState.get(), 0);
	Context->RSSetState(Rasterizer.get());
	const D3D11_VIEWPORT viewport { 0, 0, static_cast<float>(Width), static_cast<float>(Height), 0, 1 };
	Context->RSSetViewports(1, &viewport);
	Context->IASetInputLayout(InputLayout.get());
	Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Context->VSSetShader(VertexShader.get(), nullptr, 0);
	Context->PSSetShader(PixelShader.get(), nullptr, 0);
	ID3D11Buffer *constantBuffer = ConstantBuffer.get();
	Context->VSSetConstantBuffers(0, 1, &constantBuffer);
	Context->PSSetConstantBuffers(0, 1, &constantBuffer);
	if (!Vertices.empty()) {
		if (VertexCapacity < Vertices.size()) {
			const size_t capacity = std::min<size_t>(MaxTriangles * 3, std::max(Vertices.size(), VertexCapacity * 2));
			D3D11_BUFFER_DESC description {};
			description.ByteWidth = static_cast<UINT>(capacity * sizeof(GpuVertex));
			description.Usage = D3D11_USAGE_DYNAMIC;
			description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			const HRESULT result = Device->CreateBuffer(&description, nullptr, VertexBuffer.put());
			if (FAILED(result))
				return Fail("Create frame vertex buffer", result);
			VertexCapacity = capacity;
		}
		D3D11_MAPPED_SUBRESOURCE mapped {};
		HRESULT result = Context->Map(VertexBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(result))
			return Fail("Map frame vertex buffer", result);
		std::memcpy(mapped.pData, Vertices.data(), Vertices.size() * sizeof(GpuVertex));
		Context->Unmap(VertexBuffer.get(), 0);
		ID3D11Buffer *vertexBuffer = VertexBuffer.get();
		const UINT stride = sizeof(GpuVertex);
		const UINT offset = 0;
		Context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
		for (const Batch &batch : Batches) {
			result = Context->Map(ConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			if (FAILED(result))
				return Fail("Map batch constants", result);
			std::memcpy(mapped.pData, &batch.constants, sizeof(Constants));
			Context->Unmap(ConstantBuffer.get(), 0);
			ID3D11ShaderResourceView *resources[] { batch.texture->codes.get(), batch.texture->opacity.get(), batch.texture->lut.get(), ShadowResource.get() };
			Context->PSSetShaderResources(0, 4, resources);
			Context->OMSetBlendState(BlendStates[batch.preservePicking ? 1 : 0].get(), nullptr, 0xFFFFFFFFU);
			Context->Draw(batch.count, batch.first);
			++Status.drawCalls;
		}
	}
	ID3D11ShaderResourceView *emptyResources[4] {};
	Context->PSSetShaderResources(0, 4, emptyResources);
	Context->OMSetRenderTargets(0, nullptr, nullptr);
	for (Target &target : Targets)
		Context->CopyResource(target.staging.get(), target.texture.get());
	const auto readbackStart = Clock::now();
	TownGpuFrame complete;
	complete.width = Width;
	complete.height = Height;
	const size_t count = static_cast<size_t>(Width) * Height;
	complete.indexed.resize(count);
	complete.pickIds.resize(count);
	complete.depth.resize(count);
	if (!Readback(Targets[0], complete.indexed) || !Readback(Targets[1], complete.pickIds) || !Readback(Targets[2], complete.depth))
		return false;
	const HRESULT removed = Device->GetDeviceRemovedReason();
	if (FAILED(removed))
		return Fail("D3D11 device removed", removed);
	Status.readbackMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - readbackStart).count();
	Status.frameMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - FrameStart).count();
	Status.cachedTextures = TextureCache.size();
	Status.frameSucceeded = true;
	output = std::move(complete);
	return true;
#else
	return false;
#endif
}

void ResetTownGpuResources()
{
#ifdef _WIN32
	FrameActive = false;
	FrameFailed = false;
	Batches.clear();
	Vertices.clear();
	FrameShadow = {};
	FrameProjection = {};
	if (Context) {
		Context->ClearState();
		Context->Flush();
	}
	TextureCache.clear();
	TextureBytes = 0;
	ShadowResource.reset();
	ShadowKey = ShadowRevision = 0;
	ShadowResolution = 0;
	HardwareDepthView.reset();
	HardwareDepth.reset();
	for (Target &target : Targets) {
		target.view.reset();
		target.texture.reset();
		target.staging.reset();
	}
	for (auto &blend : BlendStates)
		blend.reset();
	DepthState.reset();
	Rasterizer.reset();
	ConstantBuffer.reset();
	VertexBuffer.reset();
	InputLayout.reset();
	PixelShader.reset();
	VertexShader.reset();
	Context.reset();
	Device.reset();
	Width = Height = 0;
	VertexCapacity = 0;
	FrameSerial = 0;
#endif
	Status = {};
}

const TownGpuStatus &GetTownGpuStatus()
{
	return Status;
}

} // namespace devilution
