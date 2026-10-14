#include "cathedral_native_capture.hpp"

#include <stdexcept>

#include "engine/random.hpp"
#include "game_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/trigs.h"
#include "multi.h"
#include "objects.h"
#include "player.h"

namespace devilution::cathedral {

Snapshot CaptureSnapshot(CaptureMetadata metadata)
{
	static_assert(MegaSize == DMAXX && MegaSize == DMAXY);
	static_assert(GridSize == MAXDUNX && GridSize == MAXDUNY);
	if (metadata.gameRevision == 0)
		throw std::invalid_argument("Native snapshot requires a nonzero level epoch");
	if (leveltype != DTYPE_CATHEDRAL || setlevel)
		throw std::invalid_argument("Native snapshot requires regular Cathedral context");
	if (dminPosition.x != ActiveMin || dminPosition.y != ActiveMin
	    || dmaxPosition.x != ActiveMax || dmaxPosition.y != ActiveMax)
		throw std::invalid_argument("Native Cathedral active bounds outside pilot contract");
	if (ActiveObjectCount < 0 || ActiveObjectCount > MAXOBJECTS)
		throw std::invalid_argument("Native active object count");
	if (numtrigs < 0 || numtrigs > MAXTRIGGERS)
		throw std::invalid_argument("Native trigger count");
	Snapshot result;
	result.seed = metadata.seed;
	result.rngState = GetLCGEngineState();
	result.gameRevision = metadata.gameRevision;
	result.level = currlevel;
	result.entry = metadata.entry;
	result.originalCathedral = MyPlayer != nullptr && MyPlayer->pOriginalCathedral;
	result.fullQuests = sgGameInitInfo.fullQuests != 0;
	result.hellfire = gbIsHellfire;
	result.levelType = static_cast<int>(leveltype);
	result.setLevel = setlevel;
	// setlevel is the active discriminant; the engine preserves the raw enum
	// after quest returns/saves. Normalize only this inactive DTO field.
	result.setLevelId = SL_NONE;
	result.minX = dminPosition.x;
	result.minZ = dminPosition.y;
	result.maxX = dmaxPosition.x;
	result.maxZ = dmaxPosition.y;
	for (int z = 0; z < MegaSize; ++z)
		for (int x = 0; x < MegaSize; ++x)
			result.mega[z * MegaSize + x] = dungeon[x][z];
	for (int z = 0; z < GridSize; ++z) {
		for (int x = 0; x < GridSize; ++x) {
			Cell &cell = result.At(x, z);
			cell.piece = dPiece[x][z];
			// Native dPiece is already zero based; SOL corrections have been applied
			// by LoadLevelSOLData. Preserve every property bit, including Trap.
			if (cell.piece >= MAXTILES)
				throw std::invalid_argument("Native dungeon piece outside SOL table");
			cell.properties = static_cast<uint8_t>(SOLData[cell.piece]);
			cell.transparency = static_cast<uint8_t>(dTransVal[x][z]);
			cell.special = dSpecial[x][z];
			cell.light = dLight[x][z];
			cell.flags = static_cast<uint8_t>(dFlags[x][z]);
			cell.object = dObject[x][z];
		}
	}
	result.micros.resize(MAXTILES);
	for (int piece = 0; piece < MAXTILES; ++piece)
		for (int micro = 0; micro < 16; ++micro)
			result.micros[piece][micro] = DPieceMicros[piece].mt[micro].data;
	result.activeTransparency = TransList;
	for (int i = 0; i < ActiveObjectCount; ++i) {
		const int slot = ActiveObjects[i];
		if (slot < 0 || slot >= MAXOBJECTS)
			throw std::invalid_argument("Native active object slot");
		const Object &object = Objects[slot];
		if (object._otype != OBJ_L1LDOOR && object._otype != OBJ_L1RDOOR)
			continue;
		if (object._oVar4 < 0 || object._oVar4 > 2)
			throw std::invalid_argument("Native Cathedral door state");
		if (!InDungeonBounds(object.position))
			throw std::invalid_argument("Native Cathedral door position");
		Door door;
		door.slot = slot;
		door.x = object.position.x;
		door.z = object.position.y;
		door.orientation = object._otype == OBJ_L1LDOOR ? DoorOrientation::Left : DoorOrientation::Right;
		door.state = static_cast<DoorState>(object._oVar4); // Native DOOR_CLOSED/OPEN/BLOCKED = 0/1/2.
		door.selectable = object.selectionRegion != SelectionRegion::None;
		door.animationFrame = static_cast<int>(object._oAnimFrame);
		door.originalPiece = object._oVar1; // Native stored value is 1-based.
		door.originalNeighborPiece = object._oVar2; // Native stored value is 1-based.
		result.doors.push_back(door);
	}
	for (int i = 0; i < numtrigs; ++i)
		result.triggers.push_back({ trigs[i].position.x, trigs[i].position.y, static_cast<int>(trigs[i]._tmsg), trigs[i]._tlvl });
	return result;
}

} // namespace devilution::cathedral
