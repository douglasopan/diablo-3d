#include "engine/render/town_actor.hpp"

#include "engine/render/town_actor_mask.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace devilution {
namespace {

constexpr float Pi = 3.14159265358979323846F;
constexpr float HorizontalPixels = 45.25483399593904F;
constexpr float VerticalPixels = 32.0F;
constexpr float GroundDepthPixels = 22.62741699796952F;
constexpr int MaximumCellsPerAxis = 96;

struct Position {
	float x;
	float height;
	float z;
};

struct View {
	const TownVolumeSprite *sprite = nullptr;
	float sine = 0;
	float cosine = 1;

	std::array<float, 2> Project(Position p) const
	{
		return { sprite->width * 0.5F + HorizontalPixels * (cosine * p.x - sine * p.z),
			static_cast<float>(sprite->height - 1) + GroundDepthPixels * (sine * p.x + cosine * p.z) - VerticalPixels * p.height };
	}

	bool Opaque(float px, float py) const
	{
		const int x = static_cast<int>(std::floor(px));
		const int y = static_cast<int>(std::floor(py));
		return x >= 0 && x < sprite->width && y >= 0 && y < sprite->height
		    && sprite->opacity[static_cast<size_t>(y) * sprite->width + x] != 0;
	}

	bool Opaque(Position p) const
	{
		const auto pixel = Project(p);
		return Opaque(pixel[0], pixel[1]);
	}

	uint8_t Color(Position p) const
	{
		const auto pixel = Project(p);
		const int cx = static_cast<int>(std::floor(pixel[0]));
		const int cy = static_cast<int>(std::floor(pixel[1]));
		// Color zero is valid. Only the separate opacity channel determines paint.
		unsigned bestDistance = std::numeric_limits<unsigned>::max();
		uint8_t result = 0;
		for (int radius : { 0, 2, 6 }) {
			for (int y = std::max(0, cy - radius); y < std::min(sprite->height, cy + radius + 1); ++y) {
				for (int x = std::max(0, cx - radius); x < std::min(sprite->width, cx + radius + 1); ++x) {
					if (sprite->opacity[static_cast<size_t>(y) * sprite->width + x] == 0)
						continue;
					const unsigned distance = static_cast<unsigned>((x - cx) * (x - cx) + (y - cy) * (y - cy));
					if (distance < bestDistance) {
						bestDistance = distance;
						result = sprite->pixels[static_cast<size_t>(y) * sprite->width + x];
					}
				}
			}
			if (bestDistance != std::numeric_limits<unsigned>::max())
				break;
		}
		return result;
	}
};

struct Grid {
	int width;
	int height;
	int depth;
	float horizontalStep;
	float verticalStep;
	float left;
	float back;
	std::vector<uint8_t> occupied;
	std::vector<uint8_t> votes;

	size_t Index(int x, int y, int z) const
	{
		return (static_cast<size_t>(y) * depth + z) * width + x;
	}

	bool Has(int x, int y, int z) const
	{
		return x >= 0 && x < width && y >= 0 && y < height && z >= 0 && z < depth
		    && occupied[Index(x, y, z)] != 0;
	}

	Position Corner(int x, int y, int z) const
	{
		return { left + x * horizontalStep, y * verticalStep, back + z * horizontalStep };
	}

	Position Center(int x, int y, int z) const
	{
		const Position corner = Corner(x, y, z);
		return { corner.x + horizontalStep / 2, corner.height + verticalStep / 2, corner.z + horizontalStep / 2 };
	}

