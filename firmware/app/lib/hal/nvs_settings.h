// The device settings in NVS (docs/21 §6.8): namespace "cpa" (nvs_boot's), one key per
// setting, written only when the value changed. The NVS is shared with the Launcher and
// the other firmwares; their namespaces never meet ours. Device only.
#pragma once

#include "platform.h"
#include "settings.h"

namespace cpa {

Settings nvs_settings_load();  // the defaults for a key that is missing or unreadable
bool nvs_settings_save(const Settings& s);  // true: written and read back
// docs/28 S1a: keys "offb" / "offs" (u32 each), written only when they change.
Opt<OfflineMark> nvs_offline_mark_load();
bool nvs_offline_mark_save(const OfflineMark& m);

}  // namespace cpa
