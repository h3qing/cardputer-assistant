**English** · [中文](0001-toolchain.zh-CN.md)

# ADR-0001 · Firmware toolchain

- **Status**: Decided (2026-10-19, G2 measured on real hardware)
- **Decision**: **A** = `espressif32@6.10.0` (Arduino 2.0.17 / ESP-IDF 4.4.7 / GCC 8.4), application code built with `-std=gnu++17`
- **Not adopted**: B = pioarduino `54.03.21-2` (Arduino 3.2.1 / ESP-IDF 5.4.2 / GCC 14.2). It failed check 1 of 4 (CDC round trip)
- **Test firmware**: `firmware/hwtest/` (env `cardputer-adv` = A, `cardputer-adv-p54` = B, `cardputer-adv-gnu17` = gnu++17 build check for A)
- **Test script**: `firmware/hwtest/host/g2_run.py`; raw reports and WAVs in `firmware/hwtest/captures/g2-*` (gitignored)

## 1. Background

| | A (baseline) | B (candidate) |
|---|---|---|
| Platform | `espressif32@6.10.0` | `https://github.com/pioarduino/platform-espressif32/releases/download/54.03.21-2/platform-espressif32.zip` (2,625,441 B, released 2025-10-07) |
| Arduino / IDF / GCC | 2.0.17 / 4.4.7 / 8.4.0 | 3.2.1 / 5.4.2 (`v5.4.2-25-g858a988d6e`) / 14.2.0 |
| esptool | 4.5.1 | 5.0.2 |
| Pros | hwtest already works; officially maintained by PlatformIO | IDF 5.4 is still maintained; the factory firmware is also IDF 5.4 |
| Cons | IDF 4.4 is EOL, no security updates | pioarduino effectively has one maintainer (supply-chain risk); Arduino 2→3 has breaking API changes |

