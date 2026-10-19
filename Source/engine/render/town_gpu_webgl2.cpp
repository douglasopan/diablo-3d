// Private candidate for Emscripten only. Select instead of town_gpu.cpp, never alongside it.
// Public TownGpu*/TownGpuMesh* contracts remain the native headers and actual caller stream.
// Preparation only: browser shader/link/frame parity and sustained performance are unverified.
#include "engine/render/town_gpu.hpp"
#include "engine/render/town_gpu_mesh.hpp"
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace devilution {
namespace {
using Clock = std::chrono::steady_clock;
using UINT = uint32_t;
constexpr size_t MaxPixels = 4 * 1024 * 1024;
constexpr size_t MaxTextureBytes = 256 * 1024 * 1024;
constexpr size_t MaxMeshBytes = 256 * 1024 * 1024;
constexpr size_t MaxProjectedTriangles = 1024 * 1024;
constexpr size_t MaxPaletteBlendTriangles = 2048;
constexpr size_t MaxPaletteBlendCopyBytes = 512 * 1024 * 1024;
constexpr GLenum DXGI_FORMAT_R32_UINT = GL_R32UI;
constexpr GLenum DXGI_FORMAT_R8_UINT = GL_R8UI;
constexpr GLenum DXGI_FORMAT_R32_FLOAT = GL_R32F;
TownGpuStatus Status;
TownGpuMeshStats MeshStats;
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE Context = 0;
bool ContextNeedsReset = false;
bool ContextAllowsSoftwareDiagnostic = false;
int MaxTextureSize = 0;
void (*BeforeReadbackForDiagnostics)() = nullptr;
bool FrameActive = false, FrameFailed = false;
bool FramePaletteBlendReady = false, FramePaletteOverlayStarted = false;
int Width = 0, Height = 0;
uint64_t FrameSerial = 0;
size_t TextureBytes = 0, MeshBytes = 0;
Clock::time_point FrameStart;
TownGpuProjection FrameProjection;
TownGpuShadow FrameShadow;
TownGpuMeshCamera MeshCamera;
bool MeshCameraReady = false;

bool Fail(const std::string &message, int = 0, TownGpuFailureKind kind = TownGpuFailureKind::InvalidInput)
{
    if (FrameFailed) return false;
    FrameFailed = true;
    Status.frameSucceeded = false;
    Status.failure = message;
    Status.failureKind = kind;
    return false;
}
bool CheckGl(const char *operation)
{
    if (Context == 0 || emscripten_is_webgl_context_lost(Context)) {
        Status.available = false;
        ContextNeedsReset = true;
        return Fail("WebGL2 context lost", 0, TownGpuFailureKind::Device);
    }
    const GLenum error = glGetError();
    return error == GL_NO_ERROR || Fail(std::string(operation) + " GL error " + std::to_string(error), 0,
        error == GL_OUT_OF_MEMORY ? TownGpuFailureKind::Capacity : TownGpuFailureKind::Device);
}
bool CanDeleteGl() {
    // Reset may fail to switch a still-live context. Never delete a private
    // object in SDL's context; destroying our context will release it instead.
    return Context != 0 && !emscripten_is_webgl_context_lost(Context)
        && emscripten_webgl_get_current_context() == Context;
}
class GlTexture {
    GLuint value = 0;
public:
    GlTexture() = default;
    GlTexture(const GlTexture &) = delete;
    GlTexture &operator=(const GlTexture &) = delete;
    GlTexture(GlTexture &&other) noexcept : value(std::exchange(other.value, 0)) {}
    GlTexture &operator=(GlTexture &&other) noexcept { reset(); value = std::exchange(other.value, 0); return *this; }
    ~GlTexture() { reset(); }
    void reset() { if (value && CanDeleteGl()) glDeleteTextures(1, &value); value = 0; }
    GLuint get() const { return value; }
    explicit operator bool() const { return value != 0; }
    GLuint *put() { reset(); return &value; }
};
class GlBuffer {
    GLuint value = 0;
public:
    GlBuffer() = default;
    GlBuffer(const GlBuffer &) = delete;
    GlBuffer &operator=(const GlBuffer &) = delete;
    GlBuffer(GlBuffer &&other) noexcept : value(std::exchange(other.value, 0)) {}
    GlBuffer &operator=(GlBuffer &&other) noexcept { reset(); value = std::exchange(other.value, 0); return *this; }
    ~GlBuffer() { reset(); }
    void reset() { if (value && CanDeleteGl()) glDeleteBuffers(1, &value); value = 0; }
    GLuint get() const { return value; }
    GLuint *put() { reset(); return &value; }
};
struct ContextScope {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE previous = emscripten_webgl_get_current_context();
    bool valid = Context != 0 && !emscripten_is_webgl_context_lost(Context)
        && emscripten_webgl_make_context_current(Context) == EMSCRIPTEN_RESULT_SUCCESS;
    ~ContextScope() { emscripten_webgl_make_context_current(previous); }
    explicit operator bool() const { return valid; }
};
// CPU-only recording still checks device loss on every public submission.
// Binding/restoring SDL is necessary only for actual GL work in cold paths.
bool ContextValidNoSwitch()
{
    return Context != 0 && !emscripten_is_webgl_context_lost(Context);
}
struct Box { UINT left, top, front, right, bottom, back; };
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

struct CachedTexels {
	GlTexture codes;
	GlTexture opacity;
	int width;
	int height;
	uint32_t maximumCode;
	bool hasOpacity;
	size_t bytes;
	uint64_t lastSeenFrame;
};

struct CachedLightLut {
	GlTexture view;
	unsigned lightLevels;
	unsigned codeCount;
	unsigned lutWidth;
	size_t bytes;
	uint64_t lastSeenFrame;
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
	bool operator==(const TextureBindingKey &other) const { return texels == other.texels && lut == other.lut && lightLevels == other.lightLevels && hasLut == other.hasLut; }
};

struct TextureBindingKeyHash {
	size_t operator()(const TextureBindingKey &value) const
	{
		return TextureKeyHash {}(value.texels) ^ (LightLutKeyHash {}(value.lut) << 1) ^ value.lightLevels ^ (value.hasLut ? 0xC2B2AE35U : 0);
	}
};

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
	GlBuffer vertices;
	GlBuffer indices;
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

struct Batch {
    CachedTexture *texture;
    Constants constants;
    UINT first, count;
    bool preservePicking, paletteBlend;
    Box paletteCopyBox;
};
std::vector<GpuVertex> Vertices;
std::vector<Batch> Batches;
std::unordered_map<TextureKey, CachedTexels, TextureKeyHash> TexelCache;
std::unordered_map<LightLutKey, CachedLightLut, LightLutKeyHash> LightLutCache;
std::unordered_map<TextureBindingKey, CachedTexture, TextureBindingKeyHash> TextureCache;
GlTexture ShadowResource, PaletteBlendResource, PriorIndexedColor, DummyInteger, DummyFloat;
uint64_t ShadowKey = 0, ShadowRevision = 0, PaletteBlendKey = 0, PaletteBlendRevision = 0;
int ShadowResolution = 0;
GLuint Framebuffer = 0, HardwareDepth = 0;
std::array<GlTexture, 3> Targets;
GLuint ProjectedProgram = 0, MeshProgram = 0, ProjectedVao = 0, MeshVao = 0;
GlBuffer VertexBuffer, ConstantBuffer, MeshConstantBuffer;
constexpr char ProjectedVertexSource[] = R"glsl(#version 300 es
precision highp float;
precision highp int;
precision highp usampler2D;
precision highp sampler2D;
layout(std140) uniform Frame {
 vec4 target; vec4 projection; uvec4 flags; vec4 shading; vec4 normalAndRed;
 vec4 roomMinimum; vec4 roomMaximum; vec4 parameters;
 vec4 shadowRight; vec4 shadowUp; vec4 shadowLight; vec4 shadowScale;
 vec4 lightPositionRadius[8]; vec4 lightRedIntensity[8];
 vec4 apertureBounds[8]; vec4 aperturePlanes[8];
 vec4 blockerMinimum[31]; vec4 blockerMaximum[31];
};
float saturate(float value) { return clamp(value, 0.0, 1.0); }

 smooth out float viewDepth;
 smooth out vec2 texUv;
 smooth out vec3 worldPosition;
 flat out uint pick;
 flat out vec4 normalDiffuse;
 flat out vec4 shadowParameters;
 flat out vec2 pixelFallback;
 flat out vec2 volume;

layout(location=0) in vec2 position;
layout(location=1) in float depth;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 world;
layout(location=4) in float clipW;
layout(location=5) in uint pickId;
layout(location=6) in vec4 inputNormalDiffuse;
layout(location=7) in vec4 inputShadow;
layout(location=8) in vec2 fallback;
void main() {
 float normalizedDepth = projection.z != 0.0
  ? saturate(projection.y * (depth - projection.x) / ((projection.y - projection.x) * depth))
  : depth / 4096.0;
 gl_Position = vec4((position.x * 2.0 / target.x - 1.0) * clipW,
  (1.0 - position.y * 2.0 / target.y) * clipW,
  (normalizedDepth * 2.0 - 1.0) * clipW, clipW);
 viewDepth = depth; texUv = uv; worldPosition = world; pick = pickId;
 normalDiffuse = inputNormalDiffuse; shadowParameters = inputShadow;
 pixelFallback = fallback; volume = vec2(-1.0, 1.0);
}
)glsl";
constexpr char MeshVertexSource[] = R"glsl(#version 300 es
precision highp float;
precision highp int;
precision highp usampler2D;
precision highp sampler2D;
layout(std140) uniform Frame {
 vec4 target; vec4 projection; uvec4 flags; vec4 shading; vec4 normalAndRed;
 vec4 roomMinimum; vec4 roomMaximum; vec4 parameters;
 vec4 shadowRight; vec4 shadowUp; vec4 shadowLight; vec4 shadowScale;
 vec4 lightPositionRadius[8]; vec4 lightRedIntensity[8];
 vec4 apertureBounds[8]; vec4 aperturePlanes[8];
 vec4 blockerMinimum[31]; vec4 blockerMaximum[31];
};
float saturate(float value) { return clamp(value, 0.0, 1.0); }

 smooth out float viewDepth;
 smooth out vec2 texUv;
 smooth out vec3 worldPosition;
 flat out uint pick;
 flat out vec4 normalDiffuse;
 flat out vec4 shadowParameters;
 flat out vec2 pixelFallback;
 flat out vec2 volume;

layout(std140) uniform Mesh {
 vec4 worldToView[3]; vec4 localToWorld[3]; vec4 normalToWorld[3];
 vec4 camera; vec4 eye; vec4 towardViewer; vec4 lightAndBias;
 vec4 materialNormalDiffuse; vec4 materialReceiver; vec4 fallback;
 vec4 volumeParameters; uvec4 control;
};
layout(location=0) in vec3 position;
layout(location=2) in vec2 uv;
layout(location=9) in vec3 normal;
layout(location=10) in uint volumeFace;
void main() {
 vec4 local = vec4(position, 1.0);
 vec3 world = vec3(dot(localToWorld[0],local),dot(localToWorld[1],local),dot(localToWorld[2],local));
 vec4 world4 = vec4(world,1.0);
 vec3 view = vec3(dot(worldToView[0],world4),dot(worldToView[1],world4),dot(worldToView[2],world4));
 if (volumeParameters.y != 0.0) {
  // ES 3.0 has no HLSL precise equivalent; keep native arithmetic ordering.
  // Browser/native parity is a gate, never assumed from this expression.
  vec3 relative = world - eye.xyz;
  relative.y = relative.y * camera.w;
  view.x = (relative.x*worldToView[0].x + relative.y*worldToView[0].y) + relative.z*worldToView[0].z;
  view.y = (relative.x*worldToView[1].x + relative.y*worldToView[1].y) + relative.z*worldToView[1].z;
  view.z = (relative.x*worldToView[2].x + relative.y*worldToView[2].y) + relative.z*worldToView[2].z;
 }
 float clipW = projection.z != 0.0 ? view.z : 1.0;
 float clipZ = projection.z != 0.0 ? projection.y/(projection.y-projection.x)*(view.z-projection.x) : view.z/4096.0;
 gl_Position = vec4(view.x*(2.0*camera.x/target.x)+(2.0*camera.y/target.x-1.0)*clipW,
  view.y*(2.0*camera.x/target.y)+(1.0-2.0*camera.z/target.y)*clipW,2.0*clipZ-clipW,clipW);
 if (volumeParameters.y != 0.0 && (projection.z == 0.0 || view.z != 0.0)) {
  float reciprocalW = projection.z != 0.0 ? 1.0/view.z : 1.0;
  float screenX = camera.y + camera.x*view.x*reciprocalW;
  float screenY = camera.z - camera.x*view.y*reciprocalW;
  float normalizedDepth = projection.z != 0.0
   ? projection.y*(view.z-projection.x)/((projection.y-projection.x)*view.z) : view.z/4096.0;
  gl_Position = vec4((screenX*2.0/target.x-1.0)*clipW,(1.0-screenY*2.0/target.y)*clipW,
   (normalizedDepth*2.0-1.0)*clipW,clipW);
 }
 viewDepth=view.z; texUv=uv; worldPosition=world; pick=control.x;
 normalDiffuse=materialNormalDiffuse; shadowParameters=materialReceiver;
 pixelFallback=fallback.xy; volume=vec2(-1.0,1.0);
 if (control.y != 0u) {
  vec3 rawNormal=vec3(dot(normalToWorld[0].xyz,normal),dot(normalToWorld[1].xyz,normal),dot(normalToWorld[2].xyz,normal));
  float normalScale=max(max(abs(rawNormal.x),abs(rawNormal.y)),abs(rawNormal.z));
  vec3 n=normalize(rawNormal/max(normalScale,1.0e-30));
  vec3 toward=projection.z!=0.0 ? eye.xyz-world : towardViewer.xyz;
  if (control.z!=0u && dot(n,toward)<0.0) n=-n;
  float facingLight=dot(n,lightAndBias.xyz);
  normalDiffuse=vec4(n,saturate(facingLight));
  shadowParameters.x=lightAndBias.w+fallback.z*(1.0-abs(facingLight));
  shadowParameters.yz=abs(facingLight)>0.03 ? -vec2(dot(n,shadowRight.xyz),dot(n,shadowUp.xyz))/facingLight : vec2(0.0);
  bool receives=flags.x==2u ? facingLight>0.0 : facingLight>0.02;
  if (!receives) shadowParameters.w=float(uint(materialReceiver.w)&3u);
  if (control.w!=0u) {
   volume.y=dot(n,eye.xyz-world);
   float legacyLight=(n.x+n.y+n.z)/1.7320508;
   uint legacyShade=uint(clamp(volumeParameters.x+(legacyLight < -0.1 ? 2.0 : (legacyLight < 0.4 ? 1.0 : 0.0)),0.0,3.0));
   bool spriteFront=(volumeFace&256u)!=0u; uint palette=volumeFace&255u;
   uint shade=spriteFront ? uint(volumeParameters.x) : legacyShade;
   shadowParameters.w=float((uint(shadowParameters.w)&4u)|shade);
   pixelFallback=vec2(float(palette),float(legacyShade)); volume.x=spriteFront ? -1.0 : float(palette);
  }
 }
}
)glsl";
constexpr char PixelSource[] = R"glsl(#version 300 es
precision highp float;
precision highp int;
precision highp usampler2D;
precision highp sampler2D;
layout(std140) uniform Frame {
 vec4 target; vec4 projection; uvec4 flags; vec4 shading; vec4 normalAndRed;
 vec4 roomMinimum; vec4 roomMaximum; vec4 parameters;
 vec4 shadowRight; vec4 shadowUp; vec4 shadowLight; vec4 shadowScale;
 vec4 lightPositionRadius[8]; vec4 lightRedIntensity[8];
 vec4 apertureBounds[8]; vec4 aperturePlanes[8];
 vec4 blockerMinimum[31]; vec4 blockerMaximum[31];
};
float saturate(float value) { return clamp(value, 0.0, 1.0); }

 smooth in float viewDepth;
 smooth in vec2 texUv;
 smooth in vec3 worldPosition;
 flat in uint pick;
 flat in vec4 normalDiffuse;
 flat in vec4 shadowParameters;
 flat in vec2 pixelFallback;
 flat in vec2 volume;

