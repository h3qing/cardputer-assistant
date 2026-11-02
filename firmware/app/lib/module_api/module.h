// Module interface (05 §5.4, minimal). A module is a thin shell around a value-typed
// UI state updated by pure functions in <module>_logic.cpp; it never includes
// Arduino, M5 or esp_* headers, so it is tested on the host with FakeContext and
// FakeCanvas. Modules are registered statically in src/module_list.cpp (05 §5.5).
//
// Two small additions to 05 §5.4, both with defaults:
//   - hint(): the module's line for the hint bar (04 §8.5);
//   - contextSlot(): the project the user is looking at, which the shell uses as the
//     default Compose target (04 US-01 AC2) and mirrors into AppState.ctx.
// ModuleContext::sys() carries the system-page facts AppState does not hold
// (04 US-16: firmware version, dev, reset reason, daemon version, scan counters; v0.2,
// docs/09-m2-design.md §8: boot mode, primary store, migration and storage errors).
//
// v0.4.0 (docs/13 §10.1), both with defaults so v0.3 modules are unchanged:
//   - Module::help(): the module's own rows for the Tab menu's Here page;
//   - ModuleContext::press(): "press this key for me". The home tiles and the menu rows
//     run only by replaying a key, so they always do what the key does.
// v0.5 (docs/19 §3.5, §4.3), with a default: Module::animate(), the page's motion.
// v0.4.3 (docs/21 §4.6, §6.6, §6.7), all with defaults so earlier modules are unchanged:
//   - Module::hintKeys(): the hint bar as keys and Txt words, drawn in the shown language;
//   - HelpRow::text: the row's words as a Txt (else label, as before), for the Tab menu
//     and the home tiles;
//   - ModuleContext::setLanguage(): the settings page switches the language through the
//     shell, which redraws, says how to switch back and stores it (never the module).
#pragma once

#include <cstdint>

#include "app_state.h"
#include "canvas.h"
#include "chord_detector.h"
#include "storage_plan.h"
#include "txt.h"

