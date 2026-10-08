#include "engine/render/ui_overlay_regions.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "engine/surface.hpp"

namespace devilution {
namespace {

constexpr size_t MaxOverlayRegions = 4096;

SDL_Surface *FrameSource = nullptr;
SDL_Rect FrameRegion {};
std::vector<SDL_Rect> Regions;
std::vector<SDL_Rect> CursorRegions;
bool FullFrame = false;
bool FullCursor = false;
bool RecordingCursor = false;

bool Contains(const SDL_Rect &outer, const SDL_Rect &inner)
{
	return outer.x <= inner.x && outer.y <= inner.y
	    && outer.x + outer.w >= inner.x + inner.w
	    && outer.y + outer.h >= inner.y + inner.h;
}

} // namespace

namespace detail {

bool UiOverlayRegionsActive = false;

void RecordUiOverlayRect(const Surface &out, int x, int y, int width, int height)
{
	auto &regions = RecordingCursor ? CursorRegions : Regions;
	bool &full = RecordingCursor ? FullCursor : FullFrame;
	if (out.surface != FrameSource || full || width <= 0 || height <= 0)
		return;

	// Clip in local coordinates before converting subregions to the frame's
	// logical origin. Wide intermediates also handle hostile rectangle inputs.
	const int64_t localLeft = std::max<int64_t>(x, 0);
	const int64_t localTop = std::max<int64_t>(y, 0);
	const int64_t localRight = std::min<int64_t>(static_cast<int64_t>(x) + width, out.w());
	const int64_t localBottom = std::min<int64_t>(static_cast<int64_t>(y) + height, out.h());
	const int64_t left = std::max<int64_t>(out.region.x + localLeft, FrameRegion.x);
	const int64_t top = std::max<int64_t>(out.region.y + localTop, FrameRegion.y);
	const int64_t right = std::min<int64_t>(out.region.x + localRight, static_cast<int64_t>(FrameRegion.x) + FrameRegion.w);
	const int64_t bottom = std::min<int64_t>(out.region.y + localBottom, static_cast<int64_t>(FrameRegion.y) + FrameRegion.h);
	if (localRight <= localLeft || localBottom <= localTop || right <= left || bottom <= top)
		return;

	const SDL_Rect rect = MakeSdlRect(
	    static_cast<int>(left - FrameRegion.x), static_cast<int>(top - FrameRegion.y),
	    static_cast<int>(right - left), static_cast<int>(bottom - top));
	if (!regions.empty()) {
		SDL_Rect &last = regions.back();
		if (Contains(last, rect))
			return;
		if (Contains(rect, last)) {
			last = rect;
			return;
		}
		// Coalesce consecutive primitive rows/columns without adding coverage.
		if (last.x == rect.x && last.w == rect.w
		    && last.y <= rect.y + rect.h && rect.y <= last.y + last.h) {
			const int bottomEdge = std::max(last.y + last.h, rect.y + rect.h);
			last.y = std::min(last.y, rect.y);
			last.h = bottomEdge - last.y;
			return;
		}
		if (last.y == rect.y && last.h == rect.h
		    && last.x <= rect.x + rect.w && rect.x <= last.x + last.w) {
			const int rightEdge = std::max(last.x + last.w, rect.x + rect.w);
			last.x = std::min(last.x, rect.x);
			last.w = rightEdge - last.x;
			return;
		}
	}
	if (regions.size() >= MaxOverlayRegions) {
		regions.clear();
		regions.push_back(MakeSdlRect(0, 0, FrameRegion.w, FrameRegion.h));
		full = true;
		return;
	}
	regions.push_back(rect);
}

} // namespace detail

void BeginUiOverlayRegions(const Surface &source)
{
	ClearUiOverlayRegions();
	if (source.surface == nullptr || source.w() <= 0 || source.h() <= 0)
		return;
	const int64_t left = std::max<int64_t>(source.region.x, 0);
	const int64_t top = std::max<int64_t>(source.region.y, 0);
	const int64_t right = std::min<int64_t>(static_cast<int64_t>(source.region.x) + source.w(), source.surface->w);
	const int64_t bottom = std::min<int64_t>(static_cast<int64_t>(source.region.y) + source.h(), source.surface->h);
	if (right <= left || bottom <= top)
		return;
	FrameSource = source.surface;
	FrameRegion = MakeSdlRect(static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left), static_cast<int>(bottom - top));
	detail::UiOverlayRegionsActive = true;
}

void ClearUiOverlayRegions()
{
	detail::UiOverlayRegionsActive = false;
	FrameSource = nullptr;
	FrameRegion = {};
	Regions.clear();
	CursorRegions.clear();
	FullFrame = false;
	FullCursor = false;
	RecordingCursor = false;
}

void BeginUiOverlayCursor()
{
	CursorRegions.clear();
	FullCursor = false;
	RecordingCursor = detail::UiOverlayRegionsActive;
}

void EndUiOverlayCursor()
{
	RecordingCursor = false;
}

UiOverlayFrame GetUiOverlayFrame()
{
	return { FrameSource, FrameRegion, Regions, CursorRegions };
}

} // namespace devilution
