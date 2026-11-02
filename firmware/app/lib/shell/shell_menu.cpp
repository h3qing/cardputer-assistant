// The shell's v0.4.0 overlays (docs/13 §5.3-§5.4, §6, §7.1): the lock gate, Tab and
// Opt+L, and the Tab menu. The menu is not a module: MenuState lives here, its content
// and drawing come from lib/help, and Enter on a row only replays the row's key.
#include <cstdio>
#include <cstring>

#include "compose.h"
#include "menu_model.h"
#include "shell.h"
#include "words.h"

namespace cpa {
namespace {

// The menu's words (docs/21 §6.7); its titles and rows, each the menu's 120 px.
constexpr MenuTxt kHandsFreeTitle{"免提录音", "Hands-free memo"};
constexpr MenuTxt kStartTitle{"会议确认", "Start meeting?"};
constexpr MenuTxt kEndTitle{"结束确认", "End meeting?"};
constexpr MenuTxt kMeetingTitle{"会议录音", "Meeting"};
constexpr MenuTxt kDiscardTitle{"放弃草稿？", "Discard draft?"};
constexpr MenuTxt kConfirmTitle{"确认", "Confirm"};
constexpr MenuTxt kBackToEdit{"返回编辑", "Back to editing"};
constexpr MenuTxt kDiscardDraft{"放弃草稿", "Discard draft"};
constexpr MenuTxt kStopRec{"结束录音", "Stop recording"};
constexpr MenuTxt kKeepRec{"继续录", "Keep recording"};
constexpr MenuTxt kKeepRecLock{"继续录并锁屏", "Keep recording, lock"};
constexpr MenuTxt kStart{"开始", "Start"};
constexpr MenuTxt kCancel{"取消", "Cancel"};
constexpr MenuTxt kCancelLock{"取消并锁屏", "Cancel and lock"};
constexpr MenuTxt kSaveQueued{"存为排队", "Save as queued"};
constexpr MenuTxt kBackToFix{"返回修改", "Back to edit"};
constexpr MenuTxt kClose{"关闭", "Close"};
constexpr ToastTxt kTapG0Stop{"按一下 G0 结束并保存", "Tap G0 to stop and save"};
constexpr ToastTxt kJustHoldG0{"这一行是说明：直接按住 G0", "Only a note: hold G0"};
constexpr Txt kJustPress{"这一行是说明：直接按 %s", "Only a note: press %s"};
constexpr Txt kCloseThenPress{"先按 Tab 关菜单，再按 %s", "Tab closes the menu, then %s"};

// The menu's title over a page (docs/13 §6.3); "" = the Here page.
const char* page_title(const char* id) {
  static const struct {
    const char* id;
    const Txt* title;
  } kTitles[] = {{"home", &txt::kHome},
                 {"board", &txt::kProjects},
                 {"inbox", &txt::kWaiting},
                 {"system", &txt::kSettings}};
  for (const auto& t : kTitles) {
    if (std::strcmp(t.id, id) == 0) return tr(*t.title);
  }
  return "";
}

const char* rec_title(const RecInfo& r) {
  if (r.mode == RecMode::Memo) return tr(kHandsFreeTitle);  // gh #51: the only memo the menu opens over
  if (r.phase == RecPhase::Confirm) return tr(kStartTitle);
  if (r.phase == RecPhase::EndConfirm) return tr(kEndTitle);
  return tr(kMeetingTitle);
}

// Compose's "Discard what you typed?" prompt (compose.h), where Enter discards: its own rows,
// since the editor's would say Enter saves. docs/20 U18: back to editing first, so the
// row Enter takes at once never throws the draft away.
const HelpRow kDiscardRows[] = {
    {KeyEvent{KeyEventKind::Char, '`', 0, 0, Nav::None}, "`", "", &kBackToEdit},
    {KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Back}, "Fn+`", "", &kBackToEdit},
    {KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Enter}, "Enter", "", &kDiscardDraft},
};

// The meeting's "Stop recording?" box (rec_overlay.cpp): its own rows, worded as the box is,
// since the Confirm layer's Cancel would read as dropping the recording. docs/20 D3: Opt+L
// leaves the box for the meeting and locks.
const HelpRow kEndRows[] = {
    {KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Enter}, "Enter", "", &kStopRec},
    {KeyEvent{KeyEventKind::Char, '`', 0, 0, Nav::None}, "`", "", &kKeepRec},
    {KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Back}, "Fn+`", "", &kKeepRec},
    {KeyEvent{KeyEventKind::Chord, 0, kModOpt, 'l', Nav::None}, "Opt+L", "", &kKeepRecLock},
};

