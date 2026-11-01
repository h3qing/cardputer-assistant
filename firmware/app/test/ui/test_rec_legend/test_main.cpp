// gh #53 (docs/19 §6.6-§6.7, docs/20 U27), the user's memo: while you talk, the screen shows
// what 1-9 are. The memo page (held, the grace, hands free) and the meeting page keep a
// legend in the number row's order, 1 ... 9 then 0 不归属, the projects' keys after their
// digits' keycaps, the current choice lit; a chosen digit with no project reads "9 ?" on the
// memo page, in a cell kept for it (the meeting's title says "#9").
//   fit     one cut for every label, the roomiest the rows take, never under a minimum; cut
//           between characters (CJK too), a cut label ending in two dots; 不归属 never cut;
//   memo    the fewest rows where no key is cut (one for the user's two projects, two for
//           three long keys, three for five or six, the waveform then 2/3 as tall); else the
//           fewest that keep six letters; else three;
//   meeting the row under the timer; with more, the timeline's labels' row too; at nine
//           long keys 0 is a bare [0] keycap (the title says where it goes);
//   press   a digit never moves the legend: rows, cut and every item's spot stay;
//   motion  the ticks never draw over it: the waveform and the timeline keep to their room.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "rec_legend.h"
#include "rec_overlay.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "text_fit.h"
#include "theme.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() { set_lang(Lang::Zh); }  // a failed English test skips its LangScope

namespace {

// The user's projects (docs/20 U27: slot 1 acmeshop, slot 2 cardputer).
const std::initializer_list<ProjectRow> kTwo = {project(1, "acmeshop", "Acmeshop 宠物"),
                                                project(2, "cardputer", "Cardputer 助手")};
const std::initializer_list<ProjectRow> kFive = {
    project(1, "acmeshop", "Acmeshop 宠物"), project(2, "cardputer", "Cardputer 助手"),
    project(3, "garden", "Garden 花园CRM"), project(4, "reading-list", "读书清单笔记库"),
    project(5, "run-2026", "2026 跑步")};
// Nine keys at the protocol's longest (12 characters), CJK names (never shown).
const std::initializer_list<ProjectRow> kNine = {
    project(1, "acmeshop-app", "Acmeshop 宠物"),  project(2, "cardputer-ui", "Cardputer 助手"),
    project(3, "garden-crm-x", "Garden 花园CRM"), project(4, "reading-list", "读书清单笔记库"),
    project(5, "run-2026-10k", "2026 跑步"),      project(6, "albums-photo", "家庭相册整理清单"),
    project(7, "lesson-piano", "Piano 练习"),      project(8, "demo-video-1", "Product promo"),
    project(9, "thesis-ch3-x", "论文第三章")};

AppState state(std::initializer_list<ProjectRow> rows, uint8_t slot, RecMode mode = RecMode::Memo) {
  AppState s = demo_state();
  set_projects(s, rows);
  s.rec.avail = true;
  s.rec.phase = RecPhase::Recording;
  s.rec.mode = mode;
  s.rec.slot = slot;
  return s;
}

// What is drawn: the cell kept for "?" only when it is chosen.
std::string labels(const LegendPlan& p) {
  std::string out;
  for (uint8_t i = 0; i < p.n; ++i) {
    if (p.spots[i].ghost && !p.spots[i].chosen) continue;
    out += (out.empty() ? "" : " ") + std::to_string(p.spots[i].slot) + ":" + p.spots[i].label.c_str();
  }
  return out;
}

int16_t spot_w(const FakeCanvas& c, const LegendSpot& s) {
  if (s.label.size() == 0) return kLegendCapW;  // a bare [0]
  return static_cast<int16_t>(kLegendCapW + kLegendCapGap + c.textWidth(s.label.c_str()) + (s.cut ? kLegendCutMark : 0));
}

// Every item inside its row, on the screen, clear of the next one by at least the tight gap
// (the lit capsule reaches 3 px each side); none over the hint bar.
void assert_tidy(const FakeCanvas& c, const LegendPlan& p) {
  for (uint8_t i = 0; i < p.n; ++i) {
    const LegendSpot& s = p.spots[i];
    TEST_ASSERT_EQUAL_INT16_MESSAGE(spot_w(c, s), s.w, s.label.c_str());
    // the lit capsule (3 px each side) inside the hint bar's margins
    TEST_ASSERT_TRUE_MESSAGE(s.x - 3 >= 4 && s.x + s.w <= kScreenW - theme::kGutter, s.label.c_str());
    TEST_ASSERT_TRUE_MESSAGE(s.y + theme::kLiveH - 1 <= kHintRect.y, s.label.c_str());
    if (i + 1 < p.n && p.spots[i + 1].y == s.y) {
      TEST_ASSERT_TRUE_MESSAGE(p.spots[i + 1].x - (s.x + s.w) >= kLegendTightGap, s.label.c_str());
    }
  }
}

int rows_of(const LegendPlan& p) {
  int rows = 0;
  for (uint8_t i = 0; i < p.n; ++i) rows += i == 0 || p.spots[i].y != p.spots[i - 1].y ? 1 : 0;
  return rows;
}

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[2] = {&board, &inbox};
  Shell shell{port, mods, 2};
  uint32_t now = 0;
  Rig(std::initializer_list<ProjectRow> rows, uint8_t slot, RecMode mode) {
    port.app = state(rows, slot, mode);
    shell.begin(0);
    shell.rec_started();
  }
  FakeCanvas page() {
    FakeCanvas c;
    shell.render(c, now);
    return c;
  }
  AnimFrame tick(FakeCanvas& c) {
    shell.clear_dirty();  // a full frame went out
    now += 200;
    return shell.animate(c, now);
  }
};

