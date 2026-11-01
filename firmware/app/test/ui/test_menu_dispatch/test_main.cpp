// Tab menu replay (docs/13 §3.4, §6.1-§6.2, §10.1): Enter on a 本页 row closes the menu and
// presses the row's key through the normal path, so it does exactly what pressing that
// key does. Checked row by row on two identical rigs, one through the menu and one with
// the key itself, for the board, the inbox, the system page, the home launcher, the
// Compose overlay and the meeting page. A range row ("0-9") is shown only. A replayed key
// cannot replay another. Also: the discard prompt's, the confirm boxes' and the meeting
// boxes' rows say what they do, in the box's words; a range row is shown only, its keys on its cap; Tab, then a
// letter; back from a project to the board's overview; a Backspace hold that closed the
// menu stops there; the Compose and meeting hints name the menu.
#include <unity.h>

#include <string>

#include "board_module.h"
#include "home_module.h"
#include "inbox_module.h"
#include "menu_model.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"
#include "system_module.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kT = 5000;  // every key of a comparison at the same time

struct Rig {
  FakePort port;
  BoardModule board;
  InboxModule inbox;
  SystemModule system;
  HomeModule home;
  cpa::Module* mods[4] = {&board, &inbox, &system, &home};
  Shell shell;
  explicit Rig(bool with_home) : shell(port, mods, with_home ? 4 : 3) {
    port.app = demo_state();
    set_ready(port.app);
    port.next_refusal = rec::RecRefusal::None;
    shell.begin(0);
  }
  void keys(std::initializer_list<KeyEvent> ks, uint32_t at = 1000) {
    for (const KeyEvent& k : ks) {
      if (shell.lock_gate(k, at)) shell.on_key(k, at);
    }
  }
};

template <class T>
std::string list(const std::vector<T>& v, std::string (*one)(const T&)) {
  std::string out;
  for (const T& x : v) out += one(x) + ";";
  return out;
}

std::string note(const NoteBody& n) { return std::string(n.text.c_str()) + "@" + (n.project.has ? "p" : "-"); }
std::string queued(const QueueBody& q) { return std::string(q.text.c_str()); }
std::string request(const OnlineRequest& r) { return std::to_string(static_cast<int>(r.kind)) + r.sid.c_str(); }
std::string input(const std::pair<rec::RecInput, uint8_t>& p) {
  return std::to_string(static_cast<int>(p.first)) + ":" + std::to_string(p.second);
}
std::string gesture(const std::pair<RecMode, uint8_t>& p) {
  return std::to_string(static_cast<int>(p.first)) + ":" + std::to_string(p.second);
}

// Everything a key can change: the screen (texts and fills), the shell and the port.
std::string snapshot(Rig& r) {
  FakeCanvas c;
  r.shell.render(c, kT);
  std::string s = c.dump();
  for (const auto& f : c.fills) {
    s += std::to_string(f.first.x) + "," + std::to_string(f.first.y) + "," + std::to_string(f.first.w) + "," +
         std::to_string(f.first.h) + "=" + std::to_string(static_cast<int>(f.second)) + "\n";
  }
  s += std::string("top=") + r.shell.top_id() + " overlay=" + std::to_string(static_cast<int>(r.shell.overlay())) +
       " menu=" + std::to_string(r.shell.menu_open()) + " locked=" + std::to_string(r.shell.locked()) +
       " slot=" + std::to_string(r.port.context_slot) + "\n";
  s += list(r.port.notes, note) + "|" + list(r.port.queued, queued) + "|" + list(r.port.requests, request) + "|" +
       list(r.port.inputs, input) + "|" + list(r.port.gestures, gesture) + "|" +
       std::to_string(r.port.agents.size()) + "\n";
  return s;
}

MenuContent here_rows(const Rig& r) {
  return menu_content(MenuPage::Here, r.shell.menu_source(), r.port.app, r.port.info);
}

