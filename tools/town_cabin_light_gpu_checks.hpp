#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "engine/render/town_gpu.hpp"

namespace devilution {

/** Offscreen light-visibility fixtures. No source model, game state or capture
 * is required. WARP is explicitly permitted, as in the other GPU fixtures. */
template <typename CheckFn>
void RunTownCabinLightGpuChecks(CheckFn check)
{
	constexpr int Size = 3;
	const std::array<uint32_t, 1> codes { 0 };
	const std::array<uint8_t, 2> lut { 17, 231 };
	TownGpuTexture texture;
	texture.stableKey = 0xCA81F001U;
	texture.revision = 1;
	texture.width = texture.height = 1;
	texture.texelCodes = codes;
	texture.lightLut = lut;
	texture.lightLevels = 2;
	const auto vertices = [](TownLightVector receiver) {
		const std::array<float, 3> world { receiver.x, receiver.height, receiver.z };
		return std::array<TownGpuVertex, 3> {
			TownGpuVertex { -1, -1, 100, 0, 0, world },
			TownGpuVertex { 9, -1, 100, 0, 0, world },
			TownGpuVertex { -1, 9, 100, 0, 0, world }
		};
	};
	const auto begin = [&]() {
		const bool started = TownGpuBeginFrame(Size, Size, true);
		check(started, "start synthetic opaque-light-blocker frame: " + GetTownGpuStatus().failure);
		if (!started)
			return false;
		const bool ready = TownGpuSetShadow({});
		check(ready, "synthetic point-light frame has no directional shadow");
		return ready;
	};
	const auto draw = [&](const std::string &name, TownLightVector source, TownLightVector receiver,
	                      std::span<const TownLightOccluder> blockers, const TownLightOccluder *room, bool visible) {
		std::vector<TownLightOccluder> all;
		if (room != nullptr)
			all.push_back(*room);
		all.insert(all.end(), blockers.begin(), blockers.end());
		check(TownPointLightVisibility(source, receiver, all) == (visible ? 1 : 0), name + " CPU shell visibility");
		TownLightVector normal { source.x - receiver.x, source.height - receiver.height, source.z - receiver.z };
		const float length = std::sqrt(normal.x * normal.x + normal.height * normal.height + normal.z * normal.z);
		normal = { normal.x / length, normal.height / length, normal.z / length };
		const std::array<TownPointLight, 1> lights { TownPointLight { source, { 1, 0.665F, 0.094F }, 16, 16 } };
		TownLightingConfig config;
		config.ambient = {};
		config.directionalIntensity = 0;
		const auto sample = SampleTownLighting(normal, receiver, 0, config, lights, all);
		check((sample.point.red > 0.0001F) == visible, name + " CPU diffuse receiver has the expected point contribution");
		if (!begin())
			return;
		std::vector<TownLightOccluder> borrowed(blockers.begin(), blockers.end());
		TownGpuMaterial material;
		material.lighting = TownGpuLighting::Interior;
		material.normal = { normal.x, normal.height, normal.z };
		material.interiorRedNormalization = 0.0001F;
		material.lights = lights;
		material.room = room;
		material.blockers = borrowed;
		check(TownGpuSubmitProjectedTriangle(vertices(receiver), texture, material, 37), name + " submit GPU receiver");
		// Submitted constants must own the blockers before their borrowed input
		// changes. Moving them out of the ray must not alter the queued frame.
		for (auto &blocker : borrowed)
			blocker = { { 20, 20, 20 }, { 21, 21, 21 }, {} };
		TownGpuFrame frame;
		check(TownGpuEndFrame(frame), name + " GPU readback: " + GetTownGpuStatus().failure);
		const uint8_t expected = visible ? 231 : 17;
		check(frame.indexed.size() == Size * Size
		        && std::all_of(frame.indexed.begin(), frame.indexed.end(), [&](uint8_t color) { return color == expected; })
		        && frame.pickIds.size() == Size * Size
		        && std::all_of(frame.pickIds.begin(), frame.pickIds.end(), [](uint32_t pick) { return pick == 37; })
		        && frame.depth.size() == Size * Size
		        && std::all_of(frame.depth.begin(), frame.depth.end(), [](float depth) { return std::abs(depth - 100) < 0.001F; }),
		    name + " GPU matches CPU visibility and preserves depth/picking after borrowed blockers change");
	};
	const std::array<TownLightOccluder, 1> bar { TownLightOccluder { { 0.9F, -0.2F, -0.2F }, { 1.1F, 0.2F, 0.2F }, {} } };
	draw("opaque bar crosses ray", {}, { 2, 0, 0 }, bar, nullptr, false);
	draw("parallel ray misses bar", { 0, 0.3F, 0 }, { 2, 0.3F, 0 }, bar, nullptr, true);
	draw("oblique ray misses bar", {}, { 2, 1, 0 }, bar, nullptr, true);
	draw("ray touches box edge", { 0, 0.2F, 0 }, { 2, 0.2F, 0 }, bar, nullptr, false);
	draw("both endpoints inside opaque shell", { 1, 0, 0 }, { 1.05F, 0, 0 }, bar, nullptr, true);
	draw("ray exits opaque shell", { 1, 0, 0 }, { 2, 0, 0 }, bar, nullptr, false);
	draw("ray enters opaque shell", {}, { 1, 0, 0 }, bar, nullptr, false);
	draw("receiver contacts near surface", {}, { 0.9F, 0, 0 }, bar, nullptr, true);
	draw("receiver within endpoint epsilon", {}, { 0.900005F, 0, 0 }, bar, nullptr, true);
	draw("receiver beyond endpoint epsilon", {}, { 0.90003F, 0, 0 }, bar, nullptr, false);
	draw("source contacts far surface", { 1.1F, 0, 0 }, { 2, 0, 0 }, bar, nullptr, true);
	draw("source within endpoint epsilon", { 1.099995F, 0, 0 }, { 2, 0, 0 }, bar, nullptr, true);
	draw("source beyond endpoint epsilon", { 1.09997F, 0, 0 }, { 2, 0, 0 }, bar, nullptr, false);

	const std::array<TownLightAperture, 3> apertures {
		TownLightAperture { TownLightPlane::Z, 71.38F, 71.11F, 71.81F, 1.865F, 2.565F, 20 },
		TownLightAperture { TownLightPlane::Z, 66.38F, 71.10F, 71.74F, 1.922F, 2.562F, 20 },
		TownLightAperture { TownLightPlane::X, 72.97F, 67.315F, 67.625F, 1.035F, 1.395F, 0 }
	};
	const TownLightOccluder room { { 69.78F, 0.1F, 66.38F }, { 72.97F, 4.62F, 71.38F }, apertures };
	const std::array<TownLightOccluder, 4> cabinBars {
		TownLightOccluder { { 71.444F, 1.915F, 71.508F }, { 71.476F, 2.515F, 71.548F }, {} },
		TownLightOccluder { { 71.160F, 2.199F, 71.508F }, { 71.760F, 2.231F, 71.548F }, {} },
		TownLightOccluder { { 71.404F, 1.972F, 66.208F }, { 71.436F, 2.512F, 66.248F }, {} },
		TownLightOccluder { { 71.150F, 2.226F, 66.208F }, { 71.690F, 2.258F, 66.248F }, {} }
	};
	const TownLightVector candle { 70.8499985F, 1.60500002F, 70.8880005F };
	const TownLightVector blockedTunnel { 71.80229F, 2.26367034F, 71.59660148F };
	const TownLightVector clearTunnel { 71.79857893F, 2.28710413F, 71.53200023F };
	draw("selected cabin tunnel before bar shadows", candle, blockedTunnel, {}, &room, true);
	draw("selected cabin tunnel behind wooden division", candle, blockedTunnel, cabinBars, &room, false);
	draw("selected cabin clear tunnel control", candle, clearTunnel, cabinBars, &room, true);
	draw("selected cabin room alone blocks closed wall", candle, { 69.5F, 1.5F, 70.8F }, {}, &room, false);

	std::array<TownLightOccluder, TownMaxLightOccluders - 1> maximum;
	maximum.fill({ { 20, 20, 20 }, { 21, 21, 21 }, {} });
	maximum.back() = bar.front();
	draw("last of maximum opaque blocker count", {}, { 2, 0, 0 }, maximum, nullptr, false);

	const auto reject = [&](const std::string &name, std::span<const TownLightOccluder> blockers) {
		if (!begin())
			return;
		TownGpuMaterial material;
		material.lighting = TownGpuLighting::Interior;
		material.blockers = blockers;
		check(!TownGpuSubmitProjectedTriangle(vertices({}), texture, material, 37)
		        && GetTownGpuStatus().failure.find("opaque blockers") != std::string::npos,
		    name + " is rejected with an explicit GPU blocker failure");
		TownGpuFrame frame;
		check(!TownGpuEndFrame(frame) && !GetTownGpuStatus().frameSucceeded
		        && frame.indexed.empty() && frame.pickIds.empty() && frame.depth.empty(),
		    name + " cannot publish a partial GPU frame");
	};
	std::array<TownLightOccluder, TownMaxLightOccluders> excessive;
	excessive.fill(bar.front());
	reject("excessive opaque blocker count", excessive);
	std::array<TownLightOccluder, 1> invalid = bar;
	invalid[0].minimum.x = std::numeric_limits<float>::quiet_NaN();
	reject("NaN blocker bound", invalid);
	invalid = bar;
	invalid[0].maximum.height = std::numeric_limits<float>::infinity();
	reject("infinite blocker bound", invalid);
	invalid = bar;
	invalid[0].maximum.z = 1000001;
	reject("out-of-range blocker bound", invalid);
	for (unsigned axis = 0; axis < 3; ++axis) {
		invalid = bar;
		if (axis == 0)
			invalid[0].minimum.x = invalid[0].maximum.x;
		else if (axis == 1)
			invalid[0].minimum.height = invalid[0].maximum.height + 1;
		else
			invalid[0].minimum.z = invalid[0].maximum.z + 1;
		reject("empty or reversed blocker axis " + std::to_string(axis), invalid);
	}
	invalid = bar;
	invalid[0].apertures = apertures;
	reject("apertures on opaque blocker", invalid);
	ResetTownGpuResources();
}

} // namespace devilution
