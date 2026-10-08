// Finite frontend checks with the real menu sources, fonts and native input.
// Only browser launch and frame scheduling are intercepted. No game is started.
#if defined(USE_SDL1) || defined(USE_SDL3)
#include <iostream>
int main()
{
	std::cout << "SKIP: the window-free frontend fixture requires SDL2\n";
	return 77;
}
#else
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "DiabloUI/diabloui.h"
#include "diablo.h"
#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/render/d3d_menu_presentation.hpp"
#include "engine/render/text_render.hpp"
#include "engine/sound.h"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "options.h"
#include "tables/playerdat.hpp"
#include "utils/language.h"
#include "utils/paths.h"
#include "utils/png.h"
#include "utils/sdl_ptrs.h"

namespace devilution {
void FixturePollAndRender();
void FixtureFadeIn();
void FixtureInitList(void (*focus)(size_t), void (*select)(size_t), void (*esc)(),
    const std::vector<std::unique_ptr<UiItemBase>> &items, bool wraps,
    void (*fullscreen)(), bool (*yesNo)(), size_t selected);
bool FixtureCreditsDialog();
bool FixtureSupportDialog();
void FixtureDiablo3DCreditsDialog();
}
int FixtureOpenURL(const char *url);

// libdevilutionx is an OBJECT library. Rename every public definition in these
// included sources, so the original objects remain linkable without duplicates.
// The implementation under test is read directly; it is not copied into a mock.
#define UiMainMenuDialog FixtureMainMenuDialog
#define UiSupportAndCreditsDialog FixtureSupportAndCreditsDialog
#define mainmenu_restart_repintro FixtureRestartRepIntro
#define UiCreditsDialog FixtureCreditsDialog
#define UiSupportDialog FixtureSupportDialog
#define UiDiablo3DCreditsDialog FixtureDiablo3DCreditsDialog
#define UiPollAndRender FixturePollAndRender
#define UiInitList FixtureInitList
#include "../Source/DiabloUI/mainmenu.cpp"
#define UiFadeIn FixtureFadeIn
#define SDL_OpenURL FixtureOpenURL
#include "../Source/DiabloUI/credits.cpp"
#undef SDL_OpenURL
#undef UiFadeIn
#undef UiInitList
#undef UiPollAndRender
#undef UiDiablo3DCreditsDialog
#undef UiSupportDialog
#undef UiCreditsDialog
#undef mainmenu_restart_repintro
#undef UiSupportAndCreditsDialog
#undef UiMainMenuDialog

