// Outbox boot scan (05 §5.9): an orphan .tmp (no .json of the same id) is deleted; a
// .tmp next to its .json is kept and only counted (broken invariant); a .json that
// does not decode, or whose id differs from its name, moves to bad/ (20 kept); a .json
// that cannot be read stays for a later boot. The fold works in place on the scan's
// static state, and no file is read inside a directory listing (16 KB loop stack).
#include <unity.h>

#include <string>

#include "codec.h"
#include "outbox_io.h"
#include "outbox_scan.h"
#include "support/fake_store.h"

using namespace cpa;
using testsupport::FakeStore;

void setUp() {}
void tearDown() {}

static MonoId mid(uint32_t seq) {
  MonoId id;
  id.epoch = 7;
  id.boot = 3;
  id.seq = seq;
  return id;
}

static std::string name(uint32_t seq) { return mono_format(mid(seq)).c_str(); }

static std::string item_line(uint32_t seq, const char* text) {
  return "{\"v\":0,\"t\":\"outbox.item\",\"id\":\"" + name(seq) +
         "\",\"kind\":\"note\",\"at\":{\"up_ms\":1,\"wall_ms\":null},\"body\":{\"text\":\"" + text + "\"}}\n";
}

static void test_classify_names() {
  const auto json = scan_classify((name(3) + ".json").c_str(), false);
  TEST_ASSERT_TRUE(json.kind == ScanName::Json);
  TEST_ASSERT_TRUE(json.id == mid(3));
  TEST_ASSERT_TRUE(scan_classify((name(3) + ".tmp").c_str(), false).kind == ScanName::Tmp);
  TEST_ASSERT_TRUE(scan_classify("bad", true).kind == ScanName::Dir);
  TEST_ASSERT_TRUE(scan_classify("notes.txt", false).kind == ScanName::Other);
  TEST_ASSERT_TRUE(scan_classify("x.json", false).kind == ScanName::Other);
  TEST_ASSERT_TRUE(scan_classify((name(3) + ".json").c_str(), true).kind == ScanName::Other);
}

static void test_fold_rules() {
  ScanState s;
  scan_entry(s, ScanEntry::Tmp, mid(1));  // orphan
  scan_entry(s, ScanEntry::Tmp, mid(2));  // next to its json
  scan_entry(s, ScanEntry::Json, mid(9));
  scan_entry(s, ScanEntry::Json, mid(2));
  scan_entry(s, ScanEntry::Json, mid(5));
  scan_entry(s, ScanEntry::JsonBad, mid(5));  // its content is checked after the listing
  scan_finish(s);
  TEST_ASSERT_EQUAL_UINT16(2, s.core.n);
  TEST_ASSERT_EQUAL_UINT32(2, s.core.ids[0].seq);  // sorted whatever the listing order
  TEST_ASSERT_EQUAL_UINT32(9, s.core.ids[1].seq);
  TEST_ASSERT_EQUAL_UINT16(1, s.broken);
  TEST_ASSERT_EQUAL_UINT8(2, s.n);
  TEST_ASSERT_TRUE(s.actions[0].kind == ScanActionKind::MoveToBad && s.actions[0].id == mid(5));
  TEST_ASSERT_TRUE(s.actions[1].kind == ScanActionKind::RemoveTmp && s.actions[1].id == mid(1));
  TEST_ASSERT_EQUAL_UINT8(1, s.core.n_bad);
}

static void test_bad_dir_keeps_the_newest_twenty() {
  ScanState s;
  for (uint32_t i = 0; i < 22; ++i) scan_entry(s, ScanEntry::Bad, mid(100 + i));
  scan_entry(s, ScanEntry::Json, mid(200));
  scan_entry(s, ScanEntry::JsonBad, mid(200));
  scan_finish(s);
  TEST_ASSERT_EQUAL_UINT8(kOutboxBadKeep, s.core.n_bad);
  uint8_t removed = 0;
  for (uint8_t i = 0; i < s.n; ++i) removed = static_cast<uint8_t>(removed + (s.actions[i].kind == ScanActionKind::RemoveBad));
  TEST_ASSERT_EQUAL_UINT8(3, removed);
  TEST_ASSERT_TRUE(s.core.bad[0] == mid(103));
}

