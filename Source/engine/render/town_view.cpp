#include "engine/render/town_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "control/control.hpp"
#include "engine/assets.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/light_tables.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/scrollrt.h"
#include "engine/render/town_ground_shadow.hpp"
#include "engine/render/town_gpu.hpp"
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
#include "engine/render/town_actor_mask.hpp"
#include "engine/render/town_view_resolve.hpp"
#include "engine/render/town_presentation.hpp"
#include "engine/render/ui_overlay_regions.hpp"
#include "engine/surface.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/tile_properties.hpp"
#include "missiles.h"
#include "options.h"
#include "player.h"
#include "towners.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {

constexpr float Pi = 3.14159265358979323846F;
constexpr float NearPlane = 0.4F;
constexpr float PixelsPerWorldUnit = 32.0F;
constexpr float NativeCameraScale = 45.25483399593904F; // 32 * sqrt(2)
constexpr float NativeHeightScale = 0.816496580927726F; // sqrt(2/3)
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
};

enum class PickKind : uint8_t { Ground, Towner, Item, Player };
struct PickRecord {
	uint16_t tile = std::numeric_limits<uint16_t>::max();
	int16_t entity = -1;
	PickKind kind = PickKind::Ground;
	int16_t architecture = -1;
	bool preservePicking = false;
};

bool Enabled = false;
bool PickingValid = false;
int PickScreenWidth = 0;
int PickScreenHeight = 0;
int PickViewportHeight = 0;
bool PickZoom = false;
bool PickLeftPanel = false;
bool PickRightPanel = false;
bool PickGpuRequested = false;
float CameraYaw = Pi * 0.25F;
float CameraDistance = 22.0F;
float CameraPitch = Pi / 6;
Vec3 CameraPanOffset { 0, 0, 0 };
bool CameraDragging = false;
bool CameraDragPans = false;
Point CameraDragLastPosition;
constexpr float MinCameraDistance = 10.0F;
constexpr float MaxCameraDistance = 52.0F;
constexpr float MinCameraPitch = 0.35F;
constexpr float MaxCameraPitch = 1.40F;
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
bool GpuBlocked = false;
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
std::vector<uint8_t> GpuOrdinaryLut;
std::vector<uint8_t> GpuImportedLut;
size_t GpuImportedLutColors = 0;
std::unordered_map<uint16_t, TileArt> TerrainCache;
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
bool CabinFireEnabledForDiagnostics = true;
double CabinFireDiagnosticTime = -1;
double FrameFireTime = 0;
std::unordered_map<uint32_t, uint32_t> ImportedAlbedoColors;
std::vector<std::array<uint8_t, ImportedLightLevels>> ImportedAlbedoLightTables;
TownLightingConfig SceneLightingConfig = TristramLightingConfig();
bool SceneLightingProfileLoaded = false;
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

void ConfigureCamera(int width, int height)
{
	Vec3 target { static_cast<float>(ViewPosition.x), 0, static_cast<float>(ViewPosition.y) };
	if (MyPlayer != nullptr && MyPlayer->position.tile == ViewPosition)
		target = PlayerPosition(*MyPlayer);
	target = target + CameraPanOffset;
	ViewCamera.target = target;
	const float inclination = CameraPitch;
	const float cosYaw = std::cos(CameraYaw);
	const float sinYaw = std::sin(CameraYaw);
	const float cosPitch = std::cos(inclination);
	const float sinPitch = std::sin(inclination);
	// Orthographic rays start far enough behind every town cell. Distance controls
	// magnification, so orbit/zoom cannot clip scenery merely by moving the eye.
	ViewCamera.eye = target + Vec3 { cosYaw * cosPitch, sinPitch, sinYaw * cosPitch } * 256.0F;
	ViewCamera.forward = { -cosYaw * cosPitch, -sinPitch, -sinYaw * cosPitch };
	ViewCamera.right = { sinYaw, 0, -cosYaw };
	ViewCamera.up = { -cosYaw * sinPitch, cosPitch, -sinYaw * sinPitch };
	ViewCamera.width = width;
	ViewCamera.height = height;
	// Derive the anchor from the real native viewport, including zoom and side
	// panels. Walking offsets cancel the camera offset, since target already
	// follows the interpolated player position.
	CalcViewportGeometry();
	Point anchor = GetScreenPosition(ViewPosition) + Displacement { 32, 0 };
	if (MyPlayer != nullptr && MyPlayer->position.tile == ViewPosition && MyPlayer->isWalking())
		anchor += GetOffsetForWalking(MyPlayer->AnimInfo, MyPlayer->_pdir);
	const int zoomFactor = *GetOptions().Graphics.zoom ? 2 : 1;
	ViewCamera.centerX = static_cast<float>(anchor.x * zoomFactor);
	ViewCamera.centerY = static_cast<float>(anchor.y * zoomFactor);
	if (zoomFactor == 2 && CanPanelsCoverView() && IsLeftPanelOpen())
		ViewCamera.centerX += SidePanelSize.width;
	ViewCamera.focal = NativeCameraScale * DefaultCameraDistance / CameraDistance * zoomFactor;
}

