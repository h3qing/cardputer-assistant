// The stdin of `cpa-sim serve` (gh #32): script steps (script.h) for a running device, one
// per line, plus `quit`. Keys (press, hold, type, down, up, wait) go through a KeyFeed,
// one KeySet per 10 ms loop in order; usb, bat and shot act on the device. boot, host,
// reply, mic and include only work in a script: on serve, cpad is the host and the
// microphone is --mic.
#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include "key_set.h"
#include "script.h"

namespace cpasim {

enum class LiveKind : uint8_t { Empty, Step, Quit, Error };

struct LiveCmd {
  LiveKind kind = LiveKind::Empty;
  Step step;          // kind == Step
  std::string error;  // kind == Error, without a "file:line:" prefix
};

LiveCmd parse_live(const std::string& line);

bool is_key_step(Op op);  // Press, Down, Up, Wait

class KeyFeed {
 public:
  void add(const Step& st);  // a key step; others are ignored
  cpa::KeySet next();        // the keys of this loop
  bool idle() const { return q_.empty(); }

 private:
  std::deque<cpa::KeySet> q_;
  cpa::KeySet held_;  // down after the last queued sample
};

}  // namespace cpasim
