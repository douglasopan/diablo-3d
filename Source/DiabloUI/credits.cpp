#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/credits_lines.h"
#include "DiabloUI/diabloui.h"
#include "DiabloUI/support_lines.h"
#include "DiabloUI/ui_flags.hpp"
#include "controls/input.h"
#include "controls/menu_controls.h"
#include "engine/load_clx.hpp"
#include "engine/point.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/d3d_menu_presentation.hpp"
#include "engine/render/text_render.hpp"
#include "engine/surface.hpp"
#include "game_mode.hpp"
#include "hwcursor.hpp"
#include "utils/display.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/sdl_geometry.h"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

const SDL_Rect VIEWPORT = { 0, 114, 640, 251 };
const int LINE_H = 22;

// The maximum number of visible lines is the number of whole lines
// (VIEWPORT.h / LINE_H) rounded up, plus one extra line for when
// a line is leaving the screen while another one is entering.
#define MAX_VISIBLE_LINES ((VIEWPORT.h - 1) / LINE_H + 2)

constexpr std::array<const char *, 3> ProjectLinks {
	"https://github.com/douglasopan/diablo-3d",
	"https://douglasopan.github.io/diablo-3d/",
	"https://discord.gg/4YxQ7s69S",
};
constexpr int ProjectCreditsBack = ProjectLinks.size();
int ProjectCreditsAction = -1;

void ProjectCreditsSelect(size_t value)
{
	ProjectCreditsAction = static_cast<int>(value);
}

void ProjectCreditsEsc()
{
	ProjectCreditsAction = ProjectCreditsBack;
}

bool OpenProjectLink(const char *url)
{
#ifdef USE_SDL3
	return SDL_OpenURL(url);
#elif SDL_VERSION_ATLEAST(2, 0, 14)
	return SDL_OpenURL(url) == 0;
#else
	// The address stays visible on platforms without an URL-opening API.
	(void)url;
	return false;
#endif
}

class CreditsRenderer {

public:
	CreditsRenderer(const char *const *text, std::size_t textLines)
	{
		for (size_t i = 0; i < textLines; i++) {
			const std::string_view orgText = _(text[i]);

			uint16_t offset = 0;
			size_t indexFirstNotTab = 0;
			while (indexFirstNotTab < orgText.size() && orgText[indexFirstNotTab] == '\t') {
				offset += 40;
				indexFirstNotTab++;
			}

			const std::string paragraphs = WordWrapString(orgText.substr(indexFirstNotTab), 580 - offset, FontSizeDialog);

			size_t previous = 0;
			while (true) {
				const size_t next = paragraphs.find('\n', previous);
				linesToRender.emplace_back(LineContent { offset, paragraphs.substr(previous, next - previous) });
				if (next == std::string::npos)
					break;
				previous = next + 1;
			}
		}

		ticks_begin_ = SDL_GetTicks();
		prev_offset_y_ = 0;
		finished_ = false;
	}

	~CreditsRenderer()
	{
		ArtBackgroundWidescreen = std::nullopt;
		ArtBackground = std::nullopt;
	}

	void Render();

	[[nodiscard]] bool Finished() const
	{
		return finished_;
	}

private:
	struct LineContent {
		uint16_t offset;
		std::string text;
	};

	std::vector<LineContent> linesToRender;
	bool finished_;
	Uint32 ticks_begin_;
	int prev_offset_y_;
};

void CreditsRenderer::Render()
{
	const int offsetY = -VIEWPORT.h + ((SDL_GetTicks() - ticks_begin_) / 40);
	if (offsetY == prev_offset_y_)
		return;
	prev_offset_y_ = offsetY;

	SDL_FillSurfaceRect(DiabloUiSurface(), nullptr, 0);
	const Point uiPosition = GetUIRectangle().position;
	if (ArtBackgroundWidescreen)
		RenderClxSprite(Surface(DiabloUiSurface()), (*ArtBackgroundWidescreen)[0], uiPosition - Displacement { 320, 0 });
	if (ArtBackground)
		RenderClxSprite(Surface(DiabloUiSurface()), (*ArtBackground)[0], uiPosition);

	const std::size_t linesBegin = std::max(offsetY / LINE_H, 0);
	const std::size_t linesEnd = std::min(linesBegin + MAX_VISIBLE_LINES, linesToRender.size());

	if (linesBegin >= linesEnd) {
		if (linesEnd == linesToRender.size())
			finished_ = true;
		return;
	}

	SDL_Rect viewport = VIEWPORT;
	viewport.x += uiPosition.x;
	viewport.y += uiPosition.y;
	ScaleOutputRect(&viewport);

	// We use unscaled coordinates for calculation throughout.
	auto destY = static_cast<Sint16>(uiPosition.y + VIEWPORT.y - (offsetY - linesBegin * LINE_H));
	for (std::size_t i = linesBegin; i < linesEnd; ++i, destY += LINE_H) {
		const Sint16 destX = uiPosition.x + VIEWPORT.x + 31;

		auto &lineContent = linesToRender[i];

		SDL_Rect dstRect = MakeSdlRect(destX + lineContent.offset, destY, 0, 0);
		ScaleOutputRect(&dstRect);
		dstRect.x -= viewport.x;
		dstRect.y -= viewport.y;

		const Surface &out = Surface(DiabloUiSurface(), viewport);
		DrawString(out, lineContent.text, Point { dstRect.x, dstRect.y },
		    { .flags = UiFlags::FontSizeDialog | UiFlags::ColorDialogWhite, .spacing = -1 });
	}
}

