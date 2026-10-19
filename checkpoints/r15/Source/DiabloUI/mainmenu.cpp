#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "engine/assets.hpp"
#include "engine/load_clx.hpp"
#include "engine/point.hpp"
#include "engine/render/d3d_menu_presentation.hpp"
#include "engine/render/d3d_ui_layout.hpp"
#include "game_mode.hpp"
#include "utils/language.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {
int mainmenu_attract_time_out; // seconds
uint32_t dwAttractTicks;

std::vector<std::unique_ptr<UiItemBase>> vecMainMenuDialog;
std::vector<std::unique_ptr<UiListItem>> vecMenuItems;

_mainmenu_selections MainMenuResult;
std::size_t MainMenuSelectedItem = 0;

enum class SupportMenuAction : uint8_t {
	None,
	Support,
	Credits,
	ProjectCredits,
	Back,
};

SupportMenuAction SupportMenuResult;

void SupportMenuSelect(size_t value)
{
	SupportMenuResult = static_cast<SupportMenuAction>(vecMenuItems[value]->m_value);
}

void SupportMenuBack()
{
	SupportMenuResult = SupportMenuAction::Back;
}

void FitMenuItems(int width, int rowHeight)
{
	const auto focus = rowHeight >= 42 ? FOCUS_BIG : rowHeight >= 30 ? FOCUS_MED : FOCUS_SMALL;
	const int selectorWidth = ArtFocus[focus] ? (*ArtFocus[focus])[0].width() : 0;
	const int textWidth = std::max(1, width - 2 * selectorWidth);
	for (auto &item : vecMenuItems) {
		for (const UiFlags font : { UiFlags::FontSize42, UiFlags::FontSize30, UiFlags::FontSize24, UiFlags::FontSize12 }) {
			item->uiFlags = font | UiFlags::VerticalCenter;
			if (GetLineWidth(item->m_text.str(), GetFontSizeFromUiFlags(font), 1) <= textWidth)
				break;
		}
		if (GetLineWidth(item->m_text.str(), GameFont12, 1) > textWidth)
			item->m_text = WordWrapString(item->m_text.str(), textWidth, GameFont12, 1);
	}
}

void UiMainMenuSelect(size_t value)
{
	MainMenuResult = (_mainmenu_selections)vecMenuItems[value]->m_value;
}

#ifndef NOEXIT
void MainmenuEsc()
{
	const std::size_t last = vecMenuItems.size() - 1;
	if (SelectedItem == last) {
		UiMainMenuSelect(last);
	} else {
		SelectedItem = last;
	}
}
#endif

void MainmenuLoad(const char *name)
{
	ReloadD3dUiLayout();
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Single Player"), MAINMENU_SINGLE_PLAYER));
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Multi Player"), MAINMENU_MULTIPLAYER));
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Settings"), MAINMENU_SETTINGS));
	vecMenuItems.push_back(std::make_unique<UiListItem>(_("Project Support and Credits"), MAINMENU_SUPPORT_AND_CREDITS));
#ifndef NOEXIT
	vecMenuItems.push_back(std::make_unique<UiListItem>(gbIsHellfire ? _("Exit Hellfire") : _("Exit Diablo"), MAINMENU_EXIT_DIABLO));
#endif

	if (!gbIsSpawn || gbIsHellfire) {
		ArtBackgroundWidescreen = LoadOptionalClx("ui_art\\mainmenuw.clx");
		UiLoadMenuBackground("ui_art\\mainmenu");
	} else {
		UiLoadMenuBackground("ui_art\\swmmenu");
	}

	UiAddBackground(&vecMainMenuDialog);
	UiAddLogo(&vecMainMenuDialog);
	const Rectangle logo = GetD3dUiRect("MenuLogo", gnScreenWidth, gnScreenHeight,
	    { { (gnScreenWidth - 580) / 2, GetUIRectangle().position.y }, { 580, 154 } });
	vecMainMenuDialog.back()->m_rect = { static_cast<Sint16>(logo.position.x), static_cast<Sint16>(logo.position.y), static_cast<Uint16>(logo.size.width), static_cast<Uint16>(logo.size.height) };

	const Point uiPosition = GetUIRectangle().position;

	if (gbIsSpawn && gbIsHellfire) {
		const SDL_Rect rect1 = { (Sint16)(uiPosition.x), (Sint16)(uiPosition.y + 145), 640, 30 };
		vecMainMenuDialog.push_back(std::make_unique<UiArtText>(_("Shareware").data(), rect1, UiFlags::FontSize30 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 8));
	}

	Rectangle menu = GetD3dUiRect("MenuList", gnScreenWidth, gnScreenHeight,
	    { { uiPosition.x + 64, uiPosition.y + 192 }, { 510, static_cast<int>(vecMenuItems.size()) * 43 } });
	const int rowHeight = std::clamp(menu.size.height / static_cast<int>(vecMenuItems.size()), 43, 64);
	menu.position.y = std::clamp(menu.position.y, 0, std::max(0, gnScreenHeight - rowHeight * static_cast<int>(vecMenuItems.size())));
	FitMenuItems(menu.size.width, rowHeight);
	vecMainMenuDialog.push_back(std::make_unique<UiList>(vecMenuItems, vecMenuItems.size(), menu.position.x, menu.position.y, menu.size.width, rowHeight, UiFlags::ColorUiGold | UiFlags::AlignCenter, 1));

	const SDL_Rect rect2 = { 17, (Sint16)(gnScreenHeight - 36), 605, 21 };
	vecMainMenuDialog.push_back(std::make_unique<UiArtText>(name, rect2, UiFlags::FontSize12 | UiFlags::ColorUiSilverDark));

#ifndef NOEXIT
	UiInitList(nullptr, UiMainMenuSelect, MainmenuEsc, vecMainMenuDialog, true, nullptr, nullptr, MainMenuSelectedItem);
#else
	UiInitList(nullptr, UiMainMenuSelect, nullptr, vecMainMenuDialog, true, nullptr, nullptr, MainMenuSelectedItem);
#endif
}

