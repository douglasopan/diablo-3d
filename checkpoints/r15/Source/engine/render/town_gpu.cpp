#include "engine/render/startup_loading.hpp"
#include "engine/render/town_gpu.hpp"
#include "engine/render/town_geometric_point_shadow.hpp"
#include "engine/render/d3d_local_diagnostics.hpp"
#include "engine/render/town_gpu_mesh.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
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
namespace diag = d3d_local_diagnostics;
namespace {

TownGpuStatus Status;
TownGpuFireVolumeStats FireVolumeStats;
TownGpuWorldFrameTiming WorldFrameTiming;
TownGpuFireVolumeFault FireVolumeFault = TownGpuFireVolumeFault::None;
void (*BeforeReadbackForDiagnostics)() = nullptr;

#ifdef _WIN32
using Clock = std::chrono::steady_clock;
constexpr size_t MaxPixels = 4 * 1024 * 1024;
constexpr size_t MaxTextureBytes = 256 * 1024 * 1024;
// Only the CPU-expanded, per-frame vertex stream consumes this capacity.
// Reused resident index ranges have their own immutable-cache byte budget.
constexpr size_t MaxProjectedTriangles = 1024 * 1024;
constexpr size_t MaxAuthoredColorBytes = MaxProjectedTriangles * 3 * sizeof(std::array<float, 4>);
constexpr size_t MaxPaletteBlendTriangles = 2048;
constexpr size_t MaxPaletteBlendCopyBytes = 512 * 1024 * 1024;
// CPU bytecode only: at most the two fixed embedded shader variants. Device
// resets must still release every GPU resource; these blobs own no device.
constexpr size_t MaxShaderBytecodeBytes = 8 * 1024 * 1024;
constexpr UINT ShaderCompileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
constexpr const char *ShaderPointLightMacro = "TOWN_MAX_POINT_LIGHTS";

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
constexpr uint32_t SpritePointLightFlag = 1024;
constexpr uint32_t FramePointVisibilityFlag = 2048;
constexpr uint32_t ViewModelFlag = 4096;
constexpr uint32_t InteriorRgbLightFlag = 8192;
constexpr uint32_t FirePointLightFlag = 16384;
constexpr uint32_t TownPointGeometryFlag = 65536;
constexpr uint32_t TownPointDiffuseFlag = 131072;
constexpr uint32_t GroundWorldUvFlag = 32768;
constexpr uint32_t AbsoluteDisplayRgbFlag = 262144;

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
	Float4 groundUv; // scale, world offset X/Z, reserved
	std::array<Float4, TownMaxPointLights> lightPositionRadius;
	std::array<Float4, TownMaxPointLights> lightRedIntensity;
	std::array<Float4, TownMaxLightApertures> apertureBounds;
	std::array<Float4, TownMaxLightApertures> aperturePlanes;
	std::array<Float4, TownMaxLightOccluders - 1> blockerMinimum;
	std::array<Float4, TownMaxLightOccluders - 1> blockerMaximum;
};
static_assert(sizeof(Constants) % 16 == 0);
static_assert(sizeof(Constants) <= D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16);

/** One bounded frame field, not duplicated in each recorded triangle batch.
 * All apertures are copied; no source spans or scene pointers survive Submit. */
struct SpritePointConstants {
	std::array<Float4, TownMaxPointLights> positionRadius;
	std::array<Float4, TownMaxPointLights> colorIntensity;
	std::array<Float4, TownMaxLightOccluders> occluderMinimum;
	std::array<Float4, TownMaxLightOccluders> occluderMaximum;
	std::array<Float4, TownMaxLightOccluders * TownMaxLightApertures> apertureBounds;
	std::array<Float4, TownMaxLightOccluders * TownMaxLightApertures> aperturePlanes;
	UInt4 counts;
};
static_assert(sizeof(SpritePointConstants) == sizeof(Float4)
    * (2 * TownMaxPointLights + 2 * TownMaxLightOccluders
        + 2 * TownMaxLightOccluders * TownMaxLightApertures + 1));
static_assert(sizeof(SpritePointConstants) <= D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16);

struct FirePointConstants {
	std::array<Float4, TownMaxFireShadowLights> positionRadius;
	std::array<Float4, TownMaxFireShadowLights> colorIntensity;
	UInt4 counts;
};
static_assert(sizeof(FirePointConstants) == sizeof(Float4) * (2 * TownMaxFireShadowLights + 1));
static_assert(TownMaxFireShadowLights == 4 && TownFireShadowResolution == 64);

// Keep legacy projected uploads byte-for-byte 80 B. Optional COLOR0 lives in
// a parallel SRV, not in every projected/resident vertex or immutable mesh.
struct TownPointGeometryConstants {
	std::array<Float4, TownMaxPointLights> positionRadius;
	std::array<Float4, TownMaxPointLights> colorIntensity;
	UInt4 counts;
};
static_assert(TownMaxPointLights == 32 && sizeof(TownPointGeometryConstants) == 1040);

struct ProjectedVertex {
	float x, y, depth, u, v;
	std::array<float, 3> world;
	float clipW;
};
struct GpuVertex {
	ProjectedVertex vertex;
	uint32_t pickId;
	Float4 normalDiffuse;
	Float4 shadowParameters;
	std::array<float, 2> fallback;
};
static_assert(sizeof(ProjectedVertex) == 36);
static_assert(offsetof(TownGpuVertex, authoredColor) == 36);
static_assert(sizeof(GpuVertex) == 80);

struct CachedTexels {
	ComOwner<ID3D11ShaderResourceView> codes;
	ComOwner<ID3D11ShaderResourceView> opacity;
	ComOwner<ID3D11ShaderResourceView> unlit;
	int width;
	int height;
	uint32_t maximumCode;
	bool hasOpacity;
	size_t bytes;
	uint64_t lastSeenFrame;
	uint64_t codesFingerprint, opacityFingerprint, unlitFingerprint;
};

struct CachedOpacity {
	ComOwner<ID3D11ShaderResourceView> view;
	int width, height;
	size_t bytes;
	uint64_t lastSeenFrame, fingerprint;
};

struct CachedLightLut {
	ComOwner<ID3D11ShaderResourceView> view;
	ComOwner<ID3D11ShaderResourceView> rgbaView;
	unsigned lightLevels;
	unsigned codeCount;
	unsigned lutWidth;
	size_t bytes;
	uint64_t lastSeenFrame;
	uint64_t indexedFingerprint, rgbaFingerprint;
	bool hasRgba;
};

/** Frame-local binding of independent immutable resources. Both resources are
 * pinned until BeginFrame, so recorded batches retain their exact LUT revision. */