// The meeting's "Start a meeting recording?" box: its own rows, worded as its card
// (kMeetingConfirmKeys) and its bar, not the generic Confirm (docs/20 U22, B6).
const HelpRow kStartRows[] = {
    {KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Enter}, "Enter", "", &kStart},
    {KeyEvent{KeyEventKind::Char, '`', 0, 0, Nav::None}, "`", "", &kCancel},
    {KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Back}, "Fn+`", "", &kCancel},
    {KeyEvent{KeyEventKind::Chord, 0, kModOpt, 'l', Nav::None}, "Opt+L", "", &kCancelLock},
};

// A confirm box's rows, worded as the box is (confirm_keys): what Enter does there, and `
// and Fn+` Back to edit with a draft behind the box (D6), else Cancel (docs/20 U22, B6).
struct BoxRows {
  HelpRow rows[3];
};

constexpr BoxRows box_rows(const Txt* enter, const Txt* back) {
  return BoxRows{{{KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Enter}, "Enter", "", enter},
                  {KeyEvent{KeyEventKind::Char, '`', 0, 0, Nav::None}, "`", "", back},
                  {KeyEvent{KeyEventKind::Nav, 0, 0, 0, Nav::Back}, "Fn+`", "", back}}};
}

constexpr BoxRows kConfirmRows[] = {
    box_rows(&kStart, &kCancel),      box_rows(&kStart, &kBackToFix),       // Route::Execute
    box_rows(&kSaveQueued, &kCancel), box_rows(&kSaveQueued, &kBackToFix),  // Route::Queue
    box_rows(&kClose, &kCancel),                                            // Route::Unavailable
};

const BoxRows& confirm_rows(const Decision& d, bool draft) {
  if (d.route == Route::Unavailable) return kConfirmRows[4];  // a queue item: no draft
  return kConfirmRows[(d.route == Route::Queue ? 2 : 0) + (draft ? 1 : 0)];
}

template <size_t N>
void use_rows(MenuSource& src, const HelpRow (&rows)[N]) {
  src.help = rows;
  src.help_n = N;
}

}  // namespace

bool Shell::lock_gate(const KeyEvent& ev, uint32_t now) {
  if (!lock_.locked) return true;
  now_ = now;
  if (locked_g0(ev)) return false;  // gh #51: a taken press; a tap on the lit card waits (shell_g0.cpp)
  if (lock_pat(ev, now)) return false;  // v0.5.0: Opt+W on the lit clock card (shell_eggs.cpp)
  const LockStep st = lock_key(lock_, ev, now, meeting());
  lock_ = st.state;
  dirty_ = true;
  mirror();  // an unlock, or a swallowed hold (docs/20 U3, B1)
  return st.out == LockOut::Pass;
}

// The layer whose Tab / Opt+L count (docs/13 §5.3): the overlay's own, else System.
// Compose's discard prompt is a confirm box: Tab, and no Opt+L; the meeting's boxes have
// Opt+L (docs/20 D3).
Layer Shell::under_layer() const {
  if (rec_up()) {
    const RecInfo& r = state().rec;
    if (r.phase == RecPhase::Confirm || r.phase == RecPhase::EndConfirm) return Layer::RecConfirm;
    return rec_layer();
  }
  if (overlay_ == proto::Overlay::Compose) return compose_.discard ? Layer::Confirm : Layer::Compose;
  if (overlay_ == proto::Overlay::Confirm) return Layer::Confirm;
  return Layer::System;
}

bool Shell::locks(const KeyEvent& ev) const {
  return !lock_.locked && !menu_open_ && keymap_lookup(under_layer(), ev).action == Action::Lock;
}

bool Shell::system_key(const KeyEvent& ev) {
  switch (keymap_lookup(under_layer(), ev).action) {
    case Action::Menu:
      menu_open_ = true;
      menu_ = MenuState{};
      slide(MenuSlide::Sheet, MenuPage::Here);  // docs/19 §6.5: it slides in
      return true;
    case Action::Lock:
      leave_rec_box();
      lock_now();
      return true;
    default: return false;
  }
}

