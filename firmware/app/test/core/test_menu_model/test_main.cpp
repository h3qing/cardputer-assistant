// Tab menu content (docs/13 §6.2). The checks against kKeymap are generic: they walk
// the table and never name a binding V40-F adds, so they hold before and after it.
// The System layer (Tab, Opt+L) and the lock row are checked against a small fake
// table, since kKeymap has no System bindings until V40-F.
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "menu_model.h"
#include "ui/support/fake_canvas.h"

using namespace cpa;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

static KeyEvent chr(char c) { return KeyEvent{KeyEventKind::Char, c, 0, 0, Nav::None}; }
static KeyEvent opt(char k) { return KeyEvent{KeyEventKind::Chord, 0, kModOpt, k, Nav::None}; }
static KeyEvent nav(Nav n) { return KeyEvent{KeyEventKind::Nav, 0, 0, 0, n}; }
static KeyEvent g0(KeyEventKind k) { return KeyEvent{k, 0, 0, 0, Nav::None}; }

static const Binding kFake[] = {
    {Layer::Global, Action::QuickNote, 0, g0(KeyEventKind::G0Tap)},
    {Layer::Global, Action::RecMemo, 0, g0(KeyEventKind::G0Long)},
    {Layer::Global, Action::NewNote, 0, opt('n')},
    {Layer::Global, Action::Inbox, 0, opt('i')},
    {Layer::Global, Action::OpenProject, 1, opt('1')},
    {Layer::Global, Action::OpenProject, 2, opt('2')},
    {Layer::Global, Action::OpenProject, 3, opt('3')},
    {Layer::Global, Action::Projects, 0, opt('p')},
    {Layer::Global, Action::Home, 0, opt('h')},
    {Layer::System, Action::Menu, 0, nav(Nav::Menu)},
    {Layer::System, Action::Lock, 0, opt('l')},
    {Layer::Lock, Action::Unlock, 0, nav(Nav::Enter)},
    {Layer::RecPage, Action::RecStar, 0, chr(' ')},
    {Layer::RecPage, Action::RecTag, 0, chr('0')},
    {Layer::RecPage, Action::RecTag, 1, chr('1')},
    {Layer::RecPage, Action::RecTag, 2, chr('2')},
    {Layer::RecPage, Action::RecTag, 5, chr('5')},  // not next to 2: a row of its own
    {Layer::Compose, Action::Submit, 0, nav(Nav::Enter)},
    {Layer::Confirm, Action::Confirm, 0, nav(Nav::Enter)},
};
static const KeymapView kFakeMap{kFake, sizeof(kFake) / sizeof(kFake[0])};

static const Layer kHereLayers[] = {Layer::List, Layer::Compose, Layer::Confirm, Layer::RecMemo, Layer::RecPage,
                                    Layer::RecLatch};

static MenuSource over(Layer l, bool overlay) {
  MenuSource src;
  src.layer = l;
  src.overlay = overlay;
  return src;
}

static MenuContent page(MenuPage p, const MenuSource& src, const KeymapView& km = KeymapView{}) {
  return menu_content(p, src, AppState{}, SysInfo{}, km);
}

static std::string row_text(const MenuRow& r) { return std::string(r.keys.c_str()) + " " + r.label.c_str(); }

static char digit(const KeyEvent& e) {  // the digit a Char or Chord types, else 0
  const char d = e.kind == KeyEventKind::Chord ? e.key : (e.kind == KeyEventKind::Char ? e.ch : 0);
  return d >= '0' && d <= '9' ? d : 0;
}

// b follows a in a digit run: same layer, action and kind, the next digit.
static bool next_digit(const Binding& a, const Binding& b) {
  return a.layer == b.layer && a.action == b.action && digit(a.trigger) != 0 && a.trigger.kind == b.trigger.kind &&
         a.trigger.mods == b.trigger.mods && digit(b.trigger) == digit(a.trigger) + 1;
}

// The row stands for kKeymap[i] (itself or inside its digit range).
static bool covers(const MenuRow& row, size_t i) {
  const size_t first = static_cast<size_t>(row.binding);
  return row.binding >= 0 && i >= first && i < first + row.span;
}

