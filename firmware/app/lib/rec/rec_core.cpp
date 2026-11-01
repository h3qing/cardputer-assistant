#include "rec_core.h"

#include "protocol_limits.h"

namespace cpa {
namespace rec {
namespace {

RecEffect fx(RecFx kind) {
  RecEffect e;
  e.kind = kind;
  return e;
}

RecStep keep(const RecCoreState& s) { return RecStep{s, {}}; }

RecStep refuse(const RecCoreState& s, RecRefusal why) {
  RecEffect r = fx(RecFx::Refuse);
  r.why = why;
  return RecStep{s, Effects<RecEffect, 6>{}.with(r)};
}

RecCoreState enter(const RecCoreState& s, RecPhase phase, uint32_t now) {
  RecCoreState out = s;
  out.phase = phase;
  out.opened_ms = now;
  out.box_ms = now;
  return out;
}

// docs/20 D3: a key on a box starts its time again (the guard still counts from the opening).
RecStep touch(const RecCoreState& s, uint32_t now) {
  if (!rec_box(s.phase)) return keep(s);
  RecCoreState out = s;
  out.box_ms = now;
  return keep(out);
}

// Leaving Idle: every queued file.* gets E_BUSY "live" before anything else is posted.
RecStep leave_idle(const RecStep& st) {
  if (st.state.queued == 0) return st;
  RecStep out = st;
  out.state.queued = 0;
  out.effects = out.effects.with(fx(RecFx::FlushBusy));
  return out;
}

RecStep start(const RecStep& st, const RecCtx& c) {
  RecStep out = st;
  out.state = enter(st.state, RecPhase::Cue, c.now);
  out.state.gen = static_cast<uint8_t>(st.state.gen == 255 ? 1 : st.state.gen + 1);  // never 0
  out.state.started_ms = 0;  // until Started
  out.state.seg = 0;
  out.state.stars = 0;
  out.state.seg_stars = 0;
  out.state.level = 0;
  out.state.stop_why = RecRefusal::None;
  out.state.restopped = false;
  out.state.held = false;
  out.state.bat_low = false;
  out.state.up = false;  // gh #51: G0 is down (the hold that started it, or Opt+R)
  out.state.up_ms = 0;
  out.state.latched = false;
  RecEffect e = fx(RecFx::Start);
  e.mode = out.state.mode;
  e.slot = out.state.slot;
  e.consent = out.state.consent;
  e.gen = out.state.gen;
  out.effects = out.effects.with(e);
  if (c.free_known && c.free_bytes < kFreeWarnBytes) {
    RecEffect w = fx(RecFx::Warn);
    const uint32_t hours = static_cast<uint32_t>(c.free_bytes / kBytesPerHour);
    w.hours = hours > 0 ? hours : 1;  // "剩约 N 小时": never 0
    out.effects = out.effects.with(w);
  }
  return out;
}

RecStep stop(const RecCoreState& s, uint32_t now) {
  RecCoreState out = enter(s, RecPhase::Finishing, now);
  out.stopped_ms = now;
  out.restopped = false;
  return RecStep{out, Effects<RecEffect, 6>{}.with(fx(RecFx::Stop))};
}

// It stops by itself (docs/20 U6): the reason goes with it to the end.
RecStep stop_for(const RecCoreState& s, RecRefusal why, uint32_t now) {
  RecStep out = stop(s, now);
  out.state.stop_why = why;
  return out;
}

RecStep tag(const RecCoreState& s, uint8_t slot) {
  RecCoreState out = s;
  out.slot = slot <= kSlotMax ? slot : 0;
  RecEffect e = fx(RecFx::Tag);
  e.slot = out.slot;
  return RecStep{out, Effects<RecEffect, 6>{}.with(e)};
}

RecStep star(const RecCoreState& s, uint32_t now) {
  if (s.seg_stars >= limits::kFileMarksMax) return keep(s);
  RecCoreState out = s;
  out.stars = static_cast<uint8_t>(s.stars < 255 ? s.stars + 1 : 255);
  out.seg_stars = static_cast<uint8_t>(s.seg_stars + 1);
  out.star_ms = now;
  return RecStep{out, Effects<RecEffect, 6>{}.with(fx(RecFx::Mark))};
}

bool guarded(const RecCoreState& s, uint32_t now) { return now - s.opened_ms < kRecGuardMs; }

// docs/20 D3: an Enter inside the guard is said, never dropped silently.
RecStep too_fast(const RecCoreState& s) { return RecStep{s, Effects<RecEffect, 6>{}.with(fx(RecFx::TooFast))}; }

// Cancel is never guarded: it only keeps a recording from starting.
RecStep confirm_input(const RecCoreState& s, RecInput in, const RecCtx& c) {
  if (in == RecInput::Cancel) return keep(enter(s, RecPhase::Idle, c.now));
  if (in != RecInput::Confirm) return keep(s);
  if (guarded(s, c.now)) return too_fast(s);
  const RecRefusal why = rec_refusal(c);
  if (why != RecRefusal::None) return refuse(enter(s, RecPhase::Idle, c.now), why);
  RecCoreState next = s;
  next.consent = true;  // the user confirmed that the people present were told
  return start(leave_idle(RecStep{next, {}}), c);  // the box was not live (docs/20 D3)
}

// Cancel is never guarded: the meeting goes on.
RecStep end_confirm_input(const RecCoreState& s, RecInput in, const RecCtx& c) {
  if (in == RecInput::Cancel) return keep(enter(s, RecPhase::Recording, c.now));
  if (in != RecInput::Confirm) return keep(s);
  return guarded(s, c.now) ? too_fast(s) : stop(s, c.now);
}

// gh #51: a memo follows G0. Let go, the grace runs (rec_tick ends it after kMemoGraceMs);
// pressed again inside it, the same memo goes on. Hands-free (Latch, the double tap), letting
// go ends nothing and the next press ends it, on its down edge; Hold (the latching press
// held on to a hold) makes it a held memo after all.
RecStep memo_input(const RecCoreState& s, RecInput in, const RecCtx& c) {
  RecCoreState out = s;
  switch (in) {
    case RecInput::Release:
      if (s.up) return keep(s);  // the first release's grace stands
      out.up = true;
      out.up_ms = c.now;
      return keep(out);
    case RecInput::Press:
      if (s.latched) return stop(s, c.now);
      out.up = false;
      return keep(out);
    case RecInput::Latch: out.latched = true; return keep(out);
    case RecInput::Hold: out.latched = false; return keep(out);
    default: return keep(s);
  }
}

RecStep page_input(const RecCoreState& s, RecInput in, uint8_t arg, const RecCtx& c) {
  if (in == RecInput::Tag) return tag(s, arg);
  if (s.mode == RecMode::Memo) return memo_input(s, in, c);
  if (s.phase != RecPhase::Recording) return keep(s);  // a meeting's cue: tags only
  if (in == RecInput::Star) return star(s, c.now);
  if (in == RecInput::End) return keep(enter(s, RecPhase::EndConfirm, c.now));
  return keep(s);
}

RecRefusal fail_why(RecFail f) {
  switch (f) {
    case RecFail::Full: return RecRefusal::CardFull;
    case RecFail::NoMemory: return RecRefusal::NoMemory;
    default: return RecRefusal::CardError;  // the card failed (EIO, 09 §3.8)
  }
}

// Saved or Discarded: how long, and why it stopped. An end that arrives before any Stop
// went out (still Cue / Recording / EndConfirm) means the audio task stopped at the cap.
RecEffect end_effect(const RecCoreState& s, const RecEvt& e, uint32_t now) {
  RecEffect r = fx(RecFx::Ended);
  const bool by_task = s.phase != RecPhase::Finishing;
  r.why = by_task && s.stop_why == RecRefusal::None ? RecRefusal::Cap : s.stop_why;
  r.mode = s.mode;
  r.saved = e.kind == EvtKind::Saved;
  const uint32_t until = by_task ? now : s.stopped_ms;
  r.ms = s.started_ms != 0 ? until - s.started_ms : 0;
  return r;
}

// docs/20 S1: an event of an earlier recording (0: a port that does not tag).
bool stale(const RecCoreState& s, const RecEvt& e) { return e.gen != 0 && e.gen != s.gen; }

// How it ended. A full card after Started is an automatic stop that kept the segment so
// far (rec_engine), not a refusal (docs/20 U6); any other failure is one.
RecEffect end_of(const RecCoreState& s, const RecEvt& e, uint32_t now) {
  const bool full = e.kind == EvtKind::Failed && e.fail == RecFail::Full && s.started_ms != 0;
  if (e.kind != EvtKind::Failed || full) {
    RecEffect r = end_effect(s, e, now);
    r.why = full ? RecRefusal::CardFull : r.why;
    r.saved = r.saved || full;
    return r;
  }
  RecEffect r = fx(RecFx::Refuse);
  r.why = fail_why(e.fail);
  return r;
}

RecStep ended(const RecCoreState& s, const RecEvt& e, const RecCtx& c) {
  if (s.phase == RecPhase::Idle || stale(s, e)) return keep(s);
  RecCoreState out = enter(s, RecPhase::Idle, c.now);
  out.consent = false;
  // docs/20 U15: a memo that ends while G0 is still held stays on screen until the release
  out.held = rec_memo_page(s) && !s.up;
  return RecStep{out, Effects<RecEffect, 6>{}.with(end_of(s, e, c.now))};
}

// Started: the Cue of this recording becomes Recording. Idle, nothing is on screen while
// the audio task records: it is stopped (docs/20 S1). Any other phase waits for its end.
RecStep started(const RecCoreState& s, const RecCtx& c) {
  if (s.phase == RecPhase::Idle) return RecStep{s, Effects<RecEffect, 6>{}.with(fx(RecFx::Stop))};
  if (s.phase != RecPhase::Cue) return keep(s);
  RecCoreState out = enter(s, RecPhase::Recording, c.now);
  out.started_ms = c.now;
  return keep(out);
}

// docs/20 S1: the end event never came. The Stop goes out once more, then the core gives
// up: back to Idle, so the device is usable again; written segments stay on the card.
RecStep finishing_tick(const RecCoreState& s, const RecCtx& c) {
  const uint32_t waited = c.now - s.opened_ms;
  if (waited >= kFinishGiveUpMs) {
    RecCoreState out = enter(s, RecPhase::Idle, c.now);
    out.consent = false;
    out.held = false;
    RecEffect r = fx(RecFx::Refuse);
    r.why = RecRefusal::Timeout;
    return RecStep{out, Effects<RecEffect, 6>{}.with(r)};
  }
  if (waited < kFinishRetryMs || s.restopped) return keep(s);
  RecCoreState out = s;
  out.restopped = true;
  return RecStep{out, Effects<RecEffect, 6>{}.with(fx(RecFx::Stop))};
}

// docs/11 §4.3: at or below 3.55 V a warning once; at or below 3.45 V for 10 s, the stop.
RecStep battery_tick(const RecCoreState& s, const RecCtx& c) {
  RecStep st{s, {}};
  if (c.bat_mv != 0 && c.bat_mv <= kBatWarnMv && !s.bat_low) {
    st.state.bat_low = true;
    st.effects = st.effects.with(fx(RecFx::BatWarn));
  }
  const bool low = c.bat_mv != 0 && c.bat_mv <= kBatStopMv;
  if (!low) {
    st.state.low = false;
    return st;
  }
  if (!s.low) {
    st.state.low = true;
    st.state.low_since = c.now;
    return st;
  }
  if (c.now - s.low_since < kBatStopForMs) return st;
  RecStep out = stop_for(st.state, RecRefusal::Battery, c.now);
  for (uint8_t i = 0; i < st.effects.count; ++i) out.effects = out.effects.with(st.effects.items[i]);
  return out;
}

}  // namespace

uint32_t rec_cap_ms(RecMode m) { return m == RecMode::Memo ? limits::kMemoMaxMs : limits::kMeetingMaxMs; }

RecRefusal rec_refusal(const RecCtx& c) {
  if (!c.avail) return c.no_memory ? RecRefusal::NoMemory : RecRefusal::NoSd;
  if (!c.ids_ok) return RecRefusal::NoIds;
  if (c.free_known && c.free_bytes < kFreeRefuseBytes) return RecRefusal::CardFull;
  if (c.files_n >= limits::kFilePendingMax) return RecRefusal::Pending;
  if (c.incomplete) return RecRefusal::Incomplete;
  if (c.bat_mv != 0 && c.bat_mv <= kBatStopMv) return RecRefusal::Battery;
  return RecRefusal::None;
}

RecStep rec_gesture(const RecCoreState& s, RecMode mode, const RecCtx& c) {
  if (s.phase != RecPhase::Idle) return keep(s);
  const RecRefusal why = rec_refusal(c);
  if (why != RecRefusal::None) return refuse(s, why);
  RecCoreState next = s;
  next.mode = mode;
  next.slot = c.slot;
  next.consent = false;
  next.low = false;
  if (mode == RecMode::Meeting) return keep(enter(next, RecPhase::Confirm, c.now));  // not live (docs/20 D3)
  return start(leave_idle(RecStep{next, {}}), c);
}

RecStep rec_input(const RecCoreState& s, RecInput in, uint8_t arg, const RecCtx& c) {
  if (in == RecInput::Touch) return touch(s, c.now);
  switch (s.phase) {
    case RecPhase::Confirm: return confirm_input(s, in, c);
    case RecPhase::EndConfirm: return end_confirm_input(s, in, c);
    case RecPhase::Cue:
    case RecPhase::Recording: return page_input(s, in, arg, c);
    case RecPhase::Idle: {
      RecCoreState out = s;
      out.held = s.held && in != RecInput::Release;  // the memo cap screen goes with G0
      return keep(out);
    }
    default: return keep(s);
  }
}

RecStep rec_event(const RecCoreState& s, const RecEvt& e, const RecCtx& c) {
  RecCoreState out = s;
  switch (e.kind) {
    case EvtKind::IndexReady: out.ready = true; return keep(out);
    case EvtKind::Started: return s.phase != RecPhase::Idle && stale(s, e) ? keep(s) : started(s, c);
    case EvtKind::Segment:
      out.seg = e.seg;
      out.seg_stars = 0;
      return keep(out);
    case EvtKind::Level: out.level = e.level; return keep(out);
    case EvtKind::Saved:
    case EvtKind::Discarded:
    case EvtKind::Failed: return ended(s, e, c);
    default: return keep(s);
  }
}

RecStep rec_tick(const RecCoreState& s, const RecCtx& c) {
  if (s.phase == RecPhase::Finishing) return finishing_tick(s, c);
  const uint32_t idle = c.now - s.box_ms;  // docs/20 D3: since the box's last key
  if (s.phase == RecPhase::Confirm) return idle >= kStartBoxMs ? keep(enter(s, RecPhase::Idle, c.now)) : keep(s);
  const bool running = s.phase == RecPhase::Recording || s.phase == RecPhase::EndConfirm;
  if (running && c.now - s.started_ms >= rec_cap_ms(s.mode) + kCapGraceMs) return stop_for(s, RecRefusal::Cap, c.now);
  if (rec_grace(s) && c.now - s.up_ms >= kMemoGraceMs) return stop(s, c.now);  // gh #51: G0 stayed up
  if (!running) return keep(s);
  if (s.phase == RecPhase::EndConfirm && idle >= kEndBoxMs) return keep(enter(s, RecPhase::Recording, c.now));
  return battery_tick(s, c);
}

RecStep rec_stop_for(const RecCoreState& s, RecRefusal why, const RecCtx& c) {
  const bool live = s.phase == RecPhase::Cue || s.phase == RecPhase::Recording || s.phase == RecPhase::EndConfirm;
  return live ? stop_for(s, why, c.now) : keep(s);
}

RecStep rec_file_queued(const RecCoreState& s) {
  RecCoreState out = s;
  if (s.at_task) {
    out.queued = static_cast<uint8_t>(s.queued + 1);
    return keep(out);
  }
  out.at_task = true;
  return RecStep{out, Effects<RecEffect, 6>{}.with(fx(RecFx::PostNext))};
}

RecStep rec_file_done(const RecCoreState& s) {
  RecCoreState out = s;
  out.at_task = false;
  if (s.queued == 0 || rec_live(s)) return keep(out);
  out.queued = static_cast<uint8_t>(s.queued - 1);
  out.at_task = true;
  return RecStep{out, Effects<RecEffect, 6>{}.with(fx(RecFx::PostNext))};
}

RecStep rec_files_dropped(const RecCoreState& s) {
  RecCoreState out = s;
  out.queued = 0;
  return keep(out);
}

}  // namespace rec
}  // namespace cpa