uniform highp usampler2D codes;
uniform highp usampler2D opacity;
uniform highp usampler2D lightLut;
uniform highp sampler2D shadowDepth;
uniform highp usampler2D priorIndexedColor;
uniform highp usampler2D paletteBlendLut;
layout(location=0) out highp uint outputColor;
layout(location=1) out highp uint outputPick;
layout(location=2) out highp float outputDepth;
float directionalShadow(vec3 world, vec4 receiver) {
 if ((flags.w&8u)==0u || parameters.w<1.0 || ((uint(receiver.w)&4u)==0u)) return 0.0;
 vec2 texel=vec2((dot(world,shadowRight.xyz)-shadowRight.w)/shadowLight.w,
  (dot(world,shadowUp.xyz)-shadowUp.w)/shadowScale.x);
 int resolution=int(parameters.w);
 if (any(lessThan(texel,vec2(0.0))) || any(greaterThanEqual(texel,vec2(float(resolution))))) return 0.0;
 ivec2 center=ivec2(texel); int radius=int(shadowScale.y); float occluded=0.0,taps=0.0;
 float receiverDepth=dot(world,shadowLight.xyz);
 for (int dv=-radius;dv<=radius;++dv) for (int du=-radius;du<=radius;++du) {
  taps+=1.0; ivec2 samplePoint=center+ivec2(du,dv);
  if (any(lessThan(samplePoint,ivec2(0))) || any(greaterThanEqual(samplePoint,ivec2(resolution)))) continue;
  float plane=receiverDepth+(float(samplePoint.x)+0.5-texel.x)*shadowLight.w*receiver.y
   +(float(samplePoint.y)+0.5-texel.y)*shadowScale.x*receiver.z;
  occluded+=texelFetch(shadowDepth,samplePoint,0).r > plane+receiver.x ? 1.0 : 0.0;
 }
 return occluded/max(taps,1.0);
}
bool openingAt(vec3 hit,int axis,float boundary) {
 vec2 uv=vec2(axis==0 ? hit.z : hit.x,axis==1 ? hit.z : hit.y);
 for (int i=0;i<int(parameters.z);++i) {
  vec4 plane=aperturePlanes[i],bounds=apertureBounds[i];
  if (int(plane.x)!=axis || abs(plane.y-boundary)>0.0001) continue;
  if (uv.x<=bounds.x+0.0001 || uv.x>=bounds.y-0.0001 || uv.y<=bounds.z+0.0001 || uv.y>=bounds.w-0.0001) continue;
  int sides=int(plane.z); bool inside=true;
  if (sides!=0) {
   vec2 normalized=(uv-vec2(bounds.x+bounds.y,bounds.z+bounds.w)*0.5)/(vec2(bounds.y-bounds.x,bounds.w-bounds.z)*0.5);
   float sideDistance=cos(3.14159265358979323846/float(sides));
   for (int side=0;side<sides;++side) {
    float angle=6.28318530717958647692*(float(side)+0.5)/float(sides);
    if (dot(normalized,vec2(cos(angle),sin(angle)))>=sideDistance-0.0001) inside=false;
   }
  }
  if (inside) return true;
 }
 return false;
}
bool crossingOpen(vec3 origin,vec3 delta,float t) {
 if (t<=0.00001 || t>=0.99999) return true;
 vec3 hit=origin+delta*t;
 for (int axis=0;axis<3;++axis) {
  if (abs(delta[axis])<=0.000001) continue;
  if (abs(hit[axis]-roomMinimum[axis])<=0.0001 && !openingAt(hit,axis,roomMinimum[axis])) return false;
  if (abs(hit[axis]-roomMaximum[axis])<=0.0001 && !openingAt(hit,axis,roomMaximum[axis])) return false;
 }
 return true;
}
bool visibleThroughRoom(vec3 origin,vec3 receiver) {
 if (roomMinimum.w==0.0) return true;
 vec3 delta=receiver-origin; float entry=0.0,exitPoint=1.0;
 for (int axis=0;axis<3;++axis) {
  if (abs(delta[axis])<=0.000001) { if (origin[axis]<roomMinimum[axis] || origin[axis]>roomMaximum[axis]) return true; }
  else { float first=(roomMinimum[axis]-origin[axis])/delta[axis],second=(roomMaximum[axis]-origin[axis])/delta[axis];
   entry=max(entry,min(first,second)); exitPoint=min(exitPoint,max(first,second)); if (entry>exitPoint) return true; }
 }
 return crossingOpen(origin,delta,entry) && crossingOpen(origin,delta,exitPoint);
}
bool opaqueCrossingOpen(vec3 origin,vec3 delta,float t,vec3 minimum,vec3 maximum) {
 if (t<=0.00001 || t>=0.99999) return true;
 vec3 hit=origin+delta*t;
 for(int axis=0;axis<3;++axis) { if(abs(delta[axis])<=0.000001) continue;
  if(abs(hit[axis]-minimum[axis])<=0.0001 || abs(hit[axis]-maximum[axis])<=0.0001) return false; }
 return true;
}
bool visibleThroughOpaqueBlocker(vec3 origin,vec3 receiver,vec3 minimum,vec3 maximum) {
 vec3 delta=receiver-origin; float entry=0.0,exitPoint=1.0;
 for(int axis=0;axis<3;++axis) {
  if(abs(delta[axis])<=0.000001) { if(origin[axis]<minimum[axis] || origin[axis]>maximum[axis]) return true; }
  else { float first=(minimum[axis]-origin[axis])/delta[axis],second=(maximum[axis]-origin[axis])/delta[axis];
   entry=max(entry,min(first,second)); exitPoint=min(exitPoint,max(first,second)); if(entry>exitPoint) return true; }
 }
 return opaqueCrossingOpen(origin,delta,entry,minimum,maximum) && opaqueCrossingOpen(origin,delta,exitPoint,minimum,maximum);
}
bool visibleThrough(vec3 origin,vec3 receiver) {
 if(!visibleThroughRoom(origin,receiver)) return false;
 for(int i=0;i<int(shadowScale.w);++i) if(!visibleThroughOpaqueBlocker(origin,receiver,blockerMinimum[i].xyz,blockerMaximum[i].xyz)) return false;
 return true;
}
float pointAmount(vec3 world,vec3 authoredNormal) {
 float red=0.0,normalLength=length(authoredNormal); if(normalLength<=0.0) return 0.0;
 vec3 normal=authoredNormal/normalLength;
 for(int i=0;i<int(parameters.y);++i) {
  vec4 light=lightPositionRadius[i]; vec3 direction=light.xyz-world;
  float distanceSquared=dot(direction,direction),radiusSquared=light.w*light.w;
  if(distanceSquared>=radiusSquared || distanceSquared<=0.00000001) continue;
  float diffuse=max(0.0,dot(normal,direction*inversesqrt(distanceSquared)));
  if(diffuse<=0.0 || !visibleThrough(light.xyz,world)) continue;
  float range=1.0-distanceSquared/radiusSquared;
  red+=lightRedIntensity[i].x*lightRedIntensity[i].y*diffuse*range*range/(1.0+distanceSquared);
 }
 return saturate(red/normalAndRed.w/parameters.x);
}
void main() {
 // Core WebGL2 has no SV_ClipDistance. Perspective near/far clips in homogeneous
 // position; this fragment predicate also preserves orthographic near/far.
 if(volume.y<=0.0 || viewDepth<projection.x || viewDepth>projection.y) discard;
 ivec2 dimensions=textureSize(codes,0); vec2 uv=texUv;
 if((flags.w&1u)!=0u) uv=fract(uv);
 bool absent=(flags.w&4u)!=0u && (any(lessThan(uv,vec2(0.0))) || any(greaterThanEqual(uv,vec2(1.0))));
 ivec2 texel=ivec2(clamp(uv*vec2(dimensions),vec2(0.0),vec2(dimensions-ivec2(1))));
 absent=absent || ((flags.w&4u)!=0u && texelFetch(opacity,texel,0).r==0u);
 uint code=texelFetch(codes,texel,0).r; absent=absent || ((flags.w&2u)!=0u && code==0u);
 if(volume.x>=0.0) { code=uint(volume.x); absent=false; }
 if(absent) { if(pixelFallback.x<0.0) discard; code=uint(pixelFallback.x); }
 uint color=code;
 if(flags.x!=0u) {
  uint level=0u;
  if(flags.x==1u) level=(uint(absent ? pixelFallback.y : shadowParameters.w)&3u)*4u+uint(directionalShadow(worldPosition,shadowParameters)*3.0+0.5);
  else if(flags.x==2u) level=uint(saturate(normalDiffuse.w*(1.0-directionalShadow(worldPosition,shadowParameters)))*float(flags.y-1u)+0.5);
  else if(flags.x==4u) level=uint(shading.x);
  else level=uint(pointAmount(worldPosition,normalDiffuse.xyz)*float(flags.y-1u)+0.5);
  uint lutIndex=min(code,flags.z-1u)*flags.y+min(level,flags.y-1u),lutWidth=max(1u,uint(shadowScale.z));
  color=texelFetch(lightLut,ivec2(int(lutIndex%lutWidth),int(lutIndex/lutWidth)),0).r;
 }
 if((flags.w&16u)!=0u) {
  uint destination=texelFetch(priorIndexedColor,ivec2(gl_FragCoord.xy),0).r;
  color=texelFetch(paletteBlendLut,ivec2(int(color),int(destination)),0).r;
 }
 outputColor=color; outputPick=pick; outputDepth=viewDepth;
}
)glsl";

