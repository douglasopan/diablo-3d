#include "levels/surface_world.hpp"

#include <algorithm>
#include <cassert>
#include <climits>
#include <type_traits>
#include <atomic>
#include <memory>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <mutex>
#include <new>
#include <utility>

#include "diablo.h"
#include "engine/assets.hpp"
#include "engine/render/town_visual_scene.hpp"
#include "engine/render/town_border_environment.hpp"
#include "engine/render/surface_region_view_preflight.hpp"
#include "engine/render/surface_region_visual_transaction.hpp"
#include "engine/render/surface_region_prospective_view.hpp"
#include "engine/render/town_view.hpp"
#include "options.h"
#include "panels/quest_log.hpp"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "utils/ui_fwd.h"
#include "control/control.hpp"
#include "inv.h"
#include "levels/trigs.h"
#include "levels/surface_town_visual_codec.hpp"
#include "mods/mod_identity.h"
#include "quests.h"
#include "utils/ini.hpp"
#include "engine/render/town_scene_logic.hpp"
#include "game_mode.hpp"
#include "interfac.h"
#include "levels/gendung.h"
#include "levels/surface_region_obstacles.hpp"
#include "player.h"

namespace devilution {
namespace {
std::mutex StateMutex;
SurfaceWorldState State;
uint64_t CampaignId = 1;
bool FreshCampaign = false;
bool ProtectedArchive = false;
std::shared_ptr<const surface::TownVisualMap> TownVisualOwner;
uint64_t TownCaptureEpoch = 0;
std::shared_ptr<const SurfaceWorldSnapshot> Published;
std::atomic<uint64_t> Epoch { 1 };
struct TownVisualPreparedSources;
struct PendingTransition {
	surface::Transition transition;
	SurfaceWorldState before;
	SurfaceWorldState after;
	std::shared_ptr<const SurfaceWorldSnapshot> beforeSnapshot;
	std::shared_ptr<const SurfaceWorldSnapshot> afterSnapshot;
	std::shared_ptr<const surface::TownVisualMap> beforeTownVisualMap;
	std::shared_ptr<const surface::TownVisualMap> afterTownVisualMap;
	std::shared_ptr<const TownVisualPreparedSources> sources;
	uint64_t preparedEpoch = 0;
	uint64_t committedEpoch = 0;
	surface::Tile sourceTile;
};
std::optional<PendingTransition> Pending;
// Callbacks below may inspect the published World. Every caller releases
// StateMutex first, including token destruction and cancelled Pending drafts.
void CancelTransitionDrafts(const PendingTransition &draft) noexcept
{
    if (draft.afterSnapshot) CancelTownViewSurfaceVisualResources(*draft.afterSnapshot);
    if (draft.beforeSnapshot) CancelTownViewSurfaceVisualResources(*draft.beforeSnapshot);
}
struct DraftAbortGuard {
    std::shared_ptr<const SurfaceWorldSnapshot> draft;
    bool armed = true;
    ~DraftAbortGuard() { if (armed && draft) CancelTownViewSurfaceVisualResources(*draft); }
};
struct TransitionAbortGuard {
    const PendingTransition &draft;
    bool armed = true;
    ~TransitionAbortGuard() { if (armed) CancelTransitionDrafts(draft); }
};
surface::Tile Arrival;
bool ArrivalPending = false;
bool ExitLatch = true;
bool TownReady = false;

// StateMutex must be held. Epoch covers region publications; campaignId also
// distinguishes NEW/restore even when both use native Town and identical seeds.
bool SameCampaignSaveContext(const SurfaceCampaignSaveContext &context)
{
	return context.campaignId == CampaignId && context.epoch == GetSurfaceWorldEpoch()
	    && context.freshCampaign == FreshCampaign && context.state.region == State.region
	    && context.state.seed == State.seed && context.state.revision == State.revision
	    && context.state.visited == State.visited && context.townVisualMap == TownVisualOwner
	    && context.protectedArchive == ProtectedArchive;
}

bool SurfaceCarrier()
{
	return IsSurfaceWorldSupported() && !setlevel && currlevel == 0 && leveltype == DTYPE_TOWN;
}


// All I/O is preflight/load-time. These helpers never run in Draw, regenerate
// Town, apply a native patch, resync quests or advance any random/animation clock.
bool ReadTownVisualAsset(std::string_view path, std::vector<std::byte> &bytes, std::string &failure)
{
	AssetRef ref = FindAsset(path);
	constexpr size_t MaximumAssetBytes = 96 * 1024 * 1024;
	if (!ref.ok() || ref.size() == 0 || ref.size() > MaximumAssetBytes) {
		failure = "Town visual source asset missing or outside budget";
		return false;
	}
	const size_t size = ref.size();
	AssetHandle handle = OpenAsset(std::move(ref));
	bytes.resize(size);
	if (!handle.ok() || !handle.read(bytes.data(), bytes.size())) {
		failure = "Unable to read Town visual source asset";
		return false;
	}
	return true;
}

// Mutable preparation is native-loader/host work, never Draw and never under
// StateMutex. Sources are owned locally; no cold renderer cache is consulted.
struct TownVisualPreparedSources {
    std::array<uint8_t, 32> canonicalSha256 {}, worldPatchSha256 {};
    TownVisualSelectionSnapshot selection;
    TownBorderVisualAdmission admission;
    std::shared_ptr<const TownSurroundingsVisualScene> environment;
    std::vector<std::byte> natureBytes;
};
// Private authenticated inputs retained for epoch-only repreflight; no derived
// render owner is added to the public World snapshot.
std::shared_ptr<const TownVisualPreparedSources> VisualSources;
bool SameSelection(const TownVisualSelectionSnapshot &a, const TownVisualSelectionSnapshot &b)
{
    return a.kind == b.kind && a.canonicalSha256 == b.canonicalSha256
        && a.selectedSha256 == b.selectedSha256 && a.staticSha256 == b.staticSha256;
}
bool SameAdmission(const TownBorderVisualAdmission &a, const TownBorderVisualAdmission &b)
{
    return a.status == b.status && a.nativeMapEpoch == b.nativeMapEpoch
        && a.companionSha256 == b.companionSha256 && a.baseEnvironmentSha256 == b.baseEnvironmentSha256;
}
bool ResolveTownVisualPreflight(std::string_view path, std::vector<std::byte> &bytes,
    std::string &sourceKind, std::string &failure)
{
    sourceKind = "townvisual-owned-preflight";
    return ReadTownVisualAsset(path, bytes, failure);
}
bool ValidateTownSelectionAssets(const std::array<uint8_t, 32> &canonicalSha,
    const std::array<uint8_t, 32> &patchSha, const TownVisualSelectionSnapshot &selection)
{
    if (selection.canonicalSha256 != canonicalSha || selection.kind == TownVisualSelectionKind::NotAdmitted)
        return false;
    std::vector<std::byte> canonical, patch, statics;
    std::string failure;
    if (!ReadTownVisualAsset(TownVisualCanonicalSelectionPath, canonical, failure) || canonical.size() > 256 * 1024
        || ComputeBytesSha256(canonical) != canonicalSha
        || !ReadTownVisualAsset(TownSceneLogicPatchPath, patch, failure) || patch.size() > 16 * 1024 * 1024
        || ComputeBytesSha256(patch) != patchSha)
        return false;
    const auto config = Ini::parse(std::string_view(reinterpret_cast<const char *>(canonical.data()), canonical.size()));
    std::array<uint8_t, 32> staticSha;
    if (!config || config->getString("Scene", "worldPatch") != TownSceneLogicPatchPath
        || config->getString("Scene", "worldPatchSha256") != ModHashToHex(patchSha)
        || !HexToModHash(config->getString("Scene", "staticSha256"), staticSha)
        || staticSha != selection.staticSha256
        || !ReadTownVisualAsset(config->getString("Scene", "staticData"), statics, failure)
        || ComputeBytesSha256(statics) != staticSha
        || !ValidateTownVisualSceneForLogic(canonical, ResolveTownVisualPreflight, failure))
        return false;
    if (selection.kind == TownVisualSelectionKind::Canonical)
        return selection.selectedSha256 == canonicalSha;
    if (selection.kind != TownVisualSelectionKind::SpecificOptional)
        return false;
    std::array<uint8_t, 32> exactCanonical, exactSelected;
    if (!HexToModHash(TownVisualSpecificCanonicalSha256, exactCanonical)
        || !HexToModHash(TownVisualSpecificSelectedSha256, exactSelected)
        || canonicalSha != exactCanonical || selection.selectedSha256 != exactSelected)
        return false;
    // No fallback: this exact optional file AND all fourteen models must pass.
    std::vector<std::byte> selected;
    if (!ReadTownVisualAsset(TownVisualSpecificSelectionPath, selected, failure)
        || selected.size() > 256 * 1024 || ComputeBytesSha256(selected) != selection.selectedSha256)
        return false;
    const auto optionalConfig = Ini::parse(std::string_view(reinterpret_cast<const char *>(selected.data()), selected.size()));
    if (!optionalConfig || optionalConfig->getString("Scene", "staticData") != config->getString("Scene", "staticData")
        || optionalConfig->getString("Scene", "staticSha256") != ModHashToHex(staticSha)
        || optionalConfig->getString("Scene", "worldPatch") != TownSceneLogicPatchPath
        || optionalConfig->getString("Scene", "worldPatchSha256") != ModHashToHex(patchSha))
        return false;
    return ValidateTownVisualSceneForLogic(selected, ResolveTownVisualPreflight, failure);
}
bool ReadTownNatureSources(TownVisualPreparedSources &sources)
{
    if (sources.admission.status == TownBorderVisualStatus::NotPrepared
        || sources.admission.nativeMapEpoch == 0)
        return false;
    std::string failure;
    std::vector<std::byte> baseBytes;
    if (!ReadTownVisualAsset("d3d-scenes/environment/tristram-surroundings-r2.d3denv", baseBytes, failure)
        || ComputeBytesSha256(baseBytes) != sources.admission.baseEnvironmentSha256)
        return false;
    sources.environment = DecodeTownSurroundingsVisualSceneOwned(baseBytes, failure);
    if (!sources.environment)
        return false;
    if (sources.admission.status == TownBorderVisualStatus::Refused)
        return true; // No Nature asset read or revival for a known refusal.
    if (sources.admission.status != TownBorderVisualStatus::Admitted)
        return false;
    return ReadTownVisualAsset("d3d-scenes/environment/tristram-town-border-r4.d3dborder", sources.natureBytes, failure)
        && ComputeBytesSha256(sources.natureBytes) == sources.admission.companionSha256;
}
std::shared_ptr<const TownBorderFrozenVisualOwner> PrepareTownNatureOwner(
    const surface::TownVisualMap &map, const TownVisualPreparedSources &sources, uint64_t worldEpoch)
{
    if (!sources.environment)
        return {};
    const TownBorderFrozenMapView view { map.pieces, map.specials, map.mapSha256,
        map.sourceTownSceneSha256, map.capturedTownEpoch };
    std::string failure;
    auto owner = PrepareTownBorderForFrozenMap(view, map.natureAdmission, *sources.environment,
        sources.natureBytes, worldEpoch, failure);
    return owner && owner->decisionComplete ? owner : nullptr;
}
std::optional<TownVisualPreparedSources> PrepareLiveTownVisualSources()
{
    if (!SurfaceCarrier() || !TownNativeMapPresentationActive())
        return std::nullopt;
    // GetTownScene may prepare renderer resources. It is never called while
    // holding StateMutex, and is required even before a first 3D Draw or after F4.
    (void)GetTownScene();
    TownVisualPreparedSources sources;
    std::vector<std::byte> canonical;
    std::string failure;
    if (!ReadTownVisualAsset(TownVisualCanonicalSelectionPath, canonical, failure)
        || !HexToModHash(GetTownSceneLogicAudit().sha256, sources.worldPatchSha256))
        return std::nullopt;
    sources.canonicalSha256 = ComputeBytesSha256(canonical);
    if (!CaptureTownVisualSelectionForSnapshot(sources.canonicalSha256, sources.selection))
        return std::nullopt;
    sources.admission = CaptureTownBorderVisualAdmission();
    if (!ValidateTownSelectionAssets(sources.canonicalSha256, sources.worldPatchSha256, sources.selection)
        || !ReadTownNatureSources(sources))
        return std::nullopt;
    return sources;
}
bool ValidateTownVisualAssets(const surface::TownVisualMap &map, uint64_t worldEpoch,
    TownVisualPreparedSources &sources, std::shared_ptr<const TownBorderFrozenVisualOwner> &nature)
{
    if (surface::ValidateTownVisualMap(map) != surface::TownVisualCodecError::None
        || !ValidateTownSelectionAssets(map.sourceTownSceneSha256, map.worldPatchSha256, map.visualSelection))
        return false;
    // Match native MIN preference. No regional carrier asset or MAXTILES guess.
    AssetRef min = FindAsset("nlevels\\towndata\\town.min");
    if (!min.ok()) min = FindAsset("levels\\towndata\\town.min");
    constexpr size_t BytesPerTownPiece = 16 * sizeof(uint16_t);
    if (!min.ok() || min.size() == 0 || min.size() % BytesPerTownPiece != 0
        || min.size() / BytesPerTownPiece > surface::TownVisualMaximumPieceCount)
        return false;
    for (const uint16_t piece : map.pieces) if (piece >= min.size() / BytesPerTownPiece) return false;
    sources.canonicalSha256 = map.sourceTownSceneSha256; sources.worldPatchSha256 = map.worldPatchSha256;
    sources.selection = map.visualSelection; sources.admission = map.natureAdmission;
    if (!ReadTownNatureSources(sources))
        return false;
    nature = PrepareTownNatureOwner(map, sources, worldEpoch);
    return nature != nullptr;
}

// StateMutex held; input is the real native Town AFTER schema5 and north cuts.
bool CaptureTownVisualMap(const TownVisualPreparedSources &sources, surface::TownVisualMap &map)
{
    // StateMutex held, native owner thread. Only literal arrays and O(1) leaf
    // getters are captured; preparation/I/O/parsing is entirely outside here.
    if (!SurfaceCarrier() || !TownNativeMapPresentationActive() || !State.region.IsTown() || !TownReady || MyPlayer == nullptr
        || TownCaptureEpoch >= std::numeric_limits<uint64_t>::max() - 1)
        return false;
    const auto &logic = GetTownSceneLogicAudit();
    if (logic.status != "applied" || logic.sha256 != "9d14b0c141da31ee242b902a71d76adcb58c924769f6a825793110526f2c3478"
        || dPiece[56][10] != 0 || dPiece[57][10] != 0 || dSpecial[56][10] != 0 || dSpecial[57][10] != 0)
        return false;
    TownVisualSelectionSnapshot selection;
    const auto admission = CaptureTownBorderVisualAdmission();
    if (!CaptureTownVisualSelectionForSnapshot(sources.canonicalSha256, selection)
        || !SameSelection(selection, sources.selection) || !SameAdmission(admission, sources.admission))
        return false;
    map.capturedTownEpoch = TownCaptureEpoch + 1;
    for (size_t z = 0; z < surface::TownVisualSide; ++z)
        for (size_t x = 0; x < surface::TownVisualSide; ++x) {
            const size_t at = z * surface::TownVisualSide + x;
            map.pieces[at] = dPiece[x][z]; map.specials[at] = static_cast<uint8_t>(dSpecial[x][z]);
        }
    map.events = { Quests[Q_PWATER]._qactive != QUEST_DONE && Quests[Q_PWATER]._qactive != QUEST_NOTAVAIL,
        IsWarpOpen(DTYPE_CATACOMBS), IsWarpOpen(DTYPE_CAVES), IsWarpOpen(DTYPE_HELL) };
    map.sourceTownSceneSha256 = sources.canonicalSha256; map.worldPatchSha256 = sources.worldPatchSha256;
    map.natureAdmission = admission; map.visualSelection = selection;
    return true;
}

bool ValidateProspectiveContext(const SurfaceWorldProspectiveContext &context)
{
    return context.playerTile.x >= surface::InteriorMin && context.playerTile.x <= surface::InteriorMax
        && context.playerTile.y >= surface::InteriorMin && context.playerTile.y <= surface::InteriorMax
        && context.nativeViewPosition.x >= 0 && context.nativeViewPosition.x < surface::StorageSize
        && context.nativeViewPosition.y >= 0 && context.nativeViewPosition.y < surface::StorageSize
        && context.mode >= PM_STAND && context.mode <= PM_QUIT
        && context.walking == (context.mode == PM_WALK_NORTHWARDS || context.mode == PM_WALK_SOUTHWARDS || context.mode == PM_WALK_SIDEWAYS)
        && context.direction >= 0 && context.direction < 8
        && context.ticksPerFrame >= 1 && context.ticksPerFrame <= INT8_MAX
        && context.tickCounter >= 0 && context.tickCounter < context.ticksPerFrame
        && context.numberOfFrames >= 1 && context.numberOfFrames <= INT8_MAX
        && context.currentFrame >= 0 && context.currentFrame < context.numberOfFrames;
}
bool CaptureLiveProspectiveContext(SurfaceWorldProspectiveContext &context)
{
    if (MyPlayer == nullptr) return false;
    context.playerTile = MyPlayer->position.tile; context.nativeViewPosition = ViewPosition;
    context.inventoryOpen = invflag; context.characterOpen = CharFlag;
    context.mode = MyPlayer->_pmode; context.direction = static_cast<int32_t>(MyPlayer->_pdir);
    context.walking = MyPlayer->isWalking();
    context.ticksPerFrame = MyPlayer->AnimInfo.ticksPerFrame;
    context.tickCounter = MyPlayer->AnimInfo.tickCounterOfCurrentFrame;
    context.numberOfFrames = MyPlayer->AnimInfo.numberOfFrames;
    context.currentFrame = MyPlayer->AnimInfo.currentFrame;
    return ValidateProspectiveContext(context);
}

bool BuildWorldProspectiveView(const surface::RegionGeometry &geometry,
    const SurfaceWorldProspectiveContext &context, bool firstFlagEntry,
    SurfaceRegionProspectiveView &output)
{
    if (!ValidateProspectiveContext(context)) return false;
    const auto support = surface::SampleRegionSupport(geometry, context.nativeViewPosition.x, context.nativeViewPosition.y);
    if (!support) return false;
    SurfaceRegionProspectiveViewRequest request;
    request.id = geometry.regionId; request.playerTile = context.playerTile;
    request.nativeViewPosition = context.nativeViewPosition; request.walking = context.walking;
    request.localAnchor = { float(context.nativeViewPosition.x), float(support->heightMetres / surface::CellMetres), float(context.nativeViewPosition.y) };
    auto &viewport = request.viewport;
    viewport.width = GetScreenWidth(); viewport.screenHeight = GetScreenHeight();
    viewport.viewportHeight = GetViewportHeight();
    viewport.mainPanelHeight = GetMainPanel().size.height; viewport.sidePanelWidth = SidePanelSize.width;
    viewport.zoomed = *GetOptions().Graphics.zoom; viewport.panelsCanCover = CanPanelsCoverView();
    // FirstFlagEntry closes inventory/stash/store; InitMainPanel then closes
    // character/spellbook. Neither clears QuestLogIsOpen. No UI is changed here.
    viewport.leftPanelOpen = (!firstFlagEntry && context.characterOpen) || QuestLogIsOpen
        || (!firstFlagEntry && (IsStashOpen || IsVisualStoreOpen));
    viewport.rightPanelOpen = !firstFlagEntry && (context.inventoryOpen || SpellbookFlag);
    std::string failure;
    if (!BuildSurfaceRegionProspectiveView(request, output, failure)) return false;
    // Camera mode is a read-only renderer preference. A disabled F4 rig uses
    // ISO even before this prospective regional state has been published.
    const auto mode = IsTownViewEnabled() ? GetTownViewCameraMode() : TownCameraMode::Isometric;
    if (mode == TownCameraMode::ThirdPerson || mode == TownCameraMode::FirstPerson) {
        const auto playerSupport = surface::SampleRegionSupport(geometry, context.playerTile.x, context.playerTile.y);
        if (!playerSupport) return false;
        output.view.localAnchor = { float(context.playerTile.x), float(playerSupport->heightMetres / surface::CellMetres), float(context.playerTile.y) };
    }
    return true;
}

// All geometry, halo and renderer allocations are draft-only. The consumer's
// multi-epoch cache retains the published origin through a refused/cancelled
// draft; this helper never publishes a World or touches a native carrier.
bool PrepareRegionalVisualDraft(SurfaceWorldSnapshot &draft,
    const SurfaceRegionViewPreflight &view, double walkingPaddingNative,
    const TownVisualPreparedSources &sources,
    const std::shared_ptr<const TownBorderFrozenVisualOwner> &nature,
    const surface::SurfaceHaloRequest *requiredRequest = nullptr)
{
    if (!draft.geometry || !draft.region || !draft.townVisualMap || draft.epoch == 0
        || !sources.environment || !nature || !nature->decisionComplete
        || nature->worldSnapshotEpoch != draft.epoch
        || walkingPaddingNative < 0 || walkingPaddingNative > 1) return false;
    std::string failure;
    surface::SurfaceHaloRequest request;
    if (!PrepareTownViewSurfaceHaloRequest(*draft.geometry, view, request, failure)
        || !request.requiredWorldBounds) return false;
    // Native walking displacements are bounded in each X/Z component once.
    // This is a conservative pose envelope, not a reconstructed tick timestamp.
    auto &bounds = *request.requiredWorldBounds;
    bounds.minX -= walkingPaddingNative; bounds.minZ -= walkingPaddingNative;
    bounds.maxX += walkingPaddingNative; bounds.maxZ += walkingPaddingNative;
    if (requiredRequest) {
        if (requiredRequest->activeRegion != draft.id || !requiredRequest->requiredWorldBounds) return false;
        const auto &required = *requiredRequest->requiredWorldBounds;
        bounds.minX = std::min(bounds.minX, required.minX); bounds.minZ = std::min(bounds.minZ, required.minZ);
        bounds.maxX = std::max(bounds.maxX, required.maxX); bounds.maxZ = std::max(bounds.maxZ, required.maxZ);
    }
    request.maximumCameraOffsetMetres = 0; // The prospective frustum already includes camera displacement.
    auto halo = surface::BuildSurfaceHalo(request);
    if (!halo || !surface::ValidateSurfaceHalo(*halo)) return false;
    draft.halo = std::make_shared<const surface::SurfaceHaloGeometry>(std::move(*halo));
    if (!PrepareTownViewSurfaceVisualResources(draft, { sources.environment, nature }, failure)) {
        CancelTownViewSurfaceVisualResources(draft); // Includes partial staging on false.
        return false;
    }
    return true;
}

bool PreparedSourcesMatchMap(const TownVisualPreparedSources &sources, const surface::TownVisualMap &map)
{
    return sources.canonicalSha256 == map.sourceTownSceneSha256 && sources.worldPatchSha256 == map.worldPatchSha256
        && SameSelection(sources.selection, map.visualSelection) && SameAdmission(sources.admission, map.natureAdmission);
}
bool CompleteRegionalVisualDraft(SurfaceWorldSnapshot &draft,
    const std::shared_ptr<const surface::TownVisualMap> &map,
    const TownVisualPreparedSources &sources, uint64_t epoch,
    const SurfaceWorldProspectiveContext &context, bool firstFlagEntry,
    const surface::SurfaceHaloRequest *requiredRequest = nullptr,
    const std::shared_ptr<const TownBorderFrozenVisualOwner> &preparedNature = {})
{
    if (!map || !PreparedSourcesMatchMap(sources, *map) || epoch == 0 || !draft.geometry) return false;
    const auto playerSupport = surface::SampleRegionSupport(*draft.geometry, context.playerTile.x, context.playerTile.y);
    if (!playerSupport || playerSupport->kind == surface::CellKind::Blocked) return false;
    draft.epoch = epoch; draft.townVisualMap = map;
    SurfaceRegionProspectiveView prospective;
    if (!BuildWorldProspectiveView(*draft.geometry, context, firstFlagEntry, prospective)) return false;
    const auto nature = preparedNature ? preparedNature : PrepareTownNatureOwner(*map, sources, epoch);
    return PrepareRegionalVisualDraft(draft, prospective.view, prospective.walkingWorldPaddingNative,
        sources, nature, requiredRequest);
}

std::shared_ptr<SurfaceWorldSnapshot> BuildSnapshot(surface::RegionId id)
{
	if (id.IsTown())
		return {};
	auto region = surface::GenerateRegion(id);
	if (!region)
		return {};
	auto geometry = surface::BuildRegionGeometry(*region);
	if (!geometry)
		return {};
	auto result = std::make_shared<SurfaceWorldSnapshot>();
	result->id = id;
	result->worldOffset = region->worldOffset;
	result->region = std::make_shared<const surface::Region>(std::move(*region));
	result->geometry = std::make_shared<const surface::RegionGeometry>(std::move(*geometry));
	return result;
}

// Called under StateMutex. Cloning the small header preserves the very same
// Region/triangles while stamping a fresh identity after commit/restore/rollback.
void Publish(std::shared_ptr<const SurfaceWorldSnapshot> snapshot, std::shared_ptr<const surface::TownVisualMap> townVisualMap)
{
	const uint64_t next = Epoch.load(std::memory_order_relaxed) + 1;
	if (snapshot) {
		auto stamped = std::make_shared<SurfaceWorldSnapshot>(*snapshot);
		stamped->epoch = next;
		stamped->townVisualMap = townVisualMap;
		snapshot = std::move(stamped);
	}
	TownVisualOwner = std::move(townVisualMap);
	if (TownVisualOwner)
		TownCaptureEpoch = std::max(TownCaptureEpoch, TownVisualOwner->capturedTownEpoch);
	std::atomic_store_explicit(&Published, std::move(snapshot), std::memory_order_release);
	Epoch.store(next, std::memory_order_release);
}
} // namespace

bool IsSurfaceWorldSupported() { return !gbIsMultiplayer && !gbIsHellfire && !gbIsSpawn; }
SurfaceWorldState GetSurfaceWorldState()
{
	std::lock_guard lock(StateMutex);
	return State;
}
SurfaceCampaignSaveContext CaptureSurfaceCampaignSaveContext()
{
	std::lock_guard lock(StateMutex);
	return { State, CampaignId, GetSurfaceWorldEpoch(), FreshCampaign, TownVisualOwner, ProtectedArchive };
}
bool IsSurfaceFreshCampaign()
{
	std::lock_guard lock(StateMutex);
	return FreshCampaign;
}
bool IsSurfaceCampaignSaveContextCurrent(const SurfaceCampaignSaveContext &context)
{
	std::lock_guard lock(StateMutex);
	return SameCampaignSaveContext(context);
}
bool CompleteSurfaceCampaignSave(const SurfaceCampaignSaveContext &context)
{
	std::lock_guard lock(StateMutex);
	if (!SameCampaignSaveContext(context))
		return false;
	FreshCampaign = false;
	return true;
}
std::shared_ptr<const SurfaceWorldSnapshot> GetSurfaceWorldSnapshot()
{
	return std::atomic_load_explicit(&Published, std::memory_order_acquire);
}
uint64_t GetSurfaceWorldEpoch() { return Epoch.load(std::memory_order_acquire); }
bool IsSurfaceRegionActive() { return SurfaceCarrier() && GetSurfaceWorldSnapshot() != nullptr; }

void ResetSurfaceWorldState(bool protectedArchive)
{
    std::optional<PendingTransition> cancelled;
    {
        std::lock_guard lock(StateMutex);
        cancelled.swap(Pending);
        State = {}; VisualSources.reset(); ++CampaignId; FreshCampaign = true;
        ProtectedArchive = protectedArchive; ArrivalPending = false; ExitLatch = true; TownReady = false;
        Publish({}, {});
    }
    if (cancelled) CancelTransitionDrafts(*cancelled);
}

struct PreparedSurfaceWorldRestore::Impl {
	SurfaceWorldState state;
	std::shared_ptr<SurfaceWorldSnapshot> publication;
	std::shared_ptr<const surface::TownVisualMap> townVisualMap;
	bool protectedArchive = false;
	uint64_t nextEpoch = 0;
	// Preflight-only owners; never published as render owners in WorldSnapshot.
	std::shared_ptr<const TownVisualPreparedSources> sources;
	std::shared_ptr<const TownBorderFrozenVisualOwner> naturePreflight;
	std::unique_lock<std::mutex> lock;
    ~Impl() {
        if (lock.owns_lock()) lock.unlock();
        if (publication) CancelTownViewSurfaceVisualResources(*publication);
    }
};
PreparedSurfaceWorldRestore::PreparedSurfaceWorldRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) { }
PreparedSurfaceWorldRestore::~PreparedSurfaceWorldRestore() = default;
PreparedSurfaceWorldRestore::PreparedSurfaceWorldRestore(PreparedSurfaceWorldRestore &&) noexcept = default;
PreparedSurfaceWorldRestore &PreparedSurfaceWorldRestore::operator=(PreparedSurfaceWorldRestore &&) noexcept = default;

