#include "shell.h"

#include <cstdio>
#include <cstring>

#include "keymap.h"
#include "rec_overlay.h"
#include "reducers.h"
#include "text_rules.h"
#include "txt.h"

namespace cpa {
namespace {

// docs/21: the shell's toasts, each whole in the 224 px capsule (docs/20 B6).
constexpr ToastTxt kMeetingEnd{"开会中：Opt+R 结束", "In a meeting: Opt+R ends it"};
constexpr ToastTxt kMeetingKeys{"开会中：G0 加★，Opt+R 结束", "In a meeting: G0 adds ★, Opt+R ends"};
constexpr ToastTxt kBoxFirst{"先处理确认框", "Answer the box first"};
constexpr Txt kUnknownProject{"未知项目", "Unknown project"};
constexpr Txt kBusy{"已有请求在途", "Request pending"};
constexpr ToastTxt kStillOnMac{"未执行（队列项仍在 Mac 上）", "Not run (still queued on the Mac)"};
constexpr ToastTxt kReplyOnMac{"session 的项目不在设备上：去 Mac 回复", "Project not on device: reply on Mac"};
constexpr Txt kSaved{"已保存 · 待传 ↑%u", "Saved · to sync ↑%u"};
constexpr ToastTxt kStoreFull{"存储已满，请连 Mac 同步", "Storage full: sync with the Mac"};
constexpr ToastTxt kNoStorage{"没有可用存储：请插入 FAT32 SD 卡", "No storage: insert a FAT32 SD card"};
constexpr ToastTxt kCardLost{"SD 卡出错：插好后需重启", "SD card error: reseat it, restart"};
constexpr ToastTxt kSpared{"SD 卡出错：已暂存到内部，插好卡后重启", "SD error: kept in flash, restart"};
constexpr ToastTxt kSaveFailed{"保存失败（E_STORAGE）", "Save failed (E_STORAGE)"};
constexpr ToastTxt kNoModule{"模块不可用", "Module not available"};

bool is_g0(const KeyEvent& ev) {
  return ev.kind == KeyEventKind::G0Tap || ev.kind == KeyEventKind::G0Long || ev.kind == KeyEventKind::G0LongUp;
}

bool is_back(const KeyEvent& ev) {
  return keymap_lookup(Layer::Global, ev).action == Action::Escape ||
         keymap_lookup(Layer::List, ev).action == Action::Back;
}

ComposeSpec spec_of(ComposeKind kind, uint8_t slot) {
  ComposeSpec s;
  s.kind = kind;
  s.slot = slot;
  return s;
}

Opt<ProjectKey> key_of(const AppState& s, uint8_t slot) {
  const Opt<ProjectRow> p = project_by_slot(s.projects, slot);
  return p.has ? some(p.value.key) : Opt<ProjectKey>{};
}

}  // namespace

Shell::Shell(ShellPort& port, Module* const* modules, uint8_t n)
    : port_(port), modules_(modules), n_(n < kMaxModules ? n : kMaxModules) {}

void Shell::begin(uint32_t now) {
  now_ = now;
  for (uint8_t i = 0; i < n_; ++i) {
    const char* id = modules_[i]->id();
    const bool valid = proto::is_module_id(id, std::strlen(id)) && index_of(id) < 0;
    enabled_[i] = valid && modules_[i]->begin(*this);
  }
  const int home = index_of("home");  // docs/13 §5.5: else the board, as in v0.3
  const int board = index_of("board");
  home_ = home >= 0 ? static_cast<uint8_t>(home) : (board >= 0 ? static_cast<uint8_t>(board) : first_enabled());
  nav_ = nav_home(home_);
  enter(nav_top(nav_));
}

uint8_t Shell::first_enabled() const {
  for (uint8_t i = 0; i < n_; ++i) {
    if (enabled_[i]) return i;
  }
  return 0;
}

int Shell::index_of(const char* id) const {
  for (uint8_t i = 0; i < n_; ++i) {
    if (enabled_[i] && std::strcmp(modules_[i]->id(), id) == 0) return i;
  }
  return -1;
}

Module* Shell::top() const {
  const NavEntry e = nav_top(nav_);
  return e.mod < n_ && enabled_[e.mod] ? modules_[e.mod] : nullptr;
}

void Shell::enter(const NavEntry& e) {
  if (e.mod < n_ && enabled_[e.mod]) modules_[e.mod]->onEnter(e.arg);
  toasts_ = toast_retract(toasts_, now_);  // a hint is about the page it was given on (U20)
  dirty_ = true;
}

const char* Shell::top_id() const {
  const Module* m = top();
  return m ? m->id() : "";
}

uint8_t Shell::disabled_count() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < n_; ++i) n = static_cast<uint8_t>(n + (enabled_[i] ? 0 : 1));
  return n;
}

