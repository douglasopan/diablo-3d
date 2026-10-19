#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devilution {

enum class D3dHudSkinPart;

/** Presentation data only: no rectangles, input bindings, scripts or per-part opacity. */
enum class D3dHudThemeColorRole : uint8_t {
	Text,
	MutedText,
	AccentText,
	Border,
	Highlight,
	Plate,
	Life,
	Mana,
	Count,
};

struct D3dHudThemeRgb {
	uint8_t r, g, b;
	bool operator==(const D3dHudThemeRgb &) const = default;
};

struct D3dHudThemeFont {
	/** Exact native resource, e.g. fonts/12-00.clx; language overrides retain their identity. */
	std::string resource;
	std::string path;
	bool operator==(const D3dHudThemeFont &) const = default;
};

struct D3dHudTheme {
	std::string id = "builtin-r1";
	std::string name = "Default";
	/** Order matches the six source PNG parts of D3dHudSkinPart; derived parts have no path. */
	std::array<std::string, 6> images {
		"d3d-ui/hud/r1/chassis.png", "d3d-ui/hud/r1/button.png", "d3d-ui/hud/r1/inset.png",
		"d3d-ui/hud/r1/orb-frame.png", "d3d-ui/hud/r1/orb-red.png", "d3d-ui/hud/r1/orb-blue.png"
	};
	std::vector<D3dHudThemeFont> fonts;
	/** Empty means retain the caller's native color, including translations and palette behavior. */
	std::array<std::optional<D3dHudThemeRgb>, static_cast<size_t>(D3dHudThemeColorRole::Count)> colors {};
	bool operator==(const D3dHudTheme &) const = default;
};

enum class D3dHudThemeResourceKind : uint8_t { Png, FontClx };

struct D3dHudThemeFontGlyphMetrics {
	uint16_t width = 0;
	uint16_t height = 0;
	bool operator==(const D3dHudThemeFontGlyphMetrics &) const = default;
};

/** Returned by a caller-owned decoder; never infer these values from filenames alone. */
struct D3dHudThemeResourceInfo {
	D3dHudThemeResourceKind kind;
	size_t encodedBytes = 0;
	int width = 0; // decoded PNG width or maximum CLX glyph width
	int height = 0; // decoded PNG height or maximum CLX glyph height
	size_t frames = 0; // font rows contain exactly 256 glyph frames
	/** Decoder compares all 256 glyph dimensions with the named native row (advance/wrapping). */
	bool preservesNativeFontMetrics = false;
	std::array<D3dHudThemeFontGlyphMetrics, 256> glyphMetrics {};
};

using D3dHudThemeResourceProbe = std::function<std::expected<D3dHudThemeResourceInfo, std::string>(std::string_view path, D3dHudThemeResourceKind kind, std::string_view nativeFontResource)>;

inline constexpr size_t D3dHudThemeMaxIniBytes = 64 * 1024;
inline constexpr size_t D3dHudThemeMaxFonts = 128;

/** Strict, bounded INI v1 parser. Pure: no file access, decoders or state changes. */
std::expected<D3dHudTheme, std::string> ParseD3dHudThemeIni(std::string_view ini);
std::expected<void, std::string> ValidateD3dHudTheme(const D3dHudTheme &theme);
std::expected<std::string, std::string> SerializeD3dHudThemeIni(const D3dHudTheme &theme);

/** Validate decoded resources through LoadPNG/CLX adapters supplied by integration. */
std::expected<void, std::string> ValidateD3dHudThemeResources(const D3dHudTheme &theme, const D3dHudThemeResourceProbe &probe);
/** Validate raw normal CLX before any native FromBuffer/LoadOptionalClx call. No pointer dereferences outside bytes. */
std::expected<D3dHudThemeResourceInfo, std::string> ValidateD3dHudThemeFontClxBytes(std::span<const uint8_t> bytes);

/** Atomic snapshot selection on the UI thread. Failure selects the complete built-in snapshot. */
std::expected<void, std::string> AcceptD3dHudTheme(D3dHudTheme theme, const D3dHudThemeResourceProbe &probe);
/** Import entry point: malformed INI also selects the whole built-in snapshot. */
std::expected<void, std::string> ImportD3dHudThemeIni(std::string_view ini, const D3dHudThemeResourceProbe &probe);
void ResetD3dHudTheme();
const D3dHudTheme &GetD3dHudTheme();
/** Integration must invalidate cached skin/font resources when this changes. */
uint64_t GetD3dHudThemeRevision();

/** Empty for derived EmptyOrb/BeltTint or an invalid part. */
std::string_view GetD3dHudThemeImagePath(D3dHudSkinPart part);
/** Empty means keep the native resource; accepts native slash or backslash separators. */
std::optional<std::string_view> GetD3dHudThemeFontPath(std::string_view nativeResource);
std::optional<D3dHudThemeRgb> GetD3dHudThemeColor(D3dHudThemeColorRole role);
/** The same caller-owned global byte must be passed for HUD and all floating panels. */
constexpr uint8_t MergeD3dHudThemeOpacity(uint8_t assetAlpha, uint8_t globalOpacity)
{
	return static_cast<uint8_t>((static_cast<unsigned>(assetAlpha) * globalOpacity + 127) / 255);
}

} // namespace devilution
