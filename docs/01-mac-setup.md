**English** · [中文](01-mac-setup.zh-CN.md)

# Cardputer ADV × Mac: connection and setup guide

> Based on: official M5Stack docs (Chinese and English), the Cardputer-Adv / Stamp-S3A schematics, the official M5Stack and Espressif GitHub, and Apple support docs.
> Every step has a source link. Items marked "inferred" have no official wording; they are derived from the schematics or chip docs.
> Research date: 2026-10-19. Local environment: an Apple silicon Mac, macOS 26.

---

## 0. First, understand how it connects to a computer

| Item | Fact | Source |
|---|---|---|
| USB path | The **native USB built into the** ESP32-S3FN8 chip (USB-Serial/JTAG), D- = GPIO19, D+ = GPIO20, wired straight to the USB-C port, **no CH9102/CP210x bridge chip** | [Stamp-S3 schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1150/Sch_StampS3_v0.3.3.pdf), [ADV product page](https://docs.m5stack.com/zh_CN/core/Cardputer-Adv) |
| macOS driver | **No driver needed at all**, and **do not install** the CH9102/CP210x/FTDI drivers on M5's download page | [Espressif USB-Serial-JTAG](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_serial_jtag.html) |
| What the Mac normally sees | `/dev/cu.usbmodemXXXX`; VID:PID = `303A:1001` (ROM download mode / factory firmware / Arduino defaults) | [openocd esp_usb_jtag.cfg](https://github.com/espressif/openocd-esp32/blob/master/tcl/interface/esp_usb_jtag.cfg) |
| Running UiFlow2 | `303A:816B`, named "Cardputer-ADV(UiFlow2)" | [uiflow-micropython ADV board config](https://github.com/m5stack/uiflow-micropython/blob/master/m5stack/boards/M5STACK_CardputerADV/mpconfigboard.h) |
| Factory firmware's Keyboard app in USB mode | Becomes an **HID keyboard** called "CardputerADV", **with no serial port at that point** | [Factory firmware source](https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV) |
| Side power switch | **Only controls the battery**. With USB plugged in, there is power whatever the switch position (inferred from the schematic, consistent with the official "power off, hold G0, then plug in" procedure). **Charging requires the switch ON** (official wording) | [ADV schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf) |
| Chip | ESP32-S3FN8: 8MB Flash, **no PSRAM** | [ADV product page](https://docs.m5stack.com/en/core/Cardputer-Adv) |
| Side buttons | Along one edge, in order: BtnG0, OFF/ON switch, microSD slot, BtnRst, IR | [Official product photo](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Cardputer-Adv_operation_01.jpg) |

---

## 1. When the Mac cannot see the device at all, check in this order

> Current state: `/dev/cu.*` only has Bluetooth serial ports, and the USB tree has no devices at all.

1. **macOS "Allow accessories to connect"** (specific to Apple Silicon laptops, and the easiest to miss)
   - Path: System Settings → Privacy & Security → Accessories → "Allow accessories to connect", set to "Ask for new accessories" or "Automatically when unlocked"
   - After changing it, unlock the Mac first, then unplug and replug the Cardputer, and click **Allow** when prompted
   - Apple's wording: after denying, the Mac "won't recognize the accessory", but it **still charges**. So a lit screen and charging do **not** prove that data works.
   - Source: [Apple 102282](https://support.apple.com/en-us/102282)
2. **Use a USB-C cable known to carry data**, plugged **directly into the MacBook itself**, not through a dock, display or hub for now. Try the ports on both sides.
   Source: the [official Arduino guide](https://docs.m5stack.com/zh_CN/arduino/m5cardputer/program) requires a "USB-C data cable"
3. **Check with the right commands** (once before and once after plugging in, and compare what appears):
   ```bash
   ls /dev/cu.*
   ```
   ```bash
   system_profiler SPUSBHostDataType
   ```
   ```bash
   ioreg -p IOUSB -l -w0 | grep -i -E '12346|303a|jtag|espressif|m5stack|cardputer'
   ```
   On macOS 26 use `SPUSBHostDataType`; the old `SPUSBDataType` no longer exists.
4. **Force ROM download mode** (the official ADV procedure; this is the **decisive test**)
   1. Side switch to **OFF**
   2. **Hold G0**
   3. Plug in the USB-C cable
   4. Release G0

   The screen should be black (inferred: the ROM does not turn on the screen). Now run the commands from step 3 again:
   - **`303A:1001` appears**: the USB link is fine, and the currently installed firmware has turned USB off or taken it over (e.g. deep sleep, HID keyboard mode, crash loop). Reflashing fixes it.
   - **Still nothing**: go back to steps 1 and 2 (permission, cable, port); if that fails, try another computer.
   - Source: [ADV product page](https://docs.m5stack.com/zh_CN/core/Cardputer-Adv): "set the switch on the side of the Cardputer-Adv to OFF, hold the G0 button before powering on, and release it after the device is powered"
5. **Alternative way in** (with the cable already plugged in): hold G0, press and release BtnRst, keep holding G0 for about 1 second, then release.
   This is the general Espressif method and matches the schematic (BTN1 to EN, BTN2 to G0), but M5's ADV docs do not mention it.
6. **The port appears but cannot be opened**: only one program can hold `/dev/cu.usbmodem*` at a time. First close the Arduino serial monitor, M5Burner, mpremote, your own daemon, etc. With `screen`, quit with `Ctrl-A` then `K`; closing the window directly keeps the port locked.
7. **Not recognised on another computer, with another cable, even in download mode**: treat it as a hardware fault and contact support@m5stack.com.

---

## 2. Four setup paths (ordered by how well they suit this project)

### Path A: Arduino IDE 2 + the M5Cardputer library (recommended first, to verify two-way serial communication)

1. **(Recommended) Back up the factory firmware first**, in download mode:
   ```bash
   esptool -p /dev/cu.usbmodemXXXX -b 460800 read-flash 0 ALL cardputer_adv_factory_8MB.bin
   ```
2. Arduino IDE → Settings → Additional boards manager URLs, enter:
   `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json`
   (mirror in China: `..._index_cn.json`)
   Source: [arduino_board](https://docs.m5stack.com/en/arduino/arduino_board)
3. Search "M5Stack" in Boards Manager and install **3.2.2 or later** (latest is 3.3.10).
   **There is no separate ADV board option**; the ADV also uses **M5Cardputer**, and the library detects the hardware at runtime.
   ⚠️ **If you need the mic, pin the 3.2.x version first**. 3.3.x is based on IDF 5.5 and records all zeros on the ADV (ESP-IDF #18621 is not fixed yet; the CardMind project reproduced it on 3.3.9). For keyboard, screen and serial only, the latest version is fine. See section 8 of [02-landscape-report.md](02-landscape-report.md).
4. Search "M5Cardputer" in Library Manager and click Install All. Minimum versions:
   - M5Cardputer ≥ 1.1.0
   - M5Unified ≥ 0.2.8 (≥ 0.2.9 recommended, for battery reading)
   - M5GFX ≥ 0.2.10

   **With libraries that are too old, the ADV keyboard does not respond at all** (the ADV uses a TCA8418 I2C keyboard controller; the original Cardputer uses a GPIO matrix).
   Source: [keyboard page](https://docs.m5stack.com/en/arduino/m5cardputer/keyboard)
5. **Tools menu settings** (not on the official page; checked against arduino-esp32's boards.txt):
   | Option | Setting |
   |---|---|
   | Board | M5Stack → **M5Cardputer** |
   | USB Mode | **Hardware CDC and JTAG** (default, do not change) |
   | USB CDC On Boot | **Enabled** (default) |
   | Flash Size | **8MB** (⚠️ the default is 4MB; must be changed by hand) |
   | Partition Scheme | **8M with spiffs (3MB APP/1.5MB SPIFFS)** |
   | PSRAM | Disabled |
6. **First upload**: enter download mode first, pick `/dev/cu.usbmodemXXXX` in Tools → Port, open File → Examples → M5Cardputer → Basic → display, and click Upload. After it finishes, press BtnRst to run.
7. **Later uploads need no button presses**: as long as your firmware keeps USB-Serial/JTAG available, the host can reset the chip into download mode automatically.
   These cases make the port disappear and require flashing again with G0: deep sleep, switching to USB-OTG/TinyUSB mode, repurposing GPIO19/20.
   Source: [ESP-IDF USB-Serial-JTAG console](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html)
8. **Two-way serial test firmware** (put together by us, not an official example; all APIs used were checked): type a line on the keyboard and press Enter to send it to the Mac; lines sent from the Mac are shown on screen.
   ```cpp
   #include <M5Cardputer.h>
   String typed, rx;
   void setup() {
     auto cfg = M5.config();
     M5Cardputer.begin(cfg, true);          // true = 启用键盘
     Serial.begin(115200);                  // 原生 USB CDC 会忽略波特率
   #if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
     Serial.setTxTimeoutMs(0);              // 没有主机在读时不阻塞
   #endif
     M5Cardputer.Display.setRotation(1);
     M5Cardputer.Display.setTextScroll(true);
     M5Cardputer.Display.println("ready");
   }
   void loop() {
     M5Cardputer.update();
     if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
       auto st = M5Cardputer.Keyboard.keysState();
       for (char c : st.word) typed += c;
       if (st.del && typed.length()) typed.remove(typed.length() - 1);
       if (st.enter) { Serial.println(typed); typed = ""; }
     }
     while (Serial.available()) {
       char c = (char)Serial.read();
       if (c == '\n') { M5Cardputer.Display.println(rx); rx = ""; }
       else if (c != '\r') rx += c;
     }
   }
   ```
   (Code comments: `true` = enable the keyboard; native USB CDC ignores the baud rate; do not block when no host is reading.)
   Do not write `while(!Serial)`: HWCDC only returns true when the host has opened the port, so without a serial monitor open it hangs there forever.
9. **Reading and writing on the Mac**:
   ```bash
   screen /dev/cu.usbmodemXXXX 115200
   ```
   To quit: press `Ctrl-A`, then `K`.

   Skeleton of a Python daemon: **find the port by VID:PID**; do not hard-code the port name, because it can change after a reset.
   ```python
   import serial, serial.tools.list_ports as lp
   port = next(p.device for p in lp.comports() if (p.vid, p.pid) == (0x303A, 0x1001))
   s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 1
   s.dtr = False; s.rts = False   # 先拉低再 open，避免误触发复位（中等可信度，来自论坛）
   s.open(); s.write(b'{"type":"ping"}\n'); print(s.readline())
   ```
   (Comment: pull both low before open, to avoid triggering a reset by accident; medium confidence, from a forum.)
   ⚠️ **Measured on this Mac (2026-10-19)**: on macOS, even with DTR/RTS set to False before open, opening the port with pyserial still reset the chip via USB-JTAG (`rst:0x15 USB_UART_CHIP_RESET`) and put it into download mode (`boot:0x23`). So how the daemon can open the port without an accidental reset has to be verified separately once our own firmware is flashed.

### Path B: PlatformIO (good for keeping the firmware and the Mac daemon in one git repo)

The official config from the ADV product page:
```ini
[env:m5stack-cardputer]
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
upload_speed = 1500000
monitor_speed = 115200
build_flags =
    -DESP32S3
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Cardputer=https://github.com/m5stack/M5Cardputer
```
- This config pins Arduino core 2.0.16; `esp32-s3-devkitc-1` has 8MB Flash, matching the ADV.
- `CORE_DEBUG_LEVEL` can be lowered for everyday use.
- The first flash needs download mode; after that `pio run -t upload` resets automatically.
- The new `espressif32 v8.0.0` (released today) adds an `m5stack-cardputer` board definition, **but it declares 4MB Flash**. To use it, override it to 8MB by hand.
- Source: [ADV product page](https://docs.m5stack.com/en/core/Cardputer-Adv)

### Path C: ESP-IDF (full control; the factory firmware is written with it)

```bash
git clone -b CardputerADV https://github.com/m5stack/M5Cardputer-UserDemo
cd M5Cardputer-UserDemo && python3 ./fetch_repos.py
idf.py build && idf.py -p /dev/cu.usbmodemXXXX flash monitor
```
- Needs ESP-IDF v5.4.2.
- Suits cases that need to manage USB, BLE, WiFi and low power together, or that want to reuse the factory TCA8418 keyboard and ES8311 audio drivers directly.
- ⚠️ The factory firmware **prints the WiFi SSID and password in plain text** to the serial log in `hal.cpp`. Do not copy that into your own firmware.

### Path D: UiFlow2 / MicroPython (no C++, fast prototyping)

1. Install [M5Burner](https://docs.m5stack.com/en/uiflow/m5burner/intro). The macOS build is x64 only, so on Apple Silicon you most likely need Rosetta first (unconfirmed):
   ```bash
   softwareupdate --install-rosetta
   ```
2. In M5Burner pick CARDPUTER → **"UIFlow2.0 Cardputer-Adv"** (latest v2.5.3). ⚠️ **Do not pick "UIFlow2.0"**; that one is for the original Cardputer.
3. Enter download mode → Burn → fill in the COM port and 2.4GHz WiFi, set Boot Option to "Show startup menu and network setup" → Start → press reset when done.
4. Connect in one of two ways:
   - Enter the Access Code shown on the screen at [uiflow2.m5stack.com](https://uiflow2.m5stack.com/)
   - Use the USB WebTerminal (based on Web Serial; needs Chrome or Edge)
5. You can also use the MicroPython REPL directly (inferred to work, not officially documented):
   ```bash
   mpremote connect /dev/cu.usbmodemXXXX repl
   ```
6. The cost: UiFlow2 uses TinyUSB (`303A:816B`), so **every reflash needs G0**, with no automatic reset.

### Path E: Restore the factory firmware (unbricking, also a hardware check)

M5Burner → CARDPUTER → **"Cardputer-Adv User Demo"** (v0.3) → enter download mode → Burn → port `/dev/cu.usbmodemXXXX`, baud rate 1500000 → Start.
⚠️ **Do not pick "Cardputer User Demo"** (that is the original). The dialog title in the official tutorial screenshot also says "Burn Cardputer User Demo"; the official screenshot is mislabelled.
Source: [Factory restore guide](https://docs.m5stack.com/en/guide/restore_factory/cardputer_adv)

---

## 3. Original Cardputer vs ADV: what is shared and what must not be mixed

| Shared | **Must not be mixed** |
|---|---|
| Arduino quick start, board option M5Cardputer, download mode steps, switch ON to charge | Firmware packages: the ADV can only use the User Demo and UIFlow2.0 marked "-Adv" |
| | Keyboard: the ADV has a TCA8418 over I2C, the original a GPIO matrix |
| | UiFlow2 USB PID: ADV is 816B, the original 81DA |
| | Library versions: libraries that are too old make the ADV keyboard fail completely |

---

## 4. What this means for the project

- **Use USB-Serial/JTAG (CDC) as the main link**: no driver, found automatically by VID:PID, automatic reset when flashing; the best fit for "plug into the computer and it syncs".
- **To mount the SD card as a USB drive (MSC) or emulate an HID keyboard**: you have to switch to TinyUSB/OTG mode, at the cost of losing the automatic reset, so every flash needs G0. Better to make it a "mode" that can be switched at runtime, not something entered by default at boot.
- **No PSRAM**: only about 512KB of SRAM is available. The recording buffer, TLS and UI all have to be frugal. The detailed capacity calculation will be in the deep research report.
- **A lead worth following**: cross-checking turned up an Anthropic plugin called [cwc-makers](https://claude.com/cwc-makers) (with an m5-onboard skill inside), which lets a Cardputer-Adv flashed with UiFlow2 pair with Claude Desktop's "Hardware Buddy" over BLE. This is highly relevant to your idea and will be checked specifically in the deep research report.
