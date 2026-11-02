// Confirm (05 §5.6, 04 US-09/US-10): decides "execute now" versus "save as queue" and
// guards against accidental presses: every key in the first 400 ms is ignored.
//   spawn:    Ready, agent ok and caps.spawn                    -> execute (agent.spawn)
//   feedback: kind bg, st idle, Ready and caps.reply             -> execute (agent.reply)
//   anything else                                                -> queue item, with a reason
//   (a waiting session is told apart from a running one, docs/20 U7)
// A spawn from an existing queue item (qid) is never re-queued: it is unavailable instead.
// A spawn from an uncertain queue item (an earlier Opt+S result never came back) says so first (docs/20 U25).
#pragma once

#include <cstdint>

#include "module.h"

namespace cpa {

constexpr uint32_t kConfirmGuardMs = 400;

enum class Route : uint8_t { Execute, Queue, Unavailable };

enum class QueueReason : uint8_t {
  None,
  Offline,         // link not Ready
  NoAgent,         // Claude Code missing on the Mac
  Interactive,     // tty session: replies come in v0.4 (09 §13)
  NotIdle,         // the session is running
  NoReply,         // the daemon has replies switched off
  SessionGone,     // the session is no longer listed
  Waiting,         // the session waits for an answer or a permission (docs/20 U7)
};

struct Decision {
  Route route = Route::Queue;
  QueueReason reason = QueueReason::None;
  bool maybe_open = false;  // docs/20 U25: spawning an uncertain queue item; the session may already run
};

Decision confirm_decide(const ConfirmSpec& spec, const AppState& s);
const char* queue_reason_text(QueueReason r);

// docs/20 U7: the words of the confirm box and its toasts, each one 240 px row at most.
using ConfirmText = FixedStr<96>;
ConfirmText confirm_title(ConfirmKind k, const Decision& d, const char* target);  // the first line
ConfirmText confirm_queued_text(ConfirmKind k, const char* why);  // stored as a queue item instead
ConfirmText confirm_unrun_text(QueueReason r);                    // a queue item that stayed put
const char* confirm_note(const Decision& d);  // "" or a warning row under the title (docs/20 U25)
// docs/20 U22, D6: the box's two keys, worded once for its card and its hint bar: Enter start,
// save as queued or close; ` edit when a draft is behind the box, else ` cancel.
const char* confirm_keys(const Decision& d, bool draft);

struct ConfirmState {
  ConfirmSpec spec;
  uint32_t opened_ms = 0;
  bool draft = false;  // opened from the editor: cancelling goes back to it (docs/20 D6)
};

enum class ConfirmResult : uint8_t { None, Ignored, Accepted, Cancelled };

ConfirmResult confirm_key(const ConfirmState& s, const KeyEvent& ev, uint32_t now);

// The queue item a non-executed confirm turns into (project key from the slot).
Opt<QueueBody> confirm_queue_body(const ConfirmSpec& spec, const AppState& s);

}  // namespace cpa