uint8_t Shell::compose_slot() const {
  if (overlay_ == proto::Overlay::Compose) return compose_.spec.slot;
  if (overlay_ == proto::Overlay::Confirm) return confirm_.spec.slot;  // docs/20 D4: a memo held over it
  const Module* m = top();
  return m ? m->contextSlot() : 0;
}

void Shell::on_key(const KeyEvent& ev, uint32_t now) {
  now_ = now;
  dirty_ = true;
  if (eat_g0(ev)) return;          // gh #51: the rest of a press the double tap or a memo took
  if (!is_g0(ev)) g0_other_key();  // gh #51: a waiting tap's note opens first; the key lands in it
  if (ev.repeat && eat_repeats_) return;  // only Backspace repeats (chord_detector.h)
  eat_repeats_ = false;
  dispatch(ev);
  if (pending_.kind != KeyEventKind::None) {  // docs/13 §5.4 step 9: one level only
    const KeyEvent again = pending_;
    pending_ = KeyEvent{};
    replaying_ = true;
    dispatch(again);
    replaying_ = false;
  }
  mirror();
}

// docs/13 §5.4 steps 3-8 (the header lists them). docs/20 D4: G0 held records a memo over
// the editor and a confirm box too, which stay as they are.
void Shell::dispatch(const KeyEvent& ev) {
  if (lock_.locked) return locked_key(ev);
  if (menu_open_ && ev.kind != KeyEventKind::G0LongUp) return menu_key_event(ev);  // a release goes through
  if (system_key(ev)) return;
  if (rec_up()) return rec_key_event(ev);
  if (note_hold(ev)) return;  // gh #51: G0 held in an empty quick note closes it and records (shell_g0.cpp)
  if (ev.kind == KeyEventKind::G0Long && overlay_ != proto::Overlay::None) {
    rec_gesture(RecMode::Memo);
    return;
  }
  if (overlay_ == proto::Overlay::Compose) return compose_key_event(ev);
  if (overlay_ == proto::Overlay::Confirm) return confirm_key_event(ev);
  if (pat_key(ev)) return;  // v0.5.0: Opt+W on home (shell_eggs.cpp)
  if (!global_key(ev)) module_key(ev);
}

void Shell::mirror() {
  const Module* m = top();
  port_.set_context_slot(m ? m->contextSlot() : 0);
  UiFlags ui;
  ui.menu = menu_open_;
  ui.locked = lock_.locked;
  ui.memo_ready = memo_ready_;
  ui.lock_held = lock_.locked && (lock_.eat_up || lock_.swallowed);  // the card answers the hold
  ui.pat = pat_;
  port_.set_ui(ui);
}

bool Shell::press(const KeyEvent& ev) {
  if (replaying_ || pending_.kind != KeyEventKind::None || ev.kind == KeyEventKind::None) return false;
  pending_ = ev;
  return true;
}

bool Shell::meeting() const {
  const RecInfo& r = state().rec;
  return r.mode == RecMode::Meeting && r.phase != RecPhase::Idle && r.phase != RecPhase::Confirm;
}

bool Shell::global_key(const KeyEvent& ev) {
  const Resolved g = keymap_lookup(Layer::Global, ev);
  const uint8_t slot = compose_slot();
  switch (g.action) {
    case Action::QuickNote: g0_ = g0_tap(g0_, now_, true); return true;  // gh #51: a second tap may follow
    case Action::NewNote: compose(spec_of(ComposeKind::Note, slot)); return true;
    case Action::NewQueue: compose(spec_of(ComposeKind::Queue, slot)); return true;
    case Action::NewSpawn: compose(spec_of(ComposeKind::Spawn, slot)); return true;
    case Action::RecMemo: rec_gesture(RecMode::Memo); return true;
    case Action::RecMeeting: rec_gesture(RecMode::Meeting); return true;
    case Action::RecHandsFree: hands_free(false); return true;  // the Tab menu's row (gh #51): G0 is up
    case Action::Home:
      nav_ = nav_home(home_);
      enter(nav_top(nav_));
      return true;
    case Action::Projects: go("board", 0, false); return true;
    case Action::Inbox: go("inbox", 0, false); return true;
    case Action::OpenProject: return open_project(g.arg);
    case Action::System: go("system", 0, false); return true;
    default: return false;  // Escape goes to the module first
  }
}

