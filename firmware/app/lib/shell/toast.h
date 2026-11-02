// Toasts (05 §5.6): one shown at a time, at most 3 waiting; a new toast when 3 are
// queued drops the oldest. docs/20 U20: three kinds.
//   Result  the user's own action (saved, sent, refused): shown before every notice,
//           which it pushes out (the status bar's !N keeps that count); in order among
//           results, so a later "saved" never hides an earlier error;
//   Notice  from the Mac ("X needs you"): first in, first out, behind the results;
//   Hint    a refused key's hint (docs/20 U17): as a result, never the same text twice
//           within kHintRepeatMs, and retracted once its condition is gone.
// A toast shows for 2.5 s, an error for 4.5 s. While the screen is not seen (locked or
// dark) the shown one waits (toast_hold) instead of running out unseen (docs/20 U6), and
// every toast is marked held. Seen again (toast_seen), the held ones are cut to the
// errors and the latest other result, and a result or hint pushed while seen goes ahead
// of what was held: what piled up in the pocket never delays what the user does next.
#pragma once

#include <cstdint>

#include "module.h"

namespace cpa {

constexpr uint32_t kToastMs = 2500;
constexpr uint32_t kToastErrorMs = 4500;
constexpr uint32_t kHintRepeatMs = 3000;
constexpr uint8_t kToastMax = 3;

using ToastText = FixedStr<96>;  // cut on a UTF-8 boundary

enum class ToastKind : uint8_t { Result, Notice, Hint };

struct Toast {
  ToastLevel level = ToastLevel::Info;
  ToastText text;
  uint32_t shown_ms = 0;  // when it became the visible one
  ToastKind kind = ToastKind::Notice;
  uint32_t at_ms = 0;     // when it was pushed
  bool held = false;      // queued while the screen was not seen
};

struct ToastQueue {
  uint8_t n = 0;
  Toast items[kToastMax];
  ToastText last_hint;  // the last hint pushed, and when
  uint32_t last_hint_ms = 0;
  bool unseen = false;  // held now (toast_hold): new toasts keep their order
};

ToastQueue toast_push(const ToastQueue& q, ToastLevel level, const char* utf8, uint32_t now);    // Notice
ToastQueue toast_result(const ToastQueue& q, ToastLevel level, const char* utf8, uint32_t now);  // Result
ToastQueue toast_hint(const ToastQueue& q, ToastLevel level, const char* utf8, uint32_t now);    // Hint
ToastQueue toast_retract(const ToastQueue& q, uint32_t now);  // every Hint goes
ToastQueue toast_hold(const ToastQueue& q, uint32_t now);     // the shown one starts again at now
ToastQueue toast_seen(const ToastQueue& q, uint32_t now);     // seen again: the held ones cut down
ToastQueue toast_tick(const ToastQueue& q, uint32_t now);
const Toast* toast_current(const ToastQueue& q);

}  // namespace cpa
