// The recording pages after docs/20 B2 (U1, U6, U8, U15, U23; D3, D8):
//   ready      G0 down where a hold would record shows "● Wait" in the status bar's ● slot
//              from the press, not 500 ms later (D8: no pre-roll, only the sign);
//   countdown  from 4:30 the memo page says "0:30 left" (a Warn capsule where "Part N" was)
//              and its waveform turns Warn, so the 5 minute cap never comes unannounced;
//              gh #51's grace and hands-free capsule shares the row with it, shorter;
//   battery    the meeting page keeps "Bat N%" (yellow under 30 %, red from 17 %, the stop
//              line); under 30 % the start box adds how long the battery lasts;
//   boxes      the hint bar no longer repeats the card's key line: it names Opt+L instead;
//   hint       the meeting page names Space (★) again, as keycaps at the roomiest spacing.
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "rec_overlay.h"
#include "runtime.h"
#include "shell.h"
#include "status_bar.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"
#include "theme.h"
#include "theme_levels.h"

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
  Rig() {
    port.app = demo_state();
    shell.begin(0);
  }
  void phase(RecPhase p, RecMode m) {
    port.app.rec.avail = true;
    port.app.rec.phase = p;
    port.app.rec.mode = m;
    port.app.rec.started_ms = 0;
  }
  FakeCanvas frame(uint32_t now) {
    FakeCanvas c;
    shell.render(c, now);
    return c;
  }
};

constexpr uint32_t kCountdownMs = 270000;  // 4:30

// The hint bar's keycaps at the roomiest spacing (docs/19 §4.2 rule 7): from x 4, items 8 px
// apart, the words 3 px after their cap; `word` is drawn at x there.
void assert_roomy(const char* hint, const char* word, int16_t x) {
  FakeCanvas c;
  theme::hint_bar(c, hint);
  TEST_ASSERT_TRUE_MESSAGE(theme::hint_fits(c, hint), hint);
  TEST_ASSERT_NOT_NULL_MESSAGE(c.find(word), hint);
  TEST_ASSERT_EQUAL_INT16_MESSAGE(x, c.find(word)->x, hint);
}

}  // namespace

// D8: from the press, before the memo starts at 500 ms.
static void test_ready_shows_in_the_status_bar_from_the_press() {
  AppState s = demo_state();
  s.ui.memo_ready = true;
  FakeCanvas c;
  render_status_bar(c, s, 0);
  const TextOp* mark = c.find("● 准备");
  TEST_ASSERT_NOT_NULL_MESSAGE(mark, c.dump().c_str());
  TEST_ASSERT_TRUE(mark->x == 4 && mark->style.fg == Color::Warn && c.filled_at(0, Color::WarnBg));
  s.ui.memo_ready = false;
  FakeCanvas idle;
  render_status_bar(idle, s, 0);
  TEST_ASSERT_FALSE(idle.has("准备"));
  RecRig x;  // through the runtime: 100 ms into a G0 hold on home
  x.hold_g0(100);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("● 准备"), x.r.disp.canvas_.dump().c_str());
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
}

// U15: the last 30 s of a memo count down, in Warn, and the waveform says it too.
static void test_memo_counts_down_its_last_30_s() {
  Rig r;
  r.phase(RecPhase::Recording, RecMode::Memo);
  FakeCanvas before = r.frame(kCountdownMs - 1);
  TEST_ASSERT_FALSE(before.has("还剩"));
  TEST_ASSERT_TRUE(before.has("第 1 段"));
  FakeCanvas at = r.frame(kCountdownMs);
  const TextOp* left = at.find("还剩 0:30");
  TEST_ASSERT_NOT_NULL_MESSAGE(left, at.dump().c_str());
  TEST_ASSERT_TRUE(left->style.fg == Color::Warn && at.filled_at(left->y, Color::WarnBg));
  TEST_ASSERT_FALSE(at.has("第 1 段"));
  TEST_ASSERT_TRUE(left->x + at.textWidth("还剩 0:30") <= kScreenW - theme::kGutter);
  TEST_ASSERT_TRUE(r.frame(299001).has("还剩 0:01"));
  TEST_ASSERT_TRUE(rec_memo_late(r.port.app.rec, kCountdownMs));
  TEST_ASSERT_FALSE(rec_memo_late(r.port.app.rec, kCountdownMs - 1));
  r.phase(RecPhase::Recording, RecMode::Meeting);  // a meeting's cap is hours away
  TEST_ASSERT_FALSE(rec_memo_late(r.port.app.rec, kCountdownMs));
}

