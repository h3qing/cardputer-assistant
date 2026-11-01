// Tab menu keys and drawing (docs/13 §6.1): Enter replays exactly the selected row's
// key; shown-only rows do nothing; letters on 去哪 / 控制, and on 本页 when not over an
// overlay, replay Opt+letter (§3.3: Tab, then a letter); Tab and `
// close; the list scrolls to keep the selection on screen, and a scroll bar shows rows off
// it; the status bar row (● and the timer while recording) is never drawn over; every drawn
// line fits 240 px. Rows come from kKeymap generically, plus a fake table where the System
// layer is needed (it is V40-F's). v0.5 (docs/19 §6.5): the menu is a sheet over the
// dimmed page: a segmented control for the pages, rows of [key] label, 6 to a screen.
#include <unity.h>

#include <string>
#include <vector>

#include "menu_view.h"
#include "status_bar.h"
#include "support/fake_canvas.h"
#include "theme_palette.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

static bool theme_contrast_ok(Color ink, Color ground) { return theme::contrast(ink, ground) >= 4.5; }

static KeyEvent chr(char c) { return KeyEvent{KeyEventKind::Char, c, 0, 0, Nav::None}; }
static KeyEvent opt(char k) { return KeyEvent{KeyEventKind::Chord, 0, kModOpt, k, Nav::None}; }
static KeyEvent nav(Nav n) { return KeyEvent{KeyEventKind::Nav, 0, 0, 0, n}; }
static KeyEvent g0(KeyEventKind k) { return KeyEvent{k, 0, 0, 0, Nav::None}; }

static const Binding kFake[] = {
    {Layer::Global, Action::Home, 0, opt('h')},
    {Layer::System, Action::Menu, 0, nav(Nav::Menu)},
    {Layer::System, Action::Lock, 0, opt('l')},
    {Layer::RecPage, Action::RecStar, 0, chr(' ')},
};
static const KeymapView kFakeMap{kFake, sizeof(kFake) / sizeof(kFake[0])};

static const HelpRow kModRows[] = {
    {chr(' '), "空格", "加 ★"},
    {KeyEvent{}, "1-9", "归到项目"},
    {opt('d'), "Opt+D", "隐藏"},
};


static MenuSource over(Layer l, bool overlay, const char* title = "") {
  MenuSource src;
  src.layer = l;
  src.overlay = overlay;
  src.title = title;
  return src;
}

static MenuSource module_page() {
  MenuSource src = over(Layer::List, false, "录音");
  src.help = kModRows;
  src.help_n = 3;
  return src;
}

static MenuContent content(MenuPage p, const MenuSource& src, const KeymapView& km = KeymapView{}) {
  return menu_content(p, src, AppState{}, SysInfo{}, km);
}

static MenuStep press(const MenuState& st, const KeyEvent& ev, const MenuContent& m) { return menu_key(st, ev, m); }

// The state after moving down to row r with '.'.
static MenuState at_row(MenuPage p, const MenuContent& m, uint8_t r) {
  MenuState st;
  st.page = p;
  for (uint8_t i = 0; i < r; ++i) st = press(st, chr('.'), m).state;
  return st;
}

static std::vector<MenuSource> sources() {
  return {over(Layer::List, false), over(Layer::Compose, true, "记一条"), over(Layer::Confirm, true, "确认"),
          over(Layer::RecPage, true, "会议录音"), module_page()};
}

static void test_enter_replays_exactly_the_selected_row() {
  int replayed = 0;
  for (const KeymapView& km : {KeymapView{}, kFakeMap}) {
    for (const MenuSource& src : sources()) {
      for (const MenuPage p : {MenuPage::Here, MenuPage::Goto, MenuPage::Control}) {
        const MenuContent m = content(p, src, km);
        for (uint8_t r = 0; r < m.n; ++r) {
          const MenuState st = at_row(p, m, r);
          TEST_ASSERT_EQUAL_UINT8(r, st.sel);
          const MenuStep out = press(st, nav(Nav::Enter), m);
          const std::string what = std::string(m.rows[r].keys.c_str()) + " " + m.rows[r].label.c_str();
          if (m.rows[r].ev.kind == KeyEventKind::None) {
            TEST_ASSERT_TRUE_MESSAGE(out.out == MenuOut::None, what.c_str());
            continue;
          }
          TEST_ASSERT_TRUE_MESSAGE(out.out == MenuOut::Replay, what.c_str());
          TEST_ASSERT_TRUE_MESSAGE(out.ev == m.rows[r].ev, what.c_str());
          ++replayed;
        }
      }
    }
  }
  TEST_ASSERT_TRUE(replayed > 20);
}

