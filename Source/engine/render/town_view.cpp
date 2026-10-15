#include "engine/render/cathedral/dungeon_rectified_material.hpp"
#include "engine/render/town_view.hpp"
#include "engine/render/world_grid_metric.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <unordered_map>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "control/control.hpp"
#include "cursor.h"
#include "diablo.h"
#include "game_mode.hpp"
#include "mods/mod_identity.h"
#include "nthread.h"
#include "engine/render/actor_model.hpp"
#include "engine/render/actor_pose.hpp"
#include "engine/render/actor_visual_snapshot.hpp"
#include "engine/render/player_actor_material.hpp"
#include "engine/render/player_actor_runtime.hpp"
#include "engine/render/player_actor_runtime_profile.hpp"
#include "engine/render/ogden_idle_pilot.hpp"
#include "engine/render/ogden_idle_native_clock.hpp"
#include "dead.h"
#include "engine/assets.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/hd_sprite_pack.hpp"
#include "engine/light_tables.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/cathedral/cathedral_frame.hpp"
#include "engine/render/cathedral/cathedral_native_wall.hpp"
#include "engine/render/cathedral/cathedral_native_fence.hpp"
#include "engine/render/cathedral/cathedral_native_operators.hpp"
#include "engine/render/cathedral/cathedral_native_visual_policy.hpp"
#include "engine/render/cathedral/cathedral_live.hpp"
#include "engine/render/cathedral/cathedral_native_palette.hpp"
#include "engine/render/cathedral/cathedral_native_coverage.hpp"
#include "engine/render/cathedral_fallback_diagnostic.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/light_render.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/town_camera_gameplay.hpp"
#include "engine/render/town_camera_visibility.hpp"
#include "engine/render/town_ground_shadow.hpp"
#include "engine/render/town_gpu.hpp"
#include "engine/render/d3d_local_diagnostics.hpp"
#include "engine/render/text_render.hpp"
#include "utils/display.h"
#include "engine/render/town_gpu_mesh.hpp"
#include "engine/render/town_gpu_recovery.hpp"
#include "engine/render/town_horizon.hpp"
#include "engine/render/town_lighting.hpp"
#include "engine/render/town_lighting_profile.hpp"
#include "engine/render/town_scene.hpp"
#include "engine/render/town_scene_projection.hpp"
#include "engine/render/town_shadow.hpp"
#include "engine/render/town_model_import.hpp"
#include "engine/render/town_props.hpp"
#include "engine/render/town_volume.hpp"
#include "engine/render/town_visual_objects.hpp"
#include "engine/render/town_visual_scene.hpp"
#include "engine/render/town_vegetation.hpp"
#include "engine/render/town_actor.hpp"
#include "engine/render/town_body.hpp"
#include "engine/render/town_camera_collision.hpp"
#include "engine/render/town_actor_mask.hpp"
#include "engine/render/town_view_resolve.hpp"
#include "engine/render/town_presentation.hpp"
#include "engine/render/ui_overlay_regions.hpp"
#include "engine/surface.hpp"
#include "engine/trn.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/tile_properties.hpp"
#include "levels/trigs.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "options.h"
#include "player.h"
#include "quests.h"
#include "utils/endian_swap.hpp"
#include "utils/log.hpp"
#include "utils/palette_blending.hpp"
#include "towners.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace diag = d3d_local_diagnostics;
namespace {

constexpr float Pi = 3.14159265358979323846F;
constexpr float PixelsPerWorldUnit = 32.0F;
constexpr float NativeCameraScale = 45.25483399593904F; // 32 * sqrt(2)
constexpr float DefaultCameraDistance = 22.0F;
constexpr size_t ImportedLightLevels = 64;
constexpr float InteriorPointLightRange = 4.0F;
constexpr size_t FlameLightLevels = 16;
constexpr size_t InteriorMaterialCount = 5;
constexpr size_t FlameBandCount = 3;
constexpr float FireIntensityVariation = 0.06F;

// Drawing, picking and diagnostics share the effective session request.
bool EffectiveGpuRequested()
{
	return diag::SessionPolicy().mode != diag::Mode::CpuDiagnosticExplicit
	    && *GetOptions().Graphics.townViewGpuRendering;
}

/** Prepared once per room/frame, never allocated by the pixel lighting loop. */
struct InteriorLighting {
	const TownCabinInterior *room;
	std::array<TownPointLight, TownMaxPointLights> lights;
	size_t lightCount = 0;
};

struct GpuVolumeFallback {
	int palette = -1;
	int shade = 0;
};

struct Vec3 {
	float x;
	float y;
	float z;
	Vec3 operator+(Vec3 other) const { return { x + other.x, y + other.y, z + other.z }; }
	Vec3 operator-(Vec3 other) const { return { x - other.x, y - other.y, z - other.z }; }
	Vec3 operator*(float value) const { return { x * value, y * value, z * value }; }
};

float Dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(Vec3 a, Vec3 b)
{
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

struct Camera {
	TownCameraFrame projection;
	Vec3 target;
	Vec3 eye;
	Vec3 forward;
	Vec3 right;
	Vec3 up;
	float focal = 1;
	float centerX = 0;
	float centerY = 0;
	int width = 0;
	int height = 0;
};

struct Vertex {
	Vec3 position;
	float u;
	float v;
	std::array<float, 4> authoredColor { 1, 1, 1, 1 };
};

struct ProjectedVertex {
	float x;
	float y;
	float depth;
	float u;
	float v;
	float reciprocalW = 1;
};

uint64_t NextTextureGpuIdentity()
{
	static uint64_t next = 1;
	return next++;
}

// A moved/copied texture is a new immutable upload. Pointer addresses can be
// reused when an actor cache recycles, so they cannot identify GPU resources.
struct TextureGpuIdentity {
	uint64_t value = NextTextureGpuIdentity();
	TextureGpuIdentity() = default;
	TextureGpuIdentity(const TextureGpuIdentity &) : value(NextTextureGpuIdentity()) { }
	TextureGpuIdentity(TextureGpuIdentity &&) noexcept : value(NextTextureGpuIdentity()) { }
	TextureGpuIdentity &operator=(const TextureGpuIdentity &) { value = NextTextureGpuIdentity(); return *this; }
	TextureGpuIdentity &operator=(TextureGpuIdentity &&) noexcept { value = NextTextureGpuIdentity(); return *this; }
};

bool DrawingWarriorViewModel = false;
bool WarriorViewModelEnabledForDiagnostics = true;
std::vector<uint8_t> WarriorViewModelCoverage;

struct Texture {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> pixels;
	bool repeat = false;
	std::vector<uint8_t> opacity;
	std::vector<uint8_t> lightingBasePixels;
	// Imported base color stays independent of the game palette until lighting.
	std::vector<uint32_t> albedoPixels;
	// A separately owned actor surface keeps exact authored RGB codes and its
	// indexed/RGBA pair; it never joins the scenery six-bit color namespace.
	std::optional<PlayerActorMaterial> actorMaterial;
	// Interior materials use a small point-irradiance LUT and clean base colors.
	std::vector<std::array<uint8_t, ImportedLightLevels>> interiorLightTables;
	// Optional independent-channel RGB rows; legacy palette rows stay intact.
	std::vector<std::array<uint32_t, ImportedLightLevels>> interiorRgbLightTables;
	bool emissive = false;
	TextureGpuIdentity gpuIdentity;
	bool sample(float u, float v, uint8_t &color, uint32_t *albedoColor = nullptr, uint8_t *unlitColor = nullptr) const
	{
		if (width <= 0 || height <= 0 || !std::isfinite(u) || !std::isfinite(v))
			return false;
		if (repeat) {
			u -= std::floor(u);
			v -= std::floor(v);
		}
		if (!opacity.empty() && (u < 0 || v < 0 || u >= 1 || v >= 1))
			return false;
		const int x = std::clamp(static_cast<int>(u * static_cast<float>(width)), 0, width - 1);
		const int y = std::clamp(static_cast<int>(v * static_cast<float>(height)), 0, height - 1);
		const size_t index = static_cast<size_t>(y) * width + x;
		color = pixels.empty() ? 0 : pixels[index];
		if (actorMaterial) {
			const auto code = actorMaterial->texelCodes[index];
			color = actorMaterial->indexedLightLut[std::size_t(code) * actorMaterial->lightLevels];
			if (albedoColor != nullptr) *albedoColor = code;
		}
		if (unlitColor != nullptr) *unlitColor=lightingBasePixels.empty()?color:lightingBasePixels[index];
		if (albedoColor != nullptr && !albedoPixels.empty())
			*albedoColor = albedoPixels[index];
		return opacity.empty() || opacity[index] != 0;
	}
};

struct NativeSceneFace {
	Texture texture;
	Point pixelOrigin;
};

struct NativeSceneArt {
	Texture texture;
	Point referenceTile;
	Point pixelOrigin;
	std::vector<int32_t> projectionOwners;
	std::vector<NativeSceneFace> projectedFaces;
};

void CompleteOpaqueMaterial(Texture &texture, const std::vector<uint8_t> &coverage, uint8_t fallback);

struct TileArt {
	Texture ground;
	Texture facade;
	Texture original;
	float height = 0;
	bool solid = false;
};

struct ResidentMeshRange {
	uint32_t firstIndex = 0;
	uint32_t indexCount = 0;
	uint8_t textureView = 0;
};

struct ResidentMeshArtwork {
	// Uses the same monotonic allocator as texture generations, never addresses.
	TextureGpuIdentity generation;
	std::vector<TownGpuMeshVertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<ResidentMeshRange> ranges;
	bool ready = false;
	TownGpuMeshIdentity identity() const { return { generation.value, 1 }; }
};

struct VolumeArtwork {
	Texture texture;
	TownVolumeMesh mesh;
	uint64_t sourceHash = 0;
	std::array<Texture, 8> directionalTextures;
	bool physicalFrame = false;
	Texture shadow;
	mutable Vec3 minimum {};
	mutable Vec3 maximum {};
	mutable bool boundsReady = false;
	mutable std::map<std::array<float, 3>, ResidentMeshArtwork> residentPlacements;
};

// These caches borrow a validated persistent archive, never the native dungeon
// allocation. Resource/scene epochs invalidate them without rewriting assets.
std::string FixedTownArchiveHash;
uint64_t FixedTownSceneRevision = 0;
std::vector<Texture> FixedTownTextures;
std::map<size_t, VolumeArtwork> FixedTownVolumes;
struct FixedTownDelta { uint64_t signature = 0; VolumeArtwork art; };
std::map<size_t, FixedTownDelta> FixedTownDeltas;
TownViewFixedObjectState FixedTownObjectState;

void ClearFixedTownRendererCache()
{
	FixedTownArchiveHash.clear();
	FixedTownSceneRevision = 0;
	FixedTownTextures.clear();
	FixedTownVolumes.clear();
	FixedTownDeltas.clear();
	FixedTownObjectState = {};
}

enum class PickKind : uint8_t { Ground, Towner, Item, Player, Object, Monster };
struct PickRecord {
	uint16_t tile = std::numeric_limits<uint16_t>::max();
	int16_t entity = -1;
	PickKind kind = PickKind::Ground;
	int16_t architecture = -1;
	bool preservePicking = false;
};

bool Enabled = false;
bool PickingValid = false;
uint64_t ItemLabelFrameId = 0;
std::array<TownViewItemLabelAnchor, MAXITEMS> ItemLabelAnchors;
size_t ItemLabelAnchorCount = 0;
cathedral::PilotFrame CathedralFrame;
bool CathedralFrameReady = false;
bool DrawingCathedral = false;
size_t CorpseGroundDraws=0;
bool CathedralPaletteBlend = false;
const uint8_t *CathedralTranslation = nullptr;

const std::vector<TownSceneModel> &ViewCollisionScene()
{
	return cathedral::LiveLevelEligible() ? CathedralFrame.collisionModels : GetTownScene();
}

uint64_t ViewSceneRevision()
{
	return cathedral::LiveLevelEligible() ? cathedral::LiveSceneRevision() : GetTownSceneRevision();
}
int PickScreenWidth = 0;
int PickScreenHeight = 0;
int PickViewportHeight = 0;
bool PickZoom = false;
bool PickLeftPanel = false;
bool PickRightPanel = false;
bool PickGpuRequested = false;
bool PickFrustumCullingRequested = false;
uint64_t PickSceneRevision = 0;
uint64_t PickCameraRevision = 0;
bool PickHorizonEnabled = false;
TownCameraRig CameraRig;
// Gameplay camera policy is separate from the retained four-mode diagnostics.
bool GameplayCameraPolicy = false;
TownCameraVisibilityIndex CameraVisibilityIndex;
TownViewCameraVisibilityState CameraVisibilityState;
std::vector<uint8_t> CameraHiddenArchitecture, CameraVisibilityQueryMask;
TownCameraVisibilityIndex CathedralVisibilityIndex;
std::unordered_map<uint64_t, uint32_t> CathedralVisibilityOwners;
std::vector<uint8_t> CathedralHiddenOwners;
uint64_t CathedralVisibilityGeometry = 0, CathedralVisibilityPresentation = 0;
uint64_t CameraVisibilitySceneRevision = 0;
bool CameraVisibilityBuildAttempted = false;
uint64_t CameraVisibilityRevision = 0, PickCameraVisibilityRevision = 0;
size_t CameraVisibilityBuilds = 0;
TownCameraCollisionIndex CameraCollisionIndex;
uint64_t CameraCollisionSceneRevision = std::numeric_limits<uint64_t>::max();
uint64_t FollowProjectionRevision = 0, PickFollowProjectionRevision = 0;
TownViewFollowCameraState FollowCameraState;
TownCameraPoint LastSafeCameraEye {}, LastCameraAnchor {};
bool HasSafeCameraEye = false;
float CameraFollowFrameSeconds = 0;
size_t CameraCollisionBuilds = 0;
float CameraYaw = Pi * 0.25F;
float CameraDistance = 22.0F;
float CameraPitch = Pi / 6;
Vec3 CameraPanOffset { 0, 0, 0 };
bool CameraDragging = false;
bool CameraDragPans = false;
Point CameraDragLastPosition;
Camera ViewCamera;
TownViewSamplingState SamplingState;
int RasterSampleFactor = 1;
std::unique_ptr<OwnedSurface> SamplingSurface;
TownViewColorResolve SamplingColors;
constexpr size_t MaxSmoothWorldPixels = 4 * 1024 * 1024;
std::vector<float> DepthBuffer;
std::vector<PickRecord> PickBuffer;
TownViewRendererState RendererState;
bool CaptureGpu = false;
bool CaptureGpuFailed = false;
bool ResidentMeshesEnabledForDiagnostics = true;
std::array<float, 2> RasterJitterForDiagnostics {};
TownGpuRecoveryPolicy GpuRecovery;
std::string GpuFailure;
uint64_t GpuFrameNumber = 0;
TownGpuFrame GpuFrame;
std::vector<PickRecord> GpuPickRecords;
std::unordered_map<uint64_t, uint32_t> GpuPickIds;
struct PreparedGpuTexture {
	std::vector<uint32_t> codes;
	std::vector<uint8_t> interiorLut;
	std::vector<uint32_t> interiorRgbLut;
	std::array<SDL_Color, 256> interiorRgbTone {};
	uint64_t interiorRgbRevision = 0;
	uint64_t lastSeen = 0;
	uint64_t actorPreparedFrame = 0;
	TownGpuLighting actorPreparedLighting = TownGpuLighting::Unlit;
};
std::unordered_map<uint64_t, PreparedGpuTexture> PreparedGpuTextures;
std::vector<uint32_t> SmoothWorldPixels;
std::unique_ptr<OwnedSurface> SmoothWorldSurface;
std::array<uint32_t,256> SmoothWorldPalette {};
std::array<SDL_Color,256> SmoothToneRamp {};
bool SmoothWorldValid=false;
float SmoothNativeLight(Vec3 world) {
    const float x=std::clamp(world.x,0.0F,float(MAXDUNX-1)),z=std::clamp(world.z,0.0F,float(MAXDUNY-1));
    const int ax=int(x),az=int(z),bx=std::min(ax+1,MAXDUNX-1),bz=std::min(az+1,MAXDUNY-1);
    const auto at=[](int x,int z){return float(std::min<int>(dLight[x][z],15));};
    const float top=at(ax,az)+(at(bx,az)-at(ax,az))*(x-ax);
    const float bottom=at(ax,bz)+(at(bx,bz)-at(ax,bz))*(x-ax);
    return top+(bottom-top)*(z-az);
}
float SmoothShadowOcclusion(Vec3 world,const TownShadowReceiver &receiver) {
    const auto view=GetTownShadowMapView();if(view.depth.empty())return 0;
    const float u=(world.x*view.right.x+world.y*view.right.height+world.z*view.right.z-view.minU)/view.texelU;
    const float v=(world.x*view.up.x+world.y*view.up.height+world.z*view.up.z-view.minV)/view.texelV;
    if(u<0 || v<0 || u>=view.config.resolution || v>=view.config.resolution)return 0;
    const float fu=u-std::floor(u),fv=v-std::floor(v);
    const auto sample=[&](float du,float dv){
        const float plane=receiver.depthSlopeU*du*view.texelU+receiver.depthSlopeV*dv*view.texelV;
        return SampleTownShadow(world.x+view.right.x*du*view.texelU+view.up.x*dv*view.texelV+view.light.x*plane,
            world.y+view.right.height*du*view.texelU+view.up.height*dv*view.texelV+view.light.height*plane,
            world.z+view.right.z*du*view.texelU+view.up.z*dv*view.texelV+view.light.z*plane,receiver);
    };
    const float a=sample(-fu,-fv),b=sample(1-fu,-fv),c=sample(-fu,1-fv),d=sample(1-fu,1-fv);
    return (a+(b-a)*fu)*(1-fv)+(c+(d-c)*fu)*fv;
}
uint32_t ToneWorldRgb(TownLightColor color) {
    const auto r=SmoothToneRamp[std::clamp(int(color.red*255+.5F),0,255)].r;
    const auto g=SmoothToneRamp[std::clamp(int(color.green*255+.5F),0,255)].g;
    const auto b=SmoothToneRamp[std::clamp(int(color.blue*255+.5F),0,255)].b;
    return 0xFF000000U|(uint32_t(r)<<16)|(uint32_t(g)<<8)|b;
}
uint32_t BlendWorldRgb(uint32_t a,uint32_t b,float amount) {
    amount=std::clamp(amount,0.0F,1.0F);uint32_t color=0xFF000000U;
    for(unsigned shift:{0U,8U,16U}) {const float low=float((a>>shift)&255),high=float((b>>shift)&255);color|=uint32_t(low+(high-low)*amount+0.5F)<<shift;}
    return color;
}

struct CathedralGpuPaletteBinding {
	TextureGpuIdentity identity;
	cathedral::NativePaletteRevision palette;
	uint64_t lastPreparedFrame = 0;
};
std::unordered_map<const uint8_t *, CathedralGpuPaletteBinding> GpuCathedralLuts;
std::array<uint8_t, 256 * 256> GpuCathedralBlendLookup {};
TextureGpuIdentity GpuCathedralBlendIdentity;
uint64_t GpuCathedralBlendRevision = 0, GpuCathedralBlendEpoch = 0;
bool GpuCathedralBlendValid = false;
TownViewCathedralAllocationState CathedralAllocationState;
std::vector<uint8_t> GpuOrdinaryLut;
std::vector<uint8_t> GpuImportedLut;
size_t GpuImportedLutColors = 0;
std::unordered_map<uint16_t, TileArt> TerrainCache;
// Dedicated Cathedral namespace. No Town shadow masks, donor heuristics or
// decoded facades enter this cache. Nodes retain immutable Texture identities.
using CathedralNativeTextureKey = std::tuple<cathedral::NativeTextureKind, uint16_t, cathedral::NativeTextureAxis, uint8_t, int, cathedral::NativeTextureLayout>;
struct CathedralNativeTextureEntry {
	Texture texture;
	uint64_t signature = 0, validatedAt = 0;
	uint64_t authoredPixelHash = 0;
	uint8_t authoredMaterial = 0; // 1: own floor r1; 2: own opaque wall r1.
	uint16_t sourcePiece = 0;
	uint8_t sourceMicro = 0, sourceColumn = 0;
	uint16_t sourceBlock = 0, secondBlock = 0;
	int cropX = 0, cropY = 0;
	bool approximate = false;
	bool horizontalFlip = false;
	std::array<uint16_t, 16> upperWords {};
};
constexpr size_t CathedralNativeTextureMaxEntries = 512;
constexpr size_t CathedralNativeTextureMaxBytes = 8 * 1024 * 1024;
// Immutable r1 LOD identities, qualified against the original-only asset manifest.
constexpr uint64_t CathedralOwnFloorPixelHash = 0x19fb0138bd10258bULL;
constexpr uint64_t CathedralOwnWallPixelHash = 0x7ceac50e7ad3f981ULL;
bool CathedralOwnFloorPresent = false, CathedralOwnWallPresent = false;
// Raw keys keep their existing 512-entry cap; lit keys add coordinates without
// multiplying CEL decodes. Only this frame's lit sources are resident (max512).
// The raw+lit pixel/coverage payload shares the existing 8 MiB byte budget.
using CathedralNativeLitTextureKey = std::tuple<CathedralNativeTextureKey, int, int, cathedral::NativeTextureOperationPass>;
struct CathedralNativeLitTextureEntry {
	Texture texture;
	uint64_t rawSignature = 0, rawGpuIdentity = 0, lightSignature = 0;
};
constexpr size_t CathedralNativeLitTextureMaxEntries = 512;
constexpr int CathedralNativeLightCanvasWidth = 64, CathedralNativeLightCanvasHeight = 192;
struct CathedralNativeLightScratch {
	std::array<uint8_t, CathedralNativeLightCanvasWidth * CathedralNativeLightCanvasHeight> canvas {};
	// buildLocal accepts MicroTileLen<=16; include its maximum native margin.
	std::array<uint8_t, CathedralNativeLightCanvasWidth * (CathedralNativeLightCanvasHeight + 32 * 9)> light {};
	std::array<uint8_t, 64 * 32> bleed {};
};
// Metadata is captured from the loaded native tables in the owning draw thread.
// No receipt, SHA, per-cell approval, RNG, resource load or native write is used.
std::vector<cathedral::native_cell_art::NativeObjectMetadata> CaptureCathedralNativeLightObjects(const cathedral::Snapshot &snapshot)
{
	if (snapshot.gameRevision != cathedral::LiveEpoch() || ActiveObjectCount < 0 || ActiveObjectCount > MAXOBJECTS)
		throw std::invalid_argument("Cathedral native object metadata epoch/count changed");
	std::vector<cathedral::native_cell_art::NativeObjectMetadata> result;
	std::array<bool, MAXOBJECTS> seen {};
	for (int index = 0; index < ActiveObjectCount; ++index) {
		const int slot = ActiveObjects[index];
		if (slot < 0 || slot >= MAXOBJECTS || seen[slot])
			throw std::invalid_argument("Cathedral native active object list is invalid");
		seen[slot] = true;
		const Object &object = Objects[slot];
		if (object._otype != OBJ_L1LIGHT || object._oDelFlag) continue;
		const int x = object.position.x, z = object.position.y;
		if (x < snapshot.minX || x >= snapshot.maxX || z < snapshot.minZ || z >= snapshot.maxZ) continue;
		const auto &cell = snapshot.At(x, z);
		if (cell.piece != 269) continue; // Native AddL1Objs predicate, not a MIN encoding heuristic.
		if (dPiece[x][z] != cell.piece || dObject[x][z] != cell.object
		    || std::abs(static_cast<int>(cell.object)) != slot + 1
		    || object._oAnimFrame > static_cast<uint32_t>(std::numeric_limits<int>::max()))
			throw std::invalid_argument("Cathedral native light footprint differs from the current snapshot");
		cathedral::native_cell_art::NativeObjectMetadata metadata;
		metadata.epoch = snapshot.gameRevision; metadata.slot = slot;
		metadata.sourceX = x; metadata.sourceZ = z; metadata.originX = x; metadata.originZ = z;
		metadata.nativeType = object._otype; metadata.signedOccupancy = cell.object;
		metadata.active = true; metadata.authoritativeNativeRecord = true;
		// AddL1Objs: piece269 -> OBJ_L1LIGHT. AddObjectLight/UpdateObjectLight:
		// OBJ_L1LIGHT radius5. applyLighting describes sprite shading, not emission.
		metadata.nativeEmitterRuleMatched = true; metadata.emitterRuleRadius = 5;
		metadata.appliesLighting = object.applyLighting; metadata.preDraw = object._oPreFlag;
		metadata.nativeSolid = object._oSolidFlag; metadata.animationFrame = static_cast<int>(object._oAnimFrame);
		metadata.nativeLightId = object._olid;
		result.push_back(metadata);
	}
	return result;
}

std::vector<cathedral::native_fence_runtime::NativeMegaWitness> CaptureCathedralKnownFenceMegas(
    const cathedral::Snapshot &snapshot, const cathedral::PilotFrame &frame)
{
	if (snapshot.gameRevision != cathedral::LiveEpoch() || !pMegaTiles)
		throw std::invalid_argument("Cathedral native fence table is unavailable or stale");
	std::vector<cathedral::native_fence_runtime::NativeMegaWitness> result;
	for (const auto &surface : frame.nativeSurfaces) {
		if (surface.module != cathedral::ModuleKind::Fence || surface.sourceModule != cathedral::ModuleKind::Fence) continue;
		if (surface.sources.size() != 4) throw std::invalid_argument("Cathedral native fence source count changed");
		int x = cathedral::GridSize, z = cathedral::GridSize;
		for (const auto &source : surface.sources) { x = std::min(x, source.x); z = std::min(z, source.z); }
		if (x < snapshot.minX || x + 1 >= snapshot.maxX || z < snapshot.minZ || z + 1 >= snapshot.maxZ
		    || (x & 1) != 0 || (z & 1) != 0)
			throw std::invalid_argument("Cathedral native fence macro origin is invalid");
		const int megaX = (x - 16) / 2, megaZ = (z - 16) / 2;
		const uint8_t raw = snapshot.mega[static_cast<size_t>(megaZ) * cathedral::MegaSize + megaX];
		if (raw != 35 && raw != 36) continue; // Native VFence / HFence; mixed/end variants stay legacy.
		if (dungeon[megaX][megaZ] != raw) throw std::invalid_argument("Cathedral native fence macro differs from snapshot");
		const MegaTile &mega = pMegaTiles[raw - 1]; // Same loaded TIL indexing as DRLG_LPass3.
		const std::array<uint16_t, 4> loaded { Swap16LE(mega.micro1), Swap16LE(mega.micro2), Swap16LE(mega.micro3), Swap16LE(mega.micro4) };
		cathedral::native_fence_runtime::NativeMegaWitness witness;
		witness.epoch = snapshot.gameRevision; witness.rawMega = raw; witness.originX = x; witness.originZ = z;
		for (unsigned child = 0; child < 4; ++child) {
			if (loaded[child] >= MAXTILES)
				throw std::invalid_argument("Cathedral native fence TIL child is invalid");
			witness.zeroBasedNativePieces[child] = loaded[child]; // Exact DRLG_LPass3 assignment, with no inferred decrement.
			const int childX = x + static_cast<int>(child % 2), childZ = z + static_cast<int>(child / 2);
			if (snapshot.At(childX, childZ).piece != witness.zeroBasedNativePieces[child]
			    || dPiece[childX][childZ] != witness.zeroBasedNativePieces[child])
				throw std::invalid_argument("Cathedral native fence loaded TIL mapping differs from current piece");
		}
		witness.loadedNativeTilMappingVerified = true;
		result.push_back(witness);
	}
	return result;
}
std::map<CathedralNativeTextureKey, CathedralNativeTextureEntry> CathedralNativeTextureCache;
std::map<CathedralNativeLitTextureKey, CathedralNativeLitTextureEntry> CathedralNativeLitTextureCache;
size_t CathedralNativeLitTextureBytes = 0;
bool CathedralNativeLitTexturesReady = false;
uint64_t CathedralNativeLitFrameLightSignature = 0;
uint64_t CathedralNativeLitGameRevision = 0, CathedralNativeLitGeometryRevision = 0, CathedralNativeLitPresentationSignature = 0;
TownViewCathedralNativeTextureState CathedralNativeTextureState;
const std::byte *CathedralNativeTextureResource = nullptr;
uint_fast8_t CathedralNativeTextureMicroLen = 0;
cathedral::NativeTextureBinding CathedralCeilingBinding;
uint64_t CathedralCommonPaletteSignature = 0;
bool CathedralCeilingBindingReady = false;
std::unique_ptr<VolumeArtwork> CathedralNativeLightBody;
TownViewCathedralFallbackDiagnostic CathedralFallbackDiagnostic;
TownViewCathedralFallbackDiagnostic CathedralFallbackLastLogged;
CathedralFallbackStage CathedralDiagnosticStage = CathedralFallbackStage::None;
CathedralFallbackStage CathedralDiagnosticGpuRefusalStage = CathedralFallbackStage::None;
cathedral::NativeTextureBinding CathedralDiagnosticMaterialBinding;
bool CathedralDiagnosticMaterialBindingValid = false;
bool CathedralPreparationRefused = false, CathedralDiagnosticGpuAttempted = false;
bool CathedralDiagnosticForceGeometry = false, CathedralDiagnosticHasLogKey = false;
bool CathedralDiagnosticHasLogAttempt = false;
uint32_t CathedralDiagnosticLastLogAttempt = 0;
TownViewCathedralFirstRefusal CathedralFirstRefusal;
bool CathedralFirstRefusalLogged = false, CathedralFirstRefusalLogAttempted = false;
uint32_t CathedralFirstRefusalLastAttempt = 0;

bool SameCathedralDiagnosticTransition(const TownViewCathedralFallbackDiagnostic &a,
    const TownViewCathedralFallbackDiagnostic &b) noexcept
{
	// Moving focus/counters are evidence, not a new refusal or recovery.
	return a.reason == b.reason && a.stage == b.stage && a.level == b.level
	    && a.enabled == b.enabled && a.nativeFallback == b.nativeFallback
	    && a.levelType == b.levelType && a.setLevel == b.setLevel && a.setLevelId == b.setLevelId
	    && a.liveEpoch == b.liveEpoch && a.frameFallbackFlags == b.frameFallbackFlags
	    && a.seedValid == b.seedValid && a.seed == b.seed
	    && a.materialBindingValid == b.materialBindingValid && a.materialKind == b.materialKind
	    && a.materialAxis == b.materialAxis && a.materialPiece == b.materialPiece
	    && a.materialColumn == b.materialColumn && a.materialNativeSlot == b.materialNativeSlot
	    && a.requestedGpu == b.requestedGpu && a.forceGeometry == b.forceGeometry
	    && a.gpuFailureKind == b.gpuFailureKind && a.recoveryFailureKind == b.recoveryFailureKind
	    && a.gpuFailureText == b.gpuFailureText;
}

void RecordCathedralDiagnostic(CathedralFallbackReason reason, CathedralFallbackStage stage,
    bool nativeFallback, bool requestedGpu) noexcept
{
	TownViewCathedralFallbackDiagnostic next;
	next.reason = reason; next.stage = stage; next.nativeFallback = nativeFallback && Enabled;
	next.enabled = Enabled; next.eligible = cathedral::LiveLevelEligible(); next.playerPresent = MyPlayer != nullptr;
	next.requestedGpu = requestedGpu; next.usedGpu = RendererState.usedGpu;
	next.gpuAttempted = CathedralDiagnosticGpuAttempted; next.forceGeometry = CathedralDiagnosticForceGeometry;
	next.level = currlevel; next.levelType = static_cast<int>(leveltype);
	next.setLevel = setlevel; next.setLevelId = static_cast<int>(setlvlnum);
	const int levelIndex = static_cast<int>(currlevel);
	if (levelIndex >= 0 && levelIndex < NUMLEVELS) { next.seed = DungeonSeeds[levelIndex]; next.seedValid = true; }
	if (MyPlayer != nullptr) { next.focusX = MyPlayer->position.tile.x; next.focusZ = MyPlayer->position.tile.y; }
	next.tick = static_cast<uint32_t>(SDL_GetTicks()); next.liveEpoch = cathedral::LiveEpoch(); next.frameGeometryRevision = CathedralFrame.geometryRevision;
	next.frameReady = CathedralFrameReady; next.frameRequiresFallback = CathedralFrame.requiresNativeFallback;
	next.frameFallbackFlags = CathedralFrame.fallbackReasons; next.frameEpoch = CathedralFrame.gameRevision;
	next.regionX = CathedralFrame.regionX; next.regionZ = CathedralFrame.regionZ;
	next.regions = CathedralFrame.regions; next.frameTriangles = CathedralFrame.triangles.size();
	next.nativeSpecialOverlays = CathedralFrame.nativeSpecialOverlays.size();
	next.materialBindingValid = stage == CathedralFallbackStage::NativeMaterials && CathedralDiagnosticMaterialBindingValid;
	if (next.materialBindingValid) {
		const auto &binding = CathedralDiagnosticMaterialBinding;
		next.materialKind = static_cast<int>(binding.kind); next.materialAxis = static_cast<int>(binding.axis);
		next.materialPiece = binding.piece; next.materialColumn = binding.column; next.materialNativeSlot = binding.nativeSlot;
		next.materialX = binding.x; next.materialZ = binding.z;
	} else {
		CathedralDiagnosticMaterialBindingValid = false;
	}
	next.pickingValid = PickingValid;
	next.hostEpoch = CathedralNativeTextureState.epoch;
	next.hostEntries = CathedralNativeTextureCache.size() + CathedralNativeLitTextureCache.size();
	next.hostBytes = CathedralNativeTextureState.bytes + CathedralNativeLitTextureBytes;
	next.hostEntryLimit = CathedralNativeTextureMaxEntries + CathedralNativeLitTextureMaxEntries;
	next.hostByteLimit = CathedralNativeTextureMaxBytes; next.hostDecodes = CathedralNativeTextureState.decodes;
	next.hostHits = CathedralNativeTextureState.hits; next.hostMisses = CathedralNativeTextureState.misses;
	const auto &gpu = GetTownGpuStatus(); // Borrow strings; never copy them during recovery.
	next.gpuAvailable = gpu.available; next.gpuFrameSucceeded = gpu.frameSucceeded;
	next.gpuFailureKind = static_cast<int>(gpu.failureKind); next.recoveryFailureKind = static_cast<int>(GpuRecovery.failureKind());
	const size_t failureLength = std::min(gpu.failure.size(), next.gpuFailureText.size() - 1);
	if (failureLength != 0) std::memcpy(next.gpuFailureText.data(), gpu.failure.data(), failureLength);
	next.gpuFailureTextTruncated = gpu.failure.size() > failureLength;
	next.gpuTextures = gpu.cachedTextures; next.gpuTextureBytes = gpu.cachedTextureBytes;
	next.gpuTriangles = gpu.submittedTriangles; next.gpuDrawCalls = gpu.drawCalls;
	next.gpuOverlayTriangles = gpu.paletteBlendTriangles; next.gpuOverlayCopyBytes = gpu.paletteBlendCopyBytes;
	next.observations = CathedralFallbackDiagnostic.observations + 1;
	next.transitions = CathedralFallbackDiagnostic.transitions;
	next.logLines = CathedralFallbackDiagnostic.logLines; next.logFailures = CathedralFallbackDiagnostic.logFailures;
	if (SameCathedralDiagnosticTransition(next, CathedralFallbackDiagnostic)) {
		next.firstFocusX = CathedralFallbackDiagnostic.firstFocusX; next.firstFocusZ = CathedralFallbackDiagnostic.firstFocusZ;
		next.firstTick = CathedralFallbackDiagnostic.firstTick;
	} else {
		++next.transitions; next.firstFocusX = next.focusX; next.firstFocusZ = next.focusZ; next.firstTick = next.tick;
	}
	CathedralFallbackDiagnostic = next; // Fixed-size POD; no allocation, text or exception formatting.
	if (!CathedralFirstRefusal.present && next.nativeFallback
	    && !gbIsSpawn && cathedral::SupportsRetailLevel(currlevel, leveltype, gbIsHellfire, setlevel, setlevel ? setlvlnum : SL_NONE)
	    && reason != CathedralFallbackReason::NativePoseReference && reason != CathedralFallbackReason::RenderDisabled) {
		const auto input = GetTownViewInputStateForDiagnostics();
		CathedralFirstRefusal.present = true;
		CathedralFirstRefusal.observation = next;
		CathedralFirstRefusal.cameraMode = static_cast<int>(CameraRig.mode());
		CathedralFirstRefusal.yaw = CameraYaw; CathedralFirstRefusal.pitch = CameraPitch;
		CathedralFirstRefusal.distance = CameraDistance;
		CathedralFirstRefusal.verticalFovDegrees = CameraRig.preferences().verticalFovDegrees;
		CathedralFirstRefusal.firstPersonPhase = input.firstPersonPhase;
		CathedralFirstRefusal.thirdPersonPhase = input.thirdPersonPhase;
		CathedralFirstRefusal.firstPersonKeys = input.firstPersonKeys;
		CathedralFirstRefusal.thirdPersonKeys = input.thirdPersonKeys;
		CathedralFirstRefusal.leftDown = input.leftDown; CathedralFirstRefusal.active = input.active;
		if (MyPlayer != nullptr) {
			CathedralFirstRefusal.playerMode = static_cast<int>(MyPlayer->_pmode);
			CathedralFirstRefusal.playerChangingLevel = MyPlayer->_pLvlChanging;
		}
	}
}

void TryLogCathedralDiagnostic() noexcept
{
	const auto &first = CathedralFirstRefusal;
	if (first.present && !CathedralFirstRefusalLogged
	    && (!CathedralFirstRefusalLogAttempted || SDL_GetTicks() - CathedralFirstRefusalLastAttempt >= 1000)) {
		CathedralFirstRefusalLogAttempted = true;
		CathedralFirstRefusalLastAttempt = SDL_GetTicks();
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		try {
			const auto &d = first.observation;
			Log("Cathedral FIRST refusal: reason={}, stage={}, tick={}, level={}/{}, seed={}, epoch={}, frameEpoch={}, focus=({},{}), camera={} yaw={} pitch={} distance={} fov={}, input=FP{}:{} TP{}:{} left={} active={}, playerMode={} changing={}, ready={} flags={} GPUrequested={} kind={}",
				CathedralFallbackReasonName(d.reason), CathedralFallbackStageName(d.stage), d.tick,
				d.level, d.levelType, d.seed, d.liveEpoch, d.frameEpoch, d.focusX, d.focusZ,
				first.cameraMode, first.yaw, first.pitch, first.distance, first.verticalFovDegrees,
				first.firstPersonPhase, first.firstPersonKeys, first.thirdPersonPhase, first.thirdPersonKeys,
				first.leftDown, first.active, first.playerMode, first.playerChangingLevel,
				d.frameReady, d.frameFallbackFlags, d.requestedGpu, d.gpuFailureKind);
			CathedralFirstRefusalLogged = true;
		} catch (...) { ++CathedralFallbackDiagnostic.logFailures; }
#endif
	}
	const auto &d = CathedralFallbackDiagnostic;
	if (d.reason == CathedralFallbackReason::None) return;
	if (!d.enabled || d.reason == CathedralFallbackReason::RenderDisabled || d.reason == CathedralFallbackReason::NativePoseReference
	    || d.reason == CathedralFallbackReason::ContextIneligible
	    || (d.reason == CathedralFallbackReason::Published3D && (!CathedralDiagnosticHasLogKey || !CathedralFallbackLastLogged.nativeFallback))) {
		// Intentional native rendering is not a defect. Only actual publication
		// (never successful preparation) can acknowledge GPU/draw recovery.
		CathedralFallbackLastLogged = d; CathedralDiagnosticHasLogKey = true;
		return;
	}
	if (CathedralDiagnosticHasLogKey && SameCathedralDiagnosticTransition(d, CathedralFallbackLastLogged)) return;
	// At most one formatting attempt per second, even if logging itself fails.
	// A changed classification remains pending; first focus survives moving tiles.
	if (CathedralDiagnosticHasLogAttempt && d.tick - CathedralDiagnosticLastLogAttempt < 1000) return;
	CathedralDiagnosticLastLogAttempt = d.tick; CathedralDiagnosticHasLogAttempt = true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
		Log("Cathedral pilot diagnostic: reason={}, stage={}, native={}, tick={}, firstTick={}, level={}/{}, set={}/{}, seed={}/{}, firstFocus=({},{}), focus=({},{}), epoch={}, frameEpoch={}, ready={}, flags={}, region=({},{})/{}, frameTriangles={}, special={}, material={}/{}/{}/{}/{}/{}@({},{}), host={}/{} entries {}/{}B, hostEpoch={}, GPUrequested={}, attempted={}, used={}, backendKind={}, recoveryKind={}, textures={}, textureBytes={}, triangles={}, draws={}, overlays={}, copy={}B, backendText={}, textTruncated={}",
			CathedralFallbackReasonName(d.reason), CathedralFallbackStageName(d.stage), d.nativeFallback, d.tick, d.firstTick,
			d.level, d.levelType, d.setLevel, d.setLevelId, d.seedValid, d.seed, d.firstFocusX, d.firstFocusZ, d.focusX, d.focusZ,
			d.liveEpoch, d.frameEpoch, d.frameReady, d.frameFallbackFlags, d.regionX, d.regionZ, d.regions, d.frameTriangles, d.nativeSpecialOverlays,
			d.materialBindingValid, d.materialKind, d.materialPiece, d.materialAxis, d.materialColumn, d.materialNativeSlot, d.materialX, d.materialZ,
			d.hostEntries, d.hostEntryLimit, d.hostBytes, d.hostByteLimit,
			d.hostEpoch, d.requestedGpu, d.gpuAttempted, d.usedGpu, d.gpuFailureKind, d.recoveryFailureKind,
			d.gpuTextures, d.gpuTextureBytes, d.gpuTriangles, d.gpuDrawCalls, d.gpuOverlayTriangles, d.gpuOverlayCopyBytes,
			d.gpuFailureText.data(), d.gpuFailureTextTruncated);
		++CathedralFallbackDiagnostic.logLines;
		CathedralFallbackLastLogged = CathedralFallbackDiagnostic; CathedralDiagnosticHasLogKey = true;
	} catch (...) {
		// Logging may allocate; its failure must never obscure or change fallback.
		++CathedralFallbackDiagnostic.logFailures;
	}
#endif
}

std::unordered_map<uint16_t, Texture> SceneGroundCache;
std::unordered_map<uint16_t, Texture> NativeBridgeTextureCache;
std::unordered_map<size_t, NativeSceneArt> SceneArtworkCache;
struct SceneDetailMaterial {
	Texture texture;
	float originU = 0;
	float originV = 0;
	float sizeU = 2;
	float sizeV = 2;
};
struct SceneMaterials {
	std::array<Texture, 5> base;
	std::array<SceneDetailMaterial, static_cast<size_t>(TownSceneSurfaceDetail::Count)> details;
};
std::unordered_map<size_t, SceneMaterials> SceneMaterialCache;
std::unordered_map<size_t, bool> SceneWaterEventState;
// Derived Water-only textures own fresh immutable GPU identities. Source
// texture identity distinguishes the opaque base from each projected mask.
std::unordered_map<size_t, std::array<std::unordered_map<uint64_t, Texture>, 2>> WellWaterMaterialTextureCache;
std::unordered_map<size_t, Texture> ImportedTextureCache;
std::unordered_map<size_t, std::array<Texture, InteriorMaterialCount + FlameBandCount * FlameLightLevels>> CabinInteriorTextureCache;
struct CachedArchitectureBounds {
	TownArchitectureBounds bounds;
	bool ready = false;
};
std::vector<CachedArchitectureBounds> ArchitectureBoundsCache;
uint64_t ArchitectureBoundsRevision = std::numeric_limits<uint64_t>::max();
TownViewArchitectureCullingState ArchitectureCullingState;
bool CabinFireEnabledForDiagnostics = true;
bool DirectionalShadowsEnabledForDiagnostics = true;
double CabinFireDiagnosticTime = -1;
double FrameFireTime = 0;
std::array<TownPointLight, TownMaxPointLights> FrameSpritePointLights {};
size_t FrameSpritePointLightCount = 0;
std::vector<TownLightOccluder> FrameSpritePointOccluders;
bool FrameSpritePointLightingValid = false;
const TownCabinInterior *FrameSpritePointChurchRoom = nullptr;
int16_t FrameSpritePointChurchArchitecture = -1;

bool SpritePointReceiver(PickRecord pick)
{
	return !DrawingCathedral && pick.kind != PickKind::Ground;
}

void ClearFrameSpritePointLighting()
{
	FrameSpritePointLightingValid = false;
	FrameSpritePointLightCount = 0;
	FrameSpritePointOccluders.clear();
	FrameSpritePointChurchRoom = nullptr;
	FrameSpritePointChurchArchitecture = -1;
}

void PrepareFrameSpritePointLighting()
{
	ClearFrameSpritePointLighting();
	if (DrawingCathedral || leveltype != DTYPE_TOWN || !CabinFireEnabledForDiagnostics)
		return;
	// The church's authored emitters also illuminate native entity surfaces.
	// Its room owns the aperture spans for this frame; reset clears them first.
	const auto &scene = GetTownScene();
	for (size_t index = 0; index < scene.size(); ++index) {
		const TownSceneModel &model = scene[index];
		if (model.kind != TownSceneKind::Cathedral || !model.cabinInterior)
			continue;
		const TownCabinInterior &room = *model.cabinInterior;
		FrameSpritePointChurchRoom = &room;
		FrameSpritePointChurchArchitecture = static_cast<int16_t>(index);
		if (room.fireSources.size() > TownMaxPointLights - FrameSpritePointLightCount
		    || room.lightOccluders.size() > TownMaxLightOccluders - FrameSpritePointOccluders.size()) {
			ClearFrameSpritePointLighting();
			return;
		}
		for (const TownCabinFireSource &fire : room.fireSources)
			FrameSpritePointLights[FrameSpritePointLightCount++] = fire.kind == TownCabinFireKind::EditorPoint
			    ? fire.light : TownFireLightAtTime(fire.light, fire.flickerSeed, FrameFireTime);
		FrameSpritePointOccluders.insert(FrameSpritePointOccluders.end(), room.lightOccluders.begin(), room.lightOccluders.end());
	}
	FrameSpritePointLightingValid = FrameSpritePointLightCount != 0
	    && ValidateTownSpritePointLighting({ FrameSpritePointLights.data(), FrameSpritePointLightCount }, FrameSpritePointOccluders);
}

std::unordered_map<uint32_t, uint32_t> ImportedAlbedoColors;
std::vector<std::array<uint8_t, ImportedLightLevels>> ImportedAlbedoLightTables;
TownLightingConfig SceneLightingConfig = TristramLightingConfig();
bool SceneLightingProfileLoaded = false;
int SceneTimeOfDay = -1;
uint64_t SceneLightingRevision = 1;
float SceneDayBrightness = 1;
uint64_t ImportedLightingRevision = 0;
TownHorizonMesh HorizonMesh;
std::vector<uint8_t> HorizonColors;
TownHorizonFogPalette HorizonFogPalette;
std::array<uint32_t, 256> HorizonPaletteSignature {};
std::array<uint8_t, 256> HorizonSkyColors {};
bool HorizonPaletteValid = false;
constexpr TownHorizonFogConfig HorizonFog;
constexpr unsigned HorizonDepthSteps = 8;
const std::array<uint8_t, 220 * HorizonDepthSteps + 1> HorizonFogDepthLevels = [] {
	std::array<uint8_t, 220 * HorizonDepthSteps + 1> levels {};
	for (size_t i = 0; i < levels.size(); ++i) {
		const float amount = TownHorizonFogAmount(static_cast<float>(i) / HorizonDepthSteps, HorizonFog);
		levels[i] = static_cast<uint8_t>(amount * (TownHorizonFogLevels - 1) + 0.5F);
	}
	return levels;
}();
std::unordered_map<const uint8_t *, VolumeArtwork> ActorVolumeCache;
struct BillboardArtwork {
	Texture texture;
	uint64_t sourceHash;
	int bottomCoveredRow = -1;
};
std::unordered_map<const uint8_t *, BillboardArtwork> BillboardTextureCache;
std::unordered_map<const uint8_t *, BillboardArtwork> SkullFireFlameCache;
std::unordered_map<size_t, VolumeArtwork> VegetationArtworkCache;
std::unordered_map<size_t, VolumeArtwork> PropArtworkCache;
std::array<std::array<bool, MAXDUNY>, MAXDUNX> PropVisualReplacementMask {};
std::array<std::array<bool, MAXDUNY>, MAXDUNX> PropGroundPaintReplacementMask {};
bool PropReplacementReady = false;
bool PreparedTownPropReplacesTile(Point tile, bool groundPaint = false);
std::unordered_map<uint16_t, VolumeArtwork> SceneryVolumeCache;
const std::array<Texture, 256> PaletteTextures = [] {
	std::array<Texture, 256> textures;
	for (size_t i = 0; i < textures.size(); ++i) {
		textures[i].width = 1;
		textures[i].height = 1;
		textures[i].pixels.push_back(static_cast<uint8_t>(i));
	}
	return textures;
}();
// Horizon colors have already received the same world-space sunlight. Their
// palette indices still use the shared GPU atlas, without applying light twice.
const std::array<Texture, 256> HorizonPaletteTextures = [] {
	std::array<Texture, 256> textures;
	for (size_t i = 0; i < textures.size(); ++i) {
		textures[i].width = 1;
		textures[i].height = 1;
		textures[i].pixels.push_back(static_cast<uint8_t>(i));
		textures[i].emissive = true;
	}
	return textures;
}();
// Volume triangles use constant palette colors. A shared GPU atlas lets their
// different colors remain per-vertex data instead of thousands of texture binds.
const Texture GpuPaletteAtlas = [] {
	Texture texture;
	texture.width = 256;
	texture.height = 1;
	for (size_t i = 0; i < 256; ++i)
		texture.pixels.push_back(static_cast<uint8_t>(i));
	return texture;
}();
std::array<std::array<uint8_t, 256>, 4> SceneLightTables;
std::array<std::array<std::array<uint8_t, 256>, 4>, 4> SceneShadowTables;
bool SceneLightingValid = false;
const std::byte *CachedDungeonData = nullptr;
std::unique_ptr<OwnedSurface> SpriteSurface;
std::unique_ptr<OwnedSurface> SpriteCoverageSurface;
int SpriteSurfaceWidth = 0;
int SpriteSurfaceHeight = 0;

const std::array<uint8_t, 256> IdentityPalette = [] {
	std::array<uint8_t, 256> result {};
	for (size_t i = 0; i < result.size(); ++i)
		result[i] = static_cast<uint8_t>(i);
	return result;
}();

void PrepareSceneLighting()
{
	if (SceneLightingValid)
		return;
	SceneLightingConfig = TristramLightingConfig();
	SceneLightingProfileLoaded = false;
	AssetRef profile = FindAsset("d3d-lighting.ini");
	if (profile.ok() && profile.size() > 0 && profile.size() <= 4096) {
		std::string text(profile.size(), '\0');
		AssetHandle handle = OpenAsset(std::move(profile));
		if (handle.ok() && handle.read(text.data(), text.size()))
			SceneLightingProfileLoaded = ParseTownLightingProfile(text, SceneLightingConfig);
	}
	SceneTimeOfDay = std::clamp(*GetOptions().Graphics.townViewTimeOfDay, 0, 2);
	SceneDayBrightness = SceneTimeOfDay == 0 ? 1.0F : SceneTimeOfDay == 1 ? 0.70F : 0.36F;
	const float energy = SceneTimeOfDay == 0 ? 1.0F : SceneTimeOfDay == 1 ? 0.46F : 0.12F;
	SceneLightingConfig.ambient.red *= energy;
	SceneLightingConfig.ambient.green *= energy;
	SceneLightingConfig.ambient.blue *= energy * (SceneTimeOfDay == 2 ? 1.5F : 1.10F);
	SceneLightingConfig.directionalIntensity *= energy;
	constexpr std::array<float, 4> Brightness { 1.0F, 0.93F, 0.84F, 0.74F };
	// Match darkened RGB colors back into the actual Tristram palette. Its first
	// 128 entries are not ordered shade ramps, unlike the dungeon palettes.
	for (size_t shade = 0; shade < Brightness.size(); ++shade) {
		for (size_t index = 0; index < logical_palette.size(); ++index) {
			if ((shade == 0 && SceneDayBrightness == 1) || index == 0) {
				SceneLightTables[shade][index] = static_cast<uint8_t>(index);
				continue;
			}
			const SDL_Color color = logical_palette[index];
			const float red = color.r * Brightness[shade] * SceneDayBrightness;
			const float green = color.g * Brightness[shade] * SceneDayBrightness;
			const float blue = color.b * Brightness[shade] * SceneDayBrightness;
			float bestDistance = std::numeric_limits<float>::max();
			uint8_t best = static_cast<uint8_t>(index);
			for (size_t candidate = 1; candidate < logical_palette.size(); ++candidate) {
				const SDL_Color other = logical_palette[candidate];
				const float dr = other.r - red;
				const float dg = other.g - green;
				const float db = other.b - blue;
				const float distance = dr * dr + dg * dg + db * db;
				if (distance < bestDistance) {
					bestDistance = distance;
					best = static_cast<uint8_t>(candidate);
				}
			}
			SceneLightTables[shade][index] = best;
		}
	}
	// Combine indirect surface light with occlusion from the actual world-space
	// depth map. Palette remapping stays independent of the camera and geometry.
	constexpr std::array<float, 4> ShadowBrightness { 1.0F, 0.88F, 0.76F, 0.65F };
	for (size_t shade = 0; shade < Brightness.size(); ++shade) {
		SceneShadowTables[shade][0] = SceneLightTables[shade];
		for (size_t shadow = 1; shadow < ShadowBrightness.size(); ++shadow) {
			for (size_t index = 0; index < logical_palette.size(); ++index) {
				const SDL_Color color = logical_palette[index];
				const float brightness = Brightness[shade] * ShadowBrightness[shadow] * SceneDayBrightness;
				float bestDistance = std::numeric_limits<float>::max();
				uint8_t best = static_cast<uint8_t>(index);
				for (size_t candidate = 0; candidate < logical_palette.size(); ++candidate) {
					const SDL_Color other = logical_palette[candidate];
					const float dr = other.r - color.r * brightness;
					const float dg = other.g - color.g * brightness;
					const float db = other.b - color.b * brightness;
					const float distance = dr * dr + dg * dg + db * db;
					if (distance < bestDistance) {
						bestDistance = distance;
						best = static_cast<uint8_t>(candidate);
					}
				}
				SceneShadowTables[shade][shadow][index] = best;
			}
		}
	}
	SceneLightingValid = true;
}

PickRecord PickAt(Point tile, PickKind kind = PickKind::Ground, int entity = -1)
{
	return { static_cast<uint16_t>(tile.y * MAXDUNX + tile.x), static_cast<int16_t>(entity), kind };
}

Vec3 PlayerPosition(const Player &player)
{
	Vec3 position { static_cast<float>(player.position.tile.x), 0, static_cast<float>(player.position.tile.y) };
	if (player.isWalking()) {
		const Displacement offset = GetOffsetForWalking(player.AnimInfo, player._pdir);
		// Inverse of Diablo's [32,-32;16,16] screen transform, without integer rounding.
		position.x += static_cast<float>(2 * offset.deltaY + offset.deltaX) / 64.0F;
		position.z += static_cast<float>(2 * offset.deltaY - offset.deltaX) / 64.0F;
	}
	return position;
}

void SyncCameraPose()
{
	const auto &pose = CameraRig.pose();
	CameraYaw = pose.yaw;
	CameraPitch = pose.pitch;
	CameraDistance = pose.distance;
	CameraPanOffset = { pose.pan.x, 0, pose.pan.z };
}

float CameraPointLength(TownCameraPoint point)
{
	return std::sqrt(point.x * point.x + point.height * point.height + point.z * point.z);
}

void ClearCameraVisibilityFrame()
{
	if (CameraVisibilityState.active || CameraVisibilityState.valid
	    || std::any_of(CameraHiddenArchitecture.begin(), CameraHiddenArchitecture.end(), [](uint8_t bit) { return bit != 0; })) {
		++CameraVisibilityRevision;
		PickingValid = false;
	}
	std::fill(CameraHiddenArchitecture.begin(), CameraHiddenArchitecture.end(), 0);
	std::fill(CameraVisibilityQueryMask.begin(), CameraVisibilityQueryMask.end(), 0);
	CameraVisibilityState = {};
	CameraVisibilityState.revision = CameraVisibilityRevision;
}

void ResetFollowCameraHistory(bool clearIndex = false)
{
	ClearCameraVisibilityFrame();
	HasSafeCameraEye = false;
	FollowCameraState = {};
	CameraFollowFrameSeconds = 0;
	++FollowProjectionRevision;
	PickingValid = false;
	if (clearIndex) {
		CathedralVisibilityIndex.Clear();
		CathedralVisibilityOwners.clear();
		CathedralHiddenOwners.clear();
		CathedralVisibilityGeometry = CathedralVisibilityPresentation = 0;
		CameraVisibilityIndex.Clear();
		CameraVisibilityBuildAttempted = false;
		CameraVisibilitySceneRevision = 0;
		CameraHiddenArchitecture.clear();
		CameraVisibilityQueryMask.clear();
		CameraCollisionIndex.Clear();
		CameraCollisionSceneRevision = std::numeric_limits<uint64_t>::max();
	}
}

bool FollowThroughTownArchitecture()
{
	return GameplayCameraPolicy && (leveltype == DTYPE_TOWN
	        || (cathedral::LiveLevelEligible() && CameraRig.mode() == TownCameraMode::ThirdPerson)) && !CameraRig.suspended()
	    && IsTownGameplayCameraMode(CameraRig.mode());
}

float PhysicalCameraRadius(const TownCameraFrame &frame)
{
	const float radius = TownCameraCollisionRadius(frame);
	return radius > 0 ? radius - .03F + PhysicalToNativeGrid(.03F) : 0;
}

float GameplayEyeHeight()
{
	// Reuse the caller-authored ocular calibration; do not infer anatomy from sprites.
	const auto preferences = CameraRig.preferences();
	return PhysicalToNativeGrid(ConfigureTownGameplayEyeHeight(preferences, preferences.firstPersonEyeHeight).eyeHeight);
}

bool PrepareCameraVisibilityFrame()
{
	if (!FollowThroughTownArchitecture()) {
		ClearCameraVisibilityFrame();
		return true;
	}
	if (cathedral::LiveLevelEligible()) {
		ClearCameraVisibilityFrame();
		if (!CathedralVisibilityIndex.ready() || CathedralVisibilityGeometry != CathedralFrame.geometryRevision
		    || CathedralVisibilityPresentation != CathedralFrame.presentationSignature) {
			CathedralVisibilityIndex.Clear();
			CathedralVisibilityOwners.clear();
			std::vector<TownCameraVisibilityTriangle> triangles;
			for (const auto &surface : CathedralFrame.triangles) {
				if (surface.module == cathedral::ModuleKind::Floor || surface.module == cathedral::ModuleKind::StairsUp
				    || surface.module == cathedral::ModuleKind::StairsDown || surface.pick.kind == cathedral::PickKind::Object)
					continue;
				const auto [owner, inserted] = CathedralVisibilityOwners.emplace(surface.instanceId, static_cast<uint32_t>(CathedralVisibilityOwners.size()));
				TownCameraVisibilityTriangle triangle;
				triangle.owner = owner->second;
				for (size_t i = 0; i < 3; ++i) {
					const auto &v = surface.vertices[i].position;
					triangle.vertices[i] = { v.x, v.y, v.z };
				}
				triangles.push_back(triangle);
			}
			CathedralVisibilityGeometry = CathedralFrame.geometryRevision;
			CathedralVisibilityPresentation = CathedralFrame.presentationSignature;
			CathedralVisibilityIndex.Build(CathedralVisibilityGeometry, static_cast<uint32_t>(CathedralVisibilityOwners.size()), std::move(triangles));
			CathedralHiddenOwners.assign(CathedralVisibilityOwners.size(), 0);
		}
		const TownCameraPoint focus { ViewCamera.target.x, ViewCamera.target.y + FollowCameraState.eyeHeight, ViewCamera.target.z };
		const auto stats = CathedralVisibilityIndex.Query(ViewCamera.projection.eye, focus,
		    PhysicalCameraRadius(ViewCamera.projection) + 0.02F, true, CathedralVisibilityGeometry, CathedralHiddenOwners);
		if (!stats.valid) std::fill(CathedralHiddenOwners.begin(), CathedralHiddenOwners.end(), 0);
		return true;
	}
	const auto &scene = GetTownScene();
	const uint64_t epoch = GetTownSceneRevision();
	if (!CameraVisibilityBuildAttempted || epoch != CameraVisibilitySceneRevision) {
		ClearCameraVisibilityFrame();
		CameraVisibilityIndex.Clear();
		CameraVisibilityBuildAttempted = true;
		CameraVisibilitySceneRevision = epoch;
		++CameraVisibilityBuilds;
		try {
			if (scene.size() > TownCameraVisibilityIndex::MaxOwners)
				throw std::length_error("Camera visibility owner limit");
			size_t triangleCount = 0;
			const auto count = [&](const auto &surfaces) {
				for (const auto &surface : surfaces) {
					if (surface.surfaceDetail == TownSceneSurfaceDetail::FireCore || surface.surfaceDetail == TownSceneSurfaceDetail::FireTip)
						continue;
					if (++triangleCount > TownCameraVisibilityIndex::MaxTriangles)
						throw std::length_error("Camera visibility triangle limit");
				}
			};
			for (const auto &model : scene) {
				count(TownSceneExteriorTriangles(model));
				if (model.cabinInterior)
					count(model.cabinInterior->interiorTriangles);
			}
			std::vector<TownCameraVisibilityTriangle> triangles;
			triangles.reserve(triangleCount);
			const auto append = [&](const auto &surfaces, uint32_t owner) {
				for (const auto &surface : surfaces) {
					if (surface.surfaceDetail == TownSceneSurfaceDetail::FireCore || surface.surfaceDetail == TownSceneSurfaceDetail::FireTip)
						continue;
					TownCameraVisibilityTriangle triangle;
					triangle.owner = owner;
					for (size_t i = 0; i < 3; ++i) {
						const auto &v = surface.vertices[i];
						triangle.vertices[i] = { v.x, v.height, v.z };
					}
					triangles.push_back(triangle);
				}
			};
			for (size_t owner = 0; owner < scene.size(); ++owner) {
				append(TownSceneExteriorTriangles(scene[owner]), static_cast<uint32_t>(owner));
				if (scene[owner].cabinInterior)
					append(scene[owner].cabinInterior->interiorTriangles, static_cast<uint32_t>(owner));
			}
			// Full geometry stays immutable; never build from last frame's hidden set.
			CameraVisibilityIndex.Build(epoch, static_cast<uint32_t>(scene.size()), std::move(triangles));
			if (CameraVisibilityIndex.buildStatus() == TownCameraVisibilityBuildStatus::AllocationFailure)
				CameraVisibilityBuildAttempted = false; // Retry transient pressure on a later frame.
			CameraHiddenArchitecture.assign(scene.size(), 0);
			CameraVisibilityQueryMask.assign(scene.size(), 0);
		} catch (const std::bad_alloc &) {
			CameraVisibilityBuildAttempted = false;
			CameraVisibilityIndex.Clear();
			CameraHiddenArchitecture.clear();
			CameraVisibilityQueryMask.clear();
		} catch (const std::length_error &) {
			CameraVisibilityIndex.Clear();
			CameraHiddenArchitecture.clear();
			CameraVisibilityQueryMask.clear();
		}
	}
	const auto &frame = ViewCamera.projection;
	const TownCameraPoint focus = { ViewCamera.target.x, ViewCamera.target.y + FollowCameraState.eyeHeight, ViewCamera.target.z };
	const auto stats = CameraVisibilityIndex.Query(frame.eye, focus,
	    PhysicalCameraRadius(frame) + 0.02F,
	    CameraRig.mode() == TownCameraMode::ThirdPerson || CameraRig.VisualDistance() > 0.0001F,
	    epoch, CameraVisibilityQueryMask);
	if (CameraVisibilityState.active != true || CameraVisibilityState.valid != stats.valid
	    || CameraVisibilityState.sceneRevision != epoch || CameraHiddenArchitecture != CameraVisibilityQueryMask) {
		++CameraVisibilityRevision;
		PickingValid = false;
	}
	CameraHiddenArchitecture.swap(CameraVisibilityQueryMask);
	CameraVisibilityState.active = true;
	CameraVisibilityState.valid = stats.valid;
	CameraVisibilityState.budgetExceeded = stats.budgetExceeded;
	CameraVisibilityState.sceneRevision = epoch;
	CameraVisibilityState.revision = CameraVisibilityRevision;
	CameraVisibilityState.owners = scene.size();
	CameraVisibilityState.hiddenOwners = stats.hiddenOwners;
	CameraVisibilityState.certifiedComponents = stats.certifiedComponents;
	CameraVisibilityState.uncertifiedAtEye = stats.containmentUncertified;
	CameraVisibilityState.ambiguousAtEye = stats.containmentAmbiguous;
	CameraVisibilityState.nodesVisited = stats.nodesVisited;
	CameraVisibilityState.trianglesTested = stats.trianglesTested;
	CameraVisibilityState.bytes = stats.bytes;
	CameraVisibilityState.cacheBuilds = CameraVisibilityBuilds;
	return stats.valid;
}

void PrepareCameraCollisionIndex()
{
	const auto &scene = ViewCollisionScene();
	const uint64_t revision = ViewSceneRevision();
	if (revision == CameraCollisionSceneRevision)
		return;
	std::vector<TownCameraCollisionTriangle> triangles;
	const auto append = [&](const auto &surfaces) {
		for (const auto &surface : surfaces) {
			if (surface.surfaceDetail == TownSceneSurfaceDetail::FireCore || surface.surfaceDetail == TownSceneSurfaceDetail::FireTip)
				continue;
			TownCameraCollisionTriangle triangle;
			for (size_t i = 0; i < 3; ++i) {
				const auto &v = surface.vertices[i];
				triangle.vertices[i] = { v.x, v.height, v.z };
			}
			triangles.push_back(triangle);
		}
	};
	for (const auto &model : scene) {
		append(TownSceneExteriorTriangles(model));
		if (model.cabinInterior != nullptr)
			append(model.cabinInterior->interiorTriangles);
	}
	CameraCollisionIndex.Build(std::move(triangles));
	CameraCollisionSceneRevision = revision;
	++CameraCollisionBuilds;
	HasSafeCameraEye = false;
}

void ResolveFollowCamera(TownCameraFrame &frame, TownCameraPoint anchor)
{
	const float frameSeconds = CameraFollowFrameSeconds;
	CameraFollowFrameSeconds = 0; // A capture/redraw cannot reuse a frame's recovery time.
	if (!frame.valid || (CameraRig.mode() != TownCameraMode::ThirdPerson && CameraRig.mode() != TownCameraMode::FirstPerson)) {
		if (FollowCameraState.active)
			ResetFollowCameraHistory();
		return;
	}
	const TownViewFollowCameraState previous = FollowCameraState;
	if (FollowThroughTownArchitecture()) {
		// Publish the anchor-relative eye each frame. A building must not retain
		// an old eye while the native hero continues moving on the other side.
		TownViewFollowCameraState state;
		state.active = true;
		state.transition = CameraRig.IsVisualTransitionActive();
		state.desiredDistance = PhysicalToNativeGrid(CameraRig.pose().distance);
		state.visualDistance = PhysicalToNativeGrid(CameraRig.VisualDistance());
		state.eyeHeight = GameplayEyeHeight();
		state.radius = PhysicalCameraRadius(frame);
		state.desiredEye = frame.eye;
		state.resolvedEye = frame.eye;
		// The floor only shortens the boom on its authored eye/focus axis.
		// Architecture never clamps or recovers a camera from a previous frame.
		const auto focus = anchor + TownCameraPoint { 0, state.eyeHeight, 0 };
		const auto outward = state.desiredEye - focus;
		const float length = CameraPointLength(outward);
		const float floor = state.radius + 0.002F;
		state.valid = state.radius > 0 && focus.height >= floor;
		float allowed = length;
		if (outward.height < 0 && length > 0)
			allowed = std::min(allowed, std::max(0.0F, (focus.height - floor) * length / -outward.height));
		if (length > 0)
			state.resolvedEye = focus + outward * (allowed / length);
		state.blocked = allowed < length;
		state.resolvedDistance = CameraPointLength(state.resolvedEye - focus);
		state.sceneRevision = GetTownSceneRevision();
		state.localPlayerHidden = CameraRig.HideLocalPlayer(state.resolvedDistance * WorldMetresPerTile)
		    || (previous.localPlayerHidden && state.resolvedDistance < PhysicalToNativeGrid(0.85F));
		if (!previous.active || CameraPointLength(state.resolvedEye - previous.resolvedEye) > 0.000001F
		    || previous.localPlayerHidden != state.localPlayerHidden || !previous.valid)
			++FollowProjectionRevision;
		FollowCameraState = state;
		HasSafeCameraEye = false;
		frame.valid &= state.valid;
		frame.eye = state.resolvedEye;
		return;
	}
	PrepareCameraCollisionIndex();
	TownViewFollowCameraState state;
	state.active = true;
	state.transition = CameraRig.IsVisualTransitionActive();
	state.desiredDistance = PhysicalToNativeGrid(CameraRig.pose().distance);
	state.visualDistance = PhysicalToNativeGrid(CameraRig.VisualDistance());
	state.eyeHeight = PhysicalToNativeGrid(CameraRig.VisualEyeHeight());
	state.radius = PhysicalCameraRadius(frame);
	state.desiredEye = frame.eye;
	state.sceneRevision = CameraCollisionSceneRevision;
	state.triangles = CameraCollisionIndex.triangleCount();
	state.bytes = CameraCollisionIndex.bytes();
	state.cacheBuilds = CameraCollisionBuilds;
	if (HasSafeCameraEye && CameraPointLength(anchor - LastCameraAnchor) > 4)
		HasSafeCameraEye = false; // Native load/teleport, never interpolate across the map.
	const auto sweep = [&](TownCameraPoint from, TownCameraPoint to) {
		const auto hit = CameraCollisionIndex.Sweep(from, to, state.radius);
		state.nodesVisited += hit.nodesVisited;
		state.trianglesTested += hit.trianglesTested;
		state.initialOverlap |= hit.initialOverlap;
		return hit;
	};
	const TownCameraPoint originalFocus = anchor + TownCameraPoint { 0, state.eyeHeight, 0 };
	const auto separated = CameraCollisionIndex.Separate(originalFocus,
		HasSafeCameraEye ? LastSafeCameraEye : originalFocus - frame.forward, state.radius);
	state.trianglesTested += separated.trianglesTested;
	TownCameraPoint focus = separated.point;
	state.valid = separated.resolved && state.radius > 0;
	state.blocked = CameraPointLength(focus - originalFocus) > 0.0001F;
	const TownCameraPoint outward = state.desiredEye - originalFocus;
	const float desiredLength = CameraPointLength(outward);
	TownCameraPoint candidate = focus;
	if (state.valid) {
		const auto hit = sweep(focus, focus + outward);
		state.valid = hit.valid;
		float allowed = desiredLength * hit.fraction;
		state.blocked |= hit.fraction < 1;
		if (hit.fraction < 1)
			allowed = std::max(0.0F, allowed - 0.002F);
		// Looking up may put the requested boom below ground; shorten it without
		// changing direction, desired zoom or the native player's collision.
		const float floor = state.radius + 0.002F;
		if (outward.height < 0 && desiredLength > 0) {
			const float floorLength = std::max(0.0F, (focus.height - floor) * desiredLength / -outward.height);
			state.blocked |= floorLength < allowed;
			allowed = std::min(allowed, floorLength);
		}
		if (HasSafeCameraEye && previous.active && allowed > previous.resolvedDistance) {
			const float factor = 1 - std::exp(-frameSeconds / 0.1F);
			allowed = previous.resolvedDistance + (allowed - previous.resolvedDistance) * factor;
		}
		if (desiredLength > 0)
			candidate = focus + outward * (allowed / desiredLength);
		candidate.height = std::max(candidate.height, floor);
		if (HasSafeCameraEye) {
			// A boom test alone can jump to the opposite side of a thin wall when
			// the camera rotates. Sweep from the last published safe eye as well.
			const auto old = CameraCollisionIndex.Separate(LastSafeCameraEye, LastSafeCameraEye, state.radius);
			state.trianglesTested += old.trianglesTested;
			state.valid &= old.resolved;
			if (old.resolved) {
				const auto temporal = sweep(old.point, candidate);
				state.valid &= temporal.valid;
				if (temporal.fraction < 1) {
					state.blocked = true;
					const TownCameraPoint movement = candidate - old.point;
					const float length = CameraPointLength(movement);
					candidate = old.point + movement * std::max(0.0F, temporal.fraction - (length > 0 ? 0.002F / length : 0));
				}
			}
		}
		const auto final = sweep(candidate, candidate);
		state.valid &= final.valid && !final.initialOverlap && candidate.height >= floor - 0.0001F;
	}
	if (!state.valid && HasSafeCameraEye) {
		const auto old = sweep(LastSafeCameraEye, LastSafeCameraEye);
		if (old.valid && !old.initialOverlap && LastSafeCameraEye.height >= state.radius) {
			candidate = LastSafeCameraEye;
			state.valid = true;
			state.blocked = true;
		}
	}
	state.resolvedEye = candidate;
	state.resolvedDistance = CameraPointLength(candidate - focus);
	state.transition |= !state.blocked && std::abs(state.resolvedDistance - desiredLength) > 0.001F;
	state.localPlayerHidden = CameraRig.HideLocalPlayer(state.resolvedDistance * WorldMetresPerTile)
	    || (previous.localPlayerHidden && state.resolvedDistance < PhysicalToNativeGrid(0.85F));
	if (!previous.active || CameraPointLength(candidate - previous.resolvedEye) > 0.000001F
	    || previous.localPlayerHidden != state.localPlayerHidden || previous.valid != state.valid)
		++FollowProjectionRevision;
	FollowCameraState = state;
	frame.valid &= state.valid;
	frame.eye = candidate;
	if (state.valid) {
		LastSafeCameraEye = candidate;
		LastCameraAnchor = anchor;
		HasSafeCameraEye = true;
	}
}

void ConfigureCamera(int width, int height, bool diagnosticProjection = false)
{
	Vec3 target { static_cast<float>(ViewPosition.x), 0, static_cast<float>(ViewPosition.y) };
	const bool followsHero = CameraRig.mode() == TownCameraMode::ThirdPerson || CameraRig.mode() == TownCameraMode::FirstPerson;
	if (MyPlayer != nullptr && (followsHero || MyPlayer->position.tile == ViewPosition))
		target = PlayerPosition(*MyPlayer);
	// Derive the anchor from the real native viewport, including zoom and side
	// panels. Walking offsets cancel the camera offset, since target already
	// follows the interpolated player position.
	CalcViewportGeometry();
	Point anchor = GetScreenPosition(ViewPosition) + Displacement { 32, 0 };
	if (MyPlayer != nullptr && MyPlayer->position.tile == ViewPosition && MyPlayer->isWalking())
		anchor += GetOffsetForWalking(MyPlayer->AnimInfo, MyPlayer->_pdir);
	const int zoomFactor = *GetOptions().Graphics.zoom ? 2 : 1;
	float centerX = static_cast<float>(anchor.x * zoomFactor);
	float centerY = static_cast<float>(anchor.y * zoomFactor);
	if (zoomFactor == 2 && CanPanelsCoverView() && IsLeftPanelOpen())
		centerX += SidePanelSize.width;
	if (CameraRig.mode() != TownCameraMode::Isometric) {
		// The logical world viewport remains independent of HUD/sampling. A
		// centered perspective has the configured vertical FOV exactly.
		centerX = width / 2.0F;
		centerY = height / 2.0F;
	}
	// A forced diagnostic may inspect geometry while the live input rig stays
	// suspended for native transparency/GPU fallback. Only this copy is resumed.
	TownCameraRig projectionRig = CameraRig;
	if (diagnosticProjection)
		projectionRig.Suspend(false);
	ViewCamera.projection = BuildTownCameraFrame(projectionRig, { target.x, target.y, target.z }, width, height,
		centerX, centerY, static_cast<float>(zoomFactor));
	if (ViewCamera.projection.perspective) {
		const TownCameraPoint nativeAnchor { target.x, target.y, target.z };
		ViewCamera.projection.eye = nativeAnchor + (ViewCamera.projection.eye - nativeAnchor) * PhysicalToNativeGrid(1);
		ViewCamera.projection.nearClip = PhysicalToNativeGrid(ViewCamera.projection.nearClip);
		ViewCamera.projection.farClip = PhysicalToNativeGrid(ViewCamera.projection.farClip);
	}
	if (FollowThroughTownArchitecture()) {
		// Translate only the gameplay frame onto one calibrated eye anchor. The
		// raw four-mode rig, its visual zoom and Cathedral/legacy fixtures remain.
		ViewCamera.projection.eye.height += GameplayEyeHeight() - PhysicalToNativeGrid(CameraRig.VisualEyeHeight());
	}
	ResolveFollowCamera(ViewCamera.projection, { target.x, target.y, target.z });
	const TownCameraFrame &frame = ViewCamera.projection;
	const auto vector = [](TownCameraPoint point) { return Vec3 { point.x, point.height, point.z }; };
	ViewCamera.target = target + CameraPanOffset;
	ViewCamera.eye = vector(frame.eye);
	ViewCamera.forward = vector(frame.forward);
	ViewCamera.right = vector(frame.right);
	ViewCamera.up = vector(frame.up);
	ViewCamera.width = frame.width;
	ViewCamera.height = frame.height;
	ViewCamera.centerX = frame.centerX;
	ViewCamera.centerY = frame.centerY;
	ViewCamera.focal = frame.focalPixels;
}

Vec3 ToCamera(Vec3 point)
{
	const TownCameraPoint view = TownCameraToView(ViewCamera.projection, { point.x, point.y, point.z });
	return { view.x, view.height, view.z };
}

ProjectedVertex Project(Vertex vertex)
{
	const float q = ViewCamera.projection.perspective ? 1 / vertex.position.z : 1;
	return { ViewCamera.centerX + vertex.position.x * ViewCamera.focal * q,
		ViewCamera.centerY - vertex.position.y * ViewCamera.focal * q,
		vertex.position.z, vertex.u, vertex.v, q };
}

ProjectedVertex ProjectRaster(Vertex vertex)
{
	ProjectedVertex projected = Project(vertex);
	projected.x *= RasterSampleFactor;
	projected.y *= RasterSampleFactor;
	return projected;
}

float Edge(const ProjectedVertex &a, const ProjectedVertex &b, float x, float y)
{
	return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

void ClearGpuSceneResources()
{
	ResetTownGpuResources();
	PreparedGpuTextures.clear();
	GpuCathedralLuts.clear();
	GpuCathedralBlendValid = false;
	GpuOrdinaryLut.clear();
	GpuImportedLut.clear();
	GpuImportedLutColors = 0;
	GpuPickRecords.clear();
	GpuPickIds.clear();
	GpuFrame = {};
	RendererState = {};
	CaptureGpu = CaptureGpuFailed = false;
	GpuRecovery.DisableOrReset();
	GpuFailure.clear();
}

TownGpuTexture PrepareGpuTexture(const Texture &texture, const InteriorLighting *interior)
{
	auto [entry, inserted] = PreparedGpuTextures.try_emplace(texture.gpuIdentity.value);
	PreparedGpuTexture &prepared = entry->second;
	prepared.lastSeen = GpuFrameNumber;
	const bool albedo = !texture.emissive && (!texture.albedoPixels.empty() || texture.actorMaterial.has_value());
	if (inserted) {
		if (texture.actorMaterial) {
			// Codes and paired LUTs already have one immutable owner.
		} else if (albedo)
			prepared.codes = texture.albedoPixels;
		else
			prepared.codes.assign(texture.pixels.begin(), texture.pixels.end());
		for (const auto &row : texture.interiorLightTables)
			prepared.interiorLut.insert(prepared.interiorLut.end(), row.begin(), row.end());
	}
	TownGpuTexture result;
	result.stableKey = texture.gpuIdentity.value;
	result.width = texture.width;
	result.height = texture.height;
	result.texelCodes = prepared.codes;
	result.unlitCodes = texture.lightingBasePixels;
	result.opacity = texture.opacity;
	if (texture.actorMaterial) {
		const auto &actor = *texture.actorMaterial;
		result.texelCodes = actor.texelCodes;
		result.lightLut = actor.indexedLightLut;
		result.rgbaLightLut = actor.rgbaLightLut;
		result.rgbaLightLutEnabled = true;
		result.lightLevels = actor.lightLevels;
		// This owned actor material is frozen before either backend draws. A
		// replacement mints a new texture identity, including its private LUT.
		const auto lighting = DrawingCathedral ? TownGpuLighting::Palette
		    : texture.emissive ? TownGpuLighting::Unlit
		    : interior != nullptr ? TownGpuLighting::Interior : TownGpuLighting::Directional;
		const uint64_t recordingFrame = GetTownGpuRecordingFrame();
		if (recordingFrame == 0 || prepared.actorPreparedFrame != recordingFrame
		    || prepared.actorPreparedLighting != lighting) {
			// Preparing the descriptor alone does not prove GPU residency. This
			// uploads/validates and pins before any primitive can be submitted.
			if (!TownGpuPrepareTexture(result, lighting)) {
				CaptureGpuFailed = true;
				return {};
			}
			prepared.actorPreparedFrame = recordingFrame;
			prepared.actorPreparedLighting = lighting;
		}
		// Same-frame resources are pinned against eviction. Keep every immutable
		// identity/layout/RGB-enabled witness, but never rehash megabytes per face.
		result.texelCodes = {};
		result.unlitCodes = {};
		result.opacity = {};
		result.lightLut = {};
		result.rgbaLightLut = {};
		return result;
	}
	if (DrawingCathedral) {
		// Cache buckets may use the native pointer; immutable GPU identities and
		// revisions come from exact bytes, never from that address alone.
		auto &binding = GpuCathedralLuts[CathedralTranslation];
		if (binding.lastPreparedFrame != GpuFrameNumber) {
			const auto lut = CathedralTranslation != nullptr
			    ? cathedral::BuildNativeTranslationLut(std::span<const uint8_t, 256>(CathedralTranslation, 256))
			    : cathedral::BuildNativeLightLut(LightTables);
			if (!binding.palette.Update(lut, cathedral::LiveEpoch())) {
				CaptureGpuFailed = true;
				return {}; // Invalid epoch/revision cannot publish a stale LUT.
			}
			binding.lastPreparedFrame = GpuFrameNumber;
		}
		result.lightLutKey = binding.identity.value;
		result.lightLutRevision = binding.palette.revision();
		result.lightLut = binding.palette.table();
		result.lightLevels = 16;
		return result;
	}
	if (texture.emissive)
		return result;
	if (interior != nullptr) {
		if (!texture.interiorRgbLightTables.empty()) {
			if (prepared.interiorRgbRevision == 0 || std::memcmp(prepared.interiorRgbTone.data(), SmoothToneRamp.data(), sizeof(SmoothToneRamp)) != 0) {
				prepared.interiorRgbLut.clear();
				for (const auto &row : texture.interiorRgbLightTables)
					for (uint32_t value : row)
						prepared.interiorRgbLut.push_back(ToneWorldRgb({ float((value >> 16) & 255) / 255, float((value >> 8) & 255) / 255, float(value & 255) / 255 }));
				prepared.interiorRgbTone = SmoothToneRamp;
				// The backend cache can outlive this prepared entry across GPU toggles.
				// Mint a process-wide revision; never restart a private identity at 1.
				prepared.interiorRgbRevision = NextTextureGpuIdentity();
			}
			// Private paired-LUT revision also refreshes this small interior's
			// texels on a brightness change. Unchanged tones retain a warm hit.
			result.revision = prepared.interiorRgbRevision;
			result.rgbaLightLut = prepared.interiorRgbLut;
			result.rgbaLightLutEnabled = true;
		}
		result.lightLut = prepared.interiorLut;
		result.lightLevels = ImportedLightLevels;
	} else if (albedo) {
		if (GpuImportedLutColors != ImportedAlbedoLightTables.size()) {
			GpuImportedLut.clear();
			GpuImportedLut.reserve(ImportedAlbedoLightTables.size() * ImportedLightLevels);
			for (const auto &row : ImportedAlbedoLightTables)
				GpuImportedLut.insert(GpuImportedLut.end(), row.begin(), row.end());
			GpuImportedLutColors = ImportedAlbedoLightTables.size();
		}
		// Albedo codes are immutable for this texture identity. Growing the shared
		// lighting table must not upload another copy of every 2048px code atlas.
		result.lightLutKey = 1;
		result.lightLutRevision = (SceneLightingRevision << 32) | GpuImportedLutColors;
		result.lightLut = GpuImportedLut;
		result.lightLevels = ImportedLightLevels;
	} else {
		if (GpuOrdinaryLut.empty()) {
			GpuOrdinaryLut.resize(256 * 16);
			for (size_t code = 0; code < 256; ++code)
				for (size_t shade = 0; shade < 4; ++shade)
					for (size_t shadow = 0; shadow < 4; ++shadow)
						GpuOrdinaryLut[code * 16 + shade * 4 + shadow] = SceneShadowTables[shade][shadow][code];
		}
		result.lightLutKey = 2;
		result.lightLutRevision = SceneLightingRevision;
		result.lightLut = GpuOrdinaryLut;
		result.lightLevels = 16;
	}
	return result;
}

bool PrepareCathedralGpuBlend()
{
	const uint64_t epoch = cathedral::LiveEpoch();
	if (!GpuCathedralBlendValid || GpuCathedralBlendEpoch != epoch
	    || std::memcmp(GpuCathedralBlendLookup.data(), paletteTransparencyLookup, GpuCathedralBlendLookup.size()) != 0) {
		if (GpuCathedralBlendRevision == std::numeric_limits<uint64_t>::max())
			return false;
		std::memcpy(GpuCathedralBlendLookup.data(), paletteTransparencyLookup, GpuCathedralBlendLookup.size());
		GpuCathedralBlendEpoch = epoch;
		++GpuCathedralBlendRevision;
		if (GpuCathedralBlendRevision == 0)
			++GpuCathedralBlendRevision;
		GpuCathedralBlendValid = true;
	}
	return TownGpuSetPaletteBlend({ GpuCathedralBlendIdentity.value, GpuCathedralBlendRevision, GpuCathedralBlendLookup });
}

uint32_t GpuPickId(PickRecord pick)
{
	if (pick.preservePicking)
		return 0;
	const uint64_t key = static_cast<uint64_t>(pick.tile)
	    | (static_cast<uint64_t>(static_cast<uint16_t>(pick.entity)) << 16)
	    | (static_cast<uint64_t>(pick.kind) << 32)
	    | (static_cast<uint64_t>(static_cast<uint16_t>(pick.architecture)) << 40);
	auto [entry, inserted] = GpuPickIds.try_emplace(key, static_cast<uint32_t>(GpuPickRecords.size()));
	if (inserted)
		GpuPickRecords.push_back(pick);
	return entry->second;
}

bool PrepareResidentMesh(const ResidentMeshArtwork &mesh)
{
	if (CaptureGpuFailed)
		return false;
	if (!TownGpuHasMesh(mesh.identity())
	    && !TownGpuUploadMesh({ mesh.identity(), mesh.vertices, mesh.indices })) {
		CaptureGpuFailed = true;
		return false;
	}
	return true;
}

void ConfigureResidentMeshCamera()
{
	const TownCameraFrame &frame = ViewCamera.projection;
	const auto row = [](TownCameraPoint basis) {
		return std::array<float, 4> { basis.x, basis.height, basis.z, 0 };
	};
	TownGpuMeshCamera camera;
	camera.worldToView = { row(frame.right), row(frame.up), row(frame.forward) };
	camera.nativeArithmetic = true;
	camera.heightScale = frame.heightScale;
	camera.focalPixels = frame.focalPixels * RasterSampleFactor;
	camera.centerX = frame.centerX * RasterSampleFactor;
	camera.centerY = frame.centerY * RasterSampleFactor;
	camera.eye = { frame.eye.x, frame.eye.height, frame.eye.z };
	camera.towardViewer = { -frame.forward.x, -frame.forward.height / frame.heightScale, -frame.forward.z };
	CaptureGpuFailed = !TownGpuSetMeshCamera(camera);
}

void ConfigureResidentMeshLighting(TownGpuMeshInstance &instance)
{
	const TownShadowDirection light = GetTownShadowLightDirection();
	instance.vertexNormals = true;
	instance.toLight = { light.x, light.height, light.z };
}

void Rasterize(const Surface &out, const std::array<TownCameraProjectedVertex, 3> &triangle, const Texture &texture,
	PickRecord pick, int shade, bool transparent, const TownSceneNormal *authoredNormal,
	const InteriorLighting *interior, GpuVolumeFallback fallback)
{
	const auto raster = [](const TownCameraProjectedVertex &vertex) {
		return ProjectedVertex { vertex.x * RasterSampleFactor, vertex.y * RasterSampleFactor,
			vertex.depth, vertex.source.u, vertex.source.v, vertex.reciprocalW };
	};
	const ProjectedVertex a = raster(triangle[0]);
	const ProjectedVertex b = raster(triangle[1]);
	const ProjectedVertex c = raster(triangle[2]);
	const float area = Edge(a, b, c.x, c.y);
	if (std::abs(area) < 0.001F * RasterSampleFactor * RasterSampleFactor)
		return;
	const int minX = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
	const int maxX = std::min(out.w() - 1, static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
	const int minY = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
	const int maxY = std::min(out.h() - 1, static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
	if (minX > maxX || minY > maxY)
		return;
	const float inverseArea = 1.0F / area;
	const uint8_t *lightTable = DrawingCathedral
	    ? (CathedralTranslation != nullptr || shade == 0 ? IdentityPalette.data()
	        : LightTables[std::clamp(shade, 0, static_cast<int>(LightTables.size()) - 1)].data())
	    : SceneLightTables[std::clamp(shade, 0, 3)].data();
	const auto worldPosition = [](const TownCameraProjectedVertex &vertex) {
		return Vec3 { vertex.source.world.x, vertex.source.world.height, vertex.source.world.z };
	};
	const Vec3 worldA = worldPosition(triangle[0]);
	const Vec3 worldB = worldPosition(triangle[1]);
	const Vec3 worldC = worldPosition(triangle[2]);
	Vec3 normal = Cross(worldB - worldA, worldC - worldA);
	const float normalLength = std::sqrt(Dot(normal, normal));
	if (normalLength > 0)
		normal = normal * (1.0F / normalLength);
	if (normal.y < 0 && std::abs(worldA.y) < 0.001F && std::abs(worldB.y) < 0.001F && std::abs(worldC.y) < 0.001F)
		normal = normal * -1.0F;
	if (authoredNormal != nullptr)
		normal = { authoredNormal->x, authoredNormal->height, authoredNormal->z };
	const bool hasAlbedo = !texture.albedoPixels.empty() || texture.actorMaterial.has_value();
	if (hasAlbedo) {
		// D3DMESH1 imports render both material sides. A visible back face must
		// light its visible side, without changing authored vertices or UVs.
		const Vec3 towardViewer = ViewCamera.projection.perspective
		    ? ViewCamera.eye - (worldA + worldB + worldC) * (1.0F / 3)
		    : Vec3 { -ViewCamera.forward.x, -ViewCamera.forward.y / ViewCamera.projection.heightScale, -ViewCamera.forward.z };
		const TownLightVector facing = OrientTownLightingNormal({ normal.x, normal.y, normal.z },
			{ towardViewer.x, towardViewer.y, towardViewer.z });
		normal = { facing.x, facing.height, facing.z };
	}
	const TownShadowReceiver shadowReceiver = PrepareTownShadowReceiver(normal.x, normal.y, normal.z);
	const TownShadowDirection light = GetTownShadowLightDirection();
	// Normal shading already supplies ambient light on faces pointing away from
	// the source. Shadowing their absent direct light again crushed the masonry.
	const bool receivesDirectLight = normal.x * light.x + normal.y * light.height + normal.z * light.z > 0.02F;
	const float diffuse = std::clamp(normal.x * light.x + normal.y * light.height + normal.z * light.z, 0.0F, 1.0F);
	const bool receivesShadow = !DrawingWarriorViewModel && !DrawingCathedral && DirectionalShadowsEnabledForDiagnostics && interior == nullptr && !texture.emissive
	    && (hasAlbedo ? diffuse > 0 : receivesDirectLight);
	// The church exterior keeps its directional base. Its authored point sources
	// additionally reach the actual doorway masonry and upper front tower;
	// terrain and other architecture retain their existing lighting policy.
	const bool receivesFramePointLight = SpritePointReceiver(pick)
	    || (!DrawingCathedral && interior == nullptr && FrameSpritePointChurchRoom != nullptr
	        && pick.architecture == FrameSpritePointChurchArchitecture);
	if (CaptureGpu) {
		if (CaptureGpuFailed)
			return;
		TownGpuMaterial material;
		material.lighting = DrawingCathedral ? TownGpuLighting::Palette
		    : texture.emissive ? TownGpuLighting::Unlit
		    : interior != nullptr ? TownGpuLighting::Interior
		    : hasAlbedo ? TownGpuLighting::Directional : TownGpuLighting::Shadow;
		material.repeat = texture.repeat;
		material.transparentZero = transparent && !hasAlbedo && texture.opacity.empty();
		material.preservePicking = pick.preservePicking || CathedralPaletteBlend;
		material.viewModel = DrawingWarriorViewModel;
		material.paletteBlend = DrawingCathedral && CathedralPaletteBlend;
		material.receivesShadow = receivesShadow;
		material.shade = shade;
		material.fallbackPaletteIndex = fallback.palette;
		material.fallbackShade = fallback.shade;
		material.diffuse = diffuse;
		material.shadowBias = shadowReceiver.depthBias;
		material.shadowSlopeU = shadowReceiver.depthSlopeU;
		material.shadowSlopeV = shadowReceiver.depthSlopeV;
		material.normal = { normal.x, normal.y, normal.z };
		material.receivesSpritePointLight = FrameSpritePointLightingValid && !SmoothWorldPixels.empty()
		    && receivesFramePointLight && !texture.emissive;
		// The native player path supplies local/TRN shade 0 and the remote -5
		// offset explicitly. Do not replace that policy with the world light grid.
		if (texture.actorMaterial && DrawingCathedral) material.nativeLighting = false;
		if (interior != nullptr) {
			material.lights = { interior->lights.data(), interior->lightCount };
			material.interiorRedNormalization = interior->room->fireColor.red;
			material.interiorPointRange = InteriorPointLightRange;
			material.interiorRgbLighting = !texture.interiorRgbLightTables.empty();
			const std::span<const TownLightOccluder> occluders(interior->room->lightOccluders);
			material.room = &occluders.front();
			material.useFramePointLightVisibility = FrameSpritePointLightingValid && interior->room == FrameSpritePointChurchRoom;
			// The church has several apertured shells. The frozen frame field owns
			// their visibility; the legacy material span only admits opaque blockers.
			if (!material.useFramePointLightVisibility)
				material.blockers = occluders.subspan(1);
		}
		const bool constantPalette = !hasAlbedo && texture.opacity.empty()
		    && texture.width == 1 && texture.height == 1 && texture.pixels.size() == 1;
		if (constantPalette)
			material.repeat = false;
		const auto vertex = [&](ProjectedVertex p, Vec3 w, const std::array<float, 4> &authoredColor) {
			if (constantPalette) {
				p.u = (static_cast<float>(texture.pixels[0]) + 0.5F) / 256;
				p.v = 0.5F;
			}
			return TownGpuVertex { p.x, p.y, p.depth, p.u, p.v, { w.x, w.y, w.z }, ViewCamera.projection.perspective ? p.depth : 1.0F, authoredColor };
		};
		CaptureGpuFailed = !TownGpuSubmitProjectedTriangle({ vertex(a, worldA, triangle[0].source.authoredColor),
			vertex(b, worldB, triangle[1].source.authoredColor), vertex(c, worldC, triangle[2].source.authoredColor) },
			PrepareGpuTexture(constantPalette ? GpuPaletteAtlas : texture, interior), material, GpuPickId(pick));
		++RendererState.gpuSubmittedTriangles;
		return;
	}
	++RendererState.cpuRasterizedTriangles;
	for (int y = minY; y <= maxY; ++y) {
		uint8_t *destination = out.at(0, y);
		for (int x = minX; x <= maxX; ++x) {
			++RendererState.cpuPixelVisits;
			const float px = static_cast<float>(x) + 0.5F;
			const float py = static_cast<float>(y) + 0.5F;
			const float wa = Edge(b, c, px, py) * inverseArea;
			const float wb = Edge(c, a, px, py) * inverseArea;
			const float wc = 1.0F - wa - wb;
			if (DrawingCathedral && CathedralPaletteBlend) {
				if (!cathedral::NativePaletteTriangleCovers({ cathedral::NativeCoveragePoint { a.x, a.y }, { b.x, b.y }, { c.x, c.y } }, { px, py }))
					continue;
			} else if (wa < -0.0001F || wb < -0.0001F || wc < -0.0001F) {
				continue;
			}
			++RendererState.cpuCoveredFragments;
			float ca = wa, cb = wb, cc = wc;
			if (ViewCamera.projection.perspective) {
				// Coverage tolerates float edge error; never extrapolate reciprocal
				// depth or world/UV outside a clipped perspective triangle.
				ca = std::max(0.0F, ca);
				cb = std::max(0.0F, cb);
				cc = std::max(0.0F, cc);
				const float q = ca * a.reciprocalW + cb * b.reciprocalW + cc * c.reciprocalW;
				if (!std::isfinite(q) || q <= 0)
					continue;
				ca = ca * a.reciprocalW / q;
				cb = cb * b.reciprocalW / q;
				cc = cc * c.reciprocalW / q;
			}
			const float depth = ca * a.depth + cb * b.depth + cc * c.depth;
			const size_t index = static_cast<size_t>(y) * out.w() + x;
			if (!std::isfinite(depth) || depth > DepthBuffer[index] + 0.0001F) {
				++RendererState.cpuDepthRejected;
				continue;
			}
			const float u = ca * a.u + cb * b.u + cc * c.u;
			const float v = ca * a.v + cb * b.v + cc * c.v;
			uint8_t color;
			uint32_t albedoColor = 0;
			uint8_t unlitColor=0;
			if (!texture.sample(u, v, color, &albedoColor, &unlitColor) || (transparent && !hasAlbedo && texture.opacity.empty() && color == 0))
				continue;
			const uint8_t previousColor = destination[x];
			if (DrawingCathedral && CathedralTranslation != nullptr && !texture.actorMaterial)
				color = CathedralTranslation[color];
			const Vec3 world = worldA * ca + worldB * cb + worldC * cc;
			const float shadow = receivesShadow
			    ? SampleTownShadow(world.x, world.y, world.z, shadowReceiver) : 0;
			const int shadowLevel = std::clamp(static_cast<int>(shadow * 3.0F + 0.5F), 0, 3);
			uint32_t actorRgb = 0;
			if (texture.actorMaterial) {
				const auto &actor = *texture.actorMaterial;
				const float level = DrawingCathedral ? static_cast<float>(std::clamp(shade, 0, 15))
					: std::clamp(diffuse * (1 - shadow), 0.0F, 1.0F) * (actor.lightLevels - 1);
				const unsigned low = static_cast<unsigned>(level), high = std::min(low + 1, actor.lightLevels - 1);
				const auto base = std::size_t(albedoColor) * actor.lightLevels;
				const unsigned nearest = std::min(static_cast<unsigned>(level + 0.5F), actor.lightLevels - 1);
				destination[x] = actor.indexedLightLut[base + nearest];
				actorRgb = BlendWorldRgb(actor.rgbaLightLut[base + low], actor.rgbaLightLut[base + high], level - low);
			} else if (texture.emissive) {
				destination[x] = color;
			} else if (interior != nullptr) {
				TownLightingConfig roomLight;
				roomLight.ambient = { 0.015F, 0.013F, 0.010F };
				roomLight.directionalIntensity = 0;
				const std::span<const TownPointLight> lights(interior->lights.data(), interior->lightCount);
				const TownLightingSample lighting = SampleTownLighting({ normal.x, normal.y, normal.z },
					{ world.x, world.y, world.z }, 0, roomLight, lights, interior->room->lightOccluders);
				const float amount = interior->room->fireColor.red > 0 ? lighting.point.red / interior->room->fireColor.red : 0;
				const size_t level = static_cast<size_t>(std::clamp(static_cast<int>(amount / InteriorPointLightRange * (ImportedLightLevels - 1) + 0.5F), 0, static_cast<int>(ImportedLightLevels - 1)));
				destination[x] = texture.interiorLightTables[albedoColor][level];
			} else if (hasAlbedo) {
				const size_t amount = static_cast<size_t>(std::clamp(static_cast<int>(diffuse * (1 - shadow) * (ImportedLightLevels - 1) + 0.5F), 0, static_cast<int>(ImportedLightLevels - 1)));
				destination[x] = ImportedAlbedoLightTables[albedoColor][amount];
			} else {
				destination[x] = shadowLevel == 0 ? lightTable[color]
				    : SceneShadowTables[std::clamp(shade, 0, 3)][shadowLevel][color];
			}
			if (!SmoothWorldPixels.empty()) {
				const float smoothShadow=receivesShadow?SmoothShadowOcclusion(world,shadowReceiver):shadow;
				uint32_t rgb = actorRgb != 0 ? actorRgb : SmoothWorldPalette[destination[x]];
				if (!texture.actorMaterial && !texture.emissive && !CathedralPaletteBlend) {
					float level=0;
					if (DrawingCathedral) {
						const uint8_t base=texture.lightingBasePixels.empty()?color:unlitColor;
						level=SmoothNativeLight(world);const unsigned low=unsigned(level),high=std::min(low+1,15U);
						const auto codeAt=[&](unsigned value){return value==0?base:value==15?uint8_t(0):LightTables[value][base];};
						rgb=BlendWorldRgb(SmoothWorldPalette[codeAt(low)],SmoothWorldPalette[codeAt(high)],level-low);
					} else if (interior != nullptr) {
						TownLightingConfig ambient;ambient.ambient={.015F,.013F,.010F};ambient.directionalIntensity=0;
						const auto sample=SampleTownLighting({normal.x,normal.y,normal.z},{world.x,world.y,world.z},0,ambient,{interior->lights.data(),interior->lightCount},interior->room->lightOccluders);
						if (!texture.interiorRgbLightTables.empty()) {
							const auto &row = texture.interiorRgbLightTables[albedoColor];
							const auto toned = [&](uint32_t value) {
								return ToneWorldRgb({ float((value >> 16) & 255) / 255, float((value >> 8) & 255) / 255, float(value & 255) / 255 });
							};
							const auto channel = [&](float point, unsigned shift) {
								const float amount = std::clamp(point / InteriorPointLightRange, 0.0F, 1.0F) * (ImportedLightLevels - 1);
								const unsigned low = unsigned(amount), high = std::min<unsigned>(low + 1, ImportedLightLevels - 1);
								return BlendWorldRgb(toned(row[low]), toned(row[high]), amount - low) & (255U << shift);
							};
							rgb = 0xFF000000U | channel(sample.point.red, 16) | channel(sample.point.green, 8) | channel(sample.point.blue, 0);
						} else {
							level=std::clamp(sample.point.red/interior->room->fireColor.red/InteriorPointLightRange,0.0F,1.0F)*(ImportedLightLevels-1);
							const unsigned low=unsigned(level),high=std::min<unsigned>(low+1,ImportedLightLevels-1);
							rgb=BlendWorldRgb(SmoothWorldPalette[texture.interiorLightTables[albedoColor][low]],SmoothWorldPalette[texture.interiorLightTables[albedoColor][high]],level-low);
						}
					} else if (hasAlbedo) {
						level=std::clamp(diffuse*(1-smoothShadow),0.0F,1.0F)*(ImportedLightLevels-1);
						const unsigned low=unsigned(level),high=std::min<unsigned>(low+1,ImportedLightLevels-1);
						rgb=BlendWorldRgb(SmoothWorldPalette[ImportedAlbedoLightTables[albedoColor][low]],SmoothWorldPalette[ImportedAlbedoLightTables[albedoColor][high]],level-low);
					} else {
						level=std::clamp(smoothShadow*3,0.0F,3.0F);const unsigned low=unsigned(level),high=std::min(low+1,3U);
						rgb=BlendWorldRgb(SmoothWorldPalette[SceneShadowTables[std::clamp(shade,0,3)][low][color]],SmoothWorldPalette[SceneShadowTables[std::clamp(shade,0,3)][high][color]],level-low);
					}
				}
				std::array<float, 4> color {};
				for (size_t component = 0; component < color.size(); ++component) {
					const float first = triangle[0].source.authoredColor[component];
					color[component] = first == triangle[1].source.authoredColor[component] && first == triangle[2].source.authoredColor[component]
						? first : std::clamp(ca * first + cb * triangle[1].source.authoredColor[component] + cc * triangle[2].source.authoredColor[component], 0.0F, 1.0F);
				}
				if (color != std::array<float, 4> { 1, 1, 1, 1 }) {
					const auto linear = TownSrgbToLinear({ float((rgb >> 16) & 255) / 255, float((rgb >> 8) & 255) / 255, float(rgb & 255) / 255 });
					const auto encoded = TownLinearToSrgb({ linear.red * color[0], linear.green * color[1], linear.blue * color[2] });
					const auto byte = [](float value) { return uint32_t(std::clamp(value * 255 + 0.5F, 0.0F, 255.0F)); };
					rgb = (byte(float((rgb >> 24) & 255) / 255 * color[3]) << 24)
						| (byte(encoded.red) << 16) | (byte(encoded.green) << 8) | byte(encoded.blue);
				}
				if (FrameSpritePointLightingValid && receivesFramePointLight && !texture.emissive) {
					const TownLightColor point = SampleTownSpritePointLighting({ world.x, world.y, world.z },
					    { FrameSpritePointLights.data(), FrameSpritePointLightCount }, FrameSpritePointOccluders);
					rgb = TintTownSpritePointArgb(rgb, point);
				}
				SmoothWorldPixels[index] = rgb;
			}
			if (DrawingCathedral && CathedralPaletteBlend) {
				destination[x] = paletteTransparencyLookup[previousColor][destination[x]];
				if (!SmoothWorldPixels.empty()) SmoothWorldPixels[index]=SmoothWorldPalette[destination[x]];
				++RendererState.cpuShadedFragments;
				continue; // Overlay preserves the opaque depth and target owner.
			}
			DepthBuffer[index] = depth;
			++RendererState.cpuShadedFragments;
			if (!pick.preservePicking)
				PickBuffer[index] = pick;
		}
	}
}

void DrawTriangle(const Surface &out, std::array<Vertex, 3> triangle, const Texture &texture,
	PickRecord pick, int shade, bool transparent = false, const TownSceneNormal *authoredNormal = nullptr,
	const InteriorLighting *interior = nullptr, GpuVolumeFallback fallback = {})
{
	std::array<TownCameraVertex, 3> world;
	for (size_t i = 0; i < triangle.size(); ++i)
		world[i] = { { triangle[i].position.x, triangle[i].position.y, triangle[i].position.z }, triangle[i].u, triangle[i].v, triangle[i].authoredColor };
	// Both backends receive the same six-plane clipped vertices, UV and world
	// positions. No perspective vertex is divided before clipping.
	const auto clipped = ClipTownCameraTriangle(ViewCamera.projection, world);
	for (size_t i = 0; i < clipped.count; ++i)
		Rasterize(out, clipped.triangles[i], texture, pick, shade, transparent, authoredNormal, interior, fallback);
}

void DrawQuad(const Surface &out, const std::array<Vec3, 4> &corners, const Texture &texture,
	PickRecord pick, int shade, bool transparent = false)
{
	const std::array<Vertex, 4> vertices { Vertex { corners[0], 0, 0 }, Vertex { corners[1], 1, 0 },
		Vertex { corners[2], 1, 1 }, Vertex { corners[3], 0, 1 } };
	DrawTriangle(out, { vertices[0], vertices[1], vertices[2] }, texture, pick, shade, transparent);
	DrawTriangle(out, { vertices[0], vertices[2], vertices[3] }, texture, pick, shade, transparent);
}

Texture CopySurface(const Surface &surface)
{
	Texture result { surface.w(), surface.h(), {} };
	result.pixels.resize(static_cast<size_t>(result.width) * result.height);
	for (int y = 0; y < result.height; ++y)
		std::memcpy(result.pixels.data() + static_cast<size_t>(y) * result.width, surface.at(0, y), result.width);
	return result;
}

void ClearSurface(const Surface &surface)
{
	for (int y = 0; y < surface.h(); ++y)
		std::memset(surface.at(0, y), 0, surface.w());
}

TileArt DecodeTile(uint16_t piece)
{
	TileArt result;
	result.solid = HasAnyOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile);
	OwnedSurface tileSurface(64, 256);
	ClearSurface(tileSurface);
	OwnedSurface coverageSurface(64, 256);
	for (int y = 0; y < coverageSurface.h(); ++y)
		std::memset(coverageSurface.at(0, y), 255, coverageSurface.w());
	const std::vector<uint8_t> lightBuffer(static_cast<size_t>(tileSurface.pitch()) * tileSurface.h(), 0);
	const Lightmap lightmap(tileSurface.begin(), lightBuffer, tileSurface.pitch(), LightTables,
		IdentityPalette.data(), FullyDarkLightTable);
	const Lightmap coverageLightmap(coverageSurface.begin(), lightBuffer, coverageSurface.pitch(), LightTables,
		IdentityPalette.data(), FullyDarkLightTable);
	const MICROS &micros = DPieceMicros[piece];
	for (int i = 0; i < std::min<int>(MicroTileLen, 16); ++i) {
		const LevelCelBlock block = micros.mt[i];
		if (!block.hasValue())
			continue;
		const Point position { (i & 1) * 32, 255 - (i / 2) * 32 };
		if (!result.solid && i < 2) {
			if (block.type() == TileType::TransparentSquare) {
				RenderTileFoliage(tileSurface, lightmap, position, pDungeonCels.get(), block, IdentityPalette.data());
				RenderTileFoliage(coverageSurface, coverageLightmap, position, pDungeonCels.get(), block, IdentityPalette.data());
			}
		} else {
			RenderTile(tileSurface, lightmap, position, pDungeonCels.get(), block, MaskType::Solid, IdentityPalette.data());
			RenderTile(coverageSurface, coverageLightmap, position, pDungeonCels.get(), block, MaskType::Solid, IdentityPalette.data());
		}
	}
	int top = 224;
	for (int y = 0; y < 224; ++y) {
		bool occupied = false;
		for (int x = 0; x < 64; ++x)
			occupied = occupied || tileSurface[{ x, y }] == coverageSurface[{ x, y }];
		if (occupied) {
			top = y;
			break;
		}
	}
	const int facadeHeight = 256 - top;
	result.original = CopySurface(tileSurface);
	result.original.opacity.resize(result.original.pixels.size());
	for (int y = 0; y < result.original.height; ++y)
		for (int x = 0; x < result.original.width; ++x)
			result.original.opacity[static_cast<size_t>(y) * result.original.width + x] = tileSurface[{ x, y }] == coverageSurface[{ x, y }];
	result.facade = CopySurface(tileSurface.subregion(0, top, 64, facadeHeight));
	result.facade.opacity.resize(result.facade.pixels.size());
	for (int y = 0; y < facadeHeight; ++y)
		std::memcpy(result.facade.opacity.data() + static_cast<size_t>(y) * 64,
			result.original.opacity.data() + static_cast<size_t>(y + top) * 64, 64);
	result.height = std::clamp(static_cast<float>(224 - top) / PixelsPerWorldUnit, 0.0F, 5.5F);
	// MIN/CEL store painted art, not meshes. These heights/boxes are deliberately proxies.
	if (result.solid && result.height > 0)
		result.height = std::max(0.6F, result.height);
	{
		OwnedSurface floorSurface(64, 32);
		ClearSurface(floorSurface);
		OwnedSurface floorCoverage(64, 32);
		for (int y = 0; y < floorCoverage.h(); ++y)
			std::memset(floorCoverage.at(0, y), 255, floorCoverage.w());
		const std::vector<uint8_t> floorLight(static_cast<size_t>(floorSurface.pitch()) * floorSurface.h(), 0);
		const Lightmap floorLightmap(floorSurface.begin(), floorLight, floorSurface.pitch(), LightTables,
			IdentityPalette.data(), FullyDarkLightTable);
		const Lightmap coverageFloorLightmap(floorCoverage.begin(), floorLight, floorCoverage.pitch(), LightTables,
			IdentityPalette.data(), FullyDarkLightTable);
		const auto renderFloor = [&](const Surface &destination, const Lightmap &destinationLightmap) {
			for (int i = 0; i < 2; ++i) {
				const LevelCelBlock block = micros.mt[i];
				if (!block.hasValue())
					continue;
				if (result.solid) {
					// Blocked grass, water and scenery footprints still have their own painted ground.
					// They have not undergone the floor/foliage split, so retain the actual encoding.
					RenderTile(destination, destinationLightmap, { i * 32, 31 }, pDungeonCels.get(),
						block, MaskType::Solid, IdentityPalette.data());
				} else {
					RenderTileFrame(destination, destinationLightmap, { i * 32, 31 },
						i == 0 ? TileType::LeftTriangle : TileType::RightTriangle,
						GetDunFrame(pDungeonCels.get(), block.frame()), DunFrameTriangleHeight, MaskType::Solid, IdentityPalette.data());
				}
			}
		};
		renderFloor(floorSurface, floorLightmap);
		renderFloor(floorCoverage, coverageFloorLightmap);
		// Keep the original diamond at its full pixel resolution. Converting to a
		// square and back discarded native detail even at the original camera pose.
		result.ground = CopySurface(floorSurface);
		result.ground.opacity.resize(result.ground.pixels.size());
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 64; ++x)
				result.ground.opacity[static_cast<size_t>(y) * 64 + x] = floorSurface[{ x, y }] == floorCoverage[{ x, y }];
	}
	return result;
}

const TileArt &GetTile(uint16_t piece)
{
	const auto found = TerrainCache.find(piece);
	if (found != TerrainCache.end())
		return found->second;
	return TerrainCache.emplace(piece, DecodeTile(piece)).first->second;
}

const Texture &SceneGround(uint16_t piece)
{
	const Texture &original = GetTile(piece).ground;
	const TownGroundShadowMask *mask = GetTownGroundShadowMask(piece);
	if (mask == nullptr)
		return original;
	const auto found = SceneGroundCache.find(piece);
	if (found != SceneGroundCache.end())
		return found->second;
	const Texture &donor = GetTile(mask->donorPiece).ground;
	if (original.width != 64 || original.height != 32 || donor.width != 64 || donor.height != 32
	    || original.opacity.size() != 2048 || donor.opacity.size() != 2048)
		return original;
	// Frozen, reviewed regions remove only the painted building shadow. The
	// original grass outside them and the entire original opacity stay intact.
	Texture cleaned = original;
	for (int y = 0; y < 32; ++y) {
		for (int x = 0; x < 64; ++x) {
			const size_t index = static_cast<size_t>(y) * 64 + x;
			if ((mask->rows[y] & (uint64_t { 1 } << x)) != 0
			    && original.opacity[index] != 0 && donor.opacity[index] != 0)
				cleaned.pixels[index] = donor.pixels[index];
		}
	}
	return SceneGroundCache.emplace(piece, std::move(cleaned)).first->second;
}

const Texture &FallbackGround()
{
	// A real walkable town piece supplies grass beneath coarse building geometry.
	for (int y = dminPosition.y; y < dmaxPosition.y; ++y) {
		for (int x = dminPosition.x; x < dmaxPosition.x; ++x) {
			const uint16_t piece = dPiece[x][y];
			if (piece < MAXTILES && HasNoneOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile)) {
				const Texture &ground = GetTile(piece).ground;
				if (std::any_of(ground.pixels.begin(), ground.pixels.end(), [](uint8_t pixel) { return pixel != 0; }))
					return ground;
			}
		}
	}
	static const Texture fallback { 1, 1, { 0 } };
	return fallback;
}

NativeSceneArt ComposeNativeArtwork(const TownSceneModel &model)
{
	const Point minTile = model.nativeArtwork.enabled ? model.nativeArtwork.minTile : model.minTile;
	const Point maxTile = model.nativeArtwork.enabled ? model.nativeArtwork.maxTile : model.maxTile;
	const int spanX = maxTile.x - minTile.x;
	const int spanY = maxTile.y - minTile.y;
	const int width = 32 * (spanX + spanY + 2) + 16;
	const int height = 16 * (spanX + spanY) + 272;
	if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || static_cast<size_t>(width) * height > 8000000)
		return {}; // Same admission bound as the projection-owner builder.
	const Point origin { 32 * (spanY + 1) + 8, 263 };
	OwnedSurface paint(width, height);
	OwnedSurface coverage(width, height);
	ClearSurface(paint);
	for (int y = 0; y < height; ++y)
		std::memset(coverage.at(0, y), 255, width);
	const std::vector<uint8_t> lightBuffer(static_cast<size_t>(paint.pitch()) * height, 0);
	const Lightmap paintLight(paint.begin(), lightBuffer, paint.pitch(), LightTables, IdentityPalette.data(), FullyDarkLightTable);
	const Lightmap coverageLight(coverage.begin(), lightBuffer, coverage.pitch(), LightTables, IdentityPalette.data(), FullyDarkLightTable);
	std::vector<Point> tiles;
	for (int y = minTile.y; y <= maxTile.y; ++y)
		for (int x = minTile.x; x <= maxTile.x; ++x)
			tiles.push_back({ x, y });
	std::stable_sort(tiles.begin(), tiles.end(), [](Point a, Point b) {
		return a.x + a.y != b.x + b.y ? a.x + a.y < b.x + b.y : a.x < b.x;
	});
	for (Point tile : tiles) {
		const uint16_t piece = dPiece[tile.x][tile.y];
		if (piece >= MAXTILES)
			continue;
		const bool outsideGroup = tile.x < model.minTile.x || tile.x > model.maxTile.x || tile.y < model.minTile.y || tile.y > model.maxTile.y;
		if ((outsideGroup || model.kind == TownSceneKind::Cathedral || model.kind == TownSceneKind::Cabin)
			&& model.nativeArtwork.fringeMinPiece != 0
			&& (piece < model.nativeArtwork.fringeMinPiece || piece > model.nativeArtwork.fringeMaxPiece))
			continue;
		const bool floor = HasNoneOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile);
		const Point base { origin.x - 32 + 32 * (tile.x - minTile.x - tile.y + minTile.y),
			origin.y + 16 * (tile.x - minTile.x + tile.y - minTile.y) };
		for (int i = 0; i < std::min<int>(MicroTileLen, 16); ++i) {
			const LevelCelBlock block = DPieceMicros[piece].mt[i];
			if (!block.hasValue())
				continue;
			const Point anchor { base.x + (i & 1) * 32, base.y - (i / 2) * 32 };
			if (floor && i < 2) {
				if (block.type() == TileType::TransparentSquare) {
					RenderTileFoliage(paint, paintLight, anchor, pDungeonCels.get(), block, IdentityPalette.data());
					RenderTileFoliage(coverage, coverageLight, anchor, pDungeonCels.get(), block, IdentityPalette.data());
				}
			} else {
				RenderTile(paint, paintLight, anchor, pDungeonCels.get(), block, MaskType::Solid, IdentityPalette.data());
				RenderTile(coverage, coverageLight, anchor, pDungeonCels.get(), block, MaskType::Solid, IdentityPalette.data());
			}
		}
	}
	NativeSceneArt result;
	result.referenceTile = minTile;
	int left = width, top = height, right = -1, bottom = -1;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (paint[{ x, y }] != coverage[{ x, y }])
				continue;
			left = std::min(left, x);
			top = std::min(top, y);
			right = std::max(right, x);
			bottom = std::max(bottom, y);
		}
	}
	if (right < left)
		return result;
	left = std::max(0, left - 8);
	top = std::max(0, top - 8);
	right = std::min(width - 1, right + 8);
	bottom = std::min(height - 1, bottom + 8);
	result.pixelOrigin = { left - origin.x, top - origin.y };
	result.texture = CopySurface(paint.subregion(left, top, right - left + 1, bottom - top + 1));
	result.texture.opacity.resize(result.texture.pixels.size());
	for (int y = 0; y < result.texture.height; ++y)
		for (int x = 0; x < result.texture.width; ++x)
			result.texture.opacity[static_cast<size_t>(y) * result.texture.width + x] = paint[{ left + x, top + y }] == coverage[{ left + x, top + y }];
	const auto sourceModels = GetTownVisualSceneSourceModels();
	result.projectionOwners = BuildTownSceneProjectionOwners(model, result.referenceTile, result.pixelOrigin,
		result.texture.width, result.texture.height, sourceModels.empty() ? std::span<const TownSceneModel>(GetTownScene()) : sourceModels);
	if (result.projectionOwners.size() != result.texture.pixels.size())
		return {}; // Fail closed before indexing a refused/incomplete owner map.
	result.projectedFaces.resize(model.triangles.size());
	for (size_t triangleIndex = 0; triangleIndex < model.triangles.size(); ++triangleIndex) {
		if (!model.triangles[triangleIndex].nativeProjection)
			continue;
		std::vector<uint8_t> matchingFaces(model.triangles.size(), 0);
		for (size_t owner = 0; owner < model.triangles.size(); ++owner)
			matchingFaces[owner] = TownSceneProjectionOwnerMatches(model, static_cast<int32_t>(owner), triangleIndex);
		int minX = result.texture.width, minY = result.texture.height, maxX = -1, maxY = -1;
		for (int y = 0; y < result.texture.height; ++y) {
			for (int x = 0; x < result.texture.width; ++x) {
				const size_t pixel = static_cast<size_t>(y) * result.texture.width + x;
				const int32_t owner = result.projectionOwners[pixel];
				if (owner < 0 || matchingFaces[owner] == 0 || result.texture.opacity[pixel] == 0)
					continue;
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}
		}
		if (maxX < minX)
			continue;
		NativeSceneFace &face = result.projectedFaces[triangleIndex];
		face.pixelOrigin = { result.pixelOrigin.x + minX, result.pixelOrigin.y + minY };
		face.texture.width = maxX - minX + 1;
		face.texture.height = maxY - minY + 1;
		face.texture.pixels.resize(static_cast<size_t>(face.texture.width) * face.texture.height);
		face.texture.opacity.resize(face.texture.pixels.size());
		for (int y = 0; y < face.texture.height; ++y) {
			for (int x = 0; x < face.texture.width; ++x) {
				const size_t source = static_cast<size_t>(y + minY) * result.texture.width + x + minX;
				const size_t target = static_cast<size_t>(y) * face.texture.width + x;
				face.texture.pixels[target] = result.texture.pixels[source];
				const int32_t owner = result.projectionOwners[source];
				face.texture.opacity[target] = result.texture.opacity[source] != 0
					&& owner >= 0 && matchingFaces[owner] != 0;
			}
		}
	}
	return result;
}

const NativeSceneArt &NativeArtwork(size_t index, const TownSceneModel &model)
{
	const auto found = SceneArtworkCache.find(index);
	if (found != SceneArtworkCache.end())
		return found->second;
	const auto sourceModels = GetTownVisualSceneSourceModels();
	const TownSceneModel &source = index < sourceModels.size() ? sourceModels[index] : model;
	return SceneArtworkCache.emplace(index, ComposeNativeArtwork(source)).first->second;
}

uint8_t ClosestSceneColor(float red, float green, float blue)
{
	float bestDistance = std::numeric_limits<float>::infinity();
	uint8_t best = 0;
	for (size_t i = 0; i < logical_palette.size(); ++i) {
		const SDL_Color color = logical_palette[i];
		const float dr = color.r - red;
		const float dg = color.g - green;
		const float db = color.b - blue;
		const float distance = dr * dr + dg * dg + db * db;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = static_cast<uint8_t>(i);
		}
	}
	return best;
}

void PrepareHorizon()
{
	if (HorizonMesh.triangles.empty()) {
		TownHorizonConfig config;
		// Native diamonds extend beyond tile centers. This conservative bound
		// encloses every floor quad; the visual apron never covers native ground.
		config.bounds = { -1.46875F, -1.46875F, MAXDUNX - 0.46875F, MAXDUNY - 0.46875F };
		HorizonMesh = BuildTownHorizon(config);
		HorizonPaletteValid = false;
	}
	std::array<uint32_t, 256> signature;
	for (size_t i = 0; i < signature.size(); ++i) {
		const SDL_Color color = logical_palette[i];
		signature[i] = static_cast<uint32_t>(color.r) | (static_cast<uint32_t>(color.g) << 8) | (static_cast<uint32_t>(color.b) << 16);
	}
	if (HorizonPaletteValid && signature == HorizonPaletteSignature)
		return;
	TownHorizonPalette linearPalette;
	for (size_t i = 0; i < linearPalette.size(); ++i) {
		const SDL_Color color = logical_palette[i];
		linearPalette[i] = { TownSrgbToLinear(color.r / 255.0F), TownSrgbToLinear(color.g / 255.0F), TownSrgbToLinear(color.b / 255.0F) };
	}
	const TownHorizonColor fogColor { HorizonFog.color.red * SceneDayBrightness * SceneDayBrightness, HorizonFog.color.green * SceneDayBrightness * SceneDayBrightness, HorizonFog.color.blue * SceneDayBrightness * SceneDayBrightness };
	HorizonFogPalette = BuildTownHorizonFogPalette(linearPalette, fogColor);
	HorizonColors.resize(HorizonMesh.triangles.size());
	for (size_t i = 0; i < HorizonMesh.triangles.size(); ++i) {
		const TownHorizonTriangle &triangle = HorizonMesh.triangles[i];
		const auto vec = [](TownCameraPoint p) { return Vec3 { p.x, p.height, p.z }; };
		const Vec3 normal = Cross(vec(triangle.vertices[1] - triangle.vertices[0]), vec(triangle.vertices[2] - triangle.vertices[0]));
		const TownCameraPoint center = (triangle.vertices[0] + triangle.vertices[1] + triangle.vertices[2]) * (1.0F / 3);
		const TownLightingSample light = SampleTownLighting({ normal.x, normal.y, normal.z },
		    { center.x, center.height, center.z }, 0, SceneLightingConfig);
		const TownLightColor color = TownLinearToSrgb(ComposeTownLitColor(
		    { triangle.color.red, triangle.color.green, triangle.color.blue }, light));
		HorizonColors[i] = ClosestSceneColor(color.red * 255, color.green * 255, color.blue * 255);
	}
	for (size_t i = 0; i < HorizonSkyColors.size(); ++i) {
		const float amount = static_cast<float>(i) / (HorizonSkyColors.size() - 1);
		const TownLightColor color = TownLinearToSrgb({ 0.055F + (HorizonFog.color.red - 0.055F) * amount,
		    0.085F + (HorizonFog.color.green - 0.085F) * amount, 0.13F + (HorizonFog.color.blue - 0.13F) * amount });
		HorizonSkyColors[i] = ClosestSceneColor(color.red * 255 * SceneDayBrightness, color.green * 255 * SceneDayBrightness, color.blue * 255 * SceneDayBrightness);
	}
	HorizonPaletteSignature = signature;
	HorizonPaletteValid = true;
}

void DrawHorizon(const Surface &out)
{
	for (size_t i = 0; i < HorizonMesh.triangles.size(); ++i) {
		std::array<Vertex, 3> triangle;
		for (size_t j = 0; j < triangle.size(); ++j) {
			const TownCameraPoint p = HorizonMesh.triangles[i].vertices[j];
			triangle[j] = { { p.x, p.height, p.z }, 0, 0 };
		}
		// Opaque decoration publishes invalid picking with its own depth. It
		// must never preserve the selection of a native surface hidden behind it.
		DrawTriangle(out, triangle, HorizonPaletteTextures[HorizonColors[i]], PickRecord {}, 0);
	}
}

void ApplyHorizonAtmosphere(const Surface &out)
{
	// This shared postpass runs after GPU readback or a complete CPU fallback,
	// before sampling resolve. Only the visible surface's color is transformed.
	for (int y = 0; y < out.h(); ++y) {
		uint8_t *destination = out.at(0, y);
		const size_t sky = static_cast<size_t>(y) * (HorizonSkyColors.size() - 1) / std::max(1, out.h() - 1);
		for (int x = 0; x < out.w(); ++x) {
			const size_t index = static_cast<size_t>(y) * out.w() + x;
			// GPU FP publishes explicit surviving-fragment coverage in its fifth
			// MRT. World depth/picks stay intact; sky/fog must not overwrite hands.
			if (WarriorViewModelCoverage.size() == DepthBuffer.size() && WarriorViewModelCoverage[index] == 1) continue;
			const float depth = DepthBuffer[index];
			if (!std::isfinite(depth)) {
				destination[x] = HorizonSkyColors[sky];
				if(!SmoothWorldPixels.empty()) {
					const float amount=float(y)/std::max(1,out.h()-1);
					auto color=TownLinearToSrgb({.055F+(HorizonFog.color.red-.055F)*amount,.085F+(HorizonFog.color.green-.085F)*amount,.13F+(HorizonFog.color.blue-.13F)*amount});
					color.red*=SceneDayBrightness;color.green*=SceneDayBrightness;color.blue*=SceneDayBrightness;
					SmoothWorldPixels[static_cast<size_t>(y)*out.w()+x]=ToneWorldRgb(color);
				}
				continue;
			}
			const float fogDepth = TownCameraFogDepth(ViewCamera.projection, depth);
			if (fogDepth <= HorizonFog.startDepth)
				continue;
			const size_t step = static_cast<size_t>(std::min(fogDepth, HorizonFog.endDepth) * HorizonDepthSteps);
			if (!SmoothWorldPixels.empty()) {
				const size_t index=static_cast<size_t>(y)*out.w()+x;
				const uint32_t original=SmoothWorldPixels[index]!=0?SmoothWorldPixels[index]:SmoothWorldPalette[destination[x]];
				const float amount=TownHorizonFogAmount(fogDepth,HorizonFog);
				const auto fog=TownLinearToSrgb({HorizonFog.color.red*SceneDayBrightness*SceneDayBrightness,HorizonFog.color.green*SceneDayBrightness*SceneDayBrightness,HorizonFog.color.blue*SceneDayBrightness*SceneDayBrightness});
				const uint32_t target=ToneWorldRgb(fog);
				SmoothWorldPixels[index]=BlendWorldRgb(original,target,amount);
			}
			destination[x] = HorizonFogPalette[HorizonFogDepthLevels[step]][destination[x]];
		}
	}
}

void CompleteOpaqueMaterial(Texture &texture, const std::vector<uint8_t> &coverage, uint8_t fallback)
{
	// Source coverage, rather than palette zero, decides which samples exist.
	// A painted black texel is valid. Unseen material gets the closest sample of
	// the same surface; it never makes a hole in the solid object.
	std::vector<size_t> frontier;
	std::vector<uint8_t> visited(coverage);
	for (size_t i = 0; i < coverage.size(); ++i)
		if (coverage[i] != 0)
			frontier.push_back(i);
	if (frontier.empty()) {
		std::fill(texture.pixels.begin(), texture.pixels.end(), fallback);
		return;
	}
	for (size_t head = 0; head < frontier.size(); ++head) {
		const size_t current = frontier[head];
		const int x = static_cast<int>(current % texture.width);
		const int y = static_cast<int>(current / texture.width);
		const auto visit = [&](int nx, int ny) {
			if (nx < 0 || ny < 0 || nx >= texture.width || ny >= texture.height)
				return;
			const size_t next = static_cast<size_t>(ny) * texture.width + nx;
			if (visited[next] != 0)
				return;
			visited[next] = 1;
			texture.pixels[next] = texture.pixels[current];
			frontier.push_back(next);
		};
		visit(x - 1, y);
		visit(x + 1, y);
		visit(x, y - 1);
		visit(x, y + 1);
	}
}

bool IsSurfaceSample(uint8_t pixel, TownSceneMaterial material)
{
	if (material != TownSceneMaterial::Wall && material != TownSceneMaterial::Roof && material != TownSceneMaterial::Timber)
		return true;
	const SDL_Color color = logical_palette[pixel];
	// Colored glass, doors and glowing windows are features, not a repeating
	// construction material. They remain on their authored native-facing faces.
	const bool blueDoor = color.b > 60 && color.b > color.r * 1.5F && color.b > color.g * 1.15F;
	const bool glowingGlass = color.r > 90 && color.g > 65 && color.b < color.g * 0.30F;
	const bool redWindow = color.r > 130 && color.g < 65 && color.b < 65;
	return !blueDoor && !glowingGlass && !redWindow;
}

SceneMaterials BuildSceneMaterials(const TownSceneModel &model, const NativeSceneArt &art)
{
	constexpr int Resolution = 96;
	constexpr float RepeatLength = 2.0F;
	const std::array<Vec3, 5> fallbackRgb { Vec3 { 40, 35, 31 }, Vec3 { 55, 47, 35 },
		Vec3 { 56, 57, 60 }, Vec3 { 31, 27, 20 }, Vec3 { 17, 25, 35 } };
	SceneMaterials materials;
	for (size_t materialIndex = 0; materialIndex < materials.base.size(); ++materialIndex) {
		Texture &texture = materials.base[materialIndex];
		texture.width = Resolution;
		texture.height = Resolution;
		texture.repeat = true;
		texture.pixels.resize(Resolution * Resolution);
		std::vector<uint8_t> bestCoverage(Resolution * Resolution, 0);
		int bestCount = 0;
		std::vector<const TownSceneTriangle *> candidates;
		for (const TownSceneTriangle &triangle : model.triangles)
			if (static_cast<size_t>(triangle.material) == materialIndex && triangle.nativeProjection)
				candidates.push_back(&triangle);
		const auto uvArea = [](const TownSceneTriangle *triangle) {
			const auto &a = triangle->vertices[0];
			const auto &b = triangle->vertices[1];
			const auto &c = triangle->vertices[2];
			return std::abs((b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u));
		};
		std::stable_sort(candidates.begin(), candidates.end(), [&](const auto *a, const auto *b) { return uvArea(a) > uvArea(b); });
		for (size_t candidateIndex = 0; candidateIndex < std::min<size_t>(candidates.size(), 8); ++candidateIndex) {
			const auto &vertices = candidates[candidateIndex]->vertices;
			const auto &a = vertices[0];
			const auto &b = vertices[1];
			const auto &c = vertices[2];
			const float determinant = (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
			if (std::abs(determinant) < 0.00001F)
				continue;
			const float centerU = (a.u + b.u + c.u) / 3;
			const float centerV = (a.v + b.v + c.v) / 3;
			const float spanU = std::min(RepeatLength, 0.75F * (std::max({ a.u, b.u, c.u }) - std::min({ a.u, b.u, c.u })));
			const float spanV = std::min(RepeatLength, 0.75F * (std::max({ a.v, b.v, c.v }) - std::min({ a.v, b.v, c.v })));
			std::vector<uint8_t> pixels(Resolution * Resolution);
			std::vector<uint8_t> coverage(Resolution * Resolution, 0);
			int count = 0;
			for (int y = 0; y < Resolution; ++y) {
				for (int x = 0; x < Resolution; ++x) {
					const float du = centerU + ((x + 0.5F) / Resolution - 0.5F) * spanU - a.u;
					const float dv = centerV + ((y + 0.5F) / Resolution - 0.5F) * spanV - a.v;
					const float wb = (du * (c.v - a.v) - dv * (c.u - a.u)) / determinant;
					const float wc = ((b.u - a.u) * dv - (b.v - a.v) * du) / determinant;
					const float wa = 1 - wb - wc;
					const float worldX = wa * a.x + wb * b.x + wc * c.x;
					const float worldZ = wa * a.z + wb * b.z + wc * c.z;
					const float worldHeight = wa * a.height + wb * b.height + wc * c.height;
					const float nativeX = 32 * (worldX - worldZ - art.referenceTile.x + art.referenceTile.y);
					const float nativeY = 16 * (worldX + worldZ - art.referenceTile.x - art.referenceTile.y) - 32 * worldHeight;
					const float sourceX = nativeX - art.pixelOrigin.x, sourceY = nativeY - art.pixelOrigin.y;
					if (!std::isfinite(sourceX) || !std::isfinite(sourceY) || sourceX < 0 || sourceY < 0
					    || sourceX >= art.texture.width || sourceY >= art.texture.height)
						continue; // Check before a potentially overflowing float-to-int cast.
					const int sampleX = static_cast<int>(std::floor(sourceX));
					const int sampleY = static_cast<int>(std::floor(sourceY));
					const int32_t triangleIndex = static_cast<int32_t>(candidates[candidateIndex] - model.triangles.data());
					if (sampleX < 0 || sampleY < 0
						|| sampleX >= art.texture.width || sampleY >= art.texture.height
						|| !TownSceneProjectionOwnerMatches(model,
							art.projectionOwners[static_cast<size_t>(sampleY) * art.texture.width + sampleX], triangleIndex))
						continue;
					uint8_t color;
					if (!art.texture.sample((nativeX - art.pixelOrigin.x) / art.texture.width,
							(nativeY - art.pixelOrigin.y) / art.texture.height, color)
						|| !IsSurfaceSample(color, static_cast<TownSceneMaterial>(materialIndex)))
						continue;
					const size_t pixel = static_cast<size_t>(y) * Resolution + x;
					pixels[pixel] = color;
					coverage[pixel] = 1;
					++count;
				}
			}
			if (count > bestCount) {
				bestCount = count;
				bestCoverage = std::move(coverage);
				texture.pixels = std::move(pixels);
			}
		}
		const Vec3 color = fallbackRgb[materialIndex];
		CompleteOpaqueMaterial(texture, bestCoverage, ClosestSceneColor(color.x, color.y, color.z));
	}
	for (const TownSceneMaterialPatch &patch : model.materialPatches) {
		SceneDetailMaterial &detail = materials.details[static_cast<size_t>(patch.surfaceDetail)];
		Texture &texture = detail.texture;
		texture.width = Resolution;
		texture.height = Resolution;
		texture.repeat = patch.repeat;
		texture.pixels.resize(Resolution * Resolution);
		const auto &a = patch.referencePlane[0];
		const auto &b = patch.referencePlane[1];
		const auto &c = patch.referencePlane[2];
		const float determinant = (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
		std::vector<uint8_t> coverage(Resolution * Resolution, 0);
		if (std::abs(determinant) > 0.00001F) {
			for (int y = 0; y < Resolution; ++y) {
				for (int x = 0; x < Resolution; ++x) {
					const float u = patch.uvMin[0] + (x + 0.5F) / Resolution * (patch.uvMax[0] - patch.uvMin[0]);
					const float v = patch.uvMin[1] + (y + 0.5F) / Resolution * (patch.uvMax[1] - patch.uvMin[1]);
					const float wb = ((u - a.u) * (c.v - a.v) - (v - a.v) * (c.u - a.u)) / determinant;
					const float wc = ((b.u - a.u) * (v - a.v) - (b.v - a.v) * (u - a.u)) / determinant;
					const float wa = 1 - wb - wc;
					const float worldX = wa * a.x + wb * b.x + wc * c.x;
					const float worldZ = wa * a.z + wb * b.z + wc * c.z;
					const float worldHeight = wa * a.height + wb * b.height + wc * c.height;
					const float nativeX = 32 * (worldX - worldZ - art.referenceTile.x + art.referenceTile.y);
					const float nativeY = 16 * (worldX + worldZ - art.referenceTile.x - art.referenceTile.y) - 32 * worldHeight;
					if (nativeX < patch.sourceMin.x || nativeY < patch.sourceMin.y
					    || nativeX >= patch.sourceMax.x || nativeY >= patch.sourceMax.y)
						continue;
					uint8_t color;
					if (!art.texture.sample((nativeX - art.pixelOrigin.x) / art.texture.width,
					        (nativeY - art.pixelOrigin.y) / art.texture.height, color))
						continue;
					if ((patch.surfaceDetail == TownSceneSurfaceDetail::Masonry || patch.surfaceDetail == TownSceneSurfaceDetail::Foundation)
					    && !IsSurfaceSample(color, TownSceneMaterial::Wall))
						continue;
					const size_t pixel = static_cast<size_t>(y) * Resolution + x;
					texture.pixels[pixel] = color;
					coverage[pixel] = 1;
				}
			}
		}
		const Vec3 fallback = fallbackRgb[static_cast<size_t>(patch.material)];
		CompleteOpaqueMaterial(texture, coverage, ClosestSceneColor(fallback.x, fallback.y, fallback.z));
		detail.originU = patch.repeat ? 0 : patch.uvMin[0];
		detail.originV = patch.repeat ? 0 : patch.uvMin[1];
		detail.sizeU = std::max(0.001F, patch.repeat ? patch.repeatWorldSize[0] : patch.uvMax[0] - patch.uvMin[0]);
		detail.sizeV = std::max(0.001F, patch.repeat ? patch.repeatWorldSize[1] : patch.uvMax[1] - patch.uvMin[1]);
	}
	return materials;
}

const SceneMaterials &OpaqueSceneMaterials(size_t index, const TownSceneModel &model, const NativeSceneArt &art)
{
	const auto found = SceneMaterialCache.find(index);
	if (found != SceneMaterialCache.end())
		return found->second;
	const auto sourceModels = GetTownVisualSceneSourceModels();
	const TownSceneModel &source = index < sourceModels.size() ? sourceModels[index] : model;
	return SceneMaterialCache.emplace(index, BuildSceneMaterials(source, art)).first->second;
}

const Texture &SceneSurfaceTexture(size_t index, const TownSceneModel &model,
    const TownSceneTriangle &triangle, const Texture &source)
{
	if (model.kind != TownSceneKind::Well || triangle.material != TownSceneMaterial::Water)
		return source;
	const bool poisoned = TownVisualSceneWaterPoisoned(index);
	auto &textures = WellWaterMaterialTextureCache[index][poisoned ? 1 : 0];
	const auto found = textures.find(source.gpuIdentity.value);
	if (found != textures.end())
		return found->second;
	// Bright-to-dark colors measured from the paired native Town CEL well
	// frames (594..597/602..603 vs 3542..3547), palette SHA309c7e28.
	// Preserve source tonality; this is a material transfer, not a CEL redraw.
	constexpr std::array<uint8_t, 7> Poisoned { 5, 19, 17, 45, 47, 54, 80 };
	constexpr std::array<uint8_t, 7> Clean { 43, 59, 62, 75, 91, 99, 110 };
	const auto luminance = [](uint8_t code) {
		const SDL_Color color = logical_palette[code];
		return (54U * color.r + 183U * color.g + 19U * color.b + 128U) / 256U;
	};
	unsigned minimum = 255, maximum = 0;
	for (size_t i = 0; i < source.pixels.size(); ++i) {
		if (!source.opacity.empty() && source.opacity[i] == 0)
			continue;
		const unsigned value = luminance(source.pixels[i]);
		minimum = std::min(minimum, value);
		maximum = std::max(maximum, value);
	}
	Texture water = source;
	for (size_t i = 0; i < water.pixels.size(); ++i) {
		// Preserve the exact source mask and all unrendered bytes. Only Water
		// color codes change; geometry, coverage, UV and native owners do not.
		if (!source.opacity.empty() && source.opacity[i] == 0)
			continue;
		const unsigned last = static_cast<unsigned>(poisoned ? Poisoned.size() - 1 : Clean.size() - 1);
		const unsigned level = maximum > minimum
		    ? ((maximum - luminance(source.pixels[i])) * last + (maximum - minimum) / 2) / (maximum - minimum)
		    : last / 2;
		water.pixels[i] = poisoned ? Poisoned[level] : Clean[level];
	}
	return textures.emplace(source.gpuIdentity.value, std::move(water)).first->second;
}

const Texture &ImportedRgbTexture(size_t index, uint32_t width, uint32_t height, std::span<const uint8_t> rgbBytes, bool cathedralStone = false)
{
	const auto found = ImportedTextureCache.find(index);
	if (found != ImportedTextureCache.end())
		return found->second;
	Texture texture;
	texture.width = static_cast<int>(width);
	texture.height = static_cast<int>(height);
	texture.albedoPixels.resize(static_cast<size_t>(width) * height);
	for (size_t i = 0; i < texture.albedoPixels.size(); ++i) {
		const uint8_t *rgb = rgbBytes.data() + i * 3;
		std::array<uint8_t, 3> stone;
		if (cathedralStone) {
			// Runtime material grading, preserving the original texture bytes.
			const float gray = rgb[0] * 0.2126F + rgb[1] * 0.7152F + rgb[2] * 0.0722F;
			stone = { static_cast<uint8_t>(gray * 0.66F), static_cast<uint8_t>(gray * 0.70F), static_cast<uint8_t>(gray * 0.80F) };
			rgb = stone.data();
		}
		// Six bits per source channel bound the shared LUT to 262144 colors;
		// all levels are generated from albedo, before the final game palette.
		const uint32_t key = (static_cast<uint32_t>(rgb[0] >> 2) << 12)
		    | (static_cast<uint32_t>(rgb[1] >> 2) << 6) | (rgb[2] >> 2);
		const auto color = ImportedAlbedoColors.find(key);
		if (color != ImportedAlbedoColors.end()) {
			texture.albedoPixels[i] = color->second;
			continue;
		}
		const uint32_t colorIndex = static_cast<uint32_t>(ImportedAlbedoLightTables.size());
		ImportedAlbedoColors.emplace(key, colorIndex);
		texture.albedoPixels[i] = colorIndex;
		const TownLightColor base = TownSrgbToLinear({
			(static_cast<float>((key >> 12) & 63) * 4 + 1.5F) / 255,
			(static_cast<float>((key >> 6) & 63) * 4 + 1.5F) / 255,
			(static_cast<float>(key & 63) * 4 + 1.5F) / 255 });
		std::array<uint8_t, ImportedLightLevels> table;
		for (size_t level = 0; level < table.size(); ++level) {
			const float amount = static_cast<float>(level) / (table.size() - 1);
			TownLightingSample lighting;
			lighting.ambient = SceneLightingConfig.ambient;
			lighting.directional = {
				SceneLightingConfig.directional.red * SceneLightingConfig.directionalIntensity * amount,
				SceneLightingConfig.directional.green * SceneLightingConfig.directionalIntensity * amount,
				SceneLightingConfig.directional.blue * SceneLightingConfig.directionalIntensity * amount };
			const TownLightColor lit = TownLinearToSrgb(ComposeTownLitColor(base, lighting));
			table[level] = ClosestSceneColor(lit.red * 255, lit.green * 255, lit.blue * 255);
		}
		ImportedAlbedoLightTables.push_back(table);
	}
	return ImportedTextureCache.emplace(index, std::move(texture)).first->second;
}

const Texture &ImportedSceneTexture(size_t index, const TownImportedTexture &source, bool cathedralStone = false)
{
	return ImportedRgbTexture(index, source.width, source.height, source.rgb, cathedralStone);
}

// Rigid two-part OBJ_SARC visual. Native object state and occupancy remain authoritative.
struct SarcophagusVisual {
    TownSceneModel body, lid;
    bool attempted=false, loaded=false;
    size_t draws=0;
};
SarcophagusVisual Sarcophagus;
void PrepareSarcophagusVisual()
{
    if(Sarcophagus.attempted) return;
    Sarcophagus.attempted=true;
    Sarcophagus.loaded=LoadTownModelOverride(Sarcophagus.body,"d3d-props/sarcophagus/body.d3d","6b5f63e2593bff03ad5b2c20bb1ca87eda2277bcfad4a93375fd89afc8937fc0","sarcophagus-user-r1-body")
        && LoadTownModelOverride(Sarcophagus.lid,"d3d-props/sarcophagus/lid.d3d","caa81b744f59313f981bc60e29ace570008648c8e4019d18fe8e1278bcaff26d","sarcophagus-user-r1-lid");
}
const Texture &RigidPropNativeTexture(size_t key,const TownImportedTexture &source)
{
    if(const auto found=ImportedTextureCache.find(key);found!=ImportedTextureCache.end()) return found->second;
    Texture texture;texture.width=source.width;texture.height=source.height;
    texture.pixels.resize(size_t(source.width)*source.height);
    std::vector<int16_t> colors(262144,-1);
    for(size_t i=0;i<texture.pixels.size();++i) {
        const auto *rgb=source.rgb.data()+i*3;
        const size_t color=(size_t(rgb[0]>>2)<<12)|(size_t(rgb[1]>>2)<<6)|(rgb[2]>>2);
        if(colors[color]<0) colors[color]=ClosestSceneColor(rgb[0],rgb[1],rgb[2]);
        texture.pixels[i]=uint8_t(colors[color]);
    }
    // Cathedral GPU palette/light LUT requires native texel codes, not Town albedo-table indices.
    // The full source RGB texture and UV resolution remain in the imported master.
    return ImportedTextureCache.emplace(key,std::move(texture)).first->second;
}
bool CathedralTileInFrame(Point tile);
struct DoorVisualState {
    Point tile {}; int orientation=-1; bool observed=false,open=false;
    uint32_t started=0; float from=0,progress=0;
};
struct DoorVisual {
    std::array<TownSceneModel,2> models; // leaf, stone arch
    std::array<ResidentMeshArtwork,2> resident;
    std::array<DoorVisualState,MAXOBJECTS> states;
    uint64_t epoch=0;
    bool attempted=false,loaded=false;
    size_t leafDraws=0,archDraws=0,residentDraws=0;
};
DoorVisual DoorModels;
constexpr size_t DoorTextureKey=std::numeric_limits<size_t>::max()-512;
void PrepareDoorVisual()
{
    if(DoorModels.attempted)return;
    DoorModels.attempted=true;
    DoorModels.loaded=LoadTownModelOverride(DoorModels.models[0],"d3d-props/doors/leaf.d3d","a7b83890118b0e855da53408bc5fde7096c9245b7a9c3b89455291cf26ca81cd","door-user-r1-leaf")
        && LoadTownModelOverride(DoorModels.models[1],"d3d-props/doors/arch.d3d","7dce3db8f998679f04a61fe4c70aa9427a37d22e8aebd90e0ee9b201e282bf8a","door-user-r1-stone-arch");
    if(!DoorModels.loaded)return;
    for(size_t part=0;part<2;++part) {
        auto &mesh=DoorModels.resident[part];
        const auto &triangles=DoorModels.models[part].triangles;
        mesh.vertices.reserve(triangles.size()*3);mesh.indices.reserve(triangles.size()*3);
        for(const auto &tr:triangles)for(const auto &v:tr.vertices) {
            mesh.indices.push_back(uint32_t(mesh.vertices.size()));
            mesh.vertices.push_back({{v.x,v.height,v.z},{v.u,v.v},{tr.normal.x,tr.normal.height,tr.normal.z}});
        }
        mesh.ready=true;
    }
}
int64_t PropAnimationDiagnosticTime=-1;
uint32_t PropAnimationTicks(){return PropAnimationDiagnosticTime<0?SDL_GetTicks():static_cast<uint32_t>(PropAnimationDiagnosticTime);}
void PrepareDoorVisualStates(const cathedral::Snapshot &snapshot)
{
    if(!DoorModels.loaded)return;
    if(DoorModels.epoch!=cathedral::LiveEpoch()) {
        DoorModels.states={};DoorModels.epoch=cathedral::LiveEpoch();
    }
    const uint32_t now=PropAnimationTicks();
    for(const auto &door:snapshot.doors) {
        auto &state=DoorModels.states[door.slot];const bool open=door.state!=cathedral::DoorState::Closed;
        const Point tile{door.x,door.z};
        if(!state.observed || state.tile!=tile || state.orientation!=int(door.orientation)) {
            state={tile,int(door.orientation),true,open,now,open?1.0F:0.0F,open?1.0F:0.0F};continue;
        }
        const float t=std::clamp(float(now-state.started)/350.0F,0.0F,1.0F),smooth=t*t*(3-2*t);
        state.progress=state.from+((state.open?1.0F:0.0F)-state.from)*smooth;
        if(open!=state.open){state.from=state.progress;state.open=open;state.started=now;}
    }
}
bool DoorArchitectureHidden(const cathedral::Door &door)
{
    if(!FollowThroughTownArchitecture() || CameraRig.mode()!=TownCameraMode::ThirdPerson || CathedralVisibilityGeometry!=CathedralFrame.geometryRevision)return false;
    for(const auto &tr:CathedralFrame.triangles)if(tr.module==cathedral::ModuleKind::DoorFrame && tr.pick.x==door.x && tr.pick.z==door.z) {
        const auto owner=CathedralVisibilityOwners.find(tr.instanceId);
        return owner!=CathedralVisibilityOwners.end() && owner->second<CathedralHiddenOwners.size() && CathedralHiddenOwners[owner->second]!=0;
    }
    return false;
}
void DrawDoorModels(const Surface &out,const cathedral::Door &door)
{
    if(!CathedralTileInFrame({door.x,door.z}) || DoorArchitectureHidden(door))return;
    const float base=door.orientation==cathedral::DoorOrientation::Left?1.570796327F:0;
    const float progress=DoorModels.states[door.slot].progress;
    const Vec3 hinge=door.orientation==cathedral::DoorOrientation::Left?Vec3{float(door.x),0,float(door.z)-.5F}:Vec3{float(door.x)-.5F,0,float(door.z)};
    const Point tile{door.x,door.z};
    for(size_t part=0;part<2;++part) {
        const float angle=base+(part==0?1.570796327F*progress:0),cs=std::cos(angle),sn=std::sin(angle);
        const Vec3 origin=part==0?hinge+Vec3{.5F*cs,0,.5F*sn}:Vec3{float(door.x),0,float(door.z)};
        const auto world=[&](Vec3 v){return origin+Vec3{cs*v.x-sn*v.z,v.y,sn*v.x+cs*v.z};};
        const float half=part==0?.65F:1.07F,depth=part==0?.17F:.24F,height=part==0?2.4F:2.8F;
        const float inf=std::numeric_limits<float>::infinity();
        TownArchitectureBounds bounds{{inf,inf,inf},{-inf,-inf,-inf},true};
        for(float x:{-half,half})for(float y:{0.0F,height})for(float z:{-depth,depth}) {
            const auto v=world({x,y,z});
            bounds.minimum.x=std::min(bounds.minimum.x,v.x);bounds.minimum.height=std::min(bounds.minimum.height,v.y);bounds.minimum.z=std::min(bounds.minimum.z,v.z);
            bounds.maximum.x=std::max(bounds.maximum.x,v.x);bounds.maximum.height=std::max(bounds.maximum.height,v.y);bounds.maximum.z=std::max(bounds.maximum.z,v.z);
        }
        if(ViewCamera.projection.perspective && !IsTownArchitectureBoundsVisible(ViewCamera.projection,bounds))continue;
        const auto &texture=ImportedTextureCache.at(DoorTextureKey-part);
        PickRecord pick=PickAt(tile,part==0 && door.selectable?PickKind::Object:PickKind::Ground,part==0 && door.selectable?door.slot:-1);
        const int shade=dLight[door.x][door.z];
        if(CaptureGpu && ResidentMeshesEnabledForDiagnostics && ViewCamera.projection.perspective) {
            auto &mesh=DoorModels.resident[part];if(!PrepareResidentMesh(mesh))return;
            TownGpuMeshInstance instance;instance.identity=mesh.identity();instance.pickId=GpuPickId(pick);
            instance.localToWorld={{{cs,0,-sn,origin.x},{0,1,0,0},{sn,0,cs,origin.z}}};
            TownGpuMaterial material;material.lighting=TownGpuLighting::Palette;material.shade=shade;
            if(!TownGpuSubmitMeshInstance(instance,PrepareGpuTexture(texture,nullptr),material)){CaptureGpuFailed=true;return;}
            ++DoorModels.residentDraws;
        } else {
            for(const auto &tr:DoorModels.models[part].triangles) {
                std::array<Vertex,3> vertices;
                for(size_t i=0;i<3;++i){const auto &v=tr.vertices[i];vertices[i]={world({v.x,v.height,v.z}),v.u,v.v};}
                DrawTriangle(out,vertices,texture,pick,shade);
            }
        }
        if(part==0)++DoorModels.leafDraws;else ++DoorModels.archDraws;
    }
}
struct ChestVisualState { Point tile {}; int type=-1; bool observed=false,open=false; uint32_t started=0; float progress=0; };
struct ChestVisual {
    std::array<TownSceneModel,66> parts;
    std::array<ChestVisualState,MAXOBJECTS> states;
    bool attempted=false,loaded=false;
    size_t draws=0;
};
ChestVisual Chest;
constexpr std::array<bool,66> ChestLidParts { true,false,true,true,true,true,true,true,false,false,false,false,true,true,true,true,true,true,true,false,false,false,false,false,true,true,true,true,true,true,false,false,false,false,false,true,true,true,true,false,false,false,false,true,true,false,true,true,false,false,false,false,false,false,true,false,false,false,false,true,false,false,false,false,false,true };
struct ChestVariantVisual {
    std::vector<TownSceneModel> parts;
    std::vector<bool> lid;
    Vec3 hinge {};
    bool attempted=false,loaded=false;
};
std::array<ChestVariantVisual,2> ChestVariants;
void PrepareChestVariants()
{
    { auto &variant=ChestVariants[0]; if(!variant.attempted){variant.attempted=true;variant.loaded=true;variant.parts.resize(82);
    variant.hinge={0.000000F,0.278000F,-0.248000F};
    variant.lid={true,true,false,false,false,false,false,true,true,true,false,false,false,false,false,true,false,false,true,true,true,false,true,true,true,true,true,true,true,true,true,true,false,true,true,true,true,false,true,true,true,true,true,false,false,false,false,true,true,false,true,false,true,false,false,false,true,false,true,false,false,false,false,true,true,false,true,true,true,false,false,false,false,true,true,true,false,true,true,false,true,false};
    constexpr std::array<std::string_view,82> hashes {"099eafc0b0ccd9bca43bf5b96cbdab3c4020f60baa0e074d8ea51928c936c454","9047e29d443d980ad875eec67a99192e1667a0701dc4c72d6c5a0e70cb1217d5","83e08143094058c8a9910d886ac3c6e94b6c18450cba202e18d2d1e90968d8f1","931fe86e2a2964b46caf5b66f42a1991e351c30bf809348b8189253a902886f8","b42dcb3afb8d15b9575433f7fd4307428b5c8eec1b1d712d7c8caf19c7a91378","65a01b2ac54fd4f55ef3eda13bf635daaf4e0589d2bed5191a1ed8cb89336f3d","8d7c2110b3aef6ba85dfb80c2f239593f9557a33698c19eddd2a8765f9ca96c3","9b500c14a8deb4f3863dd9d5fd168a87a1e01d913aac78d4e14762f4d220f243","7b7b1fb3129155e6c36464ab9886e7adf16e2f237bf7046947cd4fd51426cc4f","1fb49e355981bcc48705ac3c3f2487dca28b579b6cd13f13dc26c4f44f6442c1","89accb3527c4ec11a2a1205397984c9ae9e3a9d4d52e1209fffbecd56816b80e","eea51e8d637b10c07bd8346ae6778d4f43648fb7cb8d70002fa40aaf2b29239c","372a9192251ba7ad95924533dcab8bc0b08b446b43a76e5a18cb059c021411db","78a036522992dfa0855d0d0632a24c439b1b79fdb8bd19c5e4d9bbaa64ce82e3","1ebf57765c08e6ad9a1b3c08e95386e16579ec92194ad028737ecb47ffe464b2","0b8f130ab1c62d123cfecf909d57e35a0735648947afde8de08561483273bcc3","059363bdf893275089fd76e79d2eed5c52842c0d597b02fd12c8855bd2539069","37d930cfcb5fc6e77b7bf01acf9d3a1c9eddfb7036355b50f71d2d34cd919091","bfd42663543d896dbe17e21cb19ebaf2ce2f579e319fedb6de1ce1c1290bb24f","07593abcdd7176724819b1f1da472c92368b67232e1742835cdb8cb23af977cd","b0c23a13092f312cf6885b4bf736768c4e54066328fde5837696ea63e3193df6","cb7aacef22a435dc21ab716e01f560bb47694464802f742f48381727c92e7ecc","752beb047dc583adbf62271a32751caaf356b605d7cbc3f46c1aff85a9dc4bb5","2eab9953b018fccbc5b72bb4e4f0c3aaacf76befad83f4024857a9d68bfe4585","5693cc79a2b65f5b3533c96968778db2f3af9e066f91058e137de7b8c1817e0b","bc8c1f9aec6d894944166dfa24af989a0b8c8f3796c06d7556058625c060f888","ed5bcda6d7c4168b040e8cdc9a016384bee11120626907c8e4da5d1cf3782dd5","a7bf064a8535dc62abfbd338c4635bcde692aed421e1df209ae059d187fe6aac","24be27fa0714e9d854fd24da66f91a0eef0ae06bc220026b146eb836aa0a58ac","fb495d0d632ab04ca0c45c15f50d02f445d1e3e692a5a1db747dd6a772af01b5","b6e57d944a8e762ecf259e20ff087f4cfbfff63cb4a7979602e618fa3f9ccd7b","72a15ace489d72b1a63465e418b8e4897cb9dc9dfbfff9a26168cba818c21e6f","be7748fd90750ffa5e73881b815d1dd523177e4a76982aec740028bf72fff136","c49181ae89b43b09b3dd9719d313d45cc371e56a7a3da62089c6eb26121d0bef","23787afb50cb347a9bfd1e7d25ea64a942ea5c4823901b65b6ca1042d2a9fb6a","ad3ba6d2ac6b644765b923086a8bece4156abc61d4e0867e96a7453c59cd8959","b5e6d1131e0215d96b3d8fe686af7aca0224e118993fc247bfc1e65b82b2d7a7","3dc6d3c3b7e9e631c33b22b0533de22da9e702091585e9356743a716fa90edb1","af8e98b0126d68dbfae1845274eb05b41c29727b4527c3d5492bb48b41a6ce45","0986226d9b41f726b66e64d0eed9fafcd3f68f9b52f577fdbaad5891d3cd1c10","9bbc0392906c2c85d046c19cc9ced34d69dcb05782baccb68c03b45645c8ea7e","6a0e1f7d13d0e61b62611cda571bfa4b2371dde5f9a98038dc19da6dd5d496eb","600e379b4fb8dd180f12c2f1ec01244f4ad1f3627e9d6d9cdaeab06dd1267453","918e997ae4bc6791be4145bfc991103c30f7fe2e3afbfc27b1542cecd4915030","d4f846101b5e4646bf03148d94381df3572984a8dafce9f10ea60fdcdc8733da","a39b658a198d651642fbbc98e7b533309cb0430cba4917d8114b7eb0bd34f8ba","2d919b85190ed3f6bff21552521fd6beadb3c113942e1415bb06a59f617b7ab7","bf5a3ae74c86ec43a29d26fe2edeb6eca9763d26f9cbfb43409f1bf0a09daacd","e7dbf300600157d033483225d261a9524099db22ed0d7caf4193ce0885298c75","8ea77460840fa548a882393209d65543eb3c6a2f3425a63a403adf6cd0f02be9","c048ab40e707b008ffad98cea322fc4c180b4c6c8b3ee151321171dc5ca596c4","6902b16c90b7a7f8882a2c37c7a851d3eec4c58acba01f75a0eaadefc127deca","53bec5722db0e503b82ac6b23754198832a9945bf4af6ecf4e5074f942a3b0be","291e3207ec279c39cf01eae9543ca1cd658ef6924663755b79cdc7e03893764e","0da9a3105941fb67ddcde6d4c3b7c245415ed31e9c3316e31269a0e9c89483e6","b02c5e39ecf40f5e68e4fd557a320b50cb2287671a1f674e7d7be08a92de6174","0a0abead679d94f95d5672b1648a6696bd618e94774156116c0aa1c5e3904e48","e903be3ba492cd41e505da014e401ae98f99e13070b89d989d9b0ff6029f49be","f8f57201c31abb66a4adcf129c6775f2cb9ed4b850ce93fa6208bfccbb84e885","b3ac183261aa4eebde3c9d083761dfd2d7d7baad2f7bf814f492569ea5a8c532","8b3b072233a996252be0f341a3d68fcb1d20ba5404e989ab87d3242bb31b8a17","d27993d0170f730ebc6989530ffb0838056c29fd8dfbeb8956900b3ac1a6f52d","571a93b95975a4aaeefd05a4c4585121ca7b8dd1e85415baed8de634e6a3c937","d29b41e59d8b4f0c69bde162a21eabf8af0dcafc2c16b499270ae160e5a0f265","e8f9d2f3a726ee16355ed4da8548f27ea809b30196d565b0394077e4c25e48dc","335304a70adc762ed35fe9db3ad1ac2dea201eddfc6052aaa8dd4e99aee4ef39","29c64ac013106c1d1013a591ec8edd52a67a7afcb4c5835373bbefd870bd51fd","db58f5734b7ed807eca9e1522888e79f5113cc485f2e5a9a95ecc58214675c6f","349cbb707e90e4da07edc08db391f057980fb7fc7daab9e614496fa0cf890db4","8c9304267e063254996a5f66edce7320b7c26686f77a78bb6b4bddfdb9f24f9f","4c7d48f82bada9c68671a40a19fee9ba38f7ff174967bfc90dc3feb953a8c930","869ce85bfc8ff39921c8ad124a43b4e9fb82cdbd0ce9debb384d9c5966155631","42b04b1ddedebdcbe6b7e01d8d77fdca16a906e5dbd6caed143212117b31f4db","474256cd73770615a392587081fd09ddd2e10922868faa58a5add5c19ced8471","925952d11671d62552cee64f92787247fcc8a5d73e38a9bdd9ca85caaf7a79b6","8cf62bbda832ca47108d94eb4ec209aa16335542f16e43190cbafa983fe24022","a7e6b8a8172865ccdbc13fd872dd42996a69a1bb480938a9d922a86a0665cb75","a88c6df5b70e19043cf5d7e6543bd34bf200b47b95aa61782d3701e83ff877a3","d1aaba02b4ffc3f1bf4e02b85b33bfde1d089e5e1000df7991a612b39a043cc5","78759cc97bfe6f19f105fce09b9249446e14556e81baa4a0cdef5a1d6e550327","dc0f531204162e82687b61df22839d75cacb26a4e68900d047ddb0d72510813e","a0087f32d782db870bde5da70bfdf0de12258e7bb9e3413fbba4933f1a757ccb"};
    for(size_t i=0;i<hashes.size();++i) variant.loaded=LoadTownModelOverride(variant.parts[i],"d3d-props/chest-reinforced/part"+std::to_string(i)+".d3d",hashes[i],"chest-reinforced-user-r1") && variant.loaded;
    }}
    { auto &variant=ChestVariants[1]; if(!variant.attempted){variant.attempted=true;variant.loaded=true;variant.parts.resize(45);
    variant.hinge={0.000000F,0.548000F,-0.400000F};
    variant.lid={false,true,true,false,true,false,true,false,true,false,false,false,false,true,true,true,false,true,true,false,false,true,false,true,true,true,true,false,true,true,false,false,true,true,false,false,false,false,false,false,false,true,true,true,true};
    constexpr std::array<std::string_view,45> hashes {"d4580325069f23aa359138ba7f12d90ca78d5c237c122080466017d317f5e0e0","3baee806cb017d50b2cb03359ee0b6c643916ad75c20f5547e7432d000befd3d","1ce1e0b8422cf08cbdfc1acec5dc719c9b9b11d9f30ef955a779a9fe023e61f1","aed9e8f25f1c52b5119bdd8dce0c7bd4cc1010a8470afcb8a11ff9c5d8f90ee6","d7041878344a24a17c296cba130363d925aa5272f883f97389031dfaff620aeb","f646032b9e3ca475095ba0221c7225e7cc3c0c8061468ee6137373670a17c09a","5e65a46f05efde6595e42e677144808628111faf21ded1b1de87fa429f97a5da","e56f63454ec281c1a455dd873ec471843c67b3116e57210c385140abe7064516","ac0f90935e60698a250e6dc620656a8052348ceee85e628b7132707b9c026ea1","838a256acb40942d51f001660fdf92b8763ffe328c8cf5fef0fbd68423ab30d1","e809f7b2e5492a3673b2f5d404d8843c1d17ee19c51061c6a83760cc637a9413","1c60e64f74ca533ae5bc869c40d145e2e587b7660ba0939a1b571d5c04a5d1e2","22d1921ff1337232b608ea1eec5e78f72cf545aa47710ea6b02fc15fafc79bdf","6d6a476f615b9fadb08cfe1fb24b64bbd335651aa8ed76f033f7c9c330b0894e","69642b45f61b89f12238e11dda9e9c5a95c63c48b1215dc03a940ab3ba01e4de","a7805888aee38d5272df7fdf3c670fa1f4e30b03261c2c72277e0b3c64341151","38b4b30067e21fbbb60f25377eb7131417106de6f3c61c90b0d651e091e6178d","57622a63e23d83fa9e8ee217dea0d9862cba819ed70a1e3e5965830d0d2abe6a","e5cc670961de4bf72cf6f933dcbfa84f80580a17ad99c379e9874fdc1cb2aaa6","2da9f3245b0d0f285ca50362469bff48e21ad46609ff6bcbea423b0ea35faed2","6ce0bf9b795781ddb612cf322a6087db1729399c07f733a48202a19a472292b8","09e1c10ca354ae7d584a1166203ed20d5e7511dc25855b74ae3c14eb700b92d4","a71100b1f16246c65ad9d7d992a398e8fb9eda0b15cfb8c3982b07d3dc798fb7","9054db228226929f900ede46a7151af6d450dadee2309ba16a8a56a7f3cef796","1e9e23a38aa13ebba93ffb78ee9cd9d947545504c74f8e4952a874e6d9ec02d2","cce761d5ed4615a6cc3df67d9b63699a4e9ca010da028ad94c6b8aeefa12e48e","75ce80cd7ce5ef8914949fb7d69aa52f0a443e166a20f6473bf347d0c2c9fb4e","8e72031baba23047e90e90ca677bad19aa73642ba8193e470d78f13877f224d5","b707717ddc99ab3000a4e68aad600195eabfdfb6425c23af91192abb93895d6a","6f6d8a1a05a0a8b7d030f2394c3c72eb11c1d6f6bce3b6151c4fcbf43c2ca73b","3ce63d081f039041cdcb36e0201a16eae572116a1e318e5c18e865ab59e33711","3988558d816dae173d291ad92bd8a0dc07cec68912335a8771f5228671ed76eb","23b2be92e4c6161cc2435f509d00b3360604d2bd01dd0a1ae88b2bf538b778c4","1c2cffc1f9e07fb51400fd81d5d135cf2a90734aa33e85a57298a13579bd1d47","f8e4dacb556a286d726eb8cbc4cbaa783df478a4f806c82e3e9c5ae7ce6dab19","c26a1a9232a3a5eeca63f9d033603892c00578dcbb4d8a1beb08fe2650ef871c","996bf893e1b9ad7340dc770c1b25a7b291aa358d1bfe6c13f2e3df45c4ee95bb","f2a413400cfdb1b15282088c466a91a449441a3d076c455b960ee577638270fe","de7bdec9c39044b2e36a21a596d1ab8a319439e2f8346f1dca9e22c659d200a6","f666bdd54688bd011c46e714203408f28f8ca09cabfa9cf65181c32ba72b1196","fd6753103a70e14b54c2b1d2a116ca6e201d4c03521c7424daea4e8c503c485b","c1646ed0f314d82f06186e21fa3a65898e9ab8a731edc6803f9a6c0a84ca006a","8d38d0dda5e046167ace7bb296539ba13db5a4ac45cfb1087c20744add0a60ae","1c512d8bccfcd0aec560950e2870d51ed1738ee8457338af88aef388f7f69191","5dac48fca41c0e4899e79e3ec1c8b0ed6562379f39e984885313235c7ab19986"};
    for(size_t i=0;i<hashes.size();++i) variant.loaded=LoadTownModelOverride(variant.parts[i],"d3d-props/chest-wooden/part"+std::to_string(i)+".d3d",hashes[i],"chest-wooden-user-r1") && variant.loaded;
    }}
}
int ChestVariantIndex(const Object &object)
{
    if(object._otype==OBJ_CHEST2 || object._otype==OBJ_TCHEST2)return 0;
    if(object._otype==OBJ_CHEST3 || object._otype==OBJ_TCHEST3)return 1;
    return -1;
}
void PrepareChestVisual()
{
    PrepareChestVariants();
    if(Chest.attempted)return;
    Chest.attempted=true;Chest.loaded=true;
    constexpr std::array<std::string_view,66> hashes { "bf9467c22f91daf1afe381d9ab0a84b2184f198cd73d20d86780f6a5b3930a99","bfec70480b209d759b4fee4e1de01cfbe3bd622b7ad448af48c0bcab0278d62e","9524ecedc81d236e00588d144e7e7986df7dfb5e80701b5e3194a0960b168a66","271cee7a90da6520ff5d7d17940f2cd52c1bea6f1b16f9a8fa5fb5d31dcca12a","544d19fa2df5e4b2e110a05e645f9235669b1a77a2432c3d6a75df164c3d54bd","0dd0d060d97b7a2a0c9541478ce32eca8de8280583e677e0780fe3bc78d034bf","6f94126ef5de27d39c4097c66283681667f5cd59cfdcf3525891f1433cb66589","1abc591cfbc87060948294f1d21c8c5a0c07b1309fd29327cbb0d10477343bf5","9ff14a45b2e0169ba63bc235aba2ae94046b6f655d09959a9c49e17da7b9146e","60cbba4d8fc96aa9b882c4828dcb0f48da78ff25daaf12c604cd45a17d11cd19","eb93c2fb10020356219b6deb9203fde434c3e9917d915ff402801ed49ef963a0","3ed4bf3c18c19fa66fb0e07c8b951c53239d70fc6390acdcdbfcf86b7151f9b7","ffe5cc193091ce25a019d0519e2844b479f1827fee09575739d928e2f6c2bb24","d9a939bbe44037f691e50cc30b6ee44ce79af2551875337e4be60de0aba78fac","e3ad074afa700396edd920f8c510897dbe86b6cbc3a79dfc2a86d1af3c74c20b","9b2fa8e95a87164d15dd3680707bdbcfb1b6215bead7ab1a9963720001198e8b","8f9fc8503dcafb332197f95424379cdbd9333c3b2419d3f0e0cb0d7845a05131","ff5c1d6036f0626a5b17c254fbb86b17333c07ebb84effb14c7dff1658f333d2","5a5b5ad8b399c07aba51c3ad716ff14631d78f751bdbe7ec63a15c154285a3b8","42a37a6e5a6adc7276e579469aee7477af3d8684fddb6bee42bc3809a7c94b96","3c07ffe8dacfddde7343c7caa71c552ad526b4daa4c4e4c9316107b403e78dc2","7e19cf38ce329cae2960f591ce78e89f86f57e51375b54a79ab0a8ff9d0f5559","8116443082bfd73ab93542b9325400c6327eaf060dde304662f92d37b99dd2d1","f18da06ef5531955cf1a617b31caf15a01b2a2faf9abc4a4f55c597d425a0e3e","caf8d2a547f7a646dd503495989c99d1860be1398c12607f840b8c64fec03314","0c11e90fb8406ac9c2d0559ac791c0df67180e6703fa39fd54568f6cc261df2d","30239022440076602f3b97be735999b6a271efeef355a97399d2945d866935f2","0fbe30d26f293c8c434d59198f1026b22e84f3f951037a4245079c0d29f9f0c2","e09656e15a4130a4d70667762e774c60e19309582ef3c6121c2ca237280563fc","0196813ded3dd30f84bef5f5aeed8decc635b1282b471a2c5820145da0c24164","9ad64e277c51d63b3f2e99c43d484ce14ebbc66e5fa6f058480a220f7c957426","fa6460464489581775212e6fbc15008c7046ca4ce1e517afcdd9967cfa16e727","f86b097a03ec7a8c6ac5ce170501d61c4af9ad494ac272f7d497920e69ce1a48","4ac3d3936dfde25f1f217fb690cc345b095ed3147f9cf49a29f0e31c24c5712b","ed2e4793d6571c2b223e097b1ee909fc883e5ae7b54fc3a33f46fc6747e43099","ff00db691b218cff5b21f126a0f5b13e1d2dc56c7c6e2810d4f093e91cf5326d","01a749226533caee864abe5659e29e2a7a4b8ed3e0598370e1f6e41925d1bea1","f0ecf09433bef1c2ae4e1a6269e80912c99a8972a519e3007bc8db5a3ae6eba2","af09ca624f66fec59735db17f2415edd24139a7216f2ede3f30f65b70ffc8ecb","33628988805b8db7f3b28d8894e07cceec70574488637f21bd3172855e85ea91","4d3148cb72bbd5a38e1ce9945a07c1970fec15bbcaff57d65479a5e4611da035","2657b7c07bcc4e6b49cf408efe14d83345c21ebc028e728a4c29e6e7e42f85af","cc3e5360cdabff89bbc7d8b82ecf08ad4becaf0c6750f55fb6b05810d1c067fe","d9a7bdb96e4bbf1415cf19607dfa929a861c107faa8d157c0cd5e7eab5fbd01f","d9a5d7635ea857c18ce23848fdb2d12b876c6b3a0c740f5fb86ab01d2ee33448","63909870d323c7443e55914da6a75f921f81d27e0ca26c42fd1c22c1381f389c","17a3bcae40f2ea65520776bf8c54220a66b3412c8f52f0fe898fea46e818e821","b5f4d0782f3e06c8cd165ee0dd2126d779d18f35b77050d1736ae5a33aa54f13","d3d4a88feae101e924dadaefd70cc6be92f3ef2a3466697ed9d5e92deb8851bc","65d45ba0c81123f9422782e67546e7c3acc404a4b4e550ff1661e2be516bee52","6ffa89ba7bf58e6da6c5fb73d5fe0721304a57934a8f4bce2ace0ade6d3dc6d6","6c047aa5d388d87b79b8a72c13faebe6d4d50b7755b1ce4f26e84f1240e3f58f","4053faed99b090dec47c267105a4142ea869b55d32fe9871beb253d3247ae0fa","211288c646b45d08cdfab5a07af7e319378655372c439d70a8ffc2380ef61a3a","d0bf2f43c9f0ad28babe830056d07805775ecf6aa0efa7be409833fd4d5804b8","7c976b3df9a2c9486fe797cfc6c45df98c0c5b189a845016d2ed1a0e0f32411c","3184f9d1e0c23fcb62cdb69811192c1d4670a907a8525754b3af6c23e6519a4e","dffdcf51c768722dd33ce108b70d91dc8e7e8cfc27df0dc1c881a08d4522a730","92871b93f521f2b1d7e40086dbdcbd7cea440a4578541a244541e0ae87cbf170","474a8ab29c84ca11a9b9b3bdc97d49af0b61b7339acca8e7b57766c0aa865662","616143e355d49bb3a66b8871ee5fe4cd6e2548c5ef37db71c2410a40d5ae8638","0b3deca47f5989e62191d217d225ab93eade77edfce26db1b76e284614297201","0c8bb934d78d46e601814cac93a2c14dad70afd991e82f5b983e18eb90d60465","aef53a8435a30b91552727584d39a4670baf9f6f585ee76573688d06d1ab6398","45d6db31f59b2f497cf921e650b649385429620e3187166ae40caecd24f083ba","9fdb502d3e4c6b1af663194aea7b9b0e333677549b2751fb98a7140fb330c1c5" };
    for(size_t i=0;i<66;++i)Chest.loaded=LoadTownModelOverride(Chest.parts[i],"d3d-props/chest/part"+std::to_string(i)+".d3d",hashes[i],"chest-user-r1") && Chest.loaded;
}
float ChestVisualProgress(const Object &object,int slot)
{
    auto &state=Chest.states[slot];const bool open=(object._oAnimFrame%3)==0;
    const uint32_t now=PropAnimationTicks();
    if(!state.observed || state.tile!=object.position || state.type!=int(object._otype)) {
        state={object.position,int(object._otype),true,open,now,open?1.0F:0.0F};return state.progress;
    }
    if(open && !state.open){state.started=now;state.progress=0;}
    state.open=open;
    if(!open)state.progress=0;
    else if(state.progress<1)state.progress=std::clamp(float(now-state.started)/600.0F,0.0F,1.0F);
    return state.progress;
}
bool DrawChestVisual(const Surface &out,const Object &object,int slot,bool selectable)
{
    const int choice=ChestVariantIndex(object);
    const ChestVariantVisual *variant=choice>=0 && ChestVariants[choice].loaded?&ChestVariants[choice]:nullptr;
    if(!variant && !Chest.loaded)return false;
    const size_t count=variant?variant->parts.size():Chest.parts.size();
    const Vec3 hinge=variant?variant->hinge:Vec3{0,.330F,-.313F};
    const float t=ChestVisualProgress(object,slot),angle=-1.832595715F*t*t*(3-2*t),cs=std::cos(angle),sn=std::sin(angle);
    for(size_t part=0;part<count;++part) {
        const auto &model=variant?variant->parts[part]:Chest.parts[part];
        const bool lid=variant?variant->lid[part]:ChestLidParts[part];
        const auto &texture=RigidPropNativeTexture(std::numeric_limits<size_t>::max()-(variant?768+choice*128:256)-part,*model.importedTexture);
        const auto rotate=[&](Vec3 v){return lid?Vec3{v.x,v.y*cs-v.z*sn,v.y*sn+v.z*cs}:v;};
        for(const auto &tr:model.triangles) {
            std::array<Vertex,3> vertices;
            for(size_t i=0;i<3;++i){const auto &v=tr.vertices[i];const auto q=lid?hinge+rotate(Vec3{v.x-hinge.x,v.height-hinge.y,v.z-hinge.z}):Vec3{v.x,v.height,v.z};vertices[i]={{float(object.position.x)+q.x,q.y,float(object.position.y)+q.z},v.u,v.v};}
            const auto n=rotate({tr.normal.x,tr.normal.height,tr.normal.z});const TownSceneNormal normal{n.x,n.y,n.z};
            DrawTriangle(out,vertices,texture,PickAt(object.position,selectable?PickKind::Object:PickKind::Ground,selectable?slot:-1),object.applyLighting?dLight[object.position.x][object.position.y]:0,false,&normal);
        }
    }
    ++Chest.draws;return true;
}
struct SkullFireVisual {
    TownSceneModel model;
    bool attempted=false,loaded=false;
    size_t meshDraws=0,flameDraws=0;
};
SkullFireVisual SkullFire;
constexpr size_t SkullFireTextureKey=std::numeric_limits<size_t>::max()-2049;
constexpr float SkullFireFlameHeight=0.112193307F;
void PrepareSkullFireVisual()
{
    if(SkullFire.attempted)return;
    SkullFire.attempted=true;
    SkullFire.loaded=LoadTownModelOverride(SkullFire.model,"d3d-props/skull-fire/skull-fire.d3d","64443c6f3f27e6c86b44426c1584289f63ddfa076f8a469c897757feb4e16cba","cathedral-prop-skull-fire-user-r1");
}
bool DrawSkullFireVisual(const Surface &out,const Object &object)
{
    if(!SkullFire.loaded)return false;
    const auto &texture=RigidPropNativeTexture(SkullFireTextureKey,*SkullFire.model.importedTexture);
    for(const auto &tr:SkullFire.model.triangles){
        std::array<Vertex,3> vertices;
        for(size_t i=0;i<3;++i){const auto &v=tr.vertices[i];vertices[i]={{float(object.position.x)+v.x,v.height,float(object.position.y)+v.z},v.u,v.v};}
        DrawTriangle(out,vertices,texture,PickAt(object.position),dLight[object.position.x][object.position.y],false,&tr.normal);
    }
    ++SkullFire.meshDraws;return true;
}
struct BrazierVisual {
    TownSceneModel model;
    bool attempted=false,loaded=false;
    size_t meshDraws=0,flameDraws=0;
};
BrazierVisual Brazier;
constexpr size_t BrazierTextureKey=std::numeric_limits<size_t>::max()-2048;
constexpr float BrazierFlameContact=1.38F;
void PrepareBrazierVisual()
{
    if(Brazier.attempted)return;
    Brazier.attempted=true;
    Brazier.loaded=LoadTownModelOverride(Brazier.model,"d3d-props/brazier-light01/brazier.d3d","51dbfab247182fddb5ac8660751edaf26fd890afaf5c3f605385df5565664922","cathedral-prop-brazier-light01-user-r1");
}
bool DrawBrazierVisual(const Surface &out,const Object &object)
{
    if(!Brazier.loaded)return false;
    const auto &texture=RigidPropNativeTexture(BrazierTextureKey,*Brazier.model.importedTexture);
    for(const auto &tr:Brazier.model.triangles){
        std::array<Vertex,3> vertices;
        for(size_t i=0;i<3;++i){const auto &v=tr.vertices[i];vertices[i]={{float(object.position.x)+v.x,v.height,float(object.position.y)+v.z},v.u,v.v};}
        DrawTriangle(out,vertices,texture,PickAt(object.position),dLight[object.position.x][object.position.y],false,&tr.normal);
    }
    ++Brazier.meshDraws;return true;
}
struct BarrelVisual {
    std::array<TownSceneModel,7> parts;
    bool attempted=false,loaded=false;
    size_t meshDraws=0,nativeBreakDraws=0,nativeExplosionDraws=0,shardDraws=0,terminalShardDraws=0;
};
BarrelVisual Barrel;
void PrepareBarrelVisual()
{
    if(Barrel.attempted) return;
    Barrel.attempted=true;Barrel.loaded=true;
    constexpr std::array<std::string_view,7> hashes { "6f7e059c48af51fbb7a90788a52cda03c25cb3f509c2315fd610f6a564e89c28","b7b01027a6068dff62a3d667f6e486b1b7242c896a27a839c88d7a0b8dd1ae62","1660674fd35715d8d5d1915d1ab335b26f80357a43da8b5b28b22a290b280da6","0368b61a99c38db9da7c7af189b0771c466418443e47a6b82850e78ca2b1afcc","c08b5afbfdb1361ebb437c9da9d6650738ab3a3e5dd2c1942d4dd751f9f6a19b","faf5b46a55464254a509954e7ab5e605c0182743b5bb4b29d6678a818b3bd169","6fd89cd72cfbc37540f583e251bb30e39233a4a439943191a2735f2941b6c3c3" };
    for(size_t i=0;i<7;++i) Barrel.loaded=LoadTownModelOverride(Barrel.parts[i],"d3d-props/barrel/part"+std::to_string(i)+".d3d",hashes[i],"barrel-user-r1") && Barrel.loaded;
}
bool DrawBrokenBarrelShards(const Surface &out,const Object &object)
{
    if(!Barrel.loaded) return false;
    const float t=std::clamp(float(object._oAnimFrame-1)/float(std::max(1,int(object._oAnimLen)-1)),0.0F,1.0F);
    constexpr int shards=12;
    // Stable tile identity, not camera yaw or simulation RNG. Terminal geometry is reproducible after loading.
    const uint32_t seed=uint32_t(object.position.x)*73856093U ^ uint32_t(object.position.y)*19349663U;
    for(size_t part=0;part<7;++part) {
        const auto &model=Barrel.parts[part];
        const auto &texture=RigidPropNativeTexture(std::numeric_limits<size_t>::max()-16-part,*model.importedTexture);
        for(int shard=0;shard<shards;++shard) {
            const float theta=(float(shard)+0.5F)*6.283185307F/shards-3.141592654F;
            const Vec3 radial{std::cos(theta),0,std::sin(theta)},axis{-std::sin(theta),0,std::cos(theta)};
            const float tilt=-1.570796327F*t,cs=std::cos(tilt),sn=std::sin(tilt);
            const float scatter=0.38F+float((seed>>(shard%16))&7U)*0.025F;
            const Vec3 center=radial*0.28F+Vec3{0,0.5F,0};
            const auto group=[](const TownSceneTriangle &tr) {
                const float x=(tr.vertices[0].x+tr.vertices[1].x+tr.vertices[2].x)/3;
                const float z=(tr.vertices[0].z+tr.vertices[1].z+tr.vertices[2].z)/3;
                return std::clamp(int((std::atan2(z,x)+3.141592654F)*12/6.283185307F),0,11);
            };
            const auto rotate=[&](Vec3 v){return v*cs+Cross(axis,v)*sn+axis*(Dot(axis,v)*(1-cs));};
            const auto transform=[&](const TownSceneVertex &v){return center+rotate(Vec3{v.x,v.height,v.z}-center)+radial*(scatter*t);};
            float bottom=100;
            for(const auto &tr:model.triangles)if(group(tr)==shard)for(const auto &v:tr.vertices)bottom=std::min(bottom,transform(v).y);
            if(bottom==100)continue;
            // Land each rigid fragment on the floor; an arc lifts it while it separates.
            const float lift=-bottom*t+0.45F*std::sin(3.141592654F*t)+0.008F*t;
            for(const auto &tr:model.triangles)if(group(tr)==shard) {
                std::array<Vertex,3> vertices;
                for(size_t i=0;i<3;++i){const auto &v=tr.vertices[i];const auto q=transform(v);vertices[i]={{float(object.position.x)+q.x,q.y+lift,float(object.position.y)+q.z},v.u,v.v};}
                const auto n=rotate({tr.normal.x,tr.normal.height,tr.normal.z});const TownSceneNormal normal{n.x,n.y,n.z};
                DrawTriangle(out,vertices,texture,PickAt(object.position),dLight[object.position.x][object.position.y],false,&normal);
            }
        }
    }
    ++Barrel.shardDraws;if(t>=1)++Barrel.terminalShardDraws;return true;
}
bool DrawBarrelVisual(const Surface &out,const Object &object,int slot,bool selectable)
{
    // Break/explosion/terminal remain a separate native effect layer. Replacing
    // their visual later must not replace native damage, chain reactions or loot.
    if(!Barrel.loaded || object.IsBroken()) return false;
    for(size_t part=0;part<7;++part) {
        const auto &model=Barrel.parts[part];
        const auto &texture=RigidPropNativeTexture(std::numeric_limits<size_t>::max()-16-part,*model.importedTexture);
        for(const auto &triangle:model.triangles) {
            std::array<Vertex,3> vertices;
            for(size_t i=0;i<3;++i) {const auto &v=triangle.vertices[i];vertices[i]={{float(object.position.x)+v.x,v.height,float(object.position.y)+v.z},v.u,v.v};}
            const PickRecord pick=PickAt(object.position,selectable ? PickKind::Object : PickKind::Ground,selectable ? slot : -1);
            DrawTriangle(out,vertices,texture,pick,object.applyLighting ? dLight[object.position.x][object.position.y] : 0,false,&triangle.normal);
        }
    }
    ++Barrel.meshDraws;return true;
}
bool DrawSarcophagusVisual(const Surface &out,const Object &object,int slot,bool selectable)
{
    if(!Sarcophagus.loaded) return false;
    const float progress=std::clamp((float(object._oAnimFrame)-1.0F)/4.0F,0.0F,1.0F);
    // Source raw parts intersect by 0.00586 world units; explicit rigid clearance avoids it.
    const float lift=0.0064F+(progress>0 ? 0.016F : 0);
    for(int part=0;part<2;++part) {
        const auto &model=part==0 ? Sarcophagus.body : Sarcophagus.lid;
        const auto &texture=RigidPropNativeTexture(std::numeric_limits<size_t>::max()-part,*model.importedTexture);
        for(const auto &triangle:model.triangles) {
            std::array<Vertex,3> vertices;
            for(size_t i=0;i<3;++i) {
                const auto &v=triangle.vertices[i];
                vertices[i]={{float(object.position.x)+v.x-(part==1 ? 0.36F*progress : 0),
                    v.height+(part==1 ? lift : 0),float(object.position.y)-0.5F+v.z},v.u,v.v};
            }
            PickRecord pick=PickAt(object.position);pick.kind=selectable ? PickKind::Object : PickKind::Ground;pick.entity=selectable ? slot : -1;
            DrawTriangle(out,vertices,texture,pick,object.applyLighting ? dLight[object.position.x][object.position.y] : 0,false,&triangle.normal);
        }
    }
    ++Sarcophagus.draws;
    return true;
}

Texture DecodeSprite(ClxSprite sprite, bool highDefinition = false);
Texture ActorGroundShadow(const Texture &texture);

// Private Ogden runtime pilot: opt-in exact package, CPU skin, existing draw path.
constexpr size_t OgdenImportedTextureKey = std::numeric_limits<size_t>::max();
constexpr std::string_view OgdenPilotAssetPath = "d3d-actors/ogden-idle-r1.actor";
constexpr std::string_view OgdenPilotPackageSha256 = "9b7793d3e2187228a206ff139ba9e44e575de2f7ff40a783f4eee3d5b970759d";
constexpr std::string_view OgdenPilotSelection =
    "D3DOGDEN1\n"
    "assetId=tristram.actor.ogden\n"
    "variant=default\n"
    "revision=ogden-idle-transport-r1-20261009\n"
    "sourceSha256=ff6bed48aa22ea8cbd415e1ba5931a397189cb09b5b81acc61e826314f0e306f\n"
    "packageSha256=9b7793d3e2187228a206ff139ba9e44e575de2f7ff40a783f4eee3d5b970759d\n";

struct OgdenPilotArtwork {
	bool attempted = false;
	bool ready = false;
	ActorModel model;
	OgdenIdlePilotSample sample;
	Texture shadow;
	uint64_t shadowGeneration = std::numeric_limits<uint64_t>::max();
	uint16_t shadowFrame = std::numeric_limits<uint16_t>::max();
	uint64_t poseTicks = std::numeric_limits<uint64_t>::max();
	uint64_t poseGeneration = std::numeric_limits<uint64_t>::max();
	uint16_t poseProgress = std::numeric_limits<uint16_t>::max();
	uint16_t retainedProgress = 0;
	double poseTickHz = 0;
	bool posePaused = false;
};

OgdenPilotArtwork OgdenPilot;
TownViewOgdenPilotState OgdenPilotState;
bool OgdenGpuSubmissionFailed = false;
bool FailOgdenGpuSubmissionForDiagnostics = false;

bool EnsureOgdenIdlePilotLoaded()
{
	if (ShouldDisableKnownMeshyRuntimeContent(OgdenPilotPackageSha256))
		return false; // No selection/package read, CPU skin or texture preparation.
	if (OgdenPilot.attempted)
		return OgdenPilot.ready;
	OgdenPilot.attempted = true;
	OgdenPilotState.attempted = true;
	try {
		AssetRef selectionRef = FindAsset("d3d-actors/ogden-idle-r1.selection");
		if (!selectionRef.ok())
			return false; // No explicit selection: preserve the current actor.
		if (selectionRef.size() != OgdenPilotSelection.size())
			return false;
		AssetHandle selectionHandle = OpenAsset(std::move(selectionRef));
		std::string selection(OgdenPilotSelection.size(), '\0');
		if (!selectionHandle.ok() || !selectionHandle.read(selection.data(), selection.size()) || selection != OgdenPilotSelection)
			return false;
		AssetRef assetRef = FindAsset(OgdenPilotAssetPath);
		if (!assetRef.ok())
			return false;
		const size_t assetBytes = assetRef.size();
		if (assetBytes != 13190983U)
			return false;
		std::vector<uint8_t> bytes(assetBytes);
		AssetHandle assetHandle = OpenAsset(std::move(assetRef));
		if (!assetHandle.ok() || !assetHandle.read(bytes.data(), bytes.size()))
			return false;
		if (ModHashToHex(ComputeBytesSha256(std::as_bytes(std::span<const uint8_t>(bytes)))) != OgdenPilotPackageSha256)
			return false;
		std::string error;
		ActorModel model;
		if (!LoadActorModel(bytes, model, error) || model.assetId != OgdenIdlePilotAssetId
		    || model.variant != OgdenIdlePilotVariant || model.revision != OgdenIdlePilotRevision
		    || model.sourceSha256 != OgdenIdlePilotSourceSha256)
			return false;
		OgdenPilot.model = std::move(model);
		OgdenPilot.ready = true;
		OgdenPilotState.loaded = true;
		Log("Ogden idle pilot: loaded revision={}, package={}, generic clip loops at its authored duration; native sequence remains authoritative",
		    OgdenPilot.model.revision, OgdenPilotPackageSha256);
		return true;
	} catch (const std::bad_alloc &) {
		return false;
	}
}

void PrepareOgdenIdlePilotTexture()
{
	const bool present = std::any_of(Towners.begin(), Towners.end(), [](const Towner &towner) {
		return towner._ttype == TOWN_TAVERN && towner.anim;
	});
	if (!present || !EnsureOgdenIdlePilotLoaded())
		return;
	const size_t previousLutSize = ImportedAlbedoLightTables.size();
	try {
		ImportedRgbTexture(OgdenImportedTextureKey, OgdenPilot.model.textureWidth,
		    OgdenPilot.model.textureHeight, OgdenPilot.model.textureRgb);
	} catch (const std::bad_alloc &) {
		// Roll back append-only actor colors before any GPU table snapshot.
		// A failed color-map insertion/table growth must not leave dangling IDs.
		for (auto it = ImportedAlbedoColors.begin(); it != ImportedAlbedoColors.end();) {
			if (it->second >= previousLutSize)
				it = ImportedAlbedoColors.erase(it);
			else
				++it;
		}
		ImportedAlbedoLightTables.resize(previousLutSize);
		ImportedTextureCache.erase(OgdenImportedTextureKey);
		OgdenPilot.ready = false;
		OgdenPilotState.loaded = false;
	}
}

bool DrawOgdenIdlePilot(const Surface &out, const Towner &towner, size_t index)
{
	if (towner._ttype != TOWN_TAVERN || !OgdenPilot.ready || ShouldDisableKnownMeshyRuntimeContent(OgdenPilotPackageSha256))
		return false;
	bool drawingStarted = false;
	try {
		// Source native state is copied, never mutated by the visual adapter.
		if (towner._tAnimLen != 16 || towner._tAnimDelay != 3 || towner.animOrder.size() != 111
		    || towner._tAnimFrame >= towner._tAnimLen || towner._tAnimCnt < 0
		    || towner._tAnimCnt >= towner._tAnimDelay || towner._tAnimFrameCnt >= towner.animOrder.size())
			return false;
		ActorVisualInput input;
		input.kind = ActorVisualKind::Towner;
		input.nativeIndex = static_cast<uint32_t>(index);
		input.action = ActorVisualAction::Idle;
		input.assetId = OgdenPilot.model.assetId;
		input.variant = OgdenPilot.model.variant;
		input.revision = OgdenPilot.model.revision;
		input.sourceSha256 = OgdenPilot.model.sourceSha256;
		input.authoritativeFootpoint = { static_cast<float>(towner.position.x), 0, static_cast<float>(towner.position.y) };
		input.nativeDirection = 1; // Native Ogden source direction is SouthWest.
		input.logicalFrame = input.displayedFrame = towner._tAnimFrame;
		input.frameCount = towner._tAnimLen;
		input.ticksPerFrame = static_cast<uint32_t>(towner._tAnimDelay);
		input.tickCounter = static_cast<uint32_t>(towner._tAnimCnt);
		input.sequenceIndex = towner._tAnimFrameCnt;
		input.sequenceLength = static_cast<uint32_t>(towner.animOrder.size());
		const uint64_t generation = GetOgdenIdleNativeClockGeneration();
		if (OgdenPilot.poseGeneration != generation)
			OgdenPilot.retainedProgress = 0;
		input.paused = PauseMode == 2;
		if (!input.paused)
			OgdenPilot.retainedProgress = ProgressToNextGameTick;
		input.progressToNextTick128 = OgdenPilot.retainedProgress;
		ActorVisualSnapshot snapshot;
		std::string error;
		if (!MakeActorVisualSnapshot(input, snapshot, error))
			return false;
		OgdenIdlePilotClock clock;
		clock.elapsedNativeTicks = GetOgdenIdleNativeTicks();
		clock.tickHz = sgGameInitInfo.nTickRate;
		// ProcessTowners is not run during a native pause. Both elapsed ticks and
		// retained interpolation stay fixed; draw and GPU retries never tick it.
		clock.holdConfirmed = input.paused;
		if (OgdenPilot.poseTicks != clock.elapsedNativeTicks || OgdenPilot.poseGeneration != generation
		    || OgdenPilot.poseProgress != input.progressToNextTick128 || OgdenPilot.poseTickHz != clock.tickHz
		    || OgdenPilot.posePaused != input.paused || OgdenPilot.sample.pose.positions.empty()) {
			OgdenIdlePilotSample sample;
			if (SampleOgdenIdlePilot(OgdenPilot.model, snapshot, clock, sample, error) != OgdenIdlePilotResult::Sampled)
				return false;
			OgdenPilot.sample = std::move(sample);
			OgdenPilot.poseTicks = clock.elapsedNativeTicks;
			OgdenPilot.poseGeneration = generation;
			OgdenPilot.poseProgress = input.progressToNextTick128;
			OgdenPilot.poseTickHz = clock.tickHz;
			OgdenPilot.posePaused = input.paused;
		}
		// Texture/LUT preparation must precede the GPU's frozen material table.
		const auto texture = ImportedTextureCache.find(OgdenImportedTextureKey);
		if (texture == ImportedTextureCache.end())
			return false;
		const ActorPose &pose = OgdenPilot.sample.pose;
		if (pose.positions.size() != OgdenPilot.model.vertices.size() || pose.normals.size() != pose.positions.size())
			return false;
		if (OgdenPilot.shadowGeneration != generation || OgdenPilot.shadowFrame != towner._tAnimFrame) {
			Texture shadow = ActorGroundShadow(DecodeSprite(towner.currentSprite()));
			OgdenPilot.shadow = std::move(shadow);
			OgdenPilot.shadowGeneration = generation;
			OgdenPilot.shadowFrame = towner._tAnimFrame;
		}
		// All CPU allocations finish before the first pixel/triangle is drawn.
		drawingStarted = true;
		if (!OgdenPilot.shadow.pixels.empty()) {
			const auto onGround = [&](float u, float v) {
				const float px = (u - 0.5F) * OgdenPilot.shadow.width;
				const float py = v * OgdenPilot.shadow.height - OgdenPilot.shadow.height + 1;
				return Vec3 { input.authoritativeFootpoint.x + PhysicalToNativeGrid((2 * py + px) / 64), 0.001F,
					input.authoritativeFootpoint.z + PhysicalToNativeGrid((2 * py - px) / 64) };
			};
			PickRecord shadowPick = PickAt(towner.position);
			shadowPick.preservePicking = true;
			DrawQuad(out, { onGround(0,0), onGround(1,0), onGround(1,1), onGround(0,1) }, OgdenPilot.shadow, shadowPick, 0, true);
		}
		if (CaptureGpu && FailOgdenGpuSubmissionForDiagnostics)
			throw std::bad_alloc();
		const float c = std::cos(snapshot.nativeYawRadians), s = std::sin(snapshot.nativeYawRadians);
		const auto rotate = [&](ActorVec3 value) { return Vec3 { c * value.x + s * value.z, value.y, -s * value.x + c * value.z }; };
		const PickRecord pick = PickAt(towner.position, PickKind::Towner, static_cast<int>(index));
		for (size_t triangle = 0; triangle < OgdenPilot.model.indices.size(); triangle += 3) {
			std::array<Vertex, 3> vertices;
			Vec3 normal {};
			for (size_t corner = 0; corner < 3; ++corner) {
				const uint32_t vertex = OgdenPilot.model.indices[triangle + corner];
				const Vec3 point = rotate(pose.positions[vertex]) * PhysicalToNativeGrid(1);
				vertices[corner] = { { point.x + input.authoritativeFootpoint.x, point.y,
					point.z + input.authoritativeFootpoint.z }, OgdenPilot.model.vertices[vertex].uv.x, OgdenPilot.model.vertices[vertex].uv.y };
				normal = normal + rotate(pose.normals[vertex]);
			}
			const Vec3 face = Cross(vertices[1].position - vertices[0].position, vertices[2].position - vertices[0].position);
			if (Dot(face, ViewCamera.eye - vertices[0].position) <= 0)
				continue;
			const float length = std::sqrt(Dot(normal, normal));
			if (!(length > 0))
				continue;
			normal = normal * (1 / length);
			const TownSceneNormal authored { normal.x, normal.y, normal.z };
			DrawTriangle(out, vertices, texture->second, pick, 0, false, &authored);
			++OgdenPilotState.trianglesVisited;
		}
		OgdenPilotState.drawn = true;
		OgdenPilotState.nativeIndex = static_cast<int>(index);
		OgdenPilotState.nativeTicks = clock.elapsedNativeTicks;
		OgdenPilotState.clockGeneration = generation;
		OgdenPilotState.sampleSeconds = OgdenPilot.sample.sourceSampleSeconds;
		OgdenPilotState.loopSeconds = OgdenPilot.sample.sourceLoopSeconds;
		return true;
	} catch (const std::bad_alloc &) {
		if (drawingStarted && CaptureGpu) {
			// The frame owner discards this batch without downgrading to CPU 3D.
			CaptureGpuFailed = true;
			OgdenGpuSubmissionFailed = true;
			PreparedGpuTextures.clear(); // Insertion may have left incomplete codes.
			return true;
		}
		return false; // Preparation failed before any drawing.
	}
}

const std::array<Texture, InteriorMaterialCount + FlameBandCount * FlameLightLevels> &CabinInteriorTextures(size_t index, const TownCabinInterior &interior)
{
	const auto cached = CabinInteriorTextureCache.find(index);
	if (cached != CabinInteriorTextureCache.end())
		return cached->second;
	std::array<Texture, InteriorMaterialCount + FlameBandCount * FlameLightLevels> textures;
	const bool coloredPoints = std::any_of(interior.fireSources.begin(), interior.fireSources.end(), [&](const TownCabinFireSource &source) {
		return source.light.color.red != interior.fireColor.red || source.light.color.green != interior.fireColor.green || source.light.color.blue != interior.fireColor.blue;
	});
	constexpr std::array<std::array<int, 3>, InteriorMaterialCount> BaseColors { {
		{ 125, 99, 64 }, { 76, 72, 67 }, { 150, 124, 77 }, { 40, 38, 34 }, { 12, 10, 8 }
	} };
	for (size_t material = 0; material < InteriorMaterialCount; ++material) {
		Texture &texture = textures[material];
		texture.width = texture.height = material < 2 ? 64 : 1;
		texture.repeat = true;
		texture.albedoPixels.resize(static_cast<size_t>(texture.width) * texture.height);
		std::unordered_map<uint32_t, uint32_t> colors;
		for (int y = 0; y < texture.height; ++y) {
			for (int x = 0; x < texture.width; ++x) {
				// Deterministic, unlit timber/stone; no copied native lighting.
				const int grain = material < 2 ? ((x * 17 + y * 3 + (x * y) % 13) % 11) - 5 : 0;
				const int joint = material == 0 && (y % 16 == 0 || ((y / 16) % 2 == 0 ? x == 0 : x == 31)) ? -35 : 0;
				const int r = BaseColors[material][0] + grain + joint;
				const int g = BaseColors[material][1] + grain + joint;
				const int b = BaseColors[material][2] + grain + joint;
				const uint32_t key = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
				const auto color = colors.find(key);
				if (color != colors.end()) {
					texture.albedoPixels[static_cast<size_t>(y) * texture.width + x] = color->second;
					continue;
				}
				const uint32_t colorIndex = static_cast<uint32_t>(texture.interiorLightTables.size());
				colors.emplace(key, colorIndex);
				texture.albedoPixels[static_cast<size_t>(y) * texture.width + x] = colorIndex;
				const TownLightColor base = TownSrgbToLinear({ r / 255.0F, g / 255.0F, b / 255.0F });
				std::array<uint8_t, ImportedLightLevels> table;
				for (size_t level = 0; level < table.size(); ++level) {
					const float amount = InteriorPointLightRange * static_cast<float>(level) / (table.size() - 1);
					TownLightingSample lighting;
					lighting.ambient = { 0.015F, 0.013F, 0.010F };
					lighting.point = { interior.fireColor.red * amount, interior.fireColor.green * amount, interior.fireColor.blue * amount };
					const TownLightColor lit = TownLinearToSrgb(ComposeTownLitColor(base, lighting));
					table[level] = ClosestSceneColor(lit.red * 255, lit.green * 255, lit.blue * 255);
				}
				texture.interiorLightTables.push_back(table);
				if (coloredPoints) {
					std::array<uint32_t, ImportedLightLevels> rgbTable;
					for (size_t level = 0; level < rgbTable.size(); ++level) {
						const float amount = InteriorPointLightRange * static_cast<float>(level) / (rgbTable.size() - 1);
						TownLightingSample lighting;
						lighting.ambient = { 0.015F, 0.013F, 0.010F };
						lighting.point = { amount, amount, amount };
						const TownLightColor lit = TownLinearToSrgb(ComposeTownLitColor(base, lighting));
						const auto byte = [](float value) { return static_cast<uint32_t>(value * 255 + 0.5F); };
						rgbTable[level] = 0xFF000000U | (byte(lit.red) << 16) | (byte(lit.green) << 8) | byte(lit.blue);
					}
					texture.interiorRgbLightTables.push_back(rgbTable);
				}
			}
		}
	}
	const std::array<TownLightColor, FlameBandCount> emissions { interior.fireEmissionLinear, interior.fireCoreEmissionLinear, interior.fireTipEmissionLinear };
	for (size_t band = 0; band < emissions.size(); ++band) {
		for (size_t level = 0; level < FlameLightLevels; ++level) {
			const float factor = 1 - FireIntensityVariation + 2 * FireIntensityVariation * static_cast<float>(level) / (FlameLightLevels - 1);
			const TownLightColor emission = TownLinearToSrgb({ emissions[band].red * factor,
				emissions[band].green * factor, emissions[band].blue * factor });
			Texture &flame = textures[InteriorMaterialCount + band * FlameLightLevels + level];
			flame.width = flame.height = 1;
			flame.pixels = { ClosestSceneColor(emission.red * 255, emission.green * 255, emission.blue * 255) };
			flame.emissive = true;
		}
	}
	return CabinInteriorTextureCache.emplace(index, std::move(textures)).first->second;
}

void DrawCabinInterior(const Surface &out, size_t index, const TownCabinInterior &interior)
{
	const auto &textures = CabinInteriorTextures(index, interior);
	InteriorLighting lighting { &interior, {} };
	if (CabinFireEnabledForDiagnostics) {
		lighting.lightCount = std::min<size_t>(interior.fireSources.size(), lighting.lights.size());
		for (size_t i = 0; i < lighting.lightCount; ++i) {
			const auto &source = interior.fireSources[i];
			lighting.lights[i] = source.kind == TownCabinFireKind::EditorPoint
			    ? source.light : TownFireLightAtTime(source.light, source.flickerSeed, FrameFireTime, FireIntensityVariation);
		}
	}
	const auto draw = [&](const TownSceneTriangle &triangle, const Texture &texture, bool roomLit) {
		++ArchitectureCullingState.trianglesVisited;
		std::array<Vertex, 3> vertices;
		for (size_t i = 0; i < vertices.size(); ++i) {
			const auto &source = triangle.vertices[i];
			vertices[i] = { { source.x, source.height, source.z }, source.u, source.v };
		}
		PickRecord pick = PickAt(triangle.pickTile);
		pick.architecture = static_cast<int16_t>(index);
		DrawTriangle(out, vertices, texture, pick, 0, false, &triangle.normal, roomLit ? &lighting : nullptr);
	};
	for (const auto &triangle : interior.interiorTriangles) {
		size_t material = triangle.material == TownSceneMaterial::Timber ? 0 : 1;
		if (triangle.surfaceDetail == TownSceneSurfaceDetail::CandleWax)
			material = 2;
		else if (triangle.surfaceDetail == TownSceneSurfaceDetail::CandleHolder)
			material = 3;
		else if (triangle.surfaceDetail == TownSceneSurfaceDetail::CandleWick)
			material = 4;
		draw(triangle, textures[material], true);
	}
	for (size_t i = 0; i < lighting.lightCount; ++i) {
		const auto &source = interior.fireSources[i];
		const float factor = source.light.intensity > 0 ? lighting.lights[i].intensity / source.light.intensity : 1;
		const size_t level = static_cast<size_t>(std::clamp(static_cast<int>((factor - 1 + FireIntensityVariation)
			/ (2 * FireIntensityVariation) * (FlameLightLevels - 1) + 0.5F), 0, static_cast<int>(FlameLightLevels - 1)));
		for (const auto &triangle : source.emissiveTriangles) {
			const size_t band = triangle.surfaceDetail == TownSceneSurfaceDetail::FireCore ? 1
			    : triangle.surfaceDetail == TownSceneSurfaceDetail::FireTip ? 2 : 0;
			draw(triangle, textures[InteriorMaterialCount + band * FlameLightLevels + level], false);
		}
	}
}

void PrepareArchitectureBoundsCache(const std::vector<TownSceneModel> &scene)
{
	const uint64_t revision = GetTownSceneRevision();
	if (ArchitectureBoundsRevision != revision) {
		ArchitectureBoundsCache.clear();
		ArchitectureBoundsRevision = revision;
	}
	ArchitectureBoundsCache.resize(scene.size());
}

bool IsArchitectureVisible(size_t index, const TownSceneModel &model)
{
	if (!ArchitectureCullingState.requested)
		return true;
	auto &cached = ArchitectureBoundsCache[index];
	if (!cached.ready) {
		cached.bounds = BuildTownArchitectureBounds(model);
		cached.ready = true;
		++ArchitectureCullingState.boundsComputed;
	}
	return IsTownArchitectureBoundsVisible(ViewCamera.projection, cached.bounds);
}

void PrepareFrameImportedAlbedo()
{
	const auto &scene = GetTownScene();
	PrepareArchitectureBoundsCache(scene);
	for (size_t index = 0; index < scene.size(); ++index) {
		const auto &model = scene[index];
		if (model.externalModel && model.importedTexture && IsArchitectureVisible(index, model))
			ImportedSceneTexture(index, *model.importedTexture, model.kind == TownSceneKind::Cathedral);
	}
	// Freeze the append-only color table before any GPU batch snapshots it.
	// One cold frame then shares a single LUT revision instead of pinning a
	// larger copy after each newly encountered building. Warm frames reuse both.
}

void DrawScene(const Surface &out)
{
	const std::vector<TownSceneModel> &scene = GetTownScene();
	if (UpdateTownVisualSceneEvents()) {
		for (const auto &object : GetTownVisualSceneAudit().objects) {
			if (object.eventBinding == "none") continue;
			// A named native quest/access change invalidates only event objects;
			// geometry, transform, source pixels and all other caches stay intact.
			SceneArtworkCache.erase(object.sceneIndex);
			SceneMaterialCache.erase(object.sceneIndex);
			WellWaterMaterialTextureCache.erase(object.sceneIndex);
		}
	}
	PrepareArchitectureBoundsCache(scene);
	ArchitectureCullingState.modelsConsidered = 0;
	ArchitectureCullingState.modelsSubmitted = 0;
	ArchitectureCullingState.modelsCulled = 0;
	ArchitectureCullingState.trianglesVisited = 0;
	for (size_t index = 0; index < scene.size(); ++index) {
		const TownSceneModel &model = scene[index];
		if (model.kind == TownSceneKind::Well) {
			const bool poisoned = TownVisualSceneWaterPoisoned(index);
			const auto previous = SceneWaterEventState.find(index);
			if (previous != SceneWaterEventState.end() && previous->second != poisoned) {
				// Native CleanTownFountain changes the identified well cells. Retain
				// authored geometry/transforms; recompose only this event's source
				// artwork/material texture so Water surfaces show the native change.
				SceneArtworkCache.erase(index);
				SceneMaterialCache.erase(index);
				WellWaterMaterialTextureCache.erase(index);
			}
			SceneWaterEventState[index] = poisoned;
		}
		++ArchitectureCullingState.modelsConsidered;
		// One frozen local mask owns exterior, interior and attached fire together.
		// Native SOL/tiles, static shadow casters and remote players are untouched.
		if (CameraVisibilityState.active && CameraVisibilityState.valid
		    && CameraVisibilityState.sceneRevision == GetTownSceneRevision()
		    && index < CameraHiddenArchitecture.size() && CameraHiddenArchitecture[index] != 0)
			continue;
		if (!IsArchitectureVisible(index, model)) {
			++ArchitectureCullingState.modelsCulled;
			continue;
		}
		++ArchitectureCullingState.modelsSubmitted;
		if (model.externalModel && model.importedTexture) {
			const Texture &texture = ImportedSceneTexture(index, *model.importedTexture, model.kind == TownSceneKind::Cathedral);
			for (const TownSceneTriangle &triangle : TownSceneExteriorTriangles(model)) {
				++ArchitectureCullingState.trianglesVisited;
				std::array<Vertex, 3> vertices;
				for (size_t i = 0; i < vertices.size(); ++i) {
					const TownSceneVertex &vertex = triangle.vertices[i];
					vertices[i] = { { vertex.x, vertex.height, vertex.z }, vertex.u, vertex.v };
				}
				PickRecord pick = PickAt(triangle.pickTile);
				pick.architecture = static_cast<int16_t>(index);
				DrawTriangle(out, vertices, texture, pick, 0, false, &triangle.normal);
			}
			if (model.cabinInterior)
				DrawCabinInterior(out, index, *model.cabinInterior);
			continue;
		}
		const NativeSceneArt &art = NativeArtwork(index, model);
		if (art.texture.width == 0)
			continue;
		const SceneMaterials &materials = OpaqueSceneMaterials(index, model, art);
		for (const TownSceneTriangle &triangle : model.triangles) {
			++ArchitectureCullingState.trianglesVisited;
			const SceneDetailMaterial &detail = materials.details[static_cast<size_t>(triangle.surfaceDetail)];
			const bool detailed = detail.texture.width != 0;
			std::array<Vertex, 3> vertices;
			for (size_t i = 0; i < vertices.size(); ++i) {
				const TownSceneVertex &vertex = triangle.vertices[i];
				vertices[i] = { { vertex.x, vertex.height, vertex.z },
					detailed ? (vertex.u - detail.originU) / detail.sizeU : vertex.u / 2,
					detailed ? (vertex.v - detail.originV) / detail.sizeV : vertex.v / 2 };
			}
			PickRecord pick = PickAt(triangle.pickTile);
			pick.architecture = static_cast<int16_t>(index);
			const float illumination = (triangle.normal.x + triangle.normal.height + triangle.normal.z) / 1.7320508F;
			const int shade = triangle.surfaceRole == TownSceneSurfaceRole::Underside ? 3 : (illumination < -0.1F ? 2 : (illumination < 0.4F ? 1 : 0));
			DrawTriangle(out, vertices, SceneSurfaceTexture(index, model, triangle,
			    detailed ? detail.texture : materials.base[static_cast<size_t>(triangle.material)]), pick, shade);
		}
		for (size_t triangleIndex = 0; triangleIndex < model.triangles.size(); ++triangleIndex) {
			++ArchitectureCullingState.trianglesVisited;
			const TownSceneTriangle &triangle = model.triangles[triangleIndex];
			const auto sourceModels = GetTownVisualSceneSourceModels();
			const TownSceneTriangle &sourceTriangle = index < sourceModels.size() ? sourceModels[index].triangles[triangleIndex] : triangle;
			const NativeSceneFace &face = art.projectedFaces[triangleIndex];
			if (!triangle.nativeProjection || face.texture.width == 0)
				continue;
			const Vec3 toEye = ViewCamera.eye - Vec3 { triangle.vertices[0].x, triangle.vertices[0].height, triangle.vertices[0].z };
			if (triangle.normal.x * toEye.x + triangle.normal.height * toEye.y + triangle.normal.z * toEye.z <= 0)
				continue;
			std::array<Vertex, 3> vertices;
			for (size_t i = 0; i < vertices.size(); ++i) {
				const TownSceneVertex &vertex = triangle.vertices[i];
				const TownSceneVertex &sourceVertex = sourceTriangle.vertices[i];
				const float nativeX = 32 * (sourceVertex.x - sourceVertex.z - art.referenceTile.x + art.referenceTile.y);
				const float nativeY = 16 * (sourceVertex.x + sourceVertex.z - art.referenceTile.x - art.referenceTile.y) - 32 * sourceVertex.height;
				vertices[i] = { { vertex.x, vertex.height, vertex.z },
					(nativeX - face.pixelOrigin.x) / face.texture.width, (nativeY - face.pixelOrigin.y) / face.texture.height };
			}
			// Original artwork constrains the authored front. Every other surface
			// has its own continuous opaque material and no projected-photo holes.
			PickRecord pick = PickAt(triangle.pickTile);
			pick.architecture = static_cast<int16_t>(index);
			DrawTriangle(out, vertices, SceneSurfaceTexture(index, model, triangle, face.texture), pick, 0, true);
		}
	}
	ArchitectureCullingState.cachedBounds = static_cast<size_t>(std::count_if(ArchitectureBoundsCache.begin(), ArchitectureBoundsCache.end(),
	    [](const CachedArchitectureBounds &cached) { return cached.ready; }));
}

bool IsNativeTownBridgePiece(uint16_t piece)
{
	return leveltype == DTYPE_TOWN && piece >= 309 && piece <= 312;
}

const Texture &NativeBridgeTexture(uint16_t piece)
{
	const auto found = NativeBridgeTextureCache.find(piece);
	if (found != NativeBridgeTextureCache.end()) return found->second;
	const auto &art = GetTile(piece);
	Texture texture = art.original;
	// Walkable MIN bottom rows use a separate native floor bitmap. Composite
	// underneath the foliage, exactly as the classic floor/cell pass does.
	for (int y = 0; y < art.ground.height; ++y) {
		for (int x = 0; x < art.ground.width; ++x) {
			const size_t dst = size_t(y + texture.height - art.ground.height) * texture.width + x;
			const size_t src = size_t(y) * art.ground.width + x;
			if (texture.opacity[dst] == 0) {
				texture.pixels[dst] = art.ground.pixels[src];
				texture.opacity[dst] = art.ground.opacity[src];
			}
		}
	}
	return NativeBridgeTextureCache.emplace(piece, std::move(texture)).first->second;
}

void DrawNativeBridgeTile(const Surface &out, Point tile, uint16_t piece)
{
	const auto &texture = NativeBridgeTexture(piece);
	const auto vertex = [&](float x, float y) {
		return Vec3 { tile.x + (2 * (y - 255) + x - 32) / 64,
			0.002F, tile.y + (2 * (y - 255) - x + 32) / 64 };
	};
	// Bridge art is horizontal decking, not a raised generic scenery relief.
	// Inverse projection preserves the complete native coverage and pick anchor.
	DrawQuad(out, { vertex(0, 0), vertex(float(texture.width), 0),
		vertex(float(texture.width), float(texture.height)), vertex(0, float(texture.height)) },
		texture, PickAt(tile), dLight[tile.x][tile.y], true);
}

void DrawGround(const Surface &out, Point tile, const Texture &ground)
{
	if (IsNativeTownBridgePiece(dPiece[tile.x][tile.y])) {
		DrawNativeBridgeTile(out, tile, dPiece[tile.x][tile.y]);
		return;
	}
	const float x = static_cast<float>(tile.x);
	const float z = static_cast<float>(tile.y);
	const PickRecord pick = PickAt(tile);
	const int lighting = dLight[tile.x][tile.y];
	// Inverse-project the complete 64x32 native bitmap rectangle, including its
	// pixel stairs. The encoded opacity clips the diamond; shrinking the geometry
	// to the ideal mathematical diamond dropped edge pixels and opened a grid.
	DrawQuad(out, { Vec3 { x - 1.46875F, 0, z - 0.46875F }, Vec3 { x - 0.46875F, 0, z - 1.46875F },
		Vec3 { x + 0.53125F, 0, z - 0.46875F }, Vec3 { x - 0.46875F, 0, z + 0.53125F } }, ground, pick, lighting, true);
}

void DrawResidentVolume(Vec3 position, const VolumeArtwork &art, PickRecord pick, int lighting)
{
	auto &cached = art.residentPlacements[{ position.x, position.y, position.z }];
	if (!cached.ready) {
		const auto world = [&](const TownVolumeVertex &v) {
			constexpr float s = 0.7071067811865475F;
			return Vec3 { position.x + (v.x + v.z) * s,
				position.y + v.height + (art.physicalFrame ? 0 : v.z * s - 1 / PixelsPerWorldUnit),
				position.z + (-v.x + v.z) * s };
		};
		cached.vertices.reserve(art.mesh.triangles.size() * 3);
		cached.indices.reserve(art.mesh.triangles.size() * 3);
		for (const auto &triangle : art.mesh.triangles) {
			Vec3 normal = Cross(world(triangle.vertices[1]) - world(triangle.vertices[0]),
				world(triangle.vertices[2]) - world(triangle.vertices[0]));
			const float length = std::sqrt(Dot(normal, normal));
			if (!(length > 0))
				continue; // DrawVolume rejects these as a zero-facing plane too.
			normal = normal * (1 / length);
			const bool front = triangle.material == TownVolumeMaterial::SpriteFront;
			const uint8_t view = front ? (art.physicalFrame && !art.directionalTextures[triangle.textureView].pixels.empty()
				? triangle.textureView : 8) : (cached.ranges.empty() ? 8 : cached.ranges.back().textureView);
			if (cached.ranges.empty() || cached.ranges.back().textureView != view)
				cached.ranges.push_back({ static_cast<uint32_t>(cached.indices.size()), 0, view });
			for (const auto &v : triangle.vertices) {
				const Vec3 point = world(v);
				cached.indices.push_back(static_cast<uint32_t>(cached.vertices.size()));
				cached.vertices.push_back({ { point.x, point.y, point.z }, { v.u, v.v }, { normal.x, normal.y, normal.z },
					static_cast<uint32_t>(triangle.paletteIndex) | (front ? 256U : 0U) });
			}
			cached.ranges.back().indexCount += 3;
		}
		cached.ready = true;
	}
	if (cached.indices.empty() || !PrepareResidentMesh(cached))
		return;
	TownGpuMeshInstance instance;
	instance.identity = cached.identity();
	instance.pickId = GpuPickId(pick);
	instance.volumeMaterial = true;
	instance.volumeLighting = std::clamp(lighting, 0, 3);
	ConfigureResidentMeshLighting(instance);
	TownGpuMaterial material;
	material.lighting = TownGpuLighting::Shadow;
	material.receivesShadow = DirectionalShadowsEnabledForDiagnostics;
	material.shade = instance.volumeLighting;
	for (const auto &range : cached.ranges) {
		const Texture &texture = range.textureView < 8 ? art.directionalTextures[range.textureView] : art.texture;
		material.repeat = texture.repeat;
		material.transparentZero = texture.opacity.empty();
		instance.firstIndex = range.firstIndex;
		instance.indexCount = range.indexCount;
		if (!TownGpuSubmitMeshInstance(instance, PrepareGpuTexture(texture, nullptr), material)) {
			CaptureGpuFailed = true;
			return;
		}
		RendererState.gpuSubmittedTriangles += range.indexCount / 3;
	}
}

void DrawVolume(const Surface &out, Vec3 position, Point tile, const VolumeArtwork &art, PickKind kind, int entity, int lighting, bool residentAllowed = true, float localScale = 1)
{
	constexpr float InverseSqrt2 = 0.7071067811865475F;
	const auto world = [&](const TownVolumeVertex &vertex) {
		// The frame stays in world space. Its depth follows the native viewing ray,
		// preserving the authored front's silhouette without following the camera.
		return Vec3 { position.x + (vertex.x + vertex.z) * InverseSqrt2 * localScale,
			position.y + (vertex.height + (art.physicalFrame ? 0 : vertex.z * InverseSqrt2 - 1 / PixelsPerWorldUnit)) * localScale,
			position.z + (-vertex.x + vertex.z) * InverseSqrt2 * localScale };
	};
	// A ground decal can enter the frame while its body is outside it.
	if (!art.shadow.pixels.empty()) {
		const auto onGround = [&](float u, float v) {
			const float px = (u - 0.5F) * art.shadow.width;
			const float py = v * art.shadow.height - art.shadow.height + 1;
			return Vec3 { position.x + (2 * py + px) / 64 * localScale, 0.001F,
				position.z + (2 * py - px) / 64 * localScale };
		};
		PickRecord shadowPick = PickAt(tile);
		shadowPick.preservePicking = true;
		DrawQuad(out, { onGround(0, 0), onGround(1, 0), onGround(1, 1), onGround(0, 1) }, art.shadow, shadowPick, 0, true);
	}
	// True extrema also cover physical bodies translated away from local Z=0.
	if (!art.boundsReady) {
		const float infinity = std::numeric_limits<float>::infinity();
		art.minimum = { infinity, infinity, infinity };
		art.maximum = { -infinity, -infinity, -infinity };
		for (const auto &triangle : art.mesh.triangles) {
			for (const auto &v : triangle.vertices) {
				art.minimum.x = std::min(art.minimum.x, v.x);
				art.minimum.y = std::min(art.minimum.y, v.height);
				art.minimum.z = std::min(art.minimum.z, v.z);
				art.maximum.x = std::max(art.maximum.x, v.x);
				art.maximum.y = std::max(art.maximum.y, v.height);
				art.maximum.z = std::max(art.maximum.z, v.z);
			}
		}
		art.boundsReady = true;
	}
	if (art.mesh.triangles.empty())
		return;
	if (!ViewCamera.projection.perspective) {
		float minX = std::numeric_limits<float>::infinity(), minY = minX;
		float maxX = -minX, maxY = -minX;
		for (const float x : { art.minimum.x - 0.01F, art.maximum.x + 0.01F }) {
			for (const float height : { art.minimum.y - 0.01F, art.maximum.y + 0.01F }) {
				for (const float z : { art.minimum.z - 0.01F, art.maximum.z + 0.01F }) {
					const ProjectedVertex projected = ProjectRaster({ ToCamera(world({ x, height, z, 0, 0 })), 0, 0 });
					minX = std::min(minX, projected.x);
					maxX = std::max(maxX, projected.x);
					minY = std::min(minY, projected.y);
					maxY = std::max(maxY, projected.y);
				}
			}
		}
		if (maxX < 0 || maxY < 0 || minX >= out.w() || minY >= out.h())
			return;
	}
	if (ViewCamera.projection.perspective && ArchitectureCullingState.requested) {
		const float infinity = std::numeric_limits<float>::infinity();
		TownArchitectureBounds bounds { { infinity, infinity, infinity }, { -infinity, -infinity, -infinity }, true };
		for (const float x : { art.minimum.x, art.maximum.x }) {
			for (const float height : { art.minimum.y, art.maximum.y }) {
				for (const float z : { art.minimum.z, art.maximum.z }) {
					const Vec3 point = world({ x, height, z, 0, 0 });
					bounds.minimum.x = std::min(bounds.minimum.x, point.x);
					bounds.minimum.height = std::min(bounds.minimum.height, point.y);
					bounds.minimum.z = std::min(bounds.minimum.z, point.z);
					bounds.maximum.x = std::max(bounds.maximum.x, point.x);
					bounds.maximum.height = std::max(bounds.maximum.height, point.y);
					bounds.maximum.z = std::max(bounds.maximum.z, point.z);
				}
			}
		}
		if (!IsTownArchitectureBoundsVisible(ViewCamera.projection, bounds))
			return;
	}
	// Keep near/eye crossings for authoritative per-triangle clipping. Ground
	// decals above have independent extents and must survive a culled body.
	const PickRecord pick = PickAt(tile, kind, entity);
	// Orthographic edge clipping still uses the validated projected path.
	// Resident buffers are enabled only for perspective props and vegetation.
	if (CaptureGpu && ResidentMeshesEnabledForDiagnostics && ViewCamera.projection.perspective && residentAllowed && localScale == 1 && kind == PickKind::Ground) {
		DrawResidentVolume(position, art, pick, lighting);
		return;
	}
	for (const TownVolumeTriangle &triangle : art.mesh.triangles) {
		std::array<Vertex, 3> vertices;
		for (size_t i = 0; i < vertices.size(); ++i)
			vertices[i] = { world(triangle.vertices[i]), triangle.vertices[i].u, triangle.vertices[i].v };
		const Vec3 normal = Cross(vertices[1].position - vertices[0].position, vertices[2].position - vertices[0].position);
		if (Dot(normal, ViewCamera.eye - vertices[0].position) <= 0)
			continue;
		const float length = std::sqrt(Dot(normal, normal));
		const float light = length > 0 ? (normal.x + normal.y + normal.z) / (length * 1.7320508F) : 0;
		const int shade = std::clamp(lighting + (light < -0.1F ? 2 : (light < 0.4F ? 1 : 0)),
		    0, DrawingCathedral ? static_cast<int>(NumLightingLevels) - 1 : 3);
		if (CaptureGpu && !DrawingCathedral && triangle.material == TownVolumeMaterial::SpriteFront) {
			const Texture &texture = art.physicalFrame && !art.directionalTextures[triangle.textureView].pixels.empty()
			    ? art.directionalTextures[triangle.textureView] : art.texture;
			// The CPU draws a solid backing and a cutout at identical depth. The
			// GPU samples both choices in one pass, preserving their exact order
			// without a texture switch and draw call for every small front face.
			DrawTriangle(out, vertices, texture, pick, lighting, true, nullptr, nullptr,
				{ triangle.paletteIndex, shade });
			continue;
		}
		// Dungeon palette materials use the native 16-level LUT. The Town
		// combined front/back fallback is not admitted by that GPU material;
		// retain the existing ordered backing + covered front submissions.
		DrawTriangle(out, vertices, PaletteTextures[triangle.paletteIndex], pick, shade);
		if (triangle.material == TownVolumeMaterial::SpriteFront) {
			const Texture &texture = art.physicalFrame && !art.directionalTextures[triangle.textureView].pixels.empty()
				? art.directionalTextures[triangle.textureView] : art.texture;
			DrawTriangle(out, vertices, texture, pick, lighting, true);
		}
	}
}

void DrawScenery(const Surface &out, Point tile, const TileArt &art, const Texture &fallback)
{
	if (IsNativeTownBridgePiece(dPiece[tile.x][tile.y])) {
		if (art.solid) DrawNativeBridgeTile(out, tile, dPiece[tile.x][tile.y]);
		return;
	}
	const float x = static_cast<float>(tile.x);
	const float z = static_cast<float>(tile.y);
	const int lighting = dLight[tile.x][tile.y];
	if (art.solid)
		// The bottom MIN row of a solid object may contain its painted base
		// (notably the well rim). Its new body supplies that art in space.
		DrawGround(out, tile, TownSceneReplacesTile(tile) || PreparedTownPropReplacesTile(tile, true) ? fallback : SceneGround(dPiece[tile.x][tile.y]));
	// A tree's MIN base and its delayed CLX crown are one object. The solid tree
	// volume below replaces both; retaining this cell proxy duplicates its trunk.
	if (TownVegetationReplacesTile(tile) || TownSceneReplacesTile(tile) || PreparedTownPropReplacesTile(tile) || art.height < 0.25F)
		return;
	// Unclassified scenery retains its true painted contour in a closed relief,
	// rather than filling its transparent exterior into a tall one-cell box.
	const uint16_t piece = dPiece[tile.x][tile.y];
	const auto found = SceneryVolumeCache.find(piece);
	const VolumeArtwork &volume = found != SceneryVolumeCache.end() ? found->second
		: SceneryVolumeCache.emplace(piece, VolumeArtwork { art.facade,
			BuildTownPropVolume({ art.facade.width, art.facade.height, art.facade.pixels, art.facade.opacity }) }).first->second;
	DrawVolume(out, { x, 0, z }, tile, volume, PickKind::Ground, -1, lighting, false);
}

Texture DecodeSprite(ClxSprite sprite, bool highDefinition)
{
	if (highDefinition && sprite.hdFrameId() != 0) {
		auto source = LoadHdSpritePixels(sprite);
		if (!source) throw std::runtime_error(source.error());
		Texture result; result.width = source->width; result.height = source->height;
		result.pixels = std::move(source->pixels); result.opacity = std::move(source->opacity);
		return result;
	}
	const int width = sprite.width();
	const int height = sprite.height();
	if (!SpriteSurface || width > SpriteSurfaceWidth || height > SpriteSurfaceHeight) {
		SpriteSurfaceWidth = std::max(width, SpriteSurfaceWidth);
		SpriteSurfaceHeight = std::max(height, SpriteSurfaceHeight);
		SpriteSurface = std::make_unique<OwnedSurface>(SpriteSurfaceWidth, SpriteSurfaceHeight);
		SpriteCoverageSurface = std::make_unique<OwnedSurface>(SpriteSurfaceWidth, SpriteSurfaceHeight);
	}
	const Surface surface = SpriteSurface->subregion(0, 0, width, height);
	const Surface coverage = SpriteCoverageSurface->subregion(0, 0, width, height);
	ClearSurface(surface);
	for (int y = 0; y < height; ++y)
		std::memset(coverage.at(0, y), 255, width);
	ClxDraw(surface, { 0, height - 1 }, sprite);
	ClxDraw(coverage, { 0, height - 1 }, sprite);
	Texture result = CopySurface(surface);
	result.opacity.resize(result.pixels.size());
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			result.opacity[static_cast<size_t>(y) * width + x] = surface[{ x, y }] == coverage[{ x, y }];
	return result;
}

bool CathedralNativeOperationBindingSupported(const cathedral::NativeTextureBinding &binding) noexcept
{
	return static_cast<unsigned>(binding.operationPass) <= static_cast<unsigned>(cathedral::NativeTextureOperationPass::PaletteBlend)
	    && (binding.operationPass == cathedral::NativeTextureOperationPass::Unpartitioned
	        || (binding.kind == cathedral::NativeTextureKind::Masonry
	            && binding.layout == cathedral::NativeTextureLayout::OriginalFullColumn));
}

CathedralNativeTextureKey NativeTextureKey(const cathedral::NativeTextureBinding &binding)
{
	if (!CathedralNativeOperationBindingSupported(binding))
		throw std::invalid_argument("Cathedral native operation pass requires an original full column");
	return { binding.kind, binding.piece,
		binding.kind == cathedral::NativeTextureKind::Masonry ? binding.axis : cathedral::NativeTextureAxis::Horizontal, binding.column,
		binding.kind == cathedral::NativeTextureKind::DoorWood ? binding.nativeSlot : -1, binding.layout };
}

void ResetCathedralNativeTextureCache()
{
	CathedralOwnFloorPresent = CathedralOwnWallPresent = false;
	CathedralCeilingBindingReady = false;
	CathedralCeilingBinding = {};
	CathedralCommonPaletteSignature = 0;
	CathedralNativeLightBody.reset();
	CathedralNativeLitTexturesReady = false;
	CathedralNativeLitTextureCache.clear();
	CathedralNativeLitTextureBytes = 0;
	CathedralNativeLitFrameLightSignature = 0;
	CathedralNativeLitGameRevision = CathedralNativeLitGeometryRevision = CathedralNativeLitPresentationSignature = 0;
	CathedralNativeTextureCache.clear();
	CathedralNativeTextureState = {};
	CathedralNativeTextureResource = nullptr;
	CathedralNativeTextureMicroLen = 0;
}

const Object &NativeMaterialDoor(const cathedral::NativeTextureBinding &binding)
{
	if (binding.nativeSlot < 0 || binding.nativeSlot >= MAXOBJECTS)
		throw std::invalid_argument("Cathedral wood material has no native door slot");
	const Object &door = Objects[binding.nativeSlot];
	if ((door._otype != OBJ_L1LDOOR && door._otype != OBJ_L1RDOOR)
	    || door.position != Point { binding.x, binding.z } || !door._oAnimData || door._oAnimData->numSprites() == 0)
		throw std::invalid_argument("Cathedral wood material native door resource mismatch");
	return door;
}

uint64_t NativeTextureSignature(const cathedral::NativeTextureBinding &binding)
{
	if (binding.piece >= MAXTILES || binding.column > 1)
		throw std::invalid_argument("Cathedral native material source out of range");
	if (!CathedralNativeOperationBindingSupported(binding))
		throw std::invalid_argument("Cathedral native operation pass requires an original full column");
	uint64_t hash = 14695981039346656037ULL;
	const auto mix = [&](uint64_t value) {
		for (unsigned n = 0; n < 8; ++n) { hash ^= (value >> (n * 8)) & 255; hash *= 1099511628211ULL; }
	};
	if (binding.layout == cathedral::NativeTextureLayout::OriginalUpperColumn
	    || binding.layout == cathedral::NativeTextureLayout::OriginalFullColumn) {
		if (binding.kind != cathedral::NativeTextureKind::Masonry || MicroTileLen != 10
		    || binding.x < 0 || binding.z < 0 || binding.x >= cathedral::GridSize || binding.z >= cathedral::GridSize
		    || (binding.axis != cathedral::NativeTextureAxis::AlongX && binding.axis != cathedral::NativeTextureAxis::AlongZ)
		    || binding.column != (binding.axis == cathedral::NativeTextureAxis::AlongX ? 1 : 0))
			throw std::invalid_argument("Cathedral upper-column material binding invalid");
		mix(binding.layout == cathedral::NativeTextureLayout::OriginalFullColumn
		        ? cathedral::NativeFullColumnPackingRevision : cathedral::NativeUpperColumnPackingRevision);
		mix(static_cast<uint8_t>(binding.layout));
		mix(MicroTileLen);
	} else if (binding.layout == cathedral::NativeTextureLayout::RectifiedFloor) {
		if (binding.kind != cathedral::NativeTextureKind::FloorDiamond)
			throw std::invalid_argument("Rectified floor requires native floor material");
		mix(cathedral::rectified_material::Revision);
		mix(static_cast<uint8_t>(binding.layout));
	} else if (binding.layout == cathedral::NativeTextureLayout::RectifiedWall) {
		if (binding.kind != cathedral::NativeTextureKind::Masonry || binding.approximate || MicroTileLen != 10)
			throw std::invalid_argument("Rectified wall requires primary Cathedral masonry");
		mix(cathedral::rectified_material::Revision); mix(static_cast<uint8_t>(binding.layout));
	} else if (binding.layout == cathedral::NativeTextureLayout::SolidMasonry) {
		if ((binding.kind != cathedral::NativeTextureKind::Masonry && binding.kind != cathedral::NativeTextureKind::Ceiling)
		    || MicroTileLen != 10)
			throw std::invalid_argument("Solid masonry requires native Cathedral grain");
		mix(cathedral::rectified_material::Revision); mix(static_cast<uint8_t>(binding.layout));
	} else if (binding.layout != cathedral::NativeTextureLayout::RepeatedBand) {
		throw std::invalid_argument("Cathedral native material layout unknown");
	}
	// Presence and qualified LOD identity belong to the native resource epoch.
	// A pack change requires ResetHdSpritePack plus the existing resource reset.
	if (binding.kind == cathedral::NativeTextureKind::FloorDiamond
	    && binding.layout == cathedral::NativeTextureLayout::RectifiedFloor) {
		mix(2026101201ULL); mix(CathedralOwnFloorPresent ? CathedralOwnFloorPixelHash : 0);
	} else if (binding.kind == cathedral::NativeTextureKind::Masonry
	    && binding.layout == cathedral::NativeTextureLayout::SolidMasonry) {
		mix(2026101201ULL); mix(CathedralOwnWallPresent ? CathedralOwnWallPixelHash : 0);
	}
	// Constant-size MIN metadata only. Immutable CEL bytes are never rescanned
	// on a warm hit; resource loads must renew the native visual epoch.
	for (uint16_t piece : { binding.piece, uint16_t { 0 }, uint16_t { 4 }, uint16_t { 3 } }) {
		mix(static_cast<uint8_t>(SOLData[piece]));
		for (const LevelCelBlock block : DPieceMicros[piece].mt) mix(block.data);
	}
	if (binding.kind == cathedral::NativeTextureKind::DoorWood) {
		const ClxSprite closed = (*NativeMaterialDoor(binding)._oAnimData)[0];
		mix(reinterpret_cast<uintptr_t>(closed.pixelData())); mix(closed.pixelDataSize());
		mix(closed.width()); mix(closed.height());
	}
	return hash;
}

template <typename Render>
Texture DecodeCathedralCoverage(int width, int height, Render render)
{
	OwnedSurface paint(width, height), coverage(width, height);
	ClearSurface(paint);
	for (int y = 0; y < height; ++y) std::memset(coverage.at(0, y), 255, width);
	static const auto identityTables = [] {
		std::array<std::array<uint8_t, LightTableSize>, NumLightingLevels> tables {};
		for (auto &row : tables)
			for (size_t code = 0; code < row.size(); ++code) row[code] = static_cast<uint8_t>(code);
		return tables;
	}();
	const std::vector<uint8_t> zeroLights(static_cast<size_t>(paint.pitch()) * height, 0);
	const Lightmap paintLight(paint.begin(), zeroLights, paint.pitch(), identityTables,
		identityTables[0].data(), identityTables.back().data());
	const Lightmap coverageLight(coverage.begin(), zeroLights, coverage.pitch(), identityTables,
		identityTables[0].data(), identityTables.back().data());
	render(paint, paintLight, identityTables[0].data());
	render(coverage, coverageLight, identityTables[0].data());
	Texture texture = CopySurface(paint);
	texture.opacity.resize(texture.pixels.size());
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			texture.opacity[static_cast<size_t>(y) * width + x] = paint[{ x, y }] == coverage[{ x, y }];
	return texture; // Covered raw indices 0 AND 255 are independent of opacity.
}

/** Cold raw block scratch without Texture construction or GPU identity churn.
 * Stats/debug builds may have extra native renderer side effects; they are not
 * admitted to this bounded raw-oracle path. No native light/TRN/mask is baked.
 */
cathedral::wall_composition::RawBlockPatch DecodeCathedralRawCellBlock(
    const cathedral::wall_composition::RasterOperation &operation)
{
#if defined(DUN_RENDER_STATS) || defined(DEBUG_STR) || defined(DEBUG_RENDER_OFFSET_X) || defined(DEBUG_RENDER_OFFSET_Y)
	throw std::invalid_argument("Cathedral original upper path excludes native stats/debug overlays");
#else
	if ((operation.role != cathedral::wall_composition::RasterRole::DrawCellUpper
	        && operation.role != cathedral::wall_composition::RasterRole::DrawCellBase)
	    || operation.requiresPreparedFloorSubframe || operation.requiresPreparedFoliageSuffix)
		throw std::invalid_argument("Cathedral raw cell callback received floor/foliage");
	const auto checkedScratch = [] {
#ifdef USE_SDL3
		SDLSurfaceUniquePtr storage { SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_INDEX8) };
#else
		SDLSurfaceUniquePtr storage { SDL_CreateRGBSurfaceWithFormat(0, 32, 32, 8, SDL_PIXELFORMAT_INDEX8) };
#endif
		// OwnedSurface(width,height) routes null through NonNull/ErrDlg. This
		// new cold path checks SDL allocation first so the existing Cathedral
		// preparation transaction can discard and retry through bad_alloc.
		if (!storage) throw std::bad_alloc {};
		return OwnedSurface(std::move(storage));
	};
	OwnedSurface paint = checkedScratch(), coverage = checkedScratch();
	ClearSurface(paint);
	for (int y = 0; y < 32; ++y) std::memset(coverage.at(0, y), 255, 32);
	static const auto identityTables = [] {
		std::array<std::array<uint8_t, LightTableSize>, NumLightingLevels> tables {};
		for (auto &row : tables)
			for (size_t code = 0; code < row.size(); ++code) row[code] = static_cast<uint8_t>(code);
		return tables;
	}();
	const std::vector<uint8_t> zeroLights(static_cast<size_t>(paint.pitch()) * 32, 0);
	const Lightmap paintLight(paint.begin(), zeroLights, paint.pitch(), identityTables,
		identityTables[0].data(), identityTables.back().data());
	const Lightmap coverageLight(coverage.begin(), zeroLights, coverage.pitch(), identityTables,
		identityTables[0].data(), identityTables.back().data());
	const LevelCelBlock block { operation.source.word };
	RenderTile(paint, paintLight, { 0, 31 }, pDungeonCels.get(), block, MaskType::Solid, identityTables[0].data());
	RenderTile(coverage, coverageLight, { 0, 31 }, pDungeonCels.get(), block, MaskType::Solid, identityTables[0].data());
	cathedral::wall_composition::RawBlockPatch patch;
	patch.indices.resize(32 * 32); patch.coverage.resize(32 * 32);
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x) {
			const size_t index = static_cast<size_t>(y) * 32 + x;
			patch.indices[index] = paint[{ x, y }];
			patch.coverage[index] = paint[{ x, y }] == coverage[{ x, y }];
		}
	return patch;
#endif
}

cathedral::wall_composition::SourceCellMetadata CaptureCathedralNativeColumnSource(
    const cathedral::NativeTextureBinding &binding)
{
	if (binding.kind != cathedral::NativeTextureKind::Masonry || binding.layout != cathedral::NativeTextureLayout::OriginalFullColumn
	    || binding.x < 0 || binding.z < 0 || binding.x >= MAXDUNX || binding.z >= MAXDUNY || binding.piece >= MAXTILES
	    || binding.column > 1 || MicroTileLen != 10 || dPiece[binding.x][binding.z] != binding.piece)
		throw std::invalid_argument("Cathedral native operator source differs from its loaded cell");
	cathedral::wall_composition::SourceCellMetadata source;
	source.sourceX = binding.x; source.sourceZ = binding.z; source.piece = binding.piece;
	source.activeMicroTileLength = MicroTileLen;
	source.sol = static_cast<uint8_t>(SOLData[binding.piece]);
	source.transparencyGroup = static_cast<uint8_t>(dTransVal[binding.x][binding.z]);
	source.groupActive = TransList[source.transparencyGroup];
	for (size_t micro = 0; micro < source.words.size(); ++micro)
		source.words[micro] = DPieceMicros[binding.piece].mt[micro].data;
	return source;
}

bool LoadCathedralOwnMaterial(Texture &texture, const char *resource, int width, int height, uint64_t expectedHash)
{
	auto replacement = LoadHdSpriteReplacement(resource);
	if (!replacement) throw std::runtime_error(replacement.error());
	if (!*replacement) return false; // Existing native/per-piece fallback remains available.
	if ((**replacement).isSheet() || (**replacement).list().numSprites() != 1)
		throw std::runtime_error("Own Cathedral material requires one CLX frame");
	const auto sprite = (**replacement).list()[0];
	if (sprite.width() != width || sprite.height() != height)
		throw std::runtime_error("Own Cathedral material dimensions mismatch");
	texture = DecodeSprite(sprite);
	if (texture.pixels.size() != static_cast<size_t>(width) * height
	    || texture.opacity.size() != texture.pixels.size()
	    || std::any_of(texture.opacity.begin(), texture.opacity.end(), [](uint8_t alpha) { return alpha != 1; }))
		throw std::runtime_error("Own Cathedral material must be fully opaque");
	uint64_t hash = 14695981039346656037ULL;
	for (uint8_t code : texture.pixels) { hash ^= code; hash *= 1099511628211ULL; }
	if (hash != expectedHash) throw std::runtime_error("Own Cathedral material r1 pixel identity mismatch");
	return true;
}

CathedralNativeTextureEntry DecodeCathedralNativeMaterial(const cathedral::NativeTextureBinding &binding)
{
	using cathedral::NativeTextureKind;
	CathedralNativeTextureEntry entry;
	entry.signature = NativeTextureSignature(binding);
	entry.sourcePiece = binding.piece;
	entry.sourceColumn = binding.column;
	if (binding.kind == NativeTextureKind::FloorDiamond) {
		// Adapter Space includes some missile-blocking pieces and doors. Only
		// native IsFloor permits DrawFloorTile's reencoded triangle subframes.
		if (!HasNoneOf(SOLData[entry.sourcePiece], TileProperties::Solid | TileProperties::BlockMissile)) {
			entry.sourcePiece = 3; // TIL mega 13's plain Cathedral floor family.
			entry.approximate = true;
		}
		if (!HasNoneOf(SOLData[entry.sourcePiece], TileProperties::Solid | TileProperties::BlockMissile))
			throw std::invalid_argument("Cathedral floor family donor is not native IsFloor");
		const MICROS &micros = DPieceMicros[entry.sourcePiece];
		if (!micros.mt[0].hasValue() || !micros.mt[1].hasValue()
		    || static_cast<unsigned>(micros.mt[0].type()) > 5 || static_cast<unsigned>(micros.mt[1].type()) > 5)
			throw std::invalid_argument("Cathedral floor native subframes absent or invalid");
		entry.sourceBlock = micros.mt[0].data; entry.secondBlock = micros.mt[1].data;
		if (binding.layout == cathedral::NativeTextureLayout::RectifiedFloor) {
			CathedralOwnFloorPresent = LoadCathedralOwnMaterial(entry.texture,
			    "materials/cathedral/floor/stone-r1.clx", 128, 128, CathedralOwnFloorPixelHash);
			if (CathedralOwnFloorPresent) {
				entry.texture.repeat = false;
				entry.authoredMaterial = 1; entry.authoredPixelHash = CathedralOwnFloorPixelHash;
				entry.signature = NativeTextureSignature(binding);
				return entry;
			}
			const std::string resource = "materials/cathedral/floor/" + std::to_string(entry.sourcePiece) + ".clx";
			auto replacement = LoadHdSpriteReplacement(resource);
			if (!replacement) throw std::runtime_error(replacement.error());
			if (*replacement) {
				if ((**replacement).isSheet() || (**replacement).list().numSprites() != 1)
					throw std::runtime_error("HD floor must have a single material frame");
				entry.texture = DecodeSprite((**replacement).list()[0]);
				if (entry.texture.width != 128 || entry.texture.height != 128)
					throw std::runtime_error("HD floor material dimensions");
				entry.texture.repeat = false;
				return entry; // Selected before decoding the original floor pixels.
			}
		}
		entry.texture = DecodeCathedralCoverage(64, 32, [&](const Surface &out, const Lightmap &light, const uint8_t *table) {
			for (int column = 0; column < 2; ++column)
				RenderTileFrame(out, light, { column * 32, 31 }, column == 0 ? TileType::LeftTriangle : TileType::RightTriangle,
					GetDunFrame(pDungeonCels.get(), micros.mt[column].frame()), DunFrameTriangleHeight, MaskType::Solid, table);
		}); // Exact native DrawFloorTile semantics after SetDungeonMicros.
		if (binding.layout == cathedral::NativeTextureLayout::RectifiedFloor) {
			const auto square = cathedral::rectified_material::RectifyFloor(entry.texture.pixels, entry.texture.opacity);
			entry.texture.width = entry.texture.height = 64;
			entry.texture.pixels.assign(square.pixels.begin(), square.pixels.end());
			entry.texture.opacity.assign(square.coverage.begin(), square.coverage.end());
			entry.texture.repeat = false;
		}
	} else if (binding.kind == NativeTextureKind::Masonry
	    && binding.layout == cathedral::NativeTextureLayout::OriginalFullColumn) {
		cathedral::wall_composition::SourceCellMetadata source;
		source.sourceX = binding.x; source.sourceZ = binding.z; source.piece = binding.piece;
		source.activeMicroTileLength = MicroTileLen;
		source.sol = static_cast<uint8_t>(SOLData[binding.piece]);
		source.transparencyGroup = static_cast<uint8_t>(dTransVal[binding.x][binding.z]);
		source.groupActive = TransList[source.transparencyGroup];
		for (size_t micro = 0; micro < source.words.size(); ++micro)
			source.words[micro] = DPieceMicros[binding.piece].mt[micro].data;
		const auto plan = cathedral::wall_composition::BuildCellPlan(source);
		if (plan.nativeIsFloor)
			throw std::invalid_argument("Cathedral raw full column requires its own non-floor source");
		// Raw coverage and palette indices never flatten the native draw operator.
		// Known-fence derivative passes partition opacity after native light bake.
		auto atlas = cathedral::native_cell_art::ComposeFullColumnRawAtlas(source, binding.column, DecodeCathedralRawCellBlock);
		if (atlas.noPresentWords || atlas.layerOperations.empty())
			throw std::invalid_argument("Cathedral original full column absent; donor forbidden");
		entry.texture.width = atlas.descriptor.width; entry.texture.height = atlas.descriptor.height;
		entry.texture.pixels = std::move(atlas.indices); entry.texture.opacity = std::move(atlas.coverage);
		entry.texture.repeat = false;
		entry.sourceMicro = static_cast<uint8_t>(atlas.layerOperations.front().source.micro);
		entry.sourceBlock = source.words[entry.sourceMicro]; entry.upperWords = source.words;
		entry.approximate = true; // Native 3D depth/ground fit still requires external evidence.
	} else if (binding.kind == NativeTextureKind::Masonry
	    && binding.layout == cathedral::NativeTextureLayout::OriginalUpperColumn) {
		cathedral::wall_composition::SourceCellMetadata source;
		source.sourceX = binding.x; source.sourceZ = binding.z; source.piece = binding.piece;
		source.activeMicroTileLength = MicroTileLen;
		source.sol = static_cast<uint8_t>(SOLData[binding.piece]);
		source.transparencyGroup = static_cast<uint8_t>(dTransVal[binding.x][binding.z]);
		source.groupActive = TransList[source.transparencyGroup];
		for (size_t micro = 0; micro < source.words.size(); ++micro)
			source.words[micro] = DPieceMicros[binding.piece].mt[micro].data;
		const auto plan = cathedral::wall_composition::BuildCellPlan(source);
		if (plan.nativeIsFloor || plan.nativeWallTransparency)
			throw std::invalid_argument("Cathedral upper-column path requires opaque non-floor native source");
		auto atlas = cathedral::wall_composition::ComposeUpperRawAtlas(source, binding.column, DecodeCathedralRawCellBlock);
		if (atlas.noPresentUpperWords || atlas.layerOperations.empty())
			throw std::invalid_argument("Cathedral original upper column absent; donor forbidden");
		entry.texture.width = atlas.descriptor.width; entry.texture.height = atlas.descriptor.height;
		entry.texture.pixels = std::move(atlas.indices); entry.texture.opacity = std::move(atlas.coverage);
		entry.texture.repeat = false;
		entry.sourceMicro = static_cast<uint8_t>(atlas.layerOperations.front().source.micro);
		entry.sourceBlock = source.words[entry.sourceMicro]; entry.upperWords = source.words;
		entry.approximate = true; // Existing physical plane is a technical placement, not native3D depth proof.
	} else if (binding.kind == NativeTextureKind::Masonry || binding.kind == NativeTextureKind::Ceiling) {
		if (binding.kind == NativeTextureKind::Masonry && binding.layout == cathedral::NativeTextureLayout::SolidMasonry) {
			CathedralOwnWallPresent = LoadCathedralOwnMaterial(entry.texture,
			    "materials/cathedral/wall/stone-r1.clx", 64, 128, CathedralOwnWallPixelHash);
			if (CathedralOwnWallPresent) {
				entry.texture.repeat = true;
				entry.horizontalFlip = binding.axis == cathedral::NativeTextureAxis::AlongZ;
				entry.approximate = true;
				entry.authoredMaterial = 2; entry.authoredPixelHash = CathedralOwnWallPixelHash;
				entry.signature = NativeTextureSignature(binding);
				return entry; // Native light Bake follows; do not apply stone courses/bevel again.
			}
		}
		const auto bandForPiece = [&](uint16_t piece) -> bool {
			for (int micro = 2 + binding.column; micro < std::min<int>(MicroTileLen, 16); micro += 2) {
				const LevelCelBlock block = DPieceMicros[piece].mt[micro];
				// A conservative masonry catalog: opaque upper squares only.
				// Arches/fences/foliage do not become a silent plaster material.
				if (!block.hasValue() || block.type() != TileType::Square) continue;
				const Texture raw = DecodeCathedralCoverage(32, 32,
					[&](const Surface &out, const Lightmap &light, const uint8_t *table) {
						RenderTile(out, light, { 0, 31 }, pDungeonCels.get(), block, MaskType::Solid, table);
					});
				const unsigned shearSelector = binding.axis == cathedral::NativeTextureAxis::AlongX ? 0 : 1;
				const auto band = cathedral::RectifyNativeMasonryBand(raw.pixels, raw.opacity, shearSelector);
				if (std::any_of(band.opacity.begin(), band.opacity.end(), [](uint8_t value) { return value == 0; })) continue;
				entry.texture.width = 32; entry.texture.height = 16;
				entry.texture.pixels.resize(band.pixels.size()); entry.texture.opacity.resize(band.opacity.size());
				entry.horizontalFlip = binding.axis == cathedral::NativeTextureAxis::AlongZ;
				// Native screen X grows in -Z. World U grows in +Z, so the
				// AlongZ material reverses texels/coverage, not the UV/world basis.
				for (size_t y = 0; y < 16; ++y)
					for (size_t x = 0; x < 32; ++x) {
						const size_t source = y * 32 + (entry.horizontalFlip ? 31 - x : x);
						entry.texture.pixels[y * 32 + x] = band.pixels[source];
						entry.texture.opacity[y * 32 + x] = band.opacity[source];
					}
				entry.texture.repeat = true;
				entry.sourcePiece = piece; entry.sourceMicro = static_cast<uint8_t>(micro); entry.sourceBlock = block.data;
				return true;
			}
			return false;
		};
		if (!bandForPiece(binding.piece) && !bandForPiece(binding.axis == cathedral::NativeTextureAxis::AlongX ? 4 : 0))
			throw std::invalid_argument("Cathedral native masonry band unavailable");
		entry.approximate = true; // Repeated planar material, not complete facade reconstruction.
		if (binding.layout == cathedral::NativeTextureLayout::RectifiedWall || binding.layout == cathedral::NativeTextureLayout::SolidMasonry) {
			const MICROS &micros = DPieceMicros[entry.sourcePiece];
			const Texture atlas = DecodeCathedralCoverage(64,256,[&](const Surface &out,const Lightmap &light,const uint8_t *table) {
				for (unsigned micro=0;micro<MicroTileLen;++micro) {
					const LevelCelBlock block=micros.mt[micro];
					if (!block.hasValue()) continue;
					RenderTile(out,light,{static_cast<int>(micro%2)*32,255-static_cast<int>(micro/2)*32},
						pDungeonCels.get(),block,MaskType::Solid,table);
				}
			});
			const auto face = cathedral::rectified_material::RectifySolidWall(atlas.pixels,atlas.opacity,
				entry.texture.pixels,binding.column,entry.horizontalFlip);
			entry.texture.height=128;entry.texture.repeat=false;
			entry.texture.pixels.assign(face.pixels.begin(),face.pixels.end());
			entry.texture.opacity.assign(face.coverage.begin(),face.coverage.end());
			entry.approximate=true; // Reconstruction and any covered masonry infill are visual estimates.
			if (binding.layout == cathedral::NativeTextureLayout::SolidMasonry) {
				const auto courses = cathedral::rectified_material::BuildStoneCourses(entry.texture.pixels);
				entry.texture.width=64;entry.texture.height=128;entry.texture.repeat=true;
				entry.texture.pixels.assign(courses.pixels.begin(),courses.pixels.end());
				entry.texture.opacity.assign(entry.texture.pixels.size(),1);
				for(size_t i=0;i<entry.texture.pixels.size();++i)
					if(courses.bevel[i]!=0) entry.texture.pixels[i]=LightTables[courses.bevel[i]][entry.texture.pixels[i]];
				if(binding.kind == NativeTextureKind::Ceiling) {
					const auto ceiling=cathedral::rectified_material::CeilingFromStoneCourses(entry.texture.pixels);
					entry.texture.height=64;
					entry.texture.pixels.assign(ceiling.pixels.begin(),ceiling.pixels.end());
					entry.texture.opacity.assign(ceiling.coverage.begin(),ceiling.coverage.end());
				}
			}
		}
	} else if (binding.kind == NativeTextureKind::DoorWood) {
		const ClxSprite closed = (*NativeMaterialDoor(binding)._oAnimData)[0];
		if (closed.width() == 0 || closed.height() == 0 || closed.width() > 512 || closed.height() > 512)
			throw std::invalid_argument("Cathedral closed door material exceeds native scratch bounds");
		// Preparation is outside the Draw allocation wrapper. Local scratch is
		// transactional: neither global SpriteSurface nor its dimensions change
		// if the second surface allocation fails, and retry starts cleanly.
		const Texture raw = DecodeCathedralCoverage(closed.width(), closed.height(),
			[&](const Surface &out, const Lightmap &, const uint8_t *) {
				ClxDraw(out, { 0, closed.height() - 1 }, closed);
			}); // Raw CLX; no light/TRN is baked into the material.
		auto patch = cathedral::FindNativeOpaquePatch(raw.opacity, raw.width, raw.height, 16);
		if (!patch) patch = cathedral::FindNativeOpaquePatch(raw.opacity, raw.width, raw.height, 8);
		if (!patch) throw std::invalid_argument("Cathedral closed door has no covered native wood patch");
		entry.cropX = (*patch)[0]; entry.cropY = (*patch)[1];
		entry.texture.width = (*patch)[2]; entry.texture.height = (*patch)[3];
		entry.texture.pixels.resize(static_cast<size_t>(entry.texture.width) * entry.texture.height);
		entry.texture.opacity.assign(entry.texture.pixels.size(), 1);
		for (int y = 0; y < entry.texture.height; ++y)
			std::memcpy(entry.texture.pixels.data() + static_cast<size_t>(y) * entry.texture.width,
				raw.pixels.data() + static_cast<size_t>(y + entry.cropY) * raw.width + entry.cropX, entry.texture.width);
		entry.texture.repeat = true; entry.approximate = true;
	} else {
		throw std::invalid_argument("Cathedral unsupported native material role");
	}
	if (entry.texture.opacity.empty() || (binding.layout != cathedral::NativeTextureLayout::OriginalUpperColumn
	    && binding.layout != cathedral::NativeTextureLayout::OriginalFullColumn
	    && std::none_of(entry.texture.opacity.begin(), entry.texture.opacity.end(),
	        [](uint8_t coverage) { return coverage != 0; })))
		throw std::invalid_argument("Cathedral material has no covered native texels");
	return entry;
}

CathedralNativeLitTextureKey NativeLitTextureKey(const cathedral::NativeTextureBinding &binding)
{
	return { NativeTextureKey(binding), binding.x, binding.z, binding.operationPass };
}

void MixCathedralNativeLightHash(uint64_t &hash, uint64_t value)
{
	for (unsigned n = 0; n < 8; ++n) { hash ^= (value >> (n * 8)) & 255; hash *= 1099511628211ULL; }
}

uint64_t CathedralNativeLightPaletteSignature()
{
	uint64_t hash = 14695981039346656037ULL;
	for (const auto &table : LightTables)
		for (const uint8_t value : table) { hash ^= value; hash *= 1099511628211ULL; }
	return hash; // One table scan per preparation/completeness call, not per texel.
}

uint64_t CathedralNativeFrameLightSignature(uint64_t paletteSignature, bool perPixel)
{
	uint64_t hash = paletteSignature;
	MixCathedralNativeLightHash(hash, perPixel);
	for (int x = 0; x < MAXDUNX; ++x)
		for (int z = 0; z < MAXDUNY; ++z) {
			hash ^= dLight[x][z]; hash *= 1099511628211ULL;
			hash ^= static_cast<uint8_t>(dTransVal[x][z]); hash *= 1099511628211ULL;
		}
	for (const bool active : TransList) { hash ^= static_cast<uint8_t>(active); hash *= 1099511628211ULL; }
	MixCathedralNativeLightHash(hash, cathedral::native_operators::OperationRevision);
	return hash; // Native light AND operator-context freshness; no native write.
}

uint64_t CathedralNativeSourceLightSignature(const cathedral::NativeTextureBinding &binding,
    uint64_t paletteSignature, bool perPixel)
{
	uint64_t hash = paletteSignature;
	MixCathedralNativeLightHash(hash, perPixel);
	MixCathedralNativeLightHash(hash, MicroTileLen);
	MixCathedralNativeLightHash(hash, static_cast<uint64_t>(binding.x));
	MixCathedralNativeLightHash(hash, static_cast<uint64_t>(binding.z));
	MixCathedralNativeLightHash(hash, static_cast<uint8_t>(binding.operationPass));
	if (binding.operationPass != cathedral::NativeTextureOperationPass::Unpartitioned) {
		const auto source = CaptureCathedralNativeColumnSource(binding);
		MixCathedralNativeLightHash(hash, source.sol);
		MixCathedralNativeLightHash(hash, source.transparencyGroup);
		MixCathedralNativeLightHash(hash, source.groupActive);
		MixCathedralNativeLightHash(hash, cathedral::native_operators::OperationRevision);
	}
	const auto mixLight = [&](Point position) {
		const uint8_t value = dLight[std::clamp(position.x, 0, MAXDUNX - 1)][std::clamp(position.y, 0, MAXDUNY - 1)];
		MixCathedralNativeLightHash(hash, static_cast<uint64_t>(position.x));
		MixCathedralNativeLightHash(hash, static_cast<uint64_t>(position.y));
		MixCathedralNativeLightHash(hash, value);
	};
	mixLight({ binding.x, binding.z });
	if (!perPixel) return hash;
	// Fingerprint every native light sample read by buildLocal(rows1, columns1),
	// including its extra margin. This is a conservative dependency traversal,
	// not a second interpolation or a pixel oracle. The native interpolator runs
	// only in buildLocal below; samples outside the map match GetLightLevel.
	Point position { binding.x, binding.z };
	position += Displacement(Direction::NorthWest) * 2;
	const int rows = 1 + MicroTileLen + 2 + 3;
	int columns = 2;
	for (int row = 0; row < rows; ++row) {
		mixLight(position + Displacement { 0, 1 });
		for (int column = 0; column < columns; ++column, position += Direction::East) {
			mixLight(position); mixLight(position + Displacement { 1, 0 }); mixLight(position + Displacement { 1, 1 });
		}
		position += Displacement(Direction::West) * columns;
		if ((row & 1) != 0) { ++position.x; --columns; }
		else { ++position.y; ++columns; }
	}
	return hash;
}

[[noreturn]] void RefuseCathedralNativeLightCacheBudget()
{
	RecordCathedralDiagnostic(CathedralFallbackReason::NativeMaterialBudgetRefused,
	    CathedralFallbackStage::NativeMaterials, true, EffectiveGpuRequested());
	CathedralPreparationRefused = true;
	throw std::length_error("Cathedral raw+lit native material host-cache budget exceeded");
}

Texture BakeCathedralNativeMasonryLight(const cathedral::NativeTextureBinding &binding,
    const CathedralNativeTextureEntry &raw, bool perPixel, CathedralNativeLightScratch &scratch)
{
	if (binding.kind != cathedral::NativeTextureKind::Masonry || binding.column > 1
	    || binding.x < 0 || binding.z < 0 || binding.x >= MAXDUNX || binding.z >= MAXDUNY
	    || raw.texture.width != (binding.layout == cathedral::NativeTextureLayout::SolidMasonry ? 64 : 32) || raw.texture.height <= 0
	    || raw.texture.pixels.size() != static_cast<size_t>(raw.texture.width) * raw.texture.height
	    || raw.texture.opacity.size() != raw.texture.pixels.size())
		throw std::invalid_argument("Cathedral native lit masonry source invalid");
	const bool repeated = binding.layout == cathedral::NativeTextureLayout::RepeatedBand;
	if ((repeated && (raw.texture.height != 16 || raw.sourceMicro >= MicroTileLen))
	    || (!repeated && binding.layout != cathedral::NativeTextureLayout::OriginalFullColumn
	        && binding.layout != cathedral::NativeTextureLayout::OriginalUpperColumn
	        && binding.layout != cathedral::NativeTextureLayout::RectifiedWall
	        && binding.layout != cathedral::NativeTextureLayout::SolidMasonry))
		throw std::invalid_argument("Cathedral native lit masonry layout invalid");
	// Same native source anchor used by DrawCell. A scratch surface is only an
	// address coordinate system for Lightmap; no CEL decode, native build/global
	// LightmapBuffer, map/light mutation or framebuffer render occurs here.
	const Point anchor { 0, CathedralNativeLightCanvasHeight - 1 };
	const auto local = Lightmap::buildLocal(perPixel, { binding.x, binding.z }, anchor,
	    CathedralNativeLightCanvasWidth, CathedralNativeLightCanvasHeight, 1, 1,
	    scratch.canvas.data(), CathedralNativeLightCanvasWidth, LightTables,
	    FullyLitLightTable, FullyDarkLightTable, dLight, MicroTileLen, scratch.light);
	const auto bleed = Lightmap::bleedUp(perPixel, local, anchor, scratch.bleed);
	Texture lit;
	lit.width = raw.texture.width; lit.height = raw.texture.height; lit.repeat = raw.texture.repeat;
	lit.pixels = raw.texture.pixels;
	lit.lightingBasePixels = raw.texture.pixels;
	lit.opacity = raw.texture.opacity; // Coverage is independent of every palette index, including 0/255.
	const uint8_t uniform = std::min<uint8_t>(dLight[binding.x][binding.z], static_cast<uint8_t>(NumLightingLevels - 1));
	for (int y = 0; y < lit.height; ++y) for (int x = 0; x < lit.width; ++x) {
		const size_t index = static_cast<size_t>(y) * lit.width + x;
		if (lit.opacity[index] == 0) continue;
		const uint8_t rawColor = raw.texture.pixels[index];
		if (!perPixel) {
			// Native dispatch: FullyLit bypasses LUT (not LightTables[0]); FullyDark
			// paints opaque 0; intermediate levels use the actual native LUT.
			lit.pixels[index] = uniform == 0 ? rawColor : uniform == LightsMax ? 0 : LightTables[uniform][rawColor];
			continue;
		}
		int sourceX = x, sourceY;
		if (repeated) {
			// Invert the existing band rectify+flip; keep the selected donor/art
			// code but sample lighting at its original block anchor in this cell.
			sourceX = raw.horizontalFlip ? 31 - x : x;
			const int shear = binding.axis == cathedral::NativeTextureAxis::AlongX ? sourceX / 2 : (31 - sourceX) / 2;
			sourceY = anchor.y - 32 * (raw.sourceMicro / 2) - 31 + y + shear;
		} else if (binding.layout == cathedral::NativeTextureLayout::RectifiedWall || binding.layout == cathedral::NativeTextureLayout::SolidMasonry) {
			const unsigned nativeX=static_cast<unsigned>(x)*32/static_cast<unsigned>(lit.width);
			const auto sample = cathedral::rectified_material::WallSource(nativeX,y,binding.column,raw.horizontalFlip);
			sourceX=static_cast<int>(sample[0])-static_cast<int>(binding.column)*32;
			sourceY=anchor.y+static_cast<int>(sample[1])-255;
		} else {
			const int minY = binding.layout == cathedral::NativeTextureLayout::OriginalFullColumn
			    ? 1 - lit.height : -31 - lit.height;
			sourceY = anchor.y + minY + y;
		}
		sourceX += static_cast<int>(binding.column) * 32;
		if (sourceX < 0 || sourceX >= CathedralNativeLightCanvasWidth || sourceY < 0 || sourceY >= CathedralNativeLightCanvasHeight)
			throw std::invalid_argument("Cathedral native lit source outside local light canvas");
		const uint8_t level = *bleed.getLightingAt(scratch.canvas.data()
		    + static_cast<size_t>(sourceY) * CathedralNativeLightCanvasWidth + sourceX);
		if (level >= NumLightingLevels) throw std::invalid_argument("Cathedral native per-pixel light level out of range");
		lit.pixels[index] = bleed.adjustColor(rawColor, level);
	}
	if (binding.operationPass != cathedral::NativeTextureOperationPass::Unpartitioned) {
		const auto source = CaptureCathedralNativeColumnSource(binding);
		const auto operations = cathedral::native_operators::BuildFullColumnOperationMap(source, binding.column);
		// Only this derivative opacity is filtered. Raw identity pixels/coverage,
		// native LUT/bleed, UVs and original source anchors remain unchanged.
		cathedral::native_operators::FilterFullColumnCoverage(operations, lit.opacity, binding.operationPass);
	}
	return lit;
}

bool CathedralNativeLightBodyAdmitted(int slot)
{
	if (slot<0 || slot>=MAXOBJECTS || currlevel!=1 || leveltype!=DTYPE_CATHEDRAL || setlevel || gbIsHellfire || gbIsSpawn) return false;
	const Object &object=Objects[slot];const Point tile=object.position;
	return object._otype==OBJ_L1LIGHT && tile.x>=16 && tile.y>=16 && tile.x<96 && tile.y<96
	    && dPiece[tile.x][tile.y]==269 && std::abs(dObject[tile.x][tile.y])==slot+1;
}

void PrepareCathedralLightGroundPanels(cathedral::PilotFrame &frame)
{
	// Native light stands occupy a blocked SOL cell but have a floor beneath
	// them. Supply presentation-only floor/ceiling quads, never a walkable cell
	// or collision/physics change. The known native269 + exact L1LIGHT owner is
	// required; no black-pixel heuristic, arbitrary emitter or corner blanket.
	for(int i=0;i<ActiveObjectCount;++i) {
		const int slot=ActiveObjects[i];
		if(!CathedralNativeLightBodyAdmitted(slot)) continue;
		const Point tile=Objects[slot].position;
		if (!IsTileVisible(tile))
			continue;
		if (tile.x < frame.regionX - cathedral::RegionSize || tile.x >= frame.regionX + 2 * cathedral::RegionSize
		    || tile.y < frame.regionZ - cathedral::RegionSize || tile.y >= frame.regionZ + 2 * cathedral::RegionSize)
			continue;
		bool alreadyFloor=false;
		for(const auto &s:frame.triangles)
			alreadyFloor=alreadyFloor || (s.module==cathedral::ModuleKind::Floor && s.pick.x==tile.x && s.pick.z==tile.y);
		if(alreadyFloor) continue;
		uint16_t donor=0;bool found=false;
		for(int radius=1;radius<=3 && !found;++radius)
		for(int x=tile.x-radius;x<=tile.x+radius && !found;++x)
		for(int z=tile.y-radius;z<=tile.y+radius && !found;++z) {
			if(x<16||z<16||x>=96||z>=96||dSpecial[x][z]!=0||dObject[x][z]!=0) continue;
			const uint16_t piece=dPiece[x][z];
			if(piece>=MAXTILES || !HasNoneOf(SOLData[piece],TileProperties::Solid|TileProperties::BlockMissile)) continue;
			donor=piece;found=true;
		}
		if(!found) throw std::invalid_argument("Native light stand has no nearby verified floor donor");
		const float x=static_cast<float>(tile.x),z=static_cast<float>(tile.y);
		const std::array<cathedral::Vertex,4> v={{{{x-.5F,0,z-.5F},0,0},{{x-.5F,0,z+.5F},0,1},{{x+.5F,0,z+.5F},1,1},{{x+.5F,0,z-.5F},1,0}}};
		for(const auto indices:{std::array<unsigned,3>{0,1,2},std::array<unsigned,3>{0,2,3}}) {
			cathedral::FrameTriangle panel{};
			panel.module=cathedral::ModuleKind::Floor;panel.pick={cathedral::PickKind::Ground,tile.x,tile.y,-1};
			panel.instanceId=0xd3d1000000000000ULL|static_cast<uint64_t>(slot);
			panel.light=dLight[tile.x][tile.y];panel.policy=cathedral::FrameSurfacePolicy::Opaque;
			panel.nativeTexture.kind=cathedral::NativeTextureKind::FloorDiamond;
			panel.nativeTexture.layout=cathedral::NativeTextureLayout::RectifiedFloor;
			panel.nativeTexture.piece=donor;panel.nativeTexture.x=tile.x;panel.nativeTexture.z=tile.y;panel.nativeTexture.approximate=true;
			for(size_t n=0;n<3;++n)panel.vertices[n]=panel.textureVertices[n]=v[indices[n]];
			frame.nativeArtPanels.push_back(panel);
		}
	}
}

void PrepareCathedralLightBody()
{
	if(CathedralNativeLightBody) return;
	bool needed=false;
	for(int i=0;i<ActiveObjectCount;++i)needed=needed||CathedralNativeLightBodyAdmitted(ActiveObjects[i]);
	if(!needed) return;
	TileArt source=DecodeTile(269); // Exact current native resource, not the Town tile cache.
	if(source.facade.height<=32) throw std::invalid_argument("Native light stand has no upper body artwork");
	// Native269's bottom MIN row includes the stand's base/pole, not just
	// ground. Keep the full native anchor; cropping that row lowers the bowl
	// by one world unit and separates it from its original flame sprite.
	auto body=std::make_unique<VolumeArtwork>();body->texture=std::move(source.facade);
	body->mesh = BuildCathedralLightStandVolume({ body->texture.width, body->texture.height, body->texture.pixels, body->texture.opacity });
	body->physicalFrame = true;
	if(body->mesh.triangles.empty() || body->mesh.triangles.size()>4096)
		throw std::invalid_argument("Native light stand exceeds bounded closed-body geometry");
	const size_t bytes=body->texture.pixels.size()+body->texture.opacity.size()+body->mesh.triangles.size()*sizeof(TownVolumeTriangle);
	const size_t cachedBytes = CathedralNativeTextureState.bytes + CathedralNativeLitTextureBytes;
	if (cachedBytes > CathedralNativeTextureMaxBytes || bytes > CathedralNativeTextureMaxBytes - cachedBytes)
		RefuseCathedralNativeLightCacheBudget();
	CathedralNativeTextureState.lightBodyTriangles=body->mesh.triangles.size();
	CathedralNativeTextureState.lightBodyBytes=bytes;
	CathedralNativeTextureState.bytes+=bytes;
	CathedralNativeLightBody=std::move(body);
}

void PrepareCathedralCeilingBinding()
{
	// Once per native visual epoch: stable full-level donor, independent of
	// camera, focus, exploration and region selection. Do not consume game RNG.
	std::map<uint16_t, size_t> frequency;
	std::map<uint16_t, Point> origins;
	for(int x=16;x<96;++x)for(int z=16;z<96;++z) {
		const uint16_t piece=dPiece[x][z];
		if(piece>=MAXTILES || dSpecial[x][z]!=0 || dObject[x][z]!=0
		    || !HasAllOf(SOLData[piece],TileProperties::Solid | TileProperties::BlockLight)) continue;
		bool bordersFloor=false;
		for(const Point neighbor : {Point{x-1,z},Point{x+1,z},Point{x,z-1},Point{x,z+1}})
			bordersFloor = bordersFloor || IsTileWalkable(neighbor);
		if(!bordersFloor) continue;
		++frequency[piece]; origins.try_emplace(piece,Point{x,z});
	}
	if(frequency.empty()) throw std::invalid_argument("Cathedral ceiling has no native opaque masonry donor");
	uint16_t selected=0;size_t most=0;
	for(const auto &[piece,count]:frequency) if(count>most){selected=piece;most=count;}
	const Point origin=origins.at(selected);
	CathedralCeilingBinding={};
	CathedralCeilingBinding.kind=cathedral::NativeTextureKind::Ceiling;
	CathedralCeilingBinding.layout=cathedral::NativeTextureLayout::SolidMasonry;
	CathedralCeilingBinding.axis=cathedral::NativeTextureAxis::AlongX;
	CathedralCeilingBinding.column=1;CathedralCeilingBinding.piece=selected;
	CathedralCeilingBinding.x=origin.x;CathedralCeilingBinding.z=origin.y;
	CathedralCeilingBinding.approximate=true;
	CathedralCeilingBindingReady=true;
}

void PrepareCathedralNativeTextures(const cathedral::PilotFrame &frame)
{
	CathedralNativeLitTexturesReady = false; // Every refusal leaves the presentation incomplete.
	if (frame.gameRevision == 0 || !pDungeonCels || MicroTileLen < 2)
		throw std::invalid_argument("Cathedral texture preparation requires a loaded native epoch");
	const uint64_t paletteSignature = CathedralNativeLightPaletteSignature();
	if (CathedralNativeTextureState.epoch != frame.gameRevision || CathedralNativeTextureResource != pDungeonCels.get()
	    || CathedralNativeTextureMicroLen != MicroTileLen || CathedralCommonPaletteSignature != paletteSignature) {
		ResetCathedralNativeTextureCache();
		CathedralNativeTextureState.epoch = frame.gameRevision;
		CathedralNativeTextureResource = pDungeonCels.get(); CathedralNativeTextureMicroLen = MicroTileLen;
	}
	CathedralCommonPaletteSignature=paletteSignature;
	if(!CathedralCeilingBindingReady) PrepareCathedralCeilingBinding();
	CathedralNativeTextureState.lightGroundPanels=0;
	for(const auto &panel:frame.nativeArtPanels)
		CathedralNativeTextureState.lightGroundPanels+=panel.module==cathedral::ModuleKind::Floor;
	// Capture the complete raw working set before erasing any node. Lit keys
	// borrow these identities, and the ceiling is needed independently of walls.
	std::set<CathedralNativeTextureKey> selected;
	CathedralDiagnosticMaterialBinding = CathedralCeilingBinding;
	CathedralDiagnosticMaterialBindingValid = true;
	selected.insert(NativeTextureKey(CathedralCeilingBinding));
	std::map<CathedralNativeLitTextureKey, cathedral::NativeTextureBinding> selectedLit;
	for (const auto *surfaces : { &frame.triangles, &frame.nativeArtPanels })
	for (const auto &surface : *surfaces) {
		if (surface.visualSuppressed || surface.nativeTexture.kind == cathedral::NativeTextureKind::Technical) continue;
		CathedralDiagnosticMaterialBinding = surface.nativeTexture;
		CathedralDiagnosticMaterialBindingValid = true;
		selected.insert(NativeTextureKey(surface.nativeTexture));
		if (surface.nativeTexture.kind != cathedral::NativeTextureKind::Masonry) continue;
		const auto key = NativeLitTextureKey(surface.nativeTexture);
		if (selectedLit.find(key) != selectedLit.end()) continue;
		CathedralDiagnosticMaterialBinding = surface.nativeTexture; CathedralDiagnosticMaterialBindingValid = true;
		if (selectedLit.size() >= CathedralNativeLitTextureMaxEntries) RefuseCathedralNativeLightCacheBudget();
		selectedLit.emplace(key, surface.nativeTexture);
	}
	// Remove only inactive sources before admitting new materials. Warm
	// selected nodes and GPU identities stay in place in both caches.
	for (auto it = CathedralNativeLitTextureCache.begin(); it != CathedralNativeLitTextureCache.end();) {
		if (selectedLit.find(it->first) != selectedLit.end()) { ++it; continue; }
		CathedralNativeLitTextureBytes -= it->second.texture.pixels.size() + it->second.texture.opacity.size() + it->second.texture.lightingBasePixels.size();
		it = CathedralNativeLitTextureCache.erase(it);
	}
	for (auto it = CathedralNativeTextureCache.begin(); it != CathedralNativeTextureCache.end();) {
		if (selected.find(it->first) != selected.end()) { ++it; continue; }
		CathedralNativeTextureState.bytes -= it->second.texture.pixels.size() + it->second.texture.opacity.size();
		it = CathedralNativeTextureCache.erase(it);
	}
	// Light-body bytes have their own owner inside the raw-byte total.
	// Preserve that owner and check its first admission after inactive eviction.
	PrepareCathedralLightBody();
	std::set<CathedralNativeTextureKey> prepared;
	const auto prepareRaw=[&](const cathedral::NativeTextureBinding &binding) {
		// POD identity is captured before signature validation, decode or budget.
		CathedralDiagnosticMaterialBinding = binding;
		CathedralDiagnosticMaterialBindingValid = true;
		const auto key = NativeTextureKey(binding);
		if (!prepared.insert(key).second) return;
		const auto found = CathedralNativeTextureCache.find(key);
		if (found != CathedralNativeTextureCache.end() && found->second.signature == NativeTextureSignature(binding)) {
			++CathedralNativeTextureState.hits;
			return;
		}
		++CathedralNativeTextureState.misses;
		auto entry = DecodeCathedralNativeMaterial(binding); // Cold preparation only, never DrawSurface.
		const size_t bytes = entry.texture.pixels.size() + entry.texture.opacity.size();
		const size_t oldBytes = found == CathedralNativeTextureCache.end() ? 0
		    : found->second.texture.pixels.size() + found->second.texture.opacity.size();
		if ((found == CathedralNativeTextureCache.end() && CathedralNativeTextureCache.size() >= CathedralNativeTextureMaxEntries)
		    || bytes > CathedralNativeTextureMaxBytes - (CathedralNativeTextureState.bytes - oldBytes + CathedralNativeLitTextureBytes)) {
			RecordCathedralDiagnostic(CathedralFallbackReason::NativeMaterialBudgetRefused,
				CathedralFallbackStage::NativeMaterials, true, EffectiveGpuRequested());
			CathedralPreparationRefused = true;
			throw std::length_error("Cathedral native material host-cache budget exceeded");
		}
		if (found == CathedralNativeTextureCache.end()) CathedralNativeTextureCache.emplace(key, std::move(entry));
		else found->second = std::move(entry);
		CathedralNativeTextureState.bytes = CathedralNativeTextureState.bytes - oldBytes + bytes;
		++CathedralNativeTextureState.decodes;
	};
	prepareRaw(CathedralCeilingBinding);
	for (const auto *surfaces : { &frame.triangles, &frame.nativeArtPanels })
	for (const auto &surface : *surfaces) {
		if (surface.visualSuppressed || surface.nativeTexture.kind == cathedral::NativeTextureKind::Technical) continue;
		prepareRaw(surface.nativeTexture);
	}
	CathedralNativeTextureState.entries = CathedralNativeTextureCache.size();
	const bool perPixel = *GetOptions().Graphics.perPixelLighting;
	const uint64_t frameLightSignature = CathedralNativeFrameLightSignature(paletteSignature, perPixel);
	CathedralNativeLightScratch scratch;
	for (const auto &[key, binding] : selectedLit) {
		CathedralDiagnosticMaterialBinding = binding; CathedralDiagnosticMaterialBindingValid = true;
		const auto &raw = CathedralNativeTextureCache.at(NativeTextureKey(binding));
		const uint64_t signature = CathedralNativeSourceLightSignature(binding, paletteSignature, perPixel);
		auto found = CathedralNativeLitTextureCache.find(key);
		if (found != CathedralNativeLitTextureCache.end() && found->second.rawSignature == raw.signature
		    && found->second.rawGpuIdentity == raw.texture.gpuIdentity.value && found->second.lightSignature == signature) continue;
		const size_t bytes = raw.texture.pixels.size()*2 + raw.texture.opacity.size();
		const size_t oldBytes = found == CathedralNativeLitTextureCache.end() ? 0
		    : found->second.texture.pixels.size() + found->second.texture.opacity.size() + found->second.texture.lightingBasePixels.size();
		if ((found == CathedralNativeLitTextureCache.end() && CathedralNativeLitTextureCache.size() >= CathedralNativeLitTextureMaxEntries)
		    || bytes > CathedralNativeTextureMaxBytes - (CathedralNativeTextureState.bytes + CathedralNativeLitTextureBytes - oldBytes))
			RefuseCathedralNativeLightCacheBudget();
		CathedralNativeLitTextureEntry entry;
		entry.texture = BakeCathedralNativeMasonryLight(binding, raw, perPixel, scratch);
		entry.rawSignature = raw.signature; entry.rawGpuIdentity = raw.texture.gpuIdentity.value; entry.lightSignature = signature;
		if (found == CathedralNativeLitTextureCache.end()) CathedralNativeLitTextureCache.emplace(key, std::move(entry));
		else found->second = std::move(entry);
		CathedralNativeLitTextureBytes = CathedralNativeLitTextureBytes - oldBytes + bytes;
	}
	CathedralNativeLitFrameLightSignature = frameLightSignature;
	CathedralNativeLitGameRevision = frame.gameRevision; CathedralNativeLitGeometryRevision = frame.geometryRevision;
	CathedralNativeLitPresentationSignature = frame.presentationSignature;
	CathedralNativeLitTexturesReady = true;
	CathedralDiagnosticMaterialBindingValid = false;
	CathedralNativeTextureState.floorMaterials = CathedralNativeTextureState.masonryMaterials = CathedralNativeTextureState.doorMaterials = 0;
	CathedralNativeTextureState.donorMaterials = 0;
	CathedralNativeTextureState.ceilingMaterials = 0;
	for (const auto &[key, entry] : CathedralNativeTextureCache) {
		if (std::get<0>(key) == cathedral::NativeTextureKind::FloorDiamond) ++CathedralNativeTextureState.floorMaterials;
		else if (std::get<0>(key) == cathedral::NativeTextureKind::Masonry) ++CathedralNativeTextureState.masonryMaterials;
		else if (std::get<0>(key) == cathedral::NativeTextureKind::Ceiling) ++CathedralNativeTextureState.ceilingMaterials;
		else ++CathedralNativeTextureState.doorMaterials;
		if (entry.sourcePiece != std::get<1>(key)) ++CathedralNativeTextureState.donorMaterials;
	}
}

uint64_t CathedralNativeRawValidationSerial = 0;

bool CathedralNativeTexturesCurrent(const cathedral::PilotFrame &frame)
{
	if (++CathedralNativeRawValidationSerial == 0) ++CathedralNativeRawValidationSerial;
	const uint64_t validationSerial = CathedralNativeRawValidationSerial;
	if (CathedralNativeTextureState.epoch != frame.gameRevision || CathedralNativeTextureResource != pDungeonCels.get()
	    || CathedralNativeTextureMicroLen != MicroTileLen || !CathedralNativeLitTexturesReady
	    || CathedralNativeLitGameRevision != frame.gameRevision || CathedralNativeLitGeometryRevision != frame.geometryRevision
	    || CathedralNativeLitPresentationSignature != frame.presentationSignature) return false;
	const bool perPixel = *GetOptions().Graphics.perPixelLighting;
	const uint64_t paletteSignature = CathedralNativeLightPaletteSignature();
	if (CathedralNativeLitFrameLightSignature != CathedralNativeFrameLightSignature(paletteSignature, perPixel)) return false;
	if (!CathedralCeilingBindingReady || CathedralCommonPaletteSignature != paletteSignature) return false;
	const auto ceiling = CathedralNativeTextureCache.find(NativeTextureKey(CathedralCeilingBinding));
	if (ceiling == CathedralNativeTextureCache.end() || ceiling->second.texture.width != 64 || ceiling->second.texture.height != 64
	    || ceiling->second.signature != NativeTextureSignature(CathedralCeilingBinding)) return false;
	for (const auto *surfaces : { &frame.triangles, &frame.nativeArtPanels })
	for (const auto &surface : *surfaces) {
		if (surface.visualSuppressed || surface.nativeTexture.kind == cathedral::NativeTextureKind::Technical) continue;
		const auto raw = CathedralNativeTextureCache.find(NativeTextureKey(surface.nativeTexture));
		if (raw == CathedralNativeTextureCache.end()) return false;
		// Validate each shared raw material once, even when hundreds of triangles use it.
		if (raw->second.validatedAt != validationSerial) {
			if (raw->second.signature != NativeTextureSignature(surface.nativeTexture)) return false;
			raw->second.validatedAt = validationSerial;
		}
		if (surface.nativeTexture.kind != cathedral::NativeTextureKind::Masonry) continue;
		const auto lit = CathedralNativeLitTextureCache.find(NativeLitTextureKey(surface.nativeTexture));
		if (lit == CathedralNativeLitTextureCache.end() || lit->second.rawSignature != raw->second.signature
		    || lit->second.rawGpuIdentity != raw->second.texture.gpuIdentity.value) return false;
		if (surface.nativeTexture.operationPass != cathedral::NativeTextureOperationPass::Unpartitioned
		    && lit->second.lightSignature != CathedralNativeSourceLightSignature(surface.nativeTexture, paletteSignature, perPixel)) return false;
	}
	return true;
}

Texture ActorGroundShadow(const Texture &texture)
{
	const auto body = TownActorBodyOpacity({ texture.width, texture.height, texture.pixels, texture.opacity });
	Texture shadow;
	shadow.width = texture.width;
	shadow.height = texture.height;
	shadow.pixels.resize(texture.pixels.size(), 0);
	shadow.opacity.resize(texture.pixels.size(), 0);
	bool hasShadow = false;
	for (size_t i = 0; i < texture.pixels.size(); ++i) {
		const bool removed = texture.opacity[i] != 0 && body[i] == 0 && texture.pixels[i] == 0;
		shadow.opacity[i] = removed;
		hasShadow |= removed;
	}
	return hasShadow ? shadow : Texture {};
}

template <typename Group>
VolumeArtwork ComposeNativeScenery(const Group &group, bool vegetation, bool foliage = false)
{
	// Assemble the native MIN base and all delayed CLX columns in one fixed
	// image plane before fitting the trunk and branches. A column is never a
	// separate tree or a one-cell solid prism.
	const int span = group.maxTile.x - group.minTile.x + group.maxTile.y - group.minTile.y + 4;
	const int width = 64 * span;
	const int height = 320 + 32 * span;
	// MIN tiles behind/in front of the trunk extend below its anchor. Keep a
	// backing margin for the native tile decoder, then crop at the world floor.
	const Point foot { width / 2, height - 1 - 16 * span };
	OwnedSurface paint(width, height);
	OwnedSurface coverage(width, height);
	ClearSurface(paint);
	for (int y = 0; y < height; ++y)
		std::memset(coverage.at(0, y), 255, width);
	const std::vector<uint8_t> lightBuffer(static_cast<size_t>(paint.pitch()) * height, 0);
	const Lightmap paintLight(paint.begin(), lightBuffer, paint.pitch(), LightTables, IdentityPalette.data(), FullyDarkLightTable);
	const Lightmap coverageLight(coverage.begin(), lightBuffer, coverage.pitch(), LightTables, IdentityPalette.data(), FullyDarkLightTable);
	std::vector<Point> tiles(group.sourceTiles);
	std::stable_sort(tiles.begin(), tiles.end(), [](Point a, Point b) {
		return a.x + a.y != b.x + b.y ? a.x + a.y < b.x + b.y : a.x < b.x;
	});
	const auto anchorAt = [&](Point tile) {
		const int dx = tile.x - group.referenceFootpoint.x;
		const int dy = tile.y - group.referenceFootpoint.y;
		return Point { foot.x - 32 + 32 * (dx - dy), foot.y + 16 * (dx + dy) };
	};
	for (Point tile : tiles) {
		const Point base = anchorAt(tile);
		const uint16_t piece = dPiece[tile.x][tile.y];
		const bool splitFloor = HasNoneOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile);
		for (int i = 0; i < std::min<int>(MicroTileLen, 16); ++i) {
			const LevelCelBlock block = DPieceMicros[piece].mt[i];
			if (!block.hasValue())
				continue;
			const Point anchor { base.x + (i & 1) * 32, base.y - (i / 2) * 32 };
			if (i < 2 && (vegetation || splitFloor)) {
				// Keep only the foliage half of a split floor. Grass and the river
				// remain in the world ground pass rather than entering the tree.
				// Walkable MIN frames were split by SetDungeonMicros, including
				// walkable border fragments of a solid rock. A
				// solid transparent-square frame still holds ordinary RLE data.
				if (splitFloor && block.type() == TileType::TransparentSquare) {
					RenderTileFoliage(paint, paintLight, anchor, pDungeonCels.get(), block, IdentityPalette.data());
					RenderTileFoliage(coverage, coverageLight, anchor, pDungeonCels.get(), block, IdentityPalette.data());
				}
			} else {
				RenderTile(paint, paintLight, anchor, pDungeonCels.get(), block, MaskType::Solid, IdentityPalette.data());
				RenderTile(coverage, coverageLight, anchor, pDungeonCels.get(), block, MaskType::Solid, IdentityPalette.data());
			}
		}
	}
	if (pSpecialCels) {
		for (Point tile : tiles) {
			const int frame = dSpecial[tile.x][tile.y] - 1;
			if (frame < 0 || static_cast<size_t>(frame) >= pSpecialCels->numSprites())
				continue;
			ClxDraw(paint, anchorAt(tile), (*pSpecialCels)[frame]);
			ClxDraw(coverage, anchorAt(tile), (*pSpecialCels)[frame]);
		}
	}
	int left = width, right = -1, top = height;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if (paint[{ x, y }] != coverage[{ x, y }])
				continue;
			left = std::min(left, x);
			right = std::max(right, x);
			top = std::min(top, y);
		}
	}
	VolumeArtwork result;
	if (right < left)
		return result;
	// Retain the native trunk anchor, rather than recentering the cropped crown.
	const int halfWidth = std::min(width / 2, std::max(foot.x - left, right + 1 - foot.x) + 2);
	const int startX = foot.x - halfWidth;
	if (top > foot.y)
		return result;
	result.texture = CopySurface(paint.subregion(startX, top, halfWidth * 2, foot.y + 1 - top));
	result.texture.opacity.resize(result.texture.pixels.size());
	for (int y = 0; y < result.texture.height; ++y)
		for (int x = 0; x < result.texture.width; ++x)
			result.texture.opacity[static_cast<size_t>(y) * result.texture.width + x] = paint[{ startX + x, top + y }] == coverage[{ startX + x, top + y }];
	const TownVolumeSprite source { result.texture.width, result.texture.height, result.texture.pixels, result.texture.opacity };
	const uint32_t seed = static_cast<uint32_t>(group.referenceFootpoint.x + group.referenceFootpoint.y * MAXDUNX + 1);
	if constexpr (requires { group.kind; group.nativeLayout; }) {
		result.mesh = BuildTownPhysicalPropVolume(source, group.kind, group.nativeLayout, seed);
		result.physicalFrame = true;
	} else {
		result.mesh = BuildTownTreeVolume(source, seed, foliage);
	}
	return result;
}

VolumeArtwork ComposeVegetation(const TownVegetationGroup &group)
{
	return ComposeNativeScenery(group, true, group.foliage);
}

bool TownPropArtworkValid(const VolumeArtwork &art)
{
	if (art.mesh.triangles.empty() || art.texture.pixels.empty()
	    || !(art.mesh.width > 0) || !(art.mesh.height > 0) || !(art.mesh.depth > 0)
	    || !std::isfinite(art.mesh.width) || !std::isfinite(art.mesh.height) || !std::isfinite(art.mesh.depth))
		return false;
	for (const auto &triangle : art.mesh.triangles)
		for (const auto &v : triangle.vertices)
			if (!std::isfinite(v.x) || !std::isfinite(v.height) || !std::isfinite(v.z)
			    || !std::isfinite(v.u) || !std::isfinite(v.v))
				return false;
	return true;
}

void PrepareTownPropArtwork()
{
	if (PropReplacementReady || !pDungeonCels)
		return;
	for (auto &column : PropVisualReplacementMask)
		column.fill(false);
	for (auto &column : PropGroundPaintReplacementMask)
		column.fill(false);
	const auto &groups = GetTownPropGroups();
	for (size_t i = 0; i < groups.size(); ++i) {
		const auto &group = groups[i];
		const auto found = PropArtworkCache.find(i);
		const auto &art = found != PropArtworkCache.end() ? found->second
		    : PropArtworkCache.emplace(i, ComposeNativeScenery(group, false)).first->second;
		// Native paint is suppressed only after its complete physical replacement
		// is admitted. Empty or refused preparation retains the native fallback.
		if (!TownPropArtworkValid(art))
			continue;
		for (Point tile : group.sourceTiles) {
			if (!InDungeonBounds(tile))
				continue;
			PropVisualReplacementMask[tile.x][tile.y] = true;
			PropGroundPaintReplacementMask[tile.x][tile.y] = TownPropReplacesGroundPaint(tile);
		}
	}
	PropReplacementReady = true;
}

bool PreparedTownPropReplacesTile(Point tile, bool groundPaint)
{
	if (leveltype != DTYPE_TOWN || !PropReplacementReady || !InDungeonBounds(tile))
		return false;
	return groundPaint ? PropGroundPaintReplacementMask[tile.x][tile.y] : PropVisualReplacementMask[tile.x][tile.y];
}

Texture FixedObjectTexture(const TownVisualObjectTexture &source)
{
	Texture result;
	result.width = static_cast<int>(source.width);
	result.height = static_cast<int>(source.height);
	result.pixels = source.pixels;
	result.opacity = source.opacity;
	result.repeat = source.repeat;
	return result;
}

void ApplyFixedObjectTransform(TownVolumeMesh &mesh, const TownVisualObject &object)
{
	const auto &t = object.transform;
	if (t.yaw == 0 && t.scale == 1)
		return; // Preserve the baseline floats exactly, including its native-ray bias.
	constexpr float I = 0.7071067811865475F;
	const float c = std::cos(t.yaw), s = std::sin(t.yaw);
	for (auto &triangle : mesh.triangles) {
		for (auto &v : triangle.vertices) {
			const float dx = (v.x + v.z) * I;
			const float dz = (-v.x + v.z) * I;
			const float dy = v.height + (object.physicalFrame ? 0 : v.z * I - 1 / PixelsPerWorldUnit);
			const float x = t.scale * (c * dx + s * dz);
			const float z = t.scale * (-s * dx + c * dz);
			v.x = (x - z) * I;
			v.z = (x + z) * I;
			v.height = dy * t.scale - (object.physicalFrame ? 0 : v.z * I - 1 / PixelsPerWorldUnit);
		}
	}
}

void PrepareFixedTownObjects()
{
	const auto &audit = GetTownVisualObjectAudit();
	if (FixedTownArchiveHash == audit.sha256 && FixedTownSceneRevision == GetTownSceneRevision())
		return;
	ClearFixedTownRendererCache();
	const auto &set = GetTownVisualObjects();
	std::vector<Texture> textures;
	textures.reserve(set.textures.size());
	for (const auto &texture : set.textures)
		textures.push_back(FixedObjectTexture(texture));
	FixedTownTextures = std::move(textures);
	FixedTownArchiveHash = audit.sha256;
	FixedTownSceneRevision = GetTownSceneRevision();
}

uint64_t FixedObjectNativeSignature(const TownVisualObject &object)
{
	bool state = false;
	switch (object.eventBinding) {
	case TownVisualObjectEvent::None: return 0;
	case TownVisualObjectEvent::WellWater:
		state = Quests[Q_PWATER]._qactive != QUEST_DONE && Quests[Q_PWATER]._qactive != QUEST_NOTAVAIL; break;
	case TownVisualObjectEvent::CatacombsAccess: state = MyPlayer != nullptr && IsWarpOpen(DTYPE_CATACOMBS); break;
	case TownVisualObjectEvent::CavesAccess: state = MyPlayer != nullptr && IsWarpOpen(DTYPE_CAVES); break;
	case TownVisualObjectEvent::HellAccess: state = MyPlayer != nullptr && IsWarpOpen(DTYPE_HELL); break;
	}
	// Native town creation may randomize closed Hell tiles on each return.
	// The persisted visual source is retained while its semantic state agrees.
	if (state == object.eventState) return 0;
	uint64_t hash = 14695981039346656037ULL;
	hash ^= state ? 1 : 2; hash *= 1099511628211ULL;
	for (const auto &cell : object.sourceCells) {
		const uint16_t piece = dPiece[cell.tile.x][cell.tile.y];
		const uint16_t special = static_cast<uint8_t>(dSpecial[cell.tile.x][cell.tile.y]);
		hash ^= piece; hash *= 1099511628211ULL;
		hash ^= special; hash *= 1099511628211ULL;
	}
	return hash != 0 ? hash : 1;
}

VolumeArtwork &FixedObjectVolume(size_t index, const TownVisualObject &object, uint64_t changedSignature)
{
	if (changedSignature == 0) {
		const auto cached = FixedTownVolumes.find(index);
		if (cached != FixedTownVolumes.end()) return cached->second;
		VolumeArtwork art;
		art.texture = FixedTownTextures[object.texture];
		art.mesh = object.mesh;
		art.physicalFrame = object.physicalFrame;
		ApplyFixedObjectTransform(art.mesh, object);
		return FixedTownVolumes.emplace(index, std::move(art)).first->second;
	}
	auto &delta = FixedTownDeltas[index];
	if (delta.signature == changedSignature) return delta.art;
	// Only this identified object's changed source cells are observed. No whole
	// town scan/group reconstruction or authored-file regeneration occurs here.
	VolumeArtwork art;
	if (object.kind == TownVisualObjectKind::Scenery) {
		const uint16_t piece = dPiece[object.anchor.x][object.anchor.y];
		if (piece < MAXTILES) {
			const TileArt &tile = GetTile(piece);
			if (tile.height >= 0.25F) {
				art.texture = tile.facade;
				art.mesh = BuildTownPropVolume({ art.texture.width, art.texture.height, art.texture.pixels, art.texture.opacity });
			}
		}
	} else {
		struct DeltaGroup { Point referenceFootpoint, minTile, maxTile; std::vector<Point> sourceTiles; };
		DeltaGroup group { object.anchor, { MAXDUNX, MAXDUNY }, { -1, -1 }, {} };
		for (const auto &cell : object.sourceCells) {
			group.sourceTiles.push_back(cell.tile);
			group.minTile.x = std::min(group.minTile.x, cell.tile.x); group.minTile.y = std::min(group.minTile.y, cell.tile.y);
			group.maxTile.x = std::max(group.maxTile.x, cell.tile.x); group.maxTile.y = std::max(group.maxTile.y, cell.tile.y);
		}
		const bool foliage = std::any_of(object.mesh.triangles.begin(), object.mesh.triangles.end(), [](const auto &triangle) { return triangle.material == TownVolumeMaterial::Foliage; });
		art = ComposeNativeScenery(group, object.kind == TownVisualObjectKind::Vegetation, foliage);
	}
	art.physicalFrame = object.physicalFrame;
	ApplyFixedObjectTransform(art.mesh, object);
	delta.art = std::move(art); delta.signature = changedSignature;
	++FixedTownObjectState.nativeDeltaRebuilds;
	return delta.art;
}

void DrawFixedTownObjects(const Surface &out, bool volumesAfterArchitecture)
{
	PrepareFixedTownObjects();
	if (!volumesAfterArchitecture) FixedTownObjectState = { true };
	const auto &set = GetTownVisualObjects();
	for (size_t index = 0; index < set.objects.size(); ++index) {
		const auto &object = set.objects[index];
		if ((object.phase == 2) != volumesAfterArchitecture || !object.visible || object.nativeSuppressed)
			continue;
		const uint64_t changed = FixedObjectNativeSignature(object);
		if (changed != 0) ++FixedTownObjectState.nativeDeltaObjects;
		const auto &t = object.transform;
		const uint16_t piece = dPiece[object.anchor.x][object.anchor.y];
		const bool nativeBridge = object.sourceCells.size() == 1
		    && object.sourceCells[0].piece == piece && IsNativeTownBridgePiece(piece)
		    && object.eventBinding == TownVisualObjectEvent::None
		    && t.x == 0 && t.height == 0 && t.z == 0 && t.yaw == 0 && t.scale == 1;
		if (nativeBridge && (object.kind == TownVisualObjectKind::Ground || object.kind == TownVisualObjectKind::Scenery)) {
			if (object.kind == TownVisualObjectKind::Ground) {
				DrawNativeBridgeTile(out, object.anchor, piece);
				++FixedTownObjectState.groundSubmissions;
			}
			continue;
		}
		const Vec3 origin { static_cast<float>(object.anchor.x) + t.x, t.height, static_cast<float>(object.anchor.y) + t.z };
		if (object.kind == TownVisualObjectKind::Ground) {
			const Texture *texture = &FixedTownTextures[object.texture];
			if (changed != 0 && !object.groundFallback) {
				const uint16_t piece = dPiece[object.anchor.x][object.anchor.y];
				if (piece >= MAXTILES) continue;
				texture = &SceneGround(piece);
			}
			const float c = std::cos(t.yaw), s = std::sin(t.yaw);
			const auto vertex = [&](float x, float z) { return Vec3 { origin.x + t.scale * (c * x + s * z), origin.y, origin.z + t.scale * (-s * x + c * z) }; };
			DrawQuad(out, { vertex(-1.46875F, -0.46875F), vertex(-0.46875F, -1.46875F), vertex(0.53125F, -0.46875F), vertex(-0.46875F, 0.53125F) },
				*texture, PickAt(object.anchor), dLight[object.anchor.x][object.anchor.y], true);
			++FixedTownObjectState.groundSubmissions;
		} else {
			VolumeArtwork &art = FixedObjectVolume(index, object, changed);
			DrawVolume(out, origin, object.anchor, art, PickKind::Ground, -1, dLight[object.anchor.x][object.anchor.y], object.kind != TownVisualObjectKind::Scenery);
			if (!art.mesh.triangles.empty()) ++FixedTownObjectState.volumeSubmissions;
		}
	}
	FixedTownObjectState.cachedVolumes = FixedTownVolumes.size();
}

bool CaptureFixedTownObjects(TownVisualObjectSet &set, std::string &failure)
{
	if (leveltype != DTYPE_TOWN || !pDungeonCels || MicroTileLen == 0 || gbIsHellfire) {
		failure = "static-capture-requires-native-town"; return false;
	}
	// Explicit capture snapshots exactly the selected native visual branch once.
	// This function is never called by the successful persistent startup path.
	PrepareTownPropArtwork();
	const Texture &fallback = FallbackGround();
	const auto addTexture = [&](const Texture &texture) -> uint32_t {
		for (size_t i = 0; i < set.textures.size(); ++i) {
			const auto &existing = set.textures[i];
			if (existing.width == static_cast<uint32_t>(texture.width) && existing.height == static_cast<uint32_t>(texture.height)
			    && existing.repeat == texture.repeat && existing.pixels == texture.pixels && existing.opacity == texture.opacity)
				return static_cast<uint32_t>(i);
		}
		TownVisualObjectTexture record;
		record.width = texture.width; record.height = texture.height; record.repeat = texture.repeat;
		record.pixels = texture.pixels; record.opacity = texture.opacity;
		if (record.width == 0 || record.height == 0) { record.width = record.height = 1; record.pixels = { 0 }; }
		set.textures.push_back(std::move(record));
		return static_cast<uint32_t>(set.textures.size() - 1);
	};
	const auto cellAt = [](Point tile) { return TownVisualSourceCell { tile, dPiece[tile.x][tile.y], static_cast<uint8_t>(dSpecial[tile.x][tile.y]) }; };
	const auto cellId = [](std::string_view prefix, Point tile) { return std::string(prefix) + '-' + std::to_string(tile.x) + '-' + std::to_string(tile.y); };
	const auto bindEvent = [](TownVisualObject &object) {
		const Point tile = object.anchor;
		if (tile.x >= 60 && tile.x <= 61 && tile.y >= 70 && tile.y <= 71) {
			object.eventBinding = TownVisualObjectEvent::WellWater;
			object.eventState = Quests[Q_PWATER]._qactive != QUEST_DONE && Quests[Q_PWATER]._qactive != QUEST_NOTAVAIL;
		} else if (tile.x >= 48 && tile.x <= 49 && tile.y >= 20 && tile.y <= 21) {
			object.eventBinding = TownVisualObjectEvent::CatacombsAccess;
			object.eventState = MyPlayer != nullptr && IsWarpOpen(DTYPE_CATACOMBS);
		} else if (tile.x >= 16 && tile.x <= 17 && tile.y >= 68 && tile.y <= 71) {
			object.eventBinding = TownVisualObjectEvent::CavesAccess;
			object.eventState = MyPlayer != nullptr && IsWarpOpen(DTYPE_CAVES);
		} else if (tile.x >= 36 && tile.x <= 55 && tile.y >= 78 && tile.y <= 79) {
			object.eventBinding = TownVisualObjectEvent::HellAccess;
			object.eventState = MyPlayer != nullptr && IsWarpOpen(DTYPE_HELL);
		}
	};
	const auto forEachTile = [](const auto &visit) {
		for (int diagonal = 0; diagonal < MAXDUNX + MAXDUNY - 1; ++diagonal)
			for (int x = std::max(0, diagonal - MAXDUNY + 1); x <= std::min(MAXDUNX - 1, diagonal); ++x)
				visit(Point { x, diagonal - x });
	};
	const auto addGround = [&](Point tile, bool solid) {
		TownVisualObject object;
		object.id = cellId("ground", tile); object.anchor = tile; object.phase = solid ? 1 : 0;
		object.sourceCells.push_back(cellAt(tile));
		object.groundFallback = solid ? (TownSceneReplacesTile(tile) || PreparedTownPropReplacesTile(tile, true)) : PreparedTownPropReplacesTile(tile, true);
		object.texture = addTexture(object.groundFallback ? fallback : SceneGround(dPiece[tile.x][tile.y]));
		bindEvent(object);
		set.objects.push_back(std::move(object));
	};
	forEachTile([&](Point tile) { if (dPiece[tile.x][tile.y] < MAXTILES && !GetTile(dPiece[tile.x][tile.y]).solid) addGround(tile, false); });
	forEachTile([&](Point tile) {
		const uint16_t piece = dPiece[tile.x][tile.y];
		if (piece >= MAXTILES) return;
		const TileArt &art = GetTile(piece);
		if (art.solid) addGround(tile, true);
		TownVisualObject object;
		object.id = cellId("scenery", tile); object.anchor = tile; object.kind = TownVisualObjectKind::Scenery; object.phase = 1;
		object.sourceCells.push_back(cellAt(tile));
		bindEvent(object);
		object.nativeSuppressed = TownVegetationReplacesTile(tile) || TownSceneReplacesTile(tile) || PreparedTownPropReplacesTile(tile);
		object.texture = addTexture(art.facade);
		if (!object.nativeSuppressed && art.height >= 0.25F)
			object.mesh = BuildTownPropVolume({ art.facade.width, art.facade.height, art.facade.pixels, art.facade.opacity });
		set.objects.push_back(std::move(object));
	});
	for (const auto &group : GetTownVegetationGroups()) {
		TownVisualObject object;
		object.id = "tree-" + std::to_string(static_cast<unsigned>(group.family)) + '-' + std::to_string(group.referenceFootpoint.x) + '-' + std::to_string(group.referenceFootpoint.y);
		object.kind = TownVisualObjectKind::Vegetation; object.phase = 2; object.anchor = group.referenceFootpoint;
		const VolumeArtwork art = ComposeVegetation(group); object.mesh = art.mesh; object.physicalFrame = art.physicalFrame; object.texture = addTexture(art.texture);
		for (Point tile : group.sourceTiles) object.sourceCells.push_back(cellAt(tile));
		set.objects.push_back(std::move(object));
	}
	const auto &propGroups = GetTownPropGroups();
	for (size_t i = 0; i < propGroups.size(); ++i) {
		const auto &group = propGroups[i];
		const auto prepared = PropArtworkCache.find(i);
		if (prepared == PropArtworkCache.end() || !TownPropArtworkValid(prepared->second))
			continue; // The generic captured scenery retains this refused native fallback.
		std::string_view prefix;
		switch (group.kind) {
		case town_prop_stencils::Kind::Rock: prefix = "rock"; break;
		case town_prop_stencils::Kind::Grave: prefix = "grave"; break;
		case town_prop_stencils::Kind::Wall: prefix = "wall"; break;
		}
		TownVisualObject object;
		object.id = cellId(prefix, group.referenceFootpoint); object.kind = TownVisualObjectKind::Prop; object.phase = 2; object.anchor = group.referenceFootpoint;
		object.visible = !TownPropHiddenByArchitecture(group);
		const VolumeArtwork &art = prepared->second; object.mesh = art.mesh; object.physicalFrame = art.physicalFrame; object.texture = addTexture(art.texture);
		for (Point tile : group.sourceTiles) object.sourceCells.push_back(cellAt(tile));
		set.objects.push_back(std::move(object));
	}
	failure.clear(); return true;
}

void DrawProps(const Surface &out)
{
	const auto &groups = GetTownPropGroups();
	for (size_t i = 0; i < groups.size(); ++i) {
		const TownPropGroup &group = groups[i];
		if (TownPropHiddenByArchitecture(group))
			continue;
		const auto found = PropArtworkCache.find(i);
		const VolumeArtwork &art = found != PropArtworkCache.end() ? found->second
			: PropArtworkCache.emplace(i, ComposeNativeScenery(group, false)).first->second;
		if (!TownPropArtworkValid(art))
			continue;
		const Point tile = group.referenceFootpoint;
		DrawVolume(out, { static_cast<float>(tile.x), 0, static_cast<float>(tile.y) }, tile, art, PickKind::Ground, -1,
			InDungeonBounds(tile) ? dLight[tile.x][tile.y] : 0);
	}
}

void DrawVegetation(const Surface &out)
{
	const auto &groups = GetTownVegetationGroups();
	for (size_t i = 0; i < groups.size(); ++i) {
		const TownVegetationGroup &group = groups[i];
		const auto found = VegetationArtworkCache.find(i);
		const VolumeArtwork &art = found != VegetationArtworkCache.end() ? found->second
			: VegetationArtworkCache.emplace(i, ComposeVegetation(group)).first->second;
		const Point tile = group.referenceFootpoint;
		DrawVolume(out, { static_cast<float>(tile.x), 0, static_cast<float>(tile.y) }, tile, art, PickKind::Ground, -1,
			InDungeonBounds(tile) ? dLight[tile.x][tile.y] : 0);
	}
}

void DrawVolumetricSprite(const Surface &out, Vec3 position, Point tile, ClxSprite sprite, PickKind kind, int entity,
	int lighting = 0)
{
	if (!ViewCamera.projection.perspective && (std::abs(position.x - ViewCamera.target.x) > 80 || std::abs(position.z - ViewCamera.target.z) > 80))
		return;
	auto &cache = ActorVolumeCache;
	const uint8_t *key = sprite.pixelData();
	uint64_t sourceHash = 1469598103934665603ULL;
	for (uint32_t i = 0; i < sprite.pixelDataSize(); ++i) {
		sourceHash ^= key[i];
		sourceHash *= 1099511628211ULL;
	}
	auto found = cache.find(key);
	if (found != cache.end() && (found->second.sourceHash != sourceHash
		|| found->second.texture.width != sprite.width() || found->second.texture.height != sprite.height())) {
		cache.erase(found);
		found = cache.end();
	}
	if (found == cache.end()) {
		if (cache.size() >= 192)
			cache.clear();
		VolumeArtwork artwork;
		artwork.sourceHash = sourceHash;
		artwork.texture = DecodeSprite(sprite);
		const TownVolumeSprite source { artwork.texture.width, artwork.texture.height, artwork.texture.pixels, artwork.texture.opacity };
		artwork.physicalFrame = kind == PickKind::Towner || kind == PickKind::Player;
		const bool humanoid = kind != PickKind::Towner || entity < 0 || Towners[entity]._ttype != TOWN_COW;
		artwork.mesh = artwork.physicalFrame ? BuildTownActorSingleViewBody(source, humanoid) : BuildTownActorVolume(source);
		if (artwork.physicalFrame) {
			artwork.shadow = ActorGroundShadow(artwork.texture);
			artwork.texture.opacity = TownActorBodyOpacity(source);
		}
		found = cache.emplace(key, std::move(artwork)).first;
	}
	DrawVolume(out, position, tile, found->second, kind, entity, lighting, false, PhysicalToNativeGrid(1));
}

void DrawDirectionalActorVolume(const Surface &out, Vec3 position, Point tile, ClxSpriteSheet sheet,
	int direction, int frame, ClxSprite current, PickKind kind, int entity)
{
	if (sheet.numLists() != 8 || direction < 0 || direction >= 8 || frame < 0) {
		DrawVolumetricSprite(out, position, tile, current, kind, entity);
		return;
	}
	for (int i = 0; i < 8; ++i) {
		if (static_cast<uint32_t>(frame) >= sheet[i].numSprites()) {
			DrawVolumetricSprite(out, position, tile, current, kind, entity);
			return;
		}
	}
	if (sheet[direction][frame].pixelData() != current.pixelData()) {
		DrawVolumetricSprite(out, position, tile, current, kind, entity);
		return;
	}
	uint64_t sourceHash = 1469598103934665603ULL;
	for (int view = 0; view < 8; ++view) {
		const ClxSprite sprite = sheet[view][frame];
		for (uint32_t i = 0; i < sprite.pixelDataSize(); ++i) {
			sourceHash ^= sprite.pixelData()[i];
			sourceHash *= 1099511628211ULL;
		}
		sourceHash ^= sprite.width() + (static_cast<uint64_t>(sprite.height()) << 16);
		sourceHash *= 1099511628211ULL;
	}
	sourceHash ^= static_cast<uint64_t>(direction);
	sourceHash *= 1099511628211ULL;
	const uint8_t *key = current.pixelData();
	auto found = ActorVolumeCache.find(key);
	if (found != ActorVolumeCache.end() && (!found->second.physicalFrame || found->second.sourceHash != sourceHash)) {
		ActorVolumeCache.erase(found);
		found = ActorVolumeCache.end();
	}
	if (found == ActorVolumeCache.end()) {
		if (ActorVolumeCache.size() >= 192)
			ActorVolumeCache.clear();
		VolumeArtwork art;
		art.sourceHash = sourceHash;
		art.physicalFrame = true;
		std::array<TownVolumeSprite, 8> views;
		for (int view = 0; view < 8; ++view) {
			Texture &texture = art.directionalTextures[view];
			texture = DecodeSprite(sheet[view][frame]);
			views[view] = { texture.width, texture.height, texture.pixels, texture.opacity };
		}
		art.texture = art.directionalTextures[direction];
		art.mesh = BuildTownActorVisualHull(views, direction);
		art.shadow = ActorGroundShadow(art.texture);
		for (Texture &texture : art.directionalTextures)
			texture.opacity = TownActorBodyOpacity({ texture.width, texture.height, texture.pixels, texture.opacity });
		art.texture = art.directionalTextures[direction];
		if (art.mesh.triangles.empty()) {
			DrawVolumetricSprite(out, position, tile, current, kind, entity);
			return;
		}
		found = ActorVolumeCache.emplace(key, std::move(art)).first;
	}
	DrawVolume(out, position, tile, found->second, kind, entity, 0, false, PhysicalToNativeGrid(1));
}

void DrawPlayerVolume(const Surface &out, Vec3 position, const Player &player, int entity)
{
	const auto &data = player.AnimationData[static_cast<size_t>(player.getGraphic())];
	const ClxSprite current = player.currentSprite();
	if (!data.sprites) {
		DrawVolumetricSprite(out, position, player.position.tile, current, PickKind::Player, entity);
		return;
	}
	DrawDirectionalActorVolume(out, position, player.position.tile, *data.sprites,
		static_cast<int>(player._pdir), player.AnimInfo.getFrameToUseForRendering(), current, PickKind::Player, entity);
}

struct WarriorPilotArtwork {
	PlayerActorNativeRuntime runtime;
	std::array<std::optional<PlayerActorNativePilotFrame>, MAX_PLRS> frames;
	std::array<uint8_t, MAX_PLRS> retainedProgress {};
	Texture body;
	std::array<std::vector<Texture>, 2> equipment;
	std::array<uint32_t, 256> palette {};
	std::map<std::pair<int, int>, Texture> inventorySprites;
	bool attempted = false, materialsReady = false, cathedralMaterials = false;
	int timeOfDay = -1;
};
WarriorPilotArtwork WarriorPilot;
TownViewWarriorPilotState WarriorPilotState;

bool EnsureWarriorNativePilotLoaded()
{
	if (WarriorPilot.attempted) return WarriorPilot.runtime.loaded();
	WarriorPilot.attempted = true;
	WarriorPilotState.attempted = true;
	try {
		std::array<std::vector<std::byte>, 3> owned;
		std::array<std::span<const std::byte>, 3> packets;
		for (size_t i = 0; i < owned.size(); ++i) {
			const auto &profile = PlayerActorHumanPilotResources[i];
			AssetRef ref = FindAsset(profile.path);
			if (!ref.ok() || ref.size() != profile.packageBytes) {
				WarriorPilotState.refusal = "warrior-pinned-resource-missing-or-size"; return false;
			}
			owned[i].resize(profile.packageBytes);
			AssetHandle handle = OpenAsset(std::move(ref));
			if (!handle.ok() || !handle.read(owned[i].data(), owned[i].size())) {
				WarriorPilotState.refusal = "warrior-resource-read"; return false;
			}
			packets[i] = owned[i];
		}
		if (!WarriorPilot.runtime.Load(packets, WarriorPilotState.refusal)) return false;
		WarriorPilotState.loaded = true;
		return true;
	} catch (const std::bad_alloc &) {
		WarriorPilotState.refusal = "warrior-resource-allocation"; return false;
	}
}

bool PrepareWarriorNativeMaterials(bool cathedralWorld)
{
	std::array<uint32_t, 256> palette;
	for (size_t i = 0; i < palette.size(); ++i) {
		const auto color = logical_palette[i];
		palette[i] = 0xFF000000U | (uint32_t(color.r) << 16) | (uint32_t(color.g) << 8) | color.b;
	}
	if (WarriorPilot.materialsReady && WarriorPilot.cathedralMaterials == cathedralWorld
	    && WarriorPilot.palette == palette && (cathedralWorld || WarriorPilot.timeOfDay == SceneTimeOfDay)) return true;
	try {
		const auto nativeLight = cathedral::BuildNativeLightLut(LightTables);
		const auto world = cathedralWorld ? PlayerActorMaterialWorld::NativeCathedral : PlayerActorMaterialWorld::TownDirectional;
		const auto prepare = [&](uint32_t width, uint32_t height, std::span<const uint8_t> rgb, Texture &texture) {
			PlayerActorMaterial material;
			if (!PreparePlayerActorMaterial({ width, height, rgb, palette, nativeLight, world, SceneLightingConfig },
			        material, WarriorPilotState.refusal)) return false;
			texture.width = width; texture.height = height;
			texture.actorMaterial = std::move(material);
			return true;
		};
		Texture body;
		const ActorModel &model = *WarriorPilot.runtime.Resources().Body();
		if (!prepare(model.textureWidth, model.textureHeight, model.textureRgb, body)) return false;
		std::array<std::vector<Texture>, 2> gear;
		const std::array<const ActorRigidResource *, 2> rigid { WarriorPilot.runtime.Resources().Sword(), WarriorPilot.runtime.Resources().Buckler() };
		for (size_t resource = 0; resource < rigid.size(); ++resource) {
			// This exact pilot has no encoded gear textures. Keep all authored
			// material partitions and vertex COLOR0; do not flatten 27 shield parts.
			if (!rigid[resource]->textures.empty()) { WarriorPilotState.refusal = "warrior-unsupported-rigid-textures"; return false; }
			gear[resource].resize(rigid[resource]->materials.size());
			for (size_t index = 0; index < gear[resource].size(); ++index) {
				const auto &material = rigid[resource]->materials[index];
				if (material.alphaMode != 0 || material.baseColor[3] != 1
				    || std::any_of(material.textures.begin(), material.textures.end(), [](const auto &texture) { return texture.texture >= 0; })) {
					WarriorPilotState.refusal = "warrior-unsupported-rigid-material"; return false;
				}
				const auto srgb = TownLinearToSrgb({ material.baseColor[0], material.baseColor[1], material.baseColor[2] });
				const auto byte = [](float color) { return static_cast<uint8_t>(std::clamp(color * 255 + .5F, 0.0F, 255.0F)); };
				const std::array<uint8_t, 3> rgb { byte(srgb.red), byte(srgb.green), byte(srgb.blue) };
				if (!prepare(1, 1, rgb, gear[resource][index])) return false;
			}
			for (const auto &part : rigid[resource]->parts) if (part.material >= gear[resource].size()) {
				WarriorPilotState.refusal = "warrior-rigid-material-out-of-range"; return false;
			}
		}
		WarriorPilot.body = std::move(body); WarriorPilot.equipment = std::move(gear);
		WarriorPilot.palette = palette; WarriorPilot.cathedralMaterials = cathedralWorld;
		WarriorPilot.timeOfDay = SceneTimeOfDay; WarriorPilot.materialsReady = true;
		return true;
	} catch (const std::bad_alloc &) { WarriorPilotState.refusal = "warrior-material-allocation"; return false; }
}

struct WarriorFirstPersonPresentation {
	const PlayerActorNativePilotFrame *frame = nullptr;
	Vec3 footpoint {}, right {}, forward {};
	float nativeCos = 1, nativeSin = 0;
};
WarriorFirstPersonPresentation WarriorFirstPerson;

Vec3 WarriorFirstPersonPoint(Vec3 position)
{
	const auto &pose = WarriorFirstPerson;
	const auto p = position - pose.footpoint;
	const float localX = pose.nativeCos * p.x - pose.nativeSin * p.z;
	const float localZ = pose.nativeSin * p.x + pose.nativeCos * p.z;
	// Asset front is +Z and anatomical right is -X. This complete horizontal
	// basis (-cameraRight, worldUp, horizontalForward) has determinant +1.
	return pose.footpoint + pose.right * -localX + Vec3 { 0, p.y, 0 } + pose.forward * localZ;
}

void PrepareWarriorNativeFrame(bool cathedralWorld)
{
	for (auto &frame : WarriorPilot.frames) frame.reset();
	WarriorPilotState.preparedPlayers = WarriorPilotState.worldBodyDraws = WarriorPilotState.firstPersonDraws = 0;
	WarriorPilotState.bodyTriangles = WarriorPilotState.handTriangles = WarriorPilotState.rigidTriangles = WarriorPilotState.spriteQuads = 0;
	WarriorFirstPerson = {};
	if (!EnsureWarriorNativePilotLoaded()) return;
	const WarriorWorldIdentity world { cathedralWorld ? WarriorWorldKind::Cathedral : WarriorWorldKind::Town,
		currlevel, setlevel, gbIsHellfire, gbIsSpawn };
	const auto sprites = [](const PlayerActorEquippedHand &item) -> std::optional<PlayerActorInventorySpriteSource> {
		const int cursor = item.inventoryCursor + CURSOR_FIRSTITEM;
		const auto sprite = TryGetInvItemSprite(cursor);
		if (!sprite) return std::nullopt;
		// Explicit pilot grip: bottom center, full alpha canvas, 0.731 authored
		// rig units high (the real sword bounds); no ground/category substitution.
		return PlayerActorInventorySpriteSource { sprite, { sprite->width() * .5F, float(sprite->height() - 1) }, .731F / sprite->height() };
	};
	for (size_t slot = 0; slot < Players.size(); ++slot) {
		const Player &player = Players[slot];
		if (!player.plractive || !player.isOnActiveLevel() || !player.AnimInfo.sprites || player._pClass != HeroClass::Warrior) continue;
		const bool local = &player == MyPlayer;
		const bool firstPerson = local && CameraRig.mode() == TownCameraMode::FirstPerson;
		const bool hidden = local && FollowCameraState.localPlayerHidden;
		if (PauseMode != 2) WarriorPilot.retainedProgress[slot] = static_cast<uint8_t>(std::min<unsigned>(ProgressToNextGameTick, 128));
		const auto position = PlayerPosition(player);
		PlayerActorNativePilotFrame frame;
		if (!WarriorPilot.runtime.Prepare(player, NativePlayerActorLifecycle(),
		        { world, static_cast<uint32_t>(slot), { position.x, 0, position.z }, WarriorPilot.retainedProgress[slot],
		            PauseMode == 2, local, hidden, firstPerson }, sprites, frame, WarriorPilotState.refusal)) continue;
		bool valid = true;
		try {
		for (auto &sprite : frame.hands.spriteAttachments) {
			if (!sprite.sprite) { valid = false; break; }
			const std::pair<int, int> key { sprite.equipment.nativeItemId, sprite.equipment.inventoryCursor };
			if (WarriorPilot.inventorySprites.find(key) == WarriorPilot.inventorySprites.end()) {
				try { WarriorPilot.inventorySprites.emplace(key, DecodeSprite(*sprite.sprite)); }
				catch (const std::bad_alloc &) { valid = false; break; }
			}
			const auto &texture = WarriorPilot.inventorySprites.at(key);
			frame.inventoryPixels.push_back({ key.first, key.second, texture.width, texture.height, texture.pixels, texture.opacity });
			sprite.sprite.reset(); // Borrowed CLX used only during the owned decode.
		}
		} catch (const std::bad_alloc &) { valid = false; }
		if (!valid) { WarriorPilotState.refusal = "warrior-inventory-sprite-texture"; continue; }
		WarriorPilot.frames[slot] = std::move(frame); ++WarriorPilotState.preparedPlayers;
	}
}

void PrepareWarriorFirstPersonPresentation()
{
	if (CameraRig.mode() != TownCameraMode::FirstPerson || MyPlayer == nullptr || !ViewCamera.projection.valid) return;
	const size_t slot = static_cast<size_t>(MyPlayer - Players.data());
	if (slot >= WarriorPilot.frames.size() || !WarriorPilot.frames[slot]) return;
	const auto &frame = *WarriorPilot.frames[slot];
	if (!frame.firstPerson || !WarriorPilot.runtime.IsCurrent(*MyPlayer, NativePlayerActorLifecycle(), frame)) return;
	// Provisional measured iris-surface landmarks from the same pinned BODY:
	// source UV/triangle barycentrics, not guessed Head-pivot offsets. These
	// vertices include small Neck weights, so use their already sampled skin.
	static constexpr std::array<std::array<uint32_t, 3>, 2> eyeIndices {{
		{ 1125, 1122, 1124 }, { 2795, 2797, 2796 }
	}};
	static constexpr std::array<std::array<float, 3>, 2> eyeWeights {{
		{ .03924311324954033F, .3877051770687103F, .5730516910552979F },
		{ .06292831897735596F, .5515196919441223F, .38555195927619934F }
	}};
	Vec3 nativeEye {};
	for (size_t eye = 0; eye < eyeIndices.size(); ++eye) {
		Vec3 point {};
		float total = 0;
		for (size_t i = 0; i < eyeIndices[eye].size(); ++i) {
			if (eyeIndices[eye][i] >= frame.geometry.body.vertices.size()) return;
			const auto p = frame.geometry.body.vertices[eyeIndices[eye][i]].worldPosition;
			point = point + Vec3 { p.x, p.y, p.z } * eyeWeights[eye][i];
			total += eyeWeights[eye][i];
		}
		nativeEye = nativeEye + point * (.5F / total);
	}
	const auto &capture = frame.capture.snapshot;
	const auto &camera = ViewCamera.projection;
	const float horizontal = std::hypot(camera.forward.x, camera.forward.z);
	if (!std::isfinite(horizontal) || horizontal < .001F) return;
	WarriorFirstPerson = { &frame,
		{ capture.authoritativeFootpoint.x, capture.authoritativeFootpoint.y, capture.authoritativeFootpoint.z },
		{ camera.right.x, 0, camera.right.z },
		{ camera.forward.x / horizontal, 0, camera.forward.z / horizontal },
		std::cos(capture.nativeYawRadians), std::sin(capture.nativeYawRadians) };
	if (Dot(Cross(WarriorFirstPerson.right * -1, { 0, 1, 0 }), WarriorFirstPerson.forward) < .999F) {
		WarriorFirstPerson = {};
		return;
	}
	const Vec3 eye = WarriorFirstPersonPoint(nativeEye);
	const TownCameraPoint presentedEye { eye.x, eye.y, eye.z };
	const float radius = PhysicalCameraRadius(camera);
	bool safe = std::isfinite(eye.x) && std::isfinite(eye.y) && std::isfinite(eye.z)
	    && radius > 0 && eye.y >= radius + .002F;
	if (safe && !FollowThroughTownArchitecture()) {
		// Refuse rather than detach the eye from its rig to recover a safe boom.
		// Raw camera resolution already prepared this scene's collision index.
		const auto hit = CameraCollisionIndex.Sweep(camera.eye, presentedEye, radius);
		FollowCameraState.nodesVisited += hit.nodesVisited;
		FollowCameraState.trianglesTested += hit.trianglesTested;
		safe = hit.valid && !hit.initialOverlap && hit.fraction >= 1;
	}
	if (!safe) {
		WarriorFirstPerson = {};
		ViewCamera.projection.valid = false;
		FollowCameraState.valid = false;
		WarriorPilotState.refusal = "warrior-fp-eye-projection-collision";
		return;
	}
	ViewCamera.projection.eye = presentedEye;
	ViewCamera.eye = eye;
	FollowCameraState.desiredEye = FollowCameraState.resolvedEye = presentedEye;
	FollowCameraState.eyeHeight = eye.y - capture.authoritativeFootpoint.y;
	FollowCameraState.visualDistance = FollowCameraState.resolvedDistance = 0;
	FollowCameraState.localPlayerHidden = true;
	HasSafeCameraEye = false; // Never reuse a calibrated eye as an unrelated boom.
	++FollowProjectionRevision;
}

bool WarriorFirstPersonBodyAdmitted(const Player &player, size_t slot)
{
	// Admission, not requested visibility or visible arm triangles. A diagnostic
	// self-draw OFF still has the same admitted frame for OFF/ON pick witnesses.
	if (&player != MyPlayer || CameraRig.mode() != TownCameraMode::FirstPerson
	    || !ViewCamera.projection.valid || !WarriorPilot.materialsReady || player.hasNoLife()
	    || CathedralTranslation != nullptr || slot >= WarriorPilot.frames.size()
	    || !WarriorPilot.frames[slot]) return false;
	const auto &frame = *WarriorPilot.frames[slot];
	return frame.firstPerson && WarriorFirstPerson.frame == &frame
	    && !frame.geometry.body.vertices.empty() && !frame.geometry.body.indices.empty()
	    && WarriorPilot.runtime.IsCurrent(player, NativePlayerActorLifecycle(), frame);
}

bool DrawWarriorNativePilot(const Surface &out, const Player &player, size_t slot, bool selectable, int shade, bool firstPerson)
{
	if (slot >= WarriorPilot.frames.size() || !WarriorPilot.frames[slot] || !WarriorPilot.materialsReady || CathedralTranslation != nullptr) return false;
	const auto &frame = *WarriorPilot.frames[slot];
	if (frame.firstPerson != firstPerson || !WarriorPilot.runtime.IsCurrent(player, NativePlayerActorLifecycle(), frame)) return false;
	const auto &geometry = frame.geometry;
	if (firstPerson && WarriorFirstPerson.frame != &frame) return false;
	const auto convert = [&](const ActorPlayerGeometryVertex &vertex) {
		const auto &p = vertex.worldPosition;
		const Vec3 world { p.x, p.y, p.z };
		const Vec3 position = firstPerson ? WarriorFirstPersonPoint(world) : world;
		return Vertex { position, vertex.uv.x, vertex.uv.y,
			{ vertex.authoredColor.x, vertex.authoredColor.y, vertex.authoredColor.z, vertex.authoredColor.w } };
	};
	PickRecord pick = PickAt(player.position.tile, firstPerson || selectable ? PickKind::Player : PickKind::Ground,
		firstPerson || selectable ? static_cast<int>(slot) : -1);
	if (firstPerson) pick.preservePicking = true;
	const auto draw = [&](const auto &vertices, std::span<const uint32_t> indices, const Texture &texture, bool twoSided) {
		for (size_t triangle = 0; triangle < indices.size(); triangle += 3) {
			const std::array<Vertex, 3> projected { convert(vertices[indices[triangle]]), convert(vertices[indices[triangle + 1]]), convert(vertices[indices[triangle + 2]]) };
			const auto normal = Cross(projected[1].position - projected[0].position, projected[2].position - projected[0].position);
			if (!twoSided && Dot(normal, ViewCamera.eye - projected[0].position) <= 0) continue;
			const float length = std::sqrt(Dot(normal, normal));
			if (length <= 0) continue;
			const TownSceneNormal face { normal.x / length, normal.y / length, normal.z / length };
			DrawTriangle(out, projected, texture, pick, shade, false, &face);
		}
	};
	if (firstPerson) {
		draw(geometry.body.vertices, frame.hands.leftArmIndices, WarriorPilot.body, true);
		draw(geometry.body.vertices, frame.hands.rightArmIndices, WarriorPilot.body, true);
		WarriorPilotState.handTriangles += (frame.hands.leftArmIndices.size() + frame.hands.rightArmIndices.size()) / 3;
		++WarriorPilotState.firstPersonDraws;
	} else {
		draw(geometry.body.vertices, geometry.body.indices, WarriorPilot.body, true);
		WarriorPilotState.bodyTriangles += geometry.body.indices.size() / 3;
		++WarriorPilotState.worldBodyDraws;
	}
	for (const auto &attachment : geometry.attachments) {
		const size_t resource = attachment.nativeItemId == 1 ? 0 : 1;
		for (const auto &part : attachment.parts) {
			draw(part.vertices, part.indices, WarriorPilot.equipment[resource][part.material], true);
			WarriorPilotState.rigidTriangles += part.indices.size() / 3;
		}
	}
	for (const auto &sprite : frame.hands.spriteAttachments) {
		const auto texture = WarriorPilot.inventorySprites.find({ sprite.equipment.nativeItemId, sprite.equipment.inventoryCursor });
		draw(sprite.quad, PlayerActorInventorySpriteQuadIndices, texture->second, true);
		++WarriorPilotState.spriteQuads;
	}
	return true;
}

void DrawWarriorViewModel(const Surface &out)
{
	if (!WarriorViewModelEnabledForDiagnostics || CameraRig.mode() != TownCameraMode::FirstPerson || MyPlayer == nullptr) return;
	const size_t slot = static_cast<size_t>(MyPlayer - Players.data());
	if (slot >= WarriorPilot.frames.size() || !WarriorPilot.frames[slot] || !WarriorPilot.frames[slot]->firstPerson) return;
	try {
		struct VisualDepthScope {
			bool cpu;
			bool previous;
			std::vector<float> depth;
			explicit VisualDepthScope(bool gpu) : cpu(!gpu), previous(DrawingWarriorViewModel)
			{
				if (cpu) {
					depth.assign(DepthBuffer.size(), std::numeric_limits<float>::infinity());
					depth.swap(DepthBuffer);
				}
				DrawingWarriorViewModel = true;
			}
			~VisualDepthScope()
			{
				DrawingWarriorViewModel = previous;
				if (cpu) depth.swap(DepthBuffer);
			}
		} scope(CaptureGpu);
		DrawWarriorNativePilot(out, *MyPlayer, slot, false, 0, true);
	} catch (const std::bad_alloc &) {
		WarriorPilotState.refusal = "warrior-view-model-allocation";
	}
}

void DrawBillboard(const Surface &out, Vec3 position, Point tile, ClxSprite sprite, PickKind kind, int entity, int lighting = 0, bool preservePicking = false, float scale = 1, float contactHeight = -1, bool groundAligned = false, bool skullFlameOnly = false)
{
	if (!ViewCamera.projection.perspective && (std::abs(position.x - ViewCamera.target.x) > 80 || std::abs(position.z - ViewCamera.target.z) > 80))
		return;
	const uint8_t *key = sprite.pixelData();
	uint64_t hash = 1469598103934665603ULL;
	for (uint32_t i = 0; i < sprite.pixelDataSize(); ++i) {
		hash ^= key[i];
		hash *= 1099511628211ULL;
	}
	if (const auto *translation = sprite.hdTranslation()) {
		for (size_t i = 0; i < 256; ++i) {
			hash ^= translation[i];
			hash *= 1099511628211ULL;
		}
	}
	hash ^= sprite.hdFrameId();
	const int density = sprite.hdScale();
	auto &billboardCache=skullFlameOnly?SkullFireFlameCache:BillboardTextureCache;
	auto found = billboardCache.find(key);
	if (found != billboardCache.end() && (found->second.sourceHash != hash
	    || found->second.texture.width != sprite.width() * density || found->second.texture.height != sprite.height() * density)) {
		billboardCache.erase(found);
		found = billboardCache.end();
	}
	if (found == billboardCache.end()) {
		if (billboardCache.size() >= 192)
			billboardCache.clear();
		found = billboardCache.emplace(key, BillboardArtwork { DecodeSprite(sprite, true), hash }).first;
        if(skullFlameOnly){
            // The native flame sprite includes its old skull base. Crop that
            // base only in this separate 3D effect cache; source/2D stay intact.
            auto &flame=found->second.texture;int bottom=-1;
            for(int y=flame.height-1;y>=0 && bottom<0;--y)for(int x=0;x<flame.width;++x)if(flame.opacity[size_t(y)*flame.width+x]){bottom=y;break;}
            int left=flame.width,right=-1;
            for(int y=0;y<flame.height;++y)for(int x=0;x<flame.width;++x)if(flame.opacity[size_t(y)*flame.width+x]){left=std::min(left,x);right=std::max(right,x);}
            // Native canvas contains substantial side padding. Measure the
            // actual skull silhouette, not animWidth, to avoid deleting fire.
            const int bodyRows=std::max(1,int(std::ceil((right-left+1)*.34F)));
            const int cut=std::max(0,bottom-bodyRows+1);
            std::fill(flame.opacity.begin()+size_t(cut)*flame.width,flame.opacity.end(),0);
        }
	}
	const Texture &texture = found->second.texture;
	scale = PhysicalToNativeGrid(scale);
	const float width = static_cast<float>(sprite.width()) / NativeCameraScale * scale;
	const float height = static_cast<float>(sprite.height()) / PixelsPerWorldUnit * scale;
	if (contactHeight >= 0) {
		// C1 flame frames have a large transparent native offset. Anchor their
		// actual coverage to the physical bowl, not the bitmap rectangle. Cache
		// this measurement once per decoded frame; native pixels and animation
		// remain unchanged. Other billboards retain their native placement.
		int &bottom = found->second.bottomCoveredRow;
		if (bottom < 0) {
			for (int row = texture.height - 1; row >= 0 && bottom < 0; --row) {
				const auto begin = texture.opacity.begin() + static_cast<size_t>(row) * texture.width;
				if (std::any_of(begin, begin + texture.width, [](uint8_t covered) { return covered != 0; }))
					bottom = row;
			}
		}
		if (bottom >= 0)
			position.y += contactHeight - static_cast<float>(texture.height - 1 - bottom) / (PixelsPerWorldUnit * density) * scale;
	}
    if(groundAligned) {
        // Fixed native-isometric world basis: death direction is already baked into the selected sprite.
        const auto point=[&](float dx,float dy){return Vec3{position.x+(dx+2*dy)/64.0F,0.012F,position.z+(2*dy-dx)/64.0F};};
        const float half=sprite.width()*scale/2,depth=sprite.height()*scale;
        DrawQuad(out,{point(-half,-depth),point(half,-depth),point(half,0),point(-half,0)},texture,PickAt(tile,kind,entity),lighting,true);
        ++CorpseGroundDraws;return;
    }
	// CLX's anchor is the bottom pixel, while a rasterized quad ends at the edge
	// after that pixel. Keep the native anchor and its last row at the default pose.
	position.y -= scale / PixelsPerWorldUnit;
	const Vec3 halfWidth = ViewCamera.right * (width / 2);
	const Vec3 rise { 0, height, 0 };
	PickRecord pick = PickAt(tile, kind, entity);
	pick.preservePicking = preservePicking;
	DrawQuad(out, { position - halfWidth + rise, position + halfWidth + rise,
		position + halfWidth, position - halfWidth }, texture, pick, lighting, true);
}

void DrawBarrelNativeBreakEffect(const Surface &out,const Object &object,int slot,bool selectable)
{
    // Dedicated replacement seam for a future authored effect. Today use the exact native sprite/frame.
    DrawBillboard(out,{float(object.position.x),0,float(object.position.y)},object.position,object.currentSprite(),selectable ? PickKind::Object : PickKind::Ground,selectable ? slot : -1,object.applyLighting ? dLight[object.position.x][object.position.y] : 0);
    ++Barrel.nativeBreakDraws;if(object.isExplosive())++Barrel.nativeExplosionDraws;
}



bool CathedralTileInFrame(Point tile)
{
	return InDungeonBounds(tile) && tile.x >= CathedralFrame.regionX - cathedral::RegionSize
	    && tile.x < CathedralFrame.regionX + 2 * cathedral::RegionSize
	    && tile.y >= CathedralFrame.regionZ - cathedral::RegionSize
	    && tile.y < CathedralFrame.regionZ + 2 * cathedral::RegionSize;
}

void InjectCathedralAllocationFailure(TownViewCathedralAllocationFailurePoint point)
{
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	if (!CaptureGpu || !RendererState.requestedGpu || CathedralAllocationState.armed != point)
		return;
	// Diagnostic only: consume before throwing, so same-key recovery is unarmed.
	CathedralAllocationState.armed = TownViewCathedralAllocationFailurePoint::None;
	CathedralAllocationState.injected = point;
	throw std::bad_alloc();
#else
	(void)point;
#endif
}

void InjectCathedralReadbackAllocationFailure()
{
	InjectCathedralAllocationFailure(TownViewCathedralAllocationFailurePoint::BeforeReadback);
}

void DrawCathedralWorld(const Surface &out)
{
	// Existing native sprite bytes and light/TRN tables are sampled by the same
	// camera, clipping, raster depth and pick transaction as technical geometry.
	DrawingCathedral = true;
	CathedralTranslation = nullptr;
	static std::array<Texture, 16> textures;
	constexpr std::array<std::array<int, 3>, 16> colors { {
		{ 77, 87, 99 }, { 148, 158, 171 }, { 168, 184, 196 }, { 179, 179, 179 },
		{ 204, 153, 179 }, { 204, 128, 179 }, { 224, 158, 71 }, { 212, 79, 33 },
		{ 64, 184, 122 }, { 59, 143, 235 }, { 179, 51, 179 }, { 145, 107, 191 },
		{ 191, 196, 204 }, { 186, 156, 71 }, { 115, 173, 189 }, { 186, 125, 199 }
	} };
	for (size_t i = 0; i < textures.size(); ++i) {
		textures[i].width = textures[i].height = 1;
		const uint8_t color = ClosestSceneColor(static_cast<float>(colors[i][0]),
			static_cast<float>(colors[i][1]), static_cast<float>(colors[i][2]));
		if (textures[i].pixels.empty() || textures[i].pixels[0] != color) {
			textures[i].pixels.assign(1, color);
			textures[i].gpuIdentity = TextureGpuIdentity {};
		}
	}
	const auto drawSurface = [&](const cathedral::FrameTriangle &surface) {
		if (surface.visualSuppressed) return; // No color, depth or pixel pick for hidden art.
        if(DoorModels.loaded && (surface.module==cathedral::ModuleKind::DoorFrame || surface.module==cathedral::ModuleKind::DoorLeaf))return;
		if (FollowThroughTownArchitecture() && CameraRig.mode() == TownCameraMode::ThirdPerson
		    && CathedralVisibilityGeometry == CathedralFrame.geometryRevision) {
			const auto owner = CathedralVisibilityOwners.find(surface.instanceId);
			if (owner != CathedralVisibilityOwners.end() && owner->second < CathedralHiddenOwners.size()
			    && CathedralHiddenOwners[owner->second] != 0) return;
		}
		const Texture *texture = &textures[static_cast<size_t>(surface.module)];
		const auto *artVertices = &surface.vertices;
		if (surface.nativeTexture.kind != cathedral::NativeTextureKind::Technical) {
			// Preparation and the pre-draw completeness gate own every miss.
			// Draw performs no decode, material construction or identity mutation.
			texture = surface.nativeTexture.kind == cathedral::NativeTextureKind::Masonry
			    ? &CathedralNativeLitTextureCache.at(NativeLitTextureKey(surface.nativeTexture)).texture
			    : &CathedralNativeTextureCache.at(NativeTextureKey(surface.nativeTexture)).texture;
			artVertices = &surface.textureVertices;
		}
		std::array<Vertex, 3> vertices;
		for (size_t n = 0; n < vertices.size(); ++n) {
			const auto &vertex = (*artVertices)[n];
			const float woodDensity = surface.nativeTexture.kind == cathedral::NativeTextureKind::DoorWood
			    ? 32.0F / static_cast<float>(texture->width) : 1.0F;
			vertices[n] = { { vertex.position.x, vertex.position.y, vertex.position.z },
				vertex.u * woodDensity, vertex.v * woodDensity };
		}
		const Point tile { surface.pick.x, surface.pick.z };
		PickRecord pick = PickAt(tile);
		if (surface.pick.kind == cathedral::PickKind::Object && surface.pick.nativeSlot >= 0
		    && surface.pick.nativeSlot < MAXOBJECTS && Objects[surface.pick.nativeSlot].canInteractWith()
		    && std::abs(dObject[tile.x][tile.y]) == surface.pick.nativeSlot + 1)
			pick = PickAt(tile, PickKind::Object, surface.pick.nativeSlot);
		const uint8_t shade = surface.nativeTexture.kind == cathedral::NativeTextureKind::Masonry ? 0 : surface.light;
		DrawTriangle(out, vertices, *texture, pick, shade); // Lit codes use the same identity palette on CPU/GPU.
		// First-person dungeon ceiling is presentation only: no Town geometry,
		// simulation collision or pick target is added. Third-person stays open.
		if (surface.module == cathedral::ModuleKind::Floor
		    && surface.nativeTexture.layout == cathedral::NativeTextureLayout::RectifiedFloor
		    && GetTownViewCameraMode() == TownCameraMode::FirstPerson) {
			for (auto &vertex : vertices) vertex.position.y = 2.8F;
			std::swap(vertices[0],vertices[2]);
			const auto &ceiling = CathedralNativeTextureCache.at(NativeTextureKey(CathedralCeilingBinding)).texture;
			DrawTriangle(out,vertices,ceiling,PickRecord {},surface.light);
		}
	};
	const auto drawSpecial = [&](const cathedral::NativeSpecialOverlay &special) {
		if (!pSpecialCels || special.frameIndex < 0 || static_cast<uint32_t>(special.frameIndex) >= pSpecialCels->numSprites())
			return; // Normal frames were gated; forced diagnostics remain safe.
		const Point tile { special.x, special.z };
        if(DoorModels.loaded)if(const auto *snapshot=cathedral::LiveSnapshot())for(const auto &door:snapshot->doors)if(std::abs(door.x-tile.x)<=1 && std::abs(door.z-tile.y)<=1)return; // Replaced doorway includes its neighboring native trim cells.
		// CLX holes remain coverage; TransList is only the original 2D view aid.
		DrawBillboard(out, { static_cast<float>(tile.x), 0, static_cast<float>(tile.y) }, tile,
			(*pSpecialCels)[special.frameIndex], PickKind::Ground, -1, special.light);
	};
	CathedralPaletteBlend = false;
	InjectCathedralAllocationFailure(TownViewCathedralAllocationFailurePoint::BeforeOpaque);
	// Native SOL/TransList cutaways let the player see through foreground
	// walls in the isometric renderer. A 3D wall is always opaque, regardless
	// of that native view policy. Retain policy/provenance in the frame for
	// validation; keep raw texture/CLX coverage (doorways and grate holes).
	// Both disjoint native fence material passes draw opaque here.
	for (const auto *surfaces : { &CathedralFrame.triangles, &CathedralFrame.nativeArtPanels })
	for (const auto &surface : *surfaces)
		drawSurface(surface);
	for (const auto &special : CathedralFrame.nativeSpecialOverlays)
		drawSpecial(special);
    if(DoorModels.loaded)if(const auto *snapshot=cathedral::LiveSnapshot())for(const auto &door:snapshot->doors)DrawDoorModels(out,door);
	for (int i = 0; i < ActiveObjectCount; ++i) {
		const int slot = ActiveObjects[i];
		if (slot < 0 || slot >= MAXOBJECTS)
			continue;
		const Object &object = Objects[slot];
		if (!CathedralTileInFrame(object.position) || dLight[object.position.x][object.position.y] >= LightsMax
		    || !object._oAnimData || object._oAnimFrame == 0 || object._oAnimFrame > object._oAnimData->numSprites()
		    || object._otype == OBJ_L1LDOOR || object._otype == OBJ_L1RDOOR)
			continue; // Door frames/leaves already retain their native object binding.
		const bool selectable = object.canInteractWith() && std::abs(dObject[object.position.x][object.position.y]) == slot + 1;
        if(object._otype==OBJ_CHEST1 || object._otype==OBJ_CHEST2 || object._otype==OBJ_CHEST3 || object._otype==OBJ_TCHEST1 || object._otype==OBJ_TCHEST2 || object._otype==OBJ_TCHEST3) {
            if(DrawChestVisual(out,object,slot,selectable))continue;
        }
		if(object._otype==OBJ_BARREL || object._otype==OBJ_BARRELEX) {
			if(object.IsBroken() && DrawBrokenBarrelShards(out,object)) {
                // Retain only the active explosive sprite, never its old terminal debris image.
                if(object.isExplosive() && object._oAnimFrame>1 && object._oAnimFrame<object._oAnimLen)
                    DrawBarrelNativeBreakEffect(out,object,slot,false);
            } else if(!DrawBarrelVisual(out,object,slot,selectable)) DrawBarrelNativeBreakEffect(out,object,slot,selectable);
			continue;
		}
		if(object._otype==OBJ_SARC && DrawSarcophagusVisual(out,object,slot,selectable)) continue;
        if(object._otype==OBJ_SKFIRE && SkullFire.loaded){
            if(!IsTileVisible(object.position) || object._oDelFlag || std::abs(dObject[object.position.x][object.position.y])!=slot+1)continue;
            if(DrawSkullFireVisual(out,object)){
                DrawBillboard(out,{float(object.position.x)+-0.004782099F,0,float(object.position.y)+-0.005263244F},object.position,object.currentSprite(),PickKind::Ground,-1,0,false,.55F,SkullFireFlameHeight,false,true);
                ++SkullFire.flameDraws;continue;
            }
        }
		const bool nativeLightBody = CathedralNativeLightBodyAdmitted(slot);
		// Body, floor support and flame are one visual object. Native light may
		// reach an unseen cell; that alone must not reveal an orphan flame there.
		if (nativeLightBody && !IsTileVisible(object.position))
			continue;
        const bool ownBrazier=nativeLightBody && DrawBrazierVisual(out,object);
		if (nativeLightBody && !ownBrazier) {
			if(!CathedralNativeLightBody) throw std::invalid_argument("Native light body not prepared for draw");
			DrawVolume(out,{static_cast<float>(object.position.x),0,static_cast<float>(object.position.y)},object.position,
			    *CathedralNativeLightBody,PickKind::Ground,-1,dLight[object.position.x][object.position.y],false);
		}
		DrawBillboard(out, { static_cast<float>(object.position.x), 0, static_cast<float>(object.position.y) },
			object.position, object.currentSprite(), selectable ? PickKind::Object : PickKind::Ground,
			selectable ? slot : -1, object.applyLighting ? dLight[object.position.x][object.position.y] : 0, false,
			nativeLightBody ? 0.45F : 1.0F, nativeLightBody ? (ownBrazier ? BrazierFlameContact : CathedralNativeLightBody->mesh.height) : -1.0F);
        if(ownBrazier)++Brazier.flameDraws;
	}
	for (size_t i = 0; i < ActiveMonsterCount; ++i) {
		const size_t slot = ActiveMonsters[i];
		if (slot >= MaxMonsters)
			continue;
		const Monster &monster = Monsters[slot];
		const Point tile = monster.position.tile;
		if (!CathedralTileInFrame(tile) || !monster.animInfo.sprites || (monster.flags & MFLAG_HIDDEN) != 0
		    || (!IsTileLit(tile) && !(MyPlayer->_pInfraFlag && !TileHasAny(tile, TileProperties::Solid | TileProperties::BlockMissile))))
			continue;
		const ClxSprite sprite = monster.animInfo.currentSprite();
		Displacement offset = monster.getRenderingOffset(sprite);
		offset.deltaX += CalculateSpriteTileCenterX(sprite.width());
		Vec3 position { static_cast<float>(tile.x) + static_cast<float>(2 * offset.deltaY + offset.deltaX) / 64.0F,
			0, static_cast<float>(tile.y) + static_cast<float>(2 * offset.deltaY - offset.deltaX) / 64.0F };
		int light = dLight[tile.x][tile.y];
		if (!IsTileLit(tile) || (MyPlayer->_pInfraFlag && light > 8))
			CathedralTranslation = GetInfravisionTRN();
		else if (monster.mode == MonsterMode::Petrified)
			CathedralTranslation = GetStoneTRN();
		else if (monster.isUnique())
			CathedralTranslation = monster.uniqueMonsterTRN.get();
		const bool selectable = IsTileLit(tile) && !monster.hasNoLife() && !monster.isPlayerMinion()
		    && std::abs(dMonster[tile.x][tile.y]) == static_cast<int>(slot) + 1;
		DrawBillboard(out, position, tile, sprite, selectable ? PickKind::Monster : PickKind::Ground,
			selectable ? static_cast<int>(slot) : -1,
			CathedralTranslation != nullptr ? 0 : light);
		CathedralTranslation = nullptr;
	}
	for (uint8_t i = 0; i < ActiveItemCount; ++i) {
		const int slot = ActiveItems[i];
		if (slot < 0 || slot >= MAXITEMS)
			continue;
		const Item &item = Items[slot];
		if (!CathedralTileInFrame(item.position) || !item.AnimInfo.sprites)
			continue;
		const bool selectable = !item.isEmpty() && item.selectionRegion != SelectionRegion::None
		    && dItem[item.position.x][item.position.y] == slot + 1;
		DrawBillboard(out, { static_cast<float>(item.position.x), 0, static_cast<float>(item.position.y) },
			item.position, item.AnimInfo.currentSprite(), selectable ? PickKind::Item : PickKind::Ground,
			selectable ? slot : -1, dLight[item.position.x][item.position.y]);
	}
	for (size_t i = 0; i < Players.size(); ++i) {
		const Player &player = Players[i];
		const Point tile = player.position.tile;
		if (!player.plractive || !player.isOnActiveLevel() || !player.AnimInfo.sprites || !CathedralTileInFrame(tile)
		    || (&player != MyPlayer && !IsTileLit(tile) && !MyPlayer->_pInfraFlag))
			continue;
		if (&player == MyPlayer && !player.hasNoLife() && FollowCameraState.localPlayerHidden) continue;
		if (&player != MyPlayer && (!IsTileLit(tile) || (MyPlayer->_pInfraFlag && dLight[tile.x][tile.y] > 8)))
			CathedralTranslation = GetInfravisionTRN();
		const bool selectable = std::abs(dPlayer[tile.x][tile.y]) == static_cast<int>(i) + 1
		    || (player.hasNoLife() && TileContainsDeadPlayer(tile));
		const int shade = &player == MyPlayer || CathedralTranslation != nullptr ? 0 : std::max(static_cast<int>(dLight[tile.x][tile.y]) - 5, 0);
		if (!DrawWarriorNativePilot(out, player, i, selectable, shade, false))
			DrawBillboard(out, PlayerPosition(player), tile, player.currentSprite(), selectable ? PickKind::Player : PickKind::Ground,
				selectable ? static_cast<int>(i) : -1, shade);
		CathedralTranslation = nullptr;
	}
	for (const Missile &missile : Missiles) {
		const Point tile = missile.position.tileForRendering;
		if (!CathedralTileInFrame(tile) || !missile._miDrawFlag || missile._miDelFlag || !missile._miAnimData
		    || missile._miAnimFrame <= 0 || static_cast<size_t>(missile._miAnimFrame) > missile._miAnimData->numSprites())
			continue;
		const Displacement offset = missile.position.offsetForRendering;
		if (missile._miUniqTrans != 0) {
			if (missile._misource < 0 || missile._misource >= MaxMonsters || !Monsters[missile._misource].uniqueMonsterTRN)
				continue; // Invalid source is never dereferenced or painted as unlit.
			CathedralTranslation = Monsters[missile._misource].uniqueMonsterTRN.get();
		}
		DrawBillboard(out, { static_cast<float>(tile.x) + static_cast<float>(2 * offset.deltaY + offset.deltaX) / 64.0F,
			0, static_cast<float>(tile.y) + static_cast<float>(2 * offset.deltaY - offset.deltaX) / 64.0F },
			tile, (*missile._miAnimData)[missile._miAnimFrame - 1], PickKind::Ground, -1,
			CathedralTranslation == nullptr && missile._miLightFlag ? dLight[tile.x][tile.y] : 0);
		CathedralTranslation = nullptr;
	}
	for (int z = std::max(cathedral::ActiveMin, CathedralFrame.regionZ - cathedral::RegionSize);
	     z < std::min(cathedral::ActiveMax, CathedralFrame.regionZ + 2 * cathedral::RegionSize); ++z) {
		for (int x = std::max(cathedral::ActiveMin, CathedralFrame.regionX - cathedral::RegionSize);
		     x < std::min(cathedral::ActiveMax, CathedralFrame.regionX + 2 * cathedral::RegionSize); ++x) {
			const uint8_t value = static_cast<uint8_t>(dCorpse[x][z]);
			const unsigned slot = value & 0x1F;
			if (slot == 0 || slot > MaxCorpses || dLight[x][z] >= LightsMax || !Corpses[slot - 1].sprites)
				continue;
			const Corpse &corpse = Corpses[slot - 1];
			const auto sprites = corpse.spritesForDirection(static_cast<Direction>((value >> 5) & 7));
			if (corpse.frame < 0 || static_cast<uint32_t>(corpse.frame) >= sprites.numSprites())
				continue;
			if (corpse.translationPaletteIndex > 0 && corpse.translationPaletteIndex <= MaxMonsters)
				CathedralTranslation = Monsters[corpse.translationPaletteIndex - 1].uniqueMonsterTRN.get();
			DrawBillboard(out, { static_cast<float>(x), 0, static_cast<float>(z) }, { x, z },
				sprites[corpse.frame], PickKind::Ground, -1, CathedralTranslation != nullptr ? 0 : dLight[x][z],false,1.0F,-1.0F,true);
			CathedralTranslation = nullptr;
		}
	}
	InjectCathedralAllocationFailure(TownViewCathedralAllocationFailurePoint::AfterOpaque);
	// Architecture and structural CLX overlays have already written opaque
	// depth/picking above; do not submit them again as palette blends.
	CathedralPaletteBlend = false;
	DrawingCathedral = false;
}

Surface PrepareSamplingBuffers(const Surface &logical)
{
	SamplingState = { *GetOptions().Graphics.townViewAntialiasing, 1, logical.w(), logical.h(), false };
	RasterSampleFactor = 1;
	const size_t logicalPixels = static_cast<size_t>(logical.w()) * logical.h();
	// Bound the extra pixel/depth/ownership storage and avoid integer/pitch overflow.
	if (SamplingState.requested && logical.w() <= 8192 && logical.h() <= 8192
	    && logicalPixels <= MaxSmoothWorldPixels / 4) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		try {
#endif
			const int width = logical.w() * 2;
			const int height = logical.h() * 2;
			if (!SamplingSurface || SamplingSurface->w() != width || SamplingSurface->h() != height) {
#ifdef USE_SDL3
				SDLSurfaceUniquePtr allocation { SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8) };
#else
				SDLSurfaceUniquePtr allocation { SDL_CreateRGBSurfaceWithFormat(0, width, height, 8, SDL_PIXELFORMAT_INDEX8) };
#endif
				if (allocation)
					SamplingSurface = std::make_unique<OwnedSurface>(std::move(allocation));
				else
					SamplingSurface.reset();
			}
			if (SamplingSurface) {
				DepthBuffer.assign(logicalPixels * 4, std::numeric_limits<float>::infinity());
				PickBuffer.assign(logicalPixels * 4, PickRecord {});
				SamplingColors.Prepare(logical_palette);
				SamplingState.factor = RasterSampleFactor = 2;
				SamplingState.width = width;
				SamplingState.height = height;
				return *SamplingSurface;
			}
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		} catch (const std::bad_alloc &) {
			// The ordinary path remains usable if the optional allocation fails.
			SamplingSurface.reset();
			std::vector<float>().swap(DepthBuffer);
			std::vector<PickRecord>().swap(PickBuffer);
		}
#endif
	}
	SamplingState.limited = SamplingState.requested;
	DepthBuffer.assign(logicalPixels, std::numeric_limits<float>::infinity());
	PickBuffer.assign(logicalPixels, PickRecord {});
	return logical;
}

void ResolveSampling(const Surface &logical, const Surface &sampled)
{
	if (RasterSampleFactor == 1)
		return;
	for (int y = 0; y < logical.h(); ++y) {
		uint8_t *destination = logical.at(0, y);
		const uint8_t *top = sampled.at(0, y * 2);
		const uint8_t *bottom = sampled.at(0, y * 2 + 1);
		for (int x = 0; x < logical.w(); ++x) {
			const int sx = x * 2;
			destination[x] = SamplingColors.Resolve({ top[sx], top[sx + 1], bottom[sx], bottom[sx + 1] });
			const size_t source = static_cast<size_t>(y * 2) * sampled.w() + sx;
			const std::array<size_t, 4> indices { source, source + 1, source + sampled.w(), source + sampled.w() + 1 };
			size_t front = source;
			for (const size_t index : indices) {
				if (DepthBuffer[index] < DepthBuffer[front])
					front = index;
			}
			const size_t target = static_cast<size_t>(y) * logical.w() + x;
			// Forward in-place resolution is safe: every source index is >= target.
			// One visible subpixel supplies all ownership and depth information.
			DepthBuffer[target] = DepthBuffer[front];
			PickBuffer[target] = PickBuffer[front];
		}
	}
	const size_t size = static_cast<size_t>(logical.w()) * logical.h();
	DepthBuffer.resize(size);
	PickBuffer.resize(size);
	RasterSampleFactor = 1;
}

bool CurrentPickingValid()
{
	return PickingValid && SamplingState.requested == *GetOptions().Graphics.townViewAntialiasing
	    && PickCameraRevision == CameraRig.revision()
	    && PickFollowProjectionRevision == FollowProjectionRevision
	    && PickCameraVisibilityRevision == CameraVisibilityRevision
	    && PickHorizonEnabled == *GetOptions().Graphics.townViewHorizon
	    && PickGpuRequested == EffectiveGpuRequested()
	    && PickFrustumCullingRequested == *GetOptions().Graphics.townViewFrustumCulling
	    && PickSceneRevision == ViewSceneRevision()
	    && (!cathedral::LiveLevelEligible() || (CathedralFrameReady && !CathedralFrame.requiresNativeFallback
	        && (!PickGpuRequested || RendererState.usedGpu) && cathedral::LiveFrameCurrent()))
	    && PickScreenWidth == gnScreenWidth && PickScreenHeight == gnScreenHeight
	    && PickViewportHeight == gnViewportHeight && PickZoom == *GetOptions().Graphics.zoom
	    && PickLeftPanel == IsLeftPanelOpen() && PickRightPanel == IsRightPanelOpen()
	    && ViewCamera.width > 0 && ViewCamera.height > 0
	    && DepthBuffer.size() == static_cast<size_t>(ViewCamera.width) * ViewCamera.height
	    && PickBuffer.size() == DepthBuffer.size();
}

} // namespace

TownViewWarriorPilotState GetTownViewWarriorPilotState()
{
	return WarriorPilotState;
}

void SetTownViewWarriorViewModelEnabledForDiagnostics(bool enabled)
{
	WarriorViewModelEnabledForDiagnostics = enabled;
}

const PlayerActorNativePilotFrame *GetTownViewWarriorPilotFrameForDiagnostics(size_t slot)
{
	return slot < WarriorPilot.frames.size() && WarriorPilot.frames[slot] ? &*WarriorPilot.frames[slot] : nullptr;
}

TownArchitectureBounds BuildTownArchitectureBounds(const TownSceneModel &model)
{
	const float infinity = std::numeric_limits<float>::infinity();
	TownArchitectureBounds bounds { { infinity, infinity, infinity }, { -infinity, -infinity, -infinity }, false };
	bool finite = true;
	const auto include = [&](const std::vector<TownSceneTriangle> &triangles) {
		for (const auto &triangle : triangles) {
			for (const auto &vertex : triangle.vertices) {
				finite &= std::isfinite(vertex.x) && std::isfinite(vertex.height) && std::isfinite(vertex.z);
				bounds.minimum.x = std::min(bounds.minimum.x, vertex.x);
				bounds.minimum.height = std::min(bounds.minimum.height, vertex.height);
				bounds.minimum.z = std::min(bounds.minimum.z, vertex.z);
				bounds.maximum.x = std::max(bounds.maximum.x, vertex.x);
				bounds.maximum.height = std::max(bounds.maximum.height, vertex.height);
				bounds.maximum.z = std::max(bounds.maximum.z, vertex.z);
				bounds.valid = true;
			}
		}
	};
	include(model.externalModel && model.importedTexture ? TownSceneExteriorTriangles(model) : model.triangles);
	if (model.cabinInterior) {
		include(model.cabinInterior->interiorTriangles);
		// Include every source even when fire is disabled or the light budget is
		// smaller. Visual flicker changes intensity, never these coordinates.
		for (const auto &fire : model.cabinInterior->fireSources)
			include(fire.emissiveTriangles);
	}
	bounds.valid &= finite;
	return bounds;
}

bool IsTownArchitectureBoundsVisible(const TownCameraFrame &frame, const TownArchitectureBounds &bounds)
{
	const auto finite = [](TownCameraPoint p) { return std::isfinite(p.x) && std::isfinite(p.height) && std::isfinite(p.z); };
	if (!bounds.valid || !finite(bounds.minimum) || !finite(bounds.maximum)
	    || bounds.minimum.x > bounds.maximum.x || bounds.minimum.height > bounds.maximum.height || bounds.minimum.z > bounds.maximum.z
	    || !frame.valid || frame.width <= 0 || frame.height <= 0 || !finite(frame.eye)
	    || !finite(frame.right) || !finite(frame.up) || !finite(frame.forward)
	    || !std::isfinite(frame.heightScale) || frame.heightScale <= 0
	    || !std::isfinite(frame.focalPixels) || frame.focalPixels <= 0
	    || !std::isfinite(frame.centerX) || !std::isfinite(frame.centerY)
	    || !std::isfinite(frame.nearClip) || !std::isfinite(frame.farClip) || frame.nearClip < 0 || frame.farClip <= frame.nearClip)
		return true;
	std::array<bool, 6> outside { true, true, true, true, true, true };
	// A small world margin and one logical pixel guard retain grazing walls.
	// These six affine half-spaces match ClipTownCameraTriangle, before divide;
	// all corners outside one plane is the only rejection criterion.
	constexpr float WorldMargin = 0.01F;
	for (const float x : { bounds.minimum.x - WorldMargin, bounds.maximum.x + WorldMargin }) {
		for (const float height : { bounds.minimum.height - WorldMargin, bounds.maximum.height + WorldMargin }) {
			for (const float z : { bounds.minimum.z - WorldMargin, bounds.maximum.z + WorldMargin }) {
				const TownCameraPoint point = TownCameraToView(frame, { x, height, z });
				if (!finite(point))
					return true;
				const double w = frame.perspective ? point.z : 1;
				const std::array<double, 6> distances {
					point.z - static_cast<double>(frame.nearClip), static_cast<double>(frame.farClip) - point.z,
					(frame.centerX + 1.0) * w + static_cast<double>(frame.focalPixels) * point.x,
					(frame.width - static_cast<double>(frame.centerX) + 1.0) * w - static_cast<double>(frame.focalPixels) * point.x,
					(frame.centerY + 1.0) * w - static_cast<double>(frame.focalPixels) * point.height,
					(frame.height - static_cast<double>(frame.centerY) + 1.0) * w + static_cast<double>(frame.focalPixels) * point.height
				};
				for (size_t plane = 0; plane < outside.size(); ++plane) {
					if (!std::isfinite(distances[plane]))
						return true;
					outside[plane] &= distances[plane] < -0.0001;
				}
			}
		}
	}
	return std::none_of(outside.begin(), outside.end(), [](bool value) { return value; });
}

TownViewArchitectureCullingState GetTownViewArchitectureCullingState()
{
	return ArchitectureCullingState;
}

bool IsTownViewActive()
{
	return Enabled && (leveltype == DTYPE_TOWN
	    || (cathedral::LiveLevelEligible() && CathedralFrameReady && !CathedralFrame.requiresNativeFallback));
}

bool IsTownViewEnabled() { return Enabled; }

void PrepareTownViewLiveFrame()
{
	CathedralDiagnosticStage = CathedralFallbackStage::Context;
	CathedralPreparationRefused = CathedralDiagnosticGpuAttempted = CathedralDiagnosticForceGeometry = false;
	CathedralDiagnosticMaterialBindingValid = false;
	if (!cathedral::LiveLevelEligible() || MyPlayer == nullptr) {
		if (leveltype == DTYPE_CATHEDRAL || CathedralFrameReady) {
			RecordCathedralDiagnostic(MyPlayer == nullptr ? CathedralFallbackReason::PlayerUnavailable : CathedralFallbackReason::ContextIneligible,
				CathedralDiagnosticStage, Enabled, EffectiveGpuRequested());
			CathedralPreparationRefused = true;
		}
		if (CathedralFrameReady && leveltype != DTYPE_TOWN)
			InvalidateTownViewFrameForNativeFallback();
		CathedralFrameReady = false;
		CathedralFrame = {};
		if (CathedralPreparationRefused) TryLogCathedralDiagnostic();
		return;
	}
	PickingValid = false;
	CathedralFrameReady = false;
	CathedralAllocationState.failed = false;
	CathedralAllocationState.injected = TownViewCathedralAllocationFailurePoint::None;
	CathedralAllocationState.gpuSubmittedBeforeFailure = 0;
	CathedralAllocationState.gpuDrawCallsBeforeFailure = 0;
	CathedralAllocationState.cpuRasterizedBeforeFailure = 0;
	const bool diagnosticRequestedGpu = EffectiveGpuRequested();
	try {
		PrepareSarcophagusVisual();
		PrepareBarrelVisual();
        PrepareBrazierVisual();
        PrepareSkullFireVisual();
        PrepareChestVisual();
        PrepareDoorVisual();
		const Point focus = MyPlayer->position.tile;
		CathedralDiagnosticStage = CathedralFallbackStage::RefreshLiveFrame;
		if (cathedral::RefreshLiveFrame(focus.x, focus.y)) {
			CathedralDiagnosticStage = CathedralFallbackStage::LiveBindings;
			const auto *snapshot = cathedral::LiveSnapshot();
			const auto *scene = cathedral::LiveScene();
			if (snapshot != nullptr && scene != nullptr) {
                PrepareDoorVisualStates(*snapshot);
				CathedralDiagnosticStage = CathedralFallbackStage::BuildPilotFrame;
				CathedralFrame = cathedral::BuildPilotFrame(*snapshot, *scene, focus.x, focus.y);
				CathedralFrameReady = true;
				for (const auto &special : CathedralFrame.nativeSpecialOverlays) {
					if (!pSpecialCels || special.frameIndex < 0 || static_cast<uint32_t>(special.frameIndex) >= pSpecialCels->numSprites()) {
						CathedralFrame.requiresNativeFallback = true;
						CathedralFrame.fallbackReasons |= static_cast<uint32_t>(cathedral::FrameFallbackReason::UnsupportedNativeSpecial);
					}
				}
				if (!CathedralFrame.requiresNativeFallback) {
					CathedralDiagnosticStage = CathedralFallbackStage::NativeMaterials;
#if !defined(DUN_RENDER_STATS) && !defined(DEBUG_STR) && !defined(DEBUG_RENDER_OFFSET_X) && !defined(DEBUG_RENDER_OFFSET_Y)
					// General native data rules, regenerated for this live frame/epoch.
					// No receipt or seed/cell whitelist is needed in gameplay.
					const auto fenceMegas = CaptureCathedralKnownFenceMegas(*snapshot, CathedralFrame);
					const auto fenceUpdates = cathedral::native_fence_runtime::BuildKnownNativeFencePanels(
					    *snapshot, CathedralFrame, MicroTileLen, fenceMegas);
					if (fenceUpdates.requiresCompleteNativeFallback)
						throw std::invalid_argument("Cathedral known fence needs unsupported native mask/blend operator");
					cathedral::native_fence_runtime::ApplyFencePanels(CathedralFrame, fenceUpdates);
					const auto lightObjects = CaptureCathedralNativeLightObjects(*snapshot);
					const auto lightUpdates = cathedral::native_visual_policy::BuildNativeLightSuppression(
					    *snapshot, CathedralFrame, lightObjects);
					cathedral::native_visual_policy::ApplySuppression(CathedralFrame, lightUpdates);
					PrepareCathedralLightGroundPanels(CathedralFrame);
#endif
#if defined(D3D_CATHEDRAL_FULL_WALL_EXPERIMENT) && !defined(DUN_RENDER_STATS) && !defined(DEBUG_STR) \
    && !defined(DEBUG_RENDER_OFFSET_X) && !defined(DEBUG_RENDER_OFFSET_Y)
					// Separate experiment: full bytes include base, but native
					// ground/depth fit is not established by the measured plane.
					const auto wallUpdates = cathedral::native_wall_runtime::BuildUpdates(*snapshot, CathedralFrame, MicroTileLen);
					cathedral::native_wall_runtime::ApplyUpdates(CathedralFrame, wallUpdates);
#endif
					PrepareCathedralNativeTextures(CathedralFrame);
				} else {
					RecordCathedralDiagnostic(CathedralFallbackReason::PilotFrameFallback, CathedralDiagnosticStage, true, diagnosticRequestedGpu);
					CathedralPreparationRefused = true;
				}
			} else {
				RecordCathedralDiagnostic(CathedralFallbackReason::LiveBindingsUnavailable, CathedralDiagnosticStage, true, diagnosticRequestedGpu);
				CathedralPreparationRefused = true;
			}
		} else {
			// Refresh clears its internals on refusal. Do not invent an origin.
			RecordCathedralDiagnostic(CathedralFallbackReason::RefreshLiveFrameRefused, CathedralDiagnosticStage, true, diagnosticRequestedGpu);
			CathedralPreparationRefused = true;
		}
	} catch (const std::bad_alloc &) {
		CathedralAllocationState.failed = true;
		RecordCathedralDiagnostic(CathedralFallbackReason::PreparationAllocationFailure, CathedralDiagnosticStage, true, diagnosticRequestedGpu);
		CathedralPreparationRefused = true;
		CathedralFrameReady = false;
		CathedralFrame = {};
	} catch (const std::length_error &) {
		// Only the explicit cache guard above identifies a budget refusal.
		if (!CathedralPreparationRefused)
			RecordCathedralDiagnostic(CathedralFallbackReason::PreparationLengthError, CathedralDiagnosticStage, true, diagnosticRequestedGpu);
		CathedralPreparationRefused = true;
		CathedralFrameReady = false;
		CathedralFrame = {};
	} catch (const std::invalid_argument &) {
		RecordCathedralDiagnostic(CathedralDiagnosticStage == CathedralFallbackStage::NativeMaterials
			? CathedralFallbackReason::NativeMaterialValidationRefused : CathedralFallbackReason::PreparationException,
			CathedralDiagnosticStage, true, diagnosticRequestedGpu);
		CathedralPreparationRefused = true;
		CathedralFrameReady = false;
		CathedralFrame = {};
	} catch (...) {
		RecordCathedralDiagnostic(CathedralFallbackReason::PreparationException, CathedralDiagnosticStage, true, diagnosticRequestedGpu);
		CathedralPreparationRefused = true;
		CathedralFrameReady = false;
		CathedralFrame = {};
	}
	if (!CathedralFrameReady || CathedralFrame.requiresNativeFallback) {
		SamplingSurface.reset();
		SamplingState = {};
		DepthBuffer.clear();
		PickBuffer.clear();
		ResetFollowCameraHistory(true);
		CameraRig.Suspend(true);
		RendererState = {};
		RendererState.requestedGpu = EffectiveGpuRequested();
		try {
			RendererState.failure = "Private Cathedral pilot R2: incomplete frame or unresolved native surface; complete native fallback";
		} catch (const std::bad_alloc &) {
			CathedralAllocationState.failed = true;
			RendererState.failure.clear();
		}
		TryLogCathedralDiagnostic();
	} else {
		// A prepared frame is not proof that the draw/backend recovered.
		CameraRig.Suspend(!Enabled);
	}
}

TownViewCathedralFallbackDiagnostic GetTownViewCathedralFallbackDiagnostic() noexcept
{
	return CathedralFallbackDiagnostic;
}

TownViewCathedralFirstRefusal GetTownViewCathedralFirstRefusal() noexcept
{
	return CathedralFirstRefusal;
}

void InvalidateTownViewFrameForNativeFallback()
{
	PickingValid = false;
	CathedralFrameReady = false;
	SamplingSurface.reset();
	SamplingState = {};
	DepthBuffer.clear();
	PickBuffer.clear();
	EndTownViewCameraDrag();
	ResetFollowCameraHistory(true);
	CameraRig.Suspend(true);
}

TownViewCathedralState GetTownViewCathedralState()
{
	return { cathedral::LiveLevelEligible(), CathedralFrameReady,
		cathedral::LiveLevelEligible() && (!CathedralFrameReady || CathedralFrame.requiresNativeFallback),
		cathedral::LiveEpoch(), cathedral::LiveSceneRevision(), CathedralFrame.regions, CathedralFrame.triangles.size() };
}

TownViewCathedralNativeTextureState GetTownViewCathedralNativeTextureState()
{
	return CathedralNativeTextureState;
}

TownViewCathedralNativeTextureReference GetTownViewCathedralNativeTextureReference(
	const cathedral::NativeTextureBinding &binding)
{
	TownViewCathedralNativeTextureReference reference;
	if (!CathedralNativeOperationBindingSupported(binding)) return reference;
	if (CathedralNativeTextureState.epoch == 0 || CathedralNativeTextureState.epoch != cathedral::LiveEpoch()
	    || CathedralNativeTextureResource != pDungeonCels.get()) return reference;
	const auto found = CathedralNativeTextureCache.find(NativeTextureKey(binding));
	if (found == CathedralNativeTextureCache.end()) return reference;
	const auto &entry = found->second;
	reference.width = entry.texture.width; reference.height = entry.texture.height;
	reference.pixels = entry.texture.pixels; reference.opacity = entry.texture.opacity;
	reference.gpuIdentity = entry.texture.gpuIdentity.value; reference.epoch = CathedralNativeTextureState.epoch;
	reference.rawSignature = entry.signature; // Raw getter stays unpartitioned for every requested pass.
	reference.authoredMaterial = entry.authoredMaterial;
	reference.authoredPixelHash = entry.authoredPixelHash;
	reference.sourceX = binding.x; reference.sourceZ = binding.z; reference.nativeSlot = binding.nativeSlot;
	reference.requestedPiece = binding.piece; reference.sourcePiece = entry.sourcePiece;
	reference.sourceMicro = entry.sourceMicro; reference.sourceColumn = entry.sourceColumn;
	reference.sourceBlock = entry.sourceBlock; reference.secondBlock = entry.secondBlock;
	reference.cropX = entry.cropX; reference.cropY = entry.cropY;
	reference.repeat = entry.texture.repeat; reference.approximate = binding.approximate || entry.approximate;
	reference.horizontalFlip = entry.horizontalFlip;
	reference.originalUpperColumn = binding.layout == cathedral::NativeTextureLayout::OriginalUpperColumn;
	reference.originalFullColumn = binding.layout == cathedral::NativeTextureLayout::OriginalFullColumn;
	if (reference.originalUpperColumn || reference.originalFullColumn) {
		reference.activeMicroTileLength = static_cast<uint8_t>(CathedralNativeTextureMicroLen);
		reference.packingRevision = reference.originalFullColumn
		    ? cathedral::NativeFullColumnPackingRevision : cathedral::NativeUpperColumnPackingRevision;
		reference.upperWords = entry.upperWords;
		reference.sourceWords = entry.upperWords;
	}
	return reference;
}

TownViewCathedralNativeTextureReference GetTownViewCathedralCeilingTextureReference()
{
	if(!CathedralCeilingBindingReady) return {};
	return GetTownViewCathedralNativeTextureReference(CathedralCeilingBinding);
}

TownViewCathedralNativeTextureReference GetTownViewCathedralLightBodyTextureReference()
{
	if (!CathedralNativeLightBody || CathedralNativeTextureState.epoch == 0
	    || CathedralNativeTextureState.epoch != cathedral::LiveEpoch()
	    || CathedralNativeTextureResource != pDungeonCels.get())
		return {};
	TownViewCathedralNativeTextureReference result;
	const auto &texture = CathedralNativeLightBody->texture;
	result.width = texture.width; result.height = texture.height;
	result.pixels = texture.pixels; result.opacity = texture.opacity;
	result.gpuIdentity = texture.gpuIdentity.value; result.epoch = CathedralNativeTextureState.epoch;
	result.requestedPiece = result.sourcePiece = 269; result.approximate = true;
	return result;
}

TownViewCathedralNativeTextureReference GetTownViewCathedralLitTextureReference(
    const cathedral::NativeTextureBinding &binding)
{
	if (!CathedralNativeOperationBindingSupported(binding) || binding.kind != cathedral::NativeTextureKind::Masonry || !CathedralFrameReady
	    || !CathedralNativeTexturesCurrent(CathedralFrame)) return {};
	const auto found = CathedralNativeLitTextureCache.find(NativeLitTextureKey(binding));
	if (found == CathedralNativeLitTextureCache.end()) return {};
	auto reference = GetTownViewCathedralNativeTextureReference(binding);
	if (reference.width == 0) return {};
	reference.pixels = found->second.texture.pixels;
	reference.opacity = found->second.texture.opacity;
	reference.gpuIdentity = found->second.texture.gpuIdentity.value;
	reference.lightingBaked = true;
	reference.perPixelLighting = *GetOptions().Graphics.perPixelLighting;
	reference.lightSignature = found->second.lightSignature;
	reference.operationPass = static_cast<uint8_t>(binding.operationPass);
	reference.nativeOperationsPartitioned = binding.operationPass != cathedral::NativeTextureOperationPass::Unpartitioned;
	reference.operationRevision = reference.nativeOperationsPartitioned ? cathedral::native_operators::OperationRevision : 0;
	return reference;
}

bool CopyTownViewCathedralFrameForDiagnostics(cathedral::PilotFrame &out)
{
	if (!CathedralFrameReady || CathedralFrame.requiresNativeFallback || !cathedral::LiveFrameCurrent()
	    || !CathedralNativeTexturesCurrent(CathedralFrame)) return false;
	auto copy = CathedralFrame; // Actual prepared production frame, including emitted operation passes.
	static_assert(noexcept(out = std::move(copy)));
	out = std::move(copy);
	return true; // Preparation/linkage witness; caller still proves a successful native/CPU/GPU draw.
}

TownViewCathedralCellMaterialDiagnostic GetTownViewCathedralCellMaterialDiagnostic(int sourceX, int sourceZ)
{
	TownViewCathedralCellMaterialDiagnostic result;
	result.sourceX = sourceX; result.sourceZ = sourceZ;
	const auto *snapshot = cathedral::LiveSnapshot();
	if (!CathedralFrameReady || !cathedral::LiveFrameCurrent() || snapshot == nullptr
	    || sourceX < snapshot->minX || sourceX >= snapshot->maxX
	    || sourceZ < snapshot->minZ || sourceZ >= snapshot->maxZ
	    || snapshot->gameRevision != CathedralFrame.gameRevision || MicroTileLen != 10)
		return result;
	const auto &cell = snapshot->At(sourceX, sourceZ);
	if (cell.piece >= snapshot->micros.size()) return result;
	result.current = true; result.epoch = snapshot->gameRevision;
	result.piece = cell.piece; result.sol = cell.properties; result.light = cell.light;
	result.transparencyGroup = cell.transparency; result.signedObjectOccupancy = cell.object;
	result.sourceWords = snapshot->micros[cell.piece];
	result.nativeIsFloor = (cell.properties & (cathedral::wall_composition::SolSolid | cathedral::wall_composition::SolBlockMissile)) == 0;
	result.nativeWallTransparency = (cell.properties & cathedral::wall_composition::SolTransparent) != 0
	    && snapshot->activeTransparency[cell.transparency];
	result.hasBaseWords = result.sourceWords[0] != 0 || result.sourceWords[1] != 0;
	for (unsigned micro = 2; micro < 10; ++micro) result.hasUpperWords |= result.sourceWords[micro] != 0;
	result.rawMega = snapshot->mega[static_cast<size_t>((sourceZ - snapshot->minZ) / 2) * cathedral::MegaSize
	    + (sourceX - snapshot->minX) / 2];
	result.namedMegaFamily = static_cast<uint8_t>(cathedral::native_cell_art::ReadNamedNativeMegaFamily(result.rawMega));
	const int slot = std::abs(static_cast<int>(cell.object)) - 1;
	if (slot >= 0 && slot < MAXOBJECTS) {
		bool active = false;
		for (int index = 0; index < ActiveObjectCount && index < MAXOBJECTS; ++index)
			active |= ActiveObjects[index] == slot;
		const Object &object = Objects[slot];
		if (active && !object._oDelFlag && object.position == Point { sourceX, sourceZ }
		    && dObject[sourceX][sourceZ] == cell.object && std::abs(static_cast<int>(cell.object)) == slot + 1) {
			result.nativeObjectActive = true; result.objectSlot = slot; result.objectType = object._otype;
			result.nativeLightId = object._olid; result.objectAppliesLighting = object.applyLighting;
			result.objectAnimationFrame = object._oAnimFrame;
			result.nativeLightPieceAndObjectRule = cell.piece == 269 && object._otype == OBJ_L1LIGHT;
		}
	}
	for (const auto &surface : CathedralFrame.nativeSurfaces) {
		if (std::none_of(surface.sources.begin(), surface.sources.end(), [&](const auto &source) {
			    return source.x == sourceX && source.z == sourceZ && source.piece == cell.piece;
		    })) continue;
		for (const auto &triangle : CathedralFrame.triangles) {
			if (triangle.instanceId != surface.instanceId) continue;
			if (triangle.visualSuppressed) { ++result.suppressedTechnicalTriangles; continue; }
			if (triangle.nativeTexture.kind == cathedral::NativeTextureKind::Technical) ++result.technicalTriangles;
			else ++result.boundTriangles;
		}
	}
	for (const auto &panel : CathedralFrame.nativeArtPanels)
		if (!panel.visualSuppressed && panel.nativeTexture.x == sourceX && panel.nativeTexture.z == sourceZ
		    && panel.nativeTexture.piece == cell.piece) ++result.nativePanelTriangles;
	// Prepared texel counters are per matching cached reference, not screen
	// visibility. They do not reinterpret palette index0 as missing coverage.
	if (CathedralNativeTextureState.epoch == result.epoch && CathedralNativeTextureResource == pDungeonCels.get()
	    && CathedralNativeTextureMicroLen == MicroTileLen)
		for (const auto &[key, entry] : CathedralNativeTextureCache) {
			if (std::get<1>(key) != cell.piece) continue;
			++result.preparedReferences;
			for (size_t pixel = 0; pixel < entry.texture.opacity.size(); ++pixel) {
				if (entry.texture.opacity[pixel] == 0) continue;
				++result.coveredTexels;
				result.coveredIndex0 += entry.texture.pixels[pixel] == 0;
				result.coveredIndex255 += entry.texture.pixels[pixel] == 255;
			}
		}
	return result;
}

namespace {
bool DiagnosticGpuPreflightAttempted = false;
diag::Failure DiagnosticGpuPreflightFailure;
bool DiagnosticEligibleFrame = false;

bool RequiredGpu() { return diag::SessionPolicy().mode == diag::Mode::GpuRequired; }
diag::Failure PrepareDiagnosticGpuSession()
{
	if (DiagnosticGpuPreflightAttempted) {
		const auto logging = diag::OpenSession();
		return logging.error != diag::Error::None ? logging : DiagnosticGpuPreflightFailure;
	}
	DiagnosticGpuPreflightAttempted = true;
	// Visible progress and flushed enter stages precede shader/device work.
#ifndef USE_SDL1
	const std::string title = ghMainWnd != nullptr ? SDL_GetWindowTitle(ghMainWnd) : "";
	if (ghMainWnd != nullptr) SDL_SetWindowTitle(ghMainWnd, "Diablo3D: verificando GPU, shaders e targets");
	struct RestoreTitle { const std::string &title; ~RestoreTitle() { if (ghMainWnd != nullptr) SDL_SetWindowTitle(ghMainWnd, title.c_str()); } } restore { title };
#endif
	DiagnosticGpuPreflightFailure = diag::OpenSession();
	if (DiagnosticGpuPreflightFailure.error != diag::Error::None) return DiagnosticGpuPreflightFailure;
	diag::StageScope stage(diag::SessionLogger(), diag::Stage::Device3D, 0);
	if (!RequiredGpu()) { diag::SessionLogger().RecordBackend(diag::Backend::CpuDiagnostic, 0, 0, 0, gnScreenWidth, gnViewportHeight); return {}; }
	if (!*GetOptions().Graphics.townViewGpuRendering) return DiagnosticGpuPreflightFailure = { diag::Error::GpuPreferenceDisabled, diag::Stage::WorldPrepare };
	diag::Preflight preflight;
#ifdef USE_SDL3
	preflight.renderer2DReady = renderer != nullptr;
	const char *name = renderer != nullptr ? SDL_GetRendererName(renderer) : nullptr;
	preflight.renderer2DAccelerated = name != nullptr && (std::strcmp(name,"direct3d11")==0 || std::strcmp(name,"direct3d12")==0 || std::strcmp(name,"opengl")==0 || std::strcmp(name,"opengles2")==0 || std::strcmp(name,"vulkan")==0 || std::strcmp(name,"metal")==0);
#elif !defined(USE_SDL1)
	SDL_RendererInfo info {};
	preflight.renderer2DReady = renderer != nullptr && SDL_GetRendererInfo(renderer, &info) == 0;
	preflight.renderer2DAccelerated = preflight.renderer2DReady && (info.flags & SDL_RENDERER_ACCELERATED) != 0;
#endif
	diag::SessionLogger().RecordBackend(preflight.renderer2DReady && preflight.renderer2DAccelerated ? diag::Backend::SdlHardware : diag::Backend::SdlSoftware, 0, 0, 0, gnScreenWidth, gnViewportHeight);
	if (preflight.renderer2DReady && preflight.renderer2DAccelerated) {
		const bool ready = TownGpuBeginFrame(gnScreenWidth, gnViewportHeight, false);
		const auto &gpu = GetTownGpuStatus();
		preflight.device3DReady = ready && gpu.available && gpu.featureLevel >= 0xB000;
		preflight.device3DWarp = gpu.warp;
		diag::SessionLogger().RecordBackend(gpu.warp ? diag::Backend::Direct3D11Warp : preflight.device3DReady ? diag::Backend::Direct3D11Hardware : diag::Backend::Unknown, gpu.vendorId, gpu.deviceId, gpu.featureLevel, gnScreenWidth, gnViewportHeight);
		TownGpuDiscardFrame(); // No geometry, End, readback or extra gameplay frame.
	}
	DiagnosticGpuPreflightFailure = diag::CheckPreflight(diag::SessionPolicy(), preflight);
	if (DiagnosticGpuPreflightFailure.error != diag::Error::None) diag::RecordSessionFailure(DiagnosticGpuPreflightFailure);
	return DiagnosticGpuPreflightFailure;
}
bool RefuseRequiredGpuFrame(const Surface &fullOut, diag::Failure failure) noexcept
{
	// Preserve eligibility; clearing CathedralFrameReady here would let the next
	// frame masquerade as an unsupported context and enter native fallback.
	diag::RecordSessionFailure(failure);
	TownGpuDiscardFrame();
	CaptureGpu = false; CaptureGpuFailed = true; PickingValid = false;
	RendererState.usedGpu = false; // Refusal owns no published GPU world.
	DepthBuffer.clear(); PickBuffer.clear(); GpuFrame = {}; GpuPickRecords.clear();
	WarriorViewModelCoverage.clear(); WarriorFirstPerson = {};
	SmoothWorldValid = false; SmoothWorldPixels.clear(); SmoothWorldSurface.reset();
	SamplingSurface.reset(); SamplingState = {}; ItemLabelAnchorCount = 0;
	ClearCameraVisibilityFrame(); FollowCameraState.valid = false;
	const auto error = failure.error == diag::Error::None ? diag::Error::CpuFallbackRefused : failure.error;
	const Surface errorOut = fullOut.subregionY(0, std::min<int>(gnViewportHeight, fullOut.h()));
	ClearSurface(errorOut);
	try {
		RendererState.failure = diag::PublicErrorMessage(error);
		DrawString(errorOut, diag::PublicErrorMessage(error), Point { 16, 16 });
		DrawString(errorOut, "F4: modo classico. Diagnostico CPU exige reiniciar com --cpu-render-diagnostic.", Point { 16, 64 });
		const auto first = diag::SessionFirstFailure();
		if (first.nativeCodeKnown) DrawString(errorOut, "HRESULT: " + std::to_string(first.nativeCode), Point { 16, 104 });
	} catch (const std::bad_alloc &) { } // Cleared world is still refused; never CPU redraw.
	diag::Frame frame; frame.sequence = diag::SessionFrame(); frame.requestedGpu = RendererState.requestedGpu;
	frame.cpuRasterizedTriangles = RendererState.cpuRasterizedTriangles;
	diag::RecordSessionFrame(frame, diag::Decision::RefuseFrame);
	return true; // Own the error-only surface; DrawGame must not draw a native world.
}
} // namespace

void InitializeTownViewForGame()
{
	CathedralFirstRefusal = {};
	CathedralFirstRefusalLogged = CathedralFirstRefusalLogAttempted = false;
	CathedralFirstRefusalLastAttempt = 0;
	Enabled = *GetOptions().Graphics.townViewStartIn3D;
	CameraRig = TownCameraRig {};
	ApplyTownViewCameraPreferences();
	ResetTownViewCamera();
	SetTownViewCameraMode(NormalizeTownGameplayCameraMode(*GetOptions().Graphics.townViewCameraMode));
	GameplayCameraPolicy = true;
	ResetFollowCameraHistory(true);
	CameraRig.Suspend(!Enabled);
	DiagnosticGpuPreflightAttempted = false;
	if (Enabled) (void)PrepareDiagnosticGpuSession();
}

bool IsTownViewNativePose()
{
	constexpr float Tolerance = 0.00001F;
	return IsTownViewActive()
	    && CameraRig.mode() == TownCameraMode::Isometric
	    && std::abs(std::remainder(CameraYaw - Pi * 0.25F, 2 * Pi)) <= Tolerance
	    && std::abs(CameraPitch - Pi / 6) <= Tolerance
	    && std::abs(CameraDistance - DefaultCameraDistance) <= Tolerance
	    && std::abs(CameraPanOffset.x) <= Tolerance
	    && std::abs(CameraPanOffset.z) <= Tolerance;
}

void ToggleTownView()
{
	Enabled = !Enabled;
	CameraRig.Suspend(!IsTownViewActive());
	ResetFollowCameraHistory();
	EndTownViewCameraDrag();
	PickingValid = false;
}

void RotateTownView(float radians)
{
	OrbitTownView(radians, 0);
}

void AdjustTownViewDistance(float delta)
{
	if (!std::isfinite(delta) || CameraRig.mode() == TownCameraMode::FirstPerson)
		return;
	auto pose = CameraRig.pose();
	pose.distance += std::clamp(delta, -80.0F, 80.0F);
	CameraRig.SetPose(pose);
	SyncCameraPose();
	PickingValid = false;
}

void OrbitTownView(float yawDelta, float pitchDelta)
{
	if (!std::isfinite(yawDelta) || !std::isfinite(pitchDelta))
		return;
	CameraRig.Orbit(yawDelta, pitchDelta);
	SyncCameraPose();
	PickingValid = false;
}

void ZoomTownView(float wheelSteps)
{
	if (!std::isfinite(wheelSteps))
		return;
	CameraRig.Zoom(wheelSteps);
	SyncCameraPose();
	PickingValid = false;
}

void ResetTownViewCamera()
{
	GameplayCameraPolicy = false;
	ResetFollowCameraHistory();
	CameraRig.RestoreIsometric();
	SyncCameraPose();
	EndTownViewCameraDrag();
	PickingValid = false;
}

void ResetTownViewGameplayCamera()
{
	TownCameraRig defaults;
	defaults.SetMode(TownCameraMode::ThirdPerson);
	CameraRig.SetMode(TownCameraMode::ThirdPerson);
	CameraRig.SetPose(defaults.pose()); // Preserve other diagnostic/follow poses and preferences.
	GameplayCameraPolicy = true;
	ResetFollowCameraHistory();
	SyncCameraPose();
	EndTownViewCameraDrag();
}

TownViewCameraState GetTownViewCameraState()
{
	return { CameraYaw, CameraPitch, CameraDistance, CameraPanOffset.x, CameraPanOffset.z,
		CameraRig.mode(), CameraRig.preferences().verticalFovDegrees,
		FollowThroughTownArchitecture() ? GameplayEyeHeight()
		    : (CameraRig.mode() == TownCameraMode::FirstPerson ? CameraRig.preferences().firstPersonEyeHeight : CameraRig.preferences().eyeHeight) };
}

TownCameraMode GetTownViewCameraMode()
{
	return CameraRig.mode();
}

void SetTownViewCameraMode(TownCameraMode mode)
{
	if (!CameraRig.SetMode(mode))
		return;
	ResetFollowCameraHistory();
	SyncCameraPose();
	EndTownViewCameraDrag();
	PickingValid = false;
}

void CycleTownViewCameraMode()
{
	GameplayCameraPolicy = true;
	const auto next = NextTownGameplayCameraMode(CameraRig.mode());
	GetOptions().Graphics.townViewCameraMode.SetValue(static_cast<int>(next));
	SetTownViewCameraMode(next);
}

void ApplyTownViewCameraPreferences()
{
	auto preferences = CameraRig.preferences();
	preferences.verticalFovDegrees = static_cast<float>(std::clamp(*GetOptions().Graphics.townViewCameraFov, 35, 100));
	preferences.orbitRadiansPerPixel = 0.006F * static_cast<float>(std::clamp(*GetOptions().Graphics.townViewCameraSensitivity, 25, 200)) / 100;
	CameraRig.SetPreferences(preferences);
	ClearCameraVisibilityFrame();
	EndTownViewCameraDrag();
	PickingValid = false;
}

void SetTownViewCameraPoseForDiagnostics(TownCameraPose pose)
{
	GameplayCameraPolicy = false;
	ResetFollowCameraHistory();
	CameraRig.SetPose(pose);
	SyncCameraPose();
	EndTownViewCameraDrag();
	PickingValid = false;
}

bool AdvanceTownViewCamera(float seconds)
{
	CameraFollowFrameSeconds = std::isfinite(seconds) ? std::clamp(seconds, 0.0F, 0.1F) : 0;
	const bool changed = CameraRig.AdvanceVisual(CameraFollowFrameSeconds)
	    || (FollowCameraState.active && (FollowCameraState.transition || FollowCameraState.blocked) && CameraFollowFrameSeconds > 0);
	if (changed)
		PickingValid = false;
	return changed;
}

TownViewCameraVisibilityState GetTownViewCameraVisibilityState()
{
	return CameraVisibilityState;
}

bool IsTownViewArchitectureHiddenForDiagnostics(size_t owner)
{
	return CameraVisibilityState.active && CameraVisibilityState.valid
	    && CameraVisibilityState.sceneRevision == GetTownSceneRevision()
	    && owner < CameraHiddenArchitecture.size() && CameraHiddenArchitecture[owner] != 0;
}

void SetTownViewGameplayCameraPolicyForDiagnostics(bool enabled)
{
	GameplayCameraPolicy = enabled;
	ResetFollowCameraHistory();
}

TownViewFollowCameraState GetTownViewFollowCameraState()
{
	return FollowCameraState;
}

TownViewSamplingState GetTownViewSamplingState()
{
	return SamplingState;
}

TownViewRendererState GetTownViewRendererState()
{
	return RendererState;
}

const Surface *GetTownViewHighResolutionFrame()
{
	if (*GetOptions().Graphics.smooth3dLighting && SmoothWorldValid && SmoothWorldSurface && IsTownViewActive() && CurrentPickingValid() && CachedDungeonData==pDungeonCels.get()) return SmoothWorldSurface.get();
	if (!IsTownViewActive() || !CurrentPickingValid() || CachedDungeonData != pDungeonCels.get()
	    || SamplingState.factor != 2 || !SamplingSurface
	    || SamplingSurface->w() != gnScreenWidth * 2 || SamplingSurface->h() != gnViewportHeight * 2)
		return nullptr;
	return SamplingSurface.get();
}

bool BeginTownViewCameraDrag(Point screen, bool pan)
{
	if (!IsTownViewActive() || screen.x < 0 || screen.y < 0 || screen.x >= gnScreenWidth || screen.y >= gnViewportHeight)
		return false;
	if (pan && (CameraRig.mode() == TownCameraMode::ThirdPerson || CameraRig.mode() == TownCameraMode::FirstPerson))
		return false;
	CameraDragging = true;
	CameraDragPans = pan;
	CameraDragLastPosition = screen;
	return true;
}

bool UpdateTownViewCameraDrag(Point screen)
{
	if (!CameraDragging || !IsTownViewActive()) {
		EndTownViewCameraDrag();
		return false;
	}
	const Displacement delta = screen - CameraDragLastPosition;
	CameraDragLastPosition = screen;
	if (delta.deltaX == 0 && delta.deltaY == 0)
		return false;
	if (CameraDragPans) {
		const float unitsPerPixel = (ViewCamera.projection.perspective ? CameraDistance : 1.0F) / std::max(1.0F, ViewCamera.focal);
		const Vec3 right { std::sin(CameraYaw), 0, -std::cos(CameraYaw) };
		const Vec3 forward { -std::cos(CameraYaw), 0, -std::sin(CameraYaw) };
		const Vec3 movement = right * (-static_cast<float>(delta.deltaX) * unitsPerPixel)
		    + forward * (static_cast<float>(delta.deltaY) * unitsPerPixel / std::max(0.15F, std::sin(CameraPitch)));
		CameraRig.Pan(movement.x, movement.z);
		SyncCameraPose();
		PickingValid = false;
	} else {
		const float sensitivity = CameraRig.preferences().orbitRadiansPerPixel;
		OrbitTownView(static_cast<float>(delta.deltaX) * sensitivity * (4.0F / 3), static_cast<float>(delta.deltaY) * sensitivity);
	}
	return true;
}

void EndTownViewCameraDrag()
{
	CameraDragging = false;
}

bool IsTownViewCameraDragging()
{
	return CameraDragging && IsTownViewActive();
}

TownViewLightingState GetTownViewLightingState()
{
	return { SceneLightingConfig, SceneLightingProfileLoaded, ImportedTextureCache.size(),
		ImportedAlbedoLightTables.size(), ImportedAlbedoLightTables.size() * ImportedLightLevels, ImportedLightLevels,
		CabinInteriorTextureCache.size(), CabinFireEnabledForDiagnostics, DirectionalShadowsEnabledForDiagnostics };
}

void SetTownViewCabinFireEnabledForDiagnostics(bool enabled)
{
	CabinFireEnabledForDiagnostics = enabled;
}

void SetTownViewDirectionalShadowsEnabledForDiagnostics(bool enabled)
{
	DirectionalShadowsEnabledForDiagnostics = enabled;
}

void SetTownViewFireTimeForDiagnostics(double seconds)
{
	CabinFireDiagnosticTime = std::isfinite(seconds) && seconds >= 0 ? seconds : -1;
}

void SetTownViewResidentMeshesEnabledForDiagnostics(bool enabled)
{
	ResidentMeshesEnabledForDiagnostics = enabled;
	PickingValid = false;
}

void SetTownViewRasterJitterForDiagnostics(float x, float y)
{
	RasterJitterForDiagnostics = { std::isfinite(x) ? x : 0, std::isfinite(y) ? y : 0 };
	PickingValid = false;
}

void ResetTownViewResources()
{
	WarriorPilot = {};
	WarriorPilotState = {};
	WarriorViewModelCoverage.clear();
	DrawingWarriorViewModel = false;
	WarriorViewModelEnabledForDiagnostics = true;
	ClearFrameSpritePointLighting();
	ResetKnownMeshyRuntimePolicy();
	Sarcophagus = {};
	Barrel = {};
    Brazier = {};
    SkullFire = {};
    Chest = {};
    ChestVariants = {};
    DoorModels = {};
    PropAnimationDiagnosticTime=-1;
	CorpseGroundDraws=0;
	ResetCathedralNativeTextureCache();
	OgdenPilot = {};
	OgdenPilotState = {};
	OgdenGpuSubmissionFailed = false;
	FailOgdenGpuSubmissionForDiagnostics = false;
	CathedralAllocationState = {};
	CathedralFrame = {};
	CathedralFrameReady = DrawingCathedral = CathedralPaletteBlend = false;
	CathedralTranslation = nullptr;
	ResetFollowCameraHistory(true);
	ClearGpuSceneResources();
	HorizonMesh = {};
	std::vector<uint8_t>().swap(HorizonColors);
	HorizonPaletteValid = false;
	BillboardTextureCache.clear();
    SkullFireFlameCache.clear();
	ClearUiOverlayRegions();
#ifndef USE_SDL1
	ResetTownPresentationResources();
#endif
	SamplingSurface.reset();
	SamplingState = {};
	RasterSampleFactor = 1;
	std::vector<float>().swap(DepthBuffer);
	std::vector<PickRecord>().swap(PickBuffer);
	ClearTownShadowMap();
	TerrainCache.clear();
	SceneGroundCache.clear();
	NativeBridgeTextureCache.clear();
	SceneArtworkCache.clear();
	SceneMaterialCache.clear();
	SceneWaterEventState.clear();
	WellWaterMaterialTextureCache.clear();
	ClearFixedTownRendererCache();
	ImportedTextureCache.clear();
	CabinInteriorTextureCache.clear();
	ArchitectureBoundsCache.clear();
	ArchitectureBoundsRevision = std::numeric_limits<uint64_t>::max();
	ArchitectureCullingState = {};
	CabinFireEnabledForDiagnostics = true;
	DirectionalShadowsEnabledForDiagnostics = true;
	ImportedAlbedoColors.clear();
	ImportedAlbedoLightTables.clear();
	ActorVolumeCache.clear();
	VegetationArtworkCache.clear();
	PropArtworkCache.clear();
	PropReplacementReady = false;
	SceneryVolumeCache.clear();
	SceneLightingValid = false;
	SceneLightingProfileLoaded = false;
	SceneLightingConfig = TristramLightingConfig();
	SmoothWorldValid=false;SmoothWorldSurface.reset();SmoothWorldPixels.clear();
	ResetTownScene();
	ResetTownVegetation();
	ResetTownProps();
	CachedDungeonData = nullptr;
	EndTownViewCameraDrag();
	PickingValid = false;
}

static bool DrawTownViewFrame(const Surface &fullOut, bool forceGeometry)
{
	WarriorFirstPerson = {}; // No prior-frame self presentation survives early fallback.
	DiagnosticEligibleFrame = false;
	SmoothWorldValid=false;SmoothWorldPixels.clear();
	CathedralDiagnosticStage = CathedralFallbackStage::Preflight;
	CathedralDiagnosticGpuRefusalStage = CathedralFallbackStage::None;
	CathedralDiagnosticGpuAttempted = false;
	CathedralDiagnosticForceGeometry = forceGeometry;
	CathedralDiagnosticMaterialBindingValid = false;
	const bool cathedralWorld = cathedral::LiveLevelEligible() && CathedralFrameReady;
	const bool forcedCathedralGeometry = forceGeometry && Enabled && cathedralWorld && !CathedralFrame.requiresNativeFallback;
	const auto worldStart = std::chrono::steady_clock::now();
	OgdenPilotState.drawn = false;
	OgdenPilotState.trianglesVisited = 0;
	OgdenGpuSubmissionFailed = false;
	PickingValid = false;
	if (!FollowThroughTownArchitecture())
		ClearCameraVisibilityFrame();
	RasterSampleFactor = 1;
	RendererState = {};
	ArchitectureCullingState = {};
	ArchitectureCullingState.requested = *GetOptions().Graphics.townViewFrustumCulling;
	RendererState.requestedGpu = EffectiveGpuRequested();
	CaptureGpu = CaptureGpuFailed = false;
	if (!RendererState.requestedGpu) {
		if (GpuRecovery.DisableOrReset())
			ResetTownGpuResources();
		GpuFailure.clear();
	}
	if ((!IsTownViewActive() && !forcedCathedralGeometry) || !pDungeonCels || MicroTileLen == 0 || MyPlayer == nullptr) {
		ClearCameraVisibilityFrame();
		PickingValid = false;
		if (leveltype == DTYPE_CATHEDRAL) {
			if (!Enabled) {
				RecordCathedralDiagnostic(CathedralFallbackReason::RenderDisabled, CathedralDiagnosticStage, false, RendererState.requestedGpu);
			} else if (!CathedralPreparationRefused) {
				const auto reason = MyPlayer == nullptr ? CathedralFallbackReason::PlayerUnavailable
				    : (!pDungeonCels || MicroTileLen == 0) ? CathedralFallbackReason::NativeResourcesUnavailable
				    : cathedral::LiveLevelEligible() ? CathedralFallbackReason::PilotNotReady : CathedralFallbackReason::ContextIneligible;
				RecordCathedralDiagnostic(reason, CathedralDiagnosticStage, true, RendererState.requestedGpu);
			}
			TryLogCathedralDiagnostic(); // Preserve the prior preparation refusal.
		}
		return false;
	}
	if (cathedralWorld && !CathedralNativeTexturesCurrent(CathedralFrame)) {
		RecordCathedralDiagnostic(CathedralFallbackReason::MaterialBindingsStale, CathedralDiagnosticStage, true, RendererState.requestedGpu);
		InvalidateTownViewFrameForNativeFallback();
		TryLogCathedralDiagnostic();
		return false; // Never publish a frame with missing/stale material bindings.
	}
	if (CachedDungeonData != pDungeonCels.get()) {
		ClearFrameSpritePointLighting();
		ResetFollowCameraHistory(true);
		ClearGpuSceneResources();
		HorizonPaletteValid = false;
		RendererState.requestedGpu = EffectiveGpuRequested();
		BillboardTextureCache.clear();
    SkullFireFlameCache.clear();
		ClearTownShadowMap();
		TerrainCache.clear();
		SceneGroundCache.clear();
	NativeBridgeTextureCache.clear();
		SceneArtworkCache.clear();
		SceneMaterialCache.clear();
		SceneWaterEventState.clear();
		WellWaterMaterialTextureCache.clear();
		ClearFixedTownRendererCache();
		ImportedTextureCache.clear();
		CabinInteriorTextureCache.clear();
		ArchitectureBoundsCache.clear();
		ArchitectureBoundsRevision = std::numeric_limits<uint64_t>::max();
		CabinFireEnabledForDiagnostics = true;
		DirectionalShadowsEnabledForDiagnostics = true;
		ImportedAlbedoColors.clear();
		ImportedAlbedoLightTables.clear();
		ActorVolumeCache.clear();
		VegetationArtworkCache.clear();
		PropArtworkCache.clear();
		PropReplacementReady = false;
		SceneryVolumeCache.clear();
		SceneLightingValid = false;
		SceneLightingProfileLoaded = false;
		SceneLightingConfig = TristramLightingConfig();
		ResetTownScene();
		ResetTownVegetation();
		ResetTownProps();
		CachedDungeonData = pDungeonCels.get();
	}
	const int height = std::min<int>(gnViewportHeight, fullOut.h());
	if (fullOut.w() <= 0 || height <= 0) {
		ClearCameraVisibilityFrame();
		if (cathedralWorld) {
			RecordCathedralDiagnostic(CathedralFallbackReason::ViewportUnavailable, CathedralDiagnosticStage, true, RendererState.requestedGpu);
			TryLogCathedralDiagnostic();
		}
		return false;
	}
	const Surface logical = fullOut.subregionY(0, height);
	CathedralDiagnosticStage = CathedralFallbackStage::Camera;
	ConfigureCamera(logical.w(), logical.h(), forcedCathedralGeometry);
	ViewCamera.centerX += RasterJitterForDiagnostics[0];
	ViewCamera.centerY += RasterJitterForDiagnostics[1];
	ViewCamera.projection.centerX = ViewCamera.centerX;
	ViewCamera.projection.centerY = ViewCamera.centerY;
	if (IsTownViewNativePose() && !forceGeometry) {
		// A reference angle must reproduce the real game, including its painter
		// order, trees, actors and zoom. Rotation exposes the reconstructed volumes.
		// Cursor selection follows the same native path at this exact pose.
		PickingValid = false;
		if (cathedralWorld) {
			RecordCathedralDiagnostic(CathedralFallbackReason::NativePoseReference, CathedralDiagnosticStage, false, RendererState.requestedGpu);
			TryLogCathedralDiagnostic(); // Home is an intentional reference, not a failure.
		}
		SamplingState = { *GetOptions().Graphics.townViewAntialiasing, 1, logical.w(), logical.h(), false };
		ClearSurface(logical);
		return DrawNativeTownViewReference(fullOut, ViewPosition);
	}
	DiagnosticEligibleFrame = true;
	const uint64_t diagnosticFrame = diag::BeginSessionFrame();
	diag::StageScope diagnosticWorld(diag::SessionLogger(), diag::Stage::WorldPrepare, diagnosticFrame, diag::SampleSessionFrame());
	const auto preflight = PrepareDiagnosticGpuSession();
	if (preflight.error != diag::Error::None) return RefuseRequiredGpuFrame(fullOut, preflight);
	if (RequiredGpu() && !RendererState.requestedGpu) return RefuseRequiredGpuFrame(fullOut, { diag::Error::GpuPreferenceDisabled, diag::Stage::WorldPrepare });
	if (ViewCamera.projection.valid) {
		// Sample the rig once, then publish its eye before visibility/culling,
		// sampling buffers, shadow receivers and the GPU recovery identity.
		PrepareWarriorNativeFrame(cathedralWorld);
		PrepareWarriorFirstPersonPresentation();
	}
	if (!ViewCamera.projection.valid) {
		if (RequiredGpu()) return RefuseRequiredGpuFrame(fullOut, { diag::Error::CameraProjectionInvalid, diag::Stage::Camera });
		ClearCameraVisibilityFrame();
		RendererState.failure = "Invalid town camera frame";
		if (cathedralWorld) {
			RecordCathedralDiagnostic(CathedralFallbackReason::CameraProjectionInvalid, CathedralDiagnosticStage, true, RendererState.requestedGpu);
			TryLogCathedralDiagnostic();
			return false;
		}
		if (FollowCameraState.active) {
			// No safe near-plane sphere: fail closed instead of presenting the
			// native 2D world with first-person input. Picking remains invalid.
			SamplingState = { *GetOptions().Graphics.townViewAntialiasing, 1, logical.w(), logical.h(), false };
			ClearSurface(logical);
			return true;
		}
		return false;
	}
	// Compute once before any backend consumes the scene. The same immutable
	// snapshot is used by both drawWorld calls when a GPU frame replays on CPU.
	if (!PrepareCameraVisibilityFrame()) {
		if (RequiredGpu()) return RefuseRequiredGpuFrame(fullOut, { diag::Error::CameraVisibilityUnavailable, diag::Stage::Camera });
		// Reporting memory pressure must not allocate outside the protected path.
		try {
			RendererState.failure = "Town camera visibility index/query unavailable";
		} catch (const std::bad_alloc &) {
			RendererState.failure.clear();
		}
		SamplingState = { *GetOptions().Graphics.townViewAntialiasing, 1, logical.w(), logical.h(), false };
		ClearSurface(logical);
		return true; // Empty local frame, never stale IDs or a stuck old camera.
	}
	CathedralDiagnosticStage = CathedralFallbackStage::SamplingBuffers;
	const Surface out = PrepareSamplingBuffers(logical);
	if (*GetOptions().Graphics.smooth3dLighting && static_cast<size_t>(out.w())*out.h()<=MaxSmoothWorldPixels) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		try {
#endif
		SmoothWorldPixels.assign(static_cast<size_t>(out.w())*out.h(),0);
		std::array<SDL_Color,256> colors,gray;
		for(unsigned i=0;i<256;++i)gray[i]={uint8_t(i),uint8_t(i),uint8_t(i),255};
		ApplyGlobalBrightness(colors.data(),logical_palette.data());ApplyGlobalBrightness(SmoothToneRamp.data(),gray.data());
		uint64_t expected=0,active=0;for(unsigned i=0;i<256;++i){expected+=colors[i].r+colors[i].g+colors[i].b;active+=Palette->colors[i].r+Palette->colors[i].g+Palette->colors[i].b;}
		// Normal-frame palette rotations (water/lava) preserve total energy.
		// Fade frames retain unfaded colors and are faded in presentation instead.
		if(expected>0 && std::abs(double(active)-double(expected))<double(expected)*.005)for(unsigned i=0;i<256;++i)colors[i]=Palette->colors[i];
		for(size_t i=0;i<SmoothWorldPalette.size();++i){const auto color=colors[i];SmoothWorldPalette[i]=0xFF000000U|(uint32_t(color.r)<<16)|(uint32_t(color.g)<<8)|color.b;}
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        } catch(const std::bad_alloc &) {SmoothWorldPixels.clear();SmoothWorldValid=false;}
#endif
	}
	ClearSurface(out);
	if (!cathedralWorld) {
		if (SceneTimeOfDay != std::clamp(*GetOptions().Graphics.townViewTimeOfDay, 0, 2)) {
			SceneLightingValid = false;
			HorizonPaletteValid = false;
			GpuOrdinaryLut.clear();
			GpuImportedLutColors = 0;
			++SceneLightingRevision;
		}
		PrepareSceneLighting();
		if (ImportedLightingRevision != SceneLightingRevision) {
			ImportedLightingRevision = SceneLightingRevision;
			for (const auto &[key, row] : ImportedAlbedoColors) {
				const TownLightColor base = TownSrgbToLinear({
					(static_cast<float>((key >> 12) & 63) * 4 + 1.5F) / 255,
					(static_cast<float>((key >> 6) & 63) * 4 + 1.5F) / 255,
					(static_cast<float>(key & 63) * 4 + 1.5F) / 255 });
				auto &table = ImportedAlbedoLightTables[row];
				for (size_t level = 0; level < table.size(); ++level) {
					const float amount = static_cast<float>(level) / (table.size() - 1);
					TownLightingSample lighting;
					lighting.ambient = SceneLightingConfig.ambient;
					lighting.directional = { SceneLightingConfig.directional.red * SceneLightingConfig.directionalIntensity * amount,
						SceneLightingConfig.directional.green * SceneLightingConfig.directionalIntensity * amount,
						SceneLightingConfig.directional.blue * SceneLightingConfig.directionalIntensity * amount };
					const auto lit = TownLinearToSrgb(ComposeTownLitColor(base, lighting));
					table[level] = ClosestSceneColor(lit.red * 255, lit.green * 255, lit.blue * 255);
				}
			}
		}
	}
	const bool horizonEnabled = !cathedralWorld && *GetOptions().Graphics.townViewHorizon;
	if (horizonEnabled) {
		PrepareHorizon();
		RendererState.horizonTriangles = HorizonMesh.statistics.triangles;
		RendererState.horizonBytes = HorizonMesh.statistics.reservedTriangleBytes + HorizonColors.capacity()
		    + sizeof(HorizonFogPalette) + sizeof(HorizonFogDepthLevels) + sizeof(HorizonSkyColors) + sizeof(HorizonPaletteSignature);
	}
	FrameFireTime = CabinFireDiagnosticTime >= 0 ? CabinFireDiagnosticTime : static_cast<double>(SDL_GetTicks()) / 1000;
	PrepareFrameSpritePointLighting();
	TownShadowConfig shadowConfig;
	shadowConfig.toLight = { SceneLightingConfig.toLight.x, SceneLightingConfig.toLight.height, SceneLightingConfig.toLight.z };
	if (cathedralWorld)
		ClearTownShadowMap();
	else
		BuildTownShadowMap(GetTownScene(), shadowConfig);
	++GpuFrameNumber;
	const TownGpuRecoveryKey recoveryKey {
		CameraRig.revision(), ViewSceneRevision(),
		{ ViewCamera.projection.eye.x, ViewCamera.projection.eye.height, ViewCamera.projection.eye.z },
		{ ViewCamera.projection.centerX, ViewCamera.projection.centerY },
		out.w(), out.h(), RasterSampleFactor, horizonEnabled, ArchitectureCullingState.requested, *GetOptions().Graphics.zoom
	};
	// Both backends need the pilot; prepare before any frozen GPU material table.
	if (!cathedralWorld)
		PrepareOgdenIdlePilotTexture();
    else if(DoorModels.loaded)for(size_t part=0;part<2;++part)RigidPropNativeTexture(DoorTextureKey-part,*DoorModels.models[part].importedTexture);
    if(cathedralWorld && Brazier.loaded)RigidPropNativeTexture(BrazierTextureKey,*Brazier.model.importedTexture);
    if(cathedralWorld && SkullFire.loaded)RigidPropNativeTexture(SkullFireTextureKey,*SkullFire.model.importedTexture);
	// Pose/gear/eye are already frozen. Material preparation stays after the
	// world/palette lighting update and never samples another pose.
	if (!WarriorPilot.runtime.loaded() || !PrepareWarriorNativeMaterials(cathedralWorld)) WarriorPilot.materialsReady = false;
	// The Cathedral path binds native discrete light/TRN LUTs and overlays on
	// the existing hardware backend. A failed request always returns to native.
	if (GpuRecovery.ShouldAttempt(RendererState.requestedGpu, recoveryKey, SDL_GetTicks())) {
		if (!cathedralWorld)
			PrepareFrameImportedAlbedo();
		CathedralDiagnosticStage = CathedralFallbackStage::GpuBegin;
		CathedralDiagnosticGpuAttempted = true;
		diag::StageScope diagnosticBegin(diag::SessionLogger(), diag::Stage::GpuBegin, diagnosticFrame, diag::SampleSessionFrame());
		CaptureGpu = TownGpuBeginFrame(out.w(), out.h(), false,
		    { ViewCamera.projection.perspective, ViewCamera.projection.nearClip, ViewCamera.projection.farClip });
		if (CaptureGpu && !SmoothWorldPixels.empty()) {
			std::vector<float> native;
			if(cathedralWorld){native.resize(MAXDUNX*MAXDUNY);for(int z=0;z<MAXDUNY;++z)for(int x=0;x<MAXDUNX;++x)native[z*MAXDUNX+x]=std::min<int>(dLight[x][z],15);}
			CaptureGpuFailed=!TownGpuSetSmoothLighting(SmoothWorldPalette,native,cathedralWorld?MAXDUNX:0,cathedralWorld?MAXDUNY:0);
		}
		if (CaptureGpu && !CaptureGpuFailed && FrameSpritePointLightingValid)
			CaptureGpuFailed = !TownGpuSetSpritePointLighting({ FrameSpritePointLights.data(), FrameSpritePointLightCount }, FrameSpritePointOccluders);
		if (CaptureGpu) {
			GpuPickIds.clear();
			GpuPickRecords.assign(1, PickRecord {});
			const TownShadowMapView view = GetTownShadowMapView();
			TownGpuShadow shadow;
			if (!view.depth.empty()) {
				shadow.stableKey = 1;
				shadow.revision = view.revision;
				shadow.resolution = view.config.resolution;
				shadow.depth = view.depth;
				shadow.right = { view.right.x, view.right.height, view.right.z };
				shadow.up = { view.up.x, view.up.height, view.up.z };
				shadow.light = { view.light.x, view.light.height, view.light.z };
				shadow.minU = view.minU;
				shadow.minV = view.minV;
				shadow.texelU = view.texelU;
				shadow.texelV = view.texelV;
				shadow.pcfRadius = view.config.pcfRadius;
			}
			CathedralDiagnosticStage = CathedralFallbackStage::GpuShadow;
			CaptureGpuFailed = !TownGpuSetShadow(shadow);
			if (cathedralWorld && CaptureGpuFailed)
				CathedralDiagnosticGpuRefusalStage = CathedralDiagnosticStage;
			if (!CaptureGpuFailed && cathedralWorld) {
				CathedralDiagnosticStage = CathedralFallbackStage::GpuPaletteBlend;
				CaptureGpuFailed = !PrepareCathedralGpuBlend();
				if (CaptureGpuFailed) CathedralDiagnosticGpuRefusalStage = CathedralDiagnosticStage;
			}
			if (!CaptureGpuFailed && ResidentMeshesEnabledForDiagnostics)
				ConfigureResidentMeshCamera();
		} else {
			GpuRecovery.RecordFailure(GetTownGpuStatus().failureKind, recoveryKey, SDL_GetTicks());
			GpuFailure = GetTownGpuStatus().failure;
		}
	}
	if (RequiredGpu() && !CaptureGpu) return RefuseRequiredGpuFrame(fullOut, { diag::Error::GpuBeginFailed, diag::Stage::GpuBegin, GetTownGpuStatus().failureCode, GetTownGpuStatus().failureCodeKnown });
	if (cathedralWorld && RendererState.requestedGpu && !CaptureGpu) {
		RecordCathedralDiagnostic(CathedralDiagnosticGpuAttempted ? CathedralFallbackReason::GpuBeginRefused : CathedralFallbackReason::GpuRequestDeferred,
			CathedralFallbackStage::GpuBegin, true, RendererState.requestedGpu);
		RendererState.failure = GpuFailure.empty() ? "Cathedral hardware GPU request deferred; complete native fallback" : GpuFailure;
		InvalidateTownViewFrameForNativeFallback();
		TryLogCathedralDiagnostic();
		return false;
	}
	const auto drawWorld = [&]() {
		diag::StageScope diagnosticRecord(diag::SessionLogger(), diag::Stage::SceneRecord, diagnosticFrame, diag::SampleSessionFrame());
		const auto recordStart = std::chrono::steady_clock::now();
		if (cathedralWorld) {
			DrawCathedralWorld(out);
			RendererState.sceneRecordMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - recordStart).count();
			return;
		}
		if (horizonEnabled)
			DrawHorizon(out);
		// GetTownScene admits architecture and static archive in one transaction.
		// A validated persistent scene never calls native visual generators here.
		(void)GetTownScene();
		if (HasTownVisualObjects()) {
			DrawFixedTownObjects(out, false);
			DrawScene(out);
			DrawFixedTownObjects(out, true);
		} else {
		FixedTownObjectState = {};
		PrepareTownPropArtwork();
		const Texture &fallback = FallbackGround();
		// Native town generation fills the entire dungeon grid, including the outer
		// grass visible around Farnham and Adria in wide views.
		// Camera exploration changes only drawing; movement keeps the native dmin/dmax bounds.
		const auto forEachTile = [](const auto &draw) {
			// Exactly the former stable order (x+y, then x), without allocating
			// or sorting the grid each frame. Read live pieces on both passes.
			for (int diagonal = 0; diagonal < MAXDUNX + MAXDUNY - 1; ++diagonal) {
				const int firstX = std::max(0, diagonal - MAXDUNY + 1);
				const int lastX = std::min(MAXDUNX - 1, diagonal);
				for (int x = firstX; x <= lastX; ++x) {
					const int y = diagonal - x;
					// Zero is the first valid MIN piece, not an empty cell.
					if (dPiece[x][y] < MAXTILES)
						draw(Point { x, y });
				}
			}
		};
		// Native floor diamonds are drawn before the SOL cell artwork. Coplanar
		// painted bases can overlap those diamonds; retain their native draw order.
		forEachTile([&](Point tile) {
			const TileArt &art = GetTile(dPiece[tile.x][tile.y]);
			if (!art.solid)
				DrawGround(out, tile, PreparedTownPropReplacesTile(tile, true) ? fallback : SceneGround(dPiece[tile.x][tile.y]));
		});
		forEachTile([&](Point tile) {
			DrawScenery(out, tile, GetTile(dPiece[tile.x][tile.y]), fallback);
		});
		DrawScene(out);
		DrawVegetation(out);
		DrawProps(out);
		}
		for (size_t i = 0; i < Towners.size(); ++i) {
			const Towner &towner = Towners[i];
			if (!towner.anim)
				continue;
			if (DrawOgdenIdlePilot(out, towner, i))
				continue;
			if (towner._ttype == TOWN_COW) {
				const auto sheet = GetTownCowSpriteSheet();
				const ClxSprite current = towner.currentSprite();
				const int frame = towner._tAnimFrame;
				if (sheet && sheet->numLists() == 8 && frame >= 0) {
					int direction = -1;
					for (int view = 0; view < 8; ++view) {
						if (static_cast<uint32_t>(frame) < (*sheet)[view].numSprites()
							&& (*sheet)[view][frame].pixelData() == current.pixelData()) {
							direction = view;
							break;
						}
					}
					if (direction >= 0) {
						DrawDirectionalActorVolume(out, { static_cast<float>(towner.position.x), 0, static_cast<float>(towner.position.y) },
							towner.position, *sheet, direction, frame, current, PickKind::Towner, static_cast<int>(i));
						continue;
					}
				}
			}
			DrawVolumetricSprite(out, { static_cast<float>(towner.position.x), 0, static_cast<float>(towner.position.y) },
				towner.position, towner.currentSprite(), PickKind::Towner, static_cast<int>(i));
		}
		for (uint8_t i = 0; i < ActiveItemCount; ++i) {
			const int index = ActiveItems[i];
			const Item &item = Items[index];
			if (!item.AnimInfo.sprites)
				continue;
			DrawVolumetricSprite(out, { static_cast<float>(item.position.x), 0, static_cast<float>(item.position.y) },
				item.position, item.AnimInfo.currentSprite(), PickKind::Item, index);
		}
		for (size_t i = 0; i < Players.size(); ++i) {
			const Player &player = Players[i];
			if (!player.plractive || !player.isOnActiveLevel() || !player.AnimInfo.sprites)
				continue;
			if (&player == MyPlayer && !player.hasNoLife() && FollowCameraState.localPlayerHidden) continue;
			Vec3 position = PlayerPosition(player);
			position.y = 0;
			if (!DrawWarriorNativePilot(out, player, i, true, 0, false))
				DrawPlayerVolume(out, position, player, static_cast<int>(i));
		}
		for (const Missile &missile : Missiles) {
			if (!missile._miDrawFlag || missile._miDelFlag || !missile._miAnimData
				|| missile._miAnimFrame <= 0 || static_cast<size_t>(missile._miAnimFrame) > missile._miAnimData->numSprites())
				continue;
			const Point tile = missile.position.tileForRendering;
			const Displacement offset = missile.position.offsetForRendering;
			Vec3 position { static_cast<float>(tile.x) + static_cast<float>(2 * offset.deltaY + offset.deltaX) / 64.0F,
				0, static_cast<float>(tile.y) + static_cast<float>(2 * offset.deltaY - offset.deltaX) / 64.0F };
			const int light = InDungeonBounds(tile) && missile._miLightFlag ? dLight[tile.x][tile.y] : 0;
			if (InDungeonBounds(tile))
				DrawBillboard(out, position, tile, (*missile._miAnimData)[missile._miAnimFrame - 1], PickKind::Ground, -1, light);
		}
		RendererState.sceneRecordMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - recordStart).count();
	};
	CathedralDiagnosticStage = CathedralFallbackStage::Submission;
	WarriorViewModelCoverage.clear();
	drawWorld();
	if (CaptureGpu && !CaptureGpuFailed) {
		diag::StageScope diagnosticViewModel(diag::SessionLogger(), diag::Stage::ViewModelSubmit, diagnosticFrame, diag::SampleSessionFrame());
		DrawWarriorViewModel(out);
	}
	if (CaptureGpu) {
		if (cathedralWorld && CathedralAllocationState.armed == TownViewCathedralAllocationFailurePoint::BeforeReadback)
			SetTownGpuBeforeReadbackForDiagnostics(InjectCathedralReadbackAllocationFailure);
		CathedralDiagnosticStage = CathedralFallbackStage::GpuEnd;
		const bool diagnosticGpuEndSucceeded = [&]() {
			diag::StageScope diagnosticReadback(diag::SessionLogger(), diag::Stage::Readback, diagnosticFrame, diag::SampleSessionFrame());
			return !CaptureGpuFailed && TownGpuEndFrame(GpuFrame);
		}();
		CathedralDiagnosticStage = CathedralFallbackStage::OutputValidation;
		const bool complete = diagnosticGpuEndSucceeded
		    && GpuFrame.width == out.w() && GpuFrame.height == out.h()
		    && GpuFrame.indexed.size() == DepthBuffer.size()
		    && (SmoothWorldPixels.empty() || GpuFrame.rgba.size()==DepthBuffer.size())
		    && GpuFrame.depth.size() == DepthBuffer.size() && GpuFrame.pickIds.size() == PickBuffer.size()
		    && GpuFrame.viewModelMask.size() == DepthBuffer.size()
		    && std::all_of(GpuFrame.viewModelMask.begin(), GpuFrame.viewModelMask.end(), [](uint8_t mask) { return mask <= 1; })
		    && std::all_of(GpuFrame.pickIds.begin(), GpuFrame.pickIds.end(), [](uint32_t id) { return id < GpuPickRecords.size(); });
		CaptureGpu = false;
		if (!complete && RequiredGpu()) return RefuseRequiredGpuFrame(fullOut,
		    { CaptureGpuFailed ? diag::Error::GpuSubmissionFailed : diagnosticGpuEndSucceeded ? diag::Error::GpuOutputRejected : diag::Error::GpuReadbackFailed,
		        CaptureGpuFailed ? diag::Stage::WorldSubmit : diag::Stage::Readback, GetTownGpuStatus().failureCode, GetTownGpuStatus().failureCodeKnown });
		if (complete) {
			for (int y = 0; y < out.h(); ++y)
				std::memcpy(out.at(0, y), GpuFrame.indexed.data() + static_cast<size_t>(y) * out.w(), out.w());
			DepthBuffer.swap(GpuFrame.depth);
			WarriorViewModelCoverage = std::move(GpuFrame.viewModelMask);
			if (!SmoothWorldPixels.empty()) SmoothWorldPixels.swap(GpuFrame.rgba);
			for (size_t i = 0; i < PickBuffer.size(); ++i)
				PickBuffer[i] = GpuPickRecords[GpuFrame.pickIds[i]];
			RendererState.usedGpu = true;
			GpuRecovery.RecordSuccess();
			GpuFailure.clear();
		} else {
			if (OgdenGpuSubmissionFailed) {
				// Keep the GPU preference and retry hardware on the next frame with
				// the native actor volume. Never publish a partial image or CPU redraw.
				ClearGpuSceneResources();
				OgdenPilot.ready = false;
				OgdenPilotState.loaded = false;
				OgdenPilotState.drawn = false;
				RendererState.requestedGpu = true;
				GpuFailure = "Ogden GPU submission allocation failed; frame discarded";
				RendererState.failure = GpuFailure;
				std::fill(DepthBuffer.begin(), DepthBuffer.end(), std::numeric_limits<float>::infinity());
				std::fill(PickBuffer.begin(), PickBuffer.end(), PickRecord {});
				SamplingState = { *GetOptions().Graphics.townViewAntialiasing, 1, logical.w(), logical.h(), false };
				ClearSurface(logical);
				return true;
			}
			GpuRecovery.RecordFailure(GetTownGpuStatus().failureKind, recoveryKey, SDL_GetTicks());
			GpuFailure = GetTownGpuStatus().failure;
			if (GpuFailure.empty())
				GpuFailure = "GPU frame validation failed";
			if (cathedralWorld) {
				RecordCathedralDiagnostic(CaptureGpuFailed ? CathedralFallbackReason::GpuSubmissionRefused
					: diagnosticGpuEndSucceeded ? CathedralFallbackReason::GpuOutputRejected : CathedralFallbackReason::GpuEndFrameRefused,
					CaptureGpuFailed ? (CathedralDiagnosticGpuRefusalStage == CathedralFallbackStage::None
						? CathedralFallbackStage::Submission : CathedralDiagnosticGpuRefusalStage)
					: diagnosticGpuEndSucceeded ? CathedralFallbackStage::OutputValidation : CathedralFallbackStage::GpuEnd,
					true, RendererState.requestedGpu);
				RendererState.failure = GpuFailure;
				ClearSurface(out);
				InvalidateTownViewFrameForNativeFallback();
				TryLogCathedralDiagnostic();
				return false; // DrawGame owns the complete native world fallback.
			}
			ClearSurface(out);
			std::fill(DepthBuffer.begin(), DepthBuffer.end(), std::numeric_limits<float>::infinity());
			std::fill(PickBuffer.begin(), PickBuffer.end(), PickRecord {});
			drawWorld();
		}
	}
	diag::Frame diagnosticPublication;
	diagnosticPublication.sequence = diagnosticFrame;
	diagnosticPublication.requestedGpu = RendererState.requestedGpu; diagnosticPublication.usedGpu = RendererState.usedGpu;
	diagnosticPublication.hardware = GetTownGpuStatus().available && !GetTownGpuStatus().warp;
	diagnosticPublication.warp = RendererState.usedGpu && GetTownGpuStatus().warp;
	diagnosticPublication.completeOutput = DepthBuffer.size() == static_cast<size_t>(out.w()) * out.h() && PickBuffer.size() == DepthBuffer.size();
	diagnosticPublication.cpuRasterizedTriangles = RendererState.cpuRasterizedTriangles;
	diagnosticPublication.submittedTriangles = GetTownGpuStatus().submittedTriangles; diagnosticPublication.drawCalls = GetTownGpuStatus().drawCalls;
	diagnosticPublication.readbackMicroseconds = static_cast<uint64_t>(GetTownGpuStatus().readbackMilliseconds * 1000);
	diag::Failure diagnosticFailure;
	const auto diagnosticDecision = diag::CheckFrame(diag::SessionPolicy(), diagnosticPublication, diagnosticFailure);
	if (diagnosticDecision == diag::Decision::RefuseFrame) return RefuseRequiredGpuFrame(fullOut, diagnosticFailure);
	RendererState.failure = GpuFailure;
	for (auto it = PreparedGpuTextures.begin(); it != PreparedGpuTextures.end();) {
		if (it->second.lastSeen + 2 < GpuFrameNumber)
			it = PreparedGpuTextures.erase(it);
		else
			++it;
	}
	if (horizonEnabled) {
		diag::StageScope diagnosticAtmosphere(diag::SessionLogger(), diag::Stage::Atmosphere, diagnosticFrame, diag::SampleSessionFrame());
		ApplyHorizonAtmosphere(out);
	}
	// CPU FP is composed after fog against its own visual depth; published world
	// depth/picks are restored before AA resolution and interaction/label reads.
	if (!RendererState.usedGpu) DrawWarriorViewModel(out);
	CathedralDiagnosticStage = CathedralFallbackStage::Resolve;
	{
		diag::StageScope diagnosticResolve(diag::SessionLogger(), diag::Stage::Resolve, diagnosticFrame, diag::SampleSessionFrame());
		ResolveSampling(logical, out);
	}
	if (!SmoothWorldPixels.empty()) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		try {
#endif
		if (!SmoothWorldSurface || SmoothWorldSurface->w()!=out.w() || SmoothWorldSurface->h()!=out.h()) {
#ifdef USE_SDL3
			SDLSurfaceUniquePtr surface {SDL_CreateSurface(out.w(),out.h(),SDL_PIXELFORMAT_ARGB8888)};
#else
			SDLSurfaceUniquePtr surface {SDL_CreateRGBSurfaceWithFormat(0,out.w(),out.h(),32,SDL_PIXELFORMAT_ARGB8888)};
#endif
			if(surface)SmoothWorldSurface=std::make_unique<OwnedSurface>(std::move(surface));else SmoothWorldSurface.reset();
		}
		if(SmoothWorldSurface){for(int y=0;y<out.h();++y){auto *row=reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(SmoothWorldSurface->surface->pixels)+y*SmoothWorldSurface->surface->pitch);for(int x=0;x<out.w();++x){const size_t i=static_cast<size_t>(y)*out.w()+x;row[x]=SmoothWorldPixels[i]!=0?SmoothWorldPixels[i]:SmoothWorldPalette[*out.at(x,y)];}}SmoothWorldValid=true;}
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        } catch(const std::bad_alloc &) {SmoothWorldValid=false;SmoothWorldSurface.reset();}
#endif
	}
	PickScreenWidth = gnScreenWidth;
	PickScreenHeight = gnScreenHeight;
	PickViewportHeight = gnViewportHeight;
	PickZoom = *GetOptions().Graphics.zoom;
	PickLeftPanel = IsLeftPanelOpen();
	PickRightPanel = IsRightPanelOpen();
	PickGpuRequested = RendererState.requestedGpu;
	PickFrustumCullingRequested = ArchitectureCullingState.requested;
	PickSceneRevision = ViewSceneRevision();
	PickCameraRevision = CameraRig.revision();
	PickFollowProjectionRevision = FollowProjectionRevision;
	PickCameraVisibilityRevision = CameraVisibilityRevision;
	PickHorizonEnabled = *GetOptions().Graphics.townViewHorizon;
	PickingValid = !cathedralWorld || (!CathedralFrame.requiresNativeFallback
	    && (!RendererState.requestedGpu || RendererState.usedGpu) && cathedral::LiveFrameCurrent());
	ItemLabelAnchorCount = 0;
	if (PickingValid && !IsTownViewNativePose()) {
		// Capture native identity/position in the same completed color/depth frame.
		// O(MAXITEMS), no framebuffer scan, sprite decoding or simulation RNG.
		if (++ItemLabelFrameId == 0)
			++ItemLabelFrameId;
		for (size_t i = 0; i < std::min<size_t>(ActiveItemCount, MAXITEMS); ++i) {
			const int slot = ActiveItems[i];
			if (slot >= MAXITEMS)
				continue;
			const Item &item = Items[slot];
			if (item.isEmpty() || !item.AnimInfo.sprites || !InDungeonBounds(item.position)
			    || item.selectionRegion == SelectionRegion::None
			    || dItem[item.position.x][item.position.y] != slot + 1
			    || (!item.AnimInfo.isLastFrame() && item._iCurs != ICURS_MAGIC_ROCK)
			    || (cathedralWorld && (!CathedralTileInFrame(item.position) || !IsTileVisible(item.position)
			        || !IsTileLit(item.position) || dLight[item.position.x][item.position.y] >= LightsMax
			        || !LineClearMissile(MyPlayer->position.tile, item.position))))
				continue;
			if (cathedralWorld) {
				// Labels use the published world depth, not an unobstructed overlay
				// projection that could disclose loot behind masonry/camera walls.
				TownCameraProjectedVertex body;
				if (!ProjectTownCameraPoint(ViewCamera.projection,
				        { static_cast<float>(item.position.x), 0.2F, static_cast<float>(item.position.y) }, body))
					continue;
				const int x = static_cast<int>(std::lround(body.x)), y = static_cast<int>(std::lround(body.y));
				if (x < 0 || y < 0 || x >= ViewCamera.width || y >= ViewCamera.height
				    || DepthBuffer[static_cast<size_t>(y) * ViewCamera.width + x] + 0.08F < body.depth)
					continue;
			}
			TownCameraProjectedVertex projected;
			// Native labels sit one tile-height above the item's bottom anchor.
			if (!ProjectTownCameraPoint(ViewCamera.projection,
			        { static_cast<float>(item.position.x), TILE_HEIGHT / PixelsPerWorldUnit,
				        static_cast<float>(item.position.y) }, projected)
			    || projected.x < 0 || projected.y < 0
			    || projected.x >= ViewCamera.width || projected.y >= ViewCamera.height)
				continue;
			ItemLabelAnchors[ItemLabelAnchorCount++] = { slot,
				{ static_cast<int>(std::lround(projected.x)), static_cast<int>(std::lround(projected.y)) }, item.position };
		}
	}
	RendererState.worldMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - worldStart).count();
	diagnosticPublication.worldMicroseconds = static_cast<uint64_t>(RendererState.worldMilliseconds * 1000);
	diagnosticPublication.recordMicroseconds = static_cast<uint64_t>(RendererState.sceneRecordMilliseconds * 1000);
	diag::RecordSessionFrame(diagnosticPublication, diagnosticDecision);
	const auto diagnosticLogState = diag::OpenSession();
	if (diagnosticLogState.error != diag::Error::None) return RefuseRequiredGpuFrame(fullOut, diagnosticLogState);
	CathedralDiagnosticStage = CathedralFallbackStage::SessionTrace;
	if (!forceGeometry) {
		// A bounded real-session trace identifies the effective backend/fallback.
		// World timing deliberately excludes the HUD, SDL presentation and game loop.
		static uint32_t lastReport = 0;
		static bool lastGpu = false;
		static std::string lastFailure;
		const uint32_t now = SDL_GetTicks();
		if (lastReport == 0 || lastGpu != RendererState.usedGpu || lastFailure != RendererState.failure || now - lastReport >= 10000) {
			const auto &gpu = GetTownGpuStatus();
			const auto &mesh = GetTownGpuMeshStats();
			Log("Tristram renderer: GPU={}, adapter={}, mode={}, raster={}x{}, world={:.2f}ms, record={:.2f}ms, draws={}, inputTriangles={}, residentInstances={}, geometryUpload={}B, projectedUpload={}B, failureKind={}, fallback={}",
				RendererState.usedGpu, gpu.adapter, static_cast<int>(CameraRig.mode()), out.w(), out.h(), RendererState.worldMilliseconds,
				RendererState.sceneRecordMilliseconds, gpu.drawCalls, gpu.submittedTriangles, mesh.instances,
				mesh.uploadedVertexBytes + mesh.uploadedIndexBytes, mesh.projectedUploadBytes, static_cast<int>(GpuRecovery.failureKind()), RendererState.failure);
			if (leveltype == DTYPE_TOWN) {
				const auto &scene = GetTownVisualSceneAudit();
				const auto &statics = GetTownVisualObjectAudit();
				LogInfo("Tristram scene frame: status={}, sourceKind={}, revision={}, manifestSHA={}, staticStatus={}, staticSHA={}, fixedActive={}, staticFallback={}, groundSubmissions={}, volumeSubmissions={}, nativeDeltaObjects={}, nativeDeltaRebuilds={}, GPU={}, rendererFallback={}",
				    scene.status, scene.sourceKind, scene.revision, scene.sha256, statics.status, statics.sha256,
				    FixedTownObjectState.active, !FixedTownObjectState.active, FixedTownObjectState.groundSubmissions,
				    FixedTownObjectState.volumeSubmissions, FixedTownObjectState.nativeDeltaObjects,
				    FixedTownObjectState.nativeDeltaRebuilds, RendererState.usedGpu, RendererState.failure);
			}
			lastReport = now;
			lastGpu = RendererState.usedGpu;
			lastFailure = RendererState.failure;
		}
	}
	if (cathedralWorld) {
		RecordCathedralDiagnostic(CathedralFallbackReason::Published3D, CathedralFallbackStage::Published, false, RendererState.requestedGpu);
		TryLogCathedralDiagnostic(); // Only a complete publication acknowledges recovery.
	}
	return true;
}

static bool DiscardCathedralAllocationFailure(const Surface &fullOut, bool requestedGpu)
{
	// Take POD evidence before resetting backend status and host counters.
	RecordCathedralDiagnostic(CathedralFallbackReason::DrawAllocationFailure, CathedralDiagnosticStage, true, requestedGpu);
	CathedralAllocationState.failed = true;
	CathedralAllocationState.gpuSubmittedBeforeFailure = GetTownGpuStatus().submittedTriangles;
	CathedralAllocationState.gpuDrawCallsBeforeFailure = GetTownGpuStatus().drawCalls;
	CathedralAllocationState.cpuRasterizedBeforeFailure = RendererState.cpuRasterizedTriangles;
	// fullOut is borrowed from the native caller. Never touch the sampled out
	// after invalidation releases its owner, even if publication already began.
	const int height = std::max(0, std::min<int>(gnViewportHeight, fullOut.h()));
	if (fullOut.w() > 0 && height > 0)
		ClearSurface(fullOut.subregionY(0, height));
	DrawingCathedral = CathedralPaletteBlend = false;
	CathedralTranslation = nullptr;
	// A failed command push may have left incomplete batch indices. Cancel by
	// reset; calling EndFrame here could execute those invalid commands.
	ClearGpuSceneResources();
	// DecodeSprite updates dimensions before both surface allocations finish.
	// Discard that partially grown scratch and partially prepared host bindings.
	BillboardTextureCache.clear();
    SkullFireFlameCache.clear();
	SpriteSurface.reset();
	SpriteCoverageSurface.reset();
	SpriteSurfaceWidth = SpriteSurfaceHeight = 0;
	InvalidateTownViewFrameForNativeFallback();
	ClearUiOverlayRegions();
#ifndef USE_SDL1
	ResetTownPresentationResources();
#endif
	RasterSampleFactor = 1;
	RendererState.requestedGpu = requestedGpu;
	// The graphics preference is untouched. Recovery was reset, so an unarmed
	// next frame can retry hardware with the same requested camera pose and native scene.
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
		GpuFailure = "Cathedral allocation failed; complete native fallback";
		RendererState.failure = GpuFailure;
	} catch (const std::bad_alloc &) {
		// Heap pressure must not throw again while reporting the first failure.
		// The POD failed marker above remains authoritative without text storage.
		GpuFailure.clear();
		RendererState.failure.clear();
	}
#endif
	TryLogCathedralDiagnostic(); // Protected formatting after fail-closed cleanup.
	return false; // DrawGame owns the complete native redraw and input fallback.
}

bool DrawTownView(const Surface &fullOut, bool forceGeometry)
{
	// Preserve the Town/Ogden draw body and failure branches exactly. Only the
	// native Cathedral transaction gets this recoverable allocation boundary.
	if (!cathedral::LiveLevelEligible() || !CathedralFrameReady) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		try { return DrawTownViewFrame(fullOut, forceGeometry); }
		catch (const std::bad_alloc &) {
			if (RequiredGpu() && DiagnosticEligibleFrame) return RefuseRequiredGpuFrame(fullOut, { diag::Error::CapacityExceeded, diag::Stage::WorldSubmit });
			throw; // Preserve the original non-GPU/native/Town exception contract.
		}
#else
		return DrawTownViewFrame(fullOut, forceGeometry);
#endif
	}
	const bool requestedGpu = EffectiveGpuRequested();
	CathedralAllocationState.failed = false;
	CathedralAllocationState.injected = TownViewCathedralAllocationFailurePoint::None;
	CathedralAllocationState.gpuSubmittedBeforeFailure = 0;
	CathedralAllocationState.gpuDrawCallsBeforeFailure = 0;
	CathedralAllocationState.cpuRasterizedBeforeFailure = 0;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
		return DrawTownViewFrame(fullOut, forceGeometry);
	} catch (const std::bad_alloc &) {
		if (RequiredGpu() && DiagnosticEligibleFrame) {
			CathedralAllocationState.failed = true;
			CathedralAllocationState.gpuSubmittedBeforeFailure = GetTownGpuStatus().submittedTriangles;
			CathedralAllocationState.gpuDrawCallsBeforeFailure = GetTownGpuStatus().drawCalls;
			CathedralAllocationState.cpuRasterizedBeforeFailure = RendererState.cpuRasterizedTriangles;
			return RefuseRequiredGpuFrame(fullOut, { diag::Error::CapacityExceeded, diag::Stage::WorldSubmit });
		}
		return DiscardCathedralAllocationFailure(fullOut, requestedGpu);
	}
#else
	return DrawTownViewFrame(fullOut, forceGeometry);
#endif
}

void SetTownViewCathedralAllocationFailureForDiagnostics(TownViewCathedralAllocationFailurePoint point) noexcept
{
	switch (point) {
	case TownViewCathedralAllocationFailurePoint::BeforeOpaque:
	case TownViewCathedralAllocationFailurePoint::AfterOpaque:
	case TownViewCathedralAllocationFailurePoint::BeforeReadback:
		CathedralAllocationState.armed = point;
		break;
	default:
		CathedralAllocationState.armed = TownViewCathedralAllocationFailurePoint::None;
		break;
	}
}

TownViewCathedralAllocationState GetTownViewCathedralAllocationState() noexcept
{
	return CathedralAllocationState;
}

TownViewOgdenPilotState GetTownViewOgdenPilotState()
{
	return OgdenPilotState;
}

void SetTownViewOgdenGpuSubmissionFailureForDiagnostics(bool enabled)
{
	FailOgdenGpuSubmissionForDiagnostics = enabled;
}

int TownViewArchitectureAt(Point screen)
{
	if (!IsTownViewActive() || !CurrentPickingValid() || CachedDungeonData != pDungeonCels.get()
		|| screen.x < 0 || screen.y < 0 || screen.x >= ViewCamera.width || screen.y >= ViewCamera.height)
		return -1;
	return PickBuffer[static_cast<size_t>(screen.y) * ViewCamera.width + screen.x].architecture;
}

float TownViewDepthAt(Point screen)
{
	if (!IsTownViewActive() || !CurrentPickingValid() || CachedDungeonData != pDungeonCels.get()
		|| screen.x < 0 || screen.y < 0 || screen.x >= ViewCamera.width || screen.y >= ViewCamera.height)
		return std::numeric_limits<float>::infinity();
	return DepthBuffer[static_cast<size_t>(screen.y) * ViewCamera.width + screen.x];
}

bool PickTownView(Point screen, Point &tile, int &townerIndex, int &itemIndex, int &playerIndex)
{
	townerIndex = -1;
	itemIndex = -1;
	playerIndex = -1;
	TownViewPickResult result;
	if (!PickTownViewDetailed(screen, result))
		return false;
	tile = result.tile;
	townerIndex = result.townerIndex;
	itemIndex = result.itemIndex;
	playerIndex = result.playerIndex;
	return true;
}

bool PickTownViewDetailed(Point screen, TownViewPickResult &result)
{
	result = {};
	if (!IsTownViewActive() || !CurrentPickingValid() || CachedDungeonData != pDungeonCels.get()
		|| screen.x < 0 || screen.y < 0 || screen.x >= ViewCamera.width || screen.y >= ViewCamera.height)
		return false;
	const PickRecord &pick = PickBuffer[static_cast<size_t>(screen.y) * ViewCamera.width + screen.x];
	if (pick.tile == std::numeric_limits<uint16_t>::max())
		return false;
	result.tile = { pick.tile % MAXDUNX, pick.tile / MAXDUNX };
	switch (pick.kind) {
	case PickKind::Ground: break;
	case PickKind::Towner: result.townerIndex = pick.entity; break;
	case PickKind::Item:
		if (cathedral::LiveLevelEligible() && (pick.entity < 0 || pick.entity >= MAXITEMS
		        || Items[pick.entity].isEmpty() || Items[pick.entity].position != result.tile
		        || Items[pick.entity].selectionRegion == SelectionRegion::None
		        || dItem[result.tile.x][result.tile.y] != pick.entity + 1))
			return false;
		result.itemIndex = pick.entity;
		break;
	case PickKind::Player:
		if (cathedral::LiveLevelEligible()) {
			if (pick.entity < 0 || static_cast<size_t>(pick.entity) >= Players.size())
				return false;
			const Player &player = Players[pick.entity];
			if (!player.plractive || !player.isOnActiveLevel() || player.position.tile != result.tile
			    || (std::abs(dPlayer[result.tile.x][result.tile.y]) != pick.entity + 1
			        && !(player.hasNoLife() && TileContainsDeadPlayer(result.tile))))
				return false;
		}
		result.playerIndex = pick.entity;
		break;
	case PickKind::Object:
		if (!cathedral::LiveLevelEligible() || pick.entity < 0 || pick.entity >= MAXOBJECTS
		    || !Objects[pick.entity].canInteractWith() || Objects[pick.entity].position != result.tile
		    || std::abs(dObject[result.tile.x][result.tile.y]) != pick.entity + 1)
			return false;
		result.objectIndex = pick.entity;
		break;
	case PickKind::Monster:
		if (!cathedral::LiveLevelEligible() || pick.entity < 0 || static_cast<size_t>(pick.entity) >= MaxMonsters
		    || !IsTileLit(result.tile) || Monsters[pick.entity].hasNoLife() || Monsters[pick.entity].isPlayerMinion()
		    || std::abs(dMonster[result.tile.x][result.tile.y]) != pick.entity + 1
		    || (Monsters[pick.entity].flags & MFLAG_HIDDEN) != 0 || Monsters[pick.entity].position.tile != result.tile)
			return false;
		result.monsterIndex = pick.entity;
		break;
	}
	return true;
}

bool PickTownViewStandingAttack(Point screen, TownViewPickResult &result)
{
	if (PickTownViewDetailed(screen, result))
		return true;
	result = {};
	if (CameraRig.mode() != TownCameraMode::FirstPerson || MyPlayer == nullptr
	    || !IsTownViewActive() || !CurrentPickingValid() || CachedDungeonData != pDungeonCels.get()
	    || screen.x < 0 || screen.y < 0 || screen.x >= ViewCamera.width || screen.y >= ViewCamera.height)
		return false;
	// Quantize only the published horizontal view direction to a native adjacent
	// tile. Pitch, empty ceiling/sky and depth are not attack-cadence controls.
	const auto &forward = ViewCamera.projection.forward;
	if (!std::isfinite(forward.x) || !std::isfinite(forward.z)
	    || std::hypot(forward.x, forward.z) < 0.001F)
		return false;
	constexpr float Octant = 0.7853981633974483F;
	const float angle = std::round(std::atan2(forward.z, forward.x) / Octant) * Octant;
	result.tile = MyPlayer->position.tile + Displacement {
		static_cast<int>(std::lround(std::cos(angle))), static_cast<int>(std::lround(std::sin(angle))) };
	return InDungeonBounds(result.tile);
}

uint64_t TownViewItemLabelFrameId()
{
	return IsTownViewActive() && !IsTownViewNativePose() && CurrentPickingValid()
	        && CachedDungeonData == pDungeonCels.get()
	    ? ItemLabelFrameId : 0;
}

std::span<const TownViewItemLabelAnchor> GetTownViewItemLabelAnchors()
{
	if (TownViewItemLabelFrameId() == 0)
		return {};
	return { ItemLabelAnchors.data(), ItemLabelAnchorCount };
}

Point TownViewScreenPosition(Point tile)
{
	if (!IsTownViewActive() || ViewCamera.width == 0)
		return { -10000, -10000 };
	TownCameraProjectedVertex projected;
	if (!ProjectTownCameraPoint(ViewCamera.projection,
	        { static_cast<float>(tile.x) - 0.5F, 0, static_cast<float>(tile.y) - 0.5F }, projected))
		return { -10000, -10000 };
	return { static_cast<int>(std::lround(projected.x)), static_cast<int>(std::lround(projected.y)) };
}

TownViewFixedObjectState GetTownViewFixedObjectState()
{
	return FixedTownObjectState;
}

bool ExportTownViewFixedObjects(const std::filesystem::path &directory, std::string &failure)
{
	try {
		std::error_code error;
		if (std::filesystem::exists(directory / "tristram.d3dvis", error)
		    || std::filesystem::exists(directory / "town-visual-objects.json", error)
		    || std::filesystem::exists(directory / "palette.rgb", error)) {
			failure = "static-export-exists"; return false;
		}
		TownVisualObjectSet set;
		if (!CaptureFixedTownObjects(set, failure)) return false;
		std::vector<std::byte> bytes;
		if (!EncodeTownVisualObjects(set, bytes, failure)) return false;
		std::filesystem::create_directories(directory, error);
		if (error) { failure = "static-export-directory"; return false; }
		const std::string sha256 = ModHashToHex(ComputeBytesSha256(bytes));
		std::ofstream binary(directory / "tristram.d3dvis", std::ios::binary);
		binary.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())); binary.close();
		if (!binary) { failure = "static-export-write"; return false; }
		std::ofstream palette(directory / "palette.rgb", std::ios::binary);
		for (const auto &color : logical_palette) {
			const std::array<uint8_t, 3> rgb { color.r, color.g, color.b };
			palette.write(reinterpret_cast<const char *>(rgb.data()), rgb.size());
		}
		palette.close();
		if (!palette) { failure = "static-export-palette"; return false; }
		std::ofstream json(directory / "town-visual-objects.json", std::ios::binary);
		json.imbue(std::locale::classic());
		json << std::setprecision(std::numeric_limits<float>::max_digits10)
		     << "{\"format\":\"d3d.fixed-town-objects\",\"schemaVersion\":1,\"private\":true,\"archive\":\"tristram.d3dvis\",\"archiveSha256\":\""
		     << sha256 << "\",\"palette\":\"palette.rgb\",\"gridSize\":[112,112],\"textureCount\":" << set.textures.size()
		     << ",\"coordinates\":\"native-anchor-plus-fixed-sprite-ray-frame\",\"objects\":[";
		size_t offset = 28;
		for (const auto &texture : set.textures)
			offset += 12 + texture.pixels.size() + texture.opacity.size();
		bool first = true;
		for (const auto &object : set.objects) {
			json << (first ? "" : ",") << "{\"id\":\"" << object.id << "\",\"kind\":" << static_cast<unsigned>(object.kind)
			     << ",\"anchor\":[" << object.anchor.x << ',' << object.anchor.y << "],\"transform\":["
			     << object.transform.x << ',' << object.transform.height << ',' << object.transform.z << ',' << object.transform.yaw << ',' << object.transform.scale
			     << "],\"transformOffset\":" << offset + 4 + object.id.size() + 12 << ",\"texture\":" << object.texture
			     << ",\"triangles\":" << object.mesh.triangles.size() << ",\"phase\":" << static_cast<unsigned>(object.phase)
			     << ",\"visible\":" << (object.visible ? "true" : "false") << ",\"nativeSuppressed\":" << (object.nativeSuppressed ? "true" : "false")
			     << ",\"groundFallback\":" << (object.groundFallback ? "true" : "false") << ",\"eventBinding\":" << static_cast<unsigned>(object.eventBinding)
			     << ",\"eventState\":" << (object.eventState ? "true" : "false")
			     << ",\"sourceCells\":[";
			bool firstCell = true;
			for (const auto &cell : object.sourceCells) {
				json << (firstCell ? "" : ",") << '[' << cell.tile.x << ',' << cell.tile.y << ',' << cell.piece << ',' << cell.special << ']';
				firstCell = false;
			}
			json << "]}";
			first = false;
			offset += 4 + object.id.size() + 56 + object.sourceCells.size() * 8 + object.mesh.triangles.size() * 64;
		}
		json << "]}\n"; json.close();
		if (!json || offset != bytes.size()) { failure = "static-export-json"; return false; }
		failure.clear(); return true;
	} catch (const std::bad_alloc &) { failure = "static-export-allocation"; return false; }
}

TownGroundReferenceTexture GetTownGroundReferenceTexture(uint16_t piece, bool fallback)
{
	TownGroundReferenceTexture result;
	if (!pDungeonCels || piece >= MAXTILES)
		return result;
	const Texture &texture = fallback ? FallbackGround() : SceneGround(piece);
	result.width = texture.width;
	result.height = texture.height;
	result.rgba.resize(static_cast<size_t>(texture.width) * texture.height * 4);
	for (size_t i = 0; i < texture.pixels.size(); ++i) {
		const SDL_Color color = logical_palette[texture.pixels[i]];
		result.rgba[4 * i] = color.r;
		result.rgba[4 * i + 1] = color.g;
		result.rgba[4 * i + 2] = color.b;
		result.rgba[4 * i + 3] = texture.opacity.empty() || texture.opacity[i] != 0 ? 255 : 0;
	}
	return result;
}

bool DrawTownViewTileDiagnostic(const Surface &out, Point tile)
{
	if (!pDungeonCels || !InDungeonBounds(tile))
		return false;
	const uint16_t piece = dPiece[tile.x][tile.y];
	if (piece >= MAXTILES || out.w() < 256 || out.h() < 256)
		return false;
	ClearSurface(out);
	const TileArt &art = GetTile(piece);
	for (int y = 0; y < art.facade.height; ++y)
		std::memcpy(out.at(0, y), art.facade.pixels.data() + static_cast<size_t>(y) * art.facade.width, art.facade.width);
	const Texture &ground = SceneGround(piece);
	for (int y = 0; y < ground.height; ++y)
		std::memcpy(out.at(160, y), ground.pixels.data() + static_cast<size_t>(y) * ground.width, ground.width);
	const std::vector<uint8_t> lighting(static_cast<size_t>(out.pitch()) * out.h(), 0);
	const Lightmap lightmap(out.begin(), lighting, out.pitch(), LightTables, FullyLitLightTable, FullyDarkLightTable);
	const MICROS &micros = DPieceMicros[piece];
	for (int i = 0; i < std::min<int>(MicroTileLen, 16); ++i) {
		const LevelCelBlock block = micros.mt[i];
		if (!block.hasValue()) continue;
		const Point anchor { 80 + (i & 1) * 32, 255 - (i / 2) * 32 };
		if (!art.solid && i < 2) {
			if (block.type() == TileType::TransparentSquare)
				RenderTileFoliage(out, lightmap, anchor, pDungeonCels.get(), block, FullyLitLightTable);
		} else {
			RenderTile(out, lightmap, anchor, pDungeonCels.get(), block, MaskType::Solid, FullyLitLightTable);
		}
	}
	return true;
}

const TownVolumeMesh *TownViewVegetationVolume(size_t groupIndex)
{
	const auto &groups = GetTownVegetationGroups();
	if (!pDungeonCels || groupIndex >= groups.size())
		return nullptr;
	if (VegetationArtworkCache.find(groupIndex) == VegetationArtworkCache.end())
		VegetationArtworkCache.emplace(groupIndex, ComposeVegetation(groups[groupIndex]));
	return &VegetationArtworkCache.at(groupIndex).mesh;
}

const TownVolumeMesh *TownViewPropVolume(size_t groupIndex)
{
	const auto &groups = GetTownPropGroups();
	if (!pDungeonCels || groupIndex >= groups.size())
		return nullptr;
	if (PropArtworkCache.find(groupIndex) == PropArtworkCache.end())
		PropArtworkCache.emplace(groupIndex, ComposeNativeScenery(groups[groupIndex], false));
	return &PropArtworkCache.at(groupIndex).mesh;
}

bool DrawTownViewVegetationDiagnostic(const Surface &out, size_t groupIndex, Point *sourceSize)
{
	if (TownViewVegetationVolume(groupIndex) == nullptr)
		return false;
	const Texture &texture = VegetationArtworkCache.at(groupIndex).texture;
	if (sourceSize != nullptr)
		*sourceSize = { texture.width, texture.height };
	if (texture.width * 2 + 16 > out.w() || texture.height > out.h())
		return false;
	ClearSurface(out);
	for (int y = 0; y < texture.height; ++y) {
		std::memcpy(out.at(0, y), texture.pixels.data() + static_cast<size_t>(y) * texture.width, texture.width);
		for (int x = 0; x < texture.width; ++x)
			out[{ texture.width + 16 + x, y }] = texture.opacity[static_cast<size_t>(y) * texture.width + x] != 0 ? 255 : 0;
	}
	return true;
}

TownViewSarcophagusInfo GetTownViewSarcophagusInfo()
{
    return {Sarcophagus.loaded,Sarcophagus.body.triangles.size(),Sarcophagus.lid.triangles.size(),Sarcophagus.draws};
}

TownViewBarrelInfo GetTownViewBarrelInfo()
{
    size_t triangles=0;for(const auto &part:Barrel.parts)triangles+=part.triangles.size();
    return {Barrel.loaded,triangles,Barrel.meshDraws,Barrel.nativeBreakDraws,Barrel.nativeExplosionDraws,Barrel.shardDraws,Barrel.terminalShardDraws};
}

size_t GetTownViewCorpseGroundDraws(){return CorpseGroundDraws;}

} // namespace devilution

namespace devilution {
void SetTownViewPropAnimationTimeForDiagnostics(int64_t milliseconds){PropAnimationDiagnosticTime=milliseconds<0?-1:std::min<int64_t>(milliseconds,UINT32_MAX);}
TownViewChestInfo GetTownViewChestVariantInfo(int variant){
    if(variant<1 || variant>2)return GetTownViewChestInfo();
    const auto &v=ChestVariants[variant-1];size_t triangles=0;for(const auto &part:v.parts)triangles+=part.triangles.size();return {v.loaded,triangles,Chest.draws};
}
TownViewChestInfo GetTownViewChestInfo(){size_t triangles=0;for(const auto &part:Chest.parts)triangles+=part.triangles.size();return {Chest.loaded,triangles,Chest.draws};}
float GetTownViewChestOpenProgress(int slot){return slot>=0 && slot<MAXOBJECTS?Chest.states[slot].progress:0;}
}

namespace devilution {
TownViewDoorModelInfo GetTownViewDoorModelInfo(){return {DoorModels.loaded,DoorModels.models[0].triangles.size(),DoorModels.models[1].triangles.size(),DoorModels.leafDraws,DoorModels.archDraws,DoorModels.residentDraws};}
float GetTownViewDoorOpenProgress(int slot){return slot>=0 && slot<MAXOBJECTS?DoorModels.states[slot].progress:0;}
}

namespace devilution { TownViewBrazierInfo GetTownViewBrazierInfo(){return {Brazier.loaded,Brazier.model.triangles.size(),Brazier.meshDraws,Brazier.flameDraws,BrazierFlameContact};} }

namespace devilution { TownViewSkullFireInfo GetTownViewSkullFireInfo(){float lo=0,hi=0;if(SkullFire.loaded){lo=std::numeric_limits<float>::infinity();for(const auto &tr:SkullFire.model.triangles)for(const auto &v:tr.vertices){lo=std::min(lo,v.height);hi=std::max(hi,v.height);}}size_t covered=0;for(const auto &[key,art]:SkullFireFlameCache)covered+=std::count_if(art.texture.opacity.begin(),art.texture.opacity.end(),[](uint8_t c){return c!=0;});return {SkullFire.loaded,SkullFire.model.triangles.size(),SkullFire.meshDraws,SkullFire.flameDraws,lo,hi,SkullFireFlameHeight,covered};} }