// The hint bar as words: keycaps and what they do, "Enter 放弃 ` 返回" (docs/19 §4.2).
std::string hint_row(const Rig& r) {
  FakeCanvas c;
  r.shell.render(c, kT);
  return c.line(kHintRect.y);
}

// Tab, ; down to row i, Enter: the result must equal pressing the row's key at once.
// prep puts both rigs on the page under test.
void check_every_row(bool with_home, void (*prep)(Rig&)) {
  Rig probe(with_home);
  prep(probe);
  const MenuContent m = here_rows(probe);
  TEST_ASSERT_TRUE(m.n > 1);
  for (uint8_t i = 0; i < m.n; ++i) {
    Rig via_menu(with_home);
    Rig direct(with_home);
    prep(via_menu);
    prep(direct);
    via_menu.keys({nav(Nav::Menu)}, kT);
    for (uint8_t k = 0; k < i; ++k) via_menu.keys({chr('.')}, kT);
    const std::string before = snapshot(via_menu);
    via_menu.keys({enter()}, kT);
    const MenuRow& row = m.rows[i];
    const std::string what = std::string(row.keys.c_str()) + " " + row.label.c_str();
    if (row.ev.kind == KeyEventKind::None) {  // a range row: shown, nothing happens but a hint
      TEST_ASSERT_TRUE_MESSAGE(via_menu.shell.menu_open(), what.c_str());
      // docs/20 U17: Enter on a row that only explains says how to press it; D9: a chord
      // directly (the open menu runs it), plain digits with the menu closed; nothing else changes
      const bool chord = std::string(row.keys.c_str()).rfind("Opt+", 0) == 0;
      const char* how = chord ? "直接按" : "先按 Tab 关菜单";
      TEST_ASSERT_TRUE_MESSAGE(hint_row(via_menu).find(how) != std::string::npos, what.c_str());
      TEST_ASSERT_EQUAL_STRING_MESSAGE(before.substr(before.find("top=")).c_str(),
                                       snapshot(via_menu).substr(snapshot(via_menu).find("top=")).c_str(), what.c_str());
      continue;
    }
    direct.keys({row.ev}, kT);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(snapshot(direct).c_str(), snapshot(via_menu).c_str(), what.c_str());
  }
}

void on_board_home(Rig&) {}
void on_project_sessions(Rig& r) { r.keys({opt('1')}); }
void on_project_queue(Rig& r) { r.keys({opt('1'), chr('/')}); }
void on_inbox(Rig& r) { r.keys({opt('i'), chr('.')}); }
void on_system(Rig& r) { r.keys({opt('m')}); }
void on_home(Rig& r) { r.keys({chr('/')}); }
void on_compose(Rig& r) { r.keys({opt('1'), g0tap(), chr('h'), chr('i')}); }
void on_meeting(Rig& r) {
  r.port.app.rec.avail = true;
  r.port.app.rec.phase = RecPhase::Recording;
  r.port.app.rec.mode = RecMode::Meeting;
}
void on_discard(Rig& r) {  // ` on a draft (docs/20 D5): "放弃已输入的内容？"
  on_compose(r);
  r.keys({chr('`')});
}
void on_end_confirm(Rig& r) {  // Opt+R on the meeting page: "结束录音？"
  on_meeting(r);
  r.port.app.rec.phase = RecPhase::EndConfirm;
}
void on_start_box(Rig& r) {  // Opt+R: "开始会议录音？"
  on_meeting(r);
  r.port.app.rec.phase = RecPhase::Confirm;
}
void on_spawn_confirm(Rig& r) { r.keys({opt('1'), opt('s'), chr('x'), enter()}); }  // the draft behind it
void on_queued_spawn(Rig& r) {  // offline: Enter stores it on the Mac's queue
  r.port.app.link.mode = LinkMode::Attached;
  on_spawn_confirm(r);
}
void item_confirm(Rig& r) {  // a queue item started from the board: no draft behind it
  ConfirmSpec c;
  c.slot = 1;
  c.qid = some(Qid::from("q17"));
  c.text = UserText::from("add LittleFS fault-injection test");
  r.shell.confirm(c);
}
void on_item_spawn(Rig& r) { item_confirm(r); }
void on_item_unavailable(Rig& r) {  // offline: the item stays on the Mac
  r.port.app.link.mode = LinkMode::Attached;
  item_confirm(r);
}

