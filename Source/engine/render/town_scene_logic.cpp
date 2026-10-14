#include "engine/render/town_scene_logic.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <map>
#include <new>
#include <set>
#include <utility>

#include "engine/assets.hpp"
#include "engine/render/town_editor_map.hpp"
#include "engine/render/town_visual_objects.hpp"
#include "engine/render/town_visual_scene.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "mods/mod_identity.h"
#include "utils/ini.hpp"
#include "utils/log.hpp"

namespace devilution {
namespace {

constexpr size_t MaxPatchBytes = 512 * 1024;
constexpr size_t MaxManifestBytes = 256 * 1024;
constexpr size_t MaxStaticBytes = 256 * 1024 * 1024;
constexpr size_t MaxObjects = 128;
constexpr size_t MaxCells = 2048;
constexpr size_t MaxRoutePoints = 512;
constexpr std::array<std::string_view, 4> RouteNames { "north", "south", "east", "west" };
TownSceneLogicAudit LogicAudit;

struct RequestCell {
	Point tile { 0, 0 }, reference { 0, 0 };
	uint16_t before = 0, special = 0, after = 0;
};

bool Reject(std::string_view failure)
{
	LogicAudit.status = "rejected";
	LogicAudit.failure = failure;
	LogWarn("Persistent town native patch rejected: {}", failure);
	return false;
}

std::string_view Trim(std::string_view text)
{
	const auto first = text.find_first_not_of(" \t\r\n");
	return first == std::string_view::npos ? std::string_view {} : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool Hash(std::string_view text)
{
	std::array<uint8_t, 32> digest;
	return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) && HexToModHash(text, digest);
}

bool AssetPath(std::string_view path)
{
	return path.starts_with("d3d-scenes/static/") && path.ends_with(".d3dvis") && path.size() <= 256
	    && path.find("..") == std::string_view::npos && path.find("//") == std::string_view::npos
	    && std::all_of(path.begin(), path.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.'; });
}

bool SafeCell(Point p)
{
	// Keep the native outer guard and require original captured town ground.
	return p.x >= 2 && p.x <= 94 && p.y >= 2 && p.y <= 94;
}

int CellKey(Point p)
{
	return p.y * MAXDUNX + p.x;
}

bool Numbers(std::string_view text, std::span<unsigned> out)
{
	for (size_t i = 0; i < out.size(); ++i) {
		const auto comma = text.find(',');
		const auto token = Trim(text.substr(0, comma));
		if (token.empty()) return false;
		const auto result = std::from_chars(token.data(), token.data() + token.size(), out[i]);
		if (result.ec != std::errc() || result.ptr != token.data() + token.size()) return false;
		if (i + 1 == out.size()) return comma == std::string_view::npos;
		if (comma == std::string_view::npos) return false;
		text.remove_prefix(comma + 1);
	}
	return false;
}

bool Keys(const Ini &ini, std::string_view section, std::span<const std::string_view> known, std::string_view repeated = {})
{
	for (const auto &key : ini.getKeys(section)) {
		if (std::find(known.begin(), known.end(), key) == known.end()) return false;
		if (key != repeated && ini.get(section, key).size() != 1) return false;
	}
	return true;
}

bool ProtectedAnchor(Point p)
{
	// Native NPC occupancy (including cows), portals, entrances, and quest tiles.
	constexpr std::array<Point, 17> Anchors { Point { 62, 63 }, { 55, 79 }, { 24, 32 }, { 55, 62 }, { 62, 71 }, { 71, 84 }, { 80, 20 }, { 43, 66 }, { 11, 53 }, { 58, 16 }, { 56, 14 }, { 59, 20 }, { 57, 40 }, { 59, 40 }, { 61, 40 }, { 63, 40 }, { 25, 31 } };
	for (const auto anchor : Anchors)
		if (std::abs(p.x - anchor.x) <= 2 && std::abs(p.y - anchor.y) <= 2) return true;
	for (const auto &binding : GetTownEditorModelBindings())
		if (p.x >= binding.nativeMin.x - 1 && p.x <= binding.nativeMax.x + 1 && p.y >= binding.nativeMin.y - 1 && p.y <= binding.nativeMax.y + 1) return true;
	// The cathedral's native artwork apron extends beyond its model footprint.
	if (p.x >= 10 && p.x <= 33 && p.y >= 8 && p.y <= 33) return true;
	if (p.x >= 46 && p.x <= 51 && p.y >= 18 && p.y <= 24) return true;
	if (p.x >= 14 && p.x <= 20 && p.y >= 66 && p.y <= 73) return true;
	if (p.x >= 34 && p.x <= 48 && p.y >= 76 && p.y <= 83) return true;
	return false;
}

bool Grass(uint16_t piece)
{
	// Retail town's grass family; a walkable arbitrary SOL tile is insufficient.
	if (piece > 17 || HasAnyOf(SOLData[piece], TileProperties::Solid | TileProperties::BlockMissile | TileProperties::Trap)) return false;
	const auto &blocks = DPieceMicros[piece].mt;
	if (!blocks[0].hasValue() || !blocks[1].hasValue()) return false;
	return std::none_of(std::begin(blocks) + 2, std::end(blocks), [](const LevelCelBlock &block) { return block.hasValue(); });
}

bool Resolve(std::string_view path, std::vector<std::byte> &bytes, std::string &failure)
{
	AssetRef ref = FindAsset(path);
	if (!ref.ok()) { failure = "asset-missing"; return false; }
	const size_t size = ref.size();
	const size_t maximum = path.ends_with(".ini") ? MaxPatchBytes : MaxStaticBytes;
	if (size == 0 || size > maximum) { failure = "asset-size"; return false; }
	AssetHandle handle = OpenAsset(std::move(ref));
	if (!handle.ok()) { failure = "asset-open"; return false; }
	bytes.resize(size);
	if (!handle.read(bytes.data(), bytes.size())) { failure = "asset-read"; return false; }
	return true;
}

bool MainReferences(std::span<const std::byte> data, const TownSceneLogicPatchContext &context)
{
	if (data.empty() || data.size() > MaxManifestBytes) return false;
	std::string_view text(reinterpret_cast<const char *>(data.data()), data.size());
	if (text.find('\0') != std::string_view::npos) return false;
	auto ini = Ini::parse(text);
	return ini && ini->get("Scene", "worldPatch").size() == 1 && ini->get("Scene", "worldPatchSha256").size() == 1
	    && ini->getString("Scene", "worldPatch") == context.patchPath && ini->getString("Scene", "worldPatchSha256") == context.patchSha256
	    && ini->getString("Scene", "staticData") == context.targetStaticData && ini->getString("Scene", "staticSha256") == context.targetStaticSha256;
}

bool ParseApply(std::span<const std::byte> data, const TownSceneLogicPatchContext &context, const TownSceneLogicAssetResolver &resolver)
{
	LogicAudit.path = context.patchPath;
	LogicAudit.expectedSha256 = context.patchSha256;
	if (gbIsHellfire || gbIsSpawn || gbIsMultiplayer || leveltype != DTYPE_TOWN) return Reject("patch-edition-level-or-multiplayer");
	if (context.patchPath != TownSceneLogicPatchPath || !Hash(context.patchSha256) || !AssetPath(context.targetStaticData) || !Hash(context.targetStaticSha256)) return Reject("patch-context");
	if (data.empty() || data.size() > MaxPatchBytes) return Reject("patch-size");
	LogicAudit.sha256 = ModHashToHex(ComputeBytesSha256(data));
	if (LogicAudit.sha256 != context.patchSha256) return Reject("patch-sha256");
	std::string_view text(reinterpret_cast<const char *>(data.data()), data.size());
	if (text.find('\0') != std::string_view::npos) return Reject("patch-nul");
	auto parsed = Ini::parse(text);
	if (!parsed) return Reject("patch-ini");
	const Ini &ini = *parsed;
	constexpr std::array<std::string_view, 10> HeaderKeys { "format", "schemaVersion", "edition", "operation", "sourceManifestSha256", "baselineStaticData", "baselineStaticSha256", "targetStaticSha256", "intentSha256", "object" };
	if (!ini.getKeys("").empty() || !Keys(ini, "WorldPatch", HeaderKeys, "object")) return Reject("patch-header-fields");
	for (const auto key : HeaderKeys)
		if (key != "object" && (ini.get("WorldPatch", key).size() != 1 || ini.getString("WorldPatch", key).empty())) return Reject("patch-required-field");
	if (ini.getString("WorldPatch", "format") != "d3d.town-world-patch" || ini.getString("WorldPatch", "schemaVersion") != "1"
	    || ini.getString("WorldPatch", "edition") != "retail" || ini.getString("WorldPatch", "operation") != "create-cardinal-exits") return Reject("patch-schema");
	LogicAudit.sourceManifestSha256 = ini.getString("WorldPatch", "sourceManifestSha256");
	LogicAudit.baselineStaticSha256 = ini.getString("WorldPatch", "baselineStaticSha256");
	LogicAudit.targetStaticSha256 = ini.getString("WorldPatch", "targetStaticSha256");
	LogicAudit.intentSha256 = ini.getString("WorldPatch", "intentSha256");
	const std::string_view baselinePath = ini.getString("WorldPatch", "baselineStaticData");
	if (!Hash(LogicAudit.sourceManifestSha256) || !Hash(LogicAudit.baselineStaticSha256) || !Hash(LogicAudit.targetStaticSha256) || !Hash(LogicAudit.intentSha256)
	    || LogicAudit.targetStaticSha256 != context.targetStaticSha256 || !AssetPath(baselinePath) || baselinePath == context.targetStaticData
	    || LogicAudit.baselineStaticSha256 == LogicAudit.targetStaticSha256) return Reject("patch-static-reference");
	const auto requests = ini.get("WorldPatch", "object");
	if (requests.empty() || requests.size() > MaxObjects) return Reject("patch-object-count");
	std::set<std::string_view> ids, sections;
	for (const auto &request : requests) {
		if ((!request.value.starts_with("rock-") && request.value != "scenery-38-10") || request.value.size() > 32 || !ids.insert(request.value).second) return Reject("patch-object-id");
	}
	for (size_t begin = 0; begin < text.size();) {
		const auto end = text.find('\n', begin);
		const auto line = Trim(text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin));
		if (line.starts_with('[')) {
			const auto close = line.find(']');
			if (close == std::string_view::npos) return Reject("patch-section");
			const auto name = Trim(line.substr(1, close - 1));
			if (!sections.insert(name).second || (name != "WorldPatch" && name != "Routes" && !ids.contains(name))) return Reject("patch-unlisted-section");
		}
		if (end == std::string_view::npos) break;
		begin = end + 1;
	}
	if (sections.size() != ids.size() + 2 || !sections.contains("WorldPatch") || !sections.contains("Routes")) return Reject("patch-missing-section");
	if (!Keys(ini, "Routes", RouteNames) || ini.getKeys("Routes").size() != RouteNames.size()) return Reject("patch-route-fields");
	std::array<std::vector<Point>, 4> routes;
	std::set<int> routeCells;
	for (size_t i = 0; i < routes.size(); ++i) {
		std::string_view list = ini.getString("Routes", RouteNames[i]);
		while (!list.empty()) {
			const auto separator = list.find(';');
			std::array<unsigned, 2> point;
			if (!Numbers(list.substr(0, separator), point) || point[0] > 94 || point[1] > 94) return Reject("patch-route-point");
			const Point tile { static_cast<int>(point[0]), static_cast<int>(point[1]) };
			if (!SafeCell(tile) || routes[i].size() >= MaxRoutePoints || (!routes[i].empty() && std::abs(routes[i].back().x - tile.x) + std::abs(routes[i].back().y - tile.y) != 1)) return Reject("patch-route-step");
			routes[i].push_back(tile); routeCells.insert(CellKey(tile));
			if (separator == std::string_view::npos) break;
			list.remove_prefix(separator + 1);
			if (list.empty()) return Reject("patch-route-trailing-separator");
		}
		if (routes[i].size() < 2) return Reject("patch-route-count");
		const auto start = routes[i].front(), end = routes[i].back();
		if (start.x < 10 || start.x > 84 || start.y < 10 || start.y > 84
		    || (i == 0 && end.y >= 10) || (i == 1 && end.y <= 84) || (i == 2 && end.x <= 84) || (i == 3 && end.x >= 10)) return Reject("patch-route-direction");
	}
	std::vector<std::byte> baselineBytes, targetBytes;
	std::string failure;
	if (!resolver || !resolver(baselinePath, baselineBytes, failure)) return Reject(failure.empty() ? "patch-baseline-missing" : failure);
	if (baselineBytes.empty() || baselineBytes.size() > MaxStaticBytes || ModHashToHex(ComputeBytesSha256(baselineBytes)) != LogicAudit.baselineStaticSha256) return Reject("patch-baseline-sha256");
	TownVisualObjectSet expected;
	if (!ParseTownVisualObjects(baselineBytes, expected, failure)) return Reject("patch-baseline-invalid");
	std::map<std::string_view, size_t> byId;
	for (size_t i = 0; i < expected.objects.size(); ++i) byId.emplace(expected.objects[i].id, i);
	std::map<int, RequestCell> changed;
	std::vector<TownSceneLogicCellAudit> stagedAudit;
	for (const auto &request : requests) {
		const auto found = byId.find(request.value);
		if (found == byId.end()) return Reject("patch-unknown-rock");
		auto &rock = expected.objects[found->second];
		// One measured native scenery obstruction is necessary for the narrow
		// north exit. This is an exact identity/cell/piece exception, never a
		// permission to clear arbitrary solid scenery or change SOL tables.
		const bool northScenery = rock.id == "scenery-38-10";
		const bool allowedClass = northScenery
		    ? rock.kind == TownVisualObjectKind::Scenery && rock.phase == 1 && rock.anchor == Point { 38, 10 }
		        && rock.sourceCells.size() == 1 && rock.sourceCells[0].tile == Point { 38, 10 }
		        && rock.sourceCells[0].piece == 1051 && rock.sourceCells[0].special == 0 && SOLData[1051] == TileProperties::Solid
		    : rock.kind == TownVisualObjectKind::Prop && rock.phase == 2
		        && rock.id == "rock-" + std::to_string(rock.anchor.x) + "-" + std::to_string(rock.anchor.y);
		if (!allowedClass || rock.eventBinding != TownVisualObjectEvent::None || rock.eventState || !rock.visible || rock.nativeSuppressed || rock.groundFallback) return Reject("patch-rock-class");
		constexpr std::array<std::string_view, 1> CellKeys { "cell" };
		if (!Keys(ini, rock.id, CellKeys, "cell") || ini.get(rock.id, "cell").size() != rock.sourceCells.size() || rock.sourceCells.empty()) return Reject("patch-rock-cell-count");
		std::map<int, TownVisualSourceCell> source;
		for (const auto &cell : rock.sourceCells)
			if (!source.emplace(CellKey(cell.tile), cell).second) return Reject("patch-baseline-duplicate-cell");
		bool nearRoute = false;
		for (const auto &raw : ini.get(rock.id, "cell")) {
			std::array<unsigned, 7> value;
			if (!Numbers(raw.value, value) || value[0] > 94 || value[1] > 94 || value[2] > 65535 || value[3] > 255 || value[4] > 94 || value[5] > 94 || value[6] > 17) return Reject("patch-cell-format");
			RequestCell cell { { static_cast<int>(value[0]), static_cast<int>(value[1]) }, { static_cast<int>(value[4]), static_cast<int>(value[5]) }, static_cast<uint16_t>(value[2]), static_cast<uint16_t>(value[3]), static_cast<uint16_t>(value[6]) };
			const auto original = source.find(CellKey(cell.tile));
			const bool allowedPiece = northScenery ? cell.tile == Point { 38, 10 } && cell.before == 1051 : cell.before >= 219 && cell.before <= 233;
			if (!SafeCell(cell.tile) || !SafeCell(cell.reference) || ProtectedAnchor(cell.tile) || original == source.end() || original->second.tile != cell.tile || original->second.piece != cell.before || original->second.special != cell.special
			    || !allowedPiece || cell.special != 0 || !changed.emplace(CellKey(cell.tile), cell).second) return Reject("patch-cell-not-approved-rock");
			source.erase(original);
			if (changed.size() > MaxCells) return Reject("patch-cell-budget");
			if (dPiece[cell.tile.x][cell.tile.y] != cell.before || static_cast<uint8_t>(dSpecial[cell.tile.x][cell.tile.y]) != cell.special) return Reject("patch-native-before");
			if (dPiece[cell.reference.x][cell.reference.y] != cell.after || dSpecial[cell.reference.x][cell.reference.y] != 0 || !Grass(cell.after)) return Reject("patch-native-grass-reference");
			const auto groundId = "ground-" + std::to_string(cell.tile.x) + "-" + std::to_string(cell.tile.y);
			const auto donorId = "ground-" + std::to_string(cell.reference.x) + "-" + std::to_string(cell.reference.y);
			const auto ground = byId.find(groundId), donor = byId.find(donorId);
			if (ground == byId.end() || donor == byId.end()) return Reject("patch-ground-missing");
			auto &floor = expected.objects[ground->second]; const auto &reference = expected.objects[donor->second];
			if (floor.kind != TownVisualObjectKind::Ground || reference.kind != TownVisualObjectKind::Ground || floor.eventBinding != TownVisualObjectEvent::None || reference.eventBinding != TownVisualObjectEvent::None
			    || !floor.visible || floor.nativeSuppressed || !reference.visible || reference.nativeSuppressed || floor.sourceCells.size() != 1 || reference.sourceCells.size() != 1
			    || floor.anchor != cell.tile || reference.anchor != cell.reference || reference.phase != 0
			    || floor.sourceCells[0].tile != cell.tile || floor.sourceCells[0].piece != cell.before || floor.sourceCells[0].special != cell.special
			    || reference.sourceCells[0].tile != cell.reference || reference.sourceCells[0].piece != cell.after || reference.sourceCells[0].special != 0) return Reject("patch-ground-reference");
			floor.texture = reference.texture; floor.sourceCells[0].piece = cell.after; floor.sourceCells[0].special = 0;
			const auto scenery = byId.find("scenery-" + std::to_string(cell.tile.x) + "-" + std::to_string(cell.tile.y));
			if (scenery == byId.end()) return Reject("patch-scenery-missing");
			auto &oldScenery = expected.objects[scenery->second];
			if (oldScenery.kind != TownVisualObjectKind::Scenery || oldScenery.eventBinding != TownVisualObjectEvent::None) return Reject("patch-event-scenery");
			if (oldScenery.visible && !oldScenery.nativeSuppressed) oldScenery.visible = false;
			for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
				nearRoute = nearRoute || routeCells.contains(CellKey({ cell.tile.x + x, cell.tile.y + y }));
			stagedAudit.push_back({ rock.id, cell.tile, cell.before, cell.after, cell.special, 0, cell.reference });
		}
		if (!source.empty() || !nearRoute) return Reject("patch-rock-outside-corridor");
		rock.visible = false;
	}
	// Static props/vegetation are immutable outside the requested rocks. Refuse
	// an overlapping tree, grave, wall, or event even if a rock ID names the cell.
	for (const auto &object : expected.objects) {
		if (ids.contains(object.id) || object.kind == TownVisualObjectKind::Ground || object.kind == TownVisualObjectKind::Scenery) continue;
		for (const auto &cell : object.sourceCells)
			if (changed.contains(CellKey(cell.tile))) return Reject("patch-protected-static-overlap");
	}
	for (const auto &route : routes) for (const auto tile : route) {
		const auto replacement = changed.find(CellKey(tile));
		const uint16_t piece = replacement == changed.end() ? dPiece[tile.x][tile.y] : replacement->second.after;
		if (piece >= MAXTILES || HasAnyOf(SOLData[piece], TileProperties::Solid)) return Reject("patch-route-blocked");
		const auto floor = byId.find("ground-" + std::to_string(tile.x) + "-" + std::to_string(tile.y));
		if (floor == byId.end() || !expected.objects[floor->second].visible || expected.objects[floor->second].nativeSuppressed || expected.objects[floor->second].texture >= expected.textures.size()) return Reject("patch-route-void");
	}
	// Four disconnected slivers across the town window do not constitute exits.
	// Every route must connect to the immutable native new-game town spawn.
	std::array<bool, MAXDUNX * MAXDUNY> reachable {};
	std::array<int, MAXDUNX * MAXDUNY> queue {};
	constexpr Point Hub { 75, 68 };
	auto passable = [&](Point tile) {
		if (!SafeCell(tile)) return false;
		const auto replacement = changed.find(CellKey(tile));
		const uint16_t piece = replacement == changed.end() ? dPiece[tile.x][tile.y] : replacement->second.after;
		return piece < MAXTILES && !HasAnyOf(SOLData[piece], TileProperties::Solid);
	};
	if (!passable(Hub)) return Reject("patch-town-hub-blocked");
	size_t begin = 0, end = 1;
	queue[0] = CellKey(Hub); reachable[queue[0]] = true;
	constexpr std::array<Point, 4> Steps { Point { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	while (begin < end) {
		const int key = queue[begin++];
		const Point current { key % MAXDUNX, key / MAXDUNX };
		for (const auto step : Steps) {
			const Point next { current.x + step.x, current.y + step.y };
			if (!passable(next)) continue;
			const int nextKey = CellKey(next);
			if (reachable[nextKey]) continue;
			reachable[nextKey] = true; queue[end++] = nextKey;
		}
	}
	for (const auto &route : routes)
		if (!reachable[CellKey(route.front())] || !reachable[CellKey(route.back())]) return Reject("patch-route-disconnected-from-town");
	std::vector<std::byte> expectedBytes;
	if (!EncodeTownVisualObjects(expected, expectedBytes, failure)) return Reject("patch-expected-static-invalid");
	if (ModHashToHex(ComputeBytesSha256(expectedBytes)) != LogicAudit.targetStaticSha256) return Reject("patch-unexpected-static-delta");
	if (!resolver(context.targetStaticData, targetBytes, failure)) return Reject(failure.empty() ? "patch-target-missing" : failure);
	if (targetBytes != expectedBytes) return Reject("patch-target-static-bytes");
	std::vector<std::byte> manifest;
	if (!resolver(TownVisualSceneManifestPath, manifest, failure) || !MainReferences(manifest, context)) return Reject("patch-main-manifest-pair");
	const TownVisualSceneAssetResolver visualResolver = [&](std::string_view path, std::vector<std::byte> &bytes, std::string &kind, std::string &error) {
		kind = "native-patch-preflight";
		return resolver(path, bytes, error);
	};
	if (!ValidateTownVisualSceneForLogic(manifest, visualResolver, failure)) return Reject(failure.empty() ? "patch-architecture-preflight" : failure);
	// Finish every allocation, hash and decoder before the first native write.
	LogicAudit.objects = requests.size();
	LogicAudit.cells = std::move(stagedAudit);
	for (size_t i = 0; i < routes.size(); ++i) LogicAudit.routes.push_back({ std::string(RouteNames[i]), std::move(routes[i]) });
	LogicAudit.status = "applied";
	for (const auto &[key, cell] : changed) {
		dPiece[cell.tile.x][cell.tile.y] = cell.after;
		dSpecial[cell.tile.x][cell.tile.y] = 0;
	}
	return true;
}

} // namespace

bool ParseApplyTownSceneLogicPatch(std::span<const std::byte> data, const TownSceneLogicPatchContext &context, const TownSceneLogicAssetResolver &resolver)
{
	LogicAudit = {};
	try {
		return ParseApply(data, context, resolver);
	} catch (const std::bad_alloc &) {
		return Reject("patch-allocation");
	} catch (const std::exception &) {
		return Reject("patch-exception");
	}
}

bool LoadApplyTownSceneLogicPatch()
{
	LogicAudit = {};
	try {
		std::vector<std::byte> manifest, bytes;
		std::string failure;
		if (!Resolve(TownVisualSceneManifestPath, manifest, failure)) { LogicAudit.status = "missing"; return false; }
		if (manifest.size() > MaxManifestBytes) return Reject("patch-main-size");
		auto ini = Ini::parse(std::string_view(reinterpret_cast<const char *>(manifest.data()), manifest.size()));
		if (!ini) return Reject("patch-main-ini");
		const auto path = ini->getString("Scene", "worldPatch"), sha = ini->getString("Scene", "worldPatchSha256");
		if (path.empty() && sha.empty()) { LogicAudit.status = "missing"; return false; }
		TownSceneLogicPatchContext context;
		context.patchPath = path; context.patchSha256 = sha;
		context.targetStaticData = ini->getString("Scene", "staticData"); context.targetStaticSha256 = ini->getString("Scene", "staticSha256");
		if (context.patchPath != TownSceneLogicPatchPath || !Hash(context.patchSha256)) return Reject("patch-main-reference");
		if (!Resolve(context.patchPath, bytes, failure)) return Reject(failure);
		return ParseApply(bytes, context, Resolve);
	} catch (const std::bad_alloc &) {
		return Reject("patch-allocation");
	} catch (const std::exception &) {
		return Reject("patch-exception");
	}
}

const TownSceneLogicAudit &GetTownSceneLogicAudit()
{
	return LogicAudit;
}

void ResetTownSceneLogicAudit()
{
	LogicAudit = {};
}

} // namespace devilution
