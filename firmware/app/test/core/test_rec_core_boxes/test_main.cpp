// RecCore's two meeting boxes after docs/20 D3 (U1): the start box is not a recording (the
// screen may sleep, the Mac may pull: rec_live is false and file.* goes on until the
// consent Enter), it gives up after kStartBoxMs without a key; the end box goes back to the
// meeting after kEndBoxMs. Enter inside the 400 ms guard is never silently lost: TooFast
// says so. Cancel (` / Fn+`, a letter on the start box, Opt+L) is never guarded: it can
// only stop something from starting or keep a meeting going.
#include <unity.h>

#include "rec_core.h"

using namespace cpa;
using namespace cpa::rec;

void setUp() {}
void tearDown() {}

static RecCtx ctx(uint32_t now) {
  RecCtx c;
  c.now = now;
  c.avail = true;
  c.free_known = true;
  c.free_bytes = 64ull << 30;
  c.bat_mv = 4000;
  return c;
}

static bool has(const RecStep& s, RecFx k) {
  for (uint8_t i = 0; i < s.effects.count; ++i) {
    if (s.effects.items[i].kind == k) return true;
  }
  return false;
}

static int index_of(const RecStep& s, RecFx k) {
  for (uint8_t i = 0; i < s.effects.count; ++i) {
    if (s.effects.items[i].kind == k) return i;
  }
  return -1;
}

static RecCoreState box_at(uint32_t at) {
  RecCoreState s;
  s.ready = true;
  return rec_gesture(s, RecMode::Meeting, ctx(at)).state;
}

static RecCoreState end_box_at(uint32_t at) {
  RecStep st = rec_input(box_at(1000), RecInput::Confirm, 0, ctx(1500));
  RecEvt started;
  started.kind = EvtKind::Started;
  st = rec_event(st.state, started, ctx(2000));
  return rec_input(st.state, RecInput::End, 0, ctx(at)).state;
}

// D3: the start box pauses nothing; the consent Enter does, as a memo's start does.
static void test_start_box_is_not_live() {
  RecCoreState s;
  s.ready = true;
  s = rec_file_queued(s).state;  // one with the audio task
  s = rec_file_queued(s).state;  // one held by the runtime
  const RecStep box = rec_gesture(s, RecMode::Meeting, ctx(1000));
  TEST_ASSERT_TRUE(box.state.phase == RecPhase::Confirm);
  TEST_ASSERT_FALSE(rec_live(box.state));
  TEST_ASSERT_FALSE(has(box, RecFx::FlushBusy));
  TEST_ASSERT_EQUAL_UINT8(1, box.state.queued);
  const RecStep next = rec_file_done(box.state);  // the pull goes on under the box
  TEST_ASSERT_TRUE(has(next, RecFx::PostNext) && next.state.at_task && next.state.queued == 0);
  const RecStep more = rec_file_queued(next.state);
  const RecStep go = rec_input(more.state, RecInput::Confirm, 0, ctx(1400));
  TEST_ASSERT_TRUE(go.state.phase == RecPhase::Cue && rec_live(go.state));
  TEST_ASSERT_TRUE(index_of(go, RecFx::FlushBusy) >= 0 && index_of(go, RecFx::FlushBusy) < index_of(go, RecFx::Start));
  TEST_ASSERT_EQUAL_UINT8(0, go.state.queued);
}

// D3: an Enter inside the guard does nothing but say so, on both boxes.
static void test_too_fast_enter_says_so() {
  const RecStep fast = rec_input(box_at(1000), RecInput::Confirm, 0, ctx(1250));
  TEST_ASSERT_TRUE(fast.state.phase == RecPhase::Confirm && has(fast, RecFx::TooFast));
  TEST_ASSERT_FALSE(has(fast, RecFx::Start));
  const RecStep late = rec_input(fast.state, RecInput::Confirm, 0, ctx(1400));
  TEST_ASSERT_TRUE(late.state.phase == RecPhase::Cue && has(late, RecFx::Start) && !has(late, RecFx::TooFast));
  const RecStep end_fast = rec_input(end_box_at(6000), RecInput::Confirm, 0, ctx(6300));
  TEST_ASSERT_TRUE(end_fast.state.phase == RecPhase::EndConfirm && has(end_fast, RecFx::TooFast));
  TEST_ASSERT_FALSE(has(end_fast, RecFx::Stop));
}

