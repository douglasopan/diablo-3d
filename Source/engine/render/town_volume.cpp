#include "engine/render/town_volume.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace devilution {
namespace {

constexpr float Pi = 3.14159265358979323846F;
constexpr float HorizontalPixels = 45.25483399593904F;
constexpr float VerticalPixels = 32.0F;

struct Vec {
	float x;
	float y;
	float z;
	Vec operator+(Vec rhs) const { return { x + rhs.x, y + rhs.y, z + rhs.z }; }
	Vec operator-(Vec rhs) const { return { x - rhs.x, y - rhs.y, z - rhs.z }; }
	Vec operator*(float value) const { return { x * value, y * value, z * value }; }
};

float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec Cross(Vec a, Vec b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Vec Normalize(Vec a)
{
	const float length = std::sqrt(Dot(a, a));
	return length > 0.000001F ? a * (1.0F / length) : Vec { 0, 1, 0 };
}

Vec Position(const TownVolumeVertex &v) { return { v.x, v.height, v.z }; }

struct Image {
	const TownVolumeSprite &sprite;
	int left = 0;
	int top = 0;
	int right = -1;
	int bottom = -1;

	bool Opaque(int x, int y) const
	{
		return x >= 0 && y >= 0 && x < sprite.width && y < sprite.height
		    && sprite.opacity[static_cast<size_t>(y) * sprite.width + x] != 0;
	}

	bool FindBounds()
	{
		if (sprite.width <= 0 || sprite.height <= 0 || sprite.width > 2048 || sprite.height > 2048)
			return false;
		const size_t size = static_cast<size_t>(sprite.width) * sprite.height;
		if (sprite.pixels.size() < size || sprite.opacity.size() < size)
			return false;
		left = sprite.width;
		top = sprite.height;
		for (int y = 0; y < sprite.height; ++y) {
			for (int x = 0; x < sprite.width; ++x) {
				if (!Opaque(x, y))
					continue;
				left = std::min(left, x);
				right = std::max(right, x);
				top = std::min(top, y);
				bottom = std::max(bottom, y);
			}
		}
		return right >= left && bottom >= top;
	}

	uint8_t Color(int cx, int cy, int radius = 3) const
	{
		std::array<unsigned, 256> counts {};
		for (int y = std::max(top, cy - radius); y <= std::min(bottom, cy + radius); ++y)
			for (int x = std::max(left, cx - radius); x <= std::min(right, cx + radius); ++x)
				if (Opaque(x, y))
					++counts[sprite.pixels[static_cast<size_t>(y) * sprite.width + x]];
		const auto best = std::max_element(counts.begin(), counts.end());
		if (*best != 0)
			return static_cast<uint8_t>(best - counts.begin());
		// Sparse branch tips can sit a few pixels outside the painted line.
		unsigned closest = std::numeric_limits<unsigned>::max();
		uint8_t result = 0;
		for (int y = top; y <= bottom; ++y) {
			for (int x = left; x <= right; ++x) {
				if (!Opaque(x, y))
					continue;
				const unsigned distance = static_cast<unsigned>((x - cx) * (x - cx) + (y - cy) * (y - cy));
				if (distance < closest) {
					closest = distance;
					result = sprite.pixels[static_cast<size_t>(y) * sprite.width + x];
				}
			}
		}
		return result;
	}

	TownVolumeVertex Vertex(Vec point) const
	{
		// Native projected UVs are available for front materials. Opaque side/back
		// materials use paletteIndex instead, so they cannot stretch a painted face.
		return { point.x, point.y, point.z,
			(point.x * HorizontalPixels + sprite.width * 0.5F) / sprite.width,
			(sprite.height - point.y * VerticalPixels) / sprite.height };
	}
};

void Triangle(TownVolumeMesh &mesh, TownVolumeVertex a, TownVolumeVertex b, TownVolumeVertex c,
	TownVolumeMaterial material, uint8_t color, Vec outward)
{
	const Vec normal = Cross(Position(b) - Position(a), Position(c) - Position(a));
	if (Dot(normal, normal) < 0.0000000001F)
		return;
	if (Dot(normal, outward) < 0)
		std::swap(b, c);
	mesh.triangles.push_back({ { a, b, c }, material, color });
}

void Quad(TownVolumeMesh &mesh, const std::array<TownVolumeVertex, 4> &v,
	TownVolumeMaterial material, uint8_t color, Vec outward)
{
	Triangle(mesh, v[0], v[1], v[2], material, color, outward);
	Triangle(mesh, v[0], v[2], v[3], material, color, outward);
}

void Measure(TownVolumeMesh &mesh)
{
	if (mesh.triangles.empty())
		return;
	Vec low = Position(mesh.triangles.front().vertices.front());
	Vec high = low;
	for (const auto &triangle : mesh.triangles) {
		for (const auto &vertex : triangle.vertices) {
			low.x = std::min(low.x, vertex.x);
			low.y = std::min(low.y, vertex.height);
			low.z = std::min(low.z, vertex.z);
			high.x = std::max(high.x, vertex.x);
			high.y = std::max(high.y, vertex.height);
			high.z = std::max(high.z, vertex.z);
		}
	}
	mesh.width = high.x - low.x;
	mesh.height = high.y - low.y;
	mesh.depth = high.z - low.z;
}

uint32_t Random(uint32_t &state)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

float SignedRandom(uint32_t &state)
{
	return static_cast<float>(Random(state) & 65535U) / 32767.5F - 1;
}

void Tube(TownVolumeMesh &mesh, const Image &image, const std::vector<Vec> &points,
	const std::vector<float> &radii, int sides, TownVolumeMaterial material, uint8_t color)
{
	if (points.size() < 2 || points.size() != radii.size())
		return;
	std::vector<TownVolumeVertex> rings(points.size() * sides);
	Vec previousRight { 0, 0, 0 };
	for (size_t i = 0; i < points.size(); ++i) {
		const Vec tangent = Normalize(points[std::min(i + 1, points.size() - 1)] - points[i == 0 ? 0 : i - 1]);
		const Vec reference = std::abs(tangent.y) < 0.9F ? Vec { 0, 1, 0 } : Vec { 1, 0, 0 };
		const Vec transported = previousRight - tangent * Dot(previousRight, tangent);
		const Vec right = i != 0 && Dot(transported, transported) > 0.000001F
		    ? Normalize(transported) : Normalize(Cross(tangent, reference));
		previousRight = right;
		const Vec up = Cross(right, tangent);
		for (int side = 0; side < sides; ++side) {
			const float angle = 2 * Pi * side / sides;
			const Vec point = points[i] + right * (std::cos(angle) * radii[i]) + up * (std::sin(angle) * radii[i]);
			rings[i * sides + side] = image.Vertex(point);
		}
	}
	for (size_t i = 1; i < points.size(); ++i) {
		for (int side = 0; side < sides; ++side) {
			const int next = (side + 1) % sides;
			const auto &a = rings[(i - 1) * sides + side];
			const auto &b = rings[(i - 1) * sides + next];
			const auto &c = rings[i * sides + next];
			const auto &d = rings[i * sides + side];
			const Vec outward = (Position(a) + Position(b) + Position(c) + Position(d)) * 0.25F
			    - (points[i - 1] + points[i]) * 0.5F;
			Quad(mesh, { a, b, c, d }, material, color, outward);
		}
	}
	const auto first = image.Vertex(points.front());
	const auto last = image.Vertex(points.back());
	for (int side = 0; side < sides; ++side) {
		const int next = (side + 1) % sides;
		Triangle(mesh, first, rings[side], rings[next], material, color, points.front() - points[1]);
		Triangle(mesh, last, rings[(points.size() - 1) * sides + side], rings[(points.size() - 1) * sides + next],
			material, color, points.back() - points[points.size() - 2]);
	}
}

void FoliageBall(TownVolumeMesh &mesh, const Image &image, Vec center, Vec radius, uint8_t color)
{
	constexpr int Sides = 8;
	constexpr int Rings = 4;
	std::array<std::array<TownVolumeVertex, Sides>, Rings> vertices;
	for (int ring = 0; ring < Rings; ++ring) {
		const float latitude = Pi * (ring + 1) / (Rings + 1);
		for (int side = 0; side < Sides; ++side) {
			const float angle = 2 * Pi * side / Sides;
			vertices[ring][side] = image.Vertex(center + Vec {
				std::sin(latitude) * std::cos(angle) * radius.x,
				std::cos(latitude) * radius.y,
				std::sin(latitude) * std::sin(angle) * radius.z });
		}
	}
	const auto top = image.Vertex(center + Vec { 0, radius.y, 0 });
	const auto bottom = image.Vertex(center - Vec { 0, radius.y, 0 });
	for (int side = 0; side < Sides; ++side) {
		const int next = (side + 1) % Sides;
		Triangle(mesh, top, vertices[0][side], vertices[0][next], TownVolumeMaterial::Foliage, color, { 0, 1, 0 });
		Triangle(mesh, bottom, vertices[Rings - 1][side], vertices[Rings - 1][next], TownVolumeMaterial::Foliage, color, { 0, -1, 0 });
		for (int ring = 1; ring < Rings; ++ring) {
			const auto &a = vertices[ring - 1][side];
			const auto &b = vertices[ring - 1][next];
			const auto &c = vertices[ring][next];
			const auto &d = vertices[ring][side];
			const float front = (a.z + b.z + c.z + d.z) * 0.25F - center.z;
			Quad(mesh, { a, b, c, d }, front > 0 ? TownVolumeMaterial::SpriteFront : TownVolumeMaterial::Foliage,
				color, Position(a) - center);
		}
	}
}

TownVolumeMesh BuildSilhouetteVolume(const TownVolumeSprite &sprite, int pixelStep, bool prop)
{
	TownVolumeMesh mesh;
	Image image { sprite };
	if (!image.FindBounds())
		return mesh;
	// Keep pathological input bounded while normal 64/96px native actors retain
	// their hand/leg gaps at the default two-pixel contour sampling.
	pixelStep = std::max(std::clamp(pixelStep, 1, 8), (std::max(sprite.width, sprite.height) + 127) / 128);
	const int columns = (image.right - image.left + pixelStep) / pixelStep;
	const int rows = (image.bottom - image.top + pixelStep) / pixelStep;
	std::vector<uint8_t> occupied(static_cast<size_t>(columns) * rows, 0);
	std::vector<float> depth(occupied.size(), 0);
	const auto index = [columns](int x, int y) { return static_cast<size_t>(y) * columns + x; };
	const auto has = [&](int x, int y) {
		return x >= 0 && y >= 0 && x < columns && y < rows && occupied[index(x, y)] != 0;
	};
	for (int y = 0; y < rows; ++y) {
		for (int x = 0; x < columns; ++x) {
			for (int py = image.top + y * pixelStep; py <= std::min(image.bottom, image.top + (y + 1) * pixelStep - 1); ++py)
				for (int px = image.left + x * pixelStep; px <= std::min(image.right, image.left + (x + 1) * pixelStep - 1); ++px)
					if (image.Opaque(px, py))
						occupied[index(x, y)] = 1;
		}
	}
	// A pixel contour can touch only at a diagonal corner, producing a pinched
	// four-face edge when closed. Join one subpixel corner so the volume has a
	// proper two-face boundary, without filling the larger spaces between limbs.
	bool changed;
	do {
		changed = false;
		for (int y = 0; y + 1 < rows; ++y) {
			for (int x = 0; x + 1 < columns; ++x) {
				if (has(x, y) && has(x + 1, y + 1) && !has(x + 1, y) && !has(x, y + 1)) {
					occupied[index(x + 1, y)] = 1;
					changed = true;
				} else if (!has(x, y) && !has(x + 1, y + 1) && has(x + 1, y) && has(x, y + 1)) {
					occupied[index(x, y)] = 1;
					changed = true;
				}
			}
		}
	} while (changed);
	const float bodyHeight = static_cast<float>(image.bottom - image.top + 1) / VerticalPixels;
	for (int y = 0; y < rows; ++y) {
		for (int x = 0; x < columns;) {
			if (!has(x, y)) {
				++x;
				continue;
			}
			const int begin = x;
			while (x < columns && has(x, y))
				++x;
			const int end = x;
			const float runWidth = static_cast<float>((end - begin) * pixelStep) / HorizontalPixels;
			const float anatomyHeight = 1.0F - (static_cast<float>(y) + 0.5F) / rows;
			const float anatomicalRadius = bodyHeight * (anatomyHeight > 0.83F ? 0.085F : anatomyHeight > 0.40F ? 0.105F : anatomyHeight > 0.10F ? 0.075F : 0.05F);
			const float radius = prop
			    ? std::clamp(std::min(0.35F * runWidth, 0.45F * bodyHeight), 0.025F, 0.9F)
			    : std::clamp(std::min(anatomicalRadius, runWidth * 0.48F + bodyHeight * 0.008F), 0.025F, 0.34F);
			for (int cell = begin; cell < end; ++cell) {
				const float lateral = 2 * ((cell - begin + 0.5F) / (end - begin)) - 1;
				depth[index(cell, y)] = radius * (0.25F + 0.75F * std::sqrt(std::max(0.0F, 1 - lateral * lateral)));
			}
		}
	}
	const auto corner = [&](int x, int y, bool front) {
		float sum = 0;
		int count = 0;
		for (int dy = -1; dy <= 0; ++dy)
			for (int dx = -1; dx <= 0; ++dx)
				if (has(x + dx, y + dy)) {
					sum += depth[index(x + dx, y + dy)];
					++count;
				}
		const float thickness = sum / std::max(1, count);
		const float px = static_cast<float>(std::min(image.right + 1, image.left + x * pixelStep));
		const float py = static_cast<float>(std::min(image.bottom + 1, image.top + y * pixelStep));
		return TownVolumeVertex { (px - sprite.width * 0.5F) / HorizontalPixels,
			(sprite.height - py) / VerticalPixels, front ? thickness : -thickness,
			px / sprite.width, py / sprite.height };
	};
	mesh.triangles.reserve(occupied.size() * 5);
	for (int y = 0; y < rows; ++y) {
		for (int x = 0; x < columns; ++x) {
			if (!has(x, y))
				continue;
			const uint8_t color = image.Color(image.left + x * pixelStep + pixelStep / 2, image.top + y * pixelStep + pixelStep / 2);
			const auto a = corner(x, y, true);
			const auto b = corner(x + 1, y, true);
			const auto c = corner(x + 1, y + 1, true);
			const auto d = corner(x, y + 1, true);
			const auto ab = corner(x, y, false);
			const auto bb = corner(x + 1, y, false);
			const auto cb = corner(x + 1, y + 1, false);
			const auto db = corner(x, y + 1, false);
			Quad(mesh, { a, b, c, d }, TownVolumeMaterial::SpriteFront, color, { 0, 0, 1 });
			Quad(mesh, { ab, bb, cb, db }, TownVolumeMaterial::SpriteBack, color, { 0, 0, -1 });
			if (!has(x - 1, y)) Quad(mesh, { a, d, db, ab }, TownVolumeMaterial::SpriteSide, color, { -1, 0, 0 });
			if (!has(x + 1, y)) Quad(mesh, { b, c, cb, bb }, TownVolumeMaterial::SpriteSide, color, { 1, 0, 0 });
			if (!has(x, y - 1)) Quad(mesh, { a, b, bb, ab }, TownVolumeMaterial::SpriteSide, color, { 0, 1, 0 });
			if (!has(x, y + 1)) Quad(mesh, { d, c, cb, db }, TownVolumeMaterial::SpriteSide, color, { 0, -1, 0 });
		}
	}
	Measure(mesh);
	return mesh;
}

} // namespace

TownVolumeMesh BuildTownActorVolume(const TownVolumeSprite &sprite, int pixelStep)
{
	return BuildSilhouetteVolume(sprite, pixelStep, false);
}

TownVolumeMesh BuildTownPropVolume(const TownVolumeSprite &sprite, int pixelStep)
{
	return BuildSilhouetteVolume(sprite, pixelStep, true);
}

TownVolumeMesh BuildTownTreeVolume(const TownVolumeSprite &sprite, uint32_t seed, bool foliage)
{
	TownVolumeMesh mesh;
	Image image { sprite };
	if (!image.FindBounds())
		return mesh;
	uint32_t random = seed != 0 ? seed : 0x6D2B79F5U;
	const float height = std::max(0.5F, static_cast<float>(sprite.height - 1 - image.top) / VerticalPixels);
	const float spread = std::max(0.2F, static_cast<float>(image.right - image.left + 1) / HorizontalPixels);
	const float barkRadius = std::clamp(height * 0.027F, 0.035F, 0.16F);
	const float bend = SignedRandom(random) * spread * 0.06F;
	const uint8_t bark = image.Color(sprite.width / 2, image.bottom - (image.bottom - image.top) / 4, 4);
	const auto trunkAt = [&](float t) { return Vec { bend * t * t, height * t, height * 0.05F * t * t }; };
	Tube(mesh, image, { { 0, 0, 0 }, { 0, height * 0.18F, 0 }, trunkAt(0.45F), trunkAt(0.70F), trunkAt(0.94F) },
		{ barkRadius, barkRadius * 0.93F, barkRadius * 0.74F, barkRadius * 0.46F, barkRadius * 0.15F }, 10, TownVolumeMaterial::Bark, bark);
	// Fit the outward tips to painted bands instead of fabricating uniformly
	// crossed sprite cards. Deterministic depth makes branches genuinely 3D.
	for (int band = 0; band < 7; ++band) {
		const float fraction = 0.30F + band * 0.10F;
		const int cy = std::clamp(sprite.height - 1 - static_cast<int>(height * fraction * VerticalPixels), image.top, image.bottom);
		int left = sprite.width;
		int right = -1;
		for (int y = std::max(image.top, cy - 3); y <= std::min(image.bottom, cy + 3); ++y) {
			for (int x = image.left; x <= image.right; ++x) {
				if (image.Opaque(x, y)) {
					left = std::min(left, x);
					right = std::max(right, x);
				}
			}
		}
		if (right < left)
			continue;
		for (int sign : { -1, 1 }) {
			const int pixelX = sign < 0 ? left : right;
			const float tipX = (pixelX - sprite.width * 0.5F) / HorizontalPixels;
			if (std::abs(tipX) < barkRadius * 1.5F)
				continue;
			// Opposite branches attach at distinct points inside the trunk. Starting
			// both caps at its exact center can also align a diameter of their rings:
			// nearly coincident radial edges then have four incident faces. Separate
			// the actual attachments, keeping each capped root inside the trunk.
			const float attachment = std::max(0.001F, barkRadius * 0.12F);
			const Vec start = trunkAt(std::max(0.12F, fraction - 0.18F)) + Vec { sign * attachment, 0, 0 };
			const float tipDepth = SignedRandom(random) * std::max(spread * 0.22F, height * 0.12F);
			// Local height is measured in the native image plane. The caller may
			// shear height by z/sqrt(2) to project depth along the native viewing ray.
			const Vec end { tipX, height * fraction, tipDepth };
			const Vec middle = start + (end - start) * 0.57F + Vec { -sign * spread * 0.03F, height * 0.035F, tipDepth * 0.12F };
			const float radius = barkRadius * (0.58F - band * 0.055F);
			const uint8_t color = image.Color(pixelX, cy, 3);
			Tube(mesh, image, { start, middle, end }, { radius, radius * 0.62F, radius * 0.18F }, 6, TownVolumeMaterial::Branch, color);
			for (int twig = 0; twig < 2; ++twig) {
				const Vec fork = middle + (end - middle) * (0.20F + 0.26F * twig);
				const Vec twigEnd = fork + Vec { sign * spread * (0.065F + 0.025F * twig), height * (0.065F + 0.025F * twig),
					SignedRandom(random) * spread * 0.16F };
				Tube(mesh, image, { fork, twigEnd }, { radius * 0.42F, std::max(0.006F, radius * 0.08F) }, 5, TownVolumeMaterial::Branch, color);
			}
		}
	}
	if (foliage) {
		// Fit small leaf clusters to dense painted patches. One broad ellipsoid
		// per branch band filled the empty crown with huge opaque blobs, hiding
		// nearby buildings and actors even though the source leaves were sparse.
		constexpr int Patch = 10;
		for (int y = image.top; y <= image.bottom; y += Patch) {
			for (int x = image.left; x <= image.right; x += Patch) {
				int count = 0, sumX = 0, sumY = 0;
				int left = x + Patch, right = x, top = y + Patch, bottom = y;
				for (int py = y; py <= std::min(image.bottom, y + Patch - 1); ++py) {
					for (int px = x; px <= std::min(image.right, x + Patch - 1); ++px) {
						if (!image.Opaque(px, py))
							continue;
						++count;
						sumX += px;
						sumY += py;
						left = std::min(left, px);
						right = std::max(right, px);
						top = std::min(top, py);
						bottom = std::max(bottom, py);
					}
				}
				if (count < 28)
					continue;
				const float px = static_cast<float>(sumX) / count;
				const float py = static_cast<float>(sumY) / count;
				const float cx = (px - sprite.width * 0.5F) / HorizontalPixels;
				const float cy = (sprite.height - 1 - py) / VerticalPixels;
				if (cy < height * 0.35F || (cy < height * 0.70F && std::abs(cx) < barkRadius * 2))
					continue;
				const float radiusX = (right - left + 1) * 0.5F / HorizontalPixels;
				const float radiusY = (bottom - top + 1) * 0.5F / VerticalPixels;
				const float radiusZ = std::min(radiusX, radiusY) * 0.85F;
				FoliageBall(mesh, image, { cx, cy, SignedRandom(random) * std::min(spread * 0.16F, height * 0.12F) },
					{ radiusX, radiusY, std::max(0.025F, radiusZ) }, image.Color(static_cast<int>(px), static_cast<int>(py), 3));
			}
		}
	}
	Measure(mesh);
	return mesh;
}

} // namespace devilution
