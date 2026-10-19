#pragma once

#include <cstdint>
#include <span>

#include "engine/point.hpp"
#include "utils/sdl_compat.h"

namespace devilution {
class ClxSprite;
struct HeroPortrait;
struct Surface;

bool SetD3dMainMenuActive(bool active);
bool IsD3dMainMenuActive();
void SetD3dMainMenuFade(int value);
bool RenderD3dMainMenu(SDL_Renderer *renderer, SDL_Surface *indexedUi);
void ResetD3dMainMenuResources();

// One portrait contribution per UI frame, bounded to 216x100. The indexed
// fallback must already be complete before Record is called. Later native
// sprites supply their CLX coverage even when they write the same index.
void BeginD3dMainMenuUiFrame();
void RecordD3dMainMenuSpriteRgba(const Surface &out, ClxSprite sprite, Point bottomLeft);
bool CompositeD3dMainMenuSpriteRgba(SDL_Surface *surface, std::span<uint32_t> rgba, int fade);
void RecordD3dMainMenuPortrait(const Surface &out, const HeroPortrait &portrait, const SDL_Rect &rect);
void MarkD3dMainMenuNativeSprite(const Surface &out, ClxSprite sprite, Point topLeft);
void RejectD3dMainMenuPortraitOverlap(const Surface &out, const SDL_Rect &rect);
// CPU-only composition seam used by the real presentation path and its fixture.
// Invalid surfaces are untouched. False also reports declined portrait RGB;
// explicit native index-0 coverage is still preserved in that case.
bool CompositeD3dMainMenuPortrait(SDL_Surface *indexedUi, std::span<uint32_t> rgba, int fade);

struct D3dMainMenuPortraitFrameInfo {
	SDL_Rect rect {};
	bool recorded = false;
	bool rejected = false;
};
D3dMainMenuPortraitFrameInfo GetD3dMainMenuPortraitFrameInfo();

} // namespace devilution