// Locked, the gate passes G0 alone (lock.h), and it goes to the recording path whatever
// is under the lock: a draft stays as it is (docs/13 §5.3, §7.1).
void Shell::locked_key(const KeyEvent& ev) {
  if (!is_g0(ev)) return;
  if (rec_up()) return rec_key_event(ev);  // a meeting's ★, or the release of a memo
  if (ev.kind == KeyEventKind::G0Long) rec_gesture(RecMode::Memo);
}

// docs/20 U15: a memo held past its end keeps the recording screen until G0 goes up.
bool Shell::rec_up() const { return state().rec.phase != RecPhase::Idle || state().rec.held; }

Layer Shell::rec_layer() const {
  const RecInfo& r = state().rec;
  if (r.mode == RecMode::Meeting) return Layer::RecPage;
  return r.latched ? Layer::RecLatch : Layer::RecMemo;
}

bool Shell::rec_gesture(RecMode mode) {
  const rec::RecRefusal why = port_.rec_gesture(mode, compose_slot());
  if (why != rec::RecRefusal::None) {
    toast(ToastLevel::Error, rec_refusal_text(why));
    return false;
  }
  lock_ = lock_dark(lock_);  // a memo from the lock card: dark and locked again after it
  return true;
}

// docs/11 §4.1: the boxes (rec_box_key); the memo HUD digits (its G0 is on_g0's, gh #51);
// the meeting page a star, digits and Opt+R. Nothing else does anything.
void Shell::rec_key_event(const KeyEvent& ev) {
  const RecInfo& r = state().rec;
  if (r.phase == RecPhase::Confirm || r.phase == RecPhase::EndConfirm) return rec_box_key(ev);
  if (ev.kind == KeyEventKind::G0LongUp) return;
  if (r.phase != RecPhase::Cue && r.phase != RecPhase::Recording) return;
  const Resolved k = keymap_lookup(rec_layer(), ev);
  switch (k.action) {
    case Action::RecTag: return port_.rec_input(rec::RecInput::Tag, k.arg);
    case Action::RecStar: return port_.rec_input(rec::RecInput::Star, 0);
    case Action::RecEnd:  // the page's hints go with it: the end box's own is seen at once
      retract_hints();
      return port_.rec_input(rec::RecInput::End, 0);
    default: break;
  }
  if (r.mode == RecMode::Meeting) meeting_hint(ev);
}

// What the meeting page ignores says how to end it (docs/20 D3: ` no longer ends; an Enter
// meant for an end box that went away is not silent) or, for a place key, what works (U17).
void Shell::meeting_hint(const KeyEvent& ev) {
  const bool enter = ev.kind == KeyEventKind::Nav && ev.nav == Nav::Enter;
  if (is_back(ev) || enter) return hint(ToastLevel::Info, tr(kMeetingEnd));
  if (refused_global(ev)) hint(ToastLevel::Info, tr(kMeetingKeys));
}

// The meeting's boxes (docs/20 D3): Enter answers (RecCore guards it); ` and Fn+` cancel;
// on the start box every other key cancels too (a letter typed on the way, U5); G0 says
// the box comes first (D4). Tab and Opt+L are system keys (shell_menu.cpp).
void Shell::rec_box_key(const KeyEvent& ev) {
  if (is_g0(ev)) {
    if (ev.kind != KeyEventKind::G0LongUp) hint(ToastLevel::Info, tr(kBoxFirst));
    return;
  }
  const Action a = keymap_lookup(Layer::RecConfirm, ev).action;
  if (a == Action::Confirm) return port_.rec_input(rec::RecInput::Confirm, 0);
  if (a == Action::Cancel || state().rec.phase == RecPhase::Confirm) port_.rec_input(rec::RecInput::Cancel, 0);
}

