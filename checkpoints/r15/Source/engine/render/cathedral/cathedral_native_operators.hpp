#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

#include "cathedral_native_texture.hpp"
#include "native-wall-composition.hpp"

namespace devilution::cathedral::native_operators {

inline constexpr std::uint32_t OperationRevision = 1;
inline constexpr int FullColumnWidth = 32, FullColumnHeight = 160;

/** This is a draw-operation map, never native source coverage or source pixels.
 * Zero marks absent slots. 1 writes a lit palette code opaquely; 2 applies the
 * native paletteTransparencyLookup[destination][litSource] operator. The raw
 * identity atlas/coverage stays independent and is shared by both passes.
 */
struct FullColumnOperationMap {
	std::array<std::uint8_t, FullColumnWidth * FullColumnHeight> operations {};
	bool hasSolid = false, hasPaletteBlend = false;
};

/** Native dun_render.cpp InitialPrefix/PrefixIncrement/RenderLine equations.
 * Local y is top-to-bottom in a 32px slot, including a triangle's empty row.
 * Left/Right are mixed opaque/blend operators, not cutout masks. Their lower
 * 16 rows are opaque; the upper prefix advances by two pixels bottom-to-top.
 * The same absolute x handles TransparentSquare RLE runs and clipped spans.
 * Coverage and native shape are supplied separately by the raw decoder.
 */
inline NativeTextureOperationPass OperationAt(const wall_composition::RasterOperation &operation, int x, int y)
{
	using wall_composition::NativeMaskMode;
	if (x < 0 || x >= 32 || y < 0 || y >= 32
	    || (operation.role != wall_composition::RasterRole::DrawCellBase
	        && operation.role != wall_composition::RasterRole::DrawCellUpper)
	    || !operation.source.present || operation.rawDecodeMaskMode != NativeMaskMode::Solid
	    || operation.rasterShape != operation.source.sourceShape
	    || operation.requiresPreparedFloorSubframe || operation.requiresPreparedFoliageSuffix)
		throw std::invalid_argument("Native column operation requires an original nonfloor cell block");
	bool blend = false;
	switch (operation.nativeMaskMode) {
	case NativeMaskMode::Solid: break;
	case NativeMaskMode::PaletteBlend: blend = true; break;
	case NativeMaskMode::PartialLeft:
		if (operation.role != wall_composition::RasterRole::DrawCellBase || operation.source.column != 0
		    || (operation.rasterShape != wall_composition::TileShape::TransparentSquare
		        && operation.rasterShape != wall_composition::TileShape::LeftTrapezoid))
			throw std::invalid_argument("Native Left operator requires its original left base shape");
		blend = x < 30 - 2 * y;
		break;
	case NativeMaskMode::PartialRight:
		if (operation.role != wall_composition::RasterRole::DrawCellBase || operation.source.column != 1
		    || (operation.rasterShape != wall_composition::TileShape::TransparentSquare
		        && operation.rasterShape != wall_composition::TileShape::RightTrapezoid))
			throw std::invalid_argument("Native Right operator requires its original right base shape");
		blend = x >= 2 + 2 * y;
		break;
	default: throw std::invalid_argument("Native column mask operator is unknown");
	}
	return blend ? NativeTextureOperationPass::PaletteBlend : NativeTextureOperationPass::Solid;
}

/** Finite Cathedral MicroLen10 full-column operation plan. This reads metadata
 * only. It does not decode MIN/CEL, render, inspect a destination, or claim a
 * native reference. A current ORIGINAL DrawCell two-background comparison is
 * still the owner's independent admission gate.
 */
inline FullColumnOperationMap BuildFullColumnOperationMap(const wall_composition::SourceCellMetadata &source, unsigned column)
{
	if (source.activeMicroTileLength != 10 || column > 1)
		throw std::invalid_argument("Native full-column operator plan requires MicroLen10 and column0/1");
	const auto plan = wall_composition::BuildCellPlan(source);
	if (plan.nativeIsFloor)
		throw std::invalid_argument("Native full-column operators cannot reinterpret a prepared floor");
	FullColumnOperationMap result;
	const auto &selected = plan.columns[column];
	for (const auto &layer : selected.layers) {
		if (layer.operations.empty()) continue;
		if (layer.operations.size() != 1)
			throw std::invalid_argument("Native full-column source has unsupported overlapping operations");
		const auto &operation = layer.operations.front();
		const int top = operation.nativeAnchorOffset.y - 31 - selected.fullColumnCanvas.minY;
		if (top < 0 || top + 32 > FullColumnHeight || operation.source.column != column)
			throw std::invalid_argument("Native full-column operator lies outside its original slot");
		for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
			const auto pass = OperationAt(operation, x, y);
			const auto index = static_cast<std::size_t>(top + y) * FullColumnWidth + x;
			if (result.operations[index] != 0)
				throw std::invalid_argument("Native full-column operation slots overlap");
			result.operations[index] = static_cast<std::uint8_t>(pass);
			result.hasSolid |= pass == NativeTextureOperationPass::Solid;
			result.hasPaletteBlend |= pass == NativeTextureOperationPass::PaletteBlend;
		}
	}
	return result;
}

/** Filter only a derivative draw texture's opacity. Raw pixels and raw binary
 * coverage are untouched. Solid and blend masks are disjoint and their union
 * equals raw coverage; palette codes 0/255 never decide whether a texel exists.
 */
inline void FilterFullColumnCoverage(const FullColumnOperationMap &map,
    std::span<std::uint8_t> derivativeCoverage, NativeTextureOperationPass pass)
{
	if (derivativeCoverage.size() != map.operations.size()
	    || (pass != NativeTextureOperationPass::Solid && pass != NativeTextureOperationPass::PaletteBlend))
		throw std::invalid_argument("Native full-column operation filter requires one bounded draw pass");
	for (std::size_t i = 0; i < derivativeCoverage.size(); ++i) {
		if (derivativeCoverage[i] > 1 || map.operations[i] > 2 || (derivativeCoverage[i] != 0 && map.operations[i] == 0))
			throw std::invalid_argument("Native covered texel lacks an original column operation");
		derivativeCoverage[i] = derivativeCoverage[i] != 0 && map.operations[i] == static_cast<std::uint8_t>(pass);
	}
}

} // namespace devilution::cathedral::native_operators
