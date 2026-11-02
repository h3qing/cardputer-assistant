// Fakes for the runtime's ports: a byte-level Transport, the Platform and the Display.
// Lines the device sends are decoded back with the real codec, so a test can only
// observe contract-valid output.
#pragma once

#include <string>
#include <vector>

#include "codec.h"
#include "core/support/lang_pin.h"
#include "platform.h"
#include "support/fake_canvas.h"
#include "transport.h"

namespace uitest {

class FakeTransport : public cpa::Transport {
 public:
  bool is_plugged = true;
  bool accept = true;
  std::string rx;
  std::vector<std::string> sent;
  std::vector<cpa::TxClass> classes;
  uint32_t ticks = 0;

  bool plugged() const override { return is_plugged; }
  size_t read(char* buf, size_t cap) override {
    const size_t n = rx.size() < cap ? rx.size() : cap;
    rx.copy(buf, n);
    rx.erase(0, n);
    return n;
  }
  bool send(const char* line, size_t n, cpa::TxClass cls) override {
    if (!accept) return false;
    sent.emplace_back(line, n);
    classes.push_back(cls);
    return true;
  }
  void tick(uint32_t) override { ++ticks; }

  void host(const std::string& json) { rx += json + "\n"; }

  // Decodes every sent line of type t ("" = all) in order.
  std::vector<cpa::proto::Decoded> of_type(const char* t) {
    std::vector<cpa::proto::Decoded> out;
    for (const auto& line : sent) {
      const cpa::proto::Decoded& d = codec_.decode(line.data(), line.size());
      if (d.outcome != cpa::proto::Outcome::Ok) continue;
      if (*t == 0 || std::string(cpa::proto::type_name(cpa::proto::msg_type(d.msg))) == t) out.push_back(d);
    }
    return out;
  }

  size_t count(const char* t) { return of_type(t).size(); }
  bool all_valid() {
    for (const auto& line : sent) {
      if (codec_.decode(line.data(), line.size()).outcome != cpa::proto::Outcome::Ok) return false;
    }
    return true;
  }

 private:
  cpa::proto::Codec codec_;
};

class FakePlatform : public cpa::Platform {
 public:
  cpa::DeviceStats next;
  int reinits = 0;
  int reboots = 0;
  std::vector<bool> backlight;
  std::vector<cpa::Backlight> levels;
  size_t room = static_cast<size_t>(-1);  // free bytes in the TX ring
  cpa::LoopStats loop;                    // what the device's loop() timed (debug.state.diag)

  cpa::DeviceStats stats() override { return next; }
  cpa::LoopStats loop_stats() const override { return loop; }
  void reinit_keyboard() override { ++reinits; }
  void set_backlight(bool on) override { backlight.push_back(on); }
  void set_backlight_level(cpa::Backlight lv) override {
    levels.push_back(lv);
    set_backlight(lv != cpa::Backlight::Off);
  }
  size_t tx_room() override { return room; }
  void reboot_soon() override { ++reboots; }
  uint32_t clock_us = 0;  // micros(): each read moves it on by step_us
  uint32_t step_us = 0;
  uint32_t micros() override { return clock_us += step_us; }
  // docs/21 §6.8: NVS's settings, kept across a Runtime rebuilt on this platform (the
  // simulator's debug.reboot); every write asked for, whether it took or not. Settings{}
  // (English), or Chinese in a suite that pinned it (lang_pin.h).
  cpa::Settings nvs = testsupport::fake_nvs();
  bool save_ok = true;
  std::vector<cpa::Settings> saves;
  cpa::Settings load_settings() override { return nvs; }
  bool save_settings(const cpa::Settings& s) override {
    saves.push_back(s);
    if (save_ok) nvs = s;
    return save_ok;
  }
  // docs/28 S1a: the offline mark NVS keeps (none until the first unplug).
  cpa::Opt<cpa::OfflineMark> mark;
  int mark_saves = 0;
  cpa::Opt<cpa::OfflineMark> load_offline_mark() override { return mark; }
  bool save_offline_mark(const cpa::OfflineMark& m) override {
    ++mark_saves;
    mark = cpa::some(m);
    return true;
  }
};

// canvas_ holds the last full frame's drawing, then what animation ticks drew over it since
// (v0.5: the device's sprite keeps the frame between ticks too). frames counts full pushes;
// partials and breaths record present(Rect) and breathe() in order.
class FakeDisplay : public cpa::Display {
 public:
  FakeCanvas canvas_;
  int frames = 0;
  std::vector<cpa::Rect> partials;
  std::vector<uint8_t> breaths;
  cpa::Canvas& canvas() override {
    mark_ = canvas_.mark();
    return canvas_;
  }
  void present() override {
    canvas_.drop_before(mark_);
    ++frames;
  }
  void present(const cpa::Rect& r) override { partials.push_back(r); }
  void breathe(uint8_t step) override { breaths.push_back(step); }

 private:
  FakeCanvas::Mark mark_;
};

}  // namespace uitest
