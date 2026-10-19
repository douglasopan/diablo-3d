#include "engine/render/town_body.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "engine/render/town_actor_mask.hpp"

namespace devilution {
namespace {

constexpr float Pi = 3.14159265358979323846F;
constexpr float ImageXScale = 45.25483399593904F;
constexpr float ImageHeightScale = 32.0F;
constexpr float ImageDepthScale = 22.62741699796952F;

struct Vec {
	float x;
	float h;
	float z;
	Vec operator+(Vec rhs) const { return { x + rhs.x, h + rhs.h, z + rhs.z }; }
	Vec operator-(Vec rhs) const { return { x - rhs.x, h - rhs.h, z - rhs.z }; }
	Vec operator*(float value) const { return { x * value, h * value, z * value }; }
};

float Dot(Vec a, Vec b) { return a.x * b.x + a.h * b.h + a.z * b.z; }
Vec Cross(Vec a, Vec b) { return { a.h * b.z - a.z * b.h, a.z * b.x - a.x * b.z, a.x * b.h - a.h * b.x }; }
Vec Position(const TownVolumeVertex &v) { return { v.x, v.height, v.z }; }
Vec Normalize(Vec v)
{
	const float length = std::sqrt(Dot(v, v));
	return length > 0.000001F ? v * (1 / length) : Vec { 0, 1, 0 };
}

struct Run {
	int left;
	int right;
	float Center() const { return (left + right) * 0.5F; }
	int Width() const { return right - left + 1; }
};

struct Component {
	int left;
	int top;
	int right;
	int bottom;
	std::vector<size_t> pixels;
};

struct Image {
	const TownVolumeSprite &sprite;
	std::vector<int> labels;
	std::vector<Component> components;
	int primary = -1;

	bool Opaque(int x, int y) const
	{
		return x >= 0 && y >= 0 && x < sprite.width && y < sprite.height
		    && sprite.opacity[static_cast<size_t>(y) * sprite.width + x] != 0;
	}

	bool Initialize()
	{
		if (sprite.width <= 0 || sprite.height <= 0 || sprite.width > 512 || sprite.height > 512)
			return false;
		const size_t size = static_cast<size_t>(sprite.width) * sprite.height;
		if (sprite.pixels.size() < size || sprite.opacity.size() < size)
			return false;
		labels.assign(size, -1);
		for (int y = 0; y < sprite.height; ++y) {
			for (int x = 0; x < sprite.width; ++x) {
				const size_t index = static_cast<size_t>(y) * sprite.width + x;
				if (!Opaque(x, y) || labels[index] != -1)
					continue;
				const int label = static_cast<int>(components.size());
				Component component { x, y, x, y, { index } };
				labels[index] = label;
				for (size_t head = 0; head < component.pixels.size(); ++head) {
					const int px = static_cast<int>(component.pixels[head] % sprite.width);
					const int py = static_cast<int>(component.pixels[head] / sprite.width);
					component.left = std::min(component.left, px);
					component.right = std::max(component.right, px);
					component.top = std::min(component.top, py);
					component.bottom = std::max(component.bottom, py);
					for (int dy = -1; dy <= 1; ++dy) {
						for (int dx = -1; dx <= 1; ++dx) {
							const int nx = px + dx;
							const int ny = py + dy;
							if (!Opaque(nx, ny))
								continue;
							const size_t next = static_cast<size_t>(ny) * sprite.width + nx;
							if (labels[next] != -1)
								continue;
							labels[next] = label;
							component.pixels.push_back(next);
						}
					}
				}
				components.push_back(std::move(component));
				if (primary < 0 || components.back().pixels.size() > components[primary].pixels.size())
					primary = label;
			}
		}
		return primary >= 0;
	}

	std::vector<Run> Runs(int y, int label = -1) const
	{
		std::vector<Run> runs;
		if (y < 0 || y >= sprite.height)
			return runs;
		if (label < 0)
			label = primary;
		int start = -1;
		for (int x = 0; x <= sprite.width; ++x) {
			const bool present = x < sprite.width && labels[static_cast<size_t>(y) * sprite.width + x] == label;
			if (present && start < 0)
				start = x;
			if (!present && start >= 0) {
				runs.push_back({ start, x - 1 });
				start = -1;
			}
		}
		return runs;
	}

