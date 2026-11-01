// RasterCanvas (docs/19 §3.3): every Canvas call but text on an 8-bit palette frame buffer.
// The device (hal/m5_display, on its M5GFX sprite's buffer) and the simulator (FbCanvas, on
// a Frame) both derive from it and add only their efont text, so shapes are drawn by one
// piece of code. Colours go through the theme's palette tables, never as numbers.
#pragma once

#include <cstdint>

#include "canvas.h"
#include "theme_raster.h"

namespace cpa {
namespace theme {

class RasterCanvas : public Canvas {
 public:
  void fill(const Rect& r, Color c) override;
  void fillRound(const Rect& r, uint8_t radius, Color paint, Color under, uint8_t corners = kCornersAll) override;
  void fillEdge(const Rect& r, Color paint, Color under) override;
  void art(int16_t x, int16_t y, ArtId id) override;
  void dim(const Rect& r) override;
  void dot(int16_t cx, int16_t cy, uint8_t diameter, Color paint, Color under) override;
  int16_t digits(int16_t x, int16_t y, const char* chars, DigitFont f, Color ink, Color under) override;
  int16_t digitsWidth(const char* chars, DigitFont f) const override;
  void glow(const Rect& r) override;

  // fillRound, fillEdge and dot calls whose (paint, under) pair had no edge entry of its own (tokens.py
  // AA_PAIRS): drawn with the nearest colour. The simulator's tour expects none.
  uint32_t aa_misses() const { return aa_misses_; }

 protected:
  virtual Raster surface() const = 0;

 private:
  uint32_t aa_misses_ = 0;
};

}  // namespace theme
}  // namespace cpa
