#include "engine/render/town_view.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
#include "diablo.h"
#include "game_mode.hpp"
#include "mods/mod_identity.h"
#include "nthread.h"
#include "engine/render/actor_model.hpp"
#include "engine/render/actor_pose.hpp"
#include "engine/render/actor_visual_snapshot.hpp"
#include "engine/render/ogden_idle_pilot.hpp"
#include "engine/render/ogden_idle_native_clock.hpp"
#include "dead.h"
#include "engine/assets.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/light_tables.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/cathedral/cathedral_frame.hpp"
#include "engine/render/cathedral/cathedral_live.hpp"
#include "engine/render/cathedral/cathedral_native_palette.hpp"
#include "engine/render/cathedral/cathedral_native_coverage.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/town_ground_shadow.hpp"
#include "engine/render/town_gpu.hpp"
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
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "objects.h"
#include "options.h"
#include "player.h"
#include "utils/log.hpp"
#include "utils/palette_blending.hpp"
#include "towners.h"
#include "utils/ui_fwd.h"

namespace devilution {
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

struct Texture {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> pixels;
	bool repeat = false;
	std::vector<uint8_t> opacity;
	// Imported base color stays independent of the game palette until lighting.
	std::vector<uint32_t> albedoPixels;
	// Interior materials use a small point-irradiance LUT and clean base colors.
	std::vector<std::array<uint8_t, ImportedLightLevels>> interiorLightTables;
	bool emissive = false;
	TextureGpuIdentity gpuIdentity;
	bool sample(float u, float v, uint8_t &color, uint32_t *albedoColor = nullptr) const
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
cathedral::PilotFrame CathedralFrame;
bool CathedralFrameReady = false;
bool DrawingCathedral = false;
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
	uint64_t lastSeen = 0;
};
std::unordered_map<uint64_t, PreparedGpuTexture> PreparedGpuTextures;
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
using CathedralNativeTextureKey = std::tuple<cathedral::NativeTextureKind, uint16_t, cathedral::NativeTextureAxis, uint8_t, int>;
struct CathedralNativeTextureEntry {
	Texture texture;
	uint64_t signature = 0;
	uint16_t sourcePiece = 0;
	uint8_t sourceMicro = 0, sourceColumn = 0;
	uint16_t sourceBlock = 0, secondBlock = 0;
	int cropX = 0, cropY = 0;
	bool approximate = false;
	bool horizontalFlip = false;
};
constexpr size_t CathedralNativeTextureMaxEntries = 512;
constexpr size_t CathedralNativeTextureMaxBytes = 8 * 1024 * 1024;
std::map<CathedralNativeTextureKey, CathedralNativeTextureEntry> CathedralNativeTextureCache;
TownViewCathedralNativeTextureState CathedralNativeTextureState;
const std::byte *CathedralNativeTextureResource = nullptr;
uint_fast8_t CathedralNativeTextureMicroLen = 0;
std::unordered_map<uint16_t, Texture> SceneGroundCache;
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
std::unordered_map<uint32_t, uint32_t> ImportedAlbedoColors;
std::vector<std::array<uint8_t, ImportedLightLevels>> ImportedAlbedoLightTables;
TownLightingConfig SceneLightingConfig = TristramLightingConfig();
bool SceneLightingProfileLoaded = false;
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
};
std::unordered_map<const uint8_t *, BillboardArtwork> BillboardTextureCache;
std::unordered_map<size_t, VolumeArtwork> VegetationArtworkCache;
std::unordered_map<size_t, VolumeArtwork> PropArtworkCache;
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
	constexpr std::array<float, 4> Brightness { 1.0F, 0.93F, 0.84F, 0.74F };
	// Match darkened RGB colors back into the actual Tristram palette. Its first
	// 128 entries are not ordered shade ramps, unlike the dungeon palettes.
	for (size_t shade = 0; shade < Brightness.size(); ++shade) {
		for (size_t index = 0; index < logical_palette.size(); ++index) {
			if (shade == 0 || index == 0) {
				SceneLightTables[shade][index] = static_cast<uint8_t>(index);
				continue;
			}
			const SDL_Color color = logical_palette[index];
			const float red = color.r * Brightness[shade];
			const float green = color.g * Brightness[shade];
			const float blue = color.b * Brightness[shade];
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
				const float brightness = Brightness[shade] * ShadowBrightness[shadow];
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

void ResetFollowCameraHistory(bool clearIndex = false)
{
	HasSafeCameraEye = false;
	FollowCameraState = {};
	CameraFollowFrameSeconds = 0;
	++FollowProjectionRevision;
	PickingValid = false;
	if (clearIndex) {
		CameraCollisionIndex.Clear();
		CameraCollisionSceneRevision = std::numeric_limits<uint64_t>::max();
	}
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
	PrepareCameraCollisionIndex();
	const TownViewFollowCameraState previous = FollowCameraState;
	TownViewFollowCameraState state;
	state.active = true;
	state.transition = CameraRig.IsVisualTransitionActive();
	state.desiredDistance = CameraRig.pose().distance;
	state.visualDistance = CameraRig.VisualDistance();
	state.eyeHeight = CameraRig.VisualEyeHeight();
	state.radius = TownCameraCollisionRadius(frame);
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
	state.localPlayerHidden = CameraRig.HideLocalPlayer(state.resolvedDistance)
	    || (previous.localPlayerHidden && state.resolvedDistance < 0.85F);
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
	const bool albedo = !texture.emissive && !texture.albedoPixels.empty();
	if (inserted) {
		if (albedo)
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
	result.opacity = texture.opacity;
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
		result.lightLutRevision = GpuImportedLutColors;
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
	const bool hasAlbedo = !texture.albedoPixels.empty();
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
	const bool receivesShadow = !DrawingCathedral && DirectionalShadowsEnabledForDiagnostics && interior == nullptr && !texture.emissive
	    && (hasAlbedo ? diffuse > 0 : receivesDirectLight);
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
		if (interior != nullptr) {
			material.lights = { interior->lights.data(), interior->lightCount };
			material.interiorRedNormalization = interior->room->fireColor.red;
			material.interiorPointRange = InteriorPointLightRange;
			const std::span<const TownLightOccluder> occluders(interior->room->lightOccluders);
			material.room = &occluders.front();
			material.blockers = occluders.subspan(1);
		}
		const bool constantPalette = !hasAlbedo && texture.opacity.empty()
		    && texture.width == 1 && texture.height == 1 && texture.pixels.size() == 1;
		if (constantPalette)
			material.repeat = false;
		const auto vertex = [&](ProjectedVertex p, Vec3 w) {
			if (constantPalette) {
				p.u = (static_cast<float>(texture.pixels[0]) + 0.5F) / 256;
				p.v = 0.5F;
			}
			return TownGpuVertex { p.x, p.y, p.depth, p.u, p.v, { w.x, w.y, w.z }, ViewCamera.projection.perspective ? p.depth : 1.0F };
		};
		CaptureGpuFailed = !TownGpuSubmitProjectedTriangle({ vertex(a, worldA), vertex(b, worldB), vertex(c, worldC) },
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
			if (!texture.sample(u, v, color, &albedoColor) || (transparent && !hasAlbedo && texture.opacity.empty() && color == 0))
				continue;
			const uint8_t previousColor = destination[x];
			if (DrawingCathedral && CathedralTranslation != nullptr)
				color = CathedralTranslation[color];
			const Vec3 world = worldA * ca + worldB * cb + worldC * cc;
			const float shadow = receivesShadow
			    ? SampleTownShadow(world.x, world.y, world.z, shadowReceiver) : 0;
			const int shadowLevel = std::clamp(static_cast<int>(shadow * 3.0F + 0.5F), 0, 3);
			if (texture.emissive) {
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
			if (DrawingCathedral && CathedralPaletteBlend) {
				destination[x] = paletteTransparencyLookup[previousColor][destination[x]];
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
		world[i] = { { triangle[i].position.x, triangle[i].position.y, triangle[i].position.z }, triangle[i].u, triangle[i].v };
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
	result.projectionOwners = BuildTownSceneProjectionOwners(model, result.referenceTile, result.pixelOrigin,
		result.texture.width, result.texture.height, GetTownScene());
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
	return SceneArtworkCache.emplace(index, ComposeNativeArtwork(model)).first->second;
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
	HorizonFogPalette = BuildTownHorizonFogPalette(linearPalette, HorizonFog.color);
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
		HorizonSkyColors[i] = ClosestSceneColor(color.red * 255, color.green * 255, color.blue * 255);
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
			const float depth = DepthBuffer[static_cast<size_t>(y) * out.w() + x];
			if (!std::isfinite(depth)) {
				destination[x] = HorizonSkyColors[sky];
				continue;
			}
			const float fogDepth = TownCameraFogDepth(ViewCamera.projection, depth);
			if (fogDepth <= HorizonFog.startDepth)
				continue;
			const size_t step = static_cast<size_t>(std::min(fogDepth, HorizonFog.endDepth) * HorizonDepthSteps);
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
					const int sampleX = static_cast<int>(std::floor(nativeX - art.pixelOrigin.x));
					const int sampleY = static_cast<int>(std::floor(nativeY - art.pixelOrigin.y));
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
	return SceneMaterialCache.emplace(index, BuildSceneMaterials(model, art)).first->second;
}

const Texture &ImportedRgbTexture(size_t index, uint32_t width, uint32_t height, std::span<const uint8_t> rgbBytes)
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

const Texture &ImportedSceneTexture(size_t index, const TownImportedTexture &source)
{
	return ImportedRgbTexture(index, source.width, source.height, source.rgb);
}

Texture DecodeSprite(ClxSprite sprite);
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
	if (towner._ttype != TOWN_TAVERN || !OgdenPilot.ready)
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
				return Vec3 { input.authoritativeFootpoint.x + (2 * py + px) / 64, 0.001F,
					input.authoritativeFootpoint.z + (2 * py - px) / 64 };
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
				const Vec3 point = rotate(pose.positions[vertex]);
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
			lighting.lights[i] = TownFireLightAtTime(source.light, source.flickerSeed, FrameFireTime, FireIntensityVariation);
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
			ImportedSceneTexture(index, *model.importedTexture);
	}
	// Freeze the append-only color table before any GPU batch snapshots it.
	// One cold frame then shares a single LUT revision instead of pinning a
	// larger copy after each newly encountered building. Warm frames reuse both.
}

void DrawScene(const Surface &out)
{
	const std::vector<TownSceneModel> &scene = GetTownScene();
	PrepareArchitectureBoundsCache(scene);
	ArchitectureCullingState.modelsConsidered = 0;
	ArchitectureCullingState.modelsSubmitted = 0;
	ArchitectureCullingState.modelsCulled = 0;
	ArchitectureCullingState.trianglesVisited = 0;
	for (size_t index = 0; index < scene.size(); ++index) {
		const TownSceneModel &model = scene[index];
		++ArchitectureCullingState.modelsConsidered;
		if (!IsArchitectureVisible(index, model)) {
			++ArchitectureCullingState.modelsCulled;
			continue;
		}
		++ArchitectureCullingState.modelsSubmitted;
		if (model.externalModel && model.importedTexture) {
			const Texture &texture = ImportedSceneTexture(index, *model.importedTexture);
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
			DrawTriangle(out, vertices, detailed ? detail.texture : materials.base[static_cast<size_t>(triangle.material)], pick, shade);
		}
		for (size_t triangleIndex = 0; triangleIndex < model.triangles.size(); ++triangleIndex) {
			++ArchitectureCullingState.trianglesVisited;
			const TownSceneTriangle &triangle = model.triangles[triangleIndex];
			const NativeSceneFace &face = art.projectedFaces[triangleIndex];
			if (!triangle.nativeProjection || face.texture.width == 0)
				continue;
			const Vec3 toEye = ViewCamera.eye - Vec3 { triangle.vertices[0].x, triangle.vertices[0].height, triangle.vertices[0].z };
			if (triangle.normal.x * toEye.x + triangle.normal.height * toEye.y + triangle.normal.z * toEye.z <= 0)
				continue;
			std::array<Vertex, 3> vertices;
			for (size_t i = 0; i < vertices.size(); ++i) {
				const TownSceneVertex &vertex = triangle.vertices[i];
				const float nativeX = 32 * (vertex.x - vertex.z - art.referenceTile.x + art.referenceTile.y);
				const float nativeY = 16 * (vertex.x + vertex.z - art.referenceTile.x - art.referenceTile.y) - 32 * vertex.height;
				vertices[i] = { { vertex.x, vertex.height, vertex.z },
					(nativeX - face.pixelOrigin.x) / face.texture.width, (nativeY - face.pixelOrigin.y) / face.texture.height };
			}
			// Original artwork constrains the authored front. Every other surface
			// has its own continuous opaque material and no projected-photo holes.
			PickRecord pick = PickAt(triangle.pickTile);
			pick.architecture = static_cast<int16_t>(index);
			DrawTriangle(out, vertices, face.texture, pick, 0, true);
		}
	}
	ArchitectureCullingState.cachedBounds = static_cast<size_t>(std::count_if(ArchitectureBoundsCache.begin(), ArchitectureBoundsCache.end(),
	    [](const CachedArchitectureBounds &cached) { return cached.ready; }));
}

void DrawGround(const Surface &out, Point tile, const Texture &ground)
{
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

void DrawVolume(const Surface &out, Vec3 position, Point tile, const VolumeArtwork &art, PickKind kind, int entity, int lighting, bool residentAllowed = true)
{
	constexpr float InverseSqrt2 = 0.7071067811865475F;
	const auto world = [&](const TownVolumeVertex &vertex) {
		// The frame stays in world space. Its depth follows the native viewing ray,
		// preserving the authored front's silhouette without following the camera.
		return Vec3 { position.x + (vertex.x + vertex.z) * InverseSqrt2,
			position.y + vertex.height + (art.physicalFrame ? 0 : vertex.z * InverseSqrt2 - 1 / PixelsPerWorldUnit),
			position.z + (-vertex.x + vertex.z) * InverseSqrt2 };
	};
	// A ground decal can enter the frame while its body is outside it.
	if (!art.shadow.pixels.empty()) {
		const auto onGround = [&](float u, float v) {
			const float px = (u - 0.5F) * art.shadow.width;
			const float py = v * art.shadow.height - art.shadow.height + 1;
			return Vec3 { position.x + (2 * py + px) / 64, 0.001F,
				position.z + (2 * py - px) / 64 };
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
	if (CaptureGpu && ResidentMeshesEnabledForDiagnostics && ViewCamera.projection.perspective && residentAllowed && kind == PickKind::Ground) {
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
		const int shade = std::clamp(lighting + (light < -0.1F ? 2 : (light < 0.4F ? 1 : 0)), 0, 3);
		if (CaptureGpu && triangle.material == TownVolumeMaterial::SpriteFront) {
			const Texture &texture = art.physicalFrame && !art.directionalTextures[triangle.textureView].pixels.empty()
			    ? art.directionalTextures[triangle.textureView] : art.texture;
			// The CPU draws a solid backing and a cutout at identical depth. The
			// GPU samples both choices in one pass, preserving their exact order
			// without a texture switch and draw call for every small front face.
			DrawTriangle(out, vertices, texture, pick, lighting, true, nullptr, nullptr,
				{ triangle.paletteIndex, shade });
			continue;
		}
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
	const float x = static_cast<float>(tile.x);
	const float z = static_cast<float>(tile.y);
	const int lighting = dLight[tile.x][tile.y];
	if (art.solid)
		// The bottom MIN row of a solid object may contain its painted base
		// (notably the well rim). Its new body supplies that art in space.
		DrawGround(out, tile, TownSceneReplacesTile(tile) || TownPropReplacesTile(tile) ? fallback : SceneGround(dPiece[tile.x][tile.y]));
	// A tree's MIN base and its delayed CLX crown are one object. The solid tree
	// volume below replaces both; retaining this cell proxy duplicates its trunk.
	if (TownVegetationReplacesTile(tile) || TownSceneReplacesTile(tile) || TownPropReplacesTile(tile) || art.height < 0.25F)
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

Texture DecodeSprite(ClxSprite sprite)
{
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

CathedralNativeTextureKey NativeTextureKey(const cathedral::NativeTextureBinding &binding)
{
	return { binding.kind, binding.piece,
		binding.kind == cathedral::NativeTextureKind::Masonry ? binding.axis : cathedral::NativeTextureAxis::Horizontal, binding.column,
		binding.kind == cathedral::NativeTextureKind::DoorWood ? binding.nativeSlot : -1 };
}

void ResetCathedralNativeTextureCache()
{
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
	uint64_t hash = 14695981039346656037ULL;
	const auto mix = [&](uint64_t value) {
		for (unsigned n = 0; n < 8; ++n) { hash ^= (value >> (n * 8)) & 255; hash *= 1099511628211ULL; }
	};
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
		entry.texture = DecodeCathedralCoverage(64, 32, [&](const Surface &out, const Lightmap &light, const uint8_t *table) {
			for (int column = 0; column < 2; ++column)
				RenderTileFrame(out, light, { column * 32, 31 }, column == 0 ? TileType::LeftTriangle : TileType::RightTriangle,
					GetDunFrame(pDungeonCels.get(), micros.mt[column].frame()), DunFrameTriangleHeight, MaskType::Solid, table);
		}); // Exact native DrawFloorTile semantics after SetDungeonMicros.
	} else if (binding.kind == NativeTextureKind::Masonry) {
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
	if (entry.texture.opacity.empty() || std::none_of(entry.texture.opacity.begin(), entry.texture.opacity.end(),
	        [](uint8_t coverage) { return coverage != 0; }))
		throw std::invalid_argument("Cathedral material has no covered native texels");
	return entry;
}

void PrepareCathedralNativeTextures(const cathedral::PilotFrame &frame)
{
	if (frame.gameRevision == 0 || !pDungeonCels || MicroTileLen < 2)
		throw std::invalid_argument("Cathedral texture preparation requires a loaded native epoch");
	if (CathedralNativeTextureState.epoch != frame.gameRevision || CathedralNativeTextureResource != pDungeonCels.get()
	    || CathedralNativeTextureMicroLen != MicroTileLen) {
		ResetCathedralNativeTextureCache();
		CathedralNativeTextureState.epoch = frame.gameRevision;
		CathedralNativeTextureResource = pDungeonCels.get(); CathedralNativeTextureMicroLen = MicroTileLen;
	}
	std::set<CathedralNativeTextureKey> selected;
	for (const auto &surface : frame.triangles) {
		const auto &binding = surface.nativeTexture;
		if (binding.kind == cathedral::NativeTextureKind::Technical) continue;
		const auto key = NativeTextureKey(binding);
		if (!selected.insert(key).second) continue;
		const auto found = CathedralNativeTextureCache.find(key);
		if (found != CathedralNativeTextureCache.end() && found->second.signature == NativeTextureSignature(binding)) {
			++CathedralNativeTextureState.hits;
			continue;
		}
		++CathedralNativeTextureState.misses;
		auto entry = DecodeCathedralNativeMaterial(binding); // Cold preparation only, never DrawSurface.
		const size_t bytes = entry.texture.pixels.size() + entry.texture.opacity.size();
		const size_t oldBytes = found == CathedralNativeTextureCache.end() ? 0
		    : found->second.texture.pixels.size() + found->second.texture.opacity.size();
		if ((found == CathedralNativeTextureCache.end() && CathedralNativeTextureCache.size() >= CathedralNativeTextureMaxEntries)
		    || bytes > CathedralNativeTextureMaxBytes - (CathedralNativeTextureState.bytes - oldBytes))
			throw std::length_error("Cathedral native material host-cache budget exceeded");
		if (found == CathedralNativeTextureCache.end()) CathedralNativeTextureCache.emplace(key, std::move(entry));
		else found->second = std::move(entry);
		CathedralNativeTextureState.bytes = CathedralNativeTextureState.bytes - oldBytes + bytes;
		++CathedralNativeTextureState.decodes;
	}
	CathedralNativeTextureState.entries = CathedralNativeTextureCache.size();
	CathedralNativeTextureState.floorMaterials = CathedralNativeTextureState.masonryMaterials = CathedralNativeTextureState.doorMaterials = 0;
	CathedralNativeTextureState.donorMaterials = 0;
	for (const auto &[key, entry] : CathedralNativeTextureCache) {
		if (std::get<0>(key) == cathedral::NativeTextureKind::FloorDiamond) ++CathedralNativeTextureState.floorMaterials;
		else if (std::get<0>(key) == cathedral::NativeTextureKind::Masonry) ++CathedralNativeTextureState.masonryMaterials;
		else ++CathedralNativeTextureState.doorMaterials;
		if (entry.sourcePiece != std::get<1>(key)) ++CathedralNativeTextureState.donorMaterials;
	}
}

bool CathedralNativeTexturesCurrent(const cathedral::PilotFrame &frame)
{
	if (CathedralNativeTextureState.epoch != frame.gameRevision || CathedralNativeTextureResource != pDungeonCels.get()
	    || CathedralNativeTextureMicroLen != MicroTileLen) return false;
	for (const auto &surface : frame.triangles)
		if (surface.nativeTexture.kind != cathedral::NativeTextureKind::Technical
		    && CathedralNativeTextureCache.find(NativeTextureKey(surface.nativeTexture)) == CathedralNativeTextureCache.end()) return false;
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
	result.mesh = vegetation ? BuildTownTreeVolume(source, seed, foliage) : BuildTownPropVolume(source);
	return result;
}

VolumeArtwork ComposeVegetation(const TownVegetationGroup &group)
{
	return ComposeNativeScenery(group, true, group.foliage);
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
	DrawVolume(out, position, tile, found->second, kind, entity, lighting);
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
	DrawVolume(out, position, tile, found->second, kind, entity, 0);
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

void DrawBillboard(const Surface &out, Vec3 position, Point tile, ClxSprite sprite, PickKind kind, int entity, int lighting = 0, bool preservePicking = false)
{
	if (!ViewCamera.projection.perspective && (std::abs(position.x - ViewCamera.target.x) > 80 || std::abs(position.z - ViewCamera.target.z) > 80))
		return;
	const uint8_t *key = sprite.pixelData();
	uint64_t hash = 1469598103934665603ULL;
	for (uint32_t i = 0; i < sprite.pixelDataSize(); ++i) {
		hash ^= key[i];
		hash *= 1099511628211ULL;
	}
	auto found = BillboardTextureCache.find(key);
	if (found != BillboardTextureCache.end() && (found->second.sourceHash != hash
	    || found->second.texture.width != sprite.width() || found->second.texture.height != sprite.height())) {
		BillboardTextureCache.erase(found);
		found = BillboardTextureCache.end();
	}
	if (found == BillboardTextureCache.end()) {
		if (BillboardTextureCache.size() >= 192)
			BillboardTextureCache.clear();
		found = BillboardTextureCache.emplace(key, BillboardArtwork { DecodeSprite(sprite), hash }).first;
	}
	const Texture &texture = found->second.texture;
	const float width = static_cast<float>(texture.width) / NativeCameraScale;
	const float height = static_cast<float>(texture.height) / PixelsPerWorldUnit;
	// CLX's anchor is the bottom pixel, while a rasterized quad ends at the edge
	// after that pixel. Keep the native anchor and its last row at the default pose.
	position.y -= 1.0F / PixelsPerWorldUnit;
	const Vec3 halfWidth = ViewCamera.right * (width / 2);
	const Vec3 rise { 0, height, 0 };
	PickRecord pick = PickAt(tile, kind, entity);
	pick.preservePicking = preservePicking;
	DrawQuad(out, { position - halfWidth + rise, position + halfWidth + rise,
		position + halfWidth, position - halfWidth }, texture, pick, lighting, true);
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
		const Texture *texture = &textures[static_cast<size_t>(surface.module)];
		const auto *artVertices = &surface.vertices;
		if (surface.nativeTexture.kind != cathedral::NativeTextureKind::Technical) {
			// Preparation and the pre-draw completeness gate own every miss.
			// Draw performs no decode, material construction or identity mutation.
			texture = &CathedralNativeTextureCache.at(NativeTextureKey(surface.nativeTexture)).texture;
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
		DrawTriangle(out, vertices, *texture, pick, surface.light);
	};
	const auto drawSpecial = [&](const cathedral::NativeSpecialOverlay &special) {
		if (!pSpecialCels || special.frameIndex < 0 || static_cast<uint32_t>(special.frameIndex) >= pSpecialCels->numSprites())
			return; // Normal frames were gated; forced diagnostics remain safe.
		const Point tile { special.x, special.z };
		DrawBillboard(out, { static_cast<float>(tile.x), 0, static_cast<float>(tile.y) }, tile,
			(*pSpecialCels)[special.frameIndex], PickKind::Ground, -1, special.light, CathedralPaletteBlend);
	};
	CathedralPaletteBlend = false;
	InjectCathedralAllocationFailure(TownViewCathedralAllocationFailurePoint::BeforeOpaque);
	for (const auto &surface : CathedralFrame.triangles)
		if (surface.policy == cathedral::FrameSurfacePolicy::Opaque)
			drawSurface(surface);
	for (const auto &special : CathedralFrame.nativeSpecialOverlays)
		if (!special.blendActive)
			drawSpecial(special);
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
		DrawBillboard(out, { static_cast<float>(object.position.x), 0, static_cast<float>(object.position.y) },
			object.position, object.currentSprite(), selectable ? PickKind::Object : PickKind::Ground,
			selectable ? slot : -1, object.applyLighting ? dLight[object.position.x][object.position.y] : 0);
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
		    || (&player != MyPlayer && !IsTileLit(tile) && !MyPlayer->_pInfraFlag)
		    || (FollowCameraState.localPlayerHidden && &player == MyPlayer))
			continue;
		if (&player != MyPlayer && (!IsTileLit(tile) || (MyPlayer->_pInfraFlag && dLight[tile.x][tile.y] > 8)))
			CathedralTranslation = GetInfravisionTRN();
		const bool selectable = std::abs(dPlayer[tile.x][tile.y]) == static_cast<int>(i) + 1
		    || (player.hasNoLife() && TileContainsDeadPlayer(tile));
		DrawBillboard(out, PlayerPosition(player), tile, player.currentSprite(), selectable ? PickKind::Player : PickKind::Ground,
			selectable ? static_cast<int>(i) : -1,
			&player == MyPlayer || CathedralTranslation != nullptr ? 0 : std::max(static_cast<int>(dLight[tile.x][tile.y]) - 5, 0));
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
				sprites[corpse.frame], PickKind::Ground, -1, CathedralTranslation != nullptr ? 0 : dLight[x][z]);
			CathedralTranslation = nullptr;
		}
	}
	InjectCathedralAllocationFailure(TownViewCathedralAllocationFailurePoint::AfterOpaque);
	struct OverlayCommand {
		bool special;
		size_t index;
		float depth;
	};
	std::vector<OverlayCommand> overlays;
	for (size_t i = 0; i < CathedralFrame.triangles.size(); ++i) {
		const auto &surface = CathedralFrame.triangles[i];
		if (surface.policy != cathedral::FrameSurfacePolicy::NativePaletteBlend)
			continue;
		Vec3 center {};
		for (const auto &vertex : surface.vertices)
			center = center + Vec3 { vertex.position.x, vertex.position.y, vertex.position.z } * (1.0F / 3);
		overlays.push_back({ false, i, ToCamera(center).z });
	}
	for (size_t i = 0; i < CathedralFrame.nativeSpecialOverlays.size(); ++i) {
		const auto &special = CathedralFrame.nativeSpecialOverlays[i];
		if (!special.blendActive || !pSpecialCels || special.frameIndex < 0
		    || static_cast<uint32_t>(special.frameIndex) >= pSpecialCels->numSprites())
			continue;
		const auto sprite = (*pSpecialCels)[special.frameIndex];
		const Vec3 center { static_cast<float>(special.x), sprite.height() / (2 * PixelsPerWorldUnit), static_cast<float>(special.z) };
		overlays.push_back({ true, i, ToCamera(center).z });
	}
	std::stable_sort(overlays.begin(), overlays.end(), [](const OverlayCommand &a, const OverlayCommand &b) { return a.depth > b.depth; });
	CathedralPaletteBlend = true;
	for (const auto &overlay : overlays) {
		if (overlay.special)
			drawSpecial(CathedralFrame.nativeSpecialOverlays[overlay.index]);
		else
			drawSurface(CathedralFrame.triangles[overlay.index]);
	}
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
	    && PickHorizonEnabled == *GetOptions().Graphics.townViewHorizon
	    && PickGpuRequested == *GetOptions().Graphics.townViewGpuRendering
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

void PrepareTownViewLiveFrame()
{
	if (!cathedral::LiveLevelEligible() || MyPlayer == nullptr) {
		if (CathedralFrameReady && leveltype != DTYPE_TOWN)
			InvalidateTownViewFrameForNativeFallback();
		CathedralFrameReady = false;
		CathedralFrame = {};
		return;
	}
	PickingValid = false;
	CathedralFrameReady = false;
	CathedralAllocationState.failed = false;
	CathedralAllocationState.injected = TownViewCathedralAllocationFailurePoint::None;
	CathedralAllocationState.gpuSubmittedBeforeFailure = 0;
	CathedralAllocationState.gpuDrawCallsBeforeFailure = 0;
	CathedralAllocationState.cpuRasterizedBeforeFailure = 0;
	try {
		const Point focus = MyPlayer->position.tile;
		if (cathedral::RefreshLiveFrame(focus.x, focus.y)) {
			const auto *snapshot = cathedral::LiveSnapshot();
			const auto *scene = cathedral::LiveScene();
			if (snapshot != nullptr && scene != nullptr) {
				CathedralFrame = cathedral::BuildPilotFrame(*snapshot, *scene, focus.x, focus.y);
				CathedralFrameReady = true;
				for (const auto &special : CathedralFrame.nativeSpecialOverlays) {
					if (!pSpecialCels || special.frameIndex < 0 || static_cast<uint32_t>(special.frameIndex) >= pSpecialCels->numSprites()) {
						CathedralFrame.requiresNativeFallback = true;
						CathedralFrame.fallbackReasons |= static_cast<uint32_t>(cathedral::FrameFallbackReason::UnsupportedNativeSpecial);
					}
				}
				if (!CathedralFrame.requiresNativeFallback)
					PrepareCathedralNativeTextures(CathedralFrame);
			}
		}
	} catch (const std::bad_alloc &) {
		CathedralAllocationState.failed = true;
		CathedralFrameReady = false;
		CathedralFrame = {};
	} catch (...) {
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
		RendererState.requestedGpu = *GetOptions().Graphics.townViewGpuRendering;
		try {
			RendererState.failure = "Private Cathedral pilot R2: incomplete frame or unresolved native surface; complete native fallback";
		} catch (const std::bad_alloc &) {
			CathedralAllocationState.failed = true;
			RendererState.failure.clear();
		}
	} else {
		CameraRig.Suspend(!Enabled);
	}
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
	if (CathedralNativeTextureState.epoch == 0 || CathedralNativeTextureState.epoch != cathedral::LiveEpoch()
	    || CathedralNativeTextureResource != pDungeonCels.get()) return reference;
	const auto found = CathedralNativeTextureCache.find(NativeTextureKey(binding));
	if (found == CathedralNativeTextureCache.end()) return reference;
	const auto &entry = found->second;
	reference.width = entry.texture.width; reference.height = entry.texture.height;
	reference.pixels = entry.texture.pixels; reference.opacity = entry.texture.opacity;
	reference.gpuIdentity = entry.texture.gpuIdentity.value; reference.epoch = CathedralNativeTextureState.epoch;
	reference.sourceX = binding.x; reference.sourceZ = binding.z; reference.nativeSlot = binding.nativeSlot;
	reference.requestedPiece = binding.piece; reference.sourcePiece = entry.sourcePiece;
	reference.sourceMicro = entry.sourceMicro; reference.sourceColumn = entry.sourceColumn;
	reference.sourceBlock = entry.sourceBlock; reference.secondBlock = entry.secondBlock;
	reference.cropX = entry.cropX; reference.cropY = entry.cropY;
	reference.repeat = entry.texture.repeat; reference.approximate = binding.approximate || entry.approximate;
	reference.horizontalFlip = entry.horizontalFlip;
	return reference;
}

void InitializeTownViewForGame()
{
	Enabled = *GetOptions().Graphics.townViewStartIn3D;
	CameraRig = TownCameraRig {};
	ApplyTownViewCameraPreferences();
	ResetTownViewCamera();
	// Home remains an exact native comparison. Start just off that pose so the
	// initial 3D view actually renders geometry and uses its matching picking.
	if (Enabled)
		RotateTownView(0.15F);
	SetTownViewCameraMode(static_cast<TownCameraMode>(std::clamp(*GetOptions().Graphics.townViewCameraMode, 0, 3)));
	CameraRig.Suspend(!Enabled);
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
	ResetFollowCameraHistory();
	CameraRig.RestoreIsometric();
	SyncCameraPose();
	EndTownViewCameraDrag();
	PickingValid = false;
}

TownViewCameraState GetTownViewCameraState()
{
	return { CameraYaw, CameraPitch, CameraDistance, CameraPanOffset.x, CameraPanOffset.z,
		CameraRig.mode(), CameraRig.preferences().verticalFovDegrees,
		CameraRig.mode() == TownCameraMode::FirstPerson ? CameraRig.preferences().firstPersonEyeHeight : CameraRig.preferences().eyeHeight };
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
	const int next = (static_cast<int>(CameraRig.mode()) + 1) % 4;
	GetOptions().Graphics.townViewCameraMode.SetValue(next);
	SetTownViewCameraMode(static_cast<TownCameraMode>(next));
}

void ApplyTownViewCameraPreferences()
{
	auto preferences = CameraRig.preferences();
	preferences.verticalFovDegrees = static_cast<float>(std::clamp(*GetOptions().Graphics.townViewCameraFov, 35, 100));
	preferences.orbitRadiansPerPixel = 0.006F * static_cast<float>(std::clamp(*GetOptions().Graphics.townViewCameraSensitivity, 25, 200)) / 100;
	CameraRig.SetPreferences(preferences);
	EndTownViewCameraDrag();
	PickingValid = false;
}

void SetTownViewCameraPoseForDiagnostics(TownCameraPose pose)
{
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
	SceneArtworkCache.clear();
	SceneMaterialCache.clear();
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
	SceneryVolumeCache.clear();
	SceneLightingValid = false;
	SceneLightingProfileLoaded = false;
	SceneLightingConfig = TristramLightingConfig();
	ResetTownScene();
	ResetTownVegetation();
	ResetTownProps();
	CachedDungeonData = nullptr;
	EndTownViewCameraDrag();
	PickingValid = false;
}

static bool DrawTownViewFrame(const Surface &fullOut, bool forceGeometry)
{
	const bool cathedralWorld = cathedral::LiveLevelEligible() && CathedralFrameReady;
	const bool forcedCathedralGeometry = forceGeometry && Enabled && cathedralWorld && !CathedralFrame.requiresNativeFallback;
	const auto worldStart = std::chrono::steady_clock::now();
	OgdenPilotState.drawn = false;
	OgdenPilotState.trianglesVisited = 0;
	OgdenGpuSubmissionFailed = false;
	PickingValid = false;
	RasterSampleFactor = 1;
	RendererState = {};
	ArchitectureCullingState = {};
	ArchitectureCullingState.requested = *GetOptions().Graphics.townViewFrustumCulling;
	RendererState.requestedGpu = *GetOptions().Graphics.townViewGpuRendering;
	CaptureGpu = CaptureGpuFailed = false;
	if (!RendererState.requestedGpu) {
		if (GpuRecovery.DisableOrReset())
			ResetTownGpuResources();
		GpuFailure.clear();
	}
	if ((!IsTownViewActive() && !forcedCathedralGeometry) || !pDungeonCels || MicroTileLen == 0 || MyPlayer == nullptr) {
		PickingValid = false;
		return false;
	}
	if (cathedralWorld && !CathedralNativeTexturesCurrent(CathedralFrame)) {
		InvalidateTownViewFrameForNativeFallback();
		return false; // Never publish a frame with missing/stale material bindings.
	}
	if (CachedDungeonData != pDungeonCels.get()) {
		ResetFollowCameraHistory(true);
		ClearGpuSceneResources();
		HorizonPaletteValid = false;
		RendererState.requestedGpu = *GetOptions().Graphics.townViewGpuRendering;
		BillboardTextureCache.clear();
		ClearTownShadowMap();
		TerrainCache.clear();
		SceneGroundCache.clear();
		SceneArtworkCache.clear();
		SceneMaterialCache.clear();
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
	if (fullOut.w() <= 0 || height <= 0)
		return false;
	const Surface logical = fullOut.subregionY(0, height);
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
		SamplingState = { *GetOptions().Graphics.townViewAntialiasing, 1, logical.w(), logical.h(), false };
		ClearSurface(logical);
		return DrawNativeTownViewReference(fullOut, ViewPosition);
	}
	if (!ViewCamera.projection.valid) {
		RendererState.failure = "Invalid town camera frame";
		if (cathedralWorld)
			return false;
		if (FollowCameraState.active) {
			// No safe near-plane sphere: fail closed instead of presenting the
			// native 2D world with first-person input. Picking remains invalid.
			SamplingState = { *GetOptions().Graphics.townViewAntialiasing, 1, logical.w(), logical.h(), false };
			ClearSurface(logical);
			return true;
		}
		return false;
	}
	const Surface out = PrepareSamplingBuffers(logical);
	ClearSurface(out);
	if (!cathedralWorld)
		PrepareSceneLighting();
	const bool horizonEnabled = !cathedralWorld && *GetOptions().Graphics.townViewHorizon;
	if (horizonEnabled) {
		PrepareHorizon();
		RendererState.horizonTriangles = HorizonMesh.statistics.triangles;
		RendererState.horizonBytes = HorizonMesh.statistics.reservedTriangleBytes + HorizonColors.capacity()
		    + sizeof(HorizonFogPalette) + sizeof(HorizonFogDepthLevels) + sizeof(HorizonSkyColors) + sizeof(HorizonPaletteSignature);
	}
	FrameFireTime = CabinFireDiagnosticTime >= 0 ? CabinFireDiagnosticTime : static_cast<double>(SDL_GetTicks()) / 1000;
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
	// The Cathedral path binds native discrete light/TRN LUTs and overlays on
	// the existing hardware backend. A failed request always returns to native.
	if (GpuRecovery.ShouldAttempt(RendererState.requestedGpu, recoveryKey, SDL_GetTicks())) {
		if (!cathedralWorld)
			PrepareFrameImportedAlbedo();
		CaptureGpu = TownGpuBeginFrame(out.w(), out.h(), false,
		    { ViewCamera.projection.perspective, ViewCamera.projection.nearClip, ViewCamera.projection.farClip });
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
			CaptureGpuFailed = !TownGpuSetShadow(shadow);
			if (!CaptureGpuFailed && cathedralWorld)
				CaptureGpuFailed = !PrepareCathedralGpuBlend();
			if (!CaptureGpuFailed && !cathedralWorld && ResidentMeshesEnabledForDiagnostics)
				ConfigureResidentMeshCamera();
		} else {
			GpuRecovery.RecordFailure(GetTownGpuStatus().failureKind, recoveryKey, SDL_GetTicks());
			GpuFailure = GetTownGpuStatus().failure;
		}
	}
	if (cathedralWorld && RendererState.requestedGpu && !CaptureGpu) {
		RendererState.failure = GpuFailure.empty() ? "Cathedral hardware GPU request deferred; complete native fallback" : GpuFailure;
		InvalidateTownViewFrameForNativeFallback();
		return false;
	}
	const auto drawWorld = [&]() {
		const auto recordStart = std::chrono::steady_clock::now();
		if (cathedralWorld) {
			DrawCathedralWorld(out);
			RendererState.sceneRecordMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - recordStart).count();
			return;
		}
		if (horizonEnabled)
			DrawHorizon(out);
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
				DrawGround(out, tile, TownPropReplacesTile(tile) ? fallback : SceneGround(dPiece[tile.x][tile.y]));
		});
		forEachTile([&](Point tile) {
			DrawScenery(out, tile, GetTile(dPiece[tile.x][tile.y]), fallback);
		});
		DrawScene(out);
		DrawVegetation(out);
		DrawProps(out);
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
			if (FollowCameraState.localPlayerHidden && &player == MyPlayer)
				continue;
			Vec3 position = PlayerPosition(player);
			position.y = 0;
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
	drawWorld();
	if (CaptureGpu) {
		if (cathedralWorld && CathedralAllocationState.armed == TownViewCathedralAllocationFailurePoint::BeforeReadback)
			SetTownGpuBeforeReadbackForDiagnostics(InjectCathedralReadbackAllocationFailure);
		const bool complete = !CaptureGpuFailed && TownGpuEndFrame(GpuFrame)
		    && GpuFrame.width == out.w() && GpuFrame.height == out.h()
		    && GpuFrame.indexed.size() == DepthBuffer.size()
		    && GpuFrame.depth.size() == DepthBuffer.size() && GpuFrame.pickIds.size() == PickBuffer.size()
		    && std::all_of(GpuFrame.pickIds.begin(), GpuFrame.pickIds.end(), [](uint32_t id) { return id < GpuPickRecords.size(); });
		CaptureGpu = false;
		if (complete) {
			for (int y = 0; y < out.h(); ++y)
				std::memcpy(out.at(0, y), GpuFrame.indexed.data() + static_cast<size_t>(y) * out.w(), out.w());
			DepthBuffer.swap(GpuFrame.depth);
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
				RendererState.failure = GpuFailure;
				ClearSurface(out);
				InvalidateTownViewFrameForNativeFallback();
				return false; // DrawGame owns the complete native world fallback.
			}
			ClearSurface(out);
			std::fill(DepthBuffer.begin(), DepthBuffer.end(), std::numeric_limits<float>::infinity());
			std::fill(PickBuffer.begin(), PickBuffer.end(), PickRecord {});
			drawWorld();
		}
	}
	RendererState.failure = GpuFailure;
	for (auto it = PreparedGpuTextures.begin(); it != PreparedGpuTextures.end();) {
		if (it->second.lastSeen + 2 < GpuFrameNumber)
			it = PreparedGpuTextures.erase(it);
		else
			++it;
	}
	if (horizonEnabled)
		ApplyHorizonAtmosphere(out);
	ResolveSampling(logical, out);
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
	PickHorizonEnabled = *GetOptions().Graphics.townViewHorizon;
	PickingValid = !cathedralWorld || (!CathedralFrame.requiresNativeFallback
	    && (!RendererState.requestedGpu || RendererState.usedGpu) && cathedral::LiveFrameCurrent());
	RendererState.worldMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - worldStart).count();
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
			lastReport = now;
			lastGpu = RendererState.usedGpu;
			lastFailure = RendererState.failure;
		}
	}
	return true;
}

static bool DiscardCathedralAllocationFailure(const Surface &fullOut, bool requestedGpu)
{
	// Take POD evidence before resetting backend status and host counters.
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
	return false; // DrawGame owns the complete native redraw and input fallback.
}

bool DrawTownView(const Surface &fullOut, bool forceGeometry)
{
	// Preserve the Town/Ogden draw body and failure branches exactly. Only the
	// native Cathedral transaction gets this recoverable allocation boundary.
	if (!cathedral::LiveLevelEligible() || !CathedralFrameReady)
		return DrawTownViewFrame(fullOut, forceGeometry);
	const bool requestedGpu = *GetOptions().Graphics.townViewGpuRendering;
	CathedralAllocationState.failed = false;
	CathedralAllocationState.injected = TownViewCathedralAllocationFailurePoint::None;
	CathedralAllocationState.gpuSubmittedBeforeFailure = 0;
	CathedralAllocationState.gpuDrawCallsBeforeFailure = 0;
	CathedralAllocationState.cpuRasterizedBeforeFailure = 0;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
	try {
		return DrawTownViewFrame(fullOut, forceGeometry);
	} catch (const std::bad_alloc &) {
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

} // namespace devilution
