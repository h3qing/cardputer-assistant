// Host tests for lib/gate_core (pure logic behind !xfer, !cue and !mic):
//   pio test -e native
#include <unity.h>

#include <cstring>
#include <string>

#include "gate_core.h"

using namespace gate;

void setUp() {}
void tearDown() {}

// ---- !xfer <b64|bin> <len> <depth> <txring KB> <ram|sd> [secs] ---------------------

static void test_xfer_parses_all_fields() {
  const Parsed<XferArgs> p = parse_xfer("bin 4096 4 8 sd 15");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_TRUE(p.args.enc == Enc::Bin);
  TEST_ASSERT_EQUAL_UINT16(4096, p.args.len);
  TEST_ASSERT_EQUAL_UINT8(4, p.args.depth);
  TEST_ASSERT_EQUAL_UINT8(8, p.args.ring_kb);
  TEST_ASSERT_TRUE(p.args.src == Src::Sd);
  TEST_ASSERT_EQUAL_UINT16(15, p.args.secs);
}

static void test_xfer_defaults_secs_and_accepts_free_running_depth() {
  const Parsed<XferArgs> p = parse_xfer("b64 1380 0 4 ram");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_TRUE(p.args.enc == Enc::B64);
  TEST_ASSERT_EQUAL_UINT8(0, p.args.depth);
  TEST_ASSERT_TRUE(p.args.src == Src::Ram);
  TEST_ASSERT_EQUAL_UINT16(kXferDefaultSecs, p.args.secs);
}

static void test_xfer_names_the_bad_argument() {
  TEST_ASSERT_EQUAL_STRING("enc", parse_xfer("hex 1380 1 4 ram").err);
  TEST_ASSERT_EQUAL_STRING("len", parse_xfer("b64 0 1 4 ram").err);
  TEST_ASSERT_EQUAL_STRING("len", parse_xfer("b64 4097 1 4 ram").err);
  TEST_ASSERT_EQUAL_STRING("len", parse_xfer("b64 12x 1 4 ram").err);
  TEST_ASSERT_EQUAL_STRING("depth", parse_xfer("b64 1380 17 4 ram").err);
  TEST_ASSERT_EQUAL_STRING("txring", parse_xfer("b64 1380 1 6 ram").err);
  TEST_ASSERT_EQUAL_STRING("src", parse_xfer("b64 1380 1 4 flash").err);
  TEST_ASSERT_EQUAL_STRING("secs", parse_xfer("b64 1380 1 4 ram 0").err);
  TEST_ASSERT_EQUAL_STRING("secs", parse_xfer("b64 1380 1 4 ram 121").err);
  TEST_ASSERT_EQUAL_STRING("src", parse_xfer("b64 1380 1 4").err);
  TEST_ASSERT_EQUAL_STRING("extra", parse_xfer("b64 1380 1 4 ram 5 9").err);
  TEST_ASSERT_FALSE(parse_xfer("").ok);
  TEST_ASSERT_FALSE(parse_xfer(nullptr).ok);
}

static void test_xfer_keep_open_source() {
  const Parsed<XferArgs> p = parse_xfer("b64 1380 4 4 sdkeep");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_TRUE(p.args.src == Src::SdKeep);
}

static void test_xfer_tolerates_repeated_spaces() {
  const Parsed<XferArgs> p = parse_xfer("  b64   1380  2 16   ram  ");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_EQUAL_UINT8(16, p.args.ring_kb);
}

// ---- !cue <ms> <vol> [memo|meet|silent] [off|nolock|lock] [rec_ms] -----------------

static void test_cue_defaults() {
  const Parsed<CueArgs> p = parse_cue("80 128");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_EQUAL_UINT16(80, p.args.tone_ms);
  TEST_ASSERT_EQUAL_UINT8(128, p.args.vol);
  TEST_ASSERT_TRUE(p.args.pattern == Pattern::Memo);
  TEST_ASSERT_TRUE(p.args.hammer == Hammer::Off);
  TEST_ASSERT_EQUAL_UINT16(kCueDefaultRecMs, p.args.rec_ms);
}

