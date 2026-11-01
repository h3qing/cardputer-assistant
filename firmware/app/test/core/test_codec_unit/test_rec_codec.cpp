// v0.4.1 codec additions (protocol/README item 20, docs/13 §12.2): host hello caps and
// identity, note / file item dest, rec.list, rec.items and rec.act. New optional fields
// are sent only when set, so a v0.4.0 message encodes to exactly the v0.4.0 bytes.
#include <unity.h>

#include <cstring>
#include <string>

#include "codec.h"
#include "errors.h"
#include "text_rules.h"

using namespace cpa;
using namespace cpa::proto;

static Codec g_rec_codec;

static std::string encode_line(const Message& m) {
  const auto line = g_rec_codec.encode(m);
  TEST_ASSERT_TRUE(line.ok);
  return line.value.c_str();
}

static const Decoded& decode_line(const std::string& line) { return g_rec_codec.decode(line.data(), line.size()); }
// Literals take this overload: binding the result to a temporary std::string makes GCC 13 warn
// (-Wdangling-reference), although the reference points into g_rec_codec.
static const Decoded& decode_line(const char* line) { return g_rec_codec.decode(line, std::strlen(line)); }

static Hello host_hello() {
  Hello h;
  h.from_host = true;
  h.host.app = FixedStr<32>::from("cpad/0.4.1");
  h.host.agent = AgentStatus::Ok;
  h.host.cap_spawn = true;
  return h;
}

void test_host_hello_v041_fields_are_stored() {
  const Decoded& d = decode_line(
      "{\"v\":0,\"t\":\"hello\",\"id\":\"h1\",\"role\":\"host\",\"app\":\"cpad/0.4.1\",\"agent\":\"ok\",\"caps\":{\"spawn\":true,"
      "\"reply\":false,\"recs\":true,\"todo\":false,\"x_beam\":true},\"host_id\":\"7f3a9c1e5b2d4a60\",\"host_label\":\"Studio Mac\"}");
  TEST_ASSERT_TRUE(d.outcome == Outcome::Ok);
  const HostHello& h = std::get<Hello>(d.msg.body).host;
  TEST_ASSERT_TRUE(h.cap_spawn && !h.cap_reply);
  TEST_ASSERT_TRUE(h.cap_recs.has && h.cap_recs.value);
  TEST_ASSERT_TRUE(h.cap_todo.has && !h.cap_todo.value);
  TEST_ASSERT_TRUE(h.host_id.has && h.host_id.value.equals("7f3a9c1e5b2d4a60"));
  TEST_ASSERT_TRUE(h.host_label.has && h.host_label.value.equals("Studio Mac"));
  const Decoded& old = decode_line(
      "{\"v\":0,\"t\":\"hello\",\"id\":\"h1\",\"role\":\"host\",\"app\":\"cpad/0.4.0\",\"agent\":\"ok\",\"caps\":{\"spawn\":true,"
      "\"reply\":false},\"host_id\":null}");
  TEST_ASSERT_TRUE(old.outcome == Outcome::Ok);
  const HostHello& o = std::get<Hello>(old.msg.body).host;
  TEST_ASSERT_FALSE(o.cap_recs.has || o.cap_todo.has || o.host_id.has || o.host_label.has);
}

void test_host_hello_sends_v041_fields_only_when_set() {
  Hello h = host_hello();
  const std::string head =
      "{\"v\":0,\"t\":\"hello\",\"id\":\"h1\",\"role\":\"host\",\"app\":\"cpad/0.4.1\",\"agent\":\"ok\",\"caps\":{\"spawn\":true,"
      "\"reply\":false";
  TEST_ASSERT_EQUAL_STRING((head + "}}\n").c_str(), encode_line(make_message(MsgId::from("h1"), {}, h)).c_str());
  h.host.cap_recs = some(true);
  h.host.cap_todo = some(false);
  h.host.host_id = some(HostId::from("7f3a9c1e5b2d4a60"));
  h.host.host_label = some(HostLabel::from("Studio Mac"));
  TEST_ASSERT_EQUAL_STRING(
      (head + ",\"recs\":true,\"todo\":false},\"host_id\":\"7f3a9c1e5b2d4a60\",\"host_label\":\"Studio Mac\"}\n").c_str(),
      encode_line(make_message(MsgId::from("h1"), {}, h)).c_str());
}

