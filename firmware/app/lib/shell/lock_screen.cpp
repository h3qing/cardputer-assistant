#include "lock_screen.h"

#include <cstdio>
#include <cstring>

#include "mando_eggs.h"
#include "rec_overlay.h"
#include "rec_result.h"
#include "reducers.h"
#include "status_bar.h"
#include "text_fit.h"
#include "theme.h"
#include "theme_mando.h"
#include "words.h"

namespace cpa {
namespace {

// docs/19 §6.8 (mockup 06): the card, and below it the two lines that say how to unlock and
// record. While recording the card moves down, clear of the ● line. Inside the card, from
// its top left: Mando dozing with his z's and sign on the left, the clock column right of him.
constexpr Rect kCard{12, 8, 216, 94};
constexpr int16_t kCardRecY = 14;
constexpr uint8_t kCardRadius = 10;
constexpr int16_t kChipGap = 4;
constexpr int64_t kDayMs = 24LL * 3600 * 1000;
constexpr int16_t kMandoDx = 13;
constexpr int16_t kMandoDy = 9;
constexpr int16_t kSignDx = 26;  // centre
constexpr int16_t kSignDy = 59;
constexpr int16_t kColumnDx = 65;  // 已锁屏, the date line and the chips
constexpr int16_t kClockDx = 63;   // the clock's first cell (its ink starts a pixel in)
constexpr int16_t kClockDy = 21;
constexpr int16_t kBigDy = 20;     // docs/20 B1: the words in the clock's place, and below them
constexpr int16_t kDetailDy = 35;
constexpr int16_t kDateDy = 50;
constexpr int16_t kColumnW = kCard.w - kColumnDx - 6;

// docs/21: the lock card's words. The column right of Mando is kColumnW (145 px).
constexpr Txt kHandsFreeMark{"免提", "Hands-free"};
constexpr Txt kWaitingN{"等你 %u", "Waiting %u"};
constexpr Txt kSynced{"\xE2\x88\x9A 已同步", "\xE2\x88\x9A synced"};  // √ (no ✓ in efontCN_12)
constexpr Txt kFilesN{"录%u", "R%u"};
constexpr Txt kKeptN{"留%u", "kept %u"};  // docs/28 §5: kept copies are on the card, plain
constexpr SignTxt kNotRecSign{"没在录", "No rec"};
constexpr SignTxt kMissedSign{"没录上", "Missed"};
constexpr SignTxt kSaving{"保存中…", "Saving…"};
constexpr Txt kNotRecording{"没有在录", "Not recording", kColumnW};
constexpr Txt kLetGoTapHold{"松开，点一下再按住", "Let go, tap, then hold", kColumnW};
constexpr Txt kLocked{"已锁屏", "Locked", kColumnW};
constexpr Txt kLockedOffline{"已锁屏 · 离线", "Locked · offline", kColumnW};

struct MonthDay {
  unsigned month = 1;
  unsigned day = 1;
};

// Days since 1970-01-01 -> month and day (H. Hinnant's civil_from_days), as the home
// launcher's date (modules/home cannot be reached from lib/shell).
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

// The ● line shows while recording and (docs/20 D8) from the press of a G0 that would record
// on an armed card.
bool mark_shown(const AppState& s) { return rec_on_screen(s.rec.phase) || s.ui.memo_ready; }
bool mark_ready(const AppState& s) { return !rec_on_screen(s.rec.phase) || s.rec.phase == RecPhase::Cue; }

// "●42:10 ★2" (meeting) or "● REC 0:12" (memo; hands free "● 免提 0:12", gh #51: the dark
// screen's only sign of how it ends), red; "● 准备" in mustard from the press until the
// microphone is up, as on the unlocked pages, so nobody starts talking too early.
FixedStr<32> rec_mark_text(const AppState& s, uint32_t now) {
  const RecInfo& r = s.rec;
  if (mark_ready(s)) return FixedStr<32>::from(tr(txt::kMemoReady));
  const FixedStr<16> clock = rec_clock_text(now - r.started_ms);
  char buf[32];
  if (r.mode == RecMode::Meeting) {
    std::snprintf(buf, sizeof buf, "\xE2\x97\x8F%s \xE2\x98\x85%u", clock.c_str(), static_cast<unsigned>(r.stars));
  } else {
    std::snprintf(buf, sizeof buf, "\xE2\x97\x8F %s %s", r.latched ? tr(kHandsFreeMark) : "REC", clock.c_str());
  }
  return FixedStr<32>::from(buf);
}

// docs/19 §6.8 (the judges' notes): the line sits in the status bar's slot, a capsule at the
// top left (Rec on RecBg, 4.8:1); kMarkClear px round it stay free of the glow.
constexpr int16_t kMarkClear = 2;

Rect rec_mark_rect(const Canvas& c, const AppState& s, uint32_t now) {
  if (!mark_shown(s)) return Rect{};
  const int16_t w = static_cast<int16_t>(c.textWidth(rec_mark_text(s, now).c_str()) + 6);
  return Rect{1, 0, w, kBarH};
}

void draw_rec_mark(Canvas& c, const AppState& s, uint32_t now) {
  const Rect r = rec_mark_rect(c, s, now);
  const theme::ToneInk t = theme::tone_ink(mark_ready(s) ? theme::Tone::Warn : theme::Tone::Rec);
  c.fillRound(r, kBarH / 2, t.fill, Color::Bg);
  c.text(4, 0, rec_mark_text(s, now).c_str(), theme::over(t.ink));
}

// The glow on r, but not within kMarkClear of the ● capsule (top left): the part of r below
// that, and the part right of it.
void glow_clear_of_mark(Canvas& c, const Rect& r, const Rect& mark) {
  if (mark.w <= 0) return c.glow(r);
  const int16_t right = static_cast<int16_t>(mark.x + mark.w + kMarkClear);
  const int16_t below = static_cast<int16_t>(mark.y + mark.h + kMarkClear);
  const int16_t top = r.y > below ? r.y : below;
  if (top < r.y + r.h) c.glow(Rect{r.x, top, r.w, static_cast<int16_t>(r.y + r.h - top)});
  const int16_t left = r.x > right ? r.x : right;
  if (r.y < below && left < r.x + r.w) {
    c.glow(Rect{left, r.y, static_cast<int16_t>(r.x + r.w - left), static_cast<int16_t>(below - r.y)});
  }
}

// Days since 1970-01-01 in local time; none while the time is unknown.
Opt<int64_t> local_days(const TimeInfo& t, uint32_t now) {
  const Opt<int64_t> wall = wall_ms(t, now);
  if (!wall.has) return Opt<int64_t>{};
  const int64_t local = wall.value + static_cast<int64_t>(t.tz_min) * 60000;
  return some(local >= 0 ? local / kDayMs : (local - kDayMs + 1) / kDayMs);  // floor
}

const char* weekday_of(int64_t days) {
  return tr(txt::kWeekdays[((days + 4) % 7 + 7) % 7]);  // 1970-01-01 was a Thursday
}

// The Mac's picture behind 等你 (docs/20 U19, unknown is not zero): live; kept from before
// the Mac went away (home says 现在离线); or none at all.
enum class NeedData : uint8_t { None, Live, Kept };

NeedData need_data(const AppState& s) {
  const DataLink l = data_link(s);
  if (l == DataLink::Live) return NeedData::Live;
  const bool kept = l == DataLink::Stale || (l == DataLink::NoCpad && s.sessions.live);
  return kept ? NeedData::Kept : NeedData::None;
}

// 等你 N, mustard while N > 0 and live; none without a picture ("等你 0" would read as nobody
// waits). Into chip; false: no chip.
bool need_chip(const AppState& s, char (&chip)[24], theme::Tone& tone) {
  const NeedData d = need_data(s);
  if (d == NeedData::None) return false;
  const unsigned need = need_count(s.sessions);
  std::snprintf(chip, sizeof chip, tr(kWaitingN), need);
  tone = need > 0 && d == NeedData::Live ? theme::Tone::Warn : theme::Tone::Neutral;
  return true;
}

// The 等你 chip (need_chip); then, one name each as on the status bar (docs/20 U21), 录N
// and ↑N while not zero, or "√ 已同步" once neither waits and the Mac is there (B3): counts
// only, from x.
void draw_chips(Canvas& c, const AppState& s, int16_t x, int16_t y) {
  char chip[4][24];
  theme::Tone tone[4] = {theme::Tone::Neutral, theme::Tone::Neutral, theme::Tone::Neutral, theme::Tone::Neutral};
  int n = need_chip(s, chip[0], tone[0]) ? 1 : 0;
  if (s.link.mode == LinkMode::Ready && s.rec.files_n == 0 && s.outbox.pending == 0) {
    std::snprintf(chip[n], sizeof chip[n], "%s", tr(kSynced));
    tone[n++] = theme::Tone::Ok;
  }
  if (s.rec.files_n > 0) std::snprintf(chip[n++], sizeof chip[0], tr(kFilesN), static_cast<unsigned>(s.rec.files_n));
  if (s.outbox.pending > 0) {
    std::snprintf(chip[n++], sizeof chip[0], "\xE2\x86\x91%u", static_cast<unsigned>(s.outbox.pending));  // ↑N
  }
  if (s.rec.kept.segs > 0) std::snprintf(chip[n++], sizeof chip[0], tr(kKeptN), static_cast<unsigned>(s.rec.kept.segs));
  theme::PillSize ps;
  ps.h = theme::kSignH;
  ps.pad = 5;
  // docs/28: four chips can be wider than the card: the last ones that do not fit are left out
  const int16_t right = static_cast<int16_t>(kCard.x + kCard.w - 6);
  for (int i = 0; i < n && x + theme::pill_width(c, chip[i], ps) <= right; ++i) {
    x = static_cast<int16_t>(x + theme::pill(c, x, y, chip[i], tone[i], Color::Sheet, ps) + kChipGap);
  }
}

void draw_sleeper(Canvas& c, int16_t cx, int16_t cy) {
  theme::mando(c, static_cast<int16_t>(cx + kMandoDx), static_cast<int16_t>(cy + kMandoDy), theme::MandoSize::M46,
               theme::MandoAct::Doze, 0, Color::Sheet);
}

// The first `z` of his three z's climbing up and right (z, z, Z, each brighter); late at
// night (v0.5.0, mando_eggs.h) a fourth, small one beside the Z once all three are lit.
void draw_zs(Canvas& c, int16_t cx, int16_t cy, uint8_t z, bool extra) {
  static const struct {
    int16_t dx, dy;
    const char* z;
    Color ink;
  } kZ[] = {{35, 14, "z", Color::Ink3}, {41, 7, "z", Color::Ink2}, {47, -1, "Z", Color::Ink}, {54, 6, "z", Color::Ink2}};
  const uint8_t n = extra && z >= 3 ? 4 : z;
  for (uint8_t i = 0; i < n && i < 4; ++i) {
    c.text(static_cast<int16_t>(cx + kZ[i].dx), static_cast<int16_t>(cy + kZ[i].dy), kZ[i].z, theme::over(kZ[i].ink));
  }
}

// His sign on the card: what he does, or (docs/20 B1) 没在录 under a swallowed hold, 没录上
// when a recording did not start; over the clock, the clock's mood (v0.5.0: 夜深了 ...).
void draw_card_sign(Canvas& c, const AppState& s, theme::MandoAct act, int16_t cx, int16_t cy, uint32_t now) {
  const char* text = theme::mando_sign(act);
  theme::Tone tone = theme::mando_sign_tone(act);
  if (lock_mando_act(s) == theme::MandoAct::Doze) {
    const MandoMood m = mando_mood(s.time, now);
    text = m.sign;
    tone = theme::mando_sign_tone(m.act);
  }
  if (s.ui.lock_held) {
    text = tr(kNotRecSign);
    tone = theme::Tone::Warn;
  } else if (act == theme::MandoAct::NoVoice && !rec_card_result(s.rec.last).started) {
    text = tr(kMissedSign);
  }
  theme::sign(c, static_cast<int16_t>(cx + kSignDx), static_cast<int16_t>(cy + kSignDy), text, tone, Color::Sheet);
}

// Mando on the card: asleep with his z's, or awake (lock_card_act) and still; patted, with
// his heart (v0.5.0).
void draw_card_mando(Canvas& c, const AppState& s, int16_t cx, int16_t cy, uint8_t z, uint32_t now) {
  const theme::MandoAct act = lock_card_act(s, now);
  const int16_t x = static_cast<int16_t>(cx + kMandoDx);
  const int16_t y = static_cast<int16_t>(cy + kMandoDy);
  if (act == theme::MandoAct::Doze) {
    draw_sleeper(c, cx, cy);
    draw_zs(c, cx, cy, z, mando_mood(s.time, now).extra_z);
  } else {
    theme::mando(c, x, y, theme::MandoSize::M46, act, theme::mando_rest_pose(act), Color::Sheet);
  }
  if (lock_mando_act(s) == theme::MandoAct::Doze && pat_showing(s.ui.pat, now)) {
    theme::heart(c, theme::MandoSize::M46, x, y, Color::Sheet);
  }
  draw_card_sign(c, s, act, cx, cy, now);
}

// The head of the column: 已锁屏 (· 离线), or a pat's line and count (v0.5.0), the count
// left out where both do not fit.
void draw_head(Canvas& c, const AppState& s, int16_t col, int16_t y, uint32_t now) {
  const Style ink3{Color::Ink3, Color::Sheet};
  if (lock_mando_act(s) != theme::MandoAct::Doze || !pat_showing(s.ui.pat, now)) {
    return c.text(col, y, tr(need_data(s) == NeedData::Kept ? kLockedOffline : kLocked), ink3);  // U19
  }
  const char* line = pat_line(s.ui.pat);
  c.text(col, y, line, Style{Color::Ink, Color::Sheet});
  const FixedStr<24> count = pat_count(s.ui.pat);
  const int16_t x = static_cast<int16_t>(col + c.textWidth(line) + 6);
  if (x + c.textWidth(count.c_str()) <= col + kColumnW) c.text(x, y, count.c_str(), ink3);
}

int16_t card_y(const AppState& s) { return mark_shown(s) ? kCardRecY : kCard.y; }

// The large clock in Nunito; "--:--" in plain text while the time is unknown.
void draw_clock(Canvas& c, const AppState& s, int16_t cx, int16_t cy, uint32_t now) {
  const FixedStr<8> clock = clock_text(s.time, now);
  const int16_t x = static_cast<int16_t>(cx + kClockDx);
  const int16_t y = static_cast<int16_t>(cy + kClockDy);
  if (wall_ms(s.time, now).has) {
    c.digits(x, y, clock.c_str(), DigitFont::Clock, Color::Ink, Color::Sheet);
  } else {
    c.text(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 6), clock.c_str(), Style{Color::Ink2, Color::Sheet});
  }
}

// One centred line of keycaps and words below the card, written as a hint ("先点 [G0]再按住说",
// hint_layout.h): each key a keycap, each run of words as it is, 3 px apart.
struct HintPart {
  bool key;
  FixedStr<63> text;
};

struct HintParts {
  uint8_t n = 0;
  HintPart p[2 * theme::kHintItemsMax];
};

HintParts hint_parts(const char* line) {
  const theme::HintItems h = theme::hint_items(line);
  HintParts out;
  for (uint8_t i = 0; i < h.n; ++i) {
    const theme::HintItem& it = h.items[i];
    if (it.key_len > 0) out.p[out.n++] = HintPart{true, FixedStr<63>::from(line + it.key_at, it.key_len)};
    if (it.text_len > 0) out.p[out.n++] = HintPart{false, FixedStr<63>::from(line + it.text_at, it.text_len)};
  }
  return out;
}

void hint_line(Canvas& c, int16_t y, const char* line) {
  const HintParts parts = hint_parts(line);
  int16_t w = static_cast<int16_t>(parts.n > 0 ? 3 * (parts.n - 1) : 0);
  for (uint8_t i = 0; i < parts.n; ++i) {
    const char* t = parts.p[i].text.c_str();
    w = static_cast<int16_t>(w + (parts.p[i].key ? theme::keycap_width(c, t) : c.textWidth(t)));
  }
  int16_t x = static_cast<int16_t>((kScreenW - w) / 2);
  for (uint8_t i = 0; i < parts.n; ++i) {
    const char* t = parts.p[i].text.c_str();
    theme::Keycap k;
    if (parts.p[i].key && std::strcmp(t, "G0") == 0) k.ink = Color::Accent;
    if (parts.p[i].key) {
      x = static_cast<int16_t>(x + theme::keycap(c, x, y, t, k) + 3);
    } else {
      c.text(x, y, t, Style{Color::Ink3, Color::Bg});
      x = static_cast<int16_t>(x + c.textWidth(t) + 3);
    }
  }
}

// The lines under the card (docs/21 §6.7): keys bracketed, so each language has its order.
constexpr Txt kUnlockLine{"[Opt+L]再 [Enter]解锁", "[Opt+L]then [Enter]to unlock"};
constexpr Txt kMemoLine{"先点 [G0]再按住说 · 点两下免提", "Tap [G0]then hold · 2 taps: hands-free"};
constexpr Txt kStarLine{"开会中： [G0]加 ★", "Meeting: [G0]adds ★"};
constexpr Txt kHandsFreeLine{"免提录音中：按 [G0]结束", "Hands-free: [G0]ends it"};

// One centred line of words.
void word_line(Canvas& c, int16_t y, const char* text, Color ink) {
  c.text(static_cast<int16_t>((kScreenW - c.textWidth(text)) / 2), y, text, Style{ink, Color::Bg});
}

// The second line: how G0 works now: a meeting's star, a hands-free memo's end (gh #51: a G0
// press on the lit card), the memo hold and the double tap; 保存中… while a recording ends.
// How it ended is the card's own (draw_body, docs/20 B1).
void draw_hints(Canvas& c, bool recording, const RecInfo& r) {
  hint_line(c, recording ? 110 : 108, tr(kUnlockLine));
  const int16_t y = recording ? 123 : 122;
  if (r.phase == RecPhase::Finishing) return word_line(c, y, tr(kSaving), Color::Ink3);
  if (r.mode == RecMode::Meeting && rec_on_screen(r.phase)) return hint_line(c, y, tr(kStarLine));
  if (r.latched) return hint_line(c, y, tr(kHandsFreeLine));
  hint_line(c, y, tr(kMemoLine));
}

// "10/21 周三 · 电 94%" (v0.4's format); the battery yellow under 30 %, red from 17 % (docs/20
// B3), as on the status bar the lock hides.
void draw_date_line(Canvas& c, const AppState& s, int16_t x, int16_t y, uint32_t now) {
  const FixedStr<16> date = lock_date_text(s.time, now);
  char line[48] = "";
  if (!date.empty()) std::snprintf(line, sizeof line, "%s · ", date.c_str());
  if (line[0] != '\0') c.text(x, y, line, Style{Color::Ink2, Color::Sheet});
  const int pct = battery_pct(s.device.bat_mv);
  char bat[24];
  std::snprintf(bat, sizeof bat, "%s", tr(txt::kBatNone));
  if (pct >= 0) std::snprintf(bat, sizeof bat, tr(txt::kBatPct), pct);
  const Color ink = battery_ink(pct);
  c.text(static_cast<int16_t>(x + c.textWidth(line)), y, bat, Style{ink == Color::Ink3 ? Color::Ink2 : ink, Color::Sheet});
}

// Words in the clock's place (docs/20 B1): one line in bold, in ink, and a detail under it.
void draw_big(Canvas& c, int16_t x, int16_t cy, const char* line, Color ink, const char* detail) {
  const int16_t y = static_cast<int16_t>(cy + kBigDy);
  c.text(x, y, line, Style{ink, Color::Sheet});
  c.text(static_cast<int16_t>(x + 1), y, line, theme::over(ink));
  if (detail[0] == '\0') return;
  draw_fit(c, x, static_cast<int16_t>(cy + kDetailDy), kColumnW, detail, Style{Color::Ink2, Color::Sheet});
}

// The clock; or what matters more now: a G0 hold the lock swallows (U3c), how the last
// recording ended (U6).
void draw_body(Canvas& c, const AppState& s, int16_t cx, int16_t cy, uint32_t now) {
  const int16_t col = static_cast<int16_t>(cx + kColumnDx);
  if (s.ui.lock_held) return draw_big(c, col, cy, tr(kNotRecording), Color::Warn, tr(kLetGoTapHold));
  if (lock_mando_act(s) == theme::MandoAct::Doze) return draw_clock(c, s, cx, cy, now);
  const CardResult r = rec_card_result(s.rec.last);
  draw_big(c, col, cy, r.line.c_str(), r.saved ? Color::Ok : Color::Warn, r.detail.c_str());
}

// The lit card: counts only, never a project or session name or any text (docs/13 §7.1).
// The still glow (docs/19 §2.3) lights the backdrop's top left, outside the card.
void draw_card(Canvas& c, const AppState& s, uint32_t now, uint8_t z) {
  const bool recording = mark_shown(s);
  Rect card = kCard;
  card.y = card_y(s);
  theme::card(c, card, theme::CardKind::Sheet, Color::Bg, kCardRadius);
  glow_clear_of_mark(c, Rect{0, 0, kScreenW, kScreenH}, rec_mark_rect(c, s, now));
  draw_card_mando(c, s, card.x, card.y, z, now);
  const int16_t col = static_cast<int16_t>(card.x + kColumnDx);
  draw_head(c, s, col, static_cast<int16_t>(card.y + 6), now);
  draw_body(c, s, card.x, card.y, now);
  draw_date_line(c, s, static_cast<int16_t>(col + 1), static_cast<int16_t>(card.y + kDateDy), now);
  draw_chips(c, s, col, static_cast<int16_t>(card.y + 69));
  draw_hints(c, recording, s.rec);
}

}  // namespace

