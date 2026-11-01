// RecCore, how a recording ends (docs/20 S1, U6, U15): the Finishing timeout when the
// audio task's end event is lost, the reason and length of every end, the memo cap held
// until G0 is released, and the 3.55 V warning line (docs/11 §4.3).
#include <unity.h>

#include "rec_core.h"

using namespace cpa;
using namespace cpa::rec;

void setUp() {}
void tearDown() {}

static RecCtx ctx(uint32_t now = 1000) {
  RecCtx c;
  c.now = now;
  c.avail = true;
  c.files_n = 3;
  c.free_known = true;
  c.free_bytes = 64ull << 30;
  c.bat_mv = 4000;
  c.slot = 2;
  return c;
}

static bool has(const RecStep& s, RecFx k) {
  for (uint8_t i = 0; i < s.effects.count; ++i) {
    if (s.effects.items[i].kind == k) return true;
  }
  return false;
}

static const RecEffect* find(const RecStep& s, RecFx k) {
  for (uint8_t i = 0; i < s.effects.count; ++i) {
    if (s.effects.items[i].kind == k) return &s.effects.items[i];
  }
  return nullptr;
}

static RecEvt evt(EvtKind k) {
  RecEvt e;
  e.kind = k;
  return e;
}

// Recording since at + 900 (a meeting's consent Enter at at + 500).
static RecCoreState recording(RecMode mode, uint32_t at = 1000) {
  RecCoreState s;
  s.ready = true;
  RecStep st = rec_gesture(s, mode, ctx(at));
  if (mode == RecMode::Meeting) st = rec_input(st.state, RecInput::Confirm, 0, ctx(at + 500));
  return rec_event(st.state, evt(EvtKind::Started), ctx(at + 900)).state;
}

// A memo let go of at `at` (gh #51): G0 up, and the grace runs out without a re-press.
static RecStep let_go(const RecCoreState& s, uint32_t at) {
  const RecStep up = rec_input(s, RecInput::Release, 0, ctx(at));
  return rec_tick(up.state, ctx(at + kMemoGraceMs));
}

// docs/20 S1 (§4.3): a lost end event left the core in Finishing for good, and every key
// was dead until a reboot. After 5 s the Stop goes out again; after 10 s the core gives
// up: Idle, with a failure effect (the segments already written stay on the card).
static void test_finishing_times_out() {
  RecCoreState s = recording(RecMode::Memo, 0);  // started at 900
  RecStep st = let_go(s, 3000);
  const uint32_t at = 3000 + kMemoGraceMs;  // the Stop went out
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing);
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(at + kFinishRetryMs - 1)).effects.count);
  st = rec_tick(st.state, ctx(at + kFinishRetryMs));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing && has(st, RecFx::Stop));
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(at + kFinishRetryMs + 10)).effects.count);  // once
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(at + kFinishGiveUpMs - 1)).effects.count);
  st = rec_tick(st.state, ctx(at + kFinishGiveUpMs));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle && !rec_live(st.state));
  const RecEffect* fail = find(st, RecFx::Refuse);
  TEST_ASSERT_TRUE(fail && fail->why == RecRefusal::Timeout);
  TEST_ASSERT_TRUE(kFinishGiveUpMs >= 5000 && kFinishGiveUpMs <= 10000);  // back within 5-10 s
  // a late end event after giving up changes nothing
  TEST_ASSERT_EQUAL_UINT8(0, rec_event(st.state, evt(EvtKind::Saved), ctx(20000)).effects.count);
  // a meeting's end confirm -> Finishing times out the same way
  RecCoreState m = recording(RecMode::Meeting, 0);
  m = rec_input(m, RecInput::End, 0, ctx(5000)).state;
  m = rec_input(m, RecInput::Confirm, 0, ctx(5500)).state;
  TEST_ASSERT_TRUE(rec_tick(m, ctx(5500 + kFinishGiveUpMs)).state.phase == RecPhase::Idle);
}

