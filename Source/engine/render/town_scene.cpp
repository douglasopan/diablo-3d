#include "engine/render/town_scene.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <utility>

#include "engine/render/town_model_import.hpp"
#include "levels/dun_tile_data.hpp"

namespace devilution {
namespace {

constexpr float Pi = 3.14159265358979323846F;

struct Position {
	float x;
	float height;
	float z;
};

std::vector<TownSceneModel> Scene;

struct Builder {
	TownSceneModel model;
	Point sourceTile;
	Point pickTile;
	std::vector<size_t> authoredUv;
	TownSceneSurfaceDetail surfaceDetail = TownSceneSurfaceDetail::None;

	void Triangle(Position a, Position b, Position c, TownSceneMaterial material,
		TownSceneSurfaceRole role = TownSceneSurfaceRole::Exterior)
	{
		const float normalX = (b.height - a.height) * (c.z - a.z) - (b.z - a.z) * (c.height - a.height);
		const float normalY = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
		const float normalZ = (b.x - a.x) * (c.height - a.height) - (b.height - a.height) * (c.x - a.x);
		if (normalX * normalX + normalY * normalY + normalZ * normalZ < 0.00000001F)
			return;
		model.triangles.push_back({ { TownSceneVertex { a.x, a.height, a.z, 0, 0 },
			TownSceneVertex { b.x, b.height, b.z, 0, 0 },
			TownSceneVertex { c.x, c.height, c.z, 0, 0 } }, material, sourceTile, pickTile, false, {}, role, surfaceDetail });
	}

	void Quad(Position a, Position b, Position c, Position d, TownSceneMaterial material,
		TownSceneSurfaceRole role = TownSceneSurfaceRole::Exterior)
	{
		Triangle(a, b, c, material, role);
		Triangle(a, c, d, material, role);
	}

	void WrappedQuad(Position a, Position b, Position c, Position d, TownSceneMaterial material,
		float firstU, float secondU, float topV, float bottomV, TownSceneSurfaceRole role = TownSceneSurfaceRole::Exterior)
	{
		const size_t first = model.triangles.size();
		Quad(a, b, c, d, material, role);
		const std::array<std::array<float, 2>, 4> uv { std::array<float, 2> { firstU, topV }, { secondU, topV }, { secondU, bottomV }, { firstU, bottomV } };
		for (size_t i = first; i < model.triangles.size(); ++i) {
			const std::array<size_t, 3> corners = i == first ? std::array<size_t, 3> { 0, 1, 2 } : std::array<size_t, 3> { 0, 2, 3 };
			for (size_t vertex = 0; vertex < 3; ++vertex) {
				model.triangles[i].vertices[vertex].u = uv[corners[vertex]][0];
				model.triangles[i].vertices[vertex].v = uv[corners[vertex]][1];
			}
			authoredUv.push_back(i);
		}
	}

	void Box(float minX, float minZ, float maxX, float maxZ, float base, float top, TownSceneMaterial material)
	{
		Quad({ minX, top, minZ }, { maxX, top, minZ }, { maxX, base, minZ }, { minX, base, minZ }, material);
		Quad({ maxX, top, minZ }, { maxX, top, maxZ }, { maxX, base, maxZ }, { maxX, base, minZ }, material);
		Quad({ maxX, top, maxZ }, { minX, top, maxZ }, { minX, base, maxZ }, { maxX, base, maxZ }, material);
		Quad({ minX, top, maxZ }, { minX, top, minZ }, { minX, base, minZ }, { minX, base, maxZ }, material);
		Quad({ minX, top, minZ }, { minX, top, maxZ }, { maxX, top, maxZ }, { maxX, top, minZ }, material);
		Quad({ minX, base, minZ }, { maxX, base, minZ }, { maxX, base, maxZ }, { minX, base, maxZ }, material, TownSceneSurfaceRole::Underside);
	}

	/** Thick wall with a distinct original exterior and a complete inside surface. */
	void WallSlab(float minX, float minZ, float maxX, float maxZ, float base, float top,
		TownSceneMaterial material, size_t exteriorFace)
	{
		const size_t first = model.triangles.size();
		Box(minX, minZ, maxX, maxZ, base, top, material);
		for (size_t i = 0; i < 10; ++i)
			model.triangles[first + i].surfaceRole = i / 2 == exteriorFace
			    ? TownSceneSurfaceRole::Exterior : TownSceneSurfaceRole::Interior;
	}

	/** A roof sheet is a closed prism; its underside never borrows facade paint. */
	void RoofQuad(Position a, Position b, Position c, Position d, float thickness = 0.16F)
	{
		const Position lowerA { a.x, a.height - thickness, a.z };
		const Position lowerB { b.x, b.height - thickness, b.z };
		const Position lowerC { c.x, c.height - thickness, c.z };
		const Position lowerD { d.x, d.height - thickness, d.z };
		Quad(a, b, c, d, TownSceneMaterial::Roof);
		Quad(lowerA, lowerD, lowerC, lowerB, TownSceneMaterial::Timber, TownSceneSurfaceRole::Underside);
		Quad(b, a, lowerA, lowerB, TownSceneMaterial::Timber);
		Quad(c, b, lowerB, lowerC, TownSceneMaterial::Timber);
		Quad(d, c, lowerC, lowerD, TownSceneMaterial::Timber);
		Quad(a, d, lowerD, lowerA, TownSceneMaterial::Timber);
	}

	void RoofTriangle(Position a, Position b, Position c, float thickness = 0.16F)
	{
		const Position lowerA { a.x, a.height - thickness, a.z };
		const Position lowerB { b.x, b.height - thickness, b.z };
		const Position lowerC { c.x, c.height - thickness, c.z };
		Triangle(a, b, c, TownSceneMaterial::Roof);
		Triangle(lowerA, lowerC, lowerB, TownSceneMaterial::Timber, TownSceneSurfaceRole::Underside);
		Quad(b, a, lowerA, lowerB, TownSceneMaterial::Timber);
		Quad(c, b, lowerB, lowerC, TownSceneMaterial::Timber);
		Quad(a, c, lowerC, lowerA, TownSceneMaterial::Timber);
	}

	void WallTriangle(Position a, Position b, Position c, Position inset, TownSceneMaterial material)
	{
		const Position innerA { a.x + inset.x, a.height + inset.height, a.z + inset.z };
		const Position innerB { b.x + inset.x, b.height + inset.height, b.z + inset.z };
		const Position innerC { c.x + inset.x, c.height + inset.height, c.z + inset.z };
		Triangle(a, b, c, material);
		Triangle(innerA, innerC, innerB, material, TownSceneSurfaceRole::Interior);
		Quad(b, a, innerA, innerB, material, TownSceneSurfaceRole::Interior);
		Quad(c, b, innerB, innerC, material, TownSceneSurfaceRole::Interior);
		Quad(a, c, innerC, innerA, material, TownSceneSurfaceRole::Interior);
	}

	void WallQuad(Position a, Position b, Position c, Position d, Position inset, TownSceneMaterial material)
	{
		const Position innerA { a.x + inset.x, a.height + inset.height, a.z + inset.z };
		const Position innerB { b.x + inset.x, b.height + inset.height, b.z + inset.z };
		const Position innerC { c.x + inset.x, c.height + inset.height, c.z + inset.z };
		const Position innerD { d.x + inset.x, d.height + inset.height, d.z + inset.z };
		Quad(a, b, c, d, material);
		Quad(innerA, innerD, innerC, innerB, material, TownSceneSurfaceRole::Interior);
		Quad(b, a, innerA, innerB, material, TownSceneSurfaceRole::Interior);
		Quad(c, b, innerB, innerC, material, TownSceneSurfaceRole::Interior);
		Quad(d, c, innerC, innerD, material, TownSceneSurfaceRole::Interior);
		Quad(a, d, innerD, innerA, material, TownSceneSurfaceRole::Interior);
	}

	void Ring(float centerX, float centerHeight, float centerZ, float outerRadius, float innerRadius, float thickness)
	{
		constexpr int Sides = 20;
		for (int i = 0; i < Sides; ++i) {
			const float first = 2 * Pi * static_cast<float>(i) / Sides;
			const float second = 2 * Pi * static_cast<float>(i + 1) / Sides;
			const auto point = [&](float radius, float angle, float z) {
				return Position { centerX + radius * std::cos(angle), centerHeight + radius * std::sin(angle), z };
			};
			const Position a = point(outerRadius, first, centerZ + thickness / 2);
			const Position b = point(outerRadius, second, centerZ + thickness / 2);
			const Position innerA = point(innerRadius, first, a.z);
			const Position innerB = point(innerRadius, second, b.z);
			const Position backA = point(outerRadius, first, centerZ - thickness / 2);
			const Position backB = point(outerRadius, second, backA.z);
			const Position backInnerA = point(innerRadius, first, backA.z);
			const Position backInnerB = point(innerRadius, second, backA.z);
			Quad(a, b, innerB, innerA, TownSceneMaterial::Stone);
			Quad(backB, backA, backInnerA, backInnerB, TownSceneMaterial::Stone);
			WrappedQuad(b, a, backA, backB, TownSceneMaterial::Stone,
				second * outerRadius, first * outerRadius, 0, thickness);
			WrappedQuad(innerA, innerB, backInnerB, backInnerA, TownSceneMaterial::Stone,
				first * innerRadius, second * innerRadius, 0, thickness, TownSceneSurfaceRole::Interior);
		}
	}

	void Post(float x, float z, float top, float width = 0.16F)
	{
		Box(x - width / 2, z - width / 2, x + width / 2, z + width / 2, 0, top, TownSceneMaterial::Timber);
	}

