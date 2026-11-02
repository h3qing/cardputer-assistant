// The plug-in hint and the ledger's answers in the loop (docs/28 S1a review): what the
// screen cannot show it does not decide (locked, the Tab menu), a Hold remembers its Mac
// and ids across an unplug, its toast counts what the hint counted, a give-up says why, a
// LedgerDone never heard frees the screen, "give to this Mac" undoes deliver again, and a
// card short of space is tidied while plugged in.
#include <unity.h>

#include "core/support/lang_pin.h"
#include "runtime.h"
#include "support/kept_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

namespace {

constexpr uint8_t kL = 38;  // layout index of 'l'

void lock(RecRig& x) { press(x, KeySet{}.with(key::kOpt).with(kL)); }
void unlock(RecRig& x) {
  lock(x);
  press(x, key::kEnter);
}

// Plugged into A once, then a memo offline: the next hello finds one offline recording.
void offline_memo(RecRig& x) {
  hello(x, kMacA);
  x.r.unplug();
  x.memo(1500);
}

proto::FilesInfo hello_files(RecRig& x) {
  return std::get<proto::Hello>(x.last("hello").msg.body).device.files.value;
}

// The memo kept from B by the hint (Tab, Enter): B's link stays up.
void held_from_b(RecRig& x) {
  offline_memo(x);
  hello(x, kMacB);
  press(x, key::kTab);
  press(x, key::kEnter);
  x.steps();
  wait_linked(x, 6000);
}

}  // namespace

// docs/07 §11.3 (HIL): A's hello right after B's, the link never down. The hello counts
// what A may take, and files.rev moves, so A lists (it waited 60 s for a rev change).
static void test_new_mac_on_a_live_link_sees_its_own_count() {
  RecRig x;
  held_from_b(x);
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));
  const uint32_t rev_b = std::get<proto::Pong>(x.last("pong").msg.body).files.value.rev;
  hello(x, kMacA);
  const proto::FilesInfo f = hello_files(x);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, f.n, "the hello counts for A, not for B");
  TEST_ASSERT_TRUE_MESSAGE(f.rev != rev_b, "files.rev moves when the Mac changes");
}

// ... and after an unplug: B's own hello already leaves out what was kept from it.
static void test_hello_counts_for_the_mac_that_sent_it() {
  RecRig x;
  held_from_b(x);
  x.r.unplug();
  hello(x, kMacB);
  TEST_ASSERT_EQUAL_UINT16(0, hello_files(x).n);
}

// Review 3: Tab, Enter, then the cable out before the audio task answers the Hold.
static void test_unplug_while_hold_in_flight() {
  RecRig x;
  offline_memo(x);
  hello(x, kMacB);
  x.port.hold_files = true;
  press(x, key::kTab);
  press(x, key::kEnter);
  x.r.unplug();
  x.port.hold_files = false;
  x.port.release_files();
  x.steps();
  hello(x, kMacB);
  wait_linked(x, 6000);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, files_n(x), "B sees the memo the user kept from it");
  x.r.unplug();
  hello(x, kMacA);
  wait_linked(x, 6000);
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
}

// ... and before it was even posted (another ledger command first): posted after, for B.
static void test_unplug_before_the_hold_is_posted() {
  RecRig x;
  offline_memo(x);
  hello(x, kMacB);
  x.port.hold_files = true;
  TEST_ASSERT_TRUE(x.r.rt.kept_ask(KeptAsk::Clear));
  press(x, key::kTab);
  press(x, key::kEnter);
  x.r.unplug();
  hello(x, kMacB);
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));  // held while the Hold is on its way
  x.port.hold_files = false;
  x.port.release_files();
  x.steps(6);
  wait_linked(x, 6000);
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));
}

// Review 1: plugged in locked (from a pocket): the hint waits until it can be seen.
static void test_hint_waits_while_locked() {
  RecRig x;
  offline_memo(x);
  lock(x);
  hello(x, kMacB);
  wait_linked(x, 30000);
  TEST_ASSERT_TRUE(kept(x).plug == PlugHint::Shown);
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));
  unlock(x);
  TEST_ASSERT_FALSE(x.r.rt.shell().locked());
  wait_linked(x, 4000);
  TEST_ASSERT_TRUE(kept(x).plug == PlugHint::Shown);
  press(x, key::kTab);
  press(x, key::kEnter);
  x.steps();
  wait_linked(x, 6000);
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));  // kept from B
}

static void test_locked_hint_lets_go_after_two_minutes() {
  RecRig x;
  offline_memo(x);
  lock(x);
  hello(x, kMacB);
  wait_linked(x, kPlugHiddenMaxMs + 100);
  TEST_ASSERT_TRUE(kept(x).plug == PlugHint::None);
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
}

