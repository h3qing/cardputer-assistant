// docs/20 B5 (U7, U14, U19, U25): the inbox and the board.
//   - the hint says, row by row, whether Enter replies or only leaves a note on the Mac, and
//     the editor's title says the same; offline there is no Opt+D in it;
//   - a session's name is never "key/key/…", the inbox's detail lines give it whole, and the
//     reply confirm shows its end and a short id, so two alike sessions differ;
//   - a spawn whose result never came back is "开?" on the board;
//   - the board's overview names ` 返回;
//   - away from the Mac the inbox says how old its list is, its ages stop, and an empty one
//     does not say all is done.
#include <unity.h>

#include <string>

#include "board_module.h"
#include "confirm.h"
#include "inbox_module.h"
#include "reducers.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_context.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "theme.h"
#include "theme_mando.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

struct InboxRig {
  FakeContext ctx;
  InboxModule m;
  InboxRig() {
    ctx.app = demo_state();  // 架构设计 (tty, waiting), cpa/chord-tests (bg, idle)
    set_ready(ctx.app, true, true);
    m.begin(ctx);
    m.onEnter(0);
  }
  FakeCanvas draw() {
    FakeCanvas c;
    m.render(c, kContentRect);
    return c;
  }
};

struct BoardRig {
  FakeContext ctx;
  BoardModule m;
  BoardRig() {
    ctx.app = demo_state();
    set_ready(ctx.app, true, true);
    m.begin(ctx);
    m.onEnter(1);  // cpa, Sessions
  }
};

struct ShellRig {
  FakePort port;
  ProbeModule board{"board"};
  Module* mods[1] = {&board};
  Shell shell{port, mods, 1};
  ShellRig() {
    port.app = demo_state();
    set_ready(port.app, true, true);
    board.slot = 1;
    shell.begin(0);
  }
  void reply(const char* sid, uint32_t t) {
    ComposeSpec fb;
    fb.kind = ComposeKind::Feedback;
    fb.slot = 1;
    fb.sid = some(Sid::from(sid));
    shell.compose(fb);
    shell.on_key(chr('o'), t);
    shell.on_key(chr('k'), t);
  }
  FakeCanvas draw(uint32_t t) const {
    FakeCanvas c;
    shell.render(c, t);
    return c;
  }
};

void fits_bar(const char* hint) {
  FakeCanvas c;
  TEST_ASSERT_TRUE_MESSAGE(theme::hint_fits(c, hint), hint);
}

bool starts(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// Every text drawn at or below y, left to right and top to bottom, joined.
std::string below(const FakeCanvas& c, int16_t y) {
  std::string out;
  for (int16_t line = y; line < kHintRect.y; ++line) out += c.row(line);
  return out;
}

}  // namespace

// U14: a session the device started is named "key/slug" already; the key is not added twice.
static void test_a_session_label_never_doubles_the_key() {
  const AppState s = demo_state();
  TEST_ASSERT_EQUAL_STRING("cpa/chord-tests", session_label(s.projects, s.sessions.data.rows[1]).c_str());
  TEST_ASSERT_EQUAL_STRING("cpa/架构设计", session_label(s.projects, s.sessions.data.rows[0]).c_str());
  const SessionRow lookalike = session("aaaaaaa1", 1, "cpa-x", SessionKind::Tty, SessionState::Idle, true, 1);
  TEST_ASSERT_EQUAL_STRING("cpa/cpa-x", session_label(s.projects, lookalike).c_str());
  const SessionRow orphan = session("aaaaaaa2", 7, "x", SessionKind::Tty, SessionState::Idle, true, 1);
  TEST_ASSERT_EQUAL_STRING("?/x", session_label(s.projects, orphan).c_str());
  InboxRig r;
  const FakeCanvas c = r.draw();
  TEST_ASSERT_TRUE(c.has("cpa/chord-tests"));
  TEST_ASSERT_FALSE_MESSAGE(c.has("cpa/cpa/"), c.dump().c_str());
}

// U7: Enter delivers only to an idle background session with replies on; the hint says so
// for the selected row ("记给 Mac" otherwise), and it is the confirm box's own decision.
static void test_inbox_hint_says_if_a_reply_reaches() {
  InboxRig r;
  TEST_ASSERT_EQUAL_STRING("Enter记给Mac Opt+D忽略 `返回  Tab菜单", r.m.hint());  // 架构设计: a terminal
  fits_bar(r.m.hint());
  r.m.onKey(chr('.'));  // cpa/chord-tests: background, idle
  TEST_ASSERT_EQUAL_STRING("Enter回复 Opt+D忽略 `返回  Tab菜单", r.m.hint());
  r.ctx.app.link.cap_reply = false;  // cpad has replies switched off
  TEST_ASSERT_TRUE(starts(r.m.hint(), "Enter记给Mac"));
  for (bool cap : {false, true}) {
    r.ctx.app.link.cap_reply = cap;
    for (uint8_t i = 0; i < r.ctx.app.sessions.data.n; ++i) {
      ConfirmSpec spec;
      spec.kind = ConfirmKind::Feedback;
      spec.sid = some(r.ctx.app.sessions.data.rows[i].sid);
      const bool executes = confirm_decide(spec, r.ctx.app).route == Route::Execute;
      TEST_ASSERT_EQUAL(executes, reply_reaches(r.ctx.app, r.ctx.app.sessions.data.rows[i]));
    }
  }
}

