#include "status_bar.h"

#include <cstdio>

#include "lock_screen.h"
#include "rec_overlay.h"
#include "reducers.h"
#include "text_fit.h"
#include "theme.h"
#include "theme_art.h"
#include "words.h"

namespace cpa {

// docs/21 §6.7: the status bar's words; English is narrower than Chinese.
constexpr Txt kOnline{"● 在线", "● online"};
constexpr Txt kSyncing{"◎ 同步", "◎ sync"};
constexpr Txt kNoDaemon{"◇ 无响应", "◇ no reply"};
constexpr Txt kIncompatible{"◇ 版本不兼容", "◇ mismatch"};
constexpr Txt kOffline{"○ 离线", "○ offline"};
constexpr Txt kFilesN{"录%u", "R%u"};
namespace {

constexpr int kBatEmptyMv = 3300;
constexpr int kBatFullMv = 4150;
constexpr int64_t kDayMs = 24LL * 3600 * 1000;
constexpr int16_t kRight = kScreenW - theme::kGutter;
constexpr int16_t kHeadAdvance = 14;  // the 11 px head and its gap
constexpr int16_t kGroupGap = 6;      // the left group ends this far before the right one

Color glyph_color(LinkGlyph g) {
  switch (g) {
    case LinkGlyph::Syncing: return Color::Accent;
    case LinkGlyph::NoDaemon: return Color::Warn;
    case LinkGlyph::Incompatible: return Color::Error;
    default: return Color::Ink3;
  }
}

// The fixed left slot (docs/19 §6.1): while recording "● 12:31" in Rec on a RecBg capsule,
// never dimmed or breathing, the clock 6 px after the ● (mockups 04-05); docs/20 D8: from
// the press of a G0 that would record, "● Wait" in Warn on WarnBg. Empty: "".
FixedStr<24> rec_slot_text(const AppState& s, uint32_t now) {
  if (!rec_on_screen(s.rec.phase)) return FixedStr<24>::from(s.ui.memo_ready ? tr(txt::kMemoReady) : "");
  char buf[24];
  const uint32_t ms = s.rec.phase == RecPhase::Cue ? 0 : now - s.rec.started_ms;
  std::snprintf(buf, sizeof buf, "\xE2\x97\x8F %s", rec_clock_text(ms).c_str());  // ●
  return FixedStr<24>::from(buf);
}

// Returns the x after the slot.
int16_t draw_rec(Canvas& c, const AppState& s, uint32_t now) {
  const FixedStr<24> text = rec_slot_text(s, now);
  if (text.empty()) return 3;
  const theme::ToneInk t = theme::tone_ink(rec_on_screen(s.rec.phase) ? theme::Tone::Rec : theme::Tone::Warn);
  const int16_t w = static_cast<int16_t>(c.textWidth(text.c_str()) + 6);
  c.fillRound(Rect{1, 0, w, kBarH}, kBarH / 2, t.fill, Color::Bg);
  c.text(4, 0, text.c_str(), theme::over(t.ink));
  return static_cast<int16_t>(1 + w + 4);
}

const char* link_label(LinkGlyph g) { return g == LinkGlyph::Syncing ? "\xE2\x97\x8E" : link_glyph_text(g); }  // ◎

int16_t link_width(const Canvas& c, LinkGlyph g) {
  if (g == LinkGlyph::Online) return kHeadAdvance;
  const int16_t head = g == LinkGlyph::Syncing ? kHeadAdvance : 0;
  return static_cast<int16_t>(head + c.textWidth(link_label(g)) + 6);
}

// Online: Mando's head; syncing: the head and ◎; otherwise the v0.4 glyph and word.
int16_t draw_link(Canvas& c, int16_t x, LinkGlyph g) {
  const int16_t end = static_cast<int16_t>(x + link_width(c, g));
  if (g == LinkGlyph::Online || g == LinkGlyph::Syncing) {
    c.art(x, 1, ArtId::Head);
    x = static_cast<int16_t>(x + kHeadAdvance);
    if (g == LinkGlyph::Online) return end;
  }
  c.text(x, 0, link_label(g), Style{glyph_color(g), Color::Bg});
  return end;
}

// docs/20 B3: the parts of the bar that may give way, each with the gap that goes with it
// (after it on the left, before it on the right); w 0: not drawn. Zero counts are never
// drawn. Too full, they go in kDropOrder; ●, the link, !N, the clock and the battery stay.
enum Part : uint8_t { kUp, kNeed, kFiles, kSd, kKb, kDay, kPartN };
constexpr Part kDropOrder[] = {kDay, kFiles, kUp, kKb, kSd};

struct Parts {
  FixedStr<24> text[kPartN];
  int16_t w[kPartN] = {};
};

constexpr int16_t kNeedPad = 3;

void set_part(const Canvas& c, Parts& p, Part k, const char* text, int16_t extra) {
  p.text[k] = FixedStr<24>::from(text);
  p.w[k] = static_cast<int16_t>(c.textWidth(text) + extra);
}

Parts parts_of(const Canvas& c, const AppState& s, uint32_t now) {
  Parts p;
  char buf[24];
  if (s.outbox.pending > 0) {
    std::snprintf(buf, sizeof buf, "\xE2\x86\x91%u", static_cast<unsigned>(s.outbox.pending));  // ↑N
    set_part(c, p, kUp, buf, 6);
  }
  const uint16_t need = need_count(s.sessions);
  if (need > 0) {
    std::snprintf(buf, sizeof buf, "!%u", static_cast<unsigned>(need));
    set_part(c, p, kNeed, buf, 2 * kNeedPad + 4);  // a mustard pill
  }
  if (s.rec.files_n > 0) {
    std::snprintf(buf, sizeof buf, tr(kFilesN), static_cast<unsigned>(s.rec.files_n));
    set_part(c, p, kFiles, buf, 4);
  }
  if (s.rec.sd_bad) set_part(c, p, kSd, "\xE2\x80\xBBSD", 4);  // ※SD
  const unsigned kb = static_cast<unsigned>(s.device.ghost_n) + s.device.kb_reinit_n;
  if (kb > 0 && now - s.device.kb_warn_ms < kKbWarnShowMs) {
    std::snprintf(buf, sizeof buf, "\xE2\x80\xBBk%u", kb);  // ※kN (⚠ is not in efontCN_12)
    set_part(c, p, kKb, buf, 6);
  }
  const FixedStr<8> day = weekday_text(s.time, now);
  if (!day.empty()) set_part(c, p, kDay, day.c_str(), 4);
  return p;
}

// Drops parts in kDropOrder until `fixed` (what always stays) and the rest fit the bar.
void fit(Parts& p, int16_t fixed) {
  int total = fixed;
  for (int16_t w : p.w) total += w;
  for (Part k : kDropOrder) {
    if (total <= kScreenW) return;
    total -= p.w[k];
    p.w[k] = 0;
  }
}

FixedStr<8> battery_text(int pct) {
  char buf[16];  // room for any int: GCC cannot prove pct is 0..100
  if (pct < 0) return FixedStr<8>::from("--%");
  std::snprintf(buf, sizeof buf, "%d%%", pct);
  return FixedStr<8>::from(buf);
}

// "↑3 [!2] R5" after the link.
void draw_left(Canvas& c, int16_t x, const Parts& p) {
  if (p.w[kUp] > 0) c.text(x, 0, p.text[kUp].c_str(), Style{Color::Ink2, Color::Bg});
  x = static_cast<int16_t>(x + p.w[kUp]);
  if (p.w[kNeed] > 0) {
    theme::PillSize ps;
    ps.pad = kNeedPad;
    theme::pill(c, x, 0, p.text[kNeed].c_str(), theme::Tone::Warn, Color::Bg, ps);
  }
  x = static_cast<int16_t>(x + p.w[kNeed]);
  if (p.w[kFiles] > 0) c.text(x, 0, p.text[kFiles].c_str(), Style{Color::Ink2, Color::Bg});
}

// "※SD ※k3 Wed 13:42 83%" from the right edge, the parts that fit.
void draw_right_side(Canvas& c, const AppState& s, uint32_t now, const Parts& p) {
  const int pct = battery_pct(s.device.bat_mv);
  int16_t x = draw_right(c, kRight, 0, battery_text(pct).c_str(), Style{battery_ink(pct), Color::Bg});
  x = draw_right(c, static_cast<int16_t>(x - 5), 0, clock_text(s.time, now).c_str(), Style{Color::Ink2, Color::Bg});
  const struct {
    Part k;
    Color ink;
  } kRightParts[] = {{kDay, Color::Ink3}, {kKb, Color::Warn}, {kSd, Color::Warn}};
  for (const auto& r : kRightParts) {
    if (p.w[r.k] == 0) continue;
    const int16_t gap = static_cast<int16_t>(p.w[r.k] - c.textWidth(p.text[r.k].c_str()));
    x = draw_right(c, static_cast<int16_t>(x - gap), 0, p.text[r.k].c_str(), Style{r.ink, Color::Bg});
  }
}

// What always stays on the right: the battery, 5 px, the clock, from the gutter.
int16_t right_fixed(const Canvas& c, const AppState& s, uint32_t now) {
  const int16_t bat = c.textWidth(battery_text(battery_pct(s.device.bat_mv)).c_str());
  return static_cast<int16_t>(theme::kGutter + bat + 5 + c.textWidth(clock_text(s.time, now).c_str()));
}

}  // namespace

LinkGlyph link_glyph(const AppState& s) {
  switch (s.link.mode) {
    case LinkMode::Ready: return s.outbox.pending > 0 ? LinkGlyph::Syncing : LinkGlyph::Online;
    case LinkMode::Attached: return s.link.version_mismatch ? LinkGlyph::Incompatible : LinkGlyph::NoDaemon;
    default: return LinkGlyph::Offline;
  }
}

const char* link_glyph_text(LinkGlyph g) {
  switch (g) {
    case LinkGlyph::Online: return tr(kOnline);
    case LinkGlyph::Syncing: return tr(kSyncing);
    case LinkGlyph::NoDaemon: return tr(kNoDaemon);
    case LinkGlyph::Incompatible: return tr(kIncompatible);
    default: return tr(kOffline);
  }
}

FixedStr<8> clock_text(const TimeInfo& t, uint32_t now) {
  const Opt<int64_t> wall = wall_ms(t, now);
  if (!wall.has) return FixedStr<8>::from("--:--");
  int64_t ms = (wall.value + static_cast<int64_t>(t.tz_min) * 60000) % kDayMs;
  if (ms < 0) ms += kDayMs;
  const int minutes = static_cast<int>(ms / 60000);
  const int h = minutes / 60;
  const int m = minutes % 60;
  // Built digit by digit: GCC's -Wformat-truncation cannot prove the 0..23 / 0..59 ranges.
  const char buf[6] = {static_cast<char>('0' + h / 10), static_cast<char>('0' + h % 10), ':',
                       static_cast<char>('0' + m / 10), static_cast<char>('0' + m % 10), '\0'};
  return FixedStr<8>::from(buf);
}

int battery_pct(uint16_t mv) {
  if (mv == 0) return -1;
  const int pct = (static_cast<int>(mv) - kBatEmptyMv) * 100 / (kBatFullMv - kBatEmptyMv);
  if (pct < 0) return 0;
  return pct > 100 ? 100 : pct;
}

Color battery_ink(int pct) {
  if (pct < 0 || pct >= kBatWarnPct) return Color::Ink3;
  return pct > kBatStopPct ? Color::Warn : Color::Error;
}

void render_status_bar(Canvas& c, const AppState& s, uint32_t now) {
  c.fill(kStatusRect, Color::Bg);
  const LinkGlyph g = link_glyph(s);
  Parts p = parts_of(c, s, now);
  const int16_t x = draw_link(c, draw_rec(c, s, now), g);
  fit(p, static_cast<int16_t>(x + kGroupGap + right_fixed(c, s, now)));
  draw_left(c, x, p);
  draw_right_side(c, s, now, p);
}

void render_hint_bar(Canvas& c, const char* hint) { theme::hint_bar(c, hint); }

}  // namespace cpa
