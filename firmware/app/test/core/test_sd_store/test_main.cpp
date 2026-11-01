// SdStore (docs/09-m2-design.md §3.3, §3.5, §3.8) over FakeFatPort: path mapping under
// the root, parent directories created on demand, list() folding epoch directories back
// into "<MonoId>.json" names (one directory open at a time, at most 8 epochs), the
// prepare() probe write, rename refused without touching the card, and the latch: after
// the first Disk error not one more call reaches the port.
#include <unity.h>

#include <string>
#include <vector>

#include "null_store.h"
#include "sd_store.h"
#include "support/fake_fat_port.h"

using namespace cpa;
using testsupport::FakeFatPort;
using testsupport::FatCall;

void setUp() {}
void tearDown() {}

static const char kId[] = "1K3M9Q2-00042-0A1C";
static const std::string kItem = std::string("/outbox/") + kId + ".json";
static const std::string kPhys = "/cpa/outbox/1K3M9Q2/000420A1.JC";

class Names : public DirVisitor {
 public:
  std::vector<std::string> all;
  void entry(const char* name, bool is_dir) override { all.push_back(std::string(name) + (is_dir ? "/" : "")); }
  bool has(const std::string& n) const {
    for (const auto& a : all) {
      if (a == n) return true;
    }
    return false;
  }
};

static bool put(SdStore& s, const std::string& path, const std::string& text) {
  return s.open_write(path.c_str()) && s.write(text.data(), text.size()) && s.close();
}

static std::string read_all(SdStore& s, const std::string& path) {
  char buf[64];
  const long n = s.read(path.c_str(), buf, sizeof buf);
  return n < 0 ? "<error>" : std::string(buf, static_cast<size_t>(n));
}

static void test_prepare_makes_the_root_and_probes_a_write() {
  FakeFatPort port;
  SdStore store(port);
  TEST_ASSERT_TRUE(store.prepare());
  TEST_ASSERT_TRUE(port.has_dir("/cpa") && port.has_dir("/cpa/outbox"));
  TEST_ASSERT_FALSE(port.has("/cpa/PROBE.BIN"));  // written, synced, then deleted
  TEST_ASSERT_EQUAL_INT(1, port.calls(FatCall::OpenWrite));
  TEST_ASSERT_EQUAL_INT(1, port.calls(FatCall::Write));
  TEST_ASSERT_EQUAL_INT(1, port.calls(FatCall::SyncClose));
  TEST_ASSERT_EQUAL_INT(1, port.calls(FatCall::Unlink));
  TEST_ASSERT_FALSE(store.failed());
}

// mkdir alone succeeds on a full or read-only card (the directories exist): only the
// probe write shows that saves would fail.
static void test_prepare_fails_on_any_step() {
  for (long k = 0; k < 6; ++k) {
    FakeFatPort port;
    port.fail_call(k, FatErr::Other);
    SdStore store(port);
    TEST_ASSERT_FALSE_MESSAGE(store.prepare(), ("step " + std::to_string(k)).c_str());
    TEST_ASSERT_FALSE(store.failed());  // not a disk error: nothing latched
  }
  FakeFatPort port;
  port.fail_call(3, FatErr::Other);  // the probe's write: the file is still closed
  SdStore store(port);
  TEST_ASSERT_FALSE(store.prepare());
  TEST_ASSERT_EQUAL_INT(1, port.calls(FatCall::SyncClose));
}

static void test_items_live_under_the_root_in_83_names() {
  FakeFatPort port;
  SdStore store(port);
  TEST_ASSERT_TRUE(store.prepare());
  TEST_ASSERT_TRUE(put(store, kItem, "{\"x\":1}\n"));
  TEST_ASSERT_EQUAL_STRING("{\"x\":1}\n", port.get(kPhys).c_str());
  TEST_ASSERT_EQUAL_STRING("{\"x\":1}\n", read_all(store, kItem).c_str());
  TEST_ASSERT_TRUE(store.remove(kItem.c_str()));
  TEST_ASSERT_FALSE(port.has(kPhys));
  TEST_ASSERT_FALSE(store.remove(kItem.c_str()));
  TEST_ASSERT_EQUAL_STRING("<error>", read_all(store, kItem).c_str());
  TEST_ASSERT_FALSE(store.failed());  // NotFound is no disk error
}

static void test_another_root() {
  FakeFatPort port;
  SdStore store(port, "/cpatest");
  TEST_ASSERT_TRUE(store.prepare());
  TEST_ASSERT_TRUE(put(store, "/cfg/projects.json", "[]"));
  TEST_ASSERT_TRUE(port.has("/cpatest/cfg/projects.json"));
  TEST_ASSERT_FALSE(port.has_dir("/cpa"));
}