	Run Core(int y, float preferred) const
	{
		Run best { static_cast<int>(preferred), static_cast<int>(preferred) };
		float score = -std::numeric_limits<float>::infinity();
		for (int dy = -1; dy <= 1; ++dy) {
			for (const Run run : Runs(y + dy)) {
				const float candidate = run.Width() - std::abs(run.Center() - preferred) * 0.75F;
				if (candidate > score) {
					score = candidate;
					best = run;
				}
			}
		}
		return best;
	}

	uint8_t Color(float px, float py, int radius = 4) const
	{
		std::array<unsigned, 256> counts {};
		const int cx = static_cast<int>(std::round(px));
		const int cy = static_cast<int>(std::round(py));
		for (int y = std::max(0, cy - radius); y <= std::min(sprite.height - 1, cy + radius); ++y)
			for (int x = std::max(0, cx - radius); x <= std::min(sprite.width - 1, cx + radius); ++x)
				if (Opaque(x, y))
					++counts[sprite.pixels[static_cast<size_t>(y) * sprite.width + x]];
		const auto best = std::max_element(counts.begin(), counts.end());
		if (*best != 0)
			return static_cast<uint8_t>(best - counts.begin());
		// A material remains opaque even if its representative front sample falls
		// in a gap between fingers, legs or clothing. Literal palette black is valid.
		float distance = std::numeric_limits<float>::infinity();
		uint8_t result = 0;
		for (const Component &component : components) {
			for (const size_t pixel : component.pixels) {
				const float x = static_cast<float>(pixel % sprite.width) - px;
				const float y = static_cast<float>(pixel / sprite.width) - py;
				if (x * x + y * y < distance) {
					distance = x * x + y * y;
					result = sprite.pixels[pixel];
				}
			}
		}
		return result;
	}
	uint8_t Color(float px, int py, int radius = 4) const
	{
		return Color(px, static_cast<float>(py), radius);
	}

	Vec Point(float px, float py, float depth = 0) const
	{
		return { (px - sprite.width * 0.5F) / ImageXScale,
			(sprite.height - py + ImageDepthScale * depth) / ImageHeightScale, depth };
	}
	Vec Point(float px, int py, float depth = 0) const
	{
		return Point(px, static_cast<float>(py), depth);
	}