struct CachedTexture {
	CachedTexels *texels;
	CachedLightLut *lut;
	unsigned lightLevels;
	unsigned codeCount;
	unsigned lutWidth;
	bool hasOpacity;
	CachedOpacity *coverage;
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

struct LightLutKey {
	TextureKey identity;
	bool shared;
	bool operator==(const LightLutKey &other) const { return identity == other.identity && shared == other.shared; }
};

struct LightLutKeyHash {
	size_t operator()(const LightLutKey &value) const
	{
		return TextureKeyHash {}(value.identity) ^ (value.shared ? size_t { 0x85EBCA6B } : 0);
	}
};

struct TextureBindingKey {
	TextureKey texels;
	LightLutKey lut;
	unsigned lightLevels;
	bool hasLut;
	TextureKey coverage;
	bool operator==(const TextureBindingKey &other) const { return texels == other.texels && lut == other.lut && lightLevels == other.lightLevels && hasLut == other.hasLut && coverage == other.coverage; }
};

struct TextureBindingKeyHash {
	size_t operator()(const TextureBindingKey &value) const
	{
		return TextureKeyHash {}(value.texels) ^ (LightLutKeyHash {}(value.lut) << 1) ^ value.lightLevels ^ (value.hasLut ? 0xC2B2AE35U : 0) ^ (TextureKeyHash {}(value.coverage) << 2);
	}
};

struct Batch {
	CachedTexture *texture;
	Constants constants;
	UINT first;
	UINT count;
	bool preservePicking;
	bool paletteBlend;
	bool viewModel;
	D3D11_BOX paletteCopyBox;
};

struct Target {
	ComOwner<ID3D11Texture2D> texture;
	ComOwner<ID3D11RenderTargetView> view;
	ComOwner<ID3D11Texture2D> staging;
};

ComOwner<ID3D11Device> Device;
ComOwner<ID3D11DeviceContext> Context;
ComOwner<ID3D11Query> WorldTimingDisjointQuery, WorldTimingBeginQuery, WorldTimingEndQuery;
bool WorldTimingUnavailable = false, WorldTimingOpen = false, WorldTimingSubmitted = false;
uint64_t WorldTimingSerial = 0; // Process lifetime: device resets must not cause ABA.

void ResetWorldTimingQueries()
{
	if (WorldTimingOpen && Context) {
		Context->End(WorldTimingEndQuery.get());
		Context->End(WorldTimingDisjointQuery.get());
	}
	WorldTimingOpen = WorldTimingSubmitted = false;
	WorldTimingDisjointQuery.reset();
	WorldTimingBeginQuery.reset();
	WorldTimingEndQuery.reset();
}

void BeginWorldTiming()
{
	if (!Context || !Device || WorldTimingUnavailable || WorldFrameTiming.frameSerial == 0)
		return;
	if (!WorldTimingDisjointQuery || !WorldTimingBeginQuery || !WorldTimingEndQuery) {
		D3D11_QUERY_DESC description { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
		HRESULT result = Device->CreateQuery(&description, WorldTimingDisjointQuery.put());
		description.Query = D3D11_QUERY_TIMESTAMP;
		if (SUCCEEDED(result)) result = Device->CreateQuery(&description, WorldTimingBeginQuery.put());
		if (SUCCEEDED(result)) result = Device->CreateQuery(&description, WorldTimingEndQuery.put());
		if (FAILED(result)) {
			ResetWorldTimingQueries();
			WorldTimingUnavailable = true; // Optional metrics never refuse a valid render.
			return;
		}
	}
	Context->Begin(WorldTimingDisjointQuery.get());
	Context->End(WorldTimingBeginQuery.get());
	WorldTimingOpen = true;
}

void EndWorldTiming()
{
	if (!WorldTimingOpen) return;
	Context->End(WorldTimingEndQuery.get());
	Context->End(WorldTimingDisjointQuery.get());
	WorldTimingOpen = false;
	WorldTimingSubmitted = true;
}

void PublishWorldTiming()
{
	if (!WorldTimingSubmitted) return;
	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint {};
	UINT64 begin = 0, end = 0;
	// The frame has already completed its existing MRT readbacks. Never flush,
	// poll, spin or wait separately for optional timing data.
	const HRESULT frequencyResult = Context->GetData(WorldTimingDisjointQuery.get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
	const HRESULT beginResult = Context->GetData(WorldTimingBeginQuery.get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
	const HRESULT endResult = Context->GetData(WorldTimingEndQuery.get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
	WorldTimingSubmitted = false;
	if (frequencyResult == S_OK && beginResult == S_OK && endResult == S_OK
	    && !disjoint.Disjoint && disjoint.Frequency != 0 && end >= begin) {
		const double elapsed = static_cast<double>(end - begin) * 1000.0 / static_cast<double>(disjoint.Frequency);
		if (std::isfinite(elapsed) && elapsed >= 0) {
			WorldFrameTiming.known = true;
			WorldFrameTiming.gpuMilliseconds = elapsed;
			return;
		}
	}
	// Do not reuse an unresolved/disjoint query as though it belonged to the
	// next frame. A fresh optional set may be created next time.
	ResetWorldTimingQueries();
}

class WorldTimingScope {
	bool completed = false;
public:
	WorldTimingScope() = default;
	~WorldTimingScope()
	{
		if (!completed) {
			ResetWorldTimingQueries();
			WorldFrameTiming.known = false;
			WorldFrameTiming.gpuMilliseconds = -1;
		}
	}
	void StartCommands() { BeginWorldTiming(); }
	void FinishCommands() { EndWorldTiming(); }
	void Publish() { PublishWorldTiming(); completed = true; }
};


ComOwner<ID3D11VertexShader> VertexShader;
ComOwner<ID3D11PixelShader> PixelShader;
ComOwner<ID3D11InputLayout> InputLayout;
ComOwner<ID3D11Buffer> VertexBuffer;
ComOwner<ID3D11Buffer> AuthoredColorBuffer;
ComOwner<ID3D11ShaderResourceView> AuthoredColorResource;
ComOwner<ID3D11Buffer> ConstantBuffer;
ComOwner<ID3D11Buffer> SpritePointConstantBuffer;
ComOwner<ID3D11Buffer> FirePointConstantBuffer;
ComOwner<ID3D11RasterizerState> Rasterizer;
ComOwner<ID3D11DepthStencilState> DepthState;
ComOwner<ID3D11DepthStencilState> PaletteBlendDepthState;
ComOwner<ID3D11BlendState> PaletteBlendWriteState;
std::array<ComOwner<ID3D11BlendState>, 2> BlendStates;
std::array<Target, 5> Targets;
ComOwner<ID3D11ShaderResourceView> RgbPaletteResource, NativeLightResource;
std::vector<uint32_t> CachedRgbPalette;
std::vector<float> CachedNativeLight;
bool FrameSmoothLighting = false;
bool FrameRgbaLut = false;
bool FrameSpritePointLighting = false;
bool FrameFirePointLighting = false;
bool FrameTownPointGeometryLighting = false, FrameTownPointReceiverSubmitted = false;
// Publication serial survives Reset; old host outputs must never match a new frame.
uint64_t TownPointPublicationSerial = 0;
ComOwner<ID3D11Buffer> TownPointGeometryConstantBuffer;
ComOwner<ID3D11ShaderResourceView> TownPointGeometryShadowResource;
uint64_t TownPointGeometryKey = 0, TownPointGeometryRevision = 0, TownPointGeometryEpoch = 0, TownPointGeometryFingerprint = 0;
size_t TownPointGeometrySourceCount = 0;
std::array<Float4, TownMaxPointLights> TownPointGeometrySourcePositions {};
ComOwner<ID3D11ShaderResourceView> FireShadowResource;
uint64_t FireShadowKey = 0, FireShadowRevision = 0, FireShadowFingerprint = 0;
int FireShadowResolution = 0;
size_t FireShadowLightCount = 0;
std::array<Float4, TownMaxFireShadowLights> FireShadowSourcePositions {};
int CachedNativeLightWidth=0,CachedNativeLightHeight=0;
ComOwner<ID3D11Texture2D> HardwareDepth;
ComOwner<ID3D11DepthStencilView> HardwareDepthView;
ComOwner<ID3D11ShaderResourceView> ShadowResource;
ComOwner<ID3D11Texture2D> PriorIndexedColor;
ComOwner<ID3D11ShaderResourceView> PriorIndexedColorView;
ComOwner<ID3D11ShaderResourceView> PaletteBlendResource;
uint64_t PaletteBlendKey = 0;
uint64_t PaletteBlendRevision = 0;
bool FramePaletteBlendReady = false;
bool FramePaletteOverlayStarted = false;
bool FrameViewModelStarted = false;
std::unordered_map<TextureKey, CachedTexels, TextureKeyHash> TexelCache;
std::unordered_map<TextureKey, CachedOpacity, TextureKeyHash> OpacityCache;
std::unordered_map<LightLutKey, CachedLightLut, LightLutKeyHash> LightLutCache;
std::unordered_map<TextureBindingKey, CachedTexture, TextureBindingKeyHash> TextureCache;
size_t TextureBytes = 0;
size_t VertexCapacity = 0;
size_t AuthoredColorCapacity = 0;
uint64_t FrameSerial = 0;
// Separate from the resettable cache TTL serial: successful recording tokens
// never repeat after a backend reset while a caller still owns warm markers.
uint64_t TexturePreparationFrame = 0;
std::vector<GpuVertex> Vertices;
std::vector<std::array<float, 4>> AuthoredColors;
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
void ResetFireVolumeTargetViews();

void BindTownPointGeometryResources();

void ClearTownPointGeometryFrameBinding()
{
	FrameTownPointGeometryLighting = FrameTownPointReceiverSubmitted = false;
	Status.townPointFrameSerial = 0;
	Status.townPointLightCount = Status.townPointShadowBytes = 0;
	if (Context) {
		ID3D11Buffer *buffer = nullptr;
		ID3D11ShaderResourceView *resource = nullptr;
		Context->PSSetConstantBuffers(4, 1, &buffer);
		Context->PSSetShaderResources(12, 1, &resource);
	}
}

void ClearFirePointFrameBinding()
{
	FrameFirePointLighting = false;
	Status.fireLightCount = 0;
	if (Context) {
		ID3D11Buffer *buffer = nullptr;
		ID3D11ShaderResourceView *resource = nullptr;
		Context->PSSetConstantBuffers(3, 1, &buffer);
		Context->PSSetShaderResources(11, 1, &resource);
	}
}

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
 float4 groundUv;
 float4 lightPositionRadius[TOWN_MAX_POINT_LIGHTS];
 float4 lightRedIntensity[TOWN_MAX_POINT_LIGHTS];
 float4 apertureBounds[8];
 float4 aperturePlanes[8];
 float4 blockerMinimum[31];
 float4 blockerMaximum[31];
};
// Kept separate from per-draw b0 so a shared optional field does not enlarge
// every projected/resident batch. Dimensions come from the C++ contract macros.
cbuffer SpritePointField : register(b2) {
 float4 spritePointPositionRadius[TOWN_MAX_POINT_LIGHTS];
 float4 spritePointColorIntensity[TOWN_MAX_POINT_LIGHTS];
 float4 spritePointOccluderMinimum[32];
 float4 spritePointOccluderMaximum[32];
 float4 spritePointApertureBounds[256];
 float4 spritePointAperturePlanes[256];
 uint4 spritePointCounts;
};
cbuffer FirePointField : register(b3) {
 float4 firePointPositionRadius[4];
 float4 firePointColorIntensity[4];
 uint4 firePointCounts;
};
Texture2D<uint> codes : register(t0);
Texture2D<uint> opacity : register(t1);
Texture2D<uint> lightLut : register(t2);
Texture2D<float> shadowDepth : register(t3);
Texture2D<uint> priorIndexedColor : register(t4);
Texture2D<uint> paletteBlendLut : register(t5);
Texture2D<uint> rgbPalette : register(t6);
Texture2D<float> nativeLightGrid : register(t7);
Texture2D<uint> unlitCodes : register(t8);
Texture2D<uint> rgbaLightLut : register(t9);
Buffer<float4> authoredColors : register(t10);
Texture2D<float> fireShadowDepth : register(t11);
float4 unpackRgba(uint p) { return float4((p>>16)&255,(p>>8)&255,p&255,p>>24)/255.0; }
float3 paletteRgb(uint code) {
 uint p = rgbPalette.Load(int3(min(code,255),0,0));
 return float3((p>>16)&255,(p>>8)&255,p&255)/255.0;
}
float nativeLightAmount(float3 world, float fallback) {
 uint w,h; nativeLightGrid.GetDimensions(w,h);
 if(w==0 || h==0) return fallback;
 float2 p=clamp(world.xz,float2(0,0),float2(w-1,h-1));
 int2 a=(int2)floor(p), b=min(a+1,int2(w-1,h-1));
 float2 t=frac(p);
 return lerp(lerp(nativeLightGrid.Load(int3(a,0)),nativeLightGrid.Load(int3(b.x,a.y,0)),t.x),
  lerp(nativeLightGrid.Load(int3(a.x,b.y,0)),nativeLightGrid.Load(int3(b,0)),t.x),t.y);
}
float3 litRgb(uint code,float level) {
 float l=clamp(level,0.0,(float)(flags.y-1));
 uint low=(uint)floor(l), high=min(low+1,flags.y-1), width=max(1,(uint)shadowScale.z);
 uint start=min(code,flags.z-1)*flags.y;
 uint a=start+low,b=start+high;
 return lerp(paletteRgb(lightLut.Load(int3(a%width,a/width,0))),paletteRgb(lightLut.Load(int3(b%width,b/width,0))),frac(l));
}
float4 litRgba(uint code,float level) {
 float l=clamp(level,0.0,(float)(flags.y-1));
 uint low=(uint)floor(l), high=min(low+1,flags.y-1), width=max(1,(uint)shadowScale.z);
 uint start=min(code,flags.z-1)*flags.y, a=start+low,b=start+high;
 return lerp(unpackRgba(rgbaLightLut.Load(int3(a%width,a/width,0))),unpackRgba(rgbaLightLut.Load(int3(b%width,b/width,0))),frac(l));
}
float3 srgbToLinear(float3 value) {
 return float3(value.x<=0.04045?value.x/12.92:pow((value.x+0.055)/1.055,2.4),
  value.y<=0.04045?value.y/12.92:pow((value.y+0.055)/1.055,2.4),value.z<=0.04045?value.z/12.92:pow((value.z+0.055)/1.055,2.4));
}
float3 linearToSrgb(float3 value) {
 return float3(value.x<=0.0031308?value.x*12.92:1.055*pow(value.x,1.0/2.4)-0.055,
  value.y<=0.0031308?value.y*12.92:1.055*pow(value.y,1.0/2.4)-0.055,value.z<=0.0031308?value.z*12.92:1.055*pow(value.z,1.0/2.4)-0.055);
}
uint packRgb(float3 color) {
 uint3 c=(uint3)(saturate(color)*255.0+0.5);
 return 0xFF000000 | (c.r<<16) | (c.g<<8) | c.b;
}
// Axial depth and face axes exactly match town_point_shadow.hpp. A cubemap
// face is a 64-row slice in a bounded vertical atlas; PCF never crosses faces.
float firePointVisibility(uint lightIndex, float3 delta, float diffuse) {
 if (firePointCounts.y == 0) return 1;
 if (dot(delta, delta) <= 0.0004) return 1;
 float3 magnitude = abs(delta);
 uint face;
 float axial;
 float2 uv;
 if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
  axial = magnitude.x;
  if (delta.x >= 0) { face = 0; uv = float2(-delta.z, -delta.y) / axial; }
  else { face = 1; uv = float2(delta.z, -delta.y) / axial; }
 } else if (magnitude.y >= magnitude.z) {
  axial = magnitude.y;
  if (delta.y >= 0) { face = 2; uv = float2(delta.x, delta.z) / axial; }
  else { face = 3; uv = float2(delta.x, -delta.z) / axial; }
 } else {
  axial = magnitude.z;
  if (delta.z >= 0) { face = 4; uv = float2(delta.x, -delta.y) / axial; }
  else { face = 5; uv = float2(-delta.x, -delta.y) / axial; }
 }
 int resolution = (int)firePointCounts.z;
 int2 center = (int2)floor((uv * 0.5 + 0.5) * resolution);
 float bias = 0.02 + 0.025 * (1 - diffuse);
 float visible = 0;
 [unroll] for (int y = -1; y <= 1; ++y) {
  [unroll] for (int x = -1; x <= 1; ++x) {
   int2 sampleTexel = clamp(center + int2(x, y), int2(0, 0), int2(resolution - 1, resolution - 1));
   sampleTexel.y += (int)(lightIndex * 6 + face) * resolution;
   float stored = fireShadowDepth.Load(int3(sampleTexel, 0));
   visible += axial <= stored + bias ? 1 : 0;
  }
 }
 return visible / 9;
}
float3 firePointIrradiance(float3 world, float3 sourceNormal) {
 float normalLengthSquared = dot(sourceNormal, sourceNormal);
 if (!isfinite(normalLengthSquared) || normalLengthSquared <= 0.000000000001) return float3(0, 0, 0);
 float3 n = sourceNormal / sqrt(normalLengthSquared);
 float3 irradiance = float3(0, 0, 0);
 [loop] for (uint i = 0; i < min(firePointCounts.x, 4u); ++i) {
  float3 delta = world - firePointPositionRadius[i].xyz;
  float distanceSquared = dot(delta, delta);
  float radiusSquared = firePointPositionRadius[i].w * firePointPositionRadius[i].w;
  if (distanceSquared <= 0.00000001 || distanceSquared >= radiusSquared || firePointColorIntensity[i].w == 0) continue;
  float diffuse = max(0, dot(n, -delta / sqrt(distanceSquared)));
  if (diffuse <= 0) continue;
  float cutoff = max(0, 1 - distanceSquared / radiusSquared);
  float amount = firePointColorIntensity[i].w * cutoff * cutoff * diffuse
   * firePointVisibility(i, delta, diffuse) / (1 + distanceSquared);
  irradiance += firePointColorIntensity[i].rgb * amount;
 }
 return irradiance;
}
cbuffer TownPointGeometryField : register(b4) {
 float4 townPointPositionRadius[TOWN_MAX_POINT_LIGHTS];
 float4 townPointColorIntensity[TOWN_MAX_POINT_LIGHTS];
 uint4 townPointGeometryCounts;
};
Texture2D<float> townPointGeometryDepth : register(t12);
float townPointGeometryVisibility(uint lightIndex, float3 delta, float diffuse) {
 if (townPointGeometryCounts.y == 0) return 1;
 if (dot(delta, delta) <= 0.0004) return 1;
 float3 magnitude = abs(delta);
 uint face;
 float axial;
 float2 uv;
 if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
  axial = magnitude.x;
  if (delta.x >= 0) { face = 0; uv = float2(-delta.z, -delta.y) / axial; }
  else { face = 1; uv = float2(delta.z, -delta.y) / axial; }
 } else if (magnitude.y >= magnitude.z) {
  axial = magnitude.y;
  if (delta.y >= 0) { face = 2; uv = float2(delta.x, delta.z) / axial; }
  else { face = 3; uv = float2(delta.x, -delta.z) / axial; }
 } else {
  axial = magnitude.z;
  if (delta.z >= 0) { face = 4; uv = float2(delta.x, -delta.y) / axial; }
  else { face = 5; uv = float2(-delta.x, -delta.y) / axial; }
 }
 int resolution = (int)townPointGeometryCounts.z;
 int2 center = (int2)floor((uv * 0.5 + 0.5) * resolution);
 float bias = 0.02 + 0.025 * (1 - diffuse);
 float visible = 0;
 [unroll] for (int y = -1; y <= 1; ++y) {
  [unroll] for (int x = -1; x <= 1; ++x) {
   int2 sampleTexel = clamp(center + int2(x, y), int2(0, 0), int2(resolution - 1, resolution - 1));
   sampleTexel.y += (int)(lightIndex * 6 + face) * resolution;
   float stored = townPointGeometryDepth.Load(int3(sampleTexel, 0));
   visible += axial <= stored + bias ? 1 : 0;
  }
 }
 return visible / 9;
}
float3 townPointGeometryIrradiance(float3 world, float3 sourceNormal, bool diffuseSurface) {
 if (!all(isfinite(world)) || any(abs(world)>4096)) return 0;
 float3 n = 0;
 if (diffuseSurface) {
  if (!all(isfinite(sourceNormal)) || any(abs(sourceNormal)>1000000)) return 0;
  float squared = dot(sourceNormal,sourceNormal);
  if (!isfinite(squared) || squared <= 0.00000001) return 0;
  n = sourceNormal / sqrt(squared);
 }
 float3 irradiance = 0;
 [loop] for (uint i=0; i<min(townPointGeometryCounts.x,32u); ++i) {
  float3 delta = world-townPointPositionRadius[i].xyz;
  float distanceSquared=dot(delta,delta);
  float radiusSquared=townPointPositionRadius[i].w*townPointPositionRadius[i].w;
  if (distanceSquared>=radiusSquared || townPointColorIntensity[i].w==0) continue;
  float diffuse=1;
  if (diffuseSurface) {
   if (distanceSquared<=0.00000001) continue;
   diffuse=max(0,dot(n,-delta/sqrt(distanceSquared)));
   if (diffuse<=0) continue;
  }
  float cutoff=1-distanceSquared/radiusSquared;
  float amount=townPointColorIntensity[i].w*cutoff*cutoff*diffuse
   *townPointGeometryVisibility(i,delta,diffuse)/(1+distanceSquared);
  irradiance+=townPointColorIntensity[i].rgb*amount;
 }
 return irradiance;
}
struct Input {
 uint vertexId : SV_VertexID;
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
 nointerpolation float2 volume : TEXCOORD7;
 float4 authoredColor : TEXCOORD8;
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
 output.volume = float2(-1, 1);
 output.authoredColor = (flags.w&512)!=0 ? authoredColors[input.vertexId+(uint)target.z] : float4(1,1,1,1);
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
float smoothDirectionalShadow(float3 world,float4 receiver) {
 if((flags.w&8)==0 || parameters.w<1 || (((uint)receiver.w&4)==0))return 0;
 float2 uv=float2((dot(world,shadowRight.xyz)-shadowRight.w)/shadowLight.w,(dot(world,shadowUp.xyz)-shadowUp.w)/shadowScale.x);
 if(any(uv<0) || any(uv>=parameters.w))return 0;
 float2 t=frac(uv);
 float3 base=world-shadowRight.xyz*t.x*shadowLight.w-shadowUp.xyz*t.y*shadowScale.x
  -shadowLight.xyz*(receiver.y*t.x*shadowLight.w+receiver.z*t.y*shadowScale.x);
 float3 du=shadowRight.xyz*shadowLight.w+shadowLight.xyz*receiver.y*shadowLight.w;
 float3 dv=shadowUp.xyz*shadowScale.x+shadowLight.xyz*receiver.z*shadowScale.x;
 return lerp(lerp(directionalShadow(base,receiver),directionalShadow(base+du,receiver),t.x),
  lerp(directionalShadow(base+dv,receiver),directionalShadow(base+du+dv,receiver),t.x),t.y);
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
bool spritePointVisibleThrough(float3 origin, float3 receiver);
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
  if (diffuse <= 0) continue;
  bool visible;
  [branch] if ((flags.w&2048)!=0) visible = spritePointVisibleThrough(light.xyz, world);
  else visible = visibleThrough(light.xyz, world);
  if (!visible) continue;
  float range = 1 - distanceSquared / radiusSquared;
  red += lightRedIntensity[i].x * lightRedIntensity[i].y * diffuse * range * range / (1 + distanceSquared);
 }
 return saturate(red / normalAndRed.w / parameters.x);
}
// The legacy red-only path above stays byte-for-byte intact. This opt-in path
// carries each emitter's RGB, with the same diffuse/range/shell semantics.
float3 interiorPointIrradiance(float3 world, float3 authoredNormal) {
 float3 irradiance = 0;
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
  if (diffuse <= 0) continue;
  bool visible;
  [branch] if ((flags.w&2048)!=0) visible = spritePointVisibleThrough(light.xyz, world);
  else visible = visibleThrough(light.xyz, world);
  if (!visible) continue;
  float range = 1 - distanceSquared / radiusSquared;
  irradiance += lightRedIntensity[i].xzw * lightRedIntensity[i].y
   * diffuse * range * range / (1 + distanceSquared);
 }
 return irradiance;
}
bool spritePointOpeningAt(float3 hit, int axis, float boundary, uint begin, uint count) {
 float2 uv = float2(axis == 0 ? hit.z : hit.x, axis == 1 ? hit.z : hit.y);
 [loop] for (uint i = begin; i < begin + count; ++i) {
  float4 plane = spritePointAperturePlanes[i];
  float4 bounds = spritePointApertureBounds[i];
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
bool spritePointCrossingOpen(float3 origin, float3 delta, float t, float4 minimum, float4 maximum) {
 if (t <= 0.00001 || t >= 0.99999) return true;
 float3 hit = origin + delta * t;
 [loop] for (int axis = 0; axis < 3; ++axis) {
  if (abs(delta[axis]) <= 0.000001) continue;
  if (abs(hit[axis] - minimum[axis]) <= 0.0001
   && !spritePointOpeningAt(hit, axis, minimum[axis], (uint)minimum.w, (uint)maximum.w)) return false;
  if (abs(hit[axis] - maximum[axis]) <= 0.0001
   && !spritePointOpeningAt(hit, axis, maximum[axis], (uint)minimum.w, (uint)maximum.w)) return false;
 }
 return true;
}
bool spritePointVisibleThrough(float3 origin, float3 receiver) {
 float3 delta = receiver - origin;
 [loop] for (uint i = 0; i < spritePointCounts.y; ++i) {
  float4 minimum = spritePointOccluderMinimum[i];
  float4 maximum = spritePointOccluderMaximum[i];
  float entry = 0;
  float exitPoint = 1;
  bool misses = false;
  [unroll] for (int axis = 0; axis < 3; ++axis) {
   if (abs(delta[axis]) <= 0.000001) {
    if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) misses = true;
   } else {
    float first = (minimum[axis] - origin[axis]) / delta[axis];
    float second = (maximum[axis] - origin[axis]) / delta[axis];
    entry = max(entry, min(first, second));
    exitPoint = min(exitPoint, max(first, second));
   }
  }
  if (misses || entry > exitPoint) continue;
  if (!spritePointCrossingOpen(origin, delta, entry, minimum, maximum)
   || !spritePointCrossingOpen(origin, delta, exitPoint, minimum, maximum)) return false;
 }
 return true;
}
float3 spritePointIrradiance(float3 world) {
 float3 irradiance = 0;
 [loop] for (uint i = 0; i < spritePointCounts.x; ++i) {
  float4 light = spritePointPositionRadius[i];
  float3 direction = light.xyz - world;
  float distanceSquared = dot(direction, direction);
  float radiusSquared = light.w * light.w;
  // HLSL logical operators need not short-circuit function calls. Keep the
  // finite-radius rejection ahead of all shell/aperture work explicitly.
  [branch] if (distanceSquared >= radiusSquared || spritePointColorIntensity[i].w == 0) continue;
  if (!spritePointVisibleThrough(light.xyz, world)) continue;
  float range = 1 - distanceSquared / radiusSquared;
  irradiance += spritePointColorIntensity[i].xyz * spritePointColorIntensity[i].w
   * range * range / (1 + distanceSquared);
 }
 return irradiance;
}
struct Pixel {
 uint color : SV_Target0;
 uint pick : SV_Target1;
 float depth : SV_Target2;
 uint rgba : SV_Target3;
 uint viewModelMask : SV_Target4;
};
Pixel PS(Interpolated input) {
 if (input.volume.y <= 0) discard;
 uint width, height;
 codes.GetDimensions(width, height);
 float2 coverageUv = input.uv;
 if ((flags.w & 1) != 0) coverageUv = frac(coverageUv);
 float2 uv = (flags.w & 32768) != 0 ? frac((input.world.xz + groundUv.yz) * groundUv.x) : coverageUv;
 bool absent = (flags.w & 4) != 0 && (any(coverageUv < 0) || any(coverageUv >= 1));
 int2 texel = (int2)clamp(uv * float2(width, height), float2(0, 0), float2(width - 1, height - 1));
 if ((flags.w & 4) != 0) {
  uint ow, oh; opacity.GetDimensions(ow, oh);
  int2 maskTexel = (int2)clamp(coverageUv * float2(ow, oh), float2(0, 0), float2(ow - 1, oh - 1));
  absent = absent || opacity.Load(int3(maskTexel, 0)) == 0;
 }
 uint code = codes.Load(int3(texel, 0));
 absent = absent || ((flags.w & 2) != 0 && code == 0);
 // Volume side faces share the front texture binding but are opaque palette
 // samples. A painted palette zero must bypass sprite masks/transparent zero.
 if (input.volume.x >= 0) {
  code = (uint)input.volume.x;
  absent = false;
 }
 if (absent) {
  if (input.fallback.x < 0) discard;
  code = (uint)input.fallback.x;
 }
 uint color = code;
 float3 rgb = paletteRgb(code);
 float alpha = 1;
 if ((flags.w&128)!=0) { float4 rgba=litRgba(code,0); rgb=rgba.rgb; alpha=rgba.a; }
 if (flags.x != 0) {
  uint level = 0;
  if (flags.x == 1) level = ((uint)(absent ? input.fallback.y : input.shadowParameters.w) & 3) * 4
   + (uint)(directionalShadow(input.world, input.shadowParameters) * 3 + 0.5);
  else if (flags.x == 2) level = (uint)(saturate(input.normalDiffuse.w * (1 - directionalShadow(input.world, input.shadowParameters))) * (flags.y - 1) + 0.5);
  else if (flags.x == 4) level = (uint)shading.x;
  else level = (uint)(pointAmount(input.world, input.normalDiffuse.xyz) * (flags.y - 1) + 0.5);
  uint lutIndex = min(code, flags.z - 1) * flags.y + min(level, flags.y - 1);
  uint lutWidth = max(1, (uint)shadowScale.z);
  color = lightLut.Load(int3(lutIndex % lutWidth, lutIndex / lutWidth, 0));
  if((flags.w&32)!=0){
  float continuous=(float)level;
  if(flags.x==1) continuous=((uint)(absent?input.fallback.y:input.shadowParameters.w)&3)*4 + smoothDirectionalShadow(input.world,input.shadowParameters)*3;
  else if(flags.x==2) continuous=saturate(input.normalDiffuse.w*(1-smoothDirectionalShadow(input.world,input.shadowParameters)))*(flags.y-1);
  else if(flags.x==4) continuous=(flags.w&256)!=0?nativeLightAmount(input.world,shading.x):shading.x;
  else continuous=pointAmount(input.world,input.normalDiffuse.xyz)*(flags.y-1);
  uint original=((flags.w&64)!=0 && !absent)?unlitCodes.Load(int3(texel,0)):code;
  if((flags.w&128)!=0) { float4 rgba=litRgba(code,continuous); rgb=rgba.rgb; alpha=rgba.a; }
  else rgb=litRgb(original,continuous);
  } else if((flags.w&128)!=0) { float4 rgba=litRgba(code,(float)level); rgb=rgba.rgb; alpha=rgba.a; }
  else rgb=paletteRgb(color);
 }
 [branch] if ((flags.w&8192)!=0) {
  float3 pointRgb = interiorPointIrradiance(input.world, input.normalDiffuse.xyz);
  [branch] if ((flags.w&65536)!=0) pointRgb=townPointGeometryIrradiance(input.world,input.normalDiffuse.xyz,true);
  float3 level = saturate(pointRgb / parameters.x) * (flags.y-1);
  rgb = float3(litRgba(code,level.r).r,litRgba(code,level.g).g,litRgba(code,level.b).b);
 }
 if ((flags.w & 16) != 0) {
  uint destination = priorIndexedColor.Load(int3((int2)input.position.xy, 0));
  // 256-wide rows are destination codes; source is the column.
  color = paletteBlendLut.Load(int3(color, destination, 0));
  rgb=paletteRgb(color);
  alpha=1;
 }
 if ((flags.w&262144)!=0) {
  rgb = saturate(input.authoredColor.rgb);
  alpha = saturate(input.authoredColor.a);
 } else if ((flags.w&512)!=0) {
  rgb=linearToSrgb(srgbToLinear(saturate(rgb))*saturate(input.authoredColor.rgb));
  alpha*=saturate(input.authoredColor.a);
 }
 [branch] if ((flags.w&1024)!=0) {
  float3 pointIrradiance = spritePointIrradiance(input.world);
  // No gamma roundtrip for outside/blocked samples: keep legacy RGB bytes.
  if (any(pointIrradiance > 0)) {
   float3 baseLinear = srgbToLinear(saturate(rgb));
   float luminance = dot(baseLinear, float3(0.2126, 0.7152, 0.0722));
   rgb = linearToSrgb(saturate(baseLinear + luminance * pointIrradiance));
  }
 }
 [branch] if ((flags.w&16384)!=0) {
  float3 irradiance = firePointIrradiance(input.world, input.normalDiffuse.xyz);
  if (any(irradiance > 0)) {
   // Native masonry retains its original unlit codes separately from the
   // indexed bake. Imported/actor albedo uses its paired RGB row at level 0.
   uint original = ((flags.w&64)!=0 && !absent) ? unlitCodes.Load(int3(texel,0)) : code;
   float3 albedo = (flags.w&128)!=0 ? litRgba(original,0).rgb : paletteRgb(original);
   float3 baseLinear = srgbToLinear(saturate(albedo));
   if ((flags.w&512)!=0) baseLinear *= saturate(input.authoredColor.rgb);
   float3 contribution = baseLinear * irradiance;
   // Outside/blocked/zero-albedo samples preserve the existing RGB arithmetic.
   if (any(contribution > 0)) rgb = linearToSrgb(saturate(srgbToLinear(saturate(rgb)) + contribution));
  }
 }
 [branch] if ((flags.w&65536)!=0 && flags.x!=3 && (flags.w&131072)!=0) {
  float3 irradiance = townPointGeometryIrradiance(input.world,input.normalDiffuse.xyz,true);
  if (any(irradiance > 0)) {
   // Native masonry retains its original unlit codes separately from the
   // indexed bake. Imported/actor albedo uses its paired RGB row at level 0.
   uint original = ((flags.w&64)!=0 && !absent) ? unlitCodes.Load(int3(texel,0)) : code;
   float3 albedo = (flags.w&128)!=0 ? litRgba(original,0).rgb : paletteRgb(original);
   float3 baseLinear = srgbToLinear(saturate(albedo));
   if ((flags.w&512)!=0) baseLinear *= saturate(input.authoredColor.rgb);
   float3 contribution = baseLinear * irradiance;
   // Outside/blocked/zero-albedo samples preserve the existing RGB arithmetic.
   if (any(contribution > 0)) rgb = linearToSrgb(saturate(srgbToLinear(saturate(rgb)) + contribution));
  }
 }
 [branch] if ((flags.w&65536)!=0 && flags.x!=3 && (flags.w&131072)==0) {
  float3 pointLighting=townPointGeometryIrradiance(input.world,input.normalDiffuse.xyz,false);
  if (any(pointLighting>0)) {
   float3 baseLinear=srgbToLinear(saturate(rgb));
   float luminance=dot(baseLinear,float3(0.2126,0.7152,0.0722));
   rgb=linearToSrgb(saturate(baseLinear+luminance*pointLighting));
  }
 }

 Pixel output;
 output.color = color;
 output.pick = input.pick;
 output.depth = input.depth;
 output.rgba = (packRgb(rgb)&0x00FFFFFF) | ((uint)(saturate(alpha)*255.0+0.5)<<24);
 // Coverage is emitted only after the same discard/depth tests as color.
 // Never borrow or rewrite RGBA alpha to communicate first-person coverage.
 output.viewModelMask = (flags.w&4096)!=0 ? 1 : 0;
 return output;
}
)hlsl";

struct ShaderBytecodeEntry {
	// All views refer to immutable process-lifetime literals, never worker data.
	std::string_view source, entry, profile, pointLightMacro;
	UINT flags1 = 0, flags2 = 0;
	unsigned maxPointLights = 0;
	ComOwner<ID3DBlob> code;
};

// Owned by the render thread. Compile joins its CPU worker before publishing
// anything here. Static COM owners dispose these CPU blobs at process exit.
std::array<ShaderBytecodeEntry, 2> ShaderBytecode;
uint64_t ShaderCompilations = 0, ShaderCacheHits = 0;
size_t ShaderBytecodeBytes = 0;

void UpdateShaderBytecodeStatus()
{
	Status.shaderCompilations = ShaderCompilations;
	Status.shaderCacheHits = ShaderCacheHits;
	Status.shaderBytecodeBytes = ShaderBytecodeBytes;
}

bool Fail(const std::string &message, HRESULT result = S_OK, TownGpuFailureKind kind = TownGpuFailureKind::None)
{
	WorldFrameTiming.known = false;
	WorldFrameTiming.gpuMilliseconds = -1;
	Status.townPointFrameSerial = 0;
	if (FrameFailed)
		return false;
	Status.frameSucceeded = false;
	Status.failure = message;
	Status.failureKind = kind != TownGpuFailureKind::None ? kind
	    : FAILED(result) ? TownGpuFailureKind::Device : TownGpuFailureKind::InvalidInput;
	if (FAILED(result)) {
		char code[24];
		std::snprintf(code, sizeof(code), " (HRESULT %08lX)", static_cast<unsigned long>(result));
		Status.failure += code;
	}
	Status.failureCode = static_cast<int32_t>(result);
	Status.failureCodeKnown = FAILED(result);
	diag::RecordNativeFailure(static_cast<uint8_t>(Status.failureKind), Status.failureCode, Status.failureCodeKnown);
	FrameFailed = true;
	return false;
}

bool Compile(const char *entry, const char *profile, ComOwner<ID3DBlob> &blob)
{
	diag::StageScope diagnosticStage(diag::SessionLogger(), diag::Stage::ShaderCompile, diag::SessionFrame(), diag::SampleSessionFrame());
	const std::string_view source { ShaderSource, sizeof(ShaderSource) - 1 };
	const std::string_view entryName(entry), profileName(profile);
	const size_t slot = entryName == "VS" && profileName == "vs_5_0" ? 0
	    : entryName == "PS" && profileName == "ps_5_0" ? 1 : ShaderBytecode.size();
	ShaderBytecodeEntry *cached = slot < ShaderBytecode.size() ? &ShaderBytecode[slot] : nullptr;
	if (cached != nullptr && cached->code && cached->source == source
	    && cached->entry == entryName && cached->profile == profileName
	    && cached->flags1 == ShaderCompileFlags && cached->flags2 == 0
	    && cached->pointLightMacro == ShaderPointLightMacro
	    && cached->maxPointLights == TownMaxPointLights) {
		// A separate COM reference keeps the caller independent of cache lifetime.
		cached->code->AddRef();
		*blob.put() = cached->code.get();
		if (ShaderCacheHits != std::numeric_limits<uint64_t>::max())
			++ShaderCacheHits;
		UpdateShaderBytecodeStatus();
		loading_ui::Report("3D rendering", "Reusing embedded shader", std::string("Source/engine/render/town_gpu.cpp :: ShaderSource / ") + entry + " / " + profile);
		return true;
	}
	ComOwner<ID3DBlob> errors;
	const std::string pointLightCount = std::to_string(TownMaxPointLights);
	const D3D_SHADER_MACRO macros[] { { ShaderPointLightMacro, pointLightCount.c_str() }, { nullptr, nullptr } };
	loading_ui::Report("3D rendering", "Compiling embedded shader", std::string("Source/engine/render/town_gpu.cpp :: ShaderSource / ")+entry+" / "+profile);
	loading_ui::SetProgress(0, 0);
	const HRESULT result = loading_ui::RunResponsive([&] { return D3DCompile(ShaderSource, sizeof(ShaderSource) - 1, "town_gpu", macros, nullptr,
	    entry, profile, ShaderCompileFlags, 0, blob.put(), errors.put()); });
	if (ShaderCompilations != std::numeric_limits<uint64_t>::max())
		++ShaderCompilations;
	UpdateShaderBytecodeStatus();
	if (FAILED(result)) {
		std::string message = "D3D11 shader compilation failed";
		if (errors)
			message.append(": ").append(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
		return Fail(message, result);
	}
	if (cached != nullptr && blob && blob->GetBufferSize() != 0) {
		const size_t oldBytes = cached->code ? cached->code->GetBufferSize() : 0;
		const size_t retainedBytes = ShaderBytecodeBytes - oldBytes;
		const size_t bytes = blob->GetBufferSize();
		// Oversized successful compilations remain usable without entering cache.
		if (bytes <= MaxShaderBytecodeBytes - retainedBytes) {
			blob->AddRef();
			*cached->code.put() = blob.get();
			cached->source = source;
			cached->entry = slot == 0 ? "VS" : "PS";
			cached->profile = slot == 0 ? "vs_5_0" : "ps_5_0";
			cached->flags1 = ShaderCompileFlags;
			cached->flags2 = 0;
			cached->pointLightMacro = ShaderPointLightMacro;
			cached->maxPointLights = TownMaxPointLights;
			ShaderBytecodeBytes = retainedBytes + bytes;
			UpdateShaderBytecodeStatus();
		}
	}
	return true;
}

bool CreateDevice(bool allowWarp)
{
	diag::StageScope diagnosticStage(diag::SessionLogger(), diag::Stage::Device3D, diag::SessionFrame(), diag::SampleSessionFrame());
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
		return Fail("D3D11 hardware device unavailable; no production software fallback", result, TownGpuFailureKind::Unsupported);
	Status.featureLevel = static_cast<uint32_t>(feature);
	ComOwner<IDXGIDevice> dxgiDevice;
	ComOwner<IDXGIAdapter> adapter;
	DXGI_ADAPTER_DESC description {};
	if (SUCCEEDED(Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(dxgiDevice.put())))
	    && SUCCEEDED(dxgiDevice->GetAdapter(adapter.put())) && SUCCEEDED(adapter->GetDesc(&description))) {
		char name[512] {};
		WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name), nullptr, nullptr);
		Status.adapter = name;
		Status.vendorId = description.VendorId; Status.deviceId = description.DeviceId;
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
		{ "TEXCOORD", 6, DXGI_FORMAT_R32_FLOAT, 0, offsetof(ProjectedVertex, clipW), D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	result = Device->CreateDepthStencilState(&depth, PaletteBlendDepthState.put());
	if (FAILED(result))
		return Fail("Create native palette read-only depth state", result);
	D3D11_BLEND_DESC paletteWrite {};
	paletteWrite.IndependentBlendEnable = TRUE;
	paletteWrite.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	paletteWrite.RenderTarget[3].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	paletteWrite.RenderTarget[4].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	// Integer render targets use an exact lookup, never DXGI alpha blending.
	result = Device->CreateBlendState(&paletteWrite, PaletteBlendWriteState.put());
	if (FAILED(result))
		return Fail("Create native palette color-only write state", result);
	for (size_t i = 0; i < BlendStates.size(); ++i) {
		D3D11_BLEND_DESC blend {};
		blend.IndependentBlendEnable = TRUE;
		for (auto &target : blend.RenderTarget)
			target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		blend.RenderTarget[4].RenderTargetWriteMask = 0;
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
	diag::StageScope diagnosticStage(diag::SessionLogger(), diag::Stage::Targets, diag::SessionFrame(), diag::SampleSessionFrame());
	if (Width == width && Height == height && HardwareDepthView)
		return true;
	Width = Height = 0;
	ResetFireVolumeTargetViews();
	HardwareDepthView.reset();
	PriorIndexedColorView.reset();
	PriorIndexedColor.reset();
	// Optional compute bindings must never make the ordinary Town/C1 renderer
	// unavailable. Unsupported formats or an enhanced allocation refusal keep
	// original RTV-only targets; the volume preflight inspects their flags.
	bool fireBindings = true;
	for (DXGI_FORMAT format : { DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT }) {
		UINT support = 0;
		const HRESULT checked = Device->CheckFormatSupport(format, &support);
		const UINT required = format == DXGI_FORMAT_R32_UINT ? D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW : D3D11_FORMAT_SUPPORT_SHADER_LOAD;
		if (FAILED(checked) || (support & required) != required) fireBindings = false;
	}
	const DXGI_FORMAT formats[] { DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R8_UINT };
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
		if (fireBindings && (i == 0 || i == 2 || i == 4)) description.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
		if (fireBindings && i == 3) description.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		HRESULT result = Device->CreateTexture2D(&description, nullptr, Targets[i].texture.put());
		if (FAILED(result) && description.BindFlags != D3D11_BIND_RENDER_TARGET) {
			description.BindFlags = D3D11_BIND_RENDER_TARGET; fireBindings = false;
			result = Device->CreateTexture2D(&description, nullptr, Targets[i].texture.put());
		}
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

void UpdateTextureStatus()
{
	Status.cachedTextures = TexelCache.size() + OpacityCache.size();
	Status.cachedLightLuts = LightLutCache.size();
	Status.cachedTextureBytes = TextureBytes;
}

/** Eviction runs before uploads. Resources touched this frame may be borrowed
 * by any recorded batch and are never removed, even if its draw is deferred. */
bool MakeTextureRoom(size_t bytes, bool optional = false)
{
	if (bytes > MaxTextureBytes)
		return optional ? false : Fail("GPU texture payload exceeds the texture-cache budget", S_OK, TownGpuFailureKind::Capacity);
	while (TextureBytes > MaxTextureBytes - bytes) {
		auto oldestTexels = TexelCache.end();
		for (auto entry = TexelCache.begin(); entry != TexelCache.end(); ++entry) {
			if (entry->second.lastSeenFrame < FrameSerial
			    && (oldestTexels == TexelCache.end() || entry->second.lastSeenFrame < oldestTexels->second.lastSeenFrame))
				oldestTexels = entry;
		}
		auto oldestLut = LightLutCache.end();
		for (auto entry = LightLutCache.begin(); entry != LightLutCache.end(); ++entry) {
			if (entry->second.lastSeenFrame < FrameSerial
			    && (oldestLut == LightLutCache.end() || entry->second.lastSeenFrame < oldestLut->second.lastSeenFrame))
				oldestLut = entry;
		}
		auto oldestOpacity = OpacityCache.end();
		for (auto entry = OpacityCache.begin(); entry != OpacityCache.end(); ++entry) {
			if (entry->second.lastSeenFrame < FrameSerial
			    && (oldestOpacity == OpacityCache.end() || entry->second.lastSeenFrame < oldestOpacity->second.lastSeenFrame))
				oldestOpacity = entry;
		}
		if (oldestOpacity != OpacityCache.end()
		    && (oldestTexels == TexelCache.end() || oldestOpacity->second.lastSeenFrame <= oldestTexels->second.lastSeenFrame)
		    && (oldestLut == LightLutCache.end() || oldestOpacity->second.lastSeenFrame <= oldestLut->second.lastSeenFrame)) {
			TextureBytes -= oldestOpacity->second.bytes;
			OpacityCache.erase(oldestOpacity);
			++Status.textureEvictions;
			UpdateTextureStatus();
			continue;
		}
		if (oldestTexels == TexelCache.end() && oldestLut == LightLutCache.end())
			return optional ? false : Fail("GPU texture-cache budget exceeded by current-frame resources", S_OK, TownGpuFailureKind::Capacity);
		if (oldestLut == LightLutCache.end() || (oldestTexels != TexelCache.end() && oldestTexels->second.lastSeenFrame <= oldestLut->second.lastSeenFrame)) {
			TextureBytes -= oldestTexels->second.bytes;
			TexelCache.erase(oldestTexels);
		} else {
			TextureBytes -= oldestLut->second.bytes;
			LightLutCache.erase(oldestLut);
		}
		++Status.textureEvictions;
		UpdateTextureStatus();
	}
	return true;
}

template <typename T>
uint64_t PayloadFingerprint(std::span<const T> values)
{
	uint64_t result = 14695981039346656037ULL;
	const auto bytes = std::as_bytes(values);
	const std::byte *current = bytes.data();
	for (size_t i = 0; i < bytes.size(); ++i)
		result = (result ^ std::to_integer<uint8_t>(current[i])) * 1099511628211ULL;
	return result;
}

CachedTexture *GetTexture(const TownGpuTexture &input, TownGpuLighting lighting, bool optional = false)
{
	if (input.width <= 0 || input.height <= 0 || input.lightLevels == 0) {
		Fail("Invalid GPU texture dimensions or light-level count");
		return nullptr;
	}
	if (input.width > 16384 || input.height > 16384 || input.lightLevels > 16384) {
		Fail("GPU texture dimensions or light-level capacity exceeded", S_OK, TownGpuFailureKind::Capacity);
		return nullptr;
	}
	const bool sharedOpacity = input.opacityKey != 0;
	if (sharedOpacity && (input.opacityWidth <= 0 || input.opacityHeight <= 0
	        || input.opacityWidth > 16384 || input.opacityHeight > 16384)) {
		Fail("Invalid independent GPU coverage dimensions");
		return nullptr;
	}
	const size_t opacityCount = sharedOpacity ? static_cast<size_t>(input.opacityWidth) * input.opacityHeight : 0;
	if (opacityCount > MaxPixels) {
		Fail("Independent GPU coverage capacity exceeded", S_OK, TownGpuFailureKind::Capacity);
		return nullptr;
	}
	const size_t count = static_cast<size_t>(input.width) * input.height;
	if (count > MaxPixels || input.lightLut.size() / input.lightLevels > 262144) {
		Fail("GPU texture pixel or palette/LUT capacity exceeded", S_OK, TownGpuFailureKind::Capacity);
		return nullptr;
	}
	if ((!input.texelCodes.empty() && input.texelCodes.size() != count)
	    || (!input.opacity.empty() && input.opacity.size() != (sharedOpacity ? opacityCount : count))
	    || (!input.unlitCodes.empty() && input.unlitCodes.size() != count)
	    || input.lightLut.size() % input.lightLevels != 0) {
		Fail("Invalid GPU texture payload");
		return nullptr;
	}
	const TextureKey texelKey { input.stableKey, input.revision };
	const LightLutKey lutKey { input.lightLutKey != 0 ? TextureKey { input.lightLutKey, input.lightLutRevision } : texelKey, input.lightLutKey != 0 };
	const TextureKey coverageKey { input.opacityKey, input.opacityRevision };
	auto coverageFound = OpacityCache.find(coverageKey);
	CachedOpacity *coverage = !sharedOpacity || coverageFound == OpacityCache.end() ? nullptr : &coverageFound->second;
	if ((coverage != nullptr && (coverage->width != input.opacityWidth || coverage->height != input.opacityHeight
	        || (!input.opacity.empty() && PayloadFingerprint(input.opacity) != coverage->fingerprint)))
	    || (sharedOpacity && coverage == nullptr && input.opacity.size() != opacityCount)) {
		Fail("Missing or inconsistent independent immutable GPU coverage");
		return nullptr;
	}
	auto texelsFound = TexelCache.find(texelKey);
	auto lutFound = LightLutCache.find(lutKey);
	CachedTexels *texels = texelsFound == TexelCache.end() ? nullptr : &texelsFound->second;
	CachedLightLut *lut = lutFound == LightLutCache.end() ? nullptr : &lutFound->second;
	const bool rgbaEnabled = input.rgbaLightLutEnabled || !input.rgbaLightLut.empty();
	if ((texels != nullptr && (texels->width != input.width || texels->height != input.height
	        || (!sharedOpacity && !input.opacity.empty() && !texels->hasOpacity) || (!input.unlitCodes.empty() && !texels->unlit)))
	    || (lut != nullptr && (lut->lightLevels != input.lightLevels
	        || lut->hasRgba != rgbaEnabled
	        || (!input.lightLut.empty() && input.lightLut.size() != static_cast<size_t>(lut->codeCount) * lut->lightLevels)))) {
		Fail("GPU texture/LUT key/revision reused with a different layout");
		return nullptr;
	}
	if (texels == nullptr && input.texelCodes.size() != count) {
		Fail("Missing GPU texel payload for uncached identity");
		return nullptr;
	}
	if (lut == nullptr && input.lightLut.empty() && lighting != TownGpuLighting::Unlit) {
		Fail("Missing GPU light LUT for uncached identity");
		return nullptr;
	}
	const size_t indexedEntries = lut != nullptr ? static_cast<size_t>(lut->codeCount) * lut->lightLevels : input.lightLut.size();
	if ((rgbaEnabled && (indexedEntries == 0 || (lut == nullptr && input.rgbaLightLut.empty())))
	    || (!input.rgbaLightLut.empty() && input.rgbaLightLut.size() != indexedEntries)) {
		Fail("GPU RGBA light LUT requires its complete one-to-one indexed pair");
		return nullptr;
	}
	const unsigned codeCount = lut != nullptr ? lut->codeCount
	    : input.lightLut.empty() ? 256 : static_cast<unsigned>(input.lightLut.size() / input.lightLevels);
	const uint32_t maximumCode = texels != nullptr ? texels->maximumCode : *std::max_element(input.texelCodes.begin(), input.texelCodes.end());
	if (maximumCode >= codeCount || (lighting == TownGpuLighting::Unlit && maximumCode > 255)) {
		Fail("GPU texel code exceeds its palette/LUT");
		return nullptr;
	}
	const size_t texelBytes = texels == nullptr ? count * sizeof(uint32_t) + (sharedOpacity ? 0 : input.opacity.size()) + input.unlitCodes.size() : 0;
	const size_t coverageBytes = sharedOpacity && coverage == nullptr ? opacityCount : 0;
	const unsigned lutWidth = lut != nullptr ? lut->lutWidth : static_cast<unsigned>(std::min<size_t>(16384, input.lightLut.size()));
	const size_t lutHeight = lutWidth == 0 ? 0 : (input.lightLut.size() + lutWidth - 1) / lutWidth;
	const size_t paddedEntries = lutHeight * lutWidth;
	const size_t lutBytes = lut == nullptr ? paddedEntries * (rgbaEnabled ? 5 : 1) : 0;
	if (lutHeight > 16384 || texelBytes > MaxTextureBytes || lutBytes > MaxTextureBytes - texelBytes || coverageBytes > MaxTextureBytes - texelBytes - lutBytes) {
		if (!optional)
			Fail("GPU texture/LUT payload exceeds resource dimensions or cache budget", S_OK, TownGpuFailureKind::Capacity);
		return nullptr;
	}
	// Optional RGB pairs validate supplied payload witnesses even on warm hits.
	// Legacy warm resources keep their existing O(1) immutable-revision contract.
	if (rgbaEnabled && ((lut != nullptr
	        && ((!input.lightLut.empty() && PayloadFingerprint(input.lightLut) != lut->indexedFingerprint)
	            || (!input.rgbaLightLut.empty() && PayloadFingerprint(input.rgbaLightLut) != lut->rgbaFingerprint)))
	    || (texels != nullptr
	        && ((!input.texelCodes.empty() && PayloadFingerprint(input.texelCodes) != texels->codesFingerprint)
	            || (!sharedOpacity && !input.opacity.empty() && PayloadFingerprint(input.opacity) != texels->opacityFingerprint)
	            || (!input.unlitCodes.empty() && PayloadFingerprint(input.unlitCodes) != texels->unlitFingerprint))))) {
		Fail("GPU RGB-paired immutable identity reused with different supplied contents");
		return nullptr;
	}
	// Pin hits before making room: a new LUT revision can reuse the exact texel
	// resource that pressure eviction would otherwise consider an older entry.
	const uint64_t previousTexelFrame = texels != nullptr ? texels->lastSeenFrame : 0;
	const uint64_t previousLutFrame = lut != nullptr ? lut->lastSeenFrame : 0;
	const uint64_t previousCoverageFrame = coverage != nullptr ? coverage->lastSeenFrame : 0;
	if (texels != nullptr)
		texels->lastSeenFrame = FrameSerial;
	if (lut != nullptr)
		lut->lastSeenFrame = FrameSerial;
	if (coverage != nullptr)
		coverage->lastSeenFrame = FrameSerial;
	if (!MakeTextureRoom(texelBytes + lutBytes + coverageBytes, optional)) {
		// A declined optional material must not pin otherwise unused resources
		// and prevent the original floor/native materials from fitting later.
		if (optional) {
			if (texels != nullptr) texels->lastSeenFrame = previousTexelFrame;
			if (lut != nullptr) lut->lastSeenFrame = previousLutFrame;
			if (coverage != nullptr) coverage->lastSeenFrame = previousCoverageFrame;
		}
		return nullptr;
	}
	CachedTexels newTexels {};
	CachedLightLut newLut {};
	CachedOpacity newCoverage {};
	if (sharedOpacity && coverage == nullptr) {
		newCoverage.width = input.opacityWidth;
		newCoverage.height = input.opacityHeight;
		newCoverage.bytes = coverageBytes;
		newCoverage.lastSeenFrame = FrameSerial;
		newCoverage.fingerprint = PayloadFingerprint(input.opacity);
		if (!UploadTexture(input.opacityWidth, input.opacityHeight, DXGI_FORMAT_R8_UINT, input.opacity.data(), input.opacityWidth, newCoverage.view))
			return nullptr;
	}
	if (texels == nullptr) {
		newTexels.width = input.width;
		newTexels.height = input.height;
		newTexels.maximumCode = maximumCode;
		newTexels.hasOpacity = !sharedOpacity && !input.opacity.empty();
		newTexels.bytes = texelBytes;
		newTexels.lastSeenFrame = FrameSerial;
		newTexels.codesFingerprint = PayloadFingerprint(input.texelCodes);
		newTexels.opacityFingerprint = sharedOpacity ? 0 : PayloadFingerprint(input.opacity);
		newTexels.unlitFingerprint = PayloadFingerprint(input.unlitCodes);
		if (!UploadTexture(input.width, input.height, DXGI_FORMAT_R32_UINT, input.texelCodes.data(), input.width * sizeof(uint32_t), newTexels.codes)
		    || (!sharedOpacity && !input.opacity.empty() && !UploadTexture(input.width, input.height, DXGI_FORMAT_R8_UINT, input.opacity.data(), input.width, newTexels.opacity))
		    || (!input.unlitCodes.empty() && (input.unlitCodes.size()!=count || !UploadTexture(input.width,input.height,DXGI_FORMAT_R8_UINT,input.unlitCodes.data(),input.width,newTexels.unlit))))
			return nullptr;
	}
	if (lut == nullptr && !input.lightLut.empty()) {
		newLut.lightLevels = input.lightLevels;
		newLut.codeCount = codeCount;
		newLut.lutWidth = lutWidth;
		newLut.bytes = lutBytes;
		newLut.lastSeenFrame = FrameSerial;
		newLut.hasRgba = rgbaEnabled;
		newLut.indexedFingerprint = PayloadFingerprint(input.lightLut);
		newLut.rgbaFingerprint = PayloadFingerprint(input.rgbaLightLut);
		std::vector<uint8_t> padded;
		const uint8_t *lutData = input.lightLut.data();
		if (paddedEntries != input.lightLut.size()) {
			padded.assign(input.lightLut.begin(), input.lightLut.end());
			padded.resize(paddedEntries);
			lutData = padded.data();
		}
		if (!UploadTexture(lutWidth, static_cast<int>(lutHeight), DXGI_FORMAT_R8_UINT, lutData, lutWidth, newLut.view))
			return nullptr;
		if (rgbaEnabled) {
			std::vector<uint32_t> rgbaPadded;
			const uint32_t *rgbaData = input.rgbaLightLut.data();
			if (paddedEntries != input.rgbaLightLut.size()) {
				rgbaPadded.assign(input.rgbaLightLut.begin(), input.rgbaLightLut.end());
				rgbaPadded.resize(paddedEntries);
				rgbaData = rgbaPadded.data();
			}
			if (!UploadTexture(lutWidth, static_cast<int>(lutHeight), DXGI_FORMAT_R32_UINT, rgbaData, lutWidth * sizeof(uint32_t), newLut.rgbaView))
				return nullptr;
		}
	}
	// Commit only complete immutable uploads; earlier frame bindings retain
	// their resource pointers and constants when this shared LUT grows.
	if (texels == nullptr) {
		texels = &TexelCache.emplace(texelKey, std::move(newTexels)).first->second;
		TextureBytes += texelBytes;
		Status.uploadedTexelBytes += texelBytes;
	}
	if (lut == nullptr && !input.lightLut.empty()) {
		lut = &LightLutCache.emplace(lutKey, std::move(newLut)).first->second;
		TextureBytes += lutBytes;
		Status.uploadedLutBytes += lutBytes;
	}
	if (sharedOpacity && coverage == nullptr) {
		coverage = &OpacityCache.emplace(coverageKey, std::move(newCoverage)).first->second;
		TextureBytes += coverageBytes;
		Status.uploadedTexelBytes += coverageBytes;
	}
	UpdateTextureStatus();
	const TextureBindingKey bindingKey { texelKey, lutKey, input.lightLevels, lut != nullptr, sharedOpacity ? coverageKey : TextureKey {} };
	return &TextureCache.try_emplace(bindingKey, CachedTexture { texels, lut, input.lightLevels, codeCount, lutWidth, texels->hasOpacity || sharedOpacity, coverage }).first->second;
}

bool Finite(float value) { return std::isfinite(value); }
bool Finite(TownLightVector value) { return Finite(value.x) && Finite(value.height) && Finite(value.z); }


D3D11_BOX PaletteSnapshotBox(const std::array<TownGpuVertex, 3> &vertices)
{
	const D3D11_BOX full { 0, 0, 0, static_cast<UINT>(Width), static_cast<UINT>(Height), 1 };
	// Projected positions are finite before submission. Keep extreme clipping
	// inputs on the old whole-frame path rather than rely on cancellation in VS.
	const float reliableRange = 4.0F * static_cast<float>(std::max(Width, Height));
	for (const TownGpuVertex &vertex : vertices)
		if (!Finite(vertex.x) || !Finite(vertex.y)
		    || std::abs(vertex.x) > reliableRange || std::abs(vertex.y) > reliableRange)
			return full;
	const double minX = std::min({ vertices[0].x, vertices[1].x, vertices[2].x });
	const double minY = std::min({ vertices[0].y, vertices[1].y, vertices[2].y });
	const double maxX = std::max({ vertices[0].x, vertices[1].x, vertices[2].x });
	const double maxY = std::max({ vertices[0].y, vertices[1].y, vertices[2].y });
	// Two raster pixels conservatively include the fixed-point edge grid and
	// float clip-space roundtrip. Clamp in double before converting to UINT.
	const auto lower = [](double value, int extent) {
		return static_cast<UINT>(std::clamp(std::floor(value - 2.0), 0.0, static_cast<double>(extent)));
	};
	const auto upper = [](double value, int extent) {
		return static_cast<UINT>(std::clamp(std::ceil(value + 2.0), 0.0, static_cast<double>(extent)));
	};
	const D3D11_BOX bounded { lower(minX, Width), lower(minY, Height), 0,
		upper(maxX, Width), upper(maxY, Height), 1 };
	// Do not introduce skipped overlays or an empty D3D copy as a new contract.
	// Fully offscreen/degenerate bounds retain the original complete snapshot.
	if (bounded.left >= bounded.right || bounded.top >= bounded.bottom)
		return full;
	return bounded;
}

bool ValidOpaqueBlocker(const TownLightOccluder &blocker)
{
	const auto bounded = [](TownLightVector value) {
		return Finite(value) && std::abs(value.x) <= 1000000 && std::abs(value.height) <= 1000000 && std::abs(value.z) <= 1000000;
	};
	return bounded(blocker.minimum) && bounded(blocker.maximum)
	    && blocker.minimum.x < blocker.maximum.x && blocker.minimum.height < blocker.maximum.height
	    && blocker.minimum.z < blocker.maximum.z && blocker.apertures.empty();
}

bool MakeConstants(const TownGpuMaterial &material, const CachedTexture &texture, Constants &output, bool projected = false)
{
	if (material.absoluteDisplayRgb && (!projected || !FrameSmoothLighting
	    || material.lighting != TownGpuLighting::Unlit || material.paletteBlend || material.viewModel
	    || material.receivesSpritePointLight || material.receivesFirePointLight || material.receivesTownPointLight))
		return Fail("Absolute display RGB requires projected smooth Unlit geometry without added lighting");
	if (material.viewModel && (!projected || material.paletteBlend))
		return Fail("GPU viewmodel requires projected geometry and cannot blend a native palette", S_OK, TownGpuFailureKind::Unsupported);
	if (FrameViewModelStarted && !material.viewModel)
		return Fail("GPU world geometry cannot follow the final viewmodel pass");
	if (static_cast<uint32_t>(material.lighting) > static_cast<uint32_t>(TownGpuLighting::Palette)
	    || !Finite(material.diffuse) || !Finite(material.shadowBias) || !Finite(material.shadowSlopeU) || !Finite(material.shadowSlopeV)
	    || !std::all_of(material.normal.begin(), material.normal.end(), [](float value) { return Finite(value); })
	    || material.fallbackPaletteIndex < -1 || material.fallbackPaletteIndex > 255
	    || (material.fallbackPaletteIndex >= 0 && material.lighting != TownGpuLighting::Shadow && material.lighting != TownGpuLighting::Unlit)
	    || (material.lighting == TownGpuLighting::Shadow && material.fallbackPaletteIndex >= static_cast<int>(texture.codeCount))
	    || (material.lighting == TownGpuLighting::Shadow && texture.lightLevels != 16)
	    || (material.lighting == TownGpuLighting::Palette
	        && (material.shade < 0 || material.shade > 15 || texture.lightLevels != 16
	            || (texture.codeCount != 256 && (texture.lut == nullptr || !texture.lut->hasRgba))))
	    || (material.lighting == TownGpuLighting::Interior
	        && (!Finite(material.interiorRedNormalization) || material.interiorRedNormalization <= 0
	            || !Finite(material.interiorPointRange) || material.interiorPointRange <= 0)))
		return Fail("Invalid GPU material parameters");
	if (material.groundWorldUv && (material.lighting != TownGpuLighting::Directional
	        || !Finite(material.groundWorldUvScale) || material.groundWorldUvScale <= 0 || material.groundWorldUvScale > 64
	        || !std::all_of(material.groundWorldUvOffset.begin(), material.groundWorldUvOffset.end(), [](float value) { return Finite(value) && std::abs(value) <= 1000000; })
	        || material.repeat || material.paletteBlend || material.viewModel))
		return Fail("Invalid world-space ground material");
	if (material.interiorRgbLighting
	    && (material.lighting != TownGpuLighting::Interior || texture.lut == nullptr || !texture.lut->hasRgba))
		return Fail("GPU interior RGB requires Interior and a paired RGB LUT", S_OK, TownGpuFailureKind::Unsupported);
	if (material.blockers.size() > TownMaxLightOccluders - 1
	    || !std::all_of(material.blockers.begin(), material.blockers.end(), ValidOpaqueBlocker))
		return Fail("Invalid GPU interior opaque blockers");
	if (material.receivesSpritePointLight && FrameSpritePointLighting
	    && (!FrameSmoothLighting && (texture.lut == nullptr || !texture.lut->hasRgba)))
		return Fail("GPU sprite point irradiance requires smooth RGB or a paired RGB LUT", S_OK, TownGpuFailureKind::Unsupported);
	if (material.useFramePointLightVisibility
	    && (!FrameSpritePointLighting || material.lighting != TownGpuLighting::Interior))
		return Fail("GPU frame point visibility requires a validated frame field and an Interior material");
	if (material.receivesFirePointLight
	    && (!FrameFirePointLighting || material.paletteBlend
	        || (!FrameSmoothLighting && (texture.lut == nullptr || !texture.lut->hasRgba))))
		return Fail("GPU fire diffuse requires a validated RGB field and a non-blended material");
	const bool townPoint = FrameTownPointGeometryLighting && (material.receivesTownPointLight
	    || (material.lighting == TownGpuLighting::Interior && material.pointGeometryOffset != 0xFFFFFFFFU));
	if (townPoint && (material.paletteBlend
	    || (material.lighting == TownGpuLighting::Interior && !material.interiorRgbLighting)
	    || (!FrameSmoothLighting && (texture.lut == nullptr || !texture.lut->hasRgba))))
		return Fail("Town point RGB requires an RGB material without palette blending");
	const bool rgbaLut = texture.lut != nullptr && texture.lut->hasRgba
	    && (material.lighting != TownGpuLighting::Interior || material.interiorRgbLighting);
	if (townPoint) FrameTownPointReceiverSubmitted = true;
	output = {};
	output.target = { static_cast<float>(Width), static_cast<float>(Height), 0, 0 };
	output.projection = { FrameProjection.nearClip, FrameProjection.farClip, FrameProjection.perspective ? 1.0F : 0.0F, 0 };
	output.flags = { static_cast<uint32_t>(material.lighting), texture.lightLevels, texture.codeCount,
		static_cast<uint32_t>((material.repeat ? 1 : 0) | (material.transparentZero && !texture.hasOpacity ? 2 : 0)
		    | (texture.hasOpacity ? 4 : 0) | (FrameShadow.resolution > 0 ? 8 : 0) | (material.paletteBlend ? 16 : 0) | (FrameSmoothLighting ? 32 : 0)
		    | (texture.texels->unlit ? 64 : 0) | (rgbaLut ? 128 : 0) | (material.nativeLighting ? 256 : 0)
		    | (material.receivesSpritePointLight && FrameSpritePointLighting ? SpritePointLightFlag : 0)
		    | (material.useFramePointLightVisibility ? FramePointVisibilityFlag : 0)
		    | (material.viewModel ? ViewModelFlag : 0)
		    | (material.interiorRgbLighting ? InteriorRgbLightFlag : 0)
		    | (material.receivesFirePointLight && FrameFirePointLighting ? FirePointLightFlag : 0)
		    | (townPoint ? TownPointGeometryFlag : 0)
		    | (townPoint && material.townPointDiffuse ? TownPointDiffuseFlag : 0)
		    | (material.absoluteDisplayRgb ? AbsoluteDisplayRgbFlag : 0)
		    | (material.groundWorldUv ? GroundWorldUvFlag : 0)) };
	output.groundUv = { material.groundWorldUvScale, material.groundWorldUvOffset[0], material.groundWorldUvOffset[1], 0 };
	output.shading.x = material.lighting == TownGpuLighting::Palette ? static_cast<float>(material.shade) : 0;
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
			Finite(light.intensity) ? std::clamp(light.intensity, 0.0F, TownMaxPointLightIntensity) : 0,
			Finite(light.color.green) ? std::clamp(light.color.green, 0.0F, 16.0F) : 0,
			Finite(light.color.blue) ? std::clamp(light.color.blue, 0.0F, 16.0F) : 0 };
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
#include "engine/render/town_gpu_fire_volume_backend.inc"
#endif

} // namespace

#include "engine/render/town_gpu_mesh_backend.inc"

#ifdef _WIN32
namespace {
#include "engine/render/town_gpu_town_point_backend.inc"
} // namespace
#endif

bool TownGpuBeginFrame(int width, int height, bool allowWarpForDiagnostics, const TownGpuProjection &projection)
{
	WorldFrameTiming = {};
	BeforeReadbackForDiagnostics = nullptr;
	FireVolumeFault = TownGpuFireVolumeFault::None;
#ifdef _WIN32
	ClearFireVolumeFrame();
	if (WorldTimingOpen || WorldTimingSubmitted) ResetWorldTimingQueries();
	FrameActive = false;
	FrameSmoothLighting = false;
	FrameRgbaLut = false;
	FrameSpritePointLighting = false;
	ClearFirePointFrameBinding();
	ClearTownPointGeometryFrameBinding();
	Status.fireShadowUploadedBytes = 0;
	Status.townPointShadowUploadedBytes = 0;
	Status.fireShadowBytes = FireShadowResource
	    ? FireShadowLightCount * 6 * FireShadowResolution * FireShadowResolution * sizeof(float) : 0;
	FrameFailed = false;
	FramePaletteBlendReady = false;
	FramePaletteOverlayStarted = false;
	FrameViewModelStarted = false;
	Vertices.clear();
	AuthoredColors.clear();
	Batches.clear();
	BeginMeshFrame();
	TextureCache.clear();
	++FrameSerial;
	for (auto entry = TexelCache.begin(); entry != TexelCache.end();) {
		if (FrameSerial - entry->second.lastSeenFrame > 2) {
			TextureBytes -= entry->second.bytes;
			entry = TexelCache.erase(entry);
		} else {
			++entry;
		}
	}
	for (auto entry = OpacityCache.begin(); entry != OpacityCache.end();) {
		if (FrameSerial - entry->second.lastSeenFrame > 2) {
			TextureBytes -= entry->second.bytes;
			entry = OpacityCache.erase(entry);
		} else {
			++entry;
		}
	}
	for (auto entry = LightLutCache.begin(); entry != LightLutCache.end();) {
		if (FrameSerial - entry->second.lastSeenFrame > 2) {
			TextureBytes -= entry->second.bytes;
			entry = LightLutCache.erase(entry);
		} else {
			++entry;
		}
	}
	FrameShadow = {};
	FrameProjection = {};
	Status.frameSucceeded = false;
	Status.failure.clear();
	Status.failureKind = TownGpuFailureKind::None;
	Status.failureCode = 0; Status.failureCodeKnown = false;
	Status.submittedTriangles = 0;
	Status.drawCalls = 0;
	Status.uploadedTexelBytes = 0;
	Status.uploadedLutBytes = 0;
	Status.uploadedAuthoredColorBytes = 0;
	Status.textureEvictions = 0;
	Status.paletteBlendTriangles = 0;
	Status.paletteBlendCopyBytes = 0;
	Status.paletteBlendAdmittedBytes = 0;
	UpdateTextureStatus();
	Status.frameMilliseconds = 0;
	Status.readbackMilliseconds = 0;
	FrameStart = Clock::now();
	if (width <= 0 || height <= 0)
		return Fail("Invalid GPU frame dimensions");
	if (width > 16384 || height > 16384 || static_cast<uint64_t>(width) * height > MaxPixels)
		return Fail("GPU frame dimensions or pixel budget exceeded", S_OK, TownGpuFailureKind::Capacity);
	if (!Finite(projection.nearClip) || !Finite(projection.farClip) || projection.nearClip <= 0
	    || projection.farClip <= projection.nearClip || projection.farClip > 4096)
		return Fail("Invalid GPU projection range (0 < near < far <= 4096 required)");
	if (Device && Status.warp && !allowWarpForDiagnostics)
		ResetTownGpuResources();
	if (!Device && !CreateDevice(allowWarpForDiagnostics)) {
		const TownGpuStatus failed = Status;
		ResetTownGpuResources();
		Status = failed;
		UpdateShaderBytecodeStatus();
		Status.available = false;
		FrameFailed = true;
		return false;
	}
	if (!CreateTargets(width, height))
		return false;
	FrameProjection = projection;
	if (TexturePreparationFrame == std::numeric_limits<uint64_t>::max())
		return Fail("GPU texture-preparation frame counter exhausted", S_OK, TownGpuFailureKind::Capacity);
	++TexturePreparationFrame;
	FrameActive = true;
	if (WorldTimingSerial != std::numeric_limits<uint64_t>::max())
		WorldFrameTiming.frameSerial = ++WorldTimingSerial;
	return true;
#else
	(void)width;
	(void)height;
	(void)allowWarpForDiagnostics;
	(void)projection;
	Status = {};
	Status.failure = "Direct3D11 town rendering is available only on Windows";
	Status.failureKind = TownGpuFailureKind::Unsupported;
	return false;
#endif
}

void TownGpuDiscardFrame()
{
	WorldFrameTiming = {};
	Status.frameSucceeded = false; // Includes refusal after a successful End.
#ifdef _WIN32
	ResetWorldTimingQueries();
	ClearFireVolumeFrame();
	FrameActive = false;
	FrameFailed = true;
	ClearFirePointFrameBinding();
	ClearTownPointGeometryFrameBinding();
	BeforeReadbackForDiagnostics = nullptr;
#endif
}

uint64_t GetTownGpuRecordingFrame()
{
#ifdef _WIN32
	return FrameActive && !FrameFailed ? TexturePreparationFrame : 0;
#else
	return 0;
#endif
}

bool TownGpuPrepareTexture(const TownGpuTexture &texture, TownGpuLighting lighting)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed)
		return Fail("No valid GPU frame is recording");
	if (static_cast<uint32_t>(lighting) > static_cast<uint32_t>(TownGpuLighting::Palette))
		return Fail("Invalid GPU texture preparation lighting mode");
	return GetTexture(texture, lighting) != nullptr;
#else
	(void)texture;
	(void)lighting;
	return false;
#endif
}