static void test_more_json_than_capacity_stays_on_disk() {
  ScanState s;
  for (uint32_t i = 0; i < kOutboxCapacity + 5; ++i) scan_entry(s, ScanEntry::Json, mid(1000 - i));
  scan_finish(s);
  TEST_ASSERT_EQUAL_UINT16(kOutboxCapacity, s.core.n);
  TEST_ASSERT_EQUAL_UINT16(5, s.deferred);
  TEST_ASSERT_EQUAL_UINT8(0, s.n);                                          // nothing deleted
  TEST_ASSERT_EQUAL_UINT32(1000 - kOutboxCapacity - 4, s.core.ids[0].seq);  // oldest first
}

// The cleanup list is full: the bad .json stays in /outbox, so it is neither sent nor
// counted as being in bad/ (OutboxInfo.bad stays right), and .tmp files are kept.
static void test_reject_with_a_full_action_list_is_deferred() {
  ScanState s;
  s.n = kScanActionsMax;
  scan_entry(s, ScanEntry::Json, mid(1));
  scan_entry(s, ScanEntry::JsonBad, mid(1));
  TEST_ASSERT_EQUAL_UINT16(0, s.core.n);
  TEST_ASSERT_EQUAL_UINT8(0, s.core.n_bad);
  TEST_ASSERT_EQUAL_UINT16(1, s.deferred);
  TEST_ASSERT_TRUE(s.json_deferred);
}

static void test_scan_on_a_store() {
  FakeStore store;
  store.put("/outbox/" + name(1) + ".tmp", "{\"partial");
  store.put("/outbox/" + name(2) + ".tmp", "x");
  store.put("/outbox/" + name(2) + ".json", item_line(2, "two"));
  store.put("/outbox/" + name(3) + ".json", item_line(4, "id mismatch"));
  store.put("/outbox/" + name(5) + ".json", "{not json");
  store.put("/outbox/" + name(6) + ".json", item_line(6, "six"));
  store.put("/outbox/" + name(8) + ".json", "");  // empty: bad content, not a read error
  store.put("/outbox/readme.txt", "left alone");
  proto::Codec codec;
  const ScanResult& r = outbox_boot_scan(store, codec);
  TEST_ASSERT_EQUAL_UINT16(2, r.core.n);
  TEST_ASSERT_TRUE(r.core.ids[0] == mid(2) && r.core.ids[1] == mid(6));
  TEST_ASSERT_EQUAL_UINT16(1, r.broken);
  TEST_ASSERT_EQUAL_UINT16(1, r.other);
  TEST_ASSERT_FALSE(store.has("/outbox/" + name(1) + ".tmp"));
  TEST_ASSERT_TRUE(store.has("/outbox/" + name(2) + ".tmp"));
  TEST_ASSERT_TRUE(store.has("/outbox/bad/" + name(3) + ".json"));
  TEST_ASSERT_TRUE(store.has("/outbox/bad/" + name(5) + ".json"));
  TEST_ASSERT_TRUE(store.has("/outbox/bad/" + name(8) + ".json"));
  TEST_ASSERT_TRUE(store.has("/outbox/readme.txt"));
  TEST_ASSERT_EQUAL_UINT8(3, r.core.n_bad);
  TEST_ASSERT_EQUAL_UINT16(0, r.deferred);
}

// A read error is not bad content: the item stays in /outbox for a later boot (05 §5.9
// quarantines only on parse failure or id mismatch), and every .tmp is kept meanwhile.
static void test_unreadable_item_stays_for_a_later_boot() {
  FakeStore store;
  store.put("/outbox/" + name(4) + ".json", item_line(4, "four"));
  store.put("/outbox/" + name(4) + ".tmp", "x");
  store.put("/outbox/" + name(7) + ".tmp", "x");
  store.fail_read("/outbox/" + name(4) + ".json");
  proto::Codec codec;
  const ScanResult& r = outbox_boot_scan(store, codec);
  TEST_ASSERT_EQUAL_UINT16(0, r.core.n);
  TEST_ASSERT_EQUAL_UINT8(0, r.core.n_bad);
  TEST_ASSERT_TRUE(store.has("/outbox/" + name(4) + ".json"));
  TEST_ASSERT_FALSE(store.has("/outbox/bad/" + name(4) + ".json"));
  TEST_ASSERT_TRUE(store.has("/outbox/" + name(4) + ".tmp"));
  TEST_ASSERT_TRUE(store.has("/outbox/" + name(7) + ".tmp"));
  TEST_ASSERT_EQUAL_UINT16(3, r.deferred);  // the .json and both .tmp files
}

