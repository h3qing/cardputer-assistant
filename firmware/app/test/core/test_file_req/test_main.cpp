// file.* serving (docs/11 §4.7, §5.3-§5.4): the loop's checks (file_req.h) and the audio
// task's list / get / del (RecEngine) on FakeRecFilePort.
#include <unity.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "codec.h"
#include "file_req.h"
#include "rec_engine.h"
#include "support/fake_rec_file_port.h"

using namespace cpa;
using namespace cpa::rec;
using testsupport::FakeRecFilePort;
using testsupport::FakeSha256;

void setUp() {}
void tearDown() {}

static MonoId seq_id(uint32_t s) {
  MonoId id = mono_parse("3QZK1M7-0000C-0000", 18).value;
  id.seq = s;
  return id;
}

static RecIndex ready_index(std::initializer_list<uint32_t> seqs, uint32_t size = 384044) {
  RecIndex x;
  for (uint32_t s : seqs) x.insert(seq_id(s), size);
  x.set_ready(0);
  return x;
}

static proto::FileGet get_of(const MonoId& id, uint32_t off, uint16_t len) {
  proto::FileGet g;
  g.fid = mono_format(id);
  g.off = off;
  g.len = len;
  return g;
}

static void test_gate_order() {
  const FileVerdict live = file_gate(false, FileGate{true, 0});
  TEST_ASSERT_TRUE(live.answer == FileAnswer::Error && live.code == ErrCode::Busy && live.msg.value.equals("live"));
  TEST_ASSERT_TRUE(file_gate(false, FileGate{false, 0}).code == ErrCode::NotReady);
  const FileVerdict full = file_gate(true, FileGate{false, 4});
  TEST_ASSERT_TRUE(full.code == ErrCode::Busy && full.msg.value.equals("full"));
  TEST_ASSERT_TRUE(file_gate(true, FileGate{false, 3}).answer == FileAnswer::Queue);
  TEST_ASSERT_TRUE(file_gate(true, FileGate{true, 0}).msg.value.equals("live"));  // live wins over all
}

static void test_get_bounds() {
  const RecIndex idx = ready_index({1}, 1000);
  const FileVerdict ok = file_check_get(get_of(seq_id(1), 0, 1380), idx);
  TEST_ASSERT_TRUE(ok.answer == FileAnswer::Queue && ok.len == 1000 && ok.size == 1000 && ok.id == seq_id(1));
  TEST_ASSERT_EQUAL_UINT16(380, file_check_get(get_of(seq_id(1), 620, 1380), idx).len);
  const FileVerdict end = file_check_get(get_of(seq_id(1), 1000, 10), idx);
  TEST_ASSERT_TRUE(end.answer == FileAnswer::Data0 && end.size == 1000);
  TEST_ASSERT_TRUE(file_check_get(get_of(seq_id(1), 5000, 10), idx).answer == FileAnswer::Data0);
  const FileVerdict unknown = file_check_get(get_of(seq_id(2), 0, 10), idx);
  TEST_ASSERT_TRUE(unknown.answer == FileAnswer::Error && unknown.code == ErrCode::UnknownItem);
  proto::FileGet foreign = get_of(seq_id(1), 0, 10);
  foreign.fid = MonoIdStr::from("K3M9Q2T-00042-0A1C");  // schema-valid, not one of ours
  TEST_ASSERT_TRUE(file_check_get(foreign, idx).code == ErrCode::UnknownItem);
}

static void test_del_checks() {
  const RecIndex idx = ready_index({1}, 1000);
  proto::FileDel d;
  d.fid = mono_format(seq_id(1));
  d.size = 1000;
  TEST_ASSERT_TRUE(file_check_del(d, idx).answer == FileAnswer::Queue);
  d.size = 999;
  const FileVerdict bad = file_check_del(d, idx);
  TEST_ASSERT_TRUE(bad.code == ErrCode::BadField && bad.field.value.equals("size"));
  d.fid = mono_format(seq_id(9));
  TEST_ASSERT_TRUE(file_check_del(d, idx).answer == FileAnswer::Ack);  // already gone
}

static void test_list_pages() {
  const RecIndex idx = ready_index({1, 2, 3, 4, 5, 6});
  proto::FileList l;
  ListPlan p = file_plan_list(l, idx);
  TEST_ASSERT_TRUE(p.n == 4 && p.more && p.ids[0] == seq_id(1) && p.ids[3] == seq_id(4));
  l.after = some(mono_format(seq_id(4)));
  p = file_plan_list(l, idx);
  TEST_ASSERT_TRUE(p.n == 2 && !p.more && p.ids[0] == seq_id(5) && p.sizes[1] == 384044);
  l.after = {};
  l.max = some(uint8_t{1});
  p = file_plan_list(l, idx);
  TEST_ASSERT_TRUE(p.n == 1 && p.more);
  l.after = some(mono_format(seq_id(6)));
  TEST_ASSERT_EQUAL_UINT8(0, file_plan_list(l, idx).n);
}

