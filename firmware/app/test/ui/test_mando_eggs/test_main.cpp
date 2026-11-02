// Mando's easter eggs on the screens (docs/04, hidden keys; mando_eggs.h):
//   pat    Opt+W on home and on the lit lock card's clock: he hops with a heart, his line
//          and "pats ×N" where the greeting (home) or "Locked" (the card) was, for 1.5 s; then the
//          page as it was. Nowhere else: not on other pages, not over the editor or the menu,
//          never while recording, not on a dark or a result card. Silent: no toast, no hint.
//   mood   home's greeting at night and at the weekend; the lock card's sign, a fourth z
//          late at night, the early bird awake.
//   clear  the sessions needing you went from some to none with home in front: once, he
//          hops and the card says "All clear" (the greeting already says "all done"), never over
//          his "?" still on show.
#include <unity.h>

#include <string>

#include "core/support/fake_store.h"
#include "home_module.h"
#include "lock_screen.h"
#include "mando_eggs.h"
#include "rec_core.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_context.h"
#include "support/fake_io.h"
#include "support/fake_port.h"
#include "support/fake_rec_port.h"
#include "support/fixtures.h"
#include "support/runtime_rig.h"
#include "theme_mando.h"

using namespace cpa;
using namespace cpa::theme;
using namespace uitest;

void setUp() { set_lang(Lang::Zh); }
void tearDown() { set_lang(Lang::Zh); }

namespace {

// 2026-10-21 (Wednesday) 00:00 UTC + day days, hh:mm, UTC.
TimeInfo at_utc(int day, int hour, int minute) {
  TimeInfo t;
  t.known = true;
  t.offset_ms = 1792540800000LL + ((day * 24LL + hour) * 60 + minute) * 60000LL;
  return t;
}

AppState calm() {
  AppState s = demo_state();
  set_ready(s);
  set_sessions(s, {});
  s.time = at_utc(0, 14, 0);  // a Wednesday afternoon: no mood
  return s;
}

PatState patted(uint32_t n, uint32_t at_ms) {
  PatState p;
  for (uint32_t i = 0; i < n; ++i) p = pat_next(p, at_ms - (n - 1 - i));
  return p;
}

// -- the shell: where Opt+W pats --------------------------------------------------------

struct ShellRig {
  FakePort port;
  ProbeModule home{"home"};
  ProbeModule board{"board"};
  Module* mods[2] = {&home, &board};
  Shell shell{port, mods, 2};
  uint32_t now = 1000;
  ShellRig() {
    port.app = calm();
    shell.begin(now);
  }
  void key(const KeyEvent& ev, uint32_t gap = 100) {
    now += gap;
    shell.on_key(ev, now);
  }
  bool gate(const KeyEvent& ev, uint32_t gap = 100) {
    now += gap;
    return shell.lock_gate(ev, now);
  }
};

}  // namespace

static void test_opt_w_pats_on_home_silently() {
  ShellRig r;
  r.key(opt('w'));
  TEST_ASSERT_EQUAL_UINT32(1, r.shell.pat().n);
  TEST_ASSERT_EQUAL_UINT32(r.now, r.shell.pat().at_ms);
  TEST_ASSERT_TRUE(r.home.keys.empty());  // the shell's: home never sees it
  r.key(opt('w'));
  r.key(opt('w'));
  TEST_ASSERT_EQUAL_UINT32(3, r.shell.pat().n);
  r.shell.tick(r.now);
  TEST_ASSERT_TRUE(r.port.gestures.empty() && r.port.notes.empty());
}

static void test_opt_w_does_nothing_elsewhere() {
  ShellRig r;
  r.key(opt('p'));  // the board
  r.key(opt('w'));
  TEST_ASSERT_EQUAL_UINT32(0, r.shell.pat().n);
  r.key(opt('h'));
  r.key(opt('n'));  // the editor over home
  r.key(opt('w'));
  TEST_ASSERT_EQUAL_UINT32(0, r.shell.pat().n);
  r.key(chr('`'));
  r.key(nav(Nav::Menu));  // the Tab menu
  r.key(opt('w'));
  TEST_ASSERT_EQUAL_UINT32(0, r.shell.pat().n);
}

