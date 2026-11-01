// SD card layout (docs/09-m2-design.md §3.5). Logical paths are the FileStore paths the
// outbox uses; physical paths are below the VFS mount point (/sd), under SdStore's root
// (/cpa, or a test root):
//   /outbox                          <root>/outbox
//   /outbox/<E7>-<B5>-<S4>.json      <root>/outbox/<E7>/<B5><S first 3>.J<S last>
//   /outbox/bad                      <root>/outbox/BAD
//   /outbox/bad/<E7>-<B5>-<S4>.json  <root>/outbox/BAD/<E7>/<B5><S first 3>.J<S last>
//   any *.tmp                        none: the FAT commit protocol never writes one
//   any other /x/y                   <root>/x/y
// Crockford digits (0-9, A-Z without I L O U) are valid 8.3 characters, so the item names
// are upper-case 8.3 names for which FatFs writes no long-file-name entry. One directory
// per epoch: the epoch only changes when NVS is re-initialised, so a card holds one or
// two, each created once.
#pragma once

#include <cstddef>

#include "fixed_str.h"
#include "opt.h"

namespace cpa {

constexpr size_t kSdPathMax = 63;  // bytes of a physical or logical path
constexpr size_t kSdRootMax = 15;  // "/" + up to 14 bytes, no trailing "/"

using SdPath = FixedStr<kSdPathMax>;
using SdItemName = FixedStr<23>;  // "<MonoId>.json"

Opt<SdPath> sd_physical(const char* root, const char* logical);
Opt<SdPath> sd_logical(const char* root, const char* physical);

// An epoch directory name: 7 upper-case Crockford digits whose value fits 32 bits.
bool sd_is_epoch_dir(const char* name);
// "<MonoId>.json" for the 8.3 item file `name` in epoch directory `epoch`; absent for
// any other name (lower case, long names, other extensions).
Opt<SdItemName> sd_item_name(const char* epoch, const char* name);

}  // namespace cpa
