// Codec edge cases beyond the vectors: receiver order (protocol/README), who gets a
// reply, \r handling, trailing garbage, encode limits and message construction.
#include <unity.h>

#include <cstring>
#include <memory>
#include <string>

#include "b64.h"
#include "codec.h"
#include "errors.h"

using namespace cpa;
using namespace cpa::proto;

void setUp() {}
void tearDown() {}

static std::unique_ptr<Codec> g_codec;

static Decoded dec(const std::string& line) { return g_codec->decode(line.data(), line.size()); }

static void assert_error(const char* line, ErrCode code, const char* field) {
  const Decoded d = dec(line);
  TEST_ASSERT_TRUE_MESSAGE(d.outcome == Outcome::Error, line);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(err_name(code), err_name(d.code), line);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(field, d.field.c_str(), line);
}

static void test_noise_and_parse_errors() {
  TEST_ASSERT_TRUE(dec("").outcome == Outcome::Noise);
  TEST_ASSERT_TRUE(dec("\r\n").outcome == Outcome::Noise);
  TEST_ASSERT_TRUE(dec("E (12) boot\n").outcome == Outcome::Noise);
  TEST_ASSERT_TRUE(dec("[1,2]\n").outcome == Outcome::Noise);
  assert_error("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\"}x\n", ErrCode::Parse, "");
  assert_error("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\"}{}\n", ErrCode::Parse, "");
  assert_error("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"x\":[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]}\n",
               ErrCode::Parse, "");
  TEST_ASSERT_FALSE(dec("{\"v\":0,\"t\":\"ping\"\n").reply_to.has);
}

static void test_whitespace_and_cr() {
  TEST_ASSERT_TRUE(dec("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\"} \t \n").outcome == Outcome::Ok);
  TEST_ASSERT_TRUE(dec("{\"v\":0,\r\"t\":\"ping\",\"id\":\"h1\"}\r\n").outcome == Outcome::Ok);
  TEST_ASSERT_TRUE(dec("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\"}").outcome == Outcome::Ok);  // no \n
  // the 2048-byte limit is counted after dropping \r
  std::string line = "{\"v\":0,\"t\":\"ping\",\"id\":\"h1\"" + std::string(2048 - 29, ' ') + "}\n";
  TEST_ASSERT_EQUAL_UINT(2048, line.size());
  TEST_ASSERT_TRUE(dec(line).outcome == Outcome::Ok);
  TEST_ASSERT_TRUE(dec("\r\r" + line).outcome == Outcome::Ok);
  TEST_ASSERT_TRUE(dec(" " + line).outcome == Outcome::Error);
  TEST_ASSERT_TRUE(dec(" " + line).code == ErrCode::TooLarge);
}

static void test_who_gets_a_reply() {
  const Decoded bad = dec("{\"v\":0,\"t\":\"time.set\",\"id\":\"h66\",\"epoch_ms\":1792440000000,\"tz_min\":900}");
  TEST_ASSERT_TRUE(bad.reply_to.has);
  TEST_ASSERT_TRUE(bad.reply_to.value.equals("h66"));
  TEST_ASSERT_EQUAL_STRING("tz_min", bad.field.c_str());
  TEST_ASSERT_TRUE(dec("{\"v\":1,\"t\":\"ping\",\"id\":\"h64\"}").reply_to.has);
  const Decoded unknown = dec("{\"v\":0,\"t\":\"foo.bar\",\"id\":\"h61\"}");
  TEST_ASSERT_TRUE(unknown.code == ErrCode::UnknownType && unknown.reply_to.has);
  TEST_ASSERT_TRUE(dec("{\"v\":0,\"t\":\"foo.bar\",\"id\":\"h62\",\"re\":\"d60\"}").outcome == Outcome::Ignored);
  assert_error("{\"v\":0,\"t\":\"ping\",\"id\":\"h 1\"}", ErrCode::BadField, "id");
  TEST_ASSERT_FALSE(dec("{\"v\":0,\"t\":\"ping\",\"id\":\"h 1\"}").reply_to.has);
  TEST_ASSERT_FALSE(dec("{\"v\":0,\"t\":\"ack\",\"id\":\"h9\",\"re\":\"d9\"}").reply_to.has);
  // a broken reply is counted, never answered
  TEST_ASSERT_FALSE(dec("{\"v\":0,\"t\":\"pong\",\"id\":\"d1\",\"re\":\"h1\"}").reply_to.has);
  assert_error("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"re\":null}", ErrCode::BadField, "re");
  assert_error("{\"v\":0.0,\"t\":\"ping\",\"id\":\"h1\"}", ErrCode::BadField, "v");
  assert_error("{\"v\":true,\"t\":\"ping\",\"id\":\"h1\"}", ErrCode::BadField, "v");
}

