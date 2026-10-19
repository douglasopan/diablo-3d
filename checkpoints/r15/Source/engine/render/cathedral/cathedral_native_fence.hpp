#pragma once

// PRIVATE SOURCE-ONLY CANDIDATE. Never compiled/run by this author. Known native
// RAW35/36 uses loaded TIL/source corroboration, without gameplay receipt/SHA.
// Optional diagnostic proofs are separate; neither path proves native 3D depth.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cathedral_native_wall.hpp"
#include "cathedral_native_operators.hpp"

namespace devilution::cathedral::native_fence_runtime {

inline constexpr std::uint32_t PolicyRevision = 3;
inline constexpr const char *SourceArtifactStatus = "SOURCE_ONLY_CPP_EXECUTION_UNKNOWN";

struct TriangleWitness {
	std::size_t triangleIndex = 0;
	FrameTriangle triangle {};
};

/** Copy-safe external declaration. receiptSha256 OWNS its characters; each
 * columnDeclarations receiptSha256 MUST be empty, avoiding borrowed views in
 * a setter/cache copy. Validation binds a temporary NativeVisualEvidence view
 * to the owned receipt. No file is read or SHA verified here: the principal's
 * receipt must bind every declaration, original DrawCell reference, target
 * witness and full rendered replacement before setting ownerVerifiedTargetBundle.
 */
struct TargetGeometry {
	std::uint64_t epoch = 0, geometryRevision = 0, presentationSignature = 0, instanceId = 0;
	NativeSurfaceRecord expectedSurface;
	// Native macro's (0,0),(1,0),(0,1),(1,1), including flags/light/object/special.
	std::array<Cell, 4> expectedSourceCells {};
	std::vector<TriangleWitness> expectedTriangles; // EVERY original Fence bar triangle, in increasing index order.
	std::array<std::size_t, 2> primaryPlaneTriangleIndices {};
	// Native-rule path uses ONLY epoch/source/column, with all evidence flags
	// absent. This never fabricates an independent-reference declaration.
	std::vector<native_cell_art::NativeVisualEvidence> columnDeclarations;
};

struct TargetProof : TargetGeometry {
	std::string receiptSha256;
	bool ownerVerifiedTargetBundle = false;
	bool nativeBaseFloorContactCompared = false;
	bool completeTechnicalFenceReplacementCompared = false;
};

/** Actual loaded pMegaTiles[rawMega-1] children after Swap16LE, copied directly
 * to native dPiece by gendung.cpp: NO subtraction/decrement. The field name
 * zeroBasedNativePieces echoes Snapshot's piece slots, not an extra conversion.
 * Values have (0,0),(1,0),(0,1),(1,1) order. The principal
 * captures this declaration from that loaded table, never a guessed whitelist.
 * Build cross-checks all four Snapshot/Surface sources in the same loaded epoch.
 * No seed, rendered-image receipt/SHA or gameplay telemetry is required.
 */
struct NativeMegaWitness {
	std::uint64_t epoch = 0;
	std::uint8_t rawMega = 0;
	int originX = 0, originZ = 0;
	std::array<std::uint16_t, 4> zeroBasedNativePieces {};
	bool loadedNativeTilMappingVerified = false;
};

struct NativeRuleTarget : TargetGeometry {
	NativeMegaWitness witness;
};

struct TargetUpdate {
	TargetProof proof;
	std::optional<NativeRuleTarget> nativeRule; // Present only for loaded native rule; proof stays absent/default.
	std::uint8_t rawMega = 0;
	std::vector<FrameTriangle> panels; // Two triangles per own column/draw pass, never collision models.
};

// The partial/blend value retains old receipt identity; known native operators
// no longer populate it. Unsupported/unknown operators fail the pure preflight.
enum class NativeRuleSkipReason : std::uint8_t { NotSelected, NativePartialOrBlendUnsupported, NativeObjectOrSpecialUnsupported };
struct SkippedNativeRule {
	NativeMegaWitness witness;
	NativeRuleSkipReason reason;
};

struct Updates {
	std::uint32_t policyRevision = PolicyRevision, packingRevision = NativeFullColumnPackingRevision;
	std::uint64_t epoch = 0, geometryRevision = 0, presentationSignature = 0;
	int regionX = 0, regionZ = 0;
	std::size_t regions = 0, triangleCount = 0, nativeSurfaceCount = 0;
	bool requiresNativeFallback = false;
	std::uint32_t fallbackReasons = 0;
	unsigned activeMicroTileLength = 10;
	// Captured panels remain exact; Wall -> Fence -> decoration order is explicit.
	std::vector<FrameTriangle> expectedExistingPanels;
	std::vector<TargetUpdate> targets;
	std::vector<SkippedNativeRule> skippedNativeRules;
	bool requiresCompleteNativeFallback = false; // Owner checks BEFORE Apply/material/GPU submission.
	std::size_t addedPanels = 0, suppressedTechnicalTriangles = 0, uniqueFullColumnKeys = 0;
	std::uint64_t estimatedHostRawBytes = 0; // Estimate only, no cache admission.
};

namespace detail {

inline void Require(bool value, const char *message)
{
	if (!value) throw std::invalid_argument(message);
}

inline std::pair<int, int> Origin(const TargetGeometry &proof)
{
	const int x = static_cast<int>((proof.instanceId >> 8) & 65535U) - 128;
	const int z = static_cast<int>((proof.instanceId >> 24) & 255U) - 128;
	Require(wall_face_plan::detail::Active(x, z) && (x & 1) == 0 && (z & 1) == 0,
	    "Native fence proof must identify an active original macro origin");
	return { x, z };
}

inline const FrameTriangle &WitnessAt(const TargetGeometry &proof, std::size_t index)
{
	for (const auto &w : proof.expectedTriangles) if (w.triangleIndex == index) return w.triangle;
	throw std::invalid_argument("Native fence proof primary pair is not in its exact bar witness");
}

inline bool SamePanels(const std::vector<FrameTriangle> &a, const std::vector<FrameTriangle> &b)
{
	if (a.size() != b.size()) return false;
	for (std::size_t i = 0; i < a.size(); ++i)
		if (!native_wall_runtime::detail::SameTriangle(a[i], b[i])) return false;
	return true;
}

/** The original seven-box Fence has full-width outer rails and thinner
 * vertical bars. Its last bar extends past the rail tangent endpoint; bounds
 * of all84 triangles therefore are not the rail's rectangle bounds. Resolve
 * the span from outer-plane vertices ONLY, after matching every ordered
 * physical vertex/UV to the immutable technical module and original transform.
 * This reads existing mesh metadata; it never builds a scene/Frame or changes
 * physical geometry. Shared by known selection and diagnostic revalidation.
 */
struct OuterRailBounds {
	float minNormal, maxNormal, minTangent, maxTangent;
};

inline OuterRailBounds OriginalOuterRailBounds(const TargetGeometry &proof, std::uint8_t rawMega)
{
	Require((rawMega == 35 || rawMega == 36) && proof.expectedTriangles.size() == 84,
	    "Native fence rail authority requires all84 original RAW35/36 triangles");
	const auto &module = TechnicalModules().at(static_cast<std::size_t>(ModuleKind::Fence));
	Require(module.kind == ModuleKind::Fence && module.revision == 1 && module.indices.size() == 84 * 3,
	    "Native fence rail authority requires the original seven-box module revision");
	const auto [originX, originZ] = Origin(proof);
	Instance original;
	original.translation = { originX + 0.5F, 0, originZ + 0.5F };
	original.quarterTurns = rawMega == 35 ? 1 : 0;
	const unsigned normalAxis = rawMega == 35 ? 0U : 2U, tangentAxis = 2U - normalAxis;
	const float infinity = std::numeric_limits<float>::infinity();
	OuterRailBounds bounds { infinity, -infinity, infinity, -infinity };
	for (std::size_t i = 0; i < proof.expectedTriangles.size(); ++i) {
		const auto &triangle = proof.expectedTriangles[i].triangle;
		for (std::size_t corner = 0; corner < 3; ++corner) {
			const auto &local = module.vertices.at(module.indices[i * 3 + corner]);
			const auto &actual = triangle.vertices[corner];
			Require(wall_face_plan::detail::Same(actual.position, Transform(original, local.position))
			        && actual.u == local.u && actual.v == local.v,
			    "Native fence rail witness differs from original ordered physical module geometry/UVs");
			const float normal = native_wall_runtime::detail::At(actual.position, normalAxis);
			bounds.minNormal = std::min(bounds.minNormal, normal); bounds.maxNormal = std::max(bounds.maxNormal, normal);
		}
	}
	for (const auto &witness : proof.expectedTriangles) for (const auto &vertex : witness.triangle.vertices) {
		const float normal = native_wall_runtime::detail::At(vertex.position, normalAxis);
		if (normal != bounds.minNormal && normal != bounds.maxNormal) continue;
		const float tangent = native_wall_runtime::detail::At(vertex.position, tangentAxis);
		bounds.minTangent = std::min(bounds.minTangent, tangent); bounds.maxTangent = std::max(bounds.maxTangent, tangent);
	}
	Require(bounds.minNormal < bounds.maxNormal && bounds.minTangent < bounds.maxTangent,
	    "Native fence original outer rail has no complete bounded span");
	return bounds;
}

inline std::vector<FrameTriangle> PanelsGeometry(const TargetGeometry &proof, std::uint8_t rawMega, bool nativeOperators = false)
{
	Require((rawMega == 35 || rawMega == 36) && proof.epoch != 0 && proof.geometryRevision != 0,
	    "Native fence geometry requires a bounded original native family/epoch");
	const auto [originX, originZ] = Origin(proof);
	const auto &record = proof.expectedSurface;
	Require(record.instanceId == proof.instanceId && record.sourceInstanceId == proof.instanceId
	        && record.module == ModuleKind::Fence && record.sourceModule == ModuleKind::Fence
	        && record.provenanceResolved && record.sources.size() == 4
	        && static_cast<unsigned>(record.policy) <= static_cast<unsigned>(FrameSurfacePolicy::ConservativeNativeCutaway)
	        && (nativeOperators || record.policy == FrameSurfacePolicy::Opaque)
	        && proof.instanceId >> 48 == static_cast<std::uint64_t>(ModuleKind::Fence) + 1
	        && (proof.instanceId & 255U) == 0 && ((proof.instanceId >> 32) & 65535U) == 0,
	    "Native fence geometry requires one original Fence and its four exact native sources");
	Require(proof.expectedTriangles.size() == 84 && proof.primaryPlaneTriangleIndices[0] < proof.primaryPlaneTriangleIndices[1],
	    "Native fence requires every original seven-box triangle and an ordered physical pair");
	const NativeTextureAxis axis = rawMega == 35 ? NativeTextureAxis::AlongZ : NativeTextureAxis::AlongX;
	const unsigned column = rawMega == 35 ? 0U : 1U, normalAxis = rawMega == 35 ? 0U : 2U, tangentAxis = 2U - normalAxis;
	std::array<bool, 4> seenSources {};
	FrameSurfacePolicy derivedPolicy = FrameSurfacePolicy::Opaque;
	for (const auto &s : record.sources) {
		Require(s.x >= originX && s.x <= originX + 1 && s.z >= originZ && s.z <= originZ + 1,
		    "Native fence source is not one of the exact macro's four cells");
		const unsigned local = static_cast<unsigned>((s.z - originZ) * 2 + s.x - originX);
		Require(!seenSources[local], "Native fence contains duplicate source cells"); seenSources[local] = true;
		const auto &cell = proof.expectedSourceCells[local];
		Require(s.piece == cell.piece && s.properties == cell.properties && s.transparencyGroup == cell.transparency
		        && s.blendActive == (s.groupActive && (cell.properties & wall_composition::SolTransparent) != 0)
		        && (nativeOperators ? s.microMasksKnown : !s.blendActive)
		        && cell.object == 0 && cell.light <= 15 && cell.special == 0,
		    "Native fence source/palette/object context is not the exact bounded target");
		if (s.blendActive) {
			if (s.partialMask != 0) derivedPolicy = FrameSurfacePolicy::ConservativeNativeCutaway;
			else if (derivedPolicy == FrameSurfacePolicy::Opaque) derivedPolicy = FrameSurfacePolicy::NativePaletteBlend;
		}
	}
	Require(derivedPolicy == record.policy, "Native fence aggregate policy differs from its exact source operators");
	const auto rail = OriginalOuterRailBounds(proof, rawMega);
	std::size_t previous = 0;
	for (std::size_t i = 0; i < proof.expectedTriangles.size(); ++i) {
		const auto &w = proof.expectedTriangles[i]; const auto &t = w.triangle;
		Require((i == 0 || previous < w.triangleIndex) && t.instanceId == proof.instanceId && t.module == ModuleKind::Fence
		        && t.policy == record.policy && t.transparent == (record.policy != FrameSurfacePolicy::Opaque) && !t.visualSuppressed
		        && t.nativeTexture.kind == NativeTextureKind::Technical && t.nativeTexture.layout == NativeTextureLayout::RepeatedBand
		        && t.nativeTexture.operationPass == NativeTextureOperationPass::Unpartitioned
		        && t.pick.kind == PickKind::Architecture && t.pick.x == originX && t.pick.z == originZ && t.pick.nativeSlot == -1
		        && t.light == proof.expectedSourceCells[0].light && (proof.expectedSourceCells[0].flags & 2U) != 0,
		    "Native fence proof contains a changed/suppressed/nonoriginal bar triangle");
		previous = w.triangleIndex;
		wall_face_plan::detail::CheckVertices(t.vertices); wall_face_plan::detail::CheckVertices(t.textureVertices);
	}
	const std::array<FrameTriangle, 2> physical { WitnessAt(proof, proof.primaryPlaneTriangleIndices[0]), WitnessAt(proof, proof.primaryPlaneTriangleIndices[1]) };
	wall_face_plan::Reference a, b; a.triangle = physical[0]; b.triangle = physical[1];
	Require(wall_face_plan::detail::RectanglePair(a, b, axis), "Native fence nominated physical pair must be one primary-axis rectangle");
	const float measuredPlane = native_wall_runtime::detail::At(physical[0].vertices[0].position, normalAxis);
	Require(measuredPlane == rail.minNormal || measuredPlane == rail.maxNormal,
	    "Native fence nominated plane must be an original outer rail surface");
	float pairMin = std::numeric_limits<float>::infinity(), pairMax = -pairMin;
	for (const auto &t : physical) for (const auto &v : t.vertices) {
		pairMin = std::min(pairMin, native_wall_runtime::detail::At(v.position, tangentAxis));
		pairMax = std::max(pairMax, native_wall_runtime::detail::At(v.position, tangentAxis));
	}
	Require(pairMin == rail.minTangent && pairMax == rail.maxTangent, "Native fence proof must nominate a complete original rail face, not a bar/end/cap");
	std::vector<FrameTriangle> panels;
	std::set<std::pair<int, int>> usedColumns;
	Require(proof.columnDeclarations.size() == 2, "Native fence complete replacement requires its two own nonfloor columns");
	for (const auto &declaration : proof.columnDeclarations) {
		Require(declaration.receiptSha256.empty() && declaration.epoch == proof.epoch && declaration.column == column,
		    "Native fence column metadata must be owned and same-epoch");
		const auto &source = declaration.source;
		Require(source.sourceX >= originX && source.sourceX <= originX + 1 && source.sourceZ >= originZ && source.sourceZ <= originZ + 1
		        && source.activeMicroTileLength == 10 && usedColumns.emplace(source.sourceX, source.sourceZ).second,
		    "Native fence column evidence is outside its target or duplicated");
		const auto &cell = proof.expectedSourceCells[static_cast<std::size_t>((source.sourceZ - originZ) * 2 + source.sourceX - originX)];
		Require(source.piece == cell.piece && source.sol == cell.properties && source.transparencyGroup == cell.transparency,
		    "Native fence evidence differs from its full native source-cell witness");
		const auto plan = wall_composition::BuildCellPlan(source);
		Require(!plan.nativeIsFloor && (nativeOperators || !plan.nativeWallTransparency) && !plan.columns[column].noPresentUpperWords,
		    "Native fence column must supply its own nonfloor upper/base, never donor/floor");
		if (!nativeOperators) for (const auto &layer : plan.columns[column].layers) for (const auto &op : layer.operations)
			Require(op.nativeMaskMode == wall_composition::NativeMaskMode::Solid,
			    "Diagnostic native fence cannot flatten partial/palette-blend native operators into texture coverage");
		std::uint8_t partialMask = 0;
		const unsigned leftType = (source.words[0] >> 12) & 7U, rightType = (source.words[1] >> 12) & 7U;
		if (source.words[0] != 0 && (leftType == 1 || leftType == 4))
			partialMask = static_cast<std::uint8_t>(partialMask | (source.sol & wall_composition::SolTransparentLeft));
		if (source.words[1] != 0 && (rightType == 1 || rightType == 5))
			partialMask = static_cast<std::uint8_t>(partialMask | (source.sol & wall_composition::SolTransparentRight));
		for (const auto &s : record.sources) if (s.x == source.sourceX && s.z == source.sourceZ)
			Require(s.groupActive == source.groupActive && s.properties == source.sol && s.piece == source.piece
			        && s.blendActive == plan.nativeWallTransparency && s.microMasksKnown
			        && s.partialMask == partialMask,
			    "Native fence column native transparency context differs from its surface record");
		const auto operations = nativeOperators ? native_operators::BuildFullColumnOperationMap(source, column)
		                                       : native_operators::FullColumnOperationMap {};
		Require(!nativeOperators || operations.hasSolid || operations.hasPaletteBlend,
		    "Native fence own full column has no supported native draw operation");
		native_wall_projection::Plane plane { source.sourceX, source.sourceZ,
			axis == NativeTextureAxis::AlongX ? native_wall_projection::Axis::AlongX : native_wall_projection::Axis::AlongZ,
			column, measuredPlane - static_cast<float>(axis == NativeTextureAxis::AlongX ? source.sourceZ : source.sourceX), { 0, 5 }, false };
		const auto visual = native_wall_runtime::detail::VisualTriangles(physical, plane);
		const auto appendPass = [&](NativeTextureOperationPass pass) {
			for (std::size_t i = 0; i < 2; ++i) {
				FrameTriangle panel = physical[i]; panel.vertices = visual[i]; panel.textureVertices = visual[i];
				panel.light = cell.light; // Native DrawCell shades this source cell, not the macro's physical owner.
				panel.policy = pass == NativeTextureOperationPass::PaletteBlend ? FrameSurfacePolicy::NativePaletteBlend : FrameSurfacePolicy::Opaque;
				panel.transparent = panel.policy != FrameSurfacePolicy::Opaque;
				panel.nativeTexture = { NativeTextureKind::Masonry, axis, source.sourceX, source.sourceZ, source.piece, -1,
					static_cast<std::uint8_t>(column), true, NativeTextureLayout::OriginalFullColumn, pass };
				panels.push_back(panel);
			}
		};
		if (!nativeOperators) appendPass(NativeTextureOperationPass::Unpartitioned);
		else {
			if (operations.hasSolid) appendPass(NativeTextureOperationPass::Solid);
			if (operations.hasPaletteBlend) appendPass(NativeTextureOperationPass::PaletteBlend);
		}
	}
	return panels;
}

inline std::vector<FrameTriangle> Panels(const TargetProof &proof, std::uint8_t rawMega)
{
	Require(native_cell_art::IsLowerSha256(proof.receiptSha256) && proof.ownerVerifiedTargetBundle
	        && proof.nativeBaseFloorContactCompared && proof.completeTechnicalFenceReplacementCompared,
	    "Diagnostic native fence requires exact verified full-column/ground-contact/replacement evidence");
	const auto [x, z] = Origin(proof);
	for (const auto &declaration : proof.columnDeclarations) {
		Require(declaration.declaredClass == native_cell_art::CellArtClass::FenceOrGrille,
		    "Diagnostic native fence declaration must explicitly identify FenceOrGrille");
		auto view = declaration; view.receiptSha256 = proof.receiptSha256;
		const native_cell_art::NativeCellContext context { proof.epoch, rawMega, x, z, true, 0 };
		const auto c = native_cell_art::ClassifyCell(declaration.source, context, {}, view);
		Require(c.declaredCompleteVisualEvidence && c.sourceCodePredicateEligible
		        && c.classification == native_cell_art::CellArtClass::FenceOrGrille
		        && c.authority == native_cell_art::ClassAuthority::OwnerDeclaredIndependentVisualReference,
		    "Diagnostic native fence lacks independent pixels/coverage/masks/object occlusion declaration");
	}
	return PanelsGeometry(proof, rawMega);
}

inline const TargetGeometry &Geometry(const TargetUpdate &target)
{
	return target.nativeRule ? static_cast<const TargetGeometry &>(*target.nativeRule) : static_cast<const TargetGeometry &>(target.proof);
}

inline std::vector<FrameTriangle> KnownPanels(const NativeRuleTarget &target)
{
	const auto &w = target.witness;
	const auto [x, z] = Origin(target);
	Require(w.loadedNativeTilMappingVerified && w.epoch == target.epoch && w.originX == x && w.originZ == z
	        && (w.rawMega == 35 || w.rawMega == 36), "Native known fence lacks loaded TIL/raw/source authority");
	for (unsigned i = 0; i < 4; ++i)
		Require(w.zeroBasedNativePieces[i] == target.expectedSourceCells[i].piece,
		    "Native known fence loaded TIL child differs from its exact source-cell witness");
	for (const auto &decl : target.columnDeclarations)
		Require(decl.declaredClass == native_cell_art::CellArtClass::Unknown && !decl.ownerVerifiedReceipt
		        && !decl.originalDrawCellReference && !decl.independentCoverage && !decl.rawIdentityIndices
		        && !decl.originalNativeMasksCompared && !decl.physicalWitnessUnchanged && !decl.objectSpritesAndLightsPreserved
		        && !decl.exactTargetVisualReplacementCompared && !decl.nativeInterCellAndObjectOcclusionCompared
		        && decl.comparedPixels == 0 && decl.coveredPixels == 0 && decl.uncoveredPixels == 0,
		    "Native known fence must not fabricate independent visual proof");
	return PanelsGeometry(target, w.rawMega, true);
}

inline std::vector<FrameTriangle> ProposedPanels(const TargetUpdate &target)
{
	return target.nativeRule ? KnownPanels(*target.nativeRule) : Panels(target.proof, target.rawMega);
}

} // namespace detail

/** No proofs -> no targets and no suppression. This plans only the explicitly
 * proven original RAW35/36 instances already selected in the supplied frame.
 * It does not generate a map/Frame, decode pixels, classify a whole floor or
 * suppress nearby Walls/Corner/candelabra. No target admits blended operators.
 */
inline Updates BuildFencePanels(const Snapshot &snapshot, const PilotFrame &frame, unsigned actualMicroTileLength,
    std::span<const TargetProof> proofs)
{
	using detail::Require;
	wall_face_plan::detail::ValidateSnapshot(snapshot);
	Require(actualMicroTileLength == 10 && frame.gameRevision == snapshot.gameRevision && frame.geometryRevision != 0
	        && frame.regions > 0 && frame.regions <= 9 && proofs.size() <= 32,
	    "Native fence unsupported MicroLen/epoch/frame/target bound");
	Updates out;
	out.epoch = frame.gameRevision; out.geometryRevision = frame.geometryRevision; out.presentationSignature = frame.presentationSignature;
	out.regionX = frame.regionX; out.regionZ = frame.regionZ; out.regions = frame.regions;
	out.triangleCount = frame.triangles.size(); out.nativeSurfaceCount = frame.nativeSurfaces.size();
	out.requiresNativeFallback = frame.requiresNativeFallback; out.fallbackReasons = frame.fallbackReasons;
	out.expectedExistingPanels = frame.nativeArtPanels;
	std::set<std::uint64_t> usedTargets;
	std::set<wall_face_plan::RawKey> rawKeys;
	for (const auto &proof : proofs) {
		Require(proof.epoch == out.epoch && proof.geometryRevision == out.geometryRevision && proof.presentationSignature == out.presentationSignature
		        && usedTargets.insert(proof.instanceId).second, "Native fence proof is stale or target duplicated");
		const auto [x, z] = detail::Origin(proof);
		const auto raw = snapshot.mega[static_cast<std::size_t>((z - 16) / 2) * 40 + (x - 16) / 2];
		std::size_t records = 0;
		for (const auto &record : frame.nativeSurfaces) if (record.instanceId == proof.instanceId) {
			++records; wall_face_plan::detail::ValidateSurface(snapshot, record);
			Require(native_wall_runtime::detail::SameSurface(record, proof.expectedSurface), "Native fence proof source record differs from frame");
		}
		Require(records == 1, "Native fence proof requires exactly one selected surface record");
		for (unsigned dz = 0; dz < 2; ++dz) for (unsigned dx = 0; dx < 2; ++dx)
			Require(snapshot.At(x + dx, z + dz) == proof.expectedSourceCells[dz * 2 + dx], "Native fence source-cell/light/object witness changed");
		std::size_t matches = 0;
		for (std::size_t index = 0; index < frame.triangles.size(); ++index) if (frame.triangles[index].instanceId == proof.instanceId) {
			Require(matches < proof.expectedTriangles.size() && proof.expectedTriangles[matches].triangleIndex == index
			        && native_wall_runtime::detail::SameTriangle(frame.triangles[index], proof.expectedTriangles[matches].triangle),
			    "Native fence proof does not match every exact physical/pick/light bar triangle");
			wall_face_plan::detail::ValidateTriangle(snapshot, frame.triangles[index], proof.expectedSurface); ++matches;
		}
		Require(matches == proof.expectedTriangles.size(), "Native fence proof contains absent/extra triangle indices");
		for (const auto &panel : frame.nativeArtPanels) Require(panel.instanceId != proof.instanceId, "Native fence target already has visual panels");
		for (const auto &decl : proof.columnDeclarations)
			Require(decl.source.piece < snapshot.micros.size() && decl.source.words == snapshot.micros[decl.source.piece],
			    "Native fence column MIN words differ from the immutable snapshot");
		TargetUpdate target; target.proof = proof; target.rawMega = raw; target.panels = detail::Panels(proof, raw);
		for (const auto &decl : proof.columnDeclarations) rawKeys.insert({ out.epoch, decl.source.piece,
			static_cast<std::uint8_t>(decl.column), raw == 35 ? NativeTextureAxis::AlongZ : NativeTextureAxis::AlongX,
			wall_face_plan::RawLayoutRole::FullColumn, 10, false, decl.source.words });
		out.addedPanels += target.panels.size(); out.suppressedTechnicalTriangles += proof.expectedTriangles.size();
		out.targets.push_back(std::move(target));
	}
	out.uniqueFullColumnKeys = rawKeys.size(); out.estimatedHostRawBytes = static_cast<std::uint64_t>(rawKeys.size()) * 32 * 160 * 2;
	return out;
}

/** Deterministic finite native rule, independent of seed/image receipts. Only
 * loaded RAW35/36 with all four exact TIL children qualifies. New atlas remains
 * native full32x160 with coverage supplied by the original raw decoder; pixels
 * are not read here. Kappa is an existing outer technical rail plane chosen by
 * lowest original triangle pair index; it is never claimed as native3Ddepth or
 * native base/floor fit. Principal still performs native/visual host QA.
 * Native base/upper/column operators partition only derivative draw coverage;
 * raw coverage and all original bar/source policy fields remain unchanged.
 * Object/special contexts explicitly request complete native fallback before
 * ANY application/submission. Hidden/unselected instances
 * simply remain untouched. No auto-proof or receipt/SHA is constructed.
 */
inline Updates BuildKnownNativeFencePanels(const Snapshot &snapshot, const PilotFrame &frame,
    unsigned actualMicroTileLength, std::span<const NativeMegaWitness> witnesses)
{
	using detail::Require;
	wall_face_plan::detail::ValidateSnapshot(snapshot);
	Require(actualMicroTileLength == 10 && frame.gameRevision == snapshot.gameRevision && frame.geometryRevision != 0
	        && frame.regions > 0 && frame.regions <= 9 && witnesses.size() <= 256,
	    "Native known fence unsupported MicroLen/epoch/frame/witness bound");
	Updates out;
	out.epoch = frame.gameRevision; out.geometryRevision = frame.geometryRevision; out.presentationSignature = frame.presentationSignature;
	out.regionX = frame.regionX; out.regionZ = frame.regionZ; out.regions = frame.regions;
	out.triangleCount = frame.triangles.size(); out.nativeSurfaceCount = frame.nativeSurfaces.size();
	out.requiresNativeFallback = frame.requiresNativeFallback; out.fallbackReasons = frame.fallbackReasons;
	out.expectedExistingPanels = frame.nativeArtPanels;
	std::set<std::pair<int, int>> usedOrigins;
	std::set<wall_face_plan::RawKey> rawKeys;
	for (const auto &w : witnesses) {
		Require(w.loadedNativeTilMappingVerified && w.epoch == out.epoch && (w.rawMega == 35 || w.rawMega == 36)
		        && wall_face_plan::detail::Active(w.originX, w.originZ) && (w.originX & 1) == 0 && (w.originZ & 1) == 0
		        && usedOrigins.emplace(w.originX, w.originZ).second,
		    "Native known fence requires unique exact loaded RAW35/36 TIL witness");
		const auto raw = snapshot.mega[static_cast<std::size_t>((w.originZ - 16) / 2) * 40 + (w.originX - 16) / 2];
		Require(raw == w.rawMega, "Native known fence raw mega differs from snapshot");
		NativeRuleTarget state; state.witness = w;
		state.epoch = out.epoch; state.geometryRevision = out.geometryRevision; state.presentationSignature = out.presentationSignature;
		for (unsigned i = 0; i < 4; ++i) {
			state.expectedSourceCells[i] = snapshot.At(w.originX + static_cast<int>(i & 1U), w.originZ + static_cast<int>(i >> 1U));
			Require(state.expectedSourceCells[i].piece == w.zeroBasedNativePieces[i], "Native known fence loaded TIL children differ from actual snapshot cells");
		}
		std::size_t records = 0;
		for (const auto &r : frame.nativeSurfaces) if (r.module == ModuleKind::Fence) {
			const int x = static_cast<int>((r.instanceId >> 8) & 65535U) - 128, z = static_cast<int>((r.instanceId >> 24) & 255U) - 128;
			if (x != w.originX || z != w.originZ) continue;
			++records; wall_face_plan::detail::ValidateSurface(snapshot, r); state.expectedSurface = r; state.instanceId = r.instanceId;
		}
		Require(records <= 1, "Native known fence has duplicate selected surface records");
		if (records == 1) for (std::size_t i = 0; i < frame.triangles.size(); ++i) if (frame.triangles[i].instanceId == state.instanceId) {
			wall_face_plan::detail::ValidateTriangle(snapshot, frame.triangles[i], state.expectedSurface);
			state.expectedTriangles.push_back({ i, frame.triangles[i] });
		}
		if (state.expectedTriangles.empty()) { out.skippedNativeRules.push_back({ w, NativeRuleSkipReason::NotSelected }); continue; }
		if (std::any_of(state.expectedSourceCells.begin(), state.expectedSourceCells.end(), [](const Cell &c) { return c.object != 0 || c.special != 0; })) {
			out.requiresCompleteNativeFallback = true;
			out.skippedNativeRules.push_back({ w, NativeRuleSkipReason::NativeObjectOrSpecialUnsupported }); continue;
		}
		const NativeTextureAxis axis = raw == 35 ? NativeTextureAxis::AlongZ : NativeTextureAxis::AlongX;
		const unsigned normalAxis = raw == 35 ? 0U : 2U, tangentAxis = 2U - normalAxis, column = raw == 35 ? 0U : 1U;
		const auto rail = detail::OriginalOuterRailBounds(state, raw);
		bool found = false;
		for (std::size_t a = 0; a < state.expectedTriangles.size() && !found; ++a) for (std::size_t b = a + 1; b < state.expectedTriangles.size(); ++b) {
			wall_face_plan::Reference ra, rb; ra.triangle = state.expectedTriangles[a].triangle; rb.triangle = state.expectedTriangles[b].triangle;
			if (!wall_face_plan::detail::RectanglePair(ra, rb, axis)) continue;
			const float plane = native_wall_runtime::detail::At(ra.triangle.vertices[0].position, normalAxis);
			if (plane != rail.minNormal && plane != rail.maxNormal) continue;
			float low = std::numeric_limits<float>::infinity(), high = -low;
			for (const auto *t : { &ra.triangle, &rb.triangle }) for (const auto &v : t->vertices) {
				const float tangent = native_wall_runtime::detail::At(v.position, tangentAxis); low = std::min(low, tangent); high = std::max(high, tangent);
			}
			if (low != rail.minTangent || high != rail.maxTangent) continue;
			state.primaryPlaneTriangleIndices = { state.expectedTriangles[a].triangleIndex, state.expectedTriangles[b].triangleIndex }; found = true; break;
		}
		Require(found, "Native known fence has no complete original outer rail rectangle");
		for (unsigned i = 0; i < 4; ++i) {
			const auto &cell = state.expectedSourceCells[i];
			Require(cell.piece < snapshot.micros.size(), "Native known fence lacks original loaded MIN words");
			wall_composition::SourceCellMetadata source { w.originX + static_cast<int>(i & 1U), w.originZ + static_cast<int>(i >> 1U),
				cell.piece, snapshot.micros[cell.piece], 10, cell.properties, cell.transparency, snapshot.activeTransparency[cell.transparency] };
			const auto plan = wall_composition::BuildCellPlan(source);
			if (plan.nativeIsFloor || plan.columns[column].noPresentUpperWords) continue;
			// This preflight resolves native operators from original words/SOL/TransList.
			// It never changes raw coverage or accepts an aggregate mixed policy by itself.
			(void)native_operators::BuildFullColumnOperationMap(source, column);
			native_cell_art::NativeVisualEvidence metadata; metadata.epoch = out.epoch; metadata.source = source; metadata.column = column;
			state.columnDeclarations.push_back(metadata); // No flags/receipt/pixel counters are asserted.
		}
		TargetUpdate target; target.rawMega = raw; target.nativeRule = std::move(state);
		target.panels = detail::KnownPanels(*target.nativeRule);
		for (const auto &decl : target.nativeRule->columnDeclarations) rawKeys.insert({ out.epoch, decl.source.piece,
			static_cast<std::uint8_t>(column), axis, wall_face_plan::RawLayoutRole::FullColumn, 10, false, decl.source.words });
		out.addedPanels += target.panels.size(); out.suppressedTechnicalTriangles += target.nativeRule->expectedTriangles.size();
		out.targets.push_back(std::move(target));
	}
	out.uniqueFullColumnKeys = rawKeys.size(); out.estimatedHostRawBytes = static_cast<std::uint64_t>(rawKeys.size()) * 32 * 160 * 2;
	return out;
}

/** Revalidate the complete target bundle before any write. All vector growth
 * happens in a private copy; commit is a nothrow vector swap and bool stores.
 * Physical vertices/picks/light/IDs/native sources/collision and existing art
 * panels retain exact identity. Renderer must skip suppressed bars for color,
 * raster depth and pixel picking, then submit these separate covered art panels
 * through native opaque or exact palette-blend passes and frame-atomicity/cache
 * budgets. The blended pass uses the existing read-only depth/pick operator.
 */
inline void ApplyFencePanels(PilotFrame &frame, const Updates &updates)
{
	using detail::Require;
	Require(updates.policyRevision == PolicyRevision && updates.packingRevision == NativeFullColumnPackingRevision
	        && updates.activeMicroTileLength == 10 && updates.targets.size() <= 256 && !updates.requiresCompleteNativeFallback
	        && frame.gameRevision == updates.epoch && frame.geometryRevision == updates.geometryRevision
	        && frame.presentationSignature == updates.presentationSignature && frame.regionX == updates.regionX && frame.regionZ == updates.regionZ
	        && frame.regions == updates.regions && frame.triangles.size() == updates.triangleCount && frame.nativeSurfaces.size() == updates.nativeSurfaceCount
	        && frame.requiresNativeFallback == updates.requiresNativeFallback && frame.fallbackReasons == updates.fallbackReasons
	        && detail::SamePanels(frame.nativeArtPanels, updates.expectedExistingPanels), "Native fence apply stale/unsupported frame identity");
	if (updates.targets.empty()) return;
	static_assert(noexcept(frame.nativeArtPanels.swap(frame.nativeArtPanels)));
	std::set<std::uint64_t> usedTargets;
	std::set<std::size_t> usedTriangles;
	auto nextPanels = frame.nativeArtPanels;
	for (const auto &target : updates.targets) {
		const auto &proof = detail::Geometry(target);
		Require(proof.epoch == updates.epoch && proof.geometryRevision == updates.geometryRevision
		        && proof.presentationSignature == updates.presentationSignature && usedTargets.insert(proof.instanceId).second,
		    "Native fence apply stale/duplicate target proof");
		Require(detail::SamePanels(target.panels, detail::ProposedPanels(target)), "Native fence apply proposed panel geometry was changed");
		std::size_t records = 0;
		for (const auto &record : frame.nativeSurfaces) if (record.instanceId == proof.instanceId) {
			++records; Require(native_wall_runtime::detail::SameSurface(record, proof.expectedSurface), "Native fence apply target record changed");
		}
		Require(records == 1, "Native fence apply target native record is absent/duplicated");
		std::size_t currentTriangles = 0;
		for (const auto &triangle : frame.triangles) if (triangle.instanceId == proof.instanceId) ++currentTriangles;
		Require(currentTriangles == proof.expectedTriangles.size(), "Native fence apply target completeness changed");
		for (const auto &w : proof.expectedTriangles) Require(w.triangleIndex < frame.triangles.size() && usedTriangles.insert(w.triangleIndex).second
		        && native_wall_runtime::detail::SameTriangle(frame.triangles[w.triangleIndex], w.triangle), "Native fence apply stale/duplicate bar target");
		for (const auto &panel : nextPanels) Require(panel.instanceId != proof.instanceId, "Native fence apply target art already exists");
		nextPanels.insert(nextPanels.end(), target.panels.begin(), target.panels.end());
	}
	frame.nativeArtPanels.swap(nextPanels);
	for (const auto &target : updates.targets) for (const auto &w : detail::Geometry(target).expectedTriangles)
		frame.triangles[w.triangleIndex].visualSuppressed = true;
}

} // namespace devilution::cathedral::native_fence_runtime