bool IsSurfaceCampaignProtected()
{
	std::lock_guard lock(StateMutex);
	return IsSurfaceWorldSupported() && (ProtectedArchive || TownVisualOwner || !State.region.IsTown() || !State.visited.empty());
}
SurfaceLevelSaveContext CaptureSurfaceLevelSaveContext()
{
	std::lock_guard lock(StateMutex);
	return { CampaignId, GetSurfaceWorldEpoch(), FreshCampaign, ProtectedArchive, TownVisualOwner, TownCaptureEpoch };
}
bool IsSurfaceLevelSaveContextCurrent(const SurfaceLevelSaveContext &context)
{
	std::lock_guard lock(StateMutex);
	return context.campaignId == CampaignId && context.epoch == GetSurfaceWorldEpoch()
	    && context.freshCampaign == FreshCampaign && context.protectedArchive == ProtectedArchive
	    && context.townVisualMap == TownVisualOwner && context.townCaptureEpoch == TownCaptureEpoch;
}
void RestoreFailedSurfaceLevelSave(const SurfaceLevelSaveContext &before, const SurfaceLevelSaveContext &after)
{
	std::lock_guard lock(StateMutex);
	if (before.campaignId != after.campaignId || before.epoch != after.epoch
	    || after.campaignId != CampaignId || after.epoch != GetSurfaceWorldEpoch()
	    || after.freshCampaign != FreshCampaign || after.protectedArchive != ProtectedArchive
	    || after.townVisualMap != TownVisualOwner || after.townCaptureEpoch != TownCaptureEpoch)
		return; // Never overwrite a new campaign or another capture.
	TownVisualOwner = before.townVisualMap;
	TownCaptureEpoch = before.townCaptureEpoch;
}
std::optional<PreparedSurfaceWorldRestore> PrepareSurfaceWorldRestore(const SurfaceWorldState &state,
    std::shared_ptr<const surface::TownVisualMap> townVisualMap, bool protectedArchive,
    const SurfaceWorldProspectiveContext *prospective, bool firstFlagEntry)
{
	if (surface::ValidatePersistentState(state) != surface::StateCodecError::None
	    || (!state.region.IsTown() && !IsSurfaceWorldSupported())) return std::nullopt;
	try {
		if ((!state.region.IsTown() || !state.visited.empty()) && !townVisualMap) return std::nullopt;
        if (!state.region.IsTown() && (!prospective || !ValidateProspectiveContext(*prospective))) return std::nullopt;
        const uint64_t preparedEpoch = GetSurfaceWorldEpoch();
        if (preparedEpoch == std::numeric_limits<uint64_t>::max()) return std::nullopt;
        TownVisualPreparedSources sources;
        std::shared_ptr<const TownBorderFrozenVisualOwner> nature;
        if (townVisualMap && !ValidateTownVisualAssets(*townVisualMap, preparedEpoch + 1, sources, nature)) return std::nullopt;
		auto snapshot = BuildSnapshot(state.region);
        DraftAbortGuard abort { snapshot };
		if (!state.region.IsTown() && !snapshot) return std::nullopt;
        if (snapshot && !CompleteRegionalVisualDraft(*snapshot, townVisualMap, sources,
                preparedEpoch + 1, *prospective, firstFlagEntry, nullptr, nature)) return std::nullopt;
		auto prepared = std::make_unique<PreparedSurfaceWorldRestore::Impl>();
		prepared->sources = std::make_shared<const TownVisualPreparedSources>(std::move(sources));
        prepared->naturePreflight = std::move(nature);
		prepared->state = state; // visited copy/allocation BEFORE cleanup
		prepared->townVisualMap = std::move(townVisualMap);
		prepared->protectedArchive = protectedArchive || prepared->townVisualMap || !state.region.IsTown() || !state.visited.empty();
		if (snapshot) prepared->publication = std::make_shared<SurfaceWorldSnapshot>(*snapshot);
		// Acquire last. No World getter is permitted in the cleanup call graph.
		prepared->lock = std::unique_lock<std::mutex>(StateMutex);
		if (Epoch.load(std::memory_order_relaxed) != preparedEpoch) return std::nullopt;
		prepared->nextEpoch = preparedEpoch + 1;
		if (prepared->publication) {
			prepared->publication->epoch = prepared->nextEpoch;
			prepared->publication->townVisualMap = prepared->townVisualMap;
		}
        abort.armed = false; // Impl now owns exact-stamp abort after unlocking.
		return PreparedSurfaceWorldRestore(std::move(prepared));
	} catch (const std::bad_alloc &) { return std::nullopt; }
}
void CommitPreparedSurfaceWorldRestore(PreparedSurfaceWorldRestore &&prepared) noexcept
{
	assert(prepared.impl_ && prepared.impl_->lock.owns_lock());
	auto &staged = *prepared.impl_;
    std::optional<PendingTransition> cancelled;
    cancelled.swap(Pending);
	static_assert(std::is_nothrow_move_assignable_v<SurfaceWorldState>);
	State = std::move(staged.state);
	TownVisualOwner = std::move(staged.townVisualMap);
	VisualSources = std::move(staged.sources);
	if (TownVisualOwner) TownCaptureEpoch = std::max(TownCaptureEpoch, TownVisualOwner->capturedTownEpoch);
	std::shared_ptr<const SurfaceWorldSnapshot> publication = std::move(staged.publication);
	std::atomic_store_explicit(&Published, std::move(publication), std::memory_order_release);
	Epoch.store(staged.nextEpoch, std::memory_order_release);
	ProtectedArchive = staged.protectedArchive;
	++CampaignId; FreshCampaign = false; ArrivalPending = false; ExitLatch = true; TownReady = false;
	prepared.impl_.reset(); // unlock first; moved publication is not cancelled
    if (cancelled) CancelTransitionDrafts(*cancelled);
}
bool RestoreSurfaceWorldState(const SurfaceWorldState &state,
    std::shared_ptr<const surface::TownVisualMap> townVisualMap, bool protectedArchive,
    const SurfaceWorldProspectiveContext *prospective, bool firstFlagEntry)
{
	// Keep the legacy Town reset free of any new allocation/failure point.
	if (state.region.IsTown() && state.visited.empty() && !townVisualMap
	    && surface::ValidatePersistentState(state) == surface::StateCodecError::None) {
        std::optional<PendingTransition> cancelled;
        {
            std::lock_guard lock(StateMutex);
            cancelled.swap(Pending);
            State = SurfaceWorldState {}; Publish({}, {}); VisualSources.reset(); ++CampaignId;
            FreshCampaign = false; ProtectedArchive = protectedArchive;
            ArrivalPending = false; ExitLatch = true; TownReady = false;
        }
        if (cancelled) CancelTransitionDrafts(*cancelled);
        return true;
	}
	auto prepared = PrepareSurfaceWorldRestore(state, std::move(townVisualMap), protectedArchive, prospective, firstFlagEntry);
	if (!prepared) return false;
	CommitPreparedSurfaceWorldRestore(std::move(*prepared));
	return true;
}

