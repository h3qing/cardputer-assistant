// docs/29 P1 "暗屏少转", pure: when the device's loop() may give the CPU away for a while
// (vTaskDelay in src/main.cpp), so the idle task's WAITI replaces a 240 MHz busy loop. In the
// spirit of docs/29 §4 (D5): only on a dark screen on battery with nothing going on. Never
//   - while the backlight is On or Dim (someone looks, or ● REC shows);
//   - while the recording state machine is not Idle (a memo, a meeting, their boxes, the
//     end) or a memo's result is still held for G0's release;
//   - while a USB host is there (raw plugged() or the link's debounced view);
//   - while the audio task has work: the boot scan's index not ready, a file.* queued or
//     with the task, a Ledger command out, a Hold not yet done, a Clear's next round;
//   - while a key is down, nor kYieldGraceMs after the last new key (a chord or a tap
//     and hold on the dark lock card answers at the full rate);
//   - while a settings write or (test firmware) a debug injection waits.
// One yield costs at most kIdleYieldMs of key latency (docs/29 §10: < 20 ms added).
// Times are millis() and wrap after 49.7 days: every difference is modulo 2^32.
#pragma once

#include <cstdint>

namespace cpa {

constexpr uint32_t kIdleYieldMs = 10;
constexpr uint32_t kYieldGraceMs = 1000;

struct IdleFacts {
  bool dark = false;         // the backlight is Off (not On, not Dim)
  bool rec_idle = false;     // RecPhase::Idle and no held memo
  bool host = true;          // a USB host: plugged() now, or the link still counts it
  bool audio_busy = true;    // the audio task has (or owes) work
  bool keys_down = true;     // a key (G0 included) is down in this pass
  bool key_seen = false;     // a new key went down since boot ...
  uint32_t key_ms = 0;       // ... last at this time
  bool writes_due = false;   // a settings write, or a debug injection, waits
};

// 0: run the next pass at once; otherwise the milliseconds loop() may sleep first.
uint32_t idle_yield_ms(const IdleFacts& f, uint32_t now);

}  // namespace cpa
