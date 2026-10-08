#include "ingame_settings.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#endif

#include "control/control.hpp"
#include "controls/controller.h"
#include "controls/control_mode.hpp"
#include "controls/controller_motion.h"
#include "controls/game_controls.h"
#include "controls/plrctrls.h"
#include "controls/remap_keyboard.h"
#include "diablo.h"
#include "effects.h"
#include "engine/assets.hpp"
#include "engine/backbuffer_state.hpp"
#include "engine/demomode.h"
#include "engine/events.hpp"
#include "engine/palette.h"
#include "engine/render/scrollrt.h"
#include "engine/sound.h"
#include "engine/sound_defs.hpp"
#include "game_mode.hpp"
#include "gamemenu.h"
#include "gmenu.h"
#include "multi.h"
#include "options.h"
#include "utils/enum_traits.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"

namespace devilution {
namespace {

constexpr size_t MaxEntriesPerPage = GMenuSettingsMaxContentRows;
size_t EntriesPerPage = 5;
enum class View { Categories, Entries, Values, Binding, Slider };
enum class Special { None, MusicVolume, SoundVolume, CuesVolume, Gamma, Speed };
struct Entry {
	OptionEntryBase *option;
	Special special = Special::None;
};

bool Active = false;
bool Capturing = false;
EventHandler PreviousEventHandler = nullptr;
View CurrentView = View::Categories;
OptionCategoryBase *Category = nullptr;
Entry Selected { nullptr };
size_t CategoryPage = 0;
size_t EntryPage = 0;
size_t ValuePage = 0;
std::vector<OptionCategoryBase *> Categories;
std::vector<Entry> Entries;
std::array<TMenuItem, MaxEntriesPerPage + 4> Menu;
std::array<std::string, MaxEntriesPerPage + 3> Labels;
std::array<std::string, MaxEntriesPerPage + 3> Values;
std::array<std::string, MaxEntriesPerPage + 3> Descriptions;
std::string Title;
std::string Notice;
ControllerButtonCombo PadCombo;
uint32_t CaptureStarted = 0;
SDL_Keycode PendingKeyRelease = SDLK_UNKNOWN;
uint8_t PendingMouseRelease = 0;
size_t ReturnEntryIndex = 0;
size_t ReturnCategoryIndex = 0;

void Show(size_t focus = 0);
void Back();
void ChangePage(bool next);
void SelectRow(size_t row, bool activate);
void SettingsEventHandler(const SDL_Event &event, uint16_t modState);

void SavePreferences()
{
	if (!demo::IsRunning())
		SaveOptions();
}

bool IsVisible(const OptionEntryBase &option)
{
	const auto flags = option.GetFlags();
	if (HasAnyOf(flags, OptionEntryFlags::NeedDiabloMpq) && !HaveIntro())
		return false;
	return HasNoneOf(flags, OptionEntryFlags::Invisible | (gbIsHellfire ? OptionEntryFlags::OnlyDiablo : OptionEntryFlags::OnlyHellfire));
}

std::string_view LockReason(const Entry &entry)
{
	if (!gbSndInited && IsAnyOf(entry.special, Special::MusicVolume, Special::SoundVolume, Special::CuesVolume))
		return _("Sound Disabled");
	if (entry.special == Special::Speed && gbIsMultiplayer)
		return _("This setting is controlled by the multiplayer session.");
	// Explicit quick controls are Invisible in Settings, but already supported
	// by the native in-game menu. Do not change their flags in the shared model.
	if (entry.special != Special::None)
		return {};
	const auto flags = entry.option->GetFlags();
	if (HasAnyOf(flags, OptionEntryFlags::CantChangeInGame))
		return _("Leave the current game and change this setting in the main menu.");
	if (gbIsMultiplayer && HasAnyOf(flags, OptionEntryFlags::CantChangeInMultiPlayer))
		return _("Leave the multiplayer game and change this setting in the main menu.");
	if (HasAnyOf(flags, OptionEntryFlags::RecreateUI))
		return _("This setting recreates the interface. Change it in the main menu.");
	return {};
}

bool IsInactiveDuringPlay(const OptionEntryBase *option)
{
	const Options &options = GetOptions();
	return option == &options.Graphics.townViewStartIn3D || option == &options.Gameplay.runInTown
	    || option == &options.Gameplay.friendlyFire || option == &options.Gameplay.multiplayerFullQuests
	    // Party information has no solo effect and is locked in multiplayer.
	    || option == &options.Gameplay.showMultiplayerPartyInfo;
}

bool IsEditable(const Entry &entry, const OptionCategoryBase *category)
{
	const Options &options = GetOptions();
	if (category == &options.StartUp || (!gbSndInited && (category == &options.Audio || category == &options.Music)))
		return false;
	if (!LockReason(entry).empty())
		return false;
	if (entry.special != Special::None)
		return true;
	if (!IsVisible(*entry.option) || IsInactiveDuringPlay(entry.option))
		return false;
	return entry.option->GetType() != OptionEntryType::List
	    || static_cast<const OptionEntryListBase *>(entry.option)->GetListSize() > 1;
}

std::vector<Entry> EditableEntries(OptionCategoryBase *category)
{
	Options &options = GetOptions();
	// These preferences affect boot/session initialization only. Keep them in
	// the main Settings; the in-game list contains usable controls exclusively.
	if (category == &options.StartUp || (!gbSndInited && (category == &options.Audio || category == &options.Music)))
		return {};
	std::vector<Entry> entries;
	const auto add = [&entries, category](Entry entry) {
		if (IsEditable(entry, category))
			entries.push_back(entry);
	};
	if (category == &options.Audio) {
		add({ &options.Audio.musicVolume, Special::MusicVolume });
		add({ &options.Audio.soundVolume, Special::SoundVolume });
		add({ &options.Audio.audioCuesVolume, Special::CuesVolume });
	} else if (category == &options.Graphics) {
		add({ &options.Graphics.brightness, Special::Gamma });
	} else if (category == &options.Gameplay) {
		add({ &options.Gameplay.tickRate, Special::Speed });
	}
	for (auto *option : category->GetEntries())
		add({ option });
	return entries;
}

std::string_view Application()
{
	const Options &options = GetOptions();
	if (Category == &options.Music)
		return _("Changes the current location immediately; other locations use the choice when entered.");
	return _("Applies immediately.");
}

std::string EntryDescription(const Entry &entry)
{
	std::string text(entry.option->GetDescription());
	switch (entry.special) {
	case Special::MusicVolume: text = _("Adjust music volume or mute music."); break;
	case Special::SoundVolume: text = _("Adjust sound volume or mute sound."); break;
	case Special::CuesVolume: text = _("Adjust the volume of navigation audio cues."); break;
	case Special::Gamma: text = _("Adjust brightness correction."); break;
	case Special::Speed: text = _("Adjust the speed of the single-player game."); break;
	default: break;
	}
	const auto reason = LockReason(entry);
	// Put application/lock information first so even a two-line description
	// reserve at 480p cannot hide the reason behind a long option description.
	return std::string(reason.empty() ? Application() : reason).append("\n").append(text);
}

std::string_view EntryName(const Entry &entry)
{
	switch (entry.special) {
	case Special::MusicVolume: return _("Music");
	case Special::SoundVolume: return _("Sound");
	case Special::CuesVolume: return _("Audio Cues Volume");
	case Special::Gamma: return _("Gamma");
	case Special::Speed: return _("Speed");
	default: return entry.option->GetName();
	}
}

size_t ShownListIndex(const OptionEntryListBase &option)
{
	const auto &music = GetOptions().Music;
	if (Category == &music && &option != &music.theme) {
		if (*music.theme == MusicTheme::Vanilla) return 0;
		if (*music.theme == MusicTheme::Rock) return &option == &music.town ? 3 : 1;
	}
	return option.GetActiveListIndex();
}

std::string EntryValue(const Entry &entry)
{
	if (entry.special == Special::Speed)
		return std::to_string(sgGameInitInfo.nTickRate);
	if (entry.special == Special::Gamma)
		return std::to_string(*GetOptions().Graphics.brightness);
	if (entry.special != Special::None) {
		const int volume = **static_cast<OptionEntryInt<int> *>(entry.option);
		return std::to_string((volume - VOLUME_MIN) * 100 / (VOLUME_MAX - VOLUME_MIN)) + "%";
	}
	if (entry.option->GetType() == OptionEntryType::List && static_cast<OptionEntryListBase *>(entry.option)->GetListSize() == 0)
		return std::string(_("None"));
	if (Category == &GetOptions().Music && entry.option->GetType() == OptionEntryType::List) {
		const auto *option = static_cast<OptionEntryListBase *>(entry.option);
		return std::string(option->GetListDescription(ShownListIndex(*option)));
	}
	return std::string(entry.option->GetValueDescription());
}

void BuildCategories()
{
	Categories.clear();
	Options &options = GetOptions();
	const auto all = options.GetCategories();
	// The first page contains the most useful categories, including the direct
	// Soundtrack entry. Each category still comes from the shared model.
	const std::array<OptionCategoryBase *, 6> priority {
		&options.Music, &options.Graphics, &options.Audio, &options.Gameplay, &options.Keymapper, &options.Padmapper
	};
	const auto add = [](OptionCategoryBase *category) {
		if (!EditableEntries(category).empty())
			Categories.push_back(category);
	};
	for (auto *category : priority)
		add(category);
	for (auto *category : all) {
		if (std::find(priority.begin(), priority.end(), category) == priority.end())
			add(category);
	}
}

void BuildEntries()
{
	Entries = EditableEntries(Category);
}

std::string_view Describe(size_t row)
{
	if (row >= Descriptions.size())
		return {};
	return Descriptions[row];
}

template <size_t Row>
void RowHandler(bool activate)
{
	SelectRow(Row, activate);
}

template <size_t... Rows>
constexpr auto MakeRowHandlers(std::index_sequence<Rows...>)
{
	return std::array<void (*)(bool), sizeof...(Rows)> { &RowHandler<Rows>... };
}

constexpr auto RowHandlers = MakeRowHandlers(std::make_index_sequence<MaxEntriesPerPage> {});

void PreviousPage(bool) { ChangePage(false); }
void NextPage(bool) { ChangePage(true); }
void PreviousMenu(bool) { Back(); }

void AddRow(size_t row, std::string_view label, std::string_view value, std::string_view description, void (*handler)(bool))
{
	Labels[row] = label;
	Values[row] = value;
	Descriptions[row] = description;
	Menu[row] = { GMENU_ENABLED, Labels[row].c_str(), handler, Values[row].c_str() };
}

void StopCapture()
{
	Capturing = false;
	CaptureStarted = 0;
	PadCombo = ControllerButton_NONE;
}

size_t PageCapacity()
{
	return std::clamp(gmenu_settings_page_size({ gnScreenWidth, gnScreenHeight }, GetMainPanel().position.y), size_t { 1 }, MaxEntriesPerPage);
}

void UpdateCapture()
{
	if (EntriesPerPage != PageCapacity()) {
		Show(gmenu_selected_index());
		return;
	}
	if (!Capturing)
		return;
	// The menu's stick navigation must not move focus during binding capture.
	gmenu_select_index(0);
	if (SDL_GetTicks() - CaptureStarted >= 10000) {
		StopCapture();
		Notice = _("Input cancelled.");
		Show();
	}
}

void ConfigureSlider()
{
	TMenuItem &item = Menu[0];
	item.addFlags(GMENU_SLIDER);
	int minimum = VOLUME_MIN;
	int maximum = VOLUME_MAX;
	int value = **static_cast<OptionEntryInt<int> *>(Selected.option);
	int steps = VOLUME_STEPS;
	if (Selected.special == Special::Gamma) {
		minimum = 0;
		maximum = 100;
		value = UpdateBrightness(-1);
		steps = 21;
	} else if (Selected.special == Special::Speed) {
		minimum = 20;
		maximum = 50;
		value = sgGameInitInfo.nTickRate;
		steps = 46;
	}
	gmenu_slider_steps(&item, steps);
	gmenu_slider_set(&item, minimum, maximum, value);
}

void Show(size_t focus)
{
	const size_t capacity = PageCapacity();
	if (capacity != EntriesPerPage) {
		size_t *page = nullptr;
		size_t total = 0;
		switch (CurrentView) {
		case View::Categories: page = &CategoryPage; total = Categories.size(); break;
		case View::Entries: page = &EntryPage; total = Entries.size(); break;
		case View::Values: page = &ValuePage; total = static_cast<OptionEntryListBase *>(Selected.option)->GetListSize(); break;
		default: break;
		}
		// Resize preserves the selected content's absolute index. Navigation
		// buttons clamp to the final content row of the old page.
		const size_t selected = page != nullptr
		    ? std::min(*page * EntriesPerPage + std::min(focus, EntriesPerPage - 1), std::max(total, size_t { 1 }) - 1)
		    : 0;
		CategoryPage = CategoryPage * EntriesPerPage / capacity;
		EntryPage = EntryPage * EntriesPerPage / capacity;
		ValuePage = ValuePage * EntriesPerPage / capacity;
		EntriesPerPage = capacity;
		if (page != nullptr) {
			*page = selected / EntriesPerPage;
			focus = selected % EntriesPerPage;
		}
	}
	if (CurrentView != View::Categories && EditableEntries(Category).empty()) {
		StopCapture();
		CurrentView = View::Categories;
		focus = 0;
	}
	if (IsAnyOf(CurrentView, View::Values, View::Binding, View::Slider) && !IsEditable(Selected, Category)) {
		StopCapture();
		CurrentView = View::Entries;
		EntryPage = ReturnEntryIndex / EntriesPerPage;
		focus = ReturnEntryIndex % EntriesPerPage;
	}
	for (size_t row = 0; row < Descriptions.size(); ++row)
		Descriptions[row].clear();
	size_t count = 0;
	size_t navigationItems = 1;
	size_t total = 0;
	size_t *page = nullptr;
	switch (CurrentView) {
	case View::Categories:
		BuildCategories();
		Title = _("Settings");
		page = &CategoryPage;
		total = Categories.size();
		break;
	case View::Entries:
		BuildEntries();
		Title = Category->GetName();
		page = &EntryPage;
		total = Entries.size();
		break;
	case View::Values:
		Title = Selected.option->GetName();
		page = &ValuePage;
		total = static_cast<OptionEntryListBase *>(Selected.option)->GetListSize();
		break;
	case View::Binding: {
		Title = Selected.option->GetName();
		const bool key = Selected.option->GetType() == OptionEntryType::Key;
		std::string description = EntryDescription(Selected);
		if (!Notice.empty()) description.insert(0, Notice + "\n");
		AddRow(count++, Capturing ? (key ? _("Press any key to change.") : _("Press gamepad buttons to change."))
		                          : (key ? _("Bind key") : _("Bind button combo")),
		    Selected.option->GetValueDescription(), description, RowHandlers[0]);
		if (!Capturing)
			AddRow(count++, key ? _("Unbind key") : _("Unbind button combo"), {}, description, RowHandlers[1]);
		AddRow(count++, _("Previous Menu"), {}, Capturing ? _("Cancel input and keep the previous binding.") : std::string_view {}, &PreviousMenu);
		break;
	}
	case View::Slider:
		Title = EntryName(Selected);
		AddRow(count++, Title, EntryValue(Selected), EntryDescription(Selected), RowHandlers[0]);
		ConfigureSlider();
		AddRow(count++, _("Previous Menu"), {}, {}, &PreviousMenu);
		break;
	}
	if (page != nullptr) {
		const size_t pages = std::max(size_t { 1 }, (total + EntriesPerPage - 1) / EntriesPerPage);
		*page = std::min(*page, pages - 1);
		const size_t offset = *page * EntriesPerPage;
		for (size_t i = offset; i < std::min(total, offset + EntriesPerPage); ++i) {
			if (CurrentView == View::Categories) {
				auto *category = Categories[i];
				AddRow(count, category->GetName(), {}, category->GetDescription(), RowHandlers[count]);
			} else if (CurrentView == View::Entries) {
				const Entry &entry = Entries[i];
				AddRow(count, EntryName(entry), EntryValue(entry), EntryDescription(entry), RowHandlers[count]);
			} else {
				auto *option = static_cast<OptionEntryListBase *>(Selected.option);
				AddRow(count, option->GetListDescription(i), i == ShownListIndex(*option) ? "*" : "", EntryDescription(Selected), RowHandlers[count]);
			}
			++count;
		}
		if (pages > 1) {
			navigationItems = 3;
			Title.append(" (").append(std::to_string(*page + 1)).append("/").append(std::to_string(pages)).append(")");
			AddRow(count++, _("Previous Page"), {}, {}, &PreviousPage);
			AddRow(count++, _("Next Page"), {}, {}, &NextPage);
		}
		AddRow(count++, _("Previous Menu"), {}, {}, &PreviousMenu);
	}
	Menu[count] = { GMENU_ENABLED, nullptr, nullptr };
	gmenu_set_items(Menu.data(), &UpdateCapture);
	gmenu_set_settings_presentation({ Title.c_str(), &Describe, &Back, &ChangePage, navigationItems, false, Capturing });
	gmenu_select_index(std::min(focus, count - 1));
}

void ChangePage(bool next)
{
	if (Capturing)
		return;
	size_t *page = nullptr;
	size_t total = 0;
	switch (CurrentView) {
	case View::Categories: page = &CategoryPage; total = Categories.size(); break;
	case View::Entries: page = &EntryPage; total = Entries.size(); break;
	case View::Values: page = &ValuePage; total = static_cast<OptionEntryListBase *>(Selected.option)->GetListSize(); break;
	default: return;
	}
	const size_t pages = std::max(size_t { 1 }, (total + EntriesPerPage - 1) / EntriesPerPage);
	*page = next ? (*page + 1) % pages : (*page + pages - 1) % pages;
	Show();
}

void Back()
{
	if (Capturing) {
		StopCapture();
		Notice = _("Input cancelled.");
		Show();
		return;
	}
	if (CurrentView == View::Categories) {
		gamemenu_on();
		return;
	}
	if (CurrentView == View::Entries) {
		CurrentView = View::Categories;
		CategoryPage = ReturnCategoryIndex / EntriesPerPage;
		Show(ReturnCategoryIndex % EntriesPerPage);
		return;
	}
	CurrentView = View::Entries;
	EntryPage = ReturnEntryIndex / EntriesPerPage;
	Show(ReturnEntryIndex % EntriesPerPage);
}

void ApplySlider(bool activate)
{
	Options &options = GetOptions();
	if (Selected.special == Special::Speed) {
		if (gbIsMultiplayer) return;
		sgGameInitInfo.nTickRate = activate ? (sgGameInitInfo.nTickRate == 20 ? 50 : 20) : gmenu_slider_get(&Menu[0], 20, 50);
		options.Gameplay.tickRate.SetValue(sgGameInitInfo.nTickRate);
		gnTickDelay = 1000 / sgGameInitInfo.nTickRate;
	} else if (Selected.special == Special::Gamma) {
		UpdateBrightness(activate ? (UpdateBrightness(-1) == 0 ? 100 : 0) : gmenu_slider_get(&Menu[0], 0, 100));
	} else {
		if (!gbSndInited) return;
		const int current = **static_cast<OptionEntryInt<int> *>(Selected.option);
		const int volume = activate ? (current == VOLUME_MIN ? VOLUME_MAX : VOLUME_MIN) : gmenu_slider_get(&Menu[0], VOLUME_MIN, VOLUME_MAX);
		if (Selected.special == Special::MusicVolume) {
			sound_get_or_set_music_volume(volume);
			if (volume == VOLUME_MIN && gbMusicOn) {
				gbMusicOn = false;
				music_stop();
			} else if (volume != VOLUME_MIN && !gbMusicOn) {
				gbMusicOn = true;
				music_start(GetLevelMusic(leveltype));
			}
		} else if (Selected.special == Special::SoundVolume) {
			sound_get_or_set_sound_volume(volume);
			gbSoundOn = volume != VOLUME_MIN;
			if (!gbSoundOn) sound_stop();
			PlaySFX(SfxID::MenuMove);
		} else {
			SoundGetOrSetAudioCuesVolume(volume);
		}
	}
	SavePreferences();
	// Do not rebuild the item table during a drag: gmenu owns its pointer and
	// drag state until mouse release.
	Values[0] = EntryValue(Selected);
	Menu[0].value = Values[0].c_str();
	ConfigureSlider();
	RedrawEverything();
}

void SelectRow(size_t row, bool activate)
{
	if (CurrentView == View::Slider) {
		ApplySlider(activate);
		return;
	}
	if (!activate)
		return;
	switch (CurrentView) {
	case View::Categories:
		ReturnCategoryIndex = CategoryPage * EntriesPerPage + row;
		Category = Categories[ReturnCategoryIndex];
		EntryPage = 0;
		CurrentView = View::Entries;
		Show();
		break;
	case View::Entries: {
		Selected = Entries[EntryPage * EntriesPerPage + row];
		ReturnEntryIndex = EntryPage * EntriesPerPage + row;
		// Availability can change after building the page (for example audio
		// initialization). Never turn a stale row into a read-only dialog.
		if (!IsEditable(Selected, Category)) {
			Show(row);
			return;
		}
		if (Selected.special != Special::None) {
			CurrentView = View::Slider;
		} else if (Selected.option->GetType() == OptionEntryType::Boolean) {
			auto *option = static_cast<OptionEntryBoolean *>(Selected.option);
			option->SetValue(!**option);
			if (option == &GetOptions().Graphics.zoom)
				CalcViewportGeometry();
			SavePreferences();
			RedrawEverything();
		} else if (Selected.option->GetType() == OptionEntryType::List) {
			auto *option = static_cast<OptionEntryListBase *>(Selected.option);
			if (option->GetListSize() > 2 || (Category == &GetOptions().Music && option->GetListSize() != 0)) {
				ValuePage = option->GetActiveListIndex() / EntriesPerPage;
				CurrentView = View::Values;
			} else if (option->GetListSize() != 0) {
				option->SetActiveListIndex((option->GetActiveListIndex() + 1) % option->GetListSize());
				SavePreferences();
				RedrawEverything();
			}
		} else {
			CurrentView = View::Binding;
			Notice.clear();
		}
		Show(CurrentView == View::Entries ? row : 0);
		break;
	}
	case View::Values: {
		// Recheck restrictions at dispatch, not only while building the view.
		if (!IsEditable(Selected, Category)) { Back(); break; }
		auto *option = static_cast<OptionEntryListBase *>(Selected.option);
		const size_t index = ValuePage * EntriesPerPage + row;
		if (index < option->GetListSize()) {
			option->SetActiveListIndex(index);
			SavePreferences();
			RedrawEverything();
		}
		Back();
		break;
	}
	case View::Binding:
		if (Capturing) return;
		if (row == 0) {
			Capturing = true;
			// A click on Bind enters capture from inside LeftMouseDown. Its
			// release is consumed here instead of reaching LeftMouseUp.
			sgbMouseDown = CLICK_NONE;
			CaptureStarted = SDL_GetTicks();
			PadCombo = ControllerButton_NONE;
			Notice.clear();
		} else {
			if (Selected.option->GetType() == OptionEntryType::Key)
				static_cast<KeymapperOptions::Action *>(Selected.option)->SetValue(SDLK_UNKNOWN);
			else
				static_cast<PadmapperOptions::Action *>(Selected.option)->SetValue(ControllerButton_NONE);
			SavePreferences();
		}
		Show();
		break;
	default: break;
	}
}

bool FinishKeyCapture(uint32_t key)
{
	if (key == SDLK_UNKNOWN)
		return false;
	if (static_cast<KeymapperOptions::Action *>(Selected.option)->SetValue(key)) {
		StopCapture();
		Notice.clear();
		SavePreferences();
		Show();
		return true;
	}
	return false;
}

bool CaptureEvent(const SDL_Event &event)
{
	if (event.type == SDL_EVENT_KEY_DOWN && SDLC_EventKey(event) == SDLK_ESCAPE) {
		PendingKeyRelease = SDLK_ESCAPE;
		Back();
		return true;
	}
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
		// Left click remains navigation in the binding dialog, just as in the
		// main-menu Settings. In particular, Previous Menu cancels capture.
		gmenu_left_mouse(true);
		PendingMouseRelease = SDL_BUTTON_LEFT;
		return true;
	}
	const auto controllerEvents = ToControllerButtonEvents(event);
	if (Selected.option->GetType() == OptionEntryType::Key) {
		uint32_t key = SDLK_UNKNOWN;
		if (event.type == SDL_EVENT_KEY_DOWN) {
			SDL_Keycode code = SDLC_EventKey(event);
			remap_keyboard_key(&code);
			key = static_cast<uint32_t>(code);
			if (key >= SDLK_A && key <= SDLK_Z) key -= 'a' - 'A';
		} else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
			if (IsAnyOf(event.button.button, SDL_BUTTON_MIDDLE, SDL_BUTTON_X1, SDL_BUTTON_X2))
				key = event.button.button | KeymapperMouseButtonMask;
		}
#if SDL_VERSION_ATLEAST(2, 0, 0)
		else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
			if (SDLC_EventWheelIntY(event) > 0) key = MouseScrollUpButton;
			else if (SDLC_EventWheelIntY(event) < 0) key = MouseScrollDownButton;
			else if (SDLC_EventWheelIntX(event) > 0) key = MouseScrollRightButton;
			else if (SDLC_EventWheelIntX(event) < 0) key = MouseScrollLeftButton;
		}
#endif
		if (FinishKeyCapture(key)) {
			if (event.type == SDL_EVENT_KEY_DOWN)
				PendingKeyRelease = SDLC_EventKey(event);
			else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
				PendingMouseRelease = event.button.button;
		}
	} else {
		for (const auto ctrlEvent : controllerEvents) {
			DetectInputMethod(event, ctrlEvent);
			if (IsControllerMotion(event) || IsAnyOf(ctrlEvent.button, ControllerButton_NONE, ControllerButton_IGNORE)) continue;
			if (ctrlEvent.up) {
				if (PadCombo.button != ControllerButton_NONE
				    && !IsControllerButtonPressed(PadCombo.button)
				    && (PadCombo.modifier == ControllerButton_NONE || !IsControllerButtonPressed(PadCombo.modifier))) {
					const bool changed = static_cast<PadmapperOptions::Action *>(Selected.option)->SetValue(PadCombo);
					StopCapture();
					Notice = changed ? std::string {} : std::string(_("This button combination is unavailable."));
					if (changed) SavePreferences();
					Show();
					break;
				}
				continue;
			}
			if (PadCombo.button != ControllerButton_NONE && IsControllerButtonPressed(PadCombo.button))
				PadCombo.modifier = PadCombo.button;
			PadCombo.button = ctrlEvent.button;
		}
	}
	// Capture all input here before the gameplay/controller dispatch; custom,
	// quit and window events still reach the normal game handler.
	const bool controllerInput = std::any_of(controllerEvents.begin(), controllerEvents.end(), [](const auto &button) {
		return !IsAnyOf(button.button, ControllerButton_NONE, ControllerButton_IGNORE);
	});
	return IsAnyOf(event.type, SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP, SDL_EVENT_MOUSE_BUTTON_DOWN,
	    SDL_EVENT_MOUSE_BUTTON_UP, SDL_EVENT_MOUSE_MOTION)
