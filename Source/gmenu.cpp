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

constexpr int SettingsMaxRows = 8;
constexpr int SettingsRowHeight = 32;
constexpr int SettingsSliderRowHeight = 46;
constexpr int SettingsPadding = 8;
constexpr int SettingsTitleHeight = 28;
constexpr int SettingsGap = 6;
constexpr int SettingsDescriptionLineHeight = 14;

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
	return gmenu_settings_geometry({ gnScreenWidth, gnScreenHeight }, GetMainPanel().position.y, sgCurrentMenuIdx, containsSlider);
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

std::string FitSettingsDescription(std::string_view text, int width, int lines)
{
	if (text.empty() || width <= 0 || lines <= 0)
		return {};
	const std::string wrapped = WordWrapString(text, width);
	std::string_view remaining = wrapped;
	std::string result;
	for (int i = 0; i < lines && !remaining.empty(); ++i) {
		const size_t newline = remaining.find('\n');
		const bool more = newline != std::string_view::npos;
		const std::string_view line = remaining.substr(0, newline);
		if (i != 0)
			result.push_back('\n');
		result.append(FitSettingsLine(line, width, GameFont12, more && i + 1 == lines));
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

void GmenuDrawSettingsItem(const Surface &out, TMenuItem *item, const Rectangle &row)
{
	const bool selected = item == sgpCurrItem;
	if (selected) {
		FillRect(out, row.position.x, row.position.y, row.size.width, row.size.height, PAL16_BLUE + 13);
		FillRect(out, row.position.x, row.position.y, 2, row.size.height, PAL16_YELLOW + 3);
	}
	const UiFlags labelColor = !item->enabled() ? UiFlags::ColorUiSilverDark : selected ? UiFlags::ColorGold : UiFlags::ColorWhitegold;
	const std::string_view label = item->pszStr != nullptr ? item->pszStr : "";
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

	const int x = row.position.x + 10;
	const int width = row.size.width - 20;
	if (value.empty()) {
		DrawString(out, FitSettingsLine(label, width, GameFont12), { { x, row.position.y }, { width, row.size.height } },
		    { .flags = UiFlags::FontSize12 | labelColor | UiFlags::VerticalCenter });
	} else {
		DrawString(out, FitSettingsLine(label, width, GameFont12), { { x, row.position.y }, { width, 16 } },
		    { .flags = UiFlags::FontSize12 | labelColor });
		DrawString(out, FitSettingsLine(value, width, GameFont12), { { x, row.position.y + 16 }, { width, 16 } },
		    { .flags = UiFlags::FontSize12 | UiFlags::ColorUiSilver });
	}
}

void GmenuDrawSettings(const Surface &out)
{
	const GMenuSettingsGeometry layout = GetSettingsGeometry();
	if (layout.panel.size.width <= 0 || layout.panel.size.height <= 0)
		return;
	FillRect(out, layout.panel.position.x, layout.panel.position.y, layout.panel.size.width, layout.panel.size.height, PAL16_GRAY + 14);
	DrawHorizontalLine(out, layout.panel.position, layout.panel.size.width, PAL16_BEIGE + 10);
	DrawHorizontalLine(out, { layout.panel.position.x, layout.panel.position.y + layout.panel.size.height - 1 }, layout.panel.size.width, PAL16_BEIGE + 10);
	const std::string_view title = SettingsPresentation->title != nullptr ? SettingsPresentation->title : "";
	DrawString(out, FitSettingsLine(title, layout.title.size.width, GameFont24), layout.title,
	    { .flags = UiFlags::FontSize24 | UiFlags::ColorGold | UiFlags::AlignCenter });
	for (size_t i = 0; i < layout.rows; ++i)
		GmenuDrawSettingsItem(out, &sgpCurrentMenu[i], layout.row(i));
	if (sgpCurrItem == nullptr || SettingsPresentation->describe == nullptr)
		return;
	const std::string_view description = SettingsPresentation->describe(gmenu_selected_index());
	const int lineHeight = std::max(SettingsDescriptionLineHeight, GetLineHeight(description, GameFont12));
	DrawString(out, FitSettingsDescription(description, layout.description.size.width, layout.description.size.height / lineHeight), layout.description,
	    { .flags = UiFlags::FontSize12 | UiFlags::ColorUiSilver, .lineHeight = lineHeight });
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

} // namespace

TMenuItem *sgpCurrentMenu;

GMenuSettingsGeometry gmenu_settings_geometry(Size screenSize, int mainPanelTop, size_t rowCount, bool containsSlider)
{
	const int width = std::min(600, std::max(0, screenSize.width - 16));
	const int availableHeight = std::max(0, std::min(mainPanelTop, screenSize.height) - 8);
	const int rowHeight = containsSlider ? SettingsSliderRowHeight : SettingsRowHeight;
	constexpr int FixedHeight = 2 * SettingsPadding + SettingsTitleHeight + 2 * SettingsGap;
	const int capacity = std::clamp((availableHeight - FixedHeight - 2 * SettingsDescriptionLineHeight) / rowHeight, 0, SettingsMaxRows);
	const size_t rows = std::min(rowCount, static_cast<size_t>(capacity));
	if (width < 2 * SettingsPadding + 24 || (containsSlider && width < SliderItemWidth + 2 * SettingsPadding)
	    || availableHeight < FixedHeight + 2 * SettingsDescriptionLineHeight)
		return { { { 0, 0 }, { 0, 0 } }, { { 0, 0 }, { 0, 0 } }, { { 0, 0 }, { 0, 0 } }, { { 0, 0 }, { 0, 0 } }, rowHeight, 0 };
	const int itemsHeight = static_cast<int>(rows) * rowHeight;
	const int descriptionHeight = std::min(70, availableHeight - FixedHeight - itemsHeight);
	const int height = FixedHeight + itemsHeight + descriptionHeight;
	const Point position { (screenSize.width - width) / 2, (std::min(mainPanelTop, screenSize.height) - height) / 2 };
	const int contentLeft = position.x + SettingsPadding;
	const int contentWidth = width - 2 * SettingsPadding;
	const Rectangle title { { contentLeft, position.y + SettingsPadding }, { contentWidth, SettingsTitleHeight } };
	const Rectangle items { { contentLeft, title.position.y + title.size.height + SettingsGap }, { contentWidth, itemsHeight } };
	const Rectangle description { { contentLeft + 4, items.position.y + items.size.height + SettingsGap }, { contentWidth - 8, descriptionHeight } };
	return { { position, { width, height } }, title, items, description, rowHeight, rows };
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
		GameMenuMove();
		if (gmenu_current_option != nullptr)
			gmenu_current_option();
		if (sgpCurrentMenu == nullptr)
			return;
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
		if (layout.items.contains(MousePosition)) {
			const size_t index = (MousePosition.y - layout.items.position.y) / layout.rowHeight;
			if (index < layout.rows && sgpCurrentMenu[index].enabled())
				sgpCurrItem = &sgpCurrentMenu[index];
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
		if (!layout.items.contains(MousePosition))
			return MousePosition.y < GetMainPanel().position.y;
		const size_t index = (MousePosition.y - layout.items.position.y) / layout.rowHeight;
		if (index >= layout.rows || !sgpCurrentMenu[index].enabled())
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
