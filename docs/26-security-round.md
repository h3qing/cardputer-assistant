**English** · [中文](26-security-round.zh-CN.md)

# 26 · Security test round and encryption at rest: on the device each file is sealed to the Macs; on the Mac the whole disk is encrypted and files stay open

> Status: **design draft, revised after three reviews (§9); on 2026-10-22 cross-checked against docs/27 and a few gaps filled (last row of §9); waiting for your decision (§8.2)** · Date: 2026-10-22 · Role: security (data at rest, keys, security test round) · Based on origin/main `995821d` (code identical to `d330465`, only docs/25 added) · Related: docs/18 P8-E and §8, docs/05 §8, SECURITY.md, ADR 0001, ADR 0004; docs/24 (`wt-mac`), docs/25, docs/27 (new Mac setup, PR #67).
> Requirement: make security testing a milestone and run a round of it, so that all data at rest is file-encrypted; use the same device with several Macs, with a smooth setup on a new Mac, and the Mac and the device able to recognise each other.
> Sources: SECURITY.md; docs/04 S11, D16; docs/05 §8; docs/07 §6.7; docs/11 §4.4–§4.6; docs/12 G-A6, G-A8; docs/13 v0.5; docs/15 §2, §3.7, §3.9, §5; docs/18 §3.2, §4, §5.2–§5.3, §8, §9, §11, §13, §15; docs/21 §1, §5.6; docs/22 §4.5, §5.6; docs/23 §6; docs/24 §7, D12 (`wt-mac`); docs/25 §2.3; docs/27 §3.5 (PR #67); docs/gates/launcher.md; ADR 0001, 0004; `protocol/README.md`; recording contract v1; firmware `lib/{hal,rec,outbox,storage,shell,protocol}/`, `src/main.cpp`, `platformio.ini`, `partitions/cpa_8MB.csv`; daemon `service/`, `config/`, `logging/`, `events/`, `recordings/`, `agents/claude_code/hook_settings.py`, `cpa_stt/{runner,service}.py`; headers and static libraries of the Arduino 2.0.17 SDK (`nm`).
> Marks: **[src]** code or docs read · **[meas]** measured read-only on this Mac · **[calc]** estimated by calculation · **[inf]** inferred · **[assume]** design assumption, can change · **[open]** to verify with a gate, test or real device · **👤** needs you.
> Scope: design only. No device connected, no serial port opened, no code changed, no system settings changed; `backups/` untouched; in `~/CardputerNotes` only file names and permissions were looked at, no content read.
> Path shorthands: `fw/` = `firmware/app/lib/`; `cpad/` = `daemon/src/cpad/`; `stt/` = `daemon/src/cpa_stt/`; `SDK/` = `~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3/`.

## 0. One-page summary

1. **What you want**: two things. First, make security testing a milestone (M-SEC) and run a round that proves data at rest is encrypted. Second, pair one device with several Macs, with a smooth setup on a new Mac. The flow for the second is in docs/27; this doc only covers the key-related part of it (§5.5).
2. **Today**: everything not yet synced is plaintext on the device: notes, queue items, recordings, the project-name cache. "Delete after sync" is just `unlink`, so deleted data can be recovered from the card and from Flash; on a crash the coredump partition keeps a copy of the task stacks; Launcher's `/bkp/` may hold another backup of cpafs [inf]. On the Mac, FileVault is on, the archive directory is 0700 and files 0600; only the launchd logs of cpad and cpa-stt are 0644 (§2).
3. **Keys on the device cannot protect the data**: there is no Secure Boot; any USB host can read and write the whole Flash without a key press, and can read live memory through the built-in USB-JTAG. So a symmetric key kept in NVS is as good as none. Flash encryption and NVS encryption need eFuses burned, would break Launcher, and are not even compiled into the IDF 4.4 libraries (§4.2).
4. **Recommendation: SEAL (seal to the Macs' public keys)**: for every file the device writes, it uses a fresh random key, and wraps that key once for each receiving Mac's public key (X25519). The device stores only public keys, so **nothing on the device can open its own files**. Recipients are the Macs you approved on the device, with or without profiles. On every handshake the Mac must prove it has the private key; only then does the device hand over sealed items or allow them to be deleted. A Mac whose key is pinned gets nothing at all before it proves. A lost device, a pulled card, one Flash read by someone, leftovers after deletion: all of them yield only ciphertext. This is P8-E in docs/18 (§4.3).
5. **Be clear about what it cannot stop**: a connected host that deliberately modifies the device (adds itself to the recipient table, flashes firmware, attaches JTAG) can still get everything recorded afterwards. SEAL protects against "one passive read". Against a deliberate host the only answer is "sync to zero before going to an untrusted computer, and use a data blocker when only charging" (§3.1 T-USB, §4.4).
6. **Cost**: the device cannot see the bodies it has stored, so v0.5's on-device playback has three options for you to choose from (👤 §8.2 item 1); the G-ENC gate must pass first (CPU, heap, static RAM, randomness, power-loss recovery); the Mac must be able to decrypt, and the Python standard library has no X25519 or AEAD (§4.3, §4.5).
7. **Decryption on the Mac**: a small Swift command-line tool, `cpa-key`, using only Apple's built-in CryptoKit, zero third-party code, built in the same SwiftPM package as the Mac app; the daemon still depends only on `pyserial`, and item 1 of ADR 0004 is unchanged. The private key is a 0600 file under `$CPA_HOME/keys/seal/`, **not in the Keychain**: `cpa-key`'s signature changes on every rebuild, and the Keychain would suddenly pop up asking for the login password during a background sync (§5.2). If the Mac cannot decrypt for the moment (tool broken, timeout), it first stores the ciphertext in `$CPA_HOME/sealed/` and then acks, so the device's waiting items never get stuck (§4.3).
8. **The archive on the Mac is not encrypted file by file**: per-file encryption would make it unreadable to Obsidian, grep, WhisperWoof and QuickLook (against ADR 0004's "open files"), and AI clients reading through cpa/MCP would get decrypted content anyway, so it would not stop them either. Default: FileVault (already on) + encrypted Time Machine (👤 please take a look) + the small fixes in §4.6. An encrypted disk image is documented only as an optional practice, not built as a feature.
9. **So "all data file-encrypted", honestly, means**: each file sealed individually on the device; the Mac's disk and backups encrypted as a whole; files on the Mac open to your own apps, on purpose.
10. **Can be done now, no decision needed** (SEC-0): create the launchd logs of cpad and cpa-stt as 0600 up front, and chmod everything under `logs/` to 0600 at startup; have cpa-stt clean up temp WAVs left by a previous crash at startup; change the body hash in `cpad.log` to an HMAC with a per-Mac salt (field renamed `h8`; `sha8` in events unchanged); keep the first and the latest 2 copies per target in `hook-backups`; correct statements in SECURITY.md such as "deleted means gone" (§3.3, §4.6).
11. **Security test round M-SEC** (§6): automated (fuzzing with ASan/UBSan, permission walk, canary leak tests, gitleaks); simulator (hostile SD card, fake device, fake Mac); real device; Mac (`cpa doctor security`). **Two rounds**: the baseline round now runs only the automated and simulator parts plus coredump erasing on the real device (and measures the whole app's heap while recording), without spending your time proving the known fact that "today it is plaintext"; the encryption round after SEAL runs everything, with Flash reads and SD imaging combined into one script that needs sudo only once.
12. **Phases** (§7): SEC-0 to SEC-3 are about 780 lines of code and 1,850 lines of tests, with no decision needed. SEC-4 to SEC-6 are about 2,870 / 3,200, scheduled after the docs/18 v0.8 binding and the docs/24 Mac app package.
13. **All 23 decisions** have defaults (§8.1); **5 need your call** (§8.2). How every point from the three reviews was handled is in §9.

## 1. What you want, and the gap from today

### 1.1 Requirements broken down

- Security testing is a milestone with a checklist and pass criteria (§6 M-SEC).
- Not a one-off review: a baseline round first, another round after encryption, and the automated part on every release after that (§6, §7).
- Every place data lands has an answer: encrypted per file, encrypted as a whole disk, or a written reason why not (§2, §4.1).
- One device, several Macs, each Mac with its own identity and keys (§5; docs/18).
- Setup on a new Mac (docs/27; §5.5 here only covers keys).
- The Mac recognises the device as yours, and the device recognises a Mac you have bound (§3.1 T-ROGUE, §4.3 handshake proof, §5.5; docs/18 §4).

### 1.2 How far "file encryption" can go

- Encryption only protects against "someone who is not you has the storage medium", not against "a program running as you": cpad and MCP must be able to read, so any program on this Mac can read through them. SECURITY.md:62 already excludes malicious programs under the same user.
- The device and the Mac are in opposite situations. The device is easy to lose, easy to plug into someone else's computer, and has no Secure Boot; the Mac has FileVault and a login password. So the device encrypts per file, and the Mac encrypts the whole disk.
- Encryption only protects against **passive** reads: if a host connected to the device deliberately modifies it (changes the recipient table, flashes firmware), it can get later records in plaintext (§3.1 T-USB).
- Encryption does not protect against forgery: someone with the SD card can plant fake items and recordings, and the device will send them on as its own (§3.1 T-CARD, D10).

## 2. Map of data at rest

### 2.1 Device

| Location | Content | Today | Who can read it | Source |
|---|---|---|---|---|
| NVS 0x9000 (20 KiB), namespace `cpa` | Boot counters `epoch`, `boot`. Planned: language, binding (`host_id` hash 16 B), profiles, `host_label` ≤24 B ×8, `PROFON`; the SEAL recipient table (§4.3, about 470 B), `SEALON` | Plaintext. IDF 4.4 cannot encrypt NVS without Flash encryption | Any connected USB host: `esptool read_flash` without a key press, and it can write too. Other firmware installed via Launcher: there is only one NVS, and Launcher's `l_wifi` is in it as well | [src fw/hal/nvs_boot.cpp:26-43; docs/gates/launcher.md:50, 114; docs/18 §3.2, §11] |
| Internal LittleFS `cpafs` 0x670000 (1.5 MiB), used when no card is inserted | `/outbox/_shard/<MonoId>.json`: the file content is the wire line, including the body. `/outbox/bad/`: the latest 20 rejected items. `/cfg/projects.json` | Plaintext. Delete is `unlink`; LittleFS is copy-on-write, so old blocks remain | Anyone who has the device | [src fw/hal/lfs_store.cpp:108; fw/outbox/outbox_io.h:1-7; fw/shell/runtime_outbox.cpp:15-17; partitions/cpa_8MB.csv] |
| coredump 0x7F0000 (64 KiB); at 0x160000 under Launcher, shared with other firmware | On panic, the stacks of all tasks are written: the 16 KB loop stack (text being typed, decoded host messages), the audio stack | Plaintext, kept until the next panic overwrites it; cpa never reads or erases it | Anyone who has the device | [src SDK/qio_qspi/include/sdkconfig.h:424; docs/gates/launcher.md:217 says "harmless", which is wrong from a data-at-rest view] |
| SD `/cpa/outbox/…`, `/cpa/outbox/BAD/…` | Item JSON: body ≤500 B, project, intent | Plaintext FAT. `unlink` on ack; rejected items are copied then deleted, leaving more leftovers | Anyone with the card (any OS can read it); Launcher's SD manager and WebUI | [src fw/storage/sd_paths.h:1-15; fw/hal/sd_card.cpp:202-204; SECURITY.md:41] |
| SD `/cpa/rec/<E7>/*.W?` | 16 kHz mono PCM, 44 B header, 9.6 MB per 5 minutes | Plaintext. `file.del` is `unlink`, so recordings already pulled can be recovered from free clusters | Same as above | [src docs/11 §4.4; fw/rec/rec_engine.cpp:251; fw/hal/vfs_rec_file_port.cpp:125-127] |
| SD `/cpa/rec/<E7>/*.M?` | ≤512 B JSON: id, segment number, mode, byte count, `sha256` of the plaintext, time, project, star, `consent` | Plaintext; `sha256` is unkeyed, so it does not stop someone tampering with the card | Same as above | [src docs/11:220-236; fw/rec/rec_meta.h:17] |
| SD `/cpa/cfg/projects.json` (one per profile from v0.8) | Project keys and names | Plaintext | Same as above | [src fw/shell/runtime_outbox.cpp:15-17; docs/18 §11] |
| SD `/bkp/` (Launcher's) | Launcher's backup directory (`backupData.json` and `{appNum}-{appName}/`); a backup made when cpa is deleted in Launcher may include cpafs [inf] | Plaintext FAT; cpa does not manage it | Anyone with the card | [src docs/gates/launcher.md:138] |
| Memory only | Recording index, outbox index, sessions, queue, drafts, 24 KB mic ring buffer | Gone on power loss; while running, a connected host can read it through USB-JTAG without a reset: the firmware uses the built-in USB serial/JTAG (`ARDUINO_USB_MODE=1`), and `DIS_USB_JTAG` is not burned | A host connected by cable | [src firmware/app/platformio.ini:36] |

### 2.2 Mac

| Location | Content | Today | Who can read it | Source |
|---|---|---|---|---|
| `~/CardputerNotes/YYYY-MM-DD.md` | Typed notes, memos, to-dos | 0700/0600, FileVault on [meas] | Any program running as you (these two directories are not TCC-protected, so no prompt); admins; Spotlight queries; the Time Machine disk | docs/15 §2.1 |
| `recordings/<date>/<HHMM>-<session>/` | `seg-NNN.wav` (deleted 30 days after archive and transcription), `manifest.json`; `status.json`, `transcript.*`, `summary.md`, `result.json` written by the transcriber | Same as above | Same as above | [src cpad/recordings/retention.py:1, 27]; recording contract v1 |
| `$CPA_HOME/events.jsonl` | **The body of every note and queue item, kept forever**; `rec.hidden` only hides, never deletes | 0600 [meas] | Same as above; findable by file name in Spotlight [meas] | [src cpad/events/records.py:23-80]; docs/23 §6.1 |
| `config.json`, `projects.json` | claude path, notes directory, device serial number (= MAC address), `spawn_env` (any keys and values); project keys, names, **local paths** | 0600; no secrets [meas, key names only] | Same as above | [src cpad/config/settings.py:3-6, 56-61, 86-89] |
| `host_id` (`host_hw` added in v0.8) | A random value used as a password | 0600, never logged | Same as above | docs/18 §4.1 |
| `logs/cpad.log`, `logs/mcp-audit.jsonl` | Redacted log (bodies only as length and unsalted `sha8`); what MCP handed out (without content) | 0600 | Same as above | [src cpad/logging/redact.py:1-22]; docs/15 §3.9 |
| **`logs/launchd.{err,out}.log`** (one pair each for cpad and cpa-stt) | Python tracebacks (`str(exc)`); cpad's err has 84 lines, 3 tracebacks (content not read) [meas] | **0644**: the plist has no `Umask`, so launchd uses the default 022 when creating files; `secure_home` only handles 5 named files | Same as above (the parent 0700 keeps other users out) | [src cpad/service/launchd.py:57-58; stt/service.py:31-34; cpad/config/settings.py:157-166] |
| `rec-staging/<dev>/<fid>.part` | Half-pulled plaintext audio | 0700/0600 | Same as above | [src cpad/recordings/land.py:3] |
| `hook-backups/` | Old copies of `~/.claude/settings.json` and each project's `settings.local.json`, which may carry API-key-like environment variables; never cleaned up | 0700/0600 | Same as above | [src cpad/agents/claude_code/hook_settings.py:7-8, 29] |
| `$TMPDIR/cpa-stt-*/run.wav` | Plaintext audio concatenated for transcription | 0700; deleted in `finally`, but left behind on SIGKILL, crash or power loss; no sweep at startup | You | [src stt/runner.py:87-93] |
| `cpa export` output | Plaintext copy of the archive | 0700/0600; refuses cloud-synced directories by default | Depends where you put it | docs/15 §2.4 |
| Development artefacts | `backups/` (Flash and NVS images); `firmware/hwtest/captures` (43 WAVs, 0644); HIL recordings in the staging area | Plaintext, all in Time Machine backups | You; 0644 files are readable by other users | `.gitignore` |

### 2.3 Copies cpa does not control

- **Spotlight index**: readable only by root, inside FileVault. `mdfind -onlyin ~/CardputerNotes` finds 23 manifests and 2 notes by name [meas].
- **Time Machine**: the destination is a local disk; `~/CardputerNotes` and `$CPA_HOME` are both in the backup scope. The disk was not mounted, so **whether the backup is encrypted could not be seen** [meas]. The 30-day audio deletion and `rec.hidden` do not reach the backups.
- **AI clients**: results read through MCP, and voice memos transcribed by this workflow, stay in Claude Code's, Codex's and Cursor's own directories (not checked); and a client sends what it reads to its model provider, which the service cannot control [src docs/15:57, 520].
- **Cloud destinations in docs/25**: a destination marked `leaves_mac` in the presets (e.g. Grok) sends what you said off this Mac [src docs/25 §2.3].
- **Clipboard**: the docs/25 `clip` adapter puts text on the clipboard. Universal Clipboard syncs it to your other Apple devices, and clipboard managers keep history [inf].
- **iCloud "Desktop & Documents" is on** [meas]: exports dragged to the Desktop or Documents leave this Mac. The notes directory is not in iCloud.
- **WhisperWoof's own encrypted stores** (SQLCipher, WWENC1): not checked; WhisperWoof's code is out of scope this time.

## 3. Threat model (updates SECURITY.md)

### 3.1 Who gets what

| # | Adversary | Gets today | After this doc |
|---|---|---|---|
| T-LOST | Device lost together with its card | Everything not synced, project names in `/cfg`, NVS, coredump; **leftovers of notes and recordings already synced** | After SEAL, a device nobody has tampered with holds only ciphertext. What remains is metadata: file names, sizes, counts, profiles, all fields of `.M` (mode, time, project key, star, `consent`, plus `dest`, `lock` added by docs/25 §4.6), the project-name cache and public keys |
| T-CARD | Has only the SD card | Everything on the card, including old WAVs recoverable from free clusters and Launcher's `/bkp/`. **Can also plant fake items and recordings on the card**: the device sends them to the Mac as its own, into `events.jsonl`, the daily note and MCP; a prompt-injection path into AI clients (the bigger one is what people really say in meetings, handled by the "untrusted data" marking in docs/15 §3.7) | After SEAL, only ciphertext; old leftovers need one cleanup (§4.3). Forgery is still possible (public keys are public); D10 only stops someone who has only the card (§8.1 D10) |
| T-USB | Untrusted USB host: a company-managed Mac, a friend's computer, a public charging station with a data line | Read and write the whole Flash without a key press, flash firmware, read live memory through USB-JTAG [src docs/18 §8.2; platformio.ini:36] | A host that **reads once** gets only ciphertext. A host that **deliberately modifies the device** cannot be stopped: it can add its own public key to NVS, erase NVS to force the device back to plaintext, flash firmware, read file keys from memory. What can be done is detecting part of this (§4.4), plus "sync to zero first, use a data blocker when only charging" |
| T-ROGUE | A fake device plugged into your Mac; or someone who has a `host_id` | Before cpad binds a serial number, it sends `host_id` in plaintext to any 303A:1001 serial port (docs/18 T4) [src docs/18:223]. The serial number (= MAC address) can be forged by any S3 firmware [inf], and appears in about 62 files in the repo (#39), so binding by serial number is not authentication. A fake device can plant notes, queue items and recordings, and receives snapshots (project names, sessions, ≤120 B "waiting for you" excerpts); someone with `host_id` can impersonate that Mac | After SEAL: a Mac that has not proved its private key gets no sealed items and cannot delete them; for a `host_id` with a pinned key, not even unsealed items are given before it proves (§4.3 "Pinning"); for Macs with no key registered yet, unsealed waiting items are still protected only by the `host_id` password. A fake device can still plant things, unless D10 is done |
| T-USER | Another user on the Mac | Blocked by 0700; only the 0644 development recordings under 0755 paths are readable. Today there is only one account [meas] | All blocked after SEC-0 |
| T-APP | Programs running as you, malware | Everything | Unchanged: out of scope (SECURITY.md:62; docs/15 §5). It can read the private key file, or call `cpa-key` directly |
| T-MAC | Mac stolen | FileVault protects it when off; when on or asleep, the login password does | Unchanged |
| T-BACKUP | Time Machine disk lost | If the backup is not encrypted, a full plaintext copy of the archive | After SEAL the private key file is in it too (D6). 👤 All protected once you turn on "Encrypt backups" |
| T-CLOUD | Paths off this Mac | The notes directory is not in iCloud; exports are guarded [src cpad/archive/cloud.py]; the clipboard syncs via Handoff; what MCP clients read is sent to their model providers [src docs/15:57, 520]; destinations marked `leaves_mac` in docs/25 | Clipboard local only (docs/25 §5.3 `ClipWriter`, §4.6). The other two are intentional: you choose which client to connect and which destination to use (docs/15 §5 👤, docs/25) |

### 3.2 Trust boundary

The v0.1 boundary "physical USB + the currently logged-in macOS user" stays (SECURITY.md:9-11). Add one sentence: **before SEAL is on, the device is a plaintext storage medium; once it is on, what the device stores is ciphertext that only Macs that have proved their private key can open, and the device itself cannot open it. This assumes no host deliberately modified the device before that.** "There are no keys on the device" (SECURITY.md:26) still holds under SEAL, because the device stores only public keys; once D10 is done it becomes "the device holds only one identity key, and getting it only lets you impersonate the device".

### 3.3 What SECURITY.md needs corrected and added

1. "Items are deleted on ack" is not a mitigation: deletion is just `unlink`, recoverable on both FAT and LittleFS. Change T6 and T7 in docs/18 too. The real fix is SEAL: the key is only on the Mac, so leftovers are only ciphertext.
2. Add coredump: on panic, task stacks land in the coredump partition. Change "harmless" in docs/gates/launcher.md:217 to "leaves plaintext stacks, see docs/26 §4.4".
3. Add untrusted USB hosts and charging stations: they can read and write the whole Flash without a key press, flash firmware, and read memory through USB-JTAG; SEAL only protects against passive reads.
4. Add copies on the Mac and paths off the Mac: Spotlight, Time Machine, AI clients' session logs and model providers, cloud destinations in docs/25, the clipboard, iCloud Desktop & Documents.
5. Line 28, "formal pairing … moved to v0.3", is out of date; change it to "the SEAL handshake proof (Mac to device), and D10 (device to Mac)".
6. Change "encryption implemented later" on lines 41–42 and 57 to point to this doc and P8-E.
7. Write in the four v0.8 statements from docs/18 §8.4 as well.
8. When SEAL is built, add a "Keys" section: the device stores only public keys; the private key is a 0600 file under `$CPA_HOME/keys/seal/` and travels with Time Machine and Migration Assistant; losing the device needs no revocation; losing a Mac means forgetting it on the device (§5).

## 4. How each location is encrypted

### 4.1 Overview

| Location | Default (recommended) | Stops | Does not stop | Cost |
|---|---|---|---|---|
| outbox items on the device (cpafs, SD) | SEAL | Lost device, pulled card, one Flash read, leftovers after deletion, Macs that have not proved their private key | A host that deliberately modifies the device; reading memory through JTAG while running; metadata; forgery (D10) | The device cannot see stored bodies; each line gets longer |
| Recordings on the device `.V` / `.M` | SEAL seals only the audio; `.M` stays a plaintext structure, protected from tampering by the blocks' AAD; the WAV header is added by the Mac | Same as above | Same as above; the fields of `.M` | On-device playback is your call (D9 👤); G-ENC must pass |
| `/cfg` project cache | Stays plaintext (the device needs to read it) | — | Project names | Documented in SECURITY.md |
| NVS | Only public keys, hashes and labels; the recipient table is mirrored in cpafs | No secrets to steal | Labels and number of bindings; a host that deliberately edits the table (§4.4 detects part of this) | — |
| coredump | Release firmware erases it when idle, only if there is content | Stacks left after a crash | Between the crash and the next idle | Lose debugging dumps (test firmware can keep them) |
| Old deleted plaintext | Prompt once after the old items have synced: clear internal storage on the device, `cpa sd scrub` for the card | Old leftovers | Spare blocks in the SD card's FTL | You run one command on the Mac |
| Launcher's `/bkp/` | `cpa sd scrub` tells you when it finds a cpa backup in it; delete it in Launcher | Old backups on the card | — | You delete it once |
| Mac archive | FileVault (already on) | A Mac that is off or stolen | Programs of the same logged-in user (out of scope) | None |
| Time Machine | Encrypted backup (👤 check) | Lost backup disk | — | You set it once |
| Logs etc. in `$CPA_HOME` | All 0600 (fix the launchd logs) | Other users | The same user | Small fix |
| SEAL private key | `$CPA_HOME/keys/seal/`, 0600; not in `cpa export` or settings export | Other users, exports | Programs of the same user; an unencrypted Time Machine disk | No prompts |
| Ciphertext on the Mac waiting for decryption | `$CPA_HOME/sealed/`, 0700/0600 | It is ciphertext anyway | — | Small |
| cpa-stt temp WAVs | Swept at startup | Leftovers from crashes | — | Small fix |
| `events.jsonl` | Unchanged (text kept forever) | Relies on FileVault | Real deletion (D15) | — |
| Clipboard (docs/25 `clip`) | Local only, marked concealed and transient, cleared after 2 minutes | Universal Clipboard, clipboard managers [inf] | Programs on this Mac | None: already decided for docs/25 §5.3 `ClipWriter` (docs/25 D9) |
| Development artefacts | Change to 0600, or move into a 0700 directory | Other users | — | Small fix |

### 4.2 Paths ruled out

- **Flash encryption, Secure Boot**: one-way operations that burn eFuses; once on, Launcher and community flashing tools stop working [src docs/04:751 D16; SECURITY.md:35].
- **NVS encryption**: requires Flash encryption first. Also, `nvs_flash_secure_init` is only declared (`nvs_flash.h:191`); `libnvs_flash.a` only exports `nvs_flash_init`, and `CONFIG_NVS_ENCRYPTION` is not enabled [meas: `nm`; docs/21].
- **HMAC peripheral with an eFuse key**: needs eFuses burned; and a firmware flashed to read it can still call the peripheral, so it is no better than a key in NVS.
- **Symmetric key in NVS (K-NVS)**: read NVS and you have the key; no encryption at all.
- **Symmetric key in memory only, pushed down at binding (K-RAM)**: after an offline reboot there is no key, which breaks "recording always works".

### 4.3 Device: SEAL (P8-E)

The spec goes into `protocol/enc/v1/README.md`, with known-answer vectors and a checker. Field names live only in the doc for now and enter the schema when the spec is final.

**Recipients: the Macs you approved on the device, separate from profiles** (D7):
- **Registration**: a Mac's public key is accepted only at the moment you confirm on the device (docs/27 §3.5 item 1).
  - With profiles: this is the binding confirmation of docs/18 §5.3; confirming the binding registers the key at the same time, with no extra press.
  - Without profiles ("no profiles" in docs/18 §9): the first time the device sees a `hello` from this Mac carrying a public key, it shows a sheet "Let "Mac·8C1D" unlock the records on the device?", with the cursor on "Later". Asked only once per `host_id`; you can add it later under System → Encryption.
  - **A Mac already bound but with no key registered** (bound before SEAL firmware, or bound when `cpa-key` was not there yet): even with profiles there is no binding confirmation left to use, so as without profiles, its first `hello` with a public key shows the same sheet, cursor on "Later", asked once per `host_id`. In both modes System → Encryption has an "Add "…"" line; pressing Enter on it for a connected, unregistered Mac shows this sheet.
- **Who each new file is sealed to**: with profiles, to the registered Macs bound to the current profile; with profile 0 (undecided) or without profiles, to all registered Macs. Whether to deliver still follows the rules of docs/18 §9. If no Mac with a registered key exists in the current profile (e.g. the work Mac has no `swift` and cannot install `cpa-key`, docs/27 §5.2; or it is the 5th Mac): record in plaintext with the "明" (plain) mark (D8), and System → Encryption shows "Encryption: Work has no key"; **never seal to Macs of another profile**, since under docs/18's filter those could never be delivered anyway.
- **Limit**: at most 4 Macs registered on the device, so at most 4 recipients per file. To register a 5th, forget one first ("At most 4 Macs can unlock; forget one first"). docs/18's binding table still has 8 entries, but with SEAL only 4 Macs are usable in practice.
- **Where it is stored**: a table in NVS, at most 8 entries `{hh, kid, pk, profile slot, state}`, about 470 B. `hh` is the 16 B hash from docs/18 §3.2 (first 16 bytes of `sha256("cpa-host:" + host_id)`); "pinning" and "asked once per `host_id`" both look up by it, so it works without profiles even when there is no docs/18 binding table. States are "in use", "replaced old key" and "asked, you chose Later" (the last one has no `pk` and is evicted first when the table is full). 4 entries in use, plus up to 4 replaced old public keys kept until the device has no waiting items sealed to them (the handshake proof needs them). `kid` = first 8 bytes of `sha256(pk)`; one `kid` is stored only once. If an in-use `kid` brought by a new `hh` is already registered under another `hh` (migration without changing keys, or an old cpad from before `cpa host reset`), no second entry is stored; ask "Forget the old one?" as in §5.3.
- **Mirrors**: the same table and a `SEALON` marker are written to NVS and to **every mounted storage** (cpafs `/cfg/seal.json`, SD `/cpa/cfg/seal.json`), like docs/18's `PROFON` (docs/18 principle 8, T9). When NVS is wiped entirely (core 2.0.17 does this when NVS is full [src docs/gates/launcher.md:162], and Launcher adds about 19 `uiflow` keys on every boot [src docs/gates/launcher.md:203]), it is restored automatically from the cpafs copy at boot. The SD copy does not rely on you checking fingerprints: an entry is restored only when the connected Mac's `hh` matches one entry and it proves that `kid`. If any place has `SEALON` but not a single table entry can be recovered (Launcher layout without cpafs, and the card was changed): record in plaintext with the "明" mark, and System → Encryption shows "Encryption: recipient table lost"; never quietly fall back to "never turned on".
- **Pinning**: once an `hh` has an in-use `kid` in the table, if its `hello` comes without `seal`, brings a different public key, or the `seal_proof` does not arrive or does not verify, the device treats this Mac as "unassigned" in docs/18 terms: `prof{ok:[]}`, counts reported as 0, its snapshot not adopted, no online requests sent, and nothing handed over, sealed or unsealed. Only when the proof passes, or you choose on the device to switch to the new key, is this relaxed, waiting for the next `pong` as in docs/18 §3.7. So someone with a `host_id` cannot get around it by "not sending a public key". When a different public key arrives:
  - If this Mac proves itself with the old key in the same handshake (rotation in §5.3): switch directly, with a one-line note ""Mac·8C1D" changed its key".
  - If it cannot: keep the old one and ask on the device ""Mac·8C1D"'s new key (3F2A·61D0) cannot prove the old one. Switch?", cursor on "Don't switch". You are not asked to compare old fingerprints: 4- or 8-digit fingerprints can be matched by generating more keys (see "Fingerprint" below).
- **Visible at a glance**: the first line of System → Encryption always reads "Encryption: on (sealed to Mac·8C1D, Mac·41F0)" or "Encryption: off (no Mac yet)", with one line per Mac below, with fingerprints. The first line of the Mac app's security card says the same.
- **Fingerprint** = the first 8 hex digits of `kid`, written as `9C1B·27E0`; `cpa status` and the Mac app's security card on the Mac show the same one, and if they match it is the same key. Not 4 hex digits: that is only 16 bits, and generating some sixty thousand keys finds any value in seconds; 8 also keeps it distinct from docs/27's 4-digit device code.

**Handshake proof: the Mac proves it has the private key** (D21; docs/27 §3.5 item 2):
- When `hello` carries `seal`, or this `hh` has an in-use `kid` in the table, the device adds `seal_chal{E′, n, kid}` to its reply to `hello`: a fresh ephemeral key pair `(e′, E′)` and a 16 B random `n` per handshake; `kid` is the one the device has pinned for this `hh` (omitted if none). The Mac compares it with its own key and, if they differ, alerts "the device does not hold this Mac's key" (docs/27 §3.5 item 3). The device reports `kid` only here, not in `prof`: without profiles there is no `prof` (docs/18 §10.1), and `prof.ok` is a list of profile keys (docs/18 §10.3).
- The Mac's next `ping` adds `seal_proof[{kid, tag}]`: `tag` = the first 16 bytes of HMAC-SHA256(HKDF(X25519(sk, E′), info = "cpa-seal/v1 confirm"), n‖dev). Computed by `cpa-key prove`; the private key never leaves `cpa-key`.
- The device verifies each claimed `kid` against the public key in the table. Per handshake: the in-use `kid` is always verified; an old `kid` only when the device still has waiting items sealed only to it. So usually 2–3 X25519 operations, at most 1 + 5 [calc].
- **Before the proof passes**: no sealed item at all is handed over, and this Mac's acks and `file.del` for sealed items are ignored. After it passes, only items whose recipients include a `kid` it proved are accepted.
- Both sides reject an all-zero X25519 result (RFC 7748 §6.1); otherwise someone could push a low-order point as a public key, and the wrapped key would be public.
- No new message types, only new fields. It is the opposite direction from D10 (device proves itself to the Mac); the two are independent.

**Format** (D4):
1. Each file (one outbox item, or one recording segment) uses a fresh 32 B file key `fk` from the DRBG. Whenever opening the file fails and is retried, a new one is drawn.
2. **Period key**: the device keeps an ephemeral X25519 key pair `(e, E)` and, for each recipient, a precomputed `Sᵢ = X25519(e, PKᵢ)`, in static memory. When the recipient table changes, the profile switches, a Mac is forgotten, or a full hour has passed, it switches to a new one at once and zeroes the old `e` and `Sᵢ` (docs/18's "narrowing takes effect immediately").
3. Per file: `wkᵢ = HKDF-SHA256(Sᵢ, salt = E‖PKᵢ, info = "cpa-seal/v1 wrap"‖fid)`, and `fk` is wrapped once with `wkᵢ` using AES-256-GCM. `wkᵢ` differs per file and per recipient, so this step always uses an all-zero nonce. Per file only N HKDFs and N wraps, <1 ms; X25519 happens only when the period key changes.
4. **Header** (plaintext, never rewritten): magic, version, flags (mode hint: memo or meeting), `fid`, `rec`, `seg`, `prof`, `E`, recipients `[{kid, wrapped fk}]`. About 52 + 56×N B, at most about 280 B for N ≤ 4 [calc].
5. **Body** in 4 KiB plaintext blocks, each 4112 B after encryption (with a 16 B tag). nonce = block number (32 bits)‖flags (last block of this segment, last segment of the whole recording)‖0; AAD = SHA-256 of the header. So `fid`, `rec`, `seg`, `prof`, mode, recipients, "last block" and "last segment" are all authenticated: the Mac detects a changed `.M`, reordered segments, segments spliced in from another recording, or a changed `prof`.
6. **Rules**: under one `fk`, each block number is encrypted only once; only the last block may be short; power-loss recovery never re-encrypts. `fk`, `e`, `Sᵢ`, `e′` are zeroed with `sodium_memzero` after use; they live in static memory, not on a task stack (IDF 4.4's ELF coredump only captures stacks) [inf].
7. **Libraries**: X25519 from libsodium, which is already linked (`crypto_scalarmult_curve25519`, fixed 32 B arrays, no heap) [meas: `nm liblibsodium.a`]; AES-GCM from mbedtls, whose `MBEDTLS_GCM_ALT` goes through `esp_aes_gcm`: hardware AES-CTR plus software GHASH, as the S3's AES hardware has no GCM mode [meas: `nm libmbedcrypto.a`; SDK/include/mbedtls/port/include/mbedtls/esp_config.h:127]; neither library has HKDF, so build it from HMAC-SHA256, about 30 lines, checked against the RFC 5869 vectors; DRBG from mbedtls CTR_DRBG. G-ENC measures X25519 in both libsodium and mbedtls (with `CONFIG_MBEDTLS_HARDWARE_MPI` on).

**Why not age** (D4):
- age uses 64 KiB chunks and needs a 64 KiB buffer; without PSRAM, the floor while recording only guarantees a largest block ≥32 KiB, so it does not fit.
- Standard `age -d` refuses truncated streams, and power-loss-recovered segments need our own reader anyway.
- ChaCha20-Poly1305 is actually in the library: libsodium exports `crypto_aead_chacha20poly1305_ietf_encrypt` [meas] (the first draft said "needs to be brought in from source, +10 KiB"; that was wrong). It is pure software, using no heap and no DMA, and is kept as the AEAD fallback: if G-ENC finds a problem with `esp_aes`'s DMA or interrupt path, switch to it before the spec is final. CryptoKit has both.
- The upside of the age alternative: a standard format, native multi-recipient support, and `age -d` as an emergency rescue when cpa is broken.

**The WAV header is not in the ciphertext**: plaintext WAVs `lseek 0` to fill in the 44 B header when switching segments [src docs/11:242]; doing that inside ciphertext would encrypt twice under the same nonce. So the ciphertext holds only PCM, and the Mac adds the header from the byte count after decryption. In recording contract v1, `segments[].sha256` (over the whole plaintext WAV) is also computed by the Mac, so the contract is unchanged. Today, when there is no previous segment, power-loss recovery reads the mode hint ("MEMO"/"MEET") from the WAV placeholder header [src fw/rec/wav_header.h:4]; encrypted segments have no placeholder header, so the mode hint moves into the SEAL header's flags.

**SD layout of recordings**:
- Encrypted files use new 8.3 type letters: audio `.V?`, items on SD `.E?`. Old firmware and old cpad do not recognise these names and will not misread them.
- **`.M` seals nothing**: it stays a plaintext structure. `recovered_meta` copies the mode, time, project and slot from the previous segment's `.M` [src fw/rec/rec_recovery.cpp:25-27], and `rec_item_of` for `file.list` reads these fields too [src fw/rec/rec_meta.h:52]; at power loss `fk` is already gone, so the device could not seal a new `.M` anyway. Metadata leakage is accepted (project names are plaintext in `/cfg` too); tamper protection comes from the blocks' AAD above.
- `.M` gains only three fields: `seal:1`, `hdr` (header length), `rk` (recipient `kid`s, at most 4). Today's worst-case `.M` is about 501 B, only about 11 B under 512 B (as computed by the feasibility review; `test_worst_case_fits_512_bytes` only asserts ≤512 [src test/core/test_rec_meta/test_main.cpp:104-119]), so adding `prof` and `rk` would exceed it, and docs/25 §4.6 also adds `dest` and `lock`: change `kMetaMax` to 768 (the worst-case `.M` with all four added must be recomputed in `test_rec_meta` [open]), and make `MetaText` static, not on the 6 KiB `rec_task` stack [src fw/rec/rec_meta.h:17, 46; fw/hal/audio_task.h:43].
- The `sha256` in `.M` is computed over the whole `.V` file from byte 0 (the header is never rewritten, so the Mac can hash the whole file); the device's incremental hardware SHA is used as before.
- **The Mac's transfer verification must change** (the first draft said "no change needed"; wrong): `verify.py` hard-codes `HEADER = 44` and hashes `data[44:]` [src cpad/recordings/verify.py:7, 17, 37]; it must take the header length by file type; `land.py` adds the WAV header after decryption.
- Every place in the firmware that assumes `size == 44 + bytes` must change: `rec_item_of` (`exact` and `dur_ms`), the size check in `del`, `even_data`, `needs_recovery`, `backfill` [src fw/rec/rec_meta.h:52; wav_header.h:38; rec_recovery.h:48; rec_engine.h:78].
- **Blocks do not line up with the ring buffer**: `gate()` drops the silent start, so the first write of a segment is not a full block; the block that spans a segment boundary is split by `RecWriter::write` (9,600,000 mod 4096 = 3,072 B) [calc]. So `RecWriter` gets a static 4096 + 16 B staging buffer: once 4 KiB has accumulated it encrypts and writes out one 4112 B block, and on segment switch and at the end it writes out the last short block. **No in-place encryption in the ring buffer**: `RecEngine::write` computes the level from the same `pcm` block after `writer_.write` [src fw/rec/rec_engine.cpp:127-129], so in-place encryption would make the level meter read ciphertext.
- **Power-loss recovery**: truncate after the header to whole blocks (4112 B); a stream without the "last block" flag is marked `rcv`. No more "fsync the header first": the existing once-per-second fsync already guarantees ordering, and an extra fsync can take up to 78 ms, while the worst-case segment switch already takes 290 ms [src fw/hal/audio_task.h:2; docs/11:242].

**outbox items**:
- Encrypted items get a different file name: `<MonoId>.sea` on cpafs, `.E?` on SD. Old firmware only counts names it does not recognise at the boot scan and leaves them alone [src fw/outbox/outbox_scan.h:11]; if they were still called `.json`, old firmware's decoder requires `body`/`text` [src fw/protocol/decode_device.cpp:11, 82] and would move them into `bad/`, which keeps only 20 entries, so they would be lost. Downgrading via Launcher is a common thing.
- Line format: `{…, "prof":…, "rk":[kid…], "enc":"<base64>"}`. What `enc` seals is a small binary, length-prefixed blob holding **all** fields of `body` (`text`, `project`, `intent`, `dest`, `how`, `live` added by docs/18 and docs/25, and any added later), not escaped JSON text, so 500 `"` characters do not become 1000 B. AAD = header hash‖`id`‖`kind`‖`prof`.
- `id`, `kind`, `prof`, `rk` stay in plaintext: the device's boot scan can still build the index, plus a per-item recipient bitmap (1 B per item, 8 bits for the 8 entries of the recipient table; 200 outbox items plus a 256-entry recording index, about 0.5 KB, allocated once at boot). This is the per-item filter that docs/18 P8-F2 needs.
- **Unsealed items carry no `enc`**, exactly as today. There is never an `enc:false`; `enc` has only one type.
- The protocol reserved the name `enc` long ago [src protocol/README.md:201].
- **Line length**: with 4 recipients the header is about 276 B and `enc` about 1.14 KB [calc]; the body moved from escaped JSON into binary, so the worst-case line must be recomputed against the 2048 B limit in `test_worst_lines`, adding a sealed `outbox.item` (4 recipients) [open].
- Avoid adding fields to `file.items[]` if possible: ciphertext and plaintext are told apart by file type. Today a 4-item page is 2044 B, only 4 B to spare [src protocol/tools/test_worst_lines.py:100]; if a field really must be added, `file_fit_page` drops a page to 3 items and the tests change with it. The `size` cap of 44 + 600000×32 in the schema and `test_worst_lines` must also include the header and the 16 B tag per block, about 75 KB more [calc].
- Today's firmware only reads stored lines back when sending and at the boot scan; there is no playback yet (checked by the feasibility review against `d330465`).

**Who gets what**: one more condition on top of docs/18's profile filter. Let proven(H) be the `kid`s Mac H proved in this handshake; visible(H) = `prof ∈ ok(H)` (always true without profiles), and the item is unsealed, or `rk` contains a `kid` in proven(H):
- Items 1–3 of docs/18 §3.5 and their test oracles are all computed with visible(H): `file.list` lists only those; for other segments `file.get` returns `E_UNKNOWN_ITEM` and `file.del` returns `ack` without deleting; `pending`, `files.n`, `files.rev` count only those. So for items sealed only to other Macs, the ↑ this Mac sees can drop to 0, and it cannot tell whether anything is happening on the other Macs' side.
- The device's reply to `hello` does not know proven(H) yet, so its counts exclude sealed items; after the proof passes, the next `pong` widens them (docs/18 §3.7).
- A Mac with a pinned key gets nothing before it proves (see "Pinning" above).

**On the Mac: store the ciphertext first, then decrypt** (D20):
- When cpad starts, and when the `cpa-key` path in the config or `keys/seal/` changes, it runs `cpa-key selftest` and puts into `hello`'s `seal.kids` only the `kid`s the self-test can open; if the result differs from last time it sends `hello` again. So even though the installer builds `cpa-key` only after cpad is already up (docs/27 §2.2 step 7), the next handshake still carries the public key.
- On receiving ciphertext: at least one recipient in the header is a self-tested `kid` → check `sha256` → write to `$CPA_HOME/sealed/<dev>/<fid>` (0700/0600) → fsync → ack or send `file.del`. The item is then done on the device side. If the header has no self-tested `kid` at all (a normal device would never deliver such an item): do not store, do not ack, do not reply `rejected`; send `hello` again as in docs/18 §7.1 D2, and stop as in D4 after 3 times in a row.
- Decryption runs separately: a work loop hands what is in `sealed/` to `cpa-key unseal --batch`, which verifies the tag of each block and checks `.M` and the authenticated fields in the header; on success it lands as usual (`land.py`, daily note, events), and then deletes the copy in `sealed/`.
- **When it cannot decrypt** (tool missing, broken, timeout): the ciphertext stays in `sealed/`, `cpa status` and the Mac app show "Waiting to decrypt N", and it retries later. The device's outbox keeps moving, and unsealed items are not affected.
- The first draft said "if it cannot decrypt, do not ack and do not send `file.del`". That does not work: the outbox sends in ID order, with at most 4 in flight and a resend every 30 s [src fw/outbox/outbox_core.h:1-7], so 4 undecryptable items would block everything behind them (plaintext ones included); at 200 waiting items it refuses new ones, recordings pile up on the card, the recording index has only 256 entries [src docs/18:568], and "recording always works" breaks.
- **Honestly**: if a Mac's private key is really lost (disk failure, no Time Machine), anything sealed only to it can never be opened, whether it is still on the device or already in that Mac's `sealed/` (§5.4).

**Randomness**:
- Nonces do not depend on randomness, but `fk`, `e`, `e′` and `n` do. `esp_random()` is pseudo-random when the radio is off [src docs/18 §8.2].
- **Seed taken once**: at the very start of `setup()`, before `M5Cardputer.begin()` starts the battery ADC [src firmware/app/src/main.cpp:76]: `bootloader_random_enable()` → read a block → `bootloader_random_disable()`. The header requires it to finish before RF and ADC are initialised [src SDK/include/bootloader_support/include/bootloader_random.h:28-40]; it is never called again.
- Then mix in: `esp_random()`; the SHA-256 of the first few ring-buffer blocks of each recording (low bits of mic noise); the `seal.salt` cpad sends in each `hello`. The host's salt only helps against those who have not seen it; the host that sent it knows it.
- Mixing fid into the HKDF for `fk` only guarantees that file keys differ if the DRBG output ever repeats; it does **not** make them unguessable. Unguessability comes from the seed: G-ENC measures it with the SP 800-90B min-entropy estimators, at cold boot, offline, radio off [open].

**CPU, heap, static RAM, which task does the work** (estimates before G-ENC measurements [calc]):
- AES-GCM per 4 KiB block is about 0.3–1 ms (hardware AES-CTR plus software GHASH); a block period is 128 ms, so core 0 load is <1–3%. Wrapping per file is <1 ms.
- One X25519 is about 20–80 ms, done only when the period key changes and for the handshake proof, **always in the loop task, never in `rec_task`**: `rec_task` has only a 6 KiB stack, and G-A6 requires ≥1.5 KiB headroom [src fw/hal/audio_task.h:43; docs/12].
- **Recording right after binding or switching profile**: the loop computes the new period key during the recording warm-up (codec still muted, at least 70–150 ms); the ring buffer can absorb 768 ms [src docs/11:242]. This case goes into G-ENC.
- **Static RAM**: staging buffer 4112 B + `.M` text 768 B + period key, handshake key and recipient table (table about 470 B) about 1 KB, about 6 KB in total. Today 66.7 / 98.3 kB, so it fits; but if docs/21 links WiFi in (about 92.1 kB), only about 6 kB is left, and the two cannot both be added [src docs/21:91, 336]. G-ENC measures it, and the docs/21 WiFi decision must account for it [open].
- **Heap**: libsodium's X25519 uses no heap; a GCM context is about 400 B. The floor is free ≥150 KiB and largest block ≥64 KiB, and while recording ≥96 / ≥32 KiB [src docs/12:455]; today free is 178.7 KiB and largest block 160 KiB [src docs/21:88]. **The whole app's heap while recording has not been measured on the real device yet** (docs/07 §6.7 item 6 is still open); SEC-3 measures it first. Mirrors add about 20–40 KiB.
- **Stack**: stack frames inside the precompiled mbedtls and libsodium are invisible to `stack_gate` and `stack_budget`. G-ENC must record `uxTaskGetStackHighWaterMark` for `rec_task` and the loop.

**No Flash erase/write while recording**: erasing one 4 KiB sector stalls the Flash cache on both cores, typically about 45 ms and up to about 400 ms, while I2S DMA can only absorb about 32 ms [src fw/hal/audio_task.h:2]; docs/21 already has "no NVS writes while recording". So writes to the recipient table, the "last crash" counter, coredump erasing and clearing internal storage all wait until nothing is being recorded or transferred; until then they are held in memory.

**When there is no key** (D8):
- Record in plaintext only when no Mac at all is registered on the device (new device, you have not approved any Mac yet), or, with profiles, when no Mac with a registered key exists in the current profile; the format is the same as today, without `enc`. With profile 0 and without profiles, seal to all registered Macs; if NVS is lost, restore from the mirrors (see "Mirrors" above).
- The first line of System → Encryption reads "Encryption: off (no Mac yet)" or "Encryption: Work has no key".
- Once SEAL has been on (`SEALON` recorded in NVS and on every mounted storage, see "Mirrors"), if an item falls back to plaintext: show a hint "This one is not encrypted" when it is recorded, add a "明" (plain) mark on the lock card, and System → Profiles shows "Plaintext waiting N".
- Reason: recording always working matters more than encryption. The alternative is refusing to record.

**What about earlier plaintext** (D12):
- **No switch**: if there is a registered key, seal. Old plaintext items sync as usual; you do not need to clear them first.
- Once the old ones have all synced, the device prompts once "Old plaintext leftovers are still on the card and in internal storage", offering two ways:
  - System → Encryption → "Clear internal storage": selectable only when the outbox is empty and nothing is being recorded or transferred; erases the whole `cpafs` and reformats it, about 10–20 s [open].
  - `cpa sd scrub`: with the card in the Mac's card reader, fill `/cpa` with all-zero files, fsync, then delete them, overwriting old plaintext in free clusters. It does not touch Launcher's files and does not format. Writing zeros does not reach spare blocks in the card's FTL [inf].
- **Do not format the card** (the first draft suggested "zero the whole card and format"): macOS formats cards larger than 32 GB as exFAT by default, IDF 4.4 cannot mount that, so the device would have no usable card and could not record; formatting would also delete Launcher's `/config.conf`, `/downloads/`, `/bkp/` [src docs/gates/launcher.md:15, 138].
- When `cpa sd scrub` finds a cpa backup in `/bkp/`, it tells you where it is and how to delete it in Launcher; it does not delete Launcher's things itself.

### 4.4 Device: other locations

- **`/cfg` project cache**: the device itself has to show the names for 1–9; if it were encrypted the device could not read it, so it **stays plaintext**, documented in SECURITY.md. It holds the project names you chose, pushed from the Mac, and no bodies.
- **NVS**: SEAL only adds public keys, which are public. `host_label` defaults to `Mac·` plus 4 random hex digits (docs/27 D9, revising docs/23 D10's "empty by default"); it contains no real name and does not use profile words like "Home" or "Work". If docs/21 W2's WiFi password is ever done, besides the invariants in §5.6, it must also state "whoever has the device has the password".
- **coredump** (D11):
  - Release firmware, when the loop is idle and nothing is being recorded or transferred, first calls `esp_core_dump_image_check()`, and continues only if there is content [src SDK/include/espcoredump/include/esp_core_dump.h:114].
  - Read `app_elf_sha256` from `esp_core_dump_get_summary()`: if it matches the running firmware, increment the "last crash" counter (visible on the System page); if not, it was left by other firmware under Launcher (e.g. Bruce), and is not counted [src esp_core_dump.h:41, 158].
  - Then erase with `esp_partition_erase_range` in separate 4 KiB pieces, 16 pieces in about 0.7 s [open]; do not use `esp_core_dump_image_erase`, which erases everything at once [src esp_core_dump.h:134]. If recording starts midway, stop, and continue at the next idle.
  - Test firmware follows the build flag `CPA_COREDUMP_KEEP`: when set, keep the dump for debugging. It is separate from `CPA_TEST_HOOKS`, so S2-d can trigger a panic with the test hooks and then verify erasing.
  - Find the partition by type, do not cache the offset (Launcher moves partitions [src docs/gates/launcher.md:48]).
  - Not chosen: "delete the last coredump line in `cpa_8MB.csv`". It is the last line, so deleting it moves nothing, but under Launcher it would still need erasing; one path is simpler than two.
- **Leftovers after deletion**: SEAL is the fix, relying on "once the key is gone, the ciphertext is useless". No "overwrite before delete": the SD card's FTL redirects writes to other blocks, so an overwrite does not necessarily land in the same place, and each segment would need another 9.6 MB written [inf].
- **Untrusted USB hosts** (D17):
  - **Separate passive from active**: the firmware uses the built-in USB serial/JTAG (`ARDUINO_USB_MODE=1`) [src platformio.ini:36]. Without a key press, any USB host, and any firmware started from Launcher (NVS is shared), can: read the whole Flash; write NVS (add its own public key to the recipient table, or erase the table to force plaintext); flash any firmware; attach to the running chip through USB-JTAG with OpenOCD without a reset, and read `fk`, `Sᵢ`, drafts and the mic ring buffer from memory. So "only ciphertext after SEAL" holds only for "one passive read".
  - **The part that can be detected**:
    - System → Encryption lists every recipient with its fingerprint, so you can see an extra one;
    - cpad remembers "this device has sent sealed items" and the `kid`s it has seen in headers: if later it sends unsealed items (including D8 fallbacks, which are still flagged), if `seal_chal.kid` is not this Mac's key, or if a never-seen `kid` appears in a header for the first time ("The device has a new recipient 9C1B·27E0. Is it the Mac you just approved? Check System → Encryption"), it alerts in `cpa status` and the Mac app (docs/27 §3.5 item 3).
    - None of this stops a device whose firmware has been replaced, or someone who changed both the table and the mirrors.
  - **Write into SECURITY.md and the README**: "Before going to an untrusted computer, sync to zero at home first; when only charging, use a charge-only cable or a data blocker."
  - **Research items parked, not in M-SEC**: disabling USB reset while locked (`RTC_CNTL_USB_RESET_DISABLE` in `RTC_CNTL_USB_CONF_REG`, bit 17, semantics not documented in the IDF 4.4 headers) and the USB-JTAG bridge (`USB_SERIAL_JTAG_USB_JTAG_BRIDGE_EN`, bit 16, not verified). Blocking reset alone does nothing, since JTAG can read memory without a reset; even if both bits work, they cannot stop powering on with G0 held.
- **Memory**: not handled. Someone who attaches JTAG while the device is on gets the current period key, and so can open the files written in this period (≤1 hour). A fresh ephemeral key per file would not stop such a person either: they are reading the mic ring buffer at the same time.

### 4.5 Mac: the decryption tool `cpa-key`

- **What it is**: a small Swift command-line tool in the same SwiftPM package as the Mac app (`mac/`, docs/24 §7), but a separate executable target. It uses only Apple's CryptoKit (Curve25519, HKDF, AES.GCM, ChaChaPoly, HMAC) and **links no third-party code**. The daemon still depends only on `pyserial`, and item 1 of ADR 0004 is unchanged [src docs/adr/0004-local-data-access-mcp.md:57].
- **Subcommands**:
  - `init`: generate a private key, write it to `$CPA_HOME/keys/seal/<kid>.key` (0600, directory 0700), print the public key and kid;
  - `pub`;
  - `selftest`: open a known-answer sample with each private key it holds; run by cpad at startup;
  - `prove`: compute the handshake proof for the device's challenge;
  - `unseal --batch`: cpad starts one child process and sends one request per line over stdin/stdout, so it does not start a process for every note; cpad sets a timeout and kills and restarts it on timeout;
  - `rotate`: generate a new in-use key; old ones are always kept for decryption only (§5.3), with no option not to keep them;
  - `wipe`: delete all private keys in `keys/seal/`; used only by `cpa host reset --wipe-keys` (§5.5 item 3).
  - **No `export` / `import`** (D7).

  Always argument arrays, never through a shell; keys never appear in argv, stdout or logs.
- **How cpad finds it**: only an absolute path from the config, checked to be owned by you and not writable by others (the same check WhisperWoof does on `cpa`, docs/23 §6.1).
- **Always installed**: the installer (docs/27 §2.2 step 7) always installs `cpa-key` and runs `init` and `selftest` in the foreground; it is not "done if it happens to be installed". The first-run "Connect" stays greyed out until `selftest` passes (when `swift` is available), so the first handshake already carries the public key. If it cannot be built (e.g. no Swift toolchain), `cpa doctor` says so clearly, and the device shows "Encryption: off".
- **When it cannot decrypt**: see "store the ciphertext first, then decrypt" in §4.3.
- **Its own parsers must be hardened too**: the header and container parsers consume bytes from a hostile device or hostile card; they go into the mutation tests in §6.6.
- **Alternatives**: change ADR 0004 to allow `cryptography` (a large wheel with native code); the external `age` program (not installed, would come from outside); a pure-Python implementation (slow, easy to get wrong).

### 4.6 Mac: the archive and small fixes

- **Default**: FileVault (already on) + encrypted Time Machine. 👤 Please select the backup disk in System Settings → General → Time Machine and check whether it says "Encrypted". After SEAL the private key file is also in the backup, which is one more reason to encrypt backups. Files stay open.
- **No per-file encryption** (D13): it would block Obsidian, grep, WhisperWoof, QuickLook and Spotlight. Yet cpa and MCP must decrypt to read, so programs of the same user could still read through them; and cpad would need access to the key, which amounts to handing the key to any program that can run `cpa`.
- **Optional, documented as a practice only, not built as a feature**: put the notes directory in an encrypted APFS sparse disk image mounted at login. Costs:
  - cpad and cpa-stt both have to handle "the notes directory is not mounted yet";
  - Obsidian and WhisperWoof paths have to change to `/Volumes/…`;
  - once mounted, it is still open to programs on this Mac.

  It only makes sense if you want to put backups somewhere unencrypted.
- **Spotlight**: keeps indexing, so you can search your notes; the index is readable only by root and is inside FileVault. Excluding it means changing system settings, which is your call.
- **Small fixes** (SEC-0, no decision needed):
  1. **launchd logs**: in the `install()` of both cpad and cpa-stt, create `launchd.{out,err}.log` as 0600 before bootstrap (launchd sets permissions only on files it creates itself); `secure_home` chmods everything in `logs/*` to 0600 at startup, fixing the existing ones [src cpad/service/launchd.py:50-60; stt/service.py:31-34]. `secure_home` also covers `rec-staging/`, `stt/`, `hook-backups/`, `keys/`, `sealed/`, no longer just 5 named files [src cpad/config/settings.py:157-166].
     **No `Umask` in the plist** (the first draft had it): child processes inherit the umask. cpad runs `claude` in project directories, and docs/25's adapters run other apps; files they create would become 0600 and directories 0700 [inf], which could break Docker mounts and dev servers.
  2. At startup cpa-stt cleans up `$TMPDIR/cpa-stt-*`, but only those owned by you, older than 1 hour, and not in use by any process [src stt/runner.py:87-93].
  3. **Body hashes in logs**: rename the field in `cpad.log` to `h8`, whose value is the first 8 hex digits of HMAC-SHA256(per-Mac salt, body); the salt is generated randomly on this Mac (0600, not exported). So short notes cannot be guessed, and logs from two Macs cannot be correlated. Only `redact.py` changes [src cpad/logging/redact.py:19-22], with `test_control_service_logging.py` updated to match. **`sha8` in events is unchanged**: in docs/22 §5.6 and docs/23 it is an event field that `inbox_start` compares against [src docs/22:611; docs/23:224]; events already store the body, so one more unsalted hash leaks nothing extra.
  4. `hook-backups/`: for each target (`~/.claude/settings.json`, each project's `settings.local.json`, all in one directory) keep the **first copy** (the original from before cpa was installed) and the latest 2. The first draft's "keep only the latest 5" would delete the original.
  5. Change the development recordings in `firmware/hwtest/captures` to 0600, or move them into a 0700 directory. Leave `backups/` alone; just remind you it is in the Time Machine backup.
  6. Clipboard: docs/25 §5.3's `ClipWriter` (docs/25 D9) has already settled this, and this doc adds nothing: cpad calls `NSPasteboard` through ctypes (not Swift), local only, marked concealed and transient, cleared after 2 minutes; when ctypes does not work and it falls back to `pbcopy`, you have to press once to confirm. This doc only checks it in M-SEC (S3-d).
- **Secret-looking keys in `spawn_env`** (moved to SEC-1, together with `cpa doctor`): `cpa doctor` warns about keys named like `*KEY`, `*TOKEN`, `*SECRET`, `*PASSWORD`; only refuse them when cpa itself writes this setting. **Do not error when reading the config**: if that went into `_spawn_env` [src cpad/config/settings.py:86-89], an old hand-edited config would stop cpad from starting. The launchd side already copies only `PATH`, `HOME`, `LANG`, `SHELL`, `USER`, `LC_*` [src cpad/service/launchd.py:20, 44].

## 5. Key management

### 5.1 Who holds what

| Item | Where | Secret? | If lost |
|---|---|---|---|
| SEAL private keys (one in use per Mac, plus old keys kept for decryption only) | `$CPA_HOME/keys/seal/<kid>.key` (0600, directory 0700); not in `cpa export` or settings export; in Time Machine | Yes | Items sealed only to that Mac cannot be opened (§5.4) |
| SEAL public keys, kid | `$CPA_HOME/keys/seal/pub.json`; device NVS and the cpafs mirror; a copy on SD (restored only after the Mac proves) | No | Pushed again in the next `hello`, needs your confirmation on the device |
| `host_id` | `$CPA_HOME/host_id` (0600); the device stores only the hash | Yes (used as a password) | Bind again |
| File key `fk` | In device memory, zeroed when the file closes; also only in memory when the Mac decrypts | Yes | — |
| Period key `e` and `Sᵢ`, handshake key `e′` | Device static memory, zeroed on period change or after the handshake | Yes | — |
| Ciphertext waiting for decryption | `$CPA_HOME/sealed/` (0700/0600) | Ciphertext | Can be opened while the private key exists |
| Salt for log HMACs | `$CPA_HOME/keys/log_salt` (0600) | Not very sensitive | `h8` in old logs no longer matches; does not matter |
| Device authentication key (only with D10) | Device NVS and the Mac | Yes, but having it only lets you impersonate the device | Revoke with `cpa device forget` |

- **Be honest that `host_id` is still a password**: it is sent in plaintext to any board on the first handshake [src docs/18:223], and anyone who copies `$CPA_HOME` gets it too. With it, one can impersonate that Mac's binding (the first draft said "someone who copies `$CPA_HOME` cannot get the binding", which does not hold against a deliberate attacker). `host_hw` only makes an **honest** cpad change its own identity in a copied directory; it does not stop a modified client (docs/27 §3.5 item 7).
- **After SEAL**: an impersonator has no private key and cannot pass the handshake proof, so it cannot get or delete sealed items; for a `host_id` with a pinned key, without the proof it cannot even get unsealed items (§4.3 "Pinning"); for Macs with no key registered yet, unsealed waiting items are still protected only by this password. The private key file is also in `$CPA_HOME`, so if the whole directory is copied (e.g. from an unencrypted Time Machine disk), both are lost, which is the same as the archive itself being copied (§5.2).

### 5.2 Where the private key lives (D6)

- **Default: a 0600 file** in `$CPA_HOME/keys/seal/`.
- **Why not the Keychain** (the first draft's default):
  - `cpa-key` is ad-hoc signed (docs/24 D12), and its signature changes on every rebuild, so the Keychain's access control would ask again.
  - That dialog asks for the login password, and it would pop up from a launchd background process in the middle of a sync. This project is rebuilt often, not "a few times a year". It would train you to type your login password into dialogs that appear out of nowhere.
  - "Signing with a local self-signed certificate stops the prompts" is unverified; the Keychain may still track the build hash [open].
  - What the Keychain buys is also smaller than the first draft claimed: the login keychain file itself is in Time Machine too, protected only by the login password, and can be guessed offline if the backup is unencrypted.
- **Cost of a file**: if Time Machine is not encrypted, the private key is in the backup in plaintext; whoever copies `$CPA_HOME` gets it. Both are the same exposure as the archive itself, so the remedy is the same too: encrypt Time Machine (👤 §8.2 item 4), and `cpa doctor security` warns when it is not.
- **Never leaves this Mac**: not in `cpa export`, not in settings export (docs/27's leak check adds `keys/`); it travels only with Time Machine and Migration Assistant.
- **No keys on the command line**: only `cpa-key` reads this file; nothing like `security -w` that would put the key in the process list.
- **Alternative: the Keychain**. If done, all three together: no prompts for background access (if it fails, the item stays in `sealed/` and waits); add a foreground `cpa-key authorize` (or a button in the Mac app) that prompts while you are at the Mac; build `cpa-key` separately and do not rebuild it when its source has not changed.

### 5.3 Rotation

- `cpa-key rotate` generates a new key; old keys are kept for decryption only and never deleted automatically. The next `hello` pushes the new public key, and in the same handshake the Mac proves with both the new and old keys, so the device replaces this Mac's entry with a one-line note; if it cannot prove the old one, the device asks, cursor on "Don't switch" (§4.3 "Pinning"). Old files still open.
- **Automatic rotation on new hardware** (D23): when `host_hw` does not match (Migration Assistant, restoring onto another Mac), cpad switches to a new `host_id`, and `cpa-key rotate` makes a new key too, keeping the old one for decryption only. So a new Mac never uses the old Mac's key as its in-use key.
- **`cpa host reset` rotates too**: it runs `cpa-key rotate` at the same time as switching to a new `host_id`; otherwise the in-use `kid` the new identity brings is already in the device's table.
- **Honestly, how far the separation goes**: after migration the new Mac still has the old Mac's private key (decrypt-only is a rule, not cryptography) and can prove the old `kid`. The old Mac's `host_hw` still matches, so it does not rotate by itself; if it is turned into the work Mac without running `cpa host reset`, the new Mac can also open the work files sealed to it, and only docs/18's profile filter keeps the two apart. So before repurposing the old Mac, run `cpa host reset` on it first (§5.5 item 3); only then are Home and Work separated cryptographically.
- When the old `kid` a new Mac proves is exactly the in-use key of another bound Mac, the device shows ""Mac·8C1D (new)" also has "Mac·8C1D"'s key" and asks "Forget the old one?", cursor on "Keep".
- **When to rotate by hand**: only when you suspect the private key has leaked; no scheduled rotation. There are no secrets on the device, so scheduled rotation gains nothing.
- There is nothing on the device to rotate.

### 5.4 After a loss

- **Lost device**:
  - There are no secrets on the device, so nothing needs revoking on the Mac; with D10, revoke its authentication key with `cpa device forget`.
  - What is lost is whatever had not been transferred since the last sync.
  - With SEAL on, the finder gets only ciphertext and metadata; without it, plaintext.
- **Lost or broken Mac** (D22):
  - The archive relies on FileVault.
  - **Forget** this Mac under System → Profiles on the device, so new records are no longer sealed to it or delivered to it.
  - Before forgetting, the device warns "N items can only be opened by this Mac", cursor on "Keep for now".
  - After forgetting, one more line "Drop N items that cannot be opened", cursor on "Keep"; choosing it needs a confirmation. If not dropped, these items no longer count as waiting, so they do not occupy the 200-item quota forever, and do not block docs/18's "turn off profiles" forever (it requires zero waiting items [src docs/18:332]).
- **What about items sealed only to that Mac**:
  - Restore from Time Machine, or move to a new Mac with Migration Assistant: the private key files come along; the new Mac rotates to a new key but keeps the old ones, can prove the old `kid`, and the device hands over these files (§4.3 "Who gets what").
  - **Without a backup, they can never be opened.** The first draft's "losing the key … never loses data" went too far.
  - When one profile has two Macs bound, every file is sealed to both, so if one breaks the other can still open them.
  - No passphrase-protected export (D7): it would ask you to remember a passphrase and manage a file, while Time Machine already carries the private key; with two Macs bound, losing one is not a problem either.

### 5.5 New Mac (continuing docs/27)

The new-Mac setup flow is in docs/27. The key-related agreements are these six:
1. **A brand-new Mac**:
   - Setup generates `host_id` and `host_hw`, **always** installs `cpa-key`, and runs `init` and `selftest` in the foreground (docs/27 §2.2 step 7). This is done before cpad's first handshake with the device, so that `hello` carries `hello.seal`.
   - On the first handshake after you click "Connect" on the Mac (docs/27 §3.1, O3), the device shows a choice sheet: docs/18 §5.1 if the device has never used profiles, §5.2 if it has. Choosing "Home" or "Work" and confirming also registers this Mac's key. If you choose "No profiles", or profiles were never used, the sheet is "Let this Mac unlock the records on the device?", cursor on "Later" (§4.3).
   - From then on, new records are sealed to this Mac (and the other Macs in the same profile).
   - Items sealed only to the old Mac stay on the device until the old Mac picks them up; System → Profiles shows "Waiting for "Mac·8C1D" ↑3".
2. **Moved over from an old Mac with Migration Assistant**: `host_hw` does not match, so `host_id` is renewed and SEAL also rotates to a new key, keeping the old one for decryption only (§5.3). The device asks to bind again (docs/18 §4.1); after confirming, the new Mac can prove the old `kid`, and old waiting items can be delivered too.
3. **A Mac no longer used**: forget it on the device, and drop items that cannot be opened if needed (§5.4); on the Mac, change identity with `cpa host reset`, which also runs `cpa-key rotate` (§5.3). To give it away, use `cpa host reset --wipe-keys`: it first warns "N items in `sealed/` are not decrypted yet; items on the device sealed only to this Mac can never be opened once the keys are deleted", requires typing `y`, then runs `cpa-key wipe` to delete all private keys in `keys/seal/`.
4. **Recognising the hardware**: the Mac recognises the device today by the USB serial number bound after cpad's first handshake; serial numbers can be forged (T-ROGUE), so this only guards against plugging in the wrong device, not against impersonation; D10 likewise only stops boards that have never touched this device (a board that has read its NVS can still impersonate it), and after SEC-7 the serial number is recorded only from a verified `hello` (docs/27 O3 step 4 changes accordingly). The device recognises the Mac by the `host_id` password; after SEAL the handshake proof is added, and that step is real recognition. docs/27's UI must not call the serial number "verified".
5. **Setup itself**: no new ports; network only to fetch code and pinned dependencies (the installer's `git fetch --tags`, `uv build`, `uv tool install`); the first-run page itself never goes online. The last step runs `cpa doctor security` once, telling you the state of FileVault and Time Machine encryption, and whether the notes directory is in iCloud.
6. **Aligned with docs/27 (PR #67)** (docs/27 `58ed685` is already updated; below are the agreements themselves):
   - The private key lives in a 0600 file under `$CPA_HOME/keys/seal/` per D6 here, not in the Keychain; not exported; old keys are not deleted on new hardware.
   - When O4 regenerates `host_id`, and on `cpa host reset`, `cpa-key rotate` runs too (D23, §5.3).
   - Field names as in §4.3: `hello.seal{kid, pk, kids, salt}`, `seal_chal{E′, n, kid}` in the reply to `hello` (`kid` is the one the device has pinned for this Mac), `ping.seal_proof[]`. Not in `prof.ok`.
   - The toolchain point: ChaCha20-Poly1305 is in libsodium; GCM is hardware AES-CTR plus software GHASH.

## 6. Security test round M-SEC (milestone checklist)

Two rounds, with results written to `docs/gates/sec.md`. The automated part runs on every release after that.
- **Baseline round** (SEC-3, now): all of §6.1, S1-a and S1-b in §6.2 (S1-c needs docs/18 P8-S's three fake Macs, which only exist after v0.8, so it goes into the encryption round); on the real device only S2-d (coredump erasing, testing the new D11 code), and along the way measure the whole app's heap while recording and transferring files (S2-g, needed by G-ENC). That the device holds plaintext today is already known; it is not worth having you present, triggering a panic, and imaging the whole card with sudo to prove it.
- **Encryption round** (SEC-6, after SEAL): everything, and everything must pass. S2-a to S2-c are combined into one script run once, needing sudo only once.
- **New-Mac part** (docs/27 §7.2): part 1 touches only the Mac and does not test encryption; it runs once on its own after docs/27 O0–O6 are merged, with results also recorded in `sec.md`, and then runs with the automated part on every release; part 2 (including key rotation on migration and `cpa host reset`) joins the encryption round's checklist.

### 6.1 Automated (CI, no device)

- [ ] **S0-a fuzzing**: libFuzzer + ASan/UBSan. Built with a plain `clang++` script on ubuntu-24.04, not as a PlatformIO environment: Apple clang has no libFuzzer runtime, and Unity's `main` conflicts with libFuzzer's. Hooked into the existing `fuzz-nightly.yml`. Targets are all parsers that read card files and host messages: `decode_file`, `rec_meta`, `outbox_scan`, `sd_item_name` / `rec_parse_name`, the `/cfg` cache, `b64`, `line_framer`; after SEAL, add the SEAL header parser and the power-loss recovery scan of encrypted segments (recipients capped at 4, blocks capped at `bytes/4112`). Seeds come from `protocol/vectors`; each target runs 10 minutes nightly, and the corpus is committed to the repo. Pass: 0 crashes, 0 sanitizer reports.
- [ ] **S0-b**: run the existing native test suite again with `-fsanitize=address,undefined`.
- [ ] **S0-c**: where the daemon reads device files and third-party files, generate malicious input with Hypothesis (already a dev dependency): `recordings/{contract,land,verify}.py`, `results.py` (status and result written by other transcribers), `stt/parse.py` (whisper's stdout), the filing and box JSON.
- [ ] **S0-d `test_private_modes`**: after running the whole flow, walk every path cpa has written, and assert directories 0700, files 0600, no symlinks, owned by you; the launchd logs of cpad and cpa-stt are both 0600; neither plist has `Umask`.
- [ ] **S0-e canary leak test**: run the whole flow with a canary string (including cpa-stt being SIGKILLed midway), and assert it is not found in `logs/`, `mcp-audit`, `stt/state.json`, the launchd logs, events that should not carry bodies, or `$TMPDIR` after the startup sweep; after SEAL, also that `sealed/` holds only ciphertext.
- [ ] **S0-f**: gitleaks, plus the identifier rules from #39 (MAC placeholders, session IDs), plus one more: no Flash or SD dumps in the repo (`*.bin`, also added to `.gitignore`).
- [ ] **S0-g**: no `debug.*` in release firmware (already in place: ci.yml:105, release.yml:37), plus two more: no test keys, and no `CPA_LOG_CONTENT` or `CPA_COREDUMP_KEEP`.

### 6.2 Simulator

- [ ] **S1-a hostile SD card corpus** (`store=sd`): over-long or non-8.3 names, bad JSON, id not matching the file name, more than 256 segments, `.M` claiming more bytes than the file has. Pass: no crash, no hang, nothing uncommitted delivered, bad files quarantined.
- [ ] **S1-b fake device** `fake_device`: unknown serial number, forged bound serial number, forged `cpa-fw/`. Record what cpad sends before and after binding, as the baseline for D10.
- [ ] **S1-c** (after v0.8, encryption round): docs/18's G-PROF-1 (three fake Macs) canary test still passes.
- [ ] **S1-d fake Mac** (after SEAL): a fake Mac with the right `host_id` but no private key gets no sealed item at all, and its acks and `file.del` have no effect; the same `host_id` with a different public key makes the device ask for confirmation, cursor on "Don't switch"; **the same `host_id` with a `hello` without `seal`** is treated as "unassigned", with no unsealed waiting items, snapshots or online requests; pushing a low-order point as a public key is rejected; a planted copy of the recipient table on SD is not restored without proof.
- [ ] **S1-e stuck `cpa-key`** (after SEAL): a fake `cpa-key` that never answers must not block the device's outbox, nor unsealed items; the ciphertext stays in `sealed/`, and "Waiting to decrypt N" matches.

### 6.3 Real device 👤 (one device at a time, you present, using a test device)

**Dump hygiene** (treated like `backups/`): a Flash dump contains Launcher's `l_wifi` and NimBLE pairings, and an SD image may contain other firmware's data.
- Write dumps outside the repo, in a 0700 directory; whether to exclude it from Time Machine is your call (👤 §8.2 item 4);
- The canary-search script prints only offsets and regions, never content;
- Delete after use;
- Dumps are banned from the repo (S0-f).

**Canaries**: a fixed string for notes; for audio, a fixed PCM pattern fed in through a test hook (a tone recorded by the real microphone cannot be searched for byte by byte).

- [ ] **S2-a** (encryption round): plant canaries, sync and delete, trigger one panic through a test hook (built with `CPA_COREDUMP_KEEP` off), then `esptool read_flash 0 0x800000` and search NVS, cpafs, coredump and the app partition. Not a single one may be found.
- [ ] **S2-b** (encryption round): take a read-only image of the SD card with a card reader (needs sudo once), and search the image, including free clusters and Launcher's `/bkp/`.
- [ ] **S2-c** (encryption round): browse `/cpa` with Launcher's SD manager and WebUI (docs/18 T8); only ciphertext is visible.
- [ ] **S2-d coredump** (both rounds): panic via test hook (`CPA_COREDUMP_KEEP` off) → reboot → erased when idle, "last crash" +1; a dump with a different `app_elf_sha256` is still erased but not counted; never erased while recording.
- [ ] **S2-e G-ENC**: seal while recording, and measure: AEAD time per 4 KiB block (both AES-GCM and ChaCha20-Poly1305); X25519 time (both libsodium and mbedtls); total time of a 1 + 5 handshake proof; lowest free heap and largest block; stack high-water marks of `rec_task` and the loop; static RAM; `ring_high`, `mic_missing == 0`; worst-case segment switch time ≤ ring buffer headroom; **recording right after binding or switching profile**; min-entropy of the seed at cold boot offline (SP 800-90B); recovery by whole-block truncation after power loss (rerun G-A8 on encrypted segments).
- [ ] **S2-g heap baseline** (baseline round): lowest free heap and largest block of the whole app while recording and while transferring recordings (docs/07 §6.7 item 6); G-ENC uses it as the reference.

The first draft's S2-f (blocking USB reset while locked) is removed; the research item is parked (§4.4).

### 6.4 Mac

- [ ] **S3-a `cpa doctor security`** (read-only): checks FileVault; the Time Machine destination and encryption (says "unknown" when the disk is not mounted, warns when unencrypted: the backup holds the archive in plaintext and the private key); `tmutil isexcluded`; whether the notes directory and exports are in iCloud; file permissions, including `keys/` and `sealed/`; "Waiting to decrypt N"; cpa-stt temp leftovers; the owner and signature of `cpa-key`; secret-looking keys in `spawn_env`. Has `--json` output, which the Mac app shows as a card.
- [ ] **S3-b** 👤: you check whether Time Machine is encrypted.
- [ ] **S3-c** (after SEAL): scan `ps`; keys do not appear in any process's argv; private key files are 0600; `cpa export` and settings export contain no `keys/`.
- [ ] **S3-d** (after docs/25 R1): rerun the tests of docs/25 §5.3 `ClipWriter` here: local only, concealed and transient types, cleared after 2 minutes; falling back to `pbcopy` needs a confirmation.

### 6.5 Mac app (docs/24, once it is merged into main)

- [ ] **S4-a**: add a few rules to the source-rule tests (`RulesTests`): the app does not read `$CPA_HOME/keys` and does not call `security`; the security card comes only from `cpa doctor security --json`, and its first line is "Encryption: on (sealed to …)".
- [ ] **S4-b**: the existing environment allow-list, timeout and 4 MiB cap in `ProcessTests` all stay.

### 6.6 The cryptography itself (after SEAL)

- [ ] Known-answer vectors in `protocol/enc/v1/`, run by both the device's native C++ and the Mac's `cpa-key`; HKDF with the RFC 5869 vectors, X25519 with RFC 7748's, including low-order points that must give all zeros and be rejected.
- [ ] Property tests: under one `fk` each block number is encrypted only once; only the last block is short; power-loss recovery never re-encrypts; a failed open retries with a new `fk`; a different recipient cannot open it; changing any single byte makes it fail to open; truncation loses only the last incomplete block; reordering segments, splicing in segments from another recording, changing `prof`, changing fields in `.M` are all detected.
- [ ] Work keys cannot open home files (extending G-PROF-1).
- [ ] Handshake proof: a wrong private key gets nothing; a proven Mac can only ack and delete items whose recipients include it.
- [ ] Store first, decrypt later: ack only after the ciphertext is on disk and the hash matches; simulate failure, timeout and being killed with a fake `cpa-key`; `sealed/` is kept and the outbox keeps moving.
- [ ] `cpa-key`'s header and container parsers: random and mutated input (Swift tests), no crash, no out-of-bounds.
- [ ] Power-loss recovery scan of encrypted segments (using `rec_recovery`'s power-loss injection).
- [ ] Downgrade: old firmware's boot scan leaves `.sea`, `.E?` and `.V?` as they are and does not move them into `bad/`.

## 7. Phases and work packages

Always write the failing test first (RED → GREEN → REFACTOR).

| Phase | Content | Touches | Code / tests (lines) [calc] | Depends on | Waits for you? |
|---|---|---|---|---|---|
| **SEC-0 Mac small fixes and doc corrections** | Items 1–5 of the §4.6 small fixes; §3.3's corrections to SECURITY.md; launcher.md:217 | `cpad/service/launchd.py`, `stt/service.py`, `cpad/config/settings.py`, `stt/runner.py`, `cpad/logging/redact.py`, `hook_settings.py`, `test_control_service_logging.py`; docs | 110 / 240 | None | No |
| **SEC-1 automated tests and health check** | All of §6.1; `cpa doctor security` (including the `spawn_env` warning) | The fuzzing `clang++` script and targets, `fuzz-nightly.yml`, `ci.yml`; new `cpad/cli/doctor.py` | 400 / 1,000 | SEC-0 | No |
| **SEC-1b Mac app security card** | §6.5 | `mac/` | 50 / 100 | docs/24 merged into main | No |
| **SEC-2 device hygiene and simulator** | coredump erasing (D11); S1-a, S1-b in §6.2 | The firmware's boot and idle flow; simulator fuzz corpus; `daemon/tests/support/fake_device.py` | 180 / 450 | None | Done with the D11 default |
| **SEC-3 baseline round** 👤 | Results of §6.1, §6.2; real-device S2-d and S2-g | Scripts in `tools/hil`; `docs/gates/sec.md` | 40 / 60 | SEC-1, SEC-2 | You present once, briefly |
| **SEC-4 gate G-ENC** | The probe firmware and measurements of §6.3 S2-e | Test environment | 250 / 150 | SEC-3's heap baseline | 👤 item 1 |
| **SEC-5 SEAL (= docs/18 P8-E)** | `protocol/enc/v1` (spec, vectors, checker, handshake proof); firmware `fw/seal/` (pure-function core + HAL): registration and recipient table, mirrors, period key, handshake proof, `RecWriter` staging buffer, `.V`/`.E`/`.sea`, new `.M` fields, every `44 + bytes` place, recipient bitmap, plaintext fallback, System → Encryption, forget and drop, clear internal storage; `cpa-key` (Swift); cpad: `seal` fields, `sealed/` and the decryption loop, `verify.py` and `land.py`, supervising `cpa-key` with timeouts, alerts, `cpa status`, `cpa sd scrub` | Protocol, firmware, `mac/`, `cpad/recordings/`, `cpad/protocol/` | Protocol 200 plus vectors; firmware 1,300 / 1,500; Swift 350 / 450; cpad 650 / 900 | G-ENC passed; docs/18 P8-F1, P8-F2 (binding and `prof`), P8-F3 (confirm box, forget and drop lines, System → Profiles; or docs/27 O7's minimal confirm box when that is enough); docs/24 (the `mac/` package) | 👤 items 1–3 |
| **SEC-6 encryption round** 👤 | Rerun all of §6, everything must pass; one script for S2-a to S2-c | `tools/hil` | 120 / 200 | SEC-5 | You present |
| **SEC-7 (optional) authentication key** | D10: HMAC challenge in `hello` (L2 in docs/05 §8); items and `.M` on SD carry a MAC, and those that fail verification are quarantined; record the serial number only from a verified `hello` (changes docs/27 O3 step 4: the serial number written from the USB list at binding only counts once verified) | Protocol, firmware, cpad | 400 / 500 | SEC-5 | D10 |

**Totals** [calc]: SEC-0 to SEC-3 are about 780 lines of code and 1,850 lines of tests, with no decision needed (SEC-2's coredump follows the default; SEC-1b waits for docs/24). SEC-4 to SEC-6 are about 2,870 / 3,200 (the G-ENC probe counted as code). Firmware at 1,300 / 1,500 is 30–40% more than the first draft's 900 / 1,100: the change surface includes rec's size arithmetic, a second file type, encoding and decoding, deferred NVS writes, the recipient bitmap and the UI. The first M-SEC round can go out with the next release; the second is scheduled after v0.8 (profiles), as the theme of v0.9 [assume].

```
SEC-0 ─► SEC-1 (then on every release) ─┐
SEC-2 ──────────────────────────────────┴► SEC-3 baseline round ══ M-SEC round 1
docs/24 merged into main ─► SEC-1b
docs/27 O0–O6 ─► new Mac part 1 (run on its own, recorded in sec.md)
docs/18 P8-F1, F2, F3 (or docs/27 O7) ─┐
docs/24 merged into main ──────────────┤
SEC-3 ─► SEC-4 G-ENC ──────────────────┴► SEC-5 SEAL ─► docs/27 O8 ─► SEC-6 encryption round (incl. new Mac part 2) ══ M-SEC round 2 ─► SEC-7 (optional)
```

## 8. Decisions, and what needs your call

### 8.1 Decisions

If there is no reply, the default is used.

| # | Decision | Default (recommended) | Alternatives | Why | See |
|---|---|---|---|---|---|
| D1 | What "all data file-encrypted" means | Each file sealed individually on the device; whole-disk encryption on the Mac, backups encrypted too; files on the Mac open to your own apps | Per-file encryption on the Mac as well | Per-file encryption does not stop programs reading through cpa/MCP, but does block your own apps | §1.2, §4.6 |
| D2 | Device encryption scheme | SEAL: each file sealed to the public keys of the Macs you approved on the device; no secrets stored on the device | Symmetric key in NVS; symmetric key in memory only | Without Secure Boot a key in NVS is as good as none; memory only means no recording after an offline reboot | §4.2, §4.3 |
| D3 | When | Small fixes, automated tests and the baseline round now; SEAL after the docs/18 v0.8 binding and the docs/24 Mac app package, with G-ENC passed first | Fold into v0.8 | With profiles, recipients need binding first; `cpa-key` goes into the docs/24 package; G-ENC has not been measured | §7 👤 |
| D4 | Format | X25519 (libsodium) + HKDF-SHA256 + AES-256-GCM, 4 KiB blocks, block AAD is the header hash; period key ≤1 hour, changed whenever recipients or profile change; at most 4 recipients per file (`cpa-seal/v1`, spec and vectors public) | age v1; AEAD switched to libsodium's ChaCha20-Poly1305 (G-ENC decides) | age's 64 KiB chunks need a 64 KiB buffer, which does not fit without PSRAM, and standard tools cannot read truncated streams; per file only HKDF and wrapping, so Enter and recording start never wait on X25519 | §4.3 |
| D5 | How the Mac decrypts | Swift `cpa-key` (CryptoKit, same package as the Mac app, separate target); the daemon still depends only on pyserial | ADR 0004 allows `cryptography`; external `age` | Zero third-party code; item 1 of ADR 0004 unchanged | §4.5 👤 |
| D6 | Where the private key lives | `$CPA_HOME/keys/seal/`, 0600; not exported; travels with Time Machine and Migration Assistant | Login Keychain (no prompts in the background, queue when it cannot open, foreground `cpa-key authorize`) | An ad-hoc signature changes on every build, so the Keychain would suddenly ask for the login password during background syncs; in an unencrypted Time Machine the two are about the same | §5.2 |
| D7 | Recipients | Macs you approved on the device: per profile with profiles, all of them with profile 0 or without profiles; at most 4 on the device; no recovery key, no export | Public keys only after binding (first draft); a recovery key by default; `cpa-key export` | The first draft was always plaintext "without profiles"; Time Machine and Migration Assistant carry the private key file; a recovery key and export both need you to remember a passphrase and manage a file | §4.3, §5.4 |
| D8 | When there is no key | Record in plaintext only when no Mac at all is registered on the device, or no Mac with a registered key is in the current profile, in today's format, never sealing to another profile's Macs; the System page always says "Encryption: on/off" or "<profile> has no key"; once SEAL has been on (`SEALON` on every storage), a fallback to plaintext gets a hint plus the "明" mark on the lock card, and the same when the recipient table is lost | Refuse to record | Recording always working matters more than encryption | §4.3 👤 |
| D9 | The device cannot see its own bodies; what about playback | 👤 choose one of three, default (a): encrypted recordings cannot be played back on the device | (b) the device is itself also a recipient: playback works, stops someone with only the card and Launcher browsing SD, does not stop a lost device; (c) only starred or the latest few stay plaintext | docs/13 v0.5 promised "listen to recordings away from the Mac", and docs/18 §15 item 2 only listed losing playback as a cost without deciding; (b) can be added later without changing the format | §4.3, §8.2 👤 |
| D10 | Authentication key (anti-forgery) | Do it, after SEAL (SEC-7); state that it only stops someone with only the card and impersonating boards that have never touched the device | Do it together with SEAL; don't do it | A key in NVS is readable by every host the device has been plugged into, even with one per Mac; the bulk of prompt injection is what people really say in meetings, handled by docs/15 §3.7 | §3.1, §7 |
| D11 | coredump | Release firmware, when idle and not recording or transferring, looks only if there is content: counts its own, not other firmware's; erases in 4 KiB pieces; test firmware follows `CPA_COREDUMP_KEEP` | Keep all; remove coredump from the partition table | The crash stack holds the text being typed; under Launcher the partition is shared; erasing Flash makes recordings drop audio | §4.4 |
| D12 | Leftovers after deletion | Rely on SEAL; no switch, seal whenever there is a key; old plaintext syncs as usual, then one prompt: clear internal storage on the device, `cpa sd scrub` for the card | Overwrite before deleting; format the SD card | The FTL makes overwriting unreliable; macOS formats large cards as exFAT, which IDF 4.4 cannot read, and it would delete Launcher's files | §4.3 |
| D13 | The archive on the Mac | FileVault + encrypted Time Machine; encrypted disk image documented only | Per-file encryption; build the disk image as a feature | ADR 0004's open files; the mount lifecycle would make cpad and cpa-stt unstable | §4.6 |
| D14 | Spotlight | Keep indexing | Exclude | You want to search; the index is inside FileVault; changing it means touching system settings | §4.6 |
| D15 | Forget / shred | Not now; "text kept forever" stays; state that `rec.hidden` only hides, never deletes | Build `cpa forget` (rewrite `events.jsonl`) | The append-only log is the root of the archive; changing it needs your decision first | §2.2 👤 |
| D16 | Body hashes in logs | In `cpad.log`, an HMAC with a per-Mac salt, field renamed `h8`; `sha8` in events unchanged | Unchanged; make `sha8` itself an HMAC | Unsalted, short notes can be guessed; `sha8` is an event field in docs/22 and docs/23 that `inbox_start` compares against | §4.6 |
| D17 | Untrusted USB hosts | State that SEAL only protects against passive reads; add a few cheap detections; blocking USB reset and JTAG while locked is parked as research, not in M-SEC | Research it now | JTAG can read memory without a reset, so blocking reset alone does nothing; neither bit is verified | §4.4 |
| D18 | Clipboard | As in docs/25 §5.3, D9 (which owns it): `ClipWriter` via ctypes, local only, marked concealed and transient, cleared after 2 minutes; this doc only checks it in M-SEC (S3-d) | — | Universal Clipboard and clipboard managers are both copies | §4.6 |
| D19 | How the test rounds run | The baseline round runs only the automated part, the simulator part (except S1-c), real-device coredump erasing and the heap baseline; the encryption round runs everything, with Flash reads and SD imaging in one script and one sudo; docs/27 §7.2 part 1 runs on its own after O0–O6, part 2 joins the encryption round; results in `docs/gates/sec.md` | Run everything in both rounds; only one round after encryption | That it is plaintext today is known and not worth your time to prove; only the encryption round has something to compare against | §6 |
| D20 | When the Mac cannot decrypt for the moment | Store the ciphertext in `$CPA_HOME/sealed/` (0600) first; once it is on disk, the hash matches and a self-tested key is among the recipients, ack or send `file.del`; decrypt separately | Do not ack when it cannot decrypt (first draft); add a `held` receipt | Not acking would block all 4 in-flight outbox slots and fill the card with recordings; `held` would change the protocol and device state | §4.3 |
| D21 | How the device trusts a Mac's public key | Accept it only when you approve on the device, pinned by `hh`; on every handshake the Mac proves it has the private key, and only then are sealed items handed over and its acks and deletes for sealed items accepted; a Mac with a pinned key is treated as "unassigned" before it proves (also when it sends no `seal`) and gets nothing; switching keys needs a proof with the old key, otherwise a confirmation on the device, cursor on "Don't switch" | Accept the public key for any known `host_id` and replace directly (first draft) | `host_id` is sent in plaintext to any board on the first handshake; with it one could reseal future records to oneself, and delete what one cannot read | §4.3 |
| D22 | After forgetting a Mac | One more line "Drop N items that cannot be opened" (cursor on "Keep", needs confirmation); items not dropped no longer count as waiting | Keep and count them forever | Otherwise they occupy the 200-item quota and card space forever, and block docs/18's "turn off profiles" forever | §5.4 |
| D23 | New hardware (Migration Assistant), `cpa host reset` | Renew `host_id`, and rotate SEAL to a new key (`cpa-key rotate`), keeping old keys for decryption only; to give a Mac away, `cpa host reset --wipe-keys` deletes the private keys | Carry the private key over unchanged | The new Mac does not use the old Mac's key as its in-use key. But the migrated old private key can still open what was sealed to the old Mac: run `cpa host reset` on the old Mac before using it for work, so that Home and Work are separated cryptographically | §5.3 |

### 8.2 What needs your call

1. **Device encryption and playback** (D2, D3, D9): **by default** SEAL comes after v0.8 (profiles) and the Mac app, with G-ENC passed first. The device cannot see the bodies it stores, so v0.5's promise "listen to recordings away from the Mac" needs one of three:
   - (a) **default**: encrypted recordings cannot be played back on the device;
   - (b) the device is itself also a recipient: playback works, it stops someone with only the card and Launcher browsing the SD card, but if the device is lost, the recordings are plaintext;
   - (c) only starred or the latest few stay plaintext, the rest encrypted.

   Going from (a) to (b) later does not change the format. Until SEAL is done, sync to zero at home before going to a work computer or someone else's.
2. **When there is no key** (D8): **by default** record in plaintext, with a mark; this only happens when you have not approved any Mac yet. Alternative: do not allow recording.
3. **Decryption on the Mac** (D5): **by default** the Swift `cpa-key`, built together with the Mac app, with ADR 0004 unchanged. Alternative: change the ADR to allow `cryptography`.
4. **Time Machine**: please check whether the backup disk says "Encrypted". After SEAL the private key file used for decryption is in the backup as well (D6); if it is not encrypted, whoever finds the backup disk gets a plaintext copy of the archive plus that key. Whether to exclude the development artefact directories (`backups/`, `firmware/hwtest/captures`) and the real-device test dumps from backups is also your call.
5. **"Forget"** (D15): should there be a way to really delete the body of a note? Today's rule is "text kept forever", and `events.jsonl` is append-only. **Default**: no.

## 9. How the three reviews were handled

All three reviews (engineering, attacker, feasibility) were checked against the code at `d330465`; the source references below were checked again for this doc. The "Disagreement" notes say how it was settled where reviews differed.

| Review | Said | How it changed |
|---|---|---|
| Engineering 1, feasibility 3 | Not acking when decryption fails blocks the outbox and fills the card | Store the ciphertext first, then decrypt (D20, §4.3). **Disagreement**: feasibility proposed a `held` receipt; settled on storing ciphertext locally, because it changes neither the protocol nor device state, and solves everything `held` would |
| Engineering 2, 3, attacker 11, feasibility 11 | A private key in the Keychain prompts in the background; conflicts with docs/27; after Migration Assistant two Macs share one key | Private key moved to a 0600 file (D6, §5.2); automatic rotation on new hardware, old keys decrypt only (D23); always generated at setup; one field name, no export (D7). **Disagreement**: feasibility proposed keeping the Keychain with no background prompts and foreground authorisation; settled on a file, because every rebuild would need you back at the Mac to click once, against "convenience and stability first", and the Keychain in an unencrypted Time Machine is protected only by the login password too; the Keychain approach is written up as the alternative. docs/27 (PR #67) originally said Keychain and was changed back to a file in `58ed685` (§5.5 item 6) |
| Engineering 4, attacker 3 | Always plaintext without profiles, with profile 0, or when NVS is lost | Registration separated from profiles; seal to everyone with profile 0 and without profiles; table mirrored in cpafs; plaintext only when not a single Mac is registered (D7, D8). **Disagreement**: engineering said show the "明" mark only on fallback to plaintext, attacker said every fallback after SEAL has been on must be flagged; merged into one: flag only fallbacks after SEAL has been on, and the System page always says on or off |
| Engineering 5, feasibility 14 | D9 dropped v0.5 playback, for the wrong reason | D9 changed to 👤 choose one of three (§8.2 item 1). **Disagreement**: feasibility mentioned "a SEAL switch per profile"; not adopted, because engineering 9 requires no switch, and (b), (c) already cover the cases that want playback |
| Engineering 6, attacker 6, feasibility 6 | Precomputed headers do not work (HKDF binds fid), and when they run out Enter and recording start stall; reuse would repeat nonces; X25519 cannot run in `rec_task` | Switched to a period key: per file only HKDF and wrapping (D4, §4.3); switched immediately when recipients or profile change; a fresh `fk` on every open; X25519 only in the loop; "recording right after binding" goes into G-ENC. **Disagreement**: attacker and feasibility proposed "one ephemeral key per file, pools per recipient"; settled on a period key, because the kind of attacker one-key-per-file cannot stop (reading memory over JTAG) is reading the mic buffer at the same time, and a period key is simplest and uses the least power |
| Engineering 7 | Old firmware moves encrypted items into `bad/` and deletes them; `enc` has two types | Encrypted items get a different file name; unsealed ones carry no `enc` (§4.3) |
| Engineering 8, attacker 4 | After forgetting, items sealed only to that Mac are stuck forever; "never loses data" goes too far | Added "Drop N items that cannot be opened", no longer counted as waiting (D22); stated honestly that if the key is really lost they cannot be opened (§5.4) |
| Engineering 9 | Formatting the card is dangerous; no SEAL switch; Launcher's `/bkp/` was missed | No switch; `cpa sd scrub` instead of formatting; `/bkp/` added to the map and S2-b (D12, §2.1, §4.3) |
| Engineering 10, attacker 13, feasibility 13 | `Umask` in the plist is inherited by child processes; does nothing for existing files; cpa-stt's plist is the same | No `Umask`; both `install()`s pre-create 0600 logs and chmod at startup (§4.6 item 1) |
| Engineering 11, attacker 13, feasibility 13 | Erroring on `spawn_env` when reading the config would stop cpad from starting; "keep the latest 5" deletes the original; `sha8` name clash | Changed to a `cpa doctor` warning, moved to SEC-1; keep the first plus the latest 2; field renamed `h8`, events unchanged (§4.6, D16). Feasibility's "no other code reads `sha8`" is also right; the rename only avoids the event field in docs/22 and docs/23 |
| Engineering 12 | The baseline round spends your time proving known facts | The baseline round runs only automated, simulator, S2-d and the heap baseline; S2-a to S2-c run once, in the encryption round only (D19, §6) |
| Engineering 13 | Under Launcher the coredump partition is shared | Count only our own by `app_elf_sha256`; never erase while recording or transferring (D11) |
| Engineering 14, feasibility 7 | One more recipient line exceeds 2048 B; escaped bodies double in size | At most 4; seal a binary blob; `test_worst_lines` adds sealed items (§4.3). **Disagreement**: engineering said 3 Macs per profile, feasibility 4 per file; settled on at most 4 on the device, so every file has ≤4 with or without profiles |
| Engineering 15 | The `json_read` fuzz target does not exist; add an "Encryption: on (sealed to …)" line | Removed; the line added to System → Encryption and to the Mac app's card |
| Attacker 1 | With `host_id` one can replace the public key the device stores, and any Mac can delete files it cannot read | Public key pinned, private key proved on every handshake, handover and deletion only after proof (D21, §4.3), consistent with docs/27 §3.5 items 1 and 2; corrected "copying `$CPA_HOME` does not get the binding" (§5.1). **Disagreement**: attacker also proposed "`file.del` carries a preimage known only after decryption"; not adopted, because with store-then-decrypt (D20) the Mac has to ack before decrypting, and proving the private key is already enough |
| Attacker 2, feasibility 10 | USB hosts that deliberately modify the device were not considered; USB-JTAG reads memory without a reset; research on blocking reset is pointless | The whole doc reworded to "only protects against passive reads"; a few cheap detections added (§4.4, T-USB). **Disagreement**: feasibility said delete the research item, attacker said widen it to USB-JTAG; settled on parking it with both bits written down, not in M-SEC (D17) |
| Attacker 5, feasibility 1 | `.M`'s plaintext fields are not authenticated; sealing `.M`'s fields would break power-loss recovery, `file.list` and the 512 B cap | `.M` seals nothing, `kMetaMax` becomes 768; the block AAD is the header hash, and the last block's flags carry "last segment", so the Mac detects a changed `.M` (§4.3). The two reviews' approaches are combined |
| Attacker 7, feasibility 5 | The seed is weakest only at cold boot offline; the host's salt is useless against the host that sent it; "never the same" is not "unguessable"; all-zero shared secrets | Seed taken once at the very start of `setup()`; mic noise mixed in; SP 800-90B estimation; both sides reject all zeros (§4.3) |
| Attacker 8 | D10's effect was overstated | D10 and T-CARD rewritten (§3.1, D10) |
| Attacker 9, feasibility 12 | Real-device dumps are as sensitive as `backups/`; a tone cannot be searched for byte by byte | Four dump-hygiene rules; a fixed PCM pattern for audio (§6.3) |
| Attacker 10 | After migration two Macs share one private key | Automatic rotation on new hardware (D23); when the device finds the same old key it asks whether to forget the old one (§5.3) |
| Attacker 11 | Several claims about the Keychain are unverified; export needs passphrase derivation | No Keychain by default (D6); no export, so the passphrase-derivation question goes away (D7) |
| Attacker 12 | Fuzzing missed two parsers | Added the SEAL header, the encrypted-segment recovery scan and `cpa-key`'s parsers (§6.1, §6.6) |
| Attacker 14 | T-CLOUD missed MCP clients' model providers and docs/25's cloud destinations | Added to §2.3, T-CLOUD, §3.3 |
| Feasibility 2 | 4 KiB blocks only hold for the first block; in-place encryption in the ring buffer is not possible; "fsync the header first" is too expensive | `RecWriter` gets a staging buffer; encrypt from the staging buffer; "fsync the header first" removed; `sha256` from byte 0 (§4.3) |
| Feasibility 4 | Erasing Flash while recording drops audio | Recipient table, counter, coredump and cleanup all wait for idle; coredump erased in pieces only when it has content (§4.3, §4.4). **Disagreement**: feasibility also proposed "remove coredump from the partition table"; not adopted, because it still needs erasing under Launcher, and one path is simpler |
| Feasibility 5 | GCM is not in hardware; libsodium is already linked and has ChaCha; age's real problem is the 64 KiB buffer; `bootloader_random` can only be used before the ADC | All corrected to the facts (§4.3, D4); ChaCha20-Poly1305 kept as G-ENC's fallback |
| Feasibility 8 | SEC-1 and SEC-5 depend on docs/24, which is not merged; G-ENC lacks a whole-app heap baseline; S2-a contradicts itself; the fuzzing environment | SEC-1b split out; SEC-3 measures the heap baseline; `CPA_COREDUMP_KEEP` added; a `clang++` script with the existing `fuzz-nightly.yml` (§6, §7) |
| Feasibility 9 | Code size underestimated | Firmware changed to 1,300 / 1,500, cpad 650 / 900 (§7) |
| Cross-check with docs/27 (2026-10-22) | The two docs were written in parallel; checked against main (`995821d`) of docs/18 and docs/25, this doc had a few gaps or inconsistencies | Only gaps filled and contradictions fixed: the recipient table keyed by `hh`, about 470 B (§4.3 "Where it is stored"); a Mac with a pinned key that sends no `seal` or does not prove is treated as "unassigned" and gets nothing; the device reports the pinned `kid` in `seal_chal{E′, n, kid}`, not in `prof.ok`; visible(H) applied to docs/18 §3.5 items 1–3 ("Who gets what"); a bound Mac with no registered key also gets the "Let … unlock" sheet, with "Add" in both modes; no key in the current profile means plaintext with "明", never sealed to another profile; `SEALON` and the table written to every storage, and the SD copy restored only after proof; fingerprint changed to the first 8 digits of `kid` and also shown on the Mac; all fields of `body` sealed, and `.M` gains docs/25's `dest`, `lock`; cpad reruns the self-test when `cpa-key` changes; when no `kid` is recognised, do not store or ack, per D20; `rotate` has no `--keep-old`, `cpa host reset` also rotates, `--wipe-keys` for giving a Mac away, and D23 states that separation needs the old Mac to run reset; the clipboard belongs to docs/25; the baseline round does not run S1-c; SEC-5 adds P8-F3; §6 and §7 add docs/27's new-Mac part; `host_label` defaults to `Mac·XXXX` and the example names follow; statements about network access made precise |

## Appendix: evidence index

- Firmware (`d330465`, unchanged on main): `fw/hal/nvs_boot.cpp:26-43`; `fw/hal/lfs_store.cpp:108`; `fw/hal/sd_card.cpp:202-204`; `fw/hal/vfs_rec_file_port.cpp:125-127`; `fw/hal/audio_task.h:2, 43`; `fw/rec/rec_engine.cpp:127-129, 251`; `fw/rec/rec_engine.h:78`; `fw/rec/rec_meta.h:17, 46, 52`; `fw/rec/rec_recovery.cpp:25-27`; `fw/rec/rec_recovery.h:48`; `fw/rec/wav_header.h:4, 38`; `fw/storage/sd_paths.h:1-15`; `fw/outbox/outbox_io.h:1-7`; `fw/outbox/outbox_core.h:1-7`; `fw/outbox/outbox_scan.h:8-11`; `fw/protocol/decode_device.cpp:11, 82`; `fw/shell/runtime_outbox.cpp:15-17`; `src/main.cpp:30, 76`; `platformio.ini:36`; `partitions/cpa_8MB.csv`; `test/core/test_rec_meta/test_main.cpp:104`.
- daemon: `cpad/service/launchd.py:20, 44, 50-60`; `stt/service.py:31-34`; `cpad/config/settings.py:3-6, 56-61, 86-89, 157-166`; `cpad/logging/redact.py:1-22`; `cpad/events/records.py:23-80`; `cpad/recordings/land.py:1-4`; `cpad/recordings/verify.py:7, 17, 37`; `cpad/recordings/retention.py:1, 25-28`; `cpad/agents/claude_code/hook_settings.py:5-8, 29`; `stt/runner.py:87-93`; `pyproject.toml` (`["pyserial==3.5"]`; dev dependencies include `hypothesis`).
- Protocol: `protocol/README.md:201, 224`; `protocol/tools/test_worst_lines.py:100`.
- SDK (`SDK/`): `qio_qspi/include/sdkconfig.h:424, 565, 567, 568, 603, 622`; `include/mbedtls/port/include/mbedtls/esp_config.h:127` (`MBEDTLS_GCM_ALT`); `include/nvs_flash/include/nvs_flash.h:191`; `include/bootloader_support/include/bootloader_random.h:28-40`; `include/espcoredump/include/esp_core_dump.h:41, 114, 134, 158`; `nm libnvs_flash.a`, `nm libmbedcrypto.a` (`esp_aes_gcm_crypt_and_tag`, `gcm_mult`, `mbedtls_ctr_drbg_seed`; no HKDF, no ChaCha), `nm liblibsodium.a` (`crypto_scalarmult_curve25519`, `crypto_aead_chacha20poly1305_ietf_encrypt`, `crypto_auth_hmacsha256`, `sodium_memzero`) [meas]; `RTC_CNTL_USB_CONF_REG`, `RTC_CNTL_USB_RESET_DISABLE` in `soc/rtc_cntl_reg.h`.
- Docs: SECURITY.md:9-11, 26, 28, 35, 41-42, 57, 62; docs/04:632 (S11), 751 (D16); docs/11:220-236, 242; docs/12:455; docs/13:38, 425, 695; docs/15:57, 520; docs/18 §3.2, §4.1–§4.3, §5.2–§5.3, §8, §9, §11, §13, §15, and :105, 223, 332, 568, 704; docs/21:88-91, 336; docs/22:611; docs/23 §6.1, :224, D10; docs/24 §7, D12 (`wt-mac`); docs/25 §2.3; docs/27 §3.5 (PR #67); docs/gates/launcher.md:15, 48, 50, 114, 138, 162, 203, 217; ADR 0004:57; recording contract v1 `segments[].sha256`; CI `ci.yml:105`, `release.yml:37`, `fuzz-nightly.yml`.
- This Mac [meas] (2026-10-22, read-only): `fdesetup status`; permissions and file names in `$CPA_HOME` and `~/CardputerNotes`; key names in `config.json`; `tmutil destinationinfo`, `tmutil isexcluded`; `mdfind -onlyin` (file names only); whether the iCloud directories exist; count and permissions of `firmware/hwtest/captures`.