// -- Go and Control ----------------------------------------------------------------

static void test_every_global_binding_appears_once_on_goto_or_control() {
  const MenuSource src = over(Layer::List, false);
  const MenuContent go = page(MenuPage::Goto, src);
  const MenuContent ctl = page(MenuPage::Control, src);
  TEST_ASSERT_FALSE(go.overflow);
  for (size_t i = 0; i < kKeymapCount; ++i) {
    if (kKeymap[i].layer != Layer::Global) continue;
    int seen = 0;
    for (const MenuContent* m : {&go, &ctl}) {
      for (uint8_t r = 0; r < m->n; ++r) seen += covers(m->rows[r], i) ? 1 : 0;
    }
    const std::string what = "global binding " + std::to_string(i);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, seen, what.c_str());
  }
}

static void test_goto_lists_places_first() {
  const MenuContent go = page(MenuPage::Goto, over(Layer::List, false));
  auto place = [](Action a) {
    return a == Action::Home || a == Action::Projects || a == Action::Inbox || a == Action::OpenProject ||
           a == Action::System;
  };
  TEST_ASSERT_TRUE(go.n > 0);
  TEST_ASSERT_TRUE(kKeymap[go.rows[0].binding].action == Action::Home);
  bool rest = false;
  for (uint8_t r = 0; r < go.n; ++r) {
    const bool p = place(kKeymap[go.rows[r].binding].action);
    TEST_ASSERT_FALSE_MESSAGE(p && rest, row_text(go.rows[r]).c_str());
    rest = rest || !p;
  }
}

static void test_goto_order_with_the_fake_table() {
  const MenuContent go = page(MenuPage::Goto, over(Layer::List, false), kFakeMap);
  const char* want[] = {"Opt+H 主页", "Opt+P 项目看板", "Opt+I 等你", "Opt+1-3 第 N 个项目页",
                        "G0 记一条 → 今日笔记", "按住G0 说一段（语音便条）", "Opt+N 记一条"};  // docs/20 U22
  TEST_ASSERT_EQUAL_UINT8(7, go.n);
  for (uint8_t r = 0; r < go.n; ++r) TEST_ASSERT_EQUAL_STRING(want[r], row_text(go.rows[r]).c_str());
  TEST_ASSERT_TRUE(go.rows[3].ev.kind == KeyEventKind::None);  // a range is shown only
  TEST_ASSERT_TRUE(go.rows[4].ev == g0(KeyEventKind::G0Tap));
  TEST_ASSERT_TRUE(go.rows[5].ev.kind == KeyEventKind::None);  // a replayed hold has no G0 release
}

static void test_control_page_rows() {
  AppState s;
  s.device.bat_mv = 4021;
  s.rec.free_known = true;
  s.rec.free_mib = 412u * 1024u;
  s.rec.files_n = 2;
  s.outbox.pending = 3;
  s.link.mode = LinkMode::Ready;
  SysInfo sys;
  sys.host_app = FixedStr<32>::from("cpad/0.4.0");
  const MenuContent m = menu_content(MenuPage::Control, over(Layer::List, false), s, sys, kFakeMap);
  TEST_ASSERT_TRUE(m.page == MenuPage::Control);
  TEST_ASSERT_EQUAL_UINT8(5 + 2, m.n);  // docs/20 U21: then the status bar's legend (↑ R; no !0)
  TEST_ASSERT_EQUAL_STRING("Opt+L 锁屏", row_text(m.rows[0]).c_str());  // the lock card says how to unlock
  TEST_ASSERT_TRUE(m.rows[0].ev == opt('l'));
  TEST_ASSERT_EQUAL_STRING("声音 关（录音不出声）", row_text(m.rows[1]).c_str());
  TEST_ASSERT_EQUAL_STRING("电池 84% · 4021mV", row_text(m.rows[2]).c_str());
  TEST_ASSERT_EQUAL_STRING("SD 剩 412G · 待传 录2 · ↑3", row_text(m.rows[3]).c_str());
  TEST_ASSERT_EQUAL_STRING("Mac 在线 · cpad/0.4.0", row_text(m.rows[4]).c_str());
  for (uint8_t r = 1; r < m.n; ++r) {
    TEST_ASSERT_TRUE(m.rows[r].ev.kind == KeyEventKind::None);  // facts are shown only
    TEST_ASSERT_EQUAL_INT16(-1, m.rows[r].binding);
  }
}

