// docs/20 B6 (U9, U17, U22, U7): words and layout.
//   - a confirm box names Enter and ` with one verb each on its card and in its hint bar:
//     开始 / 存为排队 / 关闭, and ` 返回修改 when a draft is behind it (D6), else ` 取消;
//   - the editor wraps at spaces, so a word is not cut in two (a longer word still breaks);
//   - a Tab-menu row that only explains a key still looks like one when it is selected;
//   - every fixed toast and hint fits the theme's toast capsule (224 px), uncut.
#include <unity.h>

#include <string>

#include "home_logic.h"
#include "menu_model.h"
#include "menu_view.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "text_fit.h"
#include "theme.h"
#include "theme_palette.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

namespace {

constexpr int16_t kKeysY = kContentRect.y + 7 * 12;  // the confirm card's key row
constexpr int16_t kTextX = 10;                         // the editor's text, 6 px into its card
constexpr int16_t kTextY = 34;

struct Rig {
  FakePort port;
  ProbeModule board{"board"};
  Module* mods[1] = {&board};
  Shell shell{port, mods, 1};
  explicit Rig(bool online = true) {
    port.app = demo_state();
    if (online) set_ready(port.app);
    board.slot = 1;
    shell.begin(0);
  }
  void type(const std::string& text, uint32_t t) {
    for (char ch : text) shell.on_key(chr(ch), t);
  }
  FakeCanvas draw(uint32_t t) const {
    FakeCanvas c;
    shell.render(c, t);
    return c;
  }
};

// The card's key row and the hint bar, each as words.
struct KeyRows {
  std::string card;
  std::string bar;
};

KeyRows key_rows(const Rig& r, uint32_t t) {
  const FakeCanvas c = r.draw(t);
  return KeyRows{c.line(kKeysY), c.line(kHintRect.y)};
}

ConfirmSpec queue_item_spec() {
  ConfirmSpec q;
  q.kind = ConfirmKind::Spawn;
  q.slot = 1;
  q.qid = some(Qid::from("q17"));
  q.text = UserText::from("add LittleFS fault-injection test");
  return q;
}

void toast_fits(const char* s) {
  FakeCanvas c;
  TEST_ASSERT_TRUE_MESSAGE(c.textWidth(s) <= theme::kToastTextW, s);
  theme::toast_bar(c, s, theme::Tone::Info);
  TEST_ASSERT_TRUE_MESSAGE(c.has(s), s);
}

// The fill colour under (x, y), the last one drawn there.
Color ground(const FakeCanvas& c, int16_t x, int16_t y, Color none) {
  Color g = none;
  for (const auto& f : c.fills) {
    const Rect& r = f.first;
    if (r.x <= x && x < r.x + r.w && r.y <= y && y < r.y + r.h) g = f.second;
  }
  return g;
}

}  // namespace

// U22, D6: the card and the bar say the same; from the editor ` goes back to it.
static void test_a_new_session_box_says_one_verb() {
  Rig r;
  r.shell.on_key(opt('s'), 100);
  r.type("write tests", 100);
  r.shell.on_key(enter(), 100);
  TEST_ASSERT_TRUE(r.shell.overlay() == proto::Overlay::Confirm);
  KeyRows k = key_rows(r, 200);
  TEST_ASSERT_EQUAL_STRING("Enter 开始 ` 返回修改", k.card.c_str());
  TEST_ASSERT_EQUAL_STRING(k.card.c_str(), k.bar.c_str());
  Rig off(false);  // offline: Enter stores it as a queue item
  off.shell.on_key(opt('s'), 100);
  off.type("write tests", 100);
  off.shell.on_key(enter(), 100);
  k = key_rows(off, 200);
  TEST_ASSERT_EQUAL_STRING("Enter 存为排队 ` 返回修改", k.card.c_str());
  TEST_ASSERT_EQUAL_STRING(k.card.c_str(), k.bar.c_str());
}

// A queue item's box has no draft behind it: ` only closes it, and says 取消.
static void test_a_queue_item_box_says_cancel() {
  Rig r;
  r.shell.confirm(queue_item_spec());
  KeyRows k = key_rows(r, 100);
  TEST_ASSERT_EQUAL_STRING("Enter 开始 ` 取消", k.card.c_str());
  TEST_ASSERT_EQUAL_STRING(k.card.c_str(), k.bar.c_str());
  Rig off(false);  // offline the item stays on the Mac: Enter only closes the box
  off.shell.confirm(queue_item_spec());
  k = key_rows(off, 100);
  TEST_ASSERT_EQUAL_STRING("Enter 关闭 ` 取消", k.card.c_str());
  TEST_ASSERT_EQUAL_STRING(k.card.c_str(), k.bar.c_str());
}

// The keys are keycaps on the card, as on the meeting's boxes (docs/19 §6.7).
static void test_the_card_keys_are_keycaps() {
  Rig r;
  r.shell.confirm(queue_item_spec());
  const FakeCanvas c = r.draw(100);
  const TextOp* enter_key = nullptr;
  for (const TextOp& t : c.texts) enter_key = (t.y == kKeysY && t.text == "Enter") ? &t : enter_key;
  TEST_ASSERT_NOT_NULL(enter_key);
  TEST_ASSERT_TRUE(ground(c, static_cast<int16_t>(enter_key->x + 2), static_cast<int16_t>(kKeysY + 6), Color::Bg) ==
                   Color::Cap);
}

