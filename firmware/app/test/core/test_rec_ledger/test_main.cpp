// The SENT.L ledger on the engine (docs/28 §3.3-§3.5, §9, S1a): a handed-over segment is
// kept, never listed again, and comes back only by "deliver again" (never for the Mac it
// went to); the boot scan skips the kept ones' .M; and a power cut at any call, or a write
// torn at any byte, leaves every segment exactly one of waiting / kept / gone.
#include <unity.h>

#include "ledger_rig.h"

using namespace ledgertest;

void setUp() {}
void tearDown() {}

namespace {

// Every segment with files is waiting or kept, and the counts agree (docs/28 §9 oracle).
void check_whole(FakeRecFilePort& fs, const std::vector<MonoId>& ids, const Seen& s, long k) {
  char msg[64];
  std::snprintf(msg, sizeof msg, "cut at %ld", k);
  uint16_t with_files = 0;
  for (size_t i = 0; i < ids.size(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(s.where[i] != Where::Broken, msg);
    with_files = static_cast<uint16_t>(with_files + (s.where[i] != Where::Gone ? 1 : 0));
  }
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(with_files, s.kept + s.waiting, msg);
  TEST_ASSERT_FALSE_MESSAGE(s.incomplete, msg);
  (void)fs;
}

std::vector<MonoId> all_of(const std::vector<std::vector<MonoId>>& recs) {
  std::vector<MonoId> out;
  for (const auto& r : recs) out.insert(out.end(), r.begin(), r.end());
  return out;
}

}  // namespace

static void test_delivery_keeps_the_files_and_appends_one_deliv() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> segs = d.record(2);
  const RecEvt done = d.deliver(segs[0], kMacA);
  TEST_ASSERT_TRUE(done.code == ErrCode::None);
  TEST_ASSERT_TRUE(done.kept);
  TEST_ASSERT_TRUE(fs.has(wav_of(segs[0])) && fs.has(meta_of(segs[0])));
  TEST_ASSERT_EQUAL_UINT(kLedgerRecBytes, fs.get(kLedgerL).size());
  TEST_ASSERT_EQUAL_UINT16(1, d.kept().segs);
  TEST_ASSERT_EQUAL_HEX32(kMacA, d.kept().batch_host);
  TEST_ASSERT_EQUAL_UINT32(kWall, d.kept().oldest_s);
  const Seen s = look(fs, segs);
  TEST_ASSERT_TRUE(s.where[0] == Where::Kept && s.where[1] == Where::Waiting);
  TEST_ASSERT_EQUAL_UINT16(1, s.kept);
}

// §3.4: a kept name costs the scan no .M read (a waiting one still has its .M read).
static void test_boot_scan_skips_the_kept_ones_meta() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> segs = d.record(6);
  for (int i = 0; i < 5; ++i) d.deliver(segs[i], kMacA);
  fs.reboot();
  fs.read_opens.clear();
  Dev after(fs);
  after.boot();
  TEST_ASSERT_EQUAL_UINT16(5, after.e.engine->kept());
  TEST_ASSERT_EQUAL_UINT16(1, after.e.index.count());
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_INT_MESSAGE(0, fs.read_opens[meta_of(segs[i])], "a kept .M was read");
  TEST_ASSERT_TRUE(fs.read_opens[meta_of(segs[5])] > 0);
}

static void test_power_cut_at_every_call_of_a_keeping_delivery() {
  FakeRecFilePort base;
  Dev d0(base);
  d0.boot();
  const std::vector<MonoId> segs = d0.record(3);
  d0.deliver(segs[0], kMacA);
  for (long k = 0; k < 12; ++k) {
    FakeRecFilePort fs = base;
    Dev d(fs);
    d.boot();
    fs.cut_at(fs.calls() + k);
    d.deliver(segs[1], kMacA);
    const Seen s = look(fs, segs);
    check_whole(fs, segs, s, k);
    TEST_ASSERT_TRUE(s.where[0] == Where::Kept);
    TEST_ASSERT_TRUE(s.where[1] == Where::Waiting || s.where[1] == Where::Kept);
    TEST_ASSERT_TRUE(s.where[2] == Where::Waiting);
  }
}