const TextOp* text_at(const FakeCanvas& c, const char* text, int16_t y) {
  for (const TextOp& t : c.texts) {
    if (t.text == text && t.y == y) return &t;
  }
  return nullptr;
}

}  // namespace

// --- the list --------------------------------------------------------------------------

static void test_the_list_follows_the_number_row() {
  FakeCanvas c;
  AppState s = state({project(5, "run-2026", "跑步"), project(1, "acmeshop", "宠物"), project(2, "cardputer", "助手")}, 2);
  const LegendList l = legend_list(c, s.projects, 2);
  TEST_ASSERT_EQUAL_UINT8(5, l.n);
  const uint8_t slots[] = {1, 2, 5, 0, 3};
  const char* const keys[] = {"acmeshop", "cardputer", "run-2026", "不归属", "?"};
  for (uint8_t i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_UINT8(slots[i], l.items[i].slot);
    TEST_ASSERT_EQUAL_STRING(keys[i], l.items[i].label.c_str());  // keys, never the names
    TEST_ASSERT_EQUAL_INT16(c.textWidth(keys[i]), l.items[i].w);
    TEST_ASSERT_TRUE(l.items[i].chosen == (slots[i] == 2));
    TEST_ASSERT_TRUE(l.items[i].ghost == (i == 4));  // the cell kept for "?", last
  }
  const LegendList meeting = legend_list(c, s.projects, 0, false);  // no cell for "?"
  TEST_ASSERT_EQUAL_UINT8(4, meeting.n);
  TEST_ASSERT_EQUAL_UINT8(0, meeting.items[3].slot);
  const LegendList bare = legend_bare_none(meeting);
  TEST_ASSERT_EQUAL_UINT8(4, bare.n);
  TEST_ASSERT_EQUAL_STRING("", bare.items[3].label.c_str());  // a bare [0]
  TEST_ASSERT_EQUAL_INT16(0, bare.items[3].w);
  TEST_ASSERT_EQUAL_STRING("不归属", meeting.items[3].label.c_str());  // the list it came from is untouched
}

// 0 is 不归属, as the hint bar says; it is lit when nothing is chosen.
static void test_0_is_not_filed_and_lit_when_chosen() {
  FakeCanvas c;
  const LegendList l = legend_list(c, state(kTwo, 0).projects, 0);
  TEST_ASSERT_EQUAL_STRING(tr(kLegendNone), l.items[2].label.c_str());
  TEST_ASSERT_FALSE(l.items[3].chosen);  // the cell kept for "?" (slot 3, the first free)
  TEST_ASSERT_EQUAL_STRING("不归属", l.items[2].label.c_str());
  TEST_ASSERT_TRUE(l.items[2].chosen && !l.items[0].chosen && !l.items[1].chosen);
}

// A digit with no project behind it (docs/20 U13): the memo is tagged #9, the legend says
// "9 ?" lit, in the cell kept for it after 0 (review: a press never moves the legend).
static void test_a_chosen_digit_with_no_project_reads_a_question_mark() {
  FakeCanvas c;
  const LegendList l = legend_list(c, state(kTwo, 9).projects, 9);
  TEST_ASSERT_EQUAL_UINT8(4, l.n);
  TEST_ASSERT_EQUAL_UINT8(0, l.items[2].slot);
  TEST_ASSERT_EQUAL_UINT8(9, l.items[3].slot);
  TEST_ASSERT_EQUAL_STRING("?", l.items[3].label.c_str());
  TEST_ASSERT_TRUE(l.items[3].chosen && l.items[3].ghost);
  TEST_ASSERT_EQUAL_UINT8(4, legend_list(c, state(kTwo, 0).projects, 0).n);  // kept, not drawn
  TEST_ASSERT_EQUAL_UINT8(10, legend_list(c, state(kNine, 0).projects, 0).n);  // no free digit: none
  TEST_ASSERT_EQUAL_STRING("1:acmeshop 2:cardputer 0:不归属 9:?", labels(memo_legend(c, state(kTwo, 9)).plan).c_str());
  TEST_ASSERT_EQUAL_STRING("1:acmeshop 2:cardputer 0:不归属", labels(memo_legend(c, state(kTwo, 0)).plan).c_str());
}

// --- the fit ---------------------------------------------------------------------------