	/** A complete roof with two sloped surfaces, gable infill and an overhanging fascia. */
	void Gable(float minX, float minZ, float maxX, float maxZ, float eave, float peak,
		bool ridgeAlongZ, TownSceneMaterial gableMaterial = TownSceneMaterial::Wall, float overhang = 0.30F, float backOverhang = -1)
	{
		const float left = minX - overhang;
		const float right = maxX + overhang;
		const float back = minZ - (backOverhang >= 0 ? backOverhang : overhang);
		const float front = maxZ + overhang;
		constexpr float Thickness = 0.16F;
		if (ridgeAlongZ) {
			const float middle = (minX + maxX) / 2;
			RoofQuad({ left, eave, back }, { left, eave, front }, { middle, peak, front }, { middle, peak, back });
			RoofQuad({ middle, peak, back }, { middle, peak, front }, { right, eave, front }, { right, eave, back });
			const float sideHeight = eave - Thickness + (peak - eave) * overhang / (middle - left);
			if (sideHeight > eave) {
				WallSlab(minX, minZ, maxX, minZ + Thickness, eave, sideHeight, gableMaterial, 0);
				WallSlab(minX, maxZ - Thickness, maxX, maxZ, eave, sideHeight, gableMaterial, 2);
			}
			const float base = std::max(eave, sideHeight);
			WallTriangle({ maxX, base, minZ }, { minX, base, minZ }, { middle, peak - Thickness, minZ }, { 0, 0, Thickness }, gableMaterial);
			WallTriangle({ minX, base, maxZ }, { maxX, base, maxZ }, { middle, peak - Thickness, maxZ }, { 0, 0, -Thickness }, gableMaterial);
		} else {
			const float middle = (minZ + maxZ) / 2;
			RoofQuad({ left, eave, back }, { left, peak, middle }, { right, peak, middle }, { right, eave, back });
			RoofQuad({ left, peak, middle }, { left, eave, front }, { right, eave, front }, { right, peak, middle });
			const float sideHeight = eave - Thickness + (peak - eave) * overhang / (middle - back);
			if (sideHeight > eave) {
				WallSlab(minX, minZ, minX + Thickness, maxZ, eave, sideHeight, gableMaterial, 3);
				WallSlab(maxX - Thickness, minZ, maxX, maxZ, eave, sideHeight, gableMaterial, 1);
			}
			const float base = std::max(eave, sideHeight);
			WallTriangle({ minX, base, minZ }, { minX, base, maxZ }, { minX, peak - Thickness, middle }, { Thickness, 0, 0 }, gableMaterial);
			WallTriangle({ maxX, base, maxZ }, { maxX, base, minZ }, { maxX, peak - Thickness, middle }, { -Thickness, 0, 0 }, gableMaterial);
		}
	}

	/** Four roof slopes around a short ridge, as on Pepin's original thatched roof. */
	void Hip(float minX, float minZ, float maxX, float maxZ, float eave, float peak,
		float backInset, float frontInset, float overhang = 0.30F)
	{
		const float left = minX - overhang;
		const float right = maxX + overhang;
		const float back = minZ - overhang;
		const float front = maxZ + overhang;
		const float middle = (minX + maxX) / 2;
		const float ridgeBack = std::min(back + backInset, (back + front) / 2);
		const float ridgeFront = std::max(front - frontInset, ridgeBack);
		const Position a { middle, peak, ridgeBack };
		const Position b { middle, peak, ridgeFront };
		RoofQuad({ left, eave, back }, { left, eave, front }, b, a);
		RoofQuad(a, b, { right, eave, front }, { right, eave, back });
		RoofTriangle({ right, eave, back }, { left, eave, back }, a);
		RoofTriangle({ left, eave, front }, { right, eave, front }, b);
		// The wall ring meets the inset underside rather than leaving a floating
		// roof above the residential wall cap. It stays inside the original body.
		const float supportHeight = eave + (peak - eave) * overhang / std::min({ backInset, frontInset, (right - left) / 2 }) - 0.16F;
		if (supportHeight > eave) {
			WallSlab(minX, minZ, maxX, minZ + 0.16F, eave, supportHeight, TownSceneMaterial::Wall, 0);
			WallSlab(maxX - 0.16F, minZ, maxX, maxZ, eave, supportHeight, TownSceneMaterial::Wall, 1);
			WallSlab(minX, maxZ - 0.16F, maxX, maxZ, eave, supportHeight, TownSceneMaterial::Wall, 2);
			WallSlab(minX, minZ, minX + 0.16F, maxZ, eave, supportHeight, TownSceneMaterial::Wall, 3);
		}
	}

	/** Residential doors stay closed; actual open spaces opt out explicitly. */
	void HouseWalls(float minX, float minZ, float maxX, float maxZ, float height,
		float doorX, float doorWidth = 1.2F, TownSceneMaterial material = TownSceneMaterial::Wall,
		float doorHeight = 1.85F, bool closedDoor = true)
	{
		model.physicalBounds = { minX, minZ, maxX, maxZ, height, closedDoor };
		constexpr float Thickness = 0.16F;
		WallSlab(minX, minZ, maxX, minZ + Thickness, 0, height, material, 0);
		WallSlab(maxX - Thickness, minZ, maxX, maxZ, 0, height, material, 1);
		WallSlab(minX, minZ, minX + Thickness, maxZ, 0, height, material, 3);
		const float doorLeft = std::clamp(doorX - doorWidth / 2, minX + 0.3F, maxX - 0.6F);
		const float doorRight = std::clamp(doorX + doorWidth / 2, doorLeft + 0.3F, maxX - 0.3F);
		const float doorTop = std::min(height - 0.35F, doorHeight);
		// Ordinary Tristram houses are scenery, not enterable buildings. A painted
		// door must not create a new opening in the reconstructed wall.
		if (closedDoor) {
			WallSlab(minX, maxZ - Thickness, maxX, maxZ, 0, height, material, 2);
		} else {
			WallSlab(minX, maxZ - Thickness, doorLeft, maxZ, 0, height, material, 2);
			WallSlab(doorRight, maxZ - Thickness, maxX, maxZ, 0, height, material, 2);
			WallSlab(doorLeft, maxZ - Thickness, doorRight, maxZ, doorTop, height, material, 2);
		}
		Quad({ minX, height, minZ }, { minX, height, maxZ }, { maxX, height, maxZ }, { maxX, height, minZ }, material, TownSceneSurfaceRole::Interior);
		Quad({ minX, 0, minZ }, { maxX, 0, minZ }, { maxX, 0, maxZ }, { minX, 0, maxZ }, material, TownSceneSurfaceRole::Underside);
		Box(doorLeft - 0.10F, maxZ - 0.06F, doorLeft + 0.10F, maxZ + 0.08F, 0, doorTop, TownSceneMaterial::Timber);
		Box(doorRight - 0.10F, maxZ - 0.06F, doorRight + 0.10F, maxZ + 0.08F, 0, doorTop, TownSceneMaterial::Timber);
		Box(doorLeft - 0.15F, maxZ - 0.08F, doorRight + 0.15F, maxZ + 0.12F, doorTop, doorTop + 0.18F, TownSceneMaterial::Timber);
		Post(minX, minZ, height);
		Post(maxX, minZ, height);
		Post(minX, maxZ, height);
		Post(maxX, maxZ, height);
		Box(minX - 0.06F, minZ - 0.06F, maxX + 0.06F, maxZ + 0.06F, 0, 0.22F, TownSceneMaterial::Stone);
	}