// 本页 as "keys label;" per row.
std::string here_text(const Rig& r) {
  const MenuContent m = here_rows(r);
  std::string out;
  for (uint8_t i = 0; i < m.n; ++i) out += std::string(m.rows[i].keys.c_str()) + " " + m.rows[i].label.c_str() + ";";
  return out;
}

// The hint bar's right end.
int16_t hint_end(const Rig& r) {
  FakeCanvas c;
  r.shell.render(c, kT);
  int16_t end = 0;
  for (const TextOp& t : c.texts) {
    if (t.y == kHintRect.y && t.x + c.textWidth(t.text.c_str()) > end) end = t.x + c.textWidth(t.text.c_str());
  }
  return end;
}

// The menu page shown: the tab drawn on the caramel knob of the sheet's segmented control.
std::string active_tab(const FakeCanvas& c) {
  for (const TextOp& t : c.texts) {
    if (t.y == 19 && t.style.fg == Color::OnAccent) return t.text;
  }
  return "";
}

KeyEvent held_bksp() {
  KeyEvent e = bksp();
  e.repeat = true;
  return e;
}

// Calls press() from onKey and records what the shell answered.
class PressingModule : public ProbeModule {
 public:
  PressingModule() : ProbeModule("board", true) {}
  std::vector<bool> answers;
  bool onKey(const KeyEvent& k) override {
    ProbeModule::onKey(k);
    if (k == chr('x')) answers.push_back(ctx->press(opt('i')));
    return true;
  }
  uint8_t help(const HelpRow** rows) const override {
    static const HelpRow kRows[] = {{KeyEvent{KeyEventKind::Char, 'x', 0, 0, Nav::None}, "x", "按 Opt+I"}};
    *rows = kRows;
    return 1;
  }
};

}  // namespace

static void test_board_rows_replay_their_keys() {
  check_every_row(false, on_board_home);
  check_every_row(false, on_project_sessions);
  check_every_row(false, on_project_queue);
}

static void test_inbox_and_system_rows_replay_their_keys() {
  check_every_row(false, on_inbox);
  check_every_row(false, on_system);
}

static void test_home_rows_replay_their_keys() { check_every_row(true, on_home); }

static void test_compose_rows_replay_their_keys() { check_every_row(false, on_compose); }

static void test_meeting_rows_replay_their_keys() { check_every_row(false, on_meeting); }

static void test_range_row_is_shown_only() {
  Rig r(false);
  on_meeting(r);
  const MenuContent m = here_rows(r);
  int ranges = 0;
  for (uint8_t i = 0; i < m.n; ++i) ranges += m.rows[i].ev.kind == KeyEventKind::None ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(1, ranges);  // 0-9: tag
}

// press() queues one key, run after the current one; a replayed key cannot queue another.
static void test_a_replay_cannot_replay() {
  FakePort port;
  port.app = demo_state();
  PressingModule board;
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[] = {&board, &inbox};
  Shell shell(port, mods, 2);
  shell.begin(0);
  shell.on_key(chr('x'), 10);  // pressed by the user: Opt+I is replayed
  TEST_ASSERT_EQUAL_STRING("inbox", shell.top_id());
  TEST_ASSERT_TRUE(board.answers.back());
  shell.on_key(opt('h'), 20);
  shell.on_key(nav(Nav::Menu), 30);
  shell.on_key(enter(), 40);  // the menu replays x; x's own press() is refused
  TEST_ASSERT_FALSE(shell.menu_open());
  TEST_ASSERT_EQUAL_UINT32(2, board.answers.size());
  TEST_ASSERT_FALSE(board.answers.back());
  TEST_ASSERT_EQUAL_STRING("board", shell.top_id());
  TEST_ASSERT_FALSE(shell.press(KeyEvent{}));  // nothing to press
  TEST_ASSERT_TRUE(shell.press(opt('i')));     // outside a key: queued for the next one
  TEST_ASSERT_FALSE(shell.press(opt('m')));    // one at a time
}

