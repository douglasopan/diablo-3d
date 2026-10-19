#include "mpq/mpq_writer.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <mpqfs/mpqfs.h>
// Private, typed definition from the SAME pinned mpqfs source as the library:
// diasurgical/mpqfs de99526f869cf647fde8a2229131c880eab442e9, src/mpq_writer.h.
// Only this translation unit receives its private include directory.
#include <mpq_writer.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "mpq/mpq_common.hpp"
#include "utils/file_util.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

std::string SiblingSavePath(std::string path, std::string_view extension)
{
	const size_t sep = path.find_last_of("/\\");
	const size_t dot = path.find_last_of('.');
	if (dot != std::string::npos && (sep == std::string::npos || dot > sep))
		path.resize(dot);
	path.append(extension);
	return path;
}

// Stage and backup are siblings on the same volume. No copy/delete fallback.
// Windows ReplaceFile keeps the original as backup during replacement. Error
//1177 can leave it under that backup name; restore when possible, retain it on
// any failed recovery. POSIX rename replaces an existing name atomically.
bool PublishStagedSave(const std::string &stage, const std::string &path, const std::string &backup)
{
#ifdef _WIN32
#ifdef DEVILUTIONX_WINDOWS_NO_WCHAR
	if (!FileExists(path))
		return ::MoveFileExA(stage.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
	if (::ReplaceFileA(path.c_str(), stage.c_str(), backup.c_str(), 0, nullptr, nullptr) != 0) {
		::devilution::RemoveFile(backup.c_str());
		return true;
	}
	if (!FileExists(path) && FileExists(backup))
		(void)::MoveFileExA(backup.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
#else
	const auto stageUtf16 = ToWideChar(stage);
	const auto pathUtf16 = ToWideChar(path);
	const auto backupUtf16 = ToWideChar(backup);
	if (stageUtf16 == nullptr || pathUtf16 == nullptr || backupUtf16 == nullptr)
		return false;
	if (!FileExists(path))
		return ::MoveFileExW(stageUtf16.get(), pathUtf16.get(), MOVEFILE_WRITE_THROUGH) != 0;
	if (::ReplaceFileW(pathUtf16.get(), stageUtf16.get(), backupUtf16.get(), 0, nullptr, nullptr) != 0) {
		::devilution::RemoveFile(backup.c_str());
		return true;
	}
	if (!FileExists(path) && FileExists(backup))
		(void)::MoveFileExW(backupUtf16.get(), pathUtf16.get(), MOVEFILE_WRITE_THROUGH);
#endif
	return false;
#else
	(void)backup;
	return std::rename(stage.c_str(), path.c_str()) == 0;
#endif
}

// mpqfs_error_message() only describes the general category of failure; for
// I/O errors the specific OS error is only available via errno immediately
// after the failing call, so it's folded in here for diagnostics.
std::string FormatMpqfsError(mpqfs_error_code code)
{
	if (code == MPQFS_ERR_IO) {
		return StrCat(mpqfs_error_message(code), ": ", std::strerror(errno));
	}
	return mpqfs_error_message(code);
}

} // namespace

MpqWriter::MpqWriter(const char *path, bool carryForward, bool staged)
    : path_(path)
    , staged_(staged)
{
	const std::string dir = std::string(Dirname(path));
	if (!dir.empty()) {
		RecursivelyCreateDir(dir.c_str());
	}
	LogVerbose("Opening {}", path);
	if (staged_) {
		stagePath_ = SiblingSavePath(path_, ".swt");
		backupPath_ = SiblingSavePath(path_, ".swb");
		// Never truncate a stale/other writer's stage or recovery backup.
		if (stagePath_ == path_ || backupPath_ == path_ || FileExists(stagePath_) || FileExists(backupPath_)) {
			writeError_ = true;
			return;
		}
		FILE *reservation = OpenFile(stagePath_.c_str(), "wbx");
		if (reservation == nullptr) {
			writeError_ = true;
			return;
		}
		ownsStage_ = true;
		if (std::fclose(reservation) != 0) {
			writeError_ = true;
			return;
		}
		mpqfs_archive_t *original = nullptr;
		if (carryForward && FileExists(path_) && mpqfs_open(path, &original) != MPQFS_OK) {
			writeError_ = true;
			return;
		}
		const auto createCode = mpqfs_writer_create(stagePath_.c_str(), MpqWriterHashTableSize, &writer_);
		if (createCode != MPQFS_OK) {
			writeError_ = true;
			if (original != nullptr)
				mpqfs_close(original);
			return;
		}
		contentValid_ = true;
		if (original != nullptr) {
			const auto carryCode = mpqfs_writer_carry_forward_all(writer_, original);
			mpqfs_close(original);
			if (carryCode != MPQFS_OK) {
				contentValid_ = false;
				writeError_ = true;
			}
		}
		return;
	}

	// If the file already exists and we need to preserve its contents,
	// rename it to a temp path so we can read from it after the writer
	// truncates the original path.
	std::string tmpPath;
	mpqfs_archive_t *oldArchive = nullptr;
	bool carryForwardValid = true;
	if (carryForward && FileExists(path)) {
		// Replace the extension with .tmp rather than appending it to stay 8.3 compliant.
		tmpPath = std::string(path);
		const size_t sep = tmpPath.find_last_of("/\\");
		const size_t dot = tmpPath.find_last_of('.');
		if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
			tmpPath.resize(dot);
		}
		tmpPath += ".tmp";
		// Remove any stale temp file, then rename old → tmp.
		::devilution::RemoveFile(tmpPath.c_str());
		::devilution::RenameFile(path, tmpPath.c_str());
		// If it fails to open (e.g. corrupt), we proceed without
		// carry-forward — the file will be recreated from scratch.
		carryForwardValid = mpqfs_open(tmpPath.c_str(), &oldArchive) == MPQFS_OK;
	}

	const mpqfs_error_code code = mpqfs_writer_create(path, MpqWriterHashTableSize, &writer_);
	if (code != MPQFS_OK) {
		writeError_ = true;
		LogError("Failed to write MPQ archive to {}: {}", path, FormatMpqfsError(code));
		if (oldArchive != nullptr)
			mpqfs_close(oldArchive);
		if (!tmpPath.empty())
			::devilution::RemoveFile(tmpPath.c_str());
		return;
	}
	contentValid_ = carryForwardValid;
	writeError_ = !carryForwardValid;

	if (oldArchive != nullptr) {
		const mpqfs_error_code carryForwardCode = mpqfs_writer_carry_forward_all(writer_, oldArchive);
		if (carryForwardCode != MPQFS_OK) {
			contentValid_ = false;
			writeError_ = true;
			LogError("Failed to carry forward files from existing archive {}: {}", path, FormatMpqfsError(carryForwardCode));
		}
		mpqfs_close(oldArchive);
	}

	if (!tmpPath.empty())
		::devilution::RemoveFile(tmpPath.c_str());
}