// docs/20 U21: Control explains the status bar's counts in whole sentences, one name for
// each count, with the numbers as they are now; each only while the bar shows it: ↑N,
// !N, RN and ※kN when not zero (status_bar.cpp; B3: ↑0 is no longer drawn either).
static void test_control_page_explains_the_status_bar() {
  AppState s;
  s.outbox.pending = 1;
  s.rec.files_n = 2;
  s.sessions.data.n = 1;
  s.sessions.data.rows[0].need = true;
  const SysInfo sys;
  const MenuContent m = menu_content(MenuPage::Control, over(Layer::List, false), s, sys, kFakeMap);
  TEST_ASSERT_EQUAL_UINT8(8, m.n);
  TEST_ASSERT_EQUAL_STRING("↑1 1 条笔记等着传到 Mac", row_text(m.rows[5]).c_str());
  TEST_ASSERT_EQUAL_STRING("!1 1 个 session 等你", row_text(m.rows[6]).c_str());
  TEST_ASSERT_EQUAL_STRING("录2 2 段录音等着传", row_text(m.rows[7]).c_str());
  s.device.ghost_n = 1;
  const MenuContent k = menu_content(MenuPage::Control, over(Layer::List, false), s, sys, kFakeMap);
  TEST_ASSERT_EQUAL_UINT8(9, k.n);
  TEST_ASSERT_EQUAL_STRING("※k1 键盘按太快，丢了 1 次", row_text(k.rows[8]).c_str());
  for (uint8_t r = 5; r < k.n; ++r) TEST_ASSERT_TRUE(k.rows[r].ev.kind == KeyEventKind::None);  // shown only
  const MenuContent none = menu_content(MenuPage::Control, over(Layer::List, false), AppState{}, sys, kFakeMap);
  TEST_ASSERT_EQUAL_UINT8(5, none.n);  // docs/20 B3: nothing on the bar to explain (no ↑0)
}

static void test_a_global_lock_key_is_on_control_not_goto() {
  static const Binding kGlobalLock[] = {
      {Layer::Global, Action::Home, 0, opt('h')},
      {Layer::Global, Action::Lock, 0, opt('l')},
  };
  const KeymapView km{kGlobalLock, 2};
  const MenuContent go = page(MenuPage::Goto, over(Layer::List, false), km);
  TEST_ASSERT_EQUAL_UINT8(1, go.n);
  TEST_ASSERT_TRUE(go.rows[0].ev == opt('h'));
  const MenuContent ctl = page(MenuPage::Control, over(Layer::List, false), km);
  TEST_ASSERT_TRUE(ctl.rows[0].ev == opt('l'));
  TEST_ASSERT_EQUAL_INT16(1, ctl.rows[0].binding);
}

static std::string control_row(const AppState& s, const SysInfo& sys, uint8_t r) {
  return row_text(menu_content(MenuPage::Control, over(Layer::List, false), s, sys, kFakeMap).rows[r]);
}

static void test_control_facts_when_unknown_or_small() {
  AppState s;
  const SysInfo sys;
  TEST_ASSERT_EQUAL_STRING("电池 --", control_row(s, sys, 2).c_str());
  TEST_ASSERT_EQUAL_STRING("SD 剩 -- · 待传 录0 · ↑0", control_row(s, sys, 3).c_str());
  TEST_ASSERT_EQUAL_STRING("Mac 离线", control_row(s, sys, 4).c_str());
  s.rec.free_known = true;
  s.rec.free_mib = 900;
  TEST_ASSERT_EQUAL_STRING("SD 剩 900M · 待传 录0 · ↑0", control_row(s, sys, 3).c_str());
  s.rec.free_mib = 1536;
  TEST_ASSERT_EQUAL_STRING("SD 剩 1.5G · 待传 录0 · ↑0", control_row(s, sys, 3).c_str());
  s.rec.free_mib = 10u * 1024u;
  TEST_ASSERT_EQUAL_STRING("SD 剩 10G · 待传 录0 · ↑0", control_row(s, sys, 3).c_str());
  s.link.mode = LinkMode::Attached;
  TEST_ASSERT_EQUAL_STRING("Mac 无响应", control_row(s, sys, 4).c_str());
  s.link.version_mismatch = true;
  TEST_ASSERT_EQUAL_STRING("Mac 版本不兼容", control_row(s, sys, 4).c_str());
}