EM_JS(void, EnsureWorldCanvas, (), {
 if (!document.getElementById('d3d-world-gpu')) {
  const canvas=document.createElement('canvas'); canvas.id='d3d-world-gpu';
  canvas.width=1; canvas.height=1; canvas.style.display='none'; document.body.appendChild(canvas);
 }
});
EM_BOOL ContextLost(int, const void *, void *) {
 ContextNeedsReset=true; Status.available=false; Status.frameSucceeded=false;
 Fail("WebGL2 context lost",0,TownGpuFailureKind::Device);
 return EM_TRUE; // Permit browser restoration, but invalidate every cached identity.
}
EM_BOOL ContextRestored(int, const void *, void *) { ContextNeedsReset=true; return EM_TRUE; }
GLuint CompileShader(GLenum type, const char *source) {
 GLuint shader=glCreateShader(type); glShaderSource(shader,1,&source,nullptr); glCompileShader(shader);
 GLint ok=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
 if (!ok) { GLint size=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&size); std::string log(std::max(1,size),'\0');
  glGetShaderInfoLog(shader,size,nullptr,log.data()); glDeleteShader(shader); Fail("WebGL2 shader compile: "+log,0,TownGpuFailureKind::Device); return 0; }
 return shader;
}
GLuint LinkProgram(const char *vertex) {
 GLuint vs=CompileShader(GL_VERTEX_SHADER,vertex); if(!vs) return 0;
 GLuint ps=CompileShader(GL_FRAGMENT_SHADER,PixelSource); if(!ps) { glDeleteShader(vs); return 0; }
 GLuint program=glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,ps); glLinkProgram(program);
 glDeleteShader(vs); glDeleteShader(ps); GLint ok=0; glGetProgramiv(program,GL_LINK_STATUS,&ok);
 if(!ok) { GLint size=0; glGetProgramiv(program,GL_INFO_LOG_LENGTH,&size); std::string log(std::max(1,size),'\0');
  glGetProgramInfoLog(program,size,nullptr,log.data()); glDeleteProgram(program); Fail("WebGL2 program link: "+log,0,TownGpuFailureKind::Device); return 0; }
 const GLuint frame=glGetUniformBlockIndex(program,"Frame"); if(frame!=GL_INVALID_INDEX) glUniformBlockBinding(program,frame,0);
 const GLuint mesh=glGetUniformBlockIndex(program,"Mesh"); if(mesh!=GL_INVALID_INDEX) glUniformBlockBinding(program,mesh,1);
 glUseProgram(program);
 const char *names[]={"codes","opacity","lightLut","shadowDepth","priorIndexedColor","paletteBlendLut"};
 for(int i=0;i<6;++i) glUniform1i(glGetUniformLocation(program,names[i]),i);
 return program;
}
bool UploadTexture(int width,int height,GLenum format,const void *data,UINT,GlTexture &resource) {
 GlTexture uploaded; glGenTextures(1,uploaded.put()); glBindTexture(GL_TEXTURE_2D,uploaded.get());
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glPixelStorei(GL_UNPACK_ALIGNMENT,1);
 glTexImage2D(GL_TEXTURE_2D,0,format,width,height,0,format==GL_R32F ? GL_RED : GL_RED_INTEGER,
  format==GL_R32F ? GL_FLOAT : format==GL_R32UI ? GL_UNSIGNED_INT : GL_UNSIGNED_BYTE,data);
 if(!CheckGl("Upload immutable texture")) return false; resource=std::move(uploaded); return true;
}
bool UploadMeshBuffer(const void *data,size_t bytes,GLenum target,GlBuffer &buffer) {
 glGenBuffers(1,buffer.put()); glBindBuffer(target,buffer.get()); glBufferData(target,bytes,data,GL_STATIC_DRAW);
 return CheckGl("Upload immutable mesh");
}
bool CreateMeshPipeline() { return MeshProgram!=0 || Fail("WebGL2 mesh shader unavailable",0,TownGpuFailureKind::Device); }
void UpdateTextureStatus() { Status.cachedTextures=TexelCache.size(); Status.cachedLightLuts=LightLutCache.size(); Status.cachedTextureBytes=TextureBytes; }
void UpdateMeshResidentStats() { MeshStats.cachedMeshes=MeshCache.size(); MeshStats.residentBytes=MeshBytes; }
bool CreateContext(bool allowSoftwareDiagnostic) {
 EnsureWorldCanvas(); EmscriptenWebGLContextAttributes attributes;
 emscripten_webgl_init_context_attributes(&attributes); attributes.majorVersion=2; attributes.minorVersion=0;
 attributes.alpha=false; attributes.depth=false; attributes.stencil=false; attributes.antialias=false;
 attributes.premultipliedAlpha=false; attributes.preserveDrawingBuffer=false;
 attributes.failIfMajorPerformanceCaveat=!allowSoftwareDiagnostic;
 Context=emscripten_webgl_create_context("#d3d-world-gpu",&attributes);
 if(!Context) return Fail("WebGL2 context unavailable",0,TownGpuFailureKind::Unsupported);
 ContextAllowsSoftwareDiagnostic=allowSoftwareDiagnostic;
 ContextScope current; if(!current) return Fail("Cannot make WebGL2 context current",0,TownGpuFailureKind::Device);
 if(!emscripten_webgl_enable_extension(Context,"EXT_color_buffer_float"))
  return Fail("WebGL2 requires EXT_color_buffer_float",0,TownGpuFailureKind::Unsupported);
 GLint drawBuffers=0,attachments=0,uniformSize=0; glGetIntegerv(GL_MAX_DRAW_BUFFERS,&drawBuffers);
 glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&attachments); glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE,&uniformSize);
 glGetIntegerv(GL_MAX_TEXTURE_SIZE,&MaxTextureSize);
 if(drawBuffers<3 || attachments<3 || uniformSize<static_cast<GLint>(sizeof(Constants)) || MaxTextureSize<256)
  return Fail("WebGL2 capabilities insufficient for indexed/depth/pick MRT",0,TownGpuFailureKind::Unsupported);
 ProjectedProgram=LinkProgram(ProjectedVertexSource); MeshProgram=LinkProgram(MeshVertexSource);
 if(!ProjectedProgram || !MeshProgram) return false;
 glGenVertexArrays(1,&ProjectedVao); glGenVertexArrays(1,&MeshVao);
 glGenBuffers(1,VertexBuffer.put()); glGenBuffers(1,ConstantBuffer.put()); glGenBuffers(1,MeshConstantBuffer.put());
 glBindBuffer(GL_UNIFORM_BUFFER,ConstantBuffer.get()); glBufferData(GL_UNIFORM_BUFFER,sizeof(Constants),nullptr,GL_DYNAMIC_DRAW);
 glBindBuffer(GL_UNIFORM_BUFFER,MeshConstantBuffer.get()); glBufferData(GL_UNIFORM_BUFFER,sizeof(MeshConstants),nullptr,GL_DYNAMIC_DRAW);
 const uint32_t zero=0; const float fzero=0;
 if(!UploadTexture(1,1,GL_R32UI,&zero,4,DummyInteger) || !UploadTexture(1,1,GL_R32F,&fzero,4,DummyFloat)) return false;
 emscripten_set_webglcontextlost_callback("#d3d-world-gpu",nullptr,false,ContextLost);
 emscripten_set_webglcontextrestored_callback("#d3d-world-gpu",nullptr,false,ContextRestored);
 const auto *renderer=glGetString(GL_RENDERER);
 Status.available=true; Status.warp=false; Status.featureLevel=2;
 Status.adapter=std::string("WebGL2 / ")+(renderer ? reinterpret_cast<const char *>(renderer) : "browser-private adapter");
 return CheckGl("Create WebGL2 backend");
}
bool CreateTargets(int width,int height) {
 if(Width==width && Height==height && Framebuffer) return true;
 // A failed resize must never admit a partial framebuffer on a later retry.
 Width=Height=0;
 if(Framebuffer) glDeleteFramebuffers(1,&Framebuffer); Framebuffer=0;
 if(HardwareDepth) glDeleteRenderbuffers(1,&HardwareDepth); HardwareDepth=0;
 PriorIndexedColor.reset();
 const GLenum formats[]={GL_R8UI,GL_R32UI,GL_R32F};
 for(size_t i=0;i<3;++i) if(!UploadTexture(width,height,formats[i],nullptr,0,Targets[i])) return false;
 glGenFramebuffers(1,&Framebuffer); glBindFramebuffer(GL_FRAMEBUFFER,Framebuffer);
 for(int i=0;i<3;++i) glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,Targets[i].get(),0);
 glGenRenderbuffers(1,&HardwareDepth); glBindRenderbuffer(GL_RENDERBUFFER,HardwareDepth);
 glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT32F,width,height);
 glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,HardwareDepth);
 const GLenum draws[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2}; glDrawBuffers(3,draws);
 if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
  return Fail("WebGL2 mixed integer/float framebuffer incomplete",0,TownGpuFailureKind::Unsupported);
 Width=width; Height=height; return CheckGl("Create WebGL2 targets");
}
/** Eviction runs before uploads. Resources touched this frame may be borrowed
 * by any recorded batch and are never removed, even if its draw is deferred. */
