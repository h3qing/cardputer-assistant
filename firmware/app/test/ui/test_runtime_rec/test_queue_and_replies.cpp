// runtime_rec: the command queue to the audio task and the file.* replies under pressure.
// A Stop is never made up while the task may still record, stars and tags leave the last
// slot to the Stop, and every file.* request gets exactly one reply with its own re.
#include <unity.h>

#include <string>

#include "rec_engine.h"
#include "runtime.h"
#include "support/fixtures.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;
using rec::CmdKind;

// A full command queue at the release: the Stop is posted again on every tick until the
// task takes it. A made-up end would hide a recording that goes on (no ● REC) and let
// file.* and a second Start in while the card is still written.
void test_a_refused_stop_is_retried_never_made_up() {
  RecRig x;
  x.hold_g0(600);
  x.port.feed(2000);
  x.port.accept = false;
  x.let_go();
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Finishing);
  TEST_ASSERT_TRUE(x.port.engine->recording());
  x.r.send("ping");
  x.r.step();
  TEST_ASSERT_TRUE(std::get<proto::Pong>(x.last("pong").msg.body).files.value.live);
  x.r.send("file.list");
  x.r.step();
  TEST_ASSERT_TRUE(x.last_error().code == ErrCode::Busy && x.last_error().msg.value.equals("live"));
  x.hold_g0(600);  // no second session while the first one is still written
  x.let_go();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Finishing);
  x.port.accept = true;
  x.r.step();
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Stop));
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Start));
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.files_n);
  TEST_ASSERT_TRUE(x.port.heard.back() == rec::EvtKind::Saved);
  x.steps();
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Stop));  // posted once it was taken
}

// A Start the full queue refused never reached the task: the session fails at once, and
// nothing is left to stop.
void test_a_refused_start_fails_at_once() {
  RecRig x;
  x.port.accept = false;
  x.hold_g0(600);
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Idle);
  x.port.accept = true;
  x.let_go();
  x.steps();
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Start));
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Stop));
  x.memo(2000);  // the next one works
  TEST_ASSERT_EQUAL_UINT16(1, x.r.rt.state().rec.files_n);
}

// Stars and tags never take the last free slot of the command queue: the Stop needs it.
void test_stars_and_tags_leave_room_for_the_stop() {
  RecRig x;
  x.r.step(10, KeySet{}.with(key::kOpt).with(layout_index(4, 1)));  // Opt+R
  x.r.step(500);
  x.r.step(10, KeySet{}.with(key::kEnter));
  x.steps();
  TEST_ASSERT_TRUE(x.r.rt.state().rec.phase == RecPhase::Recording);
  x.port.room_left = 1;
  x.r.step(10, KeySet{}.with(key::kSpace));  // a star
  x.steps();
  x.r.step(10, KeySet{}.with(layout_index(1, 0)));  // tag 1
  x.steps();
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Mark));
  TEST_ASSERT_EQUAL_UINT(0, x.port.count(CmdKind::Tag));
  x.port.room_left = 2;
  x.r.step(10, KeySet{}.with(key::kSpace));
  x.steps();
  TEST_ASSERT_EQUAL_UINT(1, x.port.count(CmdKind::Mark));
}

// An empty file.list page while a file.get's data waits for TX room: each request gets
// exactly one reply, with its own re (docs/11 §5.4).
void test_an_empty_page_keeps_the_held_reply_its_own() {
  RecRig x;
  x.memo(3000);
  const std::string fid = mono_format(x.port.idx.at(0).id).c_str();
  x.r.plat.room = 0;
  const std::string get_id = x.next_host_id();
  x.r.send("file.get", "\"fid\":\"" + fid + "\",\"off\":0,\"len\":1380");
  x.steps();
  const std::string list_id = x.next_host_id();
  x.r.send("file.list", "\"after\":\"" + fid + "\",\"max\":4");
  x.steps();
  x.r.plat.room = 4096;
  x.steps();
  const auto items = x.r.tx.of_type("file.items");
  const auto data = x.r.tx.of_type("file.data");
  TEST_ASSERT_EQUAL_UINT(1, items.size());
  TEST_ASSERT_EQUAL_UINT(1, data.size());
  TEST_ASSERT_EQUAL_STRING(list_id.c_str(), items[0].msg.re.value.c_str());
  TEST_ASSERT_EQUAL_UINT8(0, std::get<proto::FileItems>(items[0].msg.body).n);
  TEST_ASSERT_EQUAL_STRING(get_id.c_str(), data[0].msg.re.value.c_str());
  TEST_ASSERT_EQUAL_UINT(0, x.r.tx.count("error"));
}
