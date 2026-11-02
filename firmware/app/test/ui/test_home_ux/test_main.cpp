// docs/20 B4 (U5, U8, U16, U19, U22): what home shows.
//   - the R and L tiles name the Opt chord they now need (D1);
//   - away from the Mac, home does not read as live: the greeting says it is offline, the
//     card says how old its picture is and its ages stop, Mando is faded;
//   - the most urgent card: waiting first (the longest wait), then idle newest first and
//     marked "idle"; idle a day or more is folded ("+N older", "+N older sessions");
//   - the G0 hint says to wait for ● REC (D8);
//   - when the device cannot record, the R tile is greyed and the G0 hint says why.
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "home_logic.h"
#include "home_module.h"
#include "support/fake_canvas.h"
#include "support/fake_context.h"
#include "support/fixtures.h"
#include "theme.h"
#include "theme_mando.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() { set_lang(Lang::Zh); }  // a failed English test skips its LangScope

namespace {

constexpr int16_t kUrgentY = 32;  // the card's first line
constexpr uint32_t kDayS = 86400;

struct Rig {
  FakeContext ctx;
  HomeModule home;
  FakeCanvas canvas;
  Rig() {
    ctx.app = demo_state();
    set_ready(ctx.app);
    ctx.app.rec.avail = true;
    home.begin(ctx);
    home.onEnter(0);
  }
  void draw() {
    canvas.clear();
    home.render(canvas, kContentRect);
  }
  std::string card() {
    draw();
    return canvas.row(kUrgentY) + "\n" + canvas.row(kUrgentY + 13);
  }
  // The greeting's words, right of Mando.
  std::string greeting() {
    draw();
    FakeCanvas g;
    for (const TextOp& t : canvas.texts) {
      if (t.x >= 44 && t.y == 15) g.texts.push_back(t);
    }
    return g.line(15);
  }
};

const TextOp* exact(const FakeCanvas& c, const char* text) {
  for (const TextOp& t : c.texts) {
    if (t.text == text) return &t;
  }
  return nullptr;
}

// The cable is out: the link is gone, the sessions are what the Mac last said.
void unplug(AppState& s, uint32_t last_rx_ms) {
  s.link.mode = LinkMode::Detached;
  s.link.last_rx_ms = last_rx_ms;
}

}  // namespace

// D1: R and L only work with Opt on home, so their tiles say so (and the Tab menu's rows).
static void test_r_and_l_tiles_show_their_opt_key() {
  Rig r;
  r.draw();
  const TextOp* rk = exact(r.canvas, "Opt+R");
  const TextOp* lk = exact(r.canvas, "Opt+L");
  TEST_ASSERT_NOT_NULL_MESSAGE(rk, r.canvas.dump().c_str());
  TEST_ASSERT_NOT_NULL(lk);
  TEST_ASSERT_NULL(exact(r.canvas, "R"));
  TEST_ASSERT_NULL(exact(r.canvas, "L"));
  const TextOp* meet = exact(r.canvas, "开会");
  const TextOp* lock = exact(r.canvas, "锁屏");
  TEST_ASSERT_TRUE(meet != nullptr && meet->y == rk->y && meet->x > rk->x);
  TEST_ASSERT_TRUE(lock != nullptr && lock->y == lk->y && lock->x > lk->x);
  for (const TextOp& t : r.canvas.texts) {  // still inside their tiles
    TEST_ASSERT_TRUE_MESSAGE(t.x + r.canvas.textWidth(t.text.c_str()) <= kScreenW - 4, t.text.c_str());
  }
  TEST_ASSERT_EQUAL_STRING("Opt+R", kHomeTiles[2].keys);
  TEST_ASSERT_EQUAL_STRING("Opt+L", kHomeTiles[7].keys);
  TEST_ASSERT_EQUAL_STRING("P", kHomeTiles[0].keys);
}

// U19: unplugged, the greeting does not say all is done, Mando is faded, and the card says
// how old the picture is.
static void test_offline_home_is_not_live() {
  Rig r;
  set_sessions(r.ctx.app, {}, 1000);
  unplug(r.ctx.app, 60000);
  r.ctx.now = 60000 + 9 * 60000;  // nine minutes since the Mac last spoke
  const std::string hello = r.greeting();
  TEST_ASSERT_TRUE_MESSAGE(hello.find("都处理完了") == std::string::npos, hello.c_str());
  TEST_ASSERT_TRUE_MESSAGE(hello.find("现在离线") != std::string::npos, hello.c_str());
  TEST_ASSERT_TRUE(home_mando(r.ctx.app, r.ctx.now) == theme::MandoAct::Offline);
  TEST_ASSERT_EQUAL_STRING("没有等你的 session\n离线 · 9 分钟前的情况", r.card().c_str());
  TEST_ASSERT_TRUE(r.canvas.find("离线 · 9 分钟前的情况")->style.fg == Color::Ink3);
}

