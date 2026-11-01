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
#include "sd_diskio.h"
#include "esp_system.h"
extern "C" {
    #include "ff.h"
    #include "diskio.h"
#if ESP_IDF_VERSION_MAJOR > 3
    #include "diskio_impl.h"
#endif
    //#include "esp_vfs.h"
    #include "esp_vfs_fat.h"
    char CRC7(const char* data, int length);
    unsigned short CRC16(const char* data, int length);
}

typedef enum {
    GO_IDLE_STATE           = 0,
    SEND_OP_COND            = 1,
    SEND_CID                = 2,
    SEND_RELATIVE_ADDR      = 3,
    SEND_SWITCH_FUNC        = 6,
    SEND_IF_COND            = 8,
    SEND_CSD                = 9,
    STOP_TRANSMISSION       = 12,
    SEND_STATUS             = 13,
    SET_BLOCKLEN            = 16,
    READ_BLOCK_SINGLE       = 17,
    READ_BLOCK_MULTIPLE     = 18,
    SEND_NUM_WR_BLOCKS      = 22,
    SET_WR_BLK_ERASE_COUNT  = 23,
    WRITE_BLOCK_SINGLE      = 24,
    WRITE_BLOCK_MULTIPLE    = 25,
    APP_OP_COND             = 41,
    APP_CLR_CARD_DETECT     = 42,
    APP_CMD                 = 55,
    READ_OCR                = 58,
    CRC_ON_OFF              = 59
} ardu_sdcard_command_t;

typedef struct {
    uint8_t ssPin;
    SPIClass * spi;
    int frequency;
    char * base_path;
    sdcard_type_t type;
    unsigned long sectors;
    bool supports_crc;
    int status;
} ardu_sdcard_t;

static ardu_sdcard_t* s_cards[FF_VOLUMES] = { NULL };

// CPA patch 4 (docs/gates/sd-card.md §11): diagnostics, see sd_diskio.h.
static sdcard_diag_t s_diag;
static char s_op = '-';
static uint32_t s_op_sector = 0;
static uint16_t s_op_count = 0;
static uint16_t s_op_block = 0;
static uint8_t s_last_cmd = 0;

static void diagEvent(uint8_t stage, uint8_t token, uint8_t status, uint32_t busy_ms)
{
    sdcard_diag_event_t e;
    e.stage = stage;
    e.cmd = s_last_cmd;
    e.token = token;
    e.status = status;
    e.op = s_op;
    e.count = s_op_count;
    e.block = s_op_block;
    e.sector = s_op_sector;
    e.busy_ms = busy_ms;
    e.at_ms = millis();
    if (s_diag.events++ == 0) {
        s_diag.first = e;
    }
    s_diag.last = e;
}

static void diagBusy(uint32_t ms)
{
    static const uint32_t kEdges[6] = {1, 10, 50, 100, 250, 500};
    int i = 0;
    while (i < 6 && ms >= kEdges[i]) {
        i++;
    }
    s_diag.busy_hist[i]++;
    if (ms > s_diag.busy_max_ms) {
        s_diag.busy_max_ms = ms;
        s_diag.busy_max_sector = s_op_sector;
        s_diag.busy_max_op = s_op;
    }
}

static void diagStop(uint32_t us)
{
    s_diag.stop_waits++;
    if (us > s_diag.stop_busy_max_us) {
        s_diag.stop_busy_max_us = us;
        s_diag.stop_busy_max_sector = s_op_sector;
    }
}

static void diagBegin(char op, uint32_t sector, uint32_t count)
{
    s_op = op;
    s_op_sector = sector;
    s_op_count = (uint16_t)count;
    s_op_block = 0;
}