// Review 2: under the Tab menu the bar is not drawn: Tab and Enter are the menu's.
static void test_menu_keys_are_the_menus() {
  RecRig x;
  offline_memo(x);
  press(x, key::kTab);
  TEST_ASSERT_TRUE(x.r.rt.shell().menu_open());
  hello(x, kMacB);
  wait_linked(x, 8000);
  TEST_ASSERT_TRUE(kept(x).plug == PlugHint::Shown);  // its 5 s stand still
  press(x, key::kTab);
  TEST_ASSERT_TRUE(kept(x).plug == PlugHint::Shown);
  TEST_ASSERT_FALSE(x.r.rt.shell().menu_open());
  wait_linked(x, 5100);
  TEST_ASSERT_TRUE(kept(x).plug == PlugHint::None);
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
}

// Review 5: one recording of three segments: the toast says 1, as the hint did.
static void test_hold_toast_counts_recordings() {
  RecRig x;
  hello(x, kMacA);
  x.r.unplug();
  x.port.engine->set_seg_ms(1024);
  x.memo(3000);
  hello(x, kMacB);
  TEST_ASSERT_EQUAL_UINT8(1, kept(x).plug_n);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.files_n >= 3);
  press(x, key::kTab);
  press(x, key::kEnter);
  x.steps();
  TEST_ASSERT_TRUE_MESSAGE(shows(x, "已不给这台 Mac：1 条"), screen(x).c_str());
}

// Review 6: a Hold that could not be posted is not an SD card error.
static void test_hold_give_up_is_not_an_sd_error() {
  RecRig x;
  offline_memo(x);
  hello(x, kMacB);
  x.port.hold_files = true;
  x.r.rt.kept_ask(KeptAsk::Clear);
  press(x, key::kTab);
  press(x, key::kEnter);
  wait_linked(x, kPlugAskMs + 100);
  TEST_ASSERT_TRUE_MESSAGE(shows(x, "没改成，已照常给这台 Mac"), screen(x).c_str());
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
}

// Review 7: a LedgerDone never heard: after kLedgerWaitMs the screen and the hint are free.
static void test_lost_ledger_done_frees_the_screen() {
  RecRig x;
  offline_memo(x);
  hello(x, kMacB);
  x.port.drop_ledger_done = 1;
  press(x, key::kTab);
  press(x, key::kEnter);
  x.steps();
  TEST_ASSERT_TRUE(kept(x).plug == PlugHint::Busy && kept(x).busy);
  wait_linked(x, 31000);
  TEST_ASSERT_TRUE(kept(x).plug == PlugHint::None);
  TEST_ASSERT_FALSE(kept(x).busy);
  TEST_ASSERT_TRUE(x.r.rt.kept_ask(KeptAsk::Clear));  // the ledger takes commands again
}

// Review 6: delivered to A, sent again while still on A: "give to this Mac" undoes it.
static void test_give_to_this_mac_undoes_deliver_again() {
  RecRig x;
  hello(x, kMacA);
  x.memo(1500);
  take_all(x);
  TEST_ASSERT_TRUE(x.r.rt.kept_ask(KeptAsk::Again));
  x.steps();
  TEST_ASSERT_EQUAL_UINT16(0, files_n(x));
  TEST_ASSERT_EQUAL_UINT16(1, kept(x).held_here);
  TEST_ASSERT_TRUE(x.r.rt.kept_ask(KeptAsk::Give));
  x.steps();
  TEST_ASSERT_EQUAL_UINT16(0, kept(x).held_here);
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
  TEST_ASSERT_TRUE_MESSAGE(shows(x, "已给这台 Mac：1 段"), screen(x).c_str());
  x.r.unplug();
  hello(x, kMacA);
  TEST_ASSERT_EQUAL_UINT16(1, files_n(x));
}

// Review 4: short of space and plugged in, what was kept goes before the next recording.
static void test_low_space_tidies_while_plugged() {
  RecRig x(true, true, 512ull << 20);
  hello(x, kMacA);
  x.memo(1500);
  take_all(x);
  x.steps(4);
  TEST_ASSERT_TRUE(x.r.tx.is_plugged);
  TEST_ASSERT_EQUAL_UINT16(0, kept(x).segs);
}

// ... and with room to spare, nothing goes while plugged in.
static void test_room_to_spare_keeps_while_plugged() {
  RecRig x;
  hello(x, kMacA);
  x.memo(1500);
  take_all(x);
  x.steps(4);
  TEST_ASSERT_EQUAL_UINT16(1, kept(x).segs);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_new_mac_on_a_live_link_sees_its_own_count);
  RUN_TEST(test_hello_counts_for_the_mac_that_sent_it);
  RUN_TEST(test_unplug_while_hold_in_flight);
  RUN_TEST(test_unplug_before_the_hold_is_posted);
  RUN_TEST(test_hint_waits_while_locked);
  RUN_TEST(test_locked_hint_lets_go_after_two_minutes);
  RUN_TEST(test_menu_keys_are_the_menus);
  RUN_TEST(test_hold_toast_counts_recordings);
  RUN_TEST(test_hold_give_up_is_not_an_sd_error);
  RUN_TEST(test_lost_ledger_done_frees_the_screen);
  RUN_TEST(test_give_to_this_mac_undoes_deliver_again);
  RUN_TEST(test_low_space_tidies_while_plugged);
  RUN_TEST(test_room_to_spare_keeps_while_plugged);
  return UNITY_END();
}
