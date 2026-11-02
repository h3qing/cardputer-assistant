// Runtime: recording and file.* (docs/11 §4.7, §5). The loop side of the audio task:
//   keys -> RecCore -> RecCmd to the audio task; RecEvt back -> RecCore, the index, replies;
//   file.* requests checked here (file_req.h), at most one at a time with the audio task,
//   all answered with E_BUSY "live" while the recording state machine is not Idle.
// file.data is built only when the whole line fits in the TX ring right now: it never
// waits in the 4-line LineQueue (docs/11 §5.4), and its reply is never dropped.
#include <cstdio>
#include <cstring>

#include "rec_overlay.h"
#include "rec_result.h"
#include "reducers.h"
#include "runtime.h"
#include "txt.h"

namespace cpa {

constexpr Txt kCardHoursLeft{"SD 卡剩余约 %u 小时", "SD card: about %u h left"};
constexpr ToastTxt kTooFast{"太快了，再按一次 Enter", "Too fast: press Enter again"};

namespace {

bool same_last(const RecLast& a, const RecLast& b) {
  return a.has == b.has && a.saved == b.saved && a.why == b.why && a.mode == b.mode && a.ms == b.ms &&
         a.seen == b.seen && a.locked == b.locked;
}

// The card side, and (docs/20) how the last recording ended, held past its end, 3.55 V;
// a memo's grace and latch (gh #51).
bool same_card_and_end(const RecInfo& a, const RecInfo& b) {
  return a.files_n == b.files_n && a.files_rev == b.files_rev && a.free_known == b.free_known &&
         a.free_mib == b.free_mib && a.held == b.held && a.bat_low == b.bat_low && a.sd_bad == b.sd_bad &&
         a.grace == b.grace && a.latched == b.latched && same_last(a.last, b.last);
}

// docs/28 S1a: the kept copies and the plug-in hint.
bool same_kept(const KeptView& a, const KeptView& b) {
  return a.segs == b.segs && a.batch_recs == b.batch_recs && a.oldest_s == b.oldest_s && a.batch_host == b.batch_host &&
         a.busy == b.busy && a.incomplete == b.incomplete && a.plug == b.plug && a.plug_n == b.plug_n &&
         a.held_here == b.held_here;
}

bool same_rec(const RecInfo& a, const RecInfo& b) {
  return a.avail == b.avail && a.ready == b.ready && a.phase == b.phase && a.mode == b.mode &&
         a.opened_ms == b.opened_ms && a.started_ms == b.started_ms && a.seg == b.seg && a.stars == b.stars &&
         a.slot == b.slot && a.level == b.level && a.star_ms == b.star_ms && same_card_and_end(a, b) &&
         same_kept(a.kept, b.kept);
}

rec::RecCmd cmd_of(rec::CmdKind kind) {
  rec::RecCmd c;
  c.kind = kind;
  return c;
}

}  // namespace

void Runtime::attach_rec(RecPort& port, const RecBoot& boot) {
  rec_ = &port;
  rec_boot_ = boot;
}

void Runtime::begin_rec(uint32_t now) {
  (void)now;
  rec_side_.free_known = rec_boot_.free_known;
  rec_side_.free_bytes = rec_boot_.free_bytes;
  mirror_rec();
}

bool Runtime::rec_avail() const {
  return rec_ && rec_boot_.buffers && sys_.store == Store::Sd && !card_lost();
}

rec::RecCtx Runtime::rec_ctx() const {
  rec::RecCtx c;
  c.now = now_;
  c.avail = rec_avail();
  c.no_memory = rec_ && sys_.store == Store::Sd && !rec_boot_.buffers;
  c.ids_ok = !ids_.exhausted();
  c.files_n = state().rec.files_n;
  c.free_known = rec_side_.free_known;
  c.free_bytes = rec_side_.free_bytes;
  c.bat_mv = state().device.bat_mv;
  c.slot = state().ctx.slot;
  c.incomplete = kept_.incomplete;  // docs/28 D15
  return c;
}

Opt<proto::FilesInfo> Runtime::files_info() const {
  if (!rec_avail()) return {};
  proto::FilesInfo f;
  // docs/28 S1a: what this Mac may take (never what was taken back from it, nor the
  // offline ones while the plug-in hint holds them)
  f.n = rec_core_.ready ? rec::file_visible_count(rec_->index(), viewer()) : state().rec.files_n;
  f.rev = state().rec.files_rev;  // 0 until the boot scan is done
  f.live = rec::rec_live(rec_core_);
  return some(f);
}

rec::RecRefusal Runtime::rec_gesture(RecMode mode, uint8_t slot) {
  rec::RecCtx c = rec_ctx();
  c.slot = slot;
  const rec::RecStep st = rec::rec_gesture(rec_core_, mode, c);
  rec::RecRefusal why = rec::RecRefusal::None;
  rec::RecStep rest{st.state, {}};
  for (uint8_t i = 0; i < st.effects.count; ++i) {
    const rec::RecEffect& e = st.effects.items[i];
    if (e.kind == rec::RecFx::Refuse) why = e.why;  // the shell shows it
    if (e.kind == rec::RecFx::Refuse) rec_side_.last = rec_last_of(e, shell_.locked());
    if (e.kind != rec::RecFx::Refuse) rest.effects = rest.effects.with(e);
  }
  rec_apply(rest);
  return why;
}

void Runtime::rec_input(rec::RecInput in, uint8_t arg) { rec_apply(rec::rec_input(rec_core_, in, arg, rec_ctx())); }

void Runtime::rec_apply(const rec::RecStep& st) {
  const bool was_held = rec_core_.held;
  const RecPhase was = rec_core_.phase;
  rec_core_ = st.state;
  // docs/20 D3: a box's hints ("Answer the box first", "press Enter again") go with it,
  // however it goes
  if (rec::rec_box(was) && rec_core_.phase != was) shell_.retract_hints();
  if (!rec::rec_live(rec_core_)) rec_side_.stop_due = false;  // the session is over
  for (uint8_t i = 0; i < st.effects.count; ++i) rec_effect(st.effects.items[i]);
  // docs/20 U15: a memo that ended under a held G0 showed its cap screen; Mando's moment
  // (docs/19 §7.2) starts when G0 goes up and home is back, not behind that screen
  if (was_held && !rec_core_.held && state().ui.moment != Moment::None) set_moment(state().ui.moment);
  mirror_rec();
}

rec::RecCmd Runtime::tag_cmd(rec::CmdKind kind, uint8_t slot) const {
  rec::RecCmd c = cmd_of(kind);
  const Opt<ProjectRow> p = project_by_slot(state().projects, slot);
  c.start.slot = slot;
  if (p.has) c.start.project = some(p.value.key);
  return c;
}

void Runtime::rec_effect(const rec::RecEffect& e) {
  char buf[48];
  switch (e.kind) {
    case rec::RecFx::Refuse:
      show_end(e);
      return shell_.toast(ToastLevel::Error, rec_refusal_text(e.why));
    case rec::RecFx::Ended:  // docs/20 U6: every end, worded by how it ended
      show_end(e);
      shell_.retract_hints();  // the battery warning is over with the recording
      set_moment(e.saved ? Moment::Saved : Moment::NoVoice);  // Mando on home (docs/19 §7.2)
      return shell_.toast(rec_end_level(e), rec_end_text(e).c_str());
    case rec::RecFx::BatWarn: return shell_.hint(ToastLevel::Warn, rec_bat_warn_text());
    case rec::RecFx::Warn:
      std::snprintf(buf, sizeof buf, tr(kCardHoursLeft), static_cast<unsigned>(e.hours));
      return shell_.toast(ToastLevel::Warn, buf);
    case rec::RecFx::Start: {
      rec::RecCmd c = tag_cmd(rec::CmdKind::Start, e.slot);
      c.start.mode = e.mode;
      c.start.consent = e.consent;
      c.start.gen = e.gen;  // docs/20 S1: its events come back with it
      if (state().time.known) c.start.wall_offset_ms = some(state().time.offset_ms);
      return post(c);
    }
    case rec::RecFx::Stop: return post(cmd_of(rec::CmdKind::Stop));
    case rec::RecFx::Mark: return post(cmd_of(rec::CmdKind::Mark));
    case rec::RecFx::Tag: return post(tag_cmd(rec::CmdKind::Tag, e.slot));
    case rec::RecFx::FlushBusy: return flush_busy();
    case rec::RecFx::PostNext: return post_next_file();
    case rec::RecFx::TooFast: return shell_.hint(ToastLevel::Info, tr(kTooFast));  // docs/20 D3
  }
}

// docs/20 D7: how a recording ended is shown. Locked, the card lights; unlocked, the screen
// lights as after a key, so a dimmed meeting that stopped by itself (the battery, the cap)
// is not taken dark and locked (D2) before anyone saw it.
void Runtime::show_end(const rec::RecEffect& e) {
  rec_side_.last = rec_last_of(e, shell_.locked());
  if (shell_.locked()) return shell_.lock_flash(now_, kLockFlashMs);
  power_ = power_wake(power_, now_);
  sync_power(now_);
}

// Stars and tags never take the last free slot of the command queue: the Stop needs it.
// A Start the queue refused never reached the task: it fails at once. A refused Stop is
// never made up (the task is still recording): pump_rec posts it again until it is taken.
void Runtime::post(const rec::RecCmd& c) {
  const bool optional = c.kind == rec::CmdKind::Mark || c.kind == rec::CmdKind::Tag;
  if (rec_ && optional && rec_->room() < 2) return;
  if (rec_ && rec_->post(c)) return;
  if (c.kind == rec::CmdKind::Stop) rec_side_.stop_due = true;
  if (c.kind != rec::CmdKind::Start) return;
  rec::RecEvt e;
  e.kind = rec::EvtKind::Failed;
  e.fail = rec::RecFail::Internal;
  e.gen = c.start.gen;
  rec_event(e);
}

void Runtime::mirror_rec() {
  RecInfo r;
  r.avail = rec_avail();
  r.ready = rec_core_.ready;
  r.phase = rec_core_.phase;
  r.mode = rec_core_.mode;
  r.opened_ms = rec_core_.opened_ms;
  r.started_ms = rec_core_.started_ms;
  r.seg = rec_core_.seg;
  r.stars = rec_core_.stars;
  r.slot = rec_core_.slot;
  r.level = rec_core_.level;
  r.star_ms = rec_core_.star_ms;
  if (rec_ && rec_core_.ready) {  // the audio task fills the index until IndexReady
    r.files_n = rec_->index().count();
    r.files_rev = rec_->index().rev();
  }
  r.free_known = rec_side_.free_known;
  r.free_mib = static_cast<uint32_t>(rec_side_.free_bytes >> 20);
  r.held = rec_core_.held;
  r.bat_low = rec_core_.bat_low;
  r.sd_bad = rec_ != nullptr && (sys_.store != Store::Sd || card_lost());  // docs/20 U16: ※SD
  r.grace = rec::rec_grace(rec_core_);  // gh #51
  r.latched = rec::rec_latched(rec_core_);
  r.last = rec_side_.last;
  r.kept = kept_view();
  if (same_rec(state().rec, r)) return;
  RecInfo old = state().rec;
  old.level = r.level;  // only the level moved: the animation shows it (docs/19 §3.5)
  update([&](AppState& s) { s.rec = r; }, !same_rec(old, r));
}

void Runtime::retry_stop() {
  if (rec_side_.stop_due && rec_->post(cmd_of(rec::CmdKind::Stop))) rec_side_.stop_due = false;
}

void Runtime::pump_rec(uint32_t now) {
  if (!rec_) return;
  (void)now;
  retry_stop();
  if (rec_side_.held && send_data(rec_side_.held_evt)) {
    rec_side_.held = false;
    rec_apply(rec::rec_file_done(rec_core_));
  }
  // A held file.data does not stop the other events: only one file request is ever with
  // the audio task, so nothing else can touch the transfer buffer meanwhile.
  rec::RecEvt e;
  for (int i = 0; i < 8 && rec_->poll(e); ++i) rec_event(e);
  rec_seen();
  if (shell_.menu_open()) rec_input(rec::RecInput::Touch, 0);  // docs/20 D3: a box waits under the menu
}

// After the keys (gh #51 review): a memo's grace ends only once this pass's G0 sample was
// read, or a re-press after a long pass would come too late to keep it.
void Runtime::tick_rec() {
  if (rec_) rec_apply(rec::rec_tick(rec_core_, rec_ctx()));
}

// Dimmed is not seen: a recording on battery dims the screen while it lies unread; nor is
// the lock card lit by itself (docs/20 D7: it may light an empty pocket). A card that
// answered a swallowed G0 hold ("Not recording") ends the last result: shown after that
// hold, or after the tap it asks for, an old "√ Saved" reads as the hold's own (its toast
// still waits).
void Runtime::rec_seen() {
  RecLast& l = rec_side_.last;
  if (!l.has) return;
  if (shell_.locked() && state().ui.lock_held) {
    l = RecLast{};
    return;
  }
  const bool lit = level_ == Backlight::On && (!shell_.locked() || shell_.lock_seen(now_));
  if (lit) {
    l.seen = true;
  } else if (l.seen) {
    l = RecLast{};
  }
}

void Runtime::rec_event(const rec::RecEvt& e) {
  switch (e.kind) {
    case rec::EvtKind::Items:
    case rec::EvtKind::Data:
    case rec::EvtKind::DelDone: return file_reply(e);
    case rec::EvtKind::Committed: committed(e); break;
    case rec::EvtKind::Requeued:
    case rec::EvtKind::LedgerDone:
    case rec::EvtKind::Kept: return kept_event(e);  // docs/28 S1a
    case rec::EvtKind::IndexReady:
      kept_.incomplete = e.kept;  // D15
      kept_.tidy_due = true;      // what aged out while the device was off
      break;
    case rec::EvtKind::Level: shell_.on_level(e.level); break;
    default: break;  // Saved / Discarded: the core's Ended effect says how (docs/20 U6)
  }
  const bool cue = rec_core_.phase == RecPhase::Cue;
  rec_apply(rec::rec_event(rec_core_, e, rec_ctx()));
  // A new level history only when the core took this Started (a late one of an earlier
  // recording is ignored, docs/20 S1).
  if (cue && rec_core_.phase == RecPhase::Recording) shell_.rec_started();
}

// The link was lost (unplugged, timeout, the daemon restarted): every file.* request of the
// old session is void. The queued ones are forgotten, a held reply is dropped; the one
// with the audio task is still answered when it comes back (a stale reply is ignored).
void Runtime::rec_link_lost() {
  if (!rec_) return;
  rec_side_.n = 0;
  rec_apply(rec::rec_files_dropped(rec_core_));
  if (!rec_side_.held) return;
  rec_side_.held = false;
  rec_apply(rec::rec_file_done(rec_core_));
}

void Runtime::committed(const rec::RecEvt& e) {
  rec::RecIndex& idx = rec_->index();
  idx.insert(e.id, e.size, 0, e.seg == 0);
  if (rec_side_.free_known) rec_side_.free_bytes = rec_side_.free_bytes > e.size ? rec_side_.free_bytes - e.size : 0;
  if (!idx.full() || !rec::rec_live(rec_core_)) return;
  // 256 segments waiting: stop at the next block; the end names the reason (docs/20 U6)
  rec_apply(rec::rec_stop_for(rec_core_, rec::RecRefusal::Pending, rec_ctx()));
}

}  // namespace cpa
