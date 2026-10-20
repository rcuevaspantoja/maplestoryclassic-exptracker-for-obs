#pragma once

#include <array>
#include <string_view>

namespace expmeter::glyphs {

// All bitmaps are rows of '#' (ink), '+' (half ink) and '.' (background), taken
// from native-resolution MapleStory Classic captures.

constexpr int kExpGlyphHeight = 7;

struct ExpGlyph {
	char ch;
	int left_bearing;  // blank columns the game draws before the glyph
	int right_bearing; // blank columns after it (on top of the 1px letter gap)
	std::array<std::string_view, kExpGlyphHeight> rows;
};

// Small HUD font used by "EXP. 74,660[28.00%]" (and HP/MP). The 9 never appeared at
// native scale in the samples; it is the 6 rotated 180 degrees, which holds for every
// other symmetric pair in this font.
constexpr std::array<ExpGlyph, 15> kExpGlyphs = {{
	{'0', 0, 0, {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
	{'1', 1, 2, {".#", "##", ".#", ".#", ".#", ".#", ".#"}},
	{'2', 0, 0, {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
	{'3', 0, 0, {".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."}},
	{'4', 0, 0, {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
	{'5', 0, 0, {"#####", "#....", "#....", ".###.", "....#", "#...#", ".###."}},
	{'6', 0, 0, {".###.", "#...#", "#....", "####.", "#...#", "#...#", ".###."}},
	{'7', 0, 0, {"#####", "....#", "....#", "....#", "...#.", "..#..", "..#.."}},
	{'8', 0, 0, {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
	{'9', 0, 0, {".###.", "#...#", "#...#", ".####", "....#", "#...#", ".###."}},
	{',', 1, 1, {"..", "..", "..", "..", "..", ".#", "#."}},
	{'.', 1, 1, {".", ".", ".", ".", ".", ".", "#"}},
	{'[', 2, 1, {"##", "#.", "#.", "#.", "#.", "#.", "##"}},
	{']', 1, 0, {"##", ".#", ".#", ".#", ".#", ".#", "##"}},
	{'%', 0, 0, {".#....#", "#.#..#.", ".#..#..", "...#...", "..#..#.", ".#..#.#", "#....#."}},
}};

// The "EXP." label left of the number, including its 1px dark outline.
constexpr int kLabelWidth = 25;
constexpr int kLabelHeight = 11;
constexpr std::array<std::string_view, kLabelHeight> kLabel = {
	".+++++++++..+++.+++++....", "+######+##+.###.######...", "+##++++.###+##..##++###..",
	"+##.....+#####..##..###..", "+#####...####...##++###..", "+#####...+###...######...",
	"+##......####+..###++....", "+##.....######..##.......", "+######+###.###.##..##...",
	"+#########..###.##..##...", ".......+.................",
};

// Layout relative to the label template's top-left corner (native pixels).
constexpr int kTextOffsetX = 24;    // first column scanned for the EXP number
constexpr int kTextOffsetY = 3;     // top row of the 7px glyphs
constexpr int kTextScanWidth = 140; // fits "2,147,483,647[100.00%]"

// The level is drawn as one white digit per orange box, centered under "LV.".
constexpr int kLevelScanOffsetX = -406;
constexpr int kLevelScanWidth = 46;
constexpr int kLevelBoxOffsetY = 7; // top row of the orange boxes
constexpr int kLevelBoxHeight = 13;
constexpr int kLevelBoxWidth = 11;
constexpr int kLevelGlyphInsetX = 2;
constexpr int kLevelGlyphInsetY = 3;
constexpr int kLevelGlyphSize = 7;

struct LevelGlyph {
	char ch;
	std::array<std::string_view, kLevelGlyphSize> rows;
};

// Bold digits inside the level boxes. Only the digits seen in native captures so far;
// a box with any other digit makes the level "unknown" rather than guessing.
constexpr std::array<LevelGlyph, 3> kLevelGlyphs = {{
	{'1', {"...##..", "..###..", "...##..", "...##..", "...##..", "...##..", "...##.."}},
	{'3', {".#####.", "##...##", ".....##", "..####.", ".....##", "##...##", ".#####."}},
	{'9', {".#####.", "##...##", "##...##", ".######", ".....##", "##...##", ".#####."}},
}};

inline float cellValue(char c)
{
	return c == '#' ? 1.0f : (c == '+' ? 0.5f : 0.0f);
}

} // namespace expmeter::glyphs
