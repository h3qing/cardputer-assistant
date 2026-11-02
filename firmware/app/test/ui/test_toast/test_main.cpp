// Toast priority (docs/20 U20, A8): the user's own result is shown at once, ahead of the
// Mac's "X needs you" notices (which it pushes out: the status bar's !N keeps that count);
// errors stay 4.5 s; a refused key's hint is never repeated within 3 s and goes away when
// its condition does; while the screen is locked or dark a toast waits instead of
// running out unseen (docs/20 U6).
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "runtime.h"
#include "support/fake_canvas.h"
#include "support/fixtures.h"
#include "support/runtime_rig.h"
#include "toast.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

static std::string current(const ToastQueue& q) {
  const Toast* t = toast_current(q);
  return t ? std::string(t->text.c_str()) : std::string();
}

static void test_own_result_jumps_the_notices() {
  ToastQueue q;
  q = toast_push(q, ToastLevel::Warn, "cpa/a 等你", 0);
  q = toast_push(q, ToastLevel::Warn, "cpa/b 等你", 10);
  q = toast_result(q, ToastLevel::Ok, "已保存", 100);
  TEST_ASSERT_EQUAL_STRING("已保存", current(q).c_str());
  TEST_ASSERT_EQUAL_UINT32(100, toast_current(q)->shown_ms);
  TEST_ASSERT_EQUAL_UINT8(1, q.n);  // the notices went: the status bar's !N has them
  q = toast_tick(q, 100 + kToastMs);
  TEST_ASSERT_NULL(toast_current(q));
}

static void test_results_keep_their_order() {
  ToastQueue q;
  q = toast_result(q, ToastLevel::Error, "保存失败", 0);
  q = toast_result(q, ToastLevel::Ok, "已保存", 50);
  TEST_ASSERT_EQUAL_STRING("保存失败", current(q).c_str());  // an error is never hidden by a later result
  q = toast_push(q, ToastLevel::Warn, "cpa/a 等你", 60);  // a notice waits behind them
  TEST_ASSERT_EQUAL_UINT8(3, q.n);
  q = toast_tick(q, kToastMs);
  TEST_ASSERT_EQUAL_STRING("保存失败", current(q).c_str());  // errors stay longer
  q = toast_tick(q, kToastErrorMs);
  TEST_ASSERT_EQUAL_STRING("已保存", current(q).c_str());
  TEST_ASSERT_TRUE(kToastErrorMs >= 4000 && kToastErrorMs <= 5000);
}

static void test_hint_is_not_repeated_within_3s() {
  ToastQueue q;
  q = toast_hint(q, ToastLevel::Info, "开会中：G0 加★，Opt+R 结束", 0);
  q = toast_hint(q, ToastLevel::Info, "开会中：G0 加★，Opt+R 结束", 500);
  TEST_ASSERT_EQUAL_UINT8(1, q.n);
  q = toast_tick(q, kToastMs);
  TEST_ASSERT_EQUAL_UINT8(0, q.n);
  q = toast_hint(q, ToastLevel::Info, "开会中：G0 加★，Opt+R 结束", kHintRepeatMs - 1);
  TEST_ASSERT_EQUAL_UINT8(0, q.n);
  q = toast_hint(q, ToastLevel::Info, "开会中：G0 加★，Opt+R 结束", kHintRepeatMs);
  TEST_ASSERT_EQUAL_UINT8(1, q.n);
  q = toast_hint(q, ToastLevel::Info, "另一句", kHintRepeatMs + 10);
  TEST_ASSERT_EQUAL_UINT8(2, q.n);
}

static void test_retract_drops_the_hints_only() {
  ToastQueue q;
  q = toast_hint(q, ToastLevel::Warn, "先选项目：1 cpa", 0);
  q = toast_result(q, ToastLevel::Ok, "已保存", 10);
  q = toast_retract(q, 20);
  TEST_ASSERT_EQUAL_UINT8(1, q.n);
  TEST_ASSERT_EQUAL_STRING("已保存", current(q).c_str());
  TEST_ASSERT_EQUAL_UINT32(20, toast_current(q)->shown_ms);  // the new head runs from now
}

