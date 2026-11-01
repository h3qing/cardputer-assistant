// The kept copies' runtime tests (docs/28 S1a): two Macs told apart by their hello's host_id,
// what a Mac does with file.*, keys pressed on the keyboard, and what the screen shows.
#pragma once

#include <unity.h>

#include <string>

#include "runtime.h"
#include "support/rec_rig.h"

namespace uitest {

inline const char* const kMacA = "mac-aaaa-0001";
inline const char* const kMacB = "mac-bbbb-0002";

inline void hello(RecRig& x, const char* host) {
  x.r.tx.is_plugged = true;
  x.r.send("hello", std::string("\"role\":\"host\",\"app\":\"cpad/0.5.0\",\"agent\":\"ok\",\"host_id\":\"") + host +
                        "\",\"caps\":{\"spawn\":true,\"reply\":false}");
  x.r.step();
}

inline cpa::proto::FileItems list(RecRig& x) {
  x.r.send("file.list");
  x.r.step();
  return std::get<cpa::proto::FileItems>(x.last("file.items").msg.body);
}

// What a Mac does: list, then delete each item (cpad pulls first; the bytes do not matter here).
inline uint8_t take_all(RecRig& x) {
  const cpa::proto::FileItems items = list(x);
  for (uint8_t i = 0; i < items.n; ++i) {
    const cpa::proto::FileItem& it = items.items[i];
    x.r.send("file.del", "\"fid\":\"" + std::string(it.id.c_str()) + "\",\"size\":" + std::to_string(it.size) +
                             ",\"sha256\":\"" + it.sha256.value.c_str() + "\"");
    x.r.step();
  }
  return items.n;
}

inline uint16_t files_n(RecRig& x) {
  x.r.send("ping");
  x.r.step();
  return std::get<cpa::proto::Pong>(x.last("pong").msg.body).files.value.n;
}

// A key pressed on the keyboard: the runtime's own path (the hint takes its keys there).
inline void press(RecRig& x, cpa::KeySet ks) {
  x.r.step(30, ks);
  x.r.step(30);
}
inline void press(RecRig& x, uint8_t k) { press(x, cpa::KeySet{}.with(k)); }

// ms of a Mac that stays on the link (a ping every second: the link never times out).
inline void wait_linked(RecRig& x, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 1000) {
    x.r.send("ping");
    x.r.step(ms - t < 1000 ? ms - t : 1000);
  }
}

// Whether `needle` is on the screen within ms (a toast queued behind another waits its turn).
inline bool shows(RecRig& x, const std::string& needle, uint32_t ms = 8000);

inline const cpa::KeptView& kept(RecRig& x) { return x.r.rt.state().rec.kept; }

// What the screen shows now (its toast bar included).
inline std::string screen(RecRig& x) {
  x.r.disp.canvas_.clear();
  x.r.rt.shell().render(x.r.disp.canvas_, x.r.now);
  return x.r.disp.canvas_.dump();
}

inline bool shows(RecRig& x, const std::string& needle, uint32_t ms) {
  for (uint32_t t = 0; t <= ms; t += 250) {
    if (screen(x).find(needle) != std::string::npos) return true;
    wait_linked(x, 250);
  }
  return false;
}

}  // namespace uitest
