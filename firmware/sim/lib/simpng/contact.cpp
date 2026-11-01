#include "contact.h"

#include "fb_canvas.h"
#include "text_fit.h"

namespace cpasim {
namespace {

constexpr int kGap = 8;
constexpr int kCaptionH = 14;

// The caption on its black strip, drawn apart so the sheet may be taller than the
// Canvas's int16 coordinates reach.
Frame caption_strip(const std::string& caption, int w) {
  Frame strip(w, kCaptionH, entry_of(cpa::Color::Bg));
  FbCanvas c(strip);
  cpa::draw_fit(c, 2, 1, static_cast<int16_t>(w - 4), caption.c_str(), cpa::Style{cpa::Color::Accent, cpa::Color::Bg});
  return strip;
}

}  // namespace

Frame contact_sheet(const std::vector<Captioned>& shots, int columns) {
  const int n = static_cast<int>(shots.size());
  const int cols = n < columns ? (n > 0 ? n : 1) : columns;
  const int rows = (n + cols - 1) / cols;
  const int cell_w = shots.empty() ? cpa::kScreenW : shots.front().frame->w();
  const int cell_h = (shots.empty() ? cpa::kScreenH : shots.front().frame->h()) + kCaptionH;
  Frame sheet(kGap + cols * (cell_w + kGap), kGap + (rows > 0 ? rows : 1) * (cell_h + kGap),
              entry_of(cpa::Color::Line));  // the theme's Line: lighter than any screen's Bg
  for (int i = 0; i < n; ++i) {
    const int x = kGap + (i % cols) * (cell_w + kGap);
    const int y = kGap + (i / cols) * (cell_h + kGap);
    sheet.blit(*shots[i].frame, x, y);
    sheet.blit(caption_strip(shots[i].caption, cell_w), x, y + shots[i].frame->h());
  }
  return sheet;
}

}  // namespace cpasim
