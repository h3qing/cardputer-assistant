#include "theme_palette.h"

#include <cmath>

#include "gen/palette_gen.h"

namespace cpa {
namespace theme {
namespace {

static_assert(gen::kTokenCount == kColorCount, "tools/theme/tokens.py and cpa::Color disagree");
static_assert(gen::kBreathSteps == kBreathSteps, "tools/theme/tokens.py BREATH has four steps");

constexpr uint8_t kNoToken = 0x00;  // a sentinel: magenta

bool is_token(Color c) { return static_cast<uint8_t>(c) < kColorCount; }

// The table row of c; a value past the tokens reads Bg's row in the edge tables (index_of
// gives it magenta, so it never draws as Bg).
uint8_t token(Color c) { return is_token(c) ? static_cast<uint8_t>(c) : 0; }

double channel(unsigned v8) {
  const double v = v8 / 255.0;
  return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

// WCAG relative luminance of what the panel shows (RGB565 widened to 8 bits).
double luminance(uint8_t entry) {
  const uint16_t c = rgb565_of(entry);
  const unsigned r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
  return 0.2126 * channel((r5 << 3) | (r5 >> 2)) + 0.7152 * channel((g6 << 2) | (g6 >> 4)) +
         0.0722 * channel((b5 << 3) | (b5 >> 2));
}

}  // namespace

PalIndex index_of(Color c) { return PalIndex(is_token(c) ? gen::kTokenIndex[token(c)] : kNoToken); }

uint8_t aa_of(Color paint, Color under) { return gen::kAa[token(paint)][token(under)]; }

bool aa_exact(Color paint, Color under) { return ((gen::kAaExact[token(paint)] >> token(under)) & 1u) != 0; }

uint8_t dim_of(uint8_t entry) { return gen::kDim[entry]; }

const uint8_t* dim_table() { return gen::kDim; }

bool is_sentinel(uint8_t entry) { return entry == 0x00 || entry == 0x10 || entry == 0xE0 || entry == 0xFF; }

uint32_t rgb888_of(uint8_t entry) { return gen::kPalette888[entry]; }

uint16_t rgb565_of(uint8_t entry) {
  const uint32_t c = gen::kPalette888[entry];
  return static_cast<uint16_t>(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

const uint32_t* palette888() { return gen::kPalette888; }

EntryMap mando_map() {
  EntryMap m;
  for (uint8_t n = 1; n < 16; ++n) m.entry[n] = static_cast<uint8_t>(0x1F + n);
  return m;
}

uint8_t digit_ramp(Color ink, Color under) {
  if (ink != Color::Fg) return 0;
  if (under == Color::Bg) return gen::kDigitRampBg;
  return under == Color::Sheet ? gen::kDigitRampSheet : 0;
}

GlowSpec glow_spec() {
  GlowSpec g;
  g.cx = gen::kGlowCx;
  g.cy = gen::kGlowCy;
  g.q = gen::kGlowQ;
  g.base = gen::kGlowBase;
  g.steps = gen::kGlowSteps;
  return g;
}

uint8_t breath_dot(uint8_t step) { return gen::kBreathDot[step % kBreathSteps]; }

uint8_t breath_edge(uint8_t step) { return gen::kBreathEdge[step % kBreathSteps]; }

uint8_t rec_dot_edge() { return gen::kRecDotEdge; }

double contrast(Color a, Color b) { return contrast_of(index_of(a).value(), index_of(b).value()); }

double contrast_of(uint8_t a, uint8_t b) {
  const double la = luminance(a);
  const double lb = luminance(b);
  return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

}  // namespace theme
}  // namespace cpa
