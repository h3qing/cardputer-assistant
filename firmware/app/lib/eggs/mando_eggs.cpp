#include "mando_eggs.h"

#include <cstdio>

#include "reducers.h"

namespace cpa {
namespace {

constexpr int64_t kDayMs = 24LL * 3600 * 1000;
constexpr int64_t kMinMs = 60000;
constexpr int64_t kLateFrom = 23 * 60 + 30;  // minutes into the local day
constexpr int64_t kEarlyFrom = 5 * 60;
constexpr int64_t kEarlyTo = 7 * 60;
constexpr uint8_t kPatPatLine = 2;  // "Pat pat": the count says just "×N"

// A pseudo-random line from the time and the count, never the one before.
uint8_t pick_line(uint32_t now, uint32_t n, uint8_t prev, bool first) {
  const uint32_t h = (now * 2654435761u) ^ (n * 40503u);
  const uint8_t i = static_cast<uint8_t>((h >> 13) % egg::kPatLineCount);
  return (!first && i == prev) ? static_cast<uint8_t>((i + 1) % egg::kPatLineCount) : i;
}

struct LocalDay {
  int64_t minute;   // into the day
  int64_t weekday;  // 0 Sunday .. 6 Saturday
};

Opt<LocalDay> local_day(const TimeInfo& t, uint32_t now) {
  const Opt<int64_t> wall = wall_ms(t, now);
  if (!wall.has) return Opt<LocalDay>{};
  const int64_t local = wall.value + static_cast<int64_t>(t.tz_min) * kMinMs;
  const int64_t days = local >= 0 ? local / kDayMs : (local - kDayMs + 1) / kDayMs;  // floor
  const int64_t in_day = local - days * kDayMs;
  return some(LocalDay{in_day / kMinMs, ((days + 4) % 7 + 7) % 7});  // 1970-01-01 was a Thursday
}

Mood mood_of(const LocalDay& d) {
  if (d.minute >= kLateFrom || d.minute < kEarlyFrom) return Mood::LateNight;
  if (d.minute < kEarlyTo) return Mood::EarlyBird;
  return d.weekday == 0 || d.weekday == 6 ? Mood::Weekend : Mood::None;
}

}  // namespace

PatState pat_next(const PatState& p, uint32_t now) {
  PatState out;
  out.n = p.n + 1;
  out.at_ms = now;
  out.line = pick_line(now, out.n, p.line, p.n == 0);
  return out;
}

bool pat_showing(const PatState& p, uint32_t now) { return p.n > 0 && now - p.at_ms < kPatShowMs; }

bool pat_rare(const PatState& p) { return p.n > 0 && p.n % kRarePat == 0; }

const char* pat_line(const PatState& p) {
  if (pat_rare(p)) return tr(egg::kRareLine);
  return tr(egg::kPatLines[p.line < egg::kPatLineCount ? p.line : 0]);
}

FixedStr<24> pat_count(const PatState& p) {
  char buf[32];
  const bool bare = !pat_rare(p) && p.line == kPatPatLine;
  std::snprintf(buf, sizeof buf, bare ? "\xC3\x97%lu" : tr(egg::kPatCount), static_cast<unsigned long>(p.n));
  return FixedStr<24>::from(buf);
}

MandoMood mando_mood(const TimeInfo& t, uint32_t now) {
  MandoMood m;
  m.sign = theme::mando_sign(theme::MandoAct::Doze);
  const Opt<LocalDay> d = local_day(t, now);
  if (!d.has) return m;
  m.mood = mood_of(d.value);
  switch (m.mood) {
    case Mood::LateNight:
      m.sign = tr(egg::kLateSign);
      m.extra_z = true;
      break;
    case Mood::EarlyBird:
      m.sign = tr(egg::kEarlySign);
      m.act = theme::MandoAct::Idle;
      break;
    case Mood::Weekend: m.sign = tr(egg::kWeekendSign); break;
    default: break;
  }
  return m;
}

const char* mood_greeting(Mood m) {
  if (m == Mood::LateNight) return tr(egg::kLateGreeting);
  return m == Mood::Weekend ? tr(egg::kWeekendGreeting) : nullptr;
}

bool all_clear(unsigned need_before, unsigned need_after) { return need_before > 0 && need_after == 0; }

bool all_clear_showing(const UiFlags& ui, uint32_t now) {
  return ui.moment == Moment::AllClear && now - ui.moment_ms < kAllClearMs;
}

bool all_clear_may_show(const UiFlags& ui, uint32_t now) {
  const uint32_t since = now - ui.moment_ms;
  if (ui.moment == Moment::Saved) return since >= kHopMs;
  return ui.moment != Moment::NoVoice || since >= kNoVoiceMs;
}

}  // namespace cpa