theme::MandoAct lock_mando_act(const AppState& s) {
  if (s.ui.lock_held) return theme::MandoAct::NoVoice;
  const RecLast& l = s.rec.last;
  if (!l.has || (l.seen && !l.locked) || rec_on_screen(s.rec.phase)) return theme::MandoAct::Doze;  // D7
  return l.saved ? theme::MandoAct::Hop : theme::MandoAct::NoVoice;
}

theme::MandoAct lock_card_act(const AppState& s, uint32_t now) {
  const theme::MandoAct act = lock_mando_act(s);
  if (act != theme::MandoAct::Doze) return act;
  if (pat_showing(s.ui.pat, now)) return theme::MandoAct::Hop;
  return mando_mood(s.time, now).act;
}

FixedStr<16> lock_date_text(const TimeInfo& t, uint32_t now) {
  const Opt<int64_t> days = local_days(t, now);
  if (!days.has) return FixedStr<16>{};
  const MonthDay md = month_day(days.value);
  char buf[48];
  if (lang() == Lang::En) {  // docs/21 §6.5: "Wed 10/21", the one place the order changes
    std::snprintf(buf, sizeof buf, "%s %u/%u", weekday_of(days.value), md.month, md.day);
  } else {
    std::snprintf(buf, sizeof buf, "%u/%u %s", md.month, md.day, weekday_of(days.value));
  }
  return FixedStr<16>::from(buf);
}

