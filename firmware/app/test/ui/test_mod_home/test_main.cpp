// Home launcher (docs/13 §5.6, v0.4.0): a 3 x 3 grid P Board / I Inbox / R Meet;
// N Note / Q AI queue / S Session; M Settings / L Lock / Tab Menu. A tile only presses a
// key for the user (ModuleContext::press): its letter or Enter presses Opt+letter, the
// Tab tile presses Tab, 1-9 press Opt+1-9. docs/20 D1: R and L need Opt (their letters
// alone press nothing), and a letter that is no tile hints once per visit. ; . , / move
// with wrap. Badges come from
// AppState (kRunningTile, "!N"); a card names the most urgent session needing you (docs/19 §6.3;
// Mando and the greeting: test_home_mando).
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "home_logic.h"
#include "home_module.h"
#include "support/fake_canvas.h"
#include "support/fake_context.h"
#include "support/fixtures.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

struct Rig {
  FakeContext ctx;
  HomeModule home;
  FakeCanvas canvas;
  Rig() {
    ctx.app = demo_state();
    set_ready(ctx.app);        // docs/20 U19: the Mac answers (unplugged, home shows its picture as old)
    ctx.app.rec.avail = true;  // docs/20 U16: the device can record (else R is greyed)
    home.begin(ctx);
    home.onEnter(0);
  }
  void draw() {
    canvas.clear();
    home.render(canvas, kContentRect);
  }
};

static const char kLetters[] = "pirnqsml";
static bool opt_only(char c) { return c == 'r' || c == 'l'; }  // docs/20 D1
static constexpr int16_t kUrgentY = 32;  // the card's first line (mockup 01: card at y 30)

static uint8_t sel_after(const char* keys) {
  HomeUi ui;
  for (const char* k = keys; *k; ++k) ui = home_next(ui, chr(*k)).ui;
  return ui.sel;
}

static void test_each_tile_letter_presses_its_opt_chord_once() {
  for (const char* c = kLetters; *c; ++c) {
    if (opt_only(*c)) continue;  // docs/20 D1: test_r_and_l_need_opt
    Rig r;
    TEST_ASSERT_TRUE_MESSAGE(r.home.onKey(chr(*c)), c);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, r.ctx.pressed.size(), c);
    TEST_ASSERT_TRUE_MESSAGE(r.ctx.pressed[0] == opt(*c), c);
  }
  Rig r;
  TEST_ASSERT_TRUE(r.home.onKey(chr('P')));  // the tile shows the capital
  TEST_ASSERT_TRUE(r.ctx.pressed.back() == opt('p'));
}

static void test_digits_press_opt_digit() {
  Rig r;
  for (char d = '1'; d <= '9'; ++d) {
    TEST_ASSERT_TRUE(r.home.onKey(chr(d)));
    TEST_ASSERT_TRUE(r.ctx.pressed.back() == opt(d));
  }
  TEST_ASSERT_EQUAL_UINT32(9, r.ctx.pressed.size());
  TEST_ASSERT_FALSE(r.home.onKey(chr('0')));  // Opt+0 opens nothing
  TEST_ASSERT_EQUAL_UINT32(9, r.ctx.pressed.size());
}

// ; up, . down, , left, / right; each wraps within its row or column.
static void test_moves_wrap_within_the_grid() {
  TEST_ASSERT_EQUAL_UINT8(0, sel_after(""));
  TEST_ASSERT_EQUAL_UINT8(6, sel_after(";"));
  TEST_ASSERT_EQUAL_UINT8(3, sel_after("."));
  TEST_ASSERT_EQUAL_UINT8(2, sel_after(","));
  TEST_ASSERT_EQUAL_UINT8(1, sel_after("/"));
  TEST_ASSERT_EQUAL_UINT8(8, sel_after(";,"));
  TEST_ASSERT_EQUAL_UINT8(2, sel_after(";,."));   // down from the bottom row
  TEST_ASSERT_EQUAL_UINT8(6, sel_after(";,/"));   // right from the last column
  TEST_ASSERT_EQUAL_UINT8(0, sel_after("..."));   // a full column
  TEST_ASSERT_EQUAL_UINT8(0, sel_after(",,,"));   // a full row
  const HomeStep st = home_next(HomeUi{}, chr('.'));
  TEST_ASSERT_TRUE(st.handled);
  TEST_ASSERT_TRUE(st.press.kind == KeyEventKind::None);
}

