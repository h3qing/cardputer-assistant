// The outbox on a FAT store (docs/09-m2-design.md §3.6), FakeStore in FAT mode:
//   - saves write <id>.json directly, moves to bad/ copy then delete: never a rename;
//   - two boot-scan rules apply only where atomic_rename() is false: an empty .json was
//     never committed (opened, power lost before fsync) and is deleted; a .tmp is never
//     ours on FAT and only counted, never deleted (it may share a cluster chain);
//   - power cut before every store operation of a session with saves, acks, rejections
//     and project-cache writes: no item that showed "saved" is lost.
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "codec.h"
#include "outbox_core.h"
#include "outbox_io.h"
#include "outbox_scan.h"
#include "store_commit.h"
#include "support/fake_store.h"

using namespace cpa;
using proto::AckStatus;
using testsupport::FakeStore;

void setUp() {}
void tearDown() {}

static MonoId mid(uint32_t seq, uint32_t boot = 3) {
  MonoId id;
  id.epoch = 7;
  id.boot = boot;
  id.seq = seq;
  return id;
}

static std::string name(uint32_t seq) { return mono_format(mid(seq)).c_str(); }
static std::string json(uint32_t seq) { return "/outbox/" + name(seq) + ".json"; }
static std::string tmp(uint32_t seq) { return "/outbox/" + name(seq) + ".tmp"; }
static std::string bad(uint32_t seq) { return "/outbox/bad/" + name(seq) + ".json"; }

static std::string item_line(uint32_t seq, const char* text) {
  return "{\"v\":0,\"t\":\"outbox.item\",\"id\":\"" + name(seq) +
         "\",\"kind\":\"note\",\"at\":{\"up_ms\":1,\"wall_ms\":null},\"body\":{\"text\":\"" + text + "\"}}\n";
}

static void set_fat(FakeStore& s) { s.set_fat(true); }

static void test_persist_and_move_to_bad_never_rename() {
  FakeStore s;
  set_fat(s);
  const std::string line = item_line(1, "one");
  TEST_ASSERT_TRUE(outbox_persist(s, mid(1), line.data(), line.size()));
  TEST_ASSERT_EQUAL_STRING(line.c_str(), s.get(json(1)).c_str());
  TEST_ASSERT_FALSE(s.has(tmp(1)));
  OutboxEffect move;
  move.kind = OutboxEffectKind::MoveToBad;
  move.id = mid(1);
  TEST_ASSERT_TRUE(outbox_apply(s, move));
  TEST_ASSERT_EQUAL_STRING(line.c_str(), s.get(bad(1)).c_str());
  TEST_ASSERT_FALSE(s.has(json(1)));
  OutboxEffect remove_bad;
  remove_bad.kind = OutboxEffectKind::RemoveBad;
  remove_bad.id = mid(1);
  TEST_ASSERT_TRUE(outbox_apply(s, remove_bad));
  TEST_ASSERT_FALSE(s.has(bad(1)));
  TEST_ASSERT_EQUAL_INT(0, s.renames());
}

static void put_scan_fixture(FakeStore& s) {
  s.put(tmp(1), "{\"partial");             // orphan .tmp
  s.put(tmp(2), "x");                      // .tmp next to its .json
  s.put(json(2), item_line(2, "two"));
  s.put(json(5), "{not json");             // bad content
  s.put(json(6), item_line(6, "six"));
  s.put(json(8), "");                      // empty
}

static void test_scan_on_fat_deletes_empty_json_and_keeps_every_tmp() {
  FakeStore s;
  set_fat(s);
  put_scan_fixture(s);
  proto::Codec codec;
  const ScanResult& r = outbox_boot_scan(s, codec);
  TEST_ASSERT_EQUAL_UINT16(2, r.core.n);
  TEST_ASSERT_TRUE(r.core.ids[0] == mid(2) && r.core.ids[1] == mid(6));
  TEST_ASSERT_FALSE(s.has(json(8)));
  TEST_ASSERT_FALSE(s.has(bad(8)));  // never committed: not evidence, just gone
  TEST_ASSERT_TRUE(s.has(tmp(1)) && s.has(tmp(2)));
  TEST_ASSERT_EQUAL_UINT16(2, r.broken);
  TEST_ASSERT_TRUE(s.has(bad(5)) && !s.has(json(5)));
  TEST_ASSERT_EQUAL_UINT8(1, r.core.n_bad);
  TEST_ASSERT_EQUAL_UINT16(0, r.failed);
  TEST_ASSERT_EQUAL_INT(0, s.renames());
}

