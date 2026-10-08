// Asset-free checks for real GNU MO loading, language changes and isolated INI
// persistence. No game archive, fonts, audio, player configuration or save is used.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/assets.hpp"
#include "headless_mode.hpp"
#include "options.h"
#include "utils/ini.hpp"
#include "utils/language.h"
#include "utils/paths.h"

namespace {
using namespace devilution;

size_t Checks = 0;
unsigned ReplacementCallbackCalls = 0;
using Bytes = std::vector<uint8_t>;

void Check(bool condition, std::string_view message)
{
	++Checks;
	std::cout << (condition ? "PASS " : "FAIL ") << message << '\n';
	if (!condition)
		throw std::runtime_error(std::string(message));
}

void Put32(Bytes &bytes, size_t offset, uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i)
		bytes.at(offset + i) = static_cast<uint8_t>(value >> (8 * i));
}

std::string Plural(std::string_view singular, std::string_view plural)
{
	std::string result(singular);
	result.push_back('\0');
	result.append(plural);
	return result;
}

// Produce a standard little-endian GNU MO independently of the game reader.
Bytes MakePortugueseCatalog()
{
	std::vector<std::pair<std::string, std::string>> entries {
		{ "", "Content-Type: text/plain; charset=UTF-8\nLanguage: pt_BR\nPlural-Forms: nplurals=2; plural=(n > 1);\n" },
		{ "Settings", "Configurações" },
		{ "menu\004Open", "Abrir" },
		{ Plural("tree", "trees"), Plural("árvore", "árvores") },
	};
	std::sort(entries.begin(), entries.end());
	const uint32_t count = static_cast<uint32_t>(entries.size());
	const uint32_t originalTable = 28;
	const uint32_t translationTable = originalTable + 8 * count;
	Bytes bytes(translationTable + 8 * count, 0);
	Put32(bytes, 0, 0x950412DE);
	Put32(bytes, 8, count);
	Put32(bytes, 12, originalTable);
	Put32(bytes, 16, translationTable);
	for (uint32_t i = 0; i < count; ++i) {
		const auto append = [&](const std::string &text, uint32_t table) {
			Put32(bytes, table + 8 * i, static_cast<uint32_t>(text.size()));
			Put32(bytes, table + 8 * i + 4, static_cast<uint32_t>(bytes.size()));
			bytes.insert(bytes.end(), text.begin(), text.end());
			bytes.push_back(0);
		};
		append(entries[i].first, originalTable);
		append(entries[i].second, translationTable);
	}
	return bytes;
}

struct ConfigFixture {
	std::filesystem::path directory;
	std::filesystem::path iniPath;
	std::filesystem::path moPath;
	std::filesystem::path gmoPath;

	ConfigFixture()
	{
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		directory = std::filesystem::temp_directory_path() / ("d3d-language-selection-" + std::to_string(nonce));
		if (!std::filesystem::create_directory(directory))
			throw std::runtime_error("Cannot create a fresh isolated language fixture");
		iniPath = directory / "diablo.ini";
		moPath = directory / "pt_BR.mo";
		gmoPath = directory / "pt_BR.gmo";
		paths::SetBasePath(directory.string());
		paths::SetPrefPath(directory.string());
		paths::SetConfigPath(directory.string());
		paths::SetAssetsPath(directory.string());
		std::ofstream out(iniPath, std::ios::binary);
		out << "[Language]\nCode=en\n";
		if (!out)
			throw std::runtime_error("Cannot write the isolated language configuration");
	}

	~ConfigFixture()
	{
		// Remove only exact fixture-owned files and then the empty directory.
		std::error_code ignored;
		std::filesystem::remove(moPath, ignored);
		std::filesystem::remove(gmoPath, ignored);
		std::filesystem::remove(iniPath, ignored);
		std::filesystem::remove(directory, ignored);
	}

	void Write(const std::filesystem::path &path, const Bytes &bytes) const
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!out)
			throw std::runtime_error("Cannot write a generated catalog fixture");
	}

	std::string SavedLanguage() const
	{
		std::ifstream input(iniPath, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(input)), {});
		const auto parsed = Ini::parse(text);
		if (!parsed.has_value())
			throw std::runtime_error("Cannot parse the isolated language configuration");
		return std::string(parsed->getString("Language", "Code"));
	}
};

void SelectLanguage(std::string_view description)
{
	auto &option = GetOptions().Language.code;
	for (size_t index = 0; index < option.GetListSize(); ++index) {
		if (option.GetListDescription(index) == description) {
			option.SetActiveListIndex(index);
			return;
		}
	}
	throw std::runtime_error("Requested language is absent from the option list");
}

void CheckPortuguese()
{
	Check(GetLanguageCode() == "pt_BR", "a loaded catalog publishes the exact pt_BR runtime code");
	Check(LanguageTranslate("Settings") == "Configurações", "real GNU MO lookup preserves UTF-8 accents");
	Check(LanguageTranslate("Unknown message") == "Unknown message", "untranslated text keeps its source message");
	Check(LanguageParticularTranslate("menu", "Open") == "Abrir", "contextual GNU MO key is translated");
	Check(LanguageParticularTranslate("other", "Open") == "Open", "a different context keeps its source text");
	Check(LanguagePluralTranslate("tree", "trees", 0) == "árvore", "Portuguese plural rule uses singular for zero");
	Check(LanguagePluralTranslate("tree", "trees", 1) == "árvore", "Portuguese singular lookup uses the first form");
	Check(LanguagePluralTranslate("tree", "trees", 2) == "árvores", "Portuguese plural lookup uses the second form");
}

