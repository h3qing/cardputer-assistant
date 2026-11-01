// The status bar after docs/20 B3 (U6, U16, U21):
//   counts   none is drawn at zero (↑0 too); one name each;
//   ※k       the keyboard warning shows 10 s after its count rose, not until the reboot;
//   battery  yellow under 30 % (the 3.55 V warning), red from 17 % (the 3.45 V stop line);
//   ※SD      the device records but has no usable SD card (none at boot, or lost);
//   budget   the left group gets what the right one leaves: the weekday goes first, then
//            录N, ↑N, ※k, ※SD; ●, the link, !N, the clock and the battery always stay, and
//            nothing ever overlaps;
//   control  the Tab menu's legend follows the bar, and says "√ 已同步" once nothing waits
//            and the Mac is there.
#include <unity.h>

#include <string>

#include "menu_model.h"
#include "rec_core.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "status_bar.h"
#include "support/fake_canvas.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

const Binding kLockMap[] = {{Layer::System, Action::Lock, 0, opt('l')}};
const KeymapView kMap{kLockMap, 1};
constexpr uint32_t kNow = 7500000;  // 125 minutes into a meeting

FakeCanvas bar(const AppState& s, uint32_t now = 0) {
  FakeCanvas c;
  render_status_bar(c, s, now);
  return c;
}

// The worst case the bar meets: a long meeting, an old host, many notes, two sessions
// waiting, many segments, the keyboard warning and no SD card.
AppState crowded() {
  AppState s = demo_state();
  s.rec.phase = RecPhase::Recording;
  s.rec.mode = RecMode::Meeting;
  s.rec.started_ms = 0;
  s.link.mode = LinkMode::Attached;
  s.link.version_mismatch = true;
  s.outbox.pending = 200;
  s.rec.files_n = 36;
  s.device.ghost_n = 9;
  s.device.kb_warn_ms = kNow - 1000;
  s.rec.sd_bad = true;
  s.time.known = true;
  s.time.offset_ms = 1792590120000LL - kNow;  // 2026-10-21 13:42 UTC at kNow, a Wednesday
  s.device.bat_mv = 3500;
  return s;
}

// The bar's texts split at the clock: what is left of it ends before what is right of it.
void assert_no_overlap(const FakeCanvas& c) {
  int16_t left_end = 0;
  int16_t right_start = kScreenW;
  for (const TextOp& t : c.texts) {
    if (t.y != 0) continue;
    const int16_t end = static_cast<int16_t>(t.x + c.textWidth(t.text.c_str()));
    if (t.x < 120 && end > left_end) left_end = end;
    if (t.x >= 120 && t.x < right_start) right_start = t.x;
  }
  TEST_ASSERT_TRUE_MESSAGE(left_end + 4 <= right_start, c.dump().c_str());
}

std::string control(const AppState& s, uint8_t from) {
  const SysInfo sys;
  MenuSource src;
  src.layer = Layer::List;
  const MenuContent m = menu_content(MenuPage::Control, src, s, sys, kMap);
  std::string out;
  for (uint8_t i = from; i < m.n; ++i) out += std::string(m.rows[i].keys.c_str()) + " " + m.rows[i].label.c_str() + "\n";
  return out;
}

}  // namespace

static void test_no_count_is_drawn_at_zero() {
  AppState s;
  TEST_ASSERT_FALSE(bar(s).has("↑"));
  s.outbox.pending = 3;
  TEST_ASSERT_TRUE(bar(s).has("↑3"));
}

static void test_keyboard_warning_shows_10_s() {
  AppState s;
  s.device.ghost_n = 2;
  s.device.kb_warn_ms = 5000;
  TEST_ASSERT_TRUE(bar(s, 5000).has("※k2"));
  TEST_ASSERT_TRUE(bar(s, 5000 + kKbWarnShowMs - 1).has("※k2"));
  TEST_ASSERT_FALSE(bar(s, 5000 + kKbWarnShowMs).has("※k"));
  RuntimeRig r;  // a ghost frame through the runtime: counted, and when
  r.step(1000);
  r.step(10, KeySet{}.with(0).with(2).with(14).with(16));
  TEST_ASSERT_EQUAL_UINT16(1, r.rt.state().device.ghost_n);
  TEST_ASSERT_EQUAL_UINT32(r.now, r.rt.state().device.kb_warn_ms);
}