bool TownGpuPrepareTextureIfResident(const TownGpuTexture &texture, TownGpuLighting lighting)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed)
		return false;
	const TextureKey texelKey { texture.stableKey, texture.revision };
	const LightLutKey lutKey { texture.lightLutKey != 0 ? TextureKey { texture.lightLutKey, texture.lightLutRevision } : texelKey, texture.lightLutKey != 0 };
	if (TexelCache.find(texelKey) == TexelCache.end()
	    || (lighting != TownGpuLighting::Unlit && LightLutCache.find(lutKey) == LightLutCache.end())
	    || (texture.opacityKey != 0 && OpacityCache.find({ texture.opacityKey, texture.opacityRevision }) == OpacityCache.end()))
		return false;
	TownGpuTexture resident = texture;
	resident.texelCodes = {};
	resident.opacity = {};
	resident.unlitCodes = {};
	resident.lightLut = {};
	resident.rgbaLightLutEnabled = texture.rgbaLightLutEnabled || !texture.rgbaLightLut.empty();
	resident.rgbaLightLut = {};
	return TownGpuPrepareTexture(resident, lighting);
#else
	(void)texture;
	(void)lighting;
	return false;
#endif
}

bool TownGpuTryPrepareTexture(const TownGpuTexture &texture, TownGpuLighting lighting)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed)
		return false;
	if (static_cast<uint32_t>(lighting) > static_cast<uint32_t>(TownGpuLighting::Palette))
		return Fail("Invalid optional GPU texture preparation lighting mode");
	return GetTexture(texture, lighting, true) != nullptr;
