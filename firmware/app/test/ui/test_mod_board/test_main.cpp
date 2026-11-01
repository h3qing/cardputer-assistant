// Board module (04 US-06, US-08, 05 §5.4): home list with the "其他" row, project
// page with Sessions / 队列 tabs, Enter and Opt+D actions, offline greying.
#include <unity.h>

#include <string>
#include <vector>

#include "board_logic.h"
#include "board_module.h"
#include "confirm.h"
#include "support/fake_canvas.h"
#include "theme.h"
#include "theme_palette.h"
#include "support/fake_context.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

struct Rig {
  FakeContext ctx;
  BoardModule board;
  FakeCanvas canvas;
  Rig() {
    ctx.app = demo_state();
    board.begin(ctx);
    board.onEnter(0);
  }
  std::string draw() {
    canvas.clear();
    board.render(canvas, kContentRect);
    return canvas.dump();
  }
};

// docs/19 §6.4: one rounded card per project and one for 其他; counts are pills.
static void test_home_lists_projects_counts_and_other() {
  Rig r;
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.has("cpa"));
  TEST_ASSERT_TRUE(r.canvas.has("Cardputer 助手"));
  const TextOp* run = r.canvas.find("跑1");
  TEST_ASSERT_TRUE(run && r.canvas.has("等1") && r.canvas.has("队2"));
  TEST_ASSERT_TRUE(run->y == r.canvas.find("等1")->y && run->x < r.canvas.find("等1")->x);  // one row
  TEST_ASSERT_TRUE(run->style.fg == Color::Ok);
  TEST_ASSERT_TRUE(r.canvas.has("博客"));
  TEST_ASSERT_TRUE(r.canvas.has("其他"));
  TEST_ASSERT_TRUE(r.canvas.has("共1"));
  TEST_ASSERT_EQUAL_UINT8(3, board_row_count(BoardUi{}, r.ctx.app));
  TEST_ASSERT_TRUE(r.canvas.filled_at(29, Color::Live));    // cursor on card 0: outlined
  TEST_ASSERT_TRUE(r.canvas.filled_at(30, Color::Select));
  TEST_ASSERT_TRUE(r.canvas.filled_at(60, Color::Card));    // the others plain
  TEST_ASSERT_TRUE(r.canvas.filled_at(60, Color::CardRim));  // with their lighter top edge
}

// The title line opens with a sentence; zero counts are plain text, not pills.
static void test_title_sentence_and_zero_counts() {
  ProjectsView v;
  TEST_ASSERT_EQUAL_STRING("", board_sentence(v).c_str());
  v.live = true;
  v.data.n = 2;
  v.data.rows[0].run = 1;
  v.data.rows[0].need = 1;
  v.data.rows[1].run = 1;
  TEST_ASSERT_EQUAL_STRING("2 个在跑，1 个在等你", board_sentence(v).c_str());
  v.data.rows[0].need = 0;
  v.data.other.run = 3;
  TEST_ASSERT_EQUAL_STRING("5 个在跑", board_sentence(v).c_str());
  Rig r;
  set_ready(r.ctx.app);
  r.draw();
  TEST_ASSERT_TRUE_MESSAGE(r.canvas.line(14).find("项目 ") == 0, r.canvas.line(14).c_str());
  TEST_ASSERT_TRUE(r.canvas.line(14).find("个在跑") != std::string::npos);
  const TextOp* zero = r.canvas.find("等0");
  TEST_ASSERT_NOT_NULL(zero);
  TEST_ASSERT_TRUE(zero->style.fg == Color::Ink3);
}

