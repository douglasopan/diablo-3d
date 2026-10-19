#include "control/d3d_hud_theme.hpp"

#include <algorithm>
#include <charconv>
#include <set>
#include <utility>

namespace devilution {
namespace {

constexpr std::array<std::string_view, 6> ImageKeys { "Chassis", "Button", "Inset", "OrbFrame", "LifeLiquid", "ManaLiquid" };
constexpr std::array<std::string_view, 8> ColorKeys { "Text", "MutedText", "AccentText", "Border", "Highlight", "Plate", "Life", "Mana" };
// Fixed R1 source rectangles in d3d_hud_skin.cpp, including the liquid crop before DeriveLiquid.
constexpr std::array<std::pair<int, int>, 6> MinimumImageSizes {
	std::pair { 2150, 521 }, { 2030, 601 }, { 1972, 601 }, { 1076, 1349 }, { 1195, 1190 }, { 1199, 1195 }
};
constexpr std::array<int, 6> FontSizes { 12, 24, 30, 42, 46, 22 };
constexpr size_t MaxPathBytes = 240;
constexpr size_t MaxEncodedPngBytes = 16 * 1024 * 1024;
constexpr size_t MaxEncodedFontBytes = 4 * 1024 * 1024;
constexpr size_t MaxDecodedImageBytes = 256 * 1024 * 1024;
D3dHudTheme SelectedTheme;
uint64_t ThemeRevision = 0;

bool AsciiAlnum(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

std::string_view Trim(std::string_view value)
{
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.remove_suffix(1);
	return value;
}

bool IsUtf8Name(std::string_view value)
{
	if (value.empty() || value.size() > 96 || Trim(value) != value) return false;
	for (size_t i = 0; i < value.size();) {
		const unsigned first = static_cast<unsigned char>(value[i++]);
		if (first < 0x80) { if (first < 0x20 || first == 0x7F || first == '[' || first == ']' || first == '=' || first == ';' || first == '#') return false; continue; }
		unsigned count, code;
		if (first >= 0xC2 && first <= 0xDF) { count = 1; code = first & 0x1F; }
		else if (first >= 0xE0 && first <= 0xEF) { count = 2; code = first & 0x0F; }
		else if (first >= 0xF0 && first <= 0xF4) { count = 3; code = first & 0x07; }
		else return false;
		if (i + count > value.size()) return false;
		for (unsigned j = 0; j < count; ++j) {
			const unsigned next = static_cast<unsigned char>(value[i++]);
			if ((next & 0xC0) != 0x80) return false;
			code = (code << 6) | (next & 0x3F);
		}
		if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) || (count == 3 && code < 0x10000)
		    || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF) || (code >= 0x80 && code <= 0x9F)) return false;
	}
	return true;
}

bool IsId(std::string_view value)
{
	return !value.empty() && value.size() <= 48 && std::all_of(value.begin(), value.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
	});
}

