// Inbox module (04 US-07): waiting first, then idle, longest wait first; Enter replies,
// Opt+D dismisses (online only); the attach hint is shown for background sessions
// only, interactive ones say "在 Mac 上处理" (docs/20 U14: the name is on the line above).
#include <unity.h>

#include <string>

#include "inbox_logic.h"
#include "inbox_module.h"
#include "theme_mando.h"
#include "support/fake_canvas.h"
#include "support/fake_context.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

static AppState mixed() {
  AppState s = demo_state();
  set_sessions(s, {session("aaaaaaa1", 1, "idle-short", SessionKind::Bg, SessionState::Idle, true, 70, "s1"),
                   session("aaaaaaa2", 1, "busy", SessionKind::Bg, SessionState::Busy, false, 10, "s2"),
                   session("aaaaaaa3", 2, "wait-short", SessionKind::Tty, SessionState::Waiting, true, 30),
                   session("aaaaaaa4", 1, "idle-long", SessionKind::Bg, SessionState::Idle, true, 900, "s4"),
                   session("aaaaaaa5", 2, "wait-long", SessionKind::Tty, SessionState::Waiting, true, 600)});
  return s;
}

static void test_order_waiting_first_then_longest_wait() {
  const AppState s = mixed();
  const InboxRows rows = inbox_rows(s);
  TEST_ASSERT_EQUAL_UINT8(4, rows.n);
  TEST_ASSERT_TRUE(s.sessions.data.rows[rows.index[0]].name.equals("wait-long"));
  TEST_ASSERT_TRUE(s.sessions.data.rows[rows.index[1]].name.equals("wait-short"));
  TEST_ASSERT_TRUE(s.sessions.data.rows[rows.index[2]].name.equals("idle-long"));
  TEST_ASSERT_TRUE(s.sessions.data.rows[rows.index[3]].name.equals("idle-short"));
}

static void test_attach_hint_only_for_background_sessions() {
  const SessionRow bg = session("5c2e8a14", 1, "chord", SessionKind::Bg, SessionState::Idle, true, 75, "a1b2c3");
  TEST_ASSERT_EQUAL_STRING("claude attach a1b2c3", inbox_detail(bg).c_str());
  const SessionRow tty = session("8d3e7a19", 1, "架构设计", SessionKind::Tty, SessionState::Waiting, true, 540);
  TEST_ASSERT_EQUAL_STRING("终端 session：在 Mac 上处理", inbox_detail(tty).c_str());  // docs/20 U14
  SessionRow bg_no_short = bg;
  bg_no_short.short_id = Opt<ShortId>{};
  TEST_ASSERT_TRUE(std::string(inbox_detail(bg_no_short).c_str()).find("attach") == std::string::npos);
}

static void test_render_title_rows_and_detail_of_selection() {
  FakeContext ctx;
  ctx.app = mixed();
  InboxModule m;
  m.begin(ctx);
  m.onEnter(0);
  FakeCanvas c;
  m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("等你 (4)"));
  TEST_ASSERT_TRUE(c.has("blog/wait-long"));
  TEST_ASSERT_TRUE(c.has("等待 10分钟"));  // docs/20 U22, tight: the detail line tells the kind
  TEST_ASSERT_TRUE(c.has("终端 session：在 Mac 上处理"));  // docs/20 U14: under the whole name
  TEST_ASSERT_EQUAL_STRING("blog/wait-long", c.row(static_cast<int16_t>(kContentRect.y + kContentRect.h - 27)).c_str());
  TEST_ASSERT_FALSE(c.has("claude attach"));
  m.onKey(chr('.'));
  m.onKey(chr('.'));  // idle-long, background
  c.clear();
  m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("claude attach s4"));
  TEST_ASSERT_FALSE(c.has("在 Mac 上处理"));
  TEST_ASSERT_EQUAL_UINT8(1, m.contextSlot());
}

static void test_enter_replies_and_opt_d_dismisses_online_only() {
  FakeContext ctx;
  ctx.app = mixed();
  InboxModule m;
  m.begin(ctx);
  m.onEnter(0);
  TEST_ASSERT_TRUE(m.onKey(enter()));
  TEST_ASSERT_EQUAL_UINT32(1, ctx.composed.size());
  TEST_ASSERT_TRUE(ctx.composed[0].kind == ComposeKind::Feedback);
  TEST_ASSERT_TRUE(ctx.composed[0].sid.value.equals("aaaaaaa5"));
  TEST_ASSERT_EQUAL_UINT8(2, ctx.composed[0].slot);
  TEST_ASSERT_TRUE(m.onKey(opt('d')));
  TEST_ASSERT_EQUAL_UINT32(0, ctx.requests.size());
  TEST_ASSERT_TRUE(ctx.toasted("离线"));
  set_ready(ctx.app);
  m.onKey(opt('d'));
  TEST_ASSERT_EQUAL_UINT32(1, ctx.requests.size());
  TEST_ASSERT_TRUE(ctx.requests[0].kind == RequestKind::InboxDismiss);
  TEST_ASSERT_TRUE(ctx.requests[0].sid.equals("aaaaaaa5"));
  TEST_ASSERT_FALSE(m.onKey(chr('`')));  // back: the shell pops
}