static void test_no_lock_row_over_a_confirm_box() {
  const MenuContent ctl = page(MenuPage::Control, over(Layer::Confirm, true), kFakeMap);
  TEST_ASSERT_EQUAL_UINT8(4, ctl.n);  // the facts; no legend: the bar draws no ↑0 (docs/20 B3)
  TEST_ASSERT_EQUAL_STRING("声音", ctl.rows[0].keys.c_str());
  const MenuContent here = page(MenuPage::Here, over(Layer::Confirm, true), kFakeMap);
  TEST_ASSERT_EQUAL_UINT8(1, here.n);  // Enter Confirm, no Opt+L (docs/13 §5.3)
}

// -- Here --------------------------------------------------------------------------

// Every "Here" row built from a layer replays that binding's trigger (but the shown-only ones:
// a hold to record, a hands-free memo's end), and every binding of the layer (but Tab,
// which closes the menu) is on the page once.
static void test_here_rows_carry_the_trigger_and_cover_their_layer() {
  for (const Layer l : kHereLayers) {
    const MenuContent m = page(MenuPage::Here, over(l, l != Layer::List));
    TEST_ASSERT_FALSE(m.overflow);
    for (uint8_t r = 0; r < m.n; ++r) {
      const MenuRow& row = m.rows[r];
      const std::string what = "layer " + std::to_string(static_cast<int>(l)) + " " + row_text(row);
      TEST_ASSERT_TRUE_MESSAGE(row.binding >= 0, what.c_str());
      const Binding& b = kKeymap[row.binding];
      if (row.span > 1 || b.action == Action::RecMemo || b.action == Action::RecStop) continue;
      TEST_ASSERT_TRUE_MESSAGE(row.ev == b.trigger, what.c_str());
    }
    for (size_t i = 0; i < kKeymapCount; ++i) {
      if (kKeymap[i].layer != l || kKeymap[i].action == Action::Menu) continue;  // Tab closes the menu
      int seen = 0;
      for (uint8_t r = 0; r < m.n; ++r) seen += covers(m.rows[r], i) ? 1 : 0;
      const std::string what = "binding " + std::to_string(i);
      TEST_ASSERT_EQUAL_INT_MESSAGE(1, seen, what.c_str());
    }
  }
}

static void test_system_rows_follow_the_page_rows() {
  const MenuContent meeting = page(MenuPage::Here, over(Layer::RecPage, true), kFakeMap);
  TEST_ASSERT_EQUAL_UINT8(4, meeting.n);  // Space, 0-2, 5, then Opt+L; never Tab
  TEST_ASSERT_EQUAL_STRING("空格 加 ★", row_text(meeting.rows[0]).c_str());
  TEST_ASSERT_EQUAL_STRING("Opt+L 锁屏（录音不停）", row_text(meeting.rows[3]).c_str());
  TEST_ASSERT_TRUE(meeting.rows[3].ev == opt('l'));
  const MenuContent compose = page(MenuPage::Here, over(Layer::Compose, true), kFakeMap);
  TEST_ASSERT_EQUAL_STRING("Opt+L 锁屏（草稿留着）", row_text(compose.rows[1]).c_str());
  const MenuContent list = page(MenuPage::Here, over(Layer::List, false), kFakeMap);
  TEST_ASSERT_EQUAL_UINT8(1, list.n);
  TEST_ASSERT_EQUAL_STRING("Opt+L 锁屏", row_text(list.rows[0]).c_str());
}

