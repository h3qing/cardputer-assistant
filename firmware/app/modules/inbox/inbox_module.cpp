#include "inbox_module.h"

#include <cstdio>

#include "reducers.h"
#include "text_fit.h"
#include "theme.h"
#include "theme_mando.h"
#include "words.h"

namespace cpa {
namespace {

constexpr int16_t kRowH = 12;
// docs/19 §6.4 (the judges' list rows): 14 px rows, the text a pixel down and 8 px in from
// the ends of the selection capsule; a session that waits for you has a 3 px mustard stripe.
constexpr int16_t kListRowH = 14;
constexpr int16_t kListX = 8;
constexpr int16_t kNameX = kListX + 7;  // after the stripe
constexpr int16_t kListY = 14;          // the first row, from the area's top

// docs/21: the inbox's words.
constexpr Txt kWait{"等待", "wait"};
constexpr Txt kIdle{"空闲", "idle"};
constexpr Txt kWaitingN{"等你 (%u)", "Inbox (%u)"};
constexpr Txt kShowOnceOn{"接上后会显示等你的", "Sessions show once it's on"};
constexpr Txt kNoneThen{"那时没有等你的 session", "None needed you then"};
constexpr ToastTxt kOfflineNoDismiss{"离线，不能忽略", "Offline: can't dismiss"};
constexpr ToastTxt kTryLater{"请稍后再试", "Try again soon"};

// The Tab menu's 本页 rows (docs/13 §6.2): each replays the key it names.
constexpr HelpRow kHelp[] = {
    {KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Enter}, "Enter", "", &txt::kReplyRow},
    {KeyEvent{KeyEventKind::Chord, 0, kModOpt, 'd', Nav::None}, "Opt+D", "", &txt::kDismissRow},
    {KeyEvent{KeyEventKind::Char, ';', 0, 0, Nav::None}, ";", "", &txt::kUp},
    {KeyEvent{KeyEventKind::Char, '.', 0, 0, Nav::None}, ".", "", &txt::kDown},
    {KeyEvent{KeyEventKind::Char, '`', 0, 0, Nav::None}, "`", "", &txt::kBackRow},
};

Style on_row(Color fg, bool sel) { return sel ? theme::over(fg) : Style{fg, Color::Bg}; }

void row(Canvas& c, const Rect& area, int16_t y, const AppState& s, uint8_t index, bool sel, uint32_t now) {
  const SessionRow& r = s.sessions.data.rows[index];
  const bool waiting = r.st == SessionState::Waiting;
  const Color under = sel ? Color::Select : Color::Bg;
  if (sel) {
    c.fillRound(Rect{static_cast<int16_t>(area.x + 2), y, static_cast<int16_t>(area.w - 4), kListRowH}, kListRowH / 2,
                Color::Select, Color::Bg);
  }
  if (waiting) theme::stripe(c, static_cast<int16_t>(area.x + kListX), static_cast<int16_t>(y + 3), 8, theme::Tone::Warn, under);
  const int16_t ty = static_cast<int16_t>(y + 1);
  char right[48];  // docs/20 U22, tight; the detail line tells the kind (review: the name's room)
  std::snprintf(right, sizeof right, "%s %s", tr(waiting ? kWait : kIdle),
                age_text(session_age_s(s.sessions, index, data_as_of(s, now)), true).c_str());  // U19: stops offline
  const int16_t rx = draw_right(c, static_cast<int16_t>(area.x + area.w - kListX), ty, right,
                                on_row(waiting ? Color::Warn : Color::Ink, sel));
  const int16_t nx = static_cast<int16_t>(area.x + kNameX);
  draw_fit(c, nx, ty, static_cast<int16_t>(rx - 6 - nx), session_label(s.projects, r).c_str(), on_row(Color::Ink, sel));
}

// docs/20 U14: the selected session's whole name on the bottom lines, two when it needs them;
// a name that fits one leaves the second for what kind of session it is.
void render_detail(Canvas& c, const Rect& area, const AppState& s, const SessionRow& r) {
  const int16_t y = static_cast<int16_t>(area.y + area.h - 2 * kRowH - 3);
  const int16_t x = static_cast<int16_t>(area.x + kListX);
  const int16_t w = static_cast<int16_t>(area.w - 2 * kListX);
  const SessionLabel name = session_label(s.projects, r);
  const size_t first = utf8_fit(c, name.c_str(), name.size(), w);
  c.text(x, y, TextLine::from(name.c_str(), first).c_str(), Style{Color::Dim, Color::Bg});
  const char* rest = name.c_str() + first;
  draw_fit(c, x, static_cast<int16_t>(y + kRowH), w, *rest ? rest : inbox_detail(r).c_str(), Style{Color::Dim, Color::Bg});
}

// The title and, when the list is not live (docs/20 U19), how old it is; never synced, no
// count at all ("等你 (0)" would be a guess).
void render_title(Canvas& c, const Rect& area, const AppState& s, uint8_t n, uint32_t now) {
  char title[32];
  const DataLink l = data_link(s);
  std::snprintf(title, sizeof title, tr(l == DataLink::Never ? txt::kWaiting : kWaitingN), n);
  const int16_t x = static_cast<int16_t>(area.x + theme::kGutter);
  const int16_t end = static_cast<int16_t>(x + theme::title(c, x, area.y, title) + 6);
  if (l == DataLink::Live || l == DataLink::Never || n == 0) return;
  draw_fit(c, end, area.y, static_cast<int16_t>(area.x + area.w - theme::kGutter - end),
           stale_text(data_age_s(s, now)).c_str(), Style{Color::Ink3, Color::Bg});
}

struct EmptyText {
  FixedStr<48> top;
  FixedStr<48> sub;
};

// What an empty list says: all done only when it is live (docs/20 U19).
EmptyText empty_text(const AppState& s, uint32_t now) {
  const DataLink l = data_link(s);
  using Line = FixedStr<48>;
  if (l == DataLink::Live) return EmptyText{Line::from(tr(txt::kAllDone)), Line::from(tr(txt::kNoneNeedsYou))};
  if (l == DataLink::Never) return EmptyText{Line::from(tr(txt::kNoMacYet)), Line::from(tr(kShowOnceOn))};
  if (l == DataLink::NoCpad && !s.sessions.live) {
    return EmptyText{Line::from(tr(txt::kNoCpad)), Line::from(tr(txt::kCheckCpad))};
  }
  if (l == DataLink::Other) return EmptyText{Line::from(tr(txt::kVersionsDiffer)), Line::from(tr(txt::kPleaseUpdate))};
  return EmptyText{stale_text(data_age_s(s, now)), Line::from(tr(kNoneThen))};
}

// Nothing waits (docs/19 §6.4): Mando sits with his 在呢 sign; not live, he is faded (离线).
void render_empty(Canvas& c, const Rect& area, const AppState& s, uint32_t now) {
  const theme::MandoAct act = data_link(s) == DataLink::Live ? theme::MandoAct::Idle : theme::MandoAct::Offline;
  const int16_t mx = static_cast<int16_t>(area.x + 24);
  const int16_t my = static_cast<int16_t>(area.y + 18);
  theme::mando(c, mx, my, theme::MandoSize::M46, act, 0);
  theme::sign(c, static_cast<int16_t>(mx + 13), static_cast<int16_t>(my + 48), theme::mando_sign(act),
              theme::mando_sign_tone(act));
  const EmptyText t = empty_text(s, now);
  const int16_t tx = static_cast<int16_t>(area.x + 70);
  const int16_t w = static_cast<int16_t>(area.x + area.w - theme::kGutter - tx);
  draw_fit(c, tx, static_cast<int16_t>(area.y + 30), w, t.top.c_str(),
           Style{act == theme::MandoAct::Idle ? Color::Ink : Color::Ink3, Color::Bg});
  draw_fit(c, tx, static_cast<int16_t>(area.y + 46), w, t.sub.c_str(), Style{Color::Ink3, Color::Bg});
}

}  // namespace

