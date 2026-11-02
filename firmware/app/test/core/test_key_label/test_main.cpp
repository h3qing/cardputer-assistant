// Key names (docs/13 §6.3): every trigger kind has a label, written as the hint bar
// writes keys; digit runs read "Opt+1-9" / "0-9". Every label of a kKeymap binding
// fits the menu's key column (54 px less a 6 px gap).
#include <unity.h>

#include <string>

#include "core/support/lang_pin.h"
#include "key_label.h"
#include "keymap.h"
#include "module.h"  // lib/help is built whole: its menu files need module.h's libraries
#include "ui/support/fake_canvas.h"

using namespace cpa;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

static KeyEvent chr(char c) { return KeyEvent{KeyEventKind::Char, c, 0, 0, Nav::None}; }
static KeyEvent chord(uint8_t mods, char k) { return KeyEvent{KeyEventKind::Chord, 0, mods, k, Nav::None}; }
static KeyEvent nav(Nav n) { return KeyEvent{KeyEventKind::Nav, 0, 0, 0, n}; }
static KeyEvent kind(KeyEventKind k) { return KeyEvent{k, 0, 0, 0, Nav::None}; }

static void test_none_is_empty() {
  TEST_ASSERT_EQUAL_STRING("", key_label(KeyEvent{}).c_str());
  TEST_ASSERT_EQUAL_STRING("", key_label(nav(Nav::None)).c_str());
}

static void test_g0() {
  TEST_ASSERT_EQUAL_STRING("G0", key_label(kind(KeyEventKind::G0Tap)).c_str());
  TEST_ASSERT_EQUAL_STRING("按住G0", key_label(kind(KeyEventKind::G0Long)).c_str());
  TEST_ASSERT_EQUAL_STRING("松开G0", key_label(kind(KeyEventKind::G0LongUp)).c_str());
  // gh #51: the user's own word (双击, double tap), and as wide as 按住G0 (the menu's key column)
  TEST_ASSERT_EQUAL_STRING("双击G0", key_label(kind(KeyEventKind::G0Double)).c_str());
}

static void test_nav_keys() {
  TEST_ASSERT_EQUAL_STRING("Tab", key_label(nav(Nav::Menu)).c_str());
  TEST_ASSERT_EQUAL_STRING("Enter", key_label(nav(Nav::Enter)).c_str());
  TEST_ASSERT_EQUAL_STRING("Fn+`", key_label(nav(Nav::Back)).c_str());
  TEST_ASSERT_EQUAL_STRING("退格", key_label(nav(Nav::Bksp)).c_str());
  TEST_ASSERT_EQUAL_STRING("Fn+;", key_label(nav(Nav::Up)).c_str());
  TEST_ASSERT_EQUAL_STRING("Fn+.", key_label(nav(Nav::Down)).c_str());
  TEST_ASSERT_EQUAL_STRING("Fn+,", key_label(nav(Nav::Left)).c_str());
  TEST_ASSERT_EQUAL_STRING("Fn+/", key_label(nav(Nav::Right)).c_str());
}

static void test_chars() {
  TEST_ASSERT_EQUAL_STRING("空格", key_label(chr(' ')).c_str());
  TEST_ASSERT_EQUAL_STRING("`", key_label(chr('`')).c_str());
  TEST_ASSERT_EQUAL_STRING(";", key_label(chr(';')).c_str());
  TEST_ASSERT_EQUAL_STRING("7", key_label(chr('7')).c_str());
  TEST_ASSERT_EQUAL_STRING("a", key_label(chr('a')).c_str());
}

static void test_chords() {
  TEST_ASSERT_EQUAL_STRING("Opt+H", key_label(chord(kModOpt, 'h')).c_str());
  TEST_ASSERT_EQUAL_STRING("Opt+1", key_label(chord(kModOpt, '1')).c_str());
  TEST_ASSERT_EQUAL_STRING("Ctrl+C", key_label(chord(kModCtrl, 'c')).c_str());
  TEST_ASSERT_EQUAL_STRING("Alt+;", key_label(chord(kModAlt, ';')).c_str());
  TEST_ASSERT_EQUAL_STRING("Opt+Enter", key_label(chord(kModOpt, '\n')).c_str());
  TEST_ASSERT_EQUAL_STRING("Opt+退格", key_label(chord(kModOpt, '\b')).c_str());
  TEST_ASSERT_EQUAL_STRING("Opt+空格", key_label(chord(kModOpt, ' ')).c_str());
  TEST_ASSERT_EQUAL_STRING("Fn+Shift+Ctrl+Opt+Alt+X",
                           key_label(chord(kModFn | kModShift | kModCtrl | kModOpt | kModAlt, 'x')).c_str());
}

static void test_ranges() {
  TEST_ASSERT_EQUAL_STRING("Opt+1-9", key_range_label(chord(kModOpt, '1'), chord(kModOpt, '9')).c_str());
  TEST_ASSERT_EQUAL_STRING("Opt+0-9", key_range_label(chord(kModOpt, '0'), chord(kModOpt, '9')).c_str());
  TEST_ASSERT_EQUAL_STRING("0-9", key_range_label(chr('0'), chr('9')).c_str());
  TEST_ASSERT_EQUAL_STRING("1-9", key_range_label(chr('1'), chr('9')).c_str());
}

// docs/21 §6.5: the keys in words, in English; Del is what the keycap says.
static void test_key_words_in_english() {
  LangScope en(Lang::En);
  TEST_ASSERT_EQUAL_STRING("Hold G0", key_label(kind(KeyEventKind::G0Long)).c_str());
  TEST_ASSERT_EQUAL_STRING("G0 up", key_label(kind(KeyEventKind::G0LongUp)).c_str());
  TEST_ASSERT_EQUAL_STRING("G0 x2", key_label(kind(KeyEventKind::G0Double)).c_str());
  TEST_ASSERT_EQUAL_STRING("Del", key_label(nav(Nav::Bksp)).c_str());
  TEST_ASSERT_EQUAL_STRING("Space", key_label(chr(' ')).c_str());
  TEST_ASSERT_EQUAL_STRING("Opt+Del", key_label(chord(kModOpt, '\b')).c_str());
}

static void test_every_binding_has_a_label_that_fits_the_key_column() {
  const uitest::FakeCanvas c;
  for (const Lang lg : kLangs) {
    LangScope in(lg);
    for (size_t i = 0; i < kKeymapCount; ++i) {
      const KeyLabel l = key_label(kKeymap[i].trigger);
      const std::string what = "binding " + std::to_string(i) + " " + l.c_str();
      TEST_ASSERT_FALSE_MESSAGE(l.empty(), what.c_str());
      TEST_ASSERT_TRUE_MESSAGE(c.textWidth(l.c_str()) <= 48, what.c_str());
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_none_is_empty);
  RUN_TEST(test_g0);
  RUN_TEST(test_nav_keys);
  RUN_TEST(test_chars);
  RUN_TEST(test_chords);
  RUN_TEST(test_ranges);
  RUN_TEST(test_key_words_in_english);
  RUN_TEST(test_every_binding_has_a_label_that_fits_the_key_column);
  return UNITY_END();
}
