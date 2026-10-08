#include <array>
#include <cstdlib>
#include <iostream>

#include "control/d3d_hud_layout.hpp"

using namespace devilution;

namespace {
int Checks = 0;
void Require(bool value, const char *message)
{
	++Checks;
	if (!value) {
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}
bool Intersects(Rectangle a, Rectangle b)
{
	return a.position.x < b.position.x + b.size.width && b.position.x < a.position.x + a.size.width
	    && a.position.y < b.position.y + b.size.height && b.position.y < a.position.y + a.size.height;
}
}

int main()
{
	constexpr std::array<Size, 8> Screens {{ { 640, 480 }, { 800, 600 }, { 1280, 720 }, { 1920, 1080 }, { 2560, 1080 }, { 3440, 1440 }, { 1280, 1024 }, { 640, 360 } }};
	for (Size screen : Screens) {
		std::array<Rectangle, D3dHudLayoutEntries.size()> rects;
		const int logicalWidth = screen.width * 480 / screen.height;
		const int safeWidth = D3dHudSafeWidth(logicalWidth, 480);
		for (size_t i = 0; i < rects.size(); ++i) {
			const auto &entry = D3dHudLayoutEntries[i];
			rects[i] = D3dHudScaleRect(D3dHudPlaceRect(D3dHudDefaultRect(entry, safeWidth, 480), entry, logicalWidth, 480), screen.height);
			const auto &rect = rects[i];
			Require(rect.position.x >= 0 && rect.position.y >= 0 && rect.position.x + rect.size.width <= screen.width && rect.position.y + rect.size.height <= screen.height, "all controls on screen");
			Require(rect.contains(rect.Center()), "rendered center is hittable");
			Require(!rect.contains(rect.position + Displacement { rect.size.width, 0 }), "right boundary exclusive");
			Require(!rect.contains(rect.position + Displacement { 0, rect.size.height }), "bottom boundary exclusive");
		}
		for (size_t i = 0; i < 11; ++i)
			for (size_t j = i + 1; j < 11; ++j)
				if (i != 4 && j != 4) Require(!Intersects(rects[i], rects[j]), "default actionable controls do not overlap");
		int next = rects[2].position.x;
		for (int i = 0; i < 8; ++i) {
			const Rectangle slot = D3dHudBeltSlotRect(rects[2], i);
			Require(slot.position.x == next, "belt has no gaps or duplicate boundary");
			Require(slot.contains(slot.Center()), "all eight belt slots hittable");
			next += slot.size.width;
		}
		Require(next == rects[2].position.x + rects[2].size.width, "belt subdivides full width");
		if (safeWidth >= 844) Require(rects[5].position.y > rects[3].position.y, "wide utilities occupy bottom row");
		else Require(rects[5].position.y + rects[5].size.height < rects[0].position.y, "compact utilities clear the core");
		std::cout << screen.width << 'x' << screen.height << ": belt=" << rects[2].position.x << ',' << rects[2].position.y << ',' << rects[2].size.width << ',' << rects[2].size.height << '\n';
	}
	Rectangle editedBelt { { 27, 42 }, { 239, 37 } };
	for (int x = 27; x < 266; ++x) {
		int matches = 0;
		for (int i = 0; i < 8; ++i) matches += D3dHudBeltSlotRect(editedBelt, i).contains(Point { x, 42 });
		Require(matches == 1, "edited non-divisible belt has exactly one slot per pixel");
	}
	const auto &entry = D3dHudLayoutEntries[0];
	const Rectangle clamped = D3dHudPlaceRect({ { -9999, 9999 }, { 9999, 9999 } }, entry, 853, 480);
	Require(clamped.position.x == 0 && clamped.position.y == 0 && clamped.size.width == 853 && clamped.size.height == 480, "edited oversized rect clamps safely");
	std::cout << Checks << " HUD geometry checks passed\n";
}
