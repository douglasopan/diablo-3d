#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "engine/assets.hpp"
#include "engine/random.hpp"
#include "engine/render/town_editor_map.hpp"
#include "engine/render/town_model_import.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "mods/mod_identity.h"
#include "utils/paths.h"

namespace devilution {
namespace town_editor_checks {

inline void Word(std::vector<std::byte> &data, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8)
		data.push_back(static_cast<std::byte>((value >> shift) & 255));
}

inline std::vector<std::byte> ModelFixture()
{
	std::vector<std::byte> data;
	for (char c : std::string_view("D3DMESH1"))
		data.push_back(static_cast<std::byte>(c));
	Word(data, 1);
	Word(data, 2);
	Word(data, 1);
	for (float f : { 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F })
		Word(data, std::bit_cast<uint32_t>(f));
	for (uint8_t c : { 10, 20, 30, 40, 50, 60 })
		data.push_back(static_cast<std::byte>(c));
	return data;
}

inline std::string GeometryState(const std::vector<TownSceneModel> &models)
{
	std::string state;
	const auto scalar = [&](const auto &value) { state.append(reinterpret_cast<const char *>(&value), sizeof(value)); };
	const auto text = [&](const std::string &value) { scalar(value.size()); state += value; };
	scalar(models.size());
	for (const auto &model : models) {
		scalar(model.kind);
		scalar(model.minTile.x); scalar(model.minTile.y); scalar(model.maxTile.x); scalar(model.maxTile.y);
		scalar(model.physicalBounds.minX); scalar(model.physicalBounds.minZ); scalar(model.physicalBounds.maxX); scalar(model.physicalBounds.maxZ);
		scalar(model.physicalBounds.wallHeight); scalar(model.physicalBounds.closed);
		scalar(model.externalModel);
		scalar(model.triangles.size());
		for (const auto &triangle : model.triangles) {
			for (const auto &v : triangle.vertices) { scalar(v.x); scalar(v.height); scalar(v.z); scalar(v.u); scalar(v.v); }
			scalar(triangle.material); scalar(triangle.sourceTile.x); scalar(triangle.sourceTile.y); scalar(triangle.pickTile.x); scalar(triangle.pickTile.y);
			scalar(triangle.nativeProjection); scalar(triangle.normal.x); scalar(triangle.normal.height); scalar(triangle.normal.z);
			scalar(triangle.surfaceRole); scalar(triangle.surfaceDetail);
		}
		const auto &art = model.nativeArtwork;
		scalar(art.referenceTile.x); scalar(art.referenceTile.y); scalar(art.minTile.x); scalar(art.minTile.y); scalar(art.maxTile.x); scalar(art.maxTile.y);
		scalar(art.pixelOrigin.x); scalar(art.pixelOrigin.y); scalar(art.pixelSize.x); scalar(art.pixelSize.y); scalar(art.enabled);
		scalar(art.fringeMinPiece); scalar(art.fringeMaxPiece);
		scalar(model.materialPatches.size());
		for (const auto &patch : model.materialPatches) {
			scalar(patch.surfaceDetail); scalar(patch.material);
			for (const auto &v : patch.referencePlane) { scalar(v.x); scalar(v.height); scalar(v.z); scalar(v.u); scalar(v.v); }
			scalar(patch.uvMin); scalar(patch.uvMax); scalar(patch.repeatWorldSize); scalar(patch.repeat);
			scalar(patch.sourceMin.x); scalar(patch.sourceMin.y); scalar(patch.sourceMax.x); scalar(patch.sourceMax.y);
		}
		scalar(static_cast<bool>(model.importedTexture));
		if (model.importedTexture) {
			scalar(model.importedTexture->width); scalar(model.importedTexture->height);
			scalar(model.importedTexture->rgb.size());
			state.append(reinterpret_cast<const char *>(model.importedTexture->rgb.data()), model.importedTexture->rgb.size());
		}
		const auto *room = model.cabinInterior.get();
		scalar(room); // A failed replacement must retain its exact existing adjunct.
		const auto &audit = model.runtimeAudit;
		text(audit.assetPath); text(audit.sourceKind); text(audit.sha256); text(audit.expectedSha256); text(audit.revision); text(audit.status); text(audit.failure);
	}
	return state;
}

inline std::string NativeState()
{
	std::string state;
	const auto append = [&](const auto &value) { state.append(reinterpret_cast<const char *>(&value), sizeof(value)); };
	append(dPiece); append(dPlayer); append(dMonster); append(dFlags);
	append(currlevel); append(leveltype); append(ViewPosition);
	const auto random = GetLCGEngineState();
	append(random);
	return state;
}

inline std::string Manifest(const TownEditorModelBinding &binding, std::string_view modelPath, std::string_view hash, bool spawn,
    std::string_view variant = {}, std::string_view nativeMin = {}, std::string_view instance = {}, std::string_view asset = {}, std::string_view interior = "none")
{
	const std::string id = instance.empty() ? std::string(binding.instanceId) : std::string(instance);
	return "[Scene]\nformat=d3d.town-map\nschemaVersion=1\nedition=" + std::string(spawn ? "shareware" : "retail") + "\ninstance=" + id
	    + "\n\n[" + id + "]\nassetId=" + std::string(asset.empty() ? binding.assetId : asset)
	    + "\nvariantId=" + std::string(variant.empty() ? binding.variantId : variant)
	    + "\nnativeMin=" + (nativeMin.empty() ? std::to_string(binding.nativeMin.x) + ',' + std::to_string(binding.nativeMin.y) : std::string(nativeMin))
	    + "\nnativeMax=" + std::to_string(binding.nativeMax.x) + ',' + std::to_string(binding.nativeMax.y)
	    + "\nmodel=" + std::string(modelPath) + "\nsha256=" + std::string(hash) + "\nrevision=fixture-v1\ninterior=" + std::string(interior) + '\n';
}

} // namespace town_editor_checks