#else
	(void)texture;
	(void)lighting;
	return false;
#endif
}

bool TownGpuSetShadow(const TownGpuShadow &shadow)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed || !GeometryCommands.empty())
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

bool TownGpuSetPaletteBlend(const TownGpuPaletteBlend &blend)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed || !GeometryCommands.empty())
		return Fail("Palette blend upload must precede GPU geometry submission");
	if (blend.stableKey == 0 || (!blend.lookup.empty() && blend.lookup.size() != 256 * 256))
		return Fail("Invalid GPU palette blend identity or 65536-byte lookup");
	if (!PaletteBlendResource || PaletteBlendKey != blend.stableKey || PaletteBlendRevision != blend.revision) {
		if (blend.lookup.size() != 256 * 256)
			return Fail("Missing GPU palette blend payload for uncached identity");
		ComOwner<ID3D11ShaderResourceView> uploaded;
		if (!UploadTexture(256, 256, DXGI_FORMAT_R8_UINT, blend.lookup.data(), 256, uploaded))
			return false;
		PaletteBlendResource = std::move(uploaded);
		PaletteBlendKey = blend.stableKey;
		PaletteBlendRevision = blend.revision;
		Status.uploadedLutBytes += 256 * 256;
	}
	// Lazy, bounded auxiliary resource: at most MaxPixels bytes, independent
	// of the immutable texture cache, plus one 64 KiB blend table.
	if (!PriorIndexedColorView) {
		D3D11_TEXTURE2D_DESC description {};
		description.Width = Width;
		description.Height = Height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R8_UINT;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		HRESULT result = Device->CreateTexture2D(&description, nullptr, PriorIndexedColor.put());
		if (FAILED(result))
			return Fail("Create native palette prior-color snapshot", result);
		result = Device->CreateShaderResourceView(PriorIndexedColor.get(), nullptr, PriorIndexedColorView.put());
		if (FAILED(result))
			return Fail("Create native palette prior-color snapshot view", result);
	}
	FramePaletteBlendReady = true;
	return true;