std::shared_ptr<const surface::TownVisualMap> GetSurfaceTownVisualMap()
{
	std::lock_guard lock(StateMutex);
	return TownVisualOwner;
}
bool PrepareSurfaceTownVisualCapture()
{
    try {
        {
            std::lock_guard lock(StateMutex);
            if (!SurfaceCarrier() || !State.region.IsTown() || !TownReady) return false;
        }
        // Preload only: do not create a Town owner, alter Fresh or turn a pure
        // legacy campaign into a Surface campaign merely for loading Town.
        return PrepareLiveTownVisualSources().has_value();
    } catch (const std::bad_alloc &) { return false; }
}
bool RefreshSurfaceTownVisualMap()
{
    try {
        {
            std::lock_guard lock(StateMutex);
            if (!SurfaceCarrier() || !State.region.IsTown() || !TownReady) return false;
        }
        const auto sources = PrepareLiveTownVisualSources(); // I/O/GetTownScene outside mutex
        if (!sources) return false;
        surface::TownVisualMap map;
        uint64_t campaign, epoch;
        {
            std::lock_guard lock(StateMutex);
            if (!CaptureTownVisualMap(*sources, map)) return false;
            campaign = CampaignId; epoch = GetSurfaceWorldEpoch();
        }
        map.mapSha256 = surface::ComputeTownVisualMapSha256(map);
        if (surface::ValidateTownVisualMap(map) != surface::TownVisualCodecError::None
            || !PrepareTownNatureOwner(map, *sources, epoch)) return false;
        auto owner = std::make_shared<const surface::TownVisualMap>(std::move(map));
        auto retainedSources = std::make_shared<const TownVisualPreparedSources>(*sources);
        {
            std::lock_guard lock(StateMutex);
            TownVisualSelectionSnapshot currentSelection;
            // These comparisons bind one live preparation/capture. Cold restore
            // NEVER compares historical NativeMapEpoch to a fresh live counter.
            if (campaign != CampaignId || epoch != GetSurfaceWorldEpoch() || !State.region.IsTown() || !TownReady
                || !TownNativeMapPresentationActive() || TownCaptureEpoch + 1 != owner->capturedTownEpoch
                || !SameAdmission(CaptureTownBorderVisualAdmission(), owner->natureAdmission)
                || !CaptureTownVisualSelectionForSnapshot(owner->sourceTownSceneSha256, currentSelection)
                || !SameSelection(currentSelection, owner->visualSelection)) return false;
            TownCaptureEpoch = owner->capturedTownEpoch;
            TownVisualOwner = std::move(owner);
            VisualSources = std::move(retainedSources);
        }
        return true;
    } catch (const std::bad_alloc &) { return false; }
}