void Shell::module_key(const KeyEvent& ev) {
  Module* m = top();
  const bool handled = m && m->onKey(ev);
  if (handled || !is_back(ev)) return;
  nav_ = nav_pop(nav_, home_);
  enter(nav_top(nav_));
}

// A refusal is a hint (toast.h): never repeated, and gone once the target or the kind
// changed (docs/20 U13, U20).
void Shell::compose_key_event(const KeyEvent& ev) {
  // docs/20 U18: the discard prompt guards Enter for 400 ms, as every confirm box does
  const bool enter = ev.kind == KeyEventKind::Nav && ev.nav == Nav::Enter;
  if (compose_.discard && enter && now_ - discard_ms_ < kConfirmGuardMs) return;
  const ComposeStep st = compose_key(compose_, ev);
  if (st.result == ComposeResult::DiscardAsked) discard_ms_ = now_;
  const bool retarget = st.state.spec.slot != compose_.spec.slot || st.state.spec.kind != compose_.spec.kind;
  compose_ = st.state;
  if (retarget) retract_hints();
  switch (st.result) {
    case ComposeResult::None: return refused_in_compose(ev);
    case ComposeResult::Cancelled:  // the editor's refusals go with it
      retract_hints();
      return close_overlay();
    case ComposeResult::Submit: submit_compose(); return;
    case ComposeResult::NeedProject: need_project(); return;
    case ComposeResult::EmptyText:
    case ComposeResult::DashText:
    case ComposeResult::Full: hint(ToastLevel::Warn, compose_result_text(st.result)); return;
    default: return;
  }
}

void Shell::submit_compose() {
  const ComposeSpec& sp = compose_.spec;
  const bool needs = sp.kind == ComposeKind::Queue || sp.kind == ComposeKind::Spawn;
  if (needs && !key_of(state(), sp.slot).has) return need_project();  // an empty slot is slot 0
  if (sp.kind == ComposeKind::Spawn || sp.kind == ComposeKind::Feedback) {
    ConfirmSpec c;
    c.kind = sp.kind == ComposeKind::Spawn ? ConfirmKind::Spawn : ConfirmKind::Feedback;
    c.slot = sp.slot;
    c.sid = sp.sid;
    c.text = compose_.text;
    confirm(c);
    confirm_.draft = true;  // docs/20 D6: ` goes back to it
    return;
  }
  const Opt<ProjectKey> key = key_of(state(), sp.slot);
  SaveResult r = SaveResult::Failed;
  if (sp.kind == ComposeKind::Note) {
    NoteBody b;
    b.text = compose_.text;
    b.project = key;
    r = port_.save_note(b);
  } else if (key.has) {
    QueueBody q;
    q.text = compose_.text;
    q.project = key.value;
    r = port_.save_queue(q);
  }
  if (r == SaveResult::Saved || r == SaveResult::Spared) close_overlay();
  save_result_toast(r);
}

// docs/20 D6: cancelling a confirm opened from the editor goes back to it, the text and
// the target kept (the draft is still in compose_); one opened by a module just closes.
void Shell::confirm_key_event(const KeyEvent& ev) {
  const ConfirmResult r = confirm_key(confirm_, ev, now_);
  if (r == ConfirmResult::Accepted) accept_confirm();
  if (r != ConfirmResult::Cancelled) return;
  if (!confirm_.draft) return close_overlay();
  overlay_ = proto::Overlay::Compose;
  overlay_ms_ = now_;
}

void Shell::accept_confirm() {
  const ConfirmSpec& spec = confirm_.spec;
  const Decision d = confirm_decide(spec, state());
  if (d.route == Route::Execute) return execute(spec);
  if (d.route == Route::Queue) return queue_instead(spec, queue_reason_text(d.reason));
  toast(ToastLevel::Warn, confirm_unrun_text(d.reason).c_str());  // offline or no Claude (U7)
  close_overlay();
}

void Shell::execute(const ConfirmSpec& spec) {
  const Opt<ProjectKey> key = key_of(state(), spec.slot);
  if (!key.has) return queue_instead(spec, tr(kUnknownProject));
  AgentDraft d;
  d.kind = spec.kind == ConfirmKind::Spawn ? AgentKind::Spawn : AgentKind::Reply;
  d.project = key.value;
  d.sid = spec.sid.value_or(Sid{});
  d.qid = spec.qid;
  d.text = spec.text;
  if (!port_.send_agent(d)) return queue_instead(spec, tr(kBusy));
  close_overlay();  // the result toast follows (agent.result, or "Unknown result")
}