static void test_cue_all_fields() {
  const Parsed<CueArgs> p = parse_cue("100 255 meet nolock 600");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_TRUE(p.args.pattern == Pattern::Meet);
  TEST_ASSERT_TRUE(p.args.hammer == Hammer::NoLock);
  TEST_ASSERT_EQUAL_UINT16(600, p.args.rec_ms);
  TEST_ASSERT_TRUE(parse_cue("100 0 memo lock").args.hammer == Hammer::Lock);
  TEST_ASSERT_TRUE(parse_cue("100 0 meet lockyield").args.hammer == Hammer::LockYield);
}

static void test_cue_silent_pattern() {  // v0.3.1's start: no speaker, ms and vol unused
  const Parsed<CueArgs> p = parse_cue("80 0 silent lockyield");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_TRUE(p.args.pattern == Pattern::Silent);
  TEST_ASSERT_TRUE(p.args.hammer == Hammer::LockYield);
}

static void test_cue_rejects_out_of_range() {
  TEST_ASSERT_EQUAL_STRING("ms", parse_cue("19 128").err);
  TEST_ASSERT_EQUAL_STRING("ms", parse_cue("1001 128").err);
  TEST_ASSERT_EQUAL_STRING("vol", parse_cue("80 256").err);
  TEST_ASSERT_EQUAL_STRING("vol", parse_cue("80").err);
  TEST_ASSERT_EQUAL_STRING("pattern", parse_cue("80 1 chirp").err);
  TEST_ASSERT_EQUAL_STRING("hammer", parse_cue("80 1 memo always").err);
  TEST_ASSERT_EQUAL_STRING("rec_ms", parse_cue("80 1 memo off 199").err);
  TEST_ASSERT_EQUAL_STRING("rec_ms", parse_cue("80 1 memo off 1001").err);
  TEST_ASSERT_EQUAL_STRING("extra", parse_cue("80 1 memo off 500 x").err);
}

// ---- !mic <mag> <pga> <secs> --------------------------------------------------------

static void test_mic_parses_and_validates() {
  const Parsed<MicArgs> p = parse_mic("32 6 60");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_EQUAL_UINT8(32, p.args.mag);
  TEST_ASSERT_EQUAL_UINT8(6, p.args.pga);
  TEST_ASSERT_EQUAL_UINT16(60, p.args.secs);
  TEST_ASSERT_EQUAL_STRING("mag", parse_mic("0 6 60").err);
  TEST_ASSERT_EQUAL_STRING("mag", parse_mic("65 6 60").err);
  TEST_ASSERT_EQUAL_STRING("pga", parse_mic("16 11 60").err);
  TEST_ASSERT_EQUAL_STRING("secs", parse_mic("16 0 0").err);
  TEST_ASSERT_EQUAL_STRING("secs", parse_mic("16 0 301").err);
  TEST_ASSERT_EQUAL_STRING("secs", parse_mic("16 0").err);
}

static void test_es8311_pga_register_keeps_mic1_select() {
  TEST_ASSERT_EQUAL_HEX8(0x10, es8311_reg14(0));
  TEST_ASSERT_EQUAL_HEX8(0x1A, es8311_reg14(10));
  TEST_ASSERT_EQUAL_HEX8(0x1A, es8311_reg14(15));  // clamped to +30 dB
}

// ---- frames (the app's file.data envelope, 11 §5.3) -------------------------------

static void test_b64_length() {
  TEST_ASSERT_EQUAL_UINT32(0, b64_len(0));
  TEST_ASSERT_EQUAL_UINT32(4, b64_len(1));
  TEST_ASSERT_EQUAL_UINT32(4, b64_len(3));
  TEST_ASSERT_EQUAL_UINT32(1840, b64_len(1380));
}