static bool fits_under(void* ctx, uint8_t k) { return k <= *static_cast<uint8_t*>(ctx); }

static void test_fit_page() {
  uint8_t limit = 2;
  TEST_ASSERT_EQUAL_UINT8(2, file_fit_page(4, fits_under, &limit));
  limit = 9;
  TEST_ASSERT_EQUAL_UINT8(4, file_fit_page(4, fits_under, &limit));
  limit = 0;
  TEST_ASSERT_EQUAL_UINT8(0, file_fit_page(4, fits_under, &limit));
}

// ---- the audio task's side ----------------------------------------------------------

struct Collect : EvtSink {
  std::vector<RecEvt> ev;
  void emit(const RecEvt& e) override { ev.push_back(e); }
};

struct Card {
  FakeRecFilePort fs;
  FakeSha256 sha;
  MonoCounter ids;
  RecIndex index;
  TransferBuf xfer;
  std::unique_ptr<RecEngine> engine;
  Collect out;
  MonoId memo;
  std::string data;

  Card() {
    const BootRecord rec{seq_id(0).epoch, 12};
    ids.start(boot_ticket(rec, rec).value, 0x40);
    EngineDeps d;
    d.fs = &fs;
    d.sha = &sha;
    d.ids = &ids;
    d.index = &index;
    d.xfer = &xfer;
    d.fw = FixedStr<31>::from("cpa-fw/0.3.0");
    engine.reset(new RecEngine(d));
    StartSpec s;
    s.project = some(ProjectKey::from("cpa"));
    engine->start(s, 435000, 0, out);
    memo = out.ev.back().id;
    for (int i = 0; i < 10; ++i) {
      std::string b(4096, '\0');
      for (size_t k = 0; k < b.size(); ++k) b[k] = static_cast<char>(k * 31 + i);
      engine->write(reinterpret_cast<const uint8_t*>(b.data()), b.size(), i * 128, out);
      data += b;
    }
    engine->stop(out);
    out.ev.clear();
  }
};

static void test_list_reads_the_meta_files() {
  Card c;
  RecCmd cmd;
  cmd.kind = CmdKind::List;
  cmd.n = 1;
  cmd.ids[0] = c.memo;
  cmd.sizes[0] = 44 + 40960;
  c.engine->list(cmd, c.out);
  TEST_ASSERT_TRUE(c.out.ev.back().kind == EvtKind::Items && c.out.ev.back().n == 1);
  const proto::FileItem& it = c.xfer.items[0];
  TEST_ASSERT_TRUE(it.id.equals(mono_format(c.memo).c_str()) && it.mode == RecMode::Memo && it.last);
  TEST_ASSERT_TRUE(it.sha256.value == FakeSha256::of(c.data) && it.at.value.up_ms == 435000);
  TEST_ASSERT_EQUAL_UINT32(1280, it.dur_ms);
  cmd.ids[0] = seq_id(0x7FF);  // gone meanwhile
  c.engine->list(cmd, c.out);
  TEST_ASSERT_TRUE(c.out.ev.back().code == ErrCode::Storage);
}

static void test_get_keeps_the_file_open_between_chunks() {
  Card c;
  RecCmd g;
  g.kind = CmdKind::Get;
  g.fid = c.memo;
  g.size = 44 + 40960;
  std::string got;
  for (uint32_t off = 0; off < g.size; off += 1380) {
    g.off = off;
    g.len = 1380;
    c.engine->get(g, c.out);
    const RecEvt& e = c.out.ev.back();
    TEST_ASSERT_TRUE(e.kind == EvtKind::Data && e.code == ErrCode::None && e.off == off);
    TEST_ASSERT_EQUAL_UINT32(crc32_ieee(c.xfer.data, e.n), e.crc);
    got.append(reinterpret_cast<const char*>(c.xfer.data), e.n);
  }
  TEST_ASSERT_TRUE(got == c.fs.get(rec_wav_path("/cpa", c.memo).c_str()));
  TEST_ASSERT_TRUE(c.fs.is_open());  // one open for all chunks
  TEST_ASSERT_EQUAL_INT(0, c.fs.double_open);
  c.engine->close_transfer();
  TEST_ASSERT_FALSE(c.fs.is_open());
  g.fid = seq_id(0x7FF);
  g.off = 0;
  c.engine->get(g, c.out);
  TEST_ASSERT_TRUE(c.out.ev.back().code == ErrCode::UnknownItem);
}