// docs/20 U6: every end of a started recording says how it ended, with its length; an
// automatic stop names its reason (the battery, the cap, too many segments waiting).
static void test_auto_stop_reasons() {
  RecStep st = let_go(recording(RecMode::Memo, 0), 13000);
  st = rec_event(st.state, evt(EvtKind::Saved), ctx(13700));
  const RecEffect* end = find(st, RecFx::Ended);
  TEST_ASSERT_TRUE(end && end->saved && end->why == RecRefusal::None && end->mode == RecMode::Memo);
  TEST_ASSERT_EQUAL_UINT32(13000 + kMemoGraceMs - 900, end->ms);  // until the stop, not until the save
  st = let_go(recording(RecMode::Memo, 0), 1200);
  end = find(rec_event(st.state, evt(EvtKind::Discarded), ctx(1900)), RecFx::Ended);
  TEST_ASSERT_TRUE(end && !end->saved && end->why == RecRefusal::None);
  // the battery stops a meeting: the reason, then the end
  RecCtx low = ctx(5000);
  low.bat_mv = 3440;
  st = rec_tick(recording(RecMode::Meeting, 0), low);
  low.now = 15000;
  st = rec_tick(st.state, low);
  st = rec_event(st.state, evt(EvtKind::Saved), ctx(15200));
  end = find(st, RecFx::Ended);
  TEST_ASSERT_TRUE(end && end->saved && end->why == RecRefusal::Battery && end->mode == RecMode::Meeting);
  TEST_ASSERT_EQUAL_UINT32(15000 - 900, end->ms);
  // the audio task stopped by itself at the cap: Saved arrives while still recording
  end = find(rec_event(recording(RecMode::Meeting, 0), evt(EvtKind::Saved), ctx(9000)), RecFx::Ended);
  TEST_ASSERT_TRUE(end && end->why == RecRefusal::Cap);
  // the loop's own cap stop
  st = rec_tick(recording(RecMode::Meeting, 0), ctx(900 + 10800000 + kCapGraceMs));
  TEST_ASSERT_TRUE(st.state.stop_why == RecRefusal::Cap);
  // 256 segments waiting (the runtime's index is full)
  st = rec_stop_for(recording(RecMode::Meeting, 0), RecRefusal::Pending, ctx(7000));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing && has(st, RecFx::Stop));
  end = find(rec_event(st.state, evt(EvtKind::Saved), ctx(7100)), RecFx::Ended);
  TEST_ASSERT_TRUE(end && end->why == RecRefusal::Pending);
  TEST_ASSERT_EQUAL_UINT8(0, rec_stop_for(RecCoreState{}, RecRefusal::Pending, ctx()).effects.count);  // idle
  // a new recording starts with no reason
  TEST_ASSERT_TRUE(rec_gesture(RecCoreState{}, RecMode::Memo, ctx()).state.stop_why == RecRefusal::None);
}

// docs/20 U15: the memo cap ends the recording while G0 is still held; the overlay stays
// ("已到 5 分钟，已保存，松开 G0") until the release, instead of jumping back to the page.
static void test_memo_cap_holds_until_release() {
  const RecStep st = rec_event(recording(RecMode::Memo, 0), evt(EvtKind::Saved), ctx(300900));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle && st.state.held && !rec_live(st.state));
  TEST_ASSERT_TRUE(find(st, RecFx::Ended) && find(st, RecFx::Ended)->why == RecRefusal::Cap);
  TEST_ASSERT_TRUE(rec_input(st.state, RecInput::Star, 0, ctx(301000)).state.held);  // only the release
  const RecStep up = rec_input(st.state, RecInput::Release, 0, ctx(302000));
  TEST_ASSERT_FALSE(up.state.held);
  TEST_ASSERT_EQUAL_UINT8(0, up.effects.count);
  // released first (the usual memo, ended in its grace), nothing is held
  RecStep rel = rec_input(recording(RecMode::Memo, 0), RecInput::Release, 0, ctx(5000));
  TEST_ASSERT_FALSE(rec_event(rel.state, evt(EvtKind::Saved), ctx(5100)).state.held);
  // a meeting is never held (no G0 under it)
  TEST_ASSERT_FALSE(rec_event(recording(RecMode::Meeting, 0), evt(EvtKind::Saved), ctx(9000)).state.held);
  // a failure while G0 is held also waits for the release
  RecEvt f = evt(EvtKind::Failed);
  f.fail = RecFail::Disk;
  TEST_ASSERT_TRUE(rec_event(recording(RecMode::Memo, 0), f, ctx(4000)).state.held);
}

// docs/11 §4.3, docs/20 U6: at or below 3.55 V while recording, a warning once per
// recording (the stop line is 3.45 V for 10 s).
static void test_battery_warning_line() {
  TEST_ASSERT_EQUAL_UINT16(3550, kBatWarnMv);
  RecCtx warn = ctx(5000);
  warn.bat_mv = 3550;
  RecStep st = rec_tick(recording(RecMode::Meeting, 0), warn);
  TEST_ASSERT_TRUE(has(st, RecFx::BatWarn) && st.state.bat_low);
  TEST_ASSERT_FALSE(has(st, RecFx::Stop));
  warn.now = 6000;
  TEST_ASSERT_FALSE(has(rec_tick(st.state, warn), RecFx::BatWarn));  // once
  RecCtx fine = ctx(5000);
  fine.bat_mv = 3551;
  TEST_ASSERT_FALSE(has(rec_tick(recording(RecMode::Meeting, 0), fine), RecFx::BatWarn));
  RecCtx unknown = ctx(5000);
  unknown.bat_mv = 0;
  TEST_ASSERT_FALSE(has(rec_tick(recording(RecMode::Memo, 0), unknown), RecFx::BatWarn));
  TEST_ASSERT_FALSE(rec_gesture(RecCoreState{}, RecMode::Memo, ctx()).state.bat_low);  // fresh per recording
}