static void diagEnd(bool write, bool ok)
{
    if (write) {
        s_diag.writes++;
        s_diag.writes_failed += ok ? 0 : 1;
    } else {
        s_diag.reads++;
        s_diag.reads_failed += ok ? 0 : 1;
    }
    s_op = '-';
}

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR
const char * fferr2str[] = {
    "(0) Succeeded",
    "(1) A hard error occurred in the low level disk I/O layer",
    "(2) Assertion failed",
    "(3) The physical drive cannot work",
    "(4) Could not find the file",
    "(5) Could not find the path",
    "(6) The path name format is invalid",
    "(7) Access denied due to prohibited access or directory full",
    "(8) Access denied due to prohibited access",
    "(9) The file/directory object is invalid",
    "(10) The physical drive is write protected",
    "(11) The logical drive number is invalid",
    "(12) The volume has no work area",
    "(13) There is no valid FAT volume",
    "(14) The f_mkfs() aborted due to any problem",
    "(15) Could not get a grant to access the volume within defined period",
    "(16) The operation is rejected according to the file sharing policy",
    "(17) LFN working buffer could not be allocated",
    "(18) Number of open files > FF_FS_LOCK",
    "(19) Given parameter is invalid"
};
#endif

/*
 * SD SPI
 * */

bool sdWait(uint8_t pdrv, int timeout)
{
    char resp;
    uint32_t start = millis();

    do {
        resp = s_cards[pdrv]->spi->transfer(0xFF);
    } while (resp == 0x00 && (millis() - start) < (unsigned int)timeout);

    diagBusy(millis() - start);  // CPA patch 4
    if (!resp) {
        diagEvent(SD_DIAG_BUSY, 0, 0, millis() - start);  // CPA patch 4
        log_w("Wait Failed");
    }
    return (resp > 0x00);
}

void sdStop(uint8_t pdrv)
{
    s_cards[pdrv]->spi->write(0xFD);
}

void sdDeselectCard(uint8_t pdrv)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    digitalWrite(card->ssPin, HIGH);
}

bool sdSelectCard(uint8_t pdrv)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    digitalWrite(card->ssPin, LOW);
    bool s = sdWait(pdrv, 500);
    if (!s) {
        log_e("Select Failed");
        digitalWrite(card->ssPin, HIGH);
        return false;
    }
    return true;
}

char sdCommand(uint8_t pdrv, char cmd, unsigned int arg, unsigned int* resp)
{
    char token;
    ardu_sdcard_t * card = s_cards[pdrv];

    for (int f = 0; f < 3; f++) {
        if (cmd == SEND_NUM_WR_BLOCKS || cmd == SET_WR_BLK_ERASE_COUNT || cmd == APP_OP_COND || cmd == APP_CLR_CARD_DETECT) {
            token = sdCommand(pdrv, APP_CMD, 0, NULL);
            sdDeselectCard(pdrv);
            if (token > 1) {
                break;
            }
            if(!sdSelectCard(pdrv)) {
                token = 0xFF;
                break;
            }
        }

        char cmdPacket[7];
        cmdPacket[0] = cmd | 0x40;
        cmdPacket[1] = arg >> 24;
        cmdPacket[2] = arg >> 16;
        cmdPacket[3] = arg >> 8;
        cmdPacket[4] = arg;
        if(card->supports_crc || cmd == GO_IDLE_STATE || cmd == SEND_IF_COND) {
            cmdPacket[5] = (CRC7(cmdPacket, 5) << 1) | 0x01;
        } else {
            cmdPacket[5] = 0x01;
        }
        cmdPacket[6] = 0xFF;

        s_last_cmd = (uint8_t)cmd;  // CPA patch 4
        card->spi->writeBytes((uint8_t*)cmdPacket, (cmd == STOP_TRANSMISSION)?7:6);

        for (int i = 0; i < 9; i++) {
            token = card->spi->transfer(0xFF);
            if (!(token & 0x80)) {
                break;
            }
        }

        if (token == 0xFF) {
            log_w("no token received");
            sdDeselectCard(pdrv);
            delay(100);
            sdSelectCard(pdrv);
            continue;
        } else if (token & 0x08) {
            diagEvent(SD_DIAG_R1, token, 0, 0);  // CPA patch 4
            log_w("crc error");
            sdDeselectCard(pdrv);
            delay(100);
            sdSelectCard(pdrv);
            continue;
        } else if (token > 1) {
            diagEvent(SD_DIAG_R1, token, 0, 0);  // CPA patch 4
            log_w("token error [%u] 0x%x", cmd, token);
            break;
        }

        if (cmd == SEND_STATUS && resp) {
            *resp = card->spi->transfer(0xFF);
        } else if ((cmd == SEND_IF_COND || cmd == READ_OCR) && resp) {
            *resp = card->spi->transfer32(0xFFFFFFFF);
        }

        break;
    }
    if (token == 0xFF) {
        diagEvent(SD_DIAG_NO_R1, token, 0, 0);  // CPA patch 4
        log_e("Card Failed! cmd: 0x%02x", cmd);
        card->status = STA_NOINIT;
    }
    return token;
}

