// The SENT.L power-cut harness (docs/28 §9, S1a, on the host while the device is away): the
// ledger cut at every byte and every record boundary, a CRC failure in each record, a cut at
// every call of a boot's compaction, and a fuzz of every command with random cuts. The oracle:
// every segment with files is waiting or kept (the counts agree), none is half deleted, and a
// segment goes only when a Clear or Tidy took it: a lost record means "delivered twice" or "a
// change that visibly did not happen", never a lost recording.
#include <unity.h>

#include <cstdlib>
#include <map>
#include <set>

#include "../test_rec_ledger/ledger_rig.h"

using namespace ledgertest;

void setUp() {}
void tearDown() {}

namespace {

struct World {
  FakeRecFilePort fs;
  std::vector<MonoId> ids;
};

// Five recordings: two kept for A, one taken back from A (waiting, never for A), one kept
// for B, one waiting.
World build() {
  World w;
  Dev d(w.fs);
  d.boot();
  const std::vector<MonoId> r1 = d.record(2, 1), r2 = d.record(1, 2), r3 = d.record(1, 3);
  for (const MonoId& id : r1) d.deliver(id, kMacA);
  d.deliver(r2[0], kMacA);
  d.deliver(r3[0], kMacA);
  d.ledger(LedgerAsk::Again);  // r1, r2, r3 back, never for A
  d.deliver(r1[0], kMacB);
  d.deliver(r1[1], kMacB);
  d.deliver(r2[0], kMacB);
  const std::vector<MonoId> r4 = d.record(1, 4);
  for (const auto* r : {&r1, &r2, &r3, &r4}) w.ids.insert(w.ids.end(), r->begin(), r->end());
  return w;
}

// Every segment on the card (a recording cut by the power has an orphan the scan recovered).
uint16_t segments_on(const FakeRecFilePort& fs) {
  std::set<std::string> stems;
  for (const std::string& p : fs.paths()) {
    const size_t dot = p.rfind('.');
    if (dot != std::string::npos && p.size() == dot + 3 && (p[dot + 1] == 'W' || p[dot + 1] == 'M')) {
      stems.insert(p.substr(0, dot) + p.back());
    }
  }
  return static_cast<uint16_t>(stems.size());
}

std::string ledger_of(const FakeRecFilePort& fs) { return fs.has(kLedgerK) ? fs.get(kLedgerK) : fs.get(kLedgerL); }

void check(const Seen& s, size_t n, const char* msg) {
  uint16_t with_files = 0;
  for (size_t i = 0; i < n; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(s.where[i] != Where::Broken, msg);
    TEST_ASSERT_TRUE_MESSAGE(s.where[i] != Where::Gone, msg);  // nothing here deletes
    with_files = static_cast<uint16_t>(with_files + 1);
  }
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(with_files, s.kept + s.waiting, msg);
}

}  // namespace

// Every prefix of the ledger: the records wholly in it count, the cut one is lost.
static void test_ledger_cut_at_every_byte() {
  World w = build();
  const std::string full = ledger_of(w.fs);
  const size_t records = full.size() / kLedgerRecBytes;
  TEST_ASSERT_TRUE(records >= 10);
  for (size_t cut = 0; cut <= full.size(); ++cut) {
    FakeRecFilePort fs = w.fs;
    fs.unlink(kLedgerK.c_str());
    fs.put(kLedgerL, full.substr(0, cut));
    char msg[48];
    std::snprintf(msg, sizeof msg, "cut at byte %zu", cut);
    const Seen s = look(fs, w.ids);
    check(s, w.ids.size(), msg);
    if (cut == full.size()) {
      TEST_ASSERT_EQUAL_UINT16(3, s.kept);
      TEST_ASSERT_EQUAL_HEX32(kMacA, s.excl[3]);  // r3: taken back from A, never re-delivered
    }
    // the next boot left a clean file: whole records only
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, ledger_of(fs).size() % kLedgerRecBytes, msg);
  }
}

// A record whose CRC fails costs that record only: the ones after it still count.
static void test_crc_failure_in_each_record() {
  World w = build();
  const std::string full = ledger_of(w.fs);
  FakeRecFilePort clean = w.fs;
  const Seen want = look(clean, w.ids);
  for (size_t r = 0; r < full.size() / kLedgerRecBytes; ++r) {
    FakeRecFilePort fs = w.fs;
    std::string bad = full;
    bad[r * kLedgerRecBytes + 9] ^= 0x5A;
    fs.unlink(kLedgerK.c_str());
    fs.put(kLedgerL, bad);
    char msg[48];
    std::snprintf(msg, sizeof msg, "bad record %zu", r);
    const Seen s = look(fs, w.ids);
    check(s, w.ids.size(), msg);
    size_t differ = 0;
    for (size_t i = 0; i < w.ids.size(); ++i) differ += s.where[i] != want.where[i] || s.excl[i] != want.excl[i];
    TEST_ASSERT_TRUE_MESSAGE(differ <= 1, msg);  // one record: one segment at most
  }
}

