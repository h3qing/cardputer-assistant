// RecCore (docs/11 §3-§4, §4.7): every input in every phase, the caps, the refusals of
// §4.3, the consent confirm, the cue-stop and short-memo discards, and the mutual
// exclusion of recording and file.* transfer (docs/20 D3: the boxes, test_rec_core_boxes). Recording is silent: a refusal or a failure
// is the screen's alone (v0.3.1).
#include <unity.h>

#include <vector>

#include "protocol_limits.h"
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

static std::vector<RecFx> kinds(const RecStep& s) {
  std::vector<RecFx> out;
  for (uint8_t i = 0; i < s.effects.count; ++i) out.push_back(s.effects.items[i].kind);
  return out;
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

static RecCoreState recording(RecMode mode, uint32_t at = 1000) {
  RecCoreState s;
  s.ready = true;
  RecStep st = mode == RecMode::Memo ? rec_gesture(s, mode, ctx(at)) : rec_gesture(s, mode, ctx(at));
  if (mode == RecMode::Meeting) st = rec_input(st.state, RecInput::Confirm, 0, ctx(at + 500));
  return rec_event(st.state, evt(EvtKind::Started), ctx(at + 900)).state;
}

static void test_memo_hold_record_release() {
  RecCoreState s;
  s.ready = true;
  RecStep st = rec_gesture(s, RecMode::Memo, ctx(1000));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Cue);
  const RecEffect* start = find(st, RecFx::Start);
  TEST_ASSERT_TRUE(start && start->mode == RecMode::Memo && start->slot == 2 && !start->consent);
  TEST_ASSERT_TRUE(rec_live(st.state));
  st = rec_input(st.state, RecInput::Tag, 5, ctx(1100));  // digits while holding G0
  TEST_ASSERT_EQUAL_UINT8(5, st.state.slot);
  TEST_ASSERT_TRUE(find(st, RecFx::Tag)->slot == 5);
  st = rec_event(st.state, evt(EvtKind::Started), ctx(1650));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Recording && st.state.started_ms == 1650);
  st = rec_input(st.state, RecInput::Star, 0, ctx(2000));  // memos have no stars
  TEST_ASSERT_EQUAL_UINT8(0, st.effects.count);
  st = rec_input(st.state, RecInput::Release, 0, ctx(5000));  // gh #51: the grace first
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Recording && st.effects.count == 0);
  st = rec_tick(st.state, ctx(5000 + kMemoGraceMs));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing && has(st, RecFx::Stop));
  st = rec_event(st.state, evt(EvtKind::Saved), ctx(5900));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle && !rec_live(st.state));
}

// B5 and gh #51: G0 let go in the cue, before the microphone came up (the late path gives
// G0Long and G0LongUp in one step). The grace runs there too: a re-press goes on; when it
// runs out, only the Stop goes out and the audio task discards the attempt.
static void test_release_in_the_cue_waits_for_the_grace() {
  RecCoreState s;
  s.ready = true;
  RecStep st = rec_gesture(s, RecMode::Memo, ctx(1000));
  st = rec_input(st.state, RecInput::Release, 0, ctx(1000));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Cue && rec_grace(st.state) && st.effects.count == 0);
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(1000 + kMemoGraceMs - 1)).effects.count);
  st = rec_tick(st.state, ctx(1000 + kMemoGraceMs));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing && has(st, RecFx::Stop));
  TEST_ASSERT_EQUAL_UINT8(1, st.effects.count);
  st = rec_event(st.state, evt(EvtKind::Started), ctx(1700));  // a late Started changes nothing
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing);
  st = rec_event(st.state, evt(EvtKind::Discarded), ctx(1800));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle && !find(st, RecFx::Ended)->saved);
  // the microphone comes up inside the grace: Recording, the grace still running
  st = rec_input(rec_gesture(s, RecMode::Memo, ctx(3000)).state, RecInput::Release, 0, ctx(3000));
  st = rec_event(st.state, evt(EvtKind::Started), ctx(3100));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Recording && rec_grace(st.state));
  TEST_ASSERT_TRUE(has(rec_tick(st.state, ctx(3000 + kMemoGraceMs)), RecFx::Stop));
  // a re-press in the cue's grace goes on
  st = rec_input(rec_gesture(s, RecMode::Memo, ctx(5000)).state, RecInput::Release, 0, ctx(5000));
  st = rec_input(st.state, RecInput::Press, 0, ctx(5200));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Cue && !rec_grace(st.state));
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(9000)).effects.count);
}

