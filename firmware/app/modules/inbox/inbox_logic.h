// Inbox module logic (04 US-07): sessions that need you, waiting first, then idle; in
// each group the longest wait first. Enter replies, Opt+D dismisses (online only).
#pragma once

#include <cstdint>

#include "module.h"

namespace cpa {

// docs/20 U12: the cursor is a session, not a row. sel is the selected session's sid
// (picked: one was chosen), looked up again on every key and every frame, so a new
// snapshot that reorders the rows never moves it; cursor is the row it was last seen on
// (where ; and . go on from once it is gone).
struct InboxUi {
  uint8_t cursor = 0;
  Sid sel;
  bool picked = false;
};

struct InboxRows {
  uint8_t n = 0;
  uint8_t index[kMaxSessions] = {};  // rows of SessionsView, in inbox order
};

InboxRows inbox_rows(const AppState& s);

// Gone: Enter or Opt+D while the selected session has left the list (docs/20 U12).
enum class InboxCmd : uint8_t { None, Reply, Dismiss, Offline, Gone };

struct InboxStep {
  InboxUi ui;
  InboxCmd cmd = InboxCmd::None;
  uint8_t row = 0;  // SessionsView row
  bool handled = false;
};

InboxStep inbox_next(const InboxUi& ui, const KeyEvent& ev, const AppState& s);

// The row (in inbox order) of the selected session; -1: it left the list, or the list is
// empty. Nothing picked yet: the cursor's row.
int inbox_selected(const InboxUi& ui, const InboxRows& rows, const AppState& s);
// The cursor's row, picked (entering the page holds its first row from the start).
InboxUi inbox_pick(const InboxUi& ui, const AppState& s);

// The detail line for a session (04 US-07 AC4): "claude attach <short>" for background
// sessions only; interactive ones say "终端 session：在 Mac 上处理". docs/20 U14: the name has
// a line of its own above it (session_label, whole).
FixedStr<64> inbox_detail(const SessionRow& r);

}  // namespace cpa
