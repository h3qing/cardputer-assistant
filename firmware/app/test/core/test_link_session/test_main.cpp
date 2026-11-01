// LinkSession (05 §5.8, §6.3, §6.4): Detached / Attached / Ready. The device never
// starts a handshake, answers every host-initiated message exactly once, only
// answers hello before the handshake, times out after 15 s without data and treats
// a hello while Ready as a daemon restart.
#include <unity.h>

#include <memory>
#include <string>

#include "codec.h"
#include "link_session.h"

using namespace cpa;
using namespace cpa::proto;

void setUp() {}
void tearDown() {}

static std::unique_ptr<Codec> g_codec;

static const char* kHello =
    "{\"v\":0,\"t\":\"hello\",\"id\":\"h1\",\"role\":\"host\",\"app\":\"cpad/0.1.0\",\"agent\":\"ok\","
    "\"caps\":{\"spawn\":true,\"reply\":false}}";

static LinkStep line(const LinkCore& c, const std::string& text, uint32_t now) {
  return link_line(c, g_codec->decode(text.data(), text.size()), now);
}

static LinkCore ready_at(uint32_t now) {
  const LinkCore attached = link_tick(LinkCore{}, true, 0).state;
  return line(attached, kHello, now).state;
}

static void assert_reply(const LinkStep& s, uint8_t i, ReplyKind kind, const char* re) {
  TEST_ASSERT_TRUE(i < s.effects.count);
  TEST_ASSERT_TRUE(s.effects.items[i].kind == LinkEffectKind::Reply);
  TEST_ASSERT_TRUE(s.effects.items[i].reply == kind);
  TEST_ASSERT_EQUAL_STRING(re, s.effects.items[i].re.c_str());
}

static void test_plug_and_unplug() {
  const auto attached = link_tick(LinkCore{}, true, 10);
  TEST_ASSERT_TRUE(attached.state.info.mode == LinkMode::Attached);
  TEST_ASSERT_TRUE(attached.effects.items[0].kind == LinkEffectKind::Changed);
  TEST_ASSERT_EQUAL_UINT8(0, link_tick(attached.state, true, 20).effects.count);
  const auto gone = link_tick(ready_at(100), false, 200);
  TEST_ASSERT_TRUE(gone.state.info.mode == LinkMode::Detached);
  TEST_ASSERT_TRUE(gone.effects.items[0].from == LinkMode::Ready);
}

static void test_handshake_makes_ready_and_records_caps() {
  const LinkCore attached = link_tick(LinkCore{}, true, 0).state;
  const auto s = line(attached, kHello, 100);
  TEST_ASSERT_TRUE(s.state.info.mode == LinkMode::Ready);
  TEST_ASSERT_TRUE(s.state.info.agent == AgentStatus::Ok);
  TEST_ASSERT_TRUE(s.state.info.cap_spawn);
  TEST_ASSERT_FALSE(s.state.info.cap_reply);
  TEST_ASSERT_EQUAL_UINT32(100, s.state.info.last_rx_ms);
  assert_reply(s, 0, ReplyKind::Hello, "h1");
  TEST_ASSERT_EQUAL_STRING("d1", s.effects.items[0].id.c_str());
  TEST_ASSERT_TRUE(s.effects.items[1].kind == LinkEffectKind::Changed);
  TEST_ASSERT_FALSE(s.effects.items[1].rehandshake);
}

static void test_only_hello_is_answered_before_handshake() {
  const LinkCore attached = link_tick(LinkCore{}, true, 0).state;
  const auto s = line(attached, "{\"v\":0,\"t\":\"ping\",\"id\":\"h12\"}", 5000);
  assert_reply(s, 0, ReplyKind::Error, "h12");
  TEST_ASSERT_TRUE(s.effects.items[0].code == ErrCode::NotReady);
  TEST_ASSERT_TRUE(s.state.info.mode == LinkMode::Attached);
}

