// Mando's easter eggs (docs/04, hidden keys), the pure part:
//   pat    Opt+W: one more pat, a random short line (never the same twice in a row), every
//          100th the rare one; shown 1.5 s; "摸 ×N" / "pats ×N", "×N" after 摸摸 / Pat pat;
//   mood   by the local clock: 夜深了 from 23:30 to 5:00 (an extra z), 早起鸟 from 5:00 to
//          7:00 (awake), 周末啦 on Saturday and Sunday; the time unknown, as before;
//   clear  the sessions needing you went from some to none: 搞定啦 for 2.5 s, never over
//          a saved hop or his "?" still on show.
#include <unity.h>

#include <cstring>
#include <string>

#include "lang.h"
#include "mando_eggs.h"

using namespace cpa;
using theme::MandoAct;

void setUp() { set_lang(Lang::Zh); }
void tearDown() { set_lang(Lang::Zh); }

namespace {

// 2026-10-21 (a Wednesday) 00:00 + day days, hh:mm, in local time at tz_min east of UTC.
TimeInfo at_tz(int tz_min, int day, int hour, int minute) {
  TimeInfo t;
  t.known = true;
  t.tz_min = static_cast<int16_t>(tz_min);
  t.offset_ms = 1792540800000LL + ((day * 24LL + hour) * 60 + minute) * 60000LL - tz_min * 60000LL;
  return t;
}

TimeInfo at_local(int day, int hour, int minute) { return at_tz(480, day, hour, minute); }  // UTC+8

PatState pats(uint32_t n, uint32_t first_ms = 1000, uint32_t gap_ms = 137) {
  PatState p;
  for (uint32_t i = 0; i < n; ++i) p = pat_next(p, first_ms + i * gap_ms);
  return p;
}

bool is_line(const char* s) {
  for (const Txt& t : egg::kPatLines) {
    if (std::strcmp(s, t.zh) == 0) return true;
  }
  return false;
}

}  // namespace

static void test_a_pat_counts_and_shows_for_a_moment() {
  const PatState none;
  TEST_ASSERT_FALSE(pat_showing(none, 0));
  TEST_ASSERT_FALSE(pat_showing(none, 5000));
  const PatState p = pat_next(none, 5000);
  TEST_ASSERT_EQUAL_UINT32(1, p.n);
  TEST_ASSERT_EQUAL_UINT32(5000, p.at_ms);
  TEST_ASSERT_TRUE(pat_showing(p, 5000));
  TEST_ASSERT_TRUE(pat_showing(p, 5000 + kPatShowMs - 1));
  TEST_ASSERT_FALSE(pat_showing(p, 5000 + kPatShowMs));
  TEST_ASSERT_EQUAL_UINT32(2, pat_next(p, 5100).n);
  TEST_ASSERT_FALSE(pat_showing(pat_next(none, 0xFFFFFF00u), 0x00000600u));  // wrap-safe
  TEST_ASSERT_TRUE(pat_showing(pat_next(none, 0xFFFFFF00u), 0x00000100u));
}

static void test_lines_are_his_four_and_never_twice_in_a_row() {
  PatState p;
  bool seen[egg::kPatLineCount] = {};
  for (uint32_t i = 0; i < 60; ++i) {
    const PatState next = pat_next(p, 2000 + i * 211);
    TEST_ASSERT_TRUE(next.line < egg::kPatLineCount);
    if (p.n > 0) TEST_ASSERT_TRUE_MESSAGE(next.line != p.line, std::to_string(i).c_str());
    seen[next.line] = true;
    TEST_ASSERT_TRUE(is_line(pat_line(next)));
    p = next;
  }
  for (bool s : seen) TEST_ASSERT_TRUE(s);  // random enough to say all four
}

static void test_every_hundredth_pat_is_the_rare_line() {
  TEST_ASSERT_FALSE(pat_rare(pats(99)));
  TEST_ASSERT_TRUE(pat_rare(pats(100)));
  TEST_ASSERT_FALSE(pat_rare(pats(101)));
  TEST_ASSERT_TRUE(pat_rare(pats(200)));
  TEST_ASSERT_EQUAL_STRING("今天也辛苦了", pat_line(pats(100)));
  set_lang(Lang::En);
  TEST_ASSERT_EQUAL_STRING("You did great today", pat_line(pats(100)));
  TEST_ASSERT_FALSE(pat_rare(PatState{}));
}

static void test_the_count_in_both_languages() {
  PatState p = pats(3);
  p.line = 0;  // 汪!
  TEST_ASSERT_EQUAL_STRING("摸 \xC3\x97" "3", pat_count(p).c_str());  // 摸 ×3
  set_lang(Lang::En);
  TEST_ASSERT_EQUAL_STRING("pats \xC3\x97" "3", pat_count(p).c_str());
  TEST_ASSERT_EQUAL_STRING("pats \xC3\x97" "100", pat_count(pats(100)).c_str());
}