// gh #51: "Here" over a hands-free memo, from kKeymap: G0 ends it (shown only: Tab, Enter are
// two pocket presses and must not end it, review of gh #51), the digits tag it, Opt+L locks
// and it goes on; never Tab. Go lists the double tap, which Enter starts.
static void test_hands_free_rows() {
  const MenuContent m = page(MenuPage::Here, over(Layer::RecLatch, true));
  TEST_ASSERT_EQUAL_UINT8(3, m.n);
  TEST_ASSERT_EQUAL_STRING("G0 结束并保存", row_text(m.rows[0]).c_str());
  TEST_ASSERT_TRUE(m.rows[0].ev.kind == KeyEventKind::None);
  TEST_ASSERT_EQUAL_STRING("0-9 归到项目，0 不归属", row_text(m.rows[1]).c_str());
  TEST_ASSERT_EQUAL_STRING("Opt+L 锁屏（录音不停）", row_text(m.rows[2]).c_str());
  const MenuContent go = page(MenuPage::Goto, over(Layer::List, false));
  bool listed = false;
  for (uint8_t r = 0; r < go.n; ++r) {
    if (row_text(go.rows[r]) != "双击G0 免提说一段") continue;
    listed = true;
    TEST_ASSERT_TRUE(go.rows[r].ev == g0(KeyEventKind::G0Double));
  }
  TEST_ASSERT_TRUE_MESSAGE(listed, "去哪: 双击G0");
}

static void test_here_never_lists_tab_or_a_key_twice() {
  // Should a layer bind Tab or Opt+L itself as well as the System layer, "Here" still lists
  // Opt+L once and Tab not at all (Tab closes the menu).
  static const Binding kBoth[] = {
      {Layer::RecPage, Action::RecStar, 0, chr(' ')},
      {Layer::RecPage, Action::Menu, 0, nav(Nav::Menu)},
      {Layer::RecPage, Action::Lock, 0, opt('l')},
      {Layer::System, Action::Menu, 0, nav(Nav::Menu)},
      {Layer::System, Action::Lock, 0, opt('l')},
  };
  const MenuContent m = page(MenuPage::Here, over(Layer::RecPage, true), KeymapView{kBoth, 5});
  TEST_ASSERT_EQUAL_UINT8(2, m.n);
  TEST_ASSERT_EQUAL_STRING("空格 加 ★", row_text(m.rows[0]).c_str());
  TEST_ASSERT_EQUAL_STRING("Opt+L 锁屏（录音不停）", row_text(m.rows[1]).c_str());
  TEST_ASSERT_EQUAL_INT16(2, m.rows[1].binding);  // the page's own binding comes first
}

static void test_system_rows_from_kkeymap() {
  // Every System binding but Tab ends "Here" (outside a confirm box); none before V40-F.
  uint8_t sys_n = 0;
  for (size_t i = 0; i < kKeymapCount; ++i) {
    sys_n = static_cast<uint8_t>(sys_n + (kKeymap[i].layer == Layer::System && kKeymap[i].action != Action::Menu));
  }
  const MenuContent m = page(MenuPage::Here, over(Layer::List, false));
  for (uint8_t k = 0; k < sys_n; ++k) {
    const MenuRow& row = m.rows[m.n - sys_n + k];
    TEST_ASSERT_TRUE(kKeymap[row.binding].layer == Layer::System);
    TEST_ASSERT_TRUE(row.ev == kKeymap[row.binding].trigger);
  }
  for (uint8_t r = 0; r + sys_n < m.n; ++r) TEST_ASSERT_TRUE(kKeymap[m.rows[r].binding].layer == Layer::List);
}

static const HelpRow kModRows[] = {
    {chr(' '), "空格", "加 ★"},
    {KeyEvent{}, "1-9", "归到项目"},
    {opt('d'), "Opt+D", "隐藏"},
};

static void test_module_rows_replace_the_layer_rows() {
  MenuSource src = over(Layer::List, false);
  src.help = kModRows;
  src.help_n = 3;
  src.title = "录音";
  const MenuContent m = page(MenuPage::Here, src, kFakeMap);
  TEST_ASSERT_EQUAL_STRING("录音", m.title);
  TEST_ASSERT_EQUAL_UINT8(4, m.n);  // three module rows, then Opt+L
  const char* labels[] = {"加 ★", "归到项目", "隐藏"};  // v0.5: the range is on its keycap (docs/19 §6.5)
  for (uint8_t r = 0; r < 3; ++r) {
    TEST_ASSERT_TRUE(m.rows[r].ev == kModRows[r].ev);
    TEST_ASSERT_EQUAL_INT16(-1, m.rows[r].binding);
    TEST_ASSERT_EQUAL_STRING(kModRows[r].keys, m.rows[r].keys.c_str());
    TEST_ASSERT_EQUAL_STRING(labels[r], m.rows[r].label.c_str());
  }
  TEST_ASSERT_TRUE(m.rows[3].ev == opt('l'));
}