#if SDL_VERSION_ATLEAST(2, 0, 0)
	    || event.type == SDL_EVENT_MOUSE_WHEEL
#endif
	    || controllerInput || IsControllerMotion(event);
}

void SettingsEventHandler(const SDL_Event &event, uint16_t modState)
{
	// Window changes may arrive between draws. Rebuild the page before any
	// click/key can use a geometry with a different capacity than its table.
	if (EntriesPerPage != PageCapacity())
		Show(gmenu_selected_index());
	if (event.type == SDL_EVENT_KEY_DOWN && PendingKeyRelease != SDLK_UNKNOWN && SDLC_EventKey(event) == PendingKeyRelease)
		return;
	if (event.type == SDL_EVENT_KEY_UP && PendingKeyRelease != SDLK_UNKNOWN && SDLC_EventKey(event) == PendingKeyRelease) {
		PendingKeyRelease = SDLK_UNKNOWN;
		return;
	}
	if (Capturing) {
		if (event.type == SDL_EVENT_MOUSE_MOTION)
			MousePosition = { SDLC_EventMotionIntX(event), SDLC_EventMotionIntY(event) };
		else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
			MousePosition = { SDLC_EventButtonIntX(event), SDLC_EventButtonIntY(event) };
	}
	if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && PendingMouseRelease != 0 && event.button.button == PendingMouseRelease) {
		PendingMouseRelease = 0;
		return;
	}
	if (Capturing && CaptureEvent(event)) return;
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) {
			// Extra mouse buttons otherwise bypass gmenu and invoke gameplay
			// key mappings directly. X1 is the usual back button outside capture.
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_X1)
				Back();
			return;
		}
	}
