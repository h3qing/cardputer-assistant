// WAV header of a recording segment (docs/11 §4.4-§4.6): 16 kHz, 16 bit, mono PCM in the
// standard 44 B header. While a segment is open its header is a placeholder (lengths 0)
// whose RIFF size carries the recording mode, so a segment recovered after a power cut
// still knows whether it was a memo or a meeting.
#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "wav_header.h"

using namespace cpa;
using namespace cpa::rec;

void setUp() {}
void tearDown() {}

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
static uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

static void test_header_layout() {
  const WavHeader h = wav_header(9600000);
  TEST_ASSERT_EQUAL_MEMORY("RIFF", h.b, 4);
  TEST_ASSERT_EQUAL_UINT32(36 + 9600000, le32(h.b + 4));
  TEST_ASSERT_EQUAL_MEMORY("WAVEfmt ", h.b + 8, 8);
  TEST_ASSERT_EQUAL_UINT32(16, le32(h.b + 16));
  TEST_ASSERT_EQUAL_UINT16(1, le16(h.b + 20));      // PCM
  TEST_ASSERT_EQUAL_UINT16(1, le16(h.b + 22));      // mono
  TEST_ASSERT_EQUAL_UINT32(16000, le32(h.b + 24));  // Hz
  TEST_ASSERT_EQUAL_UINT32(32000, le32(h.b + 28));  // bytes per second
  TEST_ASSERT_EQUAL_UINT16(2, le16(h.b + 32));
  TEST_ASSERT_EQUAL_UINT16(16, le16(h.b + 34));
  TEST_ASSERT_EQUAL_MEMORY("data", h.b + 36, 4);
  TEST_ASSERT_EQUAL_UINT32(9600000, le32(h.b + 40));
  TEST_ASSERT_EQUAL_UINT(44, sizeof(h.b));
}

static void test_parse_round_trip() {
  const WavHeader h = wav_header(384000);
  const Opt<WavInfo> w = wav_parse(h.b, sizeof h.b);
  TEST_ASSERT_TRUE(w.has);
  TEST_ASSERT_EQUAL_UINT32(384000, w.value.data_bytes);
  TEST_ASSERT_FALSE(w.value.hint.has);
  TEST_ASSERT_FALSE(wav_parse(h.b, 43).has);  // too short
  WavHeader bad = h;
  bad.b[22] = 2;  // stereo is not ours
  TEST_ASSERT_FALSE(wav_parse(bad.b, sizeof bad.b).has);
  bad = h;
  bad.b[24] = 0x44;  // 44.1 kHz
  TEST_ASSERT_FALSE(wav_parse(bad.b, sizeof bad.b).has);
  bad = h;
  bad.b[0] = 'X';
  TEST_ASSERT_FALSE(wav_parse(bad.b, sizeof bad.b).has);
}

static void test_placeholder_carries_the_mode() {
  for (RecMode m : {RecMode::Memo, RecMode::Meeting}) {
    const WavHeader p = wav_placeholder(m);
    TEST_ASSERT_EQUAL_UINT32(0, le32(p.b + 40));
    const Opt<WavInfo> w = wav_parse(p.b, sizeof p.b);
    TEST_ASSERT_TRUE(w.has);
    TEST_ASSERT_EQUAL_UINT32(0, w.value.data_bytes);
    TEST_ASSERT_TRUE(w.value.hint.has && w.value.hint.value == m);
  }
  // a finished header has no hint even with an empty data chunk
  const WavHeader empty = wav_header(0);
  TEST_ASSERT_FALSE(wav_parse(empty.b, sizeof empty.b).value.hint.has);
}

static void test_bytes_and_duration() {
  TEST_ASSERT_EQUAL_UINT32(12000, dur_ms_of(384000));
  TEST_ASSERT_EQUAL_UINT32(300000, dur_ms_of(9600000));
  TEST_ASSERT_EQUAL_UINT32(0, dur_ms_of(31));
  TEST_ASSERT_EQUAL_UINT32(9600000, bytes_of_ms(300000));
  TEST_ASSERT_EQUAL_UINT32(1000, even_data(1045));  // a power cut mid-sample: whole samples only
  TEST_ASSERT_EQUAL_UINT32(0, even_data(44));
  TEST_ASSERT_EQUAL_UINT32(0, even_data(10));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_header_layout);
  RUN_TEST(test_parse_round_trip);
  RUN_TEST(test_placeholder_carries_the_mode);
  RUN_TEST(test_bytes_and_duration);
  return UNITY_END();
}