// gh #51 (docs/20 U26): letting go of G0 starts a grace of kMemoGraceMs. The memo records
// on; only when the grace runs out does it end, the grace audio kept (a short silence).
static void test_memo_release_grace_then_end() {
  TEST_ASSERT_EQUAL_UINT32(600, kMemoGraceMs);
  RecCoreState s = recording(RecMode::Memo);  // started at 1900
  RecStep st = rec_input(s, RecInput::Release, 0, ctx(5000));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Recording && rec_grace(st.state) && rec_live(st.state));
  TEST_ASSERT_EQUAL_UINT8(0, st.effects.count);
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(5000 + kMemoGraceMs - 1)).effects.count);
  st = rec_tick(st.state, ctx(5000 + kMemoGraceMs));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing && has(st, RecFx::Stop) && !rec_grace(st.state));
  st = rec_event(st.state, evt(EvtKind::Saved), ctx(5700));
  const RecEffect* end = find(st, RecFx::Ended);
  TEST_ASSERT_TRUE(end && end->saved && end->why == RecRefusal::None && !st.state.held);
  TEST_ASSERT_EQUAL_UINT32(5000 + kMemoGraceMs - 1900, end->ms);  // the grace audio is in it
}

// A re-press inside the grace goes on with the same memo: no Start, no Stop, no new file.
// Every slip has a grace of its own; a second Release while up keeps the first one's.
static void test_memo_re_press_in_the_grace_resumes() {
  const RecCoreState s = rec_input(recording(RecMode::Memo), RecInput::Release, 0, ctx(5000)).state;
  RecStep st = rec_input(s, RecInput::Press, 0, ctx(5300));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Recording && !rec_grace(st.state));
  TEST_ASSERT_EQUAL_UINT8(0, st.effects.count);
  TEST_ASSERT_TRUE(st.state.gen == s.gen && st.state.started_ms == 1900);  // the same recording
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(9000)).effects.count);  // that grace is over
  st = rec_input(st.state, RecInput::Release, 0, ctx(9000));
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(9000 + kMemoGraceMs - 1)).effects.count);
  TEST_ASSERT_TRUE(has(rec_tick(st.state, ctx(9000 + kMemoGraceMs)), RecFx::Stop));
  const RecCoreState twice = rec_input(s, RecInput::Release, 0, ctx(5400)).state;
  TEST_ASSERT_TRUE(has(rec_tick(twice, ctx(5000 + kMemoGraceMs)), RecFx::Stop));
  // a Press while G0 is down, or with nothing recording, does nothing
  const RecCoreState held = recording(RecMode::Memo);
  TEST_ASSERT_EQUAL_UINT8(0, rec_input(held, RecInput::Press, 0, ctx(3000)).effects.count);
  TEST_ASSERT_TRUE(rec_input(RecCoreState{}, RecInput::Press, 0, ctx()).state.phase == RecPhase::Idle);
  // digits tag the memo in the grace too
  TEST_ASSERT_EQUAL_UINT8(4, rec_input(s, RecInput::Tag, 4, ctx(5100)).state.slot);
}

// A hands-free memo (the double tap, g0_gesture.h): latched on the second press's down edge,
// right after its start. Letting go ends nothing, there is no grace; the next G0 press ends
// it on its down edge (not its release), saved as usual.
static RecCoreState hands_free(uint32_t at = 1000) {
  RecCoreState s;
  s.ready = true;
  RecStep st = rec_gesture(s, RecMode::Memo, ctx(at));
  st = rec_input(st.state, RecInput::Latch, 0, ctx(at));
  return rec_event(st.state, evt(EvtKind::Started), ctx(at + 900)).state;  // started at at + 900
}

