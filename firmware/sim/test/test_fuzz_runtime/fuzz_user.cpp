// The user's side of the fuzz test: random keys and gestures, then the way home a person
// reading the screen would take, and the final drain that checks nothing was lost.
#include <algorithm>
#include <cstring>

#include "chord_detector.h"
#include "fuzz.h"
#include "mono_id.h"
#include "rec_core.h"

namespace fuzz {
namespace {

cpa::KeySet keyset(std::initializer_list<uint8_t> l) {
  cpa::KeySet s;
  for (uint8_t k : l) s = s.with(k);
  return s;
}

uint8_t char_key(char c, bool& shift) {
  for (uint8_t k = 0; k < cpa::kMatrixKeys; ++k) {
    if (cpa::is_modifier(k)) continue;
    shift = cpa::key_shifted(k) == c && cpa::key_base(k) != c;
    if (cpa::key_base(k) == c || shift) return k;
  }
  shift = false;
  return K::Space;
}

}  // namespace

Fuzz::Fuzz(uint64_t s, cpa::Store store, cpa::Lang lang)
    : seed(s), ascii(lang == cpa::Lang::En), rng(s), dev(new Dev(store, lang)) {}

// docs/21 §9.2: every frame since the last step. A Han code point (other than the
// language names) while English shows, in a run whose Mac sends ASCII only, is a string
// not moved to a Txt; text past the screen's edge in a still frame (not the Tab sheet's
// slide) was not fitted, in either language.
void Fuzz::check_text(bool en_before) {
  const uint32_t han = dev->disp.han_drawn();
  const uint32_t frames = dev->disp.presents();
  if (ascii && en_before && cpa::lang() == cpa::Lang::En && han != han_seen) fail("Han drawn in English");
  const bool slide = sh().sliding();  // the Tab sheet comes in from off the screen
  if (frames != frames_seen && !slide && dev->disp.frame_offscreen() > 0) {
    fail("text drawn past the screen's edge: " + dev->disp.last_offscreen());
  }
  han_seen = han;
  frames_seen = frames;
}

void Fuzz::fail(const std::string& what) {
  const std::string line = "seed " + std::to_string(seed) + " T=" + std::to_string(T) + ": " + what + " [" + where() + "]";
  for (const std::string& f : failures) {
    if (f.find(what) != std::string::npos) return;  // once per kind
  }
  failures.push_back(line);
}

std::string Fuzz::where() const {
  char b[160];
  const cpa::RecInfo& r = st().rec;
  std::snprintf(b, sizeof b, "top=%s ov=%d menu=%d lock=%d rec=%d/%d held=%d link=%d", sh().top_id(),
                static_cast<int>(sh().overlay()), sh().menu_open(), sh().locked(), static_cast<int>(r.phase),
                static_cast<int>(r.mode), r.held, static_cast<int>(st().link.mode));
  return b;
}

// One 10 ms loop, the microphone while recording, the host, and the meeting invariant.
void Fuzz::step(cpa::KeySet k) {
  T += kTick;
  const cpa::RecPhase before = st().rec.phase;
  const bool en_before = cpa::lang() == cpa::Lang::En;  // a switch in this loop drew both
  dev->loop(k);
  check_text(en_before);
  const cpa::RecPhase after = st().rec.phase;
  if (after == cpa::RecPhase::Confirm && before != cpa::RecPhase::Confirm) confirm_since = T;
  if (before == cpa::RecPhase::Confirm && after == cpa::RecPhase::Cue && T - confirm_since < 400) {
    fail("a meeting started less than 400 ms after its box");
  }
  mic_acc = dev->recording() ? mic_acc + kTick : 0;
  if (mic_acc >= 128) {
    mic_acc -= 128;
    dev->mic_block(1500 + rnd(3000));
  }
  for (uint16_t i = 0; i < dev->rt->outbox().n; ++i) ever_pending.insert(cpa::mono_format(dev->rt->outbox().ids[i]).c_str());
  if (dev->rt->outbox().n_bad > 0) fail("an outbox item went bad");
  host_tick();
  if (dev->reboot_due()) {
    dev->reboot();
    held = cpa::KeySet{};
    interrupt();
    hello_at = T + 500 + rnd(2000);
  }
  if (hello_at && T >= hello_at) {
    hello_at = 0;
    hello();
  }
}

void Fuzz::run_ms(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += kTick) step(held);
}