float SurfaceGroundHeight(float localX, float localZ)
{
	const auto snapshot = GetSurfaceWorldSnapshot();
	if (!snapshot || !snapshot->geometry)
		return 0;
	const auto support = surface::SampleRegionSupport(*snapshot->geometry, localX, localZ);
	return support ? static_cast<float>(support->heightMetres / surface::CellMetres) : 0;
}

bool CanTraverseSurfaceStep(Point from, Point to)
{
    // Native Town/Cathedral and unsupported game variants retain their rules.
    if (!SurfaceCarrier())
        return true;
    const auto snapshot = GetSurfaceWorldSnapshot();
    if (!snapshot)
        return true;
    if (!snapshot->region || !snapshot->geometry || snapshot->epoch != GetSurfaceWorldEpoch())
        return false;
    const auto inside = [](Point p) { return p.x >= 2 && p.x < 110 && p.y >= 2 && p.y < 110; };
    const int dx = from.x - to.x, dz = from.y - to.y;
    if (!inside(from) || !inside(to) || dx < -1 || dx > 1 || dz < -1 || dz > 1 || (dx == 0 && dz == 0))
        return false;
    // Sweep from THIS step/search node, never the hero origin for every A* node.
    return surface::CanTraverseWorldSegment(
        double(snapshot->worldOffset.x) + from.x, double(snapshot->worldOffset.z) + from.y,
        double(snapshot->worldOffset.x) + to.x, double(snapshot->worldOffset.z) + to.y);
}

