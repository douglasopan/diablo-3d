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

namespace {

uint32_t PropHash(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7feb352dU;
	value ^= value >> 15;
	value *= 0x846ca68bU;
	return value ^ (value >> 16);
}

TownVolumeVertex PhysicalPropVertex(float x, float height, float z)
{
	return { x, height, z, 0, 0 };
}

void PhysicalPropTriangle(TownVolumeMesh &mesh, TownVolumeVertex a, TownVolumeVertex b,
    TownVolumeVertex c, uint8_t color, Vec outward)
{
	Triangle(mesh, a, b, c, TownVolumeMaterial::SpriteSide, color, outward);
}

void SmallGroundedRock(TownVolumeMesh &mesh, float centerX, float centerZ,
    float radiusX, float radiusZ, float height, uint32_t seed, uint8_t color)
{
	constexpr size_t Sides = 10;
	constexpr std::array<float, 3> RingRadius { 0.72F, 1.0F, 0.60F };
	constexpr std::array<float, 3> RingHeight { 0, 0.32F, 0.78F };
	std::array<std::array<TownVolumeVertex, Sides>, 3> rings;
	const float yaw = static_cast<float>(PropHash(seed) & 65535U) * (2 * Pi / 65536);
	for (size_t ring = 0; ring < rings.size(); ++ring) {
		for (size_t side = 0; side < Sides; ++side) {
			const float angle = 2 * Pi * static_cast<float>(side) / static_cast<float>(Sides);
			const float irregular = 0.92F + static_cast<float>(PropHash(seed + static_cast<uint32_t>(side)) & 255U) * (0.16F / 255);
			const float x = std::cos(angle) * radiusX * RingRadius[ring] * irregular;
			const float z = std::sin(angle) * radiusZ * RingRadius[ring] * irregular;
			rings[ring][side] = PhysicalPropVertex(centerX + x * std::cos(yaw) - z * std::sin(yaw),
			    height * RingHeight[ring], centerZ + x * std::sin(yaw) + z * std::cos(yaw));
		}
	}
	const auto base = PhysicalPropVertex(centerX, 0, centerZ);
	const auto top = PhysicalPropVertex(centerX + radiusX * 0.08F, height, centerZ - radiusZ * 0.06F);
	for (size_t side = 0; side < Sides; ++side) {
		const size_t next = (side + 1) % Sides;
		PhysicalPropTriangle(mesh, base, rings[0][next], rings[0][side], color, { 0, -1, 0 });
		for (size_t ring = 0; ring + 1 < rings.size(); ++ring) {
			const Vec outward { (rings[ring][side].x + rings[ring][next].x) / 2 - centerX, 0,
			    (rings[ring][side].z + rings[ring][next].z) / 2 - centerZ };
			Quad(mesh, { rings[ring][side], rings[ring][next], rings[ring + 1][next], rings[ring + 1][side] },
			    TownVolumeMaterial::SpriteSide, color, outward);
		}
		PhysicalPropTriangle(mesh, rings[2][side], rings[2][next], top, color,
		    { rings[2][side].x - centerX, height, rings[2][side].z - centerZ });
	}
}

struct ProfilePoint { float x, height; };

float ProfileCross(ProfilePoint a, ProfilePoint b, ProfilePoint c)
{
	return (b.x - a.x) * (c.height - a.height) - (b.height - a.height) * (c.x - a.x);
}

bool ExtrudeGraveProfile(TownVolumeMesh &mesh, const std::vector<ProfilePoint> &profile,
    float centerX, float centerZ, float thickness, uint8_t color)
{
	// Ear clipping preserves cross-shaped concavities; a center fan would fill
	// the missing corners around the arms. All caps and side edges share points.
	std::vector<size_t> remaining(profile.size());
	for (size_t i = 0; i < remaining.size(); ++i) remaining[i] = i;
	std::vector<std::array<size_t, 3>> caps;
	while (remaining.size() > 3) {
		bool clipped = false;
		for (size_t i = 0; i < remaining.size(); ++i) {
			const size_t a = remaining[(i + remaining.size() - 1) % remaining.size()];
			const size_t b = remaining[i], c = remaining[(i + 1) % remaining.size()];
			if (ProfileCross(profile[a], profile[b], profile[c]) <= 0.0000001F) continue;
			bool occupied = false;
			for (size_t p : remaining) {
				if (p == a || p == b || p == c) continue;
				occupied |= ProfileCross(profile[a], profile[b], profile[p]) >= -0.0000001F
				    && ProfileCross(profile[b], profile[c], profile[p]) >= -0.0000001F
				    && ProfileCross(profile[c], profile[a], profile[p]) >= -0.0000001F;
			}
			if (occupied) continue;
			caps.push_back({ a, b, c });
			remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
			clipped = true;
			break;
		}
		if (!clipped) return false;
	}
	caps.push_back({ remaining[0], remaining[1], remaining[2] });
	const auto vertex = [&](size_t i, float z) {
		return PhysicalPropVertex(centerX + profile[i].x, profile[i].height, centerZ + z);
	};
	for (const auto &cap : caps) {
		PhysicalPropTriangle(mesh, vertex(cap[0], thickness), vertex(cap[1], thickness), vertex(cap[2], thickness), color, { 0, 0, 1 });
		PhysicalPropTriangle(mesh, vertex(cap[0], -thickness), vertex(cap[1], -thickness), vertex(cap[2], -thickness), color, { 0, 0, -1 });
	}
	for (size_t i = 0; i < profile.size(); ++i) {
		const size_t next = (i + 1) % profile.size();
		Quad(mesh, { vertex(i, -thickness), vertex(next, -thickness), vertex(next, thickness), vertex(i, thickness) },
		    TownVolumeMaterial::SpriteSide, color,
		    { profile[next].height - profile[i].height, profile[i].x - profile[next].x, 0 });
	}
	return true;
}

bool GraveMarker(TownVolumeMesh &mesh, float centerX, float centerZ, float width,
    float height, bool cross, bool round, uint8_t color)
{
	const float w = width / 2;
	std::vector<ProfilePoint> profile;
	if (cross) {
		const float stem = w * 0.30F, low = height * 0.59F, high = height * 0.79F;
		profile = { { -stem, 0 }, { stem, 0 }, { stem, low }, { w, low }, { w, high }, { stem, high },
			{ stem, height }, { -stem, height }, { -stem, high }, { -w, high }, { -w, low }, { -stem, low } };
	} else {
		profile = { { -w, 0 }, { w, 0 }, { w, round ? height - w : height } };
		if (round) {
			for (int i = 1; i <= 8; ++i) {
				const float angle = Pi * static_cast<float>(i) / 8;
				profile.push_back({ w * std::cos(angle), height - w + w * std::sin(angle) });
			}
		} else {
			profile.push_back({ -w, height });
		}
	}
	return ExtrudeGraveProfile(mesh, profile, centerX, centerZ, 0.065F, color);
}

void NativePhysicalBox(TownVolumeMesh &mesh, float minX, float minZ, float maxX,
    float maxZ, float height, uint8_t color)
{
	// These offsets follow native tile X/Z; DrawVolume's physical basis is fixed
	// image-right/native-forward, so convert once before the renderer rotates it.
	constexpr float S = 0.7071067811865475F;
	const auto v = [&](float x, float y, float z) { return PhysicalPropVertex((x - z) * S, y, (x + z) * S); };
	const auto normal = [&](float x, float y, float z) { return Vec { (x - z) * S, y, (x + z) * S }; };
	Quad(mesh, { v(minX, 0, minZ), v(maxX, 0, minZ), v(maxX, height, minZ), v(minX, height, minZ) }, TownVolumeMaterial::SpriteSide, color, normal(0, 0, -1));
	Quad(mesh, { v(maxX, 0, maxZ), v(minX, 0, maxZ), v(minX, height, maxZ), v(maxX, height, maxZ) }, TownVolumeMaterial::SpriteSide, color, normal(0, 0, 1));
	Quad(mesh, { v(minX, 0, maxZ), v(minX, 0, minZ), v(minX, height, minZ), v(minX, height, maxZ) }, TownVolumeMaterial::SpriteSide, color, normal(-1, 0, 0));
	Quad(mesh, { v(maxX, 0, minZ), v(maxX, 0, maxZ), v(maxX, height, maxZ), v(maxX, height, minZ) }, TownVolumeMaterial::SpriteSide, color, normal(1, 0, 0));
	Quad(mesh, { v(minX, height, minZ), v(maxX, height, minZ), v(maxX, height, maxZ), v(minX, height, maxZ) }, TownVolumeMaterial::SpriteSide, color, { 0, 1, 0 });
	Quad(mesh, { v(minX, 0, maxZ), v(maxX, 0, maxZ), v(maxX, 0, minZ), v(minX, 0, minZ) }, TownVolumeMaterial::SpriteSide, color, { 0, -1, 0 });
}

struct WallRectangle { float minX, minZ, maxX, maxZ; };

void NativeWallUnion(TownVolumeMesh &mesh, const std::vector<WallRectangle> &rectangles,
    float height, uint8_t color)
{
	// A rectilinear union makes X/Z runs and L corners one closed wall body.
	// Internal faces are omitted, keeping the coping continuous and flat.
	std::vector<float> xs, zs;
	for (const auto &rect : rectangles) {
		xs.insert(xs.end(), { rect.minX, rect.maxX });
		zs.insert(zs.end(), { rect.minZ, rect.maxZ });
	}
	if (xs.empty()) return;
	for (auto *axis : { &xs, &zs }) {
		std::sort(axis->begin(), axis->end());
		axis->erase(std::unique(axis->begin(), axis->end()), axis->end());
	}
	const size_t width = xs.size() - 1, depth = zs.size() - 1;
	std::vector<bool> occupied(width * depth);
	for (size_t z = 0; z < depth; ++z) for (size_t x = 0; x < width; ++x) {
		const float cx = (xs[x] + xs[x + 1]) / 2, cz = (zs[z] + zs[z + 1]) / 2;
		occupied[z * width + x] = std::any_of(rectangles.begin(), rectangles.end(), [&](const auto &rect) {
			return cx > rect.minX && cx < rect.maxX && cz > rect.minZ && cz < rect.maxZ;
		});
	}
	const auto has = [&](int x, int z) {
		return x >= 0 && z >= 0 && static_cast<size_t>(x) < width && static_cast<size_t>(z) < depth
		    && occupied[static_cast<size_t>(z) * width + static_cast<size_t>(x)];
	};
	constexpr float S = 0.7071067811865475F;
	const auto v = [&](float x, float y, float z) { return PhysicalPropVertex((x - z) * S, y, (x + z) * S); };
	const auto n = [&](float x, float y, float z) { return Vec { (x - z) * S, y, (x + z) * S }; };
	for (int z = 0; z < static_cast<int>(depth); ++z) for (int x = 0; x < static_cast<int>(width); ++x) {
		if (!has(x, z)) continue;
		const float left = xs[static_cast<size_t>(x)], right = xs[static_cast<size_t>(x) + 1];
		const float back = zs[static_cast<size_t>(z)], front = zs[static_cast<size_t>(z) + 1];
		Quad(mesh, { v(left, height, back), v(right, height, back), v(right, height, front), v(left, height, front) }, TownVolumeMaterial::SpriteSide, color, { 0, 1, 0 });
		Quad(mesh, { v(left, 0, front), v(right, 0, front), v(right, 0, back), v(left, 0, back) }, TownVolumeMaterial::SpriteSide, color, { 0, -1, 0 });
		if (!has(x - 1, z)) Quad(mesh, { v(left, 0, front), v(left, 0, back), v(left, height, back), v(left, height, front) }, TownVolumeMaterial::SpriteSide, color, n(-1, 0, 0));
		if (!has(x + 1, z)) Quad(mesh, { v(right, 0, back), v(right, 0, front), v(right, height, front), v(right, height, back) }, TownVolumeMaterial::SpriteSide, color, n(1, 0, 0));
		if (!has(x, z - 1)) Quad(mesh, { v(left, 0, back), v(right, 0, back), v(right, height, back), v(left, height, back) }, TownVolumeMaterial::SpriteSide, color, n(0, 0, -1));
		if (!has(x, z + 1)) Quad(mesh, { v(right, 0, front), v(left, 0, front), v(left, height, front), v(right, height, front) }, TownVolumeMaterial::SpriteSide, color, n(0, 0, 1));
	}
}

} // namespace

