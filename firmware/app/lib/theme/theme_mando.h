// Mando (docs/19 §7): WhisperWoof's dog keeps the user company on home, the recording pages
// and the lock card. An act is what he is doing; each shows key poses of his sprite loops
// (4 x 200 ms when animated, the rest pose when still), a one-word sign under him and, when
// he needs you or heard nothing, a bubble over his head. Drawn on any Canvas, so apps
// (docs/16) can use him too. Pure.
#pragma once

#include <cstdint>

#include "canvas.h"
#include "theme.h"
#include "theme_art.h"

namespace cpa {
namespace theme {

enum class MandoSize : uint8_t { M46, M64 };  // 26 x 46, 36 x 64
enum class MandoAct : uint8_t {
  Idle,      // "Here": sits (wait 0)
  Listen,    // "Listen": nods while you talk (review)
  Tilt,      // "Listen": tilts his head in a quiet moment (wait)
  Sniff,     // "Sniff…": syncing or saving, paw on chin (think)
  NeedsYou,  // "You!": sits (wait 0) with "!"
  Hop,       // "Saved": one hop (46 px only)
  NoVoice,   // "Huh?": head tilted with "?"
  Doze,      // "Napping": eyes closed (46 px only)
  Offline,   // "Offline": sits faded (wait 0, dimmed): what he knows of the Mac is old (docs/20 U19)
};

uint8_t mando_poses(MandoAct a);      // 4 for the loops and the hop, 1 for the stills
uint8_t mando_rest_pose(MandoAct a);  // the pose drawn while nothing animates (mockups 04-07)
ArtId mando_art(MandoSize s, MandoAct a, uint8_t pose);  // the pose wraps; 64 px sits for Hop and Doze

const char* mando_sign(MandoAct a);  // Here Listen Listen Sniff… You! Saved Huh? Napping Offline
Tone mando_sign_tone(MandoAct a);    // Info Info Info Neutral Warn Ok Neutral Neutral Neutral
char mando_mark(MandoAct a);         // '!' (NeedsYou), '?' (NoVoice), else 0

// A 9 x 13 bubble with `mark` (a capsule and a two-pixel tail) at (x, y), in the tone's ink
// with an OnAccent mark. Step 0 sits 2 px lower: it pops up in two steps.
void bubble(Canvas& c, int16_t x, int16_t y, char mark, Tone t, Color under, uint8_t step = 1);

// v0.5.0 (docs/04, hidden keys): a pat's bubble, where mando() puts his "!" or "?": the
// same capsule in Rec with an OnAccent heart (5 x 4 px) in it. Step 0 sits 2 px lower.
void heart(Canvas& c, MandoSize s, int16_t x, int16_t y, Color under, uint8_t step = 1);

// Where mando() draws: the sprite and the room its bubble needs.
Rect mando_box(MandoSize s, int16_t x, int16_t y);
Rect mando_sprite(MandoSize s, int16_t x, int16_t y);  // the sprite alone (an act with no bubble)

// Mando at (x, y) doing `a` in `pose`, with his bubble (at `bubble`'s step) when the act has
// one, faded (his sprite dimmed) when Offline. No sign: the page puts it where its layout has
// room (sign()). Returns mando_box.
Rect mando(Canvas& c, int16_t x, int16_t y, MandoSize s, MandoAct a, uint8_t pose, Color under = Color::Bg,
           uint8_t bubble = 1);

}  // namespace theme
}  // namespace cpa
