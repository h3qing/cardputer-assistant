// v0.5 dests.snapshot (protocol/README item 24, docs/14 §7.2, docs/25 §5.1): the codec
// knows the message so the contract vectors round-trip; the device does not act on it
// yet (F5). Optional row fields are sent only when set, and an empty fl stays "" (docs/25 §2.2).
#include <unity.h>

#include <cstring>
#include <string>

#include "codec.h"
#include "text_rules.h"

using namespace cpa;
using namespace cpa::proto;

static Codec g_dest_codec;

static const char* kThreeRows =
    "{\"v\":0,\"t\":\"dests.snapshot\",\"id\":\"h12\",\"rev\":2918374651,\"dests\":["
    "{\"id\":\"clip\",\"slot\":5,\"label\":\"剪贴板\",\"kind\":\"clip\",\"key\":\"c\",\"fl\":\"l\"},"
    "{\"id\":\"file.sb\",\"slot\":7,\"label\":\"reading-list\",\"kind\":\"file\",\"fl\":\"\"},"
    "{\"id\":\"url.grok\",\"slot\":8,\"label\":\"Grok\",\"kind\":\"url\"}]}\n";

static DestRow row(const char* id, uint8_t slot, const char* label, const char* kind) {
  DestRow r;
  r.id = FixedStr<20>::from(id);
  r.slot = slot;
  r.label = FixedStr<24>::from(label);
  r.kind = FixedStr<7>::from(kind);
  return r;
}

void test_dests_snapshot_encodes_set_fields_only() {
  DestsSnapshot s;
  s.rev = 2918374651u;
  s.rows[0] = row("clip", 5, "剪贴板", "clip");
  s.rows[0].key = some(FixedStr<1>::from("c"));
  s.rows[0].fl = some(FixedStr<8>::from("l"));
  s.rows[1] = row("file.sb", 7, "reading-list", "file");
  s.rows[1].fl = some(FixedStr<8>::from(""));
  s.rows[2] = row("url.grok", 8, "Grok", "url");  // no fl: the device reads it as lck
  s.n = 3;
  const auto line = g_dest_codec.encode(make_message(MsgId::from("h12"), {}, s));
  TEST_ASSERT_TRUE(line.ok);
  TEST_ASSERT_EQUAL_STRING(kThreeRows, line.value.c_str());
}

void test_dests_snapshot_decodes_rows() {
  const Decoded& d = g_dest_codec.decode(kThreeRows, std::strlen(kThreeRows));
  TEST_ASSERT_TRUE(d.outcome == Outcome::Ok && msg_type(d.msg) == MsgType::DestsSnapshot);
  const DestsSnapshot& s = std::get<DestsSnapshot>(d.msg.body);
  TEST_ASSERT_EQUAL_UINT32(2918374651u, s.rev);
  TEST_ASSERT_EQUAL_UINT8(3, s.n);
  TEST_ASSERT_TRUE(s.rows[0].id.equals("clip") && s.rows[0].slot == 5 && s.rows[0].label.equals("剪贴板"));
  TEST_ASSERT_TRUE(s.rows[0].key.has && s.rows[0].key.value.equals("c"));
  TEST_ASSERT_TRUE(s.rows[0].fl.has && s.rows[0].fl.value.equals("l"));
  TEST_ASSERT_TRUE(s.rows[1].kind.equals("file") && s.rows[1].fl.has && s.rows[1].fl.value.equals(""));
  TEST_ASSERT_FALSE(s.rows[1].key.has || s.rows[2].fl.has);
}

void test_dest_row_text_rules() {
  TEST_ASSERT_TRUE(is_dest_row_id("abcdefg.zzzzzzzzzzzz", 20) && is_dest("abcdefg.zzzzzzzzzzzz", 20));
  TEST_ASSERT_TRUE(is_dest_row_id("file.0-x", 8));
  TEST_ASSERT_FALSE(is_dest_row_id("ab", 2) || is_dest_row_id("abcdefgh", 8) || is_dest_row_id("file.", 5));
  TEST_ASSERT_FALSE(is_dest_row_id("file.-x", 7) || is_dest_row_id("file.sb.x", 9) || is_dest_row_id("Clip", 4));
  TEST_ASSERT_FALSE(is_dest_row_id("file.aaaaaaaaaaaaa", 18));
  TEST_ASSERT_TRUE(is_dest_label("一二三四五六七八", 24));
  TEST_ASSERT_FALSE(is_dest_label("一二三四五六七八九", 27) || is_dest_label("", 0));
  TEST_ASSERT_FALSE(is_dest_label("a\"b", 3) || is_dest_label("a\\b", 3) || is_dest_label("a\x7f", 2));
  TEST_ASSERT_TRUE(is_dest_kind("abcdefg", 7) && is_dest_key("z", 1));
  TEST_ASSERT_FALSE(is_dest_kind("ab", 2) || is_dest_kind("file.sb", 7) || is_dest_key("C", 1) || is_dest_key("cc", 2));
  TEST_ASSERT_TRUE(is_dest_flags("", 0) && is_dest_flags("lcpk", 4) && is_dest_flags("abcdefgh", 8));
  TEST_ASSERT_FALSE(is_dest_flags("abcdefghi", 9) || is_dest_flags("LC", 2) || is_dest_flags("l c", 3));
}
