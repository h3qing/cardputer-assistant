// Mando and the greeting on home (docs/19 §6.3, §7.2, mockup 01). What he does, first match:
// a recording just saved (0.8 s) -> he hops; one just dropped as too short (2.5 s) -> he
// heard nothing ("?"); a session needs you -> head tilted with "!"; the outbox is syncing or
// the Mac answers but cpad does not -> he sniffs; otherwise he sits. The greeting follows the
// local hour, and says how many things wait for you.
#include <unity.h>

#include <string>

#include "home_logic.h"
#include "home_module.h"
#include "support/fake_canvas.h"
#include "support/fake_context.h"
#include "support/fixtures.h"
#include "theme_mando.h"

using namespace cpa;
using namespace cpa::theme;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

AppState calm() {  // the Mac answered: nothing waits
  AppState s = demo_state();
  set_ready(s);  // docs/20 U19: unplugged, Mando is faded (test_home_ux)
  set_sessions(s, {});
  return s;
}

TimeInfo at_local(int hour, int minute) {
  TimeInfo t;
  t.known = true;
  t.offset_ms = 1792540800000LL + (hour * 60LL + minute) * 60000LL;  // 2026-10-21 00:00 UTC + hh:mm
  t.tz_min = 0;
  return t;
}

}  // namespace

static void test_moments_come_first_and_pass() {
  AppState s = demo_state();  // two sessions need you
  set_ready(s);               // docs/20 U19: and the Mac says so now
  s.ui.moment = Moment::Saved;
  s.ui.moment_ms = 1000;
  TEST_ASSERT_TRUE(home_mando(s, 1000) == MandoAct::Hop);
  TEST_ASSERT_TRUE(home_mando(s, 1799) == MandoAct::Hop);
  TEST_ASSERT_TRUE(home_mando(s, 1800) == MandoAct::NeedsYou);
  s.ui.moment = Moment::NoVoice;
  TEST_ASSERT_TRUE(home_mando(s, 3499) == MandoAct::NoVoice);
  TEST_ASSERT_TRUE(home_mando(s, 3500) == MandoAct::NeedsYou);
  s.ui.moment = Moment::None;
  TEST_ASSERT_TRUE(home_mando(s, 1000) == MandoAct::NeedsYou);
}

static void test_sniffs_while_the_mac_is_busy() {
  AppState s = calm();
  TEST_ASSERT_TRUE(home_mando(s, 0) == MandoAct::Idle);
  s.link.mode = LinkMode::Ready;
  TEST_ASSERT_TRUE(home_mando(s, 0) == MandoAct::Idle);
  s.outbox.pending = 2;
  TEST_ASSERT_TRUE(home_mando(s, 0) == MandoAct::Sniff);
  s = calm();
  s.link.mode = LinkMode::Attached;  // the cable is in, cpad does not answer
  TEST_ASSERT_TRUE(home_mando(s, 0) == MandoAct::Sniff);
  s.link.version_mismatch = true;  // it answers in another version: nothing to wait for
  TEST_ASSERT_TRUE(home_mando(s, 0) == MandoAct::Offline);  // docs/20 U19: nothing he knows is live
}

static void test_greeting_follows_the_local_hour() {
  TEST_ASSERT_EQUAL_STRING("你好", home_greeting(TimeInfo{}, 0));
  const struct {
    int h, m;
    const char* want;
  } cases[] = {{4, 59, "夜深了"},  {5, 0, "早上好"},   {10, 59, "早上好"}, {11, 0, "中午好"},
               {12, 59, "中午好"}, {13, 0, "下午好"},  {17, 59, "下午好"}, {18, 0, "晚上好"},
               {23, 29, "晚上好"}, {23, 30, "夜深了"}, {0, 0, "夜深了"}};  // v0.5.0: late at night (mando_eggs.h)
  for (const auto& c : cases) TEST_ASSERT_EQUAL_STRING_MESSAGE(c.want, home_greeting(at_local(c.h, c.m), 0), c.want);
  TimeInfo east = at_local(1, 0);
  east.tz_min = 480;  // 01:00 UTC is 09:00 in UTC+8
  TEST_ASSERT_EQUAL_STRING("早上好", home_greeting(east, 0));
}

struct Rig {
  FakeContext ctx;
  HomeModule home;
  FakeCanvas canvas;
  Rig() {
    ctx.app = demo_state();
    set_ready(ctx.app);  // docs/20 U19: the Mac answers
    home.begin(ctx);
    home.onEnter(0);
  }
  void draw() {
    canvas.clear();
    home.render(canvas, kContentRect);
  }
  // The greeting's words: the line at y 15 right of Mando (S3: his bubble's mark, still low
  // before its first tick, shares the line).
  std::string greeting() const {
    FakeCanvas g;
    for (const TextOp& t : canvas.texts) {
      if (t.x >= 44) g.texts.push_back(t);
    }
    return g.line(15);
  }
};