// Reading and decoding inside the DirVisitor callback nested the whole decode chain
// under the listing and overflowed the 16 KB loop stack on the device.
class ListingProbe : public FakeStore {
 public:
  bool list(const char* dir, DirVisitor& visit) override {
    listing_ = true;
    const bool ok = FakeStore::list(dir, visit);
    listing_ = false;
    return ok;
  }
  long read(const char* path, char* buf, size_t cap) override {
    reads_while_listing += listing_ ? 1 : 0;
    return FakeStore::read(path, buf, cap);
  }
  int reads_while_listing = 0;

 private:
  bool listing_ = false;
};

static void test_no_file_is_read_while_listing() {
  ListingProbe store;
  store.put("/outbox/" + name(1) + ".json", item_line(1, "one"));
  store.put("/outbox/" + name(2) + ".json", "{broken");
  store.put("/outbox/bad/" + name(0) + ".json", "{old");
  proto::Codec codec;
  const ScanResult& r = outbox_boot_scan(store, codec);
  TEST_ASSERT_EQUAL_INT(0, store.reads_while_listing);
  TEST_ASSERT_EQUAL_UINT16(1, r.core.n);
  TEST_ASSERT_EQUAL_UINT8(2, r.core.n_bad);
  TEST_ASSERT_EQUAL_PTR(&r, &outbox_boot_scan(store, codec));  // static result: no copy
}

static void test_persist_read_and_paths() {
  FakeStore store;
  const std::string line = item_line(8, "eight");
  TEST_ASSERT_TRUE(outbox_persist(store, mid(8), line.data(), line.size()));
  TEST_ASSERT_EQUAL_STRING(line.c_str(), store.get("/outbox/" + name(8) + ".json").c_str());
  TEST_ASSERT_FALSE(store.has("/outbox/" + name(8) + ".tmp"));
  char buf[64];
  TEST_ASSERT_EQUAL_INT(-1, outbox_read(store, mid(9), buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING(("/outbox/bad/" + name(8) + ".json").c_str(), outbox_path(mid(8), OutboxFile::Bad).c_str());
  OutboxEffect move;
  move.kind = OutboxEffectKind::MoveToBad;
  move.id = mid(8);
  TEST_ASSERT_TRUE(outbox_apply(store, move));
  TEST_ASSERT_TRUE(store.has("/outbox/bad/" + name(8) + ".json"));
  OutboxEffect send;
  send.kind = OutboxEffectKind::Send;
  TEST_ASSERT_TRUE(outbox_apply(store, send));  // not a storage effect
}

static void test_failed_write_leaves_no_json() {
  FakeStore store;
  store.crash_before(2);  // open, write ok; close fails
  const std::string line = item_line(8, "eight");
  TEST_ASSERT_FALSE(outbox_persist(store, mid(8), line.data(), line.size()));
  TEST_ASSERT_FALSE(store.has("/outbox/" + name(8) + ".json"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_classify_names);
  RUN_TEST(test_fold_rules);
  RUN_TEST(test_bad_dir_keeps_the_newest_twenty);
  RUN_TEST(test_more_json_than_capacity_stays_on_disk);
  RUN_TEST(test_reject_with_a_full_action_list_is_deferred);
  RUN_TEST(test_scan_on_a_store);
  RUN_TEST(test_unreadable_item_stays_for_a_later_boot);
  RUN_TEST(test_no_file_is_read_while_listing);
  RUN_TEST(test_persist_read_and_paths);
  RUN_TEST(test_failed_write_leaves_no_json);
  return UNITY_END();
}
