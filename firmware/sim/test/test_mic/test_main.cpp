// The serve microphone: silence, or a WAV file (16 kHz mono 16-bit PCM, the audio task's
// format) played in a loop, handed out in the audio task's blocks.
#include <unity.h>

#include <cstdint>
#include <string>

#include "mic_source.h"

using namespace cpasim;

void setUp() {}
void tearDown() {}

namespace {

std::string le(uint32_t v, int bytes) {
  std::string s;
  for (int i = 0; i < bytes; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
  return s;
}

std::string chunk(const std::string& id, const std::string& body) {
  return id + le(static_cast<uint32_t>(body.size()), 4) + body + (body.size() % 2 ? std::string(1, '\0') : "");
}

std::string fmt(uint16_t format, uint16_t ch, uint32_t rate, uint16_t bits) {
  const uint32_t align = ch * bits / 8;
  return chunk("fmt ", le(format, 2) + le(ch, 2) + le(rate, 4) + le(rate * align, 4) + le(align, 2) + le(bits, 2));
}

std::string wav(const std::string& chunks) {
  return "RIFF" + le(static_cast<uint32_t>(4 + chunks.size()), 4) + "WAVE" + chunks;
}

const std::string kPcm = "\x01\x02\x03\x04\x05\x06";

}  // namespace

static void test_reads_pcm_after_other_chunks() {
  const Wav w = parse_wav(wav(fmt(1, 1, 16000, 16) + chunk("LIST", "abc") + chunk("data", kPcm)));
  TEST_ASSERT_EQUAL_STRING("", w.error.c_str());
  TEST_ASSERT_TRUE(w.pcm == kPcm);
}

static void test_rejects_other_formats() {
  TEST_ASSERT_EQUAL_STRING("not a RIFF/WAVE file", parse_wav("hello").error.c_str());
  TEST_ASSERT_EQUAL_STRING("not PCM", parse_wav(wav(fmt(3, 1, 16000, 16) + chunk("data", kPcm))).error.c_str());
  TEST_ASSERT_EQUAL_STRING("need mono, 16000 Hz, 16-bit",
                           parse_wav(wav(fmt(1, 2, 16000, 16) + chunk("data", kPcm))).error.c_str());
  TEST_ASSERT_EQUAL_STRING("need mono, 16000 Hz, 16-bit",
                           parse_wav(wav(fmt(1, 1, 44100, 16) + chunk("data", kPcm))).error.c_str());
  TEST_ASSERT_EQUAL_STRING("need mono, 16000 Hz, 16-bit",
                           parse_wav(wav(fmt(1, 1, 16000, 8) + chunk("data", kPcm))).error.c_str());
}

static void test_rejects_missing_or_cut_chunks() {
  TEST_ASSERT_EQUAL_STRING("no fmt chunk", parse_wav(wav(chunk("data", kPcm))).error.c_str());
  TEST_ASSERT_EQUAL_STRING("no audio", parse_wav(wav(fmt(1, 1, 16000, 16))).error.c_str());
  TEST_ASSERT_EQUAL_STRING("no audio", parse_wav(wav(fmt(1, 1, 16000, 16) + chunk("data", ""))).error.c_str());
  const std::string cut = wav(fmt(1, 1, 16000, 16) + chunk("data", kPcm));
  TEST_ASSERT_EQUAL_STRING("chunk runs past the end", parse_wav(cut.substr(0, cut.size() - 2)).error.c_str());
}

static void test_odd_data_drops_the_half_sample() {
  const Wav w = parse_wav(wav(fmt(1, 1, 16000, 16) + chunk("data", "\x01\x02\x03")));
  TEST_ASSERT_EQUAL_size_t(2, w.pcm.size());
}

static void test_silence_by_default() {
  MicSource m;
  const std::string b = m.block(4096);
  TEST_ASSERT_EQUAL_size_t(4096, b.size());
  TEST_ASSERT_TRUE(b == std::string(4096, '\0'));
}

static void test_wav_loops() {
  MicSource m(kPcm);
  TEST_ASSERT_TRUE(m.block(4) == "\x01\x02\x03\x04");
  TEST_ASSERT_TRUE(m.block(4) == std::string("\x05\x06\x01\x02"));
  TEST_ASSERT_TRUE(m.block(10) == std::string("\x03\x04\x05\x06\x01\x02\x03\x04\x05\x06"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reads_pcm_after_other_chunks);
  RUN_TEST(test_rejects_other_formats);
  RUN_TEST(test_rejects_missing_or_cut_chunks);
  RUN_TEST(test_odd_data_drops_the_half_sample);
  RUN_TEST(test_silence_by_default);
  RUN_TEST(test_wav_loops);
  return UNITY_END();
}