static void test_enter_equals_the_selected_tiles_key() {
  for (uint8_t i = 0; i < kHomeTileCount; ++i) {
    HomeUi ui;
    ui.sel = i;
    const HomeStep st = home_next(ui, enter());
    TEST_ASSERT_TRUE(st.handled);
    TEST_ASSERT_TRUE(st.press == kHomeTiles[i].ev);
    if (i < 8 && !opt_only(kLetters[i])) {  // docs/20 D1: R and L only by Opt or Enter
      TEST_ASSERT_TRUE(home_next(HomeUi{}, chr(kLetters[i])).press == st.press);
    }
  }
  TEST_ASSERT_TRUE(kHomeTiles[8].ev == nav(Nav::Menu));
}

static void test_tab_tile_presses_the_menu_key() {
  Rig r;
  r.home.onKey(chr(';'));
  r.home.onKey(chr(','));
  TEST_ASSERT_TRUE(r.ctx.pressed.empty());
  TEST_ASSERT_TRUE(r.home.onKey(enter()));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.pressed.size());
  TEST_ASSERT_TRUE(r.ctx.pressed[0] == nav(Nav::Menu));
  r.home.onEnter(0);  // back on the home page: P again
  r.home.onKey(enter());
  TEST_ASSERT_TRUE(r.ctx.pressed.back() == opt('p'));
}

static void test_back_and_other_keys_go_to_the_shell() {
  Rig r;
  TEST_ASSERT_FALSE(r.home.onKey(chr('`')));
  TEST_ASSERT_FALSE(r.home.onKey(bksp()));
  TEST_ASSERT_FALSE(r.home.onKey(esc()));
  TEST_ASSERT_FALSE(r.home.onKey(chr('x')));
  TEST_ASSERT_FALSE(r.home.onKey(opt('d')));
  TEST_ASSERT_FALSE(r.home.onKey(nav(Nav::Menu)));  // the shell's own key
  TEST_ASSERT_TRUE(r.ctx.pressed.empty());
  TEST_ASSERT_EQUAL_UINT8(0, r.home.contextSlot());
  TEST_ASSERT_EQUAL_STRING("home", r.home.id());
}

// docs/20 D1 (U5): on home r and l alone press nothing ("remind me…" no longer opens the
// meeting box, "lunch" no longer locks); Opt+R and Opt+L are the shell's own keys, and
// Enter on their tiles still presses them.
static void test_r_and_l_need_opt() {
  for (char c : {'r', 'l', 'R', 'L'}) {
    Rig r;
    r.home.onKey(chr(c));
    TEST_ASSERT_TRUE_MESSAGE(r.ctx.pressed.empty(), std::string(1, c).c_str());
  }
  for (uint8_t tile : {uint8_t{2}, uint8_t{7}}) {
    HomeUi ui;
    ui.sel = tile;
    TEST_ASSERT_TRUE(home_next(ui, enter()).press == kHomeTiles[tile].ev);
  }
}

// docs/20 D1: a letter that is no tile ("buy milk" typed on home) says once per visit how
// to write a note; the tiles still work in between.
static void test_a_non_tile_letter_hints_once_per_visit() {
  Rig r;
  for (char c : {'b', 'u', 'y', 'r'}) r.home.onKey(chr(c));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.hints.size());
  TEST_ASSERT_TRUE_MESSAGE(r.ctx.hinted("按 G0 记一条"), r.ctx.hints[0].c_str());
  TEST_ASSERT_TRUE(r.ctx.pressed.empty());
  r.home.onKey(chr('i'));
  TEST_ASSERT_TRUE(r.ctx.pressed.size() == 1 && r.ctx.pressed[0] == opt('i'));
  r.home.onKey(chr('x'));
  TEST_ASSERT_EQUAL_UINT32(1, r.ctx.hints.size());
  r.home.onEnter(0);  // home again: one more hint
  r.home.onKey(chr('l'));
  TEST_ASSERT_EQUAL_UINT32(2, r.ctx.hints.size());
  for (const KeyEvent& k : {chr('1'), chr(';'), chr('`'), bksp(), chr(' ')}) r.home.onKey(k);
  TEST_ASSERT_EQUAL_UINT32(2, r.ctx.hints.size());  // only letters
}