	TownVolumeVertex Vertex(Vec point) const
	{
		return { point.x, point.h, point.z,
			(point.x * ImageXScale + sprite.width * 0.5F) / sprite.width,
			(sprite.height - ImageHeightScale * point.h + ImageDepthScale * point.z) / sprite.height };
	}
};

void Triangle(TownVolumeMesh &mesh, const Image &image, Vec a, Vec b, Vec c, Vec outward, uint8_t color)
{
	Vec normal = Cross(b - a, c - a);
	if (Dot(normal, normal) < 0.0000000001F)
		return;
	if (Dot(normal, outward) < 0) {
		std::swap(b, c);
		normal = normal * -1;
	}
	// Native front is a fixed physical direction, not the moving camera. Its
	// source projection is (45.2548*x,32*h-22.6274*z), with no world height shear.
	const float front = Dot(Normalize(normal), { 0, 0.577350269F, 0.816496581F });
	const TownVolumeMaterial material = front > 0.12F ? TownVolumeMaterial::SpriteFront
	    : (front < -0.12F ? TownVolumeMaterial::SpriteBack : TownVolumeMaterial::SpriteSide);
	mesh.triangles.push_back({ { image.Vertex(a), image.Vertex(b), image.Vertex(c) }, material, color });
}

void Ellipsoid(TownVolumeMesh &mesh, const Image &image, Vec center, Vec radius, uint8_t color)
{
	constexpr int Sides = 12;
	constexpr int Rings = 5;
	center.h = std::max(center.h, radius.h);
	std::array<std::array<Vec, Sides>, Rings> rings;
	for (int row = 0; row < Rings; ++row) {
		const float latitude = Pi * (row + 1) / (Rings + 1);
		for (int side = 0; side < Sides; ++side) {
			const float angle = 2 * Pi * side / Sides;
			rings[row][side] = center + Vec { std::sin(latitude) * std::cos(angle) * radius.x,
				std::cos(latitude) * radius.h, std::sin(latitude) * std::sin(angle) * radius.z };
		}
	}
	const Vec top = center + Vec { 0, radius.h, 0 };
	const Vec bottom = center - Vec { 0, radius.h, 0 };
	for (int side = 0; side < Sides; ++side) {
		const int next = (side + 1) % Sides;
		Triangle(mesh, image, top, rings.front()[side], rings.front()[next], { 0, 1, 0 }, color);
		Triangle(mesh, image, bottom, rings.back()[side], rings.back()[next], { 0, -1, 0 }, color);
		for (int row = 1; row < Rings; ++row) {
			const Vec a = rings[row - 1][side];
			const Vec b = rings[row - 1][next];
			const Vec c = rings[row][next];
			const Vec d = rings[row][side];
			Triangle(mesh, image, a, b, c, (a + b + c) * (1 / 3.0F) - center, color);
			Triangle(mesh, image, a, c, d, (a + c + d) * (1 / 3.0F) - center, color);
		}
	}
}

void Tube(TownVolumeMesh &mesh, const Image &image, const std::vector<Vec> &points,
	const std::vector<float> &radii, float depthRatio, uint8_t color)
{
	constexpr int Sides = 10;
	std::vector<std::array<Vec, Sides>> rings(points.size());
	std::vector<Vec> centers = points;
	for (size_t row = 0; row < points.size(); ++row) {
		// The medial path may lean or bend according to the actual painted pose.
		// A horizontal depth axis makes these limb/garment sections real volumes.
		const Vec tangent = Normalize(points[std::min(row + 1, points.size() - 1)] - points[row == 0 ? 0 : row - 1]);
		const Vec lateral { tangent.h, -tangent.x, 0 };
		centers[row].h = std::max(centers[row].h, std::abs(lateral.h) * radii[row]);
		for (int side = 0; side < Sides; ++side) {
			const float angle = 2 * Pi * side / Sides;
			rings[row][side] = centers[row] + lateral * (std::cos(angle) * radii[row])
			    + Vec { 0, 0, std::sin(angle) * radii[row] * depthRatio };
		}
	}
	for (int side = 0; side < Sides; ++side) {
		const int next = (side + 1) % Sides;
		// Keep the sweep's shared-edge winding, including a strongly bent seated
		// pose. A radial centroid test on each triangle can flip only half of a
		// curved quad. The frame's lateral/depth basis fixes the topology directly.
		const auto closedTriangle = [&](Vec a, Vec b, Vec c) {
			Triangle(mesh, image, a, b, c, Cross(b - a, c - a), color);
		};
		closedTriangle(centers.front(), rings.front()[side], rings.front()[next]);
		closedTriangle(centers.back(), rings.back()[next], rings.back()[side]);
		for (size_t row = 1; row < rings.size(); ++row) {
			const Vec a = rings[row - 1][side];
			const Vec b = rings[row - 1][next];
			const Vec c = rings[row][next];
			const Vec d = rings[row][side];
			closedTriangle(a, c, b);
			closedTriangle(a, d, c);
		}
	}
}

void AddProjectedEllipsoid(TownVolumeMesh &mesh, const Image &image, float cx, float cy,
	float halfWidth, float halfHeight, float depth, uint8_t color)
{
	// Solve the vertical radius from the requested projected ellipse height.
	// Depth already contributes 22.6274*z to native vertical image extent.
	const float projectedDepth = ImageDepthScale * depth;
	const float height = std::sqrt(std::max(halfHeight * halfHeight * 0.35F,
	    halfHeight * halfHeight - projectedDepth * projectedDepth)) / ImageHeightScale;
	Ellipsoid(mesh, image, image.Point(cx, cy), { std::max(halfWidth / ImageXScale, 0.025F), std::max(height, 0.025F), depth }, color);
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
			low.h = std::min(low.h, vertex.height);
			low.z = std::min(low.z, vertex.z);
			high.x = std::max(high.x, vertex.x);
			high.h = std::max(high.h, vertex.height);
			high.z = std::max(high.z, vertex.z);
		}
	}
	mesh.width = high.x - low.x;
	mesh.height = high.h - low.h;
	mesh.depth = high.z - low.z;
}

