#include "player.h"

#include <cmath>
#include <memory>

#include "presses.h"
#include "protocol_limits.h"
#include "sim_device.h"

namespace cpasim {
namespace {

constexpr int kSettleLoops = 50;  // a shot waits at most 500 ms for the screen

// `mic ... speech`: syllables of 2-5 blocks rising and falling to a peak, and now and then a
// pause of 1-3 blocks of the room, at the levels of the user's memos on the device (docs/19
// §6.6): words RMS about 225-1100 (-43 to -30 dBFS), the room 16. Deterministic.
struct Voice {
  uint32_t seed = 7;
  uint32_t span = 0;
  uint32_t left = 0;
  uint32_t peak = 0;  // 0: a pause
};

uint32_t next_level(Voice& v) {
  if (v.left == 0) {
    v.seed = v.seed * 1103515245u + 12345u;
    const uint32_t r = (v.seed >> 16) & 0x7FFF;
    const bool pause = r % 100 < 18;
    v.span = pause ? 1 + r % 3 : 2 + r % 4;
    v.peak = pause ? 0 : 250 + (r * 7) % 750;
    v.left = v.span;
  }
  const uint32_t k = v.span - v.left + 1;
  --v.left;
  if (v.peak == 0) return 16;
  return 100 + static_cast<uint32_t>(v.peak * std::sin(3.14159265 * k / (v.span + 1)));
}

struct Run {
  std::unique_ptr<SimDevice> dev;
  cpa::KeySet held;
  int host_id = 1;
  bool pinging = false;  // after `host hello`, until the cable is pulled
  uint32_t last_ping = 0;
  Voice voice;
  uint32_t mic_since = 0;  // ms since the last block while a microphone step runs
  LoopHook hook;
  PlayResult out;
};

std::string message(Run& r, const std::string& type, const std::string& re, const std::string& fields) {
  std::string m = "{\"v\":0,\"t\":\"" + type + "\",\"id\":\"h" + std::to_string(r.host_id++) + "\"";
  if (!re.empty()) m += ",\"re\":\"" + re + "\"";
  return m + (fields.empty() ? "" : "," + fields) + "}";
}

// One loop; the daemon's side pings every 5 s once it said hello, as cpad does, so the
// link stays up through long waits. debug.reboot restarts the device here as on `serve`:
// the daemon has to say hello again.
void tick(Run& r, cpa::KeySet keys) {
  if (r.pinging && r.dev->now() - r.last_ping >= cpa::limits::kPingIntervalMs) {
    r.dev->host(message(r, "ping", "", ""));
    r.last_ping = r.dev->now();
  }
  r.dev->loop(keys);
  if (r.hook) r.hook(*r.dev);
  if (!r.dev->reboot_due()) return;
  r.dev->reboot();
  r.pinging = false;
}

void loops(Run& r, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += kTickMs) tick(r, r.held);
}

// false when the host's lines are still unread or the screen still behind after 500 ms;
// still: a slide of the Tab sheet has come to rest too (frames shoot it on its way).
bool settle(Run& r, bool still = true) {
  for (int i = 0; i < kSettleLoops && !r.dev->idle(still); ++i) tick(r, r.held);
  return r.dev->idle(still);
}

void host(Run& r, const Step& st) {
  r.dev->host(message(r, st.type, "", st.text));
  if (st.type != "hello") return;
  r.pinging = true;
  r.last_ping = r.dev->now();
}

bool reply(Run& r, const Step& st) {
  settle(r);
  const std::string re = r.dev->last_sent_id(st.re_type);
  if (re.empty()) {
    r.out.error = st.where + ": the device has sent no " + st.re_type + " to answer";
    return false;
  }
  r.dev->host(message(r, st.type, re, st.text));
  return true;
}

// docs/28 S1a: the simulated daemon takes what it is shown: file.list, then file.del of each
// item (cpad deletes once it has stored a segment), page by page.
void pull(Run& r) {
  for (int page = 0; page < 64; ++page) {
    r.dev->host(message(r, "file.list", "", ""));
    settle(r);
    const std::vector<std::string> dels = r.dev->dels_for_last_items();
    for (const std::string& f : dels) {
      r.dev->host(message(r, "file.del", "", f));
      settle(r);
    }
    if (dels.empty()) return;
  }
}

// ms of the loop with a block every 128 ms: a steady tone or the voice.
void mic_loops(Run& r, const Step& st, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += kTickMs) {
    tick(r, r.held);
    r.mic_since += kTickMs;
    if (r.mic_since < kMicBlockMs) continue;
    r.mic_since -= kMicBlockMs;
    r.dev->mic_block(st.speech ? next_level(r.voice) : st.value);
  }
}