static void test_never_while_recording() {
  ShellRig r;
  r.port.app.rec.phase = RecPhase::Recording;
  r.port.app.rec.mode = RecMode::Meeting;
  r.key(opt('w'));
  TEST_ASSERT_EQUAL_UINT32(0, r.shell.pat().n);
  r.key(opt('l'));  // locked over the meeting, the card lit
  TEST_ASSERT_TRUE(r.shell.lock_lit(r.now));
  TEST_ASSERT_FALSE(r.gate(opt('w')));
  TEST_ASSERT_EQUAL_UINT32(0, r.shell.pat().n);
}

static void test_the_lit_lock_card_takes_a_pat_and_stays_lit_for_it() {
  ShellRig r;
  r.key(opt('l'));  // the card lit 2 s
  TEST_ASSERT_TRUE(r.shell.lock_lit(r.now));
  TEST_ASSERT_FALSE(r.gate(opt('w'), 1900));  // swallowed, as every other key
  TEST_ASSERT_EQUAL_UINT32(1, r.shell.pat().n);
  TEST_ASSERT_TRUE(r.shell.locked());
  r.shell.tick(r.now + kPatShowMs - 1);
  TEST_ASSERT_TRUE(r.shell.lock_lit(r.now + kPatShowMs - 1));  // lit while he hops
  r.now += 5000;
  r.shell.tick(r.now);
  TEST_ASSERT_FALSE(r.shell.lock_lit(r.now));
  TEST_ASSERT_FALSE(r.gate(opt('w')));  // dark: nothing
  TEST_ASSERT_EQUAL_UINT32(1, r.shell.pat().n);
  TEST_ASSERT_FALSE(r.shell.lock_lit(r.now));  // and it lights nothing
}

static void test_not_on_a_result_card() {
  ShellRig r;
  r.port.app.rec.last.has = true;  // how the last recording ended: no clock, no pat
  r.port.app.rec.last.saved = true;
  r.key(opt('l'));
  TEST_ASSERT_FALSE(r.gate(opt('w')));
  TEST_ASSERT_EQUAL_UINT32(0, r.shell.pat().n);
}

// The page goes back to what it was once the pat is over: a full redraw.
static void test_the_moment_ends_with_a_redraw() {
  ShellRig r;
  r.key(opt('w'));
  r.shell.clear_dirty();
  r.shell.tick(r.now + kPatShowMs - 1);
  TEST_ASSERT_FALSE(r.shell.dirty());
  r.shell.tick(r.now + kPatShowMs);
  TEST_ASSERT_TRUE(r.shell.dirty());
  r.shell.clear_dirty();
  r.shell.tick(r.now + kPatShowMs + 200);
  TEST_ASSERT_FALSE(r.shell.dirty());
}

// -- home --------------------------------------------------------------------------------

namespace {

struct HomeRig {
  FakeContext ctx;
  HomeModule home;
  FakeCanvas canvas;
  HomeRig() {
    ctx.app = calm();
    ctx.now = 5000;
    home.begin(ctx);
    home.onEnter(0);
  }
  void draw() {
    canvas.clear();
    home.render(canvas, kContentRect);
  }
  std::string greeting() const {
    FakeCanvas g;
    for (const TextOp& t : canvas.texts) {
      if (t.x >= 44) g.texts.push_back(t);
    }
    return g.line(15);
  }
  bool heart() const {
    for (const auto& f : canvas.fills) {
      if (f.second == tone_ink(Tone::Rec).ink && f.first.x >= 30 && f.first.y < 30) return true;
    }
    return false;
  }
};

}  // namespace