// The right end of t's text (its capsule reaches 5 px past it).
static int16_t text_end(const FakeCanvas& c, const TextOp* t) {
  return static_cast<int16_t>(t->x + c.textWidth(t->text.c_str()));
}

// gh #51 meets U15: the grace or hands-free capsule takes the title row ("Voice memo" and
// "Part N" go); in the last 30 s it is shorter and "0:30 left" keeps its corner, the two apart.
static void test_the_memo_state_and_the_countdown_share_the_row() {
  Rig r;
  r.phase(RecPhase::Recording, RecMode::Memo);
  struct Case {
    bool latched;
    const char* full;
    const char* late;
  } kCases[] = {{true, "免提录音 · 按 G0 结束", "免提 · G0 结束"},
                {false, "松开了 · 0.6 秒内按回继续", "0.6 秒内按回继续"}};
  for (const Case& k : kCases) {
    r.port.app.rec.latched = k.latched;
    r.port.app.rec.grace = !k.latched;
    FakeCanvas before = r.frame(kCountdownMs - 1);
    const TextOp* st = before.find(k.full);
    TEST_ASSERT_NOT_NULL_MESSAGE(st, before.dump().c_str());
    TEST_ASSERT_FALSE_MESSAGE(before.has("第 1 段") || before.has("语音便条"), before.dump().c_str());
    TEST_ASSERT_TRUE(text_end(before, st) <= kScreenW - theme::kGutter);
    FakeCanvas at = r.frame(kCountdownMs);
    const TextOp* left = at.find("还剩 0:30");
    st = at.find(k.late);
    TEST_ASSERT_NOT_NULL_MESSAGE(left, at.dump().c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(st, at.dump().c_str());
    TEST_ASSERT_FALSE(at.has(k.full));
    TEST_ASSERT_TRUE_MESSAGE(text_end(at, st) + 2 * 5 + 4 <= left->x, at.dump().c_str());  // both pads, a gap
  }
}

static int bars_in(const FakeCanvas& c, Color col) {
  int n = 0;
  for (const auto& f : c.fills) n += f.first.w == 3 && f.first.y >= 63 && f.first.y + f.first.h <= 88 && f.second == col;
  return n;
}

static void test_the_late_waveform_is_warn() {
  theme::LevelHistory h;
  for (int i = 0; i < 40; ++i) h = theme::level_push(h, 3000, 0);
  FakeCanvas early;
  rec_wave_part(early, h, false);
  TEST_ASSERT_EQUAL_INT(0, bars_in(early, Color::Warn));
  TEST_ASSERT_TRUE(bars_in(early, Color::Accent) > 0);
  FakeCanvas late;
  rec_wave_part(late, h, true);
  TEST_ASSERT_EQUAL_INT(35, bars_in(late, Color::Warn));
}

// U6: the meeting page keeps its battery, coloured as the status bar's.
static void test_meeting_page_keeps_the_battery() {
  const struct {
    uint16_t mv;
    const char* text;
    Color ink;
  } kCases[] = {{3900, "电 70%", Color::Ink3}, {3500, "电 23%", Color::Warn}, {3450, "电 17%", Color::Error},
                {0, "电 --", Color::Ink3}};
  for (const auto& k : kCases) {
    Rig r;
    r.phase(RecPhase::Recording, RecMode::Meeting);
    r.port.app.device.bat_mv = k.mv;
    FakeCanvas c = r.frame(751000);
    const TextOp* bat = c.find(k.text);
    TEST_ASSERT_NOT_NULL_MESSAGE(bat, c.dump().c_str());
    TEST_ASSERT_TRUE_MESSAGE(bat->style.fg == k.ink, k.text);
    TEST_ASSERT_EQUAL_INT16(38, bat->y);  // the timer's row, with REC
    TEST_ASSERT_EQUAL_INT16(kScreenW - theme::kGutter, bat->x + c.textWidth(k.text));
  }
}

// U6: under 30 % the start box says about how long the battery lasts; its words stay.
static void test_start_box_says_how_long_the_battery_lasts() {
  TEST_ASSERT_EQUAL_INT(50, rec_bat_minutes(3550));
  TEST_ASSERT_EQUAL_INT(25, rec_bat_minutes(3500));
  TEST_ASSERT_EQUAL_INT(0, rec_bat_minutes(3450));
  Rig r;
  r.phase(RecPhase::Confirm, RecMode::Meeting);
  r.port.app.device.bat_mv = 3500;
  FakeCanvas low = r.frame(100);
  const TextOp* line = low.find("电量约够 25 分钟");
  TEST_ASSERT_NOT_NULL_MESSAGE(line, low.dump().c_str());
  TEST_ASSERT_TRUE(line->style.fg == Color::Warn);
  TEST_ASSERT_TRUE(line->y > 48 && line->y + 12 <= 84);  // between its rows, which stay
  TEST_ASSERT_EQUAL_STRING("开始会议录音？", low.row(24).c_str());
  TEST_ASSERT_EQUAL_STRING("请先告知在场的人", low.row(48).c_str());
  TEST_ASSERT_EQUAL_STRING("Enter 开始 ` 取消", low.line(84).c_str());
  r.port.app.device.bat_mv = 3455;
  TEST_ASSERT_TRUE(r.frame(100).has("电量不够 5 分钟"));
  r.port.app.device.bat_mv = 3560;  // 30 %
  TEST_ASSERT_FALSE(r.frame(100).has("电量"));
  r.port.app.device.bat_mv = 0;  // unknown
  TEST_ASSERT_FALSE(r.frame(100).has("电量"));
}

// B2: the boxes' key line is on the card; the hint bar says what else there is (Opt+L).
static void test_boxes_do_not_repeat_their_keys() {
  Rig r;
  r.phase(RecPhase::Confirm, RecMode::Meeting);
  FakeCanvas start = r.frame(100);
  TEST_ASSERT_EQUAL_STRING("Opt+L 取消并锁屏 Tab 菜单", start.line(kHintRect.y).c_str());
  TEST_ASSERT_TRUE(start.line(84) != start.line(kHintRect.y));
  r.phase(RecPhase::EndConfirm, RecMode::Meeting);
  FakeCanvas end = r.frame(100);
  TEST_ASSERT_EQUAL_STRING("Opt+L 继续录并锁屏 Tab 菜单", end.line(kHintRect.y).c_str());
  TEST_ASSERT_EQUAL_STRING("Enter 结束 ` 继续录", end.line(90).c_str());  // the card keeps its keys
  assert_roomy("Opt+L 取消并锁屏  Tab菜单", "取消并锁屏", 4 + 34 + 3);
  assert_roomy("Opt+L 继续录并锁屏  Tab菜单", "继续录并锁屏", 4 + 34 + 3);
}

// U23: Space stars too, and the hint says so again, at the roomiest spacing.
static void test_meeting_hint_names_space() {
  Rig r;
  r.phase(RecPhase::Recording, RecMode::Meeting);
  FakeCanvas c = r.frame(1000);
  const std::string hint = c.line(kHintRect.y);
  TEST_ASSERT_EQUAL_STRING("G0 空格 加★ Opt+R 结束 Tab 菜单", hint.c_str());
  assert_roomy("G0 空格 加★  Opt+R结束  Tab菜单", "加★", 4 + 16 + 8 + 28 + 3);  // [G0] [空格] 加★
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ready_shows_in_the_status_bar_from_the_press);
  RUN_TEST(test_memo_counts_down_its_last_30_s);
  RUN_TEST(test_the_memo_state_and_the_countdown_share_the_row);
  RUN_TEST(test_the_late_waveform_is_warn);
  RUN_TEST(test_meeting_page_keeps_the_battery);
  RUN_TEST(test_start_box_says_how_long_the_battery_lasts);
  RUN_TEST(test_boxes_do_not_repeat_their_keys);
  RUN_TEST(test_meeting_hint_names_space);
  return UNITY_END();
}
