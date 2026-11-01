// The big digits (docs/19 §2.3-§2.4): Nunito ExtraBold (SIL OFL 1.1, NOTICE) baked by
// tools/theme into gen/digits_gen.h. Only 0-9 and ':'; the figures are tabular, so a digit
// always takes one cell and a running timer never shifts. Each pixel is a level 0-5 (0
// clear, 5 the ink) drawn through a palette ramp. Pure.
#pragma once

#include <cstdint>

#include "canvas.h"
#include "theme_raster.h"

namespace cpa {
namespace theme {

using GlyphView = Nibbles;  // levels 0-5, cell (colon) wide, digits_height tall
using DigitInk = EntryMap;  // the entry for each level

GlyphView digit_glyph(DigitFont f, char ch);  // w == 0 for anything but 0-9 and ':'
int16_t digits_width(const char* chars, DigitFont f);
int16_t digits_height(DigitFont f);

// Levels 1-4 of ink on under: the baked ramp where there is one (Fg on Bg, Fg on Sheet);
// on any other pair the ink from level 3 up and nothing below, never a wrong colour.
DigitInk digit_ink(Color ink, Color under);

}  // namespace theme
}  // namespace cpa