static void test_home_says_his_line_and_counts() {
  HomeRig r;
  r.draw();
  TEST_ASSERT_EQUAL_STRING("下午好， 都处理完了", r.greeting().c_str());
  TEST_ASSERT_FALSE(r.heart());
  r.ctx.app.ui.pat = patted(3, 5000);
  r.ctx.app.ui.pat.line = 1;  // "Hehe"
  r.draw();
  TEST_ASSERT_EQUAL_STRING("嘿嘿 摸 \xC3\x97" "3", r.greeting().c_str());
  TEST_ASSERT_TRUE(r.canvas.arts[0].id == mando_art(MandoSize::M46, MandoAct::Hop, 0));
  TEST_ASSERT_TRUE(r.heart());
  r.ctx.now = 5000 + kPatShowMs;
  r.draw();
  TEST_ASSERT_EQUAL_STRING("下午好， 都处理完了", r.greeting().c_str());
  TEST_ASSERT_FALSE(r.heart());
}

static void test_the_hundredth_pat_and_english() {
  HomeRig r;
  r.ctx.app.ui.pat = patted(100, 5000);
  r.draw();
  TEST_ASSERT_EQUAL_STRING("今天也辛苦了 摸 \xC3\x97" "100", r.greeting().c_str());
  set_lang(Lang::En);
  r.draw();
  TEST_ASSERT_EQUAL_STRING("You did great today pats \xC3\x97" "100", r.greeting().c_str());
  for (const TextOp& t : r.canvas.texts) TEST_ASSERT_TRUE_MESSAGE(t.x + r.canvas.textWidth(t.text.c_str()) <= 236, t.text.c_str());
}

// Home leaves the count out where line and count do not fit (as the lock card does).
static void test_home_leaves_out_a_count_that_does_not_fit() {
  set_lang(Lang::En);
  HomeRig r;
  r.ctx.app.ui.pat = PatState{4294967200u, 5000, 0};  // a rare pat with a very long count
  r.draw();
  TEST_ASSERT_EQUAL_STRING("You did great today", r.greeting().c_str());
  for (const TextOp& t : r.canvas.texts) TEST_ASSERT_TRUE_MESSAGE(t.x + r.canvas.textWidth(t.text.c_str()) <= 236, t.text.c_str());
  r.ctx.app.ui.pat = PatState{3, 5000, 2};  // Pat pat: just the number
  r.draw();
  TEST_ASSERT_EQUAL_STRING("Pat pat \xC3\x97" "3", r.greeting().c_str());
}

// A new pat starts the hop again, its heart up on the second tick.
static void test_each_pat_hops_again() {
  HomeRig r;
  r.ctx.app.ui.pat = patted(1, 5000);
  r.home.animate(r.canvas, kContentRect, 5000);
  r.ctx.now = 5600;
  r.canvas.clear();
  r.home.animate(r.canvas, kContentRect, 5600);
  TEST_ASSERT_TRUE(r.canvas.arts[0].id == mando_art(MandoSize::M46, MandoAct::Hop, 1));
  r.ctx.app.ui.pat = pat_next(r.ctx.app.ui.pat, 5700);
  r.ctx.now = 5800;
  r.canvas.clear();
  TEST_ASSERT_TRUE(r.home.animate(r.canvas, kContentRect, 5800).w > 0);
  TEST_ASSERT_TRUE(r.canvas.arts[0].id == mando_art(MandoSize::M46, MandoAct::Hop, 0));
  TEST_ASSERT_TRUE(r.heart());
}

static void test_home_greets_the_night_and_the_weekend() {
  TEST_ASSERT_EQUAL_STRING("夜深了", home_greeting(at_utc(0, 23, 45), 0));
  TEST_ASSERT_EQUAL_STRING("晚上好", home_greeting(at_utc(0, 23, 29), 0));
  TEST_ASSERT_EQUAL_STRING("早上好", home_greeting(at_utc(1, 5, 30), 0));  // the early bird: morning as ever
  TEST_ASSERT_EQUAL_STRING("周末好", home_greeting(at_utc(3, 10, 0), 0));  // Saturday
  TEST_ASSERT_EQUAL_STRING("夜深了", home_greeting(at_utc(3, 23, 50), 0));
  TEST_ASSERT_EQUAL_STRING("你好", home_greeting(TimeInfo{}, 0));
  set_lang(Lang::En);
  TEST_ASSERT_EQUAL_STRING("Late night", home_greeting(at_utc(0, 23, 45), 0));
  TEST_ASSERT_EQUAL_STRING("Weekend", home_greeting(at_utc(4, 15, 0), 0));
  HomeRig r;
  r.ctx.app = demo_state();
  set_ready(r.ctx.app);
  r.ctx.app.time = at_utc(4, 15, 0);
  r.draw();
  TEST_ASSERT_EQUAL_STRING("Weekend,  2  waiting for you", r.greeting().c_str());
}

