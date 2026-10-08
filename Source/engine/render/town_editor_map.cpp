#include "engine/render/town_editor_map.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <new>
#include <set>
#include <utility>

#include "engine/assets.hpp"
#include "engine/render/town_model_import.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/trigs.h"
#include "mods/mod_identity.h"
#include "player.h"
#include "quests.h"
#include "utils/ini.hpp"
#include "utils/log.hpp"

namespace devilution {
namespace {

constexpr size_t MaxManifestBytes = 128 * 1024;
constexpr std::string_view ManifestPath = "d3d-maps/tristram.ini";
TownEditorMapAudit MapAudit;

std::array<TownEditorModelBinding, 14> Bindings { {
	{ "tavern-main", "tristram.arch.tavern", "main-with-wing", TownSceneKind::Tavern, { 46, 54 }, { 53, 63 } },
	{ "tavern-wing", "tristram.arch.tavern", "main-with-wing", TownSceneKind::Tavern, { 53, 56 }, { 56, 61 } },
	{ "smithy-house", "tristram.arch.smithy", "main-with-open-bay", TownSceneKind::Smithy, { 60, 56 }, { 71, 60 } },
	{ "smithy-forge", "tristram.arch.smithy", "main-with-open-bay", TownSceneKind::Smithy, { 60, 60 }, { 63, 63 } },
	{ "house-gillian", "tristram.arch.house-common", "gillian", TownSceneKind::House, { 36, 64 }, { 42, 68 } },
	{ "house-pepin", "tristram.arch.house-pepin", "pepin-hip-roof", TownSceneKind::House, { 46, 74 }, { 55, 80 } },
	{ "house-adria", "tristram.arch.house-adria", "adria-open-shed", TownSceneKind::House, { 74, 16 }, { 79, 21 } },
	{ "house-north", "tristram.arch.house-common", "north", TownSceneKind::House, { 46, 40 }, { 54, 46 } },
	{ "house-southeast", "tristram.arch.house-common", "farnham", TownSceneKind::House, { 67, 78 }, { 72, 82 } },
	{ "cabin-west", "tristram.arch.cabin", "west-short", TownSceneKind::Cabin, { 26, 48 }, { 30, 52 } },
	{ "cabin-east", "tristram.arch.cabin", "east-long", TownSceneKind::Cabin, { 70, 66 }, { 74, 72 } },
	{ "well", "tristram.arch.well", "clean-water", TownSceneKind::Well, { 60, 70 }, { 61, 71 } },
	{ "cathedral", "tristram.arch.cathedral-exterior", "exterior-with-bell-tower", TownSceneKind::Cathedral, { 15, 14 }, { 29, 28 } },
	{ "catacombs-entrance", "tristram.arch.catacombs-entrance", "closed-warp", TownSceneKind::Crypt, { 48, 18 }, { 50, 21 } },
} };

std::string_view Trim(std::string_view value)
{
	const auto begin = value.find_first_not_of(" \t\r\n");
	if (begin == std::string_view::npos)
		return {};
	return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

bool ParsePoint(std::string_view text, Point &point)
{
	const auto comma = text.find(',');
	if (comma == std::string_view::npos || text.find(',', comma + 1) != std::string_view::npos)
		return false;
	const auto x = Trim(text.substr(0, comma)), z = Trim(text.substr(comma + 1));
	if (x.empty() || z.empty())
		return false;
	int px, pz;
	const auto a = std::from_chars(x.data(), x.data() + x.size(), px);
	const auto b = std::from_chars(z.data(), z.data() + z.size(), pz);
	if (a.ec != std::errc() || b.ec != std::errc()
	    || a.ptr != x.data() + x.size() || b.ptr != z.data() + z.size()
	    || px < 0 || px >= MAXDUNX || pz < 0 || pz >= MAXDUNY)
		return false;
	point = { px, pz };
	return true;
}

bool ValidEditorPath(std::string_view path)
{
	constexpr std::string_view Prefix = "d3d-models/editor/";
	if (!path.starts_with(Prefix) || !path.ends_with(".d3d") || path.size() > 256
	    || path.size() <= Prefix.size() + 4 || path.find("..") != std::string_view::npos || path.find("//") != std::string_view::npos)
		return false;
	for (char c : path)
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '_' || c == '-'))
			return false;
	return true;
}

bool KnownKeys(const Ini &ini, std::string_view section, std::span<const std::string_view> allowed, bool repeatedInstances = false)
{
	for (const std::string &key : ini.getKeys(section)) {
		if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()
		    || ((key != "instance" || !repeatedInstances) && ini.get(section, key).size() != 1))
			return false;
	}
	return true;
}

