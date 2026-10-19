#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#ifdef USE_SDL3
#include <SDL3/SDL_pixels.h>
#else
#include <SDL.h>
#endif

namespace devilution {

/** Reduces four opaque indexed samples; index zero is an ordinary color. */
class TownViewColorResolve {
public:
	void Prepare(const std::array<SDL_Color, 256> &palette)
	{
		if (prepared_) {
			bool unchanged = true;
			for (size_t i = 0; i < palette.size(); ++i) {
				if (palette[i].r != palette_[i].r || palette[i].g != palette_[i].g || palette[i].b != palette_[i].b) {
					unchanged = false;
					break;
				}
			}
			if (unchanged)
				return;
		}
		palette_ = palette;
		for (size_t key = 0; key < nearest_.size(); ++key) {
			// The 32 grid levels include both 0 and 255. Encoding and decoding
			// round to nearest, keeping the lookup's quantization consistent.
			const int red = Expand(static_cast<unsigned>(key >> 10));
			const int green = Expand(static_cast<unsigned>((key >> 5) & 31));
			const int blue = Expand(static_cast<unsigned>(key & 31));
			int bestDistance = std::numeric_limits<int>::max();
			uint8_t best = 0;
			for (size_t i = 0; i < palette_.size(); ++i) {
				const int dr = red - palette_[i].r;
				const int dg = green - palette_[i].g;
				const int db = blue - palette_[i].b;
				const int distance = dr * dr + dg * dg + db * db;
				if (distance < bestDistance) {
					bestDistance = distance;
					best = static_cast<uint8_t>(i);
				}
			}
			nearest_[key] = best;
		}
		prepared_ = true;
	}

	uint8_t Resolve(const std::array<uint8_t, 4> &samples) const
	{
		// Preserve palette ramps and cycling indices exactly in flat areas.
		if ((samples[0] == samples[1] && samples[0] == samples[2] && samples[0] == samples[3]) || !prepared_)
			return samples[0];
		unsigned red = 0;
		unsigned green = 0;
		unsigned blue = 0;
		for (const uint8_t sample : samples) {
			red += palette_[sample].r;
			green += palette_[sample].g;
			blue += palette_[sample].b;
		}
		// Average RGB, never palette indices. Round the four-sample mean
		// before snapping to the 5-bit grid; ties use the lowest palette index.
		const unsigned key = (Quantize((red + 2) / 4) << 10)
		    | (Quantize((green + 2) / 4) << 5) | Quantize((blue + 2) / 4);
		return nearest_[key];
	}

private:
	static constexpr unsigned Quantize(unsigned color)
	{
		return (color * 31 + 127) / 255;
	}

	static constexpr int Expand(unsigned color)
	{
		return static_cast<int>((color * 255 + 15) / 31);
	}

	std::array<SDL_Color, 256> palette_ {};
	std::array<uint8_t, 32768> nearest_ {};
	bool prepared_ = false;
};

} // namespace devilution
