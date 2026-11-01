// The microphone ring (docs/11 §4.2, §4.5), for the audio task. Device only.
//   MicRing: 6 blocks of 2048 samples (4 KB, 128 ms each; ~768 ms of slack for a 290 ms
//   rotation). M5Unified's capture task (priority 2) hands a full block back through the
//   release callback, which queues it as full and re-arms the next free block at once,
//   the way firmware/hwtest's !sdrec does (0 dropped frames, sd-card.md §7). A re-arm that
//   fails once stop() has begun is no miss (rec::rearm_missed): Mic.end refuses it.
// Recording is silent (v0.3.1): nothing here, or anywhere in the firmware, begins the
// speaker. The gain (mic_gain.h) is set on every start: over_sampling pinned to 1 and the
// magnification here, the ES8311 PGA (REG14) and ADC volume (REG17) written and read back
// right after Mic.begin, which rewrites both with M5Unified's own values each time. A write
// that still fails after 3 tries is counted (gain_failures()) and the recording goes on with
// that register at M5's value: a quieter memo is better than a lost one.
// warm() (issue #31, docs/11 §4.2): after a codec power-on the ES8311 holds its ADC output
// at zero for 2^14 clock frames from its first clocked start (about 1 s at 16 kHz), and an
// idle Mic.end does not bring that back. Once per boot the audio task runs the microphone
// for kWarmSamples at 48 kHz (about 0.47 s) and stops it; the next start is clocked at
// kMicRate again (start sets the port's rate every time). Measured on the device: no muted
// start after it (issue #31's report). cold() is the test firmware's way back to the
// power-on state (debug.codec): the soft reset REG00 = 0x1F with the microphone off.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "mic_gain.h"
#include "rec_health.h"

namespace cpa {

constexpr size_t kRingBlocks = 6;
constexpr size_t kBlockSamples = 2048;
constexpr size_t kBlockBytes = kBlockSamples * 2;
constexpr size_t kRingBytes = kRingBlocks * kBlockBytes;
constexpr uint32_t kMicRate = 16000;
constexpr uint32_t kWarmRate = 48000;
constexpr uint32_t kWarmSamples = 21600;  // 450 ms at 48 kHz: past 2^14 frames with margin
constexpr uint32_t kWarmMaxMs = 1000;     // a capture that delivers nothing ends the warm-up

class MicRing {
 public:
  bool attach(uint8_t* memory);  // kRingBytes; creates the queues once
  // Mic.begin at rate and the gain (I2C lock), the first two requests
  bool start(const MicGain& g, uint32_t rate = kMicRate);
  void stop();                   // Mic.end (I2C lock); full blocks not yet written are dropped
  bool warm(const MicGain& g);   // kWarmSamples at kWarmRate, then stop (blocks: the audio task)
#if CPA_TEST_HOOKS
  bool cold();                   // REG00 = 0x1F while off: the next start is muted (debug.codec)
#endif
  bool on() const { return on_; }
  int next_full(TickType_t wait);  // a full block, or -1
  const uint8_t* block(int idx) const { return mem_ + static_cast<size_t>(idx) * kBlockBytes; }
  void release(int idx);         // written: back to the free blocks
  uint8_t high_water() const { return high_; }  // most full blocks waiting at once
  uint32_t missed() const { return missed_.load(); }  // re-arms that found no free block since start()
  uint32_t gain_failures() const { return gain_fail_; }  // starts whose gain did not read back

 private:
  static void on_release(void* self, void* data, size_t len);
  int index_of(const void* p) const;
  void refill();
  void rearm_failed();  // on the capture task (rec::rearm_missed)

  uint8_t* mem_ = nullptr;
  QueueHandle_t full_ = nullptr;
  QueueHandle_t free_ = nullptr;
  std::atomic<int> missing_{0};      // re-arms still owed (refill() pays them back)
  std::atomic<uint32_t> missed_{0};  // every re-arm that failed (debug.state.rec.mic_missing)
  std::atomic<bool> stopping_{false};  // stop() has begun: a failed re-arm is no miss
  bool on_ = false;
  uint8_t high_ = 0;
  uint32_t gain_fail_ = 0;
};

}  // namespace cpa
