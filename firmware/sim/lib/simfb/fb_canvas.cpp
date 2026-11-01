#include "fb_canvas.h"

#include <algorithm>
#include <cstring>

#include "text_fit.h"
#include "txt_rules.h"

namespace cpasim {
namespace {

constexpr uint32_t kBox = 0x25A1;  // □
constexpr size_t kRunMax = 96;     // bytes per drawString call, as on the device

// Han that names a language: "语言 Language", "English · Enter again: 中文".
bool names_a_language(uint32_t cp) { return cp == 0x8BED || cp == 0x8A00 || cp == 0x4E2D || cp == 0x6587; }
bool foreign_han(uint32_t cp) { return cpa::i18n::is_han(cp) && !names_a_language(cp); }

}  // namespace

// M5GFX on a palette sprite: the entries as colours, and a cell background only when the
// two entries differ.
void FbCanvas::text(int16_t x, int16_t y, const char* utf8, const cpa::Style& s) {
  const Ink ink{entry_of(s.fg), entry_of(s.bg), entry_of(s.fg) != entry_of(s.bg)};
  int32_t ink_end = x;
  const int16_t w = walk(x, y, utf8, &ink, &ink_end);
  const int edge = f_.w();
  if (w > 0 && (x < 0 || x + w > edge || ink_end >= edge)) {
    ++offscreen_;
    last_offscreen_ = std::to_string(x) + "," + std::to_string(y) + " " + utf8;
  }
  const size_t n = std::strlen(utf8);
  for (size_t i = 0; i < n;) {
    const cpa::Utf8Char ch = cpa::utf8_at(utf8, n, i);
    han_ += foreign_han(ch.cp) ? 1 : 0;
    i += ch.len;
  }
}

int16_t FbCanvas::textWidth(const char* utf8) const { return walk(0, 0, utf8, nullptr); }

bool FbCanvas::has_glyph(uint32_t cp) const {
  if (cp < 0x20 || cp > 0xFFFF) return false;
  GlyphMetric m;
  return font_.metric(cp, m);
}

// M5Display::walk: runs of drawable code points, □ for the others; returns the width.
// ink == nullptr only measures; ink_end, if given, gets one past the rightmost lit column.
int16_t FbCanvas::walk(int16_t x, int16_t y, const char* utf8, const Ink* ink, int32_t* ink_end) const {
  const size_t n = std::strlen(utf8);
  const CodePoints box{kBox};
  CodePoints run;
  size_t bytes = 0;
  int32_t cx = x;
  auto flush = [&](const CodePoints& cps) {
    if (cps.empty()) return;
    if (ink) font_.draw_run(f_, cx, y, cps, *ink);
    if (ink_end) *ink_end = std::max<int32_t>(*ink_end, cx + font_.ink_right(cps));
    cx += font_.run_width(cps);
  };
  for (size_t i = 0; i < n;) {
    const cpa::Utf8Char ch = cpa::utf8_at(utf8, n, i);
    if (!has_glyph(ch.cp)) {
      flush(run);
      run.clear();
      bytes = 0;
      flush(box);
      missing_ += ink ? 1 : 0;
    } else {
      if (bytes + ch.len > kRunMax) {
        flush(run);
        run.clear();
        bytes = 0;
      }
      run.push_back(ch.cp);
      bytes += ch.len;
    }
    i += ch.len;
  }
  flush(run);
  return static_cast<int16_t>(cx - x);
}

}  // namespace cpasim
