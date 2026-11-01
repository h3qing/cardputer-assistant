**English** · [中文](05-architecture.zh-CN.md)

# 05 · Architecture (v0.1)

> Status: **final v0.1** · Date: 2026-10-19 · Role: Lead (merged the PRD, the reuse survey and three architecture drafts, and handled review comments)
> Sources: [02-landscape-report.md](02-landscape-report.md), [03-hardware-bringup.md](03-hardware-bringup.md), [01-mac-setup.md](01-mac-setup.md), `firmware/hwtest/`; `--help`, `agents --json` and the binary's enums of Claude Code 2.1.272 on this Mac; core 2.0.17 source.
> Marks: ✅ measured on this Mac or the real device · 📚 docs or source · ⏳ to be verified (each with a fallback)
> Readers: implementers of the firmware and the daemon, and module contributors. **What to build** is defined by [04-prd.md](04-prd.md); this doc only defines **how to build it**. The protocol (§6) is the only contract between the two sides.

---

## 0. One-page summary

1. **Three pieces of code, one contract.** The C++ firmware `cpa-fw`, the Python daemon `cpad` (command line `cpa`), and `protocol/` (JSON Schema, `limits.json`, test vectors). The firmware and the daemon are coupled only through NDJSON protocol v0, and contract tests keep the two sides from drifting apart.
2. **Pure core + imperative shell.** Business logic on both sides is written as pure functions: `step(const State&, const Input&) -> {State, Effects}`. I/O happens only in the shell. This satisfies "immutable", and all logic can be tested on the Mac.
3. **Execution only through online requests.** The outbox is the reliable delivery channel, but it **holds only notes and queue items**. Opening a session and replying go out as `agent.*` online requests: the device must be Ready, a confirmation box is passed, nothing is persisted, and nothing is retried automatically.
4. **v0.1 integrates only the Claude Code CLI.** While the device is online, `agents --json` is polled every 3 seconds to build the board and the inbox; a session is opened with `--bg --name … --permission-mode … -- <text>`; replying to an idle background session uses `stop` + `--bg --resume`, provided G3 passes. No hooks are installed and no TCP port is opened.
5. **The link uses only USB-Serial/JTAG (HWCDC).**
   - Device: 8KB receive buffer, a TX kick every 20ms, at most 2048B per line.
   - Daemon: safe opening of the serial port; handshake retried 3 times, 2s each; host-initiated data with unanswered parts not exceeding 4KB (credit flow control); a heartbeat every 5s.
6. **Device storage uses only LittleFS.** One file per entry: write `.tmp` first, `rename` after closing, delete on ack. Entry IDs are MonoIds and do not depend on time.
7. **Single-threaded firmware main loop.**
   - `AppState` is at most 4KB, statically double-buffered, with reducers per slice.
   - loop stack 16KB.
   - Display uses a full-screen 8bpp sprite (32KB); the Chinese font is efontCN_12.
8. **Modules are registered statically at compile time.** v0.1 has only three modules: `board`, `inbox` and `system`. The shared capture page (Compose) and the confirmation box live in the shell. Module code does not include Arduino or M5 headers, so it can be tested in the native environment.
9. **v0.1's security boundary is "a physical USB connection + the Mac user currently logged in".** The device holds no keys; the daemon checks the firmware prefix and binds the serial number; subprocesses get argv directly, never through a shell; the permission mode is set explicitly, `plan` by default; logs do not record body text.

---

## 1. Constraints and principles

### 1.1 Hard constraints that decide the architecture

| Constraint | Source | How the architecture responds |
|---|---|---|
| No PSRAM; free heap about 281KB, largest contiguous block about 254KB | ✅ 03 §1 | The device handles only input, display and the outbox; everything else is on the Mac. All buffers are fixed size |
| No RTC | 📚 02 §1 | MonoId as ID; wall-clock time may be missing (`wall_ms:null`) |
| HWCDC holds back the first reply | ✅ 03 §3 | The firmware kicks every 20ms; **the device never starts a handshake itself**; the host sends and retries |
| When the HWCDC receive queue is full it **silently drops bytes**; `setRxBufferSize` must be called before `begin` | ✅ core 2.0.17 `HWCDC.cpp` | Receive buffer set to 8192; a line ≤2048; unanswered host data ≤4096 |
| Opening the serial port may reset the chip | ✅ 03 §2 | Only the safe opening order; restarting the device only from inside the firmware (`debug.reboot` in test builds) |
| Default loop stack 8192B | ✅ `CONFIG_ARDUINO_LOOP_STACK_SIZE` | `SET_LOOP_TASK_STACK_SIZE(16*1024)` |
| The keyboard has no diodes, so some three-key combinations produce ghost keys | 📚 02 §6, ⏳ G1 | The input layer does "rectangle detection"; the keymap uses only two-key chords, with a static check |
| FatFS rename is not atomic; LittleFS's is | 📚, ⏳ G10 | v0.1 uses only LittleFS; SD moves to v0.2, with a checksum on every record |
| Toolchain A defaults to `-std=gnu++11`, GCC 8.4 | ✅ `platformio-build-esp32s3.py` | Changed to `-std=gnu++17` (verified along with G2); with B, GCC 14 has no such issue |
| The microphone issue is narrower than thought: it is in the switch from speaker to microphone | 📚 IDF #18621 | Recording is not on v0.1's critical path; the toolchain is decided by G2 |
| USB-JTAG disconnects in deep sleep | 📚 02 §8 | v0.1 does not sleep; on battery it only turns off the backlight |

### 1.2 How the engineering rules land in the architecture

| Rule | How |
|---|---|
| Immutable | Firmware: all logic is pure functions, slices returned by value; `AppState` lives in two static buffers, the shell assembles the new value into the other buffer and switches the index, and the old state is never modified in place.<br>Daemon: `@dataclass(frozen=True, slots=True)`, `tuple`, `dataclasses.replace`; persistence is append-only |
| The exception boundary (needs your confirmation, see 04 §16 item 5) | The serial ring buffers, display sprites and LittleFS file handles are inherently mutable and may appear only in `hal/` and `usb_transport/`. The daemon's serial object, file handles and subprocesses appear only in the shell |
| Files 200 to 400 lines (at most 800), functions <50 lines, nesting ≤4 | Folders split by feature. CI runs:<br>• `tools/repo/check_sizes.py`: warn over 400 lines, fail over 800<br>• `lizard -L 50`<br>• ruff `C901`, `PLR0915`<br>• from WP4, clang-tidy `readability-function-size` (`LineThreshold 50`, `NestingThreshold 4`) |
| Organise by feature or domain | Firmware: `lib/<domain>`, `modules/<feature>`; daemon: `cpad/<domain>`. No folders by type such as `models/` or `utils/` |
| No heap fragmentation | No `String` or `std::string` in domain code, always `FixedStr<N>`; ArduinoJson uses only one reused `JsonDocument` |
| TDD, 80% coverage | See §9. Each work package's acceptance tests are written first (RED) |
| Security | See §8 |
| Simplicity first | One thread, one link, one device, one AI adapter, three modules; the runtime depends only on `pyserial` |

### 1.3 Explicitly not in v0.1 (interface positions reserved)
Device pairing, hooks, HTTP/TCP ports, MCP, mods, SD card, recording, approvals, WiFi/BLE, Noise, loading modules at runtime, manifest checks, a Leader key, sleep, OTA, MSC/HID, a Codex adapter, SQLite, pydantic, Starlette.

---

## 2. Component diagram

```
┌────────────── Cardputer ADV firmware cpa-fw (C++17, toolchain per ADR-0001) ───────────────────────────┐
│ modules/   board (home + project pages) │ inbox │ system     ← depend only on module_api               │
│              ▲ onKey / render(Canvas&)       │ ctx.compose / request / open / toast                    │
│ shell/     nav stack │ key dispatch (overlay→global→module) │ Compose │ Confirm │ Toast │ StatusBar    │
│ core/      AppState(≤4KB, double-buffered) + slice reducers │ MonoId │ FixedStr │ Clock │ Result       │
│ input/     KeySet → GhostFilter → ChordDetector → KeyEvent (pure)                                      │
│ outbox/    OutboxCore (pure) ── FileStore interface ── LfsStore (hal)                                  │
│ link/      LineFramer (pure) → protocol/Codec (ArduinoJson) → LinkSession (pure)                       │
│ usb_transport/  HWCDC: RX 8KB, TX 4KB, 20ms kick, send queue ≤4 lines   ⚠ device-only build            │
│ hal/       M5Canvas 8bpp + efontCN_12, keyboard, LittleFS, NVS, battery/backlight  ⚠ device-only build │
└───────────────────────────────────┬────────────────────────────────────────────────────────────────────┘
                                    │ USB-Serial/JTAG CDC (303A:1001, bound serial number)
                                    │ NDJSON protocol v0 (protocol/)
┌───────────────────────────────────┴──────── Mac daemon cpad (Python 3.12 + pyserial) ────────────────────┐
│ device/    PortFinder → SafeOpen → Handshake → DeviceLink (reader thread, credit write queue, heartbeat) │
│ protocol/  codec + hand-written validators + limits (reads protocol/limits.json)                         │
│ hub/       HubState + reduce() → Effects; snapshot building (bounded, rev, ≤2048B)                       │
│ projects/  projects.json validation, cwd → project   inbox/  "needs you" derivation (debounce, ignore)   │
│ agents/    AgentAdapter interface ← claude_code/ (agents --json, spawn, reply)                           │
│ events/    events.jsonl (append-only + fsync) + fold     queue/  queue folded from events                │
│ notes/     daily markdown (block IDs, idempotent)   control/  unix socket (pause, status)                │
│ cli/       cpa daemon|status|project|queue|pause|service|device                                          │
└──────┬──────────────────────────────┬───────────────────────────────┬────────────────────────────────────┘
       │ subprocess (argv, no shell)  │ files                         │ AF_UNIX 0600
┌──────┴─────────────────────────┐ ┌──┴─────────────────────────┐ ┌───┴────────────────────────┐
│ claude agents --json           │ │ ~/CardputerNotes/*.md      │ │ cpa CLI                    │
│ claude --bg --name … -- <text> │ │ $CPA_HOME/events.jsonl …   │ │ firmware pre-upload script │
│ claude stop / --bg --resume    │ └────────────────────────────┘ └────────────────────────────┘
└────────────────────────────────┘
```

**Data flow 1: G0 quick note**
1. `G0Tap` fires, and the shell opens Compose.
2. The user presses Enter, and the shell builds an `outbox.item` with `kind` `note`.
3. `OutboxCore` emits two side effects, `WriteFile(tmp)` and `Rename`, which `LfsStore` executes.
4. After the write succeeds, the screen shows "Saved".
5. When the link is Ready, the device sends the line.
6. The daemon validates and deduplicates, appends an `item.received` event and fsyncs, replies `outbox.ack`, then renders the note.
7. The device receives the ack and deletes the file.