static void test_hands_free_memo() {
  RecStep st = rec_input(hands_free(), RecInput::Release, 0, ctx(1100));  // the second tap goes up
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Recording && rec_latched(st.state) && !rec_grace(st.state));
  TEST_ASSERT_EQUAL_UINT8(0, st.effects.count);
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(60000)).effects.count);
  st = rec_input(st.state, RecInput::Tag, 3, ctx(30000));  // digits tag it, nothing else
  TEST_ASSERT_TRUE(st.state.slot == 3 && rec_latched(st.state));
  st = rec_input(st.state, RecInput::Press, 0, ctx(60000));  // one press: the end, on its down edge
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing && has(st, RecFx::Stop));
  st = rec_input(st.state, RecInput::Release, 0, ctx(60100));  // its release: nothing more
  TEST_ASSERT_EQUAL_UINT8(0, st.effects.count);
  st = rec_event(st.state, evt(EvtKind::Saved), ctx(60200));
  const RecEffect* end = find(st, RecFx::Ended);
  TEST_ASSERT_TRUE(end && end->saved && end->why == RecRefusal::None && !st.state.held);
  TEST_ASSERT_EQUAL_UINT32(60000 - 1900, end->ms);
  TEST_ASSERT_FALSE(rec_latched(st.state));
  TEST_ASSERT_FALSE(rec_latched(rec_gesture(st.state, RecMode::Memo, ctx(70000)).state));  // a new memo
}

// Tap, then hold (g0_gesture.h): the latching press is held on to a hold. Hold makes it a
// held memo after all: its release starts the grace and the grace ends it. (Hold only ever
// comes while that press is down: the shell sends it on the press's G0Long.)
static void test_hold_makes_it_a_held_memo() {
  RecStep st = rec_input(hands_free(), RecInput::Hold, 0, ctx(1500));
  TEST_ASSERT_TRUE(!rec_latched(st.state) && st.effects.count == 0);
  st = rec_input(st.state, RecInput::Release, 0, ctx(5000));
  TEST_ASSERT_TRUE(rec_grace(st.state));
  TEST_ASSERT_TRUE(has(rec_tick(st.state, ctx(5000 + kMemoGraceMs)), RecFx::Stop));
}

// Only a memo is ever hands-free: a meeting and an idle core ignore Latch and Hold. In the
// cue (the microphone still coming up) the latch holds, and a press there ends the attempt.
static void test_only_a_memo_is_hands_free() {
  RecStep st = rec_input(recording(RecMode::Meeting), RecInput::Latch, 0, ctx(3000));
  TEST_ASSERT_TRUE(!rec_latched(st.state) && st.effects.count == 0);
  TEST_ASSERT_EQUAL_UINT8(0, rec_input(recording(RecMode::Meeting), RecInput::Hold, 0, ctx(3000)).effects.count);
  TEST_ASSERT_FALSE(rec_latched(rec_input(RecCoreState{}, RecInput::Latch, 0, ctx()).state));
  RecCoreState c;
  c.ready = true;
  st = rec_input(rec_gesture(c, RecMode::Memo, ctx(1000)).state, RecInput::Latch, 0, ctx(1000));
  TEST_ASSERT_TRUE(rec_latched(st.state) && st.state.phase == RecPhase::Cue);
  st = rec_input(st.state, RecInput::Release, 0, ctx(1100));
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(9000)).effects.count);
  st = rec_input(st.state, RecInput::Press, 0, ctx(1300));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing && has(st, RecFx::Stop));
}

