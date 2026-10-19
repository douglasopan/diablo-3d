#pragma once

#include <expected>
#include <string>

#include "engine/clx_sprite.hpp"

namespace devilution {

struct Surface;

extern OptionalOwnedClxSpriteList PanelButtonDown;
extern OptionalOwnedClxSpriteList TalkButton;

std::expected<void, std::string> LoadMainPanel();
void FreeMainPanel();

/** Draw only the localized label and shadow for one of the six main buttons. */
void DrawMainPanelButtonLabel(const Surface &out, int button, bool pressed);

} // namespace devilution
