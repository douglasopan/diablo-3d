#pragma once

// PRIVATE SOURCE-ONLY CANDIDATE. No compilation/native decode/render execution
// is established by this header. This is a metadata plan and an optional raw
// upper-atlas assembler, not an installed renderer or a native facade oracle.
// std-only: no globals, native loaders, RNG, map/frame generation, cache or IO.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace devilution::cathedral::wall_composition {

inline constexpr int NativeGridSize = 112;
inline constexpr int NativeColumnWidth = 32;
inline constexpr int NativeRowPitch = 32;
inline constexpr std::uint8_t SolSolid = 0x01;
inline constexpr std::uint8_t SolBlockMissile = 0x04;
inline constexpr std::uint8_t SolTransparent = 0x08;
inline constexpr std::uint8_t SolTransparentLeft = 0x10;
inline constexpr std::uint8_t SolTransparentRight = 0x20;

// Native LevelCelBlock type codes. Zero WORD, not type zero, means absent.
enum class TileShape : std::uint8_t {
	Square, TransparentSquare, LeftTriangle, RightTriangle,
	LeftTrapezoid, RightTrapezoid
};

// Semantic equivalents only; never cast this enum to the native MaskType.
// PaletteBlend/partial masks are draw-time operators, not texture opacity.
enum class NativeMaskMode : std::uint8_t {
	Solid, PaletteBlend, PartialLeft, PartialRight
};
enum class RasterRole : std::uint8_t {
	Absent, DrawCellBase, DrawCellUpper,
	DrawFloorLeftTriangle, DrawFloorRightTriangle, DrawFloorFoliage
};
enum class NativeColorTableRole : std::uint8_t {
	FloorTable, CellBaseTable, CellFoliageTable
};
enum class NativeLightmapRole : std::uint8_t {
	FloorLightmap, CellBleedLightmap
};

/** Caller-supplied immutable source metadata, from one prepared native epoch.
 * words are all 16 DPieceMicros words AFTER SetDungeonMicros/reencoding.
 * activeMicroTileLength is the actual native MicroTileLen, not inferred from
 * zero words. sourceZ is native y. No resource identity or pixels are loaded.
 * groupActive is TransList[transparencyGroup]; SOL bytes are native effective
 * loader-corrected properties. Native debug Alt mask override is outside this
 * normal render contract. Native current lighting/TRNs remain caller-owned.
 */
struct SourceCellMetadata {
	int sourceX = 0, sourceZ = 0;
	std::uint16_t piece = 0;
	std::array<std::uint16_t, 16> words {};
	unsigned activeMicroTileLength = 10;
	std::uint8_t sol = 0, transparencyGroup = 0;
	bool groupActive = false;
};

struct PixelOffset { int x = 0, y = 0; };

/** Half-open screen-pixel edges relative to the native cell bottom-pixel anchor.
 * A baseline index b is the bottom pixel, whose center is b+0.5. A 32px block
 * occupies [b-31,b+1); a 31px triangle occupies [b-30,b+1), leaving the top row
 * of the 32px slot empty. A rectangle is a canvas, not decoded opaque bounds.
 */
struct AtlasDescriptor {
	int width = 0, height = 0;
	int minX = 0, minY = 0, maxXExclusive = 0, maxYExclusive = 0;
};

struct MicroMetadata {
	unsigned micro = 0, column = 0, rowFromBottom = 0;
	std::uint16_t word = 0, frameOneBased = 0;
	TileShape sourceShape = TileShape::Square;
	bool present = false;
	PixelOffset nativeAnchorOffset;
};

/** One native raster operation. sourceShape/sourceWord are never replaced by a
 * donor. rasterShape may differ only for DrawFloorTile's forced left/right
 * triangle or the prepared floor foliage suffix. Native anchor includes the
 * foliage y-16 offset; source micro metadata retains its original row anchor.
 * Neither masks nor color/light roles are baked into a raw atlas.
 */
struct RasterOperation {
	MicroMetadata source;
	RasterRole role = RasterRole::Absent;
	TileShape rasterShape = TileShape::Square;
	PixelOffset nativeAnchorOffset;
	int rasterWidth = 32, rasterHeight = 32;
	NativeMaskMode nativeMaskMode = NativeMaskMode::Solid;
	NativeColorTableRole colorTableRole = NativeColorTableRole::CellBaseTable;
	NativeLightmapRole lightmapRole = NativeLightmapRole::CellBleedLightmap;
	bool requiresPreparedFloorSubframe = false;
	bool requiresPreparedFoliageSuffix = false;
	// The optional upper compositor always requests unshaded raw indices and
	// full block coverage. Native draw masks/palette blending stay separate.
	NativeMaskMode rawDecodeMaskMode = NativeMaskMode::Solid;
};

