#include "engine/render/town_lighting_profile.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <system_error>

namespace devilution {
namespace {

std::string_view Trim(std::string_view value) noexcept
{
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos)
		return {};
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

bool ReadFloat(std::string_view text, float &value) noexcept
{
	text = Trim(text);
	// from_chars deliberately omits the leading plus sign in its grammar.
	if (!text.empty() && text.front() == '+') {
		text.remove_prefix(1);
		if (text.empty() || text.front() == '+' || text.front() == '-')
			return false;
	}
	if (text.empty())
		return false;
	float parsed;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, std::chars_format::general);
	if (result.ec != std::errc {} || result.ptr != text.data() + text.size() || !std::isfinite(parsed))
		return false;
	value = parsed;
	return true;
}

bool ReadTriple(std::string_view text, std::array<float, 3> &values) noexcept
{
	for (std::size_t index = 0; index < values.size(); ++index) {
		const auto comma = text.find(',');
		const bool last = index + 1 == values.size();
		if ((last && comma != std::string_view::npos) || (!last && comma == std::string_view::npos))
			return false;
		const auto component = last ? text : text.substr(0, comma);
		if (!ReadFloat(component, values[index]))
			return false;
		if (!last)
			text.remove_prefix(comma + 1);
	}
	return true;
}

bool InRange(const std::array<float, 3> &values, float minimum, float maximum) noexcept
{
	for (const float value : values) {
		if (value < minimum || value > maximum)
			return false;
	}
	return true;
}

} // namespace

TownLightingConfig TristramLightingConfig() noexcept
{
	TownLightingConfig config;
	config.ambient = { 0.08F, 0.085F, 0.09F };
	config.directional = { 1.55F, 1.47F, 1.62F };
	config.toLight = { 0.32F, 1.0F, -0.30F };
	config.directionalIntensity = 1.0F;
	return config;
}

bool ParseTownLightingProfile(std::string_view text, TownLightingConfig &config) noexcept
{
	if (text.size() > TownLightingProfileMaxBytes || text.find('\0') != std::string_view::npos)
		return false;
	if (text.starts_with("\xEF\xBB\xBF"))
		text.remove_prefix(3);

	TownLightingConfig candidate = TristramLightingConfig();
	bool sectionSeen = false;
	unsigned seen = 0;
	while (!text.empty()) {
		const auto newline = text.find('\n');
		auto line = text.substr(0, newline);
		if (newline == std::string_view::npos)
			text = {};
		else
			text.remove_prefix(newline + 1);
		const auto comment = line.find_first_of("#;");
		if (comment != std::string_view::npos)
			line = line.substr(0, comment);
		line = Trim(line);
		if (line.empty())
			continue;
		if (line.front() == '[') {
			if (sectionSeen || line != "[Tristram]")
				return false;
			sectionSeen = true;
			continue;
		}
		if (!sectionSeen)
			return false;
		const auto equals = line.find('=');
		if (equals == std::string_view::npos)
			return false;
		const auto key = Trim(line.substr(0, equals));
		const auto value = Trim(line.substr(equals + 1));
		unsigned bit = 0;
		if (key == "AmbientRGB")
			bit = 1;
		else if (key == "DirectionalRGB")
			bit = 2;
		else if (key == "SunDirection")
			bit = 4;
		else if (key == "DirectionalIntensity")
			bit = 8;
		else
			return false;
		if ((seen & bit) != 0)
			return false;

		if (bit == 8) {
			if (!ReadFloat(value, candidate.directionalIntensity) || candidate.directionalIntensity < 0 || candidate.directionalIntensity > 4)
				return false;
		} else {
			std::array<float, 3> components;
			if (!ReadTriple(value, components) || !InRange(components, bit == 4 ? -4.0F : 0.0F, 4.0F))
				return false;
			if (bit == 1)
				candidate.ambient = { components[0], components[1], components[2] };
			else if (bit == 2)
				candidate.directional = { components[0], components[1], components[2] };
			else {
				const float lengthSquared = components[0] * components[0] + components[1] * components[1] + components[2] * components[2];
				if (components[1] <= 0.0001F || lengthSquared <= 0.000001F)
					return false;
				candidate.toLight = { components[0], components[1], components[2] };
			}
		}
		seen |= bit;
	}
	if (!sectionSeen || seen != 15)
		return false;
	config = candidate;
	return true;
}

} // namespace devilution
