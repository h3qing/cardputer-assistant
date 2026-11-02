**English** · [中文](README.zh-CN.md)

# cpa-sim: the firmware on the Mac

There is one Cardputer, so device tests run one at a time, and a new screen cannot be seen
until it is flashed (gh #32). `cpa-sim` builds the firmware's pure runtime for the host: the
same `Runtime`, shell, modules, codec, outbox and recording core as `cpa-fw`, drawn with the
device's font and colours. It does two things:

- `cpa-sim shots` plays a script and saves screenshots (below).
- `cpa-sim serve` is a virtual device on a pty: the real cpad, and the HIL scenarios of
  `tools/hil`, run against it with no Cardputer attached ([The virtual device](#the-virtual-device-serve)).

What is built, unchanged, from `firmware/app`:

- `lib/*` except `hal` and `usb_transport`
- `modules/*`
- `src/module_list.cpp` (the device's module list)
- the native test rig's fakes for the ports: `FakeTransport`, `FakePlatform`, `FakeStore`
  and `FakeRecPort`, which runs the real `RecEngine` on an in-memory card. The simulator's
  `SimRecPort` extends it with real SHA-256 and the firmware's version in the manifests,
  because cpad checks both on the segments it pulls.

`firmware/app` sources are not edited. The simulator has its own code in this directory:

| Library | What it does |
|---|---|
| `lib/simfb` | `Frame` (the 240 x 135 palette sprite: one palette entry per pixel), `FbCanvas` (the `Canvas` of `hal/m5_display`: the theme's `RasterCanvas` plus efont text), `SimDisplay`, and the efontCN_12 decoder ported from M5GFX |
| `lib/simpng` | an 8-bit indexed PNG writer (standard library only), the contact sheet, and the files of `shots` and `serve` |
| `lib/simscript` | the script parser, the key-press timeline, and `live.h` (serve's stdin) |
| `lib/simdev` | `SimDevice`, the only file that wires the `Runtime` (and reboots it), `SimRecPort`, and `play()` |
| `lib/simio` | `PtyTransport` (serve's USB link on a pty, with the device's TX ring and line queue), the WAV microphone, and SHA-256 |
| `lib/efont_cn12` | the efontCN_12 array, copied out of M5GFX at build time by `gen_font.py` |
| `lib/app_modules` | compiles `../app/src/module_list.cpp` |
| `oracle/` | opt-in: M5GFX itself on the host, drawing the same cases as `FbCanvas` ([below](#the-m5gfx-oracle)) |

## Commands

You need PlatformIO 6.2, and M5GFX once (the font comes from it):

```sh
pio pkg install -d firmware/app -e cardputer-adv   # one time; or set CPA_M5GFX_DIR
```

`gen_font.py` checks that the M5GFX it finds is the version `firmware/app/platformio.ini`
pins (0.2.33), and that its `lgfx_efont_cn.c` has the sha256 pinned for that version. If
either differs, the build stops. The generated font is rebuilt when that hash or
`gen_font.py` itself changes.

`pio pkg install` also leaves M5GFX 0.2.34 and M5Unified 0.2.27 in
`firmware/app/.pio/libdeps/cardputer-adv`, next to `M5GFX@0.2.33` and `M5Unified@0.2.26`:
M5Cardputer asks for `"*"` of both. That is expected and harmless. The device build's
dependency graph still picks the pinned versions, and `gen_font.py` takes the pinned one.

```sh
pio test -d firmware/sim                     # the simulator's tests (about 20 s)
pio run -d firmware/sim                      # builds firmware/sim/.pio/build/sim/cpa-sim
cd firmware/sim
.pio/build/sim/cpa-sim shots scripts/v03_tour.cpas --out out/v03_tour
```

`shots` writes the following to `--out` (default `out`, which git ignores):

- `NNN-<name>.png` for every `shot` step
- `contact.png`, all the shots in a grid with their captions
- `shots.json`, which gives each shot's name, caption, device time, backlight level, the
  module in front, how many □ are in the frame (`missing_glyphs`), `settled`, and
  `pushes`: what reached the panel since the shot before (`full` frames, `partial` pushes
  and their pixels, `partial_px`; docs/19 §7.5)

Other options:

- `--scale N` (1 to 8, default 2) repeats every pixel N x N times. It does not smooth, so
  the pixels stay exact.
- `--columns N` (default 4) sets how many shots go in each row of the contact sheet.
- `--lang zh|en` (docs/21) boots every `boot` step in that language, whatever the script
  says, so any script renders in English too: `cpa-sim shots scripts/v04_tour.cpas --lang en`.

A shot first waits, at most 500 ms, until the host's lines are read, the screen is
redrawn and a sliding Tab sheet has come to rest. One taken before that is still saved,
with `"settled": false` in `shots.json` and a warning on stderr.

The tour plays in about 0.3 s.

Exit status:

| Status | Meaning |
|---|---|
| 0 | Done. |
| 1 | The run failed, for example a `reply` with nothing to answer. |
| 2 | Usage or script error. The message names the file and line. |

## Scripts (`.cpas`)

A script has one step per line. `#` starts a comment, at the start of a line or after a
space or tab, outside `"` strings. That holds inside `type` text too: `type a #b` types
`a`, and an odd number of `"` before a `#` keeps it from starting a comment. To type
` #`, press it: `press space shift+3`. A script only presses keys and sends protocol
messages, so pages added to the firmware later need no change to the simulator. The v0.4.0 home, Tab menu and lock are
reached the same way, for example `press tab` or `press opt+l`.

```
include common/demo_state.cpas        # relative to this file
boot store=sd usb=on bat=4100         # also store=internal | none; a new boot is a new device
boot lang=zh                          # the language NVS holds (default en since v0.5.2, docs/21)
host projects.snapshot {"rev":3,...}  # sends {"v":0,"t":"projects.snapshot","id":"hN",...}
reply outbox.ack outbox.item {"status":"stored"}   # "re" = id of the device's last outbox.item
press opt+s enter ; esc ms=80         # each key combo held 80 ms (default 50), then 30 ms up
hold g0 600                           # one long press
type call the vet re: Bingo's shots   # ASCII; Shift is added for shifted characters
down g0                               # held until `up g0` (`up` alone releases everything)
wait 600
mic 3200 2500                         # 3.2 s of a 440 Hz tone at RMS 2500, while recording
mic 3200 speech                       # a voice: syllables rising and falling, short pauses
usb off                               # pull the cable; plugging back in needs a new hello
bat 3350                              # battery in mV (sampled once a second)
shot memo-recording  Memo: recording  # file name, then the caption
frames memo 4 200 mic=speech Memo     # 4 shots 200 ms apart: memo-1 .. memo-4 (the animation)
```

`pull` (docs/28 S1a) is the simulated daemon taking what it is shown: `file.list`, then
`file.del` of each item with its size and sha256, page by page, as cpad does once it has
stored a segment (the bytes themselves are not fetched). With the S1a firmware a deleted
segment is kept on the card, and a Mac never sees what was taken back from it.

`frames <name> <n> <ms> [mic=level|mic=speech] [caption]` (v0.5 S3) takes the first shot
once the screen is drawn and the others every `ms` as they come, without waiting for
anything, so it catches what moves (docs/19 §7): Mando's poses (200 ms), the waveform,
the breathing ●, the lock card's z's (600 ms), the Tab sheet's slide (50 ms). With `mic=`
the microphone runs through the frames, as `mic` would. `scripts/theme_motion.cpas` has
frames of every animation, and of the dimmed backlight, where nothing moves.

A key combo is key names joined by `+`, for example `opt+s`, `` fn+` ``, `shift+a`, `enter`,
`space`, `bksp`, `tab` or `g0`. These are the `debug.key` names in
`firmware/app/lib/protocol/key_names.cpp`. There are also these aliases:

- `up`, `down`, `left` and `right` are the `;` `.` `,` `/` keys, which carry the arrow legends.
- `esc` is `` fn+` ``.

Time only moves in 10 ms loops:

- A `press` follows `debug.key` timing.
- A `shot` first runs the loop until the host's lines are read and the screen is redrawn
  (at most 500 ms).
- After `host hello`, the simulated daemon sends `ping` every 5 s, as cpad does, so the
  link stays up through long waits.
- `host debug.reboot` restarts the device 200 ms after its ack, as on the device: boot 2,
  reset reason `software`, the card and the outbox kept. The pings stop; send `host hello`
  again.

If a script does not start with `boot`, it boots with the defaults.

`scripts/v03_tour.cpas` goes through every v0.3 screen:

- the board, with two projects and three sessions
- a project page, with its sessions and queue tabs
- the capture overlay
- a new session: compose, confirm and started
- the inbox and the system page
- a memo and a meeting: consent, page, star, end
- the status bar states: no daemon, online, syncing, offline, recording and low battery
- the refusals: low battery and no SD card

`scripts/common/demo_state.cpas` holds the demo board and is shared by other scripts.

`scripts/ux/` holds the regression scripts of the interaction review (docs/20 §4.4), each
with captions that say what the screen must show: `unknown_slot` (U13: a project slot
that does not exist), `queue_cursor` (U12: the cursor keeps its item through a snapshot),
`toast_order` (U20: the user's own result first), `memo_cap` (U15: the memo cap screen
waits for G0 to go up), `memo_cap_battery` (the same on battery: the held screen stays lit),
`locked_backlog` (U20 after U6: results that ended while locked never delay the next one)
and `unknown_spawn` (U25: a queue item saved after an Opt+S whose result never came back
says "可能已开过" before it is started again).

The decisions D1–D10 and the B package (v0.4.1, docs/20 §5.6) added `home`, `home_letters`
(D1), `autolock` (D2), `lock_card`, `lock_gestures` (U3, D7), `meeting_boxes` (D3),
`g0_hold_overlays` (D4), `editor_back` (D5, D6), `menu_chords` (D9), `rec_pages`,
`status_bar`, `inbox_board` and `copy_layout` (B2–B6). The review after them added
`lock_after_hold` (no old result after a swallowed hold; a pocket hold goes dark),
`confirm_menu` (the Tab menu says what the box says), `lock_need_chip` (no "等你 0" without
data), `box_hint` (a box's hint goes with it), `end_box_menu` (a box waits under the menu),
`meeting_stop_unlocked` (an unlocked stop on battery is shown) and `ready_refused` (no
"● 准备" for a hold that is refused). gh #51 (docs/20 U26, merged into v0.4.1) added `memo_grace` (a G0 that
slips keeps the memo: the 0.6 s grace) and `memo_handsfree` (the double tap: hands free, one
press ends it; locked, its end on the lit card).
docs/21 (设置 and English, v0.4.3) added `settings_zh` (Opt+M opens 设置 on the WiFi row;
the WiFi page; the device tab; the link row keeps the Mac's name once pulled) and
`settings_en` (Opt+M . Enter: English at once, kept across `debug.reboot`, and back).

docs/28 S1a (kept copies, two Macs told apart by their hello's `host_id`) added
`scripts/kept/`: `plug_hint` (two memos recorded offline are held for 5 s at the work Mac;
Tab, Enter keeps them from it, the home Mac takes them), `plug_hint_ignored` (left alone, the
work Mac takes it after 5 s), `deliver_again` (设置 → 存储 → 再交一次, whose box opens on
取消: the work Mac never lists them again, the home Mac does) and `clear_kept` (the lock
card's 留N; 清掉留底's box opens on 取消). Run each with `--lang en` too.

## The virtual device (`serve`)

`serve` runs the device in real time, one loop every 10 ms, with a pty as its USB port. cpad,
the HIL harness or a terminal open the pty as they would the Cardputer's port.

```sh
pio run -d firmware/sim
firmware/sim/.pio/build/sim/cpa-sim serve [--store sd|internal|none] [--usb on|off] [--bat MV] \
    [--mic silence|FILE.wav] [--shots DIR] [--scale N]
```

Its first line is `pty /dev/ttysNNN`. It stops on Ctrl-C, SIGTERM, `quit`, or at the end of
stdin, so it never outlives the tool that started it (to run it with no driver, keep stdin
open: `tail -f /dev/null | cpa-sim serve`). It is the HIL build (`CPA_TEST_HOOKS`), so
`debug.key`, `debug.state` and `debug.reboot` work.

- **The USB link** behaves like `UsbTransport`: a line is written only when all of it fits
  in the 4 KB TX ring, else it waits in the 4-line queue with the same drop rule. Input
  waits in an 8 KB RX queue. As in the device's HWCDC queue, bytes that arrive while it is
  full are lost, not pushed back on the host, and `serve` prints `rx_dropped <total>`.
  - Bytes leave only while a host has the port open in raw mode. pyserial sets raw mode
    when it opens the port. Until then, lines wait in the ring, as they would on the device
    while the host is not reading. This also keeps macOS from echoing them back: a newly
    opened slave starts with echo on.
  - A pty has no cable: `usb off` on stdin (or `--usb off`) unplugs it, and nothing moves
    either way until `usb on`.
- **The card and the outbox** are in memory: `--store sd` (default, an empty 64 GB card),
  `internal` or `none`. Storage is planned by the firmware's own `plan_storage`, from the
  facts of a full flash of `partitions/cpa_8MB.csv`:
  - `sd`: the card, with cpafs mounted, so the boot migration runs (from an empty cpafs).
    The system page shows `存储 SD 59G`.
  - `internal`: cpafs (1.5 MB) and no card, so the system page warns `SD：未检测到卡`.
  - `none`: neither.

  They survive `debug.reboot`, which restarts the runtime 200 ms after its ack with the
  next boot number and reset reason `software`. They do not survive the process.
- **The microphone:** while recording, a 4 KB block every 128 ms, of silence or of the WAV
  file played in a loop. Only 16 kHz mono 16-bit PCM is accepted; convert other files with
  `afconvert -f WAVE -d LEI16@16000 -c 1 in.m4a out.wav`.

stdin takes the steps of a script, one per line, run in order: `press`, `hold`, `type`,
`down`, `up`, `wait`, `usb`, `bat` and `shot`, and `quit`. A step starts once the keys before
it are played; a `shot` waits for the screen to be redrawn. `boot`, `host`, `reply`, `mic`
and `include` only work in a script, because on `serve` the host is cpad.

stdout has one line per event:

| Line | When |
|---|---|
| `pty <path>` | first: the device's port |
| `shot <file> <module> <backlight>` | a `shot` was saved, as `--shots DIR/NNN-<name>.png` (default `out/serve`) |
| `usb on`, `usb off`, `bat <mv>` | the step took effect |
| `reboot <boot>` | the device restarted after a `debug.reboot` |
| `rx_dropped <bytes>` | the host overran the 8 KB RX queue; the total lost so far |
| `error <message>` | a stdin line was refused |
| `bye` | last |

### cpad on the virtual device

`tools/sim/cpad_sim.py` runs the real cpad in the foreground, given only the pty:

```sh
CPA_HOME=$(mktemp -d) uv run --project daemon python tools/sim/cpad_sim.py --port /dev/ttysNNN
CPA_HOME=<the same dir> uv run --project daemon cpa status   # link: ready  dev=100000000051
```

- It refuses to run unless `CPA_HOME` is a directory inside the system's temporary
  directory, and the notes directory must be temporary too.
- Without a `config.json`, it writes one with `notes_dir=$CPA_HOME/notes` and fake-claude
  (`daemon/tests/fixtures/bin/fake-claude`, its state in `$CPA_HOME/fake-claude/state.json`).
- cpad's port lister reports only the pty, as a Cardputer with serial `SIM-100000000051`, so
  it never opens a real board. Your own cpad never lists a pty, so the two never meet.
- pyserial cannot set RTS / DTR on a pty, so cpad's SafeOpen would fail. `PtySerial` makes
  those two calls do nothing; everything else is pyserial's own port.

### HIL scenarios on the virtual device

`tools/sim/run_hil_sim.py` starts `serve` and runs `tools/hil`'s own runner against it, with
the harness's temporary `CPA_HOME`, notes and fake-claude. `tools/hil` is not changed: the
port lister, the tap's serial class and the port-holder check are replaced while it runs.

```sh
uv run --project daemon python tools/sim/run_hil_sim.py --scenarios handshake,capture,board \
    --report "$TMPDIR/hil-sim.json"
uv run --project daemon python tools/sim/run_hil_sim.py --scenarios rec_nosd --sim-args "--store none"
```

These passed on the sim, every check (24 scenarios; the 14 in the first row ran in one run of
about 6 minutes):

| Scenarios | Notes |
|---|---|
| `handshake`, `capture`, `offline_replay`, `dup_ack`, `reboot`, `board`, `spawn`, `offline_spawn`, `cjk`, `back_keys`, `rec_memo`, `rec_meeting`, `rec_resume`, `rec_live_hold` | the default sim |
| `sd_empty`, `sd_store`, `rec_reboot` | `sd_empty` needs a fresh sim: run it first |
| `rec_nosd` | alone, with `--sim-args "--store none"` |
| `nav`, `menu`, `lock` | v0.4.0; also with `--sim-args "--store none"`, which takes their no-card branches |
| `rec_memo_grace`, `rec_memo_handsfree` | gh #51: the memo's G0 grace and the hands-free double tap |
| `latency` | runs, but the numbers are the simulator's, not the device's |

Run `handshake` before any scenario that reboots the device (`reboot`, `sd_store`,
`rec_reboot`). On the sim the pty stays open through a reboot, so the harness records no
new handshake, and `handshake` then compares the run's first hello with a later boot's
(`same_boot_after_reopen` fails). This happened before the review fixes too.

`run_hil_sim.py` refuses the others:

- `mic_cal` and `mic_verify` pause the real cpad and play through the Mac's speakers.
- `rec_cold_start` (v0.3.2) too, with mic_cal's preflight. It also needs a codec that
  `debug.codec` can make cold: the sim's audio task starts warm, and its mic is `--mic`, not the
  speakers.
- `launcher_boot` and `migrate_*` need an installed or flashed image.
- `real_claude_smoke` starts a real Claude Code session.
- `sd_bulk`, `rec_rotation`, `rec_soak` and `soak` have not been tried on the sim.

### Tests

```sh
pio test -d firmware/sim                         # includes the pty, TX ring, WAV and stdin tests
pio run -d firmware/sim                          # the tests below start .pio/build/sim/cpa-sim
uv run --project daemon pytest tools/sim -q
```

`test/test_fuzz_runtime` (docs/20 §4.5) runs the firmware's `Runtime` against a scripted
daemon and a random user, two seeds of three simulated minutes each (about a second), and
checks that home is at most 6 presses away from any state, nothing is stuck, every capture
reaches the host, every pulled segment's SHA-256 matches and every request is answered. It
also loses one end event of the audio task (docs/20 S1) and checks the device records
again within 10 s, and sends one late, behind the next memo's Start, which must not end
that memo (the events carry their recording's gen). A longer matrix:
`CPA_FUZZ_SEED=100 CPA_FUZZ_SEEDS=12 CPA_FUZZ_MINUTES=15 CPA_FUZZ_STORE=sd|internal|none pio test -d firmware/sim -f test_fuzz_runtime`
(`fuzz-nightly.yml` runs it every night).

The tests that start `cpa-sim` are skipped when it is not built, and fail with
`CPA_SIM_REQUIRED=1` (CI sets it). They fail when it is older than any of its sources in
`firmware/sim` or `firmware/app`, so they never pass on a stale binary: run `pio run -d
firmware/sim` again (it relinks every time, so its date is current). A binary given with
`CPA_SIM_BIN` is only checked to exist.

`tools/sim/tests/test_e2e_cpad.py` is the end-to-end check, about 4 s and no Cardputer:

1. It starts `serve` and runs the real cpad on it through the HIL rig. The handshake reaches
   Ready and binds the sim's serial number.
2. A note typed on the sim's keyboard (G0, the text, Enter, from stdin) reaches cpad: it is
   stored (`item.received`), written to the notes file and acked, and the device's outbox
   empties.
3. fake-claude's sessions reach the board (`debug.state`), and the board is saved as a PNG
   and checked.

`CPA_SIM_SHOTS=DIR` keeps that PNG. `CPA_SIM_BIN` points the tools at another build.

## How close it is to the device

**The same as the device:**

- The pixels of the frame the shell draws. Every screen is rendered by the firmware's own
  code.
- `FbCanvas` mirrors `M5Display::walk`:
  - It splits text into runs and measures each run with M5GFX's `text_width`.
  - Code points the font lacks are drawn as □: below U+0020, above U+FFFF, or not in the
    font.
  - Background cells are filled when fg ≠ bg.
  - Clipping is the same.
- The glyphs are efontCN_12's own bitmaps, decoded by a port of M5GFX's U8g2 decoder.
  [The M5GFX oracle](#the-m5gfx-oracle) checks it against M5GFX itself, and the efont
  tests hold what M5GFX drew. efontCN_12 is biwidth: □ ● … ← are 6 px wide. The native
  tests' `FakeCanvas` assumes 12 px for them, so widths only come out right here.
- Colours are what the palette sprite stores (docs/19 §3.6):
  - Each pixel is an entry of the theme's 256-colour palette (`firmware/app/lib/theme`),
    the same table the device loads with `createPalette`.
  - Shapes (fills, rounded cards with their baked edge colour, the status bar's head,
    the dimmed page under the Tab menu, Mando's sprites, the big ● and Nunito digits,
    the lock card's glow) are drawn by the device's own `theme::RasterCanvas`, so they
    are byte for byte the device's.
  - The PNG palette is each entry's RGB565, as `pushSprite` sends it: Bg #120e0b shows as
    (16, 12, 8). The PNGs are 8-bit indexed with the palette's own indices.
  - Unused entries and the four sentinels (0x00, 0x10, 0xE0, 0xFF) are magenta: a
    stray v0.4 RGB565 constant would show. `test_theme_sim` checks the whole v0.4 tour,
    `scripts/theme_states.cpas` (one shot per Mando state, docs/19 §7.2) and
    `scripts/theme_motion.cpas` (the animation frames) for them, for edges drawn without
    their own colour, and for ● while recording.
- Motion (docs/19 §3.4-§3.6, S3): between full frames the runtime pushes only what an
  animation tick or a slide frame redrew (`Display::present(Rect)`), and the big ●
  breathes by changing the RecDot and RecDotEdge palette entries (`Display::breathe`).
  `SimDisplay` copies only the pushed rectangle to the panel and, like the device's panel,
  keeps the colour a pixel had when it was pushed: those two entries go to the panel as
  the breath step's own entries (`theme::breath_dot` / `breath_edge`), so one palette
  shows every shot. `test_theme_sim` checks, loop by loop through the tour and the motion
  frames, that the panel holds everything drawn (nothing a tick drew was left unpushed),
  that the breathing entries never show outside the ●'s 12 x 12 rectangle, and that at
  Dim nothing moves but the clocks. `test_anim_cost` asserts the SPI pixels a second while
  recording and prints the host time per loop (`pio test -d firmware/sim -f
  test_anim_cost -v`).

**Fidelity gaps:**

- **Backlight:**
  - Off and dimmed screens are saved as fully lit pixels. `shots.json` and the
    contact-sheet caption give the level.
  - On battery the screen goes dark after 60 s without a key, and the next key only wakes
    it.
- **Audio:**
  - The audio task is `FakeRecPort`, which runs synchronously, so "保存中…" (Finishing)
    and the cue phase pass within one loop and cannot be captured.
  - The level comes from a synthetic tone, not the microphone and its gain.
  - Segment timing is not real time.
- **Timing:** Every loop is exactly 10 ms, while the device loops faster. Renders follow
  the same rules (dirty, at most 20 fps, and once a second), but they land on 10 ms ticks.
- **Hardware is not simulated:**
  - the keyboard matrix scan (the ghost filter still runs on the injected key sets)
  - USB CDC timing. `shots` has no TX ring (`tx_room` is unlimited); `serve` has the
    device's 4 KB ring and 4-line queue, drained as fast as the host reads the pty.
  - SD mount and health (the card is an in-memory FAT-like `FakeStore` with a fixed
    64 GB free)
  - LittleFS: cpafs is an empty in-memory store. The boot migration runs on it, but its
    "i/n" screen is not drawn.
  - the NVS boot counter: boot 1, and one more for each `debug.reboot`
  - the reset reason: `poweron`, and `software` after a `debug.reboot`
- **Device stats are fixed fake values:** heap, stack and the device id `100000000051`.
- **The panel:** the PNG shows the panel's RGB565 values. LCD gamma, brightness and
  viewing angle are not modelled.
- **The daemon:** in `shots` its side is whatever the script sends, plus the pings. In
  `serve` it is the real cpad.
- **`serve` only:**
  - The pty stays when the device reboots or `usb off` unplugs it. On the device the port
    disappears and cpad gets an I/O error at once; on the sim, cpad finds out from the
    device's replies (`E_NOT_READY` after a reboot) or from the 15 s link timeout.
  - Time is the Mac's: a loop every 10 ms of wall time. After a stall of more than 1 s (the
    Mac slept, a debugger) the missed loops are skipped, not run in a burst.
  - Every `serve` is the same device: id `100000000051`, serial `SIM-100000000051`, not
    installed from the Launcher.

## The M5GFX oracle

`oracle/oracle.sh` builds M5GFX itself for the host, with its SDL back end (only the
sprite is used), next to `FbCanvas`. It draws every case of
`test/test_efont/oracle_cases.h` with both, on a fresh and on a shared sprite, and compares
the 240 x 135 bytes and the widths. The cases cover the status-bar strings, clipping on
every side, text that starts left of the screen, glyphs the font lacks (✓ ⌛, an emoji
above U+FFFF), control characters, bad UTF-8, the 96-byte run split, and fills.

```sh
brew install sdl2                           # once (Linux: apt install libsdl2-dev)
firmware/sim/oracle/oracle.sh               # 0 case(s) differ, or exit 1
firmware/sim/oracle/oracle.sh --golden      # rewrite test/test_efont/oracle_golden.h
```

`oracle_golden.h` holds what M5GFX drew: a hash of each case's frame, its width, and the
rows of A g 助 ● □. The efont tests check `FbCanvas` against it, so `pio test` catches a
decoder change without M5GFX in the build. Regenerate it only from the oracle.

efontCN_12 has no glyph that starts left of its pen or inks past its advance (a test
checks every glyph). So the port's branches for those cannot change a device pixel, and
no oracle case reaches them.

## When the firmware changes

- **New pages or keys:** write a script; nothing in the simulator changes.
- **A new port or constructor argument of `Runtime`:** change only
  `lib/simdev/sim_device.cpp`.
- **A new library or module:** usually nothing. v0.4.0's `home` and `help` were found
  without a change. If the link fails, add it to `lib_deps` in `platformio.ini`.
- **A new M5GFX pin:** install the new version with the `pio pkg install` line above, run
  the oracle, and if it passes, pin the new `lgfx_efont_cn.c` sha256 in `gen_font.py` and
  regenerate `oracle_golden.h`.
- **Code the simulator repeats from device-only files** cannot follow a change there:
  - `FbCanvas::walk` repeats `M5Display::walk`, and `TxPipe` repeats
    `UsbTransport::send` and `pump`.
  - `kRebootDelayMs` (`hal/hal_platform.h`) and the audio task's 4 KB, 128 ms block
    (`hal/audio_io.h`) are repeated once each, in `lib/simdev`.

  The palette is no longer repeated: since docs/19 both sides use `lib/theme`.