void Fuzz::press(cpa::KeySet k, uint32_t hold_ms) {
  ++keys;
  cpa::KeySet all = held;
  for (uint8_t i = 0; i <= K::G0; ++i) all = k.has(i) ? all.with(i) : all;
  for (uint32_t t = 0; t < hold_ms; t += kTick) step(all);
  for (uint32_t t = 0; t < 30; t += kTick) step(held);
}

void Fuzz::pk(std::initializer_list<uint8_t> l, uint32_t hold) { press(keyset(l), hold); }

void Fuzz::type(const std::string& s) {
  for (char c : s) {
    bool shift = false;
    const uint8_t k = char_key(c, shift);
    press(shift ? keyset({K::Shift, k}) : keyset({k}));
  }
}

void Fuzz::hold_g0(uint32_t ms) {
  ++keys;
  held = held.with(K::G0);
  run_ms(ms);
  held = held.without(K::G0);
  run_ms(30);
}

// A whole capture: open, maybe a target, a unique token, submit, maybe confirm.
void Fuzz::act_capture() {
  if (chance(0.6)) escape();
  static const uint8_t kOpen[] = {K::N, K::Q, K::S};
  const uint32_t how = rnd(4);
  if (how == 0) pk({K::G0}, 60);
  if (how > 0) pk({K::Opt, kOpen[how - 1]});
  if (chance(0.5)) pk({K::Opt, static_cast<uint8_t>(1 + rnd(9))});
  type("z" + std::to_string(token++) + (chance(0.3) ? " call the vet" : ""));
  run_ms(rnd(400));
  if (chance(0.8)) pk({chance(0.7) ? K::Enter : K::G0}, 60);
  if (!chance(0.5)) return;
  run_ms(300 + rnd(400));  // the confirm box of a spawn
  pk({K::Enter});
}

void Fuzz::act_keys(uint32_t r) {
  static const char kOptKeys[] = "hpimnqsrl1234567890d";
  static const uint8_t kNav[] = {39, 53, 52, 54};  // ; . , /
  bool shift = false;
  if (r < 50) return type("z" + std::to_string(token++));
  if (r < 140) return pk({static_cast<uint8_t>(rnd(56))});
  if (r < 240) return pk({K::Opt, char_key(kOptKeys[rnd(sizeof kOptKeys - 1)], shift)});
  if (r < 280) return pk({K::Fn, K::Grave});
  if (r < 320) return pk({K::Grave});
  if (r < 380) return pk({K::Enter});
  if (r < 430) return pk({K::Tab});
  if (r < 450) return pk({K::Bksp}, 200 + rnd(3000));
  if (r < 500) return pk({K::Fn, kNav[rnd(4)]});  // docs/20 U17: the Fn arrows
  if (r < 550) {
    for (int i = 1 + rnd(4); i > 0; --i) pk({kNav[rnd(4)]});
    return;
  }
  cpa::KeySet k;  // three or four keys at once (ghosting)
  for (int i = 3 + rnd(2); i > 0; --i) k = k.with(static_cast<uint8_t>(rnd(56)));
  press(k, 40 + rnd(200));
}

