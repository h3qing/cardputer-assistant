# Third-party licences

Cardputer Assistant is MIT-licensed (see [LICENSE](LICENSE)). This file lists the third-party components that are
linked into, or embedded in, what this repository builds and releases: the firmware image (`cpa-<tag>.bin`), the Mac
daemon and CLI (`cpad`, `cpa`, `cpa-stt`) and the Mac app. Versions come from the build manifests
(`firmware/app/platformio.ini`, `daemon/uv.lock`, `mac/Package.swift`); the firmware list was checked against the
linker map. Licence texts are in [LICENSES/](LICENSES/); [NOTICE](NOTICE) carries the attributions and the code this
project adapts. The tables are generated from the build manifests; a change of dependency must update them.

## Firmware image (`firmware/app`, PlatformIO env `cardputer-adv`)

| Component | Version | Licence (SPDX) | Copyright | Notes |
|---|---|---|---|---|
| [Arduino core for ESP32 (arduino-esp32), with its FS, LittleFS, Preferences, SPI and Wire libraries](https://github.com/espressif/arduino-esp32) | 2.0.17 (PlatformIO framework-arduinoespressif32 3.20017.241212, from platform espressif32@6.10.0) | LGPL-2.1-or-later ([text](LICENSES/LGPL-2.1.txt)) | Copyright (c) Espressif Systems (Shanghai) Co., Ltd. and the arduino-esp32 contributors; Copyright (c) 2005-2013 Arduino Team | Linked as a static library. LGPL-2.1 section 6: the complete source of this project is public under MIT, and firmware/app/platformio.ini pins the platform, so the firmware can be rebuilt and relinked against a modified core (override it with platform_packages = framework-arduinoespressif32 @ <path or URL>). Several esp32-hal files are Apache-2.0. |
| [arduino-esp32 SD library (vendored, patched: firmware/third_party/SD)](https://github.com/espressif/arduino-esp32/tree/2.0.17/libraries/SD) | from core 2.0.17 | Apache-2.0 ([text](LICENSES/Apache-2.0.txt)) | Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD; src/sd_diskio_crc.c: Copyright (c) 2014 Neil Thiessen | Changes are described in NOTICE and marked "CPA patch" in the source. |
| [ESP-IDF (precompiled in the Arduino core): app_update, bootloader_support, cxx, driver, efuse, esp_adc_cal, esp_common, esp_diagnostics, esp_event, esp_hw_support, esp_ipc, esp_phy, esp_pm, esp_ringbuf, esp_rom, esp_system, esp_timer, espcoredump, heap, log, newlib (port), nvs_flash, pthread, soc, spi_flash, vfs, xtensa](https://github.com/espressif/esp-idf/tree/v4.4.7) | v4.4.7 (38eeba213a) | Apache-2.0 ([text](LICENSES/Apache-2.0.txt)) | Copyright 2015-2024 Espressif Systems (Shanghai) Co., Ltd. |  |
| [Espressif radio and coexistence binary libraries (libbt, libcoexist, libphy, libesp_wifi stubs; only a few hundred bytes are linked)](https://github.com/espressif/esp-idf/tree/v4.4.7/components/esp_phy/lib) | as shipped with ESP-IDF v4.4.7 | LicenseRef-Espressif-binary | Copyright Espressif Systems (Shanghai) Co., Ltd. | Distributed by Espressif in binary form with ESP-IDF for use on Espressif chips; see the licence notes in those submodules. |
| [FreeRTOS kernel (ESP-IDF port)](https://github.com/espressif/esp-idf/tree/v4.4.7/components/freertos) | V10.4.3 | MIT ([text](LICENSES/MIT.txt)) | Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved. |  |
| [FatFs (ESP-IDF fatfs component)](http://elm-chan.org/fsw/ff/) | R0.13c | LicenseRef-FatFs | Copyright (C) 2018, ChaN, all right reserved. |  |
| [Mbed TLS (libmbedcrypto)](https://github.com/espressif/mbedtls) | as shipped with ESP-IDF v4.4.7 | Apache-2.0 ([text](LICENSES/Apache-2.0.txt)) | Copyright The Mbed TLS Contributors |  |
| [esp_littlefs](https://github.com/joltwallet/esp_littlefs) | 41873c2 (per the core's versions.txt) | MIT ([text](LICENSES/MIT.txt)) | Copyright (c) 2020 Brian Pugh |  |
| [littlefs (inside esp_littlefs)](https://github.com/littlefs-project/littlefs) | as bundled by esp_littlefs 41873c2 | BSD-3-Clause ([text](LICENSES/BSD-3-Clause.txt)) | Copyright (c) 2022, The littlefs authors. Copyright (c) 2017, Arm Limited. All rights reserved. |  |
| [newlib C library (libc, libm of the xtensa-esp32s3 toolchain)](https://github.com/espressif/newlib-esp32) | toolchain-xtensa-esp32s3 8.4.0+2021r2-patch5 | LicenseRef-newlib (BSD-style notices) ([text](LICENSES/newlib-COPYING.NEWLIB.txt)) | Copyright (c) 1994-2009 Red Hat, Inc. and the other authors listed in the notices file |  |
| [GCC runtime (libgcc, libstdc++ of the xtensa-esp32s3 toolchain)](https://github.com/espressif/gcc) | GCC 8.4.0 (toolchain 8.4.0+2021r2-patch5) | GPL-3.0-or-later WITH GCC-exception-3.1 | Copyright (C) Free Software Foundation, Inc. | The GCC Runtime Library Exception allows distributing the firmware under its own terms; no notice is required, it is listed for completeness. |
| [Xtensa HAL (libxt_hal)](https://github.com/espressif/esp-idf/tree/v4.4.7/components/xtensa) | as shipped with ESP-IDF v4.4.7 | MIT ([text](LICENSES/MIT.txt)) | Copyright (c) 1999-2020 Cadence Design Systems, Inc. |  |
| [M5Cardputer](https://github.com/m5stack/M5Cardputer) | 1.2.0 | MIT ([text](LICENSES/MIT.txt)) | SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD | The git tag is 1.2.0; the library.json at that tag still says 1.1.1. |
| [Adafruit_TCA8418 (bundled in M5Cardputer: src/utility/Adafruit_TCA8418)](https://github.com/adafruit/Adafruit_TCA8418) | as bundled by M5Cardputer 1.2.0 | BSD-3-Clause ([text](LICENSES/BSD-3-Clause.txt)) | Copyright (c) 2022 Limor Fried (Adafruit Industries) | The copy in M5Cardputer says "BSD (see license.txt)" but ships no license.txt; the upstream repository's licence applies. |
| [M5Unified](https://github.com/m5stack/M5Unified) | 0.2.26 | MIT ([text](LICENSES/MIT.txt)) | Copyright (c) 2021 M5Stack |  |
| [M5GFX](https://github.com/m5stack/M5GFX) | 0.2.33 | MIT ([text](LICENSES/MIT.txt)) | Copyright (c) 2021 M5Stack |  |
| [LovyanGFX (the graphics core inside M5GFX: LGFXBase, lgfx_v1*)](https://github.com/lovyan03/LovyanGFX) | as bundled by M5GFX | BSD-2-Clause ([text](LICENSES/BSD-2-Clause.txt)) | Copyright (c) 2020 lovyan03 |  |
| [Adafruit GFX GLCD font (M5GFX src/lgfx/Fonts/glcdfont.h)](https://github.com/adafruit/Adafruit-GFX-Library) | as bundled by M5GFX | BSD-2-Clause ([text](LICENSES/BSD-2-Clause.txt)) | Copyright (c) 2012 Adafruit Industries. All rights reserved. |  |
| [U8g2 font format and decoder (M5GFX lgfx_fonts.cpp, U8g2font)](https://github.com/olikraus/u8g2) | as bundled by M5GFX | BSD-2-Clause ([text](LICENSES/BSD-2-Clause.txt)) | Copyright (c) 2016, olikraus@gmail.com. All rights reserved. |  |
| [efont Unicode bitmap font, efontCN_12 (bundled in M5GFX)](http://openlab.ring.gr.jp/efont/unicode/) | as bundled by M5GFX | BSD-3-Clause ([text](LICENSES/BSD-3-Clause.txt)) | (c) Copyright 2000-2001 /efont/ The Electronic Font Open Laboratory. All rights reserved. | Full notice in NOTICE section 1. |
| [ArduinoJson (header-only)](https://github.com/bblanchon/ArduinoJson) | 7.4.3 | MIT ([text](LICENSES/MIT.txt)) | Copyright (c) 2014-2026, Benoit BLANCHON |  |
| [IRremote (dependency of M5Cardputer)](https://github.com/Arduino-IRremote/Arduino-IRremote) | 4.7.1 | MIT (some headers GPL-3.0) | (c) Copyright 2009 Ken Shirriff and contributors | Fetched by the build but not linked: no IRremote object is in firmware.map. Never include IRremote.hpp (it pulls in GPL-3.0 headers; issue #39 P0-6). |
| [Nunito ExtraBold digits (bitmaps in firmware/app/lib/theme/gen/digits_gen.h)](https://github.com/googlefonts/nunito) | WhisperWoof's Nunito-normal-latin.woff2 | OFL-1.1 ([text](LICENSES/OFL-1.1.txt)) | Copyright 2014 The Nunito Project Authors |  |

## Mac daemon and CLI (`daemon/`, Python package `cpad`)

| Component | Version | Licence (SPDX) | Copyright | Notes |
|---|---|---|---|---|
| [pyserial](https://github.com/pyserial/pyserial) | 3.5 | BSD-3-Clause ([text](LICENSES/BSD-3-Clause.txt)) | Copyright (c) 2001-2020 Chris Liechti | The only runtime dependency of the cpad package (cpa, cpad and cpa-stt). Development-only tools (pytest, ruff, hypothesis, jsonschema, lizard, ...) are not distributed. |
| [uv (fetched by scripts/install.sh when missing; not redistributed in this repository or its releases)](https://github.com/astral-sh/uv) | 0.11.16 (pinned in scripts/install.sh) | MIT OR Apache-2.0 | Copyright (c) Astral Software Inc. |  |

## Mac app (`mac/`)

`mac/Package.swift` declares no package dependencies: the app links only Apple's system frameworks (SwiftUI, AppKit,
Foundation, CryptoKit, AVFoundation, ImageIO, CoreServices, UniformTypeIdentifiers, Observation). Mando's sprites and
the app icon are generated from the firmware's own theme data (NOTICE section 4).

## Not distributed

The host simulator (`firmware/sim`), the test suites and their fixtures, and development tools (PlatformIO, Unity,
pytest, ruff, hypothesis, jsonschema, lizard, gcovr) are used to build and test only; they are not part of a release.

## Licence texts not in LICENSES/

### FatFs (ESP-IDF fatfs component)

```
Copyright (C) 2018, ChaN, all right reserved.

FatFs module is an open source software. Redistribution and use of FatFs in
source and binary forms, with or without modification, are permitted provided
that the following condition is met:

1. Redistributions of source code must retain the above copyright notice,
   this condition and the following disclaimer.

This software is provided by the copyright holder and contributors "AS IS"
and any warranties related to this software are DISCLAIMED.
The copyright owner or contributors be NOT LIABLE for any damages caused
by use of this software.
```