// The first item of an epoch (or of bad/) finds no directory: each level is created,
// then the open is tried once more.
static void test_open_creates_missing_parents_level_by_level() {
  FakeFatPort port;
  SdStore store(port);
  TEST_ASSERT_TRUE(put(store, std::string("/outbox/bad/") + kId + ".json", "b"));
  TEST_ASSERT_EQUAL_STRING("b", port.get("/cpa/outbox/BAD/1K3M9Q2/000420A1.JC").c_str());
  TEST_ASSERT_EQUAL_INT(2, port.calls(FatCall::OpenWrite));
  TEST_ASSERT_EQUAL_INT(4, port.calls(FatCall::Mkdir));  // /cpa, outbox, BAD, epoch
  const long before = port.calls(FatCall::Mkdir);
  TEST_ASSERT_TRUE(put(store, std::string("/outbox/bad/") + "1K3M9Q2-00042-0A1D.json", "c"));
  TEST_ASSERT_EQUAL_INT(before, port.calls(FatCall::Mkdir));  // the epoch exists now
}

static void test_open_gives_up_after_one_retry() {
  FakeFatPort port;
  port.fail_all(FatCall::OpenWrite, FatErr::NotFound);
  SdStore store(port);
  TEST_ASSERT_FALSE(store.open_write(kItem.c_str()));
  TEST_ASSERT_EQUAL_INT(2, port.calls(FatCall::OpenWrite));
  FakeFatPort port2;
  port2.fail_all(FatCall::Mkdir, FatErr::Other);
  SdStore store2(port2);
  TEST_ASSERT_FALSE(store2.open_write(kItem.c_str()));
  TEST_ASSERT_EQUAL_INT(1, port2.calls(FatCall::OpenWrite));
  TEST_ASSERT_EQUAL_INT(1, port2.calls(FatCall::Mkdir));
  TEST_ASSERT_FALSE(store2.write("x", 1));  // nothing open
  TEST_ASSERT_FALSE(store2.close());
}

// No rename on FAT, and no .tmp: both fail without a single call to the card.
static void test_rename_and_tmp_never_reach_the_card() {
  FakeFatPort port;
  SdStore store(port);
  TEST_ASSERT_FALSE(store.atomic_rename());
  TEST_ASSERT_FALSE(store.rename(kItem.c_str(), "/outbox/x.json"));
  const std::string tmp = std::string("/outbox/") + kId + ".tmp";
  TEST_ASSERT_FALSE(store.open_write(tmp.c_str()));
  TEST_ASSERT_FALSE(store.remove(tmp.c_str()));
  TEST_ASSERT_EQUAL_INT(-1, store.read(tmp.c_str(), nullptr, 0));
  TEST_ASSERT_EQUAL_INT(0, port.calls());
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, store.free_bytes());  // no FAT scan
}

static void test_list_outbox_folds_epochs_back_into_ids() {
  FakeFatPort port;
  port.put(kPhys, "a");
  port.put("/cpa/outbox/2ABCDEF/00001000.J1", "b");
  port.put("/cpa/outbox/1K3M9Q2/notes.txt", "c");    // not ours: reported as is
  port.put("/cpa/outbox/1K3M9Q2/000420a1.jc", "d");  // lower case: not ours either
  port.put("/cpa/outbox/readme.txt", "e");
  port.put("/cpa/outbox/BAD/1K3M9Q2/00001000.J2", "f");
  port.put_dir("/cpa/outbox/misc");
  SdStore store(port);
  Names names;
  TEST_ASSERT_TRUE(store.list("/outbox", names));
  TEST_ASSERT_EQUAL_INT(7, static_cast<int>(names.all.size()));
  TEST_ASSERT_TRUE(names.has(std::string(kId) + ".json"));
  TEST_ASSERT_TRUE(names.has("2ABCDEF-00001-0001.json"));
  TEST_ASSERT_TRUE(names.has("notes.txt") && names.has("000420a1.jc") && names.has("readme.txt"));
  TEST_ASSERT_TRUE(names.has("bad/") && names.has("misc/"));
  TEST_ASSERT_EQUAL_INT(0, port.calls_while_listing());  // one directory open at a time
  Names bad;
  TEST_ASSERT_TRUE(store.list("/outbox/bad", bad));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(bad.all.size()));
  TEST_ASSERT_TRUE(bad.has("1K3M9Q2-00001-0002.json"));
}

static void test_list_reads_at_most_eight_epochs() {
  FakeFatPort port;
  for (int i = 0; i < 9; ++i) port.put("/cpa/outbox/100000" + std::to_string(i) + "/00001000.J1", "x");
  SdStore store(port);
  Names names;
  TEST_ASSERT_TRUE(store.list("/outbox", names));
  TEST_ASSERT_EQUAL_INT(8, static_cast<int>(names.all.size()));
  TEST_ASSERT_EQUAL_INT(9, port.calls(FatCall::List));
}

