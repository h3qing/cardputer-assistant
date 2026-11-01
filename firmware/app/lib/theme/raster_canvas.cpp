#include "raster_canvas.h"

#include "theme_art.h"
#include "theme_digits.h"
#include "theme_palette.h"

namespace cpa {
namespace theme {

void RasterCanvas::fill(const Rect& r, Color c) { theme::fill(surface(), r, index_of(c).value()); }

void RasterCanvas::fillRound(const Rect& r, uint8_t radius, Color paint, Color under, uint8_t corners) {
  RoundSpec s;
  s.radius = radius;
  s.paint = index_of(paint).value();
  s.under = index_of(under).value();
  s.edge = aa_of(paint, under);
  s.corners = corners;
  if (radius > 0 && corners != 0 && !aa_exact(paint, under)) ++aa_misses_;
  fill_round(surface(), r, s);
}

void RasterCanvas::fillEdge(const Rect& r, Color paint, Color under) {
  if (!aa_exact(paint, under)) ++aa_misses_;
  theme::fill(surface(), r, aa_of(paint, under));
}

void RasterCanvas::art(int16_t x, int16_t y, ArtId id) {
  const ArtView a = art_of(id);
  if (a.mando4) return blit4(surface(), x, y, Nibbles{a.px, a.w, a.h}, mando_map());
  blit(surface(), x, y, a.px, a.w, a.h);
}

void RasterCanvas::dot(int16_t cx, int16_t cy, uint8_t diameter, Color paint, Color under) {
  if (!aa_exact(paint, under)) ++aa_misses_;
  theme::dot(surface(), cx, cy, diameter, index_of(paint).value(), aa_of(paint, under));
}

int16_t RasterCanvas::digits(int16_t x, int16_t y, const char* chars, DigitFont f, Color ink, Color under) {
  const EntryMap map = digit_ink(ink, under);
  int16_t at = x;
  for (const char* p = chars; *p != '\0'; ++p) {
    const Nibbles g = digit_glyph(f, *p);
    blit4(surface(), at, y, g, map);
    at = static_cast<int16_t>(at + g.w);
  }
  return static_cast<int16_t>(at - x);
}

int16_t RasterCanvas::digitsWidth(const char* chars, DigitFont f) const { return digits_width(chars, f); }

void RasterCanvas::glow(const Rect& r) { theme::glow(surface(), r, index_of(Color::Bg).value(), glow_spec()); }

void RasterCanvas::dim(const Rect& r) { remap(surface(), r, dim_table()); }

}  // namespace theme
}  // namespace cpa