// Offline, what waited stays on the card as it was: its age stops at the last word from the
// Mac, Mando does not pop his "!" for it, and the greeting does not count it as live.
static void test_offline_card_freezes_its_age() {
  Rig r;
  r.ctx.app.sessions.rx_ms = 0;  // 架构设计 waited 9 min then
  unplug(r.ctx.app, 60000);      // the Mac spoke a minute later, then the cable went
  r.ctx.now = 60000 + 30 * 60000;
  TEST_ASSERT_EQUAL_STRING("架构设计|10 分钟\n离线 · 30 分钟前的情况", r.card().c_str());
  r.ctx.now += 3600000;  // an hour more: the same picture, only older
  TEST_ASSERT_EQUAL_STRING("架构设计|10 分钟\n离线 · 2 小时前的情况", r.card().c_str());
  TEST_ASSERT_TRUE(home_mando(r.ctx.app, r.ctx.now) == theme::MandoAct::Offline);
  TEST_ASSERT_TRUE(r.greeting().find("件事") == std::string::npos);
}

// Faded: his sprite is dimmed where he sits, with no bubble.
static void test_offline_mando_is_faded() {
  Rig r;
  unplug(r.ctx.app, 0);
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.arts.size() >= 1);
  const ArtOp& m = r.canvas.arts[0];
  TEST_ASSERT_TRUE(m.id == theme::mando_art(theme::MandoSize::M46, theme::MandoAct::Offline, 0));
  bool faded = false;
  for (const Rect& d : r.canvas.dims) faded = faded || (d.x == m.x && d.y == m.y && d.w == 26 && d.h == 46);
  TEST_ASSERT_TRUE(faded);
  TEST_ASSERT_NULL(exact(r.canvas, "!"));  // no bubble for what may be over
  TEST_ASSERT_EQUAL_STRING("离线", theme::mando_sign(theme::MandoAct::Offline));
  set_ready(r.ctx.app);
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.dims.empty());  // online he is himself
}

// The cable is in and cpad does not answer: Mando sniffs, the card says what to run.
static void test_cpad_not_running_says_what_to_run() {
  Rig r;
  r.ctx.app.sessions = SessionsView{};
  r.ctx.app.link.mode = LinkMode::Attached;
  TEST_ASSERT_TRUE(home_mando(r.ctx.app, 0) == theme::MandoAct::Sniff);
  const std::string card = r.card();
  TEST_ASSERT_TRUE_MESSAGE(card.find("cpad 没在跑") != std::string::npos, card.c_str());
  TEST_ASSERT_TRUE_MESSAGE(card.find("cpa status") != std::string::npos, card.c_str());
  TEST_ASSERT_TRUE(r.greeting().find("都处理完了") == std::string::npos);
  r.ctx.app.link.mode = LinkMode::Detached;  // no cable: never synced, nothing to run yet
  TEST_ASSERT_TRUE(r.card().find("cpa status") == std::string::npos);
  TEST_ASSERT_TRUE(r.greeting().find("还没连上 Mac") != std::string::npos);
}

// In English the link word follows the greeting's comma: "Hello, no Mac yet", lower case,
// as "offline now" is.
static void test_english_greeting_keeps_the_link_word_lower_case() {
  LangScope en(Lang::En);
  Rig r;
  r.ctx.app.sessions = SessionsView{};
  r.ctx.app.link.mode = LinkMode::Detached;
  const std::string g = r.greeting();
  TEST_ASSERT_TRUE_MESSAGE(g.find("no Mac yet") != std::string::npos, g.c_str());  // its own text op
  TEST_ASSERT_TRUE_MESSAGE(g.find("No Mac yet") == std::string::npos, g.c_str());
}

// U22: waiting first, the longest wait; then idle newest first, marked "idle"; idle for a
// day or more is no one's most urgent and is counted: "+N older" beside the card's project
// (so "waiting" stays whole), "+N older sessions" when nothing else is on the card.
static void test_urgent_order_waiting_then_newest_idle() {
  Rig r;
  set_sessions(r.ctx.app, {session("aaaaaaa1", 2, "旧的支付重构", SessionKind::Tty, SessionState::Idle, true, 2 * kDayS),
                           session("aaaaaaa2", 1, "刚做完的录音修复", SessionKind::Tty, SessionState::Idle, true, 120),
                           session("aaaaaaa3", 1, "昨天下午的", SessionKind::Bg, SessionState::Idle, true, 5 * 3600)});
  TEST_ASSERT_EQUAL_INT(1, home_urgent(r.ctx.app, 0));
  TEST_ASSERT_EQUAL_STRING("刚做完的录音修复|2 分钟\ncpa · 空闲|+1 更早", r.card().c_str());
  TEST_ASSERT_TRUE(r.canvas.filled_at(kUrgentY + 3, Color::Warn) == false);  // idle: no mustard stripe
  set_sessions(r.ctx.app, {session("aaaaaaa1", 2, "旧的支付重构", SessionKind::Tty, SessionState::Idle, true, 2 * kDayS),
                           session("aaaaaaa2", 1, "刚做完的录音修复", SessionKind::Tty, SessionState::Idle, true, 120),
                           session("aaaaaaa4", 1, "新的权限请求", SessionKind::Tty, SessionState::Waiting, true, 5)});
  TEST_ASSERT_EQUAL_INT(2, home_urgent(r.ctx.app, 0));
  TEST_ASSERT_EQUAL_STRING("新的权限请求|5 秒\ncpa · 等你回复|+1 更早", r.card().c_str());
  set_sessions(r.ctx.app, {session("aaaaaaa1", 2, "旧的支付重构", SessionKind::Tty, SessionState::Idle, true, 2 * kDayS),
                           session("aaaaaaa5", 2, "上周的", SessionKind::Tty, SessionState::Idle, true, 7 * kDayS)});
  TEST_ASSERT_EQUAL_INT(-1, home_urgent(r.ctx.app, 0));
  const std::string folded = r.card();
  TEST_ASSERT_TRUE_MESSAGE(folded.find("+2 个更早的") != std::string::npos, folded.c_str());
  TEST_ASSERT_TRUE(folded.find("旧的支付重构") == std::string::npos);
}

