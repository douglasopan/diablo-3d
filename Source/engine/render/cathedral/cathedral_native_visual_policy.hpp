#pragma once

// PRIVATE SOURCE-ONLY. C++/native DrawCell/Frame/GPU execution UNKNOWN.
// Gameplay uses the authoritative native269/OBJ_L1LIGHT tuple, no receipts.
// Optional diagnostic receipts are not generated/verified by this header.
// Current native source/object/epoch freshness remains mandatory before draw.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cathedral_frame.hpp"
#include "cathedral_native_cell_art.hpp"

namespace devilution::cathedral::native_visual_policy {

inline constexpr std::uint32_t PolicyRevision = 1;
inline constexpr const char *SourceArtifactStatus = "SOURCE_ONLY_CPP_EXECUTION_UNKNOWN";
inline constexpr bool SuppliesReplacementPanels = false;
enum class AdmissionKind : std::uint8_t { ExplicitVerifiedTarget, NativeObjectAndPieceRule };

struct ExpectedTriangle {
	std::size_t index = 0;
	FrameTriangle triangle;
};

/** Explicit external admission, never inferred from MIN type, SOL solidity,
 * an emitter or a SHA-shaped string. The verified receipt must bind BOTH full
 * original DrawCell columns to this exact instance INCLUDING its cap/backs,
 * native object/light records, unchanged physical witness and inter-cell/object
 * occlusion. columns[n].column must equal n and columns[n].receiptSha256 MUST
 * be empty. This proof OWNS its receipt SHA; only a temporary evidence view is
 * made during validation. Exact surface/set/object state and frame identities
 * must be pinned by the owner gate, not filled retroactively by this planner.
 * Owner verification is a trust boundary, not a new native oracle here.
 */
struct TargetProof {
	std::uint64_t instanceId = 0, sourceInstanceId = 0;
	std::string receiptSha256;
	std::uint64_t epoch = 0, geometryRevision = 0, presentationSignature = 0;
	NativeSurfaceRecord expectedNativeSurface;
	std::vector<ExpectedTriangle> expectedTriangles;
	native_cell_art::NativeObjectMetadata expectedNativeObject;
	std::array<native_cell_art::NativeVisualEvidence, 2> columns {};
	bool ownerVerifiedExactTargetInstance = false;
	bool ownerVerifiedNativeObjectLightWitness = false;
};

struct TargetUpdate {
	TargetProof proof;
	native_cell_art::NativeCellContext context;
	wall_composition::SourceCellMetadata source;
	native_cell_art::NativeObjectMetadata nativeObject;
	std::size_t surfaceRecordIndex = 0;
	NativeSurfaceRecord expectedSurface;
	std::vector<ExpectedTriangle> triangles; // Every triangle of the instance.
};

struct FrameStamp {
	std::uint64_t epoch = 0, geometryRevision = 0, presentationSignature = 0;
	int regionX = 0, regionZ = 0;
	std::size_t regions = 0, triangleCount = 0, surfaceCount = 0;
	std::size_t collisionModelCount = 0, specialOverlayCount = 0, panelCount = 0;
	bool requiresNativeFallback = false;
	std::uint32_t fallbackReasons = 0;
};

struct Updates {
	std::uint32_t policyRevision = PolicyRevision;
	AdmissionKind admissionKind = AdmissionKind::ExplicitVerifiedTarget;
	FrameStamp frame;
	std::vector<TargetUpdate> targets;
	// Panels are owned by a separate verified replacement transaction. Their
	// current content is pinned, but this transaction never creates/changes any.
	std::vector<FrameTriangle> expectedNativeArtPanels;
	std::size_t suppressedTriangleCount = 0;
};

namespace detail {

inline void Require(bool condition, const char *message)
{
	if (!condition) throw std::invalid_argument(message);
}

inline bool SameVertex(const Vertex &a, const Vertex &b)
{
	return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z
	    && a.u == b.u && a.v == b.v;
}

inline bool FiniteVertex(const Vertex &v)
{
	return std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z)
	    && std::isfinite(v.u) && std::isfinite(v.v);
}

inline bool SameBinding(const NativeTextureBinding &a, const NativeTextureBinding &b)
{
	return a.kind == b.kind && a.axis == b.axis && a.x == b.x && a.z == b.z && a.piece == b.piece
	    && a.nativeSlot == b.nativeSlot && a.column == b.column && a.approximate == b.approximate && a.layout == b.layout
	    && a.operationPass == b.operationPass;
}

inline bool SameTriangle(const FrameTriangle &a, const FrameTriangle &b)
{
	for (std::size_t n = 0; n < 3; ++n)
		if (!SameVertex(a.vertices[n], b.vertices[n]) || !SameVertex(a.textureVertices[n], b.textureVertices[n])) return false;
	return a.pick == b.pick && a.module == b.module && a.instanceId == b.instanceId && a.light == b.light
	    && a.transparent == b.transparent && a.policy == b.policy && SameBinding(a.nativeTexture, b.nativeTexture)
	    && a.visualSuppressed == b.visualSuppressed;
}

inline bool SameSurfaceSource(const NativeSurfaceSource &a, const NativeSurfaceSource &b)
{
	return a.x == b.x && a.z == b.z && a.piece == b.piece && a.properties == b.properties
	    && a.transparencyGroup == b.transparencyGroup && a.partialMask == b.partialMask
	    && a.groupActive == b.groupActive && a.blendActive == b.blendActive && a.microMasksKnown == b.microMasksKnown;
}

inline bool SameSurface(const NativeSurfaceRecord &a, const NativeSurfaceRecord &b)
{
	if (a.instanceId != b.instanceId || a.sourceInstanceId != b.sourceInstanceId || a.module != b.module
	    || a.sourceModule != b.sourceModule || a.policy != b.policy || a.provenanceResolved != b.provenanceResolved
	    || a.sources.size() != b.sources.size()) return false;
	for (std::size_t n = 0; n < a.sources.size(); ++n) if (!SameSurfaceSource(a.sources[n], b.sources[n])) return false;
	return true;
}

inline bool SameObject(const native_cell_art::NativeObjectMetadata &a, const native_cell_art::NativeObjectMetadata &b)
{
	return a.epoch == b.epoch && a.slot == b.slot && a.sourceX == b.sourceX && a.sourceZ == b.sourceZ
	    && a.originX == b.originX && a.originZ == b.originZ && a.nativeType == b.nativeType && a.signedOccupancy == b.signedOccupancy
	    && a.active == b.active && a.authoritativeNativeRecord == b.authoritativeNativeRecord
	    && a.nativeEmitterRuleMatched == b.nativeEmitterRuleMatched && a.emitterRuleRadius == b.emitterRuleRadius
	    && a.appliesLighting == b.appliesLighting && a.preDraw == b.preDraw && a.nativeSolid == b.nativeSolid
	    && a.animationFrame == b.animationFrame && a.nativeLightId == b.nativeLightId;
}

inline FrameStamp Stamp(const PilotFrame &frame)
{
	return { frame.gameRevision, frame.geometryRevision, frame.presentationSignature,
		frame.regionX, frame.regionZ, frame.regions, frame.triangles.size(), frame.nativeSurfaces.size(),
		frame.collisionModels.size(), frame.nativeSpecialOverlays.size(), frame.nativeArtPanels.size(),
		frame.requiresNativeFallback, frame.fallbackReasons };
}

inline bool SameStamp(const FrameStamp &a, const FrameStamp &b)
{
	return a.epoch == b.epoch && a.geometryRevision == b.geometryRevision && a.presentationSignature == b.presentationSignature
	    && a.regionX == b.regionX && a.regionZ == b.regionZ && a.regions == b.regions && a.triangleCount == b.triangleCount
	    && a.surfaceCount == b.surfaceCount && a.collisionModelCount == b.collisionModelCount
	    && a.specialOverlayCount == b.specialOverlayCount && a.panelCount == b.panelCount
	    && a.requiresNativeFallback == b.requiresNativeFallback && a.fallbackReasons == b.fallbackReasons;
}

inline void RequireFrame(const PilotFrame &frame)
{
	Require(frame.gameRevision != 0 && frame.geometryRevision != 0 && !frame.requiresNativeFallback && frame.fallbackReasons == 0,
	    "Native visual suppression requires a current eligible frame");
}

inline void RequireSnapshot(const Snapshot &snapshot, const PilotFrame &frame)
{
	RequireFrame(frame);
	Require(snapshot.gameRevision == frame.gameRevision && snapshot.level == 1 && snapshot.levelType == 1
	        && snapshot.originalCathedral && !snapshot.hellfire && !snapshot.setLevel && snapshot.setLevelId == 0
	        && snapshot.minX == ActiveMin && snapshot.minZ == ActiveMin && snapshot.maxX == ActiveMax && snapshot.maxZ == ActiveMax,
	    "Native visual suppression requires this normal Cathedral1 snapshot/epoch");
}

inline void RequireOriginalWall(const NativeSurfaceRecord &record)
{
	Require(record.instanceId != 0 && record.instanceId == record.sourceInstanceId && record.module == ModuleKind::Wall
	        && record.sourceModule == ModuleKind::Wall && record.provenanceResolved && record.sources.size() == 1
	        && static_cast<unsigned>(record.policy) <= static_cast<unsigned>(FrameSurfacePolicy::ConservativeNativeCutaway),
	    "Native visual suppression target must be original unfragmented single-source Wall");
	const auto &source = record.sources.front();
	Require(source.x >= ActiveMin && source.x < ActiveMax && source.z >= ActiveMin && source.z < ActiveMax
	        && source.piece == 269 && source.microMasksKnown,
	    "Native visual suppression target requires authoritative piece269 source");
}

inline bool SurfaceMatchesSource(const NativeSurfaceRecord &record, const wall_composition::SourceCellMetadata &source)
{
	if (record.sources.size() != 1) return false;
	const auto &s = record.sources.front();
	return s.x == source.sourceX && s.z == source.sourceZ && s.piece == source.piece && s.properties == source.sol
	    && s.transparencyGroup == source.transparencyGroup && s.groupActive == source.groupActive
	    && s.partialMask == (source.sol & 0x30) && s.blendActive == ((source.sol & 0x08) != 0 && source.groupActive)
	    && s.microMasksKnown;
}

/** Production authority is the native tuple, not the optional diagnostic
 * receipt. The source269 rule is AddL1Objs; OBJ_L1LIGHT=0, radius5 is native
 * AddObjectLight. Actual active/signed membership and current native record
 * were captured by the caller, then tied to this immutable snapshot/epoch.
 */
inline void RequireNativeTarget(const TargetUpdate &target, const FrameStamp &frame)
{
	RequireOriginalWall(target.expectedSurface);
	Require(target.proof.instanceId == target.expectedSurface.instanceId
	        && target.proof.sourceInstanceId == target.expectedSurface.sourceInstanceId
	        && target.context.epoch == frame.epoch && target.nativeObject.epoch == frame.epoch
	        && target.source.activeMicroTileLength == 10 && SurfaceMatchesSource(target.expectedSurface, target.source),
	    "Native light suppression source/instance/epoch/actual MicroTileLen mismatch");
	const auto classification = native_cell_art::ClassifyCell(target.source, target.context,
	    std::span<const native_cell_art::NativeObjectMetadata>(&target.nativeObject, 1));
	Require(classification.corroboratedCathedralLight && classification.authority == native_cell_art::ClassAuthority::NativeObjectAndPieceRule
	        && !classification.nativeIsFloor,
	    "Native light suppression lacks exact authoritative active269/OBJ_L1LIGHT/origin/radius5 tuple");
}

inline void RequireProof(const TargetUpdate &target, const FrameStamp &frame)
{
	RequireOriginalWall(target.expectedSurface);
	Require(target.proof.instanceId == target.expectedSurface.instanceId
	        && target.proof.sourceInstanceId == target.expectedSurface.sourceInstanceId
	        && target.proof.ownerVerifiedExactTargetInstance && target.proof.ownerVerifiedNativeObjectLightWitness,
	    "Native visual suppression lacks owner-verified exact instance/object/light witness");
	Require(native_cell_art::IsLowerSha256(target.proof.receiptSha256) && target.proof.epoch == frame.epoch
	        && target.proof.geometryRevision == frame.geometryRevision && target.proof.presentationSignature == frame.presentationSignature
	        && SameSurface(target.proof.expectedNativeSurface, target.expectedSurface)
	        && SameObject(target.proof.expectedNativeObject, target.nativeObject),
	    "Native visual suppression owner receipt target/set/object/frame pins are stale");
	Require(target.context.epoch == frame.epoch && target.nativeObject.epoch == frame.epoch && target.source.activeMicroTileLength == 10
	        && SurfaceMatchesSource(target.expectedSurface, target.source),
	    "Native visual suppression source/epoch/actual MicroTileLen mismatch");
	for (unsigned column = 0; column < 2; ++column) {
		auto proof = target.proof.columns[column];
		Require(proof.receiptSha256.empty() && proof.column == column && proof.declaredClass == native_cell_art::CellArtClass::NativeLightFire,
		    "Native visual suppression requires two ordered native light full-column proofs");
		proof.receiptSha256 = target.proof.receiptSha256; // Temporary view, never stored in Updates.
		const auto classification = native_cell_art::ClassifyCell(target.source, target.context,
		    std::span<const native_cell_art::NativeObjectMetadata>(&target.nativeObject, 1), proof);
		Require(classification.corroboratedCathedralLight && classification.authority == native_cell_art::ClassAuthority::NativeObjectAndPieceRule
		        && classification.declaredCompleteVisualEvidence && !classification.evidenceRequired
		        && !classification.runtimeSuppressionAdmissionGranted && !classification.nativeIsFloor,
		    "Native visual suppression lacks verified original DrawCell/coverage/mask/occlusion evidence for both columns");
	}
	Require(!target.triangles.empty() && target.proof.expectedTriangles.size() == target.triangles.size(),
	    "Native visual suppression owner receipt lacks the exact full triangle set");
	for (std::size_t n = 0; n < target.triangles.size(); ++n)
		Require(target.proof.expectedTriangles[n].index == target.triangles[n].index
		        && SameTriangle(target.proof.expectedTriangles[n].triangle, target.triangles[n].triangle),
		    "Native visual suppression owner receipt triangle set/order/content mismatch");
}

inline void RequireTargetTriangle(const FrameTriangle &triangle, const NativeSurfaceRecord &surface)
{
	Require(triangle.instanceId == surface.instanceId && triangle.module == ModuleKind::Wall && triangle.policy == surface.policy
	        && !triangle.visualSuppressed && static_cast<unsigned>(triangle.nativeTexture.kind) <= static_cast<unsigned>(NativeTextureKind::DoorWood)
	        && static_cast<unsigned>(triangle.nativeTexture.axis) <= static_cast<unsigned>(NativeTextureAxis::AlongZ)
	        && static_cast<unsigned>(triangle.nativeTexture.layout) <= static_cast<unsigned>(NativeTextureLayout::OriginalFullColumn),
	    "Native visual suppression triangle is stale, already suppressed or not original Wall");
	for (std::size_t n = 0; n < 3; ++n)
		Require(FiniteVertex(triangle.vertices[n]) && FiniteVertex(triangle.textureVertices[n]),
		    "Native visual suppression triangle has nonfinite physical/visual vertices");
}

} // namespace detail

