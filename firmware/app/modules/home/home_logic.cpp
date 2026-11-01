#include "home_logic.h"

#include <cstdio>

#include "keymap.h"
#include "mando_eggs.h"
#include "protocol_limits.h"
#include "rec_core.h"
#include "reducers.h"

namespace cpa {
namespace {

constexpr uint8_t kHomeRows = kHomeTileCount / kHomeCols;
constexpr int64_t kDayMs = 24LL * 3600 * 1000;

HomeStep step(const HomeUi& ui, uint8_t sel, bool handled, const KeyEvent& press = KeyEvent{}) {
  HomeStep st;
  st.ui = ui;
  st.ui.sel = sel;
  st.press = press;
  st.handled = handled;
  return st;
}

char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch; }

// docs/20 D1: the tiles only Opt reaches (a meeting's consent box, the lock).
bool opt_only(char c) { return c == 'r' || c == 'l'; }

// The tile whose letter alone presses it (either case); -1: none.
int tile_of(char ch) {
  const char c = lower(ch);
  for (uint8_t i = 0; i < kHomeTileCount; ++i) {
    if (kHomeTiles[i].ev.kind == KeyEventKind::Chord && kHomeTiles[i].ev.key == c && !opt_only(c)) return i;
  }
  return -1;
}

// A letter that is no tile: the hint, once per visit (docs/20 D1).
HomeStep letter(const HomeUi& ui, uint8_t sel) {
  HomeStep st = step(ui, sel, false);
  st.ui.hinted = true;
  st.hint = ui.hinted ? nullptr : tr(kHomeLetterHint);
  return st;
}

uint8_t moved(uint8_t sel, Action a) {
  const int row = sel / kHomeCols;
  const int col = sel % kHomeCols;
  switch (a) {
    case Action::Up: return static_cast<uint8_t>((row + kHomeRows - 1) % kHomeRows * kHomeCols + col);
    case Action::Down: return static_cast<uint8_t>((row + 1) % kHomeRows * kHomeCols + col);
    case Action::TabPrev: return static_cast<uint8_t>(row * kHomeCols + (col + kHomeCols - 1) % kHomeCols);
    default: return static_cast<uint8_t>(row * kHomeCols + (col + 1) % kHomeCols);  // TabNext
  }
}

// docs/20 U22: waiting before idle; the longest wait first, but the newest idle (a session
// idle for days is not what needs you now). All rows share a snapshot: age_s compares.
bool before(const SessionRow& a, const SessionRow& b) {
  const bool aw = a.st == SessionState::Waiting;
  const bool bw = b.st == SessionState::Waiting;
  if (aw != bw) return aw;
  return aw ? a.age_s > b.age_s : a.age_s < b.age_s;
}

// Idle a day or more (as of the data's time): folded into "+N 个更早的".
bool older(const AppState& s, uint8_t i, uint32_t now) {
  const SessionRow& r = s.sessions.data.rows[i];
  return r.st != SessionState::Waiting && session_age_s(s.sessions, i, data_as_of(s, now)) >= kHomeOlderS;
}

// Local wall-clock milliseconds since 1970; none while the time is unknown.
Opt<int64_t> local_ms(const TimeInfo& t, uint32_t now) {
  const Opt<int64_t> wall = wall_ms(t, now);
  if (!wall.has) return Opt<int64_t>{};
  return some(wall.value + static_cast<int64_t>(t.tz_min) * 60000);
}

struct MonthDay {
  unsigned month = 1;
  unsigned day = 1;
};

// Days since 1970-01-01 -> month and day (H. Hinnant's civil_from_days).
MonthDay month_day(int64_t days) {
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  MonthDay md;
  md.month = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);
  md.day = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
  return md;
}

}  // namespace

HomeStep home_next(const HomeUi& ui, const KeyEvent& ev) {
  const uint8_t sel = ui.sel < kHomeTileCount ? ui.sel : 0;
  if (ev.kind == KeyEventKind::Char && ev.ch >= '1' && ev.ch <= '9') return step(ui, sel, true, home_chord(ev.ch));
  const int tile = ev.kind == KeyEventKind::Char ? tile_of(ev.ch) : -1;
  if (tile >= 0) return step(ui, sel, true, kHomeTiles[tile].ev);
  if (ev.kind == KeyEventKind::Char && lower(ev.ch) >= 'a' && lower(ev.ch) <= 'z') return letter(ui, sel);
  const Action a = keymap_lookup(Layer::List, ev).action;
  switch (a) {
    case Action::Up:
    case Action::Down:
    case Action::TabPrev:
    case Action::TabNext: return step(ui, moved(sel, a), true);
    case Action::Primary: return step(ui, sel, true, kHomeTiles[sel].ev);
    default: return step(ui, sel, false);  // Back keys go to the shell
  }
}

uint32_t home_running(const AppState& s) { return running_count(s.projects); }

int home_urgent(const AppState& s, uint32_t now) {
  const SessionsData& d = s.sessions.data;
  int best = -1;
  for (uint8_t i = 0; i < d.n; ++i) {
    if (!d.rows[i].need || older(s, i, now)) continue;
    if (best < 0 || before(d.rows[i], d.rows[best])) best = i;
  }
  return best;
}