// §9: a DELIV torn at every byte: lost (the segment waits again, delivered twice at worst),
// the next boot compacts the ledger clean, and the records after it read back.
static void test_torn_delivery_at_every_byte() {
  FakeRecFilePort base;
  Dev d0(base);
  d0.boot();
  const std::vector<MonoId> segs = d0.record(3);
  d0.deliver(segs[0], kMacA);
  for (size_t keep = 0; keep < kLedgerRecBytes; ++keep) {
    FakeRecFilePort fs = base;
    Dev d(fs);
    d.boot();
    fs.tear_next_write(keep);
    const RecEvt r = d.deliver(segs[1], kMacA);
    TEST_ASSERT_TRUE(r.code != ErrCode::None);  // no ack: the Mac asks again
    fs.reboot();
    Dev again(fs, 0x0E000);
    again.boot();  // compacts the dirty file
    TEST_ASSERT_EQUAL_UINT16(1, again.e.engine->kept());
    TEST_ASSERT_TRUE(again.e.index.find(segs[1]) != nullptr);
    TEST_ASSERT_TRUE(again.deliver(segs[1], kMacB).kept);
    const Seen s = look(fs, segs);
    TEST_ASSERT_TRUE(s.where[0] == Where::Kept && s.where[1] == Where::Kept && s.where[2] == Where::Waiting);
    const std::string live = fs.has(kLedgerK) ? fs.get(kLedgerK) : fs.get(kLedgerL);
    TEST_ASSERT_EQUAL_UINT(0, live.size() % kLedgerRecBytes);
  }
}

static void test_deliver_again_never_for_that_mac() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> a = d.record(2, 1), b = d.record(1, 2);
  for (const MonoId& id : all_of({a, b})) d.deliver(id, kMacA);
  TEST_ASSERT_EQUAL_UINT16(2, d.kept().batch_recs);
  const RecEvt done = d.ledger(LedgerAsk::Again, 0, 0, 256);
  TEST_ASSERT_TRUE(done.no == LedgerNo::None);
  TEST_ASSERT_EQUAL_UINT16(2, done.n);
  TEST_ASSERT_EQUAL_UINT(3, d.e.sink.count(EvtKind::Requeued));
  TEST_ASSERT_EQUAL_UINT16(3, d.e.index.count());
  TEST_ASSERT_FALSE(d.e.index.for_host(*d.e.index.find(a[0]), kMacA));
  TEST_ASSERT_TRUE(d.e.index.for_host(*d.e.index.find(a[0]), kMacB));
  TEST_ASSERT_TRUE(d.e.index.find(a[0])->first && !d.e.index.find(a[1])->first);
  TEST_ASSERT_EQUAL_UINT16(0, d.kept().segs);
  const Seen s = look(fs, all_of({a, b}));
  for (size_t i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_HEX32(kMacA, s.excl[i]);
  // the other Mac takes them: kept for it, and its batch now
  for (const MonoId& id : all_of({a, b})) TEST_ASSERT_TRUE(d.deliver(id, kMacB).kept);
  TEST_ASSERT_EQUAL_HEX32(kMacB, d.kept().batch_host);
}

static void test_deliver_again_refusals() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  TEST_ASSERT_TRUE(d.ledger(LedgerAsk::Again).no == LedgerNo::Nothing);
  const std::vector<MonoId> a = d.record(3);
  for (const MonoId& id : a) d.deliver(id, kMacA);
  TEST_ASSERT_TRUE(d.ledger(LedgerAsk::Again, 0, 0, 2).no == LedgerNo::TooMany);
  fs.unlink(wav_of(a[1]).c_str());
  TEST_ASSERT_TRUE(d.ledger(LedgerAsk::Again).no == LedgerNo::Incomplete);
  TEST_ASSERT_EQUAL_UINT16(0, d.e.index.count());  // nothing requeued
}