bool PrepareTownRegionalExits()
{
	std::lock_guard lock(StateMutex);
	TownReady = false;
	if (!SurfaceCarrier() || !State.region.IsTown())
		return false;
	const TownSceneLogicAudit &audit = GetTownSceneLogicAudit();
	if (audit.status != "applied" || audit.sha256 != "9d14b0c141da31ee242b902a71d76adcb58c924769f6a825793110526f2c3478"
	    || audit.targetStaticSha256 != "1e1d5894dc2b7c53cdfaf2ec19a9f53cf7aa52aca1e8c8ce22686b0c80042278"
	    || dPiece[40][40] != 0 || dSpecial[40][40] != 0
	    || dPiece[56][10] != 1053 || dSpecial[56][10] != 0
	    || dPiece[57][10] != 1054 || dSpecial[57][10] != 0)
		return false;
	for (const auto &slot : surface::ExitSlots) {
		for (std::size_t i = 0; i < slot.triggers.size(); ++i) {
			const auto tile = slot.triggers[i];
			const unsigned expected = slot.side == surface::Side::North ? (i == 0 ? 45 : 44) : 0;
			if (dPiece[tile.x][tile.z] != expected || dSpecial[tile.x][tile.z] != 0)
				return false;
		}
		const unsigned expected = slot.side == surface::Side::North ? 47 : 0;
		if (dPiece[slot.arrival.x][slot.arrival.z] != expected || dSpecial[slot.arrival.x][slot.arrival.z] != 0)
			return false;
	}
	// Sole new native Town traversal: measured northern fence, two cells only.
	dPiece[56][10] = 0;
	dPiece[57][10] = 0;
	TownReady = true;
	return true;
}