#else
	(void)blend;
	return false;
#endif
}

bool TownGpuSetSmoothLighting(std::span<const uint32_t> palette, std::span<const float> nativeLight, int fieldWidth, int fieldHeight)
{
#ifdef _WIN32
    if(!FrameActive || FrameFailed || !GeometryCommands.empty() || palette.size()!=256 || fieldWidth<0 || fieldHeight<0 || fieldWidth>128 || fieldHeight>128
       || nativeLight.size()!=static_cast<size_t>(fieldWidth)*fieldHeight || (nativeLight.empty() && (fieldWidth!=0 || fieldHeight!=0))) return Fail("Invalid smooth world light field");
    if(!RgbPaletteResource || CachedRgbPalette.size()!=palette.size() || !std::equal(palette.begin(),palette.end(),CachedRgbPalette.begin())) {
        if(!UploadTexture(256,1,DXGI_FORMAT_R32_UINT,palette.data(),256*4,RgbPaletteResource))return false;
        CachedRgbPalette.assign(palette.begin(),palette.end());
    }
    if(nativeLight.empty()){NativeLightResource.reset();CachedNativeLight.clear();CachedNativeLightWidth=CachedNativeLightHeight=0;}
    else if(!NativeLightResource || CachedNativeLightWidth!=fieldWidth || CachedNativeLightHeight!=fieldHeight || CachedNativeLight.size()!=nativeLight.size() || !std::equal(nativeLight.begin(),nativeLight.end(),CachedNativeLight.begin())) {
        if(!std::all_of(nativeLight.begin(),nativeLight.end(),[](float value){return std::isfinite(value) && value>=0 && value<=15;}))return Fail("Invalid native light intensity");
        if(!UploadTexture(fieldWidth,fieldHeight,DXGI_FORMAT_R32_FLOAT,nativeLight.data(),fieldWidth*4,NativeLightResource))return false;
        CachedNativeLight.assign(nativeLight.begin(),nativeLight.end());CachedNativeLightWidth=fieldWidth;CachedNativeLightHeight=fieldHeight;
    }
    FrameSmoothLighting=true;return true;
#else
    (void)palette;(void)nativeLight;(void)fieldWidth;(void)fieldHeight;return false;
#endif
}

