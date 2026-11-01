// Builders and checks for the recovery rows and the power-cut runs (test_main.cpp).
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "rig.h"

namespace rectest {

using testsupport::FakeRecFilePort;
using testsupport::FakeSha256;

constexpr uint32_t kBlock = 4096;

inline StartSpec meeting_spec() {
  StartSpec s;
  s.mode = RecMode::Meeting;
  s.slot = 4;  // the device does not know slot 4's project
  s.consent = true;
  s.wall_offset_ms = some(int64_t{1792440000000LL});
  return s;
}

// Writes `blocks` 4 KB blocks of pcm(seed) at 128 ms steps from now; returns the bytes.
inline std::string feed(Engine& e, int blocks, uint32_t& now, uint32_t seed = 1) {
  std::string all;
  for (int i = 0; i < blocks && (i == 0 || e.engine->recording()); ++i) {
    const std::string b = pcm(kBlock, seed * 1000 + i);
    e.engine->write(u8(b), b.size(), now, e.sink);
    all += b;
    now += 128;
  }
  return all;
}

inline RecMeta committed_meta(const MonoId& id, uint32_t bytes, const Opt<MonoId>& next) {
  RecMeta m;
  m.id = id;
  m.rec = id;
  m.next = next;
  m.last = !next.has;
  m.mode = RecMode::Meeting;
  m.bytes = bytes;
  m.sha256 = some(FakeSha256::of(pcm(bytes, 7)));
  proto::RecAt at;
  at.up_ms = 1000;
  at.wall_ms = some(int64_t{1792440000000LL});
  m.at = some(at);
  m.consent = true;
  m.lead_ms = some(kLeadMs);
  m.fw = FixedStr<31>::from("cpa-fw/0.3.0");
  return m;
}

inline std::string wav_file(uint32_t bytes, uint32_t seed = 7) {
  const WavHeader h = wav_header(bytes);
  return std::string(reinterpret_cast<const char*>(h.b), 44) + pcm(bytes, seed);
}

inline std::string placeholder_file(RecMode mode, uint32_t bytes) {
  const WavHeader h = wav_placeholder(mode);
  return std::string(reinterpret_cast<const char*>(h.b), 44) + pcm(bytes, 9);
}

inline void put_committed(FakeRecFilePort& fs, const RecMeta& m) {
  fs.put(wav_of(m.id), wav_file(m.bytes));
  fs.put(meta_of(m.id), rec_meta_encode(m).c_str());
}

struct Outcome {
  std::map<std::string, std::string> files;
  std::vector<std::pair<MonoId, uint32_t>> index;
};

inline Outcome outcome_of(FakeRecFilePort& fs, const RecIndex& idx) {
  Outcome o;
  for (const auto& p : fs.paths()) o.files[p] = fs.get(p);
  for (uint16_t i = 0; i < idx.count(); ++i) o.index.push_back({idx.at(i).id, idx.at(i).size});
  return o;
}

// After a cut and a reboot: every committed segment is intact and indexed, every .W left
// on the card is indexed, recovered segments of the session are relinked to it.
inline void check_after_cut(FakeRecFilePort& fs, const Sink& before, const std::set<MonoId>& deleted, long k) {
  fs.reboot();
  Engine boot(fs, 0x200);
  boot.scan();
  const std::string at = "cut at " + std::to_string(k);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, fs.double_open, at.c_str());
  std::set<MonoId> sessions;
  for (const MonoId& id : before.committed()) {
    const Opt<RecMeta> m = read_meta(fs, id);
    if (deleted.count(id)) continue;
    TEST_ASSERT_TRUE_MESSAGE(m.has && !m.value.rcv && fs.has(wav_of(id)), at.c_str());
    TEST_ASSERT_TRUE_MESSAGE(boot.index.find(id) != nullptr, at.c_str());
    sessions.insert(m.value.rec);
  }
  for (const auto& p : fs.paths()) {
    if (p.find(".W") == std::string::npos) continue;
    const std::string name = p.substr(p.rfind('/') + 1);
    const Opt<RecName> n = rec_parse_name("3QZK1M7", name.c_str());
    TEST_ASSERT_TRUE_MESSAGE(n.has && boot.index.find(n.value.id), (at + " unindexed " + p).c_str());
    const RecMeta m = read_meta(fs, n.value.id).value;
    if (m.rcv && m.seg > 0) TEST_ASSERT_TRUE_MESSAGE(sessions.count(m.rec), (at + " relink").c_str());
  }
  // a cut during this recovery, and the next boot, ends in the same place
  const Outcome done = outcome_of(fs, boot.index);
  Engine again(fs, 0x300);
  again.scan();
  TEST_ASSERT_TRUE_MESSAGE(outcome_of(fs, again.index).files == done.files, at.c_str());
}

inline long run_meeting(FakeRecFilePort& fs, Sink& sink_out, long cut) {
  Engine e(fs);
  e.engine->set_seg_ms(1000);
  if (cut >= 0) fs.cut_at(fs.calls() + cut);
  const long start = fs.calls();
  uint32_t now = 0;
  e.engine->start(meeting_spec(), 0, now, e.sink);
  feed(e, 25, now);  // 3.2 segments
  e.engine->stop(e.sink);
  sink_out = e.sink;
  return fs.calls() - start;
}


}  // namespace rectest
