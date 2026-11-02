// Board module logic (04 US-06/US-08, 05 §5.4): pure value UI state and transitions.
//   home (slot 0): one row per project plus the "Other" row; Enter opens the project
//     (on Other: those projects are only on the Mac).
//   project page (slot 1..9): tabs Sessions / Queue, switched with , and /.
//     Sessions: Enter -> reply (Compose feedback); Opt+D -> dismiss (online only).
//     Queue:    Enter -> open a session from the item (Confirm, online with Claude);
//               Opt+D -> mark done (online only).
// Back keys are not handled here: the shell pops the navigation stack.
#pragma once

#include <cstdint>

#include "module.h"

namespace cpa {

enum class BoardTab : uint8_t { Sessions, Queue };

// docs/20 U12: the cursor is an object, not a row. sel is the selected row's id (a
// project's slot on the home page, "*" for Other; a session's sid; a queue item's qid),
// looked up again on every key and every frame, so a new snapshot never moves it to
// another object; once it is gone nothing is selected until the cursor moves. cursor is
// the row it was last seen on.
using BoardRowId = FixedStr<11>;

struct BoardUi {
  uint8_t slot = 0;  // 0 = home page
  uint8_t cursor = 0;
  BoardTab tab = BoardTab::Sessions;
  BoardRowId sel;
  bool picked = false;  // sel holds a row's id (else: the cursor's row)
};

enum class BoardCmd : uint8_t {
  None,
  OpenProject,  // arg = slot
  Reply,        // session at index
  SpawnItem,    // queue item at index
  DoneItem,     // queue item at index
  Dismiss,      // session at index
  Offline,      // the action needs the link: refused
  NoAgent,      // the action needs Claude Code on the Mac: refused
  OtherRow,     // Enter on "Other": those projects are only on the Mac (docs/20 U13)
  Gone,         // Enter or Opt+D while the selected row has left the list (docs/20 U12)
};

struct BoardStep {
  BoardUi ui;
  BoardCmd cmd = BoardCmd::None;
  uint8_t arg = 0;
  bool handled = false;
};

BoardStep board_next(const BoardUi& ui, const KeyEvent& ev, const AppState& s);

uint8_t board_row_count(const BoardUi& ui, const AppState& s);
// The selected row; -1: it is gone (or the page is empty).
int board_selected(const BoardUi& ui, const AppState& s);
BoardUi board_pick(const BoardUi& ui, const AppState& s);  // hold the cursor's row (entering a page)
// The i-th session / queue item of a project, in snapshot order (nullptr past the end).
const SessionRow* board_session(const AppState& s, uint8_t slot, uint8_t i);
const QueueRow* board_queue_item(const AppState& s, uint8_t slot, uint8_t i);
uint8_t board_session_count(const AppState& s, uint8_t slot);

const char* session_state_text(SessionState st);  // busy / shell / idle / wait / ?

// The board's opening sentence (docs/19 §6.4): "3 running, 1 waiting", or "3 running" when
// nothing waits; "" until the counts are live.
FixedStr<48> board_sentence(const ProjectsView& v);

}  // namespace cpa