void mic(Run& r, const Step& st) {
  r.mic_since = 0;
  mic_loops(r, st, st.ms);
}

void take(Run& r, const std::string& name, const std::string& caption, bool settled) {
  Shot s;
  s.settled = settled;
  s.name = name;
  s.caption = caption;
  s.frame = r.dev->screen();
  s.at_ms = r.dev->now();
  s.backlight = r.dev->backlight();
  s.top = r.dev->top_id();
  s.missing_glyphs = r.dev->missing_glyphs();
  s.aa_misses = r.dev->aa_misses();
  const SimDevice::Edges e = r.dev->edges();
  s.offscreen = e.n;
  s.offscreen_text = e.last;
  s.pushes = r.dev->take_pushes();
  r.out.shots.push_back(s);
}

void shot(Run& r, const Step& st) { take(r, st.type, st.text, settle(r)); }

// The first frame once the screen is drawn (a slide's first frame), the others every st.ms
// as they come.
void frames(Run& r, const Step& st) {
  const bool settled = settle(r, false);
  r.mic_since = 0;
  for (uint32_t i = 1; i <= st.count; ++i) {
    take(r, st.type + "-" + std::to_string(i), st.text + " " + std::to_string(i) + "/" + std::to_string(st.count),
         i > 1 || settled);
    if (i == st.count) break;
    if (st.mic) {
      mic_loops(r, st, st.ms);
    } else {
      loops(r, st.ms);
    }
  }
}

// The steps that record, look or pull: the microphone, the shots, the daemon's pull.
bool media_step(Run& r, const Step& st) {
  switch (st.op) {
    case Op::Mic: mic(r, st); return true;
    case Op::Shot: shot(r, st); return true;
    case Op::Frames: frames(r, st); return true;
    case Op::Pull: pull(r); return true;
    default: return true;
  }
}

bool step(Run& r, const Step& st) {
  switch (st.op) {
    case Op::Boot:
      r.dev.reset(new SimDevice(st.boot));
      r.held = cpa::KeySet{};
      r.pinging = false;
      return true;
    case Op::Host: host(r, st); return true;
    case Op::Reply: return reply(r, st);
    case Op::Press:
      for (const cpa::KeySet& k : press_samples(st.keys, st.ms, r.held)) tick(r, k);
      return true;
    case Op::Down: r.held = with_keys(r.held, st.keys); loops(r, kTickMs); return true;
    case Op::Up: r.held = without_keys(r.held, st.keys); loops(r, kTickMs); return true;
    case Op::Wait: loops(r, st.ms); return true;
    case Op::Usb:
      r.dev->set_usb(st.value != 0);
      r.pinging = r.pinging && st.value != 0;  // a new cable needs a new hello
      return true;
    case Op::Bat: r.dev->set_bat(static_cast<uint16_t>(st.value)); return true;
    default: return media_step(r, st);
  }
}

}  // namespace

PlayResult play(const std::vector<Step>& steps, const LoopHook& hook) {
  Run r;
  r.hook = hook;
  if (steps.empty() || steps.front().op != Op::Boot) r.dev.reset(new SimDevice(BootSpec{}));
  for (const Step& st : steps) {
    if (!step(r, st)) break;
  }
  return r.out;
}

}  // namespace cpasim
