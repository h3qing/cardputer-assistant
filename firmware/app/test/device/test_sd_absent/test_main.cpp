// No card in the slot (run by hand, card pulled; docs/09-m2-design.md §3.9, §9.2, R18):
// hal_sd_mount reports Absent within 500 ms and the store stays unused. Without a card
// the driver retries GO_IDLE_STATE 3 x 100 ms; when MISO idles low, sdWait and
// sdSelectCard may add 500 ms each. The MISO (GPIO39) level after the attempt is printed:
// over budget, it is the first thing to record before trying the internal pull-up.
// Without the card the audio task is never started (docs/11 §4.7: its stack only with the
// card primary); its queues stay, so the loop's post and poll stay harmless.
#include <Arduino.h>
#include <unity.h>

#include "usb_transport.h"  // listed here so the dependency finder sees it
#include "audio_task.h"
#include "device_harness.h"
#include "sd_card.h"
#include "sd_store.h"

namespace {

constexpr int kMiso = 39;

cpa::UsbTransport g_usb;
cpa::VfsFatPort g_port;
cpa::SdStore g_sd(g_port, "/cpatest");
cpa::MonoCounter g_ids;
cpa::AudioTask g_audio;

}  // namespace

void setUp() {}
void tearDown() {}

static void test_mount_without_a_card_is_absent_within_500ms() {
  const uint32_t t0 = micros();
  const cpa::SdFacts f = cpa::hal_sd_mount(g_sd);
  const uint32_t us = micros() - t0;
  Serial.printf("CPA_SD_ABSENT state=%d us=%lu miso_idle=%d\n", static_cast<int>(f.state), static_cast<unsigned long>(us),
                digitalRead(kMiso));
  TEST_ASSERT_TRUE(f.state == cpa::SdState::Absent);
  TEST_ASSERT_EQUAL_UINT32(0, f.size_mib);
  TEST_ASSERT_TRUE(us <= 500000u);
  TEST_ASSERT_FALSE(g_sd.failed());  // never prepared: the store was not touched
}

static void test_no_card_starts_no_audio_task() {
  TEST_ASSERT_FALSE(g_audio.prepare(false).buffers);
  g_audio.start(g_ids, cpa::FixedStr<31>::from("cpa-fw/test"));
  TEST_ASSERT_EQUAL_UINT32(0, g_audio.stack_left());  // 0 = no task
  cpa::rec::RecCmd seg;
  seg.kind = cpa::rec::CmdKind::SegMs;  // debug.rec: the one command the loop still posts
  TEST_ASSERT_TRUE(g_audio.post(seg));
  cpa::rec::RecEvt e;
  TEST_ASSERT_FALSE(g_audio.poll(e));
}

void setup() {
  devtest::begin(g_usb);
  UNITY_BEGIN();
  RUN_TEST(test_mount_without_a_card_is_absent_within_500ms);
  RUN_TEST(test_no_card_starts_no_audio_task);
  UNITY_END();
}

void loop() { devtest::idle(); }
