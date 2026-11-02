// The locked screen (docs/13 §7.1-§7.2): black; the lock card while it is lit; and while
// recording a red ● line top-left: "●42:10 ★2" for a meeting, "● REC 0:12" for a memo,
// and a yellow "● Wait" before the microphone is up (the ● rule). The card shows counts
// only, never a project or session name or any text: the time, the date, the battery,
// Waiting / R / ↑, and how to unlock.
// v0.5 (docs/19 §6.8, mockup 06): a rounded Sheet card with a Rim top edge, Mando dozing on
// it, the large clock in Nunito, the counts as chips, a still corner glow behind it, and
// how to unlock and record as keycaps below it. The ● line keeps its words and sits in the
// status bar's REC slot, a RecBg capsule at the top left (WarnBg for ● Wait), the glow kept
// 2 px clear of it.
// docs/20 B1: in the clock's place "Not recording" (bold) while the lock swallows a G0 hold
// (and after it, until the card goes dark or a key lights it), or how the last recording
// ended ("√ Saved 0:12" and why it stopped by itself), Mando awake there; the chips
// Waiting N (none without the Mac's picture; from one kept since the Mac went away, not
// mustard and the head says "Locked · offline": U19), RN, ↑N (none at zero) or "√ synced";
// the battery coloured when low; "● Wait" from the press of a G0 on an armed card (D8).
#pragma once

#include <cstdint>

#include "app_state.h"
#include "canvas.h"
#include "fixed_str.h"
#include "theme_mando.h"

namespace cpa {

// z: how many of Mando's three z's are lit (v0.5 S3: a tick lights one every 600 ms).
void render_lock_screen(Canvas& c, const AppState& s, bool card, uint32_t now, uint8_t z = 3);
// The lit card's z's as a tick leaves them: the cells under them drawn again (the glow, the
// card's top edge, the card, Mando), then the lit ones. Returns the rectangle to push.
Rect lock_z_part(Canvas& c, const AppState& s, uint8_t z, uint32_t now);  // now: the ● line, clear of the glow

// What Mando does on the lit card (docs/20 B1): dozes, unless a G0 hold is being swallowed
// (NoVoice: not recording) or the card shows how the last recording ended (Hop: saved,
// NoVoice: not): one that ended while locked, or one not seen yet (D7: locking after a
// result seen unlocked shows the card as ever). Only a dozing Mando moves (his z's).
theme::MandoAct lock_mando_act(const AppState& s);
// v0.5.0 (mando_eggs.h): what he does on the clock card (lock_mando_act Doze): a pat's hop
// (Opt+W), else the clock's mood (the early bird is up); otherwise lock_mando_act.
theme::MandoAct lock_card_act(const AppState& s, uint32_t now);

FixedStr<16> lock_date_text(const TimeInfo& t, uint32_t now);  // "10/21 Wed"; "" while unknown
FixedStr<8> weekday_text(const TimeInfo& t, uint32_t now);      // "Wed"; "" while unknown

}  // namespace cpa
