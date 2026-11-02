// The public theme pieces (docs/19 §4.1) on FakeCanvas: a card always has an edge that
// shows (a lighter top edge or an outline), keycaps and pills measure their text, tiles and
// toasts take their tone's colours, and the hint bar draws keycaps or falls back to v0.4's
// one line when they do not fit. S2: Mando with his bubble, the live-words capsule.
#include <unity.h>

#include <string>

#include "support/fake_canvas.h"
#include "theme.h"
#include "theme_mando.h"

using namespace cpa;
using namespace cpa::theme;
using namespace uitest;

void setUp() {}
void tearDown() {}

static bool has_fill(const FakeCanvas& c, const Rect& r, Color col) {
  for (const auto& f : c.fills) {
    if (f.second == col && f.first.x == r.x && f.first.y == r.y && f.first.w == r.w && f.first.h == r.h) return true;
  }
  return false;
}

static void test_cards_have_an_edge() {
  FakeCanvas c;
  card(c, Rect{4, 29, 232, 28});
  TEST_ASSERT_TRUE(has_fill(c, Rect{4, 29, 232, 28}, Color::Card));
  TEST_ASSERT_TRUE(has_fill(c, Rect{9, 29, 222, 1}, Color::CardRim));  // the straight top edge
  c.clear();
  card(c, Rect{4, 29, 232, 28}, CardKind::Selected);
  TEST_ASSERT_TRUE(has_fill(c, Rect{4, 29, 232, 28}, Color::Live));
  TEST_ASSERT_TRUE(has_fill(c, Rect{5, 30, 230, 26}, Color::Select));
  c.clear();
  card(c, Rect{12, 8, 216, 94}, CardKind::Sheet, Color::Bg, 10);
  TEST_ASSERT_TRUE(has_fill(c, Rect{12, 8, 216, 94}, Color::Sheet));
  TEST_ASSERT_TRUE(has_fill(c, Rect{21, 8, 198, 1}, Color::Rim));
  TEST_ASSERT_TRUE(card_fill(CardKind::Plain) == Color::Card);
  TEST_ASSERT_TRUE(card_fill(CardKind::Selected) == Color::Select);
}

static void test_keycaps_measure_their_key() {
  FakeCanvas c;
  TEST_ASSERT_EQUAL_INT16(34, keycap_width(c, "Enter"));
  Keycap wide;
  wide.min_w = 12;
  TEST_ASSERT_EQUAL_INT16(12, keycap_width(c, "P", wide));
  TEST_ASSERT_EQUAL_INT16(12, keycap(c, 10, 40, "P", wide));
  TEST_ASSERT_TRUE(has_fill(c, Rect{10, 40, 12, 12}, Color::Cap));
  const TextOp* p = c.find("P");
  TEST_ASSERT_EQUAL_INT16(13, p->x);  // centred
  TEST_ASSERT_TRUE(p->style.fg == Color::Ink2 && p->style.bg == Color::Ink2);  // glyphs only
}

static void test_pills_take_their_tone() {
  FakeCanvas c;
  TEST_ASSERT_EQUAL_INT16(26, pill(c, 100, 2, "跑1", Tone::Ok, Color::Card));
  TEST_ASSERT_TRUE(has_fill(c, Rect{100, 2, 26, 12}, Color::OkBg));
  TEST_ASSERT_TRUE(c.find("跑1")->style.fg == Color::Ok);
  TEST_ASSERT_EQUAL_INT16(104, c.find("跑1")->x);
  PillSize chip;
  chip.h = 13;
  chip.pad = 5;
  TEST_ASSERT_EQUAL_INT16(46, pill_width(c, "等你 1", chip));
  const struct {
    Tone t;
    Color fill;
    Color ink;
  } tones[] = {{Tone::Neutral, Color::Raised, Color::Ink2}, {Tone::Info, Color::InfoBg, Color::Accent},
               {Tone::Ok, Color::OkBg, Color::Ok},           {Tone::Warn, Color::WarnBg, Color::Warn},
               {Tone::Rec, Color::RecBg, Color::Rec},         {Tone::Accent, Color::Accent, Color::OnAccent}};
  for (const auto& t : tones) {
    TEST_ASSERT_TRUE(tone_ink(t.t).fill == t.fill);
    TEST_ASSERT_TRUE(tone_ink(t.t).ink == t.ink);
  }
}

