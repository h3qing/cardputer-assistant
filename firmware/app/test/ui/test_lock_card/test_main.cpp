// The lit lock card after docs/20 B1 (U3c, U6, U11; D7):
//   held   a G0 hold the lock swallows says so in big words, "没有在录" over "松开，点一下再
//          按住", where the clock was; Mando does not doze there (dozing reads "all is
//          well"): he tilts his head with a "?" and his sign says 没在录;
//   result how the last recording ended, counts only (one that ended while locked, or one
//          nobody has seen yet; one already seen unlocked is not shown again on the lock): "√ 已存 0:12" (Ok), "× 太短，未存"
//          (Warn), why it stopped by itself ("会议已停：电量低", "已到 5 分钟上限") or did
//          not start ("需要 SD 卡（插卡后重启）"); Mando sits up (存好了) or tilts (没听到);
//   chips  等你 N, then 录N and ↑N only when not zero (one name each, as the status bar),
//          or "√ 已同步" when both are zero and the Mac is there;
//   ready  on an armed card, G0 down shows "● 准备" in the ● slot from the press (D8);
//   motion only a dozing Mando's z's move.
#include <unity.h>

#include <string>

#include "lock_screen.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "rec_result.h"
#include "runtime.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"
#include "theme_mando.h"

using namespace cpa;
using namespace uitest;
using rec::RecRefusal;

void setUp() {}
void tearDown() {}

namespace {

constexpr int16_t kCardRight = 12 + 216 - 6;  // the card's right edge less its padding

AppState locked() {
  AppState s = demo_state();
  s.ui.locked = true;
  s.device.bat_mv = 3900;
  s.time.known = true;
  s.time.offset_ms = 1792590120000LL;  // 2026-10-21 13:42 UTC: the clock is in big digits
  return s;
}

int zs(const FakeCanvas& c) {
  int n = 0;
  for (const TextOp& t : c.texts) n += t.text == "z" || t.text == "Z" ? 1 : 0;
  return n;
}

int count(const FakeCanvas& c, const std::string& text) {
  int n = 0;
  for (const TextOp& t : c.texts) n += t.text == text ? 1 : 0;
  return n;
}

ArtId rest_art(theme::MandoAct a) { return theme::mando_art(theme::MandoSize::M46, a, theme::mando_rest_pose(a)); }

RecLast ended(bool saved, uint32_t ms, RecRefusal why = RecRefusal::None, RecMode mode = RecMode::Memo) {
  RecLast l;
  l.has = true;
  l.saved = saved;
  l.ms = ms;
  l.why = static_cast<uint8_t>(why);
  l.mode = mode;
  return l;
}

// Every text inside the card stays inside it.
void assert_inside(const FakeCanvas& c) {
  for (const TextOp& t : c.texts) {
    if (t.y < 8 || t.y > 100) continue;
    TEST_ASSERT_TRUE_MESSAGE(t.x + c.textWidth(t.text.c_str()) <= kCardRight, t.text.c_str());
  }
}

}  // namespace

// U3c, B1: "没有在录" in big (bold) words where the clock was, and how to record instead.
static void test_a_swallowed_hold_says_not_recording() {
  AppState s = locked();
  s.ui.lock_held = true;
  FakeCanvas c;
  render_lock_screen(c, s, true, 1000);
  const TextOp* big = c.find("没有在录");
  TEST_ASSERT_NOT_NULL_MESSAGE(big, c.dump().c_str());
  TEST_ASSERT_TRUE(big->style.fg == Color::Warn);
  TEST_ASSERT_EQUAL_INT(2, count(c, "没有在录"));  // bold: drawn twice a pixel apart
  TEST_ASSERT_TRUE(c.has("松开，点一下再按住"));
  TEST_ASSERT_TRUE(c.digit_ops.empty());  // the clock gives way
  assert_inside(c);
}

// docs/20 §5.3: dozing reads "all is well"; under a swallowed hold Mando tilts his head.
static void test_mando_does_not_doze_on_not_recording() {
  AppState s = locked();
  s.ui.lock_held = true;
  TEST_ASSERT_TRUE(lock_mando_act(s) == theme::MandoAct::NoVoice);
  FakeCanvas c;
  render_lock_screen(c, s, true, 1000);
  TEST_ASSERT_EQUAL_INT(0, zs(c));
  TEST_ASSERT_FALSE(c.has("打盹中"));
  TEST_ASSERT_TRUE(c.has("没在录"));
  TEST_ASSERT_TRUE(c.arts.size() == 1 && c.arts[0].id == rest_art(theme::MandoAct::NoVoice));
}