bool TextDialog(const char *const *text, std::size_t textLines)
{
	CreditsRenderer creditsRenderer(text, textLines);
	bool endMenu = false;

	if (IsHardwareCursor())
		SetHardwareCursorVisible(false);

	SDL_Event event;
	do {
		creditsRenderer.Render();
		UiFadeIn();
		while (PollEvent(&event)) {
			switch (event.type) {
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				endMenu = true;
				break;
			default:
				for (const MenuAction menuAction : GetMenuActions(event)) {
					if (IsNoneOf(menuAction, MenuAction_BACK, MenuAction_SELECT))
						continue;
					endMenu = true;
					break;
				}
				break;
			}
			UiHandleEvents(&event);
		}
	} while (!endMenu && !creditsRenderer.Finished());

	return true;
}

} // namespace

bool UiCreditsDialog()
{
	ArtBackgroundWidescreen = LoadOptionalClx("ui_art\\creditsw.clx");
	LoadBackgroundArt("ui_art\\credits");

	return TextDialog(CreditLines, CreditLinesSize);
}

bool UiSupportDialog()
{
	ArtBackgroundWidescreen = LoadOptionalClx("ui_art\\supportw.clx");
	if (ArtBackgroundWidescreen.has_value()) {
		UiLoadMenuBackground("ui_art\\support");
	} else {
		ArtBackgroundWidescreen = LoadOptionalClx("ui_art\\creditsw.clx");
		UiLoadMenuBackground("ui_art\\credits");
	}

	return TextDialog(SupportLines, SupportLinesSize);
}

void UiDiablo3DCreditsDialog()
{
	std::size_t selectedItem = 0;
	bool linkFailed = false;
	while (true) {
		ProjectCreditsAction = -1;
		std::vector<std::unique_ptr<UiItemBase>> dialog;
		std::vector<std::unique_ptr<UiListItem>> links;
		UiLoadMenuBackground(!gbIsSpawn || gbIsHellfire ? "ui_art\\mainmenu" : "ui_art\\swmmenu");
		UiAddBackground(&dialog);

		const int width = std::min(720, gnScreenWidth - 64);
		const int x = (gnScreenWidth - width) / 2;
		constexpr int LineHeight = 20;
		constexpr int RowHeight = 32;
		constexpr int ParagraphGap = 6;
		const std::array<std::string, 4> paragraphs {
			WordWrapString(_("Goal: rebuild the whole Diablo 1 in 3D. Tristram is the first stage."), width, GameFont12, 1),
			WordWrapString(_("Based on Diablo and DevilutionX, preserving the original authors, contributors, licenses, and credits."), width, GameFont12, 1),
			WordWrapString(_("Main Menu / Tristram — Original composition: Matt Uelmen, for Diablo (Blizzard Entertainment). Cover/reinterpretation produced by Douglas Pan using AI."), width, GameFont12, 1),
			WordWrapString(_("Help shape the project with art, code, testing, and feedback. Join our community."), width, GameFont12, 1),
		};
		const std::string error = linkFailed
		    ? WordWrapString(_("Could not open a browser. Use the addresses shown above."), width, GameFont12, 1)
		    : std::string {};
		auto textHeight = [](const std::string &text) {
			return (1 + static_cast<int>(std::count(text.begin(), text.end(), '\n'))) * LineHeight;
		};
		int height = 40 + 8 + 36 + 12 + 4 + RowHeight * (ProjectCreditsBack + 1);
		for (const auto &paragraph : paragraphs)
			height += textHeight(paragraph) + ParagraphGap;
		if (linkFailed)
			height += 8 + textHeight(error);
		int y = std::max(0, (gnScreenHeight - height) / 2);
		auto addText = [&](const char *text, int textBoxHeight, UiFlags font, UiFlags color) {
			dialog.push_back(std::make_unique<UiArtText>(text,
			    MakeSdlRect(x, y, width, textBoxHeight), font | color | UiFlags::AlignCenter, 1, LineHeight));
			y += textBoxHeight;
		};
		addText(_("Diablo 3D Credits").data(), 40, UiFlags::FontSize30, UiFlags::ColorUiSilver);
		y += 8;
		addText(_("Authorship and direction: Douglas Pan").data(), 36, UiFlags::FontSize24, UiFlags::ColorUiGold);
		y += 12;
		for (const auto &paragraph : paragraphs) {
			addText(paragraph.c_str(), textHeight(paragraph), UiFlags::FontSize12, UiFlags::ColorUiSilver);
			y += ParagraphGap;
		}
		y += 4;
		for (const char *url : ProjectLinks)
			links.push_back(std::make_unique<UiListItem>(std::string_view { url }));
		links.push_back(std::make_unique<UiListItem>(_("Back")));
		dialog.push_back(std::make_unique<UiList>(links, links.size(), x, y, width, RowHeight,
		    UiFlags::FontSize12 | UiFlags::ColorUiGold | UiFlags::AlignCenter, 1));
		if (linkFailed) {
			y += RowHeight * static_cast<int>(links.size()) + 8;
			addText(error.c_str(), textHeight(error), UiFlags::FontSize12, UiFlags::ColorUiSilver);
		}
		UiInitList(nullptr, ProjectCreditsSelect, ProjectCreditsEsc, dialog, true, nullptr, nullptr, selectedItem);
		while (ProjectCreditsAction < 0) {
			UiClearScreen();
			UiPollAndRender();
		}
		selectedItem = SelectedItem;
		UiInitList_clear();
		dialog.clear();
		links.clear();
		SetD3dMainMenuActive(false);
		ArtBackgroundWidescreen = std::nullopt;
		ArtBackground = std::nullopt;

		if (ProjectCreditsAction == ProjectCreditsBack)
			return;
		linkFailed = !OpenProjectLink(ProjectLinks[ProjectCreditsAction]);
	}
}

} // namespace devilution
