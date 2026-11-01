// ShellPort: what the shell needs from the runtime underneath it. The runtime owns
// the AppState buffers, the outbox and the link; the shell only asks.
#pragma once

#include <cstdint>

#include "agent_req.h"
#include "module.h"
#include "rec_core.h"
#include "settings.h"

namespace cpa {

// NoStorage: there is no usable store at all (09 §8); checked before Full, because the
// NullStore's free_bytes() is 0. docs/20 U16: CardLost, the SD card latched while running
// (every save fails until a reboot); Spared, it latched but the capture was kept in the
// internal cpafs (the next boot's migration moves it to the card).
enum class SaveResult : uint8_t { Saved, Full, Failed, NoStorage, CardLost, Spared };

class ShellPort {
 public:
  virtual ~ShellPort() = default;
  virtual const AppState& state() const = 0;
  virtual const SysInfo& sys() const = 0;
  // Writes an outbox item; Saved only once it is durable (05 §5.9).
  virtual SaveResult save_note(const NoteBody& body) = 0;
  virtual SaveResult save_queue(const QueueBody& body) = 0;
  // Sends agent.spawn / agent.reply; false when not Ready or one is in flight.
  virtual bool send_agent(const AgentDraft& draft) = 0;
  // Sends queue.done / inbox.dismiss; false when not Ready or one is in flight.
  virtual bool send_request(const OnlineRequest& r) = 0;
  // Mirrors the default Compose target into AppState.ctx.
  virtual void set_context_slot(uint8_t slot) = 0;
  // v0.4.0: mirrors the menu and the lock into AppState.ui (debug.state.ui).
  virtual void set_ui(const UiFlags& ui) { (void)ui; }
  // v0.3 recording (docs/11 §4.1): G0 held or Opt+R from the global layer; the refusal
  // to show, or None. Without recording support every gesture is refused (no SD card).
  virtual rec::RecRefusal rec_gesture(RecMode mode, uint8_t slot) {
    (void)mode;
    (void)slot;
    return rec::RecRefusal::NoSd;
  }
  // What a gesture now would be refused for, or None (docs/20 D8: "● 准备" only for a hold
  // that records).
  virtual rec::RecRefusal rec_refusal() const { return rec::RecRefusal::NoSd; }
  // A key while a recording overlay is up, already mapped by the shell (§4.1 tables).
  virtual void rec_input(rec::RecInput in, uint8_t arg) {
    (void)in;
    (void)arg;
  }
  // docs/21 §6.8: the shell switched to l; keep it across restarts. The default keeps nothing.
  virtual SettingsSave store_lang(Lang l) {
    (void)l;
    return SettingsSave::Failed;
  }
  // docs/28 S1a: a Ledger command for the screen (runtime_kept.cpp). The default does nothing.
  virtual bool kept_ask(KeptAsk a) {
    (void)a;
    return false;
  }
};

}  // namespace cpa
