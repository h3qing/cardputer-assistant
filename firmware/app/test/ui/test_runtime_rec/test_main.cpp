// runtime_rec (docs/11 §4.7, §5): the loop's bridge between RecCore, the audio task
// (FakeRecPort: a real RecEngine on the fake card) and the link. Recording is started and
// stopped with real keys; file.* is answered as §5.4 says, file.* is refused with E_BUSY
// "live" while recording, file.data waits for room in the TX ring and never goes through
// the 4-line LineQueue, and a device without the SD card sends exactly the v0.2 lines.
#include <unity.h>

#include <string>

#include "rec_engine.h"
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;
using rec::CmdKind;

void setUp() {}
void tearDown() {}

static std::string files_of(const std::string& line) {
  const auto at = line.find("\"files\"");
  return at == std::string::npos ? std::string() : line.substr(at);
}

// docs/11 §4.8: no card, or the internal store: no files field, the same bytes as v0.2.
static void test_without_the_card_hello_and_pong_are_v02() {
  RuntimeRig plain;  // v0.2 wiring: nothing attached
  plain.handshake();
  plain.send("ping");
  plain.step();
  RecRig internal(false);
  internal.r.send("ping");
  internal.r.step();
  for (RuntimeRig* rig : {&plain, &internal.r}) {
    for (const auto& line : rig->tx.sent) TEST_ASSERT_TRUE_MESSAGE(files_of(line).empty(), line.c_str());
  }
  const auto a = plain.tx.of_type("hello")[0];
  const auto b = internal.r.tx.of_type("hello")[0];
  TEST_ASSERT_TRUE(!std::get<proto::Hello>(a.msg.body).device.files.has);
  TEST_ASSERT_TRUE(!std::get<proto::Hello>(b.msg.body).device.files.has);
}

// A refusal is the screen's alone (v0.3.1): nothing at all goes to the audio task.
static void test_nosd_refuses_silently() {
  RecRig x(false);
  x.hold_g0(600);
  x.let_go();
  x.steps();
  TEST_ASSERT_EQUAL_UINT(0, x.port.cmds.size());
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  x.r.step(10, KeySet{}.with(key::kOpt).with(layout_index(4, 1)));  // Opt+R
  x.steps();
  TEST_ASSERT_EQUAL_UINT(0, x.port.cmds.size());
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
}

// A refusal at the consent Enter, and a failure while recording: the runtime posts
// nothing for either; the session's own Start is the only command.
static void test_refusal_at_enter_and_failure_post_nothing() {
  RecRig x;
  x.r.step(10, KeySet{}.with(key::kOpt).with(layout_index(4, 1)));  // Opt+R
  x.r.plat.next.bat_mv = 3400;  // sampled within the second: checked again on Enter
  x.r.step(1000);
  x.r.step(10, KeySet{}.with(key::kEnter));
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT(0, x.port.cmds.size());
  x.r.plat.next.bat_mv = 4000;
  x.r.step(1000);
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  x.port.fs.fail_next(rec::FsErr::Disk);
  x.port.feed(1000);
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Failed);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT(1, x.port.cmds.size());
  TEST_ASSERT_TRUE(x.port.cmds[0].kind == CmdKind::Start);
}

static void test_files_on_hello_and_pong() {
  RecRig x;
  const auto h = std::get<proto::Hello>(x.last("hello").msg.body);
  TEST_ASSERT_TRUE(h.device.files.has && h.device.files.value.n == 0 && h.device.files.value.rev == 1);
  TEST_ASSERT_FALSE(h.device.files.value.live);
  x.memo(3000);
  x.r.send("ping");
  x.r.step();
  const auto p = std::get<proto::Pong>(x.last("pong").msg.body);
  TEST_ASSERT_TRUE(p.files.has && p.files.value.n == 1 && p.files.value.rev == 2 && !p.files.value.live);
}

static void test_rev_is_zero_until_the_scan_finished() {
  RecRig x(true, false);
  const auto h = std::get<proto::Hello>(x.last("hello").msg.body);
  TEST_ASSERT_TRUE(h.device.files.has && h.device.files.value.rev == 0);
  x.r.send("file.list");
  x.r.step();
  TEST_ASSERT_TRUE(x.last_error().code == ErrCode::NotReady);
  x.port.finish_scan();
  x.steps();
  x.r.send("file.list");
  x.r.step();
  TEST_ASSERT_EQUAL_UINT(1, x.r.tx.count("file.items"));
}

