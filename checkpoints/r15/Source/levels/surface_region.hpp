#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace devilution::surface {

// Storage includes two ghost/guard cells on every side. Only [2,109] is
// walkable. Neighbor offsets use108 so interiors meet without a world gap.
inline constexpr int StorageSize = 112;
inline constexpr int GuardSize = 2;
inline constexpr int InteriorSize = StorageSize - 2 * GuardSize;
inline constexpr int InteriorMin = GuardSize;
inline constexpr int InteriorMax = StorageSize - GuardSize - 1;
inline constexpr int FunctionalCenter = 55;
inline constexpr double CellMetres = 1.458;
inline constexpr double RegionalOrigin = 56.0;
inline constexpr uint32_t VisualSeed = 0x54525331;
inline constexpr uint32_t GeneratorRevision = 2;
// H/TRS1 field is unchanged; revision2 adds authored traversal semantics only.
inline constexpr uint32_t HeightRevision = 1;
// Technical admission bound, not a claim that all these regions have art.
inline constexpr int MaximumRegionCoordinate = 96;

struct Tile {
	int x = 0;
	int z = 0;
	constexpr bool operator==(const Tile &) const = default;
};

// A distinct identity namespace: this is never a native dungeon/setlevel ID.
// (0,0) means the preserved native Town and must never be regenerated.
struct RegionId {
	int32_t x = 0;
	int32_t z = 0;
	constexpr bool IsTown() const { return x == 0 && z == 0; }
	constexpr bool operator==(const RegionId &) const = default;
};

enum class Side : uint8_t { North, East, South, West };

struct ExitSlot {
	Side side;
	std::array<Tile, 2> triggers;
	Tile arrival;
};

// These are actor tile labels, not physically shifted cell centers. A pair
//55/56 has physical midpoint55 under cell[i-1,i]. All guards stay untouched.
inline constexpr std::array<ExitSlot, 4> ExitSlots { {
	{ Side::North, { Tile { 55, 2 }, Tile { 56, 2 } }, { 55, 3 } },
	{ Side::East, { Tile { 109, 55 }, Tile { 109, 56 } }, { 108, 55 } },
	{ Side::South, { Tile { 55, 109 }, Tile { 56, 109 } }, { 55, 108 } },
	{ Side::West, { Tile { 2, 55 }, Tile { 2, 56 } }, { 3, 55 } },
} };

constexpr bool ValidSide(Side side) { return static_cast<unsigned>(side) < ExitSlots.size(); }
constexpr bool ValidRegionId(RegionId id)
{
	return id.x >= -MaximumRegionCoordinate && id.x <= MaximumRegionCoordinate
	    && id.z >= -MaximumRegionCoordinate && id.z <= MaximumRegionCoordinate;
}
constexpr bool InInterior(Tile tile)
{
	return tile.x >= InteriorMin && tile.x <= InteriorMax && tile.z >= InteriorMin && tile.z <= InteriorMax;
}
// Precondition: ValidRegionId(id), checked by generation/transition entrypoints.
constexpr Tile WorldOffset(RegionId id) { return { id.x * InteriorSize, id.z * InteriorSize }; }
constexpr Side Opposite(Side side) { return static_cast<Side>((static_cast<unsigned>(side) + 2U) % 4U); }

// Invalid directions/range fail closed. Returning(0,0) is a valid Town return.
std::optional<RegionId> StepRegion(RegionId from, Side side);
const ExitSlot *FindExit(Side side);
std::optional<Side> ExitAt(Tile tile);

struct Transition {
	RegionId source;
	RegionId destination;
	Side sourceSide;
	Side destinationSide;
	Tile destinationArrival;
};
std::optional<Transition> TransitionFrom(RegionId from, Side side);

// TRS1 binary64 field, metres. No terrain result implicitly changes SOL.
// World coordinates are native x,z; TRS1(u,v)=(world-(56,56))*1.458.
double HeightMetres(double worldNativeX, double worldNativeZ);
double ForestDensity(double worldNativeX, double worldNativeZ);
uint32_t CellVariation(int worldNativeX, int worldNativeZ);
// Representation samples may use this through the ghost overlap. Blocked
// guard cells are a traversal rule, not a physical wall or a terrain cut.
bool IsRoadWorldCell(int worldNativeX, int worldNativeZ);

enum class CellKind : uint8_t { Blocked, Grass, Road };
struct Cell {
	CellKind kind = CellKind::Blocked;
	uint8_t variation = 0;
	float heightMetres = 0;
};
struct Region {
	RegionId id;
	Tile worldOffset;
	std::array<Cell, StorageSize * StorageSize> cells {};
	const Cell &At(int x, int z) const { return cells[static_cast<std::size_t>(z) * StorageSize + x]; }
};

// Pure, bounded, deterministic output; no native globals, RNG, assets or actors.
// Town/invalid IDs return nullopt. R2 uses authored world obstacle semantics;
// blocked roads retain Road material. Exterior guards remain blocked. No RNG
// obstacles, new H, slope-to-SOL policy or automatic native collision admission.
std::optional<Region> GenerateRegion(RegionId id);

} // namespace devilution::surface
