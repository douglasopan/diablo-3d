#pragma once

// PRIVATE SOURCE-ONLY CANDIDATE. C++/native decode/Frame/GPU execution UNKNOWN.
// Plain immutable metadata and raw assembly only; no Source/global/loader/RNG,
// resource, IO, palette, object animation, native light or cache mutation.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "native-wall-composition.hpp" // Frozen, std-only metadata/shape contract.

namespace devilution::cathedral::native_cell_art {

namespace composition = wall_composition;
inline constexpr std::uint32_t SourcePolicyRevision = 1;
inline constexpr const char *SourceArtifactStatus = "SOURCE_ONLY_CPP_EXECUTION_UNKNOWN";

enum class CellArtClass : std::uint8_t {
	Unknown, Masonry, FenceOrGrille, ArchOrOpening, NativeDecoration, NativeLightFire
};
enum class NativeMegaFamily : std::uint8_t {
	Unknown, NamedWall, NamedFence, NamedArch, MixedStructure, NamedPillar, LampFootprint
};
enum class ClassAuthority : std::uint8_t {
	None, NativeObjectAndPieceRule, OwnerDeclaredIndependentVisualReference
};

/** One ALREADY captured native object. authoritativeNativeRecord means the
 * owner actually checked ActiveObjects/Objects plus signed dObject membership
 * in this epoch. It is a declaration to be bound by the external receipt, not
 * proof manufactured by this header. ObjectDataFlags::Light is applyLighting
 * on the sprite, NOT an emitter flag. Native emitterRuleRadius comes from the
 * native AddObjectLight/UpdateObjectLight rule/state, never from that data bit.
 * Rendering and lights must remain native even when technical Wall art changes.
 */
struct NativeObjectMetadata {
	std::uint64_t epoch = 0;
	int slot = -1, sourceX = 0, sourceZ = 0;
	int originX = 0, originZ = 0;
	std::int16_t nativeType = -1;
	std::int8_t signedOccupancy = 0;
	bool active = false, authoritativeNativeRecord = false;
	bool nativeEmitterRuleMatched = false;
	int emitterRuleRadius = 0;
	bool appliesLighting = false, preDraw = false, nativeSolid = false;
	int animationFrame = 0, nativeLightId = -1;
};

/** A raw macro ID is semantic only under the actual Cathedral TIL expansion.
 * NativeMegaPieceMappingObserved is supplied by the owner after confirming the
 * source cell/piece is this mega's real child in the loaded epoch. BaseTypes /
 * TileDecorations must not replace rawMega: those native tables collapse fences,
 * walls and arches for other purposes. This does not classify visible pixels.
 */
struct NativeCellContext {
	std::uint64_t epoch = 0;
	std::uint8_t rawMega = 0;
	int megaOriginX = 0, megaOriginZ = 0;
	bool nativeMegaPieceMappingObserved = false;
	std::int8_t signedObjectOccupancy = 0;
};

/** All proof fields default to absent. The owner must bind the exact original
 * native DrawCell reference, source/words/SOL/TransList, object/native-light
 * records and physical witness in a receipt. A valid SHA string alone is not
 * integrity verification. This header checks declarations/context consistency
 * and always leaves runtimeSuppressionAdmissionGranted=false; the principal
 * still owns acceptance, target-scoped visual application and runtime gates.
 */
struct NativeVisualEvidence {
	std::string_view receiptSha256;
	std::uint64_t epoch = 0;
	composition::SourceCellMetadata source;
	unsigned column = 0;
	CellArtClass declaredClass = CellArtClass::Unknown;
	bool ownerVerifiedReceipt = false, originalDrawCellReference = false;
	bool independentCoverage = false, rawIdentityIndices = false;
	bool originalNativeMasksCompared = false, physicalWitnessUnchanged = false;
	bool objectSpritesAndLightsPreserved = false, exactTargetVisualReplacementCompared = false;
	bool nativeInterCellAndObjectOcclusionCompared = false;
	std::size_t comparedPixels = 0, coveredPixels = 0, uncoveredPixels = 0;
};

struct CellArtClassification {
	CellArtClass classification = CellArtClass::Unknown;
	ClassAuthority authority = ClassAuthority::None;
	NativeMegaFamily megaFamily = NativeMegaFamily::Unknown;
	bool nativeIsFloor = false, nativeWallTransparency = false;
	bool hasUpperWords = false, hasBaseWords = false;
	bool nativeObjectPresent = false, corroboratedCathedralLight = false;
	// A source-code candidate gate, not proof that an opaque box belongs here.
	bool sourceCodePredicateEligible = false;
	bool evidenceRequired = true, declaredCompleteVisualEvidence = false;
	bool runtimeSuppressionAdmissionGranted = false;
	bool preserveNativeObjects = true, preserveNativeLights = true, preserveNativePhysics = true;
};

inline bool IsLowerSha256(std::string_view text)
{
	return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
	});
}

