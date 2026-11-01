// SD write-failure diagnostics (docs/gates/sd-card.md §11), shared by sd_diag.cpp
// (!sddiag !sdbusy !sdid !sdblk) and sd_f3.cpp (!sdf3 !sdfile).
#pragma once

#include <Arduino.h>

namespace sdx {

// The card initialised through the driver with no file system mounted.
struct Raw {
    uint8_t pdrv = 0xFF;
    bool ok = false;
    uint32_t sectors = 0;
};
bool rawOpen(Raw& r, uint32_t hz);  // unmounts /sd first
void rawClose(Raw& r);              // sdcard_uninit (CMD0)
void printDiag();                   // appends "diag":{...} to the reply (sdt::jcat)

}  // namespace sdx

bool sdDiagCommand(const String& line);  // !sddiag !sdbusy !sdid !sdblk
bool sdF3Command(const String& line);    // !sdf3 !sdfile
