// The daemon's side of the fuzz test: what cpad sends and answers, played from a seed,
// and the host's own checks (every request answered, every segment's SHA-256).
#include <ArduinoJson.h>

#include <cstring>

#include "b64.h"
#include "fuzz.h"
#include "sha256.h"

namespace fuzz {
namespace {

const char* const kProjectNames[] = {"Acmeshop 宠物", "Cardputer 助手", "blog", "论文", "x", "家务 todo",
                                     "rust-play", "A very long project nm", "测试"};
const char* const kSessionNames[] = {"支付回调重试", "cpa/host-sim", "landing page copy", "fix flaky test",
                                     "一个特别特别特别长的会话名字吧"};
const char* const kStates[] = {"busy", "shell", "idle", "waiting", "unknown"};
const char* const kTexts[] = {"给 webhook 加签名校验", "write e2e test for refunds", "host sim: contact sheet"};
const char* const kIntents[] = {"todo", "spawn", "feedback"};
// docs/21 §9.2: an English run's Mac sends ASCII only, so any Han on the screen is the UI's.
const char* const kProjectNamesEn[] = {"Acmeshop pets", "Cardputer helper", "blog", "thesis", "x", "chores todo",
                                       "rust-play", "A very long project nm", "test"};
const char* const kSessionNamesEn[] = {"retry the payment hook", "cpa/host-sim", "landing page copy", "fix flaky test",
                                       "a really really really long session name"};
const char* const kTextsEn[] = {"sign the webhook", "write e2e test for refunds", "host sim: contact sheet"};

}  // namespace

std::string Fuzz::msg(const std::string& type, const std::string& fields, bool track) {
  const std::string id = "h" + std::to_string(hid++);
  if (track && cable && alive) reqs[id] = Req{type, T, epoch};
  return "{\"v\":0,\"t\":\"" + type + "\",\"id\":\"" + id + "\"" + (fields.empty() ? "" : "," + fields) + "}";
}

std::string Fuzz::reply(const std::string& type, const std::string& re, const std::string& fields) {
  const std::string id = "h" + std::to_string(hid++);
  return "{\"v\":0,\"t\":\"" + type + "\",\"id\":\"" + id + "\",\"re\":\"" + re + "\"" +
         (fields.empty() ? "" : "," + fields) + "}";
}

void Fuzz::send(const std::string& line) {
  if (cable && alive) dev->tx.host(line);
}

void Fuzz::send_later(uint32_t ms, const std::string& line) { later.emplace_back(T + ms, line); }

void Fuzz::interrupt() {
  ++epoch;
  ready = false;
  pull = Pull{};
  reqs.clear();
  later.clear();
}

void Fuzz::hello() {
  interrupt();
  hid = 1;
  send(msg("hello", "\"role\":\"host\",\"app\":\"cpad/0.4.0\",\"agent\":\"ok\",\"caps\":{\"spawn\":true,\"reply\":true}"));
  last_ping = T;
}

void Fuzz::snapshots(bool all) {
  if (all || chance(0.5)) {
    n_projects = static_cast<int>(rnd(10));
    std::string s = "\"rev\":" + std::to_string(++revs) + ",\"projects\":[";
    for (int i = 0; i < n_projects; ++i) {
      s += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(i + 1) + ",\"key\":\"p" + std::to_string(i + 1) +
           "\",\"name\":\"" + (ascii ? kProjectNamesEn : kProjectNames)[i] + "\",\"run\":" + std::to_string(rnd(3)) + ",\"need\":" +
           std::to_string(rnd(3)) + ",\"queued\":" + std::to_string(rnd(5)) + ",\"ok\":true}";
    }
    send(msg("projects.snapshot", s + "],\"other\":{\"run\":" + std::to_string(rnd(3)) + ",\"n\":1}"));
  }
  if (all || chance(0.5)) {
    const int n = n_projects ? static_cast<int>(rnd(11)) : 0;
    std::string s = "\"rev\":" + std::to_string(++revs) + ",\"total\":" + std::to_string(n) + ",\"sessions\":[";
    for (int i = 0; i < n; ++i) {
      char sid[16];
      std::snprintf(sid, sizeof sid, "%08x", static_cast<unsigned>(0x1000 + i * 17 + (rnd(2) ? 0 : 0x500000)));
      const bool bg = chance(0.5);
      s += std::string(i ? "," : "") + "{\"sid\":\"" + sid + "\",\"slot\":" + std::to_string(1 + rnd(n_projects)) +
           ",\"name\":\"" + (ascii ? kSessionNamesEn : kSessionNames)[rnd(5)] + "\",\"kind\":\"" + (bg ? "bg" : "tty") + "\",\"st\":\"" +
           kStates[rnd(5)] + "\",\"need\":" + (chance(0.4) ? "true" : "false") + ",\"age_s\":" +
           std::to_string(rnd(90000)) + (bg ? ",\"short\":\"a1b2c3\"" : "") + "}";
    }
    send(msg("sessions.snapshot", s + "]"));
  }
  if (all || chance(0.5)) {
    std::string s = "\"rev\":" + std::to_string(++revs) + ",\"items\":[";
    const int n = n_projects ? static_cast<int>(rnd(4)) : 0;
    for (int i = 0; i < n; ++i) {
      s += std::string(i ? "," : "") + "{\"qid\":\"q" + std::to_string(100 + rnd(20)) + std::to_string(i) +
           "\",\"slot\":" + std::to_string(1 + rnd(n_projects)) + ",\"text\":\"" + (ascii ? kTextsEn : kTexts)[rnd(3)] +
           "\",\"intent\":\"" + kIntents[rnd(3)] + "\"}";
    }
    send(msg("queue.snapshot", s + "]"));
  }
}

void Fuzz::start_list() {
  if (pull.active || !ready) return;
  pull = Pull{};
  pull.active = true;
  pull.t = T;
  const std::string m = msg("file.list", "\"max\":4");
  pull.list_id = "h" + std::to_string(hid - 1);
  send(m);
}

void Fuzz::more_gets() {
  while (pull.active && !pull.fid.empty() && pull.gets.size() < 4 && pull.next < pull.size) {
    const std::string m =
        msg("file.get", "\"fid\":\"" + pull.fid + "\",\"off\":" + std::to_string(pull.next) + ",\"len\":1380");
    pull.gets["h" + std::to_string(hid - 1)] = pull.next;
    pull.next += 1380;
    send(m);
  }
}

void Fuzz::on_outbox_item(const std::string& id, const std::string& text) {
  (void)text;
  const bool again = stored.count(id) > 0;
  stored.insert(id);
  if (chance(0.9)) send_later(5 + rnd(200), reply("outbox.ack", id, again ? "\"status\":\"duplicate\"" : "\"status\":\"stored\""));
}

void Fuzz::on_agent(const std::string& id) {
  const uint32_t r = rnd(10);
  if (r < 6) send_later(100 + rnd(3000), reply("agent.result", id, "\"ok\":true,\"name\":\"cpa/x\",\"short\":\"a1b2c3\""));
  if (r >= 6 && r < 8) send_later(100 + rnd(3000), reply("agent.result", id, "\"ok\":false,\"code\":\"E_SPAWN\""));
}

void Fuzz::on_items(const std::string& re, const std::string& fid, uint32_t size, const std::string& sha) {
  if (re != pull.list_id) return;
  if (fid.empty()) {
    pull = Pull{};
    return;
  }
  ever_index.insert(fid);
  pull.fid = fid;
  pull.size = size;
  pull.sha = sha;
  pull.next = 0;
  pull.data.assign(size, '\0');
  more_gets();
}

void Fuzz::on_data(const std::string& re, uint32_t off, const std::string& bytes) {
  auto g = pull.gets.find(re);
  if (g == pull.gets.end()) return;
  if (off + bytes.size() <= pull.data.size()) std::memcpy(&pull.data[off], bytes.data(), bytes.size());
  pull.gets.erase(g);
  more_gets();
  if (!pull.gets.empty() || pull.next < pull.size) return;
  pulled_one();
}

// Every byte in: the PCM after the 44-byte WAV header must hash to the manifest's digest.
void Fuzz::pulled_one() {
  bytes_pulled += pull.size;
  if (!pull.sha.empty() && pull.size > 44) {
    cpasim::Sha256 h;
    h.begin();
    h.update(reinterpret_cast<const uint8_t*>(pull.data.data() + 44), pull.size - 44);
    if (pull.sha != h.finish().c_str()) fail("sha256 mismatch on " + pull.fid);
  }
  const std::string m = msg("file.del", "\"fid\":\"" + pull.fid + "\",\"size\":" + std::to_string(pull.size) +
                                            (pull.sha.empty() ? "" : ",\"sha256\":\"" + pull.sha + "\""));
  pull.del_id = "h" + std::to_string(hid - 1);
  if (cable && alive) pulled.insert(pull.fid);
  send(m);
}

void Fuzz::on_device_line(const std::string& line) {
  const cpa::proto::Decoded& d = codec.decode(line.data(), line.size());
  if (d.outcome != cpa::proto::Outcome::Ok) fail("invalid device line: " + line.substr(0, 120));
  if (!cable || !alive) return;  // lost on the wire
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  const std::string t = doc["t"] | "";
  const std::string id = doc["id"] | "";
  const std::string re = doc["re"] | "";
  if (!re.empty()) reqs.erase(re);
  if (t == "hello" || t == "pong") {
    files_n = doc["files"]["n"] | 0;
    files_live = doc["files"]["live"] | false;
  }
  if (t == "hello") {
    ready = true;
    send(msg("time.set", "\"epoch_ms\":1792546860000,\"tz_min\":480"));
    snapshots(true);
    list_due = T + 300;
  } else if (t == "pong" && files_n > 0 && !files_live && !pull.active) {
    list_due = T + 100;
  } else if (t == "outbox.item") {
    on_outbox_item(id, doc["body"]["text"] | "");
  } else if (t == "agent.spawn" || t == "agent.reply") {
    on_agent(id);
  } else if (t == "queue.done" || t == "inbox.dismiss") {
    if (chance(0.9)) send_later(20 + rnd(500), reply("ack", id, ""));
  } else if (t == "file.items") {
    JsonArrayConst items = doc["items"].as<JsonArrayConst>();
    for (JsonObjectConst it : items) ever_index.insert(it["id"] | "");
    JsonObjectConst first = items.size() ? items[0].as<JsonObjectConst>() : JsonObjectConst{};
    on_items(re, items.size() ? (first["id"] | "") : "", first["size"] | 0, first["sha256"] | "");
  } else if (t == "file.data") {
    const char* b64 = doc["b64"] | "";
    std::string bytes(2048, '\0');
    const long n = cpa::proto::b64_decode(b64, std::strlen(b64), reinterpret_cast<uint8_t*>(&bytes[0]), bytes.size());
    if (n < 0 || static_cast<uint32_t>(n) != (doc["n"] | 0u)) fail("file.data b64 and n disagree");
    bytes.resize(n < 0 ? 0 : static_cast<size_t>(n));
    on_data(re, doc["off"] | 0u, bytes);
  } else if (t == "ack" && pull.active && re == pull.del_id) {
    pull = Pull{};
    list_due = T + 50;
  } else if (t == "error" && pull.active && (re == pull.list_id || re == pull.del_id || pull.gets.count(re))) {
    const std::string code = doc["code"] | "";
    pull = Pull{};
    if (code != "E_BUSY") list_due = T + 2000;
  }
}

void Fuzz::host_tick() {
  for (const std::string& l : dev->tx.sent) on_device_line(l);
  dev->tx.sent.clear();
  dev->tx.classes.clear();
  std::vector<std::pair<uint64_t, std::string>> keep;
  for (const auto& d : later) {
    if (d.first <= T) send(d.second);
    if (d.first > T) keep.push_back(d);
  }
  later.swap(keep);
  if (!cable || !alive) return;
  if (ready && T - last_ping >= 5000) {
    send(msg("ping", ""));
    last_ping = T;
  }
  if (ready && list_due && T >= list_due && files_n > 0 && !files_live) {
    list_due = 0;
    start_list();
  }
  if (pull.active && T - pull.t > 60000 && pull.gets.empty()) pull = Pull{};
  if (T % 1000 != 0) return;
  for (const auto& kv : reqs) {  // every request is answered exactly once (docs/05 §6)
    if (kv.second.epoch == epoch && T - kv.second.t > 12000 && !sh().locked()) fail("unanswered " + kv.second.type);
  }
}

}  // namespace fuzz