bool RejectManifest(std::string_view reason)
{
	MapAudit.status = "rejected";
	MapAudit.failure = reason;
	LogWarn("Town editor map rejected: {}", reason);
	return false;
}

bool ParseManifest(std::vector<TownSceneModel> &models, std::span<const std::byte> data)
{
	if (data.empty() || data.size() > MaxManifestBytes)
		return RejectManifest("size");
	MapAudit.sha256 = ModHashToHex(ComputeBytesSha256(data));
	std::string_view text(reinterpret_cast<const char *>(data.data()), data.size());
	if (text.find('\0') != std::string_view::npos)
		return RejectManifest("nul-byte");
	if (text.starts_with("\xEF\xBB\xBF"))
		text.remove_prefix(3);
	auto parsed = Ini::parse(text);
	if (!parsed)
		return RejectManifest("parse");
	const Ini &ini = *parsed;
	constexpr std::array<std::string_view, 4> SceneKeys { "format", "schemaVersion", "edition", "instance" };
	if (!KnownKeys(ini, "Scene", SceneKeys, true) || !ini.getKeys("").empty())
		return RejectManifest("scene-fields");
	if (ini.getString("Scene", "format") != "d3d.town-map" || ini.getString("Scene", "schemaVersion") != "1")
		return RejectManifest("schema");
	if (gbIsHellfire || ini.getString("Scene", "edition") != (gbIsSpawn ? "shareware" : "retail"))
		return RejectManifest("edition");
	const auto requests = ini.get("Scene", "instance");
	if (requests.size() > Bindings.size())
		return RejectManifest("instance-count");
	std::set<std::string_view> instanceIds;
	for (const auto &request : requests)
		if (request.value.empty() || !instanceIds.insert(request.value).second)
			return RejectManifest("duplicate-or-empty-instance");
	// Ini accepts repeated section declarations. Reject these and sections not
	// explicitly listed, rather than silently ignoring an editor export typo.
	std::set<std::string_view> sections;
	for (size_t begin = 0; begin < text.size();) {
		const auto end = text.find('\n', begin);
		const auto line = Trim(text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin));
		if (line.starts_with('[')) {
			const auto close = line.find(']');
			if (close == std::string_view::npos)
				return RejectManifest("section");
			const auto section = Trim(line.substr(1, close - 1));
			if (!sections.insert(section).second || (section != "Scene" && !instanceIds.contains(section)))
				return RejectManifest("duplicate-or-unlisted-section");
		}
		if (end == std::string_view::npos)
			break;
		begin = end + 1;
	}
	constexpr std::array<std::string_view, 8> ModelKeys { "assetId", "variantId", "nativeMin", "nativeMax", "model", "sha256", "revision", "interior" };
	bool allApplied = true;
	bool anyApplied = false;
	MapAudit.instances.reserve(requests.size());
	for (const auto &request : requests) {
		TownEditorInstanceAudit entry;
		entry.instanceId = request.value;
		entry.assetPath = ini.getString(request.value, "model");
		entry.expectedSha256 = ini.getString(request.value, "sha256");
		entry.revision = ini.getString(request.value, "revision");
		entry.status = "fallback";
		const auto binding = std::find_if(GetTownEditorModelBindings().begin(), GetTownEditorModelBindings().end(), [&](const TownEditorModelBinding &value) { return value.instanceId == request.value; });
		auto target = models.end();
		if (binding != GetTownEditorModelBindings().end())
			target = std::find_if(models.begin(), models.end(), [&](const TownSceneModel &model) {
				return model.kind == binding->kind && model.minTile == binding->nativeMin && model.maxTile == binding->nativeMax;
			});
		Point nativeMin, nativeMax;
		std::array<uint8_t, 32> expected;
		if (binding == GetTownEditorModelBindings().end())
			entry.failure = "unknown-instance";
		else if (target == models.end())
			entry.failure = "native-model-missing";
		else if (!KnownKeys(ini, request.value, ModelKeys)
		    || std::any_of(ModelKeys.begin(), ModelKeys.end(), [&](std::string_view key) { return ini.get(request.value, key).size() != 1 || ini.getString(request.value, key).empty(); }))
			entry.failure = "instance-fields";
		else if (ini.getString(request.value, "assetId") != binding->assetId || ini.getString(request.value, "variantId") != binding->variantId)
			entry.failure = "catalog-binding";
		else if (!ParsePoint(ini.getString(request.value, "nativeMin"), nativeMin) || !ParsePoint(ini.getString(request.value, "nativeMax"), nativeMax)
		    || nativeMin != binding->nativeMin || nativeMax != binding->nativeMax)
			entry.failure = "native-bounds";
		else if (!ValidEditorPath(entry.assetPath))
			entry.failure = "model-path";
		else if (!HexToModHash(entry.expectedSha256, expected))
			entry.failure = "hash-format";
		else if (entry.revision.empty() || entry.revision.size() > 128
		    || std::any_of(entry.revision.begin(), entry.revision.end(), [](unsigned char c) { return c < 32 || c == 127; }))
			entry.failure = "revision";
		else if (ini.getString(request.value, "interior") != "none")
			entry.failure = "unsupported-interior";
		else if (binding->instanceId == "cabin-east" || (target->cabinInterior && !target->cabinInterior->fireSources.empty()))
			entry.failure = "preserved-cabin-interior";
		else {
			try {
				TownSceneModel candidate = *target;
				TownModelRuntimeAudit attempt;
				if (LoadTownModelOverride(candidate, entry.assetPath, entry.expectedSha256, entry.revision, &attempt)) {
					entry.sha256 = candidate.runtimeAudit.sha256;
					entry.expectedSha256 = candidate.runtimeAudit.expectedSha256;
					entry.status = "matched";
					*target = std::move(candidate);
					anyApplied = true;
				} else {
					entry.sha256 = attempt.sha256;
					entry.failure = attempt.failure;
				}
			} catch (const std::bad_alloc &) {
				entry.failure = "allocation";
			}
		}
		if (!entry.failure.empty()) {
			allApplied = false;
			LogWarn("Town editor instance '{}' retained its baseline: {}", entry.instanceId, entry.failure);
		}
		MapAudit.instances.push_back(std::move(entry));
	}
	MapAudit.status = allApplied ? "matched" : (anyApplied ? "partial" : "rejected");
	MapAudit.failure = allApplied ? "" : "instance-failure";
	return allApplied;
}

} // namespace

