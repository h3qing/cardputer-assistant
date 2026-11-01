// Recording segments on the card (docs/11 §4.5-§4.6): RecWriter's open / write / rotate /
// finish order, the .M commit, and boot recovery after a power cut at every single file
// call of a rotation, a finish, a delete and a recovery. Invariants checked each time:
//   - no committed segment is lost (its .W and .M survive, it is indexed again);
//   - a .W with a valid .M is never removed without file.del;
//   - an uncommitted segment comes back as rcv and is relinked to its session via next;
//   - a .M whose id is not its file name's is recovered, never trusted;
//   - the audio task never has more than one file open.
#include <unity.h>

#include "cuts.h"

using namespace rectest;
using testsupport::FakeRecFilePort;
using testsupport::FakeSha256;

void setUp() {}
void tearDown() {}

static StartSpec memo_spec() {
  StartSpec s;
  s.mode = RecMode::Memo;
  s.project = some(ProjectKey::from("cpa"));
  s.slot = 1;
  return s;
}

static void test_memo_writes_one_committed_segment() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 5000;
  e.engine->start(memo_spec(), 4990, now, e.sink);
  const RecEvt* started = e.sink.last(EvtKind::Started);
  TEST_ASSERT_NOT_NULL(started);
  const MonoId id = started->id;
  TEST_ASSERT_TRUE(id == seq_id(0x0A1C));
  const std::string data = feed(e, 10, now);
  e.engine->stop(e.sink);
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Saved));
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Committed));
  TEST_ASSERT_EQUAL_UINT32(44 + data.size(), e.sink.last(EvtKind::Committed)->size);
  const std::string w = fs.get(wav_of(id));
  TEST_ASSERT_EQUAL_UINT(44 + data.size(), w.size());
  TEST_ASSERT_TRUE(w.compare(0, 44, std::string(reinterpret_cast<const char*>(wav_header(data.size()).b), 44)) == 0);
  TEST_ASSERT_TRUE(w.substr(44) == data);
  const Opt<RecMeta> m = read_meta(fs, id);
  TEST_ASSERT_TRUE(m.has);
  TEST_ASSERT_TRUE(m.value.rec == id && m.value.seg == 0 && m.value.last && !m.value.next.has);
  TEST_ASSERT_TRUE(m.value.mode == RecMode::Memo && m.value.bytes == data.size());
  TEST_ASSERT_TRUE(m.value.sha256.has && m.value.sha256.value == FakeSha256::of(data));
  TEST_ASSERT_TRUE(m.value.at.has && m.value.at.value.up_ms == 4990 && !m.value.at.value.wall_ms.has);
  TEST_ASSERT_TRUE(m.value.project.has && m.value.project.value.equals("cpa"));
  TEST_ASSERT_TRUE(m.value.lead_ms.has && m.value.lead_ms.value == kLeadMs && !m.value.consent && !m.value.rcv);
  TEST_ASSERT_EQUAL_INT(0, fs.double_open);
  TEST_ASSERT_FALSE(fs.is_open());
  TEST_ASSERT_EQUAL_UINT16(0, e.index.count());  // only the scan writes the index
}

static void test_fsync_every_second() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  e.engine->start(memo_spec(), 0, now, e.sink);
  feed(e, 32, now);  // blocks at 0 .. 3968 ms: fsync at 1024, 2048 and 3072 ms
  TEST_ASSERT_EQUAL_INT(3, fs.fsyncs);
  e.engine->stop(e.sink);
}