static void test_a_long_module_label_is_cut_between_characters() {
  static const HelpRow kLong[] = {{chr('x'), "x", "a一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十"}};
  MenuSource src = over(Layer::List, false);
  src.help = kLong;
  src.help_n = 1;
  const MenuContent m = page(MenuPage::Here, src, kFakeMap);
  TEST_ASSERT_EQUAL_UINT(61, m.rows[0].label.size());  // 'a' + 20 CJK; the 21st would split
}

// -- ranges, overlay, overflow -----------------------------------------------------

static void test_digit_runs_collapse_into_one_shown_only_row() {
  const MenuSource src = over(Layer::List, false);
  const MenuContent go = page(MenuPage::Goto, src);
  bool projects = false;
  for (uint8_t r = 0; r < go.n; ++r) projects = projects || std::string(go.rows[r].keys.c_str()) == "Opt+1-9";
  TEST_ASSERT_TRUE(projects);
  for (const Layer l : kHereLayers) {
    const MenuContent m = page(MenuPage::Here, over(l, true));
    for (const MenuContent* c : {&m, &go}) {
      for (uint8_t r = 0; r < c->n; ++r) {
        const MenuRow& row = c->rows[r];
        if (row.binding < 0) continue;
        const std::string what = row_text(row);
        const size_t last = static_cast<size_t>(row.binding) + row.span - 1;
        for (size_t i = static_cast<size_t>(row.binding); i < last; ++i)
          TEST_ASSERT_TRUE_MESSAGE(next_digit(kKeymap[i], kKeymap[i + 1]), what.c_str());
        // maximal: the binding after the run does not continue it
        const bool more = last + 1 < kKeymapCount && next_digit(kKeymap[last], kKeymap[last + 1]);
        TEST_ASSERT_FALSE_MESSAGE(more, what.c_str());
        if (row.span == 1) continue;
        TEST_ASSERT_TRUE_MESSAGE(row.ev.kind == KeyEventKind::None, what.c_str());
        const KeyLabel want = key_range_label(kKeymap[row.binding].trigger, kKeymap[last].trigger);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want.c_str(), row.keys.c_str(), what.c_str());
      }
    }
  }
}

static void test_a_broken_run_stays_apart() {
  const MenuContent m = page(MenuPage::Here, over(Layer::RecPage, true), kFakeMap);
  TEST_ASSERT_EQUAL_STRING("0-2", m.rows[1].keys.c_str());
  TEST_ASSERT_EQUAL_UINT8(3, m.rows[1].span);
  TEST_ASSERT_EQUAL_STRING("归到项目，0 不归属", m.rows[1].label.c_str());
  TEST_ASSERT_EQUAL_STRING("5", m.rows[2].keys.c_str());
  TEST_ASSERT_TRUE(m.rows[2].ev == chr('5'));
}

