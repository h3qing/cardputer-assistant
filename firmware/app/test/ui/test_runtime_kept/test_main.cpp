// The kept copies end to end (docs/28 S1a, §9 "再交一次", "5 秒提示"): a Runtime with the
// audio task on the host, two Macs told apart by their hello's host_id.
#include <unity.h>

#include "runtime.h"
#include "support/kept_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() {}
void tearDown() {}

static void test_a_handed_over_memo_is_kept_and_not_listed_again() {
  RecRig x;
  hello(x, kMacA);
  x.memo(1500);
  TEST_ASSERT_EQUAL_UINT8(1, take_all(x));
  const RecInfo& r = x.r.rt.state().rec;
  TEST_ASSERT_EQUAL_UINT16(0, r.files_n);
  TEST_ASSERT_EQUAL_UINT16(1, r.kept.segs);
  TEST_ASSERT_EQUAL_UINT16(1, r.kept.batch_recs);
  TEST_ASSERT_TRUE(r.kept.batch_host != 0);
  TEST_ASSERT_EQUAL_UINT8(0, list(x).n);
}

// §9: delivered to A, "deliver again": A never lists it again, B does.
static void test_deliver_again_goes_to_the_other_mac_only() {
  RecRig x;
  hello(x, kMacA);
  x.memo(1500);
  take_all(x);
  TEST_ASSERT_TRUE(x.r.rt.kept_ask(KeptAsk::Again));
  x.steps();
  TEST_ASSERT_EQUAL_UINT16(0, x.r.rt.state().rec.kept.segs);
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.files_n);  // waiting again, on the device
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));                     // ... but not for A
  TEST_ASSERT_EQUAL_UINT8(0, list(x).n);
  x.r.unplug();
  hello(x, kMacA);  // A again, after a reconnect
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));
  x.r.unplug();
  hello(x, kMacB);
  x.r.step(6000);  // whatever the hint held
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
  TEST_ASSERT_EQUAL_UINT8(1, take_all(x));
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.kept.segs);
}

// §9 "5 秒提示": offline memos are held for 5 s, then listed, files.rev moving.
static void test_plug_in_hint_holds_offline_memos_five_seconds() {
  RecRig x;
  hello(x, kMacA);
  x.r.unplug();  // the offline mark
  x.memo(1500);
  x.memo(1500);
  hello(x, kMacB);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::Shown);
  TEST_ASSERT_EQUAL_UINT8(2, x.r.rt.state().rec.kept.plug_n);
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));
  TEST_ASSERT_EQUAL_UINT8(0, list(x).n);
  x.r.send("ping");
  x.r.step();
  const uint32_t rev = std::get<proto::Pong>(x.last("pong").msg.body).files.value.rev;
  x.r.step(5000);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::None);
  x.r.send("ping");
  x.r.step();
  const proto::FilesInfo f = std::get<proto::Pong>(x.last("pong").msg.body).files.value;
  TEST_ASSERT_EQUAL_UINT16(2, f.n);
  TEST_ASSERT_TRUE(f.rev != rev);
}

// Tab, Enter: never to this Mac (B), and A still gets them.
static void test_tab_enter_keeps_them_from_this_mac() {
  RecRig x;
  hello(x, kMacA);
  x.r.unplug();
  x.memo(1500);
  hello(x, kMacB);
  press(x, key::kTab);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::Asking);
  TEST_ASSERT_FALSE(x.r.rt.shell().menu_open());  // the Tab was the hint's
  press(x, key::kEnter);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::None);
  x.r.step(6000);
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));
  x.r.unplug();
  hello(x, kMacB);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::None);  // nothing for B: no hint
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));
  x.r.unplug();
  hello(x, kMacA);
  x.r.step(6000);
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
}

static void test_no_hint_without_offline_memos() {
  RecRig x;
  hello(x, kMacA);
  x.memo(1500);  // recorded online
  x.r.unplug();
  hello(x, kMacB);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::None);
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
}

