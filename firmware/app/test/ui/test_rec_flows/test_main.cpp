// The device side of the recording flows (protocol/vectors/v0/flows/rec, docs/11 §4.7, §5):
// replayed against the real Runtime with the audio task on the host (FakeRecPort: a real
// RecEngine on the fake card). core/test_link_flows replays the v0.2 link cores only and
// cannot answer file.*, so these flows are checked here.
//
// The card starts with the segments of the first files.n items the flow lists (flow_card.h).
// The flows do not script the user, so a memo is started when the flow next expects
// E_BUSY "live" for every request still waiting behind the one with the audio task (or for
// a request that arrives with nothing in flight), and is finished before a ping or hello
// whose reply shows files.live false. A memo recorded during the flow has the device's own
// id (the flow's id is aliased to it) and a stand-in hash (only its presence is checked).
// Every device line of the flow must be sent (same type and re; error code/field/msg,
// pong/hello files and pending, every file.items item and every file.data byte) and the
// device must send nothing else.
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "flow_card.h"
#include "rec_engine.h"  // named here so the library finder builds rec and shell
#include "runtime.h"
#include "support/rec_rig.h"

using namespace cpa;
using namespace uitest;
using namespace testsupport;

void setUp() {}
void tearDown() {}

namespace {

bool is_file_type(const std::string& t) { return t == "file.list" || t == "file.get" || t == "file.del"; }

// flows/rec/ holds the flows the link-core replay cannot answer (protocol/README.md).
std::string flow_path(const std::string& name) {
  const std::string p = protocol_path("vectors/v0/flows/rec/" + name + ".json");
  return read_file(p).empty() ? std::string() : p;
}

class Replay {
 public:
  explicit Replay(JsonObjectConst flow) : steps_(flow["steps"]) {
    const std::vector<JsonObjectConst> items = flowcard::flow_items(steps_);
    const size_t n = flowcard::initial_files(steps_);
    for (size_t i = 0; i < items.size(); ++i) {
      if (i < n) {
        flowcard::seed(x_.port.fs, items[i], steps_);
        seeded_.push_back(items[i]["id"].as<std::string>());
      } else {
        recorded_.push_back(items[i]);
      }
    }
    x_.port.finish_scan();
    x_.port.hold_files = true;  // a request stays with the task until the flow wants its reply
    step(10);
    first_ = x_.r.tx.sent.size();
    base_ = x_.r.now;
  }

  bool run(std::string& why) {
    int i = 0;
    for (JsonObjectConst s : steps_) {
      if (!run_step(s)) {
        why = "step " + std::to_string(i) + ": " + why_;
        return false;
      }
      ++i;
    }
    refresh();
    for (size_t k = 0; k < outs_.size(); ++k) {
      if (used_[k]) continue;
      why = "unexpected device line " + x_.r.tx.sent[first_ + k];
      return false;
    }
    return true;
  }

 private:
  bool fail(const std::string& why) {
    why_ = why;
    return false;
  }

  void step(uint32_t ms) { x_.r.step(ms, g0_ ? KeySet{}.with(key::kG0) : KeySet{}); }
  void advance_to(uint32_t t) {
    if (base_ + t > x_.r.now) step(base_ + t - x_.r.now);
  }
  bool live() const { return x_.r.rt.state().rec.phase != RecPhase::Idle; }

  bool run_step(JsonObjectConst s) {
    advance_to(s["t"]);
    if (!s["event"].isNull()) return s["event"] == "daemon_restart" || fail("unknown event");  // host side
    if (!s["expect"].isNull()) return true;  // host.* / link state: the host suites
    if (s["from"] == "device") return device_line(s["msg"]);
    if (!s["lost"].as<bool>()) host_line(s["msg"]);
    return true;
  }

