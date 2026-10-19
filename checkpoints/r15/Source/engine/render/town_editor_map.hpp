#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/render/town_scene.hpp"

namespace devilution {

/** Catalog identity of an existing native scene group, never a collision edit. */
struct TownEditorModelBinding {
	std::string_view instanceId;
	std::string_view assetId;
	std::string_view variantId;
	TownSceneKind kind;
	Point nativeMin;
	Point nativeMax;
};

struct TownEditorInstanceAudit {
	std::string instanceId;
	std::string assetPath;
	std::string expectedSha256;
	std::string sha256;
	std::string revision;
	std::string status;
	std::string failure;
};

struct TownEditorMapAudit {
	std::string assetPath = "d3d-maps/tristram.ini";
	std::string sourceKind;
	std::string sha256;
	std::string status = "not-loaded";
	std::string failure;
	std::vector<TownEditorInstanceAudit> instances;
};

/** Water/warp variants reflect native state; returned binding storage is stable. */
std::span<const TownEditorModelBinding> GetTownEditorModelBindings();
const TownEditorModelBinding *FindTownEditorModelBinding(const TownSceneModel &model);

/** Decode a bounded manifest and load named assets through FindAsset. Each valid
 * replacement commits atomically. Returns false if any requested entry fails;
 * its baseline remains intact and the audit explains the failed attempt.
 */
bool ParseTownEditorMap(std::vector<TownSceneModel> &models, std::span<const std::byte> data);
bool LoadTownEditorMap(std::vector<TownSceneModel> &models);
const TownEditorMapAudit &GetTownEditorMapAudit();
void ResetTownEditorMapAudit();

} // namespace devilution
