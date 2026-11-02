// Status bar (04 US-11): link state, pending ↑N, waiting !N, battery, clock and the
// keyboard warning count (only when > 0). docs/20 B3 (test_status_bar_ux): ↑N too only
// when > 0. ⟳ / ◐ / ⚠ are not in efontCN_12, so the
// bar draws ◎ / ◇ / ※ instead (see status_bar.h). v0.5 (docs/19 §6.1): online is Mando's
// head, !N only when not zero, the weekday when it fits, ●mm:ss in a fixed left slot.
#include <unity.h>

#include "core/support/lang_pin.h"
#include "rec_overlay.h"
#include "shell.h"
#include "status_bar.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "theme_art.h"
#include "toast.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

static std::string bar(const AppState& s, uint32_t now = 0) {
  FakeCanvas c;
  render_status_bar(c, s, now);
  return c.row(0);
}

static bool head_drawn(const AppState& s) {
  FakeCanvas c;
  render_status_bar(c, s, 0);
  return c.arts.size() == 1 && c.arts[0].id == ArtId::Head && c.arts[0].y == 1;
}

static void test_link_glyphs() {
  AppState s;
  TEST_ASSERT_TRUE(link_glyph(s) == LinkGlyph::Offline);
  TEST_ASSERT_TRUE(bar(s).find("○") != std::string::npos);
  TEST_ASSERT_FALSE(head_drawn(s));
  s.link.mode = LinkMode::Attached;
  TEST_ASSERT_TRUE(link_glyph(s) == LinkGlyph::NoDaemon);
  TEST_ASSERT_TRUE(bar(s).find("◇") != std::string::npos);
  s.link.mode = LinkMode::Ready;
  TEST_ASSERT_TRUE(link_glyph(s) == LinkGlyph::Online);
  TEST_ASSERT_TRUE(head_drawn(s));                         // Mando's head, not "● online":
  TEST_ASSERT_TRUE(bar(s).find("●") == std::string::npos);  // ● on the bar means recording
  s.outbox.pending = 3;
  TEST_ASSERT_TRUE(link_glyph(s) == LinkGlyph::Syncing);
  TEST_ASSERT_TRUE(head_drawn(s));
  TEST_ASSERT_TRUE(bar(s).find("◎") != std::string::npos);
}

// 04 US-04 AC3 / E7: a host on another major version is not "no response".
static void test_version_mismatch_says_incompatible() {
  AppState s;
  s.link.mode = LinkMode::Attached;
  s.link.version_mismatch = true;
  TEST_ASSERT_TRUE(link_glyph(s) == LinkGlyph::Incompatible);
  FakeCanvas c;
  render_status_bar(c, s, 0);
  const TextOp* label = c.find("版本不兼容");
  TEST_ASSERT_NOT_NULL(label);
  TEST_ASSERT_TRUE(label->style.fg == Color::Error);
  TEST_ASSERT_TRUE(c.row(0).find("无响应") == std::string::npos);
  s.link.mode = LinkMode::Detached;  // unplugged: no host to be incompatible with
  TEST_ASSERT_TRUE(link_glyph(s) == LinkGlyph::Offline);
  TEST_ASSERT_TRUE(bar(s).find("不兼容") == std::string::npos);
}

// The longer label still leaves room for the counts and the right side.
static void test_incompatible_label_does_not_overlap_right_side() {
  AppState s = demo_state();
  s.link.mode = LinkMode::Attached;
  s.link.version_mismatch = true;
  s.outbox.pending = 200;
  s.device.bat_mv = 4150;
  s.device.ghost_n = 9;
  FakeCanvas c;
  render_status_bar(c, s, 0);
  const TextOp* need = c.find("!");
  const TextOp* warn = c.find("※");
  TEST_ASSERT_NOT_NULL(need);
  TEST_ASSERT_NOT_NULL(warn);
  TEST_ASSERT_TRUE(need->x + c.textWidth(need->text.c_str()) < warn->x);
}

static void test_counts_pending_and_need() {
  AppState s = demo_state();
  s.outbox.pending = 3;
  const std::string b = bar(s);
  TEST_ASSERT_TRUE(b.find("↑3") != std::string::npos);
  TEST_ASSERT_TRUE(b.find("!2") != std::string::npos);
  FakeCanvas c;
  render_status_bar(c, s, 0);
  TEST_ASSERT_TRUE(c.find("!2")->style.fg == Color::Warn);
  TEST_ASSERT_TRUE(c.filled_at(0, Color::WarnBg));  // in a mustard pill
  TEST_ASSERT_TRUE(c.find("↑3")->style.fg == Color::Ink2);
  AppState zero;  // docs/20 B3: no count is drawn at zero, ↑0 included
  TEST_ASSERT_TRUE(bar(zero).find("↑") == std::string::npos);
  TEST_ASSERT_TRUE(bar(zero).find("!") == std::string::npos);  // nothing waits: no !0
}

