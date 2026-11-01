// Runtime: the kept copies and the plug-in hint (docs/28 S1a). The loop side of the ledger:
//   - the Mac on the link is the CRC-32 of its hello's host_id (0: none sent): a Del tells the
//     audio task who receives the segment, and the index hides from a Mac what was taken
//     back from it (file_req.h Viewer);
//   - every unplug stores the offline mark (the next id to be issued) in NVS, never while
//     recording; a hello with offline segments waiting opens the hint (plug_hint.h);
//   - the wall clock ages kept copies only once trusted (D16, ledger_policy.h);
//   - the screen's "deliver again", "give to this Mac" and "clear kept copies" are Ledger
//     commands; one at a time, never while recording, the screen waits for LedgerDone (D17,
//     D18); one never answered frees the screen after kLedgerWaitMs (review 7);
//   - the hint's time stands still while it cannot be seen (locked, the Tab menu), and its
//     Hold remembers its Mac and ids until LedgerDone, unplugged or not (review 1-3);
//   - plugged in, a card short of space is tidied between file requests (review 4).
#include <cstdio>

#include "file_req.h"
#include "runtime.h"
#include "txt.h"

namespace cpa {

constexpr ToastTxt kHeldHere{"已不给这台 Mac：%u 条", "Kept from this Mac: %u"};
constexpr ToastTxt kGivenHere{"已给这台 Mac：%u 段", "Given to this Mac: %u"};
constexpr ToastTxt kGivenAsUsual{"没改成，已照常给这台 Mac", "Not changed: this Mac gets them"};
constexpr ToastTxt kNoAnswer{"没改成：没有回应", "Not changed: no answer"};
constexpr ToastTxt kQueuedAgain{"已排回：%u 次录音，别的 Mac 来拿", "Queued again: %u for another Mac"};
constexpr ToastTxt kClearedKept{"已清掉留底", "Kept copies cleared"};
constexpr ToastTxt kNotChanged{"没改成：SD 卡出错", "Not changed: SD card error"};
constexpr ToastTxt kNotWhole{"不完整，不能再交", "Incomplete: cannot deliver again"};
constexpr ToastTxt kTooManyWaiting{"待传太多，先同步", "Too many waiting: sync first"};
constexpr ToastTxt kNothingKept{"没有可再交的", "Nothing to deliver again"};
constexpr ToastTxt kBusyNow{"录音中，录完再试", "Recording: try again after"};

constexpr uint32_t kLedgerWaitMs = 30000;  // review 7: a LedgerDone never heard

namespace {

uint32_t host_hash(const Opt<proto::HostId>& id) {
  if (!id.has || id.value.empty()) return 0;
  return rec::crc32_ieee(reinterpret_cast<const uint8_t*>(id.value.c_str()), id.value.size());
}

}  // namespace

void Runtime::begin_kept() { kept_.mark = plat_.load_offline_mark(); }

// A Hold on its way holds its ids from every Mac until LedgerDone (its ids are never newer
// than a hint's, which comes from a later offline mark).
rec::Viewer Runtime::viewer() const {
  rec::Viewer v;
  const bool holding = kept_.hold_due || kept_.hold_sent;
  v.host = kept_.host;
  v.hold = plug_holds(kept_.plug) || holding;
  v.hold_from = holding ? kept_.hold_from : kept_.plug.from;
  return v;
}

uint32_t Runtime::wall_now_s() const {
  const TimeInfo& t = state().time;
  if (!kept_.clock_ok || !t.known) return 0;
  const int64_t ms = static_cast<int64_t>(now_) + t.offset_ms;
  return ms > 0 ? static_cast<uint32_t>(ms / 1000) : 0;
}

// docs/07 §11.3: set before the hello is answered, so its files.n counts what this Mac may
// take; a new Mac moves files.rev, so a Mac whose last listing was at this rev lists again.
void Runtime::kept_host(const proto::Hello& h) {
  const uint32_t host = host_hash(h.host.host_id);
  if (host == kept_.host) return;
  kept_.host = host;
  if (!rec_ || !rec_core_.ready) return;
  rec_->index().touch();
  mirror_rec();
}

void Runtime::kept_hello(const proto::Hello& h) {
  kept_host(h);
  kept_.plug_due = true;
}

void Runtime::kept_link_lost() {
  kept_.host = 0;
  kept_.plug = PlugState{};
  kept_.plug_due = false;  // a Hold on its way goes on (hold_host): review 3
  const Opt<MonoId> next = ids_.peek();
  if (next.has) {
    kept_.mark_next = OfflineMark{next.value.boot, next.value.seq};
    kept_.mark_due = true;
  }
  if (kept_.tidy_due) post_ledger(rec::LedgerAsk::Tidy);
}

// D16: a time.set ages kept copies only when close to the clock seen before it.
void Runtime::kept_time() {
  const TimeInfo& t = state().time;
  if (!t.known) return;
  const int64_t wall_ms = static_cast<int64_t>(now_) + t.offset_ms;
  const rec::ClockStep st = rec::clock_check(kept_.clock, static_cast<uint32_t>(wall_ms / 1000), now_ / 1000);
  kept_.clock = st.ref;
  kept_.clock_ok = st.trusted;
  if (!st.trusted) return;
  kept_.tidy_due = true;
  post_ledger(rec::LedgerAsk::Tidy);
}

bool Runtime::post_ledger(rec::LedgerAsk ask, uint32_t host, const MonoId& lo) {
  if (ask == rec::LedgerAsk::Tidy && kept_.info.segs == 0) kept_.tidy_due = false;  // nothing to tidy
  if (ask == rec::LedgerAsk::Tidy && !kept_.tidy_due) return false;
  if (!rec_avail() || kept_.busy || !rec_core_.ready || rec::rec_live(rec_core_)) return false;
  rec::RecCmd c;
  c.kind = rec::CmdKind::Ledger;
  c.ledger.ask = ask;
  c.ledger.host = host;
  c.ledger.lo = lo;
  c.ledger.wall_s = wall_now_s();
  c.ledger.room = static_cast<uint16_t>(rec::kIndexMax - rec_->index().count());
  c.ledger.free_known = rec_side_.free_known;
  c.ledger.free_bytes = rec_side_.free_bytes;
  if (!rec_->post(c)) return false;
  kept_.busy = true;
  kept_.busy_since = now_;
  kept_.ask = ask;
  if (ask == rec::LedgerAsk::Tidy) kept_.tidy_due = false;
  mirror_rec();
  return true;
}

bool Runtime::kept_ask(KeptAsk a) {
  if (rec::rec_live(rec_core_)) {
    shell_.hint(ToastLevel::Warn, tr(kBusyNow));
    return false;
  }
  if (a == KeptAsk::Give) return post_ledger(rec::LedgerAsk::Give, kept_.host);
  return post_ledger(a == KeptAsk::Again ? rec::LedgerAsk::Again : rec::LedgerAsk::Clear);
}

// Each pass: the mark once no recording runs, the hint once the index is ready, its 5 s
// (standing still while it cannot be seen), then the ledger's own work.
void Runtime::pump_kept(uint32_t now) {
  const bool live = rec::rec_live(rec_core_);
  if (kept_.mark_due && !live) {
    kept_.mark_due = false;
    const bool same = kept_.mark.has && kept_.mark.value == kept_.mark_next;
    if (!same && plat_.save_offline_mark(kept_.mark_next)) kept_.mark = some(kept_.mark_next);
  }
  // the time since the last pass, hidden, before a hint opens now
  if (shell_.locked() || shell_.menu_open()) kept_.plug = plug_hide(kept_.plug, now - kept_.plug_seen);
  kept_.plug_seen = now;
  if (kept_.plug_due && rec_core_.ready && !live && link_ready(link_)) plug_check(now);
  post_hold(now);
  plug_apply(cpa::plug_tick(kept_.plug, now));
  pump_ledger(now);
}

// The watchdog, a Clear's next round, and tidying: unplugged, or plugged in and short of
// space between file requests (a pull frees nothing now: the copies stay, and a recording
// at the desk could reach the card's end).
void Runtime::pump_ledger(uint32_t now) {
  if (kept_.busy && now - kept_.busy_since >= kLedgerWaitMs) ledger_lost();
  if (kept_.drop_due && post_ledger(rec::LedgerAsk::Clear)) kept_.drop_due = false;
  const bool low = rec_side_.free_known && rec_side_.free_bytes < rec::kKeptFreeFloor;
  const bool files_idle = rec_core_.queued == 0 && !rec_core_.at_task;
  if (kept_.tidy_due && (!link_ready(link_) || (low && files_idle))) post_ledger(rec::LedgerAsk::Tidy);
}

// No hint for a Mac without a host_id (an old cpad): nothing could be kept from it.
void Runtime::plug_check(uint32_t now) {
  kept_.plug_due = false;
  if (!kept_.mark.has || !rec_ || kept_.host == 0) return;
  const Opt<MonoId> next = ids_.peek();
  MonoId from;
  from.epoch = next.has ? next.value.epoch : ids_.epoch();
  from.boot = kept_.mark.value.boot;
  from.seq = kept_.mark.value.seq;
  const rec::Offline o = rec::file_offline(rec_->index(), from, kept_.host);
  if (o.segs == 0) return;
  kept_.plug = plug_open(static_cast<uint8_t>(o.recs > 99 ? 99 : o.recs), from, now);
  mirror_rec();
}

void Runtime::plug_apply(const PlugStep& st) {
  const PlugHint was = kept_.plug.phase;
  const PlugState before = kept_.plug;
  kept_.plug = st.st;
  if (st.act == PlugDo::Hold) {  // posted now, or once the ledger command before it is done
    kept_.hold_due = true;
    kept_.hold_host = kept_.host;
    kept_.hold_from = before.from;
    kept_.hold_n = before.n;
    kept_.hold_since = st.st.since;
    post_hold(st.st.since);
  }
  if (st.act == PlugDo::Release && rec_) rec_->index().touch();  // files.rev moves: list again
  if (was != kept_.plug.phase) mirror_rec();
}

// The hint's "not to this Mac" waits (still held) for the audio task to take it; one that
// cannot be taken within kPlugAskMs lets them go to this Mac, and says why (no stuck bar).
void Runtime::post_hold(uint32_t now) {
  if (!kept_.hold_due) return;
  if (post_ledger(rec::LedgerAsk::Hold, kept_.hold_host, kept_.hold_from)) {
    kept_.hold_due = false;
    kept_.hold_sent = true;
    return;
  }
  if (now - kept_.hold_since < kPlugAskMs) return;
  kept_.hold_due = false;
  hold_gave_up(rec::rec_live(rec_core_) ? kBusyNow : kGivenAsUsual);  // review 6: not an SD error
}

void Runtime::hold_gave_up(const Txt& why) {
  if (kept_.plug.phase == PlugHint::Busy) kept_.plug = PlugState{};
  if (rec_) rec_->index().touch();
  shell_.toast(ToastLevel::Error, tr(why));
  mirror_rec();
}

// Review 7: a LedgerDone never heard (a full event queue) frees the screen and the hint.
void Runtime::ledger_lost() {
  kept_.busy = false;
  kept_.drop_due = false;
  kept_.hold_sent = false;
  hold_gave_up(kNoAnswer);
}

// Locked or under the Tab menu the bar is not on the screen: the keys are not its (review 2).
bool Runtime::plug_key(const KeyEvent& ev, uint32_t now) {
  if (!plug_holds(kept_.plug) || shell_.locked() || shell_.menu_open()) return false;
  const PlugStep st = cpa::plug_key(kept_.plug, ev, now);
  plug_apply(st);
  return st.taken;
}

void Runtime::kept_event(const rec::RecEvt& e) {
  switch (e.kind) {
    case rec::EvtKind::Requeued: rec_->index().insert(e.id, e.size, e.host, e.first); break;
    case rec::EvtKind::LedgerDone: ledger_done(e); break;
    case rec::EvtKind::Kept:
      kept_.info = e.info;
      if (!kept_.clock.has && e.info.newest_s) kept_.clock = rec::ClockRef{true, e.info.newest_s, 0};
      break;
    default: break;
  }
  mirror_rec();
}

void Runtime::ledger_done(const rec::RecEvt& e) {
  kept_.busy = false;
  const bool ok = e.no == rec::LedgerNo::None;
  if (ok && rec_side_.free_known) rec_side_.free_bytes += static_cast<uint64_t>(e.size) << 20;
  const bool drop = e.ask == rec::LedgerAsk::Clear || e.ask == rec::LedgerAsk::Tidy;
  // a Clear or Tidy goes a few recordings a command (a Start never waits long): ask again
  if (ok && e.first && drop) {
    kept_.tidy_due = kept_.tidy_due || e.ask == rec::LedgerAsk::Tidy;
    kept_.drop_due = e.ask == rec::LedgerAsk::Clear;
    return;
  }
  if (e.ask == rec::LedgerAsk::Hold) return hold_done(e);
  if (e.ask == rec::LedgerAsk::Give && e.no == rec::LedgerNo::None) rec_->index().give_to(kept_.host);
  if (e.ask != rec::LedgerAsk::Tidy) say_done(e);
}

// The Hold's own Mac and ids (an unplug may have come between), and its recordings counted
// as the hint counted them (review 5: e.n counts segments).
void Runtime::hold_done(const rec::RecEvt& e) {
  kept_.hold_sent = false;
  const bool ok = e.no == rec::LedgerNo::None || e.no == rec::LedgerNo::Nothing;
  if (ok) rec_->index().exclude_from(kept_.hold_from, kept_.hold_host);
  if (kept_.plug.phase == PlugHint::Busy) kept_.plug = plug_done(kept_.plug);
  if (!ok) return shell_.toast(ToastLevel::Error, tr(kNotChanged));
  char buf[96];
  std::snprintf(buf, sizeof buf, tr(kHeldHere), static_cast<unsigned>(kept_.hold_n));
  shell_.toast(ToastLevel::Ok, buf);
}

// What the screen's own ask came to: cleared, queued again, or why not.
void Runtime::say_done(const rec::RecEvt& e) {
  if (e.no != rec::LedgerNo::None) return shell_.toast(ToastLevel::Error, tr(ledger_refusal(e.no)));
  if (e.ask == rec::LedgerAsk::Clear) return shell_.toast(ToastLevel::Ok, tr(kClearedKept));
  char buf[96];
  std::snprintf(buf, sizeof buf, tr(e.ask == rec::LedgerAsk::Give ? kGivenHere : kQueuedAgain), static_cast<unsigned>(e.n));
  shell_.toast(ToastLevel::Ok, buf);
}

const Txt& Runtime::ledger_refusal(rec::LedgerNo no) {
  switch (no) {
    case rec::LedgerNo::Incomplete: return kNotWhole;
    case rec::LedgerNo::TooMany: return kTooManyWaiting;
    case rec::LedgerNo::Nothing: return kNothingKept;
    case rec::LedgerNo::Busy: return kBusyNow;
    default: return kNotChanged;
  }
}

KeptView Runtime::kept_view() const {
  KeptView k;
  k.segs = kept_.info.segs;
  k.batch_recs = kept_.info.batch_recs;
  k.oldest_s = kept_.info.oldest_s;
  k.batch_host = kept_.info.batch_host;
  k.busy = kept_.busy;
  k.incomplete = kept_.incomplete;
  k.plug = kept_.plug.phase;
  k.plug_n = kept_.plug.n;
  k.held_here = rec_ && rec_core_.ready ? rec_->index().held_from(kept_.host) : 0;
  return k;
}

}  // namespace cpa
