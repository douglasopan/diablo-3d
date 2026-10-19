#include "levels/surface_world_storage.hpp"

#include <bitset>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace devilution::surface {
namespace {

constexpr std::size_t RegionSide = 2 * MaximumRegionCoordinate + 1;
constexpr std::size_t RegionCount = RegionSide * RegionSide;
constexpr std::size_t MinimumEncodedStateBytes = 72;
constexpr std::size_t MaximumEncodedStateBytes = ((MaximumSurfaceWorldStateBytes + 63) / 64) * 64 + 8;

bool IsRegionalId(RegionId region)
{
	return !region.IsTown() && region.x >= -MaximumRegionCoordinate && region.x <= MaximumRegionCoordinate
	    && region.z >= -MaximumRegionCoordinate && region.z <= MaximumRegionCoordinate;
}

bool ValidateVisited(std::span<const RegionId> visited)
{
	if (visited.size() > MaximumPersistentVisitedRegions)
		return false;
	std::bitset<RegionCount> seen;
	for (RegionId region : visited) {
		if (!IsRegionalId(region))
			return false;
		const auto index = static_cast<std::size_t>(region.x + MaximumRegionCoordinate) * RegionSide
		    + static_cast<std::size_t>(region.z + MaximumRegionCoordinate);
		if (seen.test(index))
			return false;
		seen.set(index);
	}
	return true;
}

bool RemoveOwnedNames(SaveWriter &archive, bool includePermanent)
{
	if (!archive.FreeFileSlots().has_value())
		return false;
	// The bounded 193x193 domain also removes temps from exploration made AFTER
	// the last full save. Do not derive cleanup from permanent visited metadata.
	for (std::int32_t x = -MaximumRegionCoordinate; x <= MaximumRegionCoordinate; ++x) {
		for (std::int32_t z = -MaximumRegionCoordinate; z <= MaximumRegionCoordinate; ++z) {
			const RegionId region { x, z };
			if (region.IsTown())
				continue;
			const auto temp = SurfaceRegionArchiveName(region, true);
			if (!archive.RemoveHashEntryChecked(temp->c_str()))
				return false;
			if (includePermanent) {
				const auto permanent = SurfaceRegionArchiveName(region, false);
				if (!archive.RemoveHashEntryChecked(permanent->c_str()))
					return false;
			}
		}
	}
	return !includePermanent || (archive.RemoveHashEntryChecked(SurfaceWorldArchiveName)
	    && archive.RemoveHashEntryChecked(TownVisualArchiveName));
}

} // namespace

std::expected<std::optional<PersistentState>, std::string> ReadSurfaceWorldState(SaveReader &archive)
{
	if (!archive.HasFile(SurfaceWorldArchiveName))
		return std::optional<PersistentState> {};
	const auto encodedSize = GetArchiveEncodedFileSize(archive, SurfaceWorldArchiveName);
	if (!encodedSize.has_value() || *encodedSize < MinimumEncodedStateBytes
	    || *encodedSize > MaximumEncodedStateBytes || (*encodedSize - 8) % 64 != 0)
		return std::unexpected("Invalid surface-world archive record size");
	std::size_t decodedSize = 0;
	auto decoded = ReadArchive(archive, SurfaceWorldArchiveName, &decodedSize);
	if (decoded == nullptr)
		return std::unexpected("Unable to decode native surface-world archive record");
	// Native decode reports a length from its trailing signature. Never let a
	// malformed signature expose bytes beyond the allocated encoded buffer.
	if (decodedSize < SurfaceWorldStateHeaderBytes || decodedSize > *encodedSize - 8
	    || decodedSize > MaximumSurfaceWorldStateBytes)
		return std::unexpected("Invalid decoded surface-world archive record size");
	const auto bytes = std::span<const std::byte>(decoded.get(), decodedSize);
	const auto stateBytes = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
	auto result = DecodeSurfaceWorldState(stateBytes);
	if (!result.state.has_value())
		return std::unexpected("Invalid surface-world state, codec error " + std::to_string(static_cast<unsigned>(result.error)));
	return std::optional<PersistentState>(std::move(*result.state));
}

bool WriteSurfaceWorldState(SaveWriter &archive, const PersistentState &state)
{
	// Validate/allocate/encode entirely before touching any native archive entry.
	const auto encodedState = EncodeSurfaceWorldState(state);
	if (!encodedState.has_value()) {
		archive.MarkWriteError();
		return false;
	}
	return WriteArchiveEncoded(archive, SurfaceWorldArchiveName, std::as_bytes(std::span(*encodedState)));
}

