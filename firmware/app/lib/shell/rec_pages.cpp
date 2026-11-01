// The recording pages (docs/19 §6.6-§6.7, mockups 04 and 05): Mando listening with his sign,
// the big ● and timer with REC, what 1-9 and 0 do with the current choice lit (gh #53,
// rec_legend.h); the memo's scrolling waveform of the last 35 blocks, and in its title row
// the grace or hands free (gh #51); on the meeting page the title with where it goes and the
// segment, the stars, and the whole meeting as a timeline with its stars. The status bar's
// ●mm:ss is drawn by the shell on top of this, always. v0.5 S3: Mando, the waveform and the
// timeline are drawn as the last animation tick left them (RecLook), and a tick redraws just
// those parts (rec_*_part), in the room the legend left them (RecRoom).
#include <cstdio>

#include "protocol_limits.h"
#include "rec_legend.h"
#include "rec_overlay.h"
#include "status_bar.h"
#include "text_fit.h"
#include "theme.h"
#include "theme_mando.h"
#include "words.h"

namespace cpa {
namespace {

constexpr uint8_t kDotD = 9;
constexpr int16_t kClockY = 33;
constexpr int16_t kDotY = 44;
constexpr int16_t kTextX = 60;  // the column right of Mando
constexpr uint32_t kStarFlashMs = 600;

// Where Mando, his sign, the big ● and the timer sit on a page.
struct Stage {
  theme::MandoSize size;
  int16_t mando_x;
  int16_t mando_y;
  int16_t sign_x;  // centre
  int16_t sign_y;
  int16_t dot_x;
  int16_t clock_x;
};
constexpr Stage kMemo{theme::MandoSize::M64, 10, 19, 28, 87, 66, 75};
constexpr Stage kMeeting{theme::MandoSize::M46, 10, 31, 23, 79, 52, 60};

const Stage& stage_of(const RecInfo& r) { return r.mode == RecMode::Memo ? kMemo : kMeeting; }

// The memo's waveform (mockup 04): kWaveBars mirrored 3 px bars 5 px apart round y 75,
// newest on the right, Accent and Accent2 by threes, their ends chipped once 5 px tall.
// gh #53: under a three-row legend it is 2/3 as tall, round y 70, its 35 bars kept.
struct Wave {
  Rect rect;  // the bars' extent
  int16_t mid;
  int16_t half;
};
constexpr Wave kWaveFull{{62, 64, 173, 23}, 75, 11};
constexpr Wave kWaveLow{{62, 63, 173, 15}, 70, 7};

const Wave& wave_of(const RecRoom& room) { return room.low_wave ? kWaveLow : kWaveFull; }

// The room a legend of `rows` leaves each page (rec_legend.h): the memo's third row lowers
// the waveform, the meeting's second takes the timeline's labels' row.
RecRoom memo_room(uint8_t rows) { return RecRoom{rows >= 3, false}; }
RecRoom meeting_room(uint8_t rows) { return RecRoom{false, rows >= 2}; }

// The meeting's timeline (mockup 05, the judges' notes): a 1 px bar every 2 px from x 48 round
// y 98 until the cells first merge, then the cells spread over the whole kLineW, each bar its
// pitch less a pixel; the newest kLineNew in Accent; a Live base line to a Rec dot 2 px after
// the newest bar; the stars above; 0:00 and 现在 (under the dot) below, unless the legend
// takes their row (gh #53).
constexpr Rect kLineRect{46, 73, 194, 49};  // the bars, the stars and the labels
constexpr int16_t kLineX = 48;
constexpr int16_t kLineW = 188;
constexpr int16_t kLineMid = 98;
constexpr int16_t kLineHalf = 8;
constexpr uint8_t kLineNew = 7;                       // bars in Accent at the end
constexpr int16_t kLineDotMax = kLineX + kLineW + 1;  // its rim on the screen's last column
constexpr int16_t kLabelY = kLineMid + 11;
constexpr int16_t kBarsBandH = 2 * kLineHalf + 1;                         // the bars, the base line and the dot
constexpr int16_t kStarsBandH = kLineMid + kLineHalf + 1 - kLineRect.y;  // the stars and the bars
constexpr int16_t kStarGap = 8;  // a star closer to the last one drawn is only its stem

void draw_mando(Canvas& c, const RecInfo& r, const theme::MandoView& v) {
  const Stage& st = stage_of(r);
  theme::mando(c, st.mando_x, st.mando_y, st.size, v);
  const char* sign = rec_sign(r);
  if (sign == nullptr) return;
  const theme::Tone t = r.phase == RecPhase::Finishing ? theme::Tone::Neutral : theme::Tone::Info;
  theme::sign(c, st.sign_x, st.sign_y, sign, t);
}

// REC right of the timer: the memo's cue said "看到 ● REC 再说话" (v0.3.1).
void draw_rec_word(Canvas& c, const Stage& st, const char* clock) {
  const int16_t x = static_cast<int16_t>(st.clock_x + c.digitsWidth(clock, DigitFont::Timer) + 5);
  c.text(x, static_cast<int16_t>(kDotY - 6), "REC", Style{Color::Rec, Color::Bg});
}

// The big ● (RecDot, the only place it is used: it breathes), the timer in Nunito, and REC
// while it records (not while it is saving).
void draw_clock(Canvas& c, const RecInfo& r, const Stage& st, uint32_t now) {
  c.dot(st.dot_x, kDotY, kDotD, Color::RecDot, Color::Bg);
  const FixedStr<16> clock = rec_clock_text(now - r.started_ms);
  c.digits(st.clock_x, kClockY, clock.c_str(), DigitFont::Timer, Color::Ink, Color::Bg);
  if (r.phase != RecPhase::Finishing) draw_rec_word(c, st, clock.c_str());
}

// gh #51: the memo's title row while G0 is let go (a Warn capsule: how long a re-press may
// take) or the memo is hands-free (an Info capsule: how it ends). Nothing else changes:
// Mando listens on, ● REC and the waveform go on. In the last 30 s (U15) the words are
// shorter: "还剩 0:30" keeps the corner.
// docs/21: the recording pages' words.
constexpr LineTxt kHandsFree{"免提录音 · 按 G0 结束", "Hands-free · tap G0 to end"};
constexpr LineTxt kHandsFreeShort{"免提 · G0 结束", "G0 ends it"};
constexpr Txt kGrace{"%s%u.%u 秒内按回继续", "%shold within %u.%u s"};
constexpr Txt kLetGo{"松开了 · ", "Let go · "};
constexpr LineTxt kSpeakAtRec{"看到 ● REC 再说话", "Speak once you see ● REC"};
constexpr Txt kPart{"第 %u 段", "Part %u"};
constexpr Txt kMarked{"★ 已标记 %s", "★ marked %s"};
constexpr LineTxt kStopAsk{"结束录音？", "Stop recording?"};
constexpr Txt kLeft{"还剩 %s", "%s left"};
constexpr Txt kSaveTo{"存到", "Save to"};
constexpr Txt kNow{"现在", "now"};

constexpr int16_t kStateY = 14;

void draw_memo_state(Canvas& c, const RecInfo& r, bool late) {
  theme::PillSize ps;
  ps.h = theme::kLiveH;
  ps.pad = 5;
  if (r.latched) {
    const char* text = tr(late ? kHandsFreeShort : kHandsFree);
    theme::pill(c, kTextX, kStateY, text, theme::Tone::Info, Color::Bg, ps);
    return;
  }
  char grace[48];
  std::snprintf(grace, sizeof grace, tr(kGrace), late ? "" : tr(kLetGo),
                static_cast<unsigned>(rec::kMemoGraceMs / 1000), static_cast<unsigned>(rec::kMemoGraceMs % 1000 / 100));
  theme::pill(c, kTextX, kStateY, grace, theme::Tone::Warn, Color::Bg, ps);
}

// Before the microphone is up (v0.3.1: silent): no timer yet, "● 准备" in its place.
void draw_cue(Canvas& c, bool explain) {
  c.text(kTextX, 36, tr(txt::kMemoReady), Style{Color::Warn, Color::Bg});
  if (explain) c.text(kTextX, 52, tr(kSpeakAtRec), Style{Color::Ink3, Color::Bg});
}

FixedStr<24> segment_text(const RecInfo& r) {
  char seg[24];
  std::snprintf(seg, sizeof seg, tr(kPart), static_cast<unsigned>(r.seg + 1));
  return FixedStr<24>::from(seg);
}

// "★2" in a mustard capsule, right-aligned; just after a star "★ 已标记 12:31" (when).
// Returns its left edge.
int16_t draw_stars(Canvas& c, const RecInfo& r, uint32_t now) {
  char buf[40];
  if (r.star_ms != 0 && now - r.star_ms < kStarFlashMs) {
    std::snprintf(buf, sizeof buf, tr(kMarked), rec_clock_text(r.star_ms - r.started_ms).c_str());
  } else {
    std::snprintf(buf, sizeof buf, "★%u", static_cast<unsigned>(r.stars));
  }
  theme::PillSize ps;
  ps.h = theme::kLiveH;
  ps.pad = 5;
  const int16_t x = static_cast<int16_t>(kScreenW - theme::kGutter - theme::pill_width(c, buf, ps));
  theme::pill(c, x, 14, buf, theme::Tone::Warn, Color::Bg, ps);
  return x;
}

// "开会 未归属 · 第 1 段" (the segment in the title row, clear of the star capsule at `end`).
void draw_title(Canvas& c, const AppState& s, int16_t end) {
  const int16_t tx = static_cast<int16_t>(6 + theme::title(c, 6, 15, tr(txt::kMeeting)) + 5);
  char line[64];
  std::snprintf(line, sizeof line, "%s · %s", rec_tag_text(s).c_str(), segment_text(s.rec).c_str());
  draw_fit(c, tx, 15, static_cast<int16_t>(end - 4 - tx), line, Style{Color::Ink3, Color::Bg});
}

// docs/11 §3.2: v0.4.0's words, on a card over the page, the keys as keycaps; ● and the
// timer stay.
void draw_end_confirm(Canvas& c) {
  theme::card(c, Rect{44, 58, 192, 62});
  c.text(52, 66, tr(kStopAsk), Style{Color::Warn, Color::Card});
  theme::keys_text(c, 52, 90, tr(kEndConfirmKeys), Color::Ink, Color::Card);
}

void draw_wave_bar(Canvas& c, const Wave& w, int16_t x, uint8_t q, Color col) {
  const int16_t half = theme::level_half(q, w.half);
  const Rect bar{x, static_cast<int16_t>(w.mid - half), 3, static_cast<int16_t>(2 * half + 1)};
  c.fill(bar, col);
  if (bar.h < 5) return;
  const int16_t right = static_cast<int16_t>(x + 2);
  const int16_t bottom = static_cast<int16_t>(bar.y + bar.h - 1);
  for (const Rect& px : {Rect{x, bar.y, 1, 1}, Rect{right, bar.y, 1, 1}, Rect{x, bottom, 1, 1}, Rect{right, bottom, 1, 1}}) {
    c.fillEdge(px, col, Color::Bg);
  }
}

// Blocks not heard yet are quiet dots on the left. Saving (live false): every bar in Live,
// words that have not landed yet (mockup 04's generator). docs/20 U15: in a memo's last 30 s
// (late) every bar is Warn, with the countdown.
Color wave_color(uint8_t k, bool live, bool late) {
  if (!live) return Color::Live;
  if (late) return Color::Warn;
  return (k / 3) % 2 == 0 ? Color::Accent : Color::Accent2;
}

void draw_wave(Canvas& c, const Wave& w, const theme::LevelHistory& h, bool live, bool late) {
  const uint8_t empty = static_cast<uint8_t>(theme::kWaveBars - h.wave_n);
  for (uint8_t k = 0; k < theme::kWaveBars; ++k) {
    const uint8_t q = k < empty ? 0 : h.wave[k - empty];
    draw_wave_bar(c, w, static_cast<int16_t>(w.rect.x + 5 * k), q, wave_color(k, live, late));
  }
}

// The memo's top right in its last 30 s: "还剩 0:30" in a Warn capsule (docs/20 U15),
// counting down to the cap.
void draw_countdown(Canvas& c, const RecInfo& r, uint32_t now) {
  const int16_t right = static_cast<int16_t>(kScreenW - theme::kGutter);
  const uint32_t elapsed = now - r.started_ms;
  const uint32_t left = elapsed < limits::kMemoMaxMs ? limits::kMemoMaxMs - elapsed + 999 : 0;  // whole seconds up
  char buf[32];
  std::snprintf(buf, sizeof buf, tr(kLeft), rec_clock_text(left / 1000 * 1000).c_str());
  theme::PillSize ps;
  ps.h = theme::kSignH;
  ps.pad = 5;
  theme::pill(c, static_cast<int16_t>(right - theme::pill_width(c, buf, ps)), 17, buf, theme::Tone::Warn, Color::Bg, ps);
}

// The memo's title row: "语音便条" and 第 N 段, or the grace or hands free (gh #51); the
// countdown in the corner in its last 30 s.
// 语音便条; gh #53: "存到 #key" when the legend cut the chosen key, so it is on screen whole.
void draw_memo_name(Canvas& c, const char* where) {
  if (where == nullptr) return c.text(kTextX, 18, tr(txt::kVoiceMemo), Style{Color::Ink3, Color::Bg});
  c.text(kTextX, 18, tr(kSaveTo), Style{Color::Ink3, Color::Bg});
  c.text(static_cast<int16_t>(kTextX + c.textWidth(tr(kSaveTo)) + 4), 18, where, Style{Color::Accent, Color::Bg});
}

void draw_memo_title(Canvas& c, const RecInfo& r, uint32_t now, const char* where) {
  const bool late = rec_memo_late(r, now);
  if (r.grace || r.latched) {
    draw_memo_state(c, r, late);
  } else {
    draw_memo_name(c, where);
    const int16_t right = static_cast<int16_t>(kScreenW - theme::kGutter);
    if (!late) draw_right(c, right, 18, segment_text(r).c_str(), Style{Color::Ink3, Color::Bg});
  }
  if (late) draw_countdown(c, r, now);
}

// docs/20 U6: the meeting keeps its battery on the timer's row, coloured as the status bar's.
void draw_meeting_battery(Canvas& c, uint16_t mv) {
  const int pct = battery_pct(mv);
  char buf[24];
  std::snprintf(buf, sizeof buf, "%s", tr(txt::kBatNone));
  if (pct >= 0) std::snprintf(buf, sizeof buf, tr(txt::kBatPct), pct);
  draw_right(c, static_cast<int16_t>(kScreenW - theme::kGutter), static_cast<int16_t>(kDotY - 6), buf,
             Style{battery_ink(pct), Color::Bg});
}

// Cell k's bar: before the first merge one pixel every 2 px; after it the cells share kLineW.
bool spread(const theme::LevelHistory& h) { return h.per_cell > 1 && h.cells_n > 0; }

int16_t cell_x(const theme::LevelHistory& h, int k) {
  if (!spread(h)) return static_cast<int16_t>(kLineX + 2 * k);
  return static_cast<int16_t>(kLineX + k * kLineW / h.cells_n);
}

int16_t cell_w(const theme::LevelHistory& h, int k) {
  if (!spread(h)) return 1;
  const int w = cell_x(h, k + 1) - cell_x(h, k) - 1;
  return static_cast<int16_t>(w > 1 ? w : 1);
}

// The end dot's centre: 2 px after the newest bar.
int16_t line_dot_x(const theme::LevelHistory& h) {
  const int last = h.cells_n - 1;
  const int right = h.cells_n == 0 ? kLineX - 1 : cell_x(h, last) + cell_w(h, last) - 1;
  return static_cast<int16_t>(right + 5 < kLineDotMax ? right + 5 : kLineDotMax);
}

// 现在 ends under the dot's rim, never over 0:00, and 2 px short of the screen's edge
// (the dot's rim is on its last column; "now"'s w would be too).
int16_t now_label_x(const Canvas& c, const theme::LevelHistory& h) {
  const int16_t w = c.textWidth(tr(kNow));
  const int16_t under = static_cast<int16_t>(line_dot_x(h) + 3 - w);
  const int16_t max = static_cast<int16_t>(kScreenW - 2 - w);
  const int16_t x = under < max ? under : max;
  const int16_t min = static_cast<int16_t>(kLineX + c.textWidth("0:00") + 6);
  return x > min ? x : min;
}

void draw_line_stars(Canvas& c, const theme::LevelHistory& h) {
  int16_t drawn = -kStarGap;
  for (uint8_t i = 0; i < h.stars_n; ++i) {
    const uint8_t k = theme::star_cell(h, i);
    const int16_t sx = static_cast<int16_t>(cell_x(h, k) + cell_w(h, k) / 2);
    c.fill(Rect{sx, static_cast<int16_t>(kLineMid - 13), 1, 4}, Color::Warn);
    if (sx - drawn < kStarGap) continue;  // its glyph would melt into the last one
    c.text(static_cast<int16_t>(sx - 2), static_cast<int16_t>(kLineMid - 25), "★", theme::over(Color::Warn));
    drawn = sx;
  }
}

void draw_timeline(Canvas& c, const theme::LevelHistory& h, bool labels) {
  for (uint8_t k = 0; k < h.cells_n; ++k) {
    const int16_t half = theme::level_half(h.cells[k], kLineHalf);
    const Color col = k + kLineNew >= h.cells_n ? Color::Accent : Color::Live;
    c.fill(Rect{cell_x(h, k), static_cast<int16_t>(kLineMid - half), cell_w(h, k), static_cast<int16_t>(2 * half + 1)},
           col);
  }
  const int16_t dot = line_dot_x(h);
  c.fill(Rect{kLineX, kLineMid, static_cast<int16_t>(dot - kLineX), 1}, Color::Live);
  draw_line_stars(c, h);
  c.dot(dot, kLineMid, 5, Color::Rec, Color::Bg);
  if (!labels) return;
  c.text(kLineX, kLabelY, "0:00", Style{Color::Ink3, Color::Bg});
  c.text(now_label_x(c, h), kLabelY, tr(kNow), Style{Color::Ink3, Color::Bg});
}

bool stars_moved(const theme::LevelHistory& was, const theme::LevelHistory& now) {
  bool moved = was.stars_n != now.stars_n;
  for (uint8_t i = 0; i < now.stars_n && !moved; ++i) moved = was.star_block[i] != now.star_block[i];
  return moved;
}

// 现在 from where it was to where it is (w 0: the dot did not move).
Rect label_dirty(const Canvas& c, const theme::LevelHistory& was, const theme::LevelHistory& now) {
  const int16_t a = now_label_x(c, was);
  const int16_t b = now_label_x(c, now);
  if (a == b) return Rect{};
  const int16_t lx = a < b ? a : b;
  const int16_t w = static_cast<int16_t>((a < b ? b : a) + c.textWidth(tr(kNow)) - lx);
  return Rect{lx, kLabelY, w, kBarH};
}

// What a tick changed on the timeline, as few pixels as it can (docs/19 §7.5): all of it
// after a merge or a star; when spread cells moved for a new one, the bars and the stars;
// else the bars from the first that changed height or colour (the newest kLineNew are
// Accent) to the dot, and, when the dot moved, 现在 from where it was to where it is. Without
// the labels (the legend's row, gh #53) never below the bars.
RecParts line_dirty(const Canvas& c, const theme::LevelHistory& was, const theme::LevelHistory& now, bool labels) {
  RecParts out;
  const bool grew = was.cells_n != now.cells_n;
  const Rect stars_and_bars{kLineRect.x, kLineRect.y, kLineRect.w, kStarsBandH};
  if (was.per_cell != now.per_cell || stars_moved(was, now)) {
    return RecParts{1, {labels ? kLineRect : stars_and_bars, Rect{}}};
  }
  if (grew && spread(now)) return RecParts{1, {stars_and_bars, Rect{}}};
  int first = grew ? was.cells_n - kLineNew : now.cells_n;
  for (int k = 0; k < was.cells_n && k < first; ++k) {
    if (was.cells[k] != now.cells[k]) first = k;
  }
  first = first < 0 ? 0 : first;
  if (first >= now.cells_n) return out;
  const int16_t x = static_cast<int16_t>(cell_x(now, first) - 1);
  const int16_t end = static_cast<int16_t>(line_dot_x(now) + 3);
  out.r[out.n++] = Rect{x, static_cast<int16_t>(kLineMid - kLineHalf), static_cast<int16_t>(end - x), kBarsBandH};
  const Rect label = labels ? label_dirty(c, was, now) : Rect{};
  if (label.w > 0) out.r[out.n++] = label;
  return out;
}

}  // namespace

void render_memo_page(Canvas& c, const AppState& s, const RecLook& look, uint32_t now) {
  const RecInfo& r = s.rec;
  const PageLegend legend = memo_legend(c, s);
  draw_mando(c, r, look.mando);
  draw_memo_title(c, r, now, legend.chosen_cut ? rec_tag_text(s).c_str() : nullptr);
  if (r.phase == RecPhase::Cue) {
    draw_cue(c, true);
  } else {
    draw_clock(c, r, kMemo, now);
  }
  if (r.phase == RecPhase::Recording || r.phase == RecPhase::Finishing) {
    const RecRoom room = memo_room(legend.rows);  // a named room: GCC 13 -Wdangling-reference
    const Wave& w = wave_of(room);
    draw_wave(c, w, *look.levels, r.phase == RecPhase::Recording, rec_memo_late(r, now));
  }
  draw_legend(c, legend.plan);
}

void render_meeting_page(Canvas& c, const AppState& s, const RecLook& look, uint32_t now) {
  const RecInfo& r = s.rec;
  draw_title(c, s, draw_stars(c, r, now));
  draw_mando(c, r, look.mando);
  if (r.phase == RecPhase::Cue) return draw_cue(c, false);
  draw_clock(c, r, kMeeting, now);
  draw_meeting_battery(c, s.device.bat_mv);
  if (r.phase == RecPhase::EndConfirm) return draw_end_confirm(c);
  const PageLegend legend = meeting_legend(c, s);
  draw_timeline(c, *look.levels, !meeting_room(legend.rows).no_labels);  // Finishing: as it ended
  draw_legend(c, legend.plan);
}

// His acts here have no bubble: the sprite alone (docs/19 §7.5).
Rect rec_mando_part(Canvas& c, const RecInfo& r, const theme::MandoView& v) {
  const Stage& st = stage_of(r);
  const Rect box = theme::mando_sprite(st.size, st.mando_x, st.mando_y);
  c.fill(box, Color::Bg);
  theme::mando(c, st.mando_x, st.mando_y, st.size, v);
  return box;
}

RecRoom rec_room(const Canvas& c, const AppState& s) {
  if (s.rec.mode == RecMode::Memo) return memo_room(memo_legend_rows(c, s));
  return meeting_room(meeting_legend_rows(c, s));
}

Rect rec_wave_part(Canvas& c, const theme::LevelHistory& h, bool late, const RecRoom& room) {
  const Wave& w = wave_of(room);
  c.fill(w.rect, Color::Bg);
  draw_wave(c, w, h, true, late);
  return w.rect;
}

RecParts rec_line_part(Canvas& c, const theme::LevelHistory& was, const theme::LevelHistory& now,
                       const RecRoom& room) {
  const RecParts dirty = line_dirty(c, was, now, !room.no_labels);
  for (uint8_t i = 0; i < dirty.n; ++i) c.fill(dirty.r[i], Color::Bg);
  if (dirty.n > 0) draw_timeline(c, now, !room.no_labels);
  return dirty;
}

Rect rec_dot_rect(const RecInfo& r) {
  const int16_t reach = (kDotD + 1) / 2 + 1;
  return Rect{static_cast<int16_t>(stage_of(r).dot_x - reach), static_cast<int16_t>(kDotY - reach),
              static_cast<int16_t>(2 * reach), static_cast<int16_t>(2 * reach)};
}

}  // namespace cpa
