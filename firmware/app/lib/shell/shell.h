// Shell (05 §5.6): navigation stack, key dispatch, the Compose / Confirm overlays,
// toasts and the status and hint bars. Key dispatch order (docs/13 §5.4):
//   1. the lock gate and 2. power_key, both in the runtime (lock_gate(), lock.h); a key
//      the gate passes while locked is G0 and goes to the recording path, whatever is
//      under the lock (a memo from the lit card, its release, or a meeting's ★);
//   3. the Tab menu: eats every key while open, G0 and (docs/20 D9) a chord it shows
//      excepted, which close it and act (shell_menu.cpp); the rest of a Backspace hold
//      that closed it is dropped;
//   4. Tab and Opt+L where the docs/13 §5.3 table has them (kKeymap: the System layer
//      on pages, the overlay's own layer over one);
//   5. v0.3 recording overlay (memo HUD, meeting confirm, meeting page, end confirm,
//      docs/11 §4.1): eats every key while the recording state machine is not Idle;
//   6. overlay (Compose, Confirm): eats every key while open;
//   7. global combos (G0, Opt+H/P/I/1..9/N/Q/S/M);
//   8. the foreground module; Back / Fn+` not handled by it pops the stack;
//   9. one replayed key, queued by press() or by Enter in the menu, dispatched from 3
//      once the key is done; a replayed key cannot queue another.
// gh #51 (docs/20 U26, shell_g0.cpp): before all of this, the rest of a G0 press the double
// tap or a memo took is eaten (in the lock gate while locked), and a key other than G0
// opens the note a waiting tap is due (g0_gesture.h). A memo follows G0's edges (on_g0).
// At 6, G0 held in an empty quick note closes it and records (its review).
// v0.5 S3 (docs/19 §3.5, §7): motion. animate() runs one animation tick (Mando's pose, the
// waveform or timeline, the big ●'s breath, the lock card's z's, a module's animate()) or a
// frame of the Tab sheet's slide, drawing only what moved and returning the rectangles to
// push; the runtime calls it between full redraws and only while the backlight is On. A tick
// never marks the screen dirty; render() draws what the last tick showed.
// The root page is the home module when registered, else the board (v0.3). A module's
// open() adds a level, its own pages too (the board's overview opens a project), so
// back returns to it; the global keys replace a page of the module on top (nav.h). The
// shell is the ModuleContext the modules see; everything with side effects goes
// through the ShellPort.
#pragma once

#include <cstdint>

#include "compose.h"
#include "confirm.h"
#include "g0_gesture.h"
#include "keymap.h"
#include "lock.h"
#include "menu_view.h"
#include "messages.h"
#include "module.h"
#include "nav.h"
#include "platform.h"
#include "shell_port.h"
#include "theme_anim.h"
#include "theme_levels.h"
#include "toast.h"

namespace cpa {

constexpr uint8_t kMaxModules = 8;

// What one animate() call drew: up to four rectangles to push, and the big ●'s breath step
// for Display::breathe (-1: unchanged).
struct AnimFrame {
  static constexpr uint8_t kMax = 4;
  uint8_t n = 0;
  Rect rects[kMax];
  int8_t breath = -1;
};

class Shell : public ModuleContext {
 public:
  Shell(ShellPort& port, Module* const* modules, uint8_t n);

  void begin(uint32_t now);  // begin() every module; duplicate or bad ids are disabled
  void on_key(const KeyEvent& ev, uint32_t now);
  // gh #51: G0 went down / up (the runtime's key samples, after that sample's events). A
  // memo follows these edges, not G0's events (rec_core.h: the grace, hands free), and the
  // double tap starts on one (g0_gesture.h).
  void on_g0(bool down, uint32_t now);
  bool g0_waiting() const { return g0_waits(g0_); }  // a tap waits for a second one
  // lit: the backlight is on. Locked or dark, the shown toast waits (toast.h).
  void tick(uint32_t now, bool lit = true);
  void render(Canvas& c, uint32_t now) const;

  bool dirty() const { return dirty_; }
  void mark_dirty() { dirty_ = true; }
  void clear_dirty() { dirty_ = false; }

