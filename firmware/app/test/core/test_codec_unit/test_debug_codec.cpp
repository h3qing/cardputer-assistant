// debug.state.diag (gh #21, protocol README item 21, test firmware): the codec side. The
// vectors cover the schema's values and errors; here the exact bytes and the round trip.
#include <unity.h>

#include <string>

#include "codec.h"
#include "errors.h"

using namespace cpa;
using namespace cpa::proto;

static Codec g_diag_codec;

static Decoded dec(const std::string& line) { return g_diag_codec.decode(line.data(), line.size()); }

static void assert_error(const char* line, ErrCode code, const char* field) {
  const Decoded d = dec(line);
  TEST_ASSERT_TRUE_MESSAGE(d.outcome == Outcome::Error, line);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(err_name(code), err_name(d.code), line);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(field, d.field.c_str(), line);
}

void test_debug_state_diag_is_optional_and_last() {
  const std::string head =
      "{\"v\":0,\"t\":\"debug.state\",\"id\":\"d71\",\"re\":\"h71\",\"up_ms\":9,\"link\":\"Ready\",\"mod\":\"home\","
      "\"overlay\":\"none\",\"outbox\":{\"pending\":0,\"inflight\":0,\"bad\":0},\"revs\":{\"projects\":0,\"sessions\":0,"
      "\"queue\":0},\"need\":0,\"projects\":[],\"ui\":{\"menu\":false,\"locked\":false}";
  DebugState s;
  s.summary = true;
  s.up_ms = 9;
  s.link = LinkMode::Ready;
  s.mod = FixedStr<16>::from("home");
  s.ui = some(DebugUi{});
  const auto plain = g_diag_codec.encode(make_message(MsgId::from("d71"), some(MsgId::from("h71")), s));
  TEST_ASSERT_EQUAL_STRING((head + "}\n").c_str(), plain.value.c_str());
  const DebugDiag g{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 65535};
  s.diag = some(g);
  const auto with = g_diag_codec.encode(make_message(MsgId::from("d71"), some(MsgId::from("h71")), s));
  const std::string diag =
      ",\"diag\":{\"heap_free\":1,\"heap_min\":2,\"heap_block\":3,\"stack_left\":4,\"audio_stack_left\":5,\"loop_n\":6,"
      "\"loop_max_us\":7,\"loop_win_max_us\":8,\"gap_max_us\":9,\"gap_win_max_us\":10,\"loop_over_20\":11,"
      "\"loop_over_50\":12,\"loop_over_100\":13,\"loop_over_500\":14,\"ghost_n\":15,\"kb_reinit_n\":65535}";
  TEST_ASSERT_EQUAL_STRING((head + diag + "}\n").c_str(), with.value.c_str());
  const Decoded d = dec(head + diag + "}");
  TEST_ASSERT_TRUE(d.outcome == Outcome::Ok);
  const auto& back = std::get<DebugState>(d.msg.body);
  TEST_ASSERT_TRUE(back.diag.has);
  TEST_ASSERT_EQUAL_UINT32(1, back.diag.value.heap_free);
  TEST_ASSERT_EQUAL_UINT32(10, back.diag.value.gap_win_max_us);
  TEST_ASSERT_EQUAL_UINT32(65535, back.diag.value.kb_reinit_n);
  TEST_ASSERT_FALSE(std::get<DebugState>(dec(head + ",\"diag\":null}").msg.body).diag.has);
  assert_error((head + ",\"diag\":{\"heap_free\":1}}").c_str(), ErrCode::BadField, "diag.heap_min");
  assert_error((head + ",\"diag\":7}").c_str(), ErrCode::BadField, "diag");
}

// debug.lang (v0.4.3, protocol README item 25, docs/21 §7): {lang: "zh" | "en"}, a request,
// ack. The vectors cover the schema's errors; here the decoded value and the exact bytes.
void test_debug_lang_decodes_and_encodes() {
  const Decoded d = dec("{\"v\":0,\"t\":\"debug.lang\",\"id\":\"h90\",\"lang\":\"en\"}");
  TEST_ASSERT_TRUE(d.outcome == Outcome::Ok && d.reply_to.has);
  const auto* l = std::get_if<DebugLang>(&d.msg.body);
  TEST_ASSERT_TRUE(l && l->lang == Lang::En);
  TEST_ASSERT_TRUE(DebugLang{}.lang == Lang::En);  // the default, as Settings{} (v0.5.2)
  const auto line = g_diag_codec.encode(make_message(MsgId::from("h90"), {}, DebugLang{Lang::Zh}));
  TEST_ASSERT_TRUE(line.ok);
  TEST_ASSERT_EQUAL_STRING("{\"v\":0,\"t\":\"debug.lang\",\"id\":\"h90\",\"lang\":\"zh\"}\n", line.value.c_str());
  assert_error("{\"v\":0,\"t\":\"debug.lang\",\"id\":\"h90\",\"lang\":\"fr\"}", ErrCode::BadField, "lang");
  assert_error("{\"v\":0,\"t\":\"debug.lang\",\"id\":\"h90\"}", ErrCode::BadField, "lang");
}

// debug.state.ui.lang: after locked when set; a v0.4.0 ui (no lang) is unchanged.
void test_debug_state_ui_lang_is_optional_and_last() {
  const std::string head =
      "{\"v\":0,\"t\":\"debug.state\",\"id\":\"d71\",\"re\":\"h71\",\"up_ms\":9,\"link\":\"Ready\",\"mod\":\"system\","
      "\"overlay\":\"none\",\"outbox\":{\"pending\":0,\"inflight\":0,\"bad\":0},\"revs\":{\"projects\":0,\"sessions\":0,"
      "\"queue\":0},\"need\":0,\"projects\":[],\"ui\":{\"menu\":false,\"locked\":false";
  DebugState s;
  s.summary = true;
  s.up_ms = 9;
  s.link = LinkMode::Ready;
  s.mod = FixedStr<16>::from("system");
  s.ui = some(DebugUi{});
  const auto old = g_diag_codec.encode(make_message(MsgId::from("d71"), some(MsgId::from("h71")), s));
  TEST_ASSERT_EQUAL_STRING((head + "}}\n").c_str(), old.value.c_str());
  DebugUi ui;
  ui.lang = some(Lang::En);
  s.ui = some(ui);
  const auto with = g_diag_codec.encode(make_message(MsgId::from("d71"), some(MsgId::from("h71")), s));
  TEST_ASSERT_EQUAL_STRING((head + ",\"lang\":\"en\"}}\n").c_str(), with.value.c_str());
  const Decoded back = dec(with.value.c_str());
  const auto* st = std::get_if<DebugState>(&back.msg.body);
  TEST_ASSERT_TRUE(st && st->ui.has && st->ui.value.lang.has && st->ui.value.lang.value == Lang::En);
  assert_error((head + ",\"lang\":\"fr\"}}").c_str(), ErrCode::BadField, "ui.lang");
}