// Long keys are cut evenly, between letters, without "…" (the cut mark is two dots, drawn);
// a cut that ends on '-' drops it.
static void test_long_keys_are_cut_evenly_without_an_ellipsis() {
  FakeCanvas c;
  const LegendList l = legend_list(c, state(kNine, 1).projects, 1);
  const LegendRow rows[] = {{60, 80, 236}, {60, 94, 236}, {7, 108, 236}};
  const LegendFit f = legend_fit(l, rows, 3, kMemoLegendCut);
  TEST_ASSERT_EQUAL_INT16(36, f.cut);  // six letters each and the mark: 3 + 3 + 4 items
  TEST_ASSERT_EQUAL_INT16(kLegendGap, f.gap);
  const LegendPlan p = legend_plan(c, l, rows, 3, f);
  TEST_ASSERT_EQUAL_UINT8(10, p.n);
  TEST_ASSERT_EQUAL_STRING("1:acmesh 2:cardpu 3:garden 4:readin 5:run-20 6:albums 7:lesson 8:demo-v 9:thesis 0:不归属",
                           labels(p).c_str());
  for (uint8_t i = 0; i < p.n; ++i) {
    TEST_ASSERT_TRUE(std::strstr(p.spots[i].label.c_str(), "…") == nullptr);
    TEST_ASSERT_TRUE(p.spots[i].cut == (p.spots[i].slot != 0));  // 不归属 never
  }
  assert_tidy(c, p);
  // at four letters "run-" ends on '-': it is dropped
  const LegendFit four{24, kLegendGap};
  TEST_ASSERT_EQUAL_STRING("run", legend_plan(c, l, rows, 3, four).spots[4].label.c_str());
}

// CJK labels (a name, should a page ever show them) are cut between characters: a prefix,
// valid UTF-8, no wider than the cut.
static void test_cjk_labels_are_cut_between_characters() {
  FakeCanvas c;
  LegendList l;
  const char* const names[] = {"家庭相册整理清单", "论文第三章", "读书清单笔记库", "2026 跑步"};
  for (uint8_t i = 0; i < 4; ++i) {
    l.items[l.n].slot = static_cast<uint8_t>(i + 1);
    l.items[l.n].label = LegendLabel::from(names[i]);
    l.items[l.n].w = c.textWidth(names[i]);
    ++l.n;
  }
  const LegendRow row[] = {{7, 103, 236}};
  const LegendFit f = legend_fit(l, row, 1, kLegendMinCut);
  TEST_ASSERT_TRUE(f.cut >= kLegendMinCut);
  const LegendPlan p = legend_plan(c, l, row, 1, f);
  TEST_ASSERT_EQUAL_UINT8(4, p.n);
  for (uint8_t i = 0; i < p.n; ++i) {
    const char* cut = p.spots[i].label.c_str();
    const size_t n = std::strlen(cut);
    TEST_ASSERT_TRUE_MESSAGE(n > 0 && std::strncmp(cut, names[i], n) == 0, cut);
    for (size_t k = 0; k < n; k += utf8_at(cut, n, k).len) TEST_ASSERT_TRUE(utf8_at(cut, n, k).cp != 0xFFFD);
    TEST_ASSERT_TRUE_MESSAGE(c.textWidth(cut) <= f.cut, cut);
    TEST_ASSERT_TRUE(cut[n - 1] != ' ');
  }
  assert_tidy(c, p);
}

// 不归属 is never cut, and nothing fits under the minimum: cut 0.
static void test_not_filed_is_never_cut_and_too_little_room_is_no_fit() {
  FakeCanvas c;
  const LegendList l = legend_list(c, state(kNine, 0).projects, 0);
  const LegendRow tight[] = {{4, 103, 236}};
  TEST_ASSERT_EQUAL_INT16(0, legend_fit(l, tight, 1, kMemoLegendCut).cut);
  const LegendPlan p = legend_plan(c, l, tight, 1, LegendFit{});  // as many as fit at three letters, the tight gap
  TEST_ASSERT_TRUE(p.n > 0 && p.n < l.n);
  for (uint8_t i = 1; i < p.n; ++i) TEST_ASSERT_EQUAL_INT16(kLegendTightGap, p.spots[i].x - (p.spots[i - 1].x + p.spots[i - 1].w));
  TEST_ASSERT_EQUAL_STRING("acmesh", legend_plan(c, l, tight, 1, LegendFit{36, kLegendGap}).spots[0].label.c_str());
  const LegendRow wide[] = {{60, 80, 236}, {60, 94, 236}, {7, 108, 236}};
  const LegendPlan all = legend_plan(c, l, wide, 3, LegendFit{18, kLegendTightGap});
  TEST_ASSERT_EQUAL_STRING("不归属", all.spots[all.n - 1].label.c_str());
}

// --- the memo page ---------------------------------------------------------------------