// A refused press (one already queued, or this key is itself a replay) is still the
// home page's key: nothing else happens.
static void test_refused_press_is_still_handled() {
  Rig r;
  r.ctx.accept_press = false;
  TEST_ASSERT_TRUE(r.home.onKey(chr('i')));
  TEST_ASSERT_TRUE(r.home.onKey(enter()));
  TEST_ASSERT_TRUE(r.ctx.pressed.empty());
  TEST_ASSERT_TRUE(r.ctx.opened.empty());
  TEST_ASSERT_TRUE(r.ctx.composed.empty());
}

// The text drawn exactly as `text` (find() also matches longer texts).
static const TextOp* exact(const FakeCanvas& c, const char* text) {
  for (const TextOp& t : c.texts) {
    if (t.text == text) return &t;
  }
  return nullptr;
}

// docs/19 §6.3: each tile is [key] label; the selected one is a caramel tile.
static void test_grid_labels_and_selection() {
  Rig r;
  r.draw();
  const char* tiles[] = {"P 项目", "I 等你", "R 开会", "N 记一条", "Q 排队给 AI", "S 开 session",  // docs/20 U22
                         "M 设置", "L 锁屏", "Tab 菜单"};
  std::string lines;
  for (int16_t y = kContentRect.y; y < kContentRect.y + kContentRect.h; ++y) lines += r.canvas.line(y) + "\n";
  for (const char* t : tiles) TEST_ASSERT_TRUE_MESSAGE(lines.find(t) != std::string::npos, t);
  const TextOp* p = exact(r.canvas, "项目");
  TEST_ASSERT_TRUE(r.canvas.filled_at(p->y, Color::Accent));
  TEST_ASSERT_TRUE(p->style.fg == Color::OnAccent);
  TEST_ASSERT_TRUE(exact(r.canvas, "等你")->style.fg == Color::Ink);
  r.home.onKey(chr('/'));
  r.draw();
  TEST_ASSERT_TRUE(exact(r.canvas, "等你")->style.fg == Color::OnAccent);
  TEST_ASSERT_TRUE(exact(r.canvas, "项目")->style.fg == Color::Ink);
}

static void test_badges_running_and_need() {
  Rig r;
  set_projects(r.ctx.app, {project(1, "cpa", "Cardputer 助手", 2), project(2, "blog", "博客")});
  r.ctx.app.projects.data.other.run = 1;
  TEST_ASSERT_EQUAL_UINT32(3, home_running(r.ctx.app));
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.has("跑3"));
  TEST_ASSERT_TRUE(r.canvas.has("!2"));  // demo_state: two sessions need you
  TEST_ASSERT_TRUE(r.canvas.find("!2")->style.fg == Color::Warn);
  set_projects(r.ctx.app, {project(1, "cpa", "Cardputer 助手"), project(2, "blog", "博客")});
  r.ctx.app.projects.data.other.run = 0;
  set_sessions(r.ctx.app, {session("aaaaaaa1", 1, "busy", SessionKind::Bg, SessionState::Busy, false, 10)});
  r.draw();
  TEST_ASSERT_FALSE(r.canvas.has("跑"));  // 0 running: no badge
  TEST_ASSERT_FALSE(r.canvas.has("!0"));
  TEST_ASSERT_NOT_NULL(exact(r.canvas, "等你"));
  set_projects(r.ctx.app, {project(1, "cpa", "Cardputer 助手", 4)}, false);  // cached names only
  TEST_ASSERT_EQUAL_UINT32(0, home_running(r.ctx.app));
}

// docs/19 §6.3: the card under the greeting, its two lines joined by '\n' (each line's
// texts by '|'): the session's name and how long, then its project key.
static std::string urgent_card(Rig& r) {
  r.draw();
  return r.canvas.row(kUrgentY) + "\n" + r.canvas.row(kUrgentY + 13);
}

