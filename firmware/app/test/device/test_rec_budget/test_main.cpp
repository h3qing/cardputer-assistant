// Memory and stack of recording (docs/11 §10, G-A6): the audio task with the buffers
// allocated at boot (G-A0's decision), below /cpatest. Pass:
//   idle, card primary:         free heap >= 150 KB, largest block >= 64 KB
//   recording:                   free heap >= 96 KB,  largest block >= 32 KB
//   transfer (file.get chunks):  free heap >= 128 KB
//   audio task stack never used >= 1.5 KB, the boot scan included: it starts with
//   recordings waiting (each .M decoded, two cut-off meetings recovered)
//   ledger commands (docs/28 S1a, docs/07 §11.1): 200 segments delivered and kept, deliver
//   again (one AGAIN each, Requeued), delivered to another Mac, cleared (kDropMax
//   recordings a command): the audio task stack never used stays >= 1 KB
// Prints CPA_REC_BUDGET and CPA_REC_LEDGER with every number. Needs the card.
#include <Arduino.h>
#include <M5Cardputer.h>
#include <unity.h>

#include <string>

#include "audio_task.h"
#include "hal_sha256.h"
#include "device_harness.h"
#include "i2c_lock.h"
#include "rec_harness.h"  // shared with test_rec_sd and test_rec_rotation
#include "sd_card.h"
#include "sd_store.h"
#include "usb_transport.h"
#include "vfs_rec_file_port.h"

using namespace cpa;
using namespace cpa::rec;

namespace {

cpa::UsbTransport g_usb;
cpa::VfsFatPort g_fat;
cpa::SdStore g_store(g_fat, rectest::kRoot);
cpa::MonoCounter g_ids;
cpa::AudioTask g_audio;

struct Heap {
  uint32_t free = 0;
  uint32_t block = 0;
};

// What waits on the card at boot: memos and meetings with stars, written before the audio
// task starts (its scan decodes each .M), and two meetings cut off before their .M.
constexpr uint32_t kWaitSegMs = 250;
constexpr int kWaitMemos = 3;
constexpr int kWaitBlocks = 32;  // 4 KB each: 4.1 s, 17 segments of 250 ms a recording
constexpr int kCutOffs = 2;
cpa::VfsRecFilePort g_wait_fs;
cpa::HalSha256 g_wait_sha;
RecIndex g_wait_index;
TransferBuf g_wait_xfer;
uint8_t g_wait_block[4096];

EngineDeps wait_deps() {
  EngineDeps d;
  d.fs = &g_wait_fs;
  d.sha = &g_wait_sha;
  d.ids = &g_ids;
  d.index = &g_wait_index;
  d.xfer = &g_wait_xfer;
  d.crc = hal_crc32;
  d.root = rectest::kRoot;
  d.fw = FixedStr<31>::from("cpa-fw/test");
  return d;
}

struct Quiet : EvtSink {
  uint16_t committed = 0;
  void emit(const RecEvt& e) override { committed = static_cast<uint16_t>(committed + (e.kind == EvtKind::Committed)); }
};

// One recording of kWaitBlocks; cut: the power goes before its .M (as test_rec_sd does it).
uint16_t write_waiting(RecMode mode, bool cut) {
  RecEngine engine(wait_deps());
  engine.set_seg_ms(kWaitSegMs);
  Quiet out;
  StartSpec s;
  s.mode = mode;
  s.consent = mode == RecMode::Meeting;
  s.project = some(ProjectKey::from("cpa"));
  const uint32_t t0 = millis();
  engine.start(s, t0, t0, out);
  for (int i = 0; i < kWaitBlocks; ++i) {
    for (size_t k = 0; k < sizeof g_wait_block; ++k) g_wait_block[k] = static_cast<uint8_t>((k * 13 + i) & 0xFF);
    engine.write(g_wait_block, sizeof g_wait_block, t0 + 128 * (i + 1), out);
    if (mode == RecMode::Meeting && i % 4 == 1) engine.mark();
  }
  if (cut) {
    g_wait_fs.close();
    return out.committed;
  }
  engine.stop(out);
  return out.committed;
}

Heap heap_now() { return Heap{ESP.getFreeHeap(), ESP.getMaxAllocHeap()}; }

Heap g_idle, g_rec, g_xfer;
MonoId g_seg;
uint32_t g_size = 0;

// The ledger item: kLedgerRecs recordings of kLedgerSegMs segments, about 200 segments.
constexpr uint32_t kMacA = 0xA0A0A0A0;
constexpr uint32_t kMacB = 0xB0B0B0B0;
constexpr uint32_t kWall = 1800000000;
constexpr int kLedgerRecs = 25;
constexpr uint32_t kLedgerSegMs = 500;
constexpr uint32_t kLedgerRecMs = 3800;  // 8 segments a recording
constexpr uint16_t kLedgerSegsMax = 240;
MonoId g_segs[kLedgerSegsMax];
uint16_t g_segs_n = 0;

std::string wav_of(const MonoId& id) { return std::string("/sd") + rec_wav_path(rectest::kRoot, id).c_str(); }

long size_of(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return -1;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fclose(f);
  return n;
}

void ignore(const RecEvt&) {}

// One recording of kLedgerRecMs, its committed segments appended to g_segs.
void record_segments() {
  auto collect = [](const RecEvt& ev) {
    if (ev.kind == EvtKind::Committed && g_segs_n < kLedgerSegsMax) g_segs[g_segs_n++] = ev.id;
  };
  RecCmd start;
  start.kind = CmdKind::Start;
  TEST_ASSERT_TRUE(g_audio.post(start));
  RecEvt e;
  TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::Started, 5000, e, collect));
  const uint32_t t0 = millis();
  while (millis() - t0 < kLedgerRecMs) rectest::wait_for(g_audio, EvtKind::None, 20, e, collect);
  RecCmd stop;
  stop.kind = CmdKind::Stop;
  TEST_ASSERT_TRUE(g_audio.post(stop));
  TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::Saved, 10000, e, collect));
}