static void test_battery_colours() {
  TEST_ASSERT_TRUE(battery_ink(-1) == Color::Ink3);
  TEST_ASSERT_TRUE(battery_ink(35) == Color::Ink3);
  TEST_ASSERT_TRUE(battery_ink(30) == Color::Ink3);
  TEST_ASSERT_TRUE(battery_ink(29) == Color::Warn);
  TEST_ASSERT_TRUE(battery_ink(18) == Color::Warn);
  TEST_ASSERT_TRUE(battery_ink(17) == Color::Error);
  TEST_ASSERT_TRUE(battery_ink(battery_pct(rec::kBatWarnMv)) == Color::Warn);  // the warning
  TEST_ASSERT_TRUE(battery_ink(battery_pct(rec::kBatStopMv)) == Color::Error);  // the stop line
  AppState s;
  s.device.bat_mv = 3500;
  TEST_ASSERT_TRUE(bar(s).find("23%")->style.fg == Color::Warn);
}

static void test_sd_mark() {
  AppState s;
  TEST_ASSERT_FALSE(bar(s).has("SD"));
  s.rec.sd_bad = true;
  const FakeCanvas c = bar(s);
  TEST_ASSERT_NOT_NULL(c.find("※SD"));
  TEST_ASSERT_TRUE(c.find("※SD")->style.fg == Color::Warn);
  RecRig card(true);
  TEST_ASSERT_FALSE(card.r.rt.state().rec.sd_bad);
  RecRig none(false);  // booted without a card: notes go to the internal store, nothing records
  TEST_ASSERT_TRUE(none.r.rt.state().rec.sd_bad);
  TEST_ASSERT_TRUE(none.r.disp.canvas_.has("※SD"));
}

static void test_the_budget_drops_the_least_first() {
  const AppState s = crowded();
  const uint32_t now = kNow;
  const FakeCanvas c = bar(s, now);
  assert_no_overlap(c);
  TEST_ASSERT_TRUE(c.has("● 125:00") && c.has("版本不兼容") && c.has("!2") && c.has("13:42") && c.has("23%"));
  AppState offline = s;  // the usual long link word: ※SD stays, the others give way
  offline.link.mode = LinkMode::Detached;
  const FakeCanvas o = bar(offline, now);
  assert_no_overlap(o);
  TEST_ASSERT_TRUE(o.has("※SD") && o.has("!2") && o.has("离线"));
  TEST_ASSERT_FALSE(o.has("周三") || o.has("录36"));  // the first to go
  for (uint16_t pending : {0, 3, 200}) {
    for (uint16_t files : {0, 2, 36}) {
      AppState t = s;
      t.outbox.pending = pending;
      t.rec.files_n = files;
      assert_no_overlap(bar(t, now));
    }
  }
  AppState calm = demo_state();  // a usual day: everything fits
  set_ready(calm);
  calm.outbox.pending = 3;
  calm.rec.files_n = 2;
  calm.time = s.time;
  calm.device.bat_mv = 4000;
  const FakeCanvas d = bar(calm, kNow);
  TEST_ASSERT_TRUE(d.has("↑3") && d.has("!2") && d.has("录2") && d.has("周三"));
  assert_no_overlap(d);
}

static void test_control_page_follows_the_bar() {
  AppState s;
  set_ready(s);
  TEST_ASSERT_EQUAL_STRING("√ 已同步：都传到 Mac 了\n", control(s, 5).c_str());
  s.outbox.pending = 1;
  TEST_ASSERT_EQUAL_STRING("↑1 1 条笔记等着传到 Mac\n", control(s, 5).c_str());
  const AppState offline;
  TEST_ASSERT_EQUAL_STRING("", control(offline, 5).c_str());  // nothing on the bar to explain
  AppState no_card;
  no_card.rec.sd_bad = true;
  TEST_ASSERT_EQUAL_STRING("※SD SD 卡不能用：不能录音\n", control(no_card, 5).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_count_is_drawn_at_zero);
  RUN_TEST(test_keyboard_warning_shows_10_s);
  RUN_TEST(test_battery_colours);
  RUN_TEST(test_sd_mark);
  RUN_TEST(test_the_budget_drops_the_least_first);
  RUN_TEST(test_control_page_follows_the_bar);
  return UNITY_END();
}