bool sdReadBytes(uint8_t pdrv, char* buffer, int length)
{
    char token;
    unsigned short crc;
    ardu_sdcard_t * card = s_cards[pdrv];

    uint32_t start = millis();
    do {
        token = card->spi->transfer(0xFF);
    } while (token == 0xFF && (millis() - start) < 500);

    if (token != 0xFE) {
        diagEvent(SD_DIAG_READ_TOKEN, token, 0, millis() - start);  // CPA patch 4
        return false;
    }

    card->spi->transferBytes(NULL, (uint8_t*)buffer, length);
    crc = card->spi->transfer16(0xFFFF);
    // CPA patch 4: name the CRC failure
    bool ok = (!card->supports_crc || crc == CRC16(buffer, length));
    if (!ok) {
        diagEvent(SD_DIAG_READ_CRC, token, 0, 0);
    }
    return ok;
}

char sdWriteBytes(uint8_t pdrv, const char* buffer, char token)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    unsigned short crc = (card->supports_crc)?CRC16(buffer, 512):0xFFFF;
    if (!sdWait(pdrv, 500)) {
        return false;
    }

    card->spi->write(token);
    card->spi->writeBytes((uint8_t*)buffer, 512);
    card->spi->write16(crc);
    // CPA patch 4: name a rejected block
    char resp = card->spi->transfer(0xFF) & 0x1F;
    if (resp != 0x05) {
        diagEvent(SD_DIAG_DATA_RESP, resp, 0, 0);
    }
    return resp;
}

/*
 * SPI SDCARD Communication
 * */

char sdTransaction(uint8_t pdrv, char cmd, unsigned int arg, unsigned int* resp)
{
    if(!sdSelectCard(pdrv)) {
        return 0xFF;
    }
    char token = sdCommand(pdrv, cmd, arg, resp);
    sdDeselectCard(pdrv);
    return token;
}

bool sdReadSector(uint8_t pdrv, char* buffer, unsigned long long sector)
{
    for (int f = 0; f < 3; f++) {
        if(!sdSelectCard(pdrv)) {
            return false;
        }
        if (!sdCommand(pdrv, READ_BLOCK_SINGLE, (s_cards[pdrv]->type == CARD_SDHC) ? sector : sector << 9, NULL)) {
            bool success = sdReadBytes(pdrv, buffer, 512);
            sdDeselectCard(pdrv);
            if (success) {
                return true;
            }
        } else {
            break;
        }
    }
    sdDeselectCard(pdrv);
    return false;
}

bool sdReadSectors(uint8_t pdrv, char* buffer, unsigned long long sector, int count)
{
    for (int f = 0; f < 3;) {
        if(!sdSelectCard(pdrv)) {
            return false;
        }

        if (!sdCommand(pdrv, READ_BLOCK_MULTIPLE, (s_cards[pdrv]->type == CARD_SDHC) ? sector : sector << 9, NULL)) {
            do {
                if (!sdReadBytes(pdrv, buffer, 512)) {
                    f++;
                    break;
                }

                s_op_block++;  // CPA patch 4
                sector++;
                buffer += 512;
                f = 0;
            } while (--count);

            if (sdCommand(pdrv, STOP_TRANSMISSION, 0, NULL)) {
                log_e("command failed");
                break;
            }

            sdDeselectCard(pdrv);
            if (count == 0) {
                return true;
            }
        } else {
            break;
        }
    }
    sdDeselectCard(pdrv);
    return false;
}

