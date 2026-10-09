#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace devilution::cathedral {

inline constexpr size_t NativePaletteSize = 256;
inline constexpr size_t NativePaletteLightLevels = 16;
using NativePaletteTable = std::array<uint8_t, NativePaletteSize>;
using NativePaletteLightTables = std::array<NativePaletteTable, NativePaletteLightLevels>;
// GPU layout is [source palette index * 16 + discrete native light level].
using NativePaletteLut = std::array<uint8_t, NativePaletteSize * NativePaletteLightLevels>;
// Preserve the native blend orientation [destination * 256 + source].
using NativePaletteBlendLut = std::array<uint8_t, NativePaletteSize * NativePaletteSize>;

inline NativePaletteLut BuildNativeLightLut(const NativePaletteLightTables &tables) noexcept
{
	NativePaletteLut result {};
	for (size_t source = 0; source < NativePaletteSize; ++source) {
		// ClxDrawLight(0) calls ClxDraw directly. In particular source 255
		// survives even though MakeLightTable's table[0][255] is black.
		result[source * NativePaletteLightLevels] = static_cast<uint8_t>(source);
		for (size_t level = 1; level < NativePaletteLightLevels; ++level)
			result[source * NativePaletteLightLevels + level] = tables[level][source];
	}
	return result;
}

inline NativePaletteLut BuildNativeTranslationLut(std::span<const uint8_t, NativePaletteSize> translation) noexcept
{
	NativePaletteLut result {};
	for (size_t source = 0; source < NativePaletteSize; ++source)
		std::fill_n(result.begin() + source * NativePaletteLightLevels, NativePaletteLightLevels, translation[source]);
	return result; // Runtime TRN replaces lighting; there is no second lookup.
}

inline bool CopyNativeBlendLut(std::span<const uint8_t> destinationSourceBytes, NativePaletteBlendLut &result) noexcept
{
	if (destinationSourceBytes.size() != result.size())
		return false;
	std::copy(destinationSourceBytes.begin(), destinationSourceBytes.end(), result.begin());
	return true;
}

inline bool LookupNativePalette(const NativePaletteLut &table, unsigned source, unsigned level,
    bool covered, uint8_t &color) noexcept
{
	if (!covered || source >= NativePaletteSize || level >= NativePaletteLightLevels)
		return false;
	color = table[source * NativePaletteLightLevels + level];
	return true; // Covered palette zero is still an opaque, depth-writing sample.
}

inline uint64_t NativePaletteSignature(std::span<const uint8_t> bytes) noexcept
{
	uint64_t signature = 14695981039346656037ULL;
	const auto add = [&](uint8_t byte) {
		signature ^= byte;
		signature *= 1099511628211ULL;
	};
	const uint64_t size = bytes.size();
	for (unsigned shift = 0; shift < 64; shift += 8)
		add(static_cast<uint8_t>(size >> shift));
	for (const uint8_t byte : bytes)
		add(byte);
	return signature;
}

// A signature is diagnostic provenance. GPU cache revisions are advanced by
// exact byte comparison, so a hash collision cannot preserve a stale LUT.
class NativePaletteRevision {
public:
	[[nodiscard]] bool Update(const NativePaletteLut &next, uint64_t epoch) noexcept
	{
		if (epoch == 0)
			return false;
		if (ready_ && epoch_ == epoch && table_ == next)
			return true;
		if (revision_ == std::numeric_limits<uint64_t>::max())
			return false;
		table_ = next;
		epoch_ = epoch;
		++revision_;
		ready_ = true;
		return true;
	}
	[[nodiscard]] const NativePaletteLut &table() const noexcept { return table_; }
	[[nodiscard]] uint64_t revision() const noexcept { return revision_; }
	[[nodiscard]] uint64_t epoch() const noexcept { return epoch_; }
	[[nodiscard]] bool ready() const noexcept { return ready_; }
	[[nodiscard]] uint64_t signature() const noexcept { return NativePaletteSignature(table_); }

private:
	NativePaletteLut table_ {};
	uint64_t revision_ = 0;
	uint64_t epoch_ = 0;
	bool ready_ = false;
};

} // namespace devilution::cathedral