MpqWriter::MpqWriter(MpqWriter &&other) noexcept
    : path_(std::move(other.path_))
    , writer_(other.writer_)
    , contentValid_(other.contentValid_)
    , writeError_(other.writeError_)
    , staged_(other.staged_)
    , ownsStage_(other.ownsStage_)
    , committed_(other.committed_)
    , stagePath_(std::move(other.stagePath_))
    , backupPath_(std::move(other.backupPath_))
{
	other.writer_ = nullptr;
	other.contentValid_ = false;
	other.writeError_ = true;
	other.ownsStage_ = false;
}

MpqWriter &MpqWriter::operator=(MpqWriter &&other) noexcept
{
	if (this != &other) {
		if (writer_ != nullptr)
			mpqfs_writer_discard(writer_);
		if (ownsStage_ && !committed_)
			::devilution::RemoveFile(stagePath_.c_str());
		path_ = std::move(other.path_);
		writer_ = other.writer_;
		contentValid_ = other.contentValid_;
		writeError_ = other.writeError_;
		staged_ = other.staged_;
		ownsStage_ = other.ownsStage_;
		committed_ = other.committed_;
		stagePath_ = std::move(other.stagePath_);
		backupPath_ = std::move(other.backupPath_);
		other.writer_ = nullptr;
		other.contentValid_ = false;
		other.writeError_ = true;
		other.ownsStage_ = false;
	}
	return *this;
}

MpqWriter::~MpqWriter()
{
	if (staged_) {
		if (writer_ != nullptr)
			mpqfs_writer_discard(writer_);
		if (ownsStage_ && !committed_)
			::devilution::RemoveFile(stagePath_.c_str());
		return;
	}
	if (writer_ == nullptr)
		return;

	LogVerbose("Closing {}", path_);

	const mpqfs_error_code code = mpqfs_writer_close(writer_);
	if (code != MPQFS_OK) {
		LogError("Failed to close MPQ archive {}: {}", path_, FormatMpqfsError(code));
	}
}

bool MpqWriter::Commit()
{
	if (committed_)
		return !writeError_;
	if (!FreeFileSlots().has_value()) {
		writeError_ = true;
		return false;
	}
	// The pinned close() frees metadata on every path, but does not propagate
	// fflush/fclose errors. Retain this owned FILE through its documented typed
	// owns_fd field, then check BOTH flush/error and close before publishing.
	FILE *file = writer_->fp;
	if (writer_->owns_fd == 0) {
		writeError_ = true;
		return false;
	}
	writer_->owns_fd = 0;
	const auto closeCode = mpqfs_writer_close(writer_);
	writer_ = nullptr; // close consumes the handle even on failure.
	const bool flushed = std::fflush(file) == 0 && std::ferror(file) == 0;
	const bool closed = std::fclose(file) == 0;
	if (closeCode != MPQFS_OK || !flushed || !closed) {
		writeError_ = true;
		return false;
	}
	if (staged_ && !PublishStagedSave(stagePath_, path_, backupPath_)) {
		writeError_ = true;
		return false;
	}
	committed_ = true;
	ownsStage_ = false;
	return true;
}