bool TownGpuSetSpritePointLighting(std::span<const TownPointLight> lights,
    std::span<const TownLightOccluder> occluders)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed || !GeometryCommands.empty())
		return Fail("GPU sprite point field must be set before geometry in an active frame");
	if (!ValidateTownSpritePointLighting(lights, occluders))
		return Fail("Invalid GPU sprite point lights, shells or apertures");
	if (lights.empty()) {
		FrameSpritePointLighting = false;
		return true;
	}
	SpritePointConstants constants {};
	constants.counts.x = static_cast<uint32_t>(lights.size());
	constants.counts.y = static_cast<uint32_t>(occluders.size());
	for (size_t i = 0; i < lights.size(); ++i) {
		const TownPointLight &light = lights[i];
		constants.positionRadius[i] = { light.position.x, light.position.height, light.position.z, light.radius };
		constants.colorIntensity[i] = { light.color.red, light.color.green, light.color.blue, light.intensity };
	}
	for (size_t i = 0; i < occluders.size(); ++i) {
		const TownLightOccluder &occluder = occluders[i];
		constants.occluderMinimum[i] = { occluder.minimum.x, occluder.minimum.height, occluder.minimum.z, static_cast<float>(constants.counts.z) };
		constants.occluderMaximum[i] = { occluder.maximum.x, occluder.maximum.height, occluder.maximum.z, static_cast<float>(occluder.apertures.size()) };
		for (const TownLightAperture &aperture : occluder.apertures) {
			const size_t index = constants.counts.z++;
			constants.apertureBounds[index] = { aperture.minU, aperture.maxU, aperture.minV, aperture.maxV };
			constants.aperturePlanes[index] = { static_cast<float>(aperture.plane), aperture.coordinate, static_cast<float>(aperture.polygonSides), 0 };
		}
	}
	if (!SpritePointConstantBuffer) {
		D3D11_BUFFER_DESC description {};
		description.ByteWidth = sizeof(SpritePointConstants);
		description.Usage = D3D11_USAGE_DYNAMIC;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		const HRESULT result = Device->CreateBuffer(&description, nullptr, SpritePointConstantBuffer.put());
		if (FAILED(result))
			return Fail("Create GPU sprite point constant buffer", result);
	}
	D3D11_MAPPED_SUBRESOURCE mapped {};
	const HRESULT result = Context->Map(SpritePointConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(result))
		return Fail("Map GPU sprite point constant buffer", result);
	std::memcpy(mapped.pData, &constants, sizeof(constants));
	Context->Unmap(SpritePointConstantBuffer.get(), 0);
	FrameSpritePointLighting = true;
	return true;