static void test_enter_on_a_range_or_fact_row_does_nothing() {
  const MenuContent go = content(MenuPage::Goto, over(Layer::List, false));
  int ranges = 0;
  for (uint8_t r = 0; r < go.n; ++r) {
    if (go.rows[r].span < 2) continue;
    ++ranges;
    TEST_ASSERT_TRUE(press(at_row(MenuPage::Goto, go, r), nav(Nav::Enter), go).out == MenuOut::None);
  }
  TEST_ASSERT_TRUE(ranges > 0);  // Opt+1-9
  const MenuContent here = content(MenuPage::Here, module_page());
  TEST_ASSERT_TRUE(press(at_row(MenuPage::Here, here, 1), nav(Nav::Enter), here).out == MenuOut::None);  // "1-9"
  const MenuContent ctl = content(MenuPage::Control, over(Layer::List, false));
  TEST_ASSERT_TRUE(press(at_row(MenuPage::Control, ctl, ctl.n - 1), nav(Nav::Enter), ctl).out == MenuOut::None);
  const MenuContent empty;
  TEST_ASSERT_TRUE(press(MenuState{}, nav(Nav::Enter), empty).out == MenuOut::None);
}

static bool shows(const MenuContent& m, const KeyEvent& ev) {
  for (uint8_t r = 0; r < m.n; ++r) {
    if (m.rows[r].ev == ev) return true;
  }
  return false;
}

static void test_a_bound_letter_on_goto_or_control_replays_opt_letter() {
  const MenuSource src = over(Layer::List, false);
  const MenuContent go = content(MenuPage::Goto, src);
  const MenuContent ctl = content(MenuPage::Control, src);
  int bound = 0;
  for (char c = 'a'; c <= 'z'; ++c) {
    const std::string what(1, c);
    for (const MenuContent* m : {&go, &ctl}) {
      MenuState st;
      st.page = m->page;
      for (const char typed : {c, static_cast<char>(c - 'a' + 'A')}) {
        const MenuStep out = press(st, chr(typed), *m);
        const bool want = shows(*m, opt(c));
        TEST_ASSERT_TRUE_MESSAGE(out.out == (want ? MenuOut::Replay : MenuOut::None), what.c_str());
        if (want) TEST_ASSERT_TRUE_MESSAGE(out.ev == opt(c), what.c_str());
      }
    }
    if (keymap_lookup(Layer::Global, opt(c)).action == Action::None) continue;
    ++bound;  // every letter the Global layer binds works from 去哪 or 控制
    TEST_ASSERT_TRUE_MESSAGE(shows(go, opt(c)) || shows(ctl, opt(c)), what.c_str());
  }
  TEST_ASSERT_TRUE(bound >= 5);  // Opt+H/I/N/Q/S/M/R in v0.3
}

// Tab, then a letter (docs/13 §3.3): on 本页 a letter is Opt+letter when the Global or
// System layer binds it, whatever the page's own rows are; over an overlay it is eaten.
static void test_a_bound_letter_on_here_replays_opt_letter() {
  const MenuContent here = content(MenuPage::Here, module_page());
  int bound = 0;
  for (char c = 'a'; c <= 'z'; ++c) {
    const std::string what(1, c);
    const bool want = keymap_lookup(Layer::Global, opt(c)).action != Action::None ||
                      keymap_lookup(Layer::System, opt(c)).action != Action::None;
    bound += want ? 1 : 0;
    for (const char typed : {c, static_cast<char>(c - 'a' + 'A')}) {
      const MenuStep out = press(MenuState{}, chr(typed), here);
      TEST_ASSERT_TRUE_MESSAGE(out.out == (want ? MenuOut::Replay : MenuOut::None), what.c_str());
      if (want) TEST_ASSERT_TRUE_MESSAGE(out.ev == opt(c), what.c_str());
    }
  }
  TEST_ASSERT_TRUE(bound >= 5);
  TEST_ASSERT_TRUE(press(MenuState{}, chr('3'), here).out == MenuOut::None);
  TEST_ASSERT_TRUE(press(MenuState{}, chr(' '), here).out == MenuOut::None);  // a row's key, not a letter
}

