**English** · [中文](27-new-mac-setup.zh-CN.md)

# 27 · A new Mac: install once, plug in, and it recognises the same Cardputer

> Status: **design draft, revised after three reviews (user, security, feasibility); the wording on keys and pinning is aligned with the final docs/26; cross-checked against docs/26 again on 2026-10-22 (see the last row of the appendix); waiting for your call** (§9) · Date: 2026-10-22 · Role: architecture (Mac install and first-run setup) · Based on origin/main `995821d` (same code as `d330465`, includes cpa-stt #62, adds docs/25) · Branch `docs/new-mac-onboarding` · issue #64 · Related: docs/18, docs/22, docs/23, docs/24 (`wt-mac`), docs/25 (PR #65, merged into main `995821d`), docs/26 (`wt-sec`, #63, PR #68, `96a03b6`, `8fc1f4a` after the cross-check), #57, #60, #62.
> Requirement (2026-10-22): make a security test round a later milestone and make sure all data is encrypted on disk; make it easy to use the same device on several Macs, with a smooth first-run setup on a new Mac that installs the Mac side and lets the Mac recognise the device.
> Sources: README §Quick start; SECURITY.md; docs/01 §1; docs/04 US-04, S1; docs/05 §5.10, §6.4, §8; docs/07 item 7 and line 14; docs/15 §2.4; docs/18 §1.3, §3–§5, §8, §9, §10.3, §13, §15; docs/21 §4, line 424; docs/22 §4.4, §4.10, §4.13; docs/23 §3.1, §4.2, D10; docs/24 §1.2, §2.3, §2.5, §4, §5.2, §7, D8, D10, D12; docs/25 §2.3–§2.4, §5.5, §5.7; docs/26 §3.1, §4.3–§4.5, §5, §6, §7, D1, D4–D7, D10, D13, D17, D21, D23; `protocol/README.md` lines 49, 103, 154; the daemon's `app.py`, `device/`, `config/`, `service/launchd.py`, `cli/`, `projects/registry.py`, `hub/reduce.py`, `mcp/`, `archive/cloud.py`, `recordings/`, `cpa_stt/`; the firmware's `link_session.cpp`, `runtime_outbox.cpp`, `messages.h`, `hal_platform.h`; `tools/hil/harness/rig.py`, `tools/sim/`; `.github/workflows/{ci,release}.yml`; issue #57, PR #60, #62; one read-only survey (local file names and metadata, public docs for Homebrew 5.0, Gatekeeper, SMAppService and the uv cache).
> Marks: **[src]** code or docs read · **[meas]** read-only measurement on this Mac · **[ext]** external docs · **[inf]** inference · **[asm]** design assumption, can change · **[tbv]** to be verified by a test · **👤** needs you.
> Scope: design only. No device connected, no serial port opened, no code or system settings changed; for `~/CardputerNotes` and `$CPA_HOME` only the file names were looked at.
> Path shorthands: `cpad/` = `daemon/src/cpad/`; `stt/` = `daemon/src/cpa_stt/`; `fw/` = `firmware/app/lib/`; `$CPA_HOME` = `~/Library/Application Support/CardputerAssistant`.

> **Revision (2026-10-26, branch `feat/installer`)**: on 10-26 you asked for a single curl command to install, like WhisperWoof's new-computer install. D1 becomes **a one-line install from release assets, checking each digest**: `scripts/install.sh` (`curl … | bash` once public; `gh api … | bash` while the repository is private). §5.1's "no curl one-liner" is withdrawn. Its worry, "a URL runs a script for you", is replaced by "the script installs only files from the release whose hashes match": each asset is checked against the GitHub API `digest` and the release's `SHA256SUMS`, and anything with a quarantine attribute is refused; the whole script is wrapped in `main()`, called on the last line, so a half-downloaded script never runs. The app is a zip in the release (built on a Mac with `scripts/release-local.sh`, ad-hoc signed). Files downloaded by curl carry no quarantine attribute, so Gatekeeper does not block them and nothing needs its quarantine removed. "Refuse a checkout with a quarantine attribute" applies only to the install-from-source path (§2.2's `mac-install.sh`, not built). `cpa setup check --json` (D3, `protocol/setup/v1`) ships first as the minimal 7 rows; O3 (connect only after you click) is not built yet, so the install script asks "Have you synced on your main Mac?" before it installs the LaunchAgent.

> **After review (2026-10-26, same branch)**: a new Mac has no Homebrew, so for the private repository the first choice becomes a read-only fine-grained token plus `curl -K <(printf …)` (the token never goes into arguments). The install script hands the token to curl with `printf … | curl -K -` (never written to a file), sends it only to this repository's `releases/assets/` endpoint, and allows only `[A-Za-z0-9_]` in the token. `--from DIR` installs from a folder (a USB stick) built by `release-local.sh --release-json`. The last screen is a single checklist, `cpa setup check --todo` (only what is not done, in Chinese and English, required items marked), plus a `claude` row (row 8: does the background service recognise Claude Code; `setup/v1` only adds). Security fixes: `--prefix` may not contain quotes, `$`, backticks or backslashes; `xattr | grep -q` misses under pipefail (SIGPIPE), so the value is read first and then compared; Ctrl-C also cleans up temporary files; curl always uses `--proto =https --tlsv1.2`; `release-local.sh --out` only clears a folder it built itself and never packs files git does not track; `uninstall.sh --purge-data` does not delete a `$CPA_HOME` that contains the notes folder. **Trust boundary**: releases are not signed. Trust comes from write access to the repository and HTTPS to GitHub (the API `digest` and `SHA256SUMS` come from the same release). That is acceptable for a private repository with one maintainer. The Python 3.12 that uv fetches (python-build-standalone) is checked by uv against its built-in hashes. During a release, in the few seconds while `--clobber` replaces an asset, an install fails because the digest does not match; it never installs the wrong thing.

## 0. One-page summary

