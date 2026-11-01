// The shell's answers to keys it refuses (docs/20 U13, U17): a hint (toast.h) instead of
// nothing at all. Queue and spawn targets that do not exist name the projects there
// are; a digit for an empty slot says so instead of opening an empty page; the global
// keys under the editor say how to leave it.
#include "compose.h"
#include "lang_switch.h"
#include "reducers.h"
#include "shell.h"
#include "txt.h"

namespace cpa {

constexpr ToastTxt kEditorEmpty{"编辑器开着：退格 退出", "Editor open: Del exits"};
constexpr ToastTxt kEditorText{"编辑器开着：Enter 保存，` 退出", "Editor open: Enter saves, ` exits"};
constexpr ToastTxt kSaveAsNote{"可按 Opt+N 改存成笔记", "Opt+N saves it as a Note"};

void Shell::notice(ToastLevel lv, const char* utf8) {
  toasts_ = toast_push(toasts_, lv, utf8, now_);
  dirty_ = true;
}

void Shell::hint(ToastLevel lv, const char* utf8) {
  toasts_ = toast_hint(toasts_, lv, utf8, now_);
  dirty_ = true;
}

// docs/21 §6.8: nothing queued is cleared; the toasts already formatted show their few
// seconds in the words they were queued with.
bool Shell::setLanguage(Lang l) {
  const Lang from = lang();
  if (l == from) return false;
  set_lang(l);
  const SettingsSave saved = port_.store_lang(l);
  toast(ToastLevel::Info, lang_switched_text(from, l).c_str());
  // a failed write leaves NVS on the language switched from (two languages: it differed)
  if (saved == SettingsSave::Failed) toast(ToastLevel::Warn, lang_unsaved_text(from).c_str());
  dirty_ = true;
  return true;
}

void Shell::retract_hints() {
  toasts_ = toast_retract(toasts_, now_);
  dirty_ = true;
}

// docs/20 U17: a place key (Opt+H/I/P/M) or Opt+R does nothing while the editor is
// open; say how to leave it instead of nothing at all.
void Shell::refused_in_compose(const KeyEvent& ev) {
  if (!refused_global(ev) || compose_.discard) return;
  hint(ToastLevel::Info, tr(compose_.text.empty() ? kEditorEmpty : kEditorText));
}

bool Shell::refused_global(const KeyEvent& ev) {
  switch (keymap_lookup(Layer::Global, ev).action) {
    case Action::Home:
    case Action::Projects:
    case Action::Inbox:
    case Action::System:
    case Action::OpenProject:
    case Action::NewNote:
    case Action::NewQueue:
    case Action::NewSpawn:
    case Action::RecMeeting: return true;
    default: return false;  // G0 (docs/20 D4), typing, Fn+`
  }
}

void Shell::need_project() {
  hint(ToastLevel::Warn, need_project_text(state().projects).c_str());
  if (state().projects.data.n == 0) hint(ToastLevel::Info, tr(kSaveAsNote));
}

bool Shell::open_project(uint8_t slot) {
  if (!project_by_slot(state().projects, slot).has) {
    hint(ToastLevel::Warn, no_slot_text(state().projects, slot).c_str());
    return true;
  }
  go("board", slot, false);
  return true;
}

}  // namespace cpa