// Every segment of g_segs delivered to `host` and kept; info: Kept after the last one.
void deliver_all(uint32_t host, KeptInfo& info) {
  for (uint16_t i = 0; i < g_segs_n; ++i) {
    const std::string wav = wav_of(g_segs[i]);
    RecCmd del;
    del.kind = CmdKind::Del;
    del.fid = g_segs[i];
    del.size = static_cast<uint32_t>(size_of(wav));
    del.sha256 = some(Sha256Hex::from(rectest::sha256_of(wav, 44).c_str()));
    del.ledger.host = host;
    del.ledger.wall_s = kWall + i;
    TEST_ASSERT_TRUE(g_audio.post(del));
    RecEvt e;
    TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::DelDone, 5000, e, ignore));
    TEST_ASSERT_TRUE(e.code == ErrCode::None && e.kept);
    TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::Kept, 2000, e, ignore));
    info = e.info;
  }
}

// One Ledger command; done: its LedgerDone, the Requeued events counted.
void ledger_cmd(LedgerAsk ask, RecEvt& done, uint16_t& requeued) {
  RecCmd c;
  c.kind = CmdKind::Ledger;
  c.ledger.ask = ask;
  c.ledger.room = kIndexMax;
  c.ledger.wall_s = kWall + 1000;
  TEST_ASSERT_TRUE(g_audio.post(c));
  auto count = [&requeued](const RecEvt& ev) { requeued += ev.kind == EvtKind::Requeued ? 1 : 0; };
  TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::LedgerDone, 30000, done, count));
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_boot_and_idle() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;
  M5Cardputer.begin(cfg, true);
  hal_i2c_lock_begin();
  TEST_ASSERT_TRUE_MESSAGE(hal_sd_mount(g_store).state == SdState::Ok, "insert the FAT32 card");
  TEST_ASSERT_EQUAL_INT(0, rectest::remove_tree(rectest::kVfsRoot));
  const BootRecord rec{rectest::kTestEpoch, 3};
  g_ids.start(boot_ticket(rec, rec).value);
  uint16_t waiting = 0;
  for (int i = 0; i < kWaitMemos; ++i) waiting = static_cast<uint16_t>(waiting + write_waiting(RecMode::Memo, false));
  waiting = static_cast<uint16_t>(waiting + write_waiting(RecMode::Meeting, false));
  for (int i = 0; i < kCutOffs; ++i) waiting = static_cast<uint16_t>(waiting + write_waiting(RecMode::Meeting, true));
  TEST_ASSERT_TRUE(g_audio.prepare(true).buffers);
  g_audio.start(g_ids, FixedStr<31>::from("cpa-fw/test"), rectest::kRoot);
  RecEvt e;
  TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::IndexReady, 20000, e, [](const RecEvt&) {}));
  const uint32_t scan_stack_left = g_audio.stack_left();
  Serial.printf("CPA_BOOT_SCAN waiting=%u indexed=%u audio_stack_left=%lu\n", waiting, e.n,
                static_cast<unsigned long>(scan_stack_left));
  TEST_ASSERT_TRUE(waiting >= 20 && e.n >= waiting + kCutOffs);  // the cut-off ones recovered
  TEST_ASSERT_TRUE_MESSAGE(scan_stack_left >= 1536, "the boot scan leaves the audio task 1.5 KB");
  g_idle = heap_now();
  TEST_ASSERT_TRUE(g_idle.free >= 150u * 1024u);
  TEST_ASSERT_TRUE(g_idle.block >= 64u * 1024u);
}

static void test_recording() {
  RecCmd start;
  start.kind = CmdKind::Start;
  TEST_ASSERT_TRUE(g_audio.post(start));
  Heap low = heap_now();
  auto watch = [&](const RecEvt& ev) {
    const Heap h = heap_now();
    low.free = h.free < low.free ? h.free : low.free;
    low.block = h.block < low.block ? h.block : low.block;
    if (ev.kind == EvtKind::Committed) {
      g_seg = ev.id;
      g_size = ev.size;
    }
  };
  RecEvt e;
  TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::Started, 5000, e, watch));
  const uint32_t t0 = millis();
  while (millis() - t0 < 3000) watch(RecEvt{});
  RecCmd stop;
  stop.kind = CmdKind::Stop;
  TEST_ASSERT_TRUE(g_audio.post(stop));
  TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::Saved, 10000, e, watch));
  g_rec = low;
  TEST_ASSERT_TRUE(g_rec.free >= 96u * 1024u);
  TEST_ASSERT_TRUE(g_rec.block >= 32u * 1024u);
}

