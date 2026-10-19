#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace devilution::cathedral {

struct NativeCoveragePoint { float x, y; };

/** Half-open coverage for non-idempotent palette overlays, on a 1/256 subpixel
 * grid. Shared edges have opposite integer values, including clipped fans.
 * Legacy opaque CPU coverage is unchanged. GPU parity remains a host gate.
 */
inline bool NativePaletteTriangleCovers(const std::array<NativeCoveragePoint, 3> &vertices,
    NativeCoveragePoint sample) noexcept
{
	struct Point { int64_t x, y; };
	const auto convert = [](NativeCoveragePoint input, Point &output) {
		if (!std::isfinite(input.x) || !std::isfinite(input.y)
		    || std::abs(input.x) > 1000000 || std::abs(input.y) > 1000000)
			return false;
		output = { std::llround(static_cast<double>(input.x) * 256), std::llround(static_cast<double>(input.y) * 256) };
		return true;
	};
	std::array<Point, 3> points {};
	Point pixel {};
	if (!convert(sample, pixel))
		return false;
	for (size_t i = 0; i < points.size(); ++i)
		if (!convert(vertices[i], points[i]))
			return false;
	const auto edge = [](Point a, Point b, Point p) {
		return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
	};
	const int64_t area = edge(points[0], points[1], points[2]);
	if (area == 0)
		return false;
	for (size_t i = 0; i < points.size(); ++i) {
		Point a = points[i], b = points[(i + 1) % points.size()];
		if (area < 0) {
			const Point swap = a;
			a = b;
			b = swap;
		}
		const int64_t value = edge(a, b, pixel);
		const bool topLeft = b.y > a.y || (b.y == a.y && b.x < a.x);
		if (value < 0 || (value == 0 && !topLeft))
			return false;
	}
	return true;
}

} // namespace devilution::cathedral
