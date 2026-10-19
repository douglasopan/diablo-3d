#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/light_tables.hpp"
#include "engine/palette.h"
#include "engine/point.hpp"
#include "engine/surface.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/town_surroundings_visual.hpp"
#include "levels/dun_tile_data.hpp"

namespace devilution {

/**
 * Indexed native-view adapter for the shared immutable surroundings scene.
 * It owns derived colors/lookup/scratch only, never map or simulation state.
 * Begin/End delimit one native world pass. Cells are submitted by scrollrt's
 * existing painter traversal; this class does not draw above the completed city.
 */
class TownSurroundingsNativeFrame {
public:
	bool Begin(const TownSurroundingsVisualScene *scene, const Surface &out,
	    Point firstTile, Point firstTarget) noexcept
	{
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		try {
#endif
			activeScene_ = nullptr;
			underlayPainted_ = false;
			suppressedPaddingCells_.fill(0);
			if (scene == nullptr || !scene->valid || scene->identity.empty()) {
				Clear();
				return false;
			}
			if (out.w() <= 0 || out.h() <= 0
			    || static_cast<uint64_t>(out.w()) * out.h() > MaxRasterPixels)
				return false;
			if (identity_ != scene->identity || triangleColors_.size() != scene->triangles.size()) {
				if (!Bind(*scene))
					return false;
			}
			// Every possible scratch size is a subrectangle of this viewport.
			// Preallocate before the first pixel changes; draw methods only reuse
			// capacity. With exceptions enabled an allocation failure returns the
			// complete native fallback. No-exception targets keep their existing
			// allocation-failure policy; this adapter adds no recovery claim.
			scratchDepth_.reserve(static_cast<size_t>(out.w()) * out.h());
			UpdatePalette();
			if (scene->hasNativePaddingUnderlay)
				PrepareNativePaddingArtwork();
			// RenderTile's target is the diamond's left X / bottom Y. Its bottom
			// geographic corner is (target.x + 32, target.y), not target itself.
			originX_ = firstTarget.x + 32.0 - 32.0 * (firstTile.x - firstTile.y);
			originY_ = firstTarget.y - 16.0 * (firstTile.x + firstTile.y);
			viewportWidth_ = out.w();
			viewportHeight_ = out.h();
			activeScene_ = scene;
			return true;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
		} catch (...) {
			Clear();
			return false;
		}
#endif
	}

	void End()
	{
		activeScene_ = nullptr;
		underlayPainted_ = false;
	}

	void Clear()
	{
		activeScene_ = nullptr;
		underlayPainted_ = false;
		suppressedPaddingCells_.fill(0);
		paddingArtworkAttempted_ = paddingArtworkValid_ = false;
		paddingCelData_ = nullptr;
		identity_.clear();
		decltype(cells_)().swap(cells_);
		std::vector<uint32_t>().swap(backgroundCells_);
		std::vector<uint16_t>().swap(triangleColors_);
		std::vector<float>().swap(scratchDepth_);
		paletteValid_ = false;
		extraRows_ = extraRowsBefore_ = extraColumns_ = 0;
	}

	int ExtraRows() const { return extraRows_; }
	int ExtraRowsBefore() const { return extraRowsBefore_; }
	int ExtraColumns() const { return extraColumns_; }

	/** Apron and lowered padding support, before native floors and contents. */
	void DrawBackground(const Surface &out) noexcept
	{
		underlayPainted_ = false;
		if (activeScene_ == nullptr || backgroundCells_.empty())
			return;
		BeginScratch(0, 0, out.w() - 1, out.h() - 1);
		bool submittedUnderlay = false;
		for (uint32_t index : backgroundCells_) {
			const auto &cell = activeScene_->cells[index];
			DrawRange(out, cell);
			submittedUnderlay |= cell.kind == 4 && cell.triangleCount != 0;
		}
		underlayPainted_ = submittedUnderlay && activeScene_->hasNativePaddingUnderlay
		    && paddingArtworkValid_ && PaddingPaletteIsBlack();
	}

