#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/point.hpp"
#include "engine/render/town_volume.hpp"

namespace devilution {

enum class TownVisualObjectKind : uint8_t { Ground, Scenery, Prop, Vegetation };
enum class TownVisualObjectEvent : uint8_t { None, WellWater, CatacombsAccess, CavesAccess, HellAccess };

/** Unlit native palette codes and independent coverage. No MPQ bytes or game
 * state are written by this type. Extracted textures are private local assets. */
struct TownVisualObjectTexture {
	uint32_t width = 0, height = 0;
	bool repeat = false;
	std::vector<uint8_t> pixels, opacity;
};

struct TownVisualSourceCell {
	Point tile;
	uint16_t piece = 0;
	uint16_t special = 0;
};

/** Designer edits affect presentation only. Offset/yaw/scale are applied about
 * the immutable native anchor. Pick, SOL, pathing, quests and saves remain native. */
struct TownVisualObjectTransform {
	float x = 0, height = 0, z = 0;
	float yaw = 0;
	float scale = 1;
};

struct TownVisualObject {
	std::string id;
	TownVisualObjectKind kind = TownVisualObjectKind::Ground;
	Point anchor;
	TownVisualObjectTransform transform;
	uint32_t texture = 0;
	std::vector<TownVisualSourceCell> sourceCells;
	TownVolumeMesh mesh;
	bool physicalFrame = false;
	bool visible = true;
	bool nativeSuppressed = false;
	bool groundFallback = false;
	TownVisualObjectEvent eventBinding = TownVisualObjectEvent::None;
	bool eventState = false;
	// 0: early ground, 1: native ordered solid-ground/scenery, 2: trees/props.
	uint8_t phase = 0;
};

struct TownVisualObjectSet {
	std::vector<TownVisualObjectTexture> textures;
	std::vector<TownVisualObject> objects;
};

struct TownVisualObjectAudit {
	std::string path, sha256, expectedSha256, failure;
	std::string status = "missing";
	size_t objects = 0, textures = 0, triangles = 0;
};

/** Submission/cold-cache evidence, not a measurement of visible pixels or FPS. */
struct TownViewFixedObjectState {
	bool active = false;
	size_t groundSubmissions = 0, volumeSubmissions = 0;
	size_t nativeDeltaObjects = 0, nativeDeltaRebuilds = 0;
	size_t cachedVolumes = 0;
};
TownViewFixedObjectState GetTownViewFixedObjectState();

/** Private archive D3DSTAT1/version1, explicit LE integers/float32. Parsing is
 * bounded and atomic. Unknown flags, duplicate IDs, trailing bytes, invalid
 * indices/geometry/transforms and inconsistent texture coverage are refused. */
bool ParseTownVisualObjects(std::span<const std::byte> bytes, TownVisualObjectSet &out, std::string &failure);
bool EncodeTownVisualObjects(const TownVisualObjectSet &objects, std::vector<std::byte> &out, std::string &failure);
bool LoadTownVisualObjects(std::string_view path, std::string_view expectedSha256, std::string &failure);
bool WriteTownVisualObjects(const TownVisualObjectSet &objects, const std::filesystem::path &path, std::string &failure);
bool HasTownVisualObjects();
const TownVisualObjectSet &GetTownVisualObjects();
const TownVisualObjectAudit &GetTownVisualObjectAudit();
void ClearTownVisualObjects();

/** One-time explicit authoring capture. Never called by runtime startup. Writes
 * tristram.d3dvis plus JSON/texture evidence into a new, private directory. */
bool ExportTownViewFixedObjects(const std::filesystem::path &directory, std::string &failure);

} // namespace devilution
