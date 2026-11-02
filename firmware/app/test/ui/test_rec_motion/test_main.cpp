// The recording pages in motion (docs/19 §6.6-§6.7, §7.3, §7.5; mockups 04-05): the memo's
// waveform of the last 35 blocks, newest on the right, and Mando nodding through the words of
// one of the user's memos; the meeting's timeline of the whole
// meeting with its stars, the newest bars in Accent and a Rec dot at its end; both drawn from
// the level history as the last tick took it. A tick redraws Mando's box, the waveform or
// (when it grew) the timeline, and steps the big ●'s breath; the end confirm covers the
// timeline, and once it is saving the ● stands still at full Rec.
#include <unity.h>

#include <set>
#include <vector>

#include "core/support/lang_pin.h"
#include "core/support/memo_levels.h"
#include "rec_overlay.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

namespace {

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[2] = {&board, &inbox};
  Shell shell{port, mods, 2};
  uint32_t now = 0;
  Rig(RecMode mode) {
    port.app = demo_state();
    port.app.rec.avail = true;
    port.app.rec.phase = RecPhase::Recording;
    port.app.rec.mode = mode;
    port.app.rec.seg = 2;
    shell.begin(0);
    shell.rec_started();
  }
  void levels(uint16_t rms, int n) {
    for (int i = 0; i < n; ++i) shell.on_level(rms);
  }
  AnimFrame tick(FakeCanvas& c) {
    shell.clear_dirty();  // a full frame went out
    now += 200;
    return shell.animate(c, now);
  }
  FakeCanvas page() {
    FakeCanvas c;
    shell.render(c, now);
    return c;
  }
};

bool same(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

bool has_rect(const AnimFrame& f, const Rect& r) {
  for (uint8_t i = 0; i < f.n; ++i) {
    if (same(f.rects[i], r)) return true;
  }
  return false;
}

// The waveform's bars: fills 3 px wide in its band, left to right.
std::vector<Rect> bars(const FakeCanvas& c) {
  std::vector<Rect> out;
  for (const auto& f : c.fills) {
    if (f.first.w == 3 && f.first.y >= 63 && f.first.y + f.first.h <= 88) out.push_back(f.first);
  }
  return out;
}

// The timeline's bars: fills 1-4 px wide round y 98, their colours.
std::vector<std::pair<Rect, Color>> cells(const FakeCanvas& c) {
  std::vector<std::pair<Rect, Color>> out;
  for (const auto& f : c.fills) {
    if (f.first.w <= 4 && f.first.h > 1 && f.first.y >= 90 && f.first.y + f.first.h <= 107) out.push_back(f);
  }
  return out;
}

constexpr Rect kWave{62, 64, 173, 23};
constexpr Rect kLine{46, 73, 194, 49};  // the bars, the stars and the labels

}  // namespace

static void test_the_waveform_is_the_last_35_blocks() {
  Rig r(RecMode::Memo);
  r.levels(454, 3);
  TEST_ASSERT_EQUAL_UINT(35, bars(r.page()).size());
  for (const Rect& b : bars(r.page())) TEST_ASSERT_EQUAL_INT16(3, b.h);  // nothing shown before a tick
  FakeCanvas c;
  const AnimFrame f = r.tick(c);
  TEST_ASSERT_TRUE(has_rect(f, kWave));
  const std::vector<Rect> drawn = bars(c);
  TEST_ASSERT_EQUAL_UINT(35, drawn.size());
  TEST_ASSERT_EQUAL_INT16(62 + 5 * 34, drawn.back().x);   // newest on the right
  TEST_ASSERT_EQUAL_INT16(2 * 7 + 1, drawn.back().h);     // level_half(454) = 7: a word at -37 dBFS
  TEST_ASSERT_EQUAL_INT16(3, drawn[32].h);                 // the first block: the codec settling
  TEST_ASSERT_EQUAL_INT16(3, drawn[31].h);                 // not heard yet: a quiet dot
  TEST_ASSERT_TRUE(c.fills.back().first.w == 1);           // the chipped ends
  const FakeCanvas p = r.page();
  TEST_ASSERT_EQUAL_INT16(15, bars(p).back().h);           // the page draws what the tick took
}