bool TownRegionalExitsReady()
{
	std::lock_guard lock(StateMutex);
	return SurfaceCarrier() && State.region.IsTown() && TownReady;
}
bool SuppressTownRegionalExitScenery(int x, int z)
{
	return z == 10 && (x == 56 || x == 57) && TownRegionalExitsReady();
}

bool CreateSurfaceRegionMap()
{
	const auto snapshot = GetSurfaceWorldSnapshot();
	if (!SurfaceCarrier() || !snapshot || !snapshot->region)
		return false;
	ClearTownBorderEnvironmentForMap(); // Clear native Town presentation before carrier replacement.
	ResetTownSceneLogicAudit();
	for (int z = 0; z < surface::StorageSize; ++z) {
		for (int x = 0; x < surface::StorageSize; ++x) {
			const auto &cell = snapshot->region->At(x, z);
			dPiece[x][z] = cell.kind == surface::CellKind::Blocked ? 426 : cell.variation % 4;
			dSpecial[x][z] = 0;
		}
	}
	dminPosition = { surface::InteriorMin, surface::InteriorMin };
	dmaxPosition = { surface::InteriorMax + 1, surface::InteriorMax + 1 };
	std::lock_guard lock(StateMutex);
	if (ArrivalPending)
		ViewPosition = { Arrival.x, Arrival.z };
	TownReady = false;
	return true;
}

