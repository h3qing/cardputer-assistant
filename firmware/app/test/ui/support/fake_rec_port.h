// FakeRecPort: the audio task on the host. Commands run synchronously on a real RecEngine
// over FakeRecFilePort; events wait in a queue for the runtime's poll(), which records
// each one it hands over (heard). A Start can be held (hold_start) to play "G0 released
// before the microphone came up", and file requests can be held (hold_files) to fill the
// in-flight window; the queue stays FIFO, so whatever is posted after a held request
// waits behind it. Like the real task it makes no sound.
// A Start starts the engine at once (a warm codec whose first block is live) unless gated:
// then it arms the engine as the real task does (issue #31), and Started waits for the
// first live block (feed) after the muted ones (feed_muted).
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "rec_engine.h"
#include "rec_port.h"
#include "core/support/fake_rec_file_port.h"

namespace uitest {

class FakeRecPort : public cpa::RecPort, private cpa::rec::EvtSink {
 public:
  testsupport::FakeRecFilePort fs;
  testsupport::FakeSha256 sha;
  cpa::rec::RecIndex idx;
  cpa::rec::TransferBuf buf;
  std::unique_ptr<cpa::rec::RecEngine> engine;
  std::vector<cpa::rec::RecCmd> cmds;
  std::vector<cpa::rec::EvtKind> heard;  // every event the runtime polled, in order
  bool hold_start = false;
  bool hold_files = false;
  bool gated = false;
  bool accept = true;
  uint8_t room_left = 8;  // free slots in the command queue (the task drains it at once)
  int drop_ends = 0;      // docs/20 S1: lose this many end events (Saved / Discarded / Failed)
  int drop_ledger_done = 0;  // docs/28 review 7: lose this many LedgerDone events
  uint32_t now = 0;
  cpa::rec::RecHealth health_now;  // what the task measured (debug.state.rec, gh #21)
  uint32_t stack = 0;              // the task's stack never used (debug.state.diag)

  // The audio task starts after Runtime::begin() with the runtime's id counter, and
  // scans the card first (scan = false: the scan has not finished yet).
  void boot(cpa::MonoCounter& ids, bool scan = true) {
    cpa::rec::EngineDeps d;
    d.fs = &fs;
    d.sha = &sha;
    d.ids = &ids;
    d.index = &idx;
    d.xfer = &buf;
    d.fw = cpa::FixedStr<31>::from("cpa-fw/0.3.0");
    engine.reset(new cpa::rec::RecEngine(d));
    if (scan) finish_scan();
  }
  // The default build's scan table: the idle 24 KB ring (1536 names, 1264 kept copies).
  void finish_scan() { engine->scan(scratch(), kScratch, *this); }
  static constexpr size_t kScratch = 24576;
  static uint8_t* scratch() {
    alignas(8) static uint8_t mem[kScratch];
    return mem;
  }

  bool post(const cpa::rec::RecCmd& c) override {
    if (!accept) return false;
    cmds.push_back(c);
    if (hold_files && (is_file(c) || !held_.empty())) {
      held_.push_back(c);
      return true;
    }
    run(c);
    return true;
  }

  uint8_t room() const override { return accept ? room_left : 0; }

  bool poll(cpa::rec::RecEvt& e) override {
    if (events_.empty()) return false;
    e = events_.front();
    events_.pop_front();
    heard.push_back(e.kind);
    return true;
  }

  const cpa::rec::TransferBuf& xfer() const override { return buf; }
  cpa::rec::RecIndex& index() override { return idx; }
  cpa::rec::RecHealth health() const override { return health_now; }
  uint32_t stack_left() const override { return stack; }

