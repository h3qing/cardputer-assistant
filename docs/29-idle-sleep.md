**English** · [中文](29-idle-sleep.zh-CN.md)

# 29 · Sleep: saving power when the device sits unused

> Status: **draft design, waiting for your decision** (§11) · Date: 2026-10-28 · Role: firmware (power) · Based on origin/main `677863e`, plus `feat/keep-until-right-mac` (#84, `7382e33`) and `fix/boot-scan-stack` (#85, `fe8c2db`), which will merge first. Related: docs/03 (the G0 latch trap), docs/11 §4 (recording, battery thresholds), docs/13 §7 (lock screen), docs/19 (lock card, toast), docs/20 D2, D7, D8 (lock when the screen goes dark, lighting by itself, "● Wait"), docs/21 (settings page), docs/28 (ledger, kept copies, plug-in hint, boot scan).
> Requirement (2026-10-28): when the device is left alone for a long time it stays on and drains the battery; add a sleep feature to save power.
> Tags: **[src]** code or docs read · **[meas]** existing real-device measurement · **[inf]** inference · **[asm]** design assumption, can change · **[todo]** to verify on the real device · **👤** needs you.
> Scope: design and plan only. No device connected, no code changed.
> Path abbreviations: `fw/` = `firmware/app/lib/`; `M5C/` = M5Cardputer 1.2.0 source (`.pio/libdeps/cardputer-adv/M5Cardputer/src/`); `M5U/` = M5Unified 0.2.26 source; DS = ESP32-S3 datasheet v2.2; TRM = ESP32-S3 Technical Reference Manual v1.8; IDF = ESP-IDF v4.4.7 docs.

## 0. One-page summary

1. **Why the battery runs out today** (§1): on battery, after 60 seconds with no key press the backlight goes off and the screen locks (docs/20 D2). But the chip keeps running at full speed: the main loop has no `delay()` at all, so the CPU is 100% busy [src `src/main.cpp:1-4`, `loop()`]. Only the backlight is off. Estimating from the datasheet, the whole device still draws tens of mA with the screen dark, so a 1500 mAh battery is empty in one to two days [inf].
2. **Three tiers; the user only sees one setting** (§3):
   - **Tier 0: dark screen** (exists already). On battery, 60 seconds with no key press: dim first, then screen off and lock.
   - **Tier 1: light sleep** (new). Once the screen is black and locked, on battery, with nothing in progress, enter ESP32-S3 light sleep. All memory is kept. G0 or any keyboard key **wakes it in under 1 millisecond** [inf, IDF]. After waking it is exactly like today's black screen: locked, "tap, then hold" to record a memo, and no more of the start is lost than today.
   - **Tier 2: deep sleep** (new). **2 hours** after the last effective key press (changeable in settings to 30 minutes / 2 hours / 8 hours / never), enter deep sleep, which is like a soft power-off. G0 or any key boots it again, and the lock card shows after about 1.5 seconds. Things in memory (current page, toast) are lost; recordings on the card, waiting entries, the ledger and settings all remain.
3. **When it never sleeps** (§4): while recording a memo or a meeting; when plugged into a Mac (a host is visible on USB); while a ledger command or the boot scan is unfinished; when the editor has unsaved text (blocks deep sleep only); while the lock card is lit; while a key is held. **With only a charger plugged in and no Mac**, it sleeps as on battery, and still charges while asleep.
4. **Can G0 be a wake key?** (§5): yes. Holding G0 at **power-up** enters download mode (docs/03 pitfall 2), because power-up is a "chip reset" that latches GPIO0 again. Waking from deep sleep is a "core reset" and does not latch again [src DS §3 "At Chip Reset, the latches sample…"; TRM table 7.1-1: 0x05 Deep-sleep reset = Core Reset]. The USB reset (0x15) measured in docs/03 is also a core reset and also does not read G0 again. Light sleep does not reset at all. **This conclusion has not been verified on the real device yet** (P0 item 4). One exception must be guarded against: a brown-out reset, when the voltage drops, is a chip reset and does latch again. So at low battery the device must deep-sleep on its own before brown-out (§7).
5. **USB is the hardest part** (§6): during light sleep, USB-Serial-JTAG stops working. The host may report errors, and after waking it may not re-enumerate, so the only fix is to unplug and replug the cable [src IDF usb-serial-jtag-console]. Three measures:
   - don't sleep while a host is plugged in;
   - before light sleep, **disconnect the D+ pull-up** in software, so the Mac thinks the cable is not plugged in (TRM register `USB_SERIAL_JTAG_PAD_PULL_OVERRIDE` / `DP_PULLUP`);
   - during light sleep, wake every 60 seconds, connect for 300 ms, and check for a host.

   Result: plug in a Mac while the device sleeps and it connects by itself within 1 minute at most; press any key and it connects at once.
6. **How much it saves** (§2; there is no ammeter, so every number needs measuring in P0 by voltage slope):

   | State | Estimated current | How long 1500 mAh lasts |
   |---|---|---|
   | Today's dark screen | about 45–70 mA [inf] | about 1 day |
   | Light sleep | about 1–5 mA [inf] | about 2–6 weeks |
   | Deep sleep | about 0.05–1 mA [inf] | over 2 months, close to self-discharge |
   | Power switch off | 0.23 µA [src M5Stack] | — |

   The chip alone: light sleep 240 µA, deep sleep 7–8 µA [src DS tables 5-9/5-10]. The extra for the whole device (LDO, SD card, LED, divider resistors) can only be measured.
7. **G0 press to first sample** (§8):
   - today 507 ms [meas docs/07:882];
   - light sleep: under 20 ms more [inf];
   - deep sleep: the first press only boots, which takes about 1.5 seconds [meas: reboot to Ready 1.55–1.66 s, docs/07:267,547], then "tap, then hold", about 2.2–2.6 seconds counted from the first press. The "● Wait" on screen (docs/20 D8) tells you when you can speak, so the start is never lost silently.
8. **Phases** (§10):
   - **P0**: measurement and probes (test build only).
   - **P1**: spin less with the screen dark. A one-line change, zero risk, takes half the gain first.
   - **P2**: light sleep.
   - **P3**: deep sleep, the setting, low-battery protection.
   - **P4**: one extra line of hint on the Mac side.
9. **16 decisions** all have defaults (§11.1). The 4 you need to answer are in §11.2. The one with the biggest impact is item 1: deep sleep after 2 hours by default.

## 1. How it works today

### 1.1 Power policy (pure functions, `fw/shell/power.*`)

- On battery, dim after 50 seconds with no key press, screen off at 60 seconds (`kBacklightIdleMs`, `kPreDimMs`) [src power.h:22-23; power.cpp:32-46].
- The moment the screen goes off it locks automatically (docs/20 D2) [src runtime.cpp:196-197 `if (power_.dark && !was_dark) shell_.auto_lock()`].
- When plugged into a Mac the screen stays on and does not lock [src power.cpp:34-39]. "Plugged in" means `HWCDC::isPlugged()`, that is, SOF frames keep arriving on USB and there is a host [src usb_transport.cpp:25]. **With only a charger, `isPlugged()` is false and it is treated as battery.**
- While recording, the backlight is not turned off, only dimmed (the ● rule) [src power.cpp:64-70].
- While locked, keys swallowed by the lock gate **do not** refresh `last_key_ms` (the gate comes before `power_key`) [src runtime.cpp:169-183]. So random presses in a pocket do not keep the device "in use".

### 1.2 What the chip does after the screen goes black

- Arduino's `loopTask` calls `loop()` without pause, and `loop()` explicitly forbids `delay()` [src main.cpp:1-4]. With the screen dark the CPU still spins at 240 MHz at full load.
- The ST7789 display controller does not go to sleep (only the backlight is off) [src m5_display.cpp:76-80]. The SD card stays mounted.
- When the audio task is idle, the microphone and speaker are both off. M5Unified powers down the analog part of the ES8311 at `Mic.end` (`0x0D=0xFC`, `0x00=0x00` CSM power down) [src M5U/M5Unified.inl:1553-1557]. In other words, **the codec is already in a low-power state when idle**.
- The battery voltage is read once a second (`kStatsEveryMs` 1000) [src runtime.h:50; hal_platform.cpp:16].
- docs/28's tidy of kept copies also runs when no Mac is connected, executed by the audio task [src runtime_kept.cpp:155-164].

### 1.3 Keyboard

- The Cardputer ADV keyboard is a TCA8418 keyboard chip on I2C (G8/G9). Its interrupt pin INT is wired to **G11** [src M5C/utility/Keyboard/KeyboardReader/TCA8418.cpp:13].
- M5Cardputer's reader attaches a CHANGE interrupt on G11. The interrupt only sets a flag, and `update()` reads one event each time, only when the flag is true [src TCA8418.cpp:43-66].
- **What matters for sleep**: for a press made during sleep, INT went low long ago. After waking there is no further edge, so the flag never gets set. So we need our own reader: M5Cardputer allows injecting one with `Keyboard.begin(std::unique_ptr<KeyboardReader>)` [src M5C/utility/Keyboard/Keyboard.h:153] (§9.2).
- G0 does not go through the TCA8418; it is read directly from GPIO0 [src M5U/M5Unified.inl:3573-3576].

### 1.4 Existing battery rules

| Threshold | Effect | Source |
|---|---|---|
| ≤3.55 V | On-screen warning while recording | rec_core.h:62 `kBatWarnMv` |
| ≤3.45 V for 10 s | Recording closes normally; new recordings are refused | rec_core.h:56 `kBatStopMv` |
| 3.30 V–4.15 V | The two ends of the status bar percentage | menu_model.cpp:15-16 |

When not recording, nothing happens however low the battery gets.

## 2. Hardware facts (research results, with sources)

| Question | Conclusion | Source |
|---|---|---|
| Chip light sleep current | 240 µA (VDD_SPI, WiFi off) | DS table 5-10 |
| Chip deep sleep current | 7 µA (RTC memory only) / 8 µA (plus RTC peripherals, needed for ext0 and RTC pull-ups) / 1 µA (CHIP_PU pulled low) | DS table 5-10 |
| Chip idle (both cores WAITI) | 80 MHz 22–36 mA; 240 MHz 33–48 mA. A busy loop is higher | DS table 5-9 |
| Whole device | Typical operation 120.2 mA @4.2 V; 0.23 µA with the power switch off | docs.m5stack.com/en/core/Cardputer-Adv |
| Battery | 1500 mAh (since 2026-07-13; earlier batches were 1750 mAh) | Same, change log |
| RTC GPIO | GPIO0–21. **Both G0 and G11 are**; backlight G38 is not | DS §2.3.2 |
| Pins | Keyboard INT G11; I2C G8/G9; backlight G38 (LEDC PWM); LCD RST/DC/MOSI/SCK/CS = G33/34/35/36/37; SD CS/MOSI/CLK/MISO = G12/14/40/39; battery ADC G10 (2:1 divider); RGB LED G21 | Schematic v1.0; M5GFX; M5U/Power_Class.inl:585; M5U/M5Unified.inl:247-259 |
| Charge detection | The charger chip TP4057's CHRG/STDBY are not wired to a GPIO, and there is no VBUS detect pin | Text extracted from the schematic [todo: confirm on the drawing] |
| Deep sleep wake sources | ext0 (one RTC pin); ext1 (several RTC pins; S3 has `ESP_EXT1_WAKEUP_ANY_LOW`); timer | IDF sleep_modes (v4.4.7) |
| Light sleep wake sources | `gpio_wakeup_enable()` + `esp_sleep_enable_gpio_wakeup()`, any GPIO, level-triggered; timer; ext0/ext1 also work | Same |
| Automatic light sleep | **Not available**: in the Arduino 2.0.17 precompiled libraries `CONFIG_PM_ENABLE` is off and there is no tickless idle. Only a manual `esp_light_sleep_start()` is possible | arduino-esp32 2.0.17 `tools/sdk/esp32s3/sdkconfig:1147` |
| USB during sleep | USB-Serial-JTAG stops in both light and deep sleep. After light sleep the host may not enumerate again; it "may need to unplug and replug". IDF 5's `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` only covers automatic light sleep; IDF 4.4 does not have this option | usb-serial-jtag-console in IDF v4.4.7 and latest |
| Software USB disconnect | `PAD_PULL_OVERRIDE` + `DP_PULLUP` in `USB_SERIAL_JTAG_CONF0_REG`; the RTC domain has its own set, `RTC_CNTL_USB_PAD_PULL_OVERRIDE` / `RTC_CNTL_USB_DP_PULLUP` | TRM register 33.3; TRM §RTC_CNTL |
| Time | The RTC timer "keeps system time across any reset and sleep mode"; only power-on clears it. The clock source is the internal 150 kHz RC; "time may drift in deep and light sleep" | IDF v4.4.7 system_time; sdkconfig:988 `RTC_CLK_SRC_INT_RC` |
| Wake latency | Light sleep: tens of µs to about 1 ms (forum measurements). Deep sleep: it is a reboot; sdkconfig has `DEEP_SLEEP_WAKEUP_DELAY=2000` µs | esp32.com viewtopic p=152556; sdkconfig:993 |
| A setting that may cost power | `CONFIG_RTC_CLOCK_BBPLL_POWER_ON_WITH_USB=y`: when USB is in use, the BBPLL stays on during sleep | sdkconfig:1092 [todo: measure its cost] |
| TCA8418 | 3 µA standby; pulls INT low on a key press | ti.com/product/TCA8418 |
| ST7789 sleep | About 5–30 µA after SLPIN (0x10); after SLPOUT (0x11) wait 120 ms before drawing | Secondary source, done.land [todo] |
| SD card idle | 0.1–1 mA, a few mA for some cards; the slot has no power switch | Secondary sources [todo] |

**What others do**:

- **M5Unified**: for Cardputer / Cardputer ADV it only configures the battery ADC, with no wake pin (`_wakeupPin` is 255), so its `deepSleep()` / `lightSleep()` can only wake on a timer [src M5U/Power_Class.inl:585]. Its generic code has practices worth copying:
  - deep sleep uses ext0; with ext1 `ANY_LOW` it adds `rtc_gpio_pullup_en`;
  - light sleep uses `gpio_wakeup_enable(LOW_LEVEL)`;
  - **before sleeping, wait for the wake pin to be released** (`_releaseWakeupPin`), otherwise it wakes the moment it sleeps.
- **M5Cardputer's official UserDemo**: the ADV branch has no sleep code.
- **M5Launcher**: its "sleep" only turns off the backlight, and the press that wakes it is swallowed.
- **Bruce**: the Cardputer `powerOff()` is an empty function.

**In other words, there is no existing Cardputer ADV wake implementation to copy.**

## 3. Three-tier design

### 3.1 State diagram

```
                 Effective key press (a key that passed the lock gate)
        ┌──────────────────────────────────────────────┐
        ▼                                              │
     [Bright] ──50 s──▶ [Dim] ──10 s──▶ [Dark + locked] │   Tier 0, exists today
                                        │               │
                     All gates pass (§4), on battery    │
                                        ▼               │
                                   [Light sleep] ◀──┐   │   Tier 1, new
                     Wakes itself every 60 s:       │   │
                     reads battery, checks USB host, sleeps again ┘
                     G0 / any key → wakes, key goes through the lock gate as usual ──┘
                                        │
         2 h (setting) since the last effective key press, all gates pass;
         or battery ≤3.45 V for 10 s
                                        ▼
                                   [Deep sleep]              Tier 2, new
                     G0 / any key → boots again → locked, lock card lit 3 s
                     Wakes itself every 10 min: screen stays off, only checks USB host and battery
```

### 3.2 Entry, wake and what is lost, per tier

| | Entry | Wake | Usable after | What is lost |
|---|---|---|---|---|
| Dark screen (exists) | On battery, 60 s with no effective key press | Any key | At once | Nothing |
| Light sleep | Backlight already Off (dark, locked, card not lit), on battery, all §4 gates pass | G0 (GPIO0 low), keyboard INT (G11 low), 60 s timer | Awake in < 1 ms; in the loop in < 5 ms [todo] | Nothing. Memory, page, draft, clock and boot id all remain |
| Deep sleep | `sleep_after` (default 2 h) since the last effective key press, or battery critical (§7), and all gates pass and the editor has no unsaved text | ext1 `ANY_LOW`: G0 + G11; 10 min timer (probe only, §6.3) | Lock card in about 1.5 s [inf: boot to Ready 1.55–1.66 s, docs/07] | Memory: current page, toast, Tab menu position; new boot id (`rst:"deepsleep"` in hello). Card, NVS, ledger and offline marks remain; the clock is carried over by the RTC and marked "approx." (§8.2) |

### 3.3 Why light sleep has no grace period

Once the screen is black and locked, light sleep costs nothing:

- waking takes under 1 ms;
- on battery there is no USB connection to interrupt;
- whatever can be done on a black screen (lock gate, G0 gesture) still works after waking.

A grace period would only use more power and buy nothing. The only thing to wait for is **the lock card going off**: a lit card means someone is looking, and the backlight is not Off.

### 3.4 Why deep sleep waits so long

Deep sleep has two costs: about 1.5 seconds more to wake, and what is in memory is lost. 2 hours covers "on the desk during the day, used now and then": the device stays in light sleep and reacts at once. "Forgot to switch it off at night, left in a bag for a week" is handled by deep sleep. If measured light sleep current is low enough (≤1 mA), this default can relax to 8 hours (👤 item 1).

### 3.5 After waking from deep sleep

- **It wakes locked, with the lock card lit for 3 seconds** (same as `lock_engage(now, kLockFlashMs)` in docs/20 D7). It was locked before sleeping (on battery, dark means locked), and it wakes in the same state. If something in a pocket wakes it, nothing can be recorded.
- **If nobody uses it, straight back to deep sleep**: if, after this wake from deep sleep, there is no effective key press before the screen goes black, it does not go into light sleep for 2 hours but back to deep sleep at once. So one accidental press in a pocket costs only about 1.5 s of boot power, not 2 more hours in light sleep.
- After boot, the first screen is the lock card plus a line "Waking…". It is drawn as soon as M5 and display init finish; mounting the SD card and the boot scan come after it. So there is feedback about 0.3–0.5 seconds after the press [todo], and nobody thinks it did not react and presses again.

## 4. When not to sleep (gates)

Light and deep sleep share one gate table. If any one is not met, it does not sleep this pass and checks again on the next loop pass.

| # | Condition | Why | How it is known |
|---|---|---|---|
| G1 | Backlight is Off | If the screen is lit or dimmed, someone is looking, or it is recording (the ● rule) | `backlight_of(power_) == Off` [src power.cpp:64] |
| G2 | The recording state machine is Idle | No sleep while recording a memo, in a meeting, on a confirm box, or closing. A meeting is a recording | `rec_core_.phase == RecPhase::Idle` |
| G3 | No host on USB | No sleep when plugged into a Mac: the screen stays on and the link must be up. Plugged into a Mac with cpad not running: also no sleep | `tx_.plugged()` is false, and `plug_.plugged` is false |
| G4 | The ledger is idle | No ledger command sent without a reply, no next round of clearing, no pending tidy (docs/28 D17, D18: only the audio task writes the ledger, the UI waits for its reply) | `!kept_.busy && !kept_.drop_due && !kept_.tidy_due` |
| G5 | The audio task is idle | The boot scan is done, the `file.*` queue is empty, the audio task holds no command | `rec_core_.queued == 0 && !rec_core_.at_task`, plus a new `scan_done` |
| G6 | No plug-in hint showing | docs/28 D11's 5-second hint and its Hold wait for LedgerDone | `kept_.plug == PlugHint::None` |
| G7 | No key held | With a key held (G0 or any key) it does not sleep; otherwise it would wake right after sleeping, or miss the release edge | This pass's `KeySet` is empty; G0 debounce has finished |
| G8 | Settings writes have landed | `settings_due_` is false (the deferred write after a recording ends) | runtime_settings.cpp |
| G9 | Test hooks idle | In `CPA_TEST_HOOKS` builds, the debug injection queue is empty | debug_inject |
| G10 (blocks deep sleep only) | No unsaved text in the editor | Deep sleep loses memory; a draft must not be lost silently | Whether the shell's compose is dirty |

**USB with power only, no Mac** (charger, power bank): `isPlugged()` is false, it is treated as battery, and it sleeps normally. While asleep the TP4057 keeps charging (hardware charging, no CPU needed). The cost: while charging, the screen also goes off and locks. That is how it is today (docs/20 D2), unchanged.

## 5. G0 and the boot pin

docs/03 pitfall 2 [meas]: holding G0 at **power-up** makes the chip latch GPIO0 as low, entering download mode until the next power loss; a USB reset initiated by the Mac (`rst:0x15`) is a core reset and does not latch again.

- **Datasheet**: the pin latches "sample at Chip Reset, and hold until the chip is powered down or shut down" [src DS v2.2 §3, around line 1638].
- **TRM table 7.1-1**:
  - Chip reset is only 0x01 (power-on, brown-out detection, super watchdog);
  - **0x05 Deep-sleep reset is a Core Reset**, the same class as 0x15 USB (UART) reset;
  - 0x0F brown-out can be a chip reset.
- **Conclusions**:
  - Light sleep: no reset; G0 is just an ordinary GPIO wake source.
  - Deep sleep: waking is a core reset and does not latch GPIO0 again. **Waking with G0 held does not enter download mode**, the same class as the USB reset already measured in docs/03.
  - **Not yet verified on the real device.** One post was found online about an ESP32 (not S3) "entering download mode on waking from deep sleep", with no cause (community.mongoose-os.com/t/2334). P0 item 4 tests exactly this: wake with G0 held 20 times; `esp_reset_reason()` must be `ESP_RST_DEEPSLEEP` every time and the firmware must start every time.
- **The exception to guard against**: a brown-out reset may be a chip reset and latch again. If G0 happens to be held when the battery drains to brown-out, the device gets stuck in download mode until the power switch is flipped. The critical deep sleep in §7 exists to stop the device before brown-out.
- Implementation detail: after an ext1 wake, GPIO0 and GPIO11 are still RTC IO. Call `rtc_gpio_deinit()` first, then hand them to M5Unified and the keyboard reader as digital pins [src IDF sleep_modes].

## 6. USB: what happens when a Mac is plugged in during sleep

### 6.1 The problem

During light sleep USB-Serial-JTAG has no APB clock. With a host plugged in, the host reports "device not responding"; after waking D+ stays pulled up and the host may not enumerate again, so it "may need to unplug and replug" [src IDF latest usb-serial-jtag-console]. We already don't sleep when a host is present (G3), **but the user may plug in a Mac while the device is asleep**. There must never be a stuck state of "plugged in, nothing happens, and even after a key wakes it, it can't connect".

### 6.2 Light sleep: disconnect first, connect on a timer to check

1. **Before light sleep**: turn off the D+ pull-up with `PAD_PULL_OVERRIDE` [src TRM register 33.3]. From the Mac's side the cable is not plugged in. Plug in the Mac now and it sees nothing, so there is no failed enumeration.
2. **On every wake** (key or timer): restore the pull-up first, so the Mac sees a clean "just plugged in".
3. **Timer wake (every 60 s)**:
   - read the battery once (about 2 ms);
   - connect the pull-up and wait at most 300 ms for SOF (`HWCDC::isPlugged()`);
   - host present: stay awake and switch to the plugged-into-Mac rules (screen on, link up, docs/28's plug-in hint);
   - no host: disconnect again and sleep again.
4. **Cost**: 0.3 s awake per minute at about 45 mA is about 0.23 mA on average [inf]. If P0 finds macOS cannot send SOF within 300 ms, widen the window to 500 ms, or change the interval to 120 s.
5. **What you see**: plug in a Mac while the device sleeps and it connects by itself within 1 minute at most; press any key and it connects at once (the pull-up is connected right after waking).

### 6.3 Deep sleep: timed probe boots

- In deep sleep the digital domain is powered off, the D+ pull-up is gone too, and the Mac cannot see the device.
- A timer wake every 10 min is a **probe boot**: at the very start of `setup()`, if the reset reason is `DEEPSLEEP` and the wake cause is the timer, it does only three things:
  - start USB;
  - wait at most 500 ms for SOF;
  - read the battery with `analogReadMilliVolts(10) × 2`.

  With no host, it writes a power log entry (§9.4) and goes straight back to deep sleep.
- A probe boot **does not initialise the screen or mount the SD card**. The backlight pin G38 is pulled low and held with `gpio_hold_en` + `gpio_deep_sleep_hold_en` before sleep, so the screen does not flash during the probe.
- With a host, it continues as a normal boot: locked, card lit.
- **Cost**: about 0.8 s × 40 mA / 600 s, about 0.05 mA on average [inf].

### 6.4 The Mac side (cpad)

- Light sleep: the device never sleeps while connected to a Mac, so cpad sees no difference. During a timed probe, if the cable goes to a Mac, the Mac sees a normal plug-in.
- Deep sleep: waking is a fresh boot, with `rst:"deepsleep"` in hello [src hal_platform.cpp:73] and a new `boot`. cpad already treats a new boot as a normal restart, and `rst` is only checked against `^[a-z0-9_]{1,16}$` [src daemon specs.py:135]. **cpad does not need to change.**
- The `debug.sleep` test hook (§9.4) sleeps on purpose while connected to a Mac. HIL scripts must then accept the serial port disappearing and coming back: cpad already handles unplug and replug; the HIL side needs to add a wait to reopen the port.

## 7. Low battery

| Voltage (on battery, not recording) | Action |
|---|---|
| ≤3.55 V (`kBatWarnMv`) | The wait before deep sleep shrinks to **10 minutes**, whatever the setting, "never" included. The battery text on the lock card turns the Warn colour (exists already) |
| ≤3.45 V (`kBatStopMv`) for 10 s | **Critical deep sleep**:<br>1. The screen lights for 3 s, showing "Battery low · sleeping · please charge";<br>2. If the editor has unsaved text, save it to NVS first (§7.1);<br>3. Wait for gates G2, G4, G5 to pass, at most 10 s;<br>4. Unmount the SD card;<br>5. Deep sleep |
| Woken by a key after critical deep sleep, or a timed probe | Read the battery only, don't mount the SD card:<br>- still ≤3.45 V and no host: light for 3 s "Battery low · please charge", sleep again;<br>- host present (charging): boot normally |

- Unchanged while recording: the recording closes itself at 3.45 V for 10 s (docs/11 §4.3). Only after closing back to Idle does the table above take effect.
- **The SD card is not damaged**:
  - before deep sleep, the gates make sure the audio task is idle and every ledger command has replied;
  - docs/28 D17's write order makes sure every ledger record is fsynced before its ack;
  - outbox writes complete synchronously in the loop;
  - finally `f_unmount`, and CS is pulled high and held.

  This is much safer than "voltage drops to brown-out and power is lost mid-write": at most the ledger loses its last record, which is the one before fsync anyway, and the only consequence is "delivered one more time" (docs/28 §3.4).
- Voltage readings: on a wake from sleep the reading is the **unloaded voltage**, a few tens of mV higher than under load [inf]. The critical decision keeps the "for 10 s" rule, with readings taken while awake and under load. The unloaded voltage read at a probe boot is only used to decide "boot normally or not", so the threshold gets a 50 mV margin [asm, P0 calibrates].

### 7.1 Drafts

- **Deep sleep waits for drafts** (G10): with unsaved text in the editor, only light sleep, no deep sleep. Light sleep loses nothing.
- **Critical deep sleep cannot wait**: write the draft (the editor's target + text, at most the editor's limit) to NVS key `draft`. After the next boot and unlock, the editor still holds this text, and the hint bar has an extra line "Draft restored". NVS writes are journalled and safe on power loss. The write happens at 3.45 V, before brown-out voltage is reached.

## 8. Timing

### 8.1 G0 press to first sample

| Pressed from | What the first press does | To first sample | Can the start be lost silently? |
|---|---|---|---|
| Bright / dimmed | Hold to record | 507 ms [meas docs/07:882,993] | No: "● Wait" shows as soon as it goes down (D8) |
| Dark + locked (today, on battery) | Tap to light and arm, then hold | 507 ms from the second press | Same |
| **Light sleep** | Same as dark + locked | About 507–527 ms from the second press [inf: wake < 1 ms + loop resume + USB pull-up restore (non-blocking)]. Budget: ≤ 20 ms more than today | Same |
| **Deep sleep** | The first press only boots: lock card in about 1.5 s | After the card lights, tap again and hold; 507 ms from the second press; about 2.2–2.6 s from the first press [inf] | No: before the card lights and "● Wait" appears, speech was never being recorded, and the screen tells you |

- **The codec stays as it is during sleep**; neither light nor deep sleep powers it down further.
- When idle, M5Unified has already powered down the ES8311's analog part (§1.2). So waking from light sleep does not need another "warm-up" (`warm()`, 0.47 s, issue #31) [src audio_io.h:13-18].
- Waking from deep sleep is a fresh boot, and boot already runs warm once (after the audio task's boot scan). It is most likely finished before the lock card shows [todo].

### 8.2 Clock

- **Light sleep**: after waking, IDF measures how long it slept with the RTC timer and adds it to `esp_timer`, so `millis()` is continuous [src IDF system_time]. The boot id does not change and `up_ms` is continuous; to cpad it is the same boot.
  - Drift comes from the calibration error of the 150 kHz internal RC. There is no published figure; P0 item 6 measures it: sleep 30 min, then connect the Mac and compare `up_ms` with the host clock.
- **Deep sleep**: `millis()` starts from 0, but the RTC timer keeps running. Before sleep, write `ClockRef` (docs/28 D16: the wall time last seen + the `up_ms` at that moment) together with `esp_rtc_get_time_us()` into RTC memory (`RTC_NOINIT_ATTR`, with a magic number and CRC). On waking: wall time = the stored time + the time the RTC has run.
  - **This clock is marked "approx."**: the status bar and recording times use it as usual; docs/28's "delete after 7 days" for kept copies does not use it and waits for the next `time.set`. This extends D16, "if the clock can't be trusted, don't delete by time".
- **Power switch off**: time is lost, as today. The board has no RTC chip.
- `millis()` is 32 bits and wraps at 49.7 days. With the setting at "never" and continuous light sleep, this can really happen. Time comparisons in the code all use unsigned `now - x`, which is wrap-safe [src power.cpp:40 and others], but a wrap test must be added (§9.3).

## 9. Implementation notes

### 9.1 Layers (following 05 §5: pure logic + ports + HAL)

| Layer | File | What it does |
|---|---|---|
| Pure policy | `fw/shell/sleep.h/.cpp` (new, about 150 lines) | `SleepInputs` (each gate, voltage, `sleep_after`, last effective key, `woke_deep_no_key`) → `SleepPlan {None, Light{max_ms}, Deep{why}}`; low-battery and critical timing (10 s sustained) also live here |
| Runtime | `fw/shell/runtime_sleep.cpp` (new, about 120 lines); a few lines in `runtime.h` | Collects the gates and gives `sleep_plan(now)`; `woke(cause, now)` handles waking (restore the USB pull-up, mark an effective key); `park_for_deep(now)` makes the audio task close up, unmounts SD, writes the RTC record |
| Port | `fw/shell/platform.h` | Adds `virtual WakeCause light_sleep(const LightSpec&)`, `virtual void deep_sleep(const DeepSpec&)`, `virtual void usb_attach(bool)`, all no-ops by default. `FakePlatform` and the simulator each implement them |
| HAL | `fw/hal/sleep_hal.cpp/.h` (new, about 200 lines) | `esp_sleep_*`; `gpio_wakeup_enable(0/11, LOW)`, restored after waking; ext1 `ANY_LOW` + `rtc_gpio_pullup_en`; `PAD_PULL_OVERRIDE`; hold on G38, G12, G37; the I2C lock held across sleep; before sleep wait for G11 to go high (copying M5Unified's `_releaseWakeupPin`), and don't sleep on timeout |
| HAL | `fw/hal/kb_reader.cpp/.h` (new, about 100 lines) | Our own TCA8418 reader, injected via `Keyboard.begin(std::unique_ptr<KeyboardReader>)`. Two things more than the original:<br>- `drain()`: before sleep, read the FIFO empty and clear INT;<br>- `kick()`: after waking, if INT is still low, set the flag directly.<br>One `update()` reads the FIFO until empty, not one event at a time |
| HAL | `fw/hal/rtc_keep.cpp/.h` (new, about 80 lines) | RTC memory record: `ClockRef`, deep sleep reason, `woke_deep_no_key`, power log ring (64 entries × 8 B) |
| Audio task | `fw/hal/audio_task.*` | Adds `idle()` (scan done, no command) and a `park()` command (close files, `f_unmount`, reply). Only the audio task touches SD (docs/28 D18) |
| Entry | `src/main.cpp` | At the end of `loop()`, ask `sleep_plan()`; for light sleep call `light_sleep()` then `woke()`; the very start of `setup()` is the deep-sleep probe boot branch (§6.3) |
| Settings | `fw/core/settings.h`; `fw/hal/nvs_settings.*`; `modules/system/settings_logic.*`, `system_render.cpp`, `system_texts.h`; `ModuleContext` in `fw/module_api` | `SleepAfter {M30, H2, H8, Never}`, NVS key `sleep` (u8; if unreadable, the default H2); the third row on the settings page; `ModuleContext` gains `sleepAfter()` / `setSleepAfter()` with default implementations, so third-party apps are not affected |

### 9.2 Light sleep sequence (`sleep_hal.cpp`)

1. Take the I2C lock (the audio task is idle, but it must not touch the codec midway).
2. `kb.drain()`; G11 must be high and G0 high, otherwise give up this pass.
3. The backlight PWM is already 0; `gpio_hold_en` on G38.
4. Disconnect the USB pull-up.
5. `gpio_wakeup_enable(GPIO_NUM_0, LOW)`, `gpio_wakeup_enable(GPIO_NUM_11, LOW)`, `esp_sleep_enable_gpio_wakeup()`, `esp_sleep_enable_timer_wakeup(60 s)`.
6. `esp_light_sleep_start()`.
7. On waking:
   - `gpio_wakeup_disable` on both pins, and restore G11's CHANGE interrupt type;
   - `gpio_hold_dis` on G38;
   - read `esp_sleep_get_wakeup_cause()`;
   - if it was a keyboard wake, `kb.kick()`;
   - release the I2C lock.
8. Timer wake: read the battery, connect the pull-up, wait at most 300 ms for SOF (checking keys every 10 ms during this time); if none, go back to step 2.

- **Preventing a wake-again loop**: if 5 GPIO wakes in a row read no key at all, no light sleep for 10 minutes, and a `spurious` entry goes into the power log.
- **Should the LCD get SLPIN?** Not at first (D14). P0 measures the difference between SLPIN and none during light sleep; add it if ≥1 mA. If added, SLPOUT on wake is non-blocking: send the command, note the time, and don't draw or turn on the backlight for 120 ms.

### 9.3 Tests (written first in each phase, RED → GREEN)

**Host (`test/core`, pure logic)**

- `test_sleep_policy`:
  - G1–G10 each block on their own, table-driven;
  - light sleep starts the moment the backlight goes Off;
  - deep sleep counts from the last effective key press; swallowed keys do not count (random pocket presses don't extend it);
  - 2 h / 30 min / 8 h / never;
  - ≤3.55 V shrinks it to 10 min ("never" shrinks too);
  - 3.45 V for 9.9 s does nothing, 10 s is critical;
  - after a deep sleep wake with nobody using it, back to deep sleep;
  - `now` starting at `0xFFFF0000`, crossing the wrap.
- `test_settings`: `SleepAfter` default, equality, `save_when`; an unreadable NVS byte uses the default.

**UI (`test/ui`, `FakePlatform` records `light_sleep` calls)**

- `test_runtime_sleep`:
  - recording, a meeting, a confirm box, a host plugged in, a ledger command in flight, a pending tidy, the plug-in hint, a key held, the lock card lit: each prevents sleep;
  - the key that wakes from light sleep goes into the lock gate exactly once;
  - Opt+L pressed during light sleep (Opt is the wake key): after waking L arrives too, and lights the card (guards against the FIFO being cleared, §1.3);
  - G0 tap then hold: the memo starts as usual, and the start time recorded by `FakeRecPort` is the same as without sleep;
  - with a draft in the editor, only light sleep, no deep sleep;
  - at critical battery the draft goes to NVS and comes back on the next boot.
- `test_settings_sleep_row`:
  - the cursor (`;` `.`) can stop on the third row; Enter cycles the four values;
  - row widths and toast widths in Chinese and English stay within bounds (`test_text_width`, `i18n/width.py`);
  - leaving and coming back puts the cursor on the WiFi row (docs/21).
- The existing `test_lock_power`, `test_lock_ux`, `test_plug_hint`, `test_runtime_kept` all still pass.

**Simulator (`firmware/sim`)**

- `SimPlatform::light_sleep()`: the virtual clock jumps straight to the next scripted key press or the 60 s poll, and returns the wake cause. `deep_sleep()`: takes `SimDevice`'s existing restart path, carrying the RTC record.
- New script `scripts/ux/sleep.cpas`, with screenshots of:
  - the settings page with three rows (Chinese, English);
  - the low-battery card;
  - the "Waking…" card after deep sleep;
  - the lock card after waking from deep sleep;
  - draft restore.
- `cpa-sim serve` + a new `tools/hil` scenario `sleep_sim`: use `debug.sleep` to put the virtual device into light and deep sleep, and check boot id, `up_ms`, `rst`.

**Real-device automated (`test/device`, test build)**

- `test_sleep_hal`:
  - timed light sleep 2 s, wake cause TIMER, `esp_timer` advances by about 2 s;
  - after waking, I2C can read the TCA8418 and SD can read a file;
  - 100 in a row with no spurious wake;
  - `kb_reader`'s drain / kick on events placed in the FIFO beforehand (can TCA8418 registers inject them? [todo]; if not, press by hand).

**HIL (serial, needs the device, `tools/hil`)**

- `sleep_usb`: `debug.sleep {"tier":"light","ms":5000}` → the port disappears → timer wake after 5 s, pull-up connected → the port is back within 2 s, cpad is Ready again, boot unchanged, `up_ms` continuous and about 5 s more. Repeat 20 times, 0 hangs.
- `sleep_deep`: `debug.sleep {"tier":"deep","ms":10000}` → new boot, `rst:"deepsleep"`, Ready within 3 s. 20 times.
- `sleep_gates`: while recording a memo, in a meeting, with a `file.*` request in flight, or with a ledger command, `debug.sleep` returns a refusal with the reason and does not sleep.

**Manual gates (you press, H-S1–H-S7, recorded in docs/07)**

| Gate | How | Pass |
|---|---|---|
| H-S1 G0 deep sleep wake | After deep sleep, hold G0 to wake, 20 times, 10 of them held for over 3 s | 20/20 normal boot, `rst=deepsleep`, never download mode |
| H-S2 Light sleep wake keys | After light sleep, wake with G0, a letter key, and Opt+L | Exactly like today's dark, locked screen; one Opt+L lights the card |
| H-S3 Light sleep memo | After light sleep, tap G0, then hold and speak, 10 times | `memo_lead_ms` (press to first sample) ≤ today + 20 ms |
| H-S4 Plug in a Mac while asleep | 5 times each in light and deep sleep | Connects by itself within ≤ 60 s from light sleep, ≤ 10 min from deep sleep; ≤ 3 s after any key; 0 times needing to unplug |
| H-S5 Charger only | Plug in a power bank, leave 30 min | Sleeps normally; after unplugging and plugging in the Mac it connects |
| H-S6 Low battery | Discharge to around 3.5 V, leave it | Deep sleep in 10 min; at 3.45 V the low-battery card shows; boot scan of the SD card finds no bad blocks |
| H-S7 Pocket | Locked, in a pocket, walk for 30 min | No accidental recording, no accidental save; after being woken from deep sleep it goes back within 60 s |

### 9.4 Measuring current without an ammeter

- **Power log**: a 64-entry ring in RTC memory. Each entry records RTC seconds, voltage (mV), tier (dark / light / deep / probe), and wake cause. One entry every 60 s in light sleep, every 10 min in deep sleep. It survives deep sleep and soft reset; only the power switch clears it.
  - Reading it: after plugging into the Mac, the test build reads it with `debug.state.power`; the release build adds a row "Drain −8 mV/h (last sleep 6 h)" to the "Device" page in settings.
- **Method** (once each in P0, P2, P3):
  1. Charge full, unplug, rest 30 min (let the voltage settle);
  2. Keep the device in the tier under test for 8 hours (overnight);
  3. Plug in and read the log;
  4. Take the mV/hour over the 3.7–3.9 V plateau.
- On the plateau, the voltage slope is roughly proportional to current. First measure **today's dark screen** once (`debug.sleep {"off":true}` turns sleep off) as the baseline. Convert this baseline to mA with one discharge from full to auto-stop:
  - baseline current ≈ 1500 mAh ÷ hours;
  - another tier's current = baseline × the ratio of the two slopes.
- The deep sleep slope may be too small to see in 8 hours (ADC resolution and noise are about 10–20 mV). Then leave it 3 days, or just record "≤ some upper bound".
- **Wake latency**: on waking, the test build records three times: `esp_timer` (the moment of waking), first entry into the loop, first key event. It also records `memo_lead_ms`. All of these go in `debug.state.power.last_wake`.

## 10. Phases

| Phase | What | Where | Tests written first | Budget | Risk |
|---|---|---|---|---|---|
| **P0 measurement and probes** | Power log + `debug.state.power` + `debug.sleep` (test build only). Measure on the real device:<br>① today's dark-screen baseline slope, plus one full discharge;<br>② light sleep current: LCD SLPIN or not, SD mounted or not;<br>③ deep sleep current: BMI270 and RGB LED state;<br>④ G0 deep sleep wake 20 times (H-S1);<br>⑤ whether macOS re-enumerates cleanly after the D+ pull-up is disconnected / connected, and how long SOF takes;<br>⑥ clock drift over 30 min of light sleep;<br>⑦ the cost of `BBPLL_POWER_ON_WITH_USB` | `fw/hal/rtc_keep.*`, `sleep_hal.*` (probe version), `runtime_debug.cpp`, `tools/hil` | `test_sleep_hal` (device); `sleep_usb`, `sleep_deep` (HIL) | Test build only; 0 bytes in release | If ⑤ fails, the whole light sleep plan changes (§12 R1) |
| **P1 spin less with the screen dark** | On battery with backlight Off, `vTaskDelay(10 ms)` at the end of each `loop()` pass, so the idle task enters WAITI. No frequency change, USB untouched | `src/main.cpp`; `runtime` provides `idle_slow()` (which is G1 + G2) | First half of `test_runtime_sleep` (when to slow down); HIL `rec_memo` regression | Key sampling ≤ 10 ms later; `rec_memo`'s 507 ms unchanged (±10); chip current from about 40–60 mA down to about 25–35 mA [inf DS table 5-9] | Low. No slowing while recording (G2) |
| **P2 light sleep** | All of §9.2; our own keyboard reader; USB disconnect / probe; power log in the release build; one row on the Device page | The parts of the §9.1 table other than deep sleep and settings | §9.3's core, ui, sim, device, `sleep_usb`; H-S2–H-S5 | flash ≤ 6 KB, RTC memory ≤ 600 B, RAM ≤ 128 B; per-pass checks in the loop ≤ 20 µs; wake to loop ≤ 5 ms; light sleep average ≤ 3 mA (including polling) [target, set by P0] | Medium: USB, keyboard interrupt, spurious wakes |
| **P3 deep sleep + setting + low battery** | ext1 wake; probe boot; `park()`; clock carried by RTC; wake locked, back to deep sleep if unused; third settings row (Chinese and English); shorten at ≤3.55 V, critical deep sleep at 3.45 V; draft to NVS | Deep sleep and settings in §9.1, `audio_task` `park()` | All of `test_sleep_policy`; `test_settings_sleep_row`; `sleep_deep`; H-S1, H-S6, H-S7 | Deep sleep average ≤ 0.5 mA (including the 10 min probe) [target]; press to "Waking…" card ≤ 0.5 s, to lock card ≤ 2 s [todo]; the settings page still fits the 111 px content area | Medium: boot path, SD unmount, low-battery readings |
| **P4 one line on the Mac** | Add "the device may be asleep: press any key" to "Don't see the device?" in the README and the Mac app | `README.md`, Mac app text | None | — | Low |

**P1 can be done right away**: it does not depend on P0, gains about half of today's dark-screen current, and has almost no risk. P2 and P3 parameters (poll interval, window, deep sleep default) are set once P0's numbers are in.

### 10.1 Settings page (P3)

The settings page gains a third editable row. The divider moves from y=55 to y=69, and the fact rows start at y=72. The two fact rows end at y=96, still inside the content area (y≤122) [calculated, layout from docs/21 §4.2].

```
[header] ↑0                       Wed 09:41 94%
(Settings) (Device) (Storage)
▸WiFi              None · USB cable only
 Language          English
 Sleep             2 h
 ─────────────────────────────────────
 Link   USB cable · MacBook          online
 Sync   3 min ago · to sync ↑0 R2

[;.]pick [Enter]change [`]back [Tab]menu
```

- Keys: Opt+M, `.`, `.`, Enter. Each Enter switches to the next value: 30 min → 2 h → 8 h → Never → 30 min. One change from the default takes 4 presses.
- Feedback is **silent**:
  - the value in the row changes on the spot;
  - a toast in the hint bar position spells out the meaning.
- The NVS write goes through docs/21 §6.8's `save_when` (if changed while recording, written after the recording).
- `Txt` pairs (all must pass `i18n/width.py`):

| Key | Chinese | English |
|---|---|---|
| `kSleepRow` | 休眠 | Sleep |
| `kSleep30m` | 30 分钟 | 30 min |
| `kSleep2h` | 2 小时 | 2 h |
| `kSleep8h` | 8 小时 | 8 h |
| `kSleepNever` | 从不 | Never |
| `kSleepToast` | {N}不用就深睡 · 任意键醒 | Deep sleep after {N} idle · any key wakes |
| `kSleepToastNever` | 不深睡 · 黑屏时仍浅睡 | No deep sleep · light sleep when dark |
| `kWaking` | 醒来中… | Waking… |
| `kBatSleep` | 电量低 · 已休眠 · 请充电 | Battery low · sleeping · please charge |
| `kBatCharge` | 电量低 · 请充电 | Battery low · please charge |
| `kDraftBack` | 上次的草稿 | Draft restored |
| `kPowerRow` (Device page) | 耗电 | Drain |

## 11. Decisions, and what needs your call

### 11.1 Decisions

| # | Topic | Default | Alternatives | Why | See |
|---|---|---|---|---|---|
| D1 | How many tiers | Dark screen (exists) → light sleep → deep sleep | Deep sleep only; IDF automatic light sleep | Light sleep wakes in under 1 ms and loses nothing, covering daily use; deep sleep handles "forgot to switch off for a week". Automatic light sleep would mean rebuilding the Arduino libraries | §3 |
| D2 | When to enter light sleep | The moment the backlight is Off, all gates pass, no grace period | Wait N more minutes after the screen goes dark | Light sleep on a black screen costs nothing; a lit lock card does not count as Off | §3.3 |
| D3 | Light sleep wake sources | G0, keyboard INT (G11), 60 s timer | G0 only | Matches today, where every key works on a black screen; the timer is for checking battery and USB | §3.2 |
| D4 | The waking press | Light sleep: goes into the lock gate as usual (same as today's black screen). Deep sleep: only boots, lock card lit 3 s | The deep sleep press counts too | Light sleep must be exactly like today; before booting, deep sleep cannot know which key was pressed | §3.5, §8.1 |
| D5 | When not to sleep | G1–G10 in §4 | — | Recording, meetings, the Mac connection, the ledger and transfers must not be interrupted | §4 |
| D6 | USB power only | Treated as battery: sleeps and charges as usual | No sleep while plugged in | The board cannot tell a charger apart from a "Mac without cpad running"; it still charges while asleep | §4 |
| D7 | USB during light sleep | Disconnect the D+ pull-up before sleep, connect on waking; probe for 300 ms every 60 s | Don't disconnect; rely on keys only | Without disconnecting there is a stuck state of "Mac plugged in, can't connect, must unplug" | §6.2 |
| D8 | Deep sleep default | 2 hours from the last effective key press; settings offer 30 min / 2 h / 8 h / never | 30 min; 8 h; no deep sleep | Using it now and then through the day never hits deep sleep; a night left alone goes into deep sleep | §3.4 👤 1 |
| D9 | What deep sleep loses | Memory (page, toast); no deep sleep with an unsaved draft; clock carried by RTC, marked "approx." | Save the draft and then deep sleep | A draft must not be lost silently; a carried-over clock must not be used to delete kept copies | §3.2, §7.1, §8.2 |
| D10 | Deep sleep wake | ext1 `ANY_LOW`: G0 + G11; probe boot every 10 min, screen off | Keys only | Plug in a Mac while asleep and it connects by itself within 10 minutes, at a cost of about 0.05 mA | §6.3 👤 3 |
| D11 | How it looks after deep sleep | Locked: first "Waking…", then the lock card; if unused, straight back to deep sleep | Straight to home | It was locked before sleeping; a wake in a pocket must not leave it unlocked | §3.5 👤 2 |
| D12 | Low battery | ≤3.55 V: deep sleep after 10 min; ≤3.45 V for 10 s: critical deep sleep; still low on waking: light the card and sleep again | Warn only | Stop before a brown-out reset: protects the SD card and avoids G0 being latched again | §7, §5 |
| D13 | The setting | Third row "Sleep" on the settings page, Enter cycles; silent, value changes on the spot + toast | On the Device page; not changeable | Only the deep sleep time is worth letting you change; light sleep is invisible to you | §10.1 |
| D14 | LCD during light sleep | No SLPIN at first; add it if P0 measures ≥1 mA, with non-blocking SLPOUT | Always SLPIN | One less variable ("can't draw for 120 ms after waking") | §9.2 |
| D15 | First step | P1, spin less with the screen dark (`vTaskDelay`): take half the gain first | Go straight to light sleep | Zero risk, testable on its own | §10 |
| D16 | How to measure | RTC power log + voltage slope + one full-discharge calibration; one "Drain" row on the Device page | Buy an ammeter | You have no ammeter; the slope can compare the tiers as ratios | §9.4 |

### 11.2 Your call

1. **Deep sleep default** (D8): 2 hours by default. If you often go half a day between uses and don't mind waiting 1.5 seconds more on waking, pick 30 minutes. If you want an instant reaction whenever you press all day, pick 8 hours (if P0 measures light sleep at ≤1 mA, I will recommend switching to 8 hours).
2. **Deep sleep wakes locked** (D11): locked by default, card lit 3 s, same as before sleep. Cost: to type after a deep sleep wake, unlock with Opt+L, Enter: 2 more presses. Alternative: straight to home, which leaves it unlocked for about 60 seconds when woken in a pocket.
3. **How long until a Mac plugged in during sleep connects by itself** (D7, D10): by default 1 minute from light sleep and 10 minutes from deep sleep, costing about 0.23 mA and 0.05 mA. Alternative: wake by key only, which saves the most power, but after plugging in the screen stays dark and the Mac cannot see the device, so you have to remember to press a key.
4. **Draft at critical battery** (§7.1): by default saved to NVS and put back in the editor on the next boot. Alternative: don't save; the screen says the draft was not saved.

## 12. Risks

| # | Risk | Consequence | Mitigation |
|---|---|---|---|
| R1 | Disconnecting / connecting the D+ pull-up does not trigger a clean re-enumeration on macOS (IDF 4.4 HWCDC keeps its own state) | Plugging in a Mac during sleep needs an unplug | Verify first in P0 ⑤. If it fails: `HWCDC::end()` before light sleep and `begin()` after waking; if that fails too, only light-sleep when "no Mac has been connected for more than N minutes", and explain it on the Device page |
| R2 | `BBPLL_POWER_ON_WITH_USB=y` keeps the PLL on during light sleep | Light sleep current a few mA higher | Measure in P0 ⑦. If so, try stopping the USB peripheral clock before sleep; this option cannot be changed in the precompiled libraries |
| R3 | High leakage across the device (LDO, RGB LED G21, SD card, BMI270, G10 divider) | Deep sleep gains less than estimated | Measure in P0 ③; do everything controllable (BMI270 suspend, SD CS high, LED data pin pulled low and held); document what is not |
| R4 | G0 deep sleep wake enters download mode on the real device (contrary to the manual) | Waking with G0 bricks it until the power switch is flipped | Test 20 times in P0 ④ first. If it really fails, deep sleep wakes only with G11 (keyboard), and G0 only wakes light sleep |
| R5 | The keyboard interrupt flag is lost during sleep; one event at a time cannot keep up | The first key after waking, or Opt+L, is lost | Our own reader with `drain()` / `kick()`; the Opt+L case in `test_runtime_sleep` |
| R6 | Spurious wake loop (INT stuck low, G0 bouncing) | Can't sleep, using more power than not sleeping | Before sleep, confirm both pins are high; after 5 empty wakes in a row, stop for 10 minutes and log it |
| R7 | Slow RC clock drift | After days of light sleep, `up_ms` and wall time differ by minutes | Measure in P0 ⑥; corrected at the next `time.set`; an "approx." clock is never used to delete kept copies |
| R8 | With "never", continuous light sleep for over 49.7 days wraps `millis()` | A comparison somewhere that doesn't use unsigned subtraction goes wrong | Wrap test; the low-battery rules reach deep sleep before then |
| R9 | Task watchdog, I2C lock and M5Unified's internal state during sleep | Hangs or restarts after waking | I2C lock held across sleep; `test_sleep_hal` 100 times in a row; the watchdog stops with the clock in light sleep [todo] |
| R10 | The LEDC backlight pin level is undefined in light sleep | Screen faintly lit, more power used | G38 pulled low with `gpio_hold_en` |
| R11 | Unloaded voltage is higher than loaded | A probe boot wrongly thinks there is enough battery | 50 mV margin on the threshold, calibrated in P0 |
| R12 | Light sleep tested on FakePlatform cannot catch hardware problems | Host tests all green, the real device fails | Every phase has device and HIL gates; manual H-S1–H-S7 |

## Appendix: evidence index

- **Firmware**:
  - `fw/shell/power.h:1-23`, `power.cpp:14-74`;
  - `fw/shell/runtime.cpp:37-64` (boot), `169-217` (keys, timing, backlight);
  - `fw/shell/runtime_kept.cpp:155-164` (`pump_ledger`);
  - `fw/shell/lock.h:1-24`;
  - `fw/usb_transport/usb_transport.cpp:25`;
  - `fw/hal/keyboard.cpp`; `fw/hal/hal_platform.cpp:14-24,65-77`; `fw/hal/m5_display.cpp:11,76-80`; `fw/hal/audio_io.h:1-20`;
  - `fw/rec/rec_core.h:56,62`; `fw/help/menu_model.cpp:15-16`; `fw/core/settings.h`;
  - `modules/system/settings_logic.h:1-40`; `src/main.cpp`.
- **Libraries**:
  - `M5C/utility/Keyboard/KeyboardReader/TCA8418.cpp:13,28-66`; `M5C/utility/Keyboard/Keyboard.h:152-153`, `Keyboard.cpp:15-37`;
  - `M5U/M5Unified.inl:247-259` (RGB LED G21), `1045-1067` (speaker), `1537-1558` (microphone on/off, ES8311), `3573-3576` (G0);
  - `M5U/utility/Power_Class.inl:585`.
- **Docs**:
  - docs/03 pitfall 2;
  - docs/07:267,547 (restart to Ready), 882,993 (memo 507 ms), 570,630 (G-A7 battery life not yet measured, no voltage log);
  - docs/11 §4.3; docs/13 §7; docs/20 D2, D7, D8; docs/21 §4.2, §6.8; docs/28 D11, D16–D18.
- **External**:
  - ESP32-S3 datasheet v2.2 §3, table 5-9, table 5-10;
  - ESP32-S3 TRM v1.8 table 7.1-1, register 33.3 (`USB_SERIAL_JTAG_CONF0_REG`), RTC_CNTL USB pad registers;
  - IDF v4.4.7: sleep_modes, system_time, usb-serial-jtag-console (and the same page in latest);
  - arduino-esp32 2.0.17 `tools/sdk/esp32s3/sdkconfig` (lines 988, 993, 1081-1092, 1147);
  - docs.m5stack.com/en/core/Cardputer-Adv and the schematic Sch_M5CardputerAdv_v1.0;
  - github.com/m5stack/M5Unified `Power_Class.inl`;
  - the suspend sequence in esp-adf `es8311.c` (not needed for this design; M5Unified already powers down the analog part when idle);
  - ti.com/product/TCA8418;
  - esp32.com viewtopic p=152556 (light sleep wake latency);
  - community.mongoose-os.com/t/2334 (counterexample, no cause given);
  - the Cardputer `interface.cpp` of M5Launcher and Bruce.
