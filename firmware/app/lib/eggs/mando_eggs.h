// Mando's easter eggs (v0.5.0, docs/04 "hidden keys"): small, silent, never in the way.
//   pat    Opt+W on home and on the lit lock card (never while recording): he hops with a
//          heart, says a short line (a random one of four, never the same twice in a row;
//          every 100th pat the rare one) and counts the pats ("pats ×N"; after "Pat pat"
//          just "×N") for 1.5 s, the count left out where it does not fit. RAM only.
//   mood   by the local clock: "Bedtime" from 23:30 to 5:00 (one z more), "Up early" from
//          5:00 to 7:00 (he is up), "Weekend" on Saturday and Sunday; home's greeting has a
//          night and a weekend word. The time unknown: no mood, as before.
//   clear  the sessions needing you went from some to none while home is in front: he hops
//          and his sign says "All clear" once, for kAllClearMs (the greeting already says
//          "All done"); never over a saved hop or his "?" still on show.
// Pure: the shell, the lock card and home draw them.
#pragma once

#include <cstddef>
#include <cstdint>

#include "app_state.h"
#include "fixed_str.h"
#include "theme_mando.h"
#include "txt.h"

namespace cpa {

constexpr uint32_t kPatShowMs = 1500;
constexpr uint32_t kRarePat = 100;      // every 100th pat
constexpr uint32_t kAllClearMs = 2500;

namespace egg {

// His four short lines, each a sign's width.
constexpr uint8_t kPatLineCount = 4;
constexpr SignTxt kPatLines[kPatLineCount] = {{"汪!", "Woof!"}, {"嘿嘿", "Hehe"}, {"摸摸", "Pat pat"}, {"再来", "Again"}};
constexpr Txt kRareLine{"今天也辛苦了", "You did great today"};
constexpr Txt kPatCount{"摸 \xC3\x97%lu", "pats \xC3\x97%lu"};  // ×
constexpr SignTxt kLateSign{"夜深了", "Bedtime"};
constexpr SignTxt kEarlySign{"早起鸟", "Up early"};
constexpr SignTxt kWeekendSign{"周末啦", "Weekend"};
constexpr Txt kLateGreeting{"夜深了", "Late night"};
constexpr Txt kWeekendGreeting{"周末好", "Weekend"};
constexpr Txt kAllClear{"搞定啦", "All clear"};

}  // namespace egg

// One more pat at now: counted, timed, his line picked.
PatState pat_next(const PatState& p, uint32_t now);
bool pat_showing(const PatState& p, uint32_t now);  // within kPatShowMs of the last pat
bool pat_rare(const PatState& p);                   // the 100th, 200th, ... pat
const char* pat_line(const PatState& p);            // in the current language
FixedStr<24> pat_count(const PatState& p);          // "pats ×3"; "×3" after "Pat pat"

enum class Mood : uint8_t { None, LateNight, EarlyBird, Weekend };

// What the clock makes of him on the lock card: what he does (Doze, or Idle for the early
// bird), his sign, and whether a fourth z joins the three.
struct MandoMood {
  Mood mood = Mood::None;
  theme::MandoAct act = theme::MandoAct::Doze;
  const char* sign = nullptr;
  bool extra_z = false;
};
MandoMood mando_mood(const TimeInfo& t, uint32_t now);
// Home's greeting for the mood ("Late night", "Weekend"); nullptr: the hour's own.
const char* mood_greeting(Mood m);

bool all_clear(unsigned need_before, unsigned need_after);  // some -> none
bool all_clear_showing(const UiFlags& ui, uint32_t now);
// No saved hop (kHopMs) or "?" (kNoVoiceMs) still on show: the all clear may take the moment.
bool all_clear_may_show(const UiFlags& ui, uint32_t now);

}  // namespace cpa
