// Motion (docs/19 §3.5, §7.3-§7.4): what moves, one kAnimTickMs tick at a time, and only
// while the backlight is On. A page keeps what its last tick showed (a MandoMotion, the breath
// step, a copy of the level history) and only a tick moves it, so a full redraw at Dim, or
// under the Tab menu, draws the same still picture. Apps (docs/16) animate Mando with these
// from Module::animate(). Pure.
#pragma once

#include <cstdint>

#include "theme_mando.h"

namespace cpa {
namespace theme {

constexpr uint32_t kAnimTickMs = 200;  // Mando's poses, the waveform, the breath
constexpr uint8_t kZs = 3;             // the dozing z's
constexpr uint8_t kZTicks = 3;         // one more z every 600 ms
constexpr uint16_t kSniffTicks = 40;   // he sniffs 8 s at most, then holds still

bool tick_due(uint32_t last, uint32_t now);  // kAnimTickMs since the last tick

// What Mando shows: the act drawn, its pose, the bubble's step (0 sits 2 px lower, 1 is up)
// and how many of the dozing z's are lit.
struct MandoView {
  MandoAct act = MandoAct::Idle;
  uint8_t pose = 0;
  uint8_t bubble = 0;
  uint8_t z = kZs;
};
bool same_view(const MandoView& a, const MandoView& b);

// Mando between ticks: the view the last tick drew, the ticks its act has run (0 on the
// act's first), and a phase all loops share, so nodding and tilting swap without a jump.
struct MandoMotion {
  MandoView shown;
  uint16_t ticks = 0;
  uint32_t phase = 0;
};

// One tick with Mando doing `act`: loops step through their four poses, a hop plays once,
// a sniff holds still after kSniffTicks, a bubble pops up on the second tick, a z lights
// every kZTicks (all three lit first, then none, one, two, three).
MandoMotion mando_tick(const MandoMotion& m, MandoAct act);

// What to draw for `act` between ticks: the last tick's view while the act is the same (or
// Listen and Tilt: the act too stays until a tick), else the act's entry view: its rest pose
// (a hop's first), the bubble low, every z lit.
MandoView mando_view(const MandoMotion& m, MandoAct act);

// Mando at (x, y) as `v` shows him (theme_mando.h mando()). Returns mando_box.
Rect mando(Canvas& c, int16_t x, int16_t y, MandoSize s, const MandoView& v, Color under = Color::Bg);

// The big ●'s next breath step (0-3; 0 is full Rec).
uint8_t breath_next(uint8_t step);

// The Tab sheet and its segment knob slide in kSlideFrames frames kSlideMs apart (t = .45,
// .85, 1): the full redraw that opens the sheet draws frame 0.
constexpr uint8_t kSlideFrames = 3;
constexpr uint32_t kSlideMs = 50;
int16_t slide_at(int16_t from, int16_t to, uint8_t frame);  // frame >= kSlideFrames - 1: to
// The segment knob: t = .75, .9, 1, so the full redraw already has the new page's name on it
// and the old one's clear of it.
int16_t knob_at(int16_t from, int16_t to, uint8_t frame);
uint8_t slide_frame(uint32_t since_ms);                      // the frame due, at most the last

}  // namespace theme
}  // namespace cpa