static void test_list_other_directories_and_errors() {
  FakeFatPort port;
  port.put("/cpa/cfg/projects.json", "[]");
  SdStore store(port);
  Names names;
  TEST_ASSERT_TRUE(store.list("/cfg", names));
  TEST_ASSERT_TRUE(names.has("projects.json"));
  TEST_ASSERT_FALSE(store.list("/nothing", names));
  TEST_ASSERT_FALSE(store.list("/outbox", names));  // no /cpa/outbox yet
  TEST_ASSERT_FALSE(store.mkdir("/outbox/bad"));  // the parent is missing: NotFound
  TEST_ASSERT_TRUE(store.mkdir("/outbox") && store.mkdir("/outbox/bad") && store.mkdir("/outbox/bad"));
  TEST_ASSERT_TRUE(port.has_dir("/cpa/outbox/BAD"));
}

// A store holding nothing: what plan_storage's Store::None runs on.
static void test_null_store_fails_everything() {
  NullStore none;
  Names names;
  char buf[4];
  TEST_ASSERT_FALSE(none.open_write("/outbox/a.json") || none.write("x", 1) || none.close());
  TEST_ASSERT_FALSE(none.rename("/a", "/b") || none.remove("/a") || none.mkdir("/outbox"));
  TEST_ASSERT_EQUAL_INT(-1, none.read("/a", buf, sizeof buf));
  TEST_ASSERT_FALSE(none.list("/outbox", names));
  TEST_ASSERT_EQUAL_UINT32(0, none.free_bytes());
  TEST_ASSERT_TRUE(names.all.empty());
  TEST_ASSERT_TRUE(none.atomic_rename());  // FileStore's default, as LfsStore keeps it

}

static void test_an_invalid_root_maps_nothing() {
  FakeFatPort port;
  SdStore store(port, "/a-root-too-long");
  Names names;
  TEST_ASSERT_FALSE(store.prepare());
  TEST_ASSERT_FALSE(store.open_write(kItem.c_str()) || store.mkdir("/outbox") || store.list("/outbox", names));
  TEST_ASSERT_EQUAL_INT(0, port.calls());
}

// A session touching every method; it stops being useful once the store latched.
static void session(SdStore& s) {
  Names names;
  char buf[32];
  s.prepare();
  put(s, kItem, "one");
  s.read(kItem.c_str(), buf, sizeof buf);
  s.list("/outbox", names);
  s.mkdir("/outbox/bad");
  put(s, std::string("/outbox/bad/") + kId + ".json", "one");
  s.remove(kItem.c_str());
  s.list("/outbox/bad", names);
}

// Whatever call meets the first Disk error (card pulled, card broken), the store latches:
// every later call fails at once without reaching the port, and free_bytes() drops to 0.
static void test_first_disk_error_latches_and_spares_the_card() {
  long total = 0;
  {
    FakeFatPort clean;
    SdStore s(clean);
    session(s);
    TEST_ASSERT_FALSE(s.failed());
    total = clean.calls();
  }
  TEST_ASSERT_TRUE(total > 15);
  for (long k = 0; k < total; ++k) {
    FakeFatPort port;
    port.fail_call(k, FatErr::Disk);
    SdStore s(port);
    session(s);
    const std::string at = "disk error at call " + std::to_string(k);
    TEST_ASSERT_TRUE_MESSAGE(s.failed(), at.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(k + 1, port.calls(), at.c_str());  // nothing after the error
    session(s);
    TEST_ASSERT_EQUAL_INT_MESSAGE(k + 1, port.calls(), at.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, s.free_bytes());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_prepare_makes_the_root_and_probes_a_write);
  RUN_TEST(test_prepare_fails_on_any_step);
  RUN_TEST(test_items_live_under_the_root_in_83_names);
  RUN_TEST(test_another_root);
  RUN_TEST(test_open_creates_missing_parents_level_by_level);
  RUN_TEST(test_open_gives_up_after_one_retry);
  RUN_TEST(test_rename_and_tmp_never_reach_the_card);
  RUN_TEST(test_list_outbox_folds_epochs_back_into_ids);
  RUN_TEST(test_list_reads_at_most_eight_epochs);
  RUN_TEST(test_list_other_directories_and_errors);
  RUN_TEST(test_null_store_fails_everything);
  RUN_TEST(test_an_invalid_root_maps_nothing);
  RUN_TEST(test_first_disk_error_latches_and_spares_the_card);
  return UNITY_END();
}
