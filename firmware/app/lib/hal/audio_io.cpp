#include "audio_io.h"

#include <M5Cardputer.h>

#include "i2c_lock.h"

namespace cpa {
namespace {

constexpr uint8_t kEs8311 = 0x18;
constexpr uint8_t kReg00Reset = 0x00;
constexpr uint8_t kSoftReset = 0x1F;  // every block of the codec in reset: its power-on state
constexpr uint8_t kReg14Pga = 0x14;
constexpr uint8_t kReg17Vol = 0x17;
constexpr uint32_t kCodecHz = 100000;
constexpr int kGainTries = 3;

// One codec register, written and read back; the caller holds the I2C lock.
bool write_checked(uint8_t reg, uint8_t value) {
  for (int i = 0; i < kGainTries; ++i) {
    uint8_t back = 0;
    if (M5.In_I2C.writeRegister8(kEs8311, reg, value, kCodecHz) &&
        M5.In_I2C.readRegister(kEs8311, reg, &back, 1, kCodecHz) && back == value) {
      return true;
    }
  }
  return false;
}

// After Mic.begin (which writes REG14 = 0x10, REG17 = 0xBF): the PGA first, then the volume.
bool write_gain(const MicGain& g) {
  const bool pga = write_checked(kReg14Pga, es8311_reg14(g.pga));
  const bool vol = write_checked(kReg17Vol, g.vol);
  return pga && vol;
}

}  // namespace

bool MicRing::attach(uint8_t* memory) {
  mem_ = memory;
  if (!full_) full_ = xQueueCreate(kRingBlocks, sizeof(int));
  if (!free_) free_ = xQueueCreate(kRingBlocks, sizeof(int));
  return mem_ && full_ && free_;
}

int MicRing::index_of(const void* p) const {
  for (size_t i = 0; i < kRingBlocks; ++i) {
    if (mem_ + i * kBlockBytes == p) return static_cast<int>(i);
  }
  return -1;
}

// On M5Unified's capture task: queue the full block, re-arm the next free one at once.
void MicRing::on_release(void* self, void* data, size_t) {
  MicRing& r = *static_cast<MicRing*>(self);
  const int idx = r.index_of(data);
  if (idx >= 0) xQueueSend(r.full_, &idx, 0);
  int next = -1;
  if (xQueueReceive(r.free_, &next, 0) != pdTRUE) return r.rearm_failed();
  if (!M5Cardputer.Mic.record(reinterpret_cast<int16_t*>(r.mem_ + next * kBlockBytes), kBlockSamples)) {
    xQueueSendToFront(r.free_, &next, 0);
    r.rearm_failed();
  }
}

// A miss while the capture runs, owed to refill(); nothing once stop() has begun.
void MicRing::rearm_failed() {
  if (!rec::rearm_missed(stopping_.load())) return;
  missing_.fetch_add(1);
  missed_.fetch_add(1);
}

bool MicRing::start(const MicGain& g, uint32_t rate) {
  xQueueReset(full_);
  xQueueReset(free_);
  missing_.store(0);
  missed_.store(0);
  stopping_.store(false);
  high_ = 0;
  for (int i = 2; i < static_cast<int>(kRingBlocks); ++i) xQueueSend(free_, &i, 0);
  auto cfg = M5Cardputer.Mic.config();
  cfg.sample_rate = rate;  // the port is built at Mic.begin from this: never a warm-up's 48 kHz
  cfg.over_sampling = 1;  // pinned (M5Unified's internal-mic value): the factor is mag / 2
  cfg.magnification = g.mag;
  M5Cardputer.Mic.config(cfg);
  M5Cardputer.Mic.setBufferReleaseCallback(this, on_release);  // before the first record()
  bool ok = false;
  {
    I2cAudioHold hold;  // not recursive: never stop() in here
    ok = M5Cardputer.Mic.begin();
    if (ok && !write_gain(g)) ++gain_fail_;  // record anyway: losing the memo is worse
  }
  on_ = ok;
  if (!ok) return false;
  ok = M5Cardputer.Mic.record(reinterpret_cast<int16_t*>(mem_), kBlockSamples, rate) &&
       M5Cardputer.Mic.record(reinterpret_cast<int16_t*>(mem_ + kBlockBytes), kBlockSamples, rate);
  if (!ok) stop();
  return ok;
}

void MicRing::stop() {
  if (!on_) return;
  stopping_.store(true);  // before Mic.end: its refused re-arm is no miss
  {
    I2cAudioHold hold;
    M5Cardputer.Mic.end();
  }
  M5Cardputer.Mic.setBufferReleaseCallback(nullptr, nullptr);  // only after end() returned
  on_ = false;
}

// The blocks are only counted: the codec's clock frames are what ends its muted start.
bool MicRing::warm(const MicGain& g) {
  if (!start(g, kWarmRate)) return false;
  const TickType_t t0 = xTaskGetTickCount();
  uint32_t got = 0;
  while (got < kWarmSamples && xTaskGetTickCount() - t0 < pdMS_TO_TICKS(kWarmMaxMs)) {
    const int idx = next_full(pdMS_TO_TICKS(20));
    if (idx < 0) continue;
    got += kBlockSamples;
    release(idx);
  }
  stop();
  return got >= kWarmSamples;
}

#if CPA_TEST_HOOKS
bool MicRing::cold() {
  if (on_) return false;
  I2cAudioHold hold;
  return M5.In_I2C.writeRegister8(kEs8311, kReg00Reset, kSoftReset, kCodecHz);
}
#endif

int MicRing::next_full(TickType_t wait) {
  int idx = -1;
  if (xQueueReceive(full_, &idx, wait) != pdTRUE) return -1;
  const UBaseType_t waiting = uxQueueMessagesWaiting(full_) + 1;
  high_ = waiting > high_ ? static_cast<uint8_t>(waiting) : high_;
  return idx;
}

void MicRing::release(int idx) {
  xQueueSend(free_, &idx, 0);
  refill();
}

// Requests the callback could not re-arm (both slots were taken): arm them from here.
void MicRing::refill() {
  int idx = -1;
  while (on_ && missing_.load() > 0 && xQueueReceive(free_, &idx, 0) == pdTRUE) {
    if (!M5Cardputer.Mic.record(reinterpret_cast<int16_t*>(mem_ + idx * kBlockBytes), kBlockSamples)) {
      xQueueSendToFront(free_, &idx, 0);
      return;
    }
    missing_.fetch_sub(1);
  }
}

}  // namespace cpa
