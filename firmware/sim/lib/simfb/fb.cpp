#include "fb.h"

#include <algorithm>

#include "theme_palette.h"

namespace cpasim {

uint8_t entry_of(cpa::Color c) { return cpa::theme::index_of(c).value(); }

// The entry's RGB565 (what pushSprite sends), 5 / 6 bits widened to 8.
Rgb panel_rgb(uint8_t entry) {
  const uint16_t c = cpa::theme::rgb565_of(entry);
  const unsigned r5 = c >> 11;
  const unsigned g6 = (c >> 5) & 63;
  const unsigned b5 = c & 31;
  return Rgb{static_cast<uint8_t>((r5 << 3) | (r5 >> 2)), static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
             static_cast<uint8_t>((b5 << 3) | (b5 >> 2))};
}

Frame::Frame(int w, int h, uint8_t px) : w_(w), h_(h), px_(static_cast<size_t>(w) * h, px) {}

uint8_t Frame::at(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
  return px_[static_cast<size_t>(y) * w_ + x];
}

void Frame::fill_rect(int x, int y, int w, int h, uint8_t px) {
  if (w < 0) {
    x += w;
    w = -w;
  }
  if (h < 0) {
    y += h;
    h = -h;
  }
  const int x0 = std::max(x, 0);
  const int y0 = std::max(y, 0);
  const int x1 = std::min(x + w, w_);
  const int y1 = std::min(y + h, h_);
  for (int yy = y0; yy < y1; ++yy) {
    std::fill(px_.begin() + yy * w_ + x0, px_.begin() + yy * w_ + std::max(x0, x1), px);
  }
}

void Frame::blit(const Frame& src, int x, int y) {
  for (int sy = 0; sy < src.h(); ++sy) {
    for (int sx = 0; sx < src.w(); ++sx) {
      const int dx = x + sx;
      const int dy = y + sy;
      if (dx >= 0 && dy >= 0 && dx < w_ && dy < h_) px_[static_cast<size_t>(dy) * w_ + dx] = src.at(sx, sy);
    }
  }
}

Frame scaled(const Frame& f, int k) {
  Frame out(f.w() * k, f.h() * k);
  for (int y = 0; y < f.h(); ++y) {
    for (int x = 0; x < f.w(); ++x) out.fill_rect(x * k, y * k, k, k, f.at(x, y));
  }
  return out;
}

}  // namespace cpasim