// A day is counted on the card's clock: a session the Mac called 23 h idle, seen two hours
// ago and still listed, is folded now.
static void test_a_day_counts_from_now() {
  Rig r;
  set_sessions(r.ctx.app, {session("aaaaaaa1", 1, "x", SessionKind::Tty, SessionState::Idle, true, 23 * 3600)});
  TEST_ASSERT_EQUAL_INT(0, home_urgent(r.ctx.app, 0));
  TEST_ASSERT_EQUAL_INT(-1, home_urgent(r.ctx.app, 2 * 3600 * 1000));
}

// D8 (U8): the hint says to wait for ● REC before talking, and it fits as keycaps.
static void test_g0_hint_says_wait_for_rec() {
  Rig r;
  const std::string hint = r.home.hint();
  TEST_ASSERT_TRUE_MESSAGE(hint.find("G0按住") == 0, hint.c_str());
  TEST_ASSERT_TRUE_MESSAGE(hint.find("看到\xE2\x97\x8FREC再说") != std::string::npos, hint.c_str());
  TEST_ASSERT_TRUE_MESSAGE(hint.find("双击免提") != std::string::npos, hint.c_str());  // gh #51
  FakeCanvas c;
  TEST_ASSERT_TRUE_MESSAGE(theme::hint_fits(c, hint.c_str()), hint.c_str());
}

// U16: no usable SD card (or the battery is at the stop line): the R tile is greyed and the
// G0 hint says why, before a press fails.
static void test_no_recording_marks_r_and_g0() {
  Rig r;
  r.ctx.app.rec.avail = false;
  r.ctx.app.rec.sd_bad = true;
  r.draw();
  TEST_ASSERT_TRUE(exact(r.canvas, "开会")->style.fg == Color::Ink3);
  TEST_ASSERT_TRUE(exact(r.canvas, "记一条")->style.fg == Color::Ink);
  std::string hint = r.home.hint();
  TEST_ASSERT_TRUE_MESSAGE(hint.find("G0不能录") == 0, hint.c_str());
  TEST_ASSERT_TRUE_MESSAGE(hint.find("SD") != std::string::npos, hint.c_str());
  FakeCanvas c;
  TEST_ASSERT_TRUE_MESSAGE(theme::hint_fits(c, hint.c_str()), hint.c_str());
  r.ctx.app.rec.avail = true;
  r.ctx.app.rec.sd_bad = false;
  r.ctx.app.device.bat_mv = 3440;  // under the 3.45 V stop line
  hint = r.home.hint();
  TEST_ASSERT_TRUE_MESSAGE(hint.find("电量低") != std::string::npos, hint.c_str());
  r.ctx.app.device.bat_mv = 3900;
  r.ctx.app.rec.free_known = true;
  r.ctx.app.rec.free_mib = 4;  // under 8 MB: refused
  hint = r.home.hint();
  TEST_ASSERT_TRUE_MESSAGE(hint.find("卡满") != std::string::npos, hint.c_str());
  r.ctx.app.rec.free_mib = 4000;
  TEST_ASSERT_TRUE(std::string(r.home.hint()).find("看到") != std::string::npos);
  r.draw();
  TEST_ASSERT_TRUE(exact(r.canvas, "开会")->style.fg == Color::Ink);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_r_and_l_tiles_show_their_opt_key);
  RUN_TEST(test_offline_home_is_not_live);
  RUN_TEST(test_offline_card_freezes_its_age);
  RUN_TEST(test_offline_mando_is_faded);
  RUN_TEST(test_cpad_not_running_says_what_to_run);
  RUN_TEST(test_english_greeting_keeps_the_link_word_lower_case);
  RUN_TEST(test_urgent_order_waiting_then_newest_idle);
  RUN_TEST(test_a_day_counts_from_now);
  RUN_TEST(test_g0_hint_says_wait_for_rec);
  RUN_TEST(test_no_recording_marks_r_and_g0);
  return UNITY_END();
}
