// The oracle's device side: M5GFX's own 8-bpp LGFX_Sprite (its SDL build, sprite only),
// set up as M5Display::begin() sets up the device's sprite and driven by a copy of
// M5Display::walk (hal/m5_display.cpp, device only), verbatim apart from the sprite type.
// v0.5 (docs/19 §3.1): palette_8bit with the theme's palette; colours are palette entries,
// given to setTextColor and fillRect as they are (M5GFX draws value & 0xFF). The fill
// cases use M5GFX's fillRect, the simulator's theme::RasterCanvas, so they also check that
// the sprite buffer is laid out as theme::Raster writes it.
#include <cstdint>
#include <cstring>

#include "lgfx/v1/LGFXBase.hpp"
#include "lgfx/v1/LGFX_Sprite.hpp"
#include "lgfx/v1/lgfx_fonts.hpp"
#include "lgfx/v1/platforms/common.hpp"
#include "lgfx/v1/platforms/device.hpp"
#include "oracle_cases.h"
#include "text_fit.h"
#include "theme_palette.h"

namespace {

using cpa::Utf8Char;
using cpa::utf8_at;

constexpr char kBox[] = "\xE2\x96\xA1";  // □
constexpr size_t kRunMax = 96;

// The palette entry of a cpa::Color value, as hal/m5_display.cpp passes it.
uint32_t entry(int c) { return cpa::theme::index_of(static_cast<cpa::Color>(c)).value(); }

bool has_glyph(uint32_t cp) {
  if (cp < 0x20 || cp > 0xFFFF) return false;
  lgfx::FontMetrics m{};
  return fonts::efontCN_12.updateFontMetric(&m, static_cast<uint16_t>(cp));
}

int16_t walk(lgfx::LGFX_Sprite& sprite_, int16_t x, int16_t y, const char* utf8, bool draw) {
  const size_t n = std::strlen(utf8);
  char run[kRunMax + 1];
  size_t rn = 0;
  int32_t cx = x;
  auto flush = [&]() {
    if (rn == 0) return;
    run[rn] = '\0';
    if (draw) sprite_.drawString(run, cx, y);
    cx += sprite_.textWidth(run);
    rn = 0;
  };
  for (size_t i = 0; i < n;) {
    const Utf8Char ch = utf8_at(utf8, n, i);
    if (!has_glyph(ch.cp)) {
      flush();
      if (draw) sprite_.drawString(kBox, cx, y);
      cx += sprite_.textWidth(kBox);
    } else {
      if (rn + ch.len > kRunMax) flush();
      std::memcpy(run + rn, utf8 + i, ch.len);
      rn += ch.len;
    }
    i += ch.len;
  }
  flush();
  return static_cast<int16_t>(cx - x);
}

// One sprite for every case unless fresh, as the device keeps one (decoder state persists).
lgfx::LGFX_Sprite& sprite(bool fresh) {
  static lgfx::LGFX_Sprite* sp = nullptr;
  if (!sp || fresh) {
    delete sp;
    sp = new lgfx::LGFX_Sprite();
    sp->setColorDepth(lgfx::palette_8bit);
    sp->createSprite(240, 135);
    sp->createPalette(cpa::theme::palette888(), cpa::theme::kPaletteSize);
    sp->setFont(&fonts::efontCN_12);
    sp->setTextWrap(false);
  }
  return *sp;
}

}  // namespace

// Case i on a black sprite; out gets the 240 x 135 bytes. Returns textWidth (0 for fills).
int m5gfx_render(int i, uint8_t* out, bool fresh) {
  lgfx::LGFX_Sprite& sp = sprite(fresh);
  sp.fillScreen(entry(0));
  const oracle::Case& c = oracle::kCases[i];
  int w = 0;
  if (c.fill) {
    sp.fillRect(c.x, c.y, c.w, c.h, entry(c.fg));
  } else {
    sp.setTextColor(entry(c.fg), entry(c.bg));
    walk(sp, static_cast<int16_t>(c.x), static_cast<int16_t>(c.y), c.text, true);
    w = walk(sp, 0, 0, c.text, false);
  }
  std::memcpy(out, sp.getBuffer(), 240 * 135);
  return w;
}

// kGlyphs[i] white on black at (0, 0); out gets the 240 x 135 bytes. Returns its width.
int m5gfx_glyph(int i, uint8_t* out) {
  lgfx::LGFX_Sprite& sp = sprite(true);
  sp.fillScreen(entry(0));
  sp.setTextColor(entry(1), entry(0));
  walk(sp, 0, 0, oracle::kGlyphs[i], true);
  std::memcpy(out, sp.getBuffer(), 240 * 135);
  return walk(sp, 0, 0, oracle::kGlyphs[i], false);
}