static void test_tiles_selected_and_not() {
  FakeCanvas c;
  tile(c, Rect{4, 64, 75, 16}, TileSpec{"P", "项目", "跑2", Color::Ink3}, true);
  TEST_ASSERT_TRUE(has_fill(c, Rect{4, 64, 75, 16}, Color::Accent));
  TEST_ASSERT_TRUE(c.filled_at(66, Color::Accent2));
  TEST_ASSERT_TRUE(c.find("项目")->style.fg == Color::OnAccent);
  TEST_ASSERT_TRUE(c.find("跑2")->style.fg == Color::OnAccent);
  TEST_ASSERT_EQUAL_INT16(4 + 75 - 4 - 18, c.find("跑2")->x);  // right-aligned
  c.clear();
  tile(c, Rect{83, 64, 74, 16}, TileSpec{"I", "等你", "!1", Color::Warn}, false);
  TEST_ASSERT_FALSE(c.filled_at(64, Color::Accent));
  TEST_ASSERT_TRUE(c.filled_at(66, Color::Cap));
  TEST_ASSERT_TRUE(c.find("等你")->style.fg == Color::Ink);
  TEST_ASSERT_TRUE(c.find("!1")->style.fg == Color::Warn);
  TEST_ASSERT_EQUAL_INT16(83 + 2 + 10 + 4, c.find("等你")->x);
}

// docs/20 U22's "开 session" ("new session", 60 px) is wider than a 75 px tile leaves after
// its key: it is drawn whole, still clear of the key and 2 px short of the tile's rounded end.
static void test_a_wide_label_is_whole_and_clear_of_both_ends() {
  FakeCanvas c;
  const Rect r{161, 64, 75, 16};
  tile(c, r, TileSpec{"S", "开 session", "", Color::Ink3}, true);
  const TextOp* t = c.find("开 session");
  TEST_ASSERT_NOT_NULL(t);
  TEST_ASSERT_TRUE(t->x >= r.x + 2 + 10 + 1);  // the 10 px key, then a pixel
  TEST_ASSERT_TRUE(t->x + c.textWidth("开 session") <= r.x + r.w - 2);
}

static void test_title_is_drawn_twice() {
  FakeCanvas c;
  TEST_ASSERT_EQUAL_INT16(25, title(c, 5, 14, "项目"));
  TEST_ASSERT_EQUAL_UINT32(2, c.texts.size());
  TEST_ASSERT_EQUAL_INT16(6, c.texts[1].x);
  TEST_ASSERT_EQUAL_STRING("项目", c.line(14).c_str());
}

static void test_hint_bar_keycaps_and_fallback() {
  FakeCanvas c;
  hint_bar(c, "G0按住说一段 ;.,/移动 Enter打开");
  TEST_ASSERT_EQUAL_STRING("G0 按住说一段 ;.,/ 移动 Enter 打开", c.line(kHintRect.y).c_str());
  TEST_ASSERT_TRUE(c.find("G0")->style.fg == Color::Accent);
  TEST_ASSERT_TRUE(c.find("Enter")->style.fg == Color::Ink2);
  TEST_ASSERT_TRUE(c.find("移动")->style.fg == Color::Ink3);
  TEST_ASSERT_EQUAL_INT16(6, c.find("G0")->x);  // a 16 px cap at x 4
  c.clear();
  const char* tight = "Enter开session Opt+D忽略忽略 Tab菜单";  // 245 px at the roomiest spacing
  hint_bar(c, tight);
  TEST_ASSERT_TRUE(c.has("开session"));
  TEST_ASSERT_EQUAL_INT16(100, c.find("Opt+D")->x);  // 4 px between items, 2 px key to words
  for (const TextOp& t : c.texts) TEST_ASSERT_TRUE(t.x + c.textWidth(t.text.c_str()) <= kScreenW);
  c.clear();
  hint_bar(c, "按住说一段话然后松开结束录音再按回车键保存这一段录音到项目里面去");
  TEST_ASSERT_EQUAL_UINT32(1, c.texts.size());  // too wide: one line, cut
  TEST_ASSERT_TRUE(c.texts[0].style.fg == Color::Ink3);
  TEST_ASSERT_EQUAL_INT16(0, c.texts[0].x);
  c.clear();  // docs/21 §6.6: a bracketed hint that does not fit reads as words, no brackets
  hint_bar(c, "[Enter]open the selected project [Opt+D]dismiss it now [`]go back [Tab]menu");
  TEST_ASSERT_EQUAL_UINT32(1, c.texts.size());
  const std::string line = c.texts[0].text;  // cut at the screen's edge ("…")
  TEST_ASSERT_EQUAL_UINT32(0, line.find("Enter open the selected project  Opt+D"));
  TEST_ASSERT_TRUE(line.find('[') == std::string::npos);
}