static void test_power_cut_at_every_call_of_deliver_again() {
  FakeRecFilePort base;
  Dev d0(base);
  d0.boot();
  const std::vector<MonoId> a = d0.record(3);
  for (const MonoId& id : a) d0.deliver(id, kMacA);
  for (long k = 0; k < 30; ++k) {
    FakeRecFilePort fs = base;
    Dev d(fs);
    d.boot();
    fs.cut_at(fs.calls() + k);
    d.ledger(LedgerAsk::Again);
    const Seen s = look(fs, a);
    check_whole(fs, a, s, k);
    for (size_t i = 0; i < a.size(); ++i) {
      const bool kept = s.where[i] == Where::Kept;
      const bool back = s.where[i] == Where::Waiting && s.excl[i] == kMacA;
      TEST_ASSERT_TRUE_MESSAGE(kept || back, "waiting for the Mac it was taken from");
    }
  }
}

// Review 6: deliver again never fills the waiting list: kAgainSpare places stay for recording.
static void test_deliver_again_leaves_room_to_record() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> a = d.record(3);
  for (const MonoId& id : a) d.deliver(id, kMacA);
  const uint16_t room = static_cast<uint16_t>(3 + kAgainSpare);
  TEST_ASSERT_TRUE(d.ledger(LedgerAsk::Again, 0, 0, static_cast<uint16_t>(room - 1)).no == LedgerNo::TooMany);
  TEST_ASSERT_TRUE(d.ledger(LedgerAsk::Again, 0, 0, room).no == LedgerNo::None);
}

// Review 6: "give to this Mac" undoes deliver again (or the hint's "not here") for the Mac on
// the link: one AGAIN HOLD for every one, durable, and a compaction drops them.
static void test_give_back_to_this_mac() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> a = d.record(2, 1), b = d.record(1, 2);
  for (const MonoId& id : a) d.deliver(id, kMacA);
  d.ledger(LedgerAsk::Again);  // a: never for A
  d.deliver(b[0], kMacB);
  d.ledger(LedgerAsk::Again);  // b: never for B
  TEST_ASSERT_TRUE(d.ledger(LedgerAsk::Give, kMacB + 1).no == LedgerNo::Nothing);
  const RecEvt done = d.ledger(LedgerAsk::Give, kMacA);
  TEST_ASSERT_TRUE(done.no == LedgerNo::None);
  TEST_ASSERT_EQUAL_UINT16(2, done.n);
  Seen s = look(fs, all_of({a, b}));
  TEST_ASSERT_EQUAL_HEX32(0, s.excl[0]);
  TEST_ASSERT_EQUAL_HEX32(0, s.excl[1]);
  TEST_ASSERT_EQUAL_HEX32(kMacB, s.excl[2]);
  TEST_ASSERT_EQUAL_UINT16(3, s.waiting);
  fs.put(kLedgerL, fs.get(kLedgerL) + std::string("\x01", 1));  // compacted at the next boot
  s = look(fs, all_of({a, b}));
  TEST_ASSERT_EQUAL_HEX32(0, s.excl[0]);
  TEST_ASSERT_EQUAL_HEX32(kMacB, s.excl[2]);
  TEST_ASSERT_TRUE(fs.has(kLedgerK));
  TEST_ASSERT_EQUAL_size_t(2 * kLedgerRecBytes, fs.get(kLedgerK).size());  // GEN + b's AGAIN
}

// §3.2: the plug-in hint's "not to this Mac": what was recorded from the mark on.
static void test_hold_keeps_offline_ones_from_this_mac() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> online = d.record(1, 1);
  const MonoId mark = d.e.ids.peek().value;
  const std::vector<MonoId> offline = d.record(2, 2);
  const RecEvt done = d.hold_from(mark, kMacA);
  TEST_ASSERT_TRUE(done.no == LedgerNo::None);
  TEST_ASSERT_EQUAL_UINT16(2, done.n);
  const Seen s = look(fs, all_of({online, offline}));
  TEST_ASSERT_EQUAL_HEX32(0, s.excl[0]);
  TEST_ASSERT_EQUAL_HEX32(kMacA, s.excl[1]);
  TEST_ASSERT_EQUAL_HEX32(kMacA, s.excl[2]);
  // a hold never takes a kept one back
  TEST_ASSERT_TRUE(d.deliver(online[0], kMacB).kept);
  d.hold_from(online[0], kMacA);
  TEST_ASSERT_TRUE(look(fs, online).where[0] == Where::Kept);
}

