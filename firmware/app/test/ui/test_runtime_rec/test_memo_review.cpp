// runtime_rec, gh #51 after its review (docs/20 U26): what the reviewers found through the
// real keys and the audio task. A hands-free memo started from the Tab menu's row (G0 up)
// that ends by itself is not held; tapping twice to end opens no note; a re-press read in
// the same loop pass as the grace's end goes on with the memo; a slow double tap and a slow
// tap-then-hold still record; locked, the end says on the card how the memo ended.
#include <unity.h>

#include "rec_core.h"
#include "rec_engine.h"
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;
using rec::CmdKind;

namespace {

const KeySet kG0 = KeySet{}.with(key::kG0);
const KeySet kTab = KeySet{}.with(key::kTab);
const KeySet kOptL = KeySet{}.with(key::kOpt).with(layout_index(10, 2));

void run(RecRig& x, uint32_t ms, KeySet keys = KeySet{}) {
  for (uint32_t t = 0; t < ms; t += 10) x.r.step(10, keys);
}

void tap(RecRig& x, KeySet keys) {
  run(x, 60, keys);
  run(x, 40);
}

const RecInfo& rec_of(RecRig& x) { return x.r.rt.state().rec; }
bool composing(RecRig& x) { return x.r.rt.shell().overlay() == proto::Overlay::Compose; }

}  // namespace

// Review finding 1: the menu's row starts a memo with G0 up. At the cap it ends by itself:
// saved, and nothing waits for a release that never comes (Tab works at once).
void test_a_menu_hands_free_memo_at_the_cap_is_not_held() {
  RecRig x;
  x.r.key(KeyEvent{KeyEventKind::G0Double, 0, 0, 0, Nav::None}, x.r.now);  // as the row replays it
  run(x, 20);
  TEST_ASSERT_TRUE(rec_of(x).latched);
  for (int i = 0; i < 31; ++i) {
    x.port.feed(10000);
    run(x, 10000);
  }
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle && !rec_of(x).held);
  TEST_ASSERT_TRUE(rec_of(x).last.saved && rec_of(x).last.why == static_cast<uint8_t>(rec::RecRefusal::Cap));
  tap(x, kTab);
  TEST_ASSERT_TRUE(x.r.rt.shell().menu_open());
}

// Review finding 2: tapping twice to end, as it started. The second tap, 100 ms after the
// end press, opens nothing, whether the save is done or not.
void test_a_double_tap_to_end_opens_no_note() {
  RecRig x;
  tap(x, kG0);
  run(x, 100);
  tap(x, kG0);  // hands free
  x.port.feed(3000);
  run(x, 3000);
  TEST_ASSERT_TRUE(rec_of(x).latched);
  tap(x, kG0);  // the end
  run(x, 100);
  tap(x, kG0);  // habit: a second tap
  run(x, 1000);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle && rec_of(x).last.saved);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  TEST_ASSERT_FALSE(composing(x));
}

// Review finding 3: a loop pass longer than the rest of the grace, G0 down again at its end.
// The keys are read before the grace is checked: the memo goes on, no Stop.
void test_a_long_pass_in_the_grace_keeps_the_memo() {
  RecRig x;
  x.hold_g0(600);
  x.port.feed(2000);
  run(x, 100);
  TEST_ASSERT_TRUE(rec_of(x).grace);
  x.r.step(520, kG0);
  run(x, 200, kG0);
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Stop));
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && !rec_of(x).grace);
}

// Review finding 5: a slow double tap (the second press 500 ms after the first release):
// the note opened at 350 ms closes, the hands-free memo starts on that press, and its tap
// submits nothing.
void test_a_slow_double_tap_records_hands_free() {
  RecRig x;
  tap(x, kG0);  // released at +60
  run(x, 460);
  TEST_ASSERT_TRUE(composing(x));
  x.r.step(10, kG0);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  run(x, 50, kG0);
  run(x, 30);
  x.port.feed(2000);
  run(x, 2000);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && rec_of(x).latched);
  TEST_ASSERT_FALSE(composing(x));
  TEST_ASSERT_TRUE(x.r.disp.canvas_.has("免提录音 · 按 G0 结束"));
}

// Review finding 6: tap, then a hold starting 1.2 s later (the note is open): the hold
// closes the empty note and records as a hold does; its release ends it after the grace.
void test_a_slow_tap_then_hold_records() {
  RecRig x;
  tap(x, kG0);
  run(x, 1200);
  TEST_ASSERT_TRUE(composing(x));
  run(x, 600, kG0);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  TEST_ASSERT_FALSE(composing(x));
  x.port.feed(1500);
  run(x, 1500, kG0);
  run(x, rec::kMemoGraceMs + 40);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle && rec_of(x).last.saved && !rec_of(x).latched);
  TEST_ASSERT_FALSE(composing(x));
}

// Review UX finding 3: ending a locked hands-free memo on the lit card. The card stays lit
// for 3 s from the end press and says how it ended (docs/20 B1: in the clock's place, Mando
// and "Saved"); still locked.
void test_a_locked_end_says_so_on_the_card() {
  RecRig x;
  tap(x, kG0);
  run(x, 100);
  tap(x, kG0);  // hands free
  x.port.feed(1500);
  tap(x, kOptL);
  run(x, 4000);  // dark
  tap(x, kG0);   // lights the card
  run(x, 200);
  x.port.feed(500);
  tap(x, kG0);  // the end
  run(x, 300);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle && x.r.rt.shell().locked());
  TEST_ASSERT_TRUE(x.r.rt.shell().lock_lit(x.r.now));
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("\xE2\x88\x9A 已存 0:0"), x.r.disp.canvas_.dump().c_str());
  TEST_ASSERT_TRUE(x.r.disp.canvas_.has("存好了"));
  run(x, 2500);
  TEST_ASSERT_TRUE(x.r.rt.shell().lock_lit(x.r.now));  // 3 s from the end press
}
