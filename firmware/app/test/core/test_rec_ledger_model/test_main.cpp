// SENT.L against a model (docs/28 §3.3-§3.5, S1a review): what test_rec_ledger_cuts does not
// look for. Its oracle is "nothing lost"; this one is "nothing delivered twice and nothing to
// the wrong Mac": deliver again takes exactly the last batch (also after a compaction), and a
// segment kept for one Mac or cleared never comes back waiting. File calls here fail and the
// engine carries on (an SD card that keeps failing an unlink), which a power cut never shows.
#include <unity.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

#include "../test_rec_ledger/ledger_rig.h"

using namespace ledgertest;

void setUp() {}
void tearDown() {}

namespace {

// An unlink of `bad` fails `fails` times; with `flaky`, any unlink fails one time in `flaky`.
class StickyFs : public FakeRecFilePort {
 public:
  std::string bad;
  int fails = 0;
  uint32_t flaky = 0;
  uint32_t x = 1;
  FsErr unlink(const char* path) override {
    if (fails > 0 && bad == path) {
      --fails;
      return FsErr::Disk;
    }
    x = x * 1103515245u + 12345u;
    if (flaky && (x >> 16) % flaky == 0) return FsErr::Disk;
    return FakeRecFilePort::unlink(path);
  }
};

std::string torn(const std::string& l) { return l + std::string("\x01\x02\x03", 3); }

}  // namespace

// Review 2: the .W unlink fails in Clear and again in the compaction's finish_gones. The
// compacted file must still say the recording is gone: the next boot never lists it.
static void test_cleared_recording_comes_back_after_failed_unlink() {
  StickyFs fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> a = d.record(1, 1);
  d.deliver(a[0], kMacA);
  fs.bad = wav_of(a[0]);
  fs.fails = 2;
  d.ledger(LedgerAsk::Clear);
  const Seen s = look(fs, a);
  TEST_ASSERT_TRUE_MESSAGE(s.where[0] != Where::Waiting, "a cleared (already delivered) recording is waiting again");
}

// ... and a later segment of the same recording, kept after the GONE, stays kept.
static void test_gone_never_takes_a_segment_kept_after_it() {
  StickyFs fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> r = d.record(2, 1);
  d.deliver(r[0], kMacA);
  fs.bad = wav_of(r[0]);
  fs.fails = 2;
  d.ledger(LedgerAsk::Clear);            // r[0] gone (its files stay: the unlink fails)
  d.deliver(r[1], kMacA);                // r[1] kept after the GONE
  fs.put(kLedgerL, torn(fs.has(kLedgerL) ? fs.get(kLedgerL) : fs.get(kLedgerK)));
  fs.reboot();
  Dev b(fs, 0x0E000);
  b.boot();  // compacts
  const Seen s = look(fs, r);
  TEST_ASSERT_TRUE(s.where[0] != Where::Waiting);
  TEST_ASSERT_TRUE_MESSAGE(s.where[1] == Where::Kept, "the GONE took a segment kept after it");
  TEST_ASSERT_EQUAL_UINT16(1, s.kept);
}

// Review 1: after a compaction the last batch is still {z}: the delivery to B that ended
// the older run to A is written before it, and the GEN says where the batch starts.
static void test_batch_grows_after_compaction() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> x = d.record(1, 1);
  const MonoId mark = d.e.ids.peek().value;
  const std::vector<MonoId> y = d.record(1, 2);
  d.hold_from(mark, kMacA);  // plugged into A, Tab: y not for A
  d.deliver(x[0], kMacA);    // A takes x
  d.deliver(y[0], kMacB);    // B takes y
  const std::vector<MonoId> z = d.record(1, 3);
  d.deliver(z[0], kMacA);  // A takes z: the last batch is {z}
  TEST_ASSERT_EQUAL_UINT16(1, d.kept().batch_recs);
  fs.put(kLedgerL, torn(fs.get(kLedgerL)));  // the next boot compacts
  fs.reboot();
  Dev b(fs, 0x0E000);
  b.boot();
  TEST_ASSERT_TRUE(fs.has(kLedgerK));
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, b.kept().batch_recs, "batch grew after compaction");
  b.ledger(LedgerAsk::Again);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, b.e.sink.count(EvtKind::Requeued), "deliver again took x too");
}

