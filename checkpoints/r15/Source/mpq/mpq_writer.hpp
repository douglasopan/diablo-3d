/**
 * @file mpq/mpq_writer.hpp
 *
 * Interface of functions for creating and editing MPQ files.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Forward-declare so that we can avoid exposing mpqfs.h to all consumers.
struct mpqfs_writer;
typedef struct mpqfs_writer mpqfs_writer_t;

namespace devilution {

constexpr uint32_t MpqWriterHashTableSize = 2048;

class MpqWriter {
public:
	explicit MpqWriter(const char *path, bool carryForward = true, bool staged = false);
	explicit MpqWriter(const std::string &path, bool carryForward = true, bool staged = false)
	    : MpqWriter(path.c_str(), carryForward, staged)
	{
	}
	MpqWriter(MpqWriter &&other) noexcept;
	MpqWriter &operator=(MpqWriter &&other) noexcept;
	~MpqWriter();

	MpqWriter(const MpqWriter &) = delete;
	MpqWriter &operator=(const MpqWriter &) = delete;

	bool HasFile(std::string_view name) const;
	/** Remaining add-file operations in this open writer, including replacements.
	 * Removed entries consume append slots until the archive is reopened. */
	[[nodiscard]] std::optional<size_t> FreeFileSlots() const;
	[[nodiscard]] bool HasWriteError() const { return writeError_; }
	void MarkWriteError() { writeError_ = true; }
	/** Explicit close/check/publish. Staged writers never replace the original
	 * unless this succeeds; their destructor discards an uncommitted stage. */
	[[nodiscard]] bool Commit();
	[[nodiscard]] bool RemoveHashEntryChecked(std::string_view filename);
	[[nodiscard]] bool RenameFileChecked(std::string_view name, std::string_view newName);
	void RemoveHashEntry(std::string_view filename);
	void RemoveHashEntries(bool (*fnGetName)(uint8_t, char *));
	bool WriteFile(std::string_view filename, const std::byte *data, size_t size);
	void RenameFile(std::string_view name, std::string_view newName);

private:
	std::string path_;
	mpqfs_writer_t *writer_ = nullptr;
	bool contentValid_ = false;
	bool writeError_ = false;
	bool staged_ = false;
	bool ownsStage_ = false;
	bool committed_ = false;
	std::string stagePath_;
	std::string backupPath_;
};

} // namespace devilution