static void test_letters_on_here_and_lock_on_control() {
  const MenuContent here = content(MenuPage::Here, over(Layer::RecPage, true), kFakeMap);
  for (const char c : {'l', 'h', 'i'}) {  // 本页 over an overlay: the menu eats them
    TEST_ASSERT_TRUE(press(MenuState{}, chr(c), here).out == MenuOut::None);
  }
  const MenuContent ctl = content(MenuPage::Control, over(Layer::RecPage, true), kFakeMap);
  MenuState st;
  st.page = MenuPage::Control;
  const MenuStep out = press(st, chr('l'), ctl);
  TEST_ASSERT_TRUE(out.out == MenuOut::Replay);
  TEST_ASSERT_TRUE(out.ev == opt('l'));
  TEST_ASSERT_TRUE(press(st, chr('h'), ctl).out == MenuOut::None);  // over an overlay: no jumping away
  TEST_ASSERT_TRUE(press(st, chr('3'), ctl).out == MenuOut::None);
}

static void test_tab_and_back_keys_close() {
  const MenuContent m = content(MenuPage::Here, over(Layer::List, false));
  for (const KeyEvent& ev : {nav(Nav::Menu), chr('`'), nav(Nav::Bksp), nav(Nav::Back)}) {
    TEST_ASSERT_TRUE(press(MenuState{}, ev, m).out == MenuOut::Close);
  }
  TEST_ASSERT_TRUE(press(MenuState{}, opt('d'), m).out == MenuOut::None);
  TEST_ASSERT_TRUE(press(MenuState{}, g0(KeyEventKind::G0LongUp), m).out == MenuOut::None);
}

static void test_g0_closes_and_replays_itself() {
  const MenuContent m = content(MenuPage::Here, over(Layer::RecPage, true));
  for (const KeyEventKind k : {KeyEventKind::G0Tap, KeyEventKind::G0Long}) {
    const MenuStep out = press(MenuState{}, g0(k), m);
    TEST_ASSERT_TRUE(out.out == MenuOut::Replay);
    TEST_ASSERT_TRUE(out.ev == g0(k));
  }
}

static MenuPage after(MenuPage p, char key, bool overlay) {
  const MenuSource src = over(Layer::List, overlay);
  MenuState st;
  st.page = p;
  st.sel = 2;
  st.top = 1;
  const MenuStep out = press(st, chr(key), content(p, src));
  TEST_ASSERT_TRUE(out.out == MenuOut::None);
  TEST_ASSERT_EQUAL_UINT8(0, out.state.sel);
  TEST_ASSERT_EQUAL_UINT8(0, out.state.top);
  return out.state.page;
}

static void test_pages_wrap_and_an_overlay_has_two() {
  TEST_ASSERT_TRUE(after(MenuPage::Here, '/', false) == MenuPage::Goto);
  TEST_ASSERT_TRUE(after(MenuPage::Goto, '/', false) == MenuPage::Control);
  TEST_ASSERT_TRUE(after(MenuPage::Control, '/', false) == MenuPage::Here);
  TEST_ASSERT_TRUE(after(MenuPage::Here, ',', false) == MenuPage::Control);
  TEST_ASSERT_TRUE(after(MenuPage::Control, ',', false) == MenuPage::Goto);
  TEST_ASSERT_TRUE(after(MenuPage::Here, '/', true) == MenuPage::Control);
  TEST_ASSERT_TRUE(after(MenuPage::Control, '/', true) == MenuPage::Here);
  TEST_ASSERT_TRUE(after(MenuPage::Here, ',', true) == MenuPage::Control);
  TEST_ASSERT_TRUE(after(MenuPage::Goto, '/', true) == MenuPage::Control);  // Goto reads as 本页 there
}