static void test_rotation_links_segments_and_splits_blocks() {
  FakeRecFilePort fs;
  Engine e(fs);
  e.engine->set_seg_ms(1000);  // 32,000 B: blocks of 4 KB cross the boundary
  uint32_t now = 100;
  e.engine->start(meeting_spec(), 80, now, e.sink);
  const std::string data = feed(e, 20, now);
  e.engine->stop(e.sink);
  const std::vector<MonoId> segs = e.sink.committed();
  TEST_ASSERT_EQUAL_UINT(3, segs.size());
  TEST_ASSERT_EQUAL_UINT(2, e.sink.count(EvtKind::Segment));
  std::string joined;
  for (size_t i = 0; i < segs.size(); ++i) {
    const RecMeta m = read_meta(fs, segs[i]).value;
    TEST_ASSERT_TRUE(m.rec == segs[0] && m.seg == i && m.mode == RecMode::Meeting);
    TEST_ASSERT_EQUAL(i == 2, m.last);
    TEST_ASSERT_EQUAL(i < 2, m.next.has);
    if (i < 2) TEST_ASSERT_TRUE(m.next.value == segs[i + 1]);
    TEST_ASSERT_EQUAL_UINT32(80 + 1000 * i, m.at.value.up_ms);
    TEST_ASSERT_TRUE(m.at.value.wall_ms.value == static_cast<int64_t>(1792440000000LL + 80 + 1000 * i));
    TEST_ASSERT_EQUAL(i == 0, m.consent);
    TEST_ASSERT_EQUAL(i == 0, m.lead_ms.has);
    TEST_ASSERT_TRUE(!m.project.has && m.slot == 4);
    TEST_ASSERT_EQUAL_UINT32(i < 2 ? 32000 : data.size() - 64000, m.bytes);
    joined += fs.get(wav_of(segs[i])).substr(44);
    TEST_ASSERT_TRUE(m.sha256.value == FakeSha256::of(fs.get(wav_of(segs[i])).substr(44)));
  }
  TEST_ASSERT_TRUE(joined == data);  // seamless: every sample in order, none twice
  TEST_ASSERT_TRUE(segs[1].seq == segs[0].seq + 1 && segs[2].seq == segs[1].seq + 1);
  TEST_ASSERT_EQUAL_INT(0, fs.double_open);
}

static void test_stars_and_tags_are_per_segment() {
  FakeRecFilePort fs;
  Engine e(fs);
  e.engine->set_seg_ms(1000);
  uint32_t now = 0;
  e.engine->start(meeting_spec(), 0, now, e.sink);
  feed(e, 2, now);  // 8192 B = 256 ms
  TEST_ASSERT_TRUE(e.engine->mark());
  for (int i = 1; i < 16; ++i) TEST_ASSERT_TRUE(e.engine->mark());
  TEST_ASSERT_FALSE(e.engine->mark());  // 16 per segment
  e.engine->tag(some(ProjectKey::from("web")), 2);
  feed(e, 8, now);  // into segment 1
  TEST_ASSERT_TRUE(e.engine->mark());
  e.engine->stop(e.sink);
  const std::vector<MonoId> segs = e.sink.committed();
  const RecMeta m0 = read_meta(fs, segs[0]).value;
  TEST_ASSERT_EQUAL_UINT8(16, m0.n_stars);
  TEST_ASSERT_EQUAL_UINT32(256, m0.stars[0]);
  TEST_ASSERT_TRUE(m0.project.has && m0.project.value.equals("web"));  // the tag at commit time
  const RecMeta m1 = read_meta(fs, segs[1]).value;
  TEST_ASSERT_EQUAL_UINT8(1, m1.n_stars);
  TEST_ASSERT_EQUAL_UINT32(10 * 128 - 1000, m1.stars[0]);
}

static void test_memo_cap_is_exactly_one_segment() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  e.engine->start(memo_spec(), 0, now, e.sink);
  int blocks = 0;
  while (e.engine->recording() && blocks < 3000) {
    const std::string b(kBlock, static_cast<char>(blocks));
    e.engine->write(u8(b), b.size(), now, e.sink);
    now += 128;
    ++blocks;
  }
  TEST_ASSERT_FALSE(e.engine->recording());
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Committed));
  TEST_ASSERT_TRUE(e.sink.last(EvtKind::Saved)->auto_stop);
  TEST_ASSERT_EQUAL_UINT32(44 + 9600000, e.sink.last(EvtKind::Committed)->size);
  TEST_ASSERT_EQUAL_UINT32(9600000, read_meta(fs, e.sink.committed()[0]).value.bytes);
  e.engine->stop(e.sink);  // a late Stop does nothing
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Saved));
}