static void test_hold_keeps_the_head_waiting() {
  ToastQueue q = toast_result(ToastQueue{}, ToastLevel::Error, "会议已停：电量低", 0);
  q = toast_hold(q, 60000);  // locked or dark for a minute
  q = toast_tick(q, 60000 + kToastErrorMs - 1);
  TEST_ASSERT_EQUAL_STRING("会议已停：电量低", current(q).c_str());
}

// A key as the runtime gives it: the lock gate first (docs/13 §5.4 step 1).
static void press(RuntimeRig& r, const KeyEvent& ev, uint32_t at) {
  r.now = at;
  if (r.rt.shell().lock_gate(ev, at)) r.rt.shell().on_key(ev, at);
}

// Review (U20 again): what piled up while the screen was not seen must not delay what the
// user does next. Seen again, only the latest held result and the held errors are left;
// a result pushed while seen goes ahead of them.
static void test_held_results_collapse_when_seen() {
  ToastQueue q;
  q = toast_result(q, ToastLevel::Error, "SD 卡出错：插好后需重启", 0);
  q = toast_hold(q, 10);
  q = toast_result(q, ToastLevel::Ok, "录音已保存 · 0:03", 5000);
  q = toast_hold(q, 5010);
  q = toast_result(q, ToastLevel::Info, "录音太短，已丢弃", 9000);
  q = toast_hold(q, 60000);
  TEST_ASSERT_EQUAL_UINT8(3, q.n);
  q = toast_seen(q, 70000);
  TEST_ASSERT_EQUAL_UINT8(2, q.n);  // the error and the latest result; the older result went
  TEST_ASSERT_EQUAL_STRING("SD 卡出错：插好后需重启", current(q).c_str());
  TEST_ASSERT_EQUAL_UINT32(70000, toast_current(q)->shown_ms);
  q = toast_result(q, ToastLevel::Ok, "已保存 · 待传 ↑1", 70300);
  TEST_ASSERT_EQUAL_STRING("已保存 · 待传 ↑1", current(q).c_str());  // at once
  TEST_ASSERT_EQUAL_UINT32(70300, toast_current(q)->shown_ms);
  q = toast_tick(q, 70300 + kToastMs);
  TEST_ASSERT_EQUAL_STRING("SD 卡出错：插好后需重启", current(q).c_str());  // the held ones follow
  q = toast_tick(q, 70300 + kToastMs + kToastErrorMs);
  TEST_ASSERT_EQUAL_STRING("录音太短，已丢弃", current(q).c_str());
  // held notices and hints are gone once seen (the status bar's !N has the count)
  ToastQueue n = toast_push(ToastQueue{}, ToastLevel::Warn, "cpa/a 等你", 0);
  n = toast_hint(n, ToastLevel::Info, "编辑器开着：退格 退出", 10);
  n = toast_seen(toast_hold(n, 20), 30);
  TEST_ASSERT_EQUAL_UINT8(0, n.n);
  // full: the stale held one makes room, not the fresh result before it
  ToastQueue f = toast_result(toast_hold(toast_result(ToastQueue{}, ToastLevel::Ok, "H1", 0), 10), ToastLevel::Ok,
                              "H2", 20);
  f = toast_seen(toast_hold(f, 30), 40);  // H2 kept (the latest)
  f = toast_result(f, ToastLevel::Error, "F1", 50);
  f = toast_result(f, ToastLevel::Ok, "F2", 60);
  f = toast_result(f, ToastLevel::Ok, "F3", 70);
  TEST_ASSERT_EQUAL_UINT8(3, f.n);
  TEST_ASSERT_EQUAL_STRING("F1", current(f).c_str());
  TEST_ASSERT_TRUE(f.items[1].text.equals("F2") && f.items[2].text.equals("F3"));
  // while not seen, results keep their order (nothing jumps what is held)
  ToastQueue d = toast_hold(toast_result(ToastQueue{}, ToastLevel::Error, "E1", 0), 10);
  d = toast_result(d, ToastLevel::Error, "E2", 20);
  TEST_ASSERT_EQUAL_STRING("E1", current(d).c_str());
}

