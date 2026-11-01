// RecCore (docs/11 §3, §4.1-§4.3, §4.7): the recording state machine of the loop, pure.
//
//   Idle --G0 held (memo)----------------------------------> Cue --Started--> Recording
//   Idle --Opt+R--> Confirm --Enter (consent "ack")--------> Cue                 |
//                   Confirm --Cancel, or kStartBoxMs--> Idle                     |
//   Recording (meeting) --Opt+R--> EndConfirm --Enter--> Finishing --Saved / Discarded
//                                  EndConfirm --Cancel, or kEndBoxMs--> Recording / Failed--> Idle
//   Cue / Recording (memo) --G0 up, no re-press within kMemoGraceMs--> Finishing (in the cue: discarded)
//   Cue / Recording (hands-free memo) --G0 down--> Finishing
//
// Cue: starting, the microphone is coming up. Recording makes no sound (v0.3.1): the
// screen is the only feedback, refusals and failures included.
// docs/20: Finishing gives up after kFinishGiveUpMs (S1: a lost end event, the Stop sent
// again at kFinishRetryMs); every end says its length and why it stopped by itself
// (Ended, U6; a full card after Started is such a stop, saved); a memo that ends while G0
// is held stays "held" until the release (U15). S1 also: every Start carries a new gen
// (1..255) that the audio task copies into the recording's Started and end events; an
// event of another gen is a late one of an earlier recording and is ignored (gen 0: a
// port that does not tag, taken as the current one), and a Started while Idle is
// answered with a Stop, as nothing on screen says the task records.
// gh #51 (docs/20 U26): a memo follows G0 (Press / Release, the shell's G0 edges). Let go,
// it records on through a grace of kMemoGraceMs; a re-press inside it goes on with the same
// recording, and only a grace that runs out ends it, its audio kept. A double tap starts a
// hands-free memo (Latch, right after its start): letting go then ends nothing, there is
// no grace, and the next G0 press ends it on its down edge; Hold (the latching press held
// on to a hold, g0_gesture.h) makes it a held memo after all. A meeting is never latched.
// A memo that ends by itself is "held" (U15) only while G0 is down.
// docs/20 D3 (U1): both boxes guard Enter for 400 ms, and an Enter inside the guard is
// answered with TooFast (the screen says to press it again), never dropped silently;
// Cancel is never guarded. A box nobody answers goes away: the start box after
// kStartBoxMs (nothing starts), the end box after kEndBoxMs (the meeting goes on), both
// counted from the box's last key (Touch: the runtime's every key, and while the Tab menu
// is open over it). The
// start box is not a recording: rec_live is false, file.* goes on until the consent
// Enter. Meeting page: a star (at most 16 per segment), a
// project tag (1..9, 0 none); every other key is ignored (pocket presses). Refusals of
// §4.3: no SD card, card nearly full (< 8 MB known free), 256 segments waiting, battery
// at or below 3.45 V, no buffers, no ids. Under 256 MB free it starts with a warning.
//
// file.* (§4.7): RecCore counts the requests the runtime holds (queued) and the one the
// audio task works on (at_task). Starting a recording answers every queued one with E_BUSY
// "live" (FlushBusy) before Start is posted; while live the runtime answers new ones with
// E_BUSY "live" itself (rec_live). PostNext hands the oldest queued request to the task.
#pragma once

#include <cstdint>

#include "app_state.h"
#include "rec_msgs.h"
#include "step.h"

