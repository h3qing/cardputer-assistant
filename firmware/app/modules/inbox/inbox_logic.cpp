#include "inbox_logic.h"

#include "keymap.h"
#include "txt.h"

namespace cpa {

constexpr LineTxt kBgSession{"后台 session", "Background session"};
constexpr LineTxt kTtySession{"终端 session：在 Mac 上处理", "Terminal session: answer on the Mac"};
namespace {

// Waiting before idle; within a group the longest wait first.
bool before(const SessionRow& a, const SessionRow& b) {
  const bool aw = a.st == SessionState::Waiting;
  const bool bw = b.st == SessionState::Waiting;
  if (aw != bw) return aw;
  return a.age_s > b.age_s;
}

uint8_t clamp_cursor(uint8_t cursor, uint8_t n) { return n == 0 ? 0 : (cursor >= n ? static_cast<uint8_t>(n - 1) : cursor); }

InboxStep step(const InboxUi& ui, InboxCmd cmd, uint8_t row, bool handled) { return InboxStep{ui, cmd, row, handled}; }

// ui with the session at row `at` picked.
InboxUi picked(const InboxUi& ui, const InboxRows& rows, const AppState& s, uint8_t at) {
  InboxUi out = ui;
  out.cursor = at;
  out.picked = rows.n > 0;
  if (out.picked) out.sel = s.sessions.data.rows[rows.index[at]].sid;
  return out;
}

// ; and . from the selected row; once it is gone, . takes the row that took its place.
InboxUi moved(const InboxUi& ui, const InboxRows& rows, const AppState& s, int delta) {
  const int at = inbox_selected(ui, rows, s);
  const int base = at >= 0 ? at + delta : clamp_cursor(ui.cursor, rows.n) + (delta < 0 ? delta : 0);
  const int last = rows.n > 0 ? rows.n - 1 : 0;
  return picked(ui, rows, s, static_cast<uint8_t>(base < 0 ? 0 : (base > last ? last : base)));
}

}  // namespace

InboxRows inbox_rows(const AppState& s) {
  InboxRows out;
  const SessionsData& d = s.sessions.data;
  for (uint8_t i = 0; i < d.n; ++i) {
    if (!d.rows[i].need) continue;
    uint8_t at = out.n;
    while (at > 0 && before(d.rows[i], d.rows[out.index[at - 1]])) {
      out.index[at] = out.index[at - 1];
      --at;
    }
    out.index[at] = i;
    out.n++;
  }
  return out;
}

int inbox_selected(const InboxUi& ui, const InboxRows& rows, const AppState& s) {
  if (rows.n == 0) return -1;
  if (!ui.picked) return clamp_cursor(ui.cursor, rows.n);
  for (uint8_t i = 0; i < rows.n; ++i) {
    if (s.sessions.data.rows[rows.index[i]].sid == ui.sel) return i;
  }
  return -1;
}

InboxUi inbox_pick(const InboxUi& ui, const AppState& s) {
  const InboxRows rows = inbox_rows(s);
  const int at = inbox_selected(ui, rows, s);
  return at < 0 ? ui : picked(ui, rows, s, static_cast<uint8_t>(at));
}

InboxStep inbox_next(const InboxUi& ui, const KeyEvent& ev, const AppState& s) {
  const InboxRows rows = inbox_rows(s);
  const int at = inbox_selected(ui, rows, s);
  const Resolved r = keymap_lookup(Layer::List, ev);
  switch (r.action) {
    case Action::Up: return step(moved(ui, rows, s, -1), InboxCmd::None, 0, true);
    case Action::Down: return step(moved(ui, rows, s, 1), InboxCmd::None, 0, true);
    case Action::Primary:
      if (at < 0) return step(ui, rows.n > 0 ? InboxCmd::Gone : InboxCmd::None, 0, true);  // nothing selected
      return step(ui, InboxCmd::Reply, rows.index[at], true);
    case Action::Dismiss:
      if (at < 0) return step(ui, rows.n > 0 ? InboxCmd::Gone : InboxCmd::None, 0, true);
      if (s.link.mode != LinkMode::Ready) return step(ui, InboxCmd::Offline, 0, true);
      return step(ui, InboxCmd::Dismiss, rows.index[at], true);
    case Action::TabPrev:
    case Action::TabNext: return step(ui, InboxCmd::None, 0, true);
    default: return step(ui, InboxCmd::None, 0, false);
  }
}

FixedStr<64> inbox_detail(const SessionRow& r) {
  if (r.kind == SessionKind::Bg && r.short_id.has) {
    return FixedStr<64>::from("claude attach ").append(r.short_id.value.c_str(), r.short_id.value.size());
  }
  if (r.kind == SessionKind::Bg) return FixedStr<64>::from(tr(kBgSession));
  return FixedStr<64>::from(tr(kTtySession));
}

}  // namespace cpa