// The card names only what the inbox already shows (project key, session name, how long),
// never queue or note text; waiting before idle, then the longest wait.
static void test_urgent_card() {
  Rig r;
  // docs/20 U22: the one age form of every page (age_text), on the card of mockup 01
  TEST_ASSERT_EQUAL_STRING("架构设计|9 分钟\ncpa · 等你回复", urgent_card(r).c_str());
  TEST_ASSERT_TRUE(r.canvas.find("9 分钟")->style.fg == Color::Warn);
  TEST_ASSERT_TRUE(r.canvas.filled_at(kUrgentY, Color::Card));
  TEST_ASSERT_TRUE(r.canvas.filled_at(kUrgentY + 3, Color::Warn));  // the "needs you" stripe
  TEST_ASSERT_FALSE(r.canvas.has("chord-tests"));
  TEST_ASSERT_FALSE(r.canvas.has("add LittleFS"));
  TEST_ASSERT_EQUAL_INT(0, home_urgent(r.ctx.app));
  set_sessions(r.ctx.app, {session("aaaaaaa1", 2, "idle-long", SessionKind::Bg, SessionState::Idle, true, 900),
                           session("aaaaaaa2", 2, "wait-short", SessionKind::Tty, SessionState::Waiting, true, 30),
                           session("aaaaaaa3", 2, "wait-long", SessionKind::Tty, SessionState::Waiting, true, 600),
                           session("aaaaaaa4", 1, "busy", SessionKind::Tty, SessionState::Busy, false, 9000)});
  TEST_ASSERT_EQUAL_INT(2, home_urgent(r.ctx.app));
  r.ctx.app.sessions.rx_ms = 0;
  r.ctx.now = 60000;  // the snapshot is a minute old: the age keeps counting
  TEST_ASSERT_EQUAL_STRING("wait-long|11 分钟\nblog · 等你回复", urgent_card(r).c_str());
  set_sessions(r.ctx.app, {session("aaaaaaa4", 7, "orphan", SessionKind::Tty, SessionState::Waiting, true, 5)});
  TEST_ASSERT_EQUAL_STRING("orphan|1 分钟\n等你回复", urgent_card(r).c_str());  // no project in slot 7; 5 s + 1 min
}

static void test_nothing_needs_you() {
  Rig r;
  set_sessions(r.ctx.app, {});
  TEST_ASSERT_EQUAL_INT(-1, home_urgent(r.ctx.app));
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.has("没有等你的 session"));
  TEST_ASSERT_TRUE(r.canvas.find("没有等你的 session")->style.fg == Color::Ink3);
  TEST_ASSERT_FALSE(r.canvas.has("!0"));
  TEST_ASSERT_FALSE(r.canvas.filled_at(kUrgentY + 3, Color::Warn));  // no stripe
}

// No sessions snapshot yet (the Mac does not answer): the page does not say all is done.
// docs/20 U19: with no cable the card says when it will know (cpad down: test_home_ux).
static void test_offline_home_says_so() {
  Rig r;
  r.ctx.app.sessions = SessionsView{};
  r.ctx.app.link.mode = LinkMode::Detached;
  r.draw();
  TEST_ASSERT_FALSE(r.canvas.has("都处理完了"));
  TEST_ASSERT_FALSE(r.canvas.has("没有等你的 session"));
  TEST_ASSERT_TRUE(r.canvas.has("你好，"));
  TEST_ASSERT_TRUE(r.canvas.find("还没连上 Mac")->style.fg == Color::Ink3);
  TEST_ASSERT_TRUE(r.canvas.has("接上 Mac 后显示等你的 session"));
  TEST_ASSERT_FALSE(r.canvas.filled_at(kUrgentY + 3, Color::Warn));  // no stripe
}

// docs/16's app tiles add a 4th row: the grid moves up under the card and the tiles get
// 2 px shorter, so the last row still ends above the hint bar.
static void test_a_fourth_row_still_fits() {
  const HomeGrid three = home_grid(9);
  TEST_ASSERT_TRUE(three.rows == 3 && three.y == 52 && three.h == 16 && three.pitch == 19);
  const HomeGrid four = home_grid(12);
  TEST_ASSERT_TRUE(four.rows == 4 && four.y == 48 && four.h == 14 && four.pitch == 16);
  TEST_ASSERT_TRUE(four.y >= 18 + 28 + 2);  // under the urgent card
  TEST_ASSERT_TRUE(kContentRect.y + four.y + 3 * four.pitch + four.h <= kHintRect.y);
  TEST_ASSERT_EQUAL_UINT8(4, home_grid(10).rows);
}