static void test_ready_answers_each_host_message_once() {
  const LinkCore r = ready_at(0);
  assert_reply(line(r, "{\"v\":0,\"t\":\"ping\",\"id\":\"h8\"}", 10), 0, ReplyKind::Pong, "h8");
  const auto t = line(r, "{\"v\":0,\"t\":\"time.set\",\"id\":\"h2\",\"epoch_ms\":1792440000000,\"tz_min\":-420}", 10);
  TEST_ASSERT_EQUAL_UINT8(2, t.effects.count);
  TEST_ASSERT_TRUE(t.effects.items[0].kind == LinkEffectKind::Deliver);
  assert_reply(t, 1, ReplyKind::Ack, "h2");
  const auto snap = line(r, "{\"v\":0,\"t\":\"queue.snapshot\",\"id\":\"h5\",\"rev\":9,\"items\":[]}", 10);
  assert_reply(snap, 1, ReplyKind::Ack, "h5");
  const auto dbg = line(r, "{\"v\":0,\"t\":\"debug.state\",\"id\":\"h71\"}", 10);
  TEST_ASSERT_EQUAL_UINT8(1, dbg.effects.count);
  assert_reply(dbg, 0, ReplyKind::DebugState, "h71");
  const auto reboot = line(r, "{\"v\":0,\"t\":\"debug.reboot\",\"id\":\"h72\"}", 10);
  assert_reply(reboot, 0, ReplyKind::Ack, "h72");
  TEST_ASSERT_TRUE(reboot.effects.items[1].kind == LinkEffectKind::Deliver);
  const auto key = line(r, "{\"v\":0,\"t\":\"debug.key\",\"id\":\"h70\",\"keys\":[\"g0\"]}", 10);
  assert_reply(key, 0, ReplyKind::Ack, "h70");
  // a device-only type sent by the host is not something the device accepts
  const auto odd = line(r, "{\"v\":0,\"t\":\"queue.done\",\"id\":\"h9\",\"qid\":\"q1\"}", 10);
  assert_reply(odd, 0, ReplyKind::Error, "h9");
  TEST_ASSERT_TRUE(odd.effects.items[0].code == ErrCode::UnknownType);
  // dests.snapshot decodes (protocol README item 24) but is not taken before F5 (docs/25
  // §4.6): answered as before, an unknown type, and never delivered.
  const std::string snapshot = "{\"v\":0,\"t\":\"dests.snapshot\",\"id\":\"h13\",\"rev\":1,\"dests\":[]}";
  TEST_ASSERT_TRUE(g_codec->decode(snapshot.data(), snapshot.size()).outcome == Outcome::Ok);
  const auto dests = line(r, snapshot, 10);
  TEST_ASSERT_EQUAL_UINT8(1, dests.effects.count);
  assert_reply(dests, 0, ReplyKind::Error, "h13");
  TEST_ASSERT_TRUE(dests.effects.items[0].code == ErrCode::UnknownType);
}

static void test_host_replies_are_delivered_not_answered() {
  const auto s = line(ready_at(0), "{\"v\":0,\"t\":\"outbox.ack\",\"id\":\"h6\",\"re\":\"3QZK1M7-0000C-0003\",\"status\":\"stored\"}", 50);
  TEST_ASSERT_EQUAL_UINT8(1, s.effects.count);
  TEST_ASSERT_TRUE(s.effects.items[0].kind == LinkEffectKind::Deliver);
  TEST_ASSERT_EQUAL_UINT32(50, s.state.info.last_rx_ms);
}

static void test_bad_lines() {
  const LinkCore r = ready_at(0);
  const auto bad = line(r, "{\"v\":0,\"t\":\"time.set\",\"id\":\"h66\",\"epoch_ms\":1792440000000,\"tz_min\":900}", 10);
  assert_reply(bad, 0, ReplyKind::Error, "h66");
  TEST_ASSERT_TRUE(bad.effects.items[0].code == ErrCode::BadField);
  TEST_ASSERT_TRUE(bad.effects.items[0].field.has);
  TEST_ASSERT_EQUAL_STRING("tz_min", bad.effects.items[0].field.value.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, bad.state.info.last_rx_ms);  // not valid data
  const auto parse = line(r, "{\"v\":0,", 10);
  TEST_ASSERT_EQUAL_UINT8(0, parse.effects.count);
  TEST_ASSERT_EQUAL_UINT32(1, parse.state.counters.parse);
  const auto noise = line(r, "boot log", 10);
  TEST_ASSERT_EQUAL_UINT32(1, noise.state.counters.noise);
  const auto ignored = line(r, "{\"v\":0,\"t\":\"foo.bar\",\"id\":\"h62\",\"re\":\"d60\"}", 10);
  TEST_ASSERT_EQUAL_UINT8(0, ignored.effects.count);
  TEST_ASSERT_EQUAL_UINT32(1, ignored.state.counters.ignored);
  const auto broken_reply = line(r, "{\"v\":0,\"t\":\"outbox.ack\",\"id\":\"h6\",\"re\":\"d7\",\"status\":\"stored\"}", 10);
  TEST_ASSERT_EQUAL_UINT8(0, broken_reply.effects.count);
  TEST_ASSERT_EQUAL_UINT32(1, broken_reply.state.counters.bad);
}

