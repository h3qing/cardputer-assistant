// The runtime fuzz test in CI (docs/20 §4.5): two seeds of three simulated minutes each
// on the SD card; CPA_FUZZ_SEEDS / CPA_FUZZ_MINUTES / CPA_FUZZ_STORE run a longer matrix
// (nightly). And the fault of docs/20 S1, deterministic: the audio task's end event is
// lost, and the device is usable again within 10 s, with no reboot.
#include <unity.h>

#include <cstdlib>
#include <string>

#include "fuzz.h"
#include "rec_core.h"

using namespace fuzz;

void setUp() {}
void tearDown() {}

namespace {

uint32_t env_or(const char* name, uint32_t fallback) {
  const char* v = std::getenv(name);
  return v && *v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : fallback;
}

cpa::Store env_store() {
  const char* v = std::getenv("CPA_FUZZ_STORE");
  if (!v) return cpa::Store::Sd;
  const std::string s = v;
  return s == "internal" ? cpa::Store::Internal : (s == "none" ? cpa::Store::None : cpa::Store::Sd);
}

void run_seed(uint64_t seed, uint32_t minutes, cpa::Store store) {
  Fuzz f(seed, store);
  f.run_ms(1200);
  f.hello();
  f.run_ms(1500);
  const uint64_t total = static_cast<uint64_t>(minutes) * 60000;
  while (f.T < total) f.episode_run(60000);
  f.drain();
  const std::string stats = "seed " + std::to_string(seed) + ": " + std::to_string(f.T / 1000) + " s, " +
                            std::to_string(f.keys) + " keys, " + std::to_string(f.memos) + " memos, " +
                            std::to_string(f.meetings) + " meetings, " + std::to_string(f.stored.size()) +
                            " captures, " + std::to_string(f.pulled.size()) + " segments pulled, home in <= " +
                            std::to_string(f.max_escape);
  TEST_MESSAGE(stats.c_str());
  std::string all;
  for (const std::string& s : f.failures) all += s + "\n";
  TEST_ASSERT_TRUE_MESSAGE(f.failures.empty(), all.c_str());
  // it did run (a memo past the cap, 2% of memos, takes 5 of the minutes: seed 12 since gh #51)
  TEST_ASSERT_TRUE(f.keys > 50 && f.escapes >= 1);
  TEST_ASSERT_TRUE(f.max_escape <= static_cast<uint64_t>(kMaxHomePresses));
}

}  // namespace

static void test_fuzz_seeds() {
  const uint32_t first = env_or("CPA_FUZZ_SEED", 11);
  const uint32_t seeds = env_or("CPA_FUZZ_SEEDS", 2);
  const uint32_t minutes = env_or("CPA_FUZZ_MINUTES", 3);
  for (uint32_t s = 0; s < seeds; ++s) run_seed(first + s, minutes, env_store());
}

// docs/20 S1: the end event of a memo is lost. Before the fix every key was dead until a
// reboot; now the core gives up within 10 s and the way home works.
static void test_a_lost_end_event_is_not_a_dead_device() {
  Fuzz f(7);
  f.dev->port->drop_end = 1;
  f.run_ms(1200);
  f.hello();
  f.run_ms(3000);
  f.hold_g0(3000);
  f.run_ms(500 + cpa::rec::kMemoGraceMs);  // gh #51: it ends when the grace runs out
  TEST_ASSERT_TRUE(f.st().rec.phase == cpa::RecPhase::Finishing);
  f.run_ms(10000);
  TEST_ASSERT_TRUE_MESSAGE(f.st().rec.phase == cpa::RecPhase::Idle, f.where().c_str());
  TEST_ASSERT_TRUE(f.escape() >= 0);
  const size_t starts = f.dev->port->count(cpa::rec::CmdKind::Start);
  f.hold_g0(3000);  // and it records again (review: checked, not only "Idle")
  f.run_ms(500 + cpa::rec::kMemoGraceMs);
  TEST_ASSERT_EQUAL_UINT(starts + 1, f.dev->port->count(cpa::rec::CmdKind::Start));
  const cpa::RecLast& last = f.st().rec.last;
  TEST_ASSERT_TRUE_MESSAGE(last.has && last.saved && last.mode == cpa::RecMode::Memo && last.ms >= 2000,
                           f.where().c_str());
  TEST_ASSERT_TRUE(f.st().rec.phase == cpa::RecPhase::Idle);
  TEST_ASSERT_TRUE(f.failures.empty());
}

// Review (S1): the end event is late, not lost: a slow end_session sends it after the
// core gave up, behind the next memo's Start. It must not end that memo: ● REC is shown,
// and the release stops the audio task (before, the task went on recording unseen).
static void test_a_late_end_event_does_not_end_the_next_memo() {
  Fuzz f(8);
  f.dev->port->hold_end = true;
  f.run_ms(1200);
  f.hello();
  f.run_ms(3000);
  f.hold_g0(3000);
  f.run_ms(500 + cpa::rec::kMemoGraceMs);
  TEST_ASSERT_TRUE(f.st().rec.phase == cpa::RecPhase::Finishing);
  f.run_ms(10000);
  TEST_ASSERT_TRUE_MESSAGE(f.st().rec.phase == cpa::RecPhase::Idle, f.where().c_str());
  f.held = f.held.with(K::G0);
  f.run_ms(1500);
  TEST_ASSERT_TRUE_MESSAGE(f.st().rec.phase == cpa::RecPhase::Recording, f.where().c_str());
  f.run_ms(1500);
  f.held = f.held.without(K::G0);
  f.run_ms(500 + cpa::rec::kMemoGraceMs);
  TEST_ASSERT_FALSE_MESSAGE(f.dev->recording(), "the audio task still records");
  TEST_ASSERT_TRUE(f.st().rec.phase == cpa::RecPhase::Idle);
  TEST_ASSERT_TRUE(f.st().rec.last.has && f.st().rec.last.saved);
  TEST_ASSERT_TRUE(f.failures.empty());
}

// docs/21 §9.2: a run in English whose Mac sends ASCII only never draws Han (other than
// the language names), in either language no text runs off the screen (every run checks
// that), and after it all the language is still three presses away, both ways.
static void test_fuzz_in_english() {
  const uint32_t seed = env_or("CPA_FUZZ_SEED", 11) + 10;
  Fuzz f(seed, env_store(), cpa::Lang::En);
  f.run_ms(1200);
  f.hello();
  f.run_ms(1500);
  TEST_ASSERT_TRUE(cpa::lang() == cpa::Lang::En);
  const uint64_t total = static_cast<uint64_t>(env_or("CPA_FUZZ_MINUTES", 3)) * 60000;
  while (f.T < total) f.episode_run(60000);
  f.drain();
  std::string all;
  for (const std::string& s : f.failures) all += s + "\n";
  TEST_ASSERT_TRUE_MESSAGE(f.failures.empty(), all.c_str());
  for (int i = 0; i < 2; ++i) {
    const int presses = f.escape();
    TEST_ASSERT_TRUE_MESSAGE(presses >= 0, (std::to_string(i) + " " + f.where()).c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.switch_language(), f.where().c_str());
  }
  const std::string after = f.failures.empty() ? "" : f.failures.front();
  TEST_ASSERT_TRUE_MESSAGE(f.failures.empty(), after.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_lost_end_event_is_not_a_dead_device);
  RUN_TEST(test_a_late_end_event_does_not_end_the_next_memo);
  RUN_TEST(test_fuzz_seeds);
  RUN_TEST(test_fuzz_in_english);
  return UNITY_END();
}