// The delivery to B that separated the runs aged out (Tidy), then a compaction.
static void test_batch_grows_after_separator_gone() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> x = d.record(1, 1), y = d.record(1, 2), z = d.record(1, 3);
  d.deliver(x[0], kMacA, kWall);
  d.deliver(y[0], kMacB, kWall - 10 * kDay);
  d.deliver(z[0], kMacA, kWall);
  TEST_ASSERT_EQUAL_UINT16(1, d.kept().batch_recs);
  d.ledger(LedgerAsk::Tidy, 0, kWall + 3600);
  TEST_ASSERT_TRUE(!fs.has(wav_of(y[0])));
  fs.put(kLedgerL, torn(fs.get(kLedgerL)));
  fs.reboot();
  Dev b(fs, 0x0E000);
  b.boot();
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, b.kept().batch_recs, "batch grew after compaction");
  b.ledger(LedgerAsk::Again);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, b.e.sink.count(EvtKind::Requeued), "deliver again took x too");
}

// A compaction with no batch left (it was delivered again): a stale run never becomes one.
static void test_no_batch_after_compaction_stays_none() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> x = d.record(1, 1), y = d.record(1, 2);
  d.deliver(x[0], kMacA);
  d.deliver(y[0], kMacB);
  d.ledger(LedgerAsk::Again);  // y back, never for B: no batch
  TEST_ASSERT_EQUAL_UINT16(0, d.kept().batch_recs);
  fs.put(kLedgerL, torn(fs.get(kLedgerL)));
  fs.reboot();
  Dev b(fs, 0x0E000);
  b.boot();
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, b.kept().batch_recs, "x became the batch after a compaction");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedgerNo::Nothing), static_cast<int>(b.ledger(LedgerAsk::Again).no));
}

