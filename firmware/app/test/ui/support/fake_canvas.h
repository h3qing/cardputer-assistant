// FakeCanvas (05 §5.4): records what was drawn as text operations so tests can assert
// the key strings of a screen. Width model of efontCN_12: ASCII 6 px, every other
// code point 12 px; line height 12 px. No golden files (06 WP4).
// v0.5 (docs/19 §3.3): fillRound is not overridden, so a rounded shape is recorded as the
// fill of its rectangle in its paint colour; pictures, dimmed rectangles, dots and the glow
// are recorded. The big digits are recorded both as text (has("0:12") finds a timer) and as
// a DigitOp, measured with the theme's real cells.
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "canvas.h"
#include "text_fit.h"
#include "theme_digits.h"

namespace uitest {

struct TextOp {
  int16_t x = 0;
  int16_t y = 0;
  std::string text;
  cpa::Style style;
};

struct ArtOp {
  int16_t x = 0;
  int16_t y = 0;
  cpa::ArtId id;
};

struct DigitOp {
  int16_t x = 0;
  int16_t y = 0;
  std::string text;
  cpa::DigitFont font;
  cpa::Color ink;
  cpa::Color under;
};

struct DotOp {
  int16_t cx = 0;
  int16_t cy = 0;
  uint8_t diameter = 0;
  cpa::Color paint;
  cpa::Color under;
};

class FakeCanvas : public cpa::Canvas {
 public:
  std::vector<TextOp> texts;
  std::vector<std::pair<cpa::Rect, cpa::Color>> fills;
  std::vector<ArtOp> arts;
  std::vector<cpa::Rect> dims;
  std::vector<DigitOp> digit_ops;
  std::vector<DotOp> dots;
  std::vector<cpa::Rect> glows;

  void fill(const cpa::Rect& r, cpa::Color c) override { fills.emplace_back(r, c); }
  void art(int16_t x, int16_t y, cpa::ArtId id) override { arts.push_back(ArtOp{x, y, id}); }
  void dim(const cpa::Rect& r) override { dims.push_back(r); }
  void glow(const cpa::Rect& r) override { glows.push_back(r); }
  void dot(int16_t cx, int16_t cy, uint8_t d, cpa::Color paint, cpa::Color under) override {
    dots.push_back(DotOp{cx, cy, d, paint, under});
  }
  int16_t digits(int16_t x, int16_t y, const char* s, cpa::DigitFont f, cpa::Color ink, cpa::Color under) override {
    texts.push_back(TextOp{x, y, s, cpa::Style{ink, under}});
    digit_ops.push_back(DigitOp{x, y, s, f, ink, under});
    return digitsWidth(s, f);
  }
  int16_t digitsWidth(const char* s, cpa::DigitFont f) const override { return cpa::theme::digits_width(s, f); }

  void text(int16_t x, int16_t y, const char* utf8, const cpa::Style& s) override {
    texts.push_back(TextOp{x, y, utf8, s});
  }

  int16_t textWidth(const char* utf8) const override {
    const size_t n = std::char_traits<char>::length(utf8);
    int w = 0;
    for (size_t i = 0; i < n;) {
      const cpa::Utf8Char ch = cpa::utf8_at(utf8, n, i);
      w += ch.cp < 0x80 ? 6 : 12;
      i += ch.len;
    }
    return static_cast<int16_t>(w);
  }

  int16_t lineHeight() const override { return 12; }

  // Where each record stands, to drop what came before (FakeDisplay: a full frame).
  struct Mark {
    size_t texts = 0, fills = 0, arts = 0, dims = 0, digit_ops = 0, dots = 0, glows = 0;
  };
  Mark mark() const {
    return Mark{texts.size(), fills.size(), arts.size(), dims.size(), digit_ops.size(), dots.size(), glows.size()};
  }
  void drop_before(const Mark& m) {
    texts.erase(texts.begin(), texts.begin() + static_cast<std::ptrdiff_t>(m.texts));
    fills.erase(fills.begin(), fills.begin() + static_cast<std::ptrdiff_t>(m.fills));
    arts.erase(arts.begin(), arts.begin() + static_cast<std::ptrdiff_t>(m.arts));
    dims.erase(dims.begin(), dims.begin() + static_cast<std::ptrdiff_t>(m.dims));
    digit_ops.erase(digit_ops.begin(), digit_ops.begin() + static_cast<std::ptrdiff_t>(m.digit_ops));
    dots.erase(dots.begin(), dots.begin() + static_cast<std::ptrdiff_t>(m.dots));
    glows.erase(glows.begin(), glows.begin() + static_cast<std::ptrdiff_t>(m.glows));
  }

  void clear() {
    texts.clear();
    fills.clear();
    arts.clear();
    dims.clear();
    digit_ops.clear();
    dots.clear();
    glows.clear();
  }

  bool has(const std::string& needle) const { return find(needle) != nullptr; }

  // The first text containing needle drawn at or below y (a tab pill above it skipped).
  const TextOp* find_below(const std::string& needle, int16_t y) const {
    for (const auto& t : texts) {
      if (t.y >= y && t.text.find(needle) != std::string::npos) return &t;
    }
    return nullptr;
  }
  const TextOp* find(const std::string& needle) const {
    for (const auto& t : texts) {
      if (t.text.find(needle) != std::string::npos) return &t;
    }
    return nullptr;
  }

  // Every text drawn on the line starting at y, left to right, joined by '|'.
  std::string row(int16_t y) const {
    std::vector<TextOp> on;
    for (const auto& t : texts) {
      if (t.y == y) on.push_back(t);
    }
    std::sort(on.begin(), on.end(), [](const TextOp& a, const TextOp& b) { return a.x < b.x; });
    std::string out;
    for (const auto& t : on) out += (out.empty() ? "" : "|") + t.text;
    return out;
  }

  // The texts of the line starting at y as words: left to right, joined by a space, a text
  // drawn again a pixel to the right (a bold title) once. A hint bar of keycaps reads
  // "Enter 放弃 ` 返回".
  std::string line(int16_t y) const {
    std::vector<TextOp> on;
    for (const auto& t : texts) {
      if (t.y == y) on.push_back(t);
    }
    std::stable_sort(on.begin(), on.end(), [](const TextOp& a, const TextOp& b) { return a.x < b.x; });
    std::string out;
    for (size_t i = 0; i < on.size(); ++i) {
      if (i > 0 && on[i].text == on[i - 1].text && on[i].x == on[i - 1].x + 1) continue;
      out += (out.empty() ? "" : " ") + on[i].text;
    }
    return out;
  }

  std::string dump() const {
    std::string out;
    for (const auto& t : texts) out += std::to_string(t.y) + ":" + std::to_string(t.x) + " " + t.text + "\n";
    return out;
  }

  // A fill with this colour covers the line starting at y (selection highlight).
  bool filled_at(int16_t y, cpa::Color c) const {
    for (const auto& f : fills) {
      if (f.second == c && f.first.y <= y && y < f.first.y + f.first.h) return true;
    }
    return false;
  }
};

}  // namespace uitest
