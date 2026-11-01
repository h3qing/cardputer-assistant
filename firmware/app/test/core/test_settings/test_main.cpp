// Settings (docs/21 §6.8, ST-2), the pure part: the default, a new value per change, and
// when a change is written to NVS. The switch itself (shell and runtime) is in
// test/ui/test_lang_switch.
#include <unity.h>

#include "settings.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static void test_default_is_chinese() {
  const Settings s;
  TEST_ASSERT_TRUE(s.lang == Lang::Zh);
}

static void test_with_lang_returns_a_new_value() {
  const Settings s;
  const Settings en = with_lang(s, Lang::En);
  TEST_ASSERT_TRUE(en.lang == Lang::En);
  TEST_ASSERT_TRUE(s.lang == Lang::Zh);  // the old value is untouched
  TEST_ASSERT_TRUE(en != s);
  TEST_ASSERT_TRUE(with_lang(en, Lang::Zh) == s);
}

static void test_written_only_when_changed() {
  const Settings zh;
  const Settings en = with_lang(zh, Lang::En);
  TEST_ASSERT_TRUE(save_when(zh, zh, false) == SaveWhen::Never);
  TEST_ASSERT_TRUE(save_when(zh, zh, true) == SaveWhen::Never);
  TEST_ASSERT_TRUE(save_when(en, zh, false) == SaveWhen::Now);
}

static void test_never_written_while_recording() {
  const Settings zh;
  const Settings en = with_lang(zh, Lang::En);
  TEST_ASSERT_TRUE(save_when(en, zh, true) == SaveWhen::AfterRec);
  TEST_ASSERT_TRUE(save_when(zh, en, true) == SaveWhen::AfterRec);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_is_chinese);
  RUN_TEST(test_with_lang_returns_a_new_value);
  RUN_TEST(test_written_only_when_changed);
  RUN_TEST(test_never_written_while_recording);
  return UNITY_END();
}