// The top of row r's capsule; its key and label are drawn a pixel lower.
static int16_t row_y(const MenuState& st, uint8_t r) { return static_cast<int16_t>(36 + kMenuRowH * (r - st.top)); }

// The text drawn at y that holds s.
static const TextOp* text_on(const FakeCanvas& c, int16_t y, const char* s) {
  for (const TextOp& t : c.texts) {
    if (t.y == y && t.text.find(s) != std::string::npos) return &t;
  }
  return nullptr;
}

// docs/20 B6: a selected row that only explains a key (a hold, a range) is a Select capsule
// with its words in Ink, not a caramel one (test_copy_ux).
static void check_visible(const MenuState& st, const MenuContent& m) {
  TEST_ASSERT_TRUE(st.top <= st.sel);
  TEST_ASSERT_TRUE(st.sel < st.top + kMenuVisibleRows);
  FakeCanvas c;
  menu_render(c, st, m);
  const MenuRow& r = m.rows[st.sel];
  const bool explains = r.ev.kind == KeyEventKind::None && !r.fact;
  TEST_ASSERT_TRUE(c.filled_at(row_y(st, st.sel), explains ? Color::Select : Color::Accent));
  // on the selected row: docs/20 U22's "记一条" is also inside the G0 row's label
  const TextOp* label = text_on(c, static_cast<int16_t>(row_y(st, st.sel) + 1), r.label.c_str());
  TEST_ASSERT_NOT_NULL(label);
  TEST_ASSERT_TRUE(label->style.fg == (explains ? Color::Ink : Color::OnAccent));
}

static void test_scrolling_keeps_the_selection_visible() {
  const MenuContent go = content(MenuPage::Goto, over(Layer::List, false));
  TEST_ASSERT_TRUE(go.n > kMenuVisibleRows);
  MenuState st;
  st.page = MenuPage::Goto;
  for (uint8_t i = 0; i < go.n + 3; ++i) {
    check_visible(st, go);
    st = press(st, chr('.'), go).state;
  }
  TEST_ASSERT_EQUAL_UINT8(go.n - 1, st.sel);  // clamped at the end
  TEST_ASSERT_EQUAL_UINT8(go.n - kMenuVisibleRows, st.top);
  for (uint8_t i = 0; i < go.n + 3; ++i) {
    st = press(st, chr(';'), go).state;
    check_visible(st, go);
  }
  TEST_ASSERT_EQUAL_UINT8(0, st.sel);
  TEST_ASSERT_EQUAL_UINT8(0, st.top);
}

// The scroll bar's thumb (Ink3 on the Line track at x 237): its top and height.
static bool thumb(const FakeCanvas& c, Rect& out) {
  for (const auto& f : c.fills) {
    if (f.second == Color::Ink3 && f.first.x == 237 && f.first.w == 2) {
      out = f.first;
      return true;
    }
  }
  return false;
}

// Rows off screen show as a scroll bar (docs/13 §6.3: "往下翻还有…"): the thumb moves down
// as the list does; a page that fits has none.
static void test_rows_off_screen_show_a_scroll_bar() {
  const MenuContent go = content(MenuPage::Goto, over(Layer::List, false));
  TEST_ASSERT_TRUE(go.n > kMenuVisibleRows);
  MenuState st;
  st.page = MenuPage::Goto;
  int16_t last = -1;
  for (uint8_t i = 0; i < go.n; ++i, st = press(st, chr('.'), go).state) {
    FakeCanvas c;
    menu_render(c, st, go);
    Rect t;
    TEST_ASSERT_TRUE(thumb(c, t));
    TEST_ASSERT_EQUAL_INT16(kMenuVisibleRows * kMenuRowH * kMenuVisibleRows / go.n, t.h);
    TEST_ASSERT_TRUE(t.y >= last && t.y >= 36 && t.y + t.h <= 36 + kMenuVisibleRows * kMenuRowH);
    TEST_ASSERT_EQUAL(st.top > 0, t.y > 36);
    last = t.y;
  }
  // a page that fits: 本页 over a confirm box (docs/20 U21's legend makes 控制 scroll even
  // over a confirm box on the sheet's six rows)
  const MenuContent fits = content(MenuPage::Here, over(Layer::Confirm, true));
  TEST_ASSERT_TRUE(fits.n <= kMenuVisibleRows);
  FakeCanvas c;
  menu_render(c, MenuState{}, fits);
  Rect t;
  TEST_ASSERT_FALSE(thumb(c, t));
}