Cores based on IDF 5.5 are out: on the ADV, recordings are all zeros (ESP-IDF #18621).

**G2 decision rule** (06): use B only if it passes all four checks; otherwise use A.
1. Round trip after handshake ≤100ms (p95, with the 20ms TX kick kept)
2. After the speaker has played, the microphone RMS is not 0 and not a flat line
3. A 1MB SD write followed by a rename succeeds, or no SD card is inserted
4. M5Cardputer 1.2.0 / M5Unified 0.2.26 / M5GFX 0.2.33 all compile

Also checked along the way: whether A compiles with `-std=gnu++17` (05 §11 R8).

## 2. Method

- Both toolchains flashed the same `src/main.cpp`, with no `#if` to tell the cores apart. B compiled without code changes.
- A few commands were added to hwtest: `!beep` (`Mic.end()` → `Speaker.begin()` → 1kHz for 300ms → `Speaker.end()` → `Mic.begin()`), `!sdtest` (SPI pins SCK40/MISO39/MOSI14/CS12; write 1MB to `/cpa_test.tmp`, close, rename to `/cpa_test.bin`, reopen to check the size, then delete), `!reboot` (`esp_restart()`). `hello` now also carries the `arduino` and `idf` versions.
- `g2_run.py` flow: first open the serial port the safe way (only the sequence in 03 §2, never touching RTS/DTR). Then send `!info` to handshake, retrying every 2 seconds, at most 3 times. After the handshake, do N `!info` round trips in a row, then run `mic, beep, mic, sdtest` in order, then one more round trip.
- **Reboot**: use `--reboot`, which makes the firmware run `esp_restart()` itself. G0 is not pressed and the control lines are not toggled.
- **Stuck reply rule**: if no reply arrives within 10 seconds of a request, write one more line, `nudge`. If the reply only appears then, it is recorded as `nudged`. `hello.up_ms` also shows when the device produced the reply, which tells a held TX apart from a lost RX.
- p95 uses the nearest-rank method. With 10 samples, p95 equals the maximum.

To reproduce:
```bash
cd firmware/hwtest
~/.local/bin/pio run -e cardputer-adv -t upload          # B uses -e cardputer-adv-p54
cd host && uv run --with pyserial python g2_run.py --label A --reboot --rtt 30 --steps mic,beep,mic,sdtest
```

## 3. Measurements (2026-10-19, same Cardputer ADV, no SD card inserted)

A ran 14 sessions in total, 11 of them fresh after boot. One more session is not counted: in that one the chip was stuck in the esptool stub and the firmware was not running at all; see §5.3. B ran 14 sessions with the specified kick, 7 of them fresh after boot.

| Check | A: espressif32@6.10.0 | B: pioarduino 54.03.21-2 |
|---|---|---|
| **4. The three libraries compile** | ✅ no code changes | ✅ no code changes |
| A builds with gnu++17 | ✅ 41 C++ translation units (including the M5 libraries) carry only `-std=gnu++17`, verified from compiledb | N/A (GCC 14 defaults to gnu++2b) |
| Static RAM / Flash | 93,504 B / 573,337 B | 96,508 B / 664,130 B |
| Free heap / largest block after boot | 277,488 / 245,748 B | 254,996 / 188,404 B (about 22KB / 56KB less) |
| Handshake | 14/14 succeeded first time, p50 57.7ms, max 60.1ms | 14/14 succeeded first time, p50 57.6ms, max 60.1ms |
| Normal round trip p50 / p95 / max | 57.9 / 60.0 / 60.2ms (n=240) | 57.5 / 59.9 / 60.2ms (n=328) |
| **Stuck replies during round trips** | **0** (240 round trips, 11 fresh-boot sessions) | **7**: **7/7 fresh-boot sessions**, once per session; the other 7 sessions (150 round trips) had 0 |
| How long stuck | — | About 10.1 seconds each time, i.e. until the host's next write (the device produced the reply within 50–60ms, so TX was being held) |
| When it got stuck | — | About 3.4–4.1 seconds after boot (`up_ms`), i.e. 0.2–1.3 seconds after the handshake; 8.6 seconds in the very first session |
| **1. Literal gate** (handshake plus 10 round trips, p95 ≤100ms) | ✅ 14/14 pass | ❌ **5/7 fresh-boot sessions fail** (p95 about 10.1 seconds); the other 2 got stuck only at round trip 11 and 13 |
| Mic noise floor (before the beep) | rms 6–39, peak-to-peak 48–1168, 7–120 distinct values (9/9) | rms 6–41, peak-to-peak 56–896, 8–95 distinct values (7/7) |
| **2. Mic after the beep** | ✅ rms 10–28, peak-to-peak 112–672, 15–76 distinct values (9/9) | ✅ rms 12–18, peak-to-peak 384–424, 43–50 distinct values (7/7) |
| `Speaker.begin()` / the following `Mic.begin()` | All 7 replies received were true (2 more replies were held, but the recordings after them were normal) | 7/7 returned true |
| **3. SD** | `no_sd` (no card) | `no_sd` (no card), counts as a pass under the rule |
| Stuck replies after long operations | `!beep` reply took over 5 seconds 2/9 times; the round trip after `!sdtest` got no reply within 2 seconds 1/14 times | `!beep` reply 0/7 times |

The microphone behaves the same on both toolchains: samples are all multiples of 8, and 20–48% of samples are 0 when quiet. The first 100ms after `!beep` has a codec-restart transient (rms about 48); from 200ms on it is back to ambient noise.

## 4. Decision

**Use A.** B fails check 1: shortly after every boot one reply is held for about 10 seconds, and the specified 20ms kick does not release it. B passes checks 2 and 4. Check 3 passes only because no card was inserted; the SD write itself was never tested.

## 5. Consequences

### 5.1 Build configuration
- `firmware/app/platformio.ini` uses `platform = espressif32@6.10.0`, with `build_unflags = -std=gnu++11` and `-std=gnu++17` (as written in 05 §3, verified to compile). R8 will not happen.
- IDF 4.4 is EOL and gets no security fixes. v0.1 does not turn on WiFi/BLE, so this risk is acceptable. Re-evaluate before v0.3 adds wireless features. The target is C (Arduino ≥3.3.9, HWCDC fixed, PR #12606). But C is based on IDF 5.5, so first confirm #18621 (all-zero recordings) is fixed.

### 5.2 HWCDC: the kick is needed, but not enough even on A (handed to WP3/WP4/WP5)
- On both cores, `isCDC_Connected()` returns as soon as `connected` is true, so in that state the kick does nothing.
- A stronger kick was tried on B: re-enable the interrupt every 20ms with `usb_serial_jtag_ll_ena_intr_mask(IN_EMPTY)`. **No improvement**: 4/4 fresh-boot sessions still got stuck, and the `!beep` reply was held 3/4 times. That experimental code has been removed. The root cause was not found within the half-day time box.
- On A, held replies only happen after the device has blocked for a few hundred milliseconds (`!beep`, `!sdtest`), and the host's next write releases them. From this:
  - The daemon's ping every 5 seconds bounds the worst-case delay to about 5 seconds; the 15-second disconnect rule is not affected.
  - **Recommendation**: when the daemon has waited more than about 500ms for a reply, write an empty line `\n`. LineFramer already drops lines that do not start with `{`, so this does not change protocol semantics. WP3 decides whether to adopt this; the WP5 latency scenarios can verify it.
  - WP4's usb_transport still keeps the 20ms kick (05 §5.8). The firmware should avoid blocking the loop for long, e.g. by splitting Flash writes into small pieces, consistent with R7.

### 5.3 Flashing and recovery
- Flashed A→B→A back and forth more than 10 times; all 4 regions (bootloader, partition table, boot_app0, app) verified every time. No key presses needed for upload.
- **One anomaly (A, esptool 4.5.1)**: after a session with a missing reply, `pio run -t upload` got stuck twice in a row at `Changing baud rate to 460800` → `No serial data received`. The ROM could connect, but the stub stopped responding after it started, and nothing was written to Flash. `read_mac` at 115200 behaved the same. **The fix was a normal reset through a direct ROM connection, without power-cycling**:
  ```bash
  ~/.local/share/uv/tools/platformio/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 115200 --before default_reset --after hard_reset --no-stub read_mac
  ```
  After this the app started normally, uploads worked again, and it has not happened since. The guess is that the USB-Serial/JTAG peripheral state survives a core reset, and one app re-initialisation clears it. This fix should go into the recovery section of the README (WP5).
- With both platforms installed on the same machine, PlatformIO reinstalls some tools (esptool, mklittlefs, etc.) when switching. After installing B, `~/.platformio` grew from 2.4GB to about 5.7GB.

## 6. How to switch or roll back

- **Moving to B (or C) later**: in `firmware/app/platformio.ini` change only the `platform` line, to the pinned release zip URL (05 §3), and keep the kick. Then rerun `g2_run.py --reboot --rtt 30` on hwtest for **at least 4 fresh-boot sessions**, requiring 0 stuck replies and passes on checks 2–4, then update this ADR.
- **Back from B to A**: change `platform` back to `espressif32@6.10.0` and run `pio run -t upload`. Upload writes the bootloader, partition table and app together; switching both ways worked today, and no full-chip erase is needed. ⏳ Whether data in NVS/LittleFS is compatible between IDF 4.4 and 5.4 has not been tested.
- **Freeing the disk B uses** (optional, the user's call): B uses `~/.platformio/platforms/espressif32` (pioarduino 54.03.21) and, under `packages/`, `framework-arduinoespressif32@src-*`, `framework-arduinoespressif32-libs`, `toolchain-xtensa-esp-elf`, `tool-esptoolpy@src-*`. A uses `platforms/espressif32@6.10.0`, plus `framework-arduinoespressif32` without `@src` and `toolchain-xtensa-esp32s3`.

## 7. Still to verify

- ⏳ SD card: no card was inserted, so write, rename and size check never really ran. Insert a card and run `g2_run.py --steps sdtest` to cover it. v0.1 does not use SD (05 §2).
- ⏳ Whether the speaker actually made a sound: only `Speaker.begin()` returning true was confirmed; nobody listened.
- ⏳ The root cause of HWCDC held replies (on both cores), and whether the empty-line wake-up proposed in §5.2 works.
