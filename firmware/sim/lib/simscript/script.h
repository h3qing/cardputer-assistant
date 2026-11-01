// Sim scripts (gh #32, firmware/sim/README.md): what the simulator plays, one step per
// line. A script only presses keys and sends protocol messages, so pages added to the
// firmware later need no change here. '#' starts a comment (at the start of a line or
// after a blank, outside "strings"), in `type` text too: type " #" as `press space shift+3`.
//   include <file>                     relative to the including file
//   boot [store=sd|internal|none] [usb=on|off] [bat=MV]
//   host <type> [{fields}]             {"v":0,"t":<type>,"id":"hN",fields}
//   reply <type> <re-type> [{fields}]  ... "re": id of the device's last <re-type>
//   press <combo>... [ms=N]            each combo held N ms (50), then 30 ms released
//   hold <combo> <ms>                  one long press
//   type <text>                        ASCII, Shift added for shifted characters
//   down <combo>... / up [<combo>...]  keys held across the following steps (up: all)
//   wait <ms> / usb on|off / bat <mv>
//   mic <ms> [level|speech]            while recording: a tone of this RMS (2000), or a
//                                      voice: syllables rising and falling, short pauses
//   shot <name> [caption]              save the screen
//   frames <name> <n> <ms> [mic=level|mic=speech] [caption]
//                                      v0.5 S3: n shots <ms> apart, <name>-1 .. <name>-n, the
//                                      first once the screen settled, the others as they come
//                                      (the animation, docs/19 §7); the microphone as `mic`
// A combo is key names joined by '+': opt+s, fn+`, shift+a, enter, g0 (the names of
// debug.key), plus up / down / left / right (the ; . , / keys) and esc (fn+`).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "debug_inject.h"
#include "key_set.h"
#include "lang.h"
#include "storage_plan.h"

namespace cpasim {

enum class Op : uint8_t { Boot, Host, Reply, Press, Down, Up, Wait, Usb, Bat, Mic, Shot, Frames, Pull };

constexpr uint32_t kPressMs = cpa::kInjectHoldDefaultMs;  // debug.key's default hold
constexpr uint32_t kMicLevel = 2000;                      // RMS of the default test tone

struct BootSpec {
  cpa::Store store = cpa::Store::Sd;
  bool usb = true;
  uint16_t bat_mv = 4100;
  cpa::Lang lang = cpa::Lang::Zh;  // docs/21: the language NVS holds at boot
};

struct Step {
  Op op = Op::Wait;
  std::string where;              // "file:line"
  std::vector<cpa::KeySet> keys;  // Press: one set per press; Down / Up: the keys
  uint32_t ms = 0;                // Press: hold of each press; Wait / Mic: duration; Frames: apart
  uint32_t value = 0;             // Usb: 1 = on; Bat: mV; Mic / Frames: level
  std::string type;               // Host / Reply: message type; Shot / Frames: file name
  std::string re_type;            // Reply: the device message answered
  std::string text;               // Host / Reply: fields without braces; Shot / Frames: caption
  uint32_t count = 0;             // Frames: how many shots
  bool mic = false;               // Frames: the microphone runs (level or speech as Mic's)
  bool speech = false;            // Mic / Frames: a voice instead of a steady tone
  BootSpec boot;
};

struct Script {
  std::vector<Step> steps;
  std::string error;  // "file:line: what is wrong", empty when the script is valid
};

using Reader = std::function<bool(const std::string& path, std::string& text)>;

Script parse_script(const std::string& path, const Reader& read);
bool parse_combo(const std::string& token, cpa::KeySet& out);

}  // namespace cpasim