**Data flow 2: opening a session**
1. Opt+S opens Compose; after typing, the user goes to Confirm.
2. The user presses Enter in Confirm:
   - when the link is Ready, send `agent.spawn` (an online request, at most 1 in flight);
   - otherwise write a `queue(intent=spawn)` into the outbox.
3. On receiving `agent.spawn` the daemon in turn: validates → builds argv → runs `claude --bg …` → parses the short id → records an `agent.spawned` event → replies `agent.result`.

---

## 3. Repository layout (with the owning work package)

```
/                                   # Apache-2.0; WP1 does git init
├── LICENSE  NOTICE  README.md  CONTRIBUTING.md  SECURITY.md  .gitignore      [WP1; README quick start filled in by WP5]
├── .github/workflows/ci.yml                                                  [WP1]
├── protocol/                                                                 [WP1]
│   ├── README.md                  # points to §6 of this doc; versioning rules
│   ├── limits.json                # the single limits table, read by both sides (mirrored in firmware as limits.h, with a test comparing them)
│   ├── schema/v0/<type>.schema.json
│   ├── vectors/v0/messages/<type>.json   # valid and invalid cases (including malicious body text; byte-level cases use line_b64)
│   ├── vectors/v0/flows/<name>.json      # handshake, TX held back, outbox replay, reboot detection, credit, offline execution fallback
│   └── tools/check_vectors.py  tools/test_check_vectors.py
├── firmware/
│   ├── hwtest/                    # the existing probe firmware, left unchanged (used by G1, G2)
│   └── app/
│       ├── platformio.ini                                                    [WP2]
│       ├── src/main.cpp  src/module_list.cpp                                 [WP2 stubs first, WP4 takes over]
│       ├── lib/core/ lib/protocol/ lib/link/ lib/outbox/ lib/input/          [WP2, pure logic, testable on native]
│       ├── lib/module_api/ lib/shell/                                        [WP4, pure logic, testable on native]
│       ├── lib/hal/ lib/usb_transport/                                       [WP4, ⚠ device-only build]
│       ├── modules/board/ modules/inbox/ modules/system/                     [WP4]
│       ├── scripts/check_budget.py  scripts/pre_upload.py                    [WP4]
│       └── test/core/<suite>/  test/ui/<suite>/  test/device/<suite>/        [WP2: core; WP4: ui, device]
├── daemon/                                                                   [WP3]
│   ├── pyproject.toml  uv.lock    # package cpad; entry point cpa; the only runtime dependency is pyserial==3.5
│   ├── src/cpad/{app.py, cli/, config/, protocol/, device/, hub/, projects/, agents/claude_code/,
│   │             inbox/, queue/, events/, notes/, control/, service/, logging/}
│   └── tests/{unit/, integration/, contract/, fixtures/{bin/fake-claude, agents/*.json}}
├── tools/
│   ├── repo/check_sizes.py  repo/test_check_sizes.py                         [WP1]
│   └── hil/run_e2e.py  hil/scenarios/  hil/tests/                            [WP5]
└── docs/  01..03 (existing)  04-prd.md  05-architecture.md  07-hil-checklist.md [WP5]  adr/0001-toolchain.md  adr/0002-protocol-v0.md [WP1]
```

Rules:
- Under `lib/`, apart from `hal/` and `usb_transport/`, and in `modules/` too, code **must not include `Arduino.h`, `M5*` or `esp_*`**. CI checks with grep; the native env excludes those two folders with `lib_ignore = hal, usb_transport`.
- Adding a module = a new folder `modules/<id>/` + one line in `src/module_list.cpp`.

`platformio.ini` (toolchain A is the baseline; if ADR-0001 chooses B, only the `platform` line changes):

```ini
[common]
lib_deps_board =
    https://github.com/m5stack/M5Cardputer.git#1.2.0
    m5stack/M5Unified@0.2.26
    m5stack/M5GFX@0.2.33
lib_deps_core = bblanchon/ArduinoJson@7.4.3

[env:cardputer-adv]
platform = espressif32@6.10.0      ; B = https://github.com/pioarduino/platform-espressif32/releases/download/54.03.21-2/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
board_build.partitions = default_8MB.csv   ; 2×3.2MB app + 1.5MB data partition (LittleFS)
board_build.filesystem = littlefs
build_unflags = -std=gnu++11
build_flags = -std=gnu++17 -Wall -Wextra -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1 -DCORE_DEBUG_LEVEL=0
lib_extra_dirs = modules
lib_deps = ${common.lib_deps_board} ${common.lib_deps_core}
extra_scripts = pre:scripts/pre_upload.py

[env:cardputer-adv-test]           ; HIL: with debug.* hooks
extends = env:cardputer-adv
build_flags = ${env:cardputer-adv.build_flags} -DCPA_TEST_HOOKS=1

[env:native]
platform = native
test_framework = unity
lib_extra_dirs = modules
lib_ignore = hal, usb_transport
lib_deps = ${common.lib_deps_core}
build_flags = -std=c++17 -Wall -Wextra -Werror --coverage -DCPA_PROTOCOL_DIR=\"${PROJECT_DIR}/../../protocol\"
```

---

## 4. Reuse decisions (with versions)

| # | Capability | Decision | Exact choice | Reasons and fallback |
|---|---|---|---|---|
| 1 | Firmware toolchain | REUSE | **A (baseline)**: `espressif32@6.10.0`, i.e. Arduino 2.0.17 / IDF 4.4.7 / GCC 8.4, which hwtest already runs. **B (candidate)**: pioarduino `54.03.21-2`, i.e. Arduino 3.2.1 / IDF 5.4. **B is used only if all four checks pass on G2 day** | IDF 4.4 is EOL; the factory firmware and CardMind both show the microphone works under IDF 5.4. B's HWCDC is the same source as A's, so **the kick is still needed**. Never write `platform` without a version: the official v8.0.0 has already jumped to 3.3.12 (IDF 5.5) today. C (Arduino 3.3.9 or later, HWCDC fixed) is the v0.3 target |
| 2 | Build and firmware tests | REUSE | PlatformIO 6.2.0 (installed with uv tool), Unity (bundled with PIO), gcovr | — |
| 3 | Board libraries | REUSE | M5Cardputer git tag `1.2.0`, M5Unified `0.2.26`, M5GFX `0.2.33` | SPDX MIT marked per file. If the keyboard misbehaves, ADAPT `TCA8418KeyboardReader` into this repository (keeping the file header): drain the FIFO every round, and add a watchdog |
| 4 | Display | REUSE + BUILD | M5GFX `M5Canvas` full-screen 8bpp (32,400B), plus 5 hand-written widgets; font `fonts::efontCN_12` (213,444B) | No LVGL `9.6.0`: typical use needs about 100KB of RAM, not worth it for plain text lists. Mooncake `2.3.3` only lends its lifecycle naming |
| 5 | JSON | REUSE | ArduinoJson `7.4.3` | When the v0.3 wireless link needs compression, use its built-in MessagePack |
| 6 | Device storage | REUSE + BUILD | The core's LittleFS (esp_littlefs) and Preferences (NVS); the outbox is our own | SD card (core `SD`/FatFS) moves to v0.2; SdFat `2.3.1` is not used |
| 7 | Daemon runtime | REUSE | Python 3.12 (uv), `pyserial==3.5`, otherwise only the standard library: asyncio, json, fcntl, socket, subprocess, plistlib, hashlib, os | Starlette, uvicorn, pydantic, SQLite and MCP are all decided in v0.2. No `pyserial-asyncio` (unmaintained since 2021); a reader thread + `call_soon_threadsafe` instead |
| 8 | Daemon dev dependencies | REUSE | pytest 9.x, pytest-asyncio 1.x, pytest-cov, hypothesis, ruff, jsonschema 4.x (only in contract tests), lizard | Exact versions locked in `uv.lock` |
| 9 | Claude Code | REUSE CLI | Verified on 2.1.272: `agents --json [--all]`, `--bg`, `-n/--name`, `--permission-mode`, `stop`, `--bg --resume` | hooks, mods (need 2.1.287 or later), channels and the Agent SDK are not in v0.1 |
| 10 | LaunchAgent | ADAPT | Following the idea of cc-buddy-bridge's (MIT) `_service_launchd.py`, generate the plist with `plistlib` | Attribution kept |
| 11 | Design borrowed | ADAPT | coding-toys (MIT): map cwd to the git root and cache it, drop lines not starting with `{`, the lesson about the HWCDC RX buffer. m0rg0t (MIT): one file per outbox entry. CardMind (MIT): budgeting JSON card size in firmware. cli-buddy (Apache-2.0): the starting point for v0.2's mod adapter | Original copyright headers kept when copying code, and listed in NOTICE |
| 12 | Later candidates | Decide later | v0.2: `mcp==2.3.*`, whisper.cpp `1.9.5`. v0.3: NimBLE-Arduino `2.5.1`, Noise following ESPHome's noise-c approach | — |

**License policy**:
- Code under MIT, BSD, Zlib or Apache-2.0 may be copied, keeping the copyright notices in NOTICE.
- Repositories without a license, and GPL-3.0 (the Tactility core), lend only design, never code.
- arduino-esp32 is LGPL-2.1. Our source is fully public and users can rebuild it themselves; NOTICE says so clearly.
- The efont font licence goes into NOTICE together with M5GFX.
- The TypeScript Agent SDK is not under an open-source licence; v0.x does not depend on it.

