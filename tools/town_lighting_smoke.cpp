// Standalone mathematical lighting contracts; no original game data is needed.
#include "engine/render/town_lighting.hpp"
#include "engine/render/town_lighting_profile.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::atomic<std::size_t> Allocations { 0 };

void Check(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
	std::cout << "PASS " << message << '\n';
}

bool Close(float first, float second, float tolerance = 0.00001F)
{
	return std::abs(first - second) <= tolerance;
}

bool Same(devilution::TownLightColor first, devilution::TownLightColor second)
{
	return Close(first.red, second.red) && Close(first.green, second.green) && Close(first.blue, second.blue);
}

bool Black(devilution::TownLightColor color)
{
	return color.red == 0 && color.green == 0 && color.blue == 0;
}

bool SameConfig(const devilution::TownLightingConfig &first, const devilution::TownLightingConfig &second)
{
	return first.ambient.red == second.ambient.red && first.ambient.green == second.ambient.green && first.ambient.blue == second.ambient.blue
	    && first.directional.red == second.directional.red && first.directional.green == second.directional.green && first.directional.blue == second.directional.blue
	    && first.toLight.x == second.toLight.x && first.toLight.height == second.toLight.height && first.toLight.z == second.toLight.z
	    && first.directionalIntensity == second.directionalIntensity;
}

void CheckLightingProfile()
{
	using namespace devilution;
	const TownLightingConfig defaults = TristramLightingConfig();
	TownLightingConfig parsed;
	constexpr std::string_view Canonical = "[Tristram]\nAmbientRGB=.08,.085,.09\nDirectionalRGB=1.55,1.47,1.62\nSunDirection=.32,1,-.3\nDirectionalIntensity=1\n";
	Check(ParseTownLightingProfile(Canonical, parsed) && SameConfig(parsed, defaults), "Tristram profile defaults match the explicit calibrated configuration");
	constexpr std::string_view Commented = "\xEF\xBB\xBF# local override\r\n  [Tristram] ; section\r\n\r\nDirectionalIntensity = +1 # strength\r\nSunDirection = +.32, 1e0, -.3\r\nDirectionalRGB=1.55,1.47,1.62\r\nAmbientRGB=.08,.085,.09;ambient\r\n";
	const std::size_t beforeParse = Allocations.load();
	const bool commentedValid = ParseTownLightingProfile(Commented, parsed);
	const std::size_t parseAllocations = Allocations.load() - beforeParse;
	Check(commentedValid && SameConfig(parsed, defaults) && parseAllocations == 0, "profile parsing accepts comments, whitespace, BOM and key order without allocation");
	Check(ParseTownLightingProfile("[Tristram]\nAmbientRGB=0,4,0\nDirectionalRGB=4,0,4\nSunDirection=-4,.00011,4\nDirectionalIntensity=4", parsed)
	        && parsed.ambient.green == 4 && parsed.directionalIntensity == 4 && parsed.toLight.x == -4,
		"profile boundary values are inclusive while sunlight remains above the ground plane");
	const std::array<std::string_view, 7> invalidProfiles {
		"", "[Other]\n", "AmbientRGB=1,1,1\n[Tristram]\n", "[Tristram]\nAmbientRGB=1,1,1\n",
		"[Tristram]\nAmbientRGB=1,1,1\nAmbientRGB=1,1,1\n",
		"[Tristram]\nUnknown=1\n", "[Tristram]\n[Tristram]\n"
	};
	bool rejectedAtomically = true;
	for (const auto invalidProfile : invalidProfiles) {
		const auto original = parsed;
		rejectedAtomically = rejectedAtomically && !ParseTownLightingProfile(invalidProfile, parsed) && SameConfig(parsed, original);
	}
	const std::array<std::string_view, 16> invalidValues {
		"AmbientRGB=-.01,1,1", "DirectionalRGB=4.01,1,1", "DirectionalIntensity=-1", "DirectionalIntensity=4.01",
		"AmbientRGB=nan,1,1", "DirectionalRGB=inf,1,1", "DirectionalIntensity=1e999",
		"SunDirection=0,0,0", "SunDirection=0,-1,0", "SunDirection=0,.0001,0", "SunDirection=0,.00011,0", "SunDirection=4.01,1,0",
		"AmbientRGB=1,1,1,1", "AmbientRGB=1,,1", "DirectionalIntensity=1 trailing", "DirectionalIntensity=+-1"
	};
	for (const auto invalidValue : invalidValues) {
		// Keep all required keys present so rejecting this token cannot be masked
		// by a missing-key error elsewhere in the fixture.
		const auto key = invalidValue.substr(0, invalidValue.find('=') + 1);
		std::string invalidProfile(Canonical);
		const auto begin = invalidProfile.find(key);
		const auto end = invalidProfile.find('\n', begin);
		invalidProfile.replace(begin, end - begin, invalidValue);
		const auto original = parsed;
		rejectedAtomically = rejectedAtomically && !ParseTownLightingProfile(invalidProfile, parsed) && SameConfig(parsed, original);
	}
	const auto original = parsed;
	std::string embeddedNull(Canonical);
	embeddedNull.push_back('\0');
	rejectedAtomically = rejectedAtomically && !ParseTownLightingProfile(embeddedNull, parsed) && SameConfig(parsed, original);
	Check(rejectedAtomically, "invalid, missing, duplicate, unknown and nonfinite profile data leaves the configuration unchanged");
	std::string bounded(Canonical);
	bounded.append(TownLightingProfileMaxBytes - bounded.size(), ' ');
	const bool limitValid = ParseTownLightingProfile(bounded, parsed) && SameConfig(parsed, defaults);
	bounded.push_back(' ');
	Check(limitValid && !ParseTownLightingProfile(bounded, parsed) && SameConfig(parsed, defaults), "profile parsing accepts exactly 4096 bytes and rejects larger files atomically");
}

} // namespace