void test_note_dest_is_sent_only_when_set() {
  OutboxItem item;
  item.kind = ItemKind::Note;
  item.up_ms = 41000;
  item.note.text = UserText::from("call the plumber");
  item.note.project = some(ProjectKey::from("cpa"));
  const MsgId id = MsgId::from("3QZK1M7-0000C-0005");
  const std::string head =
      "{\"v\":0,\"t\":\"outbox.item\",\"id\":\"3QZK1M7-0000C-0005\",\"kind\":\"note\",\"at\":{\"up_ms\":41000,"
      "\"wall_ms\":null},\"body\":{\"text\":\"call the plumber\",\"project\":\"cpa\"";
  TEST_ASSERT_EQUAL_STRING((head + "}}\n").c_str(), encode_line(make_message(id, {}, item)).c_str());
  item.dest = some(DestId::from("todo"));
  const std::string with = encode_line(make_message(id, {}, item));
  TEST_ASSERT_EQUAL_STRING((head + ",\"dest\":\"todo\"}}\n").c_str(), with.c_str());
  const Decoded& back = decode_line(with);
  TEST_ASSERT_TRUE(back.outcome == Outcome::Ok);
  const OutboxItem& got = std::get<OutboxItem>(back.msg.body);
  TEST_ASSERT_TRUE(got.dest.has && got.dest.value.equals("todo"));
  item.kind = ItemKind::Queue;  // dest belongs to the note body only
  item.queue.text = UserText::from("call the plumber");
  item.queue.project = ProjectKey::from("cpa");
  TEST_ASSERT_TRUE(encode_line(make_message(id, {}, item)).find("dest") == std::string::npos);
}

static FileItems one_memo() {
  FileItems items;
  FileItem& it = items.items[0];
  it.id = MonoIdStr::from("3QZK1M7-00042-0A1C");
  it.size = 384044;
  it.rec = it.id;
  it.last = true;
  it.dur_ms = 12000;
  items.n = 1;
  return items;
}

void test_file_item_dest_is_sent_only_when_set() {
  FileItems items = one_memo();
  const Message plain = make_message(MsgId::from("d41"), some(MsgId::from("h41")), items);
  TEST_ASSERT_TRUE(encode_line(plain).find("dest") == std::string::npos);
  items.items[0].dest = some(DestId::from("todo"));
  const std::string with = encode_line(make_message(MsgId::from("d41"), some(MsgId::from("h41")), items));
  TEST_ASSERT_TRUE(with.find(",\"dest\":\"todo\"}],\"more\":false}\n") != std::string::npos);
  const Decoded& back = decode_line(with);
  TEST_ASSERT_TRUE(back.outcome == Outcome::Ok);
  const FileItem& got = std::get<FileItems>(back.msg.body).items[0];
  TEST_ASSERT_TRUE(got.dest.has && got.dest.value.equals("todo"));
}

