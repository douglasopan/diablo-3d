#include <array>
#include <cstdlib>
#include <iostream>

#include "control/d3d_hud_layout.hpp"

using namespace devilution;

namespace {
int Checks = 0;
using HudRects = std::array<Rectangle, D3dHudLayoutEntries.size()>;
constexpr std::array<size_t, 4> LeftColumn { 5, 8, 9, 10 }; // CHAR, QUESTS, MAP, MENU
constexpr std::array<size_t, 2> RightColumn { 6, 7 }; // INV, SPELLS
constexpr std::array<size_t, 6> NativeButtons { 5, 8, 9, 10, 6, 7 };

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

HudRects DefaultRects(Size screen)
{
	HudRects rects;
	const int logicalWidth = screen.width * 480 / screen.height;
	const int safeWidth = D3dHudSafeWidth(logicalWidth, 480);
	for (size_t i = 0; i < rects.size(); ++i) {
		const auto &entry = D3dHudLayoutEntries[i];
		rects[i] = D3dHudScaleRect(D3dHudPlaceRect(D3dHudDefaultRect(entry, safeWidth, 480), entry, logicalWidth, 480), screen.height);
	}
	return rects;
}

void CheckComposition(const HudRects &rects, Size screen)
{
	const int logicalWidth = screen.width * 480 / screen.height;
	const Rectangle frame = D3dHudScaleRect({ { (logicalWidth - 640) / 2, 480 - 112 }, { 640, 104 } }, screen.height);
	// The body contains the belt, information, spell and six native buttons.
	// Complete globe sculptures and multiplayer buttons sit above its top edge.
	for (size_t i = 2; i < 11; ++i) {
		const Rectangle rect = rects[i];
		Require(rect.position.x >= frame.position.x && rect.position.x + rect.size.width <= frame.position.x + frame.size.width
		        && rect.position.y >= frame.position.y && rect.position.y + rect.size.height <= frame.position.y + frame.size.height,
		    "body controls share the centered 640x104 frame above the bottom margin");
	}
	for (size_t i = 0; i < 2; ++i) {
		Require(rects[i].position.y < frame.position.y && rects[i].position.y + rects[i].size.height <= frame.position.y + frame.size.height, "complete globe sculptures protrude above the body frame");
		Require(rects[11 + i].position.y + rects[11 + i].size.height < rects[i].position.y, "multiplayer buttons have a separate row above their globe");
	}
	Require(rects[2].position.y + rects[2].size.height < rects[4].position.y, "belt sits above the information area");
	Require(std::abs(rects[2].Center().x - rects[4].Center().x) <= 1, "belt and information share the center");
	Require(rects[2].position.x >= rects[4].position.x && rects[2].position.x + rects[2].size.width <= rects[4].position.x + rects[4].size.width, "information area spans the belt width");
	for (size_t row = 1; row < LeftColumn.size(); ++row) {
		const Rectangle previous = rects[LeftColumn[row - 1]];
		const Rectangle button = rects[LeftColumn[row]];
		Require(button.position.x == previous.position.x && button.size.width == previous.size.width, "CHAR QUESTS MAP MENU form one column");
		Require(previous.position.y + previous.size.height < button.position.y, "left native buttons retain separate ordered rows");
	}
	for (size_t index : LeftColumn)
		Require(rects[index].position.x + rects[index].size.width < rects[0].position.x, "left button column clears the life globe");
	for (size_t row = 0; row < RightColumn.size(); ++row) {
		const Rectangle button = rects[RightColumn[row]];
		Require(button.position.x == rects[RightColumn[0]].position.x && button.position.y == rects[LeftColumn[row]].position.y, "INV SPELLS align with the top two native rows");
		Require(rects[1].position.x + rects[1].size.width < button.position.x, "right button column clears the mana globe");
	}
	Require(rects[7].position.y + rects[7].size.height < rects[3].position.y, "prepared spell sits below SPELLS");
	Require(rects[3].position.x >= rects[6].position.x && rects[3].position.x + rects[3].size.width <= rects[6].position.x + rects[6].size.width, "prepared spell occupies the right column");
	Require(rects[0].position.y == rects[1].position.y && rects[0].size.height == rects[1].size.height, "life and mana globes share their baseline");
	Require(rects[0].position.x + rects[0].size.width < rects[4].position.x && rects[4].position.x + rects[4].size.width < rects[1].position.x, "globes flank the central information area");
}

void CheckWidthContinuity(int narrowWidth, int wideWidth)
{
	const HudRects narrow = DefaultRects({ narrowWidth, 480 });
	const HudRects wide = DefaultRects({ wideWidth, 480 });
	for (size_t i = 0; i < narrow.size(); ++i) {
		Require(narrow[i].position.y == wide[i].position.y, "adjacent widths never switch utility rows");
		Require(narrow[i].size.width == wide[i].size.width && narrow[i].size.height == wide[i].size.height, "480-high canvas keeps control sizes across adjacent widths");
		const int shift = wide[i].position.x - narrow[i].position.x;
		Require(shift >= 0 && shift <= 1, "adjacent widths only move the centered composition by at most one pixel");
		Require(narrow[i].position.x - narrow[2].position.x == wide[i].position.x - wide[2].position.x, "adjacent widths preserve the complete composition");
	}
}
}