// v0.5 (docs/19 §6.5, mockup 03): a row that opens the board or the inbox carries its count,
// runN or !N, as the home tiles do; none for zero, none on other rows.
static void test_badges_on_the_board_and_inbox_rows() {
  AppState s;
  s.projects.live = true;
  s.projects.data.n = 1;
  s.projects.data.rows[0].run = 2;
  s.projects.data.other.run = 1;
  s.sessions.data.n = 2;
  s.sessions.data.rows[0].need = true;
  const MenuContent go = menu_content(MenuPage::Goto, over(Layer::List, false), s, SysInfo{}, kFakeMap);
  int seen = 0;
  for (uint8_t r = 0; r < go.n; ++r) {
    const Action a = kFake[go.rows[r].binding].action;
    const std::string what = row_text(go.rows[r]);
    if (a == Action::Projects) {
      TEST_ASSERT_TRUE_MESSAGE(go.rows[r].badge == MenuBadge::Running, what.c_str());
      TEST_ASSERT_EQUAL_UINT32(3, go.rows[r].badge_n);
      ++seen;
    } else if (a == Action::Inbox) {
      TEST_ASSERT_TRUE_MESSAGE(go.rows[r].badge == MenuBadge::NeedsYou, what.c_str());
      TEST_ASSERT_EQUAL_UINT32(1, go.rows[r].badge_n);
      ++seen;
    } else {
      TEST_ASSERT_TRUE_MESSAGE(go.rows[r].badge == MenuBadge::None, what.c_str());
    }
  }
  TEST_ASSERT_EQUAL_INT(2, seen);
  static const HelpRow kTiles[] = {{opt('p'), "P", "项目"}, {opt('i'), "I", "等你"}, {opt('n'), "N", "记一条"}};
  MenuSource home = over(Layer::List, false);
  home.help = kTiles;
  home.help_n = 3;
  const MenuContent here = menu_content(MenuPage::Here, home, s, SysInfo{}, kFakeMap);
  TEST_ASSERT_TRUE(here.rows[0].badge == MenuBadge::Running && here.rows[1].badge == MenuBadge::NeedsYou);
  TEST_ASSERT_TRUE(here.rows[2].badge == MenuBadge::None);
  const MenuContent none = menu_content(MenuPage::Here, home, AppState{}, SysInfo{}, kFakeMap);
  TEST_ASSERT_TRUE(none.rows[0].badge == MenuBadge::None && none.rows[1].badge == MenuBadge::None);
}

static void test_overlay_mode_has_no_goto_page() {
  for (const Layer l : kHereLayers) {
    const MenuSource src = over(l, true);
    const MenuContent go = page(MenuPage::Goto, src);
    const MenuContent here = page(MenuPage::Here, src);
    TEST_ASSERT_TRUE(go.page == MenuPage::Here);
    TEST_ASSERT_TRUE(go.overlay);
    TEST_ASSERT_EQUAL_UINT8(here.n, go.n);
    for (uint8_t r = 0; r < go.n; ++r) TEST_ASSERT_EQUAL_STRING(here.rows[r].label.c_str(), go.rows[r].label.c_str());
  }
  TEST_ASSERT_TRUE(page(MenuPage::Goto, over(Layer::List, false)).page == MenuPage::Goto);
}

static void test_overflow_is_flagged() {
  Binding many[kMenuRowsMax + 4];
  for (uint8_t i = 0; i < kMenuRowsMax + 4; ++i) {
    many[i] = Binding{Layer::List, Action::Up, 0, chr(static_cast<char>('a' + i))};
  }
  const MenuContent m = page(MenuPage::Here, over(Layer::List, false), KeymapView{many, kMenuRowsMax + 4});
  TEST_ASSERT_EQUAL_UINT8(kMenuRowsMax, m.n);
  TEST_ASSERT_TRUE(m.overflow);
}

static void rows_fit_their_column() {
  const uitest::FakeCanvas c;
  for (const Layer l : kHereLayers) {
    for (const bool ov : {false, true}) {
      for (const MenuPage p : {MenuPage::Here, MenuPage::Goto, MenuPage::Control}) {
        for (const KeymapView& km : {KeymapView{}, kFakeMap}) {
          const MenuContent m = page(p, over(l, ov), km);
          for (uint8_t r = 0; r < m.n; ++r) {
            const std::string what = row_text(m.rows[r]);
            TEST_ASSERT_TRUE_MESSAGE(c.textWidth(m.rows[r].keys.c_str()) <= 48, what.c_str());
            TEST_ASSERT_TRUE_MESSAGE(c.textWidth(m.rows[r].label.c_str()) <= 240 - 54, what.c_str());
          }
        }
      }
    }
  }
}

static void test_every_row_fits_its_column() {
  for (const Lang l : kLangs) {
    LangScope in(l);
    rows_fit_their_column();
  }
}

