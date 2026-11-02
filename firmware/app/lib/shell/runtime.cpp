// Runtime: construction, boot and the loop stages (05 §5.1).
#include "runtime.h"

#include "chord_detector.h"
#include "outbox_io.h"
#include "protocol_limits.h"
#include "reducers.h"

namespace cpa {
namespace {

#if CPA_TEST_HOOKS
KeySet key_union(KeySet a, KeySet b) {
  KeySet out = a;
  for (uint8_t k = 0; k <= key::kG0; ++k) out = b.has(k) ? out.with(k) : out;
  return out;
}
#endif

class NoProgress : public MigrateObserver {
 public:
  void on_item(uint16_t, uint16_t) override {}
};

// The link as the screen shows it. Not last_rx_ms: every inbound line moves it (each
// file.get of a pull), and only the system page shows it, as an age in whole seconds.
bool same_link(const LinkInfo& a, const LinkInfo& b) {
  return a.mode == b.mode && a.agent == b.agent && a.cap_spawn == b.cap_spawn && a.cap_reply == b.cap_reply &&
         a.version_mismatch == b.version_mismatch;
}

}  // namespace

Runtime::Runtime(Transport& tx, FileStore& fs, Platform& plat, Display& disp, Module* const* modules, uint8_t n)
    : tx_(tx), fs_(fs), plat_(plat), disp_(disp), shell_(*this, modules, n) {}

void Runtime::begin(const BootInfo& boot, uint32_t now, const StorageBoot& storage) {
  now_ = now;
  begin_settings();  // before the first frame, the migration screen's included
  begin_kept();      // docs/28: the offline mark NVS holds
  if (boot.ticket.valid()) ids_.start(boot.ticket);
  sys_.fw = FixedStr<31>::from(kFwVersion);
  sys_.dev = boot.dev;
  sys_.rst = boot.rst;
  sys_.ids_ok = boot.ticket.valid();
  begin_storage(storage);
  const ScanResult& scan = outbox_boot_scan(fs_, codec_);
  outbox_ = scan.core;
  sys_.outbox_broken = scan.broken;
  sys_.outbox_deferred = scan.deferred;
  boot_free_ = fs_.free_bytes();  // the only usage walk (see kItemBlockBytes)
  if (sd_lost_at_boot()) store_failed();
  boot_items_ = static_cast<uint32_t>(outbox_.n) + outbox_.n_bad;
  load_projects_cache();
  update([&](AppState& s) {
    s.device.boot = ids_.boot_id();
    s.outbox = outbox_info(outbox_, free_estimate());
  });
  plug_ = plug_filter(PlugFilter{}, tx_.plugged(), now);
  link_ = link_tick(LinkCore{}, plug_.plugged, now).state;
  sync_link_slice();
  power_.last_key_ms = now;
  begin_rec(now);
  shell_.begin(now);
  sys_.modules_disabled = shell_.disabled_count();
}

void Runtime::begin_storage(const StorageBoot& sb) {
  sys_.store = sb.plan.primary;
  sys_.sd = sb.sd.state;
  sys_.sd_mib = sb.sd.size_mib;
  sys_.launcher = sb.part.launcher;
  sys_.cpafs_bytes = sb.part.cpafs_bytes;
  sys_.note = sb.plan.note;
  if (sb.migrate_from) migrate_boot(*sb.migrate_from, sb.observer);
}

// cpafs -> SD card (09 §3.7): the source's own boot scan first (v0.1 rules: orphan .tmp
// removed, broken items to its bad/), then each valid item is committed on the card
// before it leaves the source.
void Runtime::migrate_boot(FileStore& from, MigrateObserver* obs) {
  NoProgress quiet;
  const ScanResult& src = outbox_boot_scan(from, codec_);
  const MigrateResult m = outbox_migrate(from, fs_, src.core, obs ? *obs : quiet);
  sys_.migrated = m.moved;
  sys_.migrate_failed = m.failed;
}

void Runtime::store_failed() {
  sys_.store_errors = static_cast<uint16_t>(sys_.store_errors < 0xFFFF ? sys_.store_errors + 1 : 0xFFFF);
  shell_.mark_dirty();
}

void Runtime::loop(uint32_t now, KeySet hw_keys) {
  now_ = now;
  tx_.tick(now);
  pump_rx(now);
  flush_agent_save();
  pump_timers(now);
  flush_agent_save();  // before keys: a new request would replace the draft
  pump_rec(now);
  pump_kept(now);  // docs/28 S1a: the offline mark, the plug-in hint, tidying
  pump_keys(now, hw_keys);
  tick_rec();  // gh #51: the grace sees this pass's G0
  if (settings_due_) sync_settings();
  pump_outbox(now);
  sample_stats(now);
  shell_.tick(now, backlight_);
  render(now);
}

void Runtime::pump_rx(uint32_t now) {
  size_t lines = 0;
  while (lines < limits::kDeviceLinesPerLoop) {
    if (rx_pos_ == rx_len_) {
      rx_len_ = tx_.read(rx_, kRxChunk);
      rx_pos_ = 0;
      if (rx_len_ == 0) return;
    }
    if (scan_once(lines) == FrameEvent::Line) handle_line(framer_line(framer_), framer_line_size(framer_), now);
  }
}

// One framer step over the buffered bytes. Kept apart from handle_line() so that the
// framer's 2 KB result is off the stack before a line is decoded.
FrameEvent Runtime::scan_once(size_t& lines) {
  const FrameStep st = framer_scan(framer_, rx_ + rx_pos_, rx_len_ - rx_pos_);
  framer_ = st.framer;
  rx_pos_ += st.consumed;
  if (st.event != FrameEvent::None) ++lines;
  return st.event;
}

void Runtime::pump_keys(uint32_t now, KeySet hw) {
  KeySet keys = hw;
#if CPA_TEST_HOOKS
  const InjectStep inj = inject_tick(inject_, now);
  inject_ = inj.queue;
  keys = key_union(keys, inj.keys);
#endif
  const GhostStep g = ghost_filter(ghost_, keys);
  const uint16_t ghosts = ghost_.ghost_n;
  ghost_ = g.state;
  const KbWatchStep w = kb_watch_step(kbw_, g.keys, now);
  kbw_ = w.state;
  if (w.reinit) plat_.reinit_keyboard();
  if (w.reinit || ghost_.ghost_n != ghosts) {
    update([&](AppState& s) {
      s.device.ghost_n = ghost_.ghost_n;
      s.device.kb_reinit_n = static_cast<uint16_t>(s.device.kb_reinit_n + (w.reinit ? 1 : 0));
      s.device.kb_warn_ms = now;  // docs/20 U21: ※k shows 10 s from here
    });
  }
  if (!g.keys.minus(chords_.prev).empty()) {
    key_ms_ = now;
    key_seen_ = true;
  }
  const bool g0_was = chords_.prev.has(key::kG0);
  const ChordStep c = chord_step(chords_, KeySample{g.keys, now});
  chords_ = c.state;
  const bool g0 = c.state.prev.has(key::kG0);
  shell_.g0_level(g0, g0 && !c.state.g0_long_sent, now);  // docs/20 U3c, D8: before its events
  for (uint8_t i = 0; i < c.effects.count; ++i) key_event(c.effects.items[i], now);
  // gh #51: a memo follows G0's edges, after the sample's events (a G0Long and its release
  // in one late sample start the memo before it is let go)
  if (g.keys.has(key::kG0) != g0_was) shell_.on_g0(g.keys.has(key::kG0), now);
}

// docs/13 §5.4: 1. the lock gate, before power_key, so a locked device stays dark and a
// swallowed key wakes nothing; 2. power_key; then the shell.
void Runtime::key_event(const KeyEvent& ev, uint32_t now) {
  if (shell_.lock_gate(ev, now)) {
    if (rec::rec_box(rec_core_.phase)) rec_input(rec::RecInput::Touch, 0);  // docs/20 D3: from its last key
    const bool was_dark = power_.dark;  // dimmed while recording is not dark: keys go through
    const PowerStep p = power_key(power_, ev, now);
    power_ = p.state;
    // G0 wakes into the quick note, but never submits an open editor on the way. Locked,
    // the gate has decided already; Opt+L locks from a dark screen too, which stays dark
    // (docs/13 §7.1), so neither wakes into anything.
    const bool gated = shell_.locked() || shell_.locks(ev);
    const bool swallow = !gated && (p.swallow || (was_dark && shell_.overlay() != proto::Overlay::None));
    if (!swallow && !plug_key(ev, now)) shell_.on_key(ev, now);  // docs/28: the hint's Tab, Enter
  }
  sync_power(now);
}

void Runtime::pump_timers(uint32_t now) {
  plug_ = plug_filter(plug_, tx_.plugged(), now);
  const LinkStep s = link_tick(link_, plug_.plugged, now);
  link_ = s.state;
  for (uint8_t i = 0; i < s.effects.count; ++i) link_changed(s.effects.items[i], nullptr, now);
  sync_link_slice();
  agent_events(agent_tick(state().agent, now), now);
  if (small_.inflight && now - small_.sent_ms >= kSmallReqTimeoutMs) small_.inflight = false;
  const bool was_dark = power_.dark;
  power_ = power_tick(power_recording(power_, rec_keeps_lit()), tx_.plugged(), now);
  if (power_.dark && !was_dark) shell_.auto_lock();  // docs/20 D2: only on battery goes it dark
  sync_power(now);
}

// The recording and the lock as they are now, then the backlight (docs/13 §7.1-§7.2).
void Runtime::sync_power(uint32_t now) {
  power_ = power_recording(power_, rec_keeps_lit());
  power_ = power_lock(power_, shell_.locked(), shell_.lock_lit(now), now);
  sync_backlight();
}

// Off the On level nothing moves (docs/19 §7.4) and the big ● stands at full Rec: the full
// redraw this asks for pushes it so.
void Runtime::sync_backlight() {
  const Backlight lv = backlight_of(power_);
  if (lv == level_) return;
  level_ = lv;
  backlight_ = lv != Backlight::Off;  // dimmed still renders: ● REC stays visible
  plat_.set_backlight_level(lv);
  shell_.set_backlight(lv);
  if (lv != Backlight::On) disp_.breathe(0);
  shell_.mark_dirty();
}

// docs/29 P1: the facts of idle_yield.h as this pass left them.
uint32_t Runtime::idle_yield_ms() const {
  IdleFacts f;
  f.dark = level_ == Backlight::Off;
  f.rec_idle = rec_core_.phase == RecPhase::Idle && !rec_core_.held;
  f.host = tx_.plugged() || plug_.plugged;
  f.audio_busy = (rec_ && !rec_core_.ready) || rec::rec_inflight(rec_core_) > 0 || kept_.busy || kept_.hold_due ||
                 kept_.hold_sent || kept_.drop_due;
  f.keys_down = !chords_.prev.empty();
  f.key_seen = key_seen_;
  f.key_ms = key_ms_;
  f.writes_due = settings_due_ || kept_.mark_due;
#if CPA_TEST_HOOKS
  f.writes_due = f.writes_due || inject_.n > 0;
#endif
  return cpa::idle_yield_ms(f, now_);
}

void Runtime::sample_stats(uint32_t now) {
  if (now - last_stats_ < kStatsEveryMs) return;
  last_stats_ = now;
  const DeviceStats st = plat_.stats();
  update([&](AppState& s) {
    s.device.bat_mv = st.bat_mv;
    s.device.heap_free = st.heap_free;
    s.device.heap_min = st.heap_min;
    s.device.heap_max_block = st.heap_max_block;
    s.device.stack_hwm = st.stack_hwm;
    s.outbox = outbox_info(outbox_, free_estimate());
  });
}

uint32_t Runtime::free_estimate() const {
  const int64_t items = static_cast<int64_t>(outbox_.n) + outbox_.n_bad;
  const int64_t est = static_cast<int64_t>(boot_free_) + (static_cast<int64_t>(boot_items_) - items) * kItemBlockBytes;
  return est < 0 ? 0 : (est > 0xFFFFFFFFLL ? 0xFFFFFFFFu : static_cast<uint32_t>(est));
}

// A full redraw when something changed (at most 20 fps) and once a second; between them,
// only while the backlight is On, the animation's small pushes (docs/19 §3.5).
void Runtime::render(uint32_t now) {
  const bool due = shell_.dirty() && now - last_render_ >= kRenderMinMs;
  const bool stale = !rendered_ || now - last_render_ >= kRenderEveryMs;
  if (due || stale) return render_full(now);
  if (level_ == Backlight::On) animate(now);
}

void Runtime::render_full(uint32_t now) {
  if (!backlight_ && rendered_ && !shell_.dirty()) return;  // dark and nothing new
#if CPA_TEST_HOOKS
  const uint32_t t0 = plat_.micros();
#endif
  shell_.render(disp_.canvas(), now);
  disp_.present();
#if CPA_TEST_HOOKS
  const uint32_t us = plat_.micros() - t0;
  if (us > times_.max_render_us) times_.max_render_us = us;
#endif
  shell_.clear_dirty();
  last_render_ = now;
  rendered_ = true;
}

void Runtime::animate(uint32_t now) {
#if CPA_TEST_HOOKS
  const uint32_t t0 = plat_.micros();
#endif
  const AnimFrame f = shell_.animate(disp_.canvas(), now);
  if (f.breath >= 0) disp_.breathe(static_cast<uint8_t>(f.breath));
  for (uint8_t i = 0; i < f.n; ++i) disp_.present(f.rects[i]);
#if CPA_TEST_HOOKS
  const uint32_t us = plat_.micros() - t0;
  if (us > times_.max_tick_us) times_.max_tick_us = us;
#endif
}

// last_rx_ms alone is copied without a redraw (#43: a frame every 50 ms during a pull
// held back the file.get replies); the once-a-second frame keeps the age current.
void Runtime::sync_link_slice() {
  const bool shown = !same_link(state().link, link_.info);
  if (!shown && state().link.last_rx_ms == link_.info.last_rx_ms) return;
  update([&](AppState& s) { s.link = link_.info; }, shown);
}

void Runtime::set_context_slot(uint8_t slot) {
  if (state().ctx.slot == slot) return;
  update([&](AppState& s) { s.ctx.slot = slot; });
}

// The shell owns the menu and the lock; the moment is the runtime's (set_moment).
void Runtime::set_ui(const UiFlags& ui) {
  const UiFlags& was = state().ui;
  if (was.menu == ui.menu && was.locked == ui.locked && was.memo_ready == ui.memo_ready &&
      was.lock_held == ui.lock_held && was.pat.n == ui.pat.n) {
    return;
  }
  update([&](AppState& s) {
    s.ui.menu = ui.menu;
    s.ui.locked = ui.locked;
    s.ui.memo_ready = ui.memo_ready;
    s.ui.lock_held = ui.lock_held;
    s.ui.pat = ui.pat;
  });
}

void Runtime::set_moment(Moment m) {
  update([&](AppState& s) {
    s.ui.moment = m;
    s.ui.moment_ms = now_;
  });
}

}  // namespace cpa
