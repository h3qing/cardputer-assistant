// FakeContext: a ModuleContext that records what a module asked the shell to do.
// help_rows(): a module's Tab-menu rows (Module::help) as a vector.
#pragma once

#include <string>
#include <vector>

#include "module.h"

namespace uitest {

class FakeContext : public cpa::ModuleContext {
 public:
  cpa::AppState app;
  cpa::SysInfo info;
  uint32_t now = 0;
  bool accept_requests = true;
  bool accept_press = true;

  std::vector<cpa::ComposeSpec> composed;
  std::vector<cpa::ConfirmSpec> confirmed;
  std::vector<cpa::OnlineRequest> requests;
  std::vector<std::string> opened;  // "<id>:<arg>"
  std::vector<std::string> toasts;
  std::vector<std::string> hints;  // hint(): a refused key's answer (docs/20 U17)
  std::vector<cpa::KeyEvent> pressed;  // press(): the keys the module asked to replay
  std::vector<cpa::Lang> languages;    // setLanguage(): switched to, as the shell does
  std::vector<cpa::KeptAsk> kept_asks;  // kept(): docs/28 S1a
  bool kept(cpa::KeptAsk a) override {
    kept_asks.push_back(a);
    return true;
  }

  const cpa::AppState& state() const override { return app; }
  void compose(const cpa::ComposeSpec& spec) override { composed.push_back(spec); }
  void confirm(const cpa::ConfirmSpec& spec) override { confirmed.push_back(spec); }
  bool request(const cpa::OnlineRequest& r) override {
    if (!accept_requests) return false;
    requests.push_back(r);
    return true;
  }
  void open(const char* id, uint8_t arg) override { opened.push_back(std::string(id) + ":" + std::to_string(arg)); }
  void toast(cpa::ToastLevel, const char* utf8) override { toasts.push_back(utf8); }
  void hint(cpa::ToastLevel, const char* utf8) override { hints.push_back(utf8); }
  uint32_t nowMs() const override { return now; }
  const cpa::SysInfo& sys() const override { return info; }
  bool press(const cpa::KeyEvent& ev) override {
    if (!accept_press) return false;
    pressed.push_back(ev);
    return true;
  }

  bool setLanguage(cpa::Lang l) override {
    if (l == cpa::lang()) return false;
    languages.push_back(l);
    cpa::set_lang(l);
    return true;
  }

  bool toasted(const std::string& needle) const { return any(toasts, needle); }
  bool hinted(const std::string& needle) const { return any(hints, needle); }

 private:
  static bool any(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& t : v) {
      if (t.find(needle) != std::string::npos) return true;
    }
    return false;
  }
};

inline std::vector<cpa::HelpRow> help_rows(const cpa::Module& m) {
  const cpa::HelpRow* rows = nullptr;
  const uint8_t n = m.help(&rows);
  return rows == nullptr ? std::vector<cpa::HelpRow>{} : std::vector<cpa::HelpRow>(rows, rows + n);
}

}  // namespace uitest
