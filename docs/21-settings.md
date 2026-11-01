**English** · [中文](21-settings.zh-CN.md)

# 21 · Settings: WiFi and language (Chinese / English)

> Role: settings, multi-language and WiFi design (Lead summary). Date 2026-10-22, revised the same day after review (how the 18 review comments were handled is at the end). Target is `integrate/v041` (`e4c2c68`: v0.4.0 + Mando theme docs/19 + small-screen interaction docs/20 + soak fixes). Read the code, docs and font data; used scripts to inventory UI text and measure widths; built 4 compile-only WiFi probe firmwares that were never flashed. **No product code changed, no device connected, no serial port opened.**
> Requirement (2026-10-22): add a system settings page that shows WiFi and connection options, and that can switch the whole system to English.
> Markers follow docs/13: **[src]** code or doc read · **[test]** measured (build, script) · **[calc]** estimated, or measured with a script · **[inf]** inferred · **[asm]** design assumption, can change · **[todo]** needs simulator or real-device check · **👤** needs you.
> Widths use the real glyphs of efontCN_12: ASCII 6 px, Han characters and full-width punctuation 12 px, the 11 symbols `● … ★ ↑ ← → ◎ ◇ ○ · ×` 6 px. **`▸ ✓ ⌫ —` are not in the font**; the `▸` in the mockups below only marks "selected row".
> Memory is always written in **KiB** (1024 B), matching `150u * 1024u` in `test_runtime_budget`; images are in MB (10⁶ B).
> **Evidence is committed with this doc**, in `docs/gates/settings-probes/` (how to rerun is in the evidence index at the end):
> - `i18n/`: `inventory.py` (inventory), `efont.py` (width measurement, a Python port of the simulator's `firmware/sim/lib/simfb/efont.cpp`), `en_draft.py` (357 draft English strings), `measure.py`, `width.py`, `hints.py`, `strings.txt` (new strings from this doc);
> - `wifi/`: 4 WiFi probes (the rerun commands and results are in `platformio.ini`) and `mapsum.py`. Toolchain is the same as the product (espressif32@6.10.0, Arduino 2.0.17, IDF 4.4.7, the same USB CDC options).

## 0. One-page summary

**In one sentence**: add "Settings". **Language**: switch the whole device to English in three presses; it takes effect at once and survives power loss. **WiFi**: Settings **shows the connection** (today the "connection" is the USB cable and which Mac) and explains the WiFi situation. This version does not build wireless into the firmware: once it is linked in, even if always off, the free heap at boot drops by about 31 KiB and falls below the 150 KiB stability floor.

**What you will see** (Opt+M):

```
[头] ↑0                          Wed 09:41 94%
(Settings) (Device)
▸WiFi              None · USB only
 语言 Language     English
 ─────────────────────────────────────
 Link   USB · 工作 MBP              online
 Sync   3 min ago · to sync ↑0 R2

[;.]pick [Enter]change [`]back [Tab]menu
```

- Opt+M is renamed "Settings" and opens the **Settings** tab first; today's diagnostics move to the tab on the right (press `/`). No new keys.
- The cursor always starts on the **WiFi row**. Enter on that row only shows an explanation, so a stray press does no harm.
- **Switch language**: Opt+M, `.`, Enter: three presses. Takes effect at once, no restart, survives power loss. The "语言 Language" row reads the same in both languages, so you can always find your way back after picking the wrong one.
- The **WiFi** row says "None · USB only". Opening it explains: this firmware has no WiFi; data only goes over the USB cable to your Mac; to use WiFi, power-cycle into the Launcher and switch to the factory firmware; plus a warning that "the factory firmware prints WiFi passwords in plain text to serial". The device does not jump into the Launcher for you (§4.4).
- The **Link** and **Sync** rows: which Mac the USB cable connects to, whether it is online, when the last sync was, and how many items have not been sent.

**Why WiFi is not in this version** [test][calc]:

- Probes built with the same toolchain: just linking WiFi in (even if never turned on) cuts free heap at boot by 31,704 B (scan only) to 34,336 B (can connect). v0.4.0 measured 182,944 B (178.7 KiB) free heap, only 28.7 KiB above the 150 KiB floor; after the cut it is **147.7 / 145.1 KiB, below the floor**.
- How much more it takes when actually turned on has **never been measured** on S3 with IDF 4.4; a guess from other platforms is 35–50 KiB [inf]. It must be measured on the real device first (§5.7 G-WIFI item 4).
- The IDF 4.4 toolchain is already EOL with no security fixes; ADR 0001 decided to change toolchain before adding any radio.

**How to do it later** (§5):

- **W1 listen only, no connect: not scheduled.** The device scans nearby networks itself. Its only use is "remind you to switch profile when unplugged" (docs/18). First check whether the Mac-side signal is enough (0 B on the device); if it is really needed, it is docs/18's P8-W, it must pass G-WIFI first, and it only scans when not connected to a Mac and while you are looking at the WiFi page.
- **W2 connect to a network: not scheduled.** Only a few invariants are fixed here (§5.6): the password is typed on the Mac, travels over USB, never goes into logs; WiFi is always off while recording and while transferring recordings; the device never restarts itself.

**How to do language** (§6):

- The firmware has 373 Chinese literals in 35 files, 357 after de-duplicating per file, about 300 after global de-duplication, and no string table [calc].
- Each UI string becomes a pair `constexpr Txt{"中文", "English"}`, kept next to the code that uses it. A missing language, Han characters in the English, mismatched `%` placeholders, or exceeding its class width limit all **fail to compile**.
- English is wider than Chinese on average (median ×1.20). Measured with a draft English as short as possible, 23 strings are too wide; all can be shortened to fit (§6.9). Along the way two places were found where the Chinese itself already overflows.
- Mac side: the protocol carries almost no UI text. The only exception is "会议 42m" built by cpad, which should be written by the device itself instead. Labels in notes on the Mac stay Chinese by default.

**Cost** [calc]: flash about +13 KB, static RAM about +27 B (Mac name 25 B, language and "pending write" 2 B), heap 0, 1 NVS key. Effort about 9–11 working days (§8, §10).

**👤 6 decisions for you** (§13) all have defaults; with no reply the defaults apply. The biggest ones: rename Opt+M to "Settings" and open the Settings tab first (item 1); the English word list (item 3); what you want WiFi for (item 5).

## 1. Where your requirements land

| Requirement | Where it lands | This round |
|---|---|---|
| A system settings page | Opt+M renamed "Settings", first tab is Settings (§4) | Do |
| See WiFi and connection options | The WiFi row and WiFi explanation page (W0) on the Settings tab; the "Link" and "Sync" rows (§4.2, §4.4) | Do: show the connection and the WiFi situation, and tell you how to use the factory firmware's WiFi. Scanning (W1) and connecting (W2) are not scheduled, waiting on 👤 item 5 |
| Switch the whole system to English | String table + language setting (§6) | Do: all UI text on the device. The `cpa` command line is already English; labels in notes on the Mac see 👤 item 4 |

## 2. Starting point

### 2.1 Today's System page

- Opt+M opens the `system` module: read-only, at most 9 rows, 12 px per row, exactly filling the 111 px content area (y=12 to 123) [src system_logic.h:19 `kSystemLines = 9`; canvas.h:113-116].
- Contents are 8 fixed rows (firmware, dev, boot, storage, outbox, heap, stack free, battery) + a recording row + a warning row; when the page is full the recording row yields to the warning row [src system_logic.cpp:132-153]. `add()` **silently drops** anything past 9 rows.
- Row 0 draws the title "系统" (System) in the label position, with the firmware version as value. The value column starts at `kValueX = 46`, set by the widest label "outbox" [src system_module.cpp:17, 57-62].
- `onKey` always returns false; `` ` `` goes back to the previous page [src system_module.cpp:38].
- docs/18 §5.6 plans to add a "Profiles" tab in v0.8, switched with `,` `/`; the list layer already has those two keys [src keymap.cpp].
- The home tile is "M 系统" (M System), and the row in the Tab menu's "Go" page is also called "系统" (System) [src home_logic.h:31, action_help.cpp:23].

### 2.2 UI text

- Firmware (`lib`, `modules`, `src`, comments removed, `\x` escapes restored, generated `lib/theme/gen/` skipped): **373 Chinese literals in 35 files, 357 after per-file de-duplication, 299 after global de-duplication**, about 5.3 KB of UTF-8 [calc `i18n/inventory.py`].
- About 120 on-screen ASCII literals (key names, units, formats, commands) are shared by both languages and do not go into the string table.
- Outside the firmware: tests have Chinese in 46 files / 641 lines, simulator scripts 123 lines, HIL tools 52 lines, cpad 27 lines; the `cpa` command line has no Chinese. Details in §6.1.

### 2.3 Today's WiFi, connection and budget

