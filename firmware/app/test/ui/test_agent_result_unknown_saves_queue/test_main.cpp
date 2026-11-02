// Online agent requests through the whole runtime (05 §5.6 "results of online requests",
// 04 US-09 AC4/AC6, E9/E10): ok -> "Started · <short>"; failure -> the text becomes a queue
// item and the code is shown; link loss or 30 s without a result -> "Unknown result", the text
// becomes an uncertain queue item. Never retried; a late result is ignored.
#include <unity.h>

#include "core/support/lang_pin.h"
#include "runtime.h"
#include "support/fake_canvas.h"
#include "support/fixtures.h"
#include "support/runtime_rig.h"

using namespace cpa;
using namespace uitest;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

// Opt+S, text, Enter, then Enter again after the 400 ms guard.
static void spawn(RuntimeRig& r, const char* text) {
  r.board.slot = 1;
  r.key(opt('s'), r.now);
  for (const char* p = text; *p; ++p) r.key(chr(*p), r.now);
  r.key(enter(), r.now);
  r.key(enter(), r.now + 400);
  r.step();
}

static std::vector<proto::Decoded> queued_items(RuntimeRig& r) {
  std::vector<proto::Decoded> out;
  proto::Codec codec;
  for (uint8_t i = 0; i < r.rt.outbox().n; ++i) {
    char buf[2100];
    const MonoId id = r.rt.outbox().ids[i];
    const std::string path = std::string("/outbox/") + mono_format(id).c_str() + ".json";
    const long n = r.fs.read(path.c_str(), buf, sizeof buf);
    const proto::Decoded& d = codec.decode(buf, n > 0 ? static_cast<size_t>(n) : 0);
    if (d.outcome == proto::Outcome::Ok) out.push_back(d);
  }
  return out;
}

static std::string screen(RuntimeRig& r) {
  r.disp.canvas_.clear();
  r.rt.shell().render(r.disp.canvas_, r.now);
  return r.disp.canvas_.dump();
}

static void test_spawn_goes_out_as_online_request_not_outbox() {
  RuntimeRig r;
  r.handshake();
  spawn(r, "write tests");
  TEST_ASSERT_EQUAL_UINT32(1, r.tx.count("agent.spawn"));
  const auto req = r.tx.of_type("agent.spawn").back();
  const auto& body = std::get<proto::AgentSpawn>(req.msg.body);
  TEST_ASSERT_TRUE(body.project.equals("cpa"));
  TEST_ASSERT_TRUE(body.text.equals("write tests"));
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.outbox().n);
  TEST_ASSERT_TRUE(r.rt.state().agent.phase == AgentPhase::Inflight);
}

static void test_ok_result_toasts_started_short() {
  RuntimeRig r;
  r.handshake();
  spawn(r, "write tests");
  r.reply("agent.result", r.last_id_of("agent.spawn"), "\"ok\":true,\"name\":\"cpa/write-tests\",\"short\":\"a1b2c3\"");
  r.step();
  TEST_ASSERT_TRUE(screen(r).find("已开始 · a1b2c3") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.outbox().n);
  TEST_ASSERT_TRUE(r.rt.state().agent.phase == AgentPhase::Idle);
}

static void test_failure_saves_plain_queue_item_and_shows_code() {
  RuntimeRig r;
  r.handshake();
  spawn(r, "write tests");
  r.reply("agent.result", r.last_id_of("agent.spawn"), "\"ok\":false,\"code\":\"E_SPAWN\"");
  r.step();
  TEST_ASSERT_TRUE(screen(r).find("E_SPAWN") != std::string::npos);
  const auto items = queued_items(r);
  TEST_ASSERT_EQUAL_UINT32(1, items.size());
  const auto& item = std::get<proto::OutboxItem>(items[0].msg.body);
  TEST_ASSERT_TRUE(item.kind == ItemKind::Queue);
  TEST_ASSERT_TRUE(item.queue.intent == Intent::Spawn);
  TEST_ASSERT_FALSE(item.queue.uncertain);
  TEST_ASSERT_TRUE(item.queue.text.equals("write tests"));
}

static void test_link_loss_saves_uncertain_queue_item() {
  RuntimeRig r;
  r.handshake();
  spawn(r, "write tests");
  r.tx.is_plugged = false;  // USB pulled before the result
  r.step();
  TEST_ASSERT_TRUE(r.rt.state().link.mode == LinkMode::Ready);  // not yet: debounced
  r.step(kUnplugConfirmMs);
  TEST_ASSERT_TRUE(r.rt.state().link.mode == LinkMode::Detached);
  TEST_ASSERT_TRUE(screen(r).find("结果未知") != std::string::npos);
  const auto items = queued_items(r);
  TEST_ASSERT_EQUAL_UINT32(1, items.size());
  const auto& item = std::get<proto::OutboxItem>(items[0].msg.body);
  TEST_ASSERT_TRUE(item.queue.uncertain);
  TEST_ASSERT_TRUE(item.queue.intent == Intent::Spawn);
  TEST_ASSERT_TRUE(item.queue.project.equals("cpa"));
  // the late result is ignored: nothing else is saved, no "started" toast
  r.tx.is_plugged = true;
  r.handshake();
  r.reply("agent.result", r.last_id_of("agent.spawn"), "\"ok\":true,\"short\":\"a1b2c3\"");
  r.step();
  TEST_ASSERT_TRUE(screen(r).find("已开始") == std::string::npos);
  TEST_ASSERT_EQUAL_UINT32(1, r.tx.count("agent.spawn"));  // never retried
}

