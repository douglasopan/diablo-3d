#pragma once

// PRIVATE SOURCE-ONLY CANDIDATE. This header has not been compiled or run.
// It describes existing immutable frame references and native MIN layers.
// It does not construct a Frame/Scene/map, decode pixels, choose kappa/UV,
// publish GPU work, change picking/collision, admit a budget or mutate a cache.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "engine/render/cathedral/cathedral_frame.hpp"
#include "native-wall-composition.hpp"

namespace devilution::cathedral::wall_face_plan {

inline constexpr std::uint32_t SchemaVersion = 1;

enum class RawLayoutRole : std::uint8_t { UpperColumn, FullColumn, NativeBasePair };

/** Raw artwork key, not a GPU texture or current repeated-band key.
 * Pixel coverage is separate from color. Lighting/TRN, native masks and draw
 * policy are not baked into this key or the estimated two-plane payload.
 * nativeIsFloor changes the base raster roles; epoch/words retain provenance.
 * BasePair uses column2 as an explicit both-columns marker, never a MIN index.
 */
struct RawKey {
	std::uint64_t epoch = 0;
	std::uint16_t piece = 0;
	std::uint8_t column = 0;
	NativeTextureAxis axis = NativeTextureAxis::Horizontal;
	RawLayoutRole role = RawLayoutRole::UpperColumn;
	unsigned activeMicroTileLength = 10;
	bool nativeIsFloor = false;
	std::array<std::uint16_t, 16> words {};
	friend bool operator==(const RawKey &, const RawKey &) = default;
	friend bool operator<(const RawKey &a, const RawKey &b)
	{
		return std::tie(a.epoch, a.piece, a.column, a.axis, a.role, a.activeMicroTileLength, a.nativeIsFloor, a.words)
		    < std::tie(b.epoch, b.piece, b.column, b.axis, b.role, b.activeMicroTileLength, b.nativeIsFloor, b.words);
	}
};

/** Optional scalar metadata from an ALREADY prepared current masonry band.
 * This is never original full-column pixel evidence. No getter/decoder is
 * called by Build. Coordinates are absent because current bands share artwork
 * by piece/axis; all source-cell echoes remain on the existing frame refs.
 */
struct CurrentBandMetadata {
	std::uint64_t epoch = 0;
	std::uint16_t requestedPiece = 0, effectivePiece = 0;
	NativeTextureAxis axis = NativeTextureAxis::Horizontal;
	std::uint8_t column = 0;
	unsigned sourceMicro = 0;
	std::uint16_t sourceBlock = 0;
	int width = 0, height = 0;
	bool repeat = false, donor = false;
};

struct Context {
	// Zero selects snapshot.gameRevision; a supplied epoch must equal it.
	std::uint64_t epoch = 0;
	// Actual caller-observed native MicroTileLen; never inferred from zeros.
	unsigned activeMicroTileLength = 10;
	std::span<const CurrentBandMetadata> currentBands;
	// Existing host keys only. Build never fills, removes or mutates this cache.
	std::span<const RawKey> residentRawKeys;
};

struct GroupKey {
	int x = 0, z = 0;
	std::uint16_t piece = 0;
	std::uint8_t column = 0;
	NativeTextureAxis axis = NativeTextureAxis::Horizontal;
	friend bool operator<(const GroupKey &a, const GroupKey &b)
	{
		return std::tie(a.z, a.x, a.piece, a.column, a.axis) < std::tie(b.z, b.x, b.piece, b.column, b.axis);
	}
};

/** Every existing masonry triangle is retained, including backs/caps, joins,
 * door-frame refs and conservative cutaways. Full triangle copies preserve
 * physical and visual positions/UVs, native binding, pickOwner, light, policy,
 * transparent flag and64-bit instance ID. sourceInstanceId and every source
 * are retained in nativeSurface; nativeSourceCells have matching source order.
 */
struct Reference {
	std::size_t triangleIndex = 0, surfaceRecordIndex = 0;
	FrameTriangle triangle {};
	NativeSurfaceRecord nativeSurface;
	std::vector<Cell> nativeSourceCells;
	bool frameSelected = true;
	std::optional<bool> gpuSubmitted, pixelVisible; // Unknown, not false.
};

struct RepresentativePair {
	std::array<std::size_t, 2> referenceIndices {}, triangleIndices {};
	std::uint64_t instanceId = 0, sourceInstanceId = 0;
	ModuleKind module = ModuleKind::Floor;
	bool originalUnfragmented = false, wallFamily = false;
	bool followsWallPrimaryAxis = false, bindingNonApproximate = false;
	// Only a rectangular physical face pair was selected. No artistic-facing,
	// pixel-visible, native3D-depth, GPU-submission or admission claim follows.
};

struct Group {
	GroupKey key;
	Cell nativeSourceCell; // light is native source light, not triangle.light.
	std::size_t cellPlanIndex = 0;
	std::vector<Reference> references;
	std::optional<RepresentativePair> representative;
	std::optional<CurrentBandMetadata> currentBand;
	bool ownUpperEmpty = true, ownFullColumnEmpty = true;
	std::size_t upperZeroWords = 0, upperPresentWords = 0;
	bool mixedPolicy = false, mixedSourceContext = false;
	bool containsCutaway = false, containsDoorFrame = false;
	bool containsFragments = false;
	// One descriptor per native column, even when its upper is explicitly empty.
	// This is not permission to emit artwork for an empty original column.
	bool representativeColumnCandidate = false;
	bool runtimeAdmissionGranted = false;
	std::optional<bool> gpuSubmitted, pixelVisible;
};

struct PayloadEstimate {
	std::uint64_t pixels = 0, indexBytes = 0, coverageBytes = 0, totalBytes = 0;
};

struct PlannedRawKey {
	RawKey key;
	wall_composition::AtlasDescriptor canvas;
	PayloadEstimate hostRawEstimate;
	bool alreadyResident = false;
};

struct Counts {
	std::size_t selectedFrameTriangles = 0, masonryReferences = 0;
	std::size_t sourceCells = 0, sourceColumnGroups = 0;
	std::size_t upperEmptyGroups = 0, fullEmptyGroups = 0;
	std::size_t upperZeroWords = 0, upperPresentWords = 0;
	std::size_t representativePairs = 0, groupsWithoutPair = 0;
	std::size_t mixedPolicyGroups = 0, mixedSourceContextGroups = 0;
	std::size_t cutawayReferences = 0, fragmentReferences = 0;
	std::size_t suppliedCurrentBands = 0, knownCurrentBandGroups = 0, currentDonorGroups = 0;
	std::size_t uniqueRawRoleKeys = 0, coldNewKeys = 0, existingKeyHits = 0;
	// Conditional second-pass estimate: same metadata and all planned keys kept.
	std::size_t unchangedWarmNewKeys = 0, unchangedWarmKeyHits = 0;
	std::array<std::size_t, 3> roleKeyCounts {};
	std::array<std::uint64_t, 3> roleHostRawBytes {};
	std::uint64_t allRolesHostRawBytes = 0, coldNewHostRawBytes = 0;
};

struct Plan {
	std::uint32_t schemaVersion = SchemaVersion;
	std::uint64_t epoch = 0, geometryRevision = 0, presentationSignature = 0;
	unsigned activeMicroTileLength = 10;
	bool frameRequiresNativeFallback = false;
	std::uint32_t frameFallbackReasons = 0;
	bool nativeFullColumnPixelsCaptured = false;
	std::vector<wall_composition::CellPlan> nativeCellPlans;
	std::vector<Group> groups;
	// Upper/full/base are alternative semantic layouts. Summed estimates do not
	// prescribe keeping all layouts simultaneously or raise an existing budget.
	std::vector<PlannedRawKey> rawKeys;
	Counts counts;
	std::optional<bool> gpuSubmitted, pixelVisible;
};

namespace detail {

inline void Require(bool condition, const char *message)
{
	if (!condition) throw std::invalid_argument(message);
}
inline bool Active(int x, int z) { return x >= ActiveMin && x < ActiveMax && z >= ActiveMin && z < ActiveMax; }
inline bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline bool Same(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline float At(Vec3 v, unsigned a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
inline Vec3 Normal(const FrameTriangle &t)
{
	const Vec3 a = t.vertices[0].position, b = t.vertices[1].position, c = t.vertices[2].position;
	const Vec3 ab { b.x - a.x, b.y - a.y, b.z - a.z }, ac { c.x - a.x, c.y - a.y, c.z - a.z };
	return { ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x };
}
inline bool SameSource(const NativeSurfaceSource &a, const NativeSurfaceSource &b)
{
	return a.x == b.x && a.z == b.z && a.piece == b.piece && a.properties == b.properties
	    && a.transparencyGroup == b.transparencyGroup && a.partialMask == b.partialMask
	    && a.groupActive == b.groupActive && a.blendActive == b.blendActive && a.microMasksKnown == b.microMasksKnown;
}
inline void CheckVertices(const std::array<Vertex, 3> &vertices)
{
	for (const auto &v : vertices) Require(Finite(v.position) && std::isfinite(v.u) && std::isfinite(v.v), "Wall face plan nonfinite vertex/UV");
}

inline void ValidateSnapshot(const Snapshot &s)
{
	Require(s.level == 1 && !s.hellfire && s.levelType == 1 && !s.setLevel && s.setLevelId == 0, "Wall face plan requires retail Cathedral1");
	Require(s.gameRevision != 0 && s.minX == ActiveMin && s.minZ == ActiveMin && s.maxX == ActiveMax && s.maxZ == ActiveMax, "Wall face plan invalid epoch/active bounds");
	Require(s.micros.size() <= 1379, "Wall face plan invalid MIN table size");
	for (const auto &c : s.cells) Require(c.piece < 1379 && c.light <= 15 && c.object != -128, "Wall face plan invalid native cell");
	for (auto value : s.mega) Require(value <= 206, "Wall face plan invalid native mega");
	std::set<int> slots;
	std::set<std::pair<int, int>> positions;
	for (const auto &d : s.doors) {
		Require(Active(d.x, d.z) && d.slot >= 0 && d.slot < 127 && slots.insert(d.slot).second && positions.emplace(d.x, d.z).second
		        && static_cast<unsigned>(d.orientation) <= 1 && static_cast<unsigned>(d.state) <= 2, "Wall face plan invalid native door");
		Require(std::abs(static_cast<int>(s.At(d.x, d.z).object)) == d.slot + 1, "Wall face plan native door occupancy mismatch");
	}
	for (const auto &t : s.triggers) Require(Active(t.x, t.z), "Wall face plan invalid native trigger");
}

inline void ValidateSource(const Snapshot &s, const NativeSurfaceSource &source)
{
	Require(source.x >= 0 && source.z >= 0 && source.x < GridSize && source.z < GridSize, "Wall face plan source outside native grid");
	const Cell &c = s.At(source.x, source.z);
	Require(source.piece == c.piece && source.properties == c.properties && source.transparencyGroup == c.transparency
	        && source.groupActive == s.activeTransparency[c.transparency]
	        && source.blendActive == (source.groupActive && (c.properties & 8U) != 0), "Wall face plan source context mismatch");
	const std::uint8_t potential = static_cast<std::uint8_t>(c.properties & 48U);
	bool known = false;
	std::uint8_t mask = 0;
	if (c.piece < s.micros.size()) {
		const auto &w = s.micros[c.piece];
		const unsigned lt = (w[0] >> 12) & 7U, rt = (w[1] >> 12) & 7U;
		known = lt <= 5 && rt <= 5;
		if (known) {
			if (w[0] != 0 && (lt == 1 || lt == 4)) mask = static_cast<std::uint8_t>(mask | (potential & 16U));
			if (w[1] != 0 && (rt == 1 || rt == 5)) mask = static_cast<std::uint8_t>(mask | (potential & 32U));
		}
	}
	if (!known) mask = potential;
	Require(source.microMasksKnown == known && source.partialMask == mask, "Wall face plan source native mask mismatch");
}

inline void ValidateSurface(const Snapshot &s, const NativeSurfaceRecord &r)
{
	Require(r.instanceId != 0 && r.sourceInstanceId != 0 && r.provenanceResolved && !r.sources.empty()
	        && static_cast<unsigned>(r.module) < static_cast<unsigned>(ModuleKind::Count)
	        && static_cast<unsigned>(r.sourceModule) < static_cast<unsigned>(ModuleKind::Count)
	        && static_cast<unsigned>(r.policy) <= static_cast<unsigned>(FrameSurfacePolicy::ConservativeNativeCutaway), "Wall face plan missing/unresolved native record");
	const bool fragment = r.module == ModuleKind::WallFragment || r.module == ModuleKind::JoinFragment;
	Require(fragment ? r.instanceId != r.sourceInstanceId : r.instanceId == r.sourceInstanceId && r.module == r.sourceModule, "Wall face plan source identity mismatch");
	Require(r.instanceId >> 48 == static_cast<std::uint64_t>(r.module) + 1
	        && r.sourceInstanceId >> 48 == static_cast<std::uint64_t>(r.sourceModule) + 1
	        && ((r.sourceInstanceId >> 32) & 65535U) == 0, "Wall face plan identity namespace mismatch");
	if (fragment) Require(r.module == ModuleKind::WallFragment ? r.sourceModule == ModuleKind::Wall
	                                                            : r.sourceModule >= ModuleKind::Corner && r.sourceModule <= ModuleKind::Junction4, "Wall face plan fragment family mismatch");
	if (fragment) Require((r.instanceId & 0xFFFFFFFFULL) == (r.sourceInstanceId & 0xFFFFFFFFULL)
	        && ((r.instanceId >> 40) & 255U) == static_cast<std::uint64_t>(r.sourceModule), "Wall face plan fragment source bits mismatch");
	std::set<std::pair<int, int>> cells;
	FrameSurfacePolicy derived = FrameSurfacePolicy::Opaque;
	const bool nativeWall = r.sourceModule == ModuleKind::Wall
	    || (r.sourceModule >= ModuleKind::Corner && r.sourceModule <= ModuleKind::Junction4)
	    || r.module == ModuleKind::ArchFrame || r.module == ModuleKind::Pillar || r.module == ModuleKind::Fence;
	for (const auto &source : r.sources) {
		ValidateSource(s, source);
		Require(cells.emplace(source.x, source.z).second, "Wall face plan duplicate native source");
		if (!nativeWall || !source.blendActive) continue;
		if (source.partialMask != 0) derived = FrameSurfacePolicy::ConservativeNativeCutaway;
		else if (derived == FrameSurfacePolicy::Opaque) derived = FrameSurfacePolicy::NativePaletteBlend;
	}
	Require(derived == r.policy, "Wall face plan native surface policy mismatch");
}

inline void ValidateTriangle(const Snapshot &s, const FrameTriangle &t, const NativeSurfaceRecord &r)
{
	Require(t.instanceId == r.instanceId && t.module == r.module && t.policy == r.policy
	        && t.transparent == (t.policy != FrameSurfacePolicy::Opaque) && t.light <= 15, "Wall face plan triangle/record mismatch");
	Require(static_cast<unsigned>(t.pick.kind) <= static_cast<unsigned>(PickKind::Trigger) && Active(t.pick.x, t.pick.z), "Wall face plan invalid pickOwner");
	const int ownerX = static_cast<int>((r.sourceInstanceId >> 8) & 65535U) - 128;
	const int ownerZ = static_cast<int>((r.sourceInstanceId >> 24) & 255U) - 128;
	Require(Active(ownerX, ownerZ) && t.light == s.At(ownerX, ownerZ).light && (s.At(ownerX, ownerZ).flags & 2U) != 0,
	    "Wall face plan physical owner light/selection mismatch");
	if (t.pick.kind != PickKind::Trigger) Require(t.pick.x == ownerX && t.pick.z == ownerZ, "Wall face plan pick differs from native owner");
	if (t.pick.kind == PickKind::Ground || t.pick.kind == PickKind::Architecture) Require(t.pick.nativeSlot == -1, "Wall face plan architecture slot invalid");
	if (t.pick.kind == PickKind::Object) Require(t.module == ModuleKind::DoorLeaf
	        && std::any_of(s.doors.begin(), s.doors.end(), [&](const Door &d) {
		        return d.slot == t.pick.nativeSlot && d.x == t.pick.x && d.z == t.pick.z && d.selectable;
	        }), "Wall face plan object pick differs from native door");
	if (t.pick.kind == PickKind::Trigger) {
		Require((t.module == ModuleKind::StairsUp || t.module == ModuleKind::StairsDown)
		        && t.pick.nativeSlot >= 0 && static_cast<std::size_t>(t.pick.nativeSlot) < s.triggers.size(), "Wall face plan trigger slot invalid");
		const auto &trigger = s.triggers[static_cast<std::size_t>(t.pick.nativeSlot)];
		Require(trigger.x == t.pick.x && trigger.z == t.pick.z, "Wall face plan trigger target mismatch");
	}
	CheckVertices(t.vertices);
	const Vec3 n = Normal(t);
	Require(Finite(n) && (n.x != 0 || n.y != 0 || n.z != 0), "Wall face plan degenerate physical triangle");
}

inline bool RectanglePair(const Reference &a, const Reference &b, NativeTextureAxis axis)
{
	const auto &x = a.triangle, &y = b.triangle;
	if (x.instanceId != y.instanceId || x.module != y.module || x.pick != y.pick || x.light != y.light
	    || x.policy != y.policy || a.surfaceRecordIndex != b.surfaceRecordIndex
	    || x.nativeTexture.approximate != y.nativeTexture.approximate
	    || x.nativeTexture.operationPass != y.nativeTexture.operationPass) return false;
	const unsigned normalAxis = axis == NativeTextureAxis::AlongX ? 2U : 0U, tangentAxis = 2U - normalAxis;
	const Vec3 nx = Normal(x), ny = Normal(y);
	if (nx.y != 0 || ny.y != 0 || At(nx, tangentAxis) != 0 || At(ny, tangentAxis) != 0
	    || At(nx, normalAxis) * At(ny, normalAxis) <= 0) return false;
	std::vector<Vec3> unique;
	std::vector<Vec3> commonVertices;
	unsigned common = 0;
	for (const auto &v : x.vertices) unique.push_back(v.position);
	for (const auto &v : y.vertices) {
		if (std::any_of(unique.begin(), unique.end(), [&](Vec3 q) { return Same(q, v.position); })) {
			++common; commonVertices.push_back(v.position);
		}
		else unique.push_back(v.position);
	}
	if (common != 2 || unique.size() != 4) return false;
	// Complementary rectangle triangles share the diagonal, not a boundary edge.
	// Four corners alone would also accept two overlapping same-side triangles.
	if (At(commonVertices[0], tangentAxis) == At(commonVertices[1], tangentAxis)
	    || commonVertices[0].y == commonVertices[1].y) return false;
	const float plane = At(unique[0], normalAxis);
	float lo = At(unique[0], tangentAxis), hi = lo, lowY = unique[0].y, highY = lowY;
	for (auto v : unique) {
		if (At(v, normalAxis) != plane) return false;
		lo = std::min(lo, At(v, tangentAxis)); hi = std::max(hi, At(v, tangentAxis));
		lowY = std::min(lowY, v.y); highY = std::max(highY, v.y);
	}
	if (lo == hi || lowY == highY) return false;
	return std::all_of(unique.begin(), unique.end(), [&](Vec3 v) {
		return (At(v, tangentAxis) == lo || At(v, tangentAxis) == hi) && (v.y == lowY || v.y == highY);
	});
}

inline bool PrimaryAxis(const Reference &ref, NativeTextureAxis axis, const std::map<std::uint64_t, Bounds> &bounds)
{
	if (ref.nativeSurface.sourceModule != ModuleKind::Wall) return false;
	const auto &b = bounds.at(ref.triangle.instanceId);
	const float x = b.max.x - b.min.x, z = b.max.z - b.min.z;
	return axis == NativeTextureAxis::AlongX ? x > z : z > x;
}
inline unsigned FamilyRank(ModuleKind source)
{
	if (source == ModuleKind::Wall) return 0;
	if (source >= ModuleKind::Corner && source <= ModuleKind::Junction4) return 1;
	return 2; // DoorFrame and unrelated families never qualify as representatives.
}
inline void SelectRepresentative(Group &g, const std::map<std::uint64_t, Bounds> &bounds)
{
	using Rank = std::tuple<unsigned, unsigned, unsigned, unsigned, std::uint64_t, std::size_t, std::size_t>;
	std::optional<Rank> best;
	for (std::size_t i = 0; i < g.references.size(); ++i) {
		const auto &a = g.references[i];
		const unsigned family = FamilyRank(a.nativeSurface.sourceModule);
		if (family == 2) continue;
		const bool original = a.triangle.instanceId == a.nativeSurface.sourceInstanceId;
		const bool primary = PrimaryAxis(a, g.key.axis, bounds);
		for (std::size_t j = i + 1; j < g.references.size(); ++j) {
			const auto &b = g.references[j];
			if (!RectanglePair(a, b, g.key.axis)) continue;
			const Rank rank { family, original ? 0U : 1U, primary ? 0U : 1U,
				a.triangle.nativeTexture.approximate ? 1U : 0U, a.triangle.instanceId, a.triangleIndex, b.triangleIndex };
			if (best && !(rank < *best)) continue;
			best = rank;
			g.representative = RepresentativePair { { i, j }, { a.triangleIndex, b.triangleIndex }, a.triangle.instanceId,
				a.nativeSurface.sourceInstanceId, a.triangle.module, original, family == 0, primary, !a.triangle.nativeTexture.approximate };
		}
	}
	g.representativeColumnCandidate = g.representative.has_value();
}

inline PayloadEstimate Estimate(const wall_composition::AtlasDescriptor &canvas)
{
	Require(canvas.width >= 0 && canvas.height >= 0, "Wall face plan negative canvas");
	const auto pixels = static_cast<std::uint64_t>(canvas.width) * static_cast<std::uint64_t>(canvas.height);
	return { pixels, pixels, pixels, pixels * 2 }; // Two uint8 planes, no decoded opaque-area claim.
}

} // namespace detail

/** Build a declarative plan over accepted read-only inputs. No engine routine,
 * callback, pixel decode/composition or cache writer is called. NativeCellPlans
 * use only BuildCellPlan's metadata path. Source-only validation throws explicit
 * invalid_argument; no assert/NDEBUG-based success gate exists in this header.
 * Standard container allocation failure propagates before any external action.
 */
inline Plan Build(const Snapshot &snapshot, const PilotFrame &frame, Context context = {})
{
	using detail::Require;
	detail::ValidateSnapshot(snapshot);
	const std::uint64_t epoch = context.epoch == 0 ? snapshot.gameRevision : context.epoch;
	Require(epoch == snapshot.gameRevision && frame.gameRevision == snapshot.gameRevision && frame.geometryRevision != 0
	        && frame.regions > 0 && frame.regions <= 9 && context.activeMicroTileLength >= 2 && context.activeMicroTileLength <= 16
	        && (context.activeMicroTileLength & 1U) == 0, "Wall face plan frame/context mismatch");
	std::map<std::uint64_t, std::size_t> records;
	for (std::size_t i = 0; i < frame.nativeSurfaces.size(); ++i) {
		detail::ValidateSurface(snapshot, frame.nativeSurfaces[i]);
		Require(records.emplace(frame.nativeSurfaces[i].instanceId, i).second, "Wall face plan duplicate native record");
	}
	using BandKey = std::tuple<std::uint16_t, NativeTextureAxis, std::uint8_t>;
	std::map<BandKey, CurrentBandMetadata> bands;
	for (const auto &band : context.currentBands) {
		Require(band.epoch == epoch && (band.axis == NativeTextureAxis::AlongX || band.axis == NativeTextureAxis::AlongZ)
		        && band.column == (band.axis == NativeTextureAxis::AlongX ? 1 : 0)
		        && band.requestedPiece < snapshot.micros.size() && band.effectivePiece < snapshot.micros.size()
		        && band.sourceMicro >= 2 && band.sourceMicro < context.activeMicroTileLength && (band.sourceMicro & 1U) == band.column
		        && snapshot.micros[band.effectivePiece][band.sourceMicro] == band.sourceBlock
		        && band.sourceBlock != 0 && ((band.sourceBlock >> 12) & 7U) == 0 && (band.sourceBlock & 0x0FFFU) != 0
		        && band.width == 32 && band.height == 16 && band.repeat
		        && (!band.donor || band.effectivePiece == (band.axis == NativeTextureAxis::AlongX ? 4 : 0))
		        && band.donor == (band.requestedPiece != band.effectivePiece), "Wall face plan invalid prepared-current band metadata");
		Require(bands.emplace(BandKey { band.requestedPiece, band.axis, band.column }, band).second, "Wall face plan duplicate prepared-current band");
	}
	Plan out;
	out.epoch = epoch; out.geometryRevision = frame.geometryRevision; out.presentationSignature = frame.presentationSignature;
	out.activeMicroTileLength = context.activeMicroTileLength;
	out.frameRequiresNativeFallback = frame.requiresNativeFallback; out.frameFallbackReasons = frame.fallbackReasons;
	out.counts.selectedFrameTriangles = frame.triangles.size(); out.counts.suppliedCurrentBands = bands.size();
	std::map<GroupKey, Group> groups;
	std::map<std::pair<int, int>, std::size_t> cells;
	std::map<std::uint64_t, Bounds> physicalBounds;
	std::set<std::uint64_t> usedRecords;
	for (std::size_t i = 0; i < frame.triangles.size(); ++i) {
		const auto &triangle = frame.triangles[i];
		const auto found = records.find(triangle.instanceId);
		Require(found != records.end(), "Wall face plan triangle missing native record");
		const auto &surface = frame.nativeSurfaces[found->second];
		usedRecords.insert(surface.instanceId);
		detail::ValidateTriangle(snapshot, triangle, surface);
		for (const auto &vertex : triangle.vertices) {
			auto [entry, fresh] = physicalBounds.emplace(triangle.instanceId, Bounds { vertex.position, vertex.position });
			if (fresh) continue;
			auto &b = entry->second;
			b.min.x = std::min(b.min.x, vertex.position.x); b.max.x = std::max(b.max.x, vertex.position.x);
			b.min.y = std::min(b.min.y, vertex.position.y); b.max.y = std::max(b.max.y, vertex.position.y);
			b.min.z = std::min(b.min.z, vertex.position.z); b.max.z = std::max(b.max.z, vertex.position.z);
		}
		const auto &binding = triangle.nativeTexture;
		Require(static_cast<unsigned>(binding.kind) <= static_cast<unsigned>(NativeTextureKind::DoorWood), "Wall face plan unknown native binding kind");
		if (binding.kind != NativeTextureKind::Masonry) continue;
		detail::CheckVertices(triangle.textureVertices);
		Require((binding.axis == NativeTextureAxis::AlongX || binding.axis == NativeTextureAxis::AlongZ)
		        && binding.column == (binding.axis == NativeTextureAxis::AlongX ? 1 : 0) && binding.nativeSlot == -1
		        && binding.x >= 0 && binding.z >= 0 && binding.x < GridSize && binding.z < GridSize,
		    "Wall face plan invalid masonry axis/column/source");
		const auto source = std::find_if(surface.sources.begin(), surface.sources.end(), [&](const NativeSurfaceSource &s) { return s.x == binding.x && s.z == binding.z && s.piece == binding.piece; });
		Require(source != surface.sources.end() && snapshot.At(binding.x, binding.z).piece == binding.piece
		        && binding.piece < snapshot.micros.size(), "Wall face plan binding source absent or MIN table missing");
		const GroupKey key { binding.x, binding.z, binding.piece, binding.column, binding.axis };
		auto [entry, fresh] = groups.try_emplace(key);
		auto &g = entry->second;
		if (fresh) {
			g.key = key; g.nativeSourceCell = snapshot.At(binding.x, binding.z);
			const auto cellKey = std::make_pair(binding.x, binding.z);
			auto [cell, firstCell] = cells.emplace(cellKey, out.nativeCellPlans.size());
			if (firstCell) {
				wall_composition::SourceCellMetadata metadata;
				metadata.sourceX = binding.x; metadata.sourceZ = binding.z; metadata.piece = binding.piece;
				metadata.words = snapshot.micros[binding.piece]; metadata.activeMicroTileLength = context.activeMicroTileLength;
				metadata.sol = g.nativeSourceCell.properties; metadata.transparencyGroup = g.nativeSourceCell.transparency;
				metadata.groupActive = snapshot.activeTransparency[g.nativeSourceCell.transparency];
				out.nativeCellPlans.push_back(wall_composition::BuildCellPlan(metadata));
			}
			g.cellPlanIndex = cell->second;
			const auto &column = out.nativeCellPlans[g.cellPlanIndex].columns[binding.column];
			g.ownUpperEmpty = column.noPresentUpperWords; g.ownFullColumnEmpty = column.noPresentWords;
			for (const auto &layer : column.layers) if (layer.source.rowFromBottom != 0) {
				if (layer.source.present) ++g.upperPresentWords; else ++g.upperZeroWords;
			}
			const auto current = bands.find(BandKey { binding.piece, binding.axis, binding.column });
			if (current != bands.end()) g.currentBand = current->second;
		} else Require(g.nativeSourceCell == snapshot.At(binding.x, binding.z), "Wall face plan inconsistent group source cell");
		Reference reference;
		reference.triangleIndex = i; reference.surfaceRecordIndex = found->second;
		reference.triangle = triangle; reference.nativeSurface = surface;
		for (const auto &s : surface.sources) reference.nativeSourceCells.push_back(snapshot.At(s.x, s.z));
		if (!g.references.empty()) {
			g.mixedPolicy = g.mixedPolicy || g.references[0].triangle.policy != triangle.policy;
			const auto &first = g.references[0].nativeSurface.sources;
			g.mixedSourceContext = g.mixedSourceContext || !std::is_permutation(first.begin(), first.end(), surface.sources.begin(), surface.sources.end(), detail::SameSource);
		}
		g.containsCutaway = g.containsCutaway || triangle.policy == FrameSurfacePolicy::ConservativeNativeCutaway;
		g.containsDoorFrame = g.containsDoorFrame || triangle.module == ModuleKind::DoorFrame;
		const bool fragment = triangle.module == ModuleKind::WallFragment || triangle.module == ModuleKind::JoinFragment;
		g.containsFragments = g.containsFragments || fragment;
		g.references.push_back(std::move(reference));
		++out.counts.masonryReferences;
		out.counts.cutawayReferences += triangle.policy == FrameSurfacePolicy::ConservativeNativeCutaway;
		out.counts.fragmentReferences += fragment;
	}
	Require(usedRecords.size() == records.size(), "Wall face plan orphan native record");
	std::map<RawKey, wall_composition::AtlasDescriptor> raw;
	for (auto &[key, group] : groups) {
		detail::SelectRepresentative(group, physicalBounds);
		const auto &cell = out.nativeCellPlans[group.cellPlanIndex];
		const auto &column = cell.columns[key.column];
		const auto add = [&](RawLayoutRole role, std::uint8_t col, NativeTextureAxis axis, const wall_composition::AtlasDescriptor &canvas) {
			raw.emplace(RawKey { epoch, key.piece, col, axis, role, context.activeMicroTileLength, cell.nativeIsFloor, cell.source.words }, canvas);
		};
		add(RawLayoutRole::UpperColumn, key.column, key.axis, column.upperColumnCanvas);
		add(RawLayoutRole::FullColumn, key.column, key.axis, column.fullColumnCanvas);
		add(RawLayoutRole::NativeBasePair, 2, NativeTextureAxis::Horizontal, cell.baseCanvas);
		out.counts.upperEmptyGroups += group.ownUpperEmpty; out.counts.fullEmptyGroups += group.ownFullColumnEmpty;
		out.counts.upperZeroWords += group.upperZeroWords; out.counts.upperPresentWords += group.upperPresentWords;
		out.counts.representativePairs += group.representative.has_value(); out.counts.groupsWithoutPair += !group.representative.has_value();
		out.counts.mixedPolicyGroups += group.mixedPolicy; out.counts.mixedSourceContextGroups += group.mixedSourceContext;
		out.counts.knownCurrentBandGroups += group.currentBand.has_value();
		out.counts.currentDonorGroups += group.currentBand.has_value() && group.currentBand->donor;
		out.groups.push_back(std::move(group));
	}
	const std::set<RawKey> resident(context.residentRawKeys.begin(), context.residentRawKeys.end());
	for (const auto &[key, canvas] : raw) {
		const auto estimate = detail::Estimate(canvas);
		const bool hit = resident.contains(key);
		out.rawKeys.push_back({ key, canvas, estimate, hit });
		++out.counts.roleKeyCounts[static_cast<std::size_t>(key.role)];
		out.counts.roleHostRawBytes[static_cast<std::size_t>(key.role)] += estimate.totalBytes;
		out.counts.allRolesHostRawBytes += estimate.totalBytes;
		out.counts.existingKeyHits += hit; out.counts.coldNewKeys += !hit;
		if (!hit) out.counts.coldNewHostRawBytes += estimate.totalBytes;
	}
	out.counts.sourceCells = out.nativeCellPlans.size(); out.counts.sourceColumnGroups = out.groups.size();
	out.counts.uniqueRawRoleKeys = out.rawKeys.size(); out.counts.unchangedWarmKeyHits = out.rawKeys.size();
	return out;
}

} // namespace devilution::cathedral::wall_face_plan
