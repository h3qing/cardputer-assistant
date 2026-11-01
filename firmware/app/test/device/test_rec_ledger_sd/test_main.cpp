// SENT.L on the real card (docs/28 S1a, §9; docs/07 §11): RecEngine through VfsRecFilePort
// below the test root /cpatest (the real /cpa is never touched), synthetic PCM, no
// microphone. What the host tests cannot show, FatFs itself:
//   - a delivery appends one 28 B DELIV (O_APPEND, fsync, close) and keeps the files; three
//     appends through three opens read back whole; the next boot scan neither indexes the
//     kept ones nor reads their .M;
//   - a torn tail (bytes short of a record, as a cut write leaves them) is compacted at the
//     next boot into SENT.K (GEN + the live records), SENT.L unlinked, nothing lost;
//   - deliver again requeues the batch, never for that Mac; Clear leaves the GEN alone.
// Prints CPA_LEDGER scan_us=... for the boot scans (kept names skip their .M).
#include <Arduino.h>
#include <unity.h>

#include <string>

#include "device_harness.h"
#include "hal_sha256.h"
#include "rec_engine.h"
#include "../test_rec_budget/rec_harness.h"
#include "sd_card.h"
#include "sd_health.h"
#include "sd_store.h"
#include "usb_transport.h"
#include "vfs_rec_file_port.h"

using namespace cpa;
using namespace cpa::rec;

