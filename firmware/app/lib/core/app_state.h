// AppState (05 §5.3): the whole UI-visible state, <= 4 KB, held in two static
// buffers by the shell. Each slice is a plain value replaced as a whole by its
// reducer; the old state is never modified in place.
#pragma once

#include <cstdint>

#include "agent_req.h"
#include "domain.h"
#include "err_code.h"
#include "mono_id.h"

namespace cpa {

struct LinkInfo {
  LinkMode mode = LinkMode::Detached;
  AgentStatus agent = AgentStatus::Missing;
  bool cap_spawn = false;
  bool cap_reply = false;
  bool version_mismatch = false;  // host spoke another major version (04 US-04 AC3)
  uint32_t last_rx_ms = 0;
};

struct TimeInfo {
  bool known = false;
  int64_t offset_ms = 0;  // wall clock = up_ms + offset_ms
  int16_t tz_min = 0;
};

// live = received from the daemon since boot; false = names from /cfg cache only.
struct ProjectsView {
  bool live = false;
  uint32_t rx_ms = 0;
  ProjectsData data;
};

struct SessionsView {
  bool live = false;
  uint32_t rx_ms = 0;
  SessionsData data;
};

struct QueueView {
  bool live = false;
  uint32_t rx_ms = 0;
  QueueData data;
};

struct OutboxInfo {
  uint16_t pending = 0;  // includes in-flight
  uint16_t inflight = 0;
  uint16_t bad = 0;
  bool full = false;
};

struct DeviceInfo {
  BootIdStr boot;
  uint16_t bat_mv = 0;
  uint32_t heap_free = 0;
  uint32_t heap_min = 0;
  uint32_t heap_max_block = 0;
  uint32_t stack_hwm = 0;
  uint16_t ghost_n = 0;
  uint16_t kb_reinit_n = 0;
  uint32_t kb_warn_ms = 0;  // docs/20 U21: when ghost_n + kb_reinit_n last rose (※k shows 10 s)
};

// v0.3 recording (docs/11 §3-§4): what the screen shows, mirrored from the runtime's
// RecCore after every step. Cue: starting, the microphone is coming up (silent, v0.3.1).
enum class RecPhase : uint8_t { Idle, Confirm, Cue, Recording, EndConfirm, Finishing };

// docs/20 U6: how the last recording ended, or why one did not start, kept until the
// screen has been seen lit once (the lock card's result row, B1). why is a
// rec::RecRefusal (0: the user stopped it).
struct RecLast {
  bool has = false;
  bool saved = false;
  uint8_t why = 0;
  RecMode mode = RecMode::Memo;
  uint32_t ms = 0;     // its length
  bool seen = false;   // the screen was lit since: cleared once it is dark again
  bool locked = false;  // B1: it ended (or was refused) while locked: the lit lock card shows it
};

// docs/28 S1a: the kept copies (handed to a Mac, kept a while) and the plug-in hint, as the
// lock card, the status and System -> storage show them. batch: what was handed to one Mac
// last (its host hash; 0: nothing to deliver again). The hint: Shown for 5 s after a hello
// while segments recorded offline wait (they are held back meanwhile); Tab -> Asking;
// Enter -> Busy until the ledger has it.
enum class PlugHint : uint8_t { None, Shown, Asking, Busy };

struct KeptView {
  uint16_t segs = 0;
  uint16_t batch_recs = 0;
  uint32_t oldest_s = 0;    // wall clock of the oldest kept copy (0: unknown)
  uint32_t batch_host = 0;
  bool busy = false;        // a ledger command is with the audio task
  bool incomplete = false;  // D15: the boot scan found more names than it holds
  PlugHint plug = PlugHint::None;
  uint8_t plug_n = 0;       // recordings recorded offline
  uint16_t held_here = 0;   // waiting, but kept from the Mac on the link (give to this Mac)
};

struct RecInfo {
  bool avail = false;  // recording possible this boot (the SD card is the primary store)
  bool ready = false;  // the boot recovery scan is done
  RecPhase phase = RecPhase::Idle;
  RecMode mode = RecMode::Memo;
  uint32_t opened_ms = 0;   // the current phase began (the confirms' 400 ms guard)
  uint32_t started_ms = 0;  // first sample: elapsed = now - started_ms
  uint16_t seg = 0;
  uint8_t stars = 0;
  uint8_t slot = 0;         // project tag, 0 = none
  uint16_t level = 0;       // RMS of the last block
  uint32_t star_ms = 0;     // the last star (the page flashes)
  uint16_t files_n = 0;     // committed segments waiting for sync
  uint32_t files_rev = 0;
  uint32_t free_mib = 0;
  bool free_known = false;  // free_mib is known (beside the bools below: no padding)
  bool held = false;     // docs/20 U15: a memo ended while G0 is held; the screen waits for it
  bool bat_low = false;  // docs/11 §4.3: at or below 3.55 V while recording
  bool sd_bad = false;   // docs/20 U16: the device records, but has no usable SD card (※SD)
  bool grace = false;    // gh #51: a memo's G0 was let go; a re-press within the grace goes on
  bool latched = false;  // gh #51: a hands-free memo (G0 twice): one G0 press ends it
  RecLast last;
  KeptView kept;  // docs/28 S1a
};

struct UiContext {
  uint8_t slot = 0;  // current project slot (0 = none): default Compose target
};

// v0.5 (docs/19 §7.2): what just happened that Mando reacts to on home: a recording was
// saved (he hops) or was too short and dropped (he heard nothing). moment_ms is when.
// v0.5.0 (docs/04, hidden keys): AllClear, the sessions needing you went from some to none
// while home was in front: he hops once more, and says so.
enum class Moment : uint8_t { None, Saved, NoVoice, AllClear };
constexpr uint32_t kHopMs = 800;       // Saved: his hop on home
constexpr uint32_t kNoVoiceMs = 2500;  // NoVoice: his "?" on home

// v0.5.0 (docs/04, hidden keys): Opt+W pats Mando on home and the lit lock card. RAM only:
// how many pats since boot, when the last one was, and which of his lines it drew.
struct PatState {
  uint32_t n = 0;
  uint32_t at_ms = 0;
  uint8_t line = 0;
};

// v0.4.0 shell overlays (docs/13 §10.1, §12.1), reported as debug.state.ui.
struct UiFlags {
  bool menu = false;    // the Tab menu is open (overlay still reports the layer under it)
  bool locked = false;  // Opt+L lock is on
  // docs/20 (not in debug.state): G0 is down and a hold would record a memo, "● Wait" from
  // the press (D8); locked, a G0 hold is being swallowed: "Not recording" (U3, B1), and
  // after its release until the card goes dark or a key lights it
  bool memo_ready = false;
  bool lock_held = false;
  Moment moment = Moment::None;  // v0.5, not in debug.state
  uint32_t moment_ms = 0;
  PatState pat = {};  // the shell's (Opt+W), not in debug.state
};

struct AppState {
  LinkInfo link;
  TimeInfo time;
  ProjectsView projects;
  SessionsView sessions;
  QueueView queue;
  OutboxInfo outbox;
  AgentReqInfo agent;
  DeviceInfo device;
  UiContext ctx;
  UiFlags ui;
  RecInfo rec;
};

static_assert(sizeof(AppState) <= 4096, "AppState must stay within 4 KB (05 §5.13)");

}  // namespace cpa