/** Only names explicitly present in native drlg_l1.cpp Tile are classified.
 * Unknown decorated variants remain Unknown; no inferred MIN-type whitelist.
 * A named macro family describes the macro, not all four TIL child surfaces.
 */
inline NativeMegaFamily ReadNamedNativeMegaFamily(std::uint8_t raw)
{
	switch (raw) {
	case 1: case 2: case 3: case 4: case 6: case 7: case 16: case 17:
	case 41: case 43: case 79: case 80: case 82: case 84: case 89: case 90: case 91: case 92:
	case 100: case 146:
		return NativeMegaFamily::NamedWall;
	case 29: case 32: case 35: case 36:
		return NativeMegaFamily::NamedFence;
	case 5: case 8: case 9: case 11: case 12: case 33: case 147:
		return NativeMegaFamily::NamedArch;
	case 10: case 14: case 27: case 28: case 37: case 38: case 39: case 40: case 42:
		return NativeMegaFamily::MixedStructure;
	case 15:
		return NativeMegaFamily::NamedPillar;
	case 128: case 129: case 130:
		return NativeMegaFamily::LampFootprint; // LAMPS miniset, not a Wall family.
	default:
		return NativeMegaFamily::Unknown;
	}
}

inline bool SameSource(const composition::SourceCellMetadata &a, const composition::SourceCellMetadata &b)
{
	return a.sourceX == b.sourceX && a.sourceZ == b.sourceZ && a.piece == b.piece && a.words == b.words
	    && a.activeMicroTileLength == b.activeMicroTileLength && a.sol == b.sol
	    && a.transparencyGroup == b.transparencyGroup && a.groupActive == b.groupActive;
}

