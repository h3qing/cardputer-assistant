// Switching the UI language (docs/21 §4.3, §6.6-§6.8, ST-1 / ST-2), the shell's part: a
// module's structured hint bar and Tab-menu words in the shown language, and
// ModuleContext::setLanguage(): switched at once, redrawn, a toast in both scripts saying
// how to switch back, the store asked to keep it, nothing queued thrown away.
// The runtime's part (NVS at boot, the write after a recording, debug.lang) is in
// test_runtime_lang.cpp.
#include <unity.h>

#include <string>

#include "lang_switch.h"
#include "shell.h"
#include "support/fake_canvas.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "txt.h"

using namespace cpa;
using namespace uitest;

void setUp() { set_lang(Lang::Zh); }
void tearDown() { set_lang(Lang::Zh); }

namespace {

constexpr HintKey kKeys[] = {{";.", {"选", "pick"}}, {"Enter", {"改", "change"}}, {"`", {"返回", "back"}}};
constexpr Txt kRowWords{"切换语言", "Switch language", kMenuTxtW};

struct Rig {
  FakePort port;
  ProbeModule home{"home"};
  ProbeModule system{"system"};
  cpa::Module* mods[2] = {&home, &system};
  Shell shell{port, mods, 2};
  Rig() {
    port.app = demo_state();
    system.hint_keys = kKeys;
    system.hint_key_n = 3;
    shell.begin(0);
  }
  std::string hint_row(uint32_t now = 10) {
    FakeCanvas c;
    shell.render(c, now);
    return c.row(kHintRect.y);
  }
};

}  // namespace

static void test_structured_hint_bar_in_chinese() {
  Rig r;
  r.shell.on_key(opt('m'), 10);
  TEST_ASSERT_EQUAL_STRING("system", r.shell.top_id());
  TEST_ASSERT_EQUAL_STRING(";.|选|Enter|改|`|返回|Tab|菜单", r.hint_row().c_str());
}

static void test_structured_hint_bar_in_english_keeps_words_apart() {
  Rig r;
  r.shell.on_key(opt('m'), 10);
  set_lang(Lang::En);
  // parsed as a string, "Enter change" would be one keycap
  TEST_ASSERT_EQUAL_STRING(";.|pick|Enter|change|`|back|Tab|menu", r.hint_row().c_str());
}

static void test_a_module_without_hint_keys_keeps_its_hint() {
  Rig r;
  TEST_ASSERT_EQUAL_STRING("home", r.shell.top_id());
  TEST_ASSERT_EQUAL_STRING("probe hint", r.hint_row().c_str());
}

static void test_help_rows_take_their_txt() {
  HelpRow plain{KeyEvent{}, "Enter", "旧的说明"};
  TEST_ASSERT_EQUAL_STRING("旧的说明", help_label(plain));
  HelpRow both{KeyEvent{}, "Enter", "旧的说明", &kRowWords};
  TEST_ASSERT_EQUAL_STRING("切换语言", help_label(both));
  set_lang(Lang::En);
  TEST_ASSERT_EQUAL_STRING("Switch language", help_label(both));
  TEST_ASSERT_EQUAL_STRING("旧的说明", help_label(plain));  // an app not yet translated
}

static void test_switch_to_english_says_how_back() {
  Rig r;
  TEST_ASSERT_TRUE(r.system.ctx->setLanguage(Lang::En));
  TEST_ASSERT_TRUE(lang() == Lang::En);
  TEST_ASSERT_EQUAL_UINT(1, r.port.stored.size());
  TEST_ASSERT_TRUE(r.port.stored[0] == Lang::En);
  TEST_ASSERT_TRUE(r.shell.dirty());
  TEST_ASSERT_EQUAL_STRING("English · Enter again: 中文", r.hint_row().c_str());
}

static void test_switch_back_to_chinese_says_how_back() {
  Rig r;
  r.system.ctx->setLanguage(Lang::En);
  r.shell.tick(10 + kToastMs + 1);
  TEST_ASSERT_TRUE(r.system.ctx->setLanguage(Lang::Zh));
  TEST_ASSERT_TRUE(lang() == Lang::Zh);
  TEST_ASSERT_EQUAL_STRING("中文 · 再按 Enter 换回 English", r.hint_row(10 + kToastMs + 2).c_str());
}

static void test_the_shown_language_is_no_switch() {
  Rig r;
  r.shell.clear_dirty();
  TEST_ASSERT_FALSE(r.system.ctx->setLanguage(Lang::Zh));
  TEST_ASSERT_EQUAL_UINT(0, r.port.stored.size());
  TEST_ASSERT_FALSE(r.shell.dirty());
  TEST_ASSERT_EQUAL_STRING("probe hint", r.hint_row().c_str());
}