#else
	(void)lights;
	(void)occluders;
	return false;
#endif
}

bool TownGpuSetFireVolumes(const TownCameraFrame &camera, unsigned sampleFactor,
    double seconds, std::span<const TownGpuFireVolumeSource> sources,
    std::span<const uint32_t> palette256Argb, std::span<const uint32_t> toneRamp256Argb)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed || !GeometryCommands.empty())
		return RefuseFireVolume("Fire volumes require preflight before geometry in a valid frame");
	try {
		return PrepareFireVolumes(camera, sampleFactor, seconds, sources, palette256Argb, toneRamp256Argb);
	} catch (const std::bad_alloc &) {
		ClearFireVolumeFrame(); return RefuseFireVolume("Fire volume preflight allocation failed");
	}
#else
	(void)camera; (void)sampleFactor; (void)seconds; (void)sources; (void)palette256Argb; (void)toneRamp256Argb;
	FireVolumeStats.failure = "D3D11 fire volumes are available only on Windows";
	return false;
#endif
}

const TownGpuFireVolumeStats &GetTownGpuFireVolumeStats()
{
	return FireVolumeStats;
}

void SetTownGpuFireVolumeFaultForDiagnostics(TownGpuFireVolumeFault fault)
{
	FireVolumeFault = fault;
}

bool TownGpuSetTownPointGeometryLighting(const TownGeometricPointShadowView &field,
    std::span<const TownPointLight> lights, bool shadowsEnabled)
{
#ifdef _WIN32
 return SetTownPointGeometryLighting(field,lights,shadowsEnabled);
#else
 (void)field;(void)lights;(void)shadowsEnabled;return false;
#endif
}

bool TownGpuSetFirePointLighting(const TownPointShadowMaps &maps,
    std::span<const TownPointLight> currentFlickeredLights, bool shadowsEnabled)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed || !GeometryCommands.empty())
		return Fail("GPU fire point field must precede geometry in an active frame");
	ClearFirePointFrameBinding();
	if (maps.lightCount == 0 && currentFlickeredLights.empty() && maps.depth.empty())
		return true;
	if (!maps.stats.ready || maps.lightCount == 0 || maps.lightCount > TownMaxFireShadowLights
	    || currentFlickeredLights.size() != maps.lightCount || maps.resolution != TownFireShadowResolution
	    || maps.identity == 0 || maps.revision == 0
	    || maps.depth.size() != maps.lightCount * 6 * TownFireShadowResolution * TownFireShadowResolution)
		return Fail("Invalid GPU fire point field identity, count or dimensions");
	const auto validLight = [](const TownPointLight &light) {
		return Finite(light.position) && std::abs(light.position.x) <= 4096
		    && std::abs(light.position.height) <= 4096 && std::abs(light.position.z) <= 4096
		    && Finite(light.radius) && light.radius > TownFireShadowNear && light.radius <= 16
		    && Finite(light.intensity) && light.intensity >= 0 && light.intensity <= TownMaxPointLightIntensity
		    && Finite(light.color.red) && light.color.red >= 0 && light.color.red <= 16
		    && Finite(light.color.green) && light.color.green >= 0 && light.color.green <= 16
		    && Finite(light.color.blue) && light.color.blue >= 0 && light.color.blue <= 16;
	};
	FirePointConstants constants {};
	constants.counts.x = static_cast<uint32_t>(maps.lightCount);
	constants.counts.y = shadowsEnabled ? 1 : 0;
	constants.counts.z = static_cast<uint32_t>(maps.resolution);
	for (size_t i = 0; i < maps.lightCount; ++i) {
		const TownPointLight &source = maps.lights[i];
		const TownPointLight &light = currentFlickeredLights[i];
		if (!validLight(source) || !validLight(light)
		    || light.position.x != source.position.x || light.position.height != source.position.height
		    || light.position.z != source.position.z || light.radius != source.radius)
			return Fail("GPU fire point source differs from its shadow-map pose");
		constants.positionRadius[i] = { light.position.x, light.position.height, light.position.z, light.radius };
		constants.colorIntensity[i] = { light.color.red, light.color.green, light.color.blue, light.intensity };
	}
	if (!std::all_of(maps.depth.begin(), maps.depth.end(), [](float value) {
		return value > 0 && (std::isfinite(value) || value == std::numeric_limits<float>::infinity());
	}))
		return Fail("GPU fire point shadow depths must be positive finite or positive infinity");
	const uint64_t fingerprint = PayloadFingerprint(std::span<const float>(maps.depth));
	const bool cacheHit = FireShadowResource && FireShadowKey == maps.identity && FireShadowRevision == maps.revision
	    && FireShadowResolution == maps.resolution && FireShadowLightCount == maps.lightCount;
	if (cacheHit) {
		if (FireShadowFingerprint != fingerprint
		    || std::memcmp(&FireShadowSourcePositions, &constants.positionRadius, sizeof(FireShadowSourcePositions)) != 0)
			return Fail("Immutable GPU fire point shadow identity changed without a revision");
	} else {
		const size_t bytes = maps.depth.size() * sizeof(float);
		if (Status.fireShadowUploadedBytes > std::numeric_limits<size_t>::max() - bytes)
			return Fail("GPU fire point shadow upload counter exceeded", S_OK, TownGpuFailureKind::Capacity);
		ComOwner<ID3D11ShaderResourceView> uploaded;
		if (!UploadTexture(maps.resolution, static_cast<int>(maps.lightCount * 6 * maps.resolution),
		        DXGI_FORMAT_R32_FLOAT, maps.depth.data(), maps.resolution * sizeof(float), uploaded))
			return false;
		FireShadowResource = std::move(uploaded);
		FireShadowKey = maps.identity;
		FireShadowRevision = maps.revision;
		FireShadowFingerprint = fingerprint;
		FireShadowResolution = maps.resolution;
		FireShadowLightCount = maps.lightCount;
		FireShadowSourcePositions = constants.positionRadius;
		Status.fireShadowUploadedBytes += bytes;
		Status.fireShadowBytes = bytes;
	}
	if (!FirePointConstantBuffer) {
		D3D11_BUFFER_DESC description {};
		description.ByteWidth = sizeof(FirePointConstants);
		description.Usage = D3D11_USAGE_DYNAMIC;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		const HRESULT result = Device->CreateBuffer(&description, nullptr, FirePointConstantBuffer.put());
		if (FAILED(result)) return Fail("Create GPU fire point constant buffer", result);
	}
	D3D11_MAPPED_SUBRESOURCE mapped {};
	const HRESULT result = Context->Map(FirePointConstantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(result)) return Fail("Map GPU fire point constant buffer", result);
	std::memcpy(mapped.pData, &constants, sizeof(constants));
	Context->Unmap(FirePointConstantBuffer.get(), 0);
	FrameFirePointLighting = true;
	Status.fireLightCount = maps.lightCount;
	return true;
#else
	(void)maps;
	(void)currentFlickeredLights;
	(void)shadowsEnabled;
	return false;
#endif
}