static void test_memo_by_keys_end_to_end() {
  RecRig x;
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  const rec::RecCmd& start = x.port.cmds.back();
  TEST_ASSERT_TRUE(start.kind == CmdKind::Start && start.start.mode == RecMode::Memo && !start.start.consent);
  x.port.feed(2000);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.files_n);
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  // too short: discarded, nothing kept
  x.hold_g0(600);
  x.port.feed(500);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Discarded);
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.files_n);
  // only the sessions' own commands went to the audio task
  TEST_ASSERT_EQUAL_UINT(4, x.port.cmds.size());
  TEST_ASSERT_EQUAL_UINT(2, x.port.count(CmdKind::Start));
  TEST_ASSERT_EQUAL_UINT(2, x.port.count(CmdKind::Stop));
}

// B5 (docs/11 §4.1): released in the cue, before the microphone came up: stopped at
// once, discarded, nothing written.
static void test_release_during_the_cue_discards() {
  RecRig x;
  const size_t files_before = x.port.fs.file_count();
  x.port.hold_start = true;
  x.hold_g0(520);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Cue);
  x.let_go();
  x.steps();
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Stop));
  TEST_ASSERT_EQUAL_UINT(2, x.port.cmds.size());  // the Start and the Stop, nothing else
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Discarded);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT16(0, x.r.rt.state().rec.files_n);
  TEST_ASSERT_EQUAL_UINT(files_before, x.port.fs.file_count());
}

static void test_meeting_consent_and_end_confirm() {
  RecRig x;
  const KeySet opt_r = KeySet{}.with(key::kOpt).with(layout_index(4, 1));
  const KeySet enter = KeySet{}.with(key::kEnter);
  x.r.step(10, opt_r);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Confirm);
  x.r.step(10, enter);  // within 400 ms: ignored
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Confirm);
  x.r.step(400);
  x.r.step(10, enter);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  TEST_ASSERT_TRUE(x.port.cmds.back().kind == CmdKind::Start && x.port.cmds.back().start.consent);
  x.port.feed(3000);
  x.r.step(10, KeySet{}.with(key::kSpace));  // a star
  x.steps();
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Mark));
  x.r.step(10, opt_r);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::EndConfirm);
  x.r.step(400);
  x.r.step(10, enter);
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  // the .M of segment 0 carries the consent
  const rec::RecIndex& idx = x.port.idx;
  const std::string meta = x.port.fs.get(rec::rec_meta_path("/cpa", idx.at(0).id).c_str());
  TEST_ASSERT_TRUE(meta.find("\"consent\":\"ack\"") != std::string::npos);
  TEST_ASSERT_TRUE(meta.find("\"stars\":[") != std::string::npos);
}

static void test_pull_and_delete() {
  RecRig x;
  x.memo(3000);
  x.r.send("file.list", "\"max\":4");
  x.r.step();
  const auto items = std::get<proto::FileItems>(x.last("file.items").msg.body);
  TEST_ASSERT_TRUE(items.n == 1 && !items.more);
  const proto::FileItem it = items.items[0];
  TEST_ASSERT_TRUE(it.mode == RecMode::Memo && it.last && it.sha256.has && it.lead_ms.has);
  std::string got;
  for (uint32_t off = 0; off < it.size; off += 1380) {
    x.r.send("file.get", "\"fid\":\"" + std::string(it.id.c_str()) + "\",\"off\":" + std::to_string(off) + ",\"len\":1380");
    x.r.step();
    const auto d = std::get<proto::FileData>(x.last("file.data").msg.body);
    TEST_ASSERT_EQUAL_UINT32(off, d.off);
    got.append(reinterpret_cast<const char*>(d.bytes), d.n);
  }
  const MonoId id = mono_parse(it.id.c_str(), it.id.size()).value;
  TEST_ASSERT_TRUE(got == x.port.fs.get(rec::rec_wav_path("/cpa", id).c_str()));
  x.r.send("file.get", "\"fid\":\"" + std::string(it.id.c_str()) + "\",\"off\":" + std::to_string(it.size) + ",\"len\":10");
  x.r.step();
  TEST_ASSERT_EQUAL_UINT16(0, std::get<proto::FileData>(x.last("file.data").msg.body).n);
  x.r.send("file.del", "\"fid\":\"" + std::string(it.id.c_str()) + "\",\"size\":" + std::to_string(it.size - 2));
  x.r.step();
  TEST_ASSERT_TRUE(x.last_error().code == ErrCode::BadField && x.last_error().field.value.equals("size"));
  x.r.send("file.del", "\"fid\":\"" + std::string(it.id.c_str()) + "\",\"size\":" + std::to_string(it.size) +
                           ",\"sha256\":\"" + it.sha256.value.c_str() + "\"");
  x.r.step();
  TEST_ASSERT_EQUAL_UINT16(0, x.r.rt.state().rec.files_n);
  // docs/28 S1a: handed over, the segment is kept (its .W, .M and one DELIV in SENT.L)
  TEST_ASSERT_EQUAL_UINT(3, x.port.fs.file_count());
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.kept.segs);
  const size_t acks = x.r.tx.count("ack");
  x.r.send("file.del", "\"fid\":\"" + std::string(it.id.c_str()) + "\",\"size\":" + std::to_string(it.size));
  x.r.step();
  TEST_ASSERT_EQUAL_UINT(acks + 1, x.r.tx.count("ack"));  // idempotent
  x.r.send("file.get", "\"fid\":\"" + std::string(it.id.c_str()) + "\",\"off\":0,\"len\":10");
  x.r.step();
  TEST_ASSERT_TRUE(x.last_error().code == ErrCode::UnknownItem);
  TEST_ASSERT_TRUE(x.r.tx.all_valid());
}

