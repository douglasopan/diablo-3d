#include "engine/hd_sprite_pack.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <span>

#include "utils/endian_read.hpp"
#include "utils/file_util.h"
#include "utils/paths.h"

namespace devilution {
namespace {
constexpr uint64_t MaxPackBytes = 2ULL * 1024 * 1024 * 1024;
constexpr size_t MaxResourceBytes = 64 * 1024 * 1024;
struct Entry { uint64_t offset; uint32_t bytes; };
struct IoDeleter { void operator()(SDL_IOStream *io) const { if (io != nullptr) SDL_CloseIO(io); } };
struct Pack {
	std::string path;
	bool checked = false;
	std::unique_ptr<SDL_IOStream, IoDeleter> io;
	std::map<std::string, Entry, std::less<>> entries;
	HdSpritePackStatus status;
	std::map<uint64_t, std::vector<uint32_t>> rgbCache;
	size_t rgbCacheBytes = 0;
};
Pack State;

uint64_t Read64(const uint8_t *p) { return LoadLE32(p) | (static_cast<uint64_t>(LoadLE32(p + 4)) << 32); }
std::string Canonical(std::string_view path)
{
	std::string result(path);
	std::replace(result.begin(), result.end(), '\\', '/');
	for (char &c : result) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	const size_t extension = result.find_last_of('.');
	if (extension != std::string::npos) result.resize(extension);
	return result;
}
bool Fail(std::string message)
{
	State.status.failure = std::move(message);
	State.status.mounted = false;
	State.io.reset(); State.entries.clear();
	return false;
}
bool Mount()
{
	const std::string path = paths::AssetsPath() + "d3d-sprites.art";
	if (State.checked && State.path == path) return State.status.failure.empty();
	State = {}; State.checked = true; State.path = path;
	if (!FileExists(path.c_str())) return true;
	State.io.reset(SDL_IOFromFile(path.c_str(), "rb"));
	if (!State.io) return Fail("Unable to open the HD sprite pack");
	const auto size = SDL_GetIOSize(State.io.get());
	if (size < 16 || static_cast<uint64_t>(size) > MaxPackBytes) return Fail("HD sprite pack size");
	std::array<uint8_t, 16> header;
	if (SDL_ReadIO(State.io.get(), header.data(), header.size()) != header.size()
	    || std::memcmp(header.data(), "D3DSPR1\0", 8) != 0) return Fail("HD sprite pack header");
	const uint32_t count = LoadLE32(header.data() + 8), indexSize = LoadLE32(header.data() + 12);
	if (count == 0 || count > 65536 || indexSize > 8 * 1024 * 1024
	    || indexSize > static_cast<uint64_t>(size) - 16) return Fail("HD sprite pack index size");
	std::vector<uint8_t> index(indexSize);
	if (SDL_ReadIO(State.io.get(), index.data(), index.size()) != index.size()) return Fail("HD sprite pack index read");
	size_t cursor = 0;
	std::vector<std::pair<uint64_t, uint64_t>> ranges;
	for (uint32_t i = 0; i < count; ++i) {
		if (index.size() - cursor < 16) return Fail("HD sprite pack truncated index");
		const uint8_t *row = index.data() + cursor;
		const uint16_t length = LoadLE16(row), flags = LoadLE16(row + 2);
		const uint64_t offset = Read64(row + 4); const uint32_t bytes = LoadLE32(row + 12);
		cursor += 16;
		if (flags != 0 || length == 0 || length > 1024 || length > index.size() - cursor
		    || bytes == 0 || bytes > MaxResourceBytes || offset < 16ULL + indexSize
		    || offset > static_cast<uint64_t>(size) || bytes > static_cast<uint64_t>(size) - offset)
			return Fail("HD sprite pack resource bounds");
		std::string key(reinterpret_cast<const char *>(index.data() + cursor), length); cursor += length;
		if (key.front() == '/' || key.find("..") != std::string::npos || key.find(':') != std::string::npos
		    || key.find('\\') != std::string::npos || key.find('\0') != std::string::npos
		    || !State.entries.emplace(key, Entry { offset, bytes }).second) return Fail("HD sprite pack resource key");
		ranges.emplace_back(offset, offset + bytes);
	}
	if (cursor != index.size()) return Fail("HD sprite pack index trailing bytes");
	std::sort(ranges.begin(), ranges.end());
	for (size_t i = 1; i < ranges.size(); ++i)
		if (ranges[i].first < ranges[i - 1].second) return Fail("HD sprite pack overlapping resources");
	State.status.mounted = true; State.status.indexEntries = State.entries.size();
	State.status.bytesRead = header.size() + index.size();
	return true;
}
std::expected<std::vector<uint8_t>, std::string> Read(std::string_view key)
{
	const auto found = State.entries.find(key);
	if (found == State.entries.end()) return std::unexpected("HD sprite pack resource missing");
	const Entry entry = found->second;
	std::vector<uint8_t> bytes(entry.bytes);
	if (SDL_SeekIO(State.io.get(), entry.offset, SDL_IO_SEEK_SET) < 0
	    || SDL_ReadIO(State.io.get(), bytes.data(), bytes.size()) != bytes.size())
		return std::unexpected("HD sprite pack resource read failed");
	State.status.bytesRead += bytes.size();
	return bytes;
}
// Validate before calling the native unchecked CLX views. Both list and sheet
// containers and every RLE run are checked; covered palette0 is not an alpha key.
bool ValidClx(std::span<const uint8_t> bytes)
{
	if (bytes.size() < 12) return false;
	const uint32_t first = LoadLE32(bytes.data());
	const bool list = first > 0 && first <= 16384 && 4ULL * (first + 2) <= bytes.size()
	    && LoadLE32(bytes.data() + 4ULL * (first + 1)) == bytes.size();
	if (!list && (first == 0 || first % 4 != 0 || first / 4 > 32 || first > bytes.size())) return false;
	const uint32_t groups = list ? 1 : first / 4;
	for (uint32_t group = 0; group < groups; ++group) {
		const size_t start = list ? 0 : LoadLE32(bytes.data() + 4 * group);
		const size_t end = list || group + 1 == groups ? bytes.size() : LoadLE32(bytes.data() + 4 * (group + 1));
		if (start >= end || (!list && start < first) || end > bytes.size() || end - start < 12) return false;
		const uint8_t *data = bytes.data() + start; const size_t count = LoadLE32(data);
		if (count == 0 || count > 16384 || 4 * (count + 2) > end - start
		    || LoadLE32(data + 4 * (count + 1)) != end - start) return false;
		for (size_t frame = 0; frame < count; ++frame) {
			const size_t begin = LoadLE32(data + 4 * (frame + 1)), finish = LoadLE32(data + 4 * (frame + 2));
			if (begin < 4 * (count + 2) || begin >= finish || finish > end - start || finish - begin < 6) return false;
			const uint8_t *p = data + begin; const size_t header = LoadLE16(p), w = LoadLE16(p + 2), h = LoadLE16(p + 4);
			if ((header != 6 && header != 20 && header != 276) || header > finish - begin || w == 0 || h == 0 || w > 4096 || h > 4096) return false;
			if (header != 6 && (LoadLE32(p + 16) != 0x48443344 || LoadLE16(p + 6) < 1 || LoadLE16(p + 6) > 4)) return false;
			size_t cursor = begin + header, covered = 0;
			while (cursor < finish) {
				const unsigned control = data[cursor++];
				// 191..255 are literals; 128..190 are fills; 1..127 skip.
				const size_t actualRun = control >= 191 ? 256 - control : control >= 128 ? 191 - control : control;
				const size_t following = control >= 191 ? actualRun : control >= 128 ? 1 : 0;
				if (actualRun == 0 || actualRun > w * h - covered || following > finish - cursor) return false;
				covered += actualRun; cursor += following;
			}
			if (covered != w * h) return false;
		}
	}
	return true;
}
} // namespace

std::expected<OptionalOwnedClxSpriteListOrSheet, std::string> LoadHdSpriteReplacement(std::string_view path, SDL_Color *outPalette)
{
	if (!Mount()) return std::unexpected(State.status.failure);
	const std::string key = "s/" + Canonical(path);
	if (!State.status.mounted || !State.entries.contains(key)) {
		if (State.status.mounted) ++State.status.uncoveredRequests;
		return OptionalOwnedClxSpriteListOrSheet { std::nullopt };
	}
	auto data = Read(key);
	if (!data || !ValidClx(*data)) return std::unexpected(data ? "Invalid HD replacement CLX" : data.error());
	if (outPalette != nullptr) {
		auto palette = Read("p/" + Canonical(path));
		if (!palette || palette->size() != 768) return std::unexpected("HD replacement palette missing");
		for (size_t i = 0; i < 256; ++i) {
			outPalette[i].r = (*palette)[3 * i]; outPalette[i].g = (*palette)[3 * i + 1]; outPalette[i].b = (*palette)[3 * i + 2];
#ifndef USE_SDL1
			outPalette[i].a = 255;
#endif
		}
	}
	auto owned = std::make_unique<uint8_t[]>(data->size()); std::memcpy(owned.get(), data->data(), data->size());
	++State.status.replacementsLoaded;
	return OptionalOwnedClxSpriteListOrSheet { OwnedClxSpriteListOrSheet::FromBuffer(std::move(owned), data->size()) };
}

std::expected<HdSpritePixels, std::string> LoadHdSpritePixels(ClxSprite sprite)
{
	HdSpritePixels result;
	if (sprite.hdFrameId() == 0) return result;
	if (!Mount() || !State.status.mounted) return std::unexpected("HD sprite pack is unavailable");
	auto data = Read("h/" + std::to_string(sprite.hdFrameId()));
	if (!data || !ValidClx(*data)) return std::unexpected("HD frame read failed");
	const ClxSpriteList list(data->data());
	if (list.numSprites() != 1) return std::unexpected("HD frame must be a single CLX list");
	const ClxSprite frame = list[0];
	result.width = frame.width(); result.height = frame.height(); result.scale = sprite.hdScale();
	const size_t area = static_cast<size_t>(result.width) * result.height;
	if (result.width != sprite.width() * result.scale
	    || result.height != sprite.height() * result.scale || area > 4 * 1024 * 1024)
		return std::unexpected("HD frame dimensions disagree with the sprite contract");
	result.pixels.resize(area); result.opacity.resize(area);
	const uint8_t *p = frame.pixelData(), *end = p + frame.pixelDataSize(); size_t position = 0;
	while (p != end) {
		const unsigned control = *p++;
		const size_t run = control >= 191 ? 256 - control : control >= 128 ? 191 - control : control;
		if (control < 128) { position += run; continue; }
		const uint8_t fill = control < 191 ? *p++ : 0;
		for (size_t i = 0; i < run; ++i, ++position) {
			const size_t target = (result.height - 1 - position / result.width) * result.width + position % result.width;
			const uint8_t color = control >= 191 ? *p++ : fill;
			const uint8_t *translation = sprite.hdTranslation();
			result.pixels[target] = translation != nullptr ? translation[color] : color; result.opacity[target] = 1;
		}
	}
	++State.status.frameReads;
	return result;
}
HdSpritePackStatus GetHdSpritePackStatus() { return State.status; }
std::expected<std::vector<uint32_t>, std::string> LoadHdSpriteRgba(ClxSprite sprite)
{
	if (sprite.hdFrameId() == 0) return std::vector<uint32_t> {};
	if (!Mount() || !State.status.mounted) return std::unexpected("HD sprite pack is unavailable");
	const std::string key = "r/" + std::to_string(sprite.hdFrameId());
	const auto cached = State.rgbCache.find(sprite.hdFrameId());
	if (cached != State.rgbCache.end()) return cached->second;
	if (!State.entries.contains(key)) return std::vector<uint32_t> {};
	auto bytes = Read(key);
	if (!bytes || bytes->size() < 8) return std::unexpected("HD RGB frame read failed");
	const size_t width = LoadLE32(bytes->data()), height = LoadLE32(bytes->data() + 4);
	if (width != sprite.width() * sprite.hdScale() || height != sprite.height() * sprite.hdScale()
	    || width * height > 4 * 1024 * 1024 || bytes->size() != 8 + width * height * 4)
		return std::unexpected("HD RGB frame dimensions disagree with the sprite contract");
	std::vector<uint32_t> result(width * height);
	for (size_t i = 0; i < result.size(); ++i) {
		const uint8_t *p = bytes->data() + 8 + 4 * i;
		result[i] = static_cast<uint32_t>(p[3]) << 24 | static_cast<uint32_t>(p[0]) << 16
		    | static_cast<uint32_t>(p[1]) << 8 | p[2];
	}
	constexpr size_t Budget = 8 * 1024 * 1024;
	const size_t size = result.size() * sizeof(uint32_t);
	if (size <= Budget) {
		while (!State.rgbCache.empty() && State.rgbCacheBytes + size > Budget) {
			State.rgbCacheBytes -= State.rgbCache.begin()->second.size() * sizeof(uint32_t);
			State.rgbCache.erase(State.rgbCache.begin());
		}
		State.rgbCache.emplace(sprite.hdFrameId(), result);
		State.rgbCacheBytes += size;
	}
	return result;
}
void ResetHdSpritePack() { State = {}; }
} // namespace devilution