static void test_cached_names_show_dashes_and_staleness() {
  Rig r;
  r.ctx.app.projects.live = false;  // names from /cfg only, after a reboot
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.has("Cardputer 助手"));
  TEST_ASSERT_TRUE(r.canvas.has("--"));
  Rig s;
  s.ctx.app.projects.rx_ms = 0;
  s.ctx.now = 5 * 60 * 1000;  // offline with the last live data from 5 minutes ago
  s.draw();
  TEST_ASSERT_TRUE(s.canvas.has("更新于 5 分钟前"));
  // review (U22): the board's age is the one Chinese, rounded form of every other page
  s.ctx.now = (47 * 60 + 59) * 60 * 1000;  // 47 h 59 min
  s.draw();
  TEST_ASSERT_TRUE_MESSAGE(s.canvas.has("更新于 2 天前"), s.canvas.dump().c_str());
  s.ctx.now = (60 + 59) * 60 * 1000;  // 1 h 59 min
  s.draw();
  TEST_ASSERT_TRUE_MESSAGE(s.canvas.has("更新于 2 小时前"), s.canvas.dump().c_str());
}

static void test_agent_missing_banner() {
  Rig r;
  set_ready(r.ctx.app, false);
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.has("Mac 上未找到 Claude Code"));
}

// 04 US-04 AC3 / E7: the home page says why nothing updates, ahead of the age banner.
static void test_version_mismatch_banner() {
  Rig r;
  r.ctx.app.link.mode = LinkMode::Attached;
  r.ctx.app.link.version_mismatch = true;
  r.ctx.now = 5 * 60 * 1000;
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.has("固件和 Mac 端版本不兼容，请更新"));
  TEST_ASSERT_FALSE(r.canvas.has("分钟前"));
  TEST_ASSERT_TRUE(r.canvas.has("cpa"));  // the cached list is still shown
  r.ctx.app.link.version_mismatch = false;
  r.draw();
  TEST_ASSERT_FALSE(r.canvas.has("不兼容"));
}

static void test_home_enter_opens_project_and_cursor_clamps() {
  Rig r;
  TEST_ASSERT_TRUE(r.board.onKey(chr('.')));
  TEST_ASSERT_TRUE(r.board.onKey(enter()));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.opened.size());
  TEST_ASSERT_EQUAL_STRING("board:2", r.ctx.opened[0].c_str());
  for (int i = 0; i < 5; ++i) r.board.onKey(chr('.'));
  r.board.onKey(enter());  // on "其他": no page
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.opened.size());
  for (int i = 0; i < 5; ++i) r.board.onKey(chr(';'));
  r.board.onKey(enter());
  TEST_ASSERT_EQUAL_STRING("board:1", r.ctx.opened.back().c_str());
  TEST_ASSERT_FALSE(r.board.onKey(chr('`')));  // back: the shell pops
  TEST_ASSERT_FALSE(r.board.onKey(esc()));
  TEST_ASSERT_EQUAL_UINT8(0, r.board.contextSlot());
}

static void test_project_page_tabs_and_lists() {
  Rig r;
  r.board.onEnter(1);
  TEST_ASSERT_EQUAL_UINT8(1, r.board.contextSlot());
  r.draw();
  // docs/20 U22: one name for a session; the confirm box's "先看 Sessions" (U25) names this tab
  TEST_ASSERT_TRUE(r.canvas.has("Sessions(2)"));
  TEST_ASSERT_TRUE(std::string(confirm_note(Decision{Route::Execute, QueueReason::None, true})).find("Sessions") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(r.canvas.has("队列(2)"));
  TEST_ASSERT_TRUE(r.canvas.has("架构设计"));
  TEST_ASSERT_TRUE(r.canvas.has("等待 9分钟 终端"));  // docs/20 U22, tight in a list row
  TEST_ASSERT_TRUE(r.canvas.has("空闲 1分钟 后台"));
  TEST_ASSERT_TRUE(r.board.onKey(chr('/')));
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.has("add LittleFS"));  // offline: cut before its 离线 pill
  TEST_ASSERT_FALSE(r.canvas.has("架构设计"));
  TEST_ASSERT_TRUE(r.board.onKey(chr(',')));
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.has("架构设计"));
}