static void test_all_clear_on_home() {
  HomeRig r;
  r.ctx.app.ui.moment = Moment::AllClear;
  r.ctx.app.ui.moment_ms = 5000;
  TEST_ASSERT_TRUE(home_mando(r.ctx.app, 5000) == MandoAct::Hop);
  r.draw();
  int signs = 0;  // the sign is in the card
  for (const TextOp& t : r.canvas.texts) signs += t.text == "搞定啦" && t.y > 12 + 18 && t.y < 12 + 46 ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(1, signs);
  int greetings = 0;  // "all done" once only: in the greeting, not again in the sign
  for (const TextOp& t : r.canvas.texts) greetings += t.text == "都处理完了" ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(1, greetings);
  TEST_ASSERT_NULL(r.canvas.find("没有等你的 session"));
  r.ctx.now = 5000 + kAllClearMs;
  TEST_ASSERT_TRUE(home_mando(r.ctx.app, r.ctx.now) == MandoAct::Idle);
  r.draw();
  TEST_ASSERT_NOT_NULL(r.canvas.find("没有等你的 session"));
}

// -- the lock card -----------------------------------------------------------------------

namespace {

AppState locked_at(const TimeInfo& t) {
  AppState s = calm();
  s.ui.locked = true;
  s.device.bat_mv = 3900;
  s.time = t;
  return s;
}

int zs(const FakeCanvas& c) {
  int n = 0;
  for (const TextOp& t : c.texts) n += t.text == "z" || t.text == "Z" ? 1 : 0;
  return n;
}

}  // namespace

static void test_lock_card_pat() {
  AppState s = locked_at(at_utc(0, 14, 0));
  s.ui.pat = patted(2, 9000);
  s.ui.pat.line = 0;  // "Woof!"
  FakeCanvas c;
  render_lock_screen(c, s, true, 9000);
  TEST_ASSERT_TRUE(lock_card_act(s, 9000) == MandoAct::Hop);
  TEST_ASSERT_TRUE(lock_mando_act(s) == MandoAct::Doze);  // still the clock card
  TEST_ASSERT_TRUE(c.arts[0].id == mando_art(MandoSize::M46, MandoAct::Hop, mando_rest_pose(MandoAct::Hop)));
  TEST_ASSERT_EQUAL_INT(0, zs(c));
  TEST_ASSERT_NULL(c.find("已锁屏"));
  TEST_ASSERT_EQUAL_STRING("汪! 摸 \xC3\x97" "2", c.line(8 + 6).c_str());
  TEST_ASSERT_FALSE(c.digit_ops.empty());  // the clock stays
  c.clear();
  render_lock_screen(c, s, true, 9000 + kPatShowMs);
  TEST_ASSERT_NOT_NULL(c.find("已锁屏"));
  TEST_ASSERT_EQUAL_INT(3, zs(c));
}

static void test_lock_card_rare_line_in_english_fits() {
  set_lang(Lang::En);
  AppState s = locked_at(at_utc(0, 14, 0));
  s.ui.pat = patted(100, 9000);
  FakeCanvas c;
  render_lock_screen(c, s, true, 9000);
  TEST_ASSERT_NOT_NULL(c.find("You did great today"));
  for (const TextOp& t : c.texts) {
    if (t.y < 8 || t.y > 100) continue;
    TEST_ASSERT_TRUE_MESSAGE(t.x + c.textWidth(t.text.c_str()) <= 12 + 216 - 6, t.text.c_str());
  }
}

