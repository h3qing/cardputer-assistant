// The engine-level rig for the SENT.L ledger (docs/28 S1a): recordings made by the real
// RecEngine on FakeRecFilePort, handed over as the loop would (Del with the Mac's hash), the
// Ledger commands, and a "reboot" that scans the card again from scratch.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "../test_rec_recovery/cuts.h"
#include "ledger_rec.h"

namespace ledgertest {

using namespace rectest;

constexpr uint32_t kMacA = 0xA0A0A0A0;
constexpr uint32_t kMacB = 0xB0B0B0B0;
constexpr uint32_t kDay = 86400;
constexpr uint32_t kWall = 1800000000;
constexpr size_t kRing = 24576;  // the default build's ring: 1536 names, 1264 kept at most

inline const std::string kLedgerL = "/cpa/rec/" + std::string(mono_format(kEpochBoot).c_str(), 7) + "/SENT.L";
inline const std::string kLedgerK = "/cpa/rec/" + std::string(mono_format(kEpochBoot).c_str(), 7) + "/SENT.K";

inline uint8_t* scratch() {
  alignas(8) static uint8_t mem[kRing];
  return mem;
}

struct Dev {
  FakeRecFilePort& fs;
  Engine e;
  explicit Dev(FakeRecFilePort& f, uint32_t first_seq = 0x0A1C) : fs(f), e(f, first_seq) {}

  void boot(size_t bytes = kRing) { e.engine->scan(scratch(), bytes, e.sink); }

  // A meeting of exactly `segs` segments: 2,048 ms each (16 blocks of 4 KB), the last 12.
  std::vector<MonoId> record(int segs, uint32_t seed = 1) {
    const size_t before = e.sink.committed().size();
    e.engine->set_seg_ms(2048);
    uint32_t now = 1000 + seed;
    e.engine->start(meeting_spec(), now, now, e.sink);
    feed(e, 16 * (segs - 1) + 12, now, seed);
    e.engine->stop(e.sink);
    std::vector<MonoId> out;
    const std::vector<MonoId> all = e.sink.committed();
    for (size_t i = before; i < all.size(); ++i) {
      out.push_back(all[i]);
      e.index.insert(all[i], static_cast<uint32_t>(fs.get(wav_of(all[i])).size()), 0, i == before);
    }
    return out;
  }

  // file.del as the loop posts it; the index follows the reply as runtime_files.cpp does.
  RecEvt deliver(const MonoId& id, uint32_t host, uint32_t wall = kWall) {
    RecCmd c;
    c.kind = CmdKind::Del;
    c.fid = id;
    c.size = e.index.find(id) ? e.index.find(id)->size : 0;
    c.sha256 = read_meta(fs, id).value.sha256;
    c.ledger.host = host;
    c.ledger.wall_s = wall;
    e.sink.events.clear();
    e.engine->del(c, e.sink);
    const RecEvt* d = e.sink.last(EvtKind::DelDone);
    if (d && d->code == ErrCode::None) e.index.remove(id);
    return d ? *d : RecEvt{};
  }

  RecEvt ledger(LedgerAsk ask, uint32_t host = 0, uint32_t wall = 0, uint16_t room = 256) {
    RecCmd c;
    c.kind = CmdKind::Ledger;
    c.ledger.ask = ask;
    c.ledger.host = host;
    c.ledger.wall_s = wall;
    c.ledger.room = room;
    return run(c);
  }

  RecEvt hold_from(const MonoId& lo, uint32_t host) {
    RecCmd c;
    c.kind = CmdKind::Ledger;
    c.ledger.ask = LedgerAsk::Hold;
    c.ledger.lo = lo;
    c.ledger.host = host;
    return run(c);
  }

  RecEvt run(const RecCmd& c) {
    e.sink.events.clear();
    e.engine->ledger(c, scratch(), kRing, e.sink);
    for (const RecEvt& r : e.sink.events) {
      if (r.kind == EvtKind::Requeued) e.index.insert(r.id, r.size, r.host, r.first);
    }
    const RecEvt* d = e.sink.last(EvtKind::LedgerDone);
    return d ? *d : RecEvt{};
  }

  KeptInfo kept() const {
    const RecEvt* k = e.sink.last(EvtKind::Kept);
    return k ? k->info : KeptInfo{};
  }
};

enum class Where : uint8_t { Waiting, Kept, Gone, Broken };

// After a reboot (a fresh engine scanning the card): where each segment is.
struct Seen {
  std::vector<Where> where;
  uint16_t kept = 0;
  uint16_t waiting = 0;
  bool incomplete = false;
  std::vector<uint32_t> excl;  // the Mac each waiting one is never for (0: none)
};

inline Seen look(FakeRecFilePort& fs, const std::vector<MonoId>& ids) {
  fs.reboot();
  Dev d(fs, 0x0F000);
  d.boot();
  Seen s;
  s.kept = d.e.engine->kept();
  s.waiting = d.e.index.count();
  const RecEvt* ready = d.e.sink.last(EvtKind::IndexReady);
  s.incomplete = ready && ready->kept;
  for (const MonoId& id : ids) {
    const bool files = fs.has(wav_of(id)) || fs.has(meta_of(id));
    const RecIndex::Entry* x = d.e.index.find(id);
    s.where.push_back(x ? Where::Waiting : (files ? Where::Kept : Where::Gone));
    s.excl.push_back(x ? d.e.index.host_at(x->excl) : 0);
    if (!x && fs.has(wav_of(id)) != fs.has(meta_of(id))) s.where.back() = Where::Broken;
  }
  return s;
}

}  // namespace ledgertest
