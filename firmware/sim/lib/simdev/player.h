// play() (gh #32): runs a parsed sim script on a SimDevice and collects the screenshots.
// v0.5 S3: `frames` shoots the screen every few ms without waiting for it to settle, to
// catch the animation (docs/19 §7), and `mic ... speech` feeds a voice whose level moves.
// Time only moves in 10 ms loops: presses as press_samples() lays them out, waits and the
// microphone loop by loop, and a shot first runs the loop until the host's lines are read
// and the screen is redrawn, a Tab sheet slide come to rest (at most 500 ms; Shot::settled
// says whether it was). A
// debug.reboot restarts the device as on `serve`. A script that does not start with
// `boot` boots with the defaults (SD card, USB on, 4.1 V).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fb.h"
#include "sim_display.h"
#include "platform.h"
#include "script.h"

namespace cpasim {

struct Shot {
  std::string name;
  std::string caption;
  Frame frame{1, 1};
  uint32_t at_ms = 0;
  cpa::Backlight backlight = cpa::Backlight::On;
  std::string top;  // the module in front
  uint32_t missing_glyphs = 0;  // □ in the frame (code points efontCN_12 has no glyph for)
  uint32_t aa_misses = 0;       // rounded shapes since boot whose edge used a nearest colour
  uint32_t offscreen = 0;       // texts on the screen drawn past an edge or into the last column
  std::string offscreen_text;   // the last of them, "x,y text"
  bool settled = true;          // false: taken after 500 ms with lines unread or the screen behind
  Pushes pushes;                // pushed to the panel since the shot before (docs/19 §7.5)
};

struct PlayResult {
  std::vector<Shot> shots;
  std::string error;  // "file:line: what went wrong"
};

class SimDevice;
// Called after every loop (tests: what the panel holds at each moment).
using LoopHook = std::function<void(const SimDevice&)>;

PlayResult play(const std::vector<Step>& steps, const LoopHook& hook = LoopHook{});

}  // namespace cpasim