namespace cpa {
namespace rec {

constexpr uint32_t kRecGuardMs = 400;               // as the confirm box (confirm.h)
constexpr uint16_t kBatStopMv = 3450;               // docs/11 §4.3, G-A7 calibrates it
constexpr uint32_t kBatStopForMs = 10000;
constexpr uint64_t kFreeRefuseBytes = 8ull << 20;   // < 8 MB: refuse
constexpr uint64_t kFreeWarnBytes = 256ull << 20;   // < 256 MB: warn
constexpr uint64_t kBytesPerHour = 115200000ull;    // 32,000 B/s
constexpr uint32_t kCapGraceMs = 2000;              // the audio task stops at the cap itself
constexpr uint16_t kBatWarnMv = 3550;               // docs/11 §4.3: a warning on the screen only
constexpr uint32_t kFinishRetryMs = 5000;           // docs/20 S1: Finishing this long: Stop again
constexpr uint32_t kFinishGiveUpMs = 10000;         // ... this long: Idle, the end event is lost
constexpr uint32_t kStartBoxMs = 15000;            // docs/20 D3: the start box gives up
constexpr uint32_t kEndBoxMs = 10000;              // ... the end box goes back to the meeting
constexpr uint32_t kMemoGraceMs = 600;              // gh #51: G0 let go this long ends a memo

// Why a recording does not start, or stopped (CardError: EIO during recording, 09 §3.8).
// docs/20 U6, S1: also why a recording stopped by itself (Cap: the length cap) or gave up
// waiting for its end (Timeout).
// docs/28 D15: Incomplete, the boot scan found more names than its table (the index may miss
// some waiting segments): no recording starts until they are synced and the card rescanned.
enum class RecRefusal : uint8_t { None, NoSd, CardFull, Pending, Battery, NoMemory, NoIds, CardError, Cap, Timeout, Incomplete };

// Press / Release: G0 went down / up (a memo follows them, gh #51); Latch: the memo just
// started is hands-free (the double tap); Hold: its latching press became a hold. Touch: a
// key on a meeting box (docs/20 D3).
enum class RecInput : uint8_t { Confirm, Cancel, Star, Tag, End, Release, Touch, Press, Latch, Hold };

// Ended: a started recording ended (saved or discarded), with its length and the reason
// it stopped by itself (None: the user stopped it). BatWarn: at or below kBatWarnMv.
// TooFast: Enter inside a box's 400 ms guard (docs/20 D3).
enum class RecFx : uint8_t { Refuse, Warn, Start, Stop, Mark, Tag, FlushBusy, PostNext, Ended, BatWarn, TooFast };

struct RecEffect {
  RecFx kind = RecFx::Refuse;
  RecRefusal why = RecRefusal::None;
  RecMode mode = RecMode::Memo;
  uint8_t slot = 0;
  bool consent = false;
  uint32_t hours = 0;  // Warn: about how long the free space lasts
  uint32_t ms = 0;     // Ended: from the first sample to the stop
  bool saved = false;  // Ended: saved (else discarded: too short)
  uint8_t gen = 0;     // Start: the recording's generation
};

struct RecCtx {
  uint32_t now = 0;
  bool avail = false;    // the SD card is the primary store and the buffers exist
  bool no_memory = false;  // the SD card is primary but the buffers could not be allocated
  bool ids_ok = true;    // MonoIds can be issued
  uint16_t files_n = 0;  // segments waiting for sync
  bool free_known = false;
  uint64_t free_bytes = 0;
  uint16_t bat_mv = 0;   // 0 = unknown
  uint8_t slot = 0;      // the project the user is looking at
  bool incomplete = false;  // docs/28 D15: the boot scan's index is incomplete
};

struct RecCoreState {
  RecPhase phase = RecPhase::Idle;
  RecMode mode = RecMode::Memo;
  uint32_t opened_ms = 0;   // phase entered (the confirms' guard)
  uint32_t box_ms = 0;      // a box's last key, or its opening: its time runs from here (D3)
  uint32_t started_ms = 0;  // Started: the first sample
  uint8_t slot = 0;
  bool consent = false;
  uint16_t seg = 0;
  uint8_t stars = 0;      // this session
  uint8_t seg_stars = 0;  // this segment (<= 16)
  uint32_t star_ms = 0;   // the last star: the screen flashes
  uint16_t level = 0;
  uint32_t low_since = 0;
  bool low = false;
  bool ready = false;     // IndexReady seen
  uint8_t queued = 0;     // file.* held by the runtime
  bool at_task = false;   // one file.* with the audio task
  // docs/20 (A9, A12)
  RecRefusal stop_why = RecRefusal::None;  // why it stopped by itself (None: the user)
  uint32_t stopped_ms = 0;  // the Stop went out (the length of the recording)
  bool restopped = false;   // Finishing: the Stop went out again
  bool held = false;        // a memo ended while G0 is still held: shown until the release
  bool bat_low = false;     // at or below kBatWarnMv, warned once
  uint8_t gen = 0;          // the current recording's generation (never 0 once started)
  // gh #51 (docs/20 U26): a memo and its G0
  bool up = false;          // G0 is up: the grace runs since up_ms (latched: nothing runs)
  uint32_t up_ms = 0;
  bool latched = false;     // hands-free (the double tap): letting go ends nothing, a press ends it
};

using RecStep = Step<RecCoreState, RecEffect, 6>;

RecStep rec_gesture(const RecCoreState& s, RecMode mode, const RecCtx& c);
RecStep rec_input(const RecCoreState& s, RecInput in, uint8_t arg, const RecCtx& c);
RecStep rec_event(const RecCoreState& s, const RecEvt& e, const RecCtx& c);
RecStep rec_tick(const RecCoreState& s, const RecCtx& c);
// The runtime stops the recording for a reason of its own (Pending: the index is full).
RecStep rec_stop_for(const RecCoreState& s, RecRefusal why, const RecCtx& c);
RecStep rec_file_queued(const RecCoreState& s);  // the runtime queued one more request
RecStep rec_file_done(const RecCoreState& s);    // the task's request was answered
RecStep rec_files_dropped(const RecCoreState& s);  // the link was lost: the queued ones are void

// docs/20 D3: the start box is not live (the screen may sleep, the Mac may pull).
inline bool rec_live(const RecCoreState& s) { return s.phase != RecPhase::Idle && s.phase != RecPhase::Confirm; }
inline bool rec_box(RecPhase p) { return p == RecPhase::Confirm || p == RecPhase::EndConfirm; }  // D3
// A memo being recorded (its cue included): the page that follows G0 (gh #51).
inline bool rec_memo_page(const RecCoreState& s) {
  return s.mode == RecMode::Memo && (s.phase == RecPhase::Cue || s.phase == RecPhase::Recording);
}
inline bool rec_grace(const RecCoreState& s) { return rec_memo_page(s) && s.up && !s.latched; }
inline bool rec_latched(const RecCoreState& s) { return rec_memo_page(s) && s.latched; }
inline uint8_t rec_inflight(const RecCoreState& s) { return static_cast<uint8_t>(s.queued + (s.at_task ? 1 : 0)); }
RecRefusal rec_refusal(const RecCtx& c);
uint32_t rec_cap_ms(RecMode m);

}  // namespace rec
}  // namespace cpa