// The weekday sits left of the clock while the time is known and the bar has room.
static void test_weekday_when_it_fits() {
  AppState s = demo_state();
  s.time.known = true;
  s.time.offset_ms = 1792590120000LL;  // 2026-10-21 13:42 UTC, a Wednesday
  s.device.bat_mv = 4000;
  TEST_ASSERT_TRUE(bar(s).find("周三|13:42|82%") != std::string::npos);
  TEST_ASSERT_TRUE(bar(AppState{}).find("周") == std::string::npos);  // time unknown
  s.link.mode = LinkMode::Attached;
  s.link.version_mismatch = true;
  s.outbox.pending = 200;
  s.device.ghost_n = 9;
  FakeCanvas c;
  render_status_bar(c, s, 0);
  TEST_ASSERT_FALSE(c.has("周三"));  // the longest left side: the weekday gives way
  TEST_ASSERT_TRUE(c.has("※k9") && c.has("13:42"));
  TEST_ASSERT_TRUE(c.find("!2")->x + 12 < c.find("※k9")->x);
}

static void test_clock_unknown_then_local_time() {
  TimeInfo t;
  TEST_ASSERT_EQUAL_STRING("--:--", clock_text(t, 0).c_str());
  t.known = true;
  t.offset_ms = 1792440000000LL;  // 2026-10-19 ... UTC
  t.tz_min = -420;
  // 1792440000000 ms = 20:00:00 UTC -> 13:00 at UTC-7
  TEST_ASSERT_EQUAL_STRING("13:00", clock_text(t, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("13:42", clock_text(t, 42u * 60u * 1000u).c_str());
  AppState s;
  s.time = t;
  TEST_ASSERT_TRUE(bar(s).find("13:00") != std::string::npos);
  TEST_ASSERT_TRUE(bar(AppState{}).find("--:--") != std::string::npos);
}

static void test_battery() {
  TEST_ASSERT_EQUAL_INT(-1, battery_pct(0));
  TEST_ASSERT_EQUAL_INT(100, battery_pct(4200));
  TEST_ASSERT_EQUAL_INT(0, battery_pct(3200));
  const int mid = battery_pct(3750);
  TEST_ASSERT_TRUE(mid > 30 && mid < 70);
  AppState s;
  s.device.bat_mv = 4150;
  TEST_ASSERT_TRUE(bar(s).find("%") != std::string::npos);
  TEST_ASSERT_TRUE(bar(AppState{}).find("--%") != std::string::npos);
}

static void test_low_battery_is_red() {
  AppState s;
  s.device.bat_mv = 3300;  // under 10 %
  FakeCanvas c;
  render_status_bar(c, s, 0);
  const TextOp* pct = c.find("%");
  TEST_ASSERT_NOT_NULL(pct);
  TEST_ASSERT_TRUE(pct->style.fg == Color::Error);
}

static void test_keyboard_warning_only_when_nonzero() {
  AppState s;
  TEST_ASSERT_TRUE(bar(s).find("※") == std::string::npos);
  s.device.ghost_n = 2;
  s.device.kb_reinit_n = 1;
  TEST_ASSERT_TRUE(bar(s).find("※k3") != std::string::npos);
}

static void test_toast_queue_shows_one_for_2500ms_and_keeps_three() {
  ToastQueue q;
  TEST_ASSERT_NULL(toast_current(q));
  q = toast_push(q, ToastLevel::Info, "a", 100);
  q = toast_push(q, ToastLevel::Info, "b", 200);
  TEST_ASSERT_EQUAL_STRING("a", toast_current(q)->text.c_str());
  q = toast_tick(q, 2599);
  TEST_ASSERT_EQUAL_STRING("a", toast_current(q)->text.c_str());
  q = toast_tick(q, 2600);
  TEST_ASSERT_EQUAL_STRING("b", toast_current(q)->text.c_str());
  TEST_ASSERT_EQUAL_UINT32(2600, toast_current(q)->shown_ms);
  q = toast_push(q, ToastLevel::Info, "c", 2700);
  q = toast_push(q, ToastLevel::Info, "d", 2800);
  TEST_ASSERT_EQUAL_UINT8(3, q.n);
  q = toast_push(q, ToastLevel::Info, "e", 2900);  // full: the oldest goes
  TEST_ASSERT_EQUAL_UINT8(3, q.n);
  TEST_ASSERT_EQUAL_STRING("c", toast_current(q)->text.c_str());
  TEST_ASSERT_EQUAL_UINT32(2900, toast_current(q)->shown_ms);
  q = toast_tick(q, 2900 + 2500);
  q = toast_tick(q, 2900 + 5000);
  q = toast_tick(q, 2900 + 7500);
  TEST_ASSERT_NULL(toast_current(q));
}

static void test_shell_frame_has_status_hint_and_toast() {
  FakePort port;
  port.app = demo_state();
  ProbeModule board("board");
  cpa::Module* mods[] = {&board};
  Shell shell(port, mods, 1);
  shell.begin(0);
  FakeCanvas c;
  shell.render(c, 0);
  TEST_ASSERT_TRUE(c.row(0).find("○") != std::string::npos);
  TEST_ASSERT_TRUE(c.has("probe hint"));
  shell.toast(ToastLevel::Warn, "cpa/架构设计 等你");
  c.clear();
  shell.render(c, 10);
  TEST_ASSERT_TRUE(c.has("cpa/架构设计 等你"));
  shell.tick(3000);
  c.clear();
  shell.render(c, 3000);
  TEST_ASSERT_FALSE(c.has("等你"));
  TEST_ASSERT_TRUE(c.has("probe hint"));
}

// v0.3 (docs/11 §4.2, §3.3): ●mm:ss while recording, "RN" while segments wait for sync.
static void test_recording_and_waiting_segments() {
  AppState s = demo_state();
  TEST_ASSERT_TRUE(bar(s).find("录") == std::string::npos);
  TEST_ASSERT_TRUE(bar(s).find("●") == std::string::npos);
  s.rec.avail = true;
  s.rec.files_n = 5;
  TEST_ASSERT_TRUE(bar(s).find("录5") != std::string::npos);
  s.rec.phase = RecPhase::Recording;
  s.rec.started_ms = 1000;
  const std::string b = bar(s, 1000 + 751000);
  TEST_ASSERT_TRUE(b.find("● 12:31") != std::string::npos);
  s.rec.started_ms = 0;
  TEST_ASSERT_TRUE(bar(s, 3 * 3600 * 1000).find("● 180:00") != std::string::npos);  // 3 h meeting cap
  s.rec.phase = RecPhase::Cue;
  TEST_ASSERT_TRUE(bar(s, 5000).find("● 0:00") != std::string::npos);
  // the fixed left slot: Rec on a RecBg capsule, first on the bar; the clock 6 px after the
  // ● (mockups 04-05), so they do not run together
  FakeCanvas c;
  render_status_bar(c, s, 5000);
  TEST_ASSERT_EQUAL_INT16(4, c.find("● 0:00")->x);
  TEST_ASSERT_TRUE(c.find("● 0:00")->style.fg == Color::Rec);
  TEST_ASSERT_TRUE(c.filled_at(0, Color::RecBg));
  const int16_t w = static_cast<int16_t>(c.textWidth("● 0:00") + 6);  // 3 px each side
  bool capsule = false;
  for (const auto& f : c.fills) capsule = capsule || (f.second == Color::RecBg && f.first.x == 1 && f.first.w == w);
  TEST_ASSERT_TRUE(capsule);
  s.rec.files_n = 0;
  s.rec.phase = RecPhase::Idle;
  TEST_ASSERT_TRUE(bar(s).find("录") == std::string::npos);
  TEST_ASSERT_EQUAL_STRING("12:31", rec_clock_text(751000).c_str());
  TEST_ASSERT_EQUAL_STRING("0:05", rec_clock_text(5999).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_link_glyphs);
  RUN_TEST(test_version_mismatch_says_incompatible);
  RUN_TEST(test_incompatible_label_does_not_overlap_right_side);
  RUN_TEST(test_counts_pending_and_need);
  RUN_TEST(test_weekday_when_it_fits);
  RUN_TEST(test_clock_unknown_then_local_time);
  RUN_TEST(test_battery);
  RUN_TEST(test_low_battery_is_red);
  RUN_TEST(test_keyboard_warning_only_when_nonzero);
  RUN_TEST(test_toast_queue_shows_one_for_2500ms_and_keeps_three);
  RUN_TEST(test_shell_frame_has_status_hint_and_toast);
  RUN_TEST(test_recording_and_waiting_segments);
  return UNITY_END();
}
