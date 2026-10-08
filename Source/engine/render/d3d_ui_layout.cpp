#include "engine/render/d3d_ui_layout.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "utils/ini.hpp"
#include "utils/log.hpp"

namespace devilution {
namespace {
struct Element {
	std::string anchor;
	int x, y, width, height;
};
std::map<std::string, Element, std::less<>> Elements;

bool ReadInt(const Ini &ini, std::string_view section, std::string_view key, int min, int max, int &value)
{
	const auto text = ini.getString(section, key);
	if (text.empty())
		return false;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && value >= min && value <= max;
}
} // namespace

void ReloadD3dUiLayout()
{
	Elements.clear();
	size_t size = 0;
	auto handle = OpenAsset("d3d-ui/layout.ini", size);
	if (!handle.ok())
		return;
	if (size == 0 || size > 32768) {
		LogWarn("Ignoring UI layout: size outside limits");
		return;
	}
	std::string data(size, '\0');
	if (!handle.read(data.data(), size) || data.find('\0') != std::string::npos)
		return;
	auto parsed = Ini::parse(data);
	if (!parsed || parsed->getString("Layout", "format") != "d3d.ui-layout" || parsed->getString("Layout", "schemaVersion") != "1") {
		LogWarn("Ignoring UI layout: unsupported format");
		return;
	}
	constexpr std::array<std::string_view, 2> HeaderKeys { "format", "schemaVersion" };
	for (const auto &key : parsed->getKeys("Layout")) {
		if (std::find(HeaderKeys.begin(), HeaderKeys.end(), key) == HeaderKeys.end()
		    || parsed->get("Layout", key).size() != 1) {
			LogWarn("Ignoring UI layout: unknown or duplicated header key");
			return;
		}
	}
	// Collect sections explicitly: duplicated definitions must not hide mistakes.
	std::vector<std::string> sections;
	for (size_t begin = 0; begin < data.size();) {
		const auto end = data.find('\n', begin);
		std::string_view line(data.data() + begin, (end == std::string::npos ? data.size() : end) - begin);
		const auto first = line.find_first_not_of(" \t\r");
		if (first != std::string::npos && line[first] == '[') {
			const auto close = line.find(']', first);
			if (close == std::string::npos) return;
			const std::string name(line.substr(first + 1, close - first - 1));
			if (std::find(sections.begin(), sections.end(), name) != sections.end() || sections.size() >= 32) return;
			sections.push_back(name);
		}
		if (end == std::string::npos) break;
		begin = end + 1;
	}
	std::map<std::string, Element, std::less<>> candidate;
	for (const auto &name : sections) {
		if (name == "Layout") continue;
		if (name.empty() || name.size() > 64) return;
		Element element;
		element.anchor = parsed->getString(name, "anchor");
		constexpr std::array<std::string_view, 5> Anchors { "center", "bottom-center", "bottom-right", "top-right", "top-left" };
		if (std::find(Anchors.begin(), Anchors.end(), element.anchor) == Anchors.end()
		    || !ReadInt(*parsed, name, "offsetX", -4096, 4096, element.x)
		    || !ReadInt(*parsed, name, "offsetY", -4096, 4096, element.y)
		    || !ReadInt(*parsed, name, "width", 1, 4096, element.width)
		    || !ReadInt(*parsed, name, "height", 1, 4096, element.height)) {
			LogWarn("Ignoring UI layout: invalid element {}", name);
			return;
		}
		constexpr std::array<std::string_view, 5> Keys { "anchor", "offsetX", "offsetY", "width", "height" };
		for (const auto &key : parsed->getKeys(name))
			if (std::find(Keys.begin(), Keys.end(), key) == Keys.end() || parsed->get(name, key).size() != 1) return;
		candidate.emplace(name, std::move(element));
	}
	Elements = std::move(candidate);
}

Rectangle GetD3dUiRect(std::string_view name, int screenWidth, int screenHeight, Rectangle fallback)
{
	const auto entry = Elements.find(name);
	if (entry == Elements.end() || screenWidth <= 0 || screenHeight <= 0)
		return fallback;
	const auto &e = entry->second;
	int x = 0, y = 0;
	if (e.anchor == "center") { x = screenWidth / 2; y = screenHeight / 2; }
	else if (e.anchor == "bottom-center") { x = screenWidth / 2; y = screenHeight; }
	else if (e.anchor == "bottom-right") { x = screenWidth; y = screenHeight; }
	else if (e.anchor == "top-right") { x = screenWidth; }
	const int width = std::min(e.width, screenWidth), height = std::min(e.height, screenHeight);
	return { { std::clamp(x + e.x, 0, screenWidth - width), std::clamp(y + e.y, 0, screenHeight - height) }, { width, height } };
}

} // namespace devilution