// The menu is a sheet over the dimmed page, with its hint; the status bar stays, so over
// a recording ● and the timer stay (docs/13 §6.1, docs/19 §6.5).
static void test_menu_screen() {
  Rig r(false);
  r.keys({nav(Nav::Menu)});
  FakeCanvas c;
  r.shell.render(c, kT);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("本页", active_tab(c).c_str(), c.dump().c_str());
  TEST_ASSERT_TRUE(c.has("去哪") && c.has("控制"));
  TEST_ASSERT_EQUAL_UINT32(1, c.dims.size());  // the page under it, dimmed
  TEST_ASSERT_TRUE(c.line(kHintRect.y).find("Tab 关") != std::string::npos);
  r.keys({chr('/')});
  c.clear();
  r.shell.render(c, kT);
  TEST_ASSERT_EQUAL_STRING("去哪", active_tab(c).c_str());
  TEST_ASSERT_TRUE(c.has("Opt+P"));
  r.keys({chr('i')});  // a letter on 去哪 is Opt+letter
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_EQUAL_STRING("inbox", r.shell.top_id());
  Rig m(false);
  on_meeting(m);
  m.port.app.rec.started_ms = 0;
  m.keys({nav(Nav::Menu)});
  c.clear();
  m.shell.render(c, 62000);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("本页", active_tab(c).c_str(), c.dump().c_str());
  TEST_ASSERT_TRUE(c.has("● 1:02"));
  TEST_ASSERT_FALSE(c.has("去哪"));
  Rig box(false);  // over a confirm box: 本页 and 控制, and no lock row on either
  box.keys({opt('1'), opt('s'), chr('x'), enter(), nav(Nav::Menu)});
  c.clear();
  box.shell.render(c, kT);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("本页", active_tab(c).c_str(), c.dump().c_str());
  TEST_ASSERT_FALSE(c.has("去哪"));
  TEST_ASSERT_FALSE(c.has("Opt+L"));
  box.keys({chr('/')});
  c.clear();
  box.shell.render(c, kT);
  TEST_ASSERT_EQUAL_STRING("控制", active_tab(c).c_str());
  TEST_ASSERT_TRUE(c.has("声音"));
  TEST_ASSERT_FALSE(c.has("Opt+L"));
}

// A toast still shows over the menu, in the hint row.
static void test_toast_over_the_menu() {
  Rig r(false);
  r.keys({nav(Nav::Menu)});
  r.shell.toast(ToastLevel::Warn, "提示一下");
  FakeCanvas c;
  r.shell.render(c, 1000);
  TEST_ASSERT_TRUE(c.has("提示一下"));
}

// The home tiles reach the menu and the lock through the same replay (docs/13 §5.6);
// docs/20 D1: the L tile by Enter (l alone no longer locks on home).
static void test_home_tiles_open_the_menu_and_lock() {
  Rig r(true);
  r.keys({chr(','), chr(';'), enter()});  // left and up, wrapping: the Tab tile
  TEST_ASSERT_TRUE(r.shell.menu_open());
  r.keys({nav(Nav::Menu), chr('l')});
  TEST_ASSERT_FALSE(r.shell.locked());
  r.keys({chr(','), enter()});  // the L tile, left of Tab
  TEST_ASSERT_TRUE(r.shell.locked());
}

