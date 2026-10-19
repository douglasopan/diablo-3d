#include "engine/render/town_actor_mask.hpp"

#include <algorithm>
#include <cstddef>

namespace devilution {

std::vector<uint8_t> TownActorBodyOpacity(const TownVolumeSprite &sprite)
{
	if (sprite.width <= 0 || sprite.height <= 0 || sprite.width > 512 || sprite.height > 512)
		return {};
	const size_t size = static_cast<size_t>(sprite.width) * sprite.height;
	if (sprite.pixels.size() < size || sprite.opacity.size() < size)
		return {};
	std::vector<uint8_t> body(sprite.opacity.begin(), sprite.opacity.begin() + size);
	int firstColored = sprite.height;
	int lastColored = -1;
	for (int y = 0; y < sprite.height; ++y) {
		for (int x = 0; x < sprite.width; ++x) {
			const size_t index = static_cast<size_t>(y) * sprite.width + x;
			if (body[index] != 0 && sprite.pixels[index] != 0) {
				firstColored = std::min(firstColored, y);
				lastColored = std::max(lastColored, y);
			}
		}
	}
	if (lastColored < 0)
		return body;
	// Isometric ground shadows can extend upward beside the knees or an animal's
	// belly: a small band under the lowest foot leaves most of the shadow intact.
	// Keep the entire upper colored-body half, but separate exterior zero paint
	// in the lower half. Enclosed zero paint remains protected by the flood mask.
	const int groundZone = firstColored + (lastColored - firstColored + 1) / 2;
	std::vector<uint8_t> exterior(size, 0);
	std::vector<size_t> pending;
	pending.reserve(size);
	const auto visit = [&](size_t index) {
		if (exterior[index] != 0 || (body[index] != 0 && sprite.pixels[index] != 0))
			return;
		exterior[index] = 1;
		pending.push_back(index);
	};
	for (int x = 0; x < sprite.width; ++x) {
		visit(static_cast<size_t>(x));
		visit(static_cast<size_t>(sprite.height - 1) * sprite.width + x);
	}
	for (int y = 0; y < sprite.height; ++y) {
		visit(static_cast<size_t>(y) * sprite.width);
		visit(static_cast<size_t>(y) * sprite.width + sprite.width - 1);
	}
	for (size_t next = 0; next < pending.size(); ++next) {
		const size_t index = pending[next];
		const size_t x = index % sprite.width;
		const size_t y = index / sprite.width;
		if (x != 0)
			visit(index - 1);
		if (x + 1 < static_cast<size_t>(sprite.width))
			visit(index + 1);
		if (y != 0)
			visit(index - sprite.width);
		if (y + 1 < static_cast<size_t>(sprite.height))
			visit(index + sprite.width);
	}
	for (int y = groundZone; y < sprite.height; ++y) {
		for (int x = 0; x < sprite.width; ++x) {
			const size_t index = static_cast<size_t>(y) * sprite.width + x;
			if (exterior[index] != 0 && sprite.pixels[index] == 0)
				body[index] = 0;
		}
	}
	return body;
}

} // namespace devilution