bool IsDevice(std::string_view component)
{
	std::string stem(component.substr(0, component.find('.')));
	std::transform(stem.begin(), stem.end(), stem.begin(), [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; });
	return stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
	    || (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9');
}

std::expected<std::string, std::string> NormalizePath(std::string_view path, std::string_view extension)
{
	if (path.empty() || path.size() > MaxPathBytes) return std::unexpected("Asset path length is invalid");
	std::string result(path);
	std::replace(result.begin(), result.end(), '\\', '/');
	if (result.front() == '/' || !result.ends_with(extension) || result.find("..") != std::string::npos)
		return std::unexpected("Asset must be a relative path with the required extension");
	for (const char c : result) if (!AsciiAlnum(c) && c != '/' && c != '-' && c != '_' && c != '.')
		return std::unexpected("Asset path contains an unsupported character");
	size_t start = 0;
	while (start < result.size()) {
		const size_t slash = result.find('/', start);
		const auto component = std::string_view(result).substr(start, slash == std::string::npos ? result.size() - start : slash - start);
		if (component.empty() || component.size() > 64 || component.front() == '.' || component.back() == '.' || IsDevice(component))
			return std::unexpected("Asset path contains an invalid component");
		if (slash == std::string::npos) break;
		start = slash + 1;
	}
	return result;
}

bool IsNativeFontResource(std::string_view resource)
{
	if (!resource.starts_with("fonts/") || !resource.ends_with(".clx")) return false;
	resource.remove_prefix(6);
	if (const auto slash = resource.find('/'); slash != std::string_view::npos) {
		const auto language = resource.substr(0, slash);
		if (language.empty() || language.size() > 16 || !std::all_of(language.begin(), language.end(), [](char c) { return AsciiAlnum(c) || c == '_' || c == '-'; })) return false;
		resource.remove_prefix(slash + 1);
		if (resource.find('/') != std::string_view::npos) return false;
	}
	resource.remove_suffix(4);
	const auto dash = resource.find('-');
	if (dash == std::string_view::npos) return false;
	const auto sizeText = resource.substr(0, dash);
	int size = 0;
	const auto sizeResult = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), size);
	if (sizeResult.ec != std::errc {} || sizeResult.ptr != sizeText.data() + sizeText.size()
	    || std::find(FontSizes.begin(), FontSizes.end(), size) == FontSizes.end() || sizeText != std::to_string(size)) return false;
	const auto rowText = resource.substr(dash + 1);
	unsigned row = 0;
	const auto rowResult = std::from_chars(rowText.data(), rowText.data() + rowText.size(), row, 16);
	return rowText.size() >= 2 && rowText.size() <= 4 && (rowText.size() == 2 || rowText.front() != '0')
	    && rowResult.ec == std::errc {} && rowResult.ptr == rowText.data() + rowText.size()
	    && row <= 0x10FF && std::all_of(rowText.begin(), rowText.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

template <size_t N>
std::optional<size_t> KeyIndex(const std::array<std::string_view, N> &keys, std::string_view key)
{
	const auto found = std::find(keys.begin(), keys.end(), key);
	if (found == keys.end()) return std::nullopt;
	return static_cast<size_t>(found - keys.begin());
}

std::expected<D3dHudThemeRgb, std::string> ParseColor(std::string_view value)
{
	if (value.size() != 7 || value.front() != '#') return std::unexpected("Color must be #RRGGBB without alpha");
	unsigned rgb = 0;
	const auto result = std::from_chars(value.data() + 1, value.data() + value.size(), rgb, 16);
	if (result.ec != std::errc {} || result.ptr != value.data() + value.size()) return std::unexpected("Color must be #RRGGBB without alpha");
	return D3dHudThemeRgb { static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb) };
}

} // namespace

std::expected<void, std::string> ValidateD3dHudTheme(const D3dHudTheme &theme)
{
	if (!IsId(theme.id) || !IsUtf8Name(theme.name)) return std::unexpected("Theme identity is invalid");
	for (const auto &path : theme.images) {
		const auto normalized = NormalizePath(path, ".png");
		if (!normalized || *normalized != path) return std::unexpected("Theme image path is invalid or not canonical");
	}
	if (theme.fonts.size() > D3dHudThemeMaxFonts) return std::unexpected("Too many font overrides");
	std::set<std::string> seen;
	std::string_view previous;
	for (const auto &font : theme.fonts) {
		const auto normalized = NormalizePath(font.path, ".clx");
		if (!IsNativeFontResource(font.resource) || !normalized || *normalized != font.path || !seen.insert(font.resource).second
		    || (!previous.empty() && font.resource < previous))
			return std::unexpected("Font override is invalid, duplicated or not canonically sorted");
		previous = font.resource;
	}
	return {};
}