// The 5-minute cap holds through the grace and hands free: the loop's own stop and the
// audio task's stop at the cap. The cap screen waits only while G0 is down (docs/20 U15).
static void test_cap_in_the_grace_and_hands_free() {
  const RecCoreState free = rec_input(hands_free(0), RecInput::Release, 0, ctx(100)).state;  // started at 900
  RecStep st = rec_tick(free, ctx(900 + 300000 + kCapGraceMs));
  TEST_ASSERT_TRUE(has(st, RecFx::Stop) && st.state.stop_why == RecRefusal::Cap);
  st = rec_event(free, evt(EvtKind::Saved), ctx(300900));  // the audio task, at the cap
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle && !st.state.held);
  TEST_ASSERT_TRUE(find(st, RecFx::Ended)->why == RecRefusal::Cap);
  const RecCoreState holding = hands_free(0);  // the second press still down at the cap
  TEST_ASSERT_TRUE(rec_event(holding, evt(EvtKind::Saved), ctx(300900)).state.held);
  const RecCoreState grace = rec_input(recording(RecMode::Memo, 0), RecInput::Release, 0, ctx(300800)).state;
  st = rec_event(grace, evt(EvtKind::Saved), ctx(300900));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle && !st.state.held && !rec_grace(st.state));
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(st.state, ctx(301400)).effects.count);  // no late Stop
}

// rec_min_ms (protocol limits) is the audio task's: a memo let go soon after ● REC still
// ends in the grace and the task's Discarded says "too short" (Ended, not saved).
static void test_short_memo_still_discarded() {
  RecStep st = rec_input(recording(RecMode::Memo), RecInput::Release, 0, ctx(2100));
  st = rec_tick(st.state, ctx(2100 + kMemoGraceMs));
  TEST_ASSERT_TRUE(has(st, RecFx::Stop));
  st = rec_event(st.state, evt(EvtKind::Discarded), ctx(2800));
  const RecEffect* end = find(st, RecFx::Ended);
  TEST_ASSERT_TRUE(end && !end->saved && st.state.phase == RecPhase::Idle);
  TEST_ASSERT_TRUE(2100 + kMemoGraceMs - 1900 < limits::kRecMinMs);  // under rec_min_ms, grace and all
}

static void test_meeting_needs_the_consent_enter() {
  RecCoreState s;
  s.ready = true;
  RecStep st = rec_gesture(s, RecMode::Meeting, ctx(1000));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Confirm && !has(st, RecFx::Start));
  TEST_ASSERT_FALSE(rec_live(st.state));  // docs/20 D3: the box is no recording, transfers go on
  st = rec_input(st.state, RecInput::Confirm, 0, ctx(1399));  // 400 ms guard
  // docs/20 D3: the guarded Enter is said (TooFast), never dropped silently
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Confirm && st.effects.count == 1 && has(st, RecFx::TooFast));
  for (RecInput in : {RecInput::Star, RecInput::Tag, RecInput::End, RecInput::Release}) {
    TEST_ASSERT_TRUE(rec_input(st.state, in, 1, ctx(1500)).state.phase == RecPhase::Confirm);
  }
  const RecStep cancel = rec_input(st.state, RecInput::Cancel, 0, ctx(1500));
  TEST_ASSERT_TRUE(cancel.state.phase == RecPhase::Idle && cancel.effects.count == 0);
  st = rec_input(st.state, RecInput::Confirm, 0, ctx(1400));
  const RecEffect* start = find(st, RecFx::Start);
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Cue && start && start->mode == RecMode::Meeting && start->consent);
}