	int Neighbors(int x, int y, int z) const
	{
		return static_cast<int>(Has(x - 1, y, z)) + static_cast<int>(Has(x + 1, y, z))
		    + static_cast<int>(Has(x, y - 1, z)) + static_cast<int>(Has(x, y + 1, z))
		    + static_cast<int>(Has(x, y, z - 1)) + static_cast<int>(Has(x, y, z + 1));
	}
};

bool Valid(const TownVolumeSprite &sprite)
{
	if (sprite.width <= 0 || sprite.height <= 0 || sprite.width > 512 || sprite.height > 512)
		return false;
	const size_t size = static_cast<size_t>(sprite.width) * sprite.height;
	return sprite.pixels.size() >= size && sprite.opacity.size() >= size
	    && std::any_of(sprite.opacity.begin(), sprite.opacity.begin() + size, [](uint8_t value) { return value != 0; });
}

/** Strict reference constraint includes the projected cell footprint, not merely
 * its center. Project the box corners, then test pixel centers inside their
 * convex hull. This prevents opaque geometry from crossing a native leg gap. */
bool ReferenceCell(const Grid &grid, const View &view, int x, int y, int z)
{
	if (!view.Opaque(grid.Center(x, y, z)))
		return false;
	std::array<std::array<float, 2>, 8> points;
	int i = 0;
	for (int dz = 0; dz <= 1; ++dz)
		for (int dy = 0; dy <= 1; ++dy)
			for (int dx = 0; dx <= 1; ++dx)
				points[i++] = view.Project(grid.Corner(x + dx, y + dy, z + dz));
	std::sort(points.begin(), points.end());
	const auto cross = [](const std::array<float, 2> &a, const std::array<float, 2> &b, const std::array<float, 2> &c) {
		return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
	};
	std::array<std::array<float, 2>, 16> hull;
	size_t count = 0;
	for (const auto &point : points) {
		while (count >= 2 && cross(hull[count - 2], hull[count - 1], point) <= 0)
			--count;
		hull[count++] = point;
	}
	const size_t lower = count;
	for (size_t point = points.size() - 1; point-- > 0;) {
		while (count > lower && cross(hull[count - 2], hull[count - 1], points[point]) <= 0)
			--count;
		hull[count++] = points[point];
	}
	if (count < 4)
		return false;
	--count; // The last point repeats the first.
	float minX = hull[0][0], maxX = minX, minY = hull[0][1], maxY = minY;
	for (size_t point = 1; point < count; ++point) {
		minX = std::min(minX, hull[point][0]);
		maxX = std::max(maxX, hull[point][0]);
		minY = std::min(minY, hull[point][1]);
		maxY = std::max(maxY, hull[point][1]);
	}
	for (int py = static_cast<int>(std::floor(minY)); py <= static_cast<int>(std::floor(maxY)); ++py) {
		for (int px = static_cast<int>(std::floor(minX)); px <= static_cast<int>(std::floor(maxX)); ++px) {
			const std::array<float, 2> pixel { px + 0.5F, py + 0.5F };
			bool inside = true;
			for (size_t point = 0; point < count; ++point)
				inside = inside && cross(hull[point], hull[(point + 1) % count], pixel) >= -0.0001F;
			if (inside && !view.Opaque(pixel[0], pixel[1]))
				return false;
		}
	}
	return true;
}

/** Resolve four-face diagonal edge contacts by removing the weaker cell.
 * Removal preserves the hard reference mask; adding silhouette pixels would not.
 * Every pass removes cells, so the deterministic repair cannot oscillate. */
void CloseDiagonalEdges(Grid &grid)
{
	const auto resolve = [&](const std::array<std::array<int, 3>, 4> &cells) {
		std::array<bool, 4> has;
		for (size_t i = 0; i < has.size(); ++i)
			has[i] = grid.Has(cells[i][0], cells[i][1], cells[i][2]);
		int first = -1, second = -1;
		if (has[0] && has[3] && !has[1] && !has[2]) {
			first = 0;
			second = 3;
		} else if (has[1] && has[2] && !has[0] && !has[3]) {
			first = 1;
			second = 2;
		} else {
			return false;
		}
		const auto strength = [&](int which) {
			const auto &cell = cells[which];
			return grid.Neighbors(cell[0], cell[1], cell[2]) * 16 + grid.votes[grid.Index(cell[0], cell[1], cell[2])];
		};
		const auto &weak = cells[strength(first) < strength(second) ? first : second];
		grid.occupied[grid.Index(weak[0], weak[1], weak[2])] = 0;
		return true;
	};
	bool changed;
	do {
		changed = false;
		for (int y = 0; y < grid.height; ++y) {
			for (int z = 0; z < grid.depth; ++z) {
				for (int x = 0; x < grid.width; ++x) {
					if (x + 1 < grid.width && y + 1 < grid.height)
						changed = resolve({ std::array<int, 3> { x, y, z }, { x + 1, y, z }, { x, y + 1, z }, { x + 1, y + 1, z } }) || changed;
					if (x + 1 < grid.width && z + 1 < grid.depth)
						changed = resolve({ std::array<int, 3> { x, y, z }, { x + 1, y, z }, { x, y, z + 1 }, { x + 1, y, z + 1 } }) || changed;
					if (y + 1 < grid.height && z + 1 < grid.depth)
						changed = resolve({ std::array<int, 3> { x, y, z }, { x, y + 1, z }, { x, y, z + 1 }, { x, y + 1, z + 1 } }) || changed;
				}
			}
		}
	} while (changed);
}

void Face(TownVolumeMesh &mesh, const std::array<Position, 4> &corners, Position normal,
	const std::array<View, 8> &views, int forwardView)
{
	Position center {};
	for (const Position corner : corners) {
		center.x += corner.x / 4;
		center.height += corner.height / 4;
		center.z += corner.z / 4;
	}
	int selected = forwardView;
	float best = -std::numeric_limits<float>::infinity();
	for (int offset = 0; offset < 8; ++offset) {
		const int index = (forwardView + offset) % 8;
		if (!views[index].Opaque(center))
			continue;
		const float score = normal.x * views[index].sine + normal.z * views[index].cosine + normal.height * 0.7071067811865475F;
		if (score > best + 0.0001F) {
			best = score;
			selected = index;
		}
	}
	std::array<TownVolumeVertex, 4> vertices;
	for (size_t i = 0; i < corners.size(); ++i) {
		const Position point = corners[i];
		const auto pixel = views[selected].Project(point);
		vertices[i] = { point.x, point.height, point.z, pixel[0] / views[selected].sprite->width, pixel[1] / views[selected].sprite->height };
	}
	const TownVolumeMaterial material = best > 0 && normal.height >= 0 ? TownVolumeMaterial::SpriteFront : TownVolumeMaterial::SpriteSide;
	const uint8_t color = views[selected].Color(center);
	mesh.triangles.push_back({ { vertices[0], vertices[1], vertices[2] }, material, color, static_cast<uint8_t>(selected) });
	mesh.triangles.push_back({ { vertices[0], vertices[2], vertices[3] }, material, color, static_cast<uint8_t>(selected) });
}

} // namespace

