// Drawing into an 8-bit palette frame buffer (docs/19 §3.1, §3.6): one byte per pixel,
// rows w bytes apart, each byte a palette entry. The device's M5GFX sprite buffer and the
// simulator's Frame are both this, so shapes come out byte for byte the same on both.
// A Raster is a view of a frame buffer that these functions write into (the buffer is the
// one mutable thing, as the device's sprite is); everything is clipped to it.
#pragma once

#include <cstdint>

#include "canvas.h"

namespace cpa {
namespace theme {

struct Raster {
  uint8_t* px = nullptr;
  int16_t w = 0;
  int16_t h = 0;
};

// r cut to the raster; w == 0 or h == 0 when nothing is left. A negative w / h extends
// left / up, as LGFX fillRect does.
Rect clip(const Raster& ras, const Rect& r);

void fill(const Raster& ras, const Rect& r, uint8_t entry);

// How many of 16 sample points (4 x 4) of pixel (dx, dy) of a radius-r top-left corner lie
// inside the circle: >= 13 is paint, <= 3 is what was under it, else one edge step.
uint8_t corner_cover(uint8_t radius, uint8_t dx, uint8_t dy);

struct RoundSpec {
  uint8_t radius = 0;  // cut to half the shorter side
  uint8_t paint = 0;
  uint8_t under = 0;
  uint8_t edge = 0;  // the baked step between paint and under
  uint8_t corners = kCornersAll;
};

void fill_round(const Raster& ras, const Rect& r, const RoundSpec& s);

// Every pixel in r replaced by table[pixel] (dimming).
void remap(const Raster& ras, const Rect& r, const uint8_t* table);

// w x h entries with the top left at (x, y); 0 is clear and leaves the pixel.
void blit(const Raster& ras, int16_t x, int16_t y, const uint8_t* px, int16_t w, int16_t h);

// A 4-bit picture (Mando's frames, the big digits): w x h values 0-15, row-major, two a
// byte, the first in the high nibble.
struct Nibbles {
  const uint8_t* px = nullptr;
  int16_t w = 0;
  int16_t h = 0;
};

// The entry each 4-bit value is drawn with; an entry of 0 leaves the pixel as it is.
struct EntryMap {
  uint8_t entry[16] = {};
};

void blit4(const Raster& ras, int16_t x, int16_t y, const Nibbles& img, const EntryMap& map);

// A filled circle of `diameter` px centred on the pixel corner (cx, cy): a pixel at least 3/4
// inside (4 x 4 samples) is paint, at least 0.3 the edge entry, anything less is left.
void dot(const Raster& ras, int16_t cx, int16_t cy, uint8_t diameter, uint8_t paint, uint8_t edge);

// A still radial glow (docs/19 §2.3): pixel (x, y) is lit to step s (entry base + s - 1)
// when 16 dx^2 + 25 dy^2 < q[16 (s - 1) + d], (dx, dy) from (cx, cy) and d the pixel's 4 x 4
// ordered dither; q falls with the step.
struct GlowSpec {
  int16_t cx = 0;
  int16_t cy = 0;
  const uint32_t* q = nullptr;  // 16 * steps
  uint8_t base = 0;
  uint8_t steps = 0;
};

// The glow on the pixels in r that hold `only` (Bg); every other pixel is left.
void glow(const Raster& ras, const Rect& r, uint8_t only, const GlowSpec& g);

}  // namespace theme
}  // namespace cpa