static void test_meeting_page_keys() {
  RecCoreState s = recording(RecMode::Meeting);
  RecStep st = rec_input(s, RecInput::Star, 0, ctx(3000));
  TEST_ASSERT_TRUE(has(st, RecFx::Mark) && st.state.stars == 1 && st.state.star_ms == 3000);
  for (int i = 1; i < 16; ++i) st = rec_input(st.state, RecInput::Star, 0, ctx(3000 + i));
  TEST_ASSERT_EQUAL_UINT8(16, st.state.seg_stars);
  st = rec_input(st.state, RecInput::Star, 0, ctx(4000));
  TEST_ASSERT_FALSE(has(st, RecFx::Mark));  // 16 per segment
  RecEvt seg = evt(EvtKind::Segment);
  seg.seg = 1;
  st = rec_event(st.state, seg, ctx(5000));
  TEST_ASSERT_TRUE(st.state.seg == 1 && st.state.seg_stars == 0 && st.state.stars == 16);
  st = rec_input(st.state, RecInput::Star, 0, ctx(5100));
  TEST_ASSERT_TRUE(has(st, RecFx::Mark));
  st = rec_input(st.state, RecInput::Tag, 0, ctx(5200));
  TEST_ASSERT_TRUE(find(st, RecFx::Tag) && st.state.slot == 0);
  st = rec_input(st.state, RecInput::Release, 0, ctx(5300));  // G0 release on the page: nothing
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Recording && st.effects.count == 0);
  st = rec_input(st.state, RecInput::End, 0, ctx(6000));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::EndConfirm && st.state.opened_ms == 6000);
  TEST_ASSERT_TRUE(rec_input(st.state, RecInput::Confirm, 0, ctx(6100)).state.phase == RecPhase::EndConfirm);
  TEST_ASSERT_TRUE(rec_input(st.state, RecInput::Star, 0, ctx(6500)).effects.count == 0);
  const RecStep back = rec_input(st.state, RecInput::Cancel, 0, ctx(6500));
  TEST_ASSERT_TRUE(back.state.phase == RecPhase::Recording && back.effects.count == 0);
  st = rec_input(st.state, RecInput::Confirm, 0, ctx(6500));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Finishing && has(st, RecFx::Stop));
  TEST_ASSERT_EQUAL_UINT8(0, rec_input(st.state, RecInput::Star, 0, ctx(6600)).effects.count);
}

static void test_refusals() {
  RecCoreState s;
  s.ready = true;
  struct Case {
    RecRefusal why;
    RecCtx c;
  };
  RecCtx no_sd = ctx();
  no_sd.avail = false;
  RecCtx full = ctx();
  full.free_bytes = (8ull << 20) - 1;
  RecCtx pending = ctx();
  pending.files_n = 256;
  RecCtx bat = ctx();
  bat.bat_mv = 3450;
  RecCtx ids = ctx();
  ids.ids_ok = false;
  RecCtx mem = ctx();  // the SD card is there but the buffers could not be allocated
  mem.avail = false;
  mem.no_memory = true;
  for (const Case& k : {Case{RecRefusal::NoSd, no_sd}, Case{RecRefusal::NoMemory, mem}, Case{RecRefusal::CardFull, full},
                        Case{RecRefusal::Pending, pending}, Case{RecRefusal::Battery, bat}, Case{RecRefusal::NoIds, ids}}) {
    for (RecMode m : {RecMode::Memo, RecMode::Meeting}) {
      const RecStep st = rec_gesture(s, m, k.c);
      TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle);
      const RecEffect* r = find(st, RecFx::Refuse);
      TEST_ASSERT_TRUE(r && r->why == k.why);
      TEST_ASSERT_EQUAL_UINT8(1, st.effects.count);  // the message only: no sound
    }
  }
  RecCtx unknown = ctx();
  unknown.free_known = false;  // FSINFO unreadable: start anyway
  unknown.bat_mv = 0;          // battery unknown
  TEST_ASSERT_TRUE(rec_gesture(s, RecMode::Memo, unknown).state.phase == RecPhase::Cue);
  RecCtx low = ctx();
  low.free_bytes = 128ull << 20;
  const RecStep warn = rec_gesture(s, RecMode::Memo, low);
  TEST_ASSERT_TRUE(warn.state.phase == RecPhase::Cue && find(warn, RecFx::Warn) && find(warn, RecFx::Warn)->hours == 1);
  // the meeting is checked again on Enter
  RecStep st = rec_gesture(s, RecMode::Meeting, ctx(1000));
  RecCtx later = ctx(2000);
  later.avail = false;
  st = rec_input(st.state, RecInput::Confirm, 0, later);
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle && find(st, RecFx::Refuse));
  TEST_ASSERT_EQUAL_UINT8(1, st.effects.count);
}

static void test_only_idle_starts() {
  const RecCoreState s = recording(RecMode::Memo);
  TEST_ASSERT_EQUAL_UINT8(0, rec_gesture(s, RecMode::Meeting, ctx(9000)).effects.count);
  TEST_ASSERT_TRUE(rec_gesture(s, RecMode::Meeting, ctx(9000)).state.phase == RecPhase::Recording);
}