static void test_short_session_is_discarded() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  e.engine->start(memo_spec(), 0, now, e.sink);
  feed(e, 7, now);  // 28,672 B < 1 s
  e.engine->stop(e.sink);
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Discarded));
  TEST_ASSERT_EQUAL_UINT(0, e.sink.count(EvtKind::Committed));
  TEST_ASSERT_EQUAL_UINT(0, fs.file_count());
}

static void test_full_card_still_commits_the_segment() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  e.engine->start(memo_spec(), 0, now, e.sink);
  fs.full_after(44 + 10 * kBlock);
  feed(e, 20, now);
  TEST_ASSERT_FALSE(e.engine->recording());
  const RecEvt* f = e.sink.last(EvtKind::Failed);
  TEST_ASSERT_TRUE(f && f->fail == RecFail::Full);
  TEST_ASSERT_EQUAL_UINT(1, e.sink.count(EvtKind::Committed));
  TEST_ASSERT_EQUAL_UINT32(10 * kBlock, read_meta(fs, e.sink.committed()[0]).value.bytes);
}

static void test_disk_error_stops_and_recovery_keeps_the_audio() {
  FakeRecFilePort fs;
  Engine e(fs);
  uint32_t now = 0;
  e.engine->start(meeting_spec(), 0, now, e.sink);
  feed(e, 40, now);  // fsynced after 1 s
  fs.fail_next(FsErr::Disk);
  feed(e, 1, now);
  const RecEvt* f = e.sink.last(EvtKind::Failed);
  TEST_ASSERT_TRUE(f && f->fail == RecFail::Disk);
  TEST_ASSERT_FALSE(e.engine->recording());
  TEST_ASSERT_EQUAL_UINT(0, e.sink.count(EvtKind::Committed));
  fs.reboot();
  Engine boot(fs, 0x100);
  boot.scan();
  TEST_ASSERT_EQUAL_UINT16(1, boot.index.count());
  const RecMeta m = read_meta(fs, boot.index.at(0).id).value;
  TEST_ASSERT_TRUE(m.rcv && m.last && !m.sha256.has && m.seg == 0 && m.mode == RecMode::Meeting && m.consent);
  TEST_ASSERT_TRUE(m.bytes > 0 && m.bytes % kBlock == 0);
  TEST_ASSERT_FALSE(m.at.has);  // no earlier segment to derive it from
}

// ---- the rows of §4.6, one at a time ---------------------------------------------------

static void test_rows_committed_half_deleted_and_too_short() {
  FakeRecFilePort fs;
  const MonoId a = seq_id(1), b = seq_id(2), c = seq_id(3);
  put_committed(fs, committed_meta(a, 32000, {}));
  fs.put(meta_of(b), rec_meta_encode(committed_meta(b, 32000, {})).c_str());  // .W already gone
  fs.put(wav_of(c), std::string(20, 'x'));                                     // < 44 B
  fs.put(meta_of(c), rec_meta_encode(committed_meta(c, 32000, {})).c_str());
  fs.put("/cpa/rec/3QZK1M7/README.TXT", "not ours");
  Engine e(fs);
  e.scan();
  TEST_ASSERT_EQUAL_UINT16(1, e.index.count());
  TEST_ASSERT_TRUE(e.index.at(0).id == a && e.index.at(0).size == 32044);
  TEST_ASSERT_FALSE(fs.has(meta_of(b)));
  TEST_ASSERT_FALSE(fs.has(wav_of(c)) || fs.has(meta_of(c)));
  TEST_ASSERT_TRUE(fs.has("/cpa/rec/3QZK1M7/README.TXT"));
  TEST_ASSERT_EQUAL_UINT32(1, e.index.rev());
  const RecEvt* ready = e.sink.last(EvtKind::IndexReady);
  TEST_ASSERT_TRUE(ready && ready->n == 1 && ready->rev == 1);
}

