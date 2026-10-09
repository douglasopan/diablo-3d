#pragma once

#include <cstdint>

#ifdef USE_SDL3
#include <SDL3/SDL_keycode.h>
#else
#include <SDL.h>

#ifdef USE_SDL1
#include "utils/sdl2_to_1_2_backports.h"
#endif
#endif

namespace devilution {

enum class KeymapperContext : uint8_t { Native, TownMovement };

bool KeymapperPress(SDL_Keycode key, KeymapperContext context = KeymapperContext::Native);
void KeymapperRelease(SDL_Keycode key, KeymapperContext context = KeymapperContext::Native);

} // namespace devilution