inline CellArtClassification ClassifyCell(const composition::SourceCellMetadata &source,
    const NativeCellContext &context, std::span<const NativeObjectMetadata> objects,
    const NativeVisualEvidence &evidence = {})
{
	if (static_cast<unsigned>(evidence.declaredClass) > static_cast<unsigned>(CellArtClass::NativeLightFire))
		throw std::invalid_argument("Native cell-art visual declaration contains unknown class enum");
	const auto plan = composition::BuildCellPlan(source);
	if (context.epoch == 0 || context.rawMega > 206 || context.megaOriginX < 16 || context.megaOriginZ < 16
	    || context.megaOriginX >= 96 || context.megaOriginZ >= 96 || (context.megaOriginX & 1) != 0
	    || (context.megaOriginZ & 1) != 0 || source.sourceX < context.megaOriginX || source.sourceX >= context.megaOriginX + 2
	    || source.sourceZ < context.megaOriginZ || source.sourceZ >= context.megaOriginZ + 2 || context.signedObjectOccupancy == -128)
		throw std::invalid_argument("Native cell-art context has invalid epoch/macro/source/occupancy");
	CellArtClassification result;
	result.nativeIsFloor = plan.nativeIsFloor;
	result.nativeWallTransparency = plan.nativeWallTransparency;
	result.hasBaseWords = source.words[0] != 0 || source.words[1] != 0;
	for (unsigned micro = 2; micro < source.activeMicroTileLength; ++micro)
		result.hasUpperWords = result.hasUpperWords || source.words[micro] != 0;
	if (context.nativeMegaPieceMappingObserved) result.megaFamily = ReadNamedNativeMegaFamily(context.rawMega);
	std::size_t linkedRecords = 0;
	for (const auto &object : objects) {
		if (object.sourceX != source.sourceX || object.sourceZ != source.sourceZ) continue;
		if (!object.authoritativeNativeRecord || !object.active || object.epoch != context.epoch
		    || object.slot < 0 || object.slot >= 127 || object.nativeType < 0
		    || object.signedOccupancy != context.signedObjectOccupancy
		    || (object.signedOccupancy != object.slot + 1 && object.signedOccupancy != -(object.slot + 1))
		    || object.originX < 0 || object.originX >= 112 || object.originZ < 0 || object.originZ >= 112
		    || object.animationFrame < 0 || object.emitterRuleRadius < 0)
			throw std::invalid_argument("Native cell-art object declaration is stale or not signed active membership");
		if (++linkedRecords != 1) throw std::invalid_argument("Native cell-art source has duplicate native object declarations");
		result.nativeObjectPresent = true;
		// objdat.h enum OBJ_L1LIGHT=0; AddL1Objs explicitly maps dPiece269 to it.
		// The origin must be this exact piece, not a neighbor occupied by a large
		// object's negative ID. Radius5 corroborates native AddObjectLight, while
		// applyLighting=false remains the unlit/native CLX sprite draw branch.
		if (object.nativeType == 0 && source.piece == 269 && object.originX == source.sourceX && object.originZ == source.sourceZ
		    && object.nativeEmitterRuleMatched && object.emitterRuleRadius == 5 && !object.appliesLighting) {
			result.classification = CellArtClass::NativeLightFire;
			result.authority = ClassAuthority::NativeObjectAndPieceRule;
			result.corroboratedCathedralLight = true;
		}
	}
	// Missing authoritative records fail closed for visual replacement, without
	// inventing the native object's class from a nonzero slot or its SOL bit.
	result.nativeObjectPresent = result.nativeObjectPresent || context.signedObjectOccupancy != 0;
	result.sourceCodePredicateEligible = context.nativeMegaPieceMappingObserved && !plan.nativeIsFloor
	    && !plan.nativeWallTransparency && result.hasUpperWords && !result.nativeObjectPresent
	    && (result.megaFamily == NativeMegaFamily::NamedWall || result.megaFamily == NativeMegaFamily::NamedFence
	        || result.megaFamily == NativeMegaFamily::NamedArch);
	const std::size_t fullColumnPixels = static_cast<std::size_t>(source.activeMicroTileLength / 2) * 32 * 32;
	const bool sameEvidenceContext = evidence.epoch == context.epoch && evidence.column <= 1 && SameSource(evidence.source, source);
	result.declaredCompleteVisualEvidence = sameEvidenceContext && IsLowerSha256(evidence.receiptSha256)
	    && evidence.ownerVerifiedReceipt && evidence.originalDrawCellReference && evidence.independentCoverage && evidence.rawIdentityIndices
	    && evidence.originalNativeMasksCompared && evidence.physicalWitnessUnchanged && evidence.objectSpritesAndLightsPreserved
	    && evidence.exactTargetVisualReplacementCompared && evidence.nativeInterCellAndObjectOcclusionCompared
	    && evidence.comparedPixels == fullColumnPixels && evidence.coveredPixels <= fullColumnPixels
	    && evidence.uncoveredPixels == fullColumnPixels - evidence.coveredPixels;
	if (result.declaredCompleteVisualEvidence && !result.corroboratedCathedralLight) {
		result.classification = evidence.declaredClass;
		result.authority = evidence.declaredClass == CellArtClass::Unknown
		    ? ClassAuthority::None : ClassAuthority::OwnerDeclaredIndependentVisualReference;
	}
	// Even a named Wall macro / covered CEL rectangle is not native facade depth,
	// a measured wall volume, or admission to hide a technical cap/back. The
	// external visual proof plus scoped renderer transaction remains mandatory.
	result.evidenceRequired = !result.declaredCompleteVisualEvidence;
	return result;
}

struct FullRawAtlas {
	composition::SourceCellMetadata source;
	unsigned column = 0;
	composition::AtlasDescriptor descriptor;
	std::vector<std::uint8_t> indices, coverage;
	std::vector<composition::RasterOperation> layerOperations;
	// All source rows (including absent words), retained without compaction.
	std::vector<composition::LayerDescriptor> layers;
	std::size_t callbackInvocations = 0;
	bool noPresentWords = true, noCoveredPixels = true;
	bool originalNativeMasksAppliedToPixels = false;
	bool nativeLightOrPaletteAppliedToPixels = false;
	// Raw payload does not certify native inter-cell/object order or visual3D.
};

