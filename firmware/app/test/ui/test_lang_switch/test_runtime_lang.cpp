// The runtime's part of a language switch (docs/21 §6.8, §7, ST-2): the stored language is
// shown from the first frame, a switch is written once (only when it changed, never while
// recording, and not retried every pass when the write failed), and debug.lang (test
// firmware) takes the settings page's path, so HIL can set the language and put it back.
#include <unity.h>

#include <string>

#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"
#include "support/runtime_rig.h"

using namespace cpa;
using namespace uitest;

namespace {

proto::DebugState debug_state(RuntimeRig& r) {
  r.send("debug.state");
  r.step(10);
  return std::get<proto::DebugState>(r.tx.of_type("debug.state").back().msg.body);
}

}  // namespace

void test_boot_shows_the_stored_language() {
  RuntimeRig r(false);
  r.plat.nvs = with_lang(Settings{}, Lang::En);
  r.begin();
  TEST_ASSERT_TRUE(lang() == Lang::En);
  TEST_ASSERT_EQUAL_UINT(0, r.plat.saves.size());  // reading never writes
}

void test_boot_without_a_stored_language_is_chinese() {
  set_lang(Lang::En);  // what an earlier boot of this process left
  RuntimeRig r;
  TEST_ASSERT_TRUE(lang() == Lang::Zh);
}

void test_a_switch_is_written_once() {
  RuntimeRig r;
  TEST_ASSERT_TRUE(r.rt.shell().setLanguage(Lang::En));
  for (int i = 0; i < 5; ++i) r.step();
  TEST_ASSERT_EQUAL_UINT(1, r.plat.saves.size());
  TEST_ASSERT_TRUE(r.plat.nvs.lang == Lang::En);
}

void test_switching_back_writes_again() {
  RuntimeRig r;
  r.rt.shell().setLanguage(Lang::En);
  r.step();
  r.rt.shell().setLanguage(Lang::Zh);
  r.step();
  TEST_ASSERT_EQUAL_UINT(2, r.plat.saves.size());
  TEST_ASSERT_TRUE(r.plat.nvs.lang == Lang::Zh);
}

void test_a_failed_write_is_not_retried_every_pass() {
  RuntimeRig r;
  r.plat.save_ok = false;
  r.rt.shell().setLanguage(Lang::En);
  for (int i = 0; i < 20; ++i) r.step();
  TEST_ASSERT_EQUAL_UINT(1, r.plat.saves.size());
  TEST_ASSERT_TRUE(lang() == Lang::En);         // shown for this boot
  TEST_ASSERT_TRUE(r.plat.nvs.lang == Lang::Zh);  // what the next boot shows
}

// docs/21 §6.8 step 5: a flash write stalls the cache, so none during a recording; the
// language still changes at once, and the write follows the recording's end.
void test_while_recording_the_write_waits_for_the_end() {
  set_lang(Lang::Zh);
  RecRig x;
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  TEST_ASSERT_TRUE(x.r.rt.shell().setLanguage(Lang::En));
  TEST_ASSERT_TRUE(lang() == Lang::En);
  x.r.step(10, KeySet{}.with(key::kG0));
  TEST_ASSERT_EQUAL_UINT(0, x.r.plat.saves.size());
  x.port.feed(2000);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.files_n);  // the memo is saved as usual
  TEST_ASSERT_EQUAL_UINT(1, x.r.plat.saves.size());
  TEST_ASSERT_TRUE(x.r.plat.nvs.lang == Lang::En);
}

// Switched there and back during one recording: the stored language never changed, so
// nothing is written when it ends.
void test_there_and_back_while_recording_writes_nothing() {
  set_lang(Lang::Zh);
  RecRig x;
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  TEST_ASSERT_TRUE(x.r.rt.shell().setLanguage(Lang::En));
  x.r.step(10, KeySet{}.with(key::kG0));
  TEST_ASSERT_TRUE(x.r.rt.shell().setLanguage(Lang::Zh));
  x.r.step(10, KeySet{}.with(key::kG0));
  x.port.feed(2000);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  for (int i = 0; i < 20; ++i) x.r.step(10);
  TEST_ASSERT_EQUAL_UINT(0, x.r.plat.saves.size());
  TEST_ASSERT_TRUE(lang() == Lang::Zh);
}

// The write that waited for a recording fails: one warning says what a restart shows, and
// it does not come back (the write is not retried).
void test_a_failed_write_after_a_recording_warns_once() {
  set_lang(Lang::Zh);
  RecRig x;
  x.r.plat.save_ok = false;
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.shell().setLanguage(Lang::En));
  x.r.step(10, KeySet{}.with(key::kG0));
  x.port.feed(2000);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  int shown = 0;
  bool was = false;
  for (uint32_t t = 0; t < 4 * kToastMs; t += 10) {
    x.r.step(10);
    const bool on = x.r.disp.canvas_.has("Not saved: 中文 after a restart");
    shown += on && !was ? 1 : 0;
    was = on;
  }
  TEST_ASSERT_EQUAL_INT(1, shown);
  TEST_ASSERT_EQUAL_UINT(1, x.r.plat.saves.size());
  TEST_ASSERT_TRUE(x.r.plat.nvs.lang == Lang::Zh);
}

void test_debug_lang_switches_through_the_same_path() {
  RuntimeRig r;
  r.handshake();
  const std::string id = "h" + std::to_string(r.host_id);
  r.send("debug.lang", "\"lang\":\"en\"");
  r.step();
  TEST_ASSERT_TRUE(lang() == Lang::En);
  TEST_ASSERT_TRUE(r.tx.of_type("ack").back().msg.re.value.equals(id.c_str()));
  TEST_ASSERT_EQUAL_UINT(1, r.plat.saves.size());
  r.step(cpa::kRenderMinMs);
  TEST_ASSERT_TRUE(r.disp.canvas_.has("English · Enter again: 中文"));
  r.send("debug.lang", "\"lang\":\"en\"");  // already shown: acked, nothing written
  r.step();
  TEST_ASSERT_EQUAL_UINT(1, r.plat.saves.size());
  TEST_ASSERT_EQUAL_UINT(0, r.tx.count("error"));
}

void test_debug_lang_before_the_handshake_is_not_ready() {
  RuntimeRig r;
  r.send("debug.lang", "\"lang\":\"en\"");
  r.step();
  TEST_ASSERT_TRUE(lang() == Lang::Zh);
  const auto errs = r.tx.of_type("error");
  TEST_ASSERT_EQUAL_UINT(1, errs.size());
  TEST_ASSERT_TRUE(std::get<proto::ErrorMsg>(errs.back().msg.body).code == ErrCode::NotReady);
}

void test_debug_state_reports_the_language() {
  RuntimeRig r;
  r.handshake();
  proto::DebugState s = debug_state(r);
  TEST_ASSERT_TRUE(s.ui.has && s.ui.value.lang.has && s.ui.value.lang.value == Lang::Zh);
  r.rt.shell().setLanguage(Lang::En);
  s = debug_state(r);
  TEST_ASSERT_TRUE(s.ui.value.lang.value == Lang::En);
}
