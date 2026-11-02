// runtime_rec: the muted start of issue #31 through the real keys. The audio task arms the
// engine (FakeRecPort.gated): while the codec is still muted the memo stays in the Cue
// ("● Wait"), ● REC comes with the first live block, and letting go of G0 before that
// discards the memo (B5).
#include <unity.h>

#include <string>

#include "rec_engine.h"
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;
using rec::CmdKind;

void test_rec_comes_with_the_first_live_block() {
  RecRig x;
  x.port.gated = true;
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Cue);
  x.port.feed_muted(896);  // 7 blocks of a cold codec
  x.hold_g0(30);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Cue);
  x.port.feed(128);
  x.hold_g0(30);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  x.port.feed(2000);
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.files_n);
  const rec::RecIndex& idx = x.port.idx;
  const std::string meta = x.port.fs.get(rec::rec_meta_path("/cpa", idx.at(0).id).c_str());
  TEST_ASSERT_TRUE(meta.find("\"bytes\":69632") != std::string::npos);  // 17 live blocks, no muted one
}

void test_release_while_the_codec_is_muted_discards() {
  RecRig x;
  x.port.gated = true;
  const size_t files_before = x.port.fs.file_count();
  x.hold_g0(600);
  x.port.feed_muted(512);
  x.let_go();
  x.steps();
  TEST_ASSERT_EQUAL_UINT(2, x.port.cmds.size());  // the Start and the Stop
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Discarded);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT(files_before, x.port.fs.file_count());
  TEST_ASSERT_FALSE(x.port.engine->recording());
}