// docs/21 §6.8 step 2: nothing is cleared. A result queued before the switch is still shown
// first (in the words it was queued with), the switch's toast after it.
static void test_a_switch_keeps_what_is_queued() {
  Rig r;
  r.system.ctx->toast(ToastLevel::Ok, "已保存");
  r.system.ctx->setLanguage(Lang::En);
  TEST_ASSERT_EQUAL_STRING("已保存", r.hint_row().c_str());
  r.shell.tick(10 + kToastMs + 1);
  TEST_ASSERT_EQUAL_STRING("English · Enter again: 中文", r.hint_row(10 + kToastMs + 2).c_str());
}

static void test_a_failed_write_says_what_a_restart_shows() {
  Rig r;
  r.port.next_store = SettingsSave::Failed;
  r.system.ctx->setLanguage(Lang::En);
  TEST_ASSERT_TRUE(lang() == Lang::En);  // this boot still switches
  r.shell.tick(10 + kToastMs + 1);
  TEST_ASSERT_EQUAL_STRING("Not saved: 中文 after a restart", r.hint_row(10 + kToastMs + 2).c_str());
}

static void test_a_pending_write_says_nothing_more() {
  Rig r;
  r.port.next_store = SettingsSave::Pending;  // recording: written once it ends
  r.system.ctx->setLanguage(Lang::En);
  r.shell.tick(10 + kToastMs + 1);
  TEST_ASSERT_EQUAL_STRING("probe hint", r.hint_row(10 + kToastMs + 2).c_str());
}

// The toasts are format strings, so their width is measured here with every argument, in
// efontCN_12's real widths (docs/21 §6.3 rule 5).
static void test_switch_toasts_fit_the_toast_bar() {
  for (Lang from : kLangs) {
    const Lang to = lang_other(from);
    LangScope s(to);
    const ToastText sw = lang_switched_text(from, to);
    TEST_ASSERT_TRUE_MESSAGE(i18n::text_width(sw.c_str()) <= kToastTxtW, sw.c_str());
    for (Lang stored : kLangs) {
      const ToastText un = lang_unsaved_text(stored);
      TEST_ASSERT_TRUE_MESSAGE(i18n::text_width(un.c_str()) <= kToastTxtW, un.c_str());
      TEST_ASSERT_FALSE(i18n::has_missing_glyph(un.c_str()));
    }
  }
  LangScope zh(Lang::Zh);
  TEST_ASSERT_EQUAL_STRING("没存上：重启后会回到English", lang_unsaved_text(Lang::En).c_str());
}

// test_runtime_lang.cpp
void test_boot_shows_the_stored_language();
void test_boot_without_a_stored_language_is_chinese();
void test_a_switch_is_written_once();
void test_switching_back_writes_again();
void test_a_failed_write_is_not_retried_every_pass();
void test_while_recording_the_write_waits_for_the_end();
void test_there_and_back_while_recording_writes_nothing();
void test_a_failed_write_after_a_recording_warns_once();
void test_debug_lang_switches_through_the_same_path();
void test_debug_lang_before_the_handshake_is_not_ready();
void test_debug_state_reports_the_language();

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_structured_hint_bar_in_chinese);
  RUN_TEST(test_structured_hint_bar_in_english_keeps_words_apart);
  RUN_TEST(test_a_module_without_hint_keys_keeps_its_hint);
  RUN_TEST(test_help_rows_take_their_txt);
  RUN_TEST(test_switch_to_english_says_how_back);
  RUN_TEST(test_switch_back_to_chinese_says_how_back);
  RUN_TEST(test_the_shown_language_is_no_switch);
  RUN_TEST(test_a_switch_keeps_what_is_queued);
  RUN_TEST(test_a_failed_write_says_what_a_restart_shows);
  RUN_TEST(test_a_pending_write_says_nothing_more);
  RUN_TEST(test_switch_toasts_fit_the_toast_bar);
  RUN_TEST(test_boot_shows_the_stored_language);
  RUN_TEST(test_boot_without_a_stored_language_is_chinese);
  RUN_TEST(test_a_switch_is_written_once);
  RUN_TEST(test_switching_back_writes_again);
  RUN_TEST(test_a_failed_write_is_not_retried_every_pass);
  RUN_TEST(test_while_recording_the_write_waits_for_the_end);
  RUN_TEST(test_there_and_back_while_recording_writes_nothing);
  RUN_TEST(test_a_failed_write_after_a_recording_warns_once);
  RUN_TEST(test_debug_lang_switches_through_the_same_path);
  RUN_TEST(test_debug_lang_before_the_handshake_is_not_ready);
  RUN_TEST(test_debug_state_reports_the_language);
  return UNITY_END();
}
