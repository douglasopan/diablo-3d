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

/** Current native held state, without modifying input or button dispatch. */
bool IsMainPanelButtonPressed(int button);

// Draw the localized label or preserved multiplayer state at the local origin.
// Legacy decoration is used only when the D3D skin is not selected.
void DrawNativePanelButton(const Surface &out, int button, bool pressed);

} // namespace devilution