namespace {

struct Ev {
  MonoId id;
  uint32_t host;
};

struct Model {
  std::vector<Ev> evs;                    // confirmed DELIVs, in order
  std::map<std::string, uint32_t> kept;   // kept segment -> its Mac
  std::set<std::string> cleared;          // taken by a Clear: never waiting again
  int bad_batch = 0;
  int bad_back = 0;
  char first[160] = "";
};

std::string key(const MonoId& id) { return mono_format(id).c_str(); }

uint32_t g_x = 777;
uint32_t rnd(uint32_t n) {
  g_x = g_x * 1103515245u + 12345u;
  return (g_x >> 16) % n;
}

const uint32_t kMacs[3] = {kMacA, kMacB, 0xC0C0C0C0};

void deliver_one(Dev& d, Model& m) {
  const uint32_t mac = kMacs[rnd(3)];
  for (uint16_t i = 0; i < d.e.index.count(); ++i) {
    const RecIndex::Entry& en = d.e.index.at(i);
    if (!d.e.index.for_host(en, mac)) continue;
    const MonoId id = en.id;
    if (d.deliver(id, mac, kWall - rnd(12) * kDay).kept) {
      m.evs.push_back({id, mac});
      m.kept[key(id)] = mac;
    }
    return;
  }
}

// What the model says the last batch is: the trailing run to one Mac, still kept for it.
std::vector<std::string> model_batch(const Model& m, const FakeRecFilePort& fs) {
  std::vector<std::string> want;
  if (m.evs.empty()) return want;
  const uint32_t h = m.evs.back().host;
  size_t st = m.evs.size();
  while (st > 0 && m.evs[st - 1].host == h) --st;
  for (size_t i = st; i < m.evs.size(); ++i) {
    const std::string k = key(m.evs[i].id);
    const auto it = m.kept.find(k);
    if (it != m.kept.end() && it->second == h && fs.has(wav_of(m.evs[i].id))) want.push_back(k);
  }
  std::sort(want.begin(), want.end());
  want.erase(std::unique(want.begin(), want.end()), want.end());
  return want;
}

void again(Dev& d, Model& m, int run, int step) {
  const std::vector<std::string> want = model_batch(m, d.fs);
  const RecEvt r = d.ledger(LedgerAsk::Again);
  std::vector<std::string> got;
  for (const RecEvt& e : d.e.sink.events) {
    if (e.kind == EvtKind::Requeued) got.push_back(key(e.id));
  }
  std::sort(got.begin(), got.end());
  if (r.no == LedgerNo::None && got != want) {
    if (!m.bad_batch) snprintf(m.first, sizeof m.first, "run %d step %d: want %zu got %zu", run, step, want.size(), got.size());
    ++m.bad_batch;
  }
  for (const std::string& k : got) m.kept.erase(k);
}

void clear(Dev& d, Model& m) {
  const RecEvt r = d.ledger(LedgerAsk::Clear);
  if (r.no != LedgerNo::None || r.first) return;  // more to do or failed: the model waits
  for (const auto& k : m.kept) m.cleared.insert(k.first);
  m.kept.clear();
}

void step_once(Dev& d, StickyFs& fs, Model& m, int run, int step) {
  const uint32_t op = rnd(10);
  if (op <= 1) {
    d.record(1 + rnd(2), rnd(1000));
  } else if (op <= 3 && d.e.index.count() > 0) {
    deliver_one(d, m);
  } else if (op == 4) {
    again(d, m, run, step);
  } else if (op == 5 && d.e.ids.peek().has) {
    d.hold_from(d.e.ids.peek().value, kMacs[rnd(3)]);
  } else if (op == 6) {
    d.ledger(LedgerAsk::Tidy, 0, kWall + rnd(4) * kDay);
  } else if (op == 7) {
    const std::string l = fs.has(kLedgerL) ? kLedgerL : kLedgerK;
    if (fs.has(l)) fs.put(l, fs.get(l) + std::string("\x05\x06", 2));
  } else if (op == 8) {
    clear(d, m);
  }
}

// After a reboot: what aged out is gone from the model; nothing kept or cleared is waiting.
void check_after_boot(StickyFs& fs, Model& m) {
  for (auto it = m.kept.begin(); it != m.kept.end();) {
    bool there = false;
    for (const Ev& e : m.evs) {
      if (it->first == key(e.id)) there = fs.has(wav_of(e.id));
    }
    it = there ? std::next(it) : m.kept.erase(it);
  }
  const uint32_t flaky = fs.flaky;
  fs.flaky = 0;  // the checking boot sees a good card
  Dev chk(fs, 0x0F000);
  chk.boot();
  fs.flaky = flaky;
  for (const Ev& e : m.evs) {
    const std::string k = key(e.id);
    if ((m.kept.count(k) || m.cleared.count(k)) && chk.e.index.find(e.id)) ++m.bad_back;
  }
}

void run_model(int runs, uint32_t flaky, Model& m) {
  for (int run = 0; run < runs; ++run) {
    StickyFs fs;
    fs.flaky = flaky;
    fs.x = g_x;
    Model one;
    uint32_t seq = 0x0A1C;
    for (int step = 0; step < 30; ++step) {
      Dev d(fs, seq);
      d.boot();
      step_once(d, fs, one, run, step);
      seq = d.e.ids.peek().has ? d.e.ids.peek().value.seq + 1 : seq;
      fs.reboot();
      check_after_boot(fs, one);
      seq += 0x100;
    }
    if (!m.bad_batch && one.bad_batch) snprintf(m.first, sizeof m.first, "%s", one.first);
    m.bad_batch += one.bad_batch;
    m.bad_back += one.bad_back;
  }
}

int env_int(const char* name, int dflt) {
  const char* v = std::getenv(name);
  return v ? std::atoi(v) : dflt;
}

}  // namespace

// The model fuzz: random Macs, holds, Tidy at random clocks, Clear, torn tails (a compaction
// at the next boot) and reboots; then the same with an unlink failing now and then.
// CPA_LEDGER_MODEL_SEED / CPA_LEDGER_MODEL_RUNS for a longer run.
static void test_model_fuzz() {
  g_x = static_cast<uint32_t>(env_int("CPA_LEDGER_MODEL_SEED", 777));
  const int runs = env_int("CPA_LEDGER_MODEL_RUNS", 200);
  Model m;
  run_model(runs, 0, m);
  run_model(runs / 2, 7, m);
  char msg[256];
  snprintf(msg, sizeof msg, "bad_batch=%d bad_back=%d %s", m.bad_batch, m.bad_back, m.first);
  TEST_MESSAGE(msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, m.bad_batch + m.bad_back, msg);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cleared_recording_comes_back_after_failed_unlink);
  RUN_TEST(test_gone_never_takes_a_segment_kept_after_it);
  RUN_TEST(test_batch_grows_after_compaction);
  RUN_TEST(test_batch_grows_after_separator_gone);
  RUN_TEST(test_no_batch_after_compaction_stays_none);
  RUN_TEST(test_model_fuzz);
  return UNITY_END();
}
