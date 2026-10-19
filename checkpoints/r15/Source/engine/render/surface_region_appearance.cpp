#include "engine/render/surface_region_appearance.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "levels/surface_region_geometry.hpp"

namespace devilution {
namespace {
constexpr std::array<double, 3> Soil { .045, .035, .024 };
constexpr std::array<double, 3> LeafLitter { .065, .046, .026 };
constexpr std::array<double, 3> DryGrass { .085, .074, .042 };
constexpr std::array<double, 3> Ash { .070, .067, .059 };

double Clamp(double x) { return std::clamp(x, 0.0, 1.0); }
double Smootherstep(double x)
{
	const double t = Clamp(x);
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}
} // namespace

std::optional<std::array<float, 3>> SampleSurfaceRegionGroundAppearance(
    double worldNativeX, double worldNativeZ) noexcept
{
	if (!std::isfinite(worldNativeX) || !std::isfinite(worldNativeZ)
	    || std::abs(worldNativeX) > surface::MaximumSurfaceWorldNative
	    || std::abs(worldNativeZ) > surface::MaximumSurfaceWorldNative)
		return std::nullopt;
	const double u = (worldNativeX - surface::RegionalOrigin) * surface::CellMetres;
	const double v = (worldNativeZ - surface::RegionalOrigin) * surface::CellMetres;
	// Literal port of ground_color_native from the pinned appearance-r2 author.
	// Its y argument is unused: this is color only, never another height field.
	const double dry = Clamp(.5 + .25 * std::sin(u / 137 + v / 199 + .8)
	    + .15 * std::sin(u / 61 - v / 103));
	const double signal = std::sin(u / 238 + .3) * std::sin(v / 297 + 1.4)
	    + .5 * std::sin(u / 91 + v / 173);
	const double forest = Clamp((signal + .2) / 1.5);
	const double cover = .62 * Clamp((dry - .35) / .60);
	const double ash = .36 * Clamp((.5 + .5 * std::sin(u / 41 - v / 67 + 1.9) - .82) / .18);
	const double distance = std::hypot(std::max(std::abs(u) - 84.564, 0.0),
	    std::max(std::abs(v) - 84.564, 0.0));
	const double collar = Smootherstep(distance / 18);
	const double grain = .93 + .07 * std::sin(u / 13.1 + v / 19.7);
	std::array<float, 3> result;
	for (std::size_t i = 0; i < result.size(); ++i) {
		double c = Soil[i] * (1 - .40 * forest) + LeafLitter[i] * (.40 * forest);
		c = c * (1 - cover) + DryGrass[i] * cover;
		c = c * (1 - ash) + Ash[i] * ash;
		c = Soil[i] * (1 - collar) + c * collar;
		result[i] = static_cast<float>(Clamp(c * grain));
	}
	return result;
}

} // namespace devilution