// #43: every file.get of a pull moves the link's last_rx_ms. That alone redraws nothing
// (a frame every 50 ms held back the replies: 336 -> 263 KB/s on the home page); the
// slice still holds the time, for the system page's age at the once-a-second frame.
static void test_a_pull_does_not_redraw_per_chunk() {
  RecRig x;
  x.memo(3000);
  x.r.send("file.list", "\"max\":4");
  x.r.step();
  const std::string fid = std::get<proto::FileItems>(x.last("file.items").msg.body).items[0].id.c_str();
  x.r.step(60);  // the list's frame, if any
  const int frames = x.r.disp.frames;
  for (int i = 0; i < 200; ++i) {  // 2 s of chunks, one every 10 ms
    x.r.send("file.get", "\"fid\":\"" + fid + "\",\"off\":0,\"len\":1380");
    x.r.step();
  }
  TEST_ASSERT_EQUAL_UINT(200, x.r.tx.count("file.data"));
  TEST_ASSERT_LESS_OR_EQUAL_INT(frames + 3, x.r.disp.frames);  // about one a second, not 40
  TEST_ASSERT_EQUAL_UINT32(x.r.now, x.r.rt.state().link.last_rx_ms);
}

// docs/11 §4.7: while the state machine is not Idle every file.* is E_BUSY "live"; the
// ones still queued when a recording starts are answered before Start is posted.
static void test_busy_while_live_and_flush_before_start() {
  RecRig x;
  x.memo(3000);
  x.port.hold_files = true;
  const MonoId id = x.port.idx.at(0).id;
  const std::string fid = mono_format(id).c_str();
  for (int i = 0; i < 3; ++i) x.r.send("file.get", "\"fid\":\"" + fid + "\",\"off\":0,\"len\":100");
  x.r.step();
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Get));  // one at the task, two held
  x.r.send("file.get", "\"fid\":\"" + fid + "\",\"off\":0,\"len\":100");
  x.r.send("file.get", "\"fid\":\"" + fid + "\",\"off\":0,\"len\":100");
  x.r.step();
  TEST_ASSERT_TRUE(x.last_error().code == ErrCode::Busy && x.last_error().msg.value.equals("full"));
  const size_t errors_before = x.r.tx.count("error");
  x.hold_g0(600);
  TEST_ASSERT_EQUAL_UINT(errors_before + 3, x.r.tx.count("error"));  // the three queued
  const size_t start_at = x.port.cmds.size();
  TEST_ASSERT_TRUE(x.port.cmds.back().kind == CmdKind::Start);
  x.r.send("file.list");
  x.r.step(10, KeySet{}.with(key::kG0));
  TEST_ASSERT_TRUE(x.last_error().code == ErrCode::Busy && x.last_error().msg.value.equals("live"));
  TEST_ASSERT_EQUAL_UINT(start_at, x.port.cmds.size());  // nothing after Start
  x.port.release_files();  // the one taken before Start still completes
  x.r.step(10, KeySet{}.with(key::kG0));
  TEST_ASSERT_EQUAL_UINT(1, x.r.tx.count("file.data"));
  x.r.send("ping");
  x.r.step(10, KeySet{}.with(key::kG0));
  TEST_ASSERT_TRUE(std::get<proto::Pong>(x.last("pong").msg.body).files.value.live);
}