void Ground(TownVolumeMesh &mesh)
{
	if (mesh.triangles.empty())
		return;
	float minimumHeight = std::numeric_limits<float>::infinity();
	for (const auto &triangle : mesh.triangles)
		for (const auto &vertex : triangle.vertices)
			minimumHeight = std::min(minimumHeight, vertex.height);
	// Translate along the fixed native viewing ray: 32*deltaHeight equals
	// 22.6274*deltaDepth. Feet touch zero without changing projected pixels, UVs,
	// normals, triangle area or closed topology. This handles both signs of minH.
	const float depthShift = minimumHeight * 1.4142135623730951F;
	for (auto &triangle : mesh.triangles) {
		for (auto &vertex : triangle.vertices) {
			vertex.height -= minimumHeight;
			vertex.z -= depthShift;
		}
	}
}

TownVolumeMesh PhysicalRelief(const Image &image)
{
	TownVolumeMesh mesh = BuildTownActorVolume(image.sprite);
	for (auto &triangle : mesh.triangles) {
		for (auto &vertex : triangle.vertices) {
			// Convert the legacy relief's native-ray frame inside the module. The
			// caller always receives horizontal physical depth, including fallback.
			vertex.height += vertex.z * 0.7071067811865475F - 1 / ImageHeightScale;
			vertex = image.Vertex(Position(vertex));
		}
	}
	Ground(mesh);
	Measure(mesh);
	return mesh;
}

} // namespace

