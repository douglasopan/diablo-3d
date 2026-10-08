#pragma once

#include <cstdint>
#include <span>

#include "engine/rectangle.hpp"
#include "utils/sdl_compat.h"

namespace devilution {

struct Surface;

enum class D3dHudPresentationResult {
	/** No rendering commands were issued; retain the existing presentation. */
	Inactive,
	Presented,
	/** A rendering command failed; redraw the complete native frame. */
	Failed,
};

/** Capture the clean logical world after DrawGame and clear this frame's HUD. */
void BeginD3dHudPresentationFrame(const Surface &base);

/**
 * Compose native foreground in logical screen coordinates. Only key is
 * transparent; index 0 stays opaque. Source is never registered as UI coverage.
 */
void RecordD3dHudForeground(const Surface &source, Rectangle target, uint8_t key = 1);

/** Explicitly replace a previously recorded contribution, such as InfoBox text. */
void ClearD3dHudForeground(Rectangle target);

/** Capture the private native button state without changing any handler. */
void RecordD3dHudButtonState(int button, bool pressed);

/** Native quality translation supplies the palette family for an occupied slot. */
void RecordD3dHudBeltTint(int slot, uint8_t paletteIndex);

/** Reuse the native XP renderer in the transparent presentation plane. */
void RecordD3dHudExperienceBar();

/** Values follow the actual uniformly fitted author-art aperture, never input. */
Rectangle GetD3dHudPresentationValueRect(bool mana);

/**
 * Required once per frame, after recording only non-HUD native UI coverage.
 * Borrowed regions are checked immediately, never retained. Any overlap with
 * the HUD footprints declines the optional skin for this frame.
 */
void SetD3dHudPresentationUiCoverage(std::span<const SDL_Rect> nonHudRegions);

/** Engine-owned semantic phase: ordinary UI is captured, HUD/cursor are not. */
void SetD3dHudPresentationNativeUiCapture(bool active);

namespace detail {
/** Called by the common UI recorder after source validation and clipping. */
void RecordD3dHudPresentationNativeUiRect(const SDL_Rect &region);
} // namespace detail

/** Call at the start of every DrawCursor, including hardware/hidden branches. */
void ClearD3dHudPresentationCursor();

/**
 * The engine supplies its actual clipped bounds and original sprite position.
 * This draws only a transparent scratch, never the fallback's cursor buffers.
 */
void CaptureD3dHudPresentationCursor(Rectangle clippedBounds, Point spriteBottomLeft, int cursorId);

/**
 * Call after the existing native/Town presentation. townLayersPresented means
 * the retained 2x world was successfully uploaded and presented in THIS call.
 * Fade/other RenderPresent(false) calls conservatively retain the native path.
 * All resource preparation precedes drawing. Only Failed requires repainting
 * the complete native frame; Inactive must retain the already presented Town.
 */
D3dHudPresentationResult RenderD3dHudPresentation(SDL_Renderer *renderer,
    SDL_Surface *indexedUi, SDL_Palette *palette, bool allowTownLayers,
    bool townLayersPresented = false);

/** Discard a cleared/transitional frame while retaining allocated resources. */
void InvalidateD3dHudPresentationFrame();

/** Run before renderer destruction/reconfiguration; also invalidates captures. */
void ResetD3dHudPresentationResources();

} // namespace devilution