// CPA patch 4: after a failed post-write status check, ask once more (the select waits
// out a busy card). A card that really failed answers with an error again; a status read
// that raced the card's busy answers R1 0 and status 0.
static void diagRecheck(uint8_t pdrv)
{
    unsigned int resp = 0;
    char r1 = sdTransaction(pdrv, SEND_STATUS, 0, &resp);
    s_diag.rechecks++;
    s_diag.rechecks_clean += (r1 == 0 && resp == 0) ? 1 : 0;
    s_diag.recheck_r1 = (uint8_t)r1;
    s_diag.recheck_st = (uint8_t)resp;
}

bool sdWriteSector(uint8_t pdrv, const char* buffer, unsigned long long sector)
{
    for (int f = 0; f < 3; f++) {
        if(!sdSelectCard(pdrv)) {
            return false;
        }
        if (!sdCommand(pdrv, WRITE_BLOCK_SINGLE, (s_cards[pdrv]->type == CARD_SDHC) ? sector : sector << 9, NULL)) {
            char token = sdWriteBytes(pdrv, buffer, 0xFE);
            sdDeselectCard(pdrv);

            if (token == 0x0A) {
                continue;
            } else if (token == 0x0C) {
                return false;
            }

            // CPA patch 4: name a failed status check
            unsigned int resp = 0;
            char r1 = sdTransaction(pdrv, SEND_STATUS, 0, &resp);
            if (r1 || resp) {
                diagEvent(SD_DIAG_STATUS, r1, resp, 0);
                diagRecheck(pdrv);
                return false;
            }
            return true;
        } else {
            break;
        }
    }
    sdDeselectCard(pdrv);
    return false;
}

bool sdWriteSectors(uint8_t pdrv, const char* buffer, unsigned long long sector, int count)
{
    char token;
    const char* currentBuffer = buffer;
    unsigned long long currentSector = sector;
    int currentCount = count;
    ardu_sdcard_t * card = s_cards[pdrv];

    for (int f = 0; f < 3;) {
        if (card->type != CARD_MMC) {
            if (sdTransaction(pdrv, SET_WR_BLK_ERASE_COUNT, currentCount, NULL)) {
                return false;
            }
        }

        if(!sdSelectCard(pdrv)) {
            return false;
        }

        if (!sdCommand(pdrv, WRITE_BLOCK_MULTIPLE, (card->type == CARD_SDHC) ? currentSector : currentSector << 9, NULL)) {
            do {
                s_op_block = (uint16_t)(count - currentCount);  // CPA patch 4
                token = sdWriteBytes(pdrv, currentBuffer, 0xFC);
                if (token != 0x05) {
                    f++;
                    break;
                }
                currentBuffer += 512;
                f = 0;
            } while (--currentCount);

            if (!sdWait(pdrv, 500)) {
                break;
            }

            if (currentCount == 0) {
                sdStop(pdrv);
                // CPA patch 5 (docs/gates/sd-card.md §11): the card starts the busy of the
                // Stop Tran token one byte (Nbr) after it. Upstream deselected at once, so
                // the select of the CMD13 below could take that byte for "ready" and send
                // CMD13 into the busy. Its answer was then garbage (0x07 on the test card,
                // which asked again reported no error) and the write failed with EIO
                // although the card had taken the data. Clock Nbr, then wait out the busy.
                uint32_t stop_us = micros();
                card->spi->transfer(0xFF);
                sdWait(pdrv, 500);
                diagStop(micros() - stop_us);
                sdDeselectCard(pdrv);

                // CPA patch 4: name a failed status check
                unsigned int resp = 0;
                char r1 = sdTransaction(pdrv, SEND_STATUS, 0, &resp);
                if (r1 || resp) {
                    diagEvent(SD_DIAG_STATUS, r1, resp, 0);
                    diagRecheck(pdrv);
                    return false;
                }
                return true;
            } else {
                if (sdCommand(pdrv, STOP_TRANSMISSION, 0, NULL)) {
                    break;
                }

                if (token == 0x0A) {
                    sdDeselectCard(pdrv);
                    unsigned int writtenBlocks = 0;
                    if (card->type != CARD_MMC && sdSelectCard(pdrv)) {
                        if (!sdCommand(pdrv, SEND_NUM_WR_BLOCKS, 0, NULL)) {
                            char acmdData[4];
                            if (sdReadBytes(pdrv, acmdData, 4)) {
                                writtenBlocks = acmdData[0] << 24;
                                writtenBlocks |= acmdData[1] << 16;
                                writtenBlocks |= acmdData[2] << 8;
                                writtenBlocks |= acmdData[3];
                            }
                        }
                        sdDeselectCard(pdrv);
                    }
                    currentBuffer = buffer + (writtenBlocks << 9);
                    currentSector = sector + writtenBlocks;
                    currentCount = count - writtenBlocks;
                    continue;
                } else {
                    break;
                }
            }
        } else {
            break;
        }
    }
    sdDeselectCard(pdrv);
    return false;
}

