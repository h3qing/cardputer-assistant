// SD card layout (docs/09-m2-design.md §3.5): logical FileStore paths <-> physical paths
// below the mount point. Outbox items become 8.3 names flat per epoch:
//   /outbox/<E7>-<B5>-<S4>.json      <-> <root>/outbox/<E7>/<B5><S first 3>.J<S last>
//   /outbox/bad/<E7>-<B5>-<S4>.json  <-> <root>/outbox/BAD/<E7>/<B5><S first 3>.J<S last>
// .tmp has no physical form (the FAT commit protocol never writes one); any other path
// is the logical one under the root.
#include <unity.h>

#include <cstring>
#include <string>

#include "sd_paths.h"

using namespace cpa;

void setUp() {}
void tearDown() {}

static std::string phys(const char* logical, const char* root = "/cpa") {
  const Opt<SdPath> p = sd_physical(root, logical);
  return p.has ? std::string(p.value.c_str()) : std::string("<none>");
}

static std::string logi(const char* physical, const char* root = "/cpa") {
  const Opt<SdPath> p = sd_logical(root, physical);
  return p.has ? std::string(p.value.c_str()) : std::string("<none>");
}

static bool is_83(const std::string& name) {
  const auto dot = name.find('.');
  if (dot == std::string::npos || dot == 0 || dot > 8 || name.size() - dot - 1 > 3) return false;
  for (size_t i = 0; i < name.size(); ++i) {
    const char c = name[i];
    const bool ok = i == dot || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
    if (!ok) return false;
  }
  return true;
}

static void test_items_map_to_83_names_per_epoch() {
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/1K3M9Q2/000420A1.JC", phys("/outbox/1K3M9Q2-00042-0A1C.json").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/BAD/1K3M9Q2/000420A1.JC", phys("/outbox/bad/1K3M9Q2-00042-0A1C.json").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/0000000/00000000.J0", phys("/outbox/0000000-00000-0000.json").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/3ZZZZZZ/ZZZZZZZZ.JZ", phys("/outbox/3ZZZZZZ-ZZZZZ-ZZZZ.json").c_str());
}

static void test_directories_and_other_paths() {
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox", phys("/outbox").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/BAD", phys("/outbox/bad").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/cfg/projects.json", phys("/cfg/projects.json").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/cfg", phys("/cfg").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/readme.txt", phys("/outbox/readme.txt").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpatest/outbox/1K3M9Q2/000420A1.JC",
                           phys("/outbox/1K3M9Q2-00042-0A1C.json", "/cpatest").c_str());
}

// Not a MonoId: no 8.3 form, so the name is kept as an ordinary (long) file name.
static void test_invalid_ids_stay_ordinary_names() {
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/1K3M9Q2-00042-0A1I.json", phys("/outbox/1K3M9Q2-00042-0A1I.json").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/1k3m9q2-00042-0a1c.json", phys("/outbox/1k3m9q2-00042-0a1c.json").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/4000000-00042-0A1C.json", phys("/outbox/4000000-00042-0A1C.json").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/1K3M9Q2-00042-0A1C.jsn", phys("/outbox/1K3M9Q2-00042-0A1C.jsn").c_str());
  TEST_ASSERT_EQUAL_STRING("/cpa/outbox/x/1K3M9Q2-00042-0A1C.json", phys("/outbox/x/1K3M9Q2-00042-0A1C.json").c_str());
}

static void test_tmp_has_no_physical_form() {
  TEST_ASSERT_EQUAL_STRING("<none>", phys("/outbox/1K3M9Q2-00042-0A1C.tmp").c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", phys("/outbox/bad/1K3M9Q2-00042-0A1C.tmp").c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", phys("/cfg/projects.tmp").c_str());
}