static void test_session_enter_replies_and_opt_d_dismisses_online_only() {
  Rig r;
  r.board.onEnter(1);
  r.board.onKey(chr('.'));
  r.board.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.composed.size());
  TEST_ASSERT_TRUE(r.ctx.composed[0].kind == ComposeKind::Feedback);
  TEST_ASSERT_TRUE(r.ctx.composed[0].sid.value.equals("5c2e8a14"));
  TEST_ASSERT_EQUAL_UINT8(1, r.ctx.composed[0].slot);
  r.board.onKey(opt('d'));  // offline: greyed
  TEST_ASSERT_EQUAL_UINT32(0, r.ctx.requests.size());
  TEST_ASSERT_TRUE(r.ctx.toasted("离线"));
  set_ready(r.ctx.app);
  r.board.onKey(opt('d'));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.requests.size());
  TEST_ASSERT_TRUE(r.ctx.requests[0].kind == RequestKind::InboxDismiss);
  TEST_ASSERT_TRUE(r.ctx.requests[0].sid.equals("5c2e8a14"));
}

static void test_queue_item_enter_spawns_with_qid_and_opt_d_marks_done() {
  Rig r;
  r.board.onEnter(1);
  r.board.onKey(chr('/'));
  r.board.onKey(enter());  // offline: greyed
  TEST_ASSERT_EQUAL_UINT32(0, r.ctx.confirmed.size());
  TEST_ASSERT_TRUE(r.ctx.toasted("离线"));
  set_ready(r.ctx.app, false);
  r.board.onKey(enter());  // no Claude: greyed
  TEST_ASSERT_EQUAL_UINT32(0, r.ctx.confirmed.size());
  TEST_ASSERT_TRUE(r.ctx.toasted("Claude"));
  set_ready(r.ctx.app);
  r.board.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.confirmed.size());
  TEST_ASSERT_TRUE(r.ctx.confirmed[0].qid.value.equals("q17"));
  TEST_ASSERT_TRUE(r.ctx.confirmed[0].kind == ConfirmKind::Spawn);
  TEST_ASSERT_EQUAL_UINT8(1, r.ctx.confirmed[0].slot);
  r.board.onKey(chr('.'));
  r.board.onKey(opt('d'));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.requests.size());
  TEST_ASSERT_TRUE(r.ctx.requests[0].kind == RequestKind::QueueDone);
  TEST_ASSERT_TRUE(r.ctx.requests[0].qid.equals("q18"));
  r.ctx.accept_requests = false;  // one already in flight
  r.board.onKey(opt('d'));
  TEST_ASSERT_TRUE(r.ctx.toasted("稍后"));
}

// What a text is drawn on: its cell background, or for glyphs only the last fill under it.
static Color ground_of(const FakeCanvas& c, const TextOp& t) {
  if (t.style.bg != t.style.fg) return t.style.bg;
  Color g = Color::Bg;
  for (const auto& f : c.fills) {
    const Rect& q = f.first;
    if (q.x <= t.x + 3 && t.x + 3 < q.x + q.w && q.y <= t.y + 6 && t.y + 6 < q.y + q.h) g = f.second;
  }
  return g;
}

// Offline the queue's items are grey; the selected one reads at >= 4.5:1 (Ink2 on Select:
// Ink3 there is 3.5:1) and says why it does nothing with a 离线 pill.
static void test_offline_items_are_drawn_grey() {
  Rig r;
  r.board.onEnter(1);
  r.board.onKey(chr('/'));
  r.draw();
  const TextOp* item = r.canvas.find("write docs");
  TEST_ASSERT_NOT_NULL(item);
  TEST_ASSERT_TRUE(item->style.fg == Color::Dim);
  const TextOp* sel = r.canvas.find("add LittleFS");
  TEST_ASSERT_TRUE(sel->style.fg == Color::Ink2 && ground_of(r.canvas, *sel) == Color::Select);
  TEST_ASSERT_TRUE(theme::contrast(sel->style.fg, Color::Select) >= 4.5);
  const TextOp* why = r.canvas.find("离线");
  TEST_ASSERT_NOT_NULL(why);
  TEST_ASSERT_EQUAL_INT16(sel->y, why->y);
  set_ready(r.ctx.app);
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.find("write docs")->style.fg != Color::Dim);
  TEST_ASSERT_FALSE(r.canvas.has("离线"));
}