// Without a result or a held G0 the card is as before: Mando dozing by the clock.
static void test_otherwise_he_dozes_by_the_clock() {
  const AppState s = locked();
  TEST_ASSERT_TRUE(lock_mando_act(s) == theme::MandoAct::Doze);
  FakeCanvas c;
  render_lock_screen(c, s, true, 1000);
  TEST_ASSERT_EQUAL_INT(3, zs(c));
  TEST_ASSERT_TRUE(c.has("打盹中") && c.has("已锁屏"));
  TEST_ASSERT_FALSE(c.digit_ops.empty());
}

// U6: how the last recording ended, counts and reasons only (docs/13 §7.1: no names).
static void test_the_card_shows_how_it_ended() {
  const struct {
    RecLast last;
    const char* line;
    Color ink;
    const char* detail;
    theme::MandoAct act;
  } kCases[] = {
      {ended(true, 12000), "√ 已存 0:12", Color::Ok, "", theme::MandoAct::Hop},
      {ended(false, 400), "× 太短，未存", Color::Warn, "", theme::MandoAct::NoVoice},
      {ended(true, 2530000, RecRefusal::Battery, RecMode::Meeting), "√ 已存 42:10", Color::Ok, "会议已停：电量低",
       theme::MandoAct::Hop},
      {ended(true, 300000, RecRefusal::Cap), "√ 已存 5:00", Color::Ok, "已到 5 分钟上限", theme::MandoAct::Hop},
      {ended(false, 0, RecRefusal::NoSd), "× 没录上", Color::Warn, "需要 SD 卡（插卡后重启）",
       theme::MandoAct::NoVoice},
      {ended(false, 0, RecRefusal::Timeout), "× 收尾超时", Color::Warn, "已写下的段还在卡上",
       theme::MandoAct::NoVoice},
  };
  for (const auto& k : kCases) {
    AppState s = locked();
    s.rec.last = k.last;
    FakeCanvas c;
    render_lock_screen(c, s, true, 1000);
    const TextOp* line = c.find(k.line);
    TEST_ASSERT_NOT_NULL_MESSAGE(line, c.dump().c_str());
    TEST_ASSERT_TRUE_MESSAGE(line->style.fg == k.ink, k.line);
    if (k.detail[0] != '\0') TEST_ASSERT_TRUE_MESSAGE(c.has(k.detail), c.dump().c_str());
    TEST_ASSERT_TRUE_MESSAGE(lock_mando_act(s) == k.act, k.line);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, zs(c), k.line);
    TEST_ASSERT_FALSE(c.has("cpa") || c.has("架构设计"));  // never a project or session name
    assert_inside(c);
  }
}

// U21, B3: one name per count (录N, ↑N, as the status bar), none drawn at zero; "√ 已同步"
// once nothing waits and the Mac is there.
static void test_chips_name_each_count_once() {
  AppState s = locked();
  set_ready(s);
  FakeCanvas synced;
  render_lock_screen(synced, s, true, 1000);
  TEST_ASSERT_NOT_NULL_MESSAGE(synced.find("√ 已同步"), synced.dump().c_str());
  TEST_ASSERT_TRUE(synced.find("√ 已同步")->style.fg == Color::Ok);
  TEST_ASSERT_TRUE(synced.has("等你 2"));
  TEST_ASSERT_FALSE(synced.has("待传") || synced.has("↑0") || synced.has("录0"));
  s.rec.files_n = 3;
  s.outbox.pending = 2;
  FakeCanvas waiting;
  render_lock_screen(waiting, s, true, 1000);
  TEST_ASSERT_TRUE(waiting.has("录3") && waiting.has("↑2") && !waiting.has("已同步"));
  AppState offline = locked();
  FakeCanvas off;
  render_lock_screen(off, offline, true, 1000);
  TEST_ASSERT_FALSE(off.has("已同步") || off.has("↑0") || off.has("录0"));
  assert_inside(waiting);
}

// U19 (the review): unknown is not zero. Without session data the card has no 等你 chip
// ("等你 0" read as "nobody waits"); data that is not live is marked, as home does: the
// card's head says "已锁屏 · 离线" and the chip is not mustard. Long counts beside it fit.
static void test_the_need_chip_says_when_it_does_not_know() {
  AppState none = locked();
  none.sessions = SessionsView{};
  FakeCanvas c;
  render_lock_screen(c, none, true, 1000);
  TEST_ASSERT_FALSE_MESSAGE(c.has("等你"), c.dump().c_str());
  AppState away = locked();  // demo data, the Mac gone
  away.rec.files_n = 128;
  away.outbox.pending = 256;
  FakeCanvas off;
  render_lock_screen(off, away, true, 1000);
  TEST_ASSERT_TRUE_MESSAGE(off.has("已锁屏 · 离线"), off.dump().c_str());
  TEST_ASSERT_NOT_NULL(off.find("等你 2"));
  TEST_ASSERT_FALSE(off.find("等你 2")->style.fg == Color::Warn);
  TEST_ASSERT_TRUE(off.has("录128") && off.has("↑256"));
  assert_inside(off);
  AppState live = locked();
  set_ready(live);
  FakeCanvas on;
  render_lock_screen(on, live, true, 1000);
  TEST_ASSERT_TRUE(on.has("等你 2") && !on.has("离线"));
  TEST_ASSERT_TRUE(on.find("等你 2")->style.fg == Color::Warn);
  AppState handshake = locked();  // the Mac is there, its first snapshot not yet
  set_ready(handshake);
  handshake.sessions = SessionsView{};
  FakeCanvas wait;
  render_lock_screen(wait, handshake, true, 1000);
  TEST_ASSERT_FALSE_MESSAGE(wait.has("等你") || wait.has("离线"), wait.dump().c_str());
}

