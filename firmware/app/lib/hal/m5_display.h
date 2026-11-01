// M5Display (05 §5.12): full-screen 8 bpp M5Canvas (32,400 B) with efontCN_12, pushed
// to the panel only when the shell rendered a frame. Implements the Canvas the shell
// and modules draw on. Glyphs the font lacks are detected with the font's own lookup
// (IFont::updateFontMetric returns false) and drawn as □ (U+25A1, present in the font).
// Device only: a mutable frame buffer inside the hal/ exception boundary.
// v0.5 (docs/19 §3.1-§3.2): the sprite is palette_8bit with the theme's 256 colours (+768 B
// heap). Shapes are theme::RasterCanvas writing the sprite's buffer; only text goes through
// M5GFX, by way of PaletteSprite, which takes palette entries made from tokens and nothing
// else, so no RGB565 constant can reach the sprite.
// v0.5 S3 (docs/19 §3.4): present(Rect) pushes one rectangle (the panel clipped to it), and
// breathe() copies a breath step's colours into the RecDot and RecDotEdge entries.
#pragma once

#include <M5Cardputer.h>

#include "platform.h"
#include "raster_canvas.h"
#include "theme_palette.h"

namespace cpa {

// The M5GFX sprite, private: colours in only as theme::PalIndex.
class PaletteSprite {
 public:
  bool begin();  // palette_8bit 240 x 135, the theme's palette, efontCN_12; false: no memory
  theme::Raster raster() const;
  void text_color(theme::PalIndex fg, theme::PalIndex bg) const;
  void draw_string(const char* s, int32_t x, int32_t y) const { sprite_.drawString(s, x, y); }
  int32_t text_width(const char* s) const { return sprite_.textWidth(s); }
  void push() const { sprite_.pushSprite(0, 0); }
  void push(const Rect& r) const;     // only r reaches the panel
  void breathe(uint8_t step) const;   // theme::breath_dot / breath_edge into the two entries

 private:
  mutable M5Canvas sprite_{&M5Cardputer.Display};
};

class M5Display : public Display, public theme::RasterCanvas {
 public:
  bool begin();  // false when the sprite could not be allocated
  Canvas& canvas() override { return *this; }
  void present() override;
  void present(const Rect& r) override;
  void breathe(uint8_t step) override;
  void set_backlight(bool on);
  void set_backlight_level(Backlight lv);  // v0.3: dim while recording on battery

  void text(int16_t x, int16_t y, const char* utf8, const Style& s) override;
  int16_t textWidth(const char* utf8) const override;
  int16_t lineHeight() const override { return 12; }

  uint32_t missing_glyphs() const { return missing_; }

 protected:
  theme::Raster surface() const override { return sprite_.raster(); }

 private:
  int16_t walk(int16_t x, int16_t y, const char* utf8, bool draw) const;

  PaletteSprite sprite_;
  mutable uint32_t missing_ = 0;
};

}  // namespace cpa