static void test_bad_utf8_anywhere_is_bad_field() {
  assert_error("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"x_f\":{\"a\":[\"ok\",\"\xC0\x80\"]}}", ErrCode::BadField, "x_f.a[1]");
  assert_error("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"\xED\xA0\x80\":1}", ErrCode::BadField, "\xED\xA0\x80");
  assert_error("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"x\":\"\xF4\x90\x80\x80\"}", ErrCode::BadField, "x");
  assert_error("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"x\":\"\xE4\xB8\"}", ErrCode::BadField, "x");
  TEST_ASSERT_TRUE(dec("{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"x\":\"\xE4\xB8\xAD\xF0\x9F\x98\x80\"}").outcome == Outcome::Ok);
}

static void test_integral_numbers_follow_json_schema() {
  // JSON Schema treats 1.0 as an integer; the envelope's v must be a JSON integer.
  const Decoded d = dec("{\"v\":0,\"t\":\"time.set\",\"id\":\"h2\",\"epoch_ms\":1.79244e12,\"tz_min\":-420.0}");
  TEST_ASSERT_TRUE(d.outcome == Outcome::Ok);
  // json.loads("1e400") is inf, which JSON Schema rejects as an integer
  assert_error("{\"v\":0,\"t\":\"time.set\",\"id\":\"h2\",\"epoch_ms\":1e400,\"tz_min\":0}", ErrCode::BadField, "epoch_ms");
}

static void test_decoded_payloads() {
  const Decoded d = dec(
      "{\"v\":0,\"t\":\"outbox.item\",\"id\":\"3QZK1M7-0000C-0004\",\"kind\":\"queue\",\"at\":{\"up_ms\":98000,"
      "\"wall_ms\":1792440045000},\"body\":{\"text\":\"looks good\",\"project\":\"cpa\",\"intent\":\"feedback\","
      "\"sid\":\"5c2e8a14\"}}");
  TEST_ASSERT_TRUE(d.outcome == Outcome::Ok);
  const auto* item = std::get_if<OutboxItem>(&d.msg.body);
  TEST_ASSERT_NOT_NULL(item);
  TEST_ASSERT_TRUE(item->kind == ItemKind::Queue);
  TEST_ASSERT_TRUE(item->queue.intent == Intent::Feedback);
  TEST_ASSERT_TRUE(item->queue.sid.has && item->queue.sid.value.equals("5c2e8a14"));
  TEST_ASSERT_TRUE(item->wall_ms.has && item->wall_ms.value == 1792440045000LL);
  TEST_ASSERT_EQUAL_UINT32(98000, item->up_ms);
}