// A one-character key is a 12 px cap, as in the menu: a 10 px cap round "`" reads as a smudge.
static void test_hint_one_character_caps_are_12_px() {
  FakeCanvas c;
  hint_bar(c, "`返回 Tab菜单");
  TEST_ASSERT_TRUE(has_fill(c, Rect{4, kHintRect.y, 12, kCapH}, Color::Cap));
  TEST_ASSERT_EQUAL_INT16(7, c.find("`")->x);
  TEST_ASSERT_EQUAL_INT16(19, c.find("返回")->x);
}

// hint_fits is the hint bar's own rule (docs/16 check_apps uses it): keycaps at some spacing
// within the bar; otherwise the bar falls back to one grey line.
static void test_hint_fits_is_the_bars_rule() {
  FakeCanvas c;
  TEST_ASSERT_TRUE(hint_fits(c, "G0按住说一段 ;.,/移动 Enter打开"));
  TEST_ASSERT_TRUE(hint_fits(c, "Enter开session Opt+D忽略忽略 Tab菜单"));
  TEST_ASSERT_TRUE(hint_fits(c, ""));
  const char* over = "Enter开session Opt+D完成 `返回  Tab菜单";  // 242 px at the tightest spacing
  TEST_ASSERT_FALSE(hint_fits(c, over));
  hint_bar(c, over);
  TEST_ASSERT_EQUAL_UINT32(1, c.texts.size());
  TEST_ASSERT_FALSE(hint_fits(c, "A一 B二 C三 D四 E五 F六 G七 H八 I九"));  // more items than the bar takes
  TEST_ASSERT_FALSE(hint_fits(c, "按住说一段话然后松开结束录音再按回车键保存这一段录音到项目里面去"));
}

// A hint-style line anywhere (the confirm cards): the keys as caps on `under`, the words in
// ink, the text itself unchanged.
static void test_keys_text_draws_caps_in_a_line() {
  FakeCanvas c;
  const int16_t w = keys_text(c, 12, 84, "Enter 开始    ` 取消", Color::Ink, Color::Card);
  TEST_ASSERT_EQUAL_STRING("Enter 开始 ` 取消", c.line(84).c_str());
  TEST_ASSERT_TRUE(has_fill(c, Rect{12, 84, 34, kCapH}, Color::Cap));
  TEST_ASSERT_TRUE(c.find("开始")->style.fg == Color::Ink);
  TEST_ASSERT_TRUE(c.find("`")->style.fg == Color::Ink2);
  TEST_ASSERT_EQUAL_INT16(12 + 34 + 3 + 24 + 8 + 12 + 3 + 24, 12 + w);
}

static void test_toast_is_one_capsule() {
  FakeCanvas c;
  toast_bar(c, "已保存 · 待同步 1", Tone::Ok);
  TEST_ASSERT_TRUE(has_fill(c, Rect{2, 123, 236, 12}, Color::OkBg));
  TEST_ASSERT_TRUE(c.find("已保存")->style.fg == Color::Ok);
  TEST_ASSERT_EQUAL_INT16(8, c.find("已保存")->x);
}

static void test_sign_is_centred() {
  FakeCanvas c;
  const Rect r = sign(c, 38, 67, "打盹中", Tone::Neutral, Color::Sheet);
  TEST_ASSERT_TRUE(r.x == 38 - 23 && r.y == 67 && r.w == 46 && r.h == kSignH);
  TEST_ASSERT_TRUE(has_fill(c, r, Color::Raised));
  TEST_ASSERT_TRUE(c.find("打盹中")->style.fg == Color::Ink2);
  TEST_ASSERT_EQUAL_INT16(20, c.find("打盹中")->x);
  TEST_ASSERT_EQUAL_INT16(68, c.find("打盹中")->y);  // a pixel down in the 13 px capsule (mockup 06)
}

