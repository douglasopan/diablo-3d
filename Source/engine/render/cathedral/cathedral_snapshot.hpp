#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace devilution::cathedral {

constexpr int MegaSize = 40;
constexpr int GridSize = 112;
constexpr int ActiveMin = 16;
constexpr int ActiveMax = 96; // Exclusive. Bounds do not claim every cell is walkable.
constexpr int RegionSize = 8;
constexpr uint8_t Solid = 1;

struct Cell {
	uint16_t piece = 0; // Zero-based native dPiece, unlike the 1-based mega ID.
	uint8_t properties = 0; // Complete native SOL byte; never inferred from geometry.
	uint8_t transparency = 0; // Preserve signed dTransVal's byte representation.
	int8_t special = 0;
	uint8_t light = 0;
	uint8_t flags = 0;
	int8_t object = 0; // Native signed 1-based occupancy, not a picking ID.
	friend bool operator==(const Cell &, const Cell &) = default;
};

enum class DoorOrientation : uint8_t { Left, Right };
enum class DoorState : uint8_t { Closed, Open, Blocked };

struct Door {
	int slot = -1;
	int x = 0;
	int z = 0;
	DoorOrientation orientation = DoorOrientation::Left;
	DoorState state = DoorState::Closed;
	bool selectable = false;
	int animationFrame = 0;
	int originalPiece = 0; // Native _oVar1 verbatim: saved 1-based piece.
	int originalNeighborPiece = 0; // Native _oVar2 verbatim: saved 1-based piece.
	friend bool operator==(const Door &, const Door &) = default;
};

struct Trigger {
	int x = 0;
	int z = 0;
	int message = 0;
	int targetLevel = 0;
	friend bool operator==(const Trigger &, const Trigger &) = default;
};

/** Frozen at the simulation/render boundary. Capture never generates a map.
 * Arrays use explicit row-major indexing (z*width+x), unlike native [x][y].
 * gameRevision scopes object slots to one loaded level; seed is provenance only.
 */
struct Snapshot {
	uint32_t seed = 0;
	uint32_t rngState = 0;
	uint64_t gameRevision = 0;
	uint8_t level = 1;
	uint8_t entry = 0;
	bool originalCathedral = true;
	bool fullQuests = true;
	bool hellfire = false;
	int levelType = 1; // Native DTYPE_CATHEDRAL, kept as a scalar in this pure contract.
	bool setLevel = false;
	int setLevelId = 0; // Native SL_NONE is required by this pilot.
	int minX = ActiveMin, minZ = ActiveMin, maxX = ActiveMax, maxZ = ActiveMax;
	std::array<uint8_t, MegaSize * MegaSize> mega {};
	std::array<Cell, GridSize * GridSize> cells {};
	std::vector<std::array<uint16_t, 16>> micros;
	std::array<bool, 256> activeTransparency {};
	std::vector<Door> doors;
	std::vector<Trigger> triggers;

	const Cell &At(int x, int z) const {
		if (x < 0 || z < 0 || x >= GridSize || z >= GridSize) throw std::out_of_range("Native snapshot coordinate");
		return cells[static_cast<std::size_t>(z) * GridSize + x];
	}
	Cell &At(int x, int z) {
		if (x < 0 || z < 0 || x >= GridSize || z >= GridSize) throw std::out_of_range("Native snapshot coordinate");
		return cells[static_cast<std::size_t>(z) * GridSize + x];
	}
};

} // namespace devilution::cathedral
