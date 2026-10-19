#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "engine/rectangle.hpp"
#include "utils/sdl_compat.h"

namespace devilution {

struct Surface;

enum class D3dHudSkinPart {
	Chassis,
	Button,
	Inset,
	OrbFrame,
	LifeLiquid,
	ManaLiquid,
	EmptyOrb,
	BeltTint,
	Count,
};

/** Unmodified author RGB, independent of renderer, palette and brightness. */
struct D3dHudSkinImage {
	int width = 0;
	int height = 0;
	std::vector<uint32_t> pixels;
};

struct D3dHudSkinSlice {
	SDL_Rect source;
	std::array<int, 4> sourceMargins; // left, top, right, bottom
	std::array<int, 4> logicalMargins;
};

struct D3dHudSkinFloatRect {
	float x, y, w, h;
};

struct D3dHudSkinOrbPlacement {
	D3dHudSkinFloatRect frame;
	float centerX, centerY, radius;
};

/** Required once before entering a game; an error never selects another skin. */
std::expected<void, std::string> LoadD3dHudSkin();
void FreeD3dHudSkin();
bool IsD3dHudSkinLoaded();
/** Immutable, already validated bytes, staged with the complete selected theme. */
std::span<const uint8_t> GetD3dHudThemeFontBytes(std::string_view nativeResource);
const D3dHudSkinImage &GetD3dHudSkinImage(D3dHudSkinPart part);
const D3dHudSkinSlice &GetD3dHudSkinSlice(D3dHudSkinPart part);
SDL_Rect GetD3dHudSkinOrbFrameSource();
D3dHudSkinOrbPlacement PlaceD3dHudSkinOrb(Rectangle target, bool mana);

/** Pure pixel painters: no state updates, command handlers or renderer calls. */
void DrawD3dHudSkinIndexedPlate(const Surface &out, Rectangle rect, bool pressed = false,
    D3dHudSkinPart part = D3dHudSkinPart::Inset, float designScaleX = 0, float designScaleY = 0);
void DrawD3dHudSkinIndexedPlateShade(const Surface &out, Rectangle rect, D3dHudSkinPart part, uint8_t shade,
    float designScaleX = 0, float designScaleY = 0);
void DrawD3dHudSkinIndexedFrame(const Surface &out, Rectangle rect, bool selected = false);
/** Neutral gray at 24/255 opacity, behind occupied inventory slots only. */
void DrawD3dHudSkinIndexedItemBackdrop(const Surface &out, Rectangle rect);
void DrawD3dHudSkinIndexedOrb(const Surface &out, Rectangle rect, bool mana, int fill);
void DrawD3dHudSkinIndexedTint(const Surface &out, Rectangle rect, uint8_t paletteIndex);

struct D3dHudSkinCounters {
	uint64_t pngDecodes = 0;
	uint64_t indexedPlates = 0;
	uint64_t indexedOrbs = 0;
	uint64_t indexedTints = 0;
	uint64_t indexedPixels = 0;
};

/** Diagnostic counters measure work, not FPS; normal drawing never resets them. */
D3dHudSkinCounters GetD3dHudSkinCounters();
void ResetD3dHudSkinCounters();

} // namespace devilution