unsigned long sdGetSectorsCount(uint8_t pdrv)
{
    for (int f = 0; f < 3; f++) {
        if(!sdSelectCard(pdrv)) {
            return false;
        }

        if (!sdCommand(pdrv, SEND_CSD, 0, NULL)) {
            char csd[16];
            bool success = sdReadBytes(pdrv, csd, 16);
            sdDeselectCard(pdrv);
            if (success) {
                if ((csd[0] >> 6) == 0x01) {
                    unsigned long size = (
                         ((unsigned long)(csd[7] & 0x3F) << 16)
                         | ((unsigned long)csd[8] << 8)
                         | csd[9]
                     ) + 1;
                    return size << 10;
                }
                unsigned long size = (
                     ((unsigned long)(csd[6] & 0x03) << 10)
                     | ((unsigned long)csd[7] << 2)
                     | ((csd[8] & 0xC0) >> 6)
                 ) + 1;
                size <<= ((
                     ((csd[9] & 0x03) << 1)
                     | ((csd[10] & 0x80) >> 7)
                 ) + 2);
                size <<= (csd[5] & 0x0F);
                return size >> 9;
            }
        } else {
            break;
        }
    }

    sdDeselectCard(pdrv);
    return 0;
}


namespace
{

struct AcquireSPI
{
    ardu_sdcard_t *card;
    explicit AcquireSPI(ardu_sdcard_t* card)
        : card(card)
    {
        card->spi->beginTransaction(SPISettings(card->frequency, MSBFIRST, SPI_MODE0));
    }
    AcquireSPI(ardu_sdcard_t* card, int frequency)
        : card(card)
    {
        card->spi->beginTransaction(SPISettings(frequency, MSBFIRST, SPI_MODE0));
    }
    ~AcquireSPI()
    {
        card->spi->endTransaction();
    }
private:
    AcquireSPI(AcquireSPI const&);
    AcquireSPI& operator=(AcquireSPI const&);
};

}


/*
 * FATFS API
 * */

