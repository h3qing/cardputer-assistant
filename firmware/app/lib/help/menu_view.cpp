#include "menu_view.h"

#include <cstdio>

#include "text_fit.h"
#include "theme.h"
#include "words.h"

namespace cpa {
namespace {

constexpr Txt kNoKeys{"这里没有可列出的键", "No keys to list here"};

MenuStep stay(const MenuState& st) { return MenuStep{st, MenuOut::None, KeyEvent{}}; }
MenuStep close(const MenuState& st) { return MenuStep{st, MenuOut::Close, KeyEvent{}}; }
MenuStep replay(const MenuState& st, const KeyEvent& ev) { return MenuStep{st, MenuOut::Replay, ev}; }

// The page may have fewer rows than when sel was set (a recording ended, say).
uint8_t clamp_sel(uint8_t sel, uint8_t n) {
  if (n == 0) return 0;
  return sel < n ? sel : static_cast<uint8_t>(n - 1);
}

// top, moved just enough to show sel.
uint8_t top_for(uint8_t top, uint8_t sel) {
  if (sel < top) return sel;
  if (sel >= top + kMenuVisibleRows) return static_cast<uint8_t>(sel - kMenuVisibleRows + 1);
  return top;
}

MenuStep moved(const MenuState& st, const MenuContent& m, int delta) {
  const int last = m.n > 0 ? m.n - 1 : 0;
  const int to = clamp_sel(st.sel, m.n) + delta;
  MenuState out = st;
  out.page = m.page;
  out.sel = static_cast<uint8_t>(to < 0 ? 0 : (to > last ? last : to));
  out.top = top_for(st.top, out.sel);
  return stay(out);
}

// , and /: the previous or next page, wrapping; an overlay has no 去哪.
MenuStep paged(const MenuContent& m, int delta) {
  static const MenuPage kAll[] = {MenuPage::Here, MenuPage::Goto, MenuPage::Control};
  static const MenuPage kOverlay[] = {MenuPage::Here, MenuPage::Control};
  const MenuPage* pages = m.overlay ? kOverlay : kAll;
  const int n = m.overlay ? 2 : 3;
  int at = 0;
  for (int i = 0; i < n; ++i) at = pages[i] == m.page ? i : at;
  MenuState out;
  out.page = pages[(at + delta + n) % n];
  return stay(out);
}

MenuStep entered(const MenuState& st, const MenuContent& m) {
  if (m.n == 0) return stay(st);
  const MenuRow& r = m.rows[clamp_sel(st.sel, m.n)];
  return r.ev.kind == KeyEventKind::None ? stay(st) : replay(st, r.ev);
}

// Opt+letter goes somewhere or locks from any page: the Global or the System layer.
bool bound_everywhere(const KeyEvent& ev) {
  return keymap_lookup(Layer::Global, ev).action != Action::None ||
         keymap_lookup(Layer::System, ev).action != Action::None;
}

// A letter is Opt+letter (docs/13 §3.3: Tab, then a letter). On 去哪 and 控制 when a row
// there replays that key; on 本页 when Opt+letter is bound everywhere (the rows there are
// the page's own), but not over an overlay, which keeps its keys (§6.1).
MenuStep shortcut(const MenuState& st, const KeyEvent& ev, const MenuContent& m) {
  if (ev.kind != KeyEventKind::Char) return stay(st);
  const char c = (ev.ch >= 'A' && ev.ch <= 'Z') ? static_cast<char>(ev.ch - 'A' + 'a') : ev.ch;
  if (c < 'a' || c > 'z') return stay(st);
  const KeyEvent want{KeyEventKind::Chord, 0, kModOpt, c, Nav::None};
  if (m.page == MenuPage::Here) return !m.overlay && bound_everywhere(want) ? replay(st, want) : stay(st);
  for (uint8_t i = 0; i < m.n; ++i) {
    if (m.rows[i].ev == want) return replay(st, want);
  }
  return stay(st);
}

// The sheet (docs/19 §6.5): Card, its left corners rounded, a Rim top edge, over the
// dimmed page; its left edge moves left (sheet_x) when the page's widest row needs it.
constexpr int16_t kSheetY = 13;
constexpr int16_t kSheetH = 109;
constexpr int16_t kSheetXMin = 6;
constexpr int16_t kSheetXMax = 66;
constexpr int16_t kSegY = 17;
constexpr int16_t kSegH = 15;
constexpr int16_t kRowsY = 36;
constexpr int16_t kRowPad = 4;     // a row's right end to its label's
constexpr int16_t kBadgeEnd = 6;  // a row's right end to its badge's

// A fact on 控制 (a row there that is shown only): a word, not a key.
bool is_fact(const MenuContent& m, const MenuRow& r) {
  return m.page == MenuPage::Control && r.ev.kind == KeyEventKind::None;
}

// A row that explains a key it does not press (a hold, a range): Enter there only says how to
// press it (docs/20 U17), so selected it keeps its look (B6).
bool explains(const MenuRow& r) { return r.ev.kind == KeyEventKind::None && !r.fact; }

// A row's key: a Cap with Ink2, or, shown only, a Raised cap with Ink3 (Ink3 on a Cap reads
// at 3.5:1); on the selected caramel row an Accent2 cap with OnAccent. A selected explaining
// row is no caramel row: its Raised cap sits on Select.
theme::Keycap row_cap(const MenuRow& r, bool sel) {
  const bool shown = r.ev.kind == KeyEventKind::None;
  const bool lit = sel && !explains(r);
  theme::Keycap k;
  k.radius = 3;
  k.min_w = 12;
  k.cap = lit ? Color::Accent2 : (shown ? Color::Raised : Color::Cap);
  k.under = sel ? (lit ? Color::Accent : Color::Select) : Color::Card;
  k.ink = lit ? Color::OnAccent : (shown ? Color::Ink3 : Color::Ink2);
  return k;
}

// The key column of each row (its cap, or a fact's word) and the width most rows share (the
// narrower on a tie): labels start after it, and a wider key pushes only its own row. The
// facts' values share one column, after the widest fact (docs/20 U21's legend rows).
struct KeyColumn {
  int16_t w[kMenuRowsMax] = {};
  bool fact[kMenuRowsMax] = {};
  int16_t common = 0;
  int16_t facts = 0;  // the widest fact
};

KeyColumn key_column(const Canvas& c, const MenuContent& m) {
  KeyColumn k;
  for (uint8_t i = 0; i < m.n; ++i) {
    const MenuRow& r = m.rows[i];
    k.fact[i] = is_fact(m, r);
    k.w[i] = k.fact[i] ? c.textWidth(r.keys.c_str()) : theme::keycap_width(c, r.keys.c_str(), row_cap(r, false));
    if (k.fact[i] && k.w[i] > k.facts) k.facts = k.w[i];
  }
  uint8_t best_n = 0;
  for (uint8_t i = 0; i < m.n; ++i) {
    uint8_t n = 0;
    for (uint8_t j = 0; j < m.n; ++j) n = static_cast<uint8_t>(n + (k.w[j] == k.w[i] ? 1 : 0));
    if (n > best_n || (n == best_n && k.w[i] < k.common)) {
      best_n = n;
      k.common = k.w[i];
    }
  }
  return k;
}

// Where row i's label starts inside the row.
int16_t label_dx(const KeyColumn& k, uint8_t i) {
  const int16_t own = k.fact[i] ? k.facts : k.w[i];
  const int16_t w = own > k.common ? own : k.common;
  const int16_t dx = static_cast<int16_t>(2 + w + 5);
  return dx < kMenuKeyColW ? dx : kMenuKeyColW;
}

// "跑2" / "!1"; "" for none.
FixedStr<16> badge_text(const MenuRow& r) {
  if (r.badge == MenuBadge::None) return FixedStr<16>{};
  char buf[16];
  std::snprintf(buf, sizeof buf, r.badge == MenuBadge::Running ? tr(txt::kRunning) : "!%lu",
                static_cast<unsigned long>(r.badge_n));
  return FixedStr<16>::from(buf);
}

// From the label's start, what a row needs to its right end: the label, then the badge
// (6 px from the end) or the pad.
int16_t row_need(const Canvas& c, const MenuRow& r) {
  const FixedStr<16> badge = badge_text(r);
  const int16_t tail = badge.empty() ? kRowPad : static_cast<int16_t>(4 + c.textWidth(badge.c_str()) + kBadgeEnd);
  return static_cast<int16_t>(c.textWidth(r.label.c_str()) + tail);
}

int page_index(const MenuContent& m, MenuPage p) {
  if (p == MenuPage::Here) return 0;
  return p == MenuPage::Goto || m.overlay ? 1 : 2;
}

// The knob's left edge: on the page, or on its way there from mo.from.
int16_t knob_x(const Rect& seg, int16_t w, const MenuContent& m, const MenuMotion& mo) {
  const int16_t to = static_cast<int16_t>(seg.x + 1 + page_index(m, m.page) * w);
  if (mo.slide != MenuSlide::Knob) return to;
  const int16_t from = static_cast<int16_t>(seg.x + 1 + page_index(m, mo.from) * w);
  return theme::knob_at(from, to, mo.frame);
}

// A page's name on the knob (OnAccent) or off it (Ink2); one the sliding knob half covers is
// left out for that frame.
void draw_tab_name(Canvas& c, const Rect& cell, int16_t y, const char* name, int16_t kx) {
  const int16_t tw = c.textWidth(name);
  const int16_t tx = static_cast<int16_t>(cell.x + (cell.w - tw) / 2);
  const bool on = tx >= kx && tx + tw <= kx + cell.w;
  const bool off = tx + tw <= kx || tx >= kx + cell.w;
  if (on || off) c.text(tx, y, name, theme::over(on ? Color::OnAccent : Color::Ink2));
}

void draw_tabs(Canvas& c, const Rect& seg, const MenuContent& m, const MenuMotion& mo) {
  static const MenuPage kAll[] = {MenuPage::Here, MenuPage::Goto, MenuPage::Control};
  c.fillRound(seg, kSegH / 2, Color::Bg, Color::Card);
  const int n = m.overlay ? 2 : 3;
  const int16_t w = static_cast<int16_t>((seg.w - 2) / n);
  const int16_t kx = knob_x(seg, w, m, mo);
  c.fillRound(Rect{kx, static_cast<int16_t>(seg.y + 1), w, 13}, 6, Color::Accent, Color::Bg);
  for (int i = 0, k = 0; i < 3; ++i) {
    if (m.overlay && kAll[i] == MenuPage::Goto) continue;
    const Rect cell{static_cast<int16_t>(seg.x + 1 + k++ * w), seg.y, w, seg.h};
    draw_tab_name(c, cell, static_cast<int16_t>(seg.y + 2), menu_page_name(kAll[i]), kx);
  }
}

// [key] label and its badge; on the selected row all in OnAccent on caramel (mustard on
// caramel is 1.2:1). A selected explaining row is a Select capsule with its own colours
// (docs/20 B6: it still reads as an explanation, not as a key Enter presses).
void draw_row(Canvas& c, const Rect& row, int16_t ldx, const MenuRow& r, bool sel_row, bool fact) {
  const bool sel = sel_row && !explains(r);
  if (sel_row) c.fillRound(row, static_cast<uint8_t>(row.h / 2), sel ? Color::Accent : Color::Select, Color::Card);
  const int16_t y = static_cast<int16_t>(row.y + 1);
  const int16_t kx = static_cast<int16_t>(row.x + 2);
  if (fact) {
    c.text(kx, y, r.keys.c_str(), theme::over(sel ? Color::OnAccent : Color::Ink3));
  } else {
    theme::keycap(c, kx, y, r.keys.c_str(), row_cap(r, sel_row));
  }
  int16_t right = static_cast<int16_t>(row.x + row.w - kRowPad);
  const FixedStr<16> badge = badge_text(r);
  if (!badge.empty()) {
    const Color ink = sel ? Color::OnAccent : (r.badge == MenuBadge::NeedsYou ? Color::Warn : Color::Ink3);
    right = static_cast<int16_t>(draw_right(c, static_cast<int16_t>(row.x + row.w - kBadgeEnd), y, badge.c_str(),
                                            theme::over(ink)) - 4);
  }
  const int16_t x = static_cast<int16_t>(row.x + ldx);
  draw_fit(c, x, y, static_cast<int16_t>(right - x), r.label.c_str(), theme::over(sel ? Color::OnAccent : Color::Ink));
}

// A slim bar on the sheet's right edge while rows are off screen (docs/13 §6.3: "往下翻还有…").
void draw_scrollbar(Canvas& c, int16_t x, uint8_t top, uint8_t n) {
  if (n <= kMenuVisibleRows) return;
  const int16_t track = kMenuVisibleRows * kMenuRowH;
  c.fillRound(Rect{x, kRowsY, 2, track}, 1, Color::Line, Color::Card);
  const int16_t len = static_cast<int16_t>(track * kMenuVisibleRows / n);
  const int16_t at = static_cast<int16_t>(kRowsY + track * top / n);
  c.fillRound(Rect{x, at, 2, len}, 1, Color::Ink3, Color::Line);
}

void draw_rows(Canvas& c, const Rect& rows, const MenuState& st, const MenuContent& m) {
  if (m.n == 0) {
    c.text(static_cast<int16_t>(rows.x + 2), static_cast<int16_t>(rows.y + 1), tr(kNoKeys),
           theme::over(Color::Ink3));
    return;
  }
  const uint8_t sel = clamp_sel(st.sel, m.n);
  const uint8_t top = top_for(st.top, sel);
  const KeyColumn keys = key_column(c, m);
  for (uint8_t i = 0; i < kMenuVisibleRows && top + i < m.n; ++i) {
    const uint8_t at = static_cast<uint8_t>(top + i);
    const Rect row{rows.x, static_cast<int16_t>(rows.y + kMenuRowH * i), rows.w, kMenuRowH};
    draw_row(c, row, label_dx(keys, at), m.rows[at], at == sel, is_fact(m, m.rows[at]));
  }
  draw_scrollbar(c, static_cast<int16_t>(rows.x + rows.w + 1), top, m.n);
}


}  // namespace

MenuStep menu_key(const MenuState& st, const KeyEvent& ev, const MenuContent& m) {
  if (ev.kind == KeyEventKind::G0Tap || ev.kind == KeyEventKind::G0Long) return replay(st, ev);
  if (ev.kind == KeyEventKind::Nav && (ev.nav == Nav::Menu || ev.nav == Nav::Back)) return close(st);
  switch (keymap_lookup(Layer::List, ev).action) {
    case Action::Up: return moved(st, m, -1);
    case Action::Down: return moved(st, m, 1);
    case Action::TabPrev: return paged(m, -1);
    case Action::TabNext: return paged(m, 1);
    case Action::Primary: return entered(st, m);
    case Action::Back: return close(st);
    default: return shortcut(st, ev, m);
  }
}

int16_t menu_sheet_x(const Canvas& c, const MenuContent& m) {
  const KeyColumn keys = key_column(c, m);
  int16_t widest = 0;
  for (uint8_t i = 0; i < m.n; ++i) {
    const int16_t w = static_cast<int16_t>(label_dx(keys, i) + row_need(c, m.rows[i]));
    widest = w > widest ? w : widest;
  }
  // A row is the sheet less 4 px each side.
  const int16_t x = static_cast<int16_t>(kScreenW - 8 - widest);
  return x < kSheetXMin ? kSheetXMin : (x > kSheetXMax ? kSheetXMax : x);
}

namespace {

Rect sheet_seg(int16_t x, int16_t w) { return Rect{static_cast<int16_t>(x + 5), kSegY, static_cast<int16_t>(w - 9), kSegH}; }

// The sheet at its place in mo; returns it, cut to the screen.
Rect draw_sheet(Canvas& c, const MenuState& st, const MenuContent& m, const MenuMotion& mo) {
  const int16_t sx = menu_sheet_x(c, m);
  const int16_t w = static_cast<int16_t>(kScreenW - sx);
  const int16_t x = mo.slide == MenuSlide::Sheet ? theme::slide_at(kScreenW, sx, mo.frame) : sx;
  c.fillRound(Rect{x, kSheetY, w, kSheetH}, theme::kRadiusSheet, Color::Card, Color::Bg, kCornerTL | kCornerBL);
  c.fill(Rect{static_cast<int16_t>(x + theme::kRadiusSheet - 1), kSheetY,
              static_cast<int16_t>(w - theme::kRadiusSheet + 1), 1},
         Color::Rim);
  draw_tabs(c, sheet_seg(x, w), m, mo);
  draw_rows(c, Rect{static_cast<int16_t>(x + 4), kRowsY, static_cast<int16_t>(w - 8), 0}, st, m);
  return Rect{x, kSheetY, static_cast<int16_t>(kScreenW - x), kSheetH};
}

}  // namespace

void menu_render(Canvas& c, const MenuState& st, const MenuContent& m, const MenuMotion& mo) {
  c.dim(kContentRect);
  draw_sheet(c, st, m, mo);
  theme::hint_bar(c, tr(kMenuHint));
}

Rect menu_render_slide(Canvas& c, const MenuState& st, const MenuContent& m, const MenuMotion& mo) {
  if (mo.slide == MenuSlide::Sheet) return draw_sheet(c, st, m, mo);
  const int16_t sx = menu_sheet_x(c, m);
  const Rect seg = sheet_seg(sx, static_cast<int16_t>(kScreenW - sx));
  draw_tabs(c, seg, m, mo);
  return seg;
}

}  // namespace cpa