// The result lives in the codec (no Message-sized copy on the 16 KB loop stack): every
// decode() overwrites it, and nothing of an earlier message survives a failed one.
static void test_decode_result_lives_in_the_codec() {
  Codec c;
  const std::string ping = "{\"v\":0,\"t\":\"ping\",\"id\":\"h1\"}\n";
  const Decoded& a = c.decode(ping.data(), ping.size());
  TEST_ASSERT_TRUE(a.outcome == Outcome::Ok && a.msg.id.equals("h1") && a.reply_to.has);
  const Decoded& b = c.decode("x\n", 2);
  TEST_ASSERT_EQUAL_PTR(&a, &b);
  TEST_ASSERT_TRUE(b.outcome == Outcome::Noise);
  TEST_ASSERT_TRUE(b.msg.id.empty() && !b.msg.re.has && !b.reply_to.has && b.field.empty());
  const std::string bad_row = "{\"v\":0,\"t\":\"queue.snapshot\",\"id\":\"h2\",\"rev\":1,\"items\":[{\"qid\":\"q1\",\"slot\":1,"
                              "\"text\":\"a\",\"intent\":\"todo\"},{\"qid\":\"q2\",\"slot\":0,\"text\":\"b\",\"intent\":\"todo\"}]}";
  const Decoded& e = c.decode(bad_row.data(), bad_row.size());
  TEST_ASSERT_TRUE(e.outcome == Outcome::Error && e.code == ErrCode::BadField);
  TEST_ASSERT_EQUAL_STRING("items[1].slot", e.field.c_str());
  TEST_ASSERT_TRUE(e.msg.id.empty() && e.reply_to.value.equals("h2"));
  const std::string reply = "{\"v\":0,\"t\":\"ack\",\"id\":\"h3\",\"re\":\"d7\"}";
  const Decoded& r = c.decode(reply.data(), reply.size());
  TEST_ASSERT_TRUE(r.outcome == Outcome::Ok && r.msg.re.has && r.msg.re.value.equals("d7") && !r.reply_to.has);
  TEST_ASSERT_TRUE(c.decode(ping.data(), ping.size()).field.empty());
}

// A UTF-8 error at the nesting limit still reports the full path of the bad string.
static void test_bad_utf8_path_at_depth() {
  std::string open, close;
  for (int i = 0; i < 14; ++i) {
    open += "[1,";
    close += "]";
  }
  const std::string line = "{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"k\":{\"a\":\"ok\",\"b\":" + open + "\"\xFF\"" + close + "}}";
  assert_error(line.c_str(), ErrCode::BadField, "k.b[1][1][1][1][1][1][1][1][1][1][1][1][1][1]");
  const std::string fine = "{\"v\":0,\"t\":\"ping\",\"id\":\"h1\",\"k\":{\"a\":\"ok\",\"b\":" + open + "\"ok\"" + close + "}}";
  TEST_ASSERT_TRUE(dec(fine).outcome == Outcome::Ok);
}

static void test_encode_rejects_lines_over_the_limit() {
  QueueData d;
  d.rev = 1;
  for (uint8_t i = 0; i < kMaxQueueItems; ++i) {
    d.rows[i].qid = Qid::from(("q" + std::to_string(i)).c_str());
    d.rows[i].slot = static_cast<uint8_t>(1 + i / 4);
    d.rows[i].text = QueueText::from(std::string(80, '"').c_str());  // escapes as \"
  }
  d.n = kMaxQueueItems;
  const auto line = g_codec->encode(make_message(MsgId::from("h5"), {}, d));
  TEST_ASSERT_FALSE(line.ok);
  TEST_ASSERT_TRUE(line.error == ErrCode::TooLarge);
}

static void test_encode_ends_with_newline_and_wall_ms_null() {
  OutboxItem item;
  item.kind = ItemKind::Note;
  item.up_ms = 41000;
  item.note.text = UserText::from("idea: dedupe by sid");
  item.note.project = some(ProjectKey::from("cpa"));
  const auto line = g_codec->encode(make_message(MsgId::from("3QZK1M7-0000C-0003"), {}, item));
  TEST_ASSERT_TRUE(line.ok);
  TEST_ASSERT_EQUAL_STRING(
      "{\"v\":0,\"t\":\"outbox.item\",\"id\":\"3QZK1M7-0000C-0003\",\"kind\":\"note\",\"at\":{\"up_ms\":41000,"
      "\"wall_ms\":null},\"body\":{\"text\":\"idea: dedupe by sid\",\"project\":\"cpa\"}}\n",
      line.value.c_str());
}

