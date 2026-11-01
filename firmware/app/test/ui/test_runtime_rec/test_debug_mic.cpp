// runtime_rec: debug.mic (v0.3.1) and debug.codec (v0.3.2), test firmware only. The gain goes
// to the audio task as a MicGain command and is acked; the task uses it from the next Start
// on, so a recording already running goes on as it started (no second Start, no Stop) and
// saves as usual. debug.codec is a Codec command (issue #31), refused while live.
#include <unity.h>

#include <string>

#include "mic_gain.h"
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;
using rec::CmdKind;

void test_debug_mic_posts_the_gain_and_acks() {
  RecRig x;
  const std::string id = x.next_host_id();
  x.r.send("debug.mic", "\"pga\":10,\"vol\":197,\"mag\":2");
  x.r.step();
  const rec::RecCmd& c = x.port.cmds.back();
  TEST_ASSERT_TRUE(c.kind == CmdKind::MicGain);
  TEST_ASSERT_TRUE(c.gain == (MicGain{10, 197, 2}));
  TEST_ASSERT_TRUE(x.last("ack").msg.re.value.equals(id.c_str()));
  TEST_ASSERT_EQUAL_UINT(0, x.r.tx.count("error"));
}

void test_debug_mic_while_recording_leaves_the_session_alone() {
  RecRig x;
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  TEST_ASSERT_TRUE(x.port.cmds.back().kind == CmdKind::Start);
  const rec::StartSpec spec = x.port.cmds.back().start;
  const std::string id = x.next_host_id();
  x.r.send("debug.mic", "\"pga\":9,\"vol\":197,\"mag\":2");
  x.r.step(10, KeySet{}.with(key::kG0));
  TEST_ASSERT_TRUE(x.port.cmds.back().kind == CmdKind::MicGain);
  TEST_ASSERT_TRUE(x.port.cmds.back().gain == (MicGain{9, 197, 2}));
  TEST_ASSERT_TRUE(x.last("ack").msg.re.value.equals(id.c_str()));
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Stop));
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.mode == spec.mode);
  x.port.feed(2000);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.files_n);  // saved as a normal memo
}

// debug.codec (v0.3.2, issue #31): a Codec command for the audio task, acked; while a
// recording is live (the Cue included) it is refused with E_BUSY "live" and nothing is posted.
void test_debug_codec_posts_the_reset_and_acks() {
  RecRig x;
  const std::string id = x.next_host_id();
  x.r.send("debug.codec", "\"warm\":true");
  x.r.step();
  const rec::RecCmd& c = x.port.cmds.back();
  TEST_ASSERT_TRUE(c.kind == CmdKind::Codec && c.warm);
  TEST_ASSERT_TRUE(x.last("ack").msg.re.value.equals(id.c_str()));
  x.r.send("debug.codec", "\"warm\":false");
  x.r.step();
  TEST_ASSERT_TRUE(x.port.cmds.back().kind == CmdKind::Codec && !x.port.cmds.back().warm);
  TEST_ASSERT_EQUAL_UINT(0, x.r.tx.count("error"));
}

void test_debug_codec_while_live_is_busy() {
  RecRig x;
  x.port.hold_start = true;  // still in the Cue
  x.hold_g0(520);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Cue);
  const size_t posted = x.port.cmds.size();
  const std::string id = x.next_host_id();
  x.r.send("debug.codec", "\"warm\":false");
  x.r.step(10, KeySet{}.with(key::kG0));
  const proto::ErrorMsg e = x.last_error();
  TEST_ASSERT_TRUE(e.code == ErrCode::Busy && e.msg.has && e.msg.value.equals("live"));
  TEST_ASSERT_TRUE(x.last("error").msg.re.value.equals(id.c_str()));
  TEST_ASSERT_EQUAL_UINT(posted, x.port.cmds.size());
}
