// A Runtime on fakes, with helpers to play the daemon's side of the protocol.
#pragma once

#include <string>

#include "core/support/fake_store.h"
#include "runtime.h"
#include "support/fake_io.h"
#include "support/fake_port.h"

namespace uitest {

inline cpa::BootTicket test_ticket(uint32_t boot = 12) {
  const cpa::BootRecord rec{0x0ABCDEF1u, boot};
  return cpa::boot_ticket(rec, rec).value;
}

struct RuntimeRig {
  FakeTransport tx;
  testsupport::FakeStore fs;
  FakePlatform plat;
  FakeDisplay disp;
  ProbeModule board{"board"};
  ProbeModule inbox{"inbox"};
  cpa::Module* mods[2] = {&board, &inbox};
  cpa::Runtime rt{tx, fs, plat, disp, mods, 2};
  uint32_t now = 0;
  int host_id = 1;

  explicit RuntimeRig(bool start = true) {
    if (start) begin();
  }

  void begin() {
    cpa::BootInfo b;
    b.ticket = test_ticket();
    b.dev = cpa::FixedStr<12>::from("020000000001");
    b.rst = cpa::FixedStr<16>::from("poweron");
    rt.begin(b, now);
  }

  void step(uint32_t ms = 10, cpa::KeySet keys = cpa::KeySet{}) {
    now += ms;
    rt.loop(now, keys);
  }

  // A real cable pull: plugged() stays false until the link's debounce gives way
  // (plug_filter.h); one false sample alone is a dropout, not an unplug.
  void unplug() {
    tx.is_plugged = false;
    step();
    step(cpa::kUnplugConfirmMs);
  }

  std::string next_id() { return "h" + std::to_string(host_id++); }

  void send(const std::string& t, const std::string& fields = "") {
    tx.host("{\"v\":0,\"t\":\"" + t + "\",\"id\":\"" + next_id() + "\"" + (fields.empty() ? "" : "," + fields) + "}");
  }

  void reply(const std::string& t, const std::string& re, const std::string& fields = "") {
    tx.host("{\"v\":0,\"t\":\"" + t + "\",\"id\":\"" + next_id() + "\",\"re\":\"" + re + "\"" +
            (fields.empty() ? "" : "," + fields) + "}");
  }

  void handshake(bool reply_cap = false) {
    send("hello", std::string("\"role\":\"host\",\"app\":\"cpad/0.1.0\",\"agent\":\"ok\",\"caps\":{\"spawn\":true,\"reply\":") +
                      (reply_cap ? "true" : "false") + "}");
    send("projects.snapshot",
         "\"rev\":3,\"projects\":[{\"slot\":1,\"key\":\"cpa\",\"name\":\"Cardputer 助手\",\"run\":1,\"need\":1,"
         "\"queued\":2,\"ok\":true}],\"other\":{\"run\":0,\"n\":1}");
    step();
  }

  void key(const cpa::KeyEvent& ev, uint32_t at_ms) {
    now = at_ms;
    rt.shell().on_key(ev, now);
  }

  std::string last_id_of(const char* t) {
    const auto v = tx.of_type(t);
    return v.empty() ? std::string() : std::string(v.back().msg.id.c_str());
  }
};

}  // namespace uitest