// A boot that compacts a dirty ledger, cut at every call: the next boot sees the same.
static void test_cut_at_every_call_of_a_boot_compaction() {
  World w = build();
  std::string dirty = ledger_of(w.fs);
  dirty.insert(3 * kLedgerRecBytes, "\xA5garbage-not-a-record");
  w.fs.unlink(kLedgerK.c_str());
  w.fs.put(kLedgerL, dirty);
  FakeRecFilePort ref = w.fs;
  const Seen want = look(ref, w.ids);
  for (long k = 0; k < 60; ++k) {
    FakeRecFilePort fs = w.fs;
    Dev d(fs);
    fs.cut_at(fs.calls() + k);
    d.boot();
    char msg[48];
    std::snprintf(msg, sizeof msg, "cut at %ld", k);
    const Seen s = look(fs, w.ids);
    check(s, w.ids.size(), msg);
    for (size_t i = 0; i < w.ids.size(); ++i) {
      TEST_ASSERT_TRUE_MESSAGE(s.where[i] == want.where[i] && s.excl[i] == want.excl[i], msg);
    }
  }
}

// Both names after a cut between a compaction's fsync and its unlink: the older is used.
static void test_both_names_use_the_older() {
  World w = build();
  FakeRecFilePort ref = w.fs;
  const Seen want = look(ref, w.ids);  // compacts nothing: clean
  FakeRecFilePort fs = w.fs;
  std::string gen(kLedgerRecBytes, '\0');
  LedgerRec g;
  g.op = LedgerOp::Gen;
  g.seq = 1;
  ledger_encode(g, reinterpret_cast<uint8_t*>(&gen[0]), crc32_ieee);
  fs.put(kLedgerK, gen);  // a newer file cut short after its GEN
  const Seen s = look(fs, w.ids);
  for (size_t i = 0; i < w.ids.size(); ++i) TEST_ASSERT_TRUE(s.where[i] == want.where[i]);
  TEST_ASSERT_FALSE(fs.has(kLedgerK) && fs.has(kLedgerL));
}

// The fuzz: random commands, a random power cut now and then, a reboot after each cut.
static void test_fuzz_commands_and_cuts() {
  const char* seed = std::getenv("CPA_LEDGER_FUZZ_SEED");
  uint32_t x = seed ? static_cast<uint32_t>(std::atoi(seed)) : 12345;
  auto rnd = [&x](uint32_t n) {
    x = x * 1103515245u + 12345u;
    return (x >> 16) % n;
  };
  const char* env = std::getenv("CPA_LEDGER_FUZZ_RUNS");  // a longer run: e.g. 500
  const int runs = env ? std::atoi(env) : 12;
  for (int run = 0; run < runs; ++run) {
    FakeRecFilePort fs;
    std::vector<MonoId> ids;
    std::map<std::string, bool> may_go;  // taken by a Clear / Tidy once kept
    uint32_t seq = 0x0A1C;
    for (int step = 0; step < 25; ++step) {
      Dev d(fs, seq);
      d.boot();
      if (rnd(4) == 0) fs.cut_at(fs.calls() + rnd(40));
      const uint32_t op = rnd(7);
      if (op <= 1) {
        const std::vector<MonoId> r = d.record(1 + rnd(2), rnd(1000));
        ids.insert(ids.end(), r.begin(), r.end());
      } else if (op == 2 && d.e.index.count() > 0) {
        d.deliver(d.e.index.at(0).id, rnd(2) ? kMacA : kMacB);
      } else if (op == 3) {
        d.ledger(LedgerAsk::Again);
      } else if (op == 4 && !ids.empty()) {
        d.hold_from(ids[rnd(static_cast<uint32_t>(ids.size()))], kMacB);
      } else if (op >= 5) {
        for (const MonoId& id : ids) {
          if (fs.has(wav_of(id)) && !d.e.index.find(id)) may_go[mono_format(id).c_str()] = true;
        }
        d.ledger(op == 5 ? LedgerAsk::Clear : LedgerAsk::Tidy, 0, kWall + 30 * kDay);
      }
      seq = d.e.ids.peek().has ? d.e.ids.peek().value.seq + 1 : seq;
      fs.reboot();
      const Seen s = look(fs, ids);
      for (size_t i = 0; i < ids.size(); ++i) {
        TEST_ASSERT_TRUE(s.where[i] != Where::Broken);
        if (s.where[i] == Where::Gone) TEST_ASSERT_TRUE_MESSAGE(may_go[mono_format(ids[i]).c_str()], "lost");
      }
      TEST_ASSERT_EQUAL_UINT16(segments_on(fs), s.kept + s.waiting);
      seq += 0x100;
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ledger_cut_at_every_byte);
  RUN_TEST(test_crc_failure_in_each_record);
  RUN_TEST(test_cut_at_every_call_of_a_boot_compaction);
  RUN_TEST(test_both_names_use_the_older);
  RUN_TEST(test_fuzz_commands_and_cuts);
  return UNITY_END();
}
