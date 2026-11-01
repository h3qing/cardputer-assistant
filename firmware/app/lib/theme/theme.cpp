#include "theme.h"

#include <cstring>

#include "hint_layout.h"
#include "text_fit.h"

namespace cpa {
namespace theme {
namespace {

// The hint bar's spacing, roomiest first: where it starts, between items, key to words.
struct HintLevel {
  int16_t x0;
  int16_t gap;
  int16_t pad;
};
constexpr HintLevel kHintLevels[] = {{4, 8, 3}, {4, 4, 2}, {1, 3, 1}};

TextLine part(const char* s, uint8_t at, uint8_t len) { return TextLine::from(s + at, len); }

constexpr int16_t kOneCharCap = 12;  // a one-character key's cap, as in the menu

// A key in a line of keys and words: G0 in Accent, every cap at least kOneCharCap wide.
Keycap hint_cap(const char* key, Color under = Color::Bg) {
  Keycap k;
  k.under = under;
  k.min_w = kOneCharCap;
  if (std::strcmp(key, "G0") == 0) k.ink = Color::Accent;
  return k;
}

int16_t item_width(const Canvas& c, const char* hint, const HintItem& it, int16_t pad) {
  int16_t w = c.textWidth(part(hint, it.text_at, it.text_len).c_str());
  if (it.key_len == 0) return w;
  const TextLine key = part(hint, it.key_at, it.key_len);
  return static_cast<int16_t>(w + keycap_width(c, key.c_str(), hint_cap(key.c_str())) + (w > 0 ? pad : 0));
}

int32_t items_width(const Canvas& c, const char* hint, const HintItems& h, const HintLevel& lv) {
  int32_t w = 0;
  for (uint8_t i = 0; i < h.n; ++i) w += item_width(c, hint, h.items[i], lv.pad) + (i > 0 ? lv.gap : 0);
  return w;
}

bool fits(const Canvas& c, const char* hint, const HintItems& h, const HintLevel& lv) {
  return lv.x0 + items_width(c, hint, h, lv) <= kScreenW;
}

// The roomiest level the items fit the bar at; nullptr: none (or too many items).
const HintLevel* bar_level(const Canvas& c, const char* hint, const HintItems& h) {
  if (h.overflow) return nullptr;
  for (const HintLevel& lv : kHintLevels) {
    if (fits(c, hint, h, lv)) return &lv;
  }
  return nullptr;
}

// What a line of keys and words is drawn with: the words' ink and what is under it all.
struct LineInk {
  Color words;
  Color under;
};

int16_t draw_items(Canvas& c, int16_t x0, int16_t y, const char* hint, const HintItems& h, const HintLevel& lv,
                   const LineInk& ink) {
  int16_t x = x0;
  for (uint8_t i = 0; i < h.n; ++i) {
    const HintItem& it = h.items[i];
    if (i > 0) x = static_cast<int16_t>(x + lv.gap);
    if (it.key_len > 0) {
      const TextLine key = part(hint, it.key_at, it.key_len);
      x = static_cast<int16_t>(x + keycap(c, x, y, key.c_str(), hint_cap(key.c_str(), ink.under)));
      if (it.text_len > 0) x = static_cast<int16_t>(x + lv.pad);
    }
    const TextLine text = part(hint, it.text_at, it.text_len);
    if (it.text_len > 0) c.text(x, y, text.c_str(), Style{ink.words, ink.under});
    x = static_cast<int16_t>(x + c.textWidth(text.c_str()));
  }
  return static_cast<int16_t>(x - x0);
}

// The hint bar: the items as keycaps at the roomiest level that fits, else the text as one
// line of Ink3, as v0.4 drew it.
// The hint as one line of words: as written, or (docs/21 §6.6) a bracketed hint's keys and
// words without the brackets, "Enter open  ` back".
FixedStr<191> plain_hint(const char* hint, const HintItems& h) {
  if (std::strchr(hint, '[') == nullptr) return FixedStr<191>::from(hint);
  FixedStr<191> out;
  for (uint8_t i = 0; i < h.n; ++i) {
    const HintItem& it = h.items[i];
    if (i > 0) out = out.append("  ");
    out = out.append(hint + it.key_at, it.key_len);
    if (it.key_len > 0 && it.text_len > 0) out = out.append(" ");
    out = out.append(hint + it.text_at, it.text_len);
  }
  return out;
}

void draw_bar(Canvas& c, const char* hint, const HintItems& h) {
  c.fill(kHintRect, Color::Bg);
  const HintLevel* lv = bar_level(c, hint, h);
  if (lv != nullptr) {
    draw_items(c, lv->x0, kHintRect.y, hint, h, *lv, LineInk{Color::Ink3, Color::Bg});
    return;
  }
  draw_fit(c, 0, kHintRect.y, kScreenW, plain_hint(hint, h).c_str(), Style{Color::Ink3, Color::Bg});
}

// The straight part of r's top edge, one pixel high.
void top_edge(Canvas& c, const Rect& r, uint8_t radius, Color edge) {
  const int16_t in = radius > 0 ? static_cast<int16_t>(radius - 1) : 0;
  c.fill(Rect{static_cast<int16_t>(r.x + in), r.y, static_cast<int16_t>(r.w - 2 * in), 1}, edge);
}

}  // namespace

ToneInk tone_ink(Tone t) {
  switch (t) {
    case Tone::Info: return ToneInk{Color::InfoBg, Color::Accent};
    case Tone::Ok: return ToneInk{Color::OkBg, Color::Ok};
    case Tone::Warn: return ToneInk{Color::WarnBg, Color::Warn};
    case Tone::Rec: return ToneInk{Color::RecBg, Color::Rec};
    case Tone::Accent: return ToneInk{Color::Accent, Color::OnAccent};
    default: return ToneInk{Color::Raised, Color::Ink2};
  }
}

Color card_fill(CardKind k) {
  switch (k) {
    case CardKind::Selected: return Color::Select;
    case CardKind::Sheet: return Color::Sheet;
    default: return Color::Card;
  }
}

void card(Canvas& c, const Rect& r, CardKind k, Color under, uint8_t radius) {
  if (k == CardKind::Selected) {
    c.fillRound(r, radius, Color::Live, under);
    const Rect in{static_cast<int16_t>(r.x + 1), static_cast<int16_t>(r.y + 1), static_cast<int16_t>(r.w - 2),
                  static_cast<int16_t>(r.h - 2)};
    c.fillRound(in, radius > 0 ? static_cast<uint8_t>(radius - 1) : 0, Color::Select, Color::Live);
    return;
  }
  c.fillRound(r, radius, card_fill(k), under);
  top_edge(c, r, radius, k == CardKind::Sheet ? Color::Rim : Color::CardRim);
}

int16_t keycap_width(const Canvas& c, const char* key, const Keycap& k) {
  const int16_t w = static_cast<int16_t>(c.textWidth(key) + 2 * k.pad);
  return w < k.min_w ? k.min_w : w;
}

int16_t keycap(Canvas& c, int16_t x, int16_t y, const char* key, const Keycap& k) {
  const int16_t w = keycap_width(c, key, k);
  c.fillRound(Rect{x, y, w, kCapH}, k.radius, k.cap, k.under);
  c.text(static_cast<int16_t>(x + (w - c.textWidth(key)) / 2), y, key, over(k.ink));
  return w;
}

int16_t pill_width(const Canvas& c, const char* text, PillSize s) {
  return static_cast<int16_t>(c.textWidth(text) + 2 * s.pad);
}

int16_t pill(Canvas& c, int16_t x, int16_t y, const char* text, Tone t, Color under, PillSize s) {
  const ToneInk ti = tone_ink(t);
  const int16_t w = pill_width(c, text, s);
  c.fillRound(Rect{x, y, w, s.h}, static_cast<uint8_t>(s.h / 2), ti.fill, under);
  c.text(static_cast<int16_t>(x + s.pad), static_cast<int16_t>(y + (s.h - kCapH + 1) / 2), text, over(ti.ink));
  return w;
}

Rect sign(Canvas& c, int16_t xc, int16_t y, const char* text, Tone t, Color under) {
  PillSize s;
  s.h = kSignH;
  s.pad = 5;
  const int16_t w = pill_width(c, text, s);
  const int16_t centred = static_cast<int16_t>(xc - w / 2);
  const int16_t max_x = static_cast<int16_t>(kScreenW - kSignEdge - w);
  const int16_t x = centred < kSignEdge ? kSignEdge : (centred > max_x ? max_x : centred);  // docs/21 §6.9
  pill(c, x, y, text, t, under, s);
  return Rect{x, y, w, kSignH};
}

int16_t live_capsule(Canvas& c, int16_t x, int16_t y, const char* text, Color under) {
  const int16_t w = static_cast<int16_t>(c.textWidth(text) + 10);
  card(c, Rect{x, y, w, kLiveH}, CardKind::Selected, under, kLiveH / 2);
  c.text(static_cast<int16_t>(x + 5), static_cast<int16_t>(y + 1), text, over(Color::Accent));
  return w;
}

void stripe(Canvas& c, int16_t x, int16_t y, int16_t h, Tone t, Color under) {
  c.fillRound(Rect{x, y, 3, h}, 1, tone_ink(t).ink, under);
}

int16_t title(Canvas& c, int16_t x, int16_t y, const char* utf8, Color under) {
  c.text(x, y, utf8, Style{Color::Ink, under});
  c.text(static_cast<int16_t>(x + 1), y, utf8, over(Color::Ink));
  return static_cast<int16_t>(c.textWidth(utf8) + 1);
}

void tile(Canvas& c, const Rect& r, const TileSpec& t, bool selected) {
  const Color fill = selected ? Color::Accent : Color::Bg;
  if (selected) c.fillRound(r, kRadiusTile, fill, Color::Bg);
  Keycap k;
  k.cap = selected ? Color::Accent2 : Color::Cap;
  k.ink = selected ? Color::OnAccent : Color::Ink2;
  k.under = fill;
  k.radius = 3;
  const int16_t y = static_cast<int16_t>(r.y + (r.h - kCapH) / 2);  // 2 in a 16 px tile, 1 in 14 px
  int16_t lx = static_cast<int16_t>(r.x + 2 + keycap(c, static_cast<int16_t>(r.x + 2), y, t.key, k) + 4);
  int16_t right = static_cast<int16_t>(r.x + r.w - 4);
  if (t.badge[0] != '\0') {
    right = draw_right(c, right, y, t.badge, over(selected ? Color::OnAccent : t.badge_ink));
    right = static_cast<int16_t>(right - 4);
  } else if (c.textWidth(t.label) > right - lx) {
    // a label that would be cut takes 3 px of the gap after the key and 2 of the right
    // margin, so docs/20 U22's "开 session" (60 px) is whole in a 75 px tile, a pixel after
    // the key and 2 px short of the tile's rounded end
    lx = static_cast<int16_t>(lx - 3);
    right = static_cast<int16_t>(r.x + r.w - 2);
  }
  const Color ink = selected ? Color::OnAccent : (t.muted ? Color::Ink3 : Color::Ink);
  draw_fit(c, lx, y, static_cast<int16_t>(right - lx), t.label, over(ink));
}

bool hint_fits(const Canvas& c, const char* hint) {
  const HintItems h = hint_items(hint);
  return bar_level(c, hint, h) != nullptr;
}

void hint_bar(Canvas& c, const char* hint) { draw_bar(c, hint, hint_items(hint)); }

bool hint_fits(const Canvas& c, const HintLine& line) { return bar_level(c, line.text, line.items) != nullptr; }

void hint_bar(Canvas& c, const HintLine& line) { draw_bar(c, line.text, line.items); }

int16_t keys_text(Canvas& c, int16_t x, int16_t y, const char* text, Color ink, Color under) {
  const HintItems h = hint_items(text);
  return draw_items(c, x, y, text, h, kHintLevels[0], LineInk{ink, under});
}

void toast_bar(Canvas& c, const char* text, Tone t) {
  const ToneInk ti = tone_ink(t);
  c.fill(kHintRect, Color::Bg);
  const Rect r{kToastX, kHintRect.y, static_cast<int16_t>(kScreenW - 2 * kToastX), kCapH};
  c.fillRound(r, kCapH / 2, ti.fill, Color::Bg);
  const int16_t x = static_cast<int16_t>(r.x + kToastPad);
  draw_fit(c, x, r.y, kToastTextW, text, over(ti.ink));
}

}  // namespace theme
}  // namespace cpa