// HIL 2026-10-19: a one-loop isPlugged() dropout while a spawn is in flight must not
// make it "result unknown" (the session may already run on the Mac).
static void test_plugged_dropout_keeps_spawn_in_flight() {
  RuntimeRig r;
  r.handshake();
  spawn(r, "write tests");
  r.tx.is_plugged = false;
  r.step();
  r.tx.is_plugged = true;
  r.step();
  TEST_ASSERT_TRUE(r.rt.state().agent.phase == AgentPhase::Inflight);
  r.reply("agent.result", r.last_id_of("agent.spawn"), "\"ok\":true,\"short\":\"a1b2c3\"");
  r.step();
  TEST_ASSERT_TRUE(screen(r).find("已开始 · a1b2c3") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.outbox().n);
}

static void test_30s_timeout_saves_uncertain_queue_item() {
  RuntimeRig r;
  r.handshake();
  spawn(r, "write tests");
  const uint32_t sent = r.rt.state().agent.sent_ms;
  for (int i = 0; i < 3; ++i) {  // keep the link alive with pings
    r.send("ping");
    r.step(9000);
  }
  TEST_ASSERT_TRUE(r.now - sent < 30000);
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.outbox().n);
  r.send("ping");
  r.step(30000 - (r.now - sent));
  TEST_ASSERT_TRUE(r.rt.state().link.mode == LinkMode::Ready);
  const auto items = queued_items(r);
  TEST_ASSERT_EQUAL_UINT32(1, items.size());
  TEST_ASSERT_TRUE(std::get<proto::OutboxItem>(items[0].msg.body).queue.uncertain);
}

static void test_daemon_restart_mid_request_is_unknown() {
  RuntimeRig r;
  r.handshake();
  spawn(r, "write tests");
  r.handshake();  // a new host hello while Ready
  TEST_ASSERT_EQUAL_UINT32(1, queued_items(r).size());
}

static void test_reply_draft_keeps_its_session() {
  RuntimeRig r;
  r.handshake(true);
  r.send("sessions.snapshot",
         "\"rev\":2,\"total\":1,\"sessions\":[{\"sid\":\"5c2e8a14\",\"slot\":1,\"name\":\"chord\",\"kind\":\"bg\","
         "\"st\":\"idle\",\"need\":false,\"age_s\":5,\"short\":\"a1b2c3\"}]");
  r.step();
  ComposeSpec fb;
  fb.kind = ComposeKind::Feedback;
  fb.slot = 1;
  fb.sid = some(Sid::from("5c2e8a14"));
  r.rt.shell().compose(fb);
  r.key(chr('k'), r.now);
  r.key(enter(), r.now);
  r.key(enter(), r.now + 400);
  r.step();
  TEST_ASSERT_EQUAL_UINT32(1, r.tx.count("agent.reply"));
  r.unplug();
  const auto items = queued_items(r);
  TEST_ASSERT_EQUAL_UINT32(1, items.size());
  const auto& q = std::get<proto::OutboxItem>(items[0].msg.body).queue;
  TEST_ASSERT_TRUE(q.intent == Intent::Feedback);
  TEST_ASSERT_TRUE(q.sid.has && q.sid.value.equals("5c2e8a14"));
  TEST_ASSERT_TRUE(q.uncertain);
}

static void test_spawn_from_queue_item_is_not_saved_again() {
  RuntimeRig r;
  r.handshake();
  ConfirmSpec c;
  c.kind = ConfirmKind::Spawn;
  c.slot = 1;
  c.qid = some(Qid::from("q17"));
  c.text = UserText::from("add LittleFS fault-injection test");
  r.rt.shell().confirm(c);
  r.key(enter(), r.now + 400);
  r.step();
  const auto req = r.tx.of_type("agent.spawn");
  TEST_ASSERT_EQUAL_UINT32(1, req.size());
  TEST_ASSERT_TRUE(std::get<proto::AgentSpawn>(req[0].msg.body).qid.value.equals("q17"));
  r.unplug();
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.outbox().n);  // the item is still on the Mac
  TEST_ASSERT_TRUE(screen(r).find("结果未知") != std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_spawn_goes_out_as_online_request_not_outbox);
  RUN_TEST(test_ok_result_toasts_started_short);
  RUN_TEST(test_failure_saves_plain_queue_item_and_shows_code);
  RUN_TEST(test_link_loss_saves_uncertain_queue_item);
  RUN_TEST(test_plugged_dropout_keeps_spawn_in_flight);
  RUN_TEST(test_30s_timeout_saves_uncertain_queue_item);
  RUN_TEST(test_daemon_restart_mid_request_is_unknown);
  RUN_TEST(test_reply_draft_keeps_its_session);
  RUN_TEST(test_spawn_from_queue_item_is_not_saved_again);
  return UNITY_END();
}
