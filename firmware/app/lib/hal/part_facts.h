// Partition facts for plan_storage (docs/09-m2-design.md §4.3): collected, never judged.
//   launcher      = an APP/TEST partition exists and is not the one running (our own
//                   table has no test partition; the Launcher's app0 is one)
//   cpafs_bytes   = size of the data partition labelled "cpafs", 0 without one
//   legacy_spiffs = a data partition labelled "spiffs" exists (for a hint only: it is
//                   never mounted)
// Looked up by label at every call: the Launcher moves partitions, so no offset is kept.
// Device only.
#pragma once

#include "storage_plan.h"

namespace cpa {

PartFacts hal_part_facts();

}  // namespace cpa
