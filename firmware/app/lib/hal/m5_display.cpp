#include "m5_display.h"

#include <cstring>

#include "text_fit.h"

namespace cpa {
namespace {

constexpr uint8_t kBrightOn = 100;
constexpr uint8_t kBrightDim = 12;  // docs/11 §4.2: ● REC readable, G-A7 measures the cost
constexpr char kBox[] = "\xE2\x96\xA1";  // □
constexpr size_t kRunMax = 96;

bool has_glyph(uint32_t cp) {
  if (cp < 0x20 || cp > 0xFFFF) return false;
  lgfx::FontMetrics m{};
  return fonts::efontCN_12.updateFontMetric(&m, static_cast<uint16_t>(cp));
}

}  // namespace

bool PaletteSprite::begin() {
  sprite_.setColorDepth(lgfx::palette_8bit);
  if (!sprite_.createSprite(kScreenW, kScreenH)) return false;
  // One byte per pixel, rows kScreenW apart: what theme::Raster writes.
  if (sprite_.bufferLength() != static_cast<uint32_t>(kScreenW) * kScreenH) return false;
  if (!sprite_.createPalette(theme::palette888(), theme::kPaletteSize)) return false;
  sprite_.setFont(&fonts::efontCN_12);
  sprite_.setTextWrap(false);
  return true;
}

theme::Raster PaletteSprite::raster() const {
  return theme::Raster{static_cast<uint8_t*>(sprite_.getBuffer()), kScreenW, kScreenH};
}

// On a palette sprite M5GFX keeps text colours as they are and draws value & 0xFF: the
// entries themselves. fg == bg draws the glyphs only (no cell background).
void PaletteSprite::text_color(theme::PalIndex fg, theme::PalIndex bg) const {
  sprite_.setTextColor(static_cast<uint32_t>(fg.value()), static_cast<uint32_t>(bg.value()));
}

// The panel's clip rectangle cuts the push to r: LGFX's pushImage sends only that part.
void PaletteSprite::push(const Rect& r) const {
  M5Cardputer.Display.setClipRect(r.x, r.y, r.w, r.h);
  sprite_.pushSprite(0, 0);
  M5Cardputer.Display.clearClipRect();
}

// The colours come from the theme's own entries; pushSprite looks them up when it pushes.
void PaletteSprite::breathe(uint8_t step) const {
  const uint8_t entries[2] = {theme::index_of(Color::RecDot).value(), theme::rec_dot_edge()};
  const uint32_t colours[2] = {theme::rgb888_of(theme::breath_dot(step)), theme::rgb888_of(theme::breath_edge(step))};
  for (int i = 0; i < 2; ++i) {
    const uint32_t c = colours[i];
    sprite_.setPaletteColor(entries[i], static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8),
                            static_cast<uint8_t>(c));
  }
}

bool M5Display::begin() {
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(kBrightOn);
  if (!sprite_.begin()) return false;
  fill(Rect{0, 0, kScreenW, kScreenH}, Color::Bg);
  return true;
}

void M5Display::present() { sprite_.push(); }

void M5Display::present(const Rect& r) { sprite_.push(r); }

void M5Display::breathe(uint8_t step) { sprite_.breathe(step); }

void M5Display::set_backlight(bool on) { M5Cardputer.Display.setBrightness(on ? kBrightOn : 0); }

void M5Display::set_backlight_level(Backlight lv) {
  M5Cardputer.Display.setBrightness(lv == Backlight::On ? kBrightOn : lv == Backlight::Dim ? kBrightDim : 0);
}

void M5Display::text(int16_t x, int16_t y, const char* utf8, const Style& s) {
  sprite_.text_color(theme::index_of(s.fg), theme::index_of(s.bg));
  walk(x, y, utf8, true);
}

int16_t M5Display::textWidth(const char* utf8) const { return walk(0, 0, utf8, false); }

// Splits utf8 into runs the font can draw and missing glyphs (drawn as □); returns the
// width. Runs are drawn with drawString so M5GFX does its own UTF-8 decoding.
int16_t M5Display::walk(int16_t x, int16_t y, const char* utf8, bool draw) const {
  const size_t n = std::strlen(utf8);
  char run[kRunMax + 1];
  size_t rn = 0;
  int32_t cx = x;
  auto flush = [&]() {
    if (rn == 0) return;
    run[rn] = '\0';
    if (draw) sprite_.draw_string(run, cx, y);
    cx += sprite_.text_width(run);
    rn = 0;
  };
  for (size_t i = 0; i < n;) {
    const Utf8Char ch = utf8_at(utf8, n, i);
    if (!has_glyph(ch.cp)) {
      flush();
      if (draw) sprite_.draw_string(kBox, cx, y);
      cx += sprite_.text_width(kBox);
      missing_ += draw ? 1 : 0;
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

}  // namespace cpa