static void test_a_memo_tick_redraws_mando_wave_and_dot() {
  Rig r(RecMode::Memo);
  FakeCanvas c;
  r.levels(3000, 2);
  AnimFrame f = r.tick(c);
  TEST_ASSERT_TRUE(has_rect(f, Rect{10, 19, 36, 64}));  // he nods: Listen's first tick, his sprite
  TEST_ASSERT_TRUE(has_rect(f, Rect{60, 38, 12, 12}));
  TEST_ASSERT_EQUAL_INT8(1, f.breath);
  f = r.tick(c);
  TEST_ASSERT_FALSE(has_rect(f, kWave));  // no new block: nothing to scroll
  TEST_ASSERT_EQUAL_INT8(2, f.breath);
  TEST_ASSERT_TRUE(f.n <= 3);
}

// The act of the 64 px Mando a page draws: Listen (he nods), Tilt (his head tilted), else Idle.
theme::MandoAct memo_act(const FakeCanvas& c) {
  for (const ArtOp& a : c.arts) {
    for (uint8_t p = 0; p < theme::mando_poses(theme::MandoAct::Listen); ++p) {
      if (a.id == theme::mando_art(theme::MandoSize::M64, theme::MandoAct::Listen, p)) return theme::MandoAct::Listen;
      if (a.id == theme::mando_art(theme::MandoSize::M64, theme::MandoAct::Tilt, p)) return theme::MandoAct::Tilt;
    }
  }
  return theme::MandoAct::Idle;
}

// docs/19 §6.6, §7.1 (gh: the bar did not move with the user's voice): one of the user's memos,
// a block every 128 ms and a tick every 200 ms. Through the words Mando nods and the newest
// bar is a mid-to-high one, a different height from tick to tick; in the pauses (0.8 s of the
// room) he tilts his head and the newest bar is a dot.
static void test_a_real_memo_moves_the_wave_and_mando() {
  Rig r(RecMode::Memo);
  const memo_levels::Memo& m = memo_levels::kMemos[2];
  uint32_t at = 0;
  int words = 0, nods = 0, rooms = 0, tilts = 0;
  std::set<int16_t> heights;
  for (uint16_t i = 0; i < m.n; ++i) {
    r.levels(m.level[i], 1);  // the high-passed level; words and room by the RMS as recorded
    at += 128;
    if (r.now + 200 > at) continue;
    FakeCanvas c;
    r.tick(c);
    const FakeCanvas p = r.page();
    const int16_t newest = bars(p).back().h;
    if (i > 1 && m.rms[i] >= memo_levels::kSpeech && m.rms[i - 1] >= memo_levels::kSpeech) {
      ++words;
      nods += memo_act(p) == theme::MandoAct::Listen ? 1 : 0;
      heights.insert(newest);
      TEST_ASSERT_TRUE(newest >= 2 * 4 + 1);
    }
    if (m.rms[i] < memo_levels::kRoom && m.rms[i - 1] < memo_levels::kRoom) {
      ++rooms;
      tilts += memo_act(p) == theme::MandoAct::Tilt ? 1 : 0;
      TEST_ASSERT_EQUAL_INT16(3, newest);
    }
  }
  TEST_ASSERT_TRUE(words > 30 && rooms >= 4);
  TEST_ASSERT_EQUAL(words, nods);
  TEST_ASSERT_EQUAL(rooms, tilts);
  TEST_ASSERT_TRUE(heights.size() >= 4);  // it moves up and down
  TEST_ASSERT_TRUE(*heights.rbegin() >= 2 * 8 + 1);
  TEST_ASSERT_TRUE(*heights.rbegin() < 23);  // a louder voice still has room
}

// The bars' right end, and the end dot's centre.
int16_t right_end(const std::vector<std::pair<Rect, Color>>& cs) { return cs.back().first.x + cs.back().first.w - 1; }

bool end_dot_at(const FakeCanvas& c, int16_t cx) {
  for (const DotOp& d : c.dots) {
    if (d.cx == cx && d.cy == 98 && d.diameter == 5 && d.paint == Color::Rec) return true;
  }
  return false;
}

// The bars' band round y 98 (the bars, the base line and the end dot), and the stars' band
// above it down to the bars' bottom.
constexpr int16_t kBarsTop = 90;
constexpr int16_t kBarsH = 17;
constexpr Rect kStarsAndBars{46, 73, 194, 34};

