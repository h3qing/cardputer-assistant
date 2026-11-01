// Shell drawing: the status bar, the content area (module, Compose or Confirm) and the
// hint bar, where a toast replaces the hint while it is shown (04 §8.5 layout). v0.4.0:
// locked, the lock screen alone (no toasts: they can name a session); the Tab menu over
// everything but the status bar's ● while recording, a toast still over its hint row.
// v0.5 (docs/19 §6.2, §6.5): the menu is a sheet over the dimmed page, the status bar
// always stays; a toast is a capsule in its tone's colours.
#include <cstdio>
#include <cstring>

#include "lock_screen.h"
#include "reducers.h"
#include "shell.h"
#include "status_bar.h"
#include "text_fit.h"
#include "shell_hints.h"
#include "theme.h"
#include "words.h"

namespace cpa {
namespace {

// docs/21: the editor's and the confirm box's words.
constexpr Txt kNoProject{"未归属", "No project"};
constexpr Txt kNoteToMac{"记给 Mac", "Note to Mac"};
constexpr LineTxt kDiscardAsk{"放弃已输入的内容？", "Discard what you typed?"};
// docs/28 §3.2 (D11): the plug-in hint, over the hint bar for its 5 s.
constexpr ToastTxt kPlugShown{"离线 %u 条 → 这台 · Tab 不给", "%u offline go to this Mac · Tab: no"};
constexpr ToastTxt kPlugAsk{"不给这台 Mac？Enter 不给 · ` 照常", "Not this Mac? Enter: yes · `: no"};
constexpr ToastTxt kPlugBusy{"正在记下…", "Saving…"};
constexpr LineTxt kPermissions{"权限：按项目设置（默认 plan）", "Mode: per project (default plan)"};

constexpr int16_t kRowH = 12;
constexpr int16_t kTop = kContentRect.y;
// Compose (docs/19 §6.9): the title in a pill, the text on a card, 6 px in, the byte count
// and a refusal in the card's bottom corners.
constexpr int16_t kTitleY = 13;
constexpr Rect kTextCard{4, 28, 232, 92};
constexpr int16_t kCardPad = 6;
constexpr int16_t kTextX = kTextCard.x + kCardPad;
constexpr int16_t kTextY = kTextCard.y + kCardPad;
constexpr int16_t kCardFootY = kTextCard.y + kTextCard.h - kCardPad - kRowH;
constexpr int kTextRows = 5;      // wrapped Compose text rows, above the foot
constexpr size_t kRowChars = 36;  // ASCII characters per row (6 px each) inside the card
constexpr size_t kPreviewChars = 60; // 04 US-09 AC1: first 60 characters (code points)
constexpr int kPreviewRows = 4;      // 60 CJK characters (12 px) plus the quotes need 4 rows

using Preview = FixedStr<kPreviewChars * 4 + 8>;

int16_t row_y(int r) { return static_cast<int16_t>(kTop + r * kRowH); }

theme::Tone toast_tone(ToastLevel lv) {
  switch (lv) {
    case ToastLevel::Ok: return theme::Tone::Ok;
    case ToastLevel::Warn: return theme::Tone::Warn;
    case ToastLevel::Error: return theme::Tone::Rec;
    default: return theme::Tone::Info;
  }
}

const SessionRow* find_session(const AppState& s, const Opt<Sid>& sid) {
  for (uint8_t i = 0; sid.has && i < s.sessions.data.n; ++i) {
    if (s.sessions.data.rows[i].sid == sid.value) return &s.sessions.data.rows[i];
  }
  return nullptr;
}

// Where Compose and Confirm send: the project, or for a reply the session as every screen
// names it (docs/20 U14: never "key/key/…").
FixedStr<64> target_text(const AppState& s, const ComposeSpec& sp) {
  const Opt<ProjectRow> p = project_by_slot(s.projects, sp.slot);
  if (!p.has) return FixedStr<64>::from(tr(kNoProject));
  FixedStr<64> out = FixedStr<64>::from(p.value.key.c_str());
  if (sp.kind != ComposeKind::Feedback || !sp.sid.has) return out;
  if (const SessionRow* r = find_session(s, sp.sid)) return FixedStr<64>::from(session_label(s.projects, *r).c_str());
  return out.append("/").append(sp.sid.value.c_str());
}

// docs/20 U7: the editor's title says where a reply goes: 回复 when it reaches the session,
// else 记给 Mac (it is only stored on the Mac).
const char* compose_title_label(const AppState& s, const ComposeSpec& sp) {
  if (sp.kind != ComposeKind::Feedback) return compose_kind_label(sp.kind);
  const SessionRow* r = find_session(s, sp.sid);
  return r != nullptr && reply_reaches(s, *r) ? compose_kind_label(sp.kind) : tr(kNoteToMac);
}

// docs/20 U14: which session a reply goes to, so two alike ones differ on the confirm box:
// its name's end, a short id, how long it waited ("…webhook-timeout · aaaa · 5 分钟").
TextLine session_identity(const Canvas& c, const AppState& s, const Opt<Sid>& sid, uint32_t now) {
  const SessionRow* r = find_session(s, sid);
  if (r == nullptr) return TextLine::from(sid.has ? sid.value.c_str() : "");
  const uint8_t index = static_cast<uint8_t>(r - s.sessions.data.rows);
  char tail[48];
  std::snprintf(tail, sizeof tail, " · %s · %s", short_sid(s.sessions, r->sid).c_str(),
                age_text(session_age_s(s.sessions, index, data_as_of(s, now))).c_str());
  const SessionLabel name = session_label(s.projects, *r);
  // 2 px short of the edge: the age's last letter would light the screen's last column
  return fit_tail(c, name.c_str(), static_cast<int16_t>(kScreenW - 2 - c.textWidth(tail))).append(tail);
}

// The editor's rows: at most kRowChars ASCII characters each, broken after the last space
// that fits (docs/20 U22: never "o / n"); a word longer than a row breaks inside it.
constexpr size_t kWrapRowsMax = 64;  // 501 characters break into at most 2 rows per 37
struct WrapRows {
  uint16_t at[kWrapRowsMax] = {};
  uint16_t len[kWrapRowsMax] = {};
  uint8_t n = 0;
};

WrapRows wrap_rows(const char* text, size_t n) {
  WrapRows w;
  size_t start = 0;
  while (start < n && w.n < kWrapRowsMax) {
    size_t take = n - start <= kRowChars ? n - start : kRowChars;
    size_t next = start + take;
    if (next < n) {
      size_t brk = next;  // a space just past the row may end it too
      while (brk > start && text[brk] != ' ') --brk;
      if (brk > start) {
        take = brk - start;
        next = brk + 1;  // the space it broke at is not drawn
      }
    }
    w.at[w.n] = static_cast<uint16_t>(start);
    w.len[w.n++] = static_cast<uint16_t>(take);
    start = next;
  }
  return w;
}

// The last kTextRows rows of the text (the cursor is at its end), on the card.
void draw_wrapped(Canvas& c, const char* text, size_t n) {
  const WrapRows w = wrap_rows(text, n);
  const uint8_t skip = w.n > kTextRows ? static_cast<uint8_t>(w.n - kTextRows) : 0;
  char line[kRowChars + 2];
  for (uint8_t r = skip; r < w.n; ++r) {
    std::memcpy(line, text + w.at[r], w.len[r]);
    line[w.len[r]] = '\0';
    c.text(kTextX, static_cast<int16_t>(kTextY + (r - skip) * kRowH), line, theme::over(Color::Ink));
  }
}

// The first kPreviewChars code points of t, quoted, with "..." when there is more.
Preview preview_text(const UserText& t) {
  size_t n = 0;
  for (size_t k = 0; k < kPreviewChars && n < t.size(); ++k) n += utf8_at(t.c_str(), t.size(), n).len;
  return Preview::from("\"").append(t.c_str(), n).append(n < t.size() ? "...\"" : "\"");
}

// UTF-8 text (s[n] == '\0') wrapped by pixel width on character boundaries (04 US-14
// AC2), at most `rows` rows from row `first`; the last row ends in "…" if text remains.
// Rows end a pixel short of the edge: an ASCII letter's ink can fill its cell's last column.
void draw_wrapped_utf8(Canvas& c, const char* s, size_t n, int first, int rows) {
  constexpr int16_t kRowW = kScreenW - 1;
  size_t at = 0;
  for (int r = 0; r + 1 < rows && at < n; ++r) {
    const size_t k = utf8_fit(c, s + at, n - at, kRowW);
    c.text(0, row_y(first + r), TextLine::from(s + at, k).c_str(), Style{});
    at += k;
  }
  if (at < n) draw_fit(c, 0, row_y(first + rows - 1), kRowW, s + at, Style{});
}

// The words of v0.4's question on the confirm card, its keys as keycaps (docs/19 §6.7).
void draw_discard_prompt(Canvas& c) {
  theme::card(c, kTextCard);
  c.text(12, 40, tr(kDiscardAsk), Style{Color::Warn, Color::Card});
  theme::keys_text(c, 12, 72, tr(txt::kDiscardKeys), Color::Ink, Color::Card);
}

}  // namespace

void Shell::render(Canvas& c, uint32_t now) const {
  if (lock_.locked) {
    const uint8_t z = theme::mando_view(mando_, theme::MandoAct::Doze).z;  // as the last tick left them
    return render_lock_screen(c, state(), lock_lit(now), now, z);
  }
  render_status_bar(c, state(), now);
  render_content(c, now);
  if (menu_open_) render_menu(c);  // over the page, which it dims
  const Toast* t = toast_current(toasts_);
  if (!menu_open_ && state().rec.kept.plug != PlugHint::None) return render_plug(c);  // docs/28: its 5 s
  if (!t && menu_open_) return;  // the menu drew its own hint
  if (!t) return render_hint(c);
  theme::toast_bar(c, t->text.c_str(), toast_tone(t->level));
}

void Shell::render_content(Canvas& c, uint32_t now) const {
  c.fill(kContentRect, Color::Bg);
  if (rec_up()) {
    render_rec(c, now);
  } else if (overlay_ == proto::Overlay::Compose) {
    render_compose(c);
  } else if (overlay_ == proto::Overlay::Confirm) {
    render_confirm(c, now);
  } else if (const Module* m = top()) {
    m->render(c, kContentRect);
  }
}

// The exit key is always named in plain words (issue #15): Backspace leaves an empty
// editor, ` one with text (it asks first; docs/20 D5) and everything else. The editor's
// hints end with "Tab菜单" (docs/13 §5.6); the confirm boxes' keep their two keys.
const char* Shell::hint_text() const {
  if (rec_up()) return rec_hint();
  if (overlay_ == proto::Overlay::Compose) {
    if (compose_.discard) return tr(txt::kDiscardHint);
    if (compose_.text.empty()) return tr(txt::kEmptyHint);
    return tr(txt::kTextHint);  // Opt+0-9 目标 is on the menu's 本页
  }
  if (overlay_ == proto::Overlay::Confirm) return confirm_keys(confirm_decide(confirm_.spec, state()), confirm_.draft);
  const Module* m = top();
  return m ? m->hint() : "";
}

// docs/21 §6.6: a page module's structured hint is its keys and then [Tab] 菜单 / menu, in
// the shown language, never parsed; more keys than the bar holds draw as one line.
void Shell::render_plug(Canvas& c) const {
  const KeptView& k = state().rec.kept;
  char buf[96];
  std::snprintf(buf, sizeof buf, tr(kPlugShown), static_cast<unsigned>(k.plug_n));
  if (k.plug == PlugHint::Asking) return theme::toast_bar(c, tr(kPlugAsk), theme::Tone::Warn);
  theme::toast_bar(c, k.plug == PlugHint::Busy ? tr(kPlugBusy) : buf, theme::Tone::Info);
}

void Shell::render_hint(Canvas& c) const {
  const Module* m = rec_up() || overlay_ != proto::Overlay::None ? nullptr : top();
  const HintKey* keys = nullptr;
  const uint8_t n = m ? m->hintKeys(&keys) : 0;
  if (n == 0 || keys == nullptr) return render_hint_bar(c, hint_text());
  theme::HintPart parts[theme::kHintItemsMax + 1];
  const uint8_t k = n < theme::kHintItemsMax ? n : theme::kHintItemsMax;
  for (uint8_t i = 0; i < k; ++i) parts[i] = theme::HintPart{keys[i].keys, tr(keys[i].words)};
  parts[k] = theme::HintPart{"Tab", tr(txt::kMenu)};
  theme::hint_bar(c, theme::hint_line(parts, static_cast<uint8_t>(k + 1)));
}

void Shell::render_compose(Canvas& c) const {
  const AppState& s = state();
  char title[96];
  std::snprintf(title, sizeof title, "%s → %s", compose_title_label(s, compose_.spec),
                target_text(s, compose_.spec).c_str());
  const TextLine fitted = fit_text(c, title, static_cast<int16_t>(kTextCard.w - 8));
  theme::pill(c, kTextCard.x, kTitleY, fitted.c_str(), theme::Tone::Neutral, Color::Bg);
  if (compose_.discard) return draw_discard_prompt(c);
  theme::card(c, kTextCard);
  const UserText cursor = compose_.text.append("_");
  draw_wrapped(c, cursor.c_str(), cursor.size());
  char count[16];
  std::snprintf(count, sizeof count, "%u/500", static_cast<unsigned>(compose_.text.size()));
  draw_right(c, static_cast<int16_t>(kTextCard.x + kTextCard.w - kCardPad), kCardFootY, count, theme::over(Color::Ink3));
  if (compose_dash_blocked(compose_)) c.text(kTextX, kCardFootY, compose_result_text(ComposeResult::DashText), theme::over(Color::Warn));
}

// A reply's title names the project; the session is on the next row, whole enough to tell
// it from another that starts alike (docs/20 U14).
void Shell::render_confirm(Canvas& c, uint32_t now) const {
  const ConfirmSpec& sp = confirm_.spec;
  const AppState& s = state();
  const Decision d = confirm_decide(sp, s);
  ComposeSpec target;
  target.kind = ComposeKind::Spawn;  // the project alone
  target.slot = sp.slot;
  if (sp.kind == ConfirmKind::Spawn && d.route == Route::Execute) {
    c.text(0, row_y(1), tr(kPermissions), Style{Color::Dim, Color::Bg});
  }
  if (sp.kind == ConfirmKind::Feedback) {
    c.text(0, row_y(1), session_identity(c, s, sp.sid, now).c_str(), Style{Color::Ink2, Color::Bg});
  }
  const ConfirmText line = confirm_title(sp.kind, d, target_text(s, target).c_str());  // docs/20 U7
  draw_fit(c, 0, row_y(0), kScreenW, line.c_str(),
           Style{d.route == Route::Execute && !d.maybe_open ? Color::Ok : Color::Warn, Color::Bg});
  if (d.maybe_open) c.text(0, row_y(2), confirm_note(d), Style{Color::Warn, Color::Bg});  // docs/20 U25
  // Queue items come from the daemon and can be Chinese: never cut inside a character.
  const Preview quoted = preview_text(sp.text);
  draw_wrapped_utf8(c, quoted.c_str(), quoted.size(), 3, kPreviewRows);
  theme::keys_text(c, theme::kGutter, row_y(7), confirm_keys(d, confirm_.draft), Color::Ink, Color::Bg);  // U22
}

}  // namespace cpa
