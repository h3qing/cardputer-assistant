// NVS boot record (05 §5.9): epoch (random + MAC, set when NVS is first initialised)
// and the boot counter, advanced and written back BEFORE any MonoId is issued. The
// ticket is valid only when the read-back equals the advanced record. Device only.
#pragma once

#include "mono_id.h"

namespace cpa {

BootTicket nvs_boot_ticket();
BootRecord nvs_boot_read();  // for the device test: what NVS holds now

}  // namespace cpa
