#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "engine/clx_sprite.hpp"
#include "utils/sdl_compat.h"

namespace devilution {

// Empty is an uncovered resource. A covered but malformed resource is an error:
// callers must never disguise a damaged replacement by loading the old art.
std::expected<OptionalOwnedClxSpriteListOrSheet, std::string> LoadHdSpriteReplacement(
    std::string_view path, SDL_Color *outPalette = nullptr);

struct HdSpritePixels {
	int width = 0, height = 0, scale = 1;
	std::vector<uint8_t> pixels, opacity;
};

std::expected<HdSpritePixels, std::string> LoadHdSpritePixels(ClxSprite sprite);
/** Optional authored ARGB pixels for RGB UI composition; no palette conversion. */
std::expected<std::vector<uint32_t>, std::string> LoadHdSpriteRgba(ClxSprite sprite);

struct HdSpritePackStatus {
	bool mounted = false;
	size_t indexEntries = 0, replacementsLoaded = 0, uncoveredRequests = 0;
	size_t bytesRead = 0, frameReads = 0;
	std::string failure;
};
HdSpritePackStatus GetHdSpritePackStatus();
void ResetHdSpritePack();

} // namespace devilution
