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
	/** A rendering command failed; recover R1 pixels and upload the full frame. */
	Failed,
};

/** Capture the clean logical world after DrawGame and clear this frame's HUD. */
void BeginD3dHudPresentationFrame(const Surface &base);

/** Recognized source stays true after early recovery: never paint skin twice. */
bool IsD3dHudPresentationCapturing(const Surface &source);
bool HasD3dHudPresentationDeferredFrame();

/** Seal HUD foreground before the late native UI phase; no callbacks replay. */
void SealD3dHudPresentationForeground(const Surface &source);
void FinalizeD3dHudPresentationFrame(const Surface &source);

/** Pure cursor-buffer maintenance supplied by the engine, never DrawCursor. */
void SetD3dHudRecoveryCursorCallbacks(void (*before)(const Surface &), void (*after)(const Surface &));
bool IsD3dHudPresentationRecoveryPixel(Point position);
void BlitD3dHudPresentationCapturedCursor(const Surface &source);

/** Paint only frozen R1 decoration/foreground, once, beneath the next UI write. */
bool RecoverD3dHudPresentationFrame(const Surface &source);

/** All uploads precede the first optional draw. False requires recovery first. */
bool PrepareD3dHudPresentation(SDL_Renderer *renderer, SDL_Surface *indexedUi,
    SDL_Palette *palette, bool allowTownLayers, bool townLayersAvailable = false);

/**
 * Compose native foreground in logical screen coordinates. Only key is
 * transparent; index 0 stays opaque. Source is never registered as UI coverage.
 */
void RecordD3dHudForeground(const Surface &source, Rectangle target, uint8_t key = 1);
/** Authored ARGB foreground, layered over its indexed recovery proxy. */
void RecordD3dHudRgbaForeground(int width, int height, std::span<const uint32_t> pixels, Rectangle target);

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
 * Borrowed regions are checked immediately, never retained. Native writers
 * that overlap the frozen HUD materialize R1 before their first pixel write.
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
 * Call in the ordinary UI channel, BEFORE saving/drawing the native cursor.
 * The engine enters its cursor channel only after this capture returns.
 * This captures a transparent
 * scratch; rejection materializes R1 before the native behindBuffer is saved.
 */
void CaptureD3dHudPresentationCursor(Rectangle clippedBounds, Point spriteBottomLeft, int cursorId);

/**
 * Call in the ordinary UI channel, BEFORE saving/drawing the native cursor.
 * The engine enters its cursor channel only after this capture returns.
 * Pixels and opacity are local,
 * row-major and exactly width*height bytes each; zero opacity is transparent.
 * The explicit mask keeps every covered palette index opaque, including 1.
 * On invalid input or allocation failure, discard the optional snapshot and
 * materialize indexed R1 immediately, without invoking cursor callbacks.
 */
void CaptureD3dHudPresentationCursorPixels(Rectangle clippedBounds,
    std::span<const uint8_t> pixels, std::span<const uint8_t> opacity);

/**
 * Call after the existing native/Town presentation. townLayersPresented means
 * the retained 2x world was successfully uploaded and presented in THIS call.
 * Prepare before the first ordinary output upload. Any deferred frame that
 * cannot use HD must be recovered to indexed R1 before it is presented. Failed
 * means an optional draw may already have submitted commands: recover and
 * reupload/repaint the full ordinary frame to cover that partial result.
 */
D3dHudPresentationResult RenderD3dHudPresentation(SDL_Renderer *renderer,
    SDL_Surface *indexedUi, SDL_Palette *palette, bool allowTownLayers,
    bool townLayersPresented = false);

/** Discard a cleared/transitional frame while retaining allocated resources. */
void InvalidateD3dHudPresentationFrame();

/** Run before renderer destruction/reconfiguration; also invalidates captures. */
void ResetD3dHudPresentationResources();

enum class D3dHudPresentationFault {
	None,
	BeforeTextureAllocation,
	BeforeUpload,
	AfterUpload,
	BeforeDraw,
	AfterDraw,
	BeforeFirstSubmit,
	AfterFirstSubmit,
	BeforeCursorSurfaceAllocation,
	BeforeCursorMaskAllocation,
};

struct D3dHudPresentationCounters {
	uint64_t frames = 0;
	uint64_t preparations = 0;
	uint64_t recoveries = 0;
	uint64_t textureAllocationAttempts = 0;
	uint64_t textureCreations = 0;
	uint64_t textureAllocationSuccesses = 0;
	uint64_t uploadAttempts = 0;
	uint64_t uploads = 0;
	uint64_t drawAttempts = 0;
	uint64_t draws = 0;
	uint64_t retainedWorldRestores = 0;
	/** First own draw or retained-world restore group; not GPU completion. */
	uint64_t firstSubmissionAttempts = 0;
	uint64_t firstSubmissionSuccesses = 0;
	uint64_t cursorCaptureAttempts = 0;
	uint64_t cursorSurfaceAllocationAttempts = 0;
	uint64_t cursorMaskAllocationAttempts = 0;
	uint64_t cursorCaptureRejections = 0;
	uint64_t injectedFailures = 0;
};

/** One-shot faults; ordinal selects allocation, upload or draw attempts. */
void SetD3dHudPresentationFault(D3dHudPresentationFault fault, uint64_t ordinal = 1);
D3dHudPresentationCounters GetD3dHudPresentationCounters();
void ResetD3dHudPresentationCounters();

} // namespace devilution
