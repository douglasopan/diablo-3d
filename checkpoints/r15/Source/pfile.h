/**
 * @file pfile.h
 *
 * Interface of the save game encoding functionality.
 */
#pragma once

#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>

#include "DiabloUI/diabloui.h"
#include "player.h"

#ifdef UNPACKED_SAVES
#include "utils/file_util.h"
#else
#include "mpq/mpq_reader.hpp"
#include "mpq/mpq_writer.hpp"
#endif

namespace devilution {

#define MAX_CHARACTERS 99

extern bool gbValidSaveFile;

#ifdef UNPACKED_SAVES
struct SaveReader {
	explicit SaveReader(std::string &&dir)
	    : dir_(std::move(dir))
	{
	}

	const std::string &dir() const
	{
		return dir_;
	}

	std::unique_ptr<std::byte[]> ReadFile(const char *filename, std::size_t &fileSize, int32_t &error);

	bool HasFile(const char *path)
	{
		return ::devilution::FileExists((dir_ + path).c_str());
	}

private:
	std::string dir_;
};

struct SaveWriter {
	explicit SaveWriter(std::string &&dir, bool carryForward = true, bool staged = false)
	    : dir_(std::move(dir))
	    , writeError_(staged)
	    , stagedUnsupported_(staged)
	{
		(void)carryForward;
	}

	bool WriteFile(const char *filename, const std::byte *data, size_t size);

	bool HasFile(const char *path)
	{
		return ::devilution::FileExists((dir_ + path).c_str());
	}

	void RenameFile(const char *from, const char *to)
	{
		::devilution::RenameFile((dir_ + from).c_str(), (dir_ + to).c_str());
	}

	void RemoveHashEntry(const char *path)
	{
		RemoveFile((dir_ + path).c_str());
	}

	void RemoveHashEntries(bool (*fnGetName)(uint8_t, char *));

	// Unpacked saves have no MPQ table-slot limit; actual I/O may still fail.
	std::optional<size_t> FreeFileSlots() const
	{
		if (writeError_)
			return std::nullopt;
		return std::numeric_limits<size_t>::max();
	}
	bool HasWriteError() const { return writeError_; }
	void MarkWriteError() { writeError_ = true; }
	bool Commit() { return !writeError_ && !stagedUnsupported_; }
	bool RemoveHashEntryChecked(const char *path)
	{
		RemoveHashEntry(path);
		const bool success = !HasFile(path);
		writeError_ = writeError_ || !success;
		return success;
	}
	bool RenameFileChecked(const char *from, const char *to)
	{
		if (!HasFile(from) || HasFile(to)) {
			writeError_ = true;
			return false;
		}
		RenameFile(from, to);
		const bool success = !HasFile(from) && HasFile(to);
		writeError_ = writeError_ || !success;
		return success;
	}

private:
	std::string dir_;
	bool writeError_ = false;
	bool stagedUnsupported_ = false;
};

#else
using SaveReader = MpqArchive;
using SaveWriter = MpqWriter;
#endif

/**
 * @brief Comparison result of pfile_compare_hero_demo
 */
struct HeroCompareResult {
	enum Status : uint8_t {
		ReferenceNotFound,
		Same,
		Difference,
	};
	Status status;
	std::string message;
};

std::optional<SaveReader> OpenSaveArchive(uint32_t saveNum);
SaveWriter OpenSaveWriter(uint32_t saveNum, bool carryForward = true, bool staged = false);
std::optional<SaveReader> OpenStashArchive();
const char *pfile_get_password();
std::unique_ptr<std::byte[]> ReadArchive(SaveReader &archive, const char *pszName, size_t *pdwLen = nullptr);
std::optional<size_t> GetArchiveEncodedFileSize(SaveReader &archive, const char *name);
/** Encodes with the existing native save codec/password and propagates WriteFile failure. */
[[nodiscard]] bool WriteArchiveEncoded(SaveWriter &archive, const char *name, std::span<const std::byte> bytes);
void pfile_write_hero(bool writeGameData = false);

#ifndef DISABLE_DEMOMODE
/**
 * @brief Save a reference game-state (save game) for the demo recording
 * @param demo that is recorded
 */
void pfile_write_hero_demo(int demo);
/**
 * @brief Compares the actual game-state (savegame) with a reference game-state (save game from demo recording)
 * @param demo for the comparison
 * @param logDetails in case of a difference log details
 * @return The comparison result.
 */
HeroCompareResult pfile_compare_hero_demo(int demo, bool logDetails);
#endif

void sfile_write_stash();
bool pfile_ui_set_hero_infos(bool (*uiAddHeroInfo)(_uiheroinfo *));
void pfile_ui_set_class_stats(HeroClass playerClass, _uidefaultstats *classStats);
uint32_t pfile_ui_get_first_unused_save_num();
bool pfile_ui_save_create(_uiheroinfo *heroinfo);
bool pfile_delete_save(_uiheroinfo *heroInfo);
void pfile_read_player_from_save(uint32_t saveNum, Player &player);
[[nodiscard]] std::expected<void, std::string> pfile_save_level();
std::expected<void, std::string> pfile_convert_levels();
void pfile_remove_temp_files();
/** Surface cleanup before FreeGameMem; true means native temps were also
 * cleaned atomically, so the caller MUST skip its later legacy temp cleanup. */
std::expected<bool, std::string> pfile_remove_surface_temp_files();
/** Prepare NEW by cleaning ONLY regional/native TEMPS in one staged commit.
 * The previous game + metadata + regional permanents remain a consistent full
 * snapshot until the first successful new FULL save. true skips legacy cleanup. */
std::expected<bool, std::string> ResetSavedSurfaceWorld(uint32_t saveNum);
std::unique_ptr<std::byte[]> pfile_read(const char *pszName, size_t *pdwLen);
void pfile_update(bool forceSave);

} // namespace devilution
