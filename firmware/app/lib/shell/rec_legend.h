// gh #53 (docs/19 §6.6-§6.7, docs/20 U27): what 1-9 and 0 do while you talk, on the memo
// and the meeting page: the registered projects' keys in the number row's order (1 ... 9,
// then 0 不归属), each after its digit's keycap; the current choice lit as the live-words
// capsule was ("存到 #key"). On the memo page a chosen digit with no project reads "9 ?" in
// a cell kept for it after 0 whenever a digit is free, drawn only while chosen, so a press
// never moves the legend (its rows, cut and spots do not depend on the choice); the meeting
// keeps no such cell (its title says "#9"). It is always on the page and drawn by the full
// redraws only: the animation ticks never touch it, they only ask how much room it left
// (rec_overlay.h, RecRoom).
// The fit is pure: every label is measured once with the canvas's text width; one cut for
// all of them, from the widest down a letter (6 px) at a time and never under a minimum, is
// tried at the roomy gap and then the tight one. A label is cut between characters, and a
// cut one ends in two dots on its baseline (3 px; a "…" would take a CJK cell); 不归属 is
// never cut.
#pragma once

#include <cstdint>

#include "app_state.h"
#include "canvas.h"
#include "fixed_str.h"
#include "txt.h"

namespace cpa {

constexpr uint8_t kLegendMax = kMaxProjects + 1;  // 1-9 and 0
constexpr int16_t kLegendCapW = 10;               // a digit's keycap
constexpr int16_t kLegendCapGap = 2;              // keycap to label
constexpr int16_t kLegendGap = 8;                 // between items; the lit capsule takes 3 px of it
constexpr int16_t kLegendTightGap = 6;
constexpr int16_t kLegendStep = 6;     // one ASCII letter
constexpr int16_t kLegendMinCut = 18;  // three letters: the last resort
constexpr int16_t kLegendCutMark = 3;  // a cut label's two dots, 1 px apart
constexpr int16_t kLegendMarkDy = 10;  // the dots' row below the text's top: its baseline
constexpr Txt kLegendNone{"不归属", "none"};

using LegendLabel = FixedStr<32>;

struct LegendItem {
  uint8_t slot = 0;   // 0: 不归属
  LegendLabel label;  // the project's key, "?" (a slot with no project), 不归属 or "" (a bare [0])
  int16_t w = 0;      // the label's width
  bool chosen = false;
  bool ghost = false;  // the cell kept for "?": drawn only while chosen
};
struct LegendList {
  uint8_t n = 0;
  LegendItem items[kLegendMax];
};
// The registered projects by slot, then 0, then (with_ghost, a digit free) the cell kept for
// "?": the chosen digit when it has no project, else the first free one.
LegendList legend_list(const Canvas& c, const ProjectsView& p, uint8_t chosen, bool with_ghost = true);
LegendList legend_bare_none(const LegendList& l);  // 0 as a bare [0] keycap

// A row the legend flows into: items from x, their text at y, none past right.
struct LegendRow {
  int16_t x;
  int16_t y;
  int16_t right;
};

// The cut every label gets and the gap between items; cut 0: the list does not fit at min_cut.
struct LegendFit {
  int16_t cut = 0;
  int16_t gap = kLegendGap;
};
LegendFit legend_fit(const LegendList& l, const LegendRow* rows, uint8_t rows_n, int16_t min_cut);

// Where each item goes (its keycap's left edge, its text's y), its width with the label as
// cut, and the label as cut: what draw_legend draws.
struct LegendSpot {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  uint8_t slot = 0;
  LegendLabel label;
  bool chosen = false;
  bool cut = false;  // the label is cut: two dots after it
  bool ghost = false;
};
struct LegendPlan {
  uint8_t n = 0;
  LegendSpot spots[kLegendMax];
};
// The list flowed into the rows at f (at kLegendMinCut and the tight gap when f.cut is 0:
// as many as fit).
LegendPlan legend_plan(const Canvas& c, const LegendList& l, const LegendRow* rows, uint8_t rows_n,
                       const LegendFit& f);
void draw_legend(Canvas& c, const LegendPlan& p);

// The pages: how many rows the legend takes and what is drawn there.
struct PageLegend {
  uint8_t rows = 0;
  LegendPlan plan;
  bool chosen_cut = false;  // the lit label is cut (the memo's title then says it whole)
};
// The memo page's rows: one under Mando's sign (y 103, from x 7); two (y 94 right of Mando,
// and y 108); three (y 80 as well, and the waveform is then 2/3 as tall). Knowing which
// project is which outranks the waveform's height: the fewest rows where no key is cut; else
// the fewest where every key keeps six letters; else three.
constexpr int16_t kMemoLegendCut = 36;
uint8_t memo_legend_rows(const Canvas& c, const AppState& s);
PageLegend memo_legend(const Canvas& c, const AppState& s);
// The meeting page: the row under the timer (y 59, from x 48) while every label keeps five
// letters there; else that row and the timeline's labels' row (y 109, from x 7: 0:00 and
// 现在 give way). 0 is a bare [0] keycap before a label would go under four letters: the
// title says where the meeting goes.
constexpr int16_t kMeetingLegendCut = 30;
constexpr int16_t kMeetingNoneCut = 24;
uint8_t meeting_legend_rows(const Canvas& c, const AppState& s);
PageLegend meeting_legend(const Canvas& c, const AppState& s);

}  // namespace cpa