void CheckEnglishFallback()
{
	Check(GetLanguageCode() == "en", "an unavailable or rejected catalog reports effective English");
	Check(LanguageTranslate("Settings") == "Settings", "failed loading cannot retain a previous translated menu");
	Check(LanguageParticularTranslate("menu", "Open") == "Open", "failed loading cannot retain a contextual translation");
	Check(LanguagePluralTranslate("tree", "trees", 0) == "trees", "fallback resets the previous Portuguese plural rule");
	Check(forceLocale.empty(), "runtime fallback never invents a command-line locale override");
}

void CheckSelectionAndRecovery(const ConfigFixture &fixture, const Bytes &catalog)
{
	Check(HasTranslation("en") && !HasTranslation("pt_BR"), "English is available while the fixture has no Portuguese catalog");
	SelectLanguage("Português do Brasil");
	Check(*GetOptions().Language.code == "pt_BR", "the native language list stores the requested pt_BR preference");
	CheckEnglishFallback();
	SaveOptions();
	Check(fixture.SavedLanguage() == "pt_BR", "temporary fallback does not replace the persisted preference");

	fixture.Write(fixture.moPath, catalog);
	Check(HasTranslation("pt_BR"), "installing a generated MO makes the requested translation available");
	LanguageInitialize();
	CheckPortuguese();
	Check(forceLocale.empty(), "recovery needs no change to command-line state");

	SelectLanguage("English");
	Check(GetLanguageCode() == "en" && LanguageTranslate("Settings") == "Settings", "the native list switches back to English immediately");
	Check(LanguagePluralTranslate("tree", "trees", 0) == "trees", "explicit English resets the loaded Portuguese plural rule");
	SelectLanguage("Português do Brasil");
	CheckPortuguese();
	SaveOptions();
	GetOptions().Language.code = "en";
	LoadOptions();
	LanguageInitialize();
	Check(*GetOptions().Language.code == "pt_BR" && fixture.SavedLanguage() == "pt_BR", "save and reload preserve the selected language in the isolated profile");
	CheckPortuguese();

	std::filesystem::remove(fixture.moPath);
	LanguageInitialize();
	CheckEnglishFallback();
	SelectLanguage("English");
	Check(GetLanguageCode() == "en" && *GetOptions().Language.code == "en", "another selection remains effective after a missing-catalog fallback");
	fixture.Write(fixture.gmoPath, catalog);
	SelectLanguage("Português do Brasil");
	CheckPortuguese();
	Check(HasTranslation("pt_BR"), "the supported GMO extension is discoverable without an MO file");
	std::filesystem::remove(fixture.gmoPath);
}

void CheckMalformedCatalogs(const ConfigFixture &fixture, const Bytes &catalog)
{
	std::vector<std::pair<std::string, Bytes>> malformed;
	malformed.emplace_back("short header", Bytes { 1, 2, 3 });
	Bytes invalid = catalog;
	Put32(invalid, 0, 0);
	malformed.emplace_back("bad GNU MO magic", invalid);
	invalid = catalog;
	Put32(invalid, 4, 2);
	malformed.emplace_back("unsupported revision", invalid);
	invalid = catalog;
	Put32(invalid, 8, 0);
	malformed.emplace_back("missing metadata mapping", invalid);
	invalid = catalog;
	Put32(invalid, 8, 0xFFFFFFFF);
	malformed.emplace_back("mapping count outside the file", invalid);
	invalid = catalog;
	Put32(invalid, 12, static_cast<uint32_t>(invalid.size() + 1));
	malformed.emplace_back("source table outside the file", invalid);
	invalid = catalog;
	Put32(invalid, 16, static_cast<uint32_t>(invalid.size() + 1));
	malformed.emplace_back("translation table outside the file", invalid);
	invalid = catalog;
	Put32(invalid, 28, 0xFFFFFFFF);
	malformed.emplace_back("source string length outside the file", invalid);
	invalid = catalog;
	Put32(invalid, 28 + 8 * 4 + 4, static_cast<uint32_t>(invalid.size()));
	malformed.emplace_back("translated metadata outside the file", invalid);
	for (const auto &[description, bytes] : malformed) {
		fixture.Write(fixture.moPath, catalog);
		LanguageInitialize();
		Check(LanguageTranslate("Settings") == "Configurações", "valid catalog loads before malformed case: " + description);
		fixture.Write(fixture.moPath, bytes);
		LanguageInitialize();
		CheckEnglishFallback();
		Check(*GetOptions().Language.code == "pt_BR", "malformed catalog preserves the requested preference: " + description);
	}
	fixture.Write(fixture.moPath, catalog);
	LanguageInitialize();
	CheckPortuguese();
	std::filesystem::remove(fixture.moPath);
}