// docs/21 §6.7: the menu in English, the counts with their plurals, and the tab names.
static void test_control_page_in_english() {
  LangScope en(Lang::En);
  AppState s;
  s.device.bat_mv = 4021;
  s.rec.free_known = true;
  s.rec.free_mib = 412u * 1024u;
  s.rec.files_n = 1;
  s.outbox.pending = 3;
  s.sessions.data.n = 1;
  s.sessions.data.rows[0].need = true;
  s.device.ghost_n = 2;
  s.link.mode = LinkMode::Attached;
  s.link.version_mismatch = true;
  SysInfo sys;
  const MenuContent m = menu_content(MenuPage::Control, over(Layer::List, false), s, sys, kFakeMap);
  TEST_ASSERT_EQUAL_STRING("Opt+L Lock", row_text(m.rows[0]).c_str());
  TEST_ASSERT_EQUAL_STRING("Sound Off (silent)", row_text(m.rows[1]).c_str());
  TEST_ASSERT_EQUAL_STRING("Bat 84% · 4021mV", row_text(m.rows[2]).c_str());
  TEST_ASSERT_EQUAL_STRING("SD 412G free · R1 · ↑3", row_text(m.rows[3]).c_str());
  TEST_ASSERT_EQUAL_STRING("Mac mismatch", row_text(m.rows[4]).c_str());
  TEST_ASSERT_EQUAL_STRING("↑3 3 notes wait for the Mac", row_text(m.rows[5]).c_str());
  TEST_ASSERT_EQUAL_STRING("!1 1 session needs you", row_text(m.rows[6]).c_str());
  TEST_ASSERT_EQUAL_STRING("R1 1 recording to sync", row_text(m.rows[7]).c_str());
  TEST_ASSERT_EQUAL_STRING("※k2 Typed too fast: 2 lost", row_text(m.rows[8]).c_str());
  s.outbox.pending = 1;
  s.rec.files_n = 2;
  const MenuContent k = menu_content(MenuPage::Control, over(Layer::List, false), s, sys, kFakeMap);
  TEST_ASSERT_EQUAL_STRING("↑1 1 note waits for the Mac", row_text(k.rows[5]).c_str());
  TEST_ASSERT_EQUAL_STRING("R2 2 recordings to sync", row_text(k.rows[7]).c_str());
  TEST_ASSERT_EQUAL_STRING("Here", menu_page_name(MenuPage::Here));
  TEST_ASSERT_EQUAL_STRING("Go", menu_page_name(MenuPage::Goto));
  TEST_ASSERT_EQUAL_STRING("Control", menu_page_name(MenuPage::Control));
  AppState synced;
  synced.link.mode = LinkMode::Ready;
  const MenuContent ok = menu_content(MenuPage::Control, over(Layer::List, false), synced, sys, kFakeMap);
  TEST_ASSERT_EQUAL_STRING("√ Synced: all on the Mac", row_text(ok.rows[ok.n - 1]).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_global_binding_appears_once_on_goto_or_control);
  RUN_TEST(test_goto_lists_places_first);
  RUN_TEST(test_goto_order_with_the_fake_table);
  RUN_TEST(test_control_page_rows);
  RUN_TEST(test_control_page_explains_the_status_bar);
  RUN_TEST(test_a_global_lock_key_is_on_control_not_goto);
  RUN_TEST(test_control_facts_when_unknown_or_small);
  RUN_TEST(test_no_lock_row_over_a_confirm_box);
  RUN_TEST(test_here_rows_carry_the_trigger_and_cover_their_layer);
  RUN_TEST(test_system_rows_follow_the_page_rows);
  RUN_TEST(test_hands_free_rows);
  RUN_TEST(test_here_never_lists_tab_or_a_key_twice);
  RUN_TEST(test_system_rows_from_kkeymap);
  RUN_TEST(test_module_rows_replace_the_layer_rows);
  RUN_TEST(test_a_long_module_label_is_cut_between_characters);
  RUN_TEST(test_digit_runs_collapse_into_one_shown_only_row);
  RUN_TEST(test_a_broken_run_stays_apart);
  RUN_TEST(test_badges_on_the_board_and_inbox_rows);
  RUN_TEST(test_overlay_mode_has_no_goto_page);
  RUN_TEST(test_overflow_is_flagged);
  RUN_TEST(test_every_row_fits_its_column);
  RUN_TEST(test_control_page_in_english);
  return UNITY_END();
}