// After 摸摸 / Pat pat the word is not said twice: just "×N"; the rare line keeps it.
static void test_the_count_does_not_echo_pat_pat() {
  PatState p = pats(3);
  p.line = 2;
  TEST_ASSERT_EQUAL_STRING("摸摸", pat_line(p));
  TEST_ASSERT_EQUAL_STRING("\xC3\x97" "3", pat_count(p).c_str());
  set_lang(Lang::En);
  TEST_ASSERT_EQUAL_STRING("Pat pat", pat_line(p));
  TEST_ASSERT_EQUAL_STRING("\xC3\x97" "3", pat_count(p).c_str());
  PatState rare = pats(100);
  rare.line = 2;
  TEST_ASSERT_EQUAL_STRING("pats \xC3\x97" "100", pat_count(rare).c_str());
}

static void test_mood_follows_the_local_clock() {
  const struct {
    int day, h, m;
    Mood want;
  } cases[] = {
      {0, 23, 29, Mood::None},      {0, 23, 30, Mood::LateNight}, {1, 0, 0, Mood::LateNight},
      {1, 4, 59, Mood::LateNight},  {1, 5, 0, Mood::EarlyBird},   {1, 6, 59, Mood::EarlyBird},
      {1, 7, 0, Mood::None},        {1, 13, 0, Mood::None},       {3, 9, 0, Mood::Weekend},   // Saturday
      {4, 21, 0, Mood::Weekend},    {5, 9, 0, Mood::None},        {3, 23, 45, Mood::LateNight},  // Monday; Saturday night: late first
      {4, 6, 0, Mood::EarlyBird},   // Sunday morning: up early first
  };
  for (const auto& c : cases) {
    const std::string what = std::to_string(c.day) + " " + std::to_string(c.h) + ":" + std::to_string(c.m);
    TEST_ASSERT_TRUE_MESSAGE(mando_mood(at_local(c.day, c.h, c.m), 0).mood == c.want, what.c_str());
  }
}

static void test_mood_says_what_he_does_and_his_sign() {
  const MandoMood late = mando_mood(at_local(0, 23, 40), 0);
  TEST_ASSERT_TRUE(late.act == MandoAct::Doze);
  TEST_ASSERT_TRUE(late.extra_z);
  TEST_ASSERT_EQUAL_STRING("夜深了", late.sign);
  const MandoMood early = mando_mood(at_local(1, 5, 30), 0);
  TEST_ASSERT_TRUE(early.act == MandoAct::Idle);  // the early bird is up
  TEST_ASSERT_FALSE(early.extra_z);
  TEST_ASSERT_EQUAL_STRING("早起鸟", early.sign);
  const MandoMood weekend = mando_mood(at_local(3, 15, 0), 0);
  TEST_ASSERT_TRUE(weekend.act == MandoAct::Doze);
  TEST_ASSERT_EQUAL_STRING("周末啦", weekend.sign);
  set_lang(Lang::En);
  TEST_ASSERT_EQUAL_STRING("Bedtime", mando_mood(at_local(0, 23, 40), 0).sign);
  TEST_ASSERT_EQUAL_STRING("Up early", mando_mood(at_local(1, 5, 30), 0).sign);
  TEST_ASSERT_EQUAL_STRING("Weekend", mando_mood(at_local(3, 15, 0), 0).sign);
}

// Travel: west of UTC (UTC-7) and a half-hour zone (UTC+5:30), across UTC midnight and
// the Saturday that UTC and the local clock disagree on.
static void test_mood_in_other_time_zones() {
  const struct {
    int tz, day, h, m;
    Mood want;
  } cases[] = {
      {-420, 2, 16, 0, Mood::None},       // Friday 16:00, UTC Friday 23:00
      {-420, 2, 20, 0, Mood::None},       // Friday 20:00, UTC already Saturday 03:00
      {-420, 2, 23, 45, Mood::LateNight}, // Friday 23:45
      {-420, 3, 17, 30, Mood::Weekend},   // Saturday 17:30, UTC Sunday 00:30
      {-420, 4, 18, 0, Mood::Weekend},    // Sunday 18:00, UTC Monday 01:00
      {-420, 5, 6, 0, Mood::EarlyBird},   // Monday 06:00
      {-420, 5, 9, 0, Mood::None},        // Monday 09:00
      {330, 3, 5, 15, Mood::EarlyBird},   // Saturday 05:15, UTC Friday 23:45
      {330, 3, 5, 30, Mood::EarlyBird},   // Saturday 05:30, UTC Saturday 00:00
      {330, 3, 7, 0, Mood::Weekend},      // Saturday 07:00
      {330, 2, 23, 29, Mood::None},       // Friday 23:29
      {330, 2, 23, 30, Mood::LateNight},  // Friday 23:30, UTC 18:00
      {330, 5, 4, 59, Mood::LateNight},   // Monday 04:59, UTC Sunday 23:29
      {330, 5, 7, 0, Mood::None},         // Monday 07:00
  };
  for (const auto& c : cases) {
    const std::string what = std::to_string(c.tz) + " " + std::to_string(c.day) + " " + std::to_string(c.h) + ":" +
                             std::to_string(c.m);
    TEST_ASSERT_TRUE_MESSAGE(mando_mood(at_tz(c.tz, c.day, c.h, c.m), 777).mood == c.want, what.c_str());
  }
}