static void test_clear_kept_copies() {
  RecRig x;
  hello(x, kMacA);
  x.memo(1500);
  take_all(x);
  TEST_ASSERT_TRUE(x.r.rt.kept_ask(KeptAsk::Clear));
  x.steps();
  TEST_ASSERT_EQUAL_UINT16(0, x.r.rt.state().rec.kept.segs);
  TEST_ASSERT_EQUAL_UINT(1, x.port.fs.file_count());  // the ledger's GEN alone
}

// Ten recordings: the runtime asks again until all are gone, and says so once.
static void test_clear_of_many_asks_again_until_done() {
  RecRig x;
  hello(x, kMacA);
  for (int i = 0; i < 10; ++i) x.memo(1500);
  while (take_all(x) > 0) {
  }
  TEST_ASSERT_EQUAL_UINT16(10, x.r.rt.state().rec.kept.segs);
  TEST_ASSERT_TRUE(x.r.rt.kept_ask(KeptAsk::Clear));
  x.steps(5);
  TEST_ASSERT_EQUAL_UINT16(0, x.r.rt.state().rec.kept.segs);
  TEST_ASSERT_EQUAL_UINT(2, x.port.count(rec::CmdKind::Ledger) - 0);
}

static void test_nothing_while_recording() {
  RecRig x;
  hello(x, kMacA);
  x.memo(1500);
  take_all(x);
  x.hold_g0(600);  // recording
  TEST_ASSERT_FALSE(x.r.rt.kept_ask(KeptAsk::Clear));
  x.let_go();
  x.steps();
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.kept.segs);
}

// An old cpad sends no host_id: nothing could be kept from it, so no hint.
static void test_no_hint_for_a_mac_without_a_host_id() {
  RecRig x;
  hello(x, kMacA);
  x.r.unplug();
  x.memo(1500);
  x.r.tx.is_plugged = true;
  x.r.handshake();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::None);
}

// Enter while another ledger command is with the audio task: still held, posted after it.
static void test_hold_waits_for_the_command_before_it() {
  RecRig x;
  hello(x, kMacA);
  x.r.unplug();
  x.memo(1500);
  hello(x, kMacB);
  x.port.hold_files = true;
  TEST_ASSERT_TRUE(x.r.rt.kept_ask(KeptAsk::Clear));  // busy until released
  press(x, key::kTab);
  press(x, key::kEnter);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::Busy);
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));
  x.port.hold_files = false;
  x.port.release_files();
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::None);
  x.r.step(6000);
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));  // kept from B
}

// ... and one never taken lets them go after kPlugAskMs and says so: no bar stays up.
static void test_a_hold_never_taken_gives_up() {
  RecRig x;
  hello(x, kMacA);
  x.r.unplug();
  x.memo(1500);
  hello(x, kMacB);
  x.port.hold_files = true;
  x.r.rt.kept_ask(KeptAsk::Clear);
  press(x, key::kTab);
  press(x, key::kEnter);
  x.r.step(kPlugAskMs + 100);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.kept.plug == PlugHint::None);
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_handed_over_memo_is_kept_and_not_listed_again);
  RUN_TEST(test_deliver_again_goes_to_the_other_mac_only);
  RUN_TEST(test_plug_in_hint_holds_offline_memos_five_seconds);
  RUN_TEST(test_tab_enter_keeps_them_from_this_mac);
  RUN_TEST(test_no_hint_without_offline_memos);
  RUN_TEST(test_clear_kept_copies);
  RUN_TEST(test_nothing_while_recording);
  RUN_TEST(test_clear_of_many_asks_again_until_done);
  RUN_TEST(test_no_hint_for_a_mac_without_a_host_id);
  RUN_TEST(test_hold_waits_for_the_command_before_it);
  RUN_TEST(test_a_hold_never_taken_gives_up);
  return UNITY_END();
}
