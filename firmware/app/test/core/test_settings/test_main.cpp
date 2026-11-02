// Settings (docs/21 §6.8, ST-2), the pure part: the default, a new value per change, when a
// change is written to NVS, and (v0.5.2) the language a power-on stores when NVS has none.
// The switch itself (shell and runtime) is in test/ui/test_lang_switch.
#include <unity.h>

#include "settings.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

namespace {

// NVS's "cpa" namespace as far as the language goes, and one power-on in src/main.cpp's
// order: nvs_lang_migrate() stores lang_to_store(), nvs_boot_ticket() writes "boot", then
// the runtime loads the settings (a missing key gives Settings{}).
struct Nvs {
  Opt<uint8_t> lang;
  bool boot = false;
  int lang_writes = 0;
};

Lang power_on(Nvs& nvs) {
  const Opt<Lang> first = lang_to_store(nvs.lang.has, nvs.boot);
  if (first.has) {
    nvs.lang = some(lang_to_nvs(first.value));
    ++nvs.lang_writes;
  }
  nvs.boot = true;
  return nvs.lang.has ? lang_from_nvs(nvs.lang.value) : Settings{}.lang;
}

}  // namespace

static void test_default_is_english() {
  const Settings s;
  TEST_ASSERT_TRUE(s.lang == Lang::En);
  TEST_ASSERT_TRUE(lang() == Lang::En);  // before NVS is read (nothing here sets it)
}

static void test_with_lang_returns_a_new_value() {
  const Settings s;
  const Settings zh = with_lang(s, Lang::Zh);
  TEST_ASSERT_TRUE(zh.lang == Lang::Zh);
  TEST_ASSERT_TRUE(s.lang == Lang::En);  // the old value is untouched
  TEST_ASSERT_TRUE(zh != s);
  TEST_ASSERT_TRUE(with_lang(zh, Lang::En) == s);
}

static void test_written_only_when_changed() {
  const Settings en;
  const Settings zh = with_lang(en, Lang::Zh);
  TEST_ASSERT_TRUE(save_when(en, en, false) == SaveWhen::Never);
  TEST_ASSERT_TRUE(save_when(en, en, true) == SaveWhen::Never);
  TEST_ASSERT_TRUE(save_when(zh, en, false) == SaveWhen::Now);
}

static void test_never_written_while_recording() {
  const Settings en;
  const Settings zh = with_lang(en, Lang::Zh);
  TEST_ASSERT_TRUE(save_when(zh, en, true) == SaveWhen::AfterRec);
  TEST_ASSERT_TRUE(save_when(en, zh, true) == SaveWhen::AfterRec);
}

// v0.5.2: an erased NVS starts in English, and stores it, so its second power-on (with
// "boot" written by the first) is not taken for an older device's.
static void test_a_fresh_nvs_starts_in_english_and_stays_english() {
  Nvs nvs;
  TEST_ASSERT_TRUE(power_on(nvs) == Lang::En);
  TEST_ASSERT_TRUE(power_on(nvs) == Lang::En);
  TEST_ASSERT_TRUE(power_on(nvs) == Lang::En);
  TEST_ASSERT_EQUAL_INT(1, nvs.lang_writes);
  TEST_ASSERT_EQUAL_UINT8(1, nvs.lang.value);
}

// A device an earlier cpa-fw booted ("boot" there) whose user never switched showed Chinese,
// the default then: its first power-on on this firmware writes 0 (Chinese), once.
static void test_an_older_device_without_a_choice_keeps_chinese_written_once() {
  Nvs nvs;
  nvs.boot = true;
  TEST_ASSERT_TRUE(power_on(nvs) == Lang::Zh);
  TEST_ASSERT_EQUAL_INT(1, nvs.lang_writes);
  TEST_ASSERT_EQUAL_UINT8(0, nvs.lang.value);
  TEST_ASSERT_TRUE(power_on(nvs) == Lang::Zh);
  TEST_ASSERT_TRUE(power_on(nvs) == Lang::Zh);
  TEST_ASSERT_EQUAL_INT(1, nvs.lang_writes);
}

// The user's choice (or what an earlier migration wrote) is never touched, with or
// without "boot".
static void test_a_stored_language_is_untouched() {
  const uint8_t bytes[] = {0, 1};
  const bool boots[] = {false, true};
  for (const uint8_t b : bytes) {
    for (const bool boot : boots) {
      Nvs nvs;
      nvs.lang = some(b);
      nvs.boot = boot;
      TEST_ASSERT_TRUE(power_on(nvs) == lang_from_nvs(b));
      TEST_ASSERT_EQUAL_INT(0, nvs.lang_writes);
      TEST_ASSERT_EQUAL_UINT8(b, nvs.lang.value);
    }
  }
}

static void test_lang_to_store_table() {
  TEST_ASSERT_FALSE(lang_to_store(true, false).has);
  TEST_ASSERT_FALSE(lang_to_store(true, true).has);
  TEST_ASSERT_TRUE(lang_to_store(false, true) == some(Lang::Zh));
  TEST_ASSERT_TRUE(lang_to_store(false, false) == some(Lang::En));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_is_english);
  RUN_TEST(test_with_lang_returns_a_new_value);
  RUN_TEST(test_written_only_when_changed);
  RUN_TEST(test_never_written_while_recording);
  RUN_TEST(test_a_fresh_nvs_starts_in_english_and_stays_english);
  RUN_TEST(test_an_older_device_without_a_choice_keeps_chinese_written_once);
  RUN_TEST(test_a_stored_language_is_untouched);
  RUN_TEST(test_lang_to_store_table);
  return UNITY_END();
}
