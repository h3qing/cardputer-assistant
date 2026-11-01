// The device's 256-colour palette (docs/19 §2.2-§2.3): the canvas is an M5GFX palette_8bit
// sprite whose bytes are entries of this palette. Tokens sit at 0x01-0x0F and 0x11-0x19; the
// four sentinels 0x00, 0x10, 0xE0 and 0xFF, where the low bytes of v0.4.0's RGB565 constants
// land, are magenta, like every entry nobody uses. The tables come from tools/theme
// (gen/palette_gen.h); this is the only way into them. Pure: no Arduino, M5 or esp headers.
#pragma once

#include <cstdint>

#include "canvas.h"
#include "theme_raster.h"

namespace cpa {
namespace theme {

constexpr uint16_t kPaletteSize = 256;

// A palette entry made from a token. The device's sprite wrapper only takes these, so no
// integer reaches M5GFX as a colour.
class PalIndex {
 public:
  constexpr uint8_t value() const { return v_; }
  constexpr bool operator==(const PalIndex& o) const { return v_ == o.v_; }
  constexpr bool operator!=(const PalIndex& o) const { return v_ != o.v_; }

 private:
  explicit constexpr PalIndex(uint8_t v) : v_(v) {}
  uint8_t v_;
  friend PalIndex index_of(Color c);
};

PalIndex index_of(Color c);  // a value past the tokens: the sentinel 0x00 (magenta)

// The entry of one baked edge step between paint and what is under it. Every pair has one
// (the nearest colour when the pair has none of its own); aa_exact says which have their own.
uint8_t aa_of(Color paint, Color under);
bool aa_exact(Color paint, Color under);

uint8_t dim_of(uint8_t entry);  // the entry that shows `entry` dimmed
const uint8_t* dim_table();     // dim_of for every entry (kPaletteSize, in flash)
bool is_sentinel(uint8_t entry);

uint32_t rgb888_of(uint8_t entry);
uint16_t rgb565_of(uint8_t entry);  // what the panel shows: pushSprite truncates to RGB565
const uint32_t* palette888();       // kPaletteSize entries, for createPalette

// Mando's 4-bit value n (1-15) -> his colour entry 0x1F + n; 0 is clear.
EntryMap mando_map();

// The first of four consecutive entries for levels 1-4 of ink on under (the big digits);
// 0 for a pair with no baked ramp. Fg on Bg and Fg on Sheet have one.
uint8_t digit_ramp(Color ink, Color under);

// The lock screen's corner glow: centre, thresholds and entries (gen/palette_gen.h).
GlowSpec glow_spec();

// The big ● breathing (docs/19 §2.3): kBreathSteps steps, step 0 full Rec, the darkest still
// >= 4.5:1 on Bg. At step s the RecDot entry shows rgb888_of(breath_dot(s)) and its rim, the
// entry rec_dot_edge(), rgb888_of(breath_edge(s)): the device copies those colours into the
// two entries (Display::breathe); the simulator's panel draws these entries as pushed. Nothing
// but the big ● uses RecDot or its rim, so only it breathes. The step wraps.
constexpr uint8_t kBreathSteps = 4;
uint8_t breath_dot(uint8_t step);
uint8_t breath_edge(uint8_t step);
uint8_t rec_dot_edge();

// WCAG contrast of two tokens as the panel shows them (RGB565): text that is read needs
// >= 4.5, body text >= 7 (docs/19 §2.5).
double contrast(Color a, Color b);
double contrast_of(uint8_t a, uint8_t b);  // of two entries

}  // namespace theme
}  // namespace cpa
