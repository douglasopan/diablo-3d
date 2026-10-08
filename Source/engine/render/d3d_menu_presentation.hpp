#pragma once

#include "utils/sdl_compat.h"

namespace devilution {
bool SetD3dMainMenuActive(bool active);
bool IsD3dMainMenuActive();
void SetD3dMainMenuFade(int value);
bool RenderD3dMainMenu(SDL_Renderer *renderer, SDL_Surface *indexedUi);
void ResetD3dMainMenuResources();
} // namespace devilution