- The firmware has no WiFi or BLE code at all, USB only [src docs/18 §6].
- Toolchain Arduino 2.0.17 / IDF 4.4.7, already EOL (Espressif AR2024-008). ADR 0001: re-evaluate before adding radio; target is option C (Arduino ≥3.3.9, based on IDF 5.5), provided #18621 (recordings all zero) is fixed [src adr/0001-toolchain.md:77].
- Budget [src docs/releases/v0.4.0.md:83-88; docs/19 §10; integrate/v041 numbers given by this round's task]:
  - Free heap: v0.4.0 measured 182,944 B (178.7 KiB), largest block 163,828 B (160.0 KiB); after the theme an estimated ~0.8 KiB less;
  - Floor (G-A6, in KiB [src test_runtime_budget/test_main.cpp:377]): free ≥150 KiB / largest block ≥64 KiB; while recording ≥96 / ≥32 KiB; while transferring recordings ≥128 KiB [src docs/12:455];
  - **The whole app's heap while recording and while transferring recordings has not been measured on the device**: only the slimmed `test_rec_budget` and the whole app idle were measured; the test-build counters are in place, waiting for the device (docs/07 §6.7 item 6, still open) [src docs/07:624-628];
  - Static RAM about 66.7 kB / 98,304 B; app image about 1.03 / 2.5 MB, with 1.75 MiB as the warning line (must fit alongside Bruce under the Launcher) [src scripts/check_budget.py].
- Connection: the Mac sends `time.set` every time USB connects [src docs/05 §5.10]; the clock is only an in-memory offset `state().time.offset_ms`, lost on restart until the next `time.set` [src runtime_rec.cpp:140]. `hello` has `host_label` (the Mac's name, at most 24 bytes) [src messages.h:65], but the device **does not store** it after decoding, only `host_app` [src runtime_link.cpp:51]; the "Control" page shows "Mac 在线 · cpad/x" (Mac online · cpad/x) [src menu_model.cpp `mac_text`].
- Launcher [src docs/gates/launcher.md]:
  - Power-on reset (power switch, RST key) always goes to the Launcher; deep-sleep wake does too, but in the beta, `launcher/DDLB=1` returns straight to the firmware (line 78); `esp_restart()`, panic and USB reset go straight back to the current firmware (section 3).
  - R8 says "no deep sleep; always restart with `esp_restart()`"; a "back to Launcher" item on the System page is only optional and must first pass G-L5 (lines 98, 166, 231, 260).
  - The factory firmware has WiFi and can be switched to from the Launcher (#12); it prints WiFi passwords in plain text to serial [src docs/01:178].
  - There is only one NVS, shared by all firmwares under the Launcher (the Launcher's own `l_wifi`, Bruce, etc.); core 2.0.17 **erases the whole NVS** at boot if NVS has no free page (`NO_FREE_PAGES`) (R4, line 162).
- Local only, no cloud (ADR 0004). 05 §8 decided "never do: flash encryption or Secure Boot on by default".

## 3. Principles

1. **Convenience and stability first** (docs/20): switching language takes three presses and takes effect at once; Settings must not add any risk to recording, storage or the USB link.
2. **Always findable**: the language row reads "语言 Language" in both languages; the cursor does not start on a row that changes something; after opening Settings you can switch back in at most three presses.
3. **WiFi always yields**: no scanning or connecting while recording or transferring recordings; WiFi is only on when idle. When you press record, the mic opens first, then WiFi closes; recording is never refused because of WiFi.
4. **What can be done on the Mac is not typed on the device** (docs/17 "select, don't type"): passwords are typed on the Mac; the device only does selection.
5. **Secrets travel one path**: a password appears exactly once, on the path `cpa` → cpad → USB → device NVS; never in logs, never echoed back, never in notes, never in the outbox.
6. **UI text and data are separate**: only the device's own text is translated; project names, session names, recording titles, the Mac's name and network names from the Mac are shown as-is.
7. **Missing translations, mismatched placeholders and overflow are caught at compile time**, not when you see □ or "…" on the device.
8. **No new keys**: Opt+M, `;` `.`, `,` `/`, Enter, `` ` `` and Tab already exist.
9. **The device never restarts itself, and never enters the Launcher for you**: an automatic restart loses lock state and the clock (§5.4); the Launcher is entered only with the power switch (launcher.md R8).

## 4. The Settings app

### 4.1 Information structure

```
Opt+M  Settings
├─ (Settings)                            ← opens on this tab (👤 item 1)
│   ├─ WiFi            None · USB only    ← cursor always starts here; Enter: WiFi explanation page (W0)
│   ├─ 语言 Language   中文 / English     Enter: switch directly
│   ├─ Link            USB · Mac name · online    view only
│   └─ Sync            3 min ago · to sync ↑0 R2  view only
├─ (Device)                              ← today's diagnostics, press /
└─ (Profiles)                            ← v0.8, docs/18 §5.6
```

- The module id stays `system`: HIL finds pages by id (`debug.state.mod`), so it does not change [src tools/hil].
- The name becomes "Settings" (设置) everywhere: home tile "M Settings", the row in the Tab menu's "Go" page "Settings", the first tab "Settings". docs/20 U22 "one name per action".
- Only things that can be changed on the device, and are useful to change, go here. Other settings stay out (§12).

### 4.2 The Settings tab

The mockup parts all exist in docs/19:

| Mark | What it is | How it is drawn |
|---|---|---|
| `(设置)` | Tab pill | Same as `Sessions(N)` `队列(N)` on the project page: current tab Accent, other tab Neutral, height 12, gap 4 [src board_module.cpp:274-281] |
| `▸` row | Selected row | A 14 px Select pill, indented 8 left and right (`kListX`), same as list rows |
| Label / value | Row | Label Ink3, value Ink; fact rows have no selected state |
| `───` | Divider | 1 px `Line`, same as the System page |
| `[Enter]` | Keycap | Hint-bar keycap style (docs/19 §4.2) |

Chinese:

```
[头] ↑0                         周三 09:41 94%
(设置) (设备)
▸WiFi              无 · 只用 USB 线
 语言 Language     中文
 ─────────────────────────────────────
 连接   USB 线 · 工作 MBP           在线
 同步   3 分钟前 · 待传 ↑0 录2

[;.]选 [Enter]改 [`]返回 [Tab]菜单
```

English:

```
[头] ↑0                          Wed 09:41 94%
(Settings) (Device)
▸WiFi              None · USB only
 语言 Language     English
 ─────────────────────────────────────
 Link   USB · 工作 MBP              online
 Sync   3 min ago · to sync ↑0 R2

[;.]pick [Enter]change [`]back [Tab]menu
```

| Element | Position | Notes |
|---|---|---|
| Tab pills | **y=12**, from x=4 | All three tabs (Settings, Device, later Profiles) use this y. No separate title: the current pill is the title |
| Changeable rows | y = 26 + 14k, label x=8, value x=100 | "语言 Language" 78 px; widest value "无 · 只用 USB 线" 96 px, reaching x=196 [calc `i18n/width.py`] |
| Divider | y=55 | |
| Fact rows | y = 58 + 12k, label x=8, value x=44, right edge x=236 | See "Link" and "Sync" below |
| Hint bar | y=123 | Chinese 196 px, English 232 px, both within the first spacing tier [calc `i18n/hints.py`] |

- The **Link** row has three parts:
  - a fixed left part "USB 线 · " (54 px) / "USB · " (36 px);
  - the status word pinned on the right, "在线 / 离线" (24 px) / "online / offline" (36 / 42 px), right-aligned to x=236;
  - the Mac's name in the middle, fitted with `draw_fit` into the space left: 108 px in both languages (9 Han characters or 18 ASCII), truncated with "…" if it does not fit. `host_label` is at most 24 bytes (144 px), so truncation must be handled [calc].
  - The name is the `host_label` from the last handshake this boot; if none, `host_app` (`cpad/0.4.1`); if neither, `--`. After unplugging it still shows the last Mac's name, and the status word changes to "offline".
  - "Last 3 min ago" is not on this row; the "Sync" row below already has it.
- The **Sync** row: time since the last message from the Mac, plus the same counters as the status bar (↑N waiting, RN recordings waiting to sync). Widest English "59 min ago · to sync ↑99 R99" is 168 px, reaching x=212 [calc].
- The device must store the Mac's name: add a `HostLabel host_label` to `SysInfo` (25 B, static); `runtime_link.cpp` stores it together with `host_app` at handshake.
- The cursor only stops on **changeable rows** (WiFi, language); fact rows are skipped.
- **Every time the Settings tab opens, the cursor goes back to the WiFi row**: Enter on the WiFi row only shows the explanation and changes nothing. This is docs/18 §5.9 "pre-place the cursor on the safest row", the same class of stray-press fix as docs/20 U5 and U18. After switching language the cursor stays on the language row (so "press Enter again" in the hint matches); leaving and coming back puts it on the WiFi row again.
- Future changeable items go below language and above the divider.

### 4.3 Language

- **Enter on the language row switches directly between 中文 and English** (👤 item 2). With only two languages, a pop-up chooser would cost two more presses.
- After switching, the whole screen redraws at once, then a toast in **both scripts** tells you how to switch back:
  - switching to English: "English · Enter again: 中文" (162 px);
  - switching back to Chinese: "中文 · 再按 Enter 换回 English" (180 px) [calc].
  - Both are `Txt` format strings `"%s · Enter again: %s"` / `"%s · 再按 Enter 换回 %s"`, with the language names filled from `kLangNames`, so the English half contains no Han characters (§6.3 item 2).
- Language names are always written in their own script: "中文", "English". `kLangNames` is a plain pair of `const char*`, not a `Txt`.
- The label of the "语言 Language" row is the **two halves of** `Txt{"语言", "Language"}` drawn together (`both()`), not one mixed string.
- Saved to NVS (§6.8). During a recording NVS is not written; the write waits until the recording ends; the language still changes at once [asm].
- NVS write fails: toast "没存上：重启后会回到中文" / "Not saved: 中文 after a restart" (144 / 186 px, the language name again filled from `kLangNames`). The new language is still used for the rest of this boot.

### 4.4 The WiFi page (W0, this version)

Explanation only; nothing to press:

```
[头] ↑0                         周三 09:41 94%
WiFi                                     (无)
现在   这个固件不带 WiFi
数据   只用 USB 线连你的 Mac，不上网
想用   出厂固件有 WiFi：
       关开电源进 Launcher 切过去
注意   它会把 WiFi 密码明文打到串口
以后   还没排期
[`]返回 [Tab]菜单
```

```
WiFi                                    (None)
Now    No WiFi in this firmware
Data   USB to your Mac, never online
Want   Factory firmware has WiFi:
       power-cycle into the Launcher
Note   Prints WiFi passwords to serial
Later  not planned yet
[`]back [Tab]menu
```

- Title `WiFi` in fake bold, with a Neutral pill on the right showing the current state ("无" / "None"). Value column: Chinese x=36, English x=42 (widest English label "Later" is 30 px); widest English line "Prints WiFi passwords to serial" is 186 px, reaching x=228 [calc].
- If not started through the Launcher (`SysInfo::launcher` is false, a fully flashed device): the second "Want" line becomes "先装 Launcher（见 README）" / "Needs the Launcher (README)" (156 / 162 px).
- The "Note" line is a warning for you: the factory firmware's `hal.cpp` prints the WiFi SSID and password to the serial log [src docs/01:178]. The README section "Using the WiFi tools" has more detail.
- **Why there is no one-press "Go to Launcher…"** (review item 2):
  - launcher.md R8 says no deep sleep and always restart with `esp_restart()`; a "back to Launcher" on the System page must first pass G-L5.
  - In the beta, `launcher/DDLB=1` makes a deep-sleep wake go straight back to our firmware, which looks like a restart for no reason.
  - It only helps if the factory firmware is already installed in the Launcher.
  - Flipping the power switch is one action, simpler than 5 presses in a menu.
  - It sends you to a firmware that prints WiFi passwords in plain text; the explanation page must say so, and a one-press jump would hide it.
- The outbox and recordings are on the SD card (or cpafs); after using the factory firmware and switching back, sync continues as usual with nothing lost [src docs/09 §3].

### 4.5 The Device tab

The content is today's diagnostics. The tab pills take the top 12 px, leaving only **8 rows** (from y=24, 12 px each, 1 px around the divider: 3 × 12 + 3 + 5 × 12 = 99 px, reaching exactly y=123). Today there are 8 fixed rows + a recording row + a warning row, so some must be merged, and **the warning row must always have room**:

| Row | Chinese | English | Value width (zh / en) |
|---|---|---|---|
| 1 | 固件 cpa-fw/0.4.1 rst poweron | FW … | 144 / 144 |
| 2 | dev a1b2c3d4e5f6 boot ABCDEFG-HJKMN | dev … | 186 / 186 |
| 3 | 启动 Launcher daemon cpad/0.4.1 | Boot Launcher daemon … | 156 / 156 |
| — | divider | | |
| 4 | 存储 SD 476G 剩 412G | Store SD 476G free 412G | 90 / 102 |
| 5 | outbox 待发 0 在途 0 坏 0 | outbox pending 0 out 0 bad 0 | ≤150 / ≤168 |
| 6 | 堆 180K 最小 170K 块 160K | Heap 180K min 170K blk 160K | 132 / 132 |
| 7 | 电池 4012mV 栈余 3.2K | Bat 4012mV stack 3.2K | 96 / 102 |
| 8 | warning row (drawn only when present) | | one full sentence, `draw_fit` |

- **The recording row merges into the storage row**: "录N 待同步" (RN to sync) is already on the Settings tab's "Sync" row and on the status bar; the storage row keeps only card size and free space.
- **If a migration happened this boot** (migrated / migration-failed is not 0), the storage row's "剩 412G" (free 412G) is replaced by the migration result: "SD 476G 已迁移 3 迁移失败 1" (162 px) / "SD 476G moved 3 failed 1" (144 px); worst case internal storage "内部 1.5M 已迁移 12 迁移失败 12" 186 px / "Flash 1.5M moved 12 failed 12" 174 px [calc]. A migration failure still turns the row Warn color.
- **Stack free merges into the battery row**; "幽灵 N 重置 N" (ghost N reset N, the two keyboard counters) no longer gets its own row: when not 0, it is appended as the last segment of the warning row ("键盘 幽灵 3 重置 1", 108 px). They are already what turns that row Warn color today.
- Row 0 "系统 <firmware version>" goes back to a plain "固件 <firmware version>" (FW): the current tab pill is the title.
- **English labels are all at most 36 px** (FW, dev, Boot, Store, outbox, Heap, Bat), and the value column stays at x=46, the same in both languages. Row 2's value is 186 px and at x=46 reaches exactly 232; with "Firmware" (48 px) as the English label, the value column would move to x=58 and this row would overflow.
- The value column runs from x=46 to 236, 190 px; every row in the table fits [calc `i18n/width.py`].
- Add a case to `test_mod_system`: with all warning sources on, recording available and migration counts non-zero, the Device tab has at most 8 rows and **the last row is the warning row** (§9.2).

### 4.6 Keys

| Key | Settings tab | Device tab | WiFi page |
|---|---|---|---|
| `;` `.` (or Fn+; Fn+.) | Move between the WiFi and language rows | — | — |
| Enter | WiFi row: open the WiFi page; language row: switch | — | — |
| `,` `/` (or Fn+, Fn+/) | Switch tab | Switch tab | — |
| `` ` `` / Backspace | Back to previous page | Back to previous page | Back to Settings tab |
| Tab | Menu | Menu | Menu |

- The system module starts handling keys (today `onKey` always returns false).
- The hint bar shows the back key (docs/20 B5 added the same to the board overview): Settings tab "[;.]选 [Enter]改 [`]返回 [Tab]菜单" (English "[;.]pick [Enter]change [`]back [Tab]menu"), Device tab "[,/]换页 [`]返回 [Tab]菜单" (151 px), WiFi page "[`]返回 [Tab]菜单" (100 px). `,` `/` are not in the Settings hint bar: the tab pills are on screen, and the Tab menu lists them too.
- `help()` returns per tab the rows for the Tab menu's "Here" page: up, down, change this row, switch tab, back, home. The Enter row replays Enter, so the language can also be switched from the menu.
- Changing language goes through the shell: `ModuleContext` gets a `setLanguage(Lang)` with a default implementation (returns false by default); modules never touch NVS directly [asm].

**How many presses**:

| Task | Keys | Presses |
|---|---|---|
| See connection, WiFi status, current language | Opt+M | 1 |
| See the WiFi explanation | Opt+M, Enter | 2 |
| Switch language (whichever is current) | Opt+M, `.`, Enter | 3 |
| See diagnostics (1 press today) | Opt+M, `/` | 2 |

## 5. WiFi

### 5.1 What it does and does not do in this product

| Use | Decision | Reason |
|---|---|---|
| See connection status | **Do in this version (W0), without radio** | Your "connection" today is the USB cable and the Mac. The Settings tab shows which Mac, whether it is online, the last sync, and how many items are waiting |
| Use the WiFi tools in the factory firmware | **This version explains how**: power-cycle into the Launcher | Works today; the device does not jump for you (§4.4) |
| Recognise places (home / office) to suggest docs/18 profiles | **Not scheduled. First evaluate the Mac-side signal**, then decide on W1 | See "Recognising places: look at the Mac first" below |
| See which networks are nearby | Together with W1 | Not useful on its own |
| Time sync (NTP) | Not done | Every USB plug-in the Mac sends `time.set`; `wall_ms` may be empty; NTP needs the public internet, against "local only". If ever done, only LAN NTP from DHCP |
| Sync to the Mac without a cable | Not done unless you ask (👤 item 5) | Against "USB first, Mac at the centre"; needs pairing and encryption (Noise in 05 §8); WiFi must be off while recording and transferring recordings anyway (principle 3); weeks of work |
| Internet, cloud sync, OTA updates | Never | ADR 0004; updates go over USB or the Launcher |

**Recognising places: look at the Mac first** (review item 13):

- docs/18 setup A (one Mac at home, one at the office): which Mac you plug into already decides the profile; no WiFi needed.
- Setup B (one laptop carried around): the Mac itself knows which network it is on. The default gateway's MAC address needs no permission; CoreWLAN can get the SSID but needs location permission from macOS 15 on [src Jamf community, Apple developer forums]. docs/18 §7.3 already lists "the Mac's WiFi" as a signal source for P8-B1. **0 B on the device.**
- Device-side scanning has only one use left: **when unplugged**, remind you "looks like you are at home, switch to Home?" (docs/18 T2 "forgot to switch"). Whether that reminder is worth 31 KiB of memory and a toolchain migration is for you to say in 👤 item 5.

Also: the device is 2.4 GHz only; W2 supports neither enterprise networks (WPA2-Enterprise) like eduroam, common at schools and offices, nor networks with a web login. W1 only listens, so it can still "recognise" these networks.

### 5.2 Three levels

| Level | Content | When | Needs password |
|---|---|---|---|
| **W0** | WiFi row, WiFi explanation page | This version (phase A) | No |
| **W1** | Nearby networks: passive scan on demand, fully shut down after use. This is docs/18's P8-W | **Not scheduled**: first the Mac-side evaluation (W-M); after you agree, pass G-WIFI first (§5.7). Not tied to v0.8 (docs/18 §6, §15 item 8: no WiFi in v0.8) | No |
| **W2** | Connect to a network | **Not scheduled**: only if 👤 item 5 picks a use that needs a connection, with a separate design that meets the invariants in §5.6 | Yes |

### 5.3 Memory: linking it in costs about 31 KiB

4 probes (`docs/gates/settings-probes/wifi`), compile only, counted the same way as `scripts/check_budget.py`; free heap at boot is computed from the shift of `_heap_start` (on S3, IRAM and DRAM share one SRAM, so when IRAM and static data grow, the heap starts later) [test xtensa nm]:

| Probe (vs. a blank Arduino sketch) | Image | Static RAM | IRAM | Free heap at boot |
|---|---|---|---|---|
| Scan only: IDF called directly, passive scan, no netif | +358,064 B | +22,772 B | +8,924 B | **−31,704 B (−31.0 KiB)** |
| STA: scan + connect + SNTP, IDF called directly | +386,944 B | +25,408 B | +8,924 B | **−34,336 B (−33.5 KiB)** |
| Same features with Arduino `WiFi.h` | +426,800 B | +25,920 B | +8,924 B | −34,848 B (−34.0 KiB) |

- Where it grows [test `mapsum.py`]: scan only, net80211 bss 9.7 KB, phy about 8.2 KB (IRAM 5.1 + data 3.1), pp 3.5 KB, lwIP 1.7 KB, wpa_supplicant 1.4 KB; with STA, lwIP grows to 4.4 KB.
- **Fixed at compile time, cannot be squeezed**: Arduino 2.0.17 uses a prebuilt SDK, and `WIFI_IRAM_OPT` is already off [src sdkconfig]. Squeezing further means building the SDK yourself, i.e. changing toolchain.

Applied to today's firmware [calc]:

| Item | Today | WiFi linked, always off (W1 / STA) | Limit |
|---|---|---|---|
| Static RAM | about 66,700 B | about 89,500 / 92,100 B | 98,304 B |
| Free heap | 178.7 KiB (182,944 B) | **147.7 / 145.1 KiB** (about 0.8 less after the theme) | **≥150 KiB: below** |
| Largest block | 160.0 KiB (163,828 B) | at most 129.0 / 126.5 KiB | ≥64 KiB |
| Image | about 1.03 MB | about 1.39 / 1.42 MB | under the 1.75 MiB warning line |

**How much more it takes when turned on: not measured** [inf]:

- The numbers available are all for other chips or toolchains: Rust esp-radio STA 47–57 KB; arduino-esp32 #8033 on ESP32 (not S3), `WiFi.mode(STA)` cost 45.6 KB; Espressif's S3 no-PSRAM configuration table, about 90 KB between the "Default" and "Minimum" tiers.
- Guessing from the minimal configuration, another 35–50 KiB when on: during a W1 scan free heap about 98–113 KiB; with W2 connected, plus resident lwIP (below), about 83–104 KiB, **possibly below the 96 KiB recording floor**. This is where principle 3 comes from.
- All of these are guesses. G-WIFI item 4 measures with a probe on the device: linked-but-off, scanning, and after shutdown, with 50 rounds of init–scan–shutdown. Until measured, this doc sets no new floor.
- The scan results themselves are small: one `wifi_ap_record_t` is 80 B [test sizeof], freed by the driver at once in `esp_wifi_scan_get_ap_records` [src].

**How much comes back after shutdown** [src][inf]:

- `esp_wifi_stop()` + `esp_wifi_deinit()` free the driver buffers and the WiFi task [src esp_wifi.h:269-278].
- IDF 4.4 does not support `esp_netif_deinit`: once created, lwIP's tcpip task, the default event loop and the netif stay forever, estimated 6–12 KiB [src esp_netif.h:64-73; inf]. W1 creates no netif, so only W2 has this part.
- A maintainer in #8033: WiFi "cannot be unloaded"; the fix is a restart [src]. A forum post reports a 32–48 B leak per on/off cycle [src title only].
- **Fragmentation is the main risk**: while WiFi is on, other allocations may land in its holes, and after shutdown the largest block does not come back.

**What to do about the floor** (G-WIFI item 3, measure first, then decide):

1. **Save it back**: re-measure after changing toolchain (IDF 5.x lets us configure WiFi and lwIP ourselves); or save about 34 KiB elsewhere.
2. **Change the floor**: only after the whole app's heap while recording and transferring recordings has been measured (docs/07 §6.7 item 6 done, G-A6 completed) can we discuss whether a "firmware with WiFi linked" may use a different idle floor. No number now.
3. **A session floor while scanning**: before turning on, check free heap and largest block; do not turn on if not enough. The value is set after item 4 is measured.

### 5.4 When W1 turns on and off (if it is ever built)

```
       "Scan" pressed on the WiFi page (link not Ready, not recording or transferring, screen on, heap enough)
  Off (not initialised) ─────────────────────────────────────► Scanning (≤5 s)
    ▲    ▲                                                        │
    │    └── scan done / left WiFi page / screen off / locked ────┤
    │                                                             │
    └── G0/Opt+R: record first, close too; link Ready: close ─────┘
            measure heap after shutdown: not back → no more scans this boot (no restart)
```

1. **Off by default**; `esp_wifi_init` is not even called. The only cost while off is the static 31 KiB from §5.3.
2. **Turns on only when all of these hold**: you are looking at the WiFi page and pressed scan; the screen is on; **the link is not Ready** (not connected to cpad); not recording and not transferring recordings; battery at or above 17% (the line where recording auto-stops); free heap and largest block are enough (§5.3 item 3).
   - "Link not Ready" alone rules out three cases: a `file.*` pull arriving mid-scan, the scan and USB traffic competing for the PHY, and the heap fragmenting during a transfer; and W1 is meant for when you are unplugged anyway. With a Mac plugged in the page says "没插 Mac 时才扫描" / "Unplug to scan" (102 / 84 px).
3. **At most 5 seconds per scan**; after the scan, `esp_wifi_stop` + `esp_wifi_deinit` at once. Passive scan is 120 ms per channel, about 1.6 s for 13 channels; the 360 ms setting is about 4.7 s (beacon interval 102.4 ms, occasional misses) [src esp_wifi.h:413-418; calc].
4. **Recording first, and no slower start**: holding G0 or Opt+R during a scan **starts recording first**, and at the same time stops the scan and shuts WiFi down. The ring buffer is allocated at boot (`CPA_REC_ALLOC_AT_START=0`) and can hold 768 ms; the target for shutting WiFi down is ≤200 ms, well within that [src audio_task.h:38, audio_io.h:32-36; todo G-WIFI item 7]. Recording is never refused because of WiFi, and the start of a memo loses no more than today (docs/20 U8, D8).
5. **The link becomes Ready** (Mac plugged in, cpad connected): shut down at once.
6. **Measure once after shutdown**: only if free heap and largest block are back within ±1 KiB of before is it clean. **If not back, still no restart**: no more scans this boot; the WiFi row says "关了，重启后再用" / "Off until restart" (96 / 102 px) until the next natural boot (you power off, the battery runs out). `debug.state.wifi` records it.
   - Why no automatic restart: lock state defaults to "unlocked" at boot [src lock.h:27, app_state.h:115], so a device that auto-locked in your pocket would wake with the screen on and unlocked, undoing docs/20 U2/D2; the clock is only an in-memory offset, so after a restart, until the next Mac plug-in, notes and recordings have no `wall_ms`, and W1 is meant exactly for when you are unplugged.
7. **W1 creates no netif and starts no lwIP**: once started they cannot be unloaded.
8. **No Arduino `WiFi.h`** [src WiFiGeneric.cpp:658-707, 763]: it always creates an extra AP netif and an `arduino_events` task with a 4 KB stack; by default `persistent=true`, which writes the SSID and password in plain text into `nvs.net80211` in the **shared NVS**; the default is 32 dynamic RX buffers, up to about 51 KB during network bursts. Call IDF directly, with a minimal `wifi_init_config_t`: static RX 4, dynamic RX 8, dynamic TX 8, AMPDU off (required when static RX is under 6 [src IDF 4.4.7 S3 WiFi guide]) and CSI off, `nvs_enable=0`, then `esp_wifi_set_storage(WIFI_STORAGE_RAM)`.

**Living with everything else** [src][todo]:

- CPU: the WiFi task is pinned to core 0 at priority 23; our `rec_task` is also on core 0 at priority 1; the 6×2048-sample ring buffer only holds 768 ms [src sdkconfig, audio_task.h:43-45, audio_io.h:32-36]. Hence items 2 and 4: no scanning while recording, and pressing record records first, then shuts down.
- Noise floor: WiFi transmit bursts pull on the supply, and the analog mic front end may pick it up [src atomic14, WLED]. A passive scan does not transmit, and there is no scanning while recording; only W2 needs measuring (G-WIFI item 9).
- SD: the card is on its own SPI bus, not shared with the screen [src sd_card.cpp:43-53]; it only competes for internal RAM and CPU.
- USB: the first init writes PHY calibration to NVS (which stalls the flash cache). `CONFIG_ESP_PHY_ENABLE_USB=y` keeps USB-Serial-JTAG usable during PHY init [src sdkconfig; RIOT's fix on C3]. Since scanning only happens when the link is not Ready, the gate only has to confirm "after a scan, plugging in the Mac brings the link up as usual" and "scanning while plugged in but with cpad not connected does not drop USB".
- Power: M5 lists 120.2 mA normal, 132.3 mA with WiFi, battery 1500 mAh [src M5 product page]. One scan is 2–5 seconds; negligible.

### 5.5 The W1 page (if it is ever built)

```
[头]                            周三 09:41 94%
WiFi · 附近                      (只听，不连)
▸重新扫描
 HomeNet              强
 eduroam              中
 Cafe-Guest           弱
 3 个网络 · 12 秒前
[Enter]扫描 [`]返回 [Tab]菜单
```

- While scanning the first row says "扫描中… 约 5 秒" (Scanning… about 5 s, Accent), and Mando (46 px) uses the Sniff pose from docs/19 with the sign "嗅一嗅…" (Sniff…). It only animates while the backlight is On.
- Signal is written as "强 / 中 / 弱" (good / fair / weak), no bars.
- The list only lives in this page's memory and is lost on leaving; not written to storage, not sent to the Mac.
- W1 itself does nothing on a row; P8-W later adds "remember this as Home's place" here.
- Width: widest Chinese row 186 px, English "Nearby · listen only" 120 px [calc].

### 5.6 W2: not scheduled; invariants first

docs/18's review already decided "the WiFi part is too detailed, compress it into one paragraph" (docs/18:790). For W2 this section only writes **rules that no future design may break**; if it is ever built, it gets its own design and its own review:

1. **A use first** (👤 item 5), scheduled after docs/18's v0.8 profiles; G-WIFI items 4, 7 and 9 must be re-run and pass with **STA connected and lwIP resident** before it can be scheduled.
2. **No connecting or scanning while recording or transferring recordings**; WiFi only on when idle. Pressing G0 or Opt+R while connected: start recording first, then disconnect and shut WiFi down.
3. **The device never restarts itself**. Once lwIP is up it cannot be unloaded, and getting memory back after use requires a restart: every restart is confirmed by you, with power plugged in and the screen on; before restarting, lock state and the clock offset are written to RTC_NOINIT memory so the device wakes still locked with the right time. A "test connection" is also an action you watch and confirm, and ends with a confirmed restart in the same way.
4. **Passwords are typed on the Mac**: `cpa wifi add`, no echo (`getpass`); no `--psk` argument (it would land in `ps` and shell history), `--psk-stdin` for scripts; no "read from Keychain" (that opens another path needing admin authorisation to read the system Keychain, only to save typing a password once). Sent to the device over the USB cable; no password typing on the device (that would need a separate password editor that never writes to storage).
5. **`psk` appears only in one message, `wifi.set` sent by the host**: no device reply, no `debug.state`, no `error` carries it; `error.msg` does not echo it. `check_vectors.py` guards this. cpad only sends it when the link is Ready and the peer's `hello` is `cpa-fw/`; no queuing, no resend after disconnect (otherwise the Launcher boot screen's serial console might take it as a command [src launcher.md item 8]).
6. **Only to the right Mac**: `wifi.*` only answers the Mac bound to the current profile; with profile mode off, no list of saved or nearby networks is sent. Nearby networks are sent only while the device is on the "waiting for the Mac to enter a password" card; `wifi.set` is only accepted while that card is open, with consent given on the device. Network names reveal where home and office are, so they must not go to a Mac of another profile, and no arbitrary Mac may push a network in. When the time comes, add a threat row next to T8 in docs/18 §8.3.
7. **Logs**: cpad adds `SECRET_FIELDS = {"psk"}`, always written as `<secret>`, with no length and no hash, even with `CPA_LOG_CONTENT=1`. Today's `redact_fields` only looks at top-level keys [src logging/redact.py], and cpad logs exception text `str(exc)` [src app.py:370, 392], so a validation error like "psk too short: …" would bypass key-name filtering: **WiFi code paths do not log `str(exc)`**, and the bad-password path must have a test. `events.jsonl` does not record network names.
8. **Device memory**: after decoding, zero the Runtime's receive block `rx_`, the framer's `framer_.buf` and the decoded struct [src runtime.cpp:111, 122; line_framer.h:18]. What cannot be zeroed goes into SECURITY.md: HWCDC's 8 KB receive ring buffer (managed by Arduino), the copy left on the stack when `framer_scan` returns by value, Python strings in cpad. The send queue `LineQueue` only holds lines the device sends, so `psk` never enters it [src line_queue.h:1].
9. **Storage**: its own NVS namespace, at most 4 networks; driver `WIFI_STORAGE_RAM`, never writes `nvs.net80211`; no `WiFi.h` (grep in `firmware/app` must find none); no writes while recording.
10. **Risk rating**: a plaintext WiFi password is a **network credential that can be used elsewhere**, more serious than plaintext recordings on the SD card: anyone with the device can read it with esptool; other firmwares installed under the Launcher read the same NVS, and Bruce is itself a WiFi attack tool [src launcher.md:50, 114]. NVS encryption on IDF 4.4 requires flash encryption first, which 05 §8 decided not to enable; IDF 5.x's HMAC scheme waits for the toolchain change. Also, core 2.0.17 erases the whole NVS at boot when NVS has no free page (R4), so saved networks would vanish together with the Launcher's settings.
11. **Not supported**: open networks, enterprise networks (eduroam), web login, 5 GHz.

### 5.7 Gate G-WIFI (W1 must pass items 1–8; W2 also items 9 and 10)

docs/18 §6 only gave directions (toolchain, heap, no transmit, no USB impact). Here they become testable items:

| # | Item | Pass |
|---|---|---|
| 1 | Toolchain | First evaluate ADR 0001 option C (Arduino ≥3.3.9 / IDF 5.5): the HWCDC stuck-reply fix and the #18621 all-zero recording fix are in, and the full HIL re-run passes. Doing W1 without changing toolchain needs its own written justification |
| 2 | Build | Static RAM ≤98,304 B; image ≤1.75 MiB |
| 3 | Floor | First finish docs/07 §6.7 item 6 (whole app heap while recording and transferring, on the device); then pick one of the three paths in §5.3 and write it into docs/12 G-A6 |
| 4 | Heap (device probe) | Add an on-device environment in `docs/gates/settings-probes/wifi`: free heap and largest block in three states, linked-but-off, scanning, after shutdown; back within ±1 KiB after 50 rounds of init–scan–shutdown; the scanning numbers become the session floor of §5.3 item 3 |
| 5 | No transmit | No probe requests from us on air during a passive scan (captured with another device) [todo] |
| 6 | USB | After a scan, plugging in the Mac brings the link to Ready as usual; scanning while plugged in with cpad not connected does not drop USB, and cpad connects as usual afterwards |
| 7 | Yield | Holding G0 during a scan: recording starts first, WiFi is fully off within ≤200 ms; `ring_high` does not rise, `mic_missing == 0`; **start latency compared with no scan**: as in UX-H4, say "one two three four" right on the press, and the transcript must contain "one" (docs/20 §5.5 UX-H4) |
| 8 | Power | Extra drain per scan (estimated <1 mAh) [todo] |
| 9 | Noise floor (W2) | Record a clip while connected to WiFi and compare the noise floor with not connected (👤 listen to a clip) |
| 10 | W2 precondition | Items 4, 7 and 9 re-run and pass with STA connected and lwIP resident |

## 6. Language

### 6.1 Inventory

Per file ("strings" is the count after per-file de-duplication; "role" is what the string does on screen) [calc `i18n/inventory.py`, `measure.py`]:

| File | Literals | Strings | Main roles |
|---|---|---|---|
| `modules/board/board_module.cpp` | 43 | 35 | Menu descriptions, short in-cell words, whole lines |
| `lib/help/action_help.cpp` | 33 | 32 | Menu descriptions |
| `modules/system/system_logic.cpp` | 28 | 28 | Concatenated fragments, System page rows |
| `lib/help/menu_model.cpp` | 21 | 21 | Menu descriptions, concatenated fragments |
| `lib/shell/lock_screen.cpp` | 19 | 19 | Weekdays, concatenated fragments |
| `lib/shell/rec_result.cpp` | 18 | 18 | Toasts |
| `lib/shell/rec_overlay.cpp` | 17 | 16 | Toasts, whole lines, signs |
| `lib/shell/runtime_outbox.cpp` | 17 | 17 | Concatenated fragments |
| `lib/shell/shell_menu.cpp` | 17 | 15 | Titles, menu descriptions |
| `lib/shell/confirm.cpp` | 15 | 15 | Concatenated fragments, whole lines, toasts |
| The other 25 files | 145 | 141 | Editor, shell, home, inbox, status bar, Mando's signs, key names, ages… |
| **Total** | **373** | **357** | 299 after global de-duplication |

By role (357 strings): concatenated fragments 78 · menu descriptions 62 · toasts 54 · short in-cell words 44 · whole lines 36 · titles 19 · weekdays 14 · hint bars 12 · Mando's signs 9 · home tiles 9 · keycap rows 8 · status bar 6 · System page rows 6.

Outside the firmware:

| Place | Lines with Chinese | Handling |
|---|---|---|
| Firmware tests `firmware/app/test` | 46 files, 641 lines | Unchanged: tests default to Chinese; English added separately (§9) |
| Simulator scripts, HIL tools | 123 lines, 52 lines | Unchanged; add an English tour script |
| cpad `daemon/src` | 6 files, 27 lines | §6.11 |
| `cpa` command line | 0 | Already English |
| docs/16 and the App SDK (`feat/app-sdk`, not merged yet) | Examples 31 lines, templates 8 lines; `app_rules.py` requires hint bars to end with "Tab菜单" | §6.10 |

### 6.2 String table: three options

| Option | Pros | Cons |
|---|---|---|
| A. A pair at the call site: `tr("队列为空", "Queue is empty")` | Smallest change | No compile-time checks (C++17 has no consteval); the same word scattered around |
| B. One big X-macro table plus an enum | Tests can iterate it | 300 ids; the call site does not show the text; one big file that conflicts with parallel branches every day; third-party apps cannot join a closed enum |
| **C. Named `constexpr Txt` constants kept next to the code that uses them** | Compile-time checks; the constant name is the id; split per file, few conflicts; apps can use it too | Cannot iterate all of them in C++ (covered by the §9 scripts) |

**Pick C.** Shared words (note, AI queue, new session, reply, project, waiting, back, settings…) live in `lib/i18n/words.h`; home tiles, the Tab menu and editor titles all reference the same constant, so docs/20 U22's "one name per action" (enforced today by `test_vocabulary`) becomes a structural guarantee.

### 6.3 `Txt` and compile-time checks

```cpp
// lib/i18n/lang.h
enum class Lang : uint8_t { Zh = 0, En = 1 };
Lang lang();                     // 当前语言。只在 loop 线程里读写
void set_lang(Lang l);           // 只有外壳调用：开机读完 NVS 后、设置页切换时
Lang lang_from_nvs(uint8_t b);   // 不认识的字节 → Zh

// lib/i18n/lang_names.h：唯一允许不在 Txt 里的中文
constexpr const char* kLangNames[] = {"中文", "English"};  // 按 Lang 取，各用自己的文字

// lib/i18n/txt.h
void txt_check_failed(const char* why);  // 故意不是 constexpr：编译期调用它就是编译错误
struct Txt {
  const char* zh;
  const char* en;
  constexpr Txt(const char* z, const char* e, int16_t max_w = kNoLimit) : zh(z), en(e) {
    check_pair(z, e, max_w);  // 不合格时调用 txt_check_failed
  }
};
inline const char* tr(const Txt& t) { return lang() == Lang::En ? t.en : t.zh; }
// both(t)：把两半拼成 "语言 Language"，只给"永远找得回来"的那一行用
```

`check_pair` is a constexpr function (loops are allowed since C++14) that checks six things:

1. Neither language is an empty string;
2. The English has no Han characters, kana or full-width punctuation (U+3000–U+9FFF, U+FF00–U+FFEF), catching "forgot to translate, just copied". **No exceptions**: places that must show both scripts at once (the language row, the switch toast) use `both()` at runtime or fill `%s` from `kLangNames`;
3. Both sides have the same `%` conversion sequence (`%u %s` vs `%u %s`); argument order cannot change;
4. The English has none of the common symbols missing from efontCN_12, `— – ✓ ✗ ⚠ ▶ ▸ ▼ ‹ › « » ½ ⏎ ↵ ⌫`, which would be drawn as □ [calc `i18n/efont.py`];
5. Width: both languages within `max_w`. **Computed from real glyph widths**: ASCII 6 px, Han characters and full-width punctuation 12 px, the 11 symbols `● … ★ ↑ ← → ◎ ◇ ○ · ×` 6 px; any other non-ASCII character counts as 12 px (can only over-estimate). The characters we use are all in these classes, so the result matches the real-glyph check in §9.3, and there is no need to fall back to an unchecked `Txt` to dodge false positives. Format strings with `%` are measured by native tests with the longest arguments;
6. No control characters.

No `throw`: exceptions are disabled at compile time on ESP32. Calling a non-constexpr function has the same effect [asm].

**They can only be `constexpr` constants** (namespace constants, `static constexpr` members or arrays). An ad-hoc `Txt{...}` is not evaluated at compile time, so the checks do not run; `tools/i18n/check_texts.py` blocks it in CI. Arrays are checked too: `constexpr Txt kWeekdays[7] = {{"周日", "Sun"}, …}`.

### 6.4 Width rules

The constructor's third argument is the width class, spelled with aliases:

| Class | Limit | Basis |
|---|---|---|
| `ToastTxt` | 224 px | Text width of the toast pill `kToastTextW` [src theme.h:105] |
| `LineTxt` | 232 px | A line is 240 px, 4 px each side |
| `MenuTxt` | 120 px | The menu description limit set for apps in docs/16:197 on `feat/app-sdk` (not merged yet); first-party code follows it too, so the drawer does not have to widen |
| `TileTxt` | 60 px | First-party tiles are 75 px, names at most 60 px [src theme.cpp `tile`]; 48 px app tiles are checked by `check_apps.py` |
| `SignTxt` | 48 px | Sign = text + 10 px; the meeting page centre is x=23, backed by the left bound in `theme::sign` (§6.9) |
| `KeyTxt` | 43 px | The Tab menu key column is at most 54 px, keycap = text + 4 px [src menu_view.h:27] |
| `Txt` | not checked | Concatenated fragments, measured by the tests of the function that uses them |

- Do not write pointer arrays as `static const char* kX[]`: the pointers themselves are not const, and the whole array goes into DRAM. The existing code uses `constexpr const char*` [src key_names.cpp].

How to write them:

```cpp
// modules/board/board_texts.h
namespace cpa::txt {
constexpr LineTxt kQueueEmpty{"队列为空", "Queue is empty"};
constexpr ToastTxt kOtherOnMac{"其他项目只在 Mac 上看", "Other projects: see the Mac"};
constexpr Txt kUpdatedAgo{"更新于 %s前", "Updated %s ago"};
}  // namespace cpa::txt

draw_fit(c, x, y, w, tr(txt::kQueueEmpty), st);
std::snprintf(buf, sizeof buf, tr(txt::kUpdatedAgo), age.c_str());
```

Pure functions that return `const char*` (`action_help`, `mando_sign`, `store_note_text`…) keep their signatures; only the strings inside become `tr(...)`. The theme API already takes `const char*` and does not change.

### 6.5 Plurals, time, dates, ages, key names

- **Plurals**: English only. `CountTxt{"%u 条笔记等着传到 Mac", "%u note waits for the Mac", "%u notes wait for the Mac"}`; `trn(t, n)` picks the singular when n == 1. About 6 places [calc]: the three legend rows on the Control page, the keyboard dropped-key count, home "N things waiting for you", board "N sessions without a project".
- **Numbers and units** do not change: `476G`, `1.5M`, `83%`, `4012mV`.
- **Time**: 24-hour `13:42` in both languages (the big digits only have 0–9 and the colon, 👤 item 3).
- **Date**: the lock card's "10/21 周三" becomes "Wed 10/21" in English, both 60 px wide. This is the only place that changes argument order per language (`if (lang() == Lang::En)`). No `%1$s`: newlib's support for it is unreliable [inf].
- **Weekday**: the status bar's "周三" 24 px becomes "Wed" 18 px, narrower.
- **Age** (docs/20 U22's `age_text`): English "9 s / 40 min / 2 h / 2 d", compact form "9s / 40m / 2h / 2d". Abbreviations avoid plurals. "… ago": board "Updated %s ago", Settings tab "3 min ago", "从未" → "never".
- **Key names**:

| Today | English | Keycap width | Notes |
|---|---|---|---|
| 退格 | Del | 22 px | Use the text printed on the keycap [todo check against the device] |
| 空格 | Space | 34 px | |
| 按住G0 | Hold G0 | 46 px | |
| 松开G0 | G0 up | 34 px | A "Release G0" keycap is 64 px, more than the 54 px key column, and would cover the description [calc] |
| Enter, Tab, Opt+X, Fn+X, G0, `;` `.` `,` `/` `` ` `` | unchanged | | |

### 6.6 Making hint bars structured

Today a hint bar is one string, and the theme splits it into keycaps and descriptions by rule: leading visible ASCII is a key, and adjacent pure-ASCII words merge into one key [src hint_layout.h; docs/19 §4.2 rules 2, 4]. English is all ASCII, so `Enter open` would become one keycap called "Enter open". So a structured form is added:

```cpp
struct HintKey { const char* keys; Txt words; };   // {"Enter", {"打开", "open"}}
// Module 新增，有默认实现，老模块不用改：本页提示栏上的键，不含 Tab 菜单
virtual uint8_t hintKeys(const HintKey** keys) const { *keys = nullptr; return 0; }
```

- If a module provides `hintKeys`, the shell draws from the structure and appends "Tab 菜单 / Tab menu" at the end; modules that do not keep parsing `hint()` as before, so Chinese apps are not affected.
- Of the 12 hint bars measured, English fits in all of them with one or two words (open, reply, dismiss, back, menu, move, project); the tightest are the inbox and the board's Sessions, 238 px in Chinese and 236 px in English, both in the tightest tier [calc].
- docs/16's rule "hint bars end with Tab菜单" is no longer needed for structured hints.

### 6.7 Tab menu, tiles, signs and other theme parts

- The **Tab menu** is assembled on the fly: keymap + `action_help` + the module's `help()` [src menu_model.h]. `HelpRow` gets an optional `const Txt* text`, used when present, otherwise the old `label`. The three tabs "本页 / 去哪 / 控制" → "Here / Go / Control"; the narrowest cell is 54 px, and "Control" at 42 px fits [calc].
- **Home tiles**: `AppTile` also gets `const Txt* text`. English tile names are all ≤48 px (Appendix A).
- **Mando's signs**: short English words Listen, Sniff…, You!, Saved, Huh?, Napping, Here. `theme::sign` gets a rule: the pill's left end is never left of x=2 (which also fixes the Chinese "保存中…" overflow in §6.9).
- **Status bar**: "● 在线" → "● online", "◇ 版本不兼容" → "◇ mismatch", "录N" → "RN". English is narrower than Chinese overall [calc]. docs/18's profile characters ("公" office, "家" home) will use one letter when the time comes.
- **Lock card**: the date line "Wed 10/21 · Bat 83%" 120 px (Chinese 114); the two count pills "Waiting 3" "To sync 2" together about 138 px, about 40 px wider than Chinese, fits but tight [calc]. The two "keycap and text interleaved" lines under the card get one `{is key, text}` array per language, and the order may differ.
- **Sentences with colour segments**: home "下午好，**2 件事**在等你" is split into before, highlight and after, one `Txt` each. English "Afternoon, **2** waiting for you" is 174 px in a 192 px area [calc].
- **Device tab label column**: English labels are all words of at most 36 px, and the value column stays at x=46 regardless of language (§4.5).

### 6.8 Where the language is stored and how it takes effect at once

- **Where**: NVS, reusing `nvs_boot`'s namespace `cpa`, key `lang`, one byte (0 Chinese, 1 English). Written only when the value changes [src nvs_boot.cpp; asm].
- **Pure logic**: `struct Settings { Lang lang = Lang::Zh; }` in `lib/core/settings.h`. One NVS key per setting rather than one blob, to avoid version migrations later.
- **Device side**: `lib/hal/nvs_settings.{h,cpp}` (`Preferences`, written like `nvs_boot.cpp`); `Platform` gets `loadSettings()` and `saveSettings()`; the simulator keeps it in the simulated device's memory, surviving `debug.reboot`.
- **Boot**: read after `nvs_boot_ticket()` and before the first frame is drawn (including the "migrating to SD card" screen), then call `set_lang()`. If it cannot be read or is unknown, use Chinese.
- **Switching** (Settings tab Enter → shell):
  1. `set_lang(new language)`;
  2. **Clear nothing**. Queued toasts hold formatted text [src toast.h:27, 43]; after a switch they show in the old language for at most a few more seconds (≤4.5 s) and then disappear, nothing lost. The lock card's recording result is already stored as codes and arguments (`RecLast`'s `saved`, `why`, `mode`, `ms`, app_state.h:74-81) and written with `tr()` when drawn, so after a switch it is automatically in the new language, and results you have not seen yet are not cleared (docs/20 U6, B1). `last_hint` is only used for de-duplication, never shown, and does not need clearing;
  3. Redraw the whole screen;
  4. Show the toast from §4.3 in the new language;
  5. If not recording, write NVS; if recording, mark "pending write" and write when the recording ends (writing flash stalls the cache, no need to compete with recording) [inf].
- **Why no restart is needed**: all UI text is fetched with `tr()` at draw time, and module state only stores numbers and ids. That is already true today, the only exception being the toast queue, which passes in seconds [src]. This becomes a rule in docs/16 later (after `feat/app-sdk` is merged, ST-8): modules must not keep formatted UI text until the next frame.
- **Launcher**: NVS is shared with other firmwares, and the `cpa` namespace will not collide. A full flash does not erase NVS (only `erase_flash` or `erase_region` does); if it is erased, the device goes back to Chinese [src launcher.md].

### 6.9 English is wider than Chinese: what was measured, what to do

**Method** [calc `i18n/measure.py`]: write the shortest English possible for each of the 357 per-file de-duplicated strings (`i18n/en_draft.py`), measure with the real glyphs, compare by role and limit; hint bars and keycap rows are computed with the theme's real layout (keycap = text + 4 px, at least 12 px, three spacing tiers).

**Overall**: 241 / 357 English strings are wider (68%), median width ratio ×1.20 (Device tab labels use the short words from §4.5). By role: titles ×1.40, toasts ×1.32, whole lines ×1.28, menu descriptions ×1.20, short in-cell words ×1.12, hint bars ×1.00 (with short words), weekdays ×0.75. Most extreme: "等你" → "Waiting for you", ×3.75.

**The 23 over the limit can all be shortened**:

| Role | Count | Example: Chinese → draft → shortened (px) |
|---|---|---|
| Toast (>224) | 11 | "已存到 Mac 队列（不会发给 session）" 210 → 288 → "Queued on the Mac (not sent)" 168 |
| | | "session 的项目不在设备上：去 Mac 回复" 222 → 294 → "Project not on device: reply on Mac" 210 |
| | | "要录更久：松开 G0，再按 Opt+R 开会" 204 → 264 → "Longer? Let go, then Opt+R" 156 |
| Whole line (>232) | 3 | "SD 卡无法使用：请在 Mac 上格式化为 FAT32" 240 → 252 → "SD unusable: format FAT32 on a Mac" 204 |
| Menu description (>120) | 7 | "删一个字（空时退出）" 120 → "Delete a char (exit if empty)" 174 → "Delete (empty: exit)" 120; the other 6 are 132–150 px, e.g. "Lock (keeps recording)" 132 → "Lock, keep recording" 120 |
| Sign | 2 | The meeting page's "Saving…" (same for Chinese "保存中…"): pill 52 px, centre x=23, left end off-screen → add a left bound to `theme::sign` |
| Key names, Device tab labels | — | "Release G0" → "G0 up"; "Firmware" → "FW" (§4.5) |

**Two places where the Chinese itself already overflows** (independent of language, fixed along the way in ST-5):

- System page "SD 卡无法使用：请在 Mac 上格式化为 FAT32" is 240 px but the line is only 232 px, so the end is cut to "…". Change to "SD 卡不能用：在 Mac 上格式化成 FAT32", 216 px.
- The meeting page's closing sign "保存中…": pill 52 px, centre x=23, left end at x=−3.

**Bytes**: the English table is about 5.8 KB, Chinese about 6.2 KB (including the terminating 0). For a line of the same width, English is 6 px per byte and Chinese 4 px per byte, so the existing `FixedStr`s are all big enough: the longest English toast is 49 bytes, and `ToastText` holds 96 [calc].

**Missing glyphs**: efontCN_12 lacks `— – ✓ ✗ ⚠ ▶ ▸ ▼ ‹ › « » ½ ⏎ ↵ ⌫ ⇧ ⌘ ⌥ ♪`; it has `● … ★ ↑ ← → ◎ ◇ ○ □ · × ° ± ≈ ▲ ◆ ■ ☆ “ ’ é ü` [calc]. Dashes in English are written as `:` or `-`, and quotes are ASCII.

### 6.10 Modules and third-party apps (docs/16, on `feat/app-sdk`)

| Where an app can write text | Today | After |
|---|---|---|
| `toast(level, text)`, `hint(lv, text)` | `const char*` | Unchanged, pass `tr(kX)` |
| Hint bar `hint()` | One string, ending in "Tab菜单" | New `hintKeys()`; the old form still works |
| Tab menu `HelpRow{ev, keys, label}` | `label` is `const char*` | New optional `text` |
| Home tile `AppTile{key, label, badge}` | `label` is `const char*` | New optional `text` |
| `Canvas::text`, `draw_fit` | `const char*` | Unchanged |

- All of these are additions with defaults, so **today's apps compile and work unchanged**; they just show Chinese in English mode.
- `check_apps.py` (on `feat/app-sdk`) changes with it: measures both languages; bare Chinese literals in apps give a **warning**, not an error; structured hints are no longer required to end with "Tab菜单". The `new_app.py` template generates the `Txt` form directly, and the Pomodoro example becomes bilingual. Whichever is merged first, the other adapts.

### 6.11 Mac-side follow-up

- **The protocol carries almost no UI text** [src]: on an error the device only shows the error code's name, not the `msg` from the Mac [src runtime_outbox.cpp:212-219]; `agent.result` failure reasons are codes too, translated into text by the device.
- **The only exception**: in the recording history `rec.items`, a meeting without a title is written by cpad as "会议 42m" [src recordings/history.py:87]. The `rec.items` protocol and cpad are already on the v0.4.1 development line, while the device's recordings app (Opt+V) moved to v0.4.2 per D10 and does not draw these strings yet [src CHANGELOG]. So **before the v0.4.2 device side starts drawing `rec.items`**, change it to send `title: null` (the schema already allows it), and have the device write "会议 42 分钟 / Meeting 42 min" itself from `mode` and `dur_ms`; changing it now costs the least. This way **`hello` gets no `lang`**, and cpad does not need to know the device language.
- **The `cpa` command line** is already English and stays as is.
- **Labels in notes on the Mac** ("会议：" "语音：" "[音频]" "[全文]" "[转写]" "（仅转写）" "（未转写）" "分钟" "待办") [src recordings/route.py:22-65]: by default they **do not follow the device** (👤 item 4). Notes are long-lived files, and mixing Chinese and English in one file makes them hard to search; you may also search for these words in Obsidian or via MCP. **Only if you pick English in item 4** is `notes_lang: "en"` added to `config.json` (affects only lines written from then on; the export folder's README and the MCP truncation notice `mcp/resources.py:109, 113` follow the same key).
- **Docs**: docs/ continues in Chinese. The README key table changes to "Opt+M Settings", and gains two sections, "Settings → Language" and "Using the WiFi tools".

## 7. Protocol additions (additions only, `v` stays 0)

| Phase | Change | Direction | Content | Compatibility |
|---|---|---|---|---|
| A | `rec.items[].title`: cpad no longer builds "会议 42m" | H→D | Send `null` when there is no title | The schema already allows null; add a valid case. The device recordings app has not shipped yet |
| A | `debug.state.ui.lang?` | D→H | `"zh"` / `"en"`, **test build only** | Optional; omitted = older build. HIL asserts on it |
| A | `debug.lang` | H→D | `{lang: "zh" \| "en"}` → `ack`, **test build only**; goes through the same switch path as Enter on the Settings tab (also writes NVS) | Release firmware answers `E_UNKNOWN_TYPE`; the CI and release workflow check "release firmware contains no `debug.(key\|state\|reboot\|rec\|mic\|codec)`" adds `lang` (the approach of protocol README items 16 and 17) |
| B | `debug.state.wifi?` | D→H | `{radio, scans, free_min, block_min, dirty}`, test build only | Optional; used by the G-WIFI HIL |

- The W2 messages (`wifi.get`, `wifi.state`, `wifi.set`, `wifi.forget`) are **not defined this time**; they will be written per the §5.6 invariants when scheduled.
- **Merge constraint**: new schemas and vectors must be merged in the same batch as the firmware codec change that understands them (the firmware vector test requires "decode then re-encode equals field by field"); `protocol/README.md` gets a new item (numbered by merge order).

## 8. Budget

| Item | Today (integrate/v041) | Phase A: Settings + language + W0 | W1 (later, scan only) | Limit |
|---|---|---|---|---|
| App image | about 1.03 MB | about +13 KB (English about 5.8 KB, `Txt` pairs 357 × 8 B ≈ 2.9 KB, code 1–2 KB, Settings tab and W0 about 3 KB) | +358 KB, plus about +3 KB for the page | warning 1.75 MiB; hard limit 2.5 MB |
| Static RAM | about 66,700 B | about +27 B (`SysInfo.host_label` 25 B; current language and "pending write" 1 B each) | +22,772 B → about 89,500 B | 98,304 B |
| Free heap (WiFi off) | 178.7 KiB | 0 (`tr()` only returns a pointer) | −31.0 KiB → 147.7 KiB | ≥150 KiB, see §5.3 |
| Free heap (scanning) | — | — | guessed about 98–113 KiB, **not measured** [inf] | session floor waits for G-WIFI item 4 |
| Largest block | 160.0 KiB | 0 | at most 129.0 KiB; possibly less after a scan (fragmentation) [todo] | ≥64 KiB |
| NVS | `cpa`: `epoch`, `boot` | +1 key `lang` | 0 | 20 KB partition, shared with the Launcher and others |
| AppState | 3,496 B (incl. theme +8) | 0 (language is a global byte, not in AppState) | about +4 B (radio state) | 4 KB |
| Module object | — | system module ≤1 KB | nearby list 8 × about 40 B ≈ 320 B | 1 KB |
| Run time | — | one extra comparison per string fetched | scan ≤5 s, only on the WiFi page | — |
| Effort [inf] | — | 9–11 working days | Mac-side evaluation 1–2 days; gate 3–5 days (not counting the toolchain change itself, which is 1–2 weeks) + 3–4 days | — |

W2 is not scheduled and has no budget; the STA probe in §5.3 (static +25,408 B, heap −33.5 KiB, another 6–12 KiB for resident lwIP) is its starting point.

## 9. Tests and HIL (tests always first: RED → GREEN → REFACTOR)

### 9.1 Compile time

`test/compile_fail/i18n/` holds a set of deliberately wrong `Txt`s: missing English, Han characters in the English (including mixed text like "语言 Language"), `%u` vs `%s`, too wide, containing `—`. CI confirms they **fail to compile**; a set of correct ones confirms they compile [asm].

### 9.2 Firmware native (Unity)

| Test | Assertions written first |
|---|---|
| `test_i18n` (new) | Default is Chinese; `set_lang` / `lang`; `lang_from_nvs` falls back to Chinese on an unknown byte; `tr`, `trn` (n = 0, 1, 2), `both`; the width check matches the real-glyph result of §9.3 |
| `test_settings` (new, pure logic) | Enter switches; switching **does not clear** the toast queue (a queued recording-result toast still shows); during recording only "pending write" is set, written when recording ends; the toast on write failure; `Settings` defaults |
| `test_mod_system` (changed) | Opens on the Settings tab; **the cursor is on the WiFi row on every open**; `;` `.` only move between the two changeable rows; `,` `/` switch tabs; Enter on the WiFi row opens the WiFi page, on the language row calls `setLanguage`; Link row: status word right-aligned, long names truncated, after unplugging shows the last Mac's name and "offline"; WiFi page text for Launcher and full-flash boots; **the Device tab has at most 8 rows, and with all warning sources on the last row is always the warning row**; English labels all ≤36 px; tab pills at the same y on all three tabs; every row drawn completely in both languages |
| Existing width tests run once per language (`for (Lang l : kLangs) { LangScope s(l); … }`) | `test_text_width` (confirm dialog titles, all toasts), `test_vocabulary` (one name per action holds in both languages; "设置" Settings replaces "系统" System), `test_theme_hint` (structured hints fit), `test_menu_view` (keycaps ≤47 px), `test_status_bar_states` |
| `test_wifi_logic` (new, only when W1 is scheduled) | State machine: enters scanning only on the WiFi page, backlight On, link not Ready, not recording or transferring, battery ≥17%, heap enough; on G0 or Opt+R **sends start-recording first**, then shuts WiFi down; shuts down at once when the link becomes Ready; shuts down on leaving the page, screen off, lock; if the heap does not come back after shutdown, disables scanning until the next boot, **never calls restart** |

**Existing tests do not change**: tests default to Chinese, and the 641 lines of Chinese assertions stay valid. `LangScope` switches language temporarily in a test and switches back in its destructor.

**Fuzzing**: the random keys in `test_fuzz_runtime` include Opt+M (`kOptKeys` contains `m`) [src sim/test/test_fuzz_runtime/fuzz_user.cpp:127], so it will switch language at some point. Add three invariants:

- Language is findable: after a random sequence, leave all overlays and press Opt+M; the cursor is on the WiFi row, and `.` then Enter always switches to the other language (≤3 presses after opening Settings);
- In English no frame has missing glyphs (`missing_glyphs == 0`);
- The simulator's settings survive `debug.reboot`, regardless of the number of switches.

(The device does not jump to the Launcher or restart itself, so fuzzing does not need to simulate those two things.)

### 9.3 Real font (CI, Python)

`tools/i18n/`, tidied from the scripts in `docs/gates/settings-probes/i18n`:

- `texts.py`: finds all `*Txt` constants, measures both languages with the real glyphs, compares by class and limit; outputs a TSV (Chinese, English, width, class) used to review the English;
- `check_texts.py`: forbids ad-hoc `Txt{...}`; apart from `lib/i18n/lang_names.h`, forbids new bare Chinese literals in `lib` and `modules` (warning until migration is done, then error).

### 9.4 Simulator

- `firmware/sim/scripts/settings.cpas` (new): Opt+M, screenshot, `.` Enter to English, screenshot, `debug.reboot`, check it is still English, Opt+M, Enter into the WiFi page, screenshot, `` ` ``, `.` Enter back to Chinese. `--strict` catches □.
- `firmware/sim/scripts/v04_tour_en.cpas` (new): switch to English first, then run the existing tour; check the contact sheet for truncation, overlap and alignment.
- `test_theme_sim` adds the English pass.
- When W1 is scheduled: fake radio in the simulator (`FakePlatform` returns a fixed nearby list, with controllable timing and heap changes), and screenshot the nearby networks page.

### 9.5 Protocol and daemon

- A valid case for an untitled meeting in `rec.items` (`title: null`); daemon `test_history_title_null.py`.
- Valid and invalid cases for `debug.lang` (`lang: "fr"`); `debug.state.ui.lang`; the name `debug.lang` does not appear in release firmware (CI check).
- Only if you pick English notes (👤 item 4): `test_notes_lang.py` (Chinese by default; `notes_lang: "en"` only affects new lines).

### 9.6 HIL

| Scenario | Where | Content |
|---|---|---|
| Fixture (all scenarios) | Simulator and device | At the start read and record `debug.state.ui.lang`; if not `zh`, set Chinese with `debug.lang`; **at the end restore the starting language with `debug.lang`**. No key replay, and no silent change to the preference on your device |
| `settings` (new) | Simulator and device | Opt+M → `mod == system`, cursor on the WiFi row; `.` Enter → `ui.lang == "en"`; still `en` after `debug.reboot`; run a subset of `nav` and `menu` in English; the fixture restores the original language |
| `nav` (changed) | Same | Opt+M still reaches `system`; the row in the Tab menu's "Go" page is called "Settings" (设置) |
| `test_runtime_budget` (changed) | Device | "All new pages drawn" adds the Settings tab, Device tab, WiFi page, and home and menu in English; `missing_glyphs == 0`; free heap still ≥150 KiB, less than 1 KiB below before the change |
| G-WIFI device suite (when W1 is scheduled) | Device | `test_wifi_budget` (heap in three states, 50 on/off rounds), plugging in the Mac after a scan, holding G0 during a scan and start latency, repeatedly entering the WiFi page during `rec_soak` (must not scan) |

### 9.7 On the device (👤)

- **Phase A, about 10 minutes**: switch to English → pull power and restart → still English; record a memo and run a meeting in English; look at the English count pills on the lock card and the text printed on the "退格" (Backspace) keycap; glance at the WiFi explanation page; switch back to Chinese.

## 10. Phases and work packages

**Phase A: Settings + language + W0**. Release slot (👤 item 6): by default a minor release after v0.4.2 (recording history and to-dos, D10), provisionally **v0.4.3 "Settings and English"**. v0.4.1 "steady release" is docs/20's package A, and package B ships with the theme; neither moves.

| ID | Content | owns_paths | Depends on | Acceptance |
|---|---|---|---|---|
| **ST-1** i18n and interfaces | `Lang`, `Txt`, `check_pair`, width classes, `tr`, `trn`, `both`, `LangScope`, `kLangNames`; `HintKey` and `Module::hintKeys()`; optional `text` on `HelpRow` and `AppTile`; theme draws hint bars from the structure. **Infrastructure only, no strings moved** | `firmware/app/lib/i18n/{lang.h,lang.cpp,lang_names.h,txt.h,words.h}` (new), `firmware/app/lib/module_api/module.h`, `firmware/app/lib/theme/{hint_layout,theme}.{h,cpp}` (the structured-hint lines), `firmware/app/test/core/test_i18n/` (new), `firmware/app/test/compile_fail/i18n/` (new), `.github/workflows/ci.yml` (compile-fail step) | v0.4.1 release | `test_i18n` green; every compile_fail case fails to compile; all existing tests green |
| **ST-2** Settings storage and switching | `Settings`, `nvs_settings`, `Platform` interface, simulator implementation, boot read, switch flow; test-build `debug.lang` and `debug.state.ui.lang` (schema, vectors and firmware codec in the same batch) | `firmware/app/lib/core/settings.h` (new), `firmware/app/lib/hal/nvs_settings.{h,cpp}` (new), `firmware/app/lib/shell/platform.h`, `firmware/app/lib/shell/runtime.cpp` (the boot and switch lines), `firmware/app/lib/protocol/*` (the `debug.lang` lines), `firmware/sim/lib/simdev/*` (settings across restarts), `firmware/app/test/core/test_settings/` (new), `protocol/schema/v0/{debug.lang,debug.state}.schema.json`, `protocol/vectors/v0/messages/debug.*.json`, `protocol/README.md`, `daemon/src/cpad/protocol/specs.py`, `.github/workflows/{ci,release}.yml` (release firmware name check) | ST-1; D1–D10 merged (`runtime.cpp` is in package A) | `test_settings`; language survives `debug.reboot` in the simulator; `pytest protocol/tools` |
| **ST-3** Settings tab and W0 | Two tabs, Settings tab, Link and Sync rows, WiFi explanation page, 8-row Device tab, `help()`, hint bars; store `host_label` | `firmware/app/modules/system/*` (incl. new `system_texts.h`, `wifi_page.*`), `firmware/app/lib/module_api/module.h` (`SysInfo.host_label`), `firmware/app/lib/shell/runtime_link.cpp` (store at handshake), `firmware/app/test/ui/test_mod_system/`, `firmware/sim/scripts/settings.cpas` (new) | ST-2 | `test_mod_system` in both languages; screenshots `missing_glyphs == 0` |
| **ST-4** Move strings | Replace the Chinese in 35 files with `Txt`, in four batches: menus and key names / shell and recording / home, board, inbox / theme and signs; `words.h`; structured hints; plurals, dates, ages; name "系统" → "设置" (System → Settings) | `firmware/app/lib/help/*`, `firmware/app/lib/shell/*` (strings only), `firmware/app/lib/theme/theme_mando.cpp`, `firmware/app/lib/module_api/text_fit.cpp`, `firmware/app/modules/{home,board,inbox}/*` (incl. new `*_texts.h`), `firmware/app/lib/i18n/words.h` | ST-1; **after D1–D10 and package B (B1–B6) are all merged**; done in one short window | Each batch moves strings only, no logic; width tests in both languages green; Chinese screenshots pixel-identical to before |
| **ST-5** English review | Shorten the over-wide; left bound in `theme::sign`; the two Chinese overflows | Same files as ST-4, `firmware/app/lib/theme/theme.cpp` (`sign`) | ST-4 | All 23 from §6.9 within limits; both Chinese overflows fixed |
| **ST-6** Tools and tour | `tools/i18n`, English tour, English pass of `test_theme_sim`, the three fuzz invariants, HIL fixture and `settings` scenario, CI | `tools/i18n/*` (new), `firmware/sim/scripts/v04_tour_en.cpas` (new), `firmware/sim/test/test_theme_sim/`, `firmware/sim/test/test_fuzz_runtime/`, `tools/hil/scenarios/settings.py` (new), `tools/hil/*` (fixture), `.github/workflows/ci.yml` | ST-4 | CI runs `check_texts.py` and `texts.py`; English contact sheet reviewed; fuzzing green |
| **ST-7** Mac side: untitled meetings | `rec.items` sends `null` for no title; protocol case | `daemon/src/cpad/recordings/history.py`, `protocol/vectors/v0/messages/rec.items.json`, matching `daemon/tests/unit/*` | None; **must be merged before the v0.4.2 device side draws `rec.items`** | daemon tests green |
| **ST-7b** English notes (only if 👤 item 4 picks English) | `notes_lang`; export README and MCP truncation notice follow it | `daemon/src/cpad/recordings/route.py`, `daemon/src/cpad/config/settings.py` (the `notes_lang` lines), `daemon/src/cpad/archive/export.py`, `daemon/src/cpad/mcp/resources.py`, matching `daemon/tests/unit/*` | 👤 item 4 | Default behaviour (Chinese notes) byte-for-byte unchanged |
| **ST-8** App SDK | Template, `check_apps` measures both languages, bilingual Pomodoro, docs/16 adds the "do not keep formatted text" rule | `tools/apps/*`, `docs/16-writing-apps.md`, `examples/apps/pomodoro/*` | `feat/app-sdk` merged | `check_apps.py` passes in both languages |
| **ST-H** Device | §9.6 `settings`, `test_runtime_budget`; 👤 §9.7 | `docs/07-hil-checklist.md`, `docs/releases/v0.4.3.md` | All of the above | All pass |
| **ST-DOC** Docs | README (key table, language, using the WiFi tools), docs/04 §8, "系统" (System) in docs/13, docs/18 §5.6 "系统 → 档案" changed to "设置 → 档案" (Settings → Profiles), CHANGELOG | `README.md`, `docs/04-prd.md`, `docs/13-assistant-v2-plan.md`, `docs/18-context-profiles.md`, `CHANGELOG.md` | None | — |

```
Phase A:
v0.4.1 release ──► ST-1 (infrastructure only) ──► package B and v0.4.2's new UI write Txt from the start
D1–D10 merged ──► ST-2 ──► ST-3
D1–D10, B1–B6 merged ──► ST-4 ──► ST-5, ST-6 ──► ST-H (v0.4.3 release)
ST-7 before the v0.4.2 device side draws rec.items; ST-7b only if 👤4 picks English; ST-8 waits for feat/app-sdk; ST-DOC in parallel
Later (all wait for 👤 item 5):
W-M (Mac-side signal evaluation, together with docs/18 P8-B1) ──► you decide ──► W-G (incl. toolchain) ──► W1 (= docs/18 P8-W)
W2: not scheduled
```

- **ST-1 merges first**: it only adds types and checks, no strings moved. Once it is in, package B (`lock_screen`, `rec_overlay`, `rec_pages`, `status_bar`, `shell_render`, `menu_view`, home, board, inbox) and v0.4.2's recordings app write `Txt` directly for any new or rewritten strings, so ST-4 moves one batch fewer and does not fight package B over the same strings.
- **Phase A ships together**; no half-done release with a Settings tab that switches to English but is only half English [asm].
- ST-4 is a wide mechanical change: committed in four batches, each moving strings only, each rebasable on its own.

**Later (W1, only after you agree)**:

| ID | Content | owns_paths | Depends on | Acceptance |
|---|---|---|---|---|
| **W-M** Mac-side signal | cpad reads the default gateway's MAC address (or CoreWLAN's SSID, needs location permission) as the profile signal for docs/18 P8-B1; writes down what use device-side scanning still has | `daemon/src/cpad/...` (decided then), docs/18 §7.3 | 👤 item 5 | Evaluation result written into docs/18 |
| **W-G** Gate | Toolchain evaluation (ADR 0001 option C); §5.7 items 1–8; floor decided | `docs/gates/wifi.md` (new), `docs/gates/settings-probes/wifi/*` (add an on-device environment), `docs/adr/0001-toolchain.md` (result), `docs/12-m3-work-packages.md` (G-A6 floor) | You agree after W-M | §5.7 items 1–8 all have measured results |
| **W1** Nearby networks | Passive scan calling IDF directly; state machine; nearby networks page; Mando sniff; fake radio in the simulator | `firmware/app/lib/hal/wifi_radio.{h,cpp}` (new), `firmware/app/lib/core/wifi_logic.{h,cpp}` (new), `firmware/app/modules/system/wifi_page.*`, `firmware/sim/lib/simdev/*` (fake radio), `firmware/app/test/core/test_wifi_logic/` (new), `firmware/app/test/device/test_wifi_budget/` (new) | W-G | All W1 items in §9.2 and §9.6 |

## 11. Risks

| # | Risk | Response |
|---|---|---|
| 1 | English overflow only found on the device | Width classes block it at compile time using real glyph widths; CI re-measures with the real glyphs; the English tour contact sheet is checked by eye (§6.9, §9) |
| 2 | Once a format string goes through `tr()`, GCC no longer checks `%` against arguments (`-Wformat` only understands literals) | Compile time checks both languages have the same `%` sequence; native tests run every format string in both languages |
| 3 | Ad-hoc `Txt{...}` bypasses the compile-time checks (C++17 has no consteval) | Blocked by `check_texts.py` |
| 4 | Moving strings conflicts with parallel firmware branches (`feat/ux-decisions` changes keymap, shell, home, menu; package B changes the same strings) | ST-1 merges first and package B writes `Txt` directly; ST-4 waits for D1–D10 and package B; the system module changes (ST-3) hardly touch their files; ST-4 in four batches, strings only |
| 5 | Switched to the wrong language and cannot read it | The bilingual "语言 Language" row; Opt+M, `.`, Enter always switches back; the toast says in both scripts how to switch back; fuzzing guards "≤3 presses" |
| 6 | One stray press switches the whole UI language | The cursor starts on the WiFi row on every open (docs/18 §5.9; docs/20 U5, U18) |
| 7 | Toasts stay in the old language for a few seconds after switching | Accepted: at most 4.5 s; in return no unseen result is lost |
| 8 | HIL silently changes the language on your device | The fixture uses the test-build `debug.lang` and restores the original language at the end (§9.6) |
| 9 | Poor English word choices (what to call "等你", waiting) | Appendix A is a draft, 👤 item 3; shared words are all in `words.h`, change in one place |
| 10 | You expect a WiFi switch in Settings, but there is only an explanation | §0 and the WiFi page say why and how to use the factory firmware; the value says "None", not "Off", so it does not suggest a switch that can be turned on; 👤 item 5 asks what you want WiFi for |
| 11 | Linking WiFi drops free heap below 150 KiB | Measure first (G-WIFI items 3, 4); no WiFi code is merged until the floor is decided |
| 12 | `rec_task` starved while WiFi is on, the start of memos lost | W1 only scans when not connected to a Mac and not recording; pressing record records first, then shuts down; G-WIFI item 7 measures start latency |
| 13 | Turning WiFi on and off leaves fragmentation, the largest block does not come back | Measure heap after shutdown; if not back, disable scanning until the next boot, no automatic restart; 50 on/off rounds in the gate |
| 14 | NVS full, core 2.0.17 erases the whole NVS at boot (R4) | Language writes one byte, only when the value changes; W2's network table is a separate matter (§5.6 item 10) |
| 15 | What exactly is printed on the "退格" (Backspace) keycap | Check against the device [todo] |
| 16 | Fuzzing presses its way into Settings and switches language | Three invariants (§9.2); the device does not jump to the Launcher or restart itself, so there is no other exit |
| 17 | The Mac-side signal is enough, and W1 would be wasted | Do W-M first, then decide on W1 |

## 12. Not doing

- A third language; loading translation files from the SD card (unsigned, and uses heap); libraries like gettext or ICU.
- Switching automatically by the Mac's system language; asking you to choose a language at boot; 12-hour time; date formats by region.
- Translating data from the Mac (project names, session names, recording titles, the Mac's name, network names).
- Changing fonts or adding an English font: efontCN_12's ASCII is good enough.
- One-press Launcher entry on the device (launcher.md R8, §4.4); the device restarting itself (§5.4).
- No WiFi code linked in this version. Never later either: internet, cloud sync, OTA, NTP to the public internet, enterprise networks (eduroam) and web login, open networks, typing passwords directly on the device, reading passwords from the Keychain, flash encryption, home-made password obfuscation, Arduino `WiFi.h`.
- Other toggles in Settings: brightness, screen-off time, auto-lock delay (default already set in docs/20 D2), sound, mic gain, theme, meeting consent. Add when needed; the slot is reserved (below language, above the divider).
- Things changed on the Mac are not mirrored on the device: projects, destinations, recording pull, MCP permissions, time zone, `notes_lang`.

## 13. Decisions for you

With no reply, the defaults apply.

1. **Name and entry**: **Default: rename Opt+M to "Settings" and open the Settings tab first**; home tile "M Settings", diagnostics one tab to the right (`/`). Cost: diagnostics go from 1 press to 2. Alternative: keep "System", open diagnostics first, Settings on the second tab.
2. **How to switch language**: **Default: the cursor starts on the WiFi row, `.` moves to the language row, Enter switches directly between 中文 and English**, three presses in all. Alternative: the language row opens a two-row chooser, two more presses, with the benefit that a third language later needs no change.
3. **English word list** (Appendix A): **Default: per Appendix A, 24-hour time**. What most needs your eye: home tiles Projects · Waiting · Meeting · Note · AI queue · Session · Settings · Lock · Menu; "等你" (waiting for you) on Mando's sign is "You!"; "退格" (Backspace) is Del.
4. **Labels in notes on the Mac** (会议：/语音：/[音频]): **Default: stay Chinese**, unchanged even when the device switches to English; no `notes_lang`. Alternative: write `notes_lang: "en"` in `config.json` so lines written from then on are English (ST-7b).
5. **What you want WiFi for**: **Default: only W0 in this version (shows the connection, tells you how to use the factory firmware's WiFi); W1 and W2 not scheduled**.
   - If you want the device to "recognise places" and remind you to switch profile: do W-M first (check whether the Mac-side signal is enough, 0 B on the device), then decide on W1 (docs/18 P8-W, needs a toolchain change and passing G-WIFI).
   - If you want it to connect to a network, say what for: A: **sync to the Mac without a cable**: needs a toolchain change, pairing and encryption (Noise in 05 §8), and WiFi must be off while recording and transferring recordings, so it can only transfer when idle; weeks of work, plus one more attack surface. B: **time sync**: low value, the Mac already does it when USB is plugged in. C: something else, tell us.
6. **Release**: **Default: phase A ships as one release after v0.4.2 (recording history and to-dos), provisionally v0.4.3 "Settings and English", no half-done release; ST-1 merges first after the v0.4.1 release so package B and v0.4.2's new UI write `Txt` directly; ST-7 merges before the v0.4.2 device side**. Alternative: ship the Settings tab plus W0 first (ST-1–ST-3, in Chinese), English one release later.

## Appendix A: draft English word list

The main words are below. The full 357 are in `docs/gates/settings-probes/i18n/en_draft.py` and move into the various `*_texts.h` during ST-4.

| Chinese | English | Width (px) | Used in |
|---|---|---|---|
| 主页 | Home | 24 | Menu, hint bar |
| 项目 | Projects | 48 | Tile, title |
| 等你 | Waiting | 42 | Tile, title, lock pill |
| 开会 / 会议录音 | Meeting | 42 | Tile, page title |
| 记一条 | Note | 24 | Tile, editor title, menu |
| 排队给 AI | AI queue | 48 | Tile, editor title, menu |
| 开 session | Session (tile) / New session | 42 / 66 | Tile / editor title and menu |
| 回复 | Reply | 30 | |
| 设置 / 设备 / 档案 | Settings / Device / Profiles | 48 / 36 / 48 | Tile, tab pill |
| 连接 / 同步 | Link / Sync | 24 / 24 | Fact rows on the Settings tab |
| 在线 / 离线 | online / offline | 36 / 42 | Status word on the right of the Link row |
| 锁屏 / 解锁 | Lock / Unlock | 24 / 36 | |
| 菜单 | Menu | 24 | |
| 语音便条 | Voice memo | 60 | |
| 返回 | Back | 24 | |
| 待传 | To sync | 42 | Lock pill |
| 未归属 | No project | 60 | |
| 在线 / 同步 / 无响应 / 版本不兼容 / 离线 | online / sync / no reply / mismatch / offline | ≤48 | Status bar |
| 本页 / 去哪 / 控制 | Here / Go / Control | ≤42 | Tab menu |
| 在呢 / 在听 / 嗅一嗅… / 等你 / 存好了 / 没听到 / 打盹中 / 保存中… | Here / Listen / Sniff… / You! / Saved / Huh? / Napping / Saving… | ≤42 | Mando's signs |
| 无 · 只用 USB 线 | None · USB only | 90 | WiFi row |
| 固件 / 启动 / 存储 / 堆 / 电池 | FW / Boot / Store / Heap / Bat | ≤30 | Device tab labels (all ≤36 px) |
| 强 / 中 / 弱 | good / fair / weak | 24 | Nearby networks (W1) |
| 秒 / 分钟 / 小时 / 天 | s / min / h / d | | Ages |
| 周日 … 周六 | Sun … Sat | 18 | Status bar, lock screen |
| 退格 / 空格 / 按住G0 / 松开G0 | Del / Space / Hold G0 / G0 up | ≤42 | Key names |
| 早上好 / 中午好 / 下午好 / 晚上好 / 你好 | Morning / Hi / Afternoon / Evening / Hello | ≤54 | Home greeting |

## Appendix: evidence index

- **How to rerun** (all evidence is in `docs/gates/settings-probes/`):
  - Font file: during the simulator build, `firmware/sim/lib/efont_cn12/gen_font.py` generates `firmware/sim/.pio/gen/efont/efont_cn12.c` (M5GFX 0.2.33's `lgfx_efont_cn_12`).
  - Inventory: `python3 -I i18n/inventory.py <仓库根> lits.json` (373 Chinese literals, 35 files); width: `python3 -I i18n/measure.py <efont_cn12.c> lits.json measured.json` (357 strings, 23 over the limit).
  - New strings in this doc: `python3 -I i18n/width.py <efont_cn12.c> < i18n/strings.txt`; hint bars: `python3 -I i18n/hints.py <efont_cn12.c>`.
  - WiFi probes: see the comment at the top of `wifi/platformio.ini` (`pio run`, `nm` for `_heap_start`, `mapsum.py` to compare maps).
- **System page**: `modules/system/system_logic.{h,cpp}` (`kSystemLines = 9`, `add()` drops when full, the recording row yields to the warning row), `system_module.{h,cpp}` (`kValueX = 46`, `onKey` returns false, `kIdentityRows`, `kDividerGap`), `lib/module_api/canvas.h:111-116` (content area y=12, height 111).
- **Names and menus**: `modules/home/home_logic.h:31`, `lib/help/action_help.cpp:23`, `lib/help/menu_model.cpp` (`goto_rank`, `add_control`, `mac_text`), `lib/input/keymap.cpp` (the list layer's `;` `.` `,` `/`; `` ` `` and Backspace are both back; Fn arrow keys).
- **Layout**: `lib/theme/theme.{h,cpp}` (`kToastTextW = 224`, `pill`, `sign`, `tile`), `lib/theme/hint_layout.{h,cpp}` (split rules, Chinese key names), `lib/help/menu_view.{h,cpp}` (`kMenuKeyColW = 54`, `label_dx`), `modules/board/board_module.cpp:262-282` (tab pills, `kListX`), docs/19 §4, §6.
- **Text in state**: `lib/shell/toast.h:27,43` (`ToastText`, `last_hint`), `lib/core/app_state.h:74-81` (`RecLast` is codes and arguments), `lib/shell/rec_result.h:13` (`RecText` only for toasts).
- **Connection**: `lib/protocol/messages.h:65,81` (`HostLabel`), `lib/shell/runtime_link.cpp:51` (only stores `host_app`), `lib/module_api/module.h:61-80` (`SysInfo`).
- **Lock and clock**: `lib/shell/lock.h:27`, `lib/core/app_state.h:115` (unlocked by default at boot), `lib/shell/runtime_rec.cpp:140` (the clock is an in-memory offset).
- **Recording**: `lib/hal/audio_task.h:38-45` (`CPA_REC_ALLOC_AT_START=0`, priority 1, core 0), `audio_io.h:32-36` (6×2048 samples).
- **USB I/O**: `lib/usb_transport/{usb_transport.h,line_queue.h}` (8 KB receive, `LineQueue` send only), `lib/shell/runtime.cpp:111-123` (`rx_`, `framer_scan`), `lib/link/line_framer.h:18`.
- **Fuzzing**: `firmware/sim/test/test_fuzz_runtime/fuzz_user.cpp:127` (`kOptKeys` contains `m`).
- **Storage and budget**: `lib/hal/nvs_boot.cpp` (namespace `cpa`), `partitions/cpa_8MB.csv` (NVS 20 KB), `scripts/check_budget.py`, `test/device/test_runtime_budget/test_main.cpp:377` (`150u * 1024u`), docs/12:455 (G-A6), docs/13 §10.3, docs/19 §10, docs/releases/v0.4.0.md:83-88, docs/07:624-628 (§6.7 item 6: whole-app heap while recording not measured yet).
- **WiFi probes**: `docs/gates/settings-probes/wifi/`, `_heap_start`: base `0x3fc96810`, scan `0x3fc9e3e8` (+31,704 B), idf `0x3fc9ee30` (+34,336 B), ard `0x3fc9f030` (+34,848 B); images 257,632 / 615,696 / 644,576 / 684,432 B [test]. Local framework: `~/.platformio/packages/framework-arduinoespressif32/libraries/WiFi/src/WiFiGeneric.cpp:642-707,763`, `WiFiScan.{h,cpp}`, `tools/sdk/esp32s3/sdkconfig`, `esp_wifi.h`, `esp_netif.h`.
- **External WiFi sources**: IDF 4.4.7 S3 WiFi guide, RAM usage guide, NVS and flash encryption docs; IDF v5.5 NVS encryption (HMAC); Espressif AR2024-008 (IDF 4.4 EOL); arduino-esp32 #8033, PR #7044; Rust esp-radio memory data; M5 Cardputer-Adv product page; ESP32-S3 datasheet v2.2 table 5-7; RIOT's PHY/USB fix; atomic14 and WLED on mic noise; Jamf community and Apple developer forums (getting the SSID on macOS).
- **Launcher and security**: docs/gates/launcher.md (section 3, line 78 DDLB, line 98, R4, R8, L5, G-L5, item 8, shared NVS, `l_wifi`), docs/01:178,187, docs/05 §5.9, §5.10, §6.8, §6.10, §8, ADR 0001:77, ADR 0004, docs/18 §5.6, §5.9, §6, §7.3, §8.3, §15 item 8, docs/18:790.
- **Mac side**: `daemon/src/cpad/recordings/history.py:87`, `recordings/route.py:22-65`, `archive/export.py`, `mcp/resources.py:109,113`, `logging/redact.py` (`BODY_FIELDS`, `redact_fields` only looks at top-level keys), `app.py:370,392` (`str(exc)` goes into logs), `control/server.py` (0600, `cmd`), `device/link.py` (send queue), `cli/main.py` (`cpa` subcommands), `protocol/schema/v0/{hello,common,debug.state}.schema.json`, `protocol/README.md` items 13, 16, 17, 19, 20, CHANGELOG (`rec.items` on the v0.4.1 development line; package B not done except B6).
- **Parallel branches**: `feat/ux-decisions` (D1–D10, changes keymap, shell, home, menu, lock etc., not merged); `feat/app-sdk` (docs/16, `tools/apps`, not merged, read only).

## Appendix: decisions made while implementing ST-4 and ST-5 (2026-10-27)

- **Keys in English hint bars go in square brackets**: `"[Enter]open [`]back [Tab]menu"`. English is all ASCII, so under the old §6.6 rule it would merge into one key; the structured `hintKeys()` only covers modules, while the shell, confirm dialogs, the two lines under the lock card and the key rows on the card are still strings. So `hint_layout` gets rule 6: if a string has a "`[` at the start of a word", only brackets count; the key is inside the brackets, and the description runs until the next word-initial `[`. Chinese strings have no brackets, so they split exactly as before (Chinese screenshots pixel-identical). When it falls back to a single line, the brackets are dropped. The brackets make `Txt`'s width check over-count, so hint-bar strings use an unchecked `Txt`, and `test_hint_bars` actually draws them in both languages (keycaps, no fallback to one line, English keys the same as Chinese).
- **The "等你" (waiting for you) place is called Inbox in English**: the tile has only 37 px left after a one-letter keycap and `!9`, and "Waiting" (42) does not fit. Counts still say waiting: home "Afternoon, 2 waiting for you", lock card `Waiting 1`.
- **English home tiles**: a tile has 53 px after a single-letter keycap and 31 px after `Opt+R`, and a badge takes its width plus 4 px. So P is Board with only the count as badge (`Board 12`; the Tab menu row stays `run12`), R is Meet, S is Session. `test_mod_home` draws every tile with two-digit badges, and the names must be complete.
- **The English home hint bar** has no room for "双击免提" (double-press for hands-free): it reads `[G0]hold, talk after REC [Enter]open` (changed after review; the original "talk at ●REC" needed two reads); the double press is described in the Tab menu (`G0 x2`) and on the lock card.
- **English reasons start with a capital letter**, because they always start a sentence: "Offline: Mac queue only". A failed-request toast in English is `<what>: <code>, queued`; even the longest code (E_UNKNOWN_SESSION) fits in 224 px.
- **Checks**: `tools/i18n/check_texts.py` (CI): any Chinese literal in `lib/` or `modules/` that is not in a `constexpr Txt` constant fails, except `lang_names.h` and the key names used by hint-bar parsing; `texts.py` outputs a width table of all 428 pairs (none over the limit; 263 pairs where English is wider). Fuzzing: in a run in English with the Mac sending only ASCII, drawing any Han character (except "语言" and "中文") fails; in both languages, a still frame that draws text off-screen fails (except the frames of the Tab menu sliding in); at the end, Opt+M . Enter from home must switch in both directions.

## Appendix: English screenshot review and device run (2026-10-27)

**Status**: phase A's device side is done (ST-1 to ST-6), and ST-H's automated part passed on the device (docs/07 §9). Still left: 👤 §9.7's 10 minutes (pull power and restart, record a memo and run a meeting in English, look at the text printed on the "退格" Backspace keycap) and ST-7 / ST-7b (Mac side).

- **The review looked at 35 scripts in both languages**. Chinese screenshots did not change; English had 6 things to change, each with a failing test written first:
  - Text touching the right edge of the screen (22 English screenshots, off by 1 px, so fuzzing did not catch it): inbox and board hint bars changed to `[Opt+D]hide`; on the meeting page "now" is clamped to stay at least 2 px from the edge (Chinese "现在" also moves 2 px left when at the far right); the session line in the confirm dialog is truncated at 238 px, and quoted text wraps at 239 px per line.
  - The simulator's `FbCanvas` now counts text whose ink lights the last column (x=239) as out of bounds: ASCII u, w and n use the last column of their 6 px, while Han characters leave it empty. `test_ux_edges` runs all ux scripts and both tours in both languages, redrawing the whole screen for each screenshot to count (the last frames of the Tab menu slide-in are partial pushes). `test_hint_bars` requires hint bars ending in ASCII to stay at least 1 px from the right edge.
  - Wording: `[G0]/[Space]star` (no empty keycaps in English), `[Opt+N/Q/S]type`, `[G0]hold, talk after REC [Enter]open`, "Hello, no Mac yet" (home has its own lower-case copy; the inbox empty state stays "No Mac yet"), lock card "√ Saved 0:15" / "× Too short", "No project 5 (only 2)".
  - **Findable**: in the Chinese UI, the Opt+M row in the Tab menu's "Go" page reads "设置 Settings" (the home tile and the Settings tab label are still "设置").
- **Device** (docs/07 §9): the English redraw budget was measured for the first time, `CPA_BUDGET_EN max_render_us=38160` (Chinese 40,944), meeting page 22.2 ms, memo page 21.7 ms, all within 50 ms; storing English in NVS, reading it back, a bad value (7) read as Chinese, and restoring the original were all run on the device.
- **HIL fixture** (§9.6): after every run, `debug.lang` restores the language read at the start (Chinese if it could not be read), even on failure. The `lang` scenario (called `settings` in §9.6) switches to the other language with keys, checks it survives `debug.reboot`, switches back, and checks that survives a restart too; the item "run nav and menu in English" was not done.

## Appendix: how the review comments were handled

The review raised 5 high, 8 medium and 5 low items. Each was checked against the code and docs/05, 07, 12, 18, 19, 20, launcher.md and CHANGELOG: 17 hold and were all adopted; in item 6 the sentence "`psk` would also stay in `LineQueue`" does not hold (`LineQueue` is the send queue), and the rest was adopted.

| Review comment | Handling |
|---|---|
| **H1** Silently `esp_restart()` when the heap does not come back: loses lock state and the clock; W2's test connection would always trigger it | **Adopted**. Never restart automatically: a dirty shutdown only disables scanning until the next natural boot, and the WiFi row says "Off until restart" (§5.4 item 6, with reasons: `lock.h:27`, `app_state.h:115`, `runtime_rec.cpp:140`). W2 invariant: every restart is confirmed by you, with power plugged in and the screen on, lock and clock saved to RTC_NOINIT first; a test connection is also a confirmed action (§5.6 item 3). New principle 9 |
| **H2** "Go to Launcher…" breaks R8, ignores DDLB, takes 5 presses, depends on the factory firmware being installed, and sends you to a firmware that prints passwords in plain text | **Adopted**. Removed this row, `enterLauncher()`, the confirm dialog, the two refusal toasts and the HIL `launcher_hop`. W0 is explanation only, with a line "it prints WiFi passwords in plain text to serial" (§4.4, listing five reasons not to do it); "not doing" added to §12 |
| **H3** W2 would leave WiFi on while recording | **Adopted**. Hard invariant: no connecting or scanning while recording or transferring recordings; WiFi only on when idle (principle 3, §5.6 item 2); removed "guard the heap while recording" (§13 item 5 rewritten); before W2 is scheduled, G-WIFI items 4, 7 and 9 are re-tested with STA connected and lwIP resident (§5.7 item 10) |
| **H4** The memory budget is not solid: a 140 KiB floor resting on unmeasured numbers; the cost when on was borrowed; §8 and §5.3 disagree; mixed units; a `file.*` arriving mid-scan | **Adopted**. Removed the 140 KiB number; changing the floor first requires docs/07 §6.7 item 6 (§5.3, G-WIFI item 3); the cost when on is marked [inf] and stated as unmeasured, with a device probe added as G-WIFI item 4; the whole doc uses KiB (147.7 / 145.1 KiB), and §8 and §5.3 use the same method; W1 only scans when the link is not Ready, one rule that rules out `file.*` pre-emption, USB/PHY and transfer fragmentation (§5.4 item 2), and G-WIFI item 6 is simplified accordingly |
| **H5** `wifi.*` sends the network list to any Mac, and any Mac can push a network | **Adopted**, as a W2 invariant (§5.6 item 6): only answer the Mac bound to the current profile; with profile mode off, send no list; nearby networks only on the "waiting for Mac" card; `wifi.set` only accepted while that card is open. docs/18 §8.3 **not changed this time**: W2 is not scheduled, and writing a threat for a feature that does not exist would mislead; when scheduled, add a row next to T8, as written in item 6 |
| **M6** Holes in credential handling: `str(exc)`; device buffers; `--from-keychain`; risk rating; full NVS erased entirely | **Partly adopted**. WiFi paths do not log `str(exc)`, and the bad-password path must have a test (§5.6 item 7); the device zeroes `rx_`, `framer_.buf` and the decoded struct, and what cannot be zeroed (HWCDC receive buffer, stack copies, Python strings) goes into SECURITY.md (item 8); removed `--from-keychain` (item 4, §12); a plaintext password is rated as "a network credential that can be used elsewhere", more serious than recordings, naming Bruce (item 10); the R4 full erase is in item 10 and risk 14. **The sentence not adopted**: `LineQueue` is the send queue (`line_queue.h:1`), and `psk` never enters it |
| **M7** Opening Settings puts the cursor on the language row, so one stray press switches the whole UI language; "always back to the first row" was not stated; HIL switches with keys and does not restore | **Adopted**. The WiFi row goes first and the cursor returns to it on every open, so switching language becomes three presses, Opt+M, `.`, Enter (§0, §4.2, §4.6, §13 item 2); the HIL fixture uses the test-build `debug.lang` and restores the original language at the end (§7, §9.6, risk 8) |
| **M8** The Device tab is one row short and warnings get dropped; pill and row y differ; the merged storage row is too wide | **Adopted**. 8 rows: stack free merged into the battery row, ghost/reset into the warning row when non-zero, the recording count moved to the Settings tab's Sync row, the migration result replaces "free" (§4.5, every row's width measured); tab pills at y=12 on all three tabs; `test_mod_system` adds "the warning row is always visible" (§9.2) |
| **M9** The Link row was measured with a 6-character name; `host_label` is not stored at all | **Adopted**. The status word is pinned right-aligned, the name sits in the middle with `draw_fit`, "last …" removed (the Sync row has it); the name gets 108 px in both languages; `SysInfo.host_label` 25 B, static RAM in §0 and §8 changed to about +27 B (§4.2) |
| **M10** The Settings hint bar has no back key | **Adopted**. "[;.]选 [Enter]改 [`]返回 [Tab]菜单" / "[;.]pick [Enter]change [`]back [Tab]menu" (196 / 232 px); the Device tab and WiFi page show back too (§4.6) |
| **M11** Starting a recording would be slower while WiFi is on | **Adopted**. Start recording first and shut WiFi down at the same time (the ring buffer exists from boot, 768 ms > 200 ms); G-WIFI item 7 adds start latency against a baseline (UX-H4's "one") (§5.4 item 4, §5.7) |
| **M12** Order and version names: ST-4 only waited for D1–D10; "v0.4.1 recording history" contradicts D10; docs/16 is not on this tree | **Adopted**. ST-1 merges first, package B and v0.4.2 write `Txt` directly, ST-4 waits for D1–D10 and B1–B6 (§10); phase A is v0.4.3 (after v0.4.2), and ST-7 must merge before the v0.4.2 device side draws `rec.items` (§6.11, §13 item 6); docs/16 is always marked "on `feat/app-sdk`, not merged" (§6.4, §6.8, §6.10) |
| **M13** Scope creep: W2 too detailed; W1 tied to v0.8; `notes_lang` done by default | **Adopted**. W2 compressed into 11 invariants (§5.6), with its protocol, budget, tests, work packages and risks removed; W1 becomes docs/18 P8-W, not tied to v0.8 (§5.2); the Mac-side signal evaluation W-M comes first (§5.1, §10); ST-7 is only `title: null`, and `notes_lang` becomes ST-7b, done only if you pick English |
| **L14** "关 / Off" suggests a switch that can be turned on | **Adopted**. Changed to "无 · 只用 USB 线" / "None · USB only", pill "(无)" / "(None)" (§0, §4, Appendix A, risk 10) |
| **L15** This doc's own strings break compile-time check 2; the width check can be exact | **Adopted**. The language row uses `both(Txt{"语言","Language"})`; the two switch toasts and "not saved" fill `%s` from `kLangNames`; check 2 has no exceptions any more; width computed exactly with the 11 6-px symbols, and "fall back to an unchecked `Txt` on false positives" removed (§4.3, §6.3, §6.4) |
| **L16** Clearing the lock card's recording result row on switch would lose results you have not seen | **Adopted**, and further: `RecLast` is already codes and arguments (`app_state.h:74-81`), drawn with `tr()`; on switch **nothing is cleared**, and toasts stay in the old language for at most a few seconds (§6.8, `test_settings`) |
| **L17** Fuzzing presses Opt+M | **Adopted**. Three invariants: language findable in ≤3 presses, no missing glyphs in English, settings survive restart (§9.2); no jumps or restarts, nothing to simulate |
| **L18** The evidence behind the conclusions is only in an uncommitted temp folder | **Adopted**. Probe sources and results, inventory and width scripts are committed with this doc in `docs/gates/settings-probes/`; `inventory.py` now does the `\x` restoration that was done by hand before, and a rerun gives the same 373 / 35 / 357 / 23; the header and the evidence index say how to rerun |