static void test_lock_card_moods() {
  FakeCanvas c;
  render_lock_screen(c, locked_at(at_utc(0, 23, 45)), true, 0);  // late: "Bedtime" and a fourth z
  TEST_ASSERT_NOT_NULL(c.find("夜深了"));
  TEST_ASSERT_EQUAL_INT(4, zs(c));
  c.clear();
  const AppState early = locked_at(at_utc(1, 6, 0));  // the early bird is up: no z's
  render_lock_screen(c, early, true, 0);
  TEST_ASSERT_NOT_NULL(c.find("早起鸟"));
  TEST_ASSERT_EQUAL_INT(0, zs(c));
  TEST_ASSERT_TRUE(c.arts[0].id == mando_art(MandoSize::M46, MandoAct::Idle, 0));
  TEST_ASSERT_TRUE(lock_card_act(early, 0) == MandoAct::Idle);
  TEST_ASSERT_FALSE(c.digit_ops.empty());  // the clock as ever
  c.clear();
  render_lock_screen(c, locked_at(at_utc(3, 15, 0)), true, 0);
  TEST_ASSERT_NOT_NULL(c.find("周末啦"));
  TEST_ASSERT_EQUAL_INT(3, zs(c));
  c.clear();
  render_lock_screen(c, locked_at(TimeInfo{}), true, 0);  // the time unknown: as before
  TEST_ASSERT_NOT_NULL(c.find("打盹中"));
  TEST_ASSERT_EQUAL_INT(3, zs(c));
}

// A tick's z's: the fourth only late at night, inside the cells the tick redraws.
static void test_lock_z_part_draws_the_fourth_z() {
  FakeCanvas c;
  const AppState late = locked_at(at_utc(0, 23, 45));
  const Rect r = lock_z_part(c, late, 3, 0);
  TEST_ASSERT_EQUAL_INT(4, zs(c));
  for (const TextOp& t : c.texts) {
    if (t.text != "z" && t.text != "Z") continue;
    TEST_ASSERT_TRUE(t.x >= r.x && t.x + 6 <= r.x + r.w && t.y >= r.y && t.y + 12 <= r.y + r.h + 1);
  }
  c.clear();
  lock_z_part(c, late, 2, 0);
  TEST_ASSERT_EQUAL_INT(2, zs(c));
}

// -- all clear from the Mac --------------------------------------------------------------

namespace {

struct HomeRuntime {
  FakeTransport tx;
  testsupport::FakeStore fs;
  FakePlatform plat;
  FakeDisplay disp;
  ProbeModule home{"home"};
  ProbeModule inbox{"inbox"};
  Module* mods[2] = {&home, &inbox};
  Runtime rt{tx, fs, plat, disp, mods, 2};
  FakeRecPort port;
  uint32_t now = 0;
  int id = 1;
  // rec: the audio task on the host too (as RecRig), so a memo can be too short.
  explicit HomeRuntime(bool rec = false) {
    BootInfo b;
    b.ticket = test_ticket();
    b.dev = FixedStr<12>::from("020000000001");
    b.rst = FixedStr<16>::from("poweron");
    if (rec) {
      RecBoot rb;
      rb.buffers = true;
      rb.free_known = true;
      rb.free_bytes = 64ull << 30;
      rt.attach_rec(port, rb);
      StorageBoot sb;
      sb.plan = StoragePlan{Store::Sd, false, StoreNote::None};
      rt.begin(b, now, sb);
      port.boot(rt.ids(), true);
      step();
    } else {
      rt.begin(b, now);
    }
    send("hello", "\"role\":\"host\",\"app\":\"cpad/0.1.0\",\"agent\":\"ok\",\"caps\":{\"spawn\":true,\"reply\":false}");
    step();
  }
  void step(uint32_t ms = 10, KeySet keys = KeySet{}) {
    now += ms;
    rt.loop(now, keys);
  }
  // A memo of ms milliseconds of audio: G0 held, let go, its grace run out.
  void memo(uint32_t ms) {
    for (uint32_t t = 0; t < 600; t += 10) step(10, KeySet{}.with(key::kG0));
    port.feed(ms);
    for (uint32_t t = 0; t <= rec::kMemoGraceMs + 30; t += 10) step();
  }
  void send(const std::string& t, const std::string& fields) {
    tx.host("{\"v\":0,\"t\":\"" + t + "\",\"id\":\"h" + std::to_string(id++) + "\"," + fields + "}");
  }
  void sessions(bool need, int rev) {
    send("sessions.snapshot", "\"rev\":" + std::to_string(rev) +
                                  ",\"total\":1,\"sessions\":[{\"sid\":\"8d3e7a19\",\"slot\":1,\"name\":\"x\",\"kind\":"
                                  "\"tty\",\"st\":\"" + (need ? "waiting" : "busy") + "\",\"need\":" +
                                  (need ? "true" : "false") + ",\"age_s\":5}]");
    step();
  }
};

}  // namespace

