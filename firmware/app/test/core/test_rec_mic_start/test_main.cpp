// The muted start of the microphone (issue #31, docs/11 §4.2): after a codec power-on the
// ES8311 holds its ADC output muted for about 1 s. RecEngine::arm() waits for the first
// block that is not muted and opens the session at its first live sample; nothing is
// written and Started (● REC) is not emitted before. After kMuteWaitMs without one it
// opens anyway and counts it. The session's clock (at.up_ms, ★, lead_ms, segment bytes)
// starts at the first written sample.
#include <unity.h>

#include <string>

#include "../test_rec_recovery/cuts.h"
#include "mic_start.h"

using namespace rectest;
using testsupport::FakeRecFilePort;

void setUp() {}
void tearDown() {}

namespace {

constexpr size_t kSamples = kBlock / 2;

// n samples of value v, little-endian int16.
std::string flat(size_t n, int16_t v) {
  std::string s(2 * n, '\0');
  for (size_t i = 0; i < n; ++i) {
    s[2 * i] = static_cast<char>(static_cast<uint16_t>(v) & 0xFF);
    s[2 * i + 1] = static_cast<char>(static_cast<uint16_t>(v) >> 8);
  }
  return s;
}

int16_t sample_at(const std::string& s, size_t i) {
  return static_cast<int16_t>(static_cast<uint8_t>(s[2 * i]) | (static_cast<uint8_t>(s[2 * i + 1]) << 8));
}

// Room noise: every sample far from 0 (pcm() gives uniform 16-bit values).
std::string live(size_t n, uint32_t seed) { return pcm(2 * n, seed); }

bool muted(const std::string& b) { return block_muted(u8(b), b.size()); }

StartSpec memo() {
  StartSpec s;
  s.mode = RecMode::Memo;
  return s;
}

// Muted blocks at 128 ms steps from now (nothing may be written).
void feed_flat(Engine& e, int blocks, uint32_t& now, int16_t v = 0) {
  const std::string b = flat(kSamples, v);
  for (int i = 0; i < blocks; ++i) {
    e.engine->write(u8(b), b.size(), now, e.sink);
    now += 128;
  }
}

// M5Unified 0.2.26's DC offset correction (Mic_Class.inl:786-792, mono): one step per pair
// of samples, and its offset survives Mic.end and Mic.begin. After a capture that settled on
// a DC of dc, a cold codec's exact zeros come out as a tail from about -dc decaying towards
// 0 (64 x ln(|dc| / 1.5) samples), then a constant -1, 0 or +1.
struct Servo {
  int32_t offset = 0;
  // n samples of the codec's constant output `in`, as the ring receives them.
  std::string run(size_t n, int16_t in) {
    std::string s;
    for (size_t i = 0; i < n; i += 2) {
      offset -= (16 * in + offset + 16) >> 5;
      s += flat(2, static_cast<int16_t>(in + ((offset + 8) >> 4)));
    }
    return s;
  }
};

// The offset a cold start within a boot inherits (debug.codec, a partial warm-up).
Servo carried(int16_t dc) {
  Servo m;
  m.run(16000, dc);  // 1 s of a capture with a DC of dc
  return m;
}

}  // namespace

// --- the block test --------------------------------------------------------------------------

static void test_zero_and_dc_offset_blocks_are_muted() {
  TEST_ASSERT_TRUE(muted(flat(kSamples, 0)));
  TEST_ASSERT_TRUE(muted(flat(kSamples, 1)));   // M5Unified's DC offset correction settled at +1
  TEST_ASSERT_TRUE(muted(flat(kSamples, -2)));
  TEST_ASSERT_FALSE(muted(flat(kSamples, 3)));  // a constant above kMutedMax is a signal
  TEST_ASSERT_FALSE(muted(flat(kSamples, -3)));
}

// The first block of a cold capture: the correction's tail (35 down to 2) and then +1.
static void test_a_decaying_offset_tail_is_still_muted() {
  std::string b;
  for (int v = 35; v > 2; --v) b += flat(6, static_cast<int16_t>(v));  // 198 samples
  b += flat(kSamples - b.size() / 2, 1);
  TEST_ASSERT_TRUE(muted(b));
}

// The tail is not part of the 90 %: whatever DC the codec had, the cold block is muted
// (a tail from 38 already holds more than 10 % of the block).
static void test_a_carried_offset_tail_of_any_length_is_muted() {
  for (const int16_t dc : {40, 60, 80, 400, -40, -60, -80, -400}) {
    Servo m = carried(dc);
    const std::string b = m.run(kSamples, 0);
    const int16_t first = sample_at(b, 0);  // the tail starts beyond kMutedMax, opposite to dc
    TEST_ASSERT_TRUE_MESSAGE(dc > 0 ? first < -kMutedMax : first > kMutedMax, std::to_string(dc).c_str());
    TEST_ASSERT_TRUE_MESSAGE(muted(b), std::to_string(dc).c_str());
  }
}