struct LayerDescriptor {
	MicroMetadata source;
	RasterRole primaryRole = RasterRole::Absent;
	std::vector<RasterOperation> operations;
};
struct ColumnPlan {
	unsigned column = 0;
	std::vector<LayerDescriptor> layers; // Every active row, including absences.
	AtlasDescriptor fullColumnCanvas, upperColumnCanvas;
	bool noPresentWords = true, noPresentUpperWords = true;
};
struct CellPlan {
	SourceCellMetadata source; // Includes all16 words, including inactive tail.
	bool nativeIsFloor = false, nativeWallTransparency = false;
	AtlasDescriptor baseCanvas; //64x32, a descriptor only; no raw payload here.
	std::array<ColumnPlan, 2> columns;
	// DrawFloorTile pass L/R first (when floor), then DrawCell base/foliage L/R,
	// then every upper pair L/R. This order does not interleave other cells or
	// actors and therefore does not certify a complete native screen render.
	std::vector<RasterOperation> operationsInNativePassOrder;
};

inline void ValidateSource(const SourceCellMetadata &source)
{
	if (source.sourceX < 0 || source.sourceX >= NativeGridSize
	    || source.sourceZ < 0 || source.sourceZ >= NativeGridSize)
		throw std::invalid_argument("Native composition source cell outside grid");
	if (source.activeMicroTileLength < 2 || source.activeMicroTileLength > 16
	    || (source.activeMicroTileLength & 1U) != 0)
		throw std::invalid_argument("Native composition MicroTileLen must be even2..16");
	for (unsigned micro = 0; micro < source.activeMicroTileLength; ++micro) {
		const auto word = source.words[micro];
		if (word != 0 && (((word & 0x7000U) >> 12) > 5 || (word & 0x0FFFU) == 0))
			throw std::invalid_argument("Native composition active word has invalid type/frame");
	}
	// Inactive words are retained exactly and never interpreted or rendered.
}

inline MicroMetadata ReadMicro(const SourceCellMetadata &source, unsigned micro)
{
	if (micro >= source.activeMicroTileLength || micro >= source.words.size())
		throw std::invalid_argument("Native composition micro outside active length");
	const auto word = source.words[micro];
	return { micro, micro & 1U, micro / 2, word,
		static_cast<std::uint16_t>(word & 0x0FFFU),
		static_cast<TileShape>((word & 0x7000U) >> 12), word != 0,
		{ static_cast<int>(micro & 1U) * NativeColumnWidth,
			-static_cast<int>(micro / 2) * NativeRowPitch } };
}

inline int RasterHeight(TileShape shape)
{
	if (static_cast<unsigned>(shape) > 5)
		throw std::invalid_argument("Native composition unknown raster shape");
	return shape == TileShape::LeftTriangle || shape == TileShape::RightTriangle ? 31 : 32;
}

inline NativeMaskMode BaseMask(const SourceCellMetadata &source, const MicroMetadata &micro)
{
	if ((source.sol & SolTransparent) == 0 || !source.groupActive)
		return NativeMaskMode::Solid;
	if (micro.column == 0) {
		if (micro.sourceShape == TileShape::LeftTrapezoid || micro.sourceShape == TileShape::TransparentSquare)
			return (source.sol & SolTransparentLeft) != 0 ? NativeMaskMode::PartialLeft : NativeMaskMode::Solid;
		if (micro.sourceShape == TileShape::LeftTriangle) return NativeMaskMode::Solid;
	} else {
		if (micro.sourceShape == TileShape::RightTrapezoid || micro.sourceShape == TileShape::TransparentSquare)
			return (source.sol & SolTransparentRight) != 0 ? NativeMaskMode::PartialRight : NativeMaskMode::Solid;
		if (micro.sourceShape == TileShape::RightTriangle) return NativeMaskMode::Solid;
	}
	return NativeMaskMode::PaletteBlend;
}

inline RasterOperation CellOperation(const MicroMetadata &micro, RasterRole role, NativeMaskMode mask)
{
	RasterOperation operation;
	operation.source = micro;
	operation.role = role;
	operation.rasterShape = micro.sourceShape;
	operation.nativeAnchorOffset = micro.nativeAnchorOffset;
	operation.rasterHeight = RasterHeight(micro.sourceShape);
	operation.nativeMaskMode = mask;
	operation.colorTableRole = role == RasterRole::DrawCellUpper
		? NativeColorTableRole::CellFoliageTable : NativeColorTableRole::CellBaseTable;
	return operation;
}

