// The legend of 1-9 and 0 on the recording pages (rec_legend.h, gh #53).
#include "rec_legend.h"

#include <initializer_list>

#include "reducers.h"
#include "text_fit.h"
#include "theme.h"

namespace cpa {
namespace {

// The memo page's row sets: one under Mando's sign; else the last two or all three of these.
// A row from the left edge starts at x 7: the lit capsule then starts at the hint bar's x 4.
constexpr LegendRow kMemoOne[] = {{7, 103, 236}};
constexpr LegendRow kMemoRows[] = {{60, 80, 236}, {60, 94, 236}, {7, 108, 236}};
constexpr uint8_t kMemoRowsMax = 3;
// The meeting page's: under the timer, then the timeline's labels' row (its capsule 1 px
// under the loudest bar, 1 px over the hint bar).
constexpr LegendRow kMeetingRows[] = {{48, 59, 236}, {7, 109, 236}};
constexpr uint8_t kMeetingRowsMax = 2;

const char* digit(uint8_t slot) {
  static const char* const kDigits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  return kDigits[slot <= 9 ? slot : 0];
}

LegendItem item(const Canvas& c, uint8_t slot, const char* label, bool chosen) {
  LegendItem it;
  it.slot = slot;
  it.label = LegendLabel::from(label);
  it.w = c.textWidth(it.label.c_str());
  it.chosen = chosen;
  return it;
}

bool cuts(const LegendItem& it, int16_t cut) { return it.slot != 0 && it.w > cut; }

// An item's width with a label of w px (and the cut mark): a bare keycap without one.
int16_t item_width(size_t label_bytes, int16_t w, bool mark) {
  if (label_bytes == 0) return kLegendCapW;
  return static_cast<int16_t>(kLegendCapW + kLegendCapGap + w + (mark ? kLegendCutMark : 0));
}

// What the fit counts an item as at cut: the cut and its mark, or less (a cut label is
// never wider).
int16_t fit_width(const LegendItem& it, int16_t cut) {
  const bool c = cuts(it, cut);
  return item_width(it.label.size(), c ? cut : it.w, c);
}

// How many items, greedily row by row, the rows take at cut and gap.
uint8_t flowed(const LegendList& l, const LegendRow* rows, uint8_t rows_n, int16_t cut, int16_t gap) {
  uint8_t i = 0;
  for (uint8_t r = 0; r < rows_n && i < l.n; ++r) {
    int16_t x = rows[r].x;
    while (i < l.n && x + fit_width(l.items[i], cut) <= rows[r].right) {
      x = static_cast<int16_t>(x + fit_width(l.items[i], cut) + gap);
      ++i;
    }
  }
  return i;
}

int16_t widest(const LegendList& l) {
  int16_t w = 0;
  for (uint8_t i = 0; i < l.n; ++i) {
    if (l.items[i].slot != 0 && l.items[i].w > w) w = l.items[i].w;
  }
  return w;
}

// The label cut to cut px between characters; a cut that ends on a space or '-' drops it.
LegendLabel cut_label(const Canvas& c, const LegendItem& it, int16_t cut) {
  if (!cuts(it, cut)) return it.label;
  const char* s = it.label.c_str();
  size_t n = utf8_fit(c, s, it.label.size(), cut);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '-')) --n;
  return LegendLabel::from(s, n);
}

LegendSpot spot_of(const Canvas& c, const LegendItem& it, int16_t cut) {
  LegendSpot s;
  s.slot = it.slot;
  s.label = cut_label(c, it, cut);
  s.cut = cuts(it, cut);
  s.w = item_width(s.label.size(), c.textWidth(s.label.c_str()), s.cut);
  s.chosen = it.chosen;
  s.ghost = it.ghost;
  return s;
}

// The lit one is the live-words capsule round its keycap (an Accent cap) and its Accent label;
// the others' digits in Ink, what you match, brighter than their keys; a cut label's two dots.
void draw_spot(Canvas& c, const LegendSpot& s) {
  if (s.ghost && !s.chosen) return;
  if (s.chosen) {
    const Rect capsule{static_cast<int16_t>(s.x - 3), static_cast<int16_t>(s.y - 1), static_cast<int16_t>(s.w + 6),
                       theme::kLiveH};
    theme::card(c, capsule, theme::CardKind::Selected, Color::Bg, theme::kLiveH / 2);
  }
  theme::Keycap k;
  k.min_w = kLegendCapW;
  k.under = s.chosen ? Color::Select : Color::Bg;
  k.cap = s.chosen ? Color::Accent : Color::Cap;
  k.ink = s.chosen ? Color::OnAccent : Color::Ink;
  theme::keycap(c, s.x, s.y, digit(s.slot), k);
  if (s.label.size() == 0) return;
  const Color ink = s.chosen ? Color::Accent : Color::Ink2;
  c.text(static_cast<int16_t>(s.x + kLegendCapW + kLegendCapGap), s.y, s.label.c_str(), theme::over(ink));
  if (!s.cut) return;
  const int16_t end = static_cast<int16_t>(s.x + s.w - kLegendCutMark);
  const int16_t y = static_cast<int16_t>(s.y + kLegendMarkDy);
  c.fill(Rect{end, y, 1, 1}, ink);
  c.fill(Rect{static_cast<int16_t>(end + 2), y, 1, 1}, ink);
}

const LegendRow* memo_rows(uint8_t k) { return k == 1 ? kMemoOne : kMemoRows + kMemoRowsMax - k; }

