#include "board_logic.h"

#include <cstdio>

#include "keymap.h"
#include "txt.h"

namespace cpa {
namespace {

BoardStep handled(const BoardUi& ui, BoardCmd cmd = BoardCmd::None, uint8_t arg = 0) {
  return BoardStep{ui, cmd, arg, true};
}

bool ready(const AppState& s) { return s.link.mode == LinkMode::Ready; }

bool agent_ok(const AppState& s) { return s.link.agent == AgentStatus::Ok && s.link.cap_spawn; }

uint8_t queue_count(const AppState& s, uint8_t slot) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < s.queue.data.n; ++i) n = static_cast<uint8_t>(n + (s.queue.data.rows[i].slot == slot));
  return n;
}

BoardRowId row_id(const BoardUi& ui, const AppState& s, uint8_t i) {
  if (ui.slot == 0) {
    if (i >= s.projects.data.n) return BoardRowId::from("*");
    const char digit[2] = {static_cast<char>('0' + s.projects.data.rows[i].slot), '\0'};
    return BoardRowId::from(digit);
  }
  if (ui.tab == BoardTab::Sessions) {
    const SessionRow* r = board_session(s, ui.slot, i);
    return r ? BoardRowId::from(r->sid.c_str()) : BoardRowId{};
  }
  const QueueRow* q = board_queue_item(s, ui.slot, i);
  return q ? BoardRowId::from(q->qid.c_str()) : BoardRowId{};
}

BoardUi picked(const BoardUi& ui, const AppState& s, uint8_t at) {
  BoardUi out = ui;
  out.cursor = at;
  out.picked = at < board_row_count(ui, s);
  if (out.picked) out.sel = row_id(ui, s, at);
  return out;
}

// ; and . from the selected row; once it is gone, . takes the row that took its place.
BoardUi moved(const BoardUi& ui, int delta, const AppState& s) {
  const uint8_t rows = board_row_count(ui, s);
  const int at = board_selected(ui, s);
  const int last = rows > 0 ? rows - 1 : 0;
  const int anchor = ui.cursor > last ? last : ui.cursor;
  const int c = at >= 0 ? at + delta : anchor + (delta < 0 ? delta : 0);
  return picked(ui, s, static_cast<uint8_t>(c < 0 ? 0 : (c > last ? last : c)));
}

BoardUi with_tab(const BoardUi& ui, BoardTab tab, const AppState& s) {
  BoardUi out = ui;
  out.tab = tab;
  out.cursor = 0;
  out.picked = false;
  return board_pick(out, s);
}

BoardStep home_primary(const BoardUi& ui, uint8_t at, const AppState& s) {
  if (at >= s.projects.data.n) return handled(ui, BoardCmd::OtherRow);  // no page: say so (U13)
  return handled(ui, BoardCmd::OpenProject, s.projects.data.rows[at].slot);
}

BoardStep project_primary(const BoardUi& ui, uint8_t at, const AppState& s) {
  if (ui.tab == BoardTab::Sessions) return handled(ui, BoardCmd::Reply, at);
  if (!ready(s)) return handled(ui, BoardCmd::Offline);
  if (!agent_ok(s)) return handled(ui, BoardCmd::NoAgent);
  return handled(ui, BoardCmd::SpawnItem, at);
}

BoardStep project_dismiss(const BoardUi& ui, uint8_t at, const AppState& s) {
  if (!ready(s)) return handled(ui, BoardCmd::Offline);
  return handled(ui, ui.tab == BoardTab::Sessions ? BoardCmd::Dismiss : BoardCmd::DoneItem, at);
}

}  // namespace

