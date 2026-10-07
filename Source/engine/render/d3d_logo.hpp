#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#ifdef USE_SDL3
#include <SDL3/SDL_pixels.h>
#else
#include <SDL.h>
#endif

#include "engine/clx_sprite.hpp"
#include "engine/point.hpp"
#include "engine/surface.hpp"

namespace devilution {

constexpr uint32_t D3dLogoFrameCount = 240;
constexpr uint32_t D3dLogoCycleDurationMs = 8000;
constexpr uint8_t D3dLogoTransparentIndex = 250;
using D3dLogoPalette = std::array<SDL_Color, 256>;

enum class D3dLogoKind : uint8_t { Menu, Title, Pause };

struct D3dLogoSpec {
	const char *path;
	uint16_t width;
	uint16_t frameHeight;
};

/** Custom logos use an exact eight-second loop; the native UI timer is unchanged. */
[[nodiscard]] constexpr uint32_t D3dLogoFrameAt(uint32_t elapsedMs)
{
	return static_cast<uint32_t>(static_cast<uint64_t>(elapsedMs % D3dLogoCycleDurationMs) * D3dLogoFrameCount / D3dLogoCycleDurationMs);
}

[[nodiscard]] D3dLogoSpec GetD3dLogoSpec(D3dLogoKind kind);

class D3dLogo {
public:
	D3dLogo(OwnedClxSpriteList sprites, D3dLogoPalette palette);

	[[nodiscard]] ClxSpriteList sprites() const { return sprites_; }
	[[nodiscard]] const D3dLogoPalette &palette() const { return palette_; }

	/** Rebuilds only when target RGB values change, including level changes/cycling. */
	[[nodiscard]] const std::array<uint8_t, 256> &translation(const D3dLogoPalette &target) const;

private:
	OwnedClxSpriteList sprites_;
	D3dLogoPalette palette_;
	mutable D3dLogoPalette translatedPalette_ {};
	mutable std::array<uint8_t, 256> translation_ {};
	mutable bool translationValid_ = false;
};

/** Validated indexed PCX strip decoder. Invalid or incomplete assets return no logo. */
[[nodiscard]] std::optional<D3dLogo> DecodeD3dLogoPcx(std::span<const uint8_t> data, D3dLogoKind kind);

/** Uses the ordinary asset override search, including on unpacked-data builds. */
[[nodiscard]] std::optional<D3dLogo> LoadD3dLogo(D3dLogoKind kind);

/** Bottom-anchored draw with the current logical palette; index zero stays opaque. */
void DrawD3dLogo(const Surface &out, Point bottomPosition, const D3dLogo &logo, uint32_t frame);

} // namespace devilution
