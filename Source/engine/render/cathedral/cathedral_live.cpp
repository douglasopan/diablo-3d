#include "cathedral_live.hpp"

#include <array>
#include <limits>
#include <memory>
#include <type_traits>

#include "dead.h"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "game_mode.hpp"
#include "items.h"
#include "levels/dun_tile_data.hpp"
#include "levels/trigs.h"
#include "lighting.h"
#include "missiles.h"
#include "monster.h"
#include "multi.h"
#include "native/cathedral_native_capture.hpp"
#include "nthread.h"
#include "objects.h"
#include "player.h"
#include "utils/palette_blending.hpp"

namespace devilution::cathedral {
namespace {

struct LiveState {
	uint64_t epoch = 0;
	uint64_t frameRevision = 0;
	uint64_t fingerprint = 0;
	uint32_t seed = 0;
	uint8_t entry = 0;
	int focusX = 0, focusZ = 0;
	Adapter adapter;
	std::unique_ptr<const Snapshot> snapshot;
	std::unique_ptr<const Scene> scene;
};

std::unique_ptr<LiveState> State;
uint64_t EpochCounter = 0;
uint64_t FrameCounter = 0;

bool NativeEligible() noexcept
{
	return MyPlayer != nullptr && currlevel == 1 && leveltype == DTYPE_CATHEDRAL
	    && !gbIsSpawn && !gbIsHellfire && !setlevel
	    && dminPosition.x == ActiveMin && dminPosition.y == ActiveMin
	    && dmaxPosition.x == ActiveMax && dmaxPosition.y == ActiveMax;
}

struct Hash {
	uint64_t value = 14695981039346656037ULL;
	bool valid = true;
	void Bytes(const void *data, size_t size) noexcept
	{
		const auto *bytes = static_cast<const unsigned char *>(data);
		for (size_t i = 0; i < size; ++i) {
			value ^= bytes[i];
			value *= 1099511628211ULL;
		}
	}
	template <typename T> void Add(const T &value) noexcept
	{
		// Aggregate object representation can contain padding. Hash named
		// semantic fields below; only scalar representations reach Bytes here.
		static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>);
		Bytes(&value, sizeof(value));
	}
	template <typename T, size_t Size> void Add(const T (&values)[Size]) noexcept
	{
		for (const T &value : values) Add(value);
	}
	template <typename T, size_t Size> void Add(const std::array<T, Size> &values) noexcept
	{
		for (const T &value : values) Add(value);
	}
	template <typename T> void Add(const PointOf<T> &position) noexcept { Add(position.x); Add(position.y); }
	template <typename T> void Add(const DisplacementOf<T> &offset) noexcept { Add(offset.deltaX); Add(offset.deltaY); }
	void Add(const ActorPosition &position) noexcept
	{
		Add(position.tile); Add(position.future); Add(position.last); Add(position.old); Add(position.temp);
	}
	void Add(const MissilePosition &position) noexcept
	{
		Add(position.offset); Add(position.velocity); Add(position.traveled);
		Add(position.tile); Add(position.start); Add(position.tileForRendering); Add(position.offsetForRendering);
	}
	void Add(const MICROS &micros) noexcept
	{
		for (const LevelCelBlock &block : micros.mt) Add(block.data);
	}
	void Add(ClxSprite sprite) noexcept
	{
		Add(sprite.pixelData()); Add(sprite.width()); Add(sprite.height()); Add(sprite.pixelDataSize());
		// Raw CLX controls encode coverage separately from covered palette0.
		Bytes(sprite.pixelData(), sprite.pixelDataSize());
	}
	void Add(const OptionalClxSprite &sprite) noexcept
	{
		Add(sprite.has_value());
		if (sprite) Add(*sprite);
	}
	void Add(const OptionalClxSpriteList &sprites) noexcept
	{
		Add(sprites.has_value());
		if (sprites) { Add(sprites->data()); Add(sprites->numSprites()); Add(sprites->dataSize()); }
	}
	void Add(const OptionalClxSpriteListOrSheet &sprites) noexcept
	{
		Add(sprites.has_value());
		if (!sprites) return;
		Add(sprites->isSheet());
		if (sprites->isSheet()) {
			const ClxSpriteSheet sheet = sprites->sheet();
			Add(sheet.data()); Add(sheet.numLists()); Add(sheet.dataSize());
		} else {
			const ClxSpriteList list = sprites->list();
			Add(list.data()); Add(list.numSprites()); Add(list.dataSize());
		}
	}
	void Add(const AnimationInfo &animation) noexcept
	{
		Add(animation.sprites); Add(animation.ticksPerFrame); Add(animation.tickCounterOfCurrentFrame);
		Add(animation.numberOfFrames); Add(animation.currentFrame); Add(animation.isPetrified);
		if (!animation.sprites) return;
		if (animation.numberOfFrames <= 0 || animation.ticksPerFrame <= 0
		    || animation.numberOfFrames > static_cast<int>(animation.sprites->numSprites())) { valid = false; return; }
		const int frame = animation.getFrameToUseForRendering();
		Add(frame); Add(animation.getAnimationProgress());
		if (frame < 0 || frame >= animation.numberOfFrames) { valid = false; return; }
		Add((*animation.sprites)[frame]);
	}
	void Translation(const uint8_t *translation) noexcept
	{
		Add(translation);
		if (translation != nullptr) Bytes(translation, 256);
	}
};

// This deliberately includes occupancy beyond the pure geometry Snapshot:
// changes to a player/item/monster/missile can invalidate a screen binding even
// when every wall remains identical. Only records used by live native drawing
// or selection are inspected. Loaded palette/TRN/CLX bytes are read in memory;
// no resource loader or archive IO is invoked and no native data is written.
bool NativeFingerprint(uint64_t &fingerprint) noexcept
{
	if (!LiveLevelEligible() || ActiveObjectCount < 0 || ActiveObjectCount > MAXOBJECTS
	    || ActiveItemCount > MAXITEMS || ActiveMonsterCount > MaxMonsters
	    || numtrigs < 0 || numtrigs > MAXTRIGGERS)
		return false;
	Hash h;
	h.Add(State->epoch); h.Add(State->seed); h.Add(State->entry);
	h.Add(currlevel); h.Add(leveltype); h.Add(setlevel); h.Add(setlvlnum);
	h.Add(gbIsHellfire); h.Add(MyPlayer->pOriginalCathedral); h.Add(sgGameInitInfo.fullQuests);
	h.Add(dminPosition); h.Add(dmaxPosition); h.Add(GetLCGEngineState());
	h.Add(dungeon); h.Add(dPiece); h.Add(SOLData); h.Add(DPieceMicros);
	h.Add(dTransVal); h.Add(dSpecial); h.Add(dLight); h.Add(dFlags); h.Add(dObject); h.Add(TransList);
	h.Add(dPlayer); h.Add(dMonster); h.Add(dItem); h.Add(dCorpse);
	h.Add(LightTables); h.Add(InfravisionTable); h.Add(StoneTable);
	for (const SDL_Color &color : logical_palette) {
		h.Add(color.r); h.Add(color.g); h.Add(color.b); h.Add(color.a);
	}
	h.Add(paletteTransparencyLookup);
	h.Add(static_cast<bool>(pSpecialCels));
	if (pSpecialCels) {
		// Special frames can be reused at the same address after a native load.
		// Hash controls, dimensions and pixels, including coverage of color0.
		h.Add(pSpecialCels->numSprites());
		for (const ClxSprite sprite : ClxSpriteList { *pSpecialCels }) h.Add(sprite);
	}
	h.Add(numtrigs);
	for (int i = 0; i < numtrigs; ++i) {
		h.Add(trigs[i].position); h.Add(trigs[i]._tmsg); h.Add(trigs[i]._tlvl);
	}
	h.Add(ActiveObjectCount);
	std::array<bool, MAXOBJECTS> seenObjects {};
	for (int i = 0; i < ActiveObjectCount; ++i) {
		const int slot = ActiveObjects[i];
		if (slot < 0 || slot >= MAXOBJECTS || seenObjects[slot]) return false;
		seenObjects[slot] = true;
		const Object &o = Objects[slot];
		h.Add(slot); h.Add(o._otype); h.Add(o.position); h.Add(o.applyLighting);
		h.Add(o._oAnimData); h.Add(o._oAnimFlag); h.Add(o._oAnimDelay); h.Add(o._oAnimCnt);
		h.Add(o._oAnimLen); h.Add(o._oAnimFrame); h.Add(o._oAnimWidth);
		h.Add(o._oDelFlag); h.Add(o._oBreak); h.Add(o._oSolidFlag); h.Add(o._oMissFlag);
		h.Add(o.selectionRegion); h.Add(o._oPreFlag); h.Add(o._oDoorFlag); h.Add(o._oTrapFlag);
		h.Add(o._olid); h.Add(o._oRndSeed); h.Add(o._oVar1); h.Add(o._oVar2); h.Add(o._oVar3);
		h.Add(o._oVar4); h.Add(o._oVar5); h.Add(o._oVar6); h.Add(o._oVar8); h.Add(o.bookMessage);
		if (o._oAnimData && o._oAnimFrame > 0 && o._oAnimFrame <= o._oAnimData->numSprites())
			h.Add(o.currentSprite());
	}
	h.Add(ActiveItemCount);
	std::array<bool, MAXITEMS> seenItems {};
	for (int i = 0; i < ActiveItemCount; ++i) {
		const int slot = ActiveItems[i];
		if (slot < 0 || slot >= MAXITEMS || seenItems[slot]) return false;
		seenItems[slot] = true;
		const Item &item = Items[slot];
		h.Add(slot); h.Add(item.position); h.Add(item._iSeed); h.Add(item._iCreateInfo); h.Add(item._itype);
		h.Add(item.AnimInfo); h.Add(item._iAnimFlag); h.Add(item._iDelFlag); h.Add(item.selectionRegion);
		h.Add(item._iPostDraw); h.Add(item._iIdentified); h.Add(item._iCurs); h.Add(item.IDidx);
	}
	h.Add(Players.size()); h.Add(MyPlayerId); h.Add(MyPlayer); h.Add(MyPlayerIsDead);
	for (size_t slot = 0; slot < Players.size(); ++slot) {
		const Player &p = Players[slot];
		h.Add(slot); h.Add(p.plractive); h.Add(p.plrlevel); h.Add(p.plrIsOnSetLevel); h.Add(p._pLvlChanging);
		// Inactive slots cannot bind an actor, but their activation is itself hashed.
		if (!p.plractive && &p != MyPlayer) continue;
		h.Add(p.position); h.Add(p.AnimInfo); h.Add(p.previewCelSprite);
		h.Add(p.progressToNextGameTickWhenPreviewWasSet); h.Add(p._pmode); h.Add(p._pdir);
		h.Add(p._pClass); h.Add(p._pgfxnum); h.Add(p._pHitPoints); h.Add(p._pInfraFlag);
		h.Add(p._pIFlags); h.Add(p.pManaShield); h.Add(p.lightId); h.Add(p._pName);
		h.Add(p.destAction); h.Add(p.destParam1); h.Add(p.destParam2); h.Add(p.destParam3); h.Add(p.destParam4);
	}
	h.Add(ActiveMonsterCount);
	std::array<bool, MaxMonsters> seenMonsters {};
	for (size_t i = 0; i < ActiveMonsterCount; ++i) {
		const unsigned slot = ActiveMonsters[i];
		if (slot >= MaxMonsters || seenMonsters[slot]) return false;
		seenMonsters[slot] = true;
		const Monster &m = Monsters[slot];
		h.Add(slot); h.Add(m.position); h.Add(m.animInfo); h.Add(m.levelType); h.Add(m.mode); h.Add(m.direction);
		h.Add(m.flags); h.Add(m.hitPoints); h.Add(m.isInvalid); h.Add(m.enemy); h.Add(m.enemyPosition);
		h.Add(m.uniqueType); h.Add(m.uniqTrans); h.Add(m.lightId); h.Translation(m.uniqueMonsterTRN.get());
	}
	h.Add(Missiles.size());
	for (const Missile &m : Missiles) {
		h.Add(&m); h.Add(m._mitype); h.Add(m.position); h.Add(m._miDelFlag); h.Add(m._misource); h.Add(m._micaster);
		h.Add(m._miAnimType); h.Add(m._miAnimData); h.Add(m._miAnimFlags); h.Add(m._miAnimDelay);
		h.Add(m._miAnimLen); h.Add(m._miAnimCnt); h.Add(m._miAnimFrame); h.Add(m._miAnimAdd);
		h.Add(m._miAnimWidth); h.Add(m._miAnimWidth2);
		h.Add(m._miDrawFlag); h.Add(m._miPreFlag); h.Add(m._miLightFlag); h.Add(m._miUniqTrans);
		h.Add(m.duration); h.Add(m._mlid);
		if (m._miAnimData && m._miAnimFrame > 0 && static_cast<size_t>(m._miAnimFrame) <= m._miAnimData->numSprites())
			h.Add((*m._miAnimData)[m._miAnimFrame - 1]);
		if (m._miUniqTrans != 0) {
			if (m._misource < 0 || static_cast<size_t>(m._misource) >= MaxMonsters) return false;
			// The source can have left ActiveMonsters while its missile remains.
			h.Translation(Monsters[m._misource].uniqueMonsterTRN.get());
		}
	}
	for (const Corpse &corpse : Corpses) {
		h.Add(corpse.sprites); h.Add(corpse.frame); h.Add(corpse.width); h.Add(corpse.translationPaletteIndex);
		if (corpse.translationPaletteIndex != 0) {
			if (corpse.translationPaletteIndex > MaxMonsters) return false;
			h.Translation(Monsters[corpse.translationPaletteIndex - 1].uniqueMonsterTRN.get());
		}
		if (!corpse.sprites) continue;
		const size_t directions = corpse.sprites->isSheet() ? corpse.sprites->sheet().numLists() : 1;
		for (size_t direction = 0; direction < directions; ++direction) {
			const ClxSpriteList sprites = corpse.sprites->isSheet() ? corpse.sprites->sheet()[direction] : corpse.sprites->list();
			if (corpse.frame < 0 || static_cast<uint32_t>(corpse.frame) >= sprites.numSprites()) return false;
			h.Add(sprites[corpse.frame]);
		}
	}
	h.Add(ProgressToNextGameTick);
	if (!h.valid) return false;
	fingerprint = h.value;
	return true;
}

void ClearFrame() noexcept
{
	if (State == nullptr) return;
	State->snapshot.reset();
	State->scene.reset();
	State->fingerprint = 0;
	State->frameRevision = 0;
}

} // namespace

