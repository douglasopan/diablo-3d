#include "levels/surface_region.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>

#include "mods/mod_identity.h"
#include "levels/surface_region_obstacles.hpp"

namespace devilution::surface {
namespace {

constexpr double ProtectedHalfMetres = 84.564;
struct Lobe { double amplitude, x, z, sigmaX, sigmaZ; };
constexpr std::array<Lobe, 3> Lobes { {
	{ 780.0, -4800.0, -6100.0, 1450.0, 1700.0 },
	{ 550.0, -700.0, -7200.0, 1700.0, 1300.0 },
	{ 900.0, 6100.0, -6600.0, 1500.0, 1600.0 },
} };

double ProtectedDistance(double u, double v)
{
	return std::hypot(std::max(std::abs(u) - ProtectedHalfMetres, 0.0), std::max(std::abs(v) - ProtectedHalfMetres, 0.0));
}

double RegionalHeight(double u, double v)
{
	const double distance = ProtectedDistance(u, v);
	if (distance <= 0.0)
		return 0.0;
	double relief = 6.0 + 5.0 * std::sin(u / 340.0 + 0.37) * std::sin(v / 470.0 + 1.13)
	    + 3.0 * std::sin(u / 910.0 - v / 760.0 + 2.21)
	    + 1.5 * std::sin(u / 71.0 + v / 83.0 + 0.71);
	const double detail = 0.86 + 0.09 * std::sin(u / 210.0 + v / 317.0)
	    + 0.05 * std::sin(u / 83.0 - v / 157.0);
	for (const auto &lobe : Lobes) {
		const double dx = (u - lobe.x) / lobe.sigmaX;
		const double dz = (v - lobe.z) / lobe.sigmaZ;
		relief += lobe.amplitude * std::exp(-0.5 * dx * dx - 0.5 * dz * dz) * detail;
	}
	const double t = std::clamp(distance / 180.0, 0.0, 1.0);
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0) * relief;
}

} // namespace

std::optional<RegionId> StepRegion(RegionId from, Side side)
{
	if (!ValidRegionId(from) || !ValidSide(side))
		return std::nullopt;
	switch (side) {
	case Side::North: --from.z; break;
	case Side::East: ++from.x; break;
	case Side::South: ++from.z; break;
	case Side::West: --from.x; break;
	}
	return ValidRegionId(from) ? std::optional<RegionId>(from) : std::nullopt;
}

const ExitSlot *FindExit(Side side)
{
	return ValidSide(side) ? &ExitSlots[static_cast<unsigned>(side)] : nullptr;
}

std::optional<Side> ExitAt(Tile tile)
{
	for (const auto &slot : ExitSlots)
		if (tile == slot.triggers[0] || tile == slot.triggers[1])
			return slot.side;
	return std::nullopt;
}

std::optional<Transition> TransitionFrom(RegionId from, Side side)
{
	const auto destination = StepRegion(from, side);
	if (!destination)
		return std::nullopt;
	const Side entry = Opposite(side);
	return Transition { from, *destination, side, entry, FindExit(entry)->arrival };
}

double HeightMetres(double worldNativeX, double worldNativeZ)
{
	if (!std::isfinite(worldNativeX) || !std::isfinite(worldNativeZ))
		return 0.0;
	return RegionalHeight((worldNativeX - RegionalOrigin) * CellMetres, (worldNativeZ - RegionalOrigin) * CellMetres);
}

double ForestDensity(double worldNativeX, double worldNativeZ)
{
	if (!std::isfinite(worldNativeX) || !std::isfinite(worldNativeZ))
		return 0.0;
	const double u = (worldNativeX - RegionalOrigin) * CellMetres;
	const double v = (worldNativeZ - RegionalOrigin) * CellMetres;
	if (ProtectedDistance(u, v) <= 0.0 || RegionalHeight(u, v) >= 410.0)
		return 0.0;
	const double signal = std::sin(u / 238.0 + 0.3) * std::sin(v / 297.0 + 1.4)
	    + 0.5 * std::sin(u / 91.0 + v / 173.0);
	return std::clamp((signal + 0.2) / 1.7, 0.0, 1.0);
}

uint32_t CellVariation(int worldNativeX, int worldNativeZ)
{
	// Exact TRS1 hashing convention: SHA-256 of ASCII/UTF-8, first uint32LE.
	const std::string payload = std::to_string(VisualSeed) + "|surface-grass-variant|"
	    + std::to_string(worldNativeX) + "|" + std::to_string(worldNativeZ) + "|0";
	const auto bytes = std::as_bytes(std::span(payload.data(), payload.size()));
	const auto digest = ComputeBytesSha256(bytes);
	return static_cast<uint32_t>(digest[0]) | static_cast<uint32_t>(digest[1]) << 8U
	    | static_cast<uint32_t>(digest[2]) << 16U | static_cast<uint32_t>(digest[3]) << 24U;
}

bool IsRoadWorldCell(int worldNativeX, int worldNativeZ)
{
	auto roadBand = [](int coordinate) {
		// floor-mod, including signed western/northern world coordinates.
		const int remainder = (coordinate % InteriorSize + InteriorSize) % InteriorSize;
		return remainder == 55 || remainder == 56;
	};
	return roadBand(worldNativeX) || roadBand(worldNativeZ);
}

std::optional<Region> GenerateRegion(RegionId id)
{
	if (!ValidRegionId(id) || id.IsTown())
		return std::nullopt;
	Region result;
	result.id = id;
	result.worldOffset = WorldOffset(id);
	for (int z = 0; z < StorageSize; ++z) {
		for (int x = 0; x < StorageSize; ++x) {
			auto &cell = result.cells[static_cast<std::size_t>(z) * StorageSize + x];
			const int worldX = result.worldOffset.x + x;
			const int worldZ = result.worldOffset.z + z;
			cell.heightMetres = static_cast<float>(HeightMetres(worldX - 0.5, worldZ - 0.5));
			cell.variation = static_cast<uint8_t>(CellVariation(worldX, worldZ) & 15U);
			if (!InInterior({ x, z }))
				continue;
			const auto semantics = GetWorldCellSemantics(worldX, worldZ);
			if (!semantics)
				return std::nullopt;
			cell.kind = semantics->kind;
		}
	}
	return result;
}

} // namespace devilution::surface