	/** Skip only authenticated static void art, never a tile's actors/payload. */
	bool SuppressNativePadding(Point tile, uint16_t piece) noexcept
	{
		if (activeScene_ == nullptr || !underlayPainted_ || !paddingArtworkValid_
		    || !PaddingPaletteIsBlack()
		    || !IsTownSurroundingsNativePaddingCell(tile.x, tile.y, piece))
			return false;
		suppressedPaddingCells_[static_cast<size_t>(tile.y) * MAXDUNX + tile.x] = 1;
		return true;
	}

	/**
	 * Last ON frame's exact suppressed-art stencil, in the pre-Zoom draw buffer.
	 * Query before another Begin/reset. The fixture must also require OFF==127
	 * and exclude UI/actor overdraw; this is not a final-pixel ownership claim.
	 */
	bool IsReplacedNativePaddingPixelForDiagnostics(Point bufferPosition) const noexcept
	{
		if (!paddingArtworkValid_ || bufferPosition.x < 0 || bufferPosition.y < 0
		    || bufferPosition.x >= viewportWidth_ || bufferPosition.y >= viewportHeight_)
			return false;
		const double dx = (bufferPosition.x - originX_) / 32.0;
		const double dy = (bufferPosition.y - originY_) / 16.0;
		const int baseX = static_cast<int>(std::floor((dx + dy) * 0.5));
		const int baseZ = static_cast<int>(std::floor((dy - dx) * 0.5));
		// A complete 64x32 native bitmap spans less than two cells per axis.
		for (int z = std::max(0, baseZ - 1); z <= std::min(MAXDUNY - 1, baseZ + 2); ++z) {
			for (int x = std::max(0, baseX - 1); x <= std::min(MAXDUNX - 1, baseX + 2); ++x) {
				if (suppressedPaddingCells_[static_cast<size_t>(z) * MAXDUNX + x] == 0)
					continue;
				const int localX = bufferPosition.x - static_cast<int>(originX_ - 32 + 32 * (x - z));
				const int localY = bufferPosition.y - static_cast<int>(originY_ + 16 * (x + z)) + 31;
				if (localX >= 0 && localX < 64 && localY >= 0 && localY < 32
				    && paddingOpacity_[static_cast<size_t>(localY) * 64 + localX] != 0)
					return true;
			}
		}
		return false;
	}

	/** Ground, paired props and measured skirt, interleaved with native cells. */
	void DrawCell(const Surface &out, Point tilePosition) noexcept
	{
		if (activeScene_ == nullptr)
			return;
		const auto it = cells_.find(Key(tilePosition.x, tilePosition.y));
		if (it == cells_.end())
			return;
		double minX = out.w(), minY = out.h(), maxX = -1, maxY = -1;
		for (uint32_t index : it->second) {
			const auto &cell = activeScene_->cells[index];
			for (size_t t = cell.firstTriangle, end = t + cell.triangleCount; t < end; ++t) {
				for (const auto &vertex : activeScene_->triangles[t].vertices) {
					const auto p = Project(vertex);
					minX = std::min(minX, p.x);
					minY = std::min(minY, p.y);
					maxX = std::max(maxX, p.x);
					maxY = std::max(maxY, p.y);
				}
			}
		}
		if (maxX < 0 || maxY < 0 || minX >= out.w() || minY >= out.h())
			return;
		const int left = static_cast<int>(std::floor(std::max(0.0, minX)));
		const int top = static_cast<int>(std::floor(std::max(0.0, minY)));
		const int right = static_cast<int>(std::ceil(std::min(out.w() - 1.0, maxX)));
		const int bottom = static_cast<int>(std::ceil(std::min(out.h() - 1.0, maxY)));
		BeginScratch(left, top, right, bottom);
		// A cell may contain both its ground and one or more paired props.
		// One local depth field resolves their shared triangles and alpha-free
		// silhouette. Native actors retain the caller's existing painter order.
		for (uint32_t index : it->second)
			DrawRange(out, activeScene_->cells[index]);
	}

private:
	static constexpr size_t MaxRasterPixels = 16'777'216;
	static constexpr size_t MaxSceneTriangles = 1'000'000;
	static constexpr size_t MaxSceneCells = 100'000;
	static constexpr uint16_t UnknownColor = 256;
	static constexpr int MaxExtraRows = TownSurroundingsNativeMaxExtraRows;
	static constexpr int MaxExtraColumns = TownSurroundingsNativeMaxExtraColumns;

	struct Projected {
		double x;
		double y;
		double depth;
	};

