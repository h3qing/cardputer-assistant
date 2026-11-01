// The plug-in hint (docs/28 §3.2, D11), pure. At a hello, when segments recorded offline
// (ids from the offline mark on) wait, the hint bar says so for kPlugShowMs and those
// segments are held back from the Mac meanwhile; nothing else waits:
//   Shown  --Tab-->  Asking  --Enter-->  Busy (Hold: the ledger keeps them from this Mac)
//   Shown  --kPlugShowMs-->  None (Release: the Mac may take them)
//   Asking --` / Fn+` / another key / kPlugAskMs-->  None (Release; another key goes on)
// Busy ends with the ledger's answer (plug_done). 0 presses when right, 2 when not.
// Locked or under the Tab menu the bar cannot be seen: its time stands still (plug_hide) and
// the segments stay held, kPlugHiddenMaxMs from the hello at most (review 1, 2).
#pragma once

#include <cstdint>

#include "app_state.h"
#include "chord_detector.h"
#include "mono_id.h"

namespace cpa {

constexpr uint32_t kPlugShowMs = 5000;
constexpr uint32_t kPlugAskMs = 10000;
constexpr uint32_t kPlugHiddenMaxMs = 120000;

struct PlugState {
  PlugHint phase = PlugHint::None;
  uint32_t since = 0;   // the phase began (moved on while hidden)
  uint32_t opened = 0;  // the hello's hint opened
  MonoId from;         // the offline mark: held from this id on
  uint8_t n = 0;       // recordings
};

enum class PlugDo : uint8_t { None, Hold, Release };

struct PlugStep {
  PlugState st;
  bool taken = false;  // the key was the hint's
  PlugDo act = PlugDo::None;
};

PlugState plug_open(uint8_t recs, const MonoId& from, uint32_t now);
PlugStep plug_key(const PlugState& s, const KeyEvent& ev, uint32_t now);
PlugStep plug_tick(const PlugState& s, uint32_t now);
PlugState plug_done(const PlugState& s);
PlugState plug_hide(const PlugState& s, uint32_t dt);  // dt ms the bar could not be seen
inline bool plug_holds(const PlugState& s) { return s.phase != PlugHint::None; }

}  // namespace cpa