void InvalidateLiveLevel() noexcept
{
	State.reset();
}

void MarkLiveLevelLoaded(uint8_t entry) noexcept
{
	InvalidateLiveLevel();
	if (!NativeEligible() || entry > ENTRY_TWARPUP || EpochCounter == std::numeric_limits<uint64_t>::max()) return;
	try {
		auto state = std::make_unique<LiveState>();
		state->seed = DungeonSeeds[currlevel];
		state->entry = entry;
		state->epoch = ++EpochCounter;
		State = std::move(state);
	} catch (...) {
		InvalidateLiveLevel();
	}
}

bool LiveLevelEligible() noexcept
{
	return State != nullptr && State->epoch != 0 && NativeEligible() && DungeonSeeds[currlevel] == State->seed;
}

bool RefreshLiveFrame(int focusX, int focusZ) noexcept
{
	if (!LiveLevelEligible() || focusX < ActiveMin || focusX >= ActiveMax || focusZ < ActiveMin || focusZ >= ActiveMax) {
		ClearFrame();
		return false;
	}
	try {
		uint64_t before = 0, after = 0;
		if (!NativeFingerprint(before)) { ClearFrame(); return false; }
		auto snapshot = std::make_unique<Snapshot>(CaptureSnapshot({ State->seed, State->epoch, State->entry }));
		auto scene = std::make_unique<Scene>(State->adapter.Update(*snapshot));
		if (!NativeFingerprint(after) || before != after) { ClearFrame(); return false; }
		const bool changed = State->snapshot == nullptr || State->fingerprint != after
		    || State->focusX != focusX || State->focusZ != focusZ;
		if (changed) {
			if (FrameCounter == std::numeric_limits<uint64_t>::max()) { ClearFrame(); return false; }
			State->frameRevision = ++FrameCounter;
		}
		State->focusX = focusX; State->focusZ = focusZ;
		State->fingerprint = after;
		State->snapshot = std::move(snapshot);
		State->scene = std::move(scene);
		return true;
	} catch (...) {
		ClearFrame();
		return false;
	}
}

bool LiveFrameCurrent() noexcept
{
	uint64_t current = 0;
	return State != nullptr && State->snapshot != nullptr && State->scene != nullptr
	    && NativeFingerprint(current) && current == State->fingerprint;
}

const Snapshot *LiveSnapshot() noexcept { return LiveFrameCurrent() ? State->snapshot.get() : nullptr; }
const Scene *LiveScene() noexcept { return LiveFrameCurrent() ? State->scene.get() : nullptr; }
uint64_t LiveEpoch() noexcept { return LiveLevelEligible() ? State->epoch : 0; }
uint64_t LiveSceneRevision() noexcept { return LiveFrameCurrent() ? State->frameRevision : 0; }

} // namespace devilution::cathedral