FixedStr<8> weekday_text(const TimeInfo& t, uint32_t now) {
  const Opt<int64_t> days = local_days(t, now);
  return days.has ? FixedStr<8>::from(weekday_of(days.value)) : FixedStr<8>{};
}

// The ● line last: its capsule on Bg, the glow kept clear of it.
void render_lock_screen(Canvas& c, const AppState& s, bool card, uint32_t now, uint8_t z) {
  c.fill(Rect{0, 0, kScreenW, kScreenH}, Color::Bg);
  if (card) draw_card(c, s, now, z);
  if (mark_shown(s)) draw_rec_mark(c, s, now);
}

// The z's cells: a row of glow above the card, its Rim top edge, then the card's Sheet with
// the edge of Mando's head in it (the card's straight top: no rounded corner here).
Rect lock_z_part(Canvas& c, const AppState& s, uint8_t z, uint32_t now) {
  const int16_t cx = kCard.x;
  const int16_t cy = card_y(s);
  const bool extra = mando_mood(s.time, now).extra_z;
  const Rect r{static_cast<int16_t>(cx + 35), static_cast<int16_t>(cy - 1), static_cast<int16_t>(extra ? 25 : 18), 27};
  c.fill(r, Color::Bg);
  glow_clear_of_mark(c, r, rec_mark_rect(c, s, now));
  c.fill(Rect{r.x, cy, r.w, 1}, Color::Rim);
  c.fill(Rect{r.x, static_cast<int16_t>(cy + 1), r.w, static_cast<int16_t>(r.h - 2)}, Color::Sheet);
  draw_sleeper(c, cx, cy);
  draw_zs(c, cx, cy, z, extra);  // late at night a fourth (v0.5.0)
  return r;
}

}  // namespace cpa