// A short leftover mute (a partial warm-up): tail, mute and live audio in one block. The
// lead skips the tail with the mute; a decaying start that does not reach the mute is live.
static void test_the_muted_lead_skips_a_carried_offset_tail() {
  for (const int16_t dc : {35, 80, -80}) {
    Servo m = carried(dc);
    const std::string b = m.run(1200, 0) + live(kSamples - 1200, 11);
    TEST_ASSERT_FALSE_MESSAGE(muted(b), std::to_string(dc).c_str());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2400, muted_lead(u8(b), kBlock), std::to_string(dc).c_str());
  }
  const std::string decaying = flat(4, 300) + flat(4, 200) + live(kSamples - 8, 12);
  TEST_ASSERT_EQUAL_UINT(0, muted_lead(u8(decaying), kBlock));
  // A 1 kHz tone from its peak into an exact zero falls faster than the tail ever does.
  std::string tone;
  for (const int16_t v : {3277, 3027, 2317, 1254, 0}) tone += flat(1, v);
  tone += live(kSamples - 5, 13);
  TEST_ASSERT_EQUAL_UINT(0, muted_lead(u8(tone), kBlock));
}

static void test_ninety_percent_of_one_value_is_the_boundary() {
  const size_t at_least = (kSamples * 9 + 9) / 10;  // 1844 of 2048
  TEST_ASSERT_TRUE(muted(flat(at_least, 0) + live(kSamples - at_least, 3)));
  TEST_ASSERT_FALSE(muted(flat(at_least - 1, 0) + live(kSamples - at_least + 1, 3)));
}

static void test_room_noise_and_a_spread_of_small_values_are_not_muted() {
  TEST_ASSERT_FALSE(muted(live(kSamples, 4)));
  std::string spread;  // -2..2 in turn: quiet, but no single value holds 90 %
  for (size_t i = 0; i < kSamples; ++i) spread += flat(1, static_cast<int16_t>(static_cast<int>(i % 5) - 2));
  TEST_ASSERT_FALSE(muted(spread));
  TEST_ASSERT_FALSE(block_muted(nullptr, 0));
}

static void test_the_muted_lead_ends_at_the_first_louder_sample() {
  TEST_ASSERT_EQUAL_UINT(2000, muted_lead(u8(flat(1000, 0) + live(1048, 5)), kBlock));
  TEST_ASSERT_EQUAL_UINT(1200, muted_lead(u8(flat(600, -1) + live(1448, 6)), kBlock));
  TEST_ASSERT_EQUAL_UINT(0, muted_lead(u8(live(kSamples, 7)), kBlock));
  std::string quiet;  // nothing louder than kMutedMax at all: a quiet block, kept whole
  for (size_t i = 0; i < kSamples; ++i) quiet += flat(1, static_cast<int16_t>(static_cast<int>(i % 5) - 2));
  TEST_ASSERT_EQUAL_UINT(0, muted_lead(u8(quiet), kBlock));
}

// --- the engine ------------------------------------------------------------------------------

// The cold start of issue #31: 7 muted blocks, then a block whose first 1000 samples are
// still muted. Only the live samples reach the card, Started comes with them, and the
// session's up_ms is the first written sample's (5000 + 15,336 samples / 16 = 5958 ms).
static void test_a_muted_start_opens_at_the_first_live_sample() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 5000;
  e.engine->arm(memo(), 5000, now);
  feed_flat(e, 7, now);
  TEST_ASSERT_TRUE(e.engine->recording() && e.engine->waiting());
  TEST_ASSERT_EQUAL_UINT(0, e.sink.events.size());  // no Started, not even a Level
  TEST_ASSERT_EQUAL_UINT(0, fs.file_count());
  const std::string first = flat(1000, 0) + live(1048, 9);
  e.engine->write(u8(first), first.size(), now, e.sink);
  now += 128;
  TEST_ASSERT_NOT_NULL(e.sink.last(EvtKind::Started));
  const RecEvt started = *e.sink.last(EvtKind::Started);
  TEST_ASSERT_EQUAL_UINT32(5958, started.up_ms);
  TEST_ASSERT_FALSE(e.engine->waiting());
  const std::string rest = feed(e, 10, now);
  e.engine->stop(e.sink);
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Saved));
  const Opt<RecMeta> m = read_meta(fs, started.id);
  TEST_ASSERT_TRUE(m.has && m.value.at.has && m.value.at.value.up_ms == 5958);
  TEST_ASSERT_TRUE(m.value.lead_ms.has && m.value.lead_ms.value == kLeadMs);
  TEST_ASSERT_EQUAL_UINT32(2096 + rest.size(), m.value.bytes);
  TEST_ASSERT_TRUE(fs.get(wav_of(started.id)).substr(44) == first.substr(2000) + rest);
  TEST_ASSERT_EQUAL_UINT32(0, e.engine->mute_timeouts());
}

