#include "board_module.h"

#include <cstddef>
#include <cstdio>

#include "reducers.h"
#include "text_fit.h"
#include "board_texts.h"
#include "theme.h"

namespace cpa {
namespace {

constexpr int16_t kRowH = 12;
// docs/19 §6.4: the board's cards, three to a screen, below the title line.
constexpr int16_t kCardTop = 17;  // from the area's top
constexpr int16_t kCardH = 28;
constexpr int16_t kCardPitch = 31;
constexpr uint8_t kCardsVisible = 3;
constexpr int16_t kTextX = 23;  // a card's text, right of the number keycap
// A project page's list (the judges' rows): 14 px rows, the text a pixel down and 8 px in
// from the ends of the selection capsule.
constexpr int16_t kListRowH = 14;
constexpr int16_t kListX = 8;
constexpr int16_t kMarkW = 24;  // a queue item's zh mark (开 / 回 / · / 开?), then its text
constexpr int16_t kMarkWEn = 30;  // docs/21: "new?" is 24 px, so English leaves 6 px after it too

constexpr KeyEvent key_chr(char c) { return KeyEvent{KeyEventKind::Char, c, 0, 0, Nav::None}; }
constexpr KeyEvent key_opt(char k) { return KeyEvent{KeyEventKind::Chord, 0, kModOpt, k, Nav::None}; }
constexpr KeyEvent kEnter{KeyEventKind::Nav, 0, 0, 0, Nav::Enter};

// The Tab menu's "Here" rows (docs/13 §6.2): each replays the key it names.
constexpr HelpRow kHomeHelp[] = {
    {kEnter, "Enter", "", &txt::kOpenProject},
    {key_chr(';'), ";", "", &txt::kUp},
    {key_chr('.'), ".", "", &txt::kDown},
};
constexpr HelpRow kSessionsHelp[] = {
    {kEnter, "Enter", "", &txt::kReplyRow},
    {key_opt('d'), "Opt+D", "", &txt::kDismissRow},
    {key_chr(';'), ";", "", &txt::kUp},
    {key_chr('.'), ".", "", &txt::kDown},
    {key_chr('/'), ",/", "", &txt::kToQueue},
    {key_chr('`'), "`", "", &txt::kBackRow},
};
constexpr HelpRow kQueueHelp[] = {
    {kEnter, "Enter", "", &txt::kSpawnAsks},
    {key_opt('d'), "Opt+D", "", &txt::kMarkDone},
    {key_chr(';'), ";", "", &txt::kUp},
    {key_chr('.'), ".", "", &txt::kDown},
    {key_chr('/'), ",/", "", &txt::kToSessions},
    {key_chr('`'), "`", "", &txt::kBackRow},
};

template <size_t N>
uint8_t rows_of(const HelpRow (&a)[N], const HelpRow** out) {
  *out = a;
  return static_cast<uint8_t>(N);
}

// A list row on a project page: the selection is a Select capsule; Dim (Ink3) text there
// reads too weakly, so it becomes Ink2 unless it means "not available now".
Style row_style(bool selected, Color fg = Color::Ink) {
  if (selected && fg == Color::Dim) fg = Color::Ink2;
  return selected ? theme::over(fg) : Style{fg, Color::Bg};
}

void select_fill(Canvas& c, const Rect& area, int16_t y, bool selected) {
  if (!selected) return;
  c.fillRound(Rect{static_cast<int16_t>(area.x + 2), y, static_cast<int16_t>(area.w - 4), kListRowH}, kListRowH / 2,
              Color::Select, Color::Bg);
}

uint8_t list_rows(const Rect& area, int16_t y0) { return static_cast<uint8_t>((area.y + area.h - y0) / kListRowH); }

// First visible row so that the cursor stays on screen. sel: the selected row, or -1
// once it is gone (docs/20 U12): the page then stays where the cursor last was.
uint8_t first_row(int sel, uint8_t cursor, uint8_t visible) {
  const uint8_t at = sel >= 0 ? static_cast<uint8_t>(sel) : cursor;
  return at >= visible ? static_cast<uint8_t>(at - visible + 1) : 0;
}

struct Count {
  const char* kind;  // run / ! / Q / all
  uint16_t n;
  theme::Tone tone;
};

// The counts as pills, right-aligned to x_end, the last first; a zero is plain Ink3 and
// offline counts are "--". Returns the left edge.
int16_t draw_counts(Canvas& c, int16_t x_end, int16_t y, const Count* counts, uint8_t n, bool live, Color under) {
  int16_t x = x_end;
  for (int i = n - 1; i >= 0; --i) {
    char buf[16];
    if (live) {
      std::snprintf(buf, sizeof buf, "%s%u", counts[i].kind, static_cast<unsigned>(counts[i].n));
    } else {
      std::snprintf(buf, sizeof buf, "%s--", counts[i].kind);
    }
    x = static_cast<int16_t>(x - theme::pill_width(c, buf));
    if (live && counts[i].n > 0) {
      theme::pill(c, x, y, buf, counts[i].tone, under);
    } else {
      c.text(static_cast<int16_t>(x + 4), y, buf, theme::over(Color::Ink3));
    }
    x = static_cast<int16_t>(x - 3);
  }
  return x;
}

struct CardText {
  const char* num;  // "" for none (Other)
  const char* title;
  Color title_ink;
  const char* sub;
};

void project_card(Canvas& c, const Rect& r, const CardText& t, const Count* counts, uint8_t n, bool live, bool sel) {
  const theme::CardKind kind = sel ? theme::CardKind::Selected : theme::CardKind::Plain;
  const Color under = theme::card_fill(kind);
  theme::card(c, r, kind);
  if (t.num[0] != '\0') {
    theme::Keycap k;
    k.radius = 3;
    k.pad = 3;
    k.under = under;
    k.cap = sel ? Color::Accent : Color::Cap;
    k.ink = sel ? Color::OnAccent : Color::Ink2;
    theme::keycap(c, static_cast<int16_t>(r.x + 5), static_cast<int16_t>(r.y + 8), t.num, k);
  }
  const int16_t right = draw_counts(c, static_cast<int16_t>(r.x + r.w - 5), static_cast<int16_t>(r.y + 8), counts, n,
                                    live, under);
  const int16_t tx = static_cast<int16_t>(r.x + kTextX);
  const int16_t w = static_cast<int16_t>(right - 3 - tx);
  draw_fit(c, tx, static_cast<int16_t>(r.y + 2), w, t.title, theme::over(t.title_ink));
  draw_fit(c, tx, static_cast<int16_t>(r.y + 14), w, t.sub, theme::over(sel ? Color::Ink2 : Color::Ink3));
}

Rect card_rect(const Rect& area, uint8_t line) {
  return Rect{static_cast<int16_t>(area.x + theme::kGutter), static_cast<int16_t>(area.y + kCardTop + line * kCardPitch),
              static_cast<int16_t>(area.w - 2 * theme::kGutter), kCardH};
}

void project_row(Canvas& c, const Rect& r, const ProjectRow& p, bool live, bool sel) {
  char slot[4];
  std::snprintf(slot, sizeof slot, "%u", p.slot);
  const Count counts[] = {{tr(txt::kRunCount), p.run, theme::Tone::Ok}, {tr(txt::kNeedCount), p.need, theme::Tone::Warn},
                          {tr(txt::kQueueCount), p.queued, theme::Tone::Neutral}};
  const CardText t{slot, p.key.c_str(), p.ok ? Color::Ink : Color::Warn, p.name.c_str()};
  project_card(c, r, t, counts, 3, live, sel);
}

void other_row(Canvas& c, const Rect& r, const ProjectsView& v, bool sel) {
  char sub[48];
  const unsigned n = v.data.other.n;
  std::snprintf(sub, sizeof sub, trn(txt::kOtherSessions, n), n);
  const Count counts[] = {{tr(txt::kRunCount), v.data.other.run, theme::Tone::Ok},
                          {tr(txt::kAllCount), v.data.other.n, theme::Tone::Neutral}};
  project_card(c, r, CardText{"", tr(txt::kOther), Color::Ink, v.live ? sub : ""}, counts, 2, v.live, sel);
}

// After the title: why nothing updates (an incompatible host, Claude missing, how old the
// board is), or else the sentence.
void title_line(Canvas& c, int16_t x, int16_t y, const AppState& s, uint32_t now) {
  const int16_t w = static_cast<int16_t>(kScreenW - theme::kGutter - x);
  if (s.link.mode == LinkMode::Attached && s.link.version_mismatch) {  // 04 US-04 AC3, E7
    draw_fit(c, x, y, w, tr(txt::kUpdate), Style{Color::Error, Color::Bg});
  } else if (s.link.mode == LinkMode::Ready && s.link.agent != AgentStatus::Ok) {
    draw_fit(c, x, y, w, tr(txt::kNoAgentLine), Style{Color::Warn, Color::Bg});
  } else if (s.link.mode != LinkMode::Ready && s.projects.live) {
    char buf[48];  // docs/20 U22: the one age form of every page
    std::snprintf(buf, sizeof buf, tr(txt::kUpdatedAgo), age_text((now - s.projects.rx_ms) / 1000).c_str());
    draw_fit(c, x, y, w, buf, Style{Color::Ink3, Color::Bg});
  } else {
    draw_fit(c, x, y, w, board_sentence(s.projects).c_str(), Style{Color::Ink3, Color::Bg});
  }
}

}  // namespace

bool BoardModule::begin(ModuleContext& ctx) {
  ctx_ = &ctx;
  return true;
}

void BoardModule::onEnter(uint8_t arg) {
  BoardUi ui;
  ui.slot = arg;
  ui_ = board_pick(ui, ctx_->state());  // docs/20 U12: the first row is held from the start
}

bool BoardModule::onKey(const KeyEvent& key) {
  const BoardStep st = board_next(ui_, key, ctx_->state());
  ui_ = st.ui;
  run(st);
  return st.handled;
}

void BoardModule::run(const BoardStep& st) {
  const AppState& s = ctx_->state();
  const SessionRow* session = board_session(s, ui_.slot, st.arg);
  const QueueRow* item = board_queue_item(s, ui_.slot, st.arg);
  OnlineRequest req;
  switch (st.cmd) {
    case BoardCmd::OpenProject: return ctx_->open("board", st.arg);
    case BoardCmd::Reply: {
      ComposeSpec spec;
      spec.kind = ComposeKind::Feedback;
      spec.slot = ui_.slot;
      spec.sid = some(session->sid);
      return ctx_->compose(spec);
    }
    case BoardCmd::SpawnItem: {
      ConfirmSpec spec;
      spec.slot = ui_.slot;
      spec.qid = some(item->qid);
      spec.text = UserText::from(item->text.c_str(), item->text.size());
      return ctx_->confirm(spec);
    }
    case BoardCmd::DoneItem: req.kind = RequestKind::QueueDone; req.qid = item->qid; break;
    case BoardCmd::Dismiss: req.kind = RequestKind::InboxDismiss; req.sid = session->sid; break;
    case BoardCmd::Offline: return ctx_->toast(ToastLevel::Warn, tr(txt::kOfflineNotNow));
    case BoardCmd::NoAgent: return ctx_->toast(ToastLevel::Warn, tr(txt::kNoAgentToast));
    case BoardCmd::OtherRow: return ctx_->toast(ToastLevel::Info, tr(txt::kOtherOnMac));
    case BoardCmd::Gone: return ctx_->hint(ToastLevel::Info, selection_gone_text());
    default: return;
  }
  const bool sent = ctx_->request(req);
  if (!sent) return ctx_->toast(ToastLevel::Warn, tr(txt::kTryLaterBusy));
  const bool done = st.cmd == BoardCmd::DoneItem;  // docs/20 U12: the toast names which one
  const FixedStr<96> what = FixedStr<96>::from(tr(done ? txt::kDone : txt::kDismissed))
                                .append(done ? item->text.c_str() : session->name.c_str());
  ctx_->toast(ToastLevel::Info, what.c_str());
}

void BoardModule::render(Canvas& c, const Rect& area) const {
  if (ui_.slot == 0) return render_home(c, area);
  render_project(c, area);
}

void BoardModule::render_home(Canvas& c, const Rect& area) const {
  const AppState& s = ctx_->state();
  const ProjectsView& v = s.projects;
  if (v.data.n == 0) {
    c.text(area.x, area.y, tr(txt::kNoProjectsYet), Style{});
    c.text(area.x, static_cast<int16_t>(area.y + kRowH), "cpa project add <key> <path> --slot N", Style{Color::Dim, Color::Bg});
    return;
  }
  const int16_t ty = static_cast<int16_t>(area.y + 2);
  const int16_t tx = static_cast<int16_t>(area.x + 5);
  title_line(c, static_cast<int16_t>(tx + theme::title(c, tx, ty, tr(txt::kProjects)) + 6), ty, s, ctx_->nowMs());
  const uint8_t rows = board_row_count(ui_, s);
  const int cur = board_selected(ui_, s);
  for (uint8_t i = first_row(cur, ui_.cursor, kCardsVisible), line = 0; i < rows && line < kCardsVisible; ++i, ++line) {
    const bool sel = i == cur;
    if (i < v.data.n) {
      project_row(c, card_rect(area, line), v.data.rows[i], v.live, sel);
    } else {
      other_row(c, card_rect(area, line), v, sel);
    }
  }
}

void BoardModule::render_project(Canvas& c, const Rect& area) const {
  const AppState& s = ctx_->state();
  const Opt<ProjectRow> p = project_by_slot(s.projects, ui_.slot);
  const int16_t ty = static_cast<int16_t>(area.y + 1);
  char slot[4];
  std::snprintf(slot, sizeof slot, "%u", ui_.slot);
  theme::Keycap k;
  k.radius = 3;
  k.pad = 3;
  int16_t x = static_cast<int16_t>(area.x + theme::kGutter);
  x = static_cast<int16_t>(x + theme::keycap(c, x, ty, slot, k) + 5);
  x = static_cast<int16_t>(x + theme::title(c, x, ty, p.has ? p.value.key.c_str() : "?") + 6);
  draw_fit(c, x, ty, static_cast<int16_t>(area.x + area.w - theme::kGutter - x), p.has ? p.value.name.c_str() : "",
           Style{Color::Ink3, Color::Bg});
  char tab[32];
  const int16_t tab_y = static_cast<int16_t>(area.y + 13);
  const bool on_sessions = ui_.tab == BoardTab::Sessions;
  std::snprintf(tab, sizeof tab, "Sessions(%u)", board_session_count(s, ui_.slot));  // docs/20 U22, U25
  x = static_cast<int16_t>(area.x + theme::kGutter);
  x = static_cast<int16_t>(x + 4 + theme::pill(c, x, tab_y, tab, on_sessions ? theme::Tone::Accent : theme::Tone::Neutral,
                                               Color::Bg));
  std::snprintf(tab, sizeof tab, tr(txt::kQueueTab), queue_count_for_slot(s.queue, ui_.slot));
  theme::pill(c, x, tab_y, tab, on_sessions ? theme::Tone::Neutral : theme::Tone::Accent, Color::Bg);
  const int16_t list_y = static_cast<int16_t>(area.y + 2 * kRowH + 2);
  if (on_sessions) return render_sessions(c, area, list_y);
  render_queue(c, area, list_y);
}

void BoardModule::render_sessions(Canvas& c, const Rect& area, int16_t y0) const {
  const AppState& s = ctx_->state();
  const uint8_t rows = board_session_count(s, ui_.slot);
  if (rows == 0) return c.text(static_cast<int16_t>(area.x + kListX), y0, tr(txt::kNoSessions), Style{Color::Dim, Color::Bg});
  const uint8_t visible = list_rows(area, y0);
  const int cur = board_selected(ui_, s);
  for (uint8_t i = first_row(cur, ui_.cursor, visible), line = 0; i < rows && line < visible; ++i, ++line) {
    const SessionRow* r = board_session(s, ui_.slot, i);
    const int16_t y = static_cast<int16_t>(y0 + line * kListRowH);
    const bool sel = i == cur;
    select_fill(c, area, y, sel);
    const uint8_t index = static_cast<uint8_t>(r - s.sessions.data.rows);
    char right[48];
    std::snprintf(right, sizeof right, "%s %s %s", session_state_text(r->st),
                  age_text(session_age_s(s.sessions, index, data_as_of(s, ctx_->nowMs())), true).c_str(),  // U19
                  session_kind_text(r->kind));
    const int16_t ty = static_cast<int16_t>(y + 1);
    const int16_t rx = draw_right(c, static_cast<int16_t>(area.x + area.w - kListX), ty, right,
                                  row_style(sel, r->need ? Color::Warn : Color::Dim));
    const int16_t nx = static_cast<int16_t>(area.x + kListX);
    draw_fit(c, nx, ty, static_cast<int16_t>(rx - 6 - nx), r->name.c_str(), row_style(sel));
  }
}

// Offline (or with no Claude) Enter and Opt+D do nothing here: the items are grey, and the
// selected one reads in Ink2 with an "offline" pill that says why.
void BoardModule::render_queue(Canvas& c, const Rect& area, int16_t y0) const {
  const AppState& s = ctx_->state();
  const uint8_t rows = board_row_count(ui_, s);
  if (rows == 0) return c.text(static_cast<int16_t>(area.x + kListX), y0, tr(txt::kQueueEmpty), Style{Color::Dim, Color::Bg});
  const bool live = s.link.mode == LinkMode::Ready && s.link.agent == AgentStatus::Ok;
  const uint8_t visible = list_rows(area, y0);
  const int cur = board_selected(ui_, s);
  for (uint8_t i = first_row(cur, ui_.cursor, visible), line = 0; i < rows && line < visible; ++i, ++line) {
    const QueueRow* q = board_queue_item(s, ui_.slot, i);
    const int16_t y = static_cast<int16_t>(y0 + line * kListRowH);
    const int16_t ty = static_cast<int16_t>(y + 1);
    const bool sel = i == cur;
    select_fill(c, area, y, sel);
    const char* mark = q->intent == Intent::Spawn ? tr(txt::kSpawnMark)
                                                  : (q->intent == Intent::Feedback ? tr(txt::kReplyMark) : "\xC2\xB7");
    const bool unsure = q->intent == Intent::Spawn && q->uncertain;  // docs/20 U25: it may already run
    c.text(static_cast<int16_t>(area.x + kListX), ty, unsure ? tr(txt::kUnsureMark) : mark, row_style(sel, unsure ? Color::Warn : Color::Accent));
    int16_t right = static_cast<int16_t>(area.x + area.w - kListX);
    if (!live && sel) {
      right = static_cast<int16_t>(right - theme::pill_width(c, tr(txt::kOfflinePill)));
      theme::pill(c, right, ty, tr(txt::kOfflinePill), theme::Tone::Neutral, Color::Select);
      right = static_cast<int16_t>(right - 4);
    }
    const int16_t qx = static_cast<int16_t>(area.x + kListX + (lang() == Lang::En ? kMarkWEn : kMarkW));
    draw_fit(c, qx, ty, static_cast<int16_t>(right - qx), q->text.c_str(), row_style(sel, live ? Color::Ink : Color::Dim));
  }
}

// docs/13 §5.6: up to three keys, then "[Tab]menu" at the right end. docs/20 U10: the overview
// names its back key too; U7, U19: Sessions says whether Enter reaches the selected one.
const char* BoardModule::hint() const {
  if (ui_.slot == 0) return tr(txt::kOverviewHint);  // ;. are on the Tab menu (docs/19 §6.4)
  if (ui_.tab == BoardTab::Sessions) {
    const AppState& s = ctx_->state();
    const int at = board_selected(ui_, s);
    const SessionRow* r = at >= 0 ? board_session(s, ui_.slot, static_cast<uint8_t>(at)) : nullptr;
    const bool reaches = r != nullptr && reply_reaches(s, *r);
    return reply_hint(s.link.mode == LinkMode::Ready, reaches, board_row_count(ui_, s) == 0);
  }
  return tr(txt::kQueueHint);  // Opt+D (mark done) is on the Tab menu: the bar would be crowded
}

uint8_t BoardModule::help(const HelpRow** rows) const {
  if (ui_.slot == 0) return rows_of(kHomeHelp, rows);
  return ui_.tab == BoardTab::Sessions ? rows_of(kSessionsHelp, rows) : rows_of(kQueueHelp, rows);
}

}  // namespace cpa