TownVolumeMesh BuildTownPhysicalPropVolume(const TownVolumeSprite &sprite,
    town_prop_stencils::Kind kind, uint16_t nativeLayout, uint32_t placementSeed)
{
	TownVolumeMesh mesh;
	Image image { sprite };
	if (!image.FindBounds()) return mesh;
	const auto layouts = town_prop_stencils::GetLayouts();
	if (std::none_of(layouts.begin(), layouts.end(), [&](const auto &layout) {
		return layout.kind == kind && layout.nativeLayout == nativeLayout;
	})) return mesh;
	const uint8_t color = image.Color((image.left + image.right) / 2,
	    image.top + (image.bottom - image.top) / 3, 2);
	if (kind == town_prop_stencils::Kind::Rock) {
		const float scale = 0.90F + static_cast<float>(PropHash(placementSeed) & 255U) * (0.20F / 255);
		float rx = 0.13F, rz = 0.10F, height = 0.13F;
		switch (nativeLayout) {
		case 219:
			SmallGroundedRock(mesh, -0.17F * scale, 0, 0.12F * scale, 0.14F * scale, 0.17F * scale, placementSeed, color);
			SmallGroundedRock(mesh, 0.17F * scale, 0.03F * scale, 0.11F * scale, 0.13F * scale, 0.14F * scale, placementSeed + 11, color);
			break;
		case 221: break;
		case 222: rx = 0.16F; rz = 0.12F; height = 0.17F; break;
		case 223: rx = 0.23F; rz = 0.16F; height = 0.21F; break;
		case 226: rx = 0.29F; rz = 0.20F; height = 0.24F; break;
		case 230: rx = 0.34F; rz = 0.24F; height = 0.28F; break;
		default: return {};
		}
		if (nativeLayout != 219) SmallGroundedRock(mesh, 0, 0, rx * scale, rz * scale, height * scale, placementSeed, color);
	} else if (kind == town_prop_stencils::Kind::Grave) {
		// Existing native-til identifiers describe complete arrangements, never a
		// new resource called "grave". Crosses, upright steles and flat slabs are
		// separate semantic kit components; none uses the rock builder.
		bool ok = true;
		const auto marker = [&](float x, float z, float w, float h, bool cross, bool round) {
			ok &= GraveMarker(mesh, x, z, w, h, cross, round, color);
		};
		switch (nativeLayout) {
		case 250: case 257: case 259: case 270: marker(0, 0, 0.44F, 0.86F, true, false); break;
		case 251: case 269: case 271: marker(0, 0, 0.40F, 0.88F, false, true); break;
		case 256: marker(0, 0, 0.28F, 0.50F, false, true); break;
		case 260: case 262: marker(0, 0, 0.42F, 0.86F, false, false); break;
		case 252: case 258:
			marker(-0.28F, -0.06F, 0.26F, 0.58F, false, false);
			marker(0.28F, 0.06F, 0.24F, 0.43F, false, true); break;
		case 253:
			marker(-0.28F, -0.06F, 0.36F, 0.86F, false, true);
			marker(0.28F, 0.06F, 0.27F, 0.52F, true, false); break;
		case 254: case 263: case 267:
			marker(-0.28F, -0.06F, 0.36F, 0.88F, false, nativeLayout != 254);
			marker(0.28F, 0.06F, 0.24F, 0.45F, false, false); break;
		case 255: case 261:
			// Red native grave-ground paint is not evidence of a stone slab.
			// It remains in SceneGround; only the visible marker gains volume.
			marker(0.34F, -0.12F, 0.28F, 0.52F, true, false); break;
		case 264: case 268:
			marker(0, 0, 0.42F, 0.80F, false, false); break;
		case 266:
			marker(-0.28F, -0.06F, 0.25F, 0.55F, true, false);
			marker(0.28F, 0.06F, 0.24F, 0.45F, false, false); break;
		default: return {};
		}
		if (!ok) return {};
	} else if (kind == town_prop_stencils::Kind::Wall) {
		const auto found = std::find_if(layouts.begin(), layouts.end(), [&](const auto &layout) {
			return layout.kind == kind && layout.nativeLayout == nativeLayout;
		});
		std::vector<WallRectangle> walls;
		std::vector<const town_prop_stencils::Cell *> upright;
		for (const auto &cell : found->cells) if (cell.replacesScenery && cell.upright) upright.push_back(&cell);
		constexpr float HalfThickness = 0.14F;
		for (size_t i = 0; i < upright.size(); ++i) {
			const auto &a = *upright[i];
			const float ax = static_cast<float>(a.x - found->footX) - 0.5F;
			const float az = static_cast<float>(a.y - found->footY) - 0.5F;
			bool joined = false;
			for (size_t j = 0; j < upright.size(); ++j) {
				if (j == i) continue;
				const auto &b = *upright[j];
				if (std::abs(int(a.x) - int(b.x)) + std::abs(int(a.y) - int(b.y)) != 1) continue;
				joined = true;
				if (j < i) continue;
				const float bx = static_cast<float>(b.x - found->footX) - 0.5F;
				const float bz = static_cast<float>(b.y - found->footY) - 0.5F;
				if (a.y == b.y) walls.push_back({ std::min(ax, bx) - 0.5F, az - HalfThickness, std::max(ax, bx) + 0.5F, az + HalfThickness });
				else walls.push_back({ ax - HalfThickness, std::min(az, bz) - 0.5F, ax + HalfThickness, std::max(az, bz) + 0.5F });
			}
			if (!joined) walls.push_back({ ax - HalfThickness, az - HalfThickness, ax + HalfThickness, az + HalfThickness });
		}
		NativeWallUnion(mesh, walls, 0.72F, color);
		// Non-solid source wall paint is a broken/low component. It must not be
		// promoted into a tall segment that visually closes a native passage.
		for (const auto &cell : found->cells) if (cell.replacesScenery && !cell.upright) {
			const float x = static_cast<float>(cell.x - found->footX) - 0.5F;
			const float z = static_cast<float>(cell.y - found->footY) - 0.5F;
			NativePhysicalBox(mesh, x - 0.18F, z - 0.18F, x + 0.18F, z + 0.18F, 0.08F, color);
		}
	} else return {};
	Measure(mesh);
	return mesh;
}