TownVolumeMesh BuildTownActorSingleViewBody(const TownVolumeSprite &sprite, bool humanoid)
{
	if (sprite.width <= 0 || sprite.height <= 0 || sprite.width > 512 || sprite.height > 512)
		return {};
	// Painted ground shadow remains part of the original front texture, but is
	// not flesh/clothing from which to infer anatomy, feet or physical depth.
	const std::vector<uint8_t> bodyOpacity = TownActorBodyOpacity(sprite);
	const TownVolumeSprite bodySprite { sprite.width, sprite.height, sprite.pixels, bodyOpacity };
	Image image { bodySprite };
	if (!image.Initialize())
		return {};
	if (!humanoid)
		return PhysicalRelief(image);
	const Component &body = image.components[image.primary];
	const int pixelHeight = body.bottom - body.top + 1;
	const int pixelWidth = body.right - body.left + 1;
	// Cows and very wide/crouched silhouettes cannot safely be assigned a human
	// skeleton from one view. Keep their native relief rather than adding human
	// arms or invented legs. This fallback is deliberately visible to reviewers.
	if (pixelHeight < 20 || pixelHeight < pixelWidth * 0.86F || body.pixels.size() < 50)
		return PhysicalRelief(image);
	const auto row = [&](float fraction) { return body.top + static_cast<int>(fraction * (pixelHeight - 1)); };
	float centerX = (body.left + body.right) * 0.5F;
	for (int iteration = 0; iteration < 2; ++iteration) {
		float sum = 0;
		float weight = 0;
		for (int y = row(0.28F); y <= row(0.62F); ++y) {
			const Run run = image.Core(y, centerX);
			sum += run.Center() * run.Width();
			weight += run.Width();
		}
		centerX = sum / std::max(weight, 1.0F);
	}

	// Select a neck valley near the top quarter; hats, hunched shoulders and body
	// breadth change the resulting proportions instead of using one stock NPC.
	int neckY = row(0.24F);
	float neckScore = std::numeric_limits<float>::infinity();
	for (int y = row(0.16F); y <= row(0.30F); ++y) {
		const Run run = image.Core(y, centerX);
		const float score = run.Width() + std::abs(y - row(0.24F)) * 0.6F;
		if (score < neckScore && run.Width() >= 2) {
			neckScore = score;
			neckY = y;
		}
	}
	const int shoulderY = std::max(neckY + 2, row(0.33F));
	const int hipY = row(0.64F);
	const Run shoulder = image.Core(shoulderY, centerX);
	const Run waist = image.Core(row(0.55F), centerX);
	const Run hips = image.Core(hipY, centerX);
	if (shoulder.Width() < 4 || hips.Width() < 3 || hipY <= neckY + 3)
		return PhysicalRelief(image);
	TownVolumeMesh mesh;
	mesh.triangles.reserve(1800);
	const float bodyScale = pixelHeight / ImageHeightScale;
	const float torsoDepth = std::clamp(std::min(shoulder.Width(), waist.Width()) / ImageXScale * 0.37F, 0.10F, 0.34F);
	const float chestX = shoulder.Center();
	const float chestY = (neckY + hipY) * 0.5F;
	AddProjectedEllipsoid(mesh, image, chestX, chestY, shoulder.Width() * 0.40F,
	    (hipY - neckY) * 0.57F, torsoDepth, image.Color(chestX, chestY));
	AddProjectedEllipsoid(mesh, image, hips.Center(), hipY - pixelHeight * 0.035F,
	    hips.Width() * 0.43F, pixelHeight * 0.13F, torsoDepth * 0.90F, image.Color(hips.Center(), hipY));

	int headLeft = sprite.width;
	int headRight = -1;
	float headCenter = centerX;
	for (int y = body.top; y < neckY; ++y) {
		const Run run = image.Core(y, headCenter);
		if (run.Width() < 2)
			continue;
		headLeft = std::min(headLeft, run.left);
		headRight = std::max(headRight, run.right);
		headCenter = run.Center();
	}
	if (headRight < headLeft)
		return PhysicalRelief(image);
	const float headX = (headLeft + headRight) * 0.5F;
	const float headY = (body.top + neckY) * 0.5F;
	const float headWidth = std::min(static_cast<float>(headRight - headLeft + 1), pixelHeight * 0.44F);
	const float headDepth = std::clamp(headWidth / ImageXScale * 0.43F, 0.07F, 0.23F);
	AddProjectedEllipsoid(mesh, image, headX, headY, headWidth * 0.52F,
	    std::max((neckY - body.top) * 0.52F, 3.0F), headDepth, image.Color(headX, headY));
	Tube(mesh, image, { image.Point(headX, neckY - 1), image.Point(chestX, shoulderY) },
	    { headDepth * 0.52F, headDepth * 0.58F }, 0.95F, image.Color(headX, neckY));

	const float limbRadius = std::clamp(bodyScale * 0.047F, 0.04F, 0.13F);
	for (const int sign : { -1, 1 }) {
		const float sx = chestX + sign * shoulder.Width() * 0.35F;
		const int elbowY = row(0.49F);
		const int handY = row(0.65F);
		const auto outer = [&](int y) {
			const std::vector<Run> runs = image.Runs(y);
			if (runs.empty())
				return sx;
			const Run run = sign < 0 ? runs.front() : runs.back();
			return sign < 0 ? run.left + std::min(run.Width() * 0.30F, limbRadius * ImageXScale)
			                : run.right - std::min(run.Width() * 0.30F, limbRadius * ImageXScale);
		};
		const float elbowX = outer(elbowY);
		const float handX = outer(handY);
		Tube(mesh, image, { image.Point(sx, shoulderY), image.Point(elbowX, elbowY), image.Point(handX, handY) },
		    { limbRadius * 1.12F, limbRadius, limbRadius * 0.75F }, 0.92F, image.Color(elbowX, elbowY));
		Ellipsoid(mesh, image, image.Point(handX, handY), { limbRadius * 0.84F, limbRadius * 1.10F, limbRadius * 0.90F }, image.Color(handX, handY));
	}

	// A robe has one continuous painted lower section. Separated trouser legs
	// need two independent rounded paths; their pose is taken from actual runs.
	int separatedRows = 0;
	int lowerRows = 0;
	for (int y = row(0.70F); y <= row(0.94F); ++y) {
		++lowerRows;
		const auto runs = image.Runs(y);
		for (size_t i = 1; i < runs.size(); ++i) {
			if (runs[i - 1].Width() >= 2 && runs[i].Width() >= 2
			    && runs[i - 1].Center() < centerX && runs[i].Center() > centerX
			    && runs[i].left - runs[i - 1].right >= 2) {
				++separatedRows;
				break;
			}
		}
	}
	const Run hem = image.Core(row(0.89F), centerX);
	const bool skirt = separatedRows < lowerRows / 4 && hem.Width() > hips.Width() * 0.72F;
	if (skirt) {
		const Run middle = image.Core(row(0.77F), centerX);
		Tube(mesh, image, { image.Point(hips.Center(), hipY), image.Point(middle.Center(), row(0.77F)), image.Point(hem.Center(), body.bottom + 0.5F) },
		    { hips.Width() * 0.43F / ImageXScale, middle.Width() * 0.48F / ImageXScale, hem.Width() * 0.49F / ImageXScale },
		    0.72F, image.Color(middle.Center(), row(0.77F)));
	} else {
		for (const int sign : { -1, 1 }) {
			const float preferred = hips.Center() + sign * hips.Width() * 0.24F;
			const auto leg = [&](int y) {
				Run best = image.Core(y, preferred);
				float score = std::numeric_limits<float>::infinity();
				for (const Run run : image.Runs(y)) {
					if ((run.Center() - centerX) * sign < -1 || run.Width() < 2)
						continue;
					const float candidate = std::abs(run.Center() - preferred);
					if (candidate < score) {
						score = candidate;
						best = run;
					}
				}
				return best;
			};
			const Run knee = leg(row(0.80F));
			const Run ankle = leg(row(0.95F));
			const float radius = std::clamp(std::min(knee.Width(), ankle.Width()) * 0.48F / ImageXScale, limbRadius * 0.70F, limbRadius * 1.6F);
			Tube(mesh, image, { image.Point(preferred, hipY), image.Point(knee.Center(), row(0.80F)), image.Point(ankle.Center(), row(0.95F)) },
			    { radius * 1.15F, radius, radius * 0.75F }, 0.90F, image.Color(knee.Center(), row(0.80F)));
			const float bootHeight = std::max(bodyScale * 0.045F, 0.045F);
			Vec foot = image.Point(ankle.Center(), body.bottom + 0.5F);
			foot.h = std::max(foot.h, bootHeight);
			Ellipsoid(mesh, image, foot, { radius * 1.12F, bootHeight, radius * 1.55F }, image.Color(ankle.Center(), body.bottom));
		}
	}

	// Preserve substantial disconnected native props or hands as rounded parts,
	// not flat cards. Tiny antialias specks are ignored; no invented object is added.
	std::vector<size_t> accessories;
	for (size_t label = 0; label < image.components.size(); ++label) {
		if (static_cast<int>(label) == image.primary)
			continue;
		if (image.components[label].pixels.size() >= 5)
			accessories.push_back(label);
	}
	std::sort(accessories.begin(), accessories.end(), [&](size_t a, size_t b) {
		return image.components[a].pixels.size() > image.components[b].pixels.size();
	});
	// Bound work for an unusually noisy/malformed image; native NPCs have few
	// detached parts. Prefer substantial objects over isolated pixel fragments.
	if (accessories.size() > 16)
		accessories.resize(16);
	for (const size_t label : accessories) {
		const Component &part = image.components[label];
		const float cx = (part.left + part.right) * 0.5F;
		const float cy = (part.top + part.bottom) * 0.5F;
		const float width = static_cast<float>(part.right - part.left + 1);
		const float height = static_cast<float>(part.bottom - part.top + 1);
		const float depth = std::clamp(std::min(width, height) / ImageXScale * 0.40F, 0.025F, 0.15F);
		AddProjectedEllipsoid(mesh, image, cx, cy, width * 0.50F, height * 0.50F, depth, image.Color(cx, cy));
	}
	Ground(mesh);
	Measure(mesh);
	return mesh;
}

} // namespace devilution