// The same files on LittleFS: v0.1's rules, word for word (05 §5.9).
static void test_scan_on_littlefs_keeps_the_v01_rules() {
  FakeStore s;
  put_scan_fixture(s);
  proto::Codec codec;
  const ScanResult& r = outbox_boot_scan(s, codec);
  TEST_ASSERT_EQUAL_UINT16(2, r.core.n);
  TEST_ASSERT_TRUE(s.has(bad(8)));   // empty: bad content, quarantined
  TEST_ASSERT_FALSE(s.has(tmp(1)));  // orphan deleted
  TEST_ASSERT_TRUE(s.has(tmp(2)));
  TEST_ASSERT_EQUAL_UINT16(1, r.broken);
  TEST_ASSERT_EQUAL_UINT8(2, r.core.n_bad);
}

static void test_fold_rules_on_fat() {
  ScanState s;
  s.fat = true;
  scan_entry(s, ScanEntry::Json, mid(1));
  scan_entry(s, ScanEntry::JsonEmpty, mid(1));
  scan_entry(s, ScanEntry::Tmp, mid(3));
  scan_finish(s);
  TEST_ASSERT_EQUAL_UINT16(0, s.core.n);
  TEST_ASSERT_EQUAL_UINT8(0, s.core.n_bad);
  TEST_ASSERT_EQUAL_UINT8(1, s.n);
  TEST_ASSERT_TRUE(s.actions[0].kind == ScanActionKind::RemoveJson && s.actions[0].id == mid(1));
  TEST_ASSERT_EQUAL_UINT16(1, s.broken);  // the orphan .tmp: counted, no action
}

static void test_empty_json_off_fat_is_bad_content() {
  ScanState s;
  scan_entry(s, ScanEntry::Json, mid(1));
  scan_entry(s, ScanEntry::JsonEmpty, mid(1));
  TEST_ASSERT_EQUAL_UINT8(1, s.n);
  TEST_ASSERT_TRUE(s.actions[0].kind == ScanActionKind::MoveToBad);
  TEST_ASSERT_EQUAL_UINT8(1, s.core.n_bad);
}

static void test_empty_json_with_a_full_action_list_stays() {
  ScanState s;
  s.fat = true;
  s.n = kScanActionsMax;
  scan_entry(s, ScanEntry::Json, mid(1));
  scan_entry(s, ScanEntry::JsonEmpty, mid(1));
  TEST_ASSERT_EQUAL_UINT16(0, s.core.n);
  TEST_ASSERT_EQUAL_UINT16(1, s.deferred);
  TEST_ASSERT_TRUE(s.json_deferred);
}

// ---- power loss at every store operation (as test_outbox_core, on FAT) ----

struct Daemon {
  proto::Codec codec;
  std::map<std::string, std::string> stored;  // id -> text

  AckStatus receive(const std::string& line) {
    const auto d = codec.decode(line.data(), line.size());
    if (d.outcome != proto::Outcome::Ok) return AckStatus::Rejected;
    const std::string text = std::get<proto::OutboxItem>(d.msg.body).note.text.c_str();
    if (text.compare(0, 3, "rej") == 0) return AckStatus::Rejected;
    const std::string id = d.msg.id.c_str();
    if (stored.count(id)) return AckStatus::Duplicate;
    stored[id] = text;
    return AckStatus::Stored;
  }
};

static const char kCache[] = "/cfg/projects.json";
static const char kCacheTmp[] = "/cfg/projects.tmp";

struct Device {
  FakeStore& store;
  proto::Codec codec;
  OutboxCore core;
  MonoSeq seq;
  uint32_t now = 0;
  std::vector<std::pair<MonoId, std::string>> saved;  // items that showed "saved"

  Device(FakeStore& s, uint32_t boot) : store(s) {
    const BootRecord rec{7, boot};
    seq = mono_start(boot_ticket(rec, rec).value);
    core = outbox_boot_scan(store, codec).core;
  }

