#include "serve.h"

#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>

#include "live.h"
#include "mic_source.h"
#include "presses.h"
#include "pty_transport.h"
#include "sim_device.h"
#include "sim_files.h"

namespace cpasim {

const char* const kServeUsage =
    "       cpa-sim serve [--store sd|internal|none] [--usb on|off] [--bat MV] [--mic silence|FILE.wav]\n"
    "                     [--shots DIR] [--scale N]\n";

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kShotWaitLoops = 50;  // a shot waits at most 500 ms for the screen
constexpr auto kTick = std::chrono::milliseconds(kTickMs);
constexpr auto kMaxLag = std::chrono::seconds(1);

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

struct ServeArgs {
  BootSpec boot;
  std::string mic = "silence";
  std::string shots = "out/serve";
  int scale = 2;
};

struct Serve {
  ServeArgs args;
  PtyTransport pty;
  std::unique_ptr<SimDevice> dev;  // after pty: it holds a reference to it
  MicSource mic;
  KeyFeed keys;
  std::deque<Step> todo;  // stdin steps not started yet
  std::string partial;    // stdin bytes after the last newline
  bool quit = false;
  uint32_t mic_ms = 0;
  int shot_wait = -1;  // loops the pending shot may still wait for the screen; -1: none
  Step shot;
  int shots = 0;
  uint32_t rx_dropped = 0;  // as last said
};

void say(const std::string& line) {
  std::fputs((line + "\n").c_str(), stdout);
  std::fflush(stdout);
}

const char* store_name(cpa::Store s) {
  return s == cpa::Store::Sd ? "sd" : (s == cpa::Store::Internal ? "internal" : "none");
}

bool parse_store(const std::string& v, cpa::Store& out) {
  for (const cpa::Store s : {cpa::Store::Sd, cpa::Store::Internal, cpa::Store::None}) {
    if (v == store_name(s)) {
      out = s;
      return true;
    }
  }
  return false;
}

bool parse_opt(ServeArgs& a, const std::string& opt, const std::string& v) {
  const long n = std::atol(v.c_str());
  if (opt == "--store") return parse_store(v, a.boot.store);
  if (opt == "--usb") {
    a.boot.usb = v == "on";
    return v == "on" || v == "off";
  }
  if (opt == "--bat") {
    a.boot.bat_mv = static_cast<uint16_t>(n);
    return n > 0 && n < 65536;
  }
  if (opt == "--scale") {
    a.scale = static_cast<int>(n);
    return n >= 1 && n <= 8;
  }
  std::string* field = opt == "--mic" ? &a.mic : (opt == "--shots" ? &a.shots : nullptr);
  if (field != nullptr) *field = v;
  return field != nullptr && !v.empty();
}

bool parse_args(int argc, char** argv, ServeArgs& a) {
  if (argc % 2 != 0) return false;
  for (int i = 0; i < argc; i += 2) {
    if (!parse_opt(a, argv[i], argv[i + 1])) return false;
  }
  return true;
}

bool load_mic(const std::string& spec, MicSource& out, std::string& err) {
  if (spec == "silence") return true;
  std::string bytes;
  if (!read_file(spec, bytes)) {
    err = "cannot read " + spec;
    return false;
  }
  const Wav w = parse_wav(bytes);
  if (!w.error.empty()) {
    err = spec + ": " + w.error;
    return false;
  }
  out = MicSource(w.pcm);
  return true;
}

// ---- stdin -------------------------------------------------------------------------

void command(Serve& s, const std::string& line) {
  const LiveCmd c = parse_live(line);
  if (c.kind == LiveKind::Quit) s.quit = true;
  if (c.kind == LiveKind::Error) say("error " + c.error);
  if (c.kind == LiveKind::Step) s.todo.push_back(c.step);
}

void read_stdin(Serve& s) {
  char buf[4096];
  const ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
  if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) s.quit = true;  // whoever drove it is gone
  if (n <= 0) return;
  s.partial.append(buf, static_cast<size_t>(n));
  for (size_t nl = s.partial.find('\n'); nl != std::string::npos; nl = s.partial.find('\n')) {
    const std::string line = s.partial.substr(0, nl);
    s.partial.erase(0, nl + 1);
    command(s, line);
  }
}

// Sleeps until t, reading stdin meanwhile.
void wait_until(Serve& s, Clock::time_point t) {
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(t - Clock::now()).count();
  pollfd p{STDIN_FILENO, POLLIN, 0};
  const int r = ::poll(&p, 1, left > 0 ? static_cast<int>(left) : 0);
  if (r > 0 && (p.revents & POLLNVAL)) s.quit = true;
  if (r > 0 && (p.revents & (POLLIN | POLLHUP))) read_stdin(s);
}