// U19: offline Opt+D is refused, so the hint leaves it out; nothing reaches a session.
static void test_offline_hints_have_no_opt_d() {
  InboxRig r;
  r.ctx.app.link.mode = LinkMode::Detached;
  r.m.onKey(chr('.'));
  TEST_ASSERT_EQUAL_STRING("Enter记给Mac `返回  Tab菜单", r.m.hint());
  BoardRig b;
  b.ctx.app.link.mode = LinkMode::Detached;
  const std::string h = b.m.hint();
  TEST_ASSERT_TRUE_MESSAGE(h.find("Opt+D") == std::string::npos, h.c_str());
  fits_bar(h.c_str());
}

// The board's Sessions tab is the same list per project: the same per-row hint.
static void test_board_sessions_hint_per_row() {
  BoardRig r;
  TEST_ASSERT_EQUAL_STRING("Enter记给Mac Opt+D忽略 `返回  Tab菜单", r.m.hint());
  r.m.onKey(chr('.'));
  TEST_ASSERT_EQUAL_STRING("Enter回复 Opt+D忽略 `返回  Tab菜单", r.m.hint());
  r.m.onKey(chr('/'));  // 队列: its own hint
  TEST_ASSERT_TRUE(starts(r.m.hint(), "Enter开session"));
}

// U14: the detail lines under the list give the whole name (two lines when it needs them);
// a name that fits one leaves the second for what kind of session it is.
static void test_inbox_detail_has_the_whole_name() {
  InboxRig r;
  set_projects(r.ctx.app, {project(1, "cardputer-as", "Cardputer")});
  set_sessions(r.ctx.app, {session("aaaa0001", 1, "fix-flaky-refund-webhook-timeout", SessionKind::Tty,
                                   SessionState::Waiting, true, 60)});
  r.m.onEnter(0);
  const int16_t detail_y = static_cast<int16_t>(kContentRect.y + kContentRect.h - 2 * 12 - 3);
  FakeCanvas c = r.draw();
  const std::string whole = below(c, detail_y);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("cardputer-as/fix-flaky-refund-webhook-timeout", whole.c_str(), c.dump().c_str());
  set_sessions(r.ctx.app, {session("aaaa0001", 1, "short", SessionKind::Tty, SessionState::Waiting, true, 60)});
  r.m.onEnter(0);
  c = r.draw();
  TEST_ASSERT_EQUAL_STRING("cardputer-as/short", c.row(detail_y).c_str());
  TEST_ASSERT_EQUAL_STRING("终端 session：在 Mac 上处理", c.row(static_cast<int16_t>(detail_y + 12)).c_str());
}

// U19: unplugged, the inbox says how old its list is and its ages stop; empty, it does not
// say all is done and Mando is faded.
static void test_offline_inbox_says_how_old() {
  InboxRig r;
  r.ctx.app.link.mode = LinkMode::Detached;
  r.ctx.app.link.last_rx_ms = 60000;
  r.ctx.now = 60000 + 9 * 60000;
  FakeCanvas c = r.draw();
  TEST_ASSERT_TRUE_MESSAGE(c.has("离线 · 9 分钟前的情况"), c.dump().c_str());
  TEST_ASSERT_TRUE_MESSAGE(c.has("等待 10分钟"), c.dump().c_str());  // 540 s + the minute before the cable went
  r.ctx.now += 3600000;
  c = r.draw();
  TEST_ASSERT_TRUE(c.has("等待 10分钟"));
  set_sessions(r.ctx.app, {});
  c = r.draw();
  TEST_ASSERT_FALSE(c.has("都处理完了"));
  TEST_ASSERT_TRUE(c.has("离线"));
  TEST_ASSERT_TRUE(c.arts.size() == 1 && c.arts[0].id == theme::mando_art(theme::MandoSize::M46, theme::MandoAct::Offline, 0));
  r.ctx.app.sessions = SessionsView{};  // never synced since boot
  c = r.draw();
  TEST_ASSERT_TRUE(c.has("还没连上 Mac"));
  TEST_ASSERT_FALSE(c.has("等你 (0)"));
}