// mockup 05, the judges' notes: before it is full a bar every 2 px from x 48, and the ● and
// "now" right after the newest bar, no track beyond it; the segment is in the title row.
static void test_the_timeline_spans_the_meeting_with_its_stars() {
  Rig r(RecMode::Meeting);
  r.levels(500, 20);
  r.port.app.rec.stars = 1;
  r.levels(4096, 10);  // the star sits on block 20
  FakeCanvas c;
  const AnimFrame f = r.tick(c);
  TEST_ASSERT_TRUE(f.n >= 1);
  const auto cs = cells(c);
  TEST_ASSERT_EQUAL_UINT(30, cs.size());
  TEST_ASSERT_EQUAL_INT16(48, cs.front().first.x);
  TEST_ASSERT_EQUAL_INT16(48 + 2 * 29, cs.back().first.x);
  TEST_ASSERT_TRUE(cs.front().second == Color::Live && cs.back().second == Color::Accent);
  TEST_ASSERT_TRUE(cs[22].second == Color::Live && cs[23].second == Color::Accent);  // the newest 7
  const TextOp* star = c.find("★");
  TEST_ASSERT_NOT_NULL(star);
  TEST_ASSERT_TRUE(star->x == 48 + 2 * 20 - 2 && star->y == 73 && star->style.fg == Color::Warn);
  TEST_ASSERT_TRUE(end_dot_at(c, 48 + 2 * 29 + 5));  // 2 px after the newest bar
  bool track = false;
  for (const auto& fl : c.fills) {
    if (fl.first.h == 1 && fl.first.y == 98 && fl.second == Color::Live) track = fl.first.x == 48 && fl.first.x + fl.first.w <= 48 + 2 * 29 + 5;
  }
  TEST_ASSERT_TRUE(track);  // the base line stops at the ●
  const FakeCanvas p = r.page();
  TEST_ASSERT_EQUAL_STRING("0:00 现在", p.line(109).c_str());
  const TextOp* now_label = p.find("现在");
  TEST_ASSERT_EQUAL_INT16(48 + 2 * 29 + 5 + 3, now_label->x + p.textWidth("现在"));  // under the ●
  TEST_ASSERT_TRUE(p.line(15).find("第 3 段") != std::string::npos);  // "Meeting #cpa · Part 3"
  FakeCanvas again;
  const AnimFrame same = r.tick(again);  // nothing new: Mando nods and the ● breathes, that is all
  TEST_ASSERT_EQUAL_UINT8(2, same.n);
  TEST_ASSERT_TRUE(has_rect(same, Rect{46, 38, 12, 12}) && has_rect(same, Rect{10, 31, 26, 46}));
  r.levels(4096, 1);  // one more cell: the bars that changed colour to the moved dot, and "now"
  FakeCanvas one;
  const AnimFrame g = r.tick(one);
  const int16_t from = static_cast<int16_t>(48 + 2 * 23 - 1);  // the 7th newest bar, less a pixel
  const int16_t dot = static_cast<int16_t>(48 + 2 * 30 + 5);
  TEST_ASSERT_TRUE(has_rect(g, Rect{from, kBarsTop, static_cast<int16_t>(dot + 3 - from), kBarsH}));
  TEST_ASSERT_TRUE(has_rect(g, Rect{static_cast<int16_t>(dot - 2 + 3 - 24), 109, 24 + 2, 12}));  // "now" moved 2 px
  TEST_ASSERT_EQUAL_UINT(31, cells(one).size());  // the line drawn whole over it
  TEST_ASSERT_TRUE(end_dot_at(one, 48 + 2 * 30 + 5) && one.has("现在"));
}

// Once full, the cells merge (theme_levels) and spread over the whole 188 px: no flat track
// to a ● pinned at the end; the bars are pitch - 1 wide.
static void test_a_full_timeline_spans_the_width() {
  Rig r(RecMode::Meeting);
  r.levels(3000, theme::kLineCells + 1);  // the 95th block merges: 48 cells of 2
  FakeCanvas c;
  r.tick(c);
  const auto cs = cells(c);
  TEST_ASSERT_EQUAL_UINT(48, cs.size());
  TEST_ASSERT_EQUAL_INT16(48, cs.front().first.x);
  TEST_ASSERT_EQUAL_INT16(48 + 188 - 2, right_end(cs));
  TEST_ASSERT_EQUAL_INT16(48 + 188 * 47 / 48, cs.back().first.x);
  for (size_t k = 0; k + 1 < cs.size(); ++k) {
    TEST_ASSERT_EQUAL_INT16(cs[k + 1].first.x - cs[k].first.x - 1, cs[k].first.w);
    TEST_ASSERT_TRUE(cs[k].first.w >= 2);
  }
  TEST_ASSERT_TRUE(end_dot_at(c, 48 + 188 + 1));
  r.levels(3000, 2);  // a cell more: every bar and star moves; the dot and "now" stay
  FakeCanvas d;
  const AnimFrame g = r.tick(d);
  TEST_ASSERT_TRUE(has_rect(g, kStarsAndBars));
  TEST_ASSERT_EQUAL_UINT(49, cells(d).size());
}

