// Key bindings of 04 §8 as data. The shell resolves a KeyEvent with keymap_lookup()
// for the active layer; test_keymap_static checks every binding against the ghost
// rules (single keys and two-key combos only, 04 §8.1).
#pragma once

#include <cstddef>
#include <cstdint>

#include "chord_detector.h"

namespace cpa {

// RecMemo: the memo HUD; RecPage: the meeting page (docs/11 §4.1). v0.4.0 (docs/13
// §5.3-§5.4): System holds Tab and Opt+L, checked before the overlays; Lock holds the keys
// the lock gate lets through. docs/20 D3: RecConfirm, the meeting's start and end boxes,
// is the Confirm layer plus Opt+L (leave the box and lock). gh #51: RecLatch is a hands-free
// memo (the double tap); listed last, as it came last. v0.5.0: Egg, the hidden keys (docs/04):
// the shell reads it on home and the lit lock card; no menu or hint bar lists it.
enum class Layer : uint8_t { Global, List, Compose, Confirm, RecMemo, RecPage, System, Lock, RecConfirm, RecLatch, Egg };

enum class Action : uint8_t {
  None,
  // global (04 §8.2)
  QuickNote,    // G0 tap: Compose, kind note
  RecMemo,      // G0 held 500 ms: voice memo (v0.3, docs/11 §4.1)
  RecMeeting,   // Opt+R: the meeting consent confirm
  Home,         // Opt+H
  Inbox,        // Opt+I
  OpenProject,  // Opt+1..9 (arg = slot)
  NewNote,      // Opt+N
  NewQueue,     // Opt+Q
  NewSpawn,     // Opt+S
  System,       // Opt+M
  Escape,       // Fn+`
  // lists (04 §8.3)
  Up,
  Down,
  TabPrev,
  TabNext,
  Primary,  // Enter
  Dismiss,  // Opt+D
  Back,     // ` or Backspace
  // compose and confirm (04 §8.4)
  Submit,  // Enter or G0
  Cancel,  // Fn+`; in the confirm box also ` (issue #15)
  Target,  // Opt+0..9 (arg = slot, 0 = unassigned)
  TypeNote,
  TypeQueue,
  TypeSpawn,
  DeleteChar,  // Backspace (repeats); on empty text a fresh press exits Compose
  Confirm,     // Enter in the confirm box
  // recording overlays (docs/11 §4.1)
  RecTag,   // 1..9 project tag, 0 none (arg)
  RecStar,  // meeting page: G0 tap or long, Space
  RecEnd,   // meeting page: Opt+R or `: the end confirm
  // v0.4.0 (docs/13 §5.2, §7.1)
  Menu,      // Tab: the persistent menu
  Lock,      // Opt+L: lock; while locked, Opt+L lights the card and opens the unlock window
  Unlock,    // Enter within 3 s of Opt+L while locked
  Projects,  // Opt+P: the project board (Opt+H becomes the home launcher)
  // docs/20 D5
  Backtick,  // Fn+` in Compose: types ` (` alone leaves the editor)
  // gh #51 (docs/20 U26)
  RecHandsFree,  // G0 twice (the shell's double tap; the menu row replays G0Double)
  RecStop,       // hands-free memo: G0 ends it (a press does, on its down edge: Shell::on_g0); help only
  // v0.5.0 (docs/04, hidden keys)
  PatMando,  // Opt+W on home and the lit lock card: Mando hops (mando_eggs.h)
};

struct Binding {
  Layer layer;
  Action action;
  uint8_t arg;
  KeyEvent trigger;
};

struct Resolved {
  Action action = Action::None;
  uint8_t arg = 0;
};

extern const Binding kKeymap[];
extern const size_t kKeymapCount;

Resolved keymap_lookup(Layer layer, const KeyEvent& ev);
KeySet binding_keys(const Binding& b);  // the physical keys that produce the trigger

}  // namespace cpa
