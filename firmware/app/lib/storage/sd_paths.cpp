#include "sd_paths.h"

#include <cstring>

#include "mono_id.h"

namespace cpa {
namespace {

constexpr char kOutboxDir[] = "/outbox/";
constexpr char kBadLogical[] = "/outbox/bad";
constexpr char kBadLogicalDir[] = "/outbox/bad/";
constexpr char kBadPhysical[] = "/outbox/BAD";
constexpr char kBadPhysicalDir[] = "/outbox/BAD/";
constexpr char kJson[] = ".json";
constexpr char kTmp[] = ".tmp";
constexpr size_t kEpochLen = 7;
constexpr size_t kShortLen = 11;  // "<B5><S3>.J<S1>"
constexpr size_t kItemLen = kMonoIdLen + sizeof(kJson) - 1;

using ShortForm = FixedStr<kEpochLen + 1 + kShortLen>;  // "<E7>/<8.3 name>"

bool starts_with(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

bool ends_with(const char* s, size_t n, const char* suffix) {
  const size_t k = std::strlen(suffix);
  return n >= k && std::memcmp(s + n - k, suffix, k) == 0;
}

bool valid_root(const char* root) {
  if (!root || root[0] != '/') return false;
  const size_t n = std::strlen(root);
  return n >= 2 && n <= kSdRootMax && root[n - 1] != '/';
}

// True when `name` is exactly "<MonoId>.json".
bool is_item(const char* name) {
  const size_t n = std::strlen(name);
  return n == kItemLen && ends_with(name, n, kJson) && mono_parse(name, kMonoIdLen).has;
}

// "<E7>/<B5><S first 3>.J<S last>" of the item name "<E7>-<B5>-<S4>.json".
ShortForm short_form(const char* item) {
  return ShortForm::from(item, kEpochLen).append("/").append(item + 8, 5).append(item + 14, 3).append(".J").append(item + 17, 1);
}

Opt<SdPath> join(const char* a, const char* b, const char* c = "") {
  const size_t n = std::strlen(a) + std::strlen(b) + std::strlen(c);
  if (n > kSdPathMax) return {};
  return some(SdPath::from(a).append(b).append(c));
}

// The logical item path for "<E7>/<8.3 name>" below `dir`, if `rest` is one.
Opt<SdPath> item_path(const char* dir, const char* rest) {
  if (std::strlen(rest) != kEpochLen + 1 + kShortLen || rest[kEpochLen] != '/') return {};
  char epoch[kEpochLen + 1] = {};
  std::memcpy(epoch, rest, kEpochLen);
  const Opt<SdItemName> name = sd_item_name(epoch, rest + kEpochLen + 1);
  if (!name.has) return {};
  return join(dir, name.value.c_str());
}

}  // namespace

Opt<SdPath> sd_physical(const char* root, const char* logical) {
  if (!valid_root(root) || !logical || logical[0] != '/') return {};
  if (ends_with(logical, std::strlen(logical), kTmp)) return {};
  if (std::strcmp(logical, kBadLogical) == 0) return join(root, kBadPhysical);
  if (starts_with(logical, kBadLogicalDir) && is_item(logical + sizeof(kBadLogicalDir) - 1)) {
    return join(root, kBadPhysicalDir, short_form(logical + sizeof(kBadLogicalDir) - 1).c_str());
  }
  if (starts_with(logical, kOutboxDir) && is_item(logical + sizeof(kOutboxDir) - 1)) {
    return join(root, kOutboxDir, short_form(logical + sizeof(kOutboxDir) - 1).c_str());
  }
  return join(root, logical);
}

Opt<SdPath> sd_logical(const char* root, const char* physical) {
  if (!valid_root(root) || !physical || std::strlen(physical) > kSdPathMax) return {};
  const size_t rn = std::strlen(root);
  if (std::strncmp(physical, root, rn) != 0 || physical[rn] != '/') return {};
  const char* rest = physical + rn;
  if (std::strcmp(rest, kBadPhysical) == 0) return join(kBadLogical, "");
  Opt<SdPath> item;
  if (starts_with(rest, kBadPhysicalDir)) {
    item = item_path(kBadLogicalDir, rest + sizeof(kBadPhysicalDir) - 1);
  } else if (starts_with(rest, kOutboxDir)) {
    item = item_path(kOutboxDir, rest + sizeof(kOutboxDir) - 1);
  }
  return item.has ? item : join(rest, "");
}

bool sd_is_epoch_dir(const char* name) {
  if (!name || std::strlen(name) != kEpochLen) return false;
  const Opt<uint64_t> v = crockford_decode(name, kEpochLen);
  return v.has && v.value <= 0xFFFFFFFFull;
}

Opt<SdItemName> sd_item_name(const char* epoch, const char* name) {
  if (!sd_is_epoch_dir(epoch) || !name || std::strlen(name) != kShortLen) return {};
  if (name[8] != '.' || name[9] != 'J') return {};
  const auto id = MonoIdStr::from(epoch, kEpochLen).append("-").append(name, 5).append("-").append(name + 5, 3).append(name + 10, 1);
  if (!mono_parse(id.c_str(), id.size()).has) return {};
  return some(SdItemName::from(id.c_str(), id.size()).append(kJson));
}

}  // namespace cpa
