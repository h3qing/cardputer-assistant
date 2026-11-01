// runtime_rec, gh #51 (docs/20 U26): a memo through the real keys and the audio task. G0
// slipping for less than kMemoGraceMs is one memo, one file; a grace that runs out ends
// it. G0 twice (g0_gesture.h) starts a hands-free memo on the second press: letting go ends
// nothing, one G0 press ends it (and nothing opens after it); a single tap still opens the
// note once the window ran out; Opt+L locks a hands-free memo and it goes on, and locked
// a G0 press ends it only on the lit card. The cap still ends it.
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
const KeySet kSpace = KeySet{}.with(layout_index(13, 3));
const KeySet kOptL = KeySet{}.with(key::kOpt).with(layout_index(10, 2));

KeySet plus(KeySet a, KeySet b) {
  KeySet out = a;
  for (uint8_t k = 0; k <= key::kG0; ++k) out = b.has(k) ? out.with(k) : out;
  return out;
}

void run(RecRig& x, uint32_t ms, KeySet keys = KeySet{}) {
  for (uint32_t t = 0; t < ms; t += 10) x.r.step(10, keys);
}

void tap(RecRig& x, KeySet keys) {
  run(x, 60, keys);
  run(x, 40);
}

const RecInfo& rec_of(RecRig& x) { return x.r.rt.state().rec; }

}  // namespace

// The user's case: G0 slips for 0.3 s mid-sentence and is pressed again. One memo, one
// Start, one Stop, one file; the page said so in between.
void test_a_slip_within_the_grace_is_one_memo() {
  RecRig x;
  x.hold_g0(600);
  x.port.feed(2000);
  run(x, 300);  // let go
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && rec_of(x).grace && !rec_of(x).latched);
  TEST_ASSERT_TRUE(x.r.disp.canvas_.has("松开了 · 0.6 秒内按回继续"));
  run(x, 200, kG0);  // pressed again
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && !rec_of(x).grace);
  x.port.feed(1500);
  run(x, rec::kMemoGraceMs - 10);  // let go for good
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && rec_of(x).grace);
  run(x, 30);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Stop));
  TEST_ASSERT_EQUAL_UINT16(1, rec_of(x).files_n);
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_TRUE(rec_of(x).last.has && rec_of(x).last.saved);
}

// B5 with the grace: G0 let go in the cue and not pressed again: one Stop when the grace
// runs out, discarded, nothing written. The late path (G0Long and G0LongUp in one sample,
// a loop pass longer than 500 ms) starts the memo and lets go of it in the same sample.
void test_a_release_in_the_cue_and_the_late_path() {
  RecRig x;
  x.port.hold_start = true;
  x.hold_g0(520);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Cue);
  run(x, 10);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Cue && rec_of(x).grace);
  run(x, rec::kMemoGraceMs - 20);
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Stop));
  run(x, 40);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Stop));
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle && x.port.heard.back() == rec::EvtKind::Discarded);
  RecRig late;
  late.r.step(10, kG0);
  late.r.step(600);  // one long pass: G0 down at its start, up at its end
  TEST_ASSERT_TRUE(rec_of(late).phase != RecPhase::Idle && rec_of(late).grace);
  run(late, rec::kMemoGraceMs + 20);
  TEST_ASSERT_TRUE(rec_of(late).phase == RecPhase::Idle);  // never a memo nobody holds
}

// The user's ask: G0 twice, talk hands-free, one G0 press ends it. The memo starts on the
// second press's down edge (its Start goes out at once); the page says it plainly; the end
// press saves it and its tap opens no note.
void test_a_double_tap_records_hands_free() {
  RecRig x;
  tap(x, kG0);
  run(x, 100);  // 140 ms after the release
  x.r.step(10, kG0);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  run(x, 50, kG0);
  run(x, 30);  // let go
  x.port.feed(3000);
  run(x, 2000);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && rec_of(x).latched && !rec_of(x).grace);
  TEST_ASSERT_TRUE_MESSAGE(x.r.disp.canvas_.has("免提录音 · 按 G0 结束"), x.r.disp.canvas_.dump().c_str());
  TEST_ASSERT_TRUE(x.r.rt.shell().overlay() == proto::Overlay::None);
  x.r.step(10, kG0);  // the end: on the down edge
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Stop));
  run(x, 70, kG0);
  run(x, 1000);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle && !rec_of(x).held);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_TRUE(rec_of(x).last.saved && rec_of(x).last.ms >= 2000);
  TEST_ASSERT_TRUE(x.r.rt.shell().overlay() == proto::Overlay::None);  // the end press's tap: eaten
}

// One tap is still the quick note, once the double tap's window ran out; nothing records.
void test_a_single_tap_still_opens_the_note() {
  RecRig x;
  tap(x, kG0);  // released at +60
  run(x, kDoubleTapMs - 60);
  TEST_ASSERT_TRUE(x.r.rt.shell().overlay() == proto::Overlay::None);
  run(x, 70);
  TEST_ASSERT_TRUE(x.r.rt.shell().overlay() == proto::Overlay::Compose);
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Start));
}

// Tap, then hold: the memo starts on the hold's down edge and becomes an ordinary held
// memo at 500 ms; letting go ends it after the grace.
void test_tap_then_hold_is_a_held_memo() {
  RecRig x;
  tap(x, kG0);
  run(x, 100);
  run(x, 800, kG0);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && !rec_of(x).latched);
  x.port.feed(1500);
  run(x, rec::kMemoGraceMs + 40);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle && x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
}

// Opt+L on a hands-free memo: locked, and it goes on. Locked, a G0 tap from dark only
// lights the card; a G0 press on the lit card ends it. Space never latched anything.
void test_a_hands_free_memo_locks_and_goes_on() {
  RecRig x;
  x.hold_g0(600);  // held: Space and Opt+L do nothing
  run(x, 60, plus(kG0, kSpace));
  run(x, 60, plus(kG0, kOptL));
  TEST_ASSERT_FALSE(x.r.rt.shell().locked());
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle);
  tap(x, kG0);
  run(x, 100);
  tap(x, kG0);  // hands free
  x.port.feed(1500);
  tap(x, kOptL);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked() && rec_of(x).phase == RecPhase::Recording && rec_of(x).latched);
  run(x, 4000);  // the card is dark
  tap(x, kG0);   // from dark: lights the card
  run(x, 200);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Recording && x.r.rt.shell().lock_lit(x.r.now));
  tap(x, kG0);  // on the lit card: the end
  run(x, 30);
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle && x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_TRUE(x.r.rt.shell().locked());
}

// The 5-minute cap ends a hands-free memo as it ends any memo: saved, said, nothing held.
void test_a_hands_free_memo_stops_at_the_cap() {
  RecRig x;
  tap(x, kG0);
  run(x, 100);
  tap(x, kG0);
  for (int i = 0; i < 31; ++i) {
    x.port.feed(10000);
    run(x, 10000);
  }
  TEST_ASSERT_TRUE(rec_of(x).phase == RecPhase::Idle && !rec_of(x).held);
  TEST_ASSERT_TRUE(rec_of(x).last.saved && rec_of(x).last.why == static_cast<uint8_t>(rec::RecRefusal::Cap));
  TEST_ASSERT_TRUE(x.r.rt.shell().overlay() == proto::Overlay::None);
}