std::expected<D3dHudTheme, std::string> ParseD3dHudThemeIni(std::string_view ini)
{
	if (ini.empty() || ini.size() > D3dHudThemeMaxIniBytes) return std::unexpected("Theme INI must contain 1..65536 bytes");
	if (ini.starts_with("\xEF\xBB\xBF")) ini.remove_prefix(3);
	D3dHudTheme theme;
	std::string section;
	std::set<std::string> sections, fields;
	bool format = false, version = false, id = false, name = false;
	size_t lineNumber = 0;
	while (!ini.empty()) {
		const size_t newline = ini.find('\n');
		auto line = ini.substr(0, newline);
		if (newline == std::string_view::npos) ini = {}; else ini.remove_prefix(newline + 1);
		++lineNumber;
		const auto error = [lineNumber](std::string message) { return std::unexpected("Theme INI line " + std::to_string(lineNumber) + ": " + message); };
		if (lineNumber > 512 || line.size() > 1024) return error("line limits exceeded");
		for (const char c : line) if ((static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\r') || c == 0x7F) return error("control characters are not allowed");
		line = Trim(line);
		if (line.empty() || line.front() == ';' || line.front() == '#') continue;
		if (line.front() == '[') {
			if (line.back() != ']') return error("invalid section");
			section = std::string(line.substr(1, line.size() - 2));
			if (section != "Theme" && section != "Images" && section != "Fonts" && section != "Colors") return error("unknown section");
			if (!sections.insert(section).second) return error("duplicate section");
			continue;
		}
		const auto equals = line.find('=');
		if (section.empty() || equals == std::string_view::npos) return error("expected section and key=value");
		const auto key = Trim(line.substr(0, equals));
		const auto value = Trim(line.substr(equals + 1));
		if (key.empty() || value.empty() || !fields.insert(section + "/" + std::string(key)).second) return error("empty or duplicate field");
		if (section == "Theme") {
			if (key == "format") { if (value != "d3d.hud-theme") return error("unsupported format"); format = true; }
			else if (key == "schemaVersion") { if (value != "1") return error("unsupported schemaVersion"); version = true; }
			else if (key == "id") { if (!IsId(value)) return error("invalid id"); theme.id = std::string(value); id = true; }
			else if (key == "name") { if (!IsUtf8Name(value)) return error("invalid name"); theme.name = std::string(value); name = true; }
			else return error("unknown Theme field");
		} else if (section == "Images") {
			const auto index = KeyIndex(ImageKeys, key);
			if (!index) return error("unknown image part; derived images cannot be replaced");
			const auto path = NormalizePath(value, ".png");
			if (!path) return error(path.error());
			theme.images[*index] = *path;
		} else if (section == "Fonts") {
			const auto resource = NormalizePath(key, ".clx"), path = NormalizePath(value, ".clx");
			if (!resource || !IsNativeFontResource(*resource) || !path) return error("invalid CLX resource or replacement path");
			if (theme.fonts.size() >= D3dHudThemeMaxFonts) return error("too many font overrides");
			theme.fonts.push_back({ *resource, *path });
		} else {
			const auto index = KeyIndex(ColorKeys, key);
			if (!index) return error("unknown color role");
			const auto color = ParseColor(value);
			if (!color) return error(color.error());
			theme.colors[*index] = *color;
		}
	}
	if (!format || !version || !id) return std::unexpected("Theme requires format, schemaVersion=1 and id");
	if (!name) theme.name = theme.id;
	std::sort(theme.fonts.begin(), theme.fonts.end(), [](const auto &a, const auto &b) { return a.resource < b.resource; });
	const auto valid = ValidateD3dHudTheme(theme);
	if (!valid) return std::unexpected(valid.error());
	return theme;
}

std::expected<std::string, std::string> SerializeD3dHudThemeIni(const D3dHudTheme &theme)
{
	if (const auto valid = ValidateD3dHudTheme(theme); !valid) return std::unexpected(valid.error());
	std::string result = "[Theme]\nformat=d3d.hud-theme\nschemaVersion=1\nid=" + theme.id + "\nname=" + theme.name + "\n\n[Images]\n";
	for (size_t i = 0; i < ImageKeys.size(); ++i) result += std::string(ImageKeys[i]) + "=" + theme.images[i] + "\n";
	result += "\n[Fonts]\n";
	auto fonts = theme.fonts;
	std::sort(fonts.begin(), fonts.end(), [](const auto &a, const auto &b) { return a.resource < b.resource; });
	for (const auto &font : fonts) result += font.resource + "=" + font.path + "\n";
	result += "\n[Colors]\n";
	constexpr char Hex[] = "0123456789ABCDEF";
	for (size_t i = 0; i < ColorKeys.size(); ++i) {
		if (!theme.colors[i]) continue;
		result += std::string(ColorKeys[i]) + "=#";
		for (uint8_t channel : { theme.colors[i]->r, theme.colors[i]->g, theme.colors[i]->b }) { result += Hex[channel >> 4]; result += Hex[channel & 15]; }
		result += '\n';
	}
	if (result.size() > D3dHudThemeMaxIniBytes) return std::unexpected("Serialized theme exceeds byte limit");
	return result;
}

std::expected<void, std::string> ValidateD3dHudThemeResources(const D3dHudTheme &theme, const D3dHudThemeResourceProbe &probe)
{
	if (const auto valid = ValidateD3dHudTheme(theme); !valid) return std::unexpected(valid.error());
	if (!probe) return std::unexpected("Theme resource decoder is required");
	const auto decoded = [&probe](std::string_view path, D3dHudThemeResourceKind kind, std::string_view nativeFont) -> std::expected<D3dHudThemeResourceInfo, std::string> {
		try { return probe(path, kind, nativeFont); }
		catch (...) { return std::unexpected("Theme resource decoder failed"); }
	};
	size_t imageBytes = 0;
	for (size_t i = 0; i < theme.images.size(); ++i) {
		const auto resource = decoded(theme.images[i], D3dHudThemeResourceKind::Png, {});
		if (!resource) return std::unexpected("PNG resource rejected: " + theme.images[i] + ": " + resource.error());
		if (resource->kind != D3dHudThemeResourceKind::Png || resource->encodedBytes == 0 || resource->encodedBytes > MaxEncodedPngBytes
		    || resource->width < MinimumImageSizes[i].first || resource->height < MinimumImageSizes[i].second
		    || resource->width > 4096 || resource->height > 4096) return std::unexpected("PNG dimensions or byte budget violate the R1 source contract: " + theme.images[i]);
		imageBytes += static_cast<size_t>(resource->width) * resource->height * 4;
		if (imageBytes > MaxDecodedImageBytes) return std::unexpected("Theme decoded image budget exceeded");
	}
	for (const auto &font : theme.fonts) {
		const auto resource = decoded(font.path, D3dHudThemeResourceKind::FontClx, font.resource);
		if (!resource) return std::unexpected("CLX resource rejected: " + font.path + ": " + resource.error());
		if (resource->kind != D3dHudThemeResourceKind::FontClx || resource->encodedBytes == 0 || resource->encodedBytes > MaxEncodedFontBytes
		    || resource->frames != 256 || resource->width < 1 || resource->width > 128 || resource->height < 1 || resource->height > 128
		    || !resource->preservesNativeFontMetrics)
			return std::unexpected("CLX must preserve the native metrics of a bounded 256-glyph row: " + font.path);
	}
	return {};
}

std::expected<void, std::string> AcceptD3dHudTheme(D3dHudTheme theme, const D3dHudThemeResourceProbe &probe)
{
	if (const auto valid = ValidateD3dHudThemeResources(theme, probe); !valid) { ResetD3dHudTheme(); return std::unexpected(valid.error()); }
	std::sort(theme.fonts.begin(), theme.fonts.end(), [](const auto &a, const auto &b) { return a.resource < b.resource; });
	SelectedTheme = std::move(theme);
	++ThemeRevision;
	return {};
}

std::expected<D3dHudThemeResourceInfo, std::string> ValidateD3dHudThemeFontClxBytes(std::span<const uint8_t> bytes)
{
	constexpr size_t Frames = 256;
	constexpr size_t TableBytes = (Frames + 2) * 4;
	if (bytes.size() < TableBytes || bytes.size() > MaxEncodedFontBytes)
		return std::unexpected("CLX font list size must be 1032..4194304 bytes");
	// The fixed table size was checked before any reads. Frame metadata reads occur
	// only after checking start/end against the span; no native unchecked loader is used.
	const auto read32 = [&bytes](size_t at) {
		return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8)
		    | (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
	};
	const auto read16 = [&bytes](size_t at) {
		return static_cast<uint16_t>(static_cast<unsigned>(bytes[at]) | (static_cast<unsigned>(bytes[at + 1]) << 8));
	};
	if (read32(0) != Frames || read32(4) != TableBytes || read32(4 + Frames * 4) != bytes.size())
		return std::unexpected("CLX requires one exact 256-frame list and no trailing data/sheets");
	D3dHudThemeResourceInfo result { D3dHudThemeResourceKind::FontClx, bytes.size(), 0, 0, Frames, false, {} };
	for (size_t frame = 0; frame < Frames; ++frame) {
		const size_t start = read32(4 + frame * 4), end = read32(8 + frame * 4);
		if (start < TableBytes || end > bytes.size() || start > end || end - start < 6)
			return std::unexpected("CLX frame offsets must be increasing, bounded and contain a header");
		if (read16(start) != 6)
			return std::unexpected("CLX imported fonts require a normal six-byte header; HD/external headers are rejected");
		const auto width = read16(start + 2), height = read16(start + 4);
		if (width > 128 || height > 128) return std::unexpected("CLX glyph dimensions exceed 128");
		result.glyphMetrics[frame] = { width, height };
		result.width = std::max(result.width, static_cast<int>(width));
		result.height = std::max(result.height, static_cast<int>(height));
		const size_t area = static_cast<size_t>(width) * height;
		size_t cursor = start + 6, pixels = 0;
		// Native font files contain width-zero missing glyphs with a nonzero height.
		// Their empty payload is valid. Never take modulo/divide by a zero width.
		if (area == 0) {
			if (cursor != end) return std::unexpected("CLX zero-area glyph must have no RLE payload");
			continue;
		}
		while (cursor < end) {
			const uint8_t command = bytes[cursor++];
			if (command == 0) return std::unexpected("CLX zero-length RLE commands are rejected");
			const bool opaque = command >= 0x80;
			const size_t run = !opaque ? command : command <= 0xBE ? 0xBFU - command : 256U - command;
			const size_t payload = !opaque ? 0 : command <= 0xBE ? 1 : run;
			if (payload > end - cursor) return std::unexpected("CLX RLE color/literal payload is truncated");
			if (run > area - pixels) return std::unexpected("CLX RLE expands beyond glyph dimensions");
			// Only transparent runs may cross scanlines. Native outline/unclipped
			// painters assume opaque writes stay within the current row/pitch.
			if (opaque && run > width - pixels % width) return std::unexpected("CLX opaque RLE crosses a glyph scanline");
			cursor += payload;
			pixels += run;
		}
		if (pixels != area) return std::unexpected("CLX RLE does not cover the full declared glyph area");
	}
	if (result.width == 0 || result.height == 0) return std::unexpected("CLX font row must contain visible-sized glyphs");
	// Structural safety is separate from native advance/baseline compatibility.
	// Caller compares glyphMetrics against the separately validated native row.
	return result;
}

void ResetD3dHudTheme() { SelectedTheme = D3dHudTheme {}; ++ThemeRevision; }
std::expected<void, std::string> ImportD3dHudThemeIni(std::string_view ini, const D3dHudThemeResourceProbe &probe)
{
	auto theme = ParseD3dHudThemeIni(ini);
	if (!theme) { ResetD3dHudTheme(); return std::unexpected(theme.error()); }
	return AcceptD3dHudTheme(std::move(*theme), probe);
}
const D3dHudTheme &GetD3dHudTheme() { return SelectedTheme; }
uint64_t GetD3dHudThemeRevision() { return ThemeRevision; }

std::string_view GetD3dHudThemeImagePath(D3dHudSkinPart part)
{
	const auto index = static_cast<size_t>(part);
	return index < SelectedTheme.images.size() ? std::string_view(SelectedTheme.images[index]) : std::string_view {};
}

std::optional<std::string_view> GetD3dHudThemeFontPath(std::string_view nativeResource)
{
	std::string resource(nativeResource);
	std::replace(resource.begin(), resource.end(), '\\', '/');
	for (const auto &font : SelectedTheme.fonts) if (font.resource == resource) return font.path;
	return std::nullopt;
}

std::optional<D3dHudThemeRgb> GetD3dHudThemeColor(D3dHudThemeColorRole role)
{
	const auto index = static_cast<size_t>(role);
	return index < SelectedTheme.colors.size() ? SelectedTheme.colors[index] : std::nullopt;
}

} // namespace devilution