  void save(const std::string& text) {
    const auto next = mono_next(seq);
    seq = next.seq;
    proto::OutboxItem item;
    item.note.text = UserText::from(text.c_str());
    const auto line = codec.encode(proto::make_message(MsgId::from(mono_format(next.id.value).c_str()), {}, item));
    if (!outbox_persist(store, next.id.value, line.value.c_str(), line.value.size())) return;
    core = outbox_added(core, next.id.value).state;
    saved.emplace_back(next.id.value, text);
  }

  void cache(const std::string& content) { commit_file(store, kCache, kCacheTmp, content.data(), content.size()); }

  void pump(Daemon& daemon, bool lose_acks) {
    const auto step = outbox_tick(core, now, true);
    core = step.state;
    for (uint8_t i = 0; i < step.effects.count && !store.crashed(); ++i) {
      char buf[2100];
      const long n = outbox_read(store, step.effects.items[i].id, buf, sizeof buf);
      if (n < 0) continue;
      const AckStatus st = daemon.receive(std::string(buf, static_cast<size_t>(n)));
      if (lose_acks) continue;
      const auto acked = outbox_acked(core, step.effects.items[i].id, st, ErrCode::None);
      core = acked.state;
      for (uint8_t k = 0; k < acked.effects.count; ++k) outbox_apply(store, acked.effects.items[k]);
    }
    now += 3000;
  }
};

static void session(Device& dev, Daemon& daemon) {
  const char* plan[] = {"save", "rej", "pump", "cache", "save", "pump_lost", "save", "rej", "pump", "cache", "save", "pump"};
  int n = 0;
  for (const std::string step : plan) {
    if (dev.store.crashed()) return;
    if (step == "save") dev.save("note-" + std::to_string(++n));
    if (step == "rej") dev.save("rej-" + std::to_string(++n));
    if (step == "pump") dev.pump(daemon, false);
    if (step == "pump_lost") dev.pump(daemon, true);
    if (step == "cache") dev.cache("[\"cache-" + std::to_string(++n) + "\"]");
  }
}

static void check_nothing_lost(const FakeStore& store, const Daemon& daemon, const Device& dev, const std::string& at) {
  for (const auto& item : dev.saved) {
    const std::string id = mono_format(item.first).c_str();
    const bool rejected = item.second.compare(0, 3, "rej") == 0;
    const bool kept = rejected ? store.has("/outbox/bad/" + id + ".json")
                               : daemon.stored.count(id) && daemon.stored.at(id) == item.second;
    TEST_ASSERT_TRUE_MESSAGE(kept, (at + ": lost " + item.second).c_str());
  }
}

static void test_power_loss_at_every_store_operation_on_fat() {
  long total = 0;
  {
    FakeStore clean;
    set_fat(clean);
    Daemon daemon;
    Device dev(clean, 1);
    session(dev, daemon);
    total = clean.ops();
    TEST_ASSERT_EQUAL_UINT(2, clean.count_in("/outbox/bad", ".json"));  // both rejections and nothing else
  }
  TEST_ASSERT_TRUE(total > 25);
  for (long k = 0; k <= total; ++k) {
    FakeStore store;
    set_fat(store);
    Daemon daemon;
    Device dev(store, 1);
    store.crash_before(k);
    session(dev, daemon);
    store.power_cycle();
    Device rebooted(store, 2);
    for (int i = 0; i < 50 && rebooted.core.n > 0; ++i) rebooted.pump(daemon, false);
    const std::string at = "crash before op " + std::to_string(k);
    check_nothing_lost(store, daemon, dev, at);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, store.count_in("/outbox", ".json"), at.c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, store.count_in("/outbox", ".tmp"), at.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, store.renames(), at.c_str());
    const std::string cache = store.has(kCache) ? store.get(kCache) : "";
    TEST_ASSERT_TRUE_MESSAGE(cache.empty() || cache.find("cache-") != std::string::npos, at.c_str());
    TEST_ASSERT_FALSE(store.has(kCacheTmp));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_persist_and_move_to_bad_never_rename);
  RUN_TEST(test_scan_on_fat_deletes_empty_json_and_keeps_every_tmp);
  RUN_TEST(test_scan_on_littlefs_keeps_the_v01_rules);
  RUN_TEST(test_fold_rules_on_fat);
  RUN_TEST(test_empty_json_off_fat_is_bad_content);
  RUN_TEST(test_empty_json_with_a_full_action_list_stays);
  RUN_TEST(test_power_loss_at_every_store_operation_on_fat);
  return UNITY_END();
}
