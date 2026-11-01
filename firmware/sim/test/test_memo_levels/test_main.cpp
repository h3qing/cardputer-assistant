// A memo at the levels of the user's own (docs/19 §6.6, §7.1; the user's report of 2026-10-22:
// the waveform did not move up and down with the voice). The device holds G0 and its microphone
// plays one of the memos block by block (app/test/core/support/memo_levels.h): each 128 ms block
// is a 440 Hz tone at that block's high-passed level (SimDevice::mic_block; the level path's
// 130 Hz high-pass lets a 440 Hz tone through whole), so the level history sees what it sees
// on the device, and no audio of the user is in the repository. On the panel after
// every animation tick: through the words the newest bar is a mid-to-high one and the bars
// differ, Mando nods; in the pauses the newest bars are dots and he tilts his head.
#include <unity.h>

#include <set>
#include <vector>

#include "core/support/memo_levels.h"
#include "fb.h"
#include "fb_canvas.h"
#include "presses.h"
#include "script.h"
#include "sim_device.h"
#include "theme_mando.h"

using namespace cpasim;

void setUp() {}
void tearDown() {}

namespace {

constexpr int kNewestX = 62 + 5 * 34 + 1;  // the middle column of the waveform's newest bar
constexpr int kWaveTop = 63;
constexpr int kWaveH = 25;
constexpr int kMandoX = 10;
constexpr int kMandoY = 19;

struct Shown {
  Frame screen{1, 1};
  uint16_t block = 0;  // the newest block the tick drew
};

// Boot, hold G0 (the memo's cue), and play the memo's blocks with G0 held; the panel after
// every animation tick (every 200 ms: what it pushed), with the newest block it drew.
std::vector<Shown> recorded(const memo_levels::Memo& m) {
  SimDevice dev{BootSpec{}};
  cpa::KeySet g0;
  TEST_ASSERT_TRUE(parse_combo("g0", g0));
  for (uint32_t t = 0; t < 1200; t += kTickMs) dev.loop(cpa::KeySet{});
  for (uint32_t t = 0; t < 700; t += kTickMs) dev.loop(g0);
  TEST_ASSERT_TRUE(dev.recording());
  std::vector<Shown> out;
  uint32_t since = kMicBlockMs;
  uint16_t fed = 0;
  while (fed < m.n) {
    const uint32_t partial = dev.pushed().partial;
    dev.loop(g0);  // the blocks fed so far are in the level history now
    if (fed > 0 && dev.pushed().partial != partial) out.push_back(Shown{dev.screen(), static_cast<uint16_t>(fed - 1)});
    since += kTickMs;
    if (since < kMicBlockMs) continue;
    since -= kMicBlockMs;
    dev.mic_block(m.level[fed++]);
  }
  return out;
}

int newest_bar_h(const Frame& f) {
  const uint8_t bg = entry_of(cpa::Color::Bg);
  int h = 0;
  for (int y = kWaveTop; y < kWaveTop + kWaveH; ++y) h += f.at(kNewestX, y) != bg ? 1 : 0;
  return h;
}

// Mando's 64 px sprite as the page draws it, for one act and pose.
Frame mando_ref(cpa::theme::MandoAct act, uint8_t pose) {
  Frame f(cpa::kScreenW, cpa::kScreenH, entry_of(cpa::Color::Bg));
  FbCanvas c(f);
  cpa::theme::mando(c, kMandoX, kMandoY, cpa::theme::MandoSize::M64, act, pose, cpa::Color::Bg);
  return f;
}

bool same_sprite(const Frame& a, const Frame& b) {
  const cpa::Rect r = cpa::theme::mando_sprite(cpa::theme::MandoSize::M64, kMandoX, kMandoY);
  for (int y = r.y; y < r.y + r.h; ++y) {
    for (int x = r.x; x < r.x + r.w; ++x) {
      if (a.at(x, y) != b.at(x, y)) return false;
    }
  }
  return true;
}

// Listen (he nods) or Tilt, by his pose on the panel; Idle when it is neither.
cpa::theme::MandoAct act_of(const Frame& f) {
  for (const cpa::theme::MandoAct act : {cpa::theme::MandoAct::Listen, cpa::theme::MandoAct::Tilt}) {
    for (uint8_t p = 0; p < cpa::theme::mando_poses(act); ++p) {
      if (same_sprite(f, mando_ref(act, p))) return act;
    }
  }
  return cpa::theme::MandoAct::Idle;
}

bool word(const memo_levels::Memo& m, uint16_t i) { return i > 0 && m.rms[i] >= memo_levels::kSpeech; }

// The room for two blocks: no voice any more.
bool room(const memo_levels::Memo& m, uint16_t i) {
  return i > 0 && m.rms[i] < memo_levels::kRoom && m.rms[i - 1] < memo_levels::kRoom;
}

}  // namespace

static void test_the_waveform_moves_with_the_words() {
  const memo_levels::Memo& m = memo_levels::kMemos[2];
  const std::vector<Shown> frames = recorded(m);
  TEST_ASSERT_TRUE(frames.size() > 50);
  std::set<int> heights;
  int rooms = 0;
  for (const Shown& f : frames) {
    const int h = newest_bar_h(f.screen);
    if (word(m, f.block)) {
      heights.insert(h);
      TEST_ASSERT_TRUE_MESSAGE(h >= 9, "a word is a bar");
    }
    if (room(m, f.block)) {
      ++rooms;
      TEST_ASSERT_EQUAL_MESSAGE(3, h, "the room is a dot");
    }
  }
  TEST_ASSERT_TRUE(rooms >= 4);
  TEST_ASSERT_TRUE(heights.size() >= 4);  // up and down
  TEST_ASSERT_TRUE(*heights.rbegin() >= 17 && *heights.rbegin() < 23);
}

static void test_mando_nods_through_the_words_and_tilts_in_the_pauses() {
  const memo_levels::Memo& m = memo_levels::kMemos[2];
  int words = 0, nods = 0, rooms = 0, tilts = 0;
  for (const Shown& f : recorded(m)) {
    const cpa::theme::MandoAct act = act_of(f.screen);
    if (word(m, f.block)) {
      ++words;
      nods += act == cpa::theme::MandoAct::Listen ? 1 : 0;
    }
    if (room(m, f.block)) {
      ++rooms;
      tilts += act == cpa::theme::MandoAct::Tilt ? 1 : 0;
    }
  }
  TEST_ASSERT_TRUE(words > 30);
  TEST_ASSERT_EQUAL(words, nods);
  TEST_ASSERT_EQUAL(rooms, tilts);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_waveform_moves_with_the_words);
  RUN_TEST(test_mando_nods_through_the_words_and_tilts_in_the_pauses);
  return UNITY_END();
}