bool MpqWriter::HasFile(std::string_view name) const
{
	if (writer_ == nullptr)
		return false;

	char buf[MaxMpqPathSize];
	if (name.size() >= sizeof(buf))
		return false;
	std::memcpy(buf, name.data(), name.size());
	buf[name.size()] = '\0';

	return mpqfs_writer_has_file(writer_, buf);
}

std::optional<size_t> MpqWriter::FreeFileSlots() const
{
	// mpqfs add_file/carry_forward_all reserve one sentinel slot. file_count
	// includes removed metadata; neither remove nor replacing a name reclaims it.
	// Direct typed access, no opaque-handle cast or duplicate parser/counter.
	if (writer_ == nullptr || !contentValid_ || writeError_ || writer_->fp == nullptr
	    || writer_->hash_table_size != MpqWriterHashTableSize
	    || writer_->file_count >= writer_->hash_table_size
	    || writer_->file_count > writer_->file_capacity
	    || (writer_->file_count != 0 && writer_->files == nullptr))
		return std::nullopt;
	return static_cast<size_t>(writer_->hash_table_size - 1 - writer_->file_count);
}

bool MpqWriter::RemoveHashEntryChecked(std::string_view filename)
{
	if (!FreeFileSlots().has_value() || filename.size() >= MaxMpqPathSize) {
		writeError_ = true;
		return false;
	}
	if (!HasFile(filename))
		return true;
	char buf[MaxMpqPathSize];
	std::memcpy(buf, filename.data(), filename.size());
	buf[filename.size()] = '\0';
	const bool success = mpqfs_writer_remove_file(writer_, buf) == MPQFS_OK;
	writeError_ = writeError_ || !success;
	return success;
}

bool MpqWriter::RenameFileChecked(std::string_view name, std::string_view newName)
{
	if (!FreeFileSlots().has_value() || name.size() >= MaxMpqPathSize
	    || newName.size() >= MaxMpqPathSize || !HasFile(name)) {
		writeError_ = true;
		return false;
	}
	if (name == newName)
		return true;
	if (HasFile(newName)) {
		writeError_ = true;
		return false;
	}
	char oldBuf[MaxMpqPathSize];
	char newBuf[MaxMpqPathSize];
	std::memcpy(oldBuf, name.data(), name.size());
	oldBuf[name.size()] = '\0';
	std::memcpy(newBuf, newName.data(), newName.size());
	newBuf[newName.size()] = '\0';
	const bool success = mpqfs_writer_rename_file(writer_, oldBuf, newBuf) == MPQFS_OK;
	writeError_ = writeError_ || !success;
	return success;
}

void MpqWriter::RemoveHashEntry(std::string_view filename)
{
	if (writer_ == nullptr)
		return;

	char buf[MaxMpqPathSize];
	if (filename.size() >= sizeof(buf))
		return;
	std::memcpy(buf, filename.data(), filename.size());
	buf[filename.size()] = '\0';

	// Not found (or already removed) is not an error condition here.
	(void)mpqfs_writer_remove_file(writer_, buf);
}

void MpqWriter::RemoveHashEntries(bool (*fnGetName)(uint8_t, char *))
{
	char pszFileName[MaxMpqPathSize];
	for (uint8_t i = 0; fnGetName(i, pszFileName); i++) {
		RemoveHashEntry(pszFileName);
	}
}

bool MpqWriter::WriteFile(std::string_view filename, const std::byte *data, size_t size)
{
	if (writer_ == nullptr) {
		writeError_ = true;
		return false;
	}

	char buf[MaxMpqPathSize];
	if (filename.size() >= sizeof(buf)) {
		writeError_ = true;
		return false;
	}
	std::memcpy(buf, filename.data(), filename.size());
	buf[filename.size()] = '\0';

	// Remove any previous entry with this name so that
	// only the latest write is included in the archive.
	(void)mpqfs_writer_remove_file(writer_, buf);

	const mpqfs_error_code code = mpqfs_writer_add_file(writer_, buf, data, size);
	if (code != MPQFS_OK) {
		writeError_ = true;
		LogError("Failed to write file '{}' to MPQ {}: {}", filename, path_, FormatMpqfsError(code));
		return false;
	}
	return true;
}

void MpqWriter::RenameFile(std::string_view name, std::string_view newName)
{
	if (writer_ == nullptr)
		return;

	char oldBuf[MaxMpqPathSize];
	char newBuf[MaxMpqPathSize];
	if (name.size() >= sizeof(oldBuf) || newName.size() >= sizeof(newBuf))
		return;
	std::memcpy(oldBuf, name.data(), name.size());
	oldBuf[name.size()] = '\0';
	std::memcpy(newBuf, newName.data(), newName.size());
	newBuf[newName.size()] = '\0';

	(void)mpqfs_writer_rename_file(writer_, oldBuf, newBuf);
}

} // namespace devilution
