// Standalone synthetic prototype: no MPQ, profile, game globals or game window.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "engine/render/town_camera.hpp"
#include "engine/render/town_horizon.hpp"
#include "town_camera_checks.hpp"
#include "town_horizon_checks.hpp"

namespace {
using namespace devilution;
using Clock = std::chrono::steady_clock;

struct Triangle {
	std::array<TownCameraVertex, 3> vertices;
	TownHorizonColor color;
	int32_t pick = -1; // Visible decoration must overwrite a hidden native ID.
	bool cutout = false;
};

struct Counters {
	uint64_t submitted = 0;
	uint64_t clipped = 0;
	uint64_t pixelVisits = 0;
	uint64_t covered = 0;
	uint64_t depthRejected = 0;
	uint64_t opacityRejected = 0;
	uint64_t shaded = 0;
};

struct Image {
	int width;
	int height;
	std::vector<TownHorizonColor> colors;
	std::vector<float> depth;
	std::vector<int32_t> picks;
	Counters counters;

	Image(int w, int h)
	    : width(w), height(h), colors(static_cast<size_t>(w) * h),
	      depth(colors.size(), std::numeric_limits<float>::infinity()), picks(colors.size(), -1)
	{
		for (int y = 0; y < h; ++y) {
			const float t = static_cast<float>(y) / h;
			const TownHorizonColor sky { 0.055F + t * 0.105F, 0.08F + t * 0.12F, 0.12F + t * 0.12F };
			std::fill(colors.begin() + static_cast<size_t>(y) * w, colors.begin() + static_cast<size_t>(y + 1) * w, sky);
		}
	}