// Unknown time: as before, he dozes with his own sign.
static void test_no_time_no_mood() {
  const MandoMood m = mando_mood(TimeInfo{}, 123456);
  TEST_ASSERT_TRUE(m.mood == Mood::None);
  TEST_ASSERT_TRUE(m.act == MandoAct::Doze);
  TEST_ASSERT_FALSE(m.extra_z);
  TEST_ASSERT_EQUAL_STRING(theme::mando_sign(MandoAct::Doze), m.sign);
  TEST_ASSERT_TRUE(mando_mood(at_local(1, 13, 0), 0).act == MandoAct::Doze);  // a plain weekday
}

static void test_all_clear_is_some_to_none() {
  TEST_ASSERT_TRUE(all_clear(1, 0));
  TEST_ASSERT_TRUE(all_clear(5, 0));
  TEST_ASSERT_FALSE(all_clear(0, 0));
  TEST_ASSERT_FALSE(all_clear(2, 1));
  TEST_ASSERT_FALSE(all_clear(0, 1));
  UiFlags ui;
  TEST_ASSERT_FALSE(all_clear_showing(ui, 100));
  ui.moment = Moment::AllClear;
  ui.moment_ms = 1000;
  TEST_ASSERT_TRUE(all_clear_showing(ui, 1000));
  TEST_ASSERT_TRUE(all_clear_showing(ui, 1000 + kAllClearMs - 1));
  TEST_ASSERT_FALSE(all_clear_showing(ui, 1000 + kAllClearMs));
  ui.moment = Moment::Saved;
  TEST_ASSERT_FALSE(all_clear_showing(ui, 1000));
}

// The all clear never wipes a moment still on show: a saved hop, or his "?" (heard nothing).
static void test_all_clear_waits_for_a_voice_moment() {
  UiFlags ui;
  TEST_ASSERT_TRUE(all_clear_may_show(ui, 100));
  ui.moment = Moment::NoVoice;
  ui.moment_ms = 1000;
  TEST_ASSERT_FALSE(all_clear_may_show(ui, 1000));
  TEST_ASSERT_FALSE(all_clear_may_show(ui, 1000 + kNoVoiceMs - 1));
  TEST_ASSERT_TRUE(all_clear_may_show(ui, 1000 + kNoVoiceMs));
  ui.moment = Moment::Saved;
  TEST_ASSERT_FALSE(all_clear_may_show(ui, 1000 + kHopMs - 1));
  TEST_ASSERT_TRUE(all_clear_may_show(ui, 1000 + kHopMs));
  ui.moment = Moment::AllClear;
  TEST_ASSERT_TRUE(all_clear_may_show(ui, 1000));
}

static void test_the_all_clear_sign_is_not_the_greeting() {
  TEST_ASSERT_EQUAL_STRING("搞定啦", tr(egg::kAllClear));  // the greeting already says 都处理完了
  set_lang(Lang::En);
  TEST_ASSERT_EQUAL_STRING("All clear", tr(egg::kAllClear));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_pat_counts_and_shows_for_a_moment);
  RUN_TEST(test_lines_are_his_four_and_never_twice_in_a_row);
  RUN_TEST(test_every_hundredth_pat_is_the_rare_line);
  RUN_TEST(test_the_count_in_both_languages);
  RUN_TEST(test_the_count_does_not_echo_pat_pat);
  RUN_TEST(test_mood_follows_the_local_clock);
  RUN_TEST(test_mood_says_what_he_does_and_his_sign);
  RUN_TEST(test_mood_in_other_time_zones);
  RUN_TEST(test_no_time_no_mood);
  RUN_TEST(test_all_clear_is_some_to_none);
  RUN_TEST(test_all_clear_waits_for_a_voice_moment);
  RUN_TEST(test_the_all_clear_sign_is_not_the_greeting);
  return UNITY_END();
}