TownVolumeMesh BuildTownActorVisualHull(const std::array<TownVolumeSprite, 8> &sprites,
	int forwardView, int pixelStep, int minimumViews)
{
	TownVolumeMesh mesh;
	if (forwardView < 0 || forwardView >= 8 || !std::all_of(sprites.begin(), sprites.end(), Valid))
		return mesh;
	std::array<std::vector<uint8_t>, 8> bodyOpacity;
	std::array<TownVolumeSprite, 8> bodySprites;
	for (size_t i = 0; i < sprites.size(); ++i) {
		bodyOpacity[i] = TownActorBodyOpacity(sprites[i]);
		bodySprites[i] = { sprites[i].width, sprites[i].height, sprites[i].pixels, bodyOpacity[i] };
	}
	if (!std::all_of(bodySprites.begin(), bodySprites.end(), Valid))
		return mesh;
	pixelStep = std::clamp(pixelStep, 1, 8);
	minimumViews = std::clamp(minimumViews, 1, 8);
	std::array<View, 8> views;
	float radius = 0, top = 0;
	for (int i = 0; i < 8; ++i) {
		const float angle = static_cast<float>((i - forwardView + 8) % 8) * Pi / 4;
		views[i] = { &bodySprites[i], std::sin(angle), std::cos(angle) };
		const TownVolumeSprite &sprite = bodySprites[i];
		for (int y = 0; y < sprite.height; ++y) {
			for (int x = 0; x < sprite.width; ++x) {
				if (sprite.opacity[static_cast<size_t>(y) * sprite.width + x] == 0)
					continue;
				radius = std::max(radius, std::max(std::abs(x - sprite.width * 0.5F), std::abs(x + 1 - sprite.width * 0.5F)) / HorizontalPixels);
				top = std::max(top, static_cast<float>(sprite.height - y) / VerticalPixels);
			}
		}
	}
	float horizontalStep = static_cast<float>(pixelStep) / HorizontalPixels;
	float verticalStep = static_cast<float>(pixelStep) / VerticalPixels;
	top += radius * GroundDepthPixels / VerticalPixels;
	const float scale = std::max({ 1.0F, 2 * radius / (horizontalStep * MaximumCellsPerAxis), top / (verticalStep * MaximumCellsPerAxis) });
	horizontalStep *= scale;
	verticalStep *= scale;
	const int halfColumns = std::max(1, static_cast<int>(std::ceil(radius / horizontalStep)));
	Grid grid { 2 * halfColumns, std::max(1, static_cast<int>(std::ceil(top / verticalStep))), 2 * halfColumns,
		horizontalStep, verticalStep, -halfColumns * horizontalStep, -halfColumns * horizontalStep, {}, {} };
	const size_t size = static_cast<size_t>(grid.width) * grid.height * grid.depth;
	grid.occupied.resize(size, 0);
	grid.votes.resize(size, 0);
	for (int y = 0; y < grid.height; ++y) {
		for (int z = 0; z < grid.depth; ++z) {
			for (int x = 0; x < grid.width; ++x) {
				if (!ReferenceCell(grid, views[forwardView], x, y, z))
					continue;
				const Position center = grid.Center(x, y, z);
				int votes = 0;
				std::array<bool, 8> accepted;
				for (size_t i = 0; i < views.size(); ++i) {
					accepted[i] = views[i].Opaque(center);
					votes += static_cast<int>(accepted[i]);
				}
				// Losing both opposing profiles would discard every constraint on
				// that axis: six agreeing broad views could still produce a slab.
				// Retain one constraint from each opposite pair while tolerating
				// small disagreements between the separately drawn native poses.
				bool directionsCovered = true;
				for (size_t i = 0; i < 4; ++i)
					directionsCovered = directionsCovered && (accepted[i] || accepted[i + 4]);
				grid.votes[grid.Index(x, y, z)] = static_cast<uint8_t>(votes);
				grid.occupied[grid.Index(x, y, z)] = static_cast<uint8_t>(votes >= minimumViews && directionsCovered);
			}
		}
	}
	CloseDiagonalEdges(grid);
	Position low { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };
	Position high { -low.x, -low.height, -low.z };
	for (int y = 0; y < grid.height; ++y) {
		for (int z = 0; z < grid.depth; ++z) {
			for (int x = 0; x < grid.width; ++x) {
				if (!grid.Has(x, y, z))
					continue;
				const Position a = grid.Corner(x, y, z);
				const Position b = grid.Corner(x + 1, y + 1, z + 1);
				low = { std::min(low.x, a.x), std::min(low.height, a.height), std::min(low.z, a.z) };
				high = { std::max(high.x, b.x), std::max(high.height, b.height), std::max(high.z, b.z) };
				if (!grid.Has(x - 1, y, z))
					Face(mesh, { Position { a.x, a.height, a.z }, { a.x, a.height, b.z }, { a.x, b.height, b.z }, { a.x, b.height, a.z } }, { -1, 0, 0 }, views, forwardView);
				if (!grid.Has(x + 1, y, z))
					Face(mesh, { Position { b.x, a.height, b.z }, { b.x, a.height, a.z }, { b.x, b.height, a.z }, { b.x, b.height, b.z } }, { 1, 0, 0 }, views, forwardView);
				if (!grid.Has(x, y - 1, z))
					Face(mesh, { Position { a.x, a.height, a.z }, { b.x, a.height, a.z }, { b.x, a.height, b.z }, { a.x, a.height, b.z } }, { 0, -1, 0 }, views, forwardView);
				if (!grid.Has(x, y + 1, z))
					Face(mesh, { Position { a.x, b.height, b.z }, { b.x, b.height, b.z }, { b.x, b.height, a.z }, { a.x, b.height, a.z } }, { 0, 1, 0 }, views, forwardView);
				if (!grid.Has(x, y, z - 1))
					Face(mesh, { Position { b.x, a.height, a.z }, { a.x, a.height, a.z }, { a.x, b.height, a.z }, { b.x, b.height, a.z } }, { 0, 0, -1 }, views, forwardView);
				if (!grid.Has(x, y, z + 1))
					Face(mesh, { Position { a.x, a.height, b.z }, { b.x, a.height, b.z }, { b.x, b.height, b.z }, { a.x, b.height, b.z } }, { 0, 0, 1 }, views, forwardView);
			}
		}
	}
	if (!mesh.triangles.empty()) {
		// Native sprites have transparent bottom padding. Without calibration,
		// intersecting their silhouettes places the entire body above the floor.
		// Translate the complete rigid body along the native reference ray:
		// 16*sqrt(2)*deltaZ - 32*deltaHeight == 0. Its front image and all UVs
		// remain unchanged, but the lowest physical surface now rests at Y=0.
		// This is one translation, never a depth-dependent height shear.
		const float depthOffset = low.height * 1.4142135623730951F;
		for (TownVolumeTriangle &triangle : mesh.triangles) {
			for (TownVolumeVertex &vertex : triangle.vertices) {
				vertex.height -= low.height;
				vertex.z -= depthOffset;
			}
		}
		mesh.width = high.x - low.x;
		mesh.height = high.height - low.height;
		mesh.depth = high.z - low.z;
	}
	return mesh;
}

} // namespace devilution
