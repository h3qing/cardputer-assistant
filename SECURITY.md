**English** · [中文](SECURITY.zh-CN.md)

# Security

## Reporting a vulnerability

Please do not report vulnerabilities in public issues. Use GitHub's **Security → Report a vulnerability** (a private report). Say which versions are affected, how to reproduce it, and what you think the impact is. This is a project maintained in spare time; we will reply as soon as we can.

During v0.x, security fixes go into the latest version only.

## The v0.1 trust boundary

**A physical USB connection + the macOS user currently logged in.** Everything inside this boundary is trusted; everything outside it is not. In practice (details in [docs/04 §11](docs/04-prd.md) and [docs/05 §8](docs/05-architecture.md)):

| Measure | What it means |
|---|---|
| No network ports | The daemon listens on no TCP/UDP port. Local control only goes through `$CPA_HOME/cpad.sock` (AF_UNIX, permissions 0600). No cloud, no accounts, no telemetry |
| Only our own firmware | The `fw` in the handshake reply must start with `cpa-fw/`, otherwise the port is released within 6 seconds; after the first successful handshake the USB serial number is bound |
| The device cannot control commands | The device can only send project codes, text, and `qid`/`sid` values the daemon sent it. Paths, arguments and the permission mode are decided by the daemon from the registry |
| Execution only as online requests | Starting a session and replying can only happen while the link is Ready, through a confirm box, as an online request; nothing is persisted or retried automatically. The outbox only holds two kinds of item, `note` and `queue`; contract tests make sure every executing kind is invalid |
| No argument injection | Subprocesses take an argument list, never a shell; the body goes after `--` as one argument; a body that starts with `-` after trimming whitespace is refused outright; a `sid` must match exactly one session |
| Explicit permission mode | `--permission-mode` is always passed, `plan` by default; `bypassPermissions` is always refused; `auto` is used only when the project's config says so explicitly |
| Input checked on both ends | A whitelist of message types, one line ≤2048 bytes, a body ≤500 bytes, valid UTF-8, no control characters, ID formats (see [protocol/](protocol/)) |
| Errors leak no details | The device only gets an error code; `msg` is one English sentence giving the reason and never echoes the body, paths, command lines or command output |
| Redacted logs | Body fields are logged only as their length and the first 8 characters of their sha256; the text itself is logged only when `CPA_LOG_CONTENT=1` is set explicitly, and a warning is printed at startup |
| Local file permissions | `$CPA_HOME` is 0700 and its files 0600, checked when the daemon starts |
| Claude Code hooks (v0.4) | `cpa hooks install` changes `~/.claude/settings.json`: by default it only shows the change, and writes only with `--yes`; it only adds or removes exec-form handlers whose `command` is the absolute path of cpa and whose `args` start with `hook` (no shell), and leaves everything else as it was; before writing it backs up the old file to `$CPA_HOME/hook-backups/` (directory 0700, files 0600), not next to the settings file, so a copy of a project's `settings.local.json` cannot be committed by git. Hook input is treated as untrusted data: `cpa hook` reads at most 1 MiB and forwards only whitelisted fields (event name, session UUID, notification type, tool name, one reason word, whether it is a subagent, a cleaned excerpt of ≤120 bytes); prompts, tool arguments, paths and transcripts never leave the hook process; it only uses `cpad.sock` (permissions 0600), the daemon checks every field again and refuses anything that does not fit; hook content is never executed |
| No keys stored | There are no keys on the device and no hard-coded keys in the repo |

v0.1 **does no pairing**: a pairing code with no key behind it would be for show only. Any device that can be plugged into USB and pretends to be `cpa-fw/` can send notes and queue items as "the current Mac user", and, after confirmation, start sessions in `plan` mode. Given "physical USB = trust", this is accepted on purpose. Real pairing (an NVS key + an HMAC challenge) and an encrypted link come in v0.3, together with wireless.

## Known risks (accepted in v0.1)

### If the device is lost, items not yet synced are in plain text

