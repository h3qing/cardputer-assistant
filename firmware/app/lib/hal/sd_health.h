// sd_health (docs/11 §4.7, 09 §3.8): one flag for the card shared by the loop's
// VfsFatPort and the audio task's VfsRecFilePort. The first EIO / ENODEV / ENXIO on either
// side sets it; from then on both answer Disk without touching the bus. SdStore (pure)
// latches on the same error by itself. Device only.
#pragma once

namespace cpa {

bool hal_sd_dead();
void hal_sd_mark_dead();
void hal_sd_reset_health();  // device tests that mount the card again

}  // namespace cpa
