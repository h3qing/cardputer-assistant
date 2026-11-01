// protocol_limits.h mirrors protocol/limits.json (05 §6.7): every key of the JSON file must be
// mirrored with the same value, and nothing extra may be mirrored.
#include <unity.h>

#include <cstring>
#include <string>

#include "protocol_limits.h"
#include "support/vectors.h"

using namespace cpa::limits;
using testsupport::load_json;
using testsupport::protocol_path;

void setUp() {}
void tearDown() {}

static void test_every_key_is_mirrored() {
  JsonDocument doc;
  TEST_ASSERT_TRUE(load_json(protocol_path("limits.json"), doc));
  size_t seen = 0;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    const char* key = kv.key().c_str();
    if (key[0] == '$') continue;
    const Mirror* m = find_mirror(key);
    TEST_ASSERT_NOT_NULL_MESSAGE(m, key);
    if (m->pattern) {
      TEST_ASSERT_EQUAL_STRING_MESSAGE(kv.value().as<const char*>(), m->pattern, key);
    } else {
      TEST_ASSERT_TRUE_MESSAGE(kv.value().is<int64_t>(), key);
      TEST_ASSERT_TRUE_MESSAGE(kv.value().as<int64_t>() == m->number, key);
    }
    ++seen;
  }
  TEST_ASSERT_EQUAL_UINT(kMirrorCount, seen);
}

static void test_spot_values() {
  TEST_ASSERT_EQUAL_UINT(2048, kLineMax);
  TEST_ASSERT_EQUAL_UINT(500, kTextMax);
  TEST_ASSERT_EQUAL_UINT(4096, kHostInflightMax);
  TEST_ASSERT_EQUAL_UINT(6, kRecListMax);  // v0.4.1
  TEST_ASSERT_EQUAL_UINT(24, kDestMax);
  TEST_ASSERT_NULL(find_mirror("no_such_limit"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_key_is_mirrored);
  RUN_TEST(test_spot_values);
  return UNITY_END();
}