static void test_field_path_outside_the_error_pattern_is_omitted() {
  const auto s = line(ready_at(0), "{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"a b\":\"\xC3(\"}", 10);
  assert_reply(s, 0, ReplyKind::Error, "h1");
  TEST_ASSERT_FALSE(s.effects.items[0].field.has);
}

static void test_version_mismatch_is_flagged() {
  const LinkCore attached = link_tick(LinkCore{}, true, 0).state;
  const auto s = line(attached, "{\"v\":1,\"t\":\"hello\",\"id\":\"h1\",\"role\":\"host\"}", 10);
  assert_reply(s, 0, ReplyKind::Error, "h1");
  TEST_ASSERT_TRUE(s.effects.items[0].code == ErrCode::Version);
  TEST_ASSERT_TRUE(s.state.info.version_mismatch);
  TEST_ASSERT_FALSE(line(s.state, kHello, 20).state.info.version_mismatch);
}

static void test_error_replies_are_rate_limited() {
  LinkCore c = link_tick(LinkCore{}, true, 0).state;
  uint8_t sent = 0;
  for (int i = 0; i < 8; ++i) {
    const auto s = line(c, "{\"v\":0,\"t\":\"ping\",\"id\":\"h1\"}", 100 + i);
    sent = static_cast<uint8_t>(sent + s.effects.count);
    c = s.state;
  }
  TEST_ASSERT_EQUAL_UINT8(5, sent);
  TEST_ASSERT_EQUAL_UINT32(3, c.counters.errors_dropped);
  TEST_ASSERT_EQUAL_UINT8(1, line(c, "{\"v\":0,\"t\":\"ping\",\"id\":\"h1\"}", 1100).effects.count);
}

static void test_timeout_after_15s_without_data() {
  const LinkCore r = ready_at(1000);
  TEST_ASSERT_EQUAL_UINT8(0, link_tick(r, true, 15999).effects.count);
  const auto lost = link_tick(r, true, 16000);
  TEST_ASSERT_TRUE(lost.state.info.mode == LinkMode::Attached);
  TEST_ASSERT_TRUE(lost.effects.items[0].from == LinkMode::Ready);
  TEST_ASSERT_TRUE(lost.effects.items[0].to == LinkMode::Attached);
}

static void test_hello_while_ready_is_a_rehandshake() {
  const auto s = line(ready_at(0), kHello, 3000);
  assert_reply(s, 0, ReplyKind::Hello, "h1");
  TEST_ASSERT_TRUE(s.effects.items[1].kind == LinkEffectKind::Changed);
  TEST_ASSERT_TRUE(s.effects.items[1].rehandshake);
  TEST_ASSERT_TRUE(link_lost_inflight(s.effects.items[1]));
  TEST_ASSERT_TRUE(s.state.info.mode == LinkMode::Ready);
}

static void test_ids_are_issued_in_order() {
  const auto a = link_issue_id(LinkCore{});
  const auto b = link_issue_id(a.core);
  TEST_ASSERT_EQUAL_STRING("d1", a.id.c_str());
  TEST_ASSERT_EQUAL_STRING("d2", b.id.c_str());
}

int main() {
  g_codec.reset(new Codec());
  UNITY_BEGIN();
  RUN_TEST(test_plug_and_unplug);
  RUN_TEST(test_handshake_makes_ready_and_records_caps);
  RUN_TEST(test_only_hello_is_answered_before_handshake);
  RUN_TEST(test_ready_answers_each_host_message_once);
  RUN_TEST(test_host_replies_are_delivered_not_answered);
  RUN_TEST(test_bad_lines);
  RUN_TEST(test_field_path_outside_the_error_pattern_is_omitted);
  RUN_TEST(test_version_mismatch_is_flagged);
  RUN_TEST(test_error_replies_are_rate_limited);
  RUN_TEST(test_timeout_after_15s_without_data);
  RUN_TEST(test_hello_while_ready_is_a_rehandshake);
  RUN_TEST(test_ids_are_issued_in_order);
  return UNITY_END();
}