bool InboxModule::begin(ModuleContext& ctx) {
  ctx_ = &ctx;
  return true;
}

void InboxModule::onEnter(uint8_t) { ui_ = inbox_pick(InboxUi{}, ctx_->state()); }  // U12: hold row 1

bool InboxModule::onKey(const KeyEvent& key) {
  const AppState& s = ctx_->state();
  const InboxStep st = inbox_next(ui_, key, s);
  ui_ = st.ui;
  if (st.cmd == InboxCmd::Offline) ctx_->toast(ToastLevel::Warn, tr(kOfflineNoDismiss));
  if (st.cmd == InboxCmd::Gone) ctx_->hint(ToastLevel::Info, selection_gone_text());
  if (st.cmd == InboxCmd::Reply) {
    ComposeSpec spec;
    spec.kind = ComposeKind::Feedback;
    spec.slot = s.sessions.data.rows[st.row].slot;
    spec.sid = some(s.sessions.data.rows[st.row].sid);
    ctx_->compose(spec);
  }
  if (st.cmd == InboxCmd::Dismiss) {
    OnlineRequest req;
    req.kind = RequestKind::InboxDismiss;
    req.sid = s.sessions.data.rows[st.row].sid;
    const bool sent = ctx_->request(req);
    const FixedStr<64> done = FixedStr<64>::from(tr(txt::kDismissed)).append(s.sessions.data.rows[st.row].name.c_str());
    ctx_->toast(sent ? ToastLevel::Info : ToastLevel::Warn, sent ? done.c_str() : tr(kTryLater));  // U12: which one
  }
  return st.handled;
}

