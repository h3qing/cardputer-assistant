**English** · [中文](README.zh-CN.md)

# Cardputer Pocket Assistant (placeholder name)

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

## What this is

Turns the M5Stack Cardputer ADV into a "pocket side screen + quick-note keyboard" for your Mac:

- See at a glance which Claude Code session is running and which one is waiting for you, in every project;
- Jot down an idea or some feedback and file it into that project's queue, or, after you confirm, start a new session for that project right away;
- Keep taking notes while offline; they sync automatically when you plug back into the Mac. Anything written offline **only becomes a note or a queued item. It is never executed automatically.**

The docs are in English; most pages have a Chinese version next to them (`*.zh-CN.md`, linked on the first line). The device (Settings → Language, opened with Opt+M) and the Mac app can both switch between English and Chinese.

> Status: v0.5.1 is released ([release notes](docs/releases/v0.5.1.md)): recordings stay on the device until the right Mac takes them ([docs/28](docs/28-multi-mac-sync.md) S1a), and the docs, `cpa`, the installer and the Mac app are English first, with Chinese supported. The plan is in [docs/13](docs/13-assistant-v2-plan.md), and the keys are under [Keys](#keys) below. `cpa`, `cpad`, `cpa-fw` and the repo name are all placeholders, to be renamed before the first public release ([04 §16](docs/04-prd.md) item 9).

## Three pieces of code, one contract

| Directory | Content | Status |
|---|---|---|
| [`protocol/`](protocol/) | Machine-readable contract for NDJSON protocol v0: `limits.json`, JSON Schema, test vectors, `check_vectors.py` | WP1 |
| `firmware/app/` | Device firmware `cpa-fw` (C++17, PlatformIO) | WP2 / WP4 |
| `daemon/` | Mac daemon `cpad` and command line `cpa` (Python 3.12 + pyserial) | WP3 |
| [`firmware/hwtest/`](firmware/hwtest/) | Hardware probe firmware for the Day-0 gates; kept unchanged | Existing |
| [`firmware/third_party/SD/`](firmware/third_party/SD/) | Patched Arduino `SD` driver (Apache-2.0, see [NOTICE](NOTICE)), shared by app and hwtest | v0.2 |
| [`tools/repo/`](tools/repo/) | Repo-level checks (file line counts) | WP1 |

Firmware and daemon are coupled only through the protocol. [docs/05 §6](docs/05-architecture.md) is the single source of truth for the protocol spec; `protocol/` is its machine-readable version, and the contract tests on both sides read the same vectors.

## Install (one command, from v0.5.0)

On an Apple silicon Mac with macOS 14 or later, paste one line into Terminal. **Running it again upgrades**, and it also repairs a broken install.

```bash
curl -fsSL https://raw.githubusercontent.com/h3qing/cardputer-assistant/main/scripts/install.sh | bash
```

**USB stick, no network**: in the repo on your main Mac (on the tagged commit), build the release and copy it to a USB stick, then install from the stick on the new Mac:

```bash
scripts/release-local.sh --tag v0.5.0 --out /Volumes/USB/v0.5.0 --release-json   # main Mac
bash /Volumes/USB/v0.5.0/install.sh --from /Volumes/USB/v0.5.0                    # new Mac
```

Files from `--from` are still checked one by one against the digests in `release.json` and against `SHA256SUMS`. uv and Python 3.12 still need one download (about 70 MB); if the new Mac already has uv and Python 3.12, no network is needed at all.

What it does (each step first says what it is about to do, in English, or in Chinese with `--lang zh`):

- Downloads the cpa wheel, the pinned pyserial and the Mac app from the latest release. **Every file is checked against the SHA-256 GitHub reports and against the release's `SHA256SUMS`.** On a mismatch it stops and installs nothing; files carrying the quarantine attribute are also refused.
- Installs `cpa` and `cpa-stt` as uv tools, linked in `~/.local/bin` (without uv it downloads a pinned uv version, hash-checked the same way); installs the app into `~/Applications`. No sudo, Gatekeeper stays on: files downloaded with curl carry no quarantine attribute.
- **Asks you before installing the background service**: "Have you synced the device on your main Mac (↑ and REC both 0)?" Today's cpad takes over the first board plugged in, and notes and recordings on the device that have not been transferred are stored on this Mac. If there is no terminal to ask (or you answer n), it does not install the service and at the end tells you to run `cpa service install`.
- Does not touch `~/.claude`, your `config.json` (only the `spawn_env` and `claude_bin` entries are updated), the notes directory, or `$CPA_HOME`, and downloads no models.
- Shows progress bars while downloading the app and firmware; on a first install it first says "about to download Python 3.12 (about 70 MB)", so it never sits silent like it is stuck. In the background-service question, if the device is plugged in right now it says so plainly.
- Finally prints a version line and **one** checklist, `cpa setup check --todo`: it lists only what is not ready yet, in the same language, marks which items are required (how many steps are left and which ones), and gives each item the command to run (the local-transcription item says where the model is on the main Mac and to copy it with AirDrop; without Homebrew it gives https://brew.sh; if Claude Code is not found it says to run `cpa service install` again after installing it). A few more lines cover what the checklist cannot: register projects (`cpa project add --folder ~/code/some-project -- name`, or the app's Projects page; without registering, the device board is empty), the app lives in the menu bar, and the first time you plug in the device you must click "Allow accessory to connect".

Options go after `bash -s --`, for example `… | bash -s -- --dry-run`: `--dry-run` (only says, does nothing), `--version v0.5.0`, `--yes` (no questions), `--no-service`, `--no-app`, `--no-modify-path`, `--prefix DIR`, `--from DIR`, `--lang zh|en` (the language it prints; default: `CPA_LANG`, then the Mac's locale, else English), `--firmware` (also checks `cpa-<tag>.bin` and puts it in `~/Downloads`; you still flash it yourself on the main Mac: sync first, `cpa pause 120`, then write it to 0x0 as in "Install method A" below). If test switches (`CPA_INSTALL_*`) are left in the environment, the script lists each one in red as its first step.

Uninstall: replace `install.sh` with `uninstall.sh` in the command above. It removes the app, the two LaunchAgents, `cpa` and the PATH line, and asks you before removing the Claude Code hooks. It **keeps** `$CPA_HOME` (keys, events, settings), notes and models, and lists where they are. `--purge-data` asks you to type `delete` before it removes `$CPA_HOME`; the notes directory is never removed automatically.

If a managed Mac does not allow gh / Homebrew, follow [docs/27 §5.2](docs/27-new-mac-setup.md): download the wheel and `SHA256SUMS` by hand, verify with `shasum -a 256 -c`, then `uv tool install`. Whoever cuts a release uses `scripts/release-local.sh --tag vX.Y.Z` (by default it only builds and does not upload; `--publish` uploads), see [docs/releases](docs/releases/).

The "Quick start" below is the from-source developer install.

## Quick start

Goal: in a **brand-new user account** on a Mac, go from cloning the repo to seeing the board on the device in 15 minutes or less. Actual timings are recorded in [docs/07-hil-checklist.md](docs/07-hil-checklist.md) item 7 (⏳ not yet measured in a fresh account).

**You need**: a Cardputer ADV and a USB-C data cable, macOS (developed on Apple Silicon), Xcode Command Line Tools (`xcode-select --install`, to provide git).

1. **Install tools** ([uv](https://docs.astral.sh/uv/) and PlatformIO 6.2.0, see [docs/03 §4](docs/03-hardware-bringup.md)):
   ```bash
   curl -LsSf https://astral.sh/uv/install.sh | sh        # open a new terminal afterwards
   uv tool install platformio==6.2.0 --python 3.12         # the command is ~/.local/bin/pio
   ```
2. **Get the code and prepare the Mac side**:
   ```bash
   git clone <repo-url> cardputer-assistant && cd cardputer-assistant
   uv sync --project daemon                                # Python 3.12 + pyserial, into daemon/.venv
   ```
3. **Flash the firmware (without pressing G0)**: plug in USB-C and set the side switch to **ON**.
   ```bash
   (cd firmware/app && pio run -e cardputer-adv -t upload)
   ```
   The first run downloads the toolchain and libraries and takes a few minutes. **You do not press any key** during the upload: esptool enters and leaves download mode automatically through the USB download flag, and reboots when done. The screen shows the home page with `◇ no reply` in the status bar, because the Mac side is not running yet. This is normal.
   - Before flashing again later, run `uv run --project daemon cpa pause 120` so the daemon lets go of the serial port (when `cpa` is on PATH, the pre-upload script does this automatically).
   - **The factory firmware normally does not enable USB serial** ([03 §2 pitfall 1](docs/03-hardware-bringup.md)). If the Mac shows no `/dev/cu.usbmodem*`, automatic upload cannot work, and the first time you have to enter download mode by hand: with the cable in and the switch on ON, **hold G0, press and release BtnRst, keep holding G0 for about 1 more second, then release**, then run the upload command above; when it finishes, **press BtnRst once only** (without G0) to boot the firmware. ⏳ This key sequence has not been measured on the ADV yet ([docs/01 §1 step 5](docs/01-mac-setup.md)). **Do not** hold G0 while flipping the switch or plugging in the cable: that locks the chip in download mode until a real power cut (see E5 below).
4. **Install the Mac service** (a LaunchAgent that runs `cpad` after login):
   ```bash
   uv run --project daemon cpa service install
   ```
   It records a few environment variables such as the login shell's PATH, and finds the absolute path of `claude`. If it cannot find it, it tells you; starting sessions stays off, and quick notes work as usual.
   - **A second Mac**: today's cpad does not ask you. It binds the first board plugged in, and notes and recordings on the device that have not been transferred are stored on that Mac and deleted from the device; once stored there, they cannot come back. Before plugging into another Mac, plug into your main Mac first and wait until ↑ and REC in the status bar reach 0; do not run cpad on a Mac you only want to charge from. The improved design is in [docs/27](docs/27-new-mac-setup.md).
5. **Register projects** (each project takes one number key 1–9; sessions started from the device use the `plan` permission mode by default):
   ```bash
   uv run --project daemon cpa project add myproj ~/code/myproj --slot 1 --name "My Project"
   ```
   (`--name` is the display name; it may be Chinese.)
6. **See the board**: within a few seconds the device status bar changes to `● online`. The home page is a launcher; press P (or Opt+P on any page) to open the project board, which lists the registered projects and how many sessions in each are running or waiting for you.
   ```bash
   uv run --project daemon cpa status                     # should show link: ready
   ```
   Press G0 to open a quick note, type, and press Enter; the note appears in `~/CardputerNotes/YYYY-MM-DD.md`.
   - **Leaving**: in an empty quick note, press Backspace or `` ` `` once to leave; with text in it, press `` ` `` and it first asks whether to discard (from v0.4.1; type the backtick character itself with Fn+`). On other pages the top-left `` ` `` key goes back; on the confirm boxes for starting a session or replying, it returns to the editor and keeps your text (key presses within 0.4 s of the confirm box opening are ignored). The hint bar at the bottom always shows the current leave key.
   - **Forgot a key**: press Tab to open the menu; see [Keys](#keys) below.
7. **Install Claude Code hooks** (recommended, v0.4): when a session asks for permission, asks a question, waits for plan approval, needs a login, or finishes a turn, the board updates at once and can show the reason, without waiting for the next poll. It works without them, but then relies only on a poll every 3 seconds.
   ```bash
   uv run --project daemon cpa hooks install              # only shows what would be added to ~/.claude/settings.json, writes nothing
   uv run --project daemon cpa hooks install --yes        # writes it; the old file is backed up to hook-backups/ in the data directory first
   ```
   - Claude Code sessions in a terminal **only run hooks in trusted directories**. For a directory you have not trusted yet, run `claude` there once and accept the trust prompt (this is also required for starting sessions from the device, see `E_SPAWN` below).
   - Sessions that are already open pick up the new hooks automatically. `hooks` in `cpa status --json` shows how many sessions have sent events and how many seconds ago the last one was. Uninstall: `cpa hooks uninstall --yes`.

If you no longer want the service: `uv run --project daemon cpa service uninstall`. Data lives in `~/Library/Application Support/CardputerAssistant/`.

### When the device does not respond

- **E5: black screen, handshake fails on the Mac (probably stuck in download mode)**. Caused by G0 being held at reset or power-up. Recovery: set the side switch to **OFF**, **unplug the USB cable**, wait a few seconds, then plug back in and switch to ON. Flipping the switch alone is not enough, because USB keeps supplying power ([03 §2 pitfall 2](docs/03-hardware-bringup.md)). You can also press BtnRst once on its own, without G0.
- **Upload stuck at `Changing baud rate` / `No serial data received`**: first `cpa pause 120` and confirm nothing is holding the serial port (`lsof /dev/cu.usbmodem*`), then do one ROM-direct reset without cutting power, then upload again ([ADR-0001 §5.3](docs/adr/0001-toolchain.md)):
  ```bash
  ~/.local/share/uv/tools/platformio/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 115200 --before default_reset --after hard_reset --no-stub read_mac
  ```
- **Status bar stays at `◇ no reply`**: use `cpa status` to see whether the daemon is running and whether the port is held by another program (serial monitor, M5Burner, etc.).
- **Starting a session fails (`E_SPAWN`)**: check `cpa status`. `login=` on the `claude:` line is the result of `claude auth status` in the daemon's environment (`no` means not signed in, `-` means not known yet); the `warning:` line gives the reason and a one-time fix: if not signed in, run `claude auth login` in a terminal; if the project directory is not yet trusted by the Claude Code CLI (the desktop app's trust records are separate from the CLI's), go into that directory, run `claude` once and accept the trust prompt. cpad never accepts trust or signs in for you.
- **End-to-end self-test**: the real-device test firmware and test scripts are in [docs/07-hil-checklist.md](docs/07-hil-checklist.md) and [`tools/hil/`](tools/hil/).

## Keys

Keys as of v0.4.1 (the full table is in [docs/04 §8](docs/04-prd.md); what changed from v0.4.0 is under "Key changes" in the [v0.4.1 release notes](docs/releases/v0.4.1.md)). Only single keys and two-key chords. **Forgot a key? Press Tab**: each menu row shows its key on the left; select it and press Enter, which is the same as pressing that key.

| Key | Action |
|---|---|
| G0 short press | Write a note into today's notes (waits 0.35 s for a second press; you can also just start typing, and the first character goes in) |
| Hold G0 | Speak a voice memo; release to end. If your finger slips, pressing again within 0.6 s continues the same memo; while holding, press 1–9 to file it to a project, 0 for no project |
| Double-tap G0 | Hands-free memo: recording starts the moment the second press goes down and keeps going after release (a slower second press within 1 s also counts); press G0 once more to end and save. While recording, 1–9 / 0 file to a project, Tab opens the menu, Opt+L locks (recording continues); other keys do nothing |
| Tab | Menu |
| Opt+H | Home (launcher) |
| Opt+P | Project board (Opt+H in v0.3.1 and earlier) |
| Opt+1 to Opt+9 | Project page N |
| Opt+I | Waiting for you |
| Opt+M | System |
| Opt+N / Opt+Q / Opt+S | Write a note / queue for AI / start a session |
| Opt+R | Meeting recording (shows a confirm box first; starts only on Enter) |
| Opt+L | Lock screen |
| `` ` ``, Backspace or Fn+` | Back (in the editor, Fn+` types a backtick) |

- **List pages**: `;` `.` move up and down, `,` `/` switch tabs, Enter opens or runs, Opt+D marks done or dismisses.
- **Home**: press the letter on a tile directly (P projects, I waiting for you, N write a note, Q queue for AI, S start a session, M system); this equals Opt+that letter. Meeting and lock need Opt+R and Opt+L (from v0.4.1, so typing cannot start a meeting or lock by accident); selecting the R or L tile and pressing Enter also works. 1–9 open project page N; `;` `.` `,` `/` move, Enter opens the selected tile. Home also shows the most urgent "waiting for you" item.
- **Meeting page**: G0 or Space adds a ★, 1–9 / 0 file to a project, Opt+R ends (confirm first: Enter ends, `` ` `` keeps recording). On the meeting start and end boxes, Opt+L means cancel and lock / keep recording and lock; the start box closes by itself after 15 s and the end box after 10 s with no key press.
- **While the editor, a confirm box or the menu is open**, holding G0 also records a memo, and the draft is kept; the same 0.6 s grace applies after release. In a freshly opened note with nothing typed yet, holding G0 closes the note and records a memo. Double-tap G0 hands-free only counts where a G0 short press means "write a note" (with the menu open, the first press closes the menu first).

### Tab menu

The menu has three pages. `,` `/` switch pages, `;` `.` select a row, Enter runs it, Tab or `` ` `` closes:

- **This page**: the keys available on this page;
- **Go to**: Home, project board, waiting for you, project pages, system, plus global keys such as write a note and meeting; on this page, pressing a letter directly equals Opt+that letter;
- **Controls**: lock, plus read-only sound (off), battery, SD card and Mac link.

Tab then a letter equals Opt+that letter, on "This page" too: Tab, P twice takes you to the project board without remembering any chord. When the menu is opened on a capture page, a confirm box or the meeting page, it only has "This page" and "Controls", letters on "This page" do nothing, and drafts and ongoing recordings are not affected. Pressing G0 in the menu, or a chord the menu shows (such as Opt+L), first closes the menu and then works as usual (in a meeting G0 adds a ★; during hands-free recording one G0 press ends it).

### Lock screen

- Opt+L locks: the card lights for 2 s showing "Locked", then the screen turns off and the keyboard is locked, **even while plugged into the Mac**. **On battery, when the screen turns off after 60 s without a key press, the device locks automatically** (not while plugged into the Mac). The page and draft from before the lock are kept, and an ongoing meeting keeps recording.
- **Unlock**: press Opt+L first (shows the lock card), then press Enter within 3 s.
- While locked, **tap G0 first** to show the lock card for 3 s; only after that does holding G0 record a memo (also when locked on a draft; the draft is untouched). Before the microphone opens it shows "● Wait", after release there is a 0.6 s grace (pressing again continues the same memo), and when done the card lights for 2 s with the result ("√ Saved 0:12"), then goes back to dark and locked. Holding without the tap first only lights the card with "Not recording" and does not record, so nothing is recorded by accident in a pocket. In a meeting, G0 still adds a ★. Other keys do nothing and do not light the screen.
- Double-tap G0 hands-free also only counts on a card lit by a key press (when the screen is dark or the card lit by itself, the first press only lights the card). When locked during hands-free recording, the dark screen shows "● Hands-free 0:12" in the corner, and G0 only ends it while the card is lit: on a dark screen, tap once to light the card (it says "Hands-free: G0 ends it"), press once more to end, and the card shows the result.
- The lock card only shows time, date, battery and a few counts (waiting for you, recordings waiting to transfer, ↑N). It shows no project names, session names or any content. While plugged into the Mac, when the waiting-for-you count goes up, the card lights for 3 s.
- **The lock screen is not a security lock**: there is no PIN, and anyone holding the device can unlock it with Opt+L, Enter. It only guards against pocket presses and keeps others from glancing at content. Everything on the SD card and in `~/CardputerNotes` on the Mac is plaintext, see [SECURITY.md](SECURITY.md).

### Recording and consent

- From v0.3.1, recording is completely silent and the screen is the only indicator: while recording, `● REC` and the timer stay visible, including on the lock screen, with the menu open, and while a notice is shown.
- **Before recording other people, tell everyone present and get their consent.** The confirm box before meeting recording is only a reminder; informing people and getting consent is your responsibility. Recording laws differ by place, and this project's docs are not legal advice. To record only yourself, use a memo (hold G0).

## v0.2: microSD storage and Launcher

With a FAT32 microSD card inserted, pending items and the project cache live in `/cpa/` on the card (other files on the card are not touched); without a card, they are kept in the `cpafs` partition of internal flash, as in v0.1. The choice is made once at boot and never switches while running. The same release image can be whole-flashed or installed from the SD card by [bmorcelli Launcher](https://github.com/bmorcelli/Launcher). The System page (Opt+M) shows the boot method (`启动 直接` / `启动 Launcher`, i.e. boot direct / boot Launcher), the current storage and the reason; the link line of `cpa status` shows two fields like `store=sd launcher=no`.

Release artifacts (produced by `scripts/make_image.py`, attached by CI on release):

| File | Use |
|---|---|
| `cpa-<tag>.bin` | Merged image (bootloader, partition table, boot_app0, app). For a whole-flash write it goes to 0x0; it can also be put on the SD card for Launcher to install, where it shows as `cpa-<tag>` |
| `cpa-<tag>-app.bin` | App only, written to 0x10000, leaves the partition table alone |
| `SHA256SUMS` | Checksums (`shasum -a 256 -c SHA256SUMS`) |

Build an image yourself (run `pio run -e <env>` first): `cd firmware/app && python3 scripts/make_image.py --env cardputer-adv --tag v0.2.0-dev --out dist`.

### Preparing the SD card

- Only **FAT32 (or FAT16) + MBR partition table** is supported. Cards of 32GB and up usually ship as exFAT, which the firmware cannot use; the System page then says "SD card unusable: format it as FAT32 on a Mac". v0.2 cannot format on the device.
- Format on a Mac (**erases the whole card**). First use `diskutil list` to find the card in the reader, and **check the disk number twice** (size, name and `external, physical` must all match); a wrong disk number erases another disk:
  ```bash
  diskutil list                                        # pass 1: find the card, e.g. /dev/disk4
  diskutil list /dev/disk4                             # pass 2: only this disk, confirm it is the card
  diskutil eraseDisk FAT32 CPA MBRFormat /dev/disk4
  ```

### Step 0: before switching install method, empty the internal pending items

Whole-flash ↔ Launcher: do this step in both directions. After the partition table changes, items in internal `cpafs` that have not been sent can no longer be recovered.

- Connect to the Mac and sync, and wait until ↑N in the status bar is 0; or
- Boot once with an SD card inserted so the boot migration moves the items in `cpafs` to the card, then confirm on the System page that the storage line shows `已迁移 N` (migrated N) and no `迁移失败` (migration failed).

### Install method A: whole-flash write

- From source: `cd firmware/app && pio run -e cardputer-adv -t upload` (writes in segments, keeps NVS).
- With the release image: write `cpa-<tag>.bin` to 0x0, for example
  `esptool.py --chip esp32s3 --port /dev/cu.usbmodem1101 write_flash 0x0 cpa-v0.2.0.bin`. The merged image fills the gaps between segments with 0xFF, so writing it this way **erases NVS**: the epoch and boot counter are regenerated, which is safe by design (new ids cannot collide with old ones).

### Install method B: bmorcelli Launcher

1. Do step 0 first.
2. Install Launcher **release 2.9.1**: use the web flasher <https://bmorcelli.github.io/Launcher/> (choose the device "Cardputer and Cardputer ADV"), or download `Launcher-m5stack-cardputer.bin` from the official release, check that its size is 1,422,720 B, record its checksum with `shasum -a 256`, then write it to 0x0 with esptool.
3. Copy `cpa-<tag>.bin` (it must be the merged image, so that Launcher creates `cpafs`) to the SD card, preferably in `/cpa/fw/`.
4. In Launcher choose SD → this file → install.
5. Launcher's own SD driver may not read large cards. If it cannot, use a card under 32GB for installing, or Launcher's serial install (`flash overwrite`, see [docs/gates/launcher.md §2, §4](docs/gates/launcher.md)).

**Upgrading under Launcher**: overwrite install is recommended, i.e. the "Use <label> partition" option Launcher offers when space runs out, or serial `flash overwrite`; you can also delete the old version and **immediately** install the new one. Do not delete the old version while leaving another version lying around: deleting any one of our versions also erases `cpafs`, so without a card the remaining version has no storage (the System page shows "no storage available"). With a card inserted, pending items are on the card and not affected.

**Upgrading from v0.1**: `pio run -t upload` keeps NVS; writing `cpa-<tag>.bin` to 0x0 erases NVS and regenerates the epoch (safe). Neither touches 0x670000: v0.2's `cpafs` is the same region as v0.1's `spiffs`, so the data is still there, and booting with a card inserted migrates it to the card automatically. If you only wrote `-app.bin`, the partition table is still v0.1's, and the System page says "partition table is v0.1, please whole-flash"; no data is lost, and a whole-flash write recovers it.

### Notes for users

- Under Launcher, holding G0 at boot enters the Launcher menu instead of opening a quick note.
- Under Launcher a cold boot adds a splash screen of about 5 s; power-cycling with the switch or pressing RST also goes into Launcher first.
- Do not pull the card while ↑N is not 0. If the card is pulled while running, that storage operation stalls briefly (up to about 2 s), after which every save shows "save failed (E_STORAGE)" and the System page shows "storage error N, please restart". Put the card back and restart to recover; the items on the card are sent as usual after boot.
- Pending items on the card are plaintext, as they were in internal flash in v0.1, except that a card can be pulled out and read directly ([SECURITY.md](SECURITY.md)).

## Development

Rules are in [CONTRIBUTING.md](CONTRIBUTING.md): TDD, file and function size, conventional commits. Checks for the WP1 part:

```bash
uv run --with pytest --with 'jsonschema>=4.23,<5' pytest protocol/tools -q   # protocol tool tests
uv run --with 'jsonschema>=4.23,<5' python protocol/tools/check_vectors.py   # validate all schemas and vectors
uv run --with pytest pytest tools/repo -q
python3 tools/repo/check_sizes.py --warn 400 --fail 800 protocol tools
uvx lizard -L 50 -w protocol/tools tools/repo
```

CI is in [`.github/workflows/ci.yml`](.github/workflows/ci.yml); once `firmware/app/` and `daemon/` exist, their jobs turn on automatically. Real-device (HIL) tests are not in CI.

## Docs

| Doc | Content |
|---|---|
| [01-mac-setup](docs/01-mac-setup.md) | Mac development environment |
| [02-landscape-report](docs/02-landscape-report.md) | Survey of existing projects |
| [03-hardware-bringup](docs/03-hardware-bringup.md) | Real-device bring-up notes (USB, HWCDC, keyboard) |
| [04-prd](docs/04-prd.md) | What to build, and how far |
| [05-architecture](docs/05-architecture.md) | How it is built; §6 is the protocol spec |
| [06-work-packages](docs/06-work-packages.md) | Work packages and gates |
| [13-assistant-v2-plan](docs/13-assistant-v2-plan.md) | Assistant v2 plan: home, Tab menu, lock screen (v0.4.0) and later versions |
| [23-mac-inbox](docs/23-mac-inbox.md) | Design: to-file inbox on the Mac (`cpa filing`, WhisperWoof's Cardputer page) and environment stamps on recordings (#54) |
| [25-route-to-any-app](docs/25-route-to-any-app.md) | Design: speak and send to any chat or agent app, e.g. Grok first; destination = generic kind + one preset per app (#61) |
| [26-security-round](docs/26-security-round.md) | Design: security testing milestone M-SEC and encryption at rest; the device seals each file to the Mac's public key (SEAL), the Mac uses full-disk encryption and files stay open (#63) |
| [27-new-mac-setup](docs/27-new-mac-setup.md) | Design: one-command install on a new Mac, first-run setup page, the Mac asks you before trusting a device, the device recognises a new Mac, moving settings (#64) |
| [28-multi-mac-sync](docs/28-multi-mac-sync.md) | Design: one device, several Macs; recordings stay on the device until delivered to the right Mac, a kept copy stays a few days after delivery so it can be delivered again, groups mirrored on the device |
| [releases/](docs/releases/) | Release notes for each version |
| [adr/](docs/adr/) | Architecture decision records |

## Security and privacy

The v0.1 trust boundary is "a physical USB connection + the Mac user currently logged in": no network ports, no cloud, no telemetry. Known risks (unsynced items are plaintext if the device is lost) are in [SECURITY.md](SECURITY.md). The v0.4.0 lock screen (Opt+L) only guards against accidental presses and keeps others from seeing content; it is not a security lock and encrypts nothing.

## License

[MIT](LICENSE), Copyright (c) 2026 Heqing Huang (Apache-2.0 before v0.5.0). The patched `firmware/third_party/SD` remains Apache-2.0 (full text in [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt)). Third-party components and sources we borrowed from are listed in [NOTICE](NOTICE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