// v0.2 hello.store / hello.launcher (09 §6): optional, so a device that sets neither
// sends exactly the v0.1 bytes; when set they follow revs.
static const char kV01Hello[] =
    "{\"v\":0,\"t\":\"hello\",\"id\":\"d1\",\"re\":\"h1\",\"role\":\"device\",\"fw\":\"cpa-fw/0.1.0\",\"dev\":\"020000000001\","
    "\"boot\":\"3QZK1M7-0000C\",\"up_ms\":53211,\"rst\":\"poweron\",\"time\":false,\"pending\":3,\"bat_mv\":4150,"
    "\"revs\":{\"projects\":0,\"sessions\":0,\"queue\":0}";

static Hello v01_device_hello() {
  Hello h;
  h.from_host = false;
  h.device.fw = FixedStr<31>::from("cpa-fw/0.1.0");
  h.device.dev = FixedStr<12>::from("020000000001");
  h.device.boot = BootIdStr::from("3QZK1M7-0000C");
  h.device.up_ms = 53211;
  h.device.rst = FixedStr<16>::from("poweron");
  h.device.pending = 3;
  h.device.bat_mv = 4150;
  return h;
}

static std::string encode_hello(const Hello& h) {
  const auto line = g_codec->encode(make_message(MsgId::from("d1"), some(MsgId::from("h1")), h));
  TEST_ASSERT_TRUE(line.ok);
  return line.value.c_str();
}

static void test_device_hello_without_store_is_v01_bytes() {
  const Hello h = v01_device_hello();
  TEST_ASSERT_FALSE(h.device.store.has);
  TEST_ASSERT_FALSE(h.device.launcher.has);
  TEST_ASSERT_EQUAL_STRING((std::string(kV01Hello) + "}\n").c_str(), encode_hello(h).c_str());
}

static void test_device_hello_encodes_store_and_launcher_after_revs() {
  Hello h = v01_device_hello();
  h.device.store = some(StoreKind::Sd);
  h.device.launcher = some(true);
  TEST_ASSERT_EQUAL_STRING((std::string(kV01Hello) + ",\"store\":\"sd\",\"launcher\":true}\n").c_str(), encode_hello(h).c_str());
  h.device.store = some(StoreKind::Int);
  h.device.launcher = some(false);
  TEST_ASSERT_EQUAL_STRING((std::string(kV01Hello) + ",\"store\":\"int\",\"launcher\":false}\n").c_str(), encode_hello(h).c_str());
  h.device.store = some(StoreKind::None);
  h.device.launcher = {};
  TEST_ASSERT_EQUAL_STRING((std::string(kV01Hello) + ",\"store\":\"none\"}\n").c_str(), encode_hello(h).c_str());
}

static void test_device_hello_decodes_store_and_launcher() {
  const Decoded& set = dec(std::string(kV01Hello) + ",\"store\":\"none\",\"launcher\":false}");
  TEST_ASSERT_TRUE(set.outcome == Outcome::Ok);
  const Hello* h = std::get_if<Hello>(&set.msg.body);
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_TRUE(h->device.store.has && h->device.store.value == StoreKind::None);
  TEST_ASSERT_TRUE(h->device.launcher.has && !h->device.launcher.value);
  const Decoded& nulls = dec(std::string(kV01Hello) + ",\"store\":null,\"launcher\":null}");
  TEST_ASSERT_TRUE(nulls.outcome == Outcome::Ok);
  h = std::get_if<Hello>(&nulls.msg.body);
  TEST_ASSERT_TRUE(h != nullptr && !h->device.store.has && !h->device.launcher.has);
  assert_error((std::string(kV01Hello) + ",\"store\":\"SD\"}").c_str(), ErrCode::BadField, "store");
  assert_error((std::string(kV01Hello) + ",\"store\":true}").c_str(), ErrCode::BadField, "store");
  assert_error((std::string(kV01Hello) + ",\"launcher\":1}").c_str(), ErrCode::BadField, "launcher");
}