void Shell::queue_instead(const ConfirmSpec& spec, const char* why) {
  if (spec.qid.has) {
    toast(ToastLevel::Warn, tr(kStillOnMac));
    return close_overlay();
  }
  const Opt<QueueBody> q = confirm_queue_body(spec, state());
  if (!q.has) {  // no project to queue it under: back to the editor, never E_STORAGE (U13)
    overlay_ = proto::Overlay::Compose;
    if (spec.kind == ConfirmKind::Feedback) return hint(ToastLevel::Warn, tr(kReplyOnMac));
    return need_project();
  }
  const SaveResult r = port_.save_queue(q.value);
  if (r != SaveResult::Saved && r != SaveResult::Spared) {
    overlay_ = proto::Overlay::Compose;  // keep the text: back to the editor
    return save_result_toast(r);
  }
  toast(ToastLevel::Info, confirm_queued_text(spec.kind, why).c_str());  // docs/20 U7
  close_overlay();
}

void Shell::save_result_toast(SaveResult r) {
  char buf[64];
  if (r == SaveResult::Saved || r == SaveResult::Spared) retract_hints();  // refusals of the draft are over
  switch (r) {
    case SaveResult::Saved:
      std::snprintf(buf, sizeof buf, tr(kSaved), static_cast<unsigned>(state().outbox.pending));  // U21
      return toast(ToastLevel::Ok, buf);
    case SaveResult::Full: return toast(ToastLevel::Error, tr(kStoreFull));
    case SaveResult::NoStorage: return toast(ToastLevel::Error, tr(kNoStorage));
    case SaveResult::CardLost: return toast(ToastLevel::Error, tr(kCardLost));  // docs/20 U16
    case SaveResult::Spared: return toast(ToastLevel::Warn, tr(kSpared));
    default: return toast(ToastLevel::Error, tr(kSaveFailed));
  }
}

void Shell::close_overlay() {
  overlay_ = proto::Overlay::None;
  overlay_ms_ = now_;
  dirty_ = true;
}

void Shell::tick(uint32_t now, bool lit) {
  now_ = now;
  g0_due(now);  // gh #51: no second tap came: the note
  const bool seen = lit && !lock_.locked;
  const ToastQueue q = seen ? toast_tick(toast_seen(toasts_, now), now) : toast_hold(toasts_, now);
  if (q.n != toasts_.n || q.unseen != toasts_.unseen) dirty_ = true;
  toasts_ = q;
  egg_tick(now);
  const LockState l = lock_tick(lock_, now);
  const bool changed = l.card != lock_.card || l.swallowed != lock_.swallowed;
  lock_ = l;
  if (!changed) return;
  dirty_ = true;
  mirror();  // the card that answered a swallowed hold went dark
}

const AppState& Shell::state() const { return port_.state(); }
const SysInfo& Shell::sys() const { return port_.sys(); }

void Shell::compose(const ComposeSpec& spec) {
  compose_ = compose_open(spec);
  overlay_ = proto::Overlay::Compose;
  overlay_ms_ = now_;
  dirty_ = true;
}

void Shell::confirm(const ConfirmSpec& spec) {
  confirm_.spec = spec;
  confirm_.opened_ms = now_;
  confirm_.draft = false;
  overlay_ = proto::Overlay::Confirm;
  overlay_ms_ = now_;
  dirty_ = true;
}

bool Shell::request(const OnlineRequest& r) { return port_.send_request(r); }

void Shell::open(const char* moduleId, uint8_t arg) { go(moduleId, arg, true); }

// push: a new level even over the same module (a module's open()); else the global keys'
// v0.3 rule, which replaces a page of the module on top (nav.h).
void Shell::go(const char* moduleId, uint8_t arg, bool push) {
  const int idx = index_of(moduleId);
  if (idx < 0) return toast(ToastLevel::Warn, tr(kNoModule));
  const NavEntry e{static_cast<uint8_t>(idx), arg};
  nav_ = push ? nav_push(nav_, e) : nav_open(nav_, e);
  enter(nav_top(nav_));
}

void Shell::toast(ToastLevel lv, const char* utf8) {
  toasts_ = toast_result(toasts_, lv, utf8, now_);
  dirty_ = true;
}

}  // namespace cpa