namespace {
using namespace devilution;
size_t Checks = 0;
size_t Frames = 0;
size_t NativeCreditsFrames = 0;
size_t NativeSupportFrames = 0;
bool BrowserSucceeds = true;
std::vector<std::string> RequestedLinks;
std::vector<std::string> Captures;
std::vector<UiItemBase *> FixtureActiveItems;
UiList *FixtureActiveList = nullptr;
std::function<void()> BeforeFrame;
std::function<void()> AfterFrame;
std::filesystem::path Output;
SDL_Surface *OutputSurface = nullptr;

void Check(bool value, std::string_view message)
{
	++Checks;
	std::cout << (value ? "PASS " : "FAIL ") << message << '\n';
	if (!value)
		throw std::runtime_error(std::string(message));
}

void PushKey(SDL_Keycode key)
{
	SDL_Event event {};
	event.type = SDL_KEYDOWN;
	event.key.keysym.sym = key;
	event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
	Check(SDL_PushEvent(&event) == 1, "queue a native keyboard press");
	event.type = SDL_KEYUP;
	Check(SDL_PushEvent(&event) == 1, "queue a native keyboard release");
}

void PushClick(size_t index)
{
	Check(FixtureActiveList != nullptr && index < FixtureActiveList->m_vecItems.size(), "click targets an active native row");
	const SDL_Rect rect = FixtureActiveList->itemRect(static_cast<int>(index));
	SDL_Event event {};
	event.type = SDL_MOUSEBUTTONDOWN;
	event.button.button = SDL_BUTTON_LEFT;
	event.button.x = rect.x + rect.w / 2;
	event.button.y = rect.y + rect.h / 2;
	Check(SDL_PushEvent(&event) == 1, "queue a native mouse press");
	event.type = SDL_MOUSEBUTTONUP;
	Check(SDL_PushEvent(&event) == 1, "queue a native mouse release");
}

void CheckLayout()
{
	Check(FixtureActiveList != nullptr, "screen has a native input list");
	for (const auto *item : FixtureActiveItems) {
		if (item->IsType(UiType::ArtText) || item->IsType(UiType::List)) {
			const SDL_Rect rect = item->m_rect;
			Check(rect.x >= 0 && rect.y >= 0 && rect.x + rect.w <= gnScreenWidth && rect.y + rect.h <= gnScreenHeight,
			    "text and clickable rectangles stay inside the screen");
		}
	}
	const int focus = FixtureActiveList->m_height >= 42 ? FOCUS_BIG : FixtureActiveList->m_height >= 30 ? FOCUS_MED : FOCUS_SMALL;
	const int width = FixtureActiveList->m_width - 2 * (*ArtFocus[focus])[0].width();
	for (const auto *item : FixtureActiveList->m_vecItems) {
		std::string_view remaining = item->m_text.str();
		const auto font = GetFontSizeFromUiFlags(FixtureActiveList->GetFlags() | item->uiFlags);
		size_t lines = 0;
		do {
			const auto end = remaining.find('\n');
			const auto line = remaining.substr(0, end);
			Check(GetLineWidth(line, font, FixtureActiveList->GetSpacing()) <= width, "full row label fits between the native selectors");
			++lines;
			if (end == std::string_view::npos)
				break;
			remaining.remove_prefix(end + 1);
		} while (true);
		Check(lines * GetLineHeight(item->m_text.str(), font) <= FixtureActiveList->m_height, "wrapped row label fits its clickable height");
	}
}

void Capture(const std::string &name)
{
	UiClearScreen();
	UiRenderListItems();
	system_palette = logical_palette;
	SDL_SetPaletteColors(PalSurface->format->palette, logical_palette.data(), 0, 256);
	if (IsD3dMainMenuActive()) {
		SetD3dMainMenuFade(256);
		Check(RenderD3dMainMenu(renderer, PalSurface), "shared menu background composes in software");
	} else {
		Check(SDL_BlitSurface(PalSurface, nullptr, OutputSurface, nullptr) == 0, "native screen copies to the software capture");
	}
	Check(IMG_SavePNG(OutputSurface, (Output / name).string().c_str()) == 0, "write a private technical UI capture");
	Captures.push_back(name);
}

void Scenario(std::function<void()> before, std::function<void()> after = {})
{
	Frames = 0;
	BeforeFrame = std::move(before);
	AfterFrame = std::move(after);
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}

void CheckMainMenu(const std::string &label, int expectedWidth = -1)
{
	MainMenuSelectedItem = 0;
	Scenario([&] {
		Check(FixtureActiveList->m_vecItems.size() == 5, "main menu exposes five actions");
		Check(FixtureActiveList->GetItem(3)->m_value == MAINMENU_SUPPORT_AND_CREDITS, "grouped item dispatches the project submenu");
		std::string groupedLabel(FixtureActiveList->GetItem(3)->m_text.str());
		std::replace(groupedLabel.begin(), groupedLabel.end(), '\n', ' ');
		Check(groupedLabel == _("Project Support and Credits"), "grouped label preserves every translated word when wrapping");
		if (expectedWidth >= 0)
			Check(FixtureActiveList->m_width == expectedWidth, "private Godot override reaches the actual native list width");
		CheckLayout();
		Capture(label + "-main.png");
		for (int i = 0; i < 3; ++i)
			PushKey(SDLK_DOWN);
		PushKey(SDLK_RETURN);
	});
	_mainmenu_selections result = MAINMENU_NONE;
	FixtureMainMenuDialog("Diablo 3D fixture", &result, 3600);
	Check(result == MAINMENU_SUPPORT_AND_CREDITS && MainMenuSelectedItem == 3, "keyboard opens the grouped menu and preserves its main-menu focus");
	Scenario([&] {
		Check(SelectedItem == 3, "return to main menu keeps the grouped item selected");
		PushClick(3);
	});
	FixtureMainMenuDialog("Diablo 3D fixture", &result, 3600);
	Check(result == MAINMENU_SUPPORT_AND_CREDITS, "single mouse click opens the grouped menu");
}

void CheckNativeReturn(bool credits, const std::string &captureName = {})
{
	int step = 0;
	const size_t wanted = credits ? 1 : 0;
	const size_t initialFrames = credits ? NativeCreditsFrames : NativeSupportFrames;
	Scenario([&] {
		Check(FixtureActiveList->m_vecItems.size() == 4, "submenu contains Support, inherited Credits, project Credits and Back");
		if (step == 0) {
			CheckLayout();
			if (!captureName.empty())
				Capture(captureName);
			if (credits) {
				PushKey(SDLK_DOWN);
				PushKey(SDLK_RETURN);
			} else {
				PushClick(0);
			}
			++step;
		} else {
			Check(SelectedItem == wanted, "inherited dialog returns to the same selected submenu row");
			PushKey(SDLK_ESCAPE);
			++step;
		}
	}, [&] {
		if (step == 1)
			PushKey(SDLK_ESCAPE); // consumed by the existing scrolling TextDialog
	});
	FixtureSupportAndCreditsDialog();
	Check(step == 2, "Esc returns from the reconstructed submenu");
	Check((credits ? NativeCreditsFrames : NativeSupportFrames) > initialFrames, "the inherited scrolling dialog actually rendered");
}

void CheckProjectLinks(const std::string &label)
{
	int step = 0;
	RequestedLinks.clear();
	BrowserSucceeds = true;
	Scenario([&] {
		if (step == 0) {
			PushKey(SDLK_DOWN);
			PushKey(SDLK_DOWN);
			PushKey(SDLK_RETURN);
		} else if (step == 1) {
			CheckLayout();
			for (size_t i = 0; i < ProjectLinks.size(); ++i)
				Check(FixtureActiveList->GetItem(i)->m_text.str() == ProjectLinks[i], "official address is visible and selectable");
			Capture(label + "-project-credits.png");
			PushKey(SDLK_RETURN);
		} else if (step == 2) {
			Check(RequestedLinks.size() == 1 && RequestedLinks[0] == ProjectLinks[0] && SelectedItem == 0, "keyboard requests GitHub and keeps focus after returning");
			PushClick(1);
		} else if (step == 3) {
			Check(RequestedLinks.size() == 2 && RequestedLinks[1] == ProjectLinks[1] && SelectedItem == 1, "mouse requests the official site and keeps focus");
			BrowserSucceeds = false;
			PushClick(2);
		} else if (step == 4) {
			Check(RequestedLinks.size() == 3 && RequestedLinks[2] == ProjectLinks[2] && SelectedItem == 2, "Discord uses the exact authorized invite");
			bool foundError = false;
			for (const auto *item : FixtureActiveItems)
				if (item->IsType(UiType::ArtText))
					foundError |= static_cast<const UiArtText *>(item)->GetText() == _("Could not open a browser. Use the addresses shown above.");
			Check(foundError, "browser failure is explained while addresses remain available");
			CheckLayout();
			Capture(label + "-project-credits-link-failure.png");
			PushKey(SDLK_ESCAPE);
		} else if (step == 5) {
			Check(SelectedItem == 2, "project Credits returns to its selected submenu item");
			PushClick(3);
		} else {
			Check(false, "frontend scenario is finite");
		}
		++step;
	});
	FixtureSupportAndCreditsDialog();
	Check(step == 6, "mouse Back closes the submenu after project Credits");
}

struct Frame {
	SDLSurfaceUniquePtr output;
	SDLSurfaceUniquePtr ui;
	SDL_Renderer *target = nullptr;
	Frame(int width, int height)
	    : output(SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32))
	    , ui(SDL_CreateRGBSurfaceWithFormat(0, width, height, 8, SDL_PIXELFORMAT_INDEX8))
	{
		Check(output && ui, "allocate private software surfaces");
		target = SDL_CreateSoftwareRenderer(output.get());
		Check(target != nullptr, "create a window-free software renderer");
		renderer = target;
		PalSurface = ui.get();
		OutputSurface = output.get();
		AdjustToScreenGeometry({ width, height });
	}
	~Frame()
	{
		UiInitList_clear();
		SetD3dMainMenuActive(false);
		ResetD3dMainMenuResources();
		PalSurface = nullptr;
		renderer = nullptr;
		OutputSurface = nullptr;
		SDL_DestroyRenderer(target);
	}
};