static void test_a_stale_selection_is_clamped() {
  const MenuContent ctl = content(MenuPage::Control, over(Layer::List, false));
  MenuState st;
  st.page = MenuPage::Control;
  st.sel = 12;  // the page had more rows before
  st.top = 9;
  const MenuStep up = press(st, chr(';'), ctl);
  TEST_ASSERT_EQUAL_UINT8(ctl.n - 2, up.state.sel);
  TEST_ASSERT_TRUE(up.state.top <= up.state.sel);
  FakeCanvas c;
  menu_render(c, st, ctl);  // draws the last row selected
  TEST_ASSERT_TRUE(c.has(ctl.rows[ctl.n - 1].label.c_str()));
}

static AppState recording_meeting() {
  AppState s;
  s.rec.phase = RecPhase::Recording;
  s.rec.mode = RecMode::Meeting;
  s.rec.started_ms = 1000;
  return s;
}

// The menu dims the page and draws its sheet below the status bar, so ● and the timer
// stay while recording; its own hint goes in the hint bar.
static void test_the_status_bar_row_stays() {
  const AppState s = recording_meeting();
  const MenuContent m = content(MenuPage::Here, over(Layer::RecPage, true, "会议录音"));
  FakeCanvas c;
  render_status_bar(c, s, 43000);
  const size_t texts = c.texts.size(), fills = c.fills.size();
  menu_render(c, MenuState{}, m);
  TEST_ASSERT_TRUE(c.row(0).find("● 0:42") != std::string::npos);
  for (size_t i = texts; i < c.texts.size(); ++i) TEST_ASSERT_TRUE(c.texts[i].y >= kBarH);
  for (size_t i = fills; i < c.fills.size(); ++i) TEST_ASSERT_TRUE(c.fills[i].first.y >= kBarH);
  TEST_ASSERT_EQUAL_UINT32(1, c.dims.size());
  TEST_ASSERT_TRUE(c.dims[0].y == kContentRect.y && c.dims[0].h == kContentRect.h && c.dims[0].w == kScreenW);
}

// The page the menu shows: the tab drawn on the caramel knob ("" for none).
static std::string active_tab(const FakeCanvas& c) {
  for (const TextOp& t : c.texts) {
    if (t.y == 19 && t.style.fg == Color::OnAccent) return t.text;
  }
  return "";
}

static void test_tabs_and_hint() {
  FakeCanvas c;
  menu_render(c, MenuState{}, content(MenuPage::Here, over(Layer::RecPage, true, "会议录音")));
  TEST_ASSERT_EQUAL_STRING("本页", active_tab(c).c_str());
  TEST_ASSERT_TRUE(c.has("控制"));
  TEST_ASSERT_FALSE(c.has("去哪"));  // over an overlay
  TEST_ASSERT_EQUAL_STRING(";. 选 Enter 做 ,/ 换页 Tab 关", c.line(kScreenH - kBarH).c_str());
  FakeCanvas g;
  MenuState st;
  st.page = MenuPage::Goto;
  menu_render(g, st, content(MenuPage::Goto, over(Layer::List, false)));
  TEST_ASSERT_EQUAL_STRING("去哪", active_tab(g).c_str());
  TEST_ASSERT_TRUE(g.has("本页") && g.has("控制"));
  // mockup 03: letters still go straight there, but the hint leaves 字母直达 out (it pushed
  // the bar to its tightest spacing)
  TEST_ASSERT_EQUAL_STRING(";. 选 Enter 做 ,/ 换页 Tab 关", g.line(kScreenH - kBarH).c_str());
  TEST_ASSERT_EQUAL_INT16(6, g.find(";.")->x);  // the roomiest spacing
}

