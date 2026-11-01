// The level path's high-pass (docs/19 §6.6, §11 item 15; the user's device check of 2026-10-22:
// a low hum in the room, peaks near 35 and 68 Hz at -52 to -57 dBFS, kept Mando nodding through
// the pauses and drew 5 px bars with no one speaking). Every block's level, the RMS that draws
// the waveform and starts Mando's voice, is taken after a 4th-order Butterworth high-pass at
// 130 Hz (two biquads): hum 20 dB down or more, 300-3400 Hz within 1 dB. Its state runs on from
// block to block of a recording and starts afresh with the next one; the WAV keeps every byte.
#include <unity.h>

#include <cmath>
#include <string>
#include <vector>

#include "../test_rec_recovery/rig.h"
#include "level_hpf.h"
#include "text_fit.h"  // lib/theme is built whole: its canvases need module_api and core
#include "theme_levels.h"

using namespace rectest;
using testsupport::FakeRecFilePort;

void setUp() {}
void tearDown() {}

namespace {

constexpr double kRate = 16000.0;
constexpr size_t kSamples = 2048;  // a block, 128 ms
constexpr size_t kBytes = 2 * kSamples;
constexpr double kTwoPi = 6.283185307179586;

// Samples as the microphone gives them: little-endian int16.
std::string pcm_of(const std::vector<double>& v) {
  std::string s(2 * v.size(), '\0');
  for (size_t i = 0; i < v.size(); ++i) {
    const double c = std::fmax(-32768.0, std::fmin(32767.0, std::round(v[i])));
    const uint16_t u = static_cast<uint16_t>(static_cast<int16_t>(c));
    s[2 * i] = static_cast<char>(u & 0xFF);
    s[2 * i + 1] = static_cast<char>(u >> 8);
  }
  return s;
}

std::vector<double> tone(double hz, double amp, size_t n) {
  std::vector<double> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = amp * std::sin(kTwoPi * hz * static_cast<double>(i) / kRate);
  return v;
}

// The level of every block of pcm, through the high-pass from a fresh state.
std::vector<uint16_t> levels(const std::string& pcm) {
  std::vector<uint16_t> out;
  LevelHpf h;
  for (size_t at = 0; at < pcm.size(); at += kBytes) {
    const size_t n = pcm.size() - at < kBytes ? pcm.size() - at : kBytes;
    const LevelOut o = level_of(h, u8(pcm) + at, n);
    h = o.hpf;
    out.push_back(o.rms);
  }
  return out;
}

// The gain at hz: 2 s of a tone at RMS 7071, measured over its last second (the start passed).
double gain_db(double hz) {
  const std::vector<uint16_t> lv = levels(pcm_of(tone(hz, 10000.0, 16 * kSamples)));
  double power = 0;
  for (size_t i = 8; i < lv.size(); ++i) power += static_cast<double>(lv[i]) * lv[i];
  return 10.0 * std::log10(power / 8.0 / (10000.0 * 10000.0 / 2.0));
}

uint16_t raw_rms(const std::string& pcm, size_t at, size_t n) {
  double sum = 0;
  for (size_t i = at; i + 1 < at + n; i += 2) {
    const double s = static_cast<int16_t>(static_cast<uint8_t>(pcm[i]) | (static_cast<uint8_t>(pcm[i + 1]) << 8));
    sum += s * s;
  }
  return static_cast<uint16_t>(std::sqrt(sum / static_cast<double>(n / 2)));
}

// The room's hum at -52 dBFS (RMS 82): the device check's peaks, equal parts, or all at 68 Hz.
struct Hum {
  double lo_amp, hi_amp;  // the 35 Hz and 68 Hz tones' amplitudes
};
constexpr double kHumRms = 82.38;  // 32768 x 10^(-52/20)
constexpr Hum kHums[] = {{kHumRms, kHumRms}, {0.0, kHumRms * 1.41421356}};

// A voice: harmonics of 150 Hz up to 3.3 kHz falling 6 dB an octave, in syllables of 250 ms
// that rise from nothing and fall back to it; about -38 dBFS, near the memos' median word.
double voice(size_t i) {
  const double t = static_cast<double>(i) / kRate;
  double s = 0;
  for (int k = 1; k <= 22; ++k) s += std::sin(kTwoPi * 150.0 * k * t) / k;
  return 652.0 * std::sin(kTwoPi * 0.5 * std::fmod(t, 0.25) / 0.25) * s;
}

// A memo in the hum: 1 s of the room, then 2 s of words and a 2 s pause, three times.
constexpr size_t kLead = 16000;
constexpr size_t kTalk = 32000;
constexpr size_t kPause = 32000;
constexpr int kTurns = 3;

bool talking(size_t i) { return i >= kLead && (i - kLead) % (kTalk + kPause) < kTalk; }

std::string memo_in(const Hum& hum) {
  std::vector<double> v(kLead + kTurns * (kTalk + kPause));
  for (size_t i = 0; i < v.size(); ++i) {
    const double t = static_cast<double>(i) / kRate;
    v[i] = hum.lo_amp * std::sin(kTwoPi * 35.0 * t) + hum.hi_amp * std::sin(kTwoPi * 68.0 * t);
    if (talking(i)) v[i] += voice(i - kLead);
  }
  return pcm_of(v);
}

StartSpec memo_spec() {
  StartSpec s;
  s.mode = RecMode::Memo;
  return s;
}

// One recording of pcm through the engine, a block every 128 ms: the WAV's PCM and the levels.
struct Recorded {
  std::string pcm;
  std::vector<uint16_t> levels;
};

Recorded record(Engine& e, FakeRecFilePort& fs, const std::string& pcm) {
  Recorded out;
  uint32_t now = 5000;
  const size_t before = e.sink.events.size();
  e.engine->start(memo_spec(), now, now, e.sink);
  const MonoId id = e.sink.last(EvtKind::Started)->id;
  for (size_t at = 0; at < pcm.size(); at += kBytes, now += 128) {
    const size_t n = pcm.size() - at < kBytes ? pcm.size() - at : kBytes;
    e.engine->write(u8(pcm) + at, n, now, e.sink);
  }
  e.engine->stop(e.sink);
  for (size_t i = before; i < e.sink.events.size(); ++i) {
    if (e.sink.events[i].kind == EvtKind::Level) out.levels.push_back(e.sink.events[i].level);
  }
  out.pcm = fs.get(wav_of(id)).substr(kHeaderBytes);
  return out;
}

}  // namespace