#if SDL_VERSION_ATLEAST(2, 0, 0)
	if (event.type == SDL_EVENT_MOUSE_WHEEL) {
		const int direction = SDLC_EventWheelIntY(event);
		if (direction != 0) ChangePage(direction < 0);
		return;
	}
#endif
	// Route physical controller buttons exclusively to menu navigation here.
	// The native gameplay dispatcher lets unrecognized menu buttons reach the
	// player's pad mappings, which may otherwise use items behind this dialog.
	const auto buttons = ToControllerButtonEvents(event);
	bool handledController = IsControllerMotion(event);
	for (const auto button : buttons) {
		if (IsAnyOf(button.button, ControllerButton_NONE, ControllerButton_IGNORE)) continue;
		handledController = true;
		DetectInputMethod(event, button);
		if (button.up) continue;
		switch (TranslateTo(GamepadType, button.button)) {
		case ControllerButton_BUTTON_A:
		case ControllerButton_BUTTON_Y: gmenu_presskeys(SDLK_RETURN); break;
		case ControllerButton_BUTTON_B:
		case ControllerButton_BUTTON_BACK:
		case ControllerButton_BUTTON_START: gmenu_presskeys(SDLK_ESCAPE); break;
		case ControllerButton_BUTTON_LEFTSHOULDER: ChangePage(false); break;
		case ControllerButton_BUTTON_RIGHTSHOULDER: ChangePage(true); break;
		default: break; // Stick / D-pad repeat remains in gmenu_draw.
		}
	}
	if (handledController) return;
	if (PreviousEventHandler != nullptr)
		PreviousEventHandler(event, modState);
}

} // namespace

void OpenInGameSettings()
{
	if (!Active) {
		PreviousEventHandler = SetEventHandler(&SettingsEventHandler);
		Active = true;
	}
	StopCapture();
	PendingKeyRelease = SDLK_UNKNOWN;
	PendingMouseRelease = 0;
	CategoryPage = EntryPage = ValuePage = 0;
	ReturnEntryIndex = ReturnCategoryIndex = 0;
	Category = nullptr;
	Selected = { nullptr };
	CurrentView = View::Categories;
	Show();
}

void CloseInGameSettings()
{
	if (!Active) return;
	StopCapture();
	PendingKeyRelease = SDLK_UNKNOWN;
	PendingMouseRelease = 0;
	if (CurrentEventHandler == &SettingsEventHandler)
		SetEventHandler(PreviousEventHandler);
	PreviousEventHandler = nullptr;
	Active = false;
}

bool IsInGameSettingsOpen()
{
	return Active;
}

} // namespace devilution