// What a text is drawn on: its cell background, or for glyphs only the last fill under it.
static Color ground_of(const FakeCanvas& c, const TextOp& t) {
  if (t.style.bg != t.style.fg) return t.style.bg;
  Color g = Color::Bg;
  for (const auto& f : c.fills) {
    const Rect& r = f.first;
    if (r.x <= t.x + 3 && t.x + 3 < r.x + r.w && r.y <= t.y + 6 && t.y + 6 < r.y + r.h) g = f.second;
  }
  return g;
}

// The judges' 4.5:1 (Ink3 on a Cap is 3.5:1): a fact on 控制 is a word, not a key, drawn
// plain in Ink3 on the sheet; a shown-only row's key is Ink3 on a Raised cap.
static void test_shown_only_keys_read() {
  AppState s;
  s.device.bat_mv = 4021;
  MenuState st;
  st.page = MenuPage::Control;
  FakeCanvas c;
  menu_render(c, st, menu_content(MenuPage::Control, over(Layer::List, false), s, SysInfo{}, kFakeMap));
  for (const char* fact : {"声音", "电池", "SD", "Mac"}) {
    const TextOp* t = c.find(fact);
    TEST_ASSERT_NOT_NULL_MESSAGE(t, fact);
    TEST_ASSERT_TRUE_MESSAGE(t->style.fg == Color::Ink3, fact);
    TEST_ASSERT_TRUE_MESSAGE(ground_of(c, *t) == Color::Card, fact);  // no keycap under it
    TEST_ASSERT_TRUE(theme_contrast_ok(t->style.fg, ground_of(c, *t)));
  }
  FakeCanvas m;
  menu_render(m, MenuState{}, content(MenuPage::Here, module_page()));
  const TextOp* range = m.find("1-9");
  TEST_ASSERT_TRUE(range->style.fg == Color::Ink3 && ground_of(m, *range) == Color::Raised);
  TEST_ASSERT_TRUE(theme_contrast_ok(Color::Ink3, Color::Raised));
  const TextOp* key = m.find("Opt+D");
  TEST_ASSERT_TRUE(key->style.fg == Color::Ink2 && ground_of(m, *key) == Color::Cap);
}

// 控制's facts and the status bar's legend (docs/20 U21) are words, not keys: their values
// share one column, after the widest of them, whatever the keys' common width is.
static void test_facts_share_one_column() {
  AppState s;
  s.outbox.pending = 1;
  s.rec.files_n = 2;
  s.sessions.data.n = 1;
  s.sessions.data.rows[0].need = true;
  const MenuContent m = menu_content(MenuPage::Control, over(Layer::List, false), s, SysInfo{}, kFakeMap);
  FakeCanvas c;
  MenuState st;
  st.page = MenuPage::Control;
  menu_render(c, st, m);
  const int16_t x = c.find("关（录音不出声）")->x;
  TEST_ASSERT_EQUAL_INT16(x, c.find("--")->x);  // 电池, no reading
  TEST_ASSERT_EQUAL_INT16(x, c.find("剩 --")->x);
  TEST_ASSERT_EQUAL_INT16(x, c.find("离线")->x);
  TEST_ASSERT_EQUAL_INT16(x, c.find("1 条笔记等着传到 Mac")->x);
  TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(menu_sheet_x(c, m) + 4 + 2 + c.textWidth("声音") + 5), x);
  FakeCanvas end;
  menu_render(end, at_row(MenuPage::Control, m, m.n - 1), m);
  TEST_ASSERT_EQUAL_INT16(x, end.find("1 个 session 等你")->x);
  TEST_ASSERT_EQUAL_INT16(x, end.find("2 段录音等着传")->x);
}