static void test_b64_head_matches_app_envelope() {
  char buf[256];
  const FrameMeta m = {7, 1380, 1380, 384044, 2914141237u};
  const size_t n = frame_head(buf, sizeof buf, Enc::B64, m);
  const std::string want =
      "{\"v\":0,\"t\":\"file.data\",\"id\":\"d7\",\"re\":\"h7\",\"fid\":\"K3M9Q2T-00042-0A1C\","
      "\"off\":1380,\"n\":1380,\"size\":384044,\"crc\":2914141237,\"b64\":\"";
  TEST_ASSERT_EQUAL_UINT32(want.size(), n);
  TEST_ASSERT_EQUAL_STRING(want.c_str(), buf);
  TEST_ASSERT_EQUAL_UINT32(3, frame_tail(Enc::B64));
  TEST_ASSERT_EQUAL_UINT32(n + 1840 + 3, frame_size(Enc::B64, n, 1380));
}

static void test_bin_head_carries_length_and_newline() {
  char buf[256];
  const FrameMeta m = {1, 0, 4096, 9600044, 1};
  const size_t n = frame_head(buf, sizeof buf, Enc::Bin, m);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_CHAR('\n', buf[n - 1]);
  TEST_ASSERT_NOT_NULL(std::strstr(buf, ",\"bin\":4096}\n"));
  TEST_ASSERT_EQUAL_UINT32(1, frame_tail(Enc::Bin));
  TEST_ASSERT_EQUAL_UINT32(n + 4096 + 1, frame_size(Enc::Bin, n, 4096));
}

static void test_frame_head_reports_overflow_as_zero() {
  char small[16];
  const FrameMeta m = {1, 0, 10, 10, 0};
  TEST_ASSERT_EQUAL_UINT32(0, frame_head(small, sizeof small, Enc::B64, m));
}