  // ---- the user, as the flow implies ----
  void start_memo() {
    g0_ = true;
    for (int i = 0; i < 100 && !live(); ++i) step(10);
  }
  void finish_memo() {
    x_.port.release_files();
    step(10);
    std::string pcm(64000, '\x11');  // 2 s: kept
    if (next_rec_ < recorded_.size()) pcm = flowcard::wav_of(recorded_[next_rec_++], steps_).substr(rec::kHeaderBytes);
    x_.port.feed_pcm(pcm);
    g0_ = false;  // let go: the memo ends when its grace runs out (gh #51)
    for (uint32_t i = 0; i < 20 + rec::kMemoGraceMs / 10 && live(); ++i) step(10);
  }

  // ---- host lines ----
  void host_line(JsonObjectConst msg) {
    const std::string type = msg["t"].as<std::string>();
    const std::string id = msg["id"].as<std::string>();
    JsonObjectConst reply = reply_to(id);
    if (is_file_type(type) && expects_live(reply) && !live() && outstanding().empty()) start_memo();
    if (reply["files"]["live"].is<bool>() && !reply["files"]["live"].as<bool>() && live()) finish_memo();
    JsonDocument doc;
    doc.set(msg);
    for (const char* k : {"fid", "after"}) {
      if (doc[k].is<const char*>()) doc[k] = dev(doc[k]);
    }
    x_.r.tx.rx += canonical_line(doc.as<JsonVariantConst>());
    if (is_file_type(type)) files_sent_.push_back(id);
  }

  JsonObjectConst reply_to(const std::string& id) const {
    for (JsonObjectConst s : steps_) {
      if (s["from"] == "device" && s["msg"]["re"] == id) return s["msg"];
    }
    return JsonObjectConst();
  }
  static bool expects_live(JsonObjectConst reply) { return reply["t"] == "error" && reply["msg"] == "live"; }

  // file.* requests the device has not answered yet, oldest first (the first one is with
  // the audio task, the others wait in the runtime).
  std::vector<std::string> outstanding() {
    refresh();
    std::vector<std::string> out;
    for (const std::string& id : files_sent_) {
      bool answered = false;
      for (const JsonDocument& o : outs_) answered = answered || o["re"] == id;
      if (!answered) out.push_back(id);
    }
    return out;
  }

  // ---- device lines ----
  bool device_line(JsonObjectConst want) {
    for (int tries = 0; tries < 50; ++tries) {
      if (match(want)) return true;
      nudge();
      step(10);
    }
    return fail("device never sent " + canonical_line(want) + sent_of(want["t"]));
  }

  void nudge() {
    const std::vector<std::string> out = outstanding();
    bool rest_live = out.size() >= 2 && !expects_live(reply_to(out[0]));
    for (size_t i = 1; i < out.size(); ++i) rest_live = rest_live && expects_live(reply_to(out[i]));
    if (!live() && rest_live) return start_memo();
    x_.port.release_files();  // the task gets to the request whose reply the flow wants
  }

  bool match(JsonObjectConst want) {
    refresh();
    for (size_t k = 0; k < outs_.size(); ++k) {
      JsonObjectConst got = outs_[k].as<JsonObjectConst>();
      if (used_[k] || got["t"] != want["t"]) continue;
      if (!want["re"].isNull() && got["re"] != want["re"]) continue;
      if (!same_payload(want, got)) continue;
      used_[k] = true;
      return true;
    }
    return false;
  }

  bool same_payload(JsonObjectConst want, JsonObjectConst got) {
    const std::string t = want["t"].as<std::string>();
    if (t == "error") return same_keys(want, got, {"code", "field", "msg"});
    if (t == "pong" || t == "hello") return want["pending"] == got["pending"] && same_files(want["files"], got["files"]);
    if (t == "file.items") return same_items(want, got);
    if (t == "file.data") return got["fid"] == dev(want["fid"]) && same_keys(want, got, {"off", "n", "size", "crc", "b64"});
    return true;
  }