bool CheckSurfaceWorldExit()
{
    if (!SurfaceCarrier() || MyPlayer == nullptr || MyPlayer->_pmode != PM_STAND) return false;
    try {
        bool captureTown = false;
        {
            std::lock_guard lock(StateMutex);
            if (Pending) return false;
            const auto side = surface::ExitAt({ MyPlayer->position.tile.x, MyPlayer->position.tile.y });
            if (!side) { ExitLatch = false; return false; }
            if (ExitLatch || (State.region.IsTown() && !TownReady)) return false;
            captureTown = State.region.IsTown();
        }
        if (captureTown && !RefreshSurfaceTownVisualMap()) return false;
        PendingTransition staged;
        TransitionAbortGuard abort { staged };
        SurfaceWorldProspectiveContext sourceView;
        if (!CaptureLiveProspectiveContext(sourceView)) return false;
        {
            std::lock_guard lock(StateMutex);
            if (Pending || !SurfaceCarrier() || MyPlayer->_pmode != PM_STAND) return false;
            const auto side = surface::ExitAt({ MyPlayer->position.tile.x, MyPlayer->position.tile.y });
            if (!side || ExitLatch || (State.region.IsTown() && !TownReady) || !TownVisualOwner || !VisualSources) return false;
            const auto transition = surface::TransitionFrom(State.region, *side);
            if (!transition) return false;
            staged.transition = *transition;
            staged.before = State; staged.after = State;
            staged.sourceTile = { MyPlayer->position.tile.x, MyPlayer->position.tile.y };
            staged.beforeTownVisualMap = staged.afterTownVisualMap = TownVisualOwner;
            staged.sources = VisualSources;
            staged.beforeSnapshot = GetSurfaceWorldSnapshot();
            staged.preparedEpoch = GetSurfaceWorldEpoch();
            if (staged.preparedEpoch > std::numeric_limits<uint64_t>::max() - 2) return false;
            staged.after.region = transition->destination;
            if (!transition->destination.IsTown()
                && std::find(staged.after.visited.begin(), staged.after.visited.end(), transition->destination) == staged.after.visited.end())
                staged.after.visited.push_back(transition->destination);
            if (surface::ValidatePersistentState(staged.after) != surface::StateCodecError::None) return false;
        }
        // Both possible publications are allocated/preflighted BEFORE enqueue,
        // native save, player InitLevelChange, carrier mutation or FreeGameMem.
        auto targetView = sourceView;
        const auto arrival = staged.transition.destinationArrival;
        targetView.playerTile = targetView.nativeViewPosition = { arrival.x, arrival.z };
        targetView.mode = PM_STAND; targetView.walking = false;
        targetView.ticksPerFrame = targetView.numberOfFrames = 1;
        targetView.tickCounter = targetView.currentFrame = 0;
        auto after = BuildSnapshot(staged.transition.destination);
        DraftAbortGuard afterAbort { after };
        if (!staged.transition.destination.IsTown()) {
            if (!after || !CompleteRegionalVisualDraft(*after, staged.afterTownVisualMap, *staged.sources,
                    staged.preparedEpoch + 1, targetView, false)) return false;
        }
        staged.afterSnapshot = std::move(after);
        afterAbort.armed = false;
        if (!staged.transition.source.IsTown()) {
            if (!staged.beforeSnapshot || staged.beforeSnapshot->epoch != staged.preparedEpoch) return false;
            auto rollback = std::make_shared<SurfaceWorldSnapshot>(*staged.beforeSnapshot);
            DraftAbortGuard rollbackAbort { rollback };
            if (!CompleteRegionalVisualDraft(*rollback, staged.beforeTownVisualMap, *staged.sources,
                    staged.preparedEpoch + 2, sourceView, false)) return false;
            staged.beforeSnapshot = std::move(rollback);
            rollbackAbort.armed = false;
        }
        {
            std::lock_guard lock(StateMutex);
            if (Pending || !SurfaceCarrier() || MyPlayer->_pmode != PM_STAND
                || staged.preparedEpoch != GetSurfaceWorldEpoch() || staged.before.region != State.region
                || TownVisualOwner != staged.beforeTownVisualMap || VisualSources != staged.sources
                || MyPlayer->position.tile != Point { staged.sourceTile.x, staged.sourceTile.z }) return false;
            Pending = std::move(staged); abort.armed = false; ExitLatch = true;
        }
        StartNewLvl(*MyPlayer, WM_DIABSURFACE, 0);
        return HasPendingSurfaceTransition(); // SDL enqueue refusal cancels the untouched origin.
    } catch (const std::bad_alloc &) { return false; }
}