// Stars less than 8 px apart would melt into one blob: the later one is only its stem.
static void test_close_stars_keep_only_their_stems() {
  Rig r(RecMode::Meeting);
  r.levels(500, 2);
  r.port.app.rec.stars = 1;
  r.levels(500, 3);  // a star on block 2
  r.port.app.rec.stars = 2;
  r.levels(500, 3);  // and one on block 5, 6 px on
  r.port.app.rec.stars = 3;
  r.levels(500, 10);  // and one on block 8, 6 px on from the last, 12 from the first drawn
  FakeCanvas c;
  r.tick(c);
  int glyphs = 0;
  for (const TextOp& t : c.texts) glyphs += t.text == "★" ? 1 : 0;
  int stems = 0;
  for (const auto& fl : c.fills) stems += fl.second == Color::Warn && fl.first.w == 1 && fl.first.h == 4 ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(3, stems);
  TEST_ASSERT_EQUAL_INT(2, glyphs);  // blocks 2 and 8: 12 px apart
}

static void test_the_end_confirm_covers_the_timeline() {
  Rig r(RecMode::Meeting);
  r.port.app.rec.phase = RecPhase::EndConfirm;
  r.levels(4096, 5);
  FakeCanvas c;
  const AnimFrame f = r.tick(c);
  TEST_ASSERT_FALSE(has_rect(f, kLine));
  TEST_ASSERT_TRUE(has_rect(f, Rect{46, 38, 12, 12}));  // the ● breathes on above it
  TEST_ASSERT_TRUE(cells(r.page()).empty());
}

static void test_saving_puts_the_dot_back_to_full() {
  Rig r(RecMode::Memo);
  FakeCanvas c;
  r.tick(c);
  r.tick(c);
  r.port.app.rec.phase = RecPhase::Finishing;
  AnimFrame f = r.tick(c);
  TEST_ASSERT_EQUAL_INT8(0, f.breath);
  TEST_ASSERT_TRUE(has_rect(f, Rect{60, 38, 12, 12}));
  f = r.tick(c);
  TEST_ASSERT_EQUAL_INT8(-1, f.breath);  // and stands still
}

static void test_under_the_menu_the_dot_is_back_to_full() {
  Rig r(RecMode::Meeting);
  FakeCanvas c;
  r.tick(c);
  r.shell.on_key(nav(Nav::Menu), r.now);
  TEST_ASSERT_TRUE(r.shell.menu_open());
  const AnimFrame f = r.tick(c);
  TEST_ASSERT_EQUAL_INT8(0, f.breath);
  TEST_ASSERT_FALSE(has_rect(f, Rect{46, 38, 12, 12}));  // dimmed under the sheet: nothing to push
}

static void test_nothing_moves_unless_the_backlight_is_on() {
  Rig r(RecMode::Memo);
  FakeCanvas c;
  r.tick(c);
  r.shell.set_backlight(Backlight::Dim);
  r.levels(4096, 10);
  const AnimFrame f = r.tick(c);
  TEST_ASSERT_EQUAL_UINT8(0, f.n);
  TEST_ASSERT_EQUAL_INT8(-1, f.breath);
  for (const Rect& b : bars(r.page())) TEST_ASSERT_EQUAL_INT16(3, b.h);  // as the last tick left it
  r.shell.set_backlight(Backlight::On);
  TEST_ASSERT_TRUE(r.tick(c).n > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_waveform_is_the_last_35_blocks);
  RUN_TEST(test_a_memo_tick_redraws_mando_wave_and_dot);
  RUN_TEST(test_a_real_memo_moves_the_wave_and_mando);
  RUN_TEST(test_the_timeline_spans_the_meeting_with_its_stars);
  RUN_TEST(test_a_full_timeline_spans_the_width);
  RUN_TEST(test_close_stars_keep_only_their_stems);
  RUN_TEST(test_the_end_confirm_covers_the_timeline);
  RUN_TEST(test_saving_puts_the_dot_back_to_full);
  RUN_TEST(test_under_the_menu_the_dot_is_back_to_full);
  RUN_TEST(test_nothing_moves_unless_the_backlight_is_on);
  return UNITY_END();
}