static void test_file_data_waits_for_room_in_the_tx_ring() {
  RecRig x;
  x.memo(3000);
  const std::string fid = mono_format(x.port.idx.at(0).id).c_str();
  x.r.plat.room = 2047;
  x.r.send("file.get", "\"fid\":\"" + fid + "\",\"off\":0,\"len\":1380");
  x.steps(5);
  TEST_ASSERT_EQUAL_UINT(0, x.r.tx.count("file.data"));
  x.r.plat.room = 4096;
  x.r.step();
  TEST_ASSERT_EQUAL_UINT(1, x.r.tx.count("file.data"));
  TEST_ASSERT_TRUE(x.r.tx.classes.back() == TxClass::Reply);
}

static void test_debug_rec_and_state() {
  RecRig x;
  x.r.send("debug.rec", "\"seg_ms\":30000");
  x.r.step();
  TEST_ASSERT_TRUE(x.port.cmds.back().kind == CmdKind::SegMs && x.port.cmds.back().seg_ms == 30000);
  x.hold_g0(600);
  x.r.send("debug.state");
  x.r.step(10, KeySet{}.with(key::kG0));
  const auto s = std::get<proto::DebugState>(x.last("debug.state").msg.body);
  TEST_ASSERT_TRUE(s.rec.has && s.rec.value.live && s.rec.value.mode.value == RecMode::Memo && s.rec.value.seg.value == 0);
  x.port.feed(2000);
  x.let_go();
  x.steps();
  x.r.send("debug.state");
  x.r.step();
  const auto idle = std::get<proto::DebugState>(x.last("debug.state").msg.body);
  TEST_ASSERT_TRUE(idle.rec.has && !idle.rec.value.live && !idle.rec.value.mode.has && idle.rec.value.files_n == 1);
}

// docs/11 §4.2: on battery, 60 s without a key dims the screen while recording (never
// off), ● stays drawn, and keys on the dimmed screen work.
static void test_dim_not_off_while_recording() {
  RecRig x;
  x.r.tx.is_plugged = false;
  x.r.step();
  x.r.step(kUnplugConfirmMs);
  x.r.step(10, KeySet{}.with(key::kOpt).with(layout_index(4, 1)));
  x.r.step(500);
  x.r.step(10, KeySet{}.with(key::kEnter));
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  x.r.step(61000);
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::Dim);
  const TextOp* dot = x.r.disp.canvas_.find("●");  // the status bar's ●mm:ss, and the page's big ●
  TEST_ASSERT_TRUE(dot != nullptr && dot->style.fg == Color::Rec && !x.r.disp.canvas_.dots.empty());
  x.r.step(10, KeySet{}.with(key::kSpace));  // not swallowed: a star
  x.steps();
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Mark));
  TEST_ASSERT_TRUE(x.r.plat.levels.back() == Backlight::On);
}

// A file.data waiting for TX room must not hold up the audio task's other events, and a
// lost link drops it (the host asks again after the new handshake).
static void test_held_data_does_not_block_recording_events() {
  RecRig x;
  x.memo(3000);
  const std::string fid = mono_format(x.port.idx.at(0).id).c_str();
  x.r.plat.room = 0;  // the host stopped reading
  x.r.send("file.get", "\"fid\":\"" + fid + "\",\"off\":0,\"len\":1380");
  x.steps();
  TEST_ASSERT_EQUAL_UINT(0, x.r.tx.count("file.data"));
  x.r.unplug();
  x.hold_g0(600);  // a memo while the reply still waits
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  x.port.feed(2000);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT16(2, x.r.rt.state().rec.files_n);
  x.r.plat.room = 4096;
  x.r.tx.is_plugged = true;
  x.steps();
  TEST_ASSERT_EQUAL_UINT(0, x.r.tx.count("file.data"));  // dropped with the link
  x.r.handshake();
  x.r.send("file.get", "\"fid\":\"" + fid + "\",\"off\":0,\"len\":1380");
  x.r.step();
  TEST_ASSERT_EQUAL_UINT(1, x.r.tx.count("file.data"));  // the transfer works again
}