static void test_end_events() {
  for (EvtKind k : {EvtKind::Saved, EvtKind::Discarded, EvtKind::Failed}) {
    const RecStep st = rec_event(recording(RecMode::Meeting), evt(k), ctx(9000));
    TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle);
  }
  RecCoreState s = recording(RecMode::Meeting);
  s = rec_input(s, RecInput::End, 0, ctx(5000)).state;
  TEST_ASSERT_TRUE(rec_event(s, evt(EvtKind::Saved), ctx(5100)).state.phase == RecPhase::Idle);  // the cap
  RecEvt lvl = evt(EvtKind::Level);
  lvl.level = 812;
  TEST_ASSERT_EQUAL_UINT16(812, rec_event(s, lvl, ctx(5200)).state.level);
  RecCoreState idle;
  TEST_ASSERT_TRUE(rec_event(idle, evt(EvtKind::IndexReady), ctx()).state.ready);
}

static void test_caps_and_battery() {
  RecCoreState memo = recording(RecMode::Memo, 0);  // started at 900
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(memo, ctx(900 + 300000 + kCapGraceMs - 1)).effects.count);
  RecStep st = rec_tick(memo, ctx(900 + 300000 + kCapGraceMs));
  TEST_ASSERT_TRUE(has(st, RecFx::Stop) && st.state.phase == RecPhase::Finishing);
  RecCoreState meet = recording(RecMode::Meeting, 0);
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(meet, ctx(1000000)).effects.count);
  TEST_ASSERT_TRUE(has(rec_tick(meet, ctx(900 + 10800000 + kCapGraceMs)), RecFx::Stop));
  RecCtx low = ctx(5000);
  low.bat_mv = 3440;
  st = rec_tick(meet, low);
  TEST_ASSERT_FALSE(has(st, RecFx::Stop));
  low.now = 14999;
  st = rec_tick(st.state, low);
  TEST_ASSERT_FALSE(has(st, RecFx::Stop));
  RecCtx ok = ctx(15000);
  TEST_ASSERT_FALSE(has(rec_tick(st.state, ok), RecFx::Stop));  // recovered: the 10 s restart
  low.now = 15000;
  st = rec_tick(st.state, low);
  // docs/20 U6: an automatic stop is not a refusal ("can't record"): the reason rides along
  // to the end of the recording (test_auto_stop_reasons)
  TEST_ASSERT_TRUE(has(st, RecFx::Stop) && st.state.stop_why == RecRefusal::Battery);
  TEST_ASSERT_FALSE(has(st, RecFx::Refuse));
  TEST_ASSERT_EQUAL_UINT8(0, rec_tick(RecCoreState{}, low).effects.count);
}

// §4.7: leaving Idle answers every queued file.* with E_BUSY before Start; one request at a
// time goes to the audio task.
static void test_file_requests_and_mutual_exclusion() {
  RecCoreState s;
  s.ready = true;
  RecStep st = rec_file_queued(s);
  TEST_ASSERT_TRUE(st.state.at_task && st.state.queued == 0 && has(st, RecFx::PostNext));
  st = rec_file_queued(st.state);
  st = rec_file_queued(st.state);
  TEST_ASSERT_EQUAL_UINT8(2, st.state.queued);
  TEST_ASSERT_EQUAL_UINT8(3, rec_inflight(st.state));
  TEST_ASSERT_EQUAL_UINT8(0, st.effects.count);
  const RecStep memo = rec_gesture(st.state, RecMode::Memo, ctx());
  const std::vector<RecFx> order = kinds(memo);
  TEST_ASSERT_TRUE(order.size() >= 2 && order[0] == RecFx::FlushBusy && order[1] == RecFx::Start);
  TEST_ASSERT_EQUAL_UINT8(0, memo.state.queued);
  TEST_ASSERT_TRUE(memo.state.at_task);  // the one already taken finishes first
  st = rec_file_done(memo.state);
  TEST_ASSERT_FALSE(st.state.at_task);
  TEST_ASSERT_FALSE(has(st, RecFx::PostNext));
  // docs/20 D3: opening the meeting's box pauses nothing (test_rec_core_boxes: its Enter does)
  const RecStep meet = rec_gesture(rec_file_queued(rec_file_queued(RecCoreState{}).state).state, RecMode::Meeting, ctx());
  TEST_ASSERT_TRUE(!has(meet, RecFx::FlushBusy) && meet.state.queued == 1);
  RecCoreState two;
  two = rec_file_queued(two).state;
  two = rec_file_queued(two).state;
  st = rec_file_done(two);
  TEST_ASSERT_TRUE(has(st, RecFx::PostNext) && st.state.at_task && st.state.queued == 0);
}

