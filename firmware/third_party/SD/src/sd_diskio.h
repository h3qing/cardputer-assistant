// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef _SD_DISKIO_H_
#define _SD_DISKIO_H_

#include "Arduino.h"
#include "SPI.h"
#include "sd_defines.h"
// #include "diskio.h"

uint8_t sdcard_init(uint8_t cs, SPIClass * spi, int hz);
uint8_t sdcard_uninit(uint8_t pdrv);

bool sdcard_mount(uint8_t pdrv, const char* path, uint8_t max_files, bool format_if_empty);
uint8_t sdcard_unmount(uint8_t pdrv);

sdcard_type_t sdcard_type(uint8_t pdrv);
uint32_t sdcard_num_sectors(uint8_t pdrv);
uint32_t sdcard_sector_size(uint8_t pdrv);
bool sd_read_raw(uint8_t pdrv, uint8_t* buffer, uint32_t sector);
bool sd_write_raw(uint8_t pdrv, uint8_t* buffer, uint32_t sector);

// CPA patch 4 (docs/gates/sd-card.md §11): diagnostics. Records what the driver saw at
// its error points and how long the card held DO low (busy). The only bus traffic it
// adds: after a failed post-write status check (CMD13) it asks the card once more.
// CPA_SD_DIAG tells users this copy (not the core's SD) is linked.
#define CPA_SD_DIAG 1
typedef enum {
    SD_DIAG_NONE = 0,
    SD_DIAG_BUSY,        // the card was still busy when a wait gave up
    SD_DIAG_NO_R1,       // no R1 after 3 tries
    SD_DIAG_R1,          // R1 with an error bit (> 0x01)
    SD_DIAG_DATA_RESP,   // write data response other than 0x05 (accepted)
    SD_DIAG_STATUS,      // CMD13 after a write: R1 or the R2 status byte non-zero
    SD_DIAG_READ_TOKEN,  // no data start token (0xFE) within 500 ms
    SD_DIAG_READ_CRC,    // data block CRC16 mismatch
} sdcard_diag_stage_t;

typedef struct {
    uint8_t stage;      // sdcard_diag_stage_t
    uint8_t cmd;        // last command index sent
    uint8_t token;      // R1, data response (& 0x1F), data token or 0 for busy
    uint8_t status;     // CMD13 R2 byte (SD_DIAG_STATUS)
    char op;            // 'r' / 'w' single sector, 'R' / 'W' multi, '-' outside read/write
    uint16_t count;     // sectors in the read/write call
    uint16_t block;     // blocks of it done when the event happened
    uint32_t sector;    // first sector of the read/write call
    uint32_t busy_ms;   // how long the failed wait lasted
    uint32_t at_ms;     // millis()
} sdcard_diag_event_t;

typedef struct {
    uint32_t reads, writes;         // ff_sd_read / ff_sd_write calls
    uint32_t reads_failed, writes_failed;
    uint32_t events;                // error events (a retried one counts too)
    sdcard_diag_event_t first, last;
    uint32_t busy_max_ms;           // longest busy wait seen
    uint32_t busy_max_sector;       // the read/write call it happened in
    char busy_max_op;
    uint32_t busy_hist[7];          // waits <1, <10, <50, <100, <250, <500, >=500 ms
    uint32_t rechecks;              // failed post-write CMD13s asked again
    uint32_t rechecks_clean;        // ... that then answered R1 0 and status 0
    uint8_t recheck_r1, recheck_st; // the last answer to the second CMD13
    uint32_t stop_waits;            // multi-block writes ended (CPA patch 5 waits)
    uint32_t stop_busy_max_us;      // longest Nbr + busy after a Stop Tran token
    uint32_t stop_busy_max_sector;  // the write it ended
} sdcard_diag_t;

sdcard_diag_t sdcard_diag(void);
void sdcard_diag_reset(void);
// A register the card sends as a data block: CMD9 CSD / CMD10 CID (16 B), with app=true
// ACMD13 SD Status (64 B) and ACMD51 SCR (8 B). The card must be initialized.
bool sdcard_read_register(uint8_t pdrv, uint8_t cmd, bool app, uint8_t* out, int len);

#endif /* _SD_DISKIO_H_ */
