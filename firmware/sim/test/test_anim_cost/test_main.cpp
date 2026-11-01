// What motion costs (docs/19 §7.5, S3). The SPI work is deterministic and asserted: while a
// memo or a meeting records at Backlight::On the panel gets one full frame a second and, each
// 200 ms tick, Mando's sprite, the waveform or the grown timeline and the 12 x 12 ●, and a full
// frame for the daemon's ping every 5 s (its last_rx_ms): at 16 bits a pixel and 40 MHz that is
// under 32 ms of SPI a second (v0.4.0 pushed a full frame for every audio level: about 8 a
// second, 104 ms). The CPU time is measured on the host and printed for
// the review (pio test -v): the loops that pushed a full frame, those that pushed a tick's
// rectangles and those that pushed nothing, the same runtime code the device runs. gh #53:
// also with nine projects of 12-letter keys, the pages' legend at its fullest.
#include <unity.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "player.h"
#include "script.h"
#include "sim_device.h"
#include "sim_files.h"

using namespace cpasim;

void setUp() {}
void tearDown() {}

namespace {

constexpr double kSpiHz = 40e6;  // the panel's SPI write clock
constexpr double kPushUs = 30;   // a push's own overhead (address window, DMA setup)

PlayResult run(const std::string& text, const LoopHook& hook = LoopHook{}) {
  const Script s = parse_script("cost", [&](const std::string& path, std::string& out) {
    if (path == "cost") {
      out = text;
      return true;
    }
    return read_file(path, out);
  });
  TEST_ASSERT_EQUAL_STRING("", s.error.c_str());
  return play(s.steps, hook);
}

// Pixels pushed per second over the second shot's span, and the SPI ms that takes.
double spi_ms_per_s(const Shot& s, double seconds) {
  const double px = s.pushes.full * 240.0 * 135.0 + s.pushes.partial_px;
  const double us = px * 16 / kSpiHz * 1e6 + (s.pushes.full + s.pushes.partial) * kPushUs;
  return us / 1000.0 / seconds;
}

const char* kMemo =
    "boot\ninclude scripts/common/demo_state.cpas\ndown g0\nwait 700\nmic 1000 speech\nshot a\n"
    "mic 4000 speech\nshot b\nup g0\n";
const char* kMeeting =
    "boot\ninclude scripts/common/demo_state.cpas\npress opt+r\nwait 500\npress enter\nmic 1000 speech\n"
    "shot a\nmic 4000 speech\nshot b\n";
// Past the first merge (94 blocks, 12 s): the cells spread over the whole width, so each new
// one moves every bar (the costliest stretch of a meeting: a cell every 256 ms).
const char* kLongMeeting =
    "boot\ninclude scripts/common/demo_state.cpas\npress opt+r\nwait 500\npress enter\nmic 13000 speech\n"
    "shot a\nmic 4000 speech\nshot b\n";

// gh #53: the recording pages' legend at its fullest, nine 12-letter keys (the memo's three
// rows lower the waveform; the meeting's two take the timeline's labels' row).
const char* kMemoNine =
    "boot\ninclude scripts/common/demo_state.cpas\ninclude scripts/common/nine_projects.cpas\ndown g0\nwait 700\n"
    "mic 1000 speech\nshot a\nmic 4000 speech\nshot b\nup g0\n";
const char* kMeetingNine =
    "boot\ninclude scripts/common/demo_state.cpas\ninclude scripts/common/nine_projects.cpas\npress opt+r\nwait 500\n"
    "press enter\nmic 13000 speech\nshot a\nmic 4000 speech\nshot b\n";

struct Run {
  const char* name;
  const char* script;
};

double median(std::vector<double> v) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

}  // namespace

static void test_recording_spi_stays_under_32_ms_a_second() {
  const Run kRuns[] = {{"memo", kMemo},
                       {"meeting", kMeeting},
                       {"meeting past 12 s", kLongMeeting},
                       {"memo, nine projects", kMemoNine},
                       {"meeting past 12 s, nine projects", kMeetingNine}};
  for (const Run& run_of : kRuns) {
    const PlayResult r = run(run_of.script);
    TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
    const Shot& b = r.shots[1];
    const double ms = spi_ms_per_s(b, (b.at_ms - r.shots[0].at_ms) / 1000.0);
    std::printf("SPI while recording (%s): %u full + %u partial (%u px) in %.1f s: %.1f ms/s\n", run_of.name,
                static_cast<unsigned>(b.pushes.full),
                static_cast<unsigned>(b.pushes.partial), static_cast<unsigned>(b.pushes.partial_px),
                (b.at_ms - r.shots[0].at_ms) / 1000.0, ms);
    TEST_ASSERT_TRUE(b.pushes.full <= 5 && b.pushes.partial >= 2 * 4 * 5);  // a full frame a second
    TEST_ASSERT_TRUE(ms < 32.0);
  }
}

struct LoopTimes {
  std::vector<double> full, tick, idle;  // microseconds
};

// Host time of every loop of a script, by what the loop pushed.
LoopTimes loop_times(const std::string& script) {
  LoopTimes t;
  Pushes last;
  auto at = std::chrono::steady_clock::now();
  const PlayResult r = run(script, [&](const SimDevice& d) {
    const auto now = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(now - at).count();
    const Pushes& p = d.pushed();
    std::vector<double>& into = p.full != last.full ? t.full : (p.partial != last.partial ? t.tick : t.idle);
    into.push_back(us);
    last = p;
    at = std::chrono::steady_clock::now();
  });
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());
  return t;
}

double top(std::vector<double> v) { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }

// Printed for the review (host times vary); asserted: a tick's loop costs less than a full one.
static void test_host_time_per_loop() {
  const Run kRuns[] = {{"memo", kMemo},
                       {"meeting", kMeeting},
                       {"motion", "include scripts/theme_motion.cpas\n"},
                       {"memo, nine projects", kMemoNine},
                       {"meeting, nine projects", kMeetingNine}};
  for (const auto& run_of : kRuns) {
    const LoopTimes t = loop_times(run_of.script);
    std::printf("host loop (%s), median / max us: full frame %.1f / %.1f (n=%u), tick %.1f / %.1f (n=%u), "
                "nothing %.1f (n=%u)\n",
                run_of.name, median(t.full), top(t.full), static_cast<unsigned>(t.full.size()), median(t.tick),
                top(t.tick), static_cast<unsigned>(t.tick.size()), median(t.idle), static_cast<unsigned>(t.idle.size()));
    TEST_ASSERT_TRUE(t.tick.size() >= 20 && t.full.size() >= 4);
    TEST_ASSERT_TRUE(median(t.tick) < median(t.full));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_recording_spi_stays_under_32_ms_a_second);
  RUN_TEST(test_host_time_per_loop);
  return UNITY_END();
}
