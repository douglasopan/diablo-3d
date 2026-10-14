/**
 * @file gmenu.cpp
 *
 * Implementation of the in-game navigation and interaction.
 */
#include "gmenu.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#ifdef USE_SDL3
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/ui_flags.hpp"
#include "appfat.h"
#include "control/control.hpp"
#include "control/d3d_hud.hpp"
#include "controls/axis_direction.h"
#include "controls/controller_motion.h"
#include "engine/clx_sprite.hpp"
#include "engine/demomode.h"
#include "engine/load_cel.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/d3d_logo.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/render/ui_overlay_regions.hpp"
#include "headless_mode.hpp"
#include "options.h"
#include "stores.h"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"

namespace devilution {

namespace {

// Width of the slider menu item, including the label.
constexpr int SliderItemWidth = 490;

// Horizontal dimensions of the slider value
constexpr int SliderValueBoxLeft = 16 + (SliderItemWidth / 2);
constexpr int SliderValueBoxWidth = 287;

constexpr int SliderValueBorderWidth = 2;
constexpr int SliderValueLeft = SliderValueBoxLeft + SliderValueBorderWidth;
constexpr int SliderValueWidth = SliderValueBoxWidth - (2 * SliderValueBorderWidth);
constexpr int SliderValueHeight = 29;
constexpr int SliderValuePaddingTop = 10;
constexpr int SliderMarkerWidth = 27;

constexpr int SliderFillMin = SliderMarkerWidth / 2;
constexpr int SliderFillMax = SliderValueWidth - (SliderMarkerWidth / 2) - 1;

constexpr int GMenuTop = 117;
constexpr int GMenuItemHeight = 45;

constexpr int SettingsSliderRowHeight = 46;
constexpr int SettingsPadding = 8;
constexpr int SettingsGap = 6;
constexpr size_t SettingsMaxNavigationItems = 3;

GMenuSettingsGeometry BuildSettingsGeometry(Size screenSize, int mainPanelTop, size_t rowCount,
    bool containsSlider, size_t navigationItems, bool pauseMenu, int logoHeight = 90);

OptionalOwnedClxSpriteList optbar_cel;
OptionalOwnedClxSpriteList PentSpin_cel;
OptionalOwnedClxSpriteList option_cel;
OptionalOwnedClxSpriteList sgpLogo;
std::optional<D3dLogo> CustomPauseLogo;
uint32_t CustomPauseLogoStartTicks;
bool isDraggingSlider;
TMenuItem *sgpCurrItem;
int LogoAnim_tick;
uint8_t LogoAnim_frame;
void (*gmenu_current_option)();
int sgCurrentMenuIdx;
std::optional<GMenuSettingsPresentation> SettingsPresentation;

GMenuSettingsGeometry GetSettingsGeometry()
{
	bool containsSlider = false;
	for (int i = 0; i < sgCurrentMenuIdx; ++i) {
		if (sgpCurrentMenu[i].isSlider()) {
			containsSlider = true;
			break;
		}
	}
	int logoHeight = 90;
	if (SettingsPresentation->pauseMenu) {
		if (CustomPauseLogo)
			logoHeight = CustomPauseLogo->sprites()[0].height();
		else if (sgpLogo)
			logoHeight = (*sgpLogo)[0].height();
	}
	return BuildSettingsGeometry({ gnScreenWidth, gnScreenHeight }, gmenu_settings_bottom(), sgCurrentMenuIdx,
	    containsSlider, SettingsPresentation->navigationItems, SettingsPresentation->pauseMenu, logoHeight);
}

int GmenuItemCount()
{
	return SettingsPresentation ? static_cast<int>(GetSettingsGeometry().rows) : sgCurrentMenuIdx;
}

void GmenuUpDown(bool isDown)
{
	const int count = GmenuItemCount();
	if (sgpCurrItem == nullptr || count == 0) {
		return;
	}
	isDraggingSlider = false;
	if (sgpCurrItem >= &sgpCurrentMenu[count])
		sgpCurrItem = &sgpCurrentMenu[count - 1];
	int i = count;
	if (count != 0) {
		while (i != 0) {
			i--;
			if (isDown) {
				sgpCurrItem++;
				if (sgpCurrItem >= &sgpCurrentMenu[count])
					sgpCurrItem = &sgpCurrentMenu[0];
			} else {
				if (sgpCurrItem == sgpCurrentMenu)
					sgpCurrItem = &sgpCurrentMenu[count];
				sgpCurrItem--;
			}
			if (sgpCurrItem->enabled()) {
				if (i != 0)
					PlaySFX(SfxID::MenuMove);
				return;
			}
		}
	}
}

void GmenuLeftRight(bool isRight)
{
	if (SettingsPresentation && sgpCurrItem != nullptr) {
		const GMenuSettingsGeometry layout = GetSettingsGeometry();
		const size_t index = gmenu_selected_index();
		if (layout.navigationItems > 1 && index >= layout.contentRows) {
			const size_t column = index - layout.contentRows;
			gmenu_select_index(layout.contentRows + (column + (isRight ? 1 : layout.navigationItems - 1)) % layout.navigationItems);
			PlaySFX(SfxID::MenuMove);
			return;
		}
	}
	if (sgpCurrItem == nullptr || !sgpCurrItem->enabled() || !sgpCurrItem->isSlider())
		return;

	uint16_t step = sgpCurrItem->sliderStep();
	if (isRight) {
		if (step == sgpCurrItem->sliderSteps())
			return;
		step++;
	} else {
		if (step == 0)
			return;
		step--;
	}
	sgpCurrItem->setSliderStep(step);
	sgpCurrItem->fnMenu(false);
}

int GmenuGetLineWidth(TMenuItem *pItem)
{
	if (pItem->isSlider())
		return SliderItemWidth;

	return GetLineWidth(_(pItem->pszStr), GameFont46, 2);
}

void GmenuDrawMenuItem(const Surface &out, TMenuItem *pItem, int y)
{
	const int w = GmenuGetLineWidth(pItem);
	if (pItem->isSlider()) {
		const int uiPositionX = GetUIRectangle().position.x;
		ClxDraw(out, { SliderValueBoxLeft + uiPositionX, y + 40 }, (*optbar_cel)[0]);
		const uint16_t step = pItem->dwFlags & 0xFFF;
		const uint16_t steps = std::max<uint16_t>(pItem->sliderSteps(), 2);
		const uint16_t pos = SliderFillMin + (step * (SliderFillMax - SliderFillMin) / steps);
		SDL_Rect rect = MakeSdlRect(SliderValueLeft + uiPositionX, y + SliderValuePaddingTop, pos, SliderValueHeight);
		// SDL writes in underlying-surface coordinates, independent of out.region.
		MarkUiOverlayRect(Surface(out.surface), rect.x, rect.y, rect.w, rect.h);
		SDL_FillSurfaceRect(out.surface, &rect, 205);
		ClxDraw(out, { SliderValueLeft + pos - (SliderMarkerWidth / 2) + uiPositionX, y + SliderValuePaddingTop + SliderValueHeight - 1 }, (*option_cel)[0]);
	}

	const int x = (gnScreenWidth - w) / 2;
	const UiFlags style = pItem->enabled() ? UiFlags::ColorGold : UiFlags::ColorBlack;
	DrawString(out, _(pItem->pszStr), Point { x, y },
	    { .flags = style | UiFlags::FontSize46, .spacing = 2 });
	if (pItem == sgpCurrItem) {
		const ClxSprite sprite = (*PentSpin_cel)[PentSpn2Spin()];
		ClxDraw(out, { x - 54, y + 51 }, sprite);
		ClxDraw(out, { x + 4 + w, y + 51 }, sprite);
	}
}

std::string FitSettingsLine(std::string_view text, int width, GameFontTables font, bool more = false)
{
	if (width <= 0)
		return {};
	const size_t newline = text.find_first_of("\r\n");
	if (newline != std::string_view::npos) {
		text = text.substr(0, newline);
		more = true;
	}
	if (!more && GetLineWidth(text, font) <= width)
		return std::string(text);

	constexpr std::string_view Ellipsis = "...";
	if (GetLineWidth(Ellipsis, font) > width)
		return {};
	std::string result(text);
	result.append(Ellipsis);
	while (GetLineWidth(result, font) > width && !text.empty()) {
		text = text.substr(0, FindLastUtf8Symbols(text));
		result.assign(text);
		result.append(Ellipsis);
	}
	return result;
}

GameFontTables SettingsFont(int size)
{
	switch (size) {
	case 24: return GameFont24;
	case 30: return GameFont30;
	case 42: return GameFont42;
	case 46: return GameFont46;
	default: return GameFont12;
	}
}

UiFlags SettingsFontFlag(int size)
{
	switch (size) {
	case 24: return UiFlags::FontSize24;
	case 30: return UiFlags::FontSize30;
	case 42: return UiFlags::FontSize42;
	case 46: return UiFlags::FontSize46;
	default: return UiFlags::FontSize12;
	}
}

std::string FitSettingsDescription(std::string_view text, int width, int lines, GameFontTables font)
{
	if (text.empty() || width <= 0 || lines <= 0)
		return {};
	const std::string wrapped = WordWrapString(text, width, font);
	std::string_view remaining = wrapped;
	std::string result;
	for (int i = 0; i < lines && !remaining.empty(); ++i) {
		const size_t newline = remaining.find('\n');
		const bool more = newline != std::string_view::npos;
		const std::string_view line = remaining.substr(0, newline);
		if (i != 0)
			result.push_back('\n');
		result.append(FitSettingsLine(line, width, font, more && i + 1 == lines));
		if (!more)
			break;
		remaining.remove_prefix(newline + 1);
	}
	return result;
}

Rectangle SettingsSliderBox(const Rectangle &row)
{
	// Keep the original bar dimensions and its position relative to the 490px item.
	constexpr int LabelToBarOffset = SliderValueBoxLeft - (640 - SliderItemWidth) / 2;
	return { { row.position.x + (row.size.width - SliderItemWidth) / 2 + LabelToBarOffset, row.position.y + SliderValuePaddingTop },
		{ SliderValueBoxWidth, SliderValueHeight } };
}

void GmenuDrawSettingsItem(const Surface &out, TMenuItem *item, const GMenuSettingsGeometry &layout, size_t index)
{
	const Rectangle row = layout.row(index);
	const bool selected = item == sgpCurrItem;
	if (selected) {
		FillRect(out, row.position.x, row.position.y, row.size.width, row.size.height, PAL16_GRAY + 12);
		const OptionalOwnedClxSpriteList &spinners = SettingsPresentation->pauseMenu ? PentSpin_cel : pSPentSpn2Cels;
		if (spinners) {
			const ClxSprite sprite = (*spinners)[PentSpn2Spin()];
			const int y = row.position.y + (row.size.height - sprite.height()) / 2;
			RenderClxSprite(out, sprite, { row.position.x + 4, y });
			RenderClxSprite(out, sprite, { row.position.x + row.size.width - sprite.width() - 4, y });
		}
	}
	const UiFlags labelColor = item->enabled() ? UiFlags::ColorGold : UiFlags::ColorUiSilverDark;
	const std::string_view label = item->pszStr == nullptr ? "" : SettingsPresentation->pauseMenu ? _(item->pszStr) : item->pszStr;
	const std::string_view value = item->value != nullptr ? item->value : "";
	if (item->isSlider()) {
		const Rectangle box = SettingsSliderBox(row);
		const int textLeft = row.position.x + (row.size.width - SliderItemWidth) / 2;
		const int textWidth = box.position.x - textLeft - 12;
		DrawString(out, FitSettingsLine(label, textWidth, GameFont24), { { textLeft, row.position.y + 3 }, { textWidth, 27 } },
		    { .flags = UiFlags::FontSize24 | labelColor });
		DrawString(out, FitSettingsLine(value, textWidth, GameFont12), { { textLeft, row.position.y + 30 }, { textWidth, 16 } },
		    { .flags = UiFlags::FontSize12 | UiFlags::ColorUiSilver });
		ClxDraw(out, { box.position.x, row.position.y + 40 }, (*optbar_cel)[0]);
		const uint16_t steps = std::max<uint16_t>(item->sliderSteps(), 2);
		const int pos = SliderFillMin + (item->sliderStep() * (SliderFillMax - SliderFillMin) / steps);
		FillRect(out, box.position.x + SliderValueBorderWidth, box.position.y, pos, SliderValueHeight, 205);
		ClxDraw(out, { box.position.x + SliderValueBorderWidth + pos - SliderMarkerWidth / 2, box.position.y + SliderValueHeight - 1 }, (*option_cel)[0]);
		return;
	}

	const int inset = SettingsPresentation->pauseMenu ? 58 : 24;
	const Rectangle text { { row.position.x + inset, row.position.y }, { std::max(0, row.size.width - 2 * inset), row.size.height } };
	const GameFontTables font = SettingsFont(layout.fontSize);
	const UiFlags style = SettingsFontFlag(layout.fontSize) | UiFlags::VerticalCenter;
	if (SettingsPresentation->pauseMenu || (index >= layout.contentRows && value.empty())) {
		DrawString(out, FitSettingsLine(label, text.size.width, font), text, { .flags = style | labelColor | UiFlags::AlignCenter });
		return;
	}
	if (value.empty()) {
		DrawString(out, FitSettingsLine(label, text.size.width, font), text, { .flags = style | labelColor });
		return;
	}
	const int gap = layout.fontSize == 12 ? 12 : 24;
	const int labelWidth = std::max(0, (text.size.width - gap) * 3 / 5);
	const Rectangle labelRect { text.position, { labelWidth, text.size.height } };
	const Rectangle valueRect { { text.position.x + labelWidth + gap, text.position.y }, { std::max(0, text.size.width - labelWidth - gap), text.size.height } };
	DrawString(out, FitSettingsLine(label, labelRect.size.width, font), labelRect, { .flags = style | labelColor });
	DrawString(out, FitSettingsLine(value, valueRect.size.width, font), valueRect, { .flags = style | UiFlags::ColorUiSilver | UiFlags::AlignRight });
}

void GmenuDrawSettings(const Surface &out)
{
	const GMenuSettingsGeometry layout = GetSettingsGeometry();
	if (layout.panel.size.width <= 0 || layout.panel.size.height <= 0)
		return;
	FillRect(out, layout.panel.position.x, layout.panel.position.y, layout.panel.size.width, layout.panel.size.height, PAL16_GRAY + 14);
	DrawHorizontalLine(out, layout.panel.position, layout.panel.size.width, PAL16_BEIGE + 10);
	DrawHorizontalLine(out, { layout.panel.position.x, layout.panel.position.y + layout.panel.size.height - 1 }, layout.panel.size.width, PAL16_BEIGE + 10);
	if (SettingsPresentation->pauseMenu) {
		if (CustomPauseLogo) {
			const uint32_t frame = D3dLogoFrameAt(SDL_GetTicks() - CustomPauseLogoStartTicks);
			const ClxSprite sprite = CustomPauseLogo->sprites()[frame];
			DrawD3dLogo(out, { (gnScreenWidth - sprite.width()) / 2, layout.title.position.y + layout.title.size.height - 1 }, *CustomPauseLogo, frame);
		} else if (sgpLogo) {
			const uint32_t ticks = SDL_GetTicks();
			if (static_cast<int>(ticks - LogoAnim_tick) > 25) {
				LogoAnim_frame = (LogoAnim_frame + 1) % sgpLogo->numSprites();
				LogoAnim_tick = ticks;
			}
			const ClxSprite sprite = (*sgpLogo)[LogoAnim_frame];
			RenderClxSprite(out, sprite, { (gnScreenWidth - sprite.width()) / 2, layout.title.position.y });
		}
	} else {
		const std::string_view title = SettingsPresentation->title != nullptr ? SettingsPresentation->title : "";
		DrawString(out, FitSettingsLine(title, layout.title.size.width, SettingsFont(layout.titleFontSize)), layout.title,
		    { .flags = SettingsFontFlag(layout.titleFontSize) | UiFlags::ColorGold | UiFlags::AlignCenter });
	}
	for (size_t i = 0; i < layout.rows; ++i)
		GmenuDrawSettingsItem(out, &sgpCurrentMenu[i], layout, i);
	if (sgpCurrItem == nullptr || SettingsPresentation->describe == nullptr)
		return;
	const std::string_view description = SettingsPresentation->describe(gmenu_selected_index());
	const GameFontTables font = SettingsFont(layout.descriptionFontSize);
	const int lineHeight = std::max(14, GetLineHeight(description, font));
	DrawString(out, FitSettingsDescription(description, layout.description.size.width, layout.description.size.height / lineHeight, font), layout.description,
	    { .flags = SettingsFontFlag(layout.descriptionFontSize) | UiFlags::ColorUiSilver, .lineHeight = lineHeight });
}

void GameMenuMove()
{
	static AxisDirectionRepeater repeater;
	const AxisDirection moveDir = repeater.Get(GetLeftStickOrDpadDirection(false));
	if (moveDir.x != AxisDirectionX_NONE)
		GmenuLeftRight(moveDir.x == AxisDirectionX_RIGHT);
	if (moveDir.y != AxisDirectionY_NONE)
		GmenuUpDown(moveDir.y == AxisDirectionY_DOWN);
}

bool GmenuMouseIsOverSlider()
{
	const int left = SettingsPresentation ? SettingsSliderBox(GetSettingsGeometry().row(gmenu_selected_index())).position.x + SliderValueBorderWidth
	                                      : SliderValueLeft + GetUIRectangle().position.x;
	if (MousePosition.x < left) {
		return false;
	}
	if (MousePosition.x >= left + SliderValueWidth) {
		return false;
	}
	return true;
}

int GmenuGetSliderFill()
{
	const int left = SettingsPresentation ? SettingsSliderBox(GetSettingsGeometry().row(gmenu_selected_index())).position.x + SliderValueBorderWidth
	                                      : SliderValueLeft + GetUIRectangle().position.x;
	return std::clamp(MousePosition.x - left, SliderFillMin, SliderFillMax);
}

GMenuSettingsGeometry BuildSettingsGeometry(Size screenSize, int mainPanelTop, size_t rowCount,
    bool containsSlider, size_t navigationItems, bool pauseMenu, int logoHeight)
{
	GMenuSettingsGeometry layout {};
	int maximumWidth = 760;
	int titleHeight = 28;
	int minimumDescriptionHeight = 32;
	int maximumDescriptionHeight = 56;
	layout.fontSize = 12;
	layout.titleFontSize = 24;
	layout.descriptionFontSize = 12;
	layout.rowHeight = 24;
	if (screenSize.width >= 960 && screenSize.height >= 540) {
		maximumWidth = 1000;
		titleHeight = 40;
		maximumDescriptionHeight = 70;
		layout.fontSize = 24;
		layout.titleFontSize = 30;
		layout.rowHeight = 32;
	}
	if (screenSize.width >= 960 && screenSize.height >= 900) {
		maximumWidth = 1360;
		titleHeight = 46;
		minimumDescriptionHeight = 52;
		maximumDescriptionHeight = 104;
		layout.fontSize = 30;
		layout.titleFontSize = 42;
		layout.descriptionFontSize = 24;
		layout.rowHeight = 42;
	}
	if (screenSize.width >= 960 && screenSize.height >= 1440) {
		maximumWidth = 1600;
		titleHeight = 54;
		layout.fontSize = 42;
		layout.titleFontSize = 46;
		layout.rowHeight = 48;
	}
	const int boundary = std::clamp(mainPanelTop, 0, std::max(0, screenSize.height));
	const int availableHeight = std::max(0, boundary - 8);
	if (pauseMenu) {
		maximumWidth = 1000;
		titleHeight = std::max(1, logoHeight);
		minimumDescriptionHeight = maximumDescriptionHeight = 0;
		layout.fontSize = availableHeight >= 392 ? 46 : availableHeight >= 342 ? 42 : 30;
		layout.rowHeight = layout.fontSize == 46 ? 56 : layout.fontSize == 42 ? 46 : 44;
		navigationItems = 0;
	}
	if (containsSlider)
		layout.rowHeight = std::max(layout.rowHeight, SettingsSliderRowHeight);
	const int width = std::min(maximumWidth, std::max(0, std::max(0, screenSize.width) - 16));
	navigationItems = std::min({ navigationItems, rowCount, SettingsMaxNavigationItems });
	const int footerHeight = navigationItems != 0 ? layout.rowHeight : 0;
	const int fixedHeight = 2 * SettingsPadding + titleHeight + SettingsGap
	    + (navigationItems != 0 ? SettingsGap : 0) + (minimumDescriptionHeight != 0 ? SettingsGap : 0);
	if (width < 2 * SettingsPadding + 24 || (containsSlider && width < SliderItemWidth + 2 * SettingsPadding)
	    || (pauseMenu && width < 480) || availableHeight < fixedHeight + footerHeight + minimumDescriptionHeight)
		return layout;
	const int capacity = std::clamp((availableHeight - fixedHeight - footerHeight - minimumDescriptionHeight) / layout.rowHeight,
	    0, static_cast<int>(GMenuSettingsMaxContentRows));
	layout.contentRows = std::min(rowCount - navigationItems, static_cast<size_t>(capacity));
	layout.navigationItems = navigationItems;
	layout.rows = layout.contentRows + navigationItems;
	const int itemsHeight = static_cast<int>(layout.contentRows) * layout.rowHeight;
	const int descriptionHeight = std::min(maximumDescriptionHeight, availableHeight - fixedHeight - footerHeight - itemsHeight);
	const int height = fixedHeight + itemsHeight + footerHeight + descriptionHeight;
	const Point position { (screenSize.width - width) / 2, (boundary - height) / 2 };
	const int contentLeft = position.x + SettingsPadding;
	const int contentWidth = width - 2 * SettingsPadding;
	layout.panel = { position, { width, height } };
	layout.title = { { contentLeft, position.y + SettingsPadding }, { contentWidth, titleHeight } };
	layout.items = { { contentLeft, layout.title.position.y + titleHeight + SettingsGap }, { contentWidth, itemsHeight } };
	int nextY = layout.items.position.y + itemsHeight;
	if (descriptionHeight != 0) {
		nextY += SettingsGap;
		layout.description = { { contentLeft + 4, nextY }, { contentWidth - 8, descriptionHeight } };
		nextY += descriptionHeight;
	}
	if (navigationItems != 0) {
		nextY += SettingsGap;
		layout.navigation = { { contentLeft, nextY }, { contentWidth, footerHeight } };
	}
	return layout;
}

} // namespace

TMenuItem *sgpCurrentMenu;

int gmenu_settings_bottom()
{
	int bottom = GetMainPanel().position.y;
	if (!IsD3dHudEnabled())
		return bottom;

	// The authored HUD scales and can be moved in the editor. Reserve its
	// actual visible footprints, not the legacy panel's fixed 128px height.
	bottom = std::min({ bottom, GetD3dHudFrameRect().position.y,
	    GetD3dHudOrbRect(false).position.y, GetD3dHudOrbRect(true).position.y });
	const int buttons = IsChatAvailable() ? 8 : 6;
	for (int button = 0; button < buttons; ++button)
		bottom = std::min(bottom, GetD3dHudPanelButtonRect(button).position.y);
	return std::clamp(bottom, 0, static_cast<int>(gnScreenHeight));
}

GMenuSettingsGeometry gmenu_settings_geometry(Size screenSize, int mainPanelTop, size_t rowCount, bool containsSlider, size_t navigationItems)
{
	return BuildSettingsGeometry(screenSize, mainPanelTop, rowCount, containsSlider, navigationItems, false);
}

size_t gmenu_settings_page_size(Size screenSize, int mainPanelTop)
{
	return std::max(size_t { 1 }, gmenu_settings_geometry(screenSize, mainPanelTop,
	    GMenuSettingsMaxContentRows + SettingsMaxNavigationItems, false, SettingsMaxNavigationItems).contentRows);
}

GMenuSettingsGeometry gmenu_get_settings_geometry()
{
	return SettingsPresentation ? GetSettingsGeometry() : GMenuSettingsGeometry {};
}

void gmenu_draw_pause(const Surface &out)
{
	if (HeadlessMode)
		return;
	if (leveltype != DTYPE_TOWN)
		RedBack(out);
	if (sgpCurrentMenu == nullptr) {
		DrawString(out, _("Pause"), { { 0, 0 }, { gnScreenWidth, GetMainPanel().position.y } },
		    { .flags = UiFlags::FontSize46 | UiFlags::ColorGold | UiFlags::AlignCenter | UiFlags::VerticalCenter, .spacing = 2 });
	}
}

void FreeGMenu()
{
	SettingsPresentation = std::nullopt;
	sgpLogo = std::nullopt;
	CustomPauseLogo = std::nullopt;
	PentSpin_cel = std::nullopt;
	option_cel = std::nullopt;
	optbar_cel = std::nullopt;
}

void gmenu_init_menu()
{
	LogoAnim_frame = 0;
	sgpCurrentMenu = nullptr;
	sgpCurrItem = nullptr;
	gmenu_current_option = nullptr;
	sgCurrentMenuIdx = 0;
	isDraggingSlider = false;
	SettingsPresentation = std::nullopt;

	if (HeadlessMode)
		return;

	CustomPauseLogo = LoadD3dLogo(D3dLogoKind::Pause);
	sgpLogo = std::nullopt;
	if (!CustomPauseLogo) {
		sgpLogo = LoadOptionalCel("data\\hf_logo3", 430);
		if (!sgpLogo.has_value())
			sgpLogo = LoadCel("data\\diabsmal", 296);
	}
	PentSpin_cel = LoadCel("data\\pentspin", 48);
	if (!pSPentSpn2Cels)
		LoadSmallSelectionSpinner();
	option_cel = LoadCel("data\\option", SliderMarkerWidth);
	optbar_cel = LoadCel("data\\optbar", SliderValueBoxWidth);
}

bool gmenu_is_active()
{
	return sgpCurrentMenu != nullptr;
}

void gmenu_set_items(TMenuItem *pItem, void (*gmFunc)())
{
	SettingsPresentation = std::nullopt;
	if (pItem != nullptr && sgpCurrentMenu == nullptr && CustomPauseLogo)
		CustomPauseLogoStartTicks = SDL_GetTicks();
	PauseMode = 0;
	isDraggingSlider = false;
	sgpCurrentMenu = pItem;
	gmenu_current_option = gmFunc;
	if (gmenu_current_option != nullptr) {
		gmenu_current_option();
	}
	sgCurrentMenuIdx = 0;
	if (sgpCurrentMenu != nullptr) {
		for (int i = 0; sgpCurrentMenu[i].fnMenu != nullptr; i++) {
			sgCurrentMenuIdx++;
		}
	}
	// BUGFIX: OOB access when sgCurrentMenuIdx is 0; should be set to NULL instead. (fixed)
	sgpCurrItem = sgCurrentMenuIdx > 0 ? &sgpCurrentMenu[sgCurrentMenuIdx - 1] : nullptr;
	GmenuUpDown(true);
	if (sgpCurrentMenu == nullptr && !demo::IsRunning()) {
		SaveOptions();
	}
}

void gmenu_set_settings_presentation(GMenuSettingsPresentation presentation)
{
	SettingsPresentation = presentation;
	isDraggingSlider = false;
	gmenu_select_index(gmenu_selected_index());
}

size_t gmenu_selected_index()
{
	return sgpCurrItem != nullptr ? static_cast<size_t>(sgpCurrItem - sgpCurrentMenu) : 0;
}

void gmenu_select_index(size_t index)
{
	isDraggingSlider = false;
	const int count = GmenuItemCount();
	if (count == 0) {
		sgpCurrItem = nullptr;
		return;
	}
	index = std::min(index, static_cast<size_t>(count - 1));
	for (int i = 0; i < count; ++i) {
		TMenuItem *item = &sgpCurrentMenu[(index + i) % count];
		if (item->enabled()) {
			sgpCurrItem = item;
			return;
		}
	}
	sgpCurrItem = nullptr;
}

void gmenu_draw(const Surface &out)
{
	if (HeadlessMode)
		return;
	if (sgpCurrentMenu != nullptr) {
		if (gmenu_current_option != nullptr)
			gmenu_current_option();
		if (sgpCurrentMenu == nullptr)
			return;
		if (!SettingsPresentation || !SettingsPresentation->lockNavigation)
			GameMenuMove();
		if (SettingsPresentation) {
			GmenuDrawSettings(out);
			return;
		}
		const int uiPositionY = GetUIRectangle().position.y;
		if (CustomPauseLogo) {
			const uint32_t frame = D3dLogoFrameAt(SDL_GetTicks() - CustomPauseLogoStartTicks);
			const ClxSprite sprite = CustomPauseLogo->sprites()[frame];
			DrawD3dLogo(out, { (gnScreenWidth - sprite.width()) / 2, 102 + uiPositionY }, *CustomPauseLogo, frame);
		} else {
			if (sgpLogo->numSprites() > 1) {
				const uint32_t ticks = SDL_GetTicks();
				if ((int)(ticks - LogoAnim_tick) > 25) {
					++LogoAnim_frame;
					LogoAnim_frame = LogoAnim_frame % sgpLogo->numSprites();
					LogoAnim_tick = ticks;
				}
			}
			const ClxSprite sprite = (*sgpLogo)[LogoAnim_frame];
			ClxDraw(out, { (gnScreenWidth - sprite.width()) / 2, 102 + uiPositionY }, sprite);
		}
		int y = 110 + uiPositionY;
		TMenuItem *i = sgpCurrentMenu;
		if (sgpCurrentMenu->fnMenu != nullptr) {
			while (i->fnMenu != nullptr) {
				GmenuDrawMenuItem(out, i, y);
				i++;
				y += GMenuItemHeight;
			}
		}
	}
}

bool gmenu_presskeys(SDL_Keycode vkey)
{
	if (sgpCurrentMenu == nullptr)
		return false;
	switch (vkey) {
	case SDLK_KP_ENTER:
	case SDLK_RETURN:
		if (sgpCurrItem != nullptr && sgpCurrItem->enabled()) {
			PlaySFX(SfxID::MenuMove);
			sgpCurrItem->fnMenu(true);
		}
		break;
	case SDLK_ESCAPE:
		PlaySFX(SfxID::MenuMove);
		if (SettingsPresentation && SettingsPresentation->back != nullptr)
			SettingsPresentation->back();
		else
			gmenu_set_items(nullptr, nullptr);
		break;
	case SDLK_PAGEUP:
	case SDLK_PAGEDOWN:
		if (SettingsPresentation && SettingsPresentation->page != nullptr)
			SettingsPresentation->page(vkey == SDLK_PAGEDOWN);
		break;
	case SDLK_HOME:
		if (SettingsPresentation)
			gmenu_select_index(0);
		break;
	case SDLK_END:
		if (SettingsPresentation && GmenuItemCount() > 0)
			gmenu_select_index(GmenuItemCount() - 1);
		break;
	case SDLK_SPACE:
		return SettingsPresentation.has_value();
	case SDLK_LEFT:
		GmenuLeftRight(false);
		break;
	case SDLK_RIGHT:
		GmenuLeftRight(true);
		break;
	case SDLK_UP:
		GmenuUpDown(false);
		break;
	case SDLK_DOWN:
		GmenuUpDown(true);
		break;
	default:
		break;
	}
	return true;
}

bool gmenu_on_mouse_move()
{
	if (!isDraggingSlider) {
		if (!SettingsPresentation || sgpCurrentMenu == nullptr)
			return false;
		const GMenuSettingsGeometry layout = GetSettingsGeometry();
		for (size_t index = 0; index < layout.rows; ++index) {
			if (layout.row(index).contains(MousePosition) && sgpCurrentMenu[index].enabled()) {
				sgpCurrItem = &sgpCurrentMenu[index];
				break;
			}
		}
		return layout.panel.contains(MousePosition);
	}
	if (sgpCurrItem == nullptr || !sgpCurrItem->isSlider()) {
		isDraggingSlider = false;
		return false;
	}

	const uint16_t step = sgpCurrItem->sliderSteps() * (GmenuGetSliderFill() - SliderFillMin) / (SliderFillMax - SliderFillMin);
	sgpCurrItem->setSliderStep(step);
	sgpCurrItem->fnMenu(false);

	return true;
}

bool gmenu_left_mouse(bool isDown)
{
	if (!isDown) {
		if (isDraggingSlider) {
			isDraggingSlider = false;
			return true;
		}
		return false;
	}

	if (sgpCurrentMenu == nullptr) {
		return false;
	}
	if (SettingsPresentation) {
		const GMenuSettingsGeometry layout = GetSettingsGeometry();
		for (size_t index = 0; index < layout.rows; ++index) {
			if (!layout.row(index).contains(MousePosition))
				continue;
			if (!sgpCurrentMenu[index].enabled())
				return true;
			sgpCurrItem = &sgpCurrentMenu[index];
			PlaySFX(SfxID::MenuMove);
			if (sgpCurrItem->isSlider()) {
				isDraggingSlider = SettingsSliderBox(layout.row(index)).contains(MousePosition) && GmenuMouseIsOverSlider();
				if (isDraggingSlider)
					gmenu_on_mouse_move();
			} else {
				sgpCurrItem->fnMenu(true);
			}
			return true;
		}
		return MousePosition.y < GetMainPanel().position.y;
	}
	const Point uiPosition = GetUIRectangle().position;
	if (MousePosition.y >= GetMainPanel().position.y) {
		return false;
	}
	if (MousePosition.y - (GMenuTop + uiPosition.y) < 0) {
		return true;
	}
	const int i = (MousePosition.y - (GMenuTop + uiPosition.y)) / GMenuItemHeight;
	if (i >= sgCurrentMenuIdx) {
		return true;
	}
	TMenuItem *pItem = &sgpCurrentMenu[i];
	if (!pItem->enabled()) {
		return true;
	}
	const int w = GmenuGetLineWidth(pItem);
	const uint16_t screenWidth = GetScreenWidth();
	if (MousePosition.x < screenWidth / 2 - w / 2) {
		return true;
	}
	if (MousePosition.x > screenWidth / 2 + w / 2) {
		return true;
	}
	sgpCurrItem = pItem;
	PlaySFX(SfxID::MenuMove);
	if (pItem->isSlider()) {
		isDraggingSlider = GmenuMouseIsOverSlider();
		gmenu_on_mouse_move();
	} else {
		sgpCurrItem->fnMenu(true);
	}
	return true;
}

void gmenu_slider_set(TMenuItem *pItem, int min, int max, int value)
{
	assert(pItem);
	const uint16_t nSteps = std::max<uint16_t>(pItem->sliderSteps(), 2);
	pItem->setSliderStep(((max - min - 1) / 2 + (value - min) * nSteps) / (max - min));
}

int gmenu_slider_get(TMenuItem *pItem, int min, int max)
{
	const uint16_t step = pItem->sliderStep();
	const uint16_t steps = std::max<uint16_t>(pItem->sliderSteps(), 2);
	return min + ((step * (max - min) + (steps - 1) / 2) / steps);
}

void gmenu_slider_steps(TMenuItem *pItem, int steps)
{
	pItem->dwFlags &= 0xFF000FFF;
	pItem->setSliderSteps(steps);
}

} // namespace devilution
