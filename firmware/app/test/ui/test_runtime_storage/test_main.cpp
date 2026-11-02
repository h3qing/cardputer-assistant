// The runtime on v0.2 storage (docs/09-m2-design.md §3.1, §3.6-§3.8, §6, §8):
//   - no usable store: saving answers NoStorage before Full and never touches storage;
//   - with a migration source: cpafs's items move to the primary store before its scan;
//   - hello carries store and launcher;
//   - on FAT the projects cache is written in place, never renamed;
//   - a failed save, outbox_apply or cache write counts one storage error;
//   - an SD card that latched during boot (scan or migration) is a storage error, not
//     "full": saving fails, says so ("SD card error: reseat it, restart", docs/20 U16) and the
//     system page asks for a restart;
//   - the boot migration kicks USB and shows its progress after 500 ms.
#include <unity.h>

#include <string>

#include "core/support/fake_fat_port.h"
#include "core/support/fake_store.h"
#include "core/support/lang_pin.h"
#include "migrate_screen.h"
#include "runtime.h"
#include "sd_store.h"
#include "system_logic.h"
#include "support/fake_io.h"
#include "support/fake_port.h"
#include "support/fixtures.h"
#include "support/runtime_rig.h"

using namespace cpa;
using namespace uitest;
using testsupport::FakeFatPort;
using testsupport::FakeStore;
using testsupport::FatCall;

void setUp() { testsupport::pin_zh(); }
void tearDown() {}

// FileStore that counts every call and fails all of them (NullStore with a counter).
class CountingStore : public FileStore {
 public:
  int calls = 0;
  bool open_write(const char*) override { return ++calls < 0; }
  bool write(const char*, size_t) override { return ++calls < 0; }
  bool close() override { return ++calls < 0; }
  bool rename(const char*, const char*) override { return ++calls < 0; }
  bool remove(const char*) override { return ++calls < 0; }
  long read(const char*, char*, size_t) override { return ++calls < 0 ? 0 : -1; }
  bool list(const char*, DirVisitor&) override { return ++calls < 0; }
  bool mkdir(const char*) override { return ++calls < 0; }
  uint32_t free_bytes() override { return ++calls < 0 ? 1 : 0; }
};

class Progress : public MigrateObserver {
 public:
  int calls = 0;
  uint16_t last_done = 0;
  uint16_t last_total = 0;
  void on_item(uint16_t done, uint16_t total) override {
    ++calls;
    last_done = done;
    last_total = total;
  }
};

// A runtime over any store, begun with the given storage facts.
struct StoreRig {
  FakeTransport tx;
  FakePlatform plat;
  FakeDisplay disp;
  ProbeModule board{"board"};
  cpa::Module* mods[1] = {&board};
  Runtime rt;
  uint32_t now = 0;

  explicit StoreRig(FileStore& fs) : rt(tx, fs, plat, disp, mods, 1) {}

  void begin(const StorageBoot& sb, uint32_t boot = 12) {
    BootInfo b;
    b.ticket = test_ticket(boot);
    b.dev = FixedStr<12>::from("020000000001");
    b.rst = FixedStr<16>::from("software");
    rt.begin(b, now, sb);
  }

  void step() {
    now += 10;
    rt.loop(now, KeySet{});
  }

  void host(const std::string& json) {
    tx.host(json);
    step();
  }

  void handshake(const char* project_name = "Cardputer 助手") {
    host("{\"v\":0,\"t\":\"hello\",\"id\":\"h1\",\"role\":\"host\",\"app\":\"cpad/0.2.0\",\"agent\":\"ok\","
         "\"caps\":{\"spawn\":true,\"reply\":false}}");
    host(std::string("{\"v\":0,\"t\":\"projects.snapshot\",\"id\":\"h2\",\"rev\":3,\"projects\":[{\"slot\":1,"
                     "\"key\":\"cpa\",\"name\":\"") +
         project_name + "\",\"run\":0,\"need\":0,\"queued\":0,\"ok\":true}],\"other\":{\"run\":0,\"n\":0}}");
  }
};

static StorageBoot plan_of(Store primary, StoreNote note = StoreNote::None) {
  StorageBoot sb;
  sb.plan.primary = primary;
  sb.plan.note = note;
  return sb;
}

static NoteBody note(const char* text) {
  NoteBody b;
  b.text = UserText::from(text);
  return b;
}

