// Flow contract test (05 §9.1, protocol/README "Flows"): the device side of every flow
// in protocol/vectors/v0/flows is replayed through the pure cores (see device_sim.h).
// Host lines are delivered to the device; each device line of the flow must have been
// produced by the device (same type, re, payload) at or before its time; the device
// must send nothing the flow does not show; device.* expectations must hold.
#include <unity.h>

#include <map>
#include <string>

#include "device_sim.h"
#include "support/vectors.h"

using namespace flowsim;
using namespace testsupport;

void setUp() {}
void tearDown() {}

static Codec g_codec;
static std::string g_where;
// Flow MonoId -> the id the simulated device generated for the same item (an item
// the device creates itself cannot carry the id the flow author picked).
static std::map<std::string, std::string> g_alias;

static bool fail(const std::string& why) {
  g_where = why;
  return false;
}

static const char* mode_name(LinkMode m) {
  return m == LinkMode::Ready ? "Ready" : m == LinkMode::Attached ? "Attached" : "Detached";
}

// The payload fields a flow pins down for each device message type.
static bool same_payload(const Out& o, JsonObjectConst want, const DeviceSim& sim) {
  JsonDocument got;
  const auto line = g_codec.encode(o.msg);
  deserializeJson(got, line.value.c_str(), line.value.size());
  std::string where;
  const char* t = want["t"];
  if (std::string(t) == "outbox.item") {
    return json_equal(want["kind"], got["kind"], where) && json_equal(want["body"], got["body"], where) &&
           (!o.from_flow || json_equal(want["id"], got["id"], where));
  }
  if (std::string(t) == "agent.spawn") return json_equal(want["project"], got["project"], where) && json_equal(want["text"], got["text"], where);
  if (std::string(t) == "error") return json_equal(want["code"], got["code"], where) && json_equal(want["field"], got["field"], where);
  if (std::string(t) == "pong" || std::string(t) == "hello") {
    const char* snaps[] = {"projects", "sessions", "queue"};
    for (int i = 0; i < 3; ++i) {
      if (sim.delivered[i] && want["revs"][snaps[i]] != got["revs"][snaps[i]]) return false;
    }
    return want["pending"] == got["pending"];
  }
  return true;
}

static bool match_device_line(DeviceSim& sim, JsonObjectConst want, uint32_t t) {
  const std::string type = want["t"].as<std::string>();
  for (Out& o : sim.outs) {
    if (o.matched || o.t > t || type != type_name(msg_type(o.msg))) continue;
    if (!want["re"].isNull() && !(o.msg.re.has && o.msg.re.value.equals(want["re"].as<const char*>()))) continue;
    if (!same_payload(o, want, sim)) continue;
    o.matched = true;
    if (type == "outbox.item" && !o.from_flow) g_alias[want["id"].as<std::string>()] = o.msg.id.c_str();
    return true;
  }
  return false;
}

static bool device_line(DeviceSim& sim, JsonObjectConst step) {
  const uint32_t t = step["t"];
  if (step["msg"].isNull()) return true;  // device noise (boot log): not a message
  JsonObjectConst want = step["msg"];
  if (match_device_line(sim, want, t)) return true;
  if (std::string(want["t"].as<const char*>()) == "outbox.item") {  // an item saved just now
    const std::string raw = canonical_line(want);
    sim.seed(g_codec.decode(raw.data(), raw.size()).msg, t);
    if (match_device_line(sim, want, t)) return true;
  }
  return fail("device never sent " + canonical_line(want));
}

static bool expectations(const DeviceSim& sim, JsonObjectConst expect) {
  if (!expect["device.link"].isNull() && expect["device.link"] != mode_name(sim.link.info.mode)) {
    return fail(std::string("device.link is ") + mode_name(sim.link.info.mode));
  }
  if (!expect["device.outbox_inflight"].isNull() && expect["device.outbox_inflight"] != sim.outbox.n_inflight) {
    return fail("device.outbox_inflight is " + std::to_string(sim.outbox.n_inflight));
  }
  if (!expect["device.outbox_pending"].isNull() && expect["device.outbox_pending"] != sim.outbox.n) {
    return fail("device.outbox_pending is " + std::to_string(sim.outbox.n));
  }
  if (!expect["device.agent_inflight"].isNull() && expect["device.agent_inflight"] != agent_inflight(sim.app.agent)) {
    return fail("device.agent_inflight is " + std::to_string(agent_inflight(sim.app.agent)));
  }
  return true;
}

