#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "engine/point.hpp"
#include "levels/surface_region_geometry.hpp"
#include "levels/surface_region_halo.hpp"
#include "levels/surface_world_view.hpp"
#include "levels/surface_world_codec.hpp"
#include "levels/surface_town_visual.hpp"

namespace devilution {

using SurfaceWorldState = surface::PersistentState;

// Transient campaign identity: never serialized into worldregions-v1. Capturing
// state and identity under the same lock prevents a completed old save from
// clearing the Fresh flag of another NEW/load/region publication.
struct SurfaceCampaignSaveContext {
	SurfaceWorldState state;
	uint64_t campaignId = 0;
	uint64_t epoch = 0;
	bool freshCampaign = false;
	std::shared_ptr<const surface::TownVisualMap> townVisualMap;
	bool protectedArchive = false;
};
SurfaceCampaignSaveContext CaptureSurfaceCampaignSaveContext();
bool IsSurfaceFreshCampaign();
bool IsSurfaceCampaignSaveContextCurrent(const SurfaceCampaignSaveContext &context);
// Caller must have successfully committed the matching FULL staged save first.
bool CompleteSurfaceCampaignSave(const SurfaceCampaignSaveContext &context);

// One immutable publication per level identity. Consumers retain this shared
// owner for the entire frame, including actor support, color, depth and picks.
struct SurfaceWorldSnapshot {
	surface::RegionId id;
	uint32_t seed = surface::VisualSeed;
	uint32_t generatorRevision = surface::GeneratorRevision;
	uint64_t epoch = 0;
	uint32_t geometryRevision = surface::GeometryRevision;
	surface::Tile worldOffset;
	std::shared_ptr<const surface::Region> region;
	std::shared_ptr<const surface::RegionGeometry> geometry;
	std::shared_ptr<const surface::SurfaceHaloGeometry> halo;
	std::shared_ptr<const surface::TownVisualMap> townVisualMap;
};

SurfaceWorldState GetSurfaceWorldState();
std::shared_ptr<const SurfaceWorldSnapshot> GetSurfaceWorldSnapshot(); // null in native Town
uint64_t GetSurfaceWorldEpoch();
bool IsSurfaceRegionActive();
bool IsSurfaceWorldSupported();
void ResetSurfaceWorldState(bool protectedArchive = false);
// Includes an old Surface archive retained by deferred NEW, even before this
// session has visited a region. Pure legacy campaigns keep the old player path.
bool IsSurfaceCampaignProtected();
// Allocation-free identity for a temporary native level save; unlike FULL it
// need not copy the persistent visited vector while an I/O operation is active.
struct SurfaceLevelSaveContext {
	uint64_t campaignId = 0, epoch = 0;
	bool freshCampaign = false, protectedArchive = false;
	std::shared_ptr<const surface::TownVisualMap> townVisualMap;
	uint64_t townCaptureEpoch = 0;
};
SurfaceLevelSaveContext CaptureSurfaceLevelSaveContext();
bool IsSurfaceLevelSaveContextCurrent(const SurfaceLevelSaveContext &context);
// Undo only a refresh belonging to this failed, uncommitted level save.
void RestoreFailedSurfaceLevelSave(const SurfaceLevelSaveContext &before, const SurfaceLevelSaveContext &after);

class PreparedSurfaceWorldRestore {
public:
	~PreparedSurfaceWorldRestore();
	PreparedSurfaceWorldRestore(PreparedSurfaceWorldRestore &&) noexcept;
	PreparedSurfaceWorldRestore &operator=(PreparedSurfaceWorldRestore &&) noexcept;
	PreparedSurfaceWorldRestore(const PreparedSurfaceWorldRestore &) = delete;
	PreparedSurfaceWorldRestore &operator=(const PreparedSurfaceWorldRestore &) = delete;
private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	explicit PreparedSurfaceWorldRestore(std::unique_ptr<Impl> impl);
	friend std::optional<PreparedSurfaceWorldRestore> PrepareSurfaceWorldRestore(const SurfaceWorldState &, std::shared_ptr<const surface::TownVisualMap>, bool, const SurfaceWorldProspectiveContext *, bool);
	friend void CommitPreparedSurfaceWorldRestore(PreparedSurfaceWorldRestore &&) noexcept;
};
// All validation/I/O/allocation precedes returning this move-only token. It
// retains StateMutex through the synchronous archive cleanup, whose call graph
// MUST NOT call World getters. Destruction without Commit releases it unchanged.
std::optional<PreparedSurfaceWorldRestore> PrepareSurfaceWorldRestore(const SurfaceWorldState &state,
    std::shared_ptr<const surface::TownVisualMap> townVisualMap, bool protectedArchive = true,
    const SurfaceWorldProspectiveContext *prospective = nullptr, bool firstFlagEntry = false);
// Only no-throw moves/publication; no asset I/O, generation, allocation or recheck.
void CommitPreparedSurfaceWorldRestore(PreparedSurfaceWorldRestore &&prepared) noexcept;
// Validate and build completely before replacing state; no camera/UI preference reset.
bool RestoreSurfaceWorldState(const SurfaceWorldState &state,
    std::shared_ptr<const surface::TownVisualMap> townVisualMap = {}, bool protectedArchive = false,
    const SurfaceWorldProspectiveContext *prospective = nullptr, bool firstFlagEntry = false);
// Historical retained record in native Town/C1, not a claim of current native
// renderer admission. Refresh is required before Town FULL save/regional exit.
// Never interpreted as regional simulation data.
std::shared_ptr<const surface::TownVisualMap> GetSurfaceTownVisualMap();
// Only a live, patched native Town can refresh this immutable owner. No RNG.
bool RefreshSurfaceTownVisualMap();
// Native Town loader AFTER North cuts + actors + NatureCommit; outside mutex.
// Preload only, does not create a Surface owner or change pure legacy saves.
bool PrepareSurfaceTownVisualCapture();
// Exact barycentric support of the published triangles, in native height units.
float SurfaceGroundHeight(float localX, float localZ);
// Adds only R2 authored swept obstacle semantics. Native occupancy/corner policy
// remains the caller's authority. Inputs are literal local native labels.
bool CanTraverseSurfaceStep(Point from, Point to);

// Native map-loader hooks. Town is never regenerated by the regional generator.
bool PrepareTownRegionalExits();
bool TownRegionalExitsReady();
bool SuppressTownRegionalExitScenery(int x, int z);
bool CreateSurfaceRegionMap();

// Trigger preflights target allocation before scheduling the native load event.
bool CheckSurfaceWorldExit();
// Main/load boundary: honor renderer resize/F4/frustum request without state/save mutation.
bool RepreflightSurfaceWorldVisualContext();
bool HasPendingSurfaceTransition();
// Old level must be saved successfully before Commit; retain rollback until Finish.
bool CommitSurfaceTransition();
bool RollbackSurfaceTransition();
void FinishSurfaceTransition();
void CancelSurfaceTransition();
bool SurfaceArrivalPending();
surface::Tile GetSurfaceArrival();
void CompleteSurfaceArrival();

} // namespace devilution