- Notes and queue items in the outbox that have not received an `outbox.ack` are stored in plain text in LittleFS on the device's internal flash. Whoever has the device can read them out with esptool.
- We **do not turn on flash encryption or Secure Boot by default**: they are one-way, and once on, the community's flashing tools no longer work.
- Mitigations:
  - an item is deleted as soon as its ack arrives, so only what has not synced stays on the device;
  - the status bar shows how many items are waiting to sync (↑N), so you know how much plain text is still on the device;
  - session information is kept only in memory, and the device never gets project paths;
  - items the daemon judged `rejected` stay in `/outbox/bad/` (at most 20), also in plain text.
- **From v0.2**: with an SD card in, items waiting to sync are stored under `/cpa/` on the card, also in plain text; and the SD card can simply be pulled out and read with a card reader. Encryption (sealing each file to the Mac's public key) and a PIN lock come later; until then, treat a device with an SD card as plain-text storage.
- To evaluate for v0.2: an optional PIN lock; SD card records each sealed with the Mac's public key.
- **Kept copies (docs/28 S1a)**: once a recording is delivered to a Mac, the device no longer deletes it at once but **keeps a copy** for 7 days (the original `.W` and `.M` under `/cpa/rec/<E7>/`, plus `SENT.L`, which records which Mac each was delivered to), so it can be delivered again when it went to the wrong Mac. So `↑N` and "R N" (录N) no longer stand for all the plain text on the device:
  - the lock card and Settings → Storage show the number of kept segments (`kept 3` (`留3`)) and how many days old the oldest is;
  - before going to a Mac or a place you do not trust: first sync to zero, then "Clear kept" (清掉留底) in Settings → Storage (a confirm box with the cursor on Cancel); clearing only deletes the files, and the card's free clusters can still be read, so erase the whole card with a card reader when you need to;
  - each record in `SENT.L` holds a 4-byte hash of the receiving Mac's `host_id` (not the `host_id` itself); "Clear kept" also compacts the ledger down to a single header;
  - until encryption (docs/26 SEAL) is built, kept copies stay in plain text.

### The lock screen is not a security lock (v0.4.0)

The Opt+L lock screen ([docs/04 §8.8](docs/04-prd.md)) has only two purposes: stray presses do nothing while the device is in a pocket, and people nearby cannot see what is on the screen. It is not access control:

- **No PIN, no password.** Anyone who has the device can press Opt+L, then Enter within 3 seconds, and it is unlocked.
- **Nothing is encrypted**; what is stored is exactly the same before and after locking, all of it in plain text:
  - on the device: notes and queue items not yet synced (internal flash or `/cpa/` on the SD card), recordings on the SD card not yet transferred to the Mac, and the kept copies after delivery (docs/28);
  - on the Mac: the daily notes, recordings, transcripts and summaries in `~/CardputerNotes/`.
- **Only counts on screen while locked**: the lock card shows only the time, the date, the battery, how many need you, the number of recording segments waiting to sync and ↑N; no project names, session names or body text. Notices that would show project or session names (such as "needs you") do not pop up while locked.
- **A meeting recording does not stop while locked**: the screen goes to its dimmest, and the top-left corner always shows ● and the timer, so nothing is recorded quietly.
- **Recording a memo while locked takes two steps**: first tap G0 to light the lock card, then hold G0 while the card is lit. Holding G0 while the screen is black only lights the screen and does not record, so pressing G0 in a pocket cannot record people's conversations. This guards against recording by accident; it is not access control: whoever has the device can still record this way.
- v0.4.0 adds no data path that leaves the device or the Mac. The protocol only gains the optional field `ui{menu, locked}` in the test firmware's `debug.state`; the release firmware does not recognise any `debug.*` message (it replies `E_UNKNOWN_TYPE`).

Real protection (a PIN lock, SD card records each sealed with the Mac's public key) is still the later work described above. Until then, treat the device and the SD card as plain-text storage.

### Other

- `cpad.lock` and the exclusive serial port lock are advisory only; they cannot stop another program from opening the serial port. Run `cpa pause` before flashing.
- A program that can run code as the current user can already do everything the daemon can; v0.1 does not defend against malicious processes under the same user.
- Sessions started from the device run in the project's main directory (not a separate worktree) and start in `plan` mode; switching a project to `acceptEdits` or `auto` means anyone who "has the device and confirms" can change code in that project.