static void test_transfer_and_stack() {
  TEST_ASSERT_TRUE(g_size > 44);
  Heap low = heap_now();
  for (uint32_t off = 0; off < g_size; off += 1380) {
    RecCmd get;
    get.kind = CmdKind::Get;
    get.fid = g_seg;
    get.size = g_size;
    get.off = off;
    get.len = 1380;
    TEST_ASSERT_TRUE(g_audio.post(get));
    RecEvt e;
    TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::Data, 2000, e, [](const RecEvt&) {}));
    TEST_ASSERT_TRUE(e.code == ErrCode::None);
    const Heap h = heap_now();
    low.free = h.free < low.free ? h.free : low.free;
  }
  g_xfer = low;
  const uint32_t stack_left = g_audio.stack_left();
  Serial.printf("CPA_REC_BUDGET idle_free=%lu idle_block=%lu rec_free=%lu rec_block=%lu xfer_free=%lu "
                "audio_stack_left=%lu ring_high=%u\n",
                static_cast<unsigned long>(g_idle.free), static_cast<unsigned long>(g_idle.block),
                static_cast<unsigned long>(g_rec.free), static_cast<unsigned long>(g_rec.block),
                static_cast<unsigned long>(g_xfer.free), static_cast<unsigned long>(stack_left),
                g_audio.ring_high_water());
  TEST_ASSERT_TRUE(g_xfer.free >= 128u * 1024u);
  TEST_ASSERT_TRUE(stack_left >= 1536);
}

static void test_ledger_commands_stack() {
  RecCmd seg;
  seg.kind = CmdKind::SegMs;
  seg.seg_ms = kLedgerSegMs;
  TEST_ASSERT_TRUE(g_audio.post(seg));
  for (int i = 0; i < kLedgerRecs; ++i) record_segments();
  TEST_ASSERT_TRUE(g_segs_n >= 180);
  KeptInfo kept;
  deliver_all(kMacA, kept);
  TEST_ASSERT_EQUAL_UINT16(g_segs_n, kept.segs);
  TEST_ASSERT_EQUAL_UINT16(g_segs_n, kept.batch_segs);
  uint16_t requeued = 0;
  uint32_t t0 = micros();
  RecEvt again;
  ledger_cmd(LedgerAsk::Again, again, requeued);
  const uint32_t again_us = micros() - t0;
  TEST_ASSERT_TRUE(again.no == LedgerNo::None);
  TEST_ASSERT_EQUAL_UINT16(g_segs_n, requeued);
  deliver_all(kMacB, kept);
  TEST_ASSERT_EQUAL_UINT16(g_segs_n, kept.segs);
  uint16_t recs = 0;
  uint8_t cmds = 0;
  t0 = micros();
  for (bool more = true; more && cmds < 10; ++cmds) {
    RecEvt done;
    ledger_cmd(LedgerAsk::Clear, done, requeued);
    TEST_ASSERT_TRUE(done.no == LedgerNo::None);
    recs = static_cast<uint16_t>(recs + done.n);
    more = done.first;
  }
  const uint32_t clear_us = micros() - t0;
  for (uint16_t i = 0; i < g_segs_n; ++i) TEST_ASSERT_EQUAL_INT32(-1, size_of(wav_of(g_segs[i])));
  const uint32_t stack_left = g_audio.stack_left();
  Serial.printf("CPA_REC_LEDGER segs=%u recs=%u requeued=%u again_us=%lu clear_cmds=%u clear_us=%lu "
                "audio_stack_left=%lu heap_free=%lu\n",
                g_segs_n, recs, requeued, static_cast<unsigned long>(again_us), cmds,
                static_cast<unsigned long>(clear_us), static_cast<unsigned long>(stack_left),
                static_cast<unsigned long>(ESP.getFreeHeap()));
  TEST_ASSERT_EQUAL_UINT16(kLedgerRecs, recs);
  TEST_ASSERT_TRUE(stack_left >= 1024);
}

static void test_clean_up() {
  TEST_ASSERT_EQUAL_INT(0, rectest::remove_tree(rectest::kVfsRoot));
  ::rmdir(rectest::kVfsRoot);
}

void setup() {
  devtest::begin(g_usb);
  UNITY_BEGIN();
  RUN_TEST(test_boot_and_idle);
  RUN_TEST(test_recording);
  RUN_TEST(test_transfer_and_stack);
  RUN_TEST(test_ledger_commands_stack);
  RUN_TEST(test_clean_up);
  UNITY_END();
}

void loop() { devtest::idle(); }