namespace cpa {

enum class ToastLevel : uint8_t { Info, Ok, Warn, Error };

enum class ComposeKind : uint8_t { Note, Queue, Spawn, Feedback };

struct ComposeSpec {
  ComposeKind kind = ComposeKind::Note;
  uint8_t slot = 0;  // target project, 0 = unassigned
  Opt<Sid> sid;      // Feedback: the session
};

enum class ConfirmKind : uint8_t { Spawn, Feedback };

// What the confirm box is about to do. Execution (agent.*) only ever starts here.
struct ConfirmSpec {
  ConfirmKind kind = ConfirmKind::Spawn;
  uint8_t slot = 0;
  Opt<Sid> sid;  // Feedback
  Opt<Qid> qid;  // Spawn from a queue item: the daemon uses its stored full text
  UserText text;
};

// docs/28 S1a: what System -> storage asks of the kept copies (the runtime answers with a
// toast once the audio task has it on the card). Give: undo Again for the Mac on the link.
enum class KeptAsk : uint8_t { Again, Clear, Give };

// Online requests a module may send directly (05 §6.3: at most one in flight).
// agent.* are not here on purpose: they only go out through confirm().
enum class RequestKind : uint8_t { QueueDone, InboxDismiss };

struct OnlineRequest {
  RequestKind kind = RequestKind::QueueDone;
  Qid qid;  // QueueDone
  Sid sid;  // InboxDismiss
};

struct SysInfo {
  FixedStr<31> fw;
  FixedStr<12> dev;
  FixedStr<16> rst;
  FixedStr<32> host_app;  // from the host hello ("cpad/0.1.0"); empty until the first one
  FixedStr<24> host_label;  // docs/21 §4.2: the Mac's name from the last hello ("" without one)
  bool ids_ok = false;    // NVS boot counter advanced: MonoIds can be issued
  uint16_t outbox_broken = 0;
  uint16_t outbox_deferred = 0;
  uint8_t modules_disabled = 0;
  // Storage, chosen once at boot (09 §3.4) and never switched while running.
  Store store = Store::Internal;
  SdState sd = SdState::Disabled;
  uint32_t sd_mib = 0;       // card size
  bool launcher = false;     // running under the bmorcelli Launcher
  uint32_t cpafs_bytes = 0;  // internal partition, 0 without one
  StoreNote note = StoreNote::None;
  uint16_t migrated = 0;        // cpafs items moved to the card at this boot
  uint16_t migrate_failed = 0;  // left in cpafs (retried next boot)
  uint16_t store_errors = 0;    // failed saves, outbox_apply and cache writes since boot
};

// One Here-page row of the Tab menu (docs/13 §6.2). Enter on the row closes the menu and
// replays ev; a row whose ev.kind is None (a range such as "1-9") is shown, not replayed.
struct HelpRow {
  KeyEvent ev;
  const char* keys = "";   // UTF-8, the key as the row shows it ("Space", "1-9")
  const char* label = "";  // UTF-8, what it does
  const Txt* text = nullptr;  // docs/21 §6.7: what it does in both languages; label when unset
};

// The row's words in the shown language.
inline const char* help_label(const HelpRow& r) { return r.text ? tr(*r.text) : r.label; }

// One key of a structured hint bar (docs/21 §6.6): {"Enter", {"打开", "open"}}. The shell
// adds "[Tab] menu" after the module's keys.
struct HintKey {
  const char* keys;
  Txt words;
};

class ModuleContext {
 public:
  virtual ~ModuleContext() = default;
  virtual const AppState& state() const = 0;
  virtual void compose(const ComposeSpec& spec) = 0;  // the shared editor
  virtual void confirm(const ConfirmSpec& spec) = 0;  // keys in the first 400 ms are ignored
  virtual bool request(const OnlineRequest& r) = 0;   // false: not Ready or one in flight
  virtual void open(const char* moduleId, uint8_t arg) = 0;
  virtual void toast(ToastLevel lv, const char* utf8) = 0;
  // A key the module refuses (docs/20 U17): said once, not again within 3 s, and gone
  // when the page changes. The default is a plain toast.
  virtual void hint(ToastLevel lv, const char* utf8) { toast(lv, utf8); }
  virtual uint32_t nowMs() const = 0;
  virtual const SysInfo& sys() const = 0;
  // Queue ev to be dispatched once the current key is done, as if the user pressed it
  // (docs/13 §5.4 step 9). One level: a replayed key cannot queue another.
  // false: not queued (the default; or one is already queued, or this is a replay).
  virtual bool press(const KeyEvent& ev) {
    (void)ev;
    return false;
  }
  // docs/21 §4.3: show the UI in l from now on (redrawn at once, stored across restarts).
  // false: not switched (the default, or l is already shown).
  virtual bool setLanguage(Lang l) {
    (void)l;
    return false;
  }
  // docs/28 S1a: deliver the last batch again / clear the kept copies / give back to the Mac
  // on the link what was kept from it. false: not now (the
  // default; or a recording runs, or one is already on its way).
  virtual bool kept(KeptAsk a) {
    (void)a;
    return false;
  }
};

class Module {
 public:
  virtual ~Module() = default;
  virtual const char* id() const = 0;            // ^[a-z][a-z0-9_]{1,15}$
  virtual bool begin(ModuleContext& ctx) = 0;    // false: disabled (shown on the system page)
  virtual void onEnter(uint8_t arg) { (void)arg; }  // became the foreground module
  virtual bool onKey(const KeyEvent& key) = 0;   // true: handled
  virtual void render(Canvas& c, const Rect& area) const = 0;
  virtual const char* hint() const { return ""; }
  virtual uint8_t contextSlot() const { return 0; }
  // Sets *rows to the module's Here-page rows and returns how many (0: none, *rows nullptr).
  // The rows stay valid until the module's next onEnter() or onKey().
  virtual uint8_t help(const HelpRow** rows) const {
    *rows = nullptr;
    return 0;
  }
  // docs/21 §6.6: the hint bar as keys (0: none, *keys nullptr; the bar then draws hint()).
  // Without the Tab key, which the shell adds; at most 7, valid as help()'s rows are.
  virtual uint8_t hintKeys(const HintKey** keys) const {
    *keys = nullptr;
    return 0;
  }
  // v0.5 (docs/19 §3.5, §7): one animation tick (theme::kAnimTickMs). Redraw into c what
  // moved on the page since the last tick, over what is there, and return that rectangle
  // (w == 0: nothing moved); the shell pushes only it. Called only while the backlight is On
  // and the page is on top (no menu, overlay or lock). Never ask for a redraw here: render()
  // draws what the last tick showed, so the page stands still between ticks.
  virtual Rect animate(Canvas& c, const Rect& area, uint32_t now) {
    (void)c;
    (void)area;
    (void)now;
    return Rect{};
  }
};

}  // namespace cpa