// Enter and Opt+D act on the selected row (arg: its index in this state), or on nothing.
BoardStep board_next(const BoardUi& ui, const KeyEvent& ev, const AppState& s) {
  const Resolved r = keymap_lookup(Layer::List, ev);
  const int at = board_selected(ui, s);
  const bool home = ui.slot == 0;
  switch (r.action) {
    case Action::Up: return handled(moved(ui, -1, s));
    case Action::Down: return handled(moved(ui, 1, s));
    case Action::TabPrev:
    case Action::TabNext:
      if (home) return handled(ui);
      return handled(with_tab(ui, ui.tab == BoardTab::Sessions ? BoardTab::Queue : BoardTab::Sessions, s));
    case Action::Primary:
      if (at < 0) return handled(ui, board_row_count(ui, s) > 0 ? BoardCmd::Gone : BoardCmd::None);
      return home ? home_primary(ui, static_cast<uint8_t>(at), s) : project_primary(ui, static_cast<uint8_t>(at), s);
    case Action::Dismiss:
      if (home) return handled(ui);
      if (at < 0) return handled(ui, board_row_count(ui, s) > 0 ? BoardCmd::Gone : BoardCmd::None);
      return project_dismiss(ui, static_cast<uint8_t>(at), s);
    default: return BoardStep{ui, BoardCmd::None, 0, false};
  }
}

int board_selected(const BoardUi& ui, const AppState& s) {
  const uint8_t rows = board_row_count(ui, s);
  if (rows == 0) return -1;
  if (!ui.picked) return ui.cursor < rows ? ui.cursor : rows - 1;
  for (uint8_t i = 0; i < rows; ++i) {
    if (row_id(ui, s, i) == ui.sel) return i;
  }
  return -1;
}

BoardUi board_pick(const BoardUi& ui, const AppState& s) {
  const int at = board_selected(ui, s);
  return at < 0 ? ui : picked(ui, s, static_cast<uint8_t>(at));
}

uint8_t board_row_count(const BoardUi& ui, const AppState& s) {
  if (ui.slot == 0) return static_cast<uint8_t>(s.projects.data.n + 1);  // + "Other"
  return ui.tab == BoardTab::Sessions ? board_session_count(s, ui.slot) : queue_count(s, ui.slot);
}

uint8_t board_session_count(const AppState& s, uint8_t slot) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < s.sessions.data.n; ++i) n = static_cast<uint8_t>(n + (s.sessions.data.rows[i].slot == slot));
  return n;
}

const SessionRow* board_session(const AppState& s, uint8_t slot, uint8_t i) {
  for (uint8_t k = 0; k < s.sessions.data.n; ++k) {
    if (s.sessions.data.rows[k].slot != slot) continue;
    if (i == 0) return &s.sessions.data.rows[k];
    --i;
  }
  return nullptr;
}

const QueueRow* board_queue_item(const AppState& s, uint8_t slot, uint8_t i) {
  for (uint8_t k = 0; k < s.queue.data.n; ++k) {
    if (s.queue.data.rows[k].slot != slot) continue;
    if (i == 0) return &s.queue.data.rows[k];
    --i;
  }
  return nullptr;
}

// docs/21: the overview's sentence and a session's state word.
constexpr Txt kRunNeed{"%u 个在跑，%u 个在等你", "%u running, %u waiting"};
constexpr Txt kRun{"%u 个在跑", "%u running"};
constexpr Txt kBusy{"运行", "busy"};
constexpr Txt kIdle{"空闲", "idle"};
constexpr Txt kWait{"等待", "wait"};

FixedStr<48> board_sentence(const ProjectsView& v) {
  if (!v.live) return FixedStr<48>{};
  unsigned run = v.data.other.run;
  unsigned need = 0;
  for (uint8_t i = 0; i < v.data.n; ++i) {
    run += v.data.rows[i].run;
    need += v.data.rows[i].need;
  }
  char buf[64];
  if (need > 0) {
    std::snprintf(buf, sizeof buf, tr(kRunNeed), run, need);
  } else {
    std::snprintf(buf, sizeof buf, tr(kRun), run);
  }
  return FixedStr<48>::from(buf);
}

const char* session_state_text(SessionState st) {
  switch (st) {
    case SessionState::Busy: return tr(kBusy);
    case SessionState::Shell: return "shell";
    case SessionState::Idle: return tr(kIdle);
    case SessionState::Waiting: return tr(kWait);
    default: return "?";
  }
}

}  // namespace cpa