void test_rec_list_and_rec_act_encode() {
  RecList l;
  l.filter = RecFilter::Starred;
  l.max = 6;
  TEST_ASSERT_EQUAL_STRING("{\"v\":0,\"t\":\"rec.list\",\"id\":\"d2\",\"filter\":\"starred\",\"max\":6}\n",
                           encode_line(make_message(MsgId::from("d2"), {}, l)).c_str());
  l.cursor = some(RecCursor::from("t1792300000000-a6"));
  TEST_ASSERT_EQUAL_STRING(
      "{\"v\":0,\"t\":\"rec.list\",\"id\":\"d3\",\"filter\":\"starred\",\"max\":6,\"cursor\":\"t1792300000000-a6\"}\n",
      encode_line(make_message(MsgId::from("d3"), {}, l)).c_str());
  RecAct a;
  a.rec = MonoIdStr::from("2B7XW4R-00003-0004");
  a.op = RecOp::Star;
  a.project = some(ProjectKey::from("cpa"));  // only tag carries a project
  const std::string head = "{\"v\":0,\"t\":\"rec.act\",\"id\":\"d4\",\"rec\":\"2B7XW4R-00003-0004\",\"op\":";
  TEST_ASSERT_EQUAL_STRING((head + "\"star\"}\n").c_str(), encode_line(make_message(MsgId::from("d4"), {}, a)).c_str());
  a.op = RecOp::Tag;
  TEST_ASSERT_EQUAL_STRING((head + "\"tag\",\"project\":\"cpa\"}\n").c_str(),
                           encode_line(make_message(MsgId::from("d4"), {}, a)).c_str());
  a.project = {};  // tag with no project: the key is required, sent as null
  const std::string none = encode_line(make_message(MsgId::from("d4"), {}, a));
  TEST_ASSERT_EQUAL_STRING((head + "\"tag\",\"project\":null}\n").c_str(), none.c_str());
  const Decoded& back = decode_line(none);
  TEST_ASSERT_TRUE(back.outcome == Outcome::Ok && back.reply_to.has);
  const RecAct& got = std::get<RecAct>(back.msg.body);
  TEST_ASSERT_TRUE(got.op == RecOp::Tag && !got.project.has && got.rec.equals("2B7XW4R-00003-0004"));
}

void test_rec_items_decode_into_a_bounded_page() {
  TEST_ASSERT_TRUE(sizeof(RecPage) < sizeof(FileData));  // no new buffer: Message does not grow
  const Decoded& d = decode_line(
      "{\"v\":0,\"t\":\"rec.items\",\"id\":\"h2\",\"re\":\"d2\",\"next\":\"t1792300000000-a6\",\"items\":["
      "{\"rec\":\"2B7XW4R-00003-0001\",\"mode\":\"meeting\",\"st\":\"done\",\"wall_ms\":1792447120000,\"dur_ms\":2530000,"
      "\"project\":\"cpa\",\"starred\":true,\"marks\":2,\"title\":\"\xE5\x91\xA8\xE4\xBC\x9A\",\"todo_n\":0},"
      "{\"rec\":\"3QZK1M7-00009-0001\",\"mode\":\"memo\",\"st\":\"on_device\"}]}");
  TEST_ASSERT_TRUE(d.outcome == Outcome::Ok && !d.reply_to.has);
  const RecPage& p = std::get<RecPage>(d.msg.body);
  TEST_ASSERT_EQUAL_UINT8(2, p.n);
  TEST_ASSERT_TRUE(p.next.has && p.next.value.equals("t1792300000000-a6"));
  const RecRow& a = p.rows[0];
  TEST_ASSERT_TRUE(a.rec.equals("2B7XW4R-00003-0001") && a.mode == RecMode::Meeting && a.st == RecState::Done);
  TEST_ASSERT_TRUE(a.wall_ms.has && a.wall_ms.value == 1792447120000LL && a.dur_ms.value == 2530000);
  TEST_ASSERT_TRUE(a.project.value.equals("cpa") && a.starred.value && a.marks.value == 2);
  TEST_ASSERT_TRUE(a.title.value.equals("\xE5\x91\xA8\xE4\xBC\x9A") && a.todo_n.has && a.todo_n.value == 0);
  const RecRow& b = p.rows[1];
  TEST_ASSERT_TRUE(b.mode == RecMode::Memo && b.st == RecState::OnDevice);
  TEST_ASSERT_FALSE(b.wall_ms.has || b.dur_ms.has || b.project.has || b.starred.has || b.marks.has || b.title.has ||
                    b.todo_n.has);
}

static RecRow worst_row(int i) {
  RecRow r;
  r.rec = MonoIdStr::from(("3QZK1M7-00009-000" + std::to_string(i)).c_str());
  r.mode = RecMode::Meeting;
  r.st = RecState::Processing;
  r.wall_ms = some<int64_t>(9007199254740991LL);
  r.dur_ms = some<uint32_t>(4294967295u);
  r.project = some(ProjectKey::from("abcdefghijkl"));
  r.starred = some(false);
  r.marks = some<uint16_t>(65535);
  r.title = some(RecTitle::from(std::string(48, '"').c_str()));  // each escapes to 2 bytes
  r.todo_n = some<uint16_t>(65535);
  return r;
}