static void test_mando_sits_top_left_with_his_bubble() {
  Rig r;
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.arts.size() >= 1);
  const ArtOp& m = r.canvas.arts[0];
  TEST_ASSERT_TRUE(m.x == 7 && m.y == 14);  // mockup 01
  TEST_ASSERT_TRUE(m.id == mando_art(MandoSize::M46, MandoAct::NeedsYou, 0));
  const TextOp* mark = r.canvas.find("!");
  TEST_ASSERT_NOT_NULL(mark);
  TEST_ASSERT_TRUE(mark->x == 33 && mark->style.fg == Color::OnAccent);
  r.ctx.app = calm();
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.arts[0].id == mando_art(MandoSize::M46, MandoAct::Idle, 0));
  TEST_ASSERT_NULL(r.canvas.find("!"));
  r.ctx.app.ui.moment = Moment::Saved;
  r.ctx.app.ui.moment_ms = r.ctx.now;
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.arts[0].id == mando_art(MandoSize::M46, MandoAct::Hop, 0));  // S3: a hop from its start
}

static void test_greeting_counts_what_waits() {
  Rig r;
  r.ctx.app.time = at_local(9, 41);
  r.draw();
  TEST_ASSERT_EQUAL_STRING("早上好， 2 件事 在等你", r.greeting().c_str());
  const TextOp* n = r.canvas.find("2 件事");
  TEST_ASSERT_TRUE(n->x == 44 + 48 && n->style.fg == Color::Warn);  // after "早上好，"
  TEST_ASSERT_TRUE(r.canvas.find("早上好，")->x == 44);
  r.ctx.app = calm();
  r.draw();
  TEST_ASSERT_EQUAL_STRING("你好， 都处理完了", r.greeting().c_str());
}

// S3 (docs/19 §7.3, §7.5): a tick redraws only Mando's box (bubble included), only when he
// moved; render() draws him as the last tick left him.
static void test_ticks_move_him_in_his_box() {
  Rig r;
  const Rect box{7, 13, 33, 47};
  Rect got = r.home.animate(r.canvas, kContentRect, 200);  // needs you: the "!" low first
  TEST_ASSERT_TRUE(got.x == box.x && got.y == box.y && got.w == box.w && got.h == box.h);
  TEST_ASSERT_EQUAL_INT16(15, r.canvas.find("!")->y);
  r.canvas.clear();
  got = r.home.animate(r.canvas, kContentRect, 400);  // then up
  TEST_ASSERT_EQUAL_INT16(box.w, got.w);
  TEST_ASSERT_EQUAL_INT16(13, r.canvas.find("!")->y);
  TEST_ASSERT_EQUAL_INT16(0, r.home.animate(r.canvas, kContentRect, 600).w);  // still: nothing
  r.draw();
  TEST_ASSERT_EQUAL_INT16(13, r.canvas.find("!")->y);  // as the last tick left it
}

static void test_a_saved_memo_hops_once() {
  Rig r;
  r.ctx.app = calm();
  TEST_ASSERT_EQUAL_INT16(0, r.home.animate(r.canvas, kContentRect, 100).w);  // sitting
  r.ctx.app.ui.moment = Moment::Saved;
  r.ctx.app.ui.moment_ms = 150;
  for (uint8_t pose = 0; pose < 4; ++pose) {
    r.canvas.clear();
    const uint32_t now = 200 + 200u * pose;
    r.ctx.now = now;
    TEST_ASSERT_TRUE(r.home.animate(r.canvas, kContentRect, now).w > 0);
    TEST_ASSERT_TRUE(r.canvas.arts[0].id == mando_art(MandoSize::M46, MandoAct::Hop, pose));
  }
  r.canvas.clear();
  r.ctx.now = 1000;
  TEST_ASSERT_TRUE(r.home.animate(r.canvas, kContentRect, 1000).w > 0);  // back to sitting
  TEST_ASSERT_TRUE(r.canvas.arts[0].id == mando_art(MandoSize::M46, MandoAct::Idle, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_moments_come_first_and_pass);
  RUN_TEST(test_sniffs_while_the_mac_is_busy);
  RUN_TEST(test_greeting_follows_the_local_hour);
  RUN_TEST(test_mando_sits_top_left_with_his_bubble);
  RUN_TEST(test_greeting_counts_what_waits);
  RUN_TEST(test_ticks_move_him_in_his_box);
  RUN_TEST(test_a_saved_memo_hops_once);
  return UNITY_END();
}