// Measure actual C++ allocation calls around the repeated shader computations.
void *operator new(std::size_t size)
{
	++Allocations;
	if (void *memory = std::malloc(size == 0 ? 1 : size))
		return memory;
	throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete(void *memory) noexcept
{
	std::free(memory);
}

void operator delete[](void *memory) noexcept
{
	std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
	std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
	std::free(memory);
}

int main()
{
	using namespace devilution;
	try {
		CheckLightingProfile();
		TownLightingConfig config;
		config.toLight = { 0, 1, 0 };
		const TownLightVector upward { 0, 1, 0 };
		const TownLightVector ground { 0, 0, 0 };
		const TownLightVector sourceNormal { 0, 2, 0 };
		const auto frontNormal = OrientTownLightingNormal(sourceNormal, { 0, 3, 0 });
		const auto backNormal = OrientTownLightingNormal(sourceNormal, { 0, -3, 0 });
		Check(frontNormal.x == sourceNormal.x && frontNormal.height == sourceNormal.height && frontNormal.z == sourceNormal.z
		        && backNormal.x == -sourceNormal.x && backNormal.height == -sourceNormal.height && backNormal.z == -sourceNormal.z,
			"two-sided shading keeps front normals and reverses visible back normals without changing source length");
		Check(OrientTownLightingNormal(sourceNormal, { 0, -3, 0 }, false).height == sourceNormal.height
		        && sourceNormal.height == 2, "one-sided lighting and authored source normals remain unchanged");
		const auto frontIrradiance = SampleTownLighting(OrientTownLightingNormal({ 0, 2, 0 }, upward), ground, 0, config);
		const auto backIrradiance = SampleTownLighting(OrientTownLightingNormal({ 0, -2, 0 }, upward), ground, 0, config);
		Check(Same(frontIrradiance.total(), backIrradiance.total()), "both visible sides receive the same irradiance under matching oriented normals");
		Check(OrientTownLightingNormal({ std::numeric_limits<float>::quiet_NaN(), 1, 0 }, upward).height == 0
		        && OrientTownLightingNormal(sourceNormal, { std::numeric_limits<float>::infinity(), 0, 0 }).height == sourceNormal.height
		        && OrientTownLightingNormal(sourceNormal, {}).height == sourceNormal.height,
			"invalid and zero normal-orientation inputs remain finite and graceful");
		const auto exposed = SampleTownLighting(upward, ground, 0, config);
		const auto shadowed = SampleTownLighting(upward, ground, 1, config);
		Check(Same(exposed.ambient, shadowed.ambient) && !Black(exposed.directional) && Black(shadowed.directional),
			"directional geometry shadow attenuates direct light without changing ambient");
		const auto halfShadow = SampleTownLighting(upward, ground, 0.5F, config);
		Check(Close(halfShadow.directional.red, exposed.directional.red * 0.5F), "partial occlusion scales direct light continuously");
		const auto scaledNormal = SampleTownLighting({ 0, 10, 0 }, ground, 0, config);
		TownLightingConfig scaledLight = config;
		scaledLight.toLight = { 0, 7, 0 };
		Check(Same(exposed.total(), scaledNormal.total()) && Same(exposed.total(), SampleTownLighting(upward, ground, 0, scaledLight).total()),
			"surface normal and light direction are normalized independently of scale");
		Check(Black(SampleTownLighting({ 0, -1, 0 }, ground, 0, config).directional), "light-facing diffuse term does not illuminate back-facing surfaces");
		const TownLightColor emission { 1, 0.7F, 0.08F };
		Check(Same(ComposeTownLitColor({ 0.5F, 0.5F, 0.5F }, TownLightingSample {}, emission), emission),
			"warm self-emission survives darkness independently of diffuse light");
		bool roundtrip = true;
		for (int value = 0; value <= 255; ++value) {
			const float encoded = static_cast<float>(value) / 255;
			roundtrip = roundtrip && Close(TownLinearToSrgb(TownSrgbToLinear(encoded)), encoded, 0.000002F);
		}
		Check(roundtrip && Close(TownSrgbToLinear(0.5F), 0.214041F, 0.000002F), "all sRGB palette values roundtrip through linear-light conversion");
		TownLightingSample halfLight;
		halfLight.ambient = { 0.5F, 0.5F, 0.5F };
		const float litGray = TownLinearToSrgb(ComposeTownLitColor(TownSrgbToLinear({ 0.5F, 0.5F, 0.5F }), halfLight).red);
		Check(litGray > 0.35F && litGray < 0.37F, "linear-light shading avoids multiplying encoded sRGB values");

		std::array<TownLightOccluder, 1> room { TownLightOccluder { { 0, 0, 0 }, { 2, 2, 2 }, {} } };
		const TownLightVector lamp { 1, 1, 1 };
		Check(TownPointLightVisibility(lamp, { 1.5F, 0.5F, 1 }, room) == 1, "opaque room shell does not block two points inside its interior");
		Check(TownPointLightVisibility(lamp, { 3, 1, 1 }, room) == 0, "room walls block a point light reaching exterior receivers");
		std::array<TownLightAperture, 1> doorway { TownLightAperture { TownLightPlane::X, 2, 0.5F, 1.5F, 0.5F, 1.5F } };
		room[0].apertures = doorway;
		Check(TownPointLightVisibility(lamp, { 3, 1, 1 }, room) == 1, "an explicit open doorway transmits a ray through its actual bounds");
		Check(TownPointLightVisibility(lamp, { 3, 1.9F, 1.9F }, room) == 1,
			"legacy rectangular apertures keep their existing corner visibility");
		doorway[0].polygonSides = 20;
		const std::size_t beforePolygonCheck = Allocations.load();
		const bool centerOpen = TownPointLightVisibility(lamp, { 3, 1, 1 }, room) == 1;
		const bool cornerBlocked = TownPointLightVisibility(lamp, { 3, 1.9F, 1.9F }, room) == 0;
		const std::size_t polygonAllocations = Allocations.load() - beforePolygonCheck;
		Check(centerOpen && cornerBlocked && polygonAllocations == 0,
			"inscribed polygon aperture passes its center but blocks rectangular corner leakage without allocation");
		std::array<TownLightAperture, 1> cabinWindow {
			TownLightAperture { TownLightPlane::Z, 71.38F, 71.11F, 71.81F, 1.865F, 2.565F, 20 }
		};
		std::array<TownLightOccluder, 1> cabinRoom { TownLightOccluder { { 70, 0.1F, 67 }, { 74, 4.62F, 71.38F }, cabinWindow } };
		const TownLightVector cabinLamp { 70.85F, 1.605F, 70.888F };
		Check(TownPointLightVisibility(cabinLamp, { 71.2125F, 1.9675F, 71.43F }, cabinRoom) == 0
		        && TownPointLightVisibility(cabinLamp, { 71.46F, 2.215F, 71.6F }, cabinRoom) == 1,
			"measured cabin window blocks the stone-tunnel corner ray while preserving the clear center ray");
		bool malformedPolygonBlocked = true;
		for (const unsigned sides : { 1U, 2U, 33U, std::numeric_limits<unsigned>::max() }) {
			doorway[0].polygonSides = sides;
			malformedPolygonBlocked = malformedPolygonBlocked && TownPointLightVisibility(lamp, { 3, 1, 1 }, room) == 0;
		}
		Check(malformedPolygonBlocked, "malformed aperture polygon counts fail closed");
		doorway[0].polygonSides = 0;
		Check(TownPointLightVisibility(lamp, { 3, 2.5F, 1 }, room) == 0, "rays outside the doorway remain blocked by masonry");
		Check(TownPointLightVisibility({ -1, 1, 1 }, { 3, 1, 1 }, room) == 0, "outside-to-outside rays require openings at both wall crossings");
		Check(TownPointLightVisibility({ -1, 3, 1 }, { 3, 3, 1 }, room) == 1, "a parallel ray which misses a room is not falsely blocked");
		doorway[0].minU = 1.5F;
		doorway[0].maxU = 2.5F;
		Check(TownPointLightVisibility(lamp, { 3, 1, 3 }, room) == 0, "crossing a solid wall corner cannot leak through an opening on only one wall");
		room[0].apertures = {};
		std::array<TownPointLight, 1> points { TownPointLight { lamp, { 1, 0.7F, 0.08F }, 5, 2 } };
		const auto interior = SampleTownLighting({ -1, 0, 0 }, { 1.75F, 1, 1 }, 1, config, points, room);
		const auto exterior = SampleTownLighting({ -1, 0, 0 }, { 3, 1, 1 }, 0, config, points, room);
		Check(interior.point.red > interior.point.green && interior.point.green > interior.point.blue && Black(exterior.point),
			"warm interior point diffuse contributes inside while opaque walls prevent exterior spill");
		Check(Black(SampleTownLighting({ 1, 0, 0 }, { 2, 1, 1 }, 0, config, points, room).point),
			"surface contact does not illuminate the exterior wall's outward normal from an interior lamp");
		const auto pointWithoutDirectionalShadow = SampleTownLighting({ -1, 0, 0 }, { 1.75F, 1, 1 }, 0, config, points, room);
		Check(Same(interior.point, pointWithoutDirectionalShadow.point), "directional shadow does not incorrectly extinguish an independent point light");
		points[0].position = { 0, 0, 0 };
		const auto near = SampleTownLighting({ 1, 0, 0 }, { -1, 0, 0 }, 0, config, points);
		const auto far = SampleTownLighting({ 1, 0, 0 }, { -3, 0, 0 }, 0, config, points);
		Check(near.point.red > far.point.red && far.point.red > 0
				&& Black(SampleTownLighting({ 1, 0, 0 }, { -5, 0, 0 }, 0, config, points).point),
			"point attenuation decreases with world distance and reaches zero at its radius");
		const auto beforeTranslation = SampleTownLighting({ 1, 0, 0 }, { -1, 0, 0 }, 0, config, points);
		points[0].position = { 20, 7, -15 };
		Check(Same(beforeTranslation.total(), SampleTownLighting({ 1, 0, 0 }, { 19, 7, -15 }, 0, config, points).total()),
			"lighting depends on relative world-space geometry rather than camera or absolute location");
		std::array<TownPointLight, 9> tooManyPoints;
		for (auto &point : tooManyPoints)
			point.intensity = 0;
		tooManyPoints.back() = { { 0, 1, 0 }, { 1, 1, 1 }, 5, 2 };
		Check(Black(SampleTownLighting(upward, ground, 0, config, tooManyPoints).point), "point-light sampling has a fixed eight-light budget");
		std::array<TownLightOccluder, 33> tooManyRooms;
		Check(TownPointLightVisibility(lamp, ground, tooManyRooms) == 0, "excess occluder counts fail closed rather than bypassing walls");
		room[0].maximum = room[0].minimum;
		Check(TownPointLightVisibility(lamp, ground, room) == 0, "malformed room bounds fail closed");
		const auto invalid = SampleTownLighting({ std::numeric_limits<float>::quiet_NaN(), 1, 0 }, ground, 0, config, points);
		Check(Same(invalid.ambient, config.ambient) && Black(invalid.directional) && Black(invalid.point), "nonfinite surface data cannot create nonfinite light contributions");
		Check(TownSrgbToLinear(std::numeric_limits<float>::infinity()) == 0 && TownLinearToSrgb(-1) == 0, "invalid color inputs produce bounded encoded output");

		points[0] = { { 0, 2, 0 }, { 1, 0.7F, 0.08F }, 5, 2 };
		room[0] = { { -3, 0, -3 }, { 3, 4, 3 }, {} };
		for (bool pointEnabled : { false, true }) {
			const auto started = std::chrono::steady_clock::now();
			const std::size_t allocations = Allocations.load();
			volatile float checksum = 0;
			for (int i = 0; i < 500000; ++i) {
				const TownLightVector position { static_cast<float>(i % 64) / 16 - 2, 0.1F, static_cast<float>((i / 64) % 64) / 16 - 2 };
				const auto sample = pointEnabled ? SampleTownLighting(upward, position, 0.2F, config, points, room)
				                                : SampleTownLighting(upward, position, 0.2F, config);
				checksum = checksum + sample.total().red;
			}
			const std::size_t newAllocations = Allocations.load() - allocations;
			const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
			Check(newAllocations == 0 && std::isfinite(checksum), "repeated shader math performs zero C++ heap allocations");
			std::cout << "BENCH 500000 samples pointEnabled=" << pointEnabled << " milliseconds=" << milliseconds << " checksum=" << checksum << '\n';
		}
		std::cout << "PASS lighting contracts complete\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