static void test_empty_inbox_and_cursor_follows_shrinking_list() {
  FakeContext ctx;
  ctx.app = mixed();
  InboxModule m;
  m.begin(ctx);
  m.onEnter(0);
  for (int i = 0; i < 6; ++i) m.onKey(chr('.'));
  ctx.app = demo_state();  // now only 2 need you
  FakeCanvas c;
  m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("等你 (2)"));
  // docs/20 U12: the selected session (idle-short) left the list: nothing is selected,
  // so Enter replies to no one until the cursor moves (it no longer clamps to a row)
  TEST_ASSERT_TRUE(m.onKey(enter()));
  TEST_ASSERT_TRUE(ctx.composed.empty());
  m.onKey(chr('.'));
  TEST_ASSERT_TRUE(m.onKey(enter()));
  TEST_ASSERT_TRUE(ctx.composed.back().sid.value.equals("5c2e8a14"));  // the last row
  ctx.app = AppState{};
  set_ready(ctx.app);
  set_sessions(ctx.app, {});  // docs/20 U19: the Mac says none (never synced: "还没连上 Mac")
  c.clear();
  m.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("没有等你的 session"));
  TEST_ASSERT_TRUE(m.onKey(enter()));
  TEST_ASSERT_EQUAL_UINT8(0, m.contextSlot());
}

// docs/19 §6.4 (the judges' list rows): 14 px rows, the text a pixel down, 8 px in; a session
// that waits for you has the home card's 3 px mustard stripe, not a plain "!".
static void test_rows_breathe_and_waiting_has_a_stripe() {
  FakeContext ctx;
  ctx.app = mixed();
  InboxModule m;
  m.begin(ctx);
  m.onEnter(0);
  FakeCanvas c;
  m.render(c, kContentRect);
  const TextOp* first = c.find("blog/wait-long");
  const TextOp* third = c.find("idle-long");
  TEST_ASSERT_TRUE(first != nullptr && third != nullptr);
  TEST_ASSERT_EQUAL_INT16(third->y - 28, first->y);
  TEST_ASSERT_TRUE(first->x > 8 && first->x == third->x);
  TEST_ASSERT_TRUE(first->text.find('!') == std::string::npos);
  int stripes = 0;
  for (const auto& f : c.fills) {
    if (f.second == Color::Warn && f.first.w == 3 && f.first.x == 8) ++stripes;
  }
  TEST_ASSERT_EQUAL_INT(2, stripes);  // wait-long and wait-short
  const TextOp* right = c.find("等待 10分钟");  // docs/20 U22: tight, the detail line tells the kind
  TEST_ASSERT_EQUAL_INT16(kScreenW - 8, right->x + c.textWidth(right->text.c_str()));
}

// Nothing waits: Mando sits with his 在呢 sign, and the page says so.
static void test_an_empty_inbox_has_mando() {
  FakeContext ctx;
  set_ready(ctx.app);
  set_sessions(ctx.app, {});  // docs/20 U19: the Mac says so (offline he is faded: test_inbox_board_ux)
  InboxModule m;
  m.begin(ctx);
  m.onEnter(0);
  FakeCanvas c;
  m.render(c, kContentRect);
  TEST_ASSERT_EQUAL_UINT32(1, c.arts.size());
  TEST_ASSERT_TRUE(c.arts[0].id == theme::mando_art(theme::MandoSize::M46, theme::MandoAct::Idle, 0));
  TEST_ASSERT_TRUE(c.has("在呢") && c.has("都处理完了"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_order_waiting_first_then_longest_wait);
  RUN_TEST(test_attach_hint_only_for_background_sessions);
  RUN_TEST(test_render_title_rows_and_detail_of_selection);
  RUN_TEST(test_enter_replies_and_opt_d_dismisses_online_only);
  RUN_TEST(test_empty_inbox_and_cursor_follows_shrinking_list);
  RUN_TEST(test_rows_breathe_and_waiting_has_a_stripe);
  RUN_TEST(test_an_empty_inbox_has_mando);
  return UNITY_END();
}
