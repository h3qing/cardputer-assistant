// The runtime fuzz test (docs/20 §4.5, A12): the firmware's real Runtime on the sim's
// fakes, wired as lib/simdev does, with a scripted daemon (handshake, pings, snapshots,
// acks, agent results, recording pulls checked by SHA-256, cable pulls, daemon stalls
// and restarts, garbage lines, debug.reboot, a full TX ring) and a random user (keys,
// chords, ghost triples, G0 taps and holds, memos past the cap, hands-free memos (G0 twice,
// gh #51), meetings, Tab, Opt+L,
// Backspace held). After every episode the user goes home the way a person reading the
// screen would. Invariants: home in at most 6 presses, never stuck, every capture the
// device ever held reaches the host, every pulled segment's SHA-256 matches, every host
// request is answered, a meeting only starts from an Enter at least 400 ms after its box.
// Ported from the UX review's stability fuzzer ($UX/stab/src/fuzz.cpp).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "codec.h"
#include "core/support/fake_store.h"
#include "key_set.h"
#include "null_store.h"
#include "runtime.h"
#include "sim_display.h"
#include "sim_rec_port.h"
#include "support/fake_io.h"

namespace fuzz {

constexpr uint32_t kTick = 10;
constexpr int kMaxHomePresses = 6;  // docs/20 §4.5

namespace K {
constexpr uint8_t Grave = 0, Bksp = 13, Tab = 14, Q = 15, R = 18, Fn = 28, Shift = 29, S = 31, H = 35, L = 38,
                  Enter = 41, Ctrl = 42, Opt = 43, Alt = 44, N = 50, Space = 55, G0 = 56;
}

// The audio task, able to lose its end events (docs/20 S1), or to send one late: held
// until the next Start, as a slow end_session does (the task's queue is in order).
class Port : public cpasim::SimRecPort {
 public:
  int drop_end = 0;
  bool hold_end = false;  // the next end event waits for the next Start
  bool post(const cpa::rec::RecCmd& c) override;
  bool poll(cpa::rec::RecEvt& e) override;

 private:
  bool late_held_ = false;
  bool late_due_ = false;
  cpa::rec::RecEvt late_;
};

struct Dev {
  explicit Dev(cpa::Store k, cpa::Lang lang = cpa::Lang::Zh);
  void start();
  void reboot();
  void loop(cpa::KeySet keys);
  bool reboot_due() const { return reboot_at != 0 && now >= reboot_at; }
  bool recording() const { return port->engine && port->engine->recording(); }
  void mic_block(uint32_t level);

  cpa::Store kind;
  uitest::FakeTransport tx;
  testsupport::FakeStore store;
  testsupport::FakeStore cpafs;
  cpa::NullStore nul;
  uitest::FakePlatform plat;
  cpasim::SimDisplay disp;
  std::unique_ptr<Port> port;
  std::unique_ptr<cpa::Runtime> rt;
  uint32_t boot = 1, now = 0, reboot_at = 0;
  int reboots_seen = 0;
  uint32_t samples = 0;
};

struct Pull {
  bool active = false;
  std::string fid, sha, del_id, list_id, data;
  uint32_t size = 0, next = 0;
  std::map<std::string, uint32_t> gets;  // request id -> offset
  uint64_t t = 0;
};

struct Fuzz {
  // lang En (docs/21 §9.2): the device boots in English and the Mac sends ASCII only.
  explicit Fuzz(uint64_t s, cpa::Store store = cpa::Store::Sd, cpa::Lang lang = cpa::Lang::Zh);

  // fuzz_host.cpp: the daemon's side
  std::string msg(const std::string& type, const std::string& fields, bool track = true);
  std::string reply(const std::string& type, const std::string& re, const std::string& fields);
  void send(const std::string& line);
  void send_later(uint32_t ms, const std::string& line);
  void interrupt();
  void hello();
  void snapshots(bool all);
  void on_device_line(const std::string& line);
  void on_outbox_item(const std::string& id, const std::string& text);
  void on_agent(const std::string& id);
  void on_items(const std::string& re, const std::string& fid, uint32_t size, const std::string& sha);
  void on_data(const std::string& re, uint32_t off, const std::string& bytes);
  void pulled_one();
  void more_gets();
  void start_list();
  void host_tick();

  // fuzz_user.cpp: the user and the way home
  void step(cpa::KeySet keys);
  void run_ms(uint32_t ms);
  void press(cpa::KeySet k, uint32_t hold_ms = 50);
  void pk(std::initializer_list<uint8_t> l, uint32_t hold = 50);
  void type(const std::string& s);
  void hold_g0(uint32_t ms);
  void random_action();
  void act_capture();
  void act_keys(uint32_t r);
  void act_recording(uint32_t r);
  // fuzz_g0.cpp (gh #51): the grace, the double tap and hands-free memos
  void act_hands_free();
  void double_tap();
  void menu_hands_free();
  void g0_slip();
  void act_link(uint32_t r);
  void act_host(uint32_t r);
  bool at_home() const;
  int escape();
  bool switch_language();  // docs/21 §9.2: Opt+M . Enter from home shows the other language
  int escape_memo(const cpa::RecInfo& r);
  void episode_run(uint32_t ms);
  void drain();
  void fail(const std::string& what);
  void check_text(bool en_before);  // docs/21 §9.2: no Han drawn in English (ascii runs), no text off screen
  std::string where() const;

  uint32_t rnd(uint32_t n) { return n ? static_cast<uint32_t>(rng() % n) : 0; }
  bool chance(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }
  const cpa::Shell& sh() const { return dev->rt->shell(); }
  const cpa::AppState& st() const { return dev->rt->state(); }

  uint64_t seed;
  bool ascii = false;
  uint32_t han_seen = 0, frames_seen = 0;
  std::mt19937_64 rng;
  std::unique_ptr<Dev> dev;
  cpa::proto::Codec codec;
  std::vector<std::string> failures;
  uint64_t T = 0;  // simulated ms across reboots
  cpa::KeySet held;
  uint32_t mic_acc = 0;
  int token = 10000;
  // the daemon
  bool cable = true, alive = true, ready = false;
  int hid = 1;
  uint64_t last_ping = 0, hello_at = 0, list_due = 0;
  uint32_t epoch = 0;  // link interruptions: lines lost across them are expected
  struct Req {
    std::string type;
    uint64_t t;
    uint32_t epoch;
  };
  std::map<std::string, Req> reqs;  // host requests waiting for their reply
  std::vector<std::pair<uint64_t, std::string>> later;
  uint32_t revs = 0;
  int n_projects = 0;
  std::set<std::string> stored;         // outbox ids the host stored
  std::set<std::string> ever_pending;   // every outbox id the device ever held
  Pull pull;
  std::set<std::string> pulled, ever_index;
  uint32_t files_n = 0;
  bool files_live = false;
  // a meeting may only start from an Enter >= 400 ms after its box opened
  uint64_t confirm_since = 0;
  // stats
  uint64_t keys = 0, escapes = 0, max_escape = 0, memos = 0, meetings = 0, bytes_pulled = 0;
};

}  // namespace fuzz
