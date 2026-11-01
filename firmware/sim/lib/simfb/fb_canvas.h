// FbCanvas (gh #32): the Canvas of hal/m5_display.{h,cpp} drawing into a Frame instead
// of an M5Canvas. Same font (efontCN_12), same colours, same text walk: the string is
// split into runs the font can draw and code points it cannot (below 0x20, above
// 0xFFFF or absent), which are drawn as □ and counted; runs are measured with
// text_width and drawn with draw_string, so widths and background cells match the device.
// v0.5 (docs/19 §3.6): everything but text is the device's own theme::RasterCanvas, on the
// Frame's bytes, which are palette entries.
#pragma once

#include <cstdint>
#include <string>

#include "canvas.h"
#include "efont.h"
#include "fb.h"
#include "raster_canvas.h"

namespace cpasim {

class FbCanvas : public cpa::theme::RasterCanvas {
 public:
  explicit FbCanvas(Frame& f, const U8g2Font& font = efont_cn12()) : f_(f), font_(font) {}

  void text(int16_t x, int16_t y, const char* utf8, const cpa::Style& s) override;
  int16_t textWidth(const char* utf8) const override;
  int16_t lineHeight() const override { return 12; }

  uint32_t missing_glyphs() const { return missing_; }
  // docs/21 §9.2's invariants, counted since boot: Han, kana or full-width code points drawn
  // other than those of "语言" and "中文" (the language row and the switch toast name both
  // languages), and text drawn past the frame's left or right edge, or whose ink lights its
  // last column (flush against the bezel, it reads as cut).
  uint32_t han_glyphs() const { return han_; }
  uint32_t offscreen_texts() const { return offscreen_; }
  const std::string& last_offscreen() const { return last_offscreen_; }  // "x,y text", to report it

 protected:
  cpa::theme::Raster surface() const override {
    return cpa::theme::Raster{f_.data(), static_cast<int16_t>(f_.w()), static_cast<int16_t>(f_.h())};
  }

 private:
  int16_t walk(int16_t x, int16_t y, const char* utf8, const Ink* ink, int32_t* ink_end = nullptr) const;
  bool has_glyph(uint32_t cp) const;

  Frame& f_;
  const U8g2Font& font_;
  mutable uint32_t missing_ = 0;
  mutable uint32_t han_ = 0;
  uint32_t offscreen_ = 0;
  std::string last_offscreen_;
};

}  // namespace cpasim
