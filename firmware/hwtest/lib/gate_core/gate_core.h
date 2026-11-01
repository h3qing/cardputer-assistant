// Pure logic behind the M3 device gates in hwtest (docs/gates/m3-device-gates.md):
// argument parsing for !xfer / !cue / !mic, the app's file.data frame envelope,
// audio levels, CPU load from idle-hook counts. No Arduino, M5 or esp_* headers:
// unit-tested on the host with `pio test -e native`. C++11, because env:cardputer-adv
// builds with the core's default -std=gnu++11.
#pragma once

#include <cstddef>
#include <cstdint>

namespace gate {

enum class Enc : uint8_t { B64, Bin };
enum class Src : uint8_t { Ram, Sd, SdKeep };
enum class Pattern : uint8_t { Memo, Meet, Silent };  // silent: no speaker (v0.3.1)
enum class Hammer : uint8_t { Off, NoLock, Lock, LockYield };

constexpr uint16_t kXferDefaultSecs = 10;
constexpr uint16_t kCueDefaultRecMs = 1000;

struct XferArgs {
  Enc enc;
  uint16_t len;     // payload bytes per frame, 1..4096
  uint8_t depth;    // requests the host keeps in flight; 0 = free-running push
  uint8_t ring_kb;  // HWCDC TX ring: 4, 8 or 16 KB
  Src src;
  uint16_t secs;    // 1..120
};

struct CueArgs {
  uint16_t tone_ms;  // memo: the tone; meet: each of the three notes (20..1000); silent: unused
  uint8_t vol;       // Speaker.setVolume
  Pattern pattern;
  Hammer hammer;     // TCA8418 reads in the loop while the cue task reconfigures the codec;
                     // lockyield: the loop yields a tick after releasing while the task waits
  uint16_t rec_ms;   // capture after Mic.begin (200..1000)
};

struct MicArgs {
  uint8_t mag;   // M5 Mic magnification, 1..64
  uint8_t pga;   // ES8311 PGA step, 0..10 (3 dB each)
  uint16_t secs; // 1..300
};

template <typename T>
struct Parsed {
  bool ok;
  T args;
  const char* err;  // name of the first bad argument, nullptr when ok
};

// src: ram | sd (open, seek, read, close per frame) | sdkeep (one open file, sequential reads)
Parsed<XferArgs> parse_xfer(const char* s);  // "<b64|bin> <len> <depth> <4|8|16> <ram|sd|sdkeep> [secs]"
Parsed<CueArgs> parse_cue(const char* s);    // "<ms> <vol> [memo|meet|silent] [off|nolock|lock|lockyield] [rec_ms]"
Parsed<MicArgs> parse_mic(const char* s);    // "<mag> <pga> <secs>"

// ES8311 REG14: MIC1P/MIC1N selected (bit 4, as M5Unified sets it) plus the PGA step.
uint8_t es8311_reg14(uint8_t pga);

struct FrameMeta {
  uint32_t seq;
  uint32_t off;
  uint32_t n;
  uint32_t size;
  uint32_t crc;
};

constexpr size_t b64_len(size_t n) { return (n + 2) / 3 * 4; }
// Plain table base64 (RFC 4648, padded, no nul). Returns b64_len(n), or 0 and writes
// nothing when cap is short. mbedtls_base64_encode is constant-time and costs ~1.6 ms per
// 1380 B on the S3 (G-A1); this one is what the gate measures the link with.
size_t b64_encode(const uint8_t* in, size_t n, char* out, size_t cap);

// Envelope in front of the payload, shaped like the app's file.data line (11 §5.3):
// b64 ends with `"b64":"`, bin is a whole header line ending with `,"bin":N}\n`.
// Returns its length, or 0 when it does not fit in cap (nul included).
size_t frame_head(char* out, size_t cap, Enc enc, const FrameMeta& m);
size_t frame_tail(Enc enc);  // b64 `"}\n`, bin `\n`
size_t frame_size(Enc enc, size_t head_len, size_t n);

struct Level {
  uint16_t rms;   // about zero (the ES8311 removes DC), truncated
  uint16_t peak;  // max |sample|, 32768 for a -32768 sample
};

Level level(const int16_t* s, size_t n);
// Consecutive bins of per_bin samples; a partial tail is dropped. Returns bins written.
size_t level_bins(const int16_t* s, size_t n, size_t per_bin, Level* out, size_t cap);

// Busy % of a core from idle-hook calls in a window against an unloaded reference.
uint8_t busy_pct(uint32_t idle, uint32_t idle_ref);

struct Reg {
  uint8_t reg;
  uint8_t val;
};

size_t count_mismatch(const Reg* want, const uint8_t* got, size_t n);

// !warm / !cw (issue #31: the ES8311's warm-up after a codec power-on). Register writes are
// two hex digits each, "RR=VV"; a timed one is "<ms after Mic.begin>:RR=VV".
constexpr size_t kMaxRegWrites = 8;
constexpr uint16_t kWarmMaxMs = 4000;
constexpr uint32_t kWarmMaxSamples = 64000;  // 128 KB of heap: 4 s at 16 kHz
constexpr uint32_t kWarmRate = 16000;

struct TimedReg {
  uint16_t at_ms;
  uint8_t reg;
  uint8_t val;
};

struct WarmArgs {
  uint16_t rec_ms;  // capture after Mic.begin, 100..kWarmMaxMs, at most kWarmMaxSamples
  bool keep;        // the microphone stays on afterwards (no Mic.end)
  uint32_t rate;    // "hz=<8000..64000>" right after the mode; kWarmRate by default
  uint8_t n;
  TimedReg w[kMaxRegWrites];  // in at_ms order
};

struct RegWrites {
  uint8_t n;  // 1..kMaxRegWrites
  Reg r[kMaxRegWrites];
};

Parsed<WarmArgs> parse_warm(const char* s);  // "<rec_ms> <end|keep> [hz=<rate>] [<at_ms>:<RR>=<VV>]..."
Parsed<RegWrites> parse_regs(const char* s);  // "<RR>=<VV> [<RR>=<VV>]..."

// Samples that are exactly 0 from the start (the codec's muted warm-up).
size_t leading_zeros(const int16_t* s, size_t n);

}  // namespace gate