static void test_clear_deletes_every_kept_copy() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> a = d.record(2, 1), b = d.record(1, 2), c = d.record(1, 3);
  for (const MonoId& id : all_of({a, b})) d.deliver(id, kMacA);
  const RecEvt done = d.ledger(LedgerAsk::Clear);
  TEST_ASSERT_TRUE(done.no == LedgerNo::None);
  TEST_ASSERT_EQUAL_UINT16(2, done.n);
  TEST_ASSERT_EQUAL_UINT16(0, d.kept().segs);
  const Seen s = look(fs, all_of({a, b, c}));
  for (size_t i = 0; i < 3; ++i) TEST_ASSERT_TRUE(s.where[i] == Where::Gone);
  TEST_ASSERT_TRUE(s.where[3] == Where::Waiting);
  const std::string live = fs.has(kLedgerK) ? fs.get(kLedgerK) : fs.get(kLedgerL);
  TEST_ASSERT_EQUAL_UINT(kLedgerRecBytes, live.size());  // compacted: its GEN alone
}

// A Start is never stuck behind a whole card of deletes: kDropMax recordings a command.
static void test_clear_goes_a_few_recordings_a_command() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  std::vector<MonoId> all;
  for (uint32_t i = 0; i < kDropMax + 2; ++i) {
    const std::vector<MonoId> r = d.record(1, 10 + i);
    d.deliver(r[0], kMacA);
    all.push_back(r[0]);
  }
  const RecEvt first = d.ledger(LedgerAsk::Clear);
  TEST_ASSERT_EQUAL_UINT16(kDropMax, first.n);
  TEST_ASSERT_TRUE(first.first);  // more to do
  TEST_ASSERT_EQUAL_UINT16(2, d.kept().segs);
  const RecEvt second = d.ledger(LedgerAsk::Clear);
  TEST_ASSERT_EQUAL_UINT16(2, second.n);
  TEST_ASSERT_FALSE(second.first);
  const Seen s = look(fs, all);
  for (const Where w : s.where) TEST_ASSERT_TRUE(w == Where::Gone);
  TEST_ASSERT_EQUAL_UINT(kLedgerRecBytes, (fs.has(kLedgerK) ? fs.get(kLedgerK) : fs.get(kLedgerL)).size());
}

static void test_power_cut_at_every_call_of_clear() {
  FakeRecFilePort base;
  Dev d0(base);
  d0.boot();
  const std::vector<MonoId> a = d0.record(2, 1), b = d0.record(2, 2);
  for (const MonoId& id : all_of({a, b})) d0.deliver(id, kMacA);
  const std::vector<MonoId> ids = all_of({a, b});
  for (long k = 0; k < 40; ++k) {
    FakeRecFilePort fs = base;
    Dev d(fs);
    d.boot();
    fs.cut_at(fs.calls() + k);
    d.ledger(LedgerAsk::Clear);
    const Seen s = look(fs, ids);
    check_whole(fs, ids, s, k);
    for (size_t i = 0; i < ids.size(); ++i) TEST_ASSERT_TRUE(s.where[i] != Where::Waiting);
    // a recording goes whole: both its segments, or neither
    TEST_ASSERT_TRUE(s.where[0] == s.where[1] && s.where[2] == s.where[3]);
  }
}

static void test_tidy_by_age_only_with_a_trusted_clock() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> old = d.record(1, 1), recent = d.record(1, 2);
  d.deliver(old[0], kMacA, kWall - 8 * kDay);
  d.deliver(recent[0], kMacA, kWall - 1 * kDay);
  TEST_ASSERT_EQUAL_UINT16(0, d.ledger(LedgerAsk::Tidy, 0, 0).n);  // no clock: nothing ages
  TEST_ASSERT_EQUAL_UINT16(1, d.ledger(LedgerAsk::Tidy, 0, kWall).n);
  const Seen s = look(fs, all_of({old, recent}));
  TEST_ASSERT_TRUE(s.where[0] == Where::Gone && s.where[1] == Where::Kept);
}