namespace {

constexpr uint32_t kMacA = 0xA0A0A0A0;
constexpr uint32_t kMacB = 0xB0B0B0B0;
constexpr uint32_t kWall = 1800000000;
constexpr int kMemos = 3;

cpa::UsbTransport g_usb;
cpa::VfsFatPort g_fat;
cpa::SdStore g_store(g_fat, rectest::kRoot);
cpa::VfsRecFilePort g_fs;
cpa::HalSha256 g_sha;
cpa::MonoCounter g_ids;
RecIndex g_boots[4];  // one index a "boot": never a RecIndex{} temporary (4 KB, docs/07 §6.7 #1)
uint8_t g_boot = 0;
TransferBuf g_xfer;
alignas(8) uint8_t g_scratch[4800];
uint8_t g_block[4096];
MonoId g_ids_made[kMemos];

struct Collect : EvtSink {
  uint16_t requeued = 0;
  RecEvt done;
  RecEvt kept;
  MonoId committed;
  void emit(const RecEvt& e) override {
    if (e.kind == EvtKind::Committed) committed = e.id;
    if (e.kind == EvtKind::Requeued) ++requeued;
    if (e.kind == EvtKind::DelDone || e.kind == EvtKind::LedgerDone) done = e;
    if (e.kind == EvtKind::Kept) kept = e;
  }
};

EngineDeps deps(RecIndex& idx) {
  EngineDeps d;
  d.fs = &g_fs;
  d.sha = &g_sha;
  d.ids = &g_ids;
  d.index = &idx;
  d.xfer = &g_xfer;
  d.crc = hal_crc32;
  d.root = rectest::kRoot;
  d.fw = FixedStr<31>::from("cpa-fw/test");
  return d;
}

std::string vfs(const RecPath& p) { return std::string("/sd") + p.c_str(); }
std::string ledger(uint8_t which) {
  MonoId e;
  e.epoch = rectest::kTestEpoch;
  return vfs(rec_epoch_dir(rectest::kRoot, e)) + "/" + ledger_name(which);
}

long size_of(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return -1;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fclose(f);
  return n;
}

// A fresh engine on a fresh index, its boot scan timed.
struct Boot {
  RecIndex& idx = g_boots[g_boot++ % 4];
  RecEngine engine{deps(idx)};
  Collect out;
  Boot() {
    const uint32_t t0 = micros();
    engine.scan(g_scratch, sizeof g_scratch, out);
    Serial.printf("CPA_LEDGER scan_us=%u kept=%u waiting=%u\n", static_cast<unsigned>(micros() - t0),
                  static_cast<unsigned>(engine.kept()), static_cast<unsigned>(idx.count()));
  }
};

MonoId memo(RecEngine& engine, uint32_t seed) {
  Collect out;
  StartSpec s;
  engine.start(s, millis(), millis(), out);
  for (int i = 0; i < 40; ++i) {  // 5.1 s
    for (size_t k = 0; k < sizeof g_block; ++k) g_block[k] = static_cast<uint8_t>((k * 31 + seed * 7 + i) & 0xFF);
    engine.write(g_block, sizeof g_block, millis(), out);
  }
  engine.stop(out);
  return out.committed;
}

RecEvt deliver(Boot& b, const MonoId& id, uint32_t host = kMacA) {
  const std::string wav = vfs(rec_wav_path(rectest::kRoot, id));
  RecCmd del;
  del.kind = CmdKind::Del;
  del.fid = id;
  del.size = static_cast<uint32_t>(size_of(wav));
  del.sha256 = some(Sha256Hex::from(rectest::sha256_of(wav, 44).c_str()));
  del.ledger.host = host;
  del.ledger.wall_s = kWall;
  b.engine.del(del, b.out);
  return b.out.done;
}

RecEvt ask(Boot& b, LedgerAsk a) {
  RecCmd c;
  c.kind = CmdKind::Ledger;
  c.ledger.ask = a;
  c.ledger.room = kIndexMax;
  b.out.requeued = 0;
  b.engine.ledger(c, g_scratch, sizeof g_scratch, b.out);
  return b.out.done;
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_mount_and_clean() {
  const SdFacts sd = hal_sd_mount(g_store);
  TEST_ASSERT_TRUE_MESSAGE(sd.state == SdState::Ok, "insert the FAT32 card");
  TEST_ASSERT_EQUAL_INT(0, rectest::remove_tree(rectest::kVfsRoot));
  const BootRecord rec{rectest::kTestEpoch, 1};
  g_ids.start(boot_ticket(rec, rec).value);
}

static void test_deliveries_append_and_keep() {
  Boot b;
  for (int i = 0; i < kMemos; ++i) g_ids_made[i] = memo(b.engine, static_cast<uint32_t>(i));
  for (int i = 0; i < kMemos; ++i) {
    const RecEvt d = deliver(b, g_ids_made[i]);
    TEST_ASSERT_TRUE(d.kind == EvtKind::DelDone && d.code == ErrCode::None && d.kept);
    TEST_ASSERT_TRUE(size_of(vfs(rec_wav_path(rectest::kRoot, g_ids_made[i]))) > 44);
    TEST_ASSERT_EQUAL_INT32((i + 1) * static_cast<long>(kLedgerRecBytes), size_of(ledger(0)));  // O_APPEND
  }
}

static void test_boot_scan_keeps_them_out_of_the_index() {
  Boot b;
  TEST_ASSERT_EQUAL_UINT16(kMemos, b.engine.kept());
  TEST_ASSERT_EQUAL_UINT16(0, b.idx.count());
  TEST_ASSERT_EQUAL_UINT16(kMemos, b.out.kept.info.batch_recs);
}

static void test_torn_tail_is_compacted_at_boot() {
  FILE* f = std::fopen(ledger(0).c_str(), "ab");
  TEST_ASSERT_NOT_NULL(f);
  const uint8_t torn[5] = {kLedgerMagic, 1, 0, 0, 7};
  TEST_ASSERT_EQUAL_UINT(5, std::fwrite(torn, 1, 5, f));
  std::fclose(f);
  Boot b;
  TEST_ASSERT_EQUAL_INT32(-1, size_of(ledger(0)));
  TEST_ASSERT_EQUAL_INT32((kMemos + 1) * static_cast<long>(kLedgerRecBytes), size_of(ledger(1)));
  TEST_ASSERT_EQUAL_UINT16(kMemos, b.engine.kept());
  TEST_ASSERT_EQUAL_UINT16(kMemos, b.out.kept.info.batch_recs);
}

// Two boots at once: static, not on the loop task's 8 KB stack (a Boot is 1.8 KB, and
// the scan and the replay below it left 1 KB: the first device run overflowed there).
static void test_deliver_again_then_clear() {
  static Boot b;
  const RecEvt again = ask(b, LedgerAsk::Again);
  TEST_ASSERT_TRUE(again.no == LedgerNo::None);
  TEST_ASSERT_EQUAL_UINT16(kMemos, b.out.requeued);
  static Boot after;
  TEST_ASSERT_EQUAL_UINT16(kMemos, after.idx.count());
  for (uint16_t i = 0; i < after.idx.count(); ++i) TEST_ASSERT_FALSE(after.idx.for_host(after.idx.at(i), kMacA));
  for (int i = 0; i < kMemos; ++i) TEST_ASSERT_TRUE(deliver(after, g_ids_made[i], kMacB).kept);  // the other Mac
  const RecEvt cleared = ask(after, LedgerAsk::Clear);
  TEST_ASSERT_TRUE(cleared.no == LedgerNo::None);
  for (int i = 0; i < kMemos; ++i) TEST_ASSERT_EQUAL_INT32(-1, size_of(vfs(rec_wav_path(rectest::kRoot, g_ids_made[i]))));
  const long l = size_of(ledger(0)) > 0 ? size_of(ledger(0)) : size_of(ledger(1));
  TEST_ASSERT_EQUAL_INT32(static_cast<long>(kLedgerRecBytes), l);  // the GEN alone
}

static void test_clean_up() {
  TEST_ASSERT_EQUAL_INT(0, rectest::remove_tree(rectest::kVfsRoot));
  ::rmdir(rectest::kVfsRoot);
  TEST_ASSERT_FALSE(hal_sd_dead());
}

void setup() {
  devtest::begin(g_usb);
  UNITY_BEGIN();
  RUN_TEST(test_mount_and_clean);
  RUN_TEST(test_deliveries_append_and_keep);
  RUN_TEST(test_boot_scan_keeps_them_out_of_the_index);
  RUN_TEST(test_torn_tail_is_compacted_at_boot);
  RUN_TEST(test_deliver_again_then_clear);
  RUN_TEST(test_clean_up);
  UNITY_END();
}

void loop() { devtest::idle(); }