void InitializeArchives(const std::filesystem::path &base)
{
	LoadCoreArchives();
	std::filesystem::path selected;
	for (const char *name : { "DIABDAT.MPQ", "diabdat.mpq", "spawn.mpq", "SPAWN.MPQ" })
		if (std::filesystem::is_regular_file(base / name)) {
			selected = base / name;
			break;
		}
	Check(!selected.empty(), "explicit read-only game-data directory contains a game archive");
	auto archive = MpqArchive::Open(selected.string().c_str());
	Check(archive.has_value(), "open the explicitly selected archive");
	MpqArchives.insert_or_assign(MainMpqPriority, std::move(*archive));
	gbIsSpawn = selected.filename() == "spawn.mpq" || selected.filename() == "SPAWN.MPQ";
}
} // namespace

int FixtureOpenURL(const char *url)
{
	RequestedLinks.emplace_back(url);
	return BrowserSucceeds ? 0 : -1;
}

namespace devilution {
void FixtureInitList(void (*focus)(size_t), void (*select)(size_t), void (*esc)(),
    const std::vector<std::unique_ptr<UiItemBase>> &items, bool wraps,
    void (*fullscreen)(), bool (*yesNo)(), size_t selected)
{
	if (IsD3dMainMenuActive()) {
		// Headless mode skips LoadPalette; captures still need the real UI colors.
		const bool wasHeadless = HeadlessMode;
		HeadlessMode = false;
		LoadPalette("ui_art\\diablo.pal");
		HeadlessMode = wasHeadless;
	}
	FixtureActiveItems.clear();
	FixtureActiveList = nullptr;
	for (const auto &item : items) {
		FixtureActiveItems.push_back(item.get());
		if (item->IsType(UiType::List))
			FixtureActiveList = static_cast<UiList *>(item.get());
	}
	UiInitList(focus, select, esc, items, wraps, fullscreen, yesNo, selected);
}

void FixturePollAndRender()
{
	Check(++Frames <= 64, "frontend finishes within the finite frame budget");
	if (BeforeFrame)
		BeforeFrame();
	UiPollAndRender();
	if (AfterFrame)
		AfterFrame();
}

void FixtureFadeIn()
{
	if (SupportMenuResult == SupportMenuAction::Credits) {
		Check(++NativeCreditsFrames <= 64, "inherited Credits exits within the finite frame budget");
		Check(!IsD3dMainMenuActive() && ArtBackground.has_value(), "inherited Credits keeps its native background");
	} else if (SupportMenuResult == SupportMenuAction::Support) {
		Check(++NativeSupportFrames <= 64, "inherited Support exits within the finite frame budget");
	}
	UiFadeIn();
}
} // namespace devilution