// Enter from the menu is the same Enter: over the meeting consent confirm it waits out
// the 400 ms guard like the key itself (docs/11 §4.1, unchanged).
static void test_menu_enter_keeps_the_consent_guard() {
  RecRig x;
  const KeySet tab = KeySet{}.with(key::kTab);
  const KeySet enter_key = KeySet{}.with(key::kEnter);
  x.r.step(10, KeySet{}.with(key::kOpt).with(layout_index(4, 1)));  // Opt+R
  x.steps(1);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Confirm);
  x.r.step(10, tab);
  x.steps(1);
  x.r.step(10, enter_key);  // row 1: Enter 开始, within 400 ms
  x.steps(1);
  TEST_ASSERT_FALSE(x.r.rt.shell().menu_open());
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Confirm);
  x.r.step(400);
  x.r.step(10, tab);
  x.steps(1);
  x.r.step(10, enter_key);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
}

// Over Compose's "放弃已输入的内容？" prompt the menu is a confirm box's (docs/13 §5.3):
// each row says what the prompt does with its key, and Opt+L does nothing there.
static void test_discard_prompt_rows_say_what_happens() {
  check_every_row(false, on_discard);
  Rig probe(false);
  on_discard(probe);
  const MenuContent m = here_rows(probe);
  TEST_ASSERT_EQUAL_UINT8(3, m.n);
  for (uint8_t i = 0; i < m.n; ++i) {
    Rig r(false);
    on_discard(r);
    r.keys({nav(Nav::Menu)}, kT);
    for (uint8_t k = 0; k < i; ++k) r.keys({chr('.')}, kT);
    r.keys({enter()}, kT);
    const std::string label = m.rows[i].label.c_str();
    TEST_ASSERT_FALSE(r.shell.menu_open());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.port.notes.size(), label.c_str());
    if (label == "放弃草稿") {
      TEST_ASSERT_TRUE_MESSAGE(r.shell.overlay() == proto::Overlay::None, label.c_str());
      continue;
    }
    TEST_ASSERT_EQUAL_STRING("返回编辑", label.c_str());
    TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Compose);
    r.keys({enter()}, kT);  // editing again: Enter saves the draft
    TEST_ASSERT_EQUAL_STRING("hi@p;", list(r.port.notes, note).c_str());
  }
  Rig box(false);
  on_discard(box);
  box.keys({opt('l')});
  TEST_ASSERT_FALSE(box.shell.locked());
  TEST_ASSERT_TRUE(hint_row(box).find("Enter 放弃") != std::string::npos);  // still asking
  const MenuContent ctl = menu_content(MenuPage::Control, box.shell.menu_source(), box.port.app, box.port.info);
  for (uint8_t i = 0; i < ctl.n; ++i) TEST_ASSERT_FALSE(ctl.rows[i].ev == opt('l'));
}

// Over the meeting's "结束录音？" box the rows say what the box does with each key, as the
// box itself does ("Enter 结束 ` 继续录"): no row says 取消, which would read as dropping it.
// docs/20 D3: a fourth row, Opt+L, keeps recording and locks.
static void test_end_confirm_rows_say_what_happens() {
  check_every_row(false, on_end_confirm);
  Rig probe(false);
  on_end_confirm(probe);
  const MenuContent m = here_rows(probe);
  TEST_ASSERT_EQUAL_UINT8(4, m.n);
  for (uint8_t i = 0; i < m.n; ++i) {
    const std::string what = std::string(m.rows[i].keys.c_str()) + " " + m.rows[i].label.c_str();
    const char* want = m.rows[i].ev == enter() ? "结束录音" : (m.rows[i].ev == opt('l') ? "继续录并锁屏" : "继续录");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, m.rows[i].label.c_str(), what.c_str());
  }
}

