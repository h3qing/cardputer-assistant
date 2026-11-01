// Compose (05 §5.6, 04 US-01/US-02): the shared editor, as a pure value state.
//   - ASCII only (0x20..0x7E), one line, 1..500 bytes; input stops at 500;
//   - spawn / feedback text may not start with '-' after leading spaces;
//   - Opt+0..9 changes the target, Opt+N/Q/S the kind; queue and spawn need a project;
//   - ` cancels (docs/20 D5, as on every page); with text it first asks (Enter discards,
//     ` or Fn+` goes back); Fn+` types the backtick;
//   - Backspace on empty text exits too (issue #15), on a fresh press only;
//   - every key is eaten while Compose is open (Opt+H/I/M do nothing here).
#pragma once

#include <cstdint>

#include "module.h"

namespace cpa {

enum class ComposeResult : uint8_t {
  None,          // key ignored
  Edited,        // text, kind or target changed
  Full,          // a character did not fit (500 bytes)
  Submit,        // valid: the shell saves it or opens Confirm
  Cancelled,     // closed without saving
  DiscardAsked,  // "discard?" prompt shown
  EmptyText,     // Enter on empty or blank text
  NeedProject,   // queue / spawn without a project
  DashText,      // spawn / feedback starting with '-'
};

struct ComposeState {
  ComposeSpec spec;
  UserText text;
  bool discard = false;  // the discard prompt is open
};

struct ComposeStep {
  ComposeState state;
  ComposeResult result = ComposeResult::None;
};

ComposeState compose_open(const ComposeSpec& spec);
ComposeStep compose_key(const ComposeState& s, const KeyEvent& ev);

// Live check shown while typing (04 US-02 AC4).
bool compose_dash_blocked(const ComposeState& s);
const char* compose_kind_label(ComposeKind k);  // 记一条 / 排队给 AI / 开 session / 回复 (docs/20 U22)
const char* compose_result_text(ComposeResult r);  // toast text for a refused submit, or ""
// docs/20 U13: a queue item or a new session needs a project that exists. The hint names
// the key and the ones there are ("Opt+数字：1 cpa · 2 blog", "… …（共 9 个）" when they
// do not fit one row), or how to add one.
FixedStr<96> need_project_text(const ProjectsView& p);
FixedStr<96> no_slot_text(const ProjectsView& p, uint8_t slot);  // "没有第 5 个项目（共 2 个）"

}  // namespace cpa