bool RepreflightSurfaceWorldVisualContext()
{
    // Main/load boundary only, after camera input/advance and before Draw.
    // The renderer may request but may never build or publish this geometry.
    if (!SurfaceCarrier()) return true;
    const auto original = GetSurfaceWorldSnapshot();
    if (!original) return true;
    surface::SurfaceHaloRequest required;
    if (!GetTownViewSurfaceHaloRepreflightRequest(required)) return true;
    try {
        SurfaceWorldProspectiveContext context;
        if (!CaptureLiveProspectiveContext(context)) return false;
        std::shared_ptr<const TownVisualPreparedSources> sources;
        uint64_t campaign;
        {
            std::lock_guard lock(StateMutex);
            if (Pending || original != GetSurfaceWorldSnapshot() || original->epoch != GetSurfaceWorldEpoch()
                || original->id != State.region || !VisualSources) return false;
            sources = VisualSources; campaign = CampaignId;
        }
        if (original->epoch == std::numeric_limits<uint64_t>::max()) return false;
        auto draft = std::make_shared<SurfaceWorldSnapshot>(*original);
        DraftAbortGuard abort { draft };
        if (!CompleteRegionalVisualDraft(*draft, original->townVisualMap, *sources,
                original->epoch + 1, context, false, &required)) return false;
        {
            std::lock_guard lock(StateMutex);
            if (Pending || campaign != CampaignId || original != GetSurfaceWorldSnapshot()
                || original->epoch != GetSurfaceWorldEpoch() || VisualSources != sources) return false;
            std::shared_ptr<const SurfaceWorldSnapshot> publication = std::move(draft);
            std::atomic_store_explicit(&Published, std::move(publication), std::memory_order_release);
            Epoch.store(original->epoch + 1, std::memory_order_release);
            abort.armed = false;
        }
        // Persistent state, visited, Fresh, Town capture, saves and preferences
        // are unchanged. Nature was rebound from retained ENV to the new epoch.
        return true;
    } catch (const std::bad_alloc &) { return false; }
}

bool HasPendingSurfaceTransition()
{
	std::lock_guard lock(StateMutex);
	return Pending.has_value();
}
bool CommitSurfaceTransition()
{
    std::lock_guard lock(StateMutex);
    if (!SurfaceCarrier() || !Pending || Pending->committedEpoch != 0
        || Pending->preparedEpoch != GetSurfaceWorldEpoch() || State.region != Pending->transition.source
        || (Pending->afterSnapshot && Pending->afterSnapshot->epoch != Pending->preparedEpoch + 1)) return false;
    // The native level save already committed. All target and rollback owners
    // were prepared before that transaction; publication itself cannot allocate.
    static_assert(std::is_nothrow_move_assignable_v<SurfaceWorldState>);
    State = std::move(Pending->after);
    TownVisualOwner = Pending->afterTownVisualMap; VisualSources = Pending->sources;
    std::atomic_store_explicit(&Published, Pending->afterSnapshot, std::memory_order_release);
    const uint64_t next = Pending->preparedEpoch + 1;
    Epoch.store(next, std::memory_order_release); Pending->committedEpoch = next;
    Arrival = Pending->transition.destinationArrival; ArrivalPending = true; TownReady = false;
    return true;
}
bool RollbackSurfaceTransition()
{
    std::optional<PendingTransition> finished;
    {
        std::lock_guard lock(StateMutex);
        if (!Pending || Pending->committedEpoch == 0 || Pending->committedEpoch != GetSurfaceWorldEpoch()
            || (Pending->beforeSnapshot && Pending->beforeSnapshot->epoch != Pending->committedEpoch + 1)) return false;
        finished.swap(Pending);
        State = std::move(finished->before);
        TownVisualOwner = finished->beforeTownVisualMap; VisualSources = finished->sources;
        std::atomic_store_explicit(&Published, finished->beforeSnapshot, std::memory_order_release);
        Epoch.store(finished->committedEpoch + 1, std::memory_order_release);
        Arrival = finished->sourceTile; ArrivalPending = true; TownReady = false; ExitLatch = true;
    }
    CancelTransitionDrafts(*finished); // Current/frame source protected by exact stamp.
    return true;
}
void FinishSurfaceTransition()
{
    std::optional<PendingTransition> finished;
    { std::lock_guard lock(StateMutex); finished.swap(Pending); }
    if (finished) CancelTransitionDrafts(*finished);
}
void CancelSurfaceTransition()
{
    std::optional<PendingTransition> cancelled;
    {
        std::lock_guard lock(StateMutex);
        // A committed transaction needs rollback; never discard its origin.
        if (Pending && Pending->committedEpoch == 0) cancelled.swap(Pending);
    }
    if (cancelled) CancelTransitionDrafts(*cancelled);
}

bool SurfaceArrivalPending()
{
	std::lock_guard lock(StateMutex);
	return ArrivalPending;
}
surface::Tile GetSurfaceArrival()
{
	std::lock_guard lock(StateMutex);
	return Arrival;
}
void CompleteSurfaceArrival()
{
	std::lock_guard lock(StateMutex);
	ArrivalPending = false;
}

} // namespace devilution