uint8_t home_older(const AppState& s, uint32_t now) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < s.sessions.data.n; ++i) n = static_cast<uint8_t>(n + (s.sessions.data.rows[i].need && older(s, i, now)));
  return n;
}

RecBlock home_rec_block(const AppState& s) {
  const RecInfo& r = s.rec;
  if (!r.avail || r.sd_bad) return RecBlock::NoSd;
  if (r.free_known && r.free_mib < (rec::kFreeRefuseBytes >> 20)) return RecBlock::Full;
  if (r.files_n >= limits::kFilePendingMax) return RecBlock::Pending;
  if (s.device.bat_mv != 0 && s.device.bat_mv <= rec::kBatStopMv) return RecBlock::Battery;
  return RecBlock::None;
}

// docs/21: home's words. The hints bracket their keys in English (hint_layout.h rule 6);
// test_hint_bars checks each with the bar.
constexpr Txt kHintNoSd{"G0不能录：需要SD卡 Enter打开", "[G0]no rec: needs SD [Enter]open"};
constexpr Txt kHintFull{"G0不能录：SD卡满 Enter打开", "[G0]no rec: SD full [Enter]open"};
constexpr Txt kHintPending{"G0不能录：先连Mac传录音 Enter打开", "[G0]no rec: sync first [Enter]open"};
constexpr Txt kHintBattery{"G0不能录：电量低 Enter打开", "[G0]no rec: battery low [Enter]open"};
// English has no room for the double tap: the Tab menu ("G0 x2") and the lock card say it.
constexpr Txt kHintHold{"G0按住看到●REC再说/双击免提 Enter打开", "[G0]hold, talk after REC [Enter]open"};
constexpr Txt kHello{"你好", "Hello"};
constexpr Txt kMorning{"早上好", "Morning"};
constexpr Txt kNoon{"中午好", "Hi"};
constexpr Txt kAfternoon{"下午好", "Afternoon"};
constexpr Txt kEvening{"晚上好", "Evening"};

// No spaces inside the G0 item: the hint bar cuts words at spaces (hint_layout.h).
const char* home_hint(const AppState& s) {
  switch (home_rec_block(s)) {
    case RecBlock::NoSd: return tr(kHintNoSd);
    case RecBlock::Full: return tr(kHintFull);
    case RecBlock::Pending: return tr(kHintPending);
    case RecBlock::Battery: return tr(kHintBattery);
    default: return tr(kHintHold);  // D8: talk once ● REC shows; gh #51
  }
}

FixedStr<16> home_date(const TimeInfo& t, uint32_t now) {
  const Opt<int64_t> at = local_ms(t, now);
  if (!at.has) return FixedStr<16>{};
  const int64_t local = at.value;
  const int64_t days = local >= 0 ? local / kDayMs : (local - kDayMs + 1) / kDayMs;  // floor
  const MonthDay md = month_day(days);
  const int64_t wd = ((days + 4) % 7 + 7) % 7;  // 1970-01-01 was a Thursday
  char buf[48];
  if (lang() == Lang::En) {  // docs/21 §6.5: "Wed 10/21"
    std::snprintf(buf, sizeof buf, "%s %u/%u", tr(txt::kWeekdays[wd]), md.month, md.day);
  } else {
    std::snprintf(buf, sizeof buf, "%u/%u %s", md.month, md.day, tr(txt::kWeekdays[wd]));
  }
  return FixedStr<16>::from(buf);
}

HomeGrid home_grid(uint8_t tiles) {
  HomeGrid g;
  g.rows = static_cast<uint8_t>((tiles + kHomeCols - 1) / kHomeCols);
  if (g.rows <= 3) return g;
  g.y = 48;
  g.h = 14;
  g.pitch = 16;
  return g;
}

const char* home_greeting(const TimeInfo& t, uint32_t now) {
  const Opt<int64_t> at = local_ms(t, now);
  if (!at.has) return tr(kHello);
  const char* mood = mood_greeting(mando_mood(t, now).mood);  // v0.5.0: 夜深了, 周末好
  if (mood) return mood;
  const int64_t in_day = ((at.value % kDayMs) + kDayMs) % kDayMs;
  const int64_t hour = in_day / 3600000;
  if (hour >= 5 && hour < 11) return tr(kMorning);
  if (hour >= 11 && hour < 13) return tr(kNoon);
  if (hour >= 13 && hour < 18) return tr(kAfternoon);
  return tr(kEvening);
}

theme::MandoAct home_mando(const AppState& s, uint32_t now) {
  const uint32_t since = now - s.ui.moment_ms;
  if (pat_showing(s.ui.pat, now) || all_clear_showing(s.ui, now)) return theme::MandoAct::Hop;  // v0.5.0 eggs
  if (s.ui.moment == Moment::Saved && since < kHopMs) return theme::MandoAct::Hop;
  if (s.ui.moment == Moment::NoVoice && since < kNoVoiceMs) return theme::MandoAct::NoVoice;
  if (s.link.mode == LinkMode::Attached && !s.link.version_mismatch) return theme::MandoAct::Sniff;
  if (s.link.mode != LinkMode::Ready) return theme::MandoAct::Offline;  // docs/20 U19
  if (need_count(s.sessions) > 0) return theme::MandoAct::NeedsYou;
  return s.outbox.pending > 0 ? theme::MandoAct::Sniff : theme::MandoAct::Idle;
}

}  // namespace cpa
