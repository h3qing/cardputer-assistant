# SD (vendored, patched)

Copy of the Arduino-ESP32 core 2.0.17 `SD` library
(`framework-arduinoespressif32/libraries/SD`, Apache-2.0, unchanged except below).
Shared by the app (`firmware/app`, env `cardputer-adv` and the envs extending it) and
the hardware probes (`firmware/hwtest`, env `cardputer-adv` and `-gnu17`): both list
`../third_party` in `lib_extra_dirs`, which PlatformIO searches before the core's own
libraries. The core 3.x env (`hwtest` `cardputer-adv-p54`) keeps its own `SD`, whose
`f_mkfs` signature differs. The app uses only the `sdcard_*` functions of
`sd_diskio.h` (docs/09-m2-design.md §3.9), not the `SDFS` class.

Third-party code: outside the `CPA patch` sites it stays byte-for-byte upstream, and it
is not covered by the repository's size, lizard or clang-tidy checks. NOTICE lists it.

Patches (marked `CPA patch` in `src/sd_diskio.cpp` and `src/sd_diskio.h`; measurements
in `docs/gates/sd-card.md` §2 and §11):

1. The SPI-mode ACMD41 argument is `0x40000000` (HCS only) instead of `0x40100000`.
   With the upstream argument the test card never leaves the idle state after a reset.
2. `GET_BLOCK_SIZE` reports 8192 sectors (4 MB) instead of 1, so `f_mkfs` aligns the
   FAT32 data area (and so every cluster) to the card's 4 MB allocation units. The app
   never formats; the patch serves hwtest and later use.
3. The upstream 25 MHz cap on the SPI clock after init is kept unless
   `CPA_SD_UNCAPPED_CLOCK` is defined. Only hwtest defines it, so its clock sweep can
   try 26.7 and 40 MHz (out of spec without a CMD6 high-speed switch, which this driver
   never sends). The app runs at a fixed 20 MHz.
4. Diagnostics: `sdcard_diag()` / `sdcard_diag_reset()` report the first and last error
   event inside the driver (stage, command, token, sector), a histogram of busy waits and
   the longest busy after a Stop Tran token; `sdcard_read_register()` reads CID, CSD,
   SD Status and SCR; `CPA_SD_DIAG` marks this copy. Recording adds no bus traffic except
   one more CMD13 after a failed post-write status check. hwtest's `!sddiag`, `!sdid`,
   `!sdblk`, `!sdf3`, `!sdfile` and `!sdfatscan` use it.
5. After the Stop Tran token that ends a multi-block write (CMD25), the driver clocks one
   more byte and waits for the card's busy before deselecting. Upstream deselected at
   once, so the following CMD13 could go out while the card was still busy, read garbage
   (R1 0x07 on the test card) and fail the write with EIO although the card had taken
   the data.
