// Seamless segments with the real microphone (docs/11 §4.4-§4.5, G-A4 in short): the
// audio task records a 95 s meeting with 30 s segments (debug.rec's SegMs) below
// /cpatest. Pass: segments 0..2 hold exactly 30 s of samples each (960,000 B), the
// segments are contiguous (next links, seg 0..3), and all samples together equal the
// recording's wall time x 16,000 within two blocks (the first block's latency included).
// Prints CPA_ROT with the numbers and the ring's high-water mark. Needs the card.
#include <Arduino.h>
#include <M5Cardputer.h>
#include <unity.h>

#include "audio_task.h"
#include "device_harness.h"
#include "i2c_lock.h"
#include "../test_rec_budget/rec_harness.h"
#include "sd_card.h"
#include "sd_store.h"
#include "usb_transport.h"

using namespace cpa;
using namespace cpa::rec;

namespace {

constexpr uint32_t kSegMs = 30000;
constexpr uint32_t kRecordMs = 95000;

cpa::UsbTransport g_usb;
cpa::VfsFatPort g_fat;
cpa::SdStore g_store(g_fat, rectest::kRoot);
cpa::MonoCounter g_ids;
cpa::AudioTask g_audio;

}  // namespace

void setUp() {}
void tearDown() {}

static void test_three_30s_segments_are_sample_exact() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;
  M5Cardputer.begin(cfg, true);
  hal_i2c_lock_begin();
  TEST_ASSERT_TRUE_MESSAGE(hal_sd_mount(g_store).state == SdState::Ok, "insert the FAT32 card");
  TEST_ASSERT_EQUAL_INT(0, rectest::remove_tree(rectest::kVfsRoot));
  const BootRecord rec{rectest::kTestEpoch, 2};
  g_ids.start(boot_ticket(rec, rec).value);
  TEST_ASSERT_TRUE(g_audio.prepare(true).buffers);
  g_audio.start(g_ids, FixedStr<31>::from("cpa-fw/test"), rectest::kRoot);
  RecEvt e;
  auto none = [](const RecEvt&) {};
  TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::IndexReady, 10000, e, none));
  RecCmd seg;
  seg.kind = CmdKind::SegMs;
  seg.seg_ms = kSegMs;
  TEST_ASSERT_TRUE(g_audio.post(seg));
  RecCmd start;
  start.kind = CmdKind::Start;
  start.start.mode = RecMode::Meeting;
  start.start.consent = true;
  TEST_ASSERT_TRUE(g_audio.post(start));
  TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::Started, 5000, e, none));
  const uint32_t t0 = e.up_ms;  // the first written sample: Started comes with its block (issue #31)
  uint32_t sizes[8] = {};
  uint8_t n = 0;
  auto count = [&](const RecEvt& ev) {
    if (ev.kind == EvtKind::Committed && n < 8) sizes[n++] = ev.size;
  };
  rectest::wait_for(g_audio, EvtKind::Saved, kRecordMs, e, count);  // runs the whole time
  RecCmd stop;
  stop.kind = CmdKind::Stop;
  TEST_ASSERT_TRUE(g_audio.post(stop));
  const uint32_t wall = millis() - t0;
  TEST_ASSERT_TRUE(rectest::wait_for(g_audio, EvtKind::Saved, 10000, e, count));
  uint64_t samples = 0;
  for (uint8_t i = 0; i < n; ++i) samples += (sizes[i] - 44) / 2;
  Serial.printf("CPA_ROT segments=%u samples=%llu wall_ms=%lu expect=%llu ring_high=%u stack_left=%lu\n", n,
                static_cast<unsigned long long>(samples), static_cast<unsigned long>(wall),
                static_cast<unsigned long long>(static_cast<uint64_t>(wall) * 16), g_audio.ring_high_water(),
                static_cast<unsigned long>(g_audio.stack_left()));
  TEST_ASSERT_EQUAL_UINT8(4, n);
  for (uint8_t i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_UINT32(44 + kSegMs * 32, sizes[i]);
  const int64_t diff = static_cast<int64_t>(samples) - static_cast<int64_t>(wall) * 16;
  TEST_ASSERT_TRUE(diff <= 2 * 2048 && diff >= -2 * 2048 - 16 * 200);  // + the first block's latency
  TEST_ASSERT_TRUE(g_audio.ring_high_water() < 6);  // the ring never ran full
  TEST_ASSERT_EQUAL_INT(0, rectest::remove_tree(rectest::kVfsRoot));
  ::rmdir(rectest::kVfsRoot);
}

void setup() {
  devtest::begin(g_usb);
  UNITY_BEGIN();
  RUN_TEST(test_three_30s_segments_are_sample_exact);
  UNITY_END();
}

void loop() { devtest::idle(); }
