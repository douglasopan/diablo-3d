#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace devilution::town_prop_stencils {

enum class Kind : uint8_t { Rock, Grave, Wall };

struct Cell {
	int8_t x;
	int8_t y;
	uint16_t piece;
	bool replacesScenery = true;
	bool upright = true; // Native SOL witness: broken paint must not close a passage.
};

struct Layout {
	Kind kind;
	uint16_t nativeLayout;
	std::span<const Cell> cells;
	int8_t footX;
	int8_t footY;
	int16_t requiredAnchorX = -1;
	int16_t requiredAnchorY = -1;
};

namespace detail {

// Rock nativeLayout values identify the first MIN piece, preserving the
// existing whole-object layouts and foot offsets without joining neighbors.
inline constexpr Cell PairedRock[] = { { 0, 0, 219 }, { 1, 0, 220 } };
inline constexpr Cell SmallRockA[] = { { 0, 0, 221 } };
inline constexpr Cell SmallRockB[] = { { 0, 0, 222 } };
inline constexpr Cell AngledRock[] = { { 0, 0, 223 }, { 0, 1, 224 }, { 1, 1, 225 } };
inline constexpr Cell WideRock[] = { { 0, 0, 226 }, { 1, 0, 227 }, { 0, 1, 228 }, { 1, 1, 229 } };
inline constexpr Cell LargeRock[] = { { 0, 0, 230 }, { 1, 0, 231 }, { 0, 1, 232 }, { 1, 1, 233 } };

// Exact complete TIL layouts from the native town.til/reference census.
// TIL265 is the separately inspected floor arrangement and is not admitted.
inline constexpr Cell Grave250[] = { { 0, 0, 961 }, { 1, 0, 962 }, { 0, 1, 963 }, { 1, 1, 964 } };
inline constexpr Cell Grave251[] = { { 0, 0, 965 }, { 1, 0, 966 }, { 0, 1, 967 }, { 1, 1, 968 } };
inline constexpr Cell Grave252[] = { { 0, 0, 969 }, { 1, 0, 970 }, { 0, 1, 971 }, { 1, 1, 972 } };
inline constexpr Cell Grave253[] = { { 0, 0, 973 }, { 1, 0, 974 }, { 0, 1, 975 }, { 1, 1, 976 } };
inline constexpr Cell Grave254[] = { { 0, 0, 977 }, { 1, 0, 978 }, { 0, 1, 979 }, { 1, 1, 980 } };
inline constexpr Cell Grave255[] = { { 0, 0, 981 }, { 1, 0, 982 }, { 0, 1, 983 }, { 1, 1, 984 } };
inline constexpr Cell Grave256[] = { { 0, 0, 985 }, { 1, 0, 986 }, { 0, 1, 987 }, { 1, 1, 988 } };
inline constexpr Cell Grave257[] = { { 0, 0, 989 }, { 1, 0, 990 }, { 0, 1, 991 }, { 1, 1, 992 } };
inline constexpr Cell Grave258[] = { { 0, 0, 993 }, { 1, 0, 994 }, { 0, 1, 995 }, { 1, 1, 996 } };
inline constexpr Cell Grave259[] = { { 0, 0, 997 }, { 1, 0, 998 }, { 0, 1, 999 }, { 1, 1, 1000 } };
inline constexpr Cell Grave260[] = { { 0, 0, 1001 }, { 1, 0, 1002 }, { 0, 1, 1003 }, { 1, 1, 1004 } };
inline constexpr Cell Grave261[] = { { 0, 0, 1005 }, { 1, 0, 1006 }, { 0, 1, 1007 }, { 1, 1, 1008 } };
inline constexpr Cell Grave262[] = { { 0, 0, 1009 }, { 1, 0, 1010 }, { 0, 1, 1011 }, { 1, 1, 1012 } };
inline constexpr Cell Grave263[] = { { 0, 0, 1013 }, { 1, 0, 1014 }, { 0, 1, 1015 }, { 1, 1, 1016 } };
inline constexpr Cell Grave264[] = { { 0, 0, 1017 }, { 1, 0, 1018 }, { 0, 1, 1019 }, { 1, 1, 1020 } };
inline constexpr Cell Grave266[] = { { 0, 0, 1025 }, { 1, 0, 1026 }, { 0, 1, 1027 }, { 1, 1, 1028 } };
inline constexpr Cell Grave267[] = { { 0, 0, 1029 }, { 1, 0, 1030 }, { 0, 1, 1031 }, { 1, 1, 1032 } };
inline constexpr Cell Grave268[] = { { 0, 0, 1033 }, { 1, 0, 1034 }, { 0, 1, 1035 }, { 1, 1, 1036 } };
inline constexpr Cell Grave269[] = { { 0, 0, 1037 }, { 1, 0, 1038 }, { 0, 1, 1039 }, { 1, 1, 1040 } };
inline constexpr Cell Grave270[] = { { 0, 0, 1041 }, { 1, 0, 1042 }, { 0, 1, 1043 }, { 1, 1, 1044 } };
inline constexpr Cell Grave271[] = { { 0, 0, 1045 }, { 1, 0, 1046 }, { 0, 1, 1047 }, { 1, 1, 1048 } };

// The reviewed church-side wall perimeter in references 005141/005152. Floor cells are
// witnesses of the complete stencil, not scenery to replace. Other uses of
// these same TILs outside the verified church context remain excluded.

inline constexpr Cell Wall272[] = { { 0, 0, 1049, true, true }, { 1, 0, 1050, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall273[] = { { 0, 0, 1051, true, true }, { 1, 0, 1052, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall274[] = { { 0, 0, 1053, true, true }, { 1, 0, 1054, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall275[] = { { 0, 0, 1055, true, true }, { 1, 0, 1056, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall276[] = { { 0, 0, 1057, true, true }, { 1, 0, 1058, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall277[] = { { 0, 0, 1059, true, true }, { 1, 0, 1060, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall278[] = { { 0, 0, 1061, true, true }, { 1, 0, 1062, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall280[] = { { 0, 0, 1064, true, true }, { 1, 0, 1065, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall281[] = { { 0, 0, 1066, true, true }, { 1, 0, 1067, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall282[] = { { 0, 0, 1068, true, true }, { 1, 0, 1069, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall283[] = { { 0, 0, 1070, true, true }, { 1, 0, 9, false, false }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall284[] = { { 0, 0, 1071, true, true }, { 1, 0, 9, false, false }, { 0, 1, 1072, true, true }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall285[] = { { 0, 0, 1073, true, true }, { 1, 0, 1074, true, true }, { 0, 1, 1075, true, true }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall286[] = { { 0, 0, 1076, true, true }, { 1, 0, 1077, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall287[] = { { 0, 0, 1078, true, true }, { 1, 0, 1079, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall288[] = { { 0, 0, 1080, true, true }, { 1, 0, 9, false, false }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall289[] = { { 0, 0, 1081, true, true }, { 1, 0, 9, false, false }, { 0, 1, 1082, true, true }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall290[] = { { 0, 0, 1083, true, true }, { 1, 0, 9, false, false }, { 0, 1, 1084, true, true }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall291[] = { { 0, 0, 1085, true, true }, { 1, 0, 9, false, false }, { 0, 1, 1086, true, true }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall292[] = { { 0, 0, 1087, true, true }, { 1, 0, 9, false, false }, { 0, 1, 1088, true, true }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall293[] = { { 0, 0, 1089, true, true }, { 1, 0, 9, false, false }, { 0, 1, 1090, true, true }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall294[] = { { 0, 0, 1091, true, true }, { 1, 0, 9, false, false }, { 0, 1, 1092, true, true }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall295[] = { { 0, 0, 1093, true, false }, { 1, 0, 9, false, false }, { 0, 1, 1094, true, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall296[] = { { 0, 0, 1095, true, true }, { 1, 0, 9, false, false }, { 0, 1, 1096, true, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall297[] = { { 0, 0, 1097, true, true }, { 1, 0, 9, false, false }, { 0, 1, 1098, true, true }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall298[] = { { 0, 0, 1099, true, true }, { 1, 0, 1100, true, true }, { 0, 1, 10, false, false }, { 1, 1, 1101, true, false } };
inline constexpr Cell Wall299[] = { { 0, 0, 1102, true, true }, { 1, 0, 1103, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall300[] = { { 0, 0, 1104, true, true }, { 1, 0, 1105, true, true }, { 0, 1, 10, false, false }, { 1, 1, 11, false, false } };
inline constexpr Cell Wall301[] = { { 0, 0, 1106, true, true }, { 1, 0, 1107, true, false }, { 0, 1, 1108, true, true }, { 1, 1, 1109, true, false } };
inline constexpr Cell Wall302[] = { { 0, 0, 1110, true, true }, { 1, 0, 1111, true, false }, { 0, 1, 1112, true, true }, { 1, 1, 1113, true, false } };

inline constexpr std::array<Layout, 61> Layouts = { {
	{ Kind::Rock, 219, PairedRock, 1, 0 },
	{ Kind::Rock, 221, SmallRockA, 0, 0 },
	{ Kind::Rock, 222, SmallRockB, 0, 0 },
	{ Kind::Rock, 223, AngledRock, 1, 1 },
	{ Kind::Rock, 226, WideRock, 1, 1 },
	{ Kind::Rock, 230, LargeRock, 1, 1 },
	{ Kind::Grave, 250, Grave250, 1, 1 },
	{ Kind::Grave, 251, Grave251, 1, 1 },
	{ Kind::Grave, 252, Grave252, 1, 1 },
	{ Kind::Grave, 253, Grave253, 1, 1 },
	{ Kind::Grave, 254, Grave254, 1, 1 },
	{ Kind::Grave, 255, Grave255, 1, 1 },
	{ Kind::Grave, 256, Grave256, 1, 1 },
	{ Kind::Grave, 257, Grave257, 1, 1 },
	{ Kind::Grave, 258, Grave258, 1, 1 },
	{ Kind::Grave, 259, Grave259, 1, 1 },
	{ Kind::Grave, 260, Grave260, 1, 1 },
	{ Kind::Grave, 261, Grave261, 1, 1 },
	{ Kind::Grave, 262, Grave262, 1, 1 },
	{ Kind::Grave, 263, Grave263, 1, 1 },
	{ Kind::Grave, 264, Grave264, 1, 1 },
	{ Kind::Grave, 266, Grave266, 1, 1 },
	{ Kind::Grave, 267, Grave267, 1, 1 },
	{ Kind::Grave, 268, Grave268, 1, 1 },
	{ Kind::Grave, 269, Grave269, 1, 1 },
	{ Kind::Grave, 270, Grave270, 1, 1 },
	{ Kind::Grave, 271, Grave271, 1, 1 },
	{ Kind::Wall, 272, Wall272, 1, 0, 10, 22 },
	{ Kind::Wall, 272, Wall272, 1, 0, 12, 38 },
	{ Kind::Wall, 273, Wall273, 1, 0, 12, 22 },
	{ Kind::Wall, 274, Wall274, 1, 0, 14, 22 },
	{ Kind::Wall, 274, Wall274, 1, 0, 14, 38 },
	{ Kind::Wall, 275, Wall275, 1, 0, 16, 22 },
	{ Kind::Wall, 275, Wall275, 1, 0, 16, 38 },
	{ Kind::Wall, 276, Wall276, 1, 0, 18, 38 },
	{ Kind::Wall, 277, Wall277, 1, 0, 20, 38 },
	{ Kind::Wall, 278, Wall278, 1, 0, 22, 38 },
	{ Kind::Wall, 280, Wall280, 1, 0, 28, 38 },
	{ Kind::Wall, 281, Wall281, 1, 0, 30, 38 },
	{ Kind::Wall, 282, Wall282, 1, 0, 32, 38 },
	{ Kind::Wall, 283, Wall283, 0, 0, 34, 38 },
	{ Kind::Wall, 284, Wall284, 0, 1, 34, 36 },
	{ Kind::Wall, 285, Wall285, 1, 1, 34, 34 },
	{ Kind::Wall, 286, Wall286, 1, 0, 36, 34 },
	{ Kind::Wall, 287, Wall287, 1, 0, 38, 34 },
	{ Kind::Wall, 288, Wall288, 0, 0, 40, 34 },
	{ Kind::Wall, 289, Wall289, 0, 1, 40, 32 },
	{ Kind::Wall, 290, Wall290, 0, 1, 40, 30 },
	{ Kind::Wall, 291, Wall291, 0, 1, 40, 28 },
	{ Kind::Wall, 292, Wall292, 0, 1, 40, 26 },
	{ Kind::Wall, 293, Wall293, 0, 1, 40, 24 },
	{ Kind::Wall, 294, Wall294, 0, 1, 26, 12 },
	{ Kind::Wall, 294, Wall294, 0, 1, 40, 22 },
	{ Kind::Wall, 295, Wall295, 0, 1, 40, 20 },
	{ Kind::Wall, 296, Wall296, 0, 0, 40, 18 },
	{ Kind::Wall, 297, Wall297, 0, 1, 40, 16 },
	{ Kind::Wall, 298, Wall298, 1, 0, 38, 16 },
	{ Kind::Wall, 299, Wall299, 1, 0, 36, 16 },
	{ Kind::Wall, 300, Wall300, 1, 0, 34, 16 },
	{ Kind::Wall, 301, Wall301, 0, 1, 34, 14 },
	{ Kind::Wall, 302, Wall302, 0, 1, 34, 12 },
} };

} // namespace detail

inline constexpr std::span<const Layout> GetLayouts()
{
	return detail::Layouts;
}

/** Read-only full-stencil match, including non-replaced floor witnesses.
 * Callers retain ownership of replacement masks and lifecycle state. */
template <typename ReadPiece, typename ReadSpecial>
constexpr bool MatchesLayout(const Layout &layout, int anchorX, int anchorY,
    int width, int height, ReadPiece readPiece, ReadSpecial readSpecial)
{
	if (width <= 0 || height <= 0 || anchorX < 0 || anchorY < 0
	    || anchorX >= width || anchorY >= height || layout.cells.empty()
	    || (layout.requiredAnchorX >= 0 && anchorX != layout.requiredAnchorX)
	    || (layout.requiredAnchorY >= 0 && anchorY != layout.requiredAnchorY))
		return false;
	for (const Cell &cell : layout.cells) {
		const int64_t x = static_cast<int64_t>(anchorX) + cell.x;
		const int64_t y = static_cast<int64_t>(anchorY) + cell.y;
		if (x < 0 || y < 0 || x >= width || y >= height)
			return false;
		if (readPiece(static_cast<int>(x), static_cast<int>(y)) != cell.piece
		    || readSpecial(static_cast<int>(x), static_cast<int>(y)) != 0)
			return false;
	}
	return true;
}

} // namespace devilution::town_prop_stencils