/** Isolated local fixtures only. Call after InitializeTownDiagnostic, with the
 * capture directory also serving as PrefPath; no real profile or save is edited.
 */
template <typename Checker>
void CheckTownEditorMapLoader(const std::filesystem::path &output, Checker check)
{
	using namespace town_editor_checks;
	check(std::filesystem::weakly_canonical(output) == std::filesystem::weakly_canonical(paths::PrefPath()),
	    "editor-map fixtures use only the isolated diagnostic profile");
	check(ModHashToHex(ComputeBytesSha256({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
	        && ModHashToHex(ComputeBytesSha256(std::as_bytes(std::span<const char>("abc", 3)))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
	    "byte-span SHA-256 matches independent empty and abc standard vectors");
	const auto baseline = GetTownScene();
	const std::string original = GeometryState(baseline);
	const std::string native = NativeState();
	const auto bindings = GetTownEditorModelBindings();
	const auto well = std::find_if(bindings.begin(), bindings.end(), [](const auto &b) { return b.instanceId == "well"; });
	const auto cabin = std::find_if(bindings.begin(), bindings.end(), [](const auto &b) { return b.instanceId == "cabin-east"; });
	check(well != bindings.end() && cabin != bindings.end(), "editor fixture resolves existing catalog identities rather than invented IDs");
	const auto fixture = ModelFixture();
	const std::string hash = ModHashToHex(ComputeBytesSha256(fixture));
	const std::string path = "d3d-models/editor/__checks_well.d3d";
	const std::string malformedPath = "d3d-models/editor/__checks_malformed.d3d";
	const auto write = [&](const std::filesystem::path &relative, std::span<const std::byte> data) {
		std::filesystem::create_directories((output / relative).parent_path());
		std::ofstream stream(output / relative, std::ios::binary | std::ios::trunc);
		stream.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
		check(stream.good(), "write a bounded synthetic model only in the isolated diagnostic profile");
	};
	write(path, fixture);
#ifndef UNPACKED_MPQS
	{
		AssetRef originalRef = FindAsset(path);
		check(originalRef.ok() && originalRef.isOverridden, "fixture model resolves as a profile override before moving its asset reference");
		AssetRef moved(std::move(originalRef));
		check(moved.ok() && moved.isOverridden && originalRef.directHandle == nullptr,
		    "AssetRef move construction preserves override provenance and transfers the handle");
		AssetRef assigned;
		assigned = std::move(moved);
		check(assigned.ok() && assigned.isOverridden && moved.directHandle == nullptr,
		    "AssetRef move assignment preserves override provenance and transfers the handle");
	}
#endif
	auto malformed = fixture;
	malformed[0] = std::byte { 'X' };
	write(malformedPath, malformed);
	const auto bytes = [](const std::string &text) { return std::as_bytes(std::span<const char>(text)); };
	const std::string valid = Manifest(*well, path, hash, gbIsSpawn);
	{
		auto models = baseline;
		check(ParseTownEditorMap(models, bytes(valid)), "valid manifest loads a known well replacement through the actual asset resolver");
		const auto target = std::find_if(models.begin(), models.end(), [&](const auto &model) { return FindTownEditorModelBinding(model) == &*well; });
		check(target != models.end() && target->externalModel && target->triangles.size() == 1 && target->importedTexture
		        && target->importedTexture->rgb == std::vector<uint8_t> { 10, 20, 30, 40, 50, 60 }
		        && target->runtimeAudit.assetPath == path && target->runtimeAudit.sha256 == hash && target->runtimeAudit.expectedSha256 == hash
		        && target->runtimeAudit.sourceKind == "override" && target->runtimeAudit.status == "matched" && target->runtimeAudit.revision == "fixture-v1"
		        && target->minTile == well->nativeMin && target->maxTile == well->nativeMax,
		    "the applied model identifies the exact decoded bytes, source, revision and unchanged native binding");
		const auto &audit = GetTownEditorMapAudit();
		check(audit.status == "matched" && audit.instances.size() == 1 && audit.instances[0].sha256 == hash
		        && audit.sha256 == ModHashToHex(ComputeBytesSha256(bytes(valid))),
		    "manifest and instance audit hashes describe their actual input buffers");
	}
	const auto refused = [&](const std::string &text, std::string_view reason, std::string_view readHash = {}) {
		auto models = baseline;
		check(!ParseTownEditorMap(models, bytes(text)) && GeometryState(models) == original,
		    "rejected editor override preserves every existing model: " + std::string(reason));
		const auto &audit = GetTownEditorMapAudit();
		check(audit.status == "rejected" && (audit.failure == reason || (audit.instances.size() == 1 && audit.instances[0].failure == reason))
		        && (readHash.empty() || (audit.instances.size() == 1 && audit.instances[0].sha256 == readHash)),
		    "rejected editor override records its concrete failure and any bytes actually read: " + std::string(reason));
	};
	refused(Manifest(*well, path, std::string(64, '0'), gbIsSpawn), "hash-mismatch", hash);
	refused(Manifest(*well, "d3d-models/editor/__checks_missing_8412b17b.d3d", hash, gbIsSpawn), "missing");
	refused(Manifest(*well, malformedPath, ModHashToHex(ComputeBytesSha256(malformed)), gbIsSpawn), "parse", ModHashToHex(ComputeBytesSha256(malformed)));
	std::string duplicate = valid;
	duplicate.insert(duplicate.find("\n\n["), "\ninstance=well");
	refused(duplicate, "duplicate-or-empty-instance");
	refused(Manifest(*well, path, hash, gbIsSpawn, {}, {}, "unknown-building"), "unknown-instance");
	refused(Manifest(*well, path, hash, gbIsSpawn, {}, "59,70"), "native-bounds");
	refused(Manifest(*well, path, hash, gbIsSpawn, "not-a-catalog-variant"), "catalog-binding");
	refused(Manifest(*well, path, hash, gbIsSpawn, {}, {}, {}, "tristram.arch.house-common"), "catalog-binding");
	refused(Manifest(*well, "d3d-models/editor/../__checks_well.d3d", hash, gbIsSpawn), "model-path");
	refused(Manifest(*well, path, hash, !gbIsSpawn), "edition");
	refused(Manifest(*well, path, hash, gbIsSpawn, {}, {}, {}, {}, "candles"), "unsupported-interior");
	refused(Manifest(*cabin, path, hash, gbIsSpawn), "preserved-cabin-interior");
	{
		const auto manifestPath = output / "d3d-maps/tristram.ini";
		check(!std::filesystem::exists(manifestPath), "actual manifest-resolution fixture starts without an existing user export");
		struct RemoveFixture {
			std::filesystem::path path;
			~RemoveFixture() { std::error_code error; std::filesystem::remove(path, error); }
		} remove { manifestPath };
		write("d3d-maps/tristram.ini", bytes(valid));
		auto models = baseline;
		check(LoadTownEditorMap(models) && GetTownEditorMapAudit().sourceKind == "override" && GetTownEditorMapAudit().sha256 == ModHashToHex(ComputeBytesSha256(bytes(valid))),
		    "optional manifest loader reads the actual profile bytes and identifies their source");
	}
	{
		auto models = baseline;
		check(!LoadTownEditorMap(models) && GetTownEditorMapAudit().status == "missing" && GeometryState(models) == original,
		    "an absent optional editor manifest leaves the whole normal scene intact");
	}
	check(GeometryState(GetTownScene()) == original && NativeState() == native,
	    "all editor-map checks preserve the actual live scene, native map, collision occupancy and simulation RNG");
}

} // namespace devilution