DSTATUS ff_sd_initialize(uint8_t pdrv)
{
    char token;
    unsigned int resp;
    unsigned int start;
    ardu_sdcard_t * card = s_cards[pdrv];

    if (!(card->status & STA_NOINIT)) {
        return card->status;
    }

    AcquireSPI card_locked(card, 400000);

    digitalWrite(card->ssPin, HIGH);
    for (uint8_t i = 0; i < 20; i++) {
        card->spi->transfer(0XFF);
    }

    // Fix mount issue - sdWait fail ignored before command GO_IDLE_STATE
    digitalWrite(card->ssPin, LOW);
    if(!sdWait(pdrv, 500)){
        log_w("sdWait fail ignored, card initialize continues");
    }
    if (sdCommand(pdrv, GO_IDLE_STATE, 0, NULL) != 1){
        sdDeselectCard(pdrv);
        log_w("GO_IDLE_STATE failed");
        goto unknown_card;
    }
    sdDeselectCard(pdrv);

    token = sdTransaction(pdrv, CRC_ON_OFF, 1, NULL);
    if (token == 0x5) {
        //old card maybe
        card->supports_crc = false;
    } else if (token != 1) {
        log_w("CRC_ON_OFF failed: %u", token);
        goto unknown_card;
    }

    if (sdTransaction(pdrv, SEND_IF_COND, 0x1AA, &resp) == 1) {
        if ((resp & 0xFFF) != 0x1AA) {
            log_w("SEND_IF_COND failed: %03X", resp & 0xFFF);
            goto unknown_card;
        }

        if (sdTransaction(pdrv, READ_OCR, 0, &resp) != 1 || !(resp & (1 << 20))) {
            log_w("READ_OCR failed: %X", resp);
            goto unknown_card;
        }

        start = millis();
        do {
            // CPA patch (docs/gates/sd-card.md §2): in SPI mode only HCS (bit 30) is
            // defined in the ACMD41 argument. Upstream also sets bit 20 (3.2-3.3 V); the
            // test card then stays idle forever unless it was initialized before.
            token = sdTransaction(pdrv, APP_OP_COND, 0x40000000, NULL);
        } while (token == 1 && (millis() - start) < 1000);

        if (token) {
            log_w("APP_OP_COND failed: %u", token);
            goto unknown_card;
        }

        if (!sdTransaction(pdrv, READ_OCR, 0, &resp)) {
            if (resp & (1 << 30)) {
                card->type = CARD_SDHC;
            } else {
                card->type = CARD_SD;
            }
        } else {
            log_w("READ_OCR failed: %X", resp);
            goto unknown_card;
        }
    } else {
        if (sdTransaction(pdrv, READ_OCR, 0, &resp) != 1 || !(resp & (1 << 20))) {
            log_w("READ_OCR failed: %X", resp);
            goto unknown_card;
        }

        start = millis();
        do {
            token = sdTransaction(pdrv, APP_OP_COND, 0x100000, NULL);
        } while (token == 0x01 && (millis() - start) < 1000);

        if (!token) {
            card->type = CARD_SD;
        } else {
            start = millis();
            do {
                token = sdTransaction(pdrv, SEND_OP_COND, 0x100000, NULL);
            } while (token != 0x00 && (millis() - start) < 1000);

            if (token == 0x00) {
                card->type = CARD_MMC;
            } else {
                log_w("SEND_OP_COND failed: %u", token);
                goto unknown_card;
            }
        }
    }

    if (card->type != CARD_MMC) {
        if (sdTransaction(pdrv, APP_CLR_CARD_DETECT, 0, NULL)) {
            log_w("APP_CLR_CARD_DETECT failed");
            goto unknown_card;
        }
    }

    if (card->type != CARD_SDHC) {
        if (sdTransaction(pdrv, SET_BLOCKLEN, 512, NULL) != 0x00) {
            log_w("SET_BLOCKLEN failed");
            goto unknown_card;
        }
    }

    card->sectors = sdGetSectorsCount(pdrv);

    // CPA patch: the upstream 25 MHz cap (the SD default-speed limit; with the 80 MHz
    // APB divider that means 20 MHz) is kept unless CPA_SD_UNCAPPED_CLOCK is defined.
    // Only hwtest defines it, so !sdfreq can measure 26.7 and 40 MHz.
#ifndef CPA_SD_UNCAPPED_CLOCK
    if (card->frequency > 25000000) {
        card->frequency = 25000000;
    }
#endif

    card->status &= ~STA_NOINIT;
    return card->status;

unknown_card:
    card->type = CARD_UNKNOWN;
    return card->status;
}

DSTATUS ff_sd_status(uint8_t pdrv)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    AcquireSPI lock(card);
    
    if(sdTransaction(pdrv, SEND_STATUS, 0, NULL))
    {
        log_e("Check status failed");
        return STA_NOINIT;
    }
    return s_cards[pdrv]->status;
}

