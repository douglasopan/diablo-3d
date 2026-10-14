#include "engine/render/d3d_logo.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "utils/clx_encode.hpp"
#include "utils/endian_read.hpp"
#include "utils/endian_write.hpp"
#include "utils/log.hpp"
#include "utils/pcx.hpp"

namespace devilution {
namespace {

constexpr size_t PaletteTrailerSize = 1 + 256 * 3;

bool SameRgb(const D3dLogoPalette &left, const D3dLogoPalette &right)
{
	for (size_t i = 0; i < left.size(); ++i) {
		if (left[i].r != right[i].r || left[i].g != right[i].g || left[i].b != right[i].b)
			return false;
	}
	return true;
}

void AppendFrame(std::span<const uint8_t> pixels, const D3dLogoSpec &spec, std::vector<uint8_t> &encoded)
{
	const size_t header = encoded.size();
	encoded.resize(header + ClxFrameHeaderSize);
	WriteLE16(&encoded[header], ClxFrameHeaderSize);
	WriteLE16(&encoded[header + 2], spec.width);
	WriteLE16(&encoded[header + 4], spec.frameHeight);
	unsigned transparentRun = 0;
	for (unsigned row = spec.frameHeight; row != 0; --row) {
		const uint8_t *line = pixels.data() + static_cast<size_t>(row - 1) * spec.width;
		unsigned x = 0;
		while (x < spec.width) {
			if (line[x] == D3dLogoTransparentIndex) {
				++transparentRun;
				++x;
				continue;
			}
			AppendClxTransparentRun(transparentRun, encoded);
			transparentRun = 0;
			const unsigned begin = x;
			while (x < spec.width && line[x] != D3dLogoTransparentIndex)
				++x;
			AppendClxPixelsOrFillRun(line + begin, x - begin, encoded);
		}
	}
	AppendClxTransparentRun(transparentRun, encoded);
}

size_t MaxPcxSize(const D3dLogoSpec &spec)
{
	// An indexed pixel needs at most two PCX bytes. Bound allocations before reading.
	return PcxHeaderSize + static_cast<size_t>(spec.width) * spec.frameHeight * D3dLogoFrameCount * 2 + PaletteTrailerSize;
}

} // namespace

D3dLogoSpec GetD3dLogoSpec(D3dLogoKind kind)
{
	switch (kind) {
	case D3dLogoKind::Menu:
		return { "ui_art\\d3d-menu.pcx", 580, 154 };
	case D3dLogoKind::Title:
		return { "ui_art\\d3d-title.pcx", 620, 216 };
	case D3dLogoKind::Pause:
		return { "ui_art\\d3d-pause.pcx", 360, 90 };
	}
	return { "", 0, 0 };
}

D3dLogo::D3dLogo(OwnedClxSpriteList sprites, D3dLogoPalette palette)
    : sprites_(std::move(sprites))
    , palette_(palette)
{
}

const std::array<uint8_t, 256> &D3dLogo::translation(const D3dLogoPalette &target) const
{
	if (translationValid_ && SameRgb(target, translatedPalette_))
		return translation_;
	for (size_t source = 0; source < palette_.size(); ++source) {
		unsigned bestDistance = std::numeric_limits<unsigned>::max();
		uint8_t best = 0;
		for (size_t candidate = 0; candidate < target.size(); ++candidate) {
			if (candidate == D3dLogoTransparentIndex)
				continue;
			const int dr = static_cast<int>(palette_[source].r) - target[candidate].r;
			const int dg = static_cast<int>(palette_[source].g) - target[candidate].g;
			const int db = static_cast<int>(palette_[source].b) - target[candidate].b;
			const unsigned distance = static_cast<unsigned>(dr * dr + dg * dg + db * db);
			if (distance < bestDistance) {
				bestDistance = distance;
				best = static_cast<uint8_t>(candidate);
			}
		}
		translation_[source] = best;
	}
	translatedPalette_ = target;
	translationValid_ = true;
	return translation_;
}

std::optional<D3dLogo> DecodeD3dLogoPcx(std::span<const uint8_t> data, D3dLogoKind kind)
{
	const D3dLogoSpec spec = GetD3dLogoSpec(kind);
	if (spec.width == 0 || data.size() < PcxHeaderSize + PaletteTrailerSize || data.size() > MaxPcxSize(spec))
		return std::nullopt;
	// uint16_t coordinates deliberately support sheets taller than INT16_MAX.
	if (data[0] != 0x0A || data[1] != 5 || data[2] != 1 || data[3] != 8 || data[65] != 1
	    || LoadLE16(&data[4]) != 0 || LoadLE16(&data[6]) != 0
	    || static_cast<uint32_t>(LoadLE16(&data[8])) + 1 != spec.width
	    || static_cast<uint32_t>(LoadLE16(&data[10])) + 1 != static_cast<uint32_t>(spec.frameHeight) * D3dLogoFrameCount
	    || LoadLE16(&data[66]) != spec.width)
		return std::nullopt;
	const size_t paletteOffset = data.size() - PaletteTrailerSize;
	if (data[paletteOffset] != 0x0C)
		return std::nullopt;
	D3dLogoPalette palette {};
	for (size_t i = 0; i < palette.size(); ++i) {
		palette[i].r = data[paletteOffset + 1 + 3 * i];
		palette[i].g = data[paletteOffset + 2 + 3 * i];
		palette[i].b = data[paletteOffset + 3 + 3 * i];
#ifndef USE_SDL1
		palette[i].a = SDL_ALPHA_OPAQUE;
#endif
	}
	std::vector<uint8_t> encoded(4 * (D3dLogoFrameCount + 2));
	encoded.reserve(data.size());
	WriteLE32(encoded.data(), D3dLogoFrameCount);
	std::vector<uint8_t> pixels(static_cast<size_t>(spec.width) * spec.frameHeight);
	size_t cursor = PcxHeaderSize;
	for (uint32_t frame = 0; frame < D3dLogoFrameCount; ++frame) {
		for (unsigned row = 0; row < spec.frameHeight; ++row) {
			unsigned x = 0;
			while (x < spec.width) {
				if (cursor >= paletteOffset)
					return std::nullopt;
				const uint8_t command = data[cursor++];
				unsigned length = 1;
				uint8_t color = command;
				if ((command & 0xC0) == 0xC0) {
					length = command & 0x3F;
					if (length == 0 || cursor >= paletteOffset)
						return std::nullopt;
					color = data[cursor++];
				}
				if (length > spec.width - x)
					return std::nullopt;
				std::fill_n(pixels.data() + static_cast<size_t>(row) * spec.width + x, length, color);
				x += length;
			}
		}
		WriteLE32(&encoded[4 * (frame + 1)], static_cast<uint32_t>(encoded.size()));
		AppendFrame(pixels, spec, encoded);
	}
	if (cursor != paletteOffset)
		return std::nullopt;
	WriteLE32(&encoded[4 * (D3dLogoFrameCount + 1)], static_cast<uint32_t>(encoded.size()));
	auto owned = std::make_unique<uint8_t[]>(encoded.size());
	std::memcpy(owned.get(), encoded.data(), encoded.size());
	return D3dLogo { OwnedClxSpriteList { std::move(owned) }, palette };
}

std::optional<D3dLogo> LoadD3dLogo(D3dLogoKind kind)
{
	const D3dLogoSpec spec = GetD3dLogoSpec(kind);
	if (spec.width == 0)
		return std::nullopt;
	size_t fileSize = 0;
	AssetHandle handle = OpenAsset(spec.path, fileSize);
	if (!handle.ok())
		return std::nullopt;
	if (fileSize < PcxHeaderSize + PaletteTrailerSize || fileSize > MaxPcxSize(spec)) {
		LogWarn("Ignoring invalid custom animated logo: {}", spec.path);
		return std::nullopt;
	}
	std::vector<uint8_t> data(fileSize);
	if (!handle.read(data.data(), data.size()))
		return std::nullopt;
	auto logo = DecodeD3dLogoPcx(data, kind);
	if (!logo)
		LogWarn("Ignoring invalid custom animated logo: {}", spec.path);
	return logo;
}

void DrawD3dLogo(const Surface &out, Point bottomPosition, const D3dLogo &logo, uint32_t frame)
{
	const ClxSpriteList sprites = logo.sprites();
	if (frame >= sprites.numSprites())
		return;
	ClxDrawTRN(out, bottomPosition, sprites[frame], logo.translation(logical_palette).data());
}

} // namespace devilution
