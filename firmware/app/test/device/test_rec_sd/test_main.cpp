// On the card (docs/11 §4.5-§4.6, §11): RecEngine through VfsRecFilePort and the hardware
// SHA, below the test root /cpatest (the real /cpa is never touched). Synthetic PCM, no
// microphone:
//   - a memo session commits .W and .M; the .M's sha256 is the SHA-256 of the data on
//     the card; the WAV header carries the data length;
//   - a session cut off before its commit (the descriptor dropped, as a power cut leaves
//     it) is recovered by the next boot scan: rcv, a rewritten header, indexed;
//   - file.get reads back the same bytes with the zlib CRC; file.del removes both files.
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
#include "wav_header.h"

using namespace cpa;
using namespace cpa::rec;

namespace {

cpa::UsbTransport g_usb;
cpa::VfsFatPort g_fat;
cpa::SdStore g_store(g_fat, rectest::kRoot);
cpa::VfsRecFilePort g_fs;
cpa::HalSha256 g_sha;
cpa::MonoCounter g_ids;
RecIndex g_index;
TransferBuf g_xfer;
alignas(8) uint8_t g_scratch[4800];
uint8_t g_block[4096];

struct Collect : EvtSink {
  RecEvt last[8];
  uint8_t n = 0;
  MonoId committed;
  uint32_t committed_size = 0;
  void emit(const RecEvt& e) override {
    if (e.kind == EvtKind::Committed) {
      committed = e.id;
      committed_size = e.size;
    }
    last[n % 8] = e;
    ++n;
  }
  bool saw(EvtKind k) const {
    for (uint8_t i = 0; i < 8 && i < n; ++i) {
      if (last[i].kind == k) return true;
    }
    return false;
  }
};

EngineDeps deps() {
  EngineDeps d;
  d.fs = &g_fs;
  d.sha = &g_sha;
  d.ids = &g_ids;
  d.index = &g_index;
  d.xfer = &g_xfer;
  d.crc = hal_crc32;
  d.root = rectest::kRoot;
  d.fw = FixedStr<31>::from("cpa-fw/test");
  return d;
}

void fill(uint32_t seed) {
  for (size_t i = 0; i < sizeof g_block; ++i) g_block[i] = static_cast<uint8_t>((i * 31 + seed * 7) & 0xFF);
}

std::string vfs(const RecPath& p) { return std::string("/sd") + p.c_str(); }

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

static void test_memo_commits_with_the_hardware_sha() {
  RecEngine engine(deps());
  Collect out;
  StartSpec s;
  s.project = some(ProjectKey::from("cpa"));
  engine.start(s, millis(), millis(), out);
  TEST_ASSERT_TRUE(engine.recording());
  for (int i = 0; i < 40; ++i) {  // 5.1 s
    fill(i);
    engine.write(g_block, sizeof g_block, millis(), out);
  }
  engine.stop(out);
  TEST_ASSERT_TRUE(out.saw(EvtKind::Saved));
  TEST_ASSERT_EQUAL_UINT32(44 + 40 * 4096, out.committed_size);
  const std::string wav = vfs(rec_wav_path(rectest::kRoot, out.committed));
  char meta[kMetaMax + 1] = {};
  FILE* f = std::fopen(vfs(rec_meta_path(rectest::kRoot, out.committed)).c_str(), "rb");
  TEST_ASSERT_NOT_NULL(f);
  const size_t n = std::fread(meta, 1, kMetaMax, f);
  std::fclose(f);
  const Opt<RecMeta> m = rec_meta_decode(meta, n, out.committed);
  TEST_ASSERT_TRUE(m.has && m.value.bytes == 40 * 4096 && m.value.sha256.has);
  TEST_ASSERT_EQUAL_STRING(rectest::sha256_of(wav, 44).c_str(), m.value.sha256.value.c_str());
  FILE* w = std::fopen(wav.c_str(), "rb");
  uint8_t head[44];
  TEST_ASSERT_EQUAL_UINT(44, std::fread(head, 1, 44, w));
  std::fclose(w);
  TEST_ASSERT_TRUE(wav_parse(head, 44).value.data_bytes == 40 * 4096);
}

static void test_cut_off_session_is_recovered_at_boot() {
  {
    RecEngine engine(deps());
    Collect out;
    StartSpec s;
    s.mode = RecMode::Meeting;
    s.consent = true;
    const uint32_t t0 = millis();
    engine.start(s, t0, t0, out);
    for (int i = 0; i < 24; ++i) {  // past one fsync
      fill(100 + i);
      engine.write(g_block, sizeof g_block, t0 + 128 * (i + 1), out);
    }
    g_fs.close();  // the power goes: the descriptor is gone, no header, no .M
  }
  RecEngine boot(deps());
  Collect out;
  // Only scan() fills the index, so it is still as a boot allocates it. Never reset it
  // with a RecIndex{} temporary: that is 4 KB on the 8 KB loop stack (docs/07 §6.7 #1).
  TEST_ASSERT_FALSE(g_index.ready());
  TEST_ASSERT_EQUAL_UINT16(0, g_index.count());
  boot.scan(g_scratch, sizeof g_scratch, out);
  TEST_ASSERT_TRUE(g_index.ready());
  TEST_ASSERT_TRUE(g_index.count() >= 2);
  TEST_ASSERT_EQUAL_UINT32(2, g_index.rev());  // one recovery
  bool found = false;
  for (uint16_t i = 0; i < g_index.count(); ++i) {
    char meta[kMetaMax + 1] = {};
    FILE* f = std::fopen(vfs(rec_meta_path(rectest::kRoot, g_index.at(i).id)).c_str(), "rb");
    const size_t n = f ? std::fread(meta, 1, kMetaMax, f) : 0;
    if (f) std::fclose(f);
    const Opt<RecMeta> m = rec_meta_decode(meta, n, g_index.at(i).id);
    if (!m.has || !m.value.rcv) continue;
    found = true;
    TEST_ASSERT_TRUE(m.value.mode == RecMode::Meeting && m.value.consent && m.value.bytes >= 8 * 4096);
  }
  TEST_ASSERT_TRUE(found);
}

static void test_get_and_del() {
  RecEngine engine(deps());
  Collect out;
  const RecIndex::Entry e = g_index.at(0);
  RecCmd get;
  get.kind = CmdKind::Get;
  get.fid = e.id;
  get.size = e.size;
  get.off = 0;
  get.len = 1380;
  engine.get(get, out);
  TEST_ASSERT_TRUE(out.last[0].kind == EvtKind::Data && out.last[0].n == 1380);
  TEST_ASSERT_EQUAL_UINT32(crc32_ieee(g_xfer.data, 1380), out.last[0].crc);  // ROM CRC == zlib
  engine.close_transfer();
  const std::string wav = vfs(rec_wav_path(rectest::kRoot, e.id));
  RecCmd del;
  del.kind = CmdKind::Del;
  del.fid = e.id;
  del.size = e.size;
  del.sha256 = some(Sha256Hex::from(rectest::sha256_of(wav, 44).c_str()));
  engine.del(del, out);
  TEST_ASSERT_TRUE(out.last[1].kind == EvtKind::DelDone && out.last[1].code == ErrCode::None);
  TEST_ASSERT_TRUE(std::fopen(wav.c_str(), "rb") == nullptr);
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
  RUN_TEST(test_memo_commits_with_the_hardware_sha);
  RUN_TEST(test_cut_off_session_is_recovered_at_boot);
  RUN_TEST(test_get_and_del);
  RUN_TEST(test_clean_up);
  UNITY_END();
}

void loop() { devtest::idle(); }