// v0.3 (docs/11 §5.2): files is optional on the device hello and pong. Left unset the
// lines are byte-identical to v0.2; set, it follows launcher (hello) or revs (pong).
static void test_hello_and_pong_files_are_optional_and_last() {
  Hello h = v01_device_hello();
  h.device.store = some(StoreKind::Sd);
  h.device.launcher = some(false);
  const std::string v02 = std::string(kV01Hello) + ",\"store\":\"sd\",\"launcher\":false";
  TEST_ASSERT_EQUAL_STRING((v02 + "}\n").c_str(), encode_hello(h).c_str());
  h.device.files = some(FilesInfo{2, 17, false});
  TEST_ASSERT_EQUAL_STRING((v02 + ",\"files\":{\"n\":2,\"rev\":17,\"live\":false}}\n").c_str(), encode_hello(h).c_str());
  Pong p;
  p.up_ms = 735000;
  p.revs = Revs{42, 17, 9};
  const auto plain = g_codec->encode(make_message(MsgId::from("d40"), some(MsgId::from("h40")), p));
  const std::string v02_pong =
      "{\"v\":0,\"t\":\"pong\",\"id\":\"d40\",\"re\":\"h40\",\"up_ms\":735000,\"pending\":0,\"revs\":{\"projects\":42,"
      "\"sessions\":17,\"queue\":9}";
  TEST_ASSERT_EQUAL_STRING((v02_pong + "}\n").c_str(), plain.value.c_str());
  p.files = some(FilesInfo{0, 0, true});
  const auto with = g_codec->encode(make_message(MsgId::from("d40"), some(MsgId::from("h40")), p));
  TEST_ASSERT_EQUAL_STRING((v02_pong + ",\"files\":{\"n\":0,\"rev\":0,\"live\":true}}\n").c_str(), with.value.c_str());
}

static FileData data_of(uint16_t n, uint32_t off = 0) {
  FileData d;
  d.fid = MonoIdStr::from("K3M9Q2T-00042-0A1C");
  d.off = off;
  d.n = n;
  d.size = 384044;
  d.crc = 12345;
  for (uint16_t i = 0; i < n; ++i) d.bytes[i] = static_cast<uint8_t>(i * 7 + 3);
  return d;
}

// file.data is the one hot line (docs/gates/m3-device-gates.md §3.2): table base64 with
// padding, the fields in a fixed order, and it round-trips through the decoder.
static void test_file_data_encodes_padded_base64_and_round_trips() {
  for (uint16_t n : {0, 1, 2, 3, 44, 1379, 1380}) {
    const auto line = g_codec->encode(make_message(MsgId::from("d42"), some(MsgId::from("h42")), data_of(n)));
    TEST_ASSERT_TRUE(line.ok);
    TEST_ASSERT_TRUE(line.value.size() <= 2048);
    const Decoded& back = g_codec->decode(line.value.c_str(), line.value.size());
    TEST_ASSERT_TRUE_MESSAGE(back.outcome == Outcome::Ok, line.value.c_str());
    const auto* d = std::get_if<FileData>(&back.msg.body);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT16(n, d->n);
    TEST_ASSERT_EQUAL_UINT32(12345, d->crc);
    if (n > 0) TEST_ASSERT_EQUAL_MEMORY(data_of(n).bytes, d->bytes, n);
    TEST_ASSERT_EQUAL(n > 0, std::string(line.value.c_str()).find("\"b64\"") != std::string::npos);
  }
  const auto three = g_codec->encode(make_message(MsgId::from("d1"), some(MsgId::from("h1")), data_of(2)));
  TEST_ASSERT_EQUAL_STRING(
      "{\"v\":0,\"t\":\"file.data\",\"id\":\"d1\",\"re\":\"h1\",\"fid\":\"K3M9Q2T-00042-0A1C\",\"off\":0,\"n\":2,"
      "\"size\":384044,\"crc\":12345,\"b64\":\"Awo=\"}\n",
      three.value.c_str());
}