// --- the high-pass ---------------------------------------------------------------------------

static void test_the_hum_is_20_db_down() {
  for (const double hz : {35.0, 50.0, 60.0, 68.0}) {
    const double g = gain_db(hz);
    TEST_ASSERT_TRUE_MESSAGE(g <= -20.0, std::to_string(hz).c_str());
  }
}

static void test_the_voice_band_passes_within_1_db() {
  for (const double hz : {300.0, 440.0, 1000.0, 2000.0, 3400.0}) {
    const double g = gain_db(hz);
    TEST_ASSERT_TRUE_MESSAGE(g >= -1.0 && g <= 1.0, std::to_string(hz).c_str());
  }
}

// The state runs on: a block cut in two gives the same state as the whole, and the next
// block's level does not depend on where the blocks were cut.
static void test_the_state_runs_on_from_block_to_block() {
  const std::string pcm = pcm_of(tone(68.0, 3000.0, 3 * kSamples));
  const LevelOut whole = level_of(LevelHpf{}, u8(pcm), kBytes);
  const LevelOut half = level_of(LevelHpf{}, u8(pcm), kBytes / 2);
  const LevelOut rest = level_of(half.hpf, u8(pcm) + kBytes / 2, kBytes / 2);
  for (int k = 0; k < 4; ++k) TEST_ASSERT_EQUAL_FLOAT(whole.hpf.z[k], rest.hpf.z[k]);
  const LevelOut next = level_of(whole.hpf, u8(pcm) + kBytes, kBytes);
  TEST_ASSERT_EQUAL_UINT16(next.rms, level_of(rest.hpf, u8(pcm) + kBytes, kBytes).rms);
  TEST_ASSERT_TRUE(next.rms < 3000 / 10);  // carried: no start-up step in the second block
  TEST_ASSERT_EQUAL_UINT16(0, level_of(LevelHpf{}, u8(pcm), 1).rms);  // no whole sample
  TEST_ASSERT_EQUAL_UINT16(0, level_of(LevelHpf{}, nullptr, 0).rms);
}