// Labels start after the common cap (2 + 12 + 5 for one letter, mockup 03); a wider cap
// pushes only its own row. With the keys no longer repeated in the labels, the sheet starts
// at x 66 on home's 本页 and 去哪, so the page stays in view.
static void test_labels_follow_the_common_cap() {
  static const HelpRow kTiles[] = {{opt('p'), "P", "项目"}, {nav(Nav::Menu), "Tab", "菜单"},
                                   {KeyEvent{}, "1-9", "第 N 个项目页"}, {opt('n'), "N", "记一条"}};
  MenuSource src = over(Layer::List, false);
  src.help = kTiles;
  src.help_n = 4;
  const MenuContent m = content(MenuPage::Here, src, kFakeMap);
  FakeCanvas c;
  menu_render(c, MenuState{}, m);
  TEST_ASSERT_EQUAL_INT16(66, menu_sheet_x(c, m));
  const int16_t row_x = 66 + 4;
  TEST_ASSERT_EQUAL_INT16(row_x + 19, c.find("项目")->x);
  TEST_ASSERT_EQUAL_INT16(row_x + 19, c.find("记一条")->x);
  TEST_ASSERT_EQUAL_INT16(row_x + 29, c.find("菜单")->x);
  TEST_ASSERT_EQUAL_INT16(row_x + 29, c.find("第 N 个项目页")->x);
  TEST_ASSERT_EQUAL_INT16(row_x + 41, c.find("锁屏")->x);  // Opt+L
  TEST_ASSERT_EQUAL_INT16(66, menu_sheet_x(c, content(MenuPage::Goto, over(Layer::List, false))));
}

// mockup 03: 跑N and !N at the right end of their rows (Ink3 and Warn), OnAccent on the
// caramel row (mustard on caramel is 1.2:1).
static void test_badges_sit_at_the_row_end() {
  static const HelpRow kTiles[] = {{opt('p'), "P", "项目"}, {opt('i'), "I", "等你"}};
  MenuSource src = over(Layer::List, false);
  src.help = kTiles;
  src.help_n = 2;
  AppState s;
  s.projects.live = true;
  s.projects.data.n = 1;
  s.projects.data.rows[0].run = 2;
  s.sessions.data.n = 1;
  s.sessions.data.rows[0].need = true;
  const MenuContent m = menu_content(MenuPage::Here, src, s, SysInfo{});
  FakeCanvas c;
  menu_render(c, MenuState{}, m);
  const TextOp* run = c.find("跑2");
  const TextOp* need = c.find("!1");
  TEST_ASSERT_TRUE(run != nullptr && need != nullptr);
  TEST_ASSERT_EQUAL_INT16(240 - 4 - 6, run->x + c.textWidth("跑2"));
  TEST_ASSERT_EQUAL_INT16(240 - 4 - 6, need->x + c.textWidth("!1"));
  TEST_ASSERT_TRUE(run->style.fg == Color::OnAccent);  // row 0 is selected
  TEST_ASSERT_TRUE(need->style.fg == Color::Warn);
  FakeCanvas d;
  menu_render(d, at_row(MenuPage::Here, m, 1), m);
  TEST_ASSERT_TRUE(d.find("跑2")->style.fg == Color::Ink3 && d.find("!1")->style.fg == Color::OnAccent);
}

// The sheet starts at x 66 and moves left for a page whose widest row needs it (never
// past x 6), so labels are not cut.
static void test_sheet_width_follows_the_rows() {
  FakeCanvas c;
  TEST_ASSERT_EQUAL_INT16(66, menu_sheet_x(c, content(MenuPage::Here, module_page())));
  const int16_t ctl = menu_sheet_x(c, content(MenuPage::Control, over(Layer::List, false), kFakeMap));
  TEST_ASSERT_TRUE(ctl >= 6 && ctl < 66);
  TEST_ASSERT_EQUAL_INT16(66, menu_sheet_x(c, MenuContent{}));
}

static void test_an_empty_page_says_so() {
  FakeCanvas c;
  menu_render(c, MenuState{}, MenuContent{});
  TEST_ASSERT_TRUE(c.has("这里没有可列出的键"));
}

// Every text op inside 240 px; with `whole`, every visible row drawn uncut.
static void check_fits(const FakeCanvas& c, const MenuContent& m, const MenuState& st, bool whole) {
  for (const TextOp& t : c.texts) {
    TEST_ASSERT_TRUE_MESSAGE(t.x >= 0 && t.x + c.textWidth(t.text.c_str()) <= kScreenW, t.text.c_str());
  }
  const uint8_t end = st.top + kMenuVisibleRows < m.n ? st.top + kMenuVisibleRows : m.n;
  for (uint8_t r = st.top; whole && r < end; ++r) {
    TEST_ASSERT_TRUE_MESSAGE(c.has(m.rows[r].keys.c_str()), m.rows[r].keys.c_str());
    TEST_ASSERT_TRUE_MESSAGE(c.has(m.rows[r].label.c_str()), m.rows[r].label.c_str());
  }
}