// docs/19 §6.4 (the judges' list rows): 14 px rows, the text a pixel down in the 14 px
// selection capsule and 8 px in from its ends.
static void test_list_rows_breathe() {
  Rig r;
  r.board.onEnter(1);
  r.draw();
  const TextOp* first = r.canvas.find("架构设计");
  const TextOp* second = r.canvas.find("空闲 1分钟 后台");  // docs/20 U22
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL_INT16(8, first->x);
  bool capsule = false;
  for (const auto& f : r.canvas.fills) {
    if (f.second == Color::Select && f.first.h == 14 && f.first.y + 1 == first->y) capsule = true;
  }
  TEST_ASSERT_TRUE(capsule);
  TEST_ASSERT_EQUAL_INT16(first->y + 14, second->y);
  TEST_ASSERT_EQUAL_INT16(kScreenW - 8, second->x + r.canvas.textWidth(second->text.c_str()));
}

static void test_empty_states() {
  FakeContext ctx;
  BoardModule b;
  b.begin(ctx);
  b.onEnter(0);
  FakeCanvas c;
  b.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("cpa project add"));
  ctx.app = demo_state();
  b.onEnter(2);
  c.clear();
  b.render(c, kContentRect);
  TEST_ASSERT_TRUE(c.has("没有 session"));
  TEST_ASSERT_TRUE(b.onKey(enter()));  // nothing to act on, still handled
  TEST_ASSERT_EQUAL_UINT32(0, ctx.composed.size());
}

// v0.4.0 (docs/13 §5.6, §6.2): each page's 本页 rows for the Tab menu replay real keys
// (the menu presses them), and every hint ends in "Tab菜单" within the 240 px bar.
static bool has_row(const std::vector<HelpRow>& rows, const KeyEvent& ev) {
  for (const HelpRow& r : rows) {
    if (r.ev == ev && r.keys[0] != 0 && help_label(r)[0] != 0) return true;
  }
  return false;
}

static bool has_label(const std::vector<HelpRow>& rows, const char* word) {
  for (const HelpRow& r : rows) {
    if (std::string(help_label(r)).find(word) != std::string::npos) return true;
  }
  return false;
}

// The hint ends in Tab菜单 and fits the bar as keycaps at its roomiest spacing (docs/19 §4.2:
// a tighter one reads crowded). docs/20 U10: the overview's names ` 返回 as well, so it takes
// the second spacing (pad 2).
static void assert_hint(const Module& m, int16_t pad = 3) {
  const std::string h = m.hint();
  FakeCanvas c;
  TEST_ASSERT_TRUE_MESSAGE(h.size() >= 9 && h.compare(h.size() - 9, 9, "Tab菜单") == 0, h.c_str());
  TEST_ASSERT_TRUE_MESSAGE(theme::hint_fits(c, h.c_str()), h.c_str());
  theme::hint_bar(c, h.c_str());
  TEST_ASSERT_TRUE_MESSAGE(c.texts.size() > 2 && c.texts[0].text == "Enter", h.c_str());
  TEST_ASSERT_EQUAL_INT16_MESSAGE(4 + 34 + pad, c.texts[1].x, h.c_str());  // [Enter] at x 4, its words pad px on
}

static void test_help_rows_and_hints() {
  Rig r;
  std::vector<HelpRow> rows = help_rows(r.board);
  TEST_ASSERT_TRUE(has_row(rows, chr(';')) && has_row(rows, chr('.')) && has_row(rows, enter()));
  TEST_ASSERT_FALSE(has_row(rows, opt('d')));  // nothing to dismiss on the list of projects
  assert_hint(r.board, 2);
  r.board.onEnter(1);  // project 1, Sessions
  rows = help_rows(r.board);
  for (const KeyEvent& k : {enter(), opt('d'), chr('/'), chr('`')}) TEST_ASSERT_TRUE(has_row(rows, k));
  TEST_ASSERT_TRUE(has_label(rows, "回复"));
  assert_hint(r.board);
  r.board.onKey(chr('/'));  // 队列
  rows = help_rows(r.board);
  TEST_ASSERT_TRUE(has_row(rows, enter()) && has_row(rows, opt('d')));
  TEST_ASSERT_TRUE(has_label(rows, "开 session"));
  TEST_ASSERT_TRUE(has_label(rows, "切到 Sessions"));
  assert_hint(r.board);
}

