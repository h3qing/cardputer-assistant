// The stdin of `cpa-sim serve`: script steps for a running device, one per line. The keys
// are played one sample per 10 ms loop, in order; steps that only make sense in a script
// (boot, host, reply, mic, include) are refused, because on serve cpad is the host.
#include <unity.h>

#include <string>

#include "live.h"
#include "presses.h"

using namespace cpasim;
using cpa::KeySet;
namespace key = cpa::key;

void setUp() {}
void tearDown() {}

namespace {

Step step_of(const std::string& line) {
  const LiveCmd c = parse_live(line);
  TEST_ASSERT_EQUAL_STRING("", c.error.c_str());
  TEST_ASSERT_TRUE(c.kind == LiveKind::Step);
  return c.step;
}

KeySet g0() { return KeySet{}.with(key::kG0); }

}  // namespace

static void test_parses_steps() {
  TEST_ASSERT_TRUE(step_of("press g0").op == Op::Press);
  TEST_ASSERT_TRUE(step_of("  type hi  # a comment").op == Op::Press);
  TEST_ASSERT_TRUE(step_of("usb off").op == Op::Usb);
  TEST_ASSERT_EQUAL_UINT32(3300, step_of("bat 3300").value);
  const Step s = step_of("shot board The board");
  TEST_ASSERT_EQUAL_STRING("board", s.type.c_str());
  TEST_ASSERT_EQUAL_STRING("The board", s.text.c_str());
}

static void test_blank_quit_and_errors() {
  TEST_ASSERT_TRUE(parse_live("   ").kind == LiveKind::Empty);
  TEST_ASSERT_TRUE(parse_live("# just a note").kind == LiveKind::Empty);
  TEST_ASSERT_TRUE(parse_live("quit").kind == LiveKind::Quit);
  const LiveCmd bad = parse_live("press nokey");
  TEST_ASSERT_TRUE(bad.kind == LiveKind::Error);
  TEST_ASSERT_EQUAL_INT(0, bad.error.find("unknown key"));  // no "file:line:" prefix
  TEST_ASSERT_TRUE(parse_live("dance").kind == LiveKind::Error);
}

static void test_script_only_steps_are_refused() {
  const char* refused[] = {"boot store=sd", "host ping", "reply outbox.ack outbox.item", "mic 1000",
                           "include common/hello.cpas"};
  for (const char* line : refused) {
    const LiveCmd c = parse_live(line);
    TEST_ASSERT_TRUE_MESSAGE(c.kind == LiveKind::Error, line);
  }
  TEST_ASSERT_EQUAL_STRING("'host' only works in a script: on serve the host is cpad",
                           parse_live("host ping").error.c_str());
}

static void test_feed_plays_presses_in_order() {
  KeyFeed f;
  TEST_ASSERT_TRUE(f.idle());
  f.add(step_of("press g0 enter"));
  const auto want = press_samples(step_of("press g0 enter").keys, kPressMs, KeySet{});
  for (const KeySet& k : want) TEST_ASSERT_TRUE(f.next() == k);
  TEST_ASSERT_TRUE(f.idle());
  TEST_ASSERT_TRUE(f.next().empty());
}

static void test_down_and_up_hold_keys_after_what_is_queued() {
  KeyFeed f;
  f.add(step_of("press enter"));
  f.add(step_of("down g0"));
  f.add(step_of("wait 30"));
  f.add(step_of("up"));
  const size_t press_n = press_samples(step_of("press enter").keys, kPressMs, KeySet{}).size();
  for (size_t i = 0; i < press_n; ++i) TEST_ASSERT_FALSE(f.next().has(key::kG0));
  for (int i = 0; i < 4; ++i) TEST_ASSERT_TRUE(f.next() == g0());  // down, then 3 waiting loops
  TEST_ASSERT_TRUE(f.next().empty());                               // up
  TEST_ASSERT_TRUE(f.idle());
}

static void test_held_keys_stay_after_the_queue_runs_out() {
  KeyFeed f;
  f.add(step_of("down g0"));
  f.next();
  TEST_ASSERT_TRUE(f.idle());
  TEST_ASSERT_TRUE(f.next() == g0());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_steps);
  RUN_TEST(test_blank_quit_and_errors);
  RUN_TEST(test_script_only_steps_are_refused);
  RUN_TEST(test_feed_plays_presses_in_order);
  RUN_TEST(test_down_and_up_hold_keys_after_what_is_queued);
  RUN_TEST(test_held_keys_stay_after_the_queue_runs_out);
  return UNITY_END();
}
