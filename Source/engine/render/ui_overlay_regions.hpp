#pragma once

#include <span>

#include "utils/sdl_geometry.h"

struct SDL_Surface;

namespace devilution {

struct Surface;

/** Conservative logical regions whose final pixels contain UI drawing. */
struct UiOverlayFrame {
	SDL_Surface *source = nullptr;
	SDL_Rect sourceRegion {};
	std::span<const SDL_Rect> regions;
	std::span<const SDL_Rect> cursorRegions {};
};

/** Start a new frame. Only writes to this underlying surface are recorded. */
void BeginUiOverlayRegions(const Surface &source);

/** Stop recording and discard the previous frame's regions. */
void ClearUiOverlayRegions();

/** Replace only the transient cursor regions, keeping the current UI frame. */
void BeginUiOverlayCursor();

/** Resume recording ordinary UI without discarding either channel. */
void EndUiOverlayCursor();

namespace detail {
extern bool UiOverlayRegionsActive;
void RecordUiOverlayRect(const Surface &out, int x, int y, int width, int height);
} // namespace detail

/**
 * Record a conservative rectangle in out's local coordinates, independent of
 * color or transparency. The inactive path performs only the recording guard.
 */
inline void MarkUiOverlayRect(const Surface &out, int x, int y, int width, int height)
{
	if (detail::UiOverlayRegionsActive)
		detail::RecordUiOverlayRect(out, x, y, width, height);
}

/** Regions use coordinates relative to sourceRegion; the span is borrowed. */
UiOverlayFrame GetUiOverlayFrame();

} // namespace devilution