// U22, B6 (the review): over a confirm box the rows say what the box says (confirm_keys),
// never the generic 确认 / 取消: Enter 开始, 存为排队 or 关闭; ` and Fn+` 返回修改 with a
// draft behind the box (D6), else 取消. The meeting's start box as its card: Enter 开始,
// ` 取消, and Opt+L 取消并锁屏 (D3). Each row still does what its key does.
static void test_confirm_box_rows_say_what_the_box_says() {
  const struct {
    void (*prep)(Rig&);
    const char* rows;
  } kCases[] = {
      {on_spawn_confirm, "Enter 开始;` 返回修改;Fn+` 返回修改;"},
      {on_queued_spawn, "Enter 存为排队;` 返回修改;Fn+` 返回修改;"},
      {on_item_spawn, "Enter 开始;` 取消;Fn+` 取消;"},
      {on_item_unavailable, "Enter 关闭;` 取消;Fn+` 取消;"},
      {on_start_box, "Enter 开始;` 取消;Fn+` 取消;Opt+L 取消并锁屏;"},
  };
  for (const auto& k : kCases) {
    check_every_row(false, k.prep);
    Rig r(false);
    k.prep(r);
    TEST_ASSERT_EQUAL_STRING(k.rows, here_text(r).c_str());
    if (k.prep == on_start_box) continue;  // its card says Enter and `, its bar Opt+L
    const std::string bar = hint_row(r);   // the box's own words, on its hint bar
    const MenuContent m = here_rows(r);
    for (uint8_t i = 0; i < 2; ++i) {
      const std::string row = std::string(m.rows[i].keys.c_str()) + " " + m.rows[i].label.c_str();
      TEST_ASSERT_TRUE_MESSAGE(bar.find(row) != std::string::npos, (bar + " | " + row).c_str());
    }
  }
}

// A row for a group of keys ("1-9") is shown only; v0.5 (docs/19 §6.5): its keycap names
// the keys, so the label does not repeat them. Whether kKeymap or a module's help() gives it.
static void test_range_rows_say_which_keys() {
  int ranges = 0;
  for (void (*prep)(Rig&) : {on_home, on_board_home, on_project_sessions, on_inbox, on_compose, on_meeting}) {
    Rig r(prep == on_home);
    prep(r);
    const MenuContent m = here_rows(r);
    for (uint8_t i = 0; i < m.n; ++i) {
      const std::string keys = m.rows[i].keys.c_str();
      if (m.rows[i].ev.kind != KeyEventKind::None || keys.find('-') == std::string::npos) continue;
      const std::string label = m.rows[i].label.c_str();
      TEST_ASSERT_TRUE_MESSAGE(label.find(keys) == std::string::npos && label.find("（按") == std::string::npos,
                               (keys + " " + label).c_str());
      ++ranges;
    }
  }
  TEST_ASSERT_EQUAL_INT(3, ranges);  // home's 1-9, Compose's Opt+0-9, the meeting's 0-9
}

// Tab, then a letter: two presses to anywhere (docs/13 §3.3, §5.1), from 本页 too; over
// an overlay the letter is eaten, so the menu never jumps away from a draft (§6.1).
static void test_tab_then_a_letter() {
  Rig r(true);
  r.keys({nav(Nav::Menu), chr('p')});  // home: the key its 本页 row names
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  r.keys({opt('i'), nav(Nav::Menu), chr('P')});
  TEST_ASSERT_FALSE(r.shell.menu_open());
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  r.keys({nav(Nav::Menu), chr('l')});
  TEST_ASSERT_TRUE(r.shell.locked());
  Rig c(false);
  on_compose(c);
  c.keys({nav(Nav::Menu), chr('p')});
  TEST_ASSERT_TRUE(c.shell.menu_open());
  TEST_ASSERT_TRUE(c.shell.overlay() == proto::Overlay::Compose);
  TEST_ASSERT_EQUAL_STRING("board", c.shell.top_id());
}

