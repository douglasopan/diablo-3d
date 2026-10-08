/**
 * @file gamemenu.cpp
 *
 * Implementation of the in-game menu functions.
 */
#include "gamemenu.h"

#include <algorithm>
#include <array>
#include <string>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#endif

#include "cursor.h"
#include "diablo_msg.hpp"
#include "engine/backbuffer_state.hpp"
#include "engine/demomode.h"
#include "engine/events.hpp"
#include "engine/music_catalog.hpp"
#include "engine/sound.h"
#include "engine/sound_defs.hpp"
#include "game_mode.hpp"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "loadsave.h"
#include "multi.h"
#include "options.h"
#include "pfile.h"
#include "qol/floatingnumbers.h"
#include "utils/language.h"

#ifndef USE_SDL1
#include "controls/touch/renderers.h"
#endif

namespace devilution {

bool isGameMenuOpen = false;

namespace {

// Forward-declare menu handlers, used by the global menu structs below.
void GamemenuPrevious(bool bActivate);
void GamemenuNewGame(bool bActivate);
void GamemenuOptions(bool bActivate);
void GamemenuAudioOptions(bool bActivate);
void GamemenuAudioPrevious(bool bActivate);
void GamemenuSoundtrack(bool bActivate);
void GamemenuSoundtrackPrevious(bool bActivate);
void GamemenuMusicTheme(bool bActivate);
void GamemenuMusicLocations(bool bActivate);
void GamemenuMusicLocationsNext(bool bActivate);
void GamemenuMusicLocationFirst(bool bActivate);
void GamemenuMusicLocationSecond(bool bActivate);
void GamemenuMusicLocationThird(bool bActivate);
void GamemenuMusicLocationPrevious(bool bActivate);
void GamemenuMusicOriginal(bool bActivate);
void GamemenuMusicRock(bool bActivate);
void GamemenuMusicAlternative(bool bActivate);
void GamemenuMusicRandom(bool bActivate);
void GamemenuMusicThird(bool bActivate);
void GamemenuMusicVariantsNext(bool bActivate);
void GamemenuVideoOptions(bool bActivate);
void GamemenuVideoPrevious(bool bActivate);
void GamemenuGpuRendering(bool bActivate);
void GamemenuEdgeSmoothing(bool bActivate);
void GamemenuStartIn3D(bool bActivate);
void GamemenuMusicVolume(bool bActivate);
void GamemenuSoundVolume(bool bActivate);
void GamemenuBrightness(bool bActivate);
void GamemenuSpeed(bool bActivate);

/** Contains the game menu items of the single player menu. */
TMenuItem sgSingleMenu[] = {
	// clang-format off
	// dwFlags,      pszStr,                  fnMenu
	{ GMENU_ENABLED, N_("Options"),           &GamemenuOptions    },
	{ GMENU_ENABLED, N_("Save Game"),         &gamemenu_save_game },
	{ GMENU_ENABLED, N_("Load Game"),         &gamemenu_load_game },
	{ GMENU_ENABLED, N_("Exit to Main Menu"), &GamemenuNewGame    },
	{ GMENU_ENABLED, N_("Quit Game"),         &gamemenu_quit_game },
	{ GMENU_ENABLED, nullptr,                 nullptr             },
	// clang-format on
};
/** Contains the game menu items of the multi player menu. */
TMenuItem sgMultiMenu[] = {
	// clang-format off
	// dwFlags,      pszStr,                  fnMenu
	{ GMENU_ENABLED, N_("Options"),           &GamemenuOptions    },
	{ GMENU_ENABLED, N_("Exit to Main Menu"), &GamemenuNewGame    },
	{ GMENU_ENABLED, N_("Quit Game"),         &gamemenu_quit_game },
	{ GMENU_ENABLED, nullptr,                 nullptr             },
	// clang-format on
};
TMenuItem sgOptionsMenu[] = {
	// clang-format off
	// dwFlags,                     pszStr,              fnMenu
	{ GMENU_ENABLED               , N_("Audio Options"), &GamemenuAudioOptions  },
	{ GMENU_ENABLED               , N_("Video Options"), &GamemenuVideoOptions  },
	{ GMENU_ENABLED | GMENU_SLIDER, N_("Speed"),         &GamemenuSpeed        },
	{ GMENU_ENABLED               , N_("Previous Menu"), &GamemenuPrevious     },
	{ GMENU_ENABLED               , nullptr,             nullptr               },
	// clang-format on
};
// All submenus stay within five rows, above the control panel at 640x480.
TMenuItem sgAudioOptionsMenu[] = {
	// clang-format off
	{ GMENU_ENABLED | GMENU_SLIDER, nullptr,             &GamemenuMusicVolume        },
	{ GMENU_ENABLED | GMENU_SLIDER, nullptr,             &GamemenuSoundVolume        },
	{ GMENU_ENABLED               , N_("Soundtrack"),    &GamemenuSoundtrack         },
	{ GMENU_ENABLED               , N_("Previous Menu"), &GamemenuAudioPrevious      },
	{ GMENU_ENABLED               , nullptr,             nullptr                     },
	// clang-format on
};
TMenuItem sgSoundtrackMenu[] = {
	// clang-format off
	{ GMENU_ENABLED, nullptr,                 &GamemenuMusicTheme         },
	{ GMENU_ENABLED, N_("Music by Location"), &GamemenuMusicLocations     },
	{ GMENU_ENABLED, N_("Previous Menu"),     &GamemenuSoundtrackPrevious },
	{ GMENU_ENABLED, nullptr,                 nullptr                    },
	// clang-format on
};
TMenuItem sgMusicLocationsMenu[6];
TMenuItem sgMusicVariantsMenu[6];
constexpr std::array<_music_id, NUM_MUSIC> MusicLocationOrder = {
	TMUSIC_INTRO, TMUSIC_TOWN, TMUSIC_CATHEDRAL, TMUSIC_CATACOMBS,
	TMUSIC_CAVES, TMUSIC_HELL, TMUSIC_NEST, TMUSIC_CRYPT
};
constexpr size_t MusicLocationsPerPage = 3;
size_t MusicLocationPage = 0;
size_t MusicVariantPage = 0;
_music_id SelectedMusicLocation = TMUSIC_INTRO;
// Own the dynamic labels: a temporary option description must never back pszStr.
std::array<std::string, 4> MusicVariantLabels;
const char *const MusicThemeNames[] = {
	N_("Theme: Vanilla"),
	N_("Theme: Rock"),
	N_("Theme: Custom"),
};

TMenuItem sgVideoOptionsMenu[] = {
	// clang-format off
	{ GMENU_ENABLED               , nullptr,             &GamemenuGpuRendering  },
	{ GMENU_ENABLED               , nullptr,             &GamemenuEdgeSmoothing },
	{ GMENU_ENABLED               , nullptr,             &GamemenuStartIn3D     },
	{ GMENU_ENABLED | GMENU_SLIDER, N_("Gamma"),          &GamemenuBrightness    },
	{ GMENU_ENABLED               , N_("Previous Menu"), &GamemenuVideoPrevious },
	{ GMENU_ENABLED               , nullptr,             nullptr                },
	// clang-format on
};
const char *const GpuRenderingToggleNames[] = {
	N_("3D GPU Rendering: Off"),
	N_("3D GPU Rendering: On"),
};
const char *const EdgeSmoothingToggleNames[] = {
	N_("3D Edge Smoothing: Off"),
	N_("3D Edge Smoothing: On"),
};
const char *const StartIn3DToggleNames[] = {
	N_("Start in 3D: Off"),
	N_("Start in 3D: On"),
};

/** Specifies the menu names for music enabled and disabled. */
const char *const MusicToggleNames[] = {
	N_("Music"),
	N_("Music Disabled"),
};
/** Specifies the menu names for sound enabled and disabled. */
const char *const SoundToggleNames[] = {
	N_("Sound"),
	N_("Sound Disabled"),
};

void GamemenuUpdateSingle()
{
	sgSingleMenu[2].setEnabled(gbValidSaveFile);

	const bool enable = MyPlayer->_pmode != PM_DEATH && !MyPlayerIsDead;

	sgSingleMenu[0].setEnabled(enable);
}

void GamemenuPrevious(bool /*bActivate*/)
{
	gamemenu_on();
}

void GamemenuNewGame(bool /*bActivate*/)
{
	for (Player &player : Players) {
		player._pmode = PM_QUIT;
		player._pInvincible = true;
	}

	MyPlayerIsDead = false;
	if (!HeadlessMode) {
		RedrawEverything();
		scrollrt_draw_game_screen();
	}
	CornerStone.activated = false;
	gbRunGame = false;
	gamemenu_off();
}

void GamemenuSoundMusicToggle(const char *const *names, TMenuItem *menuItem, int volume)
{
	if (gbSndInited) {
		menuItem->addFlags(GMENU_ENABLED | GMENU_SLIDER);
		menuItem->pszStr = names[0];
		gmenu_slider_steps(menuItem, VOLUME_STEPS);
		gmenu_slider_set(menuItem, VOLUME_MIN, VOLUME_MAX, volume);
		return;
	}

	menuItem->removeFlags(GMENU_ENABLED | GMENU_SLIDER);
	menuItem->pszStr = names[1];
}

int GamemenuSliderMusicSound(TMenuItem *menuItem)
{
	return gmenu_slider_get(menuItem, VOLUME_MIN, VOLUME_MAX);
}

void GamemenuGetMusic()
{
	GamemenuSoundMusicToggle(MusicToggleNames, sgAudioOptionsMenu, sound_get_or_set_music_volume(1));
}

void GamemenuGetSound()
{
	GamemenuSoundMusicToggle(SoundToggleNames, &sgAudioOptionsMenu[1], sound_get_or_set_sound_volume(1));
}

void GamemenuGetBrightness()
{
	gmenu_slider_steps(&sgVideoOptionsMenu[3], 21);
	gmenu_slider_set(&sgVideoOptionsMenu[3], 0, 100, UpdateBrightness(-1));
}

void GamemenuGetSpeed()
{
	if (gbIsMultiplayer) {
		sgOptionsMenu[2].removeFlags(GMENU_ENABLED | GMENU_SLIDER);
		if (sgGameInitInfo.nTickRate >= 50)
			sgOptionsMenu[2].pszStr = _("Speed: Fastest").data();
		else if (sgGameInitInfo.nTickRate >= 40)
			sgOptionsMenu[2].pszStr = _("Speed: Faster").data();
		else if (sgGameInitInfo.nTickRate >= 30)
			sgOptionsMenu[2].pszStr = _("Speed: Fast").data();
		else if (sgGameInitInfo.nTickRate == 20)
			sgOptionsMenu[2].pszStr = _("Speed: Normal").data();
		return;
	}

	sgOptionsMenu[2].addFlags(GMENU_ENABLED | GMENU_SLIDER);

	sgOptionsMenu[2].pszStr = _("Speed").data();
	gmenu_slider_steps(&sgOptionsMenu[2], 46);
	gmenu_slider_set(&sgOptionsMenu[2], 20, 50, sgGameInitInfo.nTickRate);
}

int GamemenuSliderBrightness()
{
	return gmenu_slider_get(&sgVideoOptionsMenu[3], 0, 100);
}

void GamemenuOptions(bool /*bActivate*/)
{
	GamemenuGetSpeed();
	gmenu_set_items(sgOptionsMenu, nullptr);
}

void GamemenuAudioOptions(bool /*bActivate*/)
{
	GamemenuGetMusic();
	GamemenuGetSound();
	gmenu_set_items(sgAudioOptionsMenu, nullptr);
}

void GamemenuAudioPrevious(bool /*bActivate*/)
{
	GamemenuOptions(true);
}

void GamemenuGetMusicTheme()
{
	sgSoundtrackMenu[0].pszStr = MusicThemeNames[GetOptions().Music.theme.GetActiveListIndex()];
}

void GamemenuSoundtrack(bool /*bActivate*/)
{
	GamemenuGetMusicTheme();
	gmenu_set_items(sgSoundtrackMenu, nullptr);
}

void GamemenuSoundtrackPrevious(bool /*bActivate*/)
{
	GamemenuAudioOptions(true);
}

void GamemenuSaveMusicOptions()
{
	if (!demo::IsRunning())
		SaveOptions();
}

void GamemenuMusicTheme(bool bActivate)
{
	if (!bActivate)
		return;
	OptionEntryEnum<MusicTheme> &theme = GetOptions().Music.theme;
	theme.SetActiveListIndex((theme.GetActiveListIndex() + 1) % theme.GetListSize());
	GamemenuGetMusicTheme();
	GamemenuSaveMusicOptions();
}

void GamemenuShowMusicLocations()
{
	constexpr std::array<void (*)(bool), MusicLocationsPerPage> Handlers = {
		&GamemenuMusicLocationFirst, &GamemenuMusicLocationSecond, &GamemenuMusicLocationThird
	};
	const size_t offset = MusicLocationPage * MusicLocationsPerPage;
	const size_t count = std::min(MusicLocationsPerPage, MusicLocationOrder.size() - offset);
	for (size_t i = 0; i < count; ++i) {
		const MusicTrackDefinition &track = MusicTrackCatalog[MusicLocationOrder[offset + i]];
		sgMusicLocationsMenu[i] = { GMENU_ENABLED, track.name, Handlers[i] };
	}
	sgMusicLocationsMenu[count] = { GMENU_ENABLED,
		offset + count < MusicLocationOrder.size() ? N_("More Tracks") : N_("First Page"),
		&GamemenuMusicLocationsNext };
	sgMusicLocationsMenu[count + 1] = { GMENU_ENABLED, N_("Previous Menu"), &GamemenuSoundtrack };
	sgMusicLocationsMenu[count + 2] = { GMENU_ENABLED, nullptr, nullptr };
	gmenu_set_items(sgMusicLocationsMenu, nullptr);
}

void GamemenuMusicLocations(bool /*bActivate*/)
{
	MusicLocationPage = 0;
	GamemenuShowMusicLocations();
}

void GamemenuMusicLocationsNext(bool /*bActivate*/)
{
	constexpr size_t PageCount = (MusicLocationOrder.size() + MusicLocationsPerPage - 1) / MusicLocationsPerPage;
	MusicLocationPage = (MusicLocationPage + 1) % PageCount;
	GamemenuShowMusicLocations();
}

void GamemenuShowMusicVariants()
{
	constexpr std::array<void (*)(bool), 5> Handlers = {
		&GamemenuMusicOriginal, &GamemenuMusicRock, &GamemenuMusicAlternative, &GamemenuMusicRandom, &GamemenuMusicThird
	};
	constexpr std::array<MusicVariant, 5> DisplayOrder = {
		MusicVariant::Original, MusicVariant::Rock, MusicVariant::Alternative, MusicVariant::Third, MusicVariant::Random
	};
	const MusicOptions &music = GetOptions().Music;
	const OptionEntryMusicVariant &option = music.ForTrack(SelectedMusicLocation);
	const MusicVariant selected = *music.theme == MusicTheme::Vanilla ? MusicVariant::Original
	    : *music.theme == MusicTheme::Rock ? (SelectedMusicLocation == TMUSIC_TOWN ? MusicVariant::Random : MusicVariant::Rock)
	                                     : *option;
	const bool paginated = option.GetListSize() > 4;
	const size_t offset = paginated ? MusicVariantPage * 3 : 0;
	const size_t count = paginated ? std::min(size_t { 3 }, option.GetListSize() - offset) : option.GetListSize();
	for (size_t i = 0; i < count; ++i) {
		const MusicVariant variant = DisplayOrder[offset + i];
		const size_t optionIndex = static_cast<size_t>(variant);
		MusicVariantLabels[i] = option.GetListDescription(optionIndex);
		if (variant == selected)
			MusicVariantLabels[i].insert(0, "* ");
		sgMusicVariantsMenu[i] = { GMENU_ENABLED, MusicVariantLabels[i].c_str(), Handlers[optionIndex] };
	}
	size_t row = count;
	if (paginated)
		sgMusicVariantsMenu[row++] = { GMENU_ENABLED, MusicVariantPage == 0 ? N_("More Versions") : N_("Previous Page"), &GamemenuMusicVariantsNext };
	sgMusicVariantsMenu[row++] = { GMENU_ENABLED, N_("Previous Menu"), &GamemenuMusicLocationPrevious };
	sgMusicVariantsMenu[row] = { GMENU_ENABLED, nullptr, nullptr };
	gmenu_set_items(sgMusicVariantsMenu, nullptr);
}

void GamemenuMusicVariantsNext(bool /*bActivate*/)
{
	MusicVariantPage = (MusicVariantPage + 1) % 2;
	GamemenuShowMusicVariants();
}

void GamemenuSelectMusicLocation(size_t index)
{
	SelectedMusicLocation = MusicLocationOrder[MusicLocationPage * MusicLocationsPerPage + index];
	MusicVariantPage = 0;
	GamemenuShowMusicVariants();
}

void GamemenuMusicLocationFirst(bool /*bActivate*/)
{
	GamemenuSelectMusicLocation(0);
}

void GamemenuMusicLocationSecond(bool /*bActivate*/)
{
	GamemenuSelectMusicLocation(1);
}

void GamemenuMusicLocationThird(bool /*bActivate*/)
{
	GamemenuSelectMusicLocation(2);
}

void GamemenuMusicLocationPrevious(bool /*bActivate*/)
{
	GamemenuShowMusicLocations();
}

void GamemenuSetMusicVariant(MusicVariant variant)
{
	GetOptions().Music.ForTrack(SelectedMusicLocation).SetValue(variant);
	GamemenuSaveMusicOptions();
	GamemenuShowMusicVariants();
}

void GamemenuMusicOriginal(bool bActivate)
{
	if (bActivate)
		GamemenuSetMusicVariant(MusicVariant::Original);
}

void GamemenuMusicRock(bool bActivate)
{
	if (bActivate)
		GamemenuSetMusicVariant(MusicVariant::Rock);
}

void GamemenuMusicAlternative(bool bActivate)
{
	if (bActivate)
		GamemenuSetMusicVariant(MusicVariant::Alternative);
}

void GamemenuMusicRandom(bool bActivate)
{
	if (bActivate)
		GamemenuSetMusicVariant(MusicVariant::Random);
}

void GamemenuMusicThird(bool bActivate)
{
	if (bActivate)
		GamemenuSetMusicVariant(MusicVariant::Third);
}

void GamemenuGetVideoOptions()
{
	const GraphicsOptions &graphics = GetOptions().Graphics;
	sgVideoOptionsMenu[0].pszStr = GpuRenderingToggleNames[*graphics.townViewGpuRendering ? 1 : 0];
	sgVideoOptionsMenu[1].pszStr = EdgeSmoothingToggleNames[*graphics.townViewAntialiasing ? 1 : 0];
	sgVideoOptionsMenu[2].pszStr = StartIn3DToggleNames[*graphics.townViewStartIn3D ? 1 : 0];
	GamemenuGetBrightness();
}

void GamemenuVideoOptions(bool /*bActivate*/)
{
	GamemenuGetVideoOptions();
	gmenu_set_items(sgVideoOptionsMenu, nullptr);
}

void GamemenuVideoPrevious(bool /*bActivate*/)
{
	GamemenuOptions(true);
}

void GamemenuToggleVideoOption(OptionEntryBoolean &option)
{
	option.SetValue(!*option);
	GamemenuGetVideoOptions();
	RedrawEverything();
	// Match the existing menu policy: playback must not rewrite preferences.
	if (!demo::IsRunning())
		SaveOptions();
}

void GamemenuGpuRendering(bool bActivate)
{
	if (bActivate)
		GamemenuToggleVideoOption(GetOptions().Graphics.townViewGpuRendering);
}

void GamemenuEdgeSmoothing(bool bActivate)
{
	if (bActivate)
		GamemenuToggleVideoOption(GetOptions().Graphics.townViewAntialiasing);
}

void GamemenuStartIn3D(bool bActivate)
{
	if (bActivate)
		GamemenuToggleVideoOption(GetOptions().Graphics.townViewStartIn3D);
}

void GamemenuMusicVolume(bool bActivate)
{
	if (bActivate) {
		if (gbMusicOn) {
			gbMusicOn = false;
			music_stop();
			sound_get_or_set_music_volume(VOLUME_MIN);
		} else {
			gbMusicOn = true;
			sound_get_or_set_music_volume(VOLUME_MAX);
			music_start(GetLevelMusic(leveltype));
		}
	} else {
		const int volume = GamemenuSliderMusicSound(&sgAudioOptionsMenu[0]);
		sound_get_or_set_music_volume(volume);
		if (volume == VOLUME_MIN) {
			if (gbMusicOn) {
				gbMusicOn = false;
				music_stop();
			}
		} else if (!gbMusicOn) {
			gbMusicOn = true;
			music_start(GetLevelMusic(leveltype));
		}
	}

	GamemenuGetMusic();
}

void GamemenuSoundVolume(bool bActivate)
{
	if (bActivate) {
		if (gbSoundOn) {
			gbSoundOn = false;
			sound_stop();
			sound_get_or_set_sound_volume(VOLUME_MIN);
		} else {
			gbSoundOn = true;
			sound_get_or_set_sound_volume(VOLUME_MAX);
		}
	} else {
		const int volume = GamemenuSliderMusicSound(&sgAudioOptionsMenu[1]);
		sound_get_or_set_sound_volume(volume);
		if (volume == VOLUME_MIN) {
			if (gbSoundOn) {
				gbSoundOn = false;
				sound_stop();
			}
		} else if (!gbSoundOn) {
			gbSoundOn = true;
		}
	}
	PlaySFX(SfxID::MenuMove);
	GamemenuGetSound();
}

void GamemenuBrightness(bool bActivate)
{
	int brightness;
	if (bActivate) {
		brightness = UpdateBrightness(-1);
		brightness = (brightness == 0) ? 100 : 0;
	} else {
		brightness = GamemenuSliderBrightness();
	}

	UpdateBrightness(brightness);
	GamemenuGetBrightness();
}

void GamemenuSpeed(bool bActivate)
{
	if (bActivate) {
		if (sgGameInitInfo.nTickRate != 20)
			sgGameInitInfo.nTickRate = 20;
		else
			sgGameInitInfo.nTickRate = 50;
		gmenu_slider_set(&sgOptionsMenu[2], 20, 50, sgGameInitInfo.nTickRate);
	} else {
		sgGameInitInfo.nTickRate = gmenu_slider_get(&sgOptionsMenu[2], 20, 50);
	}

	GetOptions().Gameplay.tickRate.SetValue(sgGameInitInfo.nTickRate);
	gnTickDelay = 1000 / sgGameInitInfo.nTickRate;
}

} // namespace

void gamemenu_exit_game(bool bActivate)
{
	GamemenuNewGame(bActivate);
}

void gamemenu_quit_game(bool bActivate)
{
	GamemenuNewGame(bActivate);
#ifndef NOEXIT
	gbRunGameResult = false;
#else
	ReturnToMainMenu = true;
#endif
}

void gamemenu_load_game(bool /*bActivate*/)
{
	EventHandler saveProc = SetEventHandler(DisableInputEventHandler);
	gamemenu_off();
	ClearFloatingNumbers();
	NewCursor(CURSOR_NONE);
	InitDiabloMsg(EMSG_LOADING);
	RedrawEverything();
	DrawAndBlit();

	const std::array<SDL_Color, 256> prevPalette = logical_palette;
#ifndef USE_SDL1
	DeactivateVirtualGamepad();
	FreeVirtualGamepadTextures();
#endif
	if (std::expected<void, std::string> result = LoadGame(false); !result.has_value()) {
		app_fatal(result.error());
	}
#if !defined(USE_SDL1) && !defined(__vita__)
	if (renderer != nullptr) {
		InitVirtualGamepadTextures(*renderer);
	}
#endif
	ClrDiabloMsg();
	PaletteFadeOut(8, prevPalette);

	LoadPWaterPalette();
	NewCursor(CURSOR_HAND);
	CornerStone.activated = false;
	MyPlayerIsDead = false;
	RedrawEverything();
	DrawAndBlit();
	PaletteFadeIn(8);
	NewCursor(CURSOR_HAND);
	interface_msg_pump();
	SetEventHandler(saveProc);
}

void gamemenu_save_game(bool /*bActivate*/)
{
	if (pcurs != CURSOR_HAND) {
		return;
	}

	if (MyPlayer->_pmode == PM_DEATH || MyPlayerIsDead) {
		gamemenu_off();
		return;
	}

	EventHandler saveProc = SetEventHandler(DisableInputEventHandler);
	NewCursor(CURSOR_NONE);
	gamemenu_off();
	InitDiabloMsg(EMSG_SAVING);
	RedrawEverything();
	DrawAndBlit();
	const uint32_t currentTime = SDL_GetTicks();
	SaveGame();
	ClrDiabloMsg();
	InitDiabloMsg(EMSG_GAME_SAVED, currentTime + 1000 - SDL_GetTicks());
	RedrawEverything();
	NewCursor(CURSOR_HAND);
	if (CornerStone.activated) {
		CornerstoneSave();
		if (!demo::IsRunning()) SaveOptions();
	}
	interface_msg_pump();
	SetEventHandler(saveProc);
}

void gamemenu_on()
{
	isGameMenuOpen = true;
	if (!gbIsMultiplayer) {
		gmenu_set_items(sgSingleMenu, GamemenuUpdateSingle);
	} else {
		gmenu_set_items(sgMultiMenu, nullptr);
	}
	PressEscKey();
}

void gamemenu_off()
{
	isGameMenuOpen = false;
	gmenu_set_items(nullptr, nullptr);
}

void gamemenu_handle_previous()
{
	if (gmenu_is_active())
		gamemenu_off();
	else
		gamemenu_on();
}

} // namespace devilution