static void test_no_storage_refuses_before_full_without_touching_the_store() {
  CountingStore store;
  StoreRig r(store);
  r.begin(plan_of(Store::None, StoreNote::NoStorage));
  TEST_ASSERT_TRUE(r.rt.sys().store == Store::None);
  TEST_ASSERT_TRUE(r.rt.sys().note == StoreNote::NoStorage);
  store.calls = 0;
  TEST_ASSERT_TRUE(r.rt.save_note(note("hello")) == SaveResult::NoStorage);  // free_bytes() is 0: not Full
  QueueBody q;
  q.text = UserText::from("later");
  q.project = ProjectKey::from("cpa");
  TEST_ASSERT_TRUE(r.rt.save_queue(q) == SaveResult::NoStorage);
  r.handshake();  // the projects cache is not written either
  TEST_ASSERT_EQUAL_INT(0, store.calls);
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.sys().store_errors);
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.state().outbox.pending);
}

static void test_compose_says_why_nothing_was_saved() {
  CountingStore store;
  StoreRig r(store);
  r.begin(plan_of(Store::None, StoreNote::NoStorage));
  r.rt.shell().on_key(g0tap(), 10);
  r.rt.shell().on_key(chr('x'), 20);
  r.rt.shell().on_key(enter(), 30);
  TEST_ASSERT_TRUE(r.rt.shell().overlay() == proto::Overlay::Compose);  // the text stays
  FakeCanvas c;
  r.rt.shell().render(c, 40);
  TEST_ASSERT_TRUE(c.has("没有可用存储：请插入 FAT32 SD 卡"));
}

// Two notes saved on the internal store by an earlier boot.
static void seed_internal(FakeStore& lfs) {
  StoreRig old(lfs);
  old.begin(StorageBoot{}, 7);
  TEST_ASSERT_TRUE(old.rt.save_note(note("one")) == SaveResult::Saved);
  TEST_ASSERT_TRUE(old.rt.save_note(note("two")) == SaveResult::Saved);
  TEST_ASSERT_EQUAL_UINT32(2, lfs.count_in("/outbox", ".json"));
}

static void test_migration_runs_before_the_primary_scan() {
  FakeStore lfs;
  seed_internal(lfs);
  FakeStore sd;
  sd.set_fat(true);
  Progress progress;
  StorageBoot sb = plan_of(Store::Sd);
  sb.plan.migrate = true;
  sb.migrate_from = &lfs;
  sb.observer = &progress;
  StoreRig r(sd);
  r.begin(sb);
  TEST_ASSERT_EQUAL_UINT32(0, lfs.count_in("/outbox", ".json"));
  TEST_ASSERT_EQUAL_UINT32(2, sd.count_in("/outbox", ".json"));
  TEST_ASSERT_EQUAL_UINT16(2, r.rt.outbox().n);  // the primary scan saw the moved items
  TEST_ASSERT_EQUAL_UINT16(2, r.rt.state().outbox.pending);
  TEST_ASSERT_EQUAL_UINT16(2, r.rt.sys().migrated);
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.sys().migrate_failed);
  TEST_ASSERT_EQUAL_INT(2, progress.calls);
  TEST_ASSERT_EQUAL_UINT16(2, progress.last_done);
  TEST_ASSERT_EQUAL_UINT16(2, progress.last_total);
  TEST_ASSERT_EQUAL_INT(0, sd.renames());
}

static void test_migration_without_observer_and_failures_are_counted() {
  FakeStore lfs;
  seed_internal(lfs);
  FakeStore sd;
  sd.set_fat(true);
  sd.crash_before(0);  // the card refuses every write
  StorageBoot sb = plan_of(Store::Sd);
  sb.plan.migrate = true;
  sb.migrate_from = &lfs;
  StoreRig r(sd);
  r.begin(sb);
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.sys().migrated);
  TEST_ASSERT_EQUAL_UINT16(2, r.rt.sys().migrate_failed);
  TEST_ASSERT_EQUAL_UINT32(2, lfs.count_in("/outbox", ".json"));  // kept at the source
}