static void test_all_clear_comes_once_on_home() {
  HomeRuntime r;
  r.sessions(true, 1);
  TEST_ASSERT_TRUE(r.rt.state().ui.moment == Moment::None);
  r.sessions(false, 2);
  TEST_ASSERT_TRUE(r.rt.state().ui.moment == Moment::AllClear);
  const uint32_t at = r.rt.state().ui.moment_ms;
  r.sessions(false, 3);  // still none: not again
  TEST_ASSERT_EQUAL_UINT32(at, r.rt.state().ui.moment_ms);
}

// A too-short memo's "?" is still on show: the all clear does not wipe it.
static void test_all_clear_never_wipes_his_question_mark() {
  HomeRuntime r(true);
  r.sessions(true, 1);
  r.memo(100);
  TEST_ASSERT_TRUE(r.rt.state().ui.moment == Moment::NoVoice);
  const uint32_t at = r.rt.state().ui.moment_ms;
  r.sessions(false, 2);
  TEST_ASSERT_TRUE(r.rt.state().ui.moment == Moment::NoVoice);
  TEST_ASSERT_EQUAL_UINT32(at, r.rt.state().ui.moment_ms);
}

static void test_no_all_clear_away_from_home() {
  HomeRuntime r;
  r.sessions(true, 1);
  r.rt.shell().on_key(opt('i'), r.now);  // the inbox in front
  r.sessions(false, 2);
  TEST_ASSERT_TRUE(r.rt.state().ui.moment == Moment::None);
  r.rt.shell().on_key(opt('h'), r.now);
  r.sessions(true, 3);
  r.rt.shell().on_key(opt('l'), r.now);  // locked over home: the card has its own ways
  r.sessions(false, 4);
  TEST_ASSERT_TRUE(r.rt.state().ui.moment == Moment::None);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_opt_w_pats_on_home_silently);
  RUN_TEST(test_opt_w_does_nothing_elsewhere);
  RUN_TEST(test_never_while_recording);
  RUN_TEST(test_the_lit_lock_card_takes_a_pat_and_stays_lit_for_it);
  RUN_TEST(test_not_on_a_result_card);
  RUN_TEST(test_the_moment_ends_with_a_redraw);
  RUN_TEST(test_home_says_his_line_and_counts);
  RUN_TEST(test_the_hundredth_pat_and_english);
  RUN_TEST(test_home_leaves_out_a_count_that_does_not_fit);
  RUN_TEST(test_each_pat_hops_again);
  RUN_TEST(test_home_greets_the_night_and_the_weekend);
  RUN_TEST(test_all_clear_on_home);
  RUN_TEST(test_lock_card_pat);
  RUN_TEST(test_lock_card_rare_line_in_english_fits);
  RUN_TEST(test_lock_card_moods);
  RUN_TEST(test_lock_z_part_draws_the_fourth_z);
  RUN_TEST(test_all_clear_comes_once_on_home);
  RUN_TEST(test_all_clear_never_wipes_his_question_mark);
  RUN_TEST(test_no_all_clear_away_from_home);
  return UNITY_END();
}