static void check_all_pages_fit(const AppState& s, const SysInfo& sys, bool whole) {
  for (const KeymapView& km : {KeymapView{}, kFakeMap}) {
    for (const MenuSource& src : sources()) {
      for (const MenuPage p : {MenuPage::Here, MenuPage::Goto, MenuPage::Control}) {
        const MenuContent m = menu_content(p, src, s, sys, km);
        for (uint8_t r = 0; r < m.n || r == 0; ++r) {
          FakeCanvas c;
          const MenuState st = at_row(m.page, m, r);
          menu_render(c, st, m);
          check_fits(c, m, st, whole);
        }
      }
    }
  }
}

static void test_every_rendered_line_fits_240_px() {
  AppState s = recording_meeting();
  s.device.bat_mv = 3999;
  s.rec.free_known = true;
  s.rec.free_mib = 999999;
  s.rec.files_n = 999;
  s.outbox.pending = 999;
  s.link.mode = LinkMode::Attached;
  s.link.version_mismatch = true;
  SysInfo sys;
  sys.host_app = FixedStr<32>::from("cpad/0.10.10");
  check_all_pages_fit(s, sys, true);  // the widest real values: nothing is cut
  sys.host_app = FixedStr<32>::from("cpad/10.20.30-rc.4+build.5678901");
  check_all_pages_fit(s, sys, false);  // a 32-byte host name is cut, still inside 240 px
}

static void test_page_names_and_battery_curve() {
  TEST_ASSERT_EQUAL_STRING("本页", menu_page_name(MenuPage::Here));
  TEST_ASSERT_EQUAL_STRING("去哪", menu_page_name(MenuPage::Goto));
  TEST_ASSERT_EQUAL_STRING("控制", menu_page_name(MenuPage::Control));
  TEST_ASSERT_EQUAL_INT(-1, menu_battery_pct(0));
  TEST_ASSERT_EQUAL_INT(0, menu_battery_pct(3000));
  TEST_ASSERT_EQUAL_INT(0, menu_battery_pct(3300));
  TEST_ASSERT_EQUAL_INT(84, menu_battery_pct(4021));
  TEST_ASSERT_EQUAL_INT(100, menu_battery_pct(4150));
  TEST_ASSERT_EQUAL_INT(100, menu_battery_pct(4400));
}

static void test_battery_matches_the_status_bar() {
  for (uint16_t mv = 0; mv < 5000; ++mv) TEST_ASSERT_EQUAL_INT(battery_pct(mv), menu_battery_pct(mv));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_enter_replays_exactly_the_selected_row);
  RUN_TEST(test_enter_on_a_range_or_fact_row_does_nothing);
  RUN_TEST(test_a_bound_letter_on_goto_or_control_replays_opt_letter);
  RUN_TEST(test_a_bound_letter_on_here_replays_opt_letter);
  RUN_TEST(test_letters_on_here_and_lock_on_control);
  RUN_TEST(test_tab_and_back_keys_close);
  RUN_TEST(test_g0_closes_and_replays_itself);
  RUN_TEST(test_pages_wrap_and_an_overlay_has_two);
  RUN_TEST(test_scrolling_keeps_the_selection_visible);
  RUN_TEST(test_rows_off_screen_show_a_scroll_bar);
  RUN_TEST(test_a_stale_selection_is_clamped);
  RUN_TEST(test_the_status_bar_row_stays);
  RUN_TEST(test_tabs_and_hint);
  RUN_TEST(test_shown_only_keys_read);
  RUN_TEST(test_labels_follow_the_common_cap);
  RUN_TEST(test_facts_share_one_column);
  RUN_TEST(test_badges_sit_at_the_row_end);
  RUN_TEST(test_sheet_width_follows_the_rows);
  RUN_TEST(test_an_empty_page_says_so);
  RUN_TEST(test_every_rendered_line_fits_240_px);
  RUN_TEST(test_page_names_and_battery_curve);
  RUN_TEST(test_battery_matches_the_status_bar);
  return UNITY_END();
}
