#include "part_facts.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace cpa {
namespace {

const esp_partition_t* data_partition(const char* label) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
}

}  // namespace

PartFacts hal_part_facts() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* test = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, nullptr);
  const esp_partition_t* cpafs = data_partition("cpafs");
  PartFacts f;
  f.launcher = test && running && running->address != test->address;
  f.cpafs_bytes = cpafs ? cpafs->size : 0;
  f.legacy_spiffs = data_partition("spiffs") != nullptr;
  return f;
}

}  // namespace cpa