// docs/20 D3: Opt+L on the meeting's start box cancels it, on the end box the meeting goes on.
void Shell::leave_rec_box() {
  const RecPhase p = state().rec.phase;
  if (p == RecPhase::Confirm || p == RecPhase::EndConfirm) port_.rec_input(rec::RecInput::Cancel, 0);
}

// docs/20 D7 (U11): the card says "Locked" for kLockFlashMs, so a lock is never mistaken for
// a dead screen.
void Shell::lock_now() {
  menu_open_ = false;
  lock_ = lock_engage(now_, kLockFlashMs);  // the page, a draft and a meeting stay as they are
  dirty_ = true;
}

// docs/20 D2: the screen went dark on battery. Nothing lights: it was idle a minute.
void Shell::auto_lock() {
  if (lock_.locked) return;
  menu_open_ = false;
  lock_ = lock_engage();
  dirty_ = true;
  mirror();
}

void Shell::lock_flash(uint32_t now, uint32_t ms) {
  const LockState l = cpa::lock_flash(lock_, now, ms);
  dirty_ = dirty_ || l.card != lock_.card || l.card_until_ms != lock_.card_until_ms;
  lock_ = l;
}

// The G0 key as the matrix has it (docs/20 U3c, D8): the lock card stays lit under a G0
// that went down on it; "● Wait" (memo_ready) while a hold would record a memo, from the
// press until G0Long or the release. Whether it would counts at the press.
void Shell::g0_level(bool down, bool pressing, uint32_t now) {
  if (down == g0_down_ && pressing == pressing_) return;
  now_ = now;
  if (down != g0_down_) lock_ = lock_g0(lock_, down, now);
  g0_down_ = down;
  const bool start = pressing && !pressing_;
  pressing_ = pressing;
  memo_ready_ = pressing && (start ? memo_would_start(now) : memo_ready_);
  dirty_ = true;
  mirror();
}

// A hold now would record a memo: nothing refuses it (rec_refusal: the card, the battery, the
// ids...), nothing records or asks (a meeting stars, its boxes come first), and locked only
// on an armed card.
bool Shell::memo_would_start(uint32_t now) const {
  if (rec_up() || port_.rec_refusal() != rec::RecRefusal::None) return false;
  return !lock_.locked || lock_arms(lock_, now);
}

MenuSource Shell::menu_source() const {
  MenuSource src;
  const Layer under = under_layer();
  src.overlay = under != Layer::System;
  src.layer = src.overlay ? under : Layer::List;
  if (rec_up()) {
    src.title = rec_title(state().rec);
    if (state().rec.phase == RecPhase::EndConfirm) use_rows(src, kEndRows);
    if (state().rec.phase == RecPhase::Confirm) use_rows(src, kStartRows);
  } else if (overlay_ == proto::Overlay::Compose && compose_.discard) {
    src.title = tr(kDiscardTitle);
    use_rows(src, kDiscardRows);
  } else if (overlay_ == proto::Overlay::Compose) {
    src.title = compose_kind_label(compose_.spec.kind);
  } else if (overlay_ == proto::Overlay::Confirm) {
    src.title = tr(kConfirmTitle);
    use_rows(src, confirm_rows(confirm_decide(confirm_.spec, state()), confirm_.draft).rows);
  } else if (const Module* m = top()) {
    src.title = page_title(m->id());
    src.help_n = m->help(&src.help);
  }
  return src;
}