static void test_b64_encode_rfc4648_vectors() {
  const char* const in[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char* const want[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  for (int i = 0; i < 7; ++i) {
    char out[16];
    const size_t n = b64_encode(reinterpret_cast<const uint8_t*>(in[i]), std::strlen(in[i]), out, sizeof out);
    TEST_ASSERT_EQUAL_UINT32(std::strlen(want[i]), n);
    if (n) TEST_ASSERT_EQUAL_MEMORY(want[i], out, n);  // Unity rejects a 0-byte compare
  }
}

static void test_b64_encode_all_bytes_and_capacity() {
  uint8_t in[256];
  for (int i = 0; i < 256; ++i) in[i] = static_cast<uint8_t>(i);
  char out[400];
  const size_t n = b64_encode(in, 256, out, sizeof out);
  TEST_ASSERT_EQUAL_UINT32(b64_len(256), n);
  TEST_ASSERT_EQUAL_MEMORY("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4v", out, 64);
  TEST_ASSERT_EQUAL_MEMORY("+/w==", out + n - 5, 5);  // ... 0xFC 0xFD 0xFE 0xFF -> "/P3+/w=="
  TEST_ASSERT_EQUAL_UINT32(0, b64_encode(in, 256, out, b64_len(256) - 1));  // no room: nothing written
}

// ---- audio levels ------------------------------------------------------------------

static void test_level_rms_and_peak() {
  const int16_t s[4] = {300, -300, 300, -400};
  const Level l = level(s, 4);
  TEST_ASSERT_EQUAL_UINT16(327, l.rms);  // sqrt((3*90000 + 160000) / 4) = 327.9
  TEST_ASSERT_EQUAL_UINT16(400, l.peak);
  TEST_ASSERT_EQUAL_UINT16(0, level(s, 0).rms);
}

static void test_level_peak_of_full_scale_negative() {
  const int16_t s[2] = {-32768, 0};
  TEST_ASSERT_EQUAL_UINT16(32768, level(s, 2).peak);
}

static void test_level_bins_split_and_cap() {
  int16_t s[35];
  for (int i = 0; i < 35; ++i) s[i] = static_cast<int16_t>(i < 10 ? 100 : 10);
  Level out[8];
  TEST_ASSERT_EQUAL_UINT32(3, level_bins(s, 35, 10, out, 8));  // the partial 5-sample tail is dropped
  TEST_ASSERT_EQUAL_UINT16(100, out[0].rms);
  TEST_ASSERT_EQUAL_UINT16(10, out[2].rms);
  TEST_ASSERT_EQUAL_UINT32(2, level_bins(s, 35, 10, out, 2));
  TEST_ASSERT_EQUAL_UINT32(0, level_bins(s, 35, 0, out, 8));
}

// ---- CPU load from idle-hook counts -------------------------------------------------

static void test_busy_pct() {
  TEST_ASSERT_EQUAL_UINT8(0, busy_pct(1000, 1000));
  TEST_ASSERT_EQUAL_UINT8(25, busy_pct(750, 1000));
  TEST_ASSERT_EQUAL_UINT8(100, busy_pct(0, 1000));
  TEST_ASSERT_EQUAL_UINT8(0, busy_pct(1200, 1000));  // idle above the reference
  TEST_ASSERT_EQUAL_UINT8(0, busy_pct(5, 0));        // no reference
}

// ---- register read-back -------------------------------------------------------------

static void test_count_mismatch() {
  const Reg want[3] = {{0x01, 0xB5}, {0x02, 0x18}, {0x0D, 0x01}};
  const uint8_t same[3] = {0xB5, 0x18, 0x01};
  const uint8_t off[3] = {0xB5, 0x00, 0xFF};
  TEST_ASSERT_EQUAL_UINT32(0, count_mismatch(want, same, 3));
  TEST_ASSERT_EQUAL_UINT32(2, count_mismatch(want, off, 3));
}

// ---- !warm <rec_ms> <end|keep> [<at_ms>:<RR>=<VV>]... and !cw <RR>=<VV>... (issue #31) --

static void test_warm_plain_capture() {
  const Parsed<WarmArgs> p = parse_warm("2500 end");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_EQUAL_UINT16(2500, p.args.rec_ms);
  TEST_ASSERT_FALSE(p.args.keep);
  TEST_ASSERT_EQUAL_UINT8(0, p.args.n);
}

static void test_warm_timed_writes_in_order() {
  const Parsed<WarmArgs> p = parse_warm("1000 keep 0:0D=03  150:0d=01");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_TRUE(p.args.keep);
  TEST_ASSERT_EQUAL_UINT8(2, p.args.n);
  TEST_ASSERT_EQUAL_UINT16(0, p.args.w[0].at_ms);
  TEST_ASSERT_EQUAL_HEX8(0x0D, p.args.w[0].reg);
  TEST_ASSERT_EQUAL_HEX8(0x03, p.args.w[0].val);
  TEST_ASSERT_EQUAL_UINT16(150, p.args.w[1].at_ms);
  TEST_ASSERT_EQUAL_HEX8(0x0D, p.args.w[1].reg);
  TEST_ASSERT_EQUAL_HEX8(0x01, p.args.w[1].val);
}

static void test_warm_sample_rate() {
  TEST_ASSERT_EQUAL_UINT32(16000, parse_warm("1000 end").args.rate);
  const Parsed<WarmArgs> p = parse_warm("1000 keep hz=48000 0:0D=01");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_EQUAL_UINT32(48000, p.args.rate);
  TEST_ASSERT_EQUAL_UINT8(1, p.args.n);
  TEST_ASSERT_EQUAL_STRING("hz", parse_warm("1000 end hz=7999").err);
  TEST_ASSERT_EQUAL_STRING("hz", parse_warm("1000 end hz=64001").err);
  TEST_ASSERT_EQUAL_STRING("rec_ms", parse_warm("1500 end hz=48000").err);  // over kWarmMaxSamples
  TEST_ASSERT_TRUE(parse_warm("4000 end hz=16000").ok);
}

static void test_warm_rejects_bad_arguments() {
  TEST_ASSERT_EQUAL_STRING("rec_ms", parse_warm("99 end").err);
  TEST_ASSERT_EQUAL_STRING("rec_ms", parse_warm("4001 end").err);
  TEST_ASSERT_EQUAL_STRING("mode", parse_warm("1000 stay").err);
  TEST_ASSERT_EQUAL_STRING("mode", parse_warm("1000").err);
  TEST_ASSERT_EQUAL_STRING("write", parse_warm("1000 end 0D=01").err);      // no time
  TEST_ASSERT_EQUAL_STRING("write", parse_warm("1000 end 10:0D").err);      // no value
  TEST_ASSERT_EQUAL_STRING("write", parse_warm("1000 end 10:0G=01").err);   // not hex
  TEST_ASSERT_EQUAL_STRING("write", parse_warm("1000 end 10:100=01").err);  // three digits
  TEST_ASSERT_EQUAL_STRING("write", parse_warm("1000 end 4001:0D=01").err);
  TEST_ASSERT_EQUAL_STRING("order", parse_warm("1000 end 20:0D=01 10:0D=02").err);
  TEST_ASSERT_EQUAL_STRING("writes", parse_warm("100 end 0:00=80 0:00=80 0:00=80 0:00=80 0:00=80 0:00=80 "
                                                "0:00=80 0:00=80 0:00=80").err);
  TEST_ASSERT_FALSE(parse_warm(nullptr).ok);
}

static void test_reg_writes() {
  const Parsed<RegWrites> p = parse_regs("00=1F 0d=fc");
  TEST_ASSERT_TRUE(p.ok);
  TEST_ASSERT_EQUAL_UINT8(2, p.args.n);
  TEST_ASSERT_EQUAL_HEX8(0x00, p.args.r[0].reg);
  TEST_ASSERT_EQUAL_HEX8(0x1F, p.args.r[0].val);
  TEST_ASSERT_EQUAL_HEX8(0x0D, p.args.r[1].reg);
  TEST_ASSERT_EQUAL_HEX8(0xFC, p.args.r[1].val);
  TEST_ASSERT_EQUAL_STRING("write", parse_regs("").err);
  TEST_ASSERT_EQUAL_STRING("write", parse_regs("0D=1").err);
  TEST_ASSERT_EQUAL_STRING("write", parse_regs("10:0D=01").err);
}

static void test_leading_zeros() {
  const int16_t s[6] = {0, 0, 0, 5, 0, -3};
  const int16_t z[3] = {0, 0, 0};
  TEST_ASSERT_EQUAL_UINT32(3, leading_zeros(s, 6));
  TEST_ASSERT_EQUAL_UINT32(0, leading_zeros(s + 3, 3));
  TEST_ASSERT_EQUAL_UINT32(3, leading_zeros(z, 3));
  TEST_ASSERT_EQUAL_UINT32(0, leading_zeros(z, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_xfer_parses_all_fields);
  RUN_TEST(test_xfer_defaults_secs_and_accepts_free_running_depth);
  RUN_TEST(test_xfer_names_the_bad_argument);
  RUN_TEST(test_xfer_keep_open_source);
  RUN_TEST(test_xfer_tolerates_repeated_spaces);
  RUN_TEST(test_cue_defaults);
  RUN_TEST(test_cue_all_fields);
  RUN_TEST(test_cue_silent_pattern);
  RUN_TEST(test_cue_rejects_out_of_range);
  RUN_TEST(test_mic_parses_and_validates);
  RUN_TEST(test_es8311_pga_register_keeps_mic1_select);
  RUN_TEST(test_b64_length);
  RUN_TEST(test_b64_head_matches_app_envelope);
  RUN_TEST(test_bin_head_carries_length_and_newline);
  RUN_TEST(test_frame_head_reports_overflow_as_zero);
  RUN_TEST(test_b64_encode_rfc4648_vectors);
  RUN_TEST(test_b64_encode_all_bytes_and_capacity);
  RUN_TEST(test_level_rms_and_peak);
  RUN_TEST(test_level_peak_of_full_scale_negative);
  RUN_TEST(test_level_bins_split_and_cap);
  RUN_TEST(test_busy_pct);
  RUN_TEST(test_count_mismatch);
  RUN_TEST(test_warm_plain_capture);
  RUN_TEST(test_warm_timed_writes_in_order);
  RUN_TEST(test_warm_sample_rate);
  RUN_TEST(test_warm_rejects_bad_arguments);
  RUN_TEST(test_reg_writes);
  RUN_TEST(test_leading_zeros);
  return UNITY_END();
}