// test_queue_and_replies.cpp
void test_a_refused_stop_is_retried_never_made_up();
void test_a_refused_start_fails_at_once();
void test_stars_and_tags_leave_room_for_the_stop();
void test_an_empty_page_keeps_the_held_reply_its_own();

// test_debug_mic.cpp
void test_debug_mic_posts_the_gain_and_acks();
void test_debug_mic_while_recording_leaves_the_session_alone();
void test_debug_codec_posts_the_reset_and_acks();
void test_debug_codec_while_live_is_busy();

// test_memo_grace.cpp (gh #51)
void test_a_slip_within_the_grace_is_one_memo();
void test_a_release_in_the_cue_and_the_late_path();
void test_a_double_tap_records_hands_free();
void test_a_single_tap_still_opens_the_note();
void test_tap_then_hold_is_a_held_memo();
void test_a_hands_free_memo_locks_and_goes_on();
void test_a_hands_free_memo_stops_at_the_cap();
// test_memo_review.cpp (gh #51, its review)
void test_a_menu_hands_free_memo_at_the_cap_is_not_held();
void test_a_double_tap_to_end_opens_no_note();
void test_a_long_pass_in_the_grace_keeps_the_memo();
void test_a_slow_double_tap_records_hands_free();
void test_a_slow_tap_then_hold_records();
void test_a_locked_end_says_so_on_the_card();
// test_muted_start.cpp
void test_rec_comes_with_the_first_live_block();
void test_release_while_the_codec_is_muted_discards();

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_without_the_card_hello_and_pong_are_v02);
  RUN_TEST(test_nosd_refuses_silently);
  RUN_TEST(test_refusal_at_enter_and_failure_post_nothing);
  RUN_TEST(test_files_on_hello_and_pong);
  RUN_TEST(test_rev_is_zero_until_the_scan_finished);
  RUN_TEST(test_memo_by_keys_end_to_end);
  RUN_TEST(test_release_during_the_cue_discards);
  RUN_TEST(test_meeting_consent_and_end_confirm);
  RUN_TEST(test_pull_and_delete);
  RUN_TEST(test_a_pull_does_not_redraw_per_chunk);
  RUN_TEST(test_busy_while_live_and_flush_before_start);
  RUN_TEST(test_file_data_waits_for_room_in_the_tx_ring);
  RUN_TEST(test_debug_rec_and_state);
  RUN_TEST(test_dim_not_off_while_recording);
  RUN_TEST(test_held_data_does_not_block_recording_events);
  RUN_TEST(test_a_refused_stop_is_retried_never_made_up);
  RUN_TEST(test_a_refused_start_fails_at_once);
  RUN_TEST(test_stars_and_tags_leave_room_for_the_stop);
  RUN_TEST(test_an_empty_page_keeps_the_held_reply_its_own);
  RUN_TEST(test_debug_mic_posts_the_gain_and_acks);
  RUN_TEST(test_debug_mic_while_recording_leaves_the_session_alone);
  RUN_TEST(test_debug_codec_posts_the_reset_and_acks);
  RUN_TEST(test_debug_codec_while_live_is_busy);
  RUN_TEST(test_rec_comes_with_the_first_live_block);
  RUN_TEST(test_release_while_the_codec_is_muted_discards);
  RUN_TEST(test_a_slip_within_the_grace_is_one_memo);
  RUN_TEST(test_a_release_in_the_cue_and_the_late_path);
  RUN_TEST(test_a_double_tap_records_hands_free);
  RUN_TEST(test_a_single_tap_still_opens_the_note);
  RUN_TEST(test_tap_then_hold_is_a_held_memo);
  RUN_TEST(test_a_hands_free_memo_locks_and_goes_on);
  RUN_TEST(test_a_hands_free_memo_stops_at_the_cap);
  RUN_TEST(test_a_menu_hands_free_memo_at_the_cap_is_not_held);
  RUN_TEST(test_a_double_tap_to_end_opens_no_note);
  RUN_TEST(test_a_long_pass_in_the_grace_keeps_the_memo);
  RUN_TEST(test_a_slow_double_tap_records_hands_free);
  RUN_TEST(test_a_slow_tap_then_hold_records);
  RUN_TEST(test_a_locked_end_says_so_on_the_card);
  return UNITY_END();
}