/** Deterministic GAMEPLAY path: no receipt, SHA or per-cell visual declaration.
 * Native piece269 + actual active OBJ_L1LIGHT, signed occupancy, same origin,
 * native radius5 and epoch means decoration, so this exact technical Wall
 * instance is hidden VISUALLY. Physical triangles/picks/collision stay intact.
 * Only original single-source unfragmented Wall is supported. Adjacent corners,
 * joins, fragments and native objects/lights remain untouched and detectable.
 * Missing corroboration produces no target; malformed supplied active records
 * throw. No native record/seed/map/SOL/light is changed. The root caller must
 * capture the authoritative active slot, dObject and Object.position freshly.
 */
inline Updates BuildNativeLightSuppression(const Snapshot &snapshot, const PilotFrame &frame,
    std::span<const native_cell_art::NativeObjectMetadata> objectDTOs)
{
	detail::RequireSnapshot(snapshot, frame);
	Updates result;
	result.admissionKind = AdmissionKind::NativeObjectAndPieceRule;
	result.frame = detail::Stamp(frame);
	result.expectedNativeArtPanels = frame.nativeArtPanels;
	std::set<std::uint64_t> instances;
	for (std::size_t recordIndex = 0; recordIndex < frame.nativeSurfaces.size(); ++recordIndex) {
		const auto &record = frame.nativeSurfaces[recordIndex];
		if (record.module != ModuleKind::Wall || record.sourceModule != ModuleKind::Wall || record.instanceId != record.sourceInstanceId
		    || !record.provenanceResolved || record.sources.size() != 1 || record.sources.front().piece != 269) continue;
		detail::RequireOriginalWall(record);
		TargetUpdate target;
		target.surfaceRecordIndex = recordIndex; target.expectedSurface = record;
		// These are identifiers only on this path; no diagnostic proof is used.
		target.proof.instanceId = record.instanceId; target.proof.sourceInstanceId = record.sourceInstanceId;
		const auto &s = record.sources.front();
		const auto &cell = snapshot.At(s.x, s.z);
		detail::Require(cell.piece == 269 && cell.piece < snapshot.micros.size(), "Native light suppression snapshot source/MIN mismatch");
		target.source = { s.x, s.z, cell.piece, snapshot.micros[cell.piece], 10,
			cell.properties, cell.transparency, snapshot.activeTransparency[cell.transparency] };
		const int megaX = (s.x - ActiveMin) / 2, megaZ = (s.z - ActiveMin) / 2;
		target.context = { snapshot.gameRevision, snapshot.mega[static_cast<std::size_t>(megaZ) * MegaSize + megaX],
			ActiveMin + 2 * megaX, ActiveMin + 2 * megaZ, false, cell.object };
		std::size_t matchedObjects = 0;
		for (const auto &object : objectDTOs) {
			if (object.sourceX != s.x || object.sourceZ != s.z) continue;
			++matchedObjects; target.nativeObject = object;
		}
		detail::Require(matchedObjects <= 1, "Native light suppression has duplicate captured object records");
		if (matchedObjects == 0) continue;
		const auto classification = native_cell_art::ClassifyCell(target.source, target.context,
		    std::span<const native_cell_art::NativeObjectMetadata>(&target.nativeObject, 1));
		if (!classification.corroboratedCathedralLight) continue;
		detail::RequireNativeTarget(target, result.frame);
		detail::Require(instances.insert(record.instanceId).second, "Native light suppression has duplicate exact instance records");
		for (std::size_t n = 0; n < frame.triangles.size(); ++n) {
			if (frame.triangles[n].instanceId != record.instanceId) continue;
			detail::RequireTargetTriangle(frame.triangles[n], record);
			target.triangles.push_back({ n, frame.triangles[n] });
		}
		detail::Require(!target.triangles.empty(), "Native light suppression has no physical instance triangles");
		result.suppressedTriangleCount += target.triangles.size();
		result.targets.push_back(std::move(target));
	}
	return result;
}

