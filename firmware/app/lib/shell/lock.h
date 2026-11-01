// Opt+L lock (docs/13 §7.1, v0.4.0), pure. Not a security lock: it keeps pocket presses
// from doing anything and the screen from showing content. Opt+L locks and the screen
// goes dark at once, even with a USB host (power.h). While locked the gate (docs/13
// §5.4 step 1, before power_key) lets through only:
//   Opt+L             lights the lock card and opens the 3 s unlock window
//   Enter             inside the window: unlocks, back to the page under the lock
//   G0 tap            lights the card for 3 s and arms it  (in a meeting: passes, ★)
//   G0 held, armed    passes: a memo; the shell darkens the card once it started, so
//                     the device is dark and locked again after the memo
//   G0 held otherwise only lights the card; it and its release are swallowed, so a
//                     press in a pocket never records     (in a meeting: passes, ★)
//   the release of a G0 hold that passed (the memo's 0.6 s grace, gh #51; the shell takes
//                     a re-press, and a hands-free memo's presses: shell_g0.cpp)
// docs/20 U3: only a tap arms the card (a swallowed hold lights it but does not: two
// pocket holds never record); "armed" counts at the press, kG0LongMs before G0Long;
// the card stays lit while G0 is held (a swallowed hold, or one that went down on the
// lit card: lock_g0), kHoldLitMs past its time at most (a pocket may press G0 for minutes).
// The card that answered a swallowed hold says so (没有在录) until it goes dark or another
// key lights it, not only while G0 is down. docs/20 D7: locking lights the card for the user (lock_engage's
// lit_ms); lock_flash lights it by itself for a while, without arming it or opening the
// window (a recording's end, a session needing you). A card lit by itself may light an
// empty pocket: it is no look (flash), so a result waits for a key (docs/20 U6).
// Every other key is swallowed and lights nothing. A key the gate passes never changes
// the state, so dispatching it again is harmless.
#pragma once

#include <cstdint>

#include "chord_detector.h"

namespace cpa {

constexpr uint32_t kUnlockWindowMs = 3000;
constexpr uint32_t kLockCardMs = 3000;
constexpr uint32_t kLockFlashMs = 2000;   // docs/20 D7: locking, a recording's end
constexpr uint32_t kNeedsFlashMs = 3000;  // ... the count of sessions needing you rose
constexpr uint32_t kHoldLitMs = 10000;    // a held G0 keeps the card lit this long past its time

struct LockState {
  bool locked = false;
  bool window = false;  // Opt+L opened the unlock window ...
  uint32_t window_until_ms = 0;  // ... until then (exclusive)
  bool card = false;    // the lock card is lit ...
  uint32_t card_until_ms = 0;    // ... until then (exclusive)
  bool eat_up = false;  // a swallowed G0 hold: its release is swallowed too
  bool armed = false;   // docs/20 U3: a G0 tap armed the card for a memo ...
  uint32_t arm_until_ms = 0;     // ... for a press before then (exclusive)
  bool g0_held = false;  // G0 went down on the lit card and is still down: it stays lit
  bool flash = false;    // lit by lock_flash, by no key: nobody may be looking
  bool swallowed = false;  // the lit card answers a swallowed hold (没有在录), until dark or a key
};

enum class LockOut : uint8_t { Pass, Swallow, Unlock };

struct LockStep {
  LockState state;
  LockOut out = LockOut::Pass;
};

LockState lock_engage(uint32_t now = 0, uint32_t lit_ms = 0);  // locked, no window; the card lit lit_ms
// meeting: a meeting is being recorded (G0 always passes: ★).
LockStep lock_key(const LockState& s, const KeyEvent& ev, uint32_t now, bool meeting);
LockState lock_tick(const LockState& s, uint32_t now);  // the card and the window run out
LockState lock_dark(const LockState& s);                // the card off (a memo started)
bool lock_lit(const LockState& s, uint32_t now);         // locked with the card lit
LockState lock_flash(const LockState& s, uint32_t now, uint32_t ms);  // lit for ms (locked only)
LockState lock_g0(const LockState& s, bool down, uint32_t now);       // G0 down / up on the matrix
bool lock_arms(const LockState& s, uint32_t press_ms);   // a G0 hold pressed then records

}  // namespace cpa