static void test_base64_helpers() {
  char out[16];
  const uint8_t man[] = {'M', 'a', 'n'};
  TEST_ASSERT_EQUAL_UINT(4, b64_encode(man, 3, out));
  TEST_ASSERT_EQUAL_STRING_LEN("TWFu", out, 4);
  TEST_ASSERT_EQUAL_UINT(4, b64_encode(man, 1, out));
  TEST_ASSERT_EQUAL_STRING_LEN("TQ==", out, 4);
  uint8_t back[8];
  TEST_ASSERT_EQUAL_INT(2, b64_decode("TWE=", 4, back, sizeof back));
  TEST_ASSERT_EQUAL_INT(-1, b64_decode("TWE", 3, back, sizeof back));     // not a multiple of 4
  TEST_ASSERT_EQUAL_INT(-1, b64_decode("TW@=", 4, back, sizeof back));    // not base64
  TEST_ASSERT_EQUAL_INT(-1, b64_decode("T===", 4, back, sizeof back));    // bad padding
  TEST_ASSERT_EQUAL_INT(-1, b64_decode("TWFuTWFu", 8, back, 5));          // too long for the buffer
}

static void test_file_requests_decode() {
  const Decoded& g = dec("{\"v\":0,\"t\":\"file.get\",\"id\":\"h42\",\"fid\":\"K3M9Q2T-00042-0A1C\",\"off\":704,\"len\":1380}");
  TEST_ASSERT_TRUE(g.outcome == Outcome::Ok && g.reply_to.has);
  const auto* get = std::get_if<FileGet>(&g.msg.body);
  TEST_ASSERT_TRUE(get && get->off == 704 && get->len == 1380 && get->fid.equals("K3M9Q2T-00042-0A1C"));
  const Decoded& l = dec("{\"v\":0,\"t\":\"file.list\",\"id\":\"h41\",\"after\":\"K3M9Q2T-00042-0A1D\"}");
  const auto* list = std::get_if<FileList>(&l.msg.body);
  TEST_ASSERT_TRUE(list && list->after.has && list->after.value.equals("K3M9Q2T-00042-0A1D") && !list->max.has);
  const Decoded& x = dec("{\"v\":0,\"t\":\"file.del\",\"id\":\"h99\",\"fid\":\"K3M9Q2T-00042-0A1E\",\"size\":1953836}");
  const auto* del = std::get_if<FileDel>(&x.msg.body);
  TEST_ASSERT_TRUE(del && del->size == 1953836 && !del->sha256.has);
  const Decoded& r = dec("{\"v\":0,\"t\":\"debug.rec\",\"id\":\"h80\",\"seg_ms\":30000}");
  const auto* rec = std::get_if<DebugRec>(&r.msg.body);
  TEST_ASSERT_TRUE(rec && rec->seg_ms == 30000);
}

// debug.mic (v0.3.1, test firmware): the gain for the next recordings, ack; pga 0-10,
// vol 0-255, mag 1-64.
static void test_debug_mic_decodes_and_encodes() {
  const Decoded d = dec("{\"v\":0,\"t\":\"debug.mic\",\"id\":\"h81\",\"pga\":10,\"vol\":197,\"mag\":2}");
  TEST_ASSERT_TRUE(d.outcome == Outcome::Ok && d.reply_to.has);
  const auto* mic = std::get_if<DebugMic>(&d.msg.body);
  TEST_ASSERT_TRUE(mic && mic->pga == 10 && mic->vol == 197 && mic->mag == 2);
  const auto line = g_codec->encode(make_message(MsgId::from("h81"), {}, *mic));
  TEST_ASSERT_TRUE(line.ok);
  TEST_ASSERT_EQUAL_STRING("{\"v\":0,\"t\":\"debug.mic\",\"id\":\"h81\",\"pga\":10,\"vol\":197,\"mag\":2}\n",
                           line.value.c_str());
  assert_error("{\"v\":0,\"t\":\"debug.mic\",\"id\":\"h81\",\"pga\":11,\"vol\":191,\"mag\":2}", ErrCode::BadField, "pga");
  assert_error("{\"v\":0,\"t\":\"debug.mic\",\"id\":\"h81\",\"pga\":0,\"vol\":256,\"mag\":2}", ErrCode::BadField, "vol");
  assert_error("{\"v\":0,\"t\":\"debug.mic\",\"id\":\"h81\",\"pga\":0,\"vol\":191,\"mag\":0}", ErrCode::BadField, "mag");
  assert_error("{\"v\":0,\"t\":\"debug.mic\",\"id\":\"h81\",\"pga\":0,\"vol\":191,\"mag\":65}", ErrCode::BadField, "mag");
  assert_error("{\"v\":0,\"t\":\"debug.mic\",\"id\":\"h81\",\"vol\":191,\"mag\":2}", ErrCode::BadField, "pga");
}