Vec3 ToCamera(Vec3 point)
{
	point.y *= NativeHeightScale;
	const Vec3 relative = point - ViewCamera.eye;
	return { Dot(relative, ViewCamera.right), Dot(relative, ViewCamera.up), Dot(relative, ViewCamera.forward) };
}

ProjectedVertex Project(Vertex vertex)
{
	return { ViewCamera.centerX + vertex.position.x * ViewCamera.focal,
		ViewCamera.centerY - vertex.position.y * ViewCamera.focal,
		vertex.position.z, vertex.u, vertex.v };
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
	GpuOrdinaryLut.clear();
	GpuImportedLut.clear();
	GpuImportedLutColors = 0;
	GpuPickRecords.clear();
	GpuPickIds.clear();
	GpuFrame = {};
	RendererState = {};
	CaptureGpu = CaptureGpuFailed = GpuBlocked = false;
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
		result.revision = GpuImportedLutColors;
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

void Rasterize(const Surface &out, const std::array<Vertex, 3> &triangle, const Texture &texture,
	PickRecord pick, int shade, bool transparent, const TownSceneNormal *authoredNormal,
	const InteriorLighting *interior, GpuVolumeFallback fallback)
{
	const ProjectedVertex a = ProjectRaster(triangle[0]);
	const ProjectedVertex b = ProjectRaster(triangle[1]);
	const ProjectedVertex c = ProjectRaster(triangle[2]);
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
	const uint8_t *lightTable = SceneLightTables[std::clamp(shade, 0, 3)].data();
	const auto worldPosition = [](Vec3 camera) {
		Vec3 world = ViewCamera.eye + ViewCamera.right * camera.x
		    + ViewCamera.up * camera.y + ViewCamera.forward * camera.z;
		world.y /= NativeHeightScale;
		return world;
	};
	const Vec3 worldA = worldPosition(triangle[0].position);
	const Vec3 worldB = worldPosition(triangle[1].position);
	const Vec3 worldC = worldPosition(triangle[2].position);
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
		const TownLightVector facing = OrientTownLightingNormal({ normal.x, normal.y, normal.z },
			{ -ViewCamera.forward.x, -ViewCamera.forward.y / NativeHeightScale, -ViewCamera.forward.z });
		normal = { facing.x, facing.height, facing.z };
	}
	const TownShadowReceiver shadowReceiver = PrepareTownShadowReceiver(normal.x, normal.y, normal.z);
	const TownShadowDirection light = GetTownShadowLightDirection();
	// Normal shading already supplies ambient light on faces pointing away from
	// the source. Shadowing their absent direct light again crushed the masonry.
	const bool receivesDirectLight = normal.x * light.x + normal.y * light.height + normal.z * light.z > 0.02F;
	const float diffuse = std::clamp(normal.x * light.x + normal.y * light.height + normal.z * light.z, 0.0F, 1.0F);
	if (CaptureGpu) {
		if (CaptureGpuFailed)
			return;
		TownGpuMaterial material;
		material.lighting = texture.emissive ? TownGpuLighting::Unlit
		    : interior != nullptr ? TownGpuLighting::Interior
		    : hasAlbedo ? TownGpuLighting::Directional : TownGpuLighting::Shadow;
		material.repeat = texture.repeat;
		material.transparentZero = transparent && !hasAlbedo && texture.opacity.empty();
		material.preservePicking = pick.preservePicking;
		material.receivesShadow = interior == nullptr && !texture.emissive && (hasAlbedo ? diffuse > 0 : receivesDirectLight);
		material.shade = shade;
		material.fallbackPaletteIndex = fallback.palette;
		material.fallbackShade = fallback.shade;
		material.diffuse = diffuse;
		material.shadowBias = shadowReceiver.depthBias;
		material.shadowSlopeU = shadowReceiver.depthSlopeU;
		material.shadowSlopeV = shadowReceiver.depthSlopeV;
		material.normal = { normal.x, normal.y, normal.z };
		TownLightOccluder room;
		if (interior != nullptr) {
			material.lights = { interior->lights.data(), interior->lightCount };
			material.interiorRedNormalization = interior->room->fireColor.red;
			material.interiorPointRange = InteriorPointLightRange;
			room = { interior->room->roomMinimum, interior->room->roomMaximum, interior->room->apertures };
			material.room = &room;
		}
		const bool constantPalette = !hasAlbedo && !texture.emissive && texture.opacity.empty()
		    && texture.width == 1 && texture.height == 1 && texture.pixels.size() == 1;
		if (constantPalette)
			material.repeat = false;
		const auto vertex = [&](ProjectedVertex p, Vec3 w) {
			if (constantPalette) {
				p.u = (static_cast<float>(texture.pixels[0]) + 0.5F) / 256;
				p.v = 0.5F;
			}
			return TownGpuVertex { p.x, p.y, p.depth, p.u, p.v, { w.x, w.y, w.z } };
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
			const float px = static_cast<float>(x) + 0.5F;
			const float py = static_cast<float>(y) + 0.5F;
			const float wa = Edge(b, c, px, py) * inverseArea;
			const float wb = Edge(c, a, px, py) * inverseArea;
			const float wc = 1.0F - wa - wb;
			if (wa < -0.0001F || wb < -0.0001F || wc < -0.0001F)
				continue;
			const float depth = wa * a.depth + wb * b.depth + wc * c.depth;
			const size_t index = static_cast<size_t>(y) * out.w() + x;
			if (depth > DepthBuffer[index] + 0.0001F)
				continue;
			// Both texture coordinates and depth are affine under parallel rays.
			const float u = wa * a.u + wb * b.u + wc * c.u;
			const float v = wa * a.v + wb * b.v + wc * c.v;
			uint8_t color;
			uint32_t albedoColor = 0;
			if (!texture.sample(u, v, color, &albedoColor) || (transparent && !hasAlbedo && texture.opacity.empty() && color == 0))
				continue;
			const Vec3 world = worldA * wa + worldB * wb + worldC * wc;
			const float shadow = interior == nullptr && !texture.emissive && (hasAlbedo ? diffuse > 0 : receivesDirectLight)
			    ? SampleTownShadow(world.x, world.y, world.z, shadowReceiver) : 0;
			const int shadowLevel = std::clamp(static_cast<int>(shadow * 3.0F + 0.5F), 0, 3);
			if (texture.emissive) {
				destination[x] = color;
			} else if (interior != nullptr) {
				TownLightingConfig roomLight;
				roomLight.ambient = { 0.015F, 0.013F, 0.010F };
				roomLight.directionalIntensity = 0;
				const TownLightOccluder room { interior->room->roomMinimum, interior->room->roomMaximum, interior->room->apertures };
				const std::span<const TownPointLight> lights(interior->lights.data(), interior->lightCount);
				const TownLightingSample lighting = SampleTownLighting({ normal.x, normal.y, normal.z },
					{ world.x, world.y, world.z }, 0, roomLight, lights, std::span<const TownLightOccluder>(&room, 1));
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
			DepthBuffer[index] = depth;
			if (!pick.preservePicking)
				PickBuffer[index] = pick;
		}
	}
}

void DrawTriangle(const Surface &out, std::array<Vertex, 3> triangle, const Texture &texture,
	PickRecord pick, int shade, bool transparent = false, const TownSceneNormal *authoredNormal = nullptr,
	const InteriorLighting *interior = nullptr, GpuVolumeFallback fallback = {})
{
	for (Vertex &vertex : triangle)
		vertex.position = ToCamera(vertex.position);
	// Clip against the near plane before projecting; crossing triangles remain selectable.
	std::array<Vertex, 4> clipped;
	size_t count = 0;
	for (size_t i = 0; i < 3; ++i) {
		const Vertex &previous = triangle[(i + 2) % 3];
		const Vertex &current = triangle[i];
		const bool previousInside = previous.position.z >= NearPlane;
		const bool currentInside = current.position.z >= NearPlane;
		if (previousInside != currentInside) {
			const float t = (NearPlane - previous.position.z) / (current.position.z - previous.position.z);
			clipped[count] = { previous.position + (current.position - previous.position) * t,
				previous.u + (current.u - previous.u) * t, previous.v + (current.v - previous.v) * t };
			// Interpolation can round just below the plane; both backends receive
			// the exact same clipped position and the GPU validates this boundary.
			clipped[count++].position.z = NearPlane;
		}
		if (currentInside)
			clipped[count++] = current;
	}
	for (size_t i = 1; i + 1 < count; ++i)
		Rasterize(out, { clipped[0], clipped[i], clipped[i + 1] }, texture, pick, shade, transparent, authoredNormal, interior, fallback);
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

const Texture &ImportedSceneTexture(size_t index, const TownImportedTexture &source)
{
	const auto found = ImportedTextureCache.find(index);
	if (found != ImportedTextureCache.end())
		return found->second;
	Texture texture;
	texture.width = static_cast<int>(source.width);
	texture.height = static_cast<int>(source.height);
	texture.albedoPixels.resize(static_cast<size_t>(source.width) * source.height);
	for (size_t i = 0; i < texture.albedoPixels.size(); ++i) {
		const uint8_t *rgb = source.rgb.data() + i * 3;
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

void DrawScene(const Surface &out)
{
	const std::vector<TownSceneModel> &scene = GetTownScene();
	for (size_t index = 0; index < scene.size(); ++index) {
		const TownSceneModel &model = scene[index];
		if (model.externalModel && model.importedTexture) {
			const Texture &texture = ImportedSceneTexture(index, *model.importedTexture);
			for (const TownSceneTriangle &triangle : TownSceneExteriorTriangles(model)) {
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
			const TownSceneTriangle &triangle = model.triangles[triangleIndex];
			const NativeSceneFace &face = art.projectedFaces[triangleIndex];
			if (!triangle.nativeProjection || face.texture.width == 0)
				continue;
			const Vec3 toEye = ViewCamera.eye - Vec3 { triangle.vertices[0].x, triangle.vertices[0].height * NativeHeightScale, triangle.vertices[0].z };
			if (triangle.normal.x * toEye.x + triangle.normal.height * toEye.y / NativeHeightScale + triangle.normal.z * toEye.z <= 0)
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

void DrawVolume(const Surface &out, Vec3 position, Point tile, const VolumeArtwork &art, PickKind kind, int entity, int lighting)
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
	const PickRecord pick = PickAt(tile, kind, entity);
	for (const TownVolumeTriangle &triangle : art.mesh.triangles) {
		std::array<Vertex, 3> vertices;
		for (size_t i = 0; i < vertices.size(); ++i)
			vertices[i] = { world(triangle.vertices[i]), triangle.vertices[i].u, triangle.vertices[i].v };
		const Vec3 normal = Cross(vertices[1].position - vertices[0].position, vertices[2].position - vertices[0].position);
		const Vec3 eye { ViewCamera.eye.x, ViewCamera.eye.y / NativeHeightScale, ViewCamera.eye.z };
		if (Dot(normal, eye - vertices[0].position) <= 0)
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
	DrawVolume(out, { x, 0, z }, tile, volume, PickKind::Ground, -1, lighting);
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
	if (std::abs(position.x - ViewCamera.target.x) > 80 || std::abs(position.z - ViewCamera.target.z) > 80)
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

void DrawBillboard(const Surface &out, Vec3 position, Point tile, ClxSprite sprite, PickKind kind, int entity, int lighting = 0)
{
	if (std::abs(position.x - ViewCamera.target.x) > 80 || std::abs(position.z - ViewCamera.target.z) > 80)
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
	DrawQuad(out, { position - halfWidth + rise, position + halfWidth + rise,
		position + halfWidth, position - halfWidth }, texture, PickAt(tile, kind, entity), lighting, true);
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
	    && PickGpuRequested == *GetOptions().Graphics.townViewGpuRendering
	    && PickScreenWidth == gnScreenWidth && PickScreenHeight == gnScreenHeight
	    && PickViewportHeight == gnViewportHeight && PickZoom == *GetOptions().Graphics.zoom
	    && PickLeftPanel == IsLeftPanelOpen() && PickRightPanel == IsRightPanelOpen()
	    && ViewCamera.width > 0 && ViewCamera.height > 0
	    && DepthBuffer.size() == static_cast<size_t>(ViewCamera.width) * ViewCamera.height
	    && PickBuffer.size() == DepthBuffer.size();
}

} // namespace

bool IsTownViewActive()
{
	return Enabled && leveltype == DTYPE_TOWN;
}

void InitializeTownViewForGame()
{
	Enabled = *GetOptions().Graphics.townViewStartIn3D;
	ResetTownViewCamera();
	// Home remains an exact native comparison. Start just off that pose so the
	// initial 3D view actually renders geometry and uses its matching picking.
	if (Enabled)
		RotateTownView(0.15F);
}

bool IsTownViewNativePose()
{
	constexpr float Tolerance = 0.00001F;
	return IsTownViewActive()
	    && std::abs(std::remainder(CameraYaw - Pi * 0.25F, 2 * Pi)) <= Tolerance
	    && std::abs(CameraPitch - Pi / 6) <= Tolerance
	    && std::abs(CameraDistance - DefaultCameraDistance) <= Tolerance
	    && std::abs(CameraPanOffset.x) <= Tolerance
	    && std::abs(CameraPanOffset.z) <= Tolerance;
}

void ToggleTownView()
{
	Enabled = !Enabled;
	EndTownViewCameraDrag();
	PickingValid = false;
}

void RotateTownView(float radians)
{
	OrbitTownView(radians, 0);
}

void AdjustTownViewDistance(float delta)
{
	CameraDistance = std::clamp(CameraDistance + delta, MinCameraDistance, MaxCameraDistance);
	PickingValid = false;
}

void OrbitTownView(float yawDelta, float pitchDelta)
{
	if (!std::isfinite(yawDelta) || !std::isfinite(pitchDelta))
		return;
	CameraYaw = std::remainder(CameraYaw + yawDelta, 2 * Pi);
	CameraPitch = std::clamp(CameraPitch + pitchDelta, MinCameraPitch, MaxCameraPitch);
	PickingValid = false;
}

void ZoomTownView(float wheelSteps)
{
	if (!std::isfinite(wheelSteps))
		return;
	CameraDistance = std::clamp(CameraDistance * std::pow(0.88F, std::clamp(wheelSteps, -50.0F, 50.0F)), MinCameraDistance, MaxCameraDistance);
	PickingValid = false;
}

void ResetTownViewCamera()
{
	CameraYaw = Pi * 0.25F;
	CameraPitch = Pi / 6;
	CameraDistance = DefaultCameraDistance;
	CameraPanOffset = { 0, 0, 0 };
	EndTownViewCameraDrag();
	PickingValid = false;
}

TownViewCameraState GetTownViewCameraState()
{
	return { CameraYaw, CameraPitch, CameraDistance, CameraPanOffset.x, CameraPanOffset.z };
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
		const float unitsPerPixel = 1.0F / std::max(1.0F, ViewCamera.focal);
		const Vec3 right { std::sin(CameraYaw), 0, -std::cos(CameraYaw) };
		const Vec3 forward { -std::cos(CameraYaw), 0, -std::sin(CameraYaw) };
		CameraPanOffset = CameraPanOffset - right * (static_cast<float>(delta.deltaX) * unitsPerPixel)
		    + forward * (static_cast<float>(delta.deltaY) * unitsPerPixel / std::sin(CameraPitch));
		const float length = std::sqrt(Dot(CameraPanOffset, CameraPanOffset));
		if (length > 20.0F)
			CameraPanOffset = CameraPanOffset * (20.0F / length);
		PickingValid = false;
	} else {
		OrbitTownView(static_cast<float>(delta.deltaX) * 0.008F, static_cast<float>(delta.deltaY) * 0.006F);
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
		CabinInteriorTextureCache.size(), CabinFireEnabledForDiagnostics };
}

void SetTownViewCabinFireEnabledForDiagnostics(bool enabled)
{
	CabinFireEnabledForDiagnostics = enabled;
}

void SetTownViewFireTimeForDiagnostics(double seconds)
{
	CabinFireDiagnosticTime = std::isfinite(seconds) && seconds >= 0 ? seconds : -1;
}

void ResetTownViewResources()
{
	ClearGpuSceneResources();
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
	CabinFireEnabledForDiagnostics = true;
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

bool DrawTownView(const Surface &fullOut, bool forceGeometry)
{
	PickingValid = false;
	RasterSampleFactor = 1;
	RendererState = {};
	RendererState.requestedGpu = *GetOptions().Graphics.townViewGpuRendering;
	CaptureGpu = CaptureGpuFailed = false;
	if (!RendererState.requestedGpu) {
		GpuBlocked = false;
		GpuFailure.clear();
	}
	if (!IsTownViewActive() || !pDungeonCels || MicroTileLen == 0 || MyPlayer == nullptr) {
		PickingValid = false;
		return false;
	}
	if (CachedDungeonData != pDungeonCels.get()) {
		ClearGpuSceneResources();
		RendererState.requestedGpu = *GetOptions().Graphics.townViewGpuRendering;
		BillboardTextureCache.clear();
		ClearTownShadowMap();
		TerrainCache.clear();
		SceneGroundCache.clear();
		SceneArtworkCache.clear();
		SceneMaterialCache.clear();
		ImportedTextureCache.clear();
		CabinInteriorTextureCache.clear();
		CabinFireEnabledForDiagnostics = true;
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
	ConfigureCamera(logical.w(), logical.h());
	if (IsTownViewNativePose() && !forceGeometry) {
		// A reference angle must reproduce the real game, including its painter
		// order, trees, actors and zoom. Rotation exposes the reconstructed volumes.
		// Cursor selection follows the same native path at this exact pose.
		PickingValid = false;
		SamplingState = { *GetOptions().Graphics.townViewAntialiasing, 1, logical.w(), logical.h(), false };
		ClearSurface(logical);
		return DrawNativeTownViewReference(fullOut, ViewPosition);
	}
	const Surface out = PrepareSamplingBuffers(logical);
	ClearSurface(out);
	PrepareSceneLighting();
	FrameFireTime = CabinFireDiagnosticTime >= 0 ? CabinFireDiagnosticTime : static_cast<double>(SDL_GetTicks()) / 1000;
	TownShadowConfig shadowConfig;
	shadowConfig.toLight = { SceneLightingConfig.toLight.x, SceneLightingConfig.toLight.height, SceneLightingConfig.toLight.z };
	BuildTownShadowMap(GetTownScene(), shadowConfig);
	++GpuFrameNumber;
	if (RendererState.requestedGpu && !GpuBlocked) {
		CaptureGpu = TownGpuBeginFrame(out.w(), out.h());
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
		} else {
			GpuBlocked = true;
			GpuFailure = GetTownGpuStatus().failure;
		}
	}
	const auto drawWorld = [&]() {
		const Texture &fallback = FallbackGround();
		// Native town generation fills the entire dungeon grid, including the outer
		// grass visible around Farnham and Adria in wide views.
		// Camera exploration changes only drawing; movement keeps the native dmin/dmax bounds.
		const int minX = 0;
		const int maxX = MAXDUNX;
		const int minY = 0;
		const int maxY = MAXDUNY;
		std::vector<Point> tiles;
		for (int y = minY; y < maxY; ++y)
			for (int x = minX; x < maxX; ++x)
				// Zero is the first valid MIN piece, not an empty cell.
				if (dPiece[x][y] < MAXTILES)
					tiles.push_back({ x, y });
		std::stable_sort(tiles.begin(), tiles.end(), [](Point a, Point b) {
			return a.x + a.y != b.x + b.y ? a.x + a.y < b.x + b.y : a.x < b.x;
		});
		// Native floor diamonds are drawn before the SOL cell artwork. Coplanar
		// painted bases can overlap those diamonds; retain their native draw order.
		for (Point tile : tiles) {
			const TileArt &art = GetTile(dPiece[tile.x][tile.y]);
			if (!art.solid)
				DrawGround(out, tile, TownPropReplacesTile(tile) ? fallback : SceneGround(dPiece[tile.x][tile.y]));
		}
		for (Point tile : tiles)
			DrawScenery(out, tile, GetTile(dPiece[tile.x][tile.y]), fallback);
		DrawScene(out);
		DrawVegetation(out);
		DrawProps(out);
		for (size_t i = 0; i < Towners.size(); ++i) {
			const Towner &towner = Towners[i];
			if (!towner.anim)
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
	};
	drawWorld();
	if (CaptureGpu) {
		const bool complete = TownGpuEndFrame(GpuFrame) && !CaptureGpuFailed
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
		} else {
			GpuBlocked = true;
			GpuFailure = GetTownGpuStatus().failure;
			if (GpuFailure.empty())
				GpuFailure = "GPU frame validation failed";
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
	ResolveSampling(logical, out);
	PickScreenWidth = gnScreenWidth;
	PickScreenHeight = gnScreenHeight;
	PickViewportHeight = gnViewportHeight;
	PickZoom = *GetOptions().Graphics.zoom;
	PickLeftPanel = IsLeftPanelOpen();
	PickRightPanel = IsRightPanelOpen();
	PickGpuRequested = RendererState.requestedGpu;
	PickingValid = true;
	return true;
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
	if (!IsTownViewActive() || !CurrentPickingValid() || CachedDungeonData != pDungeonCels.get()
		|| screen.x < 0 || screen.y < 0 || screen.x >= ViewCamera.width || screen.y >= ViewCamera.height)
		return false;
	const PickRecord &pick = PickBuffer[static_cast<size_t>(screen.y) * ViewCamera.width + screen.x];
	if (pick.tile == std::numeric_limits<uint16_t>::max())
		return false;
	tile = { pick.tile % MAXDUNX, pick.tile / MAXDUNX };
	switch (pick.kind) {
	case PickKind::Ground: break;
	case PickKind::Towner: townerIndex = pick.entity; break;
	case PickKind::Item: itemIndex = pick.entity; break;
	case PickKind::Player: playerIndex = pick.entity; break;
	}
	return true;
}

Point TownViewScreenPosition(Point tile)
{
	if (!IsTownViewActive() || ViewCamera.width == 0)
		return { -10000, -10000 };
	const Vec3 point = ToCamera({ static_cast<float>(tile.x) - 0.5F, 0, static_cast<float>(tile.y) - 0.5F });
	if (point.z < NearPlane)
		return { -10000, -10000 };
	const ProjectedVertex projected = Project({ point, 0, 0 });
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