bool TownGpuSubmitProjectedTriangle(const std::array<TownGpuVertex, 3> &vertices,
    const TownGpuTexture &texture, const TownGpuMaterial &material, uint32_t pickId)
{
#ifdef _WIN32
	if (!FrameActive || FrameFailed)
		return Fail("No valid GPU frame is recording");
	if (FrameViewModelStarted && !material.viewModel)
		return Fail("GPU world geometry cannot follow the final viewmodel pass");
	if (material.viewModel && material.paletteBlend)
		return Fail("GPU viewmodel cannot blend a native palette", S_OK, TownGpuFailureKind::Unsupported);
	if (!material.paletteBlend && !material.viewModel && FramePaletteOverlayStarted)
		return Fail("Opaque GPU geometry must precede native palette overlays");
	if (material.paletteBlend && !FramePaletteBlendReady)
		return Fail("GPU native palette overlay requires a frame blend table");
	if (Vertices.size() >= MaxProjectedTriangles * 3)
		return Fail("GPU projected vertex-stream budget exceeded", S_OK, TownGpuFailureKind::Capacity);
	if (Status.submittedTriangles == std::numeric_limits<size_t>::max())
		return Fail("GPU submitted-triangle counter overflow", S_OK, TownGpuFailureKind::Capacity);
	bool colored = material.absoluteDisplayRgb;
	for (const TownGpuVertex &vertex : vertices) {
		if (!Finite(vertex.x) || !Finite(vertex.y) || !Finite(vertex.depth)
		    || vertex.depth < FrameProjection.nearClip || vertex.depth > FrameProjection.farClip
		    || !Finite(vertex.clipW) || vertex.clipW != (FrameProjection.perspective ? vertex.depth : 1)
		    || !Finite((vertex.x * 2 / Width - 1) * vertex.clipW) || !Finite((1 - vertex.y * 2 / Height) * vertex.clipW)
		    || !Finite(vertex.u) || !Finite(vertex.v)
		    || !std::all_of(vertex.world.begin(), vertex.world.end(), [](float value) { return Finite(value); })
		    || !std::all_of(vertex.authoredColor.begin(), vertex.authoredColor.end(), [](float value) { return Finite(value) && value >= 0 && value <= 1; }))
			return Fail("GPU projected vertices must be finite, depth-clipped and use clipW=depth (perspective) or 1 (orthographic)");
		colored = colored || vertex.authoredColor != std::array<float, 4> { 1, 1, 1, 1 };
	}
	if (colored && !FrameSmoothLighting && !(texture.rgbaLightLutEnabled || !texture.rgbaLightLut.empty()))
		return Fail("GPU authored color requires a frame RGB palette or paired RGB LUT");
	if ((colored || !AuthoredColors.empty()) && (Vertices.size() + 3) * sizeof(std::array<float, 4>) > MaxAuthoredColorBytes)
		return Fail("GPU authored-color stream budget exceeded", S_OK, TownGpuFailureKind::Capacity);
	CachedTexture *uploaded = GetTexture(texture, material.lighting);
	if (uploaded == nullptr)
		return false;
	Constants constants {};
	if (!MakeConstants(material, *uploaded, constants, true))
		return false;
	if (colored) constants.flags.w |= 512;
	D3D11_BOX paletteCopyBox {};
	size_t paletteCopyBytes = 0;
	if (material.paletteBlend) {
		// Admit the same conservative R8 region that EndFrame will copy. The
		// clamped box is nonempty and bounded by the validated frame dimensions.
		// This POD-only preflight allocates nothing; subtraction guards the sum.
		paletteCopyBox = PaletteSnapshotBox(vertices);
		paletteCopyBytes = static_cast<size_t>(paletteCopyBox.right - paletteCopyBox.left)
		    * (paletteCopyBox.bottom - paletteCopyBox.top);
		if (Status.paletteBlendTriangles >= MaxPaletteBlendTriangles
		    || paletteCopyBytes > MaxPaletteBlendCopyBytes
		    || Status.paletteBlendAdmittedBytes > MaxPaletteBlendCopyBytes - paletteCopyBytes)
			return Fail("GPU native palette overlay triangle/copy budget exceeded", S_OK, TownGpuFailureKind::Capacity);
	}
	const UINT first = static_cast<UINT>(Vertices.size());
	if (colored && AuthoredColors.empty()) AuthoredColors.resize(Vertices.size(), { 1, 1, 1, 1 });
	for (const TownGpuVertex &vertex : vertices) {
		if (colored || !AuthoredColors.empty()) AuthoredColors.push_back(vertex.authoredColor);
		Vertices.push_back({ { vertex.x, vertex.y, vertex.depth, vertex.u, vertex.v, vertex.world, vertex.clipW }, pickId,
		    { material.normal[0], material.normal[1], material.normal[2], std::clamp(material.diffuse, 0.0F, 1.0F) },
		    { material.shadowBias, material.shadowSlopeU, material.shadowSlopeV,
		        static_cast<float>(std::clamp(material.shade, 0, 3) + (material.receivesShadow ? 4 : 0)) },
		    { static_cast<float>(material.fallbackPaletteIndex), static_cast<float>(std::clamp(material.fallbackShade, 0, 3)) } });
	}
	FrameRgbaLut = FrameRgbaLut || (uploaded->lut != nullptr && uploaded->lut->hasRgba);
	if (!material.paletteBlend && !GeometryCommands.empty() && !GeometryCommands.back().mesh && !Batches.empty()
	    && !Batches.back().paletteBlend && Batches.back().texture == uploaded && Batches.back().preservePicking == material.preservePicking
	    && Batches.back().viewModel == material.viewModel
	    && std::memcmp(&Batches.back().constants, &constants, sizeof(constants)) == 0)
		Batches.back().count += 3;
	else {
		GeometryCommands.push_back({ false, Batches.size() });
		Batches.push_back({ uploaded, constants, first, 3, material.preservePicking, material.paletteBlend, material.viewModel,
		    paletteCopyBox });
	}
	if (material.paletteBlend) {
		FramePaletteOverlayStarted = true;
		++Status.paletteBlendTriangles;
		Status.paletteBlendAdmittedBytes += paletteCopyBytes;
	}
	FrameViewModelStarted = FrameViewModelStarted || material.viewModel;
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
	Status.townPointFrameSerial = 0;
	FireVolumeStats.applied = false;
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
	WorldTimingScope worldTiming;
	{
	diag::StageScope diagnosticCommands(diag::SessionLogger(), diag::Stage::WorldSubmit, diag::SessionFrame(), diag::SampleSessionFrame());
	const auto commandStart = Clock::now();
	const float zero[] { 0, 0, 0, 0 };
	const float infinity[] { std::numeric_limits<float>::infinity(), 0, 0, 0 };
	Context->ClearRenderTargetView(Targets[0].view.get(), zero);
	Context->ClearRenderTargetView(Targets[1].view.get(), zero);
	Context->ClearRenderTargetView(Targets[2].view.get(), infinity);
	Context->ClearRenderTargetView(Targets[3].view.get(), zero);
	Context->ClearRenderTargetView(Targets[4].view.get(), zero);
	Context->ClearDepthStencilView(HardwareDepthView.get(), D3D11_CLEAR_DEPTH, 1, 0);
	ID3D11RenderTargetView *targets[] { Targets[0].view.get(), Targets[1].view.get(), Targets[2].view.get(), Targets[3].view.get(), Targets[4].view.get() };
	Context->OMSetRenderTargets(5, targets, HardwareDepthView.get());
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
	ID3D11Buffer *spritePointBuffer = FrameSpritePointLighting ? SpritePointConstantBuffer.get() : nullptr;
	Context->PSSetConstantBuffers(2, 1, &spritePointBuffer);
	ID3D11Buffer *firePointBuffer = FrameFirePointLighting ? FirePointConstantBuffer.get() : nullptr;
	Context->PSSetConstantBuffers(3, 1, &firePointBuffer);
	BindTownPointGeometryResources();
	if (!Vertices.empty()) {
		if (VertexCapacity < Vertices.size()) {
			const size_t capacity = std::min<size_t>(MaxProjectedTriangles * 3, std::max(Vertices.size(), VertexCapacity * 2));
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
		MeshStats.projectedUploadBytes = Vertices.size() * sizeof(GpuVertex);
	}
	if (!AuthoredColors.empty()) {
		if (AuthoredColors.size() != Vertices.size() || AuthoredColors.size() * sizeof(AuthoredColors[0]) > MaxAuthoredColorBytes)
			return Fail("GPU authored-color recording is incomplete", S_OK, TownGpuFailureKind::Capacity);
		if (AuthoredColorCapacity < AuthoredColors.size()) {
			const size_t capacity = std::min<size_t>(MaxProjectedTriangles * 3, std::max(AuthoredColors.size(), AuthoredColorCapacity * 2));
			D3D11_BUFFER_DESC description {};
			description.ByteWidth = static_cast<UINT>(capacity * sizeof(AuthoredColors[0]));
			description.Usage = D3D11_USAGE_DYNAMIC;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			AuthoredColorResource.reset();
			HRESULT result = Device->CreateBuffer(&description, nullptr, AuthoredColorBuffer.put());
			if (FAILED(result)) return Fail("Create authored-color stream", result);
			D3D11_SHADER_RESOURCE_VIEW_DESC view {};
			view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			view.Buffer.NumElements = static_cast<UINT>(capacity);
			result = Device->CreateShaderResourceView(AuthoredColorBuffer.get(), &view, AuthoredColorResource.put());
			if (FAILED(result)) return Fail("Create authored-color stream view", result);
			AuthoredColorCapacity = capacity;
		}
		D3D11_MAPPED_SUBRESOURCE mapped {};
		const HRESULT result = Context->Map(AuthoredColorBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(result)) return Fail("Map authored-color stream", result);
		std::memcpy(mapped.pData, AuthoredColors.data(), AuthoredColors.size() * sizeof(AuthoredColors[0]));
		Context->Unmap(AuthoredColorBuffer.get(), 0);
		Status.uploadedAuthoredColorBytes = AuthoredColors.size() * sizeof(AuthoredColors[0]);
	}
	worldTiming.StartCommands();
	bool viewModelDepthReset = false;
	for (const GeometryCommand &command : GeometryCommands) {
		if (command.mesh) {
			if (!DrawMeshBatch(MeshBatches[command.batch]))
				return false;
			continue;
		}
		const Batch &batch = Batches[command.batch];
		if (batch.viewModel && !viewModelDepthReset) {
			// World ownership/depth MRTs remain bound but write-masked below.
			// Only hardware depth changes owner for this terminal visual phase.
			Context->ClearDepthStencilView(HardwareDepthView.get(), D3D11_CLEAR_DEPTH, 1, 0);
			viewModelDepthReset = true;
		}
		if (batch.paletteBlend) {
			// Never read/write the same resource at once. Each triangle observes
			// all preceding overlays, including ones with the same material.
			ID3D11ShaderResourceView *emptySnapshot = nullptr;
			Context->PSSetShaderResources(4, 1, &emptySnapshot);
			Context->OMSetRenderTargets(0, nullptr, nullptr);
			// PS reads prior color only at this triangle's covered raster pixels.
			// Preserve triangle order/one snapshot per draw; only reduce payload.
			const D3D11_BOX &box = batch.paletteCopyBox;
			if (box.left == 0 && box.top == 0 && box.right == static_cast<UINT>(Width)
			    && box.bottom == static_cast<UINT>(Height))
				Context->CopyResource(PriorIndexedColor.get(), Targets[0].texture.get());
			else
				Context->CopySubresourceRegion(PriorIndexedColor.get(), 0, box.left, box.top, 0,
				    Targets[0].texture.get(), 0, &box);
			Context->OMSetRenderTargets(5, targets, HardwareDepthView.get());
			Status.paletteBlendCopyBytes += static_cast<size_t>(box.right - box.left) * (box.bottom - box.top);
		}
		Constants drawConstants = batch.constants;
		const bool colored = (batch.constants.flags.w & 512) != 0;
		if (colored) drawConstants.target.z = static_cast<float>(batch.first);
		if (!BindGeometryConstants(drawConstants))
			return false;
		Context->IASetInputLayout(InputLayout.get());
		Context->VSSetShader(VertexShader.get(), nullptr, 0);
		ID3D11ShaderResourceView *authoredColor = AuthoredColors.empty() ? nullptr : AuthoredColorResource.get();
		Context->VSSetShaderResources(10, 1, &authoredColor);
		ID3D11Buffer *vertexBuffer = VertexBuffer.get();
		const UINT stride = sizeof(GpuVertex);
		// Optional attributes use an explicit global base and a zero-based draw.
		// This keeps SV_VertexID independent of StartVertexLocation conventions.
		const UINT offset = colored ? batch.first * sizeof(GpuVertex) : 0;
		Context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
		BindGeometryMaterial(*batch.texture, batch.preservePicking, batch.paletteBlend);
		if (batch.viewModel)
			Context->OMSetBlendState(PaletteBlendWriteState.get(), nullptr, 0xFFFFFFFFU);
		Context->Draw(batch.count, colored ? 0 : batch.first);
		++Status.drawCalls;
	}
	MeshStats.commandMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - commandStart).count();
	}
	if (!DispatchFireVolumes()) return false;
	diag::StageScope diagnosticReadback(diag::SessionLogger(), diag::Stage::Readback, diag::SessionFrame(), diag::SampleSessionFrame());
	ID3D11ShaderResourceView *emptyResources[13] {};
	Context->PSSetShaderResources(0, 13, emptyResources);
	Context->VSSetShaderResources(10, 1, emptyResources);
	Context->OMSetRenderTargets(0, nullptr, nullptr);
	for (Target &target : Targets)
		Context->CopyResource(target.staging.get(), target.texture.get());
	worldTiming.FinishCommands();
	const auto readbackStart = Clock::now();
	// One-shot diagnostics only; null in every ordinary frame. The frontend
	// allocation guard owns abort/retry if this callback throws.
	if (const auto beforeReadback = std::exchange(BeforeReadbackForDiagnostics, nullptr))
		beforeReadback();
	if (!ReadFireVolumeStatus()) return false;
	TownGpuFrame complete;
	complete.width = Width;
	complete.height = Height;
	const size_t count = static_cast<size_t>(Width) * Height;
	complete.indexed.resize(count);
	complete.pickIds.resize(count);
	complete.depth.resize(count);
	complete.viewModelMask.resize(count);
	if(FrameSmoothLighting || FrameRgbaLut || !AuthoredColors.empty() || FrameFireVolumeCount != 0 || FrameTownPointGeometryLighting) complete.rgba.resize(count);
	if (!Readback(Targets[0], complete.indexed) || !Readback(Targets[1], complete.pickIds) || !Readback(Targets[2], complete.depth) || (!complete.rgba.empty() && !Readback(Targets[3], complete.rgba)) || !Readback(Targets[4], complete.viewModelMask))
		return false;
	if (!std::all_of(complete.viewModelMask.begin(), complete.viewModelMask.end(), [](uint8_t value) { return value <= 1; }))
		return Fail("Invalid GPU viewmodel coverage readback");
	const HRESULT removed = Device->GetDeviceRemovedReason();
	if (FAILED(removed))
		return Fail("D3D11 device removed", removed);
	UpdateFireVolumeTiming();
	Status.readbackMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - readbackStart).count();
	Status.frameMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - FrameStart).count();
	UpdateTextureStatus();
	Status.frameSucceeded = true;
	worldTiming.Publish();
	complete.townPointLightingApplied = FrameTownPointGeometryLighting && FrameTownPointReceiverSubmitted;
	if (complete.townPointLightingApplied) {
		if (TownPointPublicationSerial == std::numeric_limits<uint64_t>::max())
			return Fail("Town point frame publication serial exhausted", S_OK, TownGpuFailureKind::Capacity);
		complete.townPointFrameSerial = ++TownPointPublicationSerial;
		Status.townPointFrameSerial = complete.townPointFrameSerial;
		complete.townPointLightCount = static_cast<uint32_t>(TownPointGeometrySourceCount);
		complete.townPointFieldIdentity = TownPointGeometryKey;
		complete.townPointFieldRevision = TownPointGeometryRevision;
	}
	complete.fireVolumesApplied = FrameFireVolumeCount != 0;
	complete.fireVolumeSources = static_cast<uint32_t>(FrameFireVolumeCount);
	FireVolumeStats.applied = complete.fireVolumesApplied;
	output = std::move(complete);
	return true;
#else
	return false;
#endif
}

void SetTownGpuBeforeReadbackForDiagnostics(void (*callback)()) noexcept
{
	BeforeReadbackForDiagnostics = callback;
}

void ResetTownGpuResources()
{
	WorldFrameTiming = {};
	BeforeReadbackForDiagnostics = nullptr;
	FireVolumeFault = TownGpuFireVolumeFault::None;
#ifdef _WIN32
	ResetWorldTimingQueries();
	WorldTimingUnavailable = false;
	ResetFireVolumeResources();
	FrameActive = false;
	FrameFailed = false;
	FrameSpritePointLighting = false;
	FrameFirePointLighting = false;
	FrameTownPointGeometryLighting = FrameTownPointReceiverSubmitted = false;
	FramePaletteBlendReady = false;
	FramePaletteOverlayStarted = false;
	FrameViewModelStarted = false;
	Batches.clear();
	ResetMeshResources();
	Vertices.clear();
	AuthoredColors.clear();
	FrameShadow = {};
	FrameProjection = {};
	if (Context) {
		Context->ClearState();
		Context->Flush();
	}
	TextureCache.clear();
	TexelCache.clear();
	OpacityCache.clear();
	RgbPaletteResource.reset();NativeLightResource.reset();CachedRgbPalette.clear();CachedNativeLight.clear();FrameSmoothLighting=false;
	FrameRgbaLut = false;
	LightLutCache.clear();
	TextureBytes = 0;
	ShadowResource.reset();
	TownPointGeometryShadowResource.reset();
	TownPointGeometryConstantBuffer.reset();
	TownPointGeometryKey = TownPointGeometryRevision = TownPointGeometryEpoch = TownPointGeometryFingerprint = 0;
	TownPointGeometrySourceCount = 0;
	TownPointGeometrySourcePositions = {};
	FireShadowResource.reset();
	FireShadowKey = FireShadowRevision = FireShadowFingerprint = 0;
	FireShadowResolution = 0;
	FireShadowLightCount = 0;
	FireShadowSourcePositions = {};
	PriorIndexedColorView.reset();
	PriorIndexedColor.reset();
	PaletteBlendResource.reset();
	PaletteBlendKey = PaletteBlendRevision = 0;
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
	PaletteBlendDepthState.reset();
	PaletteBlendWriteState.reset();
	Rasterizer.reset();
	ConstantBuffer.reset();
	SpritePointConstantBuffer.reset();
	FirePointConstantBuffer.reset();
	VertexBuffer.reset();
	AuthoredColorResource.reset();
	AuthoredColorBuffer.reset();
	InputLayout.reset();
	PixelShader.reset();
	VertexShader.reset();
	Context.reset();
	Device.reset();
	Width = Height = 0;
	VertexCapacity = 0;
	AuthoredColorCapacity = 0;
	FrameSerial = 0;
#endif
	Status = {};
#ifdef _WIN32
	// Resource reset invalidates GPU state, not immutable embedded CPU bytecode
	// or its process-session counters. A fresh executable starts these at zero.
	UpdateShaderBytecodeStatus();
#endif
}

const TownGpuStatus &GetTownGpuStatus()
{
	return Status;
}

const TownGpuWorldFrameTiming &GetTownGpuWorldFrameTiming()
{
	return WorldFrameTiming;
}

} // namespace devilution