/** Default-empty proofs produce an empty transaction, never auto-whitelist.
 * Every explicit proof is strict: unknown, incomplete, stale or duplicate
 * targets throw. Object metadata must come from actual active native records,
 * including signed dObject occupancy; merely constructing this DTO is not proof.
 * Only AddL1Objs' exact piece269 + OBJ_L1LIGHT at that cell is supported.
 * No coordinates, SOL-solid inference, MIN-type classification or donor art
 * grants admission. Objects/lights/sprites/physics/source metadata stay native.
 */
inline Updates BuildSuppression(const Snapshot &snapshot, const PilotFrame &frame,
    std::span<const native_cell_art::NativeObjectMetadata> objectDTOs, std::span<const TargetProof> explicitTargetProofs)
{
	detail::RequireSnapshot(snapshot, frame);
	Updates result;
	result.frame = detail::Stamp(frame);
	result.expectedNativeArtPanels = frame.nativeArtPanels;
	std::set<std::uint64_t> instances;
	for (const auto &proof : explicitTargetProofs) {
		detail::Require(proof.instanceId != 0 && instances.insert(proof.instanceId).second,
		    "Native visual suppression contains duplicate/zero target instance");
		TargetUpdate target;
		target.proof = proof;
		std::size_t matches = 0;
		for (std::size_t n = 0; n < frame.nativeSurfaces.size(); ++n) {
			if (frame.nativeSurfaces[n].instanceId != proof.instanceId) continue;
			++matches; target.surfaceRecordIndex = n; target.expectedSurface = frame.nativeSurfaces[n];
		}
		detail::Require(matches == 1, "Native visual suppression target has missing/duplicate surface record");
		detail::RequireOriginalWall(target.expectedSurface);
		const auto &sourceRecord = target.expectedSurface.sources.front();
		const auto &cell = snapshot.At(sourceRecord.x, sourceRecord.z);
		detail::Require(cell.piece == 269 && cell.piece < snapshot.micros.size(),
		    "Native visual suppression snapshot piece269/MIN mismatch");
		target.source = { sourceRecord.x, sourceRecord.z, cell.piece, snapshot.micros[cell.piece], 10,
			cell.properties, cell.transparency, snapshot.activeTransparency[cell.transparency] };
		const int megaX = (sourceRecord.x - ActiveMin) / 2, megaZ = (sourceRecord.z - ActiveMin) / 2;
		target.context = { snapshot.gameRevision, snapshot.mega[static_cast<std::size_t>(megaZ) * MegaSize + megaX],
			ActiveMin + 2 * megaX, ActiveMin + 2 * megaZ, false, cell.object };
		std::size_t objects = 0;
		for (const auto &object : objectDTOs) {
			if (object.sourceX != sourceRecord.x || object.sourceZ != sourceRecord.z) continue;
			++objects; target.nativeObject = object;
		}
		detail::Require(objects == 1, "Native visual suppression requires one authoritative native object record");
		for (std::size_t n = 0; n < frame.triangles.size(); ++n) {
			if (frame.triangles[n].instanceId != proof.instanceId) continue;
			detail::RequireTargetTriangle(frame.triangles[n], target.expectedSurface);
			target.triangles.push_back({ n, frame.triangles[n] });
		}
		detail::Require(!target.triangles.empty(), "Native visual suppression target has no physical triangles");
		detail::RequireProof(target, result.frame);
		result.suppressedTriangleCount += target.triangles.size();
		result.targets.push_back(std::move(target));
	}
	return result;
}