  static bool same_keys(JsonObjectConst want, JsonObjectConst got, std::initializer_list<const char*> keys) {
    std::string where;
    for (const char* k : keys) {
      if (!json_equal(want[k], got[k], where)) return false;
    }
    return true;
  }

  // n and live as the flow says; rev moves as the flow's rev moves (it starts anywhere).
  bool same_files(JsonVariantConst want, JsonVariantConst got) {
    if (want.isNull() || got.isNull()) return want.isNull() == got.isNull();
    if (want["n"] != got["n"] || want["live"] != got["live"]) return false;
    const long w = want["rev"].as<long>(), g = got["rev"].as<long>();
    if (!rev_known_) {
      rev_known_ = true;
      rev_shift_ = g - w;
    }
    return g - w == rev_shift_;
  }

  bool same_items(JsonObjectConst want, JsonObjectConst got) {
    JsonArrayConst a = want["items"], b = got["items"];
    if (want["more"] != got["more"] || a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (!same_item(a[i], b[i])) return false;
    }
    return true;
  }

  bool same_item(JsonObjectConst want, JsonObjectConst got) {
    const std::string id = want["id"].as<std::string>();
    std::string where;
    for (const std::string& s : seeded_) {
      if (s == id) return json_equal(want, got, where);
    }
    // recorded during the flow: the device's own id and hash
    if (!same_keys(want, got, {"kind", "size", "seg", "last", "mode", "dur_ms"})) return false;
    if (!got["sha256"].is<const char*>()) return false;
    alias_[id] = got["id"].as<std::string>();
    return true;
  }

  // The id the device uses for a flow's id: its own for a memo recorded during the flow.
  std::string dev(JsonVariantConst flow_id) {
    const std::string id = flow_id.as<std::string>();
    return alias_.count(id) ? alias_[id] : id;
  }

  void refresh() {
    for (size_t k = first_ + outs_.size(); k < x_.r.tx.sent.size(); ++k) {
      outs_.emplace_back();
      deserializeJson(outs_.back(), x_.r.tx.sent[k]);
      used_.push_back(false);
    }
  }

  std::string sent_of(JsonVariantConst type) {
    std::string out = "; unmatched of that type:";
    for (size_t k = 0; k < outs_.size(); ++k) {
      if (!used_[k] && outs_[k]["t"] == type) out += " " + x_.r.tx.sent[first_ + k];
    }
    return out;
  }

  RecRig x_{true, false};
  JsonArrayConst steps_;
  std::vector<std::string> seeded_;
  std::vector<JsonObjectConst> recorded_;
  size_t next_rec_ = 0;
  std::vector<std::string> files_sent_;
  std::map<std::string, std::string> alias_;  // flow id -> the device's id
  std::vector<JsonDocument> outs_;            // the device's lines since the flow started
  std::vector<bool> used_;
  size_t first_ = 0;
  uint32_t base_ = 0;
  bool g0_ = false;
  bool rev_known_ = false;
  long rev_shift_ = 0;
  std::string why_;
};

void replay(const char* name) {
  const std::string path = flow_path(name);
  TEST_ASSERT_FALSE_MESSAGE(path.empty(), name);
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(load_json(path, doc), name);
  Replay r(doc.as<JsonObjectConst>());
  std::string why;
  TEST_ASSERT_TRUE_MESSAGE(r.run(why), (std::string(name) + ": " + why).c_str());
}

}  // namespace

static void test_file_busy_while_live() { replay("file_busy_while_live"); }
static void test_file_del_idempotent() { replay("file_del_idempotent"); }
static void test_file_list_paging() { replay("file_list_paging"); }
static void test_file_pull_resume() { replay("file_pull_resume"); }
static void test_files_rev_relist() { replay("files_rev_relist"); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_file_busy_while_live);
  RUN_TEST(test_file_del_idempotent);
  RUN_TEST(test_file_list_paging);
  RUN_TEST(test_file_pull_resume);
  RUN_TEST(test_files_rev_relist);
  return UNITY_END();
}
