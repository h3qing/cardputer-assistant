// AudioTask (docs/11 §4.7): rec_task on core 0, priority 1 (below M5Unified's capture
// task at 2: an fsync can take 78 ms, the I2S DMA only bridges ~32 ms), 6 KB stack. It owns
// the microphone, the ring, the RecEngine (recording, the boot recovery scan, file.* reads)
// and at most one open file. It never begins the speaker: recording is silent (v0.3.1).
// The microphone gain is kMicGain; debug.mic (test firmware) replaces it from the next
// Start on, a recording already running keeps its own.
// Issue #31 (docs/11 §4.2): after the boot scan the task warms the codec once (MicRing::warm,
// about 0.5 s; a Start posted meanwhile waits in the queue), and every Start arms the
// engine: Started (● REC) and the first written sample wait for the first block the codec
// does not hold muted, 2 s at most. debug.codec (test firmware) puts the codec back into
// its power-on state, and with warm also runs the boot's warm-up again.
// The loop talks to it only through the two queues (RecPort). Device only. A recording's
// Started and end events carry the gen of its Start (docs/20 S1): an end that comes late
// (a slow card) is told apart from the next recording's.
// Test firmware (gh #21): after every written block the task replaces its RecHealth
// (rec_health.h) and puts a copy in a one-slot mailbox (xQueueOverwrite) that health()
// peeks from the loop: debug.state.rec's ring_high, mic_missing, rot_ms_max and heap minima.
//
// Memory (docs/11 §4.7, G-A0): with the SD card primary the ring (24 KB), the transfer
// buffer, the index and the engine are allocated once at boot; G-A0 measured 234,836 B free
// and a 208,884 B largest block before them. CPA_REC_ALLOC_AT_START=1 allocates the ring
// at each Start instead and frees it after the session; a failed allocation ends the start
// with Failed{NoMemory} ("Out of memory"). Without the card only the queues exist: the task is
// never started (the loop refuses recording itself), so post() only fills the queue.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "audio_io.h"
#include "hal_sha256.h"
#include "rec_engine.h"
#include "rec_port.h"
#include "vfs_rec_file_port.h"

#ifndef CPA_REC_ALLOC_AT_START
#define CPA_REC_ALLOC_AT_START 0
#endif

namespace cpa {

constexpr uint32_t kAudioStackBytes = 6 * 1024;
constexpr UBaseType_t kAudioPriority = 1;
constexpr BaseType_t kAudioCore = 0;

class AudioTask : public RecPort, private rec::EvtSink {
 public:
  // setup(), before Runtime::begin(): the queues and, with sd, the buffers (G-A0: at boot).
  RecBoot prepare(bool sd);
  // After Runtime::begin() (the id counter is started): with sd, the engine and the task,
  // which scans the card first. fw goes into every .M. xfer() and index() exist only with
  // sd (the runtime asks only then).
  void start(MonoCounter& ids, const FixedStr<31>& fw, const char* root = "/cpa");
  bool post(const rec::RecCmd& c) override;
  uint8_t room() const override;
  bool poll(rec::RecEvt& e) override;
  const rec::TransferBuf& xfer() const override { return *xfer_; }
  rec::RecIndex& index() override { return *index_; }
  uint32_t stack_left() const override;  // bytes never used so far
#if CPA_TEST_HOOKS
  rec::RecHealth health() const override;
#endif
  uint8_t ring_high_water() const { return ring_.high_water(); }
  uint32_t mute_timeouts() const { return engine_ ? engine_->mute_timeouts() : 0; }

 private:
  static void entry(void* self);
  bool take_waiting_stop();
  void run();
  void record_step();
  void handle(const rec::RecCmd& c);
  void begin_session(const rec::RecCmd& c);
  void end_session();
  void end_mic();
  void warm_codec();
  void ledger(const rec::RecCmd& c);  // docs/28 S1a
#if CPA_TEST_HOOKS
  void codec_hook(const rec::RecCmd& c);
  void track_begin();
  void track_block(uint32_t t0);
#endif
  bool acquire_ring();
  void release_ring();
  void emit(const rec::RecEvt& e) override;

  QueueHandle_t cmds_ = nullptr;
  QueueHandle_t evts_ = nullptr;
  TaskHandle_t task_ = nullptr;
  MicRing ring_;
  uint8_t* ring_mem_ = nullptr;
  rec::TransferBuf* xfer_ = nullptr;
  rec::RecIndex* index_ = nullptr;
  rec::RecEngine* engine_ = nullptr;
  VfsRecFilePort fs_;
  HalSha256 sha_;
  MicGain gain_ = kMicGain;
  bool sd_ = false;
  uint8_t gen_ = 0;  // the last Start's gen, copied into its recording's events (docs/20 S1)
#if CPA_TEST_HOOKS
  QueueHandle_t health_ = nullptr;  // one RecHealth, overwritten by the task
  rec::RecHealth tracked_;          // the task's own copy
  bool rotated_ = false;            // the engine emitted Segment since the last block
#endif
};

}  // namespace cpa