// The user's two projects: one row under Mando's sign, nothing cut, 2 lit.
static void test_two_projects_take_one_row_under_the_sign() {
  FakeCanvas c;
  const AppState s = state(kTwo, 2);
  const PageLegend lg = memo_legend(c, s);
  TEST_ASSERT_EQUAL_UINT8(1, lg.rows);
  TEST_ASSERT_EQUAL_UINT8(1, memo_legend_rows(c, s));
  TEST_ASSERT_EQUAL_STRING("1:acmeshop 2:cardputer 0:不归属", labels(lg.plan).c_str());
  TEST_ASSERT_EQUAL_INT16(7, lg.plan.spots[0].x);  // the lit capsule from x 4, as the hint bar
  TEST_ASSERT_EQUAL_INT16(7 + 60 + kLegendGap, lg.plan.spots[1].x);
  TEST_ASSERT_EQUAL_INT16(7 + 60 + 66 + 2 * kLegendGap, lg.plan.spots[2].x);
  TEST_ASSERT_FALSE(lg.chosen_cut);
  for (uint8_t i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_INT16(103, lg.plan.spots[i].y);
  TEST_ASSERT_TRUE(lg.plan.spots[1].chosen && !lg.plan.spots[0].chosen && !lg.plan.spots[2].chosen);
  assert_tidy(c, lg.plan);
}

// No projects (none registered, or the Mac never said): 0 alone, lit; one project, one row.
static void test_no_projects_and_one_project() {
  FakeCanvas c;
  const PageLegend none = memo_legend(c, state({}, 0));
  TEST_ASSERT_EQUAL_UINT8(1, none.rows);
  TEST_ASSERT_EQUAL_STRING("0:不归属", labels(none.plan).c_str());
  TEST_ASSERT_TRUE(none.plan.spots[0].chosen && none.plan.spots[0].x == 7 && none.plan.spots[0].y == 103);
  const PageLegend one = memo_legend(c, state({project(1, "reading-list", "读书清单笔记库")}, 1));
  TEST_ASSERT_EQUAL_UINT8(1, one.rows);
  TEST_ASSERT_EQUAL_STRING("1:reading-list 0:不归属", labels(one.plan).c_str());
  TEST_ASSERT_TRUE(one.plan.spots[0].chosen);
}

// Knowing which project is which outranks the waveform's height (the review's choice): the
// fewest rows where no key is cut. Three 12-letter keys: two rows, whole (one row would cut
// them to seven letters).
static void test_three_long_keys_take_two_rows_whole() {
  FakeCanvas c;
  const AppState s = state({project(1, "acmeshop-app", "宠物"), project(2, "cardputer-ui", "助手"),
                            project(3, "reading-list", "笔记")},
                           1);
  const PageLegend lg = memo_legend(c, s);
  TEST_ASSERT_EQUAL_UINT8(2, lg.rows);
  TEST_ASSERT_EQUAL_STRING("1:acmeshop-app 2:cardputer-ui 3:reading-list 0:不归属", labels(lg.plan).c_str());
  TEST_ASSERT_FALSE(rec_room(c, s).low_wave);
  assert_tidy(c, lg.plan);
}

// Five projects: three rows (the waveform 2/3 as tall), every key whole; the top rows right
// of Mando.
static void test_five_projects_take_three_rows_whole() {
  FakeCanvas c;
  const PageLegend lg = memo_legend(c, state(kFive, 4));
  TEST_ASSERT_EQUAL_UINT8(3, lg.rows);
  TEST_ASSERT_EQUAL_INT(3, rows_of(lg.plan));
  TEST_ASSERT_EQUAL_STRING("1:acmeshop 2:cardputer 3:garden 4:reading-list 5:run-2026 0:不归属", labels(lg.plan).c_str());
  TEST_ASSERT_TRUE(lg.plan.spots[0].x == 60 && lg.plan.spots[0].y == 80);
  for (uint8_t i = 0; i < lg.plan.n; ++i) {
    const LegendSpot& sp = lg.plan.spots[i];
    TEST_ASSERT_TRUE_MESSAGE(sp.y == 108 || sp.x >= 60, sp.label.c_str());  // the top rows clear Mando's sign
    TEST_ASSERT_FALSE(sp.cut);
  }
  TEST_ASSERT_TRUE(lg.plan.spots[3].chosen);  // reading-list, slot 4
  assert_tidy(c, lg.plan);
}

// Nine projects with 12-letter keys: three rows, seven letters each.
static void test_nine_projects_take_three_rows() {
  FakeCanvas c;
  const AppState s = state(kNine, 9);
  const PageLegend lg = memo_legend(c, s);
  TEST_ASSERT_EQUAL_UINT8(3, lg.rows);
  TEST_ASSERT_EQUAL_UINT8(3, memo_legend_rows(c, s));
  TEST_ASSERT_EQUAL_UINT8(10, lg.plan.n);
  TEST_ASSERT_TRUE(lg.plan.spots[0].x == 60 && lg.plan.spots[0].y == 80);
  TEST_ASSERT_TRUE(lg.plan.spots[8].chosen && lg.plan.spots[8].slot == 9);
  TEST_ASSERT_TRUE(lg.chosen_cut);  // the title says the whole key
  assert_tidy(c, lg.plan);
}

// Through the shell: the page draws the legend (keycaps and keys), the chosen one lit as the
// live-words capsule; "存到" and "#key" are gone from the memo page.
static void test_the_memo_page_lights_the_chosen_project() {
  Rig r(kTwo, 2, RecMode::Memo);
  FakeCanvas c = r.page();
  const TextOp* lit = text_at(c, "cardputer", 103);
  const TextOp* other = text_at(c, "acmeshop", 103);
  TEST_ASSERT_NOT_NULL_MESSAGE(lit, c.dump().c_str());
  TEST_ASSERT_NOT_NULL(other);
  TEST_ASSERT_TRUE(lit->style.fg == Color::Accent && other->style.fg == Color::Ink2);
  TEST_ASSERT_TRUE(c.filled_at(103, Color::Select) && c.filled_at(103, Color::Live));  // the capsule
  TEST_ASSERT_TRUE(text_at(c, "2", 103) != nullptr && text_at(c, "2", 103)->style.fg == Color::OnAccent);
  TEST_ASSERT_TRUE(text_at(c, "1", 103) != nullptr && text_at(c, "0", 103) != nullptr);
  TEST_ASSERT_TRUE(text_at(c, "1", 103)->style.fg == Color::Ink);  // the digit, what you match, the brightest
  TEST_ASSERT_NULL(text_at(c, "?", 103));                           // the kept cell, empty
  TEST_ASSERT_TRUE(c.has("语音便条"));                               // nothing cut: the title as it was
  TEST_ASSERT_TRUE(text_at(c, "不归属", 103) != nullptr);
  TEST_ASSERT_FALSE(c.has("存到") || c.has("#cardputer"));
  TEST_ASSERT_TRUE(lit->x + c.textWidth("cardputer") <= kScreenW - theme::kGutter);
  r.port.app.rec.slot = 1;  // a digit pressed: the next full frame lights it at once
  FakeCanvas d = r.page();
  TEST_ASSERT_TRUE(text_at(d, "acmeshop", 103)->style.fg == Color::Accent);
  TEST_ASSERT_TRUE(text_at(d, "cardputer", 103)->style.fg == Color::Ink2);
  r.port.app.rec.slot = 7;  // no project: "7 ?" lit in the kept cell, nothing else moves
  FakeCanvas e = r.page();
  TEST_ASSERT_NOT_NULL(text_at(e, "?", 103));
  TEST_ASSERT_TRUE(text_at(e, "?", 103)->style.fg == Color::Accent && text_at(e, "7", 103)->style.fg == Color::OnAccent);
  TEST_ASSERT_EQUAL_INT16(text_at(d, "cardputer", 103)->x, text_at(e, "cardputer", 103)->x);
  TEST_ASSERT_EQUAL_INT16(text_at(d, "不归属", 103)->x, text_at(e, "不归属", 103)->x);
}

// The grace, hands free, the cue and saving keep it; the held card covers the page.
static void test_every_memo_state_keeps_the_legend() {
  Rig r(kTwo, 1, RecMode::Memo);
  r.port.app.rec.grace = true;
  TEST_ASSERT_NOT_NULL(text_at(r.page(), "acmeshop", 103));
  r.port.app.rec.grace = false;
  r.port.app.rec.latched = true;
  TEST_ASSERT_NOT_NULL(text_at(r.page(), "acmeshop", 103));
  r.port.app.rec.latched = false;
  r.port.app.rec.phase = RecPhase::Cue;
  TEST_ASSERT_NOT_NULL(text_at(r.page(), "acmeshop", 103));
  r.port.app.rec.phase = RecPhase::Finishing;
  TEST_ASSERT_NOT_NULL(text_at(r.page(), "acmeshop", 103));
  r.port.app.rec.phase = RecPhase::Recording;
  r.port.app.rec.held = true;  // docs/20 U15: the memo ended under a held G0
  TEST_ASSERT_NULL(text_at(r.page(), "acmeshop", 103));
}

// The waveform's bars on a page or a tick: 3 px wide, y from..to.
int bars_within(const FakeCanvas& c, int16_t top, int16_t bottom) {
  int n = 0;
  for (const auto& f : c.fills) n += f.first.w == 3 && f.first.y >= top && f.first.y + f.first.h - 1 <= bottom ? 1 : 0;
  return n;
}

// Three rows: the waveform keeps its 35 bars, 2/3 as tall, clear of the top row.
static void test_three_rows_lower_the_waveform() {
  Rig r(kNine, 1, RecMode::Memo);
  for (int i = 0; i < 40; ++i) r.shell.on_level(3000);
  FakeCanvas t;
  r.tick(t);
  TEST_ASSERT_EQUAL_INT(35, bars_within(t, 63, 77));
  FakeCanvas p = r.page();
  TEST_ASSERT_EQUAL_INT(35, bars_within(p, 63, 77));
  TEST_ASSERT_NOT_NULL_MESSAGE(text_at(p, "acmesh", 80), p.dump().c_str());
  TEST_ASSERT_TRUE(rec_room(p, r.port.app).low_wave);
  TEST_ASSERT_TRUE(rec_room(p, state(kFive, 1)).low_wave);
  TEST_ASSERT_FALSE(rec_room(p, state(kTwo, 1)).low_wave);
}

// --- the meeting page ------------------------------------------------------------------

static void test_the_meeting_keeps_one_row_under_the_timer() {
  FakeCanvas c;
  const PageLegend lg = meeting_legend(c, state(kTwo, 1, RecMode::Meeting));
  TEST_ASSERT_EQUAL_UINT8(1, lg.rows);
  TEST_ASSERT_EQUAL_STRING("1:acmeshop 2:cardputer 0:不归属", labels(lg.plan).c_str());  // whole, at the tight gap
  TEST_ASSERT_TRUE(lg.plan.spots[0].x == 48 && lg.plan.spots[0].y == 59);
  assert_tidy(c, lg.plan);
  Rig r(kTwo, 1, RecMode::Meeting);
  FakeCanvas p = r.page();
  TEST_ASSERT_TRUE(text_at(p, "acmeshop", 59) != nullptr && text_at(p, "acmeshop", 59)->style.fg == Color::Accent);
  TEST_ASSERT_EQUAL_STRING("0:00 现在", p.line(109).c_str());  // the timeline keeps its labels
  TEST_ASSERT_TRUE(p.has("#acmeshop"));                        // the title still says where it goes
  TEST_ASSERT_FALSE(rec_room(p, r.port.app).no_labels);
  // a digit with no project: the title says "#8", the legend keeps no cell for it (it would
  // cut the user's keys to five letters) and lights none
  r.port.app.rec.slot = 8;
  FakeCanvas q = r.page();
  TEST_ASSERT_TRUE(q.has("#8") && !q.has("?"));
  TEST_ASSERT_EQUAL_INT16(text_at(p, "cardputer", 59)->x, text_at(q, "cardputer", 59)->x);
}

// Five: the labels' row as well; 0:00 and 现在 give way. Nine long keys: 0 a bare [0] (the
// title says 未归属), still lit when chosen; three letters each.
static void test_more_projects_take_the_labels_row() {
  FakeCanvas c;
  const PageLegend five = meeting_legend(c, state(kFive, 1, RecMode::Meeting));
  TEST_ASSERT_EQUAL_UINT8(2, five.rows);
  TEST_ASSERT_EQUAL_UINT8(6, five.plan.n);
  TEST_ASSERT_EQUAL_INT16(109, five.plan.spots[five.plan.n - 1].y);  // 1 px under the loudest bar, 1 over the hint bar
  assert_tidy(c, five.plan);
  const AppState s = state(kNine, 0, RecMode::Meeting);
  const PageLegend nine = meeting_legend(c, s);
  TEST_ASSERT_EQUAL_UINT8(2, nine.rows);
  TEST_ASSERT_EQUAL_UINT8(2, meeting_legend_rows(c, s));
  TEST_ASSERT_EQUAL_UINT8(10, nine.plan.n);
  TEST_ASSERT_EQUAL_UINT8(9, nine.plan.spots[8].slot);
  TEST_ASSERT_TRUE(nine.plan.spots[9].slot == 0 && nine.plan.spots[9].chosen);  // a bare [0], lit
  TEST_ASSERT_EQUAL_STRING("", nine.plan.spots[9].label.c_str());
  for (uint8_t i = 0; i < 9; ++i) {
    TEST_ASSERT_TRUE(nine.plan.spots[i].x >= (nine.plan.spots[i].y == 59 ? 48 : 7));
    // the weakest layout: three letters and the mark ("cc-" drops its '-')
    TEST_ASSERT_TRUE(c.textWidth(nine.plan.spots[i].label.c_str()) >= 12 && nine.plan.spots[i].cut);
  }
  assert_tidy(c, nine.plan);
  Rig r(kNine, 0, RecMode::Meeting);
  FakeCanvas p = r.page();
  TEST_ASSERT_TRUE(text_at(p, "0:00", 109) == nullptr && !p.has("现在"));
  TEST_ASSERT_TRUE(p.has("未归属"));  // the title
  TEST_ASSERT_TRUE(p.filled_at(109, Color::Select));  // and the lit [0]
  TEST_ASSERT_TRUE(rec_room(p, r.port.app).no_labels);
}

// The end confirm covers the page, and before the microphone is up there is no timeline:
// no legend either.
static void test_the_meeting_boxes_and_cue_have_no_legend() {
  Rig r(kTwo, 0, RecMode::Meeting);
  r.port.app.rec.phase = RecPhase::EndConfirm;
  TEST_ASSERT_FALSE(r.page().has("不归属"));
  r.port.app.rec.phase = RecPhase::Cue;
  TEST_ASSERT_FALSE(r.page().has("不归属"));
  r.port.app.rec.phase = RecPhase::Finishing;  // as it ended
  TEST_ASSERT_TRUE(r.page().has("不归属"));
}

// --- motion ----------------------------------------------------------------------------

bool inside(const Rect& r, int16_t top, int16_t bottom) { return r.y >= top && r.y + r.h - 1 <= bottom; }

// A memo tick under three rows pushes the low waveform only; nothing it draws reaches the
// legend's top row (y 79 on, its capsule).
static void test_a_memo_tick_leaves_the_legend_alone() {
  Rig r(kNine, 2, RecMode::Memo);
  for (int i = 0; i < 6; ++i) r.shell.on_level(4000);
  FakeCanvas t;
  const AnimFrame f = r.tick(t);
  bool wave = false;
  for (uint8_t i = 0; i < f.n; ++i) {
    wave = wave || (f.rects[i].x == 62 && f.rects[i].w == 173);
    if (f.rects[i].x == 62) TEST_ASSERT_TRUE(inside(f.rects[i], 63, 77));
  }
  TEST_ASSERT_TRUE(wave);
  for (const auto& fl : t.fills) TEST_ASSERT_TRUE_MESSAGE(fl.first.y + fl.first.h - 1 < 79 || fl.first.x + fl.first.w <= 46, "tick over the legend");
  TEST_ASSERT_TRUE(t.texts.empty());
}

// A meeting tick under two rows: after a merge it redraws the bars and the stars only (not
// the labels' row, where the legend is), and never draws 0:00 or 现在.
static void test_a_meeting_tick_leaves_the_legend_alone() {
  Rig r(kNine, 0, RecMode::Meeting);
  FakeCanvas first;
  for (int i = 0; i < 30; ++i) r.shell.on_level(3000);
  r.tick(first);
  for (int i = 0; i < 70; ++i) r.shell.on_level(3000);  // past the first merge (94 blocks)
  FakeCanvas t;
  const AnimFrame f = r.tick(t);
  TEST_ASSERT_TRUE(f.n >= 1);
  for (uint8_t i = 0; i < f.n; ++i) TEST_ASSERT_TRUE_MESSAGE(f.rects[i].y + f.rects[i].h - 1 <= 106, "tick into the labels' row");
  for (const auto& fl : t.fills) TEST_ASSERT_TRUE(fl.first.y + fl.first.h - 1 <= 106);
  TEST_ASSERT_FALSE(t.has("0:00") || t.has("现在"));
  r.shell.on_level(3000);
  r.shell.on_level(3000);  // one more cell: the dot moves (现在 would have)
  FakeCanvas u;
  const AnimFrame g = r.tick(u);
  for (uint8_t i = 0; i < g.n; ++i) TEST_ASSERT_TRUE(g.rects[i].y + g.rects[i].h - 1 <= 106);
  TEST_ASSERT_FALSE(u.has("现在"));
}

// Two projects: the meeting tick is as before, 现在 moves with the dot.
static void test_with_one_row_the_timeline_labels_still_move() {
  Rig r(kTwo, 0, RecMode::Meeting);
  for (int i = 0; i < 10; ++i) r.shell.on_level(3000);
  FakeCanvas a;
  r.tick(a);
  r.shell.on_level(3000);
  FakeCanvas b;
  r.tick(b);
  TEST_ASSERT_TRUE(b.has("现在"));
}

// The review of docs/21's English renders: with the dot at its rightmost, "now" (wider than
// 现在's end under the rim allows) still ends 2 px short of the screen's edge.
static void test_the_now_label_stays_off_the_edge() {
  for (const Lang l : kLangs) {
    LangScope in(l);
    const char* now = tr(Txt{"现在", "now"});
    Rig r(kTwo, 0, RecMode::Meeting);
    int16_t right = 0;
    for (int i = 0; i < 94; ++i) {  // to the first merge: the dot at its rightmost
      r.shell.on_level(3000);
      FakeCanvas ticked;
      r.tick(ticked);  // the page draws what the last tick showed
      FakeCanvas p = r.page();
      const TextOp* t = text_at(p, now, 109);
      TEST_ASSERT_NOT_NULL_MESSAGE(t, p.dump().c_str());
      const int16_t end = static_cast<int16_t>(t->x + p.textWidth(now));
      right = end > right ? end : right;
    }
    TEST_ASSERT_EQUAL_INT16(kScreenW - 2, right);
  }
}

// --- a press ---------------------------------------------------------------------------

// The first n of a list of keys.
AppState first_n(const std::initializer_list<ProjectRow>& rows, uint8_t n, uint8_t slot, RecMode mode) {
  AppState s = state({}, slot, mode);
  for (const ProjectRow& r : rows) {
    if (s.projects.data.n == n) break;
    s.projects.data.rows[s.projects.data.n++] = r;
  }
  return s;
}

void assert_same_place(const PageLegend& a, const PageLegend& b, const char* what) {
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(a.rows, b.rows, what);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(a.plan.n, b.plan.n, what);
  for (uint8_t i = 0; i < a.plan.n; ++i) {
    const LegendSpot& x = a.plan.spots[i];
    const LegendSpot& y = b.plan.spots[i];
    TEST_ASSERT_TRUE_MESSAGE(x.x == y.x && x.y == y.y && x.w == y.w && x.ghost == y.ghost, what);
    if (!x.ghost) TEST_ASSERT_TRUE_MESSAGE(x.slot == y.slot && x.label == y.label, what);
  }
}

// The review's probe: six projects, 8 pressed, the legend went from two rows to three and the
// meeting's keys from seven letters to five. Now, for 0-9 projects and every digit, a press
// changes what is lit and nothing else: the rows (so the waveform and 0:00 / 现在), the cut
// and every item's spot.
static void test_a_press_never_moves_the_legend() {
  FakeCanvas c;
  char what[48];
  for (const std::initializer_list<ProjectRow>* rows : {&kNine, &kFive}) {
    for (uint8_t n = 0; n <= rows->size(); ++n) {
      for (RecMode mode : {RecMode::Memo, RecMode::Meeting}) {
        const auto page = [&](uint8_t slot) {
          const AppState s = first_n(*rows, n, slot, mode);
          return mode == RecMode::Memo ? memo_legend(c, s) : meeting_legend(c, s);
        };
        const PageLegend base = page(0);
        for (uint8_t slot = 1; slot <= 9; ++slot) {
          std::snprintf(what, sizeof what, "%u projects, %s, %u pressed", n, mode == RecMode::Memo ? "memo" : "meeting", slot);
          assert_same_place(base, page(slot), what);
        }
      }
    }
  }
}

// --- the cut mark and the title --------------------------------------------------------

// A cut label ends in two dots on its baseline, 3 px after it (two fills); a whole one in none.
static void test_a_cut_label_ends_in_two_dots() {
  FakeCanvas c;
  const PageLegend lg = memo_legend(c, state(kNine, 0));
  draw_legend(c, lg.plan);
  const LegendSpot& s = lg.plan.spots[0];
  TEST_ASSERT_TRUE(s.cut);
  const int16_t end = static_cast<int16_t>(s.x + kLegendCapW + kLegendCapGap + c.textWidth(s.label.c_str()));
  int dots = 0;
  for (const auto& f : c.fills) {
    const bool dot = f.first.w == 1 && f.first.h == 1 && f.first.y == s.y + kLegendMarkDy;
    if (dot && (f.first.x == end || f.first.x == end + 2)) ++dots;
  }
  TEST_ASSERT_EQUAL_INT(2, dots);
  TEST_ASSERT_EQUAL_INT16(end + kLegendCutMark - s.x, s.w);
  FakeCanvas d;
  draw_legend(d, memo_legend(d, state(kTwo, 0)).plan);
  for (const auto& f : d.fills) TEST_ASSERT_FALSE(f.first.w == 1 && f.first.h == 1);
}

// The chosen key cut: the memo's title row says it whole, "存到 #key", in place of 语音便条;
// the grace and hands free keep their banners there.
static void test_the_title_says_a_cut_choice_whole() {
  Rig r(kNine, 4, RecMode::Memo);
  FakeCanvas p = r.page();
  TEST_ASSERT_NOT_NULL_MESSAGE(text_at(p, "#reading-list", 18), p.dump().c_str());
  TEST_ASSERT_TRUE(p.has("存到") && !p.has("语音便条"));
  r.port.app.rec.latched = true;
  FakeCanvas q = r.page();
  TEST_ASSERT_TRUE(q.has("免提录音 · 按 G0 结束") && !q.has("#reading-list"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_list_follows_the_number_row);
  RUN_TEST(test_0_is_not_filed_and_lit_when_chosen);
  RUN_TEST(test_a_chosen_digit_with_no_project_reads_a_question_mark);
  RUN_TEST(test_long_keys_are_cut_evenly_without_an_ellipsis);
  RUN_TEST(test_cjk_labels_are_cut_between_characters);
  RUN_TEST(test_not_filed_is_never_cut_and_too_little_room_is_no_fit);
  RUN_TEST(test_two_projects_take_one_row_under_the_sign);
  RUN_TEST(test_no_projects_and_one_project);
  RUN_TEST(test_three_long_keys_take_two_rows_whole);
  RUN_TEST(test_five_projects_take_three_rows_whole);
  RUN_TEST(test_nine_projects_take_three_rows);
  RUN_TEST(test_the_memo_page_lights_the_chosen_project);
  RUN_TEST(test_every_memo_state_keeps_the_legend);
  RUN_TEST(test_three_rows_lower_the_waveform);
  RUN_TEST(test_the_meeting_keeps_one_row_under_the_timer);
  RUN_TEST(test_more_projects_take_the_labels_row);
  RUN_TEST(test_the_meeting_boxes_and_cue_have_no_legend);
  RUN_TEST(test_a_memo_tick_leaves_the_legend_alone);
  RUN_TEST(test_a_meeting_tick_leaves_the_legend_alone);
  RUN_TEST(test_with_one_row_the_timeline_labels_still_move);
  RUN_TEST(test_the_now_label_stays_off_the_edge);
  RUN_TEST(test_a_press_never_moves_the_legend);
  RUN_TEST(test_a_cut_label_ends_in_two_dots);
  RUN_TEST(test_the_title_says_a_cut_choice_whole);
  return UNITY_END();
}