static void test_cap_values() {
  TEST_ASSERT_EQUAL_UINT32(300000, rec_cap_ms(RecMode::Memo));
  TEST_ASSERT_EQUAL_UINT32(10800000, rec_cap_ms(RecMode::Meeting));
  TEST_ASSERT_TRUE(rec_refusal(ctx()) == RecRefusal::None);
}

// docs/11 §4.7: with the record-time allocation mode the ring is allocated at Start; a
// failed allocation ends the attempt with "Out of memory", on the screen only.
static void test_allocation_failure_at_start() {
  RecCoreState s;
  s.ready = true;
  RecStep st = rec_gesture(s, RecMode::Memo, ctx(1000));
  RecEvt f = evt(EvtKind::Failed);
  f.fail = RecFail::NoMemory;
  st = rec_event(st.state, f, ctx(1100));
  TEST_ASSERT_TRUE(st.state.phase == RecPhase::Idle);
  TEST_ASSERT_TRUE(find(st, RecFx::Refuse) && find(st, RecFx::Refuse)->why == RecRefusal::NoMemory);
  TEST_ASSERT_EQUAL_UINT8(1, st.effects.count);
  f.fail = RecFail::Disk;
  st = rec_event(rec_gesture(s, RecMode::Memo, ctx(2000)).state, f, ctx(2100));
  TEST_ASSERT_TRUE(find(st, RecFx::Refuse)->why == RecRefusal::CardError);
  f.fail = RecFail::Full;
  st = rec_event(rec_gesture(s, RecMode::Memo, ctx(3000)).state, f, ctx(3100));
  TEST_ASSERT_TRUE(find(st, RecFx::Refuse)->why == RecRefusal::CardFull);
}

// The link went away: the queued requests are void (no replies, the host asks again).
static void test_files_dropped_with_the_link() {
  RecCoreState s;
  s = rec_file_queued(s).state;
  s = rec_file_queued(s).state;
  s = rec_file_queued(s).state;
  const RecStep st = rec_files_dropped(s);
  TEST_ASSERT_EQUAL_UINT8(0, st.state.queued);
  TEST_ASSERT_TRUE(st.state.at_task);  // the audio task still answers that one
  TEST_ASSERT_EQUAL_UINT8(0, st.effects.count);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_memo_hold_record_release);
  RUN_TEST(test_release_in_the_cue_waits_for_the_grace);
  RUN_TEST(test_memo_release_grace_then_end);
  RUN_TEST(test_memo_re_press_in_the_grace_resumes);
  RUN_TEST(test_hands_free_memo);
  RUN_TEST(test_hold_makes_it_a_held_memo);
  RUN_TEST(test_only_a_memo_is_hands_free);
  RUN_TEST(test_cap_in_the_grace_and_hands_free);
  RUN_TEST(test_short_memo_still_discarded);
  RUN_TEST(test_meeting_needs_the_consent_enter);
  RUN_TEST(test_meeting_page_keys);
  RUN_TEST(test_refusals);
  RUN_TEST(test_only_idle_starts);
  RUN_TEST(test_end_events);
  RUN_TEST(test_caps_and_battery);
  RUN_TEST(test_file_requests_and_mutual_exclusion);
  RUN_TEST(test_cap_values);
  RUN_TEST(test_allocation_failure_at_start);
  RUN_TEST(test_files_dropped_with_the_link);
  return UNITY_END();
}