// U22: "Bingo shots o / n Friday" no more: the row ends at the last space that fits.
static void test_the_editor_wraps_at_spaces() {
  Rig r;
  r.shell.on_key(g0tap(), 100);
  r.type(std::string(33, 'a') + " Bingo shots", 100);
  FakeCanvas c = r.draw(100);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(std::string(33, 'a').c_str(), c.row(kTextY).c_str(), c.dump().c_str());
  TEST_ASSERT_EQUAL_STRING("Bingo shots_", c.row(kTextY + 12).c_str());
  for (const TextOp& t : c.texts) {
    if (t.y >= kTextY && t.y < kTextY + 5 * 12) TEST_ASSERT_EQUAL_INT16(kTextX, t.x);
  }
}

// A word longer than a row still breaks, and the cursor row stays in view.
static void test_a_long_word_breaks_and_the_last_rows_show() {
  Rig r;
  r.shell.on_key(g0tap(), 100);
  r.type(std::string(40, 'b'), 100);
  FakeCanvas c = r.draw(100);
  TEST_ASSERT_EQUAL_STRING(std::string(36, 'b').c_str(), c.row(kTextY).c_str());
  TEST_ASSERT_EQUAL_STRING("bbbb_", c.row(kTextY + 12).c_str());
  Rig many;
  many.shell.on_key(g0tap(), 100);
  for (int i = 0; i < 8; ++i) many.type("word" + std::to_string(i) + std::string(28, 'x') + " ", 100);
  many.type("end", 100);
  c = many.draw(100);
  TEST_ASSERT_EQUAL_STRING("end_", c.row(kTextY + 4 * 12).c_str());  // the fifth row is the last
  TEST_ASSERT_TRUE(c.row(kTextY).rfind("word4", 0) == 0);
}

// U17: a selected row that only explains (G0 held, a range) keeps its look: no caramel,
// its key on a Raised cap in Ink3, its words in Ink; an action row is caramel as before.
static void test_a_selected_explaining_row_still_explains() {
  static const HelpRow kRows[] = {
      {chr(' '), "空格", "加 ★"},
      {KeyEvent{}, "1-9", "归到项目"},
  };
  MenuSource src;
  src.layer = Layer::List;
  src.help = kRows;
  src.help_n = 2;
  const MenuContent m = menu_content(MenuPage::Here, src, AppState{}, SysInfo{});
  MenuState st;
  st.sel = 1;
  FakeCanvas c;
  menu_render(c, st, m);
  const TextOp* key = c.find("1-9");
  const TextOp* label = c.find("归到项目");
  TEST_ASSERT_TRUE(key != nullptr && label != nullptr);
  TEST_ASSERT_TRUE(key->style.fg == Color::Ink3);
  TEST_ASSERT_TRUE(ground(c, static_cast<int16_t>(key->x + 1), static_cast<int16_t>(key->y + 6), Color::Card) ==
                   Color::Raised);
  const Color row = ground(c, static_cast<int16_t>(label->x + 1), static_cast<int16_t>(label->y + 6), Color::Card);
  TEST_ASSERT_TRUE(row == Color::Select);
  TEST_ASSERT_TRUE(label->style.fg == Color::Ink);
  TEST_ASSERT_TRUE(theme::contrast(Color::Ink, Color::Select) >= 4.5);
  TEST_ASSERT_TRUE(theme::contrast(Color::Ink3, Color::Raised) >= 4.5);
  TEST_ASSERT_TRUE(theme::aa_exact(Color::Select, Color::Card));  // its rounded ends (tokens.py AA_PAIRS)
  st.sel = 0;
  c.clear();
  menu_render(c, st, m);
  const TextOp* act = c.find("加 ★");
  TEST_ASSERT_TRUE(act->style.fg == Color::OnAccent);
  TEST_ASSERT_TRUE(ground(c, static_cast<int16_t>(act->x + 1), static_cast<int16_t>(act->y + 6), Color::Card) ==
                   Color::Accent);
}

// docs/20 B6: the theme's toast capsule leaves its text 224 px; every fixed toast and hint
// since A and B (test_text_width has the older ones) is whole in it, gh #51's too.
static void test_every_new_fixed_toast_fits_the_capsule() {
  for (const char* s : {"太快了，再按一次 Enter", "先处理确认框", "开会中：Opt+R 结束", "开会中：G0 加★，Opt+R 结束",
                        tr(kHomeLetterHint), "离线，不能忽略", "离线，此操作不可用", "其他项目只在 Mac 上看",
                        "Mac 上未找到 Claude Code", "请稍后再试（已有请求在途）", "请稍后再试", "编辑器开着：退格 退出",
                        "编辑器开着：Enter 保存，` 退出", "可按 Opt+N 改存成笔记", "未执行（队列项仍在 Mac 上）",
                        "session 的项目不在设备上：去 Mac 回复", "存储已满，请连 Mac 同步",
                        "没有可用存储：请插入 FAT32 SD 卡", "SD 卡出错：插好后需重启",
                        "SD 卡出错：已暂存到内部，插好卡后重启", "保存失败（E_STORAGE）", "模块不可用",
                        "这一行是说明：直接按住 G0", "这一行是说明：直接按 Opt+1-9", "先按 Tab 关菜单，再按 0-9",
                        "已保存 · 待传 ↑999", "SD 卡剩余约 99 小时", "结果未知，请在看板确认（已存为排队）",
                        "按一下 G0 结束并保存"}) {  // the last: gh #51, merged into v0.4.1
    toast_fits(s);
  }
  toast_fits(selection_gone_text());
  toast_fits(stale_text(23 * 3600).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_new_session_box_says_one_verb);
  RUN_TEST(test_a_queue_item_box_says_cancel);
  RUN_TEST(test_the_card_keys_are_keycaps);
  RUN_TEST(test_the_editor_wraps_at_spaces);
  RUN_TEST(test_a_long_word_breaks_and_the_last_rows_show);
  RUN_TEST(test_a_selected_explaining_row_still_explains);
  RUN_TEST(test_every_new_fixed_toast_fits_the_capsule);
  return UNITY_END();
}
