// Shared rig for the writer / recovery tests: a RecEngine on FakeRecFilePort.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rec_engine.h"
#include "support/fake_rec_file_port.h"
#include "wav_header.h"

namespace rectest {

using namespace cpa;
using namespace cpa::rec;

inline const MonoId kEpochBoot = mono_parse("3QZK1M7-0000C-0000", 18).value;

struct Sink : EvtSink {
  std::vector<RecEvt> events;
  void emit(const RecEvt& e) override { events.push_back(e); }
  size_t count(EvtKind k) const {
    size_t n = 0;
    for (const auto& e : events) n += e.kind == k ? 1 : 0;
    return n;
  }
  const RecEvt* last(EvtKind k) const {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
      if (it->kind == k) return &*it;
    }
    return nullptr;
  }
  std::vector<MonoId> committed() const {
    std::vector<MonoId> out;
    for (const auto& e : events) {
      if (e.kind == EvtKind::Committed) out.push_back(e.id);
    }
    return out;
  }
};

struct Engine {
  testsupport::FakeSha256 sha;
  MonoCounter ids;
  RecIndex index;
  TransferBuf xfer;
  std::unique_ptr<RecEngine> engine;
  Sink sink;

  Engine(testsupport::FakeRecFilePort& fs, uint32_t first_seq = 0x0A1C) {
    const BootRecord rec{kEpochBoot.epoch, 12};
    ids.start(boot_ticket(rec, rec).value, first_seq);
    EngineDeps d;
    d.fs = &fs;
    d.sha = &sha;
    d.ids = &ids;
    d.index = &index;
    d.xfer = &xfer;
    d.fw = FixedStr<31>::from("cpa-fw/0.3.0");
    engine.reset(new RecEngine(d));
  }

  void scan(size_t bytes = 4800) {
    alignas(8) static uint8_t scratch[24576];
    engine->scan(scratch, bytes < sizeof scratch ? bytes : sizeof scratch, sink);
  }
};

inline std::string pcm(size_t n, uint32_t seed) {
  std::string s(n, '\0');
  uint32_t x = seed * 2654435761u + 1;
  for (size_t i = 0; i < n; ++i) {
    x = x * 1103515245u + 12345u;
    s[i] = static_cast<char>(x >> 16);
  }
  return s;
}

inline const uint8_t* u8(const std::string& s) { return reinterpret_cast<const uint8_t*>(s.data()); }

inline std::string wav_of(const MonoId& id) { return rec_wav_path("/cpa", id).c_str(); }
inline std::string meta_of(const MonoId& id) { return rec_meta_path("/cpa", id).c_str(); }

inline Opt<RecMeta> read_meta(const testsupport::FakeRecFilePort& fs, const MonoId& id) {
  const std::string t = fs.get(meta_of(id));
  return rec_meta_decode(t.data(), t.size(), id);
}

inline MonoId seq_id(uint32_t seq) {
  MonoId id = kEpochBoot;
  id.boot = 12;
  id.seq = seq;
  return id;
}

}  // namespace rectest