void Fuzz::act_recording(uint32_t r) {
  if (r < 40) return pk({K::G0}, 30 + rnd(400));  // a tap
  if (r < 70) {  // a lock, maybe its unlock
    pk({K::Opt, K::L});
    if (!chance(0.6)) return;
    run_ms(rnd(4000));
    pk({K::Opt, K::L});
    run_ms(rnd(3500));
    return pk({K::Enter});
  }
  if (r < 140) {  // a memo, keys while holding, sometimes past the cap (docs/20 U15)
    ++memos;
    held = held.with(K::G0);
    run_ms(500 + rnd(2500));
    for (int i = rnd(3); i > 0; --i) pk({chance(0.5) ? static_cast<uint8_t>(1 + rnd(10)) : static_cast<uint8_t>(rnd(56))});
    if (chance(0.4)) g0_slip();  // gh #51: the grip slips, inside the grace or not
    run_ms(rnd(4000));
    if (chance(0.02)) run_ms(300000);
    held = held.without(K::G0);
    return run_ms(30);
  }
  if (r < 155) return act_hands_free();
  ++meetings;  // a meeting
  pk({K::Opt, K::R});
  run_ms(100 + rnd(900));
  pk({K::Enter});
  run_ms(rnd(20000));
  for (int i = rnd(4); i > 0; --i) pk({chance(0.5) ? K::Space : static_cast<uint8_t>(1 + rnd(10))});
  if (chance(0.7)) {
    pk({K::Opt, K::R});
    run_ms(rnd(900));
    pk({K::Enter});
  }
}

void Fuzz::act_link(uint32_t r) {
  if (r < 50) {  // the cable
    cable = false;
    dev->tx.is_plugged = false;
    hello_at = 0;
    interrupt();
    run_ms(100 + rnd(40000));
    cable = true;
    dev->tx.is_plugged = true;
    hello_at = T + 100 + rnd(600);
    return;
  }
  if (r < 80) {  // the daemon stalls past the link timeout
    alive = false;
    interrupt();
    run_ms(2000 + rnd(25000));
    alive = true;
    return hello();
  }
  if (r < 100) return hello();  // the daemon restarts
  run_ms(50 + rnd(5000));       // nobody presses anything
}

void Fuzz::act_host(uint32_t r) {
  static const char* const kJunk[] = {"{\"v\":0,\"t\":\"nope\",\"id\":\"h9999\"}", "not json at all",
                                      "{\"v\":1,\"t\":\"ping\",\"id\":\"x\"}", "\xff\xfe\x01{"};
  if (r < 40) return snapshots(false);
  if (r < 60) return send(chance(0.2) ? std::string(3000 + rnd(9000), 'x') : std::string(kJunk[rnd(4)]));
  if (r < 66) return send(msg("debug.reboot", "", false));
  if (r < 90) {
    dev->plat.next.bat_mv = static_cast<uint16_t>(chance(0.3) ? 3300 + rnd(300) : 3700 + rnd(500));
    return;
  }
  dev->plat.room = 100;  // the host stops reading for a while: the TX ring fills
  run_ms(200 + rnd(3000));
  dev->plat.room = static_cast<size_t>(-1);
}

void Fuzz::random_action() {
  const uint32_t r = rnd(1000);
  if (r < 90) return act_capture();
  if (r < 650) return act_keys(rnd(600));
  if (r < 800) return act_recording(rnd(170));
  if (r < 900) return act_link(rnd(200));
  act_host(rnd(100));
}

bool Fuzz::at_home() const {
  return std::strcmp(sh().top_id(), "home") == 0 && sh().overlay() == cpa::proto::Overlay::None && !sh().menu_open() &&
         !sh().locked() && st().rec.phase == cpa::RecPhase::Idle && !st().rec.held;
}

// A memo on screen, as a person ends it (gh #51): hands free, one G0 tap; let go, the grace
// runs out by itself. Any other memo page is one nobody holds: a failure. The presses taken.
int Fuzz::escape_memo(const cpa::RecInfo& r) {
  if (r.latched) {
    pk({K::G0});
    return 1;
  }
  if (r.grace) {
    run_ms(cpa::rec::kMemoGraceMs);
    return 0;
  }
  fail("a memo with no G0 held");
  hold_g0(700);
  return 1;
}

