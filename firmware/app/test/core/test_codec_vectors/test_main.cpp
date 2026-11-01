// Contract test (05 §9.1, protocol/README): the firmware codec decodes every vector in
// protocol/vectors/v0/messages exactly as tools/wire.py does. Valid cases decode to ok
// and re-encode to the same message (semantic compare); invalid cases yield the
// declared code and, for E_BAD_FIELD, the declared field path.
#include <unity.h>

#include <memory>
#include <string>

#include "codec.h"
#include "errors.h"
#include "key_names.h"
#include "support/vectors.h"

using namespace cpa;
using namespace cpa::proto;
using namespace testsupport;

void setUp() {}
void tearDown() {}

static std::unique_ptr<Codec> g_codec;

static std::string type_of_file(const std::string& name) { return name.substr(0, name.size() - 5); }

static void check_valid(const std::string& type, const std::string& label, JsonObjectConst c) {
  const std::string raw = case_line(c);
  const Decoded d = g_codec->decode(raw.data(), raw.size());
  TEST_ASSERT_TRUE_MESSAGE(d.outcome == Outcome::Ok, (label + " -> " + err_name(d.code) + " " + d.field.c_str()).c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(type.c_str(), type_name(msg_type(d.msg)), label.c_str());
  const bool request = c["msg"]["re"].isNull();
  TEST_ASSERT_EQUAL_MESSAGE(request, d.reply_to.has, (label + ": reply_to").c_str());
  const auto line = g_codec->encode(d.msg);
  TEST_ASSERT_TRUE_MESSAGE(line.ok, (label + ": encode").c_str());
  JsonDocument back;
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(back, line.value.c_str(), line.value.size()), label.c_str());
  std::string where;
  TEST_ASSERT_TRUE_MESSAGE(json_equal(c["msg"], back.as<JsonVariantConst>(), where), (label + " differs at " + where + ": " + line.value.c_str()).c_str());
}

static void check_invalid(const std::string& type, JsonObjectConst c) {
  const std::string label = type + ": " + c["name"].as<std::string>();
  const std::string raw = case_line(c);
  const Decoded d = g_codec->decode(raw.data(), raw.size());
  const std::string want = c["code"].as<std::string>();
  const std::string got = d.outcome == Outcome::Error ? err_name(d.code) : "not an error";
  TEST_ASSERT_EQUAL_STRING_MESSAGE(want.c_str(), got.c_str(), label.c_str());
  if (!c["field"].isNull()) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c["field"].as<const char*>(), d.field.c_str(), label.c_str());
  }
}

static void test_every_message_vector() {
  const auto files = list_dir(protocol_path("vectors/v0/messages"), ".json");
  TEST_ASSERT_EQUAL_UINT(kMsgTypeCount, files.size());
  size_t valid = 0, invalid = 0;
  for (const auto& file : files) {
    JsonDocument doc;
    TEST_ASSERT_TRUE_MESSAGE(load_json(protocol_path("vectors/v0/messages/" + file), doc), file.c_str());
    const std::string type = type_of_file(file);
    TEST_ASSERT_TRUE_MESSAGE(type_parse(type.c_str(), type.size()).has, type.c_str());
    for (JsonObjectConst c : doc["valid"].as<JsonArrayConst>()) {
      check_valid(type, type + ": " + c["name"].as<std::string>(), c), ++valid;
    }
    for (JsonObjectConst c : doc["invalid"].as<JsonArrayConst>()) check_invalid(type, c), ++invalid;
  }
  printf("vectors: %u valid, %u invalid\n", static_cast<unsigned>(valid), static_cast<unsigned>(invalid));
  TEST_ASSERT_TRUE(valid >= kMsgTypeCount && invalid >= kMsgTypeCount);
}

// flows/app (v0.4.1): the recordings app replays these flows (V41-R/V41-F); the link core
// cannot send rec.*. Here every line of them, either direction, must decode on the
// firmware codec and re-encode to the same message.
static void test_every_app_flow_line() {
  const auto files = list_dir(protocol_path("vectors/v0/flows/app"), ".json");
  TEST_ASSERT_TRUE(files.size() >= 3);
  size_t lines = 0;
  for (const auto& file : files) {
    JsonDocument doc;
    TEST_ASSERT_TRUE_MESSAGE(load_json(protocol_path("vectors/v0/flows/app/" + file), doc), file.c_str());
    for (JsonObjectConst step : doc["steps"].as<JsonArrayConst>()) {
      if (step["msg"].isNull()) continue;
      const std::string label = file + " t=" + std::to_string(step["t"].as<long>());
      TEST_ASSERT_TRUE_MESSAGE(step["outcome"].isNull(), label.c_str());
      check_valid(step["msg"]["t"].as<std::string>(), label, step), ++lines;
    }
  }
  printf("app flows: %u lines\n", static_cast<unsigned>(lines));
}

static void test_every_type_has_a_schema() {
  for (uint8_t i = 0; i < kMsgTypeCount; ++i) {
    const std::string name = type_name(static_cast<MsgType>(i));
    const std::string schema = read_file(protocol_path("schema/v0/" + name + ".schema.json"));
    TEST_ASSERT_FALSE_MESSAGE(schema.empty(), name.c_str());
  }
  TEST_ASSERT_EQUAL_UINT(kMsgTypeCount + 1, list_dir(protocol_path("schema/v0"), ".schema.json").size());
}

static JsonArrayConst common_enum(JsonDocument& doc, const char* def) {
  TEST_ASSERT_TRUE(load_json(protocol_path("schema/v0/common.schema.json"), doc));
  return doc["$defs"][def]["enum"].as<JsonArrayConst>();
}

static void test_error_codes_match_schema() {
  JsonDocument doc;
  const auto codes = common_enum(doc, "errorCode");
  TEST_ASSERT_EQUAL_UINT(kErrCodeCount - 1, codes.size());
  uint8_t i = 1;
  for (JsonVariantConst v : codes) {
    TEST_ASSERT_EQUAL_STRING(v.as<const char*>(), err_name(static_cast<ErrCode>(i)));
    const auto parsed = err_parse(v.as<const char*>(), std::strlen(v.as<const char*>()));
    TEST_ASSERT_TRUE(parsed.has && parsed.value == static_cast<ErrCode>(i));
    ++i;
  }
}

static void test_key_names_match_schema() {
  JsonDocument doc;
  const auto keys = common_enum(doc, "keyName");
  TEST_ASSERT_EQUAL_UINT(kKeyCount, keys.size());
  uint8_t i = 0;
  for (JsonVariantConst v : keys) TEST_ASSERT_EQUAL_STRING(v.as<const char*>(), key_name(i++));
}

int main() {
  g_codec.reset(new Codec());
  UNITY_BEGIN();
  RUN_TEST(test_every_message_vector);
  RUN_TEST(test_every_app_flow_line);
  RUN_TEST(test_every_type_has_a_schema);
  RUN_TEST(test_error_codes_match_schema);
  RUN_TEST(test_key_names_match_schema);
  return UNITY_END();
}