// Cancel is never guarded: Opt+R then ` (or Opt+L) at once leaves no box behind.
static void test_cancel_is_never_guarded() {
  const RecStep gone = rec_input(box_at(1000), RecInput::Cancel, 0, ctx(1050));
  TEST_ASSERT_TRUE(gone.state.phase == RecPhase::Idle && gone.effects.count == 0);
  const RecStep back = rec_input(end_box_at(6000), RecInput::Cancel, 0, ctx(6050));
  TEST_ASSERT_TRUE(back.state.phase == RecPhase::Recording && back.effects.count == 0);
}

// D3: 15 s without a key, the start box gives up: nothing starts, no consent is kept.
static void test_start_box_gives_up_after_15_s() {
  const RecCoreState box = box_at(1000);
  TEST_ASSERT_TRUE(rec_tick(box, ctx(1000 + kStartBoxMs - 1)).state.phase == RecPhase::Confirm);
  const RecStep out = rec_tick(box, ctx(1000 + kStartBoxMs));
  TEST_ASSERT_TRUE(out.state.phase == RecPhase::Idle && !out.state.consent);
  TEST_ASSERT_FALSE(has(out, RecFx::Start));
  TEST_ASSERT_EQUAL_UINT32(15000, kStartBoxMs);
}

// D3: 10 s without a key, the end box goes back to the meeting, which goes on.
static void test_end_box_goes_back_after_10_s() {
  const RecCoreState box = end_box_at(6000);
  TEST_ASSERT_TRUE(rec_tick(box, ctx(6000 + kEndBoxMs - 1)).state.phase == RecPhase::EndConfirm);
  const RecStep out = rec_tick(box, ctx(6000 + kEndBoxMs));
  TEST_ASSERT_TRUE(out.state.phase == RecPhase::Recording);
  TEST_ASSERT_FALSE(has(out, RecFx::Stop));
  TEST_ASSERT_EQUAL_UINT32(2000, out.state.started_ms);  // the clock goes on from the start
  TEST_ASSERT_EQUAL_UINT32(10000, kEndBoxMs);
}

// D3 "15 s / 10 s idle": a key on a box (Touch, the runtime's every key and the menu open
// over it) starts the box's time again; the 400 ms guard still counts from the opening.
static void test_a_key_starts_the_box_time_again() {
  const RecCoreState box = rec_input(box_at(1000), RecInput::Touch, 0, ctx(15000)).state;
  TEST_ASSERT_TRUE(box.phase == RecPhase::Confirm && box.opened_ms == 1000);
  TEST_ASSERT_TRUE(rec_tick(box, ctx(15000 + kStartBoxMs - 1)).state.phase == RecPhase::Confirm);
  TEST_ASSERT_TRUE(rec_tick(box, ctx(15000 + kStartBoxMs)).state.phase == RecPhase::Idle);
  const RecCoreState end = rec_input(end_box_at(6000), RecInput::Touch, 0, ctx(15000)).state;
  TEST_ASSERT_TRUE(rec_tick(end, ctx(15000 + kEndBoxMs - 1)).state.phase == RecPhase::EndConfirm);
  TEST_ASSERT_TRUE(rec_tick(end, ctx(15000 + kEndBoxMs)).state.phase == RecPhase::Recording);
  const RecCoreState early = rec_input(box_at(1000), RecInput::Touch, 0, ctx(1300)).state;
  const RecStep go = rec_input(early, RecInput::Confirm, 0, ctx(1450));
  TEST_ASSERT_TRUE(go.state.phase == RecPhase::Cue && !has(go, RecFx::TooFast));
  const RecCoreState meeting = rec_input(end_box_at(6000), RecInput::Cancel, 0, ctx(7000)).state;
  const RecStep on_page = rec_input(meeting, RecInput::Touch, 0, ctx(8000));  // no box: nothing
  TEST_ASSERT_TRUE(on_page.state.phase == RecPhase::Recording && on_page.effects.count == 0);
  TEST_ASSERT_EQUAL_UINT32(meeting.opened_ms, on_page.state.opened_ms);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_box_is_not_live);
  RUN_TEST(test_too_fast_enter_says_so);
  RUN_TEST(test_cancel_is_never_guarded);
  RUN_TEST(test_start_box_gives_up_after_15_s);
  RUN_TEST(test_end_box_goes_back_after_10_s);
  RUN_TEST(test_a_key_starts_the_box_time_again);
  return UNITY_END();
}