// D7: locking right after a recording that was seen unlocked (its toast) shows the card as
// ever, "已锁屏" and Mando dozing by the clock; one that ended while locked stays until seen.
static void test_a_result_seen_unlocked_is_not_shown_again() {
  AppState s = locked();
  s.rec.last = ended(true, 12000);
  s.rec.last.seen = true;
  TEST_ASSERT_TRUE(lock_mando_act(s) == theme::MandoAct::Doze);
  FakeCanvas c;
  render_lock_screen(c, s, true, 1000);
  TEST_ASSERT_FALSE(c.has("已存"));
  TEST_ASSERT_FALSE(c.digit_ops.empty());
  s.rec.last.locked = true;  // it ended while locked: shown while the card is lit, seen or not
  TEST_ASSERT_TRUE(lock_mando_act(s) == theme::MandoAct::Hop);
  RecRig x;  // through the runtime: a memo seen on home, then Opt+L
  x.memo(2000);
  for (int i = 0; i < 100; ++i) x.r.step(10);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.last.has && !x.r.rt.state().rec.last.locked);
  x.r.rt.shell().on_key(opt('l'), x.r.now);
  x.steps();
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("打盹中"), x.r.disp.canvas_.dump().c_str());
  TEST_ASSERT_FALSE(x.r.disp.canvas_.has("已存"));
}

// B3 (U6): locked there is no status bar: the card's battery says it is low, in the same
// colours (yellow under 30 %, red from 17 %, where recording stops).
static void test_the_card_battery_is_coloured_when_low() {
  const struct {
    uint16_t mv;
    const char* text;
    Color ink;
  } kCases[] = {{3900, "电 70%", Color::Ink2}, {3500, "电 23%", Color::Warn}, {3450, "电 17%", Color::Error}};
  for (const auto& k : kCases) {
    AppState s = locked();
    s.device.bat_mv = k.mv;
    FakeCanvas c;
    render_lock_screen(c, s, true, 1000);
    TEST_ASSERT_NOT_NULL_MESSAGE(c.find(k.text), c.dump().c_str());
    TEST_ASSERT_TRUE_MESSAGE(c.find(k.text)->style.fg == k.ink, k.text);
    TEST_ASSERT_TRUE(c.has("10/21 周三 · "));
  }
}

// D8: on an armed card G0 down shows "● 准备" from the press, in the ● slot, the card below it.
static void test_ready_from_the_press_on_an_armed_card() {
  AppState s = locked();
  s.ui.memo_ready = true;
  FakeCanvas c;
  render_lock_screen(c, s, true, 1000);
  const TextOp* mark = c.find("● 准备");
  TEST_ASSERT_NOT_NULL_MESSAGE(mark, c.dump().c_str());
  TEST_ASSERT_TRUE(mark->x == 4 && mark->y == 0 && mark->style.fg == Color::Warn && c.filled_at(0, Color::WarnBg));
  TEST_ASSERT_FALSE(c.filled_at(10, Color::Sheet));  // the card moved down, clear of it
  TEST_ASSERT_TRUE(c.filled_at(14, Color::Sheet));
}

// Through the runtime: lock, wait for the card to go, hold G0 (swallowed): "没有在录" while
// it is held and (the review) after it is up, until the card goes dark or a tap lights it
// again; nothing moves on the card meanwhile. A tap shows the card as before.
static void test_through_the_runtime() {
  RecRig x;
  RuntimeRig& r = x.r;
  r.rt.shell().on_key(opt('l'), r.now);
  for (int i = 0; i < 400; ++i) r.step(10);  // the 2 s card is out
  x.hold_g0(900);
  TEST_ASSERT_TRUE(r.rt.state().ui.lock_held);
  TEST_ASSERT_TRUE_MESSAGE(r.disp.canvas_.has("没有在录"), r.disp.canvas_.dump().c_str());
  const size_t pushes = r.disp.partials.size();
  x.hold_g0(1000);  // five ticks: no z climbs
  TEST_ASSERT_EQUAL_UINT32(pushes, r.disp.partials.size());
  x.release();
  x.steps();
  TEST_ASSERT_TRUE(r.rt.state().ui.lock_held);
  TEST_ASSERT_TRUE_MESSAGE(r.disp.canvas_.has("没有在录"), r.disp.canvas_.dump().c_str());
  r.step(10, KeySet{}.with(key::kG0));  // the tap the card asks for
  x.steps(8);
  TEST_ASSERT_FALSE(r.rt.state().ui.lock_held);
  TEST_ASSERT_FALSE(r.disp.canvas_.has("没有在录"));
  TEST_ASSERT_TRUE_MESSAGE(r.disp.canvas_.has("打盹中"), r.disp.canvas_.dump().c_str());
}

