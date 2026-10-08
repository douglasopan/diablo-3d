#pragma once

#ifndef USE_SDL1
#include "utils/sdl_compat.h"

namespace devilution {

struct Surface;
struct UiOverlayFrame;

/**
 * Draw the retained 2x world and conservative, opaque UI regions over the
 * already drawn legacy frame. Coordinates and mouse input remain logical.
 * Returns false on unsupported input or SDL failure; caller redraws legacy.
 */
bool RenderTownPresentationLayers(SDL_Renderer *renderer, SDL_Surface *logicalOutput,
    const Surface &highResolutionWorld, const UiOverlayFrame &overlay,
    SDL_Palette *palette, bool linearFilter = true);

/** Must run before the owning renderer is destroyed or reconfigured. */
void ResetTownPresentationResources();

} // namespace devilution
#endif
