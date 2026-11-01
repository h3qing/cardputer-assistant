// FakePort: a ShellPort that records what the shell saved and sent.
#pragma once

#include <utility>
#include <vector>

#include "shell_port.h"

namespace uitest {

class FakePort : public cpa::ShellPort {
 public:
  cpa::AppState app;
  cpa::SysInfo info;
  cpa::SaveResult next_save = cpa::SaveResult::Saved;
  bool agent_ok = true;
  bool request_ok = true;
  uint8_t context_slot = 0;

  std::vector<cpa::NoteBody> notes;
  std::vector<cpa::QueueBody> queued;
  std::vector<cpa::AgentDraft> agents;
  std::vector<cpa::OnlineRequest> requests;
  // v0.3 recording: what the shell asked for. Without a card the answer is NoSd.
  cpa::rec::RecRefusal next_refusal = cpa::rec::RecRefusal::NoSd;
  std::vector<std::pair<cpa::RecMode, uint8_t>> gestures;
  std::vector<std::pair<cpa::rec::RecInput, uint8_t>> inputs;
  // docs/21 §6.8: the languages the shell asked to keep, and what keeping them came to.
  cpa::SettingsSave next_store = cpa::SettingsSave::Saved;
  std::vector<cpa::Lang> stored;

  const cpa::AppState& state() const override { return app; }
  const cpa::SysInfo& sys() const override { return info; }

  cpa::SaveResult save_note(const cpa::NoteBody& b) override {
    if (next_save == cpa::SaveResult::Saved) {
      notes.push_back(b);
      app.outbox.pending++;
    }
    return next_save;
  }

  cpa::SaveResult save_queue(const cpa::QueueBody& b) override {
    if (next_save == cpa::SaveResult::Saved) {
      queued.push_back(b);
      app.outbox.pending++;
    }
    return next_save;
  }

  bool send_agent(const cpa::AgentDraft& d) override {
    if (!agent_ok) return false;
    agents.push_back(d);
    return true;
  }

  bool send_request(const cpa::OnlineRequest& r) override {
    if (!request_ok) return false;
    requests.push_back(r);
    return true;
  }

  void set_context_slot(uint8_t slot) override { context_slot = slot; }

  cpa::rec::RecRefusal rec_gesture(cpa::RecMode mode, uint8_t slot) override {
    gestures.emplace_back(mode, slot);
    return next_refusal;
  }
  cpa::rec::RecRefusal rec_refusal() const override { return next_refusal; }
  void rec_input(cpa::rec::RecInput in, uint8_t arg) override { inputs.emplace_back(in, arg); }
  cpa::SettingsSave store_lang(cpa::Lang l) override {
    stored.push_back(l);
    return next_store;
  }
};

// A module that records the keys it was given and claims the ones listed.
class ProbeModule : public cpa::Module {
 public:
  explicit ProbeModule(const char* id, bool claim_all = false) : id_(id), claim_all_(claim_all) {}
  std::vector<cpa::KeyEvent> keys;
  std::vector<uint8_t> entered;
  cpa::ModuleContext* ctx = nullptr;
  bool begin_ok = true;
  uint8_t slot = 0;
  const cpa::HintKey* hint_keys = nullptr;  // docs/21 §6.6: a structured hint bar when set
  uint8_t hint_key_n = 0;

  const char* id() const override { return id_; }
  bool begin(cpa::ModuleContext& c) override {
    ctx = &c;
    return begin_ok;
  }
  void onEnter(uint8_t arg) override { entered.push_back(arg); }
  bool onKey(const cpa::KeyEvent& k) override {
    keys.push_back(k);
    return claim_all_;
  }
  void render(cpa::Canvas& c, const cpa::Rect& area) const override {
    c.text(area.x, area.y, id_, cpa::Style{});
  }
  const char* hint() const override { return "probe hint"; }
  uint8_t contextSlot() const override { return slot; }
  uint8_t hintKeys(const cpa::HintKey** k) const override {
    *k = hint_keys;
    return hint_key_n;
  }

 private:
  const char* id_;
  bool claim_all_;
};

}  // namespace uitest