bool MakeTextureRoom(size_t bytes)
{
	if (bytes > MaxTextureBytes)
		return Fail("GPU texture payload exceeds the texture-cache budget", 0, TownGpuFailureKind::Capacity);
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
		if (oldestTexels == TexelCache.end() && oldestLut == LightLutCache.end())
			return Fail("GPU texture-cache budget exceeded by current-frame resources", 0, TownGpuFailureKind::Capacity);
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

CachedTexture *GetTexture(const TownGpuTexture &input, TownGpuLighting lighting)
{
	if (input.width <= 0 || input.height <= 0 || input.lightLevels == 0) {
		Fail("Invalid GPU texture dimensions or light-level count");
		return nullptr;
	}
	if (input.width > MaxTextureSize || input.height > MaxTextureSize || input.lightLevels > MaxTextureSize) {
		Fail("GPU texture dimensions or light-level capacity exceeded", 0, TownGpuFailureKind::Capacity);
		return nullptr;
	}
	const size_t count = static_cast<size_t>(input.width) * input.height;
	if (count > MaxPixels || input.lightLut.size() / input.lightLevels > 262144) {
		Fail("GPU texture pixel or palette/LUT capacity exceeded", 0, TownGpuFailureKind::Capacity);
		return nullptr;
	}
	if ((!input.texelCodes.empty() && input.texelCodes.size() != count)
	    || (!input.opacity.empty() && input.opacity.size() != count)
	    || input.lightLut.size() % input.lightLevels != 0) {
		Fail("Invalid GPU texture payload");
		return nullptr;
	}
	const TextureKey texelKey { input.stableKey, input.revision };
	const LightLutKey lutKey { input.lightLutKey != 0 ? TextureKey { input.lightLutKey, input.lightLutRevision } : texelKey, input.lightLutKey != 0 };
	auto texelsFound = TexelCache.find(texelKey);
	auto lutFound = LightLutCache.find(lutKey);
	CachedTexels *texels = texelsFound == TexelCache.end() ? nullptr : &texelsFound->second;
	CachedLightLut *lut = lutFound == LightLutCache.end() ? nullptr : &lutFound->second;
	if ((texels != nullptr && (texels->width != input.width || texels->height != input.height
	        || (!input.opacity.empty() && !texels->hasOpacity)))
	    || (lut != nullptr && (lut->lightLevels != input.lightLevels
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
	const unsigned codeCount = lut != nullptr ? lut->codeCount
	    : input.lightLut.empty() ? 256 : static_cast<unsigned>(input.lightLut.size() / input.lightLevels);
	const uint32_t maximumCode = texels != nullptr ? texels->maximumCode : *std::max_element(input.texelCodes.begin(), input.texelCodes.end());
	if (maximumCode >= codeCount || (lighting == TownGpuLighting::Unlit && maximumCode > 255)) {
		Fail("GPU texel code exceeds its palette/LUT");
		return nullptr;
	}
	const size_t texelBytes = texels == nullptr ? count * sizeof(uint32_t) + input.opacity.size() : 0;
	const unsigned lutWidth = lut != nullptr ? lut->lutWidth : static_cast<unsigned>(std::min<size_t>(MaxTextureSize, input.lightLut.size()));
	const size_t lutHeight = lutWidth == 0 ? 0 : (input.lightLut.size() + lutWidth - 1) / lutWidth;
	const size_t lutBytes = lut == nullptr ? lutHeight * lutWidth : 0;
	if (lutHeight > MaxTextureSize || texelBytes > MaxTextureBytes || lutBytes > MaxTextureBytes - texelBytes) {
		Fail("GPU texture/LUT payload exceeds resource dimensions or cache budget", 0, TownGpuFailureKind::Capacity);
		return nullptr;
	}
	// Pin hits before making room: a new LUT revision can reuse the exact texel
	// resource that pressure eviction would otherwise consider an older entry.
	if (texels != nullptr)
		texels->lastSeenFrame = FrameSerial;
	if (lut != nullptr)
		lut->lastSeenFrame = FrameSerial;
	// Hits retain CPU validation/pinning but perform no GL operations. Every
	// miss and every possible pressure eviction must own the private context.
	// The scope ends before returning, restoring the caller's SDL context.
	std::optional<ContextScope> uploadContext;
	if (texels == nullptr || (lut == nullptr && !input.lightLut.empty()) || TextureBytes > MaxTextureBytes) {
		uploadContext.emplace();
		if (!*uploadContext) {
			Fail("WebGL2 context unavailable", 0, TownGpuFailureKind::Device);
			return nullptr;
		}
	}
	if (!MakeTextureRoom(texelBytes + lutBytes))
		return nullptr;
	CachedTexels newTexels {};
	CachedLightLut newLut {};
	if (texels == nullptr) {
		newTexels.width = input.width;
		newTexels.height = input.height;
		newTexels.maximumCode = maximumCode;
		newTexels.hasOpacity = !input.opacity.empty();
		newTexels.bytes = texelBytes;
		newTexels.lastSeenFrame = FrameSerial;
		if (!UploadTexture(input.width, input.height, DXGI_FORMAT_R32_UINT, input.texelCodes.data(), input.width * sizeof(uint32_t), newTexels.codes)
		    || (!input.opacity.empty() && !UploadTexture(input.width, input.height, DXGI_FORMAT_R8_UINT, input.opacity.data(), input.width, newTexels.opacity)))
			return nullptr;
	}
	if (lut == nullptr && !input.lightLut.empty()) {
		newLut.lightLevels = input.lightLevels;
		newLut.codeCount = codeCount;
		newLut.lutWidth = lutWidth;
		newLut.bytes = lutBytes;
		newLut.lastSeenFrame = FrameSerial;
		std::vector<uint8_t> padded;
		const uint8_t *lutData = input.lightLut.data();
		if (lutBytes != input.lightLut.size()) {
			padded.assign(input.lightLut.begin(), input.lightLut.end());
			padded.resize(lutBytes);
			lutData = padded.data();
		}
		if (!UploadTexture(lutWidth, static_cast<int>(lutHeight), DXGI_FORMAT_R8_UINT, lutData, lutWidth, newLut.view))
			return nullptr;
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
	UpdateTextureStatus();
	const TextureBindingKey bindingKey { texelKey, lutKey, input.lightLevels, lut != nullptr };
	return &TextureCache.try_emplace(bindingKey, CachedTexture { texels, lut, input.lightLevels, codeCount, lutWidth, texels->hasOpacity }).first->second;
}

bool Finite(float value) { return std::isfinite(value); }
bool Finite(TownLightVector value) { return Finite(value.x) && Finite(value.height) && Finite(value.z); }


Box PaletteSnapshotBox(const std::array<TownGpuVertex, 3> &vertices)
{
	const Box full { 0, 0, 0, static_cast<UINT>(Width), static_cast<UINT>(Height), 1 };
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
	const Box bounded { lower(minX, Width), lower(minY, Height), 0,
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

bool MakeConstants(const TownGpuMaterial &material, const CachedTexture &texture, Constants &output)
{
	if (static_cast<uint32_t>(material.lighting) > static_cast<uint32_t>(TownGpuLighting::Palette)
	    || !Finite(material.diffuse) || !Finite(material.shadowBias) || !Finite(material.shadowSlopeU) || !Finite(material.shadowSlopeV)
	    || !std::all_of(material.normal.begin(), material.normal.end(), [](float value) { return Finite(value); })
	    || material.fallbackPaletteIndex < -1 || material.fallbackPaletteIndex > 255
	    || (material.fallbackPaletteIndex >= 0 && material.lighting != TownGpuLighting::Shadow && material.lighting != TownGpuLighting::Unlit)
	    || (material.lighting == TownGpuLighting::Shadow && material.fallbackPaletteIndex >= static_cast<int>(texture.codeCount))
	    || (material.lighting == TownGpuLighting::Shadow && texture.lightLevels != 16)
	    || (material.lighting == TownGpuLighting::Palette
	        && (material.shade < 0 || material.shade > 15 || texture.lightLevels != 16 || texture.codeCount != 256))
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
		    | (texture.hasOpacity ? 4 : 0) | (FrameShadow.resolution > 0 ? 8 : 0) | (material.paletteBlend ? 16 : 0)) };
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

bool MakeMeshRoom(size_t bytes)
{
	if (bytes > MaxMeshBytes)
		return Fail("GPU mesh payload exceeds the mesh-cache budget", 0, TownGpuFailureKind::Capacity);
	while (MeshBytes > MaxMeshBytes - bytes) {
		auto oldest = MeshCache.end();
		for (auto entry = MeshCache.begin(); entry != MeshCache.end(); ++entry) {
			if (entry->second.lastSeenFrame < FrameSerial
			    && (oldest == MeshCache.end() || entry->second.lastSeenFrame < oldest->second.lastSeenFrame))
				oldest = entry;
		}
		if (oldest == MeshCache.end())
			return Fail("GPU mesh-cache budget exceeded by current-frame resources", 0, TownGpuFailureKind::Capacity);
		MeshBytes -= oldest->second.bytes;
		MeshCache.erase(oldest);
		++MeshStats.evictions;
		UpdateMeshResidentStats();
	}
	UpdateMeshResidentStats();
	return true;
}


// This cache exists only while EndFrame owns the private GL context. Every
// binding starts unknown; no BeginFrame/Submit/upload/SDL state is inferred.
// Byte comparisons are conservative: only exactly identical UBO bytes skip.
class EndFrameGlState {
 GLuint program=0, vao=0;
 bool programKnown=false, vaoKnown=false;
 int activeTextureUnit=-1;
 std::array<GLuint,6> textures {};
 std::array<bool,6> textureKnown {};
 Constants lastConstants {};
 MeshConstants lastMeshConstants {};
 bool constantsKnown=false, meshConstantsKnown=false;
 bool frameBufferBaseBound=false, meshBufferBaseBound=false;
 bool drawBuffersKnown=false, depthMaskKnown=false;
 bool pickWrite=false, viewDepthWrite=false, depthWrite=false;
public:
 void UseProgram(GLuint value) {
  if(programKnown && program==value) return;
  glUseProgram(value); program=value; programKnown=true;
 }
 void BindVao(GLuint value) {
  if(vaoKnown && vao==value) return;
  glBindVertexArray(value); vao=value; vaoKnown=true;
 }
 void BindGeometryConstants(const Constants &value) {
  if(constantsKnown && std::memcmp(&lastConstants,&value,sizeof(value))==0) return;
  // BindBufferBase also changes the generic UBO binding. Always bind the
  // intended buffer before an upload, including after a mesh UBO upload.
  glBindBuffer(GL_UNIFORM_BUFFER,ConstantBuffer.get());
  glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(value),&value);
  if(!frameBufferBaseBound) {
   glBindBufferBase(GL_UNIFORM_BUFFER,0,ConstantBuffer.get());
   frameBufferBaseBound=true;
  }
  lastConstants=value; constantsKnown=true;
 }
 void BindMeshConstants(const MeshConstants &value) {
  if(meshConstantsKnown && std::memcmp(&lastMeshConstants,&value,sizeof(value))==0) return;
  glBindBuffer(GL_UNIFORM_BUFFER,MeshConstantBuffer.get());
  glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(value),&value);
  if(!meshBufferBaseBound) {
   glBindBufferBase(GL_UNIFORM_BUFFER,1,MeshConstantBuffer.get());
   meshBufferBaseBound=true;
  }
  lastMeshConstants=value; meshConstantsKnown=true;
 }
 void BindTexture2D(unsigned unit,GLuint texture) {
  if(textureKnown[unit] && textures[unit]==texture) return;
  if(activeTextureUnit!=static_cast<int>(unit)) {
   glActiveTexture(GL_TEXTURE0+unit); activeTextureUnit=static_cast<int>(unit);
  }
  glBindTexture(GL_TEXTURE_2D,texture);
  textures[unit]=texture; textureKnown[unit]=true;
 }
 void InvalidateTextureUnit(unsigned unit) {
  // Overlay snapshot uses direct binds, outside BindTexture2D. Conservatively
  // forget its active unit and binding before the next material is applied.
  textureKnown[unit]=false; activeTextureUnit=-1;
 }
 void BindGeometryMaterial(const CachedTexture &texture,bool preservePicking,bool paletteBlend) {
  const GLuint handles[]={texture.texels->codes.get(),texture.texels->opacity.get(),texture.lut ? texture.lut->view.get() : 0,
   ShadowResource.get(),paletteBlend ? PriorIndexedColor.get() : 0,paletteBlend ? PaletteBlendResource.get() : 0};
  for(unsigned i=0;i<6;++i)
   BindTexture2D(i,handles[i] ? handles[i] : (i==3 ? DummyFloat.get() : DummyInteger.get()));
  const bool nextPickWrite=!(paletteBlend || preservePicking), nextViewDepthWrite=!paletteBlend;
  // Same per-attachment suppression as before: indexed color is always kept.
  if(!drawBuffersKnown || pickWrite!=nextPickWrite || viewDepthWrite!=nextViewDepthWrite) {
   const GLenum draw[]={GL_COLOR_ATTACHMENT0,static_cast<GLenum>(nextPickWrite ? GL_COLOR_ATTACHMENT1 : GL_NONE),
    static_cast<GLenum>(nextViewDepthWrite ? GL_COLOR_ATTACHMENT2 : GL_NONE)};
   glDrawBuffers(3,draw); pickWrite=nextPickWrite; viewDepthWrite=nextViewDepthWrite; drawBuffersKnown=true;
  }
  if(!depthMaskKnown || depthWrite!=nextViewDepthWrite) {
   glDepthMask(nextViewDepthWrite ? GL_TRUE : GL_FALSE); depthWrite=nextViewDepthWrite; depthMaskKnown=true;
  }
 }
};
static_assert(sizeof(Float4)==16 && sizeof(UInt4)==16);
static_assert(sizeof(Constants)==16*(12+2*TownMaxPointLights+2*TownMaxLightApertures+2*(TownMaxLightOccluders-1)));
void FloatAttribute(GLuint index,int components,size_t offset,int stride) {
 glEnableVertexAttribArray(index); glVertexAttribPointer(index,components,GL_FLOAT,GL_FALSE,stride,reinterpret_cast<const void *>(offset));
}
void UintAttribute(GLuint index,size_t offset,int stride) {
 glEnableVertexAttribArray(index); glVertexAttribIPointer(index,1,GL_UNSIGNED_INT,stride,reinterpret_cast<const void *>(offset));
}
void BindProjectedVertices() {
 glBindVertexArray(ProjectedVao); glBindBuffer(GL_ARRAY_BUFFER,VertexBuffer.get());
 const int stride=sizeof(GpuVertex);
 FloatAttribute(0,2,offsetof(GpuVertex,vertex)+offsetof(TownGpuVertex,x),stride);
 FloatAttribute(1,1,offsetof(GpuVertex,vertex)+offsetof(TownGpuVertex,depth),stride);
 FloatAttribute(2,2,offsetof(GpuVertex,vertex)+offsetof(TownGpuVertex,u),stride);
 FloatAttribute(3,3,offsetof(GpuVertex,vertex)+offsetof(TownGpuVertex,world),stride);
 FloatAttribute(4,1,offsetof(GpuVertex,vertex)+offsetof(TownGpuVertex,clipW),stride);
 UintAttribute(5,offsetof(GpuVertex,pickId),stride);
 FloatAttribute(6,4,offsetof(GpuVertex,normalDiffuse),stride);
 FloatAttribute(7,4,offsetof(GpuVertex,shadowParameters),stride);
 FloatAttribute(8,2,offsetof(GpuVertex,fallback),stride);
}
bool DrawMeshBatch(const MeshBatch &batch,EndFrameGlState &drawState) {
 drawState.UseProgram(MeshProgram); drawState.BindGeometryConstants(batch.constants);
 drawState.BindMeshConstants(batch.meshConstants);
 drawState.BindVao(MeshVao); glBindBuffer(GL_ARRAY_BUFFER,batch.mesh->vertices.get());
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,batch.mesh->indices.get()); const int stride=sizeof(TownGpuMeshVertex);
 FloatAttribute(0,3,offsetof(TownGpuMeshVertex,position),stride); FloatAttribute(2,2,offsetof(TownGpuMeshVertex,uv),stride);
 FloatAttribute(9,3,offsetof(TownGpuMeshVertex,normal),stride); UintAttribute(10,offsetof(TownGpuMeshVertex,volumeFace),stride);
 drawState.BindGeometryMaterial(*batch.texture,batch.preservePicking,false);
 glDrawElements(GL_TRIANGLES,batch.count,GL_UNSIGNED_INT,reinterpret_cast<const void *>(static_cast<size_t>(batch.first)*sizeof(uint32_t)));
 ++Status.drawCalls; return true; // EndFrame checks a bounded group before any publication.
}
} // namespace

bool TownGpuBeginFrame(int width,int height,bool allowSoftwareDiagnostic,const TownGpuProjection &projection) {
 BeforeReadbackForDiagnostics=nullptr;
  if(ContextNeedsReset || (Context && ContextAllowsSoftwareDiagnostic && !allowSoftwareDiagnostic)) ResetTownGpuResources();
 FrameActive=false; FrameFailed=false; FramePaletteBlendReady=false; FramePaletteOverlayStarted=false;
 Vertices.clear(); Batches.clear(); GeometryCommands.clear(); MeshBatches.clear(); TextureCache.clear();
 MeshCamera={}; MeshCameraReady=false; MeshStats={}; UpdateMeshResidentStats(); ++FrameSerial;
 const bool available=Status.available; const auto adapter=std::move(Status.adapter); const auto feature=Status.featureLevel;
 Status={}; Status.available=available; Status.adapter=adapter; Status.featureLevel=feature; UpdateTextureStatus();
 FrameShadow={}; FrameProjection={}; FrameStart=Clock::now();
 if(width<=0 || height<=0) return Fail("Invalid GPU frame dimensions");
 if(static_cast<uint64_t>(width)*height>MaxPixels) return Fail("GPU frame pixel budget exceeded",0,TownGpuFailureKind::Capacity);
 if(!Finite(projection.nearClip) || !Finite(projection.farClip) || projection.nearClip<=0 || projection.farClip<=projection.nearClip || projection.farClip>4096)
  return Fail("Invalid GPU projection range");
 if(!Context && !CreateContext(allowSoftwareDiagnostic)) {
  const TownGpuStatus failed=Status; ResetTownGpuResources(); Status=failed;
  Status.available=false; FrameFailed=true; return false;
 }
 ContextScope current; if(!current) return Fail("WebGL2 context unavailable",0,TownGpuFailureKind::Device);
	for (auto entry = TexelCache.begin(); entry != TexelCache.end();) {
		if (FrameSerial - entry->second.lastSeenFrame > 2) {
			TextureBytes -= entry->second.bytes;
			entry = TexelCache.erase(entry);
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
 UpdateTextureStatus();
 if(width>MaxTextureSize || height>MaxTextureSize) return Fail("WebGL2 target dimensions exceed device capacity",0,TownGpuFailureKind::Capacity);
 if(!CreateTargets(width,height)) return false;
 FrameProjection=projection; FrameActive=true; return true;
}
bool TownGpuSetShadow(const TownGpuShadow &shadow) {
 ContextScope current; if(!current) return Fail("WebGL2 context unavailable",0,TownGpuFailureKind::Device);
 if(!FrameActive || FrameFailed || !GeometryCommands.empty()) return Fail("Shadow upload must precede GPU geometry");
 if(shadow.resolution==0 && shadow.depth.empty()) { FrameShadow={}; return true; }
 if(shadow.resolution<=0 || shadow.resolution>std::min(2048,MaxTextureSize)
  || shadow.depth.size()!=static_cast<size_t>(shadow.resolution)*shadow.resolution
  || !Finite(shadow.texelU) || shadow.texelU<=0 || !Finite(shadow.texelV) || shadow.texelV<=0
  || !Finite(shadow.minU) || !Finite(shadow.minV) || shadow.pcfRadius<0 || shadow.pcfRadius>2
  || !std::all_of(shadow.right.begin(),shadow.right.end(),[](float x){return Finite(x);})
  || !std::all_of(shadow.up.begin(),shadow.up.end(),[](float x){return Finite(x);})
  || !std::all_of(shadow.light.begin(),shadow.light.end(),[](float x){return Finite(x);})) return Fail("Invalid GPU shadow map");
 if(!ShadowResource || ShadowKey!=shadow.stableKey || ShadowRevision!=shadow.revision || ShadowResolution!=shadow.resolution) {
  if(!UploadTexture(shadow.resolution,shadow.resolution,GL_R32F,shadow.depth.data(),0,ShadowResource)) return false;
  ShadowKey=shadow.stableKey; ShadowRevision=shadow.revision; ShadowResolution=shadow.resolution;
 }
 FrameShadow=shadow; FrameShadow.depth={}; return true;
}
bool TownGpuSetPaletteBlend(const TownGpuPaletteBlend &blend) {
 ContextScope current; if(!current) return Fail("WebGL2 context unavailable",0,TownGpuFailureKind::Device);
 if(!FrameActive || FrameFailed || !GeometryCommands.empty()) return Fail("Blend upload must precede GPU geometry");
 if(blend.stableKey==0 || (!blend.lookup.empty() && blend.lookup.size()!=65536)) return Fail("Invalid palette blend table");
 if(!PaletteBlendResource || PaletteBlendKey!=blend.stableKey || PaletteBlendRevision!=blend.revision) {
  if(blend.lookup.size()!=65536) return Fail("Missing uncached palette blend table");
  if(!UploadTexture(256,256,GL_R8UI,blend.lookup.data(),0,PaletteBlendResource)) return false;
  PaletteBlendKey=blend.stableKey; PaletteBlendRevision=blend.revision; Status.uploadedLutBytes+=65536;
 }
 if(!PriorIndexedColor && !UploadTexture(Width,Height,GL_R8UI,nullptr,0,PriorIndexedColor)) return false;
 FramePaletteBlendReady=true; return true;
}
bool TownGpuSubmitProjectedTriangle(const std::array<TownGpuVertex, 3> &vertices,
    const TownGpuTexture &texture, const TownGpuMaterial &material, uint32_t pickId)
{
	if (!ContextValidNoSwitch()) return Fail("WebGL2 context unavailable", 0, TownGpuFailureKind::Device);

	if (!FrameActive || FrameFailed)
		return Fail("No valid GPU frame is recording");
	if (!material.paletteBlend && FramePaletteOverlayStarted)
		return Fail("Opaque GPU geometry must precede native palette overlays");
	if (material.paletteBlend && !FramePaletteBlendReady)
		return Fail("GPU native palette overlay requires a frame blend table");
	if (Vertices.size() >= MaxProjectedTriangles * 3)
		return Fail("GPU projected vertex-stream budget exceeded", 0, TownGpuFailureKind::Capacity);
	if (Status.submittedTriangles == std::numeric_limits<size_t>::max())
		return Fail("GPU submitted-triangle counter overflow", 0, TownGpuFailureKind::Capacity);
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
	Box paletteCopyBox {};
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
			return Fail("GPU native palette overlay triangle/copy budget exceeded", 0, TownGpuFailureKind::Capacity);
	}
	const UINT first = static_cast<UINT>(Vertices.size());
	for (const TownGpuVertex &vertex : vertices)
		Vertices.push_back({ vertex, pickId,
		    { material.normal[0], material.normal[1], material.normal[2], std::clamp(material.diffuse, 0.0F, 1.0F) },
		    { material.shadowBias, material.shadowSlopeU, material.shadowSlopeV,
		        static_cast<float>(std::clamp(material.shade, 0, 3) + (material.receivesShadow ? 4 : 0)) },
		    { static_cast<float>(material.fallbackPaletteIndex), static_cast<float>(std::clamp(material.fallbackShade, 0, 3)) } });
	if (!material.paletteBlend && !GeometryCommands.empty() && !GeometryCommands.back().mesh && !Batches.empty()
	    && !Batches.back().paletteBlend && Batches.back().texture == uploaded && Batches.back().preservePicking == material.preservePicking
	    && std::memcmp(&Batches.back().constants, &constants, sizeof(constants)) == 0)
		Batches.back().count += 3;
	else {
		GeometryCommands.push_back({ false, Batches.size() });
		Batches.push_back({ uploaded, constants, first, 3, material.preservePicking, material.paletteBlend,
		    paletteCopyBox });
		MeshStats.recordedProjectedBatches = Batches.size();
	}
	if (material.paletteBlend) {
		FramePaletteOverlayStarted = true;
		++Status.paletteBlendTriangles;
		Status.paletteBlendAdmittedBytes += paletteCopyBytes;
	}
	++Status.submittedTriangles;
	return true;
}

bool TownGpuSetMeshCamera(const TownGpuMeshCamera &camera)
{
	if (!ContextValidNoSwitch()) return Fail("WebGL2 context unavailable", 0, TownGpuFailureKind::Device);

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
}


bool TownGpuHasMesh(TownGpuMeshIdentity identity) {
 return Context!=0 && !ContextNeedsReset && !emscripten_is_webgl_context_lost(Context)
  && identity.stableKey!=0 && MeshCache.find({identity.stableKey,identity.revision})!=MeshCache.end();
}
bool TownGpuUploadMesh(const TownGpuMeshUpload &input)
{
	ContextScope context;
	if (!context) return Fail("WebGL2 context unavailable", 0, TownGpuFailureKind::Device);

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
		return Fail("GPU immutable mesh buffer/cache budget exceeded", 0, TownGpuFailureKind::Capacity);
	const size_t vertexBytes = input.vertices.size() * sizeof(TownGpuMeshVertex);
	const size_t indexBytes = input.indices.size() * sizeof(uint32_t);
	if (vertexBytes > std::numeric_limits<UINT>::max() || indexBytes > std::numeric_limits<UINT>::max()
	    || vertexBytes > MaxMeshBytes || indexBytes > MaxMeshBytes - vertexBytes)
		return Fail("GPU immutable mesh buffer/cache budget exceeded", 0, TownGpuFailureKind::Capacity);
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
	if (!UploadMeshBuffer(input.vertices.data(), vertexBytes, GL_ARRAY_BUFFER, mesh.vertices)
	    || !UploadMeshBuffer(input.indices.data(), indexBytes, GL_ELEMENT_ARRAY_BUFFER, mesh.indices))
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
}

bool TownGpuSubmitMeshInstance(const TownGpuMeshInstance &instance, const TownGpuTexture &texture, const TownGpuMaterial &material)
{
	if (!ContextValidNoSwitch()) return Fail("WebGL2 context unavailable", 0, TownGpuFailureKind::Device);

	if (!FrameActive || FrameFailed)
		return Fail("No valid GPU frame/mesh camera is recording");
	if (material.paletteBlend)
		return Fail("Native palette overlays require projected triangles; resident mesh overlays are unsupported", 0, TownGpuFailureKind::Unsupported);
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
		return Fail("GPU submitted-triangle counter overflow", 0, TownGpuFailureKind::Capacity);
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
	MeshStats.recordedMeshBatches = MeshBatches.size();
	mesh.lastSeenFrame = FrameSerial;
	++MeshStats.instances;
	if (mesh.uploadFrame != FrameSerial)
		++MeshStats.reusedInstances;
	Status.submittedTriangles += count / 3;
	return true;
}


bool TownGpuEndFrame(TownGpuFrame &output) {
 output={}; ContextScope current;
 if(!current) return Fail("WebGL2 context unavailable",0,TownGpuFailureKind::Device);
 if(!FrameActive || FrameFailed) return false; FrameActive=false;
 const auto commandStart=Clock::now();
 glBindFramebuffer(GL_FRAMEBUFFER,Framebuffer); glViewport(0,0,Width,Height);
 glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glDisable(GL_DITHER);
 glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
 const GLenum draws[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2}; glDrawBuffers(3,draws);
 const GLuint zero[]={0,0,0,0}; const GLfloat infinity[]={std::numeric_limits<float>::infinity(),0,0,0}; const GLfloat one=1;
 glClearBufferuiv(GL_COLOR,0,zero); glClearBufferuiv(GL_COLOR,1,zero); glClearBufferfv(GL_COLOR,2,infinity); glClearBufferfv(GL_DEPTH,0,&one);
 if(!Vertices.empty()) {
  glBindVertexArray(ProjectedVao); glBindBuffer(GL_ARRAY_BUFFER,VertexBuffer.get());
  glBufferData(GL_ARRAY_BUFFER,Vertices.size()*sizeof(GpuVertex),Vertices.data(),GL_STREAM_DRAW);
  MeshStats.projectedUploadBytes=Vertices.size()*sizeof(GpuVertex);
  BindProjectedVertices(); // VAO captures this unchanged buffer/layout once per frame.
 }
 // Avoid a synchronous error query after every draw. Never defer past a
 // bounded 256-draw group, an indexed overlay snapshot or the final readback.
 // FrameFailed/Fail retain the first detected cause; output stays empty on error.
 EndFrameGlState drawState; // Reset every EndFrame, after all setup/uploads.
 constexpr size_t DrawErrorCheckpointSize=256;
 size_t uncheckedDraws=0;
 const auto checkDrawErrors=[&]() {
  if(uncheckedDraws==0) return true;
  if(!CheckGl("Draw geometry checkpoint")) return false;
  uncheckedDraws=0; return true;
 };
 for(const GeometryCommand &command:GeometryCommands) {
  if(command.mesh) {
   if(!DrawMeshBatch(MeshBatches[command.batch],drawState)) return false;
  } else {
  const Batch &batch=Batches[command.batch];
  if(batch.paletteBlend) {
   if(!checkDrawErrors()) return false; // Prior opaque/overlay commands before snapshot.

   glReadBuffer(GL_COLOR_ATTACHMENT0); glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D,PriorIndexedColor.get());
   drawState.InvalidateTextureUnit(4);
   const Box &box=batch.paletteCopyBox; const int bottom=Height-static_cast<int>(box.bottom);
   glCopyTexSubImage2D(GL_TEXTURE_2D,0,box.left,bottom,box.left,bottom,box.right-box.left,box.bottom-box.top);
   Status.paletteBlendCopyBytes+=static_cast<size_t>(box.right-box.left)*(box.bottom-box.top);
   if(!CheckGl("Snapshot preceding indexed overlay color")) return false;
  }
  drawState.UseProgram(ProjectedProgram); drawState.BindGeometryConstants(batch.constants); drawState.BindVao(ProjectedVao);
  drawState.BindGeometryMaterial(*batch.texture,batch.preservePicking,batch.paletteBlend);
  glDrawArrays(GL_TRIANGLES,batch.first,batch.count); ++Status.drawCalls;
  }
  ++uncheckedDraws;
  if(uncheckedDraws>=DrawErrorCheckpointSize && !checkDrawErrors()) return false;
 }
 if(!checkDrawErrors()) return false; // Required even for a short final group.
 MeshStats.commandMilliseconds=std::chrono::duration<double,std::milli>(Clock::now()-commandStart).count();
 if(const auto before=std::exchange(BeforeReadbackForDiagnostics,nullptr)) before();
 const auto readStart=Clock::now(); const size_t count=static_cast<size_t>(Width)*Height;
 TownGpuFrame complete; complete.width=Width; complete.height=Height;
 complete.indexed.resize(count); complete.pickIds.resize(count); complete.depth.resize(count);
 // Portable WebGL2 read pairs: RGBA_INTEGER/UNSIGNED_INT and RGBA/FLOAT.
 // R targets publish the first component; flip rows to the native top-left API.
  // Release each portable RGBA scratch before allocating the next type: a
  // bounded 64 MiB peak scratch at MaxPixels, rather than 128 MiB combined.
 glBindBuffer(GL_PIXEL_PACK_BUFFER,0); glPixelStorei(GL_PACK_ALIGNMENT,1);
 const auto nativePixel=[&](size_t p){return (static_cast<size_t>(Height)-1-p/static_cast<size_t>(Width))*Width+p%Width;};
  { std::vector<uint32_t> integers(count*4);
  glReadBuffer(GL_COLOR_ATTACHMENT0); glReadPixels(0,0,Width,Height,GL_RGBA_INTEGER,GL_UNSIGNED_INT,integers.data());
 if(!CheckGl("Read indexed palette")) return false;
 for(size_t p=0;p<count;++p) complete.indexed[nativePixel(p)]=static_cast<uint8_t>(integers[p*4]);
 glReadBuffer(GL_COLOR_ATTACHMENT1); glReadPixels(0,0,Width,Height,GL_RGBA_INTEGER,GL_UNSIGNED_INT,integers.data());
 if(!CheckGl("Read exact pick IDs")) return false;
  for(size_t p=0;p<count;++p) complete.pickIds[nativePixel(p)]=integers[p*4]; }
  { std::vector<float> floats(count*4);
 glReadBuffer(GL_COLOR_ATTACHMENT2); glReadPixels(0,0,Width,Height,GL_RGBA,GL_FLOAT,floats.data());
 if(!CheckGl("Read unnormalized view depth")) return false;
  for(size_t p=0;p<count;++p) complete.depth[nativePixel(p)]=floats[p*4]; }
 Status.readbackMilliseconds=std::chrono::duration<double,std::milli>(Clock::now()-readStart).count();
 Status.frameMilliseconds=std::chrono::duration<double,std::milli>(Clock::now()-FrameStart).count();
 UpdateTextureStatus(); Status.frameSucceeded=true; output=std::move(complete);
 // Optional numeric diagnostics only; saves, pixels and archives are never exposed here.
 EM_ASM({ if (Module.d3dGpuDiagnostics) Module.d3dLastGpuFrame = ({
  serial:$0, draws:$1, commandMilliseconds:$2, readbackMilliseconds:$3,
  width:$4, height:$5, frameMilliseconds:$6, submittedTriangles:$7
 }); }, static_cast<double>(FrameSerial), static_cast<double>(Status.drawCalls),
  MeshStats.commandMilliseconds, Status.readbackMilliseconds, Width, Height,
  Status.frameMilliseconds, static_cast<double>(Status.submittedTriangles));
 return true;
}
void SetTownGpuBeforeReadbackForDiagnostics(void (*callback)()) noexcept { BeforeReadbackForDiagnostics=callback; }
void ResetTownGpuResources() {
 const auto previous=emscripten_webgl_get_current_context();
 if(Context && !emscripten_is_webgl_context_lost(Context)) emscripten_webgl_make_context_current(Context);
 FrameActive=false; FrameFailed=false; BeforeReadbackForDiagnostics=nullptr;
 GeometryCommands.clear(); Batches.clear(); MeshBatches.clear(); Vertices.clear(); TextureCache.clear();
 MeshCache.clear(); TexelCache.clear(); LightLutCache.clear(); TextureBytes=MeshBytes=0;
 ShadowResource.reset(); PaletteBlendResource.reset(); PriorIndexedColor.reset(); DummyInteger.reset(); DummyFloat.reset();
 for(auto &target:Targets) target.reset(); VertexBuffer.reset(); ConstantBuffer.reset(); MeshConstantBuffer.reset();
 if(CanDeleteGl()) {
  if(ProjectedProgram) glDeleteProgram(ProjectedProgram); if(MeshProgram) glDeleteProgram(MeshProgram);
  if(ProjectedVao) glDeleteVertexArrays(1,&ProjectedVao); if(MeshVao) glDeleteVertexArrays(1,&MeshVao);
  if(Framebuffer) glDeleteFramebuffers(1,&Framebuffer); if(HardwareDepth) glDeleteRenderbuffers(1,&HardwareDepth);
 }
 const auto destroyed=Context; Context=0;
 if(destroyed) emscripten_webgl_destroy_context(destroyed);
 emscripten_webgl_make_context_current(previous==destroyed ? 0 : previous);
 ProjectedProgram=MeshProgram=ProjectedVao=MeshVao=Framebuffer=HardwareDepth=0;
 ShadowKey=ShadowRevision=PaletteBlendKey=PaletteBlendRevision=FrameSerial=0; ShadowResolution=Width=Height=0;
 FrameShadow={}; FrameProjection={}; MeshCamera={}; MeshCameraReady=false; FramePaletteBlendReady=false;
 FramePaletteOverlayStarted=false; ContextNeedsReset=false; ContextAllowsSoftwareDiagnostic=false; Status={}; MeshStats={};
}
const TownGpuStatus &GetTownGpuStatus() { return Status; }
const TownGpuMeshStats &GetTownGpuMeshStats() { return MeshStats; }
} // namespace devilution
#endif // __EMSCRIPTEN__