static void test_del_checks_the_recorded_sha() {
  Card c;
  RecCmd d;
  d.kind = CmdKind::Del;
  d.fid = c.memo;
  d.size = 44 + 40960;
  d.sha256 = some(Sha256Hex::from("0000000000000000000000000000000000000000000000000000000000000000"));
  c.engine->del(d, c.out);
  TEST_ASSERT_TRUE(c.out.ev.back().code == ErrCode::BadField && c.out.ev.back().field_sha);
  d.sha256 = {};
  c.engine->del(d, c.out);
  TEST_ASSERT_TRUE(c.out.ev.back().field_sha);  // recorded: must be sent and match
  TEST_ASSERT_TRUE(c.fs.has(rec_wav_path("/cpa", c.memo).c_str()));
  d.sha256 = some(FakeSha256::of(c.data));
  c.engine->del(d, c.out);
  TEST_ASSERT_TRUE(c.out.ev.back().kind == EvtKind::DelDone && c.out.ev.back().code == ErrCode::None);
  TEST_ASSERT_EQUAL_UINT(0, c.fs.file_count());
  // a get on another file left open is closed first: one open file at a time
  TEST_ASSERT_EQUAL_INT(0, c.fs.double_open);
}

// docs/11 §5.5: a file.data line with 24-character ids and n = 1380 fits 2048 B.
static void test_file_data_line_fits() {
  proto::Codec codec;
  proto::FileData d;
  d.fid = MonoIdStr::from("K3M9Q2T-00042-0A1C");
  d.off = 4294967295u;
  d.n = 1380;
  d.size = 19200044;
  d.crc = 4294967295u;
  const auto line = codec.encode(proto::make_message(MsgId::from("abcdefghijklmnopqrstuvwx"),
                                                     some(MsgId::from("abcdefghijklmnopqrstuvwx")), d));
  TEST_ASSERT_TRUE(line.ok);
  TEST_ASSERT_TRUE(line.value.size() <= 2048);
}

// docs/28 S1a: a Mac never sees what was taken back from it, nor (for the plug-in hint's
// 5 s) the offline ones: not listed, not counted, get E_UNKNOWN_ITEM, del ack and kept.
static void test_viewer_hides_kept_from_and_held() {
  RecIndex idx;
  idx.insert(seq_id(1), 1000);
  idx.insert(seq_id(2), 1000, 0xA1);
  idx.insert(seq_id(3), 1000);
  idx.set_ready(0);
  Viewer a;
  a.host = 0xA1;
  TEST_ASSERT_EQUAL_UINT16(2, file_visible_count(idx, a));
  proto::FileList all;
  const ListPlan p = file_plan_list(all, idx, a);
  TEST_ASSERT_EQUAL_UINT8(2, p.n);
  TEST_ASSERT_TRUE(p.ids[0] == seq_id(1) && p.ids[1] == seq_id(3) && !p.more);
  TEST_ASSERT_TRUE(file_check_get(get_of(seq_id(2), 0, 10), idx, a).code == ErrCode::UnknownItem);
  proto::FileDel del;
  del.fid = mono_format(seq_id(2));
  del.size = 1000;
  TEST_ASSERT_TRUE(file_check_del(del, idx, a).answer == FileAnswer::Ack);
  Viewer b;
  b.host = 0xB2;
  TEST_ASSERT_EQUAL_UINT16(3, file_visible_count(idx, b));
  TEST_ASSERT_TRUE(file_check_del(del, idx, b).answer == FileAnswer::Queue);
  b.hold = true;
  b.hold_from = seq_id(2);
  TEST_ASSERT_EQUAL_UINT16(1, file_visible_count(idx, b));
  proto::FileList one;
  one.max = some(uint8_t{1});
  const ListPlan q = file_plan_list(one, idx, b);
  TEST_ASSERT_TRUE(q.n == 1 && q.ids[0] == seq_id(1) && !q.more);  // nothing seen after it
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_gate_order);
  RUN_TEST(test_get_bounds);
  RUN_TEST(test_del_checks);
  RUN_TEST(test_list_pages);
  RUN_TEST(test_fit_page);
  RUN_TEST(test_list_reads_the_meta_files);
  RUN_TEST(test_get_keeps_the_file_open_between_chunks);
  RUN_TEST(test_del_checks_the_recorded_sha);
  RUN_TEST(test_file_data_line_fits);
  RUN_TEST(test_viewer_hides_kept_from_and_held);
  return UNITY_END();
}