static void test_mando_and_his_bubble() {
  FakeCanvas c;
  const Rect box = mando(c, 7, 14, MandoSize::M46, MandoAct::NeedsYou, 0);
  TEST_ASSERT_EQUAL_UINT32(1, c.arts.size());
  TEST_ASSERT_TRUE(c.arts[0].x == 7 && c.arts[0].y == 14);
  TEST_ASSERT_TRUE(c.arts[0].id == mando_art(MandoSize::M46, MandoAct::NeedsYou, 0));
  TEST_ASSERT_TRUE(has_fill(c, Rect{31, 14, 9, 11}, Color::Warn));  // mockup 01: x + 24
  TEST_ASSERT_TRUE(has_fill(c, Rect{31, 26, 1, 1}, Color::Warn));   // the tail
  const TextOp* mark = c.find("!");
  TEST_ASSERT_TRUE(mark->x == 33 && mark->y == 13 && mark->style.fg == Color::OnAccent);
  const Rect want = mando_box(MandoSize::M46, 7, 14);
  TEST_ASSERT_TRUE(box.x == want.x && box.y == want.y && box.w == want.w && box.h == want.h);
  TEST_ASSERT_TRUE(box.x + box.w >= 40 && box.y + box.h >= 14 + 46 && box.y <= 13);
  c.clear();
  mando(c, 10, 19, MandoSize::M64, MandoAct::Listen, 1);
  TEST_ASSERT_TRUE(c.texts.empty() && c.fills.empty());  // no bubble, no sign
  c.clear();
  mando(c, 7, 14, MandoSize::M46, MandoAct::NoVoice, 0);
  TEST_ASSERT_TRUE(c.find("?") != nullptr && has_fill(c, Rect{31, 14, 9, 11}, Color::Ink2));
}

static void test_bubble_pops_up_in_two_steps() {
  FakeCanvas c;
  bubble(c, 31, 14, '!', Tone::Warn, Color::Bg, 0);
  TEST_ASSERT_TRUE(has_fill(c, Rect{31, 16, 9, 11}, Color::Warn));
  c.clear();
  bubble(c, 31, 14, '!', Tone::Warn, Color::Bg, 1);
  TEST_ASSERT_TRUE(has_fill(c, Rect{31, 14, 9, 11}, Color::Warn));
}

static void test_live_capsule_is_caramel_glass() {
  FakeCanvas c;
  TEST_ASSERT_EQUAL_INT16(46, live_capsule(c, 89, 95, "未归属"));
  TEST_ASSERT_TRUE(has_fill(c, Rect{89, 95, 46, 14}, Color::Live));
  TEST_ASSERT_TRUE(has_fill(c, Rect{90, 96, 44, 12}, Color::Select));
  const TextOp* t = c.find("未归属");
  TEST_ASSERT_TRUE(t->x == 94 && t->y == 96 && t->style.fg == Color::Accent);
}

static void test_stripe_is_the_tone_ink() {
  FakeCanvas c;
  stripe(c, 8, 35, 18, Tone::Warn, Color::Card);
  TEST_ASSERT_TRUE(has_fill(c, Rect{8, 35, 3, 18}, Color::Warn));
}

// docs/21 §6.9: a sign never leaves the screen. The meeting page centres it at x 23, where
// "保存中…" (52 px) and "Saving…" would start left of x 0; its capsule starts at x 2 or more
// and ends at x 238 or less, centred where it fits.
static void test_a_sign_stays_on_the_screen() {
  FakeCanvas c;
  for (const char* word : {"保存中…", "Saving…", "在听"}) {
    const Rect left = sign(c, 23, 40, word, Tone::Neutral);
    TEST_ASSERT_TRUE_MESSAGE(left.x >= 2, word);
    const Rect right = sign(c, 232, 40, word, Tone::Neutral);
    TEST_ASSERT_TRUE_MESSAGE(right.x + right.w <= kScreenW - 2, word);
  }
  const Rect mid = sign(c, 120, 40, "在听", Tone::Neutral);  // room on both sides: centred as before
  TEST_ASSERT_EQUAL_INT16(120 - mid.w / 2, mid.x);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cards_have_an_edge);
  RUN_TEST(test_keycaps_measure_their_key);
  RUN_TEST(test_pills_take_their_tone);
  RUN_TEST(test_tiles_selected_and_not);
  RUN_TEST(test_a_wide_label_is_whole_and_clear_of_both_ends);
  RUN_TEST(test_title_is_drawn_twice);
  RUN_TEST(test_hint_bar_keycaps_and_fallback);
  RUN_TEST(test_hint_one_character_caps_are_12_px);
  RUN_TEST(test_hint_fits_is_the_bars_rule);
  RUN_TEST(test_keys_text_draws_caps_in_a_line);
  RUN_TEST(test_toast_is_one_capsule);
  RUN_TEST(test_sign_is_centred);
  RUN_TEST(test_stripe_is_the_tone_ink);
  RUN_TEST(test_mando_and_his_bubble);
  RUN_TEST(test_bubble_pops_up_in_two_steps);
  RUN_TEST(test_live_capsule_is_caramel_glass);
  RUN_TEST(test_a_sign_stays_on_the_screen);
  return UNITY_END();
}