static void test_row_recover_relinks_via_next() {
  FakeRecFilePort fs;
  const MonoId a = seq_id(10), b = seq_id(11);
  put_committed(fs, committed_meta(a, 32000, some(b)));
  fs.put(wav_of(b), placeholder_file(RecMode::Meeting, 12288));
  Engine e(fs);
  e.scan();
  TEST_ASSERT_EQUAL_UINT16(2, e.index.count());
  TEST_ASSERT_EQUAL_UINT32(2, e.index.rev());  // one recovery
  const RecMeta m = read_meta(fs, b).value;
  TEST_ASSERT_TRUE(m.rec == a && m.seg == 1 && m.rcv && m.last && !m.sha256.has && m.bytes == 12288);
  TEST_ASSERT_TRUE(m.mode == RecMode::Meeting && !m.consent && !m.lead_ms.has);
  TEST_ASSERT_TRUE(m.at.has && m.at.value.up_ms == 2000 && m.at.value.wall_ms.value == 1792440001000LL);
  const Opt<WavInfo> h = wav_parse(reinterpret_cast<const uint8_t*>(fs.get(wav_of(b)).data()), 44);
  TEST_ASSERT_TRUE(h.has && h.value.data_bytes == 12288 && !h.value.hint.has);
}

static void test_row_recover_when_meta_is_bad_or_names_another_id() {
  FakeRecFilePort fs;
  const MonoId a = seq_id(20), b = seq_id(21), c = seq_id(22);
  fs.put(wav_of(a), placeholder_file(RecMode::Memo, 4097));  // odd: a sample cut in half
  fs.put(meta_of(a), "");
  fs.put(wav_of(b), placeholder_file(RecMode::Meeting, 8192));
  fs.put(meta_of(b), rec_meta_encode(committed_meta(c, 8192, {})).c_str());  // an old cluster
  Engine e(fs);
  e.scan();
  TEST_ASSERT_EQUAL_UINT16(2, e.index.count());
  const RecMeta ma = read_meta(fs, a).value;
  TEST_ASSERT_TRUE(ma.rcv && ma.rec == a && ma.seg == 0 && ma.bytes == 4096 && ma.mode == RecMode::Memo);
  TEST_ASSERT_TRUE(ma.lead_ms.has && !ma.consent && !ma.at.has);
  TEST_ASSERT_EQUAL_UINT32(44 + 4096, e.index.find(a)->size);
  const RecMeta mb = read_meta(fs, b).value;
  TEST_ASSERT_TRUE(mb.rcv && mb.id == b && mb.mode == RecMode::Meeting && mb.consent);
}

static void test_row_size_mismatch_is_served_as_is() {
  FakeRecFilePort fs;
  const MonoId a = seq_id(30);
  const RecMeta m = committed_meta(a, 8192, {});
  fs.put(wav_of(a), wav_file(8192) + std::string(4096, 'z'));
  const std::string text = rec_meta_encode(m).c_str();
  fs.put(meta_of(a), text);
  Engine e(fs);
  e.scan();
  TEST_ASSERT_EQUAL_UINT32(44 + 8192 + 4096, e.index.find(a)->size);
  TEST_ASSERT_TRUE(fs.get(meta_of(a)) == text);  // nothing rewritten
  TEST_ASSERT_EQUAL_UINT32(1, e.index.rev());
}

static void test_scan_is_idempotent() {
  FakeRecFilePort fs;
  const MonoId a = seq_id(40), b = seq_id(41);
  put_committed(fs, committed_meta(a, 32000, some(b)));
  fs.put(wav_of(b), placeholder_file(RecMode::Meeting, 9000));
  Engine first(fs);
  first.scan();
  const std::set<std::string> after_first = fs.paths();
  const std::string meta_b = fs.get(meta_of(b));
  Engine second(fs);
  second.scan();
  TEST_ASSERT_TRUE(fs.paths() == after_first);
  TEST_ASSERT_TRUE(fs.get(meta_of(b)) == meta_b);
  TEST_ASSERT_EQUAL_UINT32(1, second.index.rev());  // nothing to recover the second time
}

// ---- power cuts at every call ----------------------------------------------------------