// The same cold start within a boot (rec_cold_start's cold take): the capture before left
// an offset for a DC of 80, so block 0 starts with its tail. It is dropped like the rest.
static void test_a_muted_start_with_a_carried_offset_opens_at_the_first_live_sample() {
  FakeRecFilePort fs;
  Engine e(fs);
  Servo m = carried(80);
  uint32_t now = 5000;
  e.engine->arm(memo(), 5000, now);
  for (int i = 0; i < 7; ++i, now += 128) {
    const std::string b = m.run(kSamples, 0);
    e.engine->write(u8(b), b.size(), now, e.sink);
  }
  TEST_ASSERT_EQUAL_UINT(0, e.sink.events.size());
  const std::string first = m.run(1000, 0) + live(1048, 9);
  e.engine->write(u8(first), first.size(), now, e.sink);
  now += 128;
  TEST_ASSERT_NOT_NULL(e.sink.last(EvtKind::Started));
  const RecEvt started = *e.sink.last(EvtKind::Started);
  TEST_ASSERT_EQUAL_UINT32(5958, started.up_ms);
  const std::string rest = feed(e, 10, now);
  e.engine->stop(e.sink);
  TEST_ASSERT_TRUE(fs.get(wav_of(started.id)).substr(44) == first.substr(2000) + rest);
  TEST_ASSERT_EQUAL_UINT32(0, e.engine->mute_timeouts());
}

// A warm codec: the first block is live (the Mic.begin transient), written whole, at once.
static void test_a_live_first_block_starts_at_once() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 100;
  e.engine->arm(memo(), 90, now);
  TEST_ASSERT_EQUAL_UINT(0, e.sink.count(EvtKind::Started));
  const std::string data = feed(e, 10, now);
  e.engine->stop(e.sink);
  const RecEvt* started = e.sink.last(EvtKind::Started);
  TEST_ASSERT_NOT_NULL(started);
  TEST_ASSERT_EQUAL_UINT32(90, started->up_ms);
  TEST_ASSERT_TRUE(fs.get(wav_of(started->id)).substr(44) == data);
  TEST_ASSERT_EQUAL_UINT32(0, e.engine->mute_timeouts());
}

// ★ counts from the first written sample: 3 muted blocks are not in it.
static void test_stars_count_from_the_first_written_sample() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  StartSpec s = meeting_spec();
  e.engine->arm(s, 0, now);
  TEST_ASSERT_FALSE(e.engine->mark());  // nothing written yet: no star
  feed_flat(e, 3, now, -1);
  feed(e, 8, now);  // 8 x 128 ms written
  TEST_ASSERT_TRUE(e.engine->mark());
  feed(e, 8, now);
  e.engine->stop(e.sink);
  const Opt<RecMeta> m = read_meta(fs, e.sink.last(EvtKind::Started)->id);
  TEST_ASSERT_TRUE(m.has);
  TEST_ASSERT_EQUAL_UINT8(1, m.value.n_stars);
  TEST_ASSERT_EQUAL_UINT32(1024, m.value.stars[0]);
  TEST_ASSERT_TRUE(m.value.consent && m.value.at.value.up_ms == 384);
}

// B5 while the codec is still muted: the session is discarded, nothing is on the card.
static void test_a_stop_while_muted_discards() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  e.engine->arm(memo(), 0, now);
  feed_flat(e, 4, now);
  e.engine->stop(e.sink);
  TEST_ASSERT_EQUAL_UINT(1, e.sink.events.size());
  TEST_ASSERT_TRUE(e.sink.events[0].kind == EvtKind::Discarded);
  TEST_ASSERT_FALSE(e.engine->recording() || e.engine->waiting());
  TEST_ASSERT_EQUAL_UINT(0, fs.file_count());
  feed(e, 2, now);  // blocks still in the ring after the Stop are not written
  TEST_ASSERT_EQUAL_UINT(1, e.sink.events.size());
}

// A tag pressed in a meeting's cue (RecCore allows it) reaches segment 0's .M.
static void test_a_tag_while_muted_reaches_the_meta() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  e.engine->arm(meeting_spec(), 0, now);
  feed_flat(e, 2, now);
  e.engine->tag(some(ProjectKey::from("cpa")), 1);
  feed(e, 10, now);
  e.engine->stop(e.sink);
  const Opt<RecMeta> m = read_meta(fs, e.sink.last(EvtKind::Started)->id);
  TEST_ASSERT_TRUE(m.has && m.value.project.has && m.value.project.value.equals("cpa"));
}

