// docs/21 §6.6, §9.2: every hint bar and keys line, in both languages, is drawn as keycaps
// (never the one-line fallback) and fits the screen. English hints are ASCII like their
// keys, so they bracket them ("[Enter]open"); Txt's width check would count the brackets,
// so this test draws each one instead. A Chinese hint is cut as it always was.
#include <unity.h>

#include <string>

#include "board_texts.h"
#include "confirm.h"
#include "home_logic.h"
#include "menu_view.h"
#include "rec_overlay.h"
#include "shell_hints.h"
#include "support/fake_canvas.h"
#include "text_fit.h"
#include "theme.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() { set_lang(Lang::Zh); }  // a failed English test skips its LangScope

static std::string what(const char* hint) { return std::string(lang_code(lang())) + ": " + hint; }

// The bar: as keycaps at one of its spacings, its keys and words all drawn. A word ending
// in ASCII ends a pixel short of the right edge: efontCN_12's "u" or "w" lights its cell's
// last column, so flush right its stroke is on the bezel and reads as cut (a Han glyph's
// last column is blank). The simulator's test_ux_edges checks the real ink on every screen.
static std::string g_wide;  // every bar that does not fit, so one run names them all

static bool clear_of_the_edge(const char* hint) {
  FakeCanvas c;
  theme::hint_bar(c, hint);
  for (const TextOp& t : c.texts) {
    const bool ascii_end = !t.text.empty() && static_cast<unsigned char>(t.text.back()) < 0x80;
    if (t.x + c.textWidth(t.text.c_str()) > kScreenW - (ascii_end ? 1 : 0)) return false;
  }
  return true;
}

static void bar_fits(const char* hint) {
  FakeCanvas c;
  if (!theme::hint_fits(c, hint) || !clear_of_the_edge(hint)) g_wide += what(hint) + "\n";
  const theme::HintItems items = theme::hint_items(hint);
  TEST_ASSERT_FALSE_MESSAGE(items.overflow, what(hint).c_str());
  TEST_ASSERT_TRUE_MESSAGE(items.n > 0, what(hint).c_str());
}

// A keys line on a card: inside the card's width from x.
static void keys_fit(const char* keys, int16_t x, int16_t right) {
  FakeCanvas c;
  const int16_t w = theme::keys_text(c, x, 0, keys, Color::Ink, Color::Card);
  TEST_ASSERT_TRUE_MESSAGE(x + w <= right, what(keys).c_str());
  TEST_ASSERT_TRUE_MESSAGE(theme::hint_items(keys).n >= 1, what(keys).c_str());
}

static const Txt* const kBars[] = {&txt::kDiscardHint,     &txt::kEmptyHint,     &txt::kTextHint,
                                   &txt::kMemoLatchedHint, &txt::kMemoGraceHint, &txt::kMemoHint,
                                   &txt::kStartBoxHint,    &txt::kEndBoxHint,    &txt::kMeetingHint,
                                   &txt::kOverviewHint,    &txt::kQueueHint,     &kMenuHint};

static void every_bar_fits() {
  for (const Txt* t : kBars) bar_fits(tr(*t));
  for (const bool online : {false, true}) {
    for (const bool reaches : {false, true}) {
      for (const bool empty : {false, true}) bar_fits(reply_hint(online, reaches, empty));
    }
  }
  AppState s;  // home: each reason G0 cannot record, and none
  s.rec.avail = true;
  bar_fits(home_hint(s));
  TEST_ASSERT_TRUE(home_rec_block(s) == RecBlock::None);
  s.device.bat_mv = 3000;
  bar_fits(home_hint(s));
  s.rec.files_n = 200;
  bar_fits(home_hint(s));
  s.rec.free_known = true;
  bar_fits(home_hint(s));
  s.rec.sd_bad = true;
  bar_fits(home_hint(s));
}

static void test_every_hint_bar_fits_in_both_languages() {
  g_wide.clear();
  for (const Lang l : kLangs) {
    LangScope in(l);
    every_bar_fits();
  }
  TEST_ASSERT_TRUE_MESSAGE(g_wide.empty(), g_wide.c_str());
}

static void every_keys_line_fits() {
  keys_fit(tr(txt::kDiscardKeys), 12, 236);         // the editor's discard card
  keys_fit(tr(kMeetingConfirmKeys), 12, 236);       // the meeting's start box
  keys_fit(tr(kEndConfirmKeys), 52, 236);           // its end box
  keys_fit(tr(txt::kLetGoG0), 12, 236);             // a memo that ended under G0
  for (const Route r : {Route::Execute, Route::Queue, Route::Unavailable}) {
    for (const bool draft : {false, true}) keys_fit(confirm_keys(Decision{r, QueueReason::None}, draft), 4, 240);
  }
}

static void test_every_keys_line_fits_in_both_languages() {
  for (const Lang l : kLangs) {
    LangScope in(l);
    every_keys_line_fits();
  }
}

// The English bar keeps the Chinese one's keys, in its order: the same keycaps.
static void test_english_bars_have_the_chinese_keys() {
  for (const Txt* t : kBars) {
    const theme::HintItems zh = theme::hint_items(t->zh);
    const theme::HintItems en = theme::hint_items(t->en);
    std::string zk;
    std::string ek;
    for (uint8_t i = 0; i < zh.n; ++i) zk += std::string(t->zh + zh.items[i].key_at, zh.items[i].key_len) + "|";
    for (uint8_t i = 0; i < en.n; ++i) ek += std::string(t->en + en.items[i].key_at, en.items[i].key_len) + "|";
    if (zk.find("退格") != std::string::npos) continue;  // [Del] in English (key_label.cpp)
    if (zk.find("空格") != std::string::npos) continue;  // [Space]
    TEST_ASSERT_EQUAL_STRING_MESSAGE(zk.c_str(), ek.c_str(), t->en);
  }
}

// The review of docs/21's English renders: no keycap stands without a word in English (a
// bare [G0] reads as a stray key), and the hints say what happens in plain words.
static void test_english_hints_read_plainly() {
  LangScope en(Lang::En);
  for (const Txt* t : kBars) {
    const theme::HintItems h = theme::hint_items(t->en);
    for (uint8_t i = 0; i < h.n; ++i) TEST_ASSERT_TRUE_MESSAGE(h.items[i].text_len > 0, t->en);
  }
  TEST_ASSERT_EQUAL_STRING("[G0]/[Space]star [Opt+R]end [Tab]menu", tr(txt::kMeetingHint));
  TEST_ASSERT_EQUAL_STRING("[Del]exit  [Opt+N/Q/S]type  [Tab]menu", tr(txt::kEmptyHint));
  AppState s;
  s.rec.avail = true;
  TEST_ASSERT_EQUAL_STRING("[G0]hold, talk after REC [Enter]open", home_hint(s));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_hint_bar_fits_in_both_languages);
  RUN_TEST(test_every_keys_line_fits_in_both_languages);
  RUN_TEST(test_english_bars_have_the_chinese_keys);
  RUN_TEST(test_english_hints_read_plainly);
  return UNITY_END();
}
