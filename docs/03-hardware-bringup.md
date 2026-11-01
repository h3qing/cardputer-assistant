**English** · [中文](03-hardware-bringup.zh-CN.md)

# Hardware bring-up notes: Mac ↔ Cardputer ADV (2026-10-19)

> Today was the first time the Cardputer ADV was connected to the Mac for bring-up. This records the pitfalls hit and the conclusions verified. ✅ means confirmed on the real device, 📚 means from official docs or source code, ⏳ means not yet verified.
> The test firmware is in `firmware/hwtest/`, and the Mac-side test program is `firmware/hwtest/host/hwtest_host.py`.

## 1. Device facts

| Item | Value | Source |
|---|---|---|
| Chip | ESP32-S3 (QFN56) rev v0.2, 40MHz crystal | ✅ esptool |
| Flash / PSRAM | Built-in 8MB (XMC, quad, 3.3V) / **no PSRAM** | ✅ esptool |
| MAC | 02:00:00:00:00:01 | ✅ |
| M5Unified board ID | `24` (board_M5CardputerADV) | ✅ reported by firmware |
| Heap (test firmware) | About 281KB free, largest block about 254KB (plus a 64KB static mic buffer) | ✅ |
| Battery voltage (USB plugged in) | About 4.15V | ✅ |
| Factory firmware | `cardputer-adv` V0.9-36-ge824a76, ESP-IDF v5.4.2, built 2025-10-21; partitions: nvs / otadata / phy / one 4MB factory app | ✅ backed up (see `backups/README.md`) |

## 2. USB connection: three pitfalls

### Pitfall 1: the factory firmware normally does not enable USB data (✅)
- Symptom: the screen lights up, but the Mac sees no USB device, and changing cables does not help.
- How to tell: read the state of the Mac's USB-C port controller with `ioreg -r -c AppleHPMInterfaceType10 -l`.
  - `ConnectionActive=Yes`, `TransportsActive=("CC","USB2")`: the Mac has detected the cable and opened the USB2 data channel
  - but `ioreg -p IOUSB` still shows no device: so **the problem is on the device side**, not Mac permissions or the cable
- It only enumerates in ROM download mode, as `303A:1001 USB JTAG/serial debug unit`, on port `/dev/cu.usbmodem1101`.

### Pitfall 2: the G0 latch trap (✅ measured + 📚 confirmed by Espressif in esptool #970)
- **Powering on with G0 held** makes the chip latch GPIO0 low, and **it stays that way until the next power-off**.
- After that, every reset the Mac triggers over USB (`rst:0x15 USB_UART_CHIP_RESET`) is only a "core reset" that does not re-read the strapping pins, so **every reset falls back into download mode** (`boot:0x23`). esptool's "Hard resetting via RTS" after flashing cannot get back to the firmware either.
- Fixes, pick any one:
  1. **Really power off**: switch to OFF, **unplug the USB cable**, wait a few seconds, plug back in. Flipping the switch alone does not work, because USB keeps supplying power.
  2. Press BtnRst on its own, without G0.
  3. Use esptool ≥ 4.9 with `--after watchdog_reset`.
- **Lesson: do not press G0 for everyday flashing.** Normally `pio run -t upload` enters download mode automatically through USB's "download flag", and returns to the firmware automatically after flashing. ✅ Measured, no keys needed.

### Pitfall 3: opening or closing the serial port on the Mac can reset the chip (📚 TRM + ✅ measured)
Truth table of the ESP32-S3 USB-Serial/JTAG control lines (order RTS,DTR):

| RTS,DTR | Action |
|---|---|
| 0,0 | Clear the download flag |
| 0,1 | Set the download flag |
| **1,0** | **Reset the chip** (if the download flag is set at reset, enter download mode, otherwise boot from Flash) |
| 1,1 | No action |

macOS pulls both lines high when it opens the port. **The safe way to open it** is the same as Espressif's own esp-idf-monitor:

```python
s = serial.Serial(); s.port = dev; s.baudrate = 115200
s.dsrdtr = True          # 打开时 pyserial 不去碰 DTR；RTS 默认是高电平，不会产生变化
s.open()
s.rts = False            # (0,1)：只设置下载标志，不会复位
s.dtr = False            # (0,0)：清掉下载标志，进入安全的空闲状态
# 关闭端口时两根线已经都是 0，系统再放低（HUPCL）也不会改变任何状态
```

(Comments: with `dsrdtr = True` pyserial does not touch DTR on open, and RTS defaults to high so nothing changes; `rts = False` gives (0,1), which only sets the download flag and does not reset; `dtr = False` gives (0,0), clearing the download flag into a safe idle state; when the port closes both lines are already 0, so the system lowering them (HUPCL) changes nothing.)

✅ Measured: after opening the port this way, the uptime `up_ms` reported by the firmware keeps growing, so the chip was not reset.
**To restart the device from the Mac in future**: add a firmware command that calls `esp_restart()` in the firmware, rather than toggling the control lines.