// debug.codec (v0.3.2, test firmware, issue #31): the ES8311 as a power-on leaves it, and
// with warm the boot's warm-up after it; ack. warm is a required boolean.
static void test_debug_codec_decodes_and_encodes() {
  const Decoded d = dec("{\"v\":0,\"t\":\"debug.codec\",\"id\":\"h82\",\"warm\":true}");
  TEST_ASSERT_TRUE(d.outcome == Outcome::Ok && d.reply_to.has);
  const auto* codec = std::get_if<DebugCodec>(&d.msg.body);
  TEST_ASSERT_TRUE(codec && codec->warm);
  const Decoded c = dec("{\"v\":0,\"t\":\"debug.codec\",\"id\":\"h82\",\"warm\":false}");
  const auto* cold = std::get_if<DebugCodec>(&c.msg.body);
  TEST_ASSERT_TRUE(cold && !cold->warm);
  const auto line = g_codec->encode(make_message(MsgId::from("h82"), {}, *cold));
  TEST_ASSERT_TRUE(line.ok);
  TEST_ASSERT_EQUAL_STRING("{\"v\":0,\"t\":\"debug.codec\",\"id\":\"h82\",\"warm\":false}\n", line.value.c_str());
  assert_error("{\"v\":0,\"t\":\"debug.codec\",\"id\":\"h82\"}", ErrCode::BadField, "warm");
  assert_error("{\"v\":0,\"t\":\"debug.codec\",\"id\":\"h82\",\"warm\":1}", ErrCode::BadField, "warm");
}

// v0.4.0 debug.state.ui (docs/13 §12.1, test firmware): unset, the summary is byte-identical
// to v0.3.1; set, it follows rec. The vectors cover ui.menu / ui.locked values and errors.
static void test_debug_state_ui_is_optional_and_last() {
  const std::string head =
      "{\"v\":0,\"t\":\"debug.state\",\"id\":\"d71\",\"re\":\"h71\",\"up_ms\":98123,\"link\":\"Ready\",\"mod\":\"board\","
      "\"overlay\":\"none\",\"outbox\":{\"pending\":0,\"inflight\":0,\"bad\":0},\"revs\":{\"projects\":0,\"sessions\":0,"
      "\"queue\":0},\"need\":0,\"projects\":[]";
  DebugState s;
  s.summary = true;
  s.up_ms = 98123;
  s.link = LinkMode::Ready;
  s.mod = FixedStr<16>::from("board");
  const auto plain = g_codec->encode(make_message(MsgId::from("d71"), some(MsgId::from("h71")), s));
  TEST_ASSERT_EQUAL_STRING((head + "}\n").c_str(), plain.value.c_str());
  s.rec = some(DebugRecState{});
  s.ui = some(DebugUi{false, true, {}});
  const auto with = g_codec->encode(make_message(MsgId::from("d71"), some(MsgId::from("h71")), s));
  const std::string rec = ",\"rec\":{\"live\":false,\"mode\":null,\"seg\":null,\"files_n\":0,\"files_rev\":0}";
  TEST_ASSERT_EQUAL_STRING((head + rec + ",\"ui\":{\"menu\":false,\"locked\":true}}\n").c_str(), with.value.c_str());
  assert_error((head + ",\"ui\":true}").c_str(), ErrCode::BadField, "ui");
  assert_error((head + ",\"ui\":{\"menu\":false,\"locked\":null}}").c_str(), ErrCode::BadField, "ui.locked");
}