1. **What you want**: the same Cardputer used with several Macs; a new Mac that is easy to set up; the Mac recognises your device and the device recognises the new Mac; a security test round and "all data encrypted" as a later milestone. The overall design for encryption and security testing is in docs/26; this doc covers the new-Mac part.
2. **Three ways of using it** (§9.2 item 1 asks which is yours): **A** one Mac at home, one at work (the default); **B** one laptop carried back and forth; **C** several Macs that are all yours and count as one profile (for example a desktop plus a laptop).
3. **Today** (§1): setting up a Mac takes 6–9 manual commands, and four docs describe four ways to install. The version number has always been 0.1.0, so an upgrade installs the old code again (#57). When a second Mac sees the device plugged in, it **takes over without asking**: the notes and recordings on the device that have not been sent yet are stored on that Mac and deleted from the device. There is no way back.
4. **Goal** (§2): 5 pastes in Terminal (install tools, log in to GitHub, fetch the code and install; 3 once the repository is public). Then the Mac app's "First-run setup" page goes one row per item, in order: background service, notes folder, this Mac's name, move settings (usage C only), device, home / work. **At least 2 clicks** (choose the notes folder, connect the device); once the device has profiles, 3–4 presses on the device. The optional rows (local transcription, Claude Code, other apps, destinations) sit under "Optional". They do not nag, and each row has a "Not needed".
5. **The Mac recognises the device** (§3.1): for a board it has never connected to, the Mac **does not talk to it**. It only reads the serial number from USB and shows you a 4-character device code worked out from it (for example 3F7A). Syncing starts only when you click "Connect". Above the button, in plain words: the notes and recordings on the device that have not been sent will be stored on this Mac, the ones on the device will be deleted, and once stored here there is no way back. You can also click "Charge only", and this Mac will never touch it.
6. **The device recognises the Mac** (§3.2): per docs/18 v0.8, you choose on the device whether this Mac belongs to home, work or neither; what the Mac says about itself does not count. The Mac screen shows the exact keys: "Home" is `.` once, Enter twice; "Work" is `.` twice, Enter twice. If you choose "Later" or pick the wrong one, the screen also says how to change it.
7. **Before v0.8** (§3.3): by default, docs/18's minimal profiles move forward into the next firmware release (§9.2 item 2). Until then the protection is item 5's "connect only after you click", plus a reminder in the README right now: before you go to another Mac, plug in at your main Mac first and wait until ↑ and R (录) reach 0.
8. **Across Macs** (§4): notes and recordings stay on the Mac that received them; each Mac's identity never crosses over; the encryption private key never goes into any export and only moves with Migration Assistant and Time Machine (docs/26 D6); settings (1–9, destinations) can be exported as one file, AirDropped, and imported. Import **never widens permissions**: projects come in with the most conservative permission (plan only); destinations that run commands and links you wrote yourself are not moved; destinations that send text off the Mac need one more click from you on the new Mac.
9. **Install** (§5): there is no Apple developer certificate, so the code is built from source on your own Mac. The script picks the latest release, builds it, installs it and starts the background service; upgrading is just `mac-install.sh --upgrade`. Gatekeeper does not block things built on the same Mac, and no security setting needs to be turned off.
10. **New Mac, new device** (§6): after moving over with Migration Assistant, the new Mac gets a new identity, and once device encryption exists it also gets a new key; the old one is kept only to open, never to seal (docs/26 D23). The device asks you again, and the "Name" row asks you to change it too. With a new device, the "Device" row says "New device?", and one click fixes it.
11. **Encryption and security testing** (§3.5, §7): file encryption on the device follows docs/26 (each file is sealed to the Mac's public key, and nothing on the device can open it). The Mac's private key is a 0600 file under `$CPA_HOME/keys/seal/`, not in the Keychain (docs/26 D6). The new-Mac rules follow docs/26 D21: the Mac's public key is accepted and pinned only when you agree on the device; at every handshake the Mac must prove it holds the private key, and only after that proof does the device hand over sealed items and accept its ack and delete; after pinning, nothing is given before the proof; changing the key needs proof with the old key, and if that cannot be given the device asks you, with the cursor on "Keep". The notes on the Mac rely on FileVault and an encrypted Time Machine; first-run setup has a read-only "Disk encryption" row that tells you whether it is on. Security testing joins docs/26's milestone M-SEC: the Mac part runs after O0–O6 are merged (without encryption tests), and the encryption part runs in M-SEC round two (the encryption round, docs/26 SEC-6).
12. **Phases** (§8): release hygiene first (O0), then right after it "connect only after you click" (O3, Mac only, no protocol change), and only then first-run setup (O2). O0–O5 are about 2,200 lines of code and 2,700 lines of tests, with no wait on firmware.
13. **21 decisions** all have defaults (§9.1); only 4 need your answer (§9.2), with a one-line reply template at the end.

## 1. What you want, and how far today is from it

### 1.1 The requirement, item by item

- A milestone with a round of security testing. Here: the milestone is docs/26 §6's M-SEC; §7.2 of this doc adds new-Mac cases to it.
- Encryption at rest for all data. Here: the overall design is in docs/26 (per-file sealing on the device, full-disk encryption on the Mac); this doc covers how a new Mac hands over its public key (§3.5) and the "Disk encryption" row (§2.3).
- One device, several Macs. Here: §3, §4.
- A smoother first-run setup. Here: §2.
- Set up a new Mac in one go. Here: §2.2, §5.
- The Mac side installed, and the Mac recognises this device. Here: §2.3, §3.1.

### 1.2 Setting up a Mac today

| # | Step | Every Mac | Pitfall |
|---|---|---|---|
| 1 | Command line tools (git), uv, a data cable; on first plug-in click "Allow accessory to connect" | Yes | Without Allow it only charges, no data [src docs/01 §1] |
| 2 | Fetch the code: the repository is **private**, so `gh auth login` first [meas] | Yes | Not in the README |
| 3 | Install `cpa`: the README says `uv sync --project daemon` [src README.md:38], daemon/README says `uv tool install ./daemon`, #57 says `--force --reinstall --no-cache`, #62 says `--reinstall` | Yes | The version has always been `0.1.0` [src pyproject.toml:3]; uv keys its cache on `pyproject.toml`, so an upgrade installs the old wheel again; `cpa --version` and `cpad/0.1.0` in hello cannot tell old from new |
| 4 | `cpa service install`: the LaunchAgent records **the Python that ran it** [src cli/main.py:143-162] | Yes | Run as the README says with `uv run --project daemon`, the service runs on the checkout's `.venv` (the root of #57); nobody checks the return code of `launchctl bootstrap`, so it prints "installed" even on failure [src service/launchd.py:69-80] |
| 5 | `$CPA_HOME` and `host_id` are created automatically [src config/host.py] | Yes | `host_label` defaults to empty and `cpa host name` is not merged yet; only editing config.json by hand works |
| 6 | Device binding: automatic, **the first board that answers `cpa-fw/`** [src app.py:260-263; port_finder.py:50-54] | Yes | It does not ask or list; there is only `cpa device forget` |
| 7 | `cpa project add KEY PATH --slot N` (from 2026-10-26: `cpa project add -- name`, folder optional, or create it on the Mac app's "Projects" page, docs/25 D24) | Yes | Each Mac has its own numbering, the device caches only one (§1.4 F3) |
| 8 | Claude Code: log in, trust each project folder once; `cpa hooks install --yes` | Yes | The hooks hold **the absolute path of the `cpa` that ran them** [src hook_settings.py:57-64]; after changing the install method they point at the old path |
| 9 | MCP: `cpa mcp setup <client>` prints a snippet, you paste it yourself [src mcp/cli.py:107-109] | Each client | — |
| 10 | cpa-stt: `cpa-stt setup` → `use` → `service install`; needs Homebrew's `whisper-cli` and a 1.6 GB model | Yes | It **never downloads** the model [src stt/models.py:1-8]; `setup` only links, never copies, so if the original file moves you get `model_missing` [src stt/cli_setup.py:19-23]; only one of it and WhisperWoof can be on [src docs/22 §4.10] |
| 11 | Mac app: built locally with SwiftPM, ad-hoc signed [src docs/24 §7] | Yes | The packaging script is not written yet (`wt-mac/mac/scripts` only has `export_mando.py`) [meas] |
| 12 | Notes folder: `~/CardputerNotes` by default, no command can change it | Yes | Under `~/Documents` the LaunchAgent cannot write (TCC) [src docs/gates/whisperwoof.md:95]; `cpa status` only warns about iCloud Drive [src recordings/service.py:48-52]; cpad and cpa-stt both read the notes folder only once, at start [src app.py:116; stt/cli.py:108-110, 327] |

On this Mac [meas]: `cpad 0.1.0` is a uv tool, and `~/.local/bin/cpa` points into the uv tool folder; the LaunchAgent runs the uv tool's Python; only the command line tools, no Xcode; 0 signing identities. release.yml only publishes firmware (`cpa-<tag>.bin`, `-app.bin`, `SHA256SUMS`), with no Mac package [src].

### 1.3 A second Mac sees the same device plugged in

1. Mac B's cpad is not bound, so the first `cpa-fw/` board qualifies. It sends hello with **B's `host_id`**, and `host_label` is most likely empty [src app.py:245-248]. The device enters Ready on any host hello [src fw/link/link_session.cpp:74-86], stores the `host_id` and does nothing else with it [src messages.h:80]; the "Control" page shows `cpad/0.1.0` for both Macs.
2. B writes the serial into **its own** config.json. A's binding does not change: both Macs are "bound" to the same board.
3. B sets the clock (the last Mac wins [src docs/05 §5.10]) and after the handshake pushes a full snapshot, which includes the project list [src hub/reduce.py:76-77]: the device's **only** 1–9 is replaced by B's [src docs/18 §1.1]. If B has no projects yet, 1–9 goes empty until it is plugged back into A.
4. Waiting items go to B: as soon as the device is Ready it pushes up to 4 `outbox.item` [src fw/shell/runtime_outbox.cpp:91]; B appends, fsyncs and replies `outbox.ack stored`, and the device deletes that item [src hub/reduce.py:124-136]. Recordings are pulled by default too (`rec_pull: true`), and `file.del` follows once stored [src recordings/puller.py:151].
5. From then on, the device's recording history, ★ and hidden flags are answered only from **B's** `events.jsonl`.
6. Exactly once within each Mac; at least once across Macs: pull the cable between fsync and ack, and the next Mac stores another copy [inf].

### 1.4 Friction, by severity

| # | Severity | Friction |
|---|---|---|
| F1 | High | First plugged, first served, and irreversible; there is no way to move data between Macs, and `cpa export` does not carry `events.jsonl` or the settings [src docs/15 §2.4] |
| F2 | High | A meeting half pulled on A and finished on B leaves half a meeting on each side, each transcribed separately [src recordings/manifest.py:4-6] |
| F3 | High | The device has only one 1–9: offline `#3`, holding G0 plus a digit, and meeting tags all follow the last Mac's projects [src docs/13 §13 item 7; docs/23 §2.1] |
| F4 | Medium | Silent binding; you cannot see which Mac it is connected to |
| F5 | Medium | Every row of §1.2 must be redone on a new Mac |
| F6 | Medium | Four install methods, a version number that never changes, no Mac side in the release, a private repository that needs auth |
| F7 | Medium | On a new Mac cpa-stt lacks the model and `whisper-cli` and does not download them; a model AirDropped in sits in Downloads and breaks after a cleanup |
| F8 | Medium | "Sync the notes folder to merge" is a trap: two cpads append to the same daily file; the `.transcriber` claim is not atomic on a cloud drive [inf]; plaintext is uploaded, and `cpa status` only knows iCloud Drive |
| F9 | Low | Migration Assistant or a backup restore moves `$CPA_HOME` as is: the same `host_id` and the same name; the Python the plist points to may not exist on the new machine [inf] |
| F10 | Low | Before binding, cpad sends `host_id` in clear to any 303A:1001 serial port; after binding, a board that reports the same USB serial still gets it [src docs/18 §4.3, T4] |

## 2. Target experience

### 2.1 An honest count

A new work Mac, usage A, with the home Mac already set up:

| | Minimum (required rows only) | Everything |
|---|---|---|
| Terminal | 5 pastes: command line tools, uv, gh, `gh auth login`, fetch the code and install (3 once the repository is public) | Plus `brew install whisper-cpp` |
| System dialogs and web pages | The command line tools install dialog; GitHub web login plus a one-time code; "Allow accessory to connect" on first plug-in | Same |
| Mac app clicks | **2**: notes folder, connect the device | About 12: plus rename 1, local transcription 2, Claude Code 2, each AI client 1, each destination 2, (usage C) move settings 3 |
| File pickers | 0 | Move settings 1; on import, projects whose folder does not exist stay empty for now, with no dialog |
| Device | Once the device has profiles (v0.8, or the minimal profiles brought forward in O7) 3–4 presses; before that 0 | With "undecided" waiting items from before the upgrade, 2–3 more presses |
| Old Mac | 0 | (Usage C) Settings → This Mac → "Export settings…", then AirDrop: about 3 presses |
| Decisions for you | Where notes go; whether to connect this device; home / work on the device | Whether you want each optional row |
| Upgrade | — | One command: `~/cardputer-assistant/scripts/mac-install.sh --upgrade` |

### 2.2 Install

One block in the README, pasted line by line:

```bash
xcode-select --install     # 1. command line tools (git and swift are included): click "Install" and wait
curl -LsSf https://astral.sh/uv/install.sh | sh   # 2. uv (with Homebrew, brew install uv also works); open a new terminal afterwards
brew install gh            # 3. gh; without Homebrew, download the .pkg from cli.github.com and double-click it
gh auth login              # 4. choose GitHub.com → HTTPS → log in with the browser, enter the one-time code (steps 3 and 4 are not needed once the repository is public)
gh repo clone h3qing/cardputer-assistant ~/cardputer-assistant && ~/cardputer-assistant/scripts/mac-install.sh   # 5.
```

`scripts/mac-install.sh` does these in order. Each step first says what it will do; on failure it stops at that step and says what to do:

0. **Runs only inside a git checkout.** If files in the checkout carry `com.apple.quarantine` (a source archive downloaded by a browser), it stops: "Please fetch the code with git". It also stops if the working tree has uncommitted changes (only `--dev` lets that through). The whole script is wrapped in one function with `exit` on the last line, so bash never reads half a new file when the version switches. It also works from a checkout whose path contains spaces (for example a folder named `Cardputer Assistant`).
1. **Pick the version**: with no argument it takes the latest release tag (the first from `git tag -l 'v*' --sort=-v:refname`), runs `git checkout` to it, and then runs **this script as it is in that tag**. `--upgrade` first runs `git fetch --tags`; the rest is the same. A tag checkout has no branch, so there is no `git pull`; a manual upgrade is `git fetch --tags && git checkout v0.N.0 && scripts/mac-install.sh`.
2. Check the command line tools (`xcode-select -p`): if `git clone` worked they are already there, so this step is only a safety net; if missing, stop and give `xcode-select --install`.
3. Check uv: if missing, stop and give two ways to get it. It **does not curl for you**.
4. **Build the wheel**: continue only if the version in `daemon/pyproject.toml` matches the tag; `uv build daemon --out-dir <a new temporary folder>`, a new folder each time, so it can never pick up an old wheel. The build backend is pinned, and dependencies are installed against constraints exported from `daemon/uv.lock` [tbv: confirm whether `uv tool install --constraints` checks hashes].
5. `uv tool install --force --reinstall <that one wheel file>`.
6. **Background service**: `cpa service install`. On upgrade, if cpad is pulling a recording, it asks "Wait for it to finish before restarting?"; if not busy, it restarts cpad and any installed cpa-stt with `launchctl kickstart -k`, so the new code really runs. Afterwards it checks with `launchctl print` that it really is running; if it was switched off in System Settings → General → Login Items, it stops and says where to turn it on.
7. **Mac app**: after docs/24 v1 is released, if `swift` is present, build it locally, ad-hoc sign it and swap it into `~/Applications`; until then skip this step.
   - **Encryption key** (once device encryption SEAL ships, docs/26 SEC-5): `cpa-key`, from the same SwiftPM package, is also built in this step, installed at a fixed absolute path and written into the config (cpad only accepts that path and checks its owner, docs/26 §4.5); then `cpa-key init` and `cpa-key selftest` run **in the foreground**. This step always runs; it is not an optional row (docs/26 §4.5, §5.5 item 1). A Mac that already has a key (upgraded or migrated) does not run `init` again.
   - cpad is already up from step 6: when it sees the `cpa-key` path in the config and `keys/seal/` change, it reruns `selftest` and sends `hello` again (docs/26 §4.3); as a safety net this step ends with one more `launchctl kickstart -k` of cpad. In first-run setup, "Connect" stays grey until `selftest` passes (§2.3), so the new Mac's first `hello` already carries its public key. On a Mac upgraded while the device is connected, the next handshake carries the public key, and the device asks "Let 'Mac·8C1D' open the records on this device?" (docs/26 §4.3 "Enrol").
   - If it cannot be built (no `swift`): install `cpa` anyway, and `cpa doctor` explains why; this Mac has no public key, so the device will not enrol it as a recipient. When no enrolled Mac is on the device, it shows "Encryption: off"; with profiles, when no Mac in the current profile has a key, it records in plaintext, marked with "明" (plain), shows "Encryption: Work has no key", and never seals to a Mac of another profile (docs/26 §4.3 "Who to seal to", D8).
8. Print one line: `v0.6.0（commit 1a2b3c4）· cpa 0.6.0 · cpad 0.6.0 · cpa-stt 0.6.0 · app 0.6.0`. If the commit matches the one at the end of the release notes, it is the same code.
9. If there is an app, `open -a` it (if any row is not done, it opens the first-run setup page itself); with no app, run `cpa setup` in Terminal.

No sudo, no change to system settings, no touching `~/.claude` (the hooks are written by a first-run setup row, only after you click).

### 2.3 First-run setup page: one row per item, in order

"First-run setup (N steps left)" at the top of the sidebar **counts only the required rows**; once all of them are done, the item moves into Settings → This Mac. Optional rows sit under "Optional", each with a "Not needed" (`cpa setup skip ID`); once clicked, it stops reminding you.

The data comes from `cpa setup check --json` (§2.6): refreshed when the page opens and on "Check again", and every 3 s while the page is open. While first-run setup is not finished, the app runs `cpa device ls --json` every 10 s even when hidden in the background, and adds a dot to the menu bar icon when it finds a device; once setup is done it stops. This revises docs/24's "no polling while hidden" (§2.5).

| Row | Check (read-only) | Buttons (run only when you click) | Command behind it | Required |
|---|---|---|---|---|
| Background service | Is the LaunchAgent there and really running; does it point at **this** `cpa`'s Python; cpad's version | Not installed: "Install"; `cpa` newer than cpad: "Switch to the new version"; `cpa` older than cpad: one sentence, **no button** | `cpa service install` | Yes (the script already installed it, usually ✓) |
| Notes folder | Not on a cloud drive (all of the checks in `archive/cloud.py`), not in Documents / Desktop / Downloads, writable | "Use ~/CardputerNotes" / "Choose folder…" | `cpa notes dir PATH` (new) | Yes |
| This Mac | Name (`host_label`); migrated from another Mac and still has the old name | "Rename…"; when migrated, prefilled "Mac·8C1D (new)" | `cpa host name -- name` | Has a default name, can be left as is |
| Move settings | Does this Mac have projects | "Import from another Mac's export file…" / "Not needed" | `cpa settings import FILE` (§4.2) | Usage C only |
| Device | The board plugged in (USB list only, no serial port opened); bound or not; is the plugged board the bound one | "Not now" / "Charge only" / "It's mine, connect"; after connecting: "Disconnect"; "New device? Rebind" | `cpa device bind --cand ID` (`--charge`); `cpa device forget` | Yes |
| Home / work | **Appears only when the device reports `prof`** (`cpa status --json` has `device.prof`; it does not look at the firmware version, so it is also right if O7 comes early): which profile the device put this Mac in | No button, shows the exact keys (§3.2) | Chosen on the device | Once the device has profiles |
| Disk encryption | Is FileVault on (`fdesetup status`, no sudo). Once docs/26's `cpa doctor security` (SEC-1) is merged, use it instead and run it once at the end of first-run setup (docs/26 §5.5 item 5): it also checks whether Time Machine is encrypted (if the disk is not connected, "unknown"; if not encrypted, a reminder that backups hold the archive in plaintext, and after device encryption the private key file as well, docs/26 D6) and whether `cpa-key` is there; after device encryption it shows this Mac key's fingerprint (the first 8 hex of `kid`, written `9C1B·27E0`), which matches the System → Encryption row on the device if it is the same key (docs/26 §4.3 "Fingerprint") | When off, only one sentence on where to turn it on; it never turns it on for you | — | Reminder only |
| Optional: local transcription | Is `whisper-cli` installed; is there a model in `$CPA_HOME/models`; does the model link point into Downloads / Documents / Desktop / a cloud drive; has another transcriber claimed the work | "Find model" / "Move it in" / "Choose model file…" / "Turn on" | `cpa-stt setup --json` (new: find only, no link), `cpa-stt setup --move FILE` (new), `cpa-stt use`, `cpa-stt service install` | Optional |
| Optional: Claude Code | Is `claude` found; is the `cpa` path in the hooks this one | "Show changes" → "Write" | `cpa hooks install --json` (new: changes only) → `cpa hooks install --yes` | Optional |
| Optional: other apps | Which clients made the last 50 calls (`cpa mcp audit --last 50 --json`); it does not read other apps' config | "Copy connection settings…" (opens the integrations page) | `cpa mcp setup <client>` | Optional |
| Optional: destinations | Which apps on this Mac can receive (after docs/25 R1) | "Add…": the app first shows its own confirmation, then runs the command (docs/25 §5.7) | `cpa dest add --preset P --key K [--profile X] --yes` | Optional |

- **"Connect" is grey until both the background service and the notes folder are ✓**, with the reason beside it. This way nothing from the device lands in the default folder first. After device encryption, on a Mac with `swift`, "Connect" also waits for `cpa-key selftest` to pass, so the first handshake carries the public key (docs/26 §4.5).
- After device encryption, the "Device" row's explanation adds one sentence: items sealed only to other Macs are not handed to this one; they stay on the device until that Mac collects them (docs/26 §5.5 item 1).
- The default name is `Mac·` plus 4 random hex characters (for example `Mac·8C1D`), written into config.json on first run: it does not contain your real name, nor a profile word such as "home" or "work" (§3.2, D9). This revises docs/23 D10's "empty by default".
- `cpa-stt setup --json` and "Find model" use Spotlight, with their own 2 min timeout; `cpa setup check` itself only looks at `$CPA_HOME/models` and a few known folders.
- What cannot be done in one click gets one sentence only: Claude login and folder trust (cpad never does these for you [src README E_SPAWN]), the system's "Allow accessory to connect", `brew install whisper-cpp`, copying the model file over.

### 2.4 Page sketch

```
First-run setup · This Mac                          2 steps left  [Check again]
✓ Background service   running, version 0.6.0
● Notes folder         ~/CardputerNotes                 [Use this] [Choose folder…]
✓ This Mac             name "Mac·8C1D", the device shows this   [Rename…]
– Move settings        only needed for usage C           [Import…] [Not needed]
● Device               A Cardputer is plugged in, device code 3F7A
                       Once connected, the notes and recordings on the device that have not
                       been sent are stored on this Mac, the ones on the device are deleted,
                       and once stored here there is no way back. See ↑ and R on the device's
                       status bar for how many.
                       This Mac has no projects yet: 1–9 on the device goes empty first,
                       and comes back when you plug into the original Mac again.
                       [Not now]  [Charge only]  [It's mine, connect] (choose the notes folder first)
✓ Disk encryption      FileVault is on
Optional (no nagging)
○ Local transcription  found ggml-large-v3-turbo.bin in Downloads; a cleanup would delete it  [Move it in] [Not needed]
○ Claude Code          hooks not installed               [Show changes] [Not needed]
○ Other apps           no app has read anything yet      [Copy connection settings…] [Not needed]
```

Every symbol comes with words: ✓ done, ● needs your click, ◌ waiting for the device, – not needed or not yet, ○ optional, ✕ error. None of the three device buttons is the default Return-key button.

The "Device" row in each case:

| Case | The row says |
|---|---|
| No board found | "No device found. Common causes: the cable only charges; 'Allow accessory to connect' was not clicked on first plug-in; the device is sitting in Launcher; the device is off (side switch to ON)" |
| One board never connected | As in the sketch above |
| Two boards never connected, different device codes | One row each: "More than one on the desk: unplug the one you don't use, or go by the device code" (from O7 the device shows its own device code on the System page) |
| Two boards with the same device code | "Two boards have the same device code and cannot be told apart: unplug one and look again", no buttons |
| Connected | "Connected 3F7A · firmware v0.6.0 · 12 notes and 3 recordings stored this time" [Disconnect] |
| Connected, then found not to run Cardputer Assistant firmware | "This board is not running Cardputer Assistant firmware (it may be sitting in Launcher)" |
| Connected, then the identity the board reports does not match the USB serial | "✕ The identity this board reports does not match its USB serial; disconnected" |
| Charge only | "This device only charges on this Mac" [Switch to sync] |
| The bound one is absent, another is plugged in | "The plugged device is not the bound one: 7C21. New device?" [Rebind] |

### 2.5 Rules

- **One button runs exactly one `cpa` / `cpa-stt` command** (docs/24 §1.2). A row gives an `op` enum and arguments; the app builds argv from its own verb table and validates first, and never copies any string from the row.
- **Rerunning is harmless**; anything that writes first shows what it will change (hooks show the changes, import lists each item).
- **Timeouts**: 5 s for ordinary commands (docs/24 §5.2); 60 s for `cpa service install` and `cpa notes dir` (they include `launchctl bootout` and wait for cpad to exit); 2 min for "Find model". `cpa setup check` guarantees it returns within 5 s: no Spotlight, no login shell.
- **Revises docs/24 D8**: in first-run setup, and when `cpa` is newer than cpad, `cpa service install` may run with one click. **The command itself decides** whether it is busy: it refuses while cpad is pulling a recording or opening a session (only `--force` lets it through), in Terminal too, not just a greyed-out button. It never downgrades.
- **Revises docs/24's "no polling while hidden"**: only while first-run setup is not finished, `cpa device ls --json` runs every 10 s in the background; it stops once setup is done.
- **These setup actions live only in the CLI and the app, never in MCP**, even when `mcp.json` turns on write tools; a test asserts that MCP's tool list does not contain them (§7.2). If `setup check` is ever shown to MCP, strings reported by the device are stripped first.
- No change to system settings, no network, no ports; `host_id`, `dev` and the USB serial are never shown, only the device code, and after device encryption also this Mac key's 8-character fingerprint (not a secret; used to compare with the device); strings reported by the device (for example `fw`) pass through `display_safe` before going into a row.
- Interface text uses plain words: no cpad, no "connection method", no abbreviations like "↑12 R3"; the device status bar's ↑ and R are mentioned only when it says "look at the device".
- Terminal `cpa setup`: goes through the same table row by row, each row y / N / not needed, no app required.

### 2.6 JSON for the app: `setup/v1`

One row of `cpa setup check --json`:

```json
{"v": 1, "id": "device", "state": "act", "required": true, "blocked_by": ["notes"],
 "title": "插着一块 Cardputer，设备码 3F7A",
 "detail": "连上以后，设备上还没传的笔记和录音会存到这台 Mac，设备上的会删掉，存过来就回不去。",
 "fix": [{"op": "device.bind", "cand": "c7f3a9e01b"},
         {"op": "device.bind", "cand": "c7f3a9e01b", "charge": true}]}
```

- `id` ∈ `service notes name import device profile disk stt hooks mcp dest`; `state` ∈ `ok act wait warn off skip na err` (`x-knownValues`, only ever added to).
- `fix[].op` is only one of the verbs in the table (including `setup.skip`), each argument with its own format: `cand` is `^c[0-9a-f]{8,16}$`; names pass the `host_label` check; paths may only be `~/…` or come from the app's own file picker, and the app checks them again.
- `cand` is a **one-time candidate number** from cpad: tied to that port and serial, void once unplugged or used.
- `title` and `detail` are for people and pass through `display_safe`; they contain no local identity.
- `protocol/setup/v1/` holds the schema, valid and invalid examples and a checker, like `filing/v1`; the Mac app's `Fixtures/` carries a copy with `SOURCE`.

## 3. Recognising the same device, and the device knowing a new Mac

### 3.1 The Mac recognises the device (O3: Mac only, no protocol change)

Today: VID:PID 303A:1001, the device hello's `fw` starts with `cpa-fw/`, and the USB serial equals the bound one [src port_finder.py]. When nothing is bound, the first board that answers gets bound, and `host_id` has already been sent. The change:

1. **No binding means it asks you**: when `device.serial` is empty, cpad does not handshake with any board. **No switch**: old configs that already have a serial keep working; after `cpa device forget` and "Rebind" it is back to asking. Today's test rigs rely on automatic binding, so `tools/hil/harness/rig.py` first reads the serial of the only board from the USB list and writes it into the config, and `tools/sim/cpad_sim.py` writes `SIM_SERIAL` (`tools/sim/simtools/serial_pty.py:17`).
2. **Recognise without talking**: for an unbound 303A:1001 board, cpad only reads the serial from the USB list. It **opens no serial port and sends no hello**. The ESP32-S3's USB serial is its factory MAC [src docs/07:14 states "USB serial same as MAC"; the `dev` in hello is the same MAC without colons as 12 lowercase hex, see `hal_platform.h:42`, `hello.schema.json:58`].
   - **Device code** = the first 4 hex of `sha256("cpa-dev:" + normalised serial)`, uppercase. Normalised = colons removed, lowercased, the same shape as `dev`, so from O7 (or v0.8) the device code the device works out from `dev` on its System page matches the Mac's. It only tells boards apart. It is not a password, and it is not docs/26's per-Mac key fingerprint: that one is 8 hex with a dot in the middle (`9C1B·27E0`), under System → Encryption (docs/26 §4.3 "Fingerprint").
   - Benefits: an unconfirmed board never gets `host_id` and cannot push a single note; the device is not "online" for 15 s for nothing and then drops to "no reply" (any host hello puts the device in Ready, and it falls back after 15 s without data [src link_session.cpp:74-86, 162; protocol_limits.h:62]); no protocol field is added, so the firmware's codec and flow tests are not affected [src test_codec_vectors and test_link_flows replay all vectors].
   - Cost: before connecting, the firmware version and the waiting count are unknown. The interface asks you to look at ↑ and R on the device's status bar.
3. **Candidates** come from `cpa device ls --json`: the device code and a one-time candidate number `cand`, never the raw serial. When two boards share a device code, no `cand` is given. In Terminal, `cpa device bind 3F7A` works only when exactly one board has that code.
4. **Binding**: write the serial, then handshake normally (with `host_id`), and record `device.bound` in the events. At the first handshake check two things: `fw` starts with `cpa-fw/`; `dev` equals the normalised serial, and record it. If they do not match, release the serial port and report an error in the row. After docs/26 SEC-7 (D10) this becomes: the serial is only a candidate at first, and the binding counts only once the device has proven itself in `hello` (docs/26 §7).
5. **Charge only**: `device.mode: "charge"`. cpad knows the board but never opens its serial port; "Switch to sync" or `cpa device forget` undoes it. A Mac that should only charge no longer depends on you remembering.
6. **Disconnect**: the bound row has a "Disconnect" button = `cpa device forget`, back to asking.
7. **New device**: when another board is plugged in after binding, cpad again only reads its serial, and the row says "The plugged device is not the bound one: 7C21. New device?"; click "Rebind" (the old one is replaced, `device.rebound` is recorded).
8. **Hint on the device** (O7 or v0.8, firmware text only, no protocol change): when USB has data but no hello ever arrives, next to "no reply" it says "The Mac is not answering: cpa is not installed, or it is waiting for you to click 'Connect' on the Mac" [tbv: confirm the firmware can tell a charge-only cable from a data cable].

**Result**: F4 is solved. F10 / T4 is **narrowed, not closed**: an unconfirmed board never gets `host_id`; but a board that reports the bound serial (the USB serial is self-reported by the board) still gets it. The device proving itself to the Mac (docs/26 D10, SEC-7, after device encryption) also only stops boards that have never touched this device: a board that has read its NVS has the authentication key and can still impersonate it; the serial only prevents plugging in the wrong board, not impersonation (docs/26 §5.5 item 4). §3.5 item 2, "the Mac proves itself", covers the other direction: someone with `host_id` who impersonates this Mac cannot get or delete sealed items; once this Mac's key is pinned on the device, they cannot get unsealed ones either; for a Mac that has not enrolled a key yet, unsealed waiting items are still protected only by `host_id` as a password (docs/26 §3.1 T-ROGUE, §4.3 "Pinning", §5.1). The interface never calls a binding "verified" (docs/26 §5.5 item 4).

**Not doing**: sending a hello without `host_id` first on every connection, checking `dev`, and then handshaking properly. `dev` is also self-reported, so a deliberately spoofing board reports it correctly anyway; it only stops a board plugged in by mistake. And it costs an extra round on every connection, during which the device would push a few items and then fall back. The check in item 4 already stops wrong boards.

### 3.2 The device recognises the Mac (v0.8, per docs/18, nothing new)

- The device recognises a Mac by `host_id`, stores only the first 16 bytes of `sha256("cpa-host:" + host_id)`, and accepts only ids of ≥16 characters; **binding is chosen only on the device**, and the name the Mac reports is for display only [src docs/18 §3.2, §4.2].
- At a new Mac's first handshake, the device shows docs/18 §5.2's picker: "Later / Home / Work / Neither". **The cursor starts on "Later"**, `;` and `.` move up and down, with a 400 ms guard against accidental presses; choosing home or work brings a confirmation box (§5.3).
- **Key presses** (all counted from "Later"): home = `.` Enter Enter (3 presses); work = `.` `.` Enter Enter (4 presses); the second Enter is on the confirmation box. Once per Mac. With "undecided" waiting items from before the upgrade, it asks once more (docs/18 §5.4, 2–3 more presses).
- **After device encryption, this confirmation box also enrols this Mac's public key**, with no extra press (docs/26 §4.3 "Enrol", §5.5 item 1). Without profiles there is no such box; the device shows a separate card, "Let 'Mac·8C1D' open the records on this device?", with the cursor on "Later", asked once per `host_id`; it can be added later under System → Encryption. **A Mac that is already bound but has not enrolled a key** (bound before the SEAL firmware, or when it had no `cpa-key` yet; this is every v0.8 user) also gets this card: asked once at its first handshake with a public key, and afterwards under System → Encryption → "Add 'Mac·8C1D'" (docs/26 §4.3 "Enrol"). The device enrols at most 4 Macs; a fifth needs one forgotten first (docs/26 §4.3 "Limit").
- On the Mac, the "Home / work" row shows what the device reports in `prof` (docs/18 §10.3); cpad knows it from the next `pong` (≤5 s [src docs/18 §3.7]), and `cpa status --json` adds `device.prof{act, ok, ask}`:

| The device reports | The row on the Mac says |
|---|---|
| `ask: true` (the picker is open) | ◌ Waiting for you to choose on the device: for "Home" press `.` once, then Enter twice; for "Work" press `.` twice, then Enter twice (the second Enter confirms) |
| `ok: []`, `ask: false` | ● The device has not put this Mac in a profile yet, so this Mac receives nothing for now. The device may be locked or recording (the picker appears after unlocking or stopping), or you chose "Later" or "Neither". To choose again: unplug and plug back in; or on the device press Opt+M to open System, press `/` to switch to "Profiles", press `.` twice to move to "This Mac unassigned", and press Enter |
| `ok: ["work"]` | ✓ The device put this Mac in Work. Wrong? On the device: Opt+M → `/` → move to this Mac's row → Enter → "Forget" and confirm, then unplug and plug in again to choose again |
| After device encryption: `seal_chal.kid` is none of this Mac's keys | ◌ The device is asking whether to change this Mac's key, with the cursor on "Keep"; this Mac receives nothing for now. This only happens if you deleted `keys/seal/` yourself, and then choose change on the device; if not, leave it alone: someone else holds this Mac's identity (docs/26 §4.3 "Pinning") |
| After device encryption: `seal_chal.kid` is this Mac's key, but this Mac cannot prove it (`cpa-key` missing or broken) | ✕ The device is waiting for this Mac to prove its key and hands over nothing until then: run `cpa doctor` and look at `cpa-key` |

- **The row does not appear when the device does not report `prof`** (old firmware, or you chose "No profiles"); the last two rows of the table above do not depend on `prof` and still appear after device encryption.
- **The Mac cannot choose for you**: the suggested name never uses profile words such as "home" or "work" (D9), so the name a Mac reports cannot steer you towards a profile (docs/18 T5); and there is no "Mac suggests a profile" field. The device still shows "(name reported by the Mac itself)".
- A rename does not trigger a new handshake right away: the new name goes out with hello the next time the device is plugged in or cpad restarts. Because "This Mac" comes before "Device", the first binding already carries the name.

### 3.3 Before v0.8

| Measure | When | What it stops |
|---|---|---|
| §3.1's "connect only after you click" and "Charge only" (O3) | The next Mac-side release, no wait on firmware | A new Mac with an upgraded cpa does not quietly take everything |
| Minimal profiles brought forward (docs/18 §15 item 9: P8-P, F0, F1, F2, D1, with the simplest confirmation box as the UI) | By default brought forward into the next firmware release 👤 §9.2 item 2 | F1, F3: home items are not handed to work; offline `#3` follows the current profile |
| Write in the README now: before going to another Mac, plug in at the main Mac first and wait until ↑ and R reach 0; a Mac that has not upgraded cpa takes over as soon as the device is plugged in; a Mac that only charges should not run cpad | This PR | Eases F1; `rec_pull: false` only covers recordings, typed items are still handed over [src config/settings.py:64] |

F2 (one meeting half pulled by each of two Macs) cannot happen in usage A: a meeting carries its profile and is handed only to that profile's Mac. Only usage C (several Macs, one profile) needs the device rule "a meeting goes only to the Mac that pulled its first segment, released after 24 h"; not done by default (D8).

### 3.4 Why no pairing key, and no matching codes on two screens

- The device has no Flash encryption: on IDF 4.4, encrypted NVS needs Flash encryption first, and Flash encryption burns eFuses, after which Launcher no longer works [src docs/21:424; docs/18 §8.2]. The USB serial port can also auto-reset into download mode, so a Mac it is plugged into can **read and write** the whole Flash without you pressing anything, and can read running memory over USB-JTAG (docs/26 §2.1). So **a symmetric key stored on the device does not stop the Mac it is plugged into**.
- A public key is different: the device stores only the Mac's public key and makes the Mac prove it holds the matching private key, so the device needs no secret (§3.5; docs/26 §3.2).
- But a Mac that can write the Flash can change the public keys the device stores (add itself to the recipient table), the binding table, or even the firmware itself; no key stops that, and device encryption only protects against passive reading (docs/26 §3.1 T-USB, §4.4). The remedy: sync at home until counts are zero before going to an untrusted computer, and when only charging use a charge-only cable or a data blocker; System → Encryption lists every recipient and fingerprint, so an extra one is visible (docs/26 D17).
- Matching codes on two screens: there is only one USB port and you are right there, so a code match stops little more. The device code (§3.1 item 2) is only for telling boards apart.

### 3.5 Constraints for encryption at rest (taken into the final docs/26)

How the device encrypts is decided by docs/26: SEAL, each file sealed to the Mac's X25519 public key (docs/26 §4.3, D4). The Mac's private key is `$CPA_HOME/keys/seal/<kid>.key` (0600, folder 0700), **not in the Keychain**: `cpa-key` is ad-hoc signed, so its signature changes at every rebuild, and the Keychain would suddenly ask for the login password during a background sync (docs/26 §5.2, D6). Only the small Swift tool `cpa-key` reads this file, and the private key never leaves `cpa-key`; it is not in `cpa export` or `settings/v1`, and only moves with Time Machine and Migration Assistant (docs/26 §4.5, §5.2, D5, D6). This doc **does not define the private key file itself**. The seven items below are what the new-Mac side must satisfy; the final docs/26 has taken them into §4.3, §5.3, §5.5 and SEC-5, and they are written here in its wording:

1. **The public key is accepted only at the moment you agree on the device, and is pinned** (docs/26 §4.3 "Enrol", "Pinning", D21). With profiles this is docs/18 §5.3's binding confirmation box: confirming the binding also enrols the key. Without profiles, and for a Mac that is already bound but has no enrolled key, it is the separate card "Let 'Mac·8C1D' open the records on this device?" (§3.2). After pinning, if a `hello` with the same `host_id` carries no public key, carries a different one, or gives no proof, the device treats it as docs/18's "unassigned": `prof{ok:[]}`, counts reported as 0, no snapshot adopted, no online requests sent, and neither sealed nor unsealed items handed over, until the proof passes or you choose to change on the device. When it carries a different public key:
   - if it proved itself with the old key in the same handshake (the normal rotation by `cpa-key rotate`, docs/26 §5.3): the device changes it directly and says "'Mac·8C1D' changed its key";
   - if it cannot prove it: the device keeps the old one and asks "'Mac·8C1D''s new key (3F2A·61D0) cannot prove the old one. Change?", with the cursor on "Keep". It does not ask you to compare the old fingerprint, because a fingerprint can be matched by generating enough keys.

   This way someone with `host_id` can neither re-seal future records to themselves, nor take unsealed items by "sending no public key".
2. **At every handshake the Mac proves it holds the private key**, shipping in the same release as SEAL (docs/26 §4.3 "Handshake proof", D21): the device's reply to `hello` adds `seal_chal{E′, n, kid}` (a fresh ephemeral X25519 public key per handshake, a 16 B random number, and the `kid` the device pinned for this Mac); the Mac's next `ping` adds `seal_proof[{kid, tag}]`, where `tag` = the first 16 bytes of HMAC-SHA256(HKDF(X25519(sk, E′), info = "cpa-seal/v1 confirm"), n‖dev), computed by `cpa-key prove`, so the private key never leaves `cpa-key`. Before the proof passes, no sealed item is handed over, and this Mac's acks for sealed items and its `file.del` are not accepted; after it passes, only items whose recipients include a `kid` it proved are handed over and accepted. The `pending`, `files.n` and `file.list` the device reports to this Mac also count only those plus unsealed ones, not items sealed only to other Macs (docs/26 §4.3 "Who to hand to"), so this Mac's ↑ can drop to 0. A Mac with a pinned key gets not even unsealed items before its proof (item 1); for a Mac that has not enrolled a key, unsealed waiting items follow docs/18's rules and are still protected only by `host_id`. The device stores no secret, consistent with §3.4. Without this item, someone who stole `host_id` could not open sealed records but could ack them and make the device delete them. It runs the opposite way to docs/26 D10 (the device proving itself to the Mac, SEC-7); the two are independent.
3. The device reports the `kid` it pinned for this Mac in `seal_chal.kid` (the first 8 bytes of `sha256(pk)`, docs/26 §4.3, §5.5 item 6); it is not put into `prof.ok`, because without profiles there is no `prof`, and `prof.ok` is a list of profile names (docs/18 §10.1, §10.3). If it is none of this Mac's keys, raise the alarm "the device holds a key that is not this Mac's" (the table in §3.2). cpad also remembers "this device has sent sealed items" and the `kid`s seen in headers: if it later sends unsealed items, or a header shows a `kid` never seen before ("The device has a new recipient 9C1B·27E0. Is that the Mac you just approved?"), it also raises an alarm in `cpa status` and the Mac app (docs/26 §4.4).
4. **Say clearly how far the protection goes**: device encryption only protects against one passive read (docs/26 §3.1 T-USB, §4.4). A Mac that deliberately tampers with the device can add itself to the recipient table, wipe the table to force the device back to plaintext, or flash firmware, and then gets whatever is recorded afterwards; while plugged in it can also read the period key in memory over USB-JTAG and open the files written in that period (≤1 hour).
5. **Toolchain** (the precompiled libraries of Arduino 2.0.17, docs/26 §4.3 "Which library"): X25519 from the libsodium already linked in; AES-256-GCM from mbedtls's `esp_aes_gcm`, which is hardware AES-CTR plus software GHASH (the S3's AES hardware has no GCM mode); ChaCha20-Poly1305 is in libsodium and kept as the fallback AEAD; neither library has HKDF, so it is built from HMAC-SHA256. Time, heap and stack are measured by docs/26's gate G-ENC.
6. **Field names** follow docs/26 §4.3, §5.5 item 6: `hello.seal{kid, pk, kids, salt}`, `seal_chal{E′, n, kid}` in the reply to `hello`, `ping.seal_proof[]`; no `host_pk`, and no field added to `prof`. Documented only for now; they go into the schema when the `protocol/enc/v1` spec is final.
7. **`host_hw` protects against one thing only**: an honest cpad running on a copied `$CPA_HOME` changes its own identity. It does not stop someone deliberately impersonating (they can change cpad) (docs/26 §5.1). When O4 regenerates `host_id` it uses `token_hex(16)` (32 characters, fitting the firmware's `HostId FixedStr<32>` and `host_id_pattern`'s `{8,32}` [src messages.h:64; limits.json:71]); existing ids are left alone. After device encryption, whenever O4 changes `host_id` or `cpa host reset` runs, `cpa-key rotate` runs too: a new key is made, the old one is kept only to open, never to seal, and never deleted automatically, and the new identity never uses the old key as its current one (docs/26 §5.3, D23). A migrated old private key can still open what was sealed to the old Mac, so before the old Mac is put to another use, run `cpa host reset` on it, so that the two are separated cryptographically (docs/26 §5.3).

The notes folder on the Mac: per ADR 0004 it is open for any app to read, and encryption relies on FileVault and an encrypted Time Machine (docs/26 D1, D13); after device encryption Time Machine also holds the private key file, so the backup should all the more be encrypted (docs/26 D6, §8.2 item 4). First-run setup's "Disk encryption" row tells you, read-only, whether it is on.

## 4. What crosses between Macs, and what never does

### 4.1 Overview

| Thing | Crosses? | How it crosses / why not |
|---|---|---|
| Notes, recordings, transcripts, `events.jsonl`, to-file state, ★, hidden | **No** | Stays on the Mac that received it. Back up with `cpa export`; change Macs with Migration Assistant (§6) |
| `host_id`, `host_hw`, the device serial binding | **Never** | One identity per Mac; a copied one is voided by the hardware fingerprint (§6) |
| Encryption private key | **Not moved with the settings**; moves only with Migration Assistant and Time Machine | A 0600 file under `$CPA_HOME/keys/seal/`, not in the Keychain, not in `cpa export` or `settings/v1` (docs/26 §5.2, D6). A migrated old key only opens, never seals; the new Mac gets a new key, and the old one is never deleted automatically (docs/26 §5.3, D23) |
| The binding table, profiles and waiting items on the device | Go with the device | In the device's NVS and storage |
| 1–9 (key, slot, name) | Can be moved | `settings/v1`; folders written as `~/…`. **The permission mode is not moved; it is always `plan`**; to widen it, `cpa project rm` on this Mac and register again with `cpa project add … --mode` |
| Destinations | Can be moved, with limits | The whole record moves per the destinations schema (moving only the preset name would lose destinations added from your own presets [src docs/25:126]); **`cmd` and link templates you wrote yourself are not moved**; they are listed for you to add on the new Mac yourself; for one that sends text off the Mac, import shows "the text you send goes to …'s cloud" once more, each confirmed separately. **Each Mac's own part starts over on the new Mac** (docs/25 §2.3, §3.1, §4.5): everything starts as `act`, `verified:"draft"` is not moved, and must be answered again with `cpa dest test` on this Mac; the handling app (bundle id and Team ID) is identified again on this Mac, and if it is not installed the destination is not added yet; `profiles` takes this Mac's `served`; paths in `file.*` are stored as `~/…`; `usual` keeps only entries whose both ends (project and destination) were imported |
| Your own presets `$CPA_HOME/presets/*.json` | Not moved | Imported destinations carry their own copied template (docs/25 §2.3); link templates you wrote are not moved anyway |
| `rec_pull`, the transcription engine name, the model file name | Can be moved | Names only; paths are found again on the new Mac |
| `host_label` | Not moved | Each Mac's own name |
| `spawn_env`, `claude_bin`, the `cpa-key` path, hooks, LaunchAgent, MCP snippets | Not moved | Local paths, generated again on the new Mac by the install script and first-run setup |
| The write-tool allowlist in `$CPA_HOME/mcp.json` | Not moved | Opening write tools is your decision on each Mac [src docs/15 §3.6] |

### 4.2 `settings/v1`

**This file decides what cpad will run** (project folders, destinations), so the limits below are security rules, not nice-to-haves.

- **Export**: in the old Mac's app, Settings → This Mac → "Export settings…": saved to a temporary folder, AirDrop opens directly; in Terminal, `cpa settings export FILE`. It first shows "9 project names, 3 destinations, no notes, keys or local identity". The file is 0600. A target on a cloud drive is refused, and as with `cpa export` only `--allow-cloud` in Terminal lets it through [src archive/cli.py:46]. The file records the profiles the exporting Mac served: `served` from `profile.json` (docs/18 §7.1 D1), not the `device.prof` the device reports at that moment; as in docs/18 §7.2, it does not look at the device's state.
- **Import**: `cpa settings import FILE` lists each item it will add:
  - A slot conflict rejects that item with a reason; an identical existing item is skipped; rerunning gives the same result.
  - Folders: after expanding `~`, take the realpath; it must be under `$HOME`, not `$HOME` itself, and not inside a dot folder. If the folder does not exist, the project is **not added yet**; it is listed under "Projects without a folder yet", to be fixed later with "Choose folder…" or another import. Import shows no file picker and creates no folders.
  - The permission mode is always `plan` (even if the file says `auto`, `acceptEdits` or `dontAsk`) [src projects/registry.py:21; docs/05:919 requires `auto` to be set explicitly by the project].
  - Destinations per §4.1: `cmd` is not accepted; `url` is accepted only when the template is identical to a preset shipped with cpad; each one that sends off the Mac is confirmed separately. Each one goes through `cpa dest add`'s checks again (template shape, the reject list of where it may connect, docs/25 §2.3), then each Mac's own part starts over per §4.1: it starts as `act`, the handling app is identified again, and `profiles` takes this Mac's `served`. Paths in `file.*` follow the project folder rule above (realpath, must be under `$HOME`); one that lands on a cloud drive is confirmed again as "sends off the Mac".
  - If the file's `served` differs from this Mac's `served` (home settings imported into the work Mac): warn first; in Terminal type `y`, in the app click "Import anyway" once more. If either side's `served` is empty (before v0.8, or no profile served yet), this rule does not apply.
- You choose how to move the file: AirDrop, a USB stick.
- Contract `protocol/settings/v1/` (schema, examples, checker). **Leak check**: the export must not contain `host_id`, `host_hw`, `dev`, the USB serial (checked with and without colons, upper and lower case), `/Users/<name>` (paths of `file.*` destinations are also written as `~/…`), values of `spawn_env`, `claude_bin`, the `cpa-key` path, or anything under `$CPA_HOME/keys/` (SEAL private keys, the log salt; docs/26 §5.1, §5.2).
- When several Macs of one profile import the same settings, 1–9 and destination letters mean the same on all of them, which also solves F3 for usage C.

### 4.3 Paths not taken

| Path | Why not |
|---|---|
| Syncing the notes folder or `$CPA_HOME` on a cloud drive | F8: two cpads append to the same file, claims are not atomic, plaintext is uploaded; the "Notes folder" row warns about cloud drives (using all of `archive/cloud.py`'s checks, not only iCloud) |
| Syncing `events.jsonl` between two Macs | Two writers, one log, would need a merge protocol; breaks "one log is written by one cpad only" |
| The device as a courier (a new Mac reads the 1–9 cache from the device) | Needs a new device→host message, and protection against a Mac of another profile reading it; the export file is enough |
| Macs discovering each other, LAN sync | Opens ports and uses the network, breaking "data stays on this Mac" (docs/15 §3.12) |

## 5. Packaging and install

### 5.1 Options

| Option | What you do | Signing needed | Gatekeeper | Verdict |
|---|---|---|---|---|
| **A. git checkout + `scripts/mac-install.sh` (the script picks the tag)** | Clone + 1 command | No | Built locally, so no quarantine attribute | **Default** |
| B. Release carries a wheel, `uv tool install <wheel file>`, then `cpa setup` | Download, check, 2 commands | No | Not involved | Fallback for a managed Mac (§5.2); no app |
| C. A formula in a private Homebrew tap (`python@3.12` + pyserial) | `brew install h3qing/tap/cardputer-assistant` | No | Formulae get no quarantine attribute [ext] | Add once the repository is public; no `brew services`, to avoid one more plist |
| D. A Homebrew cask installing the `.app` | 1 command | Needs Developer ID | Homebrew 5.0 deprecated `--no-quarantine` and unsigned casks [ext] | Not doing |
| E. An unsigned `.pkg` | Double-click | Needs Developer ID Installer | Needs "Open Anyway" in Privacy & Security [ext] | Not doing: installs as root, while the LaunchAgent is per user |
| F. Python and cpad embedded in the app, registered with SMAppService | Drag in | SMAppService is unreliable when ad-hoc signed [ext] | Copied to another Mac it gets quarantined | Not doing: breaks docs/24 D8 and §1.2 |

~~No "curl one-liner install": that would let a URL run a script for you, which contradicts "does not curl for you".~~ Revised 2026-10-26: doing it, see the revision at the top and D1.

### 5.2 Default: `scripts/mac-install.sh`

- Steps in §2.2. `--dry-run` only prints the steps; `--dev` allows not being on a tag and a dirty working tree, and the version line says "development version".
- Without `swift` (command line tools too old), skip the app, install only `cpa`, and run `cpa setup` at the end.
- **When a managed Mac does not allow the command line tools or Homebrew** (§9.2 item 4), no git and no gh:
  1. Download `cpad-X-py3-none-any.whl` and `SHA256SUMS` from the Release in a browser, or AirDrop them from another Mac;
  2. `cd ~/Downloads && shasum -a 256 -c SHA256SUMS --ignore-missing` (`shasum` ships with the system);
  3. uv: download the standalone binary from uv's GitHub Release, or use the install command from its website; neither needs the command line tools;
  4. `uv tool install ~/Downloads/cpad-X-py3-none-any.whl`, then `cpa setup` (the Terminal version of first-run setup, no app).

  Do not log in to your own GitHub on a managed Mac, and leave no token there.

### 5.3 What to fix at the root (O0)

1. **Version follows the tag**: at release, `uv version X.Y.Z --project daemon`, changing `pyproject.toml` and `uv.lock` together; `__version__` is read from package metadata instead of hard-coded; CI checks that the tag equals the version. A new version number naturally invalidates uv's cache. cpa-stt reports its version too (`cpa-stt --version` and `status --json`; today it has none [src grep `stt/`]).
2. **The Release carries the Mac side**: release.yml adds `uv build daemon`, and the wheel goes into `dist/` and `SHA256SUMS`; the commit is appended automatically at the end of the release notes.
3. **The README keeps only one install method**: Quick start becomes §2.2; delete `uv sync --project daemon` and `uv run --project daemon cpa service install / hooks install` (they pin the service and hooks to the checkout's `.venv`); daemon/README and #62's release notes change with it.
4. **A Mac-only change still gets a full tag**: release.yml requires `docs/releases/<tag>.md` and builds the firmware [src release.yml:31]; even if the firmware did not change, it is built anyway, and the release notes say "firmware unchanged" (D21).

### 5.4 Rules without a Developer ID

- The command line tools include `swift` and `codesign`; on Apple Silicon ad-hoc signing (`codesign --force --sign -`) is enough to run.
- `cpa-key` is in the same package as the app, also built locally and ad-hoc signed, so its signature changes at every upgrade. That is why its private key is a file and not in the Keychain: an upgrade never pops up a request for the login password (docs/26 §5.2, D6).
- **An app built locally has no quarantine attribute**, so Gatekeeper does not check it. So never copy a `.app` between Macs: AirDrop, downloads and Messages all add a quarantine attribute, and once blocked the only way is "Open Anyway" in Privacy & Security (Sequoia removed Control-click to open) [ext]. To use it, git clone on the new Mac and rebuild locally.
- **No commands are written or suggested** for removing the quarantine attribute or turning off Gatekeeper. When something is blocked, there is one remedy: rebuild locally.
- The install script refuses a checkout with a quarantine attribute (a source archive downloaded by a browser): building locally from it would bypass the check Gatekeeper should have made.
- Model files: `cpa-stt setup` computes SHA-256 once when linking and compares it with the official values listed in docs/22; if it does not match, it shows "model of unknown origin" and still works.
- The LaunchAgent stays a plist + `launchctl bootstrap`, not SMAppService. The return code of `bootstrap` must be checked (today neither cpad's nor cpa-stt's `install()` checks it [src service/launchd.py:79-80; stt/service.py:49-50]).
- Notarisation needs a paid developer account; the free Apple Development identity needs Xcode, which this Mac does not have [meas].

## 6. Recovery

| Case | What happens | First-run setup shows | What you do |
|---|---|---|---|
| **New Mac via Migration Assistant** | Notes, `events.jsonl` and `$CPA_HOME` (including the private key files in `keys/seal/`) all move over; `host_hw` does not match → a new `host_id`, and the log only says "hardware changed" [src docs/18 §4.1]; after device encryption `cpa-key rotate` runs at the same time: the new key is current, and the migrated old key only opens, never seals (docs/26 §5.3, D23); before the first `hello`, O4 renames it to "<old name> (new)" (cut to ≤24 B), so the device never shows two Macs with the same name; the Python the plist points to may be gone | "Background service: the Python it points to does not exist" [Install]; "This Mac: the name is 'Mac·8C1D (new)' for now" [Rename…]; "Home / work: waiting for the device" | Two clicks; choose a profile on the device (3–4 presses). After device encryption, once the new Mac is confirmed it can prove the old `kid`, so waiting items sealed only to the old key are handed over too (docs/26 §5.5 item 2); when the device finds the new Mac also holds a key the old Mac uses, it asks "Forget the old one?", with the cursor on "Keep" (docs/26 §5.3). If the old Mac is no longer used, choose forget, or forget it later under System → Profiles; if the old Mac stays in use for something else (for example as the work Mac), run `cpa host reset` on it first, otherwise the old key on the new Mac can open what is sealed to it (docs/26 §5.3, D23) |
| New Mac without Migration Assistant | Fresh install, empty archive | A fresh table; the "Move settings" row | "Export settings…" on the old Mac, import on the new one; the old archive stays on the old Mac or is archived with `cpa export` |
| Reinstall macOS on the same Mac | Same hardware fingerprint, but `$CPA_HOME` is gone → a new `host_id`; after device encryption the private key is gone too, and the fresh install makes a new key. If `$CPA_HOME` is restored from Time Machine, both the identity and the private key come back (docs/26 §5.4) | As above | Choose again on the device; forget the old entry. After device encryption, before forgetting the device warns "N items can only be opened by this Mac": without a Time Machine backup they can never be opened, and can be discarded (docs/26 §5.4, D22) |
| Giving the Mac away | — | — | `cpa host reset --wipe-keys` (type `y`): a new `host_id`; first it warns "`sealed/` still has N items not opened, and items on the device sealed only to this Mac can never be opened", then deletes all private keys in `keys/seal/` (docs/26 §5.3, §5.5 item 3). If you only put it to another use and do not give it away, use `cpa host reset` without `--wipe-keys`: identity and key both change, and the old key is kept only to open, never to seal. Forget it on the device, and if needed discard what only it can open (docs/26 §5.4) [src docs/18 §4.1] |
| **New device, or the device was wiped** (Launcher wiped NVS, full flash) | The Mac is still bound to the old serial; a v0.8 device has lost its binding table, and the `PROFON` on storage makes it hand over nothing until you bind again [src docs/18 T9]. Device encryption's recipient table and `SEALON` are written to NVS and to every mounted storage: if only NVS was wiped and the internal cpafs is still there, it recovers automatically at boot; if only the SD card's copy is left (the Launcher layout has no cpafs), each Mac recovers its own entry when plugged in and its key is proven; if no entry can be found, the device records in plaintext, marked with "明" (plain), and shows "Encryption: recipient table lost"; a full flash also loses the card's copy (or the card was changed), and each Mac must be approved on the device again (docs/26 §4.3 "Mirror") | "Device: the plugged device is not the bound one: 7C21. New device?" [Rebind] | One Rebind click per Mac; choose a profile once per Mac on the device. What had not synced on the old device is lost with it (T7) |
| **New SD card** | The binding is in NVS and unaffected [src docs/18 §3.2]; waiting items and recordings on the old card stay there; `PROFON` is written when the new card is mounted | The "Device" row adds "No waiting items on the card" | To get what is on the old card, put it back and plug in at the Mac of the matching profile |
| An old card put in another device | The `PROFON` on the card turns on that device's profile mode, and items with a profile mark wait for its bindings [src docs/18 principle 8]. After SEAL, sealed items on the card are handed only to a Mac that proves one of the recipient keys: if those Macs have not enrolled on the other device, they cannot be handed over | — | To get them, put the card back in the original device |
| Reading the card on a Mac with a card reader | Plaintext (before SEAL); after SEAL only ciphertext and metadata (docs/26 §3.1 T-CARD) | — | Not supported as a sync path |
| Versions mismatch after an upgrade | The versions or paths of `cpa`, cpad, the hooks and the cpa-stt service differ | "Background service: 0.5.0 is running, 0.6.0 is installed" [Switch to the new version]; when the installed one is older than the running one, one sentence only | One click |
| The model file was moved | `model_missing` | "Local transcription: the model cannot be found" [Find model] | One click; afterwards use "Move it in" to put it in `$CPA_HOME/models` |

## 7. Security and privacy

### 7.1 Rules

| Rule | How |
|---|---|
| An unconfirmed board gets nothing | Recognise without talking (§3.1): no serial port opened, no `host_id` sent, nothing received |
| Impersonating the bound board (reporting the same serial) | **Narrowed, not closed**: it still gets `host_id`. The device proving itself to the Mac (docs/26 D10, SEC-7) also only stops boards that have never touched this device; after SEC-7 the binding counts only once the serial is verified (§3.1 item 4); the interface never says "verified" (docs/26 §5.5 item 4) |
| The device trusts only Macs it chose itself | docs/18 §3.2, §4; there is no field for a profile reported by the Mac |
| The public key is pinned at the moment of confirmation on the device | §3.5 items 1, 2; docs/26 §4.3, D21: changing keys needs proof with the old key, otherwise the device asks with the cursor on "Keep"; each handshake proves the private key, and only then are sealed items handed over and acks and deletes accepted; after pinning, no public key or no proof means "unassigned", and nothing is given |
| Import never widens permissions | §4.2: always `plan`, folders under `$HOME`, no `cmd` or self-written links moved, anything sending off the Mac confirmed again, a different profile confirmed again |
| Exports carry no secrets | `settings/v1`'s allowlist and leak check (§4.2), which also checks `keys/`; per docs/26 D6 the private key is a 0600 file under `$CPA_HOME/keys/seal/`, not in the Keychain, and in no export |
| Setup actions stay out of MCP | §2.5; asserted by tests |
| Writes happen only after you click | First-run setup is one button per command, showing what changes first; the command itself decides whether it is busy |
| Nothing loosens the system | No system settings changed, Gatekeeper not turned off, no quarantine removed, no sudo, no Accessibility permission |
| No local identity shown | Only the device code, and after device encryption the key's 8-character fingerprint (public, for comparing with the device); `host_id`, `dev` and the USB serial never go into `setup/v1`, logs or MCP output |
| File permissions | `$CPA_HOME` 0700, contents 0600 (the `keys/seal/` folder 0700, private keys 0600, docs/26 §5.1); export files 0600 |
| Identity follows the hardware | `host_hw` brought forward to O4; it only stops an honest cpad from using a copied identity (§3.5 item 7); after device encryption, changing identity (`host_hw` mismatch, `cpa host reset`) also changes the key, and the old one only opens, never seals (docs/26 D23) |
| The notes folder stays off cloud drives | `cpa notes dir` refuses cloud paths; only `--allow-cloud` in Terminal lets them through (the app never adds it) |
| Remaining risks | A Mac that deliberately reads the device can read all plaintext not yet synced (docs/18 T6), until SEAL; SEAL also only protects against passive reading: a Mac that can write the Flash can change the recipient table and the firmware and still get what is recorded afterwards, and while plugged in can read memory over USB-JTAG (docs/26 §3.1 T-USB); for a Mac without an enrolled key, unsealed waiting items are protected only by `host_id`; a migrated old private key can open what was sealed to the old Mac, so run `cpa host reset` before the old Mac is put to another use (docs/26 §5.3); a Mac that has not upgraded cpa still takes over when the device is plugged in; debug firmware on the device does not count (docs/18 §8.1) |

### 7.2 Security tests: part of docs/26's M-SEC

Each work package first writes its own security tests inside the package (RED); M-SEC is the cross-package adversarial test, run in two rounds per docs/26 §6: the baseline round (SEC-3, now) and the encryption round (SEC-6, after SEAL). Results go into `docs/gates/sec.md`, and the automated part then runs at every release. This doc adds two parts to M-SEC.

**Part 1: Mac only, runs after O0–O6 are merged, must all pass. It does not test encryption.**

It is not in the list of docs/26's baseline round (SEC-3): the baseline round runs now, when O0–O6 are not done yet. The results still go into `docs/gates/sec.md`.

1. **Unknown board**: the simulator plays an unbound board; assert that cpad never opens its serial port, and that `host_id` and `host_label` never appear on the wire; after 60 s the device's `pending` and `files.n` are unchanged.
2. **Spoofing**: a board with the same serial as the bound one but a different `dev` → disconnected with an error; one with the same serial and the same `dev` → **passes today**, recorded as a remaining risk.
3. **Old config**: a config.json without a serial → always asks.
4. **Clone**: copy `$CPA_HOME` to another temporary HOME with a fake IOPlatformUUID → identity changes, the name becomes "<old name> (new)" before the first `hello`, and the "Name" row asks you to change it. There is only one USB port, so the two connect **one after the other**.
5. **Privilege escalation through import**: an imported file containing `auto`, a `cmd` destination, a self-written `url` template, a path outside `$HOME` or in a dot folder, `..`, a symlink escaping `$HOME`, or another profile → all rejected, lowered to `plan`, or confirmed again.
6. **Leaks**: export every combination of settings; the leak check finds nothing (including anything under `$CPA_HOME/keys/`, docs/26 §5.2); invalid files (too long, control characters, values of unknown fields) are all rejected.
7. **MCP**: the tool list has no setup action (`mcp/catalog.py`), even with write tools on.
8. **Permissions and install**: permissions of `$CPA_HOME`, export files and the LaunchAgent plist; the install script never calls sudo and never writes `~/.claude`, and uses the network only to fetch the code and pinned dependencies (`git fetch --tags` for `--upgrade`, `uv build`, `uv tool install`); the first-run setup page and `cpa setup check` never use the network; a checkout with a quarantine attribute is rejected.
9. **App**: the locally built app has no `com.apple.quarantine`; `codesign --verify --strict` passes; docs/24 §8.1's rule scan (no `URLSession`, `cpad.sock`, `/bin/sh`).

**Part 2: after v0.8 and SEAL, runs in M-SEC round two (the encryption round, docs/26 SEC-6).**

1. The device trusts only Macs it chose: three fake Macs on the v0.8 simulator (docs/18 §12.4).
2. Public key replaced: same `host_id`, a different public key, no proof with the old key → treated as "unassigned", gets nothing (no unsealed items, snapshot or online requests either); the device asks for confirmation with the cursor on "Keep", and the Mac's "Home / work" row says "the device is asking whether to change the key"; a rotation proven with the old key in the same handshake → changed directly, with a one-line notice (docs/26 §4.3 "Pinning", §6.2 S1-d).
3. A Mac without the private key: only `host_id` → the proof fails, it gets no sealed item, and its acks and `file.del` for sealed items have no effect (docs/26 §6.2 S1-d). **Same `host_id`, `hello` without `seal`**: if a key is already pinned, it is "unassigned" and gets nothing; if no key is enrolled yet, unsealed waiting items follow docs/18's rules. Items sealed only to other Macs are not counted in its `pending` and `files.n`.
4. No plaintext body on the device or the SD card (docs/26 §6.3 S2-a, S2-b).
5. **Migration** (docs/26 D23): copy `$CPA_HOME` to another temporary HOME with a fake IOPlatformUUID → `cpa-key rotate` ran along with the identity change, the two HOMEs share no current key, and the old key only opens, never seals; the new one reports "<old name> (new)" in its first `hello`; after the binding is confirmed it can prove the old `kid` and collect waiting items sealed only to the old key; the device asks "Forget the old one?", with the cursor on "Keep" (docs/26 §5.3).
6. **Giving away and reuse** (docs/26 §5.3, §5.5 item 3): `cpa host reset` changes both identity and key, and the new identity's current `kid` is not in the device's table; after `--wipe-keys` no private key is left in `keys/seal/`, after a warning about N items that can no longer be opened and typing `y`.
7. **A bound Mac without an enrolled key** (docs/26 §4.3 "Enrol"): a Mac bound with profiles, at a time when it had no `cpa-key`, at its first handshake after `cpa-key` is installed: the device asks "Let … open the records on this device?" with the cursor on "Later"; if Later is chosen, it does not ask again, and "Add" under System → Encryption still works.

## 8. Phases, work packages, tests

Failing tests always come first (RED → GREEN → REFACTOR). O0–O5 need no device; they use the existing fake serial port and simulator.

### 8.1 Work packages

| Package | Content | Touches | Code / tests (lines) [inf] | Depends on |
|---|---|---|---|---|
| **O0 Release hygiene** | `uv version` follows the tag, checked in CI; the Release carries the wheel and the commit; README and daemon/README keep one install method; cpa-stt reports its version | `daemon/pyproject.toml`, `daemon/uv.lock`, `cpad/__init__.py`, `stt/`, `.github/workflows/{ci,release}.yml`, README | 60 / 80 | None |
| **O3 Connect only after you click** (right after O0) | No serial means it asks (no switch); recognise boards from USB only, device code, one-time candidate number; `cpa device ls / bind / forget --json`, `--charge`; check `fw` and `dev` after binding; new device; `rig.py` and `cpad_sim` write the serial first. **No change to the protocol or vectors** | `device/port_finder.py`, `_bind` in `app.py`, `control/server.py`, `cli/commands.py`, `config/settings.py`, `tools/hil/harness/rig.py`, `tools/sim/cpad_sim.py` | 380 / 500 | O0 |
| **O2 First-run setup** | Eleven row checks (pure functions, input is the probed facts); `setup/v1`; Terminal row by row; `cpa setup skip`; `cpa notes dir` (refuses once the archive has content, refuses cloud drives, restarts both services after a change); `cpa hooks install --json`; `cpa-stt setup --json`, `--move`, model location warnings and hashes; busy check for `service install`, checking bootstrap and Login Items (both for cpad and cpa-stt); `cpa status --json` adds `notes_dir`, `service{installed, running, program_ok}`, `device.code`, `device.prof`; the "Disk encryption" row | New `cpad/setup/`, `cpad/cli/setup.py`, `protocol/setup/v1/`; changes `cli/main.py`, `cli/hooks.py`, `service/launchd.py`, `stt/cli_setup.py`, `stt/service.py` | 900 / 1,100 | O0; O3's `device ls --json`; `cpa host name` (docs/23 P1-B; if not merged, done here first) |
| **O1 Install script** | §2.2's steps; pick the tag, then run itself from the tag; `--upgrade`, `--dry-run`, `--dev`; quarantine and working tree checks; a new out-dir and constraints; restart installed services; a test with a path containing spaces | New `scripts/mac-install.sh`, `scripts/tests/` | 260 / 240 | O0; the `service install` fix in O2 |
| **O4 Identity follows the hardware** | docs/18 §4.1's `host_hw` and `cpa host reset` brought forward; new ids use `token_hex(16)`; default name `Mac·XXXX`; when the hardware changed, rename to "<old name> (new)" (≤24 B) before the first `hello`, and the name row asks you to change it; after SEAL, changing the id (`host_hw` mismatch, `cpa host reset`) also changes the key, `--wipe-keys` (O8, docs/26 §5.3, D23) | `config/host.py`, `config/settings.py`, `cli/` | 140 / 200 | None |
| **O5 Moving settings** | `cpa settings export / import`, `settings/v1`; all of §4.2's security rules; the leak check; profiles recorded and compared by `served`; the destination part waits for docs/25 R1: rerun `dest add`'s checks; `act`, the handling app and `profiles` start over on the new Mac | New `cpad/settings_io/`, `protocol/settings/v1/` | 460 / 580 | The destination part waits for `feat/destinations` |
| **O6 Mac app first-run setup** | docs/24 gains a "First-run setup" screen and Settings → This Mac (with "Export settings…" and AirDrop); drawn from `setup/v1`; its own `op` → argv table; every state of the device row; "Not needed"; background device checks and the menu bar dot while first-run setup is unfinished; screenshot states | `mac/Sources/{CardputerCore,CardputerUI}`, `mac/Fixtures/` | 750 / 550 | O2, O3; docs/24 v1 |
| **O7 Device side** | Minimal profiles brought forward (docs/18 §15 item 9); "Device code" on the System page; the "The Mac is not answering" hint | See docs/18 §13 | Per docs/18 | §9.2 item 2 |
| **O8 Wiring up device encryption** | §3.5's seven items are already in docs/26 (§4.3, §5.3, §5.5, SEC-5) and need no more writing. When SEAL ships, this side wires up: install script step 7 builds `cpa-key`, runs `init` and `selftest` in the foreground, and finally kickstarts cpad (§2.2); "Connect" waits for `selftest` to pass (§2.3); when O4 changes `host_id` or `cpa host reset` runs, run `cpa-key rotate`, and `--wipe-keys` runs `cpa-key wipe` (§3.5 item 7, §6); the "Device" row adds one sentence that items sealed only to other Macs stay on the device (§2.3); the "Home / work" row adds the two `seal_chal.kid` cases (§3.2); the "Disk encryption" row switches to `cpa doctor security` and shows the key fingerprint | `scripts/mac-install.sh`, `config/host.py`, `cpad/setup/` | Per docs/26 SEC-5 | docs/26 SEC-5 (SEAL); the "Disk encryption" part only waits for SEC-1 |
| **M-SEC new-Mac part** | §7.2 | `docs/gates/sec.md` | — / 350 | Part 1: O0–O6; part 2: v0.8 + SEAL + O8 |

**Total** [inf]: O0–O5 about 2,200 lines of code and 2,700 lines of tests; O6 +750 / +550; the M-SEC new-Mac part about 350 lines of tests.

```
README reminder ─ this PR
O0 ─► O3 (connect only after you click, first) ─► O2 ─► O6 ─► M-SEC new Mac part 1
O0 ─► the service install fix in O2 ─► O1
O4 (independent)   O5 (the destination part waits for docs/25 R1)
§9.2 item 2 answered "early" ─► O7 (docs/18 P8-P, F0, F1, F2, D1)
docs/18 v0.8 ─► docs/26 SEC-5 (SEAL) ─► O8 ─► M-SEC new Mac part 2 (docs/26 SEC-6 encryption round)
```

### 8.2 Tests

- **Recognising boards** (fake USB list and fake serial port): when unbound, cpad never opens the port (the fake serial port's open count is 0); the device code worked out from the normalised serial equals the one from `dev`; `cand` becomes void when unplugged or used; two boards with the same code get no `cand`; `device ls --json` contains no raw serial; after binding, the first hello checks `fw` and `dev`; a charge-only board is never opened; an old config with a serial is unchanged byte for byte; an old config without a serial asks.
- **`setup check`**: each combination of facts (service not installed / wrong Python / older version / newer version / pulling a recording / switched off in Login Items; notes folder in iCloud, CloudStorage, Dropbox, `~/Documents`; `whisper-cli` missing, model missing, model link into Downloads, claimed by WhisperWoof; hooks pointing at an old path; no "Home / work" row when the device does not report `prof`, and the row present when O7 firmware reports it) → fixed rows; the JSON passes `check_setup.py`; all `invalid/` examples are rejected; a fake Runner asserts no call to `mdfind` and no login shell.
- **`service install`**: `bootstrap` fails → non-zero exit and a reason code; refuses when busy, `--force` lets it through; never downgrades; the same for cpa-stt's.
- **`notes dir`**: refuses once the archive has content; refuses cloud drives; restarts both services after a change (fake `launchctl`).
- **cpa-stt**: warns when the model link points into Downloads / Documents / Desktop / a cloud drive; `--move` on the same volume is a rename; `setup --json` does not link and does not read stdin; a hash mismatch shows "unknown origin".
- **`host_hw`**: an upgrade keeps the id; a mismatch makes a new id (32 characters) and the log has neither old nor new value; the name row asks you to change it; `host reset` needs `y`; the name in the first `hello` is already "<old name> (new)"; after SEAL a mismatch or `host reset` calls `cpa-key rotate` (a fake `cpa-key` asserts the arguments), and the old key file is not deleted; `host reset --wipe-keys` first reports N items and needs `y`, then calls `cpa-key wipe` (docs/26 §5.3, D23).
- **Settings**: export–import round trip; the leak check (including `keys/`, the `cpa-key` path, absolute paths in `file.*`); all escalations of §7.2 part 1 item 5 rejected; slot conflicts, missing folders (not added yet), unknown fields, `v:2`; importing twice gives the same result; profiles compared by both sides' `served`, not compared if either is empty; imported destinations are always `act`, `profiles` is this Mac's, and not added if the handling app is missing.
- **Install script**: shellcheck; the `--dry-run` steps under a temporary HOME; stops at the right step without `uv` / `swift`; runs from a path with spaces; a checkout with a quarantine attribute is rejected; after `--upgrade` switches tag, the new script is what runs; the installed `cpa --version` equals the tag.
- **Mac app** (as in docs/24 §8): `setup/v1` examples parse; the `op` → argv table; no button when `cpa` is older than cpad; screenshots of every state of the device row and of first-run setup; background checks stop once first-run setup is done.
- **End to end, "second Mac"**: two temporary `CPA_HOME`s + a simulated device (docs/18 §12.4's three-fake-Mac method): A binds and takes the items; B is plugged in and never opens the serial port before confirmation; after confirmation it gets the items; with v0.8 firmware B only gets its own profile's.
- **Real device 👤**: in a new macOS user, time docs/07 item 7 (≤15 minutes), and add a new item: "second Mac from clone to all required rows green ≤10 minutes, excluding copying the model".

## 9. Decisions, and what needs your call

### 9.1 Decisions

With no reply, the defaults apply.

| # | Decision | Default (recommended) | Alternatives | Why | See |
|---|---|---|---|---|---|
| D1 | Install channel | **Revised 2026-10-26**: one-line install with `scripts/install.sh`, from release assets (the wheel, pinned pyserial, the app zip), each file checked against the API `digest` and `SHA256SUMS`; running it again is the upgrade; for the private repository use gh or `GITHUB_TOKEN`. Previously: git checkout + `scripts/mac-install.sh`, the script picks the latest tag itself, `--upgrade` upgrades; add a Homebrew formula once public | Release wheel; cask; `.pkg`; cpad embedded in the app | No Developer ID; locally built things are not quarantined; the private repository needs auth on every path | §5.1 |
| D2 | Version number | Follows the tag (`uv version`), checked in CI; the script installs that one wheel from a new temporary folder; cpa-stt reports its version too | uv's `cache-keys` | With 0.1.0 never changing the old code gets installed again, and `--version` is useless | §5.3 |
| D3 | First-run setup interface | The public command `cpa setup check --json` (`setup/v1`), returning within 5 s; used by both the Mac app and Terminal | The app reads `$CPA_HOME` itself to check | docs/24 §1.2; any app can build the same page | §2.6 |
| D4 | Who installs the background service | The install script; the app can install it with one click in first-run setup and when `cpa` is newer than cpad; the command checks busyness itself, checks it really runs, never downgrades | Copy-the-command only | On a new install no service is running; with the busy check inside the command, Terminal is safe too | §2.2, §2.5 |
| D5 | The Mac recognises the device | Without talking, reading only the USB serial; connect only after you click; no serial always means asking, no switch; plus "Charge only" and "Disconnect" | Probe with a hello without `host_id`; today's silent first come, first served | A probe is also a handshake: the device pushes notes, is "online" for 15 s for nothing, and the protocol has to change | §3.1 |
| D6 | Device code | First 4 hex of `sha256("cpa-dev:" + normalised serial)`, uppercase; the Mac shows it first, the device's System page with O7 (or v0.8); it is not the same thing as docs/26's 8-character key fingerprint (`9C1B·27E0`) | None; the last 4 of the serial | Tells boards apart without leaking the factory MAC; not a password | §3.1 |
| D7 | The device recognises the Mac | Per docs/18 v0.8: chosen on the device, stores a `host_id` hash; the Mac shows the exact keys and how to change; no pairing key over USB, no code matching | Profile chosen on the Mac; codes matched on two screens | What the Mac says about itself does not count | §3.2, §3.4 |
| D8 | Several Macs before v0.8 | Minimal profiles brought forward into the next firmware release (docs/18 §15 item 9); D5 and the README reminder at the same time; "a meeting follows the first Mac" is not done | Wait for the full v0.8 | The voice memo asked for several Macs; under usage A a meeting is never split anyway | §3.3 👤 |
| D9 | This Mac's name | Default `Mac·` plus 4 random hex (revises docs/23 D10's "empty by default", docs/26 §4.4 follows); you are encouraged to rename it to something recognisable, but not to a profile word such as "home" or "work" | Default to the macOS computer name; buttons offering "Home" and "Work" directly | Computer names often contain a real name; profile words steer you towards a profile (docs/18 T5) and get mixed up with the "Home / work" row | §2.3, §3.2 |
| D10 | `host_hw` | Brought forward to O4, Mac only; new ids are 32 characters; only stops an honest cpad from using a copied identity; on mismatch a new `host_id`, the name becomes "<old name> (new)" before the first `hello`, and after SEAL `cpa-key rotate` at the same time, with the old key only opening, never sealing (docs/26 §5.3, D23) | With v0.8 | The earlier the fingerprint is written, the fewer clones Migration Assistant produces; the new identity never uses the old key as current; the device never shows two Macs with the same name | §3.5, §6 |
| D11 | What crosses between Macs | Data does not; identity never; the SEAL private key is in no export and moves only with Migration Assistant and Time Machine, where it then only opens, never seals (docs/26 D6, D23); settings (1–9, destinations, `rec_pull`, engine name) can be moved | Cloud-syncing the notes folder; syncing `events.jsonl` | One log, one writer; nothing in the cloud | §4.1 |
| D12 | How settings move | `settings/v1`: projects always `plan`, folders must be under `$HOME`, missing ones not added yet; destinations move whole but without `cmd` and self-written links, those sending off the Mac confirmed again, and each Mac's own part (`act` / `draft`, the handling app, `profiles`) starts over on the new Mac; profiles compared by both sides' `served`, confirmed again when different; the old Mac's app has "Export settings…" with direct AirDrop | Device as a courier; iCloud; moving preset names only | This file decides what cpad runs; moving preset names only loses things | §4.2 |
| D13 | New Mac | Migration Assistant; the identity is replaced via the hardware fingerprint, and after SEAL a new key too, the old one only opening, never sealing; the device asks about the binding once more, and after confirmation the new Mac can collect waiting items sealed only to the old key (docs/26 §5.3, §5.5 item 2, D23); the name changes automatically to "<old name> (new)" first and the name row asks you to change it; first-run setup fixes the service; before the old Mac is put to another use run `cpa host reset`, and `--wipe-keys` when giving it away | Fresh install + import settings | The archive and history move together; the least effort | §6 |
| D14 | New device / device wiped | The "Device" row becomes "New device?", one click to rebind; a v0.8 device that lost its bindings hands over nothing | Make you run `cpa device forget` | Today you only see "device not connected" with no reason | §6 |
| D15 | Local transcription on a new Mac | One row: find model / move it in / choose file / turn on; the model should live in `$CPA_HOME/models` or another stable place, with a warning when it points into Downloads / Documents / Desktop / a cloud drive; a wrong hash is marked "unknown origin"; never downloads | `cpa-stt` downloads it itself | docs/22's "no downloads" stays; a Downloads cleanup breaks the model | §2.3 |
| D16 | Hooks and MCP | Hooks show changes before writing; MCP only copies connection settings, with status from `cpa mcp audit --last 50 --json`, never reading other apps' config; setup actions never go into MCP | Run commands like `claude mcp add` | Changes to someone else's config need your look first; an AI client cannot bind devices or import settings for you | §2.3, §2.5 |
| D17 | Encryption | Per docs/26 (D6, D21, D23): the private key is a 0600 file under `$CPA_HOME/keys/seal/`, not in the Keychain, never exported; the install script always installs `cpa-key` and runs `init` and `selftest` in the foreground, and "Connect" waits for it to pass; the public key is enrolled and pinned when you agree on the device; changing keys needs proof with the old key, otherwise the device asks with the cursor on "Keep"; in the same release as SEAL, every handshake proves the private key, and only then are sealed items handed over and acks and deletes accepted; after pinning, no public key or no proof means "unassigned" and nothing is given; the device reports the `kid` it pinned for this Mac in `seal_chal.kid` (not in `prof.ok`); on the Mac, FileVault and an encrypted Time Machine, checked read-only by first-run setup | This doc defines its own private key file; Keychain (the alternative in docs/26 D6) | No conflict with docs/26; stops a stolen `host_id` from re-sealing or deleting sealed records | §3.5 |
| D18 | Security testing | No separate milestone; part of docs/26's M-SEC: part 1 (Mac, after O0–O6, must all pass, no encryption tests), part 2 (after v0.8 + SEAL, in encryption round SEC-6); each package first writes its own security tests | This doc's own O-SEC | docs/26 already set M-SEC's two rounds (baseline, encryption); two docs running their own would duplicate | §7.2 |
| D19 | Optional rows | Count only the required ones; optional ones are folded away, each with "Not needed" (`cpa setup skip`); no folder pickers on import | Nag on every row | Things you do not use should not hang around | §2.3 |
| D20 | A hint when the device is plugged in | While first-run setup is unfinished, check for devices every 10 s in the background and add a dot to the menu bar, stopping once done; a v0.8 device writes the reason next to "no reply" | Never poll; always poll | A device plugged into a new Mac with no reaction on either side is the worst way to get stuck | §2.3, §3.1 |
| D21 | A Mac-only release | Still a full tag, and the firmware is built even if unchanged | Change release.yml to allow Mac-only releases | One fewer release flow; the firmware build is automatic anyway | §5.3 |

### 9.2 Needs your call

1. **How you use it**: **A** one at home, one at work (default); **B** one laptop carried back and forth; **C** several Macs that are all yours (for example a desktop plus a laptop), one profile. 👤 A: first-run setup has a "Home / work" row, and settings usually need not move. B: most of this doc does not apply; back to docs/18 P8-B1. C: choose the same profile on all of them, import the same settings, then consider "a meeting follows the first Mac" (D8).
2. **Bring minimal profiles forward into the next firmware release?** (D8, docs/18 §15 item 9; not needed if item 1 is B) 👤 Default **bring forward**: you are already using, or about to use, a second Mac, and until then plugging into the second Mac takes the home items.
3. **Will the repository be public?** 👤 Default **stay private**: a new Mac first installs gh and runs `gh auth login`, then §2.2's one command. If public, no gh, and add a Homebrew formula.
4. **Can a managed Mac install the command line tools, Homebrew and cpad?** 👤 Default **yes**. If the command line tools or Homebrew cannot be installed: take §5.2's wheel fallback, with no Mac app and first-run setup in Terminal. If cpad cannot be installed at all: that profile's items stay on the device (docs/18 §14).

"Which side 'all data encrypted' refers to" is already asked in docs/26 §8.2 and is not asked again here.

**Reply template** (in the order 1–4; unanswered items take the default): `Reply: A / early / private / yes`

## Appendix: how the review comments were handled

All comments from the three reviews (user, security, feasibility) were accepted; below are only the changes and how disagreements were settled.

| Source | Comment | How it was handled |
|---|---|---|
| User 1 | "Press Enter twice" is wrong; nothing said what to do after "Later" or a wrong choice | Exact keys written out; the "Home / work" row splits into three cases by `prof`, each with how to change (§3.2) |
| User 2, feasibility 8 | The row order leads you to bind the device first: 1–9 is cleared, no name, data lands in the default folder | New order: service → notes folder → name → move settings → device → home / work; "Connect" is grey until service and notes folder are ✓; the name has a default that reveals nothing about you; with no projects it says 1–9 will go empty; `cpa notes dir` refuses once the archive has content |
| User 3, feasibility 3 | "One command" was not honest; `git pull` fails on a tag; rerunning is not upgrading | The script picks the tag and runs itself from the tag; `--upgrade`; the script installs the service and restarts installed services; one README block with prerequisites; §2.1 now counts honestly in two columns, "minimum / everything". The two reviews' upgrade approaches are combined: the user review's `--upgrade` is in the script, and the feasibility review's `git fetch && git checkout` is written as the manual equivalent |
| User 4 | A wrong click cannot be undone; warnings use device abbreviations | Plain words; none of the three buttons is the default; "Charge only"; the bound row has "Disconnect" |
| User 5, security 11 | Two rows are both called "home / work"; the name button uses profile words | **Disagreement**: the user review suggested "home Mac / work Mac", the security review suggested no profile words. Settled per security: the default name is `Mac·XXXX`, no profile words suggested; the "Home / work" row shows both key sequences, so the Mac need not guess which you want, and a wrong choice is handled by the third state |
| User 6 | Plugging in the device gets no reaction on either side | While first-run setup is unfinished, background device checks and a menu bar dot; "No device found" lists common causes; a v0.8 device shows why "The Mac is not answering". The earlier request "show a hint on receiving `probe:true`" no longer applies, because there is no probing any more |
| User 7 | Optional rows keep nagging; import pops up many folder dialogs | Only required rows count; optional ones folded away, each with "Not needed"; projects with missing folders are not added yet, no dialogs |
| User 8 | Exporting on the old Mac needs typing a command | Settings → This Mac → "Export settings…", saved to a temporary folder with direct AirDrop |
| User 9 | An AirDropped model in Downloads breaks the link | "Move it in" (`--move`); warnings for links into Downloads / Documents / Desktop / cloud drives; tests |
| User 10 | After migration two Macs have the same name | When O4 detects new hardware, the name row asks you to change it, prefilled with "(new)" |
| User 11 | The managed-Mac fallback does not work | Written as: download or AirDrop the wheel in a browser → check with `shasum` → `uv tool install` → `cpa setup`; no GitHub login on a managed Mac |
| User 12, 13 | Unclear terms; the decisions asked too much | Usages A / B / C defined in §0 and used in order; §0 and the interface avoid developer words; questions cut to 4, with a reply template; item 2 says it depends on item 1 |
| User minor points | Before v0.8 the device cannot show the device code; old firmware should hide "home / work"; paths with spaces | Ask you to look at ↑ and R on the status bar; old firmware does not show that row; script tests run from a path with spaces |
| Security 1 | Import can escalate privileges and install commands | All of §4.2 adopted: `plan`, realpath under `$HOME`, no `cmd` or self-written links moved, anything sending off the Mac confirmed again, exports refuse cloud drives, profiles recorded, the leak check covers serials in all formats and more, M-SEC gains escalation cases |
| Security 2, feasibility 10, 11 | `host_id` is still a password; `host_pk` can be replaced; "closes T4" overstates it | Changed to "narrowed"; the public key is pinned when you agree on the device, and changing keys needs proof with the old key or confirmation on the device (docs/26 D21); in the same release as SEAL, every handshake proves the private key; the device reports the `kid` it pinned for this Mac in `seal_chal.kid`; new ids are 32 characters; `host_hw` stated to stop only an honest cpad; toolchain constraints handed to docs/26 |
| Security 3, feasibility 6 | Old configs still bind silently; defaulting new installs to `confirm` breaks test rigs and cannot tell a "new install" | **Disagreement**: the security review suggested keeping an explicit `auto`, the feasibility review suggested removing the switch. Settled on the simpler and safer one: no switch, and no serial always means asking; `rig.py` and `cpad_sim` write the serial first; a Mac that has not upgraded still binds silently, which is recorded as a remaining risk and is also a reason for D8 |
| Security 4, feasibility 1, 2 | A probe is itself a handshake: the device pushes notes and is "online" for 15 s for nothing; a `probe` field turns firmware CI red; the "R3" count is wrong | **Disagreement**: the security review gave two paths, the feasibility review suggested O3 send a plain hello without `host_id` first. Settled on the security review's first path: **no talking, read only the USB serial**. This pushes no notes, fakes no online state, changes neither protocol nor vectors, and v0.8 needs no `probe` field. The cost is not knowing the firmware and waiting count before connecting |
| Security 5 | MCP must not touch setup actions | Stated in §2.5, tests added in §7.2 |
| Security 6 | "File encryption" is not done on the Mac; the private key would go into Time Machine | **Aligned with the final docs/26**: per docs/26 D6 the private key is a 0600 file under `$CPA_HOME/keys/seal/`, not in the Keychain; this doc no longer defines a `host_sk` file; new hardware gets a new key and the old one is not deleted (docs/26 D23: it is needed to open waiting items from before the migration); a read-only "Disk encryption" row added; `notes dir` refuses cloud drives |
| Security 7 | Binding by device code can be hijacked | One-time candidate numbers, void when unplugged; none when codes collide; `dev` and the serial checked after binding. **Not doing** a probe before every handshake: `dev` is self-reported too and does not stop a deliberate attacker, and it costs an extra round each time (end of §3.1) |
| Security 8, feasibility 9 | O-SEC is too narrow and cannot run before O0–O6 are done | Folded into docs/26 M-SEC, in two parts; part 1 says plainly it does not test encryption; "two connected at once" becomes "one after the other"; each package first writes its own security tests |
| Security 9 | The curl one-liner, the `xattr` command, tags not checked, dependencies not pinned, model origin | The curl one-liner and the `xattr` sentence removed; the script refuses a checkout with a quarantine attribute or outside git; prints the commit, and release notes carry the commit automatically; the build backend is pinned and installs follow `uv.lock`'s constraints (hash checking marked [tbv]); a wrong model hash is marked "unknown origin" |
| Security 10 | One-click service install from the app could downgrade | The button appears only when `cpa` is newer than cpad |
| Feasibility 4 | The app's 5 s timeout would kill `service install`; a failed bootstrap is reported as success | 60 s timeout; check the `bootstrap` return code and `launchctl print`, report "switched off in Login Items"; the busy check goes into the command; cpa-stt's is fixed together |
| Feasibility 5 | Security fixes were scheduled after the UI | Changed to O0 → O3 → O2; the README reminder is added in this PR |
| Feasibility 7 | Several commands do not exist | `hooks install --json`, `cpa-stt setup --json` and `notes dir` (refuse + restart) go into O2; MCP uses the existing `--last 50`; `setup check` uses no Spotlight and no login shell |
| Feasibility 12 | Moving only preset names loses things | Move the whole record, filtered by security 1's rules |
| Feasibility 13 | Minor points | `xcode-select` written as a safety net; the app step waits for docs/24 v1; "refresh on plug and unplug" written as every 3 s while the page is open; use `uv version`; a Mac-only change still gets a full tag (D21) |
| Feasibility (line counts) | The total did not include new work | Recounted: O0–O5 about 2,200 / 2,700, O6 750 / 550 |
| Final docs/26 (PR #68, `96a03b6`) | This doc followed an early draft of docs/26 with "private key in the Keychain"; §3.5 item 1 was written against the old docs/26 §5.3; D10, D13 and D17 did not mention changing keys | Aligned throughout: the private key is a 0600 file, not in the Keychain (D6); pinning, handshake proof and key change per D21; new hardware, new key per D23; field names, toolchain and M-SEC's two rounds per docs/26 §4.3, §5.5 item 6, §6. The few places docs/26 did not cover were marked [tbv] (closed by the next row) |
| Cross-check (2026-10-22) | docs/26 and this doc were written in parallel, then checked again against docs/18 and docs/25 on main (`995821d`): several [tbv] items were gaps in docs/26, and a few places disagreed between the two or with docs/18 and docs/25 | docs/26 filled the gaps (`8fc1f4a`, the last row of its §9), and this doc followed: a Mac with a pinned key that sends no public key or no proof is "unassigned" and gets nothing (§3.5 item 1, two rows of the §3.2 table); `kid` is now reported in `seal_chal{E′, n, kid}` (items 3, 6); a bound Mac without an enrolled key is also asked once (§3.2); `cpa-key` is built only after cpad is up, so cpad reruns its self-test, the script kickstarts at the end, and "Connect" waits for the self-test to pass (§2.2, §2.3); when the current profile has no key, plaintext is marked "明" (plain) (§2.2); `rotate` has no `--keep-old`, `cpa host reset` changes the key too, giving away uses `--wipe-keys`, and an old Mac put to another use must run reset (§3.5 item 7, §6); after migration the rename to "<old name> (new)" happens before the first `hello` (§6, O4); every storage holds a copy of the recipient table (§6); the key fingerprint is 8 characters, also shown on the Mac, and kept apart from the device code (§2.3, §2.5, §3.1); imported destinations rerun the checks and each Mac's own part starts over, with profiles compared by `served` (§4.1, §4.2); the "Home / work" row depends on whether the device reports `prof`, not on v0.8 (§2.3, §3.2); the `host_label` default is stated to revise docs/23 D10 (D9); D10 only stops boards that have never touched the device (§3.1, §7.1); the wording about network use made precise (§7.2); an old card in another device (§6); M-SEC part 2 gains two items, giving away and late enrolment of a bound Mac |

## Appendix: evidence index

- daemon (origin/main `d330465`): `app.py:116` (`NotesWriter` built only once), `245-248` (host hello), `250-273` (`on_ready`, `_bind`), `384-392` (config reload only swaps the object); `hub/reduce.py:76-77` (full snapshot pushed on Ready), `124-136`; `device/port_finder.py:3, 43, 50-54`; `device/link.py:205-242` (the host sends hello first); `config/host.py:31-41` (`token_hex(8)`); `config/settings.py:56-65, 101-119`; `projects/registry.py:21` (permission mode); `cli/main.py:69, 92, 143-162, 188-204`; `cli/commands.py:95`; `cli/hooks.py:135-147` (only `--cpa`, `--settings`, `--yes`); `mcp/cli.py:107-113` (`audit --last`); `mcp/catalog.py` (tool list); `service/launchd.py:69-80`; `archive/cloud.py:1-24`; `archive/cli.py:46` (`--allow-cloud`); `agents/claude_code/hook_settings.py:57-64`; `recordings/service.py:44-52`; `recordings/puller.py:151`; `recordings/manifest.py:4-6`; `stt/cli_setup.py:19-23, 38`; `stt/cli.py:108-110, 327`; `stt/service.py:42-50`; `stt/models.py:1-8`; `daemon/pyproject.toml:3, 7, 10-12`; `daemon/uv.lock`; `cpad/__init__.py:3`.
- Firmware: `fw/protocol/messages.h:64, 80, 89` (`HostId`, `host_id`, `files.n` is "committed segments"); `fw/protocol/protocol_limits.h:62` (`kLinkTimeoutMs`); `fw/link/link_session.cpp:74-86, 162`; `fw/shell/runtime_outbox.cpp:91`; `fw/hal/hal_platform.h:42` (`dev` is the MAC as 12 lowercase hex); `test/core/test_codec_vectors/test_main.cpp:1-4`, `test/core/test_link_flows/test_main.cpp:157-160`; `~/.platformio/.../esp32s3/qio_qspi/include/sdkconfig.h:565, 603, 622` [meas, read-only].
- Protocol: `protocol/README.md:49` (only add, ignore unknown fields), line 103 (only `hello` is answered while Attached), line 154 (a new handshake returns items to pending); `protocol/limits.json:71`; `protocol/schema/v0/hello.schema.json:58`; `protocol/vectors/v0/flows/` (no `link/` subfolder).
- Test tools: `tools/hil/harness/rig.py:157-170`; `tools/sim/cpad_sim.py:1-12`; `tools/sim/simtools/serial_pty.py:17`; `tools/sim/tests/test_e2e_cpad.py:46`.
- CI: `.github/workflows/release.yml:2, 31, 54` (firmware only, requires release notes); `ci.yml:147-180` (tests the built wheel).
- Docs: README.md:30-70, 200-213; SECURITY.md:28-57; docs/01 §1 item 1; docs/05:919; docs/07:14, 138-143; docs/15:145; docs/18 §1.3, §3.2, §3.5, §3.7, §4, §5.1–§5.6, §8.2–§8.4, §9, §10.3, §13, §15; docs/21:92, 182-186, 424; docs/gates/whisperwoof.md:95; docs/gates/launcher.md lines 13–16; docs/23 §3.1, D10; docs/24 lines 137, 143 and §1.2, §2.3, §2.5, §4, §5.2, §7, §8.1, D8, D10, D12 (`wt-mac`); docs/25 lines 16, 22, 97, 124, 126, 170 and §2.3, §3.1, §4.5, §5.3, §5.7, D7, D12, D22 (main `995821d`); docs/26 §3.1, §4.3–§4.5, §5.1–§5.5, §6, §7, §8.1 (`wt-sec`, PR #68 final `96a03b6`, `8fc1f4a` after the cross-check); docs/18 §7.1 D1, §7.2, §10.1 (main `995821d`).
- GitHub: issue #57 and comments; PR #60; PR #62 (merged as `d330465`); issue #63 (security round), #64 (this doc); PR #68 (docs/26); PR #65 (docs/25, merged, main `995821d`).
- This Mac [meas] (read-only): `uv tool list`; the `~/.local/bin/cpa` link; the cpad LaunchAgent's `ProgramArguments` and `Environment` (plutil); `$CPA_HOME` and `~/CardputerNotes` file names only; `xcode-select -p`; the number of signing identities; `gh repo view` visibility.
- External [ext]: Homebrew 5.0.0 release notes (deprecation of `--no-quarantine` and unsigned casks); Workbrew's reading of Homebrew 5.0; Macworld and OSNews on opening unsigned apps on Sequoia; Gatekeeper (Wikipedia); Apple developer forum threads 779379, 799910 (SMAppService and ad-hoc signing); uv cache docs (`cache-keys`).