// What a user who wants "home, nothing open" presses, one step per look at the screen.
// The number of presses, or -1 when stuck.
// docs/21 §9.2: the language can always be found again: from home, Opt+M opens Settings on
// its WiFi row, "." is the language row, Enter switches. Three presses.
bool Fuzz::switch_language() {
  const cpa::Lang was = cpa::lang();
  bool shift = false;
  pk({K::Opt, char_key('m', shift)});
  pk({char_key('.', shift)});
  pk({K::Enter});
  run_ms(300);
  return cpa::lang() != was;
}

int Fuzz::escape() {
  held = cpa::KeySet{};
  run_ms(600);
  int presses = 0;
  for (int i = 0; i < 12 && !at_home(); ++i, run_ms(100)) {
    const cpa::RecInfo& r = st().rec;
    const bool page = r.phase == cpa::RecPhase::Cue || r.phase == cpa::RecPhase::Recording;
    if (sh().locked()) {
      pk({K::Opt, K::L});
      pk({K::Enter});
      presses += 2;
    } else if (sh().menu_open()) {
      pk({K::Tab});
      presses += 1;
    } else if (r.phase == cpa::RecPhase::Confirm || sh().overlay() == cpa::proto::Overlay::Confirm) {
      run_ms(450);
      pk({K::Grave});
      presses += 1;
    } else if (r.phase == cpa::RecPhase::EndConfirm) {
      run_ms(450);
      pk({K::Enter});
      presses += 1;
    } else if (page && r.mode == cpa::RecMode::Meeting) {
      pk({K::Opt, K::R});
      run_ms(450);
      pk({K::Enter});
      presses += 2;
    } else if (page || r.held) {
      presses += escape_memo(r);
    } else if (r.phase == cpa::RecPhase::Finishing) {
      run_ms(1000);
    } else if (sh().overlay() == cpa::proto::Overlay::Compose) {
      pk({K::Grave});  // docs/20 D5: ` leaves the editor (Fn+` types a backtick)
      presses += 1;
      if (sh().overlay() != cpa::proto::Overlay::Compose) continue;
      run_ms(450);  // the discard prompt: its guard (docs/20 U18), then Enter
      pk({K::Enter});
      presses += 1;
    } else {
      pk({K::Opt, K::H});
      presses += 1;
    }
  }
  return at_home() ? presses : -1;
}

void Fuzz::episode_run(uint32_t ms) {
  const uint64_t end = T + ms;
  while (T < end) random_action();
  const int p = escape();
  ++escapes;
  if (p < 0) {
    fail("stuck: no way home");
    send(msg("debug.reboot", "", false));  // the user's last resort
    run_ms(3000);
    return;
  }
  max_escape = std::max<uint64_t>(max_escape, static_cast<uint64_t>(p));
  if (p > kMaxHomePresses) fail("home took " + std::to_string(p) + " presses");
}

// Everything settles: plugged, the daemon healthy, keys idle, until the outbox and the
// card are empty. Nothing the device ever held may be missing at the host.
void Fuzz::drain() {
  escape();
  dev->plat.next.bat_mv = 4100;
  dev->plat.room = static_cast<size_t>(-1);
  cable = alive = true;
  dev->tx.is_plugged = true;
  hello();
  for (int i = 0; i < 1800 && (dev->rt->outbox().n > 0 || st().rec.files_n > 0 || !ready); ++i) {
    run_ms(1000);
    if (ready && !pull.active && st().rec.files_n > 0 && T % 5000 < 1000) list_due = T + 10;
  }
  if (dev->rt->outbox().n > 0) fail("the outbox never drained");
  if (st().rec.files_n > 0) fail("the recordings never drained");
  for (const std::string& id : ever_pending) {
    if (!stored.count(id)) fail("lost capture " + id);
  }
  for (const std::string& f : ever_index) {
    if (!f.empty() && !pulled.count(f)) fail("lost recording " + f);
  }
}

}  // namespace fuzz