	static uint64_t Key(int32_t x, int32_t z)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32)
		    | static_cast<uint32_t>(z);
	}

	bool Bind(const TownSurroundingsVisualScene &scene)
	{
		Clear();
		if (scene.triangles.empty() || scene.triangles.size() > MaxSceneTriangles
		    || scene.cells.empty() || scene.cells.size() > MaxSceneCells)
			return false;
		double upward = 0, downward = 0, horizontal = 0;
		for (size_t index = 0; index < scene.cells.size(); ++index) {
			const auto &cell = scene.cells[index];
			if (cell.kind > 4 || cell.firstTriangle > scene.triangles.size()
			    || cell.triangleCount > scene.triangles.size() - cell.firstTriangle)
				return false;
			if (cell.kind == 3 || cell.kind == 4) {
				backgroundCells_.push_back(static_cast<uint32_t>(index));
				continue;
			}
			cells_[Key(cell.x, cell.z)].push_back(static_cast<uint32_t>(index));
			for (size_t t = cell.firstTriangle, end = t + cell.triangleCount; t < end; ++t) {
				for (const auto &v : scene.triangles[t].vertices) {
					const double x = 32.0 + 32.0 * (static_cast<double>(v.x) - cell.x - v.z + cell.z);
					const double y = 16.0 * (static_cast<double>(v.x) - cell.x + v.z - cell.z) - 32.0 * v.height;
					if (!std::isfinite(x) || !std::isfinite(y))
						return false;
					upward = std::max(upward, -y);
					downward = std::max(downward, y);
					horizontal = std::max({ horizontal, -x, x - 64.0 });
				}
			}
		}
		if (upward > (MaxExtraRows - 2) * 16.0
		    || downward > (MaxExtraRows - 2) * 16.0
		    || horizontal > (MaxExtraColumns - 1) * 64.0) {
			Clear();
			return false;
		}
		extraRows_ = static_cast<int>(std::ceil(upward / 16.0)) + 2;
		extraRowsBefore_ = static_cast<int>(std::ceil(downward / 16.0)) + 2;
		extraColumns_ = static_cast<int>(std::ceil(horizontal / 64.0)) + 1;
		identity_ = scene.identity;
		triangleColors_.assign(scene.triangles.size(), UnknownColor);
		return true;
	}

	static bool PaddingPaletteIsBlack() noexcept
	{
		const auto &color = logical_palette[127];
		return color.r == 0 && color.g == 0 && color.b == 0;
	}

	void PrepareNativePaddingArtwork()
	{
		if (leveltype != DTYPE_TOWN || !pDungeonCels || MicroTileLen < 2 || MicroTileLen > 16)
			return;
		std::array<uint16_t, 16> words;
		for (size_t i = 0; i < words.size(); ++i)
			words[i] = DPieceMicros[426].mt[i].data;
		const auto sol = SOLData[426];
		if (paddingArtworkAttempted_ && paddingCelData_ == pDungeonCels.get()
		    && paddingWords_ == words && paddingMicroTileLen_ == MicroTileLen && paddingSol_ == sol)
			return;
		paddingArtworkAttempted_ = true;
		paddingArtworkValid_ = false;
		paddingCelData_ = pDungeonCels.get();
		paddingWords_ = words;
		paddingMicroTileLen_ = MicroTileLen;
		paddingSol_ = sol;
		// DrawCell also owns upper MINs and unsplit floor foliage. Refuse the
		// optimization if any such art could be hidden by skipping the cell art.
		for (size_t i = 2; i < MicroTileLen; ++i)
			if (DPieceMicros[426].mt[i].hasValue())
				return;
		const bool solid = HasAnyOf(sol, TileProperties::Solid | TileProperties::BlockMissile);
		for (int i = 0; i < 2; ++i) {
			const auto block = DPieceMicros[426].mt[i];
			if (!block.hasValue() || static_cast<unsigned>(block.type()) > 5
			    || (!solid && block.type() == TileType::TransparentSquare))
				return;
		}
		auto raw = SDLWrap::CreateRGBSurfaceWithFormat(0, 64, 32, 8, SDL_PIXELFORMAT_INDEX8);
		auto coverage = SDLWrap::CreateRGBSurfaceWithFormat(0, 64, 32, 8, SDL_PIXELFORMAT_INDEX8);
		if (!raw || !coverage || raw->pitch != 64 || coverage->pitch != 64)
			return;
		const Surface pixels(raw.get());
		const Surface coveragePixels(coverage.get());
		std::memset(pixels.begin(), 0, 2048);
		std::memset(coveragePixels.begin(), 255, 2048);
		std::array<uint8_t, 256> identity;
		for (size_t i = 0; i < identity.size(); ++i)
			identity[i] = static_cast<uint8_t>(i);
		const std::array<uint8_t, 2048> lighting {};
		const Lightmap light(pixels.begin(), lighting, 64, LightTables, identity.data(), FullyDarkLightTable);
		const Lightmap coverageLight(coveragePixels.begin(), lighting, 64, LightTables, identity.data(), FullyDarkLightTable);
		const auto render = [&](const Surface &out, const Lightmap &map) {
			for (int i = 0; i < 2; ++i) {
				const auto block = DPieceMicros[426].mt[i];
				if (solid)
					RenderTile(out, map, { i * 32, 31 }, pDungeonCels.get(), block, MaskType::Solid, identity.data());
				else
					RenderTileFrame(out, map, { i * 32, 31 }, i == 0 ? TileType::LeftTriangle : TileType::RightTriangle,
					    GetDunFrame(pDungeonCels.get(), block.frame()), DunFrameTriangleHeight, MaskType::Solid, identity.data());
			}
		};
		render(pixels, light);
		render(coveragePixels, coverageLight);
		for (size_t i = 0; i < paddingOpacity_.size(); ++i)
			paddingOpacity_[i] = pixels.begin()[i] == coveragePixels.begin()[i] ? 1 : 0;
		paddingArtworkValid_ = AuthenticateTownSurroundingsNativePaddingArtwork(
		    std::span<const uint8_t>(pixels.begin(), 2048), paddingOpacity_, 64, 32);
	}

	static float DecodeSrgb(uint8_t value)
	{
		const float c = value / 255.0F;
		return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
	}

	void UpdatePalette()
	{
		std::array<uint32_t, 256> signature;
		for (size_t i = 0; i < logical_palette.size(); ++i) {
			const auto &c = logical_palette[i];
			signature[i] = c.r | (static_cast<uint32_t>(c.g) << 8) | (static_cast<uint32_t>(c.b) << 16);
		}
		if (paletteValid_ && signature == paletteSignature_)
			return;
		paletteSignature_ = signature;
		for (size_t i = 0; i < logical_palette.size(); ++i) {
			const auto &c = logical_palette[i];
			linearPalette_[i] = { DecodeSrgb(c.r), DecodeSrgb(c.g), DecodeSrgb(c.b) };
		}
		std::fill(triangleColors_.begin(), triangleColors_.end(), UnknownColor);
		paletteValid_ = true;
	}

	uint8_t Color(size_t triangleIndex)
	{
		auto &cached = triangleColors_[triangleIndex];
		if (cached != UnknownColor)
			return static_cast<uint8_t>(cached);
		const auto &c = activeScene_->triangles[triangleIndex].color;
		float bestDistance = std::numeric_limits<float>::infinity();
		uint8_t best = 0;
		for (size_t index = 0; index < linearPalette_.size(); ++index) {
			const auto &other = linearPalette_[index];
			const float r = c.red - other.red;
			const float g = c.green - other.green;
			const float b = c.blue - other.blue;
			const float distance = r * r + g * g + b * b;
			if (distance < bestDistance) {
				bestDistance = distance;
				best = static_cast<uint8_t>(index);
			}
		}
		cached = best;
		return best;
	}

	Projected Project(const TownSurroundingsPoint &v) const
	{
		return { originX_ + 32.0 * (static_cast<double>(v.x) - v.z),
			originY_ + 16.0 * (static_cast<double>(v.x) + v.z) - 32.0 * v.height,
			static_cast<double>(v.x) + v.z + v.height };
	}

	static double Edge(const Projected &a, const Projected &b, double x, double y)
	{
		return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
	}

	void BeginScratch(int left, int top, int right, int bottom)
	{
		scratchLeft_ = left;
		scratchTop_ = top;
		scratchWidth_ = right - left + 1;
		scratchHeight_ = bottom - top + 1;
		scratchDepth_.assign(static_cast<size_t>(scratchWidth_) * scratchHeight_,
		    -std::numeric_limits<float>::infinity());
	}

	void DrawRange(const Surface &out, const TownSurroundingsCell &cell)
	{
		for (size_t t = cell.firstTriangle, end = t + cell.triangleCount; t < end; ++t) {
			const auto &triangle = activeScene_->triangles[t];
			const auto a = Project(triangle.vertices[0]);
			const auto b = Project(triangle.vertices[1]);
			const auto c = Project(triangle.vertices[2]);
			const double area = Edge(a, b, c.x, c.y);
			if (std::abs(area) < 0.000001)
				continue;
			const double minX = std::min({ a.x, b.x, c.x }), maxX = std::max({ a.x, b.x, c.x });
			const double minY = std::min({ a.y, b.y, c.y }), maxY = std::max({ a.y, b.y, c.y });
			if (maxX < 0 || maxY < 0 || minX >= out.w() || minY >= out.h())
				continue;
			const int left = static_cast<int>(std::floor(std::max(0.0, minX)));
			const int top = static_cast<int>(std::floor(std::max(0.0, minY)));
			const int right = static_cast<int>(std::ceil(std::min(out.w() - 1.0, maxX)));
			const int bottom = static_cast<int>(std::ceil(std::min(out.h() - 1.0, maxY)));
			const double inverseArea = 1.0 / area;
			const uint8_t color = Color(t);
			for (int y = top; y <= bottom; ++y) {
				uint8_t *destination = out.at(left, y);
				for (int x = left; x <= right; ++x, ++destination) {
					const double wa = Edge(b, c, x + 0.5, y + 0.5) * inverseArea;
					const double wb = Edge(c, a, x + 0.5, y + 0.5) * inverseArea;
					const double wc = 1.0 - wa - wb;
					if (wa < -0.000001 || wb < -0.000001 || wc < -0.000001)
						continue;
					const float depth = static_cast<float>(wa * a.depth + wb * b.depth + wc * c.depth);
					const size_t pixel = static_cast<size_t>(y - scratchTop_) * scratchWidth_ + (x - scratchLeft_);
					if (depth <= scratchDepth_[pixel])
						continue;
					scratchDepth_[pixel] = depth;
					// World write: Surface::SetPixel would incorrectly mark this
					// as UI coverage in the presentation compositor.
					*destination = color;
				}
			}
		}
	}

	const TownSurroundingsVisualScene *activeScene_ = nullptr;
	std::string identity_;
	std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
	std::vector<uint32_t> backgroundCells_;
	std::vector<uint16_t> triangleColors_;
	std::array<uint32_t, 256> paletteSignature_ {};
	std::array<TownSurroundingsColor, 256> linearPalette_ {};
	bool paletteValid_ = false;
	bool underlayPainted_ = false;
	bool paddingArtworkAttempted_ = false;
	bool paddingArtworkValid_ = false;
	const std::byte *paddingCelData_ = nullptr;
	std::array<uint16_t, 16> paddingWords_ {};
	uint_fast8_t paddingMicroTileLen_ = 0;
	TileProperties paddingSol_ = TileProperties::None;
	std::array<uint8_t, 2048> paddingOpacity_ {};
	std::array<uint8_t, MAXDUNX * MAXDUNY> suppressedPaddingCells_ {};
	int extraRows_ = 0;
	int extraRowsBefore_ = 0;
	int extraColumns_ = 0;
	double originX_ = 0;
	double originY_ = 0;
	int viewportWidth_ = 0;
	int viewportHeight_ = 0;
	std::vector<float> scratchDepth_;
	int scratchLeft_ = 0;
	int scratchTop_ = 0;
	int scratchWidth_ = 0;
	int scratchHeight_ = 0;
};

/** One owner for native-view derived resources, visible to world reset. */
inline TownSurroundingsNativeFrame &GetTownSurroundingsNativeFrame()
{
	static TownSurroundingsNativeFrame frame;
	return frame;
}

/** Call before ClearTownSurroundingsVisualScene, after 3D drops its refs. */
inline void ResetTownSurroundingsNativeResources()
{
	GetTownSurroundingsNativeFrame().Clear();
}

} // namespace devilution