**Corrections to existing docs** (made in WP1):
1. In 03 §3, "Arduino 3.x rewrote HWCDC" is inaccurate. The fix arrived only in 3.3.9 (PR #12606); 3.2.1 still needs the kick.
2. The microphone issue in 02 §8 is narrower: it is in the switch from the speaker to M5Unified's microphone.
3. 02 §4 becomes: "When the device is absent, approvals are not decided; the terminal asks as usual".

---

## 5. Firmware design

### 5.1 Runtime model

v0.1 uses only Arduino's `loopTask` (core 1), a single thread.

```cpp
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void loop() {
  const uint32_t now = clock.nowMs();
  hal.update();                                     // M5Cardputer.update()
  usb.kick(now);                                    // runs (void)(bool)Serial every 20ms
  for (uint8_t i = 0; i < 4 && usb.readLine(line); ++i)
    shell.apply(link_step(state().link, LinkInput::line(line, now)));
  shell.apply(chord_step(chords, KeySample{hal.keys(), now}));
  shell.apply(link_step(state().link, LinkInput::tick(now)));
  shell.apply(outbox_step(outboxState, OutboxInput::tick(now)));
  shell.renderIfDirty(canvas, now);                 // at most 20fps
  power.tick(now);
}
```

Time budget: one turn takes at most 20ms; no step may block for more than 50ms; a single LittleFS write is at most one entry (at most 2KB).

### 5.2 Pure core, shell and state buffers

```cpp
struct Effects { Effect items[4]; uint8_t count; };     // fixed size, not allocated on the heap
template <class S> struct Step { S state; Effects effects; };

Step<LinkCore>   link_step  (const LinkCore&,   const LinkInput&);
Step<OutboxCore> outbox_step(const OutboxCore&, const OutboxInput&);
Step<ChordState> chord_step (const ChordState&, const KeySample&);
ProjectsView reduce_projects(const ProjectsView&, const ProjectsSnapshot&);   // slice returned by value, ≤1.6KB
SessionsView reduce_sessions(const SessionsView&, const SessionsSnapshot&, uint32_t now);
```

**State buffers**: `static AppState g_buf[2]; uint8_t g_cur;`

Each time the shell updates `AppState`, it takes three steps:
1. copy `g_buf[g_cur]` into `g_buf[g_cur ^ 1]`;
2. replace only the slice that changed;
3. switch `g_cur`.

The old state is never modified in place, and the largest temporary on the stack is one slice.

**Guarantees for stack and memory**:
- `static_assert(sizeof(AppState) <= 4096)`, plus a native test checking the same condition;
- the System page shows `uxTaskGetStackHighWaterMark`.

### 5.3 AppState

```cpp
struct AppState {                 // ≤ 4096B
  LinkInfo     link;              // Detached/Attached/Ready; agent: ok|missing|error; caps.spawn/reply; last_rx_ms
  TimeInfo     time;              // known, offset_ms, tz_min
  ProjectsView projects;          // rev, rx_ms, 9×{slot, key[13], name[25], run, need, queued, ok}, other{run, n}
  SessionsView sessions;          // rev, rx_ms, total, 10×{sid[9], slot, name[33], kind, st, need, age_s, short[17]}
  QueueView    queue;             // rev, 12×{qid[13], slot, intent, text[81], sid[9]}
  OutboxInfo   outbox;            // pending, inflight, bad, full
  AgentReqInfo agent;             // idle | inflight{rid, kind, sent_ms}
  DeviceInfo   device;            // boot, bat_mv, heap/min/max_block, stack_hwm, ghost_n, kb_reinit_n
  UiContext    ctx;               // current project slot (decides Compose's default target)
};
enum class EventKind : uint8_t { Key, LinkChanged, StateChanged, OutboxChanged, AgentResult, Tick };
```

When the project list (`slot`, `key`, `name`) changes, it is written to `/cfg/projects.json` on LittleFS, for choosing targets and showing names while offline. Session and queue information is **not written to storage**.

### 5.4 Module interface (minimal)

```cpp
namespace cpa {
class Module {
 public:
  virtual ~Module() = default;
  virtual const char* id() const = 0;                          // ^[a-z][a-z0-9_]{1,15}$
  virtual bool begin(ModuleContext& ctx) = 0;                  // false means disabled, with the reason shown on the system page
  virtual void onEnter(uint8_t arg) {}                         // becomes the foreground module; arg is e.g. a slot
  virtual bool onKey(const KeyEvent& key) = 0;                 // true means handled
  virtual void render(Canvas& c, const Rect& area) const = 0;  // read-only: state() + its own UI state
};

class ModuleContext {                                          // FakeContext in tests
 public:
  virtual const AppState& state() const = 0;
  virtual void compose(const ComposeSpec& spec) = 0;           // the shared editor; on submit the shell decides outbox or online request
  virtual void confirm(const ConfirmSpec& spec) = 0;           // keys within 400ms of opening are ignored
  virtual bool request(const OnlineRequest& r) = 0;            // queue.done / inbox.dismiss / agent.*; false when not Ready or a request is already in flight
  virtual void open(const char* moduleId, uint8_t arg) = 0;
  virtual void toast(ToastLevel lv, const char* utf8) = 0;
  virtual uint32_t nowMs() const = 0;
};
}  // namespace cpa
```

- A module's own UI state (cursor, tab) is also a value type, updated by pure functions: `BoardUi next(const BoardUi&, const KeyEvent&, const AppState&)`, written in `<module>_logic.cpp`. The module class itself is only a thin shell.
- `Canvas` is an abstract interface with only these methods: `fill`, `text(x, y, utf8, Style)`, `textWidth`, `lineHeight`.
  - On the device, `hal/m5_canvas` implements it with M5GFX.
  - In tests, `FakeCanvas` implements it, recording the picture as lines of text so it is easy to assert on. v0.1 has no golden files.

> **v0.4.0 addition (additions only, all with default implementations, so v0.3 modules need no change; design in [13 §10.1](13-assistant-v2-plan.md))**:
>
> ```cpp
> struct HelpRow {                                             // one row of the Tab menu's "Here" page
>   KeyEvent ev;                                               // the key replayed when Enter is pressed on this row; rows with kind None are display-only
>   const char* keys = "";                                     // the key name written at the start of the row ("空格", "1-9")
>   const char* label = "";                                    // one line of description
> };
> class Module {
>   virtual const char* hint() const { return ""; }            // hint bar (since v0.1); from v0.4.0 "Tab菜单" at the right end
>   virtual uint8_t contextSlot() const { return 0; }          // the project being viewed, as Compose's default target (since v0.1)
>   virtual uint8_t help(const HelpRow** rows) const;          // this page's rows; by default 0 rows and *rows is nullptr
> };
> class ModuleContext {
>   virtual const SysInfo& sys() const = 0;                    // facts for the system page (since v0.1)
>   virtual bool press(const KeyEvent& ev) { return false; }   // press a key for me, queued to §5.6 step 9
> };
> ```
>
> - **`help()`**: `*rows` points to storage the module owns (usually a static array), valid until the module's next `onEnter()` or `onKey()`. Rows for a group of keys (such as `1-9`) set `ev.kind` to `None`: shown only, not executed. Tab need not be listed; the shell adds Opt+L.
> - **`press()`**: queues `ev` after the current key is handled, and runs it from §5.6 step 3 as if the user pressed it. One level only: a replayed key cannot queue another key. False means it was not queued: the default implementation, one already queued, or a replay in progress.
> - **Home tiles and menu rows execute only by replaying keys**; there is no separate "execute action" interface, so anything the menu can do can also be done by pressing keys directly, with the same effect.
> - The menu itself is not a module but a shell overlay: `MenuState` lives in the Shell, and its content and drawing come from the pure-function library `lib/help`.

### 5.5 Registration

```cpp
// src/module_list.cpp — adding a module changes only this file
static cpa::BoardModule  g_board;
static cpa::InboxModule  g_inbox;
static cpa::SystemModule g_system;
cpa::Module* const kModules[] = { &g_board, &g_inbox, &g_system };
```

Why not self-registration through static constructors: PlatformIO archives libraries as static libraries by default, and the linker drops self-registering objects nobody references; static initialisation order is also undefined. Only one check runs at start: ids must be unique. Checks such as manifests, capability bits and message ownership wait for the SDK in v0.2.

From v0.4.0 the list also has the home launcher `home` (`modules/home/`). If `home` is registered, it is the root page of the navigation stack; if not, the root page is `board`, the v0.3 behaviour (the Rig in existing tests does not register `home`, so the v0.1–v0.3 navigation tests are unchanged).

### 5.6 Shell, navigation and key dispatch

**Screen layout**:
- 12px status bar at the top;
- 240×111 content area in the middle;
- 12px key hint bar at the bottom.

**The navigation stack** is at most 4 deep. On the back key, the foreground module is asked first whether it handles it; only if not is the stack popped; when the stack is empty it returns to the root page: in v0.1–v0.3 the `board` home page, from v0.4.0 the home launcher if `home` is registered (§5.5). Opt+H clears the stack and returns to the root page. From v0.4.0, when a module opens a page itself with `open()`, it always pushes a new level, even for the same module (the project board's overview opens a project page), so back goes first to the overview and then to the root page; when a global key (Opt+P, Opt+1…9 and so on) opens a page and the top of the stack is the same module, only its argument changes, as in v0.3 (`nav.h`).

**Key dispatch order** (v0.4.0, [13 §5.4](13-assistant-v2-plan.md)):

| Step | Where | What |
|---|---|---|
| 1 | Runtime, before `power_key` | **Lock gate** (`lib/shell/lock.h`): while locked, only Opt+L, Enter within the 3-second window, and G0 pass (rules in 04 §8.8); every other key is swallowed and does not wake the screen. A G0 that passes goes straight down the recording path (a memo while the card is lit, its release, ★ during a meeting), whether or not a capture page is under the lock; the draft is untouched |
| 2 | Runtime | `power_key`: with the screen off, the first key only wakes the screen and is swallowed, except G0 (§5.11); Opt+L, which locks, is also an exception: it locks directly and the screen stays dark |
| 3 | Shell | **Tab menu**: while open it eats every key. G0 is the exception: the menu closes and G0 works as usual (★ during a meeting). When the menu is closed by holding Backspace, the auto-repeat of that hold is not passed on |
| 4 | Shell | **Tab and Opt+L**: look up the binding in the current layer. On a page it is `Layer::System`, on an overlay the overlay's own layer, so where they work is decided entirely by `kKeymap` (see the table below) |
| 5 | Shell | **Recording overlays** (memo HUD, meeting confirmation box, meeting page, stop confirmation box, v0.3): eat every key while the recording state machine is not Idle |
| 6 | Shell | **Overlays** (Compose, Confirm): an overlay eats every key it can handle. In Compose, Opt+1…9 change the target, and Opt+H and Opt+I are ignored |
| 7 | Shell | **Global chords**: G0, Opt+H/P/I/1…9/N/Q/S/M/R |
| 8 | Shell | **Foreground module**; back keys the module does not handle (`` ` ``, Backspace, Fn+`) pop the stack |
| 9 | Shell | **Key replay**: when a module's `press()` or Enter in the menu queued a key, run it from step 3 after the current key is handled. One level only; a replayed key cannot queue another key |

v0.3 has steps 5–8, v0.1 and v0.2 have no step 5; steps 1–4 and step 9 were added in v0.4.0.

Where Tab and Opt+L work in each layer (exactly as bound in `kKeymap`, 13 §5.3):

| Layer | Tab menu | Opt+L lock |
|---|---|---|
| Home and apps (`Layer::System`) | ✓ | ✓ |
| Capture page (`Layer::Compose`) | ✓, only "Here" and "Control" | ✓, the draft stays in memory |
| Confirmation boxes, including the meeting and stop confirmation boxes (`Layer::Confirm`) | ✓, only "Here" and "Control" | —, finish the confirmation box first |
| Meeting page (`Layer::RecPage`) | ✓, only "Here" and "Control" | ✓, recording continues |
| Memo HUD (`Layer::RecMemo`) | — | — |
| Lock screen (`Layer::Lock`) | — | The first step of unlocking |

The capture page's "Discard what you typed?" (放弃已输入的内容？) is handled as a confirmation box (`Layer::Confirm`): the menu's "Here" page has the prompt's own three rows (Enter discards the draft, `` ` `` and Fn+` go back to editing), and Opt+L does nothing.

While the menu is open, `debug.state`'s `overlay` still reports the layer under the menu; test firmware also reports an optional field `ui{menu, locked}` (protocol v0 only adds, see [protocol/README.md](../protocol/README.md)).

**Compose (capture page)**
- The argument is `ComposeSpec{kind: note|queue|spawn|feedback, project, sid}`.
- ASCII input only, one line, 1 to 500B. Text of kind `spawn` or `feedback` must not start with `-`.
- The draft exists only in memory.

On submit, the shell decides how to send by these rules:

| kind | Goes to |
|---|---|
| `note`, `queue` | Written into the outbox |
| `spawn`, `feedback` | Confirm opens first |

Confirm decides whether to execute or queue by these rules:

| Condition | Confirm shows | After Enter |
|---|---|---|
| `spawn`: Ready and `agent=ok` | Start | Send `agent.spawn` |
| `feedback`: all four of `kind=bg`, `st=idle`, Ready and `caps.reply` hold | Send now | Send `agent.reply` |
| Anything else | Save as queued, with the reason | Write an outbox entry with `kind=queue` and `intent` `spawn` or `feedback` |

**Results of online requests**: `agent.result` is handled by the shell, which shows the matching toast.

| Result | The shell's action |
|---|---|
| `ok` | Toast "Started · <short>" |
| Failure | The text is saved automatically as a queue item, then the error toast |
| Link down, or no result after a 30s timeout | Toast "Unknown result"; the text is saved as a queue item marked `uncertain` |

Online requests are never retried.

**Toast**: each shows for 2.5s, at most 3 queued. Comparing two successive sessions snapshots, when a session's `need` newly becomes true, it shows "<key>/<name> needs you".

### 5.7 Input layer

```
hal.keys() → KeySet (56 matrix bits + the G0 bit, a value type)
          → diff(prev, now)          // compares the whole key set; M5's isChange() only compares the number of keys pressed (✅ 03 §6)
          → GhostFilter              // a full rectangle in this frame → drop the whole frame, ghost_n++
          → ChordDetector (pure)     // → KeyEvent
```

`KeyEvent` has these kinds: `Char{c}`, `Chord{mods, key}`, `G0Tap`, `G0Long`, `G0LongUp` (v0.3: the release after holding G0), `Nav{Up, Down, Left, Right, Enter, Back, Bksp, Menu}`. `Nav::Menu` is Tab (v0.4.0, with or without Shift); in v0.3.1 and earlier Tab produces no event.

Rules:
1. **Fire on press.** An action follows the complete key set at the moment of the press; release triggers nothing (except G0).
2. **Modifiers and characters.** Modifiers are Fn, Shift, Ctrl, Opt and Alt. While any of Ctrl, Opt or Alt is held, no `Char` is produced.
3. **Ghost key detection.** Matrix coordinates use hwtest's mapping: `row = x/2`, `col = y + 4*(x%2)`. If the pressed set contains all four corners `(r1,c1)`, `(r1,c2)`, `(r2,c1)`, `(r2,c2)`, the ghost key cannot be told apart, so the whole frame is dropped and counted.
4. **Static keymap check.** `test_keymap_static` runs ghost detection on every combination in the keymap; any combination that could produce a ghost key fails the test.
5. **G0 short and long press.** Released within 500ms of the press, it is `G0Tap`; held for a full 500ms, `G0Long` fires at once without waiting for release. There is no gap between the two.
6. **Auto-repeat.** Backspace only: starts after holding 500ms, then repeats every 60ms.
7. **Keyboard watchdog.** If a key is held but the set has not changed for 10s in a row, call `Keyboard.begin()` to reinitialise, and add 1 to `kb_reinit_n`. Refine the condition once G1 reproduces the lock-up. If G1 fails, ADAPT the TCA8418 driver: drain the FIFO every round, and include the state of INT (G11).
8. **Leader key**: not in v0.1, nor in v0.4.0. **The Tab menu is not a Leader key**: pressing a letter with the menu open chooses among options visible on screen (on the "Go" and "Control" pages, when the page has that row; on the "Here" page, when not on an overlay and the global or system layer binds Opt+that letter), not a hidden key prefix; every row in the menu executes by replaying an ordinary key (the v0.4.0 addition in §5.4).

### 5.8 USB transport and link

**UsbTransport** (`usb_transport/`, device-only build):
- Initialisation order: `Serial.setRxBufferSize(8192)` first, then `setTxBufferSize(4096)` and `setTxTimeoutMs(0)`, and `begin()` last. All these APIs exist in 2.0.17 (✅).
- Run `(void)static_cast<bool>(Serial)` every 20ms, i.e. the kick.
- Before writing, check `availableForWrite() >= len`; if there is not enough space, put the line in a fixed-size send queue (at most 4 lines).
- When the send queue is full, only resendable `outbox.item` lines are dropped; reply messages are never dropped.

**LineFramer** (pure function):
- Splits lines at `\n`, ignoring `\r`.
- When a line exceeds 2048B, it drops everything up to the next `\n`, then reports `E_TOO_LARGE`.
- Lines not starting with `{` are dropped as noise, and counted.

**Codec**:
- Parses with one reused `JsonDocument`.
- Checks `v`, `t`, `id` and every field against `limits.h`. `limits.h` mirrors `protocol/limits.json`, and a test compares the two.
- Invalid UTF-8 is always rejected (`E_BAD_FIELD`).

**LinkSession state machine** (pure function):

| State | When entered | What is allowed |
|---|---|---|
| `Detached` | `isPlugged()` false **for 500ms continuously** (`lib/link/plug_filter`; measured in HIL: HWCDC's SOF detection occasionally misreports for about 1ms, and without debouncing the link drops out of Ready; also false with only a charger connected) | Send nothing |
| `Attached` | Plugged into a host, but no valid host `hello` received yet, or no data for more than 15s | Reply only to `hello` |
| `Ready` | A host `hello` with `v=0` received and answered | Everything |

A few rules:
- The device never starts a handshake itself.
- For every message the host sends on its own (without `re`), the device replies exactly once: `hello`, `pong`, `ack` or `error`.
- A host `hello` received while Ready means the daemon restarted. Then handshake again; in-flight outbox entries go back to pending, and in-flight agent requests are treated as "unknown result".

### 5.9 Outbox (LittleFS only)

**File layout**:
```
/outbox/<MonoId>.json        pending (the content is the outbox.item line to send, verbatim)
/outbox/<MonoId>.tmp         being written
/outbox/bad/<MonoId>.json    judged rejected by the daemon, or failed to parse at boot (at most 20 kept, the oldest deleted beyond that)
/cfg/projects.json           slot→key/name cache
```

**Write**: `open(tmp, "w")` → `write` → `close()` (LittleFS commits on close) → `rename(tmp, json)` → only on success is "Saved" shown. LittleFS rename is atomic (📚 littlefs's design; power-loss behaviour measured by ⏳ G10).

**Boot scan**:
- Only a `.tmp` and no `.json` of the same name: it was never committed, and the user never saw "Saved", so delete it.
- A `.tmp` beside a `.json` of the same name: **do not delete the `.tmp`**, only count it, and report "invariant broken" on the system page. By LittleFS's design this should not happen; FatFS rename on an SD card (v0.2) is not atomic, and this rule is there for that case.
- A `.json` that fails to parse, or whose parsed `id` does not match the file name: move it to `bad/`.

**MonoId**: Crockford base32, in the form `EEEEEEE-BBBBB-SSSS`, 18 characters in all.

| Part | Bits | Meaning |
|---|---|---|
| `E` | 32 | epoch: generated when NVS is first initialised, with `esp_random()` mixed with the MAC address. After a full Flash erase the epoch changes, so it never collides with old IDs already acknowledged |
| `B` | 25 | Boot count in NVS. At every boot **the count is incremented and written back to NVS before any ID is issued** |
| `S` | 20 | Sequence number within this boot |

Within one epoch, sorting IDs lexicographically sorts them by time. The daemon deduplicates by `(dev, id)`. The boot id is `EEEEEEE-BBBBB`.

**Send and acknowledge** (`OutboxCore` is a pure function):
- Send only when Ready, in ID order, at most 4 in flight at a time.
- Without an `outbox.ack` within 3s, resend unchanged; later intervals are 6s, 12s, and then 30s from there on. When the link drops, in-flight entries go back to pending, which does not count as a retry.
- Handle by the `status` of `outbox.ack`:
  - `stored` or `duplicate`: delete the file;
  - `rejected`: move to `bad/` and show a toast.
- An ack for an unknown ID is ignored.

**Capacity**: at most 200 pending entries, and at least 64KB of free space. When either limit is reached, Compose refuses to submit.

**Delivery semantics**: the device delivers at least once, and the daemon deduplicates idempotently by ID, so the effect is exactly once. An ack means "the daemon has fsynced this into `events.jsonl`"; it **does not mean** the note has been written.

### 5.10 Time
- `nowMs()` is `millis()`.
- Wall clock = `up_ms + offset`, where `offset` is set by `time.set{epoch_ms, tz_min}`. `tz_min` is kept so the status bar can show local time.
- The wall clock survives a soft reset and is lost on power loss; when the wall clock is unknown, `--:--` is shown.

### 5.11 Power (v0.1, minimal)
- When `isPlugged()` is true, the screen stays on.
- On battery, the backlight turns off after 60 seconds without a key press. After that:
  - any key only wakes the screen, and that key press triggers nothing;
  - G0 is the exception: it wakes the screen and opens a quick note directly.
- No sleep of any kind.

> **v0.4.0 addition** (`lib/shell/power.h`, [13 §7.1–§7.2](13-assistant-v2-plan.md)):
> - **The lock comes before "always on while plugged in"**: when locked, the screen goes dark, even when plugged into the Mac, except for the lock card's 3 seconds. The lock gate comes before `power_key` (§5.6 step 1), and keys it swallows do not wake the screen.
> - **Opt+L with the screen off locks directly**, rather than only waking the screen; the screen stays dark, so nothing is "executed on wake".
> - **● is always visible while recording**: when the recording state is Cue, Recording, EndConfirm or Finishing, the backlight dims no further than "dimmed", and ● and the timer are drawn, whether locked, with the menu open, or with a toast showing.

### 5.12 Display and Chinese
- A full-screen `M5Canvas`, 8bpp colour depth (32,400B), with `pushSprite` only when the state is dirty.
- The font is `efontCN_12`: 40 ASCII characters per line, 20 Chinese characters per line.
- The daemon truncates text at UTF-8 character boundaries; the device only checks byte limits.
- For missing glyphs, the HAL first tries to detect them with M5GFX's glyph lookup and draws `□` when detected; if detection is not possible, it records how M5GFX handles them by default. HIL tests this with real Chinese session names from this Mac.

### 5.13 Budgets (checked by CI and by the system page)

| Item | Budget |
|---|---|
| Static RAM (.data + .bss) | ≤96KB (hwtest is 89KB, of which 64KB is the microphone buffer, not needed in v0.1) |
| Free heap / largest contiguous block | ≥150KB / ≥64KB (including the 32KB sprite) |
| loop stack | 16KB; the lowest remaining stack must be ≥4KB |
| `AppState` | ≤4KB (`static_assert`) |
| Peak for parsing one message | ≤8KB of heap (2KB line buffer plus `JsonDocument`) |
| Application firmware | ≤2.5MB (each app partition is 3.2MB) |

`scripts/check_budget.py` parses `firmware.map` and the size of `firmware.bin`, and fails the build when a budget is exceeded.

---

## 6. Protocol spec v0 (the single source of truth)

### 6.1 Framing
- One JSON object per line, UTF-8, ending with `\n`. The sender never sends `\r`; the receiver ignores `\r`.
- **A line is ≤2048 bytes** (including `\n`), the same limit in both directions.
- Lines not starting with `{` are noise, such as boot logs. They are dropped without an error reply and only counted.
- The daemon serialises with `json.dumps(obj, ensure_ascii=False, separators=(",", ":"))`. Chinese is output as UTF-8 as is, not as `\uXXXX`. If the serialised line exceeds 2048B, entries are removed from the end of the list.

### 6.2 Envelope

```json
{"v":0,"t":"<type>","id":"<msg-id>","re":"<id>"}
```

| Field | Required | Rule |
|---|---|---|
| `v` | Yes | Major version, fixed at `0` throughout v0. On mismatch, reply `error{code:E_VERSION}` |
| `t` | Yes | Message type, matching `^[a-z][a-z0-9_.]{1,31}$` |
| `id` | Yes | Generated by the sender, unique within one connection, ≤24 ASCII characters `[A-Za-z0-9-]`. Ordinary device messages use `d<n>`, the host `h<n>`, and `outbox.item` uses a MonoId |
| `re` | Required in replies | The `id` of the message being answered |
| Others | | Payload fields, placed directly at the top level |

Handling rules:
- Unknown fields are always ignored.
- An unknown `t`: if the message is not a reply, answer `E_UNKNOWN_TYPE`.
- A line that cannot be parsed (`E_PARSE`) gets no reply, because there is no `id` to answer; it is only counted.
- Optional fields may be omitted, meaning "none". The daemon also accepts `null`.

### 6.3 Interaction rules and flow control (credit)
1. **The device never starts a handshake.** The host sends `hello`, resending after 2s without an answer, at most 3 times.
2. **Messages the host sends on its own require exactly one reply.** These are the messages without `re`: `hello`, `ping`, `time.set`, `*.snapshot`, and `debug.*` in test builds. The device must answer each one once, with `hello`, `pong`, `ack` or `error`.
3. **The host's credit window.** The total bytes the host has sent on its own and not yet had answered must not exceed **4096B** (half of the device's 8192B receive buffer). If no answer arrives within 5s, that credit is reclaimed; if a snapshot was lost as a result, the `revs` reconciliation in `pong` resends it automatically.
4. **Requests the device sends on its own each have an in-flight limit.**
   - `outbox.item`: at most 4 in flight, answered by `outbox.ack`.
   - `agent.spawn` / `agent.reply`: at most 1 in flight, answered by `agent.result`.
   - `queue.done` / `inbox.dismiss`, and v0.4.1's `rec.list` / `rec.act`: at most 1 in flight shared among them, answered by `ack`, `rec.items` (only for `rec.list`) or `error`.

   Each host reply to these requests is at most 256B (`host_reply_max`), **with the single exception of `rec.items`**, which is limited only by the 2048B line limit (`line_max`) (`protocol/README.md` item 20, `rec_rules.LARGE_HOST_REPLIES`). Because it uses that 1 in-flight slot, in the worst case the data in the device's receive buffer is 4096 + 5×256 + 2048 = 7424B, below 8192 (before v0.4.1 it was 4096 + 6×256).
5. **The device processes at most 4 inbound lines per turn**; at most 5 `error` replies per second.
6. **State pushes are reconciled by rev.** Each of the three snapshots carries its own `rev`. The device reports the `revs` it holds in `pong`, and for any that is behind the host's, the host pushes it again.

### 6.4 Handshake, heartbeat and reboot detection

```
daemon opens the port safely (once the serial is bound, only that one)
H→D hello                     (resent after 2s without an answer, at most 3 times)
D→H hello(re)                 fw must start with cpa-fw/, otherwise the port is released within 6s and retried after 30s
H→D time.set        D→H ack
H→D projects.snapshot / sessions.snapshot / queue.snapshot   D→H ack × 3
D→H outbox.item × ≤4 …        H→D outbox.ack …
every 5s: H→D ping     D→H pong(up_ms, pending, revs)
```

- **Detecting a device reboot**: `boot` changed, or `boot` is unchanged but `up_ms` got smaller. The daemon then sends `time.set` and the three snapshots again.
- **Detecting a disconnect**: either side receives no valid data for 15s.
- **After the first successful handshake**, the daemon writes this device's USB serial number into `device.serial` in `config.json`.

### 6.5 Message catalogue (v0.1)

| `t` | Direction | Mode | Key fields |
|---|---|---|---|
| `hello` | H→D | Host-initiated | `role:"host"`, `app`, `agent` (`ok`/`missing`/`error`), `caps{spawn, reply}` |
| `hello` | D→H | Reply | `role:"device"`, `fw`, `dev`, `boot`, `up_ms`, `rst`, `time` (bool), `pending`, `bat_mv`, `revs{projects, sessions, queue}` |
| `ping` / `pong` | H→D / D→H | Host-initiated / reply | `pong`: `up_ms`, `pending`, `revs` |
| `ack` | Both | Reply | Only `re` |
| `error` | Both | Reply | `re`, `code`, `field?`, `msg?` (short English) |
| `time.set` | H→D | Host-initiated | `epoch_ms`, `tz_min` |
| `projects.snapshot` | H→D | Host-initiated (state push) | `rev`, `projects[≤9]{slot, key, name, run, need, queued, ok}`, `other{run, n}` |
| `sessions.snapshot` | H→D | Host-initiated (state push) | `rev`, `total`, `sessions[≤10]{sid, slot, name, kind, st, need, age_s, short?}` |
| `queue.snapshot` | H→D | Host-initiated (state push) | `rev`, `items[≤12]{qid, slot, text, intent, sid?, uncertain?}`, at most 4 per project; `uncertain` (v0.4.1, docs/20 U25) appears only when true, see protocol/README item 22 |
| `outbox.item` | D→H | Reliable delivery | `id` (MonoId), `kind` (`note`/`queue`), `at{up_ms, wall_ms}`, `body` |
| └ `note` | | | `body{text, project?}` |
| └ `queue` | | | `body{text, project, intent(todo/spawn/feedback), sid?, uncertain?}` |
| `outbox.ack` | H→D | Reply | `re`, `status` (`stored`/`duplicate`/`rejected`), `code?` |
| `agent.spawn` | D→H | Online request | `project`, `text`, `qid?` (with `qid`, the full text saved by the daemon wins) |
| `agent.reply` | D→H | Online request (only with `caps.reply`) | `sid`, `text` |
| `agent.result` | H→D | Reply | `re`, `ok`, `code?`, `name?`, `short?` |
| `queue.done` | D→H | Online request | `qid` |
| `inbox.dismiss` | D→H | Online request | `sid` |
| `debug.key` / `debug.state` / `debug.reboot` | H→D | **Only in test firmware** | Inject a key / return an `AppState` summary and timestamp / soft reboot |

**Field value conventions**:

| Field | Values |
|---|---|
| `kind` | `tty` for interactive, `bg` for background. `daemon` and `daemon-worker` are not sent |
| `st` | `busy`, `shell`, `idle`, `waiting`, `unknown` |
| `need` | Computed by the daemon, rules in §7.6 |
| `sid` | The first 8 hex characters of the session UUID |
| `short` | Only background sessions have it, taken from `agents --json` |
| `age_s` | How many seconds the session had been in its current state when sent; the device adds to it itself when displaying, so a mere change of duration does not increment `rev` |

**The outbox has only two kinds, `note` and `queue`; this is a hard rule.** Execution requests (`agent.*`) cannot appear in the outbox, and contract tests assert that any kind with execution semantics is invalid.

### 6.6 Examples (from `protocol/vectors`)

```jsonl
{"v":0,"t":"hello","id":"h1","role":"host","app":"cpad/0.1.0","agent":"ok","caps":{"spawn":true,"reply":false}}
{"v":0,"t":"hello","id":"d1","re":"h1","role":"device","fw":"cpa-fw/0.1.0","dev":"020000000001","boot":"3QZK1M7-0000C","up_ms":53211,"rst":"poweron","time":false,"pending":3,"bat_mv":4150,"revs":{"projects":0,"sessions":0,"queue":0}}
{"v":0,"t":"time.set","id":"h2","epoch_ms":1792440000000,"tz_min":-420}
{"v":0,"t":"ack","id":"d2","re":"h2"}
{"v":0,"t":"projects.snapshot","id":"h3","rev":42,"projects":[{"slot":1,"key":"cpa","name":"Cardputer 助手","run":1,"need":1,"queued":2,"ok":true},{"slot":2,"key":"blog","name":"博客","run":0,"need":0,"queued":0,"ok":true}],"other":{"run":0,"n":1}}
{"v":0,"t":"ack","id":"d3","re":"h3"}
{"v":0,"t":"sessions.snapshot","id":"h4","rev":17,"total":2,"sessions":[{"sid":"8d3e7a19","slot":1,"name":"架构设计","kind":"tty","st":"waiting","need":true,"age_s":540},{"sid":"5c2e8a14","slot":1,"name":"cpa/chord-tests","kind":"bg","st":"busy","need":false,"age_s":75,"short":"a1b2c3"}]}
{"v":0,"t":"queue.snapshot","id":"h5","rev":9,"items":[{"qid":"q17","slot":1,"text":"add LittleFS fault-injection test","intent":"todo"}]}
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-0003","kind":"note","at":{"up_ms":41000,"wall_ms":null},"body":{"text":"idea: dedupe by sid","project":"cpa"}}
{"v":0,"t":"outbox.ack","id":"h6","re":"3QZK1M7-0000C-0003","status":"stored"}
{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-0004","kind":"queue","at":{"up_ms":98000,"wall_ms":1792440045000},"body":{"text":"write native tests for chord detector","project":"cpa","intent":"spawn"}}
{"v":0,"t":"agent.spawn","id":"d7","project":"cpa","text":"Write native tests for the chord detector"}
{"v":0,"t":"agent.result","id":"h7","re":"d7","ok":true,"name":"cpa/write-native-tests","short":"a1b2c3"}
{"v":0,"t":"agent.reply","id":"d8","sid":"5c2e8a14","text":"looks good, add a test plan"}
{"v":0,"t":"agent.result","id":"h8","re":"d8","ok":false,"code":"E_NOT_REPLYABLE"}
{"v":0,"t":"queue.done","id":"d9","qid":"q17"}
{"v":0,"t":"ack","id":"h9","re":"d9"}
{"v":0,"t":"inbox.dismiss","id":"d10","sid":"8d3e7a19"}
{"v":0,"t":"ack","id":"h10","re":"d10"}
{"v":0,"t":"ping","id":"h11"}
{"v":0,"t":"pong","id":"d11","re":"h11","up_ms":120400,"pending":0,"revs":{"projects":42,"sessions":17,"queue":9}}
{"v":0,"t":"error","id":"d12","re":"h12","code":"E_BAD_FIELD","field":"sessions[3].name"}
```

### 6.7 Limits table (`protocol/limits.json` is the machine-readable version)

| Item | Limit or format |
|---|---|
| A line | ≤2048B (including `\n`) |
| `id` / `re` | ≤24 ASCII characters, only `[A-Za-z0-9-]` |
| Project `key` | `^[a-z0-9][a-z0-9-]{0,11}$` |
| `slot` | 1 to 9, no duplicates |
| Project `name` / session `name` / queue summary `text` | ≤24 / 32 / 80B UTF-8, truncated by the daemon at character boundaries |
| User text (note, queue, spawn, reply) | 1 to 500B, one line. The device can only input characters 0x20 to 0x7E; the daemon replaces control characters with spaces. spawn and reply text, after trimming leading and trailing whitespace, must not start with `-` |
| `sid` / `short` / `qid` | `^[0-9a-f]{8}$` / ≤16 ASCII characters / `^q[0-9]{1,10}$` |
| `epoch_ms` / `tz_min` | ≥1735689600000 / −840 to 840 |
| Snapshot entries | projects ≤9; sessions ≤10; queue ≤12, with ≤4 per project |
| Unanswered host data | ≤4096B |
| Device in-flight requests | outbox ≤4; agent ≤1; queue/inbox/rec (v0.4.1) sharing ≤1 |

### 6.8 Error codes

| code | Meaning |
|---|---|
| `E_PARSE` | Not valid JSON (no reply, only counted) |
| `E_TOO_LARGE` | A line exceeds 2048B |
| `E_VERSION` | Incompatible `v` |
| `E_UNKNOWN_TYPE` | This `t` is unknown |
| `E_BAD_FIELD` | A field is missing, of the wrong type, too long, not valid UTF-8, or starts with `-`; `field` gives the JSON path |
| `E_NOT_READY` | A business message was sent before the handshake |
| `E_STORAGE` | Device storage failed |
| `E_UNKNOWN_PROJECT` / `E_UNKNOWN_SESSION` / `E_UNKNOWN_ITEM` | The project, session or queue item does not exist or cannot be identified uniquely |
| `E_NO_AGENT` | Claude Code cannot be found on the Mac |
| `E_SPAWN` | Opening the session failed or timed out |
| `E_NOT_REPLYABLE` | The target is not an idle background session, or replying is not enabled |
| `E_REPLY_FORKED` | resume made a copy as a new session; replying is disabled for the rest of this run |
| `E_BUSY` | An agent request is already in flight |
| `E_INTERNAL` | Any other error |

`msg` holds only one short English reason. **It never echoes body text, paths, command lines or command output.**

> **v0.2 addition (additions only, `v` stays 0)**: the device's `hello` may carry two optional fields: `store` (`sd` / `int` / `none`, where the outbox lives) and `launcher` (boolean, whether it was started through Launcher). When unset, the encoding is byte-identical to v0.1; a v0.1 daemon ignores them. Also, every line the host sends must start with `{` or be blank, because Launcher's serial console shares this port, and a line starting with a word might be run as a command. Details in docs/09 §6 and protocol/README.

### 6.9 Evolution
- When only **adding** fields or message types, `v` is unchanged, and an older side ignores what it does not understand. When changing semantics or removing fields, `v` goes up by 1.
- Capabilities are negotiated through `caps` and `agent` in the host `hello`.
- Every new message type first gets its schema and test vectors (RED), then both sides are implemented.

### 6.10 Reserved type names (implemented in v0.2 or later; the names are taken)
- `voice.begin`, `voice.chunk`, `voice.end`: chunked audio transfer, base64, with sha256, resumable.
- `approval.request`, `approval.decision`: approvals.
- `log`: test builds only.
- `pair.*`: v0.3 wireless pairing.

---

## 7. Mac daemon design

### 7.1 Process model and folders

**Process model**
- One process, one asyncio event loop. `HubState` has a single writer: `reduce()` returns `(HubState, Effects)`, which the `EffectRunner` then executes. No locks needed.
- Serial reading happens in a separate reader thread, which hands each line to the event loop through `call_soon_threadsafe`.
- Subprocesses start with `asyncio.create_subprocess_exec`, never through a shell, with timeouts.
- Single instance: `fcntl.flock` on `$CPA_HOME/cpad.lock`; the serial port opens with `exclusive=True`. This exclusive lock is only advisory and does not stop other programs, so always run `cpa pause` before flashing.

**Folder** `$CPA_HOME`, by default `~/Library/Application Support/CardputerAssistant`, folder mode 0700, file mode 0600, checked at start:

```
config.json      claude_bin (absolute path), notes_dir, device.serial, poll_s=3, inbox_idle_s=60, spawn_env (allowlist from the login shell environment), experimental_bg_reply=false
projects.json    {"version":1,"projects":[{"key","name","slot","root","spawn_permission_mode"}]}
events.jsonl     append-only, fsync on every write, the only authoritative data
cpad.sock        AF_UNIX control channel, 0600
cpad.lock
logs/cpad.log    rotated at 1MB, 5 kept, redacted
```

### 7.2 Device management (`device/`)
1. **PortFinder**: looks for a `303A:1001` port once per second. If the config has a bound serial number, only that one is accepted; with nothing bound, an unknown port is tried only after a 30s back-off.
2. **SafeOpen**: open with `dsrdtr=True` first, then set `rts=False`, and finally `dtr=False`. A unit test records the sequence of control line changes on a fake Serial and asserts (RTS, DTR) never shows (1, 0).
3. **Handshake**: send `hello`, wait 2s, at most 3 tries; check the `fw` prefix and `v`; if they do not fit, release the port within 6s.
4. **DeviceLink**:
   - the write queue is kept within 4096B by the credit window;
   - replies are matched by `re`;
   - a `ping` every 5s, and no data for 15s means disconnected;
   - when `boot` changes, resync.
5. **Pause**: `cpa pause [--seconds 120]` tells the daemon over the unix socket to close the port and stop reconnecting; it resumes automatically when the time is up.

### 7.3 State, events and idempotence

```python
@dataclass(frozen=True, slots=True)
class HubState:
    link: LinkState                      # connection state, boot, revs_sent, revs reported by the device
    projects: tuple[Project, ...]
    sessions: tuple[Session, ...]        # result of the latest poll, with since
    dismissed: frozenset[tuple[str, str, int]]   # (sid, st, since): once the state changes, this dismissal expires automatically
    queue: tuple[QueueItem, ...]         # folded from events
    revs: Revs

def reduce(state: HubState, ev: HubEvent) -> tuple[HubState, tuple[Effect, ...]]: ...
```

**Events**: each event is one JSON line in `events.jsonl`, with at least `ev`, `at` (the daemon's ISO time) and `dev?`, never modified after writing.

| Event | Meaning |
|---|---|
| `item.received` | An outbox entry was received from the device. Fields: `id, kind, body, at_dev, received_at, orphan?` |
| `queue.added` | A new queue item. Fields: `qid, project, text, intent, sid?, origin(device/cli), from_item?` |
| `queue.done` | A queue item was marked done |
| `queue.spawned` | A queue item was used to open a session |
| `queue.reopened` | A closed queue item (`queue.done` or `queue.spawned`) is reopened and goes back into the queue unchanged, with the same qid. Fields as in `queue.added`: `qid, project, text, intent, sid?, origin, uncertain`, filled in by `cpa queue reopen` after scanning the log for the original item; folding therefore need not remember closed items, at a cost growing linearly with the log (v0.4.1, docs/20 U12) |
| `agent.spawned` / `agent.spawn_failed` | Opening a session succeeded / failed. Records only project, name, short and error code, **never body text**. `agent.spawn_failed` also carries `why` when the Mac knows the reason: `untrusted` (the CLI does not trust this folder yet) or `login` (the CLI is not logged in) (v0.4, #25) |
| `agent.replied` / `agent.reply_failed` | Reply succeeded / failed |
| `device.seen` | A device connected. Fields: `dev, serial, fw` |

**Order of handling an `outbox.item`**:
1. Validate; if invalid, answer `rejected`.
2. Check for duplicates. If duplicate, answer `duplicate` and append no event.
3. Append `item.received`; for `kind=queue`, also append `queue.added`.
4. fsync.
5. Answer `stored`.
6. Side effects: render the note, update the queue snapshot.

**The dedup index** (the set of `(dev, id)`) lives in the shell of `events/`, rebuilt from the log at start; the pure core only receives the result "is it a duplicate".

**When the project has been deleted**: if the entry's project no longer exists, the entry is still stored and marked `orphan`. The note is recorded as "unassigned", keeping the original key. **The user's content is never lost.**

### 7.4 Agent adapter interface (`agents/base.py`)

```python
@dataclass(frozen=True, slots=True)
class AgentCaps: present: bool; version: str; spawn: bool; reply: bool

class AgentAdapter(Protocol):
    name: str
    async def probe(self) -> AgentCaps: ...                                   # once at start, then every 60s
    async def list_sessions(self) -> tuple[SessionInfo, ...]: ...
    async def spawn(self, req: SpawnRequest) -> SpawnResult: ...              # root, name, text, permission_mode
    async def reply(self, req: ReplyRequest) -> ReplyResult: ...              # session, text, permission_mode
```

Adding Codex later only needs these four methods. v0.1 has no adapter registry and no per-project adapter choice.

### 7.5 Claude Code adapter (`agents/claude_code/`)

| Capability | Implementation | Status |
|---|---|---|
| probe | `claude --version` plus `which`. `claude_bin` is an absolute path, because launchd's PATH is short | ✅ 2.1.272 |
| list | Only while the device is Ready, run `claude agents --json` every 3s, with a 10s timeout. Parse leniently: an unknown `status` is recorded as `unknown`, and `kind` `daemon` or `daemon-worker` is skipped. Interactive sessions have no `id`, `state` or `waitingFor` fields | ✅ fields; ⏳ G4 semantics |
| spawn | `argv = [claude_bin, "--bg", "--name", f"{key}/{slug}", "--permission-mode", mode, "--", text]`, `cwd=root`, `env=spawn_env`, 20s timeout. Parse the short id from stdout; if that fails, compare `agents --json` before and after, matching by name | ✅ arguments exist; ⏳ G7, G8, G9 |
| Note | In the CLI's `agents --json`, a background session's kind is actually `background` (not `bg`); the daemon maps it to the protocol's `bg`. Also look at the `state` field: `blocked` counts as waiting for you, and `failed`/`crashed` after opening a session counts as a failure. `--bg` exiting 0 does not mean success: parse the short id with `^backgrounded · ([0-9a-f]{8})`, then check with `agents --json --all` (G7/G8) | ✅ |
| reply | Declared as supported only when `experimental_bg_reply=true` (turned on by you by hand after G3 passes). Steps:<br>1. query again to confirm the target is `bg` and `idle`;<br>2. run `claude stop <short>`;<br>3. run `claude --bg --resume <sessionId> -- <text>` (measured in G3: with `--permission-mode` it opens a copy, so it is left out; the original session's permission mode is kept);<br>4. check with `agents --json --all` that the sessionId is unchanged. If a new id appears, or the output has a copy notice, return `E_REPLY_FORKED` and disable replying for the rest of this run | ⏳ G3 |

**Input guards**:
- `text`, after trimming leading and trailing whitespace, must not start with `-`; otherwise `E_BAD_FIELD`.
- `sid` must map to exactly one UUID in the latest poll result; otherwise `E_UNKNOWN_SESSION`.
- `mode` must be in `{plan, manual, acceptEdits, dontAsk, auto}`.

**Session naming**: take at most the first 4 ASCII words of the text, lowercase, keep only `[a-z0-9-]`, at most 24 long; if nothing can be taken, use `task-<n>`. When the text has Chinese (any full-width or wide character), take the first 8 units instead: an ASCII word counts as one unit, ASCII lowercased, and units originally separated by whitespace or punctuation are joined with a single `-`, at most 24 bytes, cut at a character boundary (v0.4.1, docs/20 U14).

**Logs**: body text in argv is replaced with `<text len=N sha8=…>`; only the first 200 bytes of stderr are recorded, also redacted.

**Merging hooks and polling** (v0.4, #33, `agents/claude_code/hook_state.py`, pure functions): hooks say "why", polling (`agents --json`) says "whether it is still so now".
- **Phase**: each session records the phase from its latest hook. `working`: UserPromptSubmit, PreCompact, PreToolUse / PostToolUse. `needs` (reason permission / plan / question / input / login): PermissionRequest, PreToolUse(AskUserQuestion), Notification's `permission_prompt` / `elicitation_*` / `quota_auto_resume_stale`, login-type StopFailure. `done`: Stop, SessionStart, `idle_prompt`, other StopFailure. `ended`: SessionEnd.
- **Use the hook when they agree**: when the polled state matches the phase, use the hook's conclusion, with its reason and excerpt (`msg`). A login-type wait also matches when polling says `idle`.
- **`seen`**: a hook counts as confirmed once some poll that started after the hook showed a matching state. After confirmation, the next poll that does not match wins directly (the user answered in the terminal, or hooks are not installed).
- **`LAG_S` (10s)**: an unconfirmed hook still overrides a non-matching poll for `LAG_S` after the hook, on the assumption that polling has not caught up. Dialog-type waits (permission / plan / question) get only `NEEDS_GRACE_S` (2s): after an ordinary tool approval there is no hook, and only polling knows the wait has ended, so a poll that started more than 2s after the hook wins as long as it is not `waiting`.
- **`unknown`**: an unrecognised polled state neither confirms nor contradicts. `working` / `done` keep using the hook; `needs` holds for at most `LAG_S`, so it never stays lit.
- **SessionEnd**: the session is removed from the board before polling catches up (at most `LAG_S`).
- **Subagents**: events with an `agent_id` (sent by a subagent) do not change the session's state.
- As soon as a hook arrives, it is merged with the previous list and pushed to the device; the next poll confirms or overturns it. At most 64 sessions are remembered; a session not in the list is forgotten after `LAG_S`. `waiting_for` follows the merged state. Without hooks installed, everything is decided by polling as before.

### 7.6 Projects, inbox and snapshots
- **Project validation**: `root` must be an existing folder, stored as a realpath; **it need not be a git repository**. `key` and `slot` must be unique. `spawn_permission_mode` defaults to `plan`, and `bypassPermissions` is refused at load. The daemon checks the modification time of `projects.json` every 3s and reloads it when it changes.
- **Which project a cwd belongs to**:
  1. take the realpath of cwd and match the project `root` by longest prefix.
  2. If nothing matches, run `git -C <cwd> rev-parse --git-common-dir` and match again. The result is cached for 60s, and the command times out after 2s.
  3. If still nothing matches, it goes to `other`.
- **since**: the first time the daemon sees a `(sid, st)` combination, it records the current time as since; any state change restarts the clock. After a daemon restart, since will be too recent, which is acceptable.
- **Deciding need**: `need` is true when all three conditions hold.
  1. The session belongs to a registered project;
  2. `st == waiting`, or a background session's `state` is `blocked` and it is not running (`st` is not busy / shell, #5), or `st == idle` and `age ≥ inbox_idle_s` (default 60s). This `st` is the state after merging with hooks (see §7.5);
  3. `(sid, st, since)` is not in the dismissed list.
- **Snapshots**:
  - Generated by the pure function `build_snapshots(state) -> (projects, sessions, queue)`.
  - rev goes up by 1 only when the content changes (not counting `age_s`).
  - Order of sessions: first those with `need`, by waiting time from longest to shortest; then busy ones; then the rest. At most 10, with `total` giving the real total.
  - Each serialised snapshot is at most 2048B; beyond that, entries are removed from the end.
  - If a session's `name` starts with its own project's `key/` (every session opened from the device does), the prefix is removed when sending, because the device adds `key/` itself when displaying; all 32 bytes are left for the name itself (v0.4.1, docs/20 U14). If nothing is left after removing it, it is sent as is.

### 7.7 Queue
- The current queue is a pure fold over the event log: `fold_queue(events) -> tuple[QueueItem, ...]`.
- `qid` is assigned monotonically by the daemon, in the form `q<n>`.
- When the device sends an `agent.spawn` with a `qid`:
  1. the full text saved in the queue is used as the prompt;
  2. on success, `queue.spawned` is appended.
- Command line: `cpa queue ls [key]`, `cpa queue add <key> <text>` (`origin=cli`), `cpa queue done <qid>`, `cpa queue reopen <qid>` (v0.4.1, docs/20 U12: undo on the Mac an Opt+D pressed on the wrong row on the device; only accepts qids already closed).
- Queue items saved when the result of opening a session from the device is unknown (timeout or disconnect) carry `uncertain`; `queue.snapshot` sends it unchanged, and the device uses it to warn "may already be started" (v0.4.1, docs/20 U25).
- Not in v0.1: redelivering queued feedback to the original session; generating a queue markdown.

### 7.8 Notes (`notes/`)
- Each `item.received(kind=note)` appends one line to `<notes_dir>/YYYY-MM-DD.md`, in the form `- HH:MM #<key> <text> ^cpa-<monoid, lowercase>`:
  - date and time come from `wall_ms` first; without it from `received_at`, prefixed with `~`.
  - Unassigned notes get no `#key`.
- **Idempotent**: look for the block ID before writing, and skip if it already exists.
- **Catch-up**: at start, notes in the event log not yet written are written.
- **Default folder** `~/CardputerNotes`. This folder is not protected by TCC, so the LaunchAgent can write to it directly.
- Body text is one line, because the device can only input one line and the daemon replaces control characters with spaces. So the block ID always ends up on the same line.

### 7.9 Control channel and command line
- **The control channel** is `cpad.sock` (AF_UNIX, mode 0600). Each request is one line of JSON (at most 1024 bytes), such as `{"cmd":"pause","seconds":120}` or `{"cmd":"status"}`. No TCP port is listened on.
  - `{"cmd":"hook","ev":…,"sid":…}` (v0.4, #33): one Claude Code hook event forwarded by `cpa hook`. Only the allowlisted fields `ev, sid, ntype, tool, detail, sub, msg` are accepted: `ev` must be a string and a known event, `sid` is a full session UUID, `ntype` / `detail` match `[a-z0-9_]{1,32}`, `tool` matches `[A-Za-z0-9_.:-]{1,64}`, `sub` is a boolean, and `msg` is an already-cleaned one-line excerpt (≤120 bytes). The daemon revalidates field by field (`agents/claude_code/hooks.parse`), and anything that fails (including a wrong type, such as `ev` being an array or object) gets `{"ok":false,"error":…}`. Hook content is never executed.
- **Command line**:
  - `cpa daemon`
  - `cpa status`
  - `cpa project add|ls|rm`
  - `cpa queue ls|add|done|reopen`
  - `cpa pause [--seconds N]`
  - `cpa service install|uninstall`
  - `cpa device forget`
  - `cpa hook [EVENT] [--home DIR]`: Claude Code calls it for each hook event. It reads at most 1 MiB from stdin, forwards only allowlisted fields, and gives up if it cannot send within 0.5s; it always exits 0 and never prints (exit 2 would block the prompt or tool, and stdout would be taken by Claude as context)
  - `cpa hooks print|install|uninstall [--settings FILE] [--cpa PATH] [--yes]`: by default only shows the changes; only with `--yes` does it write `~/.claude/settings.json` (or the file given by `--settings`). Handlers use the exec form (`command` is cpa's absolute path, `args` start with `hook`, no shell); uninstall removes only such handlers. Before writing, the old file is backed up to `$CPA_HOME/hook-backups/` (folder 0700, files 0600), not next to the settings file: a project's `settings.local.json` is git-ignored only by its exact file name, so a copy beside it would be committed
- **LaunchAgent**:
  - The plist is generated with `plistlib` and placed in `~/Library/LaunchAgents/`, with `RunAtLoad` and `KeepAlive` set.
  - At install, `zsh -lic env` runs once, and only `PATH`, `HOME`, `LANG`, `LC_*`, `SHELL` and `USER` are kept and written into `spawn_env`. None of these is sensitive.
- **Logs**:
  - One JSON object per line.
  - `RedactFilter` replaces body fields (`text`, `body`, `prompt`) with the length plus the first 8 characters of the sha256.
  - The original text is logged only when `CPA_LOG_CONTENT=1` is set, and then a warning is printed at start.

---

## 8. Security by phase

| Phase | Trust boundary | Measures |
|---|---|---|
| **v0.1: USB only** | Physical contact, plus the macOS user currently logged in | • No keys on the device; no pairing (pairing without keys is only for show)<br>• The daemon checks the firmware prefix, binds the USB serial number, and lets go of any other board within 6s<br>• No TCP port; control goes only through a unix socket with mode 0600<br>• Execution happens only through online requests plus confirmation; the outbox can never hold an execution entry<br>• argv never goes through a shell, body text goes after `--`, text starting with `-` is refused, `sid` is validated<br>• `--permission-mode` is passed explicitly, `plan` by default, `bypassPermissions` refused, and `auto` must be set explicitly by the project<br>• Both sides validate input; error messages carry only error codes; logs are redacted<br>• `$CPA_HOME` folder 0700, files 0600 |
| Known risks of v0.1 | Entries in LittleFS not yet synced are plaintext | Deleted on ack; the status bar shows how many are waiting to sync; written in SECURITY.md |
| v0.2: hooks, approvals, SD card | Same | • Hooks use a command forwarding script plus a header file with mode 0600; if HTTP is needed, bind only 127.0.0.1, check Host and token, and refuse any request with an Origin header. No env interpolation in native http hooks, because credential variables are not interpolated<br>• Approvals use risk-separated keys with a 400ms guard against accidental presses; no decision while the device is absent; an audit log is written<br>• Records on the SD card carry checksums, and each file is sealed separately with the Mac's public key<br>• Queue items written through MCP record their origin; without confirmation on the Mac they cannot be used to open a session<br>• Optional PIN lock |
| v0.3: wireless | WiFi / BLE | • First pairing only over USB: a key is stored in NVS, and `hello` does an HMAC challenge<br>• The link is encrypted with Noise (following ESPHome's approach)<br>• No opening sessions, replying or approving over an unencrypted link |
| Never | — | Flash encryption or Secure Boot on by default; exposing any interface to the public internet |

---

## 9. Test strategy

### 9.1 Test pyramid

| Layer | Command | What it tests |
|---|---|---|
| Firmware unit tests (pure core) | `pio test -e native -f 'core/*'` | LineFramer and Codec with the test vectors; LinkSession with the flows; MonoId; OutboxCore (**crash injected at every storage step**, with 0 lost and 0 duplicates); GhostFilter; ChordDetector (including the 500ms G0 boundary); the static keymap check; slice reducers; `sizeof(AppState)`; whether `limits.h` and `limits.json` agree |
| Firmware unit tests (UI) | `pio test -e native -f 'ui/*'` | The shell's key dispatch order; Compose (byte limit, `-` prefix, switching kind and target); Confirm's 400ms guard and its execute and queue branches; saving as a queue item when the agent result is unknown; the logic of the three modules; asserting key text on screen with FakeCanvas |
| Firmware device tests | `pio test -e cardputer-adv-test -f 'device/*'` | LittleFS write, rename, reopen; the NVS boot count; 200 saves with p95 ≤100ms; an 8KB RX burst with no bytes lost |
| Daemon unit tests | `uv run pytest tests/unit` | codec and validators; event folding and queue folding; need decisions (debounce, dismiss); project validation and cwd resolution; parsing `agents --json` with redacted real output; argv building (malicious text); slug; note format and idempotence; redaction; SafeOpen's control line sequence; the credit window; snapshot limits and 2048B truncation |
| Daemon integration tests | `uv run pytest tests/integration` | A pty pair with `FakeDevice`: it holds back the first reply until the host's next write, simulating TX held back; unread bytes beyond 8192B are dropped.<br>`fake-claude` on the PATH: records argv, cwd and env, prints per fixture, and can flip session states.<br>Also tested: pause and status over the unix socket; "offline intents never execute"; fsync before ack; replay dedup; releasing the port on encountering another board |
| Contract tests | `uv run pytest tests/contract` plus the firmware's `core/test_codec_vectors` | Both sides read the same `protocol/vectors` and decoding results are compared deeply by meaning; `flows` behave the same on both sides; `check_vectors.py` validates every case with jsonschema |
| End to end (HIL) | `uv run --project daemon python tools/hil/run_e2e.py` | Real device plus real daemon plus `fake-claude`; scenarios in §9.3 |

**Coverage**:
- Firmware: `lib/` and `modules/`, excluding `hal/` and `usb_transport/`, line coverage ≥80%, measured with gcovr.
- Daemon: ≥80%, measured with pytest-cov.
- The HAL is not counted in coverage; HIL covers it.

### 9.2 Key tests written first (RED)
- **Outbox crash injection**: `FakeStore` can "lose power" after operation k, with k running through every step of open, write, close, rename and remove. Assert: no entry that showed "Saved" is ever lost, and none is sent twice.
- **Offline intents**: assert that `kind` in the outbox can only be `note` or `queue`. Pressing Confirm in Attached or Detached always produces a `queue` entry. When the daemon receives `queue(intent=spawn)`, fake-claude's call count is 0.
- **Malicious text**: `--dangerously-skip-permissions`, `-c`, `--permission-mode=bypassPermissions`, `$(rm -rf ~)`, `; ls`, `\u0000`, a length of 501B. Each is either refused or arrives at fake-claude as one unchanged argv element after `--`.
- **Permission mode**: when the project sets no mode, argv has `--permission-mode plan`. With `bypassPermissions` in `projects.json`, loading fails.
- **RX overflow**: sending 12KB without credit makes FakeDevice drop bytes, and the link must recover through rev reconciliation. With credit, 0 lost.
- **Stack and size**: `static_assert(sizeof(AppState) <= 4096)`.

### 9.3 HIL scenarios (test firmware `-DCPA_TEST_HOOKS=1`)
1. Handshake ≤3s; `up_ms` continues after reopening the port, showing there was no reset.
2. Quick note round trip: inject `G0`, text, `Enter`; the note appears in `notes_dir` within 2s.
3. Offline catch-up: `cpa pause` first, inject 3 notes, the device shows 3 pending; after resuming, exactly 3 notes.
4. Dropped ack: turn on the daemon's test switch and drop the first ack; in the end there is still only one note.
5. Reboot: send `debug.reboot`; the daemon detects the new boot and sends the snapshots again.
6. Board: fake-claude returns 2 sessions for project A; confirm the counts on the device with `debug.state`.
7. Opening a session: inject Opt+S, text, Enter, wait at least 400ms, then press Enter. The argv fake-claude receives contains, in order, `--bg`, `--name cpa/…`, `--permission-mode plan`, `--` and the text, and cwd is the project root.
8. Opening a session offline: the same flow in the paused state results in a queue item, and fake-claude's call count is 0.
9. Latency: fake-claude flips states 100 times; device updates have p95 ≤5s; G0 opening Compose has p95 ≤300ms.
10. Chinese: render with real session names from this Mac, and record missing glyphs.
11. 1-hour soak: a ping and snapshot every second, plus a note every minute, with 0 lost and no TX stall.

**Manual checklist** (`docs/07-hil-checklist.md`):
- G1's CER test;
- 10 power losses (switch to OFF and pull the cable);
- unplug and replug USB;
- backlight off and wake on battery;
- the G0 long-press hint;
- plug in another ESP32-S3 board and confirm the daemon lets go;
- a smoke test with a real `claude` in a temporary project;
- the README's 15-minute run (in a brand-new macOS user).

### 9.4 CI (GitHub Actions; HIL is not in CI)
1. `python3 tools/repo/check_sizes.py`, `uvx lizard -L 50 -w`, `ruff`
2. `uv run --with jsonschema python protocol/tools/check_vectors.py`
3. `pio test -e native` plus gcovr, requiring ≥80%
4. `pio run -e cardputer-adv` plus `check_budget.py`, and confirm the release binary does not contain `debug.key`
5. `cd daemon && uv run pytest --cov --cov-fail-under=80`
6. (from WP4) clang-tidy `readability-function-size`

---

## 10. Implementation order and work packages

```
WP1 repo skeleton + protocol contract ──┬──► WP2 firmware pure core ──► WP4 firmware device layer + UI ──┐
(including the Day-0 gates)             └──► WP3 daemon ──────────────────────────────────────────┴──► WP5 E2E/HIL + release docs
```

- **Parallel rule**: WP2 and WP3 can run in parallel, and WP4 can also run in parallel with WP3. Work packages running in parallel own folders that do not overlap (see the marks in §3).
- **Hand-offs**: WP2 first puts stub code in `src/`, which WP4 rewrites after taking over. WP5 takes over the README's "Quick start" section.
- **Mapping to milestones**:

| Milestone | Work packages |
|---|---|
| M0 | WP1 plus the Day-0 gates |
| M1 | WP2 and WP3, plus WP4's input, Compose and outbox |
| M2 | WP4's board and inbox |
| M3 | WP3's opening sessions and replying, plus WP4's confirmation box |
| M4 | WP5 |

---

## 11. Risks and items to verify

| # | Risk | Fallback |
|---|---|---|
| R1 | Ghost keys and lock-ups (G1) | Two-key chords only; GhostFilter; watchdog; ADAPT the TCA8418 driver in, draining the FIFO every round |
| R2 | Neither toolchain is ideal in G2 | Use A; postpone voice until v0.3 writes its own `i2s_std` driver |
| R3 | Fields of `agents --json` or `--bg` change | Lenient parsing; one fixture saved per CLI version; capability detection at start |
| R4 | G3 fails | Drop F7; all feedback goes into the queue |
| R5 | Sessions opened under the LaunchAgent have the wrong environment, or the project is not trusted yet (G9) | The `spawn_env` allowlist; require the project to have been trusted in a terminal first; self-check in `cpa status` |
| R6 | `pio monitor` or esptool grabs the serial data, or even resets the device | The pre-upload script calls `cpa pause` automatically; resync through boot change detection |
| R7 | Stalls during internal Flash erase/write (tens to hundreds of milliseconds) | Accept against p95 targets; keep entries small; no Flash writes during USB bursts |
| R8 | After toolchain A switches to `-std=gnu++17`, the M5 libraries do not compile | Verify during G2; if it fails, restrict domain code to C++14 |
| R9 | The inbox is too noisy | 60s debounce; manual dismiss; "other" stays out of the inbox |
| R10 | The scope is too large for one person | A batch of features already cut after review; proceed work package by work package |

---

## 12. Questions for you to decide

The full list with defaults is in 04 §16. The ones directly related to the architecture:
- The default permission mode `plan`: must be confirmed before M3.
- The exception boundary of the immutability rule.
- The notes folder.
- Naming.
- Whether sessions open in the project's main folder or in a worktree.

Every decision is written as `docs/adr/NNNN-*.md`.

## 13. Review handling (the architecture-related part)

The item-by-item record is in 04 §17. These three are trade-offs made specifically at the architecture level:
- The "merge three snapshots into one" proposal was not adopted, because 9 projects plus 10 sessions would exceed 2048B. They are now split into three, `projects.snapshot`, `sessions.snapshot` and `queue.snapshot`, each with its own `rev`, and each answered with `ack`.
- "Every message the host sends on its own must be answered" is a uniform rule new in this draft, so that the credit window can be accounted for exactly.
- `device.reboot` was removed; test firmware uses `debug.reboot` instead. `time.set` keeps `tz_min`.