TownVolumeMesh BuildCathedralLightStandVolume(const TownVolumeSprite &sprite)
{
	TownVolumeMesh mesh;
	Image image { sprite };
	if (sprite.width != 64 || sprite.height > 256 || !image.FindBounds())
		return mesh;
	struct Ring {
		float height, radius;
		uint8_t color;
	};
	std::vector<Ring> profile;
	// The bottom MIN row is the floor diamond. Its center, sixteen pixels
	// above the image bottom, is the stand's physical ground contact.
	// but do not revolve it into a broad conical foot. Extend the slender pole
	// onto the flat 3D floor with a small plate reconstructed from the bowl width.
	// Four native rows per band bound the geometry while preserving the bowl
	// and pole. A circle recovers depth hidden in the isometric art;
	// the relief used for rocks would otherwise turn these into flattened slabs.
	const int bodyBottom = std::min(image.bottom, sprite.height - 33);
	const int groundAnchor = sprite.height - 16;
	for (int y = bodyBottom; y >= image.top; y -= 4) {
		int left = sprite.width, right = -1;
		for (int row = std::max(image.top, y - 3); row <= y; ++row) {
			for (int x = image.left; x <= image.right; ++x) {
				if (!image.Opaque(x, row)) continue;
				left = std::min(left, x); right = std::max(right, x);
			}
		}
		if (right < left) continue;
		const float height = static_cast<float>(groundAnchor - 1 - y) / VerticalPixels;
		const float radius = std::max(0.015F, (right - left + 1) / (2 * HorizontalPixels));
		profile.push_back({ height, radius, image.Color((left + right) / 2, std::max(image.top, y - 1), 1) });
	}
	if (profile.size() < 2) return mesh;
	const auto widest = std::max_element(profile.begin(), profile.end(), [](const Ring &a, const Ring &b) { return a.radius < b.radius; });
	const float footRadius = std::clamp(widest->radius * 0.8F, 0.06F, 0.25F);
	const uint8_t footColor = image.Color(sprite.width / 2, sprite.height - 16, 2);
	const float poleRadius = profile.front().radius;
	profile.insert(profile.begin(), { { 0, footRadius, footColor }, { 0.07F, footRadius, footColor },
		{ 0.12F, poleRadius, profile.front().color } });
	profile.back().height = static_cast<float>(groundAnchor - image.top) / VerticalPixels;
	constexpr int Sides = 16;
	const auto vertex = [&](const Ring &ring, int side) {
		const float angle = 2 * Pi * side / Sides;
		return image.Vertex({ ring.radius * std::cos(angle), ring.height, ring.radius * std::sin(angle) });
	};
	mesh.triangles.reserve(profile.size() * Sides * 2);
	for (size_t band = 1; band < profile.size(); ++band) {
		for (int side = 0; side < Sides; ++side) {
			const int next = (side + 1) % Sides;
			const float angle = 2 * Pi * (side + 0.5F) / Sides;
			Quad(mesh, { vertex(profile[band - 1], side), vertex(profile[band - 1], next),
				vertex(profile[band], next), vertex(profile[band], side) },
				TownVolumeMaterial::SpriteSide, profile[band].color, { std::cos(angle), 0, std::sin(angle) });
		}
	}
	for (int side = 0; side < Sides; ++side) {
		const int next = (side + 1) % Sides;
		Triangle(mesh, image.Vertex({ 0, 0, 0 }), vertex(profile.front(), side), vertex(profile.front(), next),
			TownVolumeMaterial::SpriteSide, profile.front().color, { 0, -1, 0 });
		Triangle(mesh, image.Vertex({ 0, profile.back().height, 0 }), vertex(profile.back(), side), vertex(profile.back(), next),
			TownVolumeMaterial::SpriteSide, profile.back().color, { 0, 1, 0 });
	}
	Measure(mesh);
	return mesh;
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
