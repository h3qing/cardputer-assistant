// LineFramer (05 §5.8, §6.1): split on \n, ignore \r, lines over 2048 B (incl. \n) are
// dropped up to the next \n and reported as E_TOO_LARGE; lines not starting with '{'
// are noise (dropped, counted).
#include <unity.h>

#include <string>
#include <vector>

#include "line_framer.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

struct Ev {
  FrameEvent kind;
  std::string line;
};

// Feeds `input` in chunks of `chunk` bytes and collects every event.
static std::vector<Ev> feed(const LineFramer& start, const std::string& input, size_t chunk, LineFramer* end = nullptr) {
  std::vector<Ev> out;
  LineFramer f = start;
  for (size_t off = 0; off < input.size(); off += chunk) {
    const size_t n = std::min(chunk, input.size() - off);
    size_t used = 0;
    while (used < n) {
      const FrameStep s = framer_scan(f, input.data() + off + used, n - used);
      f = s.framer;
      used += s.consumed;
      if (s.event == FrameEvent::Line) out.push_back({s.event, std::string(framer_line(f), framer_line_size(f))});
      if (s.event == FrameEvent::TooLarge || s.event == FrameEvent::Noise) out.push_back({s.event, ""});
    }
  }
  if (end) *end = f;
  return out;
}

static void test_splits_lines_and_drops_cr() {
  const auto ev = feed(LineFramer{}, "{\"a\":1}\r\n{\"b\":\r2}\n", 64);
  TEST_ASSERT_EQUAL_UINT(2, ev.size());
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", ev[0].line.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"b\":2}", ev[1].line.c_str());
}

static void test_chunk_boundaries_do_not_matter() {
  const std::string input = "{\"x\":\"hello\"}\n{\"y\":2}\n";
  for (size_t chunk = 1; chunk <= input.size(); ++chunk) {
    const auto ev = feed(LineFramer{}, input, chunk);
    TEST_ASSERT_EQUAL_UINT(2, ev.size());
    TEST_ASSERT_EQUAL_STRING("{\"y\":2}", ev[1].line.c_str());
  }
}

static void test_noise_is_dropped_and_counted() {
  LineFramer end;
  const auto ev = feed(LineFramer{}, "ESP-ROM:esp32s3\r\n\n{\"ok\":1}\n", 7, &end);
  TEST_ASSERT_EQUAL_UINT(3, ev.size());
  TEST_ASSERT_TRUE(ev[0].kind == FrameEvent::Noise);
  TEST_ASSERT_TRUE(ev[1].kind == FrameEvent::Noise);  // blank line (daemon nudge)
  TEST_ASSERT_TRUE(ev[2].kind == FrameEvent::Line);
  TEST_ASSERT_EQUAL_UINT32(2, end.noise_n);
  TEST_ASSERT_EQUAL_UINT32(1, end.lines_n);
}

static void test_limit_is_2048_bytes_including_newline() {
  const std::string max_line = "{" + std::string(2045, ' ') + "}";  // 2047 + \n = 2048
  const auto ok = feed(LineFramer{}, max_line + "\n", 300);
  TEST_ASSERT_EQUAL_UINT(1, ok.size());
  TEST_ASSERT_TRUE(ok[0].kind == FrameEvent::Line);
  TEST_ASSERT_EQUAL_UINT(2047, ok[0].line.size());
  // \r bytes do not count
  TEST_ASSERT_TRUE(feed(LineFramer{}, max_line + "\r\r\n", 300)[0].kind == FrameEvent::Line);
  LineFramer end;
  const auto big = feed(LineFramer{}, "{" + std::string(2046, ' ') + "}\n{\"next\":1}\n", 100, &end);
  TEST_ASSERT_EQUAL_UINT(2, big.size());
  TEST_ASSERT_TRUE(big[0].kind == FrameEvent::TooLarge);
  TEST_ASSERT_TRUE(big[1].kind == FrameEvent::Line);
  TEST_ASSERT_EQUAL_STRING("{\"next\":1}", big[1].line.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, end.too_large_n);
}

static void test_too_large_wins_over_noise() {
  const auto ev = feed(LineFramer{}, std::string(3000, 'x') + "\n", 512);
  TEST_ASSERT_EQUAL_UINT(1, ev.size());
  TEST_ASSERT_TRUE(ev[0].kind == FrameEvent::TooLarge);
}

static void test_partial_line_waits_for_more() {
  LineFramer end;
  const auto ev = feed(LineFramer{}, "{\"partial\":", 4, &end);
  TEST_ASSERT_EQUAL_UINT(0, ev.size());
  const auto rest = feed(end, "1}\n", 4);
  TEST_ASSERT_EQUAL_UINT(1, rest.size());
  TEST_ASSERT_EQUAL_STRING("{\"partial\":1}", rest[0].line.c_str());
}

static void test_scan_stops_after_each_event() {
  const std::string two = "{\"a\":1}\n{\"b\":2}\n";
  const FrameStep s = framer_scan(LineFramer{}, two.data(), two.size());
  TEST_ASSERT_TRUE(s.event == FrameEvent::Line);
  TEST_ASSERT_EQUAL_UINT(8, s.consumed);
  const FrameStep none = framer_scan(LineFramer{}, "", 0);
  TEST_ASSERT_TRUE(none.event == FrameEvent::None);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_splits_lines_and_drops_cr);
  RUN_TEST(test_chunk_boundaries_do_not_matter);
  RUN_TEST(test_noise_is_dropped_and_counted);
  RUN_TEST(test_limit_is_2048_bytes_including_newline);
  RUN_TEST(test_too_large_wins_over_noise);
  RUN_TEST(test_partial_line_waits_for_more);
  RUN_TEST(test_scan_stops_after_each_event);
  return UNITY_END();
}