void CheckCommandLineOverride(const ConfigFixture &fixture, const Bytes &catalog)
{
	SelectLanguage("English");
	forceLocale = "pt_BR";
	LanguageInitialize();
	Check(GetLanguageCode() == "en" && forceLocale == "pt_BR", "missing catalog falls back temporarily without overwriting --lang pt_BR");
	Check(*GetOptions().Language.code == "en", "command-line selection does not replace the saved language preference");
	fixture.Write(fixture.moPath, catalog);
	LanguageInitialize();
	CheckPortuguese();
	Check(forceLocale == "pt_BR", "a newly installed catalog recovers the original explicit CLI locale");
	SelectLanguage("English");
	Check(GetLanguageCode() == "pt_BR" && *GetOptions().Language.code == "en", "an explicit CLI locale still takes precedence over the menu preference");
	SaveOptions();
	Check(fixture.SavedLanguage() == "en", "CLI override is not written over the independent INI preference");
	forceLocale = "en";
	LanguageInitialize();
	Check(GetLanguageCode() == "en" && LanguageTranslate("Settings") == "Settings", "explicit --lang en stays effective with a Portuguese catalog installed");
	forceLocale.clear();
	SelectLanguage("Português do Brasil");
	CheckPortuguese();
}

void ObserveLanguageChange()
{
	++ReplacementCallbackCalls;
	LanguageInitialize();
}

void CheckCallbackReplacement(const ConfigFixture &fixture)
{
	auto &option = GetOptions().Language.code;
	ReplacementCallbackCalls = 0;
	option.SetValueChangedCallback(ObserveLanguageChange);
	// Real filesystem work overwrites stack frames after the setter returns.
	// Reassigning optional<function_ref> used to borrow the setter's local
	// function_ref wrapper; the next selection then called expired stack data.
	std::filesystem::copy_file(fixture.moPath, fixture.gmoPath,
	    std::filesystem::copy_options::overwrite_existing);
	SelectLanguage("English");
	Check(ReplacementCallbackCalls == 1 && GetLanguageCode() == "en",
	    "a reassigned language callback remains callable after real filesystem stack work");
	Check(LanguageTranslate("Settings") == "Settings",
	    "the replacement callback applies the selected language instead of retaining stale state");
	option.SetValueChangedCallback(LanguageInitialize);
	std::filesystem::copy_file(fixture.moPath, fixture.gmoPath,
	    std::filesystem::copy_options::overwrite_existing);
	SelectLanguage("Português do Brasil");
	CheckPortuguese();
	Check(ReplacementCallbackCalls == 1,
	    "replacing the observer restores the loader without retaining the previous callback");
	std::filesystem::remove(fixture.gmoPath);
}

} // namespace

int main(int argc, char **argv)
{
	std::cout << std::unitbuf;
	SDL_SetMainReady();
	if (SDL_Init(0) != 0) {
		std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}
	try {
		HeadlessMode = true;
		const ConfigFixture fixture;
		LoadOptions();
		// This executable exercises the real option list and loader, replacing only
		// the game callback which otherwise also loads fonts, voices and UI sounds.
		GetOptions().Language.code.SetValueChangedCallback(LanguageInitialize);
		forceLocale.clear();
		LanguageInitialize();
		const Bytes catalog = MakePortugueseCatalog();
		CheckSelectionAndRecovery(fixture, catalog);
		CheckMalformedCatalogs(fixture, catalog);
		CheckCommandLineOverride(fixture, catalog);
		CheckCallbackReplacement(fixture);
		if (argc == 2) {
			// Exercise the catalog actually delivered beside the game, in isolation.
			std::filesystem::remove(fixture.moPath);
			std::filesystem::copy_file(std::filesystem::path(argv[1]) / "pt_BR.gmo", fixture.gmoPath,
			    std::filesystem::copy_options::overwrite_existing);
			forceLocale.clear();
			SelectLanguage("English");
			SelectLanguage("Português do Brasil");
			Check(GetLanguageCode() == "pt_BR" && LanguageTranslate("Settings") == "Configurações",
			    "delivered Portuguese GMO applies through the native option list and runtime loader");
			Check(LanguageTranslate("Language") == "Idioma" && LanguageTranslate("Single Player") == "Um Jogador",
			    "delivered catalog translates the language selector and main menu");
			SaveOptions();
			GetOptions().Language.code = "en";
			LoadOptions();
			LanguageInitialize();
			Check(GetLanguageCode() == "pt_BR" && fixture.SavedLanguage() == "pt_BR" && LanguageTranslate("Settings") == "Configurações",
			    "delivered Portuguese catalog remains selected after an isolated configuration reload");
		}
		Check(MpqArchives.empty(), "no game archive was loaded during language selection checks");
		std::cout << "PASS " << Checks << " checks; generated GNU MO only, no player files accessed\n";
	} catch (const std::exception &error) {
		std::cerr << "FAIL after " << Checks << " checks: " << error.what() << '\n';
		SDL_Quit();
		return 1;
	}
	SDL_Quit();
	return 0;
}