static TimeInfo at(int64_t wall_ms, uint32_t now, int16_t tz_min) {
  TimeInfo t;
  t.known = true;
  t.offset_ms = wall_ms - now;
  t.tz_min = tz_min;
  return t;
}

// The launcher has no title any more (docs/19 §6.3: the weekday is in the status bar);
// home_date stays for the Lead to keep or drop.
static void test_date_in_local_time() {
  TEST_ASSERT_EQUAL_STRING("10/21 周三", home_date(at(1792590120000LL, 1000, 0), 1000).c_str());
  TEST_ASSERT_EQUAL_STRING("10/22 周四", home_date(at(1792614600000LL, 1000, 480), 1000).c_str());
  TEST_ASSERT_EQUAL_STRING("2/29 周四", home_date(at(1709208000000LL, 5, 0), 5).c_str());
  TEST_ASSERT_EQUAL_STRING("12/31 周四", home_date(at(1798761540000LL, 0, -480), 0).c_str());
  TEST_ASSERT_EQUAL_STRING("1/1 周五", home_date(at(1798761540000LL, 0, 60), 0).c_str());
  TEST_ASSERT_EQUAL_STRING("1/1 周四", home_date(at(0, 0, 0), 0).c_str());
  TEST_ASSERT_EQUAL_STRING("12/31 周三", home_date(at(0, 0, -60), 0).c_str());  // before 1970
  TEST_ASSERT_EQUAL_STRING("", home_date(TimeInfo{}, 1000).c_str());
  Rig r;
  r.ctx.app.time = at(1792590120000LL, 0, 480);
  r.draw();
  TEST_ASSERT_FALSE(r.canvas.has("主页"));
  TEST_ASSERT_TRUE(r.canvas.has("晚上好"));  // 21:42 in UTC+8
}

static void test_help_rows_replay_what_the_keys_do() {
  Rig r;
  const std::vector<HelpRow> rows = help_rows(r.home);
  TEST_ASSERT_EQUAL_UINT32(9, rows.size());
  for (uint8_t i = 0; i < 8; ++i) {
    const char key[2] = {static_cast<char>(kLetters[i] - 'a' + 'A'), 0};
    const std::string want = opt_only(kLetters[i]) ? std::string("Opt+") + key : key;  // docs/20 B4
    TEST_ASSERT_EQUAL_STRING(want.c_str(), rows[i].keys);
    TEST_ASSERT_TRUE(help_label(rows[i])[0] != 0);
    TEST_ASSERT_TRUE(rows[i].ev == opt(kLetters[i]));  // docs/20 D1: R and L replay their Opt chord
    if (!opt_only(kLetters[i])) TEST_ASSERT_TRUE(rows[i].ev == home_next(HomeUi{}, chr(kLetters[i])).press);
  }
  TEST_ASSERT_EQUAL_STRING("1-9", rows[8].keys);
  TEST_ASSERT_TRUE(rows[8].ev.kind == KeyEventKind::None);  // a range: shown, not replayed
  FakeCanvas c;
  for (const HelpRow& h : rows) TEST_ASSERT_TRUE(c.textWidth(help_label(h)) <= 120);
}

// docs/19 §6.3: G0 moved from the page into the hint, which ends at Enter: the Tab menu is
// the ninth tile. docs/20 D8: talk once ● REC shows (;.,/ moving left the bar for it); gh #51
// review: it names the double tap too, at the bar's own spacing.
static void test_hint_names_g0_and_fits() {
  Rig r;
  TEST_ASSERT_EQUAL_STRING("G0按住看到●REC再说/双击免提 Enter打开", r.home.hint());
  TEST_ASSERT_TRUE(r.canvas.textWidth(r.home.hint()) <= kScreenW);
  FakeCanvas bar;
  theme::hint_bar(bar, r.home.hint());
  TEST_ASSERT_EQUAL_STRING("G0 按住看到●REC再说/双击免提 Enter 打开", bar.line(kHintRect.y).c_str());
  TEST_ASSERT_TRUE(theme::hint_fits(bar, r.home.hint()));
  TEST_ASSERT_EQUAL_STRING("菜单", help_label(kHomeTiles[8]));
  TEST_ASSERT_TRUE(kHomeTiles[8].ev == nav(Nav::Menu));
}