static void test_hello_carries_store_and_launcher() {
  FakeStore sd;
  sd.set_fat(true);
  StorageBoot sb = plan_of(Store::Sd);
  sb.part.launcher = true;
  StoreRig r(sd);
  r.begin(sb);
  r.handshake();
  const auto hello = r.tx.of_type("hello");
  TEST_ASSERT_EQUAL_UINT32(1, hello.size());
  const auto& h = std::get<proto::Hello>(hello[0].msg.body).device;
  TEST_ASSERT_TRUE(h.store.has && h.store.value == proto::StoreKind::Sd);
  TEST_ASSERT_TRUE(h.launcher.has && h.launcher.value);
  TEST_ASSERT_TRUE(r.tx.all_valid());

  RuntimeRig v01;  // the default boot: internal store, full flash
  v01.handshake();
  const auto v01_hello = v01.tx.of_type("hello");
  const auto& d = std::get<proto::Hello>(v01_hello[0].msg.body).device;
  TEST_ASSERT_TRUE(d.store.has && d.store.value == proto::StoreKind::Int);
  TEST_ASSERT_TRUE(d.launcher.has && !d.launcher.value);

  CountingStore none;
  StoreRig n(none);
  n.begin(plan_of(Store::None, StoreNote::NoStorage));
  n.handshake();
  const auto none_hello = n.tx.of_type("hello");
  const auto& z = std::get<proto::Hello>(none_hello[0].msg.body).device;
  TEST_ASSERT_TRUE(z.store.has && z.store.value == proto::StoreKind::None);
}

static void test_projects_cache_on_fat_is_never_renamed() {
  FakeStore sd;
  sd.set_fat(true);
  StoreRig r(sd);
  r.begin(plan_of(Store::Sd));
  r.handshake();
  TEST_ASSERT_TRUE(sd.has("/cfg/projects.json"));
  TEST_ASSERT_FALSE(sd.has("/cfg/projects.tmp"));
  TEST_ASSERT_EQUAL_INT(0, sd.renames());
  TEST_ASSERT_TRUE(sd.get("/cfg/projects.json").find("\"cpa\"") != std::string::npos);

  RuntimeRig lfs;  // LittleFS keeps v0.1's tmp -> rename
  lfs.handshake();
  TEST_ASSERT_TRUE(lfs.fs.has("/cfg/projects.json"));
  TEST_ASSERT_EQUAL_INT(1, lfs.fs.renames());
}

static void test_failed_writes_count_store_errors() {
  RuntimeRig r;
  r.handshake();
  TEST_ASSERT_TRUE(r.rt.save_note(note("kept")) == SaveResult::Saved);
  r.step();  // sent
  const std::string id = r.last_id_of("outbox.item");
  TEST_ASSERT_FALSE(id.empty());
  TEST_ASSERT_EQUAL_UINT16(0, r.rt.sys().store_errors);
  r.fs.crash_before(r.fs.ops());  // the store fails from here on
  r.reply("outbox.ack", id, "\"status\":\"stored\"");
  r.step();  // outbox_apply: remove fails
  TEST_ASSERT_EQUAL_UINT16(1, r.rt.sys().store_errors);
  TEST_ASSERT_TRUE(r.rt.save_note(note("lost")) == SaveResult::Failed);
  TEST_ASSERT_EQUAL_UINT16(2, r.rt.sys().store_errors);
  r.send("projects.snapshot",
         "\"rev\":4,\"projects\":[{\"slot\":1,\"key\":\"cpa\",\"name\":\"改名\",\"run\":0,\"need\":0,"
         "\"queued\":0,\"ok\":true}],\"other\":{\"run\":0,\"n\":0}");
  r.step();  // the cache write fails
  TEST_ASSERT_EQUAL_UINT16(3, r.rt.sys().store_errors);
}

static void test_storage_facts_reach_the_system_info() {
  FakeStore sd;
  sd.set_fat(true);
  StorageBoot sb = plan_of(Store::Sd, StoreNote::NoInternal);
  sb.sd.state = SdState::Ok;
  sb.sd.size_mib = 488281;
  sb.part.launcher = true;
  sb.part.cpafs_bytes = 0;
  StoreRig r(sd);
  r.begin(sb);
  const SysInfo& s = r.rt.sys();
  TEST_ASSERT_TRUE(s.store == Store::Sd);
  TEST_ASSERT_TRUE(s.sd == SdState::Ok);
  TEST_ASSERT_EQUAL_UINT32(488281, s.sd_mib);
  TEST_ASSERT_TRUE(s.launcher);
  TEST_ASSERT_EQUAL_UINT32(0, s.cpafs_bytes);
  TEST_ASSERT_TRUE(s.note == StoreNote::NoInternal);
  TEST_ASSERT_TRUE(s.fw.equals("cpa-fw/0.5.2"));
}

static bool page_has(const SystemPage& p, const char* text) {
  for (uint8_t i = 0; i < p.n; ++i) {
    if (std::string(p.lines[i].c_str()).find(text) != std::string::npos) return true;
  }
  return false;
}