int main(int argc, char **argv)
{
	using namespace devilution;
	std::cout << std::unitbuf;
	if (argc != 4) {
		std::cerr << "Usage: project_credits_smoke <game-data-directory> <built-assets-directory> <private-output-parent>\n";
		return 2;
	}
	const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
	Output = std::filesystem::absolute(argv[3]) / ("project-credits-" + std::to_string(nonce));
	const auto profile = Output / "private-profile";
	std::filesystem::create_directories(profile);
	paths::SetBasePath(argv[1]);
	paths::SetAssetsPath(argv[2]);
	paths::SetPrefPath(profile.string());
	paths::SetConfigPath(profile.string());
	SDL_SetMainReady();
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
		return 2;
	int status = 0;
	try {
		HeadlessMode = true;
		gbIsHellfire = false;
		gbMusicOn = gbSoundOn = false;
		InitializeArchives(std::filesystem::absolute(argv[1]));
		OverridePaths.emplace_back(paths::PrefPath());
		InitKeymapActions();
		LoadOptions();
		GetOptions().Graphics.hardwareCursor.SetValue(false);
		LoadPlayerDataFiles();
		UiInitialize();
		Check(ArtFocus[FOCUS_SMALL] && ArtFocus[FOCUS_MED] && ArtFocus[FOCUS_BIG], "real native selectors load");
		Check(HasTranslation("pt_BR"), "built assets include the real Portuguese catalog");
		for (const char *language : { "en", "pt_BR" }) {
			forceLocale = language;
			LanguageInitialize();
			Check(GetLanguageCode() == language, "requested language is effective");
			for (const Size size : { Size { 640, 480 }, Size { 960, 540 }, Size { 1920, 1080 } }) {
				Frame frame(size.width, size.height);
				const std::string label = std::string(language) + "-" + std::to_string(size.width) + "x" + std::to_string(size.height);
				CheckMainMenu(label);
				const bool captureSubmenu = std::string_view(language) == "pt_BR" && (size.width == 640 || size.width == 1920);
				CheckNativeReturn(false, captureSubmenu ? label + "-support-credits-submenu.png" : std::string {});
				CheckNativeReturn(true);
				CheckProjectLinks(label);
				const auto layout = profile / "d3d-ui" / "layout.ini";
				std::filesystem::create_directories(layout.parent_path());
				{
					std::ofstream file(layout);
					file << "[Layout]\nformat=d3d.ui-layout\nschemaVersion=1\n[MenuList]\nanchor=center\noffsetX=-150\noffsetY=-48\nwidth=300\nheight=258\n";
					Check(file.good(), "write minimum-width Godot v1 layout only in the private fixture profile");
				}
				CheckMainMenu(label + "-godot-minimum-width", 300);
				Check(std::filesystem::remove(layout), "remove only the fixture-owned layout override");
			}
		}
		BeforeFrame = AfterFrame = {};
		std::ofstream receipt(Output / "receipt.json");
		receipt << "{\"kind\":\"native-project-credits-fixture\",\"checks\":" << (Checks + 1)
		        << ",\"checksBeforeReceiptWrite\":" << Checks << ",\"receiptWriteCheckIncluded\":true"
		        << ",\"externalBrowserLaunches\":0,\"browserApi\":\"local-success-and-failure-stub\",\"input\":[\"native-keyboard-events\",\"native-mouse-events\",\"Esc\"],\"nativeCreditsFrames\":" << NativeCreditsFrames
		        << ",\"nativeSupportFrames\":" << NativeSupportFrames
		        << ",\"gameLoopStarted\":false,\"audioPlaybackTested\":false,\"gamepadDeviceTested\":false,\"savesWritten\":false,\"configurationWrites\":\"private-fixture-profile-only\",\"captures\":[";
		for (size_t i = 0; i < Captures.size(); ++i)
			receipt << (i ? "," : "") << '"' << Captures[i] << '"';
		receipt << "]}\n";
		Check(receipt.good(), "write private fixture receipt");
		std::cout << "PASS " << Checks << " checks; private captures: " << Output.string() << '\n';
	} catch (const std::exception &error) {
		std::cerr << "Frontend fixture stopped: " << error.what() << '\n';
		status = 1;
	}
	UiInitList_clear();
	UiDestroy();
	SDL_Quit();
	return status;
}
#endif
