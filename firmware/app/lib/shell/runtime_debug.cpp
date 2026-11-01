// debug.state summary (test firmware only, protocol README "contract additions" 9).
// Compiled out of release builds: no debug.* name may reach firmware.bin.
#if CPA_TEST_HOOKS

#include "reducers.h"
#include "runtime.h"

namespace cpa {
namespace {

// gh #21: the audio task's counters, once it measures (from the first recording of the boot).
void put_health(proto::DebugRecState& rec, const rec::RecHealth& h) {
  if (!h.measured) return;
  rec.ring_high = some(h.ring_high);
  rec.mic_missing = some(h.mic_missing);
  rec.rot_ms_max = some(h.rot_ms_max);
  rec.heap_free = some(h.heap_free);
  rec.heap_block = some(h.heap_block);
}

// v0.3 debug.state.rec (docs/11 §5.3): mode and seg are null while idle.
void fill_debug_rec(proto::DebugState& s, const RecInfo& r, const rec::RecHealth& h) {
  if (!r.avail) return;
  proto::DebugRecState rec;
  rec.live = r.phase != RecPhase::Idle;
  if (rec.live) rec.mode = some(r.mode);
  if (rec.live) rec.seg = some(r.seg);
  rec.files_n = r.files_n;
  rec.files_rev = r.files_rev;
  put_health(rec, h);
  s.rec = some(rec);
}

// debug.state.diag (gh #21, protocol README item 21): the system page's once-a-second
// numbers, the platform's loop timing and the audio task's stack.
proto::DebugDiag diag_of(const DeviceInfo& d, const LoopStats& l, uint32_t audio_stack_left) {
  proto::DebugDiag g;
  g.heap_free = d.heap_free;
  g.heap_min = d.heap_min;
  g.heap_block = d.heap_max_block;
  g.stack_left = d.stack_hwm;
  g.audio_stack_left = audio_stack_left;
  g.loop_n = l.n;
  g.loop_max_us = l.max_us;
  g.loop_win_max_us = loop_win_max_us(l);
  g.gap_max_us = l.gap_max_us;
  g.gap_win_max_us = gap_win_max_us(l);
  g.loop_over_20 = l.over_20;
  g.loop_over_50 = l.over_50;
  g.loop_over_100 = l.over_100;
  g.loop_over_500 = l.over_500;
  g.ghost_n = d.ghost_n;
  g.kb_reinit_n = d.kb_reinit_n;
  return g;
}

}  // namespace

void Runtime::fill_debug_state(uint32_t now) {
  auto& s = out_.body.emplace<proto::DebugState>();
  const AppState& app = state();
  s.summary = true;
  s.up_ms = now;
  s.link = app.link.mode;
  const char* mod = shell_.top_id();
  s.mod = FixedStr<16>::from(*mod ? mod : "none");
  s.overlay = shell_.overlay();
  if (s.overlay != proto::Overlay::None || shell_.overlay_ms() != 0) s.overlay_ms = some(shell_.overlay_ms());
  const RecInfo& r = app.rec;
  if (r.phase == RecPhase::Confirm || r.phase == RecPhase::EndConfirm) {  // HIL waits out the 400 ms guard
    s.overlay = proto::Overlay::Confirm;
    s.overlay_ms = some(r.opened_ms);
  }
  if (key_seen_) s.key_ms = some(key_ms_);
  s.outbox_pending = outbox_.n;
  s.outbox_inflight = outbox_.n_inflight;
  s.outbox_bad = outbox_.n_bad;
  s.revs = revs_of(app);
  s.need = need_count(app.sessions);
  s.n = app.projects.data.n;
  for (uint8_t i = 0; i < s.n; ++i) {
    const ProjectRow& row = app.projects.data.rows[i];
    s.projects[i].slot = row.slot;
    s.projects[i].run = row.run;
    s.projects[i].need = row.need;
    s.projects[i].queued = row.queued;
  }
  fill_debug_rec(s, app.rec, rec_ ? rec_->health() : rec::RecHealth{});
  s.ui = some(proto::DebugUi{app.ui.menu, app.ui.locked, some(lang())});  // v0.4.0; lang v0.4.3 (docs/21 §7)
  s.diag = some(diag_of(app.device, plat_.loop_stats(), rec_ ? rec_->stack_left() : 0));
}

}  // namespace cpa

#endif  // CPA_TEST_HOOKS
