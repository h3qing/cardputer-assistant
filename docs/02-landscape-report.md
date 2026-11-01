**English** · [中文](02-landscape-report.zh-CN.md)

# Cardputer ADV pocket work assistant: survey of existing projects

> Survey date: 2026-10-19.
> Method: 8 search directions in parallel: hardware, Cardputer firmware and frameworks, hardware companions for AI coding assistants, desktop companion frameworks, offline recording, computer-to-device communication and security, Chinese and Japanese communities, and a GitHub census. After merging and removing duplicates there were **201 projects**. Then came a "what is still missing" pass, with extra searches for 2 gaps, and an **adversarial check of the 10 most important projects**: reading the repository source, commit history, license and issues directly, looking for counter-examples.
> I also spot-checked on this machine that 20 of the repositories exist, and checked the Claude Code CLI's `agents --json`, `--bg` and `--name`.
> All 201 projects are in [research/appendix-all-projects.md](research/appendix-all-projects.md).

---

## TL;DR

1. **Your idea is very popular in 2026, but nobody has built all of it yet.**
   - There are 1,596 cardputer-related repositories on GitHub, and 1,165 of them were created in 2026.
   - Since April 2026, about 60 "Cardputer + AI coding assistant" projects have appeared. There were two triggers: Anthropic open-sourced the **Hardware Buddy** Bluetooth protocol of Claude Desktop ([anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)); and Anthropic handed out the Cardputer-ADV at the **Code with Claude** event, with an official plugin [cwc-makers](https://github.com/anthropics/claude-plugins-official/tree/main/plugins/cwc-makers).
   - But no single project does all of these: **a board grouped by project**, **a feedback queue**, **starting a new session from the device**, **taking notes with key chords**, **offline recording that syncs once connected**, **several encrypted connection types**, and **a modular plugin framework**. Almost all existing projects are written by one person, have 0 to 200 stars, are written as one block of code without modules, and many have no license.
2. **Recommendation: do not fork any single project.** Start a new open-source repository under MIT or Apache-2.0, in four layers:
   - Modular C++ firmware: PlatformIO + Arduino + M5Unified/M5Cardputer
   - A hub daemon on the Mac
   - AI assistant adapters: Claude Code hooks, `claude agents --json`, `claude --bg`, an optional mod
   - One versioned protocol, shared by USB, BLE and WiFi

   Then borrow designs from about 8 projects (see section 4), and build the official Hardware Buddy protocol as a compatibility module.
3. **How much audio fits**:
   - Internal flash holds only **a few minutes to a few tens of minutes**
   - An SD card holds **hundreds to thousands of hours**
   - The real bottleneck is the **battery**: recording with the screen off is estimated at 27 to 31 hours

---

## 1. Hardware facts that shape the architecture

| Item | Fact | Effect on the design |
|---|---|---|
| Chip | ESP32-S3FN8: dual core 240MHz, **8MB flash, 512KB SRAM, no PSRAM** (confirmed by the Espressif part table and several firmware configs) | Usable heap is only about 200 to 300KB. Push complex logic, rendering and integrations to the Mac where possible |
| Keyboard | TCA8418 I2C keyboard controller (0x34, interrupt pin G11), 7×8 matrix, **no diodes on the keys** | **Every two-key chord is free of false triggers**. Some three-key chords report one extra "ghost key" that was never pressed (see section 6) |
| Modifiers | Fn, Shift(Aa), Ctrl, Opt, Alt, plus the G0 button on the side. M5Cardputer 1.2.0 added an Fn layer (Fn+digit = F1 to F12, Fn+`;,./` = arrow keys) | G0 suits a dedicated "one press to jot, hold to record" key |
| Wake | G11 (keyboard interrupt) and G0 are both RTC GPIOs and can wake the chip from deep sleep or light sleep | "Sleep in the pocket, wake on a key" is possible. M5Unified has no ready implementation; we have to write it |
| Audio | ES8311 codec (8 to 96kHz), analog MEMS mic, NS4150B amplifier | The amplifier is **always on** in hardware and cannot be turned off in software (only plugging in headphones cuts it), which raises sleep current |
| Clock | **No RTC chip and no 32kHz crystal** | During sleep it drifts a few seconds a day, and the time is lost when the power switch is turned off. Sync the time every time it connects to the computer |
| microSD | On the SPI bus (shared with the EXT expansion port). ESP-IDF does not support exFAT by default | Format large cards as FAT32, or switch to the SdFat library. Max 4GB per file, so audio must be stored in segments |
| USB | Native full speed 12Mbps, device only. USB-Serial/JTAG and TinyUSB share one PHY | CDC serial needs no driver, and flashing can auto-reset. To act as a USB drive or HID keyboard you must switch to TinyUSB mode; you can only have one of the two |
| Battery | The official docs **changed 1750mAh to 1500mAh on 2026-07-13**; it depends on your unit's production date. Charger chip TP4057, about 300mA | Official measured current: 120mA screen on and working, 132mA with WiFi, 155mA with BLE |
| Bluetooth | BLE 5 only, **no Classic Bluetooth** | It cannot act as a Bluetooth headset or mic; audio can only go over WiFi, USB or a custom BLE service |

### How much audio fits (16kHz mono speech)

| Storage | PCM WAV (32KB/s) | IMA-ADPCM (about 8KB/s) | Opus 16kbps (2KB/s) |
|---|---|---|---|
| Internal flash, Arduino default 8MB partition (1.5MB file area) | **about 0.8 min** | about 3 min | about 13 min |
| Internal flash, OTA removed, one app partition (about 4.9MB) | about 2.7 min | about 10.6 min | about 43 min |
| 32GB SD card | **about 273 hours** | about 1,079 hours | about 4,375 hours |
| 128GB SD card | about 1,094 hours | about 4,315 hours | about 17,500 hours |

- **RAM only buffers a few seconds of audio**: 1 second of PCM is 32KB. So recording must write to the SD card as it goes, with a 0.5 to 1 second ring buffer to ride out stalls while writing to the card.
- **Battery life**: assuming 90% usable capacity, about 11 to 13 hours of continuous use with the screen on; about 27 to 31 hours with the screen and radios off, recording only (an estimate that needs measuring).
- **Nobody has measured the deep-sleep current**. From the schematic it should be in the milliamp range, about 3 to 4mA, mostly the always-on amplifier. When it will not be used for a long time, turn the power switch off.

---

## 2. Why so many similar projects appeared in 2026

- **2026-04-09**: Anthropic released [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) (2,658 stars). It is a BLE protocol for Claude Desktop's developer mode: line-delimited JSON over Nordic UART, carrying session counts and pending approval requests; the device can reply "allow once / deny".
- **The Code with Claude event**: Cardputer-ADVs handed out on site, with [moremas/build-with-claude](https://github.com/moremas/build-with-claude) and the official plugin [cwc-makers](https://github.com/anthropics/claude-plugins-official/tree/main/plugins/cwc-makers) (which has skills such as m5-onboard, Apache-2.0).
- **Claude Code itself opened more interfaces**: hooks, `claude agents --json`, `claude --bg --name`, mods (v2.1.287+), channels (research preview).
- The result is a large number of **one-person prototypes**, and nearly all of them only do "status display + approve/deny".

---

## 3. Main candidate projects (adversarially checked)

| Project | What it does | License | ADV support | Activity | Good as a base? | How to use it |
|---|---|---|---|---|---|---|
| [sohsatoh/cardputer-cli-buddy](https://github.com/sohsatoh/cardputer-cli-buddy) | **The "computer side" closest to your idea**: manages up to 9 Claude Code CLI sessions over BLE or WiFi. A Claude Code mod registers sessions, forwards questions and approvals, and **injects prompts as the user**. There is also a Python daemon, buddyd; traffic is encrypted with AES-CTR+HMAC, **keys are only provisioned over USB**, and speech is transcribed locally on the Mac | Apache-2.0 ✅ | ✅ (UIFlow2 MicroPython, pinned to v2.4.2) | New project, 2026-10, one author, 0 stars | Medium | **Copy the computer-side design**: mod + daemon + encryption. Replace the device side with your modular firmware. Note: no grouping by project, cannot start new sessions, no SD card, no offline recording |
| [dakshaymehta/cardputer-claude-os](https://github.com/dakshaymehta/cardputer-claude-os) | Most stars (208). Drop files into the `/apps` folder to install apps; MCP tools: notify, ask, confirm, show, progress; approval audit log, voice, Managed Agents message push | Apache-2.0 ✅ | ✅ | **Abandoned** (no commits after 2026-07-01, 7 outside PRs unanswered) | Weak | Borrow the **MCP tool interface design** and the audit log. Only one app runs at a time (switching needs a reboot), depends on a cloud service, MicroPython heap is tight |
| [anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) | The official Hardware Buddy protocol and reference firmware (M5StickC Plus) | MIT (except the bufo GIF assets) | ❌ (community ports only) | Officially **not maintained**, ports not accepted | Weak | **Build it as a compatibility module**, so it works with Claude Desktop even without the daemon. Downsides: only summary data, nothing per project, no prompt injection. Approval requests from CLI sessions started in a terminal **are not received** (claude-code #89435) |
| [TactilityProject/Tactility](https://github.com/TactilityProject/Tactility) | The most complete "ESP32 operating system": loads ELF apps from the SD card, has services and kernel modules, an app store, a Notes app, and an official ADV firmware package | Core GPL-3.0; SDK, modules and the ADV device folder Apache-2.0 | ⚠️ Yes, but **no audio on ADV** (the device tree lacks an ES8311 node), and **Ctrl/Opt/Alt produce no characters** | Very active (905 stars). But the maintainers do not accept small changes, and the main branch changes fast | Medium | **Worth watching, or build a Tactility app later**. But today it meets neither of the two core needs, recording and key chords, and whether ELF apps load reliably without PSRAM is unverified |
| [m0rg0t/cardputer-adv-ai-console](https://github.com/m0rg0t/cardputer-adv-ai-console) | **Offline recording to SD + a persistent outbox that uploads when WiFi returns**; a Mac gateway with TLS certificate pinning; a list of AI coding sessions and approvals | MIT ✅ | ✅ (C++, ADV only) | One maintainer, 0 stars | Medium | Borrow the **outbox queue, SHA-256 dedup, ES8311 recording service, writing WAV while recording**. Supports only Codex, stores keys in plain text on SD, code not modular |
| [fat23cat/cardputer-hub](https://github.com/fat23cat/cardputer-hub) | **The best architecture**: layered Services, Mini Apps, ActionBus, a Capability registry, with a macOS Companion written in Swift | ❌ **No license chosen** (all rights reserved) | ✅ | One person, 1 star | Weak (**legally unusable**) | Use the architecture as a reference only, or ask the author whether they would choose Apache-2.0 |
| [houxiaomu/m5stack-coding-toys](https://github.com/houxiaomu/m5stack-coding-toys) | A daemon in TypeScript that aggregates the status of several Claude Code sessions from statusLine and hooks, plus git info; uploads NDJSON over USB; generates the C++ protocol code from TypeScript | MIT ✅ | ⚠️ Just a shell (the keyboard part is not implemented, likely does not compile) | No updates since 2026-07 | Medium | Borrow the **daemon that aggregates multi-session status** and the protocol code generation. It is **read-only**: it cannot approve or reply |
| [charlottebepbop/cardputer-voice-remote](https://github.com/charlottebepbop/cardputer-voice-remote) | C++ firmware: records, uploads over HTTPS (certificate pinning, token provisioned over USB), transcribes with whisper.cpp, then sends into a chosen session through Channels | MIT | ✅ | **An account created today, a one-off code upload**, CI never really ran | Weak | Borrow the **LAN security model** and the "deliver through Channels" path. Review the code before flashing |
| [urazalievf/cardputer](https://github.com/urazalievf/cardputer) (CardputerOS) | Kernel and apps separated, `Ctrl+digit` global shortcuts, Markdown notes in Obsidian format, a Mac daemon | MIT | ❌ Original v1.1 only | Written in 17 hours, untouched since | Weak | Borrow the **App interface and compile-time registration** pattern |
| [srg0/s1-adv](https://github.com/srg0/s1-adv) | Many features, but all code compiles as one unit (25,000 lines), and the backend is tied to OpenClaw | MIT plus some LGPL | ✅ | Active | Weak | Chunked upload and authenticated OTA are worth borrowing |

**Conclusion**: no project can be used directly as the base. The main reasons are license, one-person maintenance, non-modular code and incomplete ADV support.

---

## 4. Your features one by one: who has done it, what to borrow

| Your feature | Best reference | What to borrow |
|---|---|---|
| **Show progress per project, mark which one needs my feedback** | `claude agents --json` (already in the local CLI, confirmed) + hooks; the status aggregation of [m5stack-coding-toys](https://github.com/houxiaomu/m5stack-coding-toys); [abdallah/session-hub](https://github.com/abdallah/session-hub) (MIT, HTTP API: sessions, inbox, messages, start, permissions) | Group sessions under projects by git root, and push live changes with hooks |
| **Quick feedback, injected into a running session** | cardputer-cli-buddy's mod, with `$.prompt.submit`; session-hub's long-polling mod; Channels ([tern/cardputer-claude](https://github.com/tern/cardputer-claude), voice-remote) | Make it an optional adapter. Mods are a new feature from 2026-10, and Channels is still a research preview |
| **Start a new session for a project** | `claude --bg --name <project>/<topic> "<prompt>"`, run in the project folder (confirmed in the local CLI); session-hub's start API; [herdr](https://github.com/herdrdev/herdr) (Apache-2.0, 45,000 stars, socket API supports agent.list, prompt, worktree.create) | Keep a `projects.json` in the daemon with project names, paths and aliases (as [walkie](https://github.com/LewisSwan24/walkie) does) |
| **Approvals** | [loml13/cardputer-claude-mcp](https://github.com/loml13/cardputer-claude-mcp) (MIT): a PreToolUse approval gate graded by risk, device replies signed with HMAC; cli-buddy's PermissionRequest hook, where whichever of terminal and device answers first wins | Different keys for different risks; dangerous actions need a long press; deny everything when the device is absent; write every decision to an audit log |
| **Take notes with key chords** | [pjournal-esp32](https://github.com/autumnc/pjournal-esp32): Ctrl chords as commands, Ctrl+Space switches Chinese/English, Ctrl+I for a quick note; [aikb](https://github.com/xyb/aikb) (MIT): single, double, triple tap and long press of one key mean different commands (tap-dance); CardputerOS: Ctrl+digit | See the key scheme in section 6 |
| **Offline recording, sent back once connected** | [maximmmanaev/cardputer-voice-notes](https://github.com/maximmmanaev/cardputer-voice-notes) (**no license**, design only): press G0 to record, one segment every 5 minutes to SD, resumable transfer, SHA-256 checks, the device deletes only after the Mac confirms receipt; m0rg0t's outbox; [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) (MIT, 31,000 stars, **officially supports ADV**, encodes Opus in real time on the PSRAM-less ADV) | Phase one: store PCM WAV on SD: no CPU cost and the best recognition. Phase two: make Opus a pluggable module |
| **Transcription on the Mac** | mlx-whisper large-v3-turbo; Qwen3-ASR (through [mlx-audio](https://github.com/Blaizzy/mlx-audio), the strongest for both Chinese and English in 2026-10 evaluations); whisper.cpp; Apple SpeechAnalyzer | You often mix Chinese and English, so **the transcription engine must be replaceable**; pick it with your own recordings |
| **Chinese display and input** | [hermes-remote-cardputer](https://github.com/Max-liu-1989/hermes-remote-cardputer) (MIT): render a 1-bit 240×135 frame on the Mac and send it to the device, about 1KB per frame; the efontCN 12px font is about 112 to 210KB; [zh_pinyin_decoder](https://github.com/FRIEDparrot/zh_pinyin_decoder) (MIT, the dictionary can live on the SD card) | No input method on the device in v1. Pinyin conversion happens on the Mac, and speech is also turned into Chinese on the Mac |
| **Modular framework (device side)** | ESPHome's external_components (compile-time modules with a manifest); Flipper's FAP external apps and API versioning; Stack-chan's MODs (the manifest declares the capabilities needed); [Mooncake](https://github.com/Forairaaaaa/mooncake) (the app lifecycle framework used by M5's official factory firmware, MIT) | v1 uses **compile-time module registration**. Loading apps (ELF) from SD at runtime comes later |
| **Modular framework (computer side)** | Stream Deck SDK / [OpenDeck](https://github.com/nekename/OpenDeck)'s OpenAction; Bitfocus Companion; DeskThing | **Community contributions will mostly happen in computer-side plugins**: a plugin is a separate process, can be written in any language, is declared by a manifest, and talks over a local WebSocket |
| **Communication protocol** | Flipper's protobuf envelope (command_id, has_next, oneof); Meshtastic's serial framing; Hardware Buddy's NDJSON; [stack-chan-dock](https://github.com/meganetaaan/stack-chan-dock) (MIT, already defines approval.request and task.status events) | Prefix message types by module, such as `cardputer.<module>.<action>`; when a connection is set up, first handshake to exchange versions and capabilities |
| **Security** | ESPHome's Noise_NNpsk0; KDE Connect's "pair only after the user confirms on both sides"; BLE with LESC bonding plus a numeric pairing code; libsodium sealing each file on SD with the Mac's public key | **First pairing goes over USB** (physical contact required); after that WiFi and BLE both use the encrypted channel |

---

## 5. Recommended architecture

```
┌──────────────────────────────────── Cardputer ADV (C++ firmware) ─────────────────────────────────────┐
│ Modules: notes │ voice │ projects │ approvals │ queue │ buddy-compat │ …                              │
│        ↑ compile-time registration: manifest + setup/loop/on_message/render                           │
│ Core: event bus │ keyboard (hardened TCA8418 driver) │ UI │ SD outbox (ULID+ack) │ power/sleep        │
│ Transport: USB CDC (primary) │ BLE NUS │ WiFi (TCP/WS)  ← one versioned protocol + Noise encryption   │
└───────────────────────────────────────────────────────────────────────────────────────────────────────┘
                     │ sync on USB plug-in / WiFi / BLE
┌──────────────────────────────────── Mac hub daemon (LaunchAgent) ─────────────────────────────────────┐
│ Owns serial / BLE / WiFi listeners; pairing and keys; outbox intake (idempotent); transcription queue │
│ projects.json (project = git root); one feedback queue per project                                    │
│ Plugins (separate processes, JSON over local WS):                                                     │
│   claude-code: hooks(HTTP) + `claude agents --json` + `claude --bg` + optional mod                    │
│   codex / herdr / session-hub adapters │ notes→Obsidian │ transcription engine (replaceable)          │
│ External: localhost MCP server (notify/ask/confirm/capture_note/list_queue)                           │
└───────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

**Key choices and why:**

- **C++ firmware, not MicroPython (UIFlow2).**
  - UIFlow2 leaves only about 60 to 128KB of free RAM on the ADV.
  - UIFlow2 versions after v2.4.3 (based on IDF 5.5) **record silence** on the ADV.
  - We need to run recording, encryption, BLE, WiFi and several modules at once, and MicroPython cannot carry that.
  - **Note**: UIFlow2 can be a tool for "learning the hardware fast in week one", but not the framework.
- **Arduino (PlatformIO) or ESP-IDF?**
  - Recommended: start with PlatformIO + Arduino + M5Unified/M5Cardputer; it is the fastest to get going, and there are official configs.
  - Later, when fine control of power and USB composite devices is needed, move to "ESP-IDF + Arduino as a component", as cardputer-hub does.
  - This is your decision; see section 9.
- **USB CDC as the main link**: no driver, auto-discovery by VID:PID, auto-reset when flashing. USB drive mode (MSC) becomes a "transfer mode" entered by hand, because once in it the device and the computer cannot access the SD card at the same time.
- **Put the complex parts on the Mac**: with no PSRAM, the device only captures input, shows text, and stores the outbox and recordings; the daemon does integrations and rendering.
- **Tactility versus our own firmware**:
  - Tactility is the only real "operating system + plugins" option, but today the ADV has no audio on it, the modifier keys produce no characters, the core code is GPL, and the maintainers do not accept small changes.
  - So **v1 is our own**, with module interfaces designed to be portable. Once Tactility adds ADV audio, we can evaluate turning modules into Tactility apps.
- **Compatibility with the official Hardware Buddy**: as a module. Then it works with Claude Desktop even without the daemon, and the community's existing CLI bridges can be used directly, such as [cc-buddy-bridge](https://github.com/SnowWarri0r/cc-buddy-bridge).

---

## 6. Key chord design (draft, for discussion)

**Hardware constraint**: the keyboard has no diodes, so **two-key chords are always safe**. Among three-key chords, these combinations report an extra "ghost key" that was never pressed:
- Fn+Shift plus keys in the a-to-Enter row
- Ctrl+Opt plus keys in the z-to-Space row
- Ctrl+Alt plus the digit row, q/w/e, a/s, z, and others

Fn+Alt, Shift+Alt and Opt+Alt with a third key each have only 2 problem keys.
Also, two projects report the TCA8418 **locking up** after fast typing, so we need a more reliable driver of our own: drain the FIFO on every pass, and add a watchdog that re-initializes it when it gets stuck.

**Recommendation**: use **Opt as the "this app's own layer"** (Opt has no meaning in HID and does not clash with system shortcuts), together with the **G0 physical key** and a "press a leader key once, then a letter" style:

| Action | Keys | Notes |
|---|---|---|
| Jot a quick note | **G0 short press** | Wakes directly from any screen or from sleep, into the input box |
| Hold to record (push-to-talk) | **G0 long press** | Keep a 300 to 500ms pre-roll buffer so the first words are not cut off |
| Switch to project 1 to 9 | **Opt + 1…9** | |
| Add feedback or a queued task to the current project | **Opt + Q** | Stored in the outbox, sent to the daemon once connected |
| Start a new session for the current project | **Opt + S** | The daemon runs `claude --bg` |
| Open the "waiting for my feedback" inbox | **Opt + I** | |
| Approve (normal) / deny | **Enter / Esc** | Key presses within 400ms after an approval request appears are ignored, to prevent mistakes |
| Approve (high-risk action) | **Fn + Y long press** | A different key from the normal approve |
| Leader key mode | Tap Opt once, then a letter | One-handed, with no false triggers at all |

This table must be verified once the device is connected, with raw key code logs, especially the chords `Ctrl+Shift+S`, `Fn+Shift+A` and `Ctrl+Alt+W`.

---

## 7. Claude Code integration points (by stability)

| Layer | What it can do | Stability |
|---|---|---|
| **Hooks** (configured in settings, HTTP callbacks to 127.0.0.1) | Receive status (Notification, Stop, SessionStart); **approvals** (PermissionRequest); answers to AskUserQuestion (PreToolUse changes the input) | ✅ Long-lived, fully documented |
| **Agent SDK** | Sessions the daemon creates and fully controls itself | ✅ Officially maintained, versioned |
| **`claude agents --json [--all]`** | Each session's state (cwd, working/blocked/done, what it is waiting for) | 🟡 Agent view is a research preview, but the docs state its JSON is the supported way to read state from outside |
| **`claude --bg --name`** | Start a new session for a project in the background | 🟡 Present in the local v2.1.272 |
| **Mods (`$.prompt.submit`)** | Inject a prompt into a running interactive session | 🟠 Only added in v2.1.287 (2026-10-01), behavior still changing. **The local version is 2.1.272 and must be upgraded to use it** |
| **Channels** | Push device messages into a session and forward approval requests | 🟠 Research preview; custom channels must start with `--dangerously-load-development-channels` |
| **Hardware Buddy BLE** | Only supported in Claude Desktop's developer mode, summary data only | 🟠 Officially "not a supported feature" |

**Principle**: when the daemon starts, it first detects capabilities (Claude Code version, whether `agents --json` works). Prompt injection is optional. Do not use PTY or tmux screen scraping and fake keystrokes as the main path.

---

## 8. Known pitfalls

1. **The silent recording problem is not fixed yet** (as of 2026-10-19): toolchains based on ESP-IDF 5.5 (UIFlow2 ≥2.4.3, M5Stack Arduino board package 3.3.x) **record all zeros** on the ADV; ESP-IDF #18621 is still open.
   - Workaround: **pin the M5Stack board package to 3.2.x (IDF 5.4)**, or bypass M5.Mic and write the recording channel yourself with `i2s_std`.
   - Add a mic self-test at boot.
   - This affects the "install the latest version" advice in [01-mac-setup.md](01-mac-setup.md); that doc already has a reminder.
2. **No RTC**: sync the time every time it connects to the computer; note and recording IDs must use a monotonically increasing sequence number, not timestamps alone.
3. **Sleep current is high**: the amplifier is always on, probably in the milliamp range. Before sleeping, turn off the ES8311, the screen backlight (GPIO38) and the ST7789, then measure. **Do not sleep while on USB**, because USB-Serial/JTAG disconnects in deep sleep.
4. **SD card**: exFAT is not supported by default; format large cards as FAT32, split audio into 5-minute segments, write files as `.part` first and rename them when done.
5. **USB drive mode and the SD card are mutually exclusive**: the device must unmount the file system before entering MSC. Launcher's bug mounting the ADV on macOS was just fixed on 10-15 and has not been released yet.
6. **License hygiene**: many good projects have no license (cardputer-hub, clawdputer, voice-notes, tern/cardputer-claude, walkie); **borrow designs only, never copy code**. The bufo GIF assets in claude-desktop-buddy are not covered by MIT.
7. **Security**:
   - **Do not turn on flash encryption and Secure Boot by default**: they are basically one-way, and once on, community flashing tools such as M5Burner and Launcher no longer work.
   - Instead, encrypt SD files one by one, sealed with the Mac's public key.
   - The approval interface must **never be exposed to the public internet**. tern/cardputer-claude's Tailscale Funnel approach is the counter-example.
8. **An official screenshot is wrong**: in the M5Burner factory-reset tutorial screenshot, the dialog title says "Burn Cardputer User Demo". You actually need the **Cardputer-Adv** User Demo; do not be misled.

---

## 9. Next steps: suggestions for phase one

**Decisions you need to make:**
1. **License**: MIT or Apache-2.0? Apache has a patent grant clause, and cli-buddy and cardputer-claude-os both use it, so borrowing their code is easier.
2. **Firmware stack**: start with PlatformIO + Arduino, or go to ESP-IDF from the start?
3. **AI assistant scope**: only Claude Code in v1, or Codex too?
4. **Chinese**: only display in v1, with input left to the Mac and voice? Or Pinyin input on the device from the start?
5. **Connections**: USB only in v1, then add WiFi and BLE? Or all three at once?

**Step 0 (hardware)**:
- Get USB recognized first (see [01-mac-setup.md](01-mac-setup.md))
- Read the chip and flash info, and confirm there really is no PSRAM
- **Back up the factory firmware**
- Measure with key code logs whether chords trigger falsely
- Test the mic

**Phase 1 (1 to 2 weeks of validation)**:
- **Firmware hello**: keyboard input → NDJSON to the computer over USB → write to SD → record a 10-second WAV. Pin the toolchain to the IDF 5.4 set.
- **Mac daemon hello**: group sessions by git root with `claude agents --json`, and show "Project A: 2 running, 1 waiting for you" on the Cardputer.
- Run one full loop end to end: **G0 quick note → stored in the SD outbox → auto-sync when USB is plugged in → lands in the notes folder on the Mac**.

**After that**:
1. Offline recording plus transcription
2. Feedback queue and starting new sessions (hooks + `--bg`)
3. Module SDK and manifest
4. Hardware Buddy compatibility module
5. WiFi and BLE, plus encrypted pairing

**Chances to give back to the community**:
- Contribute a more reliable keyboard driver to xiaozhi or M5Cardputer
- Help m5stack-coding-toys add ADV verification
- Ask the authors of cardputer-hub, clawdputer and voice-notes to choose a license
- Contribute a "device gateway" client to session-hub

---

## Appendix: main sources

- Official: [Cardputer-Adv product page](https://docs.m5stack.com/zh_CN/core/Cardputer-Adv), [ADV schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf), [M5Cardputer](https://github.com/m5stack/M5Cardputer), [M5Unified](https://github.com/m5stack/M5Unified), [factory firmware CardputerADV branch](https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV)
- Anthropic: [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy), [cwc-makers plugin](https://github.com/anthropics/claude-plugins-official/tree/main/plugins/cwc-makers), [build-with-claude](https://github.com/moremas/build-with-claude)
- Espressif: [esp-desktop-buddy](https://github.com/espressif/esp-desktop-buddy), [esp_tinyusb](https://github.com/espressif/esp-usb/tree/master/device/esp_tinyusb), [esp-adf-libs (Opus)](https://github.com/espressif/esp-adf-libs)
- Architecture references: [Flipper Zero firmware](https://github.com/flipperdevices/flipperzero-firmware), [ESPHome](https://github.com/esphome/esphome), [Stack-chan](https://github.com/stack-chan/stack-chan), [OpenDeck](https://github.com/nekename/OpenDeck), [Pebble mobileapp](https://github.com/coredevices/mobileapp), [Omi](https://github.com/BasedHardware/omi)
- All 201 projects: [research/appendix-all-projects.md](research/appendix-all-projects.md)