// U14: two sessions of one project that start alike: their confirm boxes differ, by the
// name's end and a short id (the sid's first 4, or all 8 when another session shares them).
static void test_reply_confirm_tells_alike_sessions_apart() {
  std::string seen[2];
  const char* sids[2] = {"aaaa0001", "aaaa0002"};
  for (int i = 0; i < 2; ++i) {
    ShellRig r;
    set_projects(r.port.app, {project(1, "cardputer-as", "Cardputer")});
    set_sessions(r.port.app, {session("aaaa0001", 1, "fix-flaky-refund-webhook-timeout", SessionKind::Bg,
                                      SessionState::Idle, true, 300),
                              session("aaaa0002", 1, "fix-flaky-refund-webhook-retries", SessionKind::Bg,
                                      SessionState::Idle, true, 60)});
    r.reply(sids[i], 1000);
    r.shell.on_key(enter(), 1000);
    TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
    const FakeCanvas c = r.draw(1000);
    seen[i] = c.dump();
    TEST_ASSERT_TRUE_MESSAGE(c.has(sids[i]), seen[i].c_str());
    for (const TextOp& t : c.texts) TEST_ASSERT_TRUE_MESSAGE(t.x + c.textWidth(t.text.c_str()) <= kScreenW, t.text.c_str());
  }
  TEST_ASSERT_TRUE(seen[0].find("webhook-timeout") != std::string::npos);
  TEST_ASSERT_TRUE(seen[1].find("webhook-retries") != std::string::npos);
  TEST_ASSERT_TRUE(seen[0] != seen[1]);
  AppState s = demo_state();
  TEST_ASSERT_EQUAL_STRING("8d3e", short_sid(s.sessions, Sid::from("8d3e7a19")).c_str());
}

// U7: the editor's title says where a reply goes before it is typed.
static void test_editor_title_says_where_a_reply_goes() {
  ShellRig r;
  r.reply("8d3e7a19", 1000);  // a terminal session: a note on the Mac
  FakeCanvas c = r.draw(1000);
  TEST_ASSERT_TRUE_MESSAGE(c.has("记给 Mac → cpa/架构设计"), c.dump().c_str());
  ShellRig b;
  b.reply("5c2e8a14", 1000);  // background and idle: delivered
  c = b.draw(1000);
  TEST_ASSERT_TRUE_MESSAGE(c.has("回复 → cpa/chord-tests"), c.dump().c_str());
  TEST_ASSERT_FALSE(c.has("cpa/cpa/"));
}

// U25: a spawn whose result never came back may already run: "开?" in mustard, not "开".
static void test_an_uncertain_spawn_is_marked() {
  BoardRig r;
  QueueRow unsure = queue_item("q18", 1, "write docs", Intent::Spawn);
  unsure.uncertain = true;
  set_queue(r.ctx.app, {queue_item("q17", 1, "add test", Intent::Spawn), unsure});
  r.m.onKey(chr('/'));
  FakeCanvas c;
  r.m.render(c, kContentRect);
  const TextOp* sure = c.find("add test");
  const TextOp* maybe = c.find("write docs");
  TEST_ASSERT_TRUE(sure != nullptr && maybe != nullptr);
  TEST_ASSERT_EQUAL_STRING("开", c.row(sure->y).substr(0, 3).c_str());
  const TextOp* mark = c.find("开?");
  TEST_ASSERT_NOT_NULL_MESSAGE(mark, c.dump().c_str());
  TEST_ASSERT_TRUE(mark->y == maybe->y && mark->style.fg == Color::Warn);
  TEST_ASSERT_TRUE(maybe->x >= mark->x + c.textWidth("开?") + 2);  // the text clear of the mark
  TEST_ASSERT_EQUAL_INT16(sure->x, maybe->x);
}

// U10: the board's overview names its back key, as every other list does.
static void test_board_overview_names_back() {
  BoardRig r;
  r.m.onEnter(0);
  const std::string h = r.m.hint();
  TEST_ASSERT_TRUE_MESSAGE(h.find("`返回") != std::string::npos, h.c_str());
  TEST_ASSERT_TRUE(h.compare(h.size() - 9, 9, "Tab菜单") == 0);
  fits_bar(h.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_session_label_never_doubles_the_key);
  RUN_TEST(test_inbox_hint_says_if_a_reply_reaches);
  RUN_TEST(test_offline_hints_have_no_opt_d);
  RUN_TEST(test_board_sessions_hint_per_row);
  RUN_TEST(test_inbox_detail_has_the_whole_name);
  RUN_TEST(test_offline_inbox_says_how_old);
  RUN_TEST(test_reply_confirm_tells_alike_sessions_apart);
  RUN_TEST(test_editor_title_says_where_a_reply_goes);
  RUN_TEST(test_an_uncertain_spawn_is_marked);
  RUN_TEST(test_board_overview_names_back);
  return UNITY_END();
}