// docs/20 U12: the cursor keeps its session or queue item through a new snapshot; once it
// is gone, Enter and Opt+D do nothing until the cursor moves; the toast names the item.
static void test_queue_cursor_follows_the_item() {
  Rig r;
  set_ready(r.ctx.app);
  r.board.onEnter(1);
  r.board.onKey(chr('/'));
  r.board.onKey(chr('.'));  // q18 "write docs"
  set_queue(r.ctx.app, {queue_item("q19", 1, "new on top"), queue_item("q17", 1, "add LittleFS fault-injection test"),
                        queue_item("q18", 1, "write docs")});
  r.board.onKey(opt('d'));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.requests.size());
  TEST_ASSERT_TRUE(r.ctx.requests[0].qid.equals("q18"));
  TEST_ASSERT_TRUE(r.ctx.toasted("已完成：write docs"));
  r.draw();
  // row 2 of the list (docs/19 §6.4: 14 px rows under the tabs), its middle
  TEST_ASSERT_TRUE(r.canvas.filled_at(static_cast<int16_t>(kContentRect.y + 2 * 12 + 2 + 2 * 14 + 7), Color::Select));
  set_queue(r.ctx.app, {queue_item("q19", 1, "new on top"), queue_item("q17", 1, "add LittleFS fault-injection test")});
  r.board.onKey(opt('d'));
  r.board.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.requests.size());  // q18 left: nothing selected
  TEST_ASSERT_EQUAL_UINT32(0, r.ctx.confirmed.size());
  TEST_ASSERT_TRUE(r.ctx.hinted("选中的那条已经不在了：按 ; . 选一条"));  // review: says why
  r.board.onKey(chr(';'));
  r.board.onKey(enter());
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.confirmed.size());
}

static void test_session_cursor_follows_the_session() {
  Rig r;
  set_ready(r.ctx.app);
  r.board.onEnter(1);
  r.board.onKey(chr('.'));  // 5c2e8a14, the second session of cpa
  set_sessions(r.ctx.app, {session("5c2e8a14", 1, "cpa/chord-tests", SessionKind::Bg, SessionState::Idle, true, 75),
                           session("8d3e7a19", 1, "架构设计", SessionKind::Tty, SessionState::Waiting, true, 540)});
  r.board.onKey(opt('d'));
  TEST_ASSERT_TRUE(r.ctx.requests.back().sid.equals("5c2e8a14"));
  TEST_ASSERT_TRUE(r.ctx.toasted("已忽略：cpa/chord-tests"));
}

static void test_home_cursor_follows_the_project() {
  Rig r;
  r.board.onKey(chr('.'));  // blog, slot 2
  // a new project lands between them: blog moves down a row
  set_projects(r.ctx.app, {project(1, "cpa", "Cardputer 助手"), project(3, "zzz", "新"), project(2, "blog", "博客")});
  r.board.onKey(enter());
  TEST_ASSERT_EQUAL_STRING("board:2", r.ctx.opened.back().c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_home_lists_projects_counts_and_other);
  RUN_TEST(test_title_sentence_and_zero_counts);
  RUN_TEST(test_cached_names_show_dashes_and_staleness);
  RUN_TEST(test_agent_missing_banner);
  RUN_TEST(test_version_mismatch_banner);
  RUN_TEST(test_home_enter_opens_project_and_cursor_clamps);
  RUN_TEST(test_project_page_tabs_and_lists);
  RUN_TEST(test_session_enter_replies_and_opt_d_dismisses_online_only);
  RUN_TEST(test_queue_item_enter_spawns_with_qid_and_opt_d_marks_done);
  RUN_TEST(test_offline_items_are_drawn_grey);
  RUN_TEST(test_list_rows_breathe);
  RUN_TEST(test_empty_states);
  RUN_TEST(test_help_rows_and_hints);
  RUN_TEST(test_queue_cursor_follows_the_item);
  RUN_TEST(test_session_cursor_follows_the_session);
  RUN_TEST(test_home_cursor_follows_the_project);
  return UNITY_END();
}
