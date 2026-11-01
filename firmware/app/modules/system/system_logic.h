// The device page's logic (04 US-16, docs/09-m2-design.md §8, docs/21 §4.5): its rows as
// text in the shown language, pure. Under the tab pills there are 8 rows of 12 px:
//   固件 <fw>  rst <reason>              FW ...
//   dev <dev>  boot <boot id>
//   启动 直接|Launcher  daemon <app>|--    Boot direct|Launcher ...
//   存储 SD 476G|内部 1.5M|无  剩 412G     Store ... free 412G (or the boot's migration counts)
//   outbox / 堆 / 电池 + 栈余 rows          outbox / Heap / Bat + stack
//   one warning row, when there is one: storage errors, the storage note, NVS, invariant,
//   modules, and the keyboard's ghost / re-init counts last (7 fixed rows and this one: it always fits)
// How long since the Mac last spoke and what waits to sync are on the settings page.
#pragma once

#include <cstdint>

#include "module.h"

namespace cpa {

constexpr uint8_t kSystemLines = 8;
using SystemLine = FixedStr<96>;

struct SystemPage {
  uint8_t n = 0;
  SystemLine lines[kSystemLines];
  bool warn[kSystemLines] = {};
  uint8_t key_len[kSystemLines] = {};  // bytes of the row's label (its first word); 0: the warning row
};

SystemPage system_page(const AppState& s, const SysInfo& info);
FixedStr<16> kib_text(uint32_t bytes);  // "180K" / "6.0K"
FixedStr<16> size_text(uint64_t bytes);  // "448K" / "1.5M" / "476G": K, M, G or T
const char* store_note_text(StoreNote n);  // "" for StoreNote::None

}  // namespace cpa