// Every key whole in the first k rows: the fit at the widest key alone.
bool whole_in(const LegendList& l, uint8_t k) {
  const int16_t w = widest(l) > kLegendMinCut ? widest(l) : kLegendMinCut;
  return legend_fit(l, memo_rows(k), k, w).cut > 0;
}

uint8_t memo_rows_for(const LegendList& l) {
  for (uint8_t k = 1; k <= kMemoRowsMax; ++k) {
    if (whole_in(l, k)) return k;
  }
  for (uint8_t k = 1; k < kMemoRowsMax; ++k) {
    if (legend_fit(l, memo_rows(k), k, kMemoLegendCut).cut > 0) return k;
  }
  return kMemoRowsMax;
}

// 0 "none" stays while the whole list fits both rows at four letters; else a bare [0]. No
// cell for "?": the title says "#9".
LegendList meeting_list(const Canvas& c, const AppState& s) {
  const LegendList all = legend_list(c, s.projects, s.rec.slot, false);
  if (legend_fit(all, kMeetingRows, kMeetingRowsMax, kMeetingNoneCut).cut > 0) return all;
  return legend_bare_none(all);
}

uint8_t meeting_rows_for(const LegendList& l) {
  return legend_fit(l, kMeetingRows, 1, kMeetingLegendCut).cut > 0 ? 1 : kMeetingRowsMax;
}

// The plan in the first k rows of a set, at its roomiest fit (or the last resort).
PageLegend page_legend(const Canvas& c, const LegendList& l, const LegendRow* rows, uint8_t k) {
  PageLegend out;
  out.rows = k;
  out.plan = legend_plan(c, l, rows, k, legend_fit(l, rows, k, kLegendMinCut));
  for (uint8_t i = 0; i < out.plan.n; ++i) {
    if (out.plan.spots[i].chosen && out.plan.spots[i].cut) out.chosen_cut = true;
  }
  return out;
}

}  // namespace

LegendList legend_list(const Canvas& c, const ProjectsView& p, uint8_t chosen, bool with_ghost) {
  LegendList out;
  uint8_t free_slot = 0;  // the chosen digit when free, else the first free one
  for (uint8_t slot = kSlotMin; slot <= kSlotMax; ++slot) {
    const Opt<ProjectRow> row = project_by_slot(p, slot);
    if (row.has) {
      out.items[out.n++] = item(c, slot, row.value.key.c_str(), slot == chosen);
    } else if (free_slot == 0 || slot == chosen) {
      free_slot = slot;
    }
  }
  out.items[out.n++] = item(c, 0, tr(kLegendNone), chosen == 0);
  if (!with_ghost || free_slot == 0) return out;
  out.items[out.n] = item(c, free_slot, "?", free_slot == chosen);
  out.items[out.n++].ghost = true;
  return out;
}

LegendList legend_bare_none(const LegendList& l) {
  LegendList out = l;
  for (uint8_t i = 0; i < out.n; ++i) {
    if (out.items[i].slot != 0) continue;
    out.items[i].label = LegendLabel{};
    out.items[i].w = 0;
  }
  return out;
}

LegendFit legend_fit(const LegendList& l, const LegendRow* rows, uint8_t rows_n, int16_t min_cut) {
  const int16_t top = widest(l) > min_cut ? widest(l) : min_cut;
  for (int16_t cut = top; cut >= min_cut; cut = static_cast<int16_t>(cut - kLegendStep)) {
    for (const int16_t gap : {kLegendGap, kLegendTightGap}) {
      if (flowed(l, rows, rows_n, cut, gap) == l.n) return LegendFit{cut, gap};
    }
  }
  return LegendFit{};
}

// A cut label is never wider than fit_width counted it, so the rows take every item the fit
// counted (a greedy flow of narrower items never needs more rows).
LegendPlan legend_plan(const Canvas& c, const LegendList& l, const LegendRow* rows, uint8_t rows_n,
                       const LegendFit& f) {
  const int16_t cut = f.cut > 0 ? f.cut : kLegendMinCut;
  const int16_t gap = f.cut > 0 ? f.gap : kLegendTightGap;
  LegendPlan p;
  uint8_t r = 0;
  int16_t x = rows_n > 0 ? rows[0].x : 0;
  for (uint8_t i = 0; i < l.n && r < rows_n;) {
    LegendSpot s = spot_of(c, l.items[i], cut);
    if (x + s.w > rows[r].right) {
      if (++r < rows_n) x = rows[r].x;
      continue;
    }
    s.x = x;
    s.y = rows[r].y;
    p.spots[p.n++] = s;
    x = static_cast<int16_t>(x + s.w + gap);
    ++i;
  }
  return p;
}

void draw_legend(Canvas& c, const LegendPlan& p) {
  for (uint8_t i = 0; i < p.n; ++i) draw_spot(c, p.spots[i]);
}

uint8_t memo_legend_rows(const Canvas& c, const AppState& s) {
  return memo_rows_for(legend_list(c, s.projects, s.rec.slot));
}

PageLegend memo_legend(const Canvas& c, const AppState& s) {
  const LegendList l = legend_list(c, s.projects, s.rec.slot);
  const uint8_t k = memo_rows_for(l);
  return page_legend(c, l, memo_rows(k), k);
}

uint8_t meeting_legend_rows(const Canvas& c, const AppState& s) { return meeting_rows_for(meeting_list(c, s)); }

PageLegend meeting_legend(const Canvas& c, const AppState& s) {
  const LegendList l = meeting_list(c, s);
  return page_legend(c, l, kMeetingRows, meeting_rows_for(l));
}

}  // namespace cpa