static void test_tidy_by_space_takes_the_oldest() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const std::vector<MonoId> a = d.record(1, 1), b = d.record(1, 2);
  d.deliver(a[0], kMacA, 0);
  d.deliver(b[0], kMacA, 0);
  RecCmd c;
  c.kind = CmdKind::Ledger;
  c.ledger.ask = LedgerAsk::Tidy;
  c.ledger.free_known = true;
  c.ledger.free_bytes = kKeptFreeFloor - 40000;  // one segment's .W frees it
  const RecEvt done = d.run(c);
  TEST_ASSERT_EQUAL_UINT16(1, done.n);
  const Seen s = look(fs, all_of({a, b}));
  TEST_ASSERT_TRUE(s.where[0] == Where::Gone && s.where[1] == Where::Kept);
}

// No room for kept copies (a scan table under 272 names) or too long a recording: deleted
// as before S1a.
static void test_no_room_deletes_as_before() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot(16 * 200);
  const std::vector<MonoId> a = d.record(1);
  const RecEvt r = d.deliver(a[0], kMacA);
  TEST_ASSERT_TRUE(r.code == ErrCode::None && !r.kept);
  TEST_ASSERT_FALSE(fs.has(wav_of(a[0])));
  TEST_ASSERT_FALSE(fs.has(kLedgerL));
}

static void test_scan_overflow_says_incomplete() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  d.record(4);
  fs.reboot();
  Dev small(fs);
  small.boot(16 * 3);
  const RecEvt* ready = small.e.sink.last(EvtKind::IndexReady);
  TEST_ASSERT_TRUE(ready && ready->kept);
}

// Review 7: the boot scan's four Mac slots: a fifth Mac takes over the oldest slot, and a
// name that pointed at it is then for every Mac (as in the index), never kept from the fifth.
static void test_fifth_mac_at_boot_never_excludes_the_wrong_mac() {
  FakeRecFilePort fs;
  Dev d(fs);
  d.boot();
  const uint32_t macs[5] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555};
  std::vector<MonoId> ids;
  for (uint32_t m : macs) {
    const std::vector<MonoId> r = d.record(1, m & 0xFF);
    d.deliver(r[0], m);
    d.ledger(LedgerAsk::Again);  // back, never for m
    ids.push_back(r[0]);
  }
  const Seen s = look(fs, ids);
  TEST_ASSERT_TRUE(s.where[0] == Where::Waiting);
  TEST_ASSERT_TRUE_MESSAGE(s.excl[0] != macs[4], "kept from the fifth Mac, not the first");
  TEST_ASSERT_EQUAL_HEX32(macs[4], s.excl[4]);
  TEST_ASSERT_EQUAL_HEX32(macs[1], s.excl[1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_delivery_keeps_the_files_and_appends_one_deliv);
  RUN_TEST(test_boot_scan_skips_the_kept_ones_meta);
  RUN_TEST(test_power_cut_at_every_call_of_a_keeping_delivery);
  RUN_TEST(test_torn_delivery_at_every_byte);
  RUN_TEST(test_deliver_again_never_for_that_mac);
  RUN_TEST(test_deliver_again_refusals);
  RUN_TEST(test_power_cut_at_every_call_of_deliver_again);
  RUN_TEST(test_deliver_again_leaves_room_to_record);
  RUN_TEST(test_give_back_to_this_mac);
  RUN_TEST(test_hold_keeps_offline_ones_from_this_mac);
  RUN_TEST(test_clear_deletes_every_kept_copy);
  RUN_TEST(test_clear_goes_a_few_recordings_a_command);
  RUN_TEST(test_power_cut_at_every_call_of_clear);
  RUN_TEST(test_tidy_by_age_only_with_a_trusted_clock);
  RUN_TEST(test_tidy_by_space_takes_the_oldest);
  RUN_TEST(test_no_room_deletes_as_before);
  RUN_TEST(test_scan_overflow_says_incomplete);
  RUN_TEST(test_fifth_mac_at_boot_never_excludes_the_wrong_mac);
  return UNITY_END();
}