/** All allocation/validation completes BEFORE the first POD bool commit.
 * Duplicate, out-of-range, stale, tampered, partial-instance, apply-again and
 * unknown transactions throw invalid_argument with no frame writes. Geometry,
 * texture UV/layout/policy, source records, collision and panels are untouched.
 * This checks immutable frame pins; the caller MUST also revalidate the current
 * live epoch/source/objects/light witness after proof injection and before GPU
 * submission. This function cannot observe native globals or receipt files.
 * No native base/occlusion/pixel truth is established by a successful apply.
 */
inline void ApplySuppression(PilotFrame &frame, const Updates &updates)
{
	detail::RequireFrame(frame);
	detail::Require(updates.policyRevision == PolicyRevision && detail::SameStamp(updates.frame, detail::Stamp(frame))
	        && static_cast<unsigned>(updates.admissionKind) <= static_cast<unsigned>(AdmissionKind::NativeObjectAndPieceRule),
	    "Native visual suppression transaction is stale or has unknown policy revision");
	detail::Require(updates.expectedNativeArtPanels.size() == frame.nativeArtPanels.size(),
	    "Native visual suppression native panel count changed");
	for (std::size_t n = 0; n < frame.nativeArtPanels.size(); ++n)
		detail::Require(detail::SameTriangle(updates.expectedNativeArtPanels[n], frame.nativeArtPanels[n]),
		    "Native visual suppression native panel content changed");
	std::set<std::uint64_t> instances;
	std::set<std::size_t> triangleIndices, surfaceIndices;
	for (const auto &target : updates.targets) {
		if (updates.admissionKind == AdmissionKind::NativeObjectAndPieceRule) detail::RequireNativeTarget(target, updates.frame);
		else detail::RequireProof(target, updates.frame);
		detail::Require(instances.insert(target.proof.instanceId).second && surfaceIndices.insert(target.surfaceRecordIndex).second,
		    "Native visual suppression transaction duplicates an instance/surface");
		detail::Require(target.surfaceRecordIndex < frame.nativeSurfaces.size()
		        && detail::SameSurface(target.expectedSurface, frame.nativeSurfaces[target.surfaceRecordIndex]),
		    "Native visual suppression target surface is stale or out of range");
		std::size_t surfaceMatches = 0, actualInstanceTriangles = 0;
		for (const auto &surface : frame.nativeSurfaces) if (surface.instanceId == target.proof.instanceId) ++surfaceMatches;
		for (const auto &triangle : frame.triangles) if (triangle.instanceId == target.proof.instanceId) ++actualInstanceTriangles;
		detail::Require(surfaceMatches == 1 && !target.triangles.empty() && actualInstanceTriangles == target.triangles.size(),
		    "Native visual suppression must include every exact instance triangle");
		for (const auto &expected : target.triangles) {
			detail::Require(expected.index < frame.triangles.size() && triangleIndices.insert(expected.index).second,
			    "Native visual suppression triangle index is duplicate or out of range");
			detail::RequireTargetTriangle(expected.triangle, target.expectedSurface);
			detail::Require(detail::SameTriangle(expected.triangle, frame.triangles[expected.index]),
			    "Native visual suppression physical/visual/pick/source/policy triangle changed");
		}
	}
	detail::Require(triangleIndices.size() == updates.suppressedTriangleCount,
	    "Native visual suppression transaction triangle count mismatch");
	// No allocation or throwing operation remains after this line.
	for (const auto &target : updates.targets)
		for (const auto &expected : target.triangles) frame.triangles[expected.index].visualSuppressed = true;
}

} // namespace devilution::cathedral::native_visual_policy