## 3. HWCDC "holds back" outgoing data (Arduino core 2.0.17, ✅ measured + 📚 source)
- Symptom: right after the Mac opens the port, the device's first reply gets stuck in the device and only comes out together with the next data the Mac sends. The longest stall seen was 11 minutes.
- Cause: `HWCDC` decides "is the host still there" from USB SOF signals (`isPlugged()`). Right after the host connects it wrongly decides it is disconnected, so the `IN_EMPTY` interrupt stops the transmit channel, and the data stays in the ring buffer unsent.
- How it is handled:
  - **Firmware**: the main loop reads `(bool)Serial` every 20ms, which triggers `isCDC_Connected()`, re-enables the `IN_EMPTY` interrupt and sends the held data.
  - **Mac side**: after connecting, send an `!info` handshake first; if no reply within 2 seconds, resend, at most 3 times.
  - Measured effect: after the handshake, round-trip latency is a steady ~60ms, and key events arrive in real time.
- ⚠️ Additional G2 measurement (see docs/adr/0001-toolchain.md): the kick is necessary but **not sufficient**. On core 2.0.17, after long blocking operations (e.g. playing a beep, writing SD), a reply occasionally still gets stuck until the Mac writes again. So when a reply has not arrived after about 500ms, the daemon sends an empty line to "nudge" it (the firmware drops empty lines as noise). The firmware's main loop should also avoid long blocking.
- Long-term option (⏳): evaluate Arduino core 3.3.9 or later (the HWCDC fix only landed in 3.3.9, PR #12606; 3.2.1 still needs the kick), or use ESP-IDF's `usb_serial_jtag` driver directly.

## 4. Toolchain (✅)
- **PlatformIO**: 6.2.0, installed with `uv tool install platformio --python 3.12`, command at `~/.local/bin/pio`.
- **Platform**: `espressif32@6.10.0`, i.e. Arduino core 2.0.17, ESP-IDF 4.4.7, bundled esptool 4.5.1.
  - Why this version is pinned: cores based on IDF 5.5 record all zeros on the ADV (ESP-IDF #18621). ⏳ The mic has not been measured on this toolchain yet.
- **Libraries**:
  - M5Cardputer at git tag `1.2.0`. Its library.json still says version 1.1.1, which is normal.
  - M5Unified `0.2.26`, M5GFX `0.2.33`, both compile on core 2.0.17.
- **Note**: IDF 4.4 has no `ESP_RST_USB` value. For a reset triggered over USB, the reset reason read in the firmware is `0` (unknown).
- **Test firmware resource use**: RAM 27% (89KB, of which 64KB is the mic buffer), Flash 521KB.

## 5. esptool lessons (✅)
- Always add `-b 2000000` when reading Flash: the default speed is only about 11KB/s, with it about 100KB/s, so 8MB takes only 84 seconds.
- If esptool is killed midway, the stub in the chip is left half-done. Use `--before default_reset` on the next connection.
- `--before no_reset` only applies when the chip is already in download mode.

## 6. Keyboard (✅ partly)
- `M5Cardputer.Keyboard.keyList()` gives all held keys in real time. The test firmware also reports each key's coordinates in the 4×14 layout and its row and column in the TCA8418 matrix.
- `Keyboard.isChange()` **only compares the number of held keys**, so you have to compare the full key set yourself.
- ⏳ **Whether three-key combos produce ghost keys has not been measured**; `Ctrl+Shift+S` and similar combos are still to be tested.

## 7. Mac-side troubleshooting commands
```bash
ioreg -p IOUSB -w0                                     # 有没有枚举出 USB 设备
ioreg -r -c AppleHPMInterfaceType10 -l | grep -E 'ConnectionActive|TransportsActive|PortDescription'
system_profiler SPUSBHostDataType                      # macOS 26 用这个，旧的 SPUSBDataType 已经没有了
/usr/bin/log show --last 10m ...                       # 注意：在 zsh 里 log 是内置命令，要写全路径
```

(Comments: the first shows whether a USB device enumerated; use `SPUSBHostDataType` on macOS 26, as the old `SPUSBDataType` is gone; in zsh `log` is a builtin, so write the full path.)

## 8. Impact on the architecture
1. **The Mac daemon owns the serial port exclusively**: it must use the "safe open", handshake with retries after connecting, and use `up_ms` or a boot ID to tell whether the device has restarted.
2. **The firmware keeps USB-Serial/JTAG (HWCDC) as the default mode**: so flashing resets automatically without pressing keys. No deep sleep while USB is connected. USB drive mode (MSC) and HID are only modes switched by hand.
3. **Restart the device with a firmware command**, not by toggling control lines; keep the TX kick in the firmware.
4. **Audio**: the toolchain is pinned to IDF 4.4 or 5.4; whether the mic works must be measured before deciding.