// The card mounted and passed prepare(), then failed during the boot scan (pulled, worn,
// or an EIO from a FAT left inconsistent by power loss): SdStore latches and its
// free_bytes() turns 0. That is a storage error, not a full outbox (09 §3.8, G7).
static void test_sd_latched_at_boot_is_a_storage_error_not_full() {
  FakeFatPort port;
  SdStore sd(port);
  TEST_ASSERT_TRUE(sd.prepare());
  port.fail_all(FatCall::List, FatErr::Disk);
  StoreRig r(sd);
  r.begin(plan_of(Store::Sd));
  TEST_ASSERT_TRUE(sd.failed());
  TEST_ASSERT_TRUE(r.rt.sys().store_errors >= 1);
  TEST_ASSERT_TRUE(page_has(system_page(r.rt.state(), r.rt.sys()), "存储错误"));
  const uint16_t before = r.rt.sys().store_errors;
  // docs/20 U16: a latched card says so (and how to recover), not E_STORAGE
  TEST_ASSERT_TRUE(r.rt.save_note(note("lost")) == SaveResult::CardLost);  // not Full
  TEST_ASSERT_TRUE(r.rt.sys().store_errors > before);
  r.rt.shell().on_key(g0tap(), 10);
  r.rt.shell().on_key(chr('x'), 20);
  r.rt.shell().on_key(enter(), 30);
  FakeCanvas c;
  r.rt.shell().render(c, 40);
  TEST_ASSERT_TRUE(c.has("SD 卡出错：插好后需重启"));
  TEST_ASSERT_FALSE(c.has("存储已满"));
}

// The boot migration screen (09 §3.1, §8): every item kicks USB; "Moving to the SD card i/n"
// appears once the migration has run 500 ms, redrawn at most every 200 ms and always
// for the last item.
static uint32_t g_clock = 0;
static uint32_t fake_clock() { return g_clock; }

static void test_migrate_screen_kicks_usb_and_shows_progress_after_500ms() {
  FakeTransport tx;
  FakeDisplay disp;
  g_clock = 1000;
  MigrateScreen screen(tx, disp, fake_clock);
  g_clock = 1040;
  screen.on_item(1, 10);
  g_clock = 1499;
  screen.on_item(2, 10);
  TEST_ASSERT_EQUAL_UINT32(2, tx.ticks);
  TEST_ASSERT_EQUAL_INT(0, disp.frames);
  g_clock = 1500;
  screen.on_item(3, 10);
  TEST_ASSERT_EQUAL_INT(1, disp.frames);
  TEST_ASSERT_TRUE(disp.canvas_.has("正在迁移到 SD 卡 3/10"));
  g_clock = 1650;
  screen.on_item(4, 10);  // drawn 150 ms ago: not again yet
  TEST_ASSERT_EQUAL_INT(1, disp.frames);
  g_clock = 1700;
  screen.on_item(5, 10);
  TEST_ASSERT_EQUAL_INT(2, disp.frames);
  TEST_ASSERT_TRUE(disp.canvas_.has("5/10"));
  g_clock = 1710;
  screen.on_item(10, 10);  // the last one is always shown
  TEST_ASSERT_EQUAL_INT(3, disp.frames);
  TEST_ASSERT_TRUE(disp.canvas_.has("10/10"));
  TEST_ASSERT_EQUAL_UINT32(6, tx.ticks);  // one kick per item
}

static void test_short_migration_never_draws() {
  FakeTransport tx;
  FakeDisplay disp;
  g_clock = 0xFFFFFF00u;  // across the millis() wrap
  MigrateScreen screen(tx, disp, fake_clock);
  g_clock = 0x00000100u;  // 512 ms later
  screen.on_item(1, 1);
  TEST_ASSERT_EQUAL_INT(1, disp.frames);
  FakeDisplay quiet;
  g_clock = 50;
  MigrateScreen fast(tx, quiet, fake_clock);
  g_clock = 90;
  fast.on_item(1, 1);
  TEST_ASSERT_EQUAL_INT(0, quiet.frames);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_migrate_screen_kicks_usb_and_shows_progress_after_500ms);
  RUN_TEST(test_short_migration_never_draws);
  RUN_TEST(test_no_storage_refuses_before_full_without_touching_the_store);
  RUN_TEST(test_compose_says_why_nothing_was_saved);
  RUN_TEST(test_migration_runs_before_the_primary_scan);
  RUN_TEST(test_migration_without_observer_and_failures_are_counted);
  RUN_TEST(test_hello_carries_store_and_launcher);
  RUN_TEST(test_projects_cache_on_fat_is_never_renamed);
  RUN_TEST(test_failed_writes_count_store_errors);
  RUN_TEST(test_storage_facts_reach_the_system_info);
  RUN_TEST(test_sd_latched_at_boot_is_a_storage_error_not_full);
  return UNITY_END();
}
