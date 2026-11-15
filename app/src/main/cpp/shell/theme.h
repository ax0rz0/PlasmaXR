// KDE Breeze Dark palette and the type scale used across the shell. Sizes are pixels at the
// shell's panel density (~800 px per metre, readable at about two metres).
#pragma once

#include "core/math.h"

namespace pxr {
namespace theme {

constexpr Color kWindow = Color::hex(0x31363b);
constexpr Color kView = Color::hex(0x232629);
constexpr Color kViewAlt = Color::hex(0x1b1e20);
constexpr Color kText = Color::hex(0xeff0f1);
constexpr Color kTextDim = Color::hex(0x9ba3ab);
constexpr Color kAccent = Color::hex(0x3daee9);
constexpr Color kPositive = Color::hex(0x27ae60);
constexpr Color kNegative = Color::hex(0xda4453);
constexpr Color kNeutral = Color::hex(0xf67400);
constexpr Color kSeparator = Color(1, 1, 1, 0.10f);
constexpr Color kGlass = Color::hex(0x232629, 0.84f);
constexpr Color kShadow = Color(0, 0, 0, 0.42f);

constexpr float kFontTiny = 24;
constexpr float kFontSmall = 28;
constexpr float kFontBody = 34;
constexpr float kFontLarge = 44;
constexpr float kFontClock = 132;

constexpr float kRadius = 24;      // cards and menus
constexpr float kRadiusSmall = 16; // buttons and tiles

}  // namespace theme
}  // namespace pxr