std::span<const TownEditorModelBinding> GetTownEditorModelBindings()
{
	Bindings[11].variantId = Quests[Q_PWATER]._qactive != QUEST_DONE && Quests[Q_PWATER]._qactive != QUEST_NOTAVAIL ? "poisoned-water" : "clean-water";
	Bindings[13].variantId = MyPlayer != nullptr && IsWarpOpen(DTYPE_CATACOMBS) ? "open-warp" : "closed-warp";
	return Bindings;
}

const TownEditorModelBinding *FindTownEditorModelBinding(const TownSceneModel &model)
{
	const auto bindings = GetTownEditorModelBindings();
	const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const TownEditorModelBinding &binding) {
		return binding.kind == model.kind && binding.nativeMin == model.minTile && binding.nativeMax == model.maxTile;
	});
	return found == bindings.end() ? nullptr : &*found;
}

bool ParseTownEditorMap(std::vector<TownSceneModel> &models, std::span<const std::byte> data)
{
	MapAudit = {};
	MapAudit.sourceKind = "memory";
	try {
		return ParseManifest(models, data);
	} catch (const std::bad_alloc &) {
		return RejectManifest("allocation");
	}
}

bool LoadTownEditorMap(std::vector<TownSceneModel> &models)
{
	MapAudit = {};
	try {
		AssetRef ref = FindAsset(ManifestPath);
		if (!ref.ok()) {
			MapAudit.status = "missing"; // Optional: preserve the complete normal scene.
			return false;
		}
#ifdef UNPACKED_MPQS
		MapAudit.sourceKind = "unpacked-file";
#else
		MapAudit.sourceKind = ref.archive != nullptr ? "archive" : (ref.isOverridden ? "override" : "loose-file");
#endif
		const size_t size = ref.size();
		if (size == 0 || size > MaxManifestBytes)
			return RejectManifest("size");
		AssetHandle handle = OpenAsset(std::move(ref));
		if (!handle.ok())
			return RejectManifest("open");
		std::vector<std::byte> data(size);
		if (!handle.read(data.data(), data.size()))
			return RejectManifest("read");
		return ParseManifest(models, data);
	} catch (const std::bad_alloc &) {
		return RejectManifest("allocation");
	}
}

const TownEditorMapAudit &GetTownEditorMapAudit()
{
	return MapAudit;
}

void ResetTownEditorMapAudit()
{
	MapAudit = {};
}

} // namespace devilution