static RecEvt tagged(EvtKind k, uint8_t gen) {
  RecEvt e = evt(k);
  e.gen = gen;
  return e;
}

// Review (S1): the end of an earlier recording comes late, after the core gave up on it
// and a new memo started (the audio task was still in end_session, a slow card). It
// belongs to the old recording (its generation): the new memo stays in Cue, its own
// Started makes it Recording, and the release sends the Stop.
static void test_a_late_end_belongs_to_the_old_recording() {
  RecCoreState s = recording(RecMode::Memo, 0);
  const uint8_t old = s.gen;
  TEST_ASSERT_TRUE(old != 0);
  s = let_go(s, 3000).state;
  s = rec_tick(s, ctx(3000 + kMemoGraceMs + kFinishGiveUpMs)).state;
  TEST_ASSERT_TRUE(s.phase == RecPhase::Idle);
  RecStep st = rec_gesture(s, RecMode::Memo, ctx(14000));
  const RecEffect* start = find(st, RecFx::Start);
  TEST_ASSERT_TRUE(start && start->gen != old && start->gen != 0);
  const uint8_t now = start->gen;
  st = rec_event(st.state, tagged(EvtKind::Saved, old), ctx(14100));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Cue);
  TEST_ASSERT_EQUAL_UINT8(0, st.effects.count);  // no "已到 5 分钟上限" for someone else's end
  st = rec_event(st.state, tagged(EvtKind::Started, old), ctx(14150));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Cue);
  st = rec_event(st.state, tagged(EvtKind::Started, now), ctx(14200));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Recording);
  st = let_go(st.state, 16000);
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing && has(st, RecFx::Stop));
  st = rec_event(st.state, tagged(EvtKind::Saved, now), ctx(16700));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle && has(st, RecFx::Ended));
  // the generation wraps past 0 (0: a port that does not tag, taken as the current one)
  RecCoreState w;
  w.ready = true;
  w.gen = 255;
  TEST_ASSERT_EQUAL_UINT8(1, rec_gesture(w, RecMode::Memo, ctx()).state.gen);
  // a Started while nothing is shown: the audio task records unseen, so it is stopped
  RecStep idle = rec_event(RecCoreState{}, tagged(EvtKind::Started, old), ctx());
  TEST_ASSERT_TRUE(idle.state.phase == RecPhase::Idle && has(idle, RecFx::Stop));
}

// Review (U6): the card fills during a recording. RecEngine keeps the segment so far, so
// it is an automatic stop that saved ("会议已停：SD 卡满 · 已存 1:00"), not a refusal;
// before Started (nothing recorded) it stays one.
static void test_card_full_while_recording_is_a_saved_stop() {
  RecEvt full = evt(EvtKind::Failed);
  full.fail = RecFail::Full;
  RecStep st = rec_event(recording(RecMode::Meeting, 0), full, ctx(60900));
  const RecEffect* end = find(st, RecFx::Ended);
  TEST_ASSERT_TRUE(end && end->saved && end->why == RecRefusal::CardFull && end->mode == RecMode::Meeting);
  TEST_ASSERT_EQUAL_UINT32(60000, end->ms);
  TEST_ASSERT_FALSE(has(st, RecFx::Refuse));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle);
  const RecStep cue = rec_gesture(RecCoreState{}, RecMode::Memo, ctx(1000));
  const RecEffect* refuse = find(rec_event(cue.state, full, ctx(1100)), RecFx::Refuse);
  TEST_ASSERT_TRUE(refuse && refuse->why == RecRefusal::CardFull);
  RecEvt disk = evt(EvtKind::Failed);
  disk.fail = RecFail::Disk;
  refuse = find(rec_event(recording(RecMode::Meeting, 0), disk, ctx(5000)), RecFx::Refuse);
  TEST_ASSERT_TRUE(refuse && refuse->why == RecRefusal::CardError);  // the card failed: no end
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_finishing_times_out);
  RUN_TEST(test_auto_stop_reasons);
  RUN_TEST(test_memo_cap_holds_until_release);
  RUN_TEST(test_battery_warning_line);
  RUN_TEST(test_a_late_end_belongs_to_the_old_recording);
  RUN_TEST(test_card_full_while_recording_is_a_saved_stop);
  return UNITY_END();
}
