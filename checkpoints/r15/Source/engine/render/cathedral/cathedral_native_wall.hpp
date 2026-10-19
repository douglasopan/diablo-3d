#pragma once

// PRIVATE SOURCE-ONLY CANDIDATE. Not compiled or executed by its author.
// This bounded visual update neither decodes pixels nor generates a Frame/map.
// The physical plane supplies kappa as a technical reconstruction choice, never
// as evidence of native artistic facing or native 3D depth. Rendered coverage,
// depth and pixel-pick footprint deliberately follow the new visual vertices.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

#include "engine/render/cathedral/cathedral_frame.hpp"
#include "cathedral_native_cell_art.hpp"
#include "native-wall-face-plan.hpp"
#include "native-wall-projection.hpp"

namespace devilution::cathedral::native_wall_runtime {

inline constexpr std::uint32_t PolicyRevision = 2;
inline constexpr unsigned CathedralMicroTileLength = 10;
inline constexpr bool SuppliesNativeBasePayload = true;
inline constexpr bool NativeFitEvidenceRequiredBeforeEnable = true;
// Original mt0/1 belongs to this full raw column, including absent words and
// native coverage; it is neither a squeezed lower band nor a donor. Presence of
// a base payload does not certify contact with the visual floor: the unchanged
// measured normal plane leaves that artistic/depth choice unresolved. The owner
// must validate NativeFitEvidence before enabling the default-OFF pilot, with
// independent full DrawCell pixels, base/floor contact and inter-object occlusion.

enum class SkipReason : std::uint8_t {
	NoRepresentativePair,
	NotOriginalWall,
	NotPrimaryAxis,
	ApproximateBinding,
	MixedPolicyOrContext,
	CutawayDoorOrFragment,
	NonOpaque,
	FloorOrNativeTransparency,
	OwnUpperEmpty,
	MultipleSources,
	NotRepeatedBand,
	NativeOccupied,
	VisualSuppressed
};

struct SkippedGroup {
	wall_face_plan::GroupKey sourceKey;
	SkipReason reason;
};

struct FaceUpdate {
	wall_face_plan::GroupKey sourceKey;
	wall_face_plan::RawKey rawKey;
	wall_composition::SourceCellMetadata source;
	Cell nativeSourceCell {}; // Full frozen source cell, including native object/light/flags.
	wall_composition::AtlasDescriptor canvas;
	native_wall_projection::Plane plane;
	std::array<std::size_t, 2> triangleIndices {};
	std::size_t surfaceRecordIndex = 0;
	NativeSurfaceRecord expectedSurface;
	std::array<FrameTriangle, 2> expectedTriangles {};
	std::array<std::array<Vertex, 3>, 2> visualTriangles {};
};

struct Counts {
	std::size_t sourceColumnGroups = 0, eligiblePairs = 0, updatedTriangles = 0;
	std::size_t skippedGroups = 0, uniqueFullColumnKeys = 0;
	// Unallocated raw payload estimates only. No cache/GPU admission is granted.
	std::uint64_t estimatedHostRawBytes = 0;
};

struct Updates {
	std::uint32_t policyRevision = PolicyRevision;
	std::uint32_t packingRevision = NativeFullColumnPackingRevision;
	std::uint64_t epoch = 0, geometryRevision = 0, presentationSignature = 0;
	int regionX = 0, regionZ = 0;
	std::size_t regions = 0, triangleCount = 0, nativeSurfaceCount = 0, nativeArtPanelCount = 0;
	bool frameRequiresNativeFallback = false;
	std::uint32_t frameFallbackReasons = 0;
	unsigned activeMicroTileLength = CathedralMicroTileLength;
	std::vector<FaceUpdate> faces;
	std::vector<SkippedGroup> skipped;
	Counts counts;
};

namespace detail {

inline void Require(bool condition, const char *message)
{
	if (!condition) throw std::invalid_argument(message);
}
inline float At(Vec3 p, unsigned axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
inline bool SamePosition(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool SameVertices(const std::array<Vertex, 3> &a, const std::array<Vertex, 3> &b)
{
	for (std::size_t i = 0; i < a.size(); ++i)
		if (!SamePosition(a[i].position, b[i].position) || a[i].u != b[i].u || a[i].v != b[i].v) return false;
	return true;
}
inline bool SameBinding(const NativeTextureBinding &a, const NativeTextureBinding &b)
{
	return a.kind == b.kind && a.axis == b.axis && a.x == b.x && a.z == b.z
	    && a.piece == b.piece && a.nativeSlot == b.nativeSlot && a.column == b.column
	    && a.approximate == b.approximate && a.layout == b.layout && a.operationPass == b.operationPass;
}
inline bool SameTriangle(const FrameTriangle &a, const FrameTriangle &b)
{
	return SameVertices(a.vertices, b.vertices) && SameVertices(a.textureVertices, b.textureVertices)
	    && a.pick == b.pick && a.module == b.module && a.instanceId == b.instanceId
	    && a.light == b.light && a.transparent == b.transparent && a.policy == b.policy
	    && a.visualSuppressed == b.visualSuppressed && SameBinding(a.nativeTexture, b.nativeTexture);
}
inline bool SameSurface(const NativeSurfaceRecord &a, const NativeSurfaceRecord &b)
{
	if (a.instanceId != b.instanceId || a.sourceInstanceId != b.sourceInstanceId
	    || a.module != b.module || a.sourceModule != b.sourceModule || a.policy != b.policy
	    || a.provenanceResolved != b.provenanceResolved || a.sources.size() != b.sources.size()) return false;
	for (std::size_t i = 0; i < a.sources.size(); ++i)
		if (!wall_face_plan::detail::SameSource(a.sources[i], b.sources[i])) return false;
	return true;
}
inline bool SameCanvas(const wall_composition::AtlasDescriptor &a, const wall_composition::AtlasDescriptor &b)
{
	return a.width == b.width && a.height == b.height && a.minX == b.minX && a.minY == b.minY
	    && a.maxXExclusive == b.maxXExclusive && a.maxYExclusive == b.maxYExclusive;
}

/** Labels are taken from the accepted physical rectangle, keeping each old
 * triangle's index/vertex order and shared diagonal. Raw native screen X grows
 * in +X for AlongX and -Z for AlongZ; raw bitmap V grows down. These coordinate
 * labels, rather than one universal quad winding, carry the original normal.
 */
inline std::array<std::array<Vertex, 3>, 2> VisualTriangles(
    const std::array<FrameTriangle, 2> &physical, const native_wall_projection::Plane &plane)
{
	const bool alongX = plane.axis == native_wall_projection::Axis::AlongX;
	const unsigned normalAxis = alongX ? 2U : 0U, tangentAxis = 2U - normalAxis;
	const float normalPlane = At(physical[0].vertices[0].position, normalAxis);
	float minT = At(physical[0].vertices[0].position, tangentAxis), maxT = minT;
	float minY = physical[0].vertices[0].position.y, maxY = minY;
	for (const auto &triangle : physical) for (const auto &vertex : triangle.vertices) {
		Require(At(vertex.position, normalAxis) == normalPlane, "Native full update physical face is not planar");
		minT = std::min(minT, At(vertex.position, tangentAxis)); maxT = std::max(maxT, At(vertex.position, tangentAxis));
		minY = std::min(minY, vertex.position.y); maxY = std::max(maxY, vertex.position.y);
	}
	Require(minT < maxT && minY < maxY, "Native full update physical rectangle is degenerate");
	std::array<std::array<Vertex, 3>, 2> out {};
	for (std::size_t t = 0; t < physical.size(); ++t) {
		for (std::size_t i = 0; i < physical[t].vertices.size(); ++i) {
			const Vec3 p = physical[t].vertices[i].position;
			const float tangent = At(p, tangentAxis);
			Require((tangent == minT || tangent == maxT) && (p.y == minY || p.y == maxY),
			    "Native full update requires physical corner labels");
			const float u = alongX ? (tangent == maxT ? 1.0F : 0.0F) : (tangent == minT ? 1.0F : 0.0F);
			const float v = p.y == maxY ? 0.0F : 1.0F;
			const auto q = native_wall_projection::PixelVertex(plane, u, v);
			out[t][i] = { { q.x, q.height, q.z }, u, v };
			Require(At(out[t][i].position, normalAxis) == normalPlane,
			    "Native full update float reconstruction changed the measured normal plane");
		}
		FrameTriangle art = physical[t]; art.vertices = out[t];
		const Vec3 oldNormal = wall_face_plan::detail::Normal(physical[t]);
		const Vec3 newNormal = wall_face_plan::detail::Normal(art);
		Require(newNormal.y == 0 && At(newNormal, tangentAxis) == 0
		        && At(oldNormal, normalAxis) * At(newNormal, normalAxis) > 0,
		    "Native full update changed physical winding or produced degenerate visual geometry");
	}
	return out;
}

inline void ValidateFace(const FaceUpdate &face, std::uint64_t epoch, unsigned microLength)
{
	const auto &key = face.sourceKey;
	Require((key.axis == NativeTextureAxis::AlongX || key.axis == NativeTextureAxis::AlongZ)
	        && key.column == (key.axis == NativeTextureAxis::AlongX ? 1 : 0)
	        && face.source.sourceX == key.x && face.source.sourceZ == key.z && face.source.piece == key.piece
	        && face.source.activeMicroTileLength == microLength, "Native full update source/key mismatch");
	Require(face.nativeSourceCell.piece == face.source.piece && face.nativeSourceCell.properties == face.source.sol
	        && face.nativeSourceCell.transparency == face.source.transparencyGroup && face.nativeSourceCell.object == 0
	        && face.nativeSourceCell.light <= 15,
	    "Native full update excludes any source occupied by a native object");
	const auto cell = wall_composition::BuildCellPlan(face.source); // Metadata only, before any mutation.
	Require(!cell.nativeIsFloor && !cell.nativeWallTransparency && !cell.columns[key.column].noPresentUpperWords,
	    "Native full update requires its own opaque native upper column");
	Require(SameCanvas(face.canvas, cell.columns[key.column].fullColumnCanvas)
	        && face.canvas.width == 32 && face.canvas.height == 160
	        && face.canvas.minY == -159 && face.canvas.maxYExclusive == 1,
	    "Native full update canvas differs from original Cathedral base-plus-upper span");
	Require(face.rawKey.epoch == epoch && face.rawKey.piece == key.piece && face.rawKey.column == key.column
	        && face.rawKey.axis == key.axis && face.rawKey.role == wall_face_plan::RawLayoutRole::FullColumn
	        && face.rawKey.activeMicroTileLength == microLength && !face.rawKey.nativeIsFloor
	        && face.rawKey.words == face.source.words, "Native full update raw identity mismatch");
	const auto &record = face.expectedSurface;
	Require(record.provenanceResolved && record.module == ModuleKind::Wall && record.sourceModule == ModuleKind::Wall
	        && record.instanceId == record.sourceInstanceId && record.policy == FrameSurfacePolicy::Opaque
	        && record.sources.size() == 1, "Native full update requires one original Wall source record");
	const auto &source = record.sources[0];
	Require(source.x == key.x && source.z == key.z && source.piece == key.piece && source.properties == face.source.sol
	        && source.transparencyGroup == face.source.transparencyGroup && source.groupActive == face.source.groupActive
	        && !source.blendActive, "Native full update source context differs from the native record");
	for (const auto &t : face.expectedTriangles) {
		const auto &b = t.nativeTexture;
		Require(t.instanceId == record.instanceId && t.module == ModuleKind::Wall && t.policy == FrameSurfacePolicy::Opaque
		        && !t.transparent && !t.visualSuppressed && b.kind == NativeTextureKind::Masonry && !b.approximate
		        && b.layout == NativeTextureLayout::RepeatedBand && b.x == key.x && b.z == key.z && b.piece == key.piece
		        && b.axis == key.axis && b.column == key.column && b.nativeSlot == -1,
		    "Native full update expected triangle/binding mismatch");
		wall_face_plan::detail::CheckVertices(t.vertices);
		wall_face_plan::detail::CheckVertices(t.textureVertices);
	}
	wall_face_plan::Reference a, b;
	a.triangle = face.expectedTriangles[0]; b.triangle = face.expectedTriangles[1];
	Require(wall_face_plan::detail::RectanglePair(a, b, key.axis), "Native full update is not one complementary rectangle pair");
	Require(face.triangleIndices[0] < face.triangleIndices[1], "Native full update triangle indices are not canonical");
	const bool alongX = key.axis == NativeTextureAxis::AlongX;
	const unsigned normalAxis = alongX ? 2U : 0U;
	const float measured = At(face.expectedTriangles[0].vertices[0].position, normalAxis)
	    - static_cast<float>(alongX ? key.z : key.x);
	Require(face.plane.sourceX == key.x && face.plane.sourceZ == key.z && face.plane.column == key.column
	        && face.plane.axis == (alongX ? native_wall_projection::Axis::AlongX : native_wall_projection::Axis::AlongZ)
	        && face.plane.declaredNormalOffset == measured && face.plane.layers.firstRow == 0 && face.plane.layers.rows == 5
	        && !face.plane.storedAtlasHorizontallyFlipped, "Native full update plane/span is not the measured physical face");
	const auto expected = VisualTriangles(face.expectedTriangles, face.plane);
	for (std::size_t i = 0; i < expected.size(); ++i)
		Require(SameVertices(expected[i], face.visualTriangles[i]), "Native full update visual geometry/UV was changed after planning");
}

} // namespace detail

/** Read-only planning over the accepted live Snapshot/Frame. Only opaque,
 * source-original Wall primary-axis, nonapproximate rectangular pairs qualify.
 * Shared raw artwork is deduplicated for estimates, but application remains per
 * selected pair: other Walls, corners, caps, backs, frames and doorfit fragments
 * are never rebound through a global piece/column override. Unsupported metadata
 * throws before external action. Original base rows are included without changing
 * the own-upper-present admission bound; this does not admit base-only sources.
 * No decoder/cache/global/IO is called here. Independent NativeFitEvidence is a
 * mandatory owner gate before enabling, not a result inferred from this plan.
 */
inline Updates BuildUpdates(const Snapshot &snapshot, const PilotFrame &frame, unsigned activeMicroTileLength)
{
	using detail::Require;
	Require(activeMicroTileLength == CathedralMicroTileLength, "Native full runtime candidate is bounded to Cathedral MicroTileLen10");
	wall_face_plan::Context context;
	context.activeMicroTileLength = activeMicroTileLength;
	const auto plan = wall_face_plan::Build(snapshot, frame, context);
	Updates out;
	out.epoch = frame.gameRevision; out.geometryRevision = frame.geometryRevision; out.presentationSignature = frame.presentationSignature;
	out.regionX = frame.regionX; out.regionZ = frame.regionZ; out.regions = frame.regions;
	out.triangleCount = frame.triangles.size(); out.nativeSurfaceCount = frame.nativeSurfaces.size();
	out.nativeArtPanelCount = frame.nativeArtPanels.size();
	out.frameRequiresNativeFallback = frame.requiresNativeFallback; out.frameFallbackReasons = frame.fallbackReasons;
	out.activeMicroTileLength = activeMicroTileLength;
	out.counts.sourceColumnGroups = plan.groups.size();
	std::set<wall_face_plan::RawKey> rawKeys;
	for (const auto &group : plan.groups) {
		const auto skip = [&](SkipReason reason) { out.skipped.push_back({ group.key, reason }); };
		if (!group.representative) { skip(SkipReason::NoRepresentativePair); continue; }
		const auto &pair = *group.representative;
		if (!pair.originalUnfragmented || !pair.wallFamily || pair.module != ModuleKind::Wall) { skip(SkipReason::NotOriginalWall); continue; }
		if (!pair.followsWallPrimaryAxis) { skip(SkipReason::NotPrimaryAxis); continue; }
		if (!pair.bindingNonApproximate) { skip(SkipReason::ApproximateBinding); continue; }
		if (group.mixedPolicy || group.mixedSourceContext) { skip(SkipReason::MixedPolicyOrContext); continue; }
		if (group.containsCutaway || group.containsDoorFrame || group.containsFragments) { skip(SkipReason::CutawayDoorOrFragment); continue; }
		const auto &a = group.references[pair.referenceIndices[0]];
		const auto &b = group.references[pair.referenceIndices[1]];
		if (a.triangle.visualSuppressed || b.triangle.visualSuppressed) { skip(SkipReason::VisualSuppressed); continue; }
		if (snapshot.At(group.key.x, group.key.z).object != 0) { skip(SkipReason::NativeOccupied); continue; }
		if (a.triangle.policy != FrameSurfacePolicy::Opaque || b.triangle.policy != FrameSurfacePolicy::Opaque) { skip(SkipReason::NonOpaque); continue; }
		const auto &cell = plan.nativeCellPlans[group.cellPlanIndex];
		if (cell.nativeIsFloor || cell.nativeWallTransparency) { skip(SkipReason::FloorOrNativeTransparency); continue; }
		if (group.ownUpperEmpty) { skip(SkipReason::OwnUpperEmpty); continue; }
		if (a.nativeSurface.sources.size() != 1 || b.nativeSurface.sources.size() != 1) { skip(SkipReason::MultipleSources); continue; }
		if (a.triangle.nativeTexture.layout != NativeTextureLayout::RepeatedBand
		    || b.triangle.nativeTexture.layout != NativeTextureLayout::RepeatedBand) { skip(SkipReason::NotRepeatedBand); continue; }
		FaceUpdate face;
		face.sourceKey = group.key; face.source = cell.source; face.canvas = cell.columns[group.key.column].fullColumnCanvas;
		face.nativeSourceCell = snapshot.At(group.key.x, group.key.z);
		face.rawKey = { out.epoch, group.key.piece, group.key.column, group.key.axis,
			wall_face_plan::RawLayoutRole::FullColumn, activeMicroTileLength, false, cell.source.words };
		face.triangleIndices = pair.triangleIndices; face.surfaceRecordIndex = a.surfaceRecordIndex;
		face.expectedSurface = a.nativeSurface; face.expectedTriangles = { a.triangle, b.triangle };
		const bool alongX = group.key.axis == NativeTextureAxis::AlongX;
		const float measured = detail::At(a.triangle.vertices[0].position, alongX ? 2U : 0U)
		    - static_cast<float>(alongX ? group.key.z : group.key.x);
		face.plane = { group.key.x, group.key.z,
			alongX ? native_wall_projection::Axis::AlongX : native_wall_projection::Axis::AlongZ,
			group.key.column, measured, { 0, 5 }, false };
		face.visualTriangles = detail::VisualTriangles(face.expectedTriangles, face.plane);
		detail::ValidateFace(face, out.epoch, activeMicroTileLength);
		rawKeys.insert(face.rawKey);
		out.faces.push_back(std::move(face));
	}
	out.counts.eligiblePairs = out.faces.size(); out.counts.updatedTriangles = out.faces.size() * 2;
	out.counts.skippedGroups = out.skipped.size(); out.counts.uniqueFullColumnKeys = rawKeys.size();
	out.counts.estimatedHostRawBytes = static_cast<std::uint64_t>(rawKeys.size()) * 32 * 160 * 2;
	return out;
}

/** Validate every target and proposed affine vertex before the first write.
 * Allocation/validation failure leaves the frame untouched. The commit uses
 * only nothrow POD assignments to textureVertices and layout. Physical vertices,
 * policy/light/IDs/pick, native records/specials, collisionModels, region selection
 * and revision identities are neither rewritten nor recounted by this helper.
 * Caller must still pass normal freshness/material/budget and atomic renderer
 * gates. This does not publish a frame or grant GPU/pixel visibility/admission.
 * Apply is an atomic candidate edit, not artistic promotion: the external owner
 * must supply and verify NativeFitEvidence before enabling normal presentation.
 */
inline void ApplyUpdates(PilotFrame &frame, const Updates &updates)
{
	using detail::Require;
	static_assert(std::is_nothrow_copy_assignable_v<std::array<Vertex, 3>>);
	Require(updates.policyRevision == PolicyRevision && updates.packingRevision == NativeFullColumnPackingRevision
	        && updates.activeMicroTileLength == CathedralMicroTileLength && updates.epoch != 0
	        && updates.geometryRevision != 0 && updates.regions > 0 && updates.regions <= 9,
	    "Native full apply unsupported update identity");
	Require(frame.gameRevision == updates.epoch && frame.geometryRevision == updates.geometryRevision
	        && frame.presentationSignature == updates.presentationSignature && frame.regionX == updates.regionX
	        && frame.regionZ == updates.regionZ && frame.regions == updates.regions
	        && frame.triangles.size() == updates.triangleCount && frame.nativeSurfaces.size() == updates.nativeSurfaceCount
	        && frame.nativeArtPanels.size() == updates.nativeArtPanelCount
	        && frame.requiresNativeFallback == updates.frameRequiresNativeFallback && frame.fallbackReasons == updates.frameFallbackReasons,
	    "Native full apply stale frame identity");
	std::set<std::size_t> usedTriangles;
	std::set<std::tuple<int, int, std::uint16_t, std::uint8_t, NativeTextureAxis>> usedColumns;
	for (const auto &face : updates.faces) {
		detail::ValidateFace(face, updates.epoch, updates.activeMicroTileLength);
		Require(face.surfaceRecordIndex < frame.nativeSurfaces.size()
		        && detail::SameSurface(frame.nativeSurfaces[face.surfaceRecordIndex], face.expectedSurface),
		    "Native full apply native source record changed");
		Require(usedColumns.emplace(face.sourceKey.x, face.sourceKey.z, face.sourceKey.piece, face.sourceKey.column, face.sourceKey.axis).second,
		    "Native full apply duplicate source column");
		for (std::size_t n = 0; n < face.triangleIndices.size(); ++n) {
			const auto index = face.triangleIndices[n];
			Require(index < frame.triangles.size() && usedTriangles.insert(index).second
			        && detail::SameTriangle(frame.triangles[index], face.expectedTriangles[n]),
			    "Native full apply stale/duplicate triangle or material binding");
		}
	}
	for (const auto &face : updates.faces) {
		for (std::size_t n = 0; n < face.triangleIndices.size(); ++n) {
			auto &triangle = frame.triangles[face.triangleIndices[n]];
			triangle.textureVertices = face.visualTriangles[n];
			triangle.nativeTexture.layout = NativeTextureLayout::OriginalFullColumn;
		}
	}
}

} // namespace devilution::cathedral::native_wall_runtime
