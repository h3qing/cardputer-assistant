// The oracle's simulator side: firmware/sim's FbCanvas drawing into a Frame.
#include <cstring>

#include "fb.h"
#include "fb_canvas.h"
#include "oracle_cases.h"

namespace {

cpa::Color color(int c) { return static_cast<cpa::Color>(c); }

}  // namespace

// Case i on a black frame, as m5gfx_render: one frame and canvas for every case unless fresh.
int fbcanvas_render(int i, uint8_t* out, bool fresh) {
  static cpasim::Frame* f = nullptr;
  static cpasim::FbCanvas* cv = nullptr;
  if (!f || fresh) {
    delete cv;
    delete f;
    f = new cpasim::Frame(240, 135);
    cv = new cpasim::FbCanvas(*f);
  }
  cv->fill(cpa::Rect{0, 0, 240, 135}, cpa::Color::Bg);
  const oracle::Case& c = oracle::kCases[i];
  int w = 0;
  if (c.fill) {
    cv->fill(cpa::Rect{static_cast<int16_t>(c.x), static_cast<int16_t>(c.y), static_cast<int16_t>(c.w),
                       static_cast<int16_t>(c.h)},
             color(c.fg));
  } else {
    cv->text(static_cast<int16_t>(c.x), static_cast<int16_t>(c.y), c.text, cpa::Style{color(c.fg), color(c.bg)});
    w = cv->textWidth(c.text);
  }
  std::memcpy(out, f->pixels().data(), 240 * 135);
  return w;
}