static void test_power_cut_at_every_step_of_rotation_and_finish() {
  FakeRecFilePort probe;
  Sink unused;
  const long calls = run_meeting(probe, unused, -1);
  TEST_ASSERT_TRUE(calls > 20);
  for (long k = 0; k <= calls; ++k) {
    FakeRecFilePort fs;
    Sink sink;
    run_meeting(fs, sink, k);
    check_after_cut(fs, sink, {}, k);
  }
}

// The delete itself (docs/28 S1a keeps a handed-over segment instead; with no room for kept
// copies, a 100-name scan table, it is deleted as before: test_rec_ledger covers the keeping).
static void test_power_cut_at_every_step_of_a_delete() {
  FakeRecFilePort base;
  Sink made;
  run_meeting(base, made, -1);
  const MonoId victim = made.committed()[1];
  for (long k = 0; k < 12; ++k) {
    FakeRecFilePort fs = base;
    Engine e(fs);
    e.scan(1600);
    RecCmd del;
    del.kind = CmdKind::Del;
    del.fid = victim;
    del.size = e.index.find(victim)->size;
    del.sha256 = read_meta(fs, victim).value.sha256;
    fs.cut_at(fs.calls() + k);
    e.engine->del(del, e.sink);
    // once the .W is gone the delete is under way: the next boot finishes it
    const bool deleted = !fs.has(wav_of(victim));
    check_after_cut(fs, made, deleted ? std::set<MonoId>{victim} : std::set<MonoId>{}, k);
    // half a delete: .W gone, .M left -> the scan finished the delete
    if (!fs.has(wav_of(victim))) TEST_ASSERT_FALSE(fs.has(meta_of(victim)));
  }
}

static void test_power_cut_at_every_step_of_a_recovery() {
  FakeRecFilePort base;
  const MonoId a = seq_id(50), b = seq_id(51), c = seq_id(52);
  put_committed(base, committed_meta(a, 32000, some(b)));
  base.put(wav_of(b), placeholder_file(RecMode::Meeting, 12288));
  base.put(meta_of(b), "{\"v\":1,\"id\":\"3Q");  // a torn .M
  base.put(wav_of(c), placeholder_file(RecMode::Memo, 30));
  FakeRecFilePort clean = base;
  Engine ref(clean);
  ref.scan();
  const Outcome want = outcome_of(clean, ref.index);
  for (long k = 0; k < 40; ++k) {
    FakeRecFilePort fs = base;
    Engine e(fs);
    fs.cut_at(fs.calls() + k);
    e.scan();
    fs.reboot();
    Engine boot(fs);
    boot.scan();
    TEST_ASSERT_TRUE_MESSAGE(outcome_of(fs, boot.index).files == want.files, ("cut at " + std::to_string(k)).c_str());
    TEST_ASSERT_TRUE(read_meta(fs, a).has && fs.has(wav_of(a)));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_memo_writes_one_committed_segment);
  RUN_TEST(test_fsync_every_second);
  RUN_TEST(test_rotation_links_segments_and_splits_blocks);
  RUN_TEST(test_stars_and_tags_are_per_segment);
  RUN_TEST(test_memo_cap_is_exactly_one_segment);
  RUN_TEST(test_short_session_is_discarded);
  RUN_TEST(test_full_card_still_commits_the_segment);
  RUN_TEST(test_disk_error_stops_and_recovery_keeps_the_audio);
  RUN_TEST(test_rows_committed_half_deleted_and_too_short);
  RUN_TEST(test_row_recover_relinks_via_next);
  RUN_TEST(test_row_recover_when_meta_is_bad_or_names_another_id);
  RUN_TEST(test_row_size_mismatch_is_served_as_is);
  RUN_TEST(test_scan_is_idempotent);
  RUN_TEST(test_power_cut_at_every_step_of_rotation_and_finish);
  RUN_TEST(test_power_cut_at_every_step_of_a_delete);
  RUN_TEST(test_power_cut_at_every_step_of_a_recovery);
  return UNITY_END();
}