// docs/21 §6.7: in English every tile's name is whole next to its key and badge (two-digit
// counts), never "Pro…"; the board tile's count is the number alone, the menu's "run12".
static void test_english_tiles_are_whole_with_their_badges() {
  LangScope en(Lang::En);
  Rig r;
  set_projects(r.ctx.app, {project(1, "cpa", "Cardputer", 12)});
  set_sessions(r.ctx.app, {});
  for (uint8_t i = 0; i < 9; ++i) {
    r.ctx.app.sessions.data.rows[i] = session("aaaaaaa1", 1, "s", SessionKind::Tty, SessionState::Waiting, true, 9);
  }
  r.ctx.app.sessions.data.n = 9;
  r.draw();
  for (const HelpRow& t : kHomeTiles) {
    TEST_ASSERT_NOT_NULL_MESSAGE(exact(r.canvas, help_label(t)), help_label(t));
  }
  TEST_ASSERT_NOT_NULL(exact(r.canvas, "12"));
  TEST_ASSERT_NOT_NULL(exact(r.canvas, "!9"));
  for (const TextOp& t : r.canvas.texts) TEST_ASSERT_TRUE_MESSAGE(t.text.find("\xE2\x80\xA6") == std::string::npos, t.text.c_str());
}

// Long names, huge counts and a known time: every line still fits 240 px and the
// whole page stays inside the content rect.
static void test_worst_case_fits_the_content_rect() {
  Rig r;
  set_projects(r.ctx.app, {project(1, "abcdefghijkl", "很长的项目名字很长的项目名字", 65535)});
  r.ctx.app.projects.data.other.run = 65535;
  set_sessions(r.ctx.app, {session("aaaaaaa1", 1, "非常非常长的会话名字", SessionKind::Tty, SessionState::Waiting,
                                   true, 4000000000u)});
  r.ctx.app.time = at(1792590120000LL, 0, 480);
  r.home.onKey(chr(';'));
  r.home.onKey(chr(','));  // the Tab tile, bottom right
  TEST_ASSERT_EQUAL_UINT32(131070, home_running(r.ctx.app));
  r.draw();
  TEST_ASSERT_TRUE(r.canvas.has("跑"));
  TEST_ASSERT_TRUE(r.canvas.has("!1"));
  for (const TextOp& t : r.canvas.texts) {
    TEST_ASSERT_TRUE_MESSAGE(t.x >= kContentRect.x, t.text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(t.x + r.canvas.textWidth(t.text.c_str()) <= kScreenW, t.text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(t.y >= kContentRect.y, t.text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(t.y + 12 <= kContentRect.y + kContentRect.h, t.text.c_str());
  }
  for (const auto& f : r.canvas.fills) {
    TEST_ASSERT_TRUE(f.first.x + f.first.w <= kScreenW);
    TEST_ASSERT_TRUE(f.first.y + f.first.h <= kContentRect.y + kContentRect.h);
  }
}

static void test_module_stays_small() { TEST_ASSERT_TRUE(sizeof(HomeModule) <= 256); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_each_tile_letter_presses_its_opt_chord_once);
  RUN_TEST(test_digits_press_opt_digit);
  RUN_TEST(test_moves_wrap_within_the_grid);
  RUN_TEST(test_enter_equals_the_selected_tiles_key);
  RUN_TEST(test_tab_tile_presses_the_menu_key);
  RUN_TEST(test_back_and_other_keys_go_to_the_shell);
  RUN_TEST(test_r_and_l_need_opt);
  RUN_TEST(test_a_non_tile_letter_hints_once_per_visit);
  RUN_TEST(test_refused_press_is_still_handled);
  RUN_TEST(test_grid_labels_and_selection);
  RUN_TEST(test_badges_running_and_need);
  RUN_TEST(test_urgent_card);
  RUN_TEST(test_nothing_needs_you);
  RUN_TEST(test_offline_home_says_so);
  RUN_TEST(test_a_fourth_row_still_fits);
  RUN_TEST(test_date_in_local_time);
  RUN_TEST(test_help_rows_replay_what_the_keys_do);
  RUN_TEST(test_hint_names_g0_and_fits);
  RUN_TEST(test_english_tiles_are_whole_with_their_badges);
  RUN_TEST(test_worst_case_fits_the_content_rect);
  RUN_TEST(test_module_stays_small);
  return UNITY_END();
}