  // For debug.state and the runtime.
  const char* top_id() const;
  proto::Overlay overlay() const { return overlay_; }
  uint32_t overlay_ms() const { return overlay_ms_; }
  uint8_t disabled_count() const;
  uint8_t compose_slot() const;  // the default Compose target right now (a memo's project)
  // v0.4.0 (docs/13 §5.4, §7.1)
  bool lock_gate(const KeyEvent& ev, uint32_t now);  // step 1; false: swallowed
  bool locked() const { return lock_.locked; }
  bool lock_lit(uint32_t now) const { return cpa::lock_lit(lock_, now); }
  bool lock_seen(uint32_t now) const { return lock_lit(now) && !lock_.flash; }  // lit by a key (docs/20 D7)
  bool locks(const KeyEvent& ev) const;  // ev would lock now (Opt+L where §5.3 has it)
  void auto_lock();                               // docs/20 D2: dark on battery
  void lock_flash(uint32_t now, uint32_t ms);     // docs/20 D7: locked, the card lights a while
  void g0_level(bool down, bool pressing, uint32_t now);  // docs/20 U3c, D8 (shell_menu.cpp)
  bool menu_open() const { return menu_open_; }
  // v0.5.0 (docs/04, hidden keys; shell_eggs.cpp): Opt+W's pats, and whether home is what
  // the user sees now (unlocked, nothing over it): the all clear is shown only there.
  const PatState& pat() const { return pat_; }
  bool on_home() const;
  MenuSource menu_source() const;  // what the menu is opened over (shell_menu.cpp)
  // v0.5 S3 (shell_anim.cpp)
  void set_backlight(Backlight lv);    // Off / Dim: nothing moves, the ● at full Rec
  void on_level(uint16_t rms);         // every audio block, whatever the backlight
  void rec_started();                  // a recording's first sample: a new level history
  AnimFrame animate(Canvas& c, uint32_t now);
  bool sliding() const { return menu_mo_.slide != MenuSlide::None; }  // a slide is on its way

  // ModuleContext
  const AppState& state() const override;
  void compose(const ComposeSpec& spec) override;
  void confirm(const ConfirmSpec& spec) override;
  bool request(const OnlineRequest& r) override;
  void open(const char* moduleId, uint8_t arg) override;
  void toast(ToastLevel lv, const char* utf8) override;  // the user's own result (toast.h)
  void notice(ToastLevel lv, const char* utf8);           // from the Mac: behind the results
  void hint(ToastLevel lv, const char* utf8) override;    // a refused key: once in 3 s
  void retract_hints();                                   // their condition is gone
  uint32_t nowMs() const override { return now_; }
  const SysInfo& sys() const override;
  bool press(const KeyEvent& ev) override;
  // docs/21 §4.3: shown at once, redrawn, a toast in both scripts saying how to switch back,
  // stored by the port (a warning when it could not be). debug.lang takes this path too.
  bool setLanguage(Lang l) override;
  bool kept(KeptAsk a) override { return port_.kept_ask(a); }