// The pocket day of the review (rv-ux-lens locked_backlog): three recordings end while
// locked, then the user unlocks and writes a note. Its "Saved" shows within 600 ms.
static void test_results_held_while_locked_do_not_delay_the_next_one() {
  RuntimeRig r;
  r.handshake();
  press(r, opt('l'), r.now + 10);
  const char* ends[] = {"录音已保存 · 0:03", "录音太短，已丢弃", "录音已保存 · 0:02"};
  for (const char* e : ends) {
    r.rt.shell().toast(ToastLevel::Ok, e);
    for (int i = 0; i < 50; ++i) r.step(100);
  }
  for (int i = 0; i < 600; ++i) r.step(100);  // a minute later
  press(r, opt('l'), r.now + 10);
  press(r, enter(), r.now + 100);  // unlocked
  r.step();
  FakeCanvas seen;
  r.rt.shell().render(seen, r.now);
  TEST_ASSERT_TRUE_MESSAGE(seen.has("录音已保存 · 0:02"), seen.dump().c_str());  // the latest held one
  const uint32_t t0 = r.now;
  r.key(g0tap(), t0 + 100);
  r.key(chr('x'), t0 + 200);
  r.key(enter(), t0 + 300);
  r.step();
  FakeCanvas c;
  r.rt.shell().render(c, r.now);
  TEST_ASSERT_TRUE(r.now - t0 < 600);
  TEST_ASSERT_TRUE_MESSAGE(c.has("已保存 · 待传"), c.dump().c_str());
  for (int i = 0; i < 30; ++i) r.step(100);
  FakeCanvas after;
  r.rt.shell().render(after, r.now);
  TEST_ASSERT_FALSE(after.has("录音已保存 · 0:03"));  // the older ones never come back
  TEST_ASSERT_FALSE(after.has("录音太短"));
}

// §4.4: with two "X needs you" queued, a saved note's result is on screen within 600 ms.
static void test_runtime_shows_the_own_result_first() {
  RuntimeRig r;
  r.handshake();
  r.send("sessions.snapshot",
         "\"rev\":5,\"total\":2,\"sessions\":["
         "{\"sid\":\"aaaa0001\",\"slot\":1,\"name\":\"one\",\"kind\":\"bg\",\"st\":\"waiting\",\"need\":true,\"age_s\":5},"
         "{\"sid\":\"aaaa0002\",\"slot\":1,\"name\":\"two\",\"kind\":\"bg\",\"st\":\"waiting\",\"need\":true,\"age_s\":9}]");
  r.step();
  const uint32_t t0 = r.now;
  r.key(g0tap(), t0 + 100);
  r.key(chr('x'), t0 + 200);
  r.key(enter(), t0 + 300);
  r.step();
  FakeCanvas c;
  r.rt.shell().render(c, r.now);
  TEST_ASSERT_TRUE(r.now - t0 < 600);
  TEST_ASSERT_TRUE_MESSAGE(c.has("已保存"), c.dump().c_str());
}

// docs/20 U6: a toast that ran out while locked was never seen. It waits for the unlock.
static void test_locked_screen_holds_the_toast() {
  RuntimeRig r;
  r.handshake();
  press(r, opt('l'), r.now + 10);  // lock
  TEST_ASSERT_TRUE(r.rt.shell().locked());
  r.rt.shell().toast(ToastLevel::Warn, "会议已停：电量低");
  for (int i = 0; i < 100; ++i) r.step(100);  // 10 s locked
  press(r, opt('l'), r.now + 10);
  press(r, enter(), r.now + 100);  // unlocked
  TEST_ASSERT_FALSE(r.rt.shell().locked());
  r.step();
  FakeCanvas c;
  r.rt.shell().render(c, r.now);
  TEST_ASSERT_TRUE_MESSAGE(c.has("会议已停"), c.dump().c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_own_result_jumps_the_notices);
  RUN_TEST(test_results_keep_their_order);
  RUN_TEST(test_hint_is_not_repeated_within_3s);
  RUN_TEST(test_retract_drops_the_hints_only);
  RUN_TEST(test_hold_keeps_the_head_waiting);
  RUN_TEST(test_runtime_shows_the_own_result_first);
  RUN_TEST(test_locked_screen_holds_the_toast);
  RUN_TEST(test_held_results_collapse_when_seen);
  RUN_TEST(test_results_held_while_locked_do_not_delay_the_next_one);
  return UNITY_END();
}
