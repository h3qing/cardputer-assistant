#include "audio_task.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <new>

namespace cpa {
namespace {

constexpr UBaseType_t kCmdDepth = 8;
constexpr UBaseType_t kEvtDepth = 16;
constexpr TickType_t kBlockWait = pdMS_TO_TICKS(20);
constexpr TickType_t kEvtWait = pdMS_TO_TICKS(500);
// The record-at-start mode's boot scan and ledger work (docs/28 §3.3): 1024 names, 752 kept
// copies at most. The default build uses the idle 24 KB ring (1536 names, 1264 kept).
constexpr size_t kScanScratch = 16384;

uint8_t* alloc_internal(size_t n) {
  return static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

rec::RecEvt event(rec::EvtKind kind) {
  rec::RecEvt e;
  e.kind = kind;
  return e;
}

}  // namespace

RecBoot AudioTask::prepare(bool sd) {
  RecBoot rb;
  cmds_ = xQueueCreate(kCmdDepth, sizeof(rec::RecCmd));
  evts_ = xQueueCreate(kEvtDepth, sizeof(rec::RecEvt));
#if CPA_TEST_HOOKS
  health_ = xQueueCreate(1, sizeof(rec::RecHealth));
#endif
  if (!cmds_ || !evts_ || !sd) return rb;
  xfer_ = new (std::nothrow) rec::TransferBuf();
  index_ = new (std::nothrow) rec::RecIndex();
  sd_ = xfer_ && index_ && (CPA_REC_ALLOC_AT_START || acquire_ring());
  rb.buffers = sd_;
  return rb;
}

void AudioTask::start(MonoCounter& ids, const FixedStr<31>& fw, const char* root) {
  if (!cmds_ || !evts_) return;
  if (sd_) {
    rec::EngineDeps d;
    d.fs = &fs_;
    d.sha = &sha_;
    d.ids = &ids;
    d.index = index_;
    d.xfer = xfer_;
    d.crc = hal_crc32;
    d.root = root;
    d.fw = fw;
    engine_ = new (std::nothrow) rec::RecEngine(d);
    sd_ = engine_ != nullptr;
  }
  if (!sd_) return;  // no card (or no engine): no task; the loop refuses recording itself
  xTaskCreatePinnedToCore(entry, "rec_task", kAudioStackBytes, this, kAudioPriority, &task_, kAudioCore);
}

bool AudioTask::acquire_ring() {
  if (ring_mem_) return true;
  ring_mem_ = alloc_internal(kRingBytes);
  return ring_mem_ && ring_.attach(ring_mem_);
}

void AudioTask::release_ring() {
  if (!CPA_REC_ALLOC_AT_START || !ring_mem_) return;
  heap_caps_free(ring_mem_);
  ring_mem_ = nullptr;
}

bool AudioTask::post(const rec::RecCmd& c) { return cmds_ && xQueueSend(cmds_, &c, 0) == pdTRUE; }

uint8_t AudioTask::room() const { return cmds_ ? static_cast<uint8_t>(uxQueueSpacesAvailable(cmds_)) : 0; }

bool AudioTask::poll(rec::RecEvt& e) { return evts_ && xQueueReceive(evts_, &e, 0) == pdTRUE; }

uint32_t AudioTask::stack_left() const { return task_ ? uxTaskGetStackHighWaterMark(task_) : 0; }

void AudioTask::entry(void* self) { static_cast<AudioTask*>(self)->run(); }

void AudioTask::run() {
  uint8_t* scratch = ring_mem_ ? ring_mem_ : alloc_internal(kScanScratch);
  if (scratch) engine_->scan(scratch, ring_mem_ ? kRingBytes : kScanScratch, *this);
  if (scratch != ring_mem_) heap_caps_free(scratch);
  warm_codec();
  for (;;) {
    if (engine_->recording()) {
      record_step();
      continue;
    }
    rec::RecCmd c;
    if (xQueueReceive(cmds_, &c, portMAX_DELAY) == pdTRUE) handle(c);
  }
}

void AudioTask::record_step() {
  const int idx = ring_.next_full(kBlockWait);
  if (idx >= 0) {
    const uint32_t t0 = millis();
    engine_->write(ring_.block(idx), kBlockBytes, t0, *this);
    ring_.release(idx);
#if CPA_TEST_HOOKS
    track_block(t0);
#endif
  } else {
    engine_->tick(millis(), *this);  // armed and no block at all: the wait still ends
  }
  rec::RecCmd c;
  while (engine_->recording() && xQueueReceive(cmds_, &c, 0) == pdTRUE) handle(c);
}

void AudioTask::handle(const rec::RecCmd& c) {
  switch (c.kind) {
    case rec::CmdKind::Start: return begin_session(c);
    case rec::CmdKind::Stop: return end_session();
    case rec::CmdKind::Mark: engine_->mark(); return;
    case rec::CmdKind::Tag: engine_->tag(c.start.project, c.start.slot); return;
    case rec::CmdKind::List: return engine_->list(c, *this);
    case rec::CmdKind::Get: return engine_->get(c, *this);
    case rec::CmdKind::Del: return engine_->del(c, *this);
    case rec::CmdKind::SegMs: engine_->set_seg_ms(c.seg_ms); return;
    case rec::CmdKind::Ledger: return ledger(c);
    case rec::CmdKind::MicGain:
      if (mic_gain_valid(c.gain)) gain_ = c.gain;
      return;
#if CPA_TEST_HOOKS
    case rec::CmdKind::Codec: return codec_hook(c);
#endif
    default: return;
  }
}

// A Stop already waiting at the front of the queue behind the Start: the memo was let go
// before the microphone came up (docs/11 §4.1, B5). Taken from the queue.
bool AudioTask::take_waiting_stop() {
  rec::RecCmd c;
  if (xQueuePeek(cmds_, &c, 0) != pdTRUE || c.kind != rec::CmdKind::Stop) return false;
  xQueueReceive(cmds_, &c, 0);
  return true;
}

// docs/11 §4.2: Mic.begin -> first block, no sound. A Stop already waiting discards the
// session without starting the microphone. The engine is armed: Started comes with the
// first block the codec does not hold muted (issue #31).
void AudioTask::begin_session(const rec::RecCmd& c) {
  gen_ = c.start.gen;
  engine_->close_transfer();
  if (take_waiting_stop()) return emit(event(rec::EvtKind::Discarded));
  rec::RecEvt fail = event(rec::EvtKind::Failed);
  if (!acquire_ring()) {
    fail.fail = rec::RecFail::NoMemory;
    return emit(fail);
  }
  if (!ring_.start(gain_)) {
    fail.fail = rec::RecFail::Internal;
    return emit(fail);
  }
  const uint32_t up = millis();
  engine_->arm(c.start, up, up);
#if CPA_TEST_HOOKS
  track_begin();
#endif
}

void AudioTask::end_session() {
  if (!engine_->recording()) return;
  ring_.stop();
  if (!engine_->waiting()) {  // still waiting for the codec: nothing is written, the stop discards
    for (int idx = ring_.next_full(0); idx >= 0 && engine_->recording(); idx = ring_.next_full(0)) {
      const uint32_t t0 = millis();
      engine_->write(ring_.block(idx), kBlockBytes, t0, *this);  // what the ring still holds
#if CPA_TEST_HOOKS
      track_block(t0);
#endif
    }
  }
  engine_->stop(*this);
}

// docs/28 S1a: the ledger's replay needs room, only while idle: the ring (the default build),
// or a buffer for the while (record-at-start); while recording the engine answers Busy.
void AudioTask::ledger(const rec::RecCmd& c) {
  const bool idle = !engine_->recording();
  uint8_t* scratch = idle ? (ring_mem_ ? ring_mem_ : alloc_internal(kScanScratch)) : nullptr;
  engine_->ledger(c, scratch, ring_mem_ ? kRingBytes : kScanScratch, *this);
  if (scratch && scratch != ring_mem_) heap_caps_free(scratch);
}

void AudioTask::end_mic() {
  ring_.stop();
  release_ring();
}

// Issue #31: once per boot, after the scan (and again on debug.codec warm). Recording is
// still silent: only the microphone runs, nothing is written.
void AudioTask::warm_codec() {
  if (acquire_ring()) ring_.warm(gain_);
  release_ring();
}

#if CPA_TEST_HOOKS
// debug.codec: the codec as a power-on leaves it; never under a recording (the loop answers
// E_BUSY then, this is the second guard).
void AudioTask::codec_hook(const rec::RecCmd& c) {
  if (engine_->recording() || !ring_.cold()) return;
  if (c.warm) warm_codec();
}

// gh #21: a new recording's counters, from the heap it starts with.
void AudioTask::track_begin() {
  rotated_ = false;
  tracked_ = rec::health_begin(ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (health_) xQueueOverwrite(health_, &tracked_);
}

// After a block's write (started at t0): the ring's counts, the heap now, and the write's
// time when it changed segment.
void AudioTask::track_block(uint32_t t0) {
  rec::BlockHealth b;
  b.ring_high = ring_.high_water();
  b.mic_missing = ring_.missed();
  b.rotated = rotated_;
  b.write_ms = millis() - t0;
  b.heap_free = ESP.getFreeHeap();
  b.heap_block = ESP.getMaxAllocHeap();
  rotated_ = false;
  tracked_ = rec::health_block(tracked_, b);
  if (health_) xQueueOverwrite(health_, &tracked_);
}

rec::RecHealth AudioTask::health() const {
  rec::RecHealth h;
  return health_ && xQueuePeek(health_, &h, 0) == pdTRUE ? h : rec::RecHealth{};
}
#endif

// Events from the engine (and begin_session): the end of a session stops the microphone
// before the loop hears of it, so idle always means the microphone (and the speaker) off.
void AudioTask::emit(const rec::RecEvt& e) {
#if CPA_TEST_HOOKS
  if (e.kind == rec::EvtKind::Segment) rotated_ = true;
#endif
  switch (e.kind) {
    case rec::EvtKind::Saved:
    case rec::EvtKind::Discarded:
    case rec::EvtKind::Failed: end_mic(); break;
    default: break;
  }
  rec::RecEvt out = e;
  if (rec::carries_gen(e.kind)) out.gen = gen_;
  // a level may be dropped; docs/28 review 7: a ledger command's answers never are (the
  // loop always drains, and no recording runs then): a lost Requeued or LedgerDone would
  // leave a segment out of the index or the screen waiting
  const bool ledger = e.kind == rec::EvtKind::Requeued || e.kind == rec::EvtKind::LedgerDone;
  xQueueSend(evts_, &out, e.kind == rec::EvtKind::Level ? 0 : (ledger ? portMAX_DELAY : kEvtWait));
}

}  // namespace cpa
