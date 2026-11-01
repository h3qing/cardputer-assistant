// The simulator's SHA-256 (the audio task hashes each segment's PCM with it, as the device
// does with mbedtls): FIPS 180-4 test vectors, and the same digest whatever the block sizes.
#include <unity.h>

#include <string>

#include "sha256.h"

using namespace cpasim;

void setUp() {}
void tearDown() {}

namespace {

const char* const kAbc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

std::string digest(const std::string& data, size_t step) {
  Sha256 h;
  h.begin();
  for (size_t at = 0; at < data.size(); at += step) {
    const size_t n = data.size() - at < step ? data.size() - at : step;
    h.update(reinterpret_cast<const uint8_t*>(data.data() + at), n);
  }
  return h.finish().c_str();
}

}  // namespace

static void test_fips_vectors() {
  TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", digest("", 1).c_str());
  TEST_ASSERT_EQUAL_STRING(kAbc, digest("abc", 1).c_str());
  TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                           digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 64).c_str());
}

static void test_a_million_a_in_any_blocks() {
  const std::string a(1000000, 'a');
  const char* want = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
  TEST_ASSERT_EQUAL_STRING(want, digest(a, 4096).c_str());
  TEST_ASSERT_EQUAL_STRING(want, digest(a, 63).c_str());
}

static void test_begin_starts_over() {
  Sha256 h;
  h.begin();
  h.update(reinterpret_cast<const uint8_t*>("xyz"), 3);
  h.begin();
  h.update(reinterpret_cast<const uint8_t*>("abc"), 3);
  TEST_ASSERT_EQUAL_STRING(kAbc, h.finish().c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fips_vectors);
  RUN_TEST(test_a_million_a_in_any_blocks);
  RUN_TEST(test_begin_starts_over);
  return UNITY_END();
}
