#pragma once

#include "engine/rectangle.hpp"
#include "panels/ui_panels.hpp"

namespace devilution {

struct Surface;

extern int TotalSpMainPanelButtons;
extern int TotalMpMainPanelButtons;
extern int PanelPaddingHeight;
extern const char *const PanBtnStr[8];
extern const char *const PanBtnHotKey[8];
extern Rectangle SpellButtonRect;
extern Rectangle BeltRect;

void SetPanelObjectPosition(UiPanels panel, Rectangle &button);

// Draw one original translated button at the local origin, including its
// native pressed or multiplayer state. Layout and actions are handled separately.
void DrawNativePanelButton(const Surface &out, int button, bool pressed);

} // namespace devilution