// A project opened from the board's overview is a level of its own: back returns to the
// overview, then to the root (docs/13 §5.5). Opt+1..9 still replaces a project page.
static void test_back_from_a_project_returns_to_the_overview() {
  for (const KeyEvent& back : {chr('`'), esc(), bksp()}) {
    Rig r(true);
    r.keys({opt('p'), enter()});
    TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
    TEST_ASSERT_TRUE(r.port.context_slot != 0);
    r.keys({back});
    TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
    TEST_ASSERT_EQUAL_UINT8(0, r.port.context_slot);
    r.keys({back});
    TEST_ASSERT_EQUAL_STRING("home", r.shell.top_id());
  }
  Rig r(true);
  r.keys({opt('p'), enter(), opt('2'), chr('`')});
  TEST_ASSERT_EQUAL_STRING("board", r.shell.top_id());
  TEST_ASSERT_EQUAL_UINT8(0, r.port.context_slot);
}

// Holding Backspace to close the menu: the rest of the hold does not reach the page.
static void test_held_backspace_that_closed_the_menu_stops_there() {
  Rig r(false);
  r.keys({opt('n'), chr('h'), chr('e'), chr('l'), chr('l'), chr('o'), nav(Nav::Menu), bksp()});
  TEST_ASSERT_FALSE(r.shell.menu_open());
  for (int i = 0; i < 8; ++i) r.keys({held_bksp()});
  r.keys({bksp(), held_bksp()});  // a fresh press deletes again, and so does its repeat
  r.keys({enter()});
  TEST_ASSERT_EQUAL_STRING("hel@-;", list(r.port.notes, note).c_str());
  Rig page(true);
  page.keys({opt('p'), nav(Nav::Menu), bksp()});
  for (int i = 0; i < 4; ++i) page.keys({held_bksp()});
  TEST_ASSERT_EQUAL_STRING("board", page.shell.top_id());
}

// Tab works over Compose and the meeting page, so their hints end with "Tab菜单" too,
// within the 240 px bar (docs/13 §5.6).
static void test_compose_and_meeting_hints_name_the_menu() {
  Rig r(false);
  on_meeting(r);
  Rig empty(false);
  empty.keys({g0tap()});
  empty.shell.tick(1000 + kDoubleTapMs);  // gh #51: the note opens once no second tap came
  Rig typed(false);
  on_compose(typed);
  for (const Rig* x : {&r, &empty, &typed}) {
    const std::string h = hint_row(*x);  // keycaps: "... Tab 菜单"
    TEST_ASSERT_TRUE_MESSAGE(h.size() >= 10 && h.compare(h.size() - 10, 10, "Tab 菜单") == 0, h.c_str());
    TEST_ASSERT_TRUE_MESSAGE(hint_end(*x) <= kScreenW, h.c_str());
  }
  TEST_ASSERT_TRUE(hint_row(empty).find("退格 退出") != std::string::npos);
  TEST_ASSERT_TRUE(hint_row(typed).find("` 退出") != std::string::npos);  // docs/20 D5
  TEST_ASSERT_TRUE(hint_row(typed).find("Fn+`") == std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_board_rows_replay_their_keys);
  RUN_TEST(test_inbox_and_system_rows_replay_their_keys);
  RUN_TEST(test_home_rows_replay_their_keys);
  RUN_TEST(test_compose_rows_replay_their_keys);
  RUN_TEST(test_meeting_rows_replay_their_keys);
  RUN_TEST(test_range_row_is_shown_only);
  RUN_TEST(test_a_replay_cannot_replay);
  RUN_TEST(test_menu_screen);
  RUN_TEST(test_toast_over_the_menu);
  RUN_TEST(test_home_tiles_open_the_menu_and_lock);
  RUN_TEST(test_menu_enter_keeps_the_consent_guard);
  RUN_TEST(test_discard_prompt_rows_say_what_happens);
  RUN_TEST(test_end_confirm_rows_say_what_happens);
  RUN_TEST(test_confirm_box_rows_say_what_the_box_says);
  RUN_TEST(test_range_rows_say_which_keys);
  RUN_TEST(test_tab_then_a_letter);
  RUN_TEST(test_back_from_a_project_returns_to_the_overview);
  RUN_TEST(test_held_backspace_that_closed_the_menu_stops_there);
  RUN_TEST(test_compose_and_meeting_hints_name_the_menu);
  return UNITY_END();
}
