// Online agent requests (05 §5.6, §6.3): at most one in flight, never retried. A
// result after 30 s or after a link loss is "unknown": the text becomes an
// uncertain queue item. A failure saves it as a plain queue item. Late results
// for an abandoned request are ignored.
#include <unity.h>

#include "agent_req.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static AgentDraft spawn_draft() {
  AgentDraft d;
  d.kind = AgentKind::Spawn;
  d.project = ProjectKey::from("cpa");
  d.text = UserText::from("Write native tests for the chord detector");
  return d;
}

static AgentStep sent_at(uint32_t now) { return agent_send(AgentReqInfo{}, spawn_draft(), MsgId::from("d50"), now); }

static void test_send_goes_in_flight() {
  const auto s = sent_at(1000);
  TEST_ASSERT_TRUE(s.state.phase == AgentPhase::Inflight);
  TEST_ASSERT_TRUE(s.state.rid.equals("d50"));
  TEST_ASSERT_EQUAL_UINT8(1, s.effects.count);
  TEST_ASSERT_TRUE(s.effects.items[0].kind == AgentEventKind::Send);
  TEST_ASSERT_EQUAL_UINT8(1, agent_inflight(s.state));
}

static void test_second_request_is_busy() {
  const auto s = sent_at(0);
  const auto again = agent_send(s.state, spawn_draft(), MsgId::from("d51"), 10);
  TEST_ASSERT_TRUE(again.state.rid.equals("d50"));
  TEST_ASSERT_EQUAL_UINT8(1, again.effects.count);
  TEST_ASSERT_TRUE(again.effects.items[0].kind == AgentEventKind::Busy);
}

static void test_timeout_boundary_is_30s() {
  const auto s = sent_at(0);
  const auto before = agent_tick(s.state, 29999);
  TEST_ASSERT_TRUE(before.state.phase == AgentPhase::Inflight);
  TEST_ASSERT_EQUAL_UINT8(0, before.effects.count);
  const auto after = agent_tick(before.state, 30000);
  TEST_ASSERT_TRUE(after.state.phase == AgentPhase::Idle);
  TEST_ASSERT_EQUAL_UINT8(1, after.effects.count);
  TEST_ASSERT_TRUE(after.effects.items[0].kind == AgentEventKind::Unknown);
  const QueueBody q = agent_queue_draft(after.state);
  TEST_ASSERT_TRUE(q.uncertain);
  TEST_ASSERT_TRUE(q.intent == Intent::Spawn);
  TEST_ASSERT_TRUE(q.project.equals("cpa"));
  TEST_ASSERT_TRUE(q.text.equals("Write native tests for the chord detector"));
}

static void test_timeout_survives_millis_wrap() {
  const auto s = sent_at(0xFFFFF000u);
  TEST_ASSERT_EQUAL_UINT8(0, agent_tick(s.state, 0x00000100u).effects.count);
  TEST_ASSERT_EQUAL_UINT8(1, agent_tick(s.state, 0xFFFFF000u + 30000u).effects.count);
}

static void test_link_loss_is_unknown() {
  const auto lost = agent_link_lost(sent_at(0).state);
  TEST_ASSERT_TRUE(lost.state.phase == AgentPhase::Idle);
  TEST_ASSERT_TRUE(lost.effects.items[0].kind == AgentEventKind::Unknown);
  TEST_ASSERT_TRUE(agent_queue_draft(lost.state).uncertain);
  // nothing in flight: link loss is a no-op
  TEST_ASSERT_EQUAL_UINT8(0, agent_link_lost(AgentReqInfo{}).effects.count);
}

static void test_late_result_is_ignored() {
  const auto unknown = agent_tick(sent_at(0).state, 30000);
  const auto late = agent_result(unknown.state, MsgId::from("d50"), true, ErrCode::None, ShortId::from("a1b2c3"));
  TEST_ASSERT_EQUAL_UINT8(0, late.effects.count);
  TEST_ASSERT_TRUE(late.state.phase == AgentPhase::Idle);
}

static void test_result_for_other_request_is_ignored() {
  const auto s = sent_at(0);
  const auto other = agent_result(s.state, MsgId::from("d49"), true, ErrCode::None, ShortId{});
  TEST_ASSERT_TRUE(other.state.phase == AgentPhase::Inflight);
  TEST_ASSERT_EQUAL_UINT8(0, other.effects.count);
}

static void test_ok_and_failure() {
  const auto s = sent_at(0);
  const auto ok = agent_result(s.state, MsgId::from("d50"), true, ErrCode::None, ShortId::from("a1b2c3"));
  TEST_ASSERT_TRUE(ok.state.phase == AgentPhase::Idle);
  TEST_ASSERT_TRUE(ok.effects.items[0].kind == AgentEventKind::Started);
  TEST_ASSERT_TRUE(ok.effects.items[0].short_id.equals("a1b2c3"));
  const auto fail = agent_result(s.state, MsgId::from("d50"), false, ErrCode::Spawn, ShortId{});
  TEST_ASSERT_TRUE(fail.effects.items[0].kind == AgentEventKind::Failed);
  TEST_ASSERT_TRUE(fail.effects.items[0].code == ErrCode::Spawn);
  TEST_ASSERT_FALSE(agent_queue_draft(fail.state).uncertain);
}

static void test_reply_draft_keeps_session() {
  AgentDraft d;
  d.kind = AgentKind::Reply;
  d.project = ProjectKey::from("cpa");
  d.sid = Sid::from("5c2e8a14");
  d.text = UserText::from("looks good, add a test plan");
  const auto s = agent_send(AgentReqInfo{}, d, MsgId::from("d8"), 0);
  const QueueBody q = agent_queue_draft(agent_link_lost(s.state).state);
  TEST_ASSERT_TRUE(q.intent == Intent::Feedback);
  TEST_ASSERT_TRUE(q.sid.has);
  TEST_ASSERT_TRUE(q.sid.value.equals("5c2e8a14"));
}

static void test_spawn_from_queue_item_keeps_qid() {
  AgentDraft d = spawn_draft();
  d.qid = some(Qid::from("q17"));
  const auto s = agent_send(AgentReqInfo{}, d, MsgId::from("d9"), 0);
  TEST_ASSERT_TRUE(s.state.draft.qid.has);
  TEST_ASSERT_FALSE(agent_queue_draft(s.state).sid.has);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_send_goes_in_flight);
  RUN_TEST(test_second_request_is_busy);
  RUN_TEST(test_timeout_boundary_is_30s);
  RUN_TEST(test_timeout_survives_millis_wrap);
  RUN_TEST(test_link_loss_is_unknown);
  RUN_TEST(test_late_result_is_ignored);
  RUN_TEST(test_result_for_other_request_is_ignored);
  RUN_TEST(test_ok_and_failure);
  RUN_TEST(test_reply_draft_keeps_session);
  RUN_TEST(test_spawn_from_queue_item_keeps_qid);
  return UNITY_END();
}