  void finish_start() {
    if (!start_.has) return;
    start_.has = false;
    if (gated) return engine->arm(start_.value.start, now, now);
    engine->start(start_.value.start, now, now, *this);
  }
  void release_files() {
    for (const auto& c : held_) run(c);
    held_.clear();
  }
  // ms of audio while recording, in 4 KB blocks (128 ms each)
  void feed(uint32_t ms) {
    for (uint32_t t = 0; t < ms && engine->recording(); t += 128) {
      std::vector<uint8_t> block(4096);
      for (size_t i = 0; i < block.size(); ++i) block[i] = static_cast<uint8_t>(i * 7 + t);
      now += 128;
      engine->write(block.data(), block.size(), now, *this);
    }
  }
  // ms of the codec's muted output (exact zeros) while armed, in 4 KB blocks.
  void feed_muted(uint32_t ms) {
    const std::vector<uint8_t> block(4096, 0);
    for (uint32_t t = 0; t < ms && engine->recording(); t += 128) {
      now += 128;
      engine->write(block.data(), block.size(), now, *this);
    }
  }
  // These PCM bytes while recording, in 4 KB blocks (128 ms each).
  void feed_pcm(const std::string& pcm) {
    for (size_t at = 0; at < pcm.size() && engine->recording(); at += 4096) {
      const size_t n = pcm.size() - at < 4096 ? pcm.size() - at : 4096;
      now += 128;
      engine->write(reinterpret_cast<const uint8_t*>(pcm.data() + at), n, now, *this);
    }
  }
  size_t count(cpa::rec::CmdKind k) const {
    size_t n = 0;
    for (const auto& c : cmds) n += c.kind == k ? 1 : 0;
    return n;
  }

 private:
  static bool is_file(const cpa::rec::RecCmd& c) {
    return c.kind == cpa::rec::CmdKind::List || c.kind == cpa::rec::CmdKind::Get || c.kind == cpa::rec::CmdKind::Del ||
           c.kind == cpa::rec::CmdKind::Ledger;
  }

  // As the audio task does (docs/20 S1): a recording's events carry its Start's gen.
  void emit(const cpa::rec::RecEvt& e) override {
    using cpa::rec::EvtKind;
    const bool end = e.kind == EvtKind::Saved || e.kind == EvtKind::Discarded || e.kind == EvtKind::Failed;
    if (end && drop_ends > 0) {
      --drop_ends;
      return;
    }
    if (e.kind == EvtKind::LedgerDone && drop_ledger_done > 0) {
      --drop_ledger_done;
      return;
    }
    cpa::rec::RecEvt out = e;
    if (cpa::rec::carries_gen(e.kind)) out.gen = gen_;
    events_.push_back(out);
  }

  void run(const cpa::rec::RecCmd& c) {
    using cpa::rec::CmdKind;
    switch (c.kind) {
      case CmdKind::Start:
        gen_ = c.start.gen;
        start_ = cpa::some(c);
        if (!hold_start) finish_start();
        return;
      case CmdKind::Stop: return stop();
      case CmdKind::Mark: engine->mark(); return;
      case CmdKind::Tag: engine->tag(c.start.project, c.start.slot); return;
      case CmdKind::List: return engine->list(c, *this);
      case CmdKind::Get: return engine->get(c, *this);
      case CmdKind::Del: return engine->del(c, *this);
      case CmdKind::SegMs: engine->set_seg_ms(c.seg_ms); return;
      case CmdKind::Ledger: return engine->ledger(c, engine->recording() ? nullptr : scratch(), kScratch, *this);
      default: return;
    }
  }

  // A Stop behind a Start not yet taken: the microphone never starts, nothing is recorded.
  void stop() {
    if (!start_.has) return engine->stop(*this);
    start_.has = false;
    cpa::rec::RecEvt e;
    e.kind = cpa::rec::EvtKind::Discarded;
    emit(e);
  }

  std::deque<cpa::rec::RecEvt> events_;
  std::vector<cpa::rec::RecCmd> held_;
  cpa::Opt<cpa::rec::RecCmd> start_;
  uint8_t gen_ = 0;  // the last Start's
};

}  // namespace uitest