// A broken microphone must not block recording: 2 s of muted blocks, then the session
// opens with the muted block that crossed the wait (written whole) and counts it.
static void test_two_seconds_of_muted_blocks_open_anyway_and_count() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 1000;
  e.engine->arm(memo(), 1000, now);
  feed_flat(e, 16, now);  // the last one at 1000 + 15 x 128 = 2920 ms: still waiting
  TEST_ASSERT_TRUE(e.engine->waiting());
  feed_flat(e, 1, now);  // at 3048 ms: 2048 ms waited
  TEST_ASSERT_NOT_NULL(e.sink.last(EvtKind::Started));
  const RecEvt started = *e.sink.last(EvtKind::Started);
  TEST_ASSERT_EQUAL_UINT32(1000 + 16 * 128, started.up_ms);
  TEST_ASSERT_EQUAL_UINT32(1, e.engine->mute_timeouts());
  feed(e, 8, now);
  e.engine->stop(e.sink);
  const Opt<RecMeta> m = read_meta(fs, started.id);
  TEST_ASSERT_TRUE(m.has);
  TEST_ASSERT_EQUAL_UINT32(9 * kBlock, m.value.bytes);  // the crossing block and the 8 after it
}

// No blocks at all (a dead capture): tick() opens the session at the wait's end.
static void test_tick_opens_after_the_wait_without_blocks() {
  FakeRecFilePort fs;
  Engine e(fs);
  e.engine->arm(memo(), 700, 700);
  e.engine->tick(2699, e.sink);
  TEST_ASSERT_TRUE(e.engine->waiting());
  TEST_ASSERT_EQUAL_UINT(0, e.sink.events.size());
  e.engine->tick(2700, e.sink);
  const RecEvt* started = e.sink.last(EvtKind::Started);
  TEST_ASSERT_NOT_NULL(started);
  TEST_ASSERT_EQUAL_UINT32(700, started->up_ms);
  TEST_ASSERT_EQUAL_UINT32(1, e.engine->mute_timeouts());
  e.engine->tick(9000, e.sink);  // a no-op once open
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Started));
  e.engine->stop(e.sink);
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Discarded));  // nothing written: under rec_min_ms
}

// A live block that only arrives after the wait is not a timeout.
static void test_a_late_live_block_is_not_counted() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  e.engine->arm(memo(), 0, now);
  now = 2500;
  feed(e, 1, now);
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Started));
  TEST_ASSERT_EQUAL_UINT32(0, e.engine->mute_timeouts());
  e.engine->stop(e.sink);
}

// Arming ends a file transfer like a start does (one open file at a time).
static void test_arm_closes_the_transfer_file() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  e.engine->start(memo(), 0, now, e.sink);
  feed(e, 10, now);
  e.engine->stop(e.sink);
  RecCmd get;
  get.kind = CmdKind::Get;
  get.fid = e.sink.last(EvtKind::Started)->id;
  get.size = e.sink.last(EvtKind::Committed)->size;
  get.len = 100;
  e.engine->get(get, e.sink);
  TEST_ASSERT_TRUE(fs.is_open());
  e.engine->arm(memo(), now, now);
  TEST_ASSERT_FALSE(fs.is_open());
  e.engine->stop(e.sink);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zero_and_dc_offset_blocks_are_muted);
  RUN_TEST(test_a_decaying_offset_tail_is_still_muted);
  RUN_TEST(test_a_carried_offset_tail_of_any_length_is_muted);
  RUN_TEST(test_ninety_percent_of_one_value_is_the_boundary);
  RUN_TEST(test_room_noise_and_a_spread_of_small_values_are_not_muted);
  RUN_TEST(test_the_muted_lead_ends_at_the_first_louder_sample);
  RUN_TEST(test_the_muted_lead_skips_a_carried_offset_tail);
  RUN_TEST(test_a_muted_start_opens_at_the_first_live_sample);
  RUN_TEST(test_a_muted_start_with_a_carried_offset_opens_at_the_first_live_sample);
  RUN_TEST(test_a_live_first_block_starts_at_once);
  RUN_TEST(test_stars_count_from_the_first_written_sample);
  RUN_TEST(test_a_stop_while_muted_discards);
  RUN_TEST(test_a_tag_while_muted_reaches_the_meta);
  RUN_TEST(test_two_seconds_of_muted_blocks_open_anyway_and_count);
  RUN_TEST(test_tick_opens_after_the_wait_without_blocks);
  RUN_TEST(test_a_late_live_block_is_not_counted);
  RUN_TEST(test_arm_closes_the_transfer_file);
  return UNITY_END();
}