void InboxModule::render(Canvas& c, const Rect& area) const {
  const AppState& s = ctx_->state();
  const InboxRows rows = inbox_rows(s);
  render_title(c, area, s, rows.n, ctx_->nowMs());
  if (rows.n == 0) return render_empty(c, area, s, ctx_->nowMs());
  const int sel = inbox_selected(ui_, rows, s);  // docs/20 U12: -1, the session left
  const uint8_t cursor = sel >= 0 ? static_cast<uint8_t>(sel) : (ui_.cursor >= rows.n ? rows.n - 1 : ui_.cursor);
  const int16_t detail_y = static_cast<int16_t>(area.y + area.h - 2 * kRowH - 3);
  const uint8_t visible = static_cast<uint8_t>((detail_y - area.y - kListY) / kListRowH);
  const uint8_t first = cursor >= visible ? static_cast<uint8_t>(cursor - visible + 1) : 0;
  for (uint8_t i = first, line = 0; i < rows.n && line < visible; ++i, ++line) {
    row(c, area, static_cast<int16_t>(area.y + kListY + line * kListRowH), s, rows.index[i], i == sel, ctx_->nowMs());
  }
  if (sel >= 0) render_detail(c, area, s, s.sessions.data.rows[rows.index[sel]]);
}

// docs/20 U7, U19: says whether Enter on the selected row reaches its session.
const char* InboxModule::hint() const {
  const AppState& s = ctx_->state();
  const InboxRows rows = inbox_rows(s);
  const int sel = inbox_selected(ui_, rows, s);
  const bool reaches = sel >= 0 && reply_reaches(s, s.sessions.data.rows[rows.index[sel]]);
  return reply_hint(s.link.mode == LinkMode::Ready, reaches, rows.n == 0);
}

uint8_t InboxModule::help(const HelpRow** rows) const {
  *rows = kHelp;
  return static_cast<uint8_t>(sizeof kHelp / sizeof kHelp[0]);
}

uint8_t InboxModule::contextSlot() const {
  const AppState& s = ctx_->state();
  const InboxRows rows = inbox_rows(s);
  const int sel = inbox_selected(ui_, rows, s);
  return sel < 0 ? 0 : s.sessions.data.rows[rows.index[sel]].slot;
}

}  // namespace cpa
