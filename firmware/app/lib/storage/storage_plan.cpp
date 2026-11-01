#include "storage_plan.h"

#include "mono_id.h"

namespace cpa {
namespace {

constexpr uint32_t kManyShardsFrom = 1024u * 1024u;

bool legacy_table(const PartFacts& part) { return !part.launcher && part.legacy_spiffs && part.cpafs_bytes == 0; }

StoragePlan plan(Store primary, bool migrate, StoreNote note) {
  StoragePlan p;
  p.primary = primary;
  p.migrate = migrate;
  p.note = note;
  return p;
}

StoreNote internal_note(SdState sd) {
  switch (sd) {
    case SdState::Absent: return StoreNote::NoCard;
    case SdState::Unusable: return StoreNote::SdUnusable;
    default: return StoreNote::None;
  }
}

}  // namespace

StoragePlan plan_storage(const PartFacts& part, const SdFacts& sd, bool lfs_mounted) {
  const StoreNote missing = legacy_table(part) ? StoreNote::LegacyTable : StoreNote::None;
  if (sd.state == SdState::Ok) {
    if (lfs_mounted) return plan(Store::Sd, true, StoreNote::None);
    return plan(Store::Sd, false, missing != StoreNote::None ? missing : StoreNote::NoInternal);
  }
  if (lfs_mounted) return plan(Store::Internal, false, internal_note(sd.state));
  return plan(Store::None, false, missing != StoreNote::None ? missing : StoreNote::NoStorage);
}

uint8_t lfs_shards(uint32_t part_bytes) { return part_bytes >= kManyShardsFrom ? 32 : 4; }

char lfs_shard(char last_digit, uint8_t shards) {
  const Opt<uint64_t> v = crockford_decode(&last_digit, 1);
  if (!v.has) return 0;
  if (shards == 32) return last_digit;
  if (shards == 4) return static_cast<char>('0' + v.value % 4);
  return 0;
}

}  // namespace cpa
