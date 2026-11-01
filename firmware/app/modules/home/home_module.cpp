#include "home_module.h"

#include <cstdio>

#include "mando_eggs.h"
#include "reducers.h"
#include "text_fit.h"
#include "theme.h"
#include "words.h"

namespace cpa {
namespace {

// docs/19 §6.3 (mockup 01), from the area's top (y 12 on screen): Mando on the left, the
// greeting and the card of the most urgent session right of him, then the tiles.
constexpr int16_t kMandoX = 7;
constexpr int16_t kMandoY = 2;
constexpr int16_t kGreetX = 44;
constexpr int16_t kGreetY = 3;
constexpr int16_t kGreetW = 192;  // x 44 to 236 (below)
constexpr Rect kUrgentCard{42, 18, 194, 28};
constexpr int16_t kTileX[kHomeCols] = {4, 83, 161};
constexpr int16_t kTileW[kHomeCols] = {75, 74, 75};

// docs/21 §6.7: home's words. The greeting is three pieces, the count in mustard:
// "下午好，2 件事在等你" / "Afternoon, 2 waiting for you", 192 px at most (x 44 to 236).
constexpr Txt kHelloComma{"%s，", "%s, "};
constexpr Txt kThings{"%u 件事", "%u"};
constexpr Txt kWaitForYou{"在等你", " waiting for you"};
constexpr Txt kAllDoneLower{"都处理完了", "all done"};
constexpr Txt kMacMismatch{"Mac 端版本不兼容", "Mac version mismatch"};
constexpr Txt kOfflineNow{"现在离线", "offline now"};
// After the greeting's comma: lower case, unlike the inbox's own "No Mac yet".
constexpr Txt kNoMacYetMid{"还没连上 Mac", "no Mac yet"};
// The urgent card's lines: 176 px (x 54 to 230).
constexpr int16_t kCardTextW = 176;
constexpr Txt kConnectToSee{"接上 Mac 后显示等你的 session", "Connect a Mac to see sessions", kCardTextW};
constexpr CountTxt kOlderSessions{"+%u 个更早的 session", "+%u older session", "+%u older sessions"};
constexpr Txt kAllIdle{"都空闲一天以上 · 按 I 看", "All idle 1 day+ · press I", kCardTextW};
constexpr Txt kNeedsReply{"等你回复", "waiting"};
constexpr Txt kIdle{"空闲", "idle"};
constexpr Txt kOlder{"+%u 更早", "+%u older"};
// The board tile's count: in English the number alone ("Board 12"), the room "run" would take.
constexpr Txt kRunningTile{"跑%lu", "%lu"};

// 本页 rows of the Tab menu: the eight letter tiles (each replays its Opt chord, which
// is what the letter does here) and the digits as a range row, shown and not replayed.
constexpr HelpRow kHelp[] = {
    kHomeTiles[0], kHomeTiles[1], kHomeTiles[2], kHomeTiles[3], kHomeTiles[4], kHomeTiles[5],
    kHomeTiles[6], kHomeTiles[7], HelpRow{KeyEvent{}, "1-9", "", &txt::kOptDigitsRow},
};

struct Badge {
  char text[16] = "";
  Color fg = Color::Ink3;
};

// "跑N" on P, "!N" on I; nothing for a zero count.
Badge badge_of(uint8_t tile, const AppState& s) {
  Badge b;
  const uint32_t run = home_running(s);
  const unsigned need = need_count(s.sessions);
  if (tile == 0 && run > 0) {
    std::snprintf(b.text, sizeof b.text, tr(kRunningTile), static_cast<unsigned long>(run));
  } else if (tile == 1 && need > 0) {
    std::snprintf(b.text, sizeof b.text, "!%u", need);
    b.fg = Color::Warn;
  }
  return b;
}

void tile(Canvas& c, const Rect& area, uint8_t i, bool sel, const Badge& b, bool muted) {
  const HomeGrid g = home_grid(kHomeTileCount);
  const Rect r{static_cast<int16_t>(area.x + kTileX[i % kHomeCols]),
               static_cast<int16_t>(area.y + g.y + (i / kHomeCols) * g.pitch), kTileW[i % kHomeCols], g.h};
  theme::tile(c, r, theme::TileSpec{kHomeTiles[i].keys, help_label(kHomeTiles[i]), b.text, b.fg, muted}, sel);
}

// docs/20 U19: what the greeting says instead of counting, when home's picture is not live.
const char* link_word(DataLink l, bool cached) {
  switch (l) {
    case DataLink::Other: return tr(kMacMismatch);
    case DataLink::Stale: return tr(kOfflineNow);
    case DataLink::NoCpad: return tr(cached ? kOfflineNow : kNoMacYetMid);
    default: return tr(kNoMacYetMid);
  }
}

// The card when no session is the most urgent: one line, or two (what to do, how old).
struct CardLines {
  FixedStr<64> top;
  FixedStr<64> sub;
};

CardLines no_urgent_lines(const AppState& s, DataLink l, uint32_t now) {
  using Line = FixedStr<64>;
  if (l == DataLink::Never) return CardLines{Line::from(tr(kConnectToSee)), Line{}};
  if (l == DataLink::NoCpad && !s.sessions.live) return CardLines{Line::from(tr(txt::kNoCpad)), Line::from(tr(txt::kCheckCpad))};
  if (l == DataLink::Other) return CardLines{Line::from(tr(txt::kVersionsDiffer)), Line::from(tr(txt::kPleaseUpdate))};
  const uint8_t older = home_older(s, now);
  char top[64];
  std::snprintf(top, sizeof top, trn(kOlderSessions, older), older);
  const Line sub = l != DataLink::Live ? Line::from(stale_text(data_age_s(s, now)).c_str())
                                       : Line::from(older > 0 ? tr(kAllIdle) : "");
  return CardLines{Line::from(older > 0 ? top : tr(txt::kNoneNeedsYou)), sub};
}

// The urgent card's second line: its project and what it waits for while live; how old the
// picture is when not.
FixedStr<64> urgent_sub(const AppState& s, const SessionRow& r, DataLink l, uint32_t now) {
  if (l != DataLink::Live) return FixedStr<64>::from(stale_text(data_age_s(s, now)).c_str());
  const Opt<ProjectRow> p = project_by_slot(s.projects, r.slot);
  char who[64];
  std::snprintf(who, sizeof who, "%s%s%s", p.has ? p.value.key.c_str() : "", p.has ? " · " : "",
                tr(r.st == SessionState::Waiting ? kNeedsReply : kIdle));
  return FixedStr<64>::from(who);
}

// v0.5.0 (docs/04, hidden keys): a pat's heart over Mando, up from his first tick.
void pat_heart(Canvas& c, const Rect& area, const AppState& s, const theme::MandoMotion& m, uint32_t now) {
  if (!pat_showing(s.ui.pat, now)) return;
  const uint8_t step = m.shown.act == theme::MandoAct::Hop && m.ticks > 0 ? 1 : 0;
  theme::heart(c, theme::MandoSize::M46, static_cast<int16_t>(area.x + kMandoX), static_cast<int16_t>(area.y + kMandoY),
               Color::Bg, step);
}

}  // namespace

bool HomeModule::begin(ModuleContext& ctx) {
  ctx_ = &ctx;
  return true;
}

void HomeModule::onEnter(uint8_t) { ui_ = HomeUi{}; }

bool HomeModule::onKey(const KeyEvent& key) {
  const HomeStep st = home_next(ui_, key);
  ui_ = st.ui;
  if (st.press.kind != KeyEventKind::None) ctx_->press(st.press);  // refused: nothing happens
  if (st.hint) ctx_->hint(ToastLevel::Info, st.hint);              // docs/20 D1
  return st.handled;
}

void HomeModule::render(Canvas& c, const Rect& area) const {
  const AppState& s = ctx_->state();
  const theme::MandoView v = theme::mando_view(motion_, home_mando(s, ctx_->nowMs()));
  theme::mando(c, static_cast<int16_t>(area.x + kMandoX), static_cast<int16_t>(area.y + kMandoY),
               theme::MandoSize::M46, v);
  pat_heart(c, area, s, motion_, ctx_->nowMs());
  render_greeting(c, area);
  render_urgent(c, area);
  render_tiles(c, area);
}

// "早上好，1 件事在等你" (the count in mustard), or "早上好，都处理完了"; when home's picture
// is not live (docs/20 U19), a word in Ink3 instead: 还没连上 Mac, 现在离线, 版本不兼容.
void HomeModule::render_greeting(Canvas& c, const Rect& area) const {
  const AppState& s = ctx_->state();
  const int16_t y = static_cast<int16_t>(area.y + kGreetY);
  if (pat_showing(s.ui.pat, ctx_->nowMs())) return render_pat(c, area);
  char hello[24];
  std::snprintf(hello, sizeof hello, tr(kHelloComma), home_greeting(s.time, ctx_->nowMs()));
  int16_t x = static_cast<int16_t>(area.x + kGreetX);
  c.text(x, y, hello, Style{Color::Ink, Color::Bg});
  x = static_cast<int16_t>(x + c.textWidth(hello));
  const DataLink l = data_link(s);
  if (l != DataLink::Live) return c.text(x, y, link_word(l, s.sessions.live), Style{Color::Ink3, Color::Bg});
  const unsigned need = need_count(s.sessions);
  if (need == 0) return c.text(x, y, tr(kAllDoneLower), Style{Color::Ink, Color::Bg});
  char count[16];
  std::snprintf(count, sizeof count, tr(kThings), need);
  c.text(x, y, count, Style{Color::Warn, Color::Bg});
  c.text(static_cast<int16_t>(x + c.textWidth(count)), y, tr(kWaitForYou), Style{Color::Ink, Color::Bg});
}

// v0.5.0 (docs/04, hidden keys): his line and the count where the greeting was, 1.5 s; the
// count left out where both do not fit (as on the lock card).
void HomeModule::render_pat(Canvas& c, const Rect& area) const {
  const PatState& p = ctx_->state().ui.pat;
  const int16_t x = static_cast<int16_t>(area.x + kGreetX);
  const int16_t y = static_cast<int16_t>(area.y + kGreetY);
  const char* line = pat_line(p);
  c.text(x, y, line, Style{Color::Ink, Color::Bg});
  const FixedStr<24> count = pat_count(p);
  const int16_t cx = static_cast<int16_t>(x + c.textWidth(line) + 6);
  if (cx + c.textWidth(count.c_str()) > x + kGreetW) return;
  c.text(cx, y, count.c_str(), Style{Color::Ink3, Color::Bg});
}

// docs/20 U16: R (开会) is greyed while the device cannot record; the hint says why.
void HomeModule::render_tiles(Canvas& c, const Rect& area) const {
  const AppState& s = ctx_->state();
  const bool no_rec = home_rec_block(s) != RecBlock::None;
  for (uint8_t i = 0; i < kHomeTileCount; ++i) tile(c, area, i, i == ui_.sel, badge_of(i, s), no_rec && i == 2);
}

// The most urgent session that needs you, as the inbox names it: its name and how long,
// then its project and what it waits for; a waiting one has the mustard stripe. Away from the
// Mac its age stops and the second line says how old the picture is (docs/20 U19).
void HomeModule::render_urgent(Canvas& c, const Rect& area) const {
  const AppState& s = ctx_->state();
  const uint32_t now = ctx_->nowMs();
  const Rect card{static_cast<int16_t>(area.x + kUrgentCard.x), static_cast<int16_t>(area.y + kUrgentCard.y),
                  kUrgentCard.w, kUrgentCard.h};
  theme::card(c, card, theme::CardKind::Plain, Color::Bg, theme::kRadiusTile);
  const int16_t x = static_cast<int16_t>(card.x + 12);
  const int16_t w = static_cast<int16_t>(card.x + card.w - 6 - x);
  const DataLink l = data_link(s);
  const int u = home_urgent(s, now);
  if (u < 0 && all_clear_showing(s.ui, now)) {  // v0.5.0: some needed you, now none
    theme::sign(c, static_cast<int16_t>(card.x + card.w / 2), static_cast<int16_t>(card.y + 7), tr(egg::kAllClear),
                theme::Tone::Ok, theme::card_fill(theme::CardKind::Plain));
    return;
  }
  if (u < 0) {
    const CardLines lines = no_urgent_lines(s, l, now);
    const int16_t y = static_cast<int16_t>(card.y + (lines.sub.empty() ? 8 : 2));
    draw_fit(c, x, y, w, lines.top.c_str(), theme::over(Color::Ink3));
    if (!lines.sub.empty()) draw_fit(c, x, static_cast<int16_t>(y + 13), w, lines.sub.c_str(), theme::over(Color::Ink3));
    return;
  }
  const SessionRow& r = s.sessions.data.rows[u];
  if (r.st == SessionState::Waiting) {
    theme::stripe(c, static_cast<int16_t>(card.x + 4), static_cast<int16_t>(card.y + 5),
                  static_cast<int16_t>(card.h - 10), theme::Tone::Warn, Color::Card);
  }
  const int16_t y = static_cast<int16_t>(card.y + 2);
  const uint32_t age_s = session_age_s(s.sessions, static_cast<uint8_t>(u), data_as_of(s, now));
  const FixedStr<16> age = age_text(age_s);  // U22
  const int16_t ax = draw_right(c, static_cast<int16_t>(card.x + card.w - 6), y, age.c_str(), theme::over(Color::Warn));
  draw_fit(c, x, y, static_cast<int16_t>(ax - 6 - x), r.name.c_str(), theme::over(Color::Ink));
  int16_t right = static_cast<int16_t>(card.x + card.w - 6);
  const uint8_t older = home_older(s, now);  // U22: right-aligned and short, so it is never cut
  if (older > 0 && l == DataLink::Live) {
    char more[24];
    std::snprintf(more, sizeof more, tr(kOlder), older);
    right = static_cast<int16_t>(draw_right(c, right, static_cast<int16_t>(y + 13), more, theme::over(Color::Ink3)) - 6);
  }
  draw_fit(c, x, static_cast<int16_t>(y + 13), static_cast<int16_t>(right - x), urgent_sub(s, r, l, now).c_str(),
           theme::over(Color::Ink3));
}

// docs/19 §7.3: he hops once when a recording was saved, pops a "!" or "?", sniffs a while;
// only his box is redrawn, and only when he moved.
// v0.5.0: each pat (Opt+W) starts his hop again, with its heart.
Rect HomeModule::animate(Canvas& c, const Rect& area, uint32_t now) {
  const AppState& s = ctx_->state();
  if (s.ui.pat.n != pat_seen_ && pat_showing(s.ui.pat, now)) motion_ = theme::MandoMotion{};
  pat_seen_ = s.ui.pat.n;
  const theme::MandoView was = motion_.shown;
  motion_ = theme::mando_tick(motion_, home_mando(s, now));
  if (theme::same_view(was, motion_.shown)) return Rect{};
  const int16_t x = static_cast<int16_t>(area.x + kMandoX);
  const int16_t y = static_cast<int16_t>(area.y + kMandoY);
  const Rect box = theme::mando_box(theme::MandoSize::M46, x, y);
  c.fill(box, Color::Bg);
  theme::mando(c, x, y, theme::MandoSize::M46, motion_.shown);
  pat_heart(c, area, s, motion_, now);
  return box;
}

uint8_t HomeModule::help(const HelpRow** rows) const {
  *rows = kHelp;
  return static_cast<uint8_t>(sizeof kHelp / sizeof kHelp[0]);
}

}  // namespace cpa
