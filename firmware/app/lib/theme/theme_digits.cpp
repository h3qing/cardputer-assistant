#include "theme_digits.h"

#include "gen/digits_gen.h"
#include "theme_palette.h"

namespace cpa {
namespace theme {

GlyphView digit_glyph(DigitFont f, char ch) {
  const bool clock = f == DigitFont::Clock;
  const int16_t h = digits_height(f);
  if (ch >= '0' && ch <= '9') {
    const int d = ch - '0';
    if (clock) return GlyphView{gen::kClockDigits[d], gen::kClockCell, h};
    return GlyphView{gen::kTimerDigits[d], gen::kTimerCell, h};
  }
  if (ch != ':') return GlyphView{};
  if (clock) return GlyphView{gen::kClockColonPx, gen::kClockColon, h};
  return GlyphView{gen::kTimerColonPx, gen::kTimerColon, h};
}

int16_t digits_width(const char* chars, DigitFont f) {
  int16_t w = 0;
  for (const char* p = chars; *p != '\0'; ++p) w = static_cast<int16_t>(w + digit_glyph(f, *p).w);
  return w;
}

int16_t digits_height(DigitFont f) { return f == DigitFont::Clock ? gen::kClockH : gen::kTimerH; }

DigitInk digit_ink(Color ink, Color under) {
  DigitInk d;
  const uint8_t full = index_of(ink).value();
  const uint8_t ramp = digit_ramp(ink, under);
  for (uint8_t k = 1; k < 5; ++k) {
    if (ramp != 0) {
      d.entry[k] = static_cast<uint8_t>(ramp + k - 1);
    } else {
      d.entry[k] = k >= 3 ? full : 0;
    }
  }
  d.entry[5] = full;
  return d;
}

}  // namespace theme
}  // namespace cpa
