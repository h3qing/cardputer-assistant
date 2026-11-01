// Recording health counters (gh #21, docs/07 §6.7 #6): debug.state.rec's ring_high,
// mic_missing, rot_ms_max and heap minima. The test firmware's audio task keeps one
// RecHealth: health_begin() at each Start, health_block() after every written block; the
// loop reads the latest copy (RecPort::health()). They cover the current recording, or the
// last one while idle; nothing is measured before the first recording of the boot. Pure.
#pragma once

#include <cstdint>

namespace cpa {
namespace rec {

struct RecHealth {
  bool measured = false;     // a recording has started since boot
  uint16_t ring_high = 0;    // most full ring blocks waiting at once
  uint32_t mic_missing = 0;  // capture re-arms that found no free block
  uint32_t rot_ms_max = 0;   // slowest write that changed segment
  uint32_t heap_free = 0;    // lowest free heap of the recording
  uint32_t heap_block = 0;   // lowest largest free block of the recording
};

// One written block, as the audio task saw it right after the write.
struct BlockHealth {
  uint8_t ring_high = 0;     // the ring's high-water mark since its start
  uint32_t mic_missing = 0;  // the ring's count since its start
  bool rotated = false;      // the write changed segment (EvtKind::Segment)
  uint32_t write_ms = 0;     // how long the write took
  uint32_t heap_free = 0;
  uint32_t heap_block = 0;
};

RecHealth health_begin(uint32_t heap_free, uint32_t heap_block);
RecHealth health_block(const RecHealth& h, const BlockHealth& b);

// MicRing's release callback (on M5Unified's capture task) could not re-arm a block: no free
// block, or Mic.record refused it. While the capture runs that is a miss (mic_missing) and a
// block refill() arms later. Once the stop has begun (MicRing::stop sets it before Mic.end)
// it is neither: Mic.end refuses the callback's record() while it winds the capture down, and
// nothing more is captured. v0.4.1 known issue: memos held exactly 3,000 ms ended on a block's
// edge and counted one such re-arm (17 of 18 on the device), though nothing was lost.
// True: a miss (mic_missing + 1) that refill() pays back.
bool rearm_missed(bool stopping);

}  // namespace rec
}  // namespace cpa