DRESULT ff_sd_read(uint8_t pdrv, uint8_t* buffer, DWORD sector, UINT count)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    if (card->status & STA_NOINIT) {
        return RES_NOTRDY;
    }
    DRESULT res = RES_OK;

    AcquireSPI lock(card);

    diagBegin(count > 1 ? 'R' : 'r', sector, count);  // CPA patch 4
    if (count > 1) {
        res = sdReadSectors(pdrv, (char*)buffer, sector, count) ? RES_OK : RES_ERROR;
    } else {
        res = sdReadSector(pdrv, (char*)buffer, sector) ? RES_OK : RES_ERROR;
    }
    diagEnd(false, res == RES_OK);  // CPA patch 4
    return res;
}

DRESULT ff_sd_write(uint8_t pdrv, const uint8_t* buffer, DWORD sector, UINT count)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    if (card->status & STA_NOINIT) {
        return RES_NOTRDY;
    }

    if (card->status & STA_PROTECT) {
        return RES_WRPRT;
    }
    DRESULT res = RES_OK;

    AcquireSPI lock(card);

    diagBegin(count > 1 ? 'W' : 'w', sector, count);  // CPA patch 4
    if (count > 1) {
        res = sdWriteSectors(pdrv, (const char*)buffer, sector, count) ? RES_OK : RES_ERROR;
    } else {
        res = sdWriteSector(pdrv, (const char*)buffer, sector) ? RES_OK : RES_ERROR;
    }
    diagEnd(true, res == RES_OK);  // CPA patch 4
    return res;
}

DRESULT ff_sd_ioctl(uint8_t pdrv, uint8_t cmd, void* buff)
{
    switch(cmd) {
    case CTRL_SYNC:
        {
            AcquireSPI lock(s_cards[pdrv]);
            if (sdSelectCard(pdrv)) {
                sdDeselectCard(pdrv);
                return RES_OK;
            }
        }
        return RES_ERROR;
    case GET_SECTOR_COUNT:
        *((unsigned long*) buff) = s_cards[pdrv]->sectors;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *((WORD*) buff) = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        // CPA patch: f_mkfs aligns the data area to this many sectors. 8192 x 512 B =
        // 4 MB, the allocation unit the SD Association formatter aligns SDHC/SDXC to.
        // Upstream returns 1 (no alignment).
        *((uint32_t*)buff) = 8192;
        return RES_OK;
    }
    return RES_PARERR;
}

bool sd_read_raw(uint8_t pdrv, uint8_t* buffer, DWORD sector)
{
    return ff_sd_read(pdrv, buffer, sector, 1) == ESP_OK;
}

bool sd_write_raw(uint8_t pdrv, uint8_t* buffer, DWORD sector)
{
    return ff_sd_write(pdrv, buffer, sector, 1) == ESP_OK;
}

/*
 * Public methods
 * */

uint8_t sdcard_uninit(uint8_t pdrv)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    if (pdrv >= FF_VOLUMES || card == NULL) {
        return 1;
    }
    AcquireSPI lock(card);
    sdTransaction(pdrv, GO_IDLE_STATE, 0, NULL);
    ff_diskio_register(pdrv, NULL);
    s_cards[pdrv] = NULL;
    esp_err_t err = ESP_OK;
    if (card->base_path) {
        err = esp_vfs_fat_unregister_path(card->base_path);
        free(card->base_path);
    }
    free(card);
    return err;
}

uint8_t sdcard_init(uint8_t cs, SPIClass * spi, int hz)
{

    uint8_t pdrv = 0xFF;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == 0xFF) {
        return pdrv;
    }

    ardu_sdcard_t * card = (ardu_sdcard_t *)malloc(sizeof(ardu_sdcard_t));
    if (!card) {
        return 0xFF;
    }

    card->base_path = NULL;
    card->frequency = hz;
    card->spi = spi;
    card->ssPin = cs;

    card->supports_crc = true;
    card->type = CARD_NONE;
    card->status = STA_NOINIT;

    pinMode(card->ssPin, OUTPUT);
    digitalWrite(card->ssPin, HIGH);

    s_cards[pdrv] = card;

    static const ff_diskio_impl_t sd_impl = {
        .init = &ff_sd_initialize,
        .status = &ff_sd_status,
        .read = &ff_sd_read,
        .write = &ff_sd_write,
        .ioctl = &ff_sd_ioctl
    };
    ff_diskio_register(pdrv, &sd_impl);

    return pdrv;
}