	void HouseWallsEast(float minX, float minZ, float maxX, float maxZ, float height, float doorZ)
	{
		const size_t first = model.triangles.size();
		HouseWalls(minZ, minX, maxZ, maxX, height, doorZ);
		for (size_t i = first; i < model.triangles.size(); ++i) {
			TownSceneTriangle &triangle = model.triangles[i];
			for (TownSceneVertex &vertex : triangle.vertices)
				std::swap(vertex.x, vertex.z);
			std::swap(triangle.vertices[1], triangle.vertices[2]);
		}
		model.physicalBounds = { minX, minZ, maxX, maxZ, height, true };
	}
};

Builder MakeBuilder(TownSceneKind kind, Point minTile, Point maxTile, Point sourceTile, Point pickTile)
{
	return { { kind, minTile, maxTile, {} }, sourceTile, pickTile };
}

void FinishBuilder(Builder &builder)
{
	std::sort(builder.authoredUv.begin(), builder.authoredUv.end());
	builder.authoredUv.erase(std::unique(builder.authoredUv.begin(), builder.authoredUv.end()), builder.authoredUv.end());
	TownSceneNativeArtwork &artwork = builder.model.nativeArtwork;
	if (!artwork.enabled) {
		artwork.referenceTile = builder.model.minTile;
		artwork.minTile = builder.model.minTile;
		artwork.maxTile = builder.model.maxTile;
		if (builder.model.kind == TownSceneKind::Cathedral) {
			artwork.minTile = { 14, 12 };
			artwork.maxTile = { 33, 31 };
			artwork.fringeMinPiece = 700;
			artwork.fringeMaxPiece = 834;
		} else if (builder.model.kind == TownSceneKind::Smithy) {
			artwork.minTile = { 60, 54 };
			artwork.maxTile = { 71, 65 };
			artwork.fringeMinPiece = 900;
			artwork.fringeMaxPiece = 959;
		} else if (builder.model.kind == TownSceneKind::Tavern) {
			artwork.minTile = { 46, 54 };
			artwork.maxTile = { 57, 65 };
			artwork.fringeMinPiece = 397;
			artwork.fringeMaxPiece = 452;
		} else if (builder.model.kind == TownSceneKind::House) {
			const Point origin = builder.model.minTile;
			if (origin == Point { 36, 64 }) {
				artwork.minTile = { 36, 64 };
				artwork.maxTile = { 43, 69 };
			} else if (origin == Point { 46, 40 }) {
				artwork.minTile = { 42, 40 };
				artwork.maxTile = { 55, 47 };
			} else if (origin == Point { 68, 78 }) {
				artwork.minTile = { 64, 78 };
				artwork.maxTile = { 73, 83 };
			} else if (origin == Point { 48, 74 }) {
				artwork.minTile = { 46, 74 };
				artwork.maxTile = { 59, 83 };
				artwork.fringeMinPiece = 488;
				artwork.fringeMaxPiece = 541;
			} else if (origin == Point { 74, 16 }) {
				artwork.minTile = { 72, 16 };
				artwork.maxTile = { 79, 23 };
				artwork.fringeMinPiece = 601;
				artwork.fringeMaxPiece = 642;
			}
			if (artwork.fringeMinPiece == 0) {
				artwork.fringeMinPiece = 453;
				artwork.fringeMaxPiece = 487;
			}
		}
		const int width = artwork.maxTile.x - artwork.minTile.x;
		const int depth = artwork.maxTile.y - artwork.minTile.y;
		artwork.pixelOrigin = {
			32 * (artwork.minTile.x - artwork.maxTile.y - artwork.referenceTile.x + artwork.referenceTile.y) - 32,
			16 * (artwork.minTile.x + artwork.minTile.y - artwork.referenceTile.x - artwork.referenceTile.y) - 256
		};
		artwork.pixelSize = { 32 * (width + depth) + 64, 16 * (width + depth) + 256 };
		artwork.enabled = true;
	}
	for (size_t triangleIndex = 0; triangleIndex < builder.model.triangles.size(); ++triangleIndex) {
		TownSceneTriangle &triangle = builder.model.triangles[triangleIndex];
		const TownSceneVertex &a = triangle.vertices[0];
		const TownSceneVertex &b = triangle.vertices[1];
		const TownSceneVertex &c = triangle.vertices[2];
		const float normalX = (b.height - a.height) * (c.z - a.z) - (b.z - a.z) * (c.height - a.height);
		const float normalY = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
		const float normalZ = (b.x - a.x) * (c.height - a.height) - (b.height - a.height) * (c.x - a.x);
		const float normalLength = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
		triangle.normal = { normalX / normalLength, normalY / normalLength, normalZ / normalLength };
		// Height scaling makes the original camera's direction proportional to
		// (1,1,1) in world coordinates. A positive component alone is insufficient:
		// a steep roof facing away can have +Y while still hiding its source face.
		triangle.nativeProjection = triangle.surfaceRole == TownSceneSurfaceRole::Exterior
		    && triangle.normal.x + triangle.normal.height + triangle.normal.z > 0.0001F;
		if (std::binary_search(builder.authoredUv.begin(), builder.authoredUv.end(), triangleIndex))
			continue;
		// A horizontal material axis and its in-plane perpendicular preserve
		// distances on slopes and keep one continuous scale across split quads.
		const float horizontalLength = std::hypot(triangle.normal.x, triangle.normal.z);
		float ux = 1, uz = 0, vx = 0, vy = 0, vz = 1;
		if (horizontalLength > 0.0001F) {
			ux = triangle.normal.z / horizontalLength;
			uz = -triangle.normal.x / horizontalLength;
			if (ux < -0.0001F || (std::abs(ux) <= 0.0001F && uz < 0)) {
				ux = -ux;
				uz = -uz;
			}
			vx = triangle.normal.height * uz;
			vy = triangle.normal.z * ux - triangle.normal.x * uz;
			vz = -triangle.normal.height * ux;
			if (vy < 0) {
				vx = -vx;
				vy = -vy;
				vz = -vz;
			}
		}
		for (TownSceneVertex &vertex : triangle.vertices) {
			const float x = vertex.x - static_cast<float>(artwork.referenceTile.x);
			const float z = vertex.z - static_cast<float>(artwork.referenceTile.y);
			vertex.u = x * ux + z * uz;
			vertex.v = horizontalLength > 0.0001F ? -(x * vx + vertex.height * vy + z * vz) : z;
		}
	}

	Scene.push_back(std::move(builder.model));
}

void AddHouse(TownSceneKind kind, Point minTile, Point maxTile, Point sourceTile, Point entrance,
	float wallHeight, float roofRise, bool ridgeAlongZ, float porchDepth = 0, bool doorEast = false,
	Point bodyMinTile = { -1, -1 }, Point bodyMaxTile = { -1, -1 })
{
	Builder builder = MakeBuilder(kind, minTile, maxTile, sourceTile, entrance);
	if (bodyMinTile.x < 0)
		bodyMinTile = minTile;
	if (bodyMaxTile.x < 0)
		bodyMaxTile = maxTile;
	// Leave room for the timber posts within the same solid cells. The much
	// larger painted tile envelope is kept separately in model.min/maxTile.
	const float minX = static_cast<float>(bodyMinTile.x) - 0.40F;
	const float maxX = static_cast<float>(bodyMaxTile.x) + 0.40F;
	const float minZ = static_cast<float>(bodyMinTile.y) - 0.40F;
	const float maxZ = static_cast<float>(bodyMaxTile.y) + 0.40F - porchDepth;
	const float doorX = std::clamp(static_cast<float>(entrance.x), minX + 1, maxX - 1);
	if (doorEast)
		builder.HouseWallsEast(minX, minZ, maxX, maxZ, wallHeight, static_cast<float>(entrance.y));
	else
		builder.HouseWalls(minX, minZ, maxX, maxZ, wallHeight, doorX);
	// The smith's long ridge extends over the working bay on its western side;
	// its roof envelope is independent of the enclosed room's solid rectangle.
	float roofMinX = kind == TownSceneKind::Smithy ? 59.55F : minX;
	float roofMaxX = kind == TownSceneKind::Smithy ? 71.45F : maxX;
	if (minTile == Point { 68, 78 }) {
		// Native ridge endpoints (2598,2511)..(2767,2596) lie at x66.94..72.22
		// with z79.5 and a 5.0-unit peak. The wall rectangle is deliberately
		// narrower; resizing it to the painted roof would cover walkable ground.
		roofMinX = 66.90F;
		roofMaxX = 72.30F;
	}
	if (minTile == Point { 48, 74 }) {
		// In the native atlas the short ridge runs (51,76)..(51,78.55).
		// Four hip faces meet that ridge; a lower full-length gable happened to
		// project near its tip but gave the wrong roof shape when the camera moved.
		builder.Hip(minX, minZ, maxX, maxZ, wallHeight, wallHeight + roofRise, 2.70F, 2.15F);
	} else {
		builder.Gable(roofMinX, minZ, roofMaxX, maxZ, wallHeight, wallHeight + roofRise, ridgeAlongZ);
	}
	if (kind == TownSceneKind::Smithy)
		builder.model.minTile.x = 60; // Scenery envelope includes the connected bay roof.
	if (porchDepth > 0) {
		const float frontZ = maxZ + porchDepth;
		const float outerEave = wallHeight - 0.45F;
		builder.RoofQuad({ minX - 0.25F, wallHeight + 0.12F, maxZ - 0.10F },
			{ minX - 0.25F, outerEave, frontZ + 0.25F },
			{ maxX + 0.25F, outerEave, frontZ + 0.25F },
			{ maxX + 0.25F, wallHeight + 0.12F, maxZ - 0.10F });
		builder.Post(minX + 0.15F, frontZ, outerEave);
		builder.Post(maxX - 0.15F, frontZ, outerEave);
		builder.Box(minX, frontZ - 0.12F, maxX, frontZ + 0.12F, outerEave - 0.22F, outerEave, TownSceneMaterial::Timber);
	}
	FinishBuilder(builder);
}

void AddCabinMaterialPatch(Builder &builder, TownSceneSurfaceDetail detail, TownSceneMaterial material,
	std::array<TownSceneVertex, 3> plane, std::array<float, 2> uvMin, std::array<float, 2> uvMax, bool repeat = true)
{
	TownSceneMaterialPatch patch { detail, material, plane, uvMin, uvMax,
		{ uvMax[0] - uvMin[0], uvMax[1] - uvMin[1] }, repeat, {}, {} };
	const auto &a = plane[0];
	const auto &b = plane[1];
	const auto &c = plane[2];
	const float determinant = (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
	float minX = 100000, minY = 100000, maxX = -100000, maxY = -100000;
	for (float u : { uvMin[0], uvMax[0] }) {
		for (float v : { uvMin[1], uvMax[1] }) {
			const float wb = ((u - a.u) * (c.v - a.v) - (v - a.v) * (c.u - a.u)) / determinant;
			const float wc = ((b.u - a.u) * (v - a.v) - (b.v - a.v) * (u - a.u)) / determinant;
			const float x = a.x + wb * (b.x - a.x) + wc * (c.x - a.x);
			const float z = a.z + wb * (b.z - a.z) + wc * (c.z - a.z);
			const float height = a.height + wb * (b.height - a.height) + wc * (c.height - a.height);
			const Point reference = builder.model.nativeArtwork.referenceTile;
			const float pixelX = 32 * (x - z - reference.x + reference.y);
			const float pixelY = 16 * (x + z - reference.x - reference.y) - 32 * height;
			minX = std::min(minX, pixelX);
			maxX = std::max(maxX, pixelX);
			minY = std::min(minY, pixelY);
			maxY = std::max(maxY, pixelY);
		}
	}
	patch.sourceMin = { static_cast<int>(std::floor(minX)), static_cast<int>(std::floor(minY)) };
	patch.sourceMax = { static_cast<int>(std::ceil(maxX)), static_cast<int>(std::ceil(maxY)) };
	builder.model.materialPatches.push_back(patch);
}

struct CabinClipPlane {
	float x;
	float height;
	float z;
	float offset;

	float Distance(const TownSceneVertex &vertex) const
	{
		return x * vertex.x + height * vertex.height + z * vertex.z + offset;
	}
};

using CabinPolygon = std::vector<TownSceneVertex>;

CabinPolygon ClipCabinPolygon(const CabinPolygon &input, CabinClipPlane plane, bool inside)
{
	CabinPolygon output;
	if (input.empty())
		return output;
	TownSceneVertex previous = input.back();
	float previousDistance = plane.Distance(previous);
	bool previousInside = inside ? previousDistance <= 0 : previousDistance >= 0;
	for (const TownSceneVertex &current : input) {
		const float distance = plane.Distance(current);
		const bool currentInside = inside ? distance <= 0 : distance >= 0;
		if (currentInside != previousInside) {
			const float t = std::clamp(previousDistance / (previousDistance - distance), 0.0F, 1.0F);
			output.push_back({ previous.x + t * (current.x - previous.x),
				previous.height + t * (current.height - previous.height),
				previous.z + t * (current.z - previous.z),
				previous.u + t * (current.u - previous.u), previous.v + t * (current.v - previous.v) });
		}
		if (currentInside)
			output.push_back(current);
		previous = current;
		previousDistance = distance;
		previousInside = currentInside;
	}
	return output;
}

bool SetCabinTriangleNormal(TownSceneTriangle &triangle)
{
	const auto &a = triangle.vertices[0];
	const auto &b = triangle.vertices[1];
	const auto &c = triangle.vertices[2];
	const float x = (b.height - a.height) * (c.z - a.z) - (b.z - a.z) * (c.height - a.height);
	const float height = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
	const float z = (b.x - a.x) * (c.height - a.height) - (b.height - a.height) * (c.x - a.x);
	const float squaredLength = x * x + height * height + z * z;
	if (squaredLength < 0.0000000001F)
		return false;
	const float length = std::sqrt(squaredLength);
	triangle.normal = { x / length, height / length, z / length };
	return true;
}

void AppendCabinPolygon(std::vector<TownSceneTriangle> &output, const TownSceneTriangle &source, const CabinPolygon &polygon)
{
	for (size_t i = 1; i + 1 < polygon.size(); ++i) {
		TownSceneTriangle triangle = source;
		triangle.vertices = { polygon[0], polygon[i], polygon[i + 1] };
		if (SetCabinTriangleNormal(triangle))
			output.push_back(triangle);
	}
}

std::array<CabinClipPlane, 22> CabinWindowPlanes(const TownCabinInterior &interior)
{
	std::array<CabinClipPlane, 22> planes;
	// Inscribed polygon: its vertices are on the measured circle, so the cut
	// never extends beyond the specified radius into the approved stone rim.
	const float sideDistance = interior.windowRadius * std::cos(Pi / 20);
	for (size_t i = 0; i < 20; ++i) {
		const float angle = 2 * Pi * (static_cast<float>(i) + 0.5F) / 20;
		const float x = std::cos(angle);
		const float height = std::sin(angle);
		planes[i] = { x, height, 0, -x * interior.windowCenter.x - height * interior.windowCenter.height - sideDistance };
	}
	planes[20] = { 0, 0, -1, interior.windowInnerZ - 0.08F };
	planes[21] = { 0, 0, 1, -interior.windowOuterZ - 0.15F };
	return planes;
}

std::vector<TownSceneTriangle> CutCabinWindow(const std::vector<TownSceneTriangle> &input,
	const TownCabinInterior &interior, uint32_t *clippedCount = nullptr)
{
	std::vector<TownSceneTriangle> output;
	output.reserve(input.size());
	const auto planes = CabinWindowPlanes(interior);
	for (const TownSceneTriangle &triangle : input) {
		CabinPolygon intersection(triangle.vertices.begin(), triangle.vertices.end());
		for (CabinClipPlane plane : planes)
			intersection = ClipCabinPolygon(intersection, plane, true);
		bool nonzeroIntersection = false;
		for (size_t i = 1; i + 1 < intersection.size(); ++i) {
			TownSceneTriangle test = triangle;
			test.vertices = { intersection[0], intersection[i], intersection[i + 1] };
			nonzeroIntersection = nonzeroIntersection || SetCabinTriangleNormal(test);
		}
		if (!nonzeroIntersection) {
			// Exact copy, including vertex ordering, UVs and all source metadata.
			output.push_back(triangle);
			continue;
		}
		if (clippedCount != nullptr)
			++*clippedCount;
		CabinPolygon remaining(triangle.vertices.begin(), triangle.vertices.end());
		for (CabinClipPlane plane : planes) {
			AppendCabinPolygon(output, triangle, ClipCabinPolygon(remaining, plane, false));
			remaining = ClipCabinPolygon(remaining, plane, true);
		}
	}
	return output;
}

void PrepareCabinInteriorTriangles(std::vector<TownSceneTriangle> &triangles, TownLightVector origin)
{
	for (TownSceneTriangle &triangle : triangles) {
		SetCabinTriangleNormal(triangle);
		triangle.surfaceRole = TownSceneSurfaceRole::Interior;
		triangle.nativeProjection = false;
		triangle.surfaceDetail = TownSceneSurfaceDetail::None;
		for (TownSceneVertex &vertex : triangle.vertices) {
			if (std::abs(triangle.normal.height) > 0.99F) {
				// Plank grain follows Z; widths and seams retain a real world scale.
				vertex.u = vertex.z - origin.z;
				vertex.v = vertex.x - origin.x;
			} else if (std::abs(triangle.normal.x) > std::abs(triangle.normal.z)) {
				vertex.u = vertex.z - origin.z;
				vertex.v = vertex.height;
			} else {
				vertex.u = vertex.x - origin.x;
				vertex.v = vertex.height;
			}
		}
	}
}

std::shared_ptr<const TownCabinInterior> MakeCabinInterior(const TownSceneModel &model)
{
	auto interior = std::make_shared<TownCabinInterior>();
	interior->windowCenter = { 71.460F, 2.215F, 71.608F };
	interior->windowRadius = 0.35F;
	interior->windowOuterZ = 71.608F;
	interior->windowInnerZ = 71.38F;
	interior->roomMinimum = { model.physicalBounds.minX + 0.23F, 0.10F, model.physicalBounds.minZ + 0.23F };
	interior->roomMaximum = { model.physicalBounds.maxX - 0.23F, 4.62F, interior->windowInnerZ };
	interior->apertures.push_back({ TownLightPlane::Z, interior->roomMaximum.z,
		interior->windowCenter.x - interior->windowRadius, interior->windowCenter.x + interior->windowRadius,
		interior->windowCenter.height - interior->windowRadius, interior->windowCenter.height + interior->windowRadius, 20 });
	// Offset toward an upper pane along the native ray, rather than hiding the
	// physical bulb behind the central wooden muntins in the initial view.
	interior->light = { { interior->windowCenter.x - 0.61F, interior->windowCenter.height - 0.61F, interior->windowOuterZ - 0.72F },
		{ 1.0F, 0.70F, 0.08F }, 4.0F, 6.0F };
	interior->exteriorTriangles = CutCabinWindow(model.triangles, *interior, &interior->clippedSourceTriangles);
	const Point source = model.triangles.front().sourceTile;
	const Point pick = model.triangles.front().pickTile;
	Builder room = MakeBuilder(model.kind, model.minTile, model.maxTile, source, pick);
	const float left = interior->roomMinimum.x;
	const float right = interior->roomMaximum.x;
	const float back = interior->roomMinimum.z;
	const float front = interior->roomMaximum.z;
	const float floor = interior->roomMinimum.height;
	const float middle = (left + right) / 2;
	const float eave = 1.52F;
	const float peak = interior->roomMaximum.height;
	// The complete rear shell seals the generated second window from inside.
	room.Quad({ left, floor, back }, { right, floor, back }, { right, eave, back }, { left, eave, back }, TownSceneMaterial::Stone);
	room.Triangle({ left, eave, back }, { right, eave, back }, { middle, peak, back }, TownSceneMaterial::Stone);
	room.Quad({ left, floor, front }, { left, eave, front }, { right, eave, front }, { right, floor, front }, TownSceneMaterial::Stone);
	room.Triangle({ left, eave, front }, { middle, peak, front }, { right, eave, front }, TownSceneMaterial::Stone);
	room.Quad({ left, floor, back }, { left, eave, back }, { left, eave, front }, { left, floor, front }, TownSceneMaterial::Stone);
	room.Quad({ right, floor, front }, { right, eave, front }, { right, eave, back }, { right, floor, back }, TownSceneMaterial::Stone);
	room.Quad({ left, eave, back }, { middle, peak, back }, { middle, peak, front }, { left, eave, front }, TownSceneMaterial::Timber);
	room.Quad({ middle, peak, back }, { right, eave, back }, { right, eave, front }, { middle, peak, front }, TownSceneMaterial::Timber);
	// Real shallow board solids over a closed subfloor. Tiny recessed seams
	// show board boundaries without exposing native terrain through the room.
	room.Box(left, back, right, front, floor - 0.07F, floor - 0.04F, TownSceneMaterial::Timber);
	constexpr float BoardWidth = 0.28F;
	constexpr float Seam = 0.009F;
	for (float x = left; x < right; x += BoardWidth) {
		const float end = std::min(right, x + BoardWidth);
		room.Box(x + Seam / 2, back, end - Seam / 2, front, floor - 0.04F, floor, TownSceneMaterial::Timber);
	}
	PrepareCabinInteriorTriangles(room.model.triangles, interior->roomMinimum);
	interior->interiorTriangles = CutCabinWindow(room.model.triangles, *interior);
	// Stone tunnel connects the actual outer aperture to the room shell.
	Builder inserts = MakeBuilder(model.kind, model.minTile, model.maxTile, source, pick);
	for (int i = 0; i < 20; ++i) {
		const float first = 2 * Pi * static_cast<float>(i) / 20;
		const float second = 2 * Pi * static_cast<float>(i + 1) / 20;
		const float aX = interior->windowCenter.x + interior->windowRadius * std::cos(first);
		const float aH = interior->windowCenter.height + interior->windowRadius * std::sin(first);
		const float bX = interior->windowCenter.x + interior->windowRadius * std::cos(second);
		const float bH = interior->windowCenter.height + interior->windowRadius * std::sin(second);
		inserts.Quad({ aX, aH, interior->windowOuterZ }, { bX, bH, interior->windowOuterZ },
			{ bX, bH, front }, { aX, aH, front }, TownSceneMaterial::Stone);
	}
	// Four clear panes separated by actual wood, with no opaque glow disk.
	const float centerX = interior->windowCenter.x;
	const float centerH = interior->windowCenter.height;
	const float frameZ = interior->windowOuterZ - 0.08F;
	inserts.Box(centerX - 0.016F, frameZ - 0.02F, centerX + 0.016F, frameZ + 0.02F,
		centerH - 0.30F, centerH + 0.30F, TownSceneMaterial::Timber);
	inserts.Box(centerX - 0.30F, frameZ - 0.02F, centerX + 0.30F, frameZ + 0.02F,
		centerH - 0.016F, centerH + 0.016F, TownSceneMaterial::Timber);
	PrepareCabinInteriorTriangles(inserts.model.triangles, interior->roomMinimum);
	interior->interiorTriangles.insert(interior->interiorTriangles.end(), inserts.model.triangles.begin(), inserts.model.triangles.end());
	// Closed, small luminous bulb: emission belongs to its own geometry, while
	// the point light illuminates real floor/wall receivers behind the aperture.
	Builder bulb = MakeBuilder(model.kind, model.minTile, model.maxTile, source, pick);
	constexpr float BulbRadius = 0.08F;
	const auto bulbPoint = [&](int latitude, int longitude) {
		const float vertical = Pi * static_cast<float>(latitude) / 8;
		const float horizontal = 2 * Pi * static_cast<float>(longitude) / 12;
		return Position { interior->light.position.x + BulbRadius * std::sin(vertical) * std::cos(horizontal),
			interior->light.position.height + BulbRadius * std::cos(vertical),
			interior->light.position.z + BulbRadius * std::sin(vertical) * std::sin(horizontal) };
	};
	for (int latitude = 0; latitude < 8; ++latitude)
		for (int longitude = 0; longitude < 12; ++longitude)
			bulb.Quad(bulbPoint(latitude, longitude), bulbPoint(latitude, longitude + 1),
				bulbPoint(latitude + 1, longitude + 1), bulbPoint(latitude + 1, longitude), TownSceneMaterial::Timber);
	PrepareCabinInteriorTriangles(bulb.model.triangles, interior->roomMinimum);
	interior->emissiveTriangles = std::move(bulb.model.triangles);
	return interior;
}

void AddCabin(Point minTile, Point maxTile)
{
	Builder builder = MakeBuilder(TownSceneKind::Cabin, minTile, maxTile,
		{ minTile.x + 2, maxTile.y }, { minTile.x + 4, minTile.y + 1 });
	// These dimensions come from the original wall base/eave/ridge pixel lines.
	// SOL includes painted roof fringes; it is deliberately not the wall rectangle.
	const float left = static_cast<float>(minTile.x) - 0.45F;
	const float right = static_cast<float>(minTile.x) + 3.20F;
	const float back = static_cast<float>(minTile.y) + 0.15F;
	const float front = static_cast<float>(maxTile.y) - 0.40F;
	constexpr float Eave = 1.35F;
	// The native ridge contour is y + .5*x = 150.5 east / 118 west.
	// 4.65 missed that contour by seven pixels; the measured ridge is 4.87.
	constexpr float Peak = 4.87F;
	constexpr float Overhang = 0.30F;
	constexpr float BackOverhang = 0.46F;
	builder.model.physicalBounds = { left, back, right, front, Eave, true };
	builder.surfaceDetail = TownSceneSurfaceDetail::Masonry;
	builder.Box(left, back, right, front, 0, Eave, TownSceneMaterial::Stone);
	// The closed ceiling is an internal body cap, not another visible facade.
	builder.model.triangles[8].surfaceRole = TownSceneSurfaceRole::Interior;
	builder.model.triangles[9].surfaceRole = TownSceneSurfaceRole::Interior;
	builder.Gable(left, back, right, front, Eave, Peak, true, TownSceneMaterial::Stone, Overhang, BackOverhang);
	for (auto &triangle : builder.model.triangles) {
		if (triangle.material == TownSceneMaterial::Roof)
			triangle.surfaceDetail = TownSceneSurfaceDetail::Thatch;
		else if (triangle.material == TownSceneMaterial::Timber)
			triangle.surfaceDetail = TownSceneSurfaceDetail::TimberTrim;
	}

	// A rolled thatch ridge has real thickness and closed ends. Its highest
	// point stays below the measured ridge, preserving the original silhouette.
	builder.surfaceDetail = TownSceneSurfaceDetail::Thatch;
	const float ridgeX = (left + right) / 2;
	const float roofBack = back - BackOverhang;
	const float roofFront = front + Overhang;
	constexpr float RidgeRadius = 0.07F;
	for (int i = 0; i < 12; ++i) {
		const float first = 2 * Pi * static_cast<float>(i) / 12;
		const float second = 2 * Pi * static_cast<float>(i + 1) / 12;
		const Position a { ridgeX + RidgeRadius * std::cos(first), Peak - 0.075F + RidgeRadius * std::sin(first), roofFront };
		const Position b { ridgeX + RidgeRadius * std::cos(second), Peak - 0.075F + RidgeRadius * std::sin(second), roofFront };
		builder.Quad(b, a, { a.x, a.height, roofBack }, { b.x, b.height, roofBack }, TownSceneMaterial::Roof);
		builder.Triangle({ ridgeX, Peak - 0.075F, roofFront }, a, b, TownSceneMaterial::Roof);
		builder.Triangle({ ridgeX, Peak - 0.075F, roofBack }, { b.x, b.height, roofBack }, { a.x, a.height, roofBack }, TownSceneMaterial::Roof);
	}

	// Closed arched blue door on the long +X wall, not on the front gable.
	const float doorZ = back + 1.25F;
	const float doorX = right + 0.008F;
	constexpr float DoorHalfWidth = 0.55F;
	constexpr float DoorArchBase = 1.20F;
	builder.surfaceDetail = TownSceneSurfaceDetail::TimberTrim;
	const size_t doorFirst = builder.model.triangles.size();
	builder.Box(doorX - 0.025F, doorZ - DoorHalfWidth, doorX + 0.025F, doorZ + DoorHalfWidth,
		0.04F, DoorArchBase, TownSceneMaterial::Timber);
	builder.model.triangles[doorFirst + 2].surfaceDetail = TownSceneSurfaceDetail::Door;
	builder.model.triangles[doorFirst + 3].surfaceDetail = TownSceneSurfaceDetail::Door;
	for (int i = 0; i < 12; ++i) {
		const float first = Pi * static_cast<float>(i) / 12;
		const float second = Pi * static_cast<float>(i + 1) / 12;
		const size_t firstTriangle = builder.model.triangles.size();
		builder.WallTriangle({ doorX + 0.025F, DoorArchBase, doorZ },
			{ doorX + 0.025F, DoorArchBase + DoorHalfWidth * std::sin(second), doorZ + DoorHalfWidth * std::cos(second) },
			{ doorX + 0.025F, DoorArchBase + DoorHalfWidth * std::sin(first), doorZ + DoorHalfWidth * std::cos(first) },
			{ -0.05F, 0, 0 }, TownSceneMaterial::Timber);
		builder.model.triangles[firstTriangle].surfaceDetail = TownSceneSurfaceDetail::Door;
	}
	// Stone voussoirs surround the closed blue panel instead of stretching its
	// paint over a generic timber facade. They do not create an entrance.
	builder.surfaceDetail = TownSceneSurfaceDetail::Masonry;
	for (int i = 0; i < 12; ++i) {
		const float first = Pi * static_cast<float>(i) / 12;
		const float second = Pi * static_cast<float>(i + 1) / 12;
		const auto arch = [&](float radius, float angle) {
			return Position { doorX + 0.045F, DoorArchBase + radius * std::sin(angle), doorZ + radius * std::cos(angle) };
		};
		builder.WallQuad(arch(DoorHalfWidth + 0.075F, first), arch(DoorHalfWidth, first),
			arch(DoorHalfWidth, second), arch(DoorHalfWidth + 0.075F, second), { -0.075F, 0, 0 }, TownSceneMaterial::Stone);
	}
	builder.Box(right - 0.025F, doorZ - DoorHalfWidth - 0.075F, right + 0.055F, doorZ - DoorHalfWidth,
		0, DoorArchBase, TownSceneMaterial::Stone);
	builder.Box(right - 0.025F, doorZ + DoorHalfWidth, right + 0.055F, doorZ + DoorHalfWidth + 0.075F,
		0, DoorArchBase, TownSceneMaterial::Stone);
	builder.surfaceDetail = TownSceneSurfaceDetail::Foundation;
	builder.Box(right, doorZ - DoorHalfWidth, right + 0.60F, doorZ + DoorHalfWidth,
		0, 0.08F, TownSceneMaterial::Stone);

	// The round yellow window is a closed disk in the +Z gable. Its colors and
	// muntins are supplied by the native projection, not a new invented opening.
	// Amber-paint centroid measured at (92.74,189.80) east / (92.74,157.80)
	// west. This placement gives (92.74,189.81)/(92.74,157.81) in those crops.
	const float windowX = (left + right) / 2 - 0.127F;
	constexpr float WindowHeight = 2.055F;
	constexpr float WindowRadius = 0.35F;
	builder.surfaceDetail = TownSceneSurfaceDetail::WindowGlass;
	for (int i = 0; i < 20; ++i) {
		const float first = 2 * Pi * static_cast<float>(i) / 20;
		const float second = 2 * Pi * static_cast<float>(i + 1) / 20;
		builder.WallTriangle({ windowX, WindowHeight, front + 0.008F },
			{ windowX + WindowRadius * std::cos(first), WindowHeight + WindowRadius * std::sin(first), front + 0.008F },
			{ windowX + WindowRadius * std::cos(second), WindowHeight + WindowRadius * std::sin(second), front + 0.008F },
			{ 0, 0, -0.05F }, TownSceneMaterial::Timber);
	}
	builder.surfaceDetail = TownSceneSurfaceDetail::Masonry;
	builder.Ring(windowX, WindowHeight, front + 0.025F, WindowRadius + 0.06F, WindowRadius - 0.01F, 0.06F);
	builder.surfaceDetail = TownSceneSurfaceDetail::TimberTrim;
	builder.Box(windowX - 0.02F, front + 0.018F, windowX + 0.02F, front + 0.065F,
		WindowHeight - WindowRadius + 0.02F, WindowHeight + WindowRadius - 0.02F, TownSceneMaterial::Timber);
	builder.Box(windowX - WindowRadius + 0.02F, front + 0.018F, windowX + WindowRadius - 0.02F, front + 0.065F,
		WindowHeight - 0.02F, WindowHeight + 0.02F, TownSceneMaterial::Timber);

	// Native barrel occupies the extra solid cells beside the door, never a new
	// walkable cell. Keep it round and hollow at the top rather than a square post.
	const float barrelX = right + 0.50F;
	const float barrelZ = doorZ + 1.25F;
	constexpr float BarrelRadius = 0.30F;
	constexpr float BarrelInnerRadius = 0.24F;
	constexpr float BarrelTop = 1.35F;
	builder.surfaceDetail = TownSceneSurfaceDetail::BarrelStaves;
	const size_t barrelFirst = builder.model.triangles.size();
	for (int i = 0; i < 20; ++i) {
		const float first = 2 * Pi * static_cast<float>(i) / 20;
		const float second = 2 * Pi * static_cast<float>(i + 1) / 20;
		const Position a { barrelX + BarrelRadius * std::cos(first), BarrelTop, barrelZ + BarrelRadius * std::sin(first) };
		const Position b { barrelX + BarrelRadius * std::cos(second), BarrelTop, barrelZ + BarrelRadius * std::sin(second) };
		const Position innerA { barrelX + BarrelInnerRadius * std::cos(first), BarrelTop, barrelZ + BarrelInnerRadius * std::sin(first) };
		const Position innerB { barrelX + BarrelInnerRadius * std::cos(second), BarrelTop, barrelZ + BarrelInnerRadius * std::sin(second) };
		builder.WrappedQuad(a, b, { b.x, 0, b.z }, { a.x, 0, a.z }, TownSceneMaterial::Timber,
			first * BarrelRadius, second * BarrelRadius, -BarrelTop, 0);
		builder.Quad(b, a, innerA, innerB, TownSceneMaterial::Timber);
		builder.WrappedQuad(innerB, innerA, { innerA.x, BarrelTop - 0.25F, innerA.z },
			{ innerB.x, BarrelTop - 0.25F, innerB.z }, TownSceneMaterial::Timber,
			second * BarrelInnerRadius, first * BarrelInnerRadius, -BarrelTop, -(BarrelTop - 0.25F), TownSceneSurfaceRole::Interior);
		builder.Triangle({ barrelX, BarrelTop - 0.25F, barrelZ },
			{ innerB.x, BarrelTop - 0.25F, innerB.z }, { innerA.x, BarrelTop - 0.25F, innerA.z }, TownSceneMaterial::Timber, TownSceneSurfaceRole::Interior);
		builder.Triangle({ barrelX, 0, barrelZ }, { a.x, 0, a.z }, { b.x, 0, b.z }, TownSceneMaterial::Timber, TownSceneSurfaceRole::Underside);
	}
	// Three closed hoops give the original container a readable volume from
	// behind; their finish is separate from door paint and vertical stave grain.
	builder.surfaceDetail = TownSceneSurfaceDetail::BarrelHoops;
	const size_t hoopFirst = builder.model.triangles.size();
	for (float height : { 0.18F, 0.70F, 1.23F }) {
		for (int i = 0; i < 20; ++i) {
			const float first = 2 * Pi * static_cast<float>(i) / 20;
			const float second = 2 * Pi * static_cast<float>(i + 1) / 20;
			const auto point = [&](float radius, float angle, float y) {
				return Position { barrelX + radius * std::cos(angle), y, barrelZ + radius * std::sin(angle) };
			};
			const Position a = point(BarrelRadius + 0.015F, first, height + 0.025F);
			const Position b = point(BarrelRadius + 0.015F, second, height + 0.025F);
			const Position c = point(BarrelRadius - 0.005F, first, height + 0.025F);
			const Position d = point(BarrelRadius - 0.005F, second, height + 0.025F);
			const Position lowerA = point(BarrelRadius + 0.015F, first, height - 0.025F);
			const Position lowerB = point(BarrelRadius + 0.015F, second, height - 0.025F);
			const Position lowerC = point(BarrelRadius - 0.005F, first, height - 0.025F);
			const Position lowerD = point(BarrelRadius - 0.005F, second, height - 0.025F);
			builder.WrappedQuad(a, b, lowerB, lowerA, TownSceneMaterial::Stone,
				first * (BarrelRadius + 0.015F), second * (BarrelRadius + 0.015F), -height - 0.025F, -height + 0.025F);
			builder.Quad(d, c, lowerC, lowerD, TownSceneMaterial::Stone, TownSceneSurfaceRole::Interior);
			builder.Quad(b, a, c, d, TownSceneMaterial::Stone);
			builder.Quad(lowerA, lowerB, lowerD, lowerC, TownSceneMaterial::Stone, TownSceneSurfaceRole::Underside);
		}
	}
	TownSceneNativeArtwork &artwork = builder.model.nativeArtwork;
	artwork.referenceTile = minTile;
	artwork.pixelOrigin = { minTile.x == 70 ? -232 : -168, -146 };
	artwork.pixelSize = { minTile.x == 70 ? 368 : 304, minTile.x == 70 ? 299 : 267 };
	artwork.enabled = true;
	artwork.minTile = minTile;
	artwork.maxTile = { minTile.x + 5, maxTile.y + 2 };
	artwork.fringeMinPiece = 849;
	artwork.fringeMaxPiece = 874;
	const float halfRoof = (right - left) / 2 + Overhang;
	const float roofRise = Peak - Eave;
	const float roofSlope = std::hypot(halfRoof, roofRise);
	for (size_t index = 0; index < builder.model.triangles.size(); ++index) {
		TownSceneTriangle &triangle = builder.model.triangles[index];
		if (triangle.surfaceDetail == TownSceneSurfaceDetail::BarrelStaves
		    || triangle.surfaceDetail == TownSceneSurfaceDetail::BarrelHoops)
			continue;
		const bool xWall = std::abs(triangle.vertices[0].x - triangle.vertices[1].x) < 0.00001F
		    && std::abs(triangle.vertices[0].x - triangle.vertices[2].x) < 0.00001F;
		const bool horizontal = std::abs(triangle.vertices[0].height - triangle.vertices[1].height) < 0.00001F
		    && std::abs(triangle.vertices[0].height - triangle.vertices[2].height) < 0.00001F;
		for (auto &vertex : triangle.vertices) {
			if (triangle.surfaceDetail == TownSceneSurfaceDetail::Thatch) {
				vertex.u = vertex.z - roofBack;
				vertex.v = std::abs(vertex.x - ridgeX) * halfRoof / roofSlope + (Peak - vertex.height) * roofRise / roofSlope;
			} else if (triangle.surfaceDetail == TownSceneSurfaceDetail::Door) {
				vertex.u = vertex.z - doorZ + DoorHalfWidth;
				vertex.v = vertex.height;
			} else if (triangle.surfaceDetail == TownSceneSurfaceDetail::WindowGlass) {
				vertex.u = vertex.x - windowX + WindowRadius;
				vertex.v = WindowHeight + WindowRadius - vertex.height;
			} else {
				vertex.u = xWall ? vertex.z - back : vertex.x - left;
				vertex.v = horizontal ? vertex.z - back : vertex.height;
			}
		}
		builder.authoredUv.push_back(index);
	}
	const std::array<TownSceneVertex, 3> masonryPlane { TownSceneVertex { left, 0, front, 0, 0 },
		TownSceneVertex { right, 0, front, right - left, 0 }, TownSceneVertex { left, 1, front, 0, 1 } };
	AddCabinMaterialPatch(builder, TownSceneSurfaceDetail::Masonry, TownSceneMaterial::Stone,
		masonryPlane, { 0.30F, 0.20F }, { 3.35F, 1.00F });
	AddCabinMaterialPatch(builder, TownSceneSurfaceDetail::Foundation, TownSceneMaterial::Stone,
		masonryPlane, { 0.30F, 0.20F }, { 3.35F, 1.00F });
	AddCabinMaterialPatch(builder, TownSceneSurfaceDetail::Thatch, TownSceneMaterial::Roof,
		{ TownSceneVertex { ridgeX, Peak, roofBack, 0, 0 },
			TownSceneVertex { ridgeX, Peak, roofFront, roofFront - roofBack, 0 },
			TownSceneVertex { right + Overhang, Eave, roofBack, 0, roofSlope } },
		{ 2.20F, 1.20F }, { 3.40F, 2.80F });
	// Rectify the narrow front fascia along its sloped timber grain. Sampling
	// this real beam keeps blue door paint out of rear trim and roof undersides.
	AddCabinMaterialPatch(builder, TownSceneSurfaceDetail::TimberTrim, TownSceneMaterial::Timber,
		{ TownSceneVertex { ridgeX, Peak, roofFront, 0, 0 },
			TownSceneVertex { right + Overhang, Eave, roofFront, roofSlope, 0 },
			TownSceneVertex { ridgeX, Peak - 0.16F, roofFront, 0, 0.16F } },
		{ 0.90F, 0.03F }, { 1.70F, 0.12F });
	AddCabinMaterialPatch(builder, TownSceneSurfaceDetail::Door, TownSceneMaterial::Timber,
		{ TownSceneVertex { doorX + 0.025F, 0, doorZ - DoorHalfWidth, 0, 0 },
			TownSceneVertex { doorX + 0.025F, 0, doorZ + DoorHalfWidth, 2 * DoorHalfWidth, 0 },
			TownSceneVertex { doorX + 0.025F, DoorArchBase + DoorHalfWidth, doorZ - DoorHalfWidth, 0, DoorArchBase + DoorHalfWidth } },
		{ 0, 0.04F }, { 2 * DoorHalfWidth, DoorArchBase + DoorHalfWidth }, false);
	AddCabinMaterialPatch(builder, TownSceneSurfaceDetail::WindowGlass, TownSceneMaterial::Timber,
		{ TownSceneVertex { windowX - WindowRadius, WindowHeight + WindowRadius, front + 0.008F, 0, 0 },
			TownSceneVertex { windowX + WindowRadius, WindowHeight + WindowRadius, front + 0.008F, 2 * WindowRadius, 0 },
			TownSceneVertex { windowX - WindowRadius, WindowHeight - WindowRadius, front + 0.008F, 0, 2 * WindowRadius } },
		{ 0, 0 }, { 2 * WindowRadius, 2 * WindowRadius }, false);
	// The original container's native-facing 36..54-degree stave plane is a
	// real flat mesh facet, avoiding a cylindrical crop stretched onto a wall.
	const auto &stave = builder.model.triangles[barrelFirst + 2 * 8];
	AddCabinMaterialPatch(builder, TownSceneSurfaceDetail::BarrelStaves, TownSceneMaterial::Timber,
		stave.vertices, { 2.20F * Pi * BarrelRadius / 10, -1.15F }, { 2.80F * Pi * BarrelRadius / 10, -0.25F });
	const auto &hoop = builder.model.triangles[hoopFirst + (20 + 2) * 8];
	AddCabinMaterialPatch(builder, TownSceneSurfaceDetail::BarrelHoops, TownSceneMaterial::Stone,
		hoop.vertices, { 2.20F * Pi * (BarrelRadius + 0.015F) / 10, -0.715F },
		{ 2.80F * Pi * (BarrelRadius + 0.015F) / 10, -0.685F });
	FinishBuilder(builder);
	if (minTile.x == 70 && LoadTownModelOverride(Scene.back(), "d3d-models/cabin-east.d3d"))
		BuildTownCabinInterior(Scene.back());
}

void AddSmithy(Point minTile, Point maxTile, Point sourceTile, Point entrance)
{
	Builder builder = MakeBuilder(TownSceneKind::Smithy, minTile, maxTile, sourceTile, entrance);
	const float minX = static_cast<float>(minTile.x) - 0.45F;
	const float maxX = static_cast<float>(maxTile.x) + 0.45F;
	const float minZ = static_cast<float>(minTile.y) - 0.45F;
	const float frontZ = static_cast<float>(maxTile.y) + 0.45F;
	const float backWallZ = minZ + (frontZ - minZ) * 0.40F;
	const float eave = 2.50F;
	// The smith's working bay stays open, with the merchant on his native tile.
	builder.HouseWalls(minX, minZ, maxX, backWallZ, eave, static_cast<float>(entrance.x),
		1.2F, TownSceneMaterial::Wall, 1.85F, false);
	builder.Gable(minX, minZ, maxX, frontZ, eave, 5.0F, true);
	builder.Post(minX, frontZ, eave, 0.24F);
	builder.Post(maxX, frontZ, eave, 0.24F);
	builder.Box(minX, frontZ - 0.12F, maxX, frontZ + 0.12F, eave - 0.20F, eave, TownSceneMaterial::Timber);
	// Low furnace plinth and work bench are separate recognizable workshop pieces.
	builder.Box(maxX - 1.65F, backWallZ + 0.35F, maxX - 0.25F, backWallZ + 1.50F, 0, 1.0F, TownSceneMaterial::Stone);
	builder.Box(minX + 0.35F, backWallZ + 0.30F, minX + 1.85F, backWallZ + 0.95F, 0.75F, 1.0F, TownSceneMaterial::Timber);
	FinishBuilder(builder);
}

void AddCrypt()
{
	// The cemetery's small mausoleum is a separate native building, not a prop
	// from the cathedral. Its painted family is 1170..1197 around (48,18).
	Builder builder = MakeBuilder(TownSceneKind::Crypt, { 48, 18 }, { 50, 21 }, { 50, 20 }, { 49, 22 });
	constexpr float Left = 47.55F;
	constexpr float Right = 49.50F;
	constexpr float Back = 17.85F;
	constexpr float Front = 20.55F;
	constexpr float Eave = 2.50F;
	constexpr float Peak = 3.60F;
	builder.model.physicalBounds = { Left, Back, Right, Front, Eave, true };
	builder.Box(Left, Back, Right, Front, 0, Eave, TownSceneMaterial::Stone);
	builder.Gable(Left, Back, Right, Front, Eave, Peak, true, TownSceneMaterial::Stone, 0.27F);
	// The two circular crest ornaments lie on the gable planes. Native projected
	// artwork supplies their exact stone silhouette instead of a new decoration.
	const float middle = (Left + Right) / 2;
	for (float z : { Back - 0.20F, Front + 0.20F })
		builder.Ring(middle, 3.76F, z, 0.24F, 0.16F, 0.16F);
	TownSceneNativeArtwork &artwork = builder.model.nativeArtwork;
	artwork.referenceTile = { 48, 18 };
	artwork.minTile = { 46, 18 };
	artwork.maxTile = { 51, 23 };
	artwork.pixelOrigin = { -256, -256 };
	artwork.pixelSize = { 384, 416 };
	artwork.enabled = true;
	artwork.fringeMinPiece = 1170;
	artwork.fringeMaxPiece = 1197;
	FinishBuilder(builder);
}

void AddWell(Point minTile, Point maxTile)
{
	Builder builder = MakeBuilder(TownSceneKind::Well, minTile, maxTile, minTile, minTile);
	const float centerX = (static_cast<float>(minTile.x) + maxTile.x) / 2;
	const float centerZ = (static_cast<float>(minTile.y) + maxTile.y) / 2;
	constexpr int Sides = 16;
	constexpr float OuterRadius = 1.0F;
	constexpr float InnerRadius = 0.68F;
	constexpr float RimHeight = 1.20F;
	for (int i = 0; i < Sides; ++i) {
		const float firstAngle = 2 * Pi * static_cast<float>(i) / Sides;
		const float secondAngle = 2 * Pi * static_cast<float>(i + 1) / Sides;
		const float firstX = std::cos(firstAngle);
		const float firstZ = std::sin(firstAngle);
		const float secondX = std::cos(secondAngle);
		const float secondZ = std::sin(secondAngle);
		const Position outerA { centerX + OuterRadius * firstX, RimHeight, centerZ + OuterRadius * firstZ };
		const Position outerB { centerX + OuterRadius * secondX, RimHeight, centerZ + OuterRadius * secondZ };
		const Position innerA { centerX + InnerRadius * firstX, RimHeight, centerZ + InnerRadius * firstZ };
		const Position innerB { centerX + InnerRadius * secondX, RimHeight, centerZ + InnerRadius * secondZ };
		builder.WrappedQuad(outerA, outerB, { outerB.x, 0, outerB.z }, { outerA.x, 0, outerA.z }, TownSceneMaterial::Stone,
			firstAngle * OuterRadius, secondAngle * OuterRadius, -RimHeight, 0);
		builder.WrappedQuad(innerB, innerA, { innerA.x, 0.45F, innerA.z }, { innerB.x, 0.45F, innerB.z }, TownSceneMaterial::Stone,
			secondAngle * InnerRadius, firstAngle * InnerRadius, -RimHeight, -0.45F, TownSceneSurfaceRole::Interior);
		builder.Quad(outerA, innerA, innerB, outerB, TownSceneMaterial::Stone);
		builder.Triangle({ centerX, 0, centerZ }, { outerA.x, 0, outerA.z }, { outerB.x, 0, outerB.z },
			TownSceneMaterial::Stone, TownSceneSurfaceRole::Underside);
		builder.Triangle({ centerX, 0.45F, centerZ }, { innerB.x, 0.45F, innerB.z }, { innerA.x, 0.45F, innerA.z },
			TownSceneMaterial::Stone, TownSceneSurfaceRole::Interior);
		builder.Triangle({ centerX, 0.47F, centerZ }, { innerB.x, 0.47F, innerB.z },
			{ innerA.x, 0.47F, innerA.z }, TownSceneMaterial::Water);
		builder.Triangle({ centerX, 0.46F, centerZ }, { innerA.x, 0.46F, innerA.z },
			{ innerB.x, 0.46F, innerB.z }, TownSceneMaterial::Water, TownSceneSurfaceRole::Underside);
		builder.Quad({ innerA.x, 0.47F, innerA.z }, { innerB.x, 0.47F, innerB.z },
			{ innerB.x, 0.46F, innerB.z }, { innerA.x, 0.46F, innerA.z }, TownSceneMaterial::Water, TownSceneSurfaceRole::Interior);
	}
	builder.model.physicalBounds = { centerX - OuterRadius, centerZ - OuterRadius,
		centerX + OuterRadius, centerZ + OuterRadius, RimHeight };
	FinishBuilder(builder);
}

void AddWitchHut()
{
	Builder builder = MakeBuilder(TownSceneKind::House, { 74, 16 }, { 79, 21 }, { 78, 18 }, { 80, 20 });
	constexpr float Left = 73.55F;
	constexpr float Right = 79.45F;
	constexpr float Back = 15.55F;
	constexpr float Front = 21.45F;
	constexpr float High = 2.8F;
	constexpr float Low = 2.5F;
	// Adria's native hut has one shallow sloping roof and an open east facade.
	builder.WallQuad({ Left, High, Back }, { Right, Low, Back }, { Right, 0, Back }, { Left, 0, Back }, { 0, 0, 0.16F }, TownSceneMaterial::Timber);
	builder.WallQuad({ Right, Low, Front }, { Left, High, Front }, { Left, 0, Front }, { Right, 0, Front }, { 0, 0, -0.16F }, TownSceneMaterial::Timber);
	builder.WallQuad({ Left, High, Front }, { Left, High, Back }, { Left, 0, Back }, { Left, 0, Front }, { 0.16F, 0, 0 }, TownSceneMaterial::Timber);
	builder.RoofQuad({ Left - 0.20F, High, Back - 0.20F }, { Left - 0.20F, High, Front + 0.20F },
		{ Right + 0.25F, Low, Front + 0.20F }, { Right + 0.25F, Low, Back - 0.20F });
	builder.Post(Right, Back, Low, 0.20F);
	builder.Post(Right, Front, Low, 0.20F);
	builder.Box(Right - 0.10F, Back, Right + 0.10F, Front, Low - 0.20F, Low, TownSceneMaterial::Timber);
	builder.Box(Left - 0.10F, Back - 0.10F, Right + 0.10F, Front + 0.10F, 0, 0.18F, TownSceneMaterial::Stone);
	builder.model.physicalBounds = { Left, Back, Right, Front, High };
	FinishBuilder(builder);
}

void AddCathedral(Point minTile, Point maxTile, Point sourceTile, Point entrance)
{
	Builder builder = MakeBuilder(TownSceneKind::Cathedral, minTile, maxTile, sourceTile, entrance);
	const float minX = static_cast<float>(minTile.x) - 0.45F;
	const float maxX = static_cast<float>(maxTile.x) + 0.45F;
	const float minZ = static_cast<float>(minTile.y) - 0.45F;
	const float maxZ = static_cast<float>(maxTile.y) + 0.45F;
	constexpr float WallHeight = 3.8F;
	constexpr float RidgeHeight = 6.6F;
	builder.HouseWalls(minX, minZ, maxX, maxZ, WallHeight, static_cast<float>(entrance.x),
		2.0F, TownSceneMaterial::Stone, 3.0F, false);
	builder.Gable(minX, minZ, maxX, maxZ, WallHeight, RidgeHeight, true, TownSceneMaterial::Stone, 0.22F);
	// The upper nave is a narrow raised roof above the lower side chapels.
	// Its visible native ridge endpoints (3310,687) and (3135,775) in the
	// isometric atlas imply a peak near 8.15 and an axis parallel to world Z.
	builder.Box(22.45F, 14.55F, 26.05F, 21.15F, WallHeight, 6.16F, TownSceneMaterial::Stone);
	builder.Gable(22.45F, 14.55F, 26.05F, 21.15F, 6.16F, 8.15F, true, TownSceneMaterial::Stone, 0.15F);
	// Tall masonry buttresses carry the nave walls independently of its roof.
	for (float z = minZ + 1.0F; z < maxZ - 0.8F; z += 2.7F) {
		builder.Box(minX - 0.45F, z - 0.28F, minX + 0.18F, z + 0.28F, 0, 3.8F, TownSceneMaterial::Stone);
		builder.Box(maxX - 0.18F, z - 0.28F, maxX + 0.45F, z + 0.28F, 0, 3.8F, TownSceneMaterial::Stone);
	}
	// The bell tower and its four-sided spire are one architectural object.
	// Native base corners (2946,845), (3048,895), (3125,857), the front
	// eave at (3050,605), and peak at (3039,485) locate the tower independently
	// of the tile cells whose CELs happen to carry its tall painted silhouette.
	constexpr float towerLeft = 15.30F;
	constexpr float towerRight = 18.50F;
	constexpr float towerBack = 14.60F;
	constexpr float towerFront = 17.00F;
	constexpr float TowerTop = 9.10F;
	builder.Box(towerLeft, towerBack, towerRight, towerFront, 0, TowerTop, TownSceneMaterial::Stone);
	const Position peak { (towerLeft + towerRight) / 2, 11.45F, (towerBack + towerFront) / 2 };
	const Position backLeft { towerLeft - 0.18F, TowerTop, towerBack - 0.18F };
	const Position backRight { towerRight + 0.18F, TowerTop, towerBack - 0.18F };
	const Position frontRight { towerRight + 0.18F, TowerTop, towerFront + 0.18F };
	const Position frontLeft { towerLeft - 0.18F, TowerTop, towerFront + 0.18F };
	builder.RoofTriangle(backRight, backLeft, peak);
	builder.RoofTriangle(frontRight, backRight, peak);
	builder.RoofTriangle(frontLeft, frontRight, peak);
	builder.RoofTriangle(backLeft, frontLeft, peak);
	// The rounded entrance apse uses actual curved geometry, with its opening
	// facing the original (25,29) trigger. It never closes that walkable tile.
	constexpr float ApseX = 25.0F;
	constexpr float ApseZ = 26.0F;
	constexpr float ApseRadius = 2.0F;
	constexpr int ApseSides = 16;
	for (int i = 0; i < ApseSides; ++i) {
		const float first = Pi * static_cast<float>(i) / ApseSides;
		const float second = Pi * static_cast<float>(i + 1) / ApseSides;
		const Position a { ApseX + ApseRadius * std::cos(first), 3.0F, ApseZ + ApseRadius * std::sin(first) };
		const Position b { ApseX + ApseRadius * std::cos(second), 3.0F, ApseZ + ApseRadius * std::sin(second) };
		if (i < 6 || i >= 10) {
			constexpr float InnerRadius = ApseRadius - 0.18F;
			const Position innerA { ApseX + InnerRadius * std::cos(first), 3.0F, ApseZ + InnerRadius * std::sin(first) };
			const Position innerB { ApseX + InnerRadius * std::cos(second), 3.0F, ApseZ + InnerRadius * std::sin(second) };
			builder.WrappedQuad(a, b, { b.x, 0, b.z }, { a.x, 0, a.z }, TownSceneMaterial::Stone,
				first * ApseRadius, second * ApseRadius, -3.0F, 0);
			builder.WrappedQuad(innerB, innerA, { innerA.x, 0, innerA.z }, { innerB.x, 0, innerB.z }, TownSceneMaterial::Stone,
				second * InnerRadius, first * InnerRadius, -3.0F, 0, TownSceneSurfaceRole::Interior);
			builder.Quad(a, innerA, innerB, b, TownSceneMaterial::Stone);
			builder.Quad({ a.x, 0, a.z }, { b.x, 0, b.z }, { innerB.x, 0, innerB.z }, { innerA.x, 0, innerA.z }, TownSceneMaterial::Stone, TownSceneSurfaceRole::Underside);
			builder.Quad(b, innerB, { innerB.x, 0, innerB.z }, { b.x, 0, b.z }, TownSceneMaterial::Stone, TownSceneSurfaceRole::Interior);
			builder.Quad(innerA, a, { a.x, 0, a.z }, { innerA.x, 0, innerA.z }, TownSceneMaterial::Stone, TownSceneSurfaceRole::Interior);
		}
		builder.RoofTriangle(b, a, { ApseX, 4.8F, ApseZ });
	}
	// Entrance steps lead to the original trigger rather than moving its gameplay position.
	const float entranceX = static_cast<float>(entrance.x);
	for (int i = 0; i < 3; ++i) {
		const float step = static_cast<float>(i);
		builder.Box(entranceX - 1.2F, maxZ + step * 0.30F, entranceX + 1.2F,
			maxZ + (step + 1) * 0.30F, 0, 0.30F - step * 0.08F, TownSceneMaterial::Stone);
	}
	builder.model.minTile = { 15, 14 };
	builder.model.maxTile = { 29, 28 };
	FinishBuilder(builder);
}

void SetWallSource(Point tile)
{
	for (TownSceneTriangle &triangle : Scene.back().triangles) {
		if (triangle.material != TownSceneMaterial::Roof && triangle.material != TownSceneMaterial::Water)
			triangle.sourceTile = tile;
	}
}

void BuildScene()
{
	// These groups were measured against the loaded town-tiles.csv and native
	// isometric atlas, not inferred from one solid square at a time. Bounds mark
	// painted scenery to replace; their ground and collision data remain native.
	Scene.reserve(14);
	AddHouse(TownSceneKind::Tavern, { 46, 54 }, { 53, 63 }, { 50, 54 }, { 51, 64 }, 3.1F, 4.5F, true,
		0, false, { 46, 54 }, { 53, 61 });
	SetWallSource({ 52, 62 });
	AddHouse(TownSceneKind::Tavern, { 53, 56 }, { 56, 61 }, { 55, 60 }, { 55, 62 }, 3.1F, 4.4F, false,
		0, false, { 53, 57 }, { 56, 61 });
	SetWallSource({ 54, 60 });
	AddHouse(TownSceneKind::Smithy, { 64, 56 }, { 71, 60 }, { 64, 60 }, { 67, 61 }, 2.5F, 2.5F, false,
		0, false, { 64, 56 }, { 70, 60 });
	SetWallSource({ 68, 60 });
	AddSmithy({ 60, 60 }, { 63, 63 }, { 62, 60 }, { 62, 63 });
	SetWallSource({ 62, 62 });
	// Gillian and Adria stand east of their houses in the original map.
	AddHouse(TownSceneKind::House, { 36, 64 }, { 42, 68 }, { 38, 64 }, { 43, 66 }, 2.55F, 1.65F, false,
		0, true, { 38, 64 }, { 42, 67 });
	SetWallSource({ 42, 66 });
	AddHouse(TownSceneKind::House, { 48, 74 }, { 54, 80 }, { 50, 76 }, { 55, 79 }, 2.65F, 3.35F, true, 0, true);
	SetWallSource({ 54, 78 });
	// Pepin's painted roof extends west beyond the conservative wall plan.
	Scene.back().minTile = { 46, 74 };
	Scene.back().maxTile = { 55, 80 };
	AddWitchHut();
	SetWallSource({ 78, 20 });
	AddHouse(TownSceneKind::House, { 46, 40 }, { 54, 46 }, { 50, 42 }, { 55, 44 }, 2.65F, 1.8F, false,
		0, true, { 46, 42 }, { 54, 45 });
	SetWallSource({ 54, 44 });
	AddHouse(TownSceneKind::House, { 68, 78 }, { 72, 82 }, { 69, 79 }, { 73, 80 }, 2.3F, 2.7F, false,
		0, true, { 68, 78 }, { 72, 81 });
	SetWallSource({ 72, 80 });
	Scene.back().minTile.x = 67; // The native ridge tip extends beyond the wall cells.
	AddCabin({ 26, 48 }, { 30, 52 });
	AddCabin({ 70, 66 }, { 74, 72 });
	AddWell({ 60, 70 }, { 61, 71 });
	AddCathedral({ 20, 16 }, { 29, 27 }, { 22, 18 }, { 25, 29 });
	AddCrypt();
}

} // namespace

bool BuildTownCabinInterior(TownSceneModel &model)
{
	// Only this calibrated, optional model has a measured aperture. Do not
	// infer openings in other imports or change the procedural closed houses.
	if (!model.externalModel || !model.importedTexture || model.kind != TownSceneKind::Cabin
	    || model.minTile != Point { 70, 66 } || model.triangles.empty())
		return false;
	model.cabinInterior = MakeCabinInterior(model);
	return true;
}

const std::vector<TownSceneTriangle> &TownSceneExteriorTriangles(const TownSceneModel &model)
{
	return model.cabinInterior != nullptr ? model.cabinInterior->exteriorTriangles : model.triangles;
}

const std::vector<TownSceneModel> &GetTownScene()
{
	if (Scene.empty())
		BuildScene();
	return Scene;
}

bool TownSceneReplacesTile(Point tile)
{
	if (tile.x == 25 && (tile.y == 29 || tile.y == 30))
		return false;
	// The bell and nave source art are painted several cells above their physical
	// footprint. Restrict this extra mask by both place and native art family.
	if (tile.x >= 14 && tile.x <= 33 && tile.y >= 12 && tile.y <= 31) {
		const uint16_t piece = dPiece[tile.x][tile.y];
		if (piece >= 700 && piece <= 834)
			return true;
	}
	for (const TownSceneModel &model : GetTownScene()) {
		if (model.nativeArtwork.fringeMinPiece != 0
		    && tile.x >= model.nativeArtwork.minTile.x && tile.x <= model.nativeArtwork.maxTile.x
		    && tile.y >= model.nativeArtwork.minTile.y && tile.y <= model.nativeArtwork.maxTile.y) {
			const uint16_t piece = dPiece[tile.x][tile.y];
			// Native roof, door, barrel and foundation fringes spill into otherwise
			// walkable cells. Replace their paint, without suppressing nearby trees.
			if (piece >= model.nativeArtwork.fringeMinPiece && piece <= model.nativeArtwork.fringeMaxPiece)
				return true;
		}
		// The cathedral's architecture is wider than its painted art cells. Its
		// family filter above replaces masonry without absorbing nearby scenery.
		if (model.kind != TownSceneKind::Cathedral
		    && tile.x >= model.minTile.x && tile.x <= model.maxTile.x && tile.y >= model.minTile.y && tile.y <= model.maxTile.y)
			return true;
	}
	return false;
}

void ResetTownScene()
{
	Scene.clear();
}

} // namespace devilution
