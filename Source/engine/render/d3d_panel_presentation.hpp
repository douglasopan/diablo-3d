#pragma once

#include "engine/rectangle.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/render/d3d_hud_presentation.hpp"
#include "utils/sdl_compat.h"

namespace devilution {

struct Surface;

/** One clean world snapshot, before the first ordinary UI write. */
void BeginD3dPanelPresentationFrame(const Surface &world);
/** Begin a native content scratch. Index 1 is transparent; index 0 is opaque.
 * Compatibility HUD modes paint the same R1 art immediately, without scratch.
 */
void DrawD3dPanelBackplate(const Surface &out, Rectangle bounds, Rectangle destination = {});
/** Record only an open panel's item artwork; native pixels remain recovery data. */
void RecordD3dPanelItemRgba(const Surface &out, Point position, ClxSprite sprite);
/** Close this panel after its native contents, including every early return. */
void SealD3dPanelBackplate(const Surface &out);
/** Close all panels before the late UI/cursor phase. */
void SealD3dPanelPresentationFrame(const Surface &out);
/** After the existing DrawCursor, without running cursor input logic again. */
void FinalizeD3dPanelPresentationFrame(const Surface &out);
void ClearD3dPanelPresentationCursor();
void CaptureD3dPanelPresentationCursor(Rectangle bounds, Point spriteBottomLeft, int cursorId);
void CaptureD3dPanelPresentationCursorPixels(Rectangle bounds, std::span<const uint8_t> pixels, std::span<const uint8_t> opacity);
bool HasD3dPanelPresentationDeferredFrame();
/** Before-write recovery; replays only frozen pixels, never native handlers. */
bool RecoverD3dPanelPresentationFrame(const Surface &out);
void SetD3dPanelPresentationNativeUiCapture(bool active);
void SetD3dPanelRecoveryCursorCallbacks(void (*before)(const Surface &), void (*after)(const Surface &));
bool IsD3dPanelPresentationRecoveryPixel(Point point);
/** Restricts the shared cursor seam to this pass while only panels recover. */
bool IsD3dPanelPresentationRecovering();
namespace detail {
/** Source/clipping validation is supplied by the existing common recorder. */
void RecordD3dPanelNativeUiRect(const SDL_Rect &region);
}
/** All allocations/uploads precede drawing. Decline requires CPU recovery. */
bool PrepareD3dPanelPresentation(SDL_Renderer *renderer, SDL_Surface *source,
    SDL_Palette *palette, bool allow, bool townLayersAvailable = false);
/** After Town and HUD, before SDL_RenderPresent. Failed requires full repaint. */
D3dHudPresentationResult RenderD3dPanelPresentation(SDL_Renderer *renderer, SDL_Surface *source,
    SDL_Palette *palette, bool allow, bool townLayersPresented = false);
void InvalidateD3dPanelPresentationFrame();
void ResetD3dPanelPresentationResources();

enum class D3dPanelPresentationFault { None, BeforeTextureAllocation, BeforeUpload, BeforeDraw, AfterWorldRestore };
struct D3dPanelPresentationCounters {
	uint64_t rgbItemRecords = 0, rgbItemPixels = 0;
	uint64_t frames = 0, recipes = 0, recoveries = 0, outsideContentDeclines = 0;
	uint64_t compatibilityDeclines = 0, recorderDeclines = 0;
	uint64_t preparations = 0, allocationAttempts = 0, uploadAttempts = 0, drawAttempts = 0, injectedFailures = 0;
};
void SetD3dPanelPresentationFault(D3dPanelPresentationFault fault, uint64_t ordinal = 1);
D3dPanelPresentationCounters GetD3dPanelPresentationCounters();
void ResetD3dPanelPresentationCounters();

} // namespace devilution