uint8_t sdcard_unmount(uint8_t pdrv)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    if (pdrv >= FF_VOLUMES || card == NULL) {
        return 1;
    }
    card->status |= STA_NOINIT;
    card->type = CARD_NONE;

    char drv[3] = {(char)('0' + pdrv), ':', 0};
    f_mount(NULL, drv, 0);
    return 0;
}

bool sdcard_mount(uint8_t pdrv, const char* path, uint8_t max_files, bool format_if_empty)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    if(pdrv >= FF_VOLUMES || card == NULL){
        return false;
    }

    if(card->base_path){
        free(card->base_path);
    }
    card->base_path = strdup(path);

    FATFS* fs;
    char drv[3] = {(char)('0' + pdrv), ':', 0};
    esp_err_t err = esp_vfs_fat_register(path, drv, max_files, &fs);
    if (err == ESP_ERR_INVALID_STATE) {
        log_e("esp_vfs_fat_register failed 0x(%x): SD is registered.", err);
        return false;
    } else if (err != ESP_OK) {
        log_e("esp_vfs_fat_register failed 0x(%x)", err);
        return false;
    }

    FRESULT res = f_mount(fs, drv, 1);
    if (res != FR_OK) {
        log_e("f_mount failed: %s", fferr2str[res]);
        if(res == 13 && format_if_empty){
            BYTE* work = (BYTE*) malloc(sizeof(BYTE) * FF_MAX_SS);
            if (!work) {
              log_e("alloc for f_mkfs failed");
              return false;
            }
            res = f_mkfs(drv, FM_ANY, 0, work, sizeof(BYTE) * FF_MAX_SS);
            free(work);
            if (res != FR_OK) {
                log_e("f_mkfs failed: %s", fferr2str[res]);
                esp_vfs_fat_unregister_path(path);
                return false;
            }
            res = f_mount(fs, drv, 1);
            if (res != FR_OK) {
                log_e("f_mount failed: %s", fferr2str[res]);
                esp_vfs_fat_unregister_path(path);
                return false;
            }
        } else {
            esp_vfs_fat_unregister_path(path);
            return false;
        }
    }
    AcquireSPI lock(card);
    card->sectors = sdGetSectorsCount(pdrv);
    return true;
}

uint32_t sdcard_num_sectors(uint8_t pdrv)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    if(pdrv >= FF_VOLUMES || card == NULL){
        return 0;
    }
    return card->sectors;
}

uint32_t sdcard_sector_size(uint8_t pdrv)
{
    if(pdrv >= FF_VOLUMES || s_cards[pdrv] == NULL){
        return 0;
    }
    return 512;
}

sdcard_type_t sdcard_type(uint8_t pdrv)
{
    ardu_sdcard_t * card = s_cards[pdrv];
    if(pdrv >= FF_VOLUMES || card == NULL){
        return CARD_NONE;
    }
    return card->type;
}

// CPA patch 4 (docs/gates/sd-card.md §11): diagnostics API, see sd_diskio.h.
sdcard_diag_t sdcard_diag(void)
{
    return s_diag;
}

void sdcard_diag_reset(void)
{
    memset(&s_diag, 0, sizeof(s_diag));
}

bool sdcard_read_register(uint8_t pdrv, uint8_t cmd, bool app, uint8_t* out, int len)
{
    ardu_sdcard_t * card = (pdrv < FF_VOLUMES) ? s_cards[pdrv] : NULL;
    if (card == NULL || (card->status & STA_NOINIT)) {
        return false;
    }
    AcquireSPI lock(card);
    if (app && sdTransaction(pdrv, APP_CMD, 0, NULL) > 1) {
        return false;
    }
    if (!sdSelectCard(pdrv)) {
        return false;
    }
    unsigned int r2 = 0;
    bool ok = !sdCommand(pdrv, cmd, 0, (app && cmd == SEND_STATUS) ? &r2 : NULL)
              && sdReadBytes(pdrv, (char*)out, len);
    sdDeselectCard(pdrv);
    return ok;
}