// rec.items is the one host reply above host_reply_max: its worst line (README item 20,
// 1868 B) still fits line_max, and every field survives the round trip.
void test_rec_items_worst_line_fits_and_round_trips() {
  RecPage p;
  for (int i = 0; i < kRecListMax; ++i) p.rows[i] = worst_row(i + 1);
  p.n = kRecListMax;
  p.next = some(RecCursor::from(std::string(32, 'z').c_str()));
  const std::string id(24, 'h');
  const std::string line = encode_line(make_message(MsgId::from(id.c_str()), some(MsgId::from(id.c_str())), p));
  TEST_ASSERT_EQUAL_UINT(1868, line.size());
  const Decoded& back = decode_line(line);
  TEST_ASSERT_TRUE(back.outcome == Outcome::Ok);
  const RecPage& got = std::get<RecPage>(back.msg.body);
  TEST_ASSERT_EQUAL_UINT8(kRecListMax, got.n);
  const RecRow& r = got.rows[5];
  TEST_ASSERT_TRUE(r.rec.equals("3QZK1M7-00009-0006") && r.st == RecState::Processing && r.title.value.size() == 48);
  TEST_ASSERT_TRUE(r.wall_ms.value == 9007199254740991LL && r.dur_ms.value == 4294967295u && r.marks.value == 65535);
  TEST_ASSERT_TRUE(r.starred.has && !r.starred.value && r.todo_n.value == 65535 && got.next.value.size() == 32);
}

void test_v041_text_rules() {
  for (const char* ok : {"todo", "a", "a-", "daily", "ww.plugin.todoist", "a.1.-", "abcdefghijkl.12345678901"}) {
    TEST_ASSERT_TRUE_MESSAGE(is_dest(ok, std::strlen(ok)), ok);
  }
  for (const char* bad : {"", "Todo", "1todo", "-a", "a.", ".a", "a..b", "a.b.c.d", "abcdefghijklm", "a.bcdefghijklmn",
                          "a_b", "a b", "zzzzzzzzzzzz.yyyyyyyyyyyy"}) {
    TEST_ASSERT_FALSE_MESSAGE(is_dest(bad, std::strlen(bad)), bad);
  }
  const std::string c32(32, 'Z'), c33(33, 'z');
  TEST_ASSERT_TRUE(is_rec_cursor("t1792440000000-a1_B", 19) && is_rec_cursor(c32.data(), 32));
  TEST_ASSERT_FALSE(is_rec_cursor("", 0) || is_rec_cursor(c33.data(), 33) || is_rec_cursor("a/b", 3) ||
                    is_rec_cursor("a.b", 3) || is_rec_cursor("a=b", 3));
  TEST_ASSERT_TRUE(is_host_id("7f3a9c1e", 8) && is_host_id("0123456789abcdef-0123456789abcde", 32));
  TEST_ASSERT_FALSE(is_host_id("abc1234", 7) || is_host_id(c33.data(), 33) || is_host_id("7F3A9C1E", 8) ||
                    is_host_id("7f3a_9c1e", 9));
  TEST_ASSERT_TRUE(is_host_label("\xE5\x85\xAB\xE4\xB8\xAA\xE6\xB1\x89\xE5\xAD\x97\xE7\x9A\x84\xE4\xB8\xBB\xE6\x9C\xBA\xE5\x90\x8D", 24));
  TEST_ASSERT_FALSE(is_host_label("", 0) || is_host_label("aaaaaaaaaaaaaaaaaaaaaaaaa", 25) || is_host_label("a\tb", 3) ||
                    is_host_label("a\x7f", 2));
  const std::string t48(48, 'x');
  TEST_ASSERT_TRUE(is_rec_title(t48.data(), 48));
  TEST_ASSERT_FALSE(is_rec_title(c33.c_str(), 0) || is_rec_title((t48 + "x").data(), 49) || is_rec_title("a\nb", 3));
}
