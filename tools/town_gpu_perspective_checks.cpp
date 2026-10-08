// Private offscreen GPU checks. No game assets, profile, CMake or game window.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "engine/render/town_gpu.hpp"
#include "town_cabin_light_gpu_checks.hpp"

namespace {
using namespace devilution;
constexpr int FrameSize = 128;
size_t Checks = 0;
size_t Failures = 0;

void Check(bool passed, const std::string &message)
{
	++Checks;
	if (!passed) {
		++Failures;
		if (Failures <= 20)
			std::cerr << "FAIL " << message << '\n';
	}
}

bool Begin(const TownGpuProjection &projection = {})
{
	const bool started = TownGpuBeginFrame(FrameSize, FrameSize, true, projection);
	Check(started, "begin GPU frame: " + GetTownGpuStatus().failure);
	static bool reportedAdapter = false;
	if (started && !reportedAdapter) {
		const auto &status = GetTownGpuStatus();
		std::cout << "D3D11 adapter: " << status.adapter << "; WARP=" << (status.warp ? "true" : "false")
		          << "; featureLevel=" << status.featureLevel << '\n';
		reportedAdapter = true;
	}
	return started;
}

std::array<TownGpuVertex, 3> Vertices(bool perspective)
{
	std::array<TownGpuVertex, 3> result { {
		{ 8, 8, 1, 0.03F, 0.07F, { 0, 0, 0 } },
		{ 120, 12, 4, 0.93F, 0.11F, { 8, 1, 1 } },
		{ 20, 120, 8, 0.13F, 0.91F, { 1, 0, 8 } },
	} };
	for (auto &vertex : result)
		vertex.clipW = perspective ? vertex.depth : 1;
	return result;
}

// Integer screen vertices keep the independent barycentrics exact under the
// D3D subpixel grid. Tests stay away from triangle/texel quantization edges.
std::array<double, 3> Weights(int x, int y)
{
	const double px = x + 0.5, py = y + 0.5;
	constexpr double Area = 112 * 112 - 4 * 12;
	const double wa = ((120 - px) * (120 - py) - (12 - py) * (20 - px)) / Area;
	const double wb = ((20 - px) * (8 - py) - (120 - py) * (8 - px)) / Area;
	return { wa, wb, 1 - wa - wb };
}

bool Interior(const std::array<double, 3> &weights)
{
	return *std::min_element(weights.begin(), weights.end()) > 0.02;
}

std::array<double, 3> Correct(std::array<double, 3> weights, bool perspective)
{
	if (perspective) {
		weights[1] /= 4;
		weights[2] /= 8;
		const double sum = weights[0] + weights[1] + weights[2];
		for (double &weight : weights)
			weight /= sum;
	}
	return weights;
}

bool AwayFromInteger(double value)
{
	return std::abs(value - std::round(value)) > 0.02;
}

void Interpolation(bool perspective)
{
	const TownGpuProjection projection { perspective, 0.08F, 320 };
	if (!Begin(projection))
		return;
	const auto vertices = Vertices(perspective);
	std::vector<uint32_t> codes(FrameSize * FrameSize);
	for (int y = 0; y < FrameSize; ++y)
		for (int x = 0; x < FrameSize; ++x)
			codes[static_cast<size_t>(y) * FrameSize + x] = static_cast<uint32_t>((x + 2 * y) % 251 + 1);
	TownGpuTexture texture;
	texture.stableKey = 0xCA900001U;
	texture.width = texture.height = FrameSize;
	texture.texelCodes = codes;
	TownGpuMaterial material;
	Check(TownGpuSubmitProjectedTriangle(vertices, texture, material, 42), "submit UV/depth fixture");
	TownGpuFrame frame;
	if (!TownGpuEndFrame(frame)) {
		Check(false, "end UV/depth fixture: " + GetTownGpuStatus().failure);
		return;
	}
	Check(frame.indexed.size() == FrameSize * FrameSize && frame.depth.size() == FrameSize * FrameSize
	        && frame.pickIds.size() == FrameSize * FrameSize, "publish three complete GPU outputs");
	if (frame.indexed.size() != FrameSize * FrameSize || frame.depth.size() != FrameSize * FrameSize || frame.pickIds.size() != FrameSize * FrameSize)
		return;
	Check(frame.pickIds[0] == 0 && std::isinf(frame.depth[0]), "untouched sky has no pick and infinite depth");
	size_t samples = 0;
	for (int y = 0; y < FrameSize; ++y) {
		for (int x = 0; x < FrameSize; ++x) {
			const auto screen = Weights(x, y);
			if (!Interior(screen))
				continue;
			const auto weights = Correct(screen, perspective);
			double u = 0, v = 0, depth = 0;
			for (size_t i = 0; i < 3; ++i) {
				u += weights[i] * vertices[i].u;
				v += weights[i] * vertices[i].v;
				depth += weights[i] * vertices[i].depth;
			}
			const size_t pixel = static_cast<size_t>(y) * FrameSize + x;
			Check(std::abs(frame.depth[pixel] - depth) < 0.00002 * std::max(1.0, depth), "analytical corrected camera depth");
			Check(frame.pickIds[pixel] == 42, "pick follows the visible interpolated sample");
			if (AwayFromInteger(u * FrameSize) && AwayFromInteger(v * FrameSize)) {
				const int tx = static_cast<int>(u * FrameSize), ty = static_cast<int>(v * FrameSize);
				Check(frame.indexed[pixel] == codes[static_cast<size_t>(ty) * FrameSize + tx], "analytical corrected UV sample");
				++samples;
			}
		}
	}
	Check(samples > 4000, "UV comparison has meaningful interior coverage");
}

void WorldLighting()
{
	if (!Begin({ true, 0.08F, 320 }))
		return;
	const auto vertices = Vertices(true);
	const std::array<uint32_t, 1> codes { 0 };
	std::array<uint8_t, 256> lut;
	for (size_t i = 0; i < lut.size(); ++i)
		lut[i] = static_cast<uint8_t>(i);
	TownGpuTexture texture;
	texture.stableKey = 0xCA900002U;
	texture.width = texture.height = 1;
	texture.texelCodes = codes;
	texture.lightLut = lut;
	texture.lightLevels = 256;
	const std::array<TownPointLight, 1> lights { TownPointLight { { 0, 5, 0 }, { 1, 0, 0 }, 32, 16 } };
	TownGpuMaterial material;
	material.lighting = TownGpuLighting::Interior;
	material.interiorRedNormalization = 0.2F;
	material.lights = lights;
	Check(TownGpuSubmitProjectedTriangle(vertices, texture, material, 51), "submit world-position point lighting fixture");
	TownGpuFrame frame;
	if (!TownGpuEndFrame(frame) || frame.indexed.size() != FrameSize * FrameSize) {
		Check(false, "end world lighting fixture: " + GetTownGpuStatus().failure);
		return;
	}
	TownLightingConfig config;
	config.ambient = {};
	config.directionalIntensity = 0;
	size_t samples = 0;
	for (int y = 0; y < FrameSize; ++y) {
		for (int x = 0; x < FrameSize; ++x) {
			const auto screen = Weights(x, y);
			if (!Interior(screen))
				continue;
			const auto weights = Correct(screen, true);
			std::array<double, 3> world {};
			for (size_t i = 0; i < 3; ++i)
				for (size_t axis = 0; axis < 3; ++axis)
					world[axis] += weights[i] * vertices[i].world[axis];
			const auto light = SampleTownLighting({ 0, 1, 0 },
				{ static_cast<float>(world[0]), static_cast<float>(world[1]), static_cast<float>(world[2]) }, 0, config, lights);
			const double level = std::clamp(static_cast<double>(light.point.red) / material.interiorRedNormalization, 0.0, 1.0) * 255;
			if (!AwayFromInteger(level + 0.5))
				continue;
			Check(frame.indexed[static_cast<size_t>(y) * FrameSize + x] == static_cast<uint8_t>(level + 0.5), "lighting uses perspective-correct world position");
			++samples;
		}
	}
	Check(samples > 4000, "world lighting comparison has meaningful coverage");
}

void Occlusion()
{
	if (!Begin({ true, 0.08F, 320 }))
		return;
	const auto variable = Vertices(true);
	auto flat = variable;
	for (auto &vertex : flat)
		vertex.depth = vertex.clipW = 2;
	const std::array<uint32_t, 1> codes { 73 };
	TownGpuTexture texture;
	texture.stableKey = 0xCA900003U;
	texture.width = texture.height = 1;
	texture.texelCodes = codes;
	TownGpuMaterial material;
	Check(TownGpuSubmitProjectedTriangle(variable, texture, material, 61), "submit variable-depth triangle");
	Check(TownGpuSubmitProjectedTriangle(flat, texture, material, 62), "submit intersecting depth-two plane");
	TownGpuFrame frame;
	if (!TownGpuEndFrame(frame) || frame.pickIds.size() != FrameSize * FrameSize || frame.depth.size() != FrameSize * FrameSize) {
		Check(false, "end depth intersection fixture: " + GetTownGpuStatus().failure);
		return;
	}
	size_t front = 0, back = 0;
	for (int y = 0; y < FrameSize; ++y) {
		for (int x = 0; x < FrameSize; ++x) {
			const auto screen = Weights(x, y);
			if (!Interior(screen))
				continue;
			const double depth = 1 / (screen[0] + screen[1] / 4 + screen[2] / 8);
			if (std::abs(depth - 2) < 0.001)
				continue;
			const size_t pixel = static_cast<size_t>(y) * FrameSize + x;
			const bool nearest = depth < 2;
			Check(frame.pickIds[pixel] == (nearest ? 61U : 62U), "hardware depth chooses the analytical nearest surface");
			Check(std::abs(frame.depth[pixel] - std::min(depth, 2.0)) < 0.00004, "readback depth belongs to the selected surface");
			(nearest ? front : back)++;
		}
	}
	Check(front > 500 && back > 500, "both sides of the perspective intersection are exercised");
}

void Validation()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float infinity = std::numeric_limits<float>::infinity();
	for (const TownGpuProjection invalid : { TownGpuProjection { true, 0, 320 }, { true, nan, 320 },
	         { true, 1, infinity }, { true, 2, 2 }, { true, 3, 2 }, { true, 1, 4097 } }) {
		Check(!TownGpuBeginFrame(FrameSize, FrameSize, true, invalid), "reject invalid projection range");
		TownGpuFrame frame;
		frame.indexed.push_back(255);
		Check(!TownGpuEndFrame(frame) && frame.indexed.empty() && frame.depth.empty() && frame.pickIds.empty(), "invalid projection cannot publish stale output");
	}
	const std::array<uint32_t, 1> codes { 73 };
	TownGpuTexture texture;
	texture.stableKey = 0xCA900003U;
	texture.width = texture.height = 1;
	texture.texelCodes = codes;
	for (int invalid = 0; invalid < 5; ++invalid) {
		if (!Begin({ true, 0.08F, 320 }))
			continue;
		auto vertices = Vertices(true);
		if (invalid == 0)
			vertices[1].clipW = 1;
		if (invalid == 1)
			vertices[1].clipW = nan;
		if (invalid == 2)
			vertices[1].depth = vertices[1].clipW = 0.07F;
		if (invalid == 3)
			vertices[1].depth = vertices[1].clipW = 321;
		if (invalid == 4)
			vertices[1].x = std::numeric_limits<float>::max();
		Check(!TownGpuSubmitProjectedTriangle(vertices, texture, {}, 71), "reject mismatched or unclipped perspective vertex");
		TownGpuFrame frame;
		frame.depth.push_back(42);
		Check(!TownGpuEndFrame(frame) && frame.indexed.empty() && frame.depth.empty() && frame.pickIds.empty(), "invalid vertex cannot publish partial or stale output");
	}
	for (const float depth : { 0.08F, 320.0F }) {
		if (!Begin({ true, 0.08F, 320 }))
			continue;
		auto vertices = Vertices(true);
		for (auto &vertex : vertices)
			vertex.depth = vertex.clipW = depth;
		Check(TownGpuSubmitProjectedTriangle(vertices, texture, {}, 72), "accept exact near/far boundary");
		TownGpuFrame frame;
		Check(TownGpuEndFrame(frame) && frame.pickIds.size() == FrameSize * FrameSize
		        && frame.pickIds[32 * FrameSize + 32] == 72
		        && std::abs(frame.depth[32 * FrameSize + 32] - depth) < 0.0001F,
		    "exact near/far boundary survives hardware clipping and recovery");
	}
}
} // namespace

int main()
{
	Interpolation(false);
	Interpolation(true);
	WorldLighting();
	Occlusion();
	Validation();
	// Existing orthographic receivers use six-field aggregate initializers.
	RunTownCabinLightGpuChecks(Check);
	std::cout << (Failures == 0 ? "PASS " : "FAIL ") << Checks << " checks; " << Failures << " failures\n";
	return Failures == 0 ? 0 : 1;
}