static void test_bad_input_has_no_mapping() {
  TEST_ASSERT_EQUAL_STRING("<none>", phys("outbox").c_str());  // not absolute
  TEST_ASSERT_EQUAL_STRING("<none>", phys("").c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", phys(nullptr).c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", phys("/outbox", "cpa").c_str());  // bad roots
  TEST_ASSERT_EQUAL_STRING("<none>", phys("/outbox", "/").c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", phys("/outbox", "/cpa/").c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", phys("/outbox", nullptr).c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", phys("/outbox", "/a-root-too-long").c_str());  // 16 bytes
  TEST_ASSERT_EQUAL_STRING("/a-root-max-15b/outbox", phys("/outbox", "/a-root-max-15b").c_str());
}

static void test_overlong_paths_have_no_mapping() {
  const std::string fits = "/" + std::string(kSdPathMax - 5, 'a');  // "/cpa" + it = kSdPathMax
  TEST_ASSERT_EQUAL_STRING(("/cpa" + fits).c_str(), phys(fits.c_str()).c_str());
  const std::string over = fits + "b";
  TEST_ASSERT_EQUAL_STRING("<none>", phys(over.c_str()).c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", logi(("/cpa" + over).c_str()).c_str());
}

static void test_physical_back_to_logical() {
  TEST_ASSERT_EQUAL_STRING("/outbox/1K3M9Q2-00042-0A1C.json", logi("/cpa/outbox/1K3M9Q2/000420A1.JC").c_str());
  TEST_ASSERT_EQUAL_STRING("/outbox/bad/1K3M9Q2-00042-0A1C.json", logi("/cpa/outbox/BAD/1K3M9Q2/000420A1.JC").c_str());
  TEST_ASSERT_EQUAL_STRING("/outbox/bad", logi("/cpa/outbox/BAD").c_str());
  TEST_ASSERT_EQUAL_STRING("/outbox", logi("/cpa/outbox").c_str());
  TEST_ASSERT_EQUAL_STRING("/cfg/projects.json", logi("/cpa/cfg/projects.json").c_str());
  TEST_ASSERT_EQUAL_STRING("/outbox/1K3M9Q2", logi("/cpa/outbox/1K3M9Q2").c_str());  // epoch dir itself
  TEST_ASSERT_EQUAL_STRING("<none>", logi("/cpatest/outbox").c_str());               // outside the root
  TEST_ASSERT_EQUAL_STRING("<none>", logi("/cp/outbox").c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", logi("/cpa").c_str());
  TEST_ASSERT_EQUAL_STRING("<none>", logi(nullptr).c_str());
}

// Lower case and long names in an epoch directory were not written by us: they map back
// to themselves (an ordinary path), never to an item.
static void test_foreign_names_are_not_items() {
  TEST_ASSERT_EQUAL_STRING("/outbox/1K3M9Q2/000420a1.jc", logi("/cpa/outbox/1K3M9Q2/000420a1.jc").c_str());
  TEST_ASSERT_EQUAL_STRING("/outbox/1K3M9Q2/000420A1.JSON", logi("/cpa/outbox/1K3M9Q2/000420A1.JSON").c_str());
  TEST_ASSERT_EQUAL_STRING("/outbox/1K3M9Q2/1K3M9Q2-00042-0A1C.json",
                           logi("/cpa/outbox/1K3M9Q2/1K3M9Q2-00042-0A1C.json").c_str());
  TEST_ASSERT_EQUAL_STRING("/outbox/1K3M9Q2/000420A1.TC", logi("/cpa/outbox/1K3M9Q2/000420A1.TC").c_str());
  TEST_ASSERT_EQUAL_STRING("/outbox/1k3m9q2/000420A1.JC", logi("/cpa/outbox/1k3m9q2/000420A1.JC").c_str());
  TEST_ASSERT_EQUAL_STRING("/outbox/4000000/000420A1.JC", logi("/cpa/outbox/4000000/000420A1.JC").c_str());
}

static void test_item_and_epoch_names() {
  TEST_ASSERT_TRUE(sd_is_epoch_dir("1K3M9Q2"));
  TEST_ASSERT_TRUE(sd_is_epoch_dir("3ZZZZZZ"));  // 2^32 - 1
  TEST_ASSERT_FALSE(sd_is_epoch_dir("4000000"));  // 2^32: more than 32 bits
  TEST_ASSERT_FALSE(sd_is_epoch_dir("K3M9Q2"));
  TEST_ASSERT_FALSE(sd_is_epoch_dir("1K3M9Q2X"));
  TEST_ASSERT_FALSE(sd_is_epoch_dir("1k3m9q2"));
  TEST_ASSERT_FALSE(sd_is_epoch_dir("BAD"));
  TEST_ASSERT_FALSE(sd_is_epoch_dir(nullptr));
  const Opt<SdItemName> n = sd_item_name("1K3M9Q2", "000420A1.JC");
  TEST_ASSERT_TRUE(n.has);
  TEST_ASSERT_EQUAL_STRING("1K3M9Q2-00042-0A1C.json", n.value.c_str());
  TEST_ASSERT_FALSE(sd_item_name("1K3M9Q2", "000420A1.JCX").has);
  TEST_ASSERT_FALSE(sd_item_name("1K3M9Q2", "000420A.JC").has);
  TEST_ASSERT_FALSE(sd_item_name("1K3M9Q2", "000420A1.KC").has);
  TEST_ASSERT_FALSE(sd_item_name("1K3M9Q2", "000420A1_JC").has);
  TEST_ASSERT_FALSE(sd_item_name("1K3M9Q2", "000420AI.JC").has);
  TEST_ASSERT_FALSE(sd_item_name("4000000", "000420A1.JC").has);
  TEST_ASSERT_FALSE(sd_item_name(nullptr, "000420A1.JC").has);
  TEST_ASSERT_FALSE(sd_item_name("1K3M9Q2", nullptr).has);
}

// Every item path round-trips, and every generated name is a valid upper-case 8.3 name
// (so FatFs writes no long-file-name entry).
static void test_round_trip_and_83_validity() {
  const char* digits = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
  for (int i = 0; i < 32; ++i) {
    std::string id = "1K3M9Q2-0004" + std::string(1, digits[i]) + "-0A1" + std::string(1, digits[31 - i]);
    for (const char* dir : {"/outbox/", "/outbox/bad/"}) {
      const std::string logical = dir + id + ".json";
      const std::string p = phys(logical.c_str());
      TEST_ASSERT_TRUE(is_83(p.substr(p.rfind('/') + 1)));
      TEST_ASSERT_EQUAL_STRING(logical.c_str(), logi(p.c_str()).c_str());
    }
  }
  for (const char* other : {"/outbox", "/outbox/bad", "/cfg/projects.json", "/rec"}) {
    TEST_ASSERT_EQUAL_STRING(other, logi(phys(other).c_str()).c_str());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_items_map_to_83_names_per_epoch);
  RUN_TEST(test_directories_and_other_paths);
  RUN_TEST(test_invalid_ids_stay_ordinary_names);
  RUN_TEST(test_tmp_has_no_physical_form);
  RUN_TEST(test_bad_input_has_no_mapping);
  RUN_TEST(test_overlong_paths_have_no_mapping);
  RUN_TEST(test_physical_back_to_logical);
  RUN_TEST(test_foreign_names_are_not_items);
  RUN_TEST(test_item_and_epoch_names);
  RUN_TEST(test_round_trip_and_83_validity);
  return UNITY_END();
}