int main()
{
	Require(D3dHudLayoutEntries.size() == 13, "HUD retains thirteen editable elements");
	for (const auto &entry : D3dHudLayoutEntries)
		Require(!entry.rightAnchor, "all controls use the same bottom-center anchor");
	Require(D3dHudLayoutEntries[0].width == 88 && D3dHudLayoutEntries[0].height == 113 && D3dHudLayoutEntries[1].width == 88 && D3dHudLayoutEntries[1].height == 113, "complete native globe sculptures occupy 88x113 logical pixels");
	Require(D3dHudLayoutEntries[2].width == 232 && D3dHudLayoutEntries[2].height == 29, "eight-slot belt retains its native logical size");
	Require(D3dHudLayoutEntries[3].width == 44 && D3dHudLayoutEntries[3].height == 44, "prepared spell uses its dedicated 44x44 area");
	Require(D3dHudLayoutEntries[4].width == 264 && D3dHudLayoutEntries[4].height == 64, "information area fits the center of the frame");
	for (size_t index : NativeButtons)
		Require(D3dHudLayoutEntries[index].width == 71 && D3dHudLayoutEntries[index].height == 20, "six native text buttons retain 71x20 logical pixels");
	for (size_t i = 11; i < D3dHudLayoutEntries.size(); ++i)
		Require(D3dHudLayoutEntries[i].width == 33 && D3dHudLayoutEntries[i].height == 24, "multiplayer controls retain separate 33x24 logical boxes");
	constexpr std::array<Size, 12> Screens {{ { 640, 480 }, { 800, 600 }, { 1280, 720 }, { 1920, 1080 }, { 2560, 1080 }, { 3440, 1440 }, { 1280, 1024 }, { 640, 360 }, { 843, 480 }, { 844, 480 }, { 853, 480 }, { 854, 480 } }};
	for (Size screen : Screens) {
		const HudRects rects = DefaultRects(screen);
		for (size_t i = 0; i < rects.size(); ++i) {
			const auto &rect = rects[i];
			Require(rect.position.x >= 0 && rect.position.y >= 0 && rect.position.x + rect.size.width <= screen.width && rect.position.y + rect.size.height <= screen.height, "all controls on screen");
			Require(rect.contains(rect.Center()), "rendered center is hittable");
			Require(!rect.contains(rect.position + Displacement { rect.size.width, 0 }), "right boundary exclusive");
			Require(!rect.contains(rect.position + Displacement { 0, rect.size.height }), "bottom boundary exclusive");
		}
		for (size_t i = 0; i < rects.size(); ++i)
			for (size_t j = i + 1; j < rects.size(); ++j)
				Require(!Intersects(rects[i], rects[j]), "all thirteen default HUD areas remain separate");
		int next = rects[2].position.x;
		for (int i = 0; i < 8; ++i) {
			const Rectangle slot = D3dHudBeltSlotRect(rects[2], i);
			Require(slot.position.x == next, "belt has no gaps or duplicate boundary");
			Require(slot.contains(slot.Center()), "all eight belt slots hittable");
			next += slot.size.width;
		}
		Require(next == rects[2].position.x + rects[2].size.width, "belt subdivides full width");
		CheckComposition(rects, screen);
		std::cout << screen.width << 'x' << screen.height << ": belt=" << rects[2].position.x << ',' << rects[2].position.y << ',' << rects[2].size.width << ',' << rects[2].size.height << '\n';
	}
	CheckWidthContinuity(843, 844);
	CheckWidthContinuity(853, 854);
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
