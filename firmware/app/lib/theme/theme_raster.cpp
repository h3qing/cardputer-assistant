#include "theme_raster.h"

#include <cstring>

namespace cpa {
namespace theme {
namespace {

int16_t lo(int a, int b) { return static_cast<int16_t>(a < b ? a : b); }
int16_t hi(int a, int b) { return static_cast<int16_t>(a > b ? a : b); }

void put(const Raster& ras, int x, int y, uint8_t entry) {
  if (x >= 0 && y >= 0 && x < ras.w && y < ras.h) ras.px[y * ras.w + x] = entry;
}

// The entry for pixel (dx, dy) of a corner, counted from the corner's outer point.
uint8_t corner_px(const RoundSpec& s, uint8_t radius, uint8_t dx, uint8_t dy) {
  const uint8_t cover = corner_cover(radius, dx, dy);
  if (cover >= 13) return s.paint;
  return cover <= 3 ? s.under : s.edge;
}

// How many of 16 sample points of pixel (x, y) lie in the circle of `diameter` around the
// pixel corner (cx, cy); in eighths of a pixel, sample k sits at 2k + 1.
uint8_t dot_cover(int x, int y, int cx, int cy, uint8_t diameter) {
  const int32_t r = 4 * diameter;
  uint8_t n = 0;
  for (int sy = 0; sy < 4; ++sy) {
    for (int sx = 0; sx < 4; ++sx) {
      const int32_t px = 8 * (x - cx) + 2 * sx + 1;
      const int32_t py = 8 * (y - cy) + 2 * sy + 1;
      n = static_cast<uint8_t>(n + (px * px + py * py <= r * r ? 1 : 0));
    }
  }
  return n;
}

constexpr uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

uint8_t glow_step(const GlowSpec& g, int x, int y) {
  const int32_t dx = x - g.cx;
  const int32_t dy = y - g.cy;
  const uint32_t q = static_cast<uint32_t>(16 * dx * dx + 25 * dy * dy);
  const uint8_t d = kBayer4[y & 3][x & 3];
  uint8_t s = 0;
  while (s < g.steps && q < g.q[16 * s + d]) ++s;
  return s;
}

}  // namespace

Rect clip(const Raster& ras, const Rect& r) {
  const int x = r.w < 0 ? r.x + r.w : r.x;
  const int y = r.h < 0 ? r.y + r.h : r.y;
  const int w = r.w < 0 ? -r.w : r.w;
  const int h = r.h < 0 ? -r.h : r.h;
  const int16_t x0 = hi(x, 0), y0 = hi(y, 0);
  const int16_t x1 = lo(x + w, ras.w), y1 = lo(y + h, ras.h);
  if (x1 <= x0 || y1 <= y0) return Rect{x0, y0, 0, 0};
  return Rect{x0, y0, static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0)};
}

void fill(const Raster& ras, const Rect& r, uint8_t entry) {
  const Rect c = clip(ras, r);
  for (int y = c.y; y < c.y + c.h; ++y) std::memset(ras.px + y * ras.w + c.x, entry, static_cast<size_t>(c.w));
}

uint8_t corner_cover(uint8_t radius, uint8_t dx, uint8_t dy) {
  // In eighths of a pixel: sample k of a pixel sits at 2k + 1, the circle's centre at 8r.
  const int32_t c = 8 * radius;
  uint8_t n = 0;
  for (int sy = 0; sy < 4; ++sy) {
    for (int sx = 0; sx < 4; ++sx) {
      const int32_t px = 8 * dx + 2 * sx + 1 - c;
      const int32_t py = 8 * dy + 2 * sy + 1 - c;
      n = static_cast<uint8_t>(n + (px * px + py * py <= c * c ? 1 : 0));
    }
  }
  return n;
}

void fill_round(const Raster& ras, const Rect& r, const RoundSpec& s) {
  fill(ras, r, s.paint);
  if (r.w <= 0 || r.h <= 0) return;
  const uint8_t radius = static_cast<uint8_t>(lo(s.radius, lo(r.w / 2, r.h / 2)));
  const int x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
  for (uint8_t dy = 0; dy < radius; ++dy) {
    for (uint8_t dx = 0; dx < radius; ++dx) {
      const uint8_t e = corner_px(s, radius, dx, dy);
      if (e == s.paint) continue;
      if (s.corners & kCornerTL) put(ras, r.x + dx, r.y + dy, e);
      if (s.corners & kCornerTR) put(ras, x1 - dx, r.y + dy, e);
      if (s.corners & kCornerBL) put(ras, r.x + dx, y1 - dy, e);
      if (s.corners & kCornerBR) put(ras, x1 - dx, y1 - dy, e);
    }
  }
}

void remap(const Raster& ras, const Rect& r, const uint8_t* table) {
  const Rect c = clip(ras, r);
  for (int y = c.y; y < c.y + c.h; ++y) {
    uint8_t* row = ras.px + y * ras.w;
    for (int x = c.x; x < c.x + c.w; ++x) row[x] = table[row[x]];
  }
}

void blit(const Raster& ras, int16_t x, int16_t y, const uint8_t* px, int16_t w, int16_t h) {
  for (int sy = 0; sy < h; ++sy) {
    for (int sx = 0; sx < w; ++sx) {
      const uint8_t e = px[sy * w + sx];
      if (e != 0) put(ras, x + sx, y + sy, e);
    }
  }
}

void blit4(const Raster& ras, int16_t x, int16_t y, const Nibbles& img, const EntryMap& map) {
  const Rect c = clip(ras, Rect{x, y, img.w, img.h});
  for (int py = c.y; py < c.y + c.h; ++py) {
    for (int px = c.x; px < c.x + c.w; ++px) {
      const int i = (py - y) * img.w + (px - x);
      const uint8_t b = img.px[i >> 1];
      const uint8_t e = map.entry[(i & 1) ? (b & 15) : (b >> 4)];
      if (e != 0) ras.px[py * ras.w + px] = e;
    }
  }
}

void dot(const Raster& ras, int16_t cx, int16_t cy, uint8_t diameter, uint8_t paint, uint8_t edge) {
  const int reach = (diameter + 1) / 2 + 1;
  for (int y = cy - reach; y < cy + reach; ++y) {
    for (int x = cx - reach; x < cx + reach; ++x) {
      const uint8_t cover = dot_cover(x, y, cx, cy, diameter);
      if (cover >= 12) {
        put(ras, x, y, paint);
      } else if (cover >= 5) {
        put(ras, x, y, edge);
      }
    }
  }
}

void glow(const Raster& ras, const Rect& r, uint8_t only, const GlowSpec& g) {
  const Rect c = clip(ras, r);
  for (int y = c.y; y < c.y + c.h; ++y) {
    uint8_t* row = ras.px + y * ras.w;
    for (int x = c.x; x < c.x + c.w; ++x) {
      if (row[x] != only) continue;
      const uint8_t s = glow_step(g, x, y);
      if (s > 0) row[x] = static_cast<uint8_t>(g.base + s - 1);
    }
  }
}

}  // namespace theme
}  // namespace cpa