// ---- the device --------------------------------------------------------------------

void apply(Serve& s, const Step& st) {
  if (is_key_step(st.op)) return s.keys.add(st);
  if (st.op == Op::Usb) {
    s.pty.set_plugged(st.value != 0);
    return say(st.value ? "usb on" : "usb off");
  }
  if (st.op == Op::Bat) {
    s.dev->set_bat(static_cast<uint16_t>(st.value));
    return say("bat " + std::to_string(st.value));
  }
  if (st.op == Op::Shot) {
    s.shot = st;
    s.shot_wait = kShotWaitLoops;
  }
}

// Steps run in order: the next one starts once the keys before it are played and the
// shot before it is taken.
void start_steps(Serve& s) {
  while (s.keys.idle() && s.shot_wait < 0 && !s.todo.empty()) {
    const Step st = s.todo.front();
    s.todo.pop_front();
    apply(s, st);
  }
}

void finish_shot(Serve& s) {
  if (s.shot_wait < 0 || !s.keys.idle()) return;
  if (!s.dev->drawn() && --s.shot_wait > 0) return;
  s.shot_wait = -1;
  char num[8];
  std::snprintf(num, sizeof num, "%03d", ++s.shots);
  const std::string file = s.args.shots + "/" + num + "-" + safe_name(s.shot.type) + ".png";
  std::error_code ec;
  std::filesystem::create_directories(s.args.shots, ec);
  if (!write_png(file, s.dev->screen(), s.args.scale)) return say("error cannot write " + file);
  say("shot " + file + " " + s.dev->top_id() + " " + backlight_name(s.dev->backlight()));
}

void feed_mic(Serve& s) {
  s.mic_ms += kTickMs;
  if (s.mic_ms < kMicBlockMs) return;
  s.mic_ms -= kMicBlockMs;
  if (s.dev->recording()) s.dev->mic_pcm(s.mic.block(kMicBlockBytes));
}

// Host bytes beyond the 8 KB RX queue are lost, as on the device: say so once per loop.
void report_rx(Serve& s) {
  if (s.pty.rx_dropped() == s.rx_dropped) return;
  s.rx_dropped = s.pty.rx_dropped();
  say("rx_dropped " + std::to_string(s.rx_dropped));
}

void tick(Serve& s) {
  start_steps(s);
  s.dev->set_tx_room(s.pty.tx_room());
  s.dev->loop(s.keys.next());
  report_rx(s);
  feed_mic(s);
  finish_shot(s);
  if (!s.dev->reboot_due()) return;
  s.pty.reset();  // the ring and the RX buffer are RAM
  s.dev->reboot();
  say("reboot " + std::to_string(s.dev->boots()));
}

void run(Serve& s) {
  Clock::time_point next = Clock::now();
  while (!g_stop && !s.quit) {
    wait_until(s, next);
    if (Clock::now() < next) continue;
    tick(s);
    next += kTick;
    if (Clock::now() - next > kMaxLag) next = Clock::now();  // after a suspend: no burst of loops
  }
}

}  // namespace

int serve_main(int argc, char** argv) {
  Serve s;
  if (!parse_args(argc, argv, s.args)) {
    std::fprintf(stderr, "usage:%s", kServeUsage + 6);
    return 2;
  }
  std::string err;
  if (!load_mic(s.args.mic, s.mic, err)) {
    std::fprintf(stderr, "cpa-sim: %s\n", err.c_str());
    return 2;
  }
  if (!s.pty.open(err)) {
    std::fprintf(stderr, "cpa-sim: %s\n", err.c_str());
    return 1;
  }
  s.pty.set_plugged(s.args.boot.usb);
  s.dev.reset(new SimDevice(s.args.boot, &s.pty));
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::signal(SIGPIPE, SIG_IGN);
  say("pty " + s.pty.path());
  const ServeArgs& a = s.args;
  const unsigned mv = a.boot.bat_mv;
  std::fprintf(stderr, "cpa-sim serve: %s, store %s, USB %s, %u mV, mic %s; Ctrl-C stops\n", s.pty.path().c_str(),
               store_name(a.boot.store), a.boot.usb ? "on" : "off", mv, a.mic.c_str());
  run(s);
  say("bye");
  return 0;
}

}  // namespace cpasim