// A square wave at 8 kHz passes whole: its RMS is its amplitude, also at full scale.
static void test_the_top_of_the_band_passes_whole() {
  for (const int16_t amp : {500, 3000, 32767}) {
    std::vector<double> v(2 * kSamples);
    for (size_t i = 0; i < v.size(); ++i) v[i] = i % 2 == 0 ? amp : -amp;
    TEST_ASSERT_EQUAL_UINT16(amp, levels(pcm_of(v)).back());
  }
}

// --- the engine ------------------------------------------------------------------------------

// The WAV keeps every byte; the levels are the high-passed ones, the state carried through
// the recording and started afresh with the next (the same audio, the same levels).
static void test_the_wav_keeps_its_bytes_and_the_levels_are_high_passed() {
  FakeRecFilePort fs;
  Engine e(fs);
  const std::string pcm = memo_in(kHums[0]);
  const Recorded a = record(e, fs, pcm);
  TEST_ASSERT_TRUE(a.pcm == pcm);
  const std::vector<uint16_t> want = levels(pcm);
  TEST_ASSERT_EQUAL_UINT(want.size(), a.levels.size());
  for (size_t i = 0; i < want.size(); ++i) TEST_ASSERT_EQUAL_UINT16(want[i], a.levels[i]);
  const Recorded b = record(e, fs, pcm);
  TEST_ASSERT_TRUE(b.pcm == pcm);
  TEST_ASSERT_TRUE(b.levels == a.levels);
}

// The device check: the hum at -52 dBFS, words, and 2 s pauses. Unfiltered, a pause block is
// a 5 px bar and keeps the voice (above -56 dBFS); high-passed, the voice ends within 2 blocks
// of each pause and every block of the pause is a dot. Through the words he nods.
static void test_the_hum_neither_holds_the_voice_nor_draws_bars() {
  for (const Hum& hum : kHums) {
    FakeRecFilePort fs;
    Engine e(fs);
    const std::string pcm = memo_in(hum);
    const Recorded r = record(e, fs, pcm);
    const uint8_t raw = theme::level_q(raw_rms(pcm, 4 * kBytes, kBytes));  // a block of the lead
    TEST_ASSERT_TRUE(raw > theme::kVoiceHold);
    TEST_ASSERT_EQUAL_INT16(2, theme::level_half(raw, 11));  // a 5 px bar
    theme::LevelHistory h;
    for (size_t b = 0; b < r.levels.size(); ++b) {
      h = theme::level_push(h, r.levels[b], 0);
      const size_t first = b * kSamples, last = first + kSamples - 1;
      const bool words = b > 0 && talking(first) && talking(last);
      const bool quiet = !talking(first) && !talking(last);
      const bool settled = quiet && (first < kLead || (first >= kSamples && !talking(first - kSamples)));
      if (words) TEST_ASSERT_TRUE_MESSAGE(theme::level_voice(h), "words: a voice");
      if (quiet) TEST_ASSERT_EQUAL_INT16_MESSAGE(1, theme::level_half(h.wave[h.wave_n - 1], 11), "a dot");
      if (settled) TEST_ASSERT_FALSE_MESSAGE(theme::level_voice(h), "the pause's second block: no voice");
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_hum_is_20_db_down);
  RUN_TEST(test_the_voice_band_passes_within_1_db);
  RUN_TEST(test_the_state_runs_on_from_block_to_block);
  RUN_TEST(test_the_top_of_the_band_passes_whole);
  RUN_TEST(test_the_wav_keeps_its_bytes_and_the_levels_are_high_passed);
  RUN_TEST(test_the_hum_neither_holds_the_voice_nor_draws_bars);
  return UNITY_END();
}