std::expected<std::shared_ptr<const TownVisualMap>, std::string> ReadSurfaceTownVisualMap(SaveReader &archive)
{
	if (!archive.HasFile(TownVisualArchiveName))
		return std::shared_ptr<const TownVisualMap> {};
	constexpr size_t EncodedBytes = ((TownVisualRecordBytes + 63) / 64) * 64 + 8;
	const auto encodedSize = GetArchiveEncodedFileSize(archive, TownVisualArchiveName);
	if (!encodedSize || *encodedSize != EncodedBytes)
		return std::unexpected("Invalid Town visual companion archive size");
	try {
		size_t decodedSize = 0;
		auto decoded = ReadArchive(archive, TownVisualArchiveName, &decodedSize);
		if (!decoded || decodedSize != TownVisualRecordBytes)
			return std::unexpected("Invalid decoded Town visual companion size");
		auto result = DecodeTownVisualMap(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(decoded.get()), decodedSize));
		if (!result.map)
			return std::unexpected("Invalid Town visual companion, codec error " + std::to_string(static_cast<unsigned>(result.error)));
		return std::make_shared<const TownVisualMap>(std::move(*result.map));
	} catch (const std::bad_alloc &) {
		return std::unexpected("Unable to allocate Town visual companion");
	}
}
bool WriteSurfaceTownVisualMap(SaveWriter &archive, const TownVisualMap &map)
{
	const auto bytes = EncodeTownVisualMap(map);
	if (!bytes) { archive.MarkWriteError(); return false; }
	return WriteArchiveEncoded(archive, TownVisualArchiveName, std::as_bytes(std::span(*bytes)));
}

std::optional<std::string> SurfaceRegionArchiveName(RegionId region, bool temporary)
{
	if (!IsRegionalId(region))
		return std::nullopt;
	char name[11];
	const int length = std::snprintf(name, sizeof(name), "%s%03u%03u", temporary ? "srt1" : "srp1",
	    static_cast<unsigned>(region.x + MaximumRegionCoordinate), static_cast<unsigned>(region.z + MaximumRegionCoordinate));
	if (length != 10)
		return std::nullopt;
	return std::string(name);
}

bool HasSurfaceRegionLevel(SaveReader &archive, RegionId region, bool temporary)
{
	const auto name = SurfaceRegionArchiveName(region, temporary);
	return name.has_value() && archive.HasFile(name->c_str());
}

bool HasSurfaceRegionLevel(SaveWriter &archive, RegionId region, bool temporary)
{
	const auto name = SurfaceRegionArchiveName(region, temporary);
	return name.has_value() && archive.HasFile(name->c_str());
}

std::expected<void, std::string> PreflightSurfaceWorldWrites(SaveWriter &archive, std::size_t plannedWrites)
{
	const auto freeSlots = archive.FreeFileSlots();
	if (archive.HasWriteError() || !freeSlots.has_value())
		return std::unexpected("Surface-world archive writer is unavailable or carry-forward failed");
	if (plannedWrites > *freeSlots)
		return std::unexpected("Insufficient MPQ append slots for surface-world save");
	return {};
}

std::expected<void, std::string> PreflightSurfaceRegionSave(SaveWriter &archive, RegionId region, std::size_t nativeWriteReserve)
{
	if (!IsRegionalId(region))
		return std::unexpected("Invalid regional archive identity (Town uses native level storage)");
	if (nativeWriteReserve > std::numeric_limits<std::size_t>::max() - 2)
		return std::unexpected("Surface-world archive write budget overflow");
	return PreflightSurfaceWorldWrites(archive, nativeWriteReserve + 2);
}

bool PromoteSurfaceRegionTempFiles(SaveWriter &archive, std::span<const RegionId> visited)
{
	if (!ValidateVisited(visited) || !archive.FreeFileSlots().has_value())
		return false;
	for (RegionId region : visited) {
		const auto temp = SurfaceRegionArchiveName(region, true);
		if (!archive.HasFile(temp->c_str()))
			continue;
		const auto permanent = SurfaceRegionArchiveName(region, false);
		if (!archive.RemoveHashEntryChecked(permanent->c_str())
		    || !archive.RenameFileChecked(temp->c_str(), permanent->c_str()))
			return false;
	}
	return true;
}

bool HasSurfaceRegionStorage(SaveWriter &archive)
{
	if (archive.HasFile(SurfaceWorldArchiveName) || archive.HasFile(TownVisualArchiveName))
		return true;
	for (std::int32_t x = -MaximumRegionCoordinate; x <= MaximumRegionCoordinate; ++x) {
		for (std::int32_t z = -MaximumRegionCoordinate; z <= MaximumRegionCoordinate; ++z) {
			const RegionId region { x, z };
			if (!region.IsTown()
			    && (HasSurfaceRegionLevel(archive, region, true) || HasSurfaceRegionLevel(archive, region, false)))
				return true;
		}
	}
	return false;
}

bool RemoveSurfaceRegionPermanentFiles(SaveWriter &archive)
{
	if (!archive.FreeFileSlots().has_value()) {
		archive.MarkWriteError();
		return false;
	}
	for (std::int32_t x = -MaximumRegionCoordinate; x <= MaximumRegionCoordinate; ++x) {
		for (std::int32_t z = -MaximumRegionCoordinate; z <= MaximumRegionCoordinate; ++z) {
			const RegionId region { x, z };
			if (region.IsTown())
				continue;
			const auto permanent = SurfaceRegionArchiveName(region, false);
			if (!archive.RemoveHashEntryChecked(permanent->c_str()))
				return false;
		}
	}
	return true;
}

bool RemoveSurfaceRegionTempFiles(SaveWriter &archive)
{
	return RemoveOwnedNames(archive, false);
}

bool RemoveSurfaceRegionNamespace(SaveWriter &archive, bool includePermanent)
{
	return RemoveOwnedNames(archive, includePermanent);
}

} // namespace devilution::surface