static void test_names_round_trip() {
  for (uint8_t i = 0; i < kMsgTypeCount; ++i) {
    const char* n = type_name(static_cast<MsgType>(i));
    const auto t = type_parse(n, std::strlen(n));
    TEST_ASSERT_TRUE(t.has && static_cast<uint8_t>(t.value) == i);
  }
  TEST_ASSERT_FALSE(type_parse("pingx", 5).has);
  TEST_ASSERT_FALSE(err_parse("E_OOPS", 6).has);
  TEST_ASSERT_EQUAL_STRING("", err_name(ErrCode::None));
}

// gh #21 (test_debug_codec.cpp)
void test_debug_state_diag_is_optional_and_last();
void test_debug_lang_decodes_and_encodes();      // v0.4.3 (docs/21 §7)
void test_debug_state_ui_lang_is_optional_and_last();
// v0.4.1 (test_rec_codec.cpp)
void test_host_hello_v041_fields_are_stored();
void test_host_hello_sends_v041_fields_only_when_set();
void test_note_dest_is_sent_only_when_set();
void test_file_item_dest_is_sent_only_when_set();
void test_rec_list_and_rec_act_encode();
void test_rec_items_decode_into_a_bounded_page();
void test_rec_items_worst_line_fits_and_round_trips();
void test_v041_text_rules();
// v0.5 (test_dest_codec.cpp)
void test_dests_snapshot_encodes_set_fields_only();
void test_dests_snapshot_decodes_rows();
void test_dest_row_text_rules();

int main() {
  g_codec.reset(new Codec());
  UNITY_BEGIN();
  RUN_TEST(test_noise_and_parse_errors);
  RUN_TEST(test_whitespace_and_cr);
  RUN_TEST(test_who_gets_a_reply);
  RUN_TEST(test_bad_utf8_anywhere_is_bad_field);
  RUN_TEST(test_integral_numbers_follow_json_schema);
  RUN_TEST(test_decoded_payloads);
  RUN_TEST(test_decode_result_lives_in_the_codec);
  RUN_TEST(test_bad_utf8_path_at_depth);
  RUN_TEST(test_encode_rejects_lines_over_the_limit);
  RUN_TEST(test_encode_ends_with_newline_and_wall_ms_null);
  RUN_TEST(test_device_hello_without_store_is_v01_bytes);
  RUN_TEST(test_device_hello_encodes_store_and_launcher_after_revs);
  RUN_TEST(test_device_hello_decodes_store_and_launcher);
  RUN_TEST(test_names_round_trip);
  RUN_TEST(test_hello_and_pong_files_are_optional_and_last);
  RUN_TEST(test_file_data_encodes_padded_base64_and_round_trips);
  RUN_TEST(test_base64_helpers);
  RUN_TEST(test_file_requests_decode);
  RUN_TEST(test_debug_mic_decodes_and_encodes);
  RUN_TEST(test_debug_codec_decodes_and_encodes);
  RUN_TEST(test_debug_state_ui_is_optional_and_last);
  RUN_TEST(test_debug_state_diag_is_optional_and_last);
  RUN_TEST(test_debug_lang_decodes_and_encodes);
  RUN_TEST(test_debug_state_ui_lang_is_optional_and_last);
  RUN_TEST(test_host_hello_v041_fields_are_stored);
  RUN_TEST(test_host_hello_sends_v041_fields_only_when_set);
  RUN_TEST(test_note_dest_is_sent_only_when_set);
  RUN_TEST(test_file_item_dest_is_sent_only_when_set);
  RUN_TEST(test_rec_list_and_rec_act_encode);
  RUN_TEST(test_rec_items_decode_into_a_bounded_page);
  RUN_TEST(test_rec_items_worst_line_fits_and_round_trips);
  RUN_TEST(test_v041_text_rules);
  RUN_TEST(test_dests_snapshot_encodes_set_fields_only);
  RUN_TEST(test_dests_snapshot_decodes_rows);
  RUN_TEST(test_dest_row_text_rules);
  return UNITY_END();
}