// The review's probe (stale_result): a memo saved while locked, its result flashed 2 s; ten
// minutes later the habitual hold from the dark is swallowed. Neither under the hold nor
// after it does the card show that old "√ 已存" with Mando's 存好了 (on a silent device it
// reads "my memo was saved"): 没有在录 until the card goes dark, and the old result goes
// with the hold the card answered (a tap then shows the clock).
static void test_a_swallowed_hold_never_shows_an_old_result() {
  RecRig x;
  RuntimeRig& r = x.r;
  r.unplug();
  r.rt.shell().on_key(opt('l'), r.now);
  for (int i = 0; i < 300; ++i) r.step(10);
  r.step(10, KeySet{}.with(key::kG0));  // tap, then hold: the memo
  r.step(10);
  r.step(800);
  x.hold_g0(600);
  x.port.feed(2000);
  x.let_go();  // gh #51: the grace runs out
  x.steps();
  TEST_ASSERT_TRUE(r.rt.state().rec.last.has && r.rt.state().rec.last.saved);
  r.step(600000);
  x.hold_g0(2000);
  TEST_ASSERT_TRUE_MESSAGE(r.disp.canvas_.has("没有在录"), r.disp.canvas_.dump().c_str());
  x.release();
  for (int i = 0; i < 30; ++i) {  // 0.3 s: the card is still lit
    r.step(10);
    TEST_ASSERT_FALSE_MESSAGE(r.disp.canvas_.has("已存") || r.disp.canvas_.has("存好了"), r.disp.canvas_.dump().c_str());
  }
  TEST_ASSERT_TRUE(r.rt.shell().lock_lit(r.now));
  TEST_ASSERT_TRUE(r.disp.canvas_.has("没有在录"));
  TEST_ASSERT_TRUE(lock_mando_act(r.rt.state()) == theme::MandoAct::NoVoice);
  r.step(3000);
  TEST_ASSERT_FALSE(r.rt.shell().lock_lit(r.now));
  r.step(10, KeySet{}.with(key::kG0));
  x.steps(8);
  TEST_ASSERT_FALSE_MESSAGE(r.disp.canvas_.has("已存"), r.disp.canvas_.dump().c_str());
  TEST_ASSERT_TRUE(r.disp.canvas_.has("打盹中"));
}

// docs/28 S1a: 留N while copies are kept; in English four chips are wider than the card, and
// the ones that do not fit are left out (the fuzz found "kept 2" past the edge).
static void test_kept_chip_and_the_card_edge() {
  AppState s = locked();
  s.rec.kept.segs = 3;
  FakeCanvas zh;
  render_lock_screen(zh, s, true, 1000);
  TEST_ASSERT_TRUE(zh.has("留3"));
  LangScope en(Lang::En);
  s.rec.files_n = 12;
  s.outbox.pending = 7;
  s.rec.kept.segs = 1264;
  FakeCanvas c;
  render_lock_screen(c, s, true, 1000);
  for (const TextOp& t : c.texts) {
    TEST_ASSERT_TRUE_MESSAGE(t.x + i18n::text_width(t.text.c_str()) <= 228, t.text.c_str());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_swallowed_hold_says_not_recording);
  RUN_TEST(test_mando_does_not_doze_on_not_recording);
  RUN_TEST(test_otherwise_he_dozes_by_the_clock);
  RUN_TEST(test_the_card_shows_how_it_ended);
  RUN_TEST(test_chips_name_each_count_once);
  RUN_TEST(test_a_result_seen_unlocked_is_not_shown_again);
  RUN_TEST(test_the_card_battery_is_coloured_when_low);
  RUN_TEST(test_ready_from_the_press_on_an_armed_card);
  RUN_TEST(test_through_the_runtime);
  RUN_TEST(test_a_swallowed_hold_never_shows_an_old_result);
  RUN_TEST(test_the_need_chip_says_when_it_does_not_know);
  RUN_TEST(test_kept_chip_and_the_card_edge);
  return UNITY_END();
}