namespace {

using MenuHint = FixedStr<63>;

// docs/20 U17: Enter on a row that replays nothing. A row that explains a key (a hold, a
// range) says how to press it: G0 and, with D9, a chord work with the menu open (a
// hands-free memo's G0 too, gh #51); plain digits are the menu's own keys, so the menu goes
// first. A fact says nothing.
MenuHint explain(const MenuRow& r) {
  if (r.ev.kind != KeyEventKind::None || r.fact) return MenuHint{};
  const Action a = r.binding >= 0 ? kKeymap[r.binding].action : Action::None;
  if (a == Action::RecStop) return MenuHint::from(tr(kTapG0Stop));  // gh #51
  if (a == Action::RecMemo) return MenuHint::from(tr(kJustHoldG0));
  const bool chord = r.binding >= 0 && kKeymap[r.binding].trigger.kind == KeyEventKind::Chord;
  char buf[MenuHint::capacity() + 1];
  std::snprintf(buf, sizeof buf, tr(chord ? kJustPress : kCloseThenPress), r.keys.c_str());
  return MenuHint::from(buf);
}

// docs/20 D9: the row presses ev, itself or as one key of its range.
bool row_presses(const MenuRow& r, const KeyEvent& ev) {
  if (r.ev == ev) return true;
  if (r.binding < 0 || r.span < 2) return false;
  const size_t first = static_cast<size_t>(r.binding);
  for (size_t k = first; k < first + r.span && k < kKeymapCount; ++k) {
    if (kKeymap[k].trigger == ev) return true;
  }
  return false;
}

// Kept apart, as menu_step is: one 1.6 KB MenuContent at a time.
__attribute__((noinline)) bool page_shows(MenuPage page, const MenuSource& src, const AppState& s,
                                          const SysInfo& sys, const KeyEvent& ev) {
  const MenuContent m = menu_content(page, src, s, sys);
  for (uint8_t i = 0; i < m.n; ++i) {
    if (row_presses(m.rows[i], ev)) return true;
  }
  return false;
}

// A chord on any page of the menu (over an overlay there is no Go page).
bool menu_shows(const MenuSource& src, const AppState& s, const SysInfo& sys, const KeyEvent& ev) {
  if (ev.kind != KeyEventKind::Chord) return false;
  for (MenuPage p : {MenuPage::Here, MenuPage::Goto, MenuPage::Control}) {
    if (p == MenuPage::Goto && src.overlay) continue;
    if (page_shows(p, src, s, sys, ev)) return true;
  }
  return false;
}

struct MenuTurn {
  MenuStep step;
  MenuHint hint;  // "": nothing to say
};

// Kept apart so the 1.6 KB MenuContent is in a frame of its own, never next to the
// caller's (scripts/stack_budget.py).
__attribute__((noinline)) MenuTurn menu_step(const MenuState& st, const KeyEvent& ev, const MenuSource& src,
                                             const AppState& s, const SysInfo& sys) {
  const MenuContent m = menu_content(st.page, src, s, sys);
  MenuTurn t{menu_key(st, ev, m), MenuHint{}};
  const bool enter = ev.kind == KeyEventKind::Nav && ev.nav == Nav::Enter;
  if (enter && t.step.out == MenuOut::None && st.sel < m.n) t.hint = explain(m.rows[st.sel]);
  return t;
}

__attribute__((noinline)) void menu_draw(Canvas& c, const MenuState& st, const MenuSource& src, const AppState& s,
                                         const SysInfo& sys, const MenuMotion& mo) {
  const MenuContent m = menu_content(st.page, src, s, sys);
  menu_render(c, st, m, mo);
}

__attribute__((noinline)) Rect menu_draw_slide(Canvas& c, const MenuState& st, const MenuSource& src,
                                               const AppState& s, const SysInfo& sys, const MenuMotion& mo) {
  const MenuContent m = menu_content(st.page, src, s, sys);
  return menu_render_slide(c, st, m, mo);
}

}  // namespace

// Replay and Close both close the menu; Replay then presses the row's key (step 9). A
// Backspace held to close it goes on repeating: on_key drops the rest of that hold. A key
// ends a slide where it was going; a new page slides the knob to it (docs/19 §6.5).
// docs/20 D9: a chord the menu shows closes it and does what it does.
void Shell::menu_key_event(const KeyEvent& ev) {
  if (menu_shows(menu_source(), state(), sys(), ev)) {
    menu_open_ = false;
    menu_mo_ = MenuMotion{};
    press(ev);
    return;
  }
  const MenuTurn t = menu_step(menu_, ev, menu_source(), state(), sys());
  const MenuStep& st = t.step;
  const MenuPage was = menu_.page;
  menu_ = st.state;
  if (st.state.page != was) {
    slide(MenuSlide::Knob, was);
  } else {
    menu_mo_ = MenuMotion{};
  }
  if (!t.hint.empty()) hint(ToastLevel::Info, t.hint.c_str());  // docs/20 U17
  if (st.out == MenuOut::None) return;
  menu_open_ = false;
  eat_repeats_ = true;
  if (st.out == MenuOut::Replay) press(st.ev);
}

void Shell::render_menu(Canvas& c) const { menu_draw(c, menu_, menu_source(), state(), sys(), menu_mo_); }

// A later frame of the sheet's or the knob's slide (shell_anim.cpp).
Rect Shell::render_menu_slide(Canvas& c) const {
  return menu_draw_slide(c, menu_, menu_source(), state(), sys(), menu_mo_);
}

}  // namespace cpa