static bool event(DeviceSim& sim, JsonObjectConst step) {
  const std::string name = step["event"].as<std::string>();
  const uint32_t t = step["t"];
  if (name == "user_spawn") sim.user_spawn(step["args"]["project"], step["args"]["text"], t);
  if (name == "device_reboot") sim.reboot(t);
  if (name == "detach") sim.detach(t);
  return true;  // daemon_restart is host-side only
}

static bool run_step(DeviceSim& sim, JsonObjectConst step) {
  sim.tick(step["t"]);
  if (!step["event"].isNull()) return event(sim, step);
  if (!step["expect"].isNull()) return expectations(sim, step["expect"]);
  if (step["from"] == "device") return device_line(sim, step);
  if (step["lost"].as<bool>()) return true;
  std::string raw = case_line(step);
  const auto alias = g_alias.find(step["msg"]["re"] | "");
  if (alias != g_alias.end()) {
    JsonDocument msg;
    msg.set(step["msg"]);
    msg["re"] = alias->second;
    raw = canonical_line(msg.as<JsonVariantConst>());
  }
  sim.host_line(raw, step["t"]);
  return true;
}

// Seeds the outbox with the first `pending` items the flow shows the device sending.
static void seed_initial(DeviceSim& sim, JsonObjectConst flow) {
  int pending = flow["initial"]["device.outbox_pending"] | 0;
  std::vector<std::string> seen;
  for (JsonObjectConst step : flow["steps"].as<JsonArrayConst>()) {
    if (pending == 0 || step["msg"]["t"] != "outbox.item") continue;
    const std::string id = step["msg"]["id"].as<std::string>();
    if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
    seen.push_back(id);
    const std::string raw = canonical_line(step["msg"]);
    sim.seed(g_codec.decode(raw.data(), raw.size()).msg, 0);
    --pending;
  }
}

static bool run_flow(JsonObjectConst flow) {
  g_alias.clear();
  DeviceSim sim(flow["initial"]["device.link"] == "Ready");
  seed_initial(sim, flow);
  int index = 0;
  for (JsonObjectConst step : flow["steps"].as<JsonArrayConst>()) {
    if (!run_step(sim, step)) return fail("step " + std::to_string(index) + ": " + g_where);
    ++index;
  }
  for (const Out& o : sim.outs) {
    if (!o.matched) return fail(std::string("unexpected device message ") + g_codec.encode(o.msg).value.c_str());
  }
  return true;
}

static void test_every_flow() {
  const auto files = list_dir(protocol_path("vectors/v0/flows"), ".json");
  TEST_ASSERT_TRUE(files.size() >= 7);
  for (const auto& file : files) {
    JsonDocument doc;
    TEST_ASSERT_TRUE_MESSAGE(load_json(protocol_path("vectors/v0/flows/" + file), doc), file.c_str());
    g_where.clear();
    TEST_ASSERT_TRUE_MESSAGE(run_flow(doc.as<JsonObjectConst>()), (file + ": " + g_where).c_str());
  }
}

static void test_required_flows_exist() {
  const char* required[] = {"handshake", "tx_parking", "outbox_replay_dup_ack", "reboot_detect",
                            "credit_window", "offline_spawn_becomes_queue", "agent_result_unknown",
                            // v0.4.1 (docs/13 §12.2): the link core replays these like the rest
                            "caps_absent", "old_daemon_ignores_dest", "new_daemon_old_firmware"};
  for (const char* name : required) {
    TEST_ASSERT_FALSE_MESSAGE(read_file(protocol_path(std::string("vectors/v0/flows/") + name + ".json")).empty(), name);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_flow);
  RUN_TEST(test_required_flows_exist);
  return UNITY_END();
}