	size_t bufferBytes() const { return colors.size() * (sizeof(TownHorizonColor) + sizeof(float) + sizeof(int32_t)); }
};

float Edge(const TownCameraProjectedVertex &a, const TownCameraProjectedVertex &b, float x, float y)
{
	return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}

void Draw(Image &image, const TownCameraFrame &frame, const Triangle &triangle, bool fogEnabled)
{
	++image.counters.submitted;
	const auto clipped = ClipTownCameraTriangle(frame, triangle.vertices);
	image.counters.clipped += clipped.count;
	const TownHorizonFogConfig fog;
	for (size_t i = 0; i < clipped.count; ++i) {
		const auto &vertices = clipped.triangles[i];
		const auto &a = vertices[0], &b = vertices[1], &c = vertices[2];
		const float area = Edge(a, b, c.x, c.y);
		if (std::abs(area) < 0.0001F)
			continue;
		const int left = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
		const int right = std::min(image.width - 1, static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
		const int top = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
		const int bottom = std::min(image.height - 1, static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
		for (int y = top; y <= bottom; ++y) {
			for (int x = left; x <= right; ++x) {
				++image.counters.pixelVisits;
				const float px = x + 0.5F, py = y + 0.5F;
				const float wa = Edge(b, c, px, py) / area, wb = Edge(c, a, px, py) / area;
				const std::array<float, 3> weights { wa, wb, 1 - wa - wb };
				if (weights[0] < 0 || weights[1] < 0 || weights[2] < 0)
					continue;
				++image.counters.covered;
				TownCameraSample sample;
				if (!InterpolateTownCameraSample(vertices, weights, sample))
					continue;
				const size_t index = static_cast<size_t>(y) * image.width + x;
				if (sample.depth > image.depth[index]) {
					++image.counters.depthRejected;
					continue;
				}
				if (triangle.cutout && ((static_cast<int>(std::floor(sample.u * 8)) + static_cast<int>(std::floor(sample.v * 8))) & 1) == 0) {
					++image.counters.opacityRejected;
					continue;
				}
				TownHorizonColor color = triangle.color;
				if (triangle.pick == 10) {
					const int grid = (static_cast<int>(std::floor(sample.world.x / 4)) + static_cast<int>(std::floor(sample.world.z / 4))) & 1;
					const float scale = grid != 0 ? 0.88F : 1;
					color = { color.red * scale, color.green * scale, color.blue * scale };
				}
				image.colors[index] = fogEnabled ? ApplyTownHorizonFog(color, TownCameraFogDepth(frame, sample.depth), fog) : color;
				image.depth[index] = sample.depth;
				image.picks[index] = triangle.pick;
				++image.counters.shaded;
			}
		}
	}
}

void Quad(std::vector<Triangle> &scene, std::array<TownCameraPoint, 4> points, TownHorizonColor color, int32_t pick)
{
	const std::array<TownCameraVertex, 4> vertices { TownCameraVertex { points[0], 0, 0 }, { points[1], 1, 0 }, { points[2], 1, 1 }, { points[3], 0, 1 } };
	scene.push_back({ { vertices[0], vertices[1], vertices[2] }, color, pick });
	scene.push_back({ { vertices[0], vertices[2], vertices[3] }, color, pick });
}

void Box(std::vector<Triangle> &scene, float x, float z, float halfWidth, float height, TownHorizonColor color, int32_t pick)
{
	const float a = x - halfWidth, b = x + halfWidth, c = z - halfWidth, d = z + halfWidth;
	Quad(scene, { TownCameraPoint { a, 0, c }, { b, 0, c }, { b, height, c }, { a, height, c } }, color, pick);
	Quad(scene, { TownCameraPoint { b, 0, c }, { b, 0, d }, { b, height, d }, { b, height, c } }, { color.red * 0.7F, color.green * 0.7F, color.blue * 0.7F }, pick);
	Quad(scene, { TownCameraPoint { b, 0, d }, { a, 0, d }, { a, height, d }, { b, height, d } }, color, pick);
	Quad(scene, { TownCameraPoint { a, 0, d }, { a, 0, c }, { a, height, c }, { a, height, d } }, { color.red * 0.7F, color.green * 0.7F, color.blue * 0.7F }, pick);
	Quad(scene, { TownCameraPoint { a, height, c }, { b, height, c }, { b, height, d }, { a, height, d } }, { 0.12F, 0.08F, 0.06F }, pick);
}

std::vector<Triangle> SyntheticScene()
{
	std::vector<Triangle> scene;
	Quad(scene, { TownCameraPoint { 0, 0, 0 }, { 112, 0, 0 }, { 112, 0, 112 }, { 0, 0, 112 } }, { 0.085F, 0.11F, 0.065F }, 10);
	Box(scene, 44, 46, 3, 4, { 0.26F, 0.17F, 0.09F }, 20);
	Box(scene, 61, 39, 4, 6, { 0.22F, 0.21F, 0.18F }, 21);
	Box(scene, 74, 64, 2.5F, 4, { 0.18F, 0.12F, 0.07F }, 22);
	Box(scene, 38, 72, 2, 3.5F, { 0.21F, 0.15F, 0.09F }, 23);
	return scene;
}

Image Render(const TownCameraRig &rig, const TownHorizonMesh &horizon, int width, int height, bool horizonEnabled, bool fogEnabled,
	bool localPlayer = true, TownCameraPoint anchor = { 56, 0, 56 })
{
	Image image(width, height);
	const auto frame = BuildTownCameraFrame(rig, anchor, width, height, width / 2.0F, height / 2.0F);
	if (horizonEnabled) {
		for (const auto &triangle : horizon.triangles)
			Draw(image, frame, { { TownCameraVertex { triangle.vertices[0] }, { triangle.vertices[1] }, { triangle.vertices[2] } }, triangle.color, -1 }, fogEnabled);
	}
	auto scene = SyntheticScene();
	if (localPlayer && !rig.HideLocalPlayer())
		Box(scene, anchor.x, anchor.z, 0.25F, 1.4F, { 0.45F, 0.10F, 0.06F }, 30);
	for (const Triangle &triangle : scene)
		Draw(image, frame, triangle, fogEnabled);
	return image;
}

void SavePpm(const Image &image, const std::filesystem::path &path)
{
	std::ofstream file(path, std::ios::binary);
	file << "P6\n" << image.width << ' ' << image.height << "\n255\n";
	for (const auto &color : image.colors) {
		const auto encode = [](float linear) {
			const float bounded = std::clamp(linear, 0.0F, 1.0F);
			const float srgb = bounded <= 0.0031308F ? bounded * 12.92F : 1.055F * std::pow(bounded, 1 / 2.4F) - 0.055F;
			return static_cast<char>(static_cast<unsigned char>(std::round(srgb * 255)));
		};
		const char bytes[] { encode(color.red), encode(color.green), encode(color.blue) };
		file.write(bytes, sizeof(bytes));
	}
	if (!file)
		throw std::runtime_error("Unable to save synthetic image");
}

void WriteCounters(std::ostream &out, const Counters &counters)
{
	out << "{\"submitted\":" << counters.submitted << ",\"clipped\":" << counters.clipped
	    << ",\"pixel_visits\":" << counters.pixelVisits << ",\"covered\":" << counters.covered
	    << ",\"depth_rejected\":" << counters.depthRejected << ",\"opacity_rejected\":" << counters.opacityRejected
	    << ",\"shaded\":" << counters.shaded << '}';
}

bool RasterChecks(size_t &checks)
{
	TownCameraRig rig;
	rig.SetMode(TownCameraMode::FirstPerson);
	const auto frame = BuildTownCameraFrame(rig, {}, 96, 64, 48, 32);
	const auto vertex = [&frame](float x, float y, float z, float u, float v) { return TownCameraVertex { TownCameraToWorld(frame, { x, y, z }), u, v }; };
	const Triangle behind { { vertex(-3, -3, 4, 0, 0), vertex(3, -3, 4, 1, 0), vertex(0, 3, 4, 0.5F, 1) }, { 0.5F, 0.4F, 0.1F }, 42 };
	const Triangle decoration { { vertex(-3, -3, 2, 0, 0), vertex(3, -3, 2, 1, 0), vertex(0, 3, 2, 0.5F, 1) }, { 0.1F, 0.2F, 0.3F }, -1 };
	Image image(96, 64);
	Draw(image, frame, behind, false);
	Draw(image, frame, decoration, false);
	const size_t center = 32 * 96 + 48;
	++checks;
	if (image.picks[center] != -1 || std::abs(image.depth[center] - 2) > 0.0001F)
		return false;
	Triangle cutout = decoration;
	cutout.cutout = true;
	Image holes(96, 64);
	Draw(holes, frame, behind, false);
	Draw(holes, frame, cutout, true);
	size_t through = 0, blocked = 0;
	for (int32_t pick : holes.picks) {
		through += pick == 42 ? 1 : 0;
		blocked += pick == -1 ? 1 : 0;
	}
	++checks;
	if (through == 0 || blocked == 0 || holes.counters.opacityRejected == 0)
		return false;
	const auto horizon = BuildTownHorizon({});
	rig.SetPose({ 0.7853981634F, 0.03F, 0, {} });
	const auto clear = Render(rig, horizon, 320, 180, true, false);
	const auto fog = Render(rig, horizon, 320, 180, true, true);
	++checks;
	if (clear.depth != fog.depth || clear.picks != fog.picks)
		return false;
	const auto noHorizon = Render(rig, horizon, 320, 180, false, true);
	for (size_t i = 0; i < fog.picks.size(); ++i) {
		if (noHorizon.picks[i] < 0)
			continue;
		++checks;
		if (fog.picks[i] != noHorizon.picks[i] || fog.depth[i] != noHorizon.depth[i])
			return false;
	}
	return true;
}

struct BoundaryPose {
	TownCameraPoint anchor;
	float yaw;
};

constexpr std::array<BoundaryPose, 8> Boundaries { {
	{ { 0, 0, 56 }, 0 }, { { 56, 0, 0 }, 1.5707963268F },
	{ { 112, 0, 56 }, 3.1415926536F }, { { 56, 0, 112 }, 4.7123889804F },
	{ { 0, 0, 0 }, 0.7853981634F }, { { 112, 0, 0 }, 2.3561944902F },
	{ { 112, 0, 112 }, 3.9269908170F }, { { 0, 0, 112 }, 5.4977871438F },
} };

bool BoundaryChecks(const TownHorizonMesh &horizon, size_t &checks)
{
	for (const auto &boundary : Boundaries) {
		TownCameraRig rig;
		rig.SetMode(TownCameraMode::FirstPerson);
		rig.SetPose({ boundary.yaw, 0, 0, {} });
		const Image image = Render(rig, horizon, 320, 180, true, true, true, boundary.anchor);
		// Every downward ray in this central band points outside the native
		// footprint. It must hit continuous visual terrain with no gameplay ID.
		for (int y = 135; y < 180; ++y) {
			for (int x = 40; x < 280; ++x) {
				++checks;
				const size_t index = static_cast<size_t>(y) * 320 + x;
				if (!std::isfinite(image.depth[index]) || image.picks[index] != -1)
					return false;
			}
		}
	}
	return true;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "Usage: town_camera_horizon_smoke OUTPUT_DIRECTORY\n";
		return 2;
	}
	try {
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		size_t checks = 0;
		if (!RunTownCameraChecks(std::cout, checks) || !RunTownHorizonChecks(std::cout, checks) || !RasterChecks(checks)) {
			std::cerr << "Prototype checks failed after " << checks << " checks\n";
			return 1;
		}
		const auto horizon = BuildTownHorizon({});
		if (!BoundaryChecks(horizon, checks)) {
			std::cerr << "Synthetic boundary terrain has a coverage/picking failure\n";
			return 1;
		}
		std::ofstream report(directory / "summary.json");
		report << std::fixed << std::setprecision(4);
		report << "{\"scope\":\"synthetic CPU prototype; not installed or integrated; no game assets\",\"checks\":" << checks
		       << ",\"horizon_triangles\":" << horizon.statistics.triangles << ",\"horizon_bytes\":" << horizon.statistics.reservedTriangleBytes
		       << ",\"gpu_measured\":false,\"benchmarks\":[";
		bool firstBenchmark = true;
		for (TownCameraMode mode : { TownCameraMode::Isometric, TownCameraMode::FreeOrbit, TownCameraMode::ThirdPerson, TownCameraMode::FirstPerson }) {
			TownCameraRig rig;
			rig.SetMode(mode);
			if (mode == TownCameraMode::FreeOrbit)
				rig.SetPose({ 0.9F, 0.27F, 44, {} });
			if (mode == TownCameraMode::FirstPerson)
				rig.SetPose({ 0.7853981634F, -0.025F, 0, {} });
			SavePpm(Render(rig, horizon, 960, 540, true, true), directory / ("mode-" + std::to_string(static_cast<int>(mode)) + ".ppm"));
			for (const auto resolution : { std::array<int, 2> { 960, 540 }, std::array<int, 2> { 1920, 1080 } }) {
				std::array<std::vector<double>, 2> times;
				std::vector<double> pairedDeltas;
				std::array<Counters, 2> counters;
				size_t bytes = 0;
				for (int repeat = 0; repeat < 9; ++repeat) {
					std::array<double, 2> pair;
					for (int pass = 0; pass < 2; ++pass) {
						// Alternate AB/BA pairs to limit load/thermal drift bias.
						const size_t enabled = static_cast<size_t>((repeat + pass) % 2);
						const auto start = Clock::now();
						const auto image = Render(rig, horizon, resolution[0], resolution[1], enabled != 0, true);
						const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
						if (repeat != 0)
							times[enabled].push_back(ms);
						pair[enabled] = ms;
						counters[enabled] = image.counters;
						bytes = image.bufferBytes();
					}
					if (repeat != 0)
						pairedDeltas.push_back(pair[1] - pair[0]);
				}
				std::sort(pairedDeltas.begin(), pairedDeltas.end());
				for (size_t enabled = 0; enabled < 2; ++enabled) {
					std::sort(times[enabled].begin(), times[enabled].end());
					if (!firstBenchmark)
						report << ',';
					firstBenchmark = false;
					report << "{\"mode\":" << static_cast<int>(mode) << ",\"width\":" << resolution[0] << ",\"height\":" << resolution[1]
					       << ",\"horizon\":" << (enabled != 0 ? "true" : "false") << ",\"samples\":8,\"order\":\"alternating AB/BA pairs\",\"median_ms\":" << (times[enabled][3] + times[enabled][4]) / 2
					       << ",\"p95_ms\":" << times[enabled].back() << ",\"paired_delta_median_ms\":" << (pairedDeltas[3] + pairedDeltas[4]) / 2
					       << ",\"frame_buffer_bytes\":" << bytes << ",\"counters\":";
					WriteCounters(report, counters[enabled]);
					report << '}';
				}
			}
		}
		for (int angle = 0; angle < 8; ++angle) {
			TownCameraRig rig;
			rig.SetMode(TownCameraMode::FirstPerson);
			rig.SetPose({ angle * 0.7853981634F, -0.025F, 0, {} });
			SavePpm(Render(rig, horizon, 640, 360, true, true), directory / ("ocular-" + std::to_string(angle) + ".ppm"));
		}
		for (size_t i = 0; i < Boundaries.size(); ++i) {
			TownCameraRig rig;
			rig.SetMode(TownCameraMode::FirstPerson);
			rig.SetPose({ Boundaries[i].yaw, 0, 0, {} });
			SavePpm(Render(rig, horizon, 640, 360, true, true, true, Boundaries[i].anchor), directory / ("boundary-" + std::to_string(i) + ".ppm"));
		}
		report << "]}\n";
		if (!report)
			throw std::runtime_error("Unable to save diagnostic summary");
		std::cout << "PASS " << checks << " checks; synthetic CPU images and A/B measurements saved\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 2;
	}
}