inline void ValidateFullRawBlock(const composition::RawBlockPatch &patch, const composition::RasterOperation &operation)
{
	if (!operation.source.present || (operation.role != composition::RasterRole::DrawCellBase
	        && operation.role != composition::RasterRole::DrawCellUpper)
	    || operation.requiresPreparedFloorSubframe || operation.requiresPreparedFoliageSuffix
	    || operation.rasterShape != operation.source.sourceShape || operation.rawDecodeMaskMode != composition::NativeMaskMode::Solid)
		throw std::invalid_argument("Native full-column callback must supply original nonfloor base/upper shape");
	// Reuse only the frozen patch's size/coverage/shape checks. Its upper-role
	// precondition is adapted in this LOCAL validation copy; the original base
	// operation sent to the callback and retained in the atlas is never relabeled.
	auto shapeValidation = operation;
	shapeValidation.role = composition::RasterRole::DrawCellUpper;
	composition::ValidateRawBlockPatch(patch, shapeValidation);
}

/** Callback RawBlockPatch(const RasterOperation&) supplies identity raw32x32
 * bottom-aligned at(0,31), independent binary coverage, rawDecodeMask=Solid.
 * Nonfloor mt0/1 uses ORIGINAL type (not DrawFloorTile's reencoded triangle),
 * row0 anchor0; upper row r anchor-32*r. At MicroLen10 canvas32x160 T[-159,1).
 * Missing words remain blank; triangle31 leaves each slot's first row blank.
 * All native0..255 codes remain valid paint. No fill/donor/flip/shear/repeat.
 * SOL/TransList actual nativeMaskMode/color/light roles remain on operations;
 * partial masks and paletteBlend are draw operators, never texture coverage.
 * Mixed-mask atlases are METADATA+RAW only; a renderer must split operators by
 * native rows/masks instead of applying one blend policy to the whole atlas.
 * Floor, reencoded floor subframes and foliage stay declarative in BuildCellPlan
 * and are REJECTED here. This callable reads no native resources or globals.
 * Callback/global purity and original DrawCell full reference require external
 * native witness. This payload does not include dSpecial or objects/sprites.
 */
template <typename ReadRawBlock>
FullRawAtlas ComposeFullColumnRawAtlas(const composition::SourceCellMetadata &source, unsigned column, ReadRawBlock callback)
{
	if (column > 1) throw std::invalid_argument("Native full-column must select MIN column0 or1");
	const auto plan = composition::BuildCellPlan(source);
	if (plan.nativeIsFloor) throw std::invalid_argument("Native full-column raw assembly excludes floor/prepared foliage");
	const auto &selected = plan.columns[column];
	FullRawAtlas result;
	result.source = source; result.column = column; result.descriptor = selected.fullColumnCanvas;
	result.layers = selected.layers; result.noPresentWords = selected.noPresentWords;
	const auto count = static_cast<std::size_t>(result.descriptor.width) * static_cast<std::size_t>(result.descriptor.height);
	result.indices.assign(count, 0); result.coverage.assign(count, 0);
	for (const auto &layer : selected.layers) {
		if (!layer.source.present) continue;
		if (layer.operations.size() != 1) throw std::invalid_argument("Native nonfloor layer requires exactly one original raster operation");
		const auto &operation = layer.operations.front();
		const auto patch = callback(operation);
		++result.callbackInvocations;
		ValidateFullRawBlock(patch, operation);
		const int top = operation.nativeAnchorOffset.y - 31 - result.descriptor.minY;
		if (top < 0 || top + 32 > result.descriptor.height) throw std::invalid_argument("Native full-column layer outside original fixed slot");
		for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
			const std::size_t from = static_cast<std::size_t>(y) * 32 + x;
			if (patch.coverage[from] == 0) continue;
			const std::size_t to = static_cast<std::size_t>(top + y) * 32 + x;
			if (result.coverage[to] != 0) throw std::invalid_argument("Native full-column original slots overlap");
			result.indices[to] = patch.indices[from]; result.coverage[to] = 1; result.noCoveredPixels = false;
		}
		result.layerOperations.push_back(operation);
	}
	return result;
}

} // namespace devilution::cathedral::native_cell_art
