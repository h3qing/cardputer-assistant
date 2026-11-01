// Frame (gh #32): the device's frame buffer on the host. hal/m5_display.cpp draws into a
// 240 x 135 palette_8bit M5Canvas (docs/19 §3.1), so every pixel is an entry of the theme's
// 256-colour palette (lib/theme), and pushSprite() sends that entry's colour to the panel
// as RGB565. panel_rgb gives what the panel shows for an entry.
// Like the device's sprite, a Frame is a mutable buffer: drawing changes it in place.
#pragma once

#include <cstdint>
#include <vector>

#include "canvas.h"

namespace cpasim {

struct Rgb {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

uint8_t entry_of(cpa::Color c);  // the palette entry a token is drawn as
Rgb panel_rgb(uint8_t entry);     // what the panel shows, 8 bits per channel

class Frame {
 public:
  Frame(int w, int h, uint8_t px = 0);

  int w() const { return w_; }
  int h() const { return h_; }
  uint8_t at(int x, int y) const;  // 0 outside the frame
  const std::vector<uint8_t>& pixels() const { return px_; }
  uint8_t* data() { return px_.data(); }  // w * h entries, row-major (theme::Raster)

  // LGFX fillRect: a negative w / h extends left / up, zero draws nothing, clipped.
  void fill_rect(int x, int y, int w, int h, uint8_t px);
  void blit(const Frame& src, int x, int y);  // src copied with its top left at (x, y)

 private:
  int w_;
  int h_;
  std::vector<uint8_t> px_;
};

Frame scaled(const Frame& f, int k);  // every pixel repeated k x k times

}  // namespace cpasim