/** Declarative complete column plan; it does not decode or select a visual3D
 * plane, kappa, UV, donor, repeat mode, owner, draw policy or cache identity.
 * For MicroTileLen 10, full-column 32x160 T[-159,1), upper 32x128 T[-159,-31).
 * Missing rows remain at their original anchors. Empty columns remain empty.
 * Scope is MIN cell layers only: dSpecial CLX, objects, actors and inter-cell
 * draw order are not represented. It establishes no visible face or cap/back.
 */
inline CellPlan BuildCellPlan(const SourceCellMetadata &source)
{
	ValidateSource(source);
	CellPlan plan;
	plan.source = source;
	plan.nativeIsFloor = (source.sol & (SolSolid | SolBlockMissile)) == 0;
	plan.nativeWallTransparency = (source.sol & SolTransparent) != 0 && source.groupActive;
	plan.baseCanvas = { 64, 32, 0, -31, 64, 1 };
	const int rows = static_cast<int>(source.activeMicroTileLength / 2);
	for (unsigned column = 0; column < 2; ++column) {
		auto &out = plan.columns[column];
		out.column = column;
		const int x = static_cast<int>(column) * 32;
		out.fullColumnCanvas = { 32, rows * 32, x, 1 - rows * 32, x + 32, 1 };
		out.upperColumnCanvas = { 32, (rows - 1) * 32, x, 1 - rows * 32, x + 32, -31 };
		out.layers.reserve(static_cast<std::size_t>(rows));
		for (unsigned row = 0; row < static_cast<unsigned>(rows); ++row) {
			const auto micro = ReadMicro(source, row * 2 + column);
			LayerDescriptor layer;
			layer.source = micro;
			if (!micro.present) { out.layers.push_back(layer); continue; }
			out.noPresentWords = false;
			if (row != 0) {
				out.noPresentUpperWords = false;
				layer.primaryRole = RasterRole::DrawCellUpper;
				layer.operations.push_back(CellOperation(micro, layer.primaryRole,
					plan.nativeWallTransparency ? NativeMaskMode::PaletteBlend : NativeMaskMode::Solid));
			} else if (!plan.nativeIsFloor) {
				layer.primaryRole = RasterRole::DrawCellBase;
				layer.operations.push_back(CellOperation(micro, layer.primaryRole, BaseMask(source, micro)));
			} else {
				RasterOperation floor;
				floor.source = micro;
				floor.role = column == 0 ? RasterRole::DrawFloorLeftTriangle : RasterRole::DrawFloorRightTriangle;
				floor.rasterShape = column == 0 ? TileShape::LeftTriangle : TileShape::RightTriangle;
				floor.nativeAnchorOffset = micro.nativeAnchorOffset;
				floor.rasterHeight = 31;
				floor.nativeMaskMode = NativeMaskMode::Solid; // Even if upper palette blends.
				floor.colorTableRole = NativeColorTableRole::FloorTable;
				floor.lightmapRole = NativeLightmapRole::FloorLightmap;
				floor.requiresPreparedFloorSubframe = true;
				layer.primaryRole = floor.role;
				layer.operations.push_back(floor);
				if (micro.sourceShape == TileShape::TransparentSquare) {
					RasterOperation foliage = floor;
					foliage.role = RasterRole::DrawFloorFoliage;
					foliage.rasterShape = TileShape::TransparentSquare;
					foliage.nativeAnchorOffset.y -= 16;
					foliage.rasterHeight = 16;
					foliage.colorTableRole = NativeColorTableRole::CellFoliageTable;
					foliage.lightmapRole = NativeLightmapRole::CellBleedLightmap;
					foliage.requiresPreparedFloorSubframe = false;
					foliage.requiresPreparedFoliageSuffix = true;
					layer.operations.push_back(foliage);
				}
			}
			out.layers.push_back(layer);
		}
	}
	// Native floor pass both halves precedes DrawCell foliage; do not combine a
	// prepared floor's TransparentSquare word using a generic Square decoder.
	if (plan.nativeIsFloor)
		for (const auto &column : plan.columns)
			for (const auto &operation : column.layers[0].operations)
				if (operation.role != RasterRole::DrawFloorFoliage)
					plan.operationsInNativePassOrder.push_back(operation);
	for (unsigned row = 0; row < static_cast<unsigned>(rows); ++row)
		for (const auto &column : plan.columns)
			for (const auto &operation : column.layers[row].operations)
				if (!plan.nativeIsFloor || row != 0 || operation.role == RasterRole::DrawFloorFoliage)
					plan.operationsInNativePassOrder.push_back(operation);
	return plan;
}