void MainmenuFree()
{
	UiInitList_clear();
	SetD3dMainMenuActive(false);
	ArtBackgroundWidescreen = std::nullopt;
	ArtBackground = std::nullopt;

	vecMainMenuDialog.clear();

	vecMenuItems.clear();
}

} // namespace

void UiSupportAndCreditsDialog()
{
	std::size_t selectedItem = 0;
	while (true) {
		SupportMenuResult = SupportMenuAction::None;
		vecMenuItems.push_back(std::make_unique<UiListItem>(_("Support"), static_cast<int>(SupportMenuAction::Support)));
		vecMenuItems.push_back(std::make_unique<UiListItem>(_("Show Credits"), static_cast<int>(SupportMenuAction::Credits)));
		vecMenuItems.push_back(std::make_unique<UiListItem>(_("Diablo 3D Credits"), static_cast<int>(SupportMenuAction::ProjectCredits)));
		vecMenuItems.push_back(std::make_unique<UiListItem>(_("Back"), static_cast<int>(SupportMenuAction::Back)));

		UiLoadMenuBackground(!gbIsSpawn || gbIsHellfire ? "ui_art\\mainmenu" : "ui_art\\swmmenu");
		UiAddBackground(&vecMainMenuDialog);

		const int width = std::min(720, gnScreenWidth - 40);
		const int x = (gnScreenWidth - width) / 2;
		const std::string title = WordWrapString(_("Project Support and Credits"), width, GameFont30, 1);
		constexpr int TitleLineHeight = 40;
		const int titleHeight = (1 + static_cast<int>(std::count(title.begin(), title.end(), '\n'))) * TitleLineHeight;
		constexpr int RowHeight = 43;
		const int height = titleHeight + 32 + RowHeight * static_cast<int>(vecMenuItems.size());
		const int y = std::max(0, (gnScreenHeight - height) / 2);
		vecMainMenuDialog.push_back(std::make_unique<UiArtText>(title.c_str(),
		    SDL_Rect { static_cast<Sint16>(x), static_cast<Sint16>(y), static_cast<Uint16>(width), static_cast<Uint16>(titleHeight) },
		    UiFlags::FontSize30 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 1, TitleLineHeight));
		FitMenuItems(width, RowHeight);
		vecMainMenuDialog.push_back(std::make_unique<UiList>(vecMenuItems, vecMenuItems.size(), x, y + titleHeight + 32,
		    width, RowHeight, UiFlags::ColorUiGold | UiFlags::AlignCenter, 1));
		UiInitList(nullptr, SupportMenuSelect, SupportMenuBack, vecMainMenuDialog, true, nullptr, nullptr, selectedItem);

		while (SupportMenuResult == SupportMenuAction::None) {
			UiClearScreen();
			UiPollAndRender();
		}
		selectedItem = SelectedItem;
		MainmenuFree();

		// Open another dialog only after the active list has released its pointers.
		switch (SupportMenuResult) {
		case SupportMenuAction::Support:
			UiSupportDialog();
			break;
		case SupportMenuAction::Credits:
			UiCreditsDialog();
			break;
		case SupportMenuAction::ProjectCredits:
			UiDiablo3DCreditsDialog();
			break;
		case SupportMenuAction::Back:
			return;
		case SupportMenuAction::None:
			break;
		}
	}
}

void mainmenu_restart_repintro()
{
	dwAttractTicks = SDL_GetTicks() + mainmenu_attract_time_out * 1000;
}

bool UiMainMenuDialog(const char *name, _mainmenu_selections *pdwResult, int attractTimeOut)
{
	MainMenuResult = MAINMENU_NONE;
	while (MainMenuResult == MAINMENU_NONE) {
		mainmenu_attract_time_out = attractTimeOut;
		MainmenuLoad(name);

		mainmenu_restart_repintro(); // for automatic starts

		while (MainMenuResult == MAINMENU_NONE) {
			UiClearScreen();
			UiPollAndRender();
			if (SDL_GetTicks() >= dwAttractTicks && (HaveIntro() || gbIsHellfire)) {
				MainMenuResult = MAINMENU_ATTRACT_MODE;
			}
		}

		MainMenuSelectedItem = SelectedItem;
		MainmenuFree();
	}

	*pdwResult = MainMenuResult;
	return true;
}

} // namespace devilution