 private:
  int index_of(const char* id) const;
  uint8_t first_enabled() const;
  Module* top() const;
  void enter(const NavEntry& e);
  void dispatch(const KeyEvent& ev);
  void locked_key(const KeyEvent& ev);
  void go(const char* moduleId, uint8_t arg, bool push);
  void mirror();
  bool meeting() const;
  // shell_menu.cpp: the menu, Tab / Opt+L, the lock
  Layer under_layer() const;
  bool system_key(const KeyEvent& ev);
  void menu_key_event(const KeyEvent& ev);
  void lock_now();
  void leave_rec_box();
  bool memo_would_start(uint32_t now) const;
  void render_menu(Canvas& c) const;
  Rect render_menu_slide(Canvas& c) const;
  bool global_key(const KeyEvent& ev);
  void module_key(const KeyEvent& ev);
  void compose_key_event(const KeyEvent& ev);
  bool rec_up() const;
  Layer rec_layer() const;  // the recording page's keys: RecMemo, RecLatch or RecPage
  void rec_key_event(const KeyEvent& ev);
  void rec_box_key(const KeyEvent& ev);  // docs/20 D3: the meeting's boxes
  void meeting_hint(const KeyEvent& ev);  // ... and what the meeting page ignores
  bool rec_gesture(RecMode mode);  // false: refused (the toast says why)
  // shell_g0.cpp (gh #51)
  void g0_down();
  void memo_press(const RecInfo& r, bool lit);  // G0 down on a memo: the memo's press
  bool eat_g0(const KeyEvent& ev);     // the rest of a taken press: eaten
  bool locked_g0(const KeyEvent& ev);  // the lock gate's part: true = swallowed
  void g0_other_key();
  void g0_due(uint32_t now);
  void open_note();
  bool hands_free(bool g0_down);  // false: refused (the toast says why)
  bool empty_note() const;
  bool note_hold(const KeyEvent& ev);  // G0 held in an empty quick note: true = taken
  void confirm_key_event(const KeyEvent& ev);
  void submit_compose();
  void need_project();               // docs/20 U13: the target does not exist
  void refused_in_compose(const KeyEvent& ev);  // docs/20 U17
  static bool refused_global(const KeyEvent& ev);
  bool open_project(uint8_t slot);   // Opt+1..9: only a slot that exists
  void accept_confirm();
  void execute(const ConfirmSpec& spec);
  void queue_instead(const ConfirmSpec& spec, const char* why);
  void save_result_toast(SaveResult r);
  void close_overlay();
  const char* hint_text() const;
  void render_hint(Canvas& c) const;  // hint_text(), or a module's hintKeys() (docs/21 §6.6)
  void render_plug(Canvas& c) const;  // docs/28 §3.2: the plug-in hint
  void render_content(Canvas& c, uint32_t now) const;
  void render_compose(Canvas& c) const;
  void render_confirm(Canvas& c, uint32_t now) const;
  void render_rec(Canvas& c, uint32_t now) const;  // rec_overlay.cpp
  const char* rec_hint() const;
  // shell_eggs.cpp (v0.5.0)
  bool pat_key(const KeyEvent& ev);                  // home: Opt+W
  bool lock_pat(const KeyEvent& ev, uint32_t now);   // the lit clock card: Opt+W
  bool pat_allowed_locked(uint32_t now) const;
  void pat(uint32_t now);
  void egg_tick(uint32_t now);                       // a moment over: the page as it was
  // shell_anim.cpp
  void slide(MenuSlide kind, MenuPage from);
  AnimFrame slide_frame(Canvas& c, uint32_t now);
  AnimFrame doze_tick(Canvas& c, uint32_t now);
  AnimFrame rec_tick(Canvas& c, uint32_t now);
  AnimFrame module_tick(Canvas& c, uint32_t now);
  void rec_breath(AnimFrame& f, const RecInfo& r);

  ShellPort& port_;
  Module* const* modules_;
  uint8_t n_;
  bool enabled_[kMaxModules] = {};
  uint8_t home_ = 0;
  NavStack nav_;
  proto::Overlay overlay_ = proto::Overlay::None;
  uint32_t overlay_ms_ = 0;
  ComposeState compose_;
  ConfirmState confirm_;
  ToastQueue toasts_;
  uint32_t now_ = 0;
  bool dirty_ = true;
  bool menu_open_ = false;
  MenuState menu_;
  LockState lock_;
  KeyEvent pending_;        // press(): the key to replay once the current one is done
  bool replaying_ = false;  // dispatching pending_: press() refuses
  bool eat_repeats_ = false;  // the menu closed: drop the rest of that Backspace hold
  uint32_t discard_ms_ = 0;   // the discard prompt opened (its 400 ms guard, docs/20 U18)
  bool g0_down_ = false;      // docs/20 U3c, D8: G0 on the matrix (g0_level)
  bool pressing_ = false;     // ... down and short of a hold
  bool memo_ready_ = false;   // ... and a hold would record a memo: "● 准备"
  G0Gesture g0_;              // gh #51: the double tap (shell_g0.cpp)
  PatState pat_;              // v0.5.0: Opt+W (shell_eggs.cpp), RAM only
  bool egg_on_ = false;       // ... a pat or the all clear is on show

  // v0.5 S3: motion, moved only by animate() (shell_anim.cpp)
  Backlight light_ = Backlight::On;
  uint32_t tick_at_ = 0;
  uint8_t breath_ = 0;
  theme::MandoMotion mando_;    // the recording pages' and the lock card's Mando
  theme::LevelHistory live_;    // every audio block of the recording
  theme::LevelHistory shown_;   // what the pages draw: live_ at the last tick
  MenuMotion menu_mo_;
  uint32_t menu_mo_at_ = 0;
};

}  // namespace cpa