/** Optional callback result for one UPPER block only, rendered bottom-aligned
 * at (0,31) into 32x32. Identity/raw palette codes; independent coverage must be
 * binary 0/1, never inferred from color. Covered code 0 and code 255 are valid.
 * Native draw masks/light/TRN/blend must not be baked into this patch.
 */
struct RawBlockPatch {
	int width = 32, height = 32;
	std::vector<std::uint8_t> indices, coverage;
};
struct UpperRawAtlas {
	SourceCellMetadata source;
	unsigned column = 0;
	AtlasDescriptor descriptor;
	std::vector<std::uint8_t> indices, coverage;
	std::vector<RasterOperation> layerOperations;
	std::size_t callbackInvocations = 0;
	bool noPresentUpperWords = true;
	bool noCoveredPixels = true;
	// This payload is neither a complete DrawCell/Floor render nor a native3D,
	// palette-blend, lighting, visibility, collision or artistic proof.
};

inline void ValidateRawBlockPatch(const RawBlockPatch &patch, const RasterOperation &operation)
{
	if (operation.role != RasterRole::DrawCellUpper || !operation.source.present)
		throw std::invalid_argument("Native upper callback operation is not a present upper block");
	if (patch.width != 32 || patch.height != 32 || patch.indices.size() != 1024 || patch.coverage.size() != 1024)
		throw std::invalid_argument("Native upper callback must provide32x32 indices and coverage");
	if (std::any_of(patch.coverage.begin(), patch.coverage.end(), [](std::uint8_t value) { return value > 1; }))
		throw std::invalid_argument("Native upper coverage must be independent binary0/1");
	if (operation.rasterHeight == 31
	    && std::any_of(patch.coverage.begin(), patch.coverage.begin() + 32, [](std::uint8_t value) { return value != 0; }))
		throw std::invalid_argument("Native31px triangle must leave the32px patch top row uncovered");
	if (operation.rasterShape == TileShape::Square
	    && std::any_of(patch.coverage.begin(), patch.coverage.end(), [](std::uint8_t value) { return value != 1; }))
		throw std::invalid_argument("Native upper Square must retain complete block coverage");
}

/** Callback signature: RawBlockPatch callback(const RasterOperation&).
 * Called once per present upper word in this source column, in native order,
 * including TransparentSquare, triangle and trapezoid types. No callback for
 * missing layers; no donor, fill, resampling, shear, repeat, kappa or UV chosen.
 * All raw indices0..255 remain possible independently of coverage. Source and
 * plan are immutable, but callback side effects/native setup are the caller's
 * responsibility and must be witnessed externally by the principal.
 * BASE/FLOOR/FOLIAGE intentionally stay declarative: this API never pretends a
 * 32x32 generic patch composes prepared floor triangles and foliage correctly.
 */
template <typename ReadRawUpperBlock>
UpperRawAtlas ComposeUpperRawAtlas(const SourceCellMetadata &source, unsigned column, ReadRawUpperBlock callback)
{
	if (column > 1) throw std::invalid_argument("Native composition column must be0 or1");
	const CellPlan plan = BuildCellPlan(source);
	const ColumnPlan &selected = plan.columns[column];
	UpperRawAtlas result;
	result.source = source;
	result.column = column;
	result.descriptor = selected.upperColumnCanvas;
	result.noPresentUpperWords = selected.noPresentUpperWords;
	const std::size_t count = static_cast<std::size_t>(result.descriptor.width) * static_cast<std::size_t>(result.descriptor.height);
	result.indices.assign(count, 0);
	result.coverage.assign(count, 0);
	for (const auto &layer : selected.layers) {
		if (layer.source.rowFromBottom == 0 || !layer.source.present) continue;
		const RasterOperation &operation = layer.operations[0];
		const RawBlockPatch patch = callback(operation);
		++result.callbackInvocations;
		ValidateRawBlockPatch(patch, operation);
		const int topRow = operation.nativeAnchorOffset.y - 31 - result.descriptor.minY;
		if (topRow < 0 || topRow + 32 > result.descriptor.height)
			throw std::invalid_argument("Native upper layer offset outside fixed atlas");
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x) {
				const std::size_t from = static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x);
				if (patch.coverage[from] == 0) continue;
				const std::size_t to = static_cast<std::size_t>(topRow + y) * 32 + static_cast<std::size_t>(x);
				if (result.coverage[to] != 0)
					throw std::invalid_argument("Native upper raw layer canvas overlap");
				result.indices[to] = patch.indices[from];
				result.coverage[to] = 1;
				result.noCoveredPixels = false;
			}
		result.layerOperations.push_back(operation);
	}
	return result;
}

} // namespace devilution::cathedral::wall_composition
